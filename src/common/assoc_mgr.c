/*****************************************************************************\
 *  assoc_mgr.c - File to keep track of associations/QOS used by the daemons
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "assoc_mgr.h"

#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>

#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurmdbd_pack.h"
#include "src/slurmdbd/read_config.h"

#define ASSOC_HASH_SIZE 1000
#define ASSOC_HASH_ID_INX(_assoc_id)	(_assoc_id % ASSOC_HASH_SIZE)

slurmdb_assoc_rec_t *assoc_mgr_root_assoc = NULL;
uint32_t g_qos_max_priority = 0;
uint32_t g_qos_count = 0;
uint32_t g_user_assoc_count = 0;
uint32_t g_tres_count = 0;

List assoc_mgr_tres_list = NULL;
slurmdb_tres_rec_t **assoc_mgr_tres_array = NULL;
char **assoc_mgr_tres_name_array = NULL;
List assoc_mgr_assoc_list = NULL;
List assoc_mgr_res_list = NULL;
List assoc_mgr_qos_list = NULL;
List assoc_mgr_user_list = NULL;
List assoc_mgr_wckey_list = NULL;

static char *assoc_mgr_cluster_name = NULL;
static int setup_children = 0;
static pthread_rwlock_t assoc_mgr_locks[ASSOC_MGR_ENTITY_COUNT]
	= { PTHREAD_RWLOCK_INITIALIZER };

static assoc_init_args_t init_setup;
static slurmdb_assoc_rec_t **assoc_hash_id = NULL;
static slurmdb_assoc_rec_t **assoc_hash = NULL;
static int *assoc_mgr_tres_old_pos = NULL;

static bool _running_cache(void)
{
	if (init_setup.running_cache && *init_setup.running_cache)
		return true;

	return false;
}

static int _get_str_inx(char *name)
{
	int j, index = 0;

	if (!name)
		return 0;

	for (j = 1; *name; name++, j++)
		index += (int)tolower(*name) * j;

	return index;
}

static int _assoc_hash_index(slurmdb_assoc_rec_t *assoc)
{
	int index;

	xassert(assoc);

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy.
	 */

	index = assoc->uid;

	/* only set on the slurmdbd */
	if (!assoc_mgr_cluster_name && assoc->cluster)
		index += _get_str_inx(assoc->cluster);

	if (assoc->acct)
		index += _get_str_inx(assoc->acct);

	if (assoc->partition)
		index += _get_str_inx(assoc->partition);

	index %= ASSOC_HASH_SIZE;
	if (index < 0)
		index += ASSOC_HASH_SIZE;

	return index;

}

static void _add_assoc_hash(slurmdb_assoc_rec_t *assoc)
{
	int inx = ASSOC_HASH_ID_INX(assoc->id);

	if (!assoc_hash_id)
		assoc_hash_id = xmalloc(ASSOC_HASH_SIZE *
				     sizeof(slurmdb_assoc_rec_t *));
	if (!assoc_hash)
		assoc_hash = xmalloc(ASSOC_HASH_SIZE *
				     sizeof(slurmdb_assoc_rec_t *));

	assoc->assoc_next_id = assoc_hash_id[inx];
	assoc_hash_id[inx] = assoc;

	inx = _assoc_hash_index(assoc);
	assoc->assoc_next = assoc_hash[inx];
	assoc_hash[inx] = assoc;
}

static bool _remove_from_assoc_list(slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *assoc_ptr;
	ListIterator itr = list_iterator_create(assoc_mgr_assoc_list);

	while ((assoc_ptr = list_next(itr))) {
		if (assoc_ptr == assoc) {
			list_remove(itr);
			break;
		}
	}

	list_iterator_destroy(itr);

	return assoc_ptr ? 1 : 0;
}
/*
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 */
static slurmdb_assoc_rec_t *_find_assoc_rec_id(uint32_t assoc_id)
{
	slurmdb_assoc_rec_t *assoc;

	if (!assoc_hash_id) {
		debug2("_find_assoc_rec_id: no associations added yet");
		return NULL;
	}

	assoc =	assoc_hash_id[ASSOC_HASH_ID_INX(assoc_id)];

	while (assoc) {
		if (assoc->id == assoc_id)
			return assoc;
		assoc = assoc->assoc_next_id;
	}

	return NULL;
}

/*
 * _find_assoc_rec - return a pointer to the assoc_ptr with the given
 * contents of assoc.
 * IN assoc - requested association info
 * RET pointer to the assoc_ptr's record, NULL on error
 */
static slurmdb_assoc_rec_t *_find_assoc_rec(
	slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *assoc_ptr;
	int inx;

	if (assoc->id)
		return _find_assoc_rec_id(assoc->id);

	if (!assoc_hash) {
		debug2("_find_assoc_rec: no associations added yet");
		return NULL;
	}


	inx = _assoc_hash_index(assoc);
	assoc_ptr = assoc_hash[inx];

	while (assoc_ptr) {
		if ((!assoc->user && (assoc->uid == NO_VAL))
		    && (assoc_ptr->user || (assoc_ptr->uid != NO_VAL))) {
			debug3("%s: we are looking for a nonuser association",
				__func__);
			goto next;
		} else if ((!assoc_ptr->user && (assoc_ptr->uid == NO_VAL))
			   && (assoc->user || (assoc->uid != NO_VAL))) {
			debug3("%s: we are looking for a user association",
				__func__);
			goto next;
		} else if (assoc->user && assoc_ptr->user
			   && ((assoc->uid == NO_VAL) ||
			       (assoc_ptr->uid == NO_VAL))) {
			/* This means the uid isn't set in one of the
			 * associations, so use the name instead
			 */
			if (xstrcasecmp(assoc->user, assoc_ptr->user)) {
				debug3("%s: 2 not the right user %u != %u",
				       __func__, assoc->uid, assoc_ptr->uid);
				goto next;
			}
		} else if (assoc->uid != assoc_ptr->uid) {
			debug3("%s: not the right user %u != %u",
			       __func__, assoc->uid, assoc_ptr->uid);
			goto next;
		}

		if (assoc->acct &&
		    (!assoc_ptr->acct
		     || xstrcasecmp(assoc->acct, assoc_ptr->acct))) {
			debug3("%s: not the right account %s != %s",
			       __func__, assoc->acct, assoc_ptr->acct);
			goto next;
		}

		/* only check for on the slurmdbd */
		if (!assoc_mgr_cluster_name && assoc->cluster
		    && (!assoc_ptr->cluster
			|| xstrcasecmp(assoc->cluster, assoc_ptr->cluster))) {
			debug3("%s: not the right cluster", __func__);
			goto next;
		}

		if (assoc->partition
		    && (!assoc_ptr->partition
			|| xstrcasecmp(assoc->partition,
				       assoc_ptr->partition))) {
			debug3("%s: not the right partition", __func__);
			goto next;
		}

		break;
	next:
		assoc_ptr = assoc_ptr->assoc_next;
	}

	return assoc_ptr;
}

/*
 * _list_delete_assoc - delete a assoc record
 * IN assoc_entry - pointer to assoc_record to delete
 * global: assoc_list - pointer to global assoc list
 *	assoc_count - count of assoc list entries
 *	assoc_hash - hash table into assoc records
 */
static void _delete_assoc_hash(slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *assoc_ptr = assoc;
	slurmdb_assoc_rec_t **assoc_pptr;

	xassert(assoc);

	/* Remove the record from assoc hash table */
	assoc_pptr = &assoc_hash_id[ASSOC_HASH_ID_INX(assoc_ptr->id)];
	while (assoc_pptr && ((assoc_ptr = *assoc_pptr) != assoc)) {
		if (!assoc_ptr->assoc_next_id)
			assoc_pptr = NULL;
		else
			assoc_pptr = &assoc_ptr->assoc_next_id;
	}

	if (!assoc_pptr) {
		fatal("assoc id hash error");
		return;	/* Fix CLANG false positive error */
	} else
		*assoc_pptr = assoc_ptr->assoc_next_id;

	assoc_ptr = assoc;
	assoc_pptr = &assoc_hash[_assoc_hash_index(assoc_ptr)];
	while (assoc_pptr && ((assoc_ptr = *assoc_pptr) != assoc)) {
		if (!assoc_ptr->assoc_next)
			assoc_pptr = NULL;
		else
			assoc_pptr = &assoc_ptr->assoc_next;
	}

	if (!assoc_pptr) {
		fatal("assoc hash error");
		return;	/* Fix CLANG false positive error */
	} else
		*assoc_pptr = assoc_ptr->assoc_next;
}


static void _normalize_assoc_shares_fair_tree(
	slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *fs_assoc = assoc;
	double shares_norm = 0.0;

	if ((assoc->shares_raw == SLURMDB_FS_USE_PARENT)
	    && assoc->usage->fs_assoc_ptr)
		fs_assoc = assoc->usage->fs_assoc_ptr;

	if (fs_assoc->usage->level_shares)
		shares_norm =
			(double)fs_assoc->shares_raw /
			(double)fs_assoc->usage->level_shares;
	assoc->usage->shares_norm = shares_norm;
}


/* you should check for assoc == NULL before this function */
static void _normalize_assoc_shares_traditional(
		slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *assoc2 = assoc;

	if ((assoc->shares_raw == SLURMDB_FS_USE_PARENT)
	    && assoc->usage->fs_assoc_ptr) {
		debug3("assoc %u(%s %s) normalize = %f from parent %u(%s %s)",
		       assoc->id, assoc->acct, assoc->user,
		       assoc->usage->fs_assoc_ptr->usage->shares_norm,
		       assoc->usage->fs_assoc_ptr->id,
		       assoc->usage->fs_assoc_ptr->acct,
		       assoc->usage->fs_assoc_ptr->user);
		assoc->usage->shares_norm =
			assoc->usage->fs_assoc_ptr->usage->shares_norm;
		return;
	}

	assoc2->usage->shares_norm = 1.0;
	while (assoc->usage->parent_assoc_ptr) {
		if (assoc->shares_raw != SLURMDB_FS_USE_PARENT) {
			if (!assoc->usage->level_shares)
				assoc2->usage->shares_norm = 0;
			else
				assoc2->usage->shares_norm *=
					(double)assoc->shares_raw /
					(double)assoc->usage->level_shares;
			debug3("assoc %u(%s %s) normalize = %f "
			       "from %u(%s %s) %u / %u = %f",
			       assoc2->id, assoc2->acct, assoc2->user,
			       assoc2->usage->shares_norm,
			       assoc->id, assoc->acct, assoc->user,
			       assoc->shares_raw,
			       assoc->usage->level_shares,
			       assoc->usage->level_shares ?
			       (double)assoc->shares_raw /
			       (double)assoc->usage->level_shares :
			       0);
		}

		assoc = assoc->usage->parent_assoc_ptr;
	}
}


static int _addto_used_info(slurmdb_assoc_rec_t *assoc1,
			    slurmdb_assoc_rec_t *assoc2)
{
	int i;

	if (!assoc1 || !assoc2)
		return SLURM_ERROR;

	for (i=0; i < assoc1->usage->tres_cnt; i++) {
		assoc1->usage->grp_used_tres[i] +=
			assoc2->usage->grp_used_tres[i];
		assoc1->usage->grp_used_tres_run_secs[i] +=
			assoc2->usage->grp_used_tres_run_secs[i];
		assoc1->usage->usage_tres_raw[i] +=
			assoc2->usage->usage_tres_raw[i];
	}

	assoc1->usage->accrue_cnt += assoc2->usage->accrue_cnt;

	assoc1->usage->grp_used_wall += assoc2->usage->grp_used_wall;

	assoc1->usage->used_jobs += assoc2->usage->used_jobs;
	assoc1->usage->used_submit_jobs += assoc2->usage->used_submit_jobs;
	assoc1->usage->usage_raw += assoc2->usage->usage_raw;

	return SLURM_SUCCESS;
}

static int _clear_used_assoc_info(slurmdb_assoc_rec_t *assoc)
{
	int i;

	if (!assoc || !assoc->usage)
		return SLURM_ERROR;

	for (i=0; i<assoc->usage->tres_cnt; i++) {
		assoc->usage->grp_used_tres[i] = 0;
		assoc->usage->grp_used_tres_run_secs[i] = 0;
	}

	assoc->usage->accrue_cnt = 0;
	assoc->usage->used_jobs  = 0;
	assoc->usage->used_submit_jobs = 0;
	/* do not reset usage_raw or grp_used_wall.
	 * if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	return SLURM_SUCCESS;
}

static void _clear_qos_used_limit_list(List used_limit_list, uint32_t tres_cnt)
{
	slurmdb_used_limits_t *used_limits = NULL;
	ListIterator itr = NULL;
	int i;

	if (!used_limit_list || !list_count(used_limit_list))
		return;

	itr = list_iterator_create(used_limit_list);
	while ((used_limits = list_next(itr))) {
		used_limits->accrue_cnt = 0;
		used_limits->jobs = 0;
		used_limits->submit_jobs = 0;
		for (i=0; i<tres_cnt; i++) {
			used_limits->tres[i] = 0;
			used_limits->tres_run_mins[i] = 0;
		}
	}
	list_iterator_destroy(itr);

	return;
}

static void _clear_qos_acct_limit_info(slurmdb_qos_rec_t *qos_ptr)
{
	_clear_qos_used_limit_list(qos_ptr->usage->acct_limit_list,
				   qos_ptr->usage->tres_cnt);
}

static void _clear_qos_user_limit_info(slurmdb_qos_rec_t *qos_ptr)
{
	_clear_qos_used_limit_list(qos_ptr->usage->user_limit_list,
				   qos_ptr->usage->tres_cnt);
}

static int _clear_used_qos_info(slurmdb_qos_rec_t *qos)
{
	int i;

	if (!qos || !qos->usage)
		return SLURM_ERROR;

	qos->usage->accrue_cnt = 0;
	qos->usage->grp_used_jobs  = 0;
	qos->usage->grp_used_submit_jobs = 0;
	for (i=0; i<qos->usage->tres_cnt; i++) {
		qos->usage->grp_used_tres[i] = 0;
		qos->usage->grp_used_tres_run_secs[i] = 0;
	}
	/* do not reset usage_raw or grp_used_wall.
	 * if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	_clear_qos_acct_limit_info(qos);
	_clear_qos_user_limit_info(qos);

	return SLURM_SUCCESS;
}

/* Locks should be in place before calling this. */
static int _change_user_name(slurmdb_user_rec_t *user)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	uid_t pw_uid;

	xassert(user->name);
	xassert(user->old_name);

	if (uid_from_string(user->name, &pw_uid) < 0) {
		debug("_change_user_name: couldn't get new uid for user %s",
		      user->name);
		user->uid = NO_VAL;
	} else
		user->uid = pw_uid;

	if (assoc_mgr_assoc_list) {
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc = list_next(itr))) {
			if (!assoc->user)
				continue;
			if (!xstrcmp(user->old_name, assoc->user)) {
				/* Since the uid changed the
				   hash as well will change.  Remove
				   the assoc from the hash before the
				   change or you won't find it.
				*/
				_delete_assoc_hash(assoc);

				xfree(assoc->user);
				assoc->user = xstrdup(user->name);
				assoc->uid = user->uid;
				_add_assoc_hash(assoc);
				debug3("changing assoc %d", assoc->id);
			}
		}
		list_iterator_destroy(itr);
	}

	if (assoc_mgr_wckey_list) {
		itr = list_iterator_create(assoc_mgr_wckey_list);
		while ((wckey = list_next(itr))) {
			if (!xstrcmp(user->old_name, wckey->user)) {
				xfree(wckey->user);
				wckey->user = xstrdup(user->name);
				wckey->uid = user->uid;
				debug3("changing wckey %d", wckey->id);
			}
		}
		list_iterator_destroy(itr);
	}

	return rc;
}

