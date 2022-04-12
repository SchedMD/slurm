/*****************************************************************************\
 *  slurm_mpi.c - Generic MPI selector for Slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>

#include "src/common/env.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_mpi.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define _DEBUG 0

typedef struct slurm_mpi_ops {
	int (*client_fini)(mpi_plugin_client_state_t *state);
	mpi_plugin_client_state_t *(*client_prelaunch)(
		const mpi_plugin_client_info_t *job, char ***env);
	int (*slurmstepd_prefork)(const stepd_step_rec_t *job, char ***env);
	int (*slurmstepd_task)(const mpi_plugin_task_info_t *job, char ***env);
} slurm_mpi_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_mpi_ops_t.
 */
static const char *syms[] = {
	"mpi_p_client_fini",
	"mpi_p_client_prelaunch",
	"mpi_p_slurmstepd_prefork",
	"mpi_p_slurmstepd_task"
};

static slurm_mpi_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

#if _DEBUG
/* Debugging information is invaluable to debug heterogeneous step support */
static void _log_env(char **env)
{
#if _DEBUG > 1
	if (!env)
		return;

	for (int i = 0; env[i]; i++)
		info("%s", env[i]);
#endif
}

static void _log_step_rec(const stepd_step_rec_t *job)
{
	int i;

	info("STEPD_STEP_REC");
	info("%ps", &job->step_id);
	info("ntasks:%u nnodes:%u node_id:%u", job->ntasks, job->nnodes,
	     job->nodeid);
	info("node_tasks:%u", job->node_tasks);
	for (i = 0; i < job->node_tasks; i++)
		info("gtid[%d]:%u", i, job->task[i]->gtid);
	for (i = 0; i < job->nnodes; i++)
		info("task_cnts[%d]:%u", i, job->task_cnts[i]);

	if ((job->het_job_id != 0) && (job->het_job_id != NO_VAL))
		info("het_job_id:%u", job->het_job_id);

	if (job->het_job_offset != NO_VAL) {
		info("het_job_ntasks:%u het_job_nnodes:%u", job->het_job_ntasks,
		     job->het_job_nnodes);
		info("het_job_node_offset:%u het_job_task_offset:%u",
		     job->het_job_offset, job->het_job_task_offset);
		for (i = 0; i < job->het_job_nnodes; i++)
			info("het_job_task_cnts[%d]:%u", i,
			     job->het_job_task_cnts[i]);
		info("het_job_node_list:%s", job->het_job_node_list);
	}
}

static void _log_mpi_rec(const mpi_plugin_client_info_t *job)
{
	slurm_step_layout_t *layout = job->step_layout;

	info("MPI_PLUGIN_CLIENT_INFO");
	info("%ps", &job->step_id);
	if ((job->het_job_id != 0) && (job->het_job_id != NO_VAL)) {
		info("het_job_id:%u", job->het_job_id);
	}
	if (layout) {
		info("node_cnt:%u task_cnt:%u", layout->node_cnt,
		     layout->task_cnt);
		info("node_list:%s", layout->node_list);
		info("plane_size:%u task_dist:%u", layout->plane_size,
		     layout->task_dist);
		for (int i = 0; i < layout->node_cnt; i++) {
			info("tasks[%d]:%u", i, layout->tasks[i]);
			for (int j = 0; j < layout->tasks[i]; j++) {
				info("tids[%d][%d]:%u", i, j,
				     layout->tids[i][j]);
			}
		}
	}
}

static void _log_task_rec(const mpi_plugin_task_info_t *job)
{
	info("MPI_PLUGIN_TASK_INFO");
	info("%ps", &job->step_id);
	info("nnodes:%u node_id:%u", job->nnodes, job->nodeid);
	info("ntasks:%u local_tasks:%u", job->ntasks, job->ltasks);
	info("global_task_id:%u local_task_id:%u", job->gtaskid, job->ltaskid);
}
#endif

static int _mpi_fini_locked(void)
{
	int rc;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	return rc;
}

