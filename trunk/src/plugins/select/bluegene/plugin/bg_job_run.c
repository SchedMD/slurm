/*****************************************************************************\
 *  bg_job_run.c - blue gene job execution (e.g. initiation and termination) 
 *  functions.
 *
 *  $Id$ 
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "bluegene.h"
#include "src/slurmctld/locks.h"

#define MAX_POLL_RETRIES    220
#define POLL_INTERVAL        3

bool deleting_old_blocks_flag = 0;

enum update_op {START_OP, TERM_OP, SYNC_OP};

typedef struct bg_update {
	enum update_op op;	/* start | terminate | sync */
	struct job_record *job_ptr;	/* pointer to job running on
					 * block or NULL if no job */
	uint16_t reboot;	/* reboot block before starting job */
#ifndef HAVE_BGL
	uint16_t conn_type;     /* needed to boot small blocks into
				   HTC mode or not */
#endif
	pm_partition_id_t bg_block_id;
	char *blrtsimage;       /* BlrtsImage for this block */
	char *linuximage;       /* LinuxImage for this block */
	char *mloaderimage;     /* mloaderImage for this block */
	char *ramdiskimage;     /* RamDiskImage for this block */
} bg_update_t;

static List bg_update_list = NULL;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t job_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t agent_cond = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;

#ifdef HAVE_BG_FILES
static int	_remove_job(db_job_id_t job_id);
#endif

static void	_bg_list_del(void *x);
static int	_excise_block(List block_list, 
			      pm_partition_id_t bg_block_id, 
			      char *nodes);
static List	_get_all_blocks(void);
static void *	_block_agent(void *args);
static void	_block_op(bg_update_t *bg_update_ptr);
static void	_start_agent(bg_update_t *bg_update_ptr);
static void	_sync_agent(bg_update_t *bg_update_ptr);
static void	_term_agent(bg_update_t *bg_update_ptr);


#ifdef HAVE_BG_FILES
/* Kill a job and remove its record from MMCS */
static int _remove_job(db_job_id_t job_id)
{
	int i, rc;
	rm_job_t *job_rec = NULL;
	rm_job_state_t job_state;

	debug("removing job %d from MMCS", job_id);
	while(1) {
		if (i > 0)
			sleep(POLL_INTERVAL);
		
		/* Find the job */
		if ((rc = bridge_get_job(job_id, &job_rec)) != STATUS_OK) {
			
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 

			error("bridge_get_job(%d): %s", job_id, 
			      bg_err_str(rc));
			continue;
		}
		
			
		if ((rc = bridge_get_data(job_rec, RM_JobState, &job_state)) 
		    != STATUS_OK) {
			(void) bridge_free_job(job_rec);
			if (rc == JOB_NOT_FOUND) {
				debug("job %d not found in MMCS", job_id);
				return STATUS_OK;
			} 

			error("bridge_get_data(RM_JobState) for jobid=%d "
			      "%s", job_id, bg_err_str(rc));
			continue;
		}
		if ((rc = bridge_free_job(job_rec)) != STATUS_OK)
			error("bridge_free_job: %s", bg_err_str(rc));

		debug2("job %d is in state %d", job_id, job_state);
		
		/* check the state and process accordingly */
		if(job_state == RM_JOB_TERMINATED)
			return STATUS_OK;
		else if(job_state == RM_JOB_DYING) {
			/* start sending sigkills for the last 5 tries */
			if(i > MAX_POLL_RETRIES) 
				error("Job %d isn't dying, trying for "
				      "%d seconds", i*POLL_INTERVAL);
			continue;
		} else if(job_state == RM_JOB_ERROR) {
			error("job %d is in a error state.", job_id);
			
			//free_bg_block();
			return STATUS_OK;
		}

		rc = bridge_cancel_job(job_id);

		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 
			if(rc == INCOMPATIBLE_STATE)
				debug("job %d is in an INCOMPATIBLE_STATE",
				      job_id);
			else
				error("bridge_cancel_job(%d): %s", job_id, 
				      bg_err_str(rc));
		}
	}

	error("Failed to remove job %d from MMCS", job_id);
	return INTERNAL_ERROR;
}
#endif

/* block_state_mutex should be locked before calling this function */
static int _reset_block(bg_record_t *bg_record) 
{
	int rc = SLURM_SUCCESS;
	if(bg_record) {
		if(bg_record->job_running > NO_JOB_RUNNING) {
			bg_record->job_running = NO_JOB_RUNNING;
			bg_record->job_ptr = NULL;
		}
		/* remove user from list */		
		
		if(bg_record->target_name) {
			if(strcmp(bg_record->target_name, bg_slurm_user_name)) {
				xfree(bg_record->target_name);
				bg_record->target_name = 
					xstrdup(bg_slurm_user_name);
			}
			update_block_user(bg_record, 1);
		} else {
			bg_record->target_name = xstrdup(bg_slurm_user_name);
		}	
		
			
		bg_record->boot_state = 0;
		bg_record->boot_count = 0;
		
		last_bg_update = time(NULL);
		if(remove_from_bg_list(bg_job_block_list, bg_record) 
		   == SLURM_SUCCESS) {
			num_unused_cpus += bg_record->cpu_cnt;
		}
	} else {
		error("No block given to reset");
		rc = SLURM_ERROR;
	}

	return rc;
}

