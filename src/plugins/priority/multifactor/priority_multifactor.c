/*****************************************************************************\
 *  priority_multifactor.c - slurm multifactor priority plugin.
 *****************************************************************************
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

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>

#include "slurm/slurm_errno.h"

#include "src/common/parse_time.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_time.h"
#include "src/common/xstring.h"
#include "src/common/gres.h"

#include "src/slurmctld/licenses.h"
#include "src/slurmctld/read_config.h"

#include "fair_tree.h"

#define SECS_PER_DAY	(24 * 60 * 60)
#define SECS_PER_WEEK	(7 * SECS_PER_DAY)

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
int slurmctld_tres_cnt __attribute__((weak_import)) = 0;
int accounting_enforce __attribute__((weak_import)) = 0;
#else
void *acct_db_conn = NULL;
uint32_t cluster_cpus = NO_VAL;
List job_list = NULL;
time_t last_job_update = (time_t) 0;
uint16_t part_max_priority = 0;
slurm_ctl_conf_t slurmctld_conf;
int slurmctld_tres_cnt = 0;
int accounting_enforce = 0;
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]	= "Priority MULTIFACTOR plugin";
const char plugin_type[]	= "priority/multifactor";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

static pthread_t decay_handler_thread;
static pthread_mutex_t decay_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t decay_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t decay_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t decay_init_cond = PTHREAD_COND_INITIALIZER;
static bool running_decay = 0, reconfig = 0, calc_fairshare = 1;
static time_t plugin_shutdown = 0;
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
static double  *weight_tres; /* tres weights */
static uint32_t flags;       /* Priority Flags */
static uint32_t prevflags;    /* Priority Flags before _internal_setup() resets
			       * flags after a reconfigure */
static time_t g_last_ran = 0; /* when the last poll ran */
static double decay_factor = 1; /* The decay factor when decaying time. */

/* variables defined in prirority_multifactor.h */
bool priority_debug = 0;

static void _priority_p_set_assoc_usage_debug(slurmdb_assoc_rec_t *assoc);
static void _set_assoc_usage_efctv(slurmdb_assoc_rec_t *assoc);

/*
 * apply decay factor to all associations usage_raw
 * IN: real_decay - decay to be applied to each associations' used
 * shares.  This should already be modified with the amount of delta
 * time from last application..
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
static int _apply_decay(double real_decay)
{
	int i;
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	/* continue if real_decay is 0 or 1 since that doesn't help
	   us at all. 1 means no decay and 0 will just zero
	   everything out so don't waste time doing it */
	if (!real_decay)
		return SLURM_ERROR;
	else if (!calc_fairshare || (real_decay == 1))
		return SLURM_SUCCESS;

	assoc_mgr_lock(&locks);

	xassert(assoc_mgr_assoc_list);
	xassert(assoc_mgr_qos_list);

	itr = list_iterator_create(assoc_mgr_assoc_list);
	/* We want to do this to all associations including root.
	   All usage_raws are calculated from the bottom up.
	*/
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw *= real_decay;
		for (i=0; i<slurmctld_tres_cnt; i++)
			assoc->usage->usage_tres_raw[i] *= real_decay;
		assoc->usage->grp_used_wall *= real_decay;
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((qos = list_next(itr))) {
		if (qos->flags & QOS_FLAG_NO_DECAY)
			continue;
		qos->usage->usage_raw *= real_decay;
		for (i=0; i<slurmctld_tres_cnt; i++)
			qos->usage->usage_tres_raw[i] *= real_decay;
		qos->usage->grp_used_wall *= real_decay;
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	return SLURM_SUCCESS;
}

