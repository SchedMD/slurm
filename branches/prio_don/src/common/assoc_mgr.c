/*****************************************************************************\
 *  accounting_storage_slurmdbd.c - accounting interface to slurmdbd.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/slurmdbd/read_config.h"

#define ASSOC_USAGE_VERSION 1

acct_association_rec_t *assoc_mgr_root_assoc = NULL;
uint32_t qos_max_priority = 0;

List assoc_mgr_association_list = NULL;
List assoc_mgr_qos_list = NULL;
List assoc_mgr_user_list = NULL;
List assoc_mgr_wckey_list = NULL;

static char *assoc_mgr_cluster_name = NULL;
static int setup_childern = 0;

void (*remove_assoc_notify) (acct_association_rec_t *rec) = NULL;

pthread_mutex_t assoc_mgr_association_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t assoc_mgr_qos_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t assoc_mgr_user_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t assoc_mgr_file_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t assoc_mgr_wckey_lock = PTHREAD_MUTEX_INITIALIZER;


/* 
 * Comparator used for sorting assocs largest cpu to smallest cpu
 * 
 * returns: 1: assoc_a > assoc_b  -1: assoc_a < assoc_b
 * 
 */
static int _sort_assoc_dec(acct_association_rec_t *assoc_a,
			   acct_association_rec_t *assoc_b)
{
	if (assoc_a->lft > assoc_b->lft)
		return 1;
	
	return -1;
}

static int _addto_used_info(acct_association_rec_t *assoc1,
			    acct_association_rec_t *assoc2)
{
	if(!assoc1 || !assoc2)
		return SLURM_ERROR;

	assoc1->grp_used_cpu_mins += assoc2->grp_used_cpu_mins;
	assoc1->grp_used_cpus += assoc2->grp_used_cpus;
	assoc1->grp_used_nodes += assoc2->grp_used_nodes;
	assoc1->grp_used_wall += assoc2->grp_used_wall;
	
	assoc1->used_jobs += assoc2->used_jobs;
	assoc1->used_submit_jobs += assoc2->used_submit_jobs;
	assoc1->raw_usage += assoc2->raw_usage;

	return SLURM_SUCCESS;
}

static int _clear_used_info(acct_association_rec_t *assoc)
{
	if(!assoc)
		return SLURM_ERROR;

	assoc->grp_used_cpu_mins = 0;
	assoc->grp_used_cpus = 0;
	assoc->grp_used_nodes = 0;
	assoc->grp_used_wall = 0;
	
	assoc->used_jobs  = 0;
	assoc->used_submit_jobs = 0;
	/* do not reset raw_usage if you need to reset it do it
	 * else where since sometimes we call this and do not want
	 * shares reset */

	return SLURM_SUCCESS;
}

