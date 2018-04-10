/*****************************************************************************\
 *  powercapping.h - Definitions for power capping logic in the controller
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#ifndef _POWERCAPPING_H
#define _POWERCAPPING_H

#include <stdint.h>
#include <time.h>
#include "src/slurmctld/slurmctld.h"

/**
 * powercap_get_cluster_max_watts
 * return the max power consumption of the cluster
 * RET uint32_t - the max consumption in watts
 */
uint32_t powercap_get_cluster_max_watts(void);

/**
 * powercap_get_cluster_min_watts
 * return the min power consumption of the cluster
 * RET uint32_t - the min consumption in watts
 */
uint32_t powercap_get_cluster_min_watts(void);

/**
 * powercap_get_cluster_current_cap
 * return the current powercap value
 * RET uint32_t - powercap
 */
uint32_t powercap_get_cluster_current_cap(void);

/**
 * powercap_set_cluster_cap
 * set a new powercap value
 * IN uint32_t - new_cap
 * RET int - 0 or error code
 */
int powercap_set_cluster_cap(uint32_t new_cap);

/**
 * powercap_get_cluster_adjusted_max_watts
 * return max power consumption of the cluster,
 * taking into consideration the nodes which are POWERED DOWN
 * RET uint32_t - the max consumption in watts
 */
uint32_t powercap_get_cluster_adjusted_max_watts(void);

/**
 * powercap_get_cluster_current_max_watts
 * return current max power consumption of the cluster,
 * taking into consideration the nodes which are POWERED DOWN
 * and the nodes which are idle
 * RET uint32_t - the max consumption in watts
 */
uint32_t powercap_get_cluster_current_max_watts(void);

/**
 * powercap_get_node_bitmap_maxwatt
 * return current max consumption value of the cluster,
 * taking into consideration the nodes which are POWERED DOWN
 * and the nodes which are idle using the input bitmap to identify
 * them.
 * A null argument means, use the controller idle_node_bitmap instead.
 * IN bitstr_t* idle_bitmap
 * RET uint32_t - the max consumption in watts
 */
uint32_t powercap_get_node_bitmap_maxwatts(bitstr_t* select_bitmap);

/**
 * powercap_get_job_cap
 * return the cap value of a job taking into account the current cap
 * as well as the power reservations defined on the interval
 *
 * IN job_ptr - job under consideration
 * IN when - time of job start
 * IN reboot - node reboot required
 * RET - The power cap this job is restricted to
 */
uint32_t powercap_get_job_cap(struct job_record *job_ptr, time_t when,
			      bool reboot);

/**
 * power_layout_ready
 * check if the layout has at least the minimum available attributes
 * per node declared and possible to be retrieved
 *
 * RET bool - whether the layout is ready for usage
 */
bool power_layout_ready(void);

/**
 * which_power_layout
 * return which power layout is activated to be used for powercapping
 *
 * RET int - 0 both or none, 1 power layout, 2 power_cpufreq layout
 */
int which_power_layout(void);

/**
 * powercap_get_job_nodes_numfreq
 * return the number of allowed frequencies as long as in which positions
 * they are in the layouts
 * IN bitstr_t* select_bitmap related to the nodes that the job could allocate
 * IN uint32_t cpu_freq_min for the job as given in the command
 * IN uint32_t cpu_freq_max for the job as given in the command
 * RET int* - an array of allowed frequency positions 
 *            and in 0 the number of total allowed frequencies
 */
int* powercap_get_job_nodes_numfreq(bitstr_t *select_bitmap,
			uint32_t cpu_freq_min, uint32_t cpu_freq_max);

/**
 * powercap_get_node_bitmap_maxwatts_dvfs
 * similar with powercap_get_node_bitmap_maxwatt with the difference that
 * there is a return on the max_watts_dvfs array of possible max_watts in case
 * the cores get different allowed cpu frequencies
 * IN bitstr_t* idle_bitmap 
 * IN bitstr_t* select_bitmap
 * IN/OUT uint32_t *max_watts_dvfs for the job as given in the command
 * IN int* allowed_freqs for the job as given in the command
 * IN uint32_t num_cpus par job par node 
 * RET uint32_t - the max consumption in watts
 */
uint32_t powercap_get_node_bitmap_maxwatts_dvfs(bitstr_t *idle_bitmap,
			bitstr_t *select_bitmap, uint32_t *max_watts_dvfs,
			int* allowed_freqs, uint32_t num_cpus);
/**
 * powercap_get_job_optimal_cpufreq
 * return the position upon the allowed_freqs array that gives us the optimal
 * cpu frequency for the job to be run based on the power budget available
 * and the usage of the already executing jobs
 * IN uint32_t powercap 
 * IN int* allowed_freqs
 * RET int - the position on the allowed_freqs array for the optimal cpufreq
 */
int powercap_get_job_optimal_cpufreq(uint32_t powercap, int* allowed_freqs);

/**
 * powercap_get_cpufreq
 * return the cpu frequency related to a particular position on the layouts
 * IN bitstr_t* select_bitmap
 * IN int k
 * RET uint32_t - the cpu frequency
 */
uint32_t powercap_get_cpufreq(bitstr_t *select_bitmap, int k);

#endif /* !_POWERCAPPING_H */
