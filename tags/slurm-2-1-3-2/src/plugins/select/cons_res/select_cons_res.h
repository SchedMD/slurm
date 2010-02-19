/*****************************************************************************\
 *  select_cons_res.h
 *
 *  $Id: select_cons_res.h,v 1.3 2006/10/31 20:01:38 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _CONS_RES_H
#define _CONS_RES_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_topology.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/slurmctld.h"

#include "src/slurmd/slurmd/slurmd.h"

/*
 * node_res_record.node_state assists with the unique state of each node.
 * When a job is allocated, these flags provide protection for nodes in a
 * Shared=NO or Shared=EXCLUSIVE partition from other jobs.
 *
 * NOTES:
 * - If node is in use by Shared=NO part, some CPUs/memory may be available
 * - Caution with NODE_CR_AVAILABLE: a Sharing partition could be full.
 *
 * - these values are staggered so that they can be incremented as multiple
 * jobs are allocated to each node. This is needed to be able to support
 * preemption, which can override these protections.
 */
enum node_cr_state {
	NODE_CR_AVAILABLE = 0,  /* The node may be IDLE or IN USE (shared) */
	NODE_CR_ONE_ROW = 1,    /* node is in use by Shared=NO part */
	NODE_CR_RESERVED = 100, /* node is in use by Shared=EXCLUSIVE part */
};

/* a partition's per-row CPU allocation data */
struct part_row_data {
	bitstr_t *row_bitmap;		/* contains all jobs for this row */
	uint32_t num_jobs;		/* Number of jobs in this row */
	struct job_resources **job_list;/* List of jobs in this row */
	uint32_t job_list_size;		/* Size of job_list array */
};

/* partition CPU allocation data */
struct part_res_record {
	struct part_res_record *next;	/* Ptr to next part_res_record */
	uint16_t num_rows;		/* Number of row_bitmaps */
	struct part_record *part_ptr;   /* controller part record pointer */
	struct part_row_data *row;	/* array of rows containing jobs */
};

/* per-node resource data */
struct node_res_record {
	struct node_record *node_ptr;	/* ptr to the actual node */
	uint16_t cpus;			/* count of processors configured */
	uint16_t sockets;		/* count of sockets configured */
	uint16_t cores;			/* count of cores configured */
	uint16_t vpus;			/* count of virtual cpus (hyperthreads)
					 * configured per core */
	uint32_t real_memory;		/* MB of real memory configured */
};

/* per-node resource usage record */
struct node_use_record {
	uint16_t node_state;		/* see node_cr_state comments */
	uint32_t alloc_memory;		/* real memory reserved by already
					 * scheduled jobs */
};


extern uint16_t select_fast_schedule;

extern struct part_res_record *select_part_record;
extern struct node_res_record *select_node_record;
extern struct node_use_record *select_node_usage;

extern void cr_sort_part_rows(struct part_res_record *p_ptr);
extern uint32_t cr_get_coremap_offset(uint32_t node_index);
extern uint32_t cr_get_node_num_cores(uint32_t node_index);

extern bool cr_preemption_enabled(void);
extern bool cr_preemption_killing(void);

#endif /* !_CONS_RES_H */
