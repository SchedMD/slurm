/*****************************************************************************\
 *  src/srun/srun_job.h - specification of an srun "job"
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <netinet/in.h>

#include <slurm/slurm.h>

#include "src/common/eio.h"
#include "src/common/cbuf.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/dist_tasks.h"
//#include "src/common/global_srun.h"

#include "src/srun/signals.h"
#include "src/srun/fname.h"

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

	slurm_addr *slurmd_addr;/* slurm_addr vector to slurmd's */

	pthread_t sigid;	/* signals thread tid		  */

	pthread_t jtid;		/* job control thread id 	  */
	slurm_fd *jfd;		/* job control info fd   	  */
	
	pthread_t ioid;		/* stdio thread id 		  */
	int *listensock;	/* Array of stdio listen sockets  */
	eio_handle_t *eio;      /* Event IO handle                */
	int ioservers_ready;    /* Number of servers that established contact */
	eio_obj_t **ioserver;	/* Array of nhosts pointers to eio_obj_t */
	eio_obj_t *stdin_obj;   /* stdin eio_obj_t                */
	eio_obj_t *stdout_obj;  /* stdout eio_obj_t               */
	eio_obj_t *stderr_obj;  /* stderr eio_obj_t               */
	List free_incoming;     /* List of free struct io_buf * for incoming
				 * traffic. "incoming" means traffic from srun
				 * to the tasks.
				 */
	List free_outgoing;     /* List of free struct io_buf * for outgoing
				 * traffic "outgoing" means traffic from the
				 * tasks to srun.
				 */
	int incoming_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */
	int outgoing_count;     /* Count of total incoming message buffers
			         * including free_incoming buffers and
			         * buffers in use.
			         */

	pthread_t lid;		  /* launch thread id */

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
	int num_listen;		/* Number of stdio listen sockets */
	int *listenport;	/* Array of stdio listen ports 	  */
	int thr_count;  	/* count of threads in job launch */

	/* Output streams and stdin fileno */
	select_jobinfo_t select_jobinfo;
	
} srun_job_t;

extern int message_thread;

void    update_job_state(srun_job_t *job, srun_job_state_t newstate);
void    job_force_termination(srun_job_t *job);

srun_job_state_t job_state(srun_job_t *job);

extern srun_job_t * job_create_noalloc(void);
extern srun_job_t * job_create_allocation(
	resource_allocation_response_msg_t *resp);
extern srun_job_t * job_create_structure(
	resource_allocation_response_msg_t *resp);

/*
 *  Update job filenames and modes for stderr, stdout, and stdin.
 */
void    job_update_io_fnames(srun_job_t *j);

/* 
 * Issue a fatal error message and terminate running job
 */
void    job_fatal(srun_job_t *job, const char *msg);

/* 
 * Deallocates job and or job step via slurm API
 */
void    srun_job_destroy(srun_job_t *job, int error);

/* 
 * Send SIGKILL to running job via slurm controller
 */
void    srun_job_kill(srun_job_t *job);

/*
 * report current task status
 */
void    report_task_status(srun_job_t *job);

/*
 * report current node status
 */
void    report_job_status(srun_job_t *job);

/*
 * Returns job return code (for srun exit status)
 */
int    job_rc(srun_job_t *job);

/*
 * To run a job step on existing allocation, modify the 
 * existing_allocation() response to remove nodes as needed 
 * for the job step request (for --excluded nodes or reduced 
 * --nodes count). This is a temporary fix for slurm 0.2.
 * resp IN/OUT - existing_allocation() response message
 * RET - zero or fatal error code
 */

int    job_resp_hack_for_step(resource_allocation_response_msg_t *resp);

#endif /* !_HAVE_JOB_H */
