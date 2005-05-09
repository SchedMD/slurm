/*****************************************************************************\
 *  bgl_part_info.c - blue gene partition information from the db2 database.
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
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "bluegene.h"

#define _DEBUG 0
#define RETRY_BOOT_COUNT 3
/*
 * check to see if partition is ready to execute.  Meaning
 * User is added to the list of users able to run, and no one 
 * else is running on the partition.
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BGL block is available for use.
 */
extern int part_ready(struct job_record *job_ptr)
{
	int rc = 1;
#ifdef HAVE_BGL_FILES
	char *part_id = NULL;
	bgl_record_t *bgl_record = NULL;
	
	rc = select_g_get_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_PART_ID, &part_id);
	if (rc == SLURM_SUCCESS) {
		bgl_record = find_bgl_record(part_id);
		
		if(bgl_record) {
			if (rc != -1) {
				if((bgl_record->user_uid == job_ptr->user_id)
				   && (bgl_record->state 
				       == RM_PARTITION_READY)) {
					rc = 1;
				}
				else if (bgl_record->user_uid 
					 != job_ptr->user_id)
					rc = 0;
				else
					rc = -1;
			} 
		} else {
			error("part_ready: partition %s not in bgl_list.",
			      part_id);
			rc = -1;
		}
		xfree(part_id);
	} else
		rc = -1;
#endif
	return rc;
}				

/* Pack all relevent information about a partition */
extern void pack_partition(bgl_record_t *bgl_record, Buf buffer)
{
	packstr(bgl_record->nodes, buffer);
	packstr(bgl_record->user_name, buffer);
	packstr(bgl_record->bgl_part_id, buffer);
	pack16((uint16_t)bgl_record->state, buffer);
	pack16((uint16_t)bgl_record->conn_type, buffer);
	pack16((uint16_t)bgl_record->node_use, buffer);	
}

extern int update_partition_list()
{
	int updated = 0;
#ifdef HAVE_BGL_FILES
	int j, rc, num_parts = 0;
	rm_partition_t *part_ptr = NULL;
	rm_partition_mode_t node_use;
	rm_partition_state_t state;
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	char *name = NULL;
	rm_partition_list_t *part_list = NULL;
	bgl_record_t *bgl_record = NULL;
	//struct passwd *pw_ent = NULL;
	time_t now;
	struct tm *time_ptr;
	char reason[128];
	
	if(bgl_list == NULL && !last_bgl_update)
		return 0;
	
	if ((rc = rm_get_partitions_info(part_state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions_info(): %s", bgl_err_str(rc));
		return -1; 
	}

	if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
		   != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", bgl_err_str(rc));
		updated = -1;
		num_parts = 0;
	}
			
	for (j=0; j<num_parts; j++) {
		if (j) {
			if ((rc = rm_get_data(part_list, RM_PartListNextPart, 
					      &part_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
				      bgl_err_str(rc));
				updated = -1;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list, RM_PartListFirstPart, 
					      &part_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s",
				      bgl_err_str(rc));
				updated = -1;
				break;
			}
		}
		if ((rc = rm_get_data(part_ptr, RM_PartitionID, &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s", 
			      bgl_err_str(rc));
			updated = -1;
			break;
		}
		if(strncmp("RMP", name,3))
			continue;
		
		bgl_record = find_bgl_record(name);
		
		if(bgl_record == NULL) {
			error("Partition %s not found on bgl_list", name);
			continue;
		}
		
		slurm_mutex_lock(&part_state_mutex);
		
		if ((rc = rm_get_data(part_ptr, RM_PartitionMode, &node_use))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionMode): %s",
			      bgl_err_str(rc));
			updated = -1;
			slurm_mutex_unlock(&part_state_mutex);
			break;
		} else if(bgl_record->node_use != node_use) {
			debug("node_use of Partition %s was %d and now is %d",
			      name, bgl_record->node_use, node_use);
			bgl_record->node_use = node_use;
			updated = 1;
		}
		
		if ((rc = rm_get_data(part_ptr, RM_PartitionState, &state))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionState): %s",
			      bgl_err_str(rc));
			updated = -1;
			slurm_mutex_unlock(&part_state_mutex);
			break;
		} else if(bgl_record->state != state) {
			debug("state of Partition %s was %d and now is %d",
			      name, bgl_record->state, state);
			bgl_record->state = state;
			if(bgl_record->state == RM_PARTITION_FREE) {
				if((rc = remove_all_users(
					    bgl_record->bgl_part_id, 
					    NULL))
				   == REMOVE_USER_ERR) {
					error("Something happened removing "
					      "users from partition %s", 
					      bgl_record->bgl_part_id);
				} 
				if(!strcmp(bgl_record->target_name, USER_NAME) 
				   && strcmp(bgl_record->target_name, 
					     bgl_record->user_name)) {
					info("partition %s was in a "
					     "ready state but got freed "
					     "booting again for user %s",
					     bgl_record->bgl_part_id,
					     bgl_record->user_name);
					xfree(bgl_record->target_name);	
					bgl_record->target_name = 
						xstrdup(bgl_record->user_name);
				}
			} else if(bgl_record->state 
				  == RM_PARTITION_CONFIGURING)
				bgl_record->boot_state = 1;
			updated = 1;
		}

		/* check the boot state */
		/* debug("boot state for partition %s is %d", */
/* 		      bgl_record->bgl_part_id, */
/* 		      bgl_record->boot_state); */
		if(bgl_record->boot_state == 1) {
			switch(bgl_record->state) {
			case RM_PARTITION_CONFIGURING:
				debug("checking to make sure user %s "
				      "is the user.", 
				      bgl_record->target_name);
				if(update_partition_user(bgl_record) 
				   == 1) 
					last_bgl_update = time(NULL);
			
				break;
			case RM_PARTITION_ERROR:
				error("partition in an error state");
			case RM_PARTITION_FREE:
				if(bgl_record->boot_count < RETRY_BOOT_COUNT) {
					slurm_mutex_unlock(&part_state_mutex);
					if((rc = boot_part(bgl_record,
							   bgl_record->
							   node_use))
					   != SLURM_SUCCESS) {
						updated = -1;
					}
					slurm_mutex_lock(&part_state_mutex);
					debug("boot count for partition %s "
					      " is %d",
					      bgl_record->bgl_part_id,
					      bgl_record->boot_count);
					bgl_record->boot_count++;
				} else {
					error("Couldn't boot Partition %s "
					      "for user %s",
					      bgl_record->bgl_part_id, 
					      bgl_record->target_name);
					now = time(NULL);
					time_ptr = localtime(&now);
					strftime(reason, sizeof(reason),
						 "update_partition_list: "
						 "Boot fails "
						 "[SLURM@%b %d %H:%M]",
						 time_ptr);
					slurm_drain_nodes(bgl_record->nodes, 
							  reason);
					bgl_record->boot_state = 0;
					bgl_record->boot_count = 0;
				}
				break;
			default:
				set_part_user(bgl_record); 	
				break;
			}
		}	
		slurm_mutex_unlock(&part_state_mutex);	
	}
	
	if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s", bgl_err_str(rc));
	}
#endif
	return updated;
}
