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

#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/uid.h"
#include "src/slurmctld/proc_req.h"
#include "bgl_job_run.h"
#include "bluegene.h"

#ifdef HAVE_BGL_FILES

#define MAX_POLL_RETRIES    30
#define MAX_PTHREAD_RETRIES  1
#define POLL_INTERVAL        2


typedef struct bgl_update {
	bool start;		/* true=start, false=terminate */
	uid_t uid;		/* new owner */
	uint32_t job_id;	/* SLURM job id */	
	pm_partition_id_t bgl_part_id;
} bgl_update_t;

List bgl_update_list = NULL;
static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static int agent_cnt = 0;

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
				error("rm_get_job(%d) errno=%d", job_id, rc);
			return rc;
		}

		if ((rc = rm_get_data(job_rec, RM_JobState, &job_state)) != 
				STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from DB2", job_id);
				rc = STATUS_OK;
			} else
				error("rm_get_data(RM_JobState) for jobid=%d "
					"errno=%d", job_id, rc);
			rm_free_job(job_rec);
			return rc;
		}
		rm_free_job(job_rec);

		/* Cancel or remove the job */
		if (job_state == RM_JOB_RUNNING)
			rc = jm_cancel_job(job_id);
		else
			rc = rm_remove_job(job_id);
		if (rc != STATUS_OK) {
			if (rc == JOB_NOT_FOUND) {
				debug("job %d removed from DB2", job_id);
				rc = STATUS_OK;
			} else if (job_state == RM_JOB_RUNNING)
				error("jm_cancel_job(%d) errno=%d", job_id, rc);
			else
				error("rm_remove_job(%d) errno=%d", job_id, rc);
			return rc;
		}
	}
	(void) rm_remove_job(job_id);
	error("Failed to remove job %d from DB2", job_id);
	return INTERNAL_ERROR;
}

/* Set the owner of an existing partition */
static int _set_part_owner(pm_partition_id_t bgl_part_id, char *user)
{
	int i, rc;
	rm_partition_t * part_elem;
	rm_partition_state_t part_state;

	if (user && user[0])
		info("Setting partition %s owner to %s", bgl_part_id, user);
	else
		info("Clearing partition %s owner", bgl_part_id);

	/* Make partition state FREE */
	for (i=0; i<MAX_POLL_RETRIES; i++) {
		if (i > 0)
			sleep(POLL_INTERVAL);

		/* find the partition */
		if ((rc = rm_get_partition(bgl_part_id,  &part_elem)) != 
				STATUS_OK) {
			error("rm_get_partition(%s) errno=%d", bgl_part_id, rc);
			return rc;
		}

		/* find its state */
		rc = rm_get_data(part_elem, RM_PartitionState, &part_state);
		(void) rm_free_partition(part_elem);
		if (rc != STATUS_OK) {
			error("rm_get_data(RM_PartitionState) errno=%d", rc);
			return rc;
		}
		if (part_state == RM_PARTITION_FREE)
			break;
#ifdef USE_BGL_FILES
		if ((i == 0)
		&&  ((rc = pm_destroy_partition(bgl_part_id)) != STATUS_OK)) {
			error("pm_destroy_partition(%s) errno=%d", 
				bgl_part_id, rc);
			return rc;
		}
#else
		break;
#endif
	}

	if (part_state != RM_PARTITION_FREE) {
		error("Could not free partition %s", bgl_part_id);
		return INTERNAL_ERROR;
	}
#ifdef USE_BGL_FILES
	if ((rc = rm_set_part_owner(bgl_part_id, user)) != STATUS_OK)
		error("rm_set_part_owner(%s,%s) errno=%d", bgl_part_id, user);
#endif
	return rc;
}

/* Perform job initiation work */
static void _start_agent(bgl_update_t *bgl_update_ptr)
{
	if (_set_part_owner(bgl_update_ptr->bgl_part_id, 
			uid_to_string(bgl_update_ptr->uid)) != STATUS_OK) {
		error("Could not change partition owner to start jobid=%u",
			bgl_update_ptr->job_id);
		sleep(1);	/* wait for the slurmd to begin the 
				 * batch script, this is a no-op if 
				 * issued prior to the script initiation */
		(void) slurm_fail_job(bgl_update_ptr->job_id);
	}
}

