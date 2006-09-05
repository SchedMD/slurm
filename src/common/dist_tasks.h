/*****************************************************************************\
 *  dist_tasks.c - function to distribute tasks over nodes.
 *  $Id: slurm.hp.elan.patch,v 1.1 2005/07/28 04:08:19 cholmes Exp $
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
 *  UCRL-CODE-217948.
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
 *
 *  This file is patterned after hostlist.h, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifndef _DIST_TASKS_H
#define _DIST_TASKS_H

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

typedef struct slurm_step_layout {
	char *alloc_nodes;
	char *step_nodes;
	char **host;		/* name for each host */

	uint32_t *cpus_per_node;
	uint32_t *cpu_count_reps;
	uint32_t *cpus;		/* count of processors on each host */
	uint32_t *tasks;	/* number of tasks on each host */
	
	uint32_t **tids;	/* host id => task id mapping */
	uint32_t *hostids;      /* task id => host id mapping */
	
	uint32_t num_hosts;	/* node count */
	uint32_t num_tasks;	/* number of tasks to execute */
	uint16_t task_dist;	/* see enum task_dist_state */

	hostlist_t hl;
} slurm_step_layout_t;

/* 
 * distribute_tasks - determine how many tasks of a job will be run on each.
 *                    node. Distribution is influenced by number of cpus on
 *                    each host. 
 * IN mlist - hostlist corresponding to cpu arrays
 * IN num_cpu_groups - elements in below cpu arrays
 * IN cpus_per_node - cpus per node
 * IN cpu_count_reps - how many nodes have same cpu count
 * IN tlist - hostlist of nodes on which to distribute tasks
 *               (assumed to be a subset of masterlist)
 * IN num_tasks - number of tasks to distribute across these cpus
 * RET a pointer to an integer array listing task counts per node
 * NOTE: allocates memory that should be xfreed by caller
 */
uint32_t * distribute_tasks(const char *mlist,
		       uint16_t num_cpu_groups,
		       uint32_t *cpus_per_node,
		       uint32_t *cpu_count_reps,
		       const char *tlist,
		       uint32_t num_tasks);

/* creates structure for step layout */
extern slurm_step_layout_t *step_layout_create(
	resource_allocation_response_msg_t *alloc_resp,
	job_step_create_response_msg_t *step_resp,
	job_step_create_request_msg_t *step_req);
/* destroys structure for step layout */
extern int step_layout_destroy(slurm_step_layout_t *step_layout);
/* build maps for task layout on nodes */
extern int task_layout(slurm_step_layout_t *step_layout);

extern int step_layout_host_id (slurm_step_layout_t *s, int taskid);

extern char * step_layout_host_name (slurm_step_layout_t *s, int hostid);
 
#endif /* !_DIST_TASKS_H */
