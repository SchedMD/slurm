/*****************************************************************************\
 *  priority_none.c - NO-OP slurm priority plugin.
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
static pthread_mutex_t decay_lock = PTHREAD_MUTEX_INITIALIZER;
static int running_decay = 0;

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
	xstrcat(state_file, "/priority_last_decay_ran.new");

	lock_state_files();
	state_fd = creat(new_file, 0600);
	if (state_fd < 0) {
		error("Can't save state, create file %s error %m",
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

	free_buf(buffer);

	return error_code;
}

/* job_ptr should already have the partition priority and such added
 * here before had we will be adding to it
 */
static int _add_fairshare_priority( struct job_record *job_ptr )
{
	acct_association_rec_t *assoc = 
		(acct_association_rec_t *)job_ptr->assoc_ptr;
	acct_association_rec_t *first_assoc = assoc;
	double usage = 0;

	xassert(root_assoc);

	if(!assoc) {
		error("Job %u has no association.  Unable to "
		      "compute fairshare.");
		job_ptr->priority = NO_VAL;
		return SLURM_ERROR;
	}
	
	while(assoc->parent_assoc_ptr) {
		usage += ((assoc->parent_assoc_ptr->used_shares
			   + assoc->used_shares) / assoc->level_cpu_shares)
			* assoc->cpu_shares;
		assoc = assoc->parent_assoc_ptr;
	}

	if(root_assoc->used_shares)
		usage /= root_assoc->used_shares;

	// Priority is 0 -> 1
	job_ptr->priority += ((first_assoc->norm_shares - usage) + 1) / 2;
	debug("job %u has a priority of %f", 
	      job_ptr->job_id, job_ptr->priority);
	return SLURM_SUCCESS;
}




static void *_decay_thread(void *no_data)
{
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr = NULL;
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
		uint32_t run_delta = 0;

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
			/* 
			 * This means the job is held
			 */ 
			if(job_ptr->priority == NO_VAL)
				continue;
			/* 
			 * This means the job is not eligible yet
			 */ 
			if(!job_ptr->nodes && !job_ptr->db_index)
				continue;

			if(!job_ptr->details) {
				error("priority: job %u does not have a "
				      "details symbol set");
				continue;
			}

			/* apply new usage */
			if(job_ptr->start_time && job_ptr->assoc_ptr) {
				acct_association_rec_t *assoc =
					(acct_association_rec_t *)
					job_ptr->assoc_ptr;
				time_t end_period = start_time;
				if(job_ptr->end_time) 
					end_period = job_ptr->end_time;
				run_delta = end_period - job_ptr->start_time;
				run_delta *= job_ptr->total_procs;
				while(assoc->parent_assoc_ptr) {
					assoc->used_shares += 
						(long double)run_delta;
					assoc = assoc->parent_assoc_ptr;
				}
			}

			if(job_ptr->job_state != JOB_PENDING)
				continue;

			/* figure out the priority */
			job_ptr->priority = 0;

			part_ptr = job_ptr->part_ptr;

			if(part_ptr->priority)
				job_ptr->priority += part_ptr->priority;

			if(job_ptr->assoc_ptr)
				_add_fairshare_priority(job_ptr);		
		}
		unlock_slurmctld(job_write_lock);

	sleep_now:
		last_ran = start_time;

		_write_last_decay_ran(last_ran);

		running_decay = 0;
		slurm_mutex_unlock(&decay_lock);

		/* sleep for 5 minutes */
		tm.tm_sec += DECAY_INTERVAL;
		tm.tm_isdst = -1;
		next_time = mktime(&tm);
		sleep((next_time-start_time));
		start_time = next_time;
		/* repeat ;) */
	}
	return NULL;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	pthread_attr_t thread_attr;

	slurm_attr_init(&thread_attr);
	if (pthread_create(&decay_handler_thread, &thread_attr,
			   _decay_thread, NULL))
		fatal("pthread_create error %m");
	slurm_attr_destroy(&thread_attr);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	/* Daemon termination handled here */
	if(running_decay)
		debug("Waiting for rollup thread to finish.");
	slurm_mutex_lock(&decay_lock);
	pthread_cancel(decay_handler_thread);
	slurm_mutex_unlock(&decay_lock);

	return SLURM_SUCCESS;
}
