/*****************************************************************************\
 *  src/common/uid.c - uid/gid lookup utility functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
    uid_t uid;
    char *username;
} uid_cache_entry_t;

static pthread_mutex_t uid_lock = PTHREAD_MUTEX_INITIALIZER;
static uid_cache_entry_t *uid_cache = NULL;
static int uid_cache_used = 0;

extern void slurm_getpwuid_r(uid_t uid, struct passwd *pwd, char **curr_buf,
			     char **buf_malloc, size_t *bufsize,
			     struct passwd **result)
{
	DEF_TIMERS;

	START_TIMER;
	while (true) {
		int rc = getpwuid_r(uid, pwd, *curr_buf, *bufsize, result);
		if (!rc && *result)
			break;
		if (rc == EINTR) {
			continue;
		} else if (rc == ERANGE) {
			*bufsize *= 2;
			*curr_buf = xrealloc(*buf_malloc, *bufsize);
			continue;
		} else if ((rc == 0) || (rc == ENOENT) || (rc == ESRCH) ||
			   (rc == EBADF) || (rc == EPERM)) {
			debug2("%s: getpwuid_r(%u): no record found",
			       __func__, uid);
		} else {
			error("%s: getpwuid_r(%u): %s",
			      __func__, uid, slurm_strerror(rc));
		}
		*result = NULL;
		break;
	}
	END_TIMER2("getpwuid_r");
}

int uid_from_string(const char *name, uid_t *uidp)
{
	DEF_TIMERS;
	struct passwd pwd, *result = NULL;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	char *p = NULL;
	long l;

	if (!name)
		return -1;

	/*
	 *  Check to see if name is a valid username first.
	 */
	START_TIMER;
	while (true) {
		int rc = getpwnam_r(name, &pwd, curr_buf, bufsize, &result);
		if (!rc && result)
			break;
		if (rc == EINTR) {
			continue;
		} else if (rc == ERANGE) {
			bufsize *= 2;
			curr_buf = xrealloc(buf_malloc, bufsize);
			continue;
		} else if ((rc == 0) || (rc == ENOENT) || (rc == ESRCH) ||
			   (rc == EBADF) || (rc == EPERM)) {
			debug2("%s: getpwnam_r(%s): no record found",
			       __func__, name);
		} else {
			error("%s: getpwnam_r(%s): %s",
			      __func__, name, slurm_strerror(rc));
		}
		result = NULL;
		break;
	}
	END_TIMER2("getpwnam_r");

	if (result) {
		*uidp = result->pw_uid;
		xfree(buf_malloc);
		return 0;
	}

	/*
	 *  If username was not valid, check for a valid UID.
	 */
	errno = 0;
	l = strtol(name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX))) ||
	    (name == p) || (*p != '\0') || (l < 0) || (l > UINT32_MAX)) {
		xfree(buf_malloc);
		return -1;
	}

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	slurm_getpwuid_r(l, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (!result) {
		xfree(buf_malloc);
		return -1;
	}

	*uidp = (uid_t) l;
	xfree(buf_malloc);
	return 0;
}

/*
 * Return an xmalloc'd string, or null on error.
 * Caller must free eventually.
 */
