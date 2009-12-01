/*****************************************************************************\
 *  slurm_resource_info.c - Functions to determine number of available resources 
 *  $Id: slurm_resource_info.c,v 1.12 2006/10/04 21:52:24 palermo Exp $
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
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <ctype.h>
#include <sys/types.h>
#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#if(0)
#define DEBUG 1
#endif

/*
 * First clear all of the bits in "*data" which are set in "clear_mask".
 * Then set all of the bits in "*data" that are set in "set_mask".
 */
static void _clear_then_set(int *data, int clear_mask, int set_mask)
{
	*data &= ~clear_mask;
	*data |= set_mask;
}

/*
 * _isvalue
 * returns 1 is the argument appears to be a value, 0 otherwise
 */
static int _isvalue(char *arg) {
    	if (isdigit(*arg)) {	 /* decimal values and 0x... hex values */
	    	return 1;
	}

	while (isxdigit(*arg)) { /* hex values not preceded by 0x */
		arg++;
	}
	if (*arg == ',' || *arg == '\0') { /* end of field or string */
	    	return 1;
	}

	return 0;	/* not a value */
}

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
 * Note: currently only used in the select/linear plugin.
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

	if (cpu_bind_type & CPU_BIND_VERBOSE)
		strcat(str, "verbose,");

	if (cpu_bind_type & CPU_BIND_TO_THREADS)
		strcat(str, "threads,");
	if (cpu_bind_type & CPU_BIND_TO_CORES)
		strcat(str, "cores,");
	if (cpu_bind_type & CPU_BIND_TO_SOCKETS)
		strcat(str, "sockets,");
	if (cpu_bind_type & CPU_BIND_TO_LDOMS)
		strcat(str, "ldoms,");
	if (cpu_bind_type & CPU_BIND_NONE)
		strcat(str, "none,");
	if (cpu_bind_type & CPU_BIND_RANK)
		strcat(str, "rank,");
	if (cpu_bind_type & CPU_BIND_MAP)
		strcat(str, "map_cpu,");
	if (cpu_bind_type & CPU_BIND_MASK)
		strcat(str, "mask_cpu,");
	if (cpu_bind_type & CPU_BIND_LDRANK)
		strcat(str, "rank_ldom,");
	if (cpu_bind_type & CPU_BIND_LDMAP)
		strcat(str, "map_ldom,");
	if (cpu_bind_type & CPU_BIND_LDMASK)
		strcat(str, "mask_ldom,");

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
		strcat(str, "map_mem,");
	if (mem_bind_type & MEM_BIND_MASK)
		strcat(str, "mask_mem,");

	if (*str) {
		str[strlen(str)-1] = '\0';	/* remove trailing ',' */
	} else {
	    	strcat(str, "(null type)");	/* no bits set */
	}
}

void slurm_print_cpu_bind_help(void)
{
	printf(
"CPU bind options:\n"
"    --cpu_bind=         Bind tasks to CPUs\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to CPUs (default)\n"
"        rank            bind by task rank\n"
"        map_cpu:<list>  specify a CPU ID binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_cpu:<list> specify a CPU ID binding mask for each task\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        rank_ldom       bind task by rank to CPUs in a NUMA locality domain\n"
"        map_ldom:<list> specify a NUMA locality domain ID for each task\n"
"                        where <list> is <ldom1>,<ldom2>,...<ldomN>\n"
"        mask_ldom:<list>specify a NUMA locality domain ID mask for each task\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        sockets         auto-generated masks bind to sockets\n"
"        cores           auto-generated masks bind to cores\n"
"        threads         auto-generated masks bind to threads\n"
"        ldoms           auto-generated masks bind to NUMA locality domains\n"
"        help            show this help message\n");
}

