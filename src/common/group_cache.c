/*****************************************************************************\
 *  group_cache.c - locally cache results from getgrouplist()
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
 *  Based on code originally contributed by Takao Hatazaki (HP).
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

/*
 * Theory of operation:
 * - Cache the extended groups for a (uid/username, gid).
 * - Cache expiration - the daemon needs to call group_cache_expire
 *   periodically to accomplish this, otherwise the cache will continue to
 *   grow.
 * - This always succeeds. The only error getgrouplist() is allowed to throw
 *   is -1 for not enough space, and we will xrealloc to handle this.
 *   In practice, if the name service cannot resolve a given user ID you will
 *   get an array back with a single element equal to the gid passed in.
 */

#include <grp.h>

#include "src/common/group_cache.h"
#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* how many groups to use by default to avoid repeated calls to getgrouplist */
#define NGROUPS_START 64

typedef struct gids_cache {
	uid_t uid;
	gid_t gid;
	char *username;
	int ngids;
	gid_t *gids;
	time_t expiration;
} gids_cache_t;

typedef struct gids_cache_needle {
	uid_t uid;		/* required */
	gid_t gid;		/* fallback if lookup fails */
	char *username;		/* optional, will be looked up if needed */
} gids_cache_needle_t;

static pthread_mutex_t gids_mutex = PTHREAD_MUTEX_INITIALIZER;
static List gids_cache_list = NULL;

static void _group_cache_list_delete(void *x)
{
	gids_cache_t *entry = (gids_cache_t *) x;
	xfree(entry->gids);
	xfree(entry->username);
	xfree(entry);
}

/* call on daemon shutdown to cleanup properly */
void group_cache_purge(void)
{
	slurm_mutex_lock(&gids_mutex);
	if (gids_cache_list)
		list_destroy(gids_cache_list);
	gids_cache_list = NULL;
	slurm_mutex_unlock(&gids_mutex);
}

static int _find_entry(void *x, void *key)
{
	gids_cache_t *entry = (gids_cache_t *) x;
	gids_cache_needle_t *needle = (gids_cache_needle_t *) key;

	return (needle->uid == entry->uid);
}

/*
 * This populates a new entry (or re-populates an old entry) using getpwuid_r()
 * to determine the primary group. getpwuid_r() should be used here instead of
 * the job's group to handle when the job was submited with a secondary group.
 *
 * On failure of getpwuid_r(), we will fallback to the job's group since it is
 * the only "safe" group we can determine.
 */
