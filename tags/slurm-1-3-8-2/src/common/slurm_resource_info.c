/*****************************************************************************\
 *  slurm_resource_info.c - Functions to determine number of available resources 
 *  $Id: slurm_resource_info.c,v 1.12 2006/10/04 21:52:24 palermo Exp $
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

#if(0)
#define DEBUG 1
#endif

/*
 * slurm_get_avail_procs - Get the number of "available" cpus on a node
 *	given this number given the number of cpus_per_task and
 *	maximum sockets, cores, threads.  Note that the value of
 *	cpus is the lowest-level logical processor (LLLP).
 * IN max_sockets    - Job requested max sockets
 * IN max_cores      - Job requested max cores
 * IN max_threads    - Job requested max threads
 * IN min_sockets    - Job requested min sockets
 * IN min_cores      - Job requested min cores
 * IN cpus_per_task  - Job requested cpus per task
 * IN ntaskspernode  - number of tasks per node
 * IN ntaskspersocket- number of tasks per socket
 * IN ntaskspercore  - number of tasks per core
 * IN/OUT cpus       - Available cpu count
 * IN/OUT sockets    - Available socket count
 * IN/OUT cores      - Available core count
 * IN/OUT threads    - Available thread count
 * IN alloc_cores    - Allocated cores (per socket) count to other jobs
 * IN cr_type        - Consumable Resource type
 *
 * Note: used in both the select/{linear,cons_res} plugins.
 */
int slurm_get_avail_procs(const uint16_t max_sockets,
			  const uint16_t max_cores,
			  const uint16_t max_threads,
			  const uint16_t min_sockets,
			  const uint16_t min_cores,
			  uint16_t cpus_per_task,
			  const uint16_t ntaskspernode,
			  const uint16_t ntaskspersocket,
			  const uint16_t ntaskspercore,
			  uint16_t *cpus, 
			  uint16_t *sockets, 
			  uint16_t *cores, 
			  uint16_t *threads,
			  const uint16_t *alloc_cores,
			  const select_type_plugin_info_t cr_type,
			  uint32_t job_id,
			  char *name)
{
	uint16_t avail_cpus = 0, max_cpus = 0;
	uint16_t allocated_cpus = 0, allocated_cores = 0, allocated_sockets = 0;
	uint16_t max_avail_cpus = 0xffff;	/* for alloc_* accounting */
	int i;

        /* pick defaults for any unspecified items */
	if (cpus_per_task <= 0)
		cpus_per_task = 1;
	if (*threads <= 0)
	    	*threads = 1;
	if (*cores <= 0)
	    	*cores = 1;
	if (*sockets <= 0)
	    	*sockets = *cpus / *cores / *threads;
	for (i = 0 ; alloc_cores && i < *sockets; i++) {
		allocated_cores += alloc_cores[i];
		if (alloc_cores[i])
			allocated_sockets++;
	}
#if(DEBUG)
	info("get_avail_procs %u %s MAX User_ sockets %u cores %u threads %u",
			job_id, name, max_sockets, max_cores, max_threads);
	info("get_avail_procs %u %s MIN User_ sockets %u cores %u",
			job_id, name, min_sockets, min_cores);
	info("get_avail_procs %u %s HW_   sockets %u cores %u threads %u",
			job_id, name, *sockets, *cores, *threads);
	info("get_avail_procs %u %s Ntask node   %u sockets %u core   %u",
			job_id, name, ntaskspernode, ntaskspersocket, 
			ntaskspercore);
	info("get_avail_procs %u %s cr_type %d cpus %u  alloc_ c %u s %u",
			job_id, name, cr_type, *cpus, allocated_cores,
			allocated_sockets);
	for (i = 0; alloc_cores && i < *sockets; i++)
		info("get_avail_procs %u %s alloc_cores[%d] = %u", 
		     job_id, name, i, alloc_cores[i]);
#endif
	allocated_cpus = allocated_cores * (*threads);
	switch(cr_type) {
	/* For the following CR types, nodes have no notion of socket, core,
	   and thread.  Only one level of logical processors */ 
	case SELECT_TYPE_INFO_NONE:
		/* Default for select/linear */
	case CR_CPU:
	case CR_CPU_MEMORY:
		
		if (*cpus >= allocated_cpus)
			*cpus -= allocated_cpus;
		else {
			*cpus = 0;
			error("cons_res: *cpus underflow");
		}

	case CR_MEMORY:
		/*** compute an overall maximum cpu count honoring ntasks* ***/
		max_cpus  = *cpus;
		if (ntaskspernode > 0) {
			max_cpus = MIN(max_cpus, ntaskspernode);
		}
		break;

	/* For all other types, nodes contain sockets, cores, and threads */
	case CR_CORE:
	case CR_CORE_MEMORY:
		if (*cpus >= allocated_cpus)
			*cpus -= allocated_cpus;
		else {
			*cpus = 0;
			error("cons_res: *cpus underflow");
		}
		if (allocated_cores > 0) {
			max_avail_cpus = 0;
			int tmp_diff = 0;
			for (i=0; i<*sockets; i++) {
				tmp_diff = *cores - alloc_cores[i];
				if (min_cores <= tmp_diff) {
					tmp_diff *= (*threads);
					max_avail_cpus += tmp_diff;
				}
			}
		} 

		/*** honor socket/core/thread maximums ***/
		*sockets = MIN(*sockets, max_sockets);
		*threads = MIN(*threads, max_threads);
		*cores   = MIN(*cores,   max_cores);

		if (min_sockets > *sockets) {
			*cpus = 0;
		} else {
			int max_cpus_socket = 0;
			max_cpus = 0;
			for (i=0; i<*sockets; i++) {
				max_cpus_socket = 0;
				if (min_cores <= *cores) {
				        int num_threads = *threads;
					if (ntaskspercore > 0) {
						num_threads = MIN(num_threads,
							       ntaskspercore);
					}
					max_cpus_socket = *cores * num_threads;
				}
				if (ntaskspersocket > 0) {
					max_cpus_socket = MIN(max_cpus_socket,
							      ntaskspersocket);
				}
				max_cpus += max_cpus_socket;
			}
			max_cpus = MIN(max_cpus, max_avail_cpus);
		}

		/*** honor any availability maximum ***/
		max_cpus = MIN(max_cpus, max_avail_cpus);

		if (ntaskspernode > 0) {
			max_cpus = MIN(max_cpus, ntaskspernode);
		}
		break;

	case CR_SOCKET:
	case CR_SOCKET_MEMORY:
	default:
		if (*sockets >= allocated_sockets)
			*sockets -= allocated_sockets; /* sockets count */
		else {
			*sockets = 0;
			error("cons_res: *sockets underflow");
		}
		if (*cpus >= allocated_cpus)
			*cpus -= allocated_cpus;
		else {
			*cpus = 0;
			error("cons_res: *cpus underflow");
		}

		/*** honor socket/core/thread maximums ***/
		*sockets = MIN(*sockets, max_sockets);
		*cores   = MIN(*cores,   max_cores);
		*threads = MIN(*threads, max_threads);

		if (min_sockets > *sockets)
			*cpus = 0;
		
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

#if(DEBUG)
	info("get_avail_procs %u %s return cpus %u sockets %u cores %u threads %u",
			job_id, name, *cpus, *sockets, *cores, *threads);
	info("get_avail_procs %d %s avail_cpus %u",  job_id, name, avail_cpus);
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
