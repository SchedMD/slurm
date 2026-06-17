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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef enum {
	UID_CACHE_SORT_UID = 0,
	UID_CACHE_SORT_USERNAME = 1,
} uid_cache_sort_t;

#define UID_CACHE_MAX_UNLIMITED 0
#define UID_CACHE_MAX_UPPER 65536

typedef struct {
    uid_t uid;
    char *username;
} uid_cache_entry_t;

typedef struct {
	uid_cache_entry_t *cache;
	int capacity;
	int max_entries;
	int sort;
	int used;
} uid_cache_t;

static pthread_mutex_t uid_lock = PTHREAD_MUTEX_INITIALIZER;

static uid_cache_t uid_cache = {
	.max_entries = UID_CACHE_MAX_UNLIMITED,
	.sort = UID_CACHE_SORT_UID,
};
static uid_cache_t uidfrom_uid_cache = {
	.max_entries = UID_CACHE_MAX_UNLIMITED,
	.sort = UID_CACHE_SORT_UID,
};
static uid_cache_t uidfrom_uname_cache = {
	.max_entries = UID_CACHE_MAX_UNLIMITED,
	.sort = UID_CACHE_SORT_USERNAME,
};
static uid_cache_t uidfrom_uname_neg_cache = {
	.max_entries = UID_CACHE_MAX_UPPER,
	.sort = UID_CACHE_SORT_USERNAME,
};
static int uidfrom_cache_enabled = 0;

extern void slurm_getpwuid_r(uid_t uid, struct passwd *pwd, char **curr_buf,
			     char **buf_malloc, size_t *bufsize,
			     struct passwd **result)
{
	DEF_TIMERS;

