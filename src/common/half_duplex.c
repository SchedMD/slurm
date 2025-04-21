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

#include "src/interfaces/tls.h"

#define BUFFER_SIZE 4096

static bool _half_duplex_readable(eio_obj_t *obj);
static int _half_duplex(eio_obj_t *obj, list_t *objs);

struct io_operations half_duplex_ops = {
	.readable = _half_duplex_readable,
	.handle_read = _half_duplex,
};

typedef struct {
	int *fd_out;
	void **tls_conn_out;
	void **tls_conn_in;
} half_duplex_eio_arg_t;

extern int half_duplex_add_objs_to_handle(eio_handle_t *eio_handle,
					  int *local_fd, int *remote_fd)
{
	half_duplex_eio_arg_t *local_arg = NULL;
	half_duplex_eio_arg_t *remote_arg = NULL;

	eio_obj_t *remote_to_local_eio, *local_to_remote_eio;

	local_arg = xmalloc(sizeof(*local_arg));
	local_arg->fd_out = remote_fd;

	remote_arg = xmalloc(sizeof(*remote_arg));
	remote_arg->fd_out = local_fd;

	local_to_remote_eio =
		eio_obj_create(*local_fd, &half_duplex_ops, local_arg);
	remote_to_local_eio =
		eio_obj_create(*remote_fd, &half_duplex_ops, remote_arg);

	eio_new_obj(eio_handle, local_to_remote_eio);
	eio_new_obj(eio_handle, remote_to_local_eio);

	return SLURM_SUCCESS;
}

static bool _half_duplex_readable(eio_obj_t *obj)
{
	if (obj->shutdown) {
		half_duplex_eio_arg_t *args = obj->arg;
		int *fd_out = args->fd_out;
		void **tls_conn_out = args->tls_conn_out;

		if (fd_out) {
			if (tls_conn_out && *tls_conn_out) {
				tls_g_destroy_conn(*tls_conn_out);
				*tls_conn_out = NULL;
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
	void **tls_conn_out = args->tls_conn_out;
	void **tls_conn_in = args->tls_conn_in;

	xassert(!(tls_conn_in && tls_conn_out));

	if (obj->shutdown || !fd_out)
		goto shutdown;

	if (tls_conn_in && *tls_conn_in) {
		in = tls_g_recv(*tls_conn_in, buf, sizeof(buf));
	} else {
		in = read(obj->fd, buf, sizeof(buf));
	}
	if (in == 0) {
		debug("%s: shutting down %d -> %d",
		      __func__, obj->fd, *fd_out);
		goto shutdown;
	} else if (in < 0) {
		error("%s: read error %zd %m", __func__, in);
		goto shutdown;
	}

	while (wr < in) {
		if (tls_conn_out && *tls_conn_out) {
			out = tls_g_send(*tls_conn_out, buf, in - wr);
		} else {
			out = write(*fd_out, buf, in - wr);
		}
		if (out <= 0) {
			error("%s: wrote %zd of %zd", __func__, out, in);
			goto shutdown;
		}
		wr += out;
	}
	return 0;

shutdown:
	obj->shutdown = true;
	if (tls_conn_in && *tls_conn_in) {
		tls_g_destroy_conn(*tls_conn_in);
		*tls_conn_in = NULL;
	}
	shutdown(obj->fd, SHUT_RD);
	close(obj->fd);
	obj->fd = -1;
	if (fd_out) {
		if (tls_conn_out && *tls_conn_out) {
			tls_g_destroy_conn(*tls_conn_out);
			*tls_conn_out = NULL;
		}
		shutdown(*fd_out, SHUT_WR);
		xfree(fd_out);
		xfree(obj->arg);
	}
	eio_remove_obj(obj, objs);
	return 0;
}
