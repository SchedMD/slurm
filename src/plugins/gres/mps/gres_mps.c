/*****************************************************************************\
 *  gres_mps.c - Support MPS as a generic resources.
 *  MPS or CUDA Multi-Process Services is a mechanism to share GPUs.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Morris Jette
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

#define _GNU_SOURCE

#include <ctype.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "../common/gres_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Gres MPS plugin";
const char	plugin_type[]		= "gres/mps";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static char	*gres_name		= "mps";
static List	gres_devices		= NULL;
static List	mps_info		= NULL;

typedef struct mps_dev_info {
	uint64_t count;
	int id;
} mps_dev_info_t;

static void _delete_gres_list(void *x)
{
	gres_slurmd_conf_t *p = (gres_slurmd_conf_t *) x;
	xfree(p->cpus);
	FREE_NULL_BITMAP(p->cpus_bitmap);
	xfree(p->file);
	xfree(p->links);
	xfree(p->name);
	xfree(p->type_name);
	xfree(p);
}

/*
 * Convert all GPU records to a new entries in a list where each File is a
 * unique device (i.e. convert a record with "File=nvidia[0-3]" into 4 separate
 * records).
 */
static List _build_gpu_list(List gres_list)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record, *gpu_record;
	List gpu_list;
	hostlist_t hl;
	char *f_name;
	bool log_fname = true;

	if (gres_list == NULL)
		return NULL;

	gpu_list = list_create(_delete_gres_list);
	itr = list_iterator_create(gres_list);
	while ((gres_record = list_next(itr))) {
		if (xstrcmp(gres_record->name, "gpu"))
			continue;
		if (!gres_record->file) {
			if (log_fname) {
				error("%s: GPU configuration lacks \"File\" specification",
				      plugin_name);
				log_fname = false;
			}
			continue;
		}
		hl = hostlist_create(gres_record->file);
		while ((f_name = hostlist_shift(hl))) {
			gpu_record = xmalloc(sizeof(gres_slurmd_conf_t));
			gpu_record->config_flags = gres_record->config_flags;
			if (gres_record->type_name) {
				gpu_record->config_flags |=
					GRES_CONF_HAS_TYPE;
			}
			gpu_record->count = 1;
			gpu_record->cpu_cnt = gres_record->cpu_cnt;
			gpu_record->cpus = xstrdup(gres_record->cpus);
			if (gres_record->cpus_bitmap) {
				gpu_record->cpus_bitmap =
					bit_copy(gres_record->cpus_bitmap);
			}
			gpu_record->file = xstrdup(f_name);
			gpu_record->links = xstrdup(gres_record->links);
			gpu_record->name = xstrdup(gres_record->name);
			gpu_record->plugin_id = gres_record->plugin_id;
			gpu_record->type_name = xstrdup(gres_record->type_name);
			gpu_record->unique_id = xstrdup(gres_record->unique_id);
			list_append(gpu_list, gpu_record);
			free(f_name);
		}
		hostlist_destroy(hl);
		(void) list_delete_item(itr);
	}
	list_iterator_destroy(itr);

	return gpu_list;
}

/*
 * Convert all MPS records to a new entries in a list where each File is a
 * unique device (i.e. convert a record with "File=nvidia[0-3]" into 4 separate
 * records). Similar to _build_gpu_list(), but we copy more fields, divide the
 * "Count" across all MPS records and remove from the original list.
 */
