/*****************************************************************************\
 *  node_data.h - Functions for structures dealing with nodes unique to
 *                the select plugin.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Derived in large part from select/cons_[res|tres] plugins
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

#ifndef _CONS_COMMON_NODE_DATA_H
#define _CONS_COMMON_NODE_DATA_H

/* per-node resource usage record */
typedef struct {
	uint64_t alloc_memory;	      /* real memory reserved by already
				       * scheduled jobs */
	List gres_list;		      /* list of gres_node_state_t records as
				       * defined in in src/common/gres.h.
				       * Local data used only in state copy
				       * to emulate future node state */
	uint16_t node_state;	      /* see node_cr_state comments */
} node_use_record_t;

extern node_use_record_t *select_node_usage;

/* Delete the given select_node_usage array */
extern void node_data_destroy(node_use_record_t *node_usage);

extern void node_data_dump(void);

extern node_use_record_t *node_data_dup_use(node_use_record_t *orig_ptr,
					    bitstr_t *node_map);

#endif /*_CONS_COMMON_NODE_DATA_H */
