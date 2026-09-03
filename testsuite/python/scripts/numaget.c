/*****************************************************************************\
 *  Reports SLURM_PROCID, the memory policy mode and three NUMA-node masks in
 *  JSON: the memory-binding mask, the mask of nodes the task is allowed to
 *  allocate memory from, and the nodemask of the memory policy.
 *  Similar functionality to the "numactl" command
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(LIBNUMA_API_VERSION) || (LIBNUMA_API_VERSION < 2)
#error "libnuma API version 2 or higher is required."
#endif

static const char *_numa_mode_str(int mode)
{
	switch (mode) {
	case MPOL_DEFAULT:
		return "DEFAULT";
	case MPOL_BIND:
		return "BIND";
	case MPOL_INTERLEAVE:
		return "INTERLEAVE";
	case MPOL_PREFERRED:
		return "PREFERRED";
	case MPOL_LOCAL:
		return "LOCAL";
	default:
		return "UNKNOWN";
	}
}

static void _mask_to_hex(struct bitmask **mask, char *str)
{
	int nibble_cnt = (NUMA_NUM_NODES + 3) / 4;
	int i, j, len = 0, started = 0;

	for (i = nibble_cnt - 1; i >= 0; i--) {
		int nibble = 0;

		for (j = 0; j < 4; j++) {
			int bit = (i * 4) + j;

			if (bit >= NUMA_NUM_NODES)
				continue;
			if (numa_bitmask_isbitset(*mask, bit))
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
	struct bitmask *mem_mask;
	struct bitmask *allowed_mask;
	struct bitmask *policy_mask;
	char mem_str[NUMA_NUM_NODES / 4 + 2];
	char allowed_str[NUMA_NUM_NODES / 4 + 2];
	char policy_str[NUMA_NUM_NODES / 4 + 2];
	int task_id;
	int mode;

	if (numa_available() < 0) {
		fprintf(stderr, "ERROR: numa support not available\n");
		exit(1);
	}

	task_str = getenv("SLURM_PROCID");
	if (!task_str) {
		fprintf(stderr, "ERROR: getenv(SLURM_PROCID) failed\n");
		exit(1);
	}
	task_id = atoi(task_str);
	mem_mask = numa_get_membind();
	allowed_mask = numa_get_mems_allowed();

	policy_mask = numa_allocate_nodemask();
	if (get_mempolicy(&mode, policy_mask->maskp, policy_mask->size, NULL,
			  0)) {
		fprintf(stderr, "ERROR: get_mempolicy failed\n");
		exit(1);
	}

	_mask_to_hex(&mem_mask, mem_str);
	_mask_to_hex(&allowed_mask, allowed_str);
	_mask_to_hex(&policy_mask, policy_str);
	printf("{\"task_id\": %d, \"mem_mask\": \"%s\", "
	       "\"allowed_mask\": \"%s\", \"policy_mode\": \"%s\", "
	       "\"policy_mask\": \"%s\"}\n",
	       task_id, mem_str, allowed_str, _numa_mode_str(mode), policy_str);
	numa_bitmask_free(mem_mask);
	numa_bitmask_free(allowed_mask);
	numa_bitmask_free(policy_mask);
	exit(0);
}
