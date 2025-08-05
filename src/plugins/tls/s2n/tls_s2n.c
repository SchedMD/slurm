/*****************************************************************************\
 *  tls_s2n.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <s2n.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/interfaces/certgen.h"
#include "src/interfaces/certmgr.h"
#include "src/interfaces/conn.h"

/* Set default security policy to FIPS-compliant version */
#define DEFAULT_S2N_SECURITY_POLICY "20230317"
#define CTIME_STR_LEN 72

const char plugin_name[] = "s2n tls plugin";
const char plugin_type[] = "tls/s2n";
const uint32_t plugin_id = TLS_PLUGIN_S2N;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Default client/server s2n_config objects used for most connections */
static struct s2n_config *client_config = NULL;
static struct s2n_config *server_config = NULL;

/*
 * If non-NULL, own_cert_and_key was successfully loaded into
 * both server_config/client_config
 */
static struct s2n_cert_chain_and_key *own_cert_and_key = NULL;

static char *own_cert = NULL;
static uint32_t own_cert_len = 0;
static char *own_key = NULL;
static uint32_t own_key_len = 0;

static bool is_own_cert_loaded = false;
static bool is_own_cert_trusted_by_ca = false;

static pthread_rwlock_t s2n_conf_lock = PTHREAD_RWLOCK_INITIALIZER;

static uint32_t s2n_conf_conn_cnt = 0;
static pthread_mutex_t s2n_conf_cnt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s2n_conf_cnt_cond = PTHREAD_COND_INITIALIZER;

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	int input_fd;
	int output_fd;
	bool maybe;
	struct s2n_connection *s2n_conn;
	/* absolute time shutdown() delayed until (instead of sleep()ing) */
	timespec_t delay;
	struct s2n_config *s2n_config;
	struct s2n_cert_chain_and_key *cert_and_key;
	bool do_graceful_shutdown;
	bool using_global_s2n_conf;
	bool is_client_authenticated;
} tls_conn_t;

/*
 * Handle and log a libs2n function failing
 * IN conn - ptr to connection or NULL
 * IN func - function that failed
 */
#define on_s2n_error(conn, func) \
	_on_s2n_error(conn, (void *(*)(void)) func, XSTRINGIFY(func), __func__)

static void _on_s2n_error(tls_conn_t *conn, void *(*func_ptr)(void),
			  const char *funcname, const char *caller)
{
	/* Save errno now in case error() clobbers it */
	const int orig_errno = errno;
	const int error_type = s2n_error_get_type(s2n_errno);

	/*
	 * Per libs2n docs:
	 *	After s2n_recv() or s2n_negotiate() return an error, the
	 *	application must call s2n_connection_get_delay() and pause
	 *	activity on the connection for the specified number of
	 *	nanoseconds before calling s2n_shutdown(), close(), or
	 *	shutdown()
	 */
	if ((func_ptr == (void *) s2n_negotiate) ||
	    (func_ptr == (void *) s2n_recv)) {
		timespec_t delay = { 0 };

		if ((delay.tv_nsec =
			     s2n_connection_get_delay(conn->s2n_conn))) {
			const timespec_t now = timespec_now();

			delay = timespec_normalize(delay);

			if (timespec_is_after(conn->delay, now))
				conn->delay = timespec_add(conn->delay, delay);
			else
				conn->delay = timespec_add(now, delay);

			if (slurm_conf.debug_flags & DEBUG_FLAG_TLS) {
				char str[CTIME_STR_LEN];

				timespec_ctime(conn->delay, true, str,
					       sizeof(str));

				log_flag(TLS, "%s: %s() failed %s[%d] requiring shutdown() be delayed until %s",
					 caller, funcname,
					 s2n_strerror_name(s2n_errno),
					 s2n_errno, str);
			}
		}
	}

	if (error_type == S2N_ERR_T_ALERT) {
		int alert = S2N_ERR_T_OK;

		if (conn)
			alert = s2n_connection_get_alert(conn->s2n_conn);
		else
			fatal_abort("%s: s2n alert without connection",
				    __func__);

		xassert(alert != S2N_ERR_T_OK);

		error("%s: %s() alerted %s[%d]: %s -> %s",
		      caller, funcname, s2n_strerror_name(alert), alert,
		      s2n_strerror(alert, NULL),
		      s2n_strerror_debug(alert, NULL));
	} else if (!conn->maybe) {
		xassert(s2n_errno != S2N_ERR_T_OK);

		error("%s: %s() failed %s[%d]: %s -> %s",
		      caller, funcname, s2n_strerror_name(s2n_errno), s2n_errno,
		      s2n_strerror(s2n_errno, NULL),
		      s2n_strerror_debug(s2n_errno, NULL));
	}

	if (!conn->maybe && (slurm_conf.debug_flags & DEBUG_FLAG_TLS) &&
	    s2n_stack_traces_enabled())
		s2n_print_stacktrace(log_fp());

	/* Map the s2n error to a Slurm error (as closely as possible) */
	switch (s2n_error_get_type(s2n_errno)) {
	case S2N_ERR_T_BLOCKED:
		errno = EWOULDBLOCK;
		break;
	case S2N_ERR_T_CLOSED:
		errno = SLURM_COMMUNICATIONS_SHUTDOWN_ERROR;
		break;
	case S2N_ERR_T_IO:
		/* I/O errors should set errno */
		if (orig_errno)
			errno = orig_errno;
		else
			errno = EIO;
		break;
	case S2N_ERR_T_PROTO:
		errno = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		break;
	case S2N_ERR_T_ALERT:
		errno = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		break;
	default:
		errno = SLURM_ERROR;
		break;
	}

	/* Per library docs:
	 *	NOTE: To avoid possible confusion, s2n_errno should be cleared
	 *	after processing an error
	 */
	s2n_errno = S2N_ERR_T_OK;
}

