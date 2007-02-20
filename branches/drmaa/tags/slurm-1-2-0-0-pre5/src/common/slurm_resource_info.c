/*****************************************************************************\
 *  slurm_resource_info.c - Functions to determine number of available resources 
 *  $Id: slurm_resource_info.c,v 1.12 2006/10/04 21:52:24 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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
\*****************************************************************************/
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <sys/types.h>
#include "src/common/log.h"
#include <slurm/slurm.h>
#include "src/common/slurm_resource_info.h"

/*
 * slurm_get_avail_procs - Get the number of "available" cpus on a node
 *	given this number given the number of cpus_per_task and
 *	maximum sockets, cores, threads.  Note that the value of
 *	cpus is the lowest-level logical processor (LLLP).
 * IN mxsockets      - Job requested max sockets
 * IN mxcores        - Job requested max cores
 * IN mxthreads      - Job requested max threads
 * IN cpuspertask    - Job requested cpus per task
 * IN ntaskspernode  - number of tasks per node
 * IN ntaskspersocket- number of tasks per socket
 * IN ntaskspercore  - number of tasks per core
 * IN/OUT cpus       - Available cpu count
 * IN/OUT sockets    - Available socket count
 * IN/OUT cores      - Available core count
 * IN/OUT threads    - Available thread count
 * IN alloc_sockets  - Allocated socket count to other jobs
 * IN alloc_lps      - Allocated cpu count to other jobs
 * IN cr_type        - Consumable Resource type
 *
 * Note: used in both the select/{linear,cons_res} plugins.
 */
int slurm_get_avail_procs(const int mxsockets,
			  const int mxcores,
			  const int mxthreads,
			  const int cpuspertask,
			  const int ntaskspernode,
			  const int ntaskspersocket,
			  const int ntaskspercore,
			  int *cpus, 
			  int *sockets, 
			  int *cores, 
			  int *threads,
			  const int alloc_sockets,
			  const int *alloc_cores,
			  const int alloc_lps,
			  const select_type_plugin_info_t cr_type)
{
	int avail_cpus = 0, max_cpus = 0;
	int max_avail_cpus = INT_MAX;	/* for alloc_* accounting */
	int max_sockets   = mxsockets;
	int max_cores     = mxcores;
	int max_threads   = mxthreads;
	int cpus_per_task = cpuspertask;
	int i;

        /* pick defaults for any unspecified items */
	if (cpus_per_task <= 0)
		cpus_per_task = 1;
	if (max_sockets <= 0)
		max_sockets = INT_MAX;
	if (max_cores <= 0)
		max_cores = INT_MAX;
	if (max_threads <= 0)
		max_threads = INT_MAX;

	if (*threads <= 0)
	    	*threads = 1;
	if (*cores <= 0)
	    	*cores = 1;
	if (*sockets <= 0)
	    	*sockets = *cpus / *cores / *threads;
#if(0)
	info("get_avail_procs User_ sockets %d cores %d threads %d ",
			max_sockets, max_cores, max_threads);
	info("get_avail_procs HW_   sockets %d cores %d threads %d ",
			*sockets, *cores, *threads);
        info("get_avail_procs Ntask node    %d sockets %d core    %d ",
                        ntaskspernode, ntaskspersocket, ntaskspercore);
	info("get_avail_procs cr_type %d cpus %d Allocated sockets %d lps %d ",
			cr_type, *cpus, alloc_sockets, alloc_lps);
	if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
		for (i = 0; i < *sockets; i++)
			info("get_avail_procs alloc_cores[%d] = %d", i, alloc_cores[i]);
	}
