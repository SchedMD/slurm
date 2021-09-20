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
 *  Refer to "util-net.h" for documentation on public functions.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>	/* for PATH_MAX */
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "src/common/read_config.h"
#include "src/common/strlcpy.h"
#include "src/common/util-net.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

static pthread_mutex_t hostentLock = PTHREAD_MUTEX_INITIALIZER;


static int copy_hostent(const struct hostent *src, char *dst, int len);
#ifndef NDEBUG
static int validate_hostent_copy(
	const struct hostent *src, const struct hostent *dst);
#endif /* !NDEBUG */


struct hostent * get_host_by_name(const char *name,
				  void *buf, int buflen, int *h_err)
{
/*  gethostbyname() is not thread-safe, and there is no frelling standard
 *    for gethostbyname_r() -- the arg list varies from system to system!
 */
	struct hostent *hptr;
	int n = 0;

	xassert(name && buf);

	slurm_mutex_lock(&hostentLock);
	/* It appears gethostbyname leaks memory once.  Under the covers it
	 * calls gethostbyname_r (at least on Ubuntu 16.10).  This leak doesn't
	 * appear to get worst, meaning it only happens once, so we should be
	 * ok.  Though gethostbyname is obsolete now we can't really change
	 * since aliases don't work we can't change.
	 */
	if ((hptr = gethostbyname(name)))
		n = copy_hostent(hptr, buf, buflen);
	if (h_err)
		*h_err = h_errno;
	slurm_mutex_unlock(&hostentLock);

	if (n < 0) {
		errno = ERANGE;
		return(NULL);
	}
	return(hptr ? (struct hostent *) buf : NULL);
}

static int copy_hostent(const struct hostent *src, char *buf, int len)
{
/*  Copies the (src) hostent struct (and all of its associated data)
 *    into the buffer (buf) of length (len).
 *  Returns 0 if the copy is successful, or -1 if the length of the buffer
 *    is not large enough to hold the result.
 *
 *  Note that the order in which data is copied into (buf) is done
 *    in such a way as to ensure everything is properly word-aligned.
 *    There is a method to the madness.
 */
	struct hostent *dst;
	int n;
	char **p, **q;

	xassert(src && buf);

	dst = (struct hostent *) buf;
	if ((len -= sizeof(struct hostent)) < 0)
		return(-1);
	dst->h_addrtype = src->h_addrtype;
	dst->h_length = src->h_length;
	buf += sizeof(struct hostent);

	/*  Reserve space for h_aliases[].
	 */
	dst->h_aliases = (char **) buf;
	for (p=src->h_aliases, q=dst->h_aliases, n=0; *p; p++, q++, n++) {;}
	if ((len -= ++n * sizeof(char *)) < 0)
		return(-1);
	buf = (char *) (q + 1);

	/*  Reserve space for h_addr_list[].
	 */
	dst->h_addr_list = (char **) buf;
	for (p=src->h_addr_list, q=dst->h_addr_list, n=0; *p; p++, q++, n++) {;}
	if ((len -= ++n * sizeof(char *)) < 0)
		return(-1);
	buf = (char *) (q + 1);

	/*  Copy h_addr_list[] in_addr structs.
	 */
	for (p=src->h_addr_list, q=dst->h_addr_list; *p; p++, q++) {
		if ((len -= src->h_length) < 0)
			return(-1);
		memcpy(buf, *p, src->h_length);
		*q = buf;
		buf += src->h_length;
	}
	*q = NULL;

	/*  Copy h_aliases[] strings.
	 */
	for (p=src->h_aliases, q=dst->h_aliases; *p; p++, q++) {
		n = strlcpy(buf, *p, len);
		*q = buf;
		buf += ++n;                     /* allow for trailing NUL char */
		if ((len -= n) < 0)
			return(-1);
	}
	*q = NULL;

	/*  Copy h_name string.
	 */
	dst->h_name = buf;
	n = strlcpy(buf, src->h_name, len);
	buf += ++n;                         /* allow for trailing NUL char */
	if ((len -= n) < 0)
		return(-1);

	xassert(validate_hostent_copy(src, dst) >= 0);
	xassert(buf);	/* Used only to eliminate CLANG error */
	return(0);
}