static int _check_file_permissions(const char *path, int bad_perms,
				   bool check_owner)
{
	struct stat statbuf;

	xassert(path);

	if (stat(path, &statbuf)) {
		debug("%s: cannot stat '%s': %m", plugin_type, path);
		return ENOENT;
	}

	/*
	 * Configless operation means slurm_user_id is 0.
	 * Avoid an incorrect warning if the key is actually owned by the
	 * (currently unknown) SlurmUser. (Although if you're running with
	 * SlurmUser=root, this warning will be skipped inadvertently.)
	 */
	if (check_owner && (statbuf.st_uid != 0) && slurm_conf.slurm_user_id &&
	    (statbuf.st_uid != slurm_conf.slurm_user_id)) {
		debug("%s: '%s' owned by uid=%u, instead of SlurmUser(%u) or root",
		      plugin_type, path, statbuf.st_uid,
		      slurm_conf.slurm_user_id);
		return SLURM_ERROR;
	}

	if (statbuf.st_mode & bad_perms) {
		debug("%s: file is insecure: '%s' mode=0%o",
		      plugin_type, path, statbuf.st_mode & 0777);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Note: function signature and returns are dictated by s2n library.
 * Return 1 to trust that hostname or 0 to not trust the hostname.
 */
static uint8_t _verify_hostname(const char *host_name, size_t host_name_len,
			        void *data)
{
	return 1;
}

static struct s2n_config *_create_config(void)
{
	struct s2n_config *new_conf = NULL;
	char *security_policy = NULL;

	if (xstrstr(slurm_conf.tls_params, "load_system_certificates")) {
		if (!(new_conf = s2n_config_new())) {
			on_s2n_error(NULL, s2n_config_new);
			return NULL;
		}
	} else {
		if (!(new_conf = s2n_config_new_minimal())) {
			on_s2n_error(NULL, s2n_config_new_minimal);
			return NULL;
		}
	}

	/*
	 * Get security policy version.
	 *
	 * https://aws.github.io/s2n-tls/usage-guide/ch06-security-policies.html
	 */
	security_policy = conf_get_opt_str(slurm_conf.tls_params,
					   "security_policy_version=");

	if (!security_policy)
		security_policy = xstrdup(DEFAULT_S2N_SECURITY_POLICY);

	if (s2n_config_set_cipher_preferences(new_conf, security_policy) < 0) {
		on_s2n_error(NULL, s2n_config_set_cipher_preferences);
		xfree(security_policy);
		(void) s2n_config_free(new_conf);
		return NULL;
	}
	xfree(security_policy);

	if (s2n_config_set_verify_host_callback(new_conf, _verify_hostname,
						NULL) < 0) {
		on_s2n_error(NULL, s2n_config_set_verify_host_callback);
		(void) s2n_config_free(new_conf);
		return NULL;
	}

	/*
	 * Per libs2n docs:
	 *   Server behavior:
	 *   - Optional: Request the client's certificate and validate it. If no
	 *     certificate is received then no validation is performed.
	 *   Client behavior:
	 *   - Optional (default): Sends the client certificate if the server
	 *     requests client authentication. No certificate is sent if the
	 *     application hasn't provided a certificate.
	 */
	if (s2n_config_set_client_auth_type(new_conf, S2N_CERT_AUTH_OPTIONAL)) {
		on_s2n_error(NULL, s2n_config_set_client_auth_type);
		return NULL;
	}

	return new_conf;
}

static char *_get_ca_cert_file_from_conf(void)
{
	char *cert;

	/* Get explicit path configuration */
	if ((cert = conf_get_opt_str(slurm_conf.tls_params, "ca_cert_file=")))
		return cert;

	/* Try to find default path */
	if ((cert = get_extra_conf_path("ca_cert.pem")))
		return cert;

	return NULL;
}

static int _add_ca_cert_to_config(struct s2n_config *config, char *cert_file)
{
	buf_t *cert_buf;

	/*
	 * Check if CA cert is owned by SlurmUser/root and that it's not
	 * modifiable/executable by everyone.
	 */
	if (_check_file_permissions(cert_file, (S_IWOTH | S_IXOTH), true)) {
		return SLURM_ERROR;
	}

	if (!(cert_buf = create_mmap_buf(cert_file))) {
		error("%s: Could not load CA cert file (%s)",
		      plugin_type, cert_file);
		return SLURM_ERROR;
	}

	if (s2n_config_add_pem_to_trust_store(config, cert_buf->head)) {
		on_s2n_error(NULL, s2n_config_add_pem_to_trust_store);
		FREE_NULL_BUFFER(cert_buf);
		return SLURM_ERROR;
	}
	FREE_NULL_BUFFER(cert_buf);

	return SLURM_SUCCESS;
}

static int _create_cert_and_key(struct s2n_cert_chain_and_key **cert_and_key,
				char *cert_pem, uint32_t cert_pem_len,
				char *key_pem, uint32_t key_pem_len)
{
	xassert(cert_and_key);

	if (*cert_and_key &&
	    (s2n_cert_chain_and_key_free(*cert_and_key) != S2N_SUCCESS)) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_free);
	}
	*cert_and_key = NULL;

	if (!(*cert_and_key = s2n_cert_chain_and_key_new())) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_new);
		return SLURM_ERROR;
	}

	if (s2n_cert_chain_and_key_load_pem_bytes(*cert_and_key,
						  (uint8_t *) cert_pem,
						  cert_pem_len,
						  (uint8_t *) key_pem,
						  key_pem_len) < 0) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_load_pem_bytes);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _add_own_cert_to_config(struct s2n_config *s2n_config,
				   struct s2n_cert_chain_and_key **cert_and_key)
{
	/*
	 * Per libs2n docs:
	 *	It is not recommended to free or modify the `cert_key_pair` as
	 *	any subsequent changes will be reflected in the config.
	 */
	if (s2n_config_add_cert_chain_and_key_to_store(s2n_config,
						       *cert_and_key)) {
		on_s2n_error(NULL, s2n_config_add_cert_chain_and_key_to_store);
		goto fail;
	}

