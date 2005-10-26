/*****************************************************************************\
 *  src/slurmd/slurmd_job.h  slurmd_job_t definition
 *  $Id: job.h,v 1.29 2005/06/24 18:08:30 da Exp $
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

#ifndef _JOB_H
#define _JOB_H

#if WITH_PTHREADS
#include <pthread.h>
#endif

#include <pwd.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/list.h"
#include "src/common/eio.h"
#include "src/common/switch.h"
#include "src/common/env.h"


#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

typedef struct srun_key {
	unsigned char data[SLURM_IO_KEY_SIZE];
} srun_key_t;

typedef struct srun_info {
	srun_key_t *key;	   /* srun key for IO verification         */
	slurm_addr resp_addr;	   /* response addr for task exit msg      */
	slurm_addr ioaddr;         /* Address to connect on for I/O        */
	char *	   ofname;         /* output file (if any)                 */
	char *	   efname;         /* error file  (if any)	           */
	char *     ifname;         /* input file  (if any) 		   */

} srun_info_t;

typedef enum task_state {
	SLURMD_TASK_INIT,
	SLURMD_TASK_STARTING,
	SLURMD_TASK_RUNNING,
	SLURMD_TASK_COMPLETE
} slurmd_task_state_t;

/* local job states */
typedef enum job_state {
	SLURMD_JOB_UNUSED = 0,
	SLURMD_JOB_ALLOCATED,
	SLURMD_JOB_STARTING,
	SLURMD_JOB_STARTED,
	SLURMD_JOB_ENDING,
	SLURMD_JOB_COMPLETE
} slurmd_job_state_t;

typedef struct task_info {
	pthread_mutex_t mutex;	   /* mutex to protect task state          */
	slurmd_task_state_t    state; /* task state                        */
 
	int             id;	   /* local task id                        */
	uint32_t        gtid;	   /* global task id                       */
	pid_t           pid;	   /* task pid                             */
	int             pin[2];    /* stdin pipe                           */
	int             pout[2];   /* stdout pipe                          */
	int             perr[2];   /* stderr pipe                          */
	io_obj_t       *in, 
	               *out,       /* I/O objects used in IO event loop    */
		       *err;       

        bool            esent;     /* true if exit status has been sent    */
	bool            exited;    /* true if task has exited              */
	int             estatus;   /* this task's exit status              */

	List            srun_list; /* List of srun objs for this task      */
} slurmd_task_info_t;

typedef struct slurmd_job {
	uint32_t       jobid;  /* Current SLURM job id                      */
	uint32_t       stepid; /* Current step id (or NO_VAL)               */
	uint32_t       nnodes; /* number of nodes in current job            */
	uint32_t       nprocs; /* total number of processes in current job  */
	uint32_t       nodeid; /* relative position of this node in job     */
	uint32_t       ntasks; /* number of tasks on *this* node            */
	uint32_t       debug;  /* debug level for job slurmd                */
	uint16_t       cpus;   /* number of cpus to use for this job        */
	uint16_t       argc;   /* number of commandline arguments           */
	char         **env;    /* job environment                           */
	char         **argv;   /* job argument vector                       */
	char          *cwd;    /* path to current working directory         */
	switch_jobinfo_t switch_job; /* switch-specific job information     */
	uid_t         uid;     /* user id for job                           */
	gid_t         gid;     /* group ID for job                          */

	bool           batch;      /* true if this is a batch job           */
	bool           run_prolog; /* true if need to run prolog            */
	bool           spawn_task; /* stand-alone task */
	time_t         timelimit;  /* time at which job must stop           */

	struct passwd *pwd;   /* saved passwd struct for user job           */
	slurmd_task_info_t  **task;  /* list of task information pointers   */
	eio_t          eio;
	List           objs;  /* list of IO objects                         */
	List 	       sruns; /* List of sruns                              */

	pthread_t      ioid;  /* pthread id of IO thread                    */

	pid_t          jmgr_pid;     /* job manager pid                     */
	pid_t          pgid;         /* process group id for tasks          */

	uint16_t       task_flags; 
	env_t          *envtp;
	uint32_t       cont_id;
} slurmd_job_t;


slurmd_job_t * job_create(launch_tasks_request_msg_t *msg, slurm_addr *client);
slurmd_job_t * job_batch_job_create(batch_job_launch_msg_t *msg);
slurmd_job_t * job_spawn_create(spawn_task_request_msg_t *msg, slurm_addr *client);

void job_kill(slurmd_job_t *job, int signal);

void job_destroy(slurmd_job_t *job);

struct srun_info * srun_info_create(slurm_cred_t cred, slurm_addr *respaddr, 
		                    slurm_addr *ioaddr);

void  srun_info_destroy(struct srun_info *srun);

slurmd_task_info_t * task_info_create(int taskid, int gtaskid);

void task_info_destroy(slurmd_task_info_t *t);

int job_update_shm(slurmd_job_t *job);

int job_update_state(slurmd_job_t *job, slurmd_job_state_t s);

void job_delete_shm(slurmd_job_t *job);

#endif /* !_JOB_H */
