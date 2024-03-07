/*****************************************************************************\
 *  xsystemd.c
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
#include <sys/un.h>

#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

extern void xsystemd_change_mainpid(pid_t pid)
{
	char *notify_socket = getenv("NOTIFY_SOCKET");
	char *payload = NULL;
	size_t len = 0;
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int fd = -1;

	if (!notify_socket) {
		error("%s: missing NOTIFY_SOCKET", __func__);
		return;
	}

	strlcpy(addr.sun_path, notify_socket, sizeof(addr.sun_path));
	len = strlen(addr.sun_path) + 1 + sizeof(addr.sun_family);

	if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
		error("%s: socket() failed: %m", __func__);
		return;
	}

	if (connect(fd, (struct sockaddr *) &addr, len) < 0) {
		error("%s: connect() failed for %s: %m",
		      __func__, addr.sun_path);
		close(fd);
		return;
	}

	xstrfmtcat(payload, "READY=1\nMAINPID=%d", pid);
	safe_write(fd, payload, strlen(payload));
	xfree(payload);
	close(fd);
	return;

rwfail:
	error("%s: failed to send message: %m", __func__);
	xfree(payload);
	close(fd);
}