#ifndef NDEBUG
static int validate_hostent_copy(
	const struct hostent *src, const struct hostent *dst)
{
/*  Validates the src hostent struct has been successfully copied into dst.
 *  Returns 0 if the copy is good; o/w, returns -1.
 */
	char **p, **q;

	xassert(src && dst);

	if (!dst->h_name)
		return(-1);
	if (src->h_name == dst->h_name)
		return(-1);
	if (xstrcmp(src->h_name, dst->h_name))
		return(-1);
	if (src->h_addrtype != dst->h_addrtype)
		return(-1);
	if (src->h_length != dst->h_length)
		return(-1);
	for (p=src->h_aliases, q=dst->h_aliases; *p; p++, q++)
		if ((!q) || (p == q) || (xstrcmp(*p, *q)))
			return(-1);
	for (p=src->h_addr_list, q=dst->h_addr_list; *p; p++, q++)
		if ((!q) || (p == q) || (memcmp(*p, *q, src->h_length)))
			return(-1);
	return(0);
}
#endif /* !NDEBUG */

/* is_full_path()
 *
 * Test if the given path is a full or relative one.
 */
extern
bool is_full_path(const char *path)
{
	if (path && path[0] == '/')
		return true;

	return false;
}

/* make_full_path()
 *
 * Given a relative path in input make it full relative
 * to the current working directory.
 */
extern char *make_full_path(const char *rpath)
{
	char *cwd;
	char *cwd2 = NULL;

#ifdef HAVE_GET_CURRENT_DIR_NAME
	cwd = get_current_dir_name();
#else
	cwd = malloc(PATH_MAX);
	cwd = getcwd(cwd, PATH_MAX);
#endif
	xstrfmtcat(cwd2, "%s/%s", cwd, rpath);
	free(cwd);

	return cwd2;
}

struct addrinfo *get_addr_info(const char *hostname, uint16_t port)
{
	struct addrinfo* result = NULL;
	struct addrinfo hints;
	int err;
	bool v4_enabled = slurm_conf.conf_flags & CTL_CONF_IPV4_ENABLED;
	bool v6_enabled = slurm_conf.conf_flags & CTL_CONF_IPV6_ENABLED;
	char serv[6];

	memset(&hints, 0, sizeof(hints));

	/* use configured IP support to hint at what address types to return */
	if (v4_enabled && !v6_enabled)
		hints.ai_family = AF_INET;
	else if (!v4_enabled && v6_enabled)
		hints.ai_family = AF_INET6;
	else
		hints.ai_family = AF_UNSPEC;

	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
	if (hostname)
		hints.ai_flags |= AI_CANONNAME;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(serv, sizeof(serv), "%u", port);

	err = getaddrinfo(hostname, serv, &hints, &result);
	if (err == EAI_SYSTEM) {
		error("%s: getaddrinfo() failed: %s: %m", __func__,
		      gai_strerror(err));
		return NULL;
	} else if (err != 0) {
		error("%s: getaddrinfo() failed: %s", __func__,
		      gai_strerror(err));
		return NULL;
	}

	return result;
}

/*
 * Get the short hostname using "nameinfo" for an address.
 * NOTE: caller is responsible for freeing the resulting address.
 * Returns NULL on error.
 */
extern char *xgetnameinfo(struct sockaddr *addr, socklen_t addrlen)
{
	char hbuf[NI_MAXHOST];
	int err = getnameinfo(addr, addrlen, hbuf, sizeof(hbuf), NULL, 0,
			      NI_NAMEREQD);

	if (err == EAI_SYSTEM) {
		error("%s: getnameinfo() failed: %s: %m",
		      __func__, gai_strerror(err));
		return NULL;
	} else if (err) {
		error("%s: getnameinfo() failed: %s",
		      __func__, gai_strerror(err));
		return NULL;
	}

	return xstrdup(hbuf);
}
