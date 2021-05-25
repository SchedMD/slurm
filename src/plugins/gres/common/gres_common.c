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

#include <ctype.h>

#include "gres_common.h"

#include "src/common/xstring.h"

static void _free_name_list(void *x)
{
	free(x);
}
static int _match_name_list(void *x, void *key)
{
	if (!xstrcmp(x, key))
		return 1;	/* duplicate file name */
	return 0;
}

/*
 * Common validation for what was read in from the gres.conf.
 * IN gres_conf_list
 * IN gres_name
 * OUT gres_devices
 */
extern int common_node_config_load(List gres_conf_list,
				   char *gres_name,
				   List *gres_devices)
{
	int i, tmp, rc = SLURM_SUCCESS;
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf;
	hostlist_t hl;
	char *one_name;
	gres_device_t *gres_device;
	List names_list;
	int max_dev_num = -1;
	int index = 0;

	xassert(gres_conf_list);
	xassert(gres_devices);

	names_list = list_create(_free_name_list);
	itr = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(itr))) {
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
			int digit = -1;
			if (!*gres_devices) {
				*gres_devices =
					list_create(destroy_gres_device);
			}
			gres_device = xmalloc(sizeof(gres_device_t));
			list_append(*gres_devices, gres_device);

			gres_device->index = index;
			gres_device->path = xstrdup(one_name);

			gres_device->major = gres_device_major(
				gres_device->path);
			tmp = strlen(one_name);
			for (i = 1;  i <= tmp; i++) {
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

			if (gres_device->dev_num > max_dev_num)
				max_dev_num = gres_device->dev_num;

			if ((rc == SLURM_SUCCESS) &&
			    list_find_first(names_list, _match_name_list,
					    one_name)) {
				error("%s duplicate device file name (%s)",
				      gres_name, one_name);
				rc = SLURM_ERROR;
			}

			(void) list_append(names_list, one_name);

			/*
			 * If count == 1, but there are multiple files then
			 * this is a MultipleFile device. Don't touch the
			 * index then, as the index keys into the allocation
			 * bitmap.
			 */
			if (gres_slurmd_conf->count != 1)
				index++;
		}
		hostlist_destroy(hl);
		if (gres_slurmd_conf->count == 1)
			index++;
	}
	list_iterator_destroy(itr);
	list_destroy(names_list);

	if (*gres_devices) {
		itr = list_iterator_create(*gres_devices);
		while ((gres_device = list_next(itr))) {
			if (gres_device->dev_num == -1)
				gres_device->dev_num = ++max_dev_num;
			log_flag(GRES, "%s device number %d(%s):%s",
				 gres_name, gres_device->dev_num,
				 gres_device->path, gres_device->major);
		}
		list_iterator_destroy(itr);
	}

	return rc;
}

extern bool common_use_local_device_index(void)
{
	slurm_cgroup_conf_t *cg_conf;
	bool use_cgroup = false;
	static bool use_local_index = false;
	static bool is_set = false;

	if (is_set)
		return use_local_index;
	is_set = true;

	if (!slurm_conf.task_plugin)
		return use_local_index;

	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		use_cgroup = true;
	if (!use_cgroup)
		return use_local_index;

	cg_conf = cgroup_g_get_conf();
	if (cg_conf && cg_conf->constrain_devices)
		use_local_index = true;
	cgroup_g_free_conf(cg_conf);

	return use_local_index;
}