static int _grab_parents_qos(slurmdb_assoc_rec_t *assoc)
{
	slurmdb_assoc_rec_t *parent_assoc = NULL;
	char *qos_char = NULL;
	ListIterator itr = NULL;

	if (!assoc)
		return SLURM_ERROR;

	if (assoc->qos_list)
		list_flush(assoc->qos_list);
	else
		assoc->qos_list = list_create(slurm_destroy_char);

	parent_assoc = assoc->usage->parent_assoc_ptr;

	if (!parent_assoc || !parent_assoc->qos_list
	    || !list_count(parent_assoc->qos_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(parent_assoc->qos_list);
	while ((qos_char = list_next(itr)))
		list_append(assoc->qos_list, xstrdup(qos_char));
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _local_update_assoc_qos_list(slurmdb_assoc_rec_t *assoc,
					List new_qos_list)
{
	ListIterator new_qos_itr = NULL, curr_qos_itr = NULL;
	char *new_qos = NULL, *curr_qos = NULL;
	int flushed = 0;

	if (!assoc || !new_qos_list) {
		error("need both new qos_list and an association to update");
		return SLURM_ERROR;
	}

	if (!list_count(new_qos_list)) {
		_grab_parents_qos(assoc);
		return SLURM_SUCCESS;
	}

	/* Even though we only use the valid_qos bitstr for things we
	   need to keep the list around for now since we don't pack the
	   bitstr for state save.
	*/
	new_qos_itr = list_iterator_create(new_qos_list);
	curr_qos_itr = list_iterator_create(assoc->qos_list);

	while ((new_qos = list_next(new_qos_itr))) {
		if (new_qos[0] == '-') {
			while ((curr_qos = list_next(curr_qos_itr))) {
				if (!xstrcmp(curr_qos, new_qos+1)) {
					list_delete_item(curr_qos_itr);
					break;
				}
			}

			list_iterator_reset(curr_qos_itr);
		} else if (new_qos[0] == '+') {
			while ((curr_qos = list_next(curr_qos_itr)))
				if (!xstrcmp(curr_qos, new_qos+1))
					break;

			if (!curr_qos) {
				list_append(assoc->qos_list,
					    xstrdup(new_qos+1));
				list_iterator_reset(curr_qos_itr);
			}
		} else if (new_qos[0] == '=') {
			if (!flushed)
				list_flush(assoc->qos_list);
			list_append(assoc->qos_list, xstrdup(new_qos+1));
			flushed = 1;
		} else if (new_qos[0]) {
			if (!flushed)
				list_flush(assoc->qos_list);
			list_append(assoc->qos_list, xstrdup(new_qos));
			flushed = 1;
		}
	}
	list_iterator_destroy(curr_qos_itr);
	list_iterator_destroy(new_qos_itr);

	return SLURM_SUCCESS;
}

/* locks should be put in place before calling this function USER_WRITE */
static void _set_user_default_acct(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc);
	xassert(assoc->acct);
	xassert(assoc_mgr_user_list);

	/* set up the default if this is it */
	if ((assoc->is_def == 1) && (assoc->uid != NO_VAL)) {
		slurmdb_user_rec_t *user = NULL;
		ListIterator user_itr =
			list_iterator_create(assoc_mgr_user_list);
		while ((user = list_next(user_itr))) {
			if (user->uid != assoc->uid)
				continue;
			if (!user->default_acct
			    || xstrcmp(user->default_acct, assoc->acct)) {
				xfree(user->default_acct);
				user->default_acct = xstrdup(assoc->acct);
				debug2("user %s default acct is %s",
				       user->name, user->default_acct);
			}
			break;
		}
		list_iterator_destroy(user_itr);
	}
}

/* locks should be put in place before calling this function USER_WRITE */
static void _set_user_default_wckey(slurmdb_wckey_rec_t *wckey)
{
	xassert(wckey);
	xassert(wckey->name);
	xassert(assoc_mgr_user_list);

	/* set up the default if this is it */
	if ((wckey->is_def == 1) && (wckey->uid != NO_VAL)) {
		slurmdb_user_rec_t *user = NULL;
		ListIterator user_itr =
			list_iterator_create(assoc_mgr_user_list);
		while ((user = list_next(user_itr))) {
			if (user->uid != wckey->uid)
				continue;
			if (!user->default_wckey
			    || xstrcmp(user->default_wckey, wckey->name)) {
				xfree(user->default_wckey);
				user->default_wckey = xstrdup(wckey->name);
				debug2("user %s default wckey is %s",
				       user->name, user->default_wckey);
			}
			break;
		}
		list_iterator_destroy(user_itr);
	}
}

/* Return first parent that is not SLURMDB_FS_USE_PARENT unless
 * direct is set */
static slurmdb_assoc_rec_t* _find_assoc_parent(
	slurmdb_assoc_rec_t *assoc, bool direct)
{
	slurmdb_assoc_rec_t *parent = NULL, *prev_parent;
	xassert(assoc);

	parent = assoc;

	while (parent) {
		if (!parent->parent_id)
			break;

		prev_parent = parent;
		if (!(parent = _find_assoc_rec_id(prev_parent->parent_id))) {
			error("Can't find parent id %u for assoc %u, "
			      "this should never happen.",
			      prev_parent->parent_id, prev_parent->id);
			break;
		}
		/* See if we need to look for the next parent up the tree */
		if (direct || (assoc->shares_raw != SLURMDB_FS_USE_PARENT) ||
		    (parent->shares_raw != SLURMDB_FS_USE_PARENT))
			break;
	}

	if (parent)
		debug2("assoc %u(%s, %s) has %s parent of %u(%s, %s)",
		       assoc->id, assoc->acct, assoc->user,
		       direct ? "direct" : "fs",
		       parent->id, parent->acct, parent->user);
	else
		debug2("assoc %u(%s, %s) doesn't have a %s "
		       "parent (probably root)",
		       assoc->id, assoc->acct, assoc->user,
		       direct ? "direct" : "fs");

	return parent;
}

static int _set_assoc_parent_and_user(slurmdb_assoc_rec_t *assoc,
				      int reset)
{
	xassert(verify_assoc_lock(ASSOC_LOCK, WRITE_LOCK));
	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	xassert(assoc_mgr_user_list);

	if (!assoc || !assoc_mgr_assoc_list) {
		error("you didn't give me an association");
		return SLURM_ERROR;
	}

	if (!assoc->usage)
		assoc->usage = slurmdb_create_assoc_usage(g_tres_count);

	if (assoc->parent_id) {
		/* Here we need the direct parent (parent_assoc_ptr)
		 * and also the first parent that doesn't have
		 * shares_raw == SLURMDB_FS_USE_PARENT (fs_assoc_ptr).
		 */
		assoc->usage->parent_assoc_ptr =
			_find_assoc_parent(assoc, true);
		if (!assoc->usage->parent_assoc_ptr) {
			error("Can't find parent id %u for assoc %u, "
			      "this should never happen.",
			      assoc->parent_id, assoc->id);
			assoc->usage->fs_assoc_ptr = NULL;
		} else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
			assoc->usage->fs_assoc_ptr =
				_find_assoc_parent(assoc, false);
		else if (assoc->usage->parent_assoc_ptr->shares_raw
			 == SLURMDB_FS_USE_PARENT)
			assoc->usage->fs_assoc_ptr = _find_assoc_parent(
				assoc->usage->parent_assoc_ptr, false);
		else
			assoc->usage->fs_assoc_ptr =
				assoc->usage->parent_assoc_ptr;

		if (assoc->usage->fs_assoc_ptr && setup_children) {
			if (!assoc->usage->fs_assoc_ptr->usage)
				assoc->usage->fs_assoc_ptr->usage =
					slurmdb_create_assoc_usage(
						g_tres_count);
			if (!assoc->usage->
			    fs_assoc_ptr->usage->children_list)
				assoc->usage->
					fs_assoc_ptr->usage->children_list =
					list_create(NULL);
			list_append(assoc->usage->
				    fs_assoc_ptr->usage->children_list,
				    assoc);
		}

		if (assoc == assoc->usage->parent_assoc_ptr) {
			assoc->usage->parent_assoc_ptr = NULL;
			assoc->usage->fs_assoc_ptr = NULL;
			error("association %u was pointing to "
			      "itself as it's parent",
			      assoc->id);
		}
	} else if (assoc_mgr_root_assoc != assoc) {
		slurmdb_assoc_rec_t *last_root = assoc_mgr_root_assoc;

		assoc_mgr_root_assoc = assoc;
		/* set up new root since if running off cache the
		   total usage for the cluster doesn't get set up again */
		if (last_root) {
			assoc_mgr_root_assoc->usage->usage_raw =
				last_root->usage->usage_raw;
			assoc_mgr_root_assoc->usage->usage_norm =
				last_root->usage->usage_norm;
			memcpy(assoc_mgr_root_assoc->usage->usage_tres_raw,
			       last_root->usage->usage_tres_raw,
			       sizeof(long double) * g_tres_count);
		}
	}

	if (assoc->user) {
		uid_t pw_uid;

		g_user_assoc_count++;
		if (assoc->uid == NO_VAL || assoc->uid == INFINITE ||
				assoc->uid == 0) {
			if (uid_from_string(assoc->user, &pw_uid) < 0)
				assoc->uid = NO_VAL;
			else
				assoc->uid = pw_uid;
		}
		_set_user_default_acct(assoc);

		/* get the qos bitmap here */
		if (g_qos_count > 0) {
			if (!assoc->usage->valid_qos
			    || (bit_size(assoc->usage->valid_qos)
				!= g_qos_count)) {
				FREE_NULL_BITMAP(assoc->usage->valid_qos);
				assoc->usage->valid_qos =
					bit_alloc(g_qos_count);
			} else
				bit_nclear(assoc->usage->valid_qos, 0,
					   (bit_size(assoc->usage->valid_qos)
					    - 1));
			set_qos_bitstr_from_list(assoc->usage->valid_qos,
						 assoc->qos_list);
			if (((int32_t)assoc->def_qos_id > 0)
			    && !bit_test(assoc->usage->valid_qos,
					 assoc->def_qos_id)) {
				error("assoc %u doesn't have access "
				      "to it's default qos '%s'",
				      assoc->id,
				      slurmdb_qos_str(assoc_mgr_qos_list,
						      assoc->def_qos_id));
				assoc->def_qos_id = 0;
			}
		} else
			assoc->def_qos_id = 0;
	} else {
		assoc->uid = NO_VAL;
	}
	/* If you uncomment this below make sure you put READ_LOCK on
	 * the qos_list (the third lock) on calling functions.
	 */
	//log_assoc_rec(assoc);

	return SLURM_SUCCESS;
}

static void _set_qos_norm_priority(slurmdb_qos_rec_t *qos)
{
	if (!qos || !g_qos_max_priority)
		return;

	if (!qos->usage)
		qos->usage = slurmdb_create_qos_usage(g_tres_count);
	qos->usage->norm_priority =
		(double)qos->priority / (double)g_qos_max_priority;
}

static uint32_t _get_children_level_shares(slurmdb_assoc_rec_t *assoc)
{
	List children = assoc->usage->children_list;
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *child;
	uint32_t sum = 0;

	if (!children || list_is_empty(children))
		return 0;

	itr = list_iterator_create(children);
	while ((child = list_next(itr))) {
		if (child->shares_raw == SLURMDB_FS_USE_PARENT)
			sum += _get_children_level_shares(child);
		else
			sum += child->shares_raw;
	}
	list_iterator_destroy(itr);

	return sum;
}


static void _set_children_level_shares(slurmdb_assoc_rec_t *assoc,
				       uint32_t level_shares)
{
	List children = assoc->usage->children_list;
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *child;

	if (!children || list_is_empty(children))
		return;
	//info("parent %d %s %s", assoc->id, assoc->acct, assoc->user);
	itr = list_iterator_create(children);
	while ((child = list_next(itr))) {
		/* info("%d %s %s has %d shares", */
		/*      child->id, child->acct, child->user, level_shares); */
		child->usage->level_shares = level_shares;
	}
	list_iterator_destroy(itr);
}

/* transfer slurmdb assoc list to be assoc_mgr assoc list */
static int _post_assoc_list(void)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	ListIterator itr = NULL;
	int reset = 1;
	//DEF_TIMERS;

	xassert(verify_assoc_lock(ASSOC_LOCK, WRITE_LOCK));
	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	if (!assoc_mgr_assoc_list)
		return SLURM_ERROR;

	xfree(assoc_hash_id);
	xfree(assoc_hash);

	itr = list_iterator_create(assoc_mgr_assoc_list);

	//START_TIMER;
	g_user_assoc_count = 0;
	while ((assoc = list_next(itr))) {
		_set_assoc_parent_and_user(assoc, reset);
		_add_assoc_hash(assoc);
		assoc_mgr_set_assoc_tres_cnt(assoc);
		reset = 0;
	}

	if (setup_children) {
		/* Now set the shares on each level */
		list_iterator_reset(itr);
		while ((assoc = list_next(itr))) {
			if (!assoc->usage->children_list
			    || list_is_empty(assoc->usage->children_list))
				continue;

			_set_children_level_shares(
				assoc,
				_get_children_level_shares(assoc));
		}
		/* Now normalize the static shares */
		list_iterator_reset(itr);
		while ((assoc = list_next(itr)))
			assoc_mgr_normalize_assoc_shares(assoc);
	}
	list_iterator_destroy(itr);

	slurmdb_sort_hierarchical_assoc_list(assoc_mgr_assoc_list, true);

	//END_TIMER2("load_associations");
	return SLURM_SUCCESS;
}

static int _post_user_list(List user_list)
{
	slurmdb_user_rec_t *user = NULL;
	ListIterator itr = list_iterator_create(user_list);
	//START_TIMER;
	while ((user = list_next(itr))) {
		uid_t pw_uid;
		/* Just to make sure we have a default_wckey since it
		   might not be set up yet.
		*/
		if (!user->default_wckey)
			user->default_wckey = xstrdup("");
		if (uid_from_string (user->name, &pw_uid) < 0) {
			if (slurmdbd_conf)
				debug("post user: couldn't get a "
				      "uid for user %s",
				      user->name);
			user->uid = NO_VAL;
		} else
			user->uid = pw_uid;
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

static int _post_wckey_list(List wckey_list)
{
	slurmdb_wckey_rec_t *wckey = NULL;
	ListIterator itr = list_iterator_create(wckey_list);
	//START_TIMER;

	xassert(assoc_mgr_user_list);

	while ((wckey = list_next(itr))) {
		uid_t pw_uid;
		if (uid_from_string (wckey->user, &pw_uid) < 0) {
			if (slurmdbd_conf)
				debug("post wckey: couldn't get a uid "
				      "for user %s",
				      wckey->user);
			wckey->uid = NO_VAL;
		} else
			wckey->uid = pw_uid;
		_set_user_default_wckey(wckey);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

/* NOTE QOS write lock needs to be set before calling this. */
static int _post_qos_list(List qos_list)
{
	slurmdb_qos_rec_t *qos = NULL;
	ListIterator itr = list_iterator_create(qos_list);

	g_qos_count = 0;
	g_qos_max_priority = 0;

	while ((qos = list_next(itr))) {
		if (qos->flags & QOS_FLAG_NOTSET)
			qos->flags = 0;

		if (!qos->usage)
			qos->usage = slurmdb_create_qos_usage(g_tres_count);
		/* get the highest qos value to create bitmaps from */
		if (qos->id > g_qos_count)
			g_qos_count = qos->id;

		if (qos->priority > g_qos_max_priority)
			g_qos_max_priority = qos->priority;

		assoc_mgr_set_qos_tres_cnt(qos);
	}
	/* Since in the database id's don't start at 1
	   instead of 0 we need to ignore the 0 bit and start
	   with 1 so increase the count by 1.
	*/
	if (g_qos_count > 0)
		g_qos_count++;

	if (g_qos_max_priority) {
		list_iterator_reset(itr);

		while ((qos = list_next(itr)))
			_set_qos_norm_priority(qos);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _post_res_list(List res_list)
{
	if (res_list && assoc_mgr_cluster_name) {
		slurmdb_res_rec_t *object = NULL;
		ListIterator itr = list_iterator_create(res_list);
		while ((object = list_next(itr))) {
			if (object->clus_res_list
			    && list_count(object->clus_res_list)) {
				xassert(!object->clus_res_rec);

				while ((object->clus_res_rec =
					list_pop(object->clus_res_list))) {
					/* only update the local clusters
					 * res, only one per res
					 * record, so throw the others away. */
					if (!xstrcasecmp(object->clus_res_rec->
							cluster,
							assoc_mgr_cluster_name))
						break;
					slurmdb_destroy_clus_res_rec(
						object->clus_res_rec);
				}
				FREE_NULL_LIST(object->clus_res_list);
			}

			if (!object->clus_res_rec) {
				error("Bad resource given %s@%s",
				      object->name, object->server);
				list_delete_item(itr);
			}
		}
		list_iterator_destroy(itr);
	}

	if (init_setup.sync_license_notify)
		init_setup.sync_license_notify(res_list);

	return SLURM_SUCCESS;
}

/*
 * Given the cur_pos of a tres in new_array return the old position of
 * the same tres in the old_array.
 */
static int _get_old_tres_pos(slurmdb_tres_rec_t **new_array,
			     slurmdb_tres_rec_t **old_array,
			     int cur_pos, int old_cnt)
{
	int j, pos = NO_VAL;

	/* This means the tres didn't change order */
	if ((cur_pos < old_cnt) &&
	    (new_array[cur_pos]->id == old_array[cur_pos]->id))
		pos = cur_pos;
	else {
		/* This means we might of changed the location or it
		 * wasn't there before so break
		 */
		for (j = 0; j < old_cnt; j++)
			if (new_array[cur_pos]->id == old_array[j]->id) {
				pos = j;
				break;
			}
	}

	return pos;
}

/* assoc, qos and tres write lock should be locked before calling this
 * return 1 if callback is needed */
extern int assoc_mgr_post_tres_list(List new_list)
{
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec, **new_array;
	char **new_name_array;
	bool changed_size = false, changed_pos = false;
	int i, new_size, new_name_size;
	int new_cnt;

	xassert(new_list);

	new_cnt = list_count(new_list);

	xassert(new_cnt > 0);

	new_size = sizeof(slurmdb_tres_rec_t) * new_cnt;
	new_array = xmalloc(new_size);

	new_name_size = sizeof(char *) * new_cnt;
	new_name_array = xmalloc(new_name_size);

	list_sort(new_list, (ListCmpF)slurmdb_sort_tres_by_id_asc);

	/* we don't care if it gets smaller */
	if (new_cnt > g_tres_count)
		changed_size = true;

	/* Set up the new array to see if we need to update any other
	   arrays with current values.
	*/
	i = 0;
	itr = list_iterator_create(new_list);
	while ((tres_rec = list_next(itr))) {

		new_array[i] = tres_rec;

		new_name_array[i] = xstrdup_printf(
			"%s%s%s",
			tres_rec->type,
			tres_rec->name ? "/" : "",
			tres_rec->name ? tres_rec->name : "");

		/*
		 * This can happen when a new static or dynamic TRES is added.
		 */
		if (assoc_mgr_tres_array && (i < g_tres_count) &&
		    (new_array[i]->id != assoc_mgr_tres_array[i]->id))
			changed_pos = true;
		i++;
	}
	list_iterator_destroy(itr);

	/* If for some reason the position changed
	 * (new static) we need to move it to it's new place.
	 */
	xfree(assoc_mgr_tres_old_pos);
	if (changed_pos) {
		int pos;

		assoc_mgr_tres_old_pos = xmalloc(sizeof(int) * new_cnt);
		for (i=0; i<new_cnt; i++) {
			if (!new_array[i]) {
				assoc_mgr_tres_old_pos[i] = -1;
				continue;
			}

			pos = _get_old_tres_pos(new_array, assoc_mgr_tres_array,
						i, g_tres_count);

			if (pos == NO_VAL)
				assoc_mgr_tres_old_pos[i] = -1;
			else
				assoc_mgr_tres_old_pos[i] = pos;
		}
	}


	xfree(assoc_mgr_tres_array);
	assoc_mgr_tres_array = new_array;
	new_array = NULL;

	if (assoc_mgr_tres_name_array) {
		for (i=0; i<g_tres_count; i++)
			xfree(assoc_mgr_tres_name_array[i]);
		xfree(assoc_mgr_tres_name_array);
	}
	assoc_mgr_tres_name_array = new_name_array;
	new_name_array = NULL;

	FREE_NULL_LIST(assoc_mgr_tres_list);
	assoc_mgr_tres_list = new_list;
	new_list = NULL;

	g_tres_count = new_cnt;

	if ((changed_size || changed_pos) &&
	    assoc_mgr_assoc_list && assoc_mgr_qos_list) {
		uint64_t grp_used_tres[new_cnt],
			grp_used_tres_run_secs[new_cnt];
		long double usage_tres_raw[new_cnt];
		slurmdb_assoc_rec_t *assoc_rec;
		slurmdb_qos_rec_t *qos_rec;
		int array_size = sizeof(uint64_t) * new_cnt;
		int d_array_size = sizeof(long double) * new_cnt;
		slurmdb_used_limits_t *used_limits;
		ListIterator itr_user;

		/* update the associations and such here */
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc_rec = list_next(itr))) {

			assoc_mgr_set_assoc_tres_cnt(assoc_rec);

			if (!assoc_rec->usage)
				continue;

			/* Need to increase the size of the usage counts. */
			if (changed_size) {
				assoc_rec->usage->tres_cnt = new_cnt;
				xrealloc(assoc_rec->usage->grp_used_tres,
					 array_size);
				xrealloc(assoc_rec->usage->
					 grp_used_tres_run_secs,
					 array_size);
				xrealloc(assoc_rec->usage->usage_tres_raw,
					 d_array_size);
			}


			if (changed_pos) {
				memset(grp_used_tres, 0, array_size);
				memset(grp_used_tres_run_secs, 0, array_size);
				memset(usage_tres_raw, 0, d_array_size);

				for (i=0; i<new_cnt; i++) {
					int old_pos = assoc_mgr_tres_old_pos[i];
					if (old_pos == -1)
						continue;

					grp_used_tres[i] = assoc_rec->
						usage->grp_used_tres[old_pos];
					grp_used_tres_run_secs[i] = assoc_rec->
						usage->grp_used_tres_run_secs
						[old_pos];
					usage_tres_raw[i] =
						assoc_rec->usage->usage_tres_raw
						[old_pos];
				}
				memcpy(assoc_rec->usage->grp_used_tres,
				       grp_used_tres, array_size);
				memcpy(assoc_rec->usage->grp_used_tres_run_secs,
				       grp_used_tres_run_secs, array_size);
				memcpy(assoc_rec->usage->usage_tres_raw,
				       usage_tres_raw, d_array_size);
			}
		}
		list_iterator_destroy(itr);

		/* update the qos and such here */
		itr = list_iterator_create(assoc_mgr_qos_list);
		while ((qos_rec = list_next(itr))) {

			assoc_mgr_set_qos_tres_cnt(qos_rec);

			if (!qos_rec->usage)
				continue;

			/* Need to increase the size of the usage counts. */
			if (changed_size) {
				qos_rec->usage->tres_cnt = new_cnt;
				xrealloc(qos_rec->usage->
					 grp_used_tres,
					 array_size);
				xrealloc(qos_rec->usage->
					 grp_used_tres_run_secs,
					 array_size);
				xrealloc(qos_rec->usage->
					 usage_tres_raw,
					 d_array_size);
				if (qos_rec->usage->user_limit_list) {
					itr_user = list_iterator_create(
						qos_rec->usage->
						user_limit_list);
					while ((used_limits = list_next(
							itr_user))) {
						xrealloc(used_limits->
							 tres,
							 array_size);
						xrealloc(used_limits->
							 tres_run_mins,
							 array_size);
					}
					list_iterator_destroy(itr_user);
				}
			}

			/* If for some reason the position changed
			 * (new static) we need to move it to it's new place.
			 */
			if (changed_pos) {
				memset(grp_used_tres, 0, array_size);
				memset(grp_used_tres_run_secs, 0, array_size);
				memset(usage_tres_raw, 0, d_array_size);

				for (i=0; i<new_cnt; i++) {
					int old_pos = assoc_mgr_tres_old_pos[i];
					if (old_pos == -1)
						continue;

					grp_used_tres[i] = qos_rec->
						usage->grp_used_tres[old_pos];
					grp_used_tres_run_secs[i] = qos_rec->
						usage->grp_used_tres_run_secs
						[old_pos];
					usage_tres_raw[i] =
						qos_rec->usage->usage_tres_raw
						[old_pos];
				}
				memcpy(qos_rec->usage->grp_used_tres,
				       grp_used_tres, array_size);
				memcpy(qos_rec->usage->grp_used_tres_run_secs,
				       grp_used_tres_run_secs, array_size);
				memcpy(qos_rec->usage->usage_tres_raw,
				       usage_tres_raw, d_array_size);
				if (qos_rec->usage->user_limit_list) {
					itr_user = list_iterator_create(
						qos_rec->usage->
						user_limit_list);
					while ((used_limits = list_next(
							itr_user))) {
						memset(grp_used_tres, 0,
						       array_size);
						memset(grp_used_tres_run_secs,
						       0, array_size);
						for (i=0; i<new_cnt; i++) {
							int old_pos =
								assoc_mgr_tres_old_pos[i];
							if (old_pos == -1)
								continue;

							grp_used_tres[i] =
								used_limits->
								tres[old_pos];
							grp_used_tres_run_secs
								[i] =
								used_limits->
								tres_run_mins
								[old_pos];
						}

						memcpy(used_limits->tres,
						       grp_used_tres,
						       array_size);
						memcpy(used_limits->
						       tres_run_mins,
						       grp_used_tres_run_secs,
						       array_size);
					}
					list_iterator_destroy(itr_user);
				}
			}
		}
		list_iterator_destroy(itr);
	}

	return (changed_size || changed_pos) ? 1 : 0;
}

static int _get_assoc_mgr_tres_list(void *db_conn, int enforce)
{
	slurmdb_tres_cond_t tres_q;
	uid_t uid = getuid();
	List new_list = NULL;
	char *tres_req_str;
	int changed;
	assoc_mgr_lock_t locks =
		{ .assoc = WRITE_LOCK, .qos = WRITE_LOCK, .tres= WRITE_LOCK };

	memset(&tres_q, 0, sizeof(slurmdb_tres_cond_t));

	assoc_mgr_lock(&locks);

	/* If this exists we only want/care about tracking/caching these TRES */
	if ((tres_req_str = slurm_get_accounting_storage_tres())) {
		tres_q.type_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(tres_q.type_list, tres_req_str);
		xfree(tres_req_str);
	}
	new_list = acct_storage_g_get_tres(
		db_conn, uid, &tres_q);

	FREE_NULL_LIST(tres_q.type_list);

	if (!new_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_tres_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}
	}

	changed = assoc_mgr_post_tres_list(new_list);

	assoc_mgr_unlock(&locks);

	if (changed && !_running_cache() && init_setup.update_cluster_tres) {
		/* update jobs here, this needs to be outside of the
		 * assoc_mgr locks */
		init_setup.update_cluster_tres();
	}

	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_assoc_list(void *db_conn, int enforce)
{
	slurmdb_assoc_cond_t assoc_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = READ_LOCK,
				   .tres = READ_LOCK, .user = WRITE_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_assoc_list);

	memset(&assoc_q, 0, sizeof(slurmdb_assoc_cond_t));
	if (assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_get_assoc_mgr_assoc_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

//	START_TIMER;
	assoc_mgr_assoc_list =
		acct_storage_g_get_assocs(db_conn, uid, &assoc_q);
//	END_TIMER2("get_assocs");

	FREE_NULL_LIST(assoc_q.cluster_list);

	if (!assoc_mgr_assoc_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		assoc_mgr_assoc_list =
			list_create(slurmdb_destroy_assoc_rec);
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_assoc_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing associations and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	}

	_post_assoc_list();

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_res_list(void *db_conn, int enforce)
{
	slurmdb_res_cond_t res_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .res = WRITE_LOCK };

	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_res_list);

	slurmdb_init_res_cond(&res_q, 0);
	if (assoc_mgr_cluster_name) {
		res_q.with_clusters = 1;
		res_q.cluster_list = list_create(NULL);
		list_append(res_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_get_assoc_mgr_res_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

	assoc_mgr_res_list = acct_storage_g_get_res(db_conn, uid, &res_q);

	FREE_NULL_LIST(res_q.cluster_list);

	if (!assoc_mgr_res_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_res_list:"
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}
	}

	_post_res_list(assoc_mgr_res_list);

	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_qos_list(void *db_conn, int enforce)
{
	uid_t uid = getuid();
	List new_list = NULL;
	assoc_mgr_lock_t locks = { .qos = WRITE_LOCK };

	new_list = acct_storage_g_get_qos(db_conn, uid, NULL);

	if (!new_list) {
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_qos_list: no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}
	}

	assoc_mgr_lock(&locks);

	FREE_NULL_LIST(assoc_mgr_qos_list);
	assoc_mgr_qos_list = new_list;
	new_list = NULL;

	_post_qos_list(assoc_mgr_qos_list);

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_user_list(void *db_conn, int enforce)
{
	slurmdb_user_cond_t user_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK };

	memset(&user_q, 0, sizeof(slurmdb_user_cond_t));
	user_q.with_coords = 1;

	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_user_list);
	assoc_mgr_user_list = acct_storage_g_get_users(db_conn, uid, &user_q);

	if (!assoc_mgr_user_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_user_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}
	}

	_post_user_list(assoc_mgr_user_list);

	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;
}


static int _get_assoc_mgr_wckey_list(void *db_conn, int enforce)
{
	slurmdb_wckey_cond_t wckey_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK, .wckey = WRITE_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_wckey_list);

	memset(&wckey_q, 0, sizeof(slurmdb_wckey_cond_t));
	if (assoc_mgr_cluster_name) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("_get_assoc_mgr_wckey_list: "
		      "no cluster name here going to get "
		      "all wckeys.");
	}

//	START_TIMER;
	assoc_mgr_wckey_list =
		acct_storage_g_get_wckeys(db_conn, uid, &wckey_q);
//	END_TIMER2("get_wckeys");

	FREE_NULL_LIST(wckey_q.cluster_list);

	if (!assoc_mgr_wckey_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		assoc_mgr_wckey_list = list_create(slurmdb_destroy_wckey_rec);
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
			error("_get_assoc_mgr_wckey_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing wckeys and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	}

	_post_wckey_list(assoc_mgr_wckey_list);

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_mgr_tres_list(void *db_conn, int enforce)
{
	/* this function does both get and refresh */
	_get_assoc_mgr_tres_list(db_conn, enforce);

	return SLURM_SUCCESS;
}

static int _refresh_assoc_mgr_assoc_list(void *db_conn, int enforce)
{
	slurmdb_assoc_cond_t assoc_q;
	List current_assocs = NULL;
	uid_t uid = getuid();
	ListIterator curr_itr = NULL;
	slurmdb_assoc_rec_t *curr_assoc = NULL, *assoc = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = READ_LOCK,
				   .tres = READ_LOCK, .user = WRITE_LOCK };
//	DEF_TIMERS;

	memset(&assoc_q, 0, sizeof(slurmdb_assoc_cond_t));
	if (assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_refresh_assoc_mgr_assoc_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

	assoc_mgr_lock(&locks);

	current_assocs = assoc_mgr_assoc_list;

//	START_TIMER;
	assoc_mgr_assoc_list =
		acct_storage_g_get_assocs(db_conn, uid, &assoc_q);
//	END_TIMER2("get_assocs");

	FREE_NULL_LIST(assoc_q.cluster_list);

	if (!assoc_mgr_assoc_list) {
		assoc_mgr_assoc_list = current_assocs;
		assoc_mgr_unlock(&locks);

		error("_refresh_assoc_mgr_assoc_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_assoc_list();

	if (!current_assocs) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	curr_itr = list_iterator_create(current_assocs);

	/* add used limits We only look for the user associations to
	 * do the parents since a parent may have moved */
	while ((curr_assoc = list_next(curr_itr))) {
		if (!curr_assoc->user)
			continue;

		if (!(assoc = _find_assoc_rec_id(curr_assoc->id)))
			continue;

		while (assoc) {
			_addto_used_info(assoc, curr_assoc);
			/* get the parent last since this pointer is
			   different than the one we are updating from */
			assoc = assoc->usage->parent_assoc_ptr;
		}
	}

	list_iterator_destroy(curr_itr);

	assoc_mgr_unlock(&locks);

	FREE_NULL_LIST(current_assocs);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_mgr_res_list(void *db_conn, int enforce)
{
	slurmdb_res_cond_t res_q;
	List current_res = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .res = WRITE_LOCK };

	slurmdb_init_res_cond(&res_q, 0);
	if (assoc_mgr_cluster_name) {
		res_q.with_clusters = 1;
		res_q.cluster_list = list_create(NULL);
		list_append(res_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_refresh_assoc_mgr_res_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

	current_res = acct_storage_g_get_res(db_conn, uid, &res_q);

	FREE_NULL_LIST(res_q.cluster_list);

	if (!current_res) {
		error("_refresh_assoc_mgr_res_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	assoc_mgr_lock(&locks);

	_post_res_list(current_res);

	FREE_NULL_LIST(assoc_mgr_res_list);

	assoc_mgr_res_list = current_res;

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_mgr_qos_list(void *db_conn, int enforce)
{
	List current_qos = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .qos = WRITE_LOCK };

	current_qos = acct_storage_g_get_qos(db_conn, uid, NULL);

	if (!current_qos) {
		error("_refresh_assoc_mgr_qos_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	assoc_mgr_lock(&locks);

	_post_qos_list(current_qos);

	/* move usage from old list over to the new one */
	if (assoc_mgr_qos_list) {
		slurmdb_qos_rec_t *curr_qos = NULL, *qos_rec = NULL;
		ListIterator itr = list_iterator_create(current_qos);

		while ((curr_qos = list_next(itr))) {
			if (!(qos_rec = list_find_first(assoc_mgr_qos_list,
							slurmdb_find_qos_in_list,
							&curr_qos->id)))
				continue;
			slurmdb_destroy_qos_usage(curr_qos->usage);
			curr_qos->usage = qos_rec->usage;
			qos_rec->usage = NULL;
		}
		list_iterator_destroy(itr);
		FREE_NULL_LIST(assoc_mgr_qos_list);
	}

	assoc_mgr_qos_list = current_qos;

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_mgr_user_list(void *db_conn, int enforce)
{
	List current_users = NULL;
	slurmdb_user_cond_t user_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK };

	memset(&user_q, 0, sizeof(slurmdb_user_cond_t));
	user_q.with_coords = 1;

	current_users = acct_storage_g_get_users(db_conn, uid, &user_q);

	if (!current_users) {
		error("_refresh_assoc_mgr_user_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}
	_post_user_list(current_users);

	assoc_mgr_lock(&locks);

	FREE_NULL_LIST(assoc_mgr_user_list);

	assoc_mgr_user_list = current_users;

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_wckey_list(void *db_conn, int enforce)
{
	slurmdb_wckey_cond_t wckey_q;
	List current_wckeys = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK, .wckey = WRITE_LOCK };

	memset(&wckey_q, 0, sizeof(slurmdb_wckey_cond_t));
	if (assoc_mgr_cluster_name) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("_refresh_assoc_wckey_list: "
		      "no cluster name here going to get "
		      "all wckeys.");
	}

	current_wckeys = acct_storage_g_get_wckeys(db_conn, uid, &wckey_q);

	FREE_NULL_LIST(wckey_q.cluster_list);

	if (!current_wckeys) {
		error("_refresh_assoc_wckey_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_wckey_list(current_wckeys);

	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_wckey_list);

	assoc_mgr_wckey_list = current_wckeys;
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args,
			  int db_conn_errno)
{
	static uint16_t checked_prio = 0;

	if (!checked_prio) {
		char *prio = slurm_get_priority_type();
		if (prio && xstrcmp(prio, "priority/basic"))
			setup_children = 1;

		xfree(prio);
		checked_prio = 1;
		memset(&init_setup, 0, sizeof(assoc_init_args_t));
		init_setup.cache_level = ASSOC_MGR_CACHE_ALL;
	}

	if (args)
		memcpy(&init_setup, args, sizeof(assoc_init_args_t));

	if (_running_cache()) {
		debug4("No need to run assoc_mgr_init, "
		       "we probably don't have a connection.  "
		       "If we do use assoc_mgr_refresh_lists instead.");
		return SLURM_SUCCESS;
	}

	if ((!assoc_mgr_cluster_name) && !slurmdbd_conf) {
		xfree(assoc_mgr_cluster_name);
		assoc_mgr_cluster_name = slurm_get_cluster_name();
	}

	/* check if we can't talk to the db yet (Do this after all
	 * the initialization above) */
	if (db_conn_errno != SLURM_SUCCESS)
		return SLURM_ERROR;

	/* get tres before association and qos since it is used there */
	if ((!assoc_mgr_tres_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_TRES)) {
		/*
		 * We need the old list just in case something changed.  If
		 * the tres is still stored in the assoc_mgr_list we will get
		 * it from there.  This second check can be removed 2 versions
		 * after 18.08.
		 */
		if (!slurmdbd_conf &&
		    (load_assoc_mgr_last_tres() != SLURM_SUCCESS))
			/* We don't care about the error here.  It should only
			 * happen if we can't find the file.  If that is the
			 * case then we don't need to worry about old state.
			 */
			(void)load_assoc_mgr_state(1);
		if (_get_assoc_mgr_tres_list(db_conn, init_setup.enforce)
		    == SLURM_ERROR)
			return SLURM_ERROR;
	}

	/* get qos before association since it is used there */
	if ((!assoc_mgr_qos_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_QOS))
		if (_get_assoc_mgr_qos_list(db_conn, init_setup.enforce) ==
		    SLURM_ERROR)
			return SLURM_ERROR;

	/* get user before association/wckey since it is used there */
	if ((!assoc_mgr_user_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_USER))
		if (_get_assoc_mgr_user_list(db_conn, init_setup.enforce) ==
		    SLURM_ERROR)
			return SLURM_ERROR;

	if ((!assoc_mgr_assoc_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_ASSOC))
		if (_get_assoc_mgr_assoc_list(db_conn, init_setup.enforce)
		    == SLURM_ERROR)
			return SLURM_ERROR;

	if (assoc_mgr_assoc_list && !setup_children) {
		slurmdb_assoc_rec_t *assoc = NULL;
		ListIterator itr =
			list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc = list_next(itr))) {
			log_assoc_rec(assoc, assoc_mgr_qos_list);
		}
		list_iterator_destroy(itr);
	}

	if ((!assoc_mgr_wckey_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_WCKEY))
		if (_get_assoc_mgr_wckey_list(db_conn, init_setup.enforce) ==
		    SLURM_ERROR)
			return SLURM_ERROR;

	if ((!assoc_mgr_res_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_RES))
		if (_get_assoc_mgr_res_list(db_conn, init_setup.enforce) ==
		    SLURM_ERROR)
			return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fini(bool save_state)
{
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK,
				   .res = WRITE_LOCK, .tres = WRITE_LOCK,
				   .user = WRITE_LOCK, .wckey = WRITE_LOCK };

	if (save_state)
		dump_assoc_mgr_state();

	assoc_mgr_lock(&locks);

	FREE_NULL_LIST(assoc_mgr_assoc_list);
	FREE_NULL_LIST(assoc_mgr_tres_list);
	FREE_NULL_LIST(assoc_mgr_res_list);
	FREE_NULL_LIST(assoc_mgr_qos_list);
	FREE_NULL_LIST(assoc_mgr_user_list);
	FREE_NULL_LIST(assoc_mgr_wckey_list);
	if (assoc_mgr_tres_name_array) {
		int i;
		for (i=0; i<g_tres_count; i++)
			xfree(assoc_mgr_tres_name_array[i]);
		xfree(assoc_mgr_tres_name_array);
	}
	xfree(assoc_mgr_tres_array);
	xfree(assoc_mgr_tres_old_pos);
	xfree(assoc_mgr_cluster_name);
	assoc_mgr_assoc_list = NULL;
	assoc_mgr_res_list = NULL;
	assoc_mgr_qos_list = NULL;
	assoc_mgr_user_list = NULL;
	assoc_mgr_wckey_list = NULL;

	assoc_mgr_root_assoc = NULL;

	if (_running_cache())
		*init_setup.running_cache = 0;

	xfree(assoc_hash_id);
	xfree(assoc_hash);

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

#ifndef NDEBUG
/*
 * Used to protect against double-locking within a single thread. Calling
 * assoc_mgr_lock() while already holding locks will lead to deadlock;
 * this will force such instances to abort() in development builds.
 */
/*
 * FIXME: __thread is non-standard, and may cause build failures on unusual
 * systems. Only used within development builds to mitigate possible problems
 * with production builds.
 */
static __thread bool assoc_mgr_locked = false;

/*
 * Used to detect any location where the acquired locks differ from the
 * release locks.
 */

static __thread assoc_mgr_lock_t thread_locks;

static bool _store_locks(assoc_mgr_lock_t *lock_levels)
{
	if (assoc_mgr_locked)
		return false;
	assoc_mgr_locked = true;

        memcpy((void *) &thread_locks, (void *) lock_levels,
               sizeof(assoc_mgr_lock_t));

        return true;
}

static bool _clear_locks(assoc_mgr_lock_t *lock_levels)
{
	if (!assoc_mgr_locked)
		return false;
	assoc_mgr_locked = false;

	if (memcmp((void *) &thread_locks, (void *) lock_levels,
		       sizeof(assoc_mgr_lock_t)))
		return false;

	memset((void *) &thread_locks, 0, sizeof(assoc_mgr_lock_t));

	return true;
}

bool verify_assoc_lock(assoc_mgr_lock_datatype_t datatype, lock_level_t level)
{
	return (((lock_level_t *) &thread_locks)[datatype] >= level);
}
#endif

extern void assoc_mgr_lock(assoc_mgr_lock_t *locks)
{
	xassert(_store_locks(locks));

	if (locks->assoc == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[ASSOC_LOCK]);
	else if (locks->assoc == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[ASSOC_LOCK]);

	if (locks->file == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[FILE_LOCK]);
	else if (locks->file == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[FILE_LOCK]);

	if (locks->qos == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[QOS_LOCK]);
	else if (locks->qos == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[QOS_LOCK]);

	if (locks->res == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[RES_LOCK]);
	else if (locks->res == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[RES_LOCK]);

	if (locks->tres == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[TRES_LOCK]);
	else if (locks->tres == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[TRES_LOCK]);

	if (locks->user == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[USER_LOCK]);
	else if (locks->user == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[USER_LOCK]);

	if (locks->wckey == READ_LOCK)
		slurm_rwlock_rdlock(&assoc_mgr_locks[WCKEY_LOCK]);
	else if (locks->wckey == WRITE_LOCK)
		slurm_rwlock_wrlock(&assoc_mgr_locks[WCKEY_LOCK]);
}

extern void assoc_mgr_unlock(assoc_mgr_lock_t *locks)
{
	xassert(_clear_locks(locks));

	if (locks->wckey)
		slurm_rwlock_unlock(&assoc_mgr_locks[WCKEY_LOCK]);

	if (locks->user)
		slurm_rwlock_unlock(&assoc_mgr_locks[USER_LOCK]);

	if (locks->tres)
		slurm_rwlock_unlock(&assoc_mgr_locks[TRES_LOCK]);

	if (locks->res)
		slurm_rwlock_unlock(&assoc_mgr_locks[RES_LOCK]);

	if (locks->qos)
		slurm_rwlock_unlock(&assoc_mgr_locks[QOS_LOCK]);

	if (locks->file)
		slurm_rwlock_unlock(&assoc_mgr_locks[FILE_LOCK]);

	if (locks->assoc)
		slurm_rwlock_unlock(&assoc_mgr_locks[ASSOC_LOCK]);
}

/* Since the returned assoc_list is full of pointers from the
 * assoc_mgr_assoc_list assoc_mgr_lock_t READ_LOCK on
 * assocs must be set before calling this function and while
 * handling it after a return.
 */
extern int assoc_mgr_get_user_assocs(void *db_conn,
				     slurmdb_assoc_rec_t *assoc,
				     int enforce,
				     List assoc_list)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *found_assoc = NULL;
	int set = 0;

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));

	xassert(assoc);
	xassert(assoc->uid != NO_VAL);
	xassert(assoc_list);

	if ((!assoc_mgr_assoc_list
	     || !list_count(assoc_mgr_assoc_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		return SLURM_SUCCESS;
	}

	xassert(assoc_mgr_assoc_list);

	itr = list_iterator_create(assoc_mgr_assoc_list);
	while ((found_assoc = list_next(itr))) {
		if (assoc->uid != found_assoc->uid) {
			debug4("not the right user %u != %u",
			       assoc->uid, found_assoc->uid);
			continue;
		}

		list_append(assoc_list, found_assoc);
		set = 1;
	}
	list_iterator_destroy(itr);

	if (!set) {
		debug("UID %u has no associations", assoc->uid);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_tres(void *db_conn,
				  slurmdb_tres_rec_t *tres,
				  int enforce,
				  slurmdb_tres_rec_t **tres_pptr,
				  bool locked)
{
	ListIterator itr;
	slurmdb_tres_rec_t *found_tres = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (tres_pptr)
		*tres_pptr = NULL;

	/* Since we might be locked we can't come in here and try to
	 * get the list since we would need the WRITE_LOCK to do that,
	 * so just return as this would only happen on a system not
	 * talking to the database.
	 */
	if (!assoc_mgr_tres_list) {
		int rc = SLURM_SUCCESS;

		if (enforce & ACCOUNTING_ENFORCE_TRES) {
			error("No TRES list available, this should never "
			      "happen when running with the database, "
			      "make sure it is configured.");
			rc = SLURM_ERROR;
		}
		return rc;
	}

	if ((!assoc_mgr_tres_list
	     || !list_count(assoc_mgr_tres_list))
	    && !(enforce & ACCOUNTING_ENFORCE_TRES))
		return SLURM_SUCCESS;

	if (!tres->id) {
		if (!tres->type ||
		    ((!xstrncasecmp(tres->type, "gres:", 5) ||
		      !xstrncasecmp(tres->type, "license:", 8))
		     && !tres->name)) {
			if (enforce & ACCOUNTING_ENFORCE_TRES) {
				error("get_assoc_id: "
				      "Not enough info to "
				      "get an association");
				return SLURM_ERROR;
			} else {
				return SLURM_SUCCESS;
			}
		}
	}
	/* info("looking for tres of (%d)%s:%s", */
	/*      tres->id, tres->type, tres->name); */
	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));

	itr = list_iterator_create(assoc_mgr_tres_list);
	while ((found_tres = list_next(itr))) {
		if (tres->id) {
			if (tres->id == found_tres->id)
				break;
		} else if ((tres->type
			    && !xstrcasecmp(tres->type, found_tres->type))
			   && ((!tres->name && !found_tres->name)
			       || ((tres->name && found_tres->name) &&
				   !xstrcasecmp(tres->name, found_tres->name))))
			break;
	}
	list_iterator_destroy(itr);

	if (!found_tres) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_TRES)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}
	debug3("found correct tres");
	if (tres_pptr)
		*tres_pptr = found_tres;

	tres->id           = found_tres->id;

	if (!tres->type)
		tres->type = found_tres->type;
	else {
		xfree(tres->type);
		tres->type = xstrdup(found_tres->type);
	}

	if (!tres->name)
		tres->name = found_tres->name;
	else {
		xfree(tres->name);
		tres->name = xstrdup(found_tres->name);
	}

	tres->count        = found_tres->count;

	if (!locked)
		assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_assoc(void *db_conn,
				   slurmdb_assoc_rec_t *assoc,
				   int enforce,
				   slurmdb_assoc_rec_t **assoc_pptr,
				   bool locked)
{
	slurmdb_assoc_rec_t * ret_assoc = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK };

	if (assoc_pptr)
		*assoc_pptr = NULL;

	/* Since we might be locked we can't come in here and try to
	 * get the list since we would need the WRITE_LOCK to do that,
	 * so just return as this would only happen on a system not
	 * talking to the database.
	 */
	if (!assoc_mgr_assoc_list) {
		int rc = SLURM_SUCCESS;

		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("No Association list available, "
			      "this should never happen");
			rc = SLURM_ERROR;
		}
		return rc;
	}

	if ((!assoc_mgr_assoc_list
	     || !list_count(assoc_mgr_assoc_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return SLURM_SUCCESS;

	if (!assoc->id) {
		if (!assoc->acct) {
			slurmdb_user_rec_t user;

			if (assoc->uid == NO_VAL) {
				if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("get_assoc_id: "
					      "Not enough info to "
					      "get an association");
					return SLURM_ERROR;
				} else {
					return SLURM_SUCCESS;
				}
			}
			memset(&user, 0, sizeof(slurmdb_user_rec_t));
			user.uid = assoc->uid;
			if (assoc_mgr_fill_in_user(db_conn, &user,
						   enforce, NULL, locked)
			    == SLURM_ERROR) {
				if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("User %d not found", assoc->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %d not found", assoc->uid);
					return SLURM_SUCCESS;
				}
			}
			assoc->user = user.name;
			if (user.default_acct)
				assoc->acct = user.default_acct;
			else {
				if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("User %s(%d) doesn't have a "
					      "default account", assoc->user,
					      assoc->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %s(%d) doesn't have a "
					       "default account", assoc->user,
					       assoc->uid);
					return SLURM_SUCCESS;
				}
			}
		}

		if (!assoc->cluster)
			assoc->cluster = assoc_mgr_cluster_name;
	}
/* 	info("looking for assoc of user=%s(%u), acct=%s, " */
/* 	     "cluster=%s, partition=%s", */
/* 	     assoc->user, assoc->uid, assoc->acct, */
/* 	     assoc->cluster, assoc->partition); */
	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));


	/* First look for the assoc with a partition and then check
	 * for the non-partition association if we don't find one.
	 */
	ret_assoc = _find_assoc_rec(assoc);
	if (!ret_assoc && assoc->partition) {
		char *part_holder = assoc->partition;
		assoc->partition = NULL;
		ret_assoc = _find_assoc_rec(assoc);
		assoc->partition = part_holder;
	}

	if (!ret_assoc) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}
	debug3("found correct association");
	if (assoc_pptr)
		*assoc_pptr = ret_assoc;

	assoc->id              = ret_assoc->id;

	if (!assoc->acct)
		assoc->acct    = ret_assoc->acct;

	if (!assoc->cluster)
		assoc->cluster = ret_assoc->cluster;

	assoc->def_qos_id = ret_assoc->def_qos_id;

	if (!assoc->grp_tres_mins)
		assoc->grp_tres_mins    = ret_assoc->grp_tres_mins;
	if (!assoc->grp_tres_run_mins)
		assoc->grp_tres_run_mins= ret_assoc->grp_tres_run_mins;
	if (!assoc->grp_tres)
		assoc->grp_tres        = ret_assoc->grp_tres;
	assoc->grp_jobs        = ret_assoc->grp_jobs;
	assoc->grp_jobs_accrue = ret_assoc->grp_jobs_accrue;
	assoc->grp_submit_jobs = ret_assoc->grp_submit_jobs;
	assoc->grp_wall        = ret_assoc->grp_wall;

	assoc->is_def          = ret_assoc->is_def;

	assoc->lft             = ret_assoc->lft;

	if (!assoc->max_tres_mins_pj)
		assoc->max_tres_mins_pj = ret_assoc->max_tres_mins_pj;
	if (!assoc->max_tres_run_mins)
		assoc->max_tres_run_mins = ret_assoc->max_tres_run_mins;
	if (!assoc->max_tres_pj)
		assoc->max_tres_pj     = ret_assoc->max_tres_pj;
	if (!assoc->max_tres_pn)
		assoc->max_tres_pn     = ret_assoc->max_tres_pn;
	assoc->max_jobs        = ret_assoc->max_jobs;
	assoc->max_jobs_accrue = ret_assoc->max_jobs_accrue;
	assoc->min_prio_thresh = ret_assoc->min_prio_thresh;
	assoc->max_submit_jobs = ret_assoc->max_submit_jobs;
	assoc->max_wall_pj     = ret_assoc->max_wall_pj;

	if (assoc->parent_acct) {
		xfree(assoc->parent_acct);
		assoc->parent_acct       = xstrdup(ret_assoc->parent_acct);
	} else
		assoc->parent_acct       = ret_assoc->parent_acct;

	assoc->parent_id                 = ret_assoc->parent_id;

	if (!assoc->partition)
		assoc->partition = ret_assoc->partition;

	if (!assoc->qos_list)
		assoc->qos_list = ret_assoc->qos_list;

	assoc->rgt              = ret_assoc->rgt;

	assoc->shares_raw       = ret_assoc->shares_raw;

	assoc->uid              = ret_assoc->uid;

	/* Don't send any usage info since we don't know if the usage
	   is really in existance here, if they really want it they can
	   use the pointer that is returned. */

	/* if (!assoc->usage->children_list) */
	/* 	assoc->usage->children_list = ret_assoc->usage->children_list; */
	/* assoc->usage->grp_used_tres   = ret_assoc->usage->grp_used_tres; */
	/* assoc->usage->grp_used_tres_run_mins  = */
	/* 	ret_assoc->usage->grp_used_tres_run_mins; */
	/* assoc->usage->grp_used_wall   = ret_assoc->usage->grp_used_wall; */

	/* assoc->usage->level_shares    = ret_assoc->usage->level_shares; */

	/* assoc->usage->parent_assoc_ptr = ret_assoc->usage->parent_assoc_ptr; */
	/* assoc->usage->shares_norm      = ret_assoc->usage->shares_norm; */
	/* assoc->usage->usage_efctv      = ret_assoc->usage->usage_efctv; */
	/* assoc->usage->usage_norm       = ret_assoc->usage->usage_norm; */
	/* assoc->usage->usage_raw        = ret_assoc->usage->usage_raw; */

	/* assoc->usage->used_jobs        = ret_assoc->usage->used_jobs; */
	/* assoc->usage->used_submit_jobs = ret_assoc->usage->used_submit_jobs; */
	/* if (assoc->usage->valid_qos) { */
	/* 	FREE_NULL_BITMAP(assoc->usage->valid_qos); */
	/* 	assoc->usage->valid_qos = bit_copy(ret_assoc->usage->valid_qos); */
	/* } else */
	/* 	assoc->usage->valid_qos = ret_assoc->usage->valid_qos; */

	if (!assoc->user)
		assoc->user = ret_assoc->user;
	if (!locked)
		assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_user(void *db_conn, slurmdb_user_rec_t *user,
				  int enforce,
				  slurmdb_user_rec_t **user_pptr,
				  bool locked)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { .user = READ_LOCK };

	if (user_pptr)
		*user_pptr = NULL;
	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(USER_LOCK, READ_LOCK));

	if ((!assoc_mgr_user_list || !list_count(assoc_mgr_user_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_user_list);
	while ((found_user = list_next(itr))) {
		if (user->uid != NO_VAL) {
			if (user->uid == found_user->uid)
				break;
		} else if (user->name
			   && !xstrcasecmp(user->name, found_user->name))
			break;
	}
	list_iterator_destroy(itr);

	if (!found_user) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("found correct user");
	if (user_pptr)
		*user_pptr = found_user;

	/* create coord_accts just in case the list does not exist */
	if (!found_user->coord_accts)
		found_user->coord_accts =
			list_create(slurmdb_destroy_coord_rec);

	user->admin_level = found_user->admin_level;
	if (!user->assoc_list)
		user->assoc_list = found_user->assoc_list;
	if (!user->coord_accts)
		user->coord_accts = found_user->coord_accts;
	if (!user->default_acct)
		user->default_acct = found_user->default_acct;
	if (!user->default_wckey)
		user->default_wckey = found_user->default_wckey;
	if (!user->name)
		user->name = found_user->name;
	user->uid = found_user->uid;
	if (!user->wckey_list)
		user->wckey_list = found_user->wckey_list;

	if (!locked)
		assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;

}

extern int assoc_mgr_fill_in_qos(void *db_conn, slurmdb_qos_rec_t *qos,
				 int enforce,
				 slurmdb_qos_rec_t **qos_pptr, bool locked)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t * found_qos = NULL;
	assoc_mgr_lock_t locks = { .qos = READ_LOCK };

	if (qos_pptr)
		*qos_pptr = NULL;

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));

	/* Since we might be locked we can't come in here and try to
	 * get the list since we would need the WRITE_LOCK to do that,
	 * so just return as this would only happen on a system not
	 * talking to the database.
	 */
	if (!assoc_mgr_qos_list) {
		int rc = SLURM_SUCCESS;

		if (enforce & ACCOUNTING_ENFORCE_QOS) {
			error("No QOS list available, "
			      "this should never happen");
			rc = SLURM_ERROR;
		}
		if (!locked)
			assoc_mgr_unlock(&locks);
		return rc;
	} else if (!list_count(assoc_mgr_qos_list)
		   && !(enforce & ACCOUNTING_ENFORCE_QOS)) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((found_qos = list_next(itr))) {
		if (qos->id == found_qos->id)
			break;
		else if (qos->name && !xstrcasecmp(qos->name, found_qos->name))
			break;
	}
	list_iterator_destroy(itr);

	if (!found_qos) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_QOS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("found correct qos");
	if (qos_pptr)
		*qos_pptr = found_qos;

	if (!qos->description)
		qos->description = found_qos->description;

	qos->id = found_qos->id;

	qos->grace_time      = found_qos->grace_time;
	if (!qos->grp_tres_mins)
		qos->grp_tres_mins    = found_qos->grp_tres_mins;
	if (!qos->grp_tres_run_mins)
		qos->grp_tres_run_mins= found_qos->grp_tres_run_mins;
	if (!qos->grp_tres)
		qos->grp_tres        = found_qos->grp_tres;
	qos->grp_jobs        = found_qos->grp_jobs;
	qos->grp_jobs_accrue = found_qos->grp_jobs_accrue;
	qos->grp_submit_jobs = found_qos->grp_submit_jobs;
	qos->grp_wall        = found_qos->grp_wall;

	if (!qos->max_tres_mins_pj)
		qos->max_tres_mins_pj = found_qos->max_tres_mins_pj;
	if (!qos->max_tres_run_mins_pa)
		qos->max_tres_run_mins_pa = found_qos->max_tres_run_mins_pa;
	if (!qos->max_tres_run_mins_pu)
		qos->max_tres_run_mins_pu = found_qos->max_tres_run_mins_pu;
	if (!qos->max_tres_pa)
		qos->max_tres_pa     = found_qos->max_tres_pa;
	if (!qos->max_tres_pj)
		qos->max_tres_pj     = found_qos->max_tres_pj;
	if (!qos->max_tres_pn)
		qos->max_tres_pn     = found_qos->max_tres_pn;
	if (!qos->max_tres_pu)
		qos->max_tres_pu     = found_qos->max_tres_pu;
	qos->max_jobs_pa     = found_qos->max_jobs_pa;
	qos->max_jobs_pu     = found_qos->max_jobs_pu;
	qos->max_jobs_accrue_pa = found_qos->max_jobs_accrue_pa;
	qos->max_jobs_accrue_pu = found_qos->max_jobs_accrue_pu;
	qos->min_prio_thresh    = found_qos->min_prio_thresh;
	qos->max_submit_jobs_pa = found_qos->max_submit_jobs_pa;
	qos->max_submit_jobs_pu = found_qos->max_submit_jobs_pu;
	qos->max_wall_pj     = found_qos->max_wall_pj;

	if (!qos->min_tres_pj)
		qos->min_tres_pj     = found_qos->min_tres_pj;

	if (!qos->name)
		qos->name = found_qos->name;

	if (qos->preempt_bitstr) {
		FREE_NULL_BITMAP(qos->preempt_bitstr);
		qos->preempt_bitstr = bit_copy(found_qos->preempt_bitstr);
	} else
		qos->preempt_bitstr = found_qos->preempt_bitstr;

	qos->preempt_mode = found_qos->preempt_mode;
	qos->priority = found_qos->priority;

	/* Don't send any usage info since we don't know if the usage
	   is really in existance here, if they really want it they can
	   use the pointer that is returned. */

	/* if (!qos->usage->acct_limit_list) */
	/* 	qos->usage->acct_limit_list = found_qos->usage->acct_limit_list; */

	/* qos->usage->grp_used_tres   = found_qos->usage->grp_used_tres; */
	/* qos->usage->grp_used_tres_run_mins  = */
	/* 	found_qos->usage->grp_used_tres_run_mins; */
	/* qos->usage->grp_used_jobs   = found_qos->usage->grp_used_jobs; */
	/* qos->usage->grp_used_submit_jobs = */
	/* 	found_qos->usage->grp_used_submit_jobs; */
	/* qos->usage->grp_used_wall   = found_qos->usage->grp_used_wall; */

	/* if (!qos->usage->job_list) */
	/* 	qos->usage->job_list = found_qos->usage->job_list; */

	/* qos->usage->norm_priority = found_qos->usage->norm_priority; */

	/* qos->usage->usage_raw = found_qos->usage->usage_raw; */

	/* if (!qos->usage->user_limit_list) */
	/* 	qos->usage->user_limit_list = found_qos->usage->user_limit_list; */
	qos->usage_factor = found_qos->usage_factor;

	if (!locked)
		assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_wckey(void *db_conn, slurmdb_wckey_rec_t *wckey,
				   int enforce,
				   slurmdb_wckey_rec_t **wckey_pptr,
				   bool locked)
{
	ListIterator itr = NULL;
	slurmdb_wckey_rec_t * found_wckey = NULL;
	slurmdb_wckey_rec_t * ret_wckey = NULL;
	assoc_mgr_lock_t locks = { .wckey = READ_LOCK };

	if (wckey_pptr)
		*wckey_pptr = NULL;
	if (!assoc_mgr_wckey_list) {
		if (_get_assoc_mgr_wckey_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if ((!assoc_mgr_wckey_list || !list_count(assoc_mgr_wckey_list))
	    && !(enforce & ACCOUNTING_ENFORCE_WCKEYS))
		return SLURM_SUCCESS;

	if (!wckey->id) {
		if (!wckey->name) {
			slurmdb_user_rec_t user;

			if (wckey->uid == NO_VAL && !wckey->user) {
				if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
					error("get_wckey_id: "
					      "Not enough info to "
					      "get an wckey");
					return SLURM_ERROR;
				} else {
					return SLURM_SUCCESS;
				}
			}
			memset(&user, 0, sizeof(slurmdb_user_rec_t));
			user.uid = wckey->uid;
			user.name = wckey->user;
			if (assoc_mgr_fill_in_user(db_conn, &user,
						   enforce, NULL, locked)
			    == SLURM_ERROR) {
				if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
					error("User %d not found", wckey->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %d not found", wckey->uid);
					return SLURM_SUCCESS;
				}
			}
			if (!wckey->user)
				wckey->user = user.name;
			if (user.default_wckey)
				wckey->name = user.default_wckey;
			else {
				if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
					error("User %s(%d) doesn't have a "
					      "default wckey", user.name,
					      user.uid);
					return SLURM_ERROR;
				} else {
					debug3("User %s(%d) doesn't have a "
					       "default wckey", user.name,
					       user.uid);
					return SLURM_SUCCESS;
				}
			}

		} else if (wckey->uid == NO_VAL && !wckey->user) {
			if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
				error("get_wckey_id: "
				      "Not enough info 2 to "
				      "get an wckey");
				return SLURM_ERROR;
			} else {
				return SLURM_SUCCESS;
			}
		}


		if (!wckey->cluster)
			wckey->cluster = assoc_mgr_cluster_name;
	}
/* 	info("looking for wckey of user=%s(%u), name=%s, " */
/* 	     "cluster=%s", */
/* 	     wckey->user, wckey->uid, wckey->name, */
/* 	     wckey->cluster); */
	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(WCKEY_LOCK, READ_LOCK));

	itr = list_iterator_create(assoc_mgr_wckey_list);
	while ((found_wckey = list_next(itr))) {
		if (wckey->id) {
			if (wckey->id == found_wckey->id) {
				ret_wckey = found_wckey;
				break;
			}
			continue;
		} else {
			if (wckey->uid != NO_VAL) {
				if (wckey->uid != found_wckey->uid) {
					debug4("not the right user %u != %u",
					       wckey->uid, found_wckey->uid);
					continue;
				}
			} else if (wckey->user &&
				   xstrcasecmp(wckey->user, found_wckey->user))
				continue;

			if (wckey->name
			    && (!found_wckey->name
				|| xstrcasecmp(wckey->name,
					       found_wckey->name))) {
				debug4("not the right name %s != %s",
				       wckey->name, found_wckey->name);
				continue;
			}

			/* only check for on the slurmdbd */
			if (!assoc_mgr_cluster_name) {
				if (!wckey->cluster) {
					error("No cluster name was given "
					      "to check against, "
					      "we need one to get a wckey.");
					continue;
				}

				if (found_wckey->cluster
				    && xstrcasecmp(wckey->cluster,
						   found_wckey->cluster)) {
					debug4("not the right cluster");
					continue;
				}
			}
		}
		ret_wckey = found_wckey;
		break;
	}
	list_iterator_destroy(itr);

	if (!ret_wckey) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_WCKEYS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}
	debug3("found correct wckey %u", ret_wckey->id);
	if (wckey_pptr)
		*wckey_pptr = ret_wckey;

	if (!wckey->cluster)
		wckey->cluster = ret_wckey->cluster;

	wckey->id = ret_wckey->id;

	if (!wckey->name)
		wckey->name = ret_wckey->name;

	wckey->uid = ret_wckey->uid;
	if (!wckey->user)
		wckey->user = ret_wckey->user;

	wckey->is_def = ret_wckey->is_def;

	if (!locked)
		assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern slurmdb_admin_level_t assoc_mgr_get_admin_level(void *db_conn,
						       uint32_t uid)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { .user = READ_LOCK };

	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return SLURMDB_ADMIN_NOTSET;

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_user_list) {
		assoc_mgr_unlock(&locks);
		return SLURMDB_ADMIN_NOTSET;
	}

	itr = list_iterator_create(assoc_mgr_user_list);
	while ((found_user = list_next(itr))) {
		if (uid == found_user->uid)
			break;
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	if (found_user)
		return found_user->admin_level;

	return SLURMDB_ADMIN_NOTSET;
}

extern bool assoc_mgr_is_user_acct_coord(void *db_conn,
					 uint32_t uid,
					 char *acct_name)
{
	ListIterator itr = NULL;
	slurmdb_coord_rec_t *acct = NULL;
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { .user = READ_LOCK };

	if (!acct_name)
		return false;

	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return false;

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_user_list) {
		assoc_mgr_unlock(&locks);
		return false;
	}

	itr = list_iterator_create(assoc_mgr_user_list);
	while ((found_user = list_next(itr))) {
		if (uid == found_user->uid)
			break;
	}
	list_iterator_destroy(itr);

	if (!found_user || !found_user->coord_accts) {
		assoc_mgr_unlock(&locks);
		return false;
	}
	itr = list_iterator_create(found_user->coord_accts);
	while ((acct = list_next(itr))) {
		if (!xstrcmp(acct_name, acct->name))
			break;
	}
	list_iterator_destroy(itr);

	if (acct) {
		assoc_mgr_unlock(&locks);
		return true;
	}
	assoc_mgr_unlock(&locks);

	return false;
}

extern void assoc_mgr_get_shares(void *db_conn,
				 uid_t uid, shares_request_msg_t *req_msg,
				 shares_response_msg_t *resp_msg)
{
	ListIterator itr = NULL;
	ListIterator user_itr = NULL;
	ListIterator acct_itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	assoc_shares_object_t *share = NULL;
	List ret_list = NULL;
	char *tmp_char = NULL;
	slurmdb_user_rec_t user;
	int is_admin=1;
	uint16_t private_data = slurm_get_private_data();
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .tres = READ_LOCK };

	xassert(resp_msg);

	if (!assoc_mgr_assoc_list || !list_count(assoc_mgr_assoc_list))
		return;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (req_msg) {
		if (req_msg->user_list && list_count(req_msg->user_list))
			user_itr = list_iterator_create(req_msg->user_list);

		if (req_msg->acct_list && list_count(req_msg->acct_list))
			acct_itr = list_iterator_create(req_msg->acct_list);
	}

	if (private_data & PRIVATE_DATA_USAGE) {
		uint32_t slurm_uid = slurm_get_slurm_user_id();
		is_admin = 0;
		/* Check permissions of the requesting user.
		 */
		if ((uid == slurm_uid || uid == 0)
		    || assoc_mgr_get_admin_level(db_conn, uid)
		    >= SLURMDB_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if (assoc_mgr_fill_in_user(
				    db_conn, &user,
				    ACCOUNTING_ENFORCE_ASSOCS, NULL, false)
			    == SLURM_ERROR) {
				debug3("User %d not found", user.uid);
				goto end_it;
			}
		}
	}

	resp_msg->assoc_shares_list = ret_list =
		list_create(slurm_destroy_assoc_shares_object);

	assoc_mgr_lock(&locks);

	resp_msg->tres_cnt = g_tres_count;

	/* DON'T FREE, since this shouldn't change while the slurmctld
	 * is running we should be ok.
	*/
	resp_msg->tres_names = assoc_mgr_tres_name_array;

	itr = list_iterator_create(assoc_mgr_assoc_list);
	while ((assoc = list_next(itr))) {
		if (user_itr && assoc->user) {
			while ((tmp_char = list_next(user_itr))) {
				if (!xstrcasecmp(tmp_char, assoc->user))
					break;
			}
			list_iterator_reset(user_itr);
			/* not correct user */
			if (!tmp_char)
				continue;
		}

		if (acct_itr) {
			while ((tmp_char = list_next(acct_itr))) {
				if (!xstrcasecmp(tmp_char, assoc->acct))
					break;
			}
			list_iterator_reset(acct_itr);
			/* not correct account */
			if (!tmp_char)
				continue;
		}

		if (private_data & PRIVATE_DATA_USAGE) {
			if (!is_admin) {
				ListIterator itr = NULL;
				slurmdb_coord_rec_t *coord = NULL;

				if (assoc->user &&
				    !xstrcmp(assoc->user, user.name))
					goto is_user;

				if (!user.coord_accts) {
					debug4("This user isn't a coord.");
					goto bad_user;
				}

				if (!assoc->acct) {
					debug("No account name given "
					      "in association.");
					goto bad_user;
				}

				itr = list_iterator_create(user.coord_accts);
				while ((coord = list_next(itr))) {
					if (!xstrcasecmp(coord->name,
							 assoc->acct))
						break;
				}
				list_iterator_destroy(itr);

				if (coord)
					goto is_user;

			bad_user:
				continue;
			}
		}
	is_user:

		share = xmalloc(sizeof(assoc_shares_object_t));
		list_append(ret_list, share);

		share->assoc_id = assoc->id;
		share->cluster = xstrdup(assoc->cluster);

		if (assoc == assoc_mgr_root_assoc)
			share->shares_raw = NO_VAL;
		else
			share->shares_raw = assoc->shares_raw;

		share->shares_norm = assoc->usage->shares_norm;
		share->usage_raw = (uint64_t)assoc->usage->usage_raw;

		share->usage_tres_raw = xmalloc(
			sizeof(long double) * g_tres_count);
		memcpy(share->usage_tres_raw,
		       assoc->usage->usage_tres_raw,
		       sizeof(long double) * g_tres_count);

		share->tres_grp_mins = xmalloc(sizeof(uint64_t) * g_tres_count);
		memcpy(share->tres_grp_mins, assoc->grp_tres_mins_ctld,
		       sizeof(uint64_t) * g_tres_count);
		share->tres_run_secs = xmalloc(sizeof(uint64_t) * g_tres_count);
		memcpy(share->tres_run_secs,
		       assoc->usage->grp_used_tres_run_secs,
		       sizeof(uint64_t) * g_tres_count);
		share->fs_factor = assoc->usage->fs_factor;
		share->level_fs = assoc->usage->level_fs;

		if (assoc->partition) {
			share->partition =  xstrdup(assoc->partition);
		} else {
			share->partition = NULL;
		}

		if (assoc->user) {
			/* We only calculate user effective usage when
			 * we need it
			 */
			if (fuzzy_equal(assoc->usage->usage_efctv, NO_VAL))
				priority_g_set_assoc_usage(assoc);

			share->name = xstrdup(assoc->user);
			share->parent = xstrdup(assoc->acct);
			share->user = 1;
		} else {
			share->name = xstrdup(assoc->acct);
			if (!assoc->parent_acct
			    && assoc->usage->parent_assoc_ptr)
				share->parent = xstrdup(
					assoc->usage->parent_assoc_ptr->acct);
			else
				share->parent = xstrdup(assoc->parent_acct);
		}
		share->usage_norm = (double)assoc->usage->usage_norm;
		share->usage_efctv = (double)assoc->usage->usage_efctv;
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);
end_it:
	if (user_itr)
		list_iterator_destroy(user_itr);
	if (acct_itr)
		list_iterator_destroy(acct_itr);

	/* The ret_list should already be sorted correctly, so no need
	   to do it again.
	*/
	return;
}

extern void assoc_mgr_info_get_pack_msg(
	char **buffer_ptr, int *buffer_size,
	assoc_mgr_info_request_msg_t *msg, uid_t uid,
	void *db_conn, uint16_t protocol_version)
{
	ListIterator itr = NULL;
	ListIterator user_itr = NULL, acct_itr = NULL, qos_itr = NULL;
	slurmdb_qos_rec_t *qos_rec = NULL;
	slurmdb_assoc_rec_t *assoc_rec = NULL;
	List ret_list = NULL, tmp_list;
	char *tmp_char = NULL;
	slurmdb_user_rec_t user, *user_rec = NULL;
	int is_admin=1;
	void *object;
	uint32_t flags = 0;

	uint16_t private_data = slurm_get_private_data();
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .res = READ_LOCK,
				   .tres = READ_LOCK, .user = READ_LOCK };
	Buf buffer;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (msg) {
		if (msg->user_list && list_count(msg->user_list))
			user_itr = list_iterator_create(msg->user_list);

		if (msg->acct_list && list_count(msg->acct_list))
			acct_itr = list_iterator_create(msg->acct_list);

		if (msg->qos_list && list_count(msg->qos_list))
			qos_itr = list_iterator_create(msg->qos_list);
		flags = msg->flags;
	}

	if (private_data & (PRIVATE_DATA_USAGE | PRIVATE_DATA_USERS)) {
		uint32_t slurm_uid = slurm_get_slurm_user_id();
		is_admin = 0;
		/* Check permissions of the requesting user.
		 */
		if ((uid == slurm_uid || uid == 0)
		    || assoc_mgr_get_admin_level(db_conn, uid)
		    >= SLURMDB_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if (assoc_mgr_fill_in_user(
				    db_conn, &user,
				    ACCOUNTING_ENFORCE_ASSOCS, NULL, false)
			    == SLURM_ERROR) {
				debug3("User %d not found", user.uid);
				goto end_it;
			}
		}
	}

	/* This is where we start to pack */
	buffer = init_buf(BUF_SIZE);

	packstr_array(assoc_mgr_tres_name_array, g_tres_count, buffer);

	ret_list = list_create(NULL);

	assoc_mgr_lock(&locks);

	if (!(flags & ASSOC_MGR_INFO_FLAG_ASSOC))
		goto no_assocs;

	itr = list_iterator_create(assoc_mgr_assoc_list);
	while ((assoc_rec = list_next(itr))) {
		if (user_itr && assoc_rec->user) {
			while ((tmp_char = list_next(user_itr))) {
				if (!xstrcasecmp(tmp_char, assoc_rec->user))
					break;
			}
			list_iterator_reset(user_itr);
			/* not correct user */
			if (!tmp_char)
				continue;
		}

		if (acct_itr) {
			while ((tmp_char = list_next(acct_itr))) {
				if (!xstrcasecmp(tmp_char, assoc_rec->acct))
					break;
			}
			list_iterator_reset(acct_itr);
			/* not correct account */
			if (!tmp_char)
				continue;
		}

		if (private_data & PRIVATE_DATA_USAGE) {
			if (!is_admin) {
				ListIterator itr = NULL;
				slurmdb_coord_rec_t *coord = NULL;

				if (assoc_rec->user &&
				    !xstrcmp(assoc_rec->user, user.name))
					goto is_user;

				if (!user.coord_accts) {
					debug4("This user isn't a coord.");
					goto bad_user;
				}

				if (!assoc_rec->acct) {
					debug("No account name given "
					      "in association.");
					goto bad_user;
				}

				itr = list_iterator_create(user.coord_accts);
				while ((coord = list_next(itr))) {
					if (!xstrcasecmp(coord->name,
							 assoc_rec->acct))
						break;
				}
				list_iterator_destroy(itr);

				if (coord)
					goto is_user;

			bad_user:
				continue;
			}
		}
	is_user:

		list_append(ret_list, assoc_rec);
	}
	list_iterator_destroy(itr);

no_assocs:

	/* pack the associations requested/allowed */
	pack32(list_count(ret_list), buffer);
	itr = list_iterator_create(ret_list);
	while ((object = list_next(itr)))
		slurmdb_pack_assoc_rec_with_usage(
			object, protocol_version, buffer);
	list_iterator_destroy(itr);
	list_flush(ret_list);

	if (!(flags & ASSOC_MGR_INFO_FLAG_QOS)) {
		tmp_list = ret_list;
		goto no_qos;
	}

	/* now filter out the qos */
	if (qos_itr) {
		while ((tmp_char = list_next(qos_itr)))
			if ((qos_rec = list_find_first(
				     assoc_mgr_qos_list,
				     slurmdb_find_qos_in_list_by_name,
				     tmp_char)))
				list_append(ret_list, qos_rec);
		tmp_list = ret_list;
	} else
		tmp_list = assoc_mgr_qos_list;

no_qos:
	/* pack the qos requested */
	if (tmp_list) {
		pack32(list_count(tmp_list), buffer);
		itr = list_iterator_create(tmp_list);
		while ((object = list_next(itr)))
			slurmdb_pack_qos_rec_with_usage(
				object, protocol_version, buffer);
		list_iterator_destroy(itr);
	} else
		pack32(0, buffer);

	if (qos_itr)
		list_flush(ret_list);

	if (!(flags & ASSOC_MGR_INFO_FLAG_USERS) || !assoc_mgr_user_list)
		goto no_users;

	/* now filter out the users */
	itr = list_iterator_create(assoc_mgr_user_list);
	while ((user_rec = list_next(itr))) {
		if (!is_admin && (private_data & PRIVATE_DATA_USERS) &&
		    xstrcasecmp(user_rec->name, user.name))
			continue;

		if (user_itr) {
			while ((tmp_char = list_next(user_itr)))
				if (!xstrcasecmp(tmp_char, user_rec->name))
					break;
			list_iterator_reset(user_itr);
			/* not correct user */
			if (!tmp_char)
				continue;
		}

		list_append(ret_list, user_rec);
	}

no_users:

	/* pack the users requested/allowed */
	pack32(list_count(ret_list), buffer);
	itr = list_iterator_create(ret_list);
	while ((object = list_next(itr)))
		slurmdb_pack_user_rec(object, protocol_version, buffer);
	list_iterator_destroy(itr);
//	list_flush(ret_list);

	FREE_NULL_LIST(ret_list);

	assoc_mgr_unlock(&locks);

	/* put the real record count in the message body header */
	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);

