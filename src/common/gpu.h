/*****************************************************************************\
 *  gpu.h - driver for gpu plugin
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
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

#ifndef _GPU_PLUGIN_H
#define _GPU_PLUGIN_H

#include "slurm/slurm.h"
#include "src/common/gres.h"

// array of struct to track the status of a GPU
typedef struct {
	uint32_t last_update_watt;
	time_t last_update_time;
	time_t previous_update_time;
	acct_gather_energy_t energy;
} gpu_status_t;

extern int gpu_plugin_init(void);
extern int gpu_plugin_fini(void);
extern void gpu_g_reconfig(void);
extern List gpu_g_get_system_gpu_list(node_config_load_t *node_conf);
extern void gpu_g_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq);
extern void gpu_g_step_hardware_fini(void);
extern char *gpu_g_test_cpu_conv(char *cpu_range);
extern int gpu_g_energy_read(uint32_t dv_ind, gpu_status_t *gpu);
extern void gpu_g_get_device_count(unsigned int *device_count);

#endif /* !_GPU_PLUGIN_H */
