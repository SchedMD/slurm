/*****************************************************************************\
 *  topology.h - Define topology plugin functions.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Copyright (C) 2014 Silicon Graphics International Corp. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _INTERFACES_TOPOLOGY_H
#define _INTERFACES_TOPOLOGY_H

#include "slurm/slurm.h"
#include "src/slurmctld/slurmctld.h"

#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"

typedef enum {
	TOPO_DATA_TOPOLOGY_PTR,
	TOPO_DATA_REC_CNT,
	TOPO_DATA_EXCLUSIVE_TOPO,
} topology_data_t;


typedef struct topology_eval {
	bitstr_t **avail_core; /* available core bitmap, UPDATED */
	uint16_t avail_cpus; /* How many cpus available, UPDATED */
	avail_res_t **avail_res_array; /* available resources on the node,
					* UPDATED */
	uint16_t cr_type; /* allocation type (sockets, cores, etc.) */
	bool enforce_binding; /* Enforce GPU Binding or not */
	int (*eval_nodes)(struct topology_eval *topo_eval);
	bool first_pass; /* First pass through eval_nodes() or not */
	bool gres_per_job; /* if gres_per_job was requested */
	job_record_t *job_ptr; /* pointer to the job requesting resources */
	uint32_t max_nodes; /* maximum number of nodes requested */
	gres_mc_data_t *mc_ptr; /* job's GRES multi-core options */
	uint32_t min_nodes; /* minimum number of nodes required */
	bitstr_t *node_map; /* bitmap of available/selected nodes, UPDATED */
	bool prefer_alloc_nodes; /* prefer use of already allocated nodes */
	uint32_t req_nodes; /* number of requested nodes */
	bool trump_others; /* If ->eval_nodes and set do not consider other
			    * algorithms. Only use ->eval_nodes. */
} topology_eval_t;

extern char *topo_conf;

/*****************************************************************************\
 *  Slurm topology functions
\*****************************************************************************/

/*
 * Initialize the topology plugin.
 *
 * Returns a Slurm errno.
 */
extern int topology_g_init(void);

/*
 * Terminate the topology plugin.
 *
 * Returns a Slurm errno.
 */
extern int topology_g_fini(void);

/*
 * Get the plugin ID number. Unique for each topology plugin type
 */
extern int topology_get_plugin_id(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * topology_g_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topology_g_build_config(void);

/*
 * topology_g_eval_nodes - Evaluate topology based on the topology plugin when
 *                         selecting nodes in the select plugin.
 */
extern int topology_g_eval_nodes(topology_eval_t *topo_eval);

extern int topology_g_whole_topo(bitstr_t *node_mask);

/*
 * topology_g_get_bitmap - Get bitmap of nodes in topo group
 *
 * IN name of topo group
 * RET bitmap of nodes from _record_table (do not free)
 */
extern bitstr_t *topology_g_get_bitmap(char *name);

/*
 * topology_g_generate_node_ranking  -  populate node_rank fields
 * NOTE: This operation is only supported by those topology plugins for
 *       which the node ordering between slurmd and slurmctld is invariant.
 */
extern bool topology_g_generate_node_ranking(void);

/*
 * topology_g_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 */
extern int topology_g_get_node_addr(char *node_name, char **addr,
				    char **pattern);

/*
 * topology_g_split_hostlist - logic to split an input hostlist into
 *                             a set of hostlists to forward to.
 *
 * IN: hl - hostlist_t * - List of every node to send message to
 *                         will be empty on return which is same
 *                         behavior as similar code replaced in
 *                         forward.c
 * OUT: sp_hl - hostlist_t *** - The array of hostlists that will be malloced
 * OUT: count - int * - The count of created hostlists
 * IN: tree_width - int - Max width of each branch on the tree.
 * RET: int - the number of levels opened in the tree, or SLURM_ERROR
 *
 * Note: Created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: The hostlist_t array will have to be xfree.
 */
extern int topology_g_split_hostlist(hostlist_t *hl,
				     hostlist_t ***sp_hl,
				     int *count,
				     uint16_t tree_width);

/* Get various information from the topology plugin
 * IN - type see topology_data_t
 * OUT data
 *     type = TOPO_DATA_TOPOLOGY_PTR - the system topology - Returned value must
 *                                     be freed using topology_g_topology_free.
 * RET         - slurm error code
 * NOTE: returned value must be freed using topology_g_topology_free
 */
extern int topology_g_get(topology_data_t type, void *data);

/* pack a mchine independent form system topology
 * OUT buffer  - buffer with node topology appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int topology_g_topology_pack(dynamic_plugin_data_t *topoinfo,
				    buf_t *buffer,
				    uint16_t protocol_version);

extern int topology_g_topology_print(dynamic_plugin_data_t *topoinfo,
				     char *nodes_list, char **out);

/* unpack a system topology from a buffer
 * OUT topoinfo - the system topology
 * IN  buffer  - buffer with system topology read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using topology_g_topology_free
 */
extern int topology_g_topology_unpack(dynamic_plugin_data_t **topoinfo,
				      buf_t *buffer,
				      uint16_t protocol_version);
/* free storage previously allocated for a system topology
 * IN jobinfo  - the system topology to be freed
 * RET         - slurm error code
 */
extern int topology_g_topology_free(dynamic_plugin_data_t *topoinfo);

/* Return fragmentation score of given bitmap
 * IN node_mask - aviabled nodes
 * RET: fragmentation
 */
extern uint32_t topology_g_get_fragmentation(bitstr_t *node_mask);
#endif
