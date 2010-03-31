/*****************************************************************************\
 *  select_linear.h
 *****************************************************************************
 *  Copyright (C) 2006-2007 Hewlett-Packard Development Company, L.P.
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

#ifndef _SELECT_LINEAR_H
#define _SELECT_LINEAR_H

#include "src/common/slurm_topology.h"
#include "src/slurmctld/slurmctld.h"

/*
 * part_cr_record keeps track of the number of running jobs on
 * this node in this partition. SLURM allows a node to be
 * assigned to more than one partition. One or more partitions
 * may be configured to share the cores with more than one job.
 */

struct part_cr_record {
	struct part_record *part_ptr;	/* pointer to partition in slurmctld */
	uint16_t run_job_cnt;		/* number of running jobs on this node
					 * for this partition */
	uint32_t *run_job_ids;		/* job IDs for running jobs */
	uint16_t run_job_len;		/* length of run_job_ids array */
	uint16_t tot_job_cnt;		/* number of jobs allocated to this
					 * node for this partition */
	struct part_cr_record *next;	/* ptr to next part_cr_record */
};

/*
 * node_cr_record keeps track of the resources within a node which
 * have been reserved by already scheduled jobs.
 */
struct node_cr_record {
	struct part_cr_record *parts;	/* ptr to singly-linked part_cr_record
					 * list that contains alloc_core info */
	uint32_t alloc_memory;		/* real memory reserved by already
					 * scheduled jobs */
	uint16_t exclusive_cnt;		/* count of jobs exclusively allocated
					 * this node (from different
					 * partitions) */
};

#endif /* !_SELECT_LINEAR_H */
