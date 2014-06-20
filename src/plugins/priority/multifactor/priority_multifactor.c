/*****************************************************************************\
 *  priority_multifactor.c - slurm multifactor priority plugin.
 *****************************************************************************
 *
 *  LEVEL_BASED contributed by Brigham Young University
 *  Authors: Ryan Cox <ryan_cox@byu.edu>, Levi Morrison <levi_morrison@byu.edu>
 *
 *  Copyright (C) 2012  Aalto University
 *  Written by Janne Blomqvist <janne.blomqvist@aalto.fi>
 *
 *  Based on priority_multifactor.c, whose copyright information is
 *  reproduced below:
 *
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_FLOAT_H
#  include <float.h>
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
#if HAVE_VALUES_H
#  include <values.h>
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>

#include <math.h>
#include "slurm/slurm_errno.h"

#include "src/common/slurm_priority.h"
#include "src/common/xstring.h"
#include "src/common/assoc_mgr.h"
#include "src/common/parse_time.h"

#include "src/slurmctld/locks.h"

#define SECS_PER_DAY	(24 * 60 * 60)
#define SECS_PER_WEEK	(7 * SECS_PER_DAY)

#define MIN_USAGE_FACTOR 0.01

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
void *acct_db_conn  __attribute__((weak_import)) = NULL;
uint32_t cluster_cpus __attribute__((weak_import)) = NO_VAL;
List job_list  __attribute__((weak_import)) = NULL;
time_t last_job_update __attribute__((weak_import)) = (time_t) 0;
uint16_t part_max_priority __attribute__((weak_import)) = 0;
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
#else
void *acct_db_conn = NULL;
uint32_t cluster_cpus = NO_VAL;
List job_list = NULL;
time_t last_job_update = (time_t) 0;
uint16_t part_max_priority = 0;
slurm_ctl_conf_t slurmctld_conf;
#endif

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
 * minimum version for their plugins as the job completion logging API
 * matures.
 */
const char plugin_name[]	= "Priority MULTIFACTOR plugin";
const char plugin_type[]	= "priority/multifactor";
const uint32_t plugin_version	= 100;

static pthread_t decay_handler_thread;
static pthread_t cleanup_handler_thread;
static pthread_mutex_t decay_lock = PTHREAD_MUTEX_INITIALIZER;
static bool running_decay = 0, reconfig = 0,
	calc_fairshare = 1, priority_debug = 0;
static bool favor_small; /* favor small jobs over large */
static uint16_t damp_factor = 1;  /* weight for age factor */
static uint32_t max_age; /* time when not to add any more
			  * priority to a job if reached */
static uint16_t enforce;     /* AccountingStorageEnforce */
static uint32_t weight_age;  /* weight for age factor */
static uint32_t weight_fs;   /* weight for Fairshare factor */
static uint32_t weight_js;   /* weight for Job Size factor */
static uint32_t weight_part; /* weight for Partition factor */
static uint32_t weight_qos;  /* weight for QOS factor */
static uint32_t flags;       /* Priority Flags */
static uint32_t prevflags;    /* Priority Flags before _internal_setup() resets
			      * flags after a reconfigure */
static uint32_t max_tickets; /* Maximum number of tickets given to a
			      * user. Protected by assoc_mgr lock. */
static time_t g_last_ran = 0; /* when the last poll ran */
static double decay_factor = 1; /* The decay factor when decaying time. */

extern void priority_p_set_assoc_usage(slurmdb_association_rec_t *assoc);
extern double priority_p_calc_fs_factor(long double usage_efctv,
					long double shares_norm);

extern uint16_t part_max_priority;

/* LEVEL_BASED */
static void _level_based_calc_children_fs(List children_list,
					    List users,
					     uint16_t assoc_level);

static uint16_t priority_levels;	/* How many levels to care about */
static uint32_t bucket_width_in_bits;	/* How many bits available for
					 * each level */
static uint32_t unused_bucket_bits;	/* Unused bits in priority_fs_raw due
					 * to priority_levels not being evenly
					 * divisible into 64 */
static uint64_t bucket_max;		/* Maximum integer that can be stored
					 * in a bucket */


