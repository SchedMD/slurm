/*****************************************************************************\
 *  src/srun/signals.c - signal handling for srun
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, and
 *             Morris Jette  <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
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
#include "src/common/xstring.h"
#include "src/common/xsignal.h"

#include "src/srun/opt.h"
#include "src/srun/srun_job.h"
#include "src/srun/signals.h"

#define MAX_RETRIES 3

/*
 *  Static list of signals to block in srun:
 */
static int srun_sigarray[] = {
	SIGINT,  SIGQUIT, /*SIGTSTP,*/ SIGCONT, SIGTERM,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0
};

/* 
 * Static prototypes
 */
static void   _sigterm_handler(int);
static void   _handle_intr(srun_job_t *, time_t *, time_t *); 
static void * _sig_thr(void *);

static inline bool 
_sig_thr_done(srun_job_t *job)
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

	xsignal(SIGHUP,  &_sigterm_handler);

	return SLURM_SUCCESS;
}

int 
sig_unblock_signals(void)
{
	return xsignal_unblock(srun_sigarray);
}

int 
sig_thr_create(srun_job_t *job)
{
	int e, retries = 0;
	pthread_attr_t attr;

	slurm_attr_init(&attr);

	while ((e = pthread_create(&job->sigid, &attr, &_sig_thr, job))) {
		if (++retries > MAX_RETRIES) {
			slurm_attr_destroy(&attr);
			slurm_seterrno_ret(e);
		}
		sleep(1);	/* sleep and try again */
	}
	slurm_attr_destroy(&attr);

	debug("Started signals thread (%lu)", (unsigned long) job->sigid);

	return SLURM_SUCCESS;
}


static void
_sigterm_handler(int signum)
{
}

static void
_handle_intr(srun_job_t *job, time_t *last_intr, time_t *last_intr_sent)
{
	if (opt.quit_on_intr) {
		job_force_termination(job);
		pthread_exit (0);
	}

	if (((time(NULL) - *last_intr) > 1) && !opt.disable_status) {
		info("interrupt (one more within 1 sec to abort)");
		if (mode != MODE_ATTACH)
			report_task_status(job);
		*last_intr = time(NULL);
	} else  { /* second Ctrl-C in half as many seconds */
		update_job_state(job, SRUN_JOB_CANCELLED);
		/* terminate job */
		if (job->state < SRUN_JOB_FORCETERM) {
			if ((time(NULL) - *last_intr_sent) < 1) {
				job_force_termination(job);
				pthread_exit(0);
			}

			info("sending Ctrl-C to job");
			*last_intr_sent = time(NULL);
			fwd_signal(job, SIGINT, opt.max_threads);

		} else {
			job_force_termination(job);
		}
	}
}

/* simple signal handling thread */
static void *
_sig_thr(void *arg)
{
	srun_job_t *job = (srun_job_t *)arg;
	sigset_t set;
	time_t last_intr      = 0;
	time_t last_intr_sent = 0;
	int signo, err;

	while (!_sig_thr_done(job)) {

		xsignal_sigset_create(srun_sigarray, &set);

		if ((err = sigwait(&set, &signo)) != 0) {
			if (err != EINTR) 
				error ("sigwait: %s", slurm_strerror (err));
			continue;
		}

		debug2("recvd signal %d", signo);
		switch (signo) {
		  case SIGINT:
			  _handle_intr(job, &last_intr, &last_intr_sent);
			  break;
		  /* case SIGTSTP: */
/* 			debug3("got SIGTSTP"); */
/* 			break; */
		  case SIGCONT:
			debug3("got SIGCONT");
			break;
		  case SIGQUIT:
			info("Quit");
			job_force_termination(job);
			break;
		  default:
			fwd_signal(job, signo, opt.max_threads);
			break;
		}
	}

	pthread_exit(0);
	return NULL;
}



