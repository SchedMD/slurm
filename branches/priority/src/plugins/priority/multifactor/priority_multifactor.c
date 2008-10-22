/*****************************************************************************\
 *  priority_multifactor.c - slurm multifactor priority plugin.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif
#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_priority.h"
#include "src/common/xstring.h"
#include "src/common/assoc_mgr.h"
#include "src/common/parse_time.h"

#include "src/slurmctld/locks.h"

#define DECAY_INTERVAL 300 /* sleep for this many seconds */

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job completion logging API 
 * matures.
 */
const char plugin_name[]       	= "Priority MULTIFACTOR plugin";
const char plugin_type[]       	= "priority/multifactor";
const uint32_t plugin_version	= 100;

static pthread_t decay_handler_thread;
static pthread_t cleanup_handler_thread;
static pthread_mutex_t decay_lock = PTHREAD_MUTEX_INITIALIZER;
static int running_decay = 0;
static bool favor_small; /* favor small jobs over large */
static uint32_t max_age; /* time when not to add any more
			  * priority to a job if reached */
static uint32_t weight_age; /* weight for age factor */
static uint32_t weight_fs; /* weight for Fairshare factor */
static uint32_t weight_js; /* weight for Job Size factor */
static uint32_t weight_nice; /* weight for Nice factor */
static uint32_t weight_part; /* weight for Partition factor */
static uint32_t weight_qos; /* weight for QOS factor */

static time_t _read_last_decay_ran()
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t last_ran = 0;
	/* read the file */
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/priority_last_decay_ran");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		info("No last decay (%s) to recover", state_file);
		unlock_state_files();
		return 0;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m", 
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	buffer = create_buf(data, data_size);
	safe_unpack_time(&last_ran, buffer);
	free_buf(buffer);
	debug("Last ran decay on jobs at %d", last_ran);

	return last_ran;

unpack_error:
	error("Incomplete priority last decay file returning no last ran");
	free_buf(buffer);
	return 0;

}

