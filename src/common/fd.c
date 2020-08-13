/*****************************************************************************\
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
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
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *****************************************************************************
 *  Refer to "fd.h" for documentation on public functions.
\*****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
strong_alias(fd_set_blocking,	slurm_fd_set_blocking);
strong_alias(fd_set_nonblocking,slurm_fd_set_nonblocking);
strong_alias(fd_get_socket_error, slurm_fd_get_socket_error);
strong_alias(send_fd_over_pipe, slurm_send_fd_over_pipe);
strong_alias(receive_fd_over_pipe, slurm_receive_fd_over_pipe);

static int fd_get_lock(int fd, int cmd, int type);
static pid_t fd_test_lock(int fd, int type);


void fd_set_close_on_exec(int fd)
{
	xassert(fd >= 0);

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		error("fcntl(F_SETFD) failed: %m");
	return;
}

void fd_set_noclose_on_exec(int fd)
{
	xassert(fd >= 0);

	if (fcntl(fd, F_SETFD, 0) < 0)
		error("fcntl(F_SETFD) failed: %m");
	return;
}

void fd_set_nonblocking(int fd)
{
	int fval;

	xassert(fd >= 0);

	if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
		error("fcntl(F_GETFL) failed: %m");
	if (fcntl(fd, F_SETFL, fval | O_NONBLOCK) < 0)
		error("fcntl(F_SETFL) failed: %m");
	return;
}

void fd_set_blocking(int fd)
{
	int fval;

	xassert(fd >= 0);

	if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
		error("fcntl(F_GETFL) failed: %m");
	if (fcntl(fd, F_SETFL, fval & ~O_NONBLOCK) < 0)
		error("fcntl(F_SETFL) failed: %m");
	return;
}

int fd_get_readw_lock(int fd)
{
	return(fd_get_lock(fd, F_SETLKW, F_RDLCK));
}


int fd_get_write_lock(int fd)
{
	return(fd_get_lock(fd, F_SETLK, F_WRLCK));
}

int fd_release_lock(int fd)
{
	return(fd_get_lock(fd, F_SETLK, F_UNLCK));
}


pid_t fd_is_read_lock_blocked(int fd)
{
	return(fd_test_lock(fd, F_RDLCK));
}

int fd_get_socket_error(int fd, int *err)
{
	socklen_t errlen = sizeof(err);

	xassert(fd >= 0);

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen))
		return errno;
	else
		return SLURM_SUCCESS;
}

static int fd_get_lock(int fd, int cmd, int type)
{
	struct flock lock;

	xassert(fd >= 0);

	lock.l_type = type;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	return(fcntl(fd, cmd, &lock));
}


static pid_t fd_test_lock(int fd, int type)
{
	struct flock lock;

	xassert(fd >= 0);

	lock.l_type = type;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	lock.l_pid = 0;	/* avoid valgrind error */

	if (fcntl(fd, F_GETLK, &lock) < 0)
		error("Unable to test for file lock: %m");
	if (lock.l_type == F_UNLCK)
		return(0);
	return(lock.l_pid);
}


/* Wait for a file descriptor to be readable (up to time_limit seconds).
 * Return 0 when readable or -1 on error */
extern int wait_fd_readable(int fd, int time_limit)
{
	struct pollfd ufd;
	time_t start;
	int rc, time_left;

	start = time(NULL);
	time_left = time_limit;
	ufd.fd = fd;
	ufd.events = POLLIN;
	ufd.revents = 0;
	while (1) {
		rc = poll(&ufd, 1, time_left * 1000);
		if (rc > 0) {	/* activity on this fd */
			if (ufd.revents & POLLIN)
				return 0;
			else	/* Exception */
				return -1;
		} else if (rc == 0) {
			error("Timeout waiting for slurmstepd");
			return -1;
		} else if (errno != EINTR) {
			error("poll(): %m");
			return -1;
		} else {
			time_left = time_limit - (time(NULL) - start);
		}
	}
}