	return SLURM_SUCCESS;
fail:
	if (*cert_and_key &&
	    (s2n_cert_chain_and_key_free(*cert_and_key) != S2N_SUCCESS)) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_free);
	}
	*cert_and_key = NULL;

	return SLURM_ERROR;
}

static int _reset_global_conf(struct s2n_config **config)
{
	int rc = SLURM_ERROR;
	char *cert_file = NULL;

	if (*config && s2n_config_free(*config))
		on_s2n_error(NULL, s2n_config_free);

	if (!(*config = _create_config()))
		goto end;
	if (!(cert_file = _get_ca_cert_file_from_conf()))
		goto end;
	if (_add_ca_cert_to_config(*config, cert_file))
		goto end;
	if (_add_own_cert_to_config(*config, &own_cert_and_key))
		goto end;

	rc = SLURM_SUCCESS;
end:
	xfree(cert_file);

	return rc;
}

static int _add_cert_to_global_config(char *cert_pem, uint32_t cert_pem_len,
				      char *key_pem, uint32_t key_pem_len,
				      bool server_only)
{
	if (!own_cert_and_key) {
		is_own_cert_loaded = true;

		if (_create_cert_and_key(&own_cert_and_key, cert_pem,
					 cert_pem_len, key_pem, key_pem_len))
			return SLURM_ERROR;
		if (_add_own_cert_to_config(server_config, &own_cert_and_key))
			return SLURM_ERROR;
		if (!server_only &&
		    _add_own_cert_to_config(client_config, &own_cert_and_key))
			return SLURM_ERROR;

		return SLURM_SUCCESS;
	}

	/* Stop new connections from using current server_config */
	slurm_rwlock_wrlock(&s2n_conf_lock);

	log_flag(TLS, "%s: Updating global server_conf with new certificate and key now...", plugin_type);

	/* Wait until connections using server_config are finished */
	slurm_mutex_lock(&s2n_conf_cnt_lock);
	if (s2n_conf_conn_cnt)
		slurm_cond_wait(&s2n_conf_cnt_cond, &s2n_conf_cnt_lock);

	if (_create_cert_and_key(&own_cert_and_key, cert_pem, cert_pem_len,
				 key_pem, key_pem_len))
		goto fail;

	/*
	 * s2n_config_free_cert_chain_and_key not enough to reset the
	 * certificate associated with server_config. server_config must be
	 * recreated.
	 */
	if (_reset_global_conf(&server_config))
		goto fail;
	if (!server_only && _reset_global_conf(&client_config))
		goto fail;

	slurm_mutex_unlock(&s2n_conf_cnt_lock);
	log_flag(TLS, "%s: Successfully updated global server_conf with new certificate and key", plugin_type);
	slurm_rwlock_unlock(&s2n_conf_lock);

	return SLURM_SUCCESS;
fail:
	slurm_mutex_unlock(&s2n_conf_cnt_lock);
	log_flag(TLS, "%s: Failed to update global server_conf with new certificate and key", plugin_type);
	slurm_rwlock_unlock(&s2n_conf_lock);

	return SLURM_ERROR;
}

static char *_get_cert_or_key_path(char *conf_opt, char *default_path)
{
	char *file_path = NULL;

	file_path = conf_get_opt_str(slurm_conf.tls_params, conf_opt);

	if (!file_path)
		file_path = get_extra_conf_path(default_path);

	/* Expand %h and %n in path for slurmd */
	if (running_in_slurmd() && conf) {
		char *tmp;
		slurm_conf_lock();
		tmp = slurm_conf_expand_slurmd_path(file_path, conf->node_name,
						    NULL);
		slurm_conf_unlock();
		xfree(file_path);
		file_path = tmp;
	}

	return file_path;
}

