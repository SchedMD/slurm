/*****************************************************************************\
 *  scaling.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe <c16817@cray.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#define _GNU_SOURCE

#include "switch_cray.h"

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)

#include <stdio.h>
#include <stdlib.h>

// Static functions
static int _get_cpu_total(void);
static uint32_t _get_mem_total(void);

/*
 * Determines the cpu scaling amount to use.
 * Returns -1 on failure.
 */
int get_cpu_scaling(stepd_step_rec_t *job)
{
	int total_cpus, num_app_cpus, cpu_scaling;

	/*
	 *  Get the number of CPUs on the node
	 */
	total_cpus = _get_cpu_total();
	if (total_cpus <= 0) {
		CRAY_ERR("total_cpus <= 0: %d", total_cpus);
		return -1;
	}

	/*
	 * If the submission didn't come from srun (API style)
	 * perhaps they didn't fill in things correctly.
	 */
	if (!job->cpus_per_task) {
		job->cpus_per_task = 1;
	}

	/*
	 * Determine number of CPUs requested for the step
	 */
	num_app_cpus = job->cpus;
	if (num_app_cpus <= 0) {
		num_app_cpus = job->node_tasks * job->cpus_per_task;
		if (num_app_cpus <= 0) {
			CRAY_ERR("num_app_cpus <= 0: %d", num_app_cpus);
			return -1;
		}
	}

	/*
	 * Determine what percentage of the CPUs were requested
	 */
	cpu_scaling = (((double) num_app_cpus / (double) total_cpus) *
		       (double) 100) + 0.5;
	if (cpu_scaling > MAX_SCALING) {
		debug("Cpu scaling out of bounds: %d. Reducing to %d%%",
			 cpu_scaling, MAX_SCALING);
		cpu_scaling = MAX_SCALING;
	} else if (cpu_scaling < MIN_SCALING) {
		CRAY_ERR("Cpu scaling out of bounds: %d. Increasing to %d%%",
			 cpu_scaling, MIN_SCALING);
		cpu_scaling = MIN_SCALING;
	}
	return cpu_scaling;
}

/*
 * Determines the memory scaling amount to use.
 * Returns -1 on failure.
 */
int get_mem_scaling(stepd_step_rec_t *job)
{
	int mem_scaling;
	uint32_t total_mem;

	/*
	 * Get the memory amount
	 */
	total_mem = _get_mem_total();
	if (total_mem == 0) {
		CRAY_ERR("Scanning /proc/meminfo results in MemTotal=0");
		return -1;
	}

	// Find the memory scaling factor
	if (job->step_mem == 0) {
		// step_mem of 0 indicates no memory limit,
		// divide to handle multiple --mem 0 steps per node
		mem_scaling = MAX_SCALING / MAX_STEPS_PER_NODE;
	} else {
		// Convert step_mem to kB, then find percentage of total
		mem_scaling = (uint64_t)job->step_mem * 1024 * 100 / total_mem;
	}

	// Make sure it's within boundaries
	if (mem_scaling > MAX_SCALING) {
		CRAY_INFO("Memory scaling out of bounds: %d. "
			  "Reducing to %d%%.",
			  mem_scaling, MAX_SCALING);
		mem_scaling = MAX_SCALING;
	}

	if (mem_scaling < MIN_SCALING) {
		CRAY_ERR("Memory scaling out of bounds: %d. "
			 "Increasing to %d%%",
			 mem_scaling, MIN_SCALING);
		mem_scaling = MIN_SCALING;
	}

	return mem_scaling;
}

/*
 * Get the total amount of memory on the node.
 * Returns 0 on failure.
 */
static uint32_t _get_mem_total(void)
{
	FILE *f = NULL;
	size_t sz = 0;
	ssize_t lsz = 0;
	char *lin = NULL;
	int meminfo_value;
	char meminfo_str[1024];
	uint32_t total_mem = 0;

	f = fopen("/proc/meminfo", "r");
	if (f == NULL ) {
		CRAY_ERR("Failed to open /proc/meminfo: %m");
		return 0;
	}

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			sscanf(lin, "%s %d", meminfo_str,
			       &meminfo_value);
			if (!strcmp(meminfo_str, "MemTotal:")) {
				total_mem = meminfo_value;
				break;
			}
		}
	}
	free(lin);
	TEMP_FAILURE_RETRY(fclose(f));
	return total_mem;
}

/*
 * Function: get_cpu_total
 * Description:
 *  Get the total number of online cpus on the node.
 *
 * RETURNS
 *  Returns the number of online cpus on the node.  On error, it returns -1.
 *
 * TODO:
 * 	Danny suggests using xcgroup_get_param to read the CPU values instead of
 * 	this function.  Look at the way task/cgroup/task_cgroup_cpuset.c or
 * 	jobacct_gather/cgroup/jobacct_gather_cgroup.c does it.
 */
static int _get_cpu_total(void)
{
	FILE *f = NULL;
	char *token = NULL, *lin = NULL, *saveptr = NULL;
	int total = 0;
	ssize_t lsz;
	size_t sz;
	int matches;
	long int number1, number2;

	f = fopen("/sys/devices/system/cpu/online", "r");

	if (!f) {
		CRAY_ERR("Failed to open file"
			 " /sys/devices/system/cpu/online: %m");
		return -1;
	}

	while (!feof(f)) {
		lsz = getline(&lin, &sz, f);
		if (lsz > 0) {
			// Split into comma-separated tokens
			token = strtok_r(lin, ",", &saveptr);
			while (token) {
				// Check each token for a range
				matches = sscanf(token, "%ld-%ld",
						 &number1, &number2);
				if (matches <= 0) {
					// This token isn't numeric
					CRAY_ERR("Error parsing %s: %m", token);
					free(lin);
					TEMP_FAILURE_RETRY(fclose(f));
					return -1;
				} else if (matches == 1) {
					// Single entry
					total++;
				} else if (number2 > number1) {
					// Range
					total += number2 - number1 + 1;
				} else {
					// Invalid range
					CRAY_ERR("Invalid range %s", token);
					free(lin);
					TEMP_FAILURE_RETRY(fclose(f));
					return -1;
				}
				token = strtok_r(NULL, ",", &saveptr);
			}
		}
	}
	free(lin);
	TEMP_FAILURE_RETRY(fclose(f));
	return total;
}

#endif