/* Delete a bg_update_t record */
static void _bg_list_del(void *x)
{
	bg_update_t *bg_update_ptr = (bg_update_t *) x;

	if (bg_update_ptr) {
		xfree(bg_update_ptr->blrtsimage);
		xfree(bg_update_ptr->linuximage);
		xfree(bg_update_ptr->mloaderimage);
		xfree(bg_update_ptr->ramdiskimage);
		xfree(bg_update_ptr->bg_block_id);
		xfree(bg_update_ptr);
	}
}

/* Update block user and reboot as needed */
static void _sync_agent(bg_update_t *bg_update_ptr)
{
	bg_record_t * bg_record = NULL;
	
	bg_record = find_bg_record_in_list(bg_list, bg_update_ptr->bg_block_id);
	if(!bg_record) {
		error("No block %s", bg_update_ptr->bg_block_id);
		return;
	}
	slurm_mutex_lock(&block_state_mutex);

	bg_record->job_running = bg_update_ptr->job_ptr->job_id;
	bg_record->job_ptr = bg_update_ptr->job_ptr;

	if(!block_ptr_exist_in_list(bg_job_block_list, bg_record)) {
		list_push(bg_job_block_list, bg_record);
		num_unused_cpus -= bg_record->cpu_cnt;
	}
	if(!block_ptr_exist_in_list(bg_booted_block_list, bg_record)) 
		list_push(bg_booted_block_list, bg_record);
	slurm_mutex_unlock(&block_state_mutex);

	if(bg_record->state == RM_PARTITION_READY) {
		if(bg_record->user_uid != bg_update_ptr->job_ptr->user_id) {
			int set_user_rc = SLURM_SUCCESS;

			slurm_mutex_lock(&block_state_mutex);
			debug("User isn't correct for job %d on %s, "
			      "fixing...", 
			      bg_update_ptr->job_ptr->job_id,
			      bg_update_ptr->bg_block_id);
			xfree(bg_record->target_name);
			bg_record->target_name = 
				uid_to_string(bg_update_ptr->job_ptr->user_id);
			set_user_rc = set_block_user(bg_record);
			slurm_mutex_unlock(&block_state_mutex);
		
			if(set_user_rc == SLURM_ERROR) 
				(void) slurm_fail_job(bg_record->job_running);
		}
	} else {
		if(bg_record->state != RM_PARTITION_CONFIGURING) {
			error("Block %s isn't ready and isn't "
			      "being configured! Starting job again.",
			      bg_update_ptr->bg_block_id);
		} else {
			debug("Block %s is booting, job ok",
			      bg_update_ptr->bg_block_id);
		}
		_start_agent(bg_update_ptr);
	}
}

