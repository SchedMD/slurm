/*****************************************************************************\
 *  bg_block_info.c - blue gene partition information from the db2 database.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#  if WITH_PTHREADS
#    include <pthread.h>
#  endif
#endif

#include <signal.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include <pwd.h>
#include <sys/types.h>
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "src/api/job_info.h"
#include "bluegene.h"

#define _DEBUG 0
#define RETRY_BOOT_COUNT 3

#ifdef HAVE_BG_FILES
static int  _block_is_deallocating(bg_record_t *bg_record);
static void _drain_as_needed(char *node_list, char *reason);

static int _block_is_deallocating(bg_record_t *bg_record)
{
	if(remove_all_users(bg_record->bg_block_id, NULL) 
	   == REMOVE_USER_ERR) {
		error("Something happened removing "
		      "users from partition %s", 
		      bg_record->bg_block_id);
	} 
	
	if(bg_record->target_name 
	   && bg_record->user_name) {
		if(!strcmp(bg_record->target_name, 
			   slurmctld_conf.slurm_user_name)) {
			if(strcmp(bg_record->target_name, 
				  bg_record->user_name)) {
				error("Block %s was in a ready state "
				      "for user %s but is being freed. "
				      "Job %d was lost.",
				      bg_record->bg_block_id,
				      bg_record->user_name,
				      bg_record->job_running);
				(void) slurm_fail_job(bg_record->job_running);
				slurm_mutex_unlock(&block_state_mutex);
				remove_from_bg_list(bg_job_block_list, 
						    bg_record);
				slurm_mutex_lock(&block_state_mutex);
			} else {
				debug("Block %s was in a ready state "
				      "but is being freed. No job running.",
				      bg_record->bg_block_id);
			}
		} else {
			error("State went to free on a boot "
			      "for partition %s.",
			      bg_record->bg_block_id);
		}
		slurm_mutex_unlock(&block_state_mutex);
		remove_from_bg_list(bg_booted_block_list, bg_record);
		slurm_mutex_lock(&block_state_mutex);
	} else if(bg_record->user_name) {
		error("Target Name was not set "
		      "not set for partition %s.",
		      bg_record->bg_block_id);
		bg_record->target_name = 
			xstrdup(bg_record->user_name);
	} else {
		error("Target Name and User Name are "
		      "not set for partition %s.",
		      bg_record->bg_block_id);
		bg_record->user_name = 
			xstrdup(slurmctld_conf.slurm_user_name);
		bg_record->target_name = 
			xstrdup(bg_record->user_name);
	}
	return SLURM_SUCCESS;
}

/* If any nodes in node_list are drained, draining, or down, 
 *   then just return
 *   else drain all of the nodes
 * This function lets us drain an entire bgblock only if 
 * we have not already identified a specific node as bad. */
static void _drain_as_needed(char *node_list, char *reason)
{
	bool needed = true;
	hostlist_t hl;
	char *host;

	/* scan node list */
	hl = hostlist_create(node_list);
	if (!hl) {
		slurm_drain_nodes(node_list, reason);
		return;
	}
	while ((host = hostlist_shift(hl))) {
		if (node_already_down(host)) {
			needed = false;
			break;
		}
	}
	hostlist_destroy(hl);

	if (needed)
		slurm_drain_nodes(node_list, reason);
}
#endif

/*
 * check to see if partition is ready to execute.  Meaning
 * User is added to the list of users able to run, and no one 
 * else is running on the partition.
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BG block is available for use.
 */
extern int block_ready(struct job_record *job_ptr)
{
	int rc = 1;
	char *block_id = NULL;
	bg_record_t *bg_record = NULL;
	
	rc = select_g_get_jobinfo(job_ptr->select_jobinfo,
				  SELECT_DATA_BLOCK_ID, &block_id);
	if (rc == SLURM_SUCCESS) {
		bg_record = find_bg_record_in_list(bg_list, block_id);
		slurm_mutex_lock(&block_state_mutex);
		
		if(bg_record) {
			if ((bg_record->user_uid == job_ptr->user_id)
			    && (bg_record->state == RM_PARTITION_READY)) {
				rc = 1;
			} else if (bg_record->user_uid != job_ptr->user_id)
				rc = 0;
			else
				rc = READY_JOB_ERROR;	/* try again */
		} else {
			error("block_ready: partition %s not in bg_list.",
			      block_id);
			rc = READY_JOB_FATAL;	/* fatal error */
		}
		slurm_mutex_unlock(&block_state_mutex);
		xfree(block_id);
	} else
		rc = READY_JOB_ERROR;
	return rc;
}				

