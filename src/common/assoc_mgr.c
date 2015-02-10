/*****************************************************************************\
 *  assoc_mgr.c - File to keep track of associations/QOS used by the daemons
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "assoc_mgr.h"

#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>

#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/slurm_priority.h"
#include "src/slurmdbd/read_config.h"

#define ASSOC_USAGE_VERSION 1

#define ASSOC_HASH_SIZE 1000
#define ASSOC_HASH_ID_INX(_assoc_id)	(_assoc_id % ASSOC_HASH_SIZE)

slurmdb_association_rec_t *assoc_mgr_root_assoc = NULL;
uint32_t g_qos_max_priority = 0;
uint32_t g_qos_count = 0;
uint32_t g_user_assoc_count = 0;
List assoc_mgr_association_list = NULL;
List assoc_mgr_res_list = NULL;
List assoc_mgr_qos_list = NULL;
List assoc_mgr_user_list = NULL;
List assoc_mgr_wckey_list = NULL;

static char *assoc_mgr_cluster_name = NULL;
static int setup_children = 0;
static assoc_mgr_lock_flags_t assoc_mgr_locks;
static assoc_init_args_t init_setup;
static slurmdb_association_rec_t **assoc_hash_id = NULL;
static slurmdb_association_rec_t **assoc_hash = NULL;

static pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t locks_cond = PTHREAD_COND_INITIALIZER;

static int _get_str_inx(char *name)
{
	int j, index = 0;

	if (!name)
		return 0;

	for (j = 1; *name; name++, j++)
		index += (int)*name * j;

	return index;
}

static int _assoc_hash_index(slurmdb_association_rec_t *assoc)
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

static void _add_assoc_hash(slurmdb_association_rec_t *assoc)
{
	int inx = ASSOC_HASH_ID_INX(assoc->id);

	if (!assoc_hash_id)
		assoc_hash_id = xmalloc(ASSOC_HASH_SIZE *
					sizeof(slurmdb_association_rec_t *));
	if (!assoc_hash)
		assoc_hash = xmalloc(ASSOC_HASH_SIZE *
				     sizeof(slurmdb_association_rec_t *));

	assoc->assoc_next_id = assoc_hash_id[inx];
	assoc_hash_id[inx] = assoc;

	inx = _assoc_hash_index(assoc);
	assoc->assoc_next = assoc_hash[inx];
	assoc_hash[inx] = assoc;
}

static bool _remove_from_assoc_list(slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *assoc_ptr;
	ListIterator itr = list_iterator_create(assoc_mgr_association_list);

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
static slurmdb_association_rec_t *_find_assoc_rec_id(uint32_t assoc_id)
{
	slurmdb_association_rec_t *assoc;

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
static slurmdb_association_rec_t *_find_assoc_rec(
	slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *assoc_ptr;
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
			debug("we are looking for a nonuser association");
			goto next;
		} else if ((!assoc_ptr->user && (assoc_ptr->uid == NO_VAL))
			   && (assoc->user || (assoc->uid != NO_VAL))) {
			debug("we are looking for a user association");
			goto next;
		} else if (assoc->user && assoc_ptr->user
			   && ((assoc->uid == NO_VAL) ||
			       (assoc_ptr->uid == NO_VAL))) {
			/* This means the uid isn't set in one of the
			 * associations, so use the name instead
			 */
			if (strcasecmp(assoc->user, assoc_ptr->user)) {
				debug("2 not the right user %u != %u",
				      assoc->uid, assoc_ptr->uid);
				goto next;
			}
		} else if (assoc->uid != assoc_ptr->uid) {
			debug("not the right user %u != %u",
			       assoc->uid, assoc_ptr->uid);
			goto next;
		}

		if (assoc->acct &&
		    (!assoc_ptr->acct
		     || strcasecmp(assoc->acct, assoc_ptr->acct))) {
			debug("not the right account %s != %s",
			       assoc->acct, assoc_ptr->acct);
			goto next;
		}

		/* only check for on the slurmdbd */
		if (!assoc_mgr_cluster_name && assoc->cluster
		    && (!assoc_ptr->cluster
			|| strcasecmp(assoc->cluster, assoc_ptr->cluster))) {
			debug("not the right cluster");
			goto next;
		}

		if (assoc->partition
		    && (!assoc_ptr->partition
			|| strcasecmp(assoc->partition,
				      assoc_ptr->partition))) {
			debug("not the right partition");
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
static void _delete_assoc_hash(slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *assoc_ptr = assoc;
	slurmdb_association_rec_t **assoc_pptr;

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
	slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *fs_assoc = assoc;
	double shares_norm = 0.0;
	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		fs_assoc = assoc->usage->fs_assoc_ptr;
	if (fs_assoc->usage->level_shares)
		shares_norm =
			(double)fs_assoc->shares_raw /
			(double)fs_assoc->usage->level_shares;
	assoc->usage->shares_norm = shares_norm;
}


/* you should check for assoc == NULL before this function */
static void _normalize_assoc_shares_traditional(
		slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *assoc2 = assoc;

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
			       (double)assoc->shares_raw /
			       (double)assoc->usage->level_shares);
		}

		assoc = assoc->usage->parent_assoc_ptr;
	}
}


static int _addto_used_info(slurmdb_association_rec_t *assoc1,
			    slurmdb_association_rec_t *assoc2)
{
	if (!assoc1 || !assoc2)
		return SLURM_ERROR;

	assoc1->usage->grp_used_cpus += assoc2->usage->grp_used_cpus;
	assoc1->usage->grp_used_mem += assoc2->usage->grp_used_mem;
	assoc1->usage->grp_used_nodes += assoc2->usage->grp_used_nodes;
	assoc1->usage->grp_used_wall += assoc2->usage->grp_used_wall;
	assoc1->usage->grp_used_cpu_run_secs +=
		assoc2->usage->grp_used_cpu_run_secs;

	assoc1->usage->used_jobs += assoc2->usage->used_jobs;
	assoc1->usage->used_submit_jobs += assoc2->usage->used_submit_jobs;
	assoc1->usage->usage_raw += assoc2->usage->usage_raw;

	return SLURM_SUCCESS;
}

static int _clear_used_assoc_info(slurmdb_association_rec_t *assoc)
{
	if (!assoc || !assoc->usage)
		return SLURM_ERROR;

	assoc->usage->grp_used_cpus = 0;
	assoc->usage->grp_used_mem = 0;
	assoc->usage->grp_used_nodes = 0;
	assoc->usage->grp_used_cpu_run_secs = 0;

	assoc->usage->used_jobs  = 0;
	assoc->usage->used_submit_jobs = 0;
	/* do not reset usage_raw or grp_used_wall.
	 * if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	return SLURM_SUCCESS;
}

static void _clear_qos_user_limit_info(slurmdb_qos_rec_t *qos_ptr)
{
	slurmdb_used_limits_t *used_limits = NULL;
	ListIterator itr = NULL;

	if (!qos_ptr->usage->user_limit_list
	    || !list_count(qos_ptr->usage->user_limit_list))
		return;

	itr = list_iterator_create(qos_ptr->usage->user_limit_list);
	while ((used_limits = list_next(itr))) {
		used_limits->cpu_run_mins = 0; /* Currently isn't used
						  in the code but put
						  here for future
						  reference when/if it
						  is.
					       */
		used_limits->cpus = 0;
		used_limits->jobs = 0;
		used_limits->nodes = 0;
		used_limits->submit_jobs = 0;
	}
	list_iterator_destroy(itr);

	return;
}

