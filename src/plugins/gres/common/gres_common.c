/*****************************************************************************\
 *  gres_common.c - common functions for gres plugins
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#include <ctype.h>

#include "gres_common.h"

#include "src/common/xstring.h"

static int _match_dev_inx(void *x, void *key)
{
	gres_device_t *gres_device = x;
	int dev_inx = *(int *)key;

	if (gres_device->index == dev_inx)
		return 1;

	return 0;
}

extern void common_gres_set_env(common_gres_env_t *gres_env)
{
	bool use_local_dev_index = gres_use_local_device_index();
	bool set_global_id = false;
	gres_device_t *gres_device;
	ListIterator itr;
	char *global_prefix = "", *local_prefix = "";
	char *new_global_list = NULL, *new_local_list = NULL;
	int device_index = -1;
	bool device_considered = false;
	int local_inx = 0;

	xassert(gres_env);

	if (!gres_env->gres_devices)
		return;

	/* If we are setting task env but don't have usable_gres, just exit */
	if (gres_env->is_task && !gres_env->usable_gres)
		return;

	/* is_task and is_job can't both be true */
	xassert(!(gres_env->is_task && gres_env->is_job));

	if (!gres_env->bit_alloc) {
		/*
		 * The gres.conf file must identify specific device files
		 * in order to set the CUDA_VISIBLE_DEVICES env var
		 */
		return;
	}

	itr = list_iterator_create(gres_env->gres_devices);
	while ((gres_device = list_next(itr))) {
		int index;
		int global_env_index;
		if (!bit_test(gres_env->bit_alloc, gres_device->index))
			continue;

		/* Track physical devices if MultipleFiles is used */
		if (device_index < gres_device->index) {
			device_index = gres_device->index;
			device_considered = false;
		} else if (device_index != gres_device->index)
			error("gres_device->index was not monotonically increasing! Are gres_devices not sorted by index? device_index: %d, gres_device->index: %d",
			      device_index, gres_device->index);

		/* Continue if we already bound this physical device */
		if (device_considered)
			continue;

		/*
		 * NICs want env to match the dev_num parsed from the
		 * file name; GPUs, however, want it to match the order
		 * they enumerate on the PCI bus, and this isn't always
		 * the same order as the device file names
		 */
		if (gres_env->use_dev_num)
			global_env_index = gres_device->dev_num;
		else
			global_env_index = gres_device->index;

		index = use_local_dev_index ?
			local_inx++ : global_env_index;

		if (gres_env->is_task) {
			if (!bit_test(gres_env->usable_gres,
				      use_local_dev_index ?
				      index : gres_device->index)) {
				/*
				 * Since this device is not in usable_gres, skip
				 * over any other device files associated with
				 * it by setting device_considered = true
				 */
				device_considered = true;
				continue;
			}
		}

		if (!set_global_id) {
			gres_env->global_id = gres_device->dev_num;
			set_global_id = true;
		}

		/*
		 * If unique_id is set for the device, assume that we
		 * want to use it for the env var
		 */
		if (gres_device->unique_id)
			xstrfmtcat(new_local_list, "%s%s%s", local_prefix,
				   gres_env->prefix, gres_device->unique_id);
		else
			xstrfmtcat(new_local_list, "%s%s%d", local_prefix,
				   gres_env->prefix, index);
		xstrfmtcat(new_global_list, "%s%s%d", global_prefix,
			   gres_env->prefix, global_env_index);

		local_prefix = ",";
		global_prefix = ",";
		device_considered = true;
	}
	list_iterator_destroy(itr);

	if (new_global_list) {
		xfree(gres_env->global_list);
		gres_env->global_list = new_global_list;
	}
	if (new_local_list) {
		xfree(gres_env->local_list);
		gres_env->local_list = new_local_list;
	}

	if (gres_env->flags & GRES_INTERNAL_FLAG_VERBOSE) {
		char *usable_str;
		char *alloc_str;
		if (gres_env->usable_gres)
			usable_str = bit_fmt_hexmask_trim(
				gres_env->usable_gres);
		else
			usable_str = xstrdup("NULL");
		alloc_str = bit_fmt_hexmask_trim(gres_env->bit_alloc);
		fprintf(stderr, "gpu-bind: usable_gres=%s; bit_alloc=%s; local_inx=%d; global_list=%s; local_list=%s\n",
			usable_str, alloc_str, local_inx, gres_env->global_list,
			gres_env->local_list);
		xfree(alloc_str);
		xfree(usable_str);
	}
}

/*
 * A one-liner version of _print_gres_conf_full()
 */