/* Perform job initiation work */
static void _start_agent(bg_update_t *bg_update_ptr)
{
	int rc, set_user_rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;
	bg_record_t *found_record = NULL;
	ListIterator itr;
	List delete_list;
	int requeue_job = 0;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	slurm_mutex_lock(&job_start_mutex);
		
	bg_record = find_bg_record_in_list(bg_list, bg_update_ptr->bg_block_id);

	if(!bg_record) {
		error("block %s not found in bg_list",
		      bg_update_ptr->bg_block_id);
		/* wait for the slurmd to begin 
		   the batch script, slurm_fail_job() 
		   is a no-op if issued prior 
		   to the script initiation do clean up just
		   incase the fail job isn't ran */
		sleep(2);	
		lock_slurmctld(job_write_lock);
		if((rc = job_requeue(0, bg_update_ptr->job_ptr->job_id, -1))) {
			error("couldn't requeue job %u, failing it: %s",
			      bg_update_ptr->job_ptr->job_id, 
			      slurm_strerror(rc));
			job_fail(bg_update_ptr->job_ptr->job_id);
		}
		unlock_slurmctld(job_write_lock);
		slurm_mutex_unlock(&job_start_mutex);
		return;
	}
	slurm_mutex_lock(&block_state_mutex);
	if(bg_record->job_running <= NO_JOB_RUNNING) {
		// _reset_block(bg_record); should already happened
		slurm_mutex_unlock(&block_state_mutex);
		slurm_mutex_unlock(&job_start_mutex);
		debug("job %u finished during the queueing job "
		      "(everything is ok)",
		      bg_update_ptr->job_ptr->job_id);
		return;
	}
	if(bg_record->state == RM_PARTITION_DEALLOCATING) {
		slurm_mutex_unlock(&block_state_mutex);
		debug("Block is in Deallocating state, waiting for free.");
		bg_free_block(bg_record);
		/* no reason to reboot here since we are already
		   deallocating */
		bg_update_ptr->reboot = 0;
	} else 
		slurm_mutex_unlock(&block_state_mutex);

	
	delete_list = list_create(NULL);
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_list);
	while ((found_record = list_next(itr))) {
		if ((!found_record) || (bg_record == found_record))
			continue;
		
		if(!blocks_overlap(bg_record, found_record)) {
			debug2("block %s isn't part of %s",
			       found_record->bg_block_id, 
			       bg_record->bg_block_id);
			continue;
		}

		if(found_record->job_ptr) {
			error("Trying to start job %u on block %s, "
			      "but there is a job %u running on an overlapping "
			      "block %s it will not end until %u.  "
			      "This should never happen.",
			      bg_update_ptr->job_ptr->job_id,
			      bg_record->bg_block_id,
			      found_record->job_ptr->job_id,
			      found_record->bg_block_id,
			      found_record->job_ptr->end_time);
			requeue_job = 1;
			break;
		}

		debug2("need to make sure %s is free, it's part of %s",
		       found_record->bg_block_id, 
		       bg_record->bg_block_id);
		list_push(delete_list, found_record);
		if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
			list_remove(itr);
		}
		num_block_to_free++;
	}		
	list_iterator_destroy(itr);

	if(requeue_job) {
		num_block_to_free = 0;
		num_block_freed = 0;
		list_destroy(delete_list);

		_reset_block(bg_record);

		slurm_mutex_unlock(&block_state_mutex);
		/* wait for the slurmd to begin 
		   the batch script, slurm_fail_job() 
		   is a no-op if issued prior 
		   to the script initiation do clean up just
		   incase the fail job isn't ran */
		sleep(2);	
		lock_slurmctld(job_write_lock);
		if((rc = job_requeue(0, bg_update_ptr->job_ptr->job_id, -1))) {
			error("couldn't requeue job %u, failing it: %s",
			      bg_update_ptr->job_ptr->job_id, 
			      slurm_strerror(rc));
			job_fail(bg_update_ptr->job_ptr->job_id);
		}
		unlock_slurmctld(job_write_lock);
		slurm_mutex_unlock(&job_start_mutex);
		return;
	}	

	free_block_list(delete_list);
	list_destroy(delete_list);
	slurm_mutex_unlock(&block_state_mutex);
	
	/* wait for all necessary blocks to be freed */
	while(num_block_to_free > num_block_freed) {
		sleep(1);
		debug("got %d of %d freed",
		      num_block_freed, 
		      num_block_to_free);
	}
	/* Zero out the values here because we are done with them and
	   they will be ready for the next job */
	num_block_to_free = 0;
	num_block_freed = 0;
	
	slurm_mutex_lock(&block_state_mutex);
	if(bg_record->job_running <= NO_JOB_RUNNING) {
		// _reset_block(bg_record); should already happened
		slurm_mutex_unlock(&block_state_mutex);
		slurm_mutex_unlock(&job_start_mutex);
		debug("job %u already finished before boot",
		      bg_update_ptr->job_ptr->job_id);
		return;
	}

	rc = 0;
#ifdef HAVE_BGL
	if(bg_update_ptr->blrtsimage 
	   && strcasecmp(bg_update_ptr->blrtsimage, bg_record->blrtsimage)) {
		debug3("changing BlrtsImage from %s to %s",
		       bg_record->blrtsimage, bg_update_ptr->blrtsimage);
		xfree(bg_record->blrtsimage);
		bg_record->blrtsimage = xstrdup(bg_update_ptr->blrtsimage);
		rc = 1;
	}
#else 
	if((bg_update_ptr->conn_type >= SELECT_SMALL) 
		&& (bg_update_ptr->conn_type != bg_record->conn_type)) {
		debug3("changing small block mode from %u to %u",
		       bg_record->conn_type, bg_update_ptr->conn_type);
		rc = 1;
	}
#endif
	if(bg_update_ptr->linuximage
	   && strcasecmp(bg_update_ptr->linuximage, bg_record->linuximage)) {
#ifdef HAVE_BGL
		debug3("changing LinuxImage from %s to %s",
		       bg_record->linuximage, bg_update_ptr->linuximage);
#else
		debug3("changing CnloadImage from %s to %s",
		       bg_record->linuximage, bg_update_ptr->linuximage);
#endif
		xfree(bg_record->linuximage);
		bg_record->linuximage = xstrdup(bg_update_ptr->linuximage);
		rc = 1;
	}
	if(bg_update_ptr->mloaderimage
	   && strcasecmp(bg_update_ptr->mloaderimage,
			 bg_record->mloaderimage)) {
		debug3("changing MloaderImage from %s to %s",
		       bg_record->mloaderimage, bg_update_ptr->mloaderimage);
		xfree(bg_record->mloaderimage);
		bg_record->mloaderimage = xstrdup(bg_update_ptr->mloaderimage);
		rc = 1;
	}
	if(bg_update_ptr->ramdiskimage
	   && strcasecmp(bg_update_ptr->ramdiskimage,
			 bg_record->ramdiskimage)) {
#ifdef HAVE_BGL
		debug3("changing RamDiskImage from %s to %s",
		       bg_record->ramdiskimage, bg_update_ptr->ramdiskimage);
#else
		debug3("changing IoloadImage from %s to %s",
		       bg_record->ramdiskimage, bg_update_ptr->ramdiskimage);
#endif
		xfree(bg_record->ramdiskimage);
		bg_record->ramdiskimage = xstrdup(bg_update_ptr->ramdiskimage);
		rc = 1;
	}
	slurm_mutex_unlock(&block_state_mutex);

	if(rc) {
		slurm_mutex_lock(&block_state_mutex);
		bg_record->modifying = 1;
		slurm_mutex_unlock(&block_state_mutex);
			
		bg_free_block(bg_record);
#ifdef HAVE_BG_FILES
#ifdef HAVE_BGL
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_BlrtsImg,   
					      bg_record->blrtsimage))
		    != STATUS_OK)
			error("bridge_modify_block(RM_MODIFY_BlrtsImg)",
			      bg_err_str(rc));
		
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_LinuxImg,   
					      bg_record->linuximage))
		    != STATUS_OK) 
			error("bridge_modify_block(RM_MODIFY_LinuxImg)",
			      bg_err_str(rc));
		
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_RamdiskImg, 
					      bg_record->ramdiskimage))
		    != STATUS_OK)
			error("bridge_modify_block(RM_MODIFY_RamdiskImg)", 
			      bg_err_str(rc));