static int _grab_parents_qos(acct_association_rec_t *assoc)
{
	acct_association_rec_t *parent_assoc = NULL;
	char *qos_char = NULL;
	ListIterator itr = NULL;

	if(!assoc)
		return SLURM_ERROR;

	if(assoc->qos_list)
		list_flush(assoc->qos_list);
	else
		assoc->qos_list = list_create(slurm_destroy_char);

	parent_assoc = assoc->parent_assoc_ptr;

	if(!parent_assoc || !parent_assoc->qos_list
	   || !list_count(parent_assoc->qos_list)) 
		return SLURM_SUCCESS;
	
	itr = list_iterator_create(parent_assoc->qos_list);
	while((qos_char = list_next(itr))) 
		list_append(assoc->qos_list, xstrdup(qos_char));
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _local_update_assoc_qos_list(acct_association_rec_t *assoc, 
					List new_qos_list)
{
	ListIterator new_qos_itr = NULL, curr_qos_itr = NULL;
	char *new_qos = NULL, *curr_qos = NULL;
	int flushed = 0;

	if(!assoc || !new_qos_list) {
		error("need both new qos_list and an association to update");
		return SLURM_ERROR;
	}
	
	if(!list_count(new_qos_list)) {
		_grab_parents_qos(assoc);
		return SLURM_SUCCESS;
	}			

	new_qos_itr = list_iterator_create(new_qos_list);
	curr_qos_itr = list_iterator_create(assoc->qos_list);
	
	while((new_qos = list_next(new_qos_itr))) {
		if(new_qos[0] == '-') {
			while((curr_qos = list_next(curr_qos_itr))) {
				if(!strcmp(curr_qos, new_qos+1)) {
					list_delete_item(curr_qos_itr);
					break;
				}
			}

			list_iterator_reset(curr_qos_itr);
		} else if(new_qos[0] == '+') {
			while((curr_qos = list_next(curr_qos_itr))) 
				if(!strcmp(curr_qos, new_qos+1)) 
					break;
			
			if(!curr_qos) {
				list_append(assoc->qos_list,
					    xstrdup(new_qos+1));
				list_iterator_reset(curr_qos_itr);
			}
		} else if(new_qos[0] == '=') {
			if(!flushed)
				list_flush(assoc->qos_list);
			list_append(assoc->qos_list, xstrdup(new_qos+1));
			flushed = 1;
		} else if(new_qos[0]) {
			if(!flushed)
				list_flush(assoc->qos_list);
			list_append(assoc->qos_list, xstrdup(new_qos));
			flushed = 1;			
		}
	}
	list_iterator_destroy(curr_qos_itr);
	list_iterator_destroy(new_qos_itr);

	return SLURM_SUCCESS;	
}

/* locks should be put in place before calling this function */
static int _set_assoc_parent_and_user(acct_association_rec_t *assoc,
				      List assoc_list, int reset)
{
	static acct_association_rec_t *last_acct_parent = NULL;
	static acct_association_rec_t *last_parent = NULL;

	if(reset) {
		last_acct_parent = NULL;
		last_parent = NULL;
	}
	
	if(!assoc || !assoc_list) {
		error("you didn't give me an association");
		return SLURM_ERROR;
	}

	if(assoc->parent_id) {
		/* To speed things up we are first looking if we have
		   a parent_id to look for.  If that doesn't work see
		   if the last parent we had was what we are looking
		   for.  Then if that isn't panning out look at the
		   last account parent.  If still we don't have it we
		   will look for it in the list.  If it isn't there we
		   will just add it to the parent and call it good 
		*/
		if(last_parent && assoc->parent_id == last_parent->id) {
			assoc->parent_assoc_ptr = last_parent;
		} else if(last_acct_parent 
			  && assoc->parent_id == last_acct_parent->id) {
			assoc->parent_assoc_ptr = last_acct_parent;
		} else {
			acct_association_rec_t *assoc2 = NULL;
			ListIterator itr = list_iterator_create(assoc_list);
			while((assoc2 = list_next(itr))) {
				if(assoc2->id == assoc->parent_id) {
					assoc->parent_assoc_ptr = assoc2;
					if(assoc->user) 
						last_parent = assoc2;
					else
						last_acct_parent = assoc2;
					break;
				}
			}
			list_iterator_destroy(itr);
		}
		if(assoc->parent_assoc_ptr && setup_childern) {
			if(!assoc->parent_assoc_ptr->childern_list) 
				assoc->parent_assoc_ptr->childern_list = 
					list_create(NULL);
			list_append(assoc->parent_assoc_ptr->childern_list,
				    assoc);
		}	
			
		if(assoc == assoc->parent_assoc_ptr) {
			assoc->parent_assoc_ptr = NULL;
			error("association %u was pointing to "
			      "itself as it's parent");
		}
	} else 
		assoc_mgr_root_assoc = assoc;
		
	if(assoc->user) {
		uid_t pw_uid = uid_from_string(assoc->user);
		if(pw_uid == (uid_t) -1) 
			assoc->uid = (uint32_t)NO_VAL;
		else
			assoc->uid = pw_uid;	
	} else {
		assoc->uid = (uint32_t)NO_VAL;	
	}
	//log_assoc_rec(assoc);

	return SLURM_SUCCESS;
}

static int _post_association_list(List assoc_list)
{
	acct_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;
	int reset = 1;
	//DEF_TIMERS;

	if(!assoc_list)
		return SLURM_ERROR;

	itr = list_iterator_create(assoc_list);
	//START_TIMER;
	while((assoc = list_next(itr))) {
		_set_assoc_parent_and_user(assoc, assoc_list, reset);
		reset = 0;
	}

	if(setup_childern) {
		acct_association_rec_t *assoc2 = NULL;
		ListIterator itr2 = NULL;
		/* Now set the shares on each level */
		list_iterator_reset(itr);
		while((assoc = list_next(itr))) {
			int count = 0;
			if(!assoc->childern_list
			   || !list_count(assoc->childern_list))
				continue;
			itr2 = list_iterator_create(assoc->childern_list);
			while((assoc2 = list_next(itr2))) 
				count += assoc2->raw_shares;
			list_iterator_reset(itr2);
			while((assoc2 = list_next(itr2))) 
				assoc2->level_shares = count;
			list_iterator_destroy(itr2);
		}	
		/* Now normalize the static shares */
		list_iterator_reset(itr);
		while((assoc = list_next(itr))) {
			assoc2 = assoc;
			assoc2->norm_shares = 1.0;
			/* we don't need to do this for root so stop
			   there */
			while(assoc->parent_assoc_ptr) {
				assoc2->norm_shares *=
					(double)assoc->raw_shares /
					(double)assoc->level_shares;
				assoc = assoc->parent_assoc_ptr;
			}
		}
	}
	list_iterator_destroy(itr);
	//END_TIMER2("load_associations");
	return SLURM_SUCCESS;
}
	
static int _post_user_list(List user_list)
{
	acct_user_rec_t *user = NULL;
	ListIterator itr = list_iterator_create(user_list);
	//START_TIMER;
	while((user = list_next(itr))) {
		uid_t pw_uid = uid_from_string(user->name);
		if(pw_uid == (uid_t) -1) {
			if(slurmdbd_conf)
				debug("post user: couldn't get a "
				      "uid for user %s",
				      user->name);
			user->uid = (uint32_t)NO_VAL;
		} else
			user->uid = pw_uid;
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

static int _post_wckey_list(List wckey_list)
{
	acct_wckey_rec_t *wckey = NULL;
	ListIterator itr = list_iterator_create(wckey_list);
	//START_TIMER;
	while((wckey = list_next(itr))) {
		uid_t pw_uid = uid_from_string(wckey->user);
		if(pw_uid == (uid_t) -1) {
			if(slurmdbd_conf)
				debug("post wckey: couldn't get a uid "
				      "for user %s",
				      wckey->user);
			wckey->uid = (uint32_t)NO_VAL;
		} else
			wckey->uid = pw_uid;
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}
static int _get_assoc_mgr_association_list(void *db_conn, int enforce)
{
	acct_association_cond_t assoc_q;
	uid_t uid = getuid();

//	DEF_TIMERS;
	slurm_mutex_lock(&assoc_mgr_association_lock);
	if(assoc_mgr_association_list)
		list_destroy(assoc_mgr_association_list);

	memset(&assoc_q, 0, sizeof(acct_association_cond_t));
	if(assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_get_assoc_mgr_association_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

//	START_TIMER;
	assoc_mgr_association_list =
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if(assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);
	
	if(!assoc_mgr_association_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		assoc_mgr_association_list = list_create(NULL);
		slurm_mutex_unlock(&assoc_mgr_association_lock);
		if(enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_association_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing associations and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	} 

	_post_association_list(assoc_mgr_association_list);

	slurm_mutex_unlock(&assoc_mgr_association_lock);

	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_qos_list(void *db_conn, int enforce)
{
	uid_t uid = getuid();

	slurm_mutex_lock(&assoc_mgr_qos_lock);
	if(assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);
	assoc_mgr_qos_list = acct_storage_g_get_qos(db_conn, uid, NULL);

	if(!assoc_mgr_qos_list) {
		slurm_mutex_unlock(&assoc_mgr_qos_lock);
		if(enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_qos_list: no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}		
	} else {
		ListIterator itr = list_iterator_create(assoc_mgr_qos_list);
		acct_qos_rec_t *qos = NULL;
		while((qos = list_next(itr))) {
			if(qos->priority > qos_max_priority) 
				qos_max_priority = qos->priority;
		}

		if(qos_max_priority) {
			list_iterator_reset(itr);
			
			while((qos = list_next(itr))) {
				qos->norm_priority = (double)qos->priority 
					/ (double)qos_max_priority;
			}
		}
		list_iterator_destroy(itr);
	}

	slurm_mutex_unlock(&assoc_mgr_qos_lock);
	return SLURM_SUCCESS;
}

static int _get_assoc_mgr_user_list(void *db_conn, int enforce)
{
	acct_user_cond_t user_q;
	uid_t uid = getuid();

	memset(&user_q, 0, sizeof(acct_user_cond_t));
	user_q.with_coords = 1;
	
	slurm_mutex_lock(&assoc_mgr_user_lock);
	if(assoc_mgr_user_list)
		list_destroy(assoc_mgr_user_list);
	assoc_mgr_user_list = acct_storage_g_get_users(db_conn, uid, &user_q);

	if(!assoc_mgr_user_list) {
		slurm_mutex_unlock(&assoc_mgr_user_lock);
		if(enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("_get_assoc_mgr_user_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}		
	} 

	_post_user_list(assoc_mgr_user_list);
	
	slurm_mutex_unlock(&assoc_mgr_user_lock);
	return SLURM_SUCCESS;
}


static int _get_local_wckey_list(void *db_conn, int enforce)
{
	acct_wckey_cond_t wckey_q;
	uid_t uid = getuid();

//	DEF_TIMERS;
	slurm_mutex_lock(&assoc_mgr_wckey_lock);
	if(assoc_mgr_wckey_list)
		list_destroy(assoc_mgr_wckey_list);

	memset(&wckey_q, 0, sizeof(acct_wckey_cond_t));
	if(assoc_mgr_cluster_name) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, assoc_mgr_cluster_name);
	} else if((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("_get_local_wckey_list: "
		      "no cluster name here going to get "
		      "all wckeys.");
	}

//	START_TIMER;
	assoc_mgr_wckey_list =
		acct_storage_g_get_wckeys(db_conn, uid, &wckey_q);
//	END_TIMER2("get_wckeys");
	
	if(wckey_q.cluster_list)
		list_destroy(wckey_q.cluster_list);
	
	if(!assoc_mgr_wckey_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		assoc_mgr_wckey_list = list_create(NULL);
		slurm_mutex_unlock(&assoc_mgr_wckey_lock);
		if(enforce & ACCOUNTING_ENFORCE_WCKEYS) {
			error("_get_local_wckey_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing wckeys and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	} 

	_post_wckey_list(assoc_mgr_wckey_list);
	
	slurm_mutex_unlock(&assoc_mgr_wckey_lock);

	return SLURM_SUCCESS;
}

static int _refresh_assoc_mgr_association_list(void *db_conn, int enforce)
{
	acct_association_cond_t assoc_q;
	List current_assocs = NULL;
	uid_t uid = getuid();
	ListIterator curr_itr = NULL;
	ListIterator assoc_mgr_itr = NULL;
	acct_association_rec_t *curr_assoc = NULL, *assoc = NULL;
//	DEF_TIMERS;

	memset(&assoc_q, 0, sizeof(acct_association_cond_t));
	if(assoc_mgr_cluster_name) {
		assoc_q.cluster_list = list_create(NULL);
		list_append(assoc_q.cluster_list, assoc_mgr_cluster_name);
	} else if((enforce & ACCOUNTING_ENFORCE_ASSOCS) && !slurmdbd_conf) {
		error("_refresh_assoc_mgr_association_list: "
		      "no cluster name here going to get "
		      "all associations.");
	}

	slurm_mutex_lock(&assoc_mgr_association_lock);

	current_assocs = assoc_mgr_association_list;

//	START_TIMER;
	assoc_mgr_association_list = 
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if(assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);
	
	if(!assoc_mgr_association_list) {
		assoc_mgr_association_list = current_assocs;
		slurm_mutex_unlock(&assoc_mgr_association_lock);
		
		error("_refresh_assoc_mgr_association_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_association_list(assoc_mgr_association_list);
	
	if(!current_assocs) {
		slurm_mutex_unlock(&assoc_mgr_association_lock);
		return SLURM_SUCCESS;
	}
	
	curr_itr = list_iterator_create(current_assocs);
	assoc_mgr_itr = list_iterator_create(assoc_mgr_association_list);
	
	/* add used limits We only look for the user associations to
	 * do the parents since a parent may have moved */
	while((curr_assoc = list_next(curr_itr))) {
		if(!curr_assoc->user)
			continue;
		while((assoc = list_next(assoc_mgr_itr))) {
			if(assoc->id == curr_assoc->id) 
				break;
		}

		while(assoc) {
			_addto_used_info(assoc, curr_assoc);
			/* get the parent last since this pointer is
			   different than the one we are updating from */
			assoc = assoc->parent_assoc_ptr;
		}
		list_iterator_reset(assoc_mgr_itr);			
	}
	
	list_iterator_destroy(curr_itr);
	list_iterator_destroy(assoc_mgr_itr);
		
	slurm_mutex_unlock(&assoc_mgr_association_lock);

	if(current_assocs)
		list_destroy(current_assocs);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_mgr_qos_list(void *db_conn, int enforce)
{
	List current_qos = NULL;
	uid_t uid = getuid();

	current_qos = acct_storage_g_get_qos(db_conn, uid, NULL);

	if(!current_qos) {
		error("_refresh_assoc_mgr_qos_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&assoc_mgr_qos_lock);
	if(assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);

	assoc_mgr_qos_list = current_qos;

	slurm_mutex_unlock(&assoc_mgr_qos_lock);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed 
 */
static int _refresh_assoc_mgr_user_list(void *db_conn, int enforce)
{
	List current_users = NULL;
	acct_user_cond_t user_q;
	uid_t uid = getuid();

	memset(&user_q, 0, sizeof(acct_user_cond_t));
	user_q.with_coords = 1;
	
	current_users = acct_storage_g_get_users(db_conn, uid, &user_q);

	if(!current_users) {
		error("_refresh_assoc_mgr_user_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}
	_post_user_list(current_users);

	slurm_mutex_lock(&assoc_mgr_user_lock);

	if(assoc_mgr_user_list) 
		list_destroy(assoc_mgr_user_list);

	assoc_mgr_user_list = current_users;
	
	slurm_mutex_unlock(&assoc_mgr_user_lock);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_assoc_wckey_list(void *db_conn, int enforce)
{
	acct_wckey_cond_t wckey_q;
	List current_wckeys = NULL;
	uid_t uid = getuid();

	memset(&wckey_q, 0, sizeof(acct_wckey_cond_t));
	if(assoc_mgr_cluster_name) {
		wckey_q.cluster_list = list_create(NULL);
		list_append(wckey_q.cluster_list, assoc_mgr_cluster_name);
	} else if((enforce & ACCOUNTING_ENFORCE_WCKEYS) && !slurmdbd_conf) {
		error("_refresh_assoc_wckey_list: "
		      "no cluster name here going to get "
		      "all wckeys.");
	}

	current_wckeys = acct_storage_g_get_wckeys(db_conn, uid, &wckey_q);

	if(!current_wckeys) {
		error("_refresh_assoc_wckey_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	_post_user_list(current_wckeys);

	slurm_mutex_lock(&assoc_mgr_wckey_lock);
	if(assoc_mgr_wckey_list)
		list_destroy(assoc_mgr_wckey_list);

	assoc_mgr_wckey_list = current_wckeys;
	slurm_mutex_unlock(&assoc_mgr_wckey_lock);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args)
{
	static uint16_t enforce = 0;
	static uint16_t cache_level = ASSOC_MGR_CACHE_ALL;
	static uint16_t checked_prio = 0;

	if(!checked_prio) {
		char *prio = slurm_get_priority_type();
		if(prio && !strcmp(prio, "priority/multifactor")) 
			setup_childern = 1;
		
		xfree(prio);
		checked_prio = 1;
	}

	if(args) {
		enforce = args->enforce;
		if(args->remove_assoc_notify)
			remove_assoc_notify = args->remove_assoc_notify;
		cache_level = args->cache_level;
		assoc_mgr_refresh_lists(db_conn, args);	
	}
	
	if(running_cache) { 
		debug4("No need to run assoc_mgr_init, "
		       "we probably don't have a connection.  "
		       "If we do use assoc_mgr_refresh_lists instead.");
		return SLURM_SUCCESS;
	}

	if((!assoc_mgr_cluster_name) && !slurmdbd_conf) {
		xfree(assoc_mgr_cluster_name);
		assoc_mgr_cluster_name = slurm_get_cluster_name();
	}

	/* check if we can't talk to the db yet */
	if(errno == ESLURM_ACCESS_DENIED)
		return SLURM_ERROR;
	
	if((!assoc_mgr_association_list)
	   && (cache_level & ASSOC_MGR_CACHE_ASSOC)) 
		if(_get_assoc_mgr_association_list(db_conn, enforce)
		   == SLURM_ERROR)
			return SLURM_ERROR;
		
	if((!assoc_mgr_qos_list) && (cache_level & ASSOC_MGR_CACHE_QOS))
		if(_get_assoc_mgr_qos_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!assoc_mgr_user_list) && (cache_level & ASSOC_MGR_CACHE_USER))
		if(_get_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	if(assoc_mgr_association_list && !setup_childern) {
		acct_association_rec_t *assoc = NULL;
		ListIterator itr =
			list_iterator_create(assoc_mgr_association_list);
		while((assoc = list_next(itr))) {
			log_assoc_rec(assoc, assoc_mgr_qos_list);
		}
		list_iterator_destroy(itr);
	}
	if((!assoc_mgr_wckey_list) && (cache_level & ASSOC_MGR_CACHE_WCKEY))
		if(_get_local_wckey_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fini(char *state_save_location)
{
	if(state_save_location)
		dump_assoc_mgr_state(state_save_location);

	if(assoc_mgr_association_list) 
		list_destroy(assoc_mgr_association_list);
	if(assoc_mgr_qos_list)
		list_destroy(assoc_mgr_qos_list);
	if(assoc_mgr_user_list)
		list_destroy(assoc_mgr_user_list);
	xfree(assoc_mgr_cluster_name);
	assoc_mgr_association_list = NULL;
	assoc_mgr_qos_list = NULL;
	assoc_mgr_user_list = NULL;
	assoc_mgr_wckey_list = NULL;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_get_user_assocs(void *db_conn,
				     acct_association_rec_t *assoc,
				     int enforce, 
				     List assoc_list)
{
	ListIterator itr = NULL;
	acct_association_rec_t *found_assoc = NULL;
	int set = 1;

	xassert(assoc);
	xassert(assoc->uid != (uint32_t)NO_VAL);
	xassert(assoc_list);

	if(!assoc_mgr_association_list) {
		if(_get_assoc_mgr_association_list(db_conn, enforce) 
		   == SLURM_ERROR)
			return SLURM_ERROR;
	}

	if((!assoc_mgr_association_list
	    || !list_count(assoc_mgr_association_list))
	   && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) 
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc->uid != found_assoc->uid) {
			debug4("not the right user %u != %u",
			       assoc->uid, found_assoc->uid);
			continue;
		}

		list_append(assoc_list, found_assoc);
		set = 1;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);

	if(set)
		return SLURM_SUCCESS;
	else {
		debug("user %u does not have any associations", assoc->uid);
		return SLURM_ERROR;
	}
}

extern int assoc_mgr_fill_in_assoc(void *db_conn, acct_association_rec_t *assoc,
				   int enforce, 
				   acct_association_rec_t **assoc_pptr)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;
	acct_association_rec_t * ret_assoc = NULL;

	if (assoc_pptr)
		*assoc_pptr = NULL;
	if(!assoc_mgr_association_list) {
		if(_get_assoc_mgr_association_list(db_conn, enforce) 
		   == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if((!assoc_mgr_association_list
	    || !list_count(assoc_mgr_association_list))
	   && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) 
		return SLURM_SUCCESS;

	if(!assoc->id) {
		if(!assoc->acct) {
			acct_user_rec_t user;

			if(assoc->uid == (uint32_t)NO_VAL) {
				if(enforce & ACCOUNTING_ENFORCE_ASSOCS) {
					error("get_assoc_id: "
					      "Not enough info to "
					      "get an association");
					return SLURM_ERROR;
				} else {
					return SLURM_SUCCESS;
				}
			}
			memset(&user, 0, sizeof(acct_user_rec_t));
			user.uid = assoc->uid;
			if(assoc_mgr_fill_in_user(db_conn, &user,
						  enforce, NULL) 
			   == SLURM_ERROR) {
				if(enforce & ACCOUNTING_ENFORCE_ASSOCS) 
					return SLURM_ERROR;
				else {
					return SLURM_SUCCESS;
				}
			}					
			assoc->user = user.name;
			assoc->acct = user.default_acct;
		}		
		
		if(!assoc->cluster)
			assoc->cluster = assoc_mgr_cluster_name;
	}
/* 	info("looking for assoc of user=%s(%u), acct=%s, " */
/* 	     "cluster=%s, partition=%s", */
/* 	     assoc->user, assoc->uid, assoc->acct, */
/* 	     assoc->cluster, assoc->partition); */
	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc->id) {
			if(assoc->id == found_assoc->id) {
				ret_assoc = found_assoc;
				break;
			}
			continue;
		} else {
			if(assoc->uid == (uint32_t)NO_VAL
			   && found_assoc->uid != (uint32_t)NO_VAL) {
				debug3("we are looking for a "
				       "nonuser association");
				continue;
			} else if(assoc->uid != found_assoc->uid) {
				debug4("not the right user %u != %u",
				       assoc->uid, found_assoc->uid);
				continue;
			}
			
			if(found_assoc->acct 
			   && strcasecmp(assoc->acct, found_assoc->acct)) {
				debug4("not the right account %s != %s",
				       assoc->acct, found_assoc->acct);
				continue;
			}

			/* only check for on the slurmdbd */
			if(!assoc_mgr_cluster_name && found_assoc->cluster
			   && strcasecmp(assoc->cluster,
					 found_assoc->cluster)) {
				debug4("not the right cluster");
				continue;
			}
	
			if(assoc->partition
			   && (!found_assoc->partition 
			       || strcasecmp(assoc->partition, 
					     found_assoc->partition))) {
				ret_assoc = found_assoc;
				debug3("found association for no partition");
				continue;
			}
		}
		ret_assoc = found_assoc;
		break;
	}
	list_iterator_destroy(itr);
	
	if(!ret_assoc) {
		slurm_mutex_unlock(&assoc_mgr_association_lock);
		if(enforce & ACCOUNTING_ENFORCE_ASSOCS) 
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}
	debug3("found correct association");
	if (assoc_pptr)
		*assoc_pptr = ret_assoc;
	assoc->id = ret_assoc->id;
	if(!assoc->user)
		assoc->user = ret_assoc->user;
	assoc->uid = ret_assoc->uid;

	if(!assoc->acct)
		assoc->acct = ret_assoc->acct;
	if(!assoc->cluster)
		assoc->cluster = ret_assoc->cluster;
	if(!assoc->partition)
		assoc->partition = ret_assoc->partition;

	assoc->raw_shares       = ret_assoc->raw_shares;

	assoc->grp_cpu_mins   = ret_assoc->grp_cpu_mins;
	assoc->grp_cpus        = ret_assoc->grp_cpus;
	assoc->grp_jobs        = ret_assoc->grp_jobs;
	assoc->grp_nodes       = ret_assoc->grp_nodes;
	assoc->grp_submit_jobs = ret_assoc->grp_submit_jobs;
	assoc->grp_wall        = ret_assoc->grp_wall;

	assoc->max_cpu_mins_pj = ret_assoc->max_cpu_mins_pj;
	assoc->max_cpus_pj     = ret_assoc->max_cpus_pj;
	assoc->max_jobs        = ret_assoc->max_jobs;
	assoc->max_nodes_pj    = ret_assoc->max_nodes_pj;
	assoc->max_submit_jobs = ret_assoc->max_submit_jobs;
	assoc->max_wall_pj     = ret_assoc->max_wall_pj;

	if(assoc->parent_acct) {
		xfree(assoc->parent_acct);
		assoc->parent_acct       = xstrdup(ret_assoc->parent_acct);
	} else 
		assoc->parent_acct       = ret_assoc->parent_acct;
	assoc->parent_assoc_ptr          = ret_assoc->parent_assoc_ptr;
	assoc->parent_id                 = ret_assoc->parent_id;

	slurm_mutex_unlock(&assoc_mgr_association_lock);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_user(void *db_conn, acct_user_rec_t *user,
				  int enforce,
				  acct_user_rec_t **user_pptr)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;

	if(user_pptr)
		*user_pptr = NULL;
	if(!assoc_mgr_user_list) 
		if(_get_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR) 
			return SLURM_ERROR;

	if((!assoc_mgr_user_list || !list_count(assoc_mgr_user_list))
	   && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) 
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_user_lock);
	itr = list_iterator_create(assoc_mgr_user_list);
	while((found_user = list_next(itr))) {
		if(user->uid != NO_VAL) {
			if(user->uid == found_user->uid)
				break;
		} else if(user->name 
			  && !strcasecmp(user->name, found_user->name))
			break;
	}
	list_iterator_destroy(itr);

	if(!found_user) {
		slurm_mutex_unlock(&assoc_mgr_user_lock);
		if(enforce) 
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("found correct user");	
	if(user_pptr)
		*user_pptr = found_user;

	/* create coord_accts just incase the list does not exist */
	if(!found_user->coord_accts)
		found_user->coord_accts = list_create(destroy_acct_coord_rec);

	user->admin_level = found_user->admin_level;
	if(!user->assoc_list)
		user->assoc_list = found_user->assoc_list;
	if(!user->coord_accts)
		user->coord_accts = found_user->coord_accts;
	if(!user->default_acct)
		user->default_acct = found_user->default_acct;
	if(!user->default_wckey)
		user->default_wckey = found_user->default_wckey;
	if(!user->name)
		user->name = found_user->name;

	slurm_mutex_unlock(&assoc_mgr_user_lock);
	return SLURM_SUCCESS;

}

extern int assoc_mgr_fill_in_qos(void *db_conn, acct_qos_rec_t *qos,
				 int enforce,
				 acct_qos_rec_t **qos_pptr)
{
	ListIterator itr = NULL;
	acct_qos_rec_t * found_qos = NULL;

	if(qos_pptr)
		*qos_pptr = NULL;
	if(!assoc_mgr_qos_list) 
		if(_get_assoc_mgr_qos_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!assoc_mgr_qos_list 
	    || !list_count(assoc_mgr_qos_list)) && !enforce) 
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_qos_lock);
	itr = list_iterator_create(assoc_mgr_qos_list);
	while((found_qos = list_next(itr))) {
		if(qos->id == found_qos->id) 
			break;
		else if(qos->name && strcasecmp(qos->name, found_qos->name))
			break;
	}
	list_iterator_destroy(itr);
	
	if(!found_qos) {
		slurm_mutex_unlock(&assoc_mgr_qos_lock);
		if(enforce) 
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	debug3("found correct qos");
	if (qos_pptr)
		*qos_pptr = found_qos;

	if(!qos->description)
		qos->description = found_qos->description;

	qos->id = found_qos->id;

	if(!qos->job_flags)
		qos->job_flags = found_qos->job_flags;

	if(!qos->job_list)
		qos->job_list = found_qos->job_list;

	qos->grp_cpu_mins    = found_qos->grp_cpu_mins;
	qos->grp_cpus        = found_qos->grp_cpus;
	qos->grp_jobs        = found_qos->grp_jobs;
	qos->grp_nodes       = found_qos->grp_nodes;
	qos->grp_submit_jobs = found_qos->grp_submit_jobs;
	qos->grp_wall        = found_qos->grp_wall;

	qos->max_cpu_mins_pu = found_qos->max_cpu_mins_pu;
	qos->max_cpus_pu     = found_qos->max_cpus_pu;
	qos->max_jobs_pu     = found_qos->max_jobs_pu;
	qos->max_nodes_pu    = found_qos->max_nodes_pu;
	qos->max_submit_jobs_pu = found_qos->max_submit_jobs_pu;
	qos->max_wall_pu     = found_qos->max_wall_pu;

	if(!qos->name) 
		qos->name = found_qos->name;

	qos->norm_priority = found_qos->norm_priority;

	if(!qos->preemptee_list)
		qos->preemptee_list = found_qos->preemptee_list;
	if(!qos->preemptor_list)
		qos->preemptor_list = found_qos->preemptor_list;

	qos->priority = found_qos->priority;

	if(!qos->user_limit_list)
		qos->user_limit_list = found_qos->user_limit_list;

	slurm_mutex_unlock(&assoc_mgr_qos_lock);
	return SLURM_ERROR;
}

extern int assoc_mgr_fill_in_wckey(void *db_conn, acct_wckey_rec_t *wckey,
				   int enforce, 
				   acct_wckey_rec_t **wckey_pptr)
{
	ListIterator itr = NULL;
	acct_wckey_rec_t * found_wckey = NULL;
	acct_wckey_rec_t * ret_wckey = NULL;

	if (wckey_pptr)
		*wckey_pptr = NULL;
	if(!assoc_mgr_wckey_list) {
		if(_get_local_wckey_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if((!assoc_mgr_wckey_list || !list_count(assoc_mgr_wckey_list))
	   && !(enforce & ACCOUNTING_ENFORCE_WCKEYS)) 
		return SLURM_SUCCESS;

	if(!wckey->id) {
		if(!wckey->name) {
			acct_user_rec_t user;

			if(wckey->uid == (uint32_t)NO_VAL && !wckey->user) {
				if(enforce & ACCOUNTING_ENFORCE_WCKEYS) {
					error("get_wckey_id: "
					      "Not enough info to "
					      "get an wckey");
					return SLURM_ERROR;
				} else {
					return SLURM_SUCCESS;
				}
			}
			memset(&user, 0, sizeof(acct_user_rec_t));
			user.uid = wckey->uid;
			user.name = wckey->user;
			if(assoc_mgr_fill_in_user(db_conn, &user,
						  enforce, NULL) 
			   == SLURM_ERROR) {
				if(enforce & ACCOUNTING_ENFORCE_WCKEYS) 
					return SLURM_ERROR;
				else {
					return SLURM_SUCCESS;
				}
			}
			if(!wckey->user)
				wckey->user = user.name;
			wckey->name = user.default_wckey;
		} else if(wckey->uid == (uint32_t)NO_VAL && !wckey->user) {
			if(enforce & ACCOUNTING_ENFORCE_WCKEYS) {
				error("get_wckey_id: "
				      "Not enough info 2 to "
				      "get an wckey");
				return SLURM_ERROR;
			} else {
				return SLURM_SUCCESS;
			}
		}
			
		
		if(!wckey->cluster)
			wckey->cluster = assoc_mgr_cluster_name;
	}
/* 	info("looking for wckey of user=%s(%u), name=%s, " */
/* 	     "cluster=%s", */
/* 	     wckey->user, wckey->uid, wckey->name, */
/* 	     wckey->cluster); */
	slurm_mutex_lock(&assoc_mgr_wckey_lock);
	itr = list_iterator_create(assoc_mgr_wckey_list);
	while((found_wckey = list_next(itr))) {
		if(wckey->id) {
			if(wckey->id == found_wckey->id) {
				ret_wckey = found_wckey;
				break;
			}
			continue;
		} else {
			if(wckey->uid != NO_VAL) {
				if(wckey->uid != found_wckey->uid) {
					debug4("not the right user %u != %u",
					       wckey->uid, found_wckey->uid);
					continue;
				}
			} else if(wckey->user && strcasecmp(wckey->user,
							    found_wckey->user))
				continue;
			
			if(wckey->name
			   && (!found_wckey->name 
			       || strcasecmp(wckey->name, found_wckey->name))) {
				debug4("not the right name %s != %s",
				       wckey->name, found_wckey->name);
				continue;
			}

			/* only check for on the slurmdbd */
			if(!assoc_mgr_cluster_name) {
				if(!wckey->cluster) {
					error("No cluster name was given "
					      "to check against, "
					      "we need one to get a wckey.");
					continue;
				}
				
				if(found_wckey->cluster
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
	
	if(!ret_wckey) {
		slurm_mutex_unlock(&assoc_mgr_wckey_lock);
		if(enforce & ACCOUNTING_ENFORCE_WCKEYS) 
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}
	debug3("found correct wckey %u", ret_wckey->id);
	if (wckey_pptr)
		*wckey_pptr = ret_wckey;

	wckey->id = ret_wckey->id;
	
	if(!wckey->user)
		wckey->user = ret_wckey->user;
	wckey->uid = ret_wckey->uid;
	
	if(!wckey->name)
		wckey->name = ret_wckey->name;
	if(!wckey->cluster)
		wckey->cluster = ret_wckey->cluster;


	slurm_mutex_unlock(&assoc_mgr_wckey_lock);

	return SLURM_SUCCESS;
}

extern acct_admin_level_t assoc_mgr_get_admin_level(void *db_conn,
						    uint32_t uid)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;

	if(!assoc_mgr_user_list) 
		if(_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return ACCT_ADMIN_NOTSET;

	if(!assoc_mgr_user_list) 
		return ACCT_ADMIN_NOTSET;

	slurm_mutex_lock(&assoc_mgr_user_lock);
	itr = list_iterator_create(assoc_mgr_user_list);
	while((found_user = list_next(itr))) {
		if(uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_user_lock);
		
	if(found_user) 
		return found_user->admin_level;
		
	return ACCT_ADMIN_NOTSET;	
}

extern int assoc_mgr_is_user_acct_coord(void *db_conn,
					uint32_t uid,
					char *acct_name)
{
	ListIterator itr = NULL;
	acct_coord_rec_t *acct = NULL;
	acct_user_rec_t * found_user = NULL;

	if(!assoc_mgr_user_list) 
		if(_get_assoc_mgr_user_list(db_conn, 0) == SLURM_ERROR)
			return ACCT_ADMIN_NOTSET;

	if(!assoc_mgr_user_list) 
		return ACCT_ADMIN_NOTSET;

	slurm_mutex_lock(&assoc_mgr_user_lock);
	itr = list_iterator_create(assoc_mgr_user_list);
	while((found_user = list_next(itr))) {
		if(uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);
		
	if(!found_user || !found_user->coord_accts) {
		slurm_mutex_unlock(&assoc_mgr_user_lock);
		return 0;
	}
	itr = list_iterator_create(found_user->coord_accts);
	while((acct = list_next(itr))) {
		if(!strcmp(acct_name, acct->name))
			break;
	}
	list_iterator_destroy(itr);
	
	if(acct) {
		slurm_mutex_unlock(&assoc_mgr_user_lock);
		return 1;
	}
	slurm_mutex_unlock(&assoc_mgr_user_lock);

	return 0;	
}

extern List assoc_mgr_get_shares(void *db_conn,
				 uid_t uid, List acct_list, List user_list)
{
	ListIterator itr = NULL;
	ListIterator user_itr = NULL;
	ListIterator acct_itr = NULL;
	acct_association_rec_t *assoc = NULL;
	association_shares_object_t *share = NULL;
	List ret_list = NULL;
	char *tmp_char = NULL;
	acct_user_rec_t user;
	int is_admin=1;
	uint16_t private_data = slurm_get_private_data();

	if(!assoc_mgr_association_list
	   || !list_count(assoc_mgr_association_list))
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;
	
	if(user_list && list_count(user_list)) 
		user_itr = list_iterator_create(user_list);

	if(acct_list && list_count(acct_list)) 
		acct_itr = list_iterator_create(acct_list);

	if (private_data & PRIVATE_DATA_USAGE) {
		uint32_t slurm_uid = slurm_get_slurm_user_id();
		is_admin = 0;
		/* Check permissions of the requesting user.
		 */
		if((uid == slurm_uid || uid == 0)
		   || assoc_mgr_get_admin_level(db_conn, uid) 
		   >= ACCT_ADMIN_OPERATOR) 
			is_admin = 1;	
		else {
			assoc_mgr_fill_in_user(db_conn, &user, 1, NULL);
		}
	}

	ret_list = list_create(slurm_destroy_association_shares_object);

	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((assoc = list_next(itr))) {		
		if(user_itr && assoc->user) {
			while((tmp_char = list_next(user_itr))) {
				if(!strcasecmp(tmp_char, assoc->user))
					break;
			}
			list_iterator_reset(user_itr);
			/* not correct user */
			if(!tmp_char) 
				continue;
		}

		if(acct_itr) {
			while((tmp_char = list_next(acct_itr))) {
				if(!strcasecmp(tmp_char, assoc->acct))
					break;
			}
			list_iterator_reset(acct_itr);
			/* not correct account */
			if(!tmp_char) 
				continue;
		}

		if (private_data & PRIVATE_DATA_USAGE) {
			if(!is_admin) {
				ListIterator itr = NULL;
				acct_coord_rec_t *coord = NULL;

				if(assoc->user && 
				   !strcmp(assoc->user, user.name)) 
					goto is_user;
				
				if(!user.coord_accts) {
					debug4("This user isn't a coord.");
					goto bad_user;
				}

				if(!assoc->acct) {
					debug("No account name given "
					      "in association.");
					goto bad_user;				
				}
				
				itr = list_iterator_create(user.coord_accts);
				while((coord = list_next(itr))) {
					if(!strcasecmp(coord->name, 
						       assoc->acct))
						break;
				}
				list_iterator_destroy(itr);
				
				if(coord) 
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

		if(assoc == assoc_mgr_root_assoc) 
			share->raw_shares = NO_VAL;
		else 
			share->raw_shares = assoc->raw_shares;

		share->norm_shares = assoc->norm_shares;
		share->raw_usage = (uint64_t)assoc->raw_usage;
		share->norm_usage = (double)assoc->norm_usage;
		if(assoc->user) {
		/* We only calculate user effective usage when we need
		 * it */
			long double efctv_usage = assoc->norm_usage +
				((assoc->parent_assoc_ptr->efctv_usage -
				  assoc->norm_usage) *
				 assoc->raw_shares / assoc->level_shares);
			share->efctv_usage = (double)efctv_usage;
			share->name = xstrdup(assoc->user);
			share->parent = xstrdup(assoc->acct);
			share->user = 1;
		} else {
			share->efctv_usage = (double)assoc->efctv_usage;
			share->name = xstrdup(assoc->acct);
			share->parent = xstrdup(assoc->parent_acct);
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);
	
	if(user_itr) 
		list_iterator_destroy(user_itr);
	if(acct_itr) 
		list_iterator_destroy(acct_itr);
		
	return ret_list;
}

extern int assoc_mgr_update_assocs(acct_update_object_t *update)
{
	acct_association_rec_t * rec = NULL;
	acct_association_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	int parents_changed = 0;

	if(!assoc_mgr_association_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((object = list_pop(update->objects))) {
		if(object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if(strcasecmp(object->cluster, 
				      assoc_mgr_cluster_name)) {
				destroy_acct_association_rec(object);	
				continue;
			}
		}
		list_iterator_reset(itr);
		while((rec = list_next(itr))) {
			if(object->id) {
				if(object->id == rec->id) {
					break;
				}
				continue;
			} else {
				if(!object->user && rec->user) {
					debug4("we are looking for a "
					       "nonuser association");
					continue;
				} else if(object->uid != rec->uid) {
					debug4("not the right user");
					continue;
				}
				
				if(object->acct
				   && (!rec->acct 
				       || strcasecmp(object->acct,
						     rec->acct))) {
					debug4("not the right account");
					continue;
				}
				
				if(object->partition
				   && (!rec->partition 
				       || strcasecmp(object->partition, 
						     rec->partition))) {
					debug4("not the right partition");
					continue;
				}

				/* only check for on the slurmdbd */
				if(!assoc_mgr_cluster_name && object->cluster
				   && (!rec->cluster
				       || strcasecmp(object->cluster,
						     rec->cluster))) {
					debug4("not the right cluster");
					continue;
				}
				break;
			}			
		}
		//info("%d assoc %u", update->type, object->id);
		switch(update->type) {
		case ACCT_MODIFY_ASSOC:
			if(!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if(object->raw_shares != NO_VAL) {
				rec->raw_shares = object->raw_shares;
				if(setup_childern) {
					/* we need to update the shares on
					   each sibling and child
					   association now 
					*/
					parents_changed = 1;
				}
			}

			if(object->grp_cpu_mins != NO_VAL) 
				rec->grp_cpu_mins = object->grp_cpu_mins;
			if(object->grp_cpus != NO_VAL) 
				rec->grp_cpus = object->grp_cpus;
			if(object->grp_jobs != NO_VAL) 
				rec->grp_jobs = object->grp_jobs;
			if(object->grp_nodes != NO_VAL) 
				rec->grp_nodes = object->grp_nodes;
			if(object->grp_submit_jobs != NO_VAL) 
				rec->grp_submit_jobs = object->grp_submit_jobs;
			if(object->grp_wall != NO_VAL) 
				rec->grp_wall = object->grp_wall;
			
			if(object->max_cpu_mins_pj != NO_VAL) 
				rec->max_cpu_mins_pj = object->max_cpu_mins_pj;
			if(object->max_cpus_pj != NO_VAL) 
				rec->max_cpus_pj = object->max_cpus_pj;
			if(object->max_jobs != NO_VAL) 
				rec->max_jobs = object->max_jobs;
			if(object->max_nodes_pj != NO_VAL) 
				rec->max_nodes_pj = object->max_nodes_pj;
			if(object->max_submit_jobs != NO_VAL) 
				rec->max_submit_jobs = object->max_submit_jobs;
			if(object->max_wall_pj != NO_VAL) 
				rec->max_wall_pj = object->max_wall_pj;
			

			if(object->parent_acct) {
				xfree(rec->parent_acct);
				rec->parent_acct = xstrdup(object->parent_acct);
			}
			if(object->parent_id) {
				rec->parent_id = object->parent_id;
				// after all new parents have been set we will
				// reset the parent pointers below
				parents_changed = 1;
			}

			if(object->qos_list) {
				if(rec->qos_list) {
					_local_update_assoc_qos_list(
						rec, object->qos_list);
				} else {
					rec->qos_list = object->qos_list;
					object->qos_list = NULL;
				}
			}

			if(!slurmdbd_conf && !parents_changed) {
				debug("updating assoc %u", rec->id);
				slurm_mutex_lock(&assoc_mgr_qos_lock);
				log_assoc_rec(rec, assoc_mgr_qos_list);
				slurm_mutex_unlock(&assoc_mgr_qos_lock);
			}
			break;
		case ACCT_ADD_ASSOC:
			if(rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_append(assoc_mgr_association_list, object);
			object = NULL;
			parents_changed = 1; // set since we need to
					     // set the parent
			break;
		case ACCT_REMOVE_ASSOC:
			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			if (remove_assoc_notify)
				remove_assoc_notify(rec);
			if(setup_childern)
				parents_changed = 1; /* set since we need to
							set the shares
							of surrounding childern
						     */
			list_delete_item(itr);
			break;
		default:
			break;
		}
		
		destroy_acct_association_rec(object);			
	}
		
	/* We have to do this after the entire list is processed since
	 * we may have added the parent which wasn't in the list before
	 */
	if(parents_changed) {
		int reset = 1;
		list_sort(assoc_mgr_association_list, 
			  (ListCmpF)_sort_assoc_dec);

		list_iterator_reset(itr);
		/* flush the childern lists */
		if(setup_childern) {
			while((object = list_next(itr))) {
				if(object->childern_list)
					list_flush(object->childern_list);
			}
			list_iterator_reset(itr);
		}
		while((object = list_next(itr))) {
			/* reset the limits because since a parent
			   changed we could have different usage
			*/
			if(!object->user) {
				_clear_used_info(object);
				object->raw_usage = 0;
			}
			_set_assoc_parent_and_user(
				object, assoc_mgr_association_list, reset);
			reset = 0;
		}
		/* Now that we have set up the parents correctly we
		   can update the used limits
		*/
		list_iterator_reset(itr);
		while((object = list_next(itr))) {
			if(setup_childern) {
				int count = 0;
				ListIterator itr2 = NULL;
				if(!object->childern_list
				   || !list_count(object->childern_list))
					continue;
				itr2 = list_iterator_create(
					object->childern_list);
				while((rec = list_next(itr2))) 
					count += rec->raw_shares;
				list_iterator_reset(itr2);
				while((rec = list_next(itr2))) 
					rec->level_shares = count;
				list_iterator_destroy(itr2);
			}
				
			if(!object->user)
				continue;

			rec = object;
			/* look for a parent since we are starting at
			   the parent instead of the child
			*/
			while(object->parent_assoc_ptr) {
				/* we need to get the parent first
				   here since we start at the child
				*/
				object = object->parent_assoc_ptr;

				_addto_used_info(object, rec);
			}
		}
		if(setup_childern) {
			/* Now normalize the static shares */
			list_iterator_reset(itr);
			while((object = list_next(itr))) {
				rec = object;
				rec->norm_shares = 1.0;
				/* we don't need to do this for root so stop
				   there */
				while(object->parent_assoc_ptr) {
					rec->norm_shares *= 
						(double)object->raw_shares /
						(double)object->level_shares;
					object = object->parent_assoc_ptr;
				}
				slurm_mutex_lock(&assoc_mgr_qos_lock);
				log_assoc_rec(rec, assoc_mgr_qos_list);
				slurm_mutex_unlock(&assoc_mgr_qos_lock);
			}
		}
	}

	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);

	return rc;	
}

extern int assoc_mgr_update_wckeys(acct_update_object_t *update)
{
	acct_wckey_rec_t * rec = NULL;
	acct_wckey_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;

	if(!assoc_mgr_wckey_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_wckey_lock);
	itr = list_iterator_create(assoc_mgr_wckey_list);
	while((object = list_pop(update->objects))) {
		if(object->cluster && assoc_mgr_cluster_name) {
			/* only update the local clusters assocs */
			if(strcasecmp(object->cluster, assoc_mgr_cluster_name)) {
				destroy_acct_wckey_rec(object);	
				continue;
			}
		}
		list_iterator_reset(itr);
		while((rec = list_next(itr))) {
			if(object->id) {
				if(object->id == rec->id) {
					break;
				}
				continue;
			} else {
				if(object->uid != rec->uid) {
					debug4("not the right user");
					continue;
				}
				
				if(object->name
				   && (!rec->name 
				       || strcasecmp(object->name,
						     rec->name))) {
					debug4("not the right wckey");
					continue;
				}
				
				/* only check for on the slurmdbd */
				if(!assoc_mgr_cluster_name && object->cluster
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
		case ACCT_MODIFY_WCKEY:
			if(!rec) {
				rc = SLURM_ERROR;
				break;
			}
			/* nothing yet */
			break;
		case ACCT_ADD_WCKEY:
			if(rec) {
				//rc = SLURM_ERROR;
				break;
			}
			pw_uid = uid_from_string(object->user);
			if(pw_uid == (uid_t) -1) {
				debug("wckey add couldn't get a uid "
				      "for user %s",
				      object->name);
				object->uid = NO_VAL;
			} else
				object->uid = pw_uid;
			list_append(assoc_mgr_wckey_list, object);
			object = NULL;
			break;
		case ACCT_REMOVE_WCKEY:
			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		default:
			break;
		}
		
		destroy_acct_wckey_rec(object);			
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_wckey_lock);

	return rc;	
}

extern int assoc_mgr_update_users(acct_update_object_t *update)
{
	acct_user_rec_t * rec = NULL;
	acct_user_rec_t * object = NULL;
		
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;

	if(!assoc_mgr_user_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_user_lock);
	itr = list_iterator_create(assoc_mgr_user_list);
	while((object = list_pop(update->objects))) {
		list_iterator_reset(itr);
		while((rec = list_next(itr))) {
			if(!strcasecmp(object->name, rec->name)) 
				break;
		}

		//info("%d user %s", update->type, object->name);
		switch(update->type) {
		case ACCT_MODIFY_USER:
			if(!rec) {
				rc = SLURM_ERROR;
				break;
			}

			if(object->default_acct) {
				xfree(rec->default_acct);
				rec->default_acct = object->default_acct;
				object->default_acct = NULL;
			}

			if(object->default_wckey) {
				xfree(rec->default_wckey);
				rec->default_wckey = object->default_wckey;
				object->default_wckey = NULL;
			}

			if(object->admin_level != ACCT_ADMIN_NOTSET) 
				rec->admin_level = object->admin_level;

			break;
		case ACCT_ADD_USER:
			if(rec) {
				//rc = SLURM_ERROR;
				break;
			}
			pw_uid = uid_from_string(object->name);
			if(pw_uid == (uid_t) -1) {
				debug("user add couldn't get a uid for user %s",
				      object->name);
				object->uid = NO_VAL;
			} else
				object->uid = pw_uid;
			list_append(assoc_mgr_user_list, object);
			object = NULL;
			break;
		case ACCT_REMOVE_USER:
			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		case ACCT_ADD_COORD:
			/* same as ACCT_REMOVE_COORD */
		case ACCT_REMOVE_COORD:
			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			/* We always get a complete list here */
			if(!object->coord_accts) {
				if(rec->coord_accts)
					list_flush(rec->coord_accts);
			} else {
				if(rec->coord_accts)
					list_destroy(rec->coord_accts);
				rec->coord_accts = object->coord_accts;
				object->coord_accts = NULL;
			}
			break;
		default:
			break;
		}
		
		destroy_acct_user_rec(object);			
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_user_lock);

	return rc;	
}

extern int assoc_mgr_update_qos(acct_update_object_t *update)
{
	acct_qos_rec_t *rec = NULL;
	acct_qos_rec_t *object = NULL;

	char *qos_char = NULL, *tmp_char = NULL;

	ListIterator itr = NULL, assoc_itr = NULL, qos_itr = NULL;

	acct_association_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;

	if(!assoc_mgr_qos_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&assoc_mgr_qos_lock);
	itr = list_iterator_create(assoc_mgr_qos_list);
	while((object = list_pop(update->objects))) {
		list_iterator_reset(itr);
		while((rec = list_next(itr))) {
			if(object->id == rec->id) {
				break;
			}
		}

		//info("%d qos %s", update->type, object->name);
		switch(update->type) {
		case ACCT_ADD_QOS:
			if(rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_append(assoc_mgr_qos_list, object);
			object = NULL;			
			break;
		case ACCT_MODIFY_QOS:
			/* FIX ME: fill in here the qos changes stuff */
			break;
		case ACCT_REMOVE_QOS:
			/* Remove this qos from all the associations
			   on this cluster.
			*/
			tmp_char = xstrdup_printf("%d", object->id);
			slurm_mutex_lock(&assoc_mgr_association_lock);
			assoc_itr = list_iterator_create(
				assoc_mgr_association_list);
			while((assoc = list_next(assoc_itr))) {
				if(!assoc->qos_list
				   || !list_count(assoc->qos_list))
					continue;
				qos_itr = list_iterator_create(assoc->qos_list);
				while((qos_char = list_next(qos_itr))) {
					if(!strcmp(qos_char, tmp_char)) {
						list_delete_item(qos_itr);
						break;
					}
				}
				list_iterator_destroy(qos_itr);
			}
			list_iterator_destroy(assoc_itr);
			slurm_mutex_unlock(&assoc_mgr_association_lock);
			xfree(tmp_char);

			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		default:
			break;
		}
		destroy_acct_qos_rec(object);			
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_qos_lock);

	return rc;	
}

extern int assoc_mgr_validate_assoc_id(void *db_conn,
				       uint32_t assoc_id,
				       int enforce)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if(!assoc_mgr_association_list) 
		if(_get_assoc_mgr_association_list(db_conn, enforce) 
		   == SLURM_ERROR)
			return SLURM_ERROR;

	if((!assoc_mgr_association_list
	    || !list_count(assoc_mgr_association_list))
	   && !(enforce & ACCOUNTING_ENFORCE_ASSOCS)) 
		return SLURM_SUCCESS;
	
	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc_id == found_assoc->id) 
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);

	if(found_assoc || !(enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern void assoc_mgr_clear_used_info(void)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if (!assoc_mgr_association_list)
		return;

	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((found_assoc = list_next(itr))) {
		_clear_used_info(found_assoc);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);
}

extern int dump_assoc_mgr_state(char *state_save_location) 
{
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	dbd_list_msg_t msg;
	Buf buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);

	if(assoc_mgr_association_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		slurm_mutex_lock(&assoc_mgr_association_lock);
		msg.my_list = assoc_mgr_association_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_ASSOCS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_ASSOCS, &msg, buffer);
		slurm_mutex_unlock(&assoc_mgr_association_lock);
	}
	
	if(assoc_mgr_user_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		slurm_mutex_lock(&assoc_mgr_user_lock);
		msg.my_list = assoc_mgr_user_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_USERS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_USERS, &msg, buffer);
		slurm_mutex_unlock(&assoc_mgr_user_lock);
	}

	if(assoc_mgr_qos_list) {		
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		slurm_mutex_lock(&assoc_mgr_qos_lock);
		msg.my_list = assoc_mgr_qos_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_QOS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_QOS, &msg, buffer);	
		slurm_mutex_unlock(&assoc_mgr_qos_lock);
	}

	if(assoc_mgr_wckey_list) {		
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		slurm_mutex_lock(&assoc_mgr_wckey_lock);
		msg.my_list = assoc_mgr_wckey_list;
		/* let us know what to unpack */
		pack16(DBD_ADD_WCKEYS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_WCKEYS, &msg, buffer);	
		slurm_mutex_unlock(&assoc_mgr_wckey_lock);
	}

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/assoc_mgr_state", state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);
	
	slurm_mutex_lock(&assoc_mgr_file_lock);
	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
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
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
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

	if(assoc_mgr_association_list) {
		ListIterator itr = NULL;
		acct_association_rec_t *assoc = NULL;

		slurm_mutex_lock(&assoc_mgr_association_lock);
		itr = list_iterator_create(assoc_mgr_association_list);
		while((assoc = list_next(itr))) {
			if(!assoc->user)
				continue;
			
			pack32(assoc->id, buffer);
			/* we only care about the main part here so
			   anything under 1 we are dropping 
			*/
			pack64((uint64_t)assoc->raw_usage, buffer);
		}
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&assoc_mgr_association_lock);
	}

	reg_file = xstrdup_printf("%s/assoc_usage", state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);
	
	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
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
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	slurm_mutex_unlock(&assoc_mgr_file_lock);
	
	free_buf(buffer);
	END_TIMER2("dump_assoc_mgr_state");
	return error_code;

}

extern int load_assoc_usage(char *state_save_location)
{
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	uint16_t ver = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	ListIterator itr = NULL;

	if(!assoc_mgr_association_list)
		return SLURM_SUCCESS;

	/* read the file */
	state_file = xstrdup(state_save_location);
	xstrcat(state_file, "/assoc_usage");
	//info("looking at the %s file", state_file);
	slurm_mutex_lock(&assoc_mgr_file_lock);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No Assoc usage file (%s) to recover", state_file);
		error_code = ENOENT;
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
	slurm_mutex_unlock(&assoc_mgr_file_lock);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver != ASSOC_USAGE_VERSION) {
		error("***********************************************");
		error("Can not recover usage_mgr state, incompatable version, "
		      "got %u need %u", ver, ASSOC_USAGE_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	
	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while (remaining_buf(buffer) > 0) {
		uint32_t assoc_id = 0;
		uint64_t uint64_tmp = 0;
		acct_association_rec_t *assoc = NULL;

		safe_unpack32(&assoc_id, buffer);
		safe_unpack64(&uint64_tmp, buffer);
		while((assoc = list_next(itr))) {
			if(!assoc->user)
				continue;
			if(assoc->id == assoc_id)
				break;
		}
		if(assoc) {
			while(assoc) {
				assoc->raw_usage += (long double)uint64_tmp;
				assoc = assoc->parent_assoc_ptr;
			}
		}
		list_iterator_reset(itr);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);
			
	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if(buffer)
		free_buf(buffer);
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
	
	/* read the file */
	state_file = xstrdup(state_save_location);
	xstrcat(state_file, "/assoc_mgr_state");
	//info("looking at the %s file", state_file);
	slurm_mutex_lock(&assoc_mgr_file_lock);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No association state file (%s) to recover", state_file);
		error_code = ENOENT;
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
	slurm_mutex_unlock(&assoc_mgr_file_lock);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver > SLURMDBD_VERSION || ver < SLURMDBD_VERSION_MIN) {
		error("***********************************************");
		error("Can not recover assoc_mgr state, incompatable version, "
		      "got %u need > %u <= %u", ver,
		      SLURMDBD_VERSION_MIN, SLURMDBD_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	while (remaining_buf(buffer) > 0) {
		safe_unpack16(&type, buffer);
		switch(type) {
		case DBD_ADD_ASSOCS:
			error_code = slurmdbd_unpack_list_msg(
				SLURMDBD_VERSION, DBD_ADD_ASSOCS, &msg, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if(!msg->my_list) {
				error("No associations retrieved");
				break;
			}
			slurm_mutex_lock(&assoc_mgr_association_lock);
			assoc_mgr_association_list = msg->my_list;
			_post_association_list(assoc_mgr_association_list);
			debug("Recovered %u associations", 
			      list_count(assoc_mgr_association_list));
			slurm_mutex_unlock(&assoc_mgr_association_lock);
			msg->my_list = NULL;
			slurmdbd_free_list_msg(SLURMDBD_VERSION, msg);
			break;
		case DBD_ADD_USERS:
			error_code = slurmdbd_unpack_list_msg(
				SLURMDBD_VERSION, DBD_ADD_USERS, &msg, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if(!msg->my_list) {
				error("No users retrieved");
				break;
			}
			slurm_mutex_lock(&assoc_mgr_user_lock);
			assoc_mgr_user_list = msg->my_list;
			_post_user_list(assoc_mgr_user_list);
			debug("Recovered %u users", 
			      list_count(assoc_mgr_user_list));
			slurm_mutex_unlock(&assoc_mgr_user_lock);
			msg->my_list = NULL;
			slurmdbd_free_list_msg(SLURMDBD_VERSION, msg);
			break;
		case DBD_ADD_QOS:
			error_code = slurmdbd_unpack_list_msg(
				SLURMDBD_VERSION, DBD_ADD_QOS, &msg, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if(!msg->my_list) {
				error("No qos retrieved");
				break;
			}
			slurm_mutex_lock(&assoc_mgr_qos_lock);
			assoc_mgr_qos_list = msg->my_list;
			debug("Recovered %u qos", 
			      list_count(assoc_mgr_qos_list));
			slurm_mutex_unlock(&assoc_mgr_qos_lock);
			msg->my_list = NULL;
			slurmdbd_free_list_msg(SLURMDBD_VERSION, msg);	
			break;
		case DBD_ADD_WCKEYS:
			error_code = slurmdbd_unpack_list_msg(
				SLURMDBD_VERSION, DBD_ADD_WCKEYS, &msg, buffer);
			if (error_code != SLURM_SUCCESS)
				goto unpack_error;
			else if(!msg->my_list) {
				error("No qos retrieved");
				break;
			}
			slurm_mutex_lock(&assoc_mgr_wckey_lock);
			assoc_mgr_wckey_list = msg->my_list;
			debug("Recovered %u wckeys", 
			      list_count(assoc_mgr_wckey_list));
			slurm_mutex_unlock(&assoc_mgr_wckey_lock);
			msg->my_list = NULL;
			slurmdbd_free_list_msg(SLURMDBD_VERSION, msg);	
			break;
		default:
			error("unknown type %u given", type);
			goto unpack_error;
			break;
		}
	}
	running_cache = 1;
	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if(buffer)
		free_buf(buffer);
	return SLURM_ERROR;
} 

extern int assoc_mgr_refresh_lists(void *db_conn, assoc_init_args_t *args)
{
	static uint16_t enforce = 0;
	static uint16_t cache_level = ASSOC_MGR_CACHE_ALL;

	if(args) {
		enforce = args->enforce;
		cache_level = args->cache_level;
	}
	
	if(!running_cache) { 
		debug4("No need to run assoc_mgr_refresh_lists if not running "
		       "cache things are already synced.");
		return SLURM_SUCCESS;
	}

	if(cache_level & ASSOC_MGR_CACHE_ASSOC) {
		if(_refresh_assoc_mgr_association_list(db_conn, enforce)
		   == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if(cache_level & ASSOC_MGR_CACHE_QOS)
		if(_refresh_assoc_mgr_qos_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if(cache_level & ASSOC_MGR_CACHE_USER)
		if(_refresh_assoc_mgr_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if(cache_level & ASSOC_MGR_CACHE_WCKEY)
		if(_refresh_assoc_wckey_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	running_cache = 0;

	return SLURM_SUCCESS;	
}

