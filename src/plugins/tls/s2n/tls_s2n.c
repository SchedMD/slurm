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

#include "src/interfaces/certmgr.h"
#include "src/interfaces/tls.h"

/* Set default security policy to FIPS-compliant version */
#define DEFAULT_S2N_SECURITY_POLICY "20230317"
#define CTIME_STR_LEN 72

const char plugin_name[] = "s2n tls plugin";
const char plugin_type[] = "tls/s2n";
const uint32_t plugin_id = TLS_PLUGIN_S2N;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static struct s2n_config *config = NULL;

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	pthread_mutex_t lock;
	int input_fd;
	int output_fd;
	struct s2n_connection *s2n_conn;
	/* absolute time shutdown() delayed until (instead of sleep()ing) */
	timespec_t delay;
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
	} else {
		xassert(s2n_errno != S2N_ERR_T_OK);

		error("%s: %s() failed %s[%d]: %s -> %s",
		      caller, funcname, s2n_strerror_name(s2n_errno), s2n_errno,
		      s2n_strerror(s2n_errno, NULL),
		      s2n_strerror_debug(s2n_errno, NULL));
	}

	if ((slurm_conf.debug_flags & DEBUG_FLAG_TLS) &&
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
		return SLURM_ERROR;
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

	if (!(new_conf = s2n_config_new_minimal())) {
		on_s2n_error(NULL, s2n_config_new_minimal);
		return NULL;
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
		return NULL;
	}
	xfree(security_policy);

	/*
	 * from s2n usage guide:
	 *
	 * When using client authentication, the server MUST implement the
	 * s2n_verify_host_fn, because the default behavior will likely reject
	 * all client certificates.
	 */
	if (s2n_config_set_verify_host_callback(new_conf, _verify_hostname,
						NULL) < 0) {
		on_s2n_error(NULL, s2n_config_set_verify_host_callback);
		return NULL;
	}

	return new_conf;
}

static int _load_ca_cert(void)
{
	char *cert_file;

	cert_file = conf_get_opt_str(slurm_conf.tls_params, "ca_cert_file=");
	if (!cert_file && !(cert_file = get_extra_conf_path("ca_cert.pem"))) {
		error("Failed to get cert.pem path");
		xfree(cert_file);
		return SLURM_ERROR;
	}

	/*
	 * Check if CA cert is owned by SlurmUser/root and that it's not
	 * modifiable/executable by everyone.
	 */
	if (_check_file_permissions(cert_file, (S_IWOTH | S_IXOTH), true)) {
		xfree(cert_file);
		return SLURM_ERROR;
	}
	if (s2n_config_set_verification_ca_location(config, cert_file, NULL) < 0) {
		on_s2n_error(NULL, s2n_config_set_verification_ca_location);
		xfree(cert_file);
		return SLURM_ERROR;
	}

	xfree(cert_file);
	return SLURM_SUCCESS;
}

