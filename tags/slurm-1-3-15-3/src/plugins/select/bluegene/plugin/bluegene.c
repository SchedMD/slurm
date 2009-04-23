/*****************************************************************************\
 *  bluegene.c - blue gene node configuration processing module. 
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov> et. al.
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

#include "bluegene.h"
#include "defined_block.h"
#include <stdio.h>

#define MMCS_POLL_TIME 30	/* poll MMCS for down switches and nodes 
				 * every 120 secs */
#define BG_POLL_TIME 0	        /* poll bg blocks every 3 secs */

#define _DEBUG 0

char* bg_conf = NULL;

/* Global variables */
List bg_list = NULL;			/* total list of bg_record entries */
List bg_curr_block_list = NULL;  	/* current bg blocks in bluegene.conf*/
List bg_job_block_list = NULL;  	/* jobs running in these blocks */
List bg_booted_block_list = NULL;  	/* blocks that are booted */
List bg_freeing_list = NULL;  	        /* blocks that being freed */

#ifdef HAVE_BGL
List bg_blrtsimage_list = NULL;
#endif
List bg_linuximage_list = NULL;
List bg_mloaderimage_list = NULL;
List bg_ramdiskimage_list = NULL;
#ifdef HAVE_BGL
char *default_blrtsimage = NULL;
#endif
List bg_valid_small32 = NULL;
List bg_valid_small64 = NULL;
List bg_valid_small128 = NULL;
List bg_valid_small256 = NULL;
char *default_linuximage = NULL;
char *default_mloaderimage = NULL, *default_ramdiskimage = NULL;
char *bridge_api_file = NULL; 
char *bg_slurm_user_name = NULL;
char *bg_slurm_node_prefix = NULL;
bg_layout_t bluegene_layout_mode = NO_VAL;
double bluegene_io_ratio = 0.0;
double bluegene_nc_ratio = 0.0;
uint32_t bluegene_smallest_block = 512;
uint16_t bluegene_proc_ratio = 0;
uint16_t bluegene_numpsets = 0;
uint16_t bluegene_bp_node_cnt = 0;
uint16_t bluegene_bp_nodecard_cnt = 0;
uint16_t bluegene_quarter_node_cnt = 0;
uint16_t bluegene_quarter_ionode_cnt = 0;
uint16_t bluegene_nodecard_node_cnt = 0;
uint16_t bluegene_nodecard_ionode_cnt = 0;
uint16_t bridge_api_verb = 0;

bool agent_fini = false;
time_t last_bg_update;
pthread_mutex_t block_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_block_to_free = 0;
int num_block_freed = 0;
int blocks_are_created = 0;
int num_unused_cpus = 0;

pthread_mutex_t freed_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t freed_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t destroy_cond = PTHREAD_COND_INITIALIZER;
List bg_free_block_list = NULL;  	/* blocks to be deleted */
List bg_destroy_block_list = NULL;       /* blocks to be destroyed */
int free_cnt = 0;
int destroy_cnt = 0;

#ifndef HAVE_BG_FILES
# if BA_SYSTEM_DIMENSIONS==3
int max_dim[BA_SYSTEM_DIMENSIONS] = { 0, 0, 0 };
# else
int max_dim[BA_SYSTEM_DIMENSIONS] = { 0 };
# endif
#endif


static void _set_bg_lists();
static int  _validate_config_nodes(List *bg_found_block_list, char *dir);
static int _delete_old_blocks(List bg_found_block_list);
static char *_get_bg_conf(void);
static int  _reopen_bridge_log(void);
static void _destroy_bitmap(void *object);

/* Initialize all plugin variables */
extern int init_bg(void)
{
	ba_init(NULL);

	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
extern void fini_bg(void)
{
	if(!agent_fini) {
		error("The agent hasn't been finied yet!");
		agent_fini = true;
	}
	/* wait for the agent threads to finish up */
	waitfor_block_agents();

	/* wait for the destroy/free threads to finish up */
	if(free_cnt)
		pthread_cond_wait(&freed_cond, &freed_cnt_mutex);
	if(destroy_cnt)
		pthread_cond_wait(&destroy_cond, &freed_cnt_mutex);
	
	if (bg_list) {
		list_destroy(bg_list);
		bg_list = NULL;
	}	
	if (bg_curr_block_list) {
		list_destroy(bg_curr_block_list);
		bg_curr_block_list = NULL;
	}	
	if (bg_job_block_list) {
		list_destroy(bg_job_block_list);
		bg_job_block_list = NULL;
		num_unused_cpus = 0;
	}
	if (bg_booted_block_list) {
		list_destroy(bg_booted_block_list);
		bg_booted_block_list = NULL;
	}
		
#ifdef HAVE_BGL
	if(bg_blrtsimage_list) {
		list_destroy(bg_blrtsimage_list);
		bg_blrtsimage_list = NULL;
	}
#endif	
	if(bg_linuximage_list) {
		list_destroy(bg_linuximage_list);
		bg_linuximage_list = NULL;
	}
	
	if(bg_mloaderimage_list) {
		list_destroy(bg_mloaderimage_list);
		bg_mloaderimage_list = NULL;
	}

	if(bg_ramdiskimage_list) {
		list_destroy(bg_ramdiskimage_list);
		bg_ramdiskimage_list = NULL;
	}
	
	if(bg_valid_small32) {
		list_destroy(bg_valid_small32);
		bg_valid_small32 = NULL;
	}
	if(bg_valid_small64) {
		list_destroy(bg_valid_small64);
		bg_valid_small64 = NULL;
	}
	if(bg_valid_small128) {
		list_destroy(bg_valid_small128);
		bg_valid_small128 = NULL;
	}
	if(bg_valid_small256) {
		list_destroy(bg_valid_small256);
		bg_valid_small256 = NULL;
	}

#ifdef HAVE_BGL
	xfree(default_blrtsimage);
#endif
	xfree(default_linuximage);
	xfree(default_mloaderimage);
	xfree(default_ramdiskimage);
	xfree(bridge_api_file);
	xfree(bg_conf);
	xfree(bg_slurm_user_name);
	xfree(bg_slurm_node_prefix);
	
	ba_fini();
}

/* 
 * block_state_mutex should be locked before calling this function
 */
extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b)
{
	if((rec_a->bp_count > 1) && (rec_b->bp_count > 1)) {
		/* Test for conflicting passthroughs */
		reset_ba_system(false);
		check_and_set_node_list(rec_a->bg_block_list);
		if(check_and_set_node_list(rec_b->bg_block_list)
		   == SLURM_ERROR) 
			return true;
	}
	
	
	if (!bit_overlap(rec_a->bitmap, rec_b->bitmap)) 
		return false;

	if((rec_a->node_cnt >= bluegene_bp_node_cnt)
	   || (rec_b->node_cnt >= bluegene_bp_node_cnt))
		return true;
	
	if (!bit_overlap(rec_a->ionode_bitmap, rec_b->ionode_bitmap)) 
		return false;

	return true;
}