end_it:
	if (user_itr)
		list_iterator_destroy(user_itr);
	if (acct_itr)
		list_iterator_destroy(acct_itr);
	if (qos_itr)
		list_iterator_destroy(qos_itr);

	return;
}

extern int assoc_mgr_info_unpack_msg(
	assoc_mgr_info_msg_t **object, Buf buffer, uint16_t protocol_version)
{
	assoc_mgr_info_msg_t *object_ptr =
		xmalloc(sizeof(assoc_mgr_info_msg_t));
	void *list_object = NULL;
	uint32_t count;
	int i;

	*object = object_ptr;

	safe_unpackstr_array(&object_ptr->tres_names, &object_ptr->tres_cnt,
			     buffer);

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count) {
		object_ptr->assoc_list =
			list_create(slurmdb_destroy_assoc_rec);
		for (i = 0; i < count; i++) {
			if (slurmdb_unpack_assoc_rec_with_usage(
				    &list_object, protocol_version,
				    buffer)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->assoc_list, list_object);
		}
	}

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count) {
		object_ptr->qos_list =
			list_create(slurmdb_destroy_qos_rec);
		for (i = 0; i < count; i++) {
			if (slurmdb_unpack_qos_rec_with_usage(
				    &list_object, protocol_version, buffer)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->qos_list, list_object);
		}
	}

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count) {
		object_ptr->user_list =
			list_create(slurmdb_destroy_user_rec);
		for (i = 0; i < count; i++) {
			if (slurmdb_unpack_user_rec(
				    &list_object, protocol_version, buffer)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->user_list, list_object);
		}
	}

	return SLURM_SUCCESS;
