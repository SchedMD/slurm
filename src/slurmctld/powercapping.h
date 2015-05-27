/*****************************************************************************\
 *  powercapping.h - Definitions for power capping logic in the controller
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
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
 * IN struct job_record* job_ptr
 * IN time_t when
 * RET uint32_t - the cap the job is restricted to
 */
uint32_t powercap_get_job_cap(struct job_record *job_ptr, time_t when);

#endif /* !_POWERCAPPING_H */
