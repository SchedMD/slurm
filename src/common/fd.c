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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Set minimum MSS matching TCP MSDS from RFC#879 */
#define MSS_MIN_BYTES 556

/*
 * Helper macro to log_flag(NET, ...) against a given connection
 * IN fd - file descriptor relavent for logging
 * IN con_name - human friendly name for fd or NULL (to auto resolve)
 * IN fmt - log message format
 */
#define log_net(fd, con_name, fmt, ...) \
do { \
	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) { \
		char *log_name = NULL; \
		if (!con_name) \
			log_name = fd_resolve_path(fd); \
		log_flag(NET, "%s: [%s] " fmt, \
			 __func__, con_name ? con_name : log_name, \
			 ##__VA_ARGS__); \
		xfree(log_name); \
	} \
} while (false)

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(closeall, slurm_closeall);
strong_alias(closeall_except, slurm_closeall_except);
strong_alias(fd_close, slurm_fd_close);
strong_alias(fd_set_blocking,	slurm_fd_set_blocking);
strong_alias(fd_set_nonblocking,slurm_fd_set_nonblocking);
strong_alias(fd_get_socket_error, slurm_fd_get_socket_error);
strong_alias(send_fd_over_socket, slurm_send_fd_over_socket);
strong_alias(receive_fd_over_socket, slurm_receive_fd_over_socket);
strong_alias(rmdir_recursive, slurm_rmdir_recursive);

static int fd_get_lock(int fd, int cmd, int type);
static pid_t fd_test_lock(int fd, int type);

static bool _is_fd_skipped(int fd, int *skipped)
{
	if (!skipped)
		return false;

	for (int i = 0; skipped[i] >= 0; i++)
		if (fd == skipped[i])
			return true;

	return false;
}

static void _slow_closeall(int fd, int *skipped)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		error("getrlimit(RLIMIT_NOFILE): %m");
		rlim.rlim_cur = 4096;
	}

	for (; fd < rlim.rlim_cur; fd++)
		if (!_is_fd_skipped(fd, skipped))
			close(fd);
}

extern void closeall_except(int fd, int *skipped)
{
	char *name = "/proc/self/fd";
	DIR *d;
	struct dirent *dir;

	/*
	 * Blindly closing all file descriptors is slow.
	 *
	 * Instead get all open file descriptors from /proc/self/fd, then
	 * close each one of those that are greater than or equal to fd.
	 */
	if (!(d = opendir(name))) {
		debug("Could not read open files from %s: %m, closing all potential file descriptors",
		      name);
		_slow_closeall(fd, skipped);
		return;
	}

	while ((dir = readdir(d))) {
		/* Ignore "." and ".." entries */
		if (dir->d_type != DT_DIR) {
			int open_fd = atoi(dir->d_name);

			if ((open_fd >= fd) &&
			    !_is_fd_skipped(open_fd, skipped))
				close(open_fd);
		}
	}
	closedir(d);
}

extern void closeall(int fd)
{
	return closeall_except(fd, NULL);
}

extern void fd_close(int *fd)
{
	if (fd && *fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

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
	socklen_t errlen = sizeof(*err);

	xassert(fd >= 0);

	*err = SLURM_SUCCESS;

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)err, &errlen))
		return errno;
	else {
		/*
		 * SOL_SOCKET/SO_ERROR may not find an error and will not set
		 * errno. This may happen if on duplicate calls or if something
		 * else has cleared the error.
		 */
		if (!(*err))
			*err = SLURM_COMMUNICATIONS_MISSING_SOCKET_ERROR;
		return SLURM_SUCCESS;
	}
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
			error("Timeout waiting for socket");
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
 * Check if a file descriptor is writable now.
 *
 * This function assumes that O_NONBLOCK is set already, if it is not this
 * function will block!
 *
 * Return 1 when writeable or 0 on error
 */
