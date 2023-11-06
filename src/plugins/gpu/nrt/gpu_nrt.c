/*****************************************************************************\
 *  gpu_nrt.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Caden Ellis <caden@schedmd.com>
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

#include <dirent.h>

#include "../common/gpu_common.h"

#define NEURON_SYSFS_PREFIX "/sys/devices/virtual/neuron_device/"
#define NEURON_SYSFS_DEVICE_NAME_PREFIX \
	NEURON_SYSFS_PREFIX "neuron%d/info/architecture/device_name"
#define NEURON_SYSFS_CONNECTED_DEV_PREFIX \
	NEURON_SYSFS_PREFIX "neuron%d/connected_devices"

#define CONNECTED_DEVICES_SZ 100
#define DEVICE_NAME_SZ 50

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
const char plugin_name[] = "GPU NRT plugin";
const char plugin_type[] = "gpu/nrt";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int _count_devices(unsigned int *dev_count)
{
	struct dirent *de;
	unsigned int dev_num;

	DIR *dr = opendir(NEURON_SYSFS_PREFIX);

	*dev_count = 0;

	if (!dr)
		return SLURM_ERROR;

	while ((de = readdir(dr))) {
		if ((sscanf(de->d_name, "neuron%u\n", &dev_num) == 1)) {
			(*dev_count)++;
		}
	}
	closedir(dr);
	return SLURM_SUCCESS;
}

static char *_get_device_name(unsigned int dev_inx)
{
	FILE *fp = NULL;
	char *sysfs_file = NULL;
	char *device_name = NULL;

	sysfs_file = xstrdup_printf(NEURON_SYSFS_DEVICE_NAME_PREFIX, dev_inx);
	fp = fopen(sysfs_file, "r");
	if (!fp) {
		debug("Could not access device name in Neuron sysfs interface");
		xfree(sysfs_file);
		return NULL;
	}

	device_name = xmalloc(DEVICE_NAME_SZ);

	if (!fscanf(fp, "%s", device_name))
		debug("Could not read Neuron device name");

	xstrtolower(device_name);
	xfree(sysfs_file);
	fclose(fp);
	return device_name;
}

static bool _is_link(int *link_nums, unsigned int dev_cnt, int dev_inx)
{
	for (unsigned int i = 0; i < dev_cnt; i++) {
		if (link_nums[i] == dev_inx)
			return true;
	}
	return false;
}

static char *_get_connected_devices(int dev_inx, unsigned int dev_cnt)
{
	FILE *fp = NULL;
	char *sysfs_file = NULL;
	char *tok, *save_ptr;
	char conn_dev[CONNECTED_DEVICES_SZ];
	int link_nums[CONNECTED_DEVICES_SZ];
	char *links = NULL;
	int tmp_link;
	int num_links = 0;

	sysfs_file = xstrdup_printf(NEURON_SYSFS_CONNECTED_DEV_PREFIX, dev_inx);
	fp = fopen(sysfs_file, "r");
	if (!fp) {
		debug("Could not access connected_devices in Neuron sysfs interface");
		xfree(sysfs_file);
		return NULL;
	}

	if (!fgets(conn_dev, CONNECTED_DEVICES_SZ, fp)) {
		debug("Could not read Neuron connected devices. Setting empty links");
		goto endit;
	}

	/*
	 * Convert to array of ints for processing
	 * The link numbers can be in any order
	 */
	tok = strtok_r(conn_dev, ", ", &save_ptr);
	while (tok) {
		tmp_link = atoi(tok);
		link_nums[num_links++] = tmp_link;
		tok = strtok_r(NULL, ", ", &save_ptr);
	}

	for (unsigned int i = 0; i < dev_cnt; i++) {
		if (_is_link(link_nums, num_links, i))
			xstrfmtcat(links, "%s%d", i ? "," : "", 1);
		else if (i == dev_inx)
			xstrfmtcat(links, "%s%d", i ? "," : "", -1);
		else
			xstrfmtcat(links, "%s%d", i ? "," : "", 0);
	}

endit:
	xfree(sysfs_file);
	fclose(fp);
	return links;
}

