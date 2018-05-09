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

static int _getpwnam_r (const char *name, struct passwd *pwd, char *buf,
		size_t bufsiz, struct passwd **result)
{
	int rc;
	while (1) {
		rc = getpwnam_r(name, pwd, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return (rc);
}

extern int slurm_getpwuid_r (uid_t uid, struct passwd *pwd, char *buf,
			     size_t bufsiz, struct passwd **result)
{
	int rc;
	while (1) {
		rc = getpwuid_r(uid, pwd, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return rc;
}

int
uid_from_string (char *name, uid_t *uidp)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	long l;

	if (!name)
		return -1;

	/*
	 *  Check to see if name is a valid username first.
	 */
	if ((_getpwnam_r (name, &pwd, buffer, PW_BUF_SIZE, &result) == 0)
	    && result != NULL) {
		*uidp = result->pw_uid;
		return 0;
	}

	/*
	 *  If username was not valid, check for a valid UID.
	 */
	errno = 0;
	l = strtol (name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX)))
	   || (name == p)
	   || (*p != '\0')
	   || (l < 0)
	   || (l > INT_MAX))
		return -1;

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	if (slurm_getpwuid_r(l, &pwd, buffer, PW_BUF_SIZE, &result) != 0)
		return -1;

	*uidp = (uid_t) l;
	return 0;
}

/*
 * Return an xmalloc'd string, or null on error.
 * Caller must free eventually.
 */
char *uid_to_string_or_null(uid_t uid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	char *ustring = NULL;
	int rc;

	/* Suse Linux does not handle multiple users with UID=0 well */
	if (uid == 0)
		return xstrdup("root");

	rc = slurm_getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (result && (rc == 0))
		ustring = xstrdup(result->pw_name);

	return ustring;
}

/*
 * Convert a uid to an xmalloc'd string.
 * Always returns a string - "nobody" is sent back on error.
 */
char *uid_to_string(uid_t uid)
{
	char *result = uid_to_string_or_null(uid);

	if (!result)
		result = xstrdup("nobody");

	return result;
}

static int _uid_compare(const void *a, const void *b)
{
	uid_t ua = *(const uid_t *)a;
	uid_t ub = *(const uid_t *)b;
	return ua - ub;
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
	entry = bsearch(&target, uid_cache, uid_cache_used,
			sizeof(uid_cache_entry_t), _uid_compare);
	if (entry == NULL) {
		uid_cache_entry_t new_entry = {uid, uid_to_string(uid)};
		uid_cache_used++;
		uid_cache = xrealloc(uid_cache,
				     sizeof(uid_cache_entry_t)*uid_cache_used);
		uid_cache[uid_cache_used-1] = new_entry;
		qsort(uid_cache, uid_cache_used, sizeof(uid_cache_entry_t),
		      _uid_compare);
		slurm_mutex_unlock(&uid_lock);
		return new_entry.username;
	}
	slurm_mutex_unlock(&uid_lock);
	return entry->username;
}

gid_t
gid_from_uid (uid_t uid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	gid_t gid;
	int rc;

	rc = slurm_getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (result && (rc == 0))
		gid = result->pw_gid;
	else
		gid = (gid_t) -1;

	return gid;
}

static int _getgrnam_r (const char *name, struct group *grp, char *buf,
		size_t bufsiz, struct group **result)
{
	int rc;
	while (1) {
		rc = getgrnam_r (name, grp, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return (rc);
}

static int _getgrgid_r (gid_t gid, struct group *grp, char *buf,
		size_t bufsiz, struct group **result)
{
	int rc;
	while (1) {
		rc = getgrgid_r (gid, grp, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return rc;
}

int
gid_from_string (char *name, gid_t *gidp)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	long l;

	if (!name)
		return -1;

	/*
	 *  Check for valid group name first.
	 */
	if ((_getgrnam_r (name, &grp, buffer, PW_BUF_SIZE, &result) == 0)
	    && result != NULL) {
		*gidp = result->gr_gid;
		return 0;
	}

	/*
	 *  If group name was not valid, perhaps it is a  valid GID.
	 */
	errno = 0;
	l = strtol (name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX)))
	   || (name == p)
	   || (*p != '\0')
	   || (l < 0)
	   || (l > INT_MAX))
		return -1;

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	if ((_getgrgid_r (l, &grp, buffer, PW_BUF_SIZE, &result) != 0)
	    || result == NULL)
		return -1;

	*gidp = (gid_t) l;
	return 0;
}

char *
gid_to_string (gid_t gid)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *gstring;
	int rc;

	rc = _getgrgid_r(gid, &grp, buffer, PW_BUF_SIZE, &result);
	if (rc == 0 && result)
		gstring = xstrdup(result->gr_name);
	else
		gstring = xstrdup("nobody");
	return gstring;
}

int
slurm_find_group_user(struct passwd *pwd, gid_t gid)
{
	struct group grp;
	struct group *grpp;
	char buf[PW_BUF_SIZE];
	int cc;

	setgrent();
	while (1) {
		cc = getgrent_r(&grp, buf, PW_BUF_SIZE, &grpp);
		if (cc)
			break;
		if (grpp->gr_gid != gid)
			continue;
		for (cc = 0; grpp->gr_mem[cc] ; cc++) {
			if (xstrcmp(pwd->pw_name, grpp->gr_mem[cc]) == 0) {
				endgrent();
				return 1;
			}
		}
	}
	endgrent();

	return 0;
}
