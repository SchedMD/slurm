/*****************************************************************************\
 *  prog7.11.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

static int opt_arg = 0;
static char *opt_out_file = NULL;

static int _test_opt_process(int val, const char *optarg, int remote);

/*
 *  Provide a --renice=[prio] option to srun:
 */
struct spank_option spank_options[] =
{
	{ "test_suite", "[opt_arg]", "Component of slurm test suite.", 2, 0,
		_test_opt_process
	},
	SPANK_OPTIONS_TABLE_END
};

static int _test_opt_process(int val, const char *optarg, int remote)
{
	if (!remote)
		slurm_info("_test_opt_process: test_suite: opt_arg=%s", optarg);

	return (0);
}

/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
	if (ac == 1)
		opt_out_file = strdup(av[0]);

	if (!spank_remote (sp) && opt_out_file)
		slurm_info("slurm_spank_init: opt_out_file=%s", opt_out_file);

	return (0);
}


int slurm_spank_task_post_fork (spank_t sp, int ac, char **av)
{
	return (0);
}
