/*****************************************************************************\
 *  bgl_job_run.c - blue gene job execution (e.g. initiation and termination) 
 *  functions. 
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

#define MAX_POLL_RETRIES    30
#define MAX_PTHREAD_RETRIES  1
#define POLL_INTERVAL        2

#define KILL_PARTS_ON_REBOOT 1	/* FIXME: Temporaroy */

enum update_op {START_OP, TERM_OP, SYNC_OP};

typedef struct bgl_update {
	enum update_op op;	/* start | terminate | sync */
	uid_t uid;		/* new owner */
	uint32_t job_id;	/* SLURM job id */	
	pm_partition_id_t bgl_part_id;
} bgl_update_t;

List bgl_update_list = NULL;
static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static int agent_cnt = 0;

static void	_bgl_list_del(void *x);
static void	_block_list_elem_del(void *x);
static int	_boot_part(pm_partition_id_t bgl_part_id);
static int	_excise_block(List block_list, pm_partition_id_t bgl_part_id, 
				char *nodes);
static List	_get_all_blocks(void);
static char *	_get_part_owner(pm_partition_id_t bgl_part_id);
static void *	_part_agent(void *args);
static void	_part_op(bgl_update_t *bgl_update_ptr);
static int	_remove_job(db_job_id_t job_id);
static int	_set_part_owner(pm_partition_id_t bgl_part_id, char *user);
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

/* Kill a job and remove its record from DB2 */
static int _remove_job(db_job_id_t job_id)
{
	int i, rc;
	rm_job_t *job_rec;
	rm_job_state_t job_state;

	debug("removing job %d from DB2", job_id);
	for (i=0; i<MAX_POLL_RETRIES; i++) {
		if (i > 0)
			sleep(POLL_INTERVAL);

		/* Find the job */
		if ((rc = rm_get_job(job_id, &job_rec)) != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from DB2", job_id);
				rc = STATUS_OK;
			} else
				error("rm_get_job(%d): %s", job_id, 
					bgl_err_str(rc));
			return rc;
		}

		if ((rc = rm_get_data(job_rec, RM_JobState, &job_state)) != 
				STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d not found in DB2", job_id);
				rc = STATUS_OK;
			} else
				error("rm_get_data(RM_JobState) for jobid=%d "
					"%s", job_id, bgl_err_str(rc));
			(void) rm_free_job(job_rec);
			return rc;
		}
		if ((rc = rm_free_job(job_rec)) != STATUS_OK)
			error("rm_free_job: %s", bgl_err_str(rc));

		/* Cancel or remove the job */
		if (job_state == RM_JOB_RUNNING) {
			jm_signal_job(job_id, SIGKILL);
			rc = jm_cancel_job(job_id);
		} else
			rc = rm_remove_job(job_id);
		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from DB2", job_id);
				rc = STATUS_OK;
			} else if (job_state == RM_JOB_RUNNING)
				error("jm_cancel_job(%d): %s", job_id, 
					bgl_err_str(rc));
			else
				error("rm_remove_job(%d): %s", job_id, 
					bgl_err_str(rc));
			return rc;
		}
	}
	/* try once more... */
	(void) rm_remove_job(job_id);
	error("Failed to remove job %d from DB2", job_id);
	return INTERNAL_ERROR;
}

