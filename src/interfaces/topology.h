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

/*****************************************************************************\
 *  SWITCH topology data structures
 *  defined here but is really tree plugin related
\*****************************************************************************/
typedef struct {
	int level;			/* level in hierarchy, leaf=0 */
	uint32_t link_speed;		/* link speed, arbitrary units */
	char *name;			/* switch name */
	bitstr_t *node_bitmap;		/* bitmap of all nodes descended from
					 * this switch */
	char *nodes;			/* name if direct descendant nodes */
	uint16_t  num_desc_switches;	/* number of descendant switches */
	uint16_t  num_switches;		/* number of direct descendant
					   switches */
	uint16_t  parent;		/* index of parent switch */
	char *switches;			/* name of direct descendant switches */
	uint32_t *switches_dist;
	uint16_t *switch_desc_index;	/* indexes of child descendant
					 * switches */
	uint16_t *switch_index;		/* indexes of child direct descendant
					   switches */
} switch_record_t;

extern switch_record_t *switch_record_table;  /* ptr to switch records */
extern int switch_record_cnt;		/* size of switch_record_table */
extern int switch_levels;               /* number of switch levels     */
extern char *topo_conf;

/*****************************************************************************\
 * defined here but is really block plugin related
\*****************************************************************************/
typedef struct {
	int level;
	char *name;			/* switch name */
	bitstr_t *node_bitmap;		/* bitmap of all nodes descended from
					 * this block */
	char *nodes;			/* name if direct descendant nodes */
	uint16_t block_index;
} block_record_t;


extern bitstr_t *blocks_nodes_bitmap;	/* nodes on any bblock */
extern block_record_t *block_record_table;  /* ptr to block records */
extern uint16_t bblock_node_cnt;
extern bitstr_t *block_levels;
extern int block_record_cnt;

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
 * RET: SLURM_SUCCESS - int
 *
 * Note: Created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: The hostlist_t array will have to be xfree.
 */
extern int topology_g_split_hostlist(hostlist_t *hl,
				     hostlist_t ***sp_hl,
				     int *count,
				     uint16_t tree_width);

/* unpack a system topology from a buffer
 * OUT topoinfo - the system topology
 * RET         - slurm error code
 * NOTE: returned value must be freed using topology_g_topology_free
 */
extern int topology_g_topology_get(dynamic_plugin_data_t **topoinfo);

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

#endif
