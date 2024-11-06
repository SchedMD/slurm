/*****************************************************************************\
 *  fd.h - common file descriptor functions
 *****************************************************************************
 *  Copyright (C) 2001-2006 The Regents of the University of California.
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
\*****************************************************************************/

#ifndef _FD_H
#define _FD_H

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"

/* close all FDs >= a specified value */
extern void closeall(int fd);

/*
 * close all FDs >= a specified value except FDs in skipped array
 * IN fd - start closing file descriptors at fd
 * IN skipped - array of file descriptors to skip (or NULL)
 *              array must be terminated by -1 if provided
 */
extern void closeall_except(int fd, int *skipped);

/* Close a specific file descriptor and replace it with -1 */
extern void fd_close(int *fd);

void fd_set_close_on_exec(int fd);
/*
 *  Sets the file descriptor (fd) to be closed on exec().
 */

void fd_set_noclose_on_exec(int fd);
/*
 *  Sets the file descriptor (fd) to NOT be closed on exec().
 */

void fd_set_nonblocking(int fd);
/*
 *  Sets the file descriptor (fd) for non-blocking I/O.
 */

void fd_set_blocking(int fd);
/*
 * Sets the file descriptor (fd) for blocking I/O.
 */

int fd_get_readw_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
 */

int fd_get_write_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
 */

int fd_release_lock(int fd);
/*
 *  Release a lock held on the file specified by (fd).
 *  Returns 0 on success, or -1 on error.
 */

pid_t fd_is_read_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a read-lock
 *    (ie, if a write-lock is already being held on the file),
 *    returns the pid of the process holding the lock; o/w, returns 0.
 */

/* return true if fd is writable right now */
extern bool fd_is_writable(int fd);

extern int wait_fd_readable(int fd, int time_limit);
/* Wait for a file descriptor to be readable (up to time_limit seconds).
 * Return 0 when readable or -1 on error */

/*
 * fsync() then close() a file.
 * Execute fsync() and close() multiple times if necessary and log failures
 * RET 0 on success or -1 on error
 */
extern int fsync_and_close(int fd, const char *file_type);

/*
 * Sets err to socket error
 * Returns SLURM_SUCCESS or errno value from getsockopt on error
 * */
int fd_get_socket_error(int fd, int *err);

/*
 * Expand symlink for the specified file descriptor from /proc/self/fd/
 *
 * A NULL-terminated string with the contents of the symlink is produced,
 * with a maximum of PATH_MAX + 1 bytes. For a socket/pipe it will contain the
 * name. For regular files or fds it will contain a path.
 *
 * The caller should deallocate the returned string using xfree().
 *
 * IN fd - file descriptor to resolve symlink from
 * RET ptr to a string with the contents of the symlink. NULL if fd path cannot
 * be resolved.
 *
 */
extern char *fd_resolve_path(int fd);

/*
 * Resolve peer address for a given socket (fd)
 *
 * Explicitly preserves value of errno.
 *
 * IN fd - file descriptor to resolve peer
 * IN RET ptr to a peer address (must xfree()) or NULL on failure
 */
extern char *fd_resolve_peer(int fd);

/*
 * Set inline Out of Band (OOB) data on socket fd
 */
extern void fd_set_oob(int fd, int value);

/*
 * Dump poll() revents flags to string
 * IN revents - revents from poll fds array entry
 * RET string with flags (must xfree())
 */
extern char *poll_revents_to_str(const short revents);

/*
 * Pass an open fd back over a socket.
 */
extern void send_fd_over_socket(int socket, int fd);
extern int receive_fd_over_socket(int socket);

/*
 * Make full directory path.
 *
 * Will not error if directories already exist.
 * Warning: directory creation is a not an atomic operation.
 * This function iteratively builds the path until complete, or an error
 * occurs.
 *
 * IN is_dir:
 *   true: last path component is a directory, and should be created
 *   false: last path component is a filename, do not create
 *
 * RET SLURM_SUCCESS or error.
 */
extern int mkdirpath(const char *pathname, mode_t mode, bool is_dir);

/*
 * Recursively remove a directory and all contents.
 * Takes care not to follow any symlinks outside the target directory.
 *
 * Returns the number of files/directories it failed to remove,
 * or 0 on success.
 */
extern int rmdir_recursive(const char *path, bool remove_top);

/*
 * Use ioctl(FIONREAD) to get number of bytes in buffer waiting for read().
 * IN fd - file descriptor to query
 * IN/OUT readable_ptr - Pointer to populate if ioctl() is able to query
 *	successfully. Only changed if RET=SLURM_SUCCESS.
 * IN con_name - descriptive name for fd connection (for logging)
 * RET SLURM_SUCCESS or error
 */
extern int fd_get_readable_bytes(int fd, int *readable_ptr,
				 const char *con_name);

/*
 * Use ioctl(TIOCOUTQ) to get number of bytes in buffer waiting for kernel to
 * send to destination
 * IN fd - file descriptor to query
 * IN/OUT bytes_ptr - Pointer to populate if ioctl() is able to query
 *	successfully. Only changed if RET=SLURM_SUCCESS.
 * IN con_name - descriptive name for fd connection (for logging)
 * RET SLURM_SUCCESS or error
 */
extern int fd_get_buffered_output_bytes(int fd, int *bytes_ptr,
					const char *con_name);

/*
 * Get TCP MSS (Max Segment Size) of a given socket
 * IN fd - file descriptor for socket
 * IN con_name - Connection name (for logging) or NULL
 * RET NO_VAL on failure or >0 for MSS of socket
 */
extern int fd_get_maxmss(int fd, const char *con_name);

#endif /* !_FD_H */
