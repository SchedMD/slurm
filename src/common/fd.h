/*****************************************************************************\
 *  $Id$
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"

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

int fd_get_read_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
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

int fd_get_writew_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
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

pid_t fd_is_write_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a write-lock
 *    (ie, if any lock is already being held on the file),
 *    returns the pid of a process holding the lock; o/w, returns 0.
 */

ssize_t fd_read_n(int fd, void *buf, size_t n);
/*
 *  Reads up to (n) bytes from (fd) into (buf).
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */

ssize_t fd_write_n(int fd, void *buf, size_t n);
/*
 *  Writes (n) bytes from (buf) to (fd).
 *  Returns the number of bytes written, or -1 on error.
 */

ssize_t fd_read_line(int fd, void *buf, size_t maxlen);
/*
 *  Reads at most (maxlen-1) bytes up to a newline from (fd) into (buf).
 *  The (buf) is guaranteed to be NUL-terminated and will contain the
 *    newline if it is encountered within (maxlen-1) bytes.
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */

int fd_is_blocking(int fd);
/*
 * Return 1 if the file specified by the file descriptor is blocking.
 * Return 0 otherwise.
 */

#endif /* !_FD_H */
