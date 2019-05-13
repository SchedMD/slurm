/*****************************************************************************\
 *  test7.23.prog.c - Test time_str2secs parsing of different formats.
 *
 *  Usage: test7.23.prog
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

void _ct(const char *time_string, int value)
{
	int t = time_str2secs(time_string);

	if (t != value)
		fatal("check_time: %s -> %u != %u", time_string, t, value);
}

int main (int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	logopt.prefix_level = 1;
	log_init(xbasename(argv[0]), logopt, 0, NULL);
	logopt.stderr_level += 5;
	log_alter(logopt, 0, NULL);

	_ct("INVALID TIME", NO_VAL);
	_ct("-1", INFINITE);
	_ct("INFINITE", INFINITE);
	_ct("infinite", INFINITE);
	_ct("UNLIMITED", INFINITE);
	_ct("unlimited", INFINITE);
	_ct("LONG --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- INVALID TIME", NO_VAL);
	_ct("0", 0);
	_ct("60", 60*60);
	_ct("60:15", 60*60 + 15);
	_ct("60:0", 60*60);
	_ct("60:", NO_VAL);
	_ct("60:-10", NO_VAL);
	_ct("-60:10", NO_VAL);
	_ct("1:60:15", 1*60*60 + 60*60 + 15);
	_ct("2:60:15", 2*60*60 + 60*60 + 15);
	_ct("0:0:15", 15);
	_ct("0:60:0", 60*60);
	_ct("0:0:0", 0);
	_ct("-0:-0:-0", NO_VAL);
	_ct(" 0:0:0 ", NO_VAL); //TODO: should we trim()?
	_ct("0-1:60:15", 1*60*60 + 60*60 + 15);
	_ct("1-1:60:15", 1*60*60*24 + 1*60*60 + 60*60 + 15);
	_ct("365-1:60:15", 365*60*60*24 + 1*60*60 + 60*60 + 15);
	_ct("365-0:0:0", 365*60*60*24);
	/*
	 * ct("9999999-0:0:0", 365*60*60*24)
	 * doesn't work with 32-bit int (sets high bit)
	 * TODO: Ignoring this edge for now until time_t
	 */
	//_ct("9999999-0:0:0", NO_VAL);

	return 0;
}
