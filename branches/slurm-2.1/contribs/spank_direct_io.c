/*****************************************************************************
 *
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Morris Jette <jette1@llnl.gov>.
 *
 *  UCRL-CODE-235358
 * 
 *  This file is part of chaos-spankings, a set of spank plugins for SLURM.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************
 * Two options are added for salloc, sbatch, and srun: --cache-io and
 * --direct-io. These options will set a SPANK_DIRECT_IO environment variable
 * for the job's Prolog and Epilog scripts. If neither option (or their
 * corresponding environment variables) are set, then SPANK_DIRECT_IO
 * will not exist. NOTE: Command line options take precidence over the 
 * environment variables.
 *
 * --cache-io  or SLURM_CACHE_IO  env var will set SPANK_DIRECT_IO=0
 * --direct-io or SLURM_DIRECT_IO env var will set SPANK_DIRECT_IO=1
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(direct-io, 1)

#define CACHE_IO	0x1
#define DIRECT_IO	0x2

static int io_style = 0;

static int _opt_process (int val, const char *optarg, int remote);

/*
 *  Provide a --cache-io/--direct-io option for srun:
 */
struct spank_option spank_option_array[] =
{
	{ "cache-io",     NULL, "Cache I/O", 
		0, CACHE_IO, (spank_opt_cb_f) _opt_process },
	{ "direct-io",    NULL, "Write I/O directly to disk, without caching", 
		0, DIRECT_IO,  (spank_opt_cb_f) _opt_process },
	SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
	int i, j, rc = ESPANK_SUCCESS;

	for (i=0; spank_option_array[i].name; i++) {
		j = spank_option_register(sp, &spank_option_array[i]);
		if (j != ESPANK_SUCCESS) {
			slurm_error("Could not register Spank option %s",
				    spank_option_array[i].name);
			rc = j;
		}
	}

	return rc;
}

/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
	int rc = ESPANK_SUCCESS;

	if (spank_remote (sp))
		return (0);

	if (io_style == CACHE_IO) {
		slurm_debug("cache_io option");
		rc = spank_set_job_env("O_DIRECT", "0", 1);
	} else if (io_style == DIRECT_IO) {
		slurm_debug("direct_io option");
		rc = spank_set_job_env("O_DIRECT", "1", 1);
	} else if (getenv("SLURM_CACHE_IO")) {
		slurm_debug("cache_io env var");
		rc = spank_set_job_env("O_DIRECT", "0", 1);
	} else if (getenv("SLURM_DIRECT_IO")) {
		slurm_debug("direct_io env var");
		rc = spank_set_job_env("O_DIRECT", "1", 1);
	}
	if (rc != ESPANK_SUCCESS)
		slurm_error("spank_setjob_env: %s", spank_strerror(rc));

	return (0);
}

static int _opt_process (int val, const char *optarg, int remote)
{
	io_style = val;
	return (0);
}
