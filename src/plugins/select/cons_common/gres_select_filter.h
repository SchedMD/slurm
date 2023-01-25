/*****************************************************************************\
 *  gres_select_filter.h - filters used in the select plugin
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Derived in large part from code previously in interfaces/gres.h
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

#ifndef _GRES_SELECT_FILTER_H
#define _GRES_SELECT_FILTER_H

#include "src/interfaces/gres.h"

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES) only for
 *	cons_res.
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_node_config_validate()
 * IN use_total_gres - if set then consider all GRES resources as available,
 *		       and none are committed to running jobs
 * IN/OUT core_bitmap - Identification of available cores
 *                      (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first cores
 * IN core_end_bit - index into core_bitmap for this node's last cores
 */
extern void gres_select_filter_cons_res(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *core_bitmap,
					int core_start_bit, int core_end_bit,
					char *node_name);

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list - list of sock_gres_t entries built by
 *                     gres_job_test2()
 * IN avail_mem - memory available for the job or NO_VAL64 (when no CR_MEMORY)
 * IN max_cpus - maximum CPUs available on this node (limited by specialized
 *               cores and partition CPUs-per-node)
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN core_bitmap - Identification of available cores on this node
 * IN sockets - Count of sockets on the node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN cpus_per_core - Count of CPUs per core on this node
 * IN sock_per_node - sockets requested by job per node or NO_VAL
 * IN task_per_node - tasks requested by job per node or NO_VAL16
 * IN cpus_per_task - Count of CPUs per task
 * IN whole_node - we are requesting the whole node or not
 * OUT avail_gpus - Count of available GPUs on this node
 * OUT near_gpus - Count of GPUs available on sockets with available CPUs
 * RET - 0 if job can use this node, -1 otherwise (some GRES limit prevents use)
 */
extern int gres_select_filter_remove_unusable(List sock_gres_list,
					      uint64_t avail_mem,
					      uint16_t max_cpus,
					      bool enforce_binding,
					      bitstr_t *core_bitmap,
					      uint16_t sockets,
					      uint16_t cores_per_sock,
					      uint16_t cpus_per_core,
					      uint32_t sock_per_node,
					      uint16_t task_per_node,
					      uint16_t cpus_per_task,
					      bool whole_node,
					      uint16_t *avail_gpus,
					      uint16_t *near_gpus);

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by gres_job_test2()
 * IN sockets - Count of sockets on the node
 * IN cores_per_socket - Count of cores per socket on the node
 * IN cpus_per_core - Count of CPUs per core on the node
 * IN avail_cpus - Count of available CPUs on the node, UPDATED
 * IN min_tasks_this_node - Minimum count of tasks that can be started on this
 *                          node, UPDATED
 * IN max_tasks_this_node - Maximum count of tasks that can be started on this
 *                          node or NO_VAL, UPDATED
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN first_pass - set if first scheduling attempt for this job, use
 *		   co-located GRES and cores if possible
 * IN avail_core - cores available on this node, UPDATED
 * IN node_name - name of the node
 */
extern void gres_select_filter_sock_core(gres_mc_data_t *mc_ptr,
					 List sock_gres_list,
					 uint16_t sockets,
					 uint16_t cores_per_socket,
					 uint16_t cpus_per_core,
					 uint16_t *avail_cpus,
					 uint32_t *min_tasks_this_node,
					 uint32_t *max_tasks_this_node,
					 uint32_t *min_cores_this_node,
					 int rem_nodes,
					 bool enforce_binding,
					 bool first_pass,
					 bitstr_t *avail_core,
					 char *node_name);

/*
 * Make final GRES selection for the job
 * sock_gres_list IN - per-socket GRES details, one record per allocated node
 * job_id IN - job ID for logging
 * job_res IN - job resource allocation
 * overcommit IN - job's ability to overcommit resources
 * tres_mc_ptr IN - job's multi-core options
 * RET SLURM_SUCCESS or error code
 */
extern int gres_select_filter_select_and_set(List *sock_gres_list, uint32_t job_id,
					     struct job_resources *job_res,
					     uint8_t overcommit,
					     gres_mc_data_t *tres_mc_ptr);

#endif /* _GRES_SELECT_FILTER_H */