unpack_error:
	slurm_free_assoc_mgr_info_msg(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

/*
 * assoc_mgr_update - update the association manager
 * IN update_list: updates to perform
 * RET: error code
 * NOTE: the items in update_list are not deleted
 */
extern int assoc_mgr_update(List update_list, bool locked)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	slurmdb_update_object_t *object = NULL;

	xassert(update_list);
	itr = list_iterator_create(update_list);
	while ((object = list_next(itr))) {
		if (!object->objects || !list_count(object->objects))
			continue;

		switch(object->type) {
		case SLURMDB_MODIFY_USER:
		case SLURMDB_ADD_USER:
		case SLURMDB_REMOVE_USER:
		case SLURMDB_ADD_COORD:
		case SLURMDB_REMOVE_COORD:
			rc = assoc_mgr_update_users(object, locked);
			break;
		case SLURMDB_ADD_ASSOC:
		case SLURMDB_MODIFY_ASSOC:
		case SLURMDB_REMOVE_ASSOC:
		case SLURMDB_REMOVE_ASSOC_USAGE:
			rc = assoc_mgr_update_assocs(object, locked);
			break;
		case SLURMDB_ADD_QOS:
		case SLURMDB_MODIFY_QOS:
		case SLURMDB_REMOVE_QOS:
		case SLURMDB_REMOVE_QOS_USAGE:
			rc = assoc_mgr_update_qos(object, locked);
			break;
		case SLURMDB_ADD_WCKEY:
		case SLURMDB_MODIFY_WCKEY:
		case SLURMDB_REMOVE_WCKEY:
			rc = assoc_mgr_update_wckeys(object, locked);
			break;
		case SLURMDB_ADD_RES:
		case SLURMDB_MODIFY_RES:
		case SLURMDB_REMOVE_RES:
			rc = assoc_mgr_update_res(object, locked);
			break;
		case SLURMDB_ADD_CLUSTER:
		case SLURMDB_REMOVE_CLUSTER:
			/* These are used in the accounting_storage
			   plugins for rollback purposes, just skip here.
			*/
			break;
		case SLURMDB_ADD_TRES:
			rc = assoc_mgr_update_tres(object, locked);
			break;
		case SLURMDB_UPDATE_FEDS:
			/* Only handled in the slurmctld. */
			break;
		case SLURMDB_UPDATE_NOTSET:
		default:
			error("unknown type set in "
			      "update_object: %d",
			      object->type);
			break;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

extern int assoc_mgr_update_assocs(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_assoc_rec_t * rec = NULL;
	slurmdb_assoc_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS, i;
	int parents_changed = 0;
	int run_update_resvs = 0;
	int resort = 0;
	List remove_list = NULL;
	List update_list = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK,
				   .tres = READ_LOCK, .user = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_assoc_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	while ((object = list_pop(update->objects))) {
		bool update_jobs = false;
		if (object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if (xstrcasecmp(object->cluster,
					assoc_mgr_cluster_name)) {
				slurmdb_destroy_assoc_rec(object);
				continue;
			}
		} else if (assoc_mgr_cluster_name) {
			error("We don't have a cluster here, no "
			      "idea if this is our association.");
			continue;
		} else if (!object->cluster) {
			/* This clause is only here for testing
			   purposes, it shouldn't really happen in
			   real senarios.
			*/
			debug("THIS SHOULD ONLY HAPPEN IN A TEST ENVIRONMENT");
			object->cluster = xstrdup("test");
		}

		rec = _find_assoc_rec(object);

		//info("%d assoc %u", update->type, object->id);
		switch(update->type) {
		case SLURMDB_MODIFY_ASSOC:
			if (!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if (object->shares_raw != NO_VAL) {
				rec->shares_raw = object->shares_raw;
				if (setup_children) {
					/* we need to update the shares on
					   each sibling and child
					   association now
					*/
					parents_changed = 1;
				}
			}

			if (object->grp_tres) {
				update_jobs = true;
				/* If we have a blank string that
				 * means it is cleared.
				 */
				xfree(rec->grp_tres);
				if (object->grp_tres[0]) {
					rec->grp_tres = object->grp_tres;
					object->grp_tres = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_ctld,
					rec->grp_tres, INFINITE64, 1);
			}

			if (object->grp_tres_mins) {
				xfree(rec->grp_tres_mins);
				if (object->grp_tres_mins[0]) {
					rec->grp_tres_mins =
						object->grp_tres_mins;
					object->grp_tres_mins = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_mins_ctld,
					rec->grp_tres_mins, INFINITE64, 1);
			}

			if (object->grp_tres_run_mins) {
				xfree(rec->grp_tres_run_mins);
				if (object->grp_tres_run_mins[0]) {
					rec->grp_tres_run_mins =
						object->grp_tres_run_mins;
					object->grp_tres_run_mins = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_run_mins_ctld,
					rec->grp_tres_run_mins, INFINITE64, 1);
			}

			if (object->grp_jobs != NO_VAL)
				rec->grp_jobs = object->grp_jobs;
			if (object->grp_jobs_accrue != NO_VAL)
				rec->grp_jobs_accrue = object->grp_jobs_accrue;
			if (object->grp_submit_jobs != NO_VAL)
				rec->grp_submit_jobs = object->grp_submit_jobs;
			if (object->grp_wall != NO_VAL) {
				update_jobs = true;
				rec->grp_wall = object->grp_wall;
			}

			if (object->lft != NO_VAL) {
				rec->lft = object->lft;
				resort = 1;
			}

			if (object->max_tres_pj) {
				update_jobs = true;
				xfree(rec->max_tres_pj);
				if (object->max_tres_pj[0]) {
					rec->max_tres_pj = object->max_tres_pj;
					object->max_tres_pj = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_ctld,
					rec->max_tres_pj, INFINITE64, 1);
			}

			if (object->max_tres_pn) {
				update_jobs = true;
				xfree(rec->max_tres_pn);
				if (object->max_tres_pn[0]) {
					rec->max_tres_pn = object->max_tres_pn;
					object->max_tres_pn = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_pn_ctld,
					rec->max_tres_pn, INFINITE64, 1);
			}

			if (object->max_tres_mins_pj) {
				xfree(rec->max_tres_mins_pj);
				if (object->max_tres_mins_pj[0]) {
					rec->max_tres_mins_pj =
						object->max_tres_mins_pj;
					object->max_tres_mins_pj = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_mins_ctld,
					rec->max_tres_mins_pj, INFINITE64, 1);
			}

			if (object->max_tres_run_mins) {
				xfree(rec->max_tres_run_mins);
				if (object->max_tres_run_mins[0]) {
					rec->max_tres_run_mins =
						object->max_tres_run_mins;
					object->max_tres_run_mins = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_run_mins_ctld,
					rec->max_tres_run_mins, INFINITE64, 1);
			}

			if (object->max_jobs != NO_VAL)
				rec->max_jobs = object->max_jobs;
			if (object->max_jobs_accrue != NO_VAL)
				rec->max_jobs_accrue = object->max_jobs_accrue;
			if (object->min_prio_thresh != NO_VAL)
				rec->min_prio_thresh = object->min_prio_thresh;
			if (object->max_submit_jobs != NO_VAL)
				rec->max_submit_jobs = object->max_submit_jobs;
			if (object->max_wall_pj != NO_VAL) {
				update_jobs = true;
				rec->max_wall_pj = object->max_wall_pj;
			}

			if (object->parent_acct) {
				xfree(rec->parent_acct);
				rec->parent_acct = xstrdup(object->parent_acct);
			}
			if (object->parent_id) {
				rec->parent_id = object->parent_id;
				// after all new parents have been set we will
				// reset the parent pointers below
				parents_changed = 1;
			}

			if (object->qos_list) {
				if (rec->qos_list) {
					_local_update_assoc_qos_list(
						rec, object->qos_list);
				} else {
					rec->qos_list = object->qos_list;
					object->qos_list = NULL;
				}

				if (rec->user && (g_qos_count > 0)) {
					if (!rec->usage->valid_qos
					    || (bit_size(rec->usage->valid_qos)
						!= g_qos_count)) {
						FREE_NULL_BITMAP(
							rec->usage->valid_qos);
						rec->usage->valid_qos =
							bit_alloc(g_qos_count);
					} else
						bit_nclear(rec->usage->
							   valid_qos, 0,
							   (bit_size(rec->
								     usage->
								     valid_qos)
							    - 1));
					set_qos_bitstr_from_list(
						rec->usage->valid_qos,
						rec->qos_list);
				}
			}

			/* info("rec has def of %d %d", */
			/*      rec->def_qos_id, object->def_qos_id); */
			if (object->def_qos_id != NO_VAL &&
			    object->def_qos_id >= g_qos_count) {
				error("qos %d doesn't exist", rec->def_qos_id);
				rec->def_qos_id = 0;
			} else  if (object->def_qos_id != NO_VAL)
				rec->def_qos_id = object->def_qos_id;

			if (rec->def_qos_id && rec->user
			    && rec->usage && rec->usage->valid_qos
			    && !bit_test(rec->usage->valid_qos,
					 rec->def_qos_id)) {
				error("assoc %u doesn't have access "
				      "to it's default qos '%s'",
				      rec->id,
				      slurmdb_qos_str(assoc_mgr_qos_list,
						      rec->def_qos_id));
				rec->def_qos_id = 0;
			}

			if (object->is_def != NO_VAL16) {
				rec->is_def = object->is_def;
				/* parents_changed will set this later
				   so try to avoid doing it twice.
				*/
				if (rec->is_def && !parents_changed)
					_set_user_default_acct(rec);
			}

			/* info("now rec has def of %d", rec->def_qos_id); */

			if (update_jobs && init_setup.update_assoc_notify) {
				/* since there are some deadlock
				   issues while inside our lock here
				   we have to process a notify later
				*/
				if (!update_list)
					update_list = list_create(NULL);
				list_append(update_list, rec);
			}

			if (!slurmdbd_conf && !parents_changed) {
				debug("updating assoc %u", rec->id);
				log_assoc_rec(rec, assoc_mgr_qos_list);
			}
			break;
		case SLURMDB_ADD_ASSOC:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}

			if (!object->usage)
				object->usage =
					slurmdb_create_assoc_usage(
						g_tres_count);
			/* If is_def is uninitialized the value will
			   be NO_VAL, so if it isn't 1 make it 0.
			*/
			if (object->is_def != 1)
				object->is_def = 0;

			/* Set something so we know to add it to the hash */
			object->uid = INFINITE;

			assoc_mgr_set_assoc_tres_cnt(object);

			list_append(assoc_mgr_assoc_list, object);

			object = NULL;
			parents_changed = 1; /* set since we need to
						set the parent
					     */
			run_update_resvs = 1; /* needed for updating
						 reservations */
			break;
		case SLURMDB_REMOVE_ASSOC:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}

			run_update_resvs = 1; /* needed for updating
						 reservations */

			if (setup_children)
				parents_changed = 1; /* set since we need to
							set the shares
							of surrounding children
						     */

			_delete_assoc_hash(rec);
			_remove_from_assoc_list(rec);
			if (init_setup.remove_assoc_notify) {
				/* since there are some deadlock
				   issues while inside our lock here
				   we have to process a notify later
				*/
				if (!remove_list)
					remove_list = list_create(
						slurmdb_destroy_assoc_rec);
				list_append(remove_list, rec);
			} else
				slurmdb_destroy_assoc_rec(rec);
			break;
		case SLURMDB_REMOVE_ASSOC_USAGE:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			assoc_mgr_remove_assoc_usage(rec);
			break;
		default:
			break;
		}

		slurmdb_destroy_assoc_rec(object);
	}

	/* We have to do this after the entire list is processed since
	 * we may have added the parent which wasn't in the list before
	 */
	if (parents_changed) {
		int reset = 1;
		g_user_assoc_count = 0;
		slurmdb_sort_hierarchical_assoc_list(
			assoc_mgr_assoc_list, true);

		itr = list_iterator_create(assoc_mgr_assoc_list);
		/* flush the children lists */
		if (setup_children) {
			while ((object = list_next(itr))) {
				if (object->usage->children_list)
					list_flush(object->usage->
						   children_list);
			}
			list_iterator_reset(itr);
		}
		while ((object = list_next(itr))) {
			bool addit = false;
			/* reset the limits because since a parent
			   changed we could have different usage
			*/
			if (!object->user) {
				_clear_used_assoc_info(object);
				object->usage->usage_raw = 0;
				for (i=0; i<object->usage->tres_cnt; i++)
					object->usage->usage_tres_raw[i] = 0;
				object->usage->grp_used_wall = 0;
			}

			/* This means we were just added, so we need
			   to be added to the hash after the uid is set.
			*/
			if (object->uid == INFINITE)
				addit = true;
			/* _set_assoc_parent_and_user() may change the uid if
			 * unset which changes the hash value. */
			if (object->user &&
			    (object->uid == NO_VAL || object->uid == 0)) {
				_delete_assoc_hash(object);
				addit = true;
			}

			_set_assoc_parent_and_user(object, reset);

			if (addit)
				_add_assoc_hash(object);
			reset = 0;
		}
		/* Now that we have set up the parents correctly we
		   can update the used limits
		*/
		list_iterator_reset(itr);
		while ((object = list_next(itr))) {
			if (setup_children) {
				List children = object->usage->children_list;
				if (!children || list_is_empty(children))
					goto is_user;

				_set_children_level_shares(
					object,
					_get_children_level_shares(object));
			}
		is_user:
			if (!object->user)
				continue;

			rec = object;
			/* look for a parent since we are starting at
			   the parent instead of the child
			*/
			while (object->usage->parent_assoc_ptr) {
				/* we need to get the parent first
				   here since we start at the child
				*/
				object = object->usage->parent_assoc_ptr;

				_addto_used_info(object, rec);
			}
		}
		if (setup_children) {
			/* Now normalize the static shares */
			list_iterator_reset(itr);
			while ((object = list_next(itr))) {
				assoc_mgr_normalize_assoc_shares(object);
				log_assoc_rec(object, assoc_mgr_qos_list);
			}
		}
		list_iterator_destroy(itr);
	} else if (resort)
		slurmdb_sort_hierarchical_assoc_list(
			assoc_mgr_assoc_list, true);

	if (!locked)
		assoc_mgr_unlock(&locks);

	/* This needs to happen outside of the
	   assoc_mgr_lock */
	if (remove_list) {
		itr = list_iterator_create(remove_list);
		while ((rec = list_next(itr)))
			init_setup.remove_assoc_notify(rec);
		list_iterator_destroy(itr);
		FREE_NULL_LIST(remove_list);
	}

	if (update_list) {
		itr = list_iterator_create(update_list);
		while ((rec = list_next(itr)))
			init_setup.update_assoc_notify(rec);
		list_iterator_destroy(itr);
		FREE_NULL_LIST(update_list);
	}

	if (run_update_resvs && init_setup.update_resvs)
		init_setup.update_resvs();

	return rc;
}

extern int assoc_mgr_update_wckeys(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_wckey_rec_t * rec = NULL;
	slurmdb_wckey_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK, .wckey = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_wckey_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_wckey_list);
	while ((object = list_pop(update->objects))) {
		if (object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if (xstrcasecmp(object->cluster,
					assoc_mgr_cluster_name)) {
				slurmdb_destroy_wckey_rec(object);
				continue;
			}
		} else if (assoc_mgr_cluster_name) {
			error("We don't have a cluster here, no "
			      "idea if this is our wckey.");
			continue;
		}

		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			if (object->id) {
				if (object->id == rec->id) {
					break;
				}
				continue;
			} else {
				if (object->uid != rec->uid) {
					debug4("not the right user");
					continue;
				}

				if (object->name
				    && (!rec->name
					|| xstrcasecmp(object->name,
						       rec->name))) {
					debug4("not the right wckey");
					continue;
				}

				/* only check for on the slurmdbd */
				if (!assoc_mgr_cluster_name && object->cluster
				    && (!rec->cluster
					|| xstrcasecmp(object->cluster,
						       rec->cluster))) {
					debug4("not the right cluster");
					continue;
				}
				break;
			}
		}
		//info("%d WCKEY %u", update->type, object->id);
		switch(update->type) {
		case SLURMDB_MODIFY_WCKEY:
			if (!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if (object->is_def != NO_VAL16) {
				rec->is_def = object->is_def;
				if (rec->is_def)
					_set_user_default_wckey(rec);
			}

			break;
		case SLURMDB_ADD_WCKEY:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (uid_from_string (object->user, &pw_uid) < 0) {
				debug("wckey add couldn't get a uid "
				      "for user %s",
				      object->user);
				object->uid = NO_VAL;
			} else
				object->uid = pw_uid;

			/* If is_def is uninitialized the value will
			   be NO_VAL, so if it isn't 1 make it 0.
			*/
			if (object->is_def == 1)
				_set_user_default_wckey(object);
			else
				object->is_def = 0;
			list_append(assoc_mgr_wckey_list, object);
			object = NULL;
			break;
		case SLURMDB_REMOVE_WCKEY:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		default:
			break;
		}

		slurmdb_destroy_wckey_rec(object);
	}
	list_iterator_destroy(itr);
	if (!locked)
		assoc_mgr_unlock(&locks);

	return rc;
}

extern int assoc_mgr_update_users(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_user_rec_t * rec = NULL;
	slurmdb_user_rec_t * object = NULL;

	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .user = WRITE_LOCK,
				   .wckey = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_user_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_user_list);
	while ((object = list_pop(update->objects))) {
		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			char *name;
			if (object->old_name)
				name = object->old_name;
			else
				name = object->name;
			if (!xstrcasecmp(name, rec->name))
				break;
		}

		//info("%d user %s", update->type, object->name);
		switch(update->type) {
		case SLURMDB_MODIFY_USER:
			if (!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if (object->old_name) {
				if (!object->name) {
					error("Tried to alter user %s's name "
					      "without giving a new one.",
					      rec->name);
					break;
				}
				xfree(rec->old_name);
				rec->old_name = rec->name;
				rec->name = object->name;
				object->name = NULL;
				rc = _change_user_name(rec);
			}

			if (object->default_acct) {
				xfree(rec->default_acct);
				rec->default_acct = object->default_acct;
				object->default_acct = NULL;
			}

			if (object->default_wckey) {
				xfree(rec->default_wckey);
				rec->default_wckey = object->default_wckey;
				object->default_wckey = NULL;
			}

			if (object->admin_level != SLURMDB_ADMIN_NOTSET)
				rec->admin_level = object->admin_level;

			break;
		case SLURMDB_ADD_USER:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (uid_from_string (object->name, &pw_uid) < 0) {
				debug("user add couldn't get a uid for user %s",
				      object->name);
				object->uid = NO_VAL;
			} else
				object->uid = pw_uid;
			list_append(assoc_mgr_user_list, object);
			object = NULL;
			break;
		case SLURMDB_REMOVE_USER:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		case SLURMDB_ADD_COORD:
			/* same as SLURMDB_REMOVE_COORD */
		case SLURMDB_REMOVE_COORD:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			/* We always get a complete list here */
			if (!object->coord_accts) {
				if (rec->coord_accts)
					list_flush(rec->coord_accts);
			} else {
				FREE_NULL_LIST(rec->coord_accts);
				rec->coord_accts = object->coord_accts;
				object->coord_accts = NULL;
			}
			break;
		default:
			break;
		}

		slurmdb_destroy_user_rec(object);
	}
	list_iterator_destroy(itr);
	if (!locked)
		assoc_mgr_unlock(&locks);

	return rc;
}