extern int remove_all_users(char *bg_block_id, char *user_name) 
{
	int returnc = REMOVE_USER_NONE;
#ifdef HAVE_BG_FILES
	char *user;
	rm_partition_t *block_ptr = NULL;
	int rc, i, user_count;

	if ((rc = bridge_get_block(bg_block_id,  &block_ptr)) != STATUS_OK) {
		if(rc == INCONSISTENT_DATA
		   && bluegene_layout_mode == LAYOUT_DYNAMIC)
			return REMOVE_USER_FOUND;
			
		error("bridge_get_block(%s): %s", 
		      bg_block_id, 
		      bg_err_str(rc));
		return REMOVE_USER_ERR;
	}	
	
	if((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum, 
				 &user_count)) 
	   != STATUS_OK) {
		error("bridge_get_data(RM_PartitionUsersNum): %s", 
		      bg_err_str(rc));
		returnc = REMOVE_USER_ERR;
		user_count = 0;
	} else
		debug2("got %d users for %s",user_count, bg_block_id);
	for(i=0; i<user_count; i++) {
		if(i) {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionNextUser, 
						  &user)) 
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionNextUser): %s", 
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionFirstUser, 
						  &user)) 
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionFirstUser): %s",
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		}
		if(!user) {
			error("No user was returned from database");
			continue;
		}
		if(!strcmp(user, bg_slurm_user_name)) {
			free(user);
			continue;
		}

		if(user_name) {
			if(!strcmp(user, user_name)) {
				returnc = REMOVE_USER_FOUND;
				free(user);
				continue;
			}
		}
		
		info("Removing user %s from Block %s", user, bg_block_id);
		if ((rc = bridge_remove_block_user(bg_block_id, user)) 
		    != STATUS_OK) {
			debug("user %s isn't on block %s",
			      user, 
			      bg_block_id);
		}
		free(user);
	}
	if ((rc = bridge_free_block(block_ptr)) != STATUS_OK) {
		error("bridge_free_block(): %s", bg_err_str(rc));
	}
#endif
	return returnc;
}

/* if SLURM_ERROR you will need to fail the job with
   slurm_fail_job(bg_record->job_running);
*/

extern int set_block_user(bg_record_t *bg_record) 
{
	int rc = 0;
	debug("resetting the boot state flag and "
	      "counter for block %s.",
	      bg_record->bg_block_id);
	bg_record->boot_state = 0;
	bg_record->boot_count = 0;

	if((rc = update_block_user(bg_record, 1)) == 1) {
		last_bg_update = time(NULL);
		rc = SLURM_SUCCESS;
	} else if (rc == -1) {
		error("Unable to add user name to block %s. "
		      "Cancelling job.",
		      bg_record->bg_block_id);
		rc = SLURM_ERROR;
	}	
	xfree(bg_record->target_name);
	bg_record->target_name = xstrdup(bg_slurm_user_name);

	return rc;
}

extern char* convert_conn_type(rm_connection_type_t conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH): 
		return "MESH"; 
	case (SELECT_TORUS): 
		return "TORUS"; 
	case (SELECT_SMALL): 
		return "SMALL"; 
	case (SELECT_NAV):
		return "NAV";
#ifndef HAVE_BGL
	case SELECT_HTC_S:
		return "HTC_S";
		break;
	case SELECT_HTC_D:
		return "HTC_D";
		break;
	case SELECT_HTC_V:
		return "HTC_V";
		break;
	case SELECT_HTC_L:
		return "HTC_L";
		break;
#endif
	default:
		break;
	}
	return "";
}

#ifdef HAVE_BGL
extern char* convert_node_use(rm_partition_mode_t pt)
{
	switch (pt) {
	case (SELECT_COPROCESSOR_MODE): 
		return "COPROCESSOR"; 
	case (SELECT_VIRTUAL_NODE_MODE): 
		return "VIRTUAL"; 
	default:
		break;
	}
	return "";
}
#endif

/** 
 * sort the partitions by increasing size
 */
extern void sort_bg_record_inc_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) bg_record_cmpf_inc);
	last_bg_update = time(NULL);
}

/*
 * block_agent - thread periodically updates status of
 * bluegene blocks. 
 * 
 */
extern void *block_agent(void *args)
{
	static time_t last_bg_test;
	int rc;
	time_t now = time(NULL);

	last_bg_test = now - BG_POLL_TIME;
	while (!agent_fini) {

		if (difftime(now, last_bg_test) >= BG_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				break;	/* quit now */
			if(blocks_are_created) {
				last_bg_test = now;
				if((rc = update_block_list()) == 1) {
					last_bg_update = now;
				} else if(rc == -1)
					error("Error with update_block_list");
				if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
					if((rc = update_freeing_block_list())
					   == 1) {
						last_bg_update = now;
					} else if(rc == -1)
						error("Error with "
						      "update_block_list 2");
				}
			}
			now = time(NULL);
		}
		
		sleep(1);
	}
	return NULL;
}

/*
 * state_agent - thread periodically updates status of
 * bluegene nodes. 
 * 
 */
