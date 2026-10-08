/*****************************************************************************\
 *  Reports SLURM_PROCID and CPU mask in JSON, similar to "taskset" command.
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
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void _mask_to_hex(cpu_set_t *mask, char *str)
{
	int nibble_cnt = (CPU_SETSIZE + 3) / 4;
	int i, j, len = 0, started = 0;

	for (i = nibble_cnt - 1; i >= 0; i--) {
		int nibble = 0;

		for (j = 0; j < 4; j++) {
			if (CPU_ISSET((i * 4) + j, mask))
				nibble |= (1 << j);
		}

		if (!nibble && !started)
			continue;
		started = 1;

		if (nibble < 10)
			str[len++] = '0' + nibble;
		else
			str[len++] = 'a' + (nibble - 10);
	}
	if (!started)
		str[len++] = '0';
	str[len] = '\0';
}

int main(int argc, char **argv)
{
	char *task_str;
	cpu_set_t mask;
	char cpu_str[CPU_SETSIZE / 4 + 2];
	int task_id;

	_load_mask(&mask);
	task_str = getenv("SLURM_PROCID");
	if (!task_str) {
		fprintf(stderr, "ERROR: getenv(SLURM_PROCID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	_mask_to_hex(&mask, cpu_str);
	printf("{\"task_id\": %d, \"mask\": \"%s\"}\n", task_id, cpu_str);
	exit(0);
}