static void _init_or_reinit_entry(gids_cache_t **in,
				  gids_cache_needle_t *needle)
{
	char buffer[PW_BUF_SIZE];
	gids_cache_t *entry;
	struct passwd pwd, *result;
	int rc;

	xassert(needle);

	rc = slurm_getpwuid_r(needle->uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (!result || !result->pw_name) {
		error("slurm_getpwuid_r() failed: %s", strerror(rc));

		if (*in) {
			/* discard this now-invalid cache entry */
			list_delete_ptr(gids_cache_list, *in);
			*in = NULL;
		}

		return;
	}

	if (*in) {
		/*
		 * Reusing the existing cache record. Reset ngids to the
		 * current buffer size to avoid needing to loop around
		 * on getgrouplist() to determine the correct size.
		 */
		entry = *in;
		entry->ngids = xsize(entry->gids) / sizeof(gid_t);

		if (xstrcmp(entry->username, result->pw_name)) {
			error("Cached username %s did not match queried username %s?",
			      entry->username, result->pw_name);
			xfree(entry->username);
		}

		if (entry->gid != result->pw_gid)
			debug("Cached user=%s changed primary gid from %u to %u?",
			      result->pw_name, entry->gid, result->pw_gid);
	} else {
		/* Brand new entry */
		entry = xmalloc(sizeof(*entry));
		entry->uid = needle->uid;
		entry->ngids = NGROUPS_START;
		entry->gids = xcalloc(NGROUPS_START, sizeof(gid_t));
	}

	/*
	 * Always use the primary gid as reported by getpwuid_r(). This may
	 * not match the credential gid in cases where the user has switched
	 * their primary to launch a job, but this will ensure the primary gid
	 * is always listed as part of the extended gids list, even on systems
	 * where the extended group membership does not explicitly include the
	 * primary gid.
	 */
	entry->gid = result->pw_gid;

	/* Reuse cached value if possible to skip an xstrdup() */
	if (!entry->username)
		entry->username = xstrdup(result->pw_name);

	entry->expiration = time(NULL) + slurm_conf.group_time;

	if (!*in) {
		*in = entry;
		list_prepend(gids_cache_list, entry);
	}
}

/*
 * OUT: ngids as return value
 * IN: populated needle structure
 * IN: primary group id (will always exist first in gids list)
 * IN/OUT: gids - xmalloc'd gid_t * structure with ngids elements
 */
static int _group_cache_lookup_internal(gids_cache_needle_t *needle, gid_t **gids)
{
	gids_cache_t *entry;
	int ngids; /* need a copy to safely return outside the lock */
	DEF_TIMERS;
	START_TIMER;

	slurm_mutex_lock(&gids_mutex);
	if (!gids_cache_list)
		gids_cache_list = list_create(_group_cache_list_delete);

	entry = list_find_first(gids_cache_list, _find_entry, needle);

	if (entry && (entry->expiration > time(NULL))) {
		debug2("%s: found valid entry for user=%s",
		       __func__, entry->username);
		goto out;
	}

	if (entry) {
		/* The timestamp is too old, need to replace the values. */
		debug2("%s: found old entry for uid=%u, refreshing",
		       __func__, entry->uid);
	} else {
		debug2("%s: no entry found for uid=%u", __func__, needle->uid);
	}

	_init_or_reinit_entry(&entry, needle);

	if (!entry) {
		error("failed to init group cache entry for uid=%u",
		      needle->uid);

		/*
		 * getgrouplist() does not have a way to signal failure, so
		 * return the primary group as the single member of the
		 * extended group list.
		 */
		*gids = xmalloc(sizeof(gid_t));
		*gids[0] = needle->gid;
		return 1;
	}

	/*
	 * Cache lookup failed or entry value was too old, fetch new value and
	 * insert it into cache.
	 */
#if defined(__APPLE__)
	/*
	 * macOS has (int *) for the third argument instead
	 * of (gid_t *) like FreeBSD, NetBSD, and Linux.
	 */
	while (getgrouplist(entry->username, entry->gid,
			    (int *)entry->gids, &entry->ngids) == -1) {
#else
	/*
	 * entry->gid will be in the result. This is the users primary
	 * group as determined from passwd.
	 */
	while (getgrouplist(entry->username, entry->gid,
			    entry->gids, &entry->ngids) == -1) {
#endif
		/* group list larger than array, resize array to fit */
		entry->gids = xrecalloc(entry->gids, entry->ngids,
					sizeof(gid_t));
	}

out:
	ngids = entry->ngids;
	xfree(*gids);
	*gids = copy_gids(entry->ngids, entry->gids);

	slurm_mutex_unlock(&gids_mutex);

	END_TIMER3("group_cache_lookup() took",
		   3000000);

	return ngids;
}

/*
 * OUT: ngids as return value
 * IN: uid
 * IN: gid - primary group id (will always exist first in gids list)
 * IN: (optional) username, will be looked up if NULL and is needed
 * IN/OUT: gids - xmalloc'd gid_t * structure with ngids elements
 */
extern int group_cache_lookup(uid_t uid, gid_t gid, char *username, gid_t **gids)
{
	gids_cache_needle_t needle = {0};

	needle.username = username;
	needle.uid = uid;
	needle.gid = gid;

	return _group_cache_lookup_internal(&needle, gids);
}

static int _cleanup_search(void *x, void *key)
{
	gids_cache_t *cached = (gids_cache_t *) x;
	time_t *now = (time_t *) key;

	if (cached->expiration < *now)
		return 1;

	return 0;
}

/*
 * Call periodically to remove old records.
 */
extern void group_cache_cleanup(void)
{
	time_t now = time(NULL);

	slurm_mutex_lock(&gids_mutex);
	if (gids_cache_list)
		list_delete_all(gids_cache_list, _cleanup_search, &now);
	slurm_mutex_unlock(&gids_mutex);
}

extern gid_t *copy_gids(int ngids, gid_t *gids)
{
	int size;
	gid_t *result;

	if (!ngids)
		return NULL;

	if (!gids)
		fatal_abort("%s: ngids=%d but gids=NULL", __func__, ngids);

	size = ngids * sizeof(gid_t);
	result = xmalloc(size);
	memcpy(result, gids, size);

	return result;
}

extern char **copy_gr_names(int ngids, char **gr_names)
{
	char **result;

	if (!ngids || !gr_names)
		return NULL;

	result = xcalloc(ngids, sizeof(char *));
	for (int i = 0; i < ngids; i++)
		result[i] = xstrdup(gr_names[i]);

	return result;
}