extern void *state_agent(void *args)
{
	static time_t last_mmcs_test;
	time_t now = time(NULL);

	last_mmcs_test = now - MMCS_POLL_TIME;
	while (!agent_fini) {
		if (difftime(now, last_mmcs_test) >= MMCS_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				break; 	/* quit now */
			if(blocks_are_created) {
				last_mmcs_test = now;
				/* can run for a while */
				test_mmcs_failures();
			}
		} 	
				
		sleep(1);
		now = time(NULL);
	}
	return NULL;
}

/* must set the protecting mutex if any before this function is called */

extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record)
{
	bg_record_t *found_record = NULL;
	ListIterator itr;
	int rc = SLURM_ERROR;

	if(!bg_record)
		return rc;

	//slurm_mutex_lock(&block_state_mutex);	
	itr = list_iterator_create(my_bg_list);
	while ((found_record = list_next(itr))) {
		if(found_record)
			if(bg_record == found_record) {
				list_remove(itr);
				rc = SLURM_SUCCESS;
				break;
			}
	}
	list_iterator_destroy(itr);
	//slurm_mutex_unlock(&block_state_mutex);

	return rc;
}

/* This is here to remove from the orignal list when dealing with
 * copies like above all locks need to be set.  This function does not
 * free anything you must free it when you are done */
extern bg_record_t *find_and_remove_org_from_bg_list(List my_list, 
						     bg_record_t *bg_record)
{
	ListIterator itr = list_iterator_create(my_list);
	bg_record_t *found_record = NULL;

	while ((found_record = (bg_record_t *) list_next(itr)) != NULL) {
		/* check for full node bitmap compare */
		if(bit_equal(bg_record->bitmap, found_record->bitmap)
		   && bit_equal(bg_record->ionode_bitmap,
				found_record->ionode_bitmap)) {
			if(!strcmp(bg_record->bg_block_id,
				   found_record->bg_block_id)) {
				list_remove(itr);
				debug2("got the block");
				break;
			}
		}
	}
	list_iterator_destroy(itr);
	return found_record;
}

/* This is here to remove from the orignal list when dealing with
 * copies like above all locks need to be set */
extern bg_record_t *find_org_in_bg_list(List my_list, bg_record_t *bg_record)
{
	ListIterator itr = list_iterator_create(my_list);
	bg_record_t *found_record = NULL;

	while ((found_record = (bg_record_t *) list_next(itr)) != NULL) {
		/* check for full node bitmap compare */
		if(bit_equal(bg_record->bitmap, found_record->bitmap)
		   && bit_equal(bg_record->ionode_bitmap,
				found_record->ionode_bitmap)) {
			
			if(!strcmp(bg_record->bg_block_id,
				   found_record->bg_block_id)) {
				debug2("got the block");
				break;
			}
		}
	}
	list_iterator_destroy(itr);
	return found_record;
}

extern int bg_free_block(bg_record_t *bg_record)
{
#ifdef HAVE_BG_FILES
	int rc;
#endif
	if(!bg_record) {
		error("bg_free_block: there was no bg_record");
		return SLURM_ERROR;
	}
	
	while (1) {
		if(!bg_record) {
			error("bg_free_block: there was no bg_record");
			return SLURM_ERROR;
		}
		
		slurm_mutex_lock(&block_state_mutex);			
		if (bg_record->state != NO_VAL
		    && bg_record->state != RM_PARTITION_FREE 
		    && bg_record->state != RM_PARTITION_DEALLOCATING) {
#ifdef HAVE_BG_FILES
			debug2("bridge_destroy %s",bg_record->bg_block_id);
			
			rc = bridge_destroy_block(bg_record->bg_block_id);
			if (rc != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					debug("block %s is not found",
					      bg_record->bg_block_id);
					break;
				} else if(rc == INCOMPATIBLE_STATE) {
					debug2("bridge_destroy_partition"
					       "(%s): %s State = %d",
					       bg_record->bg_block_id, 
					       bg_err_str(rc), 
					       bg_record->state);
				} else {
					error("bridge_destroy_partition"
					      "(%s): %s State = %d",
					      bg_record->bg_block_id, 
					      bg_err_str(rc), 
					      bg_record->state);
				}
			}
#else
			bg_record->state = RM_PARTITION_FREE;	
#endif
		}
		
		if ((bg_record->state == RM_PARTITION_FREE)
#ifdef HAVE_BGL
		    ||  (bg_record->state == RM_PARTITION_ERROR)
#endif
			) {
			break;
		}
		slurm_mutex_unlock(&block_state_mutex);			
		sleep(3);
	}
	remove_from_bg_list(bg_booted_block_list, bg_record);
	slurm_mutex_unlock(&block_state_mutex);			
		
	return SLURM_SUCCESS;
}

/* Free multiple blocks in parallel */
extern void *mult_free_block(void *args)
{
	bg_record_t *bg_record = NULL;
		
	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&freed_cnt_mutex);
		bg_record = list_dequeue(bg_free_block_list);
		slurm_mutex_unlock(&freed_cnt_mutex);
		if (!bg_record) {
			usleep(100000);
			continue;
		}
		if(bg_record->job_ptr) {
			info("We are freeing a block (%s) that "
			     "has job %u(%u), This should never happen.\n",
			     bg_record->bg_block_id,
			     bg_record->job_ptr->job_id, 
			     bg_record->job_running);
			term_jobs_on_block(bg_record->bg_block_id);
		}
		debug("freeing the block %s.", bg_record->bg_block_id);
		bg_free_block(bg_record);	
		debug("done\n");
		slurm_mutex_lock(&freed_cnt_mutex);
		num_block_freed++;
		slurm_mutex_unlock(&freed_cnt_mutex);
	}
	slurm_mutex_lock(&freed_cnt_mutex);
	free_cnt--;
	if(free_cnt == 0) {
		list_destroy(bg_free_block_list);
		bg_free_block_list = NULL;
		pthread_cond_signal(&freed_cond);
	}
	slurm_mutex_unlock(&freed_cnt_mutex);
	return NULL;
}