extern bool fd_is_writable(int fd)
{
	bool rc = true;
	char temp[2];
	struct pollfd ufd;

	/* setup call to poll */
	ufd.fd = fd;
	ufd.events = POLLOUT;

	while (true) {
		if (poll(&ufd, 1, 0) == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			debug2("%s: poll error: %m", __func__);
			rc = false;
			break;
		}
		if ((ufd.revents & POLLHUP) ||
		    (recv(fd, &temp, 1, MSG_PEEK) == 0)) {
			debug2("%s: socket is not writable", __func__);
			rc = false;
			break;
		}
		break;
	}

	return rc;
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

/*
 * Attempt to expand symlink for the specified file descriptor.
 *
 * The caller should deallocate the returned string using xfree().
 *
 * IN fd - file descriptor to resolve symlink from
 * RET ptr to a string containing the resolved symlink or NULL if failure.
 */
extern char *fd_resolve_path(int fd)
{
	char *resolved = NULL;
	char *path = NULL;

#if defined(__linux__)
	char ret[PATH_MAX + 1];
	ssize_t bytes;

	path = xstrdup_printf("/proc/self/fd/%u", fd);
	memset(ret, 0, (PATH_MAX + 1));
	bytes = readlink(path, ret, PATH_MAX);

	if (bytes < 0)
		debug("%s: readlink(%s) failed: %m", __func__,  path);
	else if (bytes >= PATH_MAX)
		debug("%s: rejecting readlink(%s) for possble truncation",
		      __func__, path);
	else
		resolved = xstrdup(ret);
#endif
	// TODO: use fcntl(fd, F_GETPATH, filePath) on macOS

	xfree(path);
	return resolved;
}

extern char *fd_resolve_peer(int fd)
{
	slurm_addr_t addr = {0};
	socklen_t size = sizeof(addr);
	int err = errno;
	char *peer;

	if (fd < 0)
		return NULL;

	if (slurm_get_peer_addr(fd, &addr)) {
		log_flag(NET, "%s: unable to resolve peername for fd:%d: %m",
			 __func__, fd);
		return NULL;
	}

	peer = sockaddr_to_string(&addr, size);

	errno = err;
	return peer;
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
		xstrfmtcat(txt, "(0x%04x)", revents);

	return txt;
}

/* pass an open file descriptor back to the requesting process */
extern void send_fd_over_socket(int socket, int fd)
{
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(fd))];
	char c = '\0';
	struct iovec iov[1];

	memset(buf, '\0', sizeof(buf));

	iov[0].iov_base = &c;
	iov[0].iov_len = sizeof(c);
	msg.msg_iov = iov;
	msg.msg_iovlen = sizeof(c);
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

/* receive an open file descriptor over unix socket */
extern int receive_fd_over_socket(int socket)
{
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	int fd;
	char c_buffer[256];
	char c;
	struct iovec iov[1];

	iov[0].iov_base = &c;
	iov[0].iov_len = sizeof(c);
	msg.msg_iov = iov;
	msg.msg_iovlen = sizeof(c);
	msg.msg_control = c_buffer;
	msg.msg_controllen = sizeof(c_buffer);

	if (recvmsg(socket, &msg, 0) < 0) {
		error("%s: failed to receive fd: %m", __func__);
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg) {
		error("%s: CMSG_FIRSTHDR failed", __func__);
		return -1;
	}
	memmove(&fd, CMSG_DATA(cmsg), sizeof(fd));

	return fd;
}

static int _mkdir(const char *pathname, mode_t mode)
{
	int rc;

	if ((rc = mkdir(pathname, mode)))
		rc = errno;
	else
		return SLURM_SUCCESS;

	if (rc == EEXIST)
		return SLURM_SUCCESS;

	debug("%s: unable to mkdir(%s): %s",
	      __func__, pathname, slurm_strerror(rc));

	return rc;
}

