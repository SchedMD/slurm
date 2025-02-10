/*****************************************************************************\
 *  gpu_nvidia.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#define NVIDIA_PROC_DRIVER_PREFIX "/proc/driver/nvidia/gpus/"
#define NVIDIA_INFORMATION_PREFIX "/proc/driver/nvidia/gpus/%s/information"
#define NVIDIA_CPULIST_PREFIX "/sys/bus/pci/drivers/nvidia/%s/local_cpulist"
#define MAX_CPUS 0x8000

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
const char plugin_name[] = "GPU Nvidia plugin";
const char plugin_type[] = "gpu/nvidia";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int _count_devices(unsigned int *dev_count)
{
	struct dirent *de;

	DIR *dr = opendir(NVIDIA_PROC_DRIVER_PREFIX);

	*dev_count = 0;

	if (!dr)
		return SLURM_ERROR;

	while ((de = readdir(dr))) {
		if (strlen(de->d_name) > 4) {
			(*dev_count)++;
		}
	}
	closedir(dr);
	return SLURM_SUCCESS;
}

static void _set_cpu_affinity(node_config_load_t *node_conf, char *bus_id,
			      char **cpus)
{
	FILE *f;
	char buffer[2000];
	char *cpu_aff_mac_range = NULL, *path = NULL;
	bitstr_t *enabled_cpus_bits = NULL, *cpus_bitmap = NULL;

	if (!(slurm_conf.conf_flags & CONF_FLAG_ECORE)) {
		enabled_cpus_bits = bit_alloc(MAX_CPUS);
		for (int i = 0; i < conf->block_map_size; i++) {
			bit_set(enabled_cpus_bits, conf->block_map[i]);
		}
	}

	path = xstrdup_printf(NVIDIA_CPULIST_PREFIX, bus_id);
	cpus_bitmap = bit_alloc(MAX_CPUS);

	f = fopen(path, "r");
	while (fgets(buffer, sizeof(buffer), f)) {
		if (bit_unfmt(cpus_bitmap, buffer))
			error("Unable to parse cpu list in %s", path);
	}
	fclose(f);

	if (enabled_cpus_bits) {
		/*
		 * Mask out E-cores that may be included from nvml's cpu
		 * affinity bitstring.
		 */
		bit_and(cpus_bitmap, enabled_cpus_bits);
	}

	// Convert from bitstr_t to cpu range str
	cpu_aff_mac_range = bit_fmt_full(cpus_bitmap);

	// Convert cpu range str from machine to abstract(slurm) format
	if (node_conf->xcpuinfo_mac_to_abs(cpu_aff_mac_range, cpus)) {
		error("Conversion from machine to abstract failed");
	}

	debug2("CPU Affinity Range - Machine: %s", cpu_aff_mac_range);
	debug2("Core Affinity Range - Abstract: %s", *cpus);

	FREE_NULL_BITMAP(enabled_cpus_bits);
	FREE_NULL_BITMAP(cpus_bitmap);
	xfree(cpu_aff_mac_range);
	xfree(path);
}

static void _set_name_and_file(node_config_load_t *node_conf, char *bus_id,
			       char **device_name, char **device_file)
{
	FILE *f;
	uint32_t minor_number = NO_VAL;
	char buffer[2000];
	char *path;
	const char whitespace[] = " \f\n\r\t\v";

	path = xstrdup_printf(NVIDIA_INFORMATION_PREFIX, bus_id);
	f = fopen(path, "r");

	while (fgets(buffer, sizeof(buffer), f) != NULL) {
		if (!xstrncmp("Device Minor:", buffer, 13)) {
			minor_number = strtol(buffer + 13, NULL, 10);
			xstrfmtcat(*device_file, "/dev/nvidia%u", minor_number);
		} else if (!xstrncmp("Model:", buffer, 6)) {
			buffer[strcspn(buffer, "\n")] = 0; /* Remove newline */
			*device_name = xstrdup(buffer + 6 +
					       strspn(buffer + 6, whitespace));
			gpu_common_underscorify_tolower(*device_name);
		}
	}
	fclose(f);

	if (!*device_file)
		error("Device file and Minor number not found");
	if (!*device_name)
		error("Device name not found");

	debug2("Name: %s", *device_name);
	debug2("Device File (minor number): %s", *device_file);
	xfree(path);
}

static list_t *_get_system_gpu_list_nvidia(node_config_load_t *node_conf)
{
	struct dirent *de;
	list_t *gres_list_system = NULL;
	DIR *dr = opendir(NVIDIA_PROC_DRIVER_PREFIX);

	if (!dr)
		return NULL;

	while ((de = readdir(dr))) {
		if (strlen(de->d_name) < 5) /* Don't include .. and . */
			continue;
		gres_slurmd_conf_t gres_slurmd_conf = {
			.config_flags =
				GRES_CONF_ENV_NVML | GRES_CONF_AUTODETECT,
			.count = 1,
			.cpu_cnt = node_conf->cpu_cnt,
			.name = "gpu",
		};

		_set_name_and_file(node_conf, de->d_name,
				   &gres_slurmd_conf.type_name,
				   &gres_slurmd_conf.file);
		_set_cpu_affinity(node_conf, de->d_name,
				  &gres_slurmd_conf.cpus);

		if (!gres_list_system)
			gres_list_system = list_create(
				destroy_gres_slurmd_conf);

		/* Add the GPU to list */
		add_gres_to_list(gres_list_system, &gres_slurmd_conf);

		xfree(gres_slurmd_conf.file);
		xfree(gres_slurmd_conf.type_name);
		xfree(gres_slurmd_conf.cpus);
	}
	closedir(dr);

	return gres_list_system;
}

extern int init(void)
{
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern void gpu_p_get_device_count(unsigned int *device_count)
{
	_count_devices(device_count);
}

extern void gpu_p_reconfig(void)
{
	return;
}

extern list_t *gpu_p_get_system_gpu_list(node_config_load_t *node_conf)
{
	list_t *gres_list_system = _get_system_gpu_list_nvidia(node_conf);

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
	return SLURM_SUCCESS;
}
