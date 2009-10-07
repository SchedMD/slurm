/*****************************************************************************\
 *  bg_block_info.c - bluegene block information from the db2 database.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include "src/slurmctld/trigger_mgr.h"
#include "src/slurmctld/locks.h"
#include "bluegene.h"

#define _DEBUG 0
#define RETRY_BOOT_COUNT 3

#ifdef HAVE_BG_FILES

typedef struct {
	int jobid;
} kill_job_struct_t;

List kill_job_list = NULL;

static int _block_is_deallocating(bg_record_t *bg_record);
static void _destroy_kill_struct(void *object);

static int _block_is_deallocating(bg_record_t *bg_record)
{
	int jobid = bg_record->job_running;
	char *user_name = NULL;

	if(bg_record->modifying)
		return SLURM_SUCCESS;
	
	user_name = xstrdup(bg_conf->slurm_user_name);
	if(remove_all_users(bg_record->bg_block_id, NULL) 
	   == REMOVE_USER_ERR) {
		error("Something happened removing "
		      "users from block %s", 
		      bg_record->bg_block_id);
	} 	
	
	if(bg_record->target_name && bg_record->user_name) {
		if(!strcmp(bg_record->target_name, user_name)) {
			if(strcmp(bg_record->target_name, bg_record->user_name)
			   || (jobid > NO_JOB_RUNNING)) {
				kill_job_struct_t *freeit =
					xmalloc(sizeof(freeit));
				freeit->jobid = jobid;
				list_push(kill_job_list, freeit);
				
				error("Block %s was in a ready state "
				      "for user %s but is being freed. "
				      "Job %d was lost.",
				      bg_record->bg_block_id,
				      bg_record->user_name,
				      jobid);
			} else {
				debug("Block %s was in a ready state "
				      "but is being freed. No job running.",
				      bg_record->bg_block_id);
			}
		} else {
			error("State went to free on a boot "
			      "for block %s.",
			      bg_record->bg_block_id);
		}
	} else if(bg_record->user_name) {
		error("Target Name was not set "
		      "not set for block %s.",
		      bg_record->bg_block_id);
		bg_record->target_name = xstrdup(bg_record->user_name);
	} else {
		error("Target Name and User Name are "
		      "not set for block %s.",
		      bg_record->bg_block_id);
		bg_record->user_name = xstrdup(user_name);
		bg_record->target_name = xstrdup(bg_record->user_name);
	}

	if(remove_from_bg_list(bg_lists->job_running, bg_record)
	   == SLURM_SUCCESS) 
		num_unused_cpus += bg_record->cpu_cnt;			       
	remove_from_bg_list(bg_lists->booted, bg_record);

	xfree(user_name);
			
	return SLURM_SUCCESS;
}
static void _destroy_kill_struct(void *object)
{
	kill_job_struct_t *freeit = (kill_job_struct_t *)object;

	if(freeit) {
		xfree(freeit);
	}
}

#endif


/*
 * check to see if block is ready to execute.  Meaning
 * User is added to the list of users able to run, and no one 
 * else is running on the block.
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
		bg_record = find_bg_record_in_list(bg_lists->main, block_id);
		slurm_mutex_lock(&block_state_mutex);
		
		if(bg_record) {
			if(bg_record->job_running != job_ptr->job_id) {
				rc = 0;
			} else if ((bg_record->user_uid == job_ptr->user_id)
				   && (bg_record->state 
				       == RM_PARTITION_READY)) {
				rc = 1;
			} else if (bg_record->user_uid != job_ptr->user_id)
				rc = 0;
			else
				rc = READY_JOB_ERROR;	/* try again */
		} else {
			error("block_ready: block %s not in bg_lists->main.",
			      block_id);
			rc = READY_JOB_FATAL;	/* fatal error */
		}
		slurm_mutex_unlock(&block_state_mutex);
		xfree(block_id);
	} else
		rc = READY_JOB_ERROR;
/* 	info("returning %d for job %u %d %d", */
/* 	     rc, job_ptr->job_id, READY_JOB_ERROR, READY_JOB_FATAL); */
	return rc;
}				

