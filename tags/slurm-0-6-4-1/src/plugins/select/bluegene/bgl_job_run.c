/*****************************************************************************\
 *  bgl_job_run.c - blue gene job execution (e.g. initiation and termination) 
 *  functions.
 *
 *  $Id$ 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/proc_req.h"
#include "bluegene.h"

#ifdef HAVE_BGL_FILES

#define MAX_POLL_RETRIES    220
#define POLL_INTERVAL        3
#define MAX_AGENT_COUNT      130
enum update_op {START_OP, TERM_OP, SYNC_OP};

typedef struct bgl_update {
	enum update_op op;	/* start | terminate | sync */
	uid_t uid;		/* new user */
	uint32_t job_id;	/* SLURM job id */	
	uint16_t node_use;      /* SLURM job node_use */	
	pm_partition_id_t bgl_part_id;
} bgl_update_t;

static List bgl_update_list = NULL;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static int agent_cnt = 0;

static void	_bgl_list_del(void *x);
static int	_excise_block(List block_list, 
			      pm_partition_id_t bgl_part_id, 
			      char *nodes);
static List	_get_all_blocks(void);
static void *	_part_agent(void *args);
static void	_part_op(bgl_update_t *bgl_update_ptr);
static int	_remove_job(db_job_id_t job_id);
static void	_start_agent(bgl_update_t *bgl_update_ptr);
static void	_sync_agent(bgl_update_t *bgl_update_ptr);
static void	_term_agent(bgl_update_t *bgl_update_ptr);


/* Delete a bgl_update_t record */
static void _bgl_list_del(void *x)
{
	bgl_update_t *bgl_update_ptr = (bgl_update_t *) x;

	if (bgl_update_ptr) {
		xfree(bgl_update_ptr->bgl_part_id);
		xfree(bgl_update_ptr);
	}
}

/* Kill a job and remove its record from MMCS */
static int _remove_job(db_job_id_t job_id)
{
	int i, rc;
	rm_job_t *job_rec = NULL;
	rm_job_state_t job_state;

	debug("removing job %d from MMCS", job_id);
	for (i=0; i<MAX_POLL_RETRIES; i++) {
		if (i > 0)
			sleep(POLL_INTERVAL);

		/* Find the job */
		if ((rc = rm_get_job(job_id, &job_rec)) != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 

			error("rm_get_job(%d): %s", job_id, 
			      bgl_err_str(rc));
			continue;
		}

		if ((rc = rm_get_data(job_rec, RM_JobState, &job_state)) != 
				STATUS_OK) {
			(void) rm_free_job(job_rec);
			if (rc == JOB_NOT_FOUND) {
				debug("job %d not found in MMCS", job_id);
				return STATUS_OK;
			} 

			error("rm_get_data(RM_JobState) for jobid=%d "
			      "%s", job_id, bgl_err_str(rc));
			continue;
		}
		if ((rc = rm_free_job(job_rec)) != STATUS_OK)
			error("rm_free_job: %s", bgl_err_str(rc));

		debug2("job %d is in state %d", job_id, job_state);
		
		/* check the state and process accordingly */
		if(job_state == RM_JOB_TERMINATED)
			return STATUS_OK;
		else if(job_state == RM_JOB_DYING)
			continue;
		else if(job_state == RM_JOB_ERROR) {
			error("job %d is in a error state.", job_id);
			
			//free_bgl_partition();
			return STATUS_OK;
		}

		(void) jm_signal_job(job_id, SIGKILL);
		rc = jm_cancel_job(job_id);
		/* it doesn't appear that this does anything. */
		// rc = rm_remove_job(job_id);

		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from MMCS", job_id);
				return STATUS_OK;
			} 
			if(rc == INCOMPATIBLE_STATE)
				debug("job %d is in an INCOMPATIBLE_STATE",
				      job_id);
			else
				error("rm_cancel_job(%d): %s", job_id, 
				      bgl_err_str(rc));
		}
	}
	/* try once more... */
	/* it doesn't appear that this does anything. */
	// (void) rm_remove_job(job_id);
	error("Failed to remove job %d from MMCS", job_id);
	return INTERNAL_ERROR;
}



