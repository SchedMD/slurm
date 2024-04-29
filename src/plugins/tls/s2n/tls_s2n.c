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
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/tls.h"

/* Set default security policy to FIPS-compliant version */
#define DEFAULT_S2N_SECURITY_POLICY "20230317"

#define DEFAULT_S2N_PSK_IDENTITY "slurm_s2n_psk"

const char plugin_name[] = "s2n tls plugin";
const char plugin_type[] = "tls/s2n";
const uint32_t plugin_id = TLS_PLUGIN_S2N;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static struct s2n_psk *psk = NULL;
static struct s2n_config *config = NULL;

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	pthread_mutex_t lock;
	int fd;
	struct s2n_connection *s2n_conn;
} tls_conn_t;

static void _check_key_permissions(const char *path, int bad_perms)
{
	struct stat statbuf;

	xassert(path);

	if (stat(path, &statbuf))
		fatal("%s: cannot stat '%s': %m", plugin_type, path);

	/*
	 * Configless operation means slurm_user_id is 0.
	 * Avoid an incorrect warning if the key is actually owned by the
	 * (currently unknown) SlurmUser. (Although if you're running with
	 * SlurmUser=root, this warning will be skipped inadvertently.)
	 */
	if ((statbuf.st_uid != 0) && slurm_conf.slurm_user_id &&
	    (statbuf.st_uid != slurm_conf.slurm_user_id))
		warning("%s: '%s' owned by uid=%u, instead of SlurmUser(%u) or root",
			plugin_type, path, statbuf.st_uid,
			slurm_conf.slurm_user_id);

	if (statbuf.st_mode & bad_perms)
		fatal("%s: key file is insecure: '%s' mode=0%o",
		      plugin_type, path, statbuf.st_mode & 0777);
}

static struct s2n_psk *_load_psk(void)
{
	struct s2n_psk *psk_local = NULL;
	buf_t *psk_buf = NULL;
	char *key_file = NULL;
	char *psk_identity = NULL;

