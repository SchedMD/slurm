/*****************************************************************************\
 *  callerid.c - Identify initiator of ssh connections, etc
 *****************************************************************************
 *  Copyright (C) 2015, Brigham Young University
 *  Author:  Ryan Cox <ryan_cox@byu.edu>
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

#include "config.h"

#define _GNU_SOURCE

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/*
 * FIXME: In in6.h, s6_addr32 def is guarded by #ifdef _KERNEL
 * Is there a portable interface that could be used instead of accessing
 * structure members directly?
 */
#if defined(__FreeBSD__) || defined(__NetBSD__)
#define s6_addr32 __u6_addr.__u6_addr32
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/callerid.h"
#include "src/common/log.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#ifndef PATH_PROCNET_TCP
#define PATH_PROCNET_TCP "/proc/net/tcp"
#endif
#ifndef PATH_PROCNET_TCP6
#define PATH_PROCNET_TCP6 "/proc/net/tcp6"
#endif

strong_alias(callerid_get_own_netinfo, slurm_callerid_get_own_netinfo);

static int _match_inode(callerid_conn_t *conn_result, ino_t *inode_search,
		callerid_conn_t *conn_row, ino_t inode_row, int af)
{
	if (*inode_search == inode_row) {
		memcpy(&conn_result->ip_dst, &conn_row->ip_dst, 16);
		memcpy(&conn_result->ip_src, &conn_row->ip_src, 16);
		conn_result->port_src = conn_row->port_src;
		conn_result->port_dst = conn_row->port_dst;
		conn_result->af = af;
		debug3("_match_inode matched");
		return SLURM_SUCCESS;
	}
	return SLURM_FAILURE;
}

static int _match_conn(callerid_conn_t *conn_search, ino_t *inode_result,
		callerid_conn_t *conn_row, ino_t inode_row, int af)
{
	int addrbytes = af == AF_INET ? 4 : 16;

	if (conn_search->port_dst != conn_row->port_dst ||
	    conn_search->port_src != conn_row->port_src ||
	    memcmp((void*)&conn_search->ip_dst, (void*)&conn_row->ip_dst,
		   addrbytes) !=0 ||
	    memcmp((void*)&conn_search->ip_src, (void*)&conn_row->ip_src,
		   addrbytes) !=0
	   )
		return SLURM_FAILURE;

	debug3("_match_conn matched inode %lu", (long unsigned int)inode_row);
	*inode_result = inode_row;
	return SLURM_SUCCESS;
}

/* Note that /proc/net/tcp, etc. can be updated *while reading* but a read on
 * each row is atomic: http://stackoverflow.com/a/5880485.
 *
 * This should be safe but may potentially miss an entry due to the entry
 * moving up in the file as it's read.
 */
static int _find_match_in_tcp_file(
		callerid_conn_t *conn,
		ino_t *inode,
		int af,
		const char *path,
		int (*match_func)(callerid_conn_t *,
				   ino_t *, callerid_conn_t *, ino_t, int))
{
	int rc = SLURM_FAILURE;
	FILE *fp;
	char ip_dst_str[INET6_ADDRSTRLEN+1]; /* +1 for scanf to add \0 */
	char ip_src_str[INET6_ADDRSTRLEN+1];
	char line[1024];
	int addrbytes, i, matches;
	uint64_t inode_row;
	callerid_conn_t conn_row;

	addrbytes = af == AF_INET ? 4 : 16;

	/* Zero out the IPs. Not strictly necessary but it will look much better
	 * in a debugger since IPv4 only uses 4 out of 16 bytes. */
	memset(&conn_row.ip_dst, 0, 16);
	memset(&conn_row.ip_src, 0, 16);

	fp = fopen(path, "r");
	if (!fp)
		return rc;

	while( fgets(line, 1024, fp) != NULL ) {
		matches = sscanf(line,
			"%*s %[0-9A-Z]:%x %[0-9A-Z]:%x %*s %*s %*s %*s %*s %*s %"PRIu64"",
			ip_dst_str, &conn_row.port_dst, ip_src_str,
			&conn_row.port_src, &inode_row);

		if (matches == EOF)
			break;

		/* Probably the header */
		if (!matches)
			continue;

		/* Convert to usable forms */
		inet_nsap_addr(ip_dst_str, (unsigned char*)&conn_row.ip_dst,
				addrbytes);
		inet_nsap_addr(ip_src_str, (unsigned char*)&conn_row.ip_src,
				addrbytes);

		/* Convert to network byte order. */
		for (i=0; i < (addrbytes>>2); i++) {
			conn_row.ip_dst.s6_addr32[i]
				= htonl(conn_row.ip_dst.s6_addr32[i]);
			conn_row.ip_src.s6_addr32[i]
				= htonl(conn_row.ip_src.s6_addr32[i]);
		}

		/* Check if we matched */
		rc = match_func(conn, inode, &conn_row, (ino_t)inode_row, af);
		if (rc == SLURM_SUCCESS) {
			char ip_src_str[INET6_ADDRSTRLEN];
			char ip_dst_str[INET6_ADDRSTRLEN];

			inet_ntop(af, &conn->ip_src, ip_src_str,
					INET6_ADDRSTRLEN);
			inet_ntop(af, &conn->ip_dst, ip_dst_str,
					INET6_ADDRSTRLEN);
			debug("network_callerid matched %s:%lu => %s:%lu with inode %lu",
			      ip_src_str, (long unsigned int)conn->port_src,
			      ip_dst_str, (long unsigned int)conn->port_dst,
			      (long unsigned int)inode);
			break;
		}
	}

	fclose(fp);
	return rc;
}