/* destroy multiple blocks in parallel */
extern void *mult_destroy_block(void *args)
{
	bg_record_t *bg_record = NULL;

#ifdef HAVE_BG_FILES
	int rc;
#endif
	slurm_mutex_lock(&freed_cnt_mutex);
	if ((bg_freeing_list == NULL) 
	    && ((bg_freeing_list = list_create(destroy_bg_record)) == NULL))
		fatal("malloc failure in bg_freeing_list");
	slurm_mutex_unlock(&freed_cnt_mutex);
	
	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&freed_cnt_mutex);
		bg_record = list_dequeue(bg_destroy_block_list);
		slurm_mutex_unlock(&freed_cnt_mutex);
		if (!bg_record) {
			usleep(100000);
			continue;
		}
		slurm_mutex_lock(&block_state_mutex);
		remove_from_bg_list(bg_list, bg_record);
		list_push(bg_freeing_list, bg_record);
		
		/* 
		 * we only are sorting this so when we send it to a
		 * tool such as smap it will be in a nice order
		 */
		sort_bg_record_inc_size(bg_freeing_list);
		if(remove_from_bg_list(bg_job_block_list, bg_record) 
		   == SLURM_SUCCESS) {
			num_unused_cpus += bg_record->cpu_cnt;
		}
		slurm_mutex_unlock(&block_state_mutex);
		debug3("removing the jobs on block %s\n",
		       bg_record->bg_block_id);
		term_jobs_on_block(bg_record->bg_block_id);
		
		debug2("destroying %s", (char *)bg_record->bg_block_id);
		if(bg_free_block(bg_record) == SLURM_ERROR) {
			debug("there was an error");
			goto already_here;
		}
		debug2("done destroying");
		slurm_mutex_lock(&block_state_mutex);
		remove_from_bg_list(bg_freeing_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
								
#ifdef HAVE_BG_FILES
		debug2("removing from database %s", 
		       (char *)bg_record->bg_block_id);
		
		rc = bridge_remove_block(bg_record->bg_block_id);
		if (rc != STATUS_OK) {
			if(rc == PARTITION_NOT_FOUND) {
				debug("block %s is not found",
				      bg_record->bg_block_id);
			} else {
				error("1 rm_remove_partition(%s): %s",
				      bg_record->bg_block_id,
				      bg_err_str(rc));
			}
		} else
			debug2("done %s", 
			       (char *)bg_record->bg_block_id);
#endif
		slurm_mutex_lock(&block_state_mutex);
		destroy_bg_record(bg_record);
		slurm_mutex_unlock(&block_state_mutex);
		last_bg_update = time(NULL);
		debug2("destroyed");
		
	already_here:
		slurm_mutex_lock(&freed_cnt_mutex);
		num_block_freed++;
		slurm_mutex_unlock(&freed_cnt_mutex);
				
	}
	slurm_mutex_lock(&freed_cnt_mutex);
	destroy_cnt--;
	if(destroy_cnt == 0) {
		if(bg_freeing_list) {
			list_destroy(bg_freeing_list);
			bg_freeing_list = NULL;
		}
		list_destroy(bg_destroy_block_list);
		bg_destroy_block_list = NULL;
		pthread_cond_signal(&destroy_cond);
	}
	slurm_mutex_unlock(&freed_cnt_mutex);

	return NULL;
}

extern int free_block_list(List delete_list)
{
	bg_record_t *found_record = NULL;
	int retries;
	List *block_list = NULL;
	int *count = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if(!delete_list || !list_count(delete_list))
		return SLURM_SUCCESS;

	/* set up which list to push onto */
	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
		block_list = &bg_destroy_block_list;
		count = &destroy_cnt;
	} else {
		block_list = &bg_free_block_list;
		count = &free_cnt;
	}
	
	slurm_mutex_lock(&freed_cnt_mutex);
	
	if ((*block_list == NULL) 
	    && ((*block_list = list_create(NULL)) == NULL))
		fatal("malloc failure in free_block_list");
	
	while ((found_record = (bg_record_t*)list_pop(delete_list)) != NULL) {
		/* push job onto queue in a FIFO */
		debug3("adding %s to be freed", found_record->bg_block_id);
		if(!block_ptr_exist_in_list(*block_list, found_record)) {
			if (list_push(*block_list, found_record) == NULL)
				fatal("malloc failure in _block_op/list_push");
		} else {
			error("we had block %s already on the freeing list",
			      found_record->bg_block_id);
			num_block_to_free--;
			continue;
		}
		/* already running MAX_AGENTS we don't really need more 
		   since they don't end until we shut down the controller */
		if (*count > MAX_AGENT_COUNT) 
			continue;
		
		(*count)++;
		
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate(
			    &attr_agent, 
			    PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		retries = 0;
		if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
			while (pthread_create(&thread_agent, 
					      &attr_agent, 
					      mult_destroy_block,
					      NULL)) {
				error("pthread_create "
				      "error %m");
				if (++retries > MAX_PTHREAD_RETRIES)
					fatal("Can't create "
					      "pthread");
				/* sleep and retry */
				usleep(1000);	
			}
		} else {
			while (pthread_create(&thread_agent, 
					      &attr_agent, 
					      mult_free_block, 
					      NULL)) {
				error("pthread_create "
				      "error %m");
				if (++retries > MAX_PTHREAD_RETRIES)
					fatal("Can't create "
					      "pthread");
				/* sleep and retry */
				usleep(1000);	
			}
		}
		slurm_attr_destroy(&attr_agent);
	}
	slurm_mutex_unlock(&freed_cnt_mutex);
	return SLURM_SUCCESS;
}

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * blocks are static/dynamic, torus/mesh, etc.
 */
