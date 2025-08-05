/*****************************************************************************\
 *  half_duplex.c - a half duplex connection handling io_operations struct
 *                  suitable for use with eio
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

#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/half_duplex.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"

#define BUFFER_SIZE 4096

static bool _half_duplex_readable(eio_obj_t *obj);
static int _half_duplex(eio_obj_t *obj, list_t *objs);
static int _cleanup_sockets(eio_obj_t *obj, list_t *objs, list_t *del_objs);

struct io_operations half_duplex_ops = {
	.readable = _half_duplex_readable,
	.handle_read = _half_duplex,
	.handle_cleanup = _cleanup_sockets,
};

typedef struct {
	int *fd_out;
	void **conn_out;
	void **conn_in;
} half_duplex_eio_arg_t;

extern int half_duplex_add_objs_to_handle(eio_handle_t *eio_handle,
					  int *local_fd, int *remote_fd,
					  void *conn)
{
	void **conn_ptr = xmalloc(sizeof(*conn_ptr));
	half_duplex_eio_arg_t *local_arg = NULL;
	half_duplex_eio_arg_t *remote_arg = NULL;

	xassert(conn);

	eio_obj_t *remote_to_local_eio, *local_to_remote_eio;

	local_arg = xmalloc(sizeof(*local_arg));
	local_arg->fd_out = remote_fd;

	remote_arg = xmalloc(sizeof(*remote_arg));
	remote_arg->fd_out = local_fd;

	local_to_remote_eio =
		eio_obj_create(*local_fd, &half_duplex_ops, local_arg);
	remote_to_local_eio =
		eio_obj_create(*remote_fd, &half_duplex_ops, remote_arg);

	remote_to_local_eio->conn = conn;

	*conn_ptr = conn;

	/*
	 * Ensure that both eio objects point to the same place in memory for
	 * the remote conn. This way, we avoid calling conn_g_destroy() twice.
	 *
	 * Because eio_handle_mainloop loops over both eio objects in the same
	 * thread, we don't have to worry about concurrency issues with both eio
	 * objects checking the same conn memory space.
	 */
	local_arg->conn_out = conn_ptr;
	remote_arg->conn_in = conn_ptr;

	/*
	 * tls/s2n's tls_p_recv will attempt to read data on a connection until
	 * it has read all 'n' bytes. If it is non-blocking however, it will
	 * only read the available bytes and return after that.
	 *
	 * For half_duplex, only the available data should be read and then
	 * immediately forwarded instead of waiting to fill the entire read
	 * buffer before writing. For this reason, the connection needs to be
	 * set to non-blocking when tls_enabled() is true.
	 */
	if (tls_enabled())
		fd_set_nonblocking(*remote_fd);

	/*
	 * Peer will be waiting on conn_g_recv(), and they will need to know if
	 * connection was intentionally closed or if an error occurred.
	 */
	conn_g_set_graceful_shutdown(conn, true);

	eio_new_obj(eio_handle, local_to_remote_eio);
	eio_new_obj(eio_handle, remote_to_local_eio);

	return SLURM_SUCCESS;
}

static bool _half_duplex_readable(eio_obj_t *obj)
{
	if (obj->shutdown) {
		half_duplex_eio_arg_t *args = obj->arg;
		int *fd_out = args->fd_out;
		void **conn_out = args->conn_out;

		if (fd_out) {
			if (conn_out && *conn_out) {
				conn_g_destroy(*conn_out, false);
				*conn_out = NULL;
			} else if (conn_out) {
				xfree(conn_out);
			}
			shutdown(*fd_out, SHUT_WR);
			xfree(fd_out);
			xfree(obj->arg);
		}
		shutdown(obj->fd, SHUT_RD);
		return false;
	}
	return true;
}

static int _half_duplex(eio_obj_t *obj, list_t *objs)
{
	ssize_t in, out, wr = 0;
	char buf[BUFFER_SIZE];
	half_duplex_eio_arg_t *args = obj->arg;
	int *fd_out = args->fd_out;
	void **conn_out = args->conn_out;
	void **conn_in = args->conn_in;

	xassert(!(conn_in && conn_out));

	if (obj->shutdown || !fd_out)
		goto shutdown;

	if (conn_in && *conn_in) {
		in = conn_g_recv(*conn_in, buf, sizeof(buf));
	} else {
		in = read(obj->fd, buf, sizeof(buf));
	}
	if (in == 0) {
		debug("%s: shutting down %d -> %d",
		      __func__, obj->fd, *fd_out);
		goto shutdown;
	} else if ((in < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
		return 0;
	} else if (in < 0) {
		error("%s: read error %zd %m", __func__, in);
		goto shutdown;
	}

	while (wr < in) {
		if (conn_out && *conn_out) {
			out = conn_g_send(*conn_out, buf, in - wr);
		} else {
			out = write(*fd_out, buf, in - wr);
		}
		if ((out < 0) &&
		    ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
			continue;
		} else if (out <= 0) {
			error("%s: wrote %zd of %zd: %m", __func__, out, in);
			goto shutdown;
		}
		wr += out;
	}
	return 0;

shutdown:
	obj->shutdown = true;
	if (conn_in && *conn_in) {
		conn_g_destroy(*conn_in, false);
		*conn_in = NULL;
		obj->conn = NULL;
	}
	shutdown(obj->fd, SHUT_RD);
	if (fd_out) {
		if (conn_out && *conn_out) {
			conn_g_destroy(*conn_out, false);
			*conn_out = NULL;
		} else if (conn_out) {
			xfree(conn_out);
		}
		shutdown(*fd_out, SHUT_WR);
		xfree(fd_out);
		xfree(obj->arg);
	}
	return 0;
}

static int _cleanup_sockets(eio_obj_t *obj, list_t *objs, list_t *del_objs)
{
	eio_obj_t *e;

	/* Avoid double freeing conn */
	if (!obj->arg)
		obj->conn = NULL;

	if (obj->shutdown) {
		e = eio_obj_create(obj->fd, obj->ops, obj->arg);
		e->close_time = time(NULL);
		list_enqueue(del_objs, e);
		eio_remove_obj(obj, objs);
	} else {
		eio_remove_obj(obj, del_objs);
	}

	return 0;
}