static int _add_cert_from_file_to_server(void)
{
	int rc = SLURM_SUCCESS;
	char *cert_file = NULL, *key_file = NULL;
	char *cert_conf = NULL, *key_conf = NULL;
	char *default_cert_path = NULL, *default_key_path = NULL;
	buf_t *cert_buf = NULL, *key_buf = NULL;
	bool check_owner = true;

	if (running_in_slurmdbd()) {
		cert_conf = "dbd_cert_file=";
		key_conf = "dbd_cert_key_file=";
		default_cert_path = "dbd_cert.pem";
		default_key_path = "dbd_cert_key.pem";
	} else if (running_in_slurmrestd()) {
		cert_conf = "restd_cert_file=";
		key_conf = "restd_cert_key_file=";
		default_cert_path = "restd_cert.pem";
		default_key_path = "restd_cert_key.pem";
		check_owner = false;
	} else if (running_in_slurmctld()) {
		cert_conf = "ctld_cert_file=";
		key_conf = "ctld_cert_key_file=";
		default_cert_path = "ctld_cert.pem";
		default_key_path = "ctld_cert_key.pem";
	} else if (running_in_slurmd()) {
		cert_conf = "slurmd_cert_file=";
		key_conf = "slurmd_cert_key_file=";
		default_cert_path = "slurmd_cert.pem";
		default_key_path = "slurmd_cert_key.pem";
	} else if (running_in_sackd()) {
		cert_conf = "sackd_cert_file=";
		key_conf = "sackd_cert_key_file=";
		default_cert_path = "sackd_cert.pem";
		default_key_path = "sackd_cert_key.pem";
	} else {
		return SLURM_ERROR;
	}

	if (!(cert_file = _get_cert_or_key_path(cert_conf, default_cert_path))) {
		error("Failed to get %s path", default_cert_path);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	/*
	 * Check if our public certificate is owned by SlurmUser/root (unless
	 * running in slurmrestd) and that it's not modifiable/executable by
	 * everyone.
	 */
	if ((rc = _check_file_permissions(cert_file, (S_IWOTH | S_IXOTH),
					  check_owner))) {
		/*
		 * If no static certificate was found, get a signed one from
		 * slurmctld later.
		 */
		if ((rc == ENOENT) &&
		    (running_in_slurmd() || running_in_sackd()))
			rc = SLURM_SUCCESS;

		goto cleanup;
	}
	if (!(cert_buf = create_mmap_buf(cert_file))) {
		error("%s: Could not load cert file (%s): %m",
		      plugin_type, cert_file);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	xfree(cert_file);

	if (!(key_file = _get_cert_or_key_path(key_conf, default_key_path))) {
		error("Failed to get %s path", default_key_path);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	/*
	 * Check if our private key is owned by SlurmUser/root (unless running
	 * in slurmrestd) and that it's not readable/writable/executable by
	 * everyone.
	 */
	if (_check_file_permissions(key_file, S_IRWXO, check_owner)) {
		rc = SLURM_ERROR;
		goto cleanup;
	}
	if (!(key_buf = create_mmap_buf(key_file))) {
		error("%s: Could not load private key file (%s): %m",
		      plugin_type, key_file);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	rc = _add_cert_to_global_config(cert_buf->head, cert_buf->size,
					key_buf->head, key_buf->size, false);

cleanup:
	xfree(key_file);
	xfree(cert_file);
	FREE_NULL_BUFFER(cert_buf);
	FREE_NULL_BUFFER(key_buf);

	return rc;
}

extern int tls_p_load_self_signed_cert(void)
{
	if (certgen_g_self_signed(&own_cert, &own_key) || !own_cert ||
	    !own_key) {
		error("Failed to generate self signed certificate and private key");
		return SLURM_ERROR;
	}

	own_cert_len = strlen(own_cert);
	own_key_len = strlen(own_key);

	/* Don't add self-signed cert to client_config */
	return _add_cert_to_global_config(own_cert, own_cert_len, own_key,
					  own_key_len, true);
}


extern int init(void)
{
	static bool init_run = false;

	if (init_run)
		return SLURM_SUCCESS;
	init_run = true;

	if (s2n_init() != S2N_SUCCESS) {
		on_s2n_error(NULL, s2n_init);
		return errno;
	}

#ifdef NDEBUG
	/* Always disable backtraces unless compiled in developer mode */
	s2n_stack_traces_enabled_set(false);
#else
	/* Only enable backtraces with TLS debugflag active */
	s2n_stack_traces_enabled_set(slurm_conf.debug_flags & DEBUG_FLAG_TLS);
#endif

	/* Create client s2n_config */
	if (!(client_config = _create_config())) {
		error("Could not create client configuration for s2n");
		return errno;
	}

	/* Create server s2n_config */
	if (!(server_config = _create_config())) {
		error("Could not create server configuration for s2n");
		return errno;
	}

	/*
	 * Description of OpenSSL versions:
	 * https://docs.openssl.org/1.1.1/man3/OPENSSL_VERSION_NUMBER/#description
	 */
	debug("Initialized %s. DEBUG_FLAG_TLS:%s s2n_stack_traces_enabled:%s s2n_get_openssl_version:0x%09zx",
		 plugin_type,
		 BOOL_STRINGIFY(slurm_conf.debug_flags & DEBUG_FLAG_TLS),
		 BOOL_STRINGIFY(s2n_stack_traces_enabled()),
		 s2n_get_openssl_version());

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	static bool fini_run = false;

	if (fini_run)
		return SLURM_SUCCESS;
	fini_run = true;

	if (s2n_config_free(client_config))
		on_s2n_error(NULL, s2n_config_free);

	if (server_config &&
	    s2n_config_free_cert_chain_and_key(server_config)) {
		on_s2n_error(NULL, s2n_config_free_cert_chain_and_key);
	}

	if (own_cert_and_key &&
	    (s2n_cert_chain_and_key_free(own_cert_and_key) != S2N_SUCCESS))
		on_s2n_error(NULL, s2n_cert_chain_and_key_free);

	if (s2n_config_free(server_config))
		on_s2n_error(NULL, s2n_config_free);

	if (s2n_cleanup_final())
		on_s2n_error(NULL, s2n_cleanup_final);

	xfree(own_cert);
	xfree(own_key);

	return SLURM_SUCCESS;
}

static int _negotiate(tls_conn_t *conn)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;

	if (s2n_negotiate(conn->s2n_conn, &blocked) != S2N_SUCCESS) {
		if (s2n_error_get_type(s2n_errno) == S2N_ERR_T_BLOCKED) {
			/* Avoid calling on_s2n_error for blocking */
			return EWOULDBLOCK;
		} else {
			on_s2n_error(conn, s2n_negotiate);
			return errno;
		}
	}

	if (s2n_connection_client_cert_used(conn->s2n_conn) == 1)
		conn->is_client_authenticated = true;

	if (slurm_conf.debug_flags & DEBUG_FLAG_TLS) {
		const char *cipher = NULL;
		uint8_t first = 0, second = 0;
		if (!(cipher = s2n_connection_get_cipher(conn->s2n_conn))) {
			on_s2n_error(conn, s2n_connection_get_cipher);
		}
		if (s2n_connection_get_cipher_iana_value(conn->s2n_conn, &first,
							 &second) < 0) {
			on_s2n_error(conn,
				     s2n_connection_get_cipher_iana_value);
		}
		log_flag(TLS, "%s: cipher suite:%s, {0x%02X,0x%02X}%s. fd:%d->%d.",
			 plugin_type, cipher, first, second,
			 conn->is_client_authenticated ?
				 " Client is authenticated (mTLS)" :
				 "",
			 conn->input_fd, conn->output_fd);
	}

	return SLURM_SUCCESS;
}

extern int tls_p_load_ca_cert(char *cert_file)
{
	int rc;
	bool free_cert = false;

	if (!cert_file) {
		if (!(cert_file = _get_ca_cert_file_from_conf()))
			return SLURM_ERROR;
		free_cert = true;
	}

	rc = (_add_ca_cert_to_config(client_config, cert_file) ||
	      _add_ca_cert_to_config(server_config, cert_file));

	if (free_cert)
		xfree(cert_file);

	return rc;
}

extern char *tls_p_get_own_public_cert(void)
{
	log_flag(AUDIT_TLS, "Returning own public cert: \n%s", own_cert);

	return xstrdup(own_cert);
}

extern int tls_p_load_own_cert(char *cert, uint32_t cert_len, char *key,
			       uint32_t key_len)
{
	if (!cert) {
		if (_add_cert_from_file_to_server())
			return SLURM_ERROR;
		goto cert_loaded;
	}

	xfree(own_cert);
	xfree(own_key);

	/* Save certificate details for later */
	own_cert = xstrdup(cert);
	own_cert_len = cert_len;
	own_key = xstrdup(key);
	own_key_len = key_len;

	if (_add_cert_to_global_config(cert, cert_len, key, key_len, false)) {
		error("%s: Could not add certificate and private key to s2n_config.",
		      plugin_type);
		return SLURM_ERROR;
	}

cert_loaded:
	log_flag(TLS, "Successfully loaded signed certificate into TLS store.");

	is_own_cert_trusted_by_ca = true;

	return SLURM_SUCCESS;
}

extern bool tls_p_own_cert_loaded(void)
{
	return is_own_cert_loaded;
}

static void _s2n_config_inc(tls_conn_t *conn)
{
	slurm_mutex_lock(&s2n_conf_cnt_lock);
	s2n_conf_conn_cnt++;
	slurm_mutex_unlock(&s2n_conf_cnt_lock);
}

static void _s2n_config_dec(tls_conn_t *conn)
{
	slurm_mutex_lock(&s2n_conf_cnt_lock);

	if (s2n_conf_conn_cnt > 0) {
		s2n_conf_conn_cnt--;
	} else {
		error("%s: unexpected underflow", __func__);
	}

	if (s2n_conf_conn_cnt == 0) {
		slurm_cond_signal(&s2n_conf_cnt_cond);
	}

	slurm_mutex_unlock(&s2n_conf_cnt_lock);
}

static void _cleanup_tls_conn(tls_conn_t *conn)
{
	if (conn->s2n_config && (conn->s2n_config != server_config) &&
	    (conn->s2n_config != client_config))
		if (s2n_config_free(conn->s2n_config))
			on_s2n_error(conn, s2n_config_free);

	if (conn->cert_and_key &&
	    (s2n_cert_chain_and_key_free(conn->cert_and_key) != S2N_SUCCESS))
		on_s2n_error(conn, s2n_cert_chain_and_key_free);

	if (conn->s2n_conn && s2n_connection_free(conn->s2n_conn) < 0)
		on_s2n_error(conn, s2n_connection_free);

	if (conn->using_global_s2n_conf)
		_s2n_config_dec(conn);

	if (s2n_stack_traces_enabled())
		s2n_free_stacktrace();
}

static int _set_conn_s2n_conf(tls_conn_t *conn,
			      const conn_args_t *tls_conn_args)
{
	char *cert_file = NULL;
	bool is_server = (tls_conn_args->mode == TLS_CONN_SERVER);

	if (!slurm_rwlock_tryrdlock(&s2n_conf_lock)) {
		if (is_server && !own_cert_and_key) {
			error("%s: No own certificate has been loaded yet, cannot create connection for fd:%d->%d",
			      __func__, tls_conn_args->input_fd,
			      tls_conn_args->output_fd);
			slurm_rwlock_unlock(&s2n_conf_lock);
			return SLURM_ERROR;
		}
		_s2n_config_inc(conn);

		if (is_server)
			conn->s2n_config = server_config;
		else
			conn->s2n_config = client_config;

		conn->using_global_s2n_conf = true;
		slurm_rwlock_unlock(&s2n_conf_lock);
		return SLURM_SUCCESS;
	}

	/*
	 * Create a new s2n_config with the same certificate while the
	 * old one is getting updated.
	 */
	log_flag(TLS, "%s: global s2n_config is being updated, creating new s2n_config for conn to fd:%d->%d",
		plugin_type, tls_conn_args->input_fd, tls_conn_args->output_fd);
	if (!own_cert || !own_cert_len || !own_key || !own_key_len) {
		error("%s: Global s2n_config is busy being updated and there's no saved certificate/key to create a temporary s2n_config for conn to fd:%d->%d",
		      __func__, tls_conn_args->input_fd,
		      tls_conn_args->output_fd);
		return SLURM_ERROR;
	}
	if (!(conn->s2n_config = _create_config())) {
		error("Could not create s2n_config for fd:%d->%d",
		      tls_conn_args->input_fd, tls_conn_args->output_fd);
		return SLURM_ERROR;
	}

	if (_create_cert_and_key(&conn->cert_and_key, own_cert, own_cert_len,
				 own_key, own_key_len)) {
		error("Could not cert and key pair for fd:%d->%d",
		      tls_conn_args->input_fd, tls_conn_args->output_fd);
		return SLURM_ERROR;
	}
	if (_add_own_cert_to_config(conn->s2n_config, &conn->cert_and_key)) {
		error("Could not add certificate to s2n_config for fd:%d->%d",
		      tls_conn_args->input_fd, tls_conn_args->output_fd);
		return SLURM_ERROR;
	}
	if (!(cert_file = _get_ca_cert_file_from_conf())) {
		error("Could not get CA certificate file for s2n_config for fd:%d->%d",
		      tls_conn_args->input_fd, tls_conn_args->output_fd);
		return SLURM_ERROR;
	}
	if (_add_ca_cert_to_config(conn->s2n_config, cert_file)) {
		error("Could not add certificate to s2n_config for fd:%d->%d",
		      tls_conn_args->input_fd, tls_conn_args->output_fd);
		xfree(cert_file);
		return SLURM_ERROR;
	}
	xfree(cert_file);
	return SLURM_SUCCESS;
}

extern void *tls_p_create_conn(const conn_args_t *tls_conn_args)
{
	tls_conn_t *conn;
	s2n_mode s2n_conn_mode;

	log_flag(TLS, "%s: create connection. fd:%d->%d. tls mode:%s",
		 plugin_type, tls_conn_args->input_fd, tls_conn_args->output_fd,
		 conn_mode_to_str(tls_conn_args->mode));

	conn = xmalloc(sizeof(*conn));
	conn->input_fd = tls_conn_args->input_fd;
	conn->output_fd = tls_conn_args->output_fd;
	conn->maybe = tls_conn_args->maybe;

	switch (tls_conn_args->mode) {
	case TLS_CONN_SERVER:
	{
		s2n_conn_mode = S2N_SERVER;
		break;
	}
	case TLS_CONN_CLIENT:
	{
		s2n_conn_mode = S2N_CLIENT;
		if (!tls_conn_args->cert) {
			log_flag(TLS, "%s: no certificate provided for new connection. Using default trust store.",
				 plugin_type);
			break;
		}

		log_flag(TLS, "%s: using cert to create connection:\n%s",
			 plugin_type, tls_conn_args->cert);

		/* Use new config with only "cert" loaded in trust store */
		if (!(conn->s2n_config = _create_config())) {
			error("Failed to create new config for connection");
			goto fail;
		}

		if (s2n_config_add_pem_to_trust_store(conn->s2n_config,
						      tls_conn_args->cert)) {
			on_s2n_error(conn, s2n_config_add_pem_to_trust_store);
			goto fail;
		}
		break;
	}
	default:
		error("Invalid tls connection mode");
		goto fail;
	}

	if (!conn->s2n_config && _set_conn_s2n_conf(conn, tls_conn_args))
		goto fail;

	if (!(conn->s2n_conn = s2n_connection_new(s2n_conn_mode))) {
		on_s2n_error(conn, s2n_connection_new);
		goto fail;
	}

	if (s2n_connection_set_config(conn->s2n_conn, conn->s2n_config) < 0) {
		on_s2n_error(conn, s2n_connection_set_config);
		goto fail;
	}

	if (tls_conn_args->defer_blinding &&
	    s2n_connection_set_blinding(conn->s2n_conn,
					S2N_SELF_SERVICE_BLINDING)) {
		on_s2n_error(conn, s2n_connection_set_blinding);
		goto fail;
	}

	if (tls_conn_args->callbacks.recv) {
		void *io_context = tls_conn_args->callbacks.io_context;

		if (s2n_connection_set_recv_cb(conn->s2n_conn,
					       tls_conn_args->callbacks.recv)) {
			on_s2n_error(conn, s2n_connection_set_recv_cb);
			goto fail;
		}

		if (s2n_connection_set_recv_ctx(conn->s2n_conn, io_context)) {
			on_s2n_error(conn, s2n_connection_set_recv_ctx);
			goto fail;
		}

		xassert(tls_conn_args->input_fd < 0);
		xassert(io_context);
	} else if (s2n_connection_set_read_fd(conn->s2n_conn,
					      tls_conn_args->input_fd) < 0) {
		/* Associate a connection with an incoming descriptor */
		on_s2n_error(conn, s2n_connection_set_read_fd);
		goto fail;
	}
	if (tls_conn_args->callbacks.send) {
		void *io_context = tls_conn_args->callbacks.io_context;

		if (s2n_connection_set_send_cb(conn->s2n_conn,
					       tls_conn_args->callbacks.send)) {
			on_s2n_error(conn, s2n_connection_set_send_cb);
			goto fail;
		}

		if (s2n_connection_set_send_ctx(conn->s2n_conn, io_context)) {
			on_s2n_error(conn, s2n_connection_set_send_ctx);
			goto fail;
		}

		xassert(tls_conn_args->output_fd < 0);
		xassert(io_context);
	} else if (s2n_connection_set_write_fd(conn->s2n_conn,
					       tls_conn_args->output_fd) < 0) {
		/* Associate a connection with an outgoing descriptor */
		on_s2n_error(conn, s2n_connection_set_write_fd);
		goto fail;
	}

	if (!tls_conn_args->defer_negotiation) {
		int rc;

		/* Negotiate the TLS handshake */
		while ((rc = _negotiate(conn))) {
			if (rc == EWOULDBLOCK) {
				if (wait_fd_readable(conn->input_fd,
						     slurm_conf.msg_timeout))
					error("%s: [fd:%d->fd:%d] Problem reading socket during s2n negotiation",
					      __func__, tls_conn_args->input_fd,
					      tls_conn_args->output_fd);
				else
					continue;
			}

			if (tls_conn_args->defer_blinding)
				return conn;

			goto fail;
		}
	}

	log_flag(TLS, "%s: connection successfully created. fd:%d->%d. tls mode:%s",
		 plugin_type, conn->input_fd, conn->output_fd,
		 conn_mode_to_str(tls_conn_args->mode));

	return conn;

fail:
	_cleanup_tls_conn(conn);
	xfree(conn);

	return NULL;
}

extern void tls_p_destroy_conn(tls_conn_t *conn, bool close_fds)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;

	xassert(conn);

	log_flag(TLS, "%s: destroying connection. fd:%d->%d",
		 plugin_type, conn->input_fd, conn->output_fd);

	if (!conn->s2n_conn) {
		_cleanup_tls_conn(conn);
		xfree(conn);
		return;
	}

	if (conn->do_graceful_shutdown) {
		log_flag(TLS, "%s: Attempting s2n_shutdown for fd:%d->%d",
			 plugin_type, conn->input_fd, conn->output_fd);
	} else {
		log_flag(TLS, "%s: Skipping s2n_shutdown for fd:%d->%d",
			 plugin_type, conn->input_fd, conn->output_fd);
	}

	/* Attempt graceful shutdown at TLS layer */
	while (conn->do_graceful_shutdown &&
	       s2n_shutdown(conn->s2n_conn, &blocked)) {
		if (s2n_error_get_type(s2n_errno) != S2N_ERR_T_BLOCKED) {
			on_s2n_error(conn, s2n_shutdown);
			break;
		}

		if (wait_fd_readable(conn->input_fd, slurm_conf.msg_timeout) ==
		    -1) {
			error("Problem reading socket, couldn't do graceful s2n shutdown");
			break;
		}
	}

	if (close_fds) {
		if (conn->input_fd >= 0)
			close(conn->input_fd);
		if ((conn->output_fd >= 0) &&
		    (conn->output_fd != conn->input_fd))
			close(conn->output_fd);
	}

	_cleanup_tls_conn(conn);
	xfree(conn);
}

extern ssize_t tls_p_send(tls_conn_t *conn, const void *buf, size_t n)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t bytes_written = 0;

	xassert(conn);

	while ((bytes_written < n) && (blocked == S2N_NOT_BLOCKED)) {
		ssize_t w = s2n_send(conn->s2n_conn, (buf + bytes_written),
				     (n - bytes_written), &blocked);

		if (w < 0) {
			/* blocked is expected */
			if (s2n_error_get_type(s2n_errno) != S2N_ERR_T_BLOCKED)
				on_s2n_error(conn, s2n_send);

			bytes_written = SLURM_ERROR;
			break;
		}

		bytes_written += w;
	}

	log_flag(TLS, "%s: send %zd. fd:%d->%d",
		 plugin_type, bytes_written, conn->input_fd, conn->output_fd);

	if ((blocked != S2N_NOT_BLOCKED) && !errno)
		errno = EWOULDBLOCK;

	return bytes_written;
}

extern ssize_t tls_p_sendv(tls_conn_t *conn, const struct iovec *bufs,
			   int count)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t w = 0;

	xassert(conn);

	if (slurm_conf.debug_flags & DEBUG_FLAG_TLS) {
		log_flag(TLS, "%s: s2n_sendv %d bufs. fd:%d->%d:",
			 plugin_type, count, conn->input_fd, conn->output_fd);
		for (int i = 0; i < count; i++) {
			log_flag(TLS, "%s:  bufs[%d] %zd. fd:%d->%d",
				 plugin_type, i, bufs[i].iov_len, conn->input_fd,
				 conn->output_fd);
		}
	}

	if ((w = s2n_sendv(conn->s2n_conn, bufs, count, &blocked)) < 0) {
		on_s2n_error(conn, s2n_sendv);
		return SLURM_ERROR;
	}

	return w;
}

