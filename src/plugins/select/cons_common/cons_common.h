/*****************************************************************************\
 *  cons_common.h - Common function interface for the select/cons_* plugins
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

#ifndef _CONS_COMMON_H
#define _CONS_COMMON_H

#include "src/slurmctld/slurmctld.h"

/* partition core allocation bitmap arrays (1 bitmap per node) */
struct part_res_record {
	struct part_res_record *next; /* Ptr to next part_res_record */
	uint16_t num_rows;	      /* Number of elements in "row" array */
	struct part_record *part_ptr; /* controller part record pointer */
	struct part_row_data *row;    /* array of rows containing jobs */
};

/* per-node resource data */
struct node_res_record {
	uint16_t boards; 	      /* count of boards configured */
	uint16_t cores;		      /* count of cores per socket configured */
	uint16_t cpus;		      /* count of logical processors
				       * configured */
	uint32_t cume_cores;	      /* total cores for all nodes through us */
	uint64_t mem_spec_limit;      /* MB of specialized/system memory */
	struct node_record *node_ptr; /* ptr to the actual node */
	uint64_t real_memory;	      /* MB of real memory configured */
	uint16_t sockets;	      /* count of sockets per board configured*/
	uint16_t threads;	      /* count of hyperthreads per core */
	uint16_t tot_cores;	      /* total cores per node */
	uint16_t tot_sockets;	      /* total sockets per node */
	uint16_t vpus;		      /* count of virtual processors configure
				       * this could be the physical threads
				       * count or could be the core count if
				       * the node's cpu count matches the
				       * core count */
};

/* per-node resource usage record */
struct node_use_record {
	uint64_t alloc_memory;	      /* real memory reserved by already
				       * scheduled jobs */
	List gres_list;		      /* list of gres_node_state_t records as
				       * defined in in src/common/gres.h.
				       * Local data used only in state copy
				       * to emulate future node state */
	uint16_t node_state;	      /* see node_cr_state comments */
};

/* Global common variables */
extern bool     backfill_busy_nodes;
extern int      bf_window_scale;
extern int      core_array_size;
extern uint16_t cr_type;
extern bool     gang_mode;
extern bool     have_dragonfly;
extern bool     pack_serial_at_end;
extern const uint32_t plugin_id;
extern const char *plugin_type;
extern bool     preempt_by_part;
extern bool     preempt_by_qos;
extern uint16_t priority_flags;
extern uint64_t select_debug_flags;
extern uint16_t select_fast_schedule;
extern int      select_node_cnt;
extern bool     spec_cores_first;
extern bool     topo_optional;

extern struct part_res_record *select_part_record;
extern struct node_res_record *select_node_record;
extern struct node_use_record *select_node_usage;


#endif /* _CONS_COMMON_H */