/*
 * apply decay factor to all associations usage_raw
 * IN: real_decay - decay to be applied to each associations' used
 * shares.  This should already be modified with the amount of delta
 * time from last application..
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
static int _apply_decay(double real_decay)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	/* continue if real_decay is 0 or 1 since that doesn't help
	   us at all. 1 means no decay and 0 will just zero
	   everything out so don't waste time doing it */
	if (!real_decay)
		return SLURM_ERROR;
	else if (!calc_fairshare || (real_decay == 1))
		return SLURM_SUCCESS;

	assoc_mgr_lock(&locks);

	xassert(assoc_mgr_association_list);
	xassert(assoc_mgr_qos_list);

	itr = list_iterator_create(assoc_mgr_association_list);
	/* We want to do this to all associations including
	   root.  All usage_raws are calculated from the bottom up.
	*/
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw *= real_decay;
		assoc->usage->grp_used_wall *= real_decay;
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((qos = list_next(itr))) {
		qos->usage->usage_raw *= real_decay;
		qos->usage->grp_used_wall *= real_decay;
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/*
 * reset usage_raw, and grp_used_wall on all associations
 * This should be called every PriorityUsageResetPeriod
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
static int _reset_usage(void)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!calc_fairshare)
		return SLURM_SUCCESS;

	assoc_mgr_lock(&locks);

	xassert(assoc_mgr_association_list);

	itr = list_iterator_create(assoc_mgr_association_list);
	/* We want to do this to all associations including root.
	 * All usage_raws are calculated from the bottom up.
	*/
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw = 0;
		assoc->usage->grp_used_wall = 0;
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((qos = list_next(itr))) {
		qos->usage->usage_raw = 0;
		qos->usage->grp_used_wall = 0;
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

static void _read_last_decay_ran(time_t *last_ran, time_t *last_reset)
{
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;

	xassert(last_ran);
	xassert(last_reset);

	(*last_ran) = 0;
	(*last_reset) = 0;

	/* read the file */
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/priority_last_decay_ran");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		info("No last decay (%s) to recover", state_file);
		unlock_state_files();
		return;
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
	safe_unpack_time(last_ran, buffer);
	safe_unpack_time(last_reset, buffer);
	free_buf(buffer);
	if (priority_debug)
		info("Last ran decay on jobs at %ld", (long)*last_ran);

	return;

unpack_error:
	error("Incomplete priority last decay file returning");
	free_buf(buffer);
	return;

}

static int _write_last_decay_ran(time_t last_ran, time_t last_reset)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = BUF_SIZE;
	int error_code = SLURM_SUCCESS;
	int state_fd;
	char *old_file, *new_file, *state_file;
	Buf buffer;

	if (!strcmp(slurmctld_conf.state_save_location, "/dev/null")) {
		error("Can not save priority state information, "
		      "StateSaveLocation is /dev/null");
		return error_code;
	}

	buffer = init_buf(high_buffer_size);
	pack_time(last_ran, buffer);
	pack_time(last_reset, buffer);

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
		if (link(state_file, old_file))
			debug3("unable to create link for %s -> %s: %m",
			       state_file, old_file);
		(void) unlink(state_file);
		if (link(new_file, state_file))
			debug3("unable to create link for %s -> %s: %m",
			       new_file, state_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(state_file);
	xfree(new_file);

	unlock_state_files();
	debug4("done writing time %ld", (long)last_ran);
	free_buf(buffer);

	return error_code;
}


/* Set the effective usage of a node. */
static void _ticket_based_set_usage_efctv(slurmdb_association_rec_t *assoc)
{
	long double min_shares_norm;
	slurmdb_association_rec_t *assoc2 = assoc;

	if ((assoc->shares_raw == SLURMDB_FS_USE_PARENT)
	   && assoc->usage->parent_assoc_ptr) {
		/* This function needs to find the real parent because
		 * shares_raw needs to be a useful value, not
		 * SLURMDB_FS_USE_PARENT */
		assoc2 = find_real_parent(assoc);
		assoc->usage->shares_norm =
			assoc2->usage->shares_norm;
		assoc->usage->usage_norm =
			assoc2->usage->usage_norm;
	}

	if (assoc2->usage->level_shares) {
		min_shares_norm = (long double) MIN_USAGE_FACTOR
			* assoc2->shares_raw / assoc2->usage->level_shares;
		if (assoc2->usage->usage_norm > min_shares_norm)
			assoc->usage->usage_efctv = assoc2->usage->usage_norm;
		else
			assoc->usage->usage_efctv = min_shares_norm;
	} else
		assoc->usage->usage_efctv = assoc2->usage->usage_norm;
}


/* This should initially get the children list from assoc_mgr_root_assoc.
 * Since our algorithm goes from top down we calculate all the non-user
 * associations now.  When a user submits a job, that norm_fairshare is
 * calculated.  Here we will set the usage_efctv to NO_VAL for users to not
 * have to calculate a bunch of things that will never be used, except it will
 * be calculated for LEVEL_BASED.
 *
 * NOTE: acct_mgr_association_lock must be locked before this is called.
 */
static int _set_children_usage_efctv(List children_list)
{
	slurmdb_association_rec_t *assoc = NULL;
	ListIterator itr = NULL;

	if (!children_list || !list_count(children_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		if (assoc->user) {
			/* LEVEL_BASED must calculate this for users */
			if(flags & PRIORITY_FLAGS_LEVEL_BASED)
				priority_p_set_assoc_usage(assoc);
			else
				assoc->usage->usage_efctv = (long double)NO_VAL;
			continue;
		}
		priority_p_set_assoc_usage(assoc);
		_set_children_usage_efctv(assoc->usage->children_list);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}


/* Distribute the tickets to child nodes recursively.
 *
 * NOTE: acct_mgr_association_lock must be locked before this is called.
 */
static int _distribute_tickets(List children_list, uint32_t tickets)
{
	ListIterator itr;
	slurmdb_association_rec_t *assoc;
	double sfsum = 0, fs;

	if (!children_list || !list_count(children_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		if (assoc->usage->active_seqno
		    != assoc_mgr_root_assoc->usage->active_seqno)
			continue;
		if (fuzzy_equal(assoc->usage->usage_efctv, NO_VAL))
			priority_p_set_assoc_usage(assoc);
		fs = priority_p_calc_fs_factor(assoc->usage->usage_efctv,
					       assoc->usage->shares_norm);
		sfsum += assoc->usage->shares_norm * fs;
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		if (assoc->usage->active_seqno
		    != assoc_mgr_root_assoc->usage->active_seqno)
			continue;
		fs = priority_p_calc_fs_factor(assoc->usage->usage_efctv,
					       assoc->usage->shares_norm);
		assoc->usage->tickets = tickets * assoc->usage->shares_norm
			* fs / sfsum;
		if (priority_debug) {
			if (assoc->user)
				info("User %s in account %s gets %u tickets",
				     assoc->user, assoc->acct,
				     assoc->usage->tickets);
			else
				info("Account %s gets %u tickets",
				     assoc->acct, assoc->usage->tickets);
		}
		if (assoc->user && assoc->usage->tickets > max_tickets)
			max_tickets = assoc->usage->tickets;
		_distribute_tickets(assoc->usage->children_list,
				    assoc->usage->tickets);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}


/* job_ptr should already have the partition priority and such added here
 * before had we will be adding to it
 */
static double _get_fairshare_priority(struct job_record *job_ptr)
{
	slurmdb_association_rec_t *job_assoc =
		(slurmdb_association_rec_t *)job_ptr->assoc_ptr;
	slurmdb_association_rec_t *fs_assoc = NULL;
	double priority_fs = 0.0;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	if (!calc_fairshare)
		return 0;

	if (!job_assoc) {
		error("Job %u has no association.  Unable to "
		      "compute fairshare.", job_ptr->job_id);
		return 0;
	}

	fs_assoc = job_assoc;

	assoc_mgr_lock(&locks);

	/* Use values from parent when FairShare=SLURMDB_FS_USE_PARENT */
	while ((fs_assoc->shares_raw == SLURMDB_FS_USE_PARENT)
	       && fs_assoc->usage->parent_assoc_ptr
	       && (fs_assoc != assoc_mgr_root_assoc)) {
		fs_assoc = fs_assoc->usage->parent_assoc_ptr;
	}

	if (fuzzy_equal(fs_assoc->usage->usage_efctv, NO_VAL))
		priority_p_set_assoc_usage(fs_assoc);

	/* Priority is 0 -> 1 */
	priority_fs = 0;
	if (flags & PRIORITY_FLAGS_TICKET_BASED) {
		if (fs_assoc->usage->active_seqno ==
		    assoc_mgr_root_assoc->usage->active_seqno && max_tickets) {
			priority_fs = (double) fs_assoc->usage->tickets /
				      max_tickets;
		}
		if (priority_debug) {
			info("Fairshare priority of job %u for user %s in acct"
			     " %s is %f",
			     job_ptr->job_id, job_assoc->user, job_assoc->acct,
			     priority_fs);
		}
	} else if (flags & PRIORITY_FLAGS_LEVEL_BASED) {
		priority_fs = NORMALIZE_VALUE(
			job_assoc->usage->priority_fs_ranked,
			0, UINT64_MAX,
			0.0L, 1.0L
		);
		if (priority_debug) {
			info("Fairhare priority of job %u for user %s in acct"
				" %s is %f (0x%016"PRIX64")",
			     job_ptr->job_id, job_assoc->user, job_assoc->acct,
			     priority_fs, job_assoc->usage->priority_fs_ranked);
		}
	} else {
		priority_fs = priority_p_calc_fs_factor(
				fs_assoc->usage->usage_efctv,
				(long double)fs_assoc->usage->shares_norm);
		if (priority_debug) {
			info("Fairshare priority of job %u for user %s in acct"
			     " %s is 2**(-%Lf/%f) = %f",
			     job_ptr->job_id, job_assoc->user, job_assoc->acct,
			     fs_assoc->usage->usage_efctv,
			     fs_assoc->usage->shares_norm, priority_fs);
		}
	}
	assoc_mgr_unlock(&locks);

	return priority_fs;
}

static void _set_priority_factors(
		time_t start_time,
		struct job_record *job_ptr)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;

	xassert(job_ptr);

	if (!job_ptr->prio_factors)
		job_ptr->prio_factors =
			xmalloc(sizeof(priority_factors_object_t));
	else
		memset(job_ptr->prio_factors, 0,
		       sizeof(priority_factors_object_t));

	qos_ptr = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;

	if (weight_age) {
		uint32_t diff = 0;
		time_t use_time;

		if (flags & PRIORITY_FLAGS_ACCRUE_ALWAYS)
			use_time = job_ptr->details->submit_time;
		else
			use_time = job_ptr->details->begin_time;

		/* Only really add an age priority if the use_time is
		   past the start_time.
		*/
		if (start_time > use_time)
			diff = start_time - use_time;

		if (job_ptr->details->begin_time
		    || (flags & PRIORITY_FLAGS_ACCRUE_ALWAYS)) {
			if (diff < max_age) {
				job_ptr->prio_factors->priority_age =
					(double)diff / (double)max_age;
			} else
				job_ptr->prio_factors->priority_age = 1.0;
		}
	}

	if (job_ptr->assoc_ptr && weight_fs) {
		job_ptr->prio_factors->priority_fs =
			_get_fairshare_priority(job_ptr);
	}

	if (weight_js) {
		uint32_t cpu_cnt = 0, min_nodes = 1;
		/* On the initial run of this we don't have total_cpus
		   so go off the requesting.  After the first shot
		   total_cpus should be filled in.
		*/
		if (job_ptr->total_cpus)
			cpu_cnt = job_ptr->total_cpus;
		else if (job_ptr->details
			 && (job_ptr->details->max_cpus != NO_VAL))
			cpu_cnt = job_ptr->details->max_cpus;
		else if (job_ptr->details && job_ptr->details->min_cpus)
			cpu_cnt = job_ptr->details->min_cpus;
		if (job_ptr->details)
			min_nodes = job_ptr->details->min_nodes;

		if (flags & PRIORITY_FLAGS_SIZE_RELATIVE) {
			uint32_t time_limit = 1;
			/* Job size in CPUs (based upon average CPUs/Node */
			job_ptr->prio_factors->priority_js =
				(double)min_nodes *
				(double)cluster_cpus /
				(double)node_record_count;
			if (cpu_cnt > job_ptr->prio_factors->priority_js) {
				job_ptr->prio_factors->priority_js =
					(double)cpu_cnt;
			}
			/* Divide by job time limit */
			if (job_ptr->time_limit != NO_VAL)
				time_limit = job_ptr->time_limit;
			else if (job_ptr->part_ptr)
				time_limit = job_ptr->part_ptr->max_time;
			job_ptr->prio_factors->priority_js /= time_limit;
			/* Normalize to max value of 1.0 */
			job_ptr->prio_factors->priority_js /= cluster_cpus;
			if (favor_small) {
				job_ptr->prio_factors->priority_js =
					(double) 1.0 -
					job_ptr->prio_factors->priority_js;
			}
		} else if (favor_small) {
			job_ptr->prio_factors->priority_js =
				(double)(node_record_count - min_nodes)
				/ (double)node_record_count;
			if (cpu_cnt) {
				job_ptr->prio_factors->priority_js +=
					(double)(cluster_cpus - cpu_cnt)
					/ (double)cluster_cpus;
				job_ptr->prio_factors->priority_js /= 2;
			}
		} else {	/* favor large */
			job_ptr->prio_factors->priority_js =
				(double)min_nodes / (double)node_record_count;
			if (cpu_cnt) {
				job_ptr->prio_factors->priority_js +=
					(double)cpu_cnt / (double)cluster_cpus;
				job_ptr->prio_factors->priority_js /= 2;
			}
		}
		if (job_ptr->prio_factors->priority_js < .0)
			job_ptr->prio_factors->priority_js = 0.0;
		else if (job_ptr->prio_factors->priority_js > 1.0)
			job_ptr->prio_factors->priority_js = 1.0;
	}

	if (job_ptr->part_ptr && job_ptr->part_ptr->priority && weight_part) {
		job_ptr->prio_factors->priority_part =
			job_ptr->part_ptr->norm_priority;
	}

	if (qos_ptr && qos_ptr->priority && weight_qos) {
		job_ptr->prio_factors->priority_qos =
			qos_ptr->usage->norm_priority;
	}

	if (job_ptr->details)
		job_ptr->prio_factors->nice = job_ptr->details->nice;
	else
		job_ptr->prio_factors->nice = NICE_OFFSET;
}


/* Returns the priority after applying the weight factors */
static uint32_t _apply_priority_weights(time_t start_time,
				       struct job_record *job_ptr)
{
	double priority	= 0.0;
	priority_factors_object_t pre_factors;

	if (job_ptr->direct_set_prio && (job_ptr->priority > 0))
		return job_ptr->priority;

	if (!job_ptr->details) {
		error("_get_priority_internal: job %u does not have a "
		      "details symbol set, can't set priority",
		      job_ptr->job_id);
		return 0;
	}

	memcpy(&pre_factors, job_ptr->prio_factors,
	       sizeof(priority_factors_object_t));

	job_ptr->prio_factors->priority_age  *= (double)weight_age;
	job_ptr->prio_factors->priority_fs   *= (double)weight_fs;
	job_ptr->prio_factors->priority_js   *= (double)weight_js;
	job_ptr->prio_factors->priority_part *= (double)weight_part;
	job_ptr->prio_factors->priority_qos  *= (double)weight_qos;

	priority = job_ptr->prio_factors->priority_age
		+ job_ptr->prio_factors->priority_fs
		+ job_ptr->prio_factors->priority_js
		+ job_ptr->prio_factors->priority_part
		+ job_ptr->prio_factors->priority_qos
		- (double)(job_ptr->prio_factors->nice - NICE_OFFSET);

	/* Priority 0 is reserved for held jobs */
	if (priority < 1)
		priority = 1;

	if (job_ptr->part_ptr_list) {
		struct part_record *part_ptr;
		double priority_part;
		ListIterator part_iterator;
		int i = 0;

		if (!job_ptr->priority_array) {
			job_ptr->priority_array = xmalloc(sizeof(uint32_t) *
			                                  (list_count(job_ptr->part_ptr_list) + 1));
		}
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = (struct part_record *)
				   list_next(part_iterator))) {
			priority_part = part_ptr->priority /
					(double)part_max_priority *
					(double)weight_part;
			job_ptr->priority_array[i] = (uint32_t)
					(job_ptr->prio_factors->priority_age
					+ job_ptr->prio_factors->priority_fs
					+ job_ptr->prio_factors->priority_js
					+ priority_part
					+ job_ptr->prio_factors->priority_qos
					- (double)(job_ptr->prio_factors->nice
					- NICE_OFFSET));
			debug("Job %u has more than one partition (%s)(%u)",
			      job_ptr->job_id, part_ptr->name,
			      job_ptr->priority_array[i]);
			i++;
		}
	}

	if (priority_debug) {
		info("Weighted Age priority is %f * %u = %.2f",
		     pre_factors.priority_age, weight_age,
		     job_ptr->prio_factors->priority_age);
		info("Weighted Fairshare priority is %f * %u = %.2f",
		     pre_factors.priority_fs, weight_fs,
		     job_ptr->prio_factors->priority_fs);
		info("Weighted JobSize priority is %f * %u = %.2f",
		     pre_factors.priority_js, weight_js,
		     job_ptr->prio_factors->priority_js);
		info("Weighted Partition priority is %f * %u = %.2f",
		     pre_factors.priority_part, weight_part,
		     job_ptr->prio_factors->priority_part);
		info("Weighted QOS priority is %f * %u = %.2f",
		     pre_factors.priority_qos, weight_qos,
		     job_ptr->prio_factors->priority_qos);
		info("Job %u priority: %.2f + %.2f + %.2f + %.2f + %.2f - %d "
		     "= %.2f",
		     job_ptr->job_id, job_ptr->prio_factors->priority_age,
		     job_ptr->prio_factors->priority_fs,
		     job_ptr->prio_factors->priority_js,
		     job_ptr->prio_factors->priority_part,
		     job_ptr->prio_factors->priority_qos,
		     (job_ptr->prio_factors->nice - NICE_OFFSET),
		     priority);
	}
	return (uint32_t)priority;
}


/* Mark an association and its parents as active (i.e. it may be given
 * tickets) during the current scheduling cycle.  The association
 * manager lock should be held on entry.  */
static bool _mark_assoc_active(struct job_record *job_ptr)
{
	slurmdb_association_rec_t *job_assoc =
		(slurmdb_association_rec_t *)job_ptr->assoc_ptr,
		*assoc;

	if (!job_assoc) {
		error("Job %u has no association.  Unable to "
		      "mark assiciation as active.", job_ptr->job_id);
		return false;
	}

	for (assoc = job_assoc; assoc != assoc_mgr_root_assoc;
	     assoc = assoc->usage->parent_assoc_ptr) {
		if (assoc->usage->active_seqno
		    == assoc_mgr_root_assoc->usage->active_seqno)
			break;
		assoc->usage->active_seqno
			= assoc_mgr_root_assoc->usage->active_seqno;
	}
	return true;
}


/* based upon the last reset time, compute when the next reset should be */
static time_t _next_reset(uint16_t reset_period, time_t last_reset)
{
	struct tm last_tm;
	time_t tmp_time, now = time(NULL);

	if (localtime_r(&last_reset, &last_tm) == NULL)
		return (time_t) 0;

	last_tm.tm_sec   = 0;
	last_tm.tm_min   = 0;
	last_tm.tm_hour  = 0;
/*	last_tm.tm_wday = 0	ignored */
/*	last_tm.tm_yday = 0;	ignored */
	last_tm.tm_isdst = -1;
	switch (reset_period) {
	case PRIORITY_RESET_DAILY:
		tmp_time = mktime(&last_tm);
		tmp_time += SECS_PER_DAY;
		while ((tmp_time + SECS_PER_DAY) < now)
			tmp_time += SECS_PER_DAY;
		return tmp_time;
	case PRIORITY_RESET_WEEKLY:
		tmp_time = mktime(&last_tm);
		tmp_time += (SECS_PER_DAY * (7 - last_tm.tm_wday));
		while ((tmp_time + SECS_PER_WEEK) < now)
			tmp_time += SECS_PER_WEEK;
		return tmp_time;
	case PRIORITY_RESET_MONTHLY:
		last_tm.tm_mday = 1;
		if (last_tm.tm_mon < 11)
			last_tm.tm_mon++;
		else {
			last_tm.tm_mon  = 0;
			last_tm.tm_year++;
		}
		break;
	case PRIORITY_RESET_QUARTERLY:
		last_tm.tm_mday = 1;
		if (last_tm.tm_mon < 3)
			last_tm.tm_mon = 3;
		else if (last_tm.tm_mon < 6)
			last_tm.tm_mon = 6;
		else if (last_tm.tm_mon < 9)
			last_tm.tm_mon = 9;
		else {
			last_tm.tm_mon  = 0;
			last_tm.tm_year++;
		}
		break;
	case PRIORITY_RESET_YEARLY:
		last_tm.tm_mday = 1;
		last_tm.tm_mon  = 0;
		last_tm.tm_year++;
		break;
	default:
		return (time_t) 0;
	}
	return mktime(&last_tm);
}

/*
 * Remove previously used time from qos and assocs grp_used_cpu_run_secs.
 *
 * When restarting slurmctld acct_policy_job_begin() is called for all
 * running jobs. There every jobs total requested cputime (total_cpus *
 * time_limit) is added to grp_used_cpu_run_secs of assocs and qos.
 *
 * This function will subtract all cputime that was used until the
 * decay thread last ran. This kludge is necessary as the decay thread
 * last_ran variable can't be accessed from acct_policy_job_begin().
 */
static void _init_grp_used_cpu_run_secs(time_t last_ran)
{
	struct job_record *job_ptr = NULL;
	ListIterator itr;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	uint64_t delta;
	slurmdb_qos_rec_t *qos;
	slurmdb_association_rec_t *assoc;

	if (priority_debug)
		info("Initializing grp_used_cpu_run_secs");

	if (!(enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;
	if (!(job_list && list_count(job_list)))
		return;

	lock_slurmctld(job_read_lock);
	itr = list_iterator_create(job_list);

	assoc_mgr_lock(&locks);
	while ((job_ptr = list_next(itr))) {
		if (priority_debug)
			debug2("job: %u", job_ptr->job_id);
		qos = NULL;
		assoc = NULL;
		delta = 0;

		if (!IS_JOB_RUNNING(job_ptr))
			continue;

		if (job_ptr->start_time > last_ran)
			continue;

		delta = job_ptr->total_cpus * (last_ran - job_ptr->start_time);

		qos = (slurmdb_qos_rec_t *) job_ptr->qos_ptr;
		assoc = (slurmdb_association_rec_t *) job_ptr->assoc_ptr;

		if (qos) {
			if (priority_debug) {
				info("Subtracting %"PRIu64" from qos "
				     "%s grp_used_cpu_run_secs "
				     "%"PRIu64" = %"PRIu64"",
				     delta,
				     qos->name,
				     qos->usage->grp_used_cpu_run_secs,
				     qos->usage->grp_used_cpu_run_secs -
				     delta);
			}
			if (qos->usage->grp_used_cpu_run_secs >= delta) {
				qos->usage->grp_used_cpu_run_secs -= delta;
			} else {
				error("qos %s grp_used_cpu_run_secs underflow",
				      qos->name);
				qos->usage->grp_used_cpu_run_secs = 0;
			}
		}

		while (assoc) {
			if (priority_debug) {
				info("Subtracting %"PRIu64" from assoc %u "
				     "grp_used_cpu_run_secs "
				     "%"PRIu64" = %"PRIu64"",
				     delta,
				     assoc->id,
				     assoc->usage->grp_used_cpu_run_secs,
				     assoc->usage->grp_used_cpu_run_secs -
				     delta);
			}
			if (assoc->usage->grp_used_cpu_run_secs >= delta) {
				assoc->usage->grp_used_cpu_run_secs -= delta;
			} else {
				error("assoc %u grp_used_cpu_run_secs "
				      "underflow", assoc->id);
				assoc->usage->grp_used_cpu_run_secs = 0;
			}
			assoc = assoc->usage->parent_assoc_ptr;
		}
	}
	assoc_mgr_unlock(&locks);
	list_iterator_destroy(itr);
	unlock_slurmctld(job_read_lock);
}

/* If the job is running then apply decay to the job.
 *
 * Return 0 if we don't need to process the job any further, 1 if
 * futher processing is needed.
 */
static int _apply_new_usage(struct job_record *job_ptr,
			    time_t start_period, time_t end_period)
{
	slurmdb_qos_rec_t *qos;
	slurmdb_association_rec_t *assoc;
	double run_delta = 0.0, run_decay = 0.0, real_decay = 0.0;
	uint64_t cpu_run_delta = 0;
	uint64_t job_time_limit_ends = 0;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	/* Even if job_ptr->qos_ptr->usage_factor is 0 we need to
	 * handle other non-usage variables here
	 * (grp_used_cpu_run_secs), so don't return.
	 */

	if (job_ptr->start_time > start_period)
		start_period = job_ptr->start_time;

	if (job_ptr->end_time
	    && (end_period > job_ptr->end_time))
		end_period = job_ptr->end_time;

	run_delta = difftime(end_period, start_period);

	/* Even if run_delta is 0 we need to
	 * handle other non-usage variables here
	 * (grp_used_cpu_run_secs), so don't return.
	 */
	if (run_delta < 0)
		run_delta = 0;

	/* cpu_run_delta will is used to
	 * decrease qos and assocs
	 * grp_used_cpu_run_secs values. When
	 * a job is started only seconds until
	 * start_time+time_limit is added, so
	 * for jobs running over their
	 * timelimit we should only subtract
	 * the used time until the time limit. */
	job_time_limit_ends =
		(uint64_t)job_ptr->start_time +
		(uint64_t)job_ptr->time_limit * 60;

	if ((uint64_t)start_period >= job_time_limit_ends)
		cpu_run_delta = 0;
	else if (IS_JOB_FINISHED(job_ptr) || IS_JOB_COMPLETING(job_ptr)) {
		/* If a job is being requeued sometimes the state will
		   be pending + completing so handle that the same as
		   finished so we don't leave time in the mix.
		*/
		cpu_run_delta = job_ptr->total_cpus *
			(job_time_limit_ends - (uint64_t)start_period);
	} else
		cpu_run_delta = job_ptr->total_cpus * run_delta;

	if (priority_debug)
		info("job %u ran for %g seconds on %u cpus",
		     job_ptr->job_id, run_delta, job_ptr->total_cpus);

	/* get the time in decayed fashion */
	run_decay = run_delta * pow(decay_factor, run_delta);

	real_decay = run_decay * (double)job_ptr->total_cpus;

	assoc_mgr_lock(&locks);
	/* Just to make sure we don't make a
	   window where the qos_ptr could of
	   changed make sure we get it again
	   here.
	*/
	qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
	assoc = (slurmdb_association_rec_t *)job_ptr->assoc_ptr;

	/* now apply the usage factor for this qos */
	if (qos) {
		if (qos->usage_factor >= 0) {
			real_decay *= qos->usage_factor;
			run_decay *= qos->usage_factor;
		}
		qos->usage->grp_used_wall += run_decay;
		qos->usage->usage_raw += (long double)real_decay;
		if (qos->usage->grp_used_cpu_run_secs >= cpu_run_delta) {
			if (priority_debug)
				info("QOS %s has grp_used_cpu_run_secs "
				     "of %"PRIu64", will subtract %"PRIu64"",
				     qos->name,
				     qos->usage->grp_used_cpu_run_secs,
				     cpu_run_delta);
			qos->usage->grp_used_cpu_run_secs -= cpu_run_delta;
		} else {
			if (priority_debug)
				info("jobid %u, qos %s: setting "
				     "grp_used_cpu_run_secs "
				     "to 0 because %"PRIu64" < %"PRIu64"",
				     job_ptr->job_id, qos->name,
				     qos->usage->grp_used_cpu_run_secs,
				     cpu_run_delta);
			qos->usage->grp_used_cpu_run_secs = 0;
		}
	}

	/* We want to do this all the way up
	 * to and including root.  This way we
	 * can keep track of how much usage
	 * has occured on the entire system
	 * and use that to normalize against. */
	while (assoc) {
		if (assoc->usage->grp_used_cpu_run_secs >= cpu_run_delta) {
			if (priority_debug)
				info("assoc %u (user='%s' "
				     "acct='%s') has grp_used_cpu_run_secs "
				     "of %"PRIu64", will subtract %"PRIu64"",
				     assoc->id, assoc->user, assoc->acct,
				     assoc->usage->grp_used_cpu_run_secs,
				     cpu_run_delta);
			assoc->usage->grp_used_cpu_run_secs -= cpu_run_delta;
		} else {
			if (priority_debug)
				info("jobid %u, assoc %u: setting "
				     "grp_used_cpu_run_secs "
				     "to 0 because %"PRIu64" < %"PRIu64"",
				     job_ptr->job_id, assoc->id,
				     assoc->usage->grp_used_cpu_run_secs,
				     cpu_run_delta);
			assoc->usage->grp_used_cpu_run_secs = 0;
		}

		assoc->usage->grp_used_wall += run_decay;
		assoc->usage->usage_raw += (long double)real_decay;
		if (priority_debug)
			info("adding %f new usage to assoc %u (user='%s' "
			     "acct='%s') raw usage is now %Lf.  Group wall "
			     "added %f making it %f. GrpCPURunMins is "
			     "%"PRIu64"",
			     real_decay, assoc->id,
			     assoc->user, assoc->acct,
			     assoc->usage->usage_raw,
			     run_decay,
			     assoc->usage->grp_used_wall,
			     assoc->usage->grp_used_cpu_run_secs/60);
		assoc = assoc->usage->parent_assoc_ptr;
	}
	assoc_mgr_unlock(&locks);
	return 1;
}


static void _ticket_based_decay(List job_list, time_t start_time)
{
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	ListIterator itr = NULL;
	struct job_record *job_ptr = NULL;

	/* Read lock on jobs, nodes, and partitions */
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };

	/* Multifactor Ticket Based core algo 1/3. Iterate through all jobs,
	 * mark parent associations with the current sequence id, so that we
	 * know which associations/users are active. At the same time as we're
	 * looping through all the jobs anyway, apply the new usage of running
	 * jobs too.
	 */

	lock_slurmctld(job_read_lock);
	assoc_mgr_lock(&locks);
	/* seqno 0 is a special invalid value. */
	assoc_mgr_root_assoc->usage->active_seqno++;
	if (!assoc_mgr_root_assoc->usage->active_seqno)
		assoc_mgr_root_assoc->usage->active_seqno++;
	assoc_mgr_unlock(&locks);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		/* Don't need to handle finished jobs. */
		if (IS_JOB_FINISHED(job_ptr) || IS_JOB_COMPLETING(job_ptr))
			continue;
		/* apply new usage */
		if (!IS_JOB_PENDING(job_ptr) &&
		    job_ptr->start_time && job_ptr->assoc_ptr
		    && g_last_ran)
			_apply_new_usage(job_ptr,
					 g_last_ran,
					 start_time);

		if (IS_JOB_PENDING(job_ptr)
		    && job_ptr->assoc_ptr) {
			assoc_mgr_lock(&locks);
			_mark_assoc_active(job_ptr);
			assoc_mgr_unlock(&locks);
		}
	}
	list_iterator_destroy(itr);
	unlock_slurmctld(job_read_lock);

	/* Multifactor Ticket Based core algo 2/3. Start from the root,
	 * distribute tickets to active child associations proportional to the
	 * fair share (s*F). We start with UINT32_MAX tickets at the root.
	 */
	assoc_mgr_lock(&locks);
	max_tickets = 0;
	assoc_mgr_root_assoc->usage->tickets = (uint32_t) -1;
	_distribute_tickets(
		assoc_mgr_root_assoc->usage->children_list,
		(uint32_t) -1);
	assoc_mgr_unlock(&locks);

	/* Multifactor Ticket Based core algo 3/3. Iterate through the job
	 * list again, give priorities proportional to the  maximum number of
	 * tickets given to any user.
	 */
	lock_slurmctld(job_write_lock);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		/*
		 * Priority 0 is reserved for held
		 * jobs. Also skip priority
		 * calculation for non-pending jobs.
		 */
		if ((job_ptr->priority == 0) || !IS_JOB_PENDING(job_ptr))
			continue;

		_set_priority_factors(start_time, job_ptr);
		job_ptr->priority =
			_apply_priority_weights(start_time, job_ptr);
		last_job_update = time(NULL);
		debug2("priority for job %u is now %u",
		       job_ptr->job_id, job_ptr->priority);
	}
	list_iterator_destroy(itr);
	unlock_slurmctld(job_write_lock);
}


static bool _decay_apply_new_usage(struct job_record *job_ptr,
					  time_t *start_time_ptr)
{

	/* Don't need to handle finished jobs. */
	if (IS_JOB_FINISHED(job_ptr) || IS_JOB_COMPLETING(job_ptr))
		return false;

	/* apply new usage */
	if (!IS_JOB_PENDING(job_ptr) &&
	    job_ptr->start_time && job_ptr->assoc_ptr) {
		if (!_apply_new_usage(job_ptr, g_last_ran, *start_time_ptr))
			return false;
	}
	return true;
}



static void _decay_apply_weighted_factors(struct job_record *job_ptr,
						 time_t *start_time_ptr)
{
	time_t start_time = *start_time_ptr;
	/*
	 * Priority 0 is reserved for held
	 * jobs. Also skip priority
	 * calculation for non-pending jobs.
	 */
	if ((job_ptr->priority == 0) || !IS_JOB_PENDING(job_ptr))
		return;

	_set_priority_factors(start_time, job_ptr);

	job_ptr->priority = _apply_priority_weights(start_time, job_ptr);
	last_job_update = time(NULL);
	debug2("priority for job %u is now %u",
	       job_ptr->job_id, job_ptr->priority);

}


static void _decay_apply_new_usage_and_weighted_factors(
					struct job_record *job_ptr,
					time_t *start_time_ptr)
{
	if (!_decay_apply_new_usage(job_ptr, start_time_ptr))
		return;

	_decay_apply_weighted_factors(job_ptr, start_time_ptr);
}


/* Apply usage with decay factor. Call standard functions */
static void _level_based_decay_apply_new_usage(
					struct job_record *job_ptr,
					time_t *start_time_ptr)
{
	if (!_decay_apply_new_usage(job_ptr, start_time_ptr))
		return;
	/*
	 * Priority 0 is reserved for held jobs. Also skip priority
	 * calculation for non-pending jobs.
	 */
	if ((job_ptr->priority == 0) || !IS_JOB_PENDING(job_ptr))
		return;

	_set_priority_factors(*start_time_ptr, job_ptr);
	last_job_update = time(NULL);
}


static void _level_based_calc_children_fs_priority_debug(
		uint64_t priority_fs_raw,
		uint64_t level_fs_raw,
		slurmdb_association_rec_t *assoc,
		uint16_t assoc_level)
{
	int spaces = (assoc_level + 1) * 4;
	char *name = assoc->user
		? assoc->user
		: assoc->acct;

	if (!priority_debug)
		return;

	debug2(
		"%*s0x%016"PRIX64" | 0x%016"PRIX64" (%s)",
		spaces,
		"",
		priority_fs_raw,
		level_fs_raw,
		name
	);
	if (assoc->user)
		debug2(
			"%*s%18s = 0x%016"PRIX64" (%s)",
			spaces,
			"",
			"",
			priority_fs_raw | level_fs_raw,
			assoc->user
		);

}


/* Calculate F=2**(-Ueff/S) at the current level. Shift the result based on
 * depth in the association tree and the bucket size.
 */
static uint64_t _level_based_calc_level_fs(slurmdb_association_rec_t *assoc,
					  uint16_t assoc_level)
{
	uint64_t level_fs = 0;
	long double level_ratio = 0.0L;
	long double shares_adj = 0.0L;

	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		if(assoc->user)
			level_fs = 1.0L;
		else
			return 0;
	} else if (assoc->usage->shares_norm) {

		/* This function normalizes shares to be between 0.2 and 1.0;
		 * this range fares much better than 0.0 to 1.0 when used in
		 * the denominator of the fairshare calculation:
		 *   2**(-UsageEffective / Shares)
		 *
		 * Compare these two:
		 * http://www.wolframalpha.com/input/?i=2%5E-%28u%2Fs%29%2C+u+from+0+to+1%2C+s+from+.2+to+1
		 * http://www.wolframalpha.com/input/?i=2%5E-%28u%2Fs%29%2C+u+from+0+to+1%2C+s+from+0+to+1
		 */
		shares_adj = NORMALIZE_VALUE(
			assoc->usage->shares_norm,
			0.0l, 1.0l,
			0.1L, 1.0L
		);
		level_ratio = assoc->usage->usage_efctv / shares_adj;
	}

	/* reserve 0 for special casing */
	level_fs = NORMALIZE_VALUE(
		powl(2L, -level_ratio),
		0.0L, 1.0L,
		1, bucket_max
	);


	level_fs <<= (
		(priority_levels - assoc_level - 1)
		* bucket_width_in_bits
		+ unused_bucket_bits
	);
	return level_fs;
}


/* Calculate and set priority_fs_raw at each level then recurse to children.
 * Also, append users to user list while we are traversing.
 * This function calls and is called by _level_based_calc_children_fs().
 */
static void _level_based_calc_assoc_fs(
		List users,
		slurmdb_association_rec_t *assoc,
		uint16_t assoc_level)
{
	const uint64_t priority_fs_raw =
		assoc->usage->parent_assoc_ptr->usage->priority_fs_raw;
	uint64_t level_fs = 0;

	/* Calculate the fairshare factor at this level, properly shifted
	 *
	 * If assoc_level >= priority_levels, the tree is deeper than
	 * priority_levels; you are done with priority calculations but still
	 * need to set the values on each child.
	 */
	if (assoc_level < priority_levels)
		level_fs = _level_based_calc_level_fs(assoc, assoc_level);

	/* Bitwise OR the level fairshare factor with the parent's. For a
	 * user, this is the final fairshare factor that is used in sorting
	 * and ranking.
	 */
	assoc->usage->priority_fs_raw = priority_fs_raw | level_fs;

	/* Found a user, add to users list */
	if (assoc->user)
		list_append(users, assoc);

	_level_based_calc_children_fs_priority_debug(
		priority_fs_raw, level_fs, assoc, assoc_level);

	/* If USE_PARENT, set priority_fs_raw equal to parent then work on
	 * children */
	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		_level_based_calc_children_fs(
			assoc->usage->children_list, users, assoc_level);
	else if (!assoc->user)
		/* If this is an account, descend to child accounts */
			_level_based_calc_children_fs(
				assoc->usage->children_list,
				users,
				assoc_level + 1
			);
}


/* Call _level_based_calc_assoc_fs() on each child, if any. This function will
 * be called again by _level_based_calc_assoc_fs() for child accounts (not
 * users), thus making it recursive.
 */
static void _level_based_calc_children_fs(List children_list,
					     List users,
					     uint16_t assoc_level)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;

	if (!children_list || !list_count(children_list))
		return;

	itr = list_iterator_create(children_list);
	while (assoc = list_next(itr)) {
		_level_based_calc_assoc_fs(
			users,
			assoc,
			assoc_level
		);
	}

	list_iterator_destroy(itr);
}


