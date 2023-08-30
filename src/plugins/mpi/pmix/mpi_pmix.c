/*****************************************************************************\
 **  mpi_pmix.c - Main plugin callbacks for PMIx support in Slurm
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2020 Mellanox Technologies. All rights reserved.
 *  Written by Artem Y. Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *  Copyright (C) 2020      Siberian State University of Telecommunications
 *                          and Information Sciences (SibSUTIS).
 *                          All rights reserved.
 *  Written by Boris Bochkarev <boris-bochkaryov@yandex.ru>.
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

#if (HAVE_PMIX_VER == 2)
const char plugin_type[] = "mpi/pmix_v2";
const uint32_t plugin_id = MPI_PLUGIN_PMIX2;
#elif (HAVE_PMIX_VER == 3)
const char plugin_type[] = "mpi/pmix_v3";
const uint32_t plugin_id = MPI_PLUGIN_PMIX3;
#elif (HAVE_PMIX_VER == 4)
const char plugin_type[] = "mpi/pmix_v4";
const uint32_t plugin_id = MPI_PLUGIN_PMIX4;
#elif (HAVE_PMIX_VER == 5)
const char plugin_type[] = "mpi/pmix_v5";
const uint32_t plugin_id = MPI_PLUGIN_PMIX5;
#endif

const uint32_t plugin_version = SLURM_VERSION_NUMBER;

void *libpmix_plug = NULL;

char *process_mapping = NULL;

s_p_options_t pmix_options[] = {
	{"PMIxCliTmpDirBase", S_P_STRING},
	{"PMIxCollFence", S_P_STRING},
	{"PMIxDebug", S_P_UINT32},
	{"PMIxDirectConn", S_P_BOOLEAN},
	{"PMIxDirectConnEarly", S_P_BOOLEAN},
	{"PMIxDirectConnUCX", S_P_BOOLEAN},
	{"PMIxDirectSameArch", S_P_BOOLEAN},
	{"PMIxEnv", S_P_STRING},
	{"PMIxFenceBarrier", S_P_BOOLEAN},
	{"PMIxNetDevicesUCX", S_P_STRING},
	{"PMIxTimeout", S_P_UINT32},
	{"PMIxTlsUCX", S_P_STRING},
	{NULL}
};
slurm_pmix_conf_t slurm_pmix_conf;

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
#elif defined PMIXP_V3_LIBPATH
	xstrfmtcat(full_path, "%s/", PMIXP_V3_LIBPATH);
#elif defined PMIXP_V4_LIBPATH
	xstrfmtcat(full_path, "%s/", PMIXP_V4_LIBPATH);
#elif defined PMIXP_V5_LIBPATH
	xstrfmtcat(full_path, "%s/", PMIXP_V5_LIBPATH);
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

static void _init_pmix_conf(void)
{
	slurm_pmix_conf.cli_tmpdir_base = NULL;
	slurm_pmix_conf.coll_fence = NULL;
	slurm_pmix_conf.debug = 0;
	slurm_pmix_conf.direct_conn = true;
	slurm_pmix_conf.direct_conn_early = false;
	slurm_pmix_conf.direct_conn_ucx = false;
	slurm_pmix_conf.direct_samearch = false;
	slurm_pmix_conf.env = NULL;
	slurm_pmix_conf.fence_barrier = false;
	slurm_pmix_conf.timeout = PMIXP_TIMEOUT_DEFAULT;
	slurm_pmix_conf.ucx_netdevices = NULL;
	slurm_pmix_conf.ucx_tls = NULL;
}

static void _reset_pmix_conf(void)
{
	xfree(slurm_pmix_conf.cli_tmpdir_base);
	xfree(slurm_pmix_conf.coll_fence);
	slurm_pmix_conf.debug = 0;
	slurm_pmix_conf.direct_conn = true;
	slurm_pmix_conf.direct_conn_early = false;
	slurm_pmix_conf.direct_conn_ucx = false;
	slurm_pmix_conf.direct_samearch = false;
	xfree(slurm_pmix_conf.env);
	slurm_pmix_conf.fence_barrier = false;
	slurm_pmix_conf.timeout = PMIXP_TIMEOUT_DEFAULT;
	xfree(slurm_pmix_conf.ucx_netdevices);
	xfree(slurm_pmix_conf.ucx_tls);
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
		return SLURM_ERROR;
	}
	_init_pmix_conf();
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	PMIXP_DEBUG("%s: call fini()", pmixp_info_hostname());
	pmixp_agent_stop();
	pmixp_stepd_finalize();
	_libpmix_close(libpmix_plug);
	_reset_pmix_conf();

	return SLURM_SUCCESS;
}

extern int mpi_p_slurmstepd_prefork(const stepd_step_rec_t *step, char ***env)
{
	int ret;
	pmixp_debug_hang(0);
	PMIXP_DEBUG("start");

	if (step->batch)
		return SLURM_SUCCESS;

	if (SLURM_SUCCESS != (ret = pmixp_stepd_init(step, env))) {
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
	slurm_kill_job_step(step->step_id.job_id,
			    step->step_id.step_id, SIGKILL);
	return ret;
}

extern int mpi_p_slurmstepd_task(const mpi_task_info_t *mpi_task, char ***env)
{
	char **tmp_env = NULL;
	pmixp_debug_hang(0);

	PMIXP_DEBUG("Patch environment for task %d", mpi_task->gtaskid);

	pmixp_lib_setup_fork(
		mpi_task->gtaskid, pmixp_info_namespace(), &tmp_env);
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

extern mpi_plugin_client_state_t *
mpi_p_client_prelaunch(const mpi_step_info_t *mpi_step, char ***env)
{
	static pthread_mutex_t setup_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t setup_cond  = PTHREAD_COND_INITIALIZER;
	static bool setup_done = false;
	uint32_t nnodes, ntasks, **tids;
	uint16_t *task_cnt;
	int ret;

	if ((ret = pmixp_abort_agent_start(env)) != SLURM_SUCCESS) {
		PMIXP_ERROR("pmixp_abort_agent_start() failed %d", ret);
		return NULL;
	}

	PMIXP_DEBUG("setup process mapping in srun");
	if ((mpi_step->het_job_id == NO_VAL) ||
	    (mpi_step->het_job_task_offset == 0)) {
		nnodes = mpi_step->step_layout->node_cnt;
		ntasks = mpi_step->step_layout->task_cnt;
		task_cnt = mpi_step->step_layout->tasks;
		tids = mpi_step->step_layout->tids;
		process_mapping = pack_process_mapping(nnodes, ntasks,
						       task_cnt, tids);
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

	if (!process_mapping) {
		PMIXP_ERROR("Cannot create process mapping");
		return NULL;
	}
	setenvf(env, PMIXP_SLURM_MAPPING_ENV, "%s", process_mapping);

	/* only return NULL on error */
	return (void *)0xdeadbeef;
}