static int _clear_used_qos_info(slurmdb_qos_rec_t *qos)
{
	if (!qos || !qos->usage)
		return SLURM_ERROR;

	qos->usage->grp_used_cpus = 0;
	qos->usage->grp_used_mem = 0;
	qos->usage->grp_used_nodes = 0;
	qos->usage->grp_used_cpu_run_secs = 0;

	qos->usage->grp_used_jobs  = 0;
	qos->usage->grp_used_submit_jobs = 0;
	/* do not reset usage_raw or grp_used_wall.
	 * if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	_clear_qos_user_limit_info(qos);

	return SLURM_SUCCESS;
}

/* Locks should be in place before calling this. */
static int _change_user_name(slurmdb_user_rec_t *user)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
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

	if (assoc_mgr_association_list) {
		itr = list_iterator_create(assoc_mgr_association_list);
		while ((assoc = list_next(itr))) {
			if (!assoc->user)
				continue;
			if (!strcmp(user->old_name, assoc->user)) {
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
			if (!strcmp(user->old_name, wckey->user)) {
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

static int _grab_parents_qos(slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *parent_assoc = NULL;
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

static int _local_update_assoc_qos_list(slurmdb_association_rec_t *assoc,
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
				if (!strcmp(curr_qos, new_qos+1)) {
					list_delete_item(curr_qos_itr);
					break;
				}
			}

			list_iterator_reset(curr_qos_itr);
		} else if (new_qos[0] == '+') {
			while ((curr_qos = list_next(curr_qos_itr)))
				if (!strcmp(curr_qos, new_qos+1))
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
static void _set_user_default_acct(slurmdb_association_rec_t *assoc)
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
			    || strcmp(user->default_acct, assoc->acct)) {
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
			    || strcmp(user->default_wckey, wckey->name)) {
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
static slurmdb_association_rec_t* _find_assoc_parent(
	slurmdb_association_rec_t *assoc, bool direct)
{
	slurmdb_association_rec_t *parent = NULL, *prev_parent;
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

/* locks should be put in place before calling this function
 * ASSOC_WRITE, USER_WRITE */
static int _set_assoc_parent_and_user(slurmdb_association_rec_t *assoc,
				      int reset)
{
	xassert(assoc_mgr_user_list);

	if (!assoc || !assoc_mgr_association_list) {
		error("you didn't give me an association");
		return SLURM_ERROR;
	}

	if (!assoc->usage)
		assoc->usage = create_assoc_mgr_association_usage();

	if (assoc->parent_id) {
		/* Here we need the direct parent (parent_assoc_ptr)
		 * and also the first parent that doesn't have
		 * shares_raw == SLURMDB_FS_USE_PARENT (fs_assoc_ptr).
		 */
		assoc->usage->parent_assoc_ptr =
			_find_assoc_parent(assoc, true);
		if (!assoc->usage->parent_assoc_ptr)
			error("Can't find parent id %u for assoc %u, "
			      "this should never happen.",
			      assoc->parent_id, assoc->id);
		else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
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
					create_assoc_mgr_association_usage();
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
	} else {
		slurmdb_association_rec_t *last_root = assoc_mgr_root_assoc;

		assoc_mgr_root_assoc = assoc;
		/* set up new root since if running off cache the
		   total usage for the cluster doesn't get set up again */
		if (last_root) {
			assoc_mgr_root_assoc->usage->usage_raw =
				last_root->usage->usage_raw;
			assoc_mgr_root_assoc->usage->usage_norm =
				last_root->usage->usage_norm;
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
		qos->usage = create_assoc_mgr_qos_usage();
	qos->usage->norm_priority =
		(double)qos->priority / (double)g_qos_max_priority;
}

static uint32_t _get_children_level_shares(slurmdb_association_rec_t *assoc)
{
	List children = assoc->usage->children_list;
	ListIterator itr = NULL;
	slurmdb_association_rec_t *child;
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


static void _set_children_level_shares(slurmdb_association_rec_t *assoc,
				       uint32_t level_shares)
{
	List children = assoc->usage->children_list;
	ListIterator itr = NULL;
	slurmdb_association_rec_t *child;

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
/* locks should be put in place before calling this function
 * ASSOC_WRITE, USER_WRITE */
static int _post_association_list(void)
{
	slurmdb_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;
	int reset = 1;
	//DEF_TIMERS;

	if (!assoc_mgr_association_list)
		return SLURM_ERROR;

	xfree(assoc_hash_id);
	xfree(assoc_hash);

	itr = list_iterator_create(assoc_mgr_association_list);

	//START_TIMER;
	g_user_assoc_count = 0;
	while ((assoc = list_next(itr))) {
		_set_assoc_parent_and_user(assoc, reset);
		_add_assoc_hash(assoc);
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

	slurmdb_sort_hierarchical_assoc_list(assoc_mgr_association_list);

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
			qos->usage = create_assoc_mgr_qos_usage();
		/* get the highest qos value to create bitmaps from */
		if (qos->id > g_qos_count)
			g_qos_count = qos->id;

		if (qos->priority > g_qos_max_priority)
			g_qos_max_priority = qos->priority;
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
					if (!strcasecmp(object->clus_res_rec->
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

static int _get_assoc_mgr_association_list(void *db_conn, int enforce)
{
	slurmdb_association_cond_t assoc_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	if (assoc_mgr_association_list)
		list_destroy(assoc_mgr_association_list);

	memset(&assoc_q, 0, sizeof(slurmdb_association_cond_t));
	if (assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_get_assoc_mgr_association_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

//	START_TIMER;
	assoc_mgr_association_list =
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if (assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);

	if (!assoc_mgr_association_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		assoc_mgr_association_list =
			list_create(slurmdb_destroy_association_rec);
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_association_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing associations and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	}

	_post_association_list();

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_res_list(void *db_conn, int enforce)
{
	slurmdb_res_cond_t res_q;
	uid_t uid = getuid();
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_res_list)
		list_destroy(assoc_mgr_res_list);

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

	if (res_q.cluster_list)
		list_destroy(res_q.cluster_list);

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

	memset(&user_q, 0, sizeof(slurmdb_user_cond_t));
	user_q.with_coords = 1;

	assoc_mgr_lock(&locks);
	if (assoc_mgr_user_list)
		list_destroy(assoc_mgr_user_list);
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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };

//	DEF_TIMERS;
	assoc_mgr_lock(&locks);
	if (assoc_mgr_wckey_list)
		list_destroy(assoc_mgr_wckey_list);

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

	if (wckey_q.cluster_list)
		list_destroy(wckey_q.cluster_list);

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

static int _refresh_assoc_mgr_association_list(void *db_conn, int enforce)
{
	slurmdb_association_cond_t assoc_q;
	List current_assocs = NULL;
	uid_t uid = getuid();
	ListIterator curr_itr = NULL;
	ListIterator assoc_mgr_itr = NULL;
	slurmdb_association_rec_t *curr_assoc = NULL, *assoc = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
//	DEF_TIMERS;

	memset(&assoc_q, 0, sizeof(slurmdb_association_cond_t));
	if (assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if ((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_refresh_assoc_mgr_association_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

	assoc_mgr_lock(&locks);

	current_assocs = assoc_mgr_association_list;

//	START_TIMER;
	assoc_mgr_association_list =
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if (assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);

	if (!assoc_mgr_association_list) {
		assoc_mgr_association_list = current_assocs;
		assoc_mgr_unlock(&locks);

		error("_refresh_assoc_mgr_association_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_association_list();

	if (!current_assocs) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	curr_itr = list_iterator_create(current_assocs);
	assoc_mgr_itr = list_iterator_create(assoc_mgr_association_list);

	/* add used limits We only look for the user associations to
	 * do the parents since a parent may have moved */
	while ((curr_assoc = list_next(curr_itr))) {
		if (!curr_assoc->user)
			continue;
		while ((assoc = list_next(assoc_mgr_itr))) {
			if (assoc->id == curr_assoc->id)
				break;
		}

		while (assoc) {
			_addto_used_info(assoc, curr_assoc);
			/* get the parent last since this pointer is
			   different than the one we are updating from */
			assoc = assoc->usage->parent_assoc_ptr;
		}
		list_iterator_reset(assoc_mgr_itr);
	}

	list_iterator_destroy(curr_itr);
	list_iterator_destroy(assoc_mgr_itr);

	assoc_mgr_unlock(&locks);

	if (current_assocs)
		list_destroy(current_assocs);

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

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

	if (res_q.cluster_list)
		list_destroy(res_q.cluster_list);

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	current_qos = acct_storage_g_get_qos(db_conn, uid, NULL);

	if (!current_qos) {
		error("_refresh_assoc_mgr_qos_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}
	_post_qos_list(current_qos);

	assoc_mgr_lock(&locks);

	if (assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

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

	if (assoc_mgr_user_list)
		list_destroy(assoc_mgr_user_list);

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };

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

	if (wckey_q.cluster_list)
		list_destroy(wckey_q.cluster_list);

	if (!current_wckeys) {
		error("_refresh_assoc_wckey_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_wckey_list(current_wckeys);

	assoc_mgr_lock(&locks);
	if (assoc_mgr_wckey_list)
		list_destroy(assoc_mgr_wckey_list);

	assoc_mgr_wckey_list = current_wckeys;
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/* _wr_rdlock - Issue a read lock on the specified data type */
static void _wr_rdlock(assoc_mgr_lock_datatype_t datatype)
{
	//info("going to read lock on %d", datatype);
	slurm_mutex_lock(&locks_mutex);
	//info("read lock on %d", datatype);
	while (1) {
		if ((assoc_mgr_locks.entity[write_wait_lock(datatype)] ==
		     0)
		    && (assoc_mgr_locks.entity[write_lock(datatype)] ==
			0)) {
			assoc_mgr_locks.entity[read_lock(datatype)]++;
			break;
		} else {	/* wait for state change and retry */
			pthread_cond_wait(&locks_cond, &locks_mutex);
		}
	}
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_rdunlock - Issue a read unlock on the specified data type */
static void _wr_rdunlock(assoc_mgr_lock_datatype_t datatype)
{
	//info("going to read unlock on %d", datatype);
	slurm_mutex_lock(&locks_mutex);
	//info("read unlock on %d", datatype);
	assoc_mgr_locks.entity[read_lock(datatype)]--;
	pthread_cond_broadcast(&locks_cond);
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_wrlock - Issue a write lock on the specified data type */
static void _wr_wrlock(assoc_mgr_lock_datatype_t datatype)
{
	//info("going to write lock on %d", datatype);
	slurm_mutex_lock(&locks_mutex);
	assoc_mgr_locks.entity[write_wait_lock(datatype)]++;

	//info("write lock on %d", datatype);
	while (1) {
		if ((assoc_mgr_locks.entity[read_lock(datatype)] == 0) &&
		    (assoc_mgr_locks.entity[write_lock(datatype)] == 0)) {
			assoc_mgr_locks.entity[write_lock(datatype)]++;
			assoc_mgr_locks.
				entity[write_wait_lock(datatype)]--;
			break;
		} else {	/* wait for state change and retry */
			pthread_cond_wait(&locks_cond, &locks_mutex);
		}
	}
	slurm_mutex_unlock(&locks_mutex);
}

/* _wr_wrunlock - Issue a write unlock on the specified data type */
static void _wr_wrunlock(assoc_mgr_lock_datatype_t datatype)
{
	//info("going to write unlock on %d", datatype);
	slurm_mutex_lock(&locks_mutex);
	//info("write unlock on %d", datatype);
	assoc_mgr_locks.entity[write_lock(datatype)]--;
	pthread_cond_broadcast(&locks_cond);
	slurm_mutex_unlock(&locks_mutex);
}

extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args,
			  int db_conn_errno)
{
	static uint16_t checked_prio = 0;

	if (!checked_prio) {
		char *prio = slurm_get_priority_type();
		if (prio && strcmp(prio, "priority/basic"))
			setup_children = 1;

		xfree(prio);
		checked_prio = 1;
		memset(&assoc_mgr_locks, 0, sizeof(assoc_mgr_locks));
		memset(&init_setup, 0, sizeof(assoc_init_args_t));
		init_setup.cache_level = ASSOC_MGR_CACHE_ALL;
	}

	if (args)
		memcpy(&init_setup, args, sizeof(assoc_init_args_t));

	if (running_cache) {
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

	if ((!assoc_mgr_association_list)
	    && (init_setup.cache_level & ASSOC_MGR_CACHE_ASSOC))
		if (_get_assoc_mgr_association_list(db_conn, init_setup.enforce)
		    == SLURM_ERROR)
			return SLURM_ERROR;

	if (assoc_mgr_association_list && !setup_children) {
		slurmdb_association_rec_t *assoc = NULL;
		ListIterator itr =
			list_iterator_create(assoc_mgr_association_list);
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

extern int assoc_mgr_fini(char *state_save_location)
{
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, WRITE_LOCK, WRITE_LOCK,
				   WRITE_LOCK };

	if (state_save_location)
		dump_assoc_mgr_state(state_save_location);

	assoc_mgr_lock(&locks);

	if (assoc_mgr_association_list)
		list_destroy(assoc_mgr_association_list);
	if (assoc_mgr_res_list)
		list_destroy(assoc_mgr_res_list);
	if (assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);
	if (assoc_mgr_user_list)
		list_destroy(assoc_mgr_user_list);
	if (assoc_mgr_wckey_list)
		list_destroy(assoc_mgr_wckey_list);
	xfree(assoc_mgr_cluster_name);
	assoc_mgr_association_list = NULL;
	assoc_mgr_res_list = NULL;
	assoc_mgr_qos_list = NULL;
	assoc_mgr_user_list = NULL;
	assoc_mgr_wckey_list = NULL;

	assoc_mgr_root_assoc = NULL;
	running_cache = 0;

	xfree(assoc_hash_id);
	xfree(assoc_hash);

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern void assoc_mgr_lock(assoc_mgr_lock_t *locks)
{
	if (locks->assoc == READ_LOCK)
		_wr_rdlock(ASSOC_LOCK);
	else if (locks->assoc == WRITE_LOCK)
		_wr_wrlock(ASSOC_LOCK);

	if (locks->file == READ_LOCK)
		_wr_rdlock(FILE_LOCK);
	else if (locks->file == WRITE_LOCK)
		_wr_wrlock(FILE_LOCK);

	if (locks->qos == READ_LOCK)
		_wr_rdlock(QOS_LOCK);
	else if (locks->qos == WRITE_LOCK)
		_wr_wrlock(QOS_LOCK);

	if (locks->res == READ_LOCK)
		_wr_rdlock(RES_LOCK);
	else if (locks->res == WRITE_LOCK)
		_wr_wrlock(RES_LOCK);

	if (locks->user == READ_LOCK)
		_wr_rdlock(USER_LOCK);
	else if (locks->user == WRITE_LOCK)
		_wr_wrlock(USER_LOCK);

	if (locks->wckey == READ_LOCK)
		_wr_rdlock(WCKEY_LOCK);
	else if (locks->wckey == WRITE_LOCK)
		_wr_wrlock(WCKEY_LOCK);
}

extern void assoc_mgr_unlock(assoc_mgr_lock_t *locks)
{
	if (locks->wckey == READ_LOCK)
		_wr_rdunlock(WCKEY_LOCK);
	else if (locks->wckey == WRITE_LOCK)
		_wr_wrunlock(WCKEY_LOCK);

	if (locks->user == READ_LOCK)
		_wr_rdunlock(USER_LOCK);
	else if (locks->user == WRITE_LOCK)
		_wr_wrunlock(USER_LOCK);

	if (locks->res == READ_LOCK)
		_wr_rdunlock(RES_LOCK);
	else if (locks->res == WRITE_LOCK)
		_wr_wrunlock(RES_LOCK);

	if (locks->qos == READ_LOCK)
		_wr_rdunlock(QOS_LOCK);
	else if (locks->qos == WRITE_LOCK)
		_wr_wrunlock(QOS_LOCK);

	if (locks->file == READ_LOCK)
		_wr_rdunlock(FILE_LOCK);
	else if (locks->file == WRITE_LOCK)
		_wr_wrunlock(FILE_LOCK);

	if (locks->assoc == READ_LOCK)
		_wr_rdunlock(ASSOC_LOCK);
	else if (locks->assoc == WRITE_LOCK)
		_wr_wrunlock(ASSOC_LOCK);
}

extern assoc_mgr_association_usage_t *create_assoc_mgr_association_usage()
{
	assoc_mgr_association_usage_t *usage =
		xmalloc(sizeof(assoc_mgr_association_usage_t));

	usage->level_shares = NO_VAL;
	usage->shares_norm = (double)NO_VAL;
	usage->usage_efctv = 0;
	usage->usage_norm = (long double)NO_VAL;
	usage->usage_raw = 0;
	usage->level_fs = 0;
	usage->fs_factor = 0;

	return usage;
}

extern void destroy_assoc_mgr_association_usage(void *object)
{
	assoc_mgr_association_usage_t *usage =
		(assoc_mgr_association_usage_t *)object;

	if (usage) {
		if (usage->children_list)
			list_destroy(usage->children_list);
		FREE_NULL_BITMAP(usage->valid_qos);

		xfree(usage);
	}
}

extern assoc_mgr_qos_usage_t *create_assoc_mgr_qos_usage()
{
	assoc_mgr_qos_usage_t *usage =
		xmalloc(sizeof(assoc_mgr_qos_usage_t));

	return usage;
}

extern void destroy_assoc_mgr_qos_usage(void *object)
{
	assoc_mgr_qos_usage_t *usage =
		(assoc_mgr_qos_usage_t *)object;

	if (usage) {
		if (usage->job_list)
			list_destroy(usage->job_list);
		if (usage->user_limit_list)
			list_destroy(usage->user_limit_list);
		xfree(usage);
	}
}

/* Since the returned assoc_list is full of pointers from the
 * assoc_mgr_association_list assoc_mgr_lock_t READ_LOCK on
 * associations must be set before calling this function and while
 * handling it after a return.
 */
extern int assoc_mgr_get_user_assocs(void *db_conn,
				     slurmdb_association_rec_t *assoc,
				     int enforce,
				     List assoc_list)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *found_assoc = NULL;
	int set = 0;

	xassert(assoc);
	xassert(assoc->uid != NO_VAL);
	xassert(assoc_list);

	/* Call assoc_mgr_refresh_lists instead of just getting the
	   association list because we need qos and user lists before
	   the association list can be made.
	*/
	if (!assoc_mgr_association_list)
		if (assoc_mgr_refresh_lists(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	if ((!assoc_mgr_association_list
	     || !list_count(assoc_mgr_association_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_association_list);
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

extern int assoc_mgr_fill_in_assoc(void *db_conn,
				   slurmdb_association_rec_t *assoc,
				   int enforce,
				   slurmdb_association_rec_t **assoc_pptr,
				   bool locked)
{
	slurmdb_association_rec_t * ret_assoc = NULL;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (assoc_pptr)
		*assoc_pptr = NULL;

	/* Since we might be locked we can't come in here and try to
	 * get the list since we would need the WRITE_LOCK to do that,
	 * so just return as this would only happen on a system not
	 * talking to the database.
	 */
	if (!assoc_mgr_association_list) {
		int rc = SLURM_SUCCESS;

		if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("No Association list available, "
			      "this should never happen");
			rc = SLURM_ERROR;
		}
		return rc;
	}

	if ((!assoc_mgr_association_list
	     || !list_count(assoc_mgr_association_list))
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
						   enforce, NULL)
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

	assoc->grp_cpu_mins    = ret_assoc->grp_cpu_mins;
	assoc->grp_cpu_run_mins= ret_assoc->grp_cpu_run_mins;
	assoc->grp_cpus        = ret_assoc->grp_cpus;
	assoc->grp_jobs        = ret_assoc->grp_jobs;
	assoc->grp_mem         = ret_assoc->grp_mem;
	assoc->grp_nodes       = ret_assoc->grp_nodes;
	assoc->grp_submit_jobs = ret_assoc->grp_submit_jobs;
	assoc->grp_wall        = ret_assoc->grp_wall;

	assoc->is_def          = ret_assoc->is_def;

	assoc->lft             = ret_assoc->lft;

	assoc->max_cpu_mins_pj = ret_assoc->max_cpu_mins_pj;
	assoc->max_cpu_run_mins= ret_assoc->max_cpu_run_mins;
	assoc->max_cpus_pj     = ret_assoc->max_cpus_pj;
	assoc->max_jobs        = ret_assoc->max_jobs;
	assoc->max_nodes_pj    = ret_assoc->max_nodes_pj;
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
	/* assoc->usage->grp_used_cpus   = ret_assoc->usage->grp_used_cpus; */
	/* assoc->usage->grp_used_cpu_run_mins  = */
	/* 	ret_assoc->usage->grp_used_cpu_run_mins; */
	/* assoc->usage->grp_used_nodes  = ret_assoc->usage->grp_used_nodes; */
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
				  slurmdb_user_rec_t **user_pptr)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	if (user_pptr)
		*user_pptr = NULL;
	if (!assoc_mgr_user_list)
		if (_get_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	assoc_mgr_lock(&locks);
	if ((!assoc_mgr_user_list || !list_count(assoc_mgr_user_list))
	    && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_user_list);
	while ((found_user = list_next(itr))) {
		if (user->uid != NO_VAL) {
			if (user->uid == found_user->uid)
				break;
		} else if (user->name
			   && !strcasecmp(user->name, found_user->name))
			break;
	}
	list_iterator_destroy(itr);

	if (!found_user) {
		assoc_mgr_unlock(&locks);
		if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("found correct user");
	if (user_pptr)
		*user_pptr = found_user;

	/* create coord_accts just incase the list does not exist */
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

	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;

}

extern int assoc_mgr_fill_in_qos(void *db_conn, slurmdb_qos_rec_t *qos,
				 int enforce,
				 slurmdb_qos_rec_t **qos_pptr, bool locked)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t * found_qos = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (qos_pptr)
		*qos_pptr = NULL;

	if (!locked)
		assoc_mgr_lock(&locks);

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
		else if (qos->name && !strcasecmp(qos->name, found_qos->name))
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
	qos->grp_cpu_mins    = found_qos->grp_cpu_mins;
	qos->grp_cpu_run_mins= found_qos->grp_cpu_run_mins;
	qos->grp_cpus        = found_qos->grp_cpus;
	qos->grp_jobs        = found_qos->grp_jobs;
	qos->grp_mem         = found_qos->grp_mem;
	qos->grp_nodes       = found_qos->grp_nodes;
	qos->grp_submit_jobs = found_qos->grp_submit_jobs;
	qos->grp_wall        = found_qos->grp_wall;

	qos->max_cpu_mins_pj = found_qos->max_cpu_mins_pj;
	qos->max_cpu_run_mins_pu = found_qos->max_cpu_run_mins_pu;
	qos->max_cpus_pj     = found_qos->max_cpus_pj;
	qos->max_cpus_pu     = found_qos->max_cpus_pu;
	qos->max_jobs_pu     = found_qos->max_jobs_pu;
	qos->max_nodes_pj    = found_qos->max_nodes_pj;
	qos->max_nodes_pu    = found_qos->max_nodes_pu;
	qos->max_submit_jobs_pu = found_qos->max_submit_jobs_pu;
	qos->max_wall_pj     = found_qos->max_wall_pj;

	qos->min_cpus_pj     = found_qos->min_cpus_pj;

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

	/* qos->usage->grp_used_cpus   = found_qos->usage->grp_used_cpus; */
	/* qos->usage->grp_used_cpu_run_mins  = */
	/* 	found_qos->usage->grp_used_cpu_run_mins; */
	/* qos->usage->grp_used_jobs   = found_qos->usage->grp_used_jobs; */
	/* qos->usage->grp_used_nodes  = found_qos->usage->grp_used_nodes; */
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
				   slurmdb_wckey_rec_t **wckey_pptr)
{
	ListIterator itr = NULL;
	slurmdb_wckey_rec_t * found_wckey = NULL;
	slurmdb_wckey_rec_t * ret_wckey = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

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
						   enforce, NULL)
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
	assoc_mgr_lock(&locks);
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
			} else if (wckey->user && strcasecmp(wckey->user,
							     found_wckey->user))
				continue;

			if (wckey->name
			    && (!found_wckey->name
				|| strcasecmp(wckey->name,
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
				    && strcasecmp(wckey->cluster,
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

	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

extern slurmdb_admin_level_t assoc_mgr_get_admin_level(void *db_conn,
						       uint32_t uid)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t * found_user = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

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
		if (!strcmp(acct_name, acct->name))
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

extern List assoc_mgr_get_shares(void *db_conn,
				 uid_t uid, List acct_list, List user_list)
{
	ListIterator itr = NULL;
	ListIterator user_itr = NULL;
	ListIterator acct_itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	association_shares_object_t *share = NULL;
	List ret_list = NULL;
	char *tmp_char = NULL;
	slurmdb_user_rec_t user;
	int is_admin=1;
	uint16_t private_data = slurm_get_private_data();
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!assoc_mgr_association_list
	    || !list_count(assoc_mgr_association_list))
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (user_list && list_count(user_list))
		user_itr = list_iterator_create(user_list);

	if (acct_list && list_count(acct_list))
		acct_itr = list_iterator_create(acct_list);

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
				    ACCOUNTING_ENFORCE_ASSOCS, NULL)
			    == SLURM_ERROR) {
				debug3("User %d not found", user.uid);
				goto end_it;
			}
		}
	}

	ret_list = list_create(slurm_destroy_association_shares_object);

	assoc_mgr_lock(&locks);
	itr = list_iterator_create(assoc_mgr_association_list);
	while ((assoc = list_next(itr))) {
		if (user_itr && assoc->user) {
			while ((tmp_char = list_next(user_itr))) {
				if (!strcasecmp(tmp_char, assoc->user))
					break;
			}
			list_iterator_reset(user_itr);
			/* not correct user */
			if (!tmp_char)
				continue;
		}

		if (acct_itr) {
			while ((tmp_char = list_next(acct_itr))) {
				if (!strcasecmp(tmp_char, assoc->acct))
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
				    !strcmp(assoc->user, user.name))
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
					if (!strcasecmp(coord->name,
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

		share = xmalloc(sizeof(association_shares_object_t));
		list_append(ret_list, share);

		share->assoc_id = assoc->id;
		share->cluster = xstrdup(assoc->cluster);

		if (assoc == assoc_mgr_root_assoc)
			share->shares_raw = NO_VAL;
		else
			share->shares_raw = assoc->shares_raw;

		share->shares_norm = assoc->usage->shares_norm;
		share->usage_raw = (uint64_t)assoc->usage->usage_raw;

		share->grp_cpu_mins = assoc->grp_cpu_mins;
		share->cpu_run_mins = assoc->usage->grp_used_cpu_run_secs / 60;
		share->fs_factor = assoc->usage->fs_factor;
		share->level_fs = assoc->usage->level_fs;

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
	return ret_list;
}

/*
 * assoc_mgr_update - update the association manager
 * IN update_list: updates to perform
 * RET: error code
 * NOTE: the items in update_list are not deleted
 */
extern int assoc_mgr_update(List update_list)
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
			rc = assoc_mgr_update_users(object);
			break;
		case SLURMDB_ADD_ASSOC:
		case SLURMDB_MODIFY_ASSOC:
		case SLURMDB_REMOVE_ASSOC:
		case SLURMDB_REMOVE_ASSOC_USAGE:
			rc = assoc_mgr_update_assocs(object);
			break;
		case SLURMDB_ADD_QOS:
		case SLURMDB_MODIFY_QOS:
		case SLURMDB_REMOVE_QOS:
		case SLURMDB_REMOVE_QOS_USAGE:
			rc = assoc_mgr_update_qos(object);
			break;
		case SLURMDB_ADD_WCKEY:
		case SLURMDB_MODIFY_WCKEY:
		case SLURMDB_REMOVE_WCKEY:
			rc = assoc_mgr_update_wckeys(object);
			break;
		case SLURMDB_ADD_RES:
		case SLURMDB_MODIFY_RES:
		case SLURMDB_REMOVE_RES:
			rc = assoc_mgr_update_res(object);
			break;
		case SLURMDB_ADD_CLUSTER:
		case SLURMDB_REMOVE_CLUSTER:
			/* These are used in the accounting_storage
			   plugins for rollback purposes, just skip here.
			*/
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

extern int assoc_mgr_update_assocs(slurmdb_update_object_t *update)
{
	slurmdb_association_rec_t * rec = NULL;
	slurmdb_association_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	int parents_changed = 0;
	int run_update_resvs = 0;
	int resort = 0;
	List remove_list = NULL;
	List update_list = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_association_list) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	while ((object = list_pop(update->objects))) {
		bool update_jobs = false;
		if (object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if (strcasecmp(object->cluster,
				       assoc_mgr_cluster_name)) {
				slurmdb_destroy_association_rec(object);
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

			if (object->grp_cpu_mins != (uint64_t)NO_VAL)
				rec->grp_cpu_mins = object->grp_cpu_mins;
			if (object->grp_cpu_run_mins != (uint64_t)NO_VAL)
				rec->grp_cpu_run_mins =
					object->grp_cpu_run_mins;
			if (object->grp_cpus != NO_VAL) {
				update_jobs = true;
				rec->grp_cpus = object->grp_cpus;
			}
			if (object->grp_jobs != NO_VAL)
				rec->grp_jobs = object->grp_jobs;
			if (object->grp_mem != NO_VAL) {
				update_jobs = true;
				rec->grp_mem = object->grp_mem;
			}
			if (object->grp_nodes != NO_VAL) {
				update_jobs = true;
				rec->grp_nodes = object->grp_nodes;
			}
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

			if (object->max_cpu_mins_pj != (uint64_t)NO_VAL)
				rec->max_cpu_mins_pj = object->max_cpu_mins_pj;
			if (object->max_cpu_run_mins != (uint64_t)NO_VAL)
				rec->max_cpu_run_mins =
					object->max_cpu_run_mins;
			if (object->max_cpus_pj != NO_VAL) {
				update_jobs = true;
				rec->max_cpus_pj = object->max_cpus_pj;
			}
			if (object->max_jobs != NO_VAL)
				rec->max_jobs = object->max_jobs;
			if (object->max_nodes_pj != NO_VAL) {
				update_jobs = true;
				rec->max_nodes_pj = object->max_nodes_pj;
			}
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
			/* info("rec has def of %d %d", */
			/*      rec->def_qos_id, object->def_qos_id); */
			if (object->def_qos_id != NO_VAL)
				rec->def_qos_id = object->def_qos_id;

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

			if (object->is_def != (uint16_t)NO_VAL) {
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
					create_assoc_mgr_association_usage();
			/* If is_def is uninitialized the value will
			   be NO_VAL, so if it isn't 1 make it 0.
			*/
			if (object->is_def != 1)
				object->is_def = 0;

			/* Set something so we know to add it to the hash */
			object->uid = INFINITE;

			list_append(assoc_mgr_association_list, object);

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
						slurmdb_destroy_association_rec);
				list_append(remove_list, rec);
			} else
				slurmdb_destroy_association_rec(rec);
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

		slurmdb_destroy_association_rec(object);
	}

	/* We have to do this after the entire list is processed since
	 * we may have added the parent which wasn't in the list before
	 */
	if (parents_changed) {
		int reset = 1;
		g_user_assoc_count = 0;
		slurmdb_sort_hierarchical_assoc_list(
			assoc_mgr_association_list);

		itr = list_iterator_create(assoc_mgr_association_list);
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
				object->usage->grp_used_wall = 0;
			}

			/* This means we were just added, so we need
			   to be added to the hash after the uid is set.
			*/
			if (object->uid == INFINITE)
				addit = true;

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
			assoc_mgr_association_list);

	assoc_mgr_unlock(&locks);

	/* This needs to happen outside of the
	   assoc_mgr_lock */
	if (remove_list) {
		itr = list_iterator_create(remove_list);
		while ((rec = list_next(itr)))
			init_setup.remove_assoc_notify(rec);
		list_iterator_destroy(itr);
		list_destroy(remove_list);
	}

	if (update_list) {
		itr = list_iterator_create(update_list);
		while ((rec = list_next(itr)))
			init_setup.update_assoc_notify(rec);
		list_iterator_destroy(itr);
		list_destroy(update_list);
	}

	if (run_update_resvs && init_setup.update_resvs)
		init_setup.update_resvs();

	return rc;
}

extern int assoc_mgr_update_wckeys(slurmdb_update_object_t *update)
{
	slurmdb_wckey_rec_t * rec = NULL;
	slurmdb_wckey_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_wckey_list) {
		assoc_mgr_unlock(&locks);
		return SLURM_SUCCESS;
	}

	itr = list_iterator_create(assoc_mgr_wckey_list);
	while ((object = list_pop(update->objects))) {
		if (object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if (strcasecmp(object->cluster,
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
					|| strcasecmp(object->name,
						      rec->name))) {
					debug4("not the right wckey");
					continue;
				}

				/* only check for on the slurmdbd */
				if (!assoc_mgr_cluster_name && object->cluster
				    && (!rec->cluster
					|| strcasecmp(object->cluster,
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

			if (object->is_def != (uint16_t)NO_VAL) {
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
	assoc_mgr_unlock(&locks);

	return rc;
}

extern int assoc_mgr_update_users(slurmdb_update_object_t *update)
{
	slurmdb_user_rec_t * rec = NULL;
	slurmdb_user_rec_t * object = NULL;

	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_user_list) {
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
			if (!strcasecmp(name, rec->name))
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
				if (rec->coord_accts)
					list_destroy(rec->coord_accts);
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
	assoc_mgr_unlock(&locks);

	return rc;
}

extern int assoc_mgr_update_qos(slurmdb_update_object_t *update)
{
	slurmdb_qos_rec_t *rec = NULL;
	slurmdb_qos_rec_t *object = NULL;

	ListIterator itr = NULL, assoc_itr = NULL;

	slurmdb_association_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;
	bool resize_qos_bitstr = 0;
	int redo_priority = 0;
	List remove_list = NULL;
	List update_list = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_qos_list) {
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
				object->usage = create_assoc_mgr_qos_usage();
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
			if (object->grp_cpu_mins != (uint64_t)NO_VAL)
				rec->grp_cpu_mins = object->grp_cpu_mins;
			if (object->grp_cpu_run_mins != (uint64_t)NO_VAL)
				rec->grp_cpu_run_mins =
					object->grp_cpu_run_mins;
			if (object->grp_cpus != NO_VAL) {
				update_jobs = true;
				rec->grp_cpus = object->grp_cpus;
			}
			if (object->grp_jobs != NO_VAL)
				rec->grp_jobs = object->grp_jobs;
			if (object->grp_mem != NO_VAL) {
				update_jobs = true;
				rec->grp_mem = object->grp_mem;
			}
			if (object->grp_nodes != NO_VAL) {
				update_jobs = true;
				rec->grp_nodes = object->grp_nodes;
			}
			if (object->grp_submit_jobs != NO_VAL)
				rec->grp_submit_jobs = object->grp_submit_jobs;
			if (object->grp_wall != NO_VAL) {
				update_jobs = true;
				rec->grp_wall = object->grp_wall;
			}

			if (object->max_cpu_mins_pj != (uint64_t)NO_VAL)
				rec->max_cpu_mins_pj = object->max_cpu_mins_pj;
			if (object->max_cpu_run_mins_pu != (uint64_t)NO_VAL)
				rec->max_cpu_run_mins_pu =
					object->max_cpu_run_mins_pu;
			if (object->max_cpus_pj != NO_VAL) {
				update_jobs = true;
				rec->max_cpus_pj = object->max_cpus_pj;
			}
			if (object->max_cpus_pu != NO_VAL) {
				update_jobs = true;
				rec->max_cpus_pu = object->max_cpus_pu;
			}
			if (object->max_jobs_pu != NO_VAL)
				rec->max_jobs_pu = object->max_jobs_pu;
			if (object->max_nodes_pj != NO_VAL) {
				update_jobs = true;
				rec->max_nodes_pj = object->max_nodes_pj;
			}
			if (object->max_nodes_pu != NO_VAL) {
				update_jobs = true;
				rec->max_nodes_pu = object->max_nodes_pu;
			}
			if (object->max_submit_jobs_pu != NO_VAL)
				rec->max_submit_jobs_pu =
					object->max_submit_jobs_pu;
			if (object->max_wall_pj != NO_VAL) {
				update_jobs = true;
				rec->max_wall_pj = object->max_wall_pj;
			}

			if (object->min_cpus_pj != NO_VAL)
				rec->min_cpus_pj = object->min_cpus_pj;

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

			if (object->preempt_mode != (uint16_t)NO_VAL)
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

			if (!assoc_mgr_association_list)
				break;
			/* Remove this qos from all the associations
			   on this cluster.
			*/
			assoc_itr = list_iterator_create(
				assoc_mgr_association_list);
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
		if (assoc_mgr_association_list) {
			assoc_itr = list_iterator_create(
				assoc_mgr_association_list);
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

	assoc_mgr_unlock(&locks);

	/* This needs to happen outside of the
	   assoc_mgr_lock */
	if (remove_list) {
		itr = list_iterator_create(remove_list);
		while ((rec = list_next(itr)))
			init_setup.remove_qos_notify(rec);
		list_iterator_destroy(itr);
		list_destroy(remove_list);
	}

	if (update_list) {
		itr = list_iterator_create(update_list);
		while ((rec = list_next(itr)))
			init_setup.update_qos_notify(rec);
		list_iterator_destroy(itr);
		list_destroy(update_list);
	}

	return rc;
}

extern int assoc_mgr_update_res(slurmdb_update_object_t *update)
{
	slurmdb_res_rec_t *rec = NULL;
	slurmdb_res_rec_t *object = NULL;

	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	assoc_mgr_lock(&locks);
	if (!assoc_mgr_res_list) {
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
			} else if (strcmp(object->clus_res_rec->cluster,
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

			if (object->clus_res_rec->percent_allowed !=
			    (uint16_t)NO_VAL)
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
	assoc_mgr_unlock(&locks);
	return rc;
}

extern int assoc_mgr_validate_assoc_id(void *db_conn,
				       uint32_t assoc_id,
				       int enforce)
{
	slurmdb_association_rec_t * found_assoc = NULL;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* Call assoc_mgr_refresh_lists instead of just getting the
	   association list because we need qos and user lists before
	   the association list can be made.
	*/
	if (!assoc_mgr_association_list)
		if (assoc_mgr_refresh_lists(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	assoc_mgr_lock(&locks);
	if ((!assoc_mgr_association_list
	     || !list_count(assoc_mgr_association_list))
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
	slurmdb_association_rec_t * found_assoc = NULL;
	slurmdb_qos_rec_t * found_qos = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_association_list) {
		itr = list_iterator_create(assoc_mgr_association_list);
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
	slurmdb_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;

	if (!children_list || !list_count(children_list))
		return;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw = 0.0;
		assoc->usage->grp_used_wall = 0.0;
		if (assoc->user)
			continue;

		_reset_children_usages(assoc->usage->children_list);
	}
	list_iterator_destroy(itr);
}

extern void assoc_mgr_remove_assoc_usage(slurmdb_association_rec_t *assoc)
{
	char *child;
	char *child_str;
	long double old_usage_raw = 0.0;
	double old_grp_used_wall = 0.0;
	slurmdb_association_rec_t *sav_assoc = assoc;

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
	xassert(qos);
	xassert(qos->usage);

	info("Resetting usage for QOS %s", qos->name);

	qos->usage->usage_raw = 0;
	qos->usage->grp_used_wall = 0;
	if (!qos->usage->grp_used_cpus)
		qos->usage->grp_used_cpu_run_secs = 0;
}

extern int dump_assoc_mgr_state(char *state_save_location)
{
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	dbd_list_msg_t msg;
	Buf buffer = init_buf(high_buffer_size);
	assoc_mgr_lock_t locks = { READ_LOCK, WRITE_LOCK,
				   READ_LOCK, READ_LOCK, READ_LOCK, READ_LOCK};
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	assoc_mgr_lock(&locks);
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
	if (assoc_mgr_association_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = assoc_mgr_association_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_ASSOCS, buffer);
		slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
				       DBD_ADD_ASSOCS, buffer);
	}

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/assoc_mgr_state", state_save_location);
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
	pack16(ASSOC_USAGE_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_association_list) {
		ListIterator itr = NULL;
		slurmdb_association_rec_t *assoc = NULL;
		itr = list_iterator_create(assoc_mgr_association_list);
		while ((assoc = list_next(itr))) {
			if (!assoc->user)
				continue;

			pack32(assoc->id, buffer);
			/* we only care about the main part here so
			   anything under 1 we are dropping
			*/
			pack64((uint64_t)assoc->usage->usage_raw, buffer);
			pack32(assoc->usage->grp_used_wall, buffer);
		}
		list_iterator_destroy(itr);
	}

	reg_file = xstrdup_printf("%s/assoc_usage", state_save_location);
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
	pack16(ASSOC_USAGE_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if (assoc_mgr_qos_list) {
		ListIterator itr = NULL;
		slurmdb_qos_rec_t *qos = NULL;
		itr = list_iterator_create(assoc_mgr_qos_list);
		while ((qos = list_next(itr))) {
			pack32(qos->id, buffer);
			/* we only care about the main part here so
			   anything under 1 we are dropping
			*/
			pack64((uint64_t)qos->usage->usage_raw, buffer);
			pack32(qos->usage->grp_used_wall, buffer);
		}
		list_iterator_destroy(itr);
	}

	reg_file = xstrdup_printf("%s/qos_usage", state_save_location);
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

extern int load_assoc_usage(char *state_save_location)
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	uint16_t ver = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	assoc_mgr_lock_t locks = { WRITE_LOCK, READ_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!assoc_mgr_association_list)
		return SLURM_SUCCESS;

	/* read the file */
	state_file = xstrdup(state_save_location);
	xstrcat(state_file, "/assoc_usage");	/* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No Assoc usage file (%s) to recover", state_file);
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver != ASSOC_USAGE_VERSION) {
		error("***********************************************");
		error("Can not recover usage_mgr state, incompatible version, "
		      "got %u need %u", ver, ASSOC_USAGE_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);

	while (remaining_buf(buffer) > 0) {
		uint32_t assoc_id = 0;
		uint32_t grp_used_wall = 0;
		uint64_t usage_raw = 0;
		slurmdb_association_rec_t *assoc = NULL;

		safe_unpack32(&assoc_id, buffer);
		safe_unpack64(&usage_raw, buffer);
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
		}

		while (assoc) {
			assoc->usage->grp_used_wall += grp_used_wall;
			assoc->usage->usage_raw += (long double)usage_raw;

			assoc = assoc->usage->parent_assoc_ptr;
		}
	}
	assoc_mgr_unlock(&locks);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (buffer)
		free_buf(buffer);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_qos_usage(char *state_save_location)
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	uint16_t ver = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	ListIterator itr = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, READ_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!assoc_mgr_qos_list)
		return SLURM_SUCCESS;

	/* read the file */
	state_file = xstrdup(state_save_location);
	xstrcat(state_file, "/qos_usage");	/* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No Qos usage file (%s) to recover", state_file);
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver != ASSOC_USAGE_VERSION) {
		error("***********************************************");
		error("Can not recover usage_mgr state, incompatible version, "
		      "got %u need %u", ver, ASSOC_USAGE_VERSION);
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
		uint64_t usage_raw = 0;
		slurmdb_qos_rec_t *qos = NULL;

		safe_unpack32(&qos_id, buffer);
		safe_unpack64(&usage_raw, buffer);
		safe_unpack32(&grp_used_wall, buffer);
		while ((qos = list_next(itr)))
			if (qos->id == qos_id)
				break;
		if (qos) {
			qos->usage->grp_used_wall = grp_used_wall;
			qos->usage->usage_raw = (long double)usage_raw;
		}

		list_iterator_reset(itr);
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (buffer)
		free_buf(buffer);
	if (itr)
		list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int load_assoc_mgr_state(char *state_save_location)
{
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	uint16_t type = 0;
	uint16_t ver = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	dbd_list_msg_t *msg = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, READ_LOCK,
				   WRITE_LOCK, WRITE_LOCK, WRITE_LOCK,
				   WRITE_LOCK };

	/* read the file */
	state_file = xstrdup(state_save_location);
	xstrcat(state_file, "/assoc_mgr_state"); /* Always ignore .old file */
	//info("looking at the %s file", state_file);
	assoc_mgr_lock(&locks);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No association state file (%s) to recover", state_file);
		return ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURMDBD_MIN_VERSION) {
		error("***********************************************");
		error("Can not recover assoc_mgr state, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURMDBD_MIN_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		assoc_mgr_unlock(&locks);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	while (remaining_buf(buffer) > 0) {
		safe_unpack16(&type, buffer);
		switch(type) {
		case DBD_ADD_ASSOCS:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_ASSOCS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No associations retrieved");
				break;
			}
			if (assoc_mgr_association_list)
				list_destroy(assoc_mgr_association_list);
			assoc_mgr_association_list = msg->my_list;
			_post_association_list();

			debug("Recovered %u associations",
			      list_count(assoc_mgr_association_list));
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
			if (assoc_mgr_user_list)
				list_destroy(assoc_mgr_user_list);
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
			if (assoc_mgr_res_list)
				list_destroy(assoc_mgr_res_list);
			assoc_mgr_res_list = msg->my_list;
			_post_res_list(assoc_mgr_res_list);
			debug("Recovered %u resources",
			      list_count(assoc_mgr_res_list));
			msg->my_list = NULL;
			slurmdbd_free_list_msg(msg);
			break;
		case DBD_ADD_QOS:
			error_code = slurmdbd_unpack_list_msg(
				&msg, ver, DBD_ADD_QOS, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if (!msg->my_list) {
				error("No qos retrieved");
				break;
			}
			if (assoc_mgr_qos_list)
				list_destroy(assoc_mgr_qos_list);
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
			if (assoc_mgr_wckey_list)
				list_destroy(assoc_mgr_wckey_list);
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
	running_cache = 1;
	free_buf(buffer);
	assoc_mgr_unlock(&locks);
	return SLURM_SUCCESS;

unpack_error:
	if (buffer)
		free_buf(buffer);
	assoc_mgr_unlock(&locks);
	return SLURM_ERROR;
}

extern int assoc_mgr_refresh_lists(void *db_conn)
{
	/* get qos before association since it is used there */
	if (init_setup.cache_level & ASSOC_MGR_CACHE_QOS)
		if (_refresh_assoc_mgr_qos_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	/* get user before association/wckey since it is used there */
	if (init_setup.cache_level & ASSOC_MGR_CACHE_USER)
		if (_refresh_assoc_mgr_user_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if (init_setup.cache_level & ASSOC_MGR_CACHE_ASSOC) {
		if (_refresh_assoc_mgr_association_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if (init_setup.cache_level & ASSOC_MGR_CACHE_WCKEY)
		if (_refresh_assoc_wckey_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if (init_setup.cache_level & ASSOC_MGR_CACHE_RES)
		if (_refresh_assoc_mgr_res_list(
			    db_conn, init_setup.enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	running_cache = 0;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_set_missing_uids()
{
	uid_t pw_uid;
	ListIterator itr = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, WRITE_LOCK, WRITE_LOCK };

	assoc_mgr_lock(&locks);
	if (assoc_mgr_association_list) {
		slurmdb_association_rec_t *object = NULL;
		itr = list_iterator_create(assoc_mgr_association_list);
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
extern void assoc_mgr_normalize_assoc_shares(slurmdb_association_rec_t *assoc)
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