extern int mkdirpath(const char *pathname, mode_t mode, bool is_dir)
{
	int rc;
	char *p, *dst;

	p = dst = xstrdup(pathname);

	while ((p = xstrchr(p + 1, '/'))) {
		*p = '\0';

		if ((rc = _mkdir(dst, mode)))
			goto cleanup;

		*p = '/';
	}

	/* final directory */
	if (is_dir)
		rc = _mkdir(dst, mode);

cleanup:
	xfree(dst);
	return rc;
}

static int _rmdir_recursive(int dirfd)
{
	int rc = 0;
	DIR *dp;
	struct dirent *ent;

	if (!(dp = fdopendir(dirfd))) {
		error("%s: can't open directory: %m", __func__);
		(void) close(dirfd);
		return 1;
	}

	while ((ent = readdir(dp))) {
		int childfd = -1;

		/* skip special directories */
		if (!strcmp(ent->d_name, ".") ||
		    !strcmp(ent->d_name, "..")) {
			continue;
		}

		/* try to remove entry, first as a file, then as a directory */
		if (unlinkat(dirfd, ent->d_name, 0) != -1) {
			debug("%s: removed file `%s`", __func__, ent->d_name);
			continue;
		} else if (unlinkat(dirfd, ent->d_name, AT_REMOVEDIR) != -1) {
			debug("%s: removed empty directory `%s`",
			      __func__, ent->d_name);
			continue;
		}

		/* removal didn't work. assume it's a non-empty directory */
		if ((childfd = openat(dirfd, ent->d_name,
				      (O_DIRECTORY | O_NOFOLLOW))) < 0) {
			debug("%s: openat() failed for `%s`: %m",
			      __func__, ent->d_name);
			rc++;
			continue;
		}

		debug("%s: descending into directory `%s`",
		      __func__, ent->d_name);
		rc += _rmdir_recursive(childfd);

		if (unlinkat(dirfd, ent->d_name, AT_REMOVEDIR) != -1) {
			debug("%s: removed now-empty directory `%s`",
			      __func__, ent->d_name);
		} else {
			debug("%s: unlinkat() failed for `%s`: %m",
			      __func__, ent->d_name);
			rc++;
		}
	}
	closedir(dp);

	return rc;
}

extern int rmdir_recursive(const char *path, bool remove_top)
{
	int rc = 0;
	int dirfd;

	if ((dirfd = open(path, O_DIRECTORY | O_NOFOLLOW)) < 0) {
		error("%s: could not open %s", __func__, path);
		return 1;
	}

	rc = _rmdir_recursive(dirfd);

	if (remove_top) {
		if (rmdir(path) < 0) {
			debug("%s: rmdir() failed for `%s`: %m",
			      __func__, path);
			rc++;
		} else {
			debug("%s: removed now-empty top directory `%s`",
			      __func__, path);
		}
	}

	if (rc)
		error("%s: could not completely remove `%s`, %d files left",
		      __func__, path, rc);

	return rc;
}

extern int fd_get_readable_bytes(int fd, int *readable_ptr,
				 const char *con_name)
{
#ifdef FIONREAD
	/* default readable to max positive 32 bit signed integer */
	int readable = INT32_MAX;

	/* assert readable_ptr is set but gracefully allow for it not to be */
	xassert(readable_ptr);

	if (fd < 0) {
		log_net(fd, con_name,
			"Refusing request for ioctl(%d, FIONREAD) with invalid file descriptor: %d",
			fd, fd);
		return EINVAL;
	}

	/* request kernel tell us the size of the incoming buffer */
	if (ioctl(fd, FIONREAD, &readable)) {
		int rc = errno;
		log_net(fd, con_name,
			"ioctl(%d, FIONREAD, 0x%"PRIxPTR") failed: %s",
			fd, (uintptr_t) &readable, slurm_strerror(rc));
		return rc;
	}

	/* validate response from kernel is sane (or likely sane) */
	if (readable < 0) {
		/* invalid FIONREAD response -> bad driver response */
		log_net(fd, con_name,
			"Invalid response: ioctl(%d, FIONREAD, 0x%"PRIxPTR")=%d",
			 fd, (uintptr_t) &readable, readable);
		return ENOSYS;
	}
	/* verify if readable was even set */
	if (readable == INT32_MAX) {
		/* ioctl() did not error but did not change readable?? */
		log_net(fd, con_name,
			"Invalid unchanged readable value: ioctl(%d, FIONREAD, 0x%"PRIxPTR")=%d",
			fd, (uintptr_t) &readable, readable);
		return ENOSYS;
	}

	if (readable_ptr) {
		*readable_ptr = readable;

		log_net(fd, con_name,
			"Successful query: ioctl(%d, FIONREAD, 0x%"PRIxPTR")=%d",
			 fd, (uintptr_t) readable_ptr, readable);
	}

	return SLURM_SUCCESS;
#else /* FIONREAD */
	return ESLURM_NOT_SUPPORTED;
#endif /* !FIONREAD */
}

