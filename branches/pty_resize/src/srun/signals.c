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
#include <sys/ioctl.h>
#include <sys/poll.h>

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
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, SIGWINCH, 0
};

/*
 *  Static list of signals to process by _sig_thr().
 *  NOTE: sigwait() does not work with SIGWINCH on
 *  on some operating systems (lots of references
 *  to bug this on the web).
 */
static int srun_sigarray2[] = {
	SIGINT,  SIGQUIT, /*SIGTSTP,*/ SIGCONT, SIGTERM,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0
};

/*  Processed by pty_thr() */
static int pty_sigarray[] = { SIGWINCH, 0 };
static int winch;

/* 
 * Static prototypes
 */
static void   _sigterm_handler(int);
static void   _handle_intr(srun_job_t *, time_t *, time_t *); 
static void   _handle_sigwinch(int sig);
static void * _pty_thread(void *arg);
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

		xsignal_sigset_create(srun_sigarray2, &set);

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

void set_winsize(srun_job_t *job)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws))
		error("ioctl(TIOCGWINSZ): %m");
	else {
		job->ws_row = ws.ws_row;
		job->ws_col = ws.ws_col;
		info("winsize %u:%u", job->ws_row, job->ws_col);
	}
	return;
}

/* SIGWINCH should already be blocked by srun/signal.c */
void block_sigwinch(void)
{
	xsignal_block(pty_sigarray);
}

void pty_thread_create(srun_job_t *job)
{
	slurm_addr pty_addr;
	pthread_attr_t attr;

	if ((job->pty_fd = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		return;
	}
	if (slurm_get_stream_addr(job->pty_fd, &pty_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		return;
	}
	job->pty_port = ntohs(((struct sockaddr_in) pty_addr).sin_port);
	info("initialized job control port %hu\n", job->pty_port);

	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if ((pthread_create(&job->pty_id, &attr, &_pty_thread, (void *) job)))
		error("pthread_create(pty_thread): %m");
	slurm_attr_destroy(&attr);
}

static void  _handle_sigwinch(int sig)
{
	winch = 1;
	xsignal(SIGWINCH, _handle_sigwinch);
}

static void _notify_winsize_change(int fd, srun_job_t *job)
{
	pty_winsz_t winsz;
	int len;

	winsz.cols = htons(job->ws_col);
	winsz.rows = htons(job->ws_row);

	if (fd < 0) {
		error("pty: no file to write window size changes to");
		return;
	}
	len = write(fd, &winsz, sizeof(winsz));
	if (len < sizeof(winsz))
		error("pty: window size change notification error: %m");
}

static void *_pty_thread(void *arg)
{
	int fd = -1;
	srun_job_t *job = (srun_job_t *) arg;
	slurm_addr client_addr;

	xsignal_unblock(pty_sigarray);
	xsignal(SIGWINCH, _handle_sigwinch);

	if ((fd = slurm_accept_msg_conn(job->pty_fd, &client_addr)) < 0) {
		error("pty: accept failure: %m");
		return NULL;
	}

	while (job->state <= SRUN_JOB_RUNNING) {
		info("waiting for SIGWINCH");
		poll(NULL, 0, -1);
		if (winch) {
			set_winsize(job);
			_notify_winsize_change(fd, job);
		}
		winch = 0;
	}
	return NULL;
}