/* Sort so that higher priority_fs_raw values are first in the list */
int _level_based_sort_priority_fs(slurmdb_association_rec_t **x,
				 slurmdb_association_rec_t **y)
{
	uint64_t a = (*x)->usage->priority_fs_raw;
	uint64_t b = (*y)->usage->priority_fs_raw;
	if (a < b)
		return 1;
	else if (b < a)
		return -1;
	else
		return 0;
}


/* Iterate through sorted list of users. Apply priorities based on their rank,
 * allowing for duplicate rankings if priority_fs_raw is equal for users
 * (i vs rank).
 */
void _level_based_apply_rank(List users)
{
	ListIterator itr = list_iterator_create(users);
	slurmdb_association_rec_t *assoc;
	int count = list_count(users);
	int i = count - 1;
	int rank = count - 1;
	/* priority_fs_raw can't be equal to 0 due to normalization in
	 * _level_based_calc_level_fs */
	uint64_t prev_priority_fs_raw = 0;

	while ((assoc = list_next(itr))) {
		xassert(assoc->usage->priority_fs_raw != 0);

		/* If same as prev, rank stays the same. This allows for
		 * rankings like 7,6,5,5,5,2,1,0 */
		if(prev_priority_fs_raw != assoc->usage->priority_fs_raw)
			rank = i;
		assoc->usage->priority_fs_ranked = NORMALIZE_VALUE(
			rank,
			0.0, (long double) count,
			0, UINT64_MAX
		);
		if (priority_debug)
			info(
				"Fairshare for user %s in acct %s: ranked "
				"%d/%d (0x%016"PRIX64")",
				assoc->user, assoc->acct, rank, count,
				assoc->usage->priority_fs_ranked
			);
		i--;
		prev_priority_fs_raw = assoc->usage->priority_fs_raw;
	}

	list_iterator_destroy(itr);
}


