/*****************************************************************************\
 *  gres_sched.h - Scheduling functions used by cons_tres
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

#ifndef _CONS_TRES_GRES_SCHED_H
#define _CONS_TRES_GRES_SCHED_H

#include "src/common/gres.h"

/*
 * Given a List of sock_gres_t entries, return a string identifying the
 * count of each GRES available on this set of nodes
 * IN sock_gres_list - count of GRES available in this group of nodes
 * RET xfree the returned string
 */
extern char *gres_sched_str(List sock_gres_list);

/*
 * Clear GRES allocation info for all job GRES at start of scheduling cycle
 * Return TRUE if any gres_per_job constraints to satisfy
 */
extern bool gres_sched_init(List job_gres_list);

/*
 * Return TRUE if all gres_per_job specifications are satisfied
 */
extern bool gres_sched_test(List job_gres_list, uint32_t job_id);

/*
 * Update a job's total_gres counter as we add a node to potential allocation
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN/OUT avail_cpus - CPUs currently available on this node
 */
extern void gres_sched_add(List job_gres_list, List sock_gres_list,
			   uint16_t *avail_cpus);

/*
 * Create/update List GRES that can be made available on the specified node
 * IN/OUT consec_gres - List of sock_gres_t that can be made available on
 *			a set of nodes
 * IN job_gres_list - List of job's GRES requirements (gres_job_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_sched_consec(List *consec_gres, List job_gres_list,
			      List sock_gres_list);

/*
 * Determine if the additional sock_gres_list resources will result in
 * satisfying the job's gres_per_job constraints
 * IN job_gres_list - job's GRES requirements
 * IN sock_gres_list - available GRES in a set of nodes, data structure built
 *		       by gres_sched_consec()
 */
extern bool gres_sched_sufficient(List job_gres_list, List sock_gres_list);

/*
 * Determine how many cores on each socket of a node can be used by this job
 * IN job_gres_list   - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list  - node's gres_list built by gres_node_config_validate()
 * IN use_total_gres  - if set then consider all gres resources as available,
 *			and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores on this node
 * IN sockets         - Count of sockets on the node
 * IN cores_per_sock  - Count of cores per socket on this node
 * IN job_id          - job's ID (for logging)
 * IN node_name       - name of the node (for logging)
 * IN enforce_binding - if true then only use GRES with direct access to cores
 * IN s_p_n           - Expected sockets_per_node (NO_VAL if not limited)
 * OUT req_sock_map   - bitmap of specific requires sockets
 * IN user_id         - job's user ID
 * IN node_inx        - index of node to be evaluated
 * RET: List of sock_gres_t entries identifying what resources are available on
 *	each socket. Returns NULL if none available. Call FREE_NULL_LIST() to
 *	release memory.
 */
extern List gres_sched_create_sock_gres_list(
	List job_gres_list, List node_gres_list,
	bool use_total_gres, bitstr_t *core_bitmap,
	uint16_t sockets, uint16_t cores_per_sock,
	uint32_t job_id, char *node_name,
	bool enforce_binding, uint32_t s_p_n,
	bitstr_t **req_sock_map, uint32_t user_id,
	const uint32_t node_inx);

#endif /* _CONS_TRES_GRES_SCHED_H */
