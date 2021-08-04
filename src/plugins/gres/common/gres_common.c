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

			gres_device->dev_num = -1;
			gres_device->index = index;
			gres_device->path = xstrdup(one_name);

			gres_device->major = gres_device_major(
				gres_device->path);
			gres_device->unique_id =
				xstrdup(gres_slurmd_conf->unique_id);
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

			/*
			 * Don't check for file duplicates or increment the
			 * device bitmap index if this is a MultipleFiles GRES
			 */
			if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_MULT)
				continue;

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
		packstr(gres_device->major, buffer);
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
		safe_unpackstr_xmalloc(&gres_device->major,
				       &uint32_tmp, buffer);
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
