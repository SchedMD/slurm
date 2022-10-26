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
#include <sys/stat.h>
#include <sys/types.h>

#ifdef MAJOR_IN_MKDEV
#  include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
#endif

#include "gres_common.h"

#include "src/common/xstring.h"

static void _free_name_list(void *x)
{
	free(x);
}

static int _match_dev_inx(void *x, void *key)
{
	gres_device_t *gres_device = x;
	int dev_inx = *(int *)key;

	if (gres_device->index == dev_inx)
		return 1;

	return 0;
}

static int _match_name_list(void *x, void *key)
{
	if (!xstrcmp(x, key))
		return 1;	/* duplicate file name */
	return 0;
}

static int _set_gres_device_desc(gres_device_t *dev)
{
	struct stat fs;

	dev->dev_desc.type = DEV_TYPE_NONE;
	dev->dev_desc.major = NO_VAL;
	dev->dev_desc.minor = NO_VAL;

	if (stat(dev->path, &fs) < 0) {
		error("%s: stat(%s): %m", __func__, dev->path);
		return SLURM_ERROR;
	}

	dev->dev_desc.major = major(fs.st_rdev);
	dev->dev_desc.minor = minor(fs.st_rdev);
	log_flag(GRES, "%s : %s major %d, minor %d", __func__, dev->path,
		 dev->dev_desc.major, dev->dev_desc.minor);

	if (S_ISBLK(fs.st_mode))
		dev->dev_desc.type = DEV_TYPE_BLOCK;
	else if (S_ISCHR(fs.st_mode))
		dev->dev_desc.type = DEV_TYPE_CHAR;
	else {
		error("%s is not a valid character or block device, fix your gres.conf",
		      dev->path);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static gres_device_t *_init_gres_device(int index, char *one_name,
					char *unique_id)
{
	int tmp, digit = -1;
	gres_device_t *gres_device = xmalloc(sizeof(gres_device_t));

	gres_device->dev_num = -1;
	gres_device->index = index;
	gres_device->path = xstrdup(one_name);
	gres_device->unique_id = xstrdup(unique_id);

	if (_set_gres_device_desc(gres_device) != SLURM_SUCCESS) {
		xfree(gres_device);
		return NULL;
	}

	tmp = strlen(one_name);
	for (int i = 1;  i <= tmp; i++) {
		if (isdigit(one_name[tmp - i])) {
			digit = tmp - i;
			continue;
		}
		break;
	}
	if (digit >= 0)
		gres_device->dev_num = atoi(one_name + digit);
	else
		gres_device->dev_num = -1;

	return gres_device;
}

/*
 * Common validation for what was read in from the gres.conf.
 * IN gres_conf_list
 * IN gres_name
 * IN config
 * OUT gres_devices
 */
extern int common_node_config_load(List gres_conf_list, char *gres_name,
				   node_config_load_t *config,
				   List *gres_devices)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf;
	List names_list;
	int max_dev_num = -1;
	int index = 0;

	xassert(gres_conf_list);
	xassert(gres_devices);

	names_list = list_create(_free_name_list);
	itr = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		hostlist_t hl;
		char *one_name;

		if (!(gres_slurmd_conf->config_flags & GRES_CONF_HAS_FILE) ||
		    !gres_slurmd_conf->file ||
		    xstrcmp(gres_slurmd_conf->name, gres_name))
			continue;

		if (!(hl = hostlist_create(gres_slurmd_conf->file))) {
			error("can't parse gres.conf file record (%s)",
			      gres_slurmd_conf->file);
			continue;
		}

		while ((one_name = hostlist_shift(hl))) {
			/* We don't care about gres_devices in slurmctld */
			if (config->in_slurmd) {
				gres_device_t *gres_device;
				if (!*gres_devices) {
					*gres_devices = list_create(
						destroy_gres_device);
				}

				if (!(gres_device = _init_gres_device(
					      index, one_name,
					      gres_slurmd_conf->unique_id))) {
					free(one_name);
					continue;
				}

				if (gres_device->dev_num > max_dev_num)
					max_dev_num = gres_device->dev_num;

				list_append(*gres_devices, gres_device);
			}

			/*
			 * Don't check for file duplicates or increment the
			 * device bitmap index if this is a MultipleFiles GRES
			 */
			if (gres_slurmd_conf->config_flags &
			    GRES_CONF_HAS_MULT) {
				free(one_name);
				continue;
			}

			if ((rc == SLURM_SUCCESS) &&
			    list_find_first(names_list, _match_name_list,
					    one_name)) {
				error("%s duplicate device file name (%s)",
				      gres_name, one_name);
				rc = SLURM_ERROR;
			}

			(void) list_append(names_list, one_name);

			/* Increment device bitmap index */
			index++;
		}
		hostlist_destroy(hl);
		if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_MULT)
			index++;
	}
	list_iterator_destroy(itr);
	list_destroy(names_list);

	if (*gres_devices) {
		gres_device_t *gres_device;
		char *dev_id_str;
		itr = list_iterator_create(*gres_devices);
		while ((gres_device = list_next(itr))) {
			dev_id_str = gres_device_id2str(&gres_device->dev_desc);
			if (gres_device->dev_num == -1)
				gres_device->dev_num = ++max_dev_num;
			log_flag(GRES, "%s device number %d(%s):%s",
				 gres_name, gres_device->dev_num,
				 gres_device->path,
				 dev_id_str);
			xfree(dev_id_str);
		}
		list_iterator_destroy(itr);
	}

	return rc;
}