static int _write_last_decay_ran(time_t last_ran)
{
	static int high_buffer_size = (1024 * 1024);
	int error_code = SLURM_SUCCESS;
	int state_fd;
	char *old_file, *new_file, *state_file;
	Buf buffer = init_buf(BUF_SIZE);

	pack_time(last_ran, buffer);

	/* read the file */
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/priority_last_decay_ran.old");
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/priority_last_decay_ran");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/priority_last_decay_ran.new");

	lock_state_files();
	state_fd = creat(new_file, 0600);
	if (state_fd < 0) {
		error("Can't save decay state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(state_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(state_fd);
		close(state_fd);
	}

	if (error_code != SLURM_SUCCESS)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		(void) link(state_file, old_file);
		(void) unlink(state_file);
		(void) link(new_file, state_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(state_file);
	xfree(new_file);

	unlock_state_files();
	info("done writing time %d", last_ran);
	free_buf(buffer);

	return error_code;
}

/* job_ptr should already have the partition priority and such added
 * here before had we will be adding to it
 */
static double _get_fairshare_priority( struct job_record *job_ptr )
{
	acct_association_rec_t *assoc = 
		(acct_association_rec_t *)job_ptr->assoc_ptr;
	acct_association_rec_t *first_assoc = assoc;
	long double usage = 0;
	long double tmp_usage1 = 0;
	long double tmp_usage2 = 0;

	xassert(root_assoc);

	if(!assoc) {
		error("Job %u has no association.  Unable to "
		      "compute fairshare.");
		job_ptr->priority = 0;
		return NO_VAL;
	}
	
	/* only go to the root since we do things different at the
	 * top */
	while(assoc->parent_assoc_ptr) {
		char *tmp_char = assoc->user;
		if(!tmp_char)
			tmp_char = assoc->acct;
		tmp_usage1 = ((assoc->parent_assoc_ptr->used_shares
			       + assoc->used_shares) / assoc->level_cpu_shares)
			* assoc->cpu_shares;
		tmp_usage2 = usage;
		usage += tmp_usage1;
		info("at %s ((%Lf + %Lf) / %Lf) * %Lf = %Lf + %Lf = %Lf",
		     tmp_char, 
		     assoc->parent_assoc_ptr->used_shares,
		     assoc->used_shares, assoc->level_cpu_shares,
		     assoc->cpu_shares, tmp_usage1, tmp_usage2, usage);
		assoc = assoc->parent_assoc_ptr;
	}

	tmp_usage2 = usage;
/* 	if(root_assoc->used_shares) */
/* 		usage /= root_assoc->used_shares; */
/* 	info("Normilized usage = %Lf / %Lf = %Lf",  */
/* 	     tmp_usage2, root_assoc->used_shares, usage); */
	if(root_assoc->cpu_shares)
		usage /= root_assoc->cpu_shares;
	info("Normilized usage = %Lf / %Lf = %Lf", 
	     tmp_usage2, root_assoc->cpu_shares, usage);
	// Priority is 0 -> 1
	tmp_usage1 = ((first_assoc->norm_shares - usage) + 1) / 2;
	info("((%f - %Lf) + 1) / 2 = %Lf", first_assoc->norm_shares,
	     usage, tmp_usage1);
	debug("job %u has a fairshare priority of %Lf", 
	      job_ptr->job_id, tmp_usage1);
	return (double)tmp_usage1;
}

static uint32_t _get_priority_internal(time_t start_time,
				       struct job_record *job_ptr)
{
	double priority = 0;
	acct_qos_rec_t *qos_ptr = (acct_qos_rec_t *)job_ptr->qos_ptr;

	if(job_ptr->direct_set_prio)
		return job_ptr->priority;

	if(!job_ptr->details) {
		error("_get_priority_internal: job %u does not have a "
		      "details symbol set, can't set priority");
		return 0;
	}
	/* 
	 * This means the job is not eligible yet
	 */ 
	if(!job_ptr->details->begin_time > start_time)
		return 1;

	/* figure out the priority */
	
	if(weight_age) {
		uint32_t diff = start_time - job_ptr->details->begin_time; 
		double norm_diff = 1;

		if(diff < max_age)
			norm_diff = (double)diff / (double)max_age;
		
		if(norm_diff > 0) 
			priority += norm_diff * (double)weight_age;
	}
	
	if(job_ptr->assoc_ptr && weight_fs) {
		info("getting fairshare");
		priority += _get_fairshare_priority(job_ptr) 
			* (double)weight_fs;
	} else {
		info("no assoc ptr");
	}
	
	if(weight_js) {
		double norm_js = 0;
		if(favor_small) {
			norm_js = (double)(node_record_count 
					   - job_ptr->details->min_nodes)
				/ (double)node_record_count;
		} else 
			norm_js = (double)job_ptr->details->min_nodes
				/ (double)node_record_count;
		if(norm_js > 0)
			priority += norm_js * (double)weight_js;
	}
	
	if(job_ptr->part_ptr && job_ptr->part_ptr->priority && weight_part) {
		priority += job_ptr->part_ptr->norm_priority 
			* (double)weight_part;
	}
	
	if(qos_ptr && qos_ptr->priority && weight_qos) {
		priority += qos_ptr->norm_priority 
			* (double)weight_qos;
	}

	priority -= ((double)job_ptr->details->nice - (double)NICE_OFFSET);

	if(priority < 1) 
		priority = 1;

	return (uint32_t)priority;
}

static void *_decay_thread(void *no_data)
{
	struct job_record *job_ptr = NULL;
	ListIterator itr;
	time_t start_time = time(NULL);
	time_t next_time;
/* 	int sigarray[] = {SIGUSR1, 0}; */
	struct tm tm;
	time_t last_ran = 0;
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if(!localtime_r(&start_time, &tm)) {
		fatal("_decay_thread: "
		      "Couldn't get localtime for rollup handler %d",
		      start_time);
		return NULL;
	}

	last_ran = _read_last_decay_ran();

	while(1) {
		int run_delta = 0;

		slurm_mutex_lock(&decay_lock);
		running_decay = 1;

		if(!last_ran) 
			goto sleep_now;
		else
			run_delta = (start_time - last_ran);
				
		if(run_delta <= 0)
			goto sleep_now;
		/* first apply decay to used time */
		if(assoc_mgr_apply_decay(run_delta) != SLURM_SUCCESS) {
			error("problem applying decay");
			break;
		}

		lock_slurmctld(job_write_lock);
		itr = list_iterator_create(job_list);
		while ((job_ptr = list_next(itr))) {
			/* apply new usage */
			if(job_ptr->start_time && job_ptr->assoc_ptr) {
				acct_association_rec_t *assoc =	
					(acct_association_rec_t *)
					job_ptr->assoc_ptr;
				time_t end_period = start_time;
				
				if(end_period > job_ptr->end_time) {
					info("job has ended %u", 
					     job_ptr->end_time);
					end_period = job_ptr->end_time;
				}
				run_delta = end_period - job_ptr->start_time;

				info("got job %u run_delta is %d-%d = %d",
				     job_ptr->job_id, end_period,
				     job_ptr->start_time, run_delta);
				if(run_delta < 0) {
					error("priority: some how we have "
					      "negative time %d for job %u",
					      run_delta, job_ptr->job_id);
					continue;
				} else if(!run_delta)
					run_delta = 1;

				info("run_delta is %d", run_delta);

				run_delta *= job_ptr->total_procs;
				info("run_delta is now %d", run_delta);
				while(assoc) {
					assoc->used_shares +=
						(long double)run_delta;
					info("adding %u new usage to %u"
					     "(acct='%s') "
					     "used_shares is now %Lf",
					     run_delta, assoc->id, assoc->acct,
					     assoc->used_shares);
					assoc = assoc->parent_assoc_ptr;
				}
			}

			/* 
			 * This means the job is held, 0, or a system
			 * hold, 1. Continue also if the job is not
			 * pending.  There is no reason to set the
			 * priority if the job isn't pending.
			 */ 
			if((job_ptr->priority <= 1) || !IS_JOB_PENDING(job_ptr))
				continue;
	
			job_ptr->priority =
				_get_priority_internal(start_time, job_ptr);

			debug("priority for job %u is now %u", 
			      job_ptr->job_id, job_ptr->priority);
		}
		unlock_slurmctld(job_write_lock);

	sleep_now:
		last_ran = start_time;

		_write_last_decay_ran(last_ran);

		running_decay = 0;
		slurm_mutex_unlock(&decay_lock);

		/* sleep for DECAY_INTERVAL secs */
		tm.tm_sec += DECAY_INTERVAL;
		tm.tm_isdst = -1;
		next_time = mktime(&tm);
		sleep((next_time-start_time));
		start_time = next_time;
		/* repeat ;) */
	}
	return NULL;
}

static void *_cleanup_thread(void *no_data)
{
	pthread_join(decay_handler_thread, NULL);
	return NULL;
}


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	pthread_attr_t thread_attr;

	favor_small = slurm_get_priority_favor_small();
	max_age = slurm_get_priority_max_age();
	weight_age = slurm_get_priority_weight_age(); 
	weight_fs = slurm_get_priority_weight_fairshare(); 
	weight_js = slurm_get_priority_weight_job_size();
	weight_nice = slurm_get_priority_weight_nice();
	weight_part = slurm_get_priority_weight_partition();
	weight_qos = slurm_get_priority_weight_qos();
	info("Max Age is %u", max_age);
	info("Weight Age is %u", weight_age);
	info("Weight Fairshare is %u", weight_fs);
	info("Weight JobSize is %u", weight_js);
	info("Weight Part is %u", weight_part);
	info("Weight QOS is %u", weight_qos);

	slurm_attr_init(&thread_attr);
	if (pthread_create(&decay_handler_thread, &thread_attr,
			   _decay_thread, NULL))
		fatal("pthread_create error %m");

	/* This is here to join the decay thread so we don't core
	   dump if in the sleep, since there is no other place to join
	   we have to create another thread to do it.
	*/
	slurm_attr_init(&thread_attr);
	if (pthread_create(&cleanup_handler_thread, &thread_attr,
			   _cleanup_thread, NULL))
		fatal("pthread_create error %m");
	
	slurm_attr_destroy(&thread_attr);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	/* Daemon termination handled here */
	if(running_decay)
		debug("Waiting for decay thread to finish.");

	slurm_mutex_lock(&decay_lock);
	
	/* cancel the decay thread and then join the cleanup thread */
	pthread_cancel(decay_handler_thread);
	pthread_join(cleanup_handler_thread, NULL);

	slurm_mutex_unlock(&decay_lock);

	return SLURM_SUCCESS;
}

extern uint32_t priority_p_set(uint32_t last_prio, struct job_record *job_ptr)
{
	return _get_priority_internal(time(NULL), job_ptr);
}
