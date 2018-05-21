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
#include "src/common/xcgroup_read_config.h"

extern int common_node_config_load(List gres_conf_list,
				   char *gres_name,
				   List *gres_devices)
{
	int i, rc = SLURM_SUCCESS;
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf;
	hostlist_t hl;
	char *slash, *root_path, *one_name;
	gres_device_t *gres_device;

	xassert(gres_conf_list);
	xassert(gres_devices);

	itr = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		if ((gres_slurmd_conf->has_file != 1) ||
		    !gres_slurmd_conf->file ||
		    xstrcmp(gres_slurmd_conf->name, gres_name))
			continue;
		root_path = xstrdup(gres_slurmd_conf->file);
		slash = strrchr(root_path, '/');
		if (slash) {
			hl = hostlist_create(slash + 1);
			slash[1] = '\0';
		} else {
			hl = hostlist_create(root_path);
			root_path[0] = '\0';
		}

		if (!hl) {
			error("can't parse gres.conf file record (%s)",
			      gres_slurmd_conf->file);
			xfree(root_path);
			continue;
		}
		while ((one_name = hostlist_shift(hl))) {
			if (!*gres_devices)
				*gres_devices =
					list_create(destroy_gres_device);

			gres_device = xmalloc(sizeof(gres_device_t));
			list_append(*gres_devices, gres_device);

			xstrfmtcat(gres_device->path, "%s%s",
				   root_path, one_name);

			gres_device->major = gres_device_major(
				gres_device->path);

			for (i = 0; one_name[i]; i++) {
				if (!isdigit(one_name[i]))
					continue;
				gres_device->dev_num = atoi(one_name + i);
				break;
			}
			info("%s device number %d(%s):%s",
			     gres_name, gres_device->dev_num,
			     gres_device->path, gres_device->major);
			free(one_name);
		}
		hostlist_destroy(hl);
		xfree(root_path);
	}
	list_iterator_destroy(itr);

	return rc;
}

extern bool common_use_local_device_index(void)
{
	slurm_cgroup_conf_t slurm_cgroup_conf;
	char *task_plugin;
	bool use_cgroup = false;
	static bool use_local_index = false;
	static bool is_set = false;

	if (is_set)
		return use_local_index;
	is_set = true;

	task_plugin = slurm_get_task_plugin();
	if (!task_plugin)
		return use_local_index;

	if (strstr(task_plugin, "cgroup"))
		use_cgroup = true;
	xfree(task_plugin);
	if (!use_cgroup)
		return use_local_index;

	/* Read and parse cgroup.conf */
	memset(&slurm_cgroup_conf, 0, sizeof(slurm_cgroup_conf_t));
	if (read_slurm_cgroup_conf(&slurm_cgroup_conf) != SLURM_SUCCESS)
		return use_local_index;
	if (slurm_cgroup_conf.constrain_devices)
		use_local_index = true;
	free_slurm_cgroup_conf(&slurm_cgroup_conf);

	return use_local_index;
}

