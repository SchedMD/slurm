/*****************************************************************************\
 **  mpi_pmix.c - Main plugin callbacks for PMIx support in SLURM
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Y. Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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
#include <dlfcn.h>

#include "pmixp_common.h"
#include "pmixp_server.h"
#include "pmixp_debug.h"
#include "pmixp_agent.h"
#include "pmixp_info.h"
#include "pmixp_dconn_ucx.h"
#include "pmixp_client.h"

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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various Slurm versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[] = "PMIx plugin";

#if (HAVE_PMIX_VER == 1)
const char plugin_type[] = "mpi/pmix_v1";
#elif (HAVE_PMIX_VER == 2)
const char plugin_type[] = "mpi/pmix_v2";
#elif (HAVE_PMIX_VER == 3)
const char plugin_type[] = "mpi/pmix_v3";
#endif

const uint32_t plugin_version = SLURM_VERSION_NUMBER;

void *libpmix_plug = NULL;

static void _libpmix_close(void *lib_plug)
{
	xassert(lib_plug);
	dlclose(lib_plug);
}

static void *_libpmix_open(void)
{
	void *lib_plug = NULL;
	char *full_path = NULL;

#ifdef PMIXP_V1_LIBPATH
	xstrfmtcat(full_path, "%s/", PMIXP_V1_LIBPATH);
#elif defined PMIXP_V2_LIBPATH
	xstrfmtcat(full_path, "%s/", PMIXP_V2_LIBPATH);
#endif
	xstrfmtcat(full_path, "libpmix.so");
	lib_plug = dlopen(full_path, RTLD_LAZY | RTLD_GLOBAL);
	xfree(full_path);

	if (lib_plug && (HAVE_PMIX_VER != pmixp_lib_get_version())) {
		PMIXP_ERROR("pmi/pmix: incorrect PMIx library version loaded %d was loaded, required %d version",
			    pmixp_lib_get_version(), (int)HAVE_PMIX_VER);
		_libpmix_close(lib_plug);
		lib_plug = NULL;
	}

	return lib_plug;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	libpmix_plug = _libpmix_open();
	if (!libpmix_plug) {
		PMIXP_ERROR("pmi/pmix: can not load PMIx library");
		return SLURM_FAILURE;
	}
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	PMIXP_DEBUG("%s: call fini()", pmixp_info_hostname());
	pmixp_agent_stop();
	pmixp_stepd_finalize();
	_libpmix_close(libpmix_plug);
	return SLURM_SUCCESS;
}

extern int p_mpi_hook_slurmstepd_prefork(
	const stepd_step_rec_t *job, char ***env)
{
	int ret;
	pmixp_debug_hang(0);
	PMIXP_DEBUG("start");

	if (job->batch)
		return SLURM_SUCCESS;

	if (SLURM_SUCCESS != (ret = pmixp_stepd_init(job, env))) {
		PMIXP_ERROR("pmixp_stepd_init() failed");
		goto err_ext;
	}
	if (SLURM_SUCCESS != (ret = pmixp_agent_start())) {
		PMIXP_ERROR("pmixp_agent_start() failed");
		goto err_ext;
	}
	return SLURM_SUCCESS;

err_ext:
	/* Abort the whole job if error! */
	slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
	return ret;
}

extern int p_mpi_hook_slurmstepd_task(
	const mpi_plugin_task_info_t *job, char ***env)
{
	char **tmp_env = NULL;
	pmixp_debug_hang(0);

	PMIXP_DEBUG("Patch environment for task %d", job->gtaskid);

	pmixp_lib_setup_fork(job->gtaskid, pmixp_info_namespace(), &tmp_env);
	if (NULL != tmp_env) {
		int i;
		for (i = 0; NULL != tmp_env[i]; i++) {
			char *value = strchr(tmp_env[i], '=');
			if (NULL != value) {
				*value = '\0';
				value++;
				env_array_overwrite(env,
						    (const char *)tmp_env[i],
						    value);
			}
			free(tmp_env[i]);
		}
		free(tmp_env);
		tmp_env = NULL;
	}
	return SLURM_SUCCESS;
}

extern mpi_plugin_client_state_t *p_mpi_hook_client_prelaunch(
	const mpi_plugin_client_info_t *job, char ***env)
{
	static pthread_mutex_t setup_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t setup_cond  = PTHREAD_COND_INITIALIZER;
	static char *mapping = NULL;
	static bool setup_done = false;
	uint32_t nnodes, ntasks, **tids;
	uint16_t *task_cnt;

	PMIXP_DEBUG("setup process mapping in srun");
	if ((job->pack_jobid == NO_VAL) || (job->pack_jobid == job->jobid)) {
		nnodes = job->step_layout->node_cnt;
		ntasks = job->step_layout->task_cnt;
		task_cnt = job->step_layout->tasks;
		tids = job->step_layout->tids;
		mapping = pack_process_mapping(nnodes, ntasks, task_cnt, tids);
		slurm_mutex_lock(&setup_mutex);
		setup_done = true;
		slurm_cond_broadcast(&setup_cond);
		slurm_mutex_unlock(&setup_mutex);
	} else {
		slurm_mutex_lock(&setup_mutex);
		while (!setup_done)
			slurm_cond_wait(&setup_cond, &setup_mutex);
		slurm_mutex_unlock(&setup_mutex);
	}

	if (NULL == mapping) {
		PMIXP_ERROR("Cannot create process mapping");
		return NULL;
	}
	setenvf(env, PMIXP_SLURM_MAPPING_ENV, "%s", mapping);
	xfree(mapping);

	/* only return NULL on error */
	return (void *)0xdeadbeef;
}

extern int p_mpi_hook_client_fini(void)
{
	return SLURM_SUCCESS;
}