#else
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_CnloadImg,   
					      bg_record->linuximage))
		    != STATUS_OK) 
			error("bridge_modify_block(RM_MODIFY_CnloadImg)",
			      bg_err_str(rc));
		
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_IoloadImg, 
					      bg_record->ramdiskimage))
		    != STATUS_OK)
			error("bridge_modify_block(RM_MODIFY_IoloadImg)", 
			      bg_err_str(rc));

		if(bg_update_ptr->conn_type > SELECT_SMALL) {
			char *conn_type = NULL;
			switch(bg_update_ptr->conn_type) {
			case SELECT_HTC_S:
				conn_type = "s";
				break;
			case SELECT_HTC_D:
				conn_type = "d";
				break;
			case SELECT_HTC_V:
				conn_type = "v";
				break;
			case SELECT_HTC_L:
				conn_type = "l";
				break;
			default:
				break;
			}
			/* the option has to be set before the pool can be
			   set */
			if ((rc = bridge_modify_block(
				     bg_record->bg_block_id,
				     RM_MODIFY_Options,
				     conn_type)) != STATUS_OK)
				error("bridge_set_data(RM_MODIFY_Options)",
				      bg_err_str(rc));
		}
#endif
		if ((rc = bridge_modify_block(bg_record->bg_block_id,
					      RM_MODIFY_MloaderImg, 
					      bg_record->mloaderimage))
		    != STATUS_OK)
			error("bridge_modify_block(RM_MODIFY_MloaderImg)", 
			      bg_err_str(rc));
		
#endif
		slurm_mutex_lock(&block_state_mutex);
		bg_record->modifying = 0;		
		slurm_mutex_unlock(&block_state_mutex);		
	} else if(bg_update_ptr->reboot) {
		slurm_mutex_lock(&block_state_mutex);
		bg_record->modifying = 1;
		slurm_mutex_unlock(&block_state_mutex);

		bg_free_block(bg_record);

		slurm_mutex_lock(&block_state_mutex);
		bg_record->modifying = 0;		
		slurm_mutex_unlock(&block_state_mutex);		
	}

	if(bg_record->state == RM_PARTITION_FREE) {
		if((rc = boot_block(bg_record)) != SLURM_SUCCESS) {
			slurm_mutex_lock(&block_state_mutex);
			_reset_block(bg_record);
			slurm_mutex_unlock(&block_state_mutex);
			sleep(2);	
			/* wait for the slurmd to begin 
			   the batch script, slurm_fail_job() 
			   is a no-op if issued prior 
			   to the script initiation do clean up just
			   incase the fail job isn't ran */
			lock_slurmctld(job_write_lock);
			if((rc = job_requeue(
				    0, bg_update_ptr->job_ptr->job_id, -1))) {
				error("couldn't requeue job %u, failing it: %s",
				      bg_update_ptr->job_ptr->job_id, 
				      slurm_strerror(rc));
				job_fail(bg_update_ptr->job_ptr->job_id);
			}
			unlock_slurmctld(job_write_lock);

			slurm_mutex_unlock(&job_start_mutex);
			return;
		}
	} else if (bg_record->state == RM_PARTITION_CONFIGURING) {
		bg_record->boot_state = 1;		
	}
	
	if(bg_record->job_running <= NO_JOB_RUNNING) {
		slurm_mutex_unlock(&job_start_mutex);
		debug("job %u finished during the start of the boot "
		      "(everything is ok)",
		      bg_update_ptr->job_ptr->job_id);
		return;
	}
	slurm_mutex_lock(&block_state_mutex);
		
	bg_record->boot_count = 0;
	xfree(bg_record->target_name);
	bg_record->target_name = 
		uid_to_string(bg_update_ptr->job_ptr->user_id);
	debug("setting the target_name for Block %s to %s",
	      bg_record->bg_block_id,
	      bg_record->target_name);
	
	if(bg_record->state == RM_PARTITION_READY) {
		debug("block %s is ready.",
		      bg_record->bg_block_id);
				
		set_user_rc = set_block_user(bg_record); 
	}
	slurm_mutex_unlock(&block_state_mutex);	

	if(set_user_rc == SLURM_ERROR) {
		sleep(2);	
		/* wait for the slurmd to begin 
		   the batch script, slurm_fail_job() 
		   is a no-op if issued prior 
		   to the script initiation do clean up just
		   incase the fail job isn't ran */
		(void) slurm_fail_job(bg_record->job_running);
		slurm_mutex_lock(&block_state_mutex);
		if (remove_from_bg_list(bg_job_block_list, bg_record)
		    == SLURM_SUCCESS) {
			num_unused_cpus += bg_record->cpu_cnt;
		}
		slurm_mutex_unlock(&block_state_mutex);
	}
	slurm_mutex_unlock(&job_start_mutex);
}

