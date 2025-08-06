/*****************************************************************************\
 *  tls_none.c
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

#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/conn.h"

const char plugin_name[] = "Null tls plugin";
const char plugin_type[] = "tls/none";
const uint32_t plugin_id = TLS_PLUGIN_NONE;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	int input_fd;
	int output_fd;
} tls_conn_t;

extern int init(void)
{
	debug("%s loaded", plugin_type);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern int tls_p_load_ca_cert(char *cert_file)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int tls_p_load_own_cert(char *cert, uint32_t cert_len, char *key,
			       uint32_t key_len)
{
	return SLURM_SUCCESS;
}

extern int tls_p_load_self_signed_cert(void)
{
	return SLURM_SUCCESS;
}

extern char *tls_p_get_own_public_cert(void)
{
	return NULL;
}

extern bool tls_p_own_cert_loaded(void)
{
	return true;
}

extern tls_conn_t *tls_p_create_conn(const conn_args_t *tls_conn_args)
{
	tls_conn_t *conn = xmalloc(sizeof(*conn));

	conn->input_fd = tls_conn_args->input_fd;
	conn->output_fd = tls_conn_args->output_fd;

	log_flag(TLS, "%s: create connection. fd:%d->%d",
		 plugin_type, conn->input_fd, conn->output_fd);

	return conn;
}

extern void tls_p_destroy_conn(tls_conn_t *conn, bool close_fds)
{
	log_flag(TLS, "%s: destroy connection. fd:%d->%d",
		 plugin_type, conn->input_fd, conn->output_fd);

	if (close_fds) {
		if (conn->input_fd >= 0)
			close(conn->input_fd);
		if ((conn->output_fd >= 0) &&
		    (conn->output_fd != conn->input_fd))
			close(conn->output_fd);
	}

	xfree(conn);
}

extern ssize_t tls_p_send(tls_conn_t *conn, const void *buf, size_t n)
{
	log_flag(TLS, "%s: send %zd. fd:%d->%d",
		 plugin_type, n, conn->input_fd, conn->output_fd);

	return send(conn->output_fd, buf, n, 0);
}

extern ssize_t tls_p_sendv(tls_conn_t *conn, const struct iovec *bufs,
			   int count)
{
	return writev(conn->output_fd, bufs, count);
}

extern uint32_t tls_p_peek(tls_conn_t *conn)
{
	return 0;
}

extern ssize_t tls_p_recv(tls_conn_t *conn, void *buf, size_t n, int flags)
{
	ssize_t rc = recv(conn->input_fd, buf, n, 0);

	log_flag(TLS, "%s: recv %zd. fd:%d->%d",
		 plugin_type, rc, conn->input_fd, conn->output_fd);

	return rc;
}

extern timespec_t tls_p_get_delay(void *conn)
{
	xassert(conn);

	return ((timespec_t) { 0 });
}

extern int tls_p_negotiate_conn(void *conn)
{
	return ESLURM_NOT_SUPPORTED;
}

extern bool tls_p_is_client_authenticated(void *conn)
{
	return false;
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
	return ESLURM_NOT_SUPPORTED;
}

extern int tls_p_set_conn_callbacks(tls_conn_t *conn,
				    conn_callbacks_t *callbacks)
{
	return ESLURM_NOT_SUPPORTED;
}

extern void tls_p_set_graceful_shutdown(tls_conn_t *conn,
					bool do_graceful_shutdown)
{
	return;
}