/*
 * verify cpu_bind arguments
 *
 * we support different launch policy names
 * we also allow a verbose setting to be specified
 *     --cpu_bind=threads
 *     --cpu_bind=cores
 *     --cpu_bind=sockets
 *     --cpu_bind=v
 *     --cpu_bind=rank,v
 *     --cpu_bind=rank
 *     --cpu_bind={MAP_CPU|MASK_CPU}:0,1,2,3,4
 *
 *
 * returns -1 on error, 0 otherwise
 */
int slurm_verify_cpu_bind(const char *arg, char **cpu_bind, 
			  cpu_bind_type_t *flags)
{
	char *buf, *p, *tok;
	int bind_bits =
		CPU_BIND_NONE|CPU_BIND_RANK|CPU_BIND_MAP|CPU_BIND_MASK;
	int bind_to_bits =
		CPU_BIND_TO_SOCKETS|CPU_BIND_TO_CORES|CPU_BIND_TO_THREADS;
	uint16_t task_plugin_param = slurm_get_task_plugin_param();

	bind_bits    |= CPU_BIND_LDRANK|CPU_BIND_LDMAP|CPU_BIND_LDMASK;
	bind_to_bits |= CPU_BIND_TO_LDOMS;

	if (arg == NULL) {
		if ((*flags != 0) || 		/* already set values */
		    (task_plugin_param == 0))	/* no system defaults */
			return 0;

		/* set system defaults */
		xfree(*cpu_bind);
		if (task_plugin_param & CPU_BIND_NONE)
			*flags = CPU_BIND_NONE;
		else if (task_plugin_param & CPU_BIND_TO_SOCKETS)
			*flags = CPU_BIND_TO_SOCKETS;
		else if (task_plugin_param & CPU_BIND_TO_CORES)
			*flags = CPU_BIND_TO_CORES;
		else if (task_plugin_param & CPU_BIND_TO_THREADS)
			*flags |= CPU_BIND_TO_THREADS;
		else if (task_plugin_param & CPU_BIND_TO_LDOMS)
			*flags |= CPU_BIND_TO_LDOMS;
		if (task_plugin_param & CPU_BIND_VERBOSE)
			*flags |= CPU_BIND_VERBOSE;
	    	return 0;
	}

	/* Start with system default verbose flag (if set) */
	if (task_plugin_param & CPU_BIND_VERBOSE)
		*flags |= CPU_BIND_VERBOSE;

    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			slurm_print_cpu_bind_help();
			return 1;
		} else if ((strcasecmp(tok, "q") == 0) ||
			   (strcasecmp(tok, "quiet") == 0)) {
		        *flags &= ~CPU_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "v") == 0) ||
			   (strcasecmp(tok, "verbose") == 0)) {
		        *flags |= CPU_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "no") == 0) ||
			   (strcasecmp(tok, "none") == 0)) {
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_NONE);
			xfree(*cpu_bind);
		} else if (strcasecmp(tok, "rank") == 0) {
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_RANK);
			xfree(*cpu_bind);
		} else if ((strncasecmp(tok, "map_cpu", 7) == 0) ||
		           (strncasecmp(tok, "mapcpu", 6) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_MAP);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind="
				      "map_cpu:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strncasecmp(tok, "mask_cpu", 8) == 0) ||
		           (strncasecmp(tok, "maskcpu", 7) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_MASK);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind="
				      "mask_cpu:<list>\"");
				xfree(buf);
				return -1;
			}
		} else if (strcasecmp(tok, "rank_ldom") == 0) {
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDRANK);
			xfree(*cpu_bind);
		} else if ((strncasecmp(tok, "map_ldom", 8) == 0) ||
		           (strncasecmp(tok, "mapldom", 7) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDMAP);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind="
				      "map_ldom:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strncasecmp(tok, "mask_ldom", 9) == 0) ||
		           (strncasecmp(tok, "maskldom", 8) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDMASK);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind="
				      "mask_ldom:<list>\"");
				xfree(buf);
				return -1;
			}
		} else if ((strcasecmp(tok, "socket") == 0) ||
		           (strcasecmp(tok, "sockets") == 0)) {
			if (task_plugin_param & 
			    (CPU_BIND_NONE | CPU_BIND_TO_CORES | 
			     CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS)) {
				error("--cpu_bind=sockets incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
				return -1;
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_SOCKETS);
		} else if ((strcasecmp(tok, "core") == 0) ||
		           (strcasecmp(tok, "cores") == 0)) {
			if (task_plugin_param & 
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS | 
			     CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS)) {
				error("--cpu_bind=cores incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
				return -1;
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_CORES);
		} else if ((strcasecmp(tok, "thread") == 0) ||
		           (strcasecmp(tok, "threads") == 0)) {
			if (task_plugin_param & 
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS | 
			     CPU_BIND_TO_CORES | CPU_BIND_TO_LDOMS)) {
				error("--cpu_bind=threads incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
				return -1;
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_THREADS);
		} else if ((strcasecmp(tok, "ldom") == 0) ||
		           (strcasecmp(tok, "ldoms") == 0)) {
			if (task_plugin_param & 
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS | 
			     CPU_BIND_TO_CORES | CPU_BIND_TO_THREADS)) {
				error("--cpu_bind=threads incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
				return -1;
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_LDOMS);
		} else {
			error("unrecognized --cpu_bind argument \"%s\"", tok);
			xfree(buf);
			return -1;
		}
	}
	xfree(buf);

	return 0;
}

