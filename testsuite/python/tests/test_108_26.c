/*****************************************************************************\
 *  Send a SuspendExcNodes, SuspendExcParts or SuspendExcStates update with no
 *  value, as older scontrol versions did for malformed input, and print the
 *  result. slurmctld must reject the request rather than crash.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

int main(int argc, char **argv)
{
	int rc, err;

	if (argc != 2) {
		fprintf(stderr,
			"Internal Helper Error - Usage: %s nodes|parts|states\n",
			argv[0]);
		return 2;
	}

	slurm_init(NULL);

	if (!strcmp(argv[1], "nodes")) {
		rc = slurm_update_suspend_exc_nodes(NULL, UPDATE_SET);
	} else if (!strcmp(argv[1], "parts")) {
		rc = slurm_update_suspend_exc_parts(NULL, UPDATE_SET);
	} else if (!strcmp(argv[1], "states")) {
		rc = slurm_update_suspend_exc_states(NULL, UPDATE_SET);
	} else {
		fprintf(stderr, "Internal Helper Error - Unknown setting: %s\n",
			argv[1]);
		return 2;
	}
	err = errno;

	printf("rc=%d errno=%d error=%s\n", rc, err, slurm_strerror(err));

	slurm_fini();
	return 0;
}