/* Search through /proc/$pid/fd/ symlinks for the specified inode
 *
 * All errors in this function should be silently ignored. Processes appear and
 * disappear all the time. It is natural for processes to disappear in between
 * operations such as readdir, stat, and others. We should detect errors but
 * not log them.
 */
static int _find_inode_in_fddir(pid_t pid, ino_t inode)
{
	DIR *dirp;
	struct dirent *entryp;
	char dirpath[1024];
	char fdpath[2048];
	int rc = SLURM_FAILURE;
	struct stat statbuf;

	snprintf(dirpath, 1024, "/proc/%d/fd", (pid_t)pid);
	if ((dirp = opendir(dirpath)) == NULL) {
		return SLURM_FAILURE;
	}

	while (1) {
		if (!(entryp = readdir(dirp)))
			break;
		/* Ignore . and .. */
		else if (!xstrncmp(entryp->d_name, ".", 1))
			continue;

		/* This is a symlink. Follow it to get destination's inode. */
		snprintf(fdpath, sizeof(fdpath), "%s/%s", dirpath, entryp->d_name);
		if (stat(fdpath, &statbuf) != 0)
			continue;
		if (statbuf.st_ino == inode) {
			debug3("_find_inode_in_fddir: found %lu at %s",
			       (long unsigned int)inode, fdpath);
			rc = SLURM_SUCCESS;
			break;
		}
	}

	closedir(dirp);
	return rc;
}


extern int callerid_find_inode_by_conn(callerid_conn_t conn, ino_t *inode)
{
	int rc;

	rc = _find_match_in_tcp_file(&conn, inode, AF_INET, PATH_PROCNET_TCP,
			_match_conn);
	if (rc == SLURM_SUCCESS)
		return SLURM_SUCCESS;

	rc = _find_match_in_tcp_file(&conn, inode, AF_INET6, PATH_PROCNET_TCP6,
			_match_conn);
	if (rc == SLURM_SUCCESS)
		return SLURM_SUCCESS;

	/* Add new protocols here if needed, such as UDP */

	return SLURM_FAILURE;
}


extern int callerid_find_conn_by_inode(callerid_conn_t *conn, ino_t inode)
{
	int rc;

	rc = _find_match_in_tcp_file(conn, &inode, AF_INET, PATH_PROCNET_TCP,
			_match_inode);
	if (rc == SLURM_SUCCESS)
		return SLURM_SUCCESS;

	rc = _find_match_in_tcp_file(conn, &inode, AF_INET6, PATH_PROCNET_TCP6,
			_match_inode);
	if (rc == SLURM_SUCCESS)
		return SLURM_SUCCESS;

	/* Add new protocols here if needed, such as UDP */

	return SLURM_FAILURE;
}


/* Read through /proc then read each proc's fd/ directory.
 *
 * Most errors in this function should be silently ignored. Processes appear and
 * disappear all the time. It is natural for processes to disappear in between
 * operations such as readdir, stat, and others. We should detect errors but
 * not log them.
 */
extern int find_pid_by_inode (pid_t *pid_result, ino_t inode)
{
	DIR *dirp;
	struct dirent *entryp;
	char *dirpath = "/proc";
	int rc = SLURM_FAILURE;
	pid_t pid;

	if ((dirp = opendir(dirpath)) == NULL) {
		/* Houston, we have a problem: /proc is inaccessible */
		error("find_pid_by_inode: unable to open %s: %m",
				dirpath);
		return SLURM_FAILURE;
	}

	while (1) {
		if (!(entryp = readdir(dirp)))
			break;
		/* We're only looking for /proc/[0-9]*  */
		else if (!isdigit(entryp->d_name[0]))
			continue;

		/* More sanity checks can be performed but there isn't much
		 * point. The fd/ directory will exist inside the directory and
		 * we'll find the specified inode or we won't. Failures are
		 * silent so it won't clutter logs. The above checks are
		 * currently sufficient for Linux. */

		pid = (int)atoi(entryp->d_name);
		rc = _find_inode_in_fddir(pid, inode);
		if (rc == SLURM_SUCCESS) {
			*pid_result = pid;
			break;
		}
	}

	closedir(dirp);
	return rc;
}


extern int callerid_get_own_netinfo (callerid_conn_t *conn)
{
	DIR *dirp;
	struct dirent *entryp;
	char *dirpath = "/proc/self/fd";
	char fdpath[1024];
	int rc = SLURM_FAILURE;
	struct stat statbuf;

	if ((dirp = opendir(dirpath)) == NULL) {
		error("callerid_get_own_netinfo: opendir failed for %s: %m",
				dirpath);
		return rc;
	}

	while (1) {
		if (!(entryp = readdir(dirp)))
			break;
		/* Ignore . and .. */
		else if (!xstrncmp(entryp->d_name, ".", 1))
			continue;

		snprintf(fdpath, 1024, "%s/%s", dirpath, entryp->d_name);
		debug3("callerid_get_own_netinfo: checking %s", fdpath);
		/* This is a symlink. Follow it to get destination's inode. */
		if (stat(fdpath, &statbuf) != 0) {
			debug3("stat failed for %s: %m", fdpath);
			continue;
		}

		/* We are only interested in sockets */
		if (S_ISSOCK(statbuf.st_mode)) {
			debug3("callerid_get_own_netinfo: checking socket %s",
					fdpath);
			rc = callerid_find_conn_by_inode(conn, statbuf.st_ino);
			if (rc == SLURM_SUCCESS) {
				break;
			}
		}
	}

	closedir(dirp);
	return rc;
}
