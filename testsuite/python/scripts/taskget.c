/*****************************************************************************\
 *  Reports SLURM_PROCID and CPU mask in JSON, similar to "taskset" command.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
\*****************************************************************************/
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
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

static uint64_t _mask_to_int(cpu_set_t *mask)
{
	uint64_t i, rc = 0;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask))
			rc += (((uint64_t) 1) << i);
	}
	return rc;
}

int main(int argc, char **argv)
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
	printf("{\"task_id\": %d, \"mask\": %" PRIu64 "}\n", task_id, _mask_to_int(&mask));
	exit(0);
}