extern void common_gres_set_env(List gres_devices, char ***env_ptr,
				bitstr_t *usable_gres, char *prefix,
				int *local_inx, bitstr_t *bit_alloc,
				char **local_list, char **global_list,
				bool is_task, bool is_job, int *global_id,
				gres_internal_flags_t flags, bool use_dev_num)
{
	bool use_local_dev_index = gres_use_local_device_index();
	bool set_global_id = false;
	gres_device_t *gres_device, *first_device = NULL;
	ListIterator itr;
	char *global_prefix = "", *local_prefix = "";
	char *new_global_list = NULL, *new_local_list = NULL;
	int device_index = -1;
	bool device_considered = false;

	if (!gres_devices)
		return;

	/* If we are setting task env but don't have usable_gres, just exit */
	if (is_task && !usable_gres)
		return;

	xassert(global_list);
	xassert(local_list);
	/* is_task and is_job can't both be true */
	xassert(!(is_task && is_job));

	if (!bit_alloc) {
		/*
		 * The gres.conf file must identify specific device files
		 * in order to set the CUDA_VISIBLE_DEVICES env var
		 */
		return;
	}

	itr = list_iterator_create(gres_devices);
	while ((gres_device = list_next(itr))) {
		int index;
		int global_env_index;
		if (!bit_test(bit_alloc, gres_device->index))
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
		if (use_dev_num)
			global_env_index = gres_device->dev_num;
		else
			global_env_index = gres_device->index;

		index = use_local_dev_index ?
			(*local_inx)++ : global_env_index;

		if (is_task) {
			if (!first_device) {
				first_device = gres_device;
			}

			if (!bit_test(usable_gres,
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

		if (global_id && !set_global_id) {
			*global_id = gres_device->dev_num;
			set_global_id = true;
		}

		/*
		 * If unique_id is set for the device, assume that we
		 * want to use it for the env var
		 */
		if (gres_device->unique_id)
			xstrfmtcat(new_local_list, "%s%s%s", local_prefix,
				   prefix, gres_device->unique_id);
		else
			xstrfmtcat(new_local_list, "%s%s%d", local_prefix,
				   prefix, index);
		xstrfmtcat(new_global_list, "%s%s%d", global_prefix,
			   prefix, global_env_index);

		local_prefix = ",";
		global_prefix = ",";
		device_considered = true;
	}
	list_iterator_destroy(itr);

	if (new_global_list) {
		xfree(*global_list);
		*global_list = new_global_list;
	}
	if (new_local_list) {
		xfree(*local_list);
		*local_list = new_local_list;
	}

	if (flags & GRES_INTERNAL_FLAG_VERBOSE) {
		char *usable_str;
		char *alloc_str;
		if (usable_gres)
			usable_str = bit_fmt_hexmask_trim(usable_gres);
		else
			usable_str = xstrdup("NULL");
		alloc_str = bit_fmt_hexmask_trim(bit_alloc);
		fprintf(stderr, "gpu-bind: usable_gres=%s; bit_alloc=%s; local_inx=%d; global_list=%s; local_list=%s\n",
			usable_str, alloc_str, *local_inx, *global_list,
			*local_list);
		xfree(alloc_str);
		xfree(usable_str);
	}
}

extern void common_send_stepd(buf_t *buffer, List gres_devices)
{
	uint32_t cnt = 0;
	gres_device_t *gres_device;
	ListIterator itr;

	if (gres_devices)
		cnt = list_count(gres_devices);
	pack32(cnt, buffer);

	if (!cnt)
		return;

	itr = list_iterator_create(gres_devices);
	while ((gres_device = list_next(itr))) {
		/* DON'T PACK gres_device->alloc */
		pack32(gres_device->index, buffer);
		pack32(gres_device->dev_num, buffer);
		pack32(gres_device->dev_desc.type, buffer);
		pack32(gres_device->dev_desc.major, buffer);
		pack32(gres_device->dev_desc.minor, buffer);
		packstr(gres_device->path, buffer);
		packstr(gres_device->unique_id, buffer);
	}
	list_iterator_destroy(itr);

	return;
}

extern void common_recv_stepd(buf_t *buffer, List *gres_devices)
{
	uint32_t i, cnt;
	uint32_t uint32_tmp = 0;
	gres_device_t *gres_device = NULL;

	xassert(gres_devices);

	safe_unpack32(&cnt, buffer);
	FREE_NULL_LIST(*gres_devices);

	if (!cnt)
		return;
	*gres_devices = list_create(destroy_gres_device);

	for (i = 0; i < cnt; i++) {
		gres_device = xmalloc(sizeof(gres_device_t));
		/*
		 * Since we are pulling from a list we need to append here
		 * instead of push.
		 */
		safe_unpack32(&uint32_tmp, buffer);
		gres_device->index = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		gres_device->dev_num = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		gres_device->dev_desc.type = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		gres_device->dev_desc.major = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		gres_device->dev_desc.minor = uint32_tmp;
		safe_unpackstr_xmalloc(&gres_device->path,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&gres_device->unique_id,
				       &uint32_tmp, buffer);
		list_append(*gres_devices, gres_device);
		/* info("adding %d %s %s", gres_device->dev_num, */
		/*      gres_device->major, gres_device->path); */
	}

	return;

unpack_error:
	error("%s: failed", __func__);
	destroy_gres_device(gres_device);
	return;
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

extern void gres_common_gpu_set_env(char ***env_ptr, bitstr_t *gres_bit_alloc,
				    bitstr_t *usable_gres, bool *already_seen,
				    int *local_inx, bool is_task, bool is_job,
				    gres_internal_flags_t flags,
				    uint32_t gres_conf_flags,
				    List gres_devices)
{
	uint64_t gres_cnt;
	char *global_list = NULL, *local_list = NULL, *slurm_env_var = NULL;

	if (is_job)
		slurm_env_var = "SLURM_JOB_GPUS";
	else
		slurm_env_var = "SLURM_STEP_GPUS";

	if (*already_seen) {
		global_list = xstrdup(getenvp(*env_ptr, slurm_env_var));

		/*
		 * Determine which existing env to check for local list.  We
		 * only need one since they are all the same and this is only
		 * for printing an error later.
		 */
		if (gres_conf_flags & GRES_CONF_ENV_NVML)
			local_list = xstrdup(getenvp(*env_ptr,
						     "CUDA_VISIBLE_DEVICES"));
		else if (gres_conf_flags & GRES_CONF_ENV_RSMI)
			local_list = xstrdup(getenvp(*env_ptr,
						     "ROCR_VISIBLE_DEVICES"));
		else if (gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			local_list = xstrdup(getenvp(*env_ptr,
						     "ZE_AFFINITY_MASK"));
		else if (gres_conf_flags & GRES_CONF_ENV_OPENCL)
			local_list = xstrdup(getenvp(*env_ptr,
						     "GPU_DEVICE_ORDINAL"));
	}

	common_gres_set_env(gres_devices, env_ptr,
			    usable_gres, "", local_inx,  gres_bit_alloc,
			    &local_list, &global_list, is_task, is_job, NULL,
			    flags, false);

	/*
	 * Set environment variables if GRES is found. Otherwise, unset
	 * environment variables, since this means GRES is not allocated.
	 * This is useful for jobs and steps that request --gres=none within an
	 * existing job allocation with GRES.
	 * Do not unset envs that could have already been set by an allocated
	 * sharing GRES (GPU).
	 */
	gres_cnt = gres_bit_alloc ? bit_set_count(gres_bit_alloc) : 0;
	if (gres_cnt) {
		char *gpus_on_node = xstrdup_printf("%"PRIu64, gres_cnt);
		env_array_overwrite(env_ptr, "SLURM_GPUS_ON_NODE",
				    gpus_on_node);
		xfree(gpus_on_node);
	} else if (!(flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		unsetenvp(*env_ptr, "SLURM_GPUS_ON_NODE");
	}

	if (global_list) {
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	} else if (!(flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		unsetenvp(*env_ptr, slurm_env_var);
	}

	if (local_list) {
		if (gres_conf_flags & GRES_CONF_ENV_NVML)
			env_array_overwrite(env_ptr, "CUDA_VISIBLE_DEVICES",
					    local_list);
		if (gres_conf_flags & GRES_CONF_ENV_RSMI)
			env_array_overwrite(env_ptr, "ROCR_VISIBLE_DEVICES",
					    local_list);
		if (gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			env_array_overwrite(env_ptr, "ZE_AFFINITY_MASK",
					    local_list);
		if (gres_conf_flags & GRES_CONF_ENV_OPENCL)
			env_array_overwrite(env_ptr, "GPU_DEVICE_ORDINAL",
					    local_list);
		xfree(local_list);
		*already_seen = true;
	} else if (!(flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		if (gres_conf_flags & GRES_CONF_ENV_NVML)
			unsetenvp(*env_ptr, "CUDA_VISIBLE_DEVICES");
		if (gres_conf_flags & GRES_CONF_ENV_RSMI)
			unsetenvp(*env_ptr, "ROCR_VISIBLE_DEVICES");
		if (gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			unsetenvp(*env_ptr, "ZE_AFFINITY_MASK");
		if (gres_conf_flags & GRES_CONF_ENV_OPENCL)
			unsetenvp(*env_ptr, "GPU_DEVICE_ORDINAL");
	}
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * RETURN: 1 if nothing was done, 0 otherwise.
 */
extern bool gres_common_epilog_set_env(char ***epilog_env_ptr,
				       gres_epilog_info_t *gres_ei,
				       int node_inx, uint32_t gres_conf_flags,
				       List gres_devices)
{
	int dev_inx_first = -1, dev_inx_last, dev_inx;
	gres_device_t *gres_device;
	char *vendor_gpu_str = NULL;
	char *slurm_gpu_str = NULL;
	char *sep = "";

	xassert(epilog_env_ptr);

	if (!gres_ei)
		return 1;

	if (!gres_devices)
		return 1;

	if (gres_ei->node_cnt == 0)	/* no_consume */
		return 1;

	if (node_inx > gres_ei->node_cnt) {
		error("bad node index (%d > %u)",
		      node_inx, gres_ei->node_cnt);
		return 1;
	}

	if (gres_ei->gres_bit_alloc &&
	    gres_ei->gres_bit_alloc[node_inx]) {
		dev_inx_first = bit_ffs(gres_ei->gres_bit_alloc[node_inx]);
	}
	if (dev_inx_first >= 0)
		dev_inx_last = bit_fls(gres_ei->gres_bit_alloc[node_inx]);
	else
		dev_inx_last = -2;
	for (dev_inx = dev_inx_first; dev_inx <= dev_inx_last; dev_inx++) {
		if (!bit_test(gres_ei->gres_bit_alloc[node_inx], dev_inx))
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
			env_array_overwrite(epilog_env_ptr,
					    "CUDA_VISIBLE_DEVICES",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_RSMI)
			env_array_overwrite(epilog_env_ptr,
					    "ROCR_VISIBLE_DEVICES",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_ONEAPI)
			env_array_overwrite(epilog_env_ptr,
					    "ZE_AFFINITY_MASK",
					    vendor_gpu_str);
		if (gres_conf_flags & GRES_CONF_ENV_OPENCL)
			env_array_overwrite(epilog_env_ptr,
					    "GPU_DEVICE_ORDINAL",
					    vendor_gpu_str);
		xfree(vendor_gpu_str);
	}
	if (slurm_gpu_str) {
		env_array_overwrite(epilog_env_ptr, "SLURM_JOB_GPUS",
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
