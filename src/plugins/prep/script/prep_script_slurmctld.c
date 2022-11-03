/*****************************************************************************\
 *  prep_script_slurmctld.c - PrologSlurmctld / EpilogSlurmctld handling
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/interfaces/prep.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"

#include "prep_script.h"

static char **_build_env(job_record_t *job_ptr, bool is_epilog);

extern void slurmctld_script(job_record_t *job_ptr, bool is_epilog)
{
	char **my_env;

	my_env = _build_env(job_ptr, is_epilog);
	if (!is_epilog)
		slurmscriptd_run_prepilog(job_ptr->job_id, is_epilog,
					 slurm_conf.prolog_slurmctld, my_env);
	else
		slurmscriptd_run_prepilog(job_ptr->job_id, is_epilog,
					  slurm_conf.epilog_slurmctld, my_env);

	for (int i = 0; my_env[i]; i++)
		xfree(my_env[i]);
	xfree(my_env);
}

static char **_build_env(job_record_t *job_ptr, bool is_epilog)
{
	char **my_env;

	my_env = job_common_env_vars(job_ptr, is_epilog);
	setenvf(&my_env, "SLURM_SCRIPT_CONTEXT", "%s_slurmctld",
		is_epilog ? "epilog" : "prolog");

	return my_env;
}
