/*****************************************************************************\
 *  shmem_struct.c - shared memory support functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>

#include <src/common/slurm_errno.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/shmem_struct.h>

extern int errno;
static int shmem_gid;
#define SHMEM_PERMS 0600

/* function prototypes */
static void clear_task(task_t * task);
static void clear_job_step(job_step_t * job_step);
static int prepend_task(slurmd_shmem_t * shmem, job_step_t * job_step,
			task_t * task);

/* gets a pointer to the slurmd shared memory segment
 * if it doesn't exist, one is created 
 * returns - a void * pointer to the shared memory segment
 */
void *get_shmem()
{
	void *shmem_addr;
	int key = ftok(".", 'a');

	assert(key != SLURM_ERROR);

	shmem_gid = shmget(key, sizeof(slurmd_shmem_t), IPC_CREAT | SHMEM_PERMS);

	debug("shmget id = %i ", shmem_gid);
	if (shmem_gid == SLURM_ERROR) 
		fatal("can't get shared memory segment: %m ");

	shmem_addr = shmat(shmem_gid, NULL, 0);
	if (shmem_addr == (void *)SLURM_ERROR)
		fatal("Unable to attach to shared memory: %m");

	return shmem_addr;
}

int rel_shmem(void *shmem_addr)
{
	if ((shmdt(shmem_addr)) < 0)
		error("unable to release shared memory: %m");
	return shmctl(shmem_gid, IPC_RMID, NULL);
}

/* initializes the shared memory segment, this should only be called 
 * once by the master slurmd after the initial get_shmem call.
 *
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 */
void init_shmem(slurmd_shmem_t * shmem)
{
	int i;

	/* set everthing to zero */
	memset(shmem, 0, sizeof(slurmd_shmem_t));

	/* sanity check */
	/* set all task objects to unused */
	for (i = 0; i < MAX_TASKS; i++) {
		clear_task(&shmem->tasks[i]);
	}

	/* set all job_step objects to unused */
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		clear_job_step(&shmem->job_steps[i]);
	}
	pthread_mutex_init(&shmem->mutex, NULL);
}

/* runs through the job_step array looking for a unused job_step.
 * upon finding one the passed src job_step is copied into the shared mem job_step array
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * job_step_t - src job_step to be added to the shared memory list
 * returns - the address of the assigned job_step in the shared mem job_step array or
 * the function dies on a fatal log call if the array is full
 */
job_step_t *alloc_job_step(slurmd_shmem_t * shmem, int job_id,
			   int job_step_id)
{
	int i;
	pthread_mutex_lock(&shmem->mutex);
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (shmem->job_steps[i].used == false) {
			clear_job_step(&shmem->job_steps[i]);
			shmem->job_steps[i].used = true;
			shmem->job_steps[i].job_id = job_id;
			shmem->job_steps[i].job_step_id = job_step_id;
			pthread_mutex_unlock(&shmem->mutex);
			return &shmem->job_steps[i];
		}
	}
	pthread_mutex_unlock(&shmem->mutex);
	error("No available job_step slots in shmem segment");
	slurm_seterrno(ESLURMD_NO_AVAILABLE_JOB_STEP_SLOTS_IN_SHMEM);

	return (void *) SLURM_ERROR;
}

/* runs through the task array looking for a unused task.
 * upon finding one the passed src task is copied into the shared mem task array
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * new_task - src task to be added to the shared memory list
 * returns - the address of the assigned task in the shared mem task array
 * the function dies on a fatal log call if the array is full
 */
task_t *alloc_task(slurmd_shmem_t * shmem, job_step_t * job_step)
{
	int i;
	pthread_mutex_lock(&shmem->mutex);
	for (i = 0; i < MAX_TASKS; i++) {
		if (shmem->tasks[i].used == false) {
			clear_task(&shmem->tasks[i]);
			shmem->tasks[i].used = true;
			prepend_task(shmem, job_step, &shmem->tasks[i]);
			pthread_mutex_unlock(&shmem->mutex);
			return &shmem->tasks[i];
		}
	}
	pthread_mutex_unlock(&shmem->mutex);
	error("No available task slots in shmem segment");
	slurm_seterrno(ESLURMD_NO_AVAILABLE_TASK_SLOTS_IN_SHMEM);
	return (void *) SLURM_ERROR;
}


/* prepends a new task onto the front of a list of tasks assocuated with a job_step.
 * it calls add_task which copies the passed task into a task array in shared memoery
 * sets pointers from the task to the corresponding job_step array 
 * note if the task array is full,  the add_task function will assert and exiti
 * shmem - pointer to the shared memory segment returned by get_shmem ( )
 * job_step - job_step to receive the new task
 * task - task to be prepended
 */
static int prepend_task(slurmd_shmem_t * shmem, job_step_t * job_step,
			task_t * task)
{
	/* newtask next pointer gets head of the jobstep task list */
	task->next = job_step->head_task;

	/* newtask pointer becomes the new head of the jobstep task list */
	job_step->head_task = task;

	/* set back pointer from task to job_step */
	task->job_step = job_step;

	return SLURM_SUCCESS;
}

/* clears a job_step and associated task list for future use */
int deallocate_job_step(job_step_t * jobstep)
{
	task_t *task_ptr = jobstep->head_task;
	task_t *task_temp_ptr;
	while (task_ptr != NULL) {
		task_temp_ptr = task_ptr->next;
		clear_task(task_ptr);
		task_ptr = task_temp_ptr;
	}
	clear_job_step(jobstep);
	return SLURM_SUCCESS;
}

/* clears a task array member for future use 
 */
static void clear_task(task_t * task)
{
	task->used = false;
	task->job_step = NULL;
	task->next = NULL;
}

/* clears a job_step array memeber for future use 
 */
static void clear_job_step(job_step_t * job_step)
{
	job_step->used = false;
	job_step->head_task = NULL;
}

/* api call for DPCS to return a job_id given a session_id 
 */
int find_job_id_for_session(slurmd_shmem_t * shmem, int session_id)
{
	int i;
	pthread_mutex_lock(&shmem->mutex);
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (shmem->job_steps[i].used == true) {
			if (shmem->job_steps[i].session_id == session_id)

				pthread_mutex_unlock(&shmem->mutex);
			return shmem->job_steps[i].job_id;
		}
	}
	pthread_mutex_unlock(&shmem->mutex);
	debug("No job_id found for session_id %i", session_id);
	return SLURM_FAILURE;
}

job_step_t *find_job_step(slurmd_shmem_t * shmem, int job_id,
			  int job_step_id)
{
	int i;
	pthread_mutex_lock(&shmem->mutex);
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (shmem->job_steps[i].used == true
		    && shmem->job_steps[i].job_id == job_id
		    && shmem->job_steps[i].job_step_id == job_step_id) {
			debug3("found step %d.%d in slot %d", 
			       job_id, job_step_id, i);
			pthread_mutex_unlock(&shmem->mutex);
			return &shmem->job_steps[i];
		}
	}
	debug3("find_job_step: unable to find %d.%d", job_id, job_step_id);
	pthread_mutex_unlock(&shmem->mutex);
	return (void *) SLURM_ERROR;
}

task_t *find_task(job_step_t * job_step_ptr, int task_id)
{
	task_t *task_ptr = job_step_ptr->head_task;
	while (task_ptr != NULL) {
		if (task_ptr->task_id == task_id) {
			return task_ptr;
		}
	}
	return (void *) SLURM_ERROR;
}