extern int read_bg_conf(void)
{
	int i;
	int count = 0;
	s_p_hashtbl_t *tbl = NULL;
	char *layout = NULL;
	blockreq_t **blockreq_array = NULL;
	image_t **image_array = NULL;
	image_t *image = NULL;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;
	ListIterator itr = NULL;
	
	debug("Reading the bluegene.conf file");

	/* check if config file has changed */
	if (!bg_conf)
		bg_conf = _get_bg_conf();
	if (stat(bg_conf, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bg_conf);
	if (last_config_update) {
		_reopen_bridge_log();
		if(last_config_update == config_stat.st_mtime)
			debug("%s unchanged", bg_conf);
		else {
			info("Restart slurmctld for %s changes to take effect", 
			     bg_conf);
		}
		last_config_update = config_stat.st_mtime; 
		return SLURM_SUCCESS;
	}
	last_config_update = config_stat.st_mtime; 

	/* initialization */
	/* bg_conf defined in bg_node_alloc.h */
	tbl = s_p_hashtbl_create(bg_conf_file_options);
	
	if(s_p_parse_file(tbl, bg_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading bluegene "
		      "conf file");
	
	_set_bg_lists();	
#ifdef HAVE_BGL
	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltBlrtsImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_blrtsimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_blrtsimage, "BlrtsImage", tbl)) {
		if(!list_count(bg_blrtsimage_list))
			fatal("BlrtsImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_blrtsimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_blrtsimage = xstrdup(image->name);
		info("Warning: using %s as the default BlrtsImage.  "
		     "If this isn't correct please set BlrtsImage",
		     default_blrtsimage); 
	} else {
		debug3("default BlrtsImage %s", default_blrtsimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_blrtsimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_blrtsimage_list, image);
	}
		
	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltLinuxImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_linuximage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_linuximage, "LinuxImage", tbl)) {
		if(!list_count(bg_linuximage_list))
			fatal("LinuxImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_linuximage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_linuximage = xstrdup(image->name);
		info("Warning: using %s as the default LinuxImage.  "
		     "If this isn't correct please set LinuxImage",
		     default_linuximage); 
	} else {
		debug3("default LinuxImage %s", default_linuximage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_linuximage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_linuximage_list, image);		
	}

	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltRamDiskImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_ramdiskimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_ramdiskimage,
			    "RamDiskImage", tbl)) {
		if(!list_count(bg_ramdiskimage_list))
			fatal("RamDiskImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_ramdiskimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_ramdiskimage = xstrdup(image->name);
		info("Warning: using %s as the default RamDiskImage.  "
		     "If this isn't correct please set RamDiskImage",
		     default_ramdiskimage); 
	} else {
		debug3("default RamDiskImage %s", default_ramdiskimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_ramdiskimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_ramdiskimage_list, image);		
	}
#else

	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltCnloadImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_linuximage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_linuximage, "CnloadImage", tbl)) {
		if(!list_count(bg_linuximage_list))
			fatal("CnloadImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_linuximage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_linuximage = xstrdup(image->name);
		info("Warning: using %s as the default CnloadImage.  "
		     "If this isn't correct please set CnloadImage",
		     default_linuximage); 
	} else {
		debug3("default CnloadImage %s", default_linuximage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_linuximage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_linuximage_list, image);		
	}

	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltIoloadImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_ramdiskimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_ramdiskimage,
			    "IoloadImage", tbl)) {
		if(!list_count(bg_ramdiskimage_list))
			fatal("IoloadImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_ramdiskimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_ramdiskimage = xstrdup(image->name);
		info("Warning: using %s as the default IoloadImage.  "
		     "If this isn't correct please set IoloadImage",
		     default_ramdiskimage); 
	} else {
		debug3("default IoloadImage %s", default_ramdiskimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_ramdiskimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_ramdiskimage_list, image);		
	}

#endif
	if (s_p_get_array((void ***)&image_array, 
			  &count, "AltMloaderImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_mloaderimage_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&default_mloaderimage,
			    "MloaderImage", tbl)) {
		if(!list_count(bg_mloaderimage_list))
			fatal("MloaderImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_mloaderimage_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		default_mloaderimage = xstrdup(image->name);
		info("Warning: using %s as the default MloaderImage.  "
		     "If this isn't correct please set MloaderImage",
		     default_mloaderimage); 
	} else {
		debug3("default MloaderImage %s", default_mloaderimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(default_mloaderimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_mloaderimage_list, image);		
	}

	if (!s_p_get_uint16(
		    &bluegene_bp_node_cnt, "BasePartitionNodeCnt", tbl)) {
		error("BasePartitionNodeCnt not configured in bluegene.conf "
		      "defaulting to 512 as BasePartitionNodeCnt");
		bluegene_bp_node_cnt = 512;
		bluegene_quarter_node_cnt = 128;
	} else {
		if(bluegene_bp_node_cnt<=0)
			fatal("You should have more than 0 nodes "
			      "per base partition");

		bluegene_quarter_node_cnt = bluegene_bp_node_cnt/4;
	}

	/* select_p_node_init needs to be called before this to set
	   this up correctly
	*/
	bluegene_proc_ratio = procs_per_node/bluegene_bp_node_cnt;
	if(!bluegene_proc_ratio)
		fatal("We appear to have less than 1 proc on a cnode.  "
		      "You specified %u for BasePartitionNodeCnt "
		      "in the blugene.conf and %u procs "
		      "for each node in the slurm.conf",
		      bluegene_bp_node_cnt, procs_per_node);

	if (!s_p_get_uint16(
		    &bluegene_nodecard_node_cnt, "NodeCardNodeCnt", tbl)) {
		error("NodeCardNodeCnt not configured in bluegene.conf "
		      "defaulting to 32 as NodeCardNodeCnt");
		bluegene_nodecard_node_cnt = 32;
	}
	
	if(bluegene_nodecard_node_cnt<=0)
		fatal("You should have more than 0 nodes per nodecard");

	bluegene_bp_nodecard_cnt = 
		bluegene_bp_node_cnt / bluegene_nodecard_node_cnt;

	if (!s_p_get_uint16(&bluegene_numpsets, "Numpsets", tbl))
		fatal("Warning: Numpsets not configured in bluegene.conf");

	if(bluegene_numpsets) {
		bitstr_t *tmp_bitmap = NULL;
		int small_size = 1;

		/* THIS IS A HACK TO MAKE A 1 NODECARD SYSTEM WORK */
		if(bluegene_bp_node_cnt == bluegene_nodecard_node_cnt) {
			bluegene_quarter_ionode_cnt = 2;
			bluegene_nodecard_ionode_cnt = 2;
		} else {
			bluegene_quarter_ionode_cnt = bluegene_numpsets/4;
			bluegene_nodecard_ionode_cnt =
				bluegene_quarter_ionode_cnt/4;
		}
			
		/* How many nodecards per ionode */
		bluegene_nc_ratio = 
			((double)bluegene_bp_node_cnt 
			 / (double)bluegene_nodecard_node_cnt) 
			/ (double)bluegene_numpsets;
		/* How many ionodes per nodecard */
		bluegene_io_ratio = 
			(double)bluegene_numpsets /
			((double)bluegene_bp_node_cnt 
			 / (double)bluegene_nodecard_node_cnt);
		//info("got %f %f", bluegene_nc_ratio, bluegene_io_ratio);
		/* figure out the smallest block we can have on the
		   system */
#ifdef HAVE_BGL
		if(bluegene_io_ratio >= 2)
			bluegene_smallest_block=32;
		else
			bluegene_smallest_block=128;
#else
		if(bluegene_io_ratio >= 2)
			bluegene_smallest_block=16;
		else if(bluegene_io_ratio == 1)
			bluegene_smallest_block=32;
		else if(bluegene_io_ratio == .5)
			bluegene_smallest_block=64;
		else if(bluegene_io_ratio == .25)
			bluegene_smallest_block=128;
		else if(bluegene_io_ratio == .125)
			bluegene_smallest_block=256;
		else {
			error("unknown ioratio %f.  Can't figure out "
			      "smallest block size, setting it to midplane");
			bluegene_smallest_block=512;
		}
#endif
		debug("Smallest block possible on this system is %u",
		      bluegene_smallest_block);
		/* below we are creating all the possible bitmaps for
		 * each size of small block
		 */
		if((int)bluegene_nodecard_ionode_cnt < 1) {
			bluegene_nodecard_ionode_cnt = 0;
		} else {
			bg_valid_small32 = list_create(_destroy_bitmap);
			if((small_size = bluegene_nodecard_ionode_cnt))
				small_size--;
			i = 0;
			while(i<bluegene_numpsets) {
				tmp_bitmap = bit_alloc(bluegene_numpsets);
				bit_nset(tmp_bitmap, i, i+small_size);
				i += small_size+1;
				list_append(bg_valid_small32, tmp_bitmap);
			}
		}
		/* If we only have 1 nodecard just jump to the end
		   since this will never need to happen below.
		   Pretty much a hack to avoid seg fault;). */
		if(bluegene_bp_node_cnt == bluegene_nodecard_node_cnt) 
			goto no_calc;

		bg_valid_small128 = list_create(_destroy_bitmap);
		if((small_size = bluegene_quarter_ionode_cnt))
			small_size--;
		i = 0;
		while(i<bluegene_numpsets) {
			tmp_bitmap = bit_alloc(bluegene_numpsets);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_valid_small128, tmp_bitmap);
		}

#ifndef HAVE_BGL
		bg_valid_small64 = list_create(_destroy_bitmap);
		if((small_size = bluegene_nodecard_ionode_cnt * 2))
			small_size--;
		i = 0;
		while(i<bluegene_numpsets) {
			tmp_bitmap = bit_alloc(bluegene_numpsets);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_valid_small64, tmp_bitmap);
		}

		bg_valid_small256 = list_create(_destroy_bitmap);
		if((small_size = bluegene_quarter_ionode_cnt * 2))
			small_size--;
		i = 0;
		while(i<bluegene_numpsets) {
			tmp_bitmap = bit_alloc(bluegene_numpsets);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_valid_small256, tmp_bitmap);
		}
#endif			
	} else {
		fatal("your numpsets is 0");
	}

