/*****************************************************************************\
 *  gres_filter.h - Filters used on gres to determine order of nodes for job.
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

#ifndef _COMMON_TOPO_GRES_FILTER_H
#define _COMMON_TOPO_GRES_FILTER_H

#include "common_topo.h"

typedef struct {
	bitstr_t *avail_core; /* cores available on this node, UPDATED */
	uint16_t *avail_cpus; /* Count of available CPUs on the node, UPDATED */
	uint16_t cores_per_socket; /* Count of cores per socket on the node */
	uint16_t cpus_per_core; /* Count of CPUs per core on the node */
	uint16_t cr_type;
	bool enforce_binding; /* GRES must be co-allocated with cores */
	bool first_pass; /* set if first scheduling attempt for this job,
			  * use co-located GRES and cores if possible */
	bool has_cpus_per_gres;
	job_record_t *job_ptr; /* job's pointer */
	uint32_t *max_tasks_this_node; /* Max tasks that can start on this node
					* or NO_VAL, UPDATED */
	gres_mc_data_t *mc_ptr; /* job's multi-core specs, NO_VAL and INFINITE
				 * mapped to zero */
	uint32_t *min_cores_this_node;
	uint32_t *min_tasks_this_node; /* Min tasks that can start on this node,
					* UPDATED */
	int node_i;
	char *node_name; /* name of the node */
	int rem_nodes; /* node count left to allocate, including this node */
	uint16_t res_cores_per_gpu;
	uint16_t *res_cores_per_sock; /* internally set by function */
	bool *req_sock; /* Required socket */
	uint16_t sockets; /* Count of sockets on the node */
	int *socket_index; /* internally set by function - Socket indexes */
	uint32_t task_cnt_incr; /* internally set by function -
				 * original value of min_tasks_this_node */
	int tot_core_cnt; /* internally set by function */
} foreach_gres_filter_sock_core_args_t;

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN args->job_ptr - job's pointer
 * IN args->mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by
 *		       gres_sock_list_create()
 * IN args->sockets - Count of sockets on the node
 * IN args->cores_per_socket - Count of cores per socket on the node
 * IN args->cpus_per_core - Count of CPUs per core on the node
 * IN args->avail_cpus - Count of available CPUs on the node, UPDATED
 * IN args->min_tasks_this_node - Minimum count of tasks that can be started on
 *				  this node, UPDATED
 * IN args->max_tasks_this_node - Maximum count of tasks that can be started on
 *				  this node or NO_VAL, UPDATED
 * IN args->rem_nodes - desired additional node count to allocate,
 *			including this node
 * IN args->enforce_binding - GRES must be co-allocated with cores
 * IN args->first_pass - set if first scheduling attempt for this job, use
 *			 co-located GRES and cores if possible
 * IN args->avail_core - cores available on this node, UPDATED
 * IN args->node_name - name of the node
 */
extern void gres_filter_sock_core(list_t *sock_gres_list,
				  uint16_t **cores_per_sock_limit,
				  foreach_gres_filter_sock_core_args_t *args);

#endif
