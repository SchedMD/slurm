/*****************************************************************************\
 *  slurm_step_layout.h - function to distribute tasks over nodes.
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after hostlist.h, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef _SLURM_STEP_LAYOUT_H
#define _SLURM_STEP_LAYOUT_H

#include <inttypes.h>

#include "slurm/slurm.h"

#include "src/common/hostlist.h"
#include "src/common/pack.h"

/*
 * slurm_step_layout_create - determine how many tasks of a job will be
 *                    run on each node. Distribution is influenced
 *                    by number of cpus on each host.
 * IN step_layout_req - information needed for task distibutionhostlist corresponding to task layout
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
extern slurm_step_layout_t *slurm_step_layout_create(
	slurm_step_layout_req_t *step_layout_req);

/*
 * fake_slurm_step_layout_create - used when you don't allocate a job from the
 *                    controller does not set up anything
 *                    that should really be used with a switch.
 *                    Or to really lay out tasks any any certain fashion.
 * IN tlist - hostlist corresponding to task layout
 * IN cpus_per_node - cpus per node NULL if no allocation
 * IN cpu_count_reps - how many nodes have same cpu count NULL if no allocation
 * IN node_cnt - number of nodes we have
 * IN task_cnt - number of tasks to distribute across these cpus 0
 *               if using cpus_per_node
 * IN protocol_version - version to be set in layout structure
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
extern slurm_step_layout_t *fake_slurm_step_layout_create(
	const char *tlist,
	uint16_t *cpus_per_node,
	uint32_t *cpu_count_reps,
	uint32_t node_cnt,
	uint32_t task_cnt,
	uint16_t protocol_version);

/* copys structure for step layout */
extern slurm_step_layout_t *slurm_step_layout_copy(
	slurm_step_layout_t *step_layout);

/* merge step_layout2 into step_layout1 */
extern void slurm_step_layout_merge(slurm_step_layout_t *step_layout1,
				    slurm_step_layout_t *step_layout2);

/* pack and unpack structure */
extern void pack_slurm_step_layout(slurm_step_layout_t *step_layout,
				   buf_t *buffer, uint16_t protocol_version);
extern int unpack_slurm_step_layout(slurm_step_layout_t **layout, buf_t *buffer,
				    uint16_t protocol_version);

/* destroys structure for step layout */
extern int slurm_step_layout_destroy(slurm_step_layout_t *step_layout);

/* get info from the structure */
extern int slurm_step_layout_host_id (slurm_step_layout_t *s, int taskid);
extern char *slurm_step_layout_host_name (slurm_step_layout_t *s, int hostid);

/*
 * Convert task_dist to string
 * IN task_dist - task distribution to convert to string
 * RET string (must xfree()) or NULL (on error)
 */
extern char *slurm_step_layout_type_name(task_dist_states_t task_dist);
#endif /* !_SLURM_STEP_LAYOUT_H */