/* Get the owner of an existing partition. Caller must xfree() return value. */
static char *_get_part_owner(pm_partition_id_t bgl_part_id)
{
	int rc, i, j, num_parts;
	char *owner, *cur_owner;
	rm_partition_t *part_ptr;
	rm_partition_list_t *part_list;
	
	for(i=2;i<6;i++) {
		if ((rc = rm_get_partitions_info(i, &part_list))
		    != STATUS_OK) {
			error("rm_get_partitions() errno=%s\n", 
			      bgl_err_str(rc));
			
		}
		rm_get_data(part_list, RM_PartListSize, &num_parts);
		for(j=0; j<num_parts; j++) {
			if(j)
				rm_get_data(part_list, RM_PartListNextPart, &part_ptr);
			else
				rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
			rm_get_data(part_ptr, RM_PartitionID, &owner);
			if(!strcasecmp(bgl_part_id, owner)) {
				rc = rm_get_data(part_ptr, RM_PartitionUserName, &owner);
				break;
			}
		}
		rm_free_partition_list(part_list);
		if(j<num_parts)
			break;
	}
	/* if ((rc = rm_get_partition(bgl_part_id,  &part_ptr)) != STATUS_OK) { */
/* 		error("rm_get_partition(%s): %s", bgl_part_id, bgl_err_str(rc)); */
/* 		return NULL; */
/* 	} */
/* 	if ((rc = rm_get_data(part_ptr, RM_PartitionUserName, &owner)) !=  */
/* 			STATUS_OK) { */
/* 		error("rm_get_data(RM_PartitionUserName): %s", bgl_err_str(rc)); */
/* 		(void) rm_free_partition(part_ptr); */
/* 		return NULL; */
/* 	} */
	cur_owner = xstrdup(owner);
	/* if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) */
/* 		error("rm_free_partition(): %s", bgl_err_str(rc)); */
	return cur_owner;
}