/* Pack all relevent information about a block */
extern void pack_block(bg_record_t *bg_record, Buf buffer)
{
	packstr(bg_record->nodes, buffer);
	packstr(bg_record->ionodes, buffer);
	packstr(bg_record->user_name, buffer);
	packstr(bg_record->bg_block_id, buffer);
	pack16((uint16_t)bg_record->state, buffer);
	pack16((uint16_t)bg_record->conn_type, buffer);
#ifdef HAVE_BGL
	pack16((uint16_t)bg_record->node_use, buffer);	
#endif
	pack32((uint32_t)bg_record->node_cnt, buffer);
	pack32((uint32_t)bg_record->job_running, buffer);
	pack_bit_fmt(bg_record->bitmap, buffer);
	pack_bit_fmt(bg_record->ionode_bitmap, buffer);
#ifdef HAVE_BGL
	packstr(bg_record->blrtsimage, buffer);
#endif
	packstr(bg_record->linuximage, buffer);
	packstr(bg_record->mloaderimage, buffer);
	packstr(bg_record->ramdiskimage, buffer);
}

extern int update_block_list()
{
	int updated = 0;
#ifdef HAVE_BG_FILES
	int rc;
	rm_partition_t *block_ptr = NULL;
#ifdef HAVE_BGL
	rm_partition_mode_t node_use;
#endif
	rm_partition_state_t state;
	char *name = NULL;
	bg_record_t *bg_record = NULL;
	time_t now;
	kill_job_struct_t *freeit = NULL;
	ListIterator itr = NULL;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	
	if(!kill_job_list)
		kill_job_list = list_create(_destroy_kill_struct);

	if(!bg_lists->main) 
		return updated;
	
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(!bg_record->bg_block_id)
			continue;
		name = bg_record->bg_block_id;
		if ((rc = bridge_get_block_info(name, &block_ptr)) 
		    != STATUS_OK) {
			if(bg_conf->layout_mode == LAYOUT_DYNAMIC) {
				switch(rc) {
				case INCONSISTENT_DATA:
					debug2("got inconsistent data when "
					       "quering block %s", name);
					continue;
					break;
				case PARTITION_NOT_FOUND:
					debug("block %s not found, removing "
					      "from slurm", name);
					list_remove(itr);
					destroy_bg_record(bg_record);
					continue;
					break;
				default:
					break;
				}
			}
			error("bridge_get_block_info(%s): %s", 
			      name, 
			      bg_err_str(rc));
			continue;
		}
				
#ifdef HAVE_BGL
		if ((rc = bridge_get_data(block_ptr, RM_PartitionMode,
					  &node_use))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionMode): %s",
			      bg_err_str(rc));
			updated = -1;
			goto next_block;
		} else if(bg_record->node_use != node_use) {
			debug("node_use of Block %s was %d "
			      "and now is %d",
			      bg_record->bg_block_id, 
			      bg_record->node_use, 
			      node_use);
			bg_record->node_use = node_use;
			updated = 1;
		}
#else
		if((bg_record->node_cnt < bg_conf->bp_node_cnt) 
		   || (bg_conf->bp_node_cnt == bg_conf->nodecard_node_cnt)) {
			char *mode = NULL;
			uint16_t conn_type = SELECT_SMALL;
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionOptions,
						  &mode))
			    != STATUS_OK) {
				error("bridge_get_data(RM_PartitionOptions): "
				      "%s", bg_err_str(rc));
				updated = -1;
				goto next_block;
			} else if(mode) {
				switch(mode[0]) {
				case 's':
					conn_type = SELECT_HTC_S;
					break;
				case 'd':
					conn_type = SELECT_HTC_D;
					break;
				case 'v':
					conn_type = SELECT_HTC_V;
					break;
				case 'l':
					conn_type = SELECT_HTC_L;
					break;
				default:
					conn_type = SELECT_SMALL;
					break;
				}
				free(mode);
			}
			
			if(bg_record->conn_type != conn_type) {
				debug("mode of small Block %s was %u "
				      "and now is %u",
				      bg_record->bg_block_id, 
				      bg_record->conn_type, 
				      conn_type);
				bg_record->conn_type = conn_type;
				updated = 1;
			}
		}