/* Update partition user and reboot as needed */
static void _sync_agent(bgl_update_t *bgl_update_ptr)
{
	bgl_record_t * bgl_record = NULL;
	
	bgl_record = find_bgl_record(bgl_update_ptr->bgl_part_id);
	if(!bgl_record) {
		error("No partition %s", bgl_update_ptr->bgl_part_id);
		return;
	}
		
	if(bgl_record->state==RM_PARTITION_READY) {
		if(bgl_record->user_uid != bgl_update_ptr->uid) {
			slurm_mutex_lock(&part_state_mutex);
			debug("User isn't correct for job %d on %s, "
			      "fixing...", 
			      bgl_update_ptr->job_id,
			      bgl_update_ptr->bgl_part_id);
			xfree(bgl_record->target_name);
			bgl_record->target_name = 
				xstrdup(uid_to_string(
						bgl_update_ptr->uid));
				
			if(update_partition_user(bgl_record) == 1)
				last_bgl_update = time(NULL);
			slurm_mutex_unlock(&part_state_mutex);
		}
	} else {
		if(bgl_record->state != RM_PARTITION_CONFIGURING) {
			error("Partition %s isn't ready and isn't "
			      "being configured! Starting job again.",
			      bgl_update_ptr->bgl_part_id);
		} else {
			debug("Partition %s is booting, job ok",
			      bgl_update_ptr->bgl_part_id);
		}
		_start_agent(bgl_update_ptr);
	}
}

/* Perform job initiation work */
static void _start_agent(bgl_update_t *bgl_update_ptr)
{
	int rc;
	bgl_record_t *bgl_record = NULL;
	bgl_record_t *found_record = NULL;
	ListIterator itr;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	
	bgl_record = find_bgl_record(bgl_update_ptr->bgl_part_id);
					
	if(!bgl_record) {
		error("partition %s not found in bgl_list",
		      bgl_update_ptr->bgl_part_id);
		return;
	}

	slurm_mutex_lock(&part_state_mutex);
	bgl_record->job_running = 1;
	slurm_mutex_unlock(&part_state_mutex);
		
	if(bgl_record->state == RM_PARTITION_DEALLOCATING) {
		debug("Partition is in Deallocating state, waiting for free.");
		bgl_free_partition(bgl_record);
	}
	
	if(bgl_record->state == RM_PARTITION_FREE) {
		num_part_to_free = 0;
		num_part_freed = 0;
		itr = list_iterator_create(bgl_list);
		if(bgl_record->full_partition) {
			debug("Using full partition freeing all others");
			while ((found_record = (bgl_record_t*) 
				list_next(itr)) != NULL) {
				if(found_record->state != RM_PARTITION_FREE) {
					slurm_attr_init(&attr_agent);
					if (pthread_attr_setdetachstate(
						    &attr_agent, 
						    PTHREAD_CREATE_JOINABLE))
						error("pthread_attr_setdetach"
						      "state error %m");

					retries = 0;
					while (pthread_create(&thread_agent, 
							      &attr_agent, 
							      mult_free_part, 
							      (void *)
							      found_record)) {
						error("pthread_create "
						      "error %m");
						if (++retries 
						    > MAX_PTHREAD_RETRIES)
							fatal("Can't create "
							      "pthread");
						/* sleep and retry */
						usleep(1000);	
					}
					num_part_to_free++;
				}
			}		
		} else {
			while ((found_record = (bgl_record_t*) 
				list_next(itr)) != NULL) {
				if (found_record->full_partition) {
					if(found_record->state 
					   != RM_PARTITION_FREE) {
						debug("destroying the "
						      "full partition %s.", 
						      found_record->
						      bgl_part_id);
						bgl_free_partition(
							found_record);
					}
					break;
				}
			}
		} 
		list_iterator_destroy(itr);
		
		/* wait for all necessary partitions to be freed */
		while(num_part_to_free != num_part_freed) {
			sleep(1);
		}
		
		if(!bgl_record->job_running) 
			return;
		if((rc = boot_part(bgl_record))
		   != SLURM_SUCCESS) {
			sleep(2);	
			/* wait for the slurmd to begin 
			   the batch script, slurm_fail_job() 
			   is a no-op if issued prior 
			   to the script initiation */
			(void) slurm_fail_job(
				bgl_update_ptr->job_id);
			return;
		}
	} else if (bgl_record->state == RM_PARTITION_CONFIGURING) {
		bgl_record->boot_state = 1;		
	}

	slurm_mutex_lock(&part_state_mutex);
	bgl_record->boot_count = 0;
	xfree(bgl_record->target_name);
	bgl_record->target_name = xstrdup(uid_to_string(bgl_update_ptr->uid));
	debug("setting the target_name for Partition %s to %s",
	      bgl_record->bgl_part_id,
	      bgl_record->target_name);
	
	if(bgl_record->state == RM_PARTITION_READY) {
		debug("partition %s is ready.",
		      bgl_record->bgl_part_id);
				
		set_part_user(bgl_record); 
	}
	slurm_mutex_unlock(&part_state_mutex);	
}

