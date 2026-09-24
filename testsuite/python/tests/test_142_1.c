/*****************************************************************************\
 *  Submit a batch job with an explicit tasks-per-socket value via libslurm.
 *  Copyright (C) 2026 IndraPutrabinH.
 *
 *  This file is part of Slurm, a resource management program.
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for details.
\*****************************************************************************/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

int main(int argc, char **argv)
{
	job_desc_msg_t job;
	submit_response_msg_t *response = NULL;
	char cwd[PATH_MAX];
	char *environment[] = { "PATH=/usr/bin:/bin", NULL };

	if ((argc != 3) || !getcwd(cwd, sizeof(cwd)))
		return 1;

	slurm_init(NULL);
	slurm_init_job_desc_msg(&job);
	job.user_id = getuid();
	job.group_id = getgid();
	job.name = "tasks_per_socket";
	job.min_nodes = job.max_nodes = 1;
	job.num_tasks = 1;
	job.cpus_per_task = atoi(argv[1]);
	job.min_cpus = job.max_cpus = job.cpus_per_task;
	/* The CLI rejects zero, but an API client can submit it directly. */
	job.ntasks_per_socket = atoi(argv[2]);
	job.pn_min_memory = 1;
	job.time_limit = 1;
	job.script = "#!/bin/sh\nexit 0\n";
	job.work_dir = cwd;
	job.std_out = job.std_err = "/dev/null";
	job.environment = environment;
	job.env_size = 1;

	if (slurm_submit_batch_job(&job, &response) != SLURM_SUCCESS) {
		slurm_perror("slurm_submit_batch_job");
		return 1;
	}
	printf("%u\n", response->step_id.job_id);
	slurm_free_submit_response_response_msg(response);
	slurm_fini();
	return 0;
}