no_calc:

	if (!s_p_get_uint16(&bridge_api_verb, "BridgeAPIVerbose", tbl))
		info("Warning: BridgeAPIVerbose not configured "
		     "in bluegene.conf");
	if (!s_p_get_string(&bridge_api_file, "BridgeAPILogFile", tbl)) 
		info("BridgeAPILogFile not configured in bluegene.conf");
	else
		_reopen_bridge_log();

	if (s_p_get_string(&layout, "DenyPassthrough", tbl)) {
		if(strstr(layout, "X")) 
			ba_deny_pass |= PASS_DENY_X;
		if(strstr(layout, "Y")) 
			ba_deny_pass |= PASS_DENY_Y;
		if(strstr(layout, "Z")) 
			ba_deny_pass |= PASS_DENY_Z;
		if(!strcasecmp(layout, "ALL")) 
			ba_deny_pass |= PASS_DENY_ALL;
		
		xfree(layout);
	}

	if (!s_p_get_string(&layout, "LayoutMode", tbl)) {
		info("Warning: LayoutMode was not specified in bluegene.conf "
		     "defaulting to STATIC partitioning");
		bluegene_layout_mode = LAYOUT_STATIC;
	} else {
		if(!strcasecmp(layout,"STATIC")) 
			bluegene_layout_mode = LAYOUT_STATIC;
		else if(!strcasecmp(layout,"OVERLAP")) 
			bluegene_layout_mode = LAYOUT_OVERLAP;
		else if(!strcasecmp(layout,"DYNAMIC")) 
			bluegene_layout_mode = LAYOUT_DYNAMIC;
		else {
			fatal("I don't understand this LayoutMode = %s", 
			      layout);
		}
		xfree(layout);
	}

	/* add blocks defined in file */
	if(bluegene_layout_mode != LAYOUT_DYNAMIC) {
		if (!s_p_get_array((void ***)&blockreq_array, 
				   &count, "BPs", tbl)) {
			info("WARNING: no blocks defined in bluegene.conf, "
			     "only making full system block");
			create_full_system_block(NULL);
		}
		
		for (i = 0; i < count; i++) {
			add_bg_record(bg_list, NULL, blockreq_array[i], 0, 0);
		}
	}
	s_p_hashtbl_destroy(tbl);

	return SLURM_SUCCESS;
}

