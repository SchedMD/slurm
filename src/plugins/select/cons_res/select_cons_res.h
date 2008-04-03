/*****************************************************************************\
 *  select_cons_res.h 
 *
 *  $Id: select_cons_res.h,v 1.3 2006/10/31 20:01:38 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"

#include "src/slurmd/slurmd/slurmd.h"

/* part_cr_record keeps track of the allocated cores of a node that
 * has been assigned to a partition. SLURM allows a node to be
 * assigned to more than one partition. One or more partitions
 * may be configured to share the cores with more than one job.
 */
struct part_cr_record {
	struct part_record *part_ptr;	/* ptr to slurmctld partition record */
	uint16_t *alloc_cores;		/* core count per socket reserved by
					 * already scheduled jobs */
	uint16_t num_rows;		/* number of rows in alloc_cores. The
					 * length of alloc_cores is
					 * num_sockets * num_rows. */
	struct part_cr_record *next;	/* ptr to next part_cr_record */
};

/*
 * node_cr_record.node_state assists with the unique state of each node.
 * NOTES:
 * - If node is in use by Shared=NO part, some CPUs/memory may be available
 * - Caution with NODE_CR_AVAILABLE: a Sharing partition could be full!!
 */
enum node_cr_state {
	NODE_CR_RESERVED, /* node is NOT available for use by any other jobs */
	NODE_CR_ONE_ROW,  /* node is in use by Shared=NO part */
	NODE_CR_AVAILABLE /* The node may be IDLE or IN USE by Sharing part(s)*/
};

/* node_cr_record keeps track of the resources within a node which 
 * have been reserved by already scheduled jobs. 
 *
 * NOTE: The locations of these entries are synchronized with the 
 * job records in slurmctld (entry X in both tables are the same).
 */
struct node_cr_record {
	struct node_record *node_ptr;	/* ptr to the actual node */
	uint16_t cpus;			/* count of processors configured */
	uint16_t sockets;		/* count of sockets configured */
	uint16_t cores;			/* count of cores configured */
	uint16_t threads;		/* count of threads configured */
	uint32_t real_memory;		/* MB of real memory configured */
	enum node_cr_state node_state;	/* see node_cr_state comments */
	struct part_cr_record *parts;	/* ptr to singly-linked part_cr_record
					 * list that contains alloc_core info */
	uint32_t alloc_memory;		/* real memory reserved by already
					 * scheduled jobs */
};
extern struct node_cr_record *select_node_ptr;
extern uint16_t select_fast_schedule;

/*** NOTE: If any changes are made here, the following data structure has
 ***       persistent state which is maintained by select_cons_res.c:
 ***		select_p_state_save
 ***		select_p_state_restore
 *** 
 *** as well as tracked by version control
 ***		select_cons_res.c:pstate_version
 *** which should be incremented if any changes are made.
 **/
struct select_cr_job {
	/* Information preserved across reboots */
	uint32_t job_id;	/* job ID, default set by SLURM        */
	enum node_cr_state node_req;    /* see node_cr_state comments */
	uint32_t nprocs;	/* --nprocs=n,      -n n               */
	uint32_t nhosts;	/* number of hosts allocated to job    */
	uint16_t *cpus;		/* number of processors on each host,
				 * if using Moab scheduler (sched/wiki2)
				 * then this will be initialized to the
				 * number of CPUs desired on the node	*/
	uint16_t *alloc_cpus;	/* number of allocated threads/cpus on
				 * each host */
	uint16_t *num_sockets;	/* number of sockets in alloc_cores[node] */
	uint16_t **alloc_cores;	/* number of allocated cores on each
				 * host */
	uint32_t *alloc_memory;	/* number of allocated MB of real
				 * memory on each host */
	uint16_t *node_offset;	/* the node_cr_record->alloc_cores row to
				 * which this job was assigned */

	/* Information re-established after reboot */
	struct job_record *job_ptr;	/* pointer to slurmctld job record */
	uint16_t state;		/* job state information               */
	bitstr_t *node_bitmap;	/* bitmap of nodes allocated to job, 
				 * NOTE: The node_bitmap in slurmctld's job
				 * structure clears bits as on completion.
				 * This bitmap is persistent through lifetime
				 * of the job. */
};

struct node_cr_record * find_cr_node_record (const char *name);

/* Find a partition record based upon pointer to slurmctld record */
struct part_cr_record *get_cr_part_ptr(struct node_cr_record *this_node,
				      struct part_record *part_ptr);

void get_resources_this_node(uint16_t *cpus, 
			     uint16_t *sockets, 
			     uint16_t *cores,
			     uint16_t *threads, 
			     struct node_cr_record *this_cr_node,
			     uint32_t jobid);

extern struct multi_core_data * create_default_mc(void);

#endif /* !_CONS_RES_H */
