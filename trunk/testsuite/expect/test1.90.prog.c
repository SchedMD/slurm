/*****************************************************************************\
 *  test1.90.prog.c - Simple test program for SLURM regression test1.90.
 *  Reports SLURM task ID, the CPU mask, and memory mask,
 *  similar functionality to "taskset" command
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#define _GNU_SOURCE
#include <numa.h>
#include <stdio.h>
#include <stdlib.h>

/* For RedHat systems, run with NEW_TOOLS=0
 * For current Ubuntu, run wtih NEW_TOOLS=1 */
#define NEW_TOOLS 0

#if NEW_TOOLS
#define MY_MASK struct bitmask *
#define MY_TEST numa_bitmask_isbitset
#else
#define MY_MASK nodemask_t
#define MY_TEST nodemask_isset
#endif

static void _load_cpu_mask(MY_MASK *cpu_mask)
{
	*cpu_mask = numa_get_run_node_mask();
}

static void _load_mem_mask(MY_MASK *mem_mask)
{
	*mem_mask = numa_get_membind();
}

static unsigned long _mask_to_int(MY_MASK *mask)
{
	int i;
	unsigned long rc = 0;
	for (i=0; i<NUMA_NUM_NODES; i++) {
#if NEW_TOOLS
		if (MY_TEST(*mask, i))
#else
		if (MY_TEST(mask, i))
#endif
			rc += (1 << i);
	}
	return rc;
}

main (int argc, char **argv)
{
	char *task_str;
	MY_MASK cpu_mask;
	MY_MASK mem_mask;
	int task_id;

	if (numa_available() < 0) {
		fprintf(stderr, "ERROR: numa support not available\n");
		exit(0);
	}

	if ((task_str = getenv("SLURM_PROCID")) == NULL) {
		fprintf(stderr, "ERROR: getenv(SLURM_PROCID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	_load_cpu_mask(&cpu_mask);
	_load_mem_mask(&mem_mask);
	printf("TASK_ID:%d,CPU_MASK:%lu,MEM_MASK:%lu\n",
		task_id, _mask_to_int(&cpu_mask), _mask_to_int(&mem_mask));
	exit(0);
}
