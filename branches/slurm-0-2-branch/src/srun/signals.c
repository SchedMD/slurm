/*****************************************************************************\
 * src/srun/signals.c - signal handling for srun
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, and
 *             Moe Jette     <jette@llnl.gov>
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
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/srun/job.h"
#include "src/srun/io.h"

/*
 *  Static list of signals to block in srun:
 */
static int srun_sigarray[] = {
	SIGINT,  SIGQUIT, SIGTSTP, SIGCONT, 
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0
};

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
	job_t *job_ptr;
	int host_inx;
} task_info_t;


/* 
 * Static prototypes
 */
static void   _sigterm_handler(int);
static void   _handle_intr(job_t *, time_t *, time_t *); 
static void * _sig_thr(void *);
static void   _p_fwd_signal(slurm_msg_t *, job_t *);
static void * _p_signal_task(void *);


static inline bool 
_sig_thr_done(job_t *job)
{
	bool retval;
	slurm_mutex_lock(&job->state_mutex);
	retval = (job->state >= SRUN_JOB_DONE);
	slurm_mutex_unlock(&job->state_mutex);
	return retval;
}

int 
sig_setup_sigmask(void)
{
	if (xsignal_block(srun_sigarray) < 0)
		return SLURM_ERROR;

	xsignal(SIGTERM, &_sigterm_handler);
	xsignal(SIGHUP,  &_sigterm_handler);

	return SLURM_SUCCESS;
}

int 
sig_unblock_signals(void)
{
	return xsignal_unblock(srun_sigarray);
}

int 
sig_thr_create(job_t *job)
{
	int e;
	pthread_attr_t attr;

	pthread_attr_init(&attr);

	if ((e = pthread_create(&job->sigid, &attr, &_sig_thr, job)) != 0)
		slurm_seterrno_ret(e);

	debug("Started signals thread (%lu)", (unsigned long) job->sigid);

	return SLURM_SUCCESS;
}


void 
fwd_signal(job_t *job, int signo)
{
	int i;
	slurm_msg_t *req;
	kill_tasks_msg_t msg;
	static pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;

	slurm_mutex_lock(&sig_mutex);

	if (signo == SIGKILL || signo == SIGINT || signo == SIGTERM) {
		slurm_mutex_lock(&job->state_mutex);
		job->signaled = true;
		slurm_mutex_unlock(&job->state_mutex);
	}

	debug2("forward signal %d to job", signo);

	/* common to all tasks */
	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;

	req = xmalloc(sizeof(slurm_msg_t) * job->nhosts);

	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			debug2("%s has not yet replied\n", job->host[i]);
			continue;
		}

		if (job_active_tasks_on_host(job, i) == 0)
			continue;

		req[i].msg_type = REQUEST_KILL_TASKS;
		req[i].data     = &msg;
		memcpy( &req[i].address, 
		        &job->slurmd_addr[i], sizeof(slurm_addr));
	}

	_p_fwd_signal(req, job);

	debug2("All tasks have been signalled");
	xfree(req);
	slurm_mutex_unlock(&sig_mutex);
}


static void
_sigterm_handler(int signum)
{
	if (signum == SIGTERM) {
		pthread_exit(0);
	}
}

static void
_handle_intr(job_t *job, time_t *last_intr, time_t *last_intr_sent)
{
	if (opt.quit_on_intr) {
		job_force_termination(job);
		pthread_exit (0);
	}

	if ((time(NULL) - *last_intr) > 1) {
		info("interrupt (one more within 1 sec to abort)");
		if (mode != MODE_ATTACH)
			report_task_status(job);
		*last_intr = time(NULL);
	} else  { /* second Ctrl-C in half as many seconds */

		/* terminate job */
		if (job->state < SRUN_JOB_FORCETERM) {

			if ((time(NULL) - *last_intr_sent) < 1) {
				job_force_termination(job);
				pthread_exit(0);
			}

			info("sending Ctrl-C to job");
			*last_intr_sent = time(NULL);
			fwd_signal(job, SIGINT);

		} else {
			job_force_termination(job);
		}
	}
}

/* simple signal handling thread */
static void *
_sig_thr(void *arg)
{
	job_t *job = (job_t *)arg;
	sigset_t set;
	time_t last_intr      = 0;
	time_t last_intr_sent = 0;
	int signo;

	while (!_sig_thr_done(job)) {

		xsignal_sigset_create(srun_sigarray, &set);

		sigwait(&set, &signo);
		debug2("recvd signal %d", signo);
		switch (signo) {
		  case SIGINT:
			  _handle_intr(job, &last_intr, &last_intr_sent);
			  break;
		  case SIGTSTP:
			debug3("got SIGTSTP");
			break;
		  case SIGCONT:
			debug3("got SIGCONT");
			break;
		  case SIGQUIT:
			info("Quit");
			job_force_termination(job);
			break;
		  default:
			fwd_signal(job, signo);
			break;
		}
	}

	pthread_exit(0);
}

/* _p_fwd_signal - parallel (multi-threaded) task signaller */
static void _p_fwd_signal(slurm_msg_t *req, job_t *job)
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

		if ((errno = pthread_attr_init(&thd[i].attr)))
			error("pthread_attr_init failed");

		if (pthread_attr_setdetachstate(&thd[i].attr, 
		                                PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate failed");

#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope(&thd[i].attr, PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope failed");
#endif
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
	job_t       *job  = info->job_ptr;
	char        *host = job->host[info->host_inx];

	debug3("sending signal to host %s", host);
	if (slurm_send_recv_rc_msg(req, &rc, 0) < 0) { 
		error("%s: signal: %m", host);
		goto done;
	}

	/*
	 *  Report error unless it is "Invalid job id" which 
	 *    probably just means the tasks exited in the meanwhile.
	 */
	if ((rc != 0) && (rc != ESLURM_INVALID_JOB_ID) && (rc != ESRCH)) 
		error("%s: signal: %s", host, slurm_strerror(rc));

    done:
	slurm_mutex_lock(&active_mutex);
	active--;
	pthread_cond_signal(&active_cond);
	slurm_mutex_unlock(&active_mutex);
	xfree(args);
	return NULL;
}