extern int validate_current_blocks(char *dir)
{
	/* found bg blocks already on system */
	List bg_found_block_list = NULL;
	static time_t last_config_update = (time_t) 0;
	ListIterator itr = NULL;
	bg_record_t *bg_record = NULL;

	/* only run on startup */
	if(last_config_update)
		return SLURM_SUCCESS;

	last_config_update = time(NULL);
	bg_found_block_list = list_create(NULL);
//#if 0	
	/* Check to see if the configs we have are correct */
	if (_validate_config_nodes(&bg_found_block_list, dir) == SLURM_ERROR) { 
		_delete_old_blocks(bg_found_block_list);
	}
//#endif
	/* looking for blocks only I created */
	if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
		init_wires();
		info("No blocks created until jobs are submitted");
	} else {
		if (create_defined_blocks(bluegene_layout_mode,
					  bg_found_block_list) 
		    == SLURM_ERROR) {
			/* error in creating the static blocks, so
			 * blocks referenced by submitted jobs won't
			 * correspond to actual slurm blocks.
			 */
			fatal("Error, could not create the static blocks");
			return SLURM_ERROR;
		}
	} 
	
	/* ok now since bg_list has been made we now can put blocks in
	   an error state this needs to be done outside of a lock
	   it doesn't matter much in the first place though since
	   no threads are started before this function. */
	itr = list_iterator_create(bg_list);
	while((bg_record = list_next(itr))) {
		if(bg_record->state == RM_PARTITION_ERROR) 
			put_block_in_error_state(bg_record, BLOCK_ERROR_STATE);
	}
	list_iterator_destroy(itr);

	slurm_mutex_lock(&block_state_mutex);
	list_destroy(bg_curr_block_list);
	bg_curr_block_list = NULL;
	if(bg_found_block_list) {
		list_destroy(bg_found_block_list);
		bg_found_block_list = NULL;
	}

	last_bg_update = time(NULL);
	blocks_are_created = 1;
	sort_bg_record_inc_size(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
	debug("Blocks have finished being created.");
	return SLURM_SUCCESS;
}


static void _set_bg_lists()
{
	slurm_mutex_lock(&block_state_mutex);
	if(bg_booted_block_list) 
		list_destroy(bg_booted_block_list);
	bg_booted_block_list = list_create(NULL);
	if(bg_job_block_list) 
		list_destroy(bg_job_block_list);
	bg_job_block_list = list_create(NULL);	
	num_unused_cpus = 
		DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z] * procs_per_node;
	if(bg_curr_block_list)
		list_destroy(bg_curr_block_list);	
	bg_curr_block_list = list_create(destroy_bg_record);
	
	if(bg_list) 
		list_destroy(bg_list);
	bg_list = list_create(destroy_bg_record);

	slurm_mutex_unlock(&block_state_mutex);	
	
#ifdef HAVE_BGL
	if(bg_blrtsimage_list)
		list_destroy(bg_blrtsimage_list);
	bg_blrtsimage_list = list_create(destroy_image);
#endif
	if(bg_linuximage_list)
		list_destroy(bg_linuximage_list);
	bg_linuximage_list = list_create(destroy_image);
	if(bg_mloaderimage_list)
		list_destroy(bg_mloaderimage_list);
	bg_mloaderimage_list = list_create(destroy_image);
	if(bg_ramdiskimage_list)
		list_destroy(bg_ramdiskimage_list);
	bg_ramdiskimage_list = list_create(destroy_image);	
}

/*
 * _validate_config_nodes - Match slurm configuration information with
 *                          current BG block configuration.
 * IN/OUT bg_found_block_list - if NULL is created and then any blocks
 *                              found on the system are then pushed on.
 * RET - SLURM_SUCCESS if they match, else an error 
 * code. Writes bg_block_id into bg_list records.
 */

static int _validate_config_nodes(List *bg_found_block_list, char *dir)
{
	int rc = SLURM_ERROR;
	bg_record_t* bg_record = NULL;	
	bg_record_t* init_bg_record = NULL;
	int full_created = 0;
	ListIterator itr_conf;
	ListIterator itr_curr;
	char tmp_char[256];

#ifdef HAVE_BG_FILES
	/* read current bg block info into bg_curr_block_list This
	 * happens in the state load before this in emulation mode */
	if (read_bg_blocks() == SLURM_ERROR)
		return SLURM_ERROR;
	/* since we only care about error states here we don't care
	   about the return code this must be done after the bg_list
	   is created */
	load_state_file(dir);
#else
	/* read in state from last run. */
	if ((rc = load_state_file(dir)) != SLURM_SUCCESS)
		return rc;
	/* This needs to be reset to SLURM_ERROR or it will never we
	   that way again ;). */
	rc = SLURM_ERROR;
#endif	
	if(!bg_recover) 
		return SLURM_ERROR;

	if(!bg_curr_block_list)
		return SLURM_ERROR;
	
	if(!*bg_found_block_list)
		(*bg_found_block_list) = list_create(NULL);

	itr_curr = list_iterator_create(bg_curr_block_list);
	itr_conf = list_iterator_create(bg_list);
	while ((bg_record = (bg_record_t*) list_next(itr_conf))) {
		list_iterator_reset(itr_curr);
		while ((init_bg_record = list_next(itr_curr))) {
			if (strcasecmp(bg_record->nodes, init_bg_record->nodes))
				continue; /* wrong nodes */
			if(!bit_equal(bg_record->ionode_bitmap,
				      init_bg_record->ionode_bitmap))
				continue;
#ifdef HAVE_BGL
			if (bg_record->conn_type != init_bg_record->conn_type)
				continue; /* wrong conn_type */
			if(bg_record->blrtsimage &&
			   strcasecmp(bg_record->blrtsimage,
				      init_bg_record->blrtsimage)) 
				continue;
#else
			if ((bg_record->conn_type != init_bg_record->conn_type)
			    && ((bg_record->conn_type < SELECT_SMALL)
				&& (init_bg_record->conn_type < SELECT_SMALL)))
				continue; /* wrong conn_type */
#endif
			if(bg_record->linuximage &&
			   strcasecmp(bg_record->linuximage,
				      init_bg_record->linuximage))
				continue;
			if(bg_record->mloaderimage &&
			   strcasecmp(bg_record->mloaderimage,
				      init_bg_record->mloaderimage))
				continue;
			if(bg_record->ramdiskimage &&
			   strcasecmp(bg_record->ramdiskimage,
				      init_bg_record->ramdiskimage))
				continue;
		       			
			copy_bg_record(init_bg_record, bg_record);
			/* remove from the curr list since we just
			   matched it no reason to keep it around
			   anymore */
			list_delete_item(itr_curr);
			break;
		}
			
		if (!bg_record->bg_block_id) {
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("Block found in bluegene.conf to be "
			     "created: Nodes:%s", 
			     tmp_char);
			rc = SLURM_ERROR;
		} else {
			if(bg_record->full_block)
				full_created = 1;

			list_push(*bg_found_block_list, bg_record);
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("Existing: BlockID:%s Nodes:%s Conn:%s",
			     bg_record->bg_block_id, 
			     tmp_char,
			     convert_conn_type(bg_record->conn_type));
			if(((bg_record->state == RM_PARTITION_READY)
			    || (bg_record->state == RM_PARTITION_CONFIGURING))
			   && !block_ptr_exist_in_list(bg_booted_block_list, 
						       bg_record))
				list_push(bg_booted_block_list, bg_record);
		}
	}		
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		goto finished;

	if(!full_created) {
		list_iterator_reset(itr_curr);
		while ((init_bg_record = list_next(itr_curr))) {
			if(init_bg_record->full_block) {
				list_remove(itr_curr);
				bg_record = init_bg_record;
				list_append(bg_list, bg_record);
				list_push(*bg_found_block_list, bg_record);
				format_node_name(bg_record, tmp_char,
						 sizeof(tmp_char));
				info("Existing: BlockID:%s Nodes:%s Conn:%s",
				     bg_record->bg_block_id, 
				     tmp_char,
				     convert_conn_type(bg_record->conn_type));
				if(((bg_record->state == RM_PARTITION_READY)
				    || (bg_record->state 
					== RM_PARTITION_CONFIGURING))
				   && !block_ptr_exist_in_list(
					   bg_booted_block_list, bg_record))
					list_push(bg_booted_block_list,
						  bg_record);
				break;
			}
		}
	}
		
finished:
	list_iterator_destroy(itr_conf);
	list_iterator_destroy(itr_curr);
	if(!list_count(bg_curr_block_list))
		rc = SLURM_SUCCESS;
	return rc;
}

