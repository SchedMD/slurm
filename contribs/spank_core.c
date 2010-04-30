/*****************************************************************************
 *
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  An option --core=<arg> is added for the srun command. 
 *  Valid arguments are normal, light, lcb and list.
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/spank.h>

#define CORE_INVALID -1
#define CORE_NORMAL 0
#define CORE_LIGHT  1 /* Default lightweight corefile from liblwcf */
#define CORE_LCB    2 /* PTOOLS Lightweight Corefile Browser (LCB) compliant*/
#define CORE_LIST   3 /* List core format types to stdout and exit */
#define LIB_LIGHT   "liblwcf-preload.so"

struct core_format_info {
	int type;
	const char *name;
	const char *descr;
};

/*
 * Supported types for core=%s
 */
struct core_format_info core_types[] = {
	{ CORE_NORMAL,
	  "normal",
	  "Default full corefile (do nothing)"
        },
	{ CORE_LIGHT,
	  "light",
	  "liblwcf default lightweight corefile format"
        },
	{ CORE_LCB,
	  "lcb",
	  "liblwcf Lightweight Corefile Browser compliant"
	},
	{ CORE_LIST,
	  "list",
	  "list valid core format types"
	},
	{ CORE_INVALID,
	  NULL,
	  "Invalid format"
	}
};

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(core, 1)

static int core_mode = CORE_NORMAL;

static int _opt_process (int val, const char *optarg, int remote);

struct spank_option spank_option_array[] =
{
	{ "core", "format", "Core file format", 1, 0,
	  (spank_opt_cb_f) _opt_process },
	SPANK_OPTIONS_TABLE_END
};

static void _print_valid_core_types (void)
{
	struct core_format_info *ci;

	info ("Valid corefile format types:");
	for (ci = core_types; ci && ci->name; ci++) {
		if ((ci->type == CORE_LIGHT) ||
		    (ci->type == CORE_LCB)) {
			struct stat buf;
			if ((stat("/lib/"           LIB_LIGHT, &buf) < 0) &&
			    (stat("/usr/lib/"       LIB_LIGHT, &buf) < 0) &&
			    (stat("/usr/local/lib/" LIB_LIGHT, &buf) < 0))
				continue;
		}
		if (ci->type != CORE_LIST)
			info(" %-8s -- %s", ci->name, ci->descr);
	}
	return;
}

static int _opt_process (int val, const char *optarg, int remote)
{
	int i;
	struct core_format_info *ci;

	for (ci = core_types; ci && ci->name; ci++) {
		if (strcasecmp(optarg, ci->name))
			continue;
		core_mode = ci->type;
		if (core_mode == CORE_LIST) {
			_print_valid_core_types();
			exit(0);
		}
		return ESPANK_SUCCESS;
	}

	slurm_error("Invalid core option: %s", optarg);
	exit(-1);
}

int slurm_spank_init(spank_t sp, int ac, char **av)
{
	spank_context_t context;
	int i, j, rc = ESPANK_SUCCESS;
	char *core_env;

	for (i=0; spank_option_array[i].name; i++) {
		j = spank_option_register(sp, &spank_option_array[i]);
		if (j != ESPANK_SUCCESS) {
			slurm_error("Could not register Spank option %s",
				    spank_option_array[i].name);
			rc = j;
		}
	}

	context = spank_context();
	if (context == S_CTX_LOCAL) {
		core_env = getenv("SLURM_CORE_FORMAT");
		if (core_env)
			rc = _opt_process(0, core_env, 0);
	}

	return rc;
}

int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
	spank_context_t context;
	int rc = ESPANK_SUCCESS;

	context = spank_context();
	if (context != S_CTX_LOCAL)
		return rc;

	switch (core_mode) {
	case CORE_NORMAL:
	case CORE_INVALID:
		break;
	case CORE_LCB:
		setenvfs("LWCF_CORE_FORMAT=LCB");
	case CORE_LIGHT:
		setenvfs("LD_PRELOAD=" LIB_LIGHT);
		break;
	}

	return rc;
}