static List _build_mps_list(List gres_list)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record, *mps_record;
	List mps_list;
	hostlist_t hl;
	char *f_name;
	uint64_t count_per_file;
	int mps_no_file_recs = 0, mps_file_recs = 0;

	if (gres_list == NULL)
		return NULL;

	mps_list = list_create(_delete_gres_list);
	itr = list_iterator_create(gres_list);
	while ((gres_record = list_next(itr))) {
		if (xstrcmp(gres_record->name, "mps"))
			continue;
		if (!gres_record->file) {
			if (mps_no_file_recs)
				fatal("gres/mps: bad configuration, multiple configurations without \"File\"");
			if (mps_file_recs)
				fatal("gres/mps: multiple configurations with and without \"File\"");
			mps_no_file_recs++;
			mps_record = xmalloc(sizeof(gres_slurmd_conf_t));
			mps_record->config_flags = gres_record->config_flags;
			if (gres_record->type_name)
				mps_record->config_flags |= GRES_CONF_HAS_TYPE;
			mps_record->count = gres_record->count;
			mps_record->cpu_cnt = gres_record->cpu_cnt;
			mps_record->cpus = xstrdup(gres_record->cpus);
			if (gres_record->cpus_bitmap) {
				mps_record->cpus_bitmap =
					bit_copy(gres_record->cpus_bitmap);
			}
			mps_record->name = xstrdup(gres_record->name);
			mps_record->plugin_id = gres_record->plugin_id;
			mps_record->type_name = xstrdup(gres_record->type_name);
			mps_record->unique_id = xstrdup(gres_record->unique_id);
			list_append(mps_list, mps_record);
		} else {
			mps_file_recs++;
			if (mps_no_file_recs)
				fatal("gres/mps: multiple configurations with and without \"File\"");
			hl = hostlist_create(gres_record->file);
			count_per_file = gres_record->count/hostlist_count(hl);
			while ((f_name = hostlist_shift(hl))) {
				mps_record =xmalloc(sizeof(gres_slurmd_conf_t));
				mps_record->config_flags =
					gres_record->config_flags;
				if (gres_record->type_name) {
					mps_record->config_flags |=
						GRES_CONF_HAS_TYPE;
				}
				mps_record->count = count_per_file;
				mps_record->cpu_cnt = gres_record->cpu_cnt;
				mps_record->cpus = xstrdup(gres_record->cpus);
				if (gres_record->cpus_bitmap) {
					mps_record->cpus_bitmap =
					     bit_copy(gres_record->cpus_bitmap);
				}
				mps_record->file = xstrdup(f_name);
				mps_record->name = xstrdup(gres_record->name);
				mps_record->plugin_id = gres_record->plugin_id;
				mps_record->type_name =
					xstrdup(gres_record->type_name);
				mps_record->unique_id =
					xstrdup(gres_record->unique_id);
				list_append(mps_list, mps_record);
				free(f_name);
			}
			hostlist_destroy(hl);
		}
		(void) list_delete_item(itr);
	}
	list_iterator_destroy(itr);

	return mps_list;
}

/*
 * Count of gres/mps records is zero, remove them from GRES list sent to
 * slurmctld daemon.
 */
static void _remove_mps_recs(List gres_list)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record;

	if (gres_list == NULL)
		return;

	itr = list_iterator_create(gres_list);
	while ((gres_record = list_next(itr))) {
		if (!xstrcmp(gres_record->name, "mps")) {
			(void) list_delete_item(itr);
		}
	}
	list_iterator_destroy(itr);
}

/* Distribute MPS Count to records on original list */
static void _distribute_count(List gres_conf_list, List gpu_conf_list,
			      uint64_t count)
{
	ListIterator gpu_itr;
	gres_slurmd_conf_t *gpu_record, *mps_record;
	int rem_gpus = list_count(gpu_conf_list);

	gpu_itr = list_iterator_create(gpu_conf_list);
	while ((gpu_record = list_next(gpu_itr))) {
		mps_record = xmalloc(sizeof(gres_slurmd_conf_t));
		mps_record->config_flags = gpu_record->config_flags;
		mps_record->count = count / rem_gpus;
		count -= mps_record->count;
		rem_gpus--;
		mps_record->cpu_cnt = gpu_record->cpu_cnt;
		mps_record->cpus = xstrdup(gpu_record->cpus);
		if (gpu_record->cpus_bitmap) {
			mps_record->cpus_bitmap =
				bit_copy(gpu_record->cpus_bitmap);
		}
		mps_record->file = xstrdup(gpu_record->file);
		mps_record->name = xstrdup("mps");
		mps_record->plugin_id = gres_build_id("mps");
		mps_record->type_name = xstrdup(gpu_record->type_name);
		list_append(gres_conf_list, mps_record);

		list_append(gres_conf_list, gpu_record);
		(void) list_remove(gpu_itr);
	}
	list_iterator_destroy(gpu_itr);
}

