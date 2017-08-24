/*****************************************************************************\
 *  state_save.c - Keep saved slurmctld state current
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <pthread.h>

#include "src/common/macros.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"

/* Maximum delay for pending state save to be processed, in seconds */
#ifndef SAVE_MAX_WAIT
#define SAVE_MAX_WAIT	5
#endif

static pthread_mutex_t state_save_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_save_cond = PTHREAD_COND_INITIALIZER;
static int save_jobs = 0, save_nodes = 0, save_parts = 0;
static int save_front_end = 0, save_triggers = 0, save_resv = 0;
static bool run_save_thread = true;

/* fsync() and close() a file,
 * Execute fsync() and close() multiple times if necessary and log failures
 * RET 0 on success or -1 on error */
extern int fsync_and_close(int fd, char *file_type)
{
	int rc = 0, retval, pos;

	/* SLURM state save files are typically stored on shared filesystems,
	 * so lets give fysync() three tries to sync the data to disk. */
	for (retval = 1, pos = 1; retval && pos < 4; pos++) {
		retval = fsync(fd);
		if (retval && (errno != EINTR)) {
			error("fsync() error writing %s state save file: %m",
			      file_type);
		}
	}
	if (retval)
		rc = retval;

	for (retval = 1, pos = 1; retval && pos < 4; pos++) {
		retval = close(fd);
		if (retval && (errno != EINTR)) {
			error("close () error on %s state save file: %m",
			      file_type);
		}
	}
	if (retval)
		rc = retval;

	return rc;
}

/* Queue saving of front_end state information */
extern void schedule_front_end_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_front_end++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* Queue saving of job state information */
extern void schedule_job_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_jobs++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* Queue saving of node state information */
extern void schedule_node_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_nodes++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* Queue saving of partition state information */
extern void schedule_part_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_parts++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* Queue saving of reservation state information */
extern void schedule_resv_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_resv++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* Queue saving of trigger state information */
extern void schedule_trigger_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	save_triggers++;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/* shutdown the slurmctld_state_save thread */
extern void shutdown_state_save(void)
{
	slurm_mutex_lock(&state_save_lock);
	run_save_thread = false;
	slurm_cond_broadcast(&state_save_cond);
	slurm_mutex_unlock(&state_save_lock);
}

/*
 * Run as pthread to keep saving slurmctld state information as needed,
 * Use schedule_job_save(),  schedule_node_save(), and schedule_part_save()
 * to queue state save of each data structure
 * no_data IN - unused
 * RET - NULL
 */
extern void *slurmctld_state_save(void *no_data)
{
	time_t last_save = 0, now;
	double save_delay;
	bool run_save;
	int save_count;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "sstate", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "sstate");
	}
#endif

	while (1) {
		/* wait for work to perform */
		slurm_mutex_lock(&state_save_lock);
		while (1) {
			save_count = save_jobs + save_nodes + save_parts +
				     save_front_end + save_resv +
				     save_triggers;
			now = time(NULL);
			save_delay = difftime(now, last_save);
			if (save_count &&
			    (!run_save_thread ||
			     (save_delay >= SAVE_MAX_WAIT))) {
				last_save = now;
				break;		/* do the work */
			} else if (!run_save_thread) {
				run_save_thread = true;
				slurm_mutex_unlock(&state_save_lock);
				return NULL;	/* shutdown */
			} else if (save_count) { /* wait for a timeout */
				struct timespec ts = {0, 0};
				ts.tv_sec = now + 1;
				slurm_cond_timedwait(&state_save_cond,
					  	     &state_save_lock, &ts);
			} else {		/* wait for more work */
				slurm_cond_wait(&state_save_cond,
					  	&state_save_lock);
			}
		}

		/* save front_end node info if necessary */
		run_save = false;
		/* slurm_mutex_lock(&state_save_lock); done above */
		if (save_front_end) {
			run_save = true;
			save_front_end = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)dump_all_front_end_state();

		/* save job info if necessary */
		run_save = false;
		slurm_mutex_lock(&state_save_lock);
		if (save_jobs) {
			run_save = true;
			save_jobs = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)dump_all_job_state();

		/* save node info if necessary */
		run_save = false;
		slurm_mutex_lock(&state_save_lock);
		if (save_nodes) {
			run_save = true;
			save_nodes = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)dump_all_node_state();

		/* save partition info if necessary */
		run_save = false;
		slurm_mutex_lock(&state_save_lock);
		if (save_parts) {
			run_save = true;
			save_parts = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)dump_all_part_state();

		/* save reservation info if necessary */
		run_save = false;
		slurm_mutex_lock(&state_save_lock);
		if (save_resv) {
			run_save = true;
			save_resv = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)dump_all_resv_state();

		/* save trigger info if necessary */
		run_save = false;
		slurm_mutex_lock(&state_save_lock);
		if (save_triggers) {
			run_save = true;
			save_triggers = 0;
		}
		slurm_mutex_unlock(&state_save_lock);
		if (run_save)
			(void)trigger_state_save();
	}
}

