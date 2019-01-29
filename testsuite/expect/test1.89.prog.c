/*****************************************************************************\
 *  test1.89.prog.c - Simple test program for Slurm regression test1.89.
 *  Reports Slurm task ID and the CPU mask,
 *  similar functionality to "taskset" command
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
\*****************************************************************************/
#define _GNU_SOURCE
#define __USE_GNU
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static void _load_mask(cpu_set_t *mask)
{
	int rc;

	rc = sched_getaffinity((pid_t) 0, sizeof(cpu_set_t), mask);
	if (rc != 0) {
		fprintf(stderr, "ERROR: sched_getaffinity: %s\n",
			strerror(errno));
		exit(1);
	}
}

static uint64_t _mask_to_int(cpu_set_t *mask)
{
	uint64_t i, rc = 0;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask))
			rc += (((uint64_t) 1) << i);
	}
	return rc;
}


int main (int argc, char **argv)
{
	char *task_str;
	cpu_set_t mask;
	int task_id;

	_load_mask(&mask);
	/* On POE systems, MP_CHILD is equivalent to SLURM_PROCID */
	if (((task_str = getenv("SLURM_PROCID")) == NULL) &&
	    ((task_str = getenv("MP_CHILD")) == NULL)) {
		fprintf(stderr, "ERROR: getenv(SLURM_PROCID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	printf("TASK_ID:%d,MASK:%"PRIu64"\n", task_id, _mask_to_int(&mask));
	exit(0);
}
