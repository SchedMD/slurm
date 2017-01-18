/*****************************************************************************\
 *  src/common/uid.c - uid/gid lookup utility functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif

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

char *
uid_to_string (uid_t uid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE], *ustring;
	int rc;

	/* Suse Linux does not handle multiple users with UID=0 well */
	if (uid == 0)
		return xstrdup("root");

	rc = slurm_getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (result && (rc == 0))
		ustring = xstrdup(result->pw_name);
	else
		ustring = xstrdup("nobody");
	return ustring;
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

/* slurm_valid_uid_gid()
 *
 * IN - uid - User id to verify
 * IN/OUT - gid - Group id to verify or set
 * IN/OUT - user_name - User name for the uid, this will be set if not
 *                      already set.
 * IN - name_already_verfied - If set we will only validate the
 *                             *user_name exists and return 1, else we
 *                             will validate uid and fill in *user_name.
 * IN - validate_gid - If set we will validate the gid as well as the
 *                     uid.  Set gid with correct gid if root launched job.
 * RET - returns 0 if invalid uid/gid, otherwise returns 1
 */
extern int slurm_valid_uid_gid(uid_t uid, gid_t *gid, char **user_name,
			       bool name_already_verified,
			       bool validate_gid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	int rc;
	struct group *grp;
	int i;

	/* already verified */
	if (name_already_verified && *user_name)
		return 1;

	rc = slurm_getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);

	if (!result || rc) {
		error("uid %ld not found on system", (long)uid);
		slurm_seterrno(ESLURMD_UID_NOT_FOUND);
		return 0;
	}

	if (!*user_name)
		*user_name = xstrdup(result->pw_name);

	if (!validate_gid)
		return 1;

	if (result->pw_gid == *gid)
		return 1;

	if (!(grp = getgrgid(*gid))) {
		error("gid %ld not found on system", (long)(*gid));
		slurm_seterrno(ESLURMD_GID_NOT_FOUND);
		return 0;
	}

	/* Allow user root to use any valid gid */
	if (result->pw_uid == 0) {
		result->pw_gid = *gid;
		return 1;
	}

	for (i = 0; grp->gr_mem[i]; i++) {
		if (!xstrcmp(result->pw_name, grp->gr_mem[i])) {
			result->pw_gid = *gid;
			return 1;
		}
	}
	if (*gid != 0) {
		if (slurm_find_group_user(result, *gid))
			return 1;
	}

	/* root user may have launched this job for this user, but
	 * root did not explicitly set the gid. This would set the
	 * gid to 0. In this case we should set the appropriate
	 * default gid for the user (from the passwd struct).
	 */
	if (*gid == 0) {
		*gid = result->pw_gid;
		return 1;
	}
	error("uid %ld is not a member of gid %ld",
		(long)result->pw_uid, (long)(*gid));
	slurm_seterrno(ESLURMD_GID_NOT_FOUND);

	return 0;
}
