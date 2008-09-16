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

static List local_association_list = NULL;
static List local_qos_list = NULL;
static List local_user_list = NULL;
static char *local_cluster_name = NULL;

void (*remove_assoc_notify) (acct_association_rec_t *rec) = NULL;

static pthread_mutex_t local_association_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t local_qos_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t local_user_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t local_file_lock = PTHREAD_MUTEX_INITIALIZER;

/* locks should be put in place before calling this function */
static int _set_assoc_parent_and_user(acct_association_rec_t *assoc,
				      List assoc_list)
{
	if(!assoc || !assoc_list) {
		error("you didn't give me an association");
		return SLURM_ERROR;
	}

	if(assoc->parent_id) {
		acct_association_rec_t *assoc2 = NULL;
		ListIterator itr = list_iterator_create(assoc_list);
		while((assoc2 = list_next(itr))) {
			if(assoc2->id == assoc->parent_id) {
				assoc->parent_acct_ptr = assoc2;
				break;
			}
		}
		list_iterator_destroy(itr);
	}

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
	//DEF_TIMERS;

	if(!assoc_list)
		return SLURM_ERROR;

	itr = list_iterator_create(assoc_list);
	//START_TIMER;
	while((assoc = list_next(itr))) 
		_set_assoc_parent_and_user(assoc, assoc_list);
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
			debug("couldn't get a uid for user %s",
			      user->name);
			user->uid = (uint32_t)NO_VAL;
		} else
			user->uid = pw_uid;
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

