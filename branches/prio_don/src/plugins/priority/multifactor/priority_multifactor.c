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
#include <math.h>

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
static bool running_decay = 0, reconfig = 0, calc_fairshare = 1;
static bool favor_small; /* favor small jobs over large */
static uint32_t max_age; /* time when not to add any more
			  * priority to a job if reached */
static uint32_t weight_age; /* weight for age factor */
static uint32_t weight_fs; /* weight for Fairshare factor */
static uint32_t weight_js; /* weight for Job Size factor */
static uint32_t weight_part; /* weight for Partition factor */
static uint32_t weight_qos; /* weight for QOS factor */
static long double small_usage; /* amount of usage to add if multiple
				 * jobs are scheduled during the same
				 * decay period for the same association 
				 */
extern int priority_p_set_max_cluster_usage(uint32_t procs, uint32_t half_life);

/*
 * apply decay factor to all associations usage_raw
 * IN: decay_factor - decay to be applied to each associations' used
 * shares.  This should already be modified with the amount of delta
 * time from last application..
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
static int _apply_decay(double decay_factor)
{
	ListIterator itr = NULL;
	acct_association_rec_t *assoc = NULL;

	if(!calc_fairshare)
		return SLURM_SUCCESS;

	xassert(assoc_mgr_association_list);

	if(!decay_factor)
		return SLURM_ERROR;
	
	slurm_mutex_lock(&assoc_mgr_association_lock);
	itr = list_iterator_create(assoc_mgr_association_list);
	while((assoc = list_next(itr))) {
		if (assoc == assoc_mgr_root_assoc)
			continue;
		assoc->usage_raw *= decay_factor;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&assoc_mgr_association_lock);

	return SLURM_SUCCESS;
}

/* you should test for assoc == NULL before this function */
static void _set_assoc_usage(acct_association_rec_t *assoc)
{
	char *child = "account";
	char *child_str = assoc->acct;

	if(assoc->user) {
		child = "user";
		child_str = assoc->user;
	}

	xassert(assoc_mgr_root_assoc);
	xassert(assoc_mgr_root_assoc->usage_raw);
	xassert(assoc->parent_assoc_ptr);
	
	assoc->usage_norm = assoc->usage_raw / assoc_mgr_root_assoc->usage_raw;
	debug4("Normalized usage for %s %s off %s %Lf / %Lf = %Lf",
	       child, child_str, assoc->parent_assoc_ptr->acct,
	       assoc->usage_raw, assoc_mgr_root_assoc->usage_raw,
	       assoc->usage_norm);
	/* This is needed in case someone changes the half-life on the
	   fly and now we have used more time than is available under
	   the new config */
	if (assoc->usage_norm > 1.0) 
		assoc->usage_norm = 1.0;
	
	if (assoc->parent_assoc_ptr == assoc_mgr_root_assoc) {
		assoc->usage_efctv = assoc->usage_norm;
		debug4("Effective usage for %s %s off %s %Lf %Lf",
		       child, child_str, assoc->parent_assoc_ptr->acct,
		       assoc->usage_efctv, assoc->usage_norm);
	} else {
		assoc->usage_efctv = assoc->usage_norm +
			((assoc->parent_assoc_ptr->usage_efctv -
			  assoc->usage_norm) *
			 assoc->shares_raw / 
			 (long double)assoc->level_shares);
		debug4("Effective usage for %s %s off %s "
		       "%Lf + ((%Lf - %Lf) * %d / %d) = %Lf",
		       child, child_str, assoc->parent_assoc_ptr->acct,
		       assoc->usage_norm,
		       assoc->parent_assoc_ptr->usage_efctv,
		       assoc->usage_norm, assoc->shares_raw,
		       assoc->level_shares, assoc->usage_efctv);
	}
}

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
	debug5("Last ran decay on jobs at %d", last_ran);

	return last_ran;

unpack_error:
	error("Incomplete priority last decay file returning no last ran");
	free_buf(buffer);
	return 0;

}

