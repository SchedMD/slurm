/****************************************************************************\
 *  spank_pbs.c - SPANK plugin to set PBS environment variables.
 *
 *  Note: The job_submit/pbs plugin establishes some environment
 *  variables for batch jobs to complement those configured here.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "slurm/spank.h"

SPANK_PLUGIN(pbs, 1);

int slurm_spank_task_init(spank_t sp, int ac, char **av)
{
	char val[30000];

	/* PBS_ACCOUNT is set in the job_submit/pbs plugin, but only for
	 * batch jobs that specify the job's account at job submit time. */

	/* Setting PBS_ENVIRONMENT causes Intel MPI to believe that
	 * it is running on a PBS system, which isn't the case here. */
#if 0
	/* PBS_ENVIRONMENT is set to PBS_BATCH in the job_submit/pbs plugin.
	 * Interactive jobs get PBS_ENVIRONMENT set here since it's environment
	 * never passes through the slurmctld daemon. */
	if (spank_getenv(sp, "PBS_ENVIRONMENT", val, sizeof(val)) !=
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_ENVIRONMENT", "PBS_INTERACTIVE", 1);
#endif

	if (spank_getenv(sp, "SLURM_ARRAY_JOB_ID", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_ARRAY_ID", val, 1);
	if (spank_getenv(sp, "SLURM_ARRAY_TASK_ID", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_ARRAY_INDEX", val, 1);

	if (getcwd(val, sizeof(val)))
		spank_setenv(sp, "PBS_JOBDIR", val, 1);

	if (spank_getenv(sp, "SLURM_JOB_ID", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_JOBID", val, 1);

	if (spank_getenv(sp, "SLURM_JOB_NAME", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_JOBNAME", val, 1);

	/* PBS_NODEFILE is not currently available, although such a file might
	 * be build based upon the SLURM_JOB_NODELIST environment variable */

	if (spank_getenv(sp, "SLURM_NODEID", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_NODENUM", val, 1);

	if (spank_getenv(sp, "HOME", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_HOME", val, 1);

	if (spank_getenv(sp, "HOST", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_HOST", val, 1);

	if (spank_getenv(sp, "LANG", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_LANG", val, 1);

	if (spank_getenv(sp, "LOGNAME", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_LOGNAME", val, 1);

	if (spank_getenv(sp, "MAIL", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_MAIL", val, 1);

	if (spank_getenv(sp, "PATH", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_PATH", val, 1);

	if (spank_getenv(sp, "QUEUE", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_QUEUE", val, 1);

	if (spank_getenv(sp, "SHELL", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_SHELL", val, 1);

	if (spank_getenv(sp, "SYSTEM", val, sizeof(val)) == ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_SYSTEM", val, 1);

	if (spank_getenv(sp, "SLURM_SUBMIT_DIR", val, sizeof(val)) ==
	    ESPANK_SUCCESS)
		spank_setenv(sp, "PBS_O_WORKDIR", val, 1);

	/* PBS_QUEUE is set in the job_submit/pbs plugin, but only for
	 * batch jobs that specify the job's partition at job submit time. */

	if (spank_getenv(sp, "SLURM_PROCID", val, sizeof(val)) ==
	    ESPANK_SUCCESS) {
		int i = atoi(val) + 1;
		snprintf(val, sizeof(val), "%d", i);
		spank_setenv(sp, "PBS_TASKNUM", val, 1);
	}

	return 0;
}
