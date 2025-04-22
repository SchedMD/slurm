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

typedef struct {
	uint16_t cores_per_sock; /* IN - Count of cores per socket on this node
				  */
	bitstr_t *core_bitmap; /* IN/OUT - Identification of available cores on
				* this node */
	uint16_t cr_type;
	bool enforce_binding; /* IN - if true then only use GRES with direct
			       * access to cores */
	bitstr_t *gpu_spec_bitmap; /* IN - bitmap of reserved gpu cores */
	list_t *job_gres_list; /* IN - job's gres_list built by
				* gres_job_state_validate() */
	list_t *node_gres_list; /* IN - node's gres_list built by
				 * gres_node_config_validate() */
	uint32_t node_inx; /* IN - index of node to be evaluated */
	char *node_name; /* IN - name of the node (for logging) */
	resv_exc_t *resv_exc_ptr; /* IN - gres that can be included
				   * (gres_list_inc) or excluded (gres_list_exc)
				  */
	bitstr_t *req_sock_map; /* OUT - bitmap of specific requires sockets */
	uint32_t res_cores_per_gpu; /* IN - number of cores reserved for each
				     * gpu */
	uint16_t sockets; /* IN - Count of sockets on the node */
	list_t *sock_gres_list; /* OUT - list of sock_gres_t entries identifying
				 * what resources are available on each
				 * socket. Returns NULL if none available. Call
				 * FREE_NULL_LIST() to release memory. */
	uint32_t s_p_n; /* IN - Expected sockets_per_node */
	bool use_total_gres; /* IN - if set then consider all gres resources as
			      * available, and none are committed to running
			      * jobs */
} gres_sock_list_create_t;


/*
 * Determine how many cores on each socket of a node can be used by this job.
 *
 * core_bitmap, req_sock_map and sock_gres_list are the possibly altered from
 * this function. sock_gres_list needs to be freed. See gres_sock_list_create_t
 * declaration above.
 */
extern void gres_sock_list_create(gres_sock_list_create_t *create_args);

#endif /* _GRES_SCHED_H */
