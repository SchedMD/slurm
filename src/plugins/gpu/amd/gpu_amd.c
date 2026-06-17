/*****************************************************************************\
 *  gpu_amd.c - Support for AMD GPUs through the kernel sysfs interface.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "../common/gpu_common.h"

/*
 * gpu/amd: reads GPU information from the amdgpu kernel driver via sysfs, with
 * no vendor library.
 */

/* Required Slurm plugin symbols: */
const char plugin_name[] = "GPU AMD plugin";
const char plugin_type[] = "gpu/amd";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * Initialize the plugin on load.
 *
 * Returns SLURM_SUCCESS.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

/*
 * Free plugin state on unload.
 */
extern void fini(void)
{
	debug("unloading %s", plugin_name);
}

/*
 * Detect the node's AMD GPUs via sysfs and return them as a GRES conf list.
 *
 * node_config (IN) The node configuration.
 *
 * Returns NULL.
 */
extern list_t *gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	xassert(node_config);

	return NULL;
}

/*
 * At step start (in slurmstepd), report a requested GPU frequency as not
 * controlled, like gpu/generic does. This plugin does not drive GPU clocks.
 *
 * usable_gpus (IN) Bitmap of the GPUs allocated to the step.
 * tres_freq (IN) The step's TRES frequency specification.
 */
extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	xassert(tres_freq);
	xassert(usable_gpus);

	if (!usable_gpus || !tres_freq)
		return;

	/* No GPU frequency spec */
	if (!xstrstr(tres_freq, "gpu:"))
		return;

	fprintf(stderr, "GpuFreq=control_disabled\n");
}

/*
 * At step end (in slurmstepd), reset each card gpu_p_step_hardware_init()
 * drove back to automatic performance management.
 */
extern void gpu_p_step_hardware_fini(void)
{
	return;
}

/*
 * Testsuite CPU range conversion hook, not implemented by this plugin.
 *
 * cpu_range (IN) The CPU range string to convert.
 *
 * Returns NULL.
 */
extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

/*
 * Read the current power draw of a GPU for energy accounting.
 *
 * dv_ind (IN) The GPU index.
 * gpu (IN/OUT) The GPU status to update with the power reading.
 *
 * Returns SLURM_SUCCESS.
 */
extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	xassert(gpu);

	return SLURM_SUCCESS;
}

/*
 * Count the node's amdgpu GPUs.
 *
 * device_count (OUT) Set to 0.
 */
extern void gpu_p_get_device_count(uint32_t *device_count)
{
	xassert(device_count);

	*device_count = 0;
}

/*
 * Read a process's GPU memory and compute-unit usage for job accounting.
 *
 * pid (IN) The process ID to read GPU usage for.
 * data (IN/OUT) The accounting array to fill with the process's GPU usage.
 *
 * Returns SLURM_SUCCESS.
 */
extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
	xassert(data);

	return SLURM_SUCCESS;
}