#endif		
		if ((rc = bridge_get_data(block_ptr, RM_PartitionState,
					  &state))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionState): %s",
			      bg_err_str(rc));
			updated = -1;
			goto next_block;
		} else if(bg_record->job_running != BLOCK_ERROR_STATE 
			  //plugin set error
			  && bg_record->state != state) {
			int skipped_dealloc = 0;

			debug("state of Block %s was %d and now is %d",
			      bg_record->bg_block_id, 
			      bg_record->state, 
			      state);
			/* 
			   check to make sure block went 
			   through freeing correctly 
			*/
			if((bg_record->state != RM_PARTITION_DEALLOCATING
			    && bg_record->state != RM_PARTITION_ERROR)
			   && state == RM_PARTITION_FREE)
				skipped_dealloc = 1;
			else if((bg_record->state == RM_PARTITION_READY)
				&& (state == RM_PARTITION_CONFIGURING)) {
				/* This means the user did a reboot through
				   mpirun but we missed the state
				   change */
				debug("Block %s skipped rebooting, "
				      "but it really is.  "
				      "Setting target_name back to %s",
				      bg_record->bg_block_id,
				      bg_record->user_name);
				xfree(bg_record->target_name);
				bg_record->target_name =
					xstrdup(bg_record->user_name);
			} else if((bg_record->state 
				   == RM_PARTITION_DEALLOCATING)
				  && (state == RM_PARTITION_CONFIGURING)) 
				/* This is a funky state IBM says
				   isn't a bug, but all their
				   documentation says this doesn't
				   happen, but IBM says oh yeah, you
				   weren't really suppose to notice
				   that. So we will just skip this
				   state and act like this didn't happen. */
				goto nochange_state;
			
			bg_record->state = state;

			if(bg_record->state == RM_PARTITION_DEALLOCATING
			   || skipped_dealloc) 
				_block_is_deallocating(bg_record);
#ifndef HAVE_BGL
			else if(bg_record->state == RM_PARTITION_REBOOTING) {
				/* This means the user did a reboot through
				   mpirun */
				debug("Block %s rebooting.  "
				      "Setting target_name back to %s",
				      bg_record->bg_block_id,
				      bg_record->user_name);
				xfree(bg_record->target_name);
				bg_record->target_name =
					xstrdup(bg_record->user_name);
			}
#endif
			else if(bg_record->state == RM_PARTITION_CONFIGURING)
				bg_record->boot_state = 1;
			else if(bg_record->state == RM_PARTITION_FREE) {
				if(remove_from_bg_list(bg_lists->job_running, 
						       bg_record) 
				   == SLURM_SUCCESS) {
					num_unused_cpus += bg_record->cpu_cnt;
				}
				remove_from_bg_list(bg_lists->booted,
						    bg_record);
			} else if(bg_record->state == RM_PARTITION_ERROR) {
				if(bg_record->boot_state == 1)
					error("Block %s in an error "
					      "state while booting.",
					      bg_record->bg_block_id);
				else					
					error("Block %s in an error state.",
					      bg_record->bg_block_id);
				remove_from_bg_list(bg_lists->booted,
						    bg_record);
				trigger_block_error();
			}
			updated = 1;
		}
	nochange_state:

		/* check the boot state */
		debug3("boot state for block %s is %d",
		       bg_record->bg_block_id,
		       bg_record->boot_state);
		if(bg_record->boot_state == 1) {
			switch(bg_record->state) {
			case RM_PARTITION_CONFIGURING:
				debug3("checking to make sure user %s "
				       "is the user.",
				       bg_record->target_name);
				
				if(update_block_user(bg_record, 0) == 1)
					last_bg_update = time(NULL);
				
				break;
			case RM_PARTITION_ERROR:
				/* If we get an error on boot that
				 * means it is a transparent L3 error
				 * and should be trying to fix
				 * itself.  If this is the case we
				 * just hang out waiting for the state
				 * to go to free where we will try to
				 * boot again below.
				 */
				break;
			case RM_PARTITION_FREE:
				if(bg_record->boot_count < RETRY_BOOT_COUNT) {
					slurm_mutex_unlock(&block_state_mutex);
					if((rc = boot_block(bg_record))
					   != SLURM_SUCCESS) {
						updated = -1;
					}
					slurm_mutex_lock(&block_state_mutex);
					debug("boot count for block "
					      "%s is %d",
					      bg_record->bg_block_id,
					      bg_record->boot_count);
					bg_record->boot_count++;
				} else {
					char reason[128], time_str[32];

					error("Couldn't boot Block %s "
					      "for user %s",
					      bg_record->bg_block_id, 
					      bg_record->target_name);
					slurm_mutex_unlock(&block_state_mutex);
					
					now = time(NULL);
					slurm_make_time_str(&now, time_str,
							    sizeof(time_str));
					snprintf(reason, 
						 sizeof(reason),
						 "update_block_list: "
						 "Boot fails "
						 "[SLURM@%s]", 
						 time_str);
					drain_as_needed(bg_record, reason);
					slurm_mutex_lock(&block_state_mutex);
					bg_record->boot_state = 0;
					bg_record->boot_count = 0;
					if(remove_from_bg_list(
						   bg_lists->job_running, 
						   bg_record) 
					   == SLURM_SUCCESS) {
						num_unused_cpus += 
							bg_record->cpu_cnt;
					} 
					remove_from_bg_list(
						bg_lists->booted,
						bg_record);
				}
				break;
			case RM_PARTITION_READY:
				debug("block %s is ready.",
				      bg_record->bg_block_id);
				if(set_block_user(bg_record) == SLURM_ERROR) {
					freeit = xmalloc(
						sizeof(kill_job_struct_t));
					freeit->jobid = bg_record->job_running;
					list_push(kill_job_list, freeit);
				}
				break;
			case RM_PARTITION_DEALLOCATING:
				debug2("Block %s is in a deallocating state "
				       "during a boot.  Doing nothing until "
				       "free state.",
				       bg_record->bg_block_id);
				break;
#ifndef HAVE_BGL
			case RM_PARTITION_REBOOTING:
				debug2("Block %s is rebooting.",
				       bg_record->bg_block_id);
				break;
#endif
			default:
				debug("Hey the state of block "
				      "%s is %d(%s) doing nothing.",
				      bg_record->bg_block_id,
				      bg_record->state,
				      bg_block_state_string(bg_record->state));
				break;
			}
		}
	next_block:
		if ((rc = bridge_free_block(block_ptr)) 
		    != STATUS_OK) {
			error("bridge_free_block(): %s", 
			      bg_err_str(rc));
		}				
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);
	
	/* kill all the jobs from unexpectedly freed blocks */
	while((freeit = list_pop(kill_job_list))) {
		debug2("Trying to requeue job %d", freeit->jobid);
		lock_slurmctld(job_write_lock);
		if((rc = job_requeue(0, freeit->jobid, -1))) {
			error("couldn't requeue job %u, failing it: %s",
			      freeit->jobid, 
			      slurm_strerror(rc));
			(void) job_fail(freeit->jobid);
		}
		unlock_slurmctld(job_write_lock);
		_destroy_kill_struct(freeit);
	}
		