static int _add_cert_and_key_to_store(char *cert_pem, uint32_t cert_pem_len,
				      char *key_pem, uint32_t key_pem_len)
{
	struct s2n_cert_chain_and_key *cert_and_key;

	if (!(cert_and_key = s2n_cert_chain_and_key_new())) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_new);
		return SLURM_ERROR;
	}

	if (s2n_cert_chain_and_key_load_pem_bytes(cert_and_key,
						  (uint8_t *) cert_pem,
						  cert_pem_len,
						  (uint8_t *) key_pem,
						  key_pem_len) < 0) {
		on_s2n_error(NULL, s2n_cert_chain_and_key_load_pem_bytes);
		return SLURM_ERROR;
	}

	/*
	 * Per libs2n docs:
	 *	It is not recommended to free or modify the `cert_key_pair` as
	 *	any subsequent changes will be reflected in the config.
	 */
	if (s2n_config_add_cert_chain_and_key_to_store(config, cert_and_key) <
	    0) {
		on_s2n_error(NULL, s2n_config_add_cert_chain_and_key_to_store);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _load_self_cert(void)
{
	int rc;
	char *cert_file, *key_file;
	char *cert_conf = NULL, *key_conf = NULL;
	char *default_cert_path = NULL, *default_key_path = NULL;
	buf_t *cert_buf, *key_buf;
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
	} else {
		int rc;
		char *cert_pem = NULL, *key_pem = NULL;
		uint32_t cert_pem_len, key_pem_len;

		if (!certmgr_enabled()) {
			error("certmgr plugin not enabled, unable to get self signed certificate.");
			return SLURM_ERROR;
		}
		if (certmgr_g_get_self_signed_cert(&cert_pem, &key_pem) ||
		    !cert_pem || !key_pem) {
			error("Failed to get self signed certificate and private key");
			return SLURM_ERROR;
		}

		cert_pem_len = strlen(cert_pem);
		key_pem_len = strlen(key_pem);

		rc = _add_cert_and_key_to_store(cert_pem, cert_pem_len, key_pem,
						key_pem_len);
		xfree(cert_pem);
		xfree(key_pem);

		return rc;
	}
	xassert(cert_conf);
	xassert(key_conf);
	xassert(default_cert_path);

	/* Get self certificate file */
	cert_file = conf_get_opt_str(slurm_conf.tls_params, cert_conf);
	if (!cert_file &&
	    !(cert_file = get_extra_conf_path(default_cert_path))) {
		error("Failed to get %s path", default_cert_path);
		xfree(cert_file);
		return SLURM_ERROR;
	}
	/*
	 * Check if our public certificate is owned by SlurmUser/root (unless
	 * running in slurmrestd) and that it's not modifiable/executable by
	 * everyone.
	 */
	if (_check_file_permissions(cert_file, (S_IWOTH | S_IXOTH),
				    check_owner)) {
		xfree(cert_file);
		return SLURM_ERROR;
	}
	if (!(cert_buf = create_mmap_buf(cert_file))) {
		error("%s: Could not load cert file (%s): %m",
		      plugin_type, cert_file);
		xfree(cert_file);
		return SLURM_ERROR;
	}
	xfree(cert_file);

	/* Get private key file */
	key_file = conf_get_opt_str(slurm_conf.tls_params, key_conf);
	if (!key_file && !(key_file = get_extra_conf_path(default_key_path))) {
		error("Failed to get %s path", default_key_path);
		xfree(key_file);
		FREE_NULL_BUFFER(cert_buf);
		return SLURM_ERROR;
	}
	/*
	 * Check if our private key is owned by SlurmUser/root (unless running
	 * in slurmrestd) and that it's not readable/writable/executable by
	 * everyone.
	 */
	if (_check_file_permissions(key_file, S_IRWXO, check_owner)) {
		xfree(key_file);
		FREE_NULL_BUFFER(cert_buf);
		return SLURM_ERROR;
	}
	if (!(key_buf = create_mmap_buf(key_file))) {
		error("%s: Could not load private key file (%s): %m",
		      plugin_type, key_file);
		xfree(key_file);
		FREE_NULL_BUFFER(cert_buf);
		return SLURM_ERROR;
	}
	xfree(key_file);

	rc = _add_cert_and_key_to_store(cert_buf->head, cert_buf->size,
					key_buf->head, key_buf->size);

	FREE_NULL_BUFFER(cert_buf);
	FREE_NULL_BUFFER(key_buf);

	return rc;
}

extern int init(void)
{
	debug("%s loaded", plugin_type);

	if (s2n_init() != S2N_SUCCESS) {
		on_s2n_error(NULL, s2n_init);
		return errno;
	}

	if (!(config = _create_config())) {
		error("Could not create configuration for s2n");
		return errno;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_TLS)
		s2n_stack_traces_enabled_set(true);

	if (!running_in_slurmstepd() && _load_ca_cert()) {
		error("Could not load trusted certificates for s2n");
		return SLURM_ERROR;
	}

	/*
	 * slurmctld and slurmdbd need to load their own pre-signed certificate
	 */
	if (running_in_slurmctld() || running_in_slurmdbd() ||
	    running_in_slurmrestd() || !running_in_daemon()) {
		if (_load_self_cert()) {
			error("Could not load own certificate and private key for s2n");
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (s2n_config_free(config))
		on_s2n_error(NULL, s2n_config_free);

	if (s2n_cleanup_final())
		on_s2n_error(NULL, s2n_cleanup_final);

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
		log_flag(TLS, "%s: cipher suite:%s, {0x%02X,0x%02X}. fd:%d->%d.",
			 plugin_type, cipher, first, second, conn->input_fd,
			 conn->output_fd);
	}

	return SLURM_SUCCESS;
}

extern void *tls_p_create_conn(const tls_conn_args_t *tls_conn_args)
{
	tls_conn_t *conn;
	s2n_mode s2n_conn_mode;

	log_flag(TLS, "%s: create connection. fd:%d->%d. tls mode:%s",
		 plugin_type, tls_conn_args->input_fd, tls_conn_args->output_fd,
		 tls_conn_mode_to_str(tls_conn_args->mode));

	switch (tls_conn_args->mode) {
	case TLS_CONN_SERVER:
		s2n_conn_mode = S2N_SERVER;
		break;
	case TLS_CONN_CLIENT:
		s2n_conn_mode = S2N_CLIENT;
		break;
	default:
		error("Invalid tls connection mode");
		return NULL;
	}

	conn = xmalloc(sizeof(*conn));
	conn->input_fd = tls_conn_args->input_fd;
	conn->output_fd = tls_conn_args->output_fd;
	slurm_mutex_init(&conn->lock);

	if (!(conn->s2n_conn = s2n_connection_new(s2n_conn_mode))) {
		on_s2n_error(conn, s2n_connection_new);
		slurm_mutex_destroy(&conn->lock);
		xfree(conn);
		return NULL;
	}

	if (s2n_connection_set_config(conn->s2n_conn, config) < 0) {
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

			goto fail;
		}
	}

	log_flag(TLS, "%s: connection successfully created. fd:%d->%d. tls mode:%s",
		 plugin_type, conn->input_fd, conn->output_fd,
		 tls_conn_mode_to_str(tls_conn_args->mode));

	return conn;

fail:
	if (!tls_conn_args->defer_blinding) {
		if (s2n_connection_free(conn->s2n_conn) < 0)
			on_s2n_error(conn, s2n_connection_free);

		slurm_mutex_destroy(&conn->lock);
		xfree(conn);
	}

	return conn;
}