/*
 * reset usage_raw, and grp_used_wall on all assocs
 * This should be called every PriorityUsageResetPeriod
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
static int _reset_usage(void)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	int i;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!calc_fairshare)
		return SLURM_SUCCESS;

	assoc_mgr_lock(&locks);

	xassert(assoc_mgr_assoc_list);

	itr = list_iterator_create(assoc_mgr_assoc_list);
	/* We want to do this to all associations including root.
	 * All usage_raws are calculated from the bottom up.
	 */
	while ((assoc = list_next(itr))) {
		assoc->usage->usage_raw = 0;
		for (i=0; i<slurmctld_tres_cnt; i++)
			assoc->usage->usage_tres_raw[i] = 0;
		assoc->usage->grp_used_wall = 0;
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((qos = list_next(itr))) {
		qos->usage->usage_raw = 0;
		for (i=0; i<slurmctld_tres_cnt; i++)
			qos->usage->usage_tres_raw[i] = 0;
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
	if (!ignore_state_errors)
		fatal("Incomplete priority last decay file exiting, start with '-i' to ignore this");
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

	if (!xstrcmp(slurmctld_conf.state_save_location, "/dev/null")) {
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


/* This should initially get the children list from assoc_mgr_root_assoc.
 * Since our algorithm goes from top down we calculate all the non-user
 * associations now.  When a user submits a job, that norm_fairshare is
 * calculated.  Here we will set the usage_efctv to NO_VAL for users to not have
 * to calculate a bunch of things that will never be used. (Fair Tree calls a
 * different function.)
 *
 * NOTE: acct_mgr_assoc_lock must be locked before this is called.
 */
static int _set_children_usage_efctv(List children_list)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	ListIterator itr = NULL;

	if (!children_list || !list_count(children_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		if (assoc->user) {
			assoc->usage->usage_efctv = (long double)NO_VAL;
			continue;
		}
		priority_p_set_assoc_usage(assoc);
		_set_children_usage_efctv(assoc->usage->children_list);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}


/* job_ptr should already have the partition priority and such added here
 * before had we will be adding to it
 */
static double _get_fairshare_priority(struct job_record *job_ptr)
{
	slurmdb_assoc_rec_t *job_assoc;
	slurmdb_assoc_rec_t *fs_assoc = NULL;
	double priority_fs = 0.0;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	if (!calc_fairshare)
		return 0;

	assoc_mgr_lock(&locks);

	job_assoc = job_ptr->assoc_ptr;

	if (!job_assoc) {
		assoc_mgr_unlock(&locks);
		error("Job %u has no association.  Unable to "
		      "compute fairshare.", job_ptr->job_id);
		return 0;
	}

	/* Use values from parent when FairShare=SLURMDB_FS_USE_PARENT */
	if (job_assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		fs_assoc = job_assoc->usage->fs_assoc_ptr;
	else
		fs_assoc = job_assoc;

	if (fuzzy_equal(fs_assoc->usage->usage_efctv, NO_VAL))
		priority_p_set_assoc_usage(fs_assoc);

	/* Priority is 0 -> 1 */
	if (flags & PRIORITY_FLAGS_FAIR_TREE) {
		priority_fs = job_assoc->usage->fs_factor;
		if (priority_debug) {
			info("Fairhare priority of job %u for user %s in acct"
			     " %s is %f",
			     job_ptr->job_id, job_assoc->user, job_assoc->acct,
			     priority_fs);
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

/* Returns the priority after applying the weight factors */
static uint32_t _get_priority_internal(time_t start_time,
				       struct job_record *job_ptr)
{
	double priority	= 0.0;
	priority_factors_object_t pre_factors;
	uint64_t tmp_64;
	double tmp_tres = 0.0;

	if (job_ptr->direct_set_prio && (job_ptr->priority > 0)) {
		if (job_ptr->prio_factors) {
			xfree(job_ptr->prio_factors->tres_weights);
			xfree(job_ptr->prio_factors->priority_tres);
			memset(job_ptr->prio_factors, 0,
			       sizeof(priority_factors_object_t));
		}
		return job_ptr->priority;
	}

	if (!job_ptr->details) {
		error("_get_priority_internal: job %u does not have a "
		      "details symbol set, can't set priority",
		      job_ptr->job_id);
		if (job_ptr->prio_factors) {
			xfree(job_ptr->prio_factors->tres_weights);
			xfree(job_ptr->prio_factors->priority_tres);
			memset(job_ptr->prio_factors, 0,
			       sizeof(priority_factors_object_t));
		}
		return 0;
	}

	set_priority_factors(start_time, job_ptr);

	if (priority_debug) {
		memcpy(&pre_factors, job_ptr->prio_factors,
		       sizeof(priority_factors_object_t));
		if (job_ptr->prio_factors->priority_tres) {
			pre_factors.priority_tres = xmalloc(sizeof(double) *
							    slurmctld_tres_cnt);
			memcpy(pre_factors.priority_tres,
			       job_ptr->prio_factors->priority_tres,
			       sizeof(double) * slurmctld_tres_cnt);
		}
	} else	/* clang needs this memset to avoid a warning */
		memset(&pre_factors, 0, sizeof(priority_factors_object_t));

	job_ptr->prio_factors->priority_age  *= (double)weight_age;
	job_ptr->prio_factors->priority_fs   *= (double)weight_fs;
	job_ptr->prio_factors->priority_js   *= (double)weight_js;
	job_ptr->prio_factors->priority_part *= (double)weight_part;
	job_ptr->prio_factors->priority_qos  *= (double)weight_qos;

	if (weight_tres && job_ptr->prio_factors->priority_tres) {
		int i;
		double *tres_factors = NULL;
		tres_factors = job_ptr->prio_factors->priority_tres;

		for (i = 0; i < slurmctld_tres_cnt; i++) {
			tres_factors[i] *= weight_tres[i];
			tmp_tres += tres_factors[i];
		}
	}

	priority = job_ptr->prio_factors->priority_age
		+ job_ptr->prio_factors->priority_fs
		+ job_ptr->prio_factors->priority_js
		+ job_ptr->prio_factors->priority_part
		+ job_ptr->prio_factors->priority_qos
		+ tmp_tres
		- (double)(((int64_t)job_ptr->prio_factors->nice)
			   - NICE_OFFSET);

	/* Priority 0 is reserved for held jobs */
	if (priority < 1)
		priority = 1;

	tmp_64 = (uint64_t) priority;
	if (tmp_64 > 0xffffffff) {
		error("Job %u priority exceeds 32 bits", job_ptr->job_id);
		tmp_64 = 0xffffffff;
		priority = (double) tmp_64;
	}

	if (job_ptr->part_ptr_list) {
		struct part_record *part_ptr;
		double priority_part;
		ListIterator part_iterator;
		int i = 0;

		if (!job_ptr->priority_array) {
			i = list_count(job_ptr->part_ptr_list) + 1;
			job_ptr->priority_array = xmalloc(sizeof(uint32_t) * i);
		}

		i = 0;
		list_sort(job_ptr->part_ptr_list, priority_sort_part_tier);
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = (struct part_record *)
			list_next(part_iterator))) {
			priority_part = part_ptr->priority_job_factor /
				(double)part_max_priority *
				(double)weight_part;
			priority_part +=
				 (job_ptr->prio_factors->priority_age
				 + job_ptr->prio_factors->priority_fs
				 + job_ptr->prio_factors->priority_js
				 + job_ptr->prio_factors->priority_qos
				 + tmp_tres
				 - (double)
				   (((uint64_t)job_ptr->prio_factors->nice)
				    - NICE_OFFSET));

			/* Priority 0 is reserved for held jobs */
			if (priority_part < 1)
				priority_part = 1;

			tmp_64 = (uint64_t) priority_part;
			if (tmp_64 > 0xffffffff) {
				error("Job %u priority exceeds 32 bits",
				      job_ptr->job_id);
				tmp_64 = 0xffffffff;
				priority_part = (double) tmp_64;
			}
			if (((flags & PRIORITY_FLAGS_INCR_ONLY) == 0) ||
			    (job_ptr->priority_array[i] <
			     (uint32_t) priority_part)) {
				job_ptr->priority_array[i] =
					(uint32_t) priority_part;
			}
			debug("Job %u has more than one partition (%s)(%u)",
			      job_ptr->job_id, part_ptr->name,
			      job_ptr->priority_array[i]);
			i++;
		}
		list_iterator_destroy(part_iterator);
	}

	if (priority_debug) {
		int i;
		double *post_tres_factors =
			job_ptr->prio_factors->priority_tres;
		double *pre_tres_factors = pre_factors.priority_tres;
		assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
					   READ_LOCK, NO_LOCK, NO_LOCK };

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

		if (weight_tres && pre_tres_factors && post_tres_factors) {
			assoc_mgr_lock(&locks);
			for(i = 0; i < slurmctld_tres_cnt; i++) {
				if (!post_tres_factors[i])
					continue;
				info("Weighted TRES:%s is %f * %.2f = %.2f",
				     assoc_mgr_tres_name_array[i],
				     pre_tres_factors[i], weight_tres[i],
				     post_tres_factors[i]);
			}
			assoc_mgr_unlock(&locks);
		}

		info("Job %u priority: %.2f + %.2f + %.2f + %.2f + %.2f + %2.f "
		     "- %"PRId64" = %.2f",
		     job_ptr->job_id, job_ptr->prio_factors->priority_age,
		     job_ptr->prio_factors->priority_fs,
		     job_ptr->prio_factors->priority_js,
		     job_ptr->prio_factors->priority_part,
		     job_ptr->prio_factors->priority_qos,
		     tmp_tres,
		     (((int64_t)job_ptr->prio_factors->nice) - NICE_OFFSET),
		     priority);

		xfree(pre_factors.priority_tres);
	}
	return (uint32_t)priority;
}


/* based upon the last reset time, compute when the next reset should be */
static time_t _next_reset(uint16_t reset_period, time_t last_reset)
{
	struct tm last_tm;
	time_t tmp_time, now = time(NULL);

	if (slurm_localtime_r(&last_reset, &last_tm) == NULL)
		return (time_t) 0;

	last_tm.tm_sec   = 0;
	last_tm.tm_min   = 0;
	last_tm.tm_hour  = 0;
/*	last_tm.tm_wday = 0	ignored */
/*	last_tm.tm_yday = 0;	ignored */
	switch (reset_period) {
	case PRIORITY_RESET_DAILY:
		tmp_time = slurm_mktime(&last_tm);
		tmp_time += SECS_PER_DAY;
		while ((tmp_time + SECS_PER_DAY) < now)
			tmp_time += SECS_PER_DAY;
		return tmp_time;
	case PRIORITY_RESET_WEEKLY:
		tmp_time = slurm_mktime(&last_tm);
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
	return slurm_mktime(&last_tm);
}

static void _handle_qos_tres_run_secs(long double *tres_run_decay,
				      uint64_t *tres_run_delta,
				      uint32_t job_id,
				      slurmdb_qos_rec_t *qos)
{
	int i;

	if (!qos || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	for (i=0; i<slurmctld_tres_cnt; i++) {
		if (i == TRES_ARRAY_ENERGY)
			continue;
		if (tres_run_decay)
			qos->usage->usage_tres_raw[i] += tres_run_decay[i];

		if (tres_run_delta[i] >
		    qos->usage->grp_used_tres_run_secs[i]) {
			error("_handle_qos_tres_run_secs: job %u: "
			      "QOS %s TRES %s grp_used_tres_run_secs "
			      "underflow, tried to remove %"PRIu64" seconds "
			      "when only %"PRIu64" remained.",
			      job_id,
			      qos->name,
			      assoc_mgr_tres_name_array[i],
			      tres_run_delta[i],
			      qos->usage->grp_used_tres_run_secs[i]);
			qos->usage->grp_used_tres_run_secs[i] = 0;
		} else
			qos->usage->grp_used_tres_run_secs[i] -=
				tres_run_delta[i];

		if (priority_debug) {
			info("_handle_qos_tres_run_secs: job %u: "
			     "Removed %"PRIu64" unused seconds "
			     "from QOS %s TRES %s "
			     "grp_used_tres_run_secs = %"PRIu64,
			     job_id,
			     tres_run_delta[i],
			     qos->name,
			     assoc_mgr_tres_name_array[i],
			     qos->usage->grp_used_tres_run_secs[i]);
		}
	}
}

static void _handle_assoc_tres_run_secs(long double *tres_run_decay,
					uint64_t *tres_run_delta,
					uint32_t job_id,
					slurmdb_assoc_rec_t *assoc)
{
	int i;

	if (!assoc || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return;

	for (i=0; i<slurmctld_tres_cnt; i++) {
		if (i == TRES_ARRAY_ENERGY)
			continue;
		if (tres_run_decay)
			assoc->usage->usage_tres_raw[i] += tres_run_decay[i];

		if (tres_run_delta[i] >
		    assoc->usage->grp_used_tres_run_secs[i]) {
			error("_handle_assoc_tres_run_secs: job %u: "
			      "assoc %u TRES %s grp_used_tres_run_secs "
			      "underflow, tried to remove %"PRIu64" seconds "
			      "when only %"PRIu64" remained.",
			      job_id,
			      assoc->id,
			      assoc_mgr_tres_name_array[i],
			      tres_run_delta[i],
			      assoc->usage->grp_used_tres_run_secs[i]);
			assoc->usage->grp_used_tres_run_secs[i] = 0;
		} else
			assoc->usage->grp_used_tres_run_secs[i] -=
				tres_run_delta[i];

		if (priority_debug) {
			info("_handle_assoc_tres_run_secs: job %u: "
			     "Removed %"PRIu64" unused seconds "
			     "from assoc %d TRES %s "
			     "grp_used_tres_run_secs = %"PRIu64,
			     job_id,
			     tres_run_delta[i],
			     assoc->id,
			     assoc_mgr_tres_name_array[i],
			     assoc->usage->grp_used_tres_run_secs[i]);
		}
	}
}

static void _handle_tres_run_secs(uint64_t *tres_run_delta,
				  struct job_record *job_ptr)
{

	slurmdb_assoc_rec_t *assoc = job_ptr->assoc_ptr;

	_handle_qos_tres_run_secs(NULL, tres_run_delta,
				  job_ptr->job_id, job_ptr->qos_ptr);
	while (assoc) {
		_handle_assoc_tres_run_secs(NULL, tres_run_delta,
					    job_ptr->job_id, assoc);
		assoc = assoc->usage->parent_assoc_ptr;
	}
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
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	uint64_t tres_run_delta[slurmctld_tres_cnt];
	int i;

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

		/* If end_time_exp is NO_VAL we have already ran the end for
		 * this job.  We don't want to do it again, so just exit.
		 */
		if (job_ptr->end_time_exp == (time_t)NO_VAL)
			continue;

		if (!IS_JOB_RUNNING(job_ptr))
			continue;

		if (job_ptr->start_time > last_ran)
			continue;

		for (i=0; i<slurmctld_tres_cnt; i++) {
			tres_run_delta[i] =
				(uint64_t)(last_ran - job_ptr->start_time) *
				job_ptr->tres_alloc_cnt[i];
		}

		_handle_tres_run_secs(tres_run_delta, job_ptr);
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
			    time_t start_period, time_t end_period,
			    bool adjust_for_end)
{
	slurmdb_qos_rec_t *qos;
	slurmdb_assoc_rec_t *assoc;
	double run_delta = 0.0, run_decay = 0.0, run_nodecay = 0.0;
	double billable_tres = 0.0;
	double real_decay = 0.0, real_nodecay = 0.0;
	uint64_t tres_run_delta[slurmctld_tres_cnt];
	long double tres_run_decay[slurmctld_tres_cnt];
	long double tres_run_nodecay[slurmctld_tres_cnt];
	uint64_t tres_time_delta = 0;
	int i;
	uint64_t job_time_limit_ends = 0;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	/* If end_time_exp is NO_VAL we have already ran the end for
	 * this job.  We don't want to do it again, so just exit.
	 */
	if (job_ptr->end_time_exp == (time_t)NO_VAL)
		return SLURM_SUCCESS;

	/* Even if job_ptr->qos_ptr->usage_factor is 0 we need to
	 * handle other non-usage variables here
	 * (grp_used_cpu_run_secs), so don't return.
	 */

	if (job_ptr->start_time > start_period)
		start_period = job_ptr->start_time;

	/* Only change the end_time if we are at the end of the job.
	 * If the job is running over the time limit the end_time will
	 * be in the past.
	 */
	if (job_ptr->end_time && adjust_for_end
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
		tres_time_delta = 0;
	else if (IS_JOB_FINISHED(job_ptr) || IS_JOB_COMPLETING(job_ptr) ||
		 IS_JOB_RESIZING(job_ptr)) {
		/* If a job is being requeued sometimes the state will
		   be pending + completing so handle that the same as
		   finished so we don't leave time in the mix.
		*/
		tres_time_delta = (job_time_limit_ends -
				   (uint64_t)start_period);
	} else if (end_period > job_ptr->end_time_exp) {
		int end_exp = difftime(job_ptr->end_time_exp, start_period);

		if (end_exp > 0)
			tres_time_delta = (uint64_t)end_exp;
	} else
		tres_time_delta = run_delta;

	/* make sure we only run through this once at the end */
	if (adjust_for_end)
		job_ptr->end_time_exp = (time_t)NO_VAL;

	if (priority_debug) {
		info("job %u ran for %g seconds with TRES counts of",
		     job_ptr->job_id, run_delta);
		if (job_ptr->tres_alloc_cnt) {
			for (i=0; i<slurmctld_tres_cnt; i++) {
				if (!job_ptr->tres_alloc_cnt[i])
					continue;
				info("TRES %s: %"PRIu64,
				     assoc_mgr_tres_name_array[i],
				     job_ptr->tres_alloc_cnt[i]);
			}
		} else
			info("No alloced TRES, state is %s",
			     job_state_string(job_ptr->job_state));
	}
	/* get the time in decayed fashion */
	run_decay = run_delta * pow(decay_factor, run_delta);
	/* clang needs these memset to avoid a warning */
	memset(tres_run_decay, 0, sizeof(tres_run_decay));
	memset(tres_run_nodecay, 0, sizeof(tres_run_nodecay));
	memset(tres_run_delta, 0, sizeof(tres_run_delta));
	assoc_mgr_lock(&locks);

	billable_tres = calc_job_billable_tres(job_ptr, start_period, true);
	real_decay    = run_decay * billable_tres;
	real_nodecay  = run_delta * billable_tres;
	run_nodecay   = run_delta;

	qos = job_ptr->qos_ptr;
	if (qos && (qos->usage_factor >= 0)) {
		real_decay *= qos->usage_factor;
		run_decay  *= qos->usage_factor;
		real_nodecay *= qos->usage_factor;
		run_nodecay  *= qos->usage_factor;
	}
	if (job_ptr->tres_alloc_cnt) {
		for (i=0; i<slurmctld_tres_cnt; i++) {
			if (!job_ptr->tres_alloc_cnt[i])
				continue;
			tres_run_delta[i] = tres_time_delta *
				job_ptr->tres_alloc_cnt[i];
			tres_run_decay[i] = (long double)run_decay *
				(long double)job_ptr->tres_alloc_cnt[i];
			tres_run_nodecay[i] = (long double)run_nodecay *
				(long double)job_ptr->tres_alloc_cnt[i];
		}
	}

	assoc = job_ptr->assoc_ptr;

	/* now apply the usage factor for this qos */
	if (qos) {
		if (qos->flags & QOS_FLAG_NO_DECAY) {
			qos->usage->grp_used_wall += run_nodecay;
			qos->usage->usage_raw += (long double)real_nodecay;

			_handle_qos_tres_run_secs(tres_run_nodecay,
						  tres_run_delta,
						  job_ptr->job_id, qos);
		} else {
			qos->usage->grp_used_wall += run_decay;
			qos->usage->usage_raw += (long double)real_decay;

			_handle_qos_tres_run_secs(tres_run_decay,
						  tres_run_delta,
						  job_ptr->job_id, qos);
		}
	}

	/* sanity check, there should always be a part_ptr here, but only do
	 * the qos if it isn't the same qos as the job is using */
	if (job_ptr->part_ptr && (job_ptr->part_ptr->qos_ptr != qos))
		qos = job_ptr->part_ptr->qos_ptr;
	else
		qos = NULL;

	/* now apply the usage factor for this qos */
	if (qos) {
		/* usage factor only matter's on the job qos */
		/* if (qos->usage_factor >= 0) { */
		/* 	real_decay *= qos->usage_factor; */
		/* 	run_decay *= qos->usage_factor; */
		/* } */
		if (qos->flags & QOS_FLAG_NO_DECAY) {
			qos->usage->grp_used_wall += run_nodecay;
			qos->usage->usage_raw += (long double)real_nodecay;

			_handle_qos_tres_run_secs(tres_run_nodecay,
						  tres_run_delta,
						  job_ptr->job_id, qos);
		} else {
			qos->usage->grp_used_wall += run_decay;
			qos->usage->usage_raw += (long double)real_decay;

			_handle_qos_tres_run_secs(tres_run_decay,
						  tres_run_delta,
						  job_ptr->job_id, qos);
		}
	}


	/* We want to do this all the way up
	 * to and including root.  This way we
	 * can keep track of how much usage
	 * has occured on the entire system
	 * and use that to normalize against. */
	while (assoc) {
		assoc->usage->grp_used_wall += run_decay;
		assoc->usage->usage_raw += (long double)real_decay;
		if (priority_debug)
			info("Adding %f new usage to assoc %u (%s/%s/%s) "
			     "raw usage is now %Lf.  Group wall "
			     "added %f making it %f.",
			     real_decay, assoc->id, assoc->acct,
			     assoc->user, assoc->partition,
			     assoc->usage->usage_raw, run_decay,
			     assoc->usage->grp_used_wall);
		_handle_assoc_tres_run_secs(tres_run_decay, tres_run_delta,
					    job_ptr->job_id, assoc);

		assoc = assoc->usage->parent_assoc_ptr;
	}
	assoc_mgr_unlock(&locks);
	return 1;
}


static int _decay_apply_new_usage_and_weighted_factors(
	struct job_record *job_ptr,
	time_t *start_time_ptr)
{
	/* Always return SUCCESS so that list_for_each will
	 * continue processing list of jobs. */

	if (!decay_apply_new_usage(job_ptr, start_time_ptr))
		return SLURM_SUCCESS;

	decay_apply_weighted_factors(job_ptr, start_time_ptr);

	return SLURM_SUCCESS;
}


static void *_decay_thread(void *no_data)
{
	time_t start_time = time(NULL);
	time_t last_reset = 0, next_reset = 0;
	uint32_t calc_period = slurm_get_priority_calc_period();
	double decay_hl = (double)slurm_get_priority_decay_hl();
	uint16_t reset_period = slurm_get_priority_reset_period();

	time_t now;
	double run_delta = 0.0, real_decay = 0.0;
	struct timeval tvnow;
	struct timespec abs;

	/* Write lock on jobs, read lock on nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "decay", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "decay");
	}
#endif
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

	slurm_mutex_lock(&decay_init_mutex);

	if (decay_hl > 0)
		decay_factor = 1 - (0.693 / decay_hl);

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec;
	abs.tv_nsec = tvnow.tv_usec * 1000;

	_read_last_decay_ran(&g_last_ran, &last_reset);
	if (last_reset == 0)
		last_reset = start_time;

	slurm_cond_signal(&decay_init_cond);
	slurm_mutex_unlock(&decay_init_mutex);

	_init_grp_used_cpu_run_secs(g_last_ran);

	while (!plugin_shutdown) {
		now = start_time;

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

		/* Calculate all the normalized usage unless this is Fair Tree;
		 * it handles these calculations during its tree traversal */
		if (!(flags & PRIORITY_FLAGS_FAIR_TREE)) {
			assoc_mgr_lock(&locks);
			_set_children_usage_efctv(
				assoc_mgr_root_assoc->usage->children_list);
			assoc_mgr_unlock(&locks);
		}

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

		if (!(flags & PRIORITY_FLAGS_FAIR_TREE)) {
			lock_slurmctld(job_write_lock);
			list_for_each(
				job_list,
				(ListForF) _decay_apply_new_usage_and_weighted_factors,
				&start_time
				);
			unlock_slurmctld(job_write_lock);
		}

	get_usage:
		if (flags & PRIORITY_FLAGS_FAIR_TREE)
			fair_tree_decay(job_list, start_time);

		g_last_ran = start_time;

		_write_last_decay_ran(g_last_ran, last_reset);

		running_decay = 0;

		/* Sleep until the next time. */
		abs.tv_sec += calc_period;
		slurm_cond_timedwait(&decay_cond, &decay_lock, &abs);
		slurm_mutex_unlock(&decay_lock);

		start_time = time(NULL);
		/* repeat ;) */
	}
	return NULL;
}

/* If the specified job record satisfies the filter specifications in req_msg
 * and part_ptr_list (partition name filters), then add its priority specs
 * to ret_list */
static void _filter_job(struct job_record *job_ptr,
			priority_factors_request_msg_t *req_msg,
			List part_ptr_list, List ret_list)
{
	priority_factors_object_t *obj = NULL;
	struct part_record *job_part_ptr = NULL, *filter_part_ptr = NULL;
	List req_job_list, req_user_list;
	int filter = 0, inx;
	ListIterator iterator, job_iter, filter_iter;
	uint32_t *job_id;
	uint32_t *user_id;

	/* Filter by job ID */
	req_job_list = req_msg->job_id_list;
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
		if (filter == 1)
			return;
	}

	/* Filter by user/UID */
	req_user_list = req_msg->uid_list;
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
			return;
	}

	/*
	 * Job is not in any partition, so there is nothing to return.
	 * This can happen if the Partition was deleted, CALCULATE_RUNNING
	 * is enabled, and this job is still waiting out MinJobAge before
	 * being removed from the system.
	 */
	if (!job_ptr->part_ptr && !job_ptr->part_ptr_list)
		return;

	/* Filter by partition, job in one partition */
	if (!job_ptr->part_ptr_list) {
		job_part_ptr =  job_ptr->part_ptr;
		filter = 0;
		if (part_ptr_list) {
			filter = 1;
			filter_iter = list_iterator_create(part_ptr_list);
			while ((filter_part_ptr = list_next(filter_iter))) {
				if (filter_part_ptr == job_part_ptr) {
					filter = 0;
					break;
				}
			}
			list_iterator_destroy(filter_iter);
		}

		if (filter == 0) {
			obj = xmalloc(sizeof(priority_factors_object_t));
			slurm_copy_priority_factors_object(obj,
						job_ptr->prio_factors);
			obj->job_id = job_ptr->job_id;
			obj->partition = job_part_ptr->name;
			obj->user_id = job_ptr->user_id;
			list_append(ret_list, obj);
		}
		return;
	}

	/* Filter by partition, job in multiple partitions */
	inx = 0;
	job_iter = list_iterator_create(job_ptr->part_ptr_list);
	while ((job_part_ptr = list_next(job_iter))) {
		filter = 0;
		if (part_ptr_list) {
			filter = 1;
			filter_iter = list_iterator_create(part_ptr_list);
			while ((filter_part_ptr = list_next(filter_iter))) {
				if (filter_part_ptr == job_part_ptr) {
					filter = 0;
					break;
				}
			}
			list_iterator_destroy(filter_iter);
		}

		if (filter == 0) {
			obj = xmalloc(sizeof(priority_factors_object_t));
			slurm_copy_priority_factors_object(obj,
						job_ptr->prio_factors);
			obj->priority_part = job_part_ptr->priority_job_factor /
					     (double)part_max_priority *
					     (double)weight_part;
			obj->job_id = job_ptr->job_id;
			obj->partition = job_part_ptr->name;
			obj->user_id = job_ptr->user_id;
			list_append(ret_list, obj);
		}
		inx++;
	}
	list_iterator_destroy(job_iter);
}

static void _internal_setup(void)
{
	char *tres_weights_str;
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
	xfree(weight_tres);
	if ((tres_weights_str = slurm_get_priority_weight_tres())) {
		weight_tres = slurm_get_tres_weight_array(tres_weights_str,
							  slurmctld_tres_cnt,
							  true);
	}
	xfree(tres_weights_str);
	flags = slurm_get_priority_flags();

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


/* Reursively call assoc_mgr_normalize_assoc_shares from assoc_mgr.c on
 * children of an assoc
 */
static void _set_norm_shares(List children_list)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!children_list || list_is_empty(children_list))
		return;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr))) {
		assoc_mgr_normalize_assoc_shares(assoc);
		if (!assoc->user)
			_set_norm_shares(assoc->usage->children_list);
	}

	list_iterator_destroy(itr);
}


static void _depth_oblivious_set_usage_efctv(slurmdb_assoc_rec_t *assoc)
{
	long double ratio_p, ratio_l, k, f, ratio_s;
	slurmdb_assoc_rec_t *parent_assoc = NULL;
	ListIterator sib_itr = NULL;
	slurmdb_assoc_rec_t *sibling = NULL;
	char *child;
	char *child_str;

	if (assoc->user) {
		child = "user";
		child_str = assoc->user;
	} else {
		child = "account";
		child_str = assoc->acct;
	}

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
	parent_assoc =  assoc->usage->fs_assoc_ptr;

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
			info("Effective usage for %s %s off %s(%s) "
			     "(%Lf * %Lf ^ %Lf) * %f  = %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->fs_assoc_ptr->acct,
			     ratio_p, ratio_l, k,
			     assoc->usage->shares_norm,
			     assoc->usage->usage_efctv);
		}
	} else {
		assoc->usage->usage_efctv = assoc->usage->usage_norm;
		if (priority_debug) {
			info("Effective usage for %s %s off %s(%s) %Lf",
			     child, child_str,
			     assoc->usage->parent_assoc_ptr->acct,
			     assoc->usage->fs_assoc_ptr->acct,
			     assoc->usage->usage_efctv);
		}
	}
}