/* Perform job termination work */
static void _term_agent(bgl_update_t *bgl_update_ptr)
{
	int i, jobs, rc;
	rm_job_list_t *job_list = NULL;
	int live_states;
	rm_element_t *job_elem = NULL;
	pm_partition_id_t part_id;
	db_job_id_t job_id;
	bgl_record_t *bgl_record = NULL;
	time_t now;
	struct tm *time_ptr;
	char reason[128];
	int job_remove_failed = 0;

	debug2("getting the job info");
	live_states = JOB_ALL_FLAG 
		& (~JOB_TERMINATED_FLAG) 
		& (~JOB_KILLED_FLAG);
	if ((rc = rm_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("rm_get_jobs(): %s", bgl_err_str(rc));
		return;
	}
	
	if ((rc = rm_get_data(job_list, RM_JobListSize, &jobs)) != STATUS_OK) {
		error("rm_get_data(RM_JobListSize): %s", bgl_err_str(rc));
		jobs = 0;
	} else if (jobs > 300)
		fatal("Active job count (%d) invalid, restart MMCS", jobs);

	debug2("job count %d",jobs);

	for (i=0; i<jobs; i++) {		
		if (i) {
			if ((rc = rm_get_data(job_list, RM_JobListNextJob, 
					&job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListNextJob): %s", 
				      bgl_err_str(rc));
				continue;
			}
		} else {
			if ((rc = rm_get_data(job_list, RM_JobListFirstJob, 
					      &job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListFirstJob): %s",
				      bgl_err_str(rc));
				continue;
			}
		}
		
		if(!job_elem) {
			error("No Job Elem breaking out job count = %d\n", 
			      jobs);
			break;
		}
		if ((rc = rm_get_data(job_elem, RM_JobPartitionID, &part_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_JobPartitionID) %s: %s", 
			      part_id, bgl_err_str(rc));
			continue;
		}

		if(!part_id) {
			error("No partitionID returned from Database");
			continue;
		}

		debug2("looking at partition %s looking for %s\n",
			part_id, bgl_update_ptr->bgl_part_id);
			
		if (strcmp(part_id, bgl_update_ptr->bgl_part_id) != 0) {
			free(part_id);
			continue;
		}
		
		free(part_id);

		if ((rc = rm_get_data(job_elem, RM_JobDBJobID, &job_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_JobDBJobID): %s", 
			      bgl_err_str(rc));
			continue;
		}
		debug2("got job_id %d",job_id);
		if((rc = _remove_job(job_id)) == INTERNAL_ERROR) {
			job_remove_failed = 1;
			break;
		}
	}
	
	/* remove the partition's users */
	bgl_record = find_bgl_record(bgl_update_ptr->bgl_part_id);
	if(bgl_record) {
		debug2("got the record %s user is %s",
		      bgl_record->bgl_part_id,
		      bgl_record->user_name);

		if(job_remove_failed) {
			time_ptr = localtime(&now);
			strftime(reason, sizeof(reason),
				 "_term_agent: "
				 "Couldn't remove job "
				 "[SLURM@%b %d %H:%M]",
				 time_ptr);
			if(bgl_record->nodes)
				slurm_drain_nodes(bgl_record->nodes, 
						  reason);
			else
				error("Partition %s doesn't have a node list.",
				      bgl_update_ptr->bgl_part_id);
		}
			
		slurm_mutex_lock(&part_state_mutex);
		bgl_record->job_running = 0;
		
		/*remove user from list */
		if(bgl_record->target_name) {
			if(strcmp(bgl_record->target_name, 
				  slurmctld_conf.slurm_user_name)) {
				xfree(bgl_record->target_name);
				bgl_record->target_name = 
					xstrdup(slurmctld_conf.
						slurm_user_name);
			}
		
			update_partition_user(bgl_record);
		} else {
			bgl_record->target_name = 
				xstrdup(slurmctld_conf.slurm_user_name);
		}	
		
		bgl_record->boot_state = 0;
		bgl_record->boot_count = 0;
		
		last_bgl_update = time(NULL);
		slurm_mutex_unlock(&part_state_mutex);
	} 
not_removed:
	if ((rc = rm_free_job_list(job_list)) != STATUS_OK)
		error("rm_free_job_list(): %s", bgl_err_str(rc));
}
	
/* Process requests off the bgl_update_list queue and exit when done */
static void *_part_agent(void *args)
{
	bgl_update_t *bgl_update_ptr;

	/*
	 * Don't just exit when there is no work left. Creating 
	 * pthreads from within a dynamically linked object (plugin)
	 * causes large memory leaks on some systems that seem 
	 * unavoidable even from detached pthreads.
	 */
	while (!agent_fini) {
		slurm_mutex_lock(&agent_cnt_mutex);
		bgl_update_ptr = list_dequeue(bgl_update_list);
		slurm_mutex_unlock(&agent_cnt_mutex);
		if (!bgl_update_ptr) {
			usleep(100000);
			continue;
		}
		if (bgl_update_ptr->op == START_OP)
			_start_agent(bgl_update_ptr);
		else if (bgl_update_ptr->op == TERM_OP) 
			_term_agent(bgl_update_ptr);
		else if (bgl_update_ptr->op == SYNC_OP)
			_sync_agent(bgl_update_ptr);
		_bgl_list_del(bgl_update_ptr);
	}
	slurm_mutex_lock(&agent_cnt_mutex);
	agent_cnt--;
	slurm_mutex_unlock(&agent_cnt_mutex);
	return NULL;
}

/* Perform an operation upon a BGL partition (block) for starting or 
 * terminating a job */
static void _part_op(bgl_update_t *bgl_update_ptr)
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	
	slurm_mutex_lock(&agent_cnt_mutex);
	if ((bgl_update_list == NULL)
	&&  ((bgl_update_list = list_create(_bgl_list_del)) == NULL))
		fatal("malloc failure in start_job/list_create");

	/* push job onto queue in a FIFO */
	if (list_push(bgl_update_list, bgl_update_ptr) == NULL)
		fatal("malloc failure in _part_op/list_push");
	
	if (agent_cnt > MAX_AGENT_COUNT) {	/* already running an agent */
		slurm_mutex_unlock(&agent_cnt_mutex);
		return;
	}
	agent_cnt++;
	slurm_mutex_unlock(&agent_cnt_mutex);
	/* spawn an agent */
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent, PTHREAD_CREATE_JOINABLE))
		error("pthread_attr_setdetachstate error %m");

	retries = 0;
	while (pthread_create(&thread_agent, &attr_agent, _part_agent, NULL)) {
		error("pthread_create error %m");
		if (++retries > MAX_PTHREAD_RETRIES)
			fatal("Can't create pthread");
		usleep(1000);	/* sleep and retry */
	}
}


