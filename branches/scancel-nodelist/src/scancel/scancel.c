/*****************************************************************************\
 *  scancel - cancel specified job(s) and/or job step(s)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
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
/*
static void _cancel_job_id (uint32_t job_id, uint16_t sig);
static void _cancel_step_id (uint32_t job_id, uint32_t step_id, 
			     uint16_t sig);
*/
static void *_cancel_job_id (void *cancel_info);
static void *_cancel_step_id (void *cancel_info);

static int  _confirmation (int i, uint32_t step_id);
static void _filter_job_records (void);
static void _load_job_records (void);

static job_info_msg_t * job_buffer_ptr = NULL;

typedef struct job_cancel_info {
	uint32_t job_id;
	uint32_t step_id;
	uint16_t sig;
	int             *num_active_threads;
	pthread_mutex_t *num_active_threads_lock;
	pthread_cond_t  *num_active_threads_cond;
} job_cancel_info_t;


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
	
	_load_job_records ();

	if ((opt.interactive) ||
	    (opt.job_name) ||
	    (opt.partition) ||
	    (opt.state != JOB_END) ||
	    (opt.user_name) ||
	    (opt.nodelist)) {
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


/* _filter_job_records - filtering job information per user specification */
static void 
_filter_job_records (void)
{
	int i, j;
	job_info_t *job_ptr = NULL;

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_ptr[i].job_id == 0) 
			continue;

		if ((job_ptr[i].job_state != JOB_PENDING)
		&&  (job_ptr[i].job_state != JOB_RUNNING)
		&&  (job_ptr[i].job_state != JOB_SUSPENDED)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if (opt.job_name != NULL &&
		    (strcmp(job_ptr[i].name, opt.job_name) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if (opt.wckey != NULL &&
		    (strcmp(job_ptr[i].wckey, opt.wckey) != 0)) {
			job_ptr[i].job_id = 0;
			continue;
		}

		if ((opt.partition != NULL) &&
		    (strcmp(job_ptr[i].partition,opt.partition) != 0)) {
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
				char *reallist = slurm_read_hostfile(opt.nodelist, NO_VAL);
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


/* _cancel_jobs - filter then cancel jobs or job steps per request */
static void
_cancel_jobs (void)
{
	int i, j, err;
	job_info_t *job_ptr = NULL;
	pthread_attr_t  attr;
	job_cancel_info_t *cancel_info;
	pthread_t  dummy;
	int num_active_threads = 0;
	pthread_mutex_t  num_active_threads_lock;
	pthread_cond_t   num_active_threads_cond;

	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	slurm_mutex_init(&num_active_threads_lock);

	if (pthread_cond_init(&num_active_threads_cond, NULL))
		error("pthread_cond_init error %m");

	job_ptr = job_buffer_ptr->job_array ;

	/* If a list of jobs was given, make sure each job is actually in
           our list of job records. */
	for (j = 0; j < opt.job_cnt; j++ ) {
		for (i = 0; i < job_buffer_ptr->record_count; i++) {
			if (job_ptr[i].job_id == opt.job_id[j])
				break;
		}
		if (i >= job_buffer_ptr->record_count) {
			fprintf(stderr, "Job %u not found\n", opt.job_id[j]);
		}
	}

	/* If a list of jobs was given, search the list of all jobs, and
           remove any not found in the given list. */
	if (opt.job_cnt) {
		for (i = 0; i < job_buffer_ptr->record_count; i++) {
			/*skip jobs removed by other filtering*/
			if (job_ptr[i].job_id == 0)
				continue;

			for (j = 0; j < opt.job_cnt; j++ ) {
				if (job_ptr[i].job_id != opt.job_id[j])
					continue;
				if (opt.interactive && 
				    (_confirmation(i, opt.step_id[j]) == 0)) {
					job_ptr[i].job_id = 0;
				}
				break;
			}
			if (j >= opt.job_cnt)   /* not found */
				job_ptr[i].job_id = 0;
		}
	}

	/* Spawn a thread to cancel each job or job step marked for cancellation */
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_ptr[i].job_id == 0) 
			continue;
		if (opt.job_cnt == 0 && opt.interactive && 
		    (_confirmation(i, SLURM_BATCH_SCRIPT) == 0))
			continue;

		cancel_info = (job_cancel_info_t *)malloc(sizeof(job_cancel_info_t));
		cancel_info->job_id  = job_ptr[i].job_id;
		cancel_info->sig     = opt.signal;
		cancel_info->num_active_threads = &num_active_threads;
		cancel_info->num_active_threads_lock = &num_active_threads_lock;
		cancel_info->num_active_threads_cond = &num_active_threads_cond;

		pthread_mutex_lock( &num_active_threads_lock );
		num_active_threads++;
		while (num_active_threads > MAX_THREADS) {
			pthread_cond_wait( &num_active_threads_cond,
					   &num_active_threads_lock );
		}
		pthread_mutex_unlock( &num_active_threads_lock );

		err = pthread_create(&dummy, &attr, 
				     _cancel_job_id,
				     cancel_info);
		if (err)
			_cancel_job_id(cancel_info);
	}

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
				error_code = slurm_signal_job_step(job_id,
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
	   to keep track of it, but don't destroy the mutex and condition 
	   variables contained. */ 
	pthread_mutex_lock(   cancel_info->num_active_threads_lock );
	(*(cancel_info->num_active_threads))--;
	pthread_cond_signal(  cancel_info->num_active_threads_cond );
	pthread_mutex_unlock( cancel_info->num_active_threads_lock );

	free(cancel_info);
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

	if (sig == (uint16_t)-1)
		sig = SIGKILL;

	for (i=0; i<MAX_CANCEL_RETRY; i++) {
		if (sig == SIGKILL)
			verbose("Terminating step %u.%u", job_id, step_id);
		else {
			verbose("Signal %u to step %u.%u", 
				sig, job_id, step_id);
		}

		if (opt.ctld)
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
	   to keep track of it, but don't destroy the mutex and condition 
	   variables contained. */ 
	pthread_mutex_lock(   cancel_info->num_active_threads_lock );
	(*(cancel_info->num_active_threads))--;
	pthread_cond_signal(  cancel_info->num_active_threads_cond );
	pthread_mutex_unlock( cancel_info->num_active_threads_lock );

	free(cancel_info);
	return NULL;
}

/* _confirmation - Confirm job cancel request interactively */
static int 
_confirmation (int i, uint32_t step_id)
{
	char in_line[128];
	job_info_t *job_ptr = NULL;

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

		fgets (in_line, sizeof (in_line), stdin);
		if ((in_line[0] == 'y') || (in_line[0] == 'Y'))
			return 1;
		if ((in_line[0] == 'n') || (in_line[0] == 'N'))
			return 0;
	}

}
