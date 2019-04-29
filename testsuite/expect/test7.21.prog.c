/*****************************************************************************\
 *  prog7.21.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
 *  Copyright (C) 2018-2019 SchedMD LLC
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <slurm/spank.h>
#include <slurm/slurm.h>

#define SPANK_JOB_ENV_TESTS 0

/*
 * All spank plugins must define this macro for the Slurm plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

static char *opt_out_file = NULL;

int run_test(spank_t sp, const char *caller)
{
	uint32_t job_id, array_job_id, array_task_id, step_id;
	FILE *fp = NULL;
	int rc, i;
	job_info_msg_t *job_info = NULL;
	errno = 0;

	if (!opt_out_file)
		return -1;
	for (i = 0; (i < 10) && !fp; i++)
		fp = fopen(opt_out_file, "a");
	if (!fp)
		return -1;

	// only want to test against a running job
	if (spank_context() != S_CTX_REMOTE) {
		fprintf(fp, "skipping %s\n", caller);
		return 0;
	}

	if ((rc = spank_get_item(sp, S_JOB_STEPID, &step_id)))
		return rc;
	if ((rc = spank_get_item(sp, S_JOB_ID, &job_id)))
		return rc;
	if ((rc = spank_get_item(sp, S_JOB_ARRAY_ID, &array_job_id)))
		array_job_id = 0;
	if ((rc = spank_get_item(sp, S_JOB_ARRAY_TASK_ID, &array_task_id)))
		array_task_id = 0;

	fprintf(fp, "%s spank_get_item: step_id=%u job_id=%u array_job_id=%u array_task_id=%u\n",
			caller, step_id, job_id, array_job_id, array_task_id);

	// Ask slurm about this job
	rc = slurm_load_job(&job_info, job_id, SHOW_DETAIL);
	if (rc)
		return rc;

	for (i = 0; i < job_info->record_count; ++i) {
		slurm_job_info_t *job = job_info->job_array + i;

		fprintf(fp, "%s load_job: step_id=%u job_id=%u array_job_id=%u array_task_id=%u\n",
			caller, step_id, job->job_id, job->array_job_id,
			job->array_task_id);
	}

	fclose(fp);

	return (0);
}

/*  Called from both srun and slurmd */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
	if (spank_remote(sp) && (ac == 1))
		opt_out_file = strdup(av[0]);

	return (0);
}

/* Called from slurmd only */
int slurm_spank_task_init(spank_t sp, int ac, char **av)
{
	return run_test(sp, __func__);
}