extern int assoc_mgr_update_qos(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_qos_rec_t *rec = NULL;
	slurmdb_qos_rec_t *object = NULL;

	ListIterator itr = NULL, assoc_itr = NULL;

	slurmdb_assoc_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;
	bool resize_qos_bitstr = 0;
	int redo_priority = 0;
	List remove_list = NULL;
	List update_list = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_qos_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((object = list_pop(update->objects))) {
		bool update_jobs = false;
		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			if (object->id == rec->id) {
				break;
			}
		}

		//info("%d qos %s", update->type, object->name);
		switch(update->type) {
		case SLURMDB_ADD_QOS:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}

			if (!object->usage)
				object->usage = slurmdb_create_qos_usage(
					g_tres_count);

			assoc_mgr_set_qos_tres_cnt(object);

			list_append(assoc_mgr_qos_list, object);
/* 			char *tmp = get_qos_complete_str_bitstr( */
/* 				assoc_mgr_qos_list, */
/* 				object->preempt_bitstr); */

/* 			info("new qos %s(%d) now preempts %s",  */
/* 			     object->name, object->id, tmp); */
/* 			xfree(tmp); */

			/* Since in the database id's don't start at 1
			   instead of 0 we need to ignore the 0 bit and start
			   with 1 so increase the count by 1.
			*/
			if (object->id+1 > g_qos_count) {
				resize_qos_bitstr = 1;
				g_qos_count = object->id+1;
			}

			if (object->priority > g_qos_max_priority) {
				g_qos_max_priority = object->priority;
				redo_priority = 1;
			} else
				_set_qos_norm_priority(object);

			object = NULL;
			break;
		case SLURMDB_MODIFY_QOS:
			if (!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if (!(object->flags & QOS_FLAG_NOTSET)) {
				if (object->flags & QOS_FLAG_ADD) {
					rec->flags |= object->flags;
					rec->flags &= (~QOS_FLAG_ADD);
				} else if (object->flags & QOS_FLAG_REMOVE) {
					rec->flags &= ~object->flags;
					rec->flags &= (~QOS_FLAG_REMOVE);
				} else
					rec->flags = object->flags;
			}

			if (object->grace_time != NO_VAL)
				rec->grace_time = object->grace_time;

			if (object->grp_tres) {
				update_jobs = true;
				/* If we have a blank string that
				 * means it is cleared.
				 */
				xfree(rec->grp_tres);
				if (object->grp_tres[0]) {
					rec->grp_tres = object->grp_tres;
					object->grp_tres = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_ctld, rec->grp_tres,
					INFINITE64, 1);

			}

			if (object->grp_tres_mins) {
				xfree(rec->grp_tres_mins);
				if (object->grp_tres_mins[0]) {
					rec->grp_tres_mins =
						object->grp_tres_mins;
					object->grp_tres_mins = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_mins_ctld,
					rec->grp_tres_mins, INFINITE64, 1);
			}

			if (object->grp_tres_run_mins) {
				xfree(rec->grp_tres_run_mins);
				if (object->grp_tres_run_mins[0]) {
					rec->grp_tres_run_mins =
						object->grp_tres_run_mins;
					object->grp_tres_run_mins = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->grp_tres_run_mins_ctld,
					rec->grp_tres_run_mins, INFINITE64, 1);
			}

			if (object->grp_jobs != NO_VAL)
				rec->grp_jobs = object->grp_jobs;
			if (object->grp_jobs_accrue != NO_VAL)
				rec->grp_jobs_accrue = object->grp_jobs_accrue;
			if (object->grp_submit_jobs != NO_VAL)
				rec->grp_submit_jobs = object->grp_submit_jobs;
			if (object->grp_wall != NO_VAL) {
				update_jobs = true;
				rec->grp_wall = object->grp_wall;
			}

			if (object->max_tres_pa) {
				update_jobs = true;
				xfree(rec->max_tres_pa);
				if (object->max_tres_pa[0]) {
					rec->max_tres_pa = object->max_tres_pa;
					object->max_tres_pa = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_pa_ctld,
					rec->max_tres_pa, INFINITE64, 1);
			}

			if (object->max_tres_pj) {
				update_jobs = true;
				xfree(rec->max_tres_pj);
				if (object->max_tres_pj[0]) {
					rec->max_tres_pj = object->max_tres_pj;
					object->max_tres_pj = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_pj_ctld,
					rec->max_tres_pj, INFINITE64, 1);
			}

			if (object->max_tres_pn) {
				update_jobs = true;
				xfree(rec->max_tres_pn);
				if (object->max_tres_pn[0]) {
					rec->max_tres_pn = object->max_tres_pn;
					object->max_tres_pn = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_pn_ctld,
					rec->max_tres_pn, INFINITE64, 1);
			}

			if (object->max_tres_pu) {
				update_jobs = true;
				xfree(rec->max_tres_pu);
				if (object->max_tres_pu[0]) {
					rec->max_tres_pu = object->max_tres_pu;
					object->max_tres_pu = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_pu_ctld,
					rec->max_tres_pu, INFINITE64, 1);
			}

			if (object->max_tres_mins_pj) {
				xfree(rec->max_tres_mins_pj);
				if (object->max_tres_mins_pj[0]) {
					rec->max_tres_mins_pj =
						object->max_tres_mins_pj;
					object->max_tres_mins_pj = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_mins_pj_ctld,
					rec->max_tres_mins_pj, INFINITE64, 1);
			}

			if (object->max_tres_run_mins_pa) {
				xfree(rec->max_tres_run_mins_pa);
				if (object->max_tres_run_mins_pa[0]) {
					rec->max_tres_run_mins_pa =
						object->max_tres_run_mins_pa;
					object->max_tres_run_mins_pa = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_run_mins_pa_ctld,
					rec->max_tres_run_mins_pa,
					INFINITE64, 1);
			}

			if (object->max_tres_run_mins_pu) {
				xfree(rec->max_tres_run_mins_pu);
				if (object->max_tres_run_mins_pu[0]) {
					rec->max_tres_run_mins_pu =
						object->max_tres_run_mins_pu;
					object->max_tres_run_mins_pu = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->max_tres_run_mins_pu_ctld,
					rec->max_tres_run_mins_pu,
					INFINITE64, 1);
			}

			if (object->max_jobs_pa != NO_VAL)
				rec->max_jobs_pa = object->max_jobs_pa;

			if (object->max_jobs_pu != NO_VAL)
				rec->max_jobs_pu = object->max_jobs_pu;

			if (object->max_jobs_accrue_pa != NO_VAL)
				rec->max_jobs_accrue_pa =
					object->max_jobs_accrue_pa;

			if (object->max_jobs_accrue_pu != NO_VAL)
				rec->max_jobs_accrue_pu =
					object->max_jobs_accrue_pu;

			if (object->min_prio_thresh != NO_VAL)
				rec->min_prio_thresh = object->min_prio_thresh;
			if (object->max_submit_jobs_pa != NO_VAL)
				rec->max_submit_jobs_pa =
					object->max_submit_jobs_pa;

			if (object->max_submit_jobs_pu != NO_VAL)
				rec->max_submit_jobs_pu =
					object->max_submit_jobs_pu;

			if (object->max_wall_pj != NO_VAL) {
				update_jobs = true;
				rec->max_wall_pj = object->max_wall_pj;
			}

			if (object->min_tres_pj) {
				xfree(rec->min_tres_pj);
				if (object->min_tres_pj[0]) {
					rec->min_tres_pj = object->min_tres_pj;
					object->min_tres_pj = NULL;
				}
				assoc_mgr_set_tres_cnt_array(
					&rec->min_tres_pj_ctld,
					rec->min_tres_pj, INFINITE64, 1);
			}

			if (object->preempt_bitstr) {
				if (rec->preempt_bitstr)
					FREE_NULL_BITMAP(rec->preempt_bitstr);

				rec->preempt_bitstr = object->preempt_bitstr;
				object->preempt_bitstr = NULL;
				/* char *tmp = get_qos_complete_str_bitstr( */
/* 					assoc_mgr_qos_list, */
/* 					rec->preempt_bitstr); */

/* 				info("qos %s(%d) now preempts %s",  */
/* 				     rec->name, rec->id, tmp); */
/* 				xfree(tmp); */
			}

			if (object->preempt_mode != NO_VAL16)
				rec->preempt_mode = object->preempt_mode;

			if (object->priority != NO_VAL) {
				if (rec->priority == g_qos_max_priority)
					redo_priority = 2;

				rec->priority = object->priority;

				if (rec->priority > g_qos_max_priority) {
					g_qos_max_priority = rec->priority;
					redo_priority = 1;
				} else if (redo_priority != 2)
					_set_qos_norm_priority(rec);
			}

			if (!fuzzy_equal(object->usage_factor, NO_VAL))
				rec->usage_factor = object->usage_factor;

			if (!fuzzy_equal(object->usage_thres, NO_VAL))
				rec->usage_thres = object->usage_thres;

			if (update_jobs && init_setup.update_qos_notify) {
				/* since there are some deadlock
				   issues while inside our lock here
				   we have to process a notify later
				*/
				if (!update_list)
					update_list = list_create(NULL);
				list_append(update_list, rec);
			}

			break;
		case SLURMDB_REMOVE_QOS:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}

			/* We need to renormalize of something else */
			if (rec->priority == g_qos_max_priority)
				redo_priority = 2;

			if (init_setup.remove_qos_notify) {
				/* since there are some deadlock
				   issues while inside our lock here
				   we have to process a notify later
				*/
				if (!remove_list)
					remove_list = list_create(
						slurmdb_destroy_qos_rec);
				list_remove(itr);
				list_append(remove_list, rec);
			} else
				list_delete_item(itr);

			if (!assoc_mgr_assoc_list)
				break;
			/* Remove this qos from all the associations
			   on this cluster.
			*/
			assoc_itr = list_iterator_create(
				assoc_mgr_assoc_list);
			while ((assoc = list_next(assoc_itr))) {

				if (assoc->def_qos_id == object->id)
					assoc->def_qos_id = 0;

				if (!assoc->usage->valid_qos)
					continue;

				if (bit_size(assoc->usage->valid_qos)
				    > object->id)
					bit_clear(assoc->usage->valid_qos,
						  object->id);
			}
			list_iterator_destroy(assoc_itr);

			break;
		case SLURMDB_REMOVE_QOS_USAGE:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			assoc_mgr_remove_qos_usage(rec);
			break;
		default:
			break;
		}
		slurmdb_destroy_qos_rec(object);
	}

	if (resize_qos_bitstr) {
		/* we need to resize all bitstring's that represent
		   qos' */
		list_iterator_reset(itr);
		while ((object = list_next(itr))) {
			if (!object->preempt_bitstr)
				continue;

			object->preempt_bitstr =
				bit_realloc(object->preempt_bitstr,
					    g_qos_count);
		}
		if (assoc_mgr_assoc_list) {
			assoc_itr = list_iterator_create(
				assoc_mgr_assoc_list);
			while ((assoc = list_next(assoc_itr))) {
				if (!assoc->usage->valid_qos)
					continue;
				assoc->usage->valid_qos =
					bit_realloc(assoc->usage->valid_qos,
						    g_qos_count);
			}
			list_iterator_destroy(assoc_itr);
		}
	}

	if (redo_priority == 1) {
		list_iterator_reset(itr);
		while ((object = list_next(itr)))
			_set_qos_norm_priority(object);
	} else if (redo_priority == 2)
		_post_qos_list(assoc_mgr_qos_list);

	list_iterator_destroy(itr);

	if (!locked)
		assoc_mgr_unlock(&locks);

	/* This needs to happen outside of the
	   assoc_mgr_lock */
	if (remove_list) {
		itr = list_iterator_create(remove_list);
		while ((rec = list_next(itr)))
			init_setup.remove_qos_notify(rec);
		list_iterator_destroy(itr);
		FREE_NULL_LIST(remove_list);
	}

	if (update_list) {
		itr = list_iterator_create(update_list);
		while ((rec = list_next(itr)))
			init_setup.update_qos_notify(rec);
		list_iterator_destroy(itr);
		FREE_NULL_LIST(update_list);
	}

	if (resize_qos_bitstr && init_setup.resize_qos_notify)
		init_setup.resize_qos_notify();

	return rc;
}