/* get a list of all BGL blocks with users */
static List _get_all_blocks(void)
{
	List ret_list = list_create(destroy_bgl_record);
	ListIterator itr;
	bgl_record_t *block_ptr = NULL;
	bgl_record_t *str_ptr = NULL;
	
	if (!ret_list)
		fatal("malloc error");

	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((block_ptr = (bgl_record_t *) list_next(itr))) {
			if ((block_ptr->user_name == NULL)
			    ||  (block_ptr->user_name[0] == '\0')
			    ||  (block_ptr->bgl_part_id == NULL)
			    ||  (block_ptr->bgl_part_id[0] == '0'))
				continue;
			str_ptr = xmalloc(sizeof(bgl_record_t));
			str_ptr->bgl_part_id = xstrdup(block_ptr->bgl_part_id);
			str_ptr->nodes = xstrdup(block_ptr->nodes);
			
			list_append(ret_list, str_ptr);
		}
		list_iterator_destroy(itr);
	} else {
		error("_get_all_blocks: no bgl_list");
	}

	return ret_list;
}

/* remove a BGL block from the given list */
static int _excise_block(List block_list, pm_partition_id_t bgl_part_id,
			 char *nodes)
{
	int rc = SLURM_SUCCESS;
	ListIterator iter;
	bgl_record_t *block = NULL;
	
	if(block_list) {
		iter = list_iterator_create(block_list);
		xassert(iter);
		while ((block = list_next(iter))) {
			rc = SLURM_ERROR;
			if (strcmp(block->bgl_part_id, bgl_part_id))
				continue;
			if (strcmp(block->nodes, nodes)) {	
				/* changed bglblock */
				error("bgl_part_id:%s old_nodes:%s "
				      "new_nodes:%s",
				      bgl_part_id, nodes, block->nodes);
				break;
			}
			
			/* exact match of name and node list */
			debug("synced Partition %s", bgl_part_id);
			list_delete(iter);
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
 * Perform any work required to terminate a jobs on a partition.
 * bgl_part_id IN - partition name
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: The job is killed before the function returns. This can take 
 * many seconds. Do not call from slurmctld  or any other entity that 
 * can not wait.
 */
int term_jobs_on_part(pm_partition_id_t bgl_part_id)
{
	int rc = SLURM_SUCCESS;
	bgl_update_t *bgl_update_ptr;
	/* if (bgl_update_list == NULL) { */
/* 		debug("No jobs started that I know about"); */
/* 		return rc; */
/* 	} */
	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->op = TERM_OP;
	bgl_update_ptr->bgl_part_id = xstrdup(bgl_part_id);
	_part_op(bgl_update_ptr);
	
	return rc;
}

#endif

/*
 * Perform any setup required to initiate a job
 * job_ptr IN - pointer to the job being initiated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd spawning
 * the job. A prolog script is expected to defer initiation of
 * the job script until the BGL block is available for use.
 */
extern int start_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BGL_FILES
	bgl_update_t *bgl_update_ptr = NULL;
		
	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->op = START_OP;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->job_id = job_ptr->job_id;
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_PART_ID, &(bgl_update_ptr->bgl_part_id));
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_NODE_USE, &(bgl_update_ptr->node_use));
	info("Queue start of job %u in BGL partition %s",
	     job_ptr->job_id, 
	     bgl_update_ptr->bgl_part_id);

	_part_op(bgl_update_ptr);
#else
	if (bgl_list) {
		ListIterator itr;
		bgl_record_t *bgl_record;
		char *part_id = NULL;
		uint16_t node_use;

		select_g_get_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_PART_ID, &part_id);
		select_g_get_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_NODE_USE, &node_use);
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = (bgl_record_t *) list_next(itr))) {
			if (!part_id)
				break;
			if ((!bgl_record->bgl_part_id)
			||  (strcmp(part_id, bgl_record->bgl_part_id)))
				continue;
			bgl_record->node_use = node_use;
			bgl_record->state = RM_PARTITION_READY;
			last_bgl_update = time(NULL);
			break;
		}
		list_iterator_destroy(itr);
		xfree(part_id);
	}