extern void common_gres_set_env(List gres_devices, char ***env_ptr,
				void *gres_ptr, int node_inx,
				bitstr_t *usable_gres, char *prefix,
				int *local_inx,
				char **local_list, char **global_list,
				bool reset, bool is_job)
{
	int i, len;
	bitstr_t *bit_alloc = NULL;
	bool use_local_dev_index = common_use_local_device_index();
	bool alloc_cnt = false;
	gres_device_t *gres_device, *first_device = NULL;
	ListIterator itr;

	if (!gres_devices)
		return;

	xassert(local_list);
	xassert(global_list);

	if (is_job) {
		gres_job_state_t *gres_job_ptr = (gres_job_state_t *) gres_ptr;
		if (gres_job_ptr &&
		    (node_inx >= 0) &&
		    (node_inx < gres_job_ptr->node_cnt) &&
		    gres_job_ptr->gres_bit_alloc &&
		    gres_job_ptr->gres_bit_alloc[node_inx]) {
			bit_alloc = gres_job_ptr->gres_bit_alloc[node_inx];
//FIXME: Change to total_gres check below once field is set
		} else if (gres_job_ptr && (gres_job_ptr->gres_per_node > 0))
			alloc_cnt = true;

	} else {
		gres_step_state_t *gres_step_ptr =
			(gres_step_state_t *) gres_ptr;
		if (gres_step_ptr &&
		    (gres_step_ptr->node_cnt == 1) &&
		    gres_step_ptr->gres_bit_alloc &&
		    gres_step_ptr->gres_bit_alloc[0]) {
			bit_alloc = gres_step_ptr->gres_bit_alloc[0];
//FIXME: Change to total_gres check below once field is set
		} else if (gres_step_ptr && (gres_step_ptr->gres_per_node > 0))
			alloc_cnt = true;
	}

	/* If we are resetting and we don't have a usable_gres we just exit */
	if (reset && !usable_gres)
		return;

	if (bit_alloc) {
		len = bit_size(bit_alloc);
		if (len != list_count(gres_devices)) {
			error("%s: gres list is not equal to the number of gres_devices.  This should never happen.",
			      __func__);
			return;
		}

		i = -1;
		itr = list_iterator_create(gres_devices);
		while ((gres_device = list_next(itr))) {
			i++;
			if (!bit_test(bit_alloc, i))
				continue;
			if (reset) {
				if (!first_device)
					first_device = gres_device;
				if (!bit_test(usable_gres, i))
					continue;
			}
			if (*global_list) {
				xstrcat(*global_list, ",");
				xstrcat(*local_list,  ",");
			}

			xstrfmtcat(*local_list, "%s%d",
				   prefix, use_local_dev_index ?
				   (*local_inx)++ : gres_device->dev_num);
			//info("looking at %d and %d", i, gres_device->dev_num);
			xstrfmtcat(*global_list, "%s%d",
				   prefix, gres_device->dev_num);
		}
		list_iterator_destroy(itr);

		if (reset && !*global_list && first_device) {
			xstrfmtcat(*local_list, "%s%d",
				   prefix, use_local_dev_index ?
				   (*local_inx)++ : first_device->dev_num);
			xstrfmtcat(*global_list, "%s%d",
				   prefix, first_device->dev_num);
		}
	} else if (alloc_cnt) {
		/* The gres.conf file must identify specific device files
		 * in order to set the CUDA_VISIBLE_DEVICES env var */
		debug("%s: unable to set env vars, no device files configured",
		      __func__);
	} else if (!*global_list) {
		xstrcat(*global_list, "NoDevFiles");
		xstrcat(*local_list, "NoDevFiles");
	}
}

extern void common_send_stepd(int fd, List gres_devices)
{
	int i;
	int cnt = 0;
	gres_device_t *gres_device;
	ListIterator itr;

	if (gres_devices)
		cnt = list_count(gres_devices);
	safe_write(fd, &cnt, sizeof(int));

	if (!cnt)
		return;

	itr = list_iterator_create(gres_devices);
	while ((gres_device = list_next(itr))) {
		safe_write(fd, &gres_device->dev_num, sizeof(int));
		if (gres_device->major) {
			i = strlen(gres_device->major);
			safe_write(fd, &i, sizeof(int));
			safe_write(fd, gres_device->major, i);
		} else {
			i = 0;
			safe_write(fd, &i, sizeof(int));
		}

		if (gres_device->path) {
			i = strlen(gres_device->path);
			safe_write(fd, &i, sizeof(int));
			safe_write(fd, gres_device->path, i);
		} else {
			i = 0;
			safe_write(fd, &i, sizeof(int));
		}
	}
	list_iterator_destroy(itr);

	return;

rwfail:
	error("%s: failed", __func__);
	return;
}

extern void common_recv_stepd(int fd, List *gres_devices)
{
	int i, cnt, len;
	gres_device_t *gres_device;

	xassert(gres_devices);

	safe_read(fd, &cnt, sizeof(int));
	if (*gres_devices) {
		list_destroy(*gres_devices);
		*gres_devices = NULL;
	}
	if (!cnt)
		return;
	*gres_devices = list_create(destroy_gres_device);

	for (i = 0; i < cnt; i++) {
		gres_device = xmalloc(sizeof(gres_device_t));
		/*
		 * Since we are pulling from a list we need to append here
		 * instead of push.
		 */
		list_append(*gres_devices, gres_device);
		safe_read(fd, &gres_device->dev_num, sizeof(int));
		safe_read(fd, &len, sizeof(int));
		if (len) {
			gres_device->major = xmalloc(sizeof(char) * (len + 1));
			safe_read(fd, gres_device->major, len);
		}
		safe_read(fd, &len, sizeof(int));
		if (len) {
			gres_device->path = xmalloc(sizeof(char) * (len + 1));
			safe_read(fd, gres_device->path, len);
		}
		/* info("adding %d %s %s", gres_device->dev_num, */
		/*      gres_device->major, gres_device->path); */
	}

	return;

rwfail:
	error("%s: failed", __func__);
	return;
}