extern int assoc_mgr_update_res(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_res_rec_t *rec = NULL;
	slurmdb_res_rec_t *object = NULL;

	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	assoc_mgr_lock_t locks = { .res = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_res_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_res_list);
	while ((object = list_pop(update->objects))) {
		/* If this doesn't already have a clus_res_rec and no
		   clus_res_list then the resource it self changed so
		   update counts.
		*/
		if (assoc_mgr_cluster_name && object->clus_res_rec) {
			if (!object->clus_res_rec->cluster) {
				error("Resource doesn't have a cluster name?");
				slurmdb_destroy_res_rec(object);
				continue;
			} else if (xstrcmp(object->clus_res_rec->cluster,
					   assoc_mgr_cluster_name)) {
				debug("Not for our cluster for '%s'",
				      object->clus_res_rec->cluster);
				slurmdb_destroy_res_rec(object);
				continue;
			}
		}

		/* just get rid of clus_res_list if it exists (we only
		   look at objects with clus_res_rec or none
		*/
		FREE_NULL_LIST(object->clus_res_list);

		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			if (object->id == rec->id)
				break;
		}
		switch(update->type) {
		case SLURMDB_ADD_RES:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (!object->clus_res_rec) {
				error("trying to add resource without a "
				      "clus_res_rec!  This should never "
				      "happen.");
				break;
			}
			list_append(assoc_mgr_res_list, object);
			switch (object->type) {
			case SLURMDB_RESOURCE_LICENSE:
				if (init_setup.add_license_notify)
					init_setup.add_license_notify(object);
				break;
			default:
				error("SLURMDB_ADD_RES: unknown type %d",
				      object->type);
				break;
			}
			object = NULL;
			break;
		case SLURMDB_MODIFY_RES:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (!object->clus_res_rec) {
				error("trying to Modify resource without a "
				      "clus_res_rec!  This should never "
				      "happen.");
				break;
			}

			if (!(object->flags & SLURMDB_RES_FLAG_NOTSET)) {
				uint32_t base_flags = (object->flags &
						       SLURMDB_RES_FLAG_BASE);
				if (object->flags & SLURMDB_RES_FLAG_ADD) {
					rec->flags |= base_flags;
				} else if (object->flags
					   & SLURMDB_RES_FLAG_REMOVE) {
					rec->flags &= ~base_flags;
				} else
					rec->flags = base_flags;
			}

			if (object->count != NO_VAL)
				rec->count = object->count;

			if (object->type != SLURMDB_RESOURCE_NOTSET)
				rec->type = object->type;

			if (object->clus_res_rec->percent_allowed != NO_VAL16)
				rec->clus_res_rec->percent_allowed =
					object->clus_res_rec->percent_allowed;

			switch (rec->type) {
			case SLURMDB_RESOURCE_LICENSE:
				if (init_setup.update_license_notify)
					init_setup.update_license_notify(rec);
				break;
			default:
				error("SLURMDB_MODIFY_RES: "
				      "unknown type %d",
				      rec->type);
				break;
			}
			break;
		case SLURMDB_REMOVE_RES:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			switch (rec->type) {
			case SLURMDB_RESOURCE_LICENSE:
				if (init_setup.remove_license_notify)
					init_setup.remove_license_notify(rec);
				break;
			default:
				error("SLURMDB_REMOVE_RES: "
				      "unknown type %d",
				      rec->type);
				break;
			}

			list_delete_item(itr);
			break;
		default:
			break;
		}

		slurmdb_destroy_res_rec(object);
	}
	list_iterator_destroy(itr);
	if (!locked)
		assoc_mgr_unlock(&locks);
	return rc;
}