extern int mpi_p_client_fini(void)
{
	xfree(process_mapping);
	return pmixp_abort_agent_stop();
}

extern void mpi_p_conf_options(s_p_options_t **full_options, int *full_opt_cnt)
{
	transfer_s_p_options(full_options, pmix_options, full_opt_cnt);
}

extern void mpi_p_conf_set(s_p_hashtbl_t *tbl)
{
	_reset_pmix_conf();

	if (tbl) {
		s_p_get_string(&slurm_pmix_conf.cli_tmpdir_base,
			       "PMIxCliTmpDirBase", tbl);
		s_p_get_string(
			&slurm_pmix_conf.coll_fence, "PMIxCollFence", tbl);
		s_p_get_uint32(&slurm_pmix_conf.debug, "PMIxDebug", tbl);
		s_p_get_boolean(
			&slurm_pmix_conf.direct_conn,"PMIxDirectConn", tbl);
		s_p_get_boolean(&slurm_pmix_conf.direct_conn_early,
				"PMIxDirectConnEarly", tbl);
		s_p_get_boolean(&slurm_pmix_conf.direct_conn_ucx,
				"PMIxDirectConnUCX", tbl);
		s_p_get_boolean(&slurm_pmix_conf.direct_samearch,
				"PMIxDirectSameArch", tbl);
		s_p_get_string(&slurm_pmix_conf.env, "PMIxEnv", tbl);
		s_p_get_boolean(&slurm_pmix_conf.fence_barrier,
				"PMIxFenceBarrier", tbl);
		s_p_get_string(&slurm_pmix_conf.ucx_netdevices,
			       "PMIxNetDevicesUCX", tbl);
		s_p_get_uint32(&slurm_pmix_conf.timeout, "PMIxTimeout", tbl);
		s_p_get_string(&slurm_pmix_conf.ucx_tls, "PMIxTlsUCX", tbl);
	}
}

