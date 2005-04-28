/*****************************************************************************\
 *  select_plugin.h - Define node selection plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
\*****************************************************************************/

#ifndef __SELECT_PLUGIN_API_H__
#define __SELECT_PLUGIN_API_H__

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(void);

/*
 * Save any global state information
 */
extern int select_g_state_save(char *dir_name);

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 */
extern int select_g_state_restore(char *dir_name);

/*
 * Note re/initialization of node record data structure
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt);

/*
 * Note re/initialization of partition record data structure
 */
extern int select_g_part_init(List part_list);

/*
 * Select the "best" nodes for given job from those available
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
	int min_nodes, int max_nodes);

/*
 * Note initiation of job is about to begin
 */
extern int select_g_job_init(struct job_record *job_ptr);

/*
 * Note termination of job is starting
 */
extern int select_g_job_fini(struct job_record *job_ptr);

#endif /*__SELECT_PLUGIN_API_H__*/