/* Pack all relevent information about a partition */
extern void pack_block(bg_record_t *bg_record, Buf buffer)
{
	packstr(bg_record->nodes, buffer);
	packstr(bg_record->user_name, buffer);
	packstr(bg_record->bg_block_id, buffer);
	pack16((uint16_t)bg_record->state, buffer);
	pack16((uint16_t)bg_record->conn_type, buffer);
	pack16((uint16_t)bg_record->node_use, buffer);	
	pack16((uint16_t)bg_record->quarter, buffer);	
	pack16((uint16_t)bg_record->nodecard, buffer);	
	pack32((uint32_t)bg_record->node_cnt, buffer);	
}

extern int update_block_list()
{
	int updated = 0;
#ifdef HAVE_BG_FILES
	int j, rc, num_blocks = 0;
	rm_partition_t *block_ptr = NULL;
	rm_partition_mode_t node_use;
	rm_partition_state_t state;
	rm_partition_state_flag_t block_state = PARTITION_ALL_FLAG;
	char *name = NULL;
	rm_partition_list_t *block_list = NULL;
	bg_record_t *bg_record = NULL;
	time_t now;
	struct tm *time_ptr;
	char reason[128];
	int skipped_dealloc = 0;

	slurm_mutex_lock(&api_file_mutex);
	if ((rc = rm_get_partitions_info(block_state, &block_list))
	    != STATUS_OK) {
		slurm_mutex_unlock(&api_file_mutex);
		if(rc != PARTITION_NOT_FOUND)
			error("rm_get_partitions_info(): %s", bg_err_str(rc));
		return -1; 
	}
	
	if ((rc = rm_get_data(block_list, RM_PartListSize, &num_blocks))
		   != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", bg_err_str(rc));
		updated = -1;
		num_blocks = 0;
	}
	slurm_mutex_unlock(&api_file_mutex);
			
	for (j=0; j<num_blocks; j++) {
		if (j) {
			if ((rc = rm_get_data(block_list, RM_PartListNextPart, 
					      &block_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
				      bg_err_str(rc));
				updated = -1;
				break;
			}
		} else {
			if ((rc = rm_get_data(block_list, RM_PartListFirstPart,
					      &block_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s",
				      bg_err_str(rc));
				updated = -1;
				break;
			}
		}
		if ((rc = rm_get_data(block_ptr, RM_PartitionID, &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s", 
			      bg_err_str(rc));
			updated = -1;
			break;
		}
		if(!name) {
			error("No Partition ID was returned from database");
			continue;
		}
		if(strncmp("RMP", name, 3)) {
			free(name);
			continue;
		}
		bg_record = find_bg_record_in_list(bg_list, name);
		
		if(bg_record == NULL) {
			if(find_bg_record_in_list(bg_freeing_list, name)) {
				break;
			}
			debug("Block %s not found in bg_list "
			      "removing from database", name);
			term_jobs_on_block(name);
			if ((rc = rm_get_data(block_ptr, 
					      RM_PartitionState, &state))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionState): %s",
				      bg_err_str(rc));
				updated = -1;
				break;
			}
			slurm_mutex_lock(&api_file_mutex);
			if (state != NO_VAL
			    && state != RM_PARTITION_FREE 
			    && state != RM_PARTITION_DEALLOCATING) {
				if ((rc = pm_destroy_partition(name)) 
				    != STATUS_OK) {
					if(rc == PARTITION_NOT_FOUND) {
						debug("partition %s is "
						      "not found",
						      name);
						free(name);
						slurm_mutex_unlock(
							&api_file_mutex);
						break;
					}
					error("pm_destroy_partition(%s): %s",
					      name, 
					      bg_err_str(rc));
				}
			}
			if ((state == RM_PARTITION_FREE)
			    ||  (state == RM_PARTITION_ERROR)) {
				rc = rm_remove_partition(name);
				if (rc != STATUS_OK) {
					if(rc == PARTITION_NOT_FOUND) {
						debug("1 block %s not found",
						      name);
					} else {
						error("1 rm_remove_partition"
						      "(%s): %s",
						      name,
						      bg_err_str(rc));
					}
				} else
					debug("done\n");
			}
			slurm_mutex_unlock(&api_file_mutex);
			
			free(name);
			continue;
		}
		free(name);
			
		slurm_mutex_lock(&block_state_mutex);
		
		if ((rc = rm_get_data(block_ptr, RM_PartitionMode, &node_use))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionMode): %s",
			      bg_err_str(rc));
			updated = -1;
			slurm_mutex_unlock(&block_state_mutex);
			break;
		} else if(bg_record->node_use != node_use) {
			debug("node_use of Partition %s was %d and now is %d",
			      bg_record->bg_block_id, 
			      bg_record->node_use, 
			      node_use);
			bg_record->node_use = node_use;
			updated = 1;
		}
		
		if ((rc = rm_get_data(block_ptr, RM_PartitionState, &state))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionState): %s",
			      bg_err_str(rc));
			updated = -1;
			slurm_mutex_unlock(&block_state_mutex);
			break;
		} else if(bg_record->state != state) {
			debug("state of Partition %s was %d and now is %d",
			      bg_record->bg_block_id, 
			      bg_record->state, 
			      state);
			/* 
			   check to make sure partition went 
			   through freeing correctly 
			*/
			if(bg_record->state != RM_PARTITION_DEALLOCATING
			   && state == RM_PARTITION_FREE)
				skipped_dealloc = 1;
			bg_record->state = state;
			if(bg_record->state == RM_PARTITION_DEALLOCATING) {
				_block_is_deallocating(bg_record);
			} else if(skipped_dealloc) {
				_block_is_deallocating(bg_record);
				skipped_dealloc = 0;
			} else if(bg_record->state 
				  == RM_PARTITION_CONFIGURING)
				bg_record->boot_state = 1;
			updated = 1;
		}

		/* check the boot state */
		/* debug("boot state for partition %s is %d", */
