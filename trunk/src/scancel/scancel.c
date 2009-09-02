/*****************************************************************************\
 *  scancel - cancel specified job(s) and/or job step(s)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"
#include "src/scancel/scancel.h"

#define MAX_CANCEL_RETRY 10
#define MAX_THREADS 20


static void _cancel_jobs (void);
static void *_cancel_job_id (void *cancel_info);
static void *_cancel_step_id (void *cancel_info);

static int  _confirmation (int i, uint32_t step_id);
static void _filter_job_records (void);
static void _load_job_records (void);
static void _verify_job_ids (void);

static job_info_msg_t * job_buffer_ptr = NULL;

typedef struct job_cancel_info {
	uint32_t job_id;
	uint32_t step_id;
	uint16_t sig;
	int             *num_active_threads;
	pthread_mutex_t *num_active_threads_lock;
	pthread_cond_t  *num_active_threads_cond;
} job_cancel_info_t;

static	pthread_attr_t  attr;
static	int num_active_threads = 0;
static	pthread_mutex_t  num_active_threads_lock;
static	pthread_cond_t   num_active_threads_cond;

int
main (int argc, char *argv[]) 
{
	log_options_t log_opts = LOG_OPTS_STDERR_ONLY ;

	log_init (xbasename(argv[0]), log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	initialize_and_process_args(argc, argv);
	if (opt.verbose) {
		log_opts.stderr_level += opt.verbose;
		log_alter (log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	} 
	
	_load_job_records();
	_verify_job_ids();

	if ((opt.account) ||
	    (opt.interactive) ||
	    (opt.job_name) ||
	    (opt.nodelist) ||
	    (opt.partition) ||
	    (opt.qos) ||
	    (opt.state != JOB_END) ||
	    (opt.user_name) ||
	    (opt.wckey)) {
		_filter_job_records ();
	}
	_cancel_jobs ();

	exit (0);
}


/* _load_job_records - load all job information for filtering and verification */
static void 
_load_job_records (void)
{
	int error_code;

	error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr, 1);
	
	if (error_code) {
		slurm_perror ("slurm_load_jobs error");
		exit (1);
	}
}


static void
_verify_job_ids (void)
{
	/* If a list of jobs was given, make sure each job is actually in
         * our list of job records. */
	int i, j;
	job_info_t *job_ptr = job_buffer_ptr->job_array;

	for (j = 0; j < opt.job_cnt; j++ ) {
		for (i = 0; i < job_buffer_ptr->record_count; i++) {
			if (job_ptr[i].job_id == opt.job_id[j])
				break;
		}
		if (((job_ptr[i].job_state >= JOB_COMPLETE) ||
		     (i >= job_buffer_ptr->record_count)) &&
		     (opt.verbose >= 0)) {
			if (opt.step_id[j] == SLURM_BATCH_SCRIPT)
				error("Kill job error on job id %u: %s", 
				      opt.job_id[j], 
				      slurm_strerror(ESLURM_INVALID_JOB_ID));
			else
				error("Kill job error on job step id %u.%u: %s",
				      opt.job_id[j], opt.step_id[j],
				      slurm_strerror(ESLURM_INVALID_JOB_ID));
		}
	}
}


