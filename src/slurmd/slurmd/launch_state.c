/*****************************************************************************\
 *  launch_state.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "src/common/macros.h"

#include "src/slurmd/slurmd/launch_state.h"

#define JOB_STATE_CNT 64
static pthread_mutex_t job_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_state_cond = PTHREAD_COND_INITIALIZER;
static slurm_step_id_t active_job_id[JOB_STATE_CNT] = { { 0 } };

static void _launch_complete_log(char *type, slurm_step_id_t *step_id)
{
#if 0
	int j;

	info("active %s %pI", type, step_id);
	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (active_job_id[j].job_id != 0) {
			info("active_job_id[%d]=%pI", j, &active_job_id[j]);
		}
	}
	slurm_mutex_unlock(&job_state_mutex);
#endif
}

extern void launch_complete_add(slurm_step_id_t *step_id)
{
	int j, empty;

	slurm_mutex_lock(&job_state_mutex);
	empty = -1;
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (step_id->job_id == active_job_id[j].job_id) {
			if (step_id->step_id == SLURM_BATCH_SCRIPT)
				active_job_id[j].step_id = SLURM_BATCH_SCRIPT;
			break;
		}
		if ((active_job_id[j].job_id == 0) && (empty == -1))
			empty = j;
	}
	if (j >= JOB_STATE_CNT || (step_id->job_id != active_job_id[j].job_id)) {
		if (empty == -1) /* Discard oldest job */
			empty = 0;
		for (j = empty + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1].job_id = 0;
		active_job_id[JOB_STATE_CNT - 1].step_id = 0;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (active_job_id[j].job_id == 0) {
				active_job_id[j] = *step_id;
				break;
			}
		}
	}
	slurm_cond_signal(&job_state_cond);
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job add", step_id);
}

/* Test if we have a specific job ID still running */
extern bool launch_job_test(slurm_step_id_t *step_id)
{
	bool found = false;
	int j;

	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (step_id->job_id == active_job_id[j].job_id) {
			if (active_job_id[j].step_id == SLURM_BATCH_SCRIPT)
				found = true;
			break;
		}
	}
	slurm_mutex_unlock(&job_state_mutex);
	return found;
}

extern void launch_complete_rm(slurm_step_id_t *step_id)
{
	int j;

	slurm_mutex_lock(&job_state_mutex);
	for (j = 0; j < JOB_STATE_CNT; j++) {
		if (step_id->job_id == active_job_id[j].job_id)
			break;
	}
	if (j < JOB_STATE_CNT && (step_id->job_id == active_job_id[j].job_id)) {
		for (j = j + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1].job_id = 0;
		active_job_id[JOB_STATE_CNT - 1].step_id = 0;
	}
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job remove", step_id);
}

extern void launch_complete_wait(slurm_step_id_t *step_id)
{
	int j, empty;
	time_t start = time(NULL);
	struct timeval now;
	struct timespec timeout;

	slurm_mutex_lock(&job_state_mutex);
	while (true) {
		empty = -1;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (step_id->job_id == active_job_id[j].job_id)
				break;
			if ((active_job_id[j].job_id == 0) && (empty == -1))
				empty = j;
		}
		if (j < JOB_STATE_CNT) /* Found job, ready to return */
			break;
		if (difftime(time(NULL), start) <= 9) { /* Retry for 9 secs */
			debug2("wait for launch of %pI before suspending it",
			       step_id);
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec + 1;
			timeout.tv_nsec = now.tv_usec * 1000;
			slurm_cond_timedwait(&job_state_cond, &job_state_mutex,
					     &timeout);
			continue;
		}
		if (empty == -1) /* Discard oldest job */
			empty = 0;
		for (j = empty + 1; j < JOB_STATE_CNT; j++) {
			active_job_id[j - 1] = active_job_id[j];
		}
		active_job_id[JOB_STATE_CNT - 1].job_id = 0;
		active_job_id[JOB_STATE_CNT - 1].step_id = 0;
		for (j = 0; j < JOB_STATE_CNT; j++) {
			if (active_job_id[j].job_id == 0) {
				active_job_id[j].job_id = step_id->job_id;
				break;
			}
		}
		break;
	}
	slurm_mutex_unlock(&job_state_mutex);
	_launch_complete_log("job wait", step_id);
}
