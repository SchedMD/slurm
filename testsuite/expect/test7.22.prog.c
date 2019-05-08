/*****************************************************************************\
 *  prog7.22.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
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

/*
 * All spank plugins must define this macro for the Slurm plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

/*
 *  Provide a --test_suite=[opt_arg] option to sbatch:
 */
struct spank_option spank_options[] =
{
	{ "test_suite_prolog",
	  "[opt_arg_sbatch]",
	  "Registered component of slurm test suite.",
	  2,
	  0,
	  NULL
	},
	SPANK_OPTIONS_TABLE_END
};

/*  Called from both srun and slurmd */
int slurm_spank_init(spank_t sp, int ac, char **av)
{
	spank_context_t context;

	context = spank_context();
	if (spank_option_register(sp, spank_options) != ESPANK_SUCCESS)
		slurm_error("spank_option_register error");

	return (0);
}