extern int assoc_mgr_update_tres(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_tres_rec_t *rec = NULL;
	slurmdb_tres_rec_t *object = NULL;

	ListIterator itr = NULL;
	List tmp_list;
	bool changed = false, freeit = false;
	int rc = SLURM_SUCCESS;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK,
				   .tres = WRITE_LOCK };
	if (!locked)
		assoc_mgr_lock(&locks);

	if (!assoc_mgr_tres_list) {
		tmp_list = list_create(slurmdb_destroy_tres_rec);
		freeit = true;
	} else {
		/* Since assoc_mgr_tres_list gets freed later we need
		 * to swap things out to avoid memory corruption.
		 */
		tmp_list = assoc_mgr_tres_list;
		assoc_mgr_tres_list = NULL;
	}

	itr = list_iterator_create(tmp_list);
	while ((object = list_pop(update->objects))) {
		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			if (object->id == rec->id)
				break;
		}

		switch (update->type) {
		case SLURMDB_ADD_TRES:
			if (rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (!object->id) {
				error("trying to add resource without an id!  "
				      "This should never happen.");
				break;
			}
			list_append(tmp_list, object);
			object = NULL;
			changed = true;
			break;
		default:
			break;
		}

		slurmdb_destroy_tres_rec(object);
	}
	list_iterator_destroy(itr);
	if (changed) {
		/* We want to run this on the assoc_mgr_tres_list, but we need
		 * to make a tmp variable since assoc_mgr_post_tres_list will
		 * set assoc_mgr_tres_list for us.
		 */
		assoc_mgr_post_tres_list(tmp_list);
	} else if (freeit)
		FREE_NULL_LIST(tmp_list);
	else
		assoc_mgr_tres_list = tmp_list;

	if (!locked)
		assoc_mgr_unlock(&locks);
	return rc;
}

extern int assoc_mgr_validate_assoc_id(void *db_conn,
				       uint32_t assoc_id,
				       int enforce)
{
	slurmdb_assoc_rec_t * found_assoc = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK };

	/* Call assoc_mgr_refresh_lists instead of just getting the
	   association list because we need qos and user lists before
	   the association list can be made.
	*/
	if (!assoc_mgr_assoc_list)
		if (assoc_mgr_refresh_lists(db_conn, 0) == SLURM_ERROR)
			return SLURM_ERROR;

	assoc_mgr_lock(&locks);
	if ((!assoc_mgr_assoc_list
	     || !list_count(assoc_mgr_assoc_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	found_assoc = _find_assoc_rec_id(assoc_id);
	assoc_mgr_unlock(&locks);

	if (found_assoc || !(enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern void assoc_mgr_clear_used_info(void)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t * found_assoc = NULL;
	slurmdb_qos_rec_t * found_qos = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_assoc_list) {
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((found_assoc = list_next(itr))) {
			_clear_used_assoc_info(found_assoc);
		}
		list_iterator_destroy(itr);
	}

	if (assoc_mgr_qos_list) {
		itr = list_iterator_create(assoc_mgr_qos_list);
		while ((found_qos = list_next(itr))) {
			_clear_used_qos_info(found_qos);
		}
		list_iterator_destroy(itr);
	}

	assoc_mgr_unlock(&locks);
}

static void _reset_children_usages(List children_list)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	ListIterator itr = NULL;
	int i;

	if (!children_list || !list_count(children_list))
		return;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw = 0.0;
		assoc->usage->grp_used_wall = 0.0;
		for (i=0; i<assoc->usage->tres_cnt; i++)
			assoc->usage->usage_tres_raw[i] = 0;

		if (assoc->user)
			continue;

		_reset_children_usages(assoc->usage->children_list);
	}
	list_iterator_destroy(itr);
}

/* tres read lock needs to be locked before calling this. */
static char *_make_usage_tres_raw_str(long double *tres_cnt)
{
	int i;
	char *tres_str = NULL;

	if (!tres_cnt)
		return NULL;

	for (i=0; i<g_tres_count; i++) {
		if (!assoc_mgr_tres_array[i] || !tres_cnt[i])
			continue;
		xstrfmtcat(tres_str, "%s%u=%Lf", tres_str ? "," : "",
			   assoc_mgr_tres_array[i]->id, tres_cnt[i]);
	}

	return tres_str;
}

static void _set_usage_tres_raw(long double *tres_cnt, char *tres_str)
{
	char *tmp_str = tres_str;
	int pos, id;
	char *endptr;
	slurmdb_tres_rec_t tres_rec;

	xassert(tres_cnt);

	if (!tres_str || !tres_str[0])
		return;

	if (tmp_str[0] == ',')
		tmp_str++;

	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));

	while (tmp_str) {
		id = atoi(tmp_str);
		/* 0 isn't a valid tres id */
		if (id <= 0) {
			error("_set_usage_tres_raw: no id "
			      "found at %s instead", tmp_str);
			break;
		}
		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("_set_usage_tres_raw: "
			      "no value found %s", tres_str);
			break;
		}

		tres_rec.id = id;
		pos = assoc_mgr_find_tres_pos(&tres_rec, true);
		if (pos != -1) {
			/* set the index to the count */
			tres_cnt[pos] = strtold(++tmp_str, &endptr);
		} else {
			debug("_set_usage_tres_raw: "
			       "no tres of id %u found in the array",
			       tres_rec.id);
		}
		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}


	return;
}

extern void assoc_mgr_remove_assoc_usage(slurmdb_assoc_rec_t *assoc)
{
	char *child;
	char *child_str;
	long double old_usage_raw = 0.0;
	long double old_usage_tres_raw[g_tres_count];
	int i;
	double old_grp_used_wall = 0.0;
	slurmdb_assoc_rec_t *sav_assoc = assoc;

	xassert(assoc);
	xassert(assoc->usage);

	if (assoc->user) {
		child = "user";
		child_str = assoc->user;
	} else {
		child = "account";
		child_str = assoc->acct;
	}
	info("Resetting usage for %s %s", child, child_str);

	old_usage_raw = assoc->usage->usage_raw;
	/* clang needs this memset to avoid a warning */
	memset(old_usage_tres_raw, 0, sizeof(old_usage_tres_raw));
	for (i=0; i<g_tres_count; i++)
		old_usage_tres_raw[i] = assoc->usage->usage_tres_raw[i];
	old_grp_used_wall = assoc->usage->grp_used_wall;
/*
 *	Reset this association's raw and group usages and subtract its
 *	current usages from all parental units
 */
	while (assoc) {
		info("Subtracting %Lf from %Lf raw usage and %f from "
		     "%f group wall for assoc %u (user='%s' acct='%s')",
		     old_usage_raw, assoc->usage->usage_raw,
		     old_grp_used_wall, assoc->usage->grp_used_wall,
		     assoc->id, assoc->user, assoc->acct);

		assoc->usage->usage_raw -= old_usage_raw;

		for (i=0; i<g_tres_count; i++)
			assoc->usage->usage_tres_raw[i] -=
				old_usage_tres_raw[i];

		assoc->usage->grp_used_wall -= old_grp_used_wall;
		assoc = assoc->usage->parent_assoc_ptr;
	}
	if (sav_assoc->user)
		return;
/*
 *	The assoc is an account, so reset all children
 */
	_reset_children_usages(sav_assoc->usage->children_list);
}

extern void assoc_mgr_remove_qos_usage(slurmdb_qos_rec_t *qos)
{
	int i;

	xassert(qos);
	xassert(qos->usage);

	info("Resetting usage for QOS %s", qos->name);

	qos->usage->usage_raw = 0;
	qos->usage->grp_used_wall = 0;

	for (i=0; i<qos->usage->tres_cnt; i++) {
		qos->usage->usage_tres_raw[i] = 0;
		if (!qos->usage->grp_used_tres[i])
			qos->usage->grp_used_tres_run_secs[i] = 0;
	}
}

extern int dump_assoc_mgr_state(void)
{
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL,
		*tmp_char = NULL;
	dbd_list_msg_t msg;
	Buf buffer = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .file = WRITE_LOCK,
				   .qos = READ_LOCK, .res = READ_LOCK,
				   .tres = READ_LOCK, .user = READ_LOCK,
				   .wckey = READ_LOCK};
	DEF_TIMERS;

	xassert(init_setup.state_save_location &&
		*init_setup.state_save_location);

	START_TIMER;

	/* now make a file for last_tres */
	buffer = init_buf(high_buffer_size);

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	assoc_mgr_lock(&locks);
	if (assoc_mgr_tres_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_tres_list;
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_TRES, buffer);
	}

	reg_file = xstrdup_printf("%s/last_tres",
				  *init_setup.state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);

	/* Now write the rest of the lists */
	buffer = init_buf(high_buffer_size);

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_user_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_user_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_USERS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_USERS, buffer);
	}

	if (assoc_mgr_res_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_res_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_RES, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_RES, buffer);
	}

	if (assoc_mgr_qos_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_qos_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_QOS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_QOS, buffer);
	}

	if (assoc_mgr_wckey_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_wckey_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_WCKEYS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_WCKEYS, buffer);
	}
	/* this needs to be done last so qos is set up
	 * before hand when loading it back */
	if (assoc_mgr_assoc_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_assoc_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_ASSOCS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_ASSOCS, buffer);
	}

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/assoc_mgr_state",
				  *init_setup.state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);
	/* now make a file for assoc_usage */

	buffer = init_buf(high_buffer_size);
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_assoc_list) {
		ListIterator itr = NULL;
		slurmdb_assoc_rec_t *assoc = NULL;
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc = list_next(itr))) {
			if (!assoc->user)
				continue;

			pack32(assoc->id, buffer);
			packlongdouble(assoc->usage->usage_raw, buffer);
			tmp_char = _make_usage_tres_raw_str(
				assoc->usage->usage_tres_raw);
			packstr(tmp_char, buffer);
			xfree(tmp_char);
			pack32(assoc->usage->grp_used_wall, buffer);
		}
		list_iterator_destroy(itr);
	}

	reg_file = xstrdup_printf("%s/assoc_usage",
				  *init_setup.state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);
	/* now make a file for qos_usage */

	buffer = init_buf(high_buffer_size);
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_qos_list) {
		ListIterator itr = NULL;
		slurmdb_qos_rec_t *qos = NULL;
		itr = list_iterator_create(assoc_mgr_qos_list);
		while ((qos = list_next(itr))) {
			pack32(qos->id, buffer);
			packlongdouble(qos->usage->usage_raw, buffer);
			tmp_char = _make_usage_tres_raw_str(
				qos->usage->usage_tres_raw);
			packstr(tmp_char, buffer);
			xfree(tmp_char);
			pack32(qos->usage->grp_used_wall, buffer);
		}
		list_iterator_destroy(itr);
	}

	reg_file = xstrdup_printf("%s/qos_usage",
				  *init_setup.state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	assoc_mgr_unlock(&locks);

	free_buf(buffer);
	END_TIMER2("dump_assoc_mgr_state");
	return error_code;

}