	if (uid == SLURM_AUTH_NOBODY) {
		*result = NULL;
		return;
	}

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

static int _uid_cache_cmp_uid(const void *tkey, const void *tmember)
{
	const uid_t key = *(const uid_t *) tkey;
	const uid_cache_entry_t *member = (const uid_cache_entry_t *) tmember;
	if (key < member->uid)
		return -1;
	else if (key == member->uid)
		return 0;
	else
		return 1;
}

static int _uid_cache_cmp_username(const void *tkey, const void *tmember)
{
	const char *key = *(const char **) tkey;
	const uid_cache_entry_t *member = (const uid_cache_entry_t *) tmember;
	return xstrcmp(key, member->username);
}

static int _uid_cache_cmp(uid_cache_t *cache, int i, uid_cache_entry_t *item)
{
	if (cache->sort == UID_CACHE_SORT_UID)
		return _uid_cache_cmp_uid(&item->uid, &cache->cache[i]);
	/* must be UID_CACHE_SORT_USERNAME */
	return _uid_cache_cmp_username(&item->username, &cache->cache[i]);
}

/*
 * _uid_cache_insert - add a new entry into the uid cache
 *	will automatically expand the cache as needed
 * IN pointer to entry, this function copies the values as-is, assumes
 *    ownership of the username pointer
 *
 * NOTE: caller MUST have the mutex uid_lock prior to the call
 */
static void _uid_cache_insert(uid_cache_t *cache, uid_cache_entry_t *item)
{
	int low = 0, high = cache->used;

	if ((cache->max_entries != UID_CACHE_MAX_UNLIMITED) &&
	    (cache->used >= cache->max_entries)) {
		xfree(item->username);
		return;
	}

	/* binary insertion sort to manage cache */
	while (low < high) {
		int mid = (low + high) / 2;
		int cmp = _uid_cache_cmp(cache, mid, item);
		if (!cmp) {
			/* had a cache hit, do not insert, clean up */
			xfree(item->username);
			return;
		} else if (cmp > 0) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	if (cache->used >= cache->capacity) {
		cache->capacity += 128;
		cache->cache = xrecalloc(cache->cache, cache->capacity,
					 sizeof(uid_cache_entry_t));
	}
	if (low < cache->used)
		memmove(&cache->cache[low + 1], &cache->cache[low],
			(cache->used - low) * sizeof(uid_cache_entry_t));
	cache->cache[low] = *item;
	cache->used++;
}

static void _uid_cache_insert_by_username(uid_cache_t *cache, const char *name)
{
	uid_cache_entry_t entry = { 0 };
	entry.uid = NO_VAL;
	entry.username = xstrdup(name);
	_uid_cache_insert(cache, &entry);
}

static uid_cache_entry_t *_uid_cache_search_uid(const uid_cache_t *cache,
						uid_t tgt_uid)
{
	if (cache->sort == UID_CACHE_SORT_UID) {
		return bsearch(&tgt_uid, cache->cache, cache->used,
			       sizeof(uid_cache_entry_t), _uid_cache_cmp_uid);
	} else {
		/* linear scan fallback in case cache is searched by uid */
		for (int i = 0; i < cache->used; i++) {
			if (cache->cache[i].uid == tgt_uid) {
				return &cache->cache[i];
			}
		}
	}
	return NULL;
}

static uid_cache_entry_t *_uid_cache_search_username(const uid_cache_t *cache,
						     const char *tgt_username)
{
	if (cache->sort == UID_CACHE_SORT_USERNAME) {
		return bsearch(&tgt_username, cache->cache, cache->used,
			       sizeof(uid_cache_entry_t),
			       _uid_cache_cmp_username);
	} else {
		/* linear scan fallback in case cache is searched by username */
		for (int i = 0; i < cache->used; i++) {
			if (!xstrcmp(cache->cache[i].username, tgt_username)) {
				return &cache->cache[i];
			}
		}
	}
	return NULL;
}

/*
 * _uid_from_string_cached() - check username_cache for any entry matching
 * 			       the input string.
 *
 * IN name - string with either a provided username or provided string uid
 * IN uidp - pointer to the caller's uid_t memory. Upon success, will be
 * 	     populated with the identified uid.
 *
 * Returns:
 *     SLURM_SUCCESS: uid was successfully looked up in the cache, uidp
 *     		      populated with the cached version of the uid
 *     SLURM_ERROR: the name was not in the username cache
 *
 *
 * NOTE: Caller is assumed to hold lock on uid_lock to call this
 */
static int _uid_from_string_cached(const char *name, uid_t *uidp)
{
	uid_cache_entry_t *entry = NULL;
	long l;
	char *p = NULL;
	uid_t target;

	if (!name)
		return SLURM_ERROR;

	entry = _uid_cache_search_username(&uidfrom_uname_cache, name);
	if (entry) {
		*uidp = entry->uid;
		return SLURM_SUCCESS;
	}

	/* maybe we were given a string of a uid? */
	errno = 0;
	l = strtol(name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX))) ||
	    (name == p) || (*p != '\0') || (l < 0) || (l > UINT32_MAX))
		return SLURM_ERROR;

	target = (uid_t) l;

	entry = _uid_cache_search_uid(&uidfrom_uid_cache, target);

	/*
	 * Only trusting a string of a uid if it is already cached because then
	 * the insertion point will have been a valid lookup of the uid
	 * previously.
	 */
	if (entry) {
		*uidp = entry->uid;
		return SLURM_SUCCESS;
	}
	return SLURM_ERROR;
}

/*
 * _uid_from_string() - given a string with either a username or a uid in it
 *      populate the passed uid pointer with the correct uid and return an
 *      integer indicated the degree of success.  This function is sanitizing
 *      inputs against current system state and provides UIDs that the Slurm
 *      daemons use in a security sensitive manner.
 *
 * IN name: string with either a provided username or provided string uid
 *          ownership of the username pointer
 * IN/OUT uidp: pointer to the caller's uid_t memory, will be populated with
 *          the identified uid, though degree of success is communicated via
 *          the return code.
 * IN/OUT unamep - pointer to caller's username string, if non-NULL, will be
 * 	    populated with the identified username only when SLURM_SUCCESS is
 * 	    returned. If non-NULL, will be populated with NULL when
 * 	    ESLURM_USER_ID_UNKNOWN is returned. Unchanged for any other return
 * 	    values. Caller takes on ownership of any non-NULL value set in
 * 	    unamep.
 *
 * Returns:
 *     SLURM_SUCCESS: uid was successfully looked up, uidp populated with
 *                    the passwd database version of the uid
 *     SLURM_ERROR: the name string was not a username nor was it a valid
 *                    number that *might* be a uid. uidp is NOT populated,
 *                    caller should NOT proceed with the results
 *     ESLURM_USER_ID_UNKNOWN: the name string had an encoded number but was
 *                    not in the user database accessible to the system.
 *                    uidp is populated with the parsed uid number. caller
 *                    should proceed with care.
 *     SLURM_AUTH_NOBODY: requested name matches SLURM_AUTH_NOBODY_NAME,
 *     		      the in/out variables are untouched.
 */
