/*****************************************************************************\
 *  scancel - cancel specified job(s) and/or job step(s)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2015 SchedMD LLC.
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

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/scancel/scancel.h"

#define MAX_CANCEL_RETRY 10
#define MAX_THREADS 10

static void  _add_delay(void);
static int   _cancel_jobs(void);
static void *_cancel_job_id (void *cancel_info);
static void *_cancel_step_id (void *cancel_info);
static int  _confirmation(job_info_t *job_ptr, uint32_t step_id);
static void _filter_job_records(void);
static void _load_job_records (void);
static int  _multi_cluster(List clusters);
static int  _proc_cluster(void);
static int  _signal_job_by_str(void);
static int  _verify_job_ids(void);

static job_info_msg_t * job_buffer_ptr = NULL;

typedef struct job_cancel_info {
	uint32_t array_job_id;
	uint32_t array_task_id;
	bool     array_flag;
/* Note: Either set job_id_str OR job_id */
	char *   job_id_str;
	uint32_t job_id;
	uint32_t step_id;
	uint16_t sig;
	int    * rc;
	int             *num_active_threads;
	pthread_mutex_t *num_active_threads_lock;
	pthread_cond_t  *num_active_threads_cond;
} job_cancel_info_t;

static	int num_active_threads = 0;
static	pthread_mutex_t  num_active_threads_lock;
static	pthread_cond_t   num_active_threads_cond;
static	pthread_mutex_t  max_delay_lock;
static	uint32_t max_resp_time = 0;
static	int request_count = 0;

int
main (int argc, char **argv)
{
	log_options_t log_opts = LOG_OPTS_STDERR_ONLY ;
	int rc = 0;

	slurm_conf_init(NULL);
	log_init (xbasename(argv[0]), log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	initialize_and_process_args(argc, argv);
	if (opt.verbose) {
		log_opts.stderr_level += opt.verbose;
		log_alter (log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	}

	if (opt.clusters)
		rc = _multi_cluster(opt.clusters);
	else
		rc = _proc_cluster();

	exit(rc);
}

/* _multi_cluster - process job cancellation across a list of clusters */
static int
_multi_cluster(List clusters)
{
	ListIterator itr;
	int rc = 0, rc2;

	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		rc2 = _proc_cluster();
		rc = MAX(rc, rc2);
	}
	list_iterator_destroy(itr);

	return rc;
}

/* _proc_cluster - process job cancellation on a specific cluster */
static int
_proc_cluster(void)
{
	int rc, rc2;

	if (has_default_opt() && !has_job_steps()) {
		rc = _signal_job_by_str();
		return rc;
	}

	_load_job_records();
	rc = _verify_job_ids();
	if ((opt.account) ||
	    (opt.job_name) ||
	    (opt.nodelist) ||
	    (opt.partition) ||
	    (opt.qos) ||
	    (opt.reservation) ||
	    (opt.state != JOB_END) ||
	    (opt.user_name) ||
	    (opt.wckey)) {
		_filter_job_records();
	}
	rc2 = _cancel_jobs();
	rc = MAX(rc, rc2);
	slurm_free_job_info_msg(job_buffer_ptr);

	return rc;
}

/* _load_job_records - load all job information for filtering
 * and verification
 */
static void
_load_job_records (void)
{
	int error_code;

	/* We need the fill job array string representation for identifying
	 * and killing job arrays */
	setenv("SLURM_BITSTR_LEN", "0", 1);
	error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr,
				      SHOW_ALL | SHOW_FEDERATION);

	if (error_code) {
		slurm_perror ("slurm_load_jobs error");
		exit (1);
	}
}

static bool _is_task_in_job(job_info_t *job_ptr, int array_id)
{
	int len;

	if (job_ptr->array_task_id == array_id)
		return true;

	if (!job_ptr->array_bitmap)
		return false;
	len = bit_size((bitstr_t *)job_ptr->array_bitmap);
	if (len <= array_id)
		return false;
	return (bit_test((bitstr_t *)job_ptr->array_bitmap, array_id));
}

