/*****************************************************************************\
 *  gres_filter.h - Filters used on gres to determine order of nodes for job.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC
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

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN job_ptr - job's pointer
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by
 *	gres_sched_create_sock_gres_list()
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
extern void gres_filter_sock_core(job_record_t *job_ptr,
				  gres_mc_data_t *mc_ptr,
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
				  char *node_name,
				  uint16_t cr_type);

#endif