static int _write_last_decay_ran(time_t last_ran)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = BUF_SIZE;
	int error_code = SLURM_SUCCESS;
	int state_fd;
	char *old_file, *new_file, *state_file;
	Buf buffer = init_buf(high_buffer_size);

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
	debug5("done writing time %d", last_ran);
	free_buf(buffer);

	return error_code;
}

/* This should initially get the childern list from
 * assoc_mgr_root_assoc.  Since our algorythm goes from top down we
 * calculate all the non-user associations now.  When a user submits a
 * job, that norm_fairshare is calculated.  Here we will set the
 * usage_efctv to NO_VAL for users to not have to calculate a bunch
 * of things that will never be used. 
 *
 * NOTE: acct_mgr_association_lock must be locked before this is called.
 */
static int _set_children_usage_efctv(List childern_list)
{
	acct_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;

	if(!childern_list || !list_count(childern_list)) 
		return SLURM_SUCCESS;

	itr = list_iterator_create(childern_list);
	while((assoc = list_next(itr))) {
		if(assoc->user) {
			assoc->usage_efctv = (long double)NO_VAL;
			continue;
		}
		_set_assoc_usage(assoc);
		_set_children_usage_efctv(assoc->childern_list);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

/* job_ptr should already have the partition priority and such added
 * here before had we will be adding to it
 */
static double _get_fairshare_priority( struct job_record *job_ptr )
{
	acct_association_rec_t *assoc = 
		(acct_association_rec_t *)job_ptr->assoc_ptr;
	double fs_priority = 0.0;

	if(!calc_fairshare)
		return 0;

	if(!assoc) {
		error("Job %u has no association.  Unable to "
		      "compute fairshare.");
		return 0;
	}

	slurm_mutex_lock(&assoc_mgr_association_lock);
	if(assoc->usage_efctv == (long double)NO_VAL) {
		_set_assoc_usage(assoc);
	} else {
		/* Add a tiny amount so the next job will get a lower
		   priority than the previous jobs if they are
		   submitted during the polling period.   */
		assoc->usage_efctv += small_usage;
		/* If the user submits a bunch of jobs and then
		   cancels the jobs before they run the priority will
		   not be reset until the decay loop happens.
		*/
	}

	// Priority is 0 -> 1
	fs_priority =
		(assoc->shares_norm - (double)assoc->usage_efctv + 1.0) / 2.0;
	info("Fairshare priority for user %s in acct %s"
	       "((%f - %Lf) + 1) / 2 = %f",
	       assoc->user, assoc->acct, assoc->shares_norm,
	       assoc->usage_efctv, fs_priority);

	slurm_mutex_unlock(&assoc_mgr_association_lock);

	debug3("job %u has a fairshare priority of %f",
	      job_ptr->job_id, fs_priority);

	return fs_priority;
}

static uint32_t _get_priority_internal(time_t start_time,
				       struct job_record *job_ptr)
{
	double age_priority = 0.0;
	double fs_priority = 0.0;
	double js_priority = 0.0;
	double part_priority = 0.0;
	double priority = 0.0;
	double qos_priority = 0.0;
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

		if(norm_diff > 0) {
			age_priority = norm_diff * (double)weight_age;
			debug3("Weighted Age priority is %f * %u = %.2f", 
			       norm_diff, weight_age, age_priority);
		}
	}

	if(job_ptr->assoc_ptr && weight_fs) {
		double fs_temp = _get_fairshare_priority(job_ptr);
		fs_priority = fs_temp * (double)weight_fs;
		debug3("Weighted Fairshare priority is %f * %u = %.2f",
		       fs_temp, weight_fs, fs_priority);
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
		if(norm_js > 0) {
			js_priority = norm_js * (double)weight_js;
			debug3("Weighted JobSize priority is %f * %u = %.2f", 
			       norm_js, weight_js, js_priority);
		}
	}

	if(job_ptr->part_ptr && job_ptr->part_ptr->priority && weight_part) {
		part_priority = job_ptr->part_ptr->norm_priority 
			* (double)weight_part;
		debug3("Weighted Partition priority is %f * %u = %.2f", 
		       job_ptr->part_ptr->norm_priority, weight_part,
		       part_priority);
	}

	if(qos_ptr && qos_ptr->priority && weight_qos) {
		qos_priority = qos_ptr->norm_priority 
			* (double)weight_qos;
		debug3("Weighted QOS priority is %f * %u = %.2f", 
		       qos_ptr->norm_priority, weight_qos,
		       qos_priority);
	}

	priority = age_priority + fs_priority + js_priority + part_priority +
		qos_priority;
	debug3("Job %u priority: %.2f + %.2f + %.2f + %.2f + %.2f = %.2f",
	       job_ptr->job_id, age_priority, fs_priority, js_priority,
	       part_priority, qos_priority, priority);

	priority -= ((double)job_ptr->details->nice - (double)NICE_OFFSET);
	debug3("Nice offset is %u", job_ptr->details->nice - NICE_OFFSET);

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
	double decay_factor = 
		1 - (0.693 / (double)slurm_get_priority_decay_hl());

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
		double real_decay = 0.0;

		slurm_mutex_lock(&decay_lock);
		running_decay = 1;

		/* If reconfig is called handle all that happens
		   outside of the loop here */
		if(reconfig) {
			decay_factor = 
				1 - (0.693 
				     / (double)slurm_get_priority_decay_hl());
			reconfig = 0;
		}

		if(!last_ran) 
			goto sleep_now;
		else
			run_delta = (start_time - last_ran);

		if(run_delta <= 0)
			goto sleep_now;

		real_decay = pow(decay_factor, (double)run_delta);

		debug3("Decay factor over %d seconds goes from %.15f -> %.15f",
		       run_delta, decay_factor, real_decay);

		/* first apply decay to used time */
		if(_apply_decay(real_decay) != SLURM_SUCCESS) {
			error("problem applying decay");
			running_decay = 0;
			slurm_mutex_unlock(&decay_lock);
			break;
		}

		lock_slurmctld(job_write_lock);
		itr = list_iterator_create(job_list);
		while ((job_ptr = list_next(itr))) {
			/* apply new usage */
			if(!IS_JOB_PENDING(job_ptr) &&
			   job_ptr->start_time && job_ptr->assoc_ptr) {
				acct_qos_rec_t *qos = 
					(acct_qos_rec_t *)job_ptr->qos_ptr;
				acct_association_rec_t *assoc =	
					(acct_association_rec_t *)
					job_ptr->assoc_ptr;
				time_t start_period = last_ran;
				time_t end_period = start_time;

				if(job_ptr->start_time > start_period) 
					start_period = job_ptr->start_time;

				if(job_ptr->end_time 
				   && (end_period > job_ptr->end_time)) 
					end_period = job_ptr->end_time;

				run_delta = (int)end_period - (int)start_period;

				/* job already has been accounted for
				   go to next */
				if(run_delta < 1) 
					continue;

				debug4("job %u ran for %d seconds",
				       job_ptr->job_id, run_delta);

				/* figure out the decayed new usage to
				   add */
				real_decay = ((double)run_delta 
					      * (double)job_ptr->total_procs)
					* pow(decay_factor, (double)run_delta);

				/* now apply the usage factor for this
				   qos */
				if(qos && qos->usage_factor > 0)
					real_decay *= qos->usage_factor;

				slurm_mutex_lock(&assoc_mgr_association_lock);
				while(assoc) {
					assoc->usage_raw +=
						(long double)real_decay;
					debug4("adding %f new usage to "
					       "assoc %u (user='%s' acct='%s') "
					       "raw usage is now %Lf",
					       real_decay, assoc->id, 
					       assoc->user, assoc->acct,
					       assoc->usage_raw);
					assoc = assoc->parent_assoc_ptr;
					if (assoc == assoc_mgr_root_assoc)
						break;
				}
				slurm_mutex_unlock(&assoc_mgr_association_lock);
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

			debug2("priority for job %u is now %u", 
			       job_ptr->job_id, job_ptr->priority);
		}
		list_iterator_destroy(itr);
		unlock_slurmctld(job_write_lock);

		/* now calculate all the normalized usage here */
		slurm_mutex_lock(&assoc_mgr_association_lock);
		_set_children_usage_efctv(assoc_mgr_root_assoc->childern_list);
		slurm_mutex_unlock(&assoc_mgr_association_lock);
	
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

static void _internal_setup()
{
	favor_small = slurm_get_priority_favor_small();
	max_age = slurm_get_priority_max_age();
	weight_age = slurm_get_priority_weight_age(); 
	weight_fs = slurm_get_priority_weight_fairshare(); 
	weight_js = slurm_get_priority_weight_job_size();
	weight_part = slurm_get_priority_weight_partition();
	weight_qos = slurm_get_priority_weight_qos();

	small_usage = 2.0 / (long double) weight_fs;

	debug3("priority: Max Age is %u", max_age);
	debug3("priority: Weight Age is %u", weight_age);
	debug3("priority: Weight Fairshare is %u", weight_fs);
	debug3("priority: Weight JobSize is %u", weight_js);
	debug3("priority: Weight Part is %u", weight_part);
	debug3("priority: Weight QOS is %u", weight_qos);
	debug3("priority: Small Usage is %Lf", small_usage);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	pthread_attr_t thread_attr;
	char *temp = NULL;

	_internal_setup();

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_accounting_storage_type();
	if(strcasecmp(temp, "accounting_storage/slurmdbd")
	   && strcasecmp(temp, "accounting_storage/mysql")) {
		error("You are not running a supported "
		      "accounting_storage plugin\n(%s).\n"
		      "Fairshare can only be calculated with either "
		      "'accounting_storage/slurmdbd' "
		      "or 'accounting_storage/mysql' enabled.  "
		      "If you want multifactor priority without fairshare "
		      "ignore this message.\n",
		      temp);
		calc_fairshare = 0;
	} else {
		if(!cluster_procs)
			fatal("We need to have a cluster cpu count "
			      "before we can init the priority/multifactor "
			      "plugin");
		priority_p_set_max_cluster_usage(cluster_procs,
					  slurm_get_priority_decay_hl());
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
	}
	xfree(temp);

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
	if(decay_handler_thread)
		pthread_cancel(decay_handler_thread);
	if(cleanup_handler_thread)
		pthread_join(cleanup_handler_thread, NULL);

	slurm_mutex_unlock(&decay_lock);

	return SLURM_SUCCESS;
}

extern uint32_t priority_p_set(uint32_t last_prio, struct job_record *job_ptr)
{
	uint32_t priority = _get_priority_internal(time(NULL), job_ptr);

	debug2("initial priority for job %u is %u", job_ptr->job_id, priority);

	return priority;
}

extern void priority_p_reconfig()
{
	reconfig = 1;
	_internal_setup();
	debug2("%s reconfigured", plugin_name);
	
	return;
}

extern int priority_p_set_max_cluster_usage(uint32_t procs, uint32_t half_life)
{
	static uint32_t last_procs = 0;
	static uint32_t last_half_life = 0;

	if(!calc_fairshare)
		return SLURM_SUCCESS;

	/* No need to do this if nothing has changed so just return */
	if((procs == last_procs) && (half_life == last_half_life))
		return SLURM_SUCCESS;

	xassert(assoc_mgr_root_assoc);

	last_procs = procs;
	last_half_life = half_life;

	/* get the total decay for the entire cluster */
	assoc_mgr_root_assoc->usage_raw =
		(long double)procs * (long double)half_life * (long double)2;
	assoc_mgr_root_assoc->usage_norm = 1.0;
	debug3("Total possible cpu usage for half_life of %d secs "
	       "on the system is %.0Lf",
	       half_life, assoc_mgr_root_assoc->usage_raw);

	return SLURM_SUCCESS;
}