extern uint32_t tls_p_peek(tls_conn_t *conn)
{
	if (!conn)
		return 0;

	return s2n_peek(conn->s2n_conn);
}

extern ssize_t tls_p_recv(tls_conn_t *conn, void *buf, size_t n)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t bytes_read = 0;

	xassert(conn);

	while (bytes_read < n) {
		ssize_t r = s2n_recv(conn->s2n_conn, (buf + bytes_read),
				     (n - bytes_read), &blocked);
		if (r > 0) {
			bytes_read += r;
		} else if (!r) {
			/* connection closed */
			break;
		} else if (s2n_error_get_type(s2n_errno) == S2N_ERR_T_BLOCKED) {
			if (!bytes_read) {
				log_flag(TLS, "%s: recv would block. fd:%d->%d",
					 plugin_type, conn->input_fd,
					 conn->output_fd);
				errno = EWOULDBLOCK;
				return -1;
			}

			/*
			 * recv() would block so consider the recv() complete
			 * for now
			 */
			break;
		} else {
			on_s2n_error(conn, s2n_recv);
			return SLURM_ERROR;
		}
	}

	log_flag(TLS, "%s: recv %zd. fd:%d->%d",
		 plugin_type, bytes_read, conn->input_fd, conn->output_fd);

	return bytes_read;
}