/* Set the owner of an existing partition */
static int _set_part_owner(pm_partition_id_t bgl_part_id, char *user)
{
	int rc;
	rm_partition_t * part_ptr;
	
	if (user && user[0])
		info("Setting partition %s owner to %s", bgl_part_id, user);
	else
		info("Clearing partition %s owner", bgl_part_id);


#ifdef USE_BGL_FILES

/* Logic shown below is the type of code we want to use to change the 
 * owner of an existing bglblock - without rebooting it. This logic 
 * does not work as of driver 040 2/17/2005.
 */

	int err_ret = SLURM_SUCCESS;

/* 	find the partition */
	if ((rc = rm_get_partition(bgl_part_id,  &part_ptr)) != STATUS_OK) {
		error("rm_get_partition(%s): %s", bgl_part_id, bgl_err_str(rc));
		return SLURM_ERROR;
	}

/* 	/\* set its owner *\/ */
	if ((rc = rm_set_part_owner(bgl_part_id, user)) != STATUS_OK) {
		error("rm_set_part_owner(%s,%s): %s", bgl_part_id, user,
			bgl_err_str(rc));
		return SLURM_ERROR;
	}
	

/* 	if ((rc = rm_set_data(part_ptr, RM_PartitionUserName, &user)) */
/* 			!= STATUS_OK) { */
/* 		error("rm_set_date(%s, RM_PartitionUserName): %s", bgl_part_id, */
/* 			bgl_err_str(rc)); */
/* 		err_ret = SLURM_ERROR; */
/* 	} */

	if ((rc = rm_free_partition(part_ptr)) != STATUS_OK)
		error("rm_free_partition(): %s", bgl_err_str(rc));

	return err_ret;
#else
	int i=0, j, num_parts;
	rm_partition_list_t *part_list;
	rm_partition_state_t state;
	rm_partition_state_flag_t part_state = RM_PARTITION_ALL;
	char *name;
	int is_ready=0;
	/* Wait for partition state to be FREE */
	for (i=0; i<MAX_POLL_RETRIES; i++) {
		if (i > 0)
			sleep(POLL_INTERVAL);

		if ((rc = rm_get_partitions_info(part_state, &part_list))
		    != STATUS_OK) {
			error("rm_get_partitions() errno=%s\n", 
				bgl_err_str(rc));
			
		}
		rm_get_data(part_list, RM_PartListSize, &num_parts);
		for(j=0; j<num_parts; j++) {
			if(j)
				rm_get_data(part_list, RM_PartListNextPart, &part_ptr);
			else
				rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
			rm_get_data(part_ptr, RM_PartitionID, &name);
			if(!strcasecmp(bgl_part_id, name)) {
				rc = rm_get_data(part_ptr, RM_PartitionState, &state);
				is_ready = 1;
				break;
			}
		}
		rm_free_partition_list(part_list);
		if (state == RM_PARTITION_FREE)
			break;	/* partition is now free */
		/* /\* find the partition *\/ */
/* 		if ((rc = rm_get_partition(bgl_part_id,  &part_ptr)) !=  */
/* 				STATUS_OK) { */
/* 			error("rm_get_partition(%s): %s", bgl_part_id,  */
/* 				bgl_err_str(rc)); */
/* 			return SLURM_ERROR; */
/* 		} */

/* 		/\* find its state *\/ */
/* 		rc = rm_get_data(part_ptr, RM_PartitionState, &part_state); */
/* 		if (rc != STATUS_OK) { */
/* 			error("rm_get_data(RM_PartitionState): %s",  */
/* 				bgl_err_str(rc)); */
/* 			(void) rm_free_partition(part_ptr); */
/* 			return SLURM_ERROR; */
/* 		} */

/* 		if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) */
/* 			error("rm_free_partition(): %s", bgl_err_str(rc)); */

/* 		if (part_state == RM_PARTITION_FREE) */
/* 			break;	/\* partition is now free *\/ */

		/* Destroy the partition, only on first pass */
		if ((i == 0)
		&&  ((rc = pm_destroy_partition(bgl_part_id)) != STATUS_OK))  {
			error("pm_destroy_partition(%s): %s", bgl_part_id,
				bgl_err_str(rc));
			return SLURM_ERROR;
		}
	}

	/* if (part_state != RM_PARTITION_FREE) { */
/* 		error("Could not free partition %s", bgl_part_id); */
/* 		return SLURM_ERROR; */
/* 	} */

	if (!is_ready) {
		error("Could not free partition %s", bgl_part_id);
		return SLURM_ERROR;
	}

	if ((rc = rm_set_part_owner(bgl_part_id, user)) != STATUS_OK) {
		error("rm_set_part_owner(%s,%s): %s", bgl_part_id, user,
			bgl_err_str(rc));
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
#endif
}

/*
 * Boot a partition. Partition state expected to be FREE upon entry. 
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 */
static int _boot_part(pm_partition_id_t bgl_part_id)
{
#ifdef HAVE_BGL_FILES
/* Due to various system problems, we do not want to boot BGL
 * partitions when each job is started, but only at slurmctld 
 * startup on an as needed basis. */
	int rc;
	
	info("Booting partition %s", bgl_part_id);
	if ((rc = pm_create_partition(bgl_part_id)) != STATUS_OK) {
		error("pm_create_partition(%s): %s", 
			bgl_part_id, bgl_err_str(rc));
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}

/* Update partition owner and reboot as needed */
static void _sync_agent(bgl_update_t *bgl_update_ptr)
{
	char *cur_part_owner, *new_part_owner;

	cur_part_owner = _get_part_owner(bgl_update_ptr->bgl_part_id);
	new_part_owner = uid_to_string(bgl_update_ptr->uid);
	if (strcmp(cur_part_owner, new_part_owner)) {
		error("changing owner of bgl_part %s from %s to %s",
			bgl_update_ptr->bgl_part_id, cur_part_owner, 
			new_part_owner);
		_term_agent(bgl_update_ptr);
		_start_agent(bgl_update_ptr);
	}
	xfree(cur_part_owner);
}
	
/* Perform job initiation work */
static void _start_agent(bgl_update_t *bgl_update_ptr)
{
	int rc;

	rc = _set_part_owner(bgl_update_ptr->bgl_part_id,
		uid_to_string(bgl_update_ptr->uid));
	if (rc == SLURM_SUCCESS)
		rc = _boot_part(bgl_update_ptr->bgl_part_id);
	
	if (rc != SLURM_SUCCESS) {
		sleep(2);	/* wait for the slurmd to begin the batch 
				 * script, slurm_fail_job() is a no-op if 
				 * issued prior to the script initiation */
		(void) slurm_fail_job(bgl_update_ptr->job_id);
	}
}

/* Perform job termination work */
static void _term_agent(bgl_update_t *bgl_update_ptr)
{
	int i, jobs, rc;
	rm_job_list_t *job_list;
	int live_states;

	live_states = JOB_ALL_FLAG & (~RM_JOB_TERMINATED) & (~RM_JOB_KILLED);
	if ((rc = rm_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("rm_get_jobs(): %s", bgl_err_str(rc));
		return;
	}

	if ((rc = rm_get_data(job_list, RM_JobListSize, &jobs)) != STATUS_OK) {
		error("rm_get_data(RM_JobListSize): %s", bgl_err_str(rc));
		jobs = 0;
	} else if (jobs > 128)
		fatal("Active job count (%d) invalid, restart DB2", jobs);
		
	for (i=0; i<jobs; i++) {
		rm_element_t *job_elem;
		pm_partition_id_t part_id;
		db_job_id_t job_id;
		
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
			error("No Job Elem breaking out job count = %d\n", jobs);
			break;
		}
		if ((rc = rm_get_data(job_elem, RM_JobPartitionID, &part_id))
				!= STATUS_OK) {
			error("rm_get_data(RM_JobPartitionID) %s: %s", 
				part_id, bgl_err_str(rc));
			continue;
		}

		if (strcmp(part_id, bgl_update_ptr->bgl_part_id) != 0)
			continue;
		if ((rc = rm_get_data(job_elem, RM_JobDBJobID, &job_id))
				!= STATUS_OK) {
			error("rm_get_data(RM_JobDBJobID): %s", 
				bgl_err_str(rc));
			continue;
		}
		(void) _remove_job(job_id);
	}

	/* Free the partition */
	bgl_free_partition(bgl_update_ptr->bgl_part_id);

	/* Change the block's owner */
	_set_part_owner(bgl_update_ptr->bgl_part_id, USER_NAME);

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
	agent_cnt = 0;
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

	if (list_enqueue(bgl_update_list, bgl_update_ptr) == NULL)
		fatal("malloc failure in _part_op/list_enqueue");
	if (agent_cnt > 0) {	/* already running an agent */
		slurm_mutex_unlock(&agent_cnt_mutex);
		return;
	}
	agent_cnt = 1;
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

static void _block_list_elem_del(void *x)
{
	bgl_record_t *block_ptr = (bgl_record_t *)x;
		
	xfree(block_ptr->nodes);
	xfree(block_ptr->bgl_part_id);
	xfree(block_ptr);
}

/* get a list of all BGL blocks with owners */
static List _get_all_blocks(void)
{
	List ret_list = list_create(_block_list_elem_del);
	ListIterator itr;
	bgl_record_t *block_ptr;
	bgl_record_t *str_ptr;
	
	if (!ret_list)
		fatal("malloc error");

	itr = list_iterator_create(bgl_list);
	while ((block_ptr = (bgl_record_t *) list_next(itr))) {
		if ((block_ptr->owner_name == NULL)
		||  (block_ptr->owner_name[0] == '\0')
		||  (block_ptr->bgl_part_id == NULL)
		||  (block_ptr->bgl_part_id[0] == '0'))
			continue;
		str_ptr = xmalloc(sizeof(bgl_record_t));
		str_ptr->bgl_part_id = xstrdup(block_ptr->bgl_part_id);
		str_ptr->nodes = xstrdup(block_ptr->nodes);
		
		list_append(ret_list, str_ptr);
	}
	list_iterator_destroy(itr);

	return ret_list;
}

/* remove a BGL block from the given list */
static int _excise_block(List block_list, pm_partition_id_t bgl_part_id,
		char *nodes)
{
	int rc = SLURM_SUCCESS;
	ListIterator iter = list_iterator_create(block_list);
	bgl_record_t *block;
	xassert(iter);

	while ((block = list_next(iter))) {
		rc = SLURM_ERROR;
		if (strcmp(block->bgl_part_id, bgl_part_id))
			continue;
		if (strcmp(block->nodes, nodes)) {	/* changed bglblock */
			error("bgl_part_id:%s old_nodes:%s new_nodes:%s",
				bgl_part_id, nodes, block->nodes);
			break;
		}

		/* exact match of name and node list */
		rc = SLURM_SUCCESS;
		break;
	}

	list_iterator_destroy(iter);
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
	bgl_update_t *bgl_update_ptr = xmalloc(sizeof(bgl_update_t));

	bgl_update_ptr->op = START_OP;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->job_id = job_ptr->job_id;
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_PART_ID, &(bgl_update_ptr->bgl_part_id));
	info("Queue start of job %u in BGL partition %s",
		job_ptr->job_id, bgl_update_ptr->bgl_part_id);
	_part_op(bgl_update_ptr);
#endif
	return rc;
}

#ifdef HAVE_BGL_FILES
/*
 * Perform any work required to terminate a jobs on a partition
 * bgl_part_id IN - partition name
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens when new partitions are created and we 
 * need to clean up jobs on them.
 */
int term_jobs_on_part(pm_partition_id_t bgl_part_id)
{
	int rc = SLURM_SUCCESS;
	bgl_update_t *bgl_update_ptr;
	if (bgl_update_list == NULL) {
		debug("No jobs started that I know about");
		return rc;
	}
	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->op = TERM_OP;
	bgl_update_ptr->bgl_part_id = xstrdup(bgl_part_id);
	_part_op(bgl_update_ptr);
	
	return rc;
}
#endif

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
	bgl_update_t *bgl_update_ptr = xmalloc(sizeof(bgl_update_t));

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
 * This can recover from slurmctld crashes when partition ownership
 * changes were queued
 */
extern int sync_jobs(List job_list)
{
#ifdef HAVE_BGL_FILES
#if KILL_PARTS_ON_REBOOT
	static int have_run = 0;
#endif
	ListIterator job_iterator, block_iterator;
	struct job_record  *job_ptr;
	pm_partition_id_t bgl_part_id;
	bgl_update_t *bgl_update_ptr;
	List block_list = _get_all_blocks();

#if KILL_PARTS_ON_REBOOT
	if (have_run)
		return SLURM_SUCCESS;
	have_run = 1;
#endif

	/* Insure that all running jobs own the specified partition */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool good_block = true;
		if (job_ptr->job_state != JOB_RUNNING)
			continue;

		bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
		select_g_get_jobinfo(job_ptr->select_jobinfo,
				     SELECT_DATA_PART_ID, &(bgl_update_ptr->bgl_part_id));

		if (bgl_update_ptr->bgl_part_id == NULL) {
			error("Running job %u has bglblock==NULL", 
				job_ptr->job_id);
			good_block = false;
		} else if (job_ptr->nodes == NULL) {
			error("Running job %u has nodes==NULL",
				job_ptr->job_id);
			good_block = false;
#if KILL_PARTS_ON_REBOOT
		} else if (1) {
			info("Running job %u being killed", job_ptr->job_id);
			good_block = false;
#endif
		} else if (_excise_block(block_list, bgl_update_ptr->
					 bgl_part_id, job_ptr->nodes) != SLURM_SUCCESS) {
			error("Kill job %u belongs to defunct bglblock %s",
			      job_ptr->job_id, bgl_update_ptr->bgl_part_id);
			good_block = false;
		}
		if (!good_block) {
			job_ptr->job_state = JOB_FAILED | JOB_COMPLETING;
			xfree(bgl_update_ptr->bgl_part_id);
			xfree(bgl_update_ptr);
			continue;
		}

		debug3("Queue sync of job %u in BGL partition %s",
			job_ptr->job_id, bgl_update_ptr->bgl_part_id);
		bgl_update_ptr->op = SYNC_OP;
		bgl_update_ptr->uid = job_ptr->user_id;
		bgl_update_ptr->job_id = job_ptr->job_id;
		_part_op(bgl_update_ptr);
	}
	list_iterator_destroy(job_iterator);

	/* Insure that all other partitions are free */
	block_iterator = list_iterator_create(block_list);
	while ((bgl_part_id = (pm_partition_id_t) list_next(block_iterator))) {
		debug3("Queue clearing of vestigial owner in BGL partition %s",
			bgl_part_id);
		bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
		bgl_update_ptr->op = TERM_OP;
		bgl_update_ptr->bgl_part_id = xstrdup(bgl_part_id);
		_part_op(bgl_update_ptr);
	}
	list_iterator_destroy(block_iterator);
	list_destroy(block_list);
	
#endif
	return SLURM_SUCCESS;
}