static int _uid_from_string(const char *name, uid_t *uidp, char **unamep)
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
		return SLURM_ERROR;

	if (!xstrcmp(name, SLURM_AUTH_NOBODY_NAME))
		return SLURM_AUTH_NOBODY;

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

	if (result)
		goto success;

	/*
	 *  If username was not valid, check for a valid UID.
	 */
	errno = 0;
	l = strtol(name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX))) ||
	    (name == p) || (*p != '\0') || (l < 0) || (l > UINT32_MAX)) {
		xfree(buf_malloc);
		return SLURM_ERROR;
	}

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	slurm_getpwuid_r(l, &pwd, &curr_buf, &buf_malloc, &bufsize, &result);
	if (!result) {
		/*
		 * some calling clients still use the value of *uidp even with
		 * ESLURM_USER_ID_UNKNOWN
		 */
		*uidp = (uid_t) l;
		if (unamep)
			*unamep = NULL;
		xfree(buf_malloc);
		return ESLURM_USER_ID_UNKNOWN;
	}

success:
	*uidp = pwd.pw_uid;
	if (unamep)
		*unamep = xstrdup(pwd.pw_name);

	xfree(buf_malloc);
	return SLURM_SUCCESS;
}

extern int uid_from_string(const char *name, uid_t *uidp)
{
	return _uid_from_string(name, uidp, NULL);
}

