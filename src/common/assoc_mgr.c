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

#include "src/common/slurmdbd_pack.h"
#include "src/common/state_save.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/interfaces/gres.h"
#include "src/interfaces/priority.h"

#include "src/slurmdbd/read_config.h"

#define ASSOC_HASH_SIZE 1000
#define ASSOC_HASH_ID_INX(_assoc_id)	(_assoc_id % ASSOC_HASH_SIZE)

typedef struct {
	char *req;
	list_t *ret_list;
} find_coord_t;

typedef struct {
	bool locked;
	bool relative;
	uint64_t *relative_tres_cnt;
	uint64_t **tres_cnt;
} foreach_tres_pos_t;

slurmdb_assoc_rec_t *assoc_mgr_root_assoc = NULL;
uint32_t g_qos_max_priority = 0;
uint32_t g_assoc_max_priority = 0;
uint32_t g_qos_count = 0;
uint32_t g_user_assoc_count = 0;
uint32_t g_tres_count = 0;

list_t *assoc_mgr_tres_list = NULL;
slurmdb_tres_rec_t **assoc_mgr_tres_array = NULL;
char **assoc_mgr_tres_name_array = NULL;
list_t *assoc_mgr_assoc_list = NULL;
list_t *assoc_mgr_coord_list = NULL;
list_t *assoc_mgr_res_list = NULL;
list_t *assoc_mgr_qos_list = NULL;
list_t *assoc_mgr_user_list = NULL;
list_t *assoc_mgr_wckey_list = NULL;

static int setup_children = 0;
static pthread_rwlock_t assoc_mgr_locks[ASSOC_MGR_ENTITY_COUNT];
static pthread_mutex_t assoc_lock_init = PTHREAD_MUTEX_INITIALIZER;

static assoc_init_args_t init_setup;
static slurmdb_assoc_rec_t **assoc_hash_id = NULL;
static slurmdb_assoc_rec_t **assoc_hash = NULL;
static int *assoc_mgr_tres_old_pos = NULL;

static bool _running_cache(void)
{
	if (init_setup.running_cache &&
	    (*init_setup.running_cache != RUNNING_CACHE_STATE_NOTRUNNING))
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
	if (slurmdbd_conf && assoc->cluster)
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
		assoc_hash_id = xcalloc(ASSOC_HASH_SIZE,
					sizeof(slurmdb_assoc_rec_t *));
	if (!assoc_hash)
		assoc_hash = xcalloc(ASSOC_HASH_SIZE,
				     sizeof(slurmdb_assoc_rec_t *));

	assoc->assoc_next_id = assoc_hash_id[inx];
	assoc_hash_id[inx] = assoc;

	inx = _assoc_hash_index(assoc);
	assoc->assoc_next = assoc_hash[inx];
	assoc_hash[inx] = assoc;
}

static slurmdb_assoc_rec_t *_find_assoc_rec_id(uint32_t assoc_id,
					       char *cluster_name)
{
	slurmdb_assoc_rec_t *assoc;

	if (!assoc_hash_id) {
		debug2("%s: no associations added yet", __func__);
		return NULL;
	}

	assoc =	assoc_hash_id[ASSOC_HASH_ID_INX(assoc_id)];

	while (assoc) {
		if ((!slurmdbd_conf ||
		     !xstrcmp(cluster_name, assoc->cluster)) &&
		    (assoc->id == assoc_id))
			return assoc;
		assoc = assoc->assoc_next_id;
	}

	return NULL;
}

static int _find_acct_by_name(void *x, void *y)
{
	slurmdb_coord_rec_t *acct = (slurmdb_coord_rec_t*) x;
	if (!xstrcmp(acct->name, (char*)y))
		return 1;
	return 0;
}