/* Merge MPS records back to original list, updating and reordering as needed */
static int _merge_lists(List gres_conf_list, List gpu_conf_list,
			List mps_conf_list)
{
	ListIterator gpu_itr, mps_itr;
	gres_slurmd_conf_t *gpu_record, *mps_record;

	if (!list_count(gpu_conf_list) && list_count(mps_conf_list)) {
		error("%s: MPS specified without any GPU found", plugin_name);
		return SLURM_ERROR;
	}

	/*
	 * If gres/mps has Count, but no File specification, then evenly
	 * distribute gres/mps Count over all gres/gpu file records
	 */
	if (list_count(mps_conf_list) == 1) {
		mps_record = list_peek(mps_conf_list);
		if (!mps_record->file) {
			_distribute_count(gres_conf_list, gpu_conf_list,
					  mps_record->count);
			list_flush(mps_conf_list);
			return SLURM_SUCCESS;
		}
	}

	/* Add MPS records, matching File ordering to that of GPU records */
	gpu_itr = list_iterator_create(gpu_conf_list);
	while ((gpu_record = list_next(gpu_itr))) {
		mps_itr = list_iterator_create(mps_conf_list);
		while ((mps_record = list_next(mps_itr))) {
			if (!xstrcmp(gpu_record->file, mps_record->file)) {
				/* Copy gres/gpu Type & CPU info to gres/mps */
				if (gpu_record->type_name) {
					mps_record->config_flags |=
						GRES_CONF_HAS_TYPE;
				}
				if (gpu_record->cpus) {
					xfree(mps_record->cpus);
					mps_record->cpus =
						xstrdup(gpu_record->cpus);
				}
				if (gpu_record->cpus_bitmap) {
					mps_record->cpu_cnt =
						gpu_record->cpu_cnt;
					FREE_NULL_BITMAP(
						mps_record->cpus_bitmap);
					mps_record->cpus_bitmap =
					      bit_copy(gpu_record->cpus_bitmap);
				}
				xfree(mps_record->type_name);
				mps_record->type_name =
					xstrdup(gpu_record->type_name);
				xfree(mps_record->unique_id);
				mps_record->unique_id =
					xstrdup(gpu_record->unique_id);
				list_append(gres_conf_list, mps_record);
				(void) list_remove(mps_itr);
				break;
			}
		}
		list_iterator_destroy(mps_itr);
		if (!mps_record) {
			/* Add gres/mps record to match gres/gps record */
			mps_record = xmalloc(sizeof(gres_slurmd_conf_t));
			mps_record->config_flags = gpu_record->config_flags;
			mps_record->count = 0;
			mps_record->cpu_cnt = gpu_record->cpu_cnt;
			mps_record->cpus = xstrdup(gpu_record->cpus);
			if (gpu_record->cpus_bitmap) {
				mps_record->cpus_bitmap =
					bit_copy(gpu_record->cpus_bitmap);
			}
			mps_record->file = xstrdup(gpu_record->file);
			mps_record->name = xstrdup("mps");
			mps_record->plugin_id = gres_build_id("mps");
			mps_record->type_name = xstrdup(gpu_record->type_name);
			mps_record->unique_id = xstrdup(gpu_record->unique_id);
			list_append(gres_conf_list, mps_record);
		}
		list_append(gres_conf_list, gpu_record);
		(void) list_remove(gpu_itr);
	}
	list_iterator_destroy(gpu_itr);

	/* Remove any remaining MPS records (no matching File) */
	mps_itr = list_iterator_create(mps_conf_list);
	while ((mps_record = list_next(mps_itr))) {
		error("%s: Discarding gres/mps configuration (File=%s) without matching gres/gpu record",
		      plugin_name, mps_record->file);
		(void) list_delete_item(mps_itr);
	}
	list_iterator_destroy(mps_itr);
	return SLURM_SUCCESS;
}