static void _set_usage_efctv(slurmdb_assoc_rec_t *assoc)
{
	/* Variable names taken from HTML documentation */
	long double ua_child = assoc->usage->usage_norm;
	long double ue_parent =
		assoc->usage->fs_assoc_ptr->usage->usage_efctv;
	uint32_t s_child = assoc->shares_raw;
	uint32_t s_all_siblings = assoc->usage->level_shares;

	/* If no user in the account has shares, avoid division by zero by
	 * setting usage_efctv to the parent's usage_efctv */
	if (!s_all_siblings)
		assoc->usage->usage_efctv = ue_parent;
	else
		assoc->usage->usage_efctv = ua_child +
			(ue_parent - ua_child) *
			(s_child / (long double) s_all_siblings);
}


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	char *temp = NULL;
	/* Write lock on jobs, read lock on nodes and partitions */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	/* This means we aren't running from the controller so skip setup. */
	if (cluster_cpus == NO_VAL) {
		damp_factor = (long double)slurm_get_fs_dampening_factor();
		return SLURM_SUCCESS;
	}

	_internal_setup();

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_accounting_storage_type();
	if (xstrcasecmp(temp, "accounting_storage/slurmdbd")
	    && xstrcasecmp(temp, "accounting_storage/mysql")) {
		time_t start_time = time(NULL);
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

		/* Initialize job priority factors for valid sprio output */
		lock_slurmctld(job_write_lock);
		list_for_each(
			job_list,
			(ListForF) _decay_apply_new_usage_and_weighted_factors,
			&start_time);
		unlock_slurmctld(job_write_lock);
	} else if (assoc_mgr_root_assoc) {
		if (!cluster_cpus)
			fatal("We need to have a cluster cpu count "
			      "before we can init the priority/multifactor "
			      "plugin");
		assoc_mgr_root_assoc->usage->usage_efctv = 1.0;

		/* The decay_thread sets up some global variables that are
		 * needed outside of the decay_thread (i.e. decay_factor,
		 * g_last_ran).  These are needed if a job was completing and
		 * the slurmctld was reset.  If they aren't setup before
		 * continuing we could get more time added than should be on a
		 * restart.  So wait until they are set up. Set the lock now so
		 * that the decay thread won't trigger the conditional before we
		 * wait for it. */
		slurm_mutex_lock(&decay_init_mutex);

		slurm_thread_create(&decay_handler_thread,
				    _decay_thread, NULL);

		slurm_cond_wait(&decay_init_cond, &decay_init_mutex);
		slurm_mutex_unlock(&decay_init_mutex);
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
	plugin_shutdown = time(NULL);

	/* Daemon termination handled here */
	if (running_decay)
		debug("Waiting for priority decay thread to finish.");

	slurm_mutex_lock(&decay_lock);

	/* signal the decay thread to end */
	if (decay_handler_thread)
		slurm_cond_signal(&decay_cond);

	xfree(weight_tres);

	slurm_mutex_unlock(&decay_lock);

	/* Now join outside the lock */
	if (decay_handler_thread)
		pthread_join(decay_handler_thread, NULL);

	return SLURM_SUCCESS;
}