	/* Create pre-shared key */
	if (!(psk_local = s2n_external_psk_new())) {
		error("%s: s2n_external_psk_new: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		return NULL;
	}

	if (s2n_psk_set_hmac(psk_local, S2N_PSK_HMAC_SHA256) < 0) {
		error("%s: s2n_psk_set_hmac: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		goto fail;
	}

	/* Get PSK identity */
	psk_identity = conf_get_opt_str(slurm_conf.tls_params, "psk_identity=");

	if (!psk_identity)
		psk_identity = xstrdup(DEFAULT_S2N_PSK_IDENTITY);

	if (s2n_psk_set_identity(psk_local, (const uint8_t *) psk_identity,
				 sizeof(psk_identity)) < 0) {
		error("%s: s2n_psk_set_identity: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		xfree(psk_identity);
		goto fail;
	}
	xfree(psk_identity);

	/* Get PSK secret */
	key_file = conf_get_opt_str(slurm_conf.tls_params, "psk_file=");
	if (!key_file && !(key_file = get_extra_conf_path("tls_psk.key"))) {
		error("Failed to get tls_psk.key path");
		xfree(key_file);
		goto fail;
	}
	_check_key_permissions(key_file, S_IRWXO);
	if (!(psk_buf = create_mmap_buf(key_file))) {
		error("%s: Could not load key file (%s)", plugin_type,
		      key_file);
		xfree(key_file);
		goto fail;
	}
	xfree(key_file);

	/* Set PSK secret */
	if (s2n_psk_set_secret(psk_local, (const uint8_t *) psk_buf->head,
			       psk_buf->size) < 0) {
		error("%s: s2n_psk_set_secret: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		free_buf(psk_buf);
		goto fail;
	}

	free_buf(psk_buf);
	return psk_local;

fail:
	s2n_psk_free(&psk_local);
	return NULL;
}

static struct s2n_config *_create_config(void)
{
	struct s2n_config *new_conf = NULL;
	char *security_policy = NULL;

	if (!(new_conf = s2n_config_new())) {
		error("%s: s2n_config_new: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
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
		error("%s: s2n_config_set_cipher_preferences: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		xfree(security_policy);
		return NULL;
	}
	xfree(security_policy);

	return new_conf;
}

extern int init(void)
{
	debug("%s loaded", plugin_type);

	if (s2n_init() != S2N_SUCCESS) {
		error("%s: s2n_init: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		return SLURM_ERROR;
	}

	if (!(psk = _load_psk())) {
		error("Could not load pre-shared key for s2n");
		return SLURM_ERROR;
	}

	if (!(config = _create_config())) {
		error("Could not create configuration for s2n");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	s2n_psk_free(&psk);
	s2n_config_free(config);

	return SLURM_SUCCESS;
}

extern void *tls_p_create_conn(int fd, tls_conn_mode_t tls_mode)
{
	tls_conn_t *conn;
	s2n_mode s2n_conn_mode;
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;

	log_flag(TLS, "%s: create connection. fd:%d. tls mode:%d",
		 plugin_type, fd, tls_mode);

	switch (tls_mode) {
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
	conn->fd = fd;
	slurm_mutex_init(&conn->lock);

	if (!(conn->s2n_conn = s2n_connection_new(s2n_conn_mode))) {
		error("%s: s2n_connection_new: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		slurm_mutex_destroy(&conn->lock);
		xfree(conn);
		return NULL;
	}

	if (s2n_connection_set_config(conn->s2n_conn, config) < 0) {
		error("%s: s2n_connection_set_config: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		goto fail;
	}

	if (s2n_connection_append_psk(conn->s2n_conn, psk) < 0) {
		error("%s: s2n_connection_append_psk: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		goto fail;
	}

	if (s2n_connection_set_psk_mode(conn->s2n_conn,
					S2N_PSK_MODE_EXTERNAL) < 0) {
		error("%s: s2n_connection_set_psk_mode: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		goto fail;
	}

	/* Associate a connection with a file descriptor */
	if (s2n_connection_set_fd(conn->s2n_conn, fd) < 0) {
		error("%s: s2n_connection_set_fd: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		goto fail;
	}

	/* Negotiate the TLS handshake */
	while (s2n_negotiate(conn->s2n_conn, &blocked) != S2N_SUCCESS) {
		if (s2n_error_get_type(s2n_errno) != S2N_ERR_T_BLOCKED) {
			error("%s: s2n_negotiate: %s",
			      __func__, s2n_strerror(s2n_errno, NULL));
			goto fail;
		}

		if (wait_fd_readable(conn->fd, slurm_conf.msg_timeout) == -1) {
			error("Problem reading socket, couldn't do s2n negotiation");
			goto fail;
		}
	}

	return conn;

fail:
	if (s2n_connection_free(conn->s2n_conn) < 0)
		error("%s: s2n_connection_free: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
	slurm_mutex_destroy(&conn->lock);
	xfree(conn);

	return NULL;
}

extern void tls_p_destroy_conn(tls_conn_t *conn)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;

	xassert(conn);

	log_flag(TLS, "%s: destroying connection. fd:%d",
		 plugin_type, conn->fd);

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
			error("%s: s2n_shutdown: %s",
			      __func__, s2n_strerror(s2n_errno, NULL));
			break;
		}

		if (wait_fd_readable(conn->fd, slurm_conf.msg_timeout) == -1) {
			error("Problem reading socket, couldn't do graceful s2n shutdown");
			break;
		}
	}

	if (s2n_connection_free(conn->s2n_conn) < 0) {
		error("%s: s2n_connection_free: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
	}

	slurm_mutex_unlock(&conn->lock);
	slurm_mutex_destroy(&conn->lock);
	xfree(conn);
}

extern ssize_t tls_p_send(tls_conn_t *conn, const void *buf, size_t n)
{
	s2n_blocked_status blocked = S2N_NOT_BLOCKED;
	ssize_t bytes_written;

	xassert(conn);

	slurm_mutex_lock(&conn->lock);
	if ((bytes_written = s2n_send(conn->s2n_conn, buf, n, &blocked)) < 0) {
		error("%s: s2n_send: %s",
		      __func__, s2n_strerror(s2n_errno, NULL));
		bytes_written = SLURM_ERROR;
	}
	slurm_mutex_unlock(&conn->lock);

	log_flag(TLS, "%s: send %zd. fd:%d", plugin_type, bytes_written, conn->fd);

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
			/* busy wait until we get further data */
			continue;
		} else {
			error("%s: s2n_recv: %s",
			      __func__, s2n_strerror(s2n_errno, NULL));
			slurm_mutex_unlock(&conn->lock);
			return SLURM_ERROR;
		}
	}
	slurm_mutex_unlock(&conn->lock);

	log_flag(TLS, "%s: recv %zd. fd:%d", plugin_type, bytes_read, conn->fd);

	return bytes_read;
}
