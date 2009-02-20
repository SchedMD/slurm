/*****************************************************************************\
 *  step_terminate_monitor.c - Run an external program if there are 
 *    unkillable processes at step termination.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/read_config.h"
#include "src/slurmd/slurmstepd/step_terminate_monitor.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int running_flag = 0;
static int stop_flag = 0;
static pthread_t tid;
static uint16_t timeout;
static char *program_name;
static uint32_t recorded_jobid = NO_VAL;
static uint32_t recorded_stepid = NO_VAL;

static void *monitor(void *);
static int call_external_program(void);

void step_terminate_monitor_start(uint32_t jobid, uint32_t stepid)
{
	slurm_ctl_conf_t *conf;
	pthread_attr_t attr;

	pthread_mutex_lock(&lock);

	if (running_flag) {
		pthread_mutex_unlock(&lock);
		return;
	}

	conf = slurm_conf_lock();
	if (conf->unkillable_program == NULL) {
		/* do nothing */
		slurm_conf_unlock();
		pthread_mutex_unlock(&lock);
		return;
	}
	timeout = conf->unkillable_timeout;
	program_name = xstrdup(conf->unkillable_program);
	slurm_conf_unlock();

	slurm_attr_init(&attr);
	pthread_create(&tid, &attr, monitor, NULL);
	slurm_attr_destroy(&attr);
	running_flag = 1;
	recorded_jobid = jobid;
	recorded_stepid = stepid;

	pthread_mutex_unlock(&lock);

	return;
}

void step_terminate_monitor_stop(void)
{
	pthread_mutex_lock(&lock);

	if (!running_flag) {
		pthread_mutex_unlock(&lock);
		return;
	}
	if (stop_flag) {
		error("step_terminate_monitor_stop: already stopped");
		pthread_mutex_unlock(&lock);
		return;
	}

	stop_flag = 1;
	debug("step_terminate_monitor_stop signalling condition");
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);

	if (pthread_join(tid, NULL) != 0) {
		error("step_terminate_monitor_stop: pthread_join: %m");
	}

	xfree(program_name);
	return;
}


static void *monitor(void *notused)
{
	struct timespec ts = {0, 0};
	int rc;

	info("monitor is running");

	ts.tv_sec = time(NULL) + 1 + timeout;

	pthread_mutex_lock(&lock);
	if (stop_flag)
		goto done;

	rc = pthread_cond_timedwait(&cond, &lock, &ts);
	if (rc == ETIMEDOUT) {
		call_external_program();
	} else if (rc != 0) {
		error("Error waiting on condition in monitor: %m");
	}
done:
	pthread_mutex_unlock(&lock);

	info("monitor is stopping");
	return NULL;
}


static int call_external_program(void)
{
	int status, rc, opt;
	pid_t cpid;
	int max_wait = 300; /* seconds */
	int time_remaining;

	debug("step_terminate_monitor: unkillable after %d sec, calling: %s",
	     timeout, program_name);

	if (program_name == NULL || program_name[0] == '\0')
		return 0;

	if (access(program_name, R_OK | X_OK) < 0) {
		debug("step_terminate_monitor not running %s: %m",
		      program_name);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error("step_terminate_monitor executing %s: fork: %m",
		      program_name);
		return -1;
	}
	if (cpid == 0) {
		/* child */
		char *argv[2];
		char buf[16];

		snprintf(buf, 16, "%u", recorded_jobid);
		setenv("SLURM_JOBID", buf, 1);
		setenv("SLURM_JOB_ID", buf, 1);
		snprintf(buf, 16, "%u", recorded_stepid);
		setenv("SLURM_STEPID", buf, 1);
		setenv("SLURM_STEP_ID", buf, 1);

		argv[0] = program_name;
		argv[1] = NULL;

#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execv(program_name, argv);
		error("step_terminate_monitor execv(): %m");
		exit(127);
	}

	opt = WNOHANG;
	time_remaining = max_wait;
	while (1) {
		rc = waitpid(cpid, &status, opt);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			/* waitpid may very well fail under normal conditions
			   because the wait3() in mgr.c:_wait_for_any_task()
			   may have reaped the return code. */
			return 0;
		} else if (rc == 0) {
			sleep(1);
			if ((--time_remaining) == 0) {
				error("step_terminate_monitor: %s still running"
				      " after %d seconds.  Killing.",
				      program_name, max_wait);
				killpg(cpid, SIGKILL);
				opt = 0;
			}
		} else  {
			return status;
		}
	}

	/* NOTREACHED */
}