extern uint32_t priority_p_set(uint32_t last_prio, struct job_record *job_ptr)
{
	uint32_t priority = _get_priority_internal(time(NULL), job_ptr);

	debug2("initial priority for job %u is %u", job_ptr->job_id, priority);

	return priority;
}

extern void priority_p_reconfig(bool assoc_clear)
{
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	reconfig = 1;
	prevflags = flags;
	_internal_setup();

	/* Since Fair Tree uses a different shares calculation method, we
	 * must reassign shares at reconfigure if the algorithm was switched to
	 * or from Fair Tree */
	if ((flags & PRIORITY_FLAGS_FAIR_TREE) !=
	    (prevflags & PRIORITY_FLAGS_FAIR_TREE)) {
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


extern void set_assoc_usage_norm(slurmdb_assoc_rec_t *assoc)
{
	/* If root usage is 0, there is no usage anywhere. */
	if (!assoc_mgr_root_assoc->usage->usage_raw) {
		assoc->usage->usage_norm = 0L;
		return;
	}

	assoc->usage->usage_norm = assoc->usage->usage_raw
		/ assoc_mgr_root_assoc->usage->usage_raw;


	/* This is needed in case someone changes the half-life on the
	 * fly and now we have used more time than is available under
	 * the new config */
	if (assoc->usage->usage_norm > 1L)
		assoc->usage->usage_norm = 1L;
}


extern void priority_p_set_assoc_usage(slurmdb_assoc_rec_t *assoc)
{
	xassert(assoc_mgr_root_assoc);
	xassert(assoc);
	xassert(assoc->usage);
	xassert(assoc->usage->fs_assoc_ptr);

	set_assoc_usage_norm(assoc);
	_set_assoc_usage_efctv(assoc);

	if (priority_debug)
		_priority_p_set_assoc_usage_debug(assoc);
}


extern double priority_p_calc_fs_factor(long double usage_efctv,
					long double shares_norm)
{
	double priority_fs = 0.0;

	if (fuzzy_equal(usage_efctv, NO_VAL))
		return priority_fs;

	if (shares_norm <= 0)
		return priority_fs;

	priority_fs = pow(2.0, -((usage_efctv/shares_norm) / damp_factor));

	return priority_fs;
}

extern List priority_p_get_priority_factors_list(
	priority_factors_request_msg_t *req_msg, uid_t uid)
{
	List ret_list = NULL, part_filter_list = NULL;
	ListIterator itr;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr;
	time_t start_time = time(NULL);
	char *part_str, *tok, *last = NULL;
	/* Read lock on jobs, nodes, and partitions */
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	xassert(req_msg);

	lock_slurmctld(job_read_lock);
	if (req_msg->partitions) {
		part_filter_list = list_create(NULL);
		part_str = xstrdup(req_msg->partitions);
		tok = strtok_r(part_str, ",", &last);
		while (tok) {
			if ((part_ptr = find_part_record(tok)))
				list_append(part_filter_list, part_ptr);
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(part_str);
	}

	if (job_list && list_count(job_list)) {
		time_t use_time;

		ret_list = list_create(slurm_destroy_priority_factors_object);
		itr = list_iterator_create(job_list);
		while ((job_ptr = list_next(itr))) {
			if (!(flags & PRIORITY_FLAGS_CALCULATE_RUNNING) &&
			    !IS_JOB_PENDING(job_ptr))
				continue;

			/* Job is not active on this cluster. */
			if (IS_JOB_REVOKED(job_ptr))
				continue;

			/*
			 * This means the job is not eligible yet
			 */
			if (flags & PRIORITY_FLAGS_ACCRUE_ALWAYS)
				use_time = job_ptr->details->submit_time;
			else
				use_time = job_ptr->details->begin_time;

			if (!use_time || (use_time > start_time))
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

			if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
			    (job_ptr->user_id != uid) &&
			    !validate_operator(uid) &&
			    (((slurm_mcs_get_privatedata() == 0) &&
			      !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
							    job_ptr->account))||
			     ((slurm_mcs_get_privatedata() == 1) &&
			      (mcs_g_check_mcs_label(uid, job_ptr->mcs_label)
			       != 0))))
				continue;

			_filter_job(job_ptr, req_msg, part_filter_list,
				    ret_list);
		}
		list_iterator_destroy(itr);
		if (!list_count(ret_list))
			FREE_NULL_LIST(ret_list);
	}
	unlock_slurmctld(job_read_lock);
	FREE_NULL_LIST(part_filter_list);

	return ret_list;
}

/* at least slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK,
 * READ_LOCK, READ_LOCK, NO_LOCK }; should be locked before calling this */
extern void priority_p_job_end(struct job_record *job_ptr)
{
	if (priority_debug)
		info("priority_p_job_end: called for job %u", job_ptr->job_id);

	_apply_new_usage(job_ptr, g_last_ran, time(NULL), 1);
}

extern bool decay_apply_new_usage(struct job_record *job_ptr,
				  time_t *start_time_ptr)
{

	/* Don't need to handle finished jobs. */
	if (IS_JOB_FINISHED(job_ptr) || IS_JOB_COMPLETING(job_ptr))
		return false;

	/* apply new usage */
	if (((flags & PRIORITY_FLAGS_CALCULATE_RUNNING) ||
	     !IS_JOB_PENDING(job_ptr)) &&
	    !IS_JOB_POWER_UP_NODE(job_ptr) &&
	    job_ptr->start_time && job_ptr->assoc_ptr) {
		if (!_apply_new_usage(job_ptr, g_last_ran, *start_time_ptr, 0))
			return false;
	}
	return true;
}


extern int decay_apply_weighted_factors(struct job_record *job_ptr,
					 time_t *start_time_ptr)
{
	uint32_t new_prio;

	/* Always return SUCCESS so that list_for_each will
	 * continue processing list of jobs. */

	/*
	 * Priority 0 is reserved for held jobs. Also skip priority
	 * re_calculation for non-pending jobs.
	 */
	if ((job_ptr->priority == 0) ||
	    IS_JOB_POWER_UP_NODE(job_ptr) ||
	    (!IS_JOB_PENDING(job_ptr) &&
	     !(flags & PRIORITY_FLAGS_CALCULATE_RUNNING)))
		return SLURM_SUCCESS;

	new_prio = _get_priority_internal(*start_time_ptr, job_ptr);
	if (((flags & PRIORITY_FLAGS_INCR_ONLY) == 0) ||
	    (job_ptr->priority < new_prio)) {
		job_ptr->priority = new_prio;
		last_job_update = time(NULL);
	}

	debug2("priority for job %u is now %u",
	       job_ptr->job_id, job_ptr->priority);

	return SLURM_SUCCESS;
}


extern void set_priority_factors(time_t start_time, struct job_record *job_ptr)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;

	xassert(job_ptr);

	if (!job_ptr->prio_factors) {
		job_ptr->prio_factors =
			xmalloc(sizeof(priority_factors_object_t));
	} else {
		xfree(job_ptr->prio_factors->tres_weights);
		xfree(job_ptr->prio_factors->priority_tres);
		memset(job_ptr->prio_factors, 0,
		       sizeof(priority_factors_object_t));
	}

	qos_ptr = job_ptr->qos_ptr;

	if (weight_age && job_ptr->details->accrue_time) {
		uint32_t diff = 0;

		/*
		 * Only really add an age priority if the
		 * job_ptr->details->accrue_time is past the start_time.
		 */
		if (start_time > job_ptr->details->accrue_time)
			diff = start_time - job_ptr->details->accrue_time;

		if (diff < max_age)
			job_ptr->prio_factors->priority_age =
				(double)diff / (double)max_age;
		else
			job_ptr->prio_factors->priority_age = 1.0;
	}

	if (job_ptr->assoc_ptr && weight_fs) {
		job_ptr->prio_factors->priority_fs =
			_get_fairshare_priority(job_ptr);
	}

	/* FIXME: this should work off the product of TRESBillingWeights */
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

	if (job_ptr->part_ptr && job_ptr->part_ptr->priority_job_factor &&
	    weight_part) {
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

	if (weight_tres) {
		int i;
		double *tres_factors = NULL;

		if (!job_ptr->prio_factors->priority_tres) {
			job_ptr->prio_factors->priority_tres =
				xmalloc(sizeof(double) * slurmctld_tres_cnt);
			job_ptr->prio_factors->tres_weights =
				xmalloc(sizeof(double) * slurmctld_tres_cnt);
			memcpy(job_ptr->prio_factors->tres_weights, weight_tres,
			       sizeof(double) * slurmctld_tres_cnt);
			job_ptr->prio_factors->tres_cnt = slurmctld_tres_cnt;
		}
		tres_factors = job_ptr->prio_factors->priority_tres;

		/* can't memcpy because of different types
		 * uint64_t vs. double */
		for (i = 0; i < slurmctld_tres_cnt; i++) {
			uint64_t value = 0;
			if (job_ptr->tres_alloc_cnt)
				value = job_ptr->tres_alloc_cnt[i];
			else if (job_ptr->tres_req_cnt)
				value = job_ptr->tres_req_cnt[i];

			if (value &&
			    job_ptr->part_ptr &&
			    job_ptr->part_ptr->tres_cnt &&
			    job_ptr->part_ptr->tres_cnt[i])
				tres_factors[i] = value /
					(double)job_ptr->part_ptr->tres_cnt[i];
		}
	}
}


/* Set usage_efctv based on algorithm-specific code. Fair Tree sets this
 * elsewhere.
 */
static void _set_assoc_usage_efctv(slurmdb_assoc_rec_t *assoc)
{
	if (assoc->usage->fs_assoc_ptr == assoc_mgr_root_assoc)
		assoc->usage->usage_efctv = assoc->usage->usage_norm;
	else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		slurmdb_assoc_rec_t *parent_assoc =
			assoc->usage->fs_assoc_ptr;

		assoc->usage->usage_efctv =
			parent_assoc->usage->usage_efctv;
	} else if (flags & PRIORITY_FLAGS_DEPTH_OBLIVIOUS)
		_depth_oblivious_set_usage_efctv(assoc);
	else
		_set_usage_efctv(assoc);
}