/* Calculate fairshare for associations, sort users by priority_fs_raw, then
 * use the rank in the sorted list as a user's fs factor
 *
 * Call assoc_mgr_lock before this */
static void _level_based_apply_priority_fs(void)
{
	List users = list_create(NULL);

	if (priority_debug) {
		debug2("LEVEL_BASED Fairshare, starting at root:");
		debug2("%s | %s", "parent_fs", "current_fs");
	}
	assoc_mgr_root_assoc->usage->priority_fs_raw = 0;
	assoc_mgr_root_assoc->usage->priority_fs_ranked = 0;

	/* set priority_fs_raw on each assoc and add users to List users */
	_level_based_calc_children_fs(
		assoc_mgr_root_assoc->usage->children_list,
		users,
		0
	);

	/* sort users by priority_fs_raw */
	list_sort(users, (ListCmpF) _level_based_sort_priority_fs);

	/* set user ranking based on their position in the sorted list */
	_level_based_apply_rank(users);

	list_destroy(users);
}


/* LEVEL_BASED code called from the decay thread loop */
static void _level_based_decay(List job_list, time_t start_time)
{
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	/* apply decayed usage */
	lock_slurmctld(job_write_lock);
	list_for_each(
		job_list,
		(ListForF) _level_based_decay_apply_new_usage,
		&start_time
	);
	unlock_slurmctld(job_write_lock);

	/* calculate priority for associations */
	assoc_mgr_lock(&locks);
	_level_based_apply_priority_fs();
	assoc_mgr_unlock(&locks);

	/* assign job priorities */
	lock_slurmctld(job_write_lock);
	list_for_each(
		job_list,
		(ListForF) _decay_apply_weighted_factors,
		&start_time
	);
	unlock_slurmctld(job_write_lock);
}


