/*****************************************************************************\
 *  switch_record.h - Determine order of nodes for job using tree algo.
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

#ifndef _TOPO_TREE_SWITCH_RECORD_H
#define _TOPO_TREE_SWITCH_RECORD_H

#include "../common/common_topo.h"

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

#define SWITCH_NO_PARENT 0xffff

typedef struct {
	switch_record_t *switch_table; /* ptr to switch records */
	int switch_count; /* size of switch_table */
	int switch_levels; /* number of switch levels     */
} tree_context_t;

/* Free all memory associated with switch_table structure */
extern void switch_record_table_destroy(tree_context_t *ctx);

extern int switch_record_validate(topology_ctx_t *tctx);

extern void switch_record_update_block_config(topology_ctx_t *tctx, int idx);

extern int switch_record_add_switch(topology_ctx_t *tctx, char *name,
				    int parent);

/* Return the index of a given switch name or -1 if not found */
extern int switch_record_get_switch_inx(const char *name, tree_context_t *ctx);

#endif
