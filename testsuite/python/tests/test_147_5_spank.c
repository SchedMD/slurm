/*****************************************************************************\
 *  test_147_5_spank.c - Hold the extern step in slurm_spank_init().
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
\*****************************************************************************/

/*
 * Plugstack argument: wait_dir=<dir>
 *
 * In the extern step only, slurm_spank_init() creates <dir>/blocked.<job_id>
 * and then waits for <dir>/release.<job_id> to exist, for at most 300
 * seconds. slurmstepd runs it after its socket is open and before the task
 * plugins set up the step, so this holds that window open until the test
 * releases it. Every other step and context returns at once.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/spank.h"

SPANK_PLUGIN(test_147_5, 1);

#define MAX_WAIT 300

int slurm_spank_init(spank_t sp, int ac, char **av)
{
	const char *wait_dir = NULL;
	char path[4096];
	uint32_t job_id = 0, step_id = 0;
	int fd = -1;

	if (spank_context() != S_CTX_REMOTE)
		return ESPANK_SUCCESS;

	if ((spank_get_item(sp, S_JOB_STEPID, &step_id) != ESPANK_SUCCESS) ||
	    (step_id != SLURM_EXTERN_CONT))
		return ESPANK_SUCCESS;

	for (int i = 0; i < ac; i++) {
		if (!strncmp(av[i], "wait_dir=", strlen("wait_dir=")))
			wait_dir = av[i] + strlen("wait_dir=");
	}
	if (!wait_dir ||
	    (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS))
		return ESPANK_SUCCESS;

	snprintf(path, sizeof(path), "%s/blocked.%u", wait_dir, job_id);
	if ((fd = open(path, (O_CREAT | O_WRONLY), 0644)) >= 0)
		close(fd);

	snprintf(path, sizeof(path), "%s/release.%u", wait_dir, job_id);
	for (int i = 0; (i < MAX_WAIT) && access(path, F_OK); i++)
		sleep(1);

	return ESPANK_SUCCESS;
}