void slurm_print_mem_bind_help(void)
{
			printf(
"Memory bind options:\n"
"    --mem_bind=         Bind memory to locality domains (ldom)\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to memory (default)\n"
"        rank            bind by task rank\n"
"        local           bind to memory local to processor\n"
"        map_mem:<list>  specify a memory binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_mem:<list> specify a memory binding mask for each tasks\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        help            show this help message\n");
}

/*
 * verify mem_bind arguments
 *
 * we support different memory binding names
 * we also allow a verbose setting to be specified
 *     --mem_bind=v
 *     --mem_bind=rank,v
 *     --mem_bind=rank
 *     --mem_bind={MAP_MEM|MASK_MEM}:0,1,2,3,4
 *
 * returns -1 on error, 0 otherwise
 */
int slurm_verify_mem_bind(const char *arg, char **mem_bind, 
			  mem_bind_type_t *flags)
{
	char *buf, *p, *tok;
	int bind_bits = MEM_BIND_NONE|MEM_BIND_RANK|MEM_BIND_LOCAL|
		MEM_BIND_MAP|MEM_BIND_MASK;

	if (arg == NULL) {
	    	return 0;
	}

    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			slurm_print_mem_bind_help();
			return 1;
			
		} else if ((strcasecmp(tok, "q") == 0) ||
			   (strcasecmp(tok, "quiet") == 0)) {
		        *flags &= ~MEM_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "v") == 0) ||
			   (strcasecmp(tok, "verbose") == 0)) {
		        *flags |= MEM_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "no") == 0) ||
			   (strcasecmp(tok, "none") == 0)) {
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_NONE);
			xfree(*mem_bind);
		} else if (strcasecmp(tok, "rank") == 0) {
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_RANK);
			xfree(*mem_bind);
		} else if (strcasecmp(tok, "local") == 0) {
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_LOCAL);
			xfree(*mem_bind);
		} else if ((strncasecmp(tok, "map_mem", 7) == 0) ||
		           (strncasecmp(tok, "mapmem", 6) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_MAP);
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = xstrdup(list);
			} else {
				error("missing list for \"--mem_bind=map_mem:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strncasecmp(tok, "mask_mem", 8) == 0) ||
		           (strncasecmp(tok, "maskmem", 7) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_MASK);
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = xstrdup(list);
			} else {
				error("missing list for \"--mem_bind=mask_mem:<list>\"");
				xfree(buf);
				return 1;
			}
		} else {
			error("unrecognized --mem_bind argument \"%s\"", tok);
			xfree(buf);
			return 1;
		}
	}

	xfree(buf);
	return 0;
}
