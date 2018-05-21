/*****************************************************************************\
 **  mpi_pmi2.c - Library routines for initiating MPI jobs using PMI2.
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
 *  Portions copyright (C) 2015 Mellanox Technologies Inc.
 *  Written by Artem Polyakov <artemp@mellanox.com>.
 *  All rights reserved.
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

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_mpi.h"

#include "setup.h"
#include "agent.h"
#include "spawn.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for Slurm switch) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "mpi PMI2 plugin";
const char plugin_type[]        = "mpi/pmi2";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*
 * The following is executed in slurmstepd.
 */

int p_mpi_hook_slurmstepd_prefork(const stepd_step_rec_t *job,
				  char ***env)
{
	int rc;

	debug("using mpi/pmi2");

	if (job->batch)
		return SLURM_SUCCESS;

	rc = pmi2_setup_stepd(job, env);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (pmi2_start_agent() < 0) {
		error ("mpi/pmi2: failed to create pmi2 agent thread");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int p_mpi_hook_slurmstepd_task (const mpi_plugin_task_info_t *job,
				char ***env)
{
	int i;

	env_array_overwrite_fmt(env, "PMI_FD", "%u",
				TASK_PMI_SOCK(job->ltaskid));

	env_array_overwrite_fmt(env, "PMI_JOBID", "%s",
				job_info.pmi_jobid);
	env_array_overwrite_fmt(env, "PMI_RANK", "%u", job->gtaskid);
	env_array_overwrite_fmt(env, "PMI_SIZE", "%u", job->ntasks);
	if (job_info.spawn_seq) { /* PMI1.1 needs this env-var */
		env_array_overwrite_fmt(env, "PMI_SPAWNED", "%u", 1);
	}
	/* close unused sockets in task */
	close(tree_sock);
	tree_sock = 0;
	for (i = 0; i < job->ltasks; i ++) {
		close(STEPD_PMI_SOCK(i));
		STEPD_PMI_SOCK(i) = 0;
		if (i != job->ltaskid) {
			close(TASK_PMI_SOCK(i));
			TASK_PMI_SOCK(i) = 0;
		}
	}
	return SLURM_SUCCESS;
}


/*
 * The following is executed in srun.
 */

mpi_plugin_client_state_t *
p_mpi_hook_client_prelaunch(mpi_plugin_client_info_t *job, char ***env)
{
	int rc;

	debug("mpi/pmi2: client_prelaunch");

	rc = pmi2_setup_srun(job, env);
	if (rc != SLURM_SUCCESS) {
		return NULL;
	}

	if (pmi2_start_agent() < 0) {
		error("failed to start PMI2 agent thread");
		return NULL;
	}

	return (void *)0x12345678;
}

int p_mpi_hook_client_fini(mpi_plugin_client_state_t *state)
{

	pmi2_stop_agent();

	/* the job may be allocated by this srun.
	 * or exit of this srun may cause the job script to exit.
	 * wait for the spawned steps. */
	spawn_job_wait();

	return SLURM_SUCCESS;
}

extern int fini()
{
	/* cleanup after ourself */
	pmi2_stop_agent();
	pmi2_cleanup_stepd();
	return 0;
}