/*
 * fsync() then close() a file.
 * Execute fsync() and close() multiple times if necessary and log failures
 * RET 0 on success or -1 on error
 */
extern int fsync_and_close(int fd, const char *file_type)
{
	int rc = 0, retval, pos;
	DEF_TIMERS;

	/*
	 * Slurm state save files are commonly stored on shared filesystems,
	 * so lets give fsync() three tries to sync the data to disk.
	 */
	START_TIMER;
	for (retval = 1, pos = 1; retval && pos < 4; pos++) {
		retval = fsync(fd);
		if (retval && (errno != EINTR)) {
			error("fsync() error writing %s state save file: %m",
			      file_type);
		}
	}
	END_TIMER2("fsync_and_close:fsync");
	if (retval)
		rc = retval;

	START_TIMER;
	for (retval = 1, pos = 1; retval && pos < 4; pos++) {
		retval = close(fd);
		if (retval && (errno != EINTR)) {
			error("close () error on %s state save file: %m",
			      file_type);
		}
	}
	END_TIMER2("fsync_and_close:close");
	if (retval)
		rc = retval;

	return rc;
}

extern char *fd_resolve_path(int fd)
{
	char *resolved = NULL;
	char *path = NULL;

#if defined(__linux__)
	struct stat sb = {0};

	path = xstrdup_printf("/proc/self/fd/%u", fd);
	if (lstat(path, &sb) == -1) {
		debug("%s: unable to lstat(%s): %m", __func__, path);
	} else {
		size_t name_len = sb.st_size + 1;
		resolved = xmalloc(name_len);
		if (readlink(path, resolved, name_len) <= 0) {
			debug("%s: unable to readlink(%s): %m",
			      __func__, path);
			xfree(resolved);
		}
	}
#endif

	// TODO: use fcntl(fd, F_GETPATH, filePath) on macOS

	xfree(path);
	return resolved;
}

extern void fd_set_oob(int fd, int value)
{
	if (setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &value, sizeof(value)))
		fatal("Unable disable inline OOB messages on socket: %m");
}

extern char *poll_revents_to_str(const short revents)
{
	char *txt = NULL;

	if (revents & POLLIN)
		xstrfmtcat(txt, "POLLIN");
	if (revents & POLLPRI)
		xstrfmtcat(txt, "%sPOLLPRI", (txt ? "|" : ""));
	if (revents & POLLOUT)
		xstrfmtcat(txt, "%sPOLLOUT", (txt ? "|" : ""));
	if (revents & POLLHUP)
		xstrfmtcat(txt, "%sPOLLHUP", (txt ? "|" : ""));
	if (revents & POLLNVAL)
		xstrfmtcat(txt, "%sPOLLNVAL", (txt ? "|" : ""));
	if (revents & POLLERR)
		xstrfmtcat(txt, "%sPOLLERR", (txt ? "|" : ""));

	if (!revents)
		xstrfmtcat(txt, "0");
	else
		xstrfmtcat(txt, "(0x%04" PRIx16 ")", revents);

	return txt;
}

/* pass an open file descriptor back to the parent process */
extern void send_fd_over_pipe(int socket, int fd)
{
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(fd))];
	memset(buf, '\0', sizeof(buf));

	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

	memmove(CMSG_DATA(cmsg), &fd, sizeof(fd));
	msg.msg_controllen = cmsg->cmsg_len;

	if (sendmsg(socket, &msg, 0) < 0)
		error("%s: failed to send fd: %m", __func__);
}

/* receive an open file descriptor from fork()'d child over unix socket */
extern int receive_fd_over_pipe(int socket)
{
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	int fd;
	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
	char c_buffer[256];
	msg.msg_control = c_buffer;
	msg.msg_controllen = sizeof(c_buffer);

	if (recvmsg(socket, &msg, 0) < 0) {
		error("%s: failed to receive fd: %m", __func__);
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg) {
		error("%s: CMSG_FIRSTHDR error: %m", __func__);
		return -1;
	}
	memmove(&fd, CMSG_DATA(cmsg), sizeof(fd));

	return fd;
}
