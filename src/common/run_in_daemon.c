/*****************************************************************************\
 *  run_in_daemon.c - functions to determine if you are a given daemon or not
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/log.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

strong_alias(run_in_daemon, slurm_run_in_daemon);
strong_alias(running_in_daemon, slurm_running_in_daemon);
strong_alias(running_in_slurmctld, slurm_running_in_slurmctld);
strong_alias(running_in_slurmd, slurm_running_in_slurmd);
strong_alias(running_in_slurmdbd, slurm_running_in_slurmdbd);
strong_alias(running_in_slurmd_stepd, slurm_running_in_slurmd_stepd);
strong_alias(running_in_slurmrestd, slurm_running_in_slurmrestd);
strong_alias(running_in_slurmstepd, slurm_running_in_slurmstepd);

extern bool run_in_daemon(bool *run, bool *set, char *daemons)
{
	char *full, *start_char, *end_char;

	if (*set)
		return *run;

	xassert(slurm_prog_name);

	*set = true;

	if (!xstrcmp(daemons, slurm_prog_name))
		return *run = true;

	full = xstrdup(daemons);
	start_char = full;

	while (start_char && (end_char = strstr(start_char, ","))) {
		*end_char = 0;
		if (!xstrcmp(start_char, slurm_prog_name)) {
			xfree(full);
			return *run = true;
		}

		start_char = end_char + 1;
	}

	if (start_char && !xstrcmp(start_char, slurm_prog_name)) {
		xfree(full);
		return *run = true;
	}

	xfree(full);

	return *run = false;
}

extern bool running_in_daemon(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set,
			     "slurmctld,slurmd,slurmdbd,slurmstepd,slurmrestd");
}

static bool _running_in_slurmctld(bool reset)
{
	static bool run = false, set = false;

	if (reset)
		set = run = false;

	return run_in_daemon(&run, &set, "slurmctld");
}

extern bool running_in_slurmctld(void)
{
	return _running_in_slurmctld(false);
}

extern bool running_in_slurmctld_reset(void)
{
	return _running_in_slurmctld(true);
}

extern bool running_in_slurmd(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set, "slurmd");
}

extern bool running_in_slurmdbd(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set, "slurmdbd");
}

extern bool running_in_slurmd_stepd(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set, "slurmd,slurmstepd");
}

extern bool running_in_slurmrestd(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set, "slurmrestd");
}

extern bool running_in_slurmstepd(void)
{
	static bool run = false, set = false;
	return run_in_daemon(&run, &set, "slurmstepd");
}
