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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/tls.h"

const char plugin_name[] = "Null tls plugin";
const char plugin_type[] = "tls/none";
const uint32_t plugin_id = TLS_PLUGIN_NONE;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	int fd;
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

extern tls_conn_t *tls_p_create_conn(int fd, tls_conn_mode_t tls_mode)
{
	tls_conn_t *conn = xmalloc(sizeof(*conn));

	conn->fd = fd;

	log_flag(TLS, "%s: create connection. fd:%d", plugin_type, fd);

	return conn;
}

extern void tls_p_destroy_conn(tls_conn_t *conn)
{
	log_flag(TLS, "%s: destroy connection. fd:%d", plugin_type, conn->fd);

	xfree(conn);
}

extern ssize_t tls_p_send(tls_conn_t *conn, const void *buf, size_t n)
{
	log_flag(TLS, "%s: send %zd. fd:%d", plugin_type, n, conn->fd);

	return send(conn->fd, buf, n, 0);
}

extern ssize_t tls_p_recv(tls_conn_t *conn, void *buf, size_t n, int flags)
{
	ssize_t rc = recv(conn->fd, buf, n, 0);

	log_flag(TLS, "%s: recv %zd. fd:%d", plugin_type, rc, conn->fd);

	return rc;
}
