/*****************************************************************************\
 *  src/common/global_srun.c - functions needed by more than just srun
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grodnona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _GLOBAL_SRUN_H
#define _GLOBAL_SRUN_H

#include <slurm/slurm.h>
#include "src/common/slurm_protocol_common.h"
#include "src/api/step_io.h"

typedef enum {
	SRUN_JOB_INIT = 0,         /* Job's initial state                   */
	SRUN_JOB_LAUNCHING,        /* Launch thread is running              */
	SRUN_JOB_STARTING,         /* Launch thread is complete             */
	SRUN_JOB_RUNNING,          /* Launch thread complete                */
	SRUN_JOB_TERMINATING,      /* Once first task terminates            */
	SRUN_JOB_TERMINATED,       /* All tasks terminated (may have IO)    */
	SRUN_JOB_WAITING_ON_IO,    /* All tasks terminated; waiting for IO  */
	SRUN_JOB_DONE,             /* tasks and IO complete                 */
	SRUN_JOB_DETACHED,         /* Detached IO from job (Not used now)   */
	SRUN_JOB_FAILED,           /* Job failed for some reason            */
	SRUN_JOB_CANCELLED,        /* CTRL-C cancelled                      */
	SRUN_JOB_FORCETERM         /* Forced termination of IO thread       */
} srun_job_state_t;

typedef enum {
	SRUN_HOST_INIT = 0,
	SRUN_HOST_CONTACTED,
	SRUN_HOST_UNREACHABLE,
	SRUN_HOST_REPLIED
} srun_host_state_t;

typedef enum {
	SRUN_TASK_INIT = 0,
	SRUN_TASK_RUNNING,
	SRUN_TASK_FAILED,
	SRUN_TASK_IO_WAIT, /* this state deprecated with new eio stdio engine */
	SRUN_TASK_EXITED,
	SRUN_TASK_ABNORMAL_EXIT
} srun_task_state_t;

typedef enum { 
	PIPE_NONE = 0, 
	PIPE_JOB_STATE, 
	PIPE_TASK_STATE, 
	PIPE_TASK_EXITCODE,
	PIPE_HOST_STATE, 
	PIPE_SIGNALED,
	PIPE_MPIR_DEBUG_STATE,
	PIPE_UPDATE_MPIR_PROCTABLE,
	PIPE_UPDATE_STEP_LAYOUT
} pipe_enum_t;

/* For Message thread */
typedef struct forked_msg_pipe {
	int msg_pipe[2];
	int pid;
} forked_msg_pipe_t;

typedef struct forked_message {
	forked_msg_pipe_t *          par_msg;
	forked_msg_pipe_t *          msg_par;
	enum job_states	*	     job_state;
} forked_msg_t;

typedef struct io_filename io_filename_t;

typedef struct srun_job {
	slurm_step_layout_t *step_layout; /* holds info about how the task is 
					     laid out */
	uint32_t jobid;		/* assigned job id 	                  */
	uint32_t stepid;	/* assigned step id 	                  */
	bool old_job;           /* run job step under previous allocation */
	bool removed;       /* job has been removed from SLURM */

	uint32_t nhosts;	/* node count */
	uint32_t ntasks;	/* task count */
	srun_job_state_t state;	/* job state	   	                  */
	pthread_mutex_t state_mutex; 
	pthread_cond_t  state_cond;

	bool signaled;          /* True if user generated signal to job   */
	int  rc;                /* srun return code                       */

	slurm_cred_t  cred;     /* Slurm job credential    */
	char *nodelist;		/* nodelist in string form */

	pthread_t sigid;	/* signals thread tid		  */

	pthread_t jtid;		/* job control thread id 	  */
	slurm_fd *jfd;		/* job control info fd   	  */
	
	pthread_t lid;		  /* launch thread id */

	client_io_t *client_io;
	time_t    ltimeout;       /* Time by which all tasks must be running */
	time_t    etimeout;       /* exit timeout (see opt.max_wait          */

	srun_host_state_t *host_state; /* nhost host states */

	int *tstatus;	          /* ntask exit statii */
	srun_task_state_t *task_state; /* ntask task states */
	
	switch_jobinfo_t switch_job;
	io_filename_t *ifname;
	io_filename_t *ofname;
	io_filename_t *efname;
	forked_msg_t *forked_msg;
	char *task_epilog;	/* task-epilog */
	char *task_prolog;	/* task-prolog */
	pthread_mutex_t task_mutex;
	int njfds;		/* number of job control info fds */
	slurm_addr *jaddr;	/* job control info ports 	  */
	int thr_count;  	/* count of threads in job launch */

	/* Output streams and stdin fileno */
	select_jobinfo_t select_jobinfo;
	
} srun_job_t;


void fwd_signal(srun_job_t *job, int signal, int max_threads);
int job_active_tasks_on_host(srun_job_t *job, int hostid);

#endif /* !_GLOBAL_SRUN_H */
