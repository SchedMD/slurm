/*****************************************************************************\
 * src/srun/job.h - specification of an srun "job"
 * $Id$
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
#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <netinet/in.h>

#include <slurm/slurm.h>

#include "src/common/cbuf.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/srun/fname.h"

typedef enum {
	SRUN_JOB_INIT = 0,         /* Job's initial state                   */
	SRUN_JOB_LAUNCHING,        /* Launch thread is running              */
	SRUN_JOB_STARTING,         /* Launch thread is complete             */
	SRUN_JOB_RUNNING,          /* Launch thread complete                */
	SRUN_JOB_TERMINATING,      /* Once first task terminates            */
	SRUN_JOB_TERMINATED,       /* All tasks terminated (may have IO)    */
	SRUN_JOB_WAITING_ON_IO,    /* All tasks terminated; waiting for IO  */
	SRUN_JOB_FORCETERM,        /* Forced termination of IO thread       */
	SRUN_JOB_DONE,             /* tasks and IO complete                 */
	SRUN_JOB_DETACHED,         /* Detached IO from job (Not used now)   */
	SRUN_JOB_FAILED,           /* Job failed for some reason            */
} job_state_t;

typedef enum {
	SRUN_HOST_INIT = 0,
	SRUN_HOST_CONTACTED,
	SRUN_HOST_UNREACHABLE,
	SRUN_HOST_REPLIED
} host_state_t;

typedef enum {
	SRUN_TASK_INIT = 0,
	SRUN_TASK_RUNNING,
	SRUN_TASK_FAILED,
	SRUN_TASK_IO_WAIT,
	SRUN_TASK_EXITED,
	SRUN_TASK_ABNORMAL_EXIT
} task_state_t;


typedef struct srun_job {
	uint32_t jobid;		/* assigned job id 	                  */
	uint32_t stepid;	/* assigned step id 	                  */
	bool old_job;           /* run job step under previous allocation */
	bool removed;       /* job has been removed from SLURM */

	job_state_t state;	/* job state	   	                  */
	pthread_mutex_t state_mutex; 
	pthread_cond_t  state_cond;

	bool signaled;          /* True if user generated signal to job   */
	int  rc;                /* srun return code                       */

	slurm_cred_t  cred;     /* Slurm job credential    */
	char *nodelist;		/* nodelist in string form */
	int nhosts;
	char **host;		/* hostname vector */
	int *cpus; 		/* number of processors on each host */
	int *ntask; 		/* number of tasks to run on each host */
	uint32_t **tids;	/* host id => task ids mapping    */
	uint32_t *hostid;	/* task id => host id mapping     */

	slurm_addr *slurmd_addr;/* slurm_addr vector to slurmd's */

	pthread_t sigid;	/* signals thread tid		  */

	pthread_t jtid;		/* job control thread id 	  */
	int njfds;		/* number of job control info fds */
	slurm_fd *jfd;		/* job control info fd   	  */
	slurm_addr *jaddr;	/* job control info ports 	  */

	pthread_t ioid;		/* stdio thread id 		  */
	int niofds;		/* Number of IO fds  		  */
	int *iofd;		/* stdio listen fds 		  */
	int *ioport;		/* stdio listen ports 		  */

	int *out;		/* ntask stdout fds */
	int *err;		/* ntask stderr fds */

	/* XXX Need long term solution here:
	 * Quickfix: ntask*2 cbufs for buffering job output
	 */
	cbuf_t *outbuf;
	cbuf_t *errbuf;
	cbuf_t *inbuf;            /* buffer for stdin data */

	pthread_t lid;		  /* launch thread id */

	time_t    ltimeout;       /* Time by which all tasks must be running */
	time_t    etimeout;       /* exit timeout (see opt.max_wait          */

	host_state_t *host_state; /* nhost host states */

	int *tstatus;	          /* ntask exit statii */
	task_state_t *task_state; /* ntask task states */
	pthread_mutex_t task_mutex;

#ifdef HAVE_ELAN
	qsw_jobinfo_t qsw_job;
#endif
	io_filename_t *ifname;
	io_filename_t *ofname;
	io_filename_t *efname;

	/* Output streams and stdin fileno */
	FILE *outstream;
	FILE *errstream;
	int   stdinfd;
	bool *stdin_eof;  /* true if task i processed stdin eof */

} job_t;

void    update_job_state(job_t *job, job_state_t newstate);
void    job_force_termination(job_t *job);

job_state_t job_state(job_t *job);

job_t * job_create_noalloc(void);
job_t * job_create_allocation(resource_allocation_response_msg_t *resp);

/*
 *  Update job filenames and modes for stderr, stdout, and stdin.
 */
void    job_update_io_fnames(job_t *j);

/* 
 * Issue a fatal error message and terminate running job
 */
void    job_fatal(job_t *job, const char *msg);

/* 
 * Deallocates job and or job step via slurm API
 */
void    job_destroy(job_t *job, int error);

/* 
 * Send SIGKILL to running job via slurm controller
 */
void    job_kill(job_t *job);

/*
 * returns number of active tasks on host with id = hostid.
 */
int     job_active_tasks_on_host(job_t *job, int hostid);

/*
 * report current task status
 */
void    report_task_status(job_t *job);

/*
 * report current node status
 */
void    report_job_status(job_t *job);

/*
 * Returns job return code (for srun exit status)
 */
int    job_rc(job_t *job);

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