static int _get_local_association_list(void *db_conn, int enforce)
{
	acct_association_cond_t assoc_q;
	char *cluster_name = NULL;
	uid_t uid = getuid();

//	DEF_TIMERS;
	slurm_mutex_lock(&local_association_lock);
	if(local_association_list)
		list_destroy(local_association_list);

	memset(&assoc_q, 0, sizeof(acct_association_cond_t));
	if(local_cluster_name) {
		assoc_q.cluster_list = list_create(slurm_destroy_char);
		cluster_name = xstrdup(local_cluster_name);
		if(!cluster_name) {
			if(enforce && !slurmdbd_conf) {
				error("_get_local_association_list: "
				      "no cluster name here going to get "
				      "all associations.");
			}
		} else 
			list_append(assoc_q.cluster_list, cluster_name);
	}

//	START_TIMER;
	local_association_list =
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if(assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);
	
	if(!local_association_list) {
		/* create list so we don't keep calling this if there
		   isn't anything there */
		local_association_list = list_create(NULL);
		slurm_mutex_unlock(&local_association_lock);
		if(enforce) {
			error("_get_local_association_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			debug3("not enforcing associations and no "
			       "list was given so we are giving a blank list");
			return SLURM_SUCCESS;
		}
	} 

	_post_association_list(local_association_list);

	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

static int _get_local_qos_list(void *db_conn, int enforce)
{
	uid_t uid = getuid();

	slurm_mutex_lock(&local_qos_lock);
	if(local_qos_list)
		list_destroy(local_qos_list);
	local_qos_list = acct_storage_g_get_qos(db_conn, uid, NULL);

	if(!local_qos_list) {
		slurm_mutex_unlock(&local_qos_lock);
		if(enforce) {
			error("_get_local_qos_list: no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}		
	}

	slurm_mutex_unlock(&local_qos_lock);
	return SLURM_SUCCESS;
}

static int _get_local_user_list(void *db_conn, int enforce)
{
	acct_user_cond_t user_q;
	uid_t uid = getuid();

	memset(&user_q, 0, sizeof(acct_user_cond_t));
	user_q.with_coords = 1;
	
	slurm_mutex_lock(&local_user_lock);
	if(local_user_list)
		list_destroy(local_user_list);
	local_user_list = acct_storage_g_get_users(db_conn, uid, &user_q);

	if(!local_user_list) {
		slurm_mutex_unlock(&local_user_lock);
		if(enforce) {
			error("_get_local_user_list: "
			      "no list was made.");
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}		
	} 

	_post_user_list(local_user_list);
	
	slurm_mutex_unlock(&local_user_lock);
	return SLURM_SUCCESS;
}

static int _refresh_local_association_list(void *db_conn, int enforce)
{
	acct_association_cond_t assoc_q;
	List current_assocs = NULL;
	char *cluster_name = NULL;
	uid_t uid = getuid();
	ListIterator curr_itr = NULL;
	ListIterator local_itr = NULL;
	acct_association_rec_t *curr_assoc = NULL, *assoc = NULL;
//	DEF_TIMERS;

	memset(&assoc_q, 0, sizeof(acct_association_cond_t));
	if(local_cluster_name) {
		assoc_q.cluster_list = list_create(slurm_destroy_char);
		cluster_name = xstrdup(local_cluster_name);
		if(!cluster_name) {
			if(enforce && !slurmdbd_conf) {
				error("_get_local_association_list: "
				      "no cluster name here going to get "
				      "all associations.");
			}
		} else 
			list_append(assoc_q.cluster_list, cluster_name);
	}


	slurm_mutex_lock(&local_association_lock);

	current_assocs = local_association_list;

//	START_TIMER;
	local_association_list = 
		acct_storage_g_get_associations(db_conn, uid, &assoc_q);
//	END_TIMER2("get_associations");

	if(assoc_q.cluster_list)
		list_destroy(assoc_q.cluster_list);
	
	if(!local_association_list) {
		local_association_list = current_assocs;
		slurm_mutex_unlock(&local_association_lock);
		
		error("_refresh_local_association_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}
 
	_post_association_list(local_association_list);
	
	if(!current_assocs) {
		slurm_mutex_unlock(&local_association_lock);
		return SLURM_SUCCESS;
	}
	
	curr_itr = list_iterator_create(current_assocs);
	local_itr = list_iterator_create(local_association_list);
	/* add limitss */
	while((curr_assoc = list_next(curr_itr))) {
		while((assoc = list_next(local_itr))) {
			if(assoc->id == curr_assoc->id) 
				break;
		}
		
		if(!assoc) 
			continue;
		assoc->used_jobs = curr_assoc->used_jobs;
		assoc->used_share = curr_assoc->used_share;
		list_iterator_reset(local_itr);			
	}
	
	list_iterator_destroy(curr_itr);
	list_iterator_destroy(local_itr);
		
	slurm_mutex_unlock(&local_association_lock);

	if(current_assocs)
		list_destroy(current_assocs);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed
 */
static int _refresh_local_qos_list(void *db_conn, int enforce)
{
	List current_qos = NULL;
	uid_t uid = getuid();

	current_qos = acct_storage_g_get_qos(db_conn, uid, NULL);

	if(!current_qos) {
		error("_refresh_local_qos_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&local_qos_lock);
	if(local_qos_list)
		list_destroy(local_qos_list);

	local_qos_list = current_qos;

	slurm_mutex_unlock(&local_qos_lock);

	return SLURM_SUCCESS;
}

/* This only gets a new list if available dropping the old one if
 * needed 
 */
static int _refresh_local_user_list(void *db_conn, int enforce)
{
	List current_users = NULL;
	acct_user_cond_t user_q;
	uid_t uid = getuid();

	memset(&user_q, 0, sizeof(acct_user_cond_t));
	user_q.with_coords = 1;
	
	current_users = acct_storage_g_get_users(db_conn, uid, &user_q);

	if(!current_users) {
		error("_refresh_local_user_list: "
		      "no new list given back keeping cached one.");
		return SLURM_ERROR;
	}
	_post_user_list(current_users);

	slurm_mutex_lock(&local_user_lock);

	if(local_user_list) 
		list_destroy(local_user_list);

	local_user_list = current_users;
	
	slurm_mutex_unlock(&local_user_lock);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_init(void *db_conn, assoc_init_args_t *args)
{
	static uint16_t enforce = 0;
	static uint16_t cache_level = ASSOC_MGR_CACHE_ALL;

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

	if((!local_cluster_name) && !slurmdbd_conf) {
		xfree(local_cluster_name);
		local_cluster_name = slurm_get_cluster_name();
	}

	if((!local_association_list) && (cache_level & ASSOC_MGR_CACHE_ASSOC)) 
		if(_get_local_association_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!local_qos_list) && (cache_level & ASSOC_MGR_CACHE_QOS))
		if(_get_local_qos_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!local_user_list) && (cache_level & ASSOC_MGR_CACHE_USER))
		if(_get_local_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fini(char *state_save_location)
{
	if(state_save_location)
		dump_assoc_mgr_state(state_save_location);

	if(local_association_list) 
		list_destroy(local_association_list);
	if(local_qos_list)
		list_destroy(local_qos_list);
	if(local_user_list)
		list_destroy(local_user_list);
	xfree(local_cluster_name);
	local_association_list = NULL;
	local_qos_list = NULL;
	local_user_list = NULL;

	return SLURM_SUCCESS;
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
	if(!local_association_list) {
		if(_get_local_association_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;
	}
	if((!local_association_list || !list_count(local_association_list))
	   && !enforce) 
		return SLURM_SUCCESS;

	if(!assoc->id) {
		if(!assoc->acct) {
			acct_user_rec_t user;

			if(assoc->uid == (uint32_t)NO_VAL) {
				if(enforce) {
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
			if(assoc_mgr_fill_in_user(db_conn, &user, enforce) 
			   == SLURM_ERROR) {
				if(enforce) 
					return SLURM_ERROR;
				else {
					return SLURM_SUCCESS;
				}
			}					
			assoc->user = user.name;
			assoc->acct = user.default_acct;
		} 
		
		if(!assoc->cluster)
			assoc->cluster = local_cluster_name;
	}
/* 	info("looking for assoc of user=%s(%u), acct=%s, " */
/* 	     "cluster=%s, partition=%s", */
/* 	     assoc->user, assoc->uid, assoc->acct, */
/* 	     assoc->cluster, assoc->partition); */
	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
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
			if(!local_cluster_name && found_assoc->cluster
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
		slurm_mutex_unlock(&local_association_lock);
		if(enforce) 
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
	if(!assoc->acct)
		assoc->acct = ret_assoc->acct;
	if(!assoc->cluster)
		assoc->cluster = ret_assoc->cluster;
	if(!assoc->partition)
		assoc->partition = ret_assoc->partition;
	assoc->fairshare                 = ret_assoc->fairshare;
	assoc->max_cpu_secs_per_job      = ret_assoc->max_cpu_secs_per_job;
	assoc->max_jobs                  = ret_assoc->max_jobs;
	assoc->max_nodes_per_job         = ret_assoc->max_nodes_per_job;
	assoc->max_wall_duration_per_job = ret_assoc->max_wall_duration_per_job;
	assoc->parent_acct_ptr           = ret_assoc->parent_acct_ptr;
	if(assoc->parent_acct) {
		xfree(assoc->parent_acct);
		assoc->parent_acct       = xstrdup(ret_assoc->parent_acct);
	} else 
		assoc->parent_acct       = ret_assoc->parent_acct;
	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fill_in_user(void *db_conn, acct_user_rec_t *user,
				  int enforce)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;

	if(!local_user_list) 
		if(_get_local_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!local_user_list || !list_count(local_user_list)) && !enforce) 
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((found_user = list_next(itr))) {
		if(user->uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);

	if(found_user) {
		memcpy(user, found_user, sizeof(acct_user_rec_t));		
		slurm_mutex_unlock(&local_user_lock);
		return SLURM_SUCCESS;
	}
	slurm_mutex_unlock(&local_user_lock);
	return SLURM_ERROR;
}

extern acct_admin_level_t assoc_mgr_get_admin_level(void *db_conn,
						    uint32_t uid)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;

	if(!local_user_list) 
		if(_get_local_user_list(db_conn, 0) == SLURM_ERROR)
			return ACCT_ADMIN_NOTSET;

	if(!local_user_list) 
		return ACCT_ADMIN_NOTSET;

	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((found_user = list_next(itr))) {
		if(uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_user_lock);
		
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

	if(!local_user_list) 
		if(_get_local_user_list(db_conn, 0) == SLURM_ERROR)
			return ACCT_ADMIN_NOTSET;

	if(!local_user_list) 
		return ACCT_ADMIN_NOTSET;

	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((found_user = list_next(itr))) {
		if(uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);
		
	if(!found_user || !found_user->coord_accts) {
		slurm_mutex_unlock(&local_user_lock);
		return 0;
	}
	itr = list_iterator_create(found_user->coord_accts);
	while((acct = list_next(itr))) {
		if(!strcmp(acct_name, acct->name))
			break;
	}
	list_iterator_destroy(itr);
	
	if(acct) {
		slurm_mutex_unlock(&local_user_lock);
		return 1;
	}
	slurm_mutex_unlock(&local_user_lock);

	return 0;	
}

extern int assoc_mgr_update_local_assocs(acct_update_object_t *update)
{
	acct_association_rec_t * rec = NULL;
	acct_association_rec_t * object = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	int parents_changed = 0;

	if(!local_association_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((object = list_pop(update->objects))) {
		if(object->cluster && local_cluster_name) {
			/* only update the local clusters assocs */
			if(strcasecmp(object->cluster, local_cluster_name))
				continue;
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
				
				/* only check for on the slurmdbd */
				if(!local_cluster_name && object->acct
				   && (!rec->cluster
				       || strcasecmp(object->cluster,
						     rec->cluster))) {
					debug4("not the right cluster");
					continue;
				}
				
				if(object->partition
				   && (!rec->partition 
				       || strcasecmp(object->partition, 
						     rec->partition))) {
					debug4("not the right partition");
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
			debug("updating assoc %u", rec->id);
			if(object->fairshare != NO_VAL) {
				rec->fairshare = object->fairshare;
			}

			if(object->max_jobs != NO_VAL) {
				rec->max_jobs = object->max_jobs;
			}

			if(object->max_nodes_per_job != NO_VAL) {
				rec->max_nodes_per_job =
					object->max_nodes_per_job;
			}

			if(object->max_wall_duration_per_job != NO_VAL) {
				rec->max_wall_duration_per_job =
					object->max_wall_duration_per_job;
			}

			if(object->max_cpu_secs_per_job != NO_VAL) {
				rec->max_cpu_secs_per_job = 
					object->max_cpu_secs_per_job;
			}

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
			log_assoc_rec(rec);
			break;
		case ACCT_ADD_ASSOC:
			if(rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_append(local_association_list, object);
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
			list_delete_item(itr);
			break;
		default:
			break;
		}
		if(update->type != ACCT_ADD_ASSOC) {
			destroy_acct_association_rec(object);			
		}				
	}

	/* We have to do this after the entire list is processed since
	 * we may have added the parent which wasn't in the list before
	 */
	if(parents_changed) {
		list_iterator_reset(itr);
		while((object = list_next(itr))) 
			_set_assoc_parent_and_user(
				object, local_association_list);
	}

	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);

	return rc;	
}

extern int assoc_mgr_update_local_users(acct_update_object_t *update)
{
	acct_user_rec_t * rec = NULL;
	acct_user_rec_t * object = NULL;
		
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	uid_t pw_uid;

	if(!local_user_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((object = list_pop(update->objects))) {
		list_iterator_reset(itr);
		while((rec = list_next(itr))) {
			if(!strcasecmp(object->name, rec->name)) {
				break;
			}
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

			if(object->qos_list) {
				if(rec->qos_list)
					list_destroy(rec->qos_list);
				rec->qos_list = object->qos_list;
				object->qos_list = NULL;
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
				debug("couldn't get a uid for user %s",
				      object->name);
				object->uid = NO_VAL;
			} else
				object->uid = pw_uid;
			list_append(local_user_list, object);
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
		if(update->type != ACCT_ADD_USER) {
			destroy_acct_user_rec(object);			
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_user_lock);

	return rc;	
}

extern int assoc_mgr_update_local_qos(acct_update_object_t *update)
{
	acct_qos_rec_t * rec = NULL;
	acct_qos_rec_t * object = NULL;
		
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;

	if(!local_qos_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_qos_lock);
	itr = list_iterator_create(local_qos_list);
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
			list_append(local_qos_list, object);
			break;
		case ACCT_REMOVE_QOS:
			if(!rec) {
				//rc = SLURM_ERROR;
				break;
			}
			list_delete_item(itr);
			break;
		default:
			break;
		}
		if(update->type != ACCT_ADD_QOS) {
			destroy_acct_qos_rec(object);			
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_qos_lock);

	return rc;	
}

extern int assoc_mgr_validate_assoc_id(void *db_conn,
				       uint32_t assoc_id,
				       int enforce)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if(!local_association_list) 
		if(_get_local_association_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if((!local_association_list || !list_count(local_association_list))
	   && !enforce) 
		return SLURM_SUCCESS;
	
	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc_id == found_assoc->id) 
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);

	if(found_assoc || !enforce)
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern void assoc_mgr_clear_used_info(void)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if (!local_association_list)
		return;

	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((found_assoc = list_next(itr))) {
		found_assoc->used_jobs  = 0;
		found_assoc->used_share = 0;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);
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

	if(local_association_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = local_association_list;
		slurm_mutex_lock(&local_association_lock);
		/* let us know what to unpack */
		pack16(DBD_ADD_ASSOCS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_ASSOCS, &msg, buffer);
		slurm_mutex_unlock(&local_association_lock);
	}
	
	if(local_user_list) {
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = local_user_list;
		slurm_mutex_lock(&local_user_lock);
		/* let us know what to unpack */
		pack16(DBD_ADD_USERS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_USERS, &msg, buffer);
		slurm_mutex_unlock(&local_user_lock);
	}

	if(local_qos_list) {		
		memset(&msg, 0, sizeof(dbd_list_msg_t));
		msg.my_list = local_qos_list;
		slurm_mutex_lock(&local_qos_lock);
		/* let us know what to unpack */
		pack16(DBD_ADD_QOS, buffer);
		slurmdbd_pack_list_msg(SLURMDBD_VERSION, 
				       DBD_ADD_QOS, &msg, buffer);	
		slurm_mutex_unlock(&local_qos_lock);
	}

	/* write the buffer to file */
	old_file = xstrdup(state_save_location);
	xstrcat(old_file, "/assoc_mgr_state.old");
	reg_file = xstrdup(state_save_location);
	xstrcat(reg_file, "/assoc_mgr_state");
	new_file = xstrdup(state_save_location);
	xstrcat(new_file, "/assoc_mgr_state.new");
	
	slurm_mutex_lock(&local_file_lock);
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
	slurm_mutex_unlock(&local_file_lock);
	
	free_buf(buffer);
	END_TIMER2("dump_assoc_mgr_state");
	return error_code;

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
	slurm_mutex_lock(&local_file_lock);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		info("No job state file (%s) to recover", state_file);
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
	slurm_mutex_unlock(&local_file_lock);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver > SLURMDBD_VERSION || ver < SLURMDBD_VERSION_MIN) {
		error("***********************************************");
		error("Can not recover assoc_mgr state, incompatable version, got %u need > %u <= %u", ver, SLURMDBD_VERSION_MIN, SLURMDBD_VERSION);
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
			slurm_mutex_lock(&local_association_lock);
			local_association_list = msg->my_list;
			_post_association_list(local_association_list);
			debug("Recovered %u associations", 
			      list_count(local_association_list));
			slurm_mutex_unlock(&local_association_lock);
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
			slurm_mutex_lock(&local_user_lock);
			local_user_list = msg->my_list;
			_post_user_list(local_user_list);
			debug("Recovered %u users", 
			      list_count(local_user_list));
			slurm_mutex_unlock(&local_user_lock);
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
			slurm_mutex_lock(&local_qos_lock);
			local_qos_list = msg->my_list;
			debug("Recovered %u qos", 
			      list_count(local_qos_list));
			slurm_mutex_unlock(&local_qos_lock);
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

	if(cache_level & ASSOC_MGR_CACHE_ASSOC) 
		if(_refresh_local_association_list(db_conn, enforce)
		   == SLURM_ERROR)
			return SLURM_ERROR;

	if(cache_level & ASSOC_MGR_CACHE_QOS)
		if(_refresh_local_qos_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	if(cache_level & ASSOC_MGR_CACHE_USER)
		if(_refresh_local_user_list(db_conn, enforce) == SLURM_ERROR)
			return SLURM_ERROR;

	running_cache = 0;
	
	return SLURM_SUCCESS;
}