extern int init(void)
{
	debug("loaded");

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("unloading");
	FREE_NULL_LIST(gres_devices);
	FREE_NULL_LIST(mps_info);

	return SLURM_SUCCESS;
}


/*
 * Return true if fake_gpus.conf does exist. Used for testing
 */
static bool _test_gpu_list_fake(void)
{
	struct stat config_stat;
	char *fake_gpus_file = NULL;
	bool have_fake_gpus = false;

	fake_gpus_file = get_extra_conf_path("fake_gpus.conf");
	if (stat(fake_gpus_file, &config_stat) >= 0) {
		have_fake_gpus = true;
	}
	xfree(fake_gpus_file);
	return have_fake_gpus;
}

/* Translate device file name to numeric index "/dev/nvidia2" -> 2 */
static int _compute_local_id(char *dev_file_name)
{
	int i, local_id = -1, mult = 1;

	if (!dev_file_name)
		return -1;

	for (i = strlen(dev_file_name) - 1; i >= 0; i--) {
		if ((dev_file_name[i] < '0') || (dev_file_name[i] > '9'))
			break;
		if (local_id == -1)
			local_id = 0;
		local_id += (dev_file_name[i] - '0') * mult;
		mult *= 10;
	}

	return local_id;
}

static uint64_t _build_mps_dev_info(List gres_conf_list)
{
	uint64_t mps_count = 0;
	gres_slurmd_conf_t *gres_conf;
	mps_dev_info_t *mps_conf;
	ListIterator iter;

	mps_info = list_create(xfree_ptr);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_conf = list_next(iter))) {
		if (!gres_id_shared(gres_conf->plugin_id))
			continue;
		mps_conf = xmalloc(sizeof(mps_dev_info_t));
		mps_conf->count = gres_conf->count;
		mps_conf->id = _compute_local_id(gres_conf->file);
		list_append(mps_info, mps_conf);
		mps_count += gres_conf->count;
	}
	list_iterator_destroy(iter);
	return mps_count;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int gres_p_node_config_load(List gres_conf_list,
				   node_config_load_t *config)
{
	int rc = SLURM_SUCCESS;
	log_level_t log_lvl;
	List gpu_conf_list, mps_conf_list;
	bool have_fake_gpus = _test_gpu_list_fake();

	/* Assume this state is caused by an scontrol reconfigure */
	if (gres_devices) {
		debug("Resetting gres_devices");
		FREE_NULL_LIST(gres_devices);
	}
	FREE_NULL_LIST(mps_info);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_VERBOSE;
	else
		log_lvl = LOG_LEVEL_DEBUG;

	log_var(log_lvl, "%s: Initalized gres.conf list:", plugin_name);
	print_gres_list(gres_conf_list, log_lvl);

	/*
	 * Ensure that every GPU device file is listed as a MPS file.
	 * Any MPS entry that we need to add will have a "Count" of zero.
	 * Every MPS "Type" will be made to match the GPU "Type". The order
	 * of MPS records (by "File") must match the order in which GPUs are
	 * defined for the GRES bitmaps in slurmctld to line up.
	 *
	 * First, convert all GPU records to a new entries in a list where
	 * each File is a unique device (i.e. convert a record with
	 * "File=nvidia[0-3]" into 4 separate records).
	 */
	gpu_conf_list = _build_gpu_list(gres_conf_list);

	/* Now move MPS records to new List, each with unique device file */
	mps_conf_list = _build_mps_list(gres_conf_list);

	/*
	 * Merge MPS records back to original list, updating and reordering
	 * as needed.
	 */
	rc = _merge_lists(gres_conf_list, gpu_conf_list, mps_conf_list);
	FREE_NULL_LIST(gpu_conf_list);
	FREE_NULL_LIST(mps_conf_list);
	if (rc != SLURM_SUCCESS)
		fatal("%s: failed to merge MPS and GPU configuration", plugin_name);

	rc = common_node_config_load(gres_conf_list, gres_name, &gres_devices);
	if (rc != SLURM_SUCCESS)
		fatal("%s: failed to load configuration", plugin_name);
	if (_build_mps_dev_info(gres_conf_list) == 0)
		_remove_mps_recs(gres_conf_list);

	log_var(log_lvl, "%s: Final gres.conf list:", plugin_name);
	print_gres_list(gres_conf_list, log_lvl);

	// Print in parsable format for tests if fake system is in use
	if (have_fake_gpus) {
		info("Final normalized gres.conf list (parsable):");
		print_gres_list_parsable(gres_conf_list);
	}

	return rc;
}