/* Perform job termination work */
static void _term_agent(bgl_update_t *bgl_update_ptr)
{
	int jobs, rc;
	rm_job_list_t *job_list;
	int live_states;

	live_states = JOB_ALL_FLAG & (~RM_JOB_TERMINATED) & (~RM_JOB_KILLED);
	if ((rc = rm_get_jobs(live_states, &job_list)) != STATUS_OK) {
		error("rm_get_jobs() errno=%d", rc);
		return;
	}

	if ((rc = rm_get_data(job_list, RM_JobListSize, &jobs)) != STATUS_OK) {
		error("rm_get_data(RM_JobListSize) errno=%d", rc);
		return;
	}

#ifdef USE_BGL_FILES
	for (i=0; i<jobs; i++) {
		rm_elemment_t job_elem;
		pm_partition_id_t part_id;
		db_job_id_t job_id;
		if (i) {
			if ((rc = rm_get_data(job_list, RM_JobListNextPart, 
					&job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListNextPart) " 
					"errno=%d", rc);
				continue;
			}
		} else {
			if ((rc = rm_get_data(job_list, RM_JobListFirstPart, 
					&job_elem)) != STATUS_OK) {
				error("rm_get_data(RM_JobListFirstPart) "
					"errno=%d", rc);
				continue;
			}
		}
		if ((rc = rm_get_data(job_elem, RM_JobPartitionID, &part_id)
				!= STATUS_OK) {
			error("rm_get_data(RM_JobPartitionID) errno=%d", rc);
			continue;
		}
		if (strcmp(part_id, bgl_update_ptr->bgl_part_id) != 0)
			continue;
		if ((rc = rm_get_data(job_elem, RM_JobDBJobID, &job_id)
				!= STATUS_OK) {
			error("rm_get_data(RM_JobDBJobID) errno=%d", rc);
			continue;
		}
		(void) _remove_job(job_id);
	}
	if ((rc = rm_free_job_list(job_list)) != STATUS_OK)
		error("rm_free_job_list() errno=%d", rc);

#endif
	/* Change the block's owner */
	_set_part_owner(bgl_update_ptr->bgl_part_id, "");
}

/* Process requests off the bgl_update_list queue and exit when done */
static void *_part_agent(void *args)
{
	bgl_update_t *bgl_update_ptr;

	while (1) {
		slurm_mutex_lock(&agent_cnt_mutex);
		bgl_update_ptr = list_dequeue(bgl_update_list);
		if (!bgl_update_ptr) {
			agent_cnt = 0;
			slurm_mutex_unlock(&agent_cnt_mutex);
			return NULL;
		}
		slurm_mutex_unlock(&agent_cnt_mutex);
		if (bgl_update_ptr->start)
			_start_agent(bgl_update_ptr);
		else
			_term_agent(bgl_update_ptr);
		_bgl_list_del(bgl_update_ptr);
	}	
}

/* Perform an operation upon a BGL partition (block) for starting or 
 * terminating a job */
static void _part_op(bgl_update_t *bgl_update_ptr)
{
	slurm_mutex_lock(&agent_cnt_mutex);
	if (list_enqueue(bgl_update_list, bgl_update_ptr) == NULL)
		fatal("malloc failure in _part_op/list_enqueue");
	if (agent_cnt == 0) {
		/* spawn an agent */
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		int retries = 0;
		agent_cnt = 1;
		slurm_attr_init(&attr_agent);
		while (pthread_create(&thread_agent, &attr_agent, _part_agent,
				NULL)) {
			error("pthread_create error %m");
			if (++retries > MAX_PTHREAD_RETRIES)
				fatal("Can't create pthread");
			sleep(1);       /* sleep and retry */
		}
	}
	slurm_mutex_unlock(&agent_cnt_mutex);
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
	pm_partition_id_t bgl_part_id;
	bgl_update_t *bgl_update_ptr;

	select_g_get_jobinfo(job_ptr->select_jobinfo, 
		SELECT_DATA_PART_ID, &bgl_part_id);
	info("Queue start of job %u in BGL partition %s", 
		job_ptr->job_id, bgl_part_id);

	if ((bgl_update_list == NULL)
	&&  ((bgl_update_list = list_create(_bgl_list_del)) == NULL)) {
		fatal("malloc failure in start_job/list_create");
		return SLURM_ERROR;
	}

	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->start = true;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->job_id = job_ptr->job_id;
	bgl_update_ptr->bgl_part_id = bgl_part_id;
	_part_op(bgl_update_ptr);
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
extern int term_job(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BGL_FILES
	pm_partition_id_t bgl_part_id;
	bgl_update_t *bgl_update_ptr;

	/* Identify the BGL block */
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_PART_ID, &bgl_part_id);
	info("Queue termination of job %u in BGL partition %s",
		job_ptr->job_id, bgl_part_id);

	bgl_update_ptr = xmalloc(sizeof(bgl_update_t));
	bgl_update_ptr->start = false;
	bgl_update_ptr->uid = job_ptr->user_id;
	bgl_update_ptr->job_id = job_ptr->job_id;
	bgl_update_ptr->bgl_part_id = bgl_part_id;
	_part_op(bgl_update_ptr);
#endif
	return rc;
}

