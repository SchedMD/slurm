/*****************************************************************************\
 *  util-net.h - assorted network functions
 *****************************************************************************
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
\*****************************************************************************/

#ifndef _UTIL_NET_H
#define _UTIL_NET_H

#define _GNU_SOURCE

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

struct hostent * get_host_by_name(const char *name,
    void *buf, int buflen, int *h_err);
/*
 *  A portable thread-safe alternative to be used in place of gethostbyname().
 *  The result is stored in the buffer (buf) of length (buflen); if the buffer
 *    is too small to hold the result, NULL is returned with errno = ERANGE.
 *  Returns a ptr into (buf) on success; returns NULL on error, setting the
 *    (h_err) variable reference (if not NULL) to indicate the h_error.
 */

/* is_full_path()
 *
 * Test if the given path is a full or relative one.
 */
extern bool is_full_path(const char *);

/* make_full_path()
 *
 * Given a relative path in input make it full relative
 * to the current working directory.
 */
extern char *make_full_path(const char *);

/* Wrapper functions for name and address resolution routines.
 */
extern struct addrinfo *get_addr_info(const char *hostname, uint16_t port);
extern char *xgetnameinfo(struct sockaddr *addr, socklen_t addrlen);

#endif /* !_UTIL_NET_H */