extern void print_gres_conf(gres_slurmd_conf_t *gres_slurmd_conf,
			    log_level_t log_lvl)
{
	log_var(log_lvl, "    GRES[%s] Type:%s Count:%"PRIu64" Cores(%d):%s  "
		"Links:%s Flags:%s File:%s UniqueId:%s", gres_slurmd_conf->name,
		gres_slurmd_conf->type_name, gres_slurmd_conf->count,
		gres_slurmd_conf->cpu_cnt, gres_slurmd_conf->cpus,
		gres_slurmd_conf->links,
		gres_flags2str(gres_slurmd_conf->config_flags),
		gres_slurmd_conf->file, gres_slurmd_conf->unique_id);
}


/*
 * Print the gres.conf record in a parsable format
 * Do NOT change the format of this without also changing test39.18!
 */
static void _print_gres_conf_parsable(gres_slurmd_conf_t *gres_slurmd_conf,
				      log_level_t log_lvl)
{
	/* Only print out unique_id if set */
	log_var(log_lvl, "GRES_PARSABLE[%s](%"PRIu64"):%s|%d|%s|%s|%s|%s%s%s",
		gres_slurmd_conf->name, gres_slurmd_conf->count,
		gres_slurmd_conf->type_name, gres_slurmd_conf->cpu_cnt,
		gres_slurmd_conf->cpus, gres_slurmd_conf->links,
		gres_slurmd_conf->file,
		gres_slurmd_conf->unique_id ? gres_slurmd_conf->unique_id : "",
		gres_slurmd_conf->unique_id ? "|" : "",
		gres_flags2str(gres_slurmd_conf->config_flags));
}

/*
 * Prints out each gres_slurmd_conf_t record in the list
 */
static void _print_gres_list_helper(List gres_list, log_level_t log_lvl,
				    bool parsable)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf;

	if (gres_list == NULL)
		return;
	itr = list_iterator_create(gres_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		if (parsable)
			_print_gres_conf_parsable(gres_slurmd_conf, log_lvl);
		else
			print_gres_conf(gres_slurmd_conf, log_lvl);
	}
	list_iterator_destroy(itr);
}

/*
 * Print each gres_slurmd_conf_t record in the list
 */
extern void print_gres_list(List gres_list, log_level_t log_lvl)
{
	_print_gres_list_helper(gres_list, log_lvl, false);
}

/*
 * Print each gres_slurmd_conf_t record in the list in a parsable manner for
 * test consumption
 */
extern void print_gres_list_parsable(List gres_list)
{
	_print_gres_list_helper(gres_list, LOG_LEVEL_INFO, true);
}

extern void gres_common_gpu_set_env(common_gres_env_t *gres_env)
{
	char *slurm_env_var;
	uint64_t gres_cnt;

	if (gres_env->is_job)
		slurm_env_var = "SLURM_JOB_GPUS";
	else
		slurm_env_var = "SLURM_STEP_GPUS";

	gres_env->prefix = "";

	common_gres_set_env(gres_env);

	/*
	 * Set environment variables if GRES is found. Otherwise, unset
	 * environment variables, since this means GRES is not allocated.
	 * This is useful for jobs and steps that request --gres=none within an
	 * existing job allocation with GRES.
	 * Do not unset envs that could have already been set by an allocated
	 * sharing GRES (GPU).
	 *
	 * NOTE: Use gres_env->bit_alloc to ensure SLURM_GPUS_ON_NODE is
	 * correct with shared gres. Do not use gres_env->gres_cnt.
	 */
	gres_cnt = gres_env->bit_alloc ? bit_set_count(gres_env->bit_alloc) : 0;
	if (gres_cnt) {
		char *gpus_on_node = xstrdup_printf("%"PRIu64,
						    gres_cnt);
		env_array_overwrite(gres_env->env_ptr, "SLURM_GPUS_ON_NODE",
				    gpus_on_node);
		xfree(gpus_on_node);
	} else if (!(gres_env->flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		unsetenvp(*gres_env->env_ptr, "SLURM_GPUS_ON_NODE");
	}

	if (gres_env->global_list) {
		env_array_overwrite(gres_env->env_ptr, slurm_env_var,
				    gres_env->global_list);
		xfree(gres_env->global_list);
	} else if (!(gres_env->flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		unsetenvp(*gres_env->env_ptr, slurm_env_var);
	}

	if (gres_env->local_list) {
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_NVML)
			env_array_overwrite(gres_env->env_ptr,
					    "CUDA_VISIBLE_DEVICES",
					    gres_env->local_list);
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_RSMI)
			env_array_overwrite(gres_env->env_ptr,
					    "ROCR_VISIBLE_DEVICES",
					    gres_env->local_list);
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			env_array_overwrite(gres_env->env_ptr,
					    "ZE_AFFINITY_MASK",
					    gres_env->local_list);
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_OPENCL)
			env_array_overwrite(gres_env->env_ptr,
					    "GPU_DEVICE_ORDINAL",
					    gres_env->local_list);
		xfree(gres_env->local_list);
	} else if (!(gres_env->flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_NVML)
			unsetenvp(*gres_env->env_ptr, "CUDA_VISIBLE_DEVICES");
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_RSMI)
			unsetenvp(*gres_env->env_ptr, "ROCR_VISIBLE_DEVICES");
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			unsetenvp(*gres_env->env_ptr, "ZE_AFFINITY_MASK");
		if (gres_env->gres_conf_flags & GRES_CONF_ENV_OPENCL)
			unsetenvp(*gres_env->env_ptr, "GPU_DEVICE_ORDINAL");
	}
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * RETURN: 1 if nothing was done, 0 otherwise.
 */