static void _priority_p_set_assoc_usage_debug(slurmdb_assoc_rec_t *assoc)
{
	char *child;
	char *child_str;

	if (assoc->user) {
		child = "user";
		child_str = assoc->user;
	} else {
		child = "account";
		child_str = assoc->acct;
	}

	info("Normalized usage for %s %s off %s(%s) %Lf / %Lf = %Lf",
	     child, child_str,
	     assoc->usage->parent_assoc_ptr->acct,
	     assoc->usage->fs_assoc_ptr->acct,
	     assoc->usage->usage_raw,
	     assoc_mgr_root_assoc->usage->usage_raw,
	     assoc->usage->usage_norm);

	if (assoc->usage->fs_assoc_ptr == assoc_mgr_root_assoc) {
		info("Effective usage for %s %s off %s(%s) %Lf %Lf",
		     child, child_str,
		     assoc->usage->parent_assoc_ptr->acct,
		     assoc->usage->fs_assoc_ptr->acct,
		     assoc->usage->usage_efctv,
		     assoc->usage->usage_norm);
	} else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		slurmdb_assoc_rec_t *parent_assoc =
			assoc->usage->fs_assoc_ptr;

		info("Effective usage for %s %s off %s %Lf",
		     child, child_str,
		     parent_assoc->acct,
		     parent_assoc->usage->usage_efctv);
	} else if (flags & PRIORITY_FLAGS_DEPTH_OBLIVIOUS) {
		/* Unfortunately, this must be handled inside of
		 * _depth_oblivious_set_usage_efctv */
	} else {
		info("Effective usage for %s %s off %s(%s) "
		     "%Lf + ((%Lf - %Lf) * %d / %d) = %Lf",
		     child, child_str,
		     assoc->usage->parent_assoc_ptr->acct,
		     assoc->usage->fs_assoc_ptr->acct,
		     assoc->usage->usage_norm,
		     assoc->usage->fs_assoc_ptr->usage->usage_efctv,
		     assoc->usage->usage_norm,
		     assoc->shares_raw,
		     assoc->usage->level_shares,
		     assoc->usage->usage_efctv);
	}

}
