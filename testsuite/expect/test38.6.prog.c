/*****************************************************************************\
 *  prog38.6.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
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

#define SPANK_JOB_ENV_TESTS 0

/*
 * All spank plugins must define this macro for the Slurm plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

static int opt_arg_srun   = 0;
static int opt_arg_sbatch = 0;
static char *opt_out_file = NULL;

static int _test_opt_process_srun(int val, const char *optarg, int remote);
static int _test_opt_process_sbatch(int val, const char *optarg, int remote);

/*
 *  Provide a --test_suite=[opt_arg] option to srun:
 */
struct spank_option spank_options[] =
{
	{ "test_suite_srun",
	  "[opt_arg_srun]",
	  "Component of slurm test suite.",
	  2,
	  0,
	  _test_opt_process_srun
	},
	SPANK_OPTIONS_TABLE_END
};
struct spank_option spank_options_reg[] =
{
	{ "test_suite_sbatch",
	  "[opt_arg_sbatch]",
	  "Registered component of slurm test suite.",
	  2,
	  0,
	  _test_opt_process_sbatch
	},
	SPANK_OPTIONS_TABLE_END
};

static int _test_opt_process_srun(int val, const char *optarg, int remote)
{
	opt_arg_srun = atoi(optarg);
	if (!remote)
		slurm_info("%s: opt_arg_srun=%d", __func__, opt_arg_srun);

	return (0);
}

static int _test_opt_process_sbatch(int val, const char *optarg, int remote)
{
	opt_arg_sbatch = atoi(optarg);
	if (!remote)
		slurm_info("%s: opt_arg_sbatch=%d", __func__, opt_arg_sbatch);

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
int slurm_spank_local_user_init(spank_t sp, int ac, char **av)
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
	char hostname[64] = "";

	gethostname(hostname, sizeof(hostname));
	if (opt_out_file && (opt_arg_sbatch || opt_arg_srun)) {
		FILE *fp = NULL;
		usleep(getpid() % 500000);   /* Reduce NFS collisions */
		for (i = 0; (i < 10) && !fp; i++)
			fp = fopen(opt_out_file, "a");
		if (!fp) {
			slurm_error("%s: could not open %s",
				    __func__, opt_out_file);
			return -1;
		}
		fprintf(fp, "%s: opt_arg_sbatch=%d opt_arg_srun=%d hostname=%s\n",
			__func__, opt_arg_sbatch, opt_arg_srun, hostname);
		fflush(fp);
		if (spank_get_item(sp, S_JOB_UID, &my_uid) == ESPANK_SUCCESS)
			fprintf(fp, "spank_get_item: my_uid=%d\n", my_uid);
                if (spank_get_item(sp, S_JOB_ARGV, &argc, &argv) ==
		    ESPANK_SUCCESS) {
			for (i = 0; i < argc; i++) {
				fprintf(fp, "spank_get_item: argv[%d]=%s\n",
					i, argv[i]);
			}
		}
		fclose(fp);
	}

	slurm_info("%s: opt_arg_sbatch=%d opt_arg_srun=%d hostname=%s out_file=%s",
		   __func__, opt_arg_sbatch, opt_arg_srun, hostname,
		   opt_out_file);

	return 0;
}

/* Called from slurmd only, not tested here
int slurm_spank_task_post_fork(spank_t sp, int ac, char **av) */

/* Called from slurmd only, not tested here
int slurm_spank_task_exit(spank_t sp, int ac, char **av) */

/* Called from both srun and slurmd */
int slurm_spank_exit(spank_t sp, int ac, char **av)
{
	char hostname[64] = "";
	int i;

	gethostname(hostname, sizeof(hostname));
	if (opt_out_file && (opt_arg_sbatch || opt_arg_srun)) {
		FILE *fp = NULL;
		usleep(getpid() % 500000);   /* Reduce NFS collisions */
		for (i = 0; (i < 10) && !fp; i++)
			fp = fopen(opt_out_file, "a");
		if (!fp) {
			slurm_error("%s: could not open %s",
				    __func__, opt_out_file);
			return -1;
		}
		fprintf(fp, "%s: opt_arg_sbatch=%d opt_arg_srun=%d hostname=%s\n",
			__func__, opt_arg_sbatch, opt_arg_srun, hostname);
		fclose(fp);
	}

	slurm_info("%s: opt_arg_sbatch=%d opt_arg_srun=%d hostname=%s out_file=%s",
		   __func__, opt_arg_sbatch, opt_arg_srun, hostname,
		   opt_out_file);

	return 0;
}
