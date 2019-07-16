/*****************************************************************************\
 *  half_duplex.c - a half duplex connection handling io_operations struct
 *                  suitable for use with eio
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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
#include "src/common/half_duplex.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"

#define BUFFER_SIZE 4096

static bool _half_duplex_readable(eio_obj_t *obj);
static int _half_duplex(eio_obj_t *obj, List objs);

struct io_operations half_duplex_ops = {
	.readable = _half_duplex_readable,
	.handle_read = _half_duplex,
};

static bool _half_duplex_readable(eio_obj_t *obj)
{
	if (obj->shutdown) {
		int *fd_out = (int *) obj->arg;
		if (fd_out) {
			shutdown(*fd_out, SHUT_WR);
			xfree(obj->arg);
		}
		shutdown(obj->fd, SHUT_RD);
		return false;
	}
	return true;
}

static int _half_duplex(eio_obj_t *obj, List objs)
{
	ssize_t in, out, wr = 0;
	char buf[BUFFER_SIZE];
	int *fd_out = (int *) obj->arg;

	if (obj->shutdown || !fd_out)
		goto shutdown;

	in = read(obj->fd, buf, sizeof(buf));
	if (in == 0) {
		debug("%s: shutting down %d -> %d",
		      __func__, obj->fd, *fd_out);
		goto shutdown;
	} else if (in < 0) {
		error("%s: read error %zd %m", __func__, in);
		goto shutdown;
	}

	while (wr < in) {
		out = write(*fd_out, buf, in - wr);
		if (out <= 0) {
			error("%s: wrote %zd of %zd", __func__, out, in);
			goto shutdown;
		}
		wr += out;
	}
	return 0;

shutdown:
	obj->shutdown = true;
	shutdown(obj->fd, SHUT_RD);
	if (fd_out) {
		shutdown(*fd_out, SHUT_WR);
		xfree(fd_out);
	}
	eio_remove_obj(obj, objs);
	return 0;
}