extern timespec_t tls_p_get_delay(tls_conn_t *conn)
{
	xassert(conn);

	return conn->delay;
}

extern int tls_p_negotiate_conn(tls_conn_t *conn)
{
	xassert(conn);

	return _negotiate(conn);
}

extern bool tls_p_is_client_authenticated(tls_conn_t *conn)
{
	xassert(conn);

	return conn->is_client_authenticated;
}

extern int tls_p_get_conn_fd(tls_conn_t *conn)
{
	if (!conn)
		return -1;

	if (conn->input_fd != conn->output_fd)
		debug("%s: asymmetric connection %d->%d",
		      __func__, conn->input_fd, conn->output_fd);

	return conn->input_fd;
}

extern int tls_p_set_conn_fds(tls_conn_t *conn, int input_fd, int output_fd)
{
	xassert(conn);
	xassert(conn->s2n_conn);
	xassert(input_fd >= 0);
	xassert(output_fd >= 0);

	/* Reset read/write callbacks/contexts */
	if (s2n_connection_set_recv_cb(conn->s2n_conn, NULL)) {
		on_s2n_error(conn, s2n_connection_set_recv_cb);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_recv_ctx(conn->s2n_conn, NULL)) {
		on_s2n_error(conn, s2n_connection_set_recv_ctx);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_send_cb(conn->s2n_conn, NULL)) {
		on_s2n_error(conn, s2n_connection_set_send_cb);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_send_ctx(conn->s2n_conn, NULL)) {
		on_s2n_error(conn, s2n_connection_set_send_ctx);
		return SLURM_ERROR;
	}

	/* Set new read/write fd's */
	if (s2n_connection_set_read_fd(conn->s2n_conn, input_fd)) {
		on_s2n_error(conn, s2n_connection_set_read_fd);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_write_fd(conn->s2n_conn, output_fd)) {
		on_s2n_error(conn, s2n_connection_set_write_fd);
		return SLURM_ERROR;
	}

	conn->input_fd = input_fd;
	conn->output_fd = output_fd;

	log_flag(TLS, "Successfully set input_fd:%d output_fd:%d on s2n conn %p",
		 input_fd, output_fd, conn->s2n_conn);

	return SLURM_SUCCESS;
}