/* Perform job termination work */
static void _term_agent(bg_update_t *bg_update_ptr)
{
	bg_record_t *bg_record = NULL;
	time_t now;
	char reason[128];
	int job_remove_failed = 0;
	
#ifdef HAVE_BG_FILES
	rm_element_t *job_elem = NULL;
	rm_job_list_t *job_list = NULL;
	db_job_id_t job_id;
	int live_states;
	pm_partition_id_t block_id;
	int i, jobs, rc;
	
	debug2("getting the job info");
	live_states = JOB_ALL_FLAG 
		& (~JOB_TERMINATED_FLAG) 
		& (~JOB_KILLED_FLAG)
		& (~JOB_ERROR_FLAG);
	
	if ((rc = bridge_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("bridge_get_jobs(): %s", bg_err_str(rc));
		
		return;
	}
	
			
	if ((rc = bridge_get_data(job_list, RM_JobListSize, &jobs)) != STATUS_OK) {
		error("bridge_get_data(RM_JobListSize): %s", bg_err_str(rc));
		jobs = 0;
	}
	debug2("job count %d",jobs);

	for (i=0; i<jobs; i++) {		
		if (i) {
			if ((rc = bridge_get_data(job_list, RM_JobListNextJob, 
						  &job_elem)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_JobListNextJob): %s", 
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(job_list, RM_JobListFirstJob,
						  &job_elem)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_JobListFirstJob): %s",
				      bg_err_str(rc));
				continue;
			}
		}
		
		if(!job_elem) {
			error("No Job Elem breaking out job count = %d\n", 
			      jobs);
			break;
		}
		if ((rc = bridge_get_data(job_elem, RM_JobPartitionID, 
					  &block_id))
		    != STATUS_OK) {
			error("bridge_get_data(RM_JobPartitionID) %s: %s", 
			      block_id, bg_err_str(rc));
			continue;
		}

		if(!block_id) {
			error("No blockID returned from Database");
			continue;
		}

		debug2("looking at block %s looking for %s\n",
		       block_id, bg_update_ptr->bg_block_id);
			
		if (strcmp(block_id, bg_update_ptr->bg_block_id) != 0) {
			free(block_id);
			continue;
		}
		
		free(block_id);

		if ((rc = bridge_get_data(job_elem, RM_JobDBJobID, &job_id))
		    != STATUS_OK) {
			error("bridge_get_data(RM_JobDBJobID): %s", 
			      bg_err_str(rc));
			continue;
		}
		debug2("got job_id %d",job_id);
		if((rc = _remove_job(job_id)) == INTERNAL_ERROR) {
			job_remove_failed = 1;
			break;
		}
	}
#endif
	
	/* remove the block's users */
	bg_record = find_bg_record_in_list(bg_list, bg_update_ptr->bg_block_id);
	if(bg_record) {
		debug("got the record %s user is %s",
		      bg_record->bg_block_id,
		      bg_record->user_name);

		if(job_remove_failed) {
			char time_str[32];
			slurm_make_time_str(&now, time_str, sizeof(time_str));
			snprintf(reason, sizeof(reason),
				 "_term_agent: Couldn't remove job "
				 "[SLURM@%s]", time_str);
			if(bg_record->nodes)
				slurm_drain_nodes(bg_record->nodes, 
						  reason);
			else
				error("Block %s doesn't have a node list.",
				      bg_update_ptr->bg_block_id);
		}
			
		slurm_mutex_lock(&block_state_mutex);

		_reset_block(bg_record);
		
		slurm_mutex_unlock(&block_state_mutex);
		
	} else if (bluegene_layout_mode == LAYOUT_DYNAMIC) {
		debug2("Hopefully we are destroying this block %s "
		       "since it isn't in the bg_list",
		       bg_update_ptr->bg_block_id);
	} else {
		error("Could not find block %s previously assigned to job.  "
		      "If this is happening at startup and you just changed "
		      "your bluegene.conf this is expected.  Else you should "
		      "probably restart your slurmctld since this shouldn't "
		      "happen outside of that.",
		      bg_update_ptr->bg_block_id);
	}

#ifdef HAVE_BG_FILES
	if ((rc = bridge_free_job_list(job_list)) != STATUS_OK)
		error("bridge_free_job_list(): %s", bg_err_str(rc));
#endif
	
}

/* Process requests off the bg_update_list queue and exit when done */
static void *_block_agent(void *args)
{
	bg_update_t *bg_update_ptr = NULL;
				
	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&agent_cnt_mutex);
		bg_update_ptr = list_dequeue(bg_update_list);
