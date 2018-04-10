/*****************************************************************************\
 *  select_cons_res.h
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#ifndef _CONS_RES_H
#define _CONS_RES_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_topology.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/slurmctld.h"

#include "src/slurmd/slurmd/slurmd.h"

/* a partition's per-row CPU allocation data */
struct part_row_data {
	bitstr_t *row_bitmap;		/* contains core bitmap for all jobs in
					 * this row */
	struct job_resources **job_list;/* List of jobs in this row */
	uint32_t job_list_size;		/* Size of job_list array */
	uint32_t num_jobs;		/* Number of occupied entries in job_list array */
};

/* partition CPU allocation data */
struct part_res_record {
	struct part_res_record *next;	/* Ptr to next part_res_record */
	uint16_t num_rows;		/* Number of elements in "row" array */
	struct part_record *part_ptr;   /* controller part record pointer */
	struct part_row_data *row;	/* array of rows containing jobs */
};

/* per-node resource data */
struct node_res_record {
	struct node_record *node_ptr;	/* ptr to the actual node */
	uint16_t cpus;			/* count of processors configured */
	uint16_t boards; 		/* count of boards configured */
	uint16_t sockets;		/* count of sockets configured */
	uint16_t cores;			/* count of cores configured */
	uint16_t threads;		/* count of hyperthreads per core */
	uint16_t vpus;			/* count of virtual cpus (hyperthreads)
					 * configured per core */
	uint64_t real_memory;		/* MB of real memory configured */
	uint64_t mem_spec_limit;	/* MB of specialized/system memory */
};

/* per-node resource usage record */
struct node_use_record {
	uint64_t alloc_memory;		/* real memory reserved by already
					 * scheduled jobs */
	List gres_list;			/* list of gres state info managed by 
					 * plugins */
	uint16_t node_state;		/* see node_cr_state comments */
};

extern bool     backfill_busy_nodes;
extern bool     have_dragonfly;
extern bool     pack_serial_at_end;
extern bool     preempt_by_part;
extern bool     preempt_by_qos;
extern uint64_t select_debug_flags;
extern uint16_t select_fast_schedule;
extern bool     spec_cores_first;
extern bool     topo_optional;

extern struct part_res_record *select_part_record;
extern struct node_res_record *select_node_record;
extern struct node_use_record *select_node_usage;

extern void cr_sort_part_rows(struct part_res_record *p_ptr);
extern uint32_t cr_get_coremap_offset(uint32_t node_index);
extern int cr_cpus_per_core(struct job_details *details, int node_inx);

#endif /* !_CONS_RES_H */