#endif
	return updated;
}

extern int update_freeing_block_list()
{
	int updated = 0;
#ifdef HAVE_BG_FILES
	int rc;
	rm_partition_t *block_ptr = NULL;
	rm_partition_state_t state;
	char *name = NULL;
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;
	
	if(!bg_lists->freeing) 
		return updated;
	
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->freeing);
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(!bg_record->bg_block_id)
			continue;

		name = bg_record->bg_block_id;
		if ((rc = bridge_get_block_info(name, &block_ptr)) 
		    != STATUS_OK) {
			if(bg_conf->layout_mode == LAYOUT_DYNAMIC) {
				switch(rc) {
				case INCONSISTENT_DATA:
					debug2("got inconsistent data when "
					       "quering block %s", name);
					continue;
					break;
				case PARTITION_NOT_FOUND:
					debug("block %s not found, removing "
					      "from slurm", name);
					list_remove(itr);
					destroy_bg_record(bg_record);
					continue;
					break;
				default:
					break;
				}
			}
			error("bridge_get_block_info(%s): %s", 
			      name, 
			      bg_err_str(rc));
			continue;
		}
				
		if ((rc = bridge_get_data(block_ptr, RM_PartitionState,
					  &state))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionState): %s",
			      bg_err_str(rc));
			updated = -1;
			goto next_block;
		} else if(bg_record->state != state) {
			debug("freeing state of Block %s was %d and now is %d",
			      bg_record->bg_block_id, 
			      bg_record->state, 
			      state);

			bg_record->state = state;
			updated = 1;
		}
	next_block:
		if ((rc = bridge_free_block(block_ptr)) 
		    != STATUS_OK) {
			error("bridge_free_block(): %s", 
			      bg_err_str(rc));
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);
		
#endif
	return updated;
}