/* 		info("running %d %d %d", TERM_OP, bg_update_ptr->op, */
/* 		     list_count(bg_update_list)); */
		slurm_mutex_unlock(&agent_cnt_mutex);
		if (!bg_update_ptr) {
			usleep(100000);
			continue;
		}
		if (bg_update_ptr->op == START_OP)
			_start_agent(bg_update_ptr);
		else if (bg_update_ptr->op == TERM_OP)
			_term_agent(bg_update_ptr);
		else if (bg_update_ptr->op == SYNC_OP)
			_sync_agent(bg_update_ptr);
		_bg_list_del(bg_update_ptr);
	}
	slurm_mutex_lock(&agent_cnt_mutex);
	agent_cnt--;
	if (agent_cnt == 0) {
		list_destroy(bg_update_list);
		bg_update_list = NULL;
		pthread_cond_signal(&agent_cond);
			
	}
	slurm_mutex_unlock(&agent_cnt_mutex);
	return NULL;
}

/* Perform an operation upon a BG block (block) for starting or 
 * terminating a job */
static void _block_op(bg_update_t *bg_update_ptr)
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	
	slurm_mutex_lock(&agent_cnt_mutex);
	if ((bg_update_list == NULL)
	    &&  ((bg_update_list = list_create(_bg_list_del)) == NULL))
		fatal("malloc failure in start_job/list_create");

	/* push TERM_OP on the head of the queue
	 * append START_OP and SYNC_OP to the tail of the queue */
	if (bg_update_ptr->op == TERM_OP) {
		if (list_push(bg_update_list, bg_update_ptr) == NULL)
			fatal("malloc failure in _block_op/list_push");
	} else {
		if (list_enqueue(bg_update_list, bg_update_ptr) == NULL)
			fatal("malloc failure in _block_op/list_enqueue");
	}
		
	/* already running MAX_AGENTS we don't really need more 
	   since they never end */
	if (agent_cnt > MAX_AGENT_COUNT) {
		slurm_mutex_unlock(&agent_cnt_mutex);
		return;
	}

	slurm_mutex_unlock(&agent_cnt_mutex);
	agent_cnt++;
	/* spawn an agent */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent, 
					PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	
	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent, 
			      _block_agent, NULL)) {
		error("pthread_create error %m");
		if (++retries > MAX_PTHREAD_RETRIES)
			fatal("Can't create pthread");
		usleep(1000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_agent);
}


/* get a list of all BG blocks with users */
static List _get_all_blocks(void)
{
	List ret_list = list_create(destroy_bg_record);
	ListIterator itr;
	bg_record_t *block_ptr = NULL;
	bg_record_t *str_ptr = NULL;
	
	if (!ret_list)
		fatal("malloc error");

	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((block_ptr = (bg_record_t *) list_next(itr))) {
			if ((block_ptr->user_name == NULL)
			    ||  (block_ptr->user_name[0] == '\0')
			    ||  (block_ptr->bg_block_id == NULL)
			    ||  (block_ptr->bg_block_id[0] == '0'))
				continue;
			str_ptr = xmalloc(sizeof(bg_record_t));
			str_ptr->bg_block_id = xstrdup(block_ptr->bg_block_id);
			str_ptr->nodes = xstrdup(block_ptr->nodes);
			
			list_append(ret_list, str_ptr);
		}
		list_iterator_destroy(itr);
	} else {
		error("_get_all_blocks: no bg_list");
	}

	return ret_list;
}

/* remove a BG block from the given list */
static int _excise_block(List block_list, pm_partition_id_t bg_block_id,
			 char *nodes)
{
	int rc = SLURM_SUCCESS;
	ListIterator iter;
	bg_record_t *block = NULL;
	
	if(block_list) {
		iter = list_iterator_create(block_list);
		xassert(iter);
		while ((block = list_next(iter))) {
			rc = SLURM_ERROR;
			if (strcmp(block->bg_block_id, bg_block_id))
				continue;
			if (strcmp(block->nodes, nodes)) {	
				/* changed bgblock */
				error("bg_block_id:%s old_nodes:%s "
				      "new_nodes:%s",
				      bg_block_id, nodes, block->nodes);
				break;
			}
			
			/* exact match of name and node list */
			debug("synced Block %s", bg_block_id);
			list_delete_item(iter);
			rc = SLURM_SUCCESS;
			break;
		}		
		list_iterator_destroy(iter);
	} else {
		error("_excise_block: No block_list");
		rc = SLURM_ERROR;
	}
	return rc;
}

/*
 * Perform any work required to terminate a jobs on a block.
 * bg_block_id IN - block name
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: The job is killed before the function returns. This can take 
 * many seconds. Do not call from slurmctld  or any other entity that 
 * can not wait.
 */
int term_jobs_on_block(pm_partition_id_t bg_block_id)
{
	int rc = SLURM_SUCCESS;
	bg_update_t *bg_update_ptr;
	/* if (bg_update_list == NULL) { */
/* 		debug("No jobs started that I know about"); */
/* 		return rc; */
/* 	} */
	bg_update_ptr = xmalloc(sizeof(bg_update_t));
	bg_update_ptr->op = TERM_OP;
	bg_update_ptr->bg_block_id = xstrdup(bg_block_id);
	_block_op(bg_update_ptr);
	
	return rc;
}

