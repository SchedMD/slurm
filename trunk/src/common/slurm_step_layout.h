/*****************************************************************************\
 *  slurm_step_layout.c - function to distribute tasks over nodes.
 *  $Id: slurm.hp.elan.patch,v 1.1 2005/07/28 04:08:19 cholmes Exp $
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 *  This file is patterned after hostlist.h, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef _SLURM_STEP_LAYOUT_H
#define _SLURM_STEP_LAYOUT_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include "src/common/hostlist.h"
#include "src/common/pack.h"

/* 
 * slurm_step_layout_create - determine how many tasks of a job will be 
 *                    run on each node. Distribution is influenced 
 *                    by number of cpus on each host. 
 * IN tlist - hostlist corresponding to task layout
 * IN cpus_per_node - cpus per node
 * IN cpu_count_reps - how many nodes have same cpu count
 * IN node_cnt - number of nodes we have 
 * IN task_cnt - number of tasks to distribute across these cpus
 * IN cpus_per_task - number of cpus per task
 * IN task_dist - type of distribution we are using 
 * IN plane_size - plane size (only needed for the plane distribution)
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
extern slurm_step_layout_t *slurm_step_layout_create(const char *tlist,
						     uint16_t *cpus_per_node, 
						     uint32_t *cpu_count_reps,
						     uint32_t node_cnt, 
						     uint32_t task_cnt,
						     uint16_t cpus_per_task,
						     uint16_t task_dist,
						     uint16_t plane_size);

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
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
extern slurm_step_layout_t *fake_slurm_step_layout_create(
	const char *tlist,
	uint16_t *cpus_per_node, 
	uint32_t *cpu_count_reps,
	uint32_t node_cnt, 
	uint32_t task_cnt);

/* copys structure for step layout */
extern slurm_step_layout_t *slurm_step_layout_copy(
	slurm_step_layout_t *step_layout);

/* pack and unpack structure */
extern void pack_slurm_step_layout(slurm_step_layout_t *step_layout, 
				   Buf buffer);
extern int unpack_slurm_step_layout(slurm_step_layout_t **layout, Buf buffer);

/* destroys structure for step layout */
extern int slurm_step_layout_destroy(slurm_step_layout_t *step_layout);

/* get info from the structure */
extern int slurm_step_layout_host_id (slurm_step_layout_t *s, int taskid);
extern char *slurm_step_layout_host_name (slurm_step_layout_t *s, int hostid);

extern char *slurm_step_layout_type_name(task_dist_states_t task_dist);
#endif /* !_SLURM_STEP_LAYOUT_H */