extern int uid_from_string_cached(const char *name, uid_t *uidp)
{
	int rc = SLURM_ERROR;
	char *resolved_username = NULL;

	if (!name)
		return SLURM_ERROR;

	/* Check to see if name is in username cache first */
	slurm_mutex_lock(&uid_lock);
	if (uidfrom_cache_enabled) {
		if (_uid_from_string_cached(name, uidp) == SLURM_SUCCESS) {
			slurm_mutex_unlock(&uid_lock);
			return SLURM_SUCCESS;
		}
		if (_uid_cache_search_username(&uidfrom_uname_neg_cache,
					       name)) {
			slurm_mutex_unlock(&uid_lock);
			return SLURM_ERROR;
		}
	}
	slurm_mutex_unlock(&uid_lock);

	rc = _uid_from_string(name, uidp, &resolved_username);
	slurm_mutex_lock(&uid_lock);
	if (uidfrom_cache_enabled) {
		if (rc == SLURM_SUCCESS) {
			uid_cache_entry_t entry = { 0 };
			entry.uid = *uidp;
			/* duplicating username for first cache reference */
			entry.username = xstrdup(resolved_username);
			_uid_cache_insert(&uidfrom_uname_cache, &entry);

			/* handing off username for second cache reference */
			entry.username = resolved_username;
			_uid_cache_insert(&uidfrom_uid_cache, &entry);
			resolved_username = NULL;
		} else if (rc == SLURM_ERROR) {
			_uid_cache_insert_by_username(&uidfrom_uname_neg_cache,
						      name);
		} else if (rc == ESLURM_USER_ID_UNKNOWN) {
			/* do nothing */
		} else if (rc == SLURM_AUTH_NOBODY) {
			/* do nothing */
		}
	}
	slurm_mutex_unlock(&uid_lock);

	xfree(resolved_username);
	return rc;
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

	if (uid == SLURM_AUTH_NOBODY)
		return xstrdup(SLURM_AUTH_NOBODY_NAME);

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

static void _uid_cache_clear(uid_cache_t *cache)
{
	for (int i = 0; i < cache->used; i++)
		xfree(cache->cache[i].username);
	xfree(cache->cache);
	cache->used = 0;
	cache->capacity = 0;
}

extern void uid_cache_clear(void)
{
	slurm_mutex_lock(&uid_lock);
	_uid_cache_clear(&uid_cache);
	_uid_cache_clear(&uidfrom_uid_cache);
	_uid_cache_clear(&uidfrom_uname_cache);
	_uid_cache_clear(&uidfrom_uname_neg_cache);
	slurm_mutex_unlock(&uid_lock);
}

/*
 * Turns on uid_from_string caching. Will clear any entries existing in the
 * cache. Reference counts so as not to re-init the caches until truly
 * re-enabling from a disabled state.
 */
extern void uid_from_string_cache_enable(void)
{
	slurm_mutex_lock(&uid_lock);
	if (!uidfrom_cache_enabled) {
		_uid_cache_clear(&uidfrom_uid_cache);
		_uid_cache_clear(&uidfrom_uname_cache);
		_uid_cache_clear(&uidfrom_uname_neg_cache);
	}
	uidfrom_cache_enabled += 1;
	slurm_mutex_unlock(&uid_lock);
}

/*
 * Turns off uid_from_string caching. xassert exists to make sure we do not
 * underflow the reference count if there is an imbalance in enable/disable
 * calls
 */
extern void uid_from_string_cache_disable(void)
{
	slurm_mutex_lock(&uid_lock);
	uidfrom_cache_enabled -= 1;
	xassert(uidfrom_cache_enabled >= 0);
	slurm_mutex_unlock(&uid_lock);
}

extern char *uid_to_string_cached(uid_t uid)
{
	uid_cache_entry_t *entry;

	if (uid == SLURM_AUTH_NOBODY)
		return SLURM_AUTH_NOBODY_NAME;

	slurm_mutex_lock(&uid_lock);
	entry = _uid_cache_search_uid(&uid_cache, uid);
	if (entry == NULL) {
		uid_cache_entry_t new_entry = {uid, uid_to_string(uid)};

		_uid_cache_insert(&uid_cache, &new_entry);
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
	char *result = NULL;

	if (gid == SLURM_AUTH_NOBODY)
		return xstrdup(SLURM_AUTH_NOBODY_NAME);

	if (!(result = gid_to_string_or_null(gid)))
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

	if (gid == SLURM_AUTH_NOBODY)
		return NULL;

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

extern int drop_supplementary_groups(uid_t uid, gid_t gid)
{
	int rc = EINVAL;
	gid_t *gids = NULL;
	bool need_drop = false;
	int gid_count = getgroups(0, NULL);

	if (gid_count < 0) {
		rc = errno;
		error("%s: getgroups(0, NULL) failed: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	if (!gid_count)
		return SLURM_SUCCESS;

	gids = xcalloc(gid_count, sizeof(*gids));

	if ((gid_count = getgroups(gid_count, gids)) < 0) {
		rc = errno;
		error("%s: getgroups() failed: %s",
		      __func__, slurm_strerror(rc));
		xfree(gids);
		return rc;
	}

	for (int i = 0; i < gid_count; i++) {
		/*
		 * Ignore same gid being in supplementary groups
		 * as it won't change permissions
		 */
		if (gids[i] == gid)
			continue;

		need_drop = true;
		debug("%s: Supplementary group %d needs to be dropped",
		      __func__, gids[i]);
	}

	xfree(gids);

	if (!need_drop)
		return SLURM_SUCCESS;

	debug("%s: Dropping all supplementary groups", __func__);

	if (!setgroups(0, NULL))
		return SLURM_SUCCESS;

	rc = errno;

#ifdef __linux__
	if (rc == EPERM) {
		warning("Process lacks CAP_SETGID to drop supplementary groups. Supplementary groups should be removed from user (uid=%d,gid=%d) prior to starting process.",
			uid, gid);
		return SLURM_SUCCESS;
	}
#endif /* __linux__ */

	warning("Unable to drop supplementary groups: %s", slurm_strerror(rc));
	return rc;
}
