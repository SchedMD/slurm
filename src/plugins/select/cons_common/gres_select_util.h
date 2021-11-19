/*****************************************************************************\
 *  gres_select_util.h - filters used in the select plugin
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Derived in large part from code previously in common/gres.h
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

#ifndef _GRES_SELECT_UTIL_H
#define _GRES_SELECT_UTIL_H

#include "src/common/gres.h"

/*
 * Set job default parameters in a given element of a list
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * IN gres_name - name of gres, apply defaults to all elements (e.g. updates to
 *		  gres_name="gpu" would apply to "gpu:tesla", "gpu:volta", etc.)
 * IN cpu_per_gpu - value to set as default
 * IN mem_per_gpu - value to set as default
 * OUT *cpus_per_tres - CpusPerTres string displayed by scontrol show job
 * OUT *mem_per_tres - MemPerTres string displayed by scontrol show job
 * IN/OUT *cpus_per_task - Increased if cpu_per_gpu * gres_per_task is more than
 *                         *cpus_per_task
 */
extern void gres_select_util_job_set_defs(List job_gres_list,
					  char *gres_name,
					  uint64_t cpu_per_gpu,
					  uint64_t mem_per_gpu,
					  char **cpus_per_tres,
					  char **mem_per_tres,
					  uint16_t *cpus_per_task);

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request on one node
 * sockets_per_node IN - count of sockets per node in job allocation
 * tasks_per_node IN - count of tasks per node in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_select_util_job_min_cpu_node(uint32_t sockets_per_node,
					     uint32_t tasks_per_node,
					     List job_gres_list);

/*
 * Determine the minimum number of tasks required to satisfy the job's GRES
 *	request (based upon total GRES times ntasks_per_tres value). If
 *	ntasks_per_tres is not specified, returns 0.
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * ntasks_per_tres IN - # of tasks per GPU
 * gres_name IN - (optional) Filter GRES by name. If NULL, check all GRES
 * job_gres_list IN - job GRES specification
 * RET count of required tasks for the job
 */
extern int gres_select_util_job_min_tasks(uint32_t node_count,
					  uint32_t sockets_per_node,
					  uint16_t ntasks_per_tres,
					  char *gres_name,
					  List job_gres_list);

/*
 * Set per-node memory limits based upon GRES assignments
 * RET TRUE if mem-per-tres specification used to set memory limits
 */
extern bool gres_select_util_job_mem_set(List job_gres_list,
					 job_resources_t *job_res);

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request (based upon total GRES times cpus_per_gres value)
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * task_count IN - count of tasks in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_select_util_job_min_cpus(uint32_t node_count,
					 uint32_t sockets_per_node,
					 uint32_t task_count,
					 List job_gres_list);

/*
 * Determine if the job GRES specification includes a mem-per-tres specification
 * RET largest mem-per-tres specification found
 */
extern uint64_t gres_select_util_job_mem_max(List job_gres_list);

/*
 * Determine if job GRES specification includes a tres-per-task specification
 * RET TRUE if any GRES requested by the job include a tres-per-task option
 */
extern bool gres_select_util_job_tres_per_task(List job_gres_list);

/*
 * Return the maximum number of tasks that can be started on a node with
 * sock_gres_list (per-socket GRES details for some node)
 */
extern uint32_t gres_select_util_get_task_limit(List sock_gres_list);

/*
 * Create a (partial) copy of a job's gres state accumlating the gres_per_*
 * requirements to accuratly calculate cpus_per_gres
 * IN gres_list - List of Gres records
 * RET The copy of list or NULL on failure
 */
extern List gres_select_util_create_list_req_accum(List gres_list);

#endif /* _GRES_SELECT_UTIL_H */