extern int load_assoc_usage(void)
{
	int i;
	uint16_t ver = 0;
	char *state_file, *tmp_str = NULL;
	Buf buffer = NULL;
	time_t buf_time;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .file = READ_LOCK };

	if (!assoc_mgr_assoc_list)
		return SLURM_SUCCESS;

	xassert(init_setup.state_save_location &&
		*init_setup.state_save_location);

	/* read the file */
	state_file = xstrdup(*init_setup.state_save_location);
	xstrcat(state_file, "/assoc_usage");	/* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);

	if (!(buffer = create_mmap_buf(state_file))) {
		debug2("No Assoc usage file (%s) to recover", state_file);
		xfree(state_file);
		assoc_mgr_unlock(&locks);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_usage header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		if (!ignore_state_errors)
			fatal("Can not recover assoc_usage state, incompatible version, got %u need >= %u <= %u, start with '-i' to ignore this",
			      ver, SLURM_MIN_PROTOCOL_VERSION,
			      SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover assoc_usage state, "
		      "incompatible version, got %u need >= %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);

	while (remaining_buf(buffer) > 0) {
		uint32_t assoc_id = 0;
		uint32_t grp_used_wall = 0;
		long double usage_raw = 0;
		slurmdb_assoc_rec_t *assoc = NULL;
		uint32_t tmp32;
		long double usage_tres_raw[g_tres_count];

		safe_unpack32(&assoc_id, buffer);
		safe_unpacklongdouble(&usage_raw, buffer);
		safe_unpackstr_xmalloc(&tmp_str, &tmp32, buffer);
		safe_unpack32(&grp_used_wall, buffer);

		assoc = _find_assoc_rec_id(assoc_id);

		/* We want to do this all the way up to and including
		   root.  This way we can keep track of how much usage
		   has occured on the entire system and use that to
		   normalize against.
		*/
		if (assoc) {
			assoc->usage->grp_used_wall = 0;
			assoc->usage->usage_raw = 0;
			for (i=0; i < g_tres_count; i++)
				assoc->usage->usage_tres_raw[i] = 0;
			memset(usage_tres_raw, 0, sizeof(usage_tres_raw));
			_set_usage_tres_raw(usage_tres_raw, tmp_str);
		}
		while (assoc) {
			assoc->usage->grp_used_wall += grp_used_wall;
			assoc->usage->usage_raw += usage_raw;
			for (i=0; i < g_tres_count; i++)
				assoc->usage->usage_tres_raw[i] +=
					usage_tres_raw[i];
			assoc = assoc->usage->parent_assoc_ptr;
		}

		xfree(tmp_str);
	}
	assoc_mgr_unlock(&locks);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete assoc usage state file, start with '-i' to ignore this");
	error("Incomplete assoc usage state file");

	free_buf(buffer);

	xfree(tmp_str);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_qos_usage(void)
{
	uint16_t ver = 0;
	char *state_file, *tmp_str = NULL;
	Buf buffer = NULL;
	time_t buf_time;
	ListIterator itr = NULL;
	assoc_mgr_lock_t locks = { .file = READ_LOCK, .qos = WRITE_LOCK };

	if (!assoc_mgr_qos_list)
		return SLURM_SUCCESS;

	xassert(init_setup.state_save_location &&
		*init_setup.state_save_location);

	/* read the file */
	state_file = xstrdup(*init_setup.state_save_location);
	xstrcat(state_file, "/qos_usage");	/* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);

	if (!(buffer = create_mmap_buf(state_file))) {
		debug2("No Qos usage file (%s) to recover", state_file);
		xfree(state_file);
		assoc_mgr_unlock(&locks);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpack16(&ver, buffer);
	debug3("Version in qos_usage header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		if (!ignore_state_errors)
			fatal("Can not recover qos_usage state, incompatible version, "
			      "got %u need >= %u <= %u, start with '-i' to ignore this",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover qos_usage state, "
		      "incompatible version, got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while (remaining_buf(buffer) > 0) {
		uint32_t qos_id = 0;
		uint32_t grp_used_wall = 0;
		uint32_t tmp32;
		long double usage_raw = 0;
		slurmdb_qos_rec_t *qos = NULL;

		safe_unpack32(&qos_id, buffer);
		safe_unpacklongdouble(&usage_raw, buffer);
		safe_unpackstr_xmalloc(&tmp_str, &tmp32, buffer);
		safe_unpack32(&grp_used_wall, buffer);

		while ((qos = list_next(itr)))
			if (qos->id == qos_id)
				break;
		if (qos) {
			qos->usage->grp_used_wall = grp_used_wall;
			qos->usage->usage_raw = usage_raw;
			_set_usage_tres_raw(qos->usage->usage_tres_raw,
					    tmp_str);
		}

		xfree(tmp_str);
		list_iterator_reset(itr);
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete QOS usage state file, start with '-i' to ignore this");
	error("Incomplete QOS usage state file");

	free_buf(buffer);

	if (itr)
		list_iterator_destroy(itr);
	xfree(tmp_str);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_assoc_mgr_last_tres(void)
{
	int error_code = SLURM_SUCCESS;
	uint16_t ver = 0;
	char *state_file;
	Buf buffer = NULL;
	time_t buf_time;
	dbd_list_msg_t *msg = NULL;
	assoc_mgr_lock_t locks = { .tres = WRITE_LOCK };

	xassert(init_setup.state_save_location &&
		*init_setup.state_save_location);

	/* read the file Always ignore .old file */
	state_file = xstrdup_printf("%s/last_tres",
				    *init_setup.state_save_location);
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);

	if (!(buffer = create_mmap_buf(state_file))) {
		debug2("No last_tres file (%s) to recover", state_file);
		xfree(state_file);
		assoc_mgr_unlock(&locks);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpack16(&ver, buffer);
	debug3("Version in last_tres header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		if (!ignore_state_errors)
			fatal("Can not recover last_tres state, incompatible version, got %u need >= %u <= %u, start with '-i' to ignore this",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover last_tres state, incompatible version, got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	error_code = slurmdbd_unpack_list_msg(&msg, ver, DBD_ADD_TRES, buffer);
	if (error_code != SLURM_SUCCESS)
		goto unpack_error;
	else if (!msg->my_list) {
		error("No tres retrieved");
	} else {
		FREE_NULL_LIST(assoc_mgr_tres_list);
		assoc_mgr_post_tres_list(msg->my_list);
		/* assoc_mgr_tres_list gets set in assoc_mgr_post_tres_list */
		debug("Recovered %u tres",
		      list_count(assoc_mgr_tres_list));
		msg->my_list = NULL;
	}
	slurmdbd_free_list_msg(msg);
	assoc_mgr_unlock(&locks);
	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete last_tres state file, start with '-i' to ignore this");
	error("Incomplete last_tres state file");

	free_buf(buffer);

	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_assoc_mgr_state(bool only_tres)
{
	int error_code = SLURM_SUCCESS;
	uint16_t type = 0;
	uint16_t ver = 0;
	char *state_file;
	Buf buffer = NULL;
	time_t buf_time;
	dbd_list_msg_t *msg = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .file = READ_LOCK,
				   .qos = WRITE_LOCK, .res = WRITE_LOCK,
				   .tres = WRITE_LOCK, .user = WRITE_LOCK,
				   .wckey = WRITE_LOCK };

	xassert(init_setup.state_save_location &&
		*init_setup.state_save_location);

	/* read the file */
	state_file = xstrdup(*init_setup.state_save_location);
	xstrcat(state_file, "/assoc_mgr_state"); /* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);

	if (!(buffer = create_mmap_buf(state_file))) {
		debug2("No association state file (%s) to recover", state_file);
		xfree(state_file);
		assoc_mgr_unlock(&locks);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		if (!ignore_state_errors)
			fatal("Can not recover assoc_mgr state, incompatible version, "
			      "got %u need >= %u <= %u, start with '-i' to ignore this",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover assoc_mgr state, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	while (remaining_buf(buffer) > 0) {
		safe_unpack16(&type, buffer);
		switch(type) {
		/* DBD_ADD_TRES can be removed 2 versions after 18.08 */
		case DBD_ADD_TRES:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_TRES, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No tres retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_tres_list);
			assoc_mgr_post_tres_list(msg->my_list);
			/*
			 * assoc_mgr_tres_list gets set in
			 * assoc_mgr_post_tres_list
			 */
			debug("Recovered %u tres",
			      list_count(assoc_mgr_tres_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_ASSOCS:
			if (!g_tres_count)
				fatal("load_assoc_mgr_state: "
				      "Unable to run cache without TRES, "
				      "please make sure you have a connection "
				      "to your database to continue.");
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_ASSOCS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No associations retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_assoc_list);
			assoc_mgr_assoc_list = msg->my_list;
			_post_assoc_list();

			debug("Recovered %u associations",
			      list_count(assoc_mgr_assoc_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_USERS:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_USERS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No users retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_user_list);
			assoc_mgr_user_list = msg->my_list;
			_post_user_list(assoc_mgr_user_list);
			debug("Recovered %u users",
			      list_count(assoc_mgr_user_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_RES:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_RES, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No resources retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_res_list);
			assoc_mgr_res_list = msg->my_list;
			_post_res_list(assoc_mgr_res_list);
			debug("Recovered %u resources",
			      list_count(assoc_mgr_res_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_QOS:
			if (!g_tres_count)
				fatal("load_assoc_mgr_state: "
				      "Unable to run cache without TRES, "
				      "please make sure you have a connection "
				      "to your database to continue.");
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_QOS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No qos retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_qos_list);
			assoc_mgr_qos_list = msg->my_list;
			_post_qos_list(assoc_mgr_qos_list);
			debug("Recovered %u qos",
			      list_count(assoc_mgr_qos_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_WCKEYS:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_WCKEYS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No wckeys retrieved");
				break;
			}
			FREE_NULL_LIST(assoc_mgr_wckey_list);
			assoc_mgr_wckey_list = msg->my_list;
			debug("Recovered %u wckeys",
			      list_count(assoc_mgr_wckey_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		default:
			error("unknown type %u given", type);
			goto unpack_error;
			break;
		}
		/* The tres, if here, will always be first */
		if (only_tres)
			break;
	}

	if (!only_tres && init_setup.running_cache)
		*init_setup.running_cache = 1;

	free_buf(buffer);
	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete assoc mgr state file, start with '-i' to ignore this");
	error("Incomplete assoc mgr state file");

	free_buf(buffer);

	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int assoc_mgr_refresh_lists(void *db_conn, uint16_t cache_level)
{
	bool partial_list = 1;

	if (!cache_level) {
		cache_level = init_setup.cache_level;
		partial_list = 0;
	}

	/* get tres before association and qos since it is used there */
	if (cache_level & ASSOC_MGR_CACHE_TRES) {
		if (_refresh_assoc_mgr_tres_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}

	/* get qos before association since it is used there */
	if (cache_level & ASSOC_MGR_CACHE_QOS)
		if (_refresh_assoc_mgr_qos_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	/* get user before association/wckey since it is used there */
	if (cache_level & ASSOC_MGR_CACHE_USER)
		if (_refresh_assoc_mgr_user_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if (cache_level & ASSOC_MGR_CACHE_ASSOC) {
		if (_refresh_assoc_mgr_assoc_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if (cache_level & ASSOC_MGR_CACHE_WCKEY)
		if (_refresh_assoc_wckey_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	if (cache_level & ASSOC_MGR_CACHE_RES)
		if (_refresh_assoc_mgr_res_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if (!partial_list && _running_cache())
		*init_setup.running_cache = 0;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_set_missing_uids()
{
	uid_t pw_uid;
	ListIterator itr = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .user = WRITE_LOCK,
				   .wckey = WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_assoc_list) {
		slurmdb_assoc_rec_t *object = NULL;
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((object = list_next(itr))) {
			if (object->user && (object->uid == NO_VAL)) {
				if (uid_from_string(
					    object->user, &pw_uid) < 0) {
					debug2("refresh association "
					       "couldn't get a uid for user %s",
					       object->user);
				} else {
					/* Since the uid changed the
					   hash as well will change.  Remove
					   the assoc from the hash before the
					   change or you won't find it.
					*/
					_delete_assoc_hash(object);

					object->uid = pw_uid;
					_add_assoc_hash(object);
				}
			}
		}
		list_iterator_destroy(itr);
	}

	if (assoc_mgr_wckey_list) {
		slurmdb_wckey_rec_t *object = NULL;
		itr = list_iterator_create(assoc_mgr_wckey_list);
		while ((object = list_next(itr))) {
			if (object->user && (object->uid == NO_VAL)) {
				if (uid_from_string(
					    object->user, &pw_uid) < 0) {
					debug2("refresh wckey "
					       "couldn't get a uid for user %s",
					       object->user);
				} else
					object->uid = pw_uid;
			}
		}
		list_iterator_destroy(itr);
	}

	if (assoc_mgr_user_list) {
		slurmdb_user_rec_t *object = NULL;
		itr = list_iterator_create(assoc_mgr_user_list);
		while ((object = list_next(itr))) {
			if (object->name && (object->uid == NO_VAL)) {
				if (uid_from_string(
					    object->name, &pw_uid) < 0) {
					debug3("refresh user couldn't get "
					       "a uid for user %s",
					       object->name);
				} else
					object->uid = pw_uid;
			}
		}
		list_iterator_destroy(itr);
	}
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* you should check for assoc == NULL before this function */
extern void assoc_mgr_normalize_assoc_shares(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc);
	/* Use slurmctld_conf.priority_flags directly instead of using a
	 * global flags variable. assoc_mgr_init() would be the logical
	 * place to set a global, but there is no great location for
	 * resetting it when scontrol reconfigure is called */
	if (slurmctld_conf.priority_flags & PRIORITY_FLAGS_FAIR_TREE)
		_normalize_assoc_shares_fair_tree(assoc);
	else
		_normalize_assoc_shares_traditional(assoc);
}

extern int assoc_mgr_find_tres_pos(slurmdb_tres_rec_t *tres_rec, bool locked)
{
	int i, tres_pos = -1;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!tres_rec->id && !tres_rec->type)
		return tres_pos;

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(assoc_mgr_tres_array);
	xassert(g_tres_count);
	xassert(assoc_mgr_tres_array[g_tres_count - 1]);

	for (i=0; i<g_tres_count; i++) {
		if (tres_rec->id &&
		    assoc_mgr_tres_array[i]->id == tres_rec->id) {
			tres_pos = i;
			break;
		} else if (!xstrcasecmp(assoc_mgr_tres_array[i]->type,
				  tres_rec->type) &&
			 !xstrcasecmp(assoc_mgr_tres_array[i]->name,
				  tres_rec->name)) {
			tres_pos = i;
			break;
		}
	}

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_pos;
}

/* The assoc_mgr tres read lock needs to be locked before calling this
 * function and while using the returned record */
extern slurmdb_tres_rec_t *assoc_mgr_find_tres_rec(slurmdb_tres_rec_t *tres_rec)
{
	int pos = assoc_mgr_find_tres_pos(tres_rec, 1);

	if (pos == -1)
		return NULL;
	else
		return assoc_mgr_tres_array[pos];
}

extern int assoc_mgr_set_tres_cnt_array(uint64_t **tres_cnt, char *tres_str,
					uint64_t init_val, bool locked)
{
	int array_size = sizeof(uint64_t) * g_tres_count;
	int diff_cnt = 0, i;

	xassert(tres_cnt);

	/* When doing the cnt the string is always the
	 * complete string, so always set everything to 0 to
	 * catch anything that was removed.
	 */
	xfree(*tres_cnt);
	if (!init_val)
		*tres_cnt = xmalloc(array_size);
	else {
		*tres_cnt = xmalloc_nz(array_size);
		for (i=0; i<g_tres_count; i++)
			(*tres_cnt)[i] = init_val;
	}

	if (tres_str) {
		List tmp_list = NULL;
		/* info("got %s", tres_str); */
		slurmdb_tres_list_from_string(
			&tmp_list, tres_str, TRES_STR_FLAG_NONE);
		if (tmp_list) {
			slurmdb_tres_rec_t *tres_rec;
			ListIterator itr = list_iterator_create(tmp_list);
			while ((tres_rec = list_next(itr))) {
				int pos = assoc_mgr_find_tres_pos(
					tres_rec, locked);
				if (pos == -1) {
					debug2("assoc_mgr_set_tres_cnt_array: "
					       "no tres "
					       "of id %u found in the array",
					       tres_rec->id);
					continue;
				}
				/* set the index to the count */
				(*tres_cnt)[pos] = tres_rec->count;
				/* info("%d pos %d has count of %"PRIu64, */
				/*      tres_rec->id, */
				/*      pos, tres_rec->count); */
			}
			list_iterator_destroy(itr);
			if (g_tres_count != list_count(tmp_list))
				diff_cnt = 1;
			FREE_NULL_LIST(tmp_list);
		}
	}
	return diff_cnt;
}

/* tres read lock needs to be locked before this is called. */
extern void assoc_mgr_set_assoc_tres_cnt(slurmdb_assoc_rec_t *assoc)
{
	/* This isn't needed on the dbd */
	if (slurmdbd_conf)
		return;

	xassert(assoc_mgr_tres_array);

	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_ctld, assoc->grp_tres,
				     INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_mins_ctld,
				     assoc->grp_tres_mins, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_run_mins_ctld,
				     assoc->grp_tres_run_mins, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_ctld,
				     assoc->max_tres_pj, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_pn_ctld,
				     assoc->max_tres_pn, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_mins_ctld,
				     assoc->max_tres_mins_pj, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_run_mins_ctld,
				     assoc->max_tres_run_mins, INFINITE64, 1);
}

/* tres read lock needs to be locked before this is called. */
extern void assoc_mgr_set_qos_tres_cnt(slurmdb_qos_rec_t *qos)
{
	/* This isn't needed on the dbd */
	if (slurmdbd_conf)
		return;

	xassert(assoc_mgr_tres_array);

	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_ctld, qos->grp_tres,
				     INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_mins_ctld,
				     qos->grp_tres_mins, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_run_mins_ctld,
				     qos->grp_tres_run_mins, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pa_ctld,
				     qos->max_tres_pa, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pj_ctld,
				     qos->max_tres_pj, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pn_ctld,
				     qos->max_tres_pn, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pu_ctld,
				     qos->max_tres_pu, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_mins_pj_ctld,
				     qos->max_tres_mins_pj, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_run_mins_pa_ctld,
				     qos->max_tres_run_mins_pa, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_run_mins_pu_ctld,
				     qos->max_tres_run_mins_pu, INFINITE64, 1);
	assoc_mgr_set_tres_cnt_array(&qos->min_tres_pj_ctld,
				     qos->min_tres_pj, INFINITE64, 1);
}

extern char *assoc_mgr_make_tres_str_from_array(
	uint64_t *tres_cnt, uint32_t flags, bool locked)
{
	int i;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!tres_cnt)
		return NULL;

	if (!locked)
		assoc_mgr_lock(&locks);

	for (i=0; i<g_tres_count; i++) {
		if (!assoc_mgr_tres_array[i])
			continue;

		if (flags & TRES_STR_FLAG_ALLOW_REAL) {
			if ((tres_cnt[i] == NO_VAL64) ||
			    (tres_cnt[i] == INFINITE64))
				continue;
		} else if (!tres_cnt[i])
			continue;

		if (flags & TRES_STR_FLAG_SIMPLE)
			xstrfmtcat(tres_str, "%s%u=%"PRIu64,
				   tres_str ? "," : "",
				   assoc_mgr_tres_array[i]->id, tres_cnt[i]);
		else {
			/* Always skip these when printing out named TRES */
			if ((tres_cnt[i] == NO_VAL64) ||
			    (tres_cnt[i] == INFINITE64))
				continue;
			if ((flags & TRES_STR_CONVERT_UNITS) &&
			    ((assoc_mgr_tres_array[i]->id == TRES_MEM) ||
			     !xstrcasecmp(assoc_mgr_tres_array[i]->type, "bb"))
				) {
				char outbuf[32];
				convert_num_unit((double)tres_cnt[i], outbuf,
						 sizeof(outbuf), UNIT_MEGA,
						 NO_VAL,
						 CONVERT_NUM_UNIT_EXACT);
				xstrfmtcat(tres_str, "%s%s=%s",
					   tres_str ? "," : "",
					   assoc_mgr_tres_name_array[i],
					   outbuf);
			} else if (!xstrcasecmp(assoc_mgr_tres_array[i]->type,
						"fs") ||
				   !xstrcasecmp(assoc_mgr_tres_array[i]->type,
						"ic")) {
				char outbuf[32];
				convert_num_unit((double)tres_cnt[i], outbuf,
						 sizeof(outbuf), UNIT_NONE,
						 NO_VAL,
						 CONVERT_NUM_UNIT_EXACT);
				xstrfmtcat(tres_str, "%s%s=%s",
					   tres_str ? "," : "",
					   assoc_mgr_tres_name_array[i],
					   outbuf);
			} else
				xstrfmtcat(tres_str, "%s%s=%"PRIu64,
					   tres_str ? "," : "",
					   assoc_mgr_tres_name_array[i],
					   tres_cnt[i]);
		}
	}

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_str;
}

/* READ lock needs to be set on associations before calling this. */
extern void assoc_mgr_get_default_qos_info(
	slurmdb_assoc_rec_t *assoc_ptr, slurmdb_qos_rec_t *qos_rec)
{
	xassert(qos_rec);

	if (!qos_rec->name && !qos_rec->id) {
		if (assoc_ptr && assoc_ptr->usage->valid_qos) {
			if (assoc_ptr->def_qos_id)
				qos_rec->id = assoc_ptr->def_qos_id;
			else if (bit_set_count(assoc_ptr->usage->valid_qos)
				 == 1)
				qos_rec->id =
					bit_ffs(assoc_ptr->usage->valid_qos);
			else if (assoc_mgr_root_assoc
				 && assoc_mgr_root_assoc->def_qos_id)
				qos_rec->id = assoc_mgr_root_assoc->def_qos_id;
			else
				qos_rec->name = "normal";
		} else if (assoc_mgr_root_assoc
			   && assoc_mgr_root_assoc->def_qos_id)
			qos_rec->id = assoc_mgr_root_assoc->def_qos_id;
		else
			qos_rec->name = "normal";
	}

	return;
}

/* Calcuate a weighted tres value.
 * IN: tres_cnt - array of tres values of size g_tres_count.
 * IN: weights - weights to apply to tres values of size g_tres_count.
 * IN: flags - priority flags (toogle between MAX or SUM of tres).
 * IN: locked - whether the tres read assoc mgr lock is locked or not.
 * RET: returns the calcuated tres weight.
 */
extern double assoc_mgr_tres_weighted(uint64_t *tres_cnt, double *weights,
				      uint16_t flags, bool locked)
{
	int    i;
	double to_bill_node   = 0.0;
	double to_bill_global = 0.0;
	double billable_tres  = 0.0;
	assoc_mgr_lock_t tres_read_lock = { .tres = READ_LOCK };

	/* We don't have any resources allocated, just return 0. */
	if (!tres_cnt)
		return 0.0;

	/* Default to cpus if no weights given */
	if (!weights)
		return (double)tres_cnt[TRES_ARRAY_CPU];

	if (!locked)
		assoc_mgr_lock(&tres_read_lock);

	for (i = 0; i < g_tres_count; i++) {
		double tres_weight = weights[i];
		char  *tres_type   = assoc_mgr_tres_array[i]->type;
		double tres_value  = tres_cnt[i];

		if (i == TRES_ARRAY_BILLING)
			continue;

		debug3("TRES Weight: %s = %f * %f = %f",
		       assoc_mgr_tres_name_array[i], tres_value, tres_weight,
		       tres_value * tres_weight);

		tres_value *= tres_weight;

		if ((flags & PRIORITY_FLAGS_MAX_TRES) &&
		    ((i == TRES_ARRAY_CPU) ||
		     (i == TRES_ARRAY_MEM) ||
		     (i == TRES_ARRAY_NODE) ||
		     (!xstrcasecmp(tres_type, "gres"))))
			to_bill_node = MAX(to_bill_node, tres_value);
		else
			to_bill_global += tres_value;
	}

	billable_tres = to_bill_node + to_bill_global;

	debug3("TRES Weighted: %s = %f",
	       (flags & PRIORITY_FLAGS_MAX_TRES) ?
	       "MAX(node TRES) + SUM(Global TRES)" : "SUM(TRES)",
	       billable_tres);

	if (!locked)
		assoc_mgr_unlock(&tres_read_lock);

	return billable_tres;
}

/*
 * Must have TRES read locks
 */
extern int assoc_mgr_tres_pos_changed()
{
	return assoc_mgr_tres_old_pos ? true : false;
}

/*
 * Must have TRES read locks
 */
extern int assoc_mgr_get_old_tres_pos(int cur_pos)
{
	if (!assoc_mgr_tres_old_pos || (cur_pos >= g_tres_count))
		return -1;
	return assoc_mgr_tres_old_pos[cur_pos];
}
