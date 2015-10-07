/*****************************************************************************\
 **	pmix_utils.c - Various PMIx utility functions
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 \*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "pmixp_common.h"
#include "pmixp_utils.h"
#include "pmixp_debug.h"

#define PMIXP_MAX_RETRY 7

void pmixp_xfree_xmalloced(void *x)
{
	xfree(x);
}

void pmixp_free_Buf(void *x)
{
	Buf buf = (Buf)x;
	free_buf(buf);
}

int pmixp_usock_create_srv(char *path)
{
	static struct sockaddr_un sa;
	int ret = 0;

	if (strlen(path) >= sizeof(sa.sun_path)) {
		PMIXP_ERROR_STD("UNIX socket path is too long: %lu, max %lu",
				(unsigned long) strlen(path),
				(unsigned long) sizeof(sa.sun_path) - 1);
		return SLURM_ERROR;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		PMIXP_ERROR_STD("Cannot create UNIX socket");
		return SLURM_ERROR;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, path);
	if (ret = bind(fd, (struct sockaddr *)&sa, SUN_LEN(&sa))) {
		PMIXP_ERROR_STD("Cannot bind() UNIX socket %s", path);
		goto err_fd;
	}

	if ((ret = listen(fd, 64))) {
		PMIXP_ERROR_STD("Cannot listen(%d, 64) UNIX socket %s", fd,
				path);
		goto err_bind;

	}
	return fd;

      err_bind:
	unlink(path);
      err_fd:
	close(fd);
	return ret;
}

size_t pmixp_read_buf(int sd, void *buf, size_t count, int *shutdown,
		bool blocking)
{
	ssize_t ret, offs = 0;

	*shutdown = 0;

	if (!blocking && !pmixp_fd_read_ready(sd, shutdown)) {
		return 0;
	}

	if (blocking) {
		fd_set_blocking(sd);
	}

	while (count - offs > 0) {
		ret = read(sd, (char *)buf + offs, count - offs);
		if (ret > 0) {
			offs += ret;
			continue;
		} else if (ret == 0) {
			/* connection closed. */
			*shutdown = 1;
			return offs;
		}
		switch (errno) {
		case EINTR:
			continue;
		case EWOULDBLOCK:
			/* we can get here in non-blocking mode only */
			return offs;
		default:
			PMIXP_ERROR_STD("blocking=%d", blocking);
			*shutdown = -errno;
			return offs;
		}
	}

	if (blocking) {
		fd_set_nonblocking(sd);
	}
	return offs;
}

size_t pmixp_write_buf(int sd, void *buf, size_t count, int *shutdown,
		bool blocking)
{
	ssize_t ret, offs = 0;

	*shutdown = 0;

	if (!blocking && !pmixp_fd_write_ready(sd, shutdown)) {
		return 0;
	}

	if (blocking) {
		fd_set_blocking(sd);
	}

	while (count - offs > 0) {
		ret = write(sd, (char *)buf + offs, count - offs);
		if (ret > 0) {
			offs += ret;
			continue;
		}
		switch (errno) {
		case EINTR:
			continue;
		case EWOULDBLOCK:
			return offs;
		default:
			*shutdown = -errno;
			return offs;
		}
	}

	if (blocking) {
		fd_set_nonblocking(sd);
	}

	return offs;
}

bool pmixp_fd_read_ready(int fd, int *shutdown)
{
	struct pollfd pfd[1];
	int rc;
	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	/* Drop shutdown before the check */
	*shutdown = 0;

	rc = poll(pfd, 1, 10);
	if (rc < 0) {
		*shutdown = -errno;
		return false;
	}
	bool ret = ((rc == 1) && (pfd[0].revents & POLLIN));
	if (!ret && (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
		if (pfd[0].revents & (POLLERR | POLLNVAL)) {
			*shutdown = -EBADF;
		} else {
			/* POLLHUP - normal connection close */
			*shutdown = 1;
		}
	}
	return ret;
}

bool pmixp_fd_write_ready(int fd, int *shutdown)
{
	struct pollfd pfd[1];
	int rc;
	pfd[0].fd = fd;
	pfd[0].events = POLLOUT;
	rc = poll(pfd, 1, 10);
	if (rc < 0) {
		*shutdown = -errno;
		return false;
	}
	if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
		if (pfd[0].revents & (POLLERR | POLLNVAL)) {
			*shutdown = -EBADF;
		} else {
			/* POLLHUP - normal connection close */
			*shutdown = 1;
		}
	}
	return ((rc == 1) && (pfd[0].revents & POLLOUT));
}