/* _filter_job_records - filtering job information per user specification */
static void 
_filter_job_records (void)
{
	int i, j;
	job_info_t *job_ptr = NULL;
	uint16_t job_base_state;

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_ptr[i].job_id == 0) 
			continue;

		job_base_state = job_ptr[i].job_state & JOB_STATE_BASE;
		if ((job_base_state != JOB_PENDING) &&
		    (job_base_state != JOB_RUNNING) &&
		    (job_base_state != JOB_SUSPENDED)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if (opt.account != NULL &&
		    (strcmp(job_ptr[i].account, opt.account) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if (opt.job_name != NULL &&
		    (strcmp(job_ptr[i].name, opt.job_name) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if ((opt.partition != NULL) &&
		    (strcmp(job_ptr[i].partition,opt.partition) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if ((opt.qos != NULL) &&
		    (strcmp(job_ptr[i].qos, opt.qos) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if ((opt.state != JOB_END) &&
		    (job_ptr[i].job_state != opt.state)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if ((opt.user_name != NULL) &&
		    (job_ptr[i].user_id != opt.user_id)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if (opt.nodelist != NULL) {
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

			hostset_t hs = hostset_create(job_ptr[i].nodes);
			if (!hostset_intersects(hs, opt.nodelist)) {
				job_ptr[i].job_id = 0;
				hostset_destroy(hs);
				continue;
			} else {
				hostset_destroy(hs);
			}
		}

		if (opt.wckey != NULL) {
			char *job_key = job_ptr[i].wckey;

			/*
			 * A wckey that begins with '*' indicates that the wckey
			 * was applied by default.  When the --wckey option does
			 * not begin with a '*', act on all wckeys with the same
			 * name, default or not.
			 */
			if ((opt.wckey[0] != '*') && (job_key[0] == '*'))
				job_key++;

			if (strcmp(job_key, opt.wckey) != 0) {
				job_ptr[i].job_id = 0;
				continue;
			}
		}

		if (opt.job_cnt == 0)
			continue;
		for (j = 0; j < opt.job_cnt; j++) {
			if (job_ptr[i].job_id == opt.job_id[j])
				break;
		}
		if (j >= opt.job_cnt) { /* not found */
			job_ptr[i].job_id = 0;
			continue;
		}
	}
}

static void
_cancel_jobs_by_state(uint16_t job_state)
{
	int i, j, err;
	job_cancel_info_t *cancel_info;
	job_info_t *job_ptr = job_buffer_ptr->job_array;
	pthread_t  dummy;

	/* Spawn a thread to cancel each job or job step marked for
	 * cancellation */
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_ptr[i].job_id == 0)
			continue;

		if ((job_state < JOB_END) &&
		    (job_ptr[i].job_state != job_state))
			continue;

		/* If cancelling a list of jobs, see if the current job
		 * included a step id */
		if (opt.job_cnt) {
			for (j = 0; j < opt.job_cnt; j++ ) {
				if (job_ptr[i].job_id != opt.job_id[j])
					continue;

				if (opt.interactive &&
				    (_confirmation(i, opt.step_id[j]) == 0))
					continue;

				cancel_info =
					(job_cancel_info_t *)
					xmalloc(sizeof(job_cancel_info_t));
				cancel_info->job_id  = job_ptr[i].job_id;
				cancel_info->sig     = opt.signal;
				cancel_info->num_active_threads =
					&num_active_threads;
				cancel_info->num_active_threads_lock =
					&num_active_threads_lock;
				cancel_info->num_active_threads_cond =
					&num_active_threads_cond;

				pthread_mutex_lock(&num_active_threads_lock);
				num_active_threads++;
				while (num_active_threads > MAX_THREADS) {
					pthread_cond_wait(
						&num_active_threads_cond,
						&num_active_threads_lock);
				}
				pthread_mutex_unlock(&num_active_threads_lock);

				if (opt.step_id[j] == SLURM_BATCH_SCRIPT) {
					err = pthread_create(&dummy, &attr,
							     _cancel_job_id,
							     cancel_info);
					if (err)
						_cancel_job_id(cancel_info);
					break;
				} else {
					cancel_info->step_id = opt.step_id[j];
					err = pthread_create(&dummy, &attr,
							     _cancel_step_id,
							     cancel_info);
					if (err)
						_cancel_step_id(cancel_info);
					/* Don't break here.  Keep looping in
					 * case other steps from the same job
					 * are cancelled. */
				}
			}
		} else {
			if (opt.interactive &&
			    (_confirmation(i, SLURM_BATCH_SCRIPT) == 0))
				continue;

			cancel_info = (job_cancel_info_t *)
				xmalloc(sizeof(job_cancel_info_t));
			cancel_info->job_id  = job_ptr[i].job_id;
			cancel_info->sig     = opt.signal;
			cancel_info->num_active_threads = &num_active_threads;
			cancel_info->num_active_threads_lock =
				&num_active_threads_lock;
			cancel_info->num_active_threads_cond =
				&num_active_threads_cond;

			pthread_mutex_lock( &num_active_threads_lock );
			num_active_threads++;
			while (num_active_threads > MAX_THREADS) {
				pthread_cond_wait(&num_active_threads_cond,
						  &num_active_threads_lock);
			}
			pthread_mutex_unlock(&num_active_threads_lock);

			err = pthread_create(&dummy, &attr, _cancel_job_id,
					     cancel_info);
			if (err)
				_cancel_job_id(cancel_info);
		}
		job_ptr[i].job_id = 0;
	}
}

/* _cancel_jobs - filter then cancel jobs or job steps per request */
static void
_cancel_jobs (void)
{
	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	slurm_mutex_init(&num_active_threads_lock);

	if (pthread_cond_init(&num_active_threads_cond, NULL))
		error("pthread_cond_init error %m");

	_cancel_jobs_by_state(JOB_PENDING);
	_cancel_jobs_by_state(JOB_END);

	/* Wait for any spawned threads that have not finished */
	pthread_mutex_lock( &num_active_threads_lock );
	while (num_active_threads > 0) {
		pthread_cond_wait( &num_active_threads_cond,
				   &num_active_threads_lock );
	}
	pthread_mutex_unlock( &num_active_threads_lock );

	slurm_attr_destroy(&attr);
	slurm_mutex_destroy(&num_active_threads_lock);
	if (pthread_cond_destroy(&num_active_threads_cond))
		error("pthread_cond_destroy error %m");
}

static void *
_cancel_job_id (void *ci)
{
	int error_code = SLURM_SUCCESS, i;
	bool sig_set = true;

	job_cancel_info_t *cancel_info = (job_cancel_info_t *)ci;
	uint32_t job_id = cancel_info->job_id;
	uint16_t sig    = cancel_info->sig;

	if (sig == (uint16_t)-1) {
		sig = SIGKILL;
		sig_set = false;
	}

	for (i=0; i<MAX_CANCEL_RETRY; i++) {
		if (!sig_set)
			verbose("Terminating job %u", job_id);
		else
			verbose("Signal %u to job %u", sig, job_id);

		if ((!sig_set) || opt.ctld) {
			error_code = slurm_kill_job (job_id, sig,
						     (uint16_t)opt.batch);
		} else {
			if (opt.batch)
				error_code = slurm_signal_job_step(
					job_id,
					SLURM_BATCH_SCRIPT,
					sig);
			else
				error_code = slurm_signal_job (job_id, sig);
		}
		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		verbose("Job is in transistional state, retrying");
		sleep ( 5 + i );
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((opt.verbose > 0) || 
		    ((error_code != ESLURM_ALREADY_DONE) &&
		     (error_code != ESLURM_INVALID_JOB_ID)))
			error("Kill job error on job id %u: %s", 
				job_id, slurm_strerror(slurm_get_errno()));
	}

	/* Purposely free the struct passed in here, so the caller doesn't have
	 * to keep track of it, but don't destroy the mutex and condition 
	 * variables contained. */ 
	pthread_mutex_lock(   cancel_info->num_active_threads_lock );
	(*(cancel_info->num_active_threads))--;
	pthread_cond_signal(  cancel_info->num_active_threads_cond );
	pthread_mutex_unlock( cancel_info->num_active_threads_lock );

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
	uint16_t sig     = cancel_info->sig;
	bool sig_set = true;

	if (sig == (uint16_t)-1) {
		sig = SIGKILL;
		sig_set = false;
	}

	for (i=0; i<MAX_CANCEL_RETRY; i++) {
		if (sig == SIGKILL)
			verbose("Terminating step %u.%u", job_id, step_id);
		else {
			verbose("Signal %u to step %u.%u", 
				sig, job_id, step_id);
		}

		if ((!sig_set) || opt.ctld)
			error_code = slurm_kill_job_step(job_id, step_id, sig);
		else if (sig == SIGKILL)
			error_code = slurm_terminate_job_step(job_id, step_id);
		else
			error_code = slurm_signal_job_step(job_id, step_id,
							   sig);
		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		verbose("Job is in transistional state, retrying");
		sleep ( 5 + i );
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((opt.verbose > 0) || (error_code != ESLURM_ALREADY_DONE ))
			error("Kill job error on job step id %u.%u: %s", 
		 		job_id, step_id, 
				slurm_strerror(slurm_get_errno()));
	}

	/* Purposely free the struct passed in here, so the caller doesn't have
	 * to keep track of it, but don't destroy the mutex and condition 
	 * variables contained. */ 
	pthread_mutex_lock(   cancel_info->num_active_threads_lock );
	(*(cancel_info->num_active_threads))--;
	pthread_cond_signal(  cancel_info->num_active_threads_cond );
	pthread_mutex_unlock( cancel_info->num_active_threads_lock );

	xfree(cancel_info);
	return NULL;
}

/* _confirmation - Confirm job cancel request interactively */
static int 
_confirmation (int i, uint32_t step_id)
{
	char in_line[128];
	job_info_t *job_ptr = NULL;
	char *line = NULL;

	job_ptr = job_buffer_ptr->job_array ;
	while (1) {
		if (step_id == SLURM_BATCH_SCRIPT) {
			printf ("Cancel job_id=%u name=%s partition=%s [y/n]? ", 
			        job_ptr[i].job_id, job_ptr[i].name, 
				job_ptr[i].partition);
		} else {
			printf ("Cancel step_id=%u.%u name=%s partition=%s [y/n]? ", 
			        job_ptr[i].job_id, step_id, job_ptr[i].name, 
				job_ptr[i].partition);
		}

		/* we only set this here to avoid a warning.  We throw it away
		   later. */
		line = fgets (in_line, sizeof (in_line), stdin);
		if ((in_line[0] == 'y') || (in_line[0] == 'Y'))
			return 1;
		if ((in_line[0] == 'n') || (in_line[0] == 'N'))
			return 0;
	}

}
