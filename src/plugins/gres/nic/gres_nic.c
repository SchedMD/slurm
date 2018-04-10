/*****************************************************************************\
 *  gres_nic.c - Support NICs as a generic resources.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
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

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/xcgroup_read_config.c"
#include "src/common/xstring.h"

#include "../common/gres_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char	plugin_name[]		= "Gres NIC plugin";
const char	plugin_type[]		= "gres/nic";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static char	gres_name[]		= "nic";

static List gres_devices = NULL;

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job)
{
	char *global_list = NULL, *local_list = NULL;
	char *slurm_env_var = NULL;

	if (is_job)
			slurm_env_var = "SLURM_JOB_NICS";
	else
			slurm_env_var = "SLURM_STEP_NICS";

	if (*already_seen) {
		global_list = xstrdup(getenvp(*env_ptr, slurm_env_var));
		local_list = xstrdup(getenvp(*env_ptr,
					     "OMPI_MCA_btl_openib_if_include"));
	}

	common_gres_set_env(gres_devices, env_ptr, gres_ptr, node_inx,
			    usable_gres, "mlx4_", local_inx,
			    &local_list, &global_list,
			    reset, is_job);

	if (global_list) {
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	}

	if (local_list) {
		env_array_overwrite(
			env_ptr, "OMPI_MCA_btl_openib_if_include", local_list);
		xfree(local_list);
		*already_seen = true;
	}
}

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
	FREE_NULL_LIST(gres_devices);

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int node_config_load(List gres_conf_list)
{
	int rc = SLURM_SUCCESS;

	if (gres_devices)
		return rc;

	rc = common_node_config_load(gres_conf_list, gres_name,
				     &gres_devices);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);

	return rc;
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void job_set_env(char ***job_env_ptr, void *gres_ptr, int node_inx)
{
	/*
	 * Variables are not static like in step_*_env since we could be calling
	 * this from the slurmd where we are dealing with a different job each
	 * time we hit this function, so we don't want to keep track of other
	 * unrelated job's status.  This can also get called multiple times
	 * (different prologs and such) which would also result in bad info each
	 * call after the first.
	 */
	int local_inx = 0;
	bool already_seen = false;

	_set_env(job_env_ptr, gres_ptr, node_inx, NULL,
		 &already_seen, &local_inx, false, true);
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void step_set_env(char ***step_env_ptr, void *gres_ptr)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, NULL,
		 &already_seen, &local_inx, false, false);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one tasks)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void step_reset_env(char ***step_env_ptr, void *gres_ptr,
			   bitstr_t *usable_gres)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, usable_gres,
		 &already_seen, &local_inx, true, false);
}

/* Send GRES information to slurmstepd on the specified file descriptor*/
extern void send_stepd(int fd)
{
	common_send_stepd(fd, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(int fd)
{
	common_recv_stepd(fd, &gres_devices);
}

extern int job_info(gres_job_state_t *job_gres_data, uint32_t node_inx,
		     enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

extern int step_info(gres_step_state_t *step_gres_data, uint32_t node_inx,
		     enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

extern List get_devices(void)
{
	return gres_devices;
}