char *uid_to_string_or_null(uid_t uid)
{
	struct passwd pwd, *result;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	char *ustring = NULL;

	/* Suse Linux does not handle multiple users with UID=0 well */
	if (uid == 0)
		return xstrdup("root");

	slurm_getpwuid_r(uid, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (result)
		ustring = xstrdup(result->pw_name);

	xfree(buf_malloc);

	return ustring;
}

extern char *uid_to_string(uid_t uid)
{
	char *result = uid_to_string_or_null(uid);

	if (!result)
		result = xstrdup_printf("%u", uid);

	return result;
}

extern void uid_cache_clear(void)
{
	int i;

	slurm_mutex_lock(&uid_lock);
	for (i = 0; i < uid_cache_used; i++)
		xfree(uid_cache[i].username);
	xfree(uid_cache);
	uid_cache_used = 0;
	slurm_mutex_unlock(&uid_lock);
}

extern char *uid_to_string_cached(uid_t uid)
{
	uid_cache_entry_t *entry;
	uid_cache_entry_t target = {uid, NULL};

	slurm_mutex_lock(&uid_lock);
	/* 
	 * bsearch and qsort depend on the first field of uid_cache_entry
	 * being a 16 bit integer uid
	 */
	entry = bsearch(&target, uid_cache, uid_cache_used,
			sizeof(uid_cache_entry_t), slurm_sort_uint16_list_asc);
	if (entry == NULL) {
		uid_cache_entry_t new_entry = {uid, uid_to_string(uid)};
		uid_cache_used++;
		uid_cache = xrealloc(uid_cache,
				     sizeof(uid_cache_entry_t)*uid_cache_used);
		uid_cache[uid_cache_used-1] = new_entry;
		qsort(uid_cache, uid_cache_used, sizeof(uid_cache_entry_t),
		      slurm_sort_uint16_list_asc);
		slurm_mutex_unlock(&uid_lock);
		return new_entry.username;
	}
	slurm_mutex_unlock(&uid_lock);
	return entry->username;
}

extern char *uid_to_dir(uid_t uid)
{
	struct passwd pwd, *result;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	char *dir = NULL;

	slurm_getpwuid_r(uid, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (result)
		dir = xstrdup(result->pw_dir);

	xfree(buf_malloc);

	return dir;
}

extern char *uid_to_shell(uid_t uid)
{
	struct passwd pwd, *result;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	char *shell = NULL;

	slurm_getpwuid_r(uid, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (result)
		shell = xstrdup(result->pw_shell);

	xfree(buf_malloc);

	return shell;
}

gid_t gid_from_uid(uid_t uid)
{
	struct passwd pwd, *result;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	gid_t gid;

	slurm_getpwuid_r(uid, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (result)
		gid = result->pw_gid;
	else
		gid = (gid_t) -1;

	xfree(buf_malloc);

	return gid;
}

int gid_from_string(const char *name, gid_t *gidp)
{
	DEF_TIMERS;
	struct group grp, *result = NULL;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	char *curr_buf = buf_stack;
	size_t bufsize = PW_BUF_SIZE;
	char *p = NULL;
	long l;

	if (!name)
		return -1;

	/*
	 *  Check for valid group name first.
	 */
	START_TIMER;
	while (true) {
		int rc = getgrnam_r(name, &grp, curr_buf, bufsize, &result);
		if (!rc && result)
			break;
		if (rc == EINTR) {
			continue;
		} else if (rc == ERANGE) {
			bufsize *= 2;
			curr_buf = xrealloc(buf_malloc, bufsize);
			continue;
		} else if ((rc == 0) || (rc == ENOENT) || (rc == ESRCH) ||
			   (rc == EBADF) || (rc == EPERM)) {
			debug2("%s: getgrnam_r(%s): no record found",
			       __func__, name);
		} else {
			error("%s: getgrnam_r(%s): %s",
			      __func__, name, slurm_strerror(rc));
		}
		result = NULL;
		break;
	}
	END_TIMER2("getgrnam_r");

	if (result) {
		*gidp = result->gr_gid;
		xfree(buf_malloc);
		return 0;
	}

	/*
	 *  If group name was not valid, perhaps it is a  valid GID.
	 */
	errno = 0;
	l = strtol(name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX))) ||
	    (name == p) || (*p != '\0') || (l < 0) || (l > INT_MAX)) {
		xfree(buf_malloc);
		return -1;
	}

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	START_TIMER;
	while (true) {
		int rc = getgrgid_r(l, &grp, curr_buf, bufsize, &result);
		if (!rc && result)
			break;
		if (rc == EINTR) {
			continue;
		} else if (rc == ERANGE) {
			bufsize *= 2;
			curr_buf = xrealloc(buf_malloc, bufsize);
			continue;
		} else if ((rc == 0) || (rc == ENOENT) || (rc == ESRCH) ||
			   (rc == EBADF) || (rc == EPERM)) {
			debug2("%s: getgrgid_r(%ld): no record found",
			       __func__, l);
		} else {
			error("%s: getgrgid_r(%ld): %s",
			      __func__, l, slurm_strerror(rc));
		}
		result = NULL;
		break;
	}
	END_TIMER2("getgrgid_r");

	xfree(buf_malloc);
	/*
	 * Warning - result is now a pointer to invalid memory.
	 * Do not dereference it, but checking that it is non-NULL is safe.
	 */
	if (!result)
		return -1;

	*gidp = (gid_t) l;
	return 0;
}

extern char *gid_to_string(gid_t gid)
{
	char *result = gid_to_string_or_null(gid);

	if (!result)
		return xstrdup_printf("%u", gid);

	return result;
}

/*
 * Return an xmalloc'd string, or null on error.
 * Caller must xfree() eventually.
 */
char *gid_to_string_or_null(gid_t gid)
{
	DEF_TIMERS;
	struct group grp, *result = NULL;
	char buf_stack[PW_BUF_SIZE];
	char *buf_malloc = NULL;
	size_t bufsize = PW_BUF_SIZE;
	char *curr_buf = buf_stack;
	char *name = NULL;

	START_TIMER;
	while (true) {
		int rc = getgrgid_r(gid, &grp, curr_buf, bufsize, &result);
		if (!rc && result)
			break;
		if (rc == EINTR) {
			continue;
		} else if (rc == ERANGE) {
			bufsize *= 2;
			curr_buf = xrealloc(buf_malloc, bufsize);
			continue;
		} else if ((rc == 0) || (rc == ENOENT) || (rc == ESRCH) ||
			   (rc == EBADF) || (rc == EPERM)) {
			debug2("%s: getgrgid_r(%d): no record found",
			       __func__, gid);
		} else {
			error("%s: getgrgid_r(%d): %s",
			      __func__, gid, slurm_strerror(rc));
		}
		result = NULL;
		break;
	}
	END_TIMER2("getgrgid_r");

	if (result)
		name = xstrdup(result->gr_name);

	xfree(buf_malloc);

	return name;
}
