/*****************************************************************************\
 *  src/slurmd/shm.h - shared memory routines for slurmd
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef _SHM_H
#define _SHM_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif  

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif /* HAVE_INTTYPES_H */

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif 

#include <sys/param.h>		/* MAXPATHLEN */

#include "src/common/slurm_protocol_api.h"
#include "src/common/list.h"
#include "src/slurmd/slurmd_job.h"

typedef struct task task_t;
typedef struct job_step job_step_t;


struct task {
	int used;
	int id;	        /* local task id              			*/
	int global_id;  /* global task id             			*/
	pid_t pid;	/* pid of user process        			*/
	pid_t ppid;	/* parent pid of user process 			*/

	/* reverse pointer back to controlling job step */
	job_step_t *job_step;
	task_t *next;	/* next task in this job step			*/
};

struct job_step {
	uid_t      uid;
	uint32_t   jobid;		
	uint32_t   stepid;
	uint32_t   sw_id;	/* Switch/Interconnect specific id       */
	int        ntasks;	/* number of tasks in this job	         */
	pid_t      mpid;        /* Job manager pid                       */
	pid_t      pgid;        /* Process group containing the tasks    */
	uint32_t   cont_id;	/* Job container id                      */

                                /* Executable's pathname                 */
	char       exec_name[MAXPATHLEN]; 

	int        io_update;	/* srun address has been updated         */
	slurm_addr respaddr;	/* Addr to send messages to srun on      */
	slurm_addr ioaddr;	/* Addr to connect to initialize IO      */
	srun_key_t key;		/* last key from srun client	         */


	slurmd_job_state_t state;	/* Job step status               */
	time_t      timelimit;	/* job time limit		         */
	task_t     *task_list;	/* list of this step's tasks             */
};


/* 
 * Attach to and initialize slurmd shared memory segment
 * startup (IN) - validate shared memory contents if true
 * Returns -1 and sets errno on failure.
 */
int shm_init(bool startup);

/*
 * Release slurmd shared memory segment. Deallocates segment if no
 * other processes are currently attached.
 */
int shm_fini(void);

/*
 * Force cleanup of any stale shared memory locks
 */
void shm_cleanup(void);

/*
 * Returns a list of job_step_t's currently recorded 
 * in shared memory. Presumably, these job steps are 
 * still running or have abnormally terminated. 
 *
 * Caller must free the resulting list with list_destroy()
 */
List shm_get_steps(void);

/* 
 * Try to determine whether the step {jobid,stepid} is still
 * running on this node. (i.e., check for sid and all pids)
 *
 * Returns false if job step sid and at least one pid do
 * not exist
 */
bool shm_step_still_running(uint32_t jobid, uint32_t stepid);

/*
 * Insert a new step into shared memory, the step passed in by address
 * should be filled in with the appropriate values, excepting the
 * task_list pointer (see add_task below to add tasks to a job step)
 * The resulting step will not be modified nor freed. The step information is
 * *copied* into shared memory
 *
 * Failure modes are:
 *   EEXIST: A step already exists in shared memory with that jobid,stepid
 *   ENOSPC: No step slots remain in shared memory
 */
int shm_insert_step(job_step_t *step);

/*
 * Delete the job step record from shared memory, if it exists
 *
 * Returns SLURM_FAILURE and sets errno if job step cannot be deleted
 *  ESRCH: Job step with jobid,stepid not found
 */
int shm_delete_step(uint32_t jobid, uint32_t stepid);

/*
 * Return a *copy* of the job step with jobid,stepid from shared
 * memory. The copy must be freed with xfree()
 *
 * Returns NULL if job step is not found in shared memory.
 */
job_step_t *shm_get_step(uint32_t jobid, uint32_t stepid);

/*
 * Return the uid that the given job step is running under.
 *
 * Returns (uid_t) -1 if the job step is not found
 */
uid_t shm_get_step_owner(uint32_t jobid, uint32_t stepid);

/*
 * Update an existing job step to match "step"
 * returns SLURM_FAILURE if job step cannot be found
 */
int shm_update_step(job_step_t *step);

/*
 * Deallocate memory used by step struct returned from shm_get_step()
 */
void shm_free_step(job_step_t *step);

/* 
 * Add a task record to a job step in memory
 *
 * Returns SLURM_FAILURE and following errnos if not successful:
 *   ESRCH: Cannot find job step
 *   EEXIST: A task with that id is already associated with job step
 *   ENOMEM: No more task slots available in shared memory
 */
int shm_add_task(uint32_t jobid, uint32_t stepid, task_t *task);


/*
 *  update job step container id
 */
int shm_update_step_cont_id(uint32_t jobid, uint32_t stepid, uint32_t cont_id);

/* 
 * update job step process group id
 */
int shm_update_step_pgid(uint32_t jobid, uint32_t stepid, int pgid);

/* 
 * update job step "manager" pid
 */
int shm_update_step_mpid(uint32_t jobid, uint32_t stepid, int mpid);


/* 
 * update job step "session manager" pid
 */
int shm_update_step_spid(uint32_t jobid, uint32_t stepid, int spid);


/*
 * update job step state 
 */
int shm_update_step_state(uint32_t jobid, 
			  uint32_t stepid, 
			  slurmd_job_state_t state);


/* 
 * lock and return _pointer_ to step state in shared memory
 * Caller must subsequently call shm_unlock_step_state() or shared memory
 *  will be locked for everyone else.
 * (Note: This function is different from most others in this module as
 *  it returns a pointer into the shared memory region instead of a copy
 *  of the data. Callers should remain cognizant of this fact. )
 */
slurmd_job_state_t *shm_lock_step_state(uint32_t jobid, uint32_t stepid);

/* unlock job step state
 */
void shm_unlock_step_state(uint32_t jobid, uint32_t stepid);

/* 
 * update job step io_addrs 
 */
int shm_update_step_addrs(uint32_t jobid, uint32_t stepid, 
		          slurm_addr *ioaddr, slurm_addr *respaddr,
			  char *keydata);

/* 
 * Return true if ioaddr was updated
 */
bool shm_addr_updated(uint32_t jobid, uint32_t stepid);


/* 
 * Atomically return current ioaddr and reset io_update field to false
 */
int shm_step_addrs(uint32_t jobid, uint32_t stepid, 
		   slurm_addr *ioaddr, slurm_addr *respaddr, srun_key_t *key);


/* 
 * update job timelimit for all steps associated with the specified jobid
 */
int shm_update_job_timelimit(uint32_t jobid, time_t newlim);


/* 
 * update job step timelimit
 */
int shm_update_step_timelimit(uint32_t jobid, uint32_t stepid, time_t newlim);

/*
 * Return count of free shm records
 */
extern int shm_free_steps(void);

/* 
 * Return job step timelimit
 */
time_t shm_step_timelimit(uint32_t jobid, uint32_t stepid);

#endif /* !_SHM_H */