extern int assoc_mgr_find_nondirect_coord_by_name(void *x, void *y)
{
	slurmdb_coord_rec_t *acct = x;

	if (acct->direct)
		return 0;

	return _find_acct_by_name(x, y);
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

	/* We can only use _find_assoc_rec_id if we are not on the slurmdbd */
	if (assoc->id)
		return _find_assoc_rec_id(assoc->id, assoc->cluster);

	if (!assoc_hash) {
		debug2("%s: no associations added yet", __func__);
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
		if (slurmdbd_conf && assoc->cluster
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
	xassert(assoc);

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

static int _addto_used_info(slurmdb_assoc_usage_t *usage1,
			    slurmdb_assoc_usage_t *usage2)
{
	int i;

	if (!usage1 || !usage2)
		return SLURM_ERROR;

	for (i=0; i < usage1->tres_cnt; i++) {
		usage1->grp_used_tres[i] +=
			usage2->grp_used_tres[i];
		usage1->grp_used_tres_run_secs[i] +=
			usage2->grp_used_tres_run_secs[i];
		usage1->usage_tres_raw[i] +=
			usage2->usage_tres_raw[i];
	}

	usage1->accrue_cnt += usage2->accrue_cnt;

	usage1->grp_used_wall += usage2->grp_used_wall;

	usage1->used_jobs += usage2->used_jobs;
	usage1->used_submit_jobs += usage2->used_submit_jobs;
	usage1->usage_raw += usage2->usage_raw;

	slurmdb_merge_grp_node_usage(&usage1->grp_node_bitmap,
				     &usage1->grp_node_job_cnt,
				     usage2->grp_node_bitmap,
				     usage2->grp_node_job_cnt);
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

	if (assoc->usage->grp_node_bitmap)
		bit_clear_all(assoc->usage->grp_node_bitmap);
	if (assoc->usage->grp_node_job_cnt)
		memset(assoc->usage->grp_node_job_cnt, 0,
		       sizeof(uint16_t) * node_record_count);

	/* do not reset usage_raw or grp_used_wall.
	 * if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	return SLURM_SUCCESS;
}

static void _clear_qos_used_limit_list(list_t *used_limit_list, uint32_t tres_cnt)
{
	slurmdb_used_limits_t *used_limits = NULL;
	list_itr_t *itr = NULL;
	int i;

	if (!used_limit_list || !list_count(used_limit_list))
		return;

	itr = list_iterator_create(used_limit_list);
	while ((used_limits = list_next(itr))) {
		used_limits->accrue_cnt = 0;
		used_limits->jobs = 0;
		if (used_limits->node_bitmap)
			bit_clear_all(used_limits->node_bitmap);
		if (used_limits->node_job_cnt) {
			memset(used_limits->node_job_cnt, 0,
			       sizeof(uint16_t) * node_record_count);
		}
		used_limits->submit_jobs = 0;
		for (i=0; i<tres_cnt; i++) {
			used_limits->tres[i] = 0;
			used_limits->tres_run_secs[i] = 0;
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
	if (qos->usage->grp_node_bitmap)
		bit_clear_all(qos->usage->grp_node_bitmap);
	if (qos->usage->grp_node_job_cnt) {
		memset(qos->usage->grp_node_job_cnt, 0,
		       sizeof(uint16_t) * node_record_count);
	}
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
	list_itr_t *itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	uid_t pw_uid;

	xassert(user->name);
	xassert(user->old_name);

	if (uid_from_string(user->name, &pw_uid) < 0) {
		debug("%s: couldn't get new uid for user %s",
		      __func__, user->name);
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
	list_itr_t *itr = NULL;

	if (!assoc)
		return SLURM_ERROR;

	if (assoc->qos_list)
		list_flush(assoc->qos_list);
	else
		assoc->qos_list = list_create(xfree_ptr);

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
					list_t *new_qos_list)
{
	list_itr_t *new_qos_itr = NULL, *curr_qos_itr = NULL;
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

static int _list_find_uid(void *x, void *key)
{
	slurmdb_user_rec_t *user = (slurmdb_user_rec_t *) x;
	uint32_t uid = *(uint32_t *) key;

	if (user->uid == uid)
		return 1;
	return 0;
}

static int _list_find_user(void *x, void *key)
{
	slurmdb_user_rec_t *found_user = x;
	slurmdb_user_rec_t *user = key;

	if (user->uid != NO_VAL)
		return (found_user->uid == user->uid);
	else if (!xstrcasecmp(found_user->name, user->name))
		return 1;

	return 0;
}

static int _list_find_coord(void *x, void *key)
{
	slurmdb_user_rec_t *user = x;
	find_coord_t *find_coord = key;
	slurmdb_coord_rec_t *found_coord, *coord;

	if (!user->coord_accts ||
	    !(found_coord = list_find_first(user->coord_accts,
					    _find_acct_by_name,
					    find_coord->req)))
		return 0;

	if (!find_coord->ret_list)
		find_coord->ret_list = list_create(slurmdb_destroy_coord_rec);
	coord = xmalloc(sizeof(*coord));
	list_append(find_coord->ret_list, coord);
	coord->name = xstrdup(user->name);
	coord->direct = found_coord->direct;

	return 0;
}

/* locks should be put in place before calling this function USER_WRITE */
static void _set_user_default_acct(slurmdb_assoc_rec_t *assoc,
				   slurmdb_user_rec_t *user)
{
	xassert(assoc);
	xassert(assoc->acct);
	xassert(assoc_mgr_user_list);

	/* set up the default if this is it */
	if ((assoc->is_def == 1) && (assoc->uid != NO_VAL)) {
		if (!user)
			user = list_find_first(assoc_mgr_user_list,
					       _list_find_uid, &assoc->uid);

		if (!user)
			return;

		if (!user->default_acct
		    || xstrcmp(user->default_acct, assoc->acct)) {
			xfree(user->default_acct);
			if (assoc->is_def == 1) {
				user->default_acct = xstrdup(assoc->acct);
				debug2("user %s default acct is %s",
				       user->name, user->default_acct);
			} else
				debug2("user %s default acct %s removed",
				       user->name, assoc->acct);
		}
		/* cache user rec reference for backfill*/
		assoc->user_rec = user;
	}
}

/* locks should be put in place before calling this function USER_WRITE */
static void _clear_user_default_acct(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc);
	xassert(assoc->acct);
	xassert(assoc_mgr_user_list);

	/* set up the default if this is it */
	if ((assoc->is_def == 0) && (assoc->uid != NO_VAL)) {
		slurmdb_user_rec_t *user = list_find_first(
			assoc_mgr_user_list, _list_find_uid, &assoc->uid);

		if (!user)
			return;

		if (!user->default_acct
		    || !xstrcmp(user->default_acct, assoc->acct)) {
			xfree(user->default_acct);
			debug2("user %s default acct %s removed",
			       user->name, assoc->acct);
		}
		/* cache user rec reference for backfill */
		assoc->user_rec = user;
	}
}

/* locks should be put in place before calling this function USER_WRITE */
static void _set_user_default_wckey(slurmdb_wckey_rec_t *wckey,
				    slurmdb_user_rec_t *user)
{
	xassert(wckey);
	xassert(wckey->name);
	xassert(assoc_mgr_user_list);

	/* set up the default if this is it */
	if ((wckey->is_def == 1) && (wckey->uid != NO_VAL)) {
		if (!user)
			user = list_find_first(assoc_mgr_user_list,
					       _list_find_uid, &wckey->uid);

		if (!user)
			return;
		if (!user->default_wckey
		    || xstrcmp(user->default_wckey, wckey->name)) {
			xfree(user->default_wckey);
			user->default_wckey = xstrdup(wckey->name);
			debug2("user %s default wckey is %s",
			       user->name, user->default_wckey);
		}
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
		if (!(parent = _find_assoc_rec_id(prev_parent->parent_id,
						  prev_parent->cluster))) {
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
		debug2("assoc %u(%s, %s) has %s parent of %u(%s, %s) %s",
		       assoc->id, assoc->acct, assoc->user,
		       direct ? "direct" : "fs",
		       parent->id, parent->acct, parent->user, assoc->lineage);
	else
		debug2("assoc %u(%s, %s) doesn't have a %s "
		       "parent (probably root) %s",
		       assoc->id, assoc->acct, assoc->user,
		       direct ? "direct" : "fs", assoc->lineage);

	return parent;
}

static int _set_assoc_parent_and_user(slurmdb_assoc_rec_t *assoc)
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
	/* Users have no children so leaf is same as total */
	if (assoc->user)
		assoc->leaf_usage = assoc->usage;

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
	} else if (!slurmdbd_conf && (assoc_mgr_root_assoc != assoc)) {
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

	/*
	 * Get the qos bitmap here for the assoc
	 * On the DBD we want this for all the associations, else we only want
	 * this for users.
	 */
	if ((g_qos_count > 0) && (slurmdbd_conf || assoc->user)) {
		if (!assoc->usage->valid_qos ||
		    (bit_size(assoc->usage->valid_qos) != g_qos_count)) {
			FREE_NULL_BITMAP(assoc->usage->valid_qos);
			assoc->usage->valid_qos = bit_alloc(g_qos_count);
		} else
			bit_clear_all(assoc->usage->valid_qos);
		set_qos_bitstr_from_list(assoc->usage->valid_qos,
					 assoc->qos_list);
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
		_set_user_default_acct(assoc, NULL);

		if (assoc->usage->valid_qos) {
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

static void _set_assoc_norm_priority(slurmdb_assoc_rec_t *assoc)
{
	if (!assoc)
		return;

	if (assoc->priority == INFINITE)
		assoc->priority = 0;

	if (!assoc->usage)
		assoc->usage = slurmdb_create_assoc_usage(g_tres_count);
	/* Users have no children so leaf_usage is same as total */
	if (assoc->user)
		assoc->leaf_usage = assoc->usage;

	if (!g_assoc_max_priority)
		assoc->usage->priority_norm = 0.0;
	else
		assoc->usage->priority_norm =
			(double)assoc->priority / (double)g_assoc_max_priority;
}

static void _calculate_assoc_norm_priorities(bool new_max)
{
	list_itr_t *itr = NULL;
	slurmdb_assoc_rec_t *assoc;

	xassert(verify_assoc_lock(ASSOC_LOCK, WRITE_LOCK));
	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	itr = list_iterator_create(assoc_mgr_assoc_list);

	if (new_max) {
		g_assoc_max_priority = 0;
		while ((assoc = list_next(itr))) {
			if ((assoc->priority != INFINITE) &&
			    assoc->priority > g_assoc_max_priority)
				g_assoc_max_priority = assoc->priority;
		}
	}

	list_iterator_reset(itr);
	while ((assoc = list_next(itr)))
		_set_assoc_norm_priority(assoc);

	list_iterator_destroy(itr);
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
	list_t *children = assoc->usage->children_list;
	list_itr_t *itr = NULL;
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
	list_t *children = assoc->usage->children_list;
	list_itr_t *itr = NULL;
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
	list_itr_t *itr = NULL;
	g_assoc_max_priority = 0;
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
		_set_assoc_parent_and_user(assoc);
		_add_assoc_hash(assoc);
		assoc_mgr_set_assoc_tres_cnt(assoc);
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

	_calculate_assoc_norm_priorities(true);

	slurmdb_sort_hierarchical_assoc_list(assoc_mgr_assoc_list);

	//END_TIMER2("load_associations");
	return SLURM_SUCCESS;
}

static int _post_user_list(list_t *user_list)
{
	slurmdb_user_rec_t *user = NULL;
	list_itr_t *itr = list_iterator_create(user_list);
	DEF_TIMERS;

	START_TIMER;

	if (assoc_mgr_coord_list)
		list_flush(assoc_mgr_coord_list);
	else
		assoc_mgr_coord_list = list_create(NULL);

	while ((user = list_next(itr))) {
		uid_t pw_uid;
		/* Just to make sure we have a default_wckey since it
		   might not be set up yet.
		*/
		if (!user->default_wckey)
			user->default_wckey = xstrdup("");
		if (uid_from_string (user->name, &pw_uid) < 0) {
			debug("%s: couldn't get a uid for user: %s",
			      __func__, user->name);
			user->uid = NO_VAL;
		} else
			user->uid = pw_uid;

		if (user->coord_accts && list_count(user->coord_accts))
			list_append(assoc_mgr_coord_list, user);
	}
	list_iterator_destroy(itr);
	END_TIMER2(__func__);
	return SLURM_SUCCESS;
}

static int _post_wckey_list(list_t *wckey_list)
{
	slurmdb_wckey_rec_t *wckey = NULL;
	list_itr_t *itr = list_iterator_create(wckey_list);
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
		_set_user_default_wckey(wckey, NULL);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

/* NOTE QOS write lock needs to be set before calling this. */
static int _post_qos_list(list_t *qos_list)
{
	slurmdb_qos_rec_t *qos = NULL;
	list_itr_t *itr = list_iterator_create(qos_list);

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

static int _post_res_list(list_t *res_list)
{
	if (res_list && !slurmdbd_conf) {
		slurmdb_res_rec_t *object = NULL;
		list_itr_t *itr = list_iterator_create(res_list);
		while ((object = list_next(itr))) {
			if (object->clus_res_list
			    && list_count(object->clus_res_list)) {
				xassert(!object->clus_res_rec);

				while ((object->clus_res_rec =
					list_pop(object->clus_res_list))) {
					/* only update the local clusters
					 * res, only one per res
					 * record, so throw the others away. */
					if (!xstrcasecmp(
						object->clus_res_rec->cluster,
						slurm_conf.cluster_name))
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
extern int assoc_mgr_post_tres_list(list_t *new_list)
{
	list_itr_t *itr;
	slurmdb_tres_rec_t *tres_rec, **new_array;
	char **new_name_array;
	bool changed_size = false, changed_pos = false;
	int i;
	int new_cnt;

	xassert(new_list);

	new_cnt = list_count(new_list);

	xassert(new_cnt > 0);

	new_array = xcalloc(new_cnt, sizeof(slurmdb_tres_rec_t *));
	new_name_array = xcalloc(new_cnt, sizeof(char *));

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

		assoc_mgr_tres_old_pos = xcalloc(new_cnt, sizeof(int));
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
		list_itr_t *itr_user;

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
							 tres_run_secs,
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
								tres_run_secs
								[old_pos];
						}

						memcpy(used_limits->tres,
						       grp_used_tres,
						       array_size);
						memcpy(used_limits->
						       tres_run_secs,
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
	slurmdb_tres_cond_t tres_q = {0};
	uid_t uid = getuid();
	list_t *new_list = NULL;
	int changed;
	assoc_mgr_lock_t locks =
		{ .assoc = WRITE_LOCK, .qos = WRITE_LOCK, .tres= WRITE_LOCK };

	assoc_mgr_lock(&locks);

	/* If this exists we only want/care about tracking/caching these TRES */
	if (slurm_conf.accounting_storage_tres) {
		tres_q.type_list = list_create(xfree_ptr);
		slurm_addto_char_list(tres_q.type_list,
				      slurm_conf.accounting_storage_tres);
	}
	new_list = acct_storage_g_get_tres(
		db_conn, uid, &tres_q);

	FREE_NULL_LIST(tres_q.type_list);

	if (!new_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("%s: no list was made.", __func__);
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
	slurmdb_assoc_cond_t assoc_q = {0};
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = READ_LOCK,
				   .tres = READ_LOCK, .user = WRITE_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_assoc_list);

	if (!slurmdbd_conf) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all associations.",
		      __func__);
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
			error("%s: no list was made.", __func__);
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
	if (!slurmdbd_conf) {
		res_q.with_clusters = 1;
		res_q.cluster_list = list_create(NULL);
		list_append(res_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all associations.",
		      __func__);
	}

	assoc_mgr_res_list = acct_storage_g_get_res(db_conn, uid, &res_q);

	FREE_NULL_LIST(res_q.cluster_list);

	if (!assoc_mgr_res_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("%s: no list was made.", __func__);
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
	list_t *new_list = NULL;
	assoc_mgr_lock_t locks = { .qos = WRITE_LOCK };

	new_list = acct_storage_g_get_qos(db_conn, uid, NULL);

	if (!new_list) {
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("%s: no list was made.", __func__);
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
	slurmdb_user_cond_t user_q = { .with_coords = 1 };
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK };

	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_user_list);
	FREE_NULL_LIST(assoc_mgr_coord_list);
	assoc_mgr_user_list = acct_storage_g_get_users(db_conn, uid, &user_q);

	if (!assoc_mgr_user_list) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("%s: no list was made.", __func__);
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
	slurmdb_wckey_cond_t wckey_q = {0};
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK, .wckey = WRITE_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	FREE_NULL_LIST(assoc_mgr_wckey_list);

	if (!slurmdbd_conf) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all wckeys.",
		      __func__);
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
			error("%s: no list was made.", __func__);
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
	slurmdb_assoc_cond_t assoc_q = {0};
	list_t *current_assocs = NULL;
	uid_t uid = getuid();
	list_itr_t *curr_itr = NULL;
	slurmdb_assoc_rec_t *curr_assoc = NULL, *assoc = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = READ_LOCK,
				   .tres = READ_LOCK, .user = WRITE_LOCK };
//	DEF_TIMERS;

	if (!slurmdbd_conf) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all associations.",
		      __func__);
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

		error("%s: no new list given back keeping cached one.",
		      __func__);
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
		if (!curr_assoc->leaf_usage)
			continue;

		if (!(assoc = _find_assoc_rec_id(curr_assoc->id,
						 curr_assoc->cluster)))
			continue;

		while (assoc) {
			_addto_used_info(assoc->usage, curr_assoc->leaf_usage);
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
	list_t *current_res = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .res = WRITE_LOCK };

	slurmdb_init_res_cond(&res_q, 0);
	if (!slurmdbd_conf) {
		res_q.with_clusters = 1;
		res_q.cluster_list = list_create(NULL);
		list_append(res_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all associations.",
		      __func__);
	}

	current_res = acct_storage_g_get_res(db_conn, uid, &res_q);

	FREE_NULL_LIST(res_q.cluster_list);

	if (!current_res) {
		error("%s: no new list given back keeping cached one.",
		      __func__);
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
	list_t *current_qos = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .qos = WRITE_LOCK };

	current_qos = acct_storage_g_get_qos(db_conn, uid, NULL);

	if (!current_qos) {
		error("%s: no new list given back keeping cached one.",
		      __func__);
		return SLURM_ERROR;
	}

	assoc_mgr_lock(&locks);

	_post_qos_list(current_qos);

	/* move usage from old list over to the new one */
	if (assoc_mgr_qos_list) {
		slurmdb_qos_rec_t *curr_qos = NULL, *qos_rec = NULL;
		list_itr_t *itr = list_iterator_create(current_qos);

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
	list_t *current_users = NULL;
	slurmdb_user_cond_t user_q = { .with_coords = 1 };
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK };

	current_users = acct_storage_g_get_users(db_conn, uid, &user_q);

	if (!current_users) {
		error("%s: no new list given back keeping cached one.",
		      __func__);
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
	slurmdb_wckey_cond_t wckey_q = {0};
	list_t *current_wckeys = NULL;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { .user = WRITE_LOCK, .wckey = WRITE_LOCK };

	if (!slurmdbd_conf) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, slurm_conf.cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("%s: no cluster name here going to get all wckeys.",
		      __func__);
	}

	current_wckeys = acct_storage_g_get_wckeys(db_conn, uid, &wckey_q);

	FREE_NULL_LIST(wckey_q.cluster_list);

	if (!current_wckeys) {
		error("%s: no new list given back keeping cached one.",
		      __func__);
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
		if (xstrcmp(slurm_conf.priority_type, "priority/basic"))
			setup_children = 1;

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

	/* check if we can't talk to the db yet (Do this after all
	 * the initialization above) */
	if (db_conn_errno != SLURM_SUCCESS)
		return SLURM_ERROR;

	/* get tres before association and qos since it is used there */
	if ((!assoc_mgr_tres_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_TRES)) {
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
		list_itr_t *itr =
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
	FREE_NULL_LIST(assoc_mgr_coord_list);
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
	assoc_mgr_assoc_list = NULL;
	assoc_mgr_res_list = NULL;
	assoc_mgr_qos_list = NULL;
	assoc_mgr_user_list = NULL;
	assoc_mgr_wckey_list = NULL;

	assoc_mgr_root_assoc = NULL;

	if (_running_cache())
		*init_setup.running_cache = RUNNING_CACHE_STATE_NOTRUNNING;

	xfree(assoc_hash_id);
	xfree(assoc_hash);

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

static slurmdb_admin_level_t _get_admin_level_internal(void *db_conn,
						       uint32_t uid,
						       bool locked)
{
	assoc_mgr_lock_t locks = { .user = READ_LOCK };
	slurmdb_user_rec_t *found_user = NULL;
	slurmdb_admin_level_t level = SLURMDB_ADMIN_NOTSET;

	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return SLURMDB_ADMIN_NOTSET;

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(USER_LOCK, READ_LOCK));

	if (!assoc_mgr_user_list) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURMDB_ADMIN_NOTSET;
	}

	found_user = list_find_first_ro(assoc_mgr_user_list,
					_list_find_uid, &uid);

	if (found_user)
		level = found_user->admin_level;

	if (!locked)
		assoc_mgr_unlock(&locks);

	return level;
}

static int _foreach_add2coord(void *x, void *arg)
{
	slurmdb_user_rec_t *user = x;
	slurmdb_assoc_rec_t *assoc_in = arg;
	slurmdb_assoc_rec_t *assoc = assoc_in;
	slurmdb_coord_rec_t *coord;

	/* Check to see if user a coord */
	if (!user->coord_accts)
		return 0;

	/* See if the user is a coord of any of this tree */
	while (assoc) {
		if (assoc_mgr_is_user_acct_coord_user_rec(user, assoc->acct))
			break;
		assoc = assoc->usage->parent_assoc_ptr;
	}

	if (!assoc)
		return 0;

	/* If it is add any missing to the list */
	assoc = assoc_in;
	while (assoc) {
		if (assoc_mgr_is_user_acct_coord_user_rec(user, assoc->acct))
			break;
		coord = xmalloc(sizeof(*coord));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(assoc->acct);
		coord->direct = 0;
		assoc = assoc->usage->parent_assoc_ptr;
	}
	return 0;
}

static void _add_potential_coord_children(slurmdb_assoc_rec_t *assoc)
{
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	if (assoc->user || !assoc_mgr_coord_list)
		return;

	(void) list_for_each(assoc_mgr_coord_list, _foreach_add2coord, assoc);
}

static int _delete_nondirect_coord_children(void *x, void *arg)
{
	slurmdb_assoc_rec_t *assoc = x;
	slurmdb_user_rec_t *user = arg;

	(void) list_delete_first(user->coord_accts,
				 assoc_mgr_find_nondirect_coord_by_name,
				 assoc->acct);
	if (assoc->usage->children_list)
		(void) list_for_each(assoc->usage->children_list,
				     _delete_nondirect_coord_children, user);

	return 0;
}

static int _foreach_rem_coord(void *x, void *arg)
{
	slurmdb_user_rec_t *user = x;
	slurmdb_assoc_rec_t *assoc = arg;

	/* Check to see if user a coord */
	if (!user->coord_accts)
		return 0;

	return _delete_nondirect_coord_children(assoc, user);
}

/* This is called when resetting a partition's QOS */
static int _reset_relative_flag(void *x, void *arg)
{
	slurmdb_qos_rec_t *qos = x;

	qos->flags &= ~QOS_FLAG_RELATIVE_SET;

	/* Remove the Part flag as well */
	qos->flags &= ~QOS_FLAG_PART_QOS;

	return 0;
}

static int _set_relative_cnt(void *x, void *arg)
{
	assoc_mgr_set_qos_tres_relative_cnt(x, NULL);

	return 0;
}

static void _remove_nondirect_coord_acct(slurmdb_assoc_rec_t *assoc)
{
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	if (assoc->user || !assoc_mgr_coord_list)
		return;

	(void) list_for_each(assoc_mgr_coord_list, _foreach_rem_coord, assoc);
}

static void _handle_new_user_coord(slurmdb_user_rec_t *rec)
{
	xassert(verify_assoc_lock(USER_LOCK, WRITE_LOCK));

	if (rec->coord_accts && list_count(rec->coord_accts)) {
		if (!list_find_first(assoc_mgr_coord_list,
				     slurm_find_ptr_in_list,
				     rec))
			list_append(assoc_mgr_coord_list, rec);
	} else
		list_delete_first(assoc_mgr_coord_list,
				  slurm_find_ptr_in_list, rec);
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

extern bool verify_assoc_lock(assoc_mgr_lock_datatype_t datatype,
			      lock_level_t level)
{
	return (((lock_level_t *) &thread_locks)[datatype] >= level);
}

extern bool verify_assoc_unlock(assoc_mgr_lock_datatype_t datatype)
{
	return (((lock_level_t *) &thread_locks)[datatype] == 0);
}
#endif

extern void assoc_mgr_lock(assoc_mgr_lock_t *locks)
{
	static bool init_run = false;
	xassert(_store_locks(locks));

	slurm_mutex_lock(&assoc_lock_init);
	if (!init_run) {
		init_run = true;
		for (int i = 0; i < ASSOC_MGR_ENTITY_COUNT; i++)
			slurm_rwlock_init(&assoc_mgr_locks[i]);
	}
	slurm_mutex_unlock(&assoc_lock_init);

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
				     list_t *assoc_list)
{
	list_itr_t *itr = NULL;
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
		if (assoc->acct && xstrcmp(assoc->acct, found_assoc->acct)) {
			debug4("not the right acct %s != %s",
			       assoc->acct, found_assoc->acct);
			continue;
		}

		list_append(assoc_list, found_assoc);
		set = 1;
	}
	list_iterator_destroy(itr);

	if (!set) {
		if (assoc->acct)
			debug("UID %u Acct %s has no associations", assoc->uid,
			      assoc->acct);
		else
			debug("UID %u has no associations", assoc->uid);

		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return ESLURM_INVALID_ACCOUNT;
	}
	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_tres(void *db_conn,
				  slurmdb_tres_rec_t *tres,
				  int enforce,
				  slurmdb_tres_rec_t **tres_pptr,
				  bool locked)
{
	list_itr_t *itr;
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
		    ((!xstrncasecmp(tres->type, "gres/", 5) ||
		      !xstrncasecmp(tres->type, "license/", 8))
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
	} else if ((!list_count(assoc_mgr_assoc_list)) &&
		   !(enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return SLURM_SUCCESS;

	if (!assoc->id) {
		if (!assoc->acct) {
			slurmdb_user_rec_t user = { .uid = assoc->uid };

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
			if (assoc_mgr_fill_in_user(db_conn, &user,
						   enforce, NULL, locked)
			    == SLURM_ERROR) {
				if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("User %u not found", assoc->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %u not found", assoc->uid);
					return SLURM_SUCCESS;
				}
			}
			assoc->user = user.name;
			if (user.default_acct)
				assoc->acct = user.default_acct;
			else {
				if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("User %s(%u) doesn't have a default account",
					      assoc->user, assoc->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %s(%u) doesn't have a default account",
					       assoc->user, assoc->uid);
					return SLURM_SUCCESS;
				}
			}
		}

		if (!assoc->cluster)
			assoc->cluster = slurm_conf.cluster_name;
	}
	debug5("%s: looking for assoc of user=%s(%u), acct=%s, cluster=%s, partition=%s",
	       __func__, assoc->user, assoc->uid, assoc->acct, assoc->cluster,
	       assoc->partition);
	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));


	/* First look for the assoc with a partition and then check
	 * for the non-partition association if we don't find one.
	 */
	ret_assoc = _find_assoc_rec(assoc);
	if (!ret_assoc && assoc->partition &&
	    !(assoc->flags & ASSOC_FLAG_EXACT)) {
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
	debug3("%s: found correct association of user=%s(%u), acct=%s, cluster=%s, partition=%s to assoc=%u acct=%s",
	       __func__, assoc->user, assoc->uid, assoc->acct, assoc->cluster,
	       assoc->partition, ret_assoc->id, ret_assoc->acct);
	if (assoc_pptr)
		*assoc_pptr = ret_assoc;

	assoc->id              = ret_assoc->id;

	if (!assoc->acct)
		assoc->acct    = ret_assoc->acct;

	if (!assoc->cluster)
		assoc->cluster = ret_assoc->cluster;

	assoc->comment = ret_assoc->comment;
	assoc->def_qos_id = ret_assoc->def_qos_id;

	assoc->flags = ret_assoc->flags;

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

	if (!assoc->lineage)
		assoc->lineage = ret_assoc->lineage;

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

	assoc->priority = ret_assoc->priority;

	assoc->rgt              = ret_assoc->rgt;

	assoc->shares_raw       = ret_assoc->shares_raw;

	assoc->uid              = ret_assoc->uid;

	/* Don't send any usage info since we don't know if the usage
	   is really in existence here, if they really want it they can
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
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { .user = READ_LOCK };

	if (user_pptr)
		*user_pptr = NULL;

	if (!locked) {
		if (!assoc_mgr_user_list &&
		    _get_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	} else {
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			xassert(assoc_mgr_user_list);
	}

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(USER_LOCK, READ_LOCK));

	if ((!assoc_mgr_user_list || !list_count(assoc_mgr_user_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	if (!(found_user = list_find_first_ro(assoc_mgr_user_list,
					      _list_find_user, user))) {
		if (!locked)
			assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("%s: found correct user: %s(%u)",
	       __func__, found_user->name, found_user->uid);
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
	list_itr_t *itr = NULL;
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
	   is really in existence here, if they really want it they can
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
	qos->limit_factor = found_qos->limit_factor;

	if (!locked)
		assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_wckey(void *db_conn, slurmdb_wckey_rec_t *wckey,
				   int enforce,
				   slurmdb_wckey_rec_t **wckey_pptr,
				   bool locked)
{
	list_itr_t *itr = NULL;
	slurmdb_wckey_rec_t * found_wckey = NULL;
	slurmdb_wckey_rec_t * ret_wckey = NULL;
	assoc_mgr_lock_t locks = { .wckey = READ_LOCK };

	if (wckey_pptr)
		*wckey_pptr = NULL;

	if (!assoc_mgr_wckey_list) {
		int rc = SLURM_SUCCESS;

		if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
			error("No WCKey list available, this should never happen");
			rc = SLURM_ERROR;
		}

		return rc;
	} else if (!list_count(assoc_mgr_wckey_list) &&
		   !(enforce & ACCOUNTING_ENFORCE_WCKEYS))
		return SLURM_SUCCESS;

	if (!wckey->id) {
		if (!wckey->name) {
			slurmdb_user_rec_t user =
				{ .uid = wckey->uid, .name = wckey->user };

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
			if (assoc_mgr_fill_in_user(db_conn, &user,
						   enforce, NULL, locked)
			    == SLURM_ERROR) {
				if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
					error("User %u not found", wckey->uid);
					return SLURM_ERROR;
				} else {
					debug3("User %u not found", wckey->uid);
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
			wckey->cluster = slurm_conf.cluster_name;
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
		/* only and always check for on the slurmdbd */
		if (slurmdbd_conf) {
			if (!wckey->cluster) {
				error("No cluster name was given "
				      "to check against, "
				      "we need one to get a wckey.");
				continue;
			}

			if (xstrcasecmp(wckey->cluster, found_wckey->cluster)) {
				debug4("not the right cluster");
				continue;
			}
		}

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
	return _get_admin_level_internal(db_conn, uid, false);
}

extern slurmdb_admin_level_t assoc_mgr_get_admin_level_locked(void *db_conn,
							      uint32_t uid)
{
	return _get_admin_level_internal(db_conn, uid, true);
}

extern list_t *assoc_mgr_acct_coords(void *db_conn, char *acct_name)
{
	assoc_mgr_lock_t locks = { .user = READ_LOCK };
	find_coord_t find_coord = { .req = acct_name };

	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return NULL;

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_coord_list || !list_count(assoc_mgr_coord_list)) {
		assoc_mgr_unlock(&locks);
		return NULL;
	}

	(void) list_for_each(assoc_mgr_coord_list,
			     _list_find_coord, &find_coord);

	assoc_mgr_unlock(&locks);

	return find_coord.ret_list;
}

extern list_t *assoc_mgr_user_acct_coords(void *db_conn, char *user_name)
{
	assoc_mgr_lock_t locks = { .user = READ_LOCK };
	slurmdb_user_rec_t req_user = { .name = user_name, .uid = NO_VAL };
	slurmdb_user_rec_t *user;
	list_t *ret_list = NULL;

	assoc_mgr_lock(&locks);

	xassert(assoc_mgr_coord_list);

	if (!list_count(assoc_mgr_coord_list)) {
		assoc_mgr_unlock(&locks);
		return NULL;
	}

	user = list_find_first_ro(assoc_mgr_coord_list,
				  _list_find_user, &req_user);

	if (user && user->coord_accts)
		ret_list = slurmdb_list_copy_coord(user->coord_accts);

	assoc_mgr_unlock(&locks);

	return ret_list;
}

extern bool assoc_mgr_is_user_acct_coord(void *db_conn,
					 uint32_t uid,
					 char *acct_name,
					 bool is_locked)
{
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { .user = READ_LOCK };
	bool found = false;

	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return false;

	if (!is_locked)
		assoc_mgr_lock(&locks);
	if (!assoc_mgr_coord_list || !list_count(assoc_mgr_coord_list)) {
		if (!is_locked)
			assoc_mgr_unlock(&locks);
		return false;
	}

	found_user = list_find_first_ro(assoc_mgr_coord_list,
					_list_find_uid, &uid);

	found = assoc_mgr_is_user_acct_coord_user_rec(found_user, acct_name);

	if (!is_locked)
		assoc_mgr_unlock(&locks);
	return found;
}

extern bool assoc_mgr_is_user_acct_coord_user_rec(slurmdb_user_rec_t *user,
						  char *acct_name)
{
	if (!user)
		return false;

	if (!user->coord_accts || !list_count(user->coord_accts))
		return false;

	/*
	 * If the acct_name == NULL we are only checking to see if they are a
	 * coord of anything.
	 */
	if (!acct_name)
		return true;

	if (list_find_first(user->coord_accts, _find_acct_by_name, acct_name))
		return true;

	return false;
}

extern void assoc_mgr_get_shares(void *db_conn,
				 uid_t uid, shares_request_msg_t *req_msg,
				 shares_response_msg_t *resp_msg)
{
	list_itr_t *itr = NULL;
	list_itr_t *user_itr = NULL;
	list_itr_t *acct_itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	assoc_shares_object_t *share = NULL;
	list_t *ret_list = NULL;
	char *tmp_char = NULL;
	slurmdb_user_rec_t user = { .uid = uid };
	int is_admin=1;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .tres = READ_LOCK };

	xassert(resp_msg);

	if (!assoc_mgr_assoc_list || !list_count(assoc_mgr_assoc_list))
		return;

	if (req_msg) {
		if (req_msg->user_list && list_count(req_msg->user_list))
			user_itr = list_iterator_create(req_msg->user_list);

		if (req_msg->acct_list && list_count(req_msg->acct_list))
			acct_itr = list_iterator_create(req_msg->acct_list);
	}

	if (slurm_conf.private_data & PRIVATE_DATA_USAGE) {
		is_admin = 0;
		/* Check permissions of the requesting user.
		 */
		if ((uid == slurm_conf.slurm_user_id || uid == 0)
		    || assoc_mgr_get_admin_level(db_conn, uid)
		    >= SLURMDB_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if (assoc_mgr_fill_in_user(
				    db_conn, &user,
				    ACCOUNTING_ENFORCE_ASSOCS, NULL, false)
			    == SLURM_ERROR) {
				debug3("User %u not found", user.uid);
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

		if (slurm_conf.private_data & PRIVATE_DATA_USAGE) {
			if (!is_admin) {
				list_itr_t *itr = NULL;
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

		share->usage_tres_raw = xcalloc(g_tres_count,
						sizeof(long double));
		memcpy(share->usage_tres_raw,
		       assoc->usage->usage_tres_raw,
		       sizeof(long double) * g_tres_count);

		share->tres_grp_mins = xcalloc(g_tres_count, sizeof(uint64_t));
		memcpy(share->tres_grp_mins, assoc->grp_tres_mins_ctld,
		       sizeof(uint64_t) * g_tres_count);
		share->tres_run_secs = xcalloc(g_tres_count, sizeof(uint64_t));
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

extern buf_t *assoc_mgr_info_get_pack_msg(
	assoc_mgr_info_request_msg_t *msg, uid_t uid,
	void *db_conn, uint16_t protocol_version)
{
	list_itr_t *itr = NULL;
	list_itr_t *user_itr = NULL, *acct_itr = NULL, *qos_itr = NULL;
	slurmdb_qos_rec_t *qos_rec = NULL;
	slurmdb_assoc_rec_t *assoc_rec = NULL;
	list_t *ret_list = NULL, *tmp_list;
	char *tmp_char = NULL;
	slurmdb_user_rec_t user = { .uid = uid };
	slurmdb_user_rec_t *user_rec = NULL;
	int is_admin=1;
	void *object;
	uint32_t flags = 0;

	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .res = READ_LOCK,
				   .tres = READ_LOCK, .user = READ_LOCK };
	buf_t *buffer = NULL;

	if (msg) {
		if (msg->user_list && list_count(msg->user_list))
			user_itr = list_iterator_create(msg->user_list);

		if (msg->acct_list && list_count(msg->acct_list))
			acct_itr = list_iterator_create(msg->acct_list);

		if (msg->qos_list && list_count(msg->qos_list))
			qos_itr = list_iterator_create(msg->qos_list);
		flags = msg->flags;
	}

	if (slurm_conf.private_data &
	    (PRIVATE_DATA_USAGE | PRIVATE_DATA_USERS)) {
		is_admin = 0;
		/* Check permissions of the requesting user.
		 */
		if ((uid == slurm_conf.slurm_user_id || uid == 0)
		    || assoc_mgr_get_admin_level(db_conn, uid)
		    >= SLURMDB_ADMIN_OPERATOR)
			is_admin = 1;
		else {
			if (assoc_mgr_fill_in_user(
				    db_conn, &user,
				    ACCOUNTING_ENFORCE_ASSOCS, NULL, false)
			    == SLURM_ERROR) {
				debug3("User %u not found", user.uid);
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

		if (slurm_conf.private_data & PRIVATE_DATA_USAGE) {
			if (!is_admin) {
				list_itr_t *itr = NULL;
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
		if (!is_admin &&
		    (slurm_conf.private_data & PRIVATE_DATA_USERS) &&
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

end_it:
	if (user_itr)
		list_iterator_destroy(user_itr);
	if (acct_itr)
		list_iterator_destroy(acct_itr);
	if (qos_itr)
		list_iterator_destroy(qos_itr);

	return buffer;
}

extern int assoc_mgr_info_unpack_msg(assoc_mgr_info_msg_t **object,
				     buf_t *buffer, uint16_t protocol_version)
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

extern int assoc_mgr_update_object(void *x, void *arg)
{
	slurmdb_update_object_t *object = x;
	bool locked = *(bool *)arg;
	int rc = SLURM_SUCCESS;

	if (!object->objects || !list_count(object->objects))
		return rc;

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
	case SLURMDB_UPDATE_QOS_USAGE:
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
		/*
		 * These are used in the accounting_storage
		 * plugins for rollback purposes, just skip here.
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
		error("unknown type set in update_object: %d",
		      object->type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

/*
 * assoc_mgr_update - update the association manager
 * IN update_list: updates to perform
 * RET: error code
 * NOTE: the items in update_list are not deleted
 */
extern int assoc_mgr_update(list_t *update_list, bool locked)
{
	int rc = SLURM_SUCCESS;

	xassert(update_list);
	(void) list_for_each(update_list,
			     assoc_mgr_update_object,
			     &locked);
	return rc;
}

extern int assoc_mgr_update_assocs(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_assoc_rec_t * rec = NULL;
	slurmdb_assoc_rec_t * object = NULL;
	list_itr_t *itr = NULL;
	int rc = SLURM_SUCCESS, i;
	int parents_changed = 0;
	int run_update_resvs = 0;
	int resort = 0;
	int redo_priority = 0;
	list_t *remove_list = NULL;
	list_t *update_list = NULL;
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
		if (object->cluster && !slurmdbd_conf) {
			/* only update the local clusters assocs */
			if (xstrcasecmp(object->cluster,
					slurm_conf.cluster_name)) {
				slurmdb_destroy_assoc_rec(object);
				continue;
			}
		} else if (!slurmdbd_conf) {
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
				error("SLURMDB_MODIFY_ASSOC: assoc %u(%s, %s, %s) not found, unable to update.",
				     object->id, object->acct,
				     object->user, object->partition);
				rc = SLURM_ERROR;
				break;
			}

			if (object->comment) {
				xfree(rec->comment);
				if (object->comment[0]) {
					rec->comment = object->comment;
					object->comment = NULL;
				}
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

			/* flags is always set */
			rec->flags = object->flags;

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
					rec->grp_tres, INFINITE64, 1,
					false, NULL);
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
					rec->grp_tres_mins, INFINITE64, 1,
					false, NULL);
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
					rec->grp_tres_run_mins, INFINITE64, 1,
					false, NULL);
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

			if (object->lineage) {
				xfree(rec->lineage);
				rec->lineage = object->lineage;
				object->lineage = NULL;
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
					rec->max_tres_pj, INFINITE64, 1,
					false, NULL);
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
					rec->max_tres_pn, INFINITE64, 1,
					false, NULL);
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
					rec->max_tres_mins_pj, INFINITE64, 1,
					false, NULL);
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
					rec->max_tres_run_mins, INFINITE64, 1,
					false, NULL);
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
				_remove_nondirect_coord_acct(rec);
			}

			if (object->priority != NO_VAL) {
				if (rec->priority == g_assoc_max_priority)
					redo_priority = 2;

				rec->priority = object->priority;

				if ((rec->priority != INFINITE) &&
				    (rec->priority > g_assoc_max_priority)) {
					g_assoc_max_priority = rec->priority;
					redo_priority = 1;
				} else if (redo_priority != 2)
					_set_assoc_norm_priority(rec);
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
						bit_clear_all(
							rec->usage->valid_qos);
					set_qos_bitstr_from_list(
						rec->usage->valid_qos,
						rec->qos_list);
				}
			}

			/* info("rec has def of %d %d", */
			/*      rec->def_qos_id, object->def_qos_id); */
			if (object->def_qos_id == INFINITE) {
				rec->def_qos_id = 0;
			} else if (object->def_qos_id != NO_VAL &&
				   object->def_qos_id >= g_qos_count) {
				error("qos %d doesn't exist",
				      object->def_qos_id);
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
				if (!parents_changed) {
					_set_user_default_acct(rec, NULL);
					_clear_user_default_acct(rec);
				}
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
			/* info("adding %d %s/%s/%s", */
			/*      object->id, object->cluster, */
			/*      object->acct, object->user); */

			if (!object->usage)
				object->usage =
					slurmdb_create_assoc_usage(
						g_tres_count);

			/* Users have no children so leaf is same as total */
			if (object->user)
				object->leaf_usage = object->usage;

			/* If is_def is uninitialized the value will
			   be NO_VAL, so if it isn't 1 make it 0.
			*/
			if (object->is_def != 1)
				object->is_def = 0;

			if ((object->priority != INFINITE) &&
			    (object->priority > g_assoc_max_priority)) {
				g_assoc_max_priority = object->priority;
				redo_priority = 1;
			} else
				_set_assoc_norm_priority(object);

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
			/* info("removing %d %s/%s/%s", */
			/*      rec->id, rec->cluster, rec->acct, rec->user); */
			run_update_resvs = 1; /* needed for updating
						 reservations */

			if (setup_children)
				parents_changed = 1; /* set since we need to
							set the shares
							of surrounding children
						     */

			/*
			 * We don't want to lose the usage data of the user
			 * so we store it directly to its parent assoc.
			 * Otherwise accounts could boost their fairshare by
			 * removing users.
			 */
			if (rec->leaf_usage && rec->usage->parent_assoc_ptr) {
				slurmdb_assoc_rec_t *parent_assoc_ptr =
					rec->usage->parent_assoc_ptr;

				if (!parent_assoc_ptr->leaf_usage)
					parent_assoc_ptr->leaf_usage =
						slurmdb_create_assoc_usage(
							g_tres_count);

				_addto_used_info(parent_assoc_ptr->leaf_usage,
						 rec->leaf_usage);
			}

			/* We need to renormalize of something else */
			if (rec->priority == g_assoc_max_priority)
				redo_priority = 2;

			_remove_nondirect_coord_acct(rec);

			/*
			 * Remove the pointer from the children_list or
			 * any call removing a parent could get an illegal read
			 * when _remove_nondirect_coord_acct() is called and
			 * this rec is still in it's children_list.
			 */
			if (rec->usage->parent_assoc_ptr &&
			    rec->usage->parent_assoc_ptr->
			    usage->children_list)
				list_delete_first(rec->usage->parent_assoc_ptr->
						  usage->children_list,
						  slurm_find_ptr_in_list, rec);

			/*
			 * If the root assoc has been removed we need to clear
			 * the short cut pointer.
			 */
			if (rec == assoc_mgr_root_assoc)
				assoc_mgr_root_assoc = NULL;

			_delete_assoc_hash(rec);
			list_remove_first(assoc_mgr_assoc_list,
					  slurm_find_ptr_in_list, rec);
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

	if (redo_priority)
		_calculate_assoc_norm_priorities(redo_priority == 2);

	/* We have to do this after the entire list is processed since
	 * we may have added the parent which wasn't in the list before
	 */
	if (parents_changed) {
		g_user_assoc_count = 0;
		slurmdb_sort_hierarchical_assoc_list(assoc_mgr_assoc_list);

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

			_set_assoc_parent_and_user(object);

			if (addit)
				_add_assoc_hash(object);
		}
		/* Now that we have set up the parents correctly we
		   can update the used limits
		*/
		list_iterator_reset(itr);
		while ((object = list_next(itr))) {
			/*
			 * This needs to run for all since we could had removed
			 * some that need to be added back (different clusters
			 * have different paths).
			 */
			_add_potential_coord_children(object);

			if (setup_children) {
				list_t *children = object->usage->children_list;
				if (!children || list_is_empty(children))
					goto is_user;

				_set_children_level_shares(
					object,
					_get_children_level_shares(object));
			}
		is_user:
			if (!object->leaf_usage)
				continue;

			/* Add usage of formerly deleted child assocs*/
			if (object->leaf_usage != object->usage)
				_addto_used_info(object->usage,
						 object->leaf_usage);
			rec = object;
			/* look for a parent since we are starting at
			   the parent instead of the child
			*/
			while (object->usage->parent_assoc_ptr) {
				/* we need to get the parent first
				   here since we start at the child
				*/
				object = object->usage->parent_assoc_ptr;

				_addto_used_info(object->usage,
						 rec->leaf_usage);
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
		slurmdb_sort_hierarchical_assoc_list(assoc_mgr_assoc_list);

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
	list_itr_t *itr = NULL;
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
		if (object->cluster && !slurmdbd_conf) {
			/* only update the local clusters assocs */
			if (xstrcasecmp(object->cluster,
					slurm_conf.cluster_name)) {
				slurmdb_destroy_wckey_rec(object);
				continue;
			}
		} else if (!slurmdbd_conf) {
			error("We don't have a cluster here, no "
			      "idea if this is our wckey.");
			continue;
		}

		list_iterator_reset(itr);
		while ((rec = list_next(itr))) {
			/* only and always check for on the slurmdbd */
			if (slurmdbd_conf &&
			    xstrcasecmp(object->cluster, rec->cluster)) {
				debug4("not the right cluster");
				continue;
			}
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
				break;
			}
		}
		//info("%d WCKEY %u", update->type, object->id);
		switch(update->type) {
		case SLURMDB_MODIFY_WCKEY:
			if (!rec) {
				error("SLURMDB_MODIFY_WCKEY: wckey %u(%s) not found, unable to update.",
				     object->id, object->name);
				rc = SLURM_ERROR;
				break;
			}

			if (object->is_def != NO_VAL16) {
				rec->is_def = object->is_def;
				if (rec->is_def)
					_set_user_default_wckey(rec, NULL);
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
				_set_user_default_wckey(object, NULL);
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

	list_itr_t *itr = NULL;
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
				error("SLURMDB_MODIFY_USER: user %s not found, unable to update.",
				      object->old_name ?
				      object->old_name : object->name);
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
			_handle_new_user_coord(object);
			object = NULL;
			break;
		case SLURMDB_REMOVE_USER:
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_first(assoc_mgr_coord_list,
					  slurm_find_ptr_in_list, rec);
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

			_handle_new_user_coord(rec);
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

	list_itr_t *itr = NULL, *assoc_itr = NULL;

	slurmdb_assoc_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;
	bool resize_qos_bitstr = 0;
	int redo_priority = 0;
	list_t *remove_list = NULL;
	list_t *update_list = NULL;
	assoc_mgr_lock_t locks = {
		.assoc = WRITE_LOCK,
		.qos = WRITE_LOCK,
		.tres = READ_LOCK,
	};

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
		bool relative = false;

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

			if (object->flags & QOS_FLAG_RELATIVE)
				assoc_mgr_set_qos_tres_relative_cnt(object, NULL);
			else
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
				error("SLURMDB_MODIFY_QOS: qos %u(%s) not found, unable to update.",
				      object->id, object->name);
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

			relative = (rec->flags & QOS_FLAG_RELATIVE);

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
					INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->grp_tres_mins, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->grp_tres_run_mins, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->max_tres_pa, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->max_tres_pj, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->max_tres_pn, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->max_tres_pu, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->max_tres_mins_pj, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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
					rec->min_tres_pj, INFINITE64, 1,
					relative, rec->relative_tres_cnt);
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

			if (object->preempt_exempt_time != NO_VAL)
				rec->preempt_exempt_time =
					object->preempt_exempt_time;

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

			if (!fuzzy_equal(object->limit_factor, NO_VAL))
				rec->limit_factor = object->limit_factor;

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
		case SLURMDB_UPDATE_QOS_USAGE:
		{
			long double raw_usage =
				object->usage ? object->usage->usage_raw : 0.0;
			if (!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			assoc_mgr_update_qos_usage(rec, raw_usage);
			break;
		}
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

			bit_realloc(object->preempt_bitstr, g_qos_count);
		}
		if (assoc_mgr_assoc_list) {
			assoc_itr = list_iterator_create(
				assoc_mgr_assoc_list);
			while ((assoc = list_next(assoc_itr))) {
				if (!assoc->usage->valid_qos)
					continue;
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

/*
 * NOTE: This function does not currently work for the slurmdbd.
 */
extern int assoc_mgr_update_res(slurmdb_update_object_t *update, bool locked)
{
	slurmdb_res_rec_t *rec = NULL;
	slurmdb_res_rec_t *object = NULL;

	list_itr_t *itr = NULL;
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
		if (!slurmdbd_conf && object->clus_res_rec) {
			if (!object->clus_res_rec->cluster) {
				error("Resource doesn't have a cluster name?");
				slurmdb_destroy_res_rec(object);
				continue;
			} else if (xstrcmp(object->clus_res_rec->cluster,
					   slurm_conf.cluster_name)) {
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

			if (object->last_consumed != NO_VAL)
				rec->last_consumed = object->last_consumed;

			if (object->type != SLURMDB_RESOURCE_NOTSET)
				rec->type = object->type;

			if (object->clus_res_rec->allowed != NO_VAL)
				rec->clus_res_rec->allowed =
					object->clus_res_rec->allowed;

			rec->last_update = object->last_update;

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

	list_itr_t *itr = NULL;
	list_t *tmp_list;
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

	/*
	 * NULL is fine for cluster_name here as this is only called in the
	 * slurmctld where it doesn't matter.  If this changes this will also
	 * have to change.
	 */
	xassert(!slurmdbd_conf);
	found_assoc = _find_assoc_rec_id(assoc_id, NULL);
	assoc_mgr_unlock(&locks);

	if (found_assoc || !(enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern void assoc_mgr_clear_used_info(void)
{
	list_itr_t *itr = NULL;
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

static void _reset_children_usages(list_t *children_list)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	list_itr_t *itr = NULL;
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

		slurmdb_destroy_assoc_usage(assoc->leaf_usage);
		assoc->leaf_usage = NULL;

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
	slurmdb_tres_rec_t tres_rec = {0};

	xassert(tres_cnt);

	if (!tres_str || !tres_str[0])
		return;

	if (tmp_str[0] == ',')
		tmp_str++;

	while (tmp_str) {
		id = atoi(tmp_str);
		/* 0 isn't a valid tres id */
		if (id <= 0) {
			error("%s: no id found at %s instead",
			      __func__, tmp_str);
			break;
		}
		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("%s: no value found %s",
			      __func__, tres_str);
			break;
		}

		tres_rec.id = id;
		pos = assoc_mgr_find_tres_pos(&tres_rec, true);
		if (pos != -1) {
			/* set the index to the count */
			tres_cnt[pos] = strtold(++tmp_str, &endptr);
		} else {
			debug("%s: no tres of id %u found in the array",
			      __func__, tres_rec.id);
		}
		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}


	return;
}

static int _foreach_tres_pos_set_cnt(void *x, void *key)
{
	slurmdb_tres_rec_t *tres_rec = x;
	foreach_tres_pos_t *foreach_tres_pos = key;
	int pos = assoc_mgr_find_tres_pos(tres_rec, foreach_tres_pos->locked);

	if (pos == -1) {
		debug2("%s: no tres of id %u found in the array",
		       __func__, tres_rec->id);
		return 0;
	}
	/*
	 * If Relative make the number absolute based on
	 * the relative_tres_cnt[pos]
	 */
	if (foreach_tres_pos->relative && foreach_tres_pos->relative_tres_cnt &&
	    (tres_rec->count != INFINITE64)) {
		/* Sanity check for max possible. */
		if (tres_rec->count > 100)
			tres_rec->count = 100;

		tres_rec->count *= foreach_tres_pos->relative_tres_cnt[pos];

		/* This will truncate/round down */
		tres_rec->count /= 100;
	}
	/* set the index to the count */
	(*foreach_tres_pos->tres_cnt)[pos] = tres_rec->count;
	/* info("%d pos %d has count of %"PRIu64, */
	/*      tres_rec->id, */
	/*      pos, tres_rec->count); */

	return 0;
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

	slurmdb_destroy_assoc_usage(sav_assoc->leaf_usage);
	sav_assoc->leaf_usage = NULL;

/*
 *	The assoc is an account, so reset all children
 */
	_reset_children_usages(sav_assoc->usage->children_list);
}

extern void assoc_mgr_update_qos_usage(slurmdb_qos_rec_t *qos,
				       long double new_usage)
{
	int i;

	xassert(qos);
	xassert(qos->usage);

	if (new_usage) {
		info("Setting RawUsage for QOS %s from %Lf to %Lf",
		     qos->name, qos->usage->usage_raw, new_usage);
		qos->usage->usage_raw = new_usage;
		return;
	} else
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
	static uint32_t high_buffer_size = (1024 * 1024);
	int error_code = 0;
	char *tmp_char = NULL;
	buf_t *buffer = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .file = WRITE_LOCK,
				   .qos = READ_LOCK, .res = READ_LOCK,
				   .tres = READ_LOCK, .user = READ_LOCK,
				   .wckey = READ_LOCK};
	DEF_TIMERS;

	START_TIMER;

	/* now make a file for last_tres */
	buffer = init_buf(high_buffer_size);

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	assoc_mgr_lock(&locks);
	if (assoc_mgr_tres_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_tres_list };
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_TRES, buffer);
	}

	error_code = save_buf_to_state("last_tres", buffer, NULL);

	FREE_NULL_BUFFER(buffer);

	/* Now write the rest of the lists */
	buffer = init_buf(high_buffer_size);

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_user_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_user_list };
		/* let us know what to unpack */
		pack16(DBD_ADD_USERS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_USERS, buffer);
	}

	if (assoc_mgr_res_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_res_list };
		/* let us know what to unpack */
		pack16(DBD_ADD_RES, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_RES, buffer);
	}

	if (assoc_mgr_qos_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_qos_list };
		/* let us know what to unpack */
		pack16(DBD_ADD_QOS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_QOS, buffer);
	}

	if (assoc_mgr_wckey_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_wckey_list };
		/* let us know what to unpack */
		pack16(DBD_ADD_WCKEYS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_WCKEYS, buffer);
	}
	/* this needs to be done last so qos is set up
	 * before hand when loading it back */
	if (assoc_mgr_assoc_list) {
		dbd_list_msg_t msg = { .my_list = assoc_mgr_assoc_list };
		/* let us know what to unpack */
		pack16(DBD_ADD_ASSOCS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_ASSOCS, buffer);
	}

	/* write the buffer to file */
	error_code = save_buf_to_state("assoc_mgr_state", buffer, NULL);
	FREE_NULL_BUFFER(buffer);
	/* now make a file for assoc_usage */

	buffer = init_buf(high_buffer_size);
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_assoc_list) {
		list_itr_t *itr = NULL;
		slurmdb_assoc_rec_t *assoc = NULL;
		itr = list_iterator_create(assoc_mgr_assoc_list);
		while ((assoc = list_next(itr))) {
			if (!assoc->leaf_usage)
				continue;

			pack32(assoc->id, buffer);
			packlongdouble(assoc->leaf_usage->usage_raw, buffer);
			tmp_char = _make_usage_tres_raw_str(
				assoc->leaf_usage->usage_tres_raw);
			packstr(tmp_char, buffer);
			xfree(tmp_char);
			pack32(assoc->leaf_usage->grp_used_wall, buffer);
		}
		list_iterator_destroy(itr);
	}

	error_code = save_buf_to_state("assoc_usage", buffer, NULL);

	FREE_NULL_BUFFER(buffer);
	/* now make a file for qos_usage */

	buffer = init_buf(high_buffer_size);
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_qos_list) {
		list_itr_t *itr = NULL;
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

	error_code = save_buf_to_state("qos_usage", buffer, NULL);
	assoc_mgr_unlock(&locks);

	FREE_NULL_BUFFER(buffer);
	END_TIMER2("dump_assoc_mgr_state");
	return error_code;

}

extern int load_assoc_usage(void)
{
	int i;
	uint16_t ver = 0;
	char *state_file, *tmp_str = NULL;
	buf_t *buffer = NULL;
	time_t buf_time;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .file = READ_LOCK };

	if (!assoc_mgr_assoc_list)
		return SLURM_SUCCESS;

	/* read the file */
	state_file = xstrdup(slurm_conf.state_save_location);
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
			fatal("Can not recover assoc_usage state, incompatible version, got %u need >= %u <= %u, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.",
			      ver, SLURM_MIN_PROTOCOL_VERSION,
			      SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover assoc_usage state, "
		      "incompatible version, got %u need >= %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);

	while (remaining_buf(buffer) > 0) {
		uint32_t assoc_id = 0;
		uint32_t grp_used_wall = 0;
		long double usage_raw = 0;
		slurmdb_assoc_rec_t *assoc = NULL;
		long double usage_tres_raw[g_tres_count];

		safe_unpack32(&assoc_id, buffer);
		safe_unpacklongdouble(&usage_raw, buffer);
		safe_unpackstr(&tmp_str, buffer);
		safe_unpack32(&grp_used_wall, buffer);

		/*
		 * NULL is fine for cluster_name here as this is only called in
		 * the slurmctld where it doesn't matter.  If this changes this
		 * will also have to change.
		 */
		xassert(!slurmdbd_conf);
		assoc = _find_assoc_rec_id(assoc_id, NULL);

		/* We want to do this all the way up to and including
		   root.  This way we can keep track of how much usage
		   has occurred on the entire system and use that to
		   normalize against.
		*/
		if (assoc) {
			memset(usage_tres_raw, 0, sizeof(usage_tres_raw));
			_set_usage_tres_raw(usage_tres_raw, tmp_str);
			if (!assoc->leaf_usage)
				assoc->leaf_usage = slurmdb_create_assoc_usage(
					g_tres_count);
			assoc->leaf_usage->grp_used_wall = grp_used_wall;
			assoc->leaf_usage->usage_raw = usage_raw;
			for (i = 0; i < g_tres_count; i++)
				assoc->leaf_usage->usage_tres_raw[i] =
					usage_tres_raw[i];
			if (assoc->leaf_usage == assoc->usage)
				assoc = assoc->usage->parent_assoc_ptr;
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

	FREE_NULL_BUFFER(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete assoc usage state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete assoc usage state file");

	FREE_NULL_BUFFER(buffer);

	xfree(tmp_str);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_qos_usage(void)
{
	uint16_t ver = 0;
	char *state_file, *tmp_str = NULL;
	buf_t *buffer = NULL;
	time_t buf_time;
	list_itr_t *itr = NULL;
	assoc_mgr_lock_t locks = { .file = READ_LOCK, .qos = WRITE_LOCK };

	if (!assoc_mgr_qos_list)
		return SLURM_SUCCESS;

	/* read the file */
	state_file = xstrdup(slurm_conf.state_save_location);
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
			      "got %u need >= %u <= %u, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover qos_usage state, "
		      "incompatible version, got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while (remaining_buf(buffer) > 0) {
		uint32_t qos_id = 0;
		uint32_t grp_used_wall = 0;
		long double usage_raw = 0;
		slurmdb_qos_rec_t *qos = NULL;

		safe_unpack32(&qos_id, buffer);
		safe_unpacklongdouble(&usage_raw, buffer);
		safe_unpackstr(&tmp_str, buffer);
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

	FREE_NULL_BUFFER(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete QOS usage state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete QOS usage state file");

	FREE_NULL_BUFFER(buffer);

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
	buf_t *buffer = NULL;
	time_t buf_time;
	dbd_list_msg_t *msg = NULL;
	assoc_mgr_lock_t locks = { .tres = WRITE_LOCK, .qos = WRITE_LOCK };

	/* read the file Always ignore .old file */
	state_file = xstrdup_printf("%s/last_tres",
				    slurm_conf.state_save_location);
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
			fatal("Can not recover last_tres state, incompatible version, got %u need >= %u <= %u, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover last_tres state, incompatible version, got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
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
	FREE_NULL_BUFFER(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete last_tres state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete last_tres state file");

	FREE_NULL_BUFFER(buffer);

	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_assoc_mgr_state(void)
{
	int error_code = SLURM_SUCCESS;
	uint16_t type = 0;
	uint16_t ver = 0;
	char *state_file;
	buf_t *buffer = NULL;
	time_t buf_time;
	dbd_list_msg_t *msg = NULL;
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .file = READ_LOCK,
				   .qos = WRITE_LOCK, .res = WRITE_LOCK,
				   .tres = WRITE_LOCK, .user = WRITE_LOCK,
				   .wckey = WRITE_LOCK };

	/* read the file */
	state_file = xstrdup(slurm_conf.state_save_location);
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
			      "got %u need >= %u <= %u, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.",
			      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover assoc_mgr state, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	while (remaining_buf(buffer) > 0) {
		safe_unpack16(&type, buffer);
		switch(type) {
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
	}

	if (init_setup.running_cache)
		*init_setup.running_cache = RUNNING_CACHE_STATE_RUNNING;

	FREE_NULL_BUFFER(buffer);
	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete assoc mgr state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete assoc mgr state file");

	FREE_NULL_BUFFER(buffer);

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
		*init_setup.running_cache = RUNNING_CACHE_STATE_LISTS_REFRESHED;

	return SLURM_SUCCESS;
}

static int _each_assoc_set_uid(void *x, void *arg)
{
	slurmdb_assoc_rec_t *assoc = x;
	slurmdb_user_rec_t *user = arg;

	if ((assoc->uid != NO_VAL) || xstrcmp(assoc->user, user->name))
		return 0;

	/*
	 * Since the uid changed the hash will change.
	 * Remove it, change it, then insert it.
	*/
	_delete_assoc_hash(assoc);
	assoc->uid = user->uid;
	_add_assoc_hash(assoc);

	if (assoc->is_def)
		_set_user_default_acct(assoc, user);

	return 0;
}

static int _each_wckey_set_uid(void *x, void *arg)
{
	slurmdb_wckey_rec_t *wckey = x;
	slurmdb_user_rec_t *user = arg;

	if ((wckey->uid != NO_VAL) || xstrcmp(wckey->user, user->name))
		return 0;

	wckey->uid = user->uid;
	if (wckey->is_def)
		_set_user_default_wckey(wckey, user);

	return 0;
}

extern void assoc_mgr_set_uid(uid_t uid, char *username)
{
	assoc_mgr_lock_t read_lock = { .user = READ_LOCK };
	assoc_mgr_lock_t write_locks = {
		.assoc = WRITE_LOCK,
		.user = WRITE_LOCK,
		.wckey = WRITE_LOCK
	};
	slurmdb_user_rec_t lookup = { .uid = NO_VAL, .name = username };
	slurmdb_user_rec_t *user = NULL;

	/*
	 * Check if we know about this uid already. If so, exit sooner.
	 */
	assoc_mgr_lock(&read_lock);
	if (!assoc_mgr_user_list) {
		debug("%s: missing assoc_mgr_user_list", __func__);
		assoc_mgr_unlock(&read_lock);
		return;
	}

	if (list_find_first_ro(assoc_mgr_user_list, _list_find_uid, &uid)) {
		debug2("%s: uid=%u already known", __func__, uid);
		assoc_mgr_unlock(&read_lock);
		return;
	}
	assoc_mgr_unlock(&read_lock);

	assoc_mgr_lock(&write_locks);
	if (!assoc_mgr_user_list) {
		debug("%s: missing assoc_mgr_user_list", __func__);
		assoc_mgr_unlock(&write_locks);
		return;
	}

	if (!(user = list_find_first(assoc_mgr_user_list, _list_find_user,
				     &lookup))) {
		debug2("%s: user %s not in assoc_mgr_user_list",
		       __func__, username);
		assoc_mgr_unlock(&write_locks);
		return;
	}

	debug2("%s: adding mapping for user %s uid %u",
	       __func__, username, uid);
	user->uid = uid;

	if (assoc_mgr_assoc_list)
		list_for_each(assoc_mgr_assoc_list, _each_assoc_set_uid, user);
	if (assoc_mgr_wckey_list)
		list_for_each(assoc_mgr_wckey_list, _each_wckey_set_uid, user);
	assoc_mgr_unlock(&write_locks);
}

static int _for_each_assoc_missing_uids(void *x, void *arg)
{
	slurmdb_assoc_rec_t *object = x;
	uid_t pw_uid;

	if (!object->user || (object->uid != NO_VAL))
		return 1;

	if (uid_from_string(object->user, &pw_uid) < 0) {
		debug2("%s: refresh association couldn't get a uid for user %s",
		       __func__, object->user);
	} else {
		/*
		 * Since the uid changed the hash will change.
		 * Remove the assoc from the hash, then add it back.
		*/
		_delete_assoc_hash(object);
		object->uid = pw_uid;
		_add_assoc_hash(object);
		debug3("%s: found uid %u for user %s",
		       __func__, pw_uid, object->user);
	}

	return 1;
}

static int _for_each_wckey_missing_uids(void *x, void *arg)
{
	slurmdb_wckey_rec_t *object = x;
	uid_t pw_uid;

	if (!object->user || (object->uid != NO_VAL))
		return 1;

	if (uid_from_string(object->user, &pw_uid) < 0) {
		debug2("%s: refresh wckey couldn't get a uid for user %s",
		       __func__, object->user);
	} else {
		object->uid = pw_uid;
		debug3("%s: found uid %u for user %s",
		       __func__, pw_uid, object->name);
	}

	return 1;
}

static int _for_each_user_missing_uids(void *x, void *arg)
{
	slurmdb_user_rec_t *object = x;
	uid_t pw_uid;

	if (!object->name || (object->uid != NO_VAL))
		return 1;

	if (uid_from_string(object->name, &pw_uid) < 0) {
		debug2("%s: refresh user couldn't get uid for user %s",
		       __func__, object->name);
	} else {
		debug3("%s: found uid %u for user %s",
		       __func__, pw_uid, object->name);
		object->uid = pw_uid;
	}

	return 1;
}

extern int assoc_mgr_set_missing_uids(void)
{
	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .user = WRITE_LOCK,
				   .wckey = WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_assoc_list) {
		list_for_each(assoc_mgr_assoc_list,
			      _for_each_assoc_missing_uids, NULL);
	}

	if (assoc_mgr_wckey_list) {
		list_for_each(assoc_mgr_wckey_list,
			      _for_each_wckey_missing_uids, NULL);
	}

	if (assoc_mgr_user_list) {
		list_for_each(assoc_mgr_user_list,
			      _for_each_user_missing_uids, NULL);
	}
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* you should check for assoc == NULL before this function */
extern void assoc_mgr_normalize_assoc_shares(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc);
	/*
	 * Use slurm_conf.priority_flags directly instead of using a
	 * global flags variable. assoc_mgr_init() would be the logical
	 * place to set a global, but there is no great location for
	 * resetting it when scontrol reconfigure is called
	 */
	if (slurm_conf.priority_flags & PRIORITY_FLAGS_FAIR_TREE)
		_normalize_assoc_shares_fair_tree(assoc);
	else
		_normalize_assoc_shares_traditional(assoc);
}

/*
 * Find the position of the given TRES ID or type/name in the
 * assoc_mgr_tres_array. If the TRES name or ID isn't found -1 is returned.
 */
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

	for (i = 0; i < g_tres_count; i++) {
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

/*
 * Find the position of the given TRES name in the
 * assoc_mgr_tres_array. Ignore anything after ":" in the TRES name.
 * So tres_rec->name of "gpu" can match accounting TRES name of "gpu:tesla".
 * If the TRES name isn't found -1 is returned.
 */
extern int assoc_mgr_find_tres_pos2(slurmdb_tres_rec_t *tres_rec, bool locked)
{
	int i, len, tres_pos = -1;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!tres_rec->type)
		return tres_pos;

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(assoc_mgr_tres_array);
	xassert(g_tres_count);
	xassert(assoc_mgr_tres_array[g_tres_count - 1]);

	len = strlen(tres_rec->name);
	for (i = 0; i < g_tres_count; i++) {
		if (xstrcasecmp(assoc_mgr_tres_array[i]->type, tres_rec->type))
			continue;
		if (xstrncasecmp(assoc_mgr_tres_array[i]->name, tres_rec->name,
				 len) ||
		    (assoc_mgr_tres_array[i]->name[len] != ':'))
			continue;
		tres_pos = i;
		break;
	}

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_pos;
}

/*
 * Calls assoc_mgr_find_tres_pos and returns the pointer in the
 * assoc_mgr_tres_array.
 * NOTE: The assoc_mgr tres read lock needs to be locked before calling this
 * function and while using the returned record.
 */
extern slurmdb_tres_rec_t *assoc_mgr_find_tres_rec(slurmdb_tres_rec_t *tres_rec)
{
	int pos = assoc_mgr_find_tres_pos(tres_rec, 1);

	if (pos == -1)
		return NULL;
	else
		return assoc_mgr_tres_array[pos];
}

extern int assoc_mgr_set_tres_cnt_array_from_list(
	uint64_t **tres_cnt, list_t *tres_list, bool locked,
	bool relative, uint64_t *relative_tres_cnt)
{
	foreach_tres_pos_t foreach_tres_pos = {
		.locked = locked,
		.relative = relative,
		.relative_tres_cnt = relative_tres_cnt,
		.tres_cnt = tres_cnt,
	};

	if (!tres_list)
		return 0;

	(void) list_for_each(tres_list,
			     _foreach_tres_pos_set_cnt,
			     &foreach_tres_pos);

	if (g_tres_count != list_count(tres_list))
		return 1;

	return 0;
}

extern int assoc_mgr_set_tres_cnt_array(uint64_t **tres_cnt, char *tres_str,
					uint64_t init_val, bool locked,
					bool relative,
					uint64_t *relative_tres_cnt)
{
	int diff_cnt = 0, i;

	xassert(tres_cnt);

	/* When doing the cnt the string is always the
	 * complete string, so always set everything to 0 to
	 * catch anything that was removed.
	 */
	xfree(*tres_cnt);
	if (!init_val)
		*tres_cnt = xcalloc(g_tres_count, sizeof(uint64_t));
	else {
		*tres_cnt = xcalloc_nz(g_tres_count, sizeof(uint64_t));
		for (i=0; i<g_tres_count; i++)
			(*tres_cnt)[i] = init_val;
	}

	if (tres_str) {
		list_t *tmp_list = NULL;
		/* info("got %s", tres_str); */
		slurmdb_tres_list_from_string(
			&tmp_list, tres_str, TRES_STR_FLAG_NONE);
		diff_cnt = assoc_mgr_set_tres_cnt_array_from_list(
			tres_cnt, tmp_list, locked,
			relative, relative_tres_cnt);
		FREE_NULL_LIST(tmp_list);
	}
	return diff_cnt;
}

/* tres read lock needs to be locked before this is called. */
extern void assoc_mgr_set_assoc_tres_cnt(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc_mgr_tres_array);

	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_ctld, assoc->grp_tres,
				     INFINITE64, 1, false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_mins_ctld,
				     assoc->grp_tres_mins, INFINITE64, 1,
				     false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_run_mins_ctld,
				     assoc->grp_tres_run_mins, INFINITE64, 1,
				     false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_ctld,
				     assoc->max_tres_pj, INFINITE64, 1,
				     false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_pn_ctld,
				     assoc->max_tres_pn, INFINITE64, 1,
				     false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_mins_ctld,
				     assoc->max_tres_mins_pj, INFINITE64, 1,
				     false, NULL);
	assoc_mgr_set_tres_cnt_array(&assoc->max_tres_run_mins_ctld,
				     assoc->max_tres_run_mins, INFINITE64, 1,
				     false, NULL);
}

/* tres read and qos write locks need to be locked before this is called. */
extern void assoc_mgr_set_qos_tres_cnt(slurmdb_qos_rec_t *qos)
{
	bool relative;

	/* This isn't needed on the dbd */
	if (slurmdbd_conf)
		return;

	xassert(verify_assoc_lock(QOS_LOCK, WRITE_LOCK));
	xassert(assoc_mgr_tres_array);

	relative = (qos->flags & QOS_FLAG_RELATIVE);

	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_ctld, qos->grp_tres,
				     INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_mins_ctld,
				     qos->grp_tres_mins, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->grp_tres_run_mins_ctld,
				     qos->grp_tres_run_mins, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pa_ctld,
				     qos->max_tres_pa, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pj_ctld,
				     qos->max_tres_pj, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pn_ctld,
				     qos->max_tres_pn, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_pu_ctld,
				     qos->max_tres_pu, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_mins_pj_ctld,
				     qos->max_tres_mins_pj, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_run_mins_pa_ctld,
				     qos->max_tres_run_mins_pa, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->max_tres_run_mins_pu_ctld,
				     qos->max_tres_run_mins_pu, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
	assoc_mgr_set_tres_cnt_array(&qos->min_tres_pj_ctld,
				     qos->min_tres_pj, INFINITE64, 1,
				     relative, qos->relative_tres_cnt);
}

/* qos write and tres read lock needs to be locked before this is called. */
extern void assoc_mgr_set_qos_tres_relative_cnt(slurmdb_qos_rec_t *qos,
						uint64_t *relative_tres_cnt)
{
	xassert(verify_assoc_lock(QOS_LOCK, WRITE_LOCK));
	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));

	if (!(qos->flags & QOS_FLAG_RELATIVE) ||
	    (qos->flags & QOS_FLAG_RELATIVE_SET))
		return;

	xfree(qos->relative_tres_cnt);

	qos->relative_tres_cnt = xcalloc_nz(g_tres_count,
					    sizeof(*qos->relative_tres_cnt));

	if (relative_tres_cnt)
		memcpy(qos->relative_tres_cnt, relative_tres_cnt,
		       sizeof(*relative_tres_cnt) * g_tres_count);
	else {
		for (int i = 0; i < g_tres_count; i++)
			qos->relative_tres_cnt[i] =
				assoc_mgr_tres_array[i]->count;
	}

	assoc_mgr_set_qos_tres_cnt(qos);

	qos->flags |= QOS_FLAG_RELATIVE_SET;
}

/* tres read lock needs to be locked before this is called. */
extern void assoc_mgr_set_unset_qos_tres_relative_cnt(bool locked)
{
	assoc_mgr_lock_t locks = {
		.qos = WRITE_LOCK,
		.tres = READ_LOCK,
	};

	if (!locked)
		assoc_mgr_lock(&locks);

	if (!assoc_mgr_qos_list) {
		if (!(init_setup.enforce & ACCOUNTING_ENFORCE_QOS)) {
			if (!locked)
				assoc_mgr_unlock(&locks);
			return;
		}
		xassert(assoc_mgr_qos_list);
	}

	(void) list_for_each(assoc_mgr_qos_list, _set_relative_cnt, NULL);

	if (!locked)
		assoc_mgr_unlock(&locks);
}

extern void assoc_mgr_clear_qos_tres_relative_cnt(bool locked)
{
	assoc_mgr_lock_t locks = { .qos = WRITE_LOCK };

	if (!locked)
		assoc_mgr_lock(&locks);

	if (!assoc_mgr_qos_list) {
		if (!(init_setup.enforce & ACCOUNTING_ENFORCE_QOS)) {
			if (!locked)
				assoc_mgr_unlock(&locks);
			return;
		}
		xassert(assoc_mgr_qos_list);
	}

	(void) list_for_each(assoc_mgr_qos_list, _reset_relative_flag, NULL);

	if (!locked)
		assoc_mgr_unlock(&locks);
}

extern char *assoc_mgr_make_tres_str_from_array(
	uint64_t *tres_cnt, uint32_t flags, bool locked)
{
	int i;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	uint64_t count;

	if (!tres_cnt)
		return NULL;

	if (!locked)
		assoc_mgr_lock(&locks);

	for (i = 0; i < g_tres_count; i++) {
		if (!assoc_mgr_tres_array[i])
			continue;

		if (flags & TRES_STR_FLAG_ALLOW_REAL) {
			if ((tres_cnt[i] == NO_VAL64) ||
			    (tres_cnt[i] == INFINITE64))
				continue;
		} else if (!tres_cnt[i])
			continue;

		count = tres_cnt[i];

		/* We want to print no_consume with a 0 */
		if (count == NO_CONSUME_VAL64)
			count = 0;

		if (flags & TRES_STR_FLAG_SIMPLE) {
			xstrfmtcat(tres_str, "%s%u=%"PRIu64,
				   tres_str ? "," : "",
				   assoc_mgr_tres_array[i]->id, count);
		} else {
			/* Always skip these when printing out named TRES */
			if ((count == NO_VAL64) ||
			    (count == INFINITE64))
				continue;
			if ((flags & TRES_STR_CONVERT_UNITS) &&
			    ((assoc_mgr_tres_array[i]->id == TRES_MEM) ||
			     !xstrcasecmp(assoc_mgr_tres_array[i]->type,"bb"))){
				char outbuf[32];
				convert_num_unit((double)count, outbuf,
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
				convert_num_unit((double)count, outbuf,
						 sizeof(outbuf), UNIT_NONE,
						 NO_VAL,
						 CONVERT_NUM_UNIT_EXACT);
				xstrfmtcat(tres_str, "%s%s=%s",
					   tres_str ? "," : "",
					   assoc_mgr_tres_name_array[i],
					   outbuf);
			} else {
				xstrfmtcat(tres_str, "%s%s=%"PRIu64,
					   tres_str ? "," : "",
					   assoc_mgr_tres_name_array[i],
					   count);
			}
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

/*
 * Calculate a weighted tres value.
 * IN: tres_cnt - array of tres values of size g_tres_count.
 * IN: weights - weights to apply to tres values of size g_tres_count.
 * IN: flags - priority flags (toogle between MAX or SUM of tres).
 * IN: locked - whether the tres read assoc mgr lock is locked or not.
 * RET: returns the calculated tres weight.
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

		if (tres_cnt[i] == NO_CONSUME_VAL64)
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
extern int assoc_mgr_tres_pos_changed(void)
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

extern bool assoc_mgr_valid_tres_cnt(char *tres, bool gres_tres_enforce)
{
	char *tres_type = NULL, *name = NULL, *type = NULL, *save_ptr = NULL;
	int rc = true, pos = -1;
	uint64_t cnt = 0;

	while (((rc = slurm_get_next_tres(&tres_type,
					  tres,
					  &name, &type,
					  &cnt, &save_ptr)) == SLURM_SUCCESS) &&
	       save_ptr) {
		/*
		 * This is here to handle the old craynetwork:0
		 * Any gres that is formatted correctly and has a count
		 * of 0 is valid to be thrown away but allow job to
		 * allocate.
		 */
		if (gres_tres_enforce && type) {
			xstrfmtcat(name, ":%s", type);
		}
		xfree(type);
		if (cnt == 0) {
			xfree(tres_type);
			xfree(name);
			continue;
		}
		/* gres doesn't have to be a TRES to be valid */
		if (!gres_tres_enforce && !xstrcmp(tres_type, "gres")) {
			pos = gres_valid_name(name) ? 1 : -1;
		} else {
			slurmdb_tres_rec_t tres_rec = {
				.type = tres_type,
				.name = name,
			};
			pos = assoc_mgr_find_tres_pos(&tres_rec, false);
		}
		xfree(tres_type);
		xfree(name);

		if (pos == -1) {
			rc = SLURM_ERROR;
			break;
		}
	}

	return (rc == SLURM_SUCCESS) ? true : false;
}

extern void assoc_mgr_set_job_tres_alloc_str(job_record_t *job_ptr,
					     bool assoc_mgr_locked)
{
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	xassert(job_ptr);

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	xfree(job_ptr->tres_alloc_str);
	job_ptr->tres_alloc_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_alloc_cnt, TRES_STR_FLAG_SIMPLE, true);

	xfree(job_ptr->tres_fmt_alloc_str);
	job_ptr->tres_fmt_alloc_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_alloc_cnt, TRES_STR_CONVERT_UNITS, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

static bool _check_incr(uint32_t a, uint32_t b)
{
	if ((a != NO_VAL) && (a != INFINITE) &&
	    (b != NO_VAL) && (b != INFINITE) &&
	    (a > b))
		return true;
	return false;
}

static bool _find_tres_incr(uint64_t *a, uint64_t *b, int *tres_pos)
{
	for (int i = 0; i < g_tres_count; i++)
		if ((a[i] != NO_VAL64) && (a[i] != INFINITE64) &&
		    (b[i] != NO_VAL64) && (b[i] != INFINITE64) &&
		    (a[i] > b[i])) {
			*tres_pos = i;
			return true;
		}
	return false;
}

static char *_make_tres_str(char *spec, int tres_pos)
{
	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));

	return xstrdup_printf("%s for tres %s", spec,
			      assoc_mgr_tres_name_array[tres_pos]);
}

extern bool assoc_mgr_check_assoc_lim_incr(slurmdb_assoc_rec_t *assoc,
					   char **str)
{
	slurmdb_assoc_rec_t *curr;
	bool rc = false;
	int tres_pos = 0;
	/* tres read lock needed to look up tres name */
	assoc_mgr_lock_t locks = {
		.assoc = READ_LOCK,
		.tres = READ_LOCK,
	};

	assoc_mgr_lock(&locks);

	if (!assoc_mgr_assoc_list)
		goto end_it;

	curr = _find_assoc_rec(assoc);
	if (!curr)
		goto end_it;

	if ((rc = _check_incr(assoc->grp_jobs, curr->grp_jobs))) {
		if (str)
			*str = xstrdup("GrpJobs");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->grp_jobs_accrue, curr->grp_jobs_accrue))) {
		if (str)
			*str = xstrdup("GrpJobsAccrue");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->grp_submit_jobs, curr->grp_submit_jobs))) {
		if (str)
			*str = xstrdup("GrpSubmitJobs");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->grp_wall, curr->grp_wall))) {
		if (str)
			*str = xstrdup("GrpWall");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->max_jobs, curr->max_jobs))) {
		if (str)
			*str = xstrdup("MaxJobs");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->max_jobs_accrue, curr->max_jobs_accrue))) {
		if (str)
			*str = xstrdup("MaxJobsAccrue");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->min_prio_thresh, curr->min_prio_thresh))) {
		if (str)
			*str = xstrdup("MinPrioThreshold");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->max_submit_jobs, curr->max_submit_jobs))) {
		if (str)
			*str = xstrdup("MaxSubmitJobs");
		goto end_it;
	}
	if ((rc = _check_incr(assoc->max_wall_pj, curr->max_wall_pj))) {
		if (str)
			*str = xstrdup("MaxWall");
		goto end_it;
	}
	/* priority 0 is infinite so skip check if so */
	if ((curr->priority != 0) &&
	    (rc = _check_incr(assoc->priority, curr->priority))) {
		if (str)
			*str = xstrdup("Priority");
		goto end_it;
	}

	/* curr assoc will already have *ctld arrays filled in */

	if (assoc->grp_tres) {
		assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_ctld,
					     assoc->grp_tres, INFINITE64, 1,
					     false, NULL);
		if ((rc = _find_tres_incr(assoc->grp_tres_ctld,
					  curr->grp_tres_ctld, &tres_pos))) {
			if (str)
				*str = _make_tres_str("GrpTRES", tres_pos);
			goto end_it;
		}
	}
	if (assoc->grp_tres_mins) {
		assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_mins_ctld,
					     assoc->grp_tres_mins, INFINITE64,
					     1, false, NULL);
		if ((rc = _find_tres_incr(assoc->grp_tres_mins_ctld,
					  curr->grp_tres_mins_ctld,
					  &tres_pos))) {
			if (str)
				*str = _make_tres_str("GrpTRESMins", tres_pos);
			goto end_it;
		}
	}
	if (assoc->grp_tres_run_mins) {
		assoc_mgr_set_tres_cnt_array(&assoc->grp_tres_run_mins_ctld,
					     assoc->grp_tres_run_mins,
					     INFINITE64, 1, false, NULL);
		if ((rc = _find_tres_incr(assoc->grp_tres_run_mins_ctld,
					  curr->grp_tres_run_mins_ctld,
					  &tres_pos))) {
			if (str)
				*str = _make_tres_str("GrpTRESRunMins",
						      tres_pos);
			goto end_it;
		}
	}
	if (assoc->max_tres_mins_pj) {
		assoc_mgr_set_tres_cnt_array(&assoc->max_tres_mins_ctld,
					     assoc->max_tres_mins_pj,
					     INFINITE64,
					     1, false, NULL);
		if ((rc = _find_tres_incr(assoc->max_tres_mins_ctld,
					  curr->max_tres_mins_ctld,
					  &tres_pos))) {
			if (str)
				*str = _make_tres_str("MaxTRESMins", tres_pos);
			goto end_it;
		}
	}
	if (assoc->max_tres_run_mins) {
		assoc_mgr_set_tres_cnt_array(&assoc->max_tres_run_mins_ctld,
					     assoc->max_tres_run_mins,
					     INFINITE64, 1, false, NULL);
		if ((rc = _find_tres_incr(assoc->max_tres_run_mins_ctld,
					  curr->max_tres_run_mins_ctld,
					  &tres_pos))) {
			if (str)
				*str = _make_tres_str("MaxTRESRunMins",
						      tres_pos);
			goto end_it;
		}
	}
	if (assoc->max_tres_pj) {
		assoc_mgr_set_tres_cnt_array(&assoc->max_tres_ctld,
					     assoc->max_tres_pj, INFINITE64, 1,
					     false, NULL);
		if ((rc = _find_tres_incr(assoc->max_tres_ctld,
					  curr->max_tres_ctld, &tres_pos))) {
			if (str)
				*str = _make_tres_str("MaxTRES", tres_pos);
			goto end_it;
		}
	}
	if (assoc->max_tres_pn) {
		assoc_mgr_set_tres_cnt_array(&assoc->max_tres_pn_ctld,
					     assoc->max_tres_pn, INFINITE64, 1,
					     false, NULL);
		if ((rc = _find_tres_incr(assoc->max_tres_pn_ctld,
					  curr->max_tres_pn_ctld, &tres_pos))) {
			if (str)
				*str = _make_tres_str("MaxTRESPn", tres_pos);
		}
	}

end_it:
	assoc_mgr_unlock(&locks);

	return rc;
}

static int _find_qos_not_in_coord_assoc(void *x, void *y)
{
	return list_find_first(y, slurm_find_char_exact_in_list, x) ? 0 : 1;
}

extern int assoc_mgr_find_coord_in_user(void *x, void *y)
{
	slurmdb_coord_rec_t *coord = x;

	return slurm_find_char_exact_in_list(coord->name, y);
}

/* assoc_mgr_lock_t should be clear before coming in here. */
extern bool assoc_mgr_check_coord_qos(char *cluster_name, char *account,
				      char *coord_name, list_t *qos_list)
{
	bool rc = true;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_assoc_rec_t req_assoc = {
		.acct = account,
		.cluster = cluster_name,
		.uid = NO_VAL,
	};
	slurmdb_user_rec_t req_user = {
		.name = coord_name,
		.uid = NO_VAL,
	};
	slurmdb_user_rec_t *user;
	assoc_mgr_lock_t locks = {
		.assoc = READ_LOCK,
		.user = READ_LOCK,
	};

	if (!qos_list || !list_count(qos_list))
		return true;

	assoc_mgr_lock(&locks);

	/* check if coord_name is coord of account name */

	if ((user = list_find_first_ro(assoc_mgr_coord_list,
				       _list_find_user, &req_user))) {
		if (list_find_first(user->coord_accts,
				    assoc_mgr_find_coord_in_user,
				    account)) {
			/*
			 * coord_name is coord of account so get account assoc
			 */
			assoc = _find_assoc_rec(&req_assoc);
		}
	}

	if (!assoc)  {
		/*
		 * coord_name is not coordinator of account name so see if
		 * there's an assoc record for coord_name and the account
		 */
		req_assoc.user = coord_name;
		assoc = _find_assoc_rec(&req_assoc);
		if (!assoc) {
			rc = false;
			goto end_it;
		}
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG2) {
		char *qos_string = slurm_char_list_to_xstr(qos_list);
		debug2("string from qos_list is \"%s\"", qos_string);
		xfree(qos_string);

		qos_string = slurm_char_list_to_xstr(qos_list);
		debug2("string from assoc->qos_list is \"%s\"", qos_string);
		xfree(qos_string);
	}

	/*
	 * see if each qos name in qos_list matches one in
	 * coord->assoc->qos_list
	 */
	if (list_find_first(qos_list, _find_qos_not_in_coord_assoc,
			    assoc->qos_list))
		rc = false;

end_it:
	assoc_mgr_unlock(&locks);

	return rc;
}

extern bool assoc_mgr_tree_has_user_coord(slurmdb_assoc_rec_t *assoc,
					  bool locked)
{
	assoc_mgr_lock_t locks = {
		.assoc = READ_LOCK
	};
	bool rc = false;

	xassert(assoc);
	xassert(assoc->id);

	if (!locked)
		assoc_mgr_lock(&locks);

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));

	/* We don't have an assoc_mgr pointer given, let's find it */
	if (!assoc->usage)
		assoc = _find_assoc_rec(assoc);

	/* See if this assoc or ansestor is making users coordinators */
	while (assoc) {
		if (assoc->flags & ASSOC_FLAG_USER_COORD) {
			rc = true;
			break;
		}
		assoc = assoc->usage->parent_assoc_ptr;
	}

	if (!locked)
		assoc_mgr_unlock(&locks);

	return rc;
}
