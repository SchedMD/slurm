/*****************************************************************************\
 *  dist_tasks.c - function to distribute tasks over nodes.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *
 *  This file is patterned after hostlist.c, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#endif                /* HAVE_CONFIG_H */


#include <stdlib.h>

#include <slurm/slurm_errno.h>

#include "src/common/dist_tasks.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"


/* 
 * distribute_tasks - determine how many tasks of a job will be run on each.
 *                    node. Distribution is influenced by number of cpus on
 *                    each host. 
 * IN mlist - hostlist corresponding to cpu arrays
 * IN num_cpu_groups - elements in below cpu arrays
 * IN cpus_per_node - cpus per node
 * IN cpu_count_reps - how many nodes have same cpu count
 * IN tlist - hostlist of nodes on which to distribute tasks
 * IN num_tasks - number of tasks to distribute across these cpus
 * RET a pointer to an integer array listing task counts per node
 * NOTE: allocates memory that should be xfreed by caller
 */
int *distribute_tasks(const char *mlist, uint16_t num_cpu_groups,
			uint32_t *cpus_per_node, uint32_t *cpu_count_reps,
			const char *tlist, uint32_t num_tasks) 
{
	hostlist_t master_hl = NULL, task_hl = NULL;
	int i, index, count, hostid, nnodes, ncpus, *cpus, *ntask = NULL;
	char *this_node_name;
	
	if (!tlist || num_tasks == 0)
		return NULL;

	if ((master_hl = hostlist_create(mlist)) == NULL)
		fatal("hostlist_create error for %s: %m", mlist);

	if ((task_hl = hostlist_create(tlist)) == NULL)
		fatal("hostlist_create error for %s: %m", tlist);

	nnodes = hostlist_count(task_hl);
	ntask = (int *) xmalloc(sizeof(int *) * nnodes);
	if (!ntask) {
		hostlist_destroy(master_hl);
		hostlist_destroy(task_hl);
		slurm_seterrno(ENOMEM);
		return NULL;
	}

	index = 0;
	count = 1;
	i = 0;
	ncpus = 0;
	while ((this_node_name = hostlist_shift(master_hl))) {

		if (hostlist_find(task_hl, this_node_name) >= 0) {
			if (i >= nnodes)
				fatal("Internal error: duplicate nodes? (%s)(%s):%m", mlist, tlist);
			ntask[i++] = cpus_per_node[index];
			ncpus += cpus_per_node[index];
		}

		if (++count > cpu_count_reps[index]) {
			index++;
			count = 1;
		}
		free(this_node_name);
	}
	hostlist_destroy(master_hl);
	hostlist_destroy(task_hl);

	if (num_tasks >= ncpus) {
		/*
		 * Evenly overcommit tasks over the hosts
		 */
		int extra = num_tasks - ncpus;
		int add_to_all = extra / nnodes;
		int subset = extra % nnodes;
		for (i = 0; i < nnodes; i++) {
			ntask[i] += add_to_all;
			if (i < subset)
				ntask[i]++;
		}
		return ntask;
	}

	/*
	 * NOTE: num_tasks is less than ncpus here.
	 *
	 * In a cyclic fashion, place tasks on the nodes as permitted
	 * by the cpu constraints.
	 */
	cpus = ntask;
	ntask = (int *) xmalloc(sizeof(int *) * nnodes);
	if (!ntask) {
		slurm_seterrno(ENOMEM);
		xfree(cpus);
		return NULL;
	}

	for (i = 0; i < nnodes; i++)
		ntask[i] = 0;

	hostid = 0;
	for (i = 0; i < num_tasks;) {
		if (ntask[hostid] < cpus[hostid]) {
			ntask[hostid]++;
			i++;
		}
		if (++hostid >= nnodes)
			hostid = 0;
	}
	xfree(cpus);
	return ntask;
}