static int _verify_job_ids(void)
{
	job_info_t *job_ptr;
	int i, j, rc = 0;

	if (opt.job_cnt == 0)
		return rc;

	opt.job_found = xmalloc(sizeof(bool) * opt.job_cnt);
	opt.job_pend  = xmalloc(sizeof(bool) * opt.job_cnt);
	job_ptr = job_buffer_ptr->job_array;
	for (i = 0; i < job_buffer_ptr->record_count; i++, job_ptr++) {
		/* NOTE: We re-use the job's "assoc_id" value as a flag to
		 * record if the job is referenced in the job list supplied
		 * by the user. */
		job_ptr->assoc_id = 0;
		if (IS_JOB_FINISHED(job_ptr))
			job_ptr->job_id = 0;
		if (job_ptr->job_id == 0)
			continue;

		for (j = 0; j < opt.job_cnt; j++) {
			if (opt.array_id[j] == NO_VAL) {
				if ((opt.job_id[j] == job_ptr->job_id) ||
				    ((opt.job_id[j] == job_ptr->array_job_id) &&
				     (opt.step_id[j] == SLURM_BATCH_SCRIPT))) {
					opt.job_found[j] = true;
				}
			} else if (opt.array_id[j] == INFINITE) {
				if (opt.job_id[j] == job_ptr->array_job_id) {
					opt.job_found[j] = true;
				}
			} else if (opt.job_id[j] != job_ptr->array_job_id) {
				continue;
			} else if (_is_task_in_job(job_ptr, opt.array_id[j])) {
				opt.job_found[j] = true;
			}
			if (opt.job_found[j]) {
				if (IS_JOB_PENDING(job_ptr))
					opt.job_pend[j] = true;
				job_ptr->assoc_id = 1;
			}
		}
		if (job_ptr->assoc_id == 0)
			job_ptr->job_id = 0;
	}

	for (j = 0; j < opt.job_cnt; j++) {
		char *job_id_str = NULL;
		if (!opt.job_found[j])
			rc = 1;
		else
			continue;

		if (opt.verbose < 0) {
			;
		} else if (opt.array_id[j] == NO_VAL) {
			xstrfmtcat(job_id_str, "%u", opt.job_id[j]);
		} else if (opt.array_id[j] == INFINITE) {
			xstrfmtcat(job_id_str, "%u_*", opt.job_id[j]);
		} else {
			xstrfmtcat(job_id_str, "%u_%u", opt.job_id[j],
				   opt.array_id[j]);
		}

		if (opt.verbose < 0) {
			;
		} else if (opt.step_id[j] == SLURM_BATCH_SCRIPT) {
			error("Kill job error on job id %s: %s",
			      job_id_str,
			      slurm_strerror(ESLURM_INVALID_JOB_ID));
		} else {
			error("Kill job error on job step id %s.%u: %s",
			      job_id_str, opt.step_id[j],
			      slurm_strerror(ESLURM_INVALID_JOB_ID));
		}
		xfree(job_id_str);

		/* Avoid this job in the cancel_job logic */
		opt.job_id[j] = 0;
	}

	return rc;
}

/* _filter_job_records - filtering job information per user specification
 * RET Count of job's filtered out OTHER than for job ID value */
