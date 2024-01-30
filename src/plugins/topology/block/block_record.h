/*****************************************************************************\
 *  block_record.h - Determine order of nodes for job using block algo.
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

#ifndef _TOPO_TREE_BLOCK_RECORD_H
#define _TOPO_TREE_BLOCK_RECORD_H

#include "../common/common_topo.h"

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

/* Free all memory associated with block_record_table structure */
extern void block_record_table_destroy(void);

extern void block_record_validate(void);

#endif