extern int fd_get_buffered_output_bytes(int fd, int *bytes_ptr,
					const char *con_name)
{
#ifdef TIOCOUTQ
	/* default pending to max positive 32 bit signed integer */
	int pending = INT32_MAX;

	xassert(bytes_ptr);

	if (fd < 0) {
		log_net(fd, con_name,
			"Refusing request for ioctl(%d, TIOCOUTQ) with invalid file descriptor: %d",
			fd, fd);
		return EINVAL;
	}

	/*
	 * Request kernel tell us the number of bytes remain in the outgoing
	 * buffer.
	 */
	if (ioctl(fd, TIOCOUTQ, &pending)) {
		int rc = errno;
		log_net(fd, con_name,
			"ioctl(%d, TIOCOUTQ, 0x%"PRIxPTR") failed: %s",
			fd, (uintptr_t) &pending, slurm_strerror(rc));
		return rc;
	}

	/* validate response from kernel is sane (or likely sane) */
	if (pending < 0) {
		/* invalid TIOCOUTQ response -> bad driver response */
		log_net(fd, con_name,
			"Invalid response: ioctl(%d, TIOCOUTQ, 0x%"PRIxPTR")=%d",
			 fd, (uintptr_t) &pending, pending);
		return ENOSYS;
	}
	/* verify if pending was even set */
	if (pending == INT32_MAX) {
		/* ioctl() did not error but did not change pending?? */
		log_net(fd, con_name,
			"Invalid unchanged pending value: ioctl(%d, TIOCOUTQ, 0x%"PRIxPTR")=%d",
			fd, (uintptr_t) &pending, pending);
		return ENOSYS;
	}

	*bytes_ptr = pending;

	log_net(fd, con_name,
		"Successful query: ioctl(%d, TIOCOUTQ, 0x%"PRIxPTR")=%d",
		fd, (uintptr_t) bytes_ptr, pending);

	return SLURM_SUCCESS;
#else /* TIOCOUTQ */
	return ESLURM_NOT_SUPPORTED;
#endif /* !TIOCOUTQ */
}

extern int fd_get_maxmss(int fd, const char *con_name)
{
	int mss = NO_VAL;
	socklen_t tmp_socklen = { 0 };

	if (getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &mss, &tmp_socklen))
		log_net(fd, con_name,
			"getsockopt(%d, IPPROTO_TCP, TCP_MAXSEG) failed: %m",
			fd);
	else
		log_net(fd, con_name,
			"getsockopt(%d, IPPROTO_TCP, TCP_MAXSEG)=%d", fd, mss);

	if ((mss < MSS_MIN_BYTES) || (mss > MAX_MSG_SIZE)) {
		log_net(fd, con_name,
			"Rejecting invalid response from getsockopt(%d, IPPROTO_TCP, TCP_MAXSEG)=%d",
			fd, mss);
		mss = NO_VAL;
	}

	return mss;
}