/*
 * Perform any setup required to initiate a job
 * job_ptr IN - pointer to the job being initiated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BG block is available for use.
 */
extern int start_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_record_t *bg_record = NULL;

	bg_update_t *bg_update_ptr = NULL;

	bg_update_ptr = xmalloc(sizeof(bg_update_t));
	bg_update_ptr->op = START_OP;
	bg_update_ptr->job_ptr = job_ptr;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLOCK_ID, 
			     &(bg_update_ptr->bg_block_id));
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLRTS_IMAGE, 
			     &(bg_update_ptr->blrtsimage));
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_REBOOT, 
			     &(bg_update_ptr->reboot));
#ifdef HAVE_BGL
	if(!bg_update_ptr->blrtsimage) {
		bg_update_ptr->blrtsimage = xstrdup(default_blrtsimage);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_BLRTS_IMAGE, 
				     bg_update_ptr->blrtsimage);
	}
#else
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CONN_TYPE, 
			     &(bg_update_ptr->conn_type));
#endif

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_LINUX_IMAGE, 
			     &(bg_update_ptr->linuximage));
	if(!bg_update_ptr->linuximage) {
		bg_update_ptr->linuximage = xstrdup(default_linuximage);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_LINUX_IMAGE, 
				     bg_update_ptr->linuximage);
	}
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MLOADER_IMAGE, 
			     &(bg_update_ptr->mloaderimage));
	if(!bg_update_ptr->mloaderimage) {
		bg_update_ptr->mloaderimage = xstrdup(default_mloaderimage);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_MLOADER_IMAGE, 
				     bg_update_ptr->mloaderimage);
	}
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_RAMDISK_IMAGE, 
			     &(bg_update_ptr->ramdiskimage));
	if(!bg_update_ptr->ramdiskimage) {
		bg_update_ptr->ramdiskimage = xstrdup(default_ramdiskimage);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_RAMDISK_IMAGE, 
				     bg_update_ptr->ramdiskimage);
	}
	bg_record = 
		find_bg_record_in_list(bg_list, bg_update_ptr->bg_block_id);
	if (bg_record) {
		slurm_mutex_lock(&block_state_mutex);
		job_ptr->num_procs = bg_record->cpu_cnt;
		job_ptr->total_procs = job_ptr->num_procs;
		bg_record->job_running = bg_update_ptr->job_ptr->job_id;
		bg_record->job_ptr = bg_update_ptr->job_ptr;
		if(!block_ptr_exist_in_list(bg_job_block_list, bg_record)) {
			list_push(bg_job_block_list, bg_record);
			num_unused_cpus -= bg_record->cpu_cnt;
		}
		if(!block_ptr_exist_in_list(bg_booted_block_list, bg_record))
			list_push(bg_booted_block_list, bg_record);
		slurm_mutex_unlock(&block_state_mutex);
	} else {
		error("bg_record %s doesn't exist, requested for job (%d)", 
		      bg_update_ptr->bg_block_id, job_ptr->job_id);
		_bg_list_del(bg_update_ptr);
		return SLURM_ERROR;
	}
	info("Queue start of job %u in BG block %s",
	     job_ptr->job_id, 
	     bg_update_ptr->bg_block_id);
	_block_op(bg_update_ptr);
	return rc;
}


/*
 * Perform any work required to terminate a job
 * job_ptr IN - pointer to the job being terminated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd terminating
 * the job. Insure that this function, mpirun and the epilog can
 * all deal with termination race conditions.
 */
int term_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	bg_update_t *bg_update_ptr = NULL;
	
	bg_update_ptr = xmalloc(sizeof(bg_update_t));
	bg_update_ptr->op = TERM_OP;
	bg_update_ptr->job_ptr = job_ptr;
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLOCK_ID, 
			     &(bg_update_ptr->bg_block_id));
	info("Queue termination of job %u in BG block %s",
	     job_ptr->job_id, bg_update_ptr->bg_block_id);
	_block_op(bg_update_ptr);

	return rc;
}

/*
 * Synchronize BG block state to that of currently active jobs.
 * This can recover from slurmctld crashes when block usership
 * changes were queued
 */
extern int sync_jobs(List job_list)
{
	ListIterator job_iterator, block_iterator;
	struct job_record  *job_ptr = NULL;
	bg_update_t *bg_update_ptr = NULL;
	bg_record_t *bg_record = NULL;
	List block_list;
	static bool run_already = false;

	/* Execute only on initial startup. We don't support bgblock 
	 * creation on demand today, so there is no need to re-sync data. */
	if (run_already)
		return SLURM_SUCCESS;
	run_already = true;

	/* Insure that all running jobs own the specified block */
	block_list = _get_all_blocks();
	if(job_list) {
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *) 
			list_next(job_iterator))) {
			bool good_block = true;
			if (job_ptr->job_state != JOB_RUNNING)
				continue;
			
			bg_update_ptr = xmalloc(sizeof(bg_update_t));
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID, 
					     &(bg_update_ptr->bg_block_id));
#ifdef HAVE_BGL
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLRTS_IMAGE, 
					     &(bg_update_ptr->blrtsimage));
