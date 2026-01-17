/*****************************************************************************\
 *  state_save.c - Keep saved slurmctld state current
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include <pthread.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/probes.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"

#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"

/* Maximum delay for pending state save to be processed, in seconds */
#ifndef SAVE_MAX_WAIT
#define SAVE_MAX_WAIT	5
#endif

#define SAVE_COUNT_DELAY \
	((timespec_t) { \
		.tv_sec = 1, \
	})
#define STATESAVE_WARN_TS \
	((timespec_t) { \
		.tv_nsec = (NSEC_IN_SEC / 2), \
	})
#define CTIME_STR_LEN 72

static pthread_mutex_t state_save_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_save_cond = PTHREAD_COND_INITIALIZER;
static int save_jobs = 0, save_nodes = 0, save_parts = 0;
static int save_triggers = 0, save_resv = 0;
static bool run_save_thread = true;
static latency_histogram_t save_histogram = LATENCY_HISTOGRAM_INITIALIZER;
static timespec_t last_save = { 0, 0 };
static timespec_t save_start = { 0, 0 };

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

static void _check_slow_save(void)
{
	timespec_diff_ns_t tdiff = { { 0 } };
	bool warn = false;
	char delay_str[CTIME_STR_LEN] = { 0 };

	tdiff = timespec_diff_ns(last_save, save_start);
	xassert(tdiff.after);

	latency_metric_add_histogram_value(&save_histogram, tdiff.diff);

	if (!(warn = timespec_is_after(tdiff.diff, STATESAVE_WARN_TS)))
		return;

	(void) timespec_ctime(tdiff.diff, false, delay_str, sizeof(delay_str));
	warning("Saving to StateSaveLocation took %s. Please check backing filesystem as all of Slurm operations are delayed due to slow StateSaveLocation writes.",
		delay_str);
}

static void _probe_verbose(probe_log_t *log)
{
	char histogram[LATENCY_METRIC_HISTOGRAM_STR_LEN] = { 0 };
	char ts_str[CTIME_STR_LEN] = { 0 };

	if (timespec_is_after(save_start, last_save)) {
		(void) timespec_ctime(save_start, true, ts_str, sizeof(ts_str));
		probe_log(log, "StateSave Status: SAVING");
		probe_log(log, "StateSave Started: %s", ts_str);
	} else {
		(void) timespec_ctime(timespec_rem(last_save, save_start),
				      false, ts_str, sizeof(ts_str));
		probe_log(log, "StateSave Status: SLEEPING");
		probe_log(log, "StateSave Last Duration: %s", ts_str);
	}

	(void) timespec_ctime(last_save, true, ts_str, sizeof(ts_str));
	probe_log(log, "StateSave Last Save: %s", ts_str);

	(void) latency_histogram_print_labels(histogram, sizeof(histogram));
	probe_log(log, "StateSave Histogram: %s", histogram);

	(void) latency_histogram_print(&save_histogram, histogram,
				       sizeof(histogram));
	probe_log(log, "StateSave Histogram: %s", histogram);
}

static probe_status_t _probe(probe_log_t *log)
{
	probe_status_t status = PROBE_RC_UNKNOWN;

	slurm_mutex_lock(&state_save_lock);

	if (log)
		_probe_verbose(log);

	if (!last_save.tv_sec && save_start.tv_sec)
		status = PROBE_RC_ONLINE;
	else
		status = PROBE_RC_READY;

	slurm_mutex_unlock(&state_save_lock);

	return status;
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
	bool run_save;
	int save_count;

	probe_register(__func__, _probe);

	while (1) {
		/* wait for work to perform */
		slurm_mutex_lock(&state_save_lock);
		while (1) {
			timespec_diff_ns_t save_delay = { { 0 } };

			if (last_save.tv_sec) {
				save_delay = timespec_diff_ns(timespec_now(),
							      save_start);
				xassert(save_delay.after);
			}

			save_count = save_jobs + save_nodes + save_parts +
				     save_resv + save_triggers;

			if (save_count &&
			    (!run_save_thread || !last_save.tv_sec ||
			     (timespec_to_secs(save_delay.diff) >=
			      SAVE_MAX_WAIT))) {
				break;		/* do the work */
			} else if (!run_save_thread) {
				run_save_thread = true;
				slurm_mutex_unlock(&state_save_lock);
				return NULL;	/* shutdown */
			} else if (save_count) { /* wait for a timeout */
				timespec_t delay =
					timespec_add(timespec_now(),
						     SAVE_COUNT_DELAY);

				slurm_cond_timedwait(&state_save_cond,
						     &state_save_lock, &delay);
			} else { /* wait for more work */
				slurm_cond_wait(&state_save_cond,
					  	&state_save_lock);
			}
		}

		save_start = timespec_now();

		/* save job info if necessary */
		run_save = false;
		/* slurm_mutex_lock(&state_save_lock); done above */
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

		slurm_mutex_lock(&state_save_lock);
		last_save = timespec_now();
		_check_slow_save();
		slurm_mutex_unlock(&state_save_lock);
	}
}