static void _filter_job_records(void)
{
	int i, job_matches = 0;
	job_info_t *job_ptr = NULL;
	uint32_t job_base_state;

	job_ptr = job_buffer_ptr->job_array;
	for (i = 0; i < job_buffer_ptr->record_count; i++, job_ptr++) {
		if (IS_JOB_FINISHED(job_ptr))
			job_ptr->job_id = 0;
		if (job_ptr->job_id == 0)
			continue;

		job_base_state = job_ptr->job_state & JOB_STATE_BASE;
		if ((job_base_state != JOB_PENDING) &&
		    (job_base_state != JOB_RUNNING) &&
		    (job_base_state != JOB_SUSPENDED)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.account &&
		    xstrcmp(job_ptr->account, opt.account)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.job_name &&
		    xstrcmp(job_ptr->name, opt.job_name)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.partition &&
		    xstrcmp(job_ptr->partition, opt.partition)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.qos && xstrcmp(job_ptr->qos, opt.qos)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.reservation &&
		    xstrcmp(job_ptr->resv_name, opt.reservation)) {
			job_ptr->job_id = 0;
			continue;
		}

		if ((opt.state != JOB_END) &&
		    (job_ptr->job_state != opt.state)) {
			job_ptr->job_id = 0;
			continue;
		}

		if ((opt.user_name) &&
		    (job_ptr->user_id != opt.user_id)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (opt.nodelist) {
			/* If nodelist contains a '/', treat it as a file name */
			if (strchr(opt.nodelist, '/') != NULL) {
				char *reallist;
				reallist = slurm_read_hostfile(opt.nodelist,
							       NO_VAL);
				if (reallist) {
					xfree(opt.nodelist);
					opt.nodelist = reallist;
				}
			}

			hostset_t hs = hostset_create(job_ptr->nodes);
			if (!hostset_intersects(hs, opt.nodelist)) {
				job_ptr->job_id = 0;
				hostset_destroy(hs);
				continue;
			} else {
				hostset_destroy(hs);
			}
		}

		if (opt.wckey) {
			char *job_key = job_ptr->wckey;

			/*
			 * A wckey that begins with '*' indicates that the wckey
			 * was applied by default.  When the --wckey option does
			 * not begin with a '*', act on all wckeys with the same
			 * name, default or not.
			 */
			if ((opt.wckey[0] != '*') && (job_key[0] == '*'))
				job_key++;

			if (xstrcmp(job_key, opt.wckey) != 0) {
				job_ptr->job_id = 0;
				continue;
			}
		}

		job_matches++;
	}


	if ((job_matches == 0) && (opt.verbose > 0)) {
		char *err_msg = NULL;
		if (opt.account)
			xstrfmtcat(err_msg, "account=%s ", opt.account);
		if (opt.job_name)
			xstrfmtcat(err_msg, "job_name=%s ", opt.job_name);
		if (opt.nodelist)
			xstrfmtcat(err_msg, "nodelist=%s ", opt.nodelist);
		if (opt.partition)
			xstrfmtcat(err_msg, "partition=%s ", opt.partition);
		if (opt.qos)
			xstrfmtcat(err_msg, "qos=%s ", opt.qos);
		if (opt.reservation)
			xstrfmtcat(err_msg, "reservation=%s ", opt.reservation);
		if (opt.state != JOB_END) {
			xstrfmtcat(err_msg, "state=%s ",
				   job_state_string(opt.state));
		}
		if (opt.user_name)
			xstrfmtcat(err_msg, "user_name=%s ", opt.user_name);
		if (opt.wckey)
			xstrfmtcat(err_msg, "wckey=%s ", opt.wckey);
		if (err_msg) {
			error("No active jobs match ALL job filters, including: %s",
			      err_msg);
			xfree(err_msg);
		}
	}
	return;
}

static char *_build_jobid_str(job_info_t *job_ptr)
{
	char *result = NULL;

	if (job_ptr->array_task_str) {
		xstrfmtcat(result, "%u_[%s]",
			   job_ptr->array_job_id, job_ptr->array_task_str);
	} else if (job_ptr->array_task_id != NO_VAL) {
		xstrfmtcat(result, "%u_%u",
			   job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		xstrfmtcat(result, "%u", job_ptr->job_id);
	}

	return result;
}

static void _cancel_jobid_by_state(uint32_t job_state, int *rc)
{
	job_cancel_info_t *cancel_info;
	job_info_t *job_ptr;
	int i, j;

	if (opt.job_cnt == 0)
		return;

	for (j = 0; j < opt.job_cnt; j++) {
		if (opt.job_id[j] == 0)
			continue;
		if ((job_state == JOB_PENDING) && !opt.job_pend[j])
			continue;

		job_ptr = job_buffer_ptr->job_array;
		for (i = 0; i < job_buffer_ptr->record_count; i++, job_ptr++) {
			if (IS_JOB_FINISHED(job_ptr))
				job_ptr->job_id = 0;
			if (job_ptr->job_id == 0)
				continue;
			if ((opt.step_id[j] != SLURM_BATCH_SCRIPT) &&
			    IS_JOB_PENDING(job_ptr)) {
				/* User specified #.# for step, but the job ID
				 * may be job array leader with part of job
				 * array running with other tasks pending */
				continue;
			}

			opt.job_found[j] = false;
			if (opt.array_id[j] == NO_VAL) {
				if ((opt.job_id[j] == job_ptr->job_id) ||
				    ((opt.job_id[j] == job_ptr->array_job_id) &&
				     (opt.step_id[j] == SLURM_BATCH_SCRIPT))) {
					opt.job_found[j] = true;
				}
			} else if (opt.array_id[j] == INFINITE) {
				if (opt.job_id[j] == job_ptr->array_job_id) {
					opt.job_found[j] = true;
				}
			} else if (opt.job_id[j] != job_ptr->array_job_id) {
				continue;
			} else if (_is_task_in_job(job_ptr, opt.array_id[j])) {
				opt.job_found[j] = true;
			}

			if (!opt.job_found[j])
				continue;

			if (opt.interactive &&
			    (_confirmation(job_ptr, opt.step_id[j]) == 0)) {
				job_ptr->job_id = 0;	/* Don't check again */
				continue;
			}

			slurm_mutex_lock(&num_active_threads_lock);
			num_active_threads++;
			while (num_active_threads > MAX_THREADS) {
				slurm_cond_wait(&num_active_threads_cond,
						&num_active_threads_lock);
			}
			slurm_mutex_unlock(&num_active_threads_lock);

			cancel_info = (job_cancel_info_t *)
				      xmalloc(sizeof(job_cancel_info_t));
			cancel_info->rc      = rc;
			cancel_info->sig     = opt.signal;
			cancel_info->num_active_threads = &num_active_threads;
			cancel_info->num_active_threads_lock =
					&num_active_threads_lock;
			cancel_info->num_active_threads_cond =
					&num_active_threads_cond;
			if (opt.step_id[j] == SLURM_BATCH_SCRIPT) {
				cancel_info->job_id_str =
					_build_jobid_str(job_ptr);
				slurm_thread_create_detached(NULL,
							     _cancel_job_id,
							     cancel_info);
				job_ptr->job_id = 0;
			} else {
				cancel_info->job_id = job_ptr->job_id;
				cancel_info->step_id = opt.step_id[j];
				slurm_thread_create_detached(NULL,
							     _cancel_step_id,
							     cancel_info);
			}

			if (opt.interactive) {
				/* Print any error message for first job before
				 * starting confirmation of next job */
				slurm_mutex_lock(&num_active_threads_lock);
				while (num_active_threads > 0) {
					slurm_cond_wait(&num_active_threads_cond,
							&num_active_threads_lock);
				}
				slurm_mutex_unlock(&num_active_threads_lock);
			}
		}
	}
}

static void
_cancel_jobs_by_state(uint32_t job_state, int *rc)
{
	int i;
	job_cancel_info_t *cancel_info;
	job_info_t *job_ptr = job_buffer_ptr->job_array;

	/* Spawn a thread to cancel each job or job step marked for
	 * cancellation */
	if (opt.job_cnt) {
		_cancel_jobid_by_state(job_state, rc);
		return;
	}

	for (i = 0; i < job_buffer_ptr->record_count; i++, job_ptr++) {
		if (IS_JOB_FINISHED(job_ptr))
			job_ptr->job_id = 0;
		if (job_ptr->job_id == 0)
			continue;

		if ((job_state < JOB_END) &&
		    (job_ptr->job_state != job_state))
			continue;

		if (opt.interactive &&
		    (_confirmation(job_ptr, SLURM_BATCH_SCRIPT) == 0)) {
			job_ptr->job_id = 0;
			continue;
		}

		cancel_info = (job_cancel_info_t *)
			xmalloc(sizeof(job_cancel_info_t));
		cancel_info->job_id_str = _build_jobid_str(job_ptr);
		cancel_info->rc      = rc;
		cancel_info->sig     = opt.signal;
		cancel_info->num_active_threads = &num_active_threads;
		cancel_info->num_active_threads_lock =
			&num_active_threads_lock;
		cancel_info->num_active_threads_cond =
			&num_active_threads_cond;

		slurm_mutex_lock(&num_active_threads_lock);
		num_active_threads++;
		while (num_active_threads > MAX_THREADS) {
			slurm_cond_wait(&num_active_threads_cond,
					&num_active_threads_lock);
		}
		slurm_mutex_unlock(&num_active_threads_lock);

		slurm_thread_create_detached(NULL, _cancel_job_id, cancel_info);
		job_ptr->job_id = 0;

		if (opt.interactive) {
			/* Print any error message for first job before
			 * starting confirmation of next job */
			slurm_mutex_lock(&num_active_threads_lock);
			while (num_active_threads > 0) {
				slurm_cond_wait(&num_active_threads_cond,
						&num_active_threads_lock);
			}
			slurm_mutex_unlock(&num_active_threads_lock);
		}
	}
}

/* _cancel_jobs - filter then cancel jobs or job steps per request */
static int _cancel_jobs(void)
{
	int rc = 0;

	slurm_mutex_init(&num_active_threads_lock);
	slurm_cond_init(&num_active_threads_cond, NULL);

	_cancel_jobs_by_state(JOB_PENDING, &rc);
	/* Wait for any cancel of pending jobs to complete before starting
	 * cancellation of running jobs so that we don't have a race condition
	 * with pending jobs getting scheduled while running jobs are also
	 * being cancelled. */
	slurm_mutex_lock( &num_active_threads_lock );
	while (num_active_threads > 0) {
		slurm_cond_wait(&num_active_threads_cond,
				&num_active_threads_lock);
	}
	slurm_mutex_unlock(&num_active_threads_lock);

	_cancel_jobs_by_state(JOB_END, &rc);
	/* Wait for any spawned threads that have not finished */
	slurm_mutex_lock( &num_active_threads_lock );
	while (num_active_threads > 0) {
		slurm_cond_wait(&num_active_threads_cond,
				&num_active_threads_lock);
	}
	slurm_mutex_unlock(&num_active_threads_lock);

	slurm_mutex_destroy(&num_active_threads_lock);
	slurm_cond_destroy(&num_active_threads_cond);

	return rc;
}

/* scancel can cancel huge numbers of job from a single command line using
 * pthreads for parallelism. Add a delay if there are many RPCs and response
 * delays get excessive to avoid causing a denial of service attack. */
static void _add_delay(void)
{
	static int target_resp_time = -1;
	static int delay_time = 10000, previous_delay = 0;
	int my_delay;

	slurm_mutex_lock(&max_delay_lock);
	if (target_resp_time < 0) {
		target_resp_time = slurm_get_msg_timeout() / 4;
		target_resp_time = MAX(target_resp_time, 3);
		target_resp_time = MIN(target_resp_time, 5);
		target_resp_time *= 1000000;
		debug("%s: target response time = %d", __func__,
		      target_resp_time);
	}
	if ((++request_count < MAX_THREADS) ||
	    (max_resp_time <= target_resp_time)) {
		slurm_mutex_unlock(&max_delay_lock);
		return;
	}

	/* Maximum delay of 1 second. Start at 10 msec with Fibonacci backoff */
	my_delay = MIN((delay_time + previous_delay), 1000000);
	previous_delay = delay_time;
	delay_time = my_delay;
	slurm_mutex_unlock(&max_delay_lock);

	info("%s: adding delay in RPC send of %d usec", __func__, my_delay);
	usleep(my_delay);
	return;
}

static void *
_cancel_job_id (void *ci)
{
	int error_code = SLURM_SUCCESS, i;
	job_cancel_info_t *cancel_info = (job_cancel_info_t *)ci;
	bool sig_set = true;
	uint16_t flags = 0;
	char *job_type = "";
	DEF_TIMERS;

	if (cancel_info->sig == NO_VAL16) {
		cancel_info->sig = SIGKILL;
		sig_set = false;
	}
	if (opt.batch) {
		flags |= KILL_JOB_BATCH;
		job_type = "batch ";
	}
	if (opt.full) {
		flags |= KILL_FULL_JOB;
		job_type = "full ";
	}
	if (opt.hurry)
		flags |= KILL_HURRY;
	if (cancel_info->array_flag)
		flags |= KILL_JOB_ARRAY;

	if (!cancel_info->job_id_str) {
		if (cancel_info->array_job_id &&
		    (cancel_info->array_task_id == INFINITE)) {
			xstrfmtcat(cancel_info->job_id_str, "%u_*",
				   cancel_info->array_job_id);
		} else if (cancel_info->array_job_id) {
			xstrfmtcat(cancel_info->job_id_str, "%u_%u",
				   cancel_info->array_job_id,
				   cancel_info->array_task_id);
		} else {
			xstrfmtcat(cancel_info->job_id_str, "%u",
				   cancel_info->job_id);
		}
	}

	if (!sig_set) {
		verbose("Terminating %sjob %s", job_type,
			cancel_info->job_id_str);
	} else {
		verbose("Signal %u to %sjob %s", cancel_info->sig, job_type,
			cancel_info->job_id_str);
	}

	for (i = 0; i < MAX_CANCEL_RETRY; i++) {
		job_step_kill_msg_t kill_msg;

		_add_delay();
		START_TIMER;

		memset(&kill_msg, 0, sizeof(job_step_kill_msg_t));
		kill_msg.flags	= flags;
		kill_msg.job_id      = NO_VAL;
		kill_msg.job_step_id = NO_VAL;
		kill_msg.sibling     = opt.sibling;
		kill_msg.signal      = cancel_info->sig;
		kill_msg.sjob_id     = cancel_info->job_id_str;

		error_code = slurm_kill_job_msg(REQUEST_KILL_JOB, &kill_msg);

		END_TIMER;
		slurm_mutex_lock(&max_delay_lock);
		max_resp_time = MAX(max_resp_time, DELTA_TIMER);
		slurm_mutex_unlock(&max_delay_lock);

		if ((error_code == 0) ||
		    (errno != ESLURM_TRANSITION_STATE_NO_UPDATE))
			break;
		verbose("Job is in transistional state, retrying");
		sleep(5 + i);
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((opt.verbose > 0) ||
		    ((error_code != ESLURM_ALREADY_DONE) &&
		     (error_code != ESLURM_INVALID_JOB_ID) &&
		     ((error_code != ESLURM_NOT_PACK_WHOLE) ||
		      (opt.job_cnt != 0)))) {
			error("Kill job error on job id %s: %s",
			      cancel_info->job_id_str,
			      slurm_strerror(slurm_get_errno()));
		}
		if (((error_code == ESLURM_ALREADY_DONE) ||
		     (error_code == ESLURM_INVALID_JOB_ID)) &&
		    (cancel_info->sig == SIGKILL)) {
			error_code = 0;	/* Ignore error if job done */
		}	
	}

	/* Purposely free the struct passed in here, so the caller doesn't have
	 * to keep track of it, but don't destroy the mutex and condition
	 * variables contained. */
	slurm_mutex_lock(cancel_info->num_active_threads_lock);
	*(cancel_info->rc) = MAX(*(cancel_info->rc), error_code);
	(*(cancel_info->num_active_threads))--;
	slurm_cond_signal(cancel_info->num_active_threads_cond);
	slurm_mutex_unlock(cancel_info->num_active_threads_lock);

	xfree(cancel_info->job_id_str);
	xfree(cancel_info);
	return NULL;
}

static void *
_cancel_step_id (void *ci)
{
	int error_code = SLURM_SUCCESS, i;
	job_cancel_info_t *cancel_info = (job_cancel_info_t *)ci;
	uint32_t job_id  = cancel_info->job_id;
	uint32_t step_id = cancel_info->step_id;
	bool sig_set = true;
	DEF_TIMERS;

	if (cancel_info->sig == NO_VAL16) {
		cancel_info->sig = SIGKILL;
		sig_set = false;
	}

	if (!cancel_info->job_id_str) {
		if (cancel_info->array_job_id &&
		    (cancel_info->array_task_id == INFINITE)) {
			xstrfmtcat(cancel_info->job_id_str, "%u_*",
				   cancel_info->array_job_id);
		} else if (cancel_info->array_job_id) {
			xstrfmtcat(cancel_info->job_id_str, "%u_%u",
				   cancel_info->array_job_id,
				   cancel_info->array_task_id);
		} else {
			xstrfmtcat(cancel_info->job_id_str, "%u",
				   cancel_info->job_id);
		}
	}

	for (i = 0; i < MAX_CANCEL_RETRY; i++) {
		if (cancel_info->sig == SIGKILL) {
			verbose("Terminating step %s.%u",
				cancel_info->job_id_str, step_id);
		} else {
			verbose("Signal %u to step %s.%u",
				cancel_info->sig,
				cancel_info->job_id_str, step_id);
		}

		_add_delay();
		START_TIMER;
		if ((!sig_set) || opt.ctld)
			error_code = slurm_kill_job_step(job_id, step_id,
							 cancel_info->sig);
		else if (cancel_info->sig == SIGKILL)
			error_code = slurm_terminate_job_step(job_id, step_id);
		else
			error_code = slurm_signal_job_step(job_id, step_id,
							   cancel_info->sig);
		END_TIMER;
		slurm_mutex_lock(&max_delay_lock);
		max_resp_time = MAX(max_resp_time, DELTA_TIMER);
		slurm_mutex_unlock(&max_delay_lock);

		if ((error_code == 0) ||
		    ((errno != ESLURM_TRANSITION_STATE_NO_UPDATE) &&
		     (errno != ESLURM_JOB_PENDING)))
			break;
		verbose("Job is in transistional state, retrying");
		sleep(5 + i);
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((opt.verbose > 0) || (error_code != ESLURM_ALREADY_DONE))
			error("Kill job error on job step id %s: %s",
		 	      cancel_info->job_id_str,
			      slurm_strerror(slurm_get_errno()));

		if ((error_code == ESLURM_ALREADY_DONE) &&
		    (cancel_info->sig == SIGKILL)) {
			error_code = 0;	/* Ignore error if job done */
		}
	}

	/* Purposely free the struct passed in here, so the caller doesn't have
	 * to keep track of it, but don't destroy the mutex and condition
	 * variables contained. */
	slurm_mutex_lock(cancel_info->num_active_threads_lock);
	*(cancel_info->rc) = MAX(*(cancel_info->rc), error_code);
	(*(cancel_info->num_active_threads))--;
	slurm_cond_signal(cancel_info->num_active_threads_cond);
	slurm_mutex_unlock(cancel_info->num_active_threads_lock);

	xfree(cancel_info->job_id_str);
	xfree(cancel_info);
	return NULL;
}

/* _confirmation - Confirm job cancel request interactively */
static int
_confirmation(job_info_t *job_ptr, uint32_t step_id)
{
	char *job_id_str, in_line[128];

	while (1) {
		job_id_str = _build_jobid_str(job_ptr);
		if (step_id == SLURM_BATCH_SCRIPT) {
			printf("Cancel job_id=%s name=%s partition=%s [y/n]? ",
			       job_id_str, job_ptr->name,
			       job_ptr->partition);
		} else {
			printf("Cancel step_id=%s.%u name=%s partition=%s [y/n]? ",
			       job_id_str, step_id, job_ptr->name,
			       job_ptr->partition);
		}
		xfree(job_id_str);
		if (fgets(in_line, sizeof(in_line), stdin) == NULL)
			continue;
		if ((in_line[0] == 'y') || (in_line[0] == 'Y'))
			return 1;
		if ((in_line[0] == 'n') || (in_line[0] == 'N'))
			return 0;
	}

}

static int _signal_job_by_str(void)
{
	job_cancel_info_t *cancel_info;
	int i, rc = 0;

	slurm_mutex_init(&num_active_threads_lock);
	slurm_cond_init(&num_active_threads_cond, NULL);

	for (i = 0; opt.job_list[i]; i++) {
		cancel_info = (job_cancel_info_t *)
			xmalloc(sizeof(job_cancel_info_t));
		cancel_info->job_id_str = xstrdup(opt.job_list[i]);
		cancel_info->rc      = &rc;
		cancel_info->sig     = opt.signal;
		cancel_info->num_active_threads = &num_active_threads;
		cancel_info->num_active_threads_lock =
			&num_active_threads_lock;
		cancel_info->num_active_threads_cond =
			&num_active_threads_cond;

		slurm_mutex_lock(&num_active_threads_lock);
		num_active_threads++;
		while (num_active_threads > MAX_THREADS) {
			slurm_cond_wait(&num_active_threads_cond,
					&num_active_threads_lock);
		}
		slurm_mutex_unlock(&num_active_threads_lock);

		slurm_thread_create_detached(NULL, _cancel_job_id, cancel_info);
	}

	/* Wait all spawned threads to finish */
	slurm_mutex_lock( &num_active_threads_lock );
	while (num_active_threads > 0) {
		slurm_cond_wait(&num_active_threads_cond,
				&num_active_threads_lock);
	}
	slurm_mutex_unlock(&num_active_threads_lock);

	return rc;
}
