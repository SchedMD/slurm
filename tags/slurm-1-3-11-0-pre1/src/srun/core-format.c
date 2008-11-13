/*****************************************************************************\
 * src/srun/core-format.c - Change corefile characteristics for job
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under 
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

//#include "src/common/env.h" 
#include "src/srun/core-format.h"
#include "src/common/log.h"
#include "src/common/env.h"

#define CORE_NORMAL 0
#define CORE_LIGHT  1 /* Default lightweight corefile from liblwcf */
#define CORE_LCB    2 /* PTOOLS Lightweight Corefile Browser (LCB) compliant*/
#define CORE_LIST   3 /* List core format types to stdout and exit */
#define LIB_LIGHT   "liblwcf-preload.so"

struct core_format_info {
	core_format_t type;
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

static struct core_format_info * _find_core_format_info (const char *name)
{
	struct core_format_info *ci;

	for (ci = core_types; ci && ci->name != NULL; ci++) {
		if ( strncasecmp (ci->name, name, strlen (ci->name)) == 0)
			break;
	}

	return (ci);
}

static void _print_valid_core_types (void)
{
	struct core_format_info *ci;

	info ("Valid corefile format types:");
	for (ci = core_types; ci && ci->name != NULL; ci++) {
		if ((ci->type == CORE_LIGHT) ||
		    (ci->type == CORE_LCB)) {
			struct stat buf;
			if ((stat("/lib/"           LIB_LIGHT, &buf) < 0) &&
			    (stat("/usr/lib/"       LIB_LIGHT, &buf) < 0) &&
			    (stat("/usr/local/lib/" LIB_LIGHT, &buf) < 0))
				continue;
		}
		if (ci->type != CORE_LIST) 
			info (" %-8s -- %s", ci->name, ci->descr);
	}
	return;
}

core_format_t core_format_type (const char *str)
{
	struct core_format_info *ci = _find_core_format_info (str);

	if (ci->type == CORE_LIST) {
		_print_valid_core_types ();
		exit (0);
	}

	return (ci->type);
}

const char * core_format_name (core_format_t type)
{
	struct core_format_info *ci;

	for (ci = core_types; ci && ci->name != NULL; ci++) {
		if (ci->type == type)
			break;
	}

	return (ci->name);
}

int core_format_enable (core_format_t type)
{
	switch (type) {
	case CORE_NORMAL: case CORE_INVALID:
		break;
	case CORE_LCB:
		setenvfs ("LWCF_CORE_FORMAT=LCB");
	case CORE_LIGHT:
		setenvfs ("LD_PRELOAD=" LIB_LIGHT);
		break;
	}

	return (0);
}

