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
const char	*plugin_name		= "Gres MPS plugin";
const char	*plugin_type		= "gres/mps";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static uint64_t	debug_flags		= 0;
static char	*gres_name		= "mps";
static List	gres_devices		= NULL;

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
 * A one-liner version of _print_gres_conf_full()
 */
static void _print_gres_conf(gres_slurmd_conf_t *gres_slurmd_conf,
			     log_level_t log_lvl)
{
	log_var(log_lvl,
		"    GRES[%s](%"PRIu64"): %8s | %s",
		gres_slurmd_conf->name, gres_slurmd_conf->count,
		gres_slurmd_conf->type_name, gres_slurmd_conf->file);
}


/*
 * Print the gres.conf record in a parsable format
 * Do NOT change the format of this without also changing test39.17!
 */
static void _print_gres_conf_parsable(gres_slurmd_conf_t *gres_slurmd_conf,
				      log_level_t log_lvl)
{
	log_var(log_lvl, "GRES_PARSABLE[%s](%"PRIu64"):%s|%s",
		gres_slurmd_conf->name, gres_slurmd_conf->count,
		gres_slurmd_conf->type_name, gres_slurmd_conf->file);
}

/*
 * Prints out each gres_slurmd_conf_t record in the list
 */
static void _print_gres_list_helper(List gres_list, log_level_t log_lvl,
				    bool parsable)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record;

	if (gres_list == NULL)
		return;
	itr = list_iterator_create(gres_list);
	while ((gres_record = list_next(itr))) {
		if (parsable)
			_print_gres_conf_parsable(gres_record, log_lvl);
		else
			_print_gres_conf(gres_record, log_lvl);
	}
	list_iterator_destroy(itr);
}

/*
 * Print each gres_slurmd_conf_t record in the list
 */
static void _print_gres_list(List gres_list, log_level_t log_lvl)
{
	_print_gres_list_helper(gres_list, log_lvl, false);
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
			gpu_record->cpu_cnt = gres_record->cpu_cnt;
			gpu_record->cpus = xstrdup(gres_record->cpus);
			if (gres_record->cpus_bitmap) {
				gpu_record->cpus_bitmap =
					bit_copy(gres_record->cpus_bitmap);
			}
			gpu_record->file = xstrdup(f_name);
			gpu_record->name = xstrdup(gres_record->name);
			gpu_record->type_name = xstrdup(gres_record->type_name);
			list_append(gpu_list, gpu_record);
			free(f_name);
		}
		hostlist_destroy(hl);
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
				list_append(mps_list, mps_record);
				free(f_name);
			}
			hostlist_destroy(hl);
		}
		(void) list_remove(itr);
	}
	list_iterator_destroy(itr);

	return mps_list;
}

/* Distributed MPS Count to records on original list */
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
		mps_record->plugin_id = gres_plugin_build_id("mps");
		mps_record->type_name = xstrdup(gpu_record->type_name);
		list_append(gres_conf_list, mps_record);
	}
	list_iterator_destroy(gpu_itr);
}

/* Merge MPS records back to original list, updating and reordering as needed */
static void _merge_lists(List gres_conf_list, List gpu_conf_list,
			 List mps_conf_list)
{
	ListIterator gpu_itr, mps_itr;
	gres_slurmd_conf_t *gpu_record, *mps_record;

	/*
	 * If gres/mps has Count, but no File specification and there are more
	 * than one gres/gpu record, then evenly distribute gres/mps Count
	 * evenly over all gres/gpu file records
	 */
	if ((list_count(mps_conf_list) == 1) &&
	    (list_count(gpu_conf_list) >  1)) {
		mps_record = list_peek(mps_conf_list);
		if (!mps_record->file) {
			_distribute_count(gres_conf_list, gpu_conf_list,
					  mps_record->count);
			list_flush(mps_conf_list);
			return;
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
					mps_record->cpus_bitmap =
					      bit_copy(gpu_record->cpus_bitmap);
				}
				xfree(mps_record->type_name);
				mps_record->type_name =
					xstrdup(gpu_record->type_name);
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
			mps_record->plugin_id = gres_plugin_build_id("mps");
			mps_record->type_name = xstrdup(gpu_record->type_name);
			list_append(gres_conf_list, mps_record);
		}
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
}

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
	FREE_NULL_LIST(gres_devices);

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int node_config_load(List gres_conf_list, node_config_load_t *config)
{
	int rc = SLURM_SUCCESS;
	log_level_t log_lvl;
	List gpu_conf_list, mps_conf_list;

	/* Assume this state is caused by an scontrol reconfigure */
	debug_flags = slurm_get_debug_flags();
	if (gres_devices) {
		debug("Resetting gres_devices");
		FREE_NULL_LIST(gres_devices);
	}

	if (debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_DEBUG;

	log_var(log_lvl, "%s: Initalized gres.conf list:", plugin_name);
	_print_gres_list(gres_conf_list, log_lvl);

	rc = common_node_config_load(gres_conf_list, gres_name, &gres_devices);
	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);

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
	_merge_lists(gres_conf_list, gpu_conf_list, mps_conf_list);
	FREE_NULL_LIST(gpu_conf_list);
	FREE_NULL_LIST(mps_conf_list);

	log_var(log_lvl, "%s: Final gres.conf list:", plugin_name);
	_print_gres_list(gres_conf_list, log_lvl);

	return rc;
}

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job)
{
	char *global_list = NULL, *local_list = NULL, *percentage = NULL;
	char *slurm_env_var = NULL;

	if (is_job)
		slurm_env_var = "SLURM_JOB_GRES";
	else
		slurm_env_var = "SLURM_STEP_GRES";

	if (*already_seen) {
		global_list = xstrdup(getenvp(*env_ptr, slurm_env_var));
		local_list = xstrdup(getenvp(*env_ptr,
					     "CUDA_VISIBLE_DEVICES"));
	}

	common_gres_set_env(gres_devices, env_ptr, gres_ptr, node_inx,
			    usable_gres, "", local_inx,
			    &percentage, &local_list, &global_list,
			    reset, is_job);

	if (percentage) {
		env_array_overwrite(env_ptr,
				    "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE",
				    percentage);
		xfree(percentage);
	}

	if (global_list) {
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	}

	if (local_list) {
		env_array_overwrite(env_ptr, "CUDA_VISIBLE_DEVICES",
				    local_list);
		env_array_overwrite(env_ptr, "GPU_DEVICE_ORDINAL", local_list);
		xfree(local_list);
		*already_seen = true;
	}
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void job_set_env(char ***job_env_ptr, void *gres_ptr, int node_inx)
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

	_set_env(job_env_ptr, gres_ptr, node_inx, NULL,
		 &already_seen, &local_inx, false, true);
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void step_set_env(char ***step_env_ptr, void *gres_ptr)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, NULL,
		 &already_seen, &local_inx, false, false);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one task)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void step_reset_env(char ***step_env_ptr, void *gres_ptr,
			   bitstr_t *usable_gres)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, usable_gres,
		 &already_seen, &local_inx, true, false);
}

/* Send GRES information to slurmstepd on the specified file descriptor*/
extern void send_stepd(int fd)
{
	common_send_stepd(fd, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(int fd)
{
	common_recv_stepd(fd, &gres_devices);
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
extern int job_info(gres_job_state_t *job_gres_data, uint32_t node_inx,
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
extern int step_info(gres_step_state_t *step_gres_data, uint32_t node_inx,
		     enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern List get_devices(void)
{
	return gres_devices;
}

extern void step_configure_hardware(bitstr_t *usable_gres, char *settings)
{

}

extern void step_unconfigure_hardware(void)
{

}