static list_t *_get_system_gpu_list_neuron(node_config_load_t *node_conf)
{
	struct dirent *de;
	unsigned int dev_inx;
	unsigned int dev_cnt = 0;
	list_t *gres_list_system = NULL;
	DIR *dr = opendir(NEURON_SYSFS_PREFIX);

	if (!dr)
		return NULL;

	_count_devices(&dev_cnt);

	while ((de = readdir(dr))) {
		if ((sscanf(de->d_name, "neuron%d\n", &dev_inx) == 1)) {

			char *device_file = NULL;
			char *links = NULL;
			char *device_name = NULL;

			gres_slurmd_conf_t gres_slurmd_conf = {
				.count = 1,
				.cpu_cnt = node_conf->cpu_cnt,
				.name = "gpu",
			};

			xstrfmtcat(device_file, "/dev/neuron%u", dev_inx);
			device_name = _get_device_name(dev_inx);
			links = _get_connected_devices(dev_inx, dev_cnt);

			debug2("GPU index %u:", dev_inx);
			debug2("    Name: %s", device_name);
			debug2("    Links: %s", links);
			debug2("    Device File: %s", device_file);

			gres_slurmd_conf.type_name = device_name;
			gres_slurmd_conf.links = links;
			gres_slurmd_conf.file = device_file;

			if (!gres_list_system)
				gres_list_system =
					list_create(destroy_gres_slurmd_conf);

			/* Add the GPU to list */
			add_gres_to_list(gres_list_system, &gres_slurmd_conf);

			xfree(device_file);
			xfree(links);
			xfree(device_name);
		}
	}
	closedir(dr);
	return gres_list_system;
}

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern void gpu_p_get_device_count(unsigned int *device_count)
{
	if (_count_devices(device_count) != SLURM_SUCCESS)
		error("Failed to get device count from neuron sysfs interface");

	return;
}

extern void gpu_p_reconfig(void)
{
	return;
}

extern list_t *gpu_p_get_system_gpu_list(node_config_load_t *node_conf)
{
	list_t *gres_list_system = _get_system_gpu_list_neuron(node_conf);

	xassert(node_conf);

	if (!gres_list_system)
		error("System GPU detection failed");

	return gres_list_system;
}

extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	return;
}

extern void gpu_p_step_hardware_fini(void)
{
	return;
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	return SLURM_SUCCESS;
}

extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
/*
 * Currently there is the ability to read memory usage of a device in the
 * sysfs interface but it is not PID based and requires adding different
 * memory fields for each core for a total value on the device.
 */

/*
 *#define NEURON_SYSFS_CORE_CNT_PREFIX \
 *	NEURON_SYSFS_PREFIX "neuron%d/core_count"
 *
 *#define NEURON_SYSFS_DEVICE_MEM_PREFIX \
 *	NEURON_SYSFS_PREFIX
 *	"neuron%d/neuron_core%d/stats/memory_usage/device_mem/present"
 *
 *static int gpumem_pos = -1; // Init this in init()
 *
 *static int _get_core_count(int dev_inx)
 *{
 *	FILE *fp = NULL;
 *	char *sysfs_file = NULL;
 *	int dev_core_cnt = 0;
 *
 *	sysfs_file = xstrdup_printf(NEURON_SYSFS_CORE_CNT_PREFIX, dev_inx);
 *	fp = fopen(sysfs_file, "r");
 *	if (!fp) {
 *		debug("Could not access core count in Neuron sysfs interface");
 *		xfree(sysfs_file);
 *		return -1;
 *	}
 *
 *	if (!fscanf(fp, "%d", dev_core_cnt))
 *		debug("Could not read Neuron core count");
 *
 *	xfree(sysfs_file);
 *	fclose(fp);
 *	return dev_core_cnt;
 *}
 *
 *static void _read_mem(int dev_inx, int core_cnt, pid_t pid, acct_gather_data_t *data)
 *{
 *	FILE *fp = NULL;
 *	char *sysfs_file = NULL;
 *	int dev_mem = 0;
 *
 *	for (int i = 0; i < core_cnt; i++) {
 *
 *		sysfs_file = xstrdup_printf(NEURON_SYSFS_DEVICE_MEM_PREFIX,
 *					    dev_inx, i);
 *		fp = fopen(sysfs_file, "r");
 *		if (!fp) {
 *			debug("Could not access device memory in Neuron sysfs interface");
 *			xfree(sysfs_file);
 *			return;
 *		}
 *
 *		if (!fscanf(fp, "%d", dev_mem))
 *			debug("Could not read Neuron device memory for core %d",
 *			      i);
 *
 *		data[gpumem_pos].size_read += dev_mem;
 *
 *		xfree(sysfs_file);
 *		fclose(fp);
 *	}
 *}
 *
 *extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
 *{
 *
 *	 // TODO: Determine how to incorporate getting acct_gather_data_t for
 *	 // specific pids. As is this code just adds all memory being used across
 *	 // all cores on all devices
 *
 *	unsigned int device_count = 0;
 *	int core_cnt = 0;
 *	bool track_gpumem;
 *
 *	track_gpumem = (gpumem_pos != -1);
 *
 *	if (!track_gpumem) {
 *		debug2("%s: We are not tracking TRES gpumem", __func__);
 *		return SLURM_SUCCESS;
 *	}
 *
 *	_count_devices(&device_count);
 *
 *	data[gpumem_pos].size_read = 0;
 *
 *	for (int i = 0; i < device_count; i++) {
 *
 *		if (track_gpumem) {
 *			core_cnt = _get_core_count(i);
 *			_read_mem(i, core_cnt, pid, data)
 *		}
 *
 *		log_flag(JAG, "pid %d has MemMB=%lu",
 *			 pid,
 *			 data[gpumem_pos].size_read / 1048576);
 *	}
 *}
 */
	return SLURM_SUCCESS;
}