static void *_decay_thread(void *no_data)
{
	struct job_record *job_ptr = NULL;
	ListIterator itr;
	time_t start_time = time(NULL);
	time_t last_reset = 0, next_reset = 0;
	uint32_t calc_period = slurm_get_priority_calc_period();
	double decay_hl = (double)slurm_get_priority_decay_hl();
	uint16_t reset_period = slurm_get_priority_reset_period();

	time_t now;
	double run_delta = 0.0, real_decay = 0.0;
	uint32_t excluded_flags = 0;
	double elapsed;

	/* Write lock on jobs, read lock on nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	/*
	 * DECAY_FACTOR DESCRIPTION:
	 *
	 * The decay thread applies an exponential decay over the past
	 * consumptions using a rolling approach.
	 * Every calc period p in seconds, the already computed usage is
	 * computed again applying the decay factor of that slice :
	 * decay_factor_slice.
	 *
	 * To ease the computation, the notion of decay_factor
	 * is introduced and corresponds to the decay factor
	 * required for a slice of 1 second. Thus, for any given
	 * slice ot time of n seconds, decay_factor_slice will be
	 * defined as : df_slice = pow(df,n)
	 *
	 * For a slice corresponding to the defined half life 'decay_hl' and
	 * a usage x, we will therefore have :
	 *    >>  x * pow(decay_factor,decay_hl) = 1/2 x  <<
	 *
	 * This expression helps to define the value of decay_factor that
	 * is necessary to apply the previously described logic.
	 *
	 * The expression is equivalent to :
	 *    >> decay_hl * ln(decay_factor) = ln(1/2)
	 *    >> ln(decay_factor) = ln(1/2) / decay_hl
	 *    >> decay_factor = e( ln(1/2) / decay_hl )
	 *
	 * Applying THe power series e(x) = sum(x^n/n!) for n from 0 to infinity
	 *    >> decay_factor = 1 + ln(1/2)/decay_hl
	 *    >> decay_factor = 1 - ( 0.693 / decay_hl)
	 *
	 * This explain the following declaration.
	 */
	if (decay_hl > 0)
		decay_factor = 1 - (0.693 / decay_hl);

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	_read_last_decay_ran(&g_last_ran, &last_reset);
	if (last_reset == 0)
		last_reset = start_time;

	_init_grp_used_cpu_run_secs(g_last_ran);

	while (1) {
		now = start_time;
		run_delta = 0.0;
		real_decay = 0.0;

		slurm_mutex_lock(&decay_lock);
		running_decay = 1;

		/* If reconfig is called handle all that happens
		   outside of the loop here */
		if (reconfig) {
			/* if decay_hl is 0 or less that means no
			   decay is to be had.  This also means we
			   flush the used time at a certain time
			   set by PriorityUsageResetPeriod in the slurm.conf
			*/
			calc_period = slurm_get_priority_calc_period();
			reset_period = slurm_get_priority_reset_period();
			next_reset = 0;
			decay_hl = (double)slurm_get_priority_decay_hl();
			if (decay_hl > 0)
				decay_factor = 1 - (0.693 / decay_hl);
			else
				decay_factor = 1;

			reconfig = 0;
		}

		/* this needs to be done right away so as to
		 * incorporate it into the decay loop.
		 */
		switch(reset_period) {
		case PRIORITY_RESET_NONE:
			break;
		case PRIORITY_RESET_NOW:	/* do once */
			_reset_usage();
			reset_period = PRIORITY_RESET_NONE;
			last_reset = now;
			break;
		case PRIORITY_RESET_DAILY:
		case PRIORITY_RESET_WEEKLY:
		case PRIORITY_RESET_MONTHLY:
		case PRIORITY_RESET_QUARTERLY:
		case PRIORITY_RESET_YEARLY:
			if (next_reset == 0) {
				next_reset = _next_reset(reset_period,
							 last_reset);
			}
			if (now >= next_reset) {
				_reset_usage();
				last_reset = next_reset;
				next_reset = _next_reset(reset_period,
							 last_reset);
			}
		}

		/* now calculate all the normalized usage here */
		assoc_mgr_lock(&locks);
		_set_children_usage_efctv(
			assoc_mgr_root_assoc->usage->children_list);
		assoc_mgr_unlock(&locks);

		if (!g_last_ran)
			goto get_usage;
		else
			run_delta = difftime(start_time, g_last_ran);

		if (run_delta <= 0)
			goto get_usage;
		real_decay = pow(decay_factor, (double)run_delta);
#ifdef DBL_MIN
		if (real_decay < DBL_MIN)
			real_decay = DBL_MIN;
#endif
		if (priority_debug)
			info("Decay factor over %g seconds goes "
			     "from %.15f -> %.15f",
			     run_delta, decay_factor, real_decay);

		/* first apply decay to used time */
		if (_apply_decay(real_decay) != SLURM_SUCCESS) {
			error("priority/multifactor: problem applying decay");
			running_decay = 0;
			slurm_mutex_unlock(&decay_lock);
			break;
		}

		excluded_flags =
			PRIORITY_FLAGS_TICKET_BASED
			| PRIORITY_FLAGS_LEVEL_BASED;

		if (!(flags & excluded_flags)) {
			lock_slurmctld(job_write_lock);
			list_for_each(
				job_list,
				(ListForF) _decay_apply_new_usage_and_weighted_factors,
				&start_time
			);
			unlock_slurmctld(job_write_lock);
		}

	get_usage:

		if (flags & PRIORITY_FLAGS_TICKET_BASED)
			_ticket_based_decay(job_list, start_time);
		else if (flags & PRIORITY_FLAGS_LEVEL_BASED)
			_level_based_decay(job_list, start_time);

		g_last_ran = start_time;

		_write_last_decay_ran(g_last_ran, last_reset);

		running_decay = 0;
		slurm_mutex_unlock(&decay_lock);

		/* Sleep until the next time. */
		now = time(NULL);
		elapsed = difftime(now, start_time);
		if (elapsed < calc_period) {
			sleep(calc_period - elapsed);
			start_time = time(NULL);
		} else
			start_time = now;
		/* repeat ;) */
	}
	return NULL;
}