#endif
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_LINUX_IMAGE, 
					     &(bg_update_ptr->linuximage));
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_MLOADER_IMAGE, 
					     &(bg_update_ptr->mloaderimage));
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_RAMDISK_IMAGE, 
					     &(bg_update_ptr->ramdiskimage));
	
			if (bg_update_ptr->bg_block_id == NULL) {
				error("Running job %u has bgblock==NULL", 
				      job_ptr->job_id);
				good_block = false;
			} else if (job_ptr->nodes == NULL) {
				error("Running job %u has nodes==NULL",
				      job_ptr->job_id);
				good_block = false;
			} else if (_excise_block(block_list, 
						 bg_update_ptr->bg_block_id, 
						 job_ptr->nodes) 
				   != SLURM_SUCCESS) {
				error("Kill job %u belongs to defunct "
				      "bgblock %s",
				      job_ptr->job_id, 
				      bg_update_ptr->bg_block_id);
				good_block = false;
			}
			if (!good_block) {
				job_ptr->job_state = JOB_FAILED 
					| JOB_COMPLETING;
				job_ptr->end_time = time(NULL);
				_bg_list_del(bg_update_ptr);
				continue;
			}

			debug3("Queue sync of job %u in BG block %s "
			       "ending at %d",
			       job_ptr->job_id, 
			       bg_update_ptr->bg_block_id,
			       job_ptr->end_time);
			bg_update_ptr->op = SYNC_OP;
			bg_update_ptr->job_ptr = job_ptr;
			_block_op(bg_update_ptr);
		}
		list_iterator_destroy(job_iterator);
	} else {
		error("sync_jobs: no job_list");
		list_destroy(block_list);
		return SLURM_ERROR;
	}
	/* Insure that all other blocks are free of users */
	if(block_list) {
		block_iterator = list_iterator_create(block_list);
		while ((bg_record = (bg_record_t *) 
			list_next(block_iterator))) {
			info("Queue clearing of users of BG block %s",
			     bg_record->bg_block_id);
			bg_update_ptr = xmalloc(sizeof(bg_update_t));
			bg_update_ptr->op = TERM_OP;
			bg_update_ptr->bg_block_id = 
				xstrdup(bg_record->bg_block_id);
			_block_op(bg_update_ptr);
		}
		list_iterator_destroy(block_iterator);
		list_destroy(block_list);
	} else {
		/* this should never happen, 
		 * vestigial logic */
		error("sync_jobs: no block_list");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * Boot a block. Block state expected to be FREE upon entry. 
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 */
extern int boot_block(bg_record_t *bg_record)
{
#ifdef HAVE_BG_FILES
	int rc;	
		
	if ((rc = bridge_set_block_owner(bg_record->bg_block_id, 
					 bg_slurm_user_name)) 
	    != STATUS_OK) {
		error("bridge_set_block_owner(%s,%s): %s", 
		      bg_record->bg_block_id, 
		      bg_slurm_user_name,
		      bg_err_str(rc));
		return SLURM_ERROR;
	}	
			
	info("Booting block %s", bg_record->bg_block_id);
	if ((rc = bridge_create_block(bg_record->bg_block_id)) 
	    != STATUS_OK) {
		error("bridge_create_block(%s): %s",
		      bg_record->bg_block_id, bg_err_str(rc));
		if(rc == INCOMPATIBLE_STATE) {
			char reason[128], time_str[32];
			time_t now = time(NULL);
			slurm_make_time_str(&now, time_str, sizeof(time_str));
			snprintf(reason, sizeof(reason),
				 "boot_block: "
				 "Block %s is in an incompatable state.  "
				 "This usually means hardware is allocated "
				 "by another block (maybe outside of SLURM). "
				 "[SLURM@%s]", 
				 bg_record->bg_block_id, time_str);
			drain_as_needed(bg_record, reason);
			bg_record->boot_state = 0;
			bg_record->boot_count = 0;
		}
		return SLURM_ERROR;
	}
	
	slurm_mutex_lock(&block_state_mutex);
	if(!block_ptr_exist_in_list(bg_booted_block_list, bg_record))
		list_push(bg_booted_block_list, bg_record);
	slurm_mutex_unlock(&block_state_mutex);
	
	rc = 0;
	while(rc < 10) {
		if(bg_record->state == RM_PARTITION_CONFIGURING) {
			break;
		}
		sleep(1);
		rc++;
	}
	slurm_mutex_lock(&block_state_mutex);
	/* reset state right now, don't wait for 
	 * update_block_list() to run or epilog could 
	 * get old/bad data. */
	if(bg_record->state != RM_PARTITION_CONFIGURING)
		bg_record->state = RM_PARTITION_CONFIGURING;
	debug("Setting bootflag for %s", bg_record->bg_block_id);
	
	bg_record->boot_state = 1;
	//bg_record->boot_count = 0;
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);
#else
	slurm_mutex_lock(&block_state_mutex);
	if(!block_ptr_exist_in_list(bg_booted_block_list, bg_record))
		list_push(bg_booted_block_list, bg_record);
	bg_record->state = RM_PARTITION_READY;
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);				
#endif
	

	return SLURM_SUCCESS;
}

extern void waitfor_block_agents()
{
	if(agent_cnt)
		pthread_cond_wait(&agent_cond, &agent_cnt_mutex);
}
