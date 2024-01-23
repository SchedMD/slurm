/*****************************************************************************\
 *  eval_nodes.h - Determine order of nodes for job.
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

#ifndef _COMMON_TOPO_EVAL_NODES_H
#define _COMMON_TOPO_EVAL_NODES_H

#include "common_topo.h"

/*
 * This is the heart of the selection process
 *
 * IN topo_eval->
 *  IN/OUT avail_core - available core bitmap
 *  OUT avail_cpus - How many cpus available
 *  IN/OUT avail_res_array - available resources on the node
 *  IN cr_type - allocation type (sockets, cores, etc.)
 *  IN enforce_binding - Enforce GPU Binding or not
 *  IN first_pass - First pass through common_topo_eval_nodes() or not
 *  IN job_ptr - pointer to the job requesting resources
 *  IN max_nodes - maximum number of nodes requested
 *  IN mc_ptr - job's GRES multi-core options
 *  IN min_nodes - minimum number of nodes required
 *  IN/OUT node_map - bitmap of available/selected nodes
 *  IN prefer_alloc_nodes - prefer use of already allocated nodes
 *  IN req_nodes - number of requested nodes
 *
 * RET SLURM_SUCCESS or an error code
 */
extern int eval_nodes(topology_eval_t *topo_eval);

#endif
