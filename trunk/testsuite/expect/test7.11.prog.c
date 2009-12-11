/*****************************************************************************\
 *  prog7.11.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <slurm/spank.h>

#define SPANK_JOB_ENV_TESTS 0

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

static int opt_arg = 0;
static char *opt_out_file = NULL;

static int _test_opt_process(int val, const char *optarg, int remote);

/*
 *  Provide a --test_suite=[opt_arg] option to srun:
 */
struct spank_option spank_options[] =
{
	{ "test_suite", "[opt_arg]", "Component of slurm test suite.", 2, 0,
		_test_opt_process
	},
	SPANK_OPTIONS_TABLE_END
};
struct spank_option spank_options_reg[] =
{
	{ "test_suite_reg", "[opt_arg]",
		"Registered component of slurm test suite.", 2, 0,
		_test_opt_process
	},
	SPANK_OPTIONS_TABLE_END
};

static int _test_opt_process(int val, const char *optarg, int remote)
{
	opt_arg = atoi(optarg);
	if (!remote)
		slurm_info("_test_opt_process: test_suite: opt_arg=%d", opt_arg);

	return (0);
}

/*  Called from both srun and slurmd */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
	spank_context_t context;

	context = spank_context();
	if ((context != S_CTX_LOCAL) && (context != S_CTX_REMOTE) &&
	    (context != S_CTX_ALLOCATOR))
		slurm_error("spank_context error");
	if (SPANK_JOB_ENV_TESTS &&
	    ((context == S_CTX_LOCAL) || (context == S_CTX_ALLOCATOR))) {
		/* Testing logic for spank_job_control_env options */
		char test_value[200];
		spank_err_t err;
		spank_job_control_setenv(sp, "DUMMY", "DV",   1);
		spank_job_control_setenv(sp, "NAME", "VALUE", 1);
		spank_job_control_setenv(sp, "name", "value", 1);
/*		spank_job_control_setenv(sp, "PATH", "/", 1); */
		memset(test_value, 0, sizeof(test_value));
		err = spank_job_control_getenv(
			sp, "NAME", test_value, sizeof(test_value));
		if (err != ESPANK_SUCCESS)
			slurm_error("spank_get_job_env error, NULL");
		else if (strcmp(test_value, "VALUE"))
			slurm_error("spank_get_job_env error, bad value");
		spank_job_control_unsetenv(sp, "DUMMY");
	}

	if (spank_option_register(sp, spank_options_reg) != ESPANK_SUCCESS)
		slurm_error("spank_option_register error");
	if (spank_remote(sp) && (ac == 1))
		opt_out_file = strdup(av[0]);

	return (0);
}

/* Called from both srun and slurmd, not tested here
int slurm_spank_init_post_opt(spank_t sp, int ac, char **av) */

/* Called from srun only */
slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
	slurm_info("slurm_spank_local_user_init");

	return (0);
}

/* Called from slurmd only */
int slurm_spank_task_init(spank_t sp, int ac, char **av)
{
	uid_t my_uid;
	int argc, i;
	char **argv;

	if (opt_out_file && opt_arg) {
		FILE *fp = fopen(opt_out_file, "a");
		if (!fp)
			return (-1);
		fprintf(fp, "slurm_spank_task_init: opt_arg=%d\n", opt_arg);
		if (spank_get_item(sp, S_JOB_UID, &my_uid) == ESPANK_SUCCESS)
			fprintf(fp, "spank_get_item: my_uid=%d\n", my_uid);
                if (spank_get_item(sp, S_JOB_ARGV, &argc, &argv) ==
		    ESPANK_SUCCESS) {
			for (i=0; i<argc; i++) {
				fprintf(fp, "spank_get_item: argv[%d]=%s\n",
					i, argv[i]);
			}
		}
		fclose(fp);
	}
	return (0);
}

/* Called from slurmd only, not tested here
int slurm_spank_task_post_fork(spank_t sp, int ac, char **av) */

/* Called from slurmd only, not tested here
int slurm_spank_task_exit(spank_t sp, int ac, char **av) */

/* Called from both srun and slurmd */
int slurm_spank_exit(spank_t sp, int ac, char **av)
{
	if (opt_out_file && opt_arg) {
		FILE *fp = fopen(opt_out_file, "a");
		if (!fp)
			return (-1);
		fprintf(fp, "slurm_spank_exit: opt_arg=%d\n", opt_arg);
		fclose(fp);
	} else if (opt_arg)
		slurm_info("slurm_spank_exit: opt_arg=%d", opt_arg);
	return (0);
}
