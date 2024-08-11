/*****************************************************************************\
 *  gres_sock_list.h - Create Scheduling functions used by topology with cons_tres
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

#ifndef _GRES_SCHED_H
#define _GRES_SCHED_H

#include "src/interfaces/gres.h"

/*
 * Determine how many cores on each socket of a node can be used by this job
 * IN job_gres_list   - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list  - node's gres_list built by gres_node_config_validate()
 * IN resv_exc_ptr - gres that can be included (gres_list_inc)
 *                   or excluded (gres_list_exc)
 * IN use_total_gres  - if set then consider all gres resources as available,
 *			and none are committed to running jobs
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
 * IN gpu_spec_bitmap - bitmap of reserved gpu cores
 * IN res_cores_per_gpu - number of cores reserved for each gpu
 * IN sockets_per_node - number of requested sockets per node
 * RET: list of sock_gres_t entries identifying what resources are available on
 *	each socket. Returns NULL if none available. Call FREE_NULL_LIST() to
 *	release memory.
 */
extern list_t *gres_sock_list_create(
	list_t *job_gres_list, list_t *node_gres_list,
	resv_exc_t *resv_exc_ptr,
	bool use_total_gres, bitstr_t *core_bitmap,
	uint16_t sockets, uint16_t cores_per_sock,
	uint32_t job_id, char *node_name,
	bool enforce_binding, uint32_t s_p_n,
	bitstr_t **req_sock_map, uint32_t user_id,
	const uint32_t node_inx, bitstr_t *gpu_spec_bitmap,
	uint32_t res_cores_per_gpu, uint16_t cr_type);

#endif /* _GRES_SCHED_H */
