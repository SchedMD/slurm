/*****************************************************************************\
 * src/common/global_srun.c - functions needed by more than just srun
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, and
 *             Morris Jette  <jette1@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_PTHREAD
#include <pthread.h>
#endif

#include <signal.h>
#include <string.h>

#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/global_srun.h"

/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
} thd_t;

typedef struct task_info {
	slurm_msg_t *req_ptr;
	srun_job_t *job_ptr;
	int host_inx;
} task_info_t;

int message_thread = 0;

/* 
 * Static prototypes
 */
static void   _p_fwd_signal(slurm_msg_t *, srun_job_t *);
static void * _p_signal_task(void *);

void 
fwd_signal(srun_job_t *job, int signo)
{
	int i;
	slurm_msg_t *req;
	kill_tasks_msg_t msg;
	static pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
	pipe_enum_t pipe_enum = PIPE_SIGNALED;
	
	slurm_mutex_lock(&sig_mutex);

	if (signo == SIGKILL || signo == SIGINT || signo == SIGTERM) {
		slurm_mutex_lock(&job->state_mutex);
		job->signaled = true;
		slurm_mutex_unlock(&job->state_mutex);
		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->signaled,sizeof(int));
		}
	}

	debug2("forward signal %d to job", signo);

	/* common to all tasks */
	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;

	req = xmalloc(sizeof(slurm_msg_t) * job->nhosts);

	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			debug2("%s has not yet replied\n", 
			       job->step_layout->host[i]);
			continue;
		}
		if (job_active_tasks_on_host(job, i) == 0)
			continue;

		req[i].msg_type = REQUEST_SIGNAL_TASKS;
		req[i].data     = &msg;
		memcpy( &req[i].address, 
		        &job->slurmd_addr[i], sizeof(slurm_addr));
	}

	_p_fwd_signal(req, job);

	debug2("All tasks have been signalled");
	xfree(req);
	slurm_mutex_unlock(&sig_mutex);
}

int
job_active_tasks_on_host(srun_job_t *job, int hostid)
{
	int i;
	int retval = 0;

	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->step_layout->tasks[hostid]; i++) {
		uint32_t *tids = job->step_layout->tids[hostid];
		xassert(tids != NULL);
		debug("Task %d state: %d", tids[i], job->task_state[tids[i]]);
		if (job->task_state[tids[i]] == SRUN_TASK_RUNNING) 
			retval++;
	}
	slurm_mutex_unlock(&job->task_mutex);
	return retval;
}

/* _p_fwd_signal - parallel (multi-threaded) task signaller */
static void _p_fwd_signal(slurm_msg_t *req, srun_job_t *job)
{
	int i;
	task_info_t *tinfo;
	thd_t *thd;

	thd = xmalloc(job->nhosts * sizeof (thd_t));
	for (i = 0; i < job->nhosts; i++) {
		if (req[i].msg_type == 0)
			continue;	/* inactive task */

		slurm_mutex_lock(&active_mutex);
		while (active >= opt.max_threads) {
			pthread_cond_wait(&active_cond, &active_mutex);
		}
		active++;
		slurm_mutex_unlock(&active_mutex);

		tinfo = (task_info_t *)xmalloc(sizeof(task_info_t));
		tinfo->req_ptr  = &req[i];
		tinfo->job_ptr  = job;
		tinfo->host_inx = i;
		slurm_attr_init(&thd[i].attr);
		if (pthread_attr_setdetachstate(&thd[i].attr, 
		                                PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate failed");
		if (pthread_create( &thd[i].thread, &thd[i].attr, 
			            _p_signal_task, (void *) tinfo )) {
			error ("pthread_create failed");
			_p_signal_task((void *) tinfo);
		}
	}


	slurm_mutex_lock(&active_mutex);
	while (active > 0) {
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	slurm_mutex_unlock(&active_mutex);
	xfree(thd);
}

/* _p_signal_task - parallelized signal of a specific task */
static void * _p_signal_task(void *args)
{
	int          rc   = SLURM_SUCCESS;
	task_info_t *info = (task_info_t *)args;
	slurm_msg_t *req  = info->req_ptr;
	srun_job_t  *job  = info->job_ptr;
	char        *host = NULL;
	List ret_list = NULL;
	ListIterator itr;
	ret_types_t *ret_type = NULL;
	ret_data_info_t *ret_data_info = NULL;

	debug3("sending signal to host %s", host);
	
	if ((ret_list = slurm_send_recv_rc_msg(req, 0)) == NULL) { 
		errno = SLURM_SOCKET_ERROR;
		error("%s: signal: %m", host);
		goto done;
	}

	itr = list_iterator_create(ret_list);		
	while((ret_type = list_next(itr)) != NULL) {
		rc = ret_type->msg_rc;
		if(!ret_type->ret_data_list)
			host = job->step_layout->host[info->host_inx];
		else 
			host = NULL;
		/*
		 *  Report error unless it is "Invalid job id" which 
		 *    probably just means the tasks exited in the meanwhile.
		 */
		if ((rc != 0) && (rc != ESLURM_INVALID_JOB_ID)
		    &&  (rc != ESLURMD_JOB_NOTRUNNING) && (rc != ESRCH)) {
			if(!host) {
				while(ret_data_info 
				      = list_pop(ret_type->ret_data_list)) {
					error("%s: signal: %s", 
					      ret_data_info->node_name, 
					      slurm_strerror(rc));
					destroy_data_info(ret_data_info);
				}
			} else {
				error("%s: signal: %s", 
				      host, 
				      slurm_strerror(rc));
			}
		}
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);

    done:
	slurm_mutex_lock(&active_mutex);
	active--;
	pthread_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
	xfree(args);
	return NULL;
}


