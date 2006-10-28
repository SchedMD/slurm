/*****************************************************************************\
 *  $Id$
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>


#define HOSTENT_SIZE 8192               /* cf. Stevens UNPv1 11.15 p304 */


struct hostent * get_host_by_name(const char *name,
    void *buf, int buflen, int *h_err);
/*
 *  A portable thread-safe alternative to be used in place of gethostbyname().
 *  The result is stored in the buffer (buf) of length (buflen); if the buffer
 *    is too small to hold the result, NULL is returned with errno = ERANGE.
 *  Returns a ptr into (buf) on success; returns NULL on error, setting the
 *    (h_err) variable reference (if not NULL) to indicate the h_error.
 */

struct hostent * get_host_by_addr(const char *addr, int len, int type,
    void *buf, int buflen, int *h_err);
/*
 *  A portable thread-safe alternative to be used in place of gethostbyaddr().
 *  The result is stored in the buffer (buf) of length (buflen); if the buffer
 *    is too small to hold the result, NULL is returned with errno = ERANGE.
 *  Returns a ptr into (buf) on success; returns NULL on error, setting the
 *    (h_err) variable reference (if not NULL) to indicate the h_error.
 */

const char * host_strerror(int h_err);
/*
 *  Returns a string describing the error code (h_err) returned by
 *    get_host_by_name() or get_host_by_addr().
 */

int host_name_to_addr4(const char *name, struct in_addr *addr);
/*
 *  Converts the string (name) to an IPv4 address (addr).
 *  Returns 0 on success, or -1 on error.
 *  Note that this routine is thread-safe.
 */

char * host_addr4_to_name(const struct in_addr *addr, char *dst, int dstlen);
/*
 *  Converts an IPv4 address (addr) to a host name string residing in
 *    buffer (dst) of length (dstlen).
 *  Returns a ptr to the NULL-terminated string (dst) on success,
 *    or NULL on error.
 *  Note that this routine is thread-safe.
 */

char * host_name_to_cname(const char *src, char *dst, int dstlen);
/*
 *  Converts the hostname or IP address string (src) to the
 *    canonical name of the host residing in buffer (dst) of length (dstlen).
 *  Returns a ptr to the NULL-terminated string (dst) on success,
 *    or NULL on error.
 *  Note that this routine is thread-safe.
 */

#ifndef HAVE_INET_PTON
int inet_pton(int family, const char *str, void *addr);
/*
 *  Convert from presentation format of an internet number in (str)
 *    to the binary network format, storing the result for interface
 *    type (family) in the socket address structure specified by (addr).
 *  Returns 1 if OK, 0 if input not a valid presentation format, -1 on error.
 */
#endif /* !HAVE_INET_PTON */

#ifndef HAVE_INET_NTOP
const char * inet_ntop(int family, const void *addr, char *str, size_t len);
/*   
 *  Convert an Internet address in binary network format for interface
 *    type (family) in the socket address structure specified by (addr),
 *    storing the result in the buffer (str) of length (len).
 *  Returns ptr to result buffer if OK, or NULL on error.
 */
#endif /* !HAVE_INET_NTOP */


#endif /* !_UTIL_NET_H */