/* Selects the specific jobs that the user wanted to see
 * Requests that include job id(s) and user id(s) must match both to be passed.
 * Returns 1 if job should be omitted */
static int _filter_job(struct job_record *job_ptr, List req_job_list,
		       List req_user_list)
{
	int filter = 0;
	ListIterator iterator;
	uint32_t *job_id;
	uint32_t *user_id;

	if (req_job_list) {
		filter = 1;
		iterator = list_iterator_create(req_job_list);
		while ((job_id = list_next(iterator))) {
			if (*job_id == job_ptr->job_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1) {
			return 1;
		}
	}

	if (req_user_list) {
		filter = 1;
		iterator = list_iterator_create(req_user_list);
		while ((user_id = list_next(iterator))) {
			if (*user_id == job_ptr->user_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 1;
	}

	return filter;
}

static void *_cleanup_thread(void *no_data)
{
	pthread_join(decay_handler_thread, NULL);
	return NULL;
}

static void _internal_setup(void)
{
	if (slurm_get_debug_flags() & DEBUG_FLAG_PRIO)
		priority_debug = 1;
	else
		priority_debug = 0;

	favor_small = slurm_get_priority_favor_small();
	damp_factor = (long double)slurm_get_fs_dampening_factor();
	enforce = slurm_get_accounting_storage_enforce();
	max_age = slurm_get_priority_max_age();
	weight_age = slurm_get_priority_weight_age();
	weight_fs = slurm_get_priority_weight_fairshare();
	weight_js = slurm_get_priority_weight_job_size();
	weight_part = slurm_get_priority_weight_partition();
	weight_qos = slurm_get_priority_weight_qos();
	flags = slurmctld_conf.priority_flags;

	if (flags & PRIORITY_FLAGS_LEVEL_BASED) {
		priority_levels = slurm_get_priority_levels();
		/* calculate how many bits per level. truncate if necessary */
		bucket_width_in_bits = 64 / priority_levels;
		unused_bucket_bits = 64 % priority_levels;
		bucket_max = UINT64_MAX >> (64 - bucket_width_in_bits);
	}

	if (priority_debug) {
		info("priority: Damp Factor is %u", damp_factor);
		info("priority: AccountingStorageEnforce is %u", enforce);
		info("priority: Max Age is %u", max_age);
		info("priority: Weight Age is %u", weight_age);
		info("priority: Weight Fairshare is %u", weight_fs);
		info("priority: Weight JobSize is %u", weight_js);
		info("priority: Weight Part is %u", weight_part);
		info("priority: Weight QOS is %u", weight_qos);
		info("priority: Flags is %u", flags);
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	pthread_attr_t thread_attr;
	char *temp = NULL;

	/* This means we aren't running from the controller so skip setup. */
	if (cluster_cpus == NO_VAL) {
		damp_factor = (long double)slurm_get_fs_dampening_factor();
		return SLURM_SUCCESS;
	}

	_internal_setup();

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_accounting_storage_type();
	if (strcasecmp(temp, "accounting_storage/slurmdbd")
	    && strcasecmp(temp, "accounting_storage/mysql")) {
		error("You are not running a supported "
		      "accounting_storage plugin\n(%s).\n"
		      "Fairshare can only be calculated with either "
		      "'accounting_storage/slurmdbd' "
		      "or 'accounting_storage/mysql' enabled.  "
		      "If you want multifactor priority without fairshare "
		      "ignore this message.",
		      temp);
		calc_fairshare = 0;
		weight_fs = 0;
	} else if (assoc_mgr_root_assoc) {
		if (!cluster_cpus)
			fatal("We need to have a cluster cpu count "
			      "before we can init the priority/multifactor "
			      "plugin");
		assoc_mgr_root_assoc->usage->usage_efctv = 1.0;
		slurm_attr_init(&thread_attr);
		if (pthread_create(&decay_handler_thread, &thread_attr,
				   _decay_thread, NULL))
			fatal("pthread_create error %m");

		/* This is here to join the decay thread so we don't core
		 * dump if in the sleep, since there is no other place to join
		 * we have to create another thread to do it. */
		slurm_attr_init(&thread_attr);
		if (pthread_create(&cleanup_handler_thread, &thread_attr,
				   _cleanup_thread, NULL))
			fatal("pthread_create error %m");

		slurm_attr_destroy(&thread_attr);
	} else {
		if (weight_fs) {
			fatal("It appears you don't have any association "
			      "data from your database.  "
			      "The priority/multifactor plugin requires "
			      "this information to run correctly.  Please "
			      "check your database connection and try again.");
		}
		calc_fairshare = 0;
	}

	xfree(temp);

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	/* Daemon termination handled here */
	if (running_decay)
		debug("Waiting for decay thread to finish.");

	slurm_mutex_lock(&decay_lock);

	/* cancel the decay thread and then join the cleanup thread */
	if (decay_handler_thread)
		pthread_cancel(decay_handler_thread);
	if (cleanup_handler_thread)
		pthread_join(cleanup_handler_thread, NULL);

	slurm_mutex_unlock(&decay_lock);

	return SLURM_SUCCESS;
}

extern uint32_t priority_p_set(uint32_t last_prio, struct job_record *job_ptr)
{
	uint32_t priority = 0;
	_set_priority_factors(time(NULL), job_ptr);
	priority = _apply_priority_weights(time(NULL), job_ptr);

	debug2("initial priority for job %u is %u", job_ptr->job_id, priority);

	return priority;
}


/* Reursively call assoc_mgr_normalize_assoc_shares from assoc_mgr.c on
 * children of an association
 */
static void _set_norm_shares(List children_list)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;

	if (!children_list || list_is_empty(children_list))
		return;

	itr = list_iterator_create(children_list);
	while (assoc = list_next(itr)) {
		assoc_mgr_normalize_assoc_shares(assoc);
		if (!assoc->user)
			_set_norm_shares(assoc->usage->children_list);
	}

	list_iterator_destroy(itr);
}


extern void priority_p_reconfig(bool assoc_clear)
{
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };


	reconfig = 1;
	prevflags = flags;
	_internal_setup();

	/* Since LEVEL_BASED uses a different shares calculation method, we
	 * must reassign shares at reconfigure if the algorithm was switched to
	 * or from LEVEL_BASED */
	if ((flags & PRIORITY_FLAGS_LEVEL_BASED) !=
	    (prevflags & PRIORITY_FLAGS_LEVEL_BASED)) {
		assoc_mgr_lock(&locks);
		_set_norm_shares(assoc_mgr_root_assoc->usage->children_list);
		assoc_mgr_unlock(&locks);
	}

	/* Since the used_cpu_run_secs has been reset by the reconfig,
	 * we need to remove the time that has past since the last
	 * poll.  We can't just do the correct calculation in the
	 * first place because it will mess up everything in the poll
	 * since it is based off the g_last_ran time.
	 */
	if (assoc_clear)
		_init_grp_used_cpu_run_secs(g_last_ran);
	debug2("%s reconfigured", plugin_name);

	return;
}


static void _depth_oblivious_set_usage_efctv(
			slurmdb_association_rec_t *assoc,
			char *child,
			char *child_str)
{
	long double ratio_p, ratio_l, k, f, ratio_s;
	slurmdb_association_rec_t *parent_assoc = NULL;
	ListIterator sib_itr = NULL;
	slurmdb_association_rec_t *sibling = NULL;

	/* We want priority_fs = pow(2.0, -R); where
	   R = ratio_p * ratio_l^k
	*/

	/* ratio_p is R for our parent */

	/* ratio_l is our usage ratio r divided by ratio_s,
	 * the usage ratio of our siblings (including
	 * ourselves). In the standard case where everything
	 * is consumed at the leaf accounts ratio_s=ratio_p
	 */

	/* k is a factor which tends towards 0 when ratio_p
	   diverges from 1 and ratio_l would bring back R
	   towards 1
	*/

	/* Effective usage is now computed to be R*shares_norm
	   so that the general formula of
	   priority_fs = pow(2.0, -(usage_efctv / shares_norm))
	   gives what we want: priority_fs = pow(2.0, -R);
	*/

	f = 5.0; /* FIXME: This could be a tunable parameter
		    (higher f means more impact when parent consumption
		    is inadequate) */
	parent_assoc =  assoc->usage->parent_assoc_ptr;

	if (assoc->usage->shares_norm &&
	    parent_assoc->usage->shares_norm &&
	    parent_assoc->usage->usage_efctv &&
	    assoc->usage->usage_norm) {
		ratio_p = (parent_assoc->usage->usage_efctv /
		      parent_assoc->usage->shares_norm);

		ratio_s = 0;
		sib_itr = list_iterator_create(
			parent_assoc->usage->children_list);
		while ((sibling = list_next(sib_itr))) {
			if(sibling->shares_raw != SLURMDB_FS_USE_PARENT)
				ratio_s += sibling->usage->usage_norm;
		}
		list_iterator_destroy(sib_itr);
		ratio_s /= parent_assoc->usage->shares_norm;

		ratio_l = (assoc->usage->usage_norm /
		      assoc->usage->shares_norm) / ratio_s;
#if defined(__FreeBSD__)
		if (!ratio_p || !ratio_l
		    || log(ratio_p) * log(ratio_l) >= 0) {
			k = 1;
		} else {
			k = 1 / (1 + pow(f * log(ratio_p), 2));
		}

		assoc->usage->usage_efctv =
			ratio_p * pow(ratio_l, k) *
			assoc->usage->shares_norm;
#else
		if (!ratio_p || !ratio_l
		    || logl(ratio_p) * logl(ratio_l) >= 0) {
			k = 1;
		} else {
			k = 1 / (1 + powl(f * logl(ratio_p), 2));
		}

		assoc->usage->usage_efctv =
			ratio_p * pow(ratio_l, k) *
			assoc->usage->shares_norm;
#endif

		if (priority_debug) {
			info("Effective usage for %s %s off %s "
			     "(%Lf * %Lf ^ %Lf) * %f  = %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     ratio_p, ratio_l, k,
			     assoc->usage->shares_norm,
			     assoc->usage->usage_efctv);
		}
	} else {
		assoc->usage->usage_efctv = assoc->usage->usage_norm;
		if (priority_debug) {
			info("Effective usage for %s %s off %s %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->usage_efctv);
		}
	}
}


static long double _set_usage_efctv(slurmdb_association_rec_t *assoc)
{
	/* Variable names taken from HTML documentation */
	long double UAchild = assoc->usage->usage_norm;
	long double UEparent =
		 find_real_parent(assoc)->usage->usage_efctv;
	uint32_t Schild = assoc->shares_raw;
	uint32_t Sall_siblings = assoc->usage->level_shares;

	assoc->usage->usage_efctv = UAchild +
		(UEparent - UAchild) * (Schild / (long double) Sall_siblings);
}


/* Normalize the assoc's usage for use in usage_efctv:
 * from:  0.0 to parent->usage->usage_raw
 * to:    0.0 to 1.0
 *
 * In LEVEL_BASED, usage_efctv is the normalized usage within the account
 */
static double _level_based_calc_assoc_usage(slurmdb_association_rec_t *assoc)
{
	double norm = 0.0l;
	slurmdb_association_rec_t *parent = find_real_parent(assoc);

	if (parent && parent->usage->usage_raw)
		norm = NORMALIZE_VALUE(
			assoc->usage->usage_raw,
			0.0L, (long double) parent->usage->usage_raw,
			0.0L, 1.0L
		);

	return norm;
}


extern void priority_p_set_assoc_usage(slurmdb_association_rec_t *assoc)
{
	char *child;
	char *child_str;

	xassert(assoc_mgr_root_assoc);
	xassert(assoc);
	xassert(assoc->usage);
	xassert(assoc->usage->parent_assoc_ptr);

	if (assoc->user) {
		child = "user";
		child_str = assoc->user;
	} else {
		child = "account";
		child_str = assoc->acct;
	}

	if (assoc_mgr_root_assoc->usage->usage_raw) {
		assoc->usage->usage_norm = assoc->usage->usage_raw
			/ assoc_mgr_root_assoc->usage->usage_raw;
	} else {
		/* This should only happen when no usage has occured
		 * at all so no big deal, the other usage should be 0
		 * as well here. */
		assoc->usage->usage_norm = 0;
	}

	if (priority_debug) {
		info("Normalized usage for %s %s off %s %Lf / %Lf = %Lf",
		     child, child_str, assoc->usage->parent_assoc_ptr->acct,
		     assoc->usage->usage_raw,
		     assoc_mgr_root_assoc->usage->usage_raw,
		     assoc->usage->usage_norm);
	}
	/* This is needed in case someone changes the half-life on the
	 * fly and now we have used more time than is available under
	 * the new config */
	if (assoc->usage->usage_norm > 1.0)
		assoc->usage->usage_norm = 1.0;

	if (flags & PRIORITY_FLAGS_LEVEL_BASED)
		assoc->usage->usage_efctv =
			_level_based_calc_assoc_usage(assoc);
	else if (assoc->usage->parent_assoc_ptr == assoc_mgr_root_assoc) {
		assoc->usage->usage_efctv = assoc->usage->usage_norm;
		if (priority_debug)
			info("Effective usage for %s %s off %s %Lf %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->usage_efctv,
			     assoc->usage->usage_norm);
	} else if (flags & PRIORITY_FLAGS_TICKET_BASED) {
		_ticket_based_set_usage_efctv(assoc);
		if (priority_debug) {
			info("Effective usage for %s %s off %s = %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->usage_efctv);
		}
	} else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		slurmdb_association_rec_t *parent_assoc =
			assoc->usage->parent_assoc_ptr;

		assoc->usage->usage_efctv =
			parent_assoc->usage->usage_efctv;
		if (priority_debug) {
			info("Effective usage for %s %s off %s %Lf",
			     child, child_str,
			     parent_assoc->acct,
			     parent_assoc->usage->usage_efctv);
		}
	} else if (flags & PRIORITY_FLAGS_DEPTH_OBLIVIOUS) {
		_depth_oblivious_set_usage_efctv(assoc, child, child_str);
	} else {
		_set_usage_efctv(assoc);
		if (priority_debug) {
			info("Effective usage for %s %s off %s "
			     "%Lf + ((%Lf - %Lf) * %d / %d) = %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->usage_norm,
			     assoc->usage->parent_assoc_ptr->usage->usage_efctv,
			     assoc->usage->usage_norm,
			     assoc->shares_raw,
			     assoc->usage->level_shares,
			     assoc->usage->usage_efctv);
		}
	}
}


extern double priority_p_calc_fs_factor(long double usage_efctv,
					long double shares_norm)
{
	double priority_fs = 0.0;

	if (fuzzy_equal(usage_efctv, NO_VAL))
		return priority_fs;

	if (shares_norm <= 0)
		return priority_fs;

	if (flags & PRIORITY_FLAGS_TICKET_BASED) {
		if (usage_efctv < MIN_USAGE_FACTOR * shares_norm)
			usage_efctv = MIN_USAGE_FACTOR * shares_norm;
		priority_fs = shares_norm / usage_efctv;
	} else {
		priority_fs =
			pow(2.0, -((usage_efctv/shares_norm) / damp_factor));
	}

	return priority_fs;
}

extern List priority_p_get_priority_factors_list(
	priority_factors_request_msg_t *req_msg, uid_t uid)
{
	List req_job_list;
	List req_user_list;
	List ret_list = NULL;
	ListIterator itr;
	priority_factors_object_t *obj = NULL;
	struct job_record *job_ptr = NULL;
	time_t start_time = time(NULL);

	/* Read lock on jobs, nodes, and partitions */
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };

	xassert(req_msg);
	req_job_list = req_msg->job_id_list;
	req_user_list = req_msg->uid_list;

	lock_slurmctld(job_read_lock);
	if (job_list && list_count(job_list)) {
		ret_list = list_create(slurm_destroy_priority_factors_object);
		itr = list_iterator_create(job_list);
		while ((job_ptr = list_next(itr))) {
			/*
			 * We are only looking for pending jobs
			 */
			if (!IS_JOB_PENDING(job_ptr))
				continue;

			/*
			 * This means the job is not eligible yet
			 */
			if (!job_ptr->details->begin_time
			    || (job_ptr->details->begin_time > start_time))
				continue;

			/*
			 * 0 means the job is held
			 */
			if (job_ptr->priority == 0)
				continue;

			/*
			 * Priority has been set elsewhere (e.g. by SlurmUser)
			 */
			if (job_ptr->direct_set_prio)
				continue;

			if (_filter_job(job_ptr, req_job_list, req_user_list))
				continue;

			if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS)
			    && (job_ptr->user_id != uid)
			    && !validate_operator(uid)
			    && !assoc_mgr_is_user_acct_coord(
				    acct_db_conn, uid,
				    job_ptr->account))
				continue;

			obj = xmalloc(sizeof(priority_factors_object_t));
			memcpy(obj, job_ptr->prio_factors,
			       sizeof(priority_factors_object_t));
			obj->job_id = job_ptr->job_id;
			obj->user_id = job_ptr->user_id;
			list_append(ret_list, obj);
		}
		list_iterator_destroy(itr);
		if (!list_count(ret_list)) {
			list_destroy(ret_list);
			ret_list = NULL;
		}
	}
	unlock_slurmctld(job_read_lock);

	return ret_list;
}

/* at least slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK,
 * READ_LOCK, READ_LOCK }; should be locked before calling this */
extern void priority_p_job_end(struct job_record *job_ptr)
{
	if (priority_debug)
		info("priority_p_job_end: called for job %u", job_ptr->job_id);

	_apply_new_usage(job_ptr, g_last_ran, time(NULL));
}