extern int tls_p_set_conn_callbacks(tls_conn_t *conn,
				    conn_callbacks_t *callbacks)
{
	xassert(conn);
	xassert(conn->s2n_conn);
	xassert(callbacks);
	xassert(callbacks->recv);
	xassert(callbacks->send);

	/* Set new read/write callbacks/contexts */
	if (s2n_connection_set_recv_cb(conn->s2n_conn, callbacks->recv)) {
		on_s2n_error(conn, s2n_connection_set_recv_cb);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_recv_ctx(conn->s2n_conn,
					callbacks->io_context)) {
		on_s2n_error(conn, s2n_connection_set_recv_ctx);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_send_cb(conn->s2n_conn, callbacks->send)) {
		on_s2n_error(conn, s2n_connection_set_send_cb);
		return SLURM_ERROR;
	}
	if (s2n_connection_set_send_ctx(conn->s2n_conn,
					callbacks->io_context)) {
		on_s2n_error(conn, s2n_connection_set_send_ctx);
		return SLURM_ERROR;
	}

	log_flag(TLS, "Successfully set recv_cb:%p recv_ctx:%p send_cb:%p send_ctx:%p on s2n conn %p",
		 callbacks->recv, callbacks->io_context,
		 callbacks->send, callbacks->io_context, conn->s2n_conn);

	return SLURM_SUCCESS;
}

extern void tls_p_set_graceful_shutdown(tls_conn_t *conn,
					bool do_graceful_shutdown)
{
	xassert(conn);

	log_flag(TLS, "%s: %s graceful shutdown on fd:%d->%d",
		 plugin_type, do_graceful_shutdown ? "Enabled" : "Disabled",
		 conn->input_fd, conn->output_fd);

	conn->do_graceful_shutdown = do_graceful_shutdown;
}
