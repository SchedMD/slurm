/*****************************************************************************\
 ** pmix_utils.c - Various PMIx utility functions
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "pmixp_common.h"
#include "pmixp_utils.h"
#include "pmixp_debug.h"

/* must come after the above pmixp includes */
#include "src/common/forward.h"

extern int pmixp_count_digits_base10(uint32_t val)
{
	int digit_count = 0;

	while (val) {
		digit_count++;
		val /= 10;
	}

	return digit_count;
}

void pmixp_xfree_xmalloced(void *x)
{
	xfree(x);
}

void pmixp_free_buf(void *x)
{
	Buf buf = (Buf)x;
	FREE_NULL_BUFFER(buf);
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
	if ((ret = bind(fd, (struct sockaddr *)&sa, SUN_LEN(&sa)))) {
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

int pmixp_fd_set_nodelay(int fd)
{
	int val = 1;
	if ( 0 > setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val,
			    sizeof(val)) ) {
		PMIXP_ERROR_STD("Cannot set TCP_NODELAY on fd = %d\n", fd);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
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

static int _iov_shift(struct iovec *iov, size_t iovcnt, int offset)
{
	int skip, i;
	size_t count = 0;

	/* find out how many iov's was completely sent */
	for (skip = 0; skip < iovcnt; skip++) {
		if (offset < count + iov[skip].iov_len) {
			break;
		}
		count += iov[skip].iov_len;
	}

	/* remove tose iov's from the list */
	for (i = 0; i < iovcnt - skip; i++) {
		iov[i] = iov[i + skip];
	}

	/* shift the current iov */
	offset -= count;
	iov[0].iov_base += offset;
	iov[0].iov_len -= offset;
	return iovcnt - skip;
}

size_t pmixp_writev_buf(int sd, struct iovec *iov, size_t iovcnt,
			size_t offset, int *shutdown)
{
	ssize_t ret;
	size_t size = 0, written = 0;
	int i;

	for (i=0; i < iovcnt; i++) {
		size += iov[i].iov_len;
	}

	/* Adjust initial buffer with the offset */
	iovcnt = _iov_shift(iov, iovcnt, offset);

	*shutdown = 0;

	while (size - (offset + written) > 0) {
		ret = writev(sd, iov, iovcnt);
		if (ret > 0) {
			written += ret;
			iovcnt = _iov_shift(iov, iovcnt, ret);
			continue;
		}
		switch (errno) {
		case EINTR:
			continue;
		case EWOULDBLOCK:
			return written;
		default:
			*shutdown = -errno;
			return written;
		}
	}

	return written;
}

bool pmixp_fd_read_ready(int fd, int *shutdown)
{
	struct pollfd pfd[1];
	int rc;
	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	/* Drop shutdown before the check */
	*shutdown = 0;

	rc = poll(pfd, 1, 0);
	if (rc < 0) {
		if (!(errno == EINTR)) {
			*shutdown = -errno;
			return false;
		}
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
	int rc = 0;
	struct timeval tv;
	double start, cur;
	pfd[0].fd = fd;
	pfd[0].events = POLLOUT;
	pfd[0].revents = 0;

	gettimeofday(&tv,NULL);
	start = tv.tv_sec + 1E-6*tv.tv_usec;
	cur = start;
	while ((cur - start) < 0.01) {
		rc = poll(pfd, 1, 10);

		/* update current timestamp */
		gettimeofday(&tv,NULL);
		cur = tv.tv_sec + 1E-6*tv.tv_usec;
		if (0 > rc) {
			if (errno == EINTR) {
				continue;
			} else {
				*shutdown = -errno;
				return false;
			}
		}
		break;
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

int pmixp_stepd_send(const char *nodelist, const char *address,
		     const char *data, uint32_t len,
		     unsigned int start_delay,
		     unsigned int retry_cnt, int silent)
{

	int retry = 0, rc;
	unsigned int delay = start_delay; /* in milliseconds */
	char *copy_of_nodelist = xstrdup(nodelist);

	while (1) {
		if (!silent && retry >= 1) {
			PMIXP_DEBUG("send failed, rc=%d, try #%d", rc, retry);
		}

		rc = slurm_forward_data(&copy_of_nodelist, (char *)address,
					len, data);

		if (rc == SLURM_SUCCESS)
			break;

		retry++;
		if (retry >= retry_cnt) {
			PMIXP_ERROR("send failed, rc=%d, exceeded the retry limit", rc);
			break;
		}

		/* wait with constantly increasing delay */
		struct timespec ts =
		{(delay / MSEC_IN_SEC),
		 ((delay % MSEC_IN_SEC) * NSEC_IN_MSEC)};
		nanosleep(&ts, NULL);
		delay *= 2;
	}
	xfree(copy_of_nodelist);

	return rc;
}

static int _pmix_p2p_send_core(const char *nodename, const char *address,
			       const char *data, uint32_t len)
{
	int rc, timeout;
	slurm_msg_t msg;
	forward_data_msg_t req;
	List ret_list;
	ret_data_info_t *ret_data_info = NULL;

	pmixp_debug_hang(0);

	slurm_msg_t_init(&msg);

	PMIXP_DEBUG("nodelist=%s, address=%s, len=%u", nodename, address, len);
	req.address = (char *)address;
	req.len = len;
	/* there is not much we can do - just cast) */
	req.data = (char*)data;

	msg.msg_type = REQUEST_FORWARD_DATA;
	msg.data = &req;

	if (slurm_conf_get_addr(nodename, &msg.address, msg.flags)
	    == SLURM_ERROR) {
		PMIXP_ERROR("Can't find address for host "
			    "%s, check slurm.conf", nodename);
		return SLURM_ERROR;
	}

	timeout = slurm_conf.msg_timeout * 1000;
	msg.forward.timeout = timeout;
	msg.forward.cnt = 0;
	msg.forward.nodelist = NULL;
	slurm_msg_set_r_uid(&msg, slurm_conf.slurmd_user_id);
	ret_list = slurm_send_addr_recv_msgs(&msg, (char*)nodename, timeout);
	if (!ret_list) {
		/* This should never happen (when this was
		 * written slurm_send_addr_recv_msgs always
		 * returned a list */
		PMIXP_ERROR("No return list given from "
			    "slurm_send_addr_recv_msgs spawned for %s",
			    nodename);
		return SLURM_ERROR;
	} else if ((errno != SLURM_COMMUNICATIONS_CONNECTION_ERROR) &&
		   !list_count(ret_list)) {
		PMIXP_ERROR("failed to send to %s, errno=%d", nodename, errno);
		return SLURM_ERROR;
	}

	rc = SLURM_SUCCESS;
	while ((ret_data_info = list_pop(ret_list))) {
		int temp_rc = slurm_get_return_code(ret_data_info->type,
						    ret_data_info->data);
		if (temp_rc != SLURM_SUCCESS)
			rc = temp_rc;
		destroy_data_info(ret_data_info);
	}

	FREE_NULL_LIST(ret_list);

	return rc;
}

int pmixp_p2p_send(const char *nodename, const char *address, const char *data,
		   uint32_t len, unsigned int start_delay,
		   unsigned int retry_cnt, int silent)
{
	int retry = 0, rc;
	unsigned int delay = start_delay; /* in milliseconds */

	pmixp_debug_hang(0);

	while (1) {
		if (!silent && retry >= 1) {
			PMIXP_DEBUG("send failed, rc=%d, try #%d", rc, retry);
		}

		rc = _pmix_p2p_send_core(nodename, address, data, len);

		if (rc == SLURM_SUCCESS)
			break;

		retry++;
		if (retry >= retry_cnt) {
			PMIXP_ERROR("send failed, rc=%d, exceeded the retry limit", rc);
			break;
		}

		/* wait with constantly increasing delay */
		struct timespec ts =
		{(delay / MSEC_IN_SEC),
		 ((delay % MSEC_IN_SEC) * NSEC_IN_MSEC)};
		nanosleep(&ts, NULL);
		delay *= 2;
	}

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
		if (0 == xstrcmp(ent->d_name, ".")
		    || 0 == xstrcmp(ent->d_name, "..")) {
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

static inline int _file_fix_rights(char *path, uid_t uid, mode_t mode)
{
	if (chmod(path, mode) < 0) {
		PMIXP_ERROR("chown(%s): %m", path);
		return errno;
	}

	if (chown(path, uid, (gid_t) -1) < 0) {
		PMIXP_ERROR("chown(%s): %m", path);
		return errno;
	}
	return 0;
}

int pmixp_fixrights(char *path, uid_t uid, mode_t mode)
{
	char nested_path[PATH_MAX];
	DIR *dp;
	struct dirent *ent;
	int rc = 0;

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
		if (0 == xstrcmp(ent->d_name, ".")
		    || 0 == xstrcmp(ent->d_name, "..")) {
			/* skip special dir's */
			continue;
		}
		snprintf(nested_path, sizeof(nested_path), "%s/%s", path,
			 ent->d_name);
		if (_is_dir(nested_path)) {
			if ((rc = _file_fix_rights(nested_path, uid, mode))) {
				PMIXP_ERROR_STD("cannot fix permissions for "
						"\"%s\"",
						nested_path);
				goto exit;
			}
			pmixp_rmdir_recursively(nested_path);
		} else {
			if ((rc = _file_fix_rights(nested_path, uid, mode))) {
				PMIXP_ERROR_STD("cannot fix permissions for "
						"\"%s\"",
						nested_path);
				goto exit;
			}
		}
	}

exit:
	closedir(dp);
	return rc;
}

int pmixp_mkdir(char *path, mode_t rights)
{
	/* NOTE: we need user who owns the job to access PMIx usock
	 * file. According to 'man 7 unix':
	 * "... In the Linux implementation, sockets which are visible in the
	 * file system honor the permissions of the directory  they are in... "
	 * Our case is the following: slurmstepd is usually running as root,
	 * user application will be "sudo'ed". To provide both of them with
	 * access to the unix socket we do the following:
	 * 1. Owner ID is set to the job owner.
	 * 2. Group ID corresponds to slurmstepd.
	 * 3. Set 0770 access mode
	 */

	if (0 != mkdir(path, rights) ) {
		PMIXP_ERROR_STD("Cannot create directory \"%s\"",
				path);
		return errno;
	}

	/* There might be umask that will drop essential rights.
	 * Fix it explicitly.
	 * TODO: is there more elegant solution? */
	if (chmod(path, rights) < 0) {
		error("%s: chown(%s): %m", __func__, path);
		return errno;
	}

	if (chown(path, (uid_t) pmixp_info_jobuid(), (gid_t) -1) < 0) {
		error("%s: chown(%s): %m", __func__, path);
		return errno;
	}
	return 0;
}