#endif
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
#ifdef HAVE_BGL_FILES

	bgl_update_t *bgl_update_ptr = NULL;
	
	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->op = TERM_OP;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->job_id = job_ptr->job_id;
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_PART_ID, &(bgl_update_ptr->bgl_part_id));
	info("Queue termination of job %u in BGL partition %s",
		job_ptr->job_id, bgl_update_ptr->bgl_part_id);
	_part_op(bgl_update_ptr);
#endif
	return rc;
}

/*
 * Synchronize BGL block state to that of currently active jobs.
 * This can recover from slurmctld crashes when partition usership
 * changes were queued
 */
extern int sync_jobs(List job_list)
{
#ifdef HAVE_BGL_FILES
	ListIterator job_iterator, block_iterator;
	struct job_record  *job_ptr = NULL;
	bgl_update_t *bgl_update_ptr = NULL;
	bgl_record_t *bgl_record = NULL;
	List block_list;
	static bool run_already = false;

	/* Execute only on initial startup. We don't support bglblock 
	 * creation on demand today, so there is no need to re-sync data. */
	if (run_already)
		return SLURM_SUCCESS;
	run_already = true;

	/* Insure that all running jobs own the specified partition */
	block_list = _get_all_blocks();
	if(job_list) {
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *) 
			list_next(job_iterator))) {
			bool good_block = true;
			if (job_ptr->job_state != JOB_RUNNING)
				continue;
			
			bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
			select_g_get_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_PART_ID, 
					     &(bgl_update_ptr->bgl_part_id));

			if (bgl_update_ptr->bgl_part_id == NULL) {
				error("Running job %u has bglblock==NULL", 
				      job_ptr->job_id);
				good_block = false;
			} else if (job_ptr->nodes == NULL) {
				error("Running job %u has nodes==NULL",
				      job_ptr->job_id);
				good_block = false;
			} else if (_excise_block(block_list, 
						 bgl_update_ptr->bgl_part_id, 
						 job_ptr->nodes) 
				   != SLURM_SUCCESS) {
				error("Kill job %u belongs to defunct "
				      "bglblock %s",
				      job_ptr->job_id, 
				      bgl_update_ptr->bgl_part_id);
				good_block = false;
			}
			if (!good_block) {
				job_ptr->job_state = JOB_FAILED 
					| JOB_COMPLETING;
				job_ptr->end_time = time(NULL);
				xfree(bgl_update_ptr->bgl_part_id);
				xfree(bgl_update_ptr);
				continue;
			}

			debug3("Queue sync of job %u in BGL partition %s",
			       job_ptr->job_id, 
			       bgl_update_ptr->bgl_part_id);
			bgl_update_ptr->op = SYNC_OP;
			bgl_update_ptr->uid = job_ptr->user_id;
			bgl_update_ptr->job_id = job_ptr->job_id;
			_part_op(bgl_update_ptr);
		}
		list_iterator_destroy(job_iterator);
	} else {
		error("sync_jobs: no job_list");
		return SLURM_ERROR;
	}
	/* Insure that all other partitions are free of users */
	if(block_list) {
		block_iterator = list_iterator_create(block_list);
		while ((bgl_record = (bgl_record_t *) 
			list_next(block_iterator))) {
			info("Queue clearing of users of BGL partition %s",
			     bgl_record->bgl_part_id);
			bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
			bgl_update_ptr->op = TERM_OP;
			bgl_update_ptr->bgl_part_id = 
				xstrdup(bgl_record->bgl_part_id);
			_part_op(bgl_update_ptr);
		}
		list_iterator_destroy(block_iterator);
		list_destroy(block_list);
	} else {
		error("sync_jobs: no block_list");
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

/*
 * Boot a partition. Partition state expected to be FREE upon entry. 
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 */
extern int boot_part(bgl_record_t *bgl_record)
{
#ifdef HAVE_BGL_FILES
	int rc;
	
	if ((rc = rm_set_part_owner(bgl_record->bgl_part_id, 
				    slurmctld_conf.slurm_user_name)) 
	    != STATUS_OK) {
		error("rm_set_part_owner(%s,%s): %s", 
		      bgl_record->bgl_part_id, 
		      slurmctld_conf.slurm_user_name,
		      bgl_err_str(rc));
		return SLURM_ERROR;
	}
	
	info("Booting partition %s", 
	     bgl_record->bgl_part_id);
	if ((rc = pm_create_partition(bgl_record->bgl_part_id)) 
	    != STATUS_OK) {
		error("pm_create_partition(%s): %s",
		      bgl_record->bgl_part_id, bgl_err_str(rc));
		return SLURM_ERROR;
	}
	rc = 0;
	while(rc < 10) {
		if(bgl_record->state == RM_PARTITION_CONFIGURING)
			break;
		sleep(1);
		rc++;
	}
	slurm_mutex_lock(&part_state_mutex);
	/* reset state right now, don't wait for 
	 * update_partition_list() to run or epilog could 
	 * get old/bad data. */
	if(bgl_record->state != RM_PARTITION_CONFIGURING)
		bgl_record->state = RM_PARTITION_CONFIGURING;
	debug("Setting bootflag for %s", bgl_record->bgl_part_id);
	bgl_record->boot_state = 1;
	//bgl_record->boot_count = 0;
	last_bgl_update = time(NULL);
	slurm_mutex_unlock(&part_state_mutex);
#endif
	return SLURM_SUCCESS;
}
