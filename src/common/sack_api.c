/*****************************************************************************\
 *  sack_api.c - [S]lurm's [a]uth and [c]red [k]iosk API
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

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/pack.h"
#include "src/common/sack_api.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#define SACK_CLUSTER_PATTERN "/run/slurm-%s/sack.socket"

static struct sockaddr_un sack_addrs[] =
{
	{
		.sun_family = AF_UNIX,
		.sun_path = "/run/slurm/sack.socket",
	}, {
		.sun_family = AF_UNIX,
		.sun_path = "/run/slurmctld/sack.socket",
	}, {
		.sun_family = AF_UNIX,
		.sun_path = "/run/slurmdbd/sack.socket",
	}
};

static int _sack_try_connection(struct sockaddr_un *addr)
{
	int fd;
	size_t len = strlen(addr->sun_path) + 1 + sizeof(addr->sun_family);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		debug3("%s: socket() failed: %m", __func__);
		return -1;
	}

	if (connect(fd, (struct sockaddr *) addr, len) < 0) {
		debug3("%s: connect() failed for %s: %m",
		      __func__, addr->sun_path);
		close(fd);
		return -1;
	}

	return fd;
}

static int _sack_connect_cluster(char *cluster_name)
{
	int fd, ret;
	struct sockaddr_un sack_addr = { .sun_family = AF_UNIX };

	ret = snprintf(sack_addr.sun_path, sizeof(sack_addr.sun_path),
		       SACK_CLUSTER_PATTERN, cluster_name);

	if (ret < 0) {
		error("snprintf failed for '/run/slurm-%s/sack.socket'",
		      cluster_name);
		return -1;
	}

	if (ret >= (sizeof(sack_addr.sun_path))) {
		error("'/run/slurm-%s/sack.socket' exceeds unix socket path max size",
		      cluster_name);
		return -1;
	}

	/* Don't error, fall back to sack_addrs[]. */
	if ((fd = _sack_try_connection(&sack_addr)) < 0)
		return -1;

	debug2("%s: connected to %s", __func__, sack_addr.sun_path);
	return fd;
}

static int _sack_connect_env(char *sack_socket)
{
	int fd, ret;
	struct sockaddr_un sack_addr = { .sun_family = AF_UNIX };

	ret = snprintf(sack_addr.sun_path, sizeof(sack_addr.sun_path), "%s",
		       sack_socket);

	if (ret < 0) {
		error("snprintf failed with 'SLURM_SACK_SOCKET=%s'",
		      sack_socket);
		return -1;
	}

	if (ret >= (sizeof(sack_addr.sun_path))) {
		error("'SLURM_SACK_SOCKET=%s' exceeds unix socket path max size",
		      sack_socket);
		return -1;
	}

	if ((fd = _sack_try_connection(&sack_addr)) < 0) {
		error("failed to connect to 'SLURM_SACK_SOCKET=%s'",
		      sack_addr.sun_path);
		return -1;
	}

	debug2("%s: connected to %s", __func__, sack_addr.sun_path);
	return fd;
}

static int _sack_connect(char *cluster_name)
{
	char *sack_socket = NULL;
	int fd;

	if ((sack_socket = getenv("SLURM_SACK_SOCKET")))
		return _sack_connect_env(sack_socket);

	if (cluster_name && ((fd = _sack_connect_cluster(cluster_name)) >= 0))
		return fd;

	for (int i = 0; i < ARRAY_SIZE(sack_addrs); i++) {
		if ((fd = _sack_try_connection(&sack_addrs[i])) < 0)
			continue;
		debug2("%s: connected to %s", __func__, sack_addrs[i].sun_path);
		return fd;
	}

	error("failed to connect to any sack sockets");
	return -1;
}

extern char *sack_create(uid_t r_uid, void *data, int dlen, char *cluster_name)
{
	int fd = -1;
	char *token = NULL;
	buf_t *request = init_buf(1024);
	uint32_t len;
	uint32_t length_position, end_position;

	if ((fd = _sack_connect(cluster_name)) < 0)
		goto rwfail;

	/* version is not included in length calculation */
	pack16(SLURM_PROTOCOL_VERSION, request);
	length_position = get_buf_offset(request);
	pack32(0, request);
	pack32(SACK_CREATE, request);
	pack32(r_uid, request);
	packmem(data, dlen, request);
	end_position = get_buf_offset(request);
	set_buf_offset(request, length_position);
	pack32(end_position - length_position, request);
	set_buf_offset(request, end_position);
	safe_write(fd, get_buf_data(request), get_buf_offset(request));

	safe_read(fd, &len, sizeof(uint32_t));
	if (!(len = ntohl(len)))
		goto rwfail;
	token = xmalloc(len + 1);
	safe_read(fd, token, len);

rwfail:
	if (fd >= 0)
		close(fd);
	FREE_NULL_BUFFER(request);
	return token;
}

extern int sack_verify(char *token, char *cluster_name)
{
	int fd = -1;
	uint32_t result = SLURM_ERROR;
	buf_t *request = init_buf(1024);
	uint32_t length_position, end_position;

	if ((fd = _sack_connect(cluster_name)) < 0)
		goto rwfail;

	/* version is not included in length calculation */
	pack16(SLURM_PROTOCOL_VERSION, request);
	length_position = get_buf_offset(request);
	pack32(0, request);
	pack32(SACK_VERIFY, request);
	packstr(token, request);
	end_position = get_buf_offset(request);
	set_buf_offset(request, length_position);
	pack32(end_position - length_position, request);
	set_buf_offset(request, end_position);
	safe_write(fd, get_buf_data(request), get_buf_offset(request));

	safe_read(fd, &result, sizeof(uint32_t));
	result = ntohl(result);

rwfail:
	if (fd >= 0)
		close(fd);
	FREE_NULL_BUFFER(request);
	return result;
}