extern void common_gres_set_env(List gres_devices, char ***env_ptr,
				void *gres_ptr, int node_inx,
				bitstr_t *usable_gres, char *prefix,
				int *local_inx, uint64_t *gres_per_node,
				char **local_list, char **global_list,
				bool reset, bool is_job, int *global_id,
				gres_internal_flags_t flags)
{
	int first_inx = -1;
	bitstr_t *bit_alloc = NULL;
	bool use_local_dev_index = common_use_local_device_index();
	bool alloc_cnt = false, set_global_id = false;
	gres_device_t *gres_device, *first_device = NULL;
	ListIterator itr;
	char *global_prefix = "", *local_prefix = "";
	char *new_global_list = NULL, *new_local_list = NULL;
	uint64_t tmp_gres_per_node = 0;

	if (!gres_devices)
		return;

	xassert(global_list);
	xassert(local_list);

	if (is_job) {
		gres_job_state_t *gres_job_ptr = (gres_job_state_t *) gres_ptr;
		if (gres_job_ptr &&
		    (node_inx >= 0) &&
		    (node_inx < gres_job_ptr->node_cnt) &&
		    gres_job_ptr->gres_bit_alloc &&
		    gres_job_ptr->gres_bit_alloc[node_inx]) {
			bit_alloc = gres_job_ptr->gres_bit_alloc[node_inx];
		} else if (gres_job_ptr &&
			   ((gres_job_ptr->gres_per_job    > 0) ||
			    (gres_job_ptr->gres_per_node   > 0) ||
			    (gres_job_ptr->gres_per_socket > 0) ||
			    (gres_job_ptr->gres_per_task   > 0))) {
			alloc_cnt = true;
		}
		if (gres_job_ptr) {
			tmp_gres_per_node = gres_job_ptr->gres_per_node;
		}
	} else {
		gres_step_state_t *gres_step_ptr =
			(gres_step_state_t *) gres_ptr;
		if (gres_step_ptr &&
		    (gres_step_ptr->node_cnt == 1) &&
		    gres_step_ptr->gres_bit_alloc &&
		    gres_step_ptr->gres_bit_alloc[0]) {
			bit_alloc = gres_step_ptr->gres_bit_alloc[0];
		} else if (gres_step_ptr &&
			   ((gres_step_ptr->gres_per_step   > 0) ||
			    (gres_step_ptr->gres_per_node   > 0) ||
			    (gres_step_ptr->gres_per_socket > 0) ||
			    (gres_step_ptr->gres_per_task   > 0))) {
			alloc_cnt = true;
		}
		if (gres_step_ptr) {
			tmp_gres_per_node = gres_step_ptr->gres_per_node;
		}
	}

	/* If we are resetting and we don't have a usable_gres we just exit */
	if (reset && !usable_gres)
		return;

	if (bit_alloc) {
		itr = list_iterator_create(gres_devices);
		while ((gres_device = list_next(itr))) {
			int index;
			if (!bit_test(bit_alloc, gres_device->index))
				continue;

			index = use_local_dev_index ?
				(*local_inx)++ : gres_device->dev_num;

			if (reset) {
				if (!first_device) {
					first_inx = index;
					first_device = gres_device;
				}

				if (!bit_test(usable_gres,
					      use_local_dev_index ?
					      index : gres_device->index))
					continue;
			}

			if (global_id && !set_global_id) {
				*global_id = gres_device->dev_num;
				set_global_id = true;
			}

			xstrfmtcat(new_local_list, "%s%s%d", local_prefix,
				   prefix, index);
			local_prefix = ",";
			//info("looking at %d and %d",
			//     gres_device->index, gres_device->dev_num);
			xstrfmtcat(new_global_list, "%s%s%d", global_prefix,
				   prefix, gres_device->dev_num);
			global_prefix = ",";
		}
		list_iterator_destroy(itr);

		/*
		 * Bind to the first allocated device as a fallback if the bind
		 * request does not specify any devices within the allocation.
		 */
		if (reset && !new_global_list && first_device) {
			char *usable_gres_str = bit_fmt_full(usable_gres);
			char *usable_gres_str_hex =
				bit_fmt_hexmask_trim(usable_gres);
			error("Bind request %s (%s) does not specify any devices within the allocation. Binding to the first device in the allocation instead.",
			      usable_gres_str, usable_gres_str_hex);
			xfree(usable_gres_str);
			xfree(usable_gres_str_hex);
			xstrfmtcat(new_local_list, "%s%s%d", local_prefix,
				   prefix, first_inx);
			(*local_inx) = first_inx;
			xstrfmtcat(new_global_list, "%s%s%d", global_prefix,
				   prefix, first_device->dev_num);
		}
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

	} else if (alloc_cnt) {
		/*
		 * The gres.conf file must identify specific device files
		 * in order to set the CUDA_VISIBLE_DEVICES env var
		 */
		debug("%s: unable to set env vars, no device files configured",
		      __func__);
	}

	if (gres_per_node) {
		*gres_per_node = tmp_gres_per_node;
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
		packstr(gres_device->major, buffer);
		packstr(gres_device->path, buffer);
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
		safe_unpackstr_xmalloc(&gres_device->major,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&gres_device->path,
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
		"Links:%s Flags:%s File:%s", gres_slurmd_conf->name,
		gres_slurmd_conf->type_name, gres_slurmd_conf->count,
		gres_slurmd_conf->cpu_cnt, gres_slurmd_conf->cpus,
		gres_slurmd_conf->links,
		gres_flags2str(gres_slurmd_conf->config_flags),
		gres_slurmd_conf->file);
}


/*
 * Print the gres.conf record in a parsable format
 * Do NOT change the format of this without also changing test39.18!
 */
static void _print_gres_conf_parsable(gres_slurmd_conf_t *gres_slurmd_conf,
				      log_level_t log_lvl)
{
	log_var(log_lvl, "GRES_PARSABLE[%s](%"PRIu64"):%s|%d|%s|%s|%s|",
		gres_slurmd_conf->name, gres_slurmd_conf->count,
		gres_slurmd_conf->type_name, gres_slurmd_conf->cpu_cnt,
		gres_slurmd_conf->cpus, gres_slurmd_conf->links,
		gres_slurmd_conf->file);
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
			print_gres_conf(gres_record, log_lvl);
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