extern bool gres_common_prep_set_env(char ***prep_env_ptr,
				     gres_prep_t *gres_prep,
				     int node_inx, uint32_t gres_conf_flags,
				     List gres_devices)
{
	int dev_inx_first = -1, dev_inx_last, dev_inx;
	gres_device_t *gres_device;
	char *vendor_gpu_str = NULL;
	char *slurm_gpu_str = NULL;
	char *sep = "";

	xassert(prep_env_ptr);

	if (!gres_prep)
		return 1;

	if (!gres_devices)
		return 1;

	if (gres_prep->node_cnt == 0)	/* no_consume */
		return 1;

	if (node_inx > gres_prep->node_cnt) {
		error("bad node index (%d > %u)",
		      node_inx, gres_prep->node_cnt);
		return 1;
	}

	if (gres_prep->gres_bit_alloc &&
	    gres_prep->gres_bit_alloc[node_inx]) {
		dev_inx_first = bit_ffs(gres_prep->gres_bit_alloc[node_inx]);
	}
	if (dev_inx_first >= 0)
		dev_inx_last = bit_fls(gres_prep->gres_bit_alloc[node_inx]);
	else
		dev_inx_last = -2;
	for (dev_inx = dev_inx_first; dev_inx <= dev_inx_last; dev_inx++) {
		if (!bit_test(gres_prep->gres_bit_alloc[node_inx],
			      dev_inx))
			continue;
		if ((gres_device =
		     list_find_first(gres_devices, _match_dev_inx, &dev_inx))) {
			if (gres_device->unique_id)
				xstrfmtcat(vendor_gpu_str, "%s%s", sep,
					   gres_device->unique_id);
			else
				xstrfmtcat(vendor_gpu_str, "%s%d", sep,
					   gres_device->index);
			xstrfmtcat(slurm_gpu_str, "%s%d", sep,
				   gres_device->index);
			sep = ",";
		}
	}
	if (vendor_gpu_str) {
		if (gres_conf_flags & GRES_CONF_ENV_NVML)
			env_array_overwrite(prep_env_ptr,
					    "CUDA_VISIBLE_DEVICES",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_RSMI)
			env_array_overwrite(prep_env_ptr,
					    "ROCR_VISIBLE_DEVICES",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			env_array_overwrite(prep_env_ptr,
					    "ZE_AFFINITY_MASK",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_OPENCL)
			env_array_overwrite(prep_env_ptr,
					    "GPU_DEVICE_ORDINAL",
					    vendor_gpu_str);
		xfree(vendor_gpu_str);
	}
	if (slurm_gpu_str) {
		env_array_overwrite(prep_env_ptr, "SLURM_JOB_GPUS",
				    slurm_gpu_str);
		xfree(slurm_gpu_str);
	}

	return 0;
}

extern int gres_common_set_env_types_on_node_flags(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = (gres_slurmd_conf_t *)x;
	uint32_t *node_flags = arg;

	if (gres_slurmd_conf->config_flags & GRES_CONF_ENV_NVML)
		*node_flags |= GRES_CONF_ENV_NVML;
	if (gres_slurmd_conf->config_flags & GRES_CONF_ENV_RSMI)
		*node_flags |= GRES_CONF_ENV_RSMI;
	if (gres_slurmd_conf->config_flags & GRES_CONF_ENV_OPENCL)
		*node_flags |= GRES_CONF_ENV_OPENCL;
	if (gres_slurmd_conf->config_flags & GRES_CONF_ENV_ONEAPI)
		*node_flags |= GRES_CONF_ENV_ONEAPI;

	/* No need to continue if all are set */
	if ((*node_flags & GRES_CONF_ENV_SET) == GRES_CONF_ENV_SET)
		return -1;

	return 0;
}
