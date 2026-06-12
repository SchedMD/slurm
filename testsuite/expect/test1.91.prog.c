/*****************************************************************************\
 *  test1.91.prog.c - Simple test program for Slurm regression test1.91.
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

int val_to_char(int v)
{
	if (v >= 0 && v < 10)
		return '0' + v;
	else if (v >= 10 && v < 16)
		return ('a' - 10) + v;
	else
		return -1;
}

static char *_cpuset_to_str(const cpu_set_t *mask, char *str, int size)
{
	int base, cnt;
	char *ptr = str;
	char *ret = NULL;

	for (base = CPU_SETSIZE - 4; base >= 0; base -= 4) {
		char val = 0;
		if (++cnt >= size)
			break;
		if (CPU_ISSET(base, mask))
			val |= 1;
		if (CPU_ISSET(base + 1, mask))
			val |= 2;
		if (CPU_ISSET(base + 2, mask))
			val |= 4;
		if (CPU_ISSET(base + 3, mask))
			val |= 8;
		if (!ret && val)
			ret = ptr;
		*ptr++ = val_to_char(val);
	}
	*ptr = '\0';
	return ret ? ret : ptr - 1;
}

static uint64_t _mask_to_int(cpu_set_t *mask)
{
	uint64_t i, rc = 0;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask)) {
			if (i > 63) {
				printf("OVERFLOW\n");
				rc = 999999999;
				break;
			}
			rc += (((uint64_t) 1) << i);
		}
	}
	return rc;
}

int main (int argc, char **argv)
{
	char mask_str[2048], *task_str;
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
	/* NOTE: The uint64_t number is subject to overflow if there are
	 * >64 CPUs on a compute node, but the hex value will be valid */
	printf("TASK_ID:%d,MASK:%"PRIu64":0x%s\n", task_id,
	       _mask_to_int(&mask),
	       _cpuset_to_str(&mask, mask_str, sizeof(mask_str)));
	exit(0);
}