static int _delete_old_blocks(List bg_found_block_list)
{
	ListIterator itr_curr, itr_found;
	bg_record_t *found_record = NULL, *init_record = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	List bg_destroy_list = list_create(NULL);

	info("removing unspecified blocks");
	if(!bg_recover) {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = 
				(bg_record_t*)list_next(itr_curr))) {
				list_remove(itr_curr);
				list_push(bg_destroy_list, init_record);
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 1");
			list_destroy(bg_destroy_list);
			return SLURM_ERROR;
		}
	} else {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = list_next(itr_curr))) {
				if(bg_found_block_list) {
					itr_found = list_iterator_create(
						bg_found_block_list);
					while ((found_record 
						= list_next(itr_found)) 
					       != NULL) {
						if (!strcmp(init_record->
							    bg_block_id, 
							    found_record->
							    bg_block_id)) {
							/* don't delete 
							   this one 
							*/
							break;	
						}
					}
					list_iterator_destroy(itr_found);
				} else {
					error("_delete_old_blocks: "
					      "no bg_found_block_list");
					list_iterator_destroy(itr_curr);
					list_destroy(bg_destroy_list);
					return SLURM_ERROR;
				}
				if(found_record == NULL) {
					list_remove(itr_curr);
					list_push(bg_destroy_list, 
						  init_record);
				}
			}		
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 2");
			list_destroy(bg_destroy_list);
			return SLURM_ERROR;
		}
	}

	slurm_mutex_lock(&freed_cnt_mutex);
	if ((bg_destroy_block_list == NULL) 
	    && ((bg_destroy_block_list = list_create(NULL)) == NULL))
		fatal("malloc failure in block_list");

	itr_curr = list_iterator_create(bg_destroy_list);
	while ((init_record = (bg_record_t*) list_next(itr_curr))) {
		list_push(bg_destroy_block_list, init_record);
		num_block_to_free++;
		if (destroy_cnt > MAX_AGENT_COUNT) 
			continue;
		
		destroy_cnt++;

		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate(&attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		retries = 0;
		while (pthread_create(&thread_agent, 
				      &attr_agent, 
				      mult_destroy_block, 
				      NULL)) {
			error("pthread_create "
			      "error %m");
			if (++retries > MAX_PTHREAD_RETRIES)
				fatal("Can't create "
				      "pthread");
			/* sleep and retry */
			usleep(1000);	
		}
		slurm_attr_destroy(&attr_agent);
	}
	list_iterator_destroy(itr_curr);
	slurm_mutex_unlock(&freed_cnt_mutex);
	list_destroy(bg_destroy_list);
		
	retries=30;
	while(num_block_to_free > num_block_freed) {
		/* no need to check for return code here, things
		   haven't started up yet. */
		update_freeing_block_list();
		if(retries==30) {
			info("Waiting for old blocks to be "
			     "freed.  Have %d of %d",
			     num_block_freed, 
			     num_block_to_free);
			retries=0;
		}
		retries++;
		sleep(1);
	}
	
	info("I am done deleting");

	return SLURM_SUCCESS;
}

static char *_get_bg_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc;
	int i;

	if (!val)
		return xstrdup(BLUEGENE_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("bluegene.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "bluegene.conf");
	return rc;
}

static int _reopen_bridge_log(void)
{
	int rc = SLURM_SUCCESS;

	if (bridge_api_file == NULL)
		return rc;
	
#ifdef HAVE_BG_FILES
	rc = bridge_set_log_params(bridge_api_file, bridge_api_verb);
#endif
	debug3("Bridge api file set to %s, verbose level %d\n", 
	       bridge_api_file, bridge_api_verb);
	
	return rc;
}

static void _destroy_bitmap(void *object)
{
	bitstr_t *bitstr = (bitstr_t *)object;

	if(bitstr) {
		FREE_NULL_BITMAP(bitstr);
	}
}