extern s_p_hashtbl_t *mpi_p_conf_get(void)
{
	s_p_hashtbl_t *tbl = s_p_hashtbl_create(pmix_options);
	char *value;

	if (slurm_pmix_conf.cli_tmpdir_base)
		s_p_parse_pair(tbl, "PMIxCliTmpDirBase",
			       slurm_pmix_conf.cli_tmpdir_base);

	if (slurm_pmix_conf.coll_fence)
		s_p_parse_pair(tbl, "PMIxCollFence",
			       slurm_pmix_conf.coll_fence);

	value = xstrdup_printf("%u", slurm_pmix_conf.debug);
	s_p_parse_pair(tbl, "PMIxDebug", value);
	xfree(value);

	s_p_parse_pair(tbl, "PMIxDirectConn",
		       (slurm_pmix_conf.direct_conn ? "yes" : "no"));

	s_p_parse_pair(tbl, "PMIxDirectConnEarly",
		       (slurm_pmix_conf.direct_conn_early ? "yes" : "no"));

	s_p_parse_pair(tbl, "PMIxDirectConnUCX",
		       (slurm_pmix_conf.direct_conn_ucx ? "yes" : "no"));

	s_p_parse_pair(tbl, "PMIxDirectSameArch",
		       (slurm_pmix_conf.direct_samearch ? "yes" : "no"));

	if(slurm_pmix_conf.env)
		s_p_parse_pair(tbl, "PMIxEnv", slurm_pmix_conf.env);

	s_p_parse_pair(tbl, "PMIxFenceBarrier",
		       (slurm_pmix_conf.fence_barrier ? "yes" : "no"));

	if (slurm_pmix_conf.ucx_netdevices)
		s_p_parse_pair(tbl, "PMIxNetDevicesUCX",
			       slurm_pmix_conf.ucx_netdevices);

	value = xstrdup_printf("%u", slurm_pmix_conf.timeout);
	s_p_parse_pair(tbl, "PMIxTimeout", value);
	xfree(value);

	if (slurm_pmix_conf.ucx_tls)
		s_p_parse_pair(tbl, "PMIxTlsUCX", slurm_pmix_conf.ucx_tls);

	return tbl;
}

extern List mpi_p_conf_get_printable(void)
{
	config_key_pair_t *key_pair;
	List data = list_create(destroy_config_key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxCliTmpDirBase");
	key_pair->value = xstrdup(slurm_pmix_conf.cli_tmpdir_base);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxCollFence");
	key_pair->value = xstrdup(slurm_pmix_conf.coll_fence);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxDebug");
	key_pair->value = xstrdup_printf("%u", slurm_pmix_conf.debug);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxDirectConn");
	key_pair->value = xstrdup(slurm_pmix_conf.direct_conn ? "yes" : "no");
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxDirectConnEarly");
	key_pair->value = xstrdup(slurm_pmix_conf.direct_conn_early ?
				  "yes" : "no");
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxDirectConnUCX");
	key_pair->value = xstrdup(slurm_pmix_conf.direct_conn_ucx ?
				  "yes" : "no");
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxDirectSameArch");
	key_pair->value = xstrdup(slurm_pmix_conf.direct_samearch ?
				  "yes" : "no");
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxEnv");
	key_pair->value = xstrdup(slurm_pmix_conf.env);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxFenceBarrier");
	key_pair->value = xstrdup(slurm_pmix_conf.fence_barrier ? "yes" : "no");
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxNetDevicesUCX");
	key_pair->value = xstrdup(slurm_pmix_conf.ucx_netdevices);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxTimeout");
	key_pair->value = xstrdup_printf("%u", slurm_pmix_conf.timeout);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("PMIxTlsUCX");
	key_pair->value = xstrdup(slurm_pmix_conf.ucx_tls);
	list_append(data, key_pair);

	return data;
}