#endif
	if ((*threads <= 0) || (*cores <= 0) || (*sockets <= 0))
		fatal(" ((threads <= 0) || (cores <= 0) || (sockets <= 0))");
		
	switch(cr_type) {
	/* For the following CR types, nodes have no notion of socket, core,
	   and thread.  Only one level of logical processors */ 
	case CR_CPU:
	case CR_CPU_MEMORY:
	case CR_MEMORY:
		switch(cr_type) { 
		case CR_CPU:
		case CR_CPU_MEMORY:
			*cpus -= alloc_lps;
			if (*cpus < 0) 
				error(" cons_res: *cpus < 0");
			break;
		default:
			break;
		}

		/*** compute an overall maximum cpu count honoring ntasks* ***/
		max_cpus  = *cpus;
		if (ntaskspernode > 0) {
			max_cpus = MIN(max_cpus, ntaskspernode);
		}
		break;
	/* For all other types, nodes contain sockets, cores, and threads */
	case CR_SOCKET:
	case CR_SOCKET_MEMORY:
	case CR_CORE:
	case CR_CORE_MEMORY:
	default:
		switch(cr_type) { 
		case CR_SOCKET:
		case CR_SOCKET_MEMORY:
			*sockets -= alloc_sockets; /* sockets count */
			if (*sockets < 0) 
				error(" cons_res: *sockets < 0");
			
			*cpus -= alloc_lps;
			if (*cpus < 0) 
				error(" cons_res: *cpus < 0");
			break;
		case CR_CORE:
		case CR_CORE_MEMORY:
			*cpus -= alloc_lps;
			if (*cpus < 0) 
				error(" cons_res: *cpus < 0");

			if (alloc_lps > 0) {
				max_avail_cpus = 0;
				for (i=0; i<*sockets; i++)
					max_avail_cpus += *cores - alloc_cores[i];
				max_avail_cpus *= *threads;
			}
			break;
		default:
			break;
		}

		/*** honor socket/core/thread maximums ***/
		*sockets = MIN(*sockets, max_sockets);
		*cores   = MIN(*cores,   max_cores);
		*threads = MIN(*threads, max_threads);
		
		/*** compute an overall maximum cpu count honoring ntasks* ***/
		max_cpus  = *threads;
		if (ntaskspercore > 0) {
			max_cpus = MIN(max_cpus, ntaskspercore);
		}
		max_cpus *= *cores;
		if (ntaskspersocket > 0) {
			max_cpus = MIN(max_cpus, ntaskspersocket);
		}
		max_cpus *= *sockets;
		if (ntaskspernode > 0) {
			max_cpus = MIN(max_cpus, ntaskspernode);
		}

		/*** honor any availability maximum ***/
		max_cpus = MIN(max_cpus, max_avail_cpus);
		break;
	}

	/*** factor cpus_per_task into max_cpus ***/
	max_cpus *= cpus_per_task; 

	/*** round down available based on cpus_per_task ***/
	avail_cpus = (*cpus / cpus_per_task) * cpus_per_task;
	
	avail_cpus = MIN(avail_cpus, max_cpus);

#if(0)
	info("get_avail_procs return cpus %d sockets %d cores %d threads %d ",
			*cpus, *sockets, *cores, *threads);
	info("get_avail_procs avail_cpus %d", avail_cpus);
#endif
	return(avail_cpus);
}

/*
 * slurm_sprint_cpu_bind_type
 *
 * Given a cpu_bind_type, report all flag settings in str
 * IN  - cpu_bind_type
 * OUT - str
 */
void slurm_sprint_cpu_bind_type(char *str, cpu_bind_type_t cpu_bind_type)
{
	if (!str)
		return;

	str[0] = '\0';

	if (cpu_bind_type & CPU_BIND_TO_THREADS)
		strcat(str, "threads,");
	if (cpu_bind_type & CPU_BIND_TO_CORES)
		strcat(str, "cores,");
	if (cpu_bind_type & CPU_BIND_TO_SOCKETS)
		strcat(str, "sockets,");
	if (cpu_bind_type & CPU_BIND_VERBOSE)
		strcat(str, "verbose,");
	if (cpu_bind_type & CPU_BIND_NONE)
		strcat(str, "none,");
	if (cpu_bind_type & CPU_BIND_RANK)
		strcat(str, "rank,");
	if (cpu_bind_type & CPU_BIND_MAP)
		strcat(str, "mapcpu,");
	if (cpu_bind_type & CPU_BIND_MASK)
		strcat(str, "maskcpu,");

	if (*str) {
		str[strlen(str)-1] = '\0';	/* remove trailing ',' */
	} else {
	    	strcat(str, "(null type)");	/* no bits set */
	}
}

/*
 * slurm_sprint_mem_bind_type
 *
 * Given a mem_bind_type, report all flag settings in str
 * IN  - mem_bind_type
 * OUT - str
 */
void slurm_sprint_mem_bind_type(char *str, mem_bind_type_t mem_bind_type)
{
	if (!str)
		return;

	str[0] = '\0';

	if (mem_bind_type & MEM_BIND_VERBOSE)
		strcat(str, "verbose,");
	if (mem_bind_type & MEM_BIND_NONE)
		strcat(str, "none,");
	if (mem_bind_type & MEM_BIND_RANK)
		strcat(str, "rank,");
	if (mem_bind_type & MEM_BIND_LOCAL)
		strcat(str, "local,");
	if (mem_bind_type & MEM_BIND_MAP)
		strcat(str, "mapmem,");
	if (mem_bind_type & MEM_BIND_MASK)
		strcat(str, "maskmem,");

	if (*str) {
		str[strlen(str)-1] = '\0';	/* remove trailing ',' */
	} else {
	    	strcat(str, "(null type)");	/* no bits set */
	}
}
