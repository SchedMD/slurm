/*****************************************************************************\
 *  test_147_5.c - Adopt a process into the extern step of a job.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
\*****************************************************************************/

/*
 * Usage: test_147_5 <job_id> <node_name> <pid>
 *
 * Sends the extern step of <job_id> on <node_name> the same
 * REQUEST_ADD_EXTERN_PID that pam_slurm_adopt sends, asking it to adopt
 * <pid>. Must run as root.
 *
 * Prints "RC=<n>" with the reply of slurmstepd and exits 0, or exits 1 if the
 * step could not be reached at all.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/stepd_api.h"

int main(int argc, char **argv)
{
	slurm_step_id_t step_id = {
		.step_het_comp = NO_VAL,
		.step_id = SLURM_EXTERN_CONT,
	};
	uint16_t protocol_version = 0;
	char *endptr = NULL;
	pid_t pid = 0;
	int fd = -1, rc = SLURM_SUCCESS;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <job_id> <node_name> <pid>\n",
			argv[0]);
		return 1;
	}

	slurm_init(NULL);

	step_id.job_id = strtoul(argv[1], &endptr, 10);
	if (*endptr || !step_id.job_id) {
		fprintf(stderr, "Invalid job id '%s'\n", argv[1]);
		return 1;
	}
	pid = strtol(argv[3], &endptr, 10);
	if (*endptr || (pid <= 0)) {
		fprintf(stderr, "Invalid pid '%s'\n", argv[3]);
		return 1;
	}

	fd = stepd_connect(NULL, argv[2], &step_id, &protocol_version);
	if (fd < 0) {
		fprintf(stderr,
			"Cannot connect to the extern step of job %s on %s\n",
			argv[1], argv[2]);
		return 1;
	}

	rc = stepd_add_extern_pid(fd, protocol_version, pid);
	close(fd);

	printf("RC=%d\n", rc);
	slurm_fini();
	return 0;
}