extern void tls_p_destroy_conn(tls_conn_t *conn)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;

	xassert(conn);

	log_flag(TLS, "%s: destroying connection. fd:%d->%d",
		 plugin_type, conn->input_fd, conn->output_fd);

	slurm_mutex_lock(&conn->lock);

	if (!conn->s2n_conn) {
		slurm_mutex_unlock(&conn->lock);
		slurm_mutex_destroy(&conn->lock);
		xfree(conn);
		return;
	}

	/* Attempt graceful shutdown at TLS layer */
	/*
	 * FIXME: the dbd agent in slurmctld sleeps periodically if it doesn't have
	 * anything to send to the slurmdbd, and thus the slurmdbd attempting to
	 * shut the connection down cleanly will almost always time out.
	 */
	while (running_in_slurmctld() &&
	       (s2n_shutdown(conn->s2n_conn, &blocked) != S2N_SUCCESS)) {
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

	if (s2n_connection_free(conn->s2n_conn) < 0)
		on_s2n_error(conn, s2n_connection_free);

	slurm_mutex_unlock(&conn->lock);
	slurm_mutex_destroy(&conn->lock);
	xfree(conn);
}

extern ssize_t tls_p_send(tls_conn_t *conn, const void *buf, size_t n)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t bytes_written = 0;

	xassert(conn);

	slurm_mutex_lock(&conn->lock);
	while ((bytes_written < n) && (blocked == S2N_NOT_BLOCKED)) {
		ssize_t w = s2n_send(conn->s2n_conn, (buf + bytes_written),
				     (n - bytes_written), &blocked);

		if (w < 0) {
			on_s2n_error(conn, s2n_send);
			bytes_written = SLURM_ERROR;
			break;
		}

		bytes_written += w;
	}
	slurm_mutex_unlock(&conn->lock);

	log_flag(TLS, "%s: send %zd. fd:%d->%d",
		 plugin_type, bytes_written, conn->input_fd, conn->output_fd);

	if ((blocked != S2N_NOT_BLOCKED) && !errno)
		errno = EWOULDBLOCK;

	return bytes_written;
}

extern ssize_t tls_p_recv(tls_conn_t *conn, void *buf, size_t n)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t bytes_read = 0;

	xassert(conn);

	slurm_mutex_lock(&conn->lock);
	while (bytes_read < n) {
		ssize_t r = s2n_recv(conn->s2n_conn, (buf + bytes_read),
				     (n - bytes_read), &blocked);
		if (r > 0) {
			bytes_read += r;
		} else if (!r) {
			/* connection closed */
			break;
		} else if (s2n_error_get_type(s2n_errno) == S2N_ERR_T_BLOCKED) {
			/*
			 * recv() would block so consider the recv() complete
			 * for now
			 */
			errno = EWOULDBLOCK;
			break;
		} else {
			on_s2n_error(conn, s2n_recv);
			slurm_mutex_unlock(&conn->lock);
			return SLURM_ERROR;
		}
	}
	slurm_mutex_unlock(&conn->lock);

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

	log_flag(TLS, "Successfully set input_fd:%d output_fd:%d on s2n conn %p",
		 input_fd, output_fd, conn->s2n_conn);

	return SLURM_SUCCESS;
}

extern int tls_p_set_conn_callbacks(tls_conn_t *conn,
				    tls_conn_callbacks_t *callbacks)
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