/* 		      bg_record->bg_block_id, */
/* 		      bg_record->boot_state); */
		if(bg_record->boot_state == 1) {
			switch(bg_record->state) {
			case RM_PARTITION_CONFIGURING:
				/* debug("checking to make sure user %s " */
/* 				      "is the user.",  */
/* 				      bg_record->target_name); */
				if(update_block_user(bg_record, 0) == 1)
					last_bg_update = time(NULL);
				break;
			case RM_PARTITION_ERROR:
				error("partition in an error state");
			case RM_PARTITION_FREE:
				if(bg_record->boot_count < RETRY_BOOT_COUNT) {
					slurm_mutex_unlock(&block_state_mutex);
					if((rc = boot_block(bg_record))
					   != SLURM_SUCCESS) {
						updated = -1;
					}
					slurm_mutex_lock(&block_state_mutex);
					debug("boot count for partition %s "
					      " is %d",
					      bg_record->bg_block_id,
					      bg_record->boot_count);
					bg_record->boot_count++;
				} else {
					error("Couldn't boot Partition %s "
					      "for user %s",
					      bg_record->bg_block_id, 
					      bg_record->target_name);
					now = time(NULL);
					time_ptr = localtime(&now);
					strftime(reason, sizeof(reason),
						"update_block_list: "
						"Boot fails "
						"[SLURM@%b %d %H:%M]",
						time_ptr);
					_drain_as_needed(bg_record->nodes,
						reason);
					bg_record->boot_state = 0;
					bg_record->boot_count = 0;
				}
				break;
			case RM_PARTITION_READY:
				debug("partition %s is ready.",
				      bg_record->bg_block_id);
				set_block_user(bg_record); 	
				break;
			default:
				debug("Hey the state of the Partition is %d "
				      "doing nothing.",bg_record->state);
				break;
			}
		}	
		slurm_mutex_unlock(&block_state_mutex);	
	}
	
	if ((rc = rm_free_partition_list(block_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s", bg_err_str(rc));
	}
	
#endif
	return updated;
}