static int _mpi_init_locked(char **mpi_type)
{
	const char *plugin_type = "mpi";

	char *plugin_name = NULL;

	xassert(mpi_type);

#if _DEBUG
	info("%s: MPI: Type: %s", __func__, *mpi_type);
#else
	debug("MPI: Type: %s", *mpi_type);
#endif

	if (!slurm_conf.mpi_default) {
		error("MPI: No default type set.");
		return SLURM_ERROR;
	} else if (!*mpi_type)
		*mpi_type = xstrdup(slurm_conf.mpi_default);
	/*
	 * The openmpi plugin has been equivalent to none for a while.
	 * Translate so we can discard that duplicated no-op plugin.
	 */
	if (!xstrcmp(*mpi_type, "openmpi")) {
		xfree(*mpi_type);
		*mpi_type = xstrdup("none");
	}

	plugin_name = xstrdup_printf("%s/%s", plugin_type, *mpi_type);
	g_context = plugin_context_create(
		plugin_type, plugin_name, (void **)&ops, syms, sizeof(syms));
	xfree(plugin_name);

	if (g_context) {
		setenvf(NULL, "SLURM_MPI_TYPE", "%s", *mpi_type);
		init_run = true;
	} else {
		error("MPI: Cannot create context for %s", *mpi_type);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _mpi_init(char **mpi_type)
{
	int rc = SLURM_SUCCESS;

	if (init_run && g_context)
		return rc;

	slurm_mutex_lock(&context_lock);

	if (!g_context)
		rc = _mpi_init_locked(mpi_type);

	slurm_mutex_unlock(&context_lock);
	return rc;
}

extern int mpi_g_slurmstepd_init(char ***env)
{
	int rc = SLURM_SUCCESS;
	char *mpi_type;

	xassert(env);

	if (!(mpi_type = xstrdup(getenvp(*env, "SLURM_MPI_TYPE")))) {
		error("MPI: SLURM_MPI_TYPE environmental variable is not set.");
		rc = SLURM_ERROR;
		goto done;
	}

#if _DEBUG
	info("%s: MPI: Environment before call:", __func__);
	_log_env(*env);
#endif

	if ((rc = _mpi_init(&mpi_type)) != SLURM_SUCCESS)
		goto done;

	/*
	 * Unset env var so that "none" doesn't exist in salloc'ed env, but
	 * still keep it in srun if not none.
	 */
	if (!xstrcmp(mpi_type, "none"))
		unsetenvp(*env, "SLURM_MPI_TYPE");

done:
	xfree(mpi_type);
	return rc;
}

extern int mpi_g_slurmstepd_prefork(const stepd_step_rec_t *job, char ***env)
{
	xassert(job);
	xassert(env);
	xassert(g_context);

#if _DEBUG
	info("%s: MPI: Details before call:", __func__);
	_log_env(*env);
	_log_step_rec(job);
#endif

	return (*(ops.slurmstepd_prefork))(job, env);
}

extern int mpi_g_slurmstepd_task(const mpi_plugin_task_info_t *job, char ***env)
{
	xassert(job);
	xassert(env);
	xassert(g_context);

#if _DEBUG
	info("%s: MPI: Details before call:", __func__);
	_log_env(*env);
	_log_task_rec(job);
#endif

	return (*(ops.slurmstepd_task))(job, env);
}

extern int mpi_g_client_init(char **mpi_type)
{
	return _mpi_init(mpi_type);
}

extern mpi_plugin_client_state_t *mpi_g_client_prelaunch(
	const mpi_plugin_client_info_t *job, char ***env)
{
	mpi_plugin_client_state_t *state;

	xassert(job);
	xassert(env);
	xassert(g_context);

#if _DEBUG
	info("%s: MPI: Details before call:", __func__);
	_log_env(*env);
	_log_mpi_rec(job);
#endif

	state = (*(ops.client_prelaunch))(job, env);
#if _DEBUG
	info("%s: MPI: Environment after call:", __func__);
	_log_env(*env);
#endif
	return state;
}

extern int mpi_g_client_fini(mpi_plugin_client_state_t *state)
{
	xassert(state);
	xassert(g_context);

#if _DEBUG
	info("%s called", __func__);
#endif

	return (*(ops.client_fini))(state);
}

extern int mpi_fini(void)
{
	int rc = SLURM_SUCCESS;

	if (!init_run || !g_context)
		return rc;

	slurm_mutex_lock(&context_lock);

	if (g_context)
		rc = _mpi_fini_locked();

	slurm_mutex_unlock(&context_lock);
	return rc;
}