static int _send_to_stepds(hostlist_t hl, const char *addr, uint32_t len,
		char *data)
{
	List ret_list = NULL;
	int temp_rc = 0, rc = 0;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
	forward_data_msg_t req;
	char *nodelist = NULL;

	slurm_msg_t_init(msg);
	req.address = xstrdup(addr);
	req.len = len;
	req.data = data;

	msg->msg_type = REQUEST_FORWARD_DATA;
	msg->data = &req;

	nodelist = hostlist_ranged_string_xmalloc(hl);

	if ((ret_list = slurm_send_recv_msgs(nodelist, msg, 0, false))) {
		while ((ret_data_info = list_pop(ret_list))) {
			temp_rc = slurm_get_return_code(ret_data_info->type,
					ret_data_info->data);
			if (temp_rc) {
				rc = temp_rc;
			} else {
				hostlist_delete_host(hl,
						ret_data_info->node_name);
			}
		}
	} else {
		error("tree_msg_to_stepds: no list was returned");
		rc = SLURM_ERROR;
	}

	slurm_free_msg(msg);
	xfree(nodelist);
	xfree(req.address);
	return rc;
}

int pmixp_stepd_send(char *nodelist, const char *address, char *data,
		uint32_t len)
{

	int retry = 0, rc;
	unsigned int delay = 100; /* in milliseconds */
	hostlist_t hl;

	hl = hostlist_create(nodelist);
	while (1) {
		if (retry == 1) {
			PMIXP_ERROR("send failed, rc=%d, retrying", rc);
		}

		rc = _send_to_stepds(hl, address, len, data);

		if (rc == SLURM_SUCCESS)
			break;
		retry++;
		if (retry >= PMIXP_MAX_RETRY)
			break;
		/* wait with constantly increasing delay */
		struct timespec ts =
			{(delay / 1000), ((delay % 1000) * 1000000)};
		nanosleep(&ts, NULL);
		delay *= 2;
	}
	hostlist_destroy(hl);
	return rc;
}

static int _is_dir(char *path)
{
	struct stat stat_buf;
	int rc;
	if (0 > (rc = stat(path, &stat_buf))) {
		PMIXP_ERROR_STD("Cannot stat() path=\"%s\"", path);
		return rc;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		return 0;
	}
	return 1;
}

int pmixp_rmdir_recursively(char *path)
{
	char nested_path[PATH_MAX];
	DIR *dp;
	struct dirent *ent;

	int rc;

	/*
	 * Make sure that "directory" exists and is a directory.
	 */
	if (1 != (rc = _is_dir(path))) {
		PMIXP_ERROR("path=\"%s\" is not a directory", path);
		return (rc == 0) ? -1 : rc;
	}

	if ((dp = opendir(path)) == NULL) {
		PMIXP_ERROR_STD("cannot open path=\"%s\"", path);
		return -1;
	}

	while ((ent = readdir(dp)) != NULL) {
		if (0 == strcmp(ent->d_name, ".")
				|| 0 == strcmp(ent->d_name, "..")) {
			/* skip special dir's */
			continue;
		}
		snprintf(nested_path, sizeof(nested_path), "%s/%s", path,
				ent->d_name);
		if (_is_dir(nested_path)) {
			pmixp_rmdir_recursively(nested_path);
		} else {
			unlink(nested_path);
		}
	}
	closedir(dp);
	if ((rc = rmdir(path))) {
		PMIXP_ERROR_STD("Cannot remove path=\"%s\"", path);
	}
	return rc;
}
