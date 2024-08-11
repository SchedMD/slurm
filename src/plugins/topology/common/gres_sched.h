/*****************************************************************************\
 *  gres_sched.h - Scheduling functions used by topology with cons_tres
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _COMMON_TOPO_GRES_SCHED_H
#define _COMMON_TOPO_GRES_SCHED_H

#include "src/interfaces/gres.h"

/*
 * Given a list of sock_gres_t entries, return a string identifying the
 * count of each GRES available on this set of nodes
 * IN sock_gres_list - count of GRES available in this group of nodes
 * RET xfree the returned string
 */
extern char *gres_sched_str(list_t *sock_gres_list);

/*
 * Clear GRES allocation info for all job GRES at start of scheduling cycle
 * Return TRUE if any gres_per_job constraints to satisfy
 */
extern bool gres_sched_init(list_t *job_gres_list);

/*
 * Return TRUE if all gres_per_job specifications are satisfied
 */
extern bool gres_sched_test(list_t *job_gres_list, uint32_t job_id);

/*
 * Update a job's total_gres counter as we add a node to potential allocation
 * IN/OUT avail_cpus - CPUs currently available on this node
 * IN/OUT avail_core - Core bitmap of currently available cores on this node
 * IN/OUT avail_cores_per_sock - Number of cores per socket available
 * IN/OUT sock_gres_list - Per socket GRES availability on this node
 *			   (sock_gres_t). Updates total_cnt
 * IN job_gres_list - list of job's GRES requirements (gres_state_job_t)
 * IN res_cores_per_gpu - Number of restricted cores per gpu
 * IN sockets - Number of sockets on the node
 * IN cores_per_socket - Number of cores on each socket on the node
 * IN cpus_per_core - Number of threads per core on the node
 * IN cr_type - Allocation type (sockets, cores, etc.)
 */
extern bool gres_sched_add(uint16_t *avail_cpus,
			   bitstr_t *avail_core,
			   uint16_t *avail_cores_per_sock,
			   list_t *sock_gres_list,
			   list_t *job_gres_list,
			   uint16_t res_cores_per_gpu,
			   int sockets,
			   uint16_t cores_per_socket,
			   uint16_t cpus_per_core,
			   uint16_t cr_type,
			   uint16_t min_cpus,
			   int node_i);

/*
 * Create/update list GRES that can be made available on the specified node
 * IN/OUT consec_gres - list of sock_gres_t that can be made available on
 *			a set of nodes
 * IN job_gres_list - list of job's GRES requirements (gres_job_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_sched_consec(list_t **consec_gres, list_t *job_gres_list,
			      list_t *sock_gres_list);

/*
 * Determine if the additional sock_gres_list resources will result in
 * satisfying the job's gres_per_job constraints
 * IN job_gres_list - job's GRES requirements
 * IN sock_gres_list - available GRES in a set of nodes, data structure built
 *		       by gres_sched_consec()
 */
extern bool gres_sched_sufficient(list_t *job_gres_list, list_t *sock_gres_list);

#endif /* _COMMON_TOPO_GRES_SCHED_H */