/* Given a global device ID, return its gres/mps count */
static uint64_t _get_dev_count(int global_id)
{
	ListIterator itr;
	mps_dev_info_t *mps_ptr;
	uint64_t count = NO_VAL64;

	if (!mps_info) {
		error("mps_info is NULL");
		return 100;
	}
	itr = list_iterator_create(mps_info);
	while ((mps_ptr = (mps_dev_info_t *) list_next(itr))) {
		if (mps_ptr->id == global_id) {
			count = mps_ptr->count;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (count == NO_VAL64) {
		error("Could not find gres/mps count for device ID %d",
		      global_id);
		return 100;
	}

	return count;
}

static void _set_env(char ***env_ptr, bitstr_t *gres_bit_alloc,
		     bitstr_t *usable_gres, uint64_t gres_per_node,
		     bool *already_seen, int *local_inx,
		     bool is_task, bool is_job, gres_internal_flags_t flags)
{
	char *global_list = NULL, *local_list = NULL, *perc_env = NULL;
	char perc_str[64], *slurm_env_var = NULL;
	uint64_t count_on_dev, percentage;
	int global_id = -1;

	if (is_job)
		slurm_env_var = "SLURM_JOB_GPUS";
	else
		slurm_env_var = "SLURM_STEP_GPUS";

	if (*already_seen) {
		global_list = xstrdup(getenvp(*env_ptr, slurm_env_var));
		local_list = xstrdup(getenvp(*env_ptr,
					     "CUDA_VISIBLE_DEVICES"));
		perc_env = xstrdup(getenvp(*env_ptr,
					  "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE"));
	}

	common_gres_set_env(gres_devices, env_ptr,
			    usable_gres, "", local_inx, gres_bit_alloc,
			    &local_list, &global_list,
			    is_task, is_job, &global_id, flags, true);

	if (perc_env) {
		env_array_overwrite(env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_perc_str",
				    perc_env);
		xfree(perc_env);
	} else if (gres_per_node && mps_info) {
		count_on_dev = _get_dev_count(global_id);
		if (count_on_dev > 0) {
			percentage = (gres_per_node * 100) / count_on_dev;
			percentage = MAX(percentage, 1);
		} else
			percentage = 0;
		snprintf(perc_str, sizeof(perc_str), "%"PRIu64, percentage);
		env_array_overwrite(env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
				    perc_str);
	} else if (gres_per_node) {
		error("mps_info list is NULL");
		snprintf(perc_str, sizeof(perc_str), "%"PRIu64, gres_per_node);
		env_array_overwrite(env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
				    perc_str);
	}

	if (global_list) {
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	}

	if (local_list) {
		/*
		 * CUDA_VISIBLE_DEVICES is relative to the MPS server.
		 * With only one GPU under the control of MPS, the device
		 * number will always be "0".
		 */
		env_array_overwrite(env_ptr, "CUDA_VISIBLE_DEVICES", "0");
		env_array_overwrite(env_ptr, "GPU_DEVICE_ORDINAL", "0");
		xfree(local_list);
		*already_seen = true;
	}
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void gres_p_job_set_env(char ***job_env_ptr,
			       bitstr_t *gres_bit_alloc,
			       uint64_t gres_per_node,
			       gres_internal_flags_t flags)
{
	/*
	 * Variables are not static like in step_*_env since we could be calling
	 * this from the slurmd where we are dealing with a different job each
	 * time we hit this function, so we don't want to keep track of other
	 * unrelated job's status.  This can also get called multiple times
	 * (different prologs and such) which would also result in bad info each
	 * call after the first.
	 */
	int local_inx = 0;
	bool already_seen = false;

	_set_env(job_env_ptr, gres_bit_alloc, NULL, gres_per_node,
		 &already_seen, &local_inx, false, true, flags);
}

/*
 * Set environment variables as appropriate for a step (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void gres_p_step_set_env(char ***step_env_ptr,
				bitstr_t *gres_bit_alloc,
				uint64_t gres_per_node,
				gres_internal_flags_t flags)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_bit_alloc, NULL, gres_per_node,
		 &already_seen, &local_inx, false, false, flags);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one task)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void gres_p_task_set_env(char ***step_env_ptr,
				bitstr_t *gres_bit_alloc,
				bitstr_t *usable_gres,
				uint64_t gres_per_node,
				gres_internal_flags_t flags)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_bit_alloc, usable_gres, gres_per_node,
		 &already_seen, &local_inx, true, false, flags);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_p_send_stepd(buf_t *buffer)
{
	int mps_cnt;
	mps_dev_info_t *mps_ptr;
	ListIterator itr;

	common_send_stepd(buffer, gres_devices);

	if (!mps_info) {
		mps_cnt = 0;
		pack32(mps_cnt, buffer);
	} else {
		mps_cnt = list_count(mps_info);
		pack32(mps_cnt, buffer);
		itr = list_iterator_create(mps_info);
		while ((mps_ptr = (mps_dev_info_t *) list_next(itr))) {
			pack64(mps_ptr->count, buffer);
			pack64(mps_ptr->id, buffer);
		}
		list_iterator_destroy(itr);
	}
	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_p_recv_stepd(buf_t *buffer)
{
	int i, mps_cnt;
	mps_dev_info_t *mps_ptr = NULL;
	uint64_t uint64_tmp;
	uint32_t cnt;

	common_recv_stepd(buffer, &gres_devices);

	safe_unpack32(&cnt, buffer);
	mps_cnt = cnt;
	if (!mps_cnt)
		return;

	mps_info = list_create(xfree_ptr);
	for (i = 0; i < mps_cnt; i++) {
		mps_ptr = xmalloc(sizeof(mps_dev_info_t));
		safe_unpack64(&uint64_tmp, buffer);
		mps_ptr->count = uint64_tmp;
		safe_unpack64(&uint64_tmp, buffer);
		mps_ptr->id = uint64_tmp;
		list_append(mps_info, mps_ptr);
	}
	return;

unpack_error:
	error("failed");
	xfree(mps_ptr);
	return;
}

/*
 * get data from a job's GRES data structure
 * IN job_gres_data  - job's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired
 * IN data_type - type of data to get from the job's data
 * OUT data - pointer to the data from job's GRES data structure
 *            DO NOT FREE: This is a pointer into the job's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_p_get_job_info(gres_job_state_t *job_gres_data,
			       uint32_t node_inx,
			       enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * get data from a step's GRES data structure
 * IN step_gres_data  - step's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired. Note this can differ from the step's
 *	node allocation index.
 * IN data_type - type of data to get from the step's data
 * OUT data - pointer to the data from step's GRES data structure
 *            DO NOT FREE: This is a pointer into the step's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_p_get_step_info(gres_step_state_t *step_gres_data,
				uint32_t node_inx,
				enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern List gres_p_get_devices(void)
{
	return gres_devices;
}

extern void gres_p_step_hardware_init(bitstr_t *usable_gres, char *settings)
{
	return;
}

extern void gres_p_step_hardware_fini(void)
{
	return;
}

/*
 * Build record used to set environment variables as appropriate for a job's
 * prolog or epilog based GRES allocated to the job.
 */
extern gres_epilog_info_t *gres_p_epilog_build_env(
	gres_job_state_t *gres_job_ptr)
{
	int i;
	gres_epilog_info_t *epilog_info;

	epilog_info = xmalloc(sizeof(gres_epilog_info_t));
	epilog_info->node_cnt = gres_job_ptr->node_cnt;
	epilog_info->gres_bit_alloc = xcalloc(epilog_info->node_cnt,
					      sizeof(bitstr_t *));
	epilog_info->gres_cnt_node_alloc = xcalloc(epilog_info->node_cnt,
					      sizeof(uint64_t));
	for (i = 0; i < epilog_info->node_cnt; i++) {
		if (gres_job_ptr->gres_bit_alloc &&
		    gres_job_ptr->gres_bit_alloc[i]) {
			epilog_info->gres_bit_alloc[i] =
				bit_copy(gres_job_ptr->gres_bit_alloc[i]);
		}
		if (gres_job_ptr->gres_bit_alloc &&
		    gres_job_ptr->gres_bit_alloc[i]) {
			epilog_info->gres_cnt_node_alloc[i] =
				gres_job_ptr->gres_cnt_node_alloc[i];
		}
	}

	return epilog_info;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 */
extern void gres_p_epilog_set_env(char ***epilog_env_ptr,
				  gres_epilog_info_t *epilog_info, int node_inx)
{
	int dev_inx = -1, env_inx = 0, global_id = -1, i;
	uint64_t count_on_dev, gres_per_node = 0, percentage;
	gres_device_t *gres_device;
	ListIterator iter;

	xassert(epilog_env_ptr);

	if (!epilog_info)
		return;

	if (!gres_devices)
		return;

	if (epilog_info->node_cnt == 0)	/* no_consume */
		return;

	if (node_inx > epilog_info->node_cnt) {
		error("bad node index (%d > %u)",
		      node_inx, epilog_info->node_cnt);
		return;
	}

	if (*epilog_env_ptr) {
		for (env_inx = 0; (*epilog_env_ptr)[env_inx]; env_inx++)
			;
		xrealloc(*epilog_env_ptr, sizeof(char *) * (env_inx + 3));
	} else {
		*epilog_env_ptr = xcalloc(3, sizeof(char *));
	}

	if (epilog_info->gres_bit_alloc &&
	    epilog_info->gres_bit_alloc[node_inx])
		dev_inx = bit_ffs(epilog_info->gres_bit_alloc[node_inx]);
	if (dev_inx >= 0) {
		/* Translate bit to device number, may differ */
		i = -1;
		iter = list_iterator_create(gres_devices);
		while ((gres_device = list_next(iter))) {
			i++;
			if (i == dev_inx) {
				global_id = gres_device->dev_num;
				break;
			}
		}
		list_iterator_destroy(iter);
	}
	if (global_id >= 0) {
		xstrfmtcat((*epilog_env_ptr)[env_inx++],
			   "CUDA_VISIBLE_DEVICES=%d", global_id);
	}
	if ((global_id >= 0) &&
	    epilog_info->gres_cnt_node_alloc &&
	    epilog_info->gres_cnt_node_alloc[node_inx]) {
		gres_per_node = epilog_info->gres_cnt_node_alloc[node_inx];
		count_on_dev = _get_dev_count(global_id);
		if (count_on_dev > 0) {
			percentage = (gres_per_node * 100) / count_on_dev;
			percentage = MAX(percentage, 1);
		} else
			percentage = 0;
		xstrfmtcat((*epilog_env_ptr)[env_inx++],
			   "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE=%"PRIu64,
			   percentage);
	}

	return;
}
