/*****************************************************************************\
 *  slurm_mpi.c - Generic mpi selector for slurm
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
	int          (*slurmstepd_prefork)(const stepd_step_rec_t *job,
					   char ***env);
	int          (*slurmstepd_init)   (const mpi_plugin_task_info_t *job,
					   char ***env);
	mpi_plugin_client_state_t *
	             (*client_prelaunch)  (const mpi_plugin_client_info_t *job,
					   char ***env);
	int          (*client_fini)       (mpi_plugin_client_state_t *);
} slurm_mpi_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_mpi_ops_t.
 */
static const char *syms[] = {
	"p_mpi_hook_slurmstepd_prefork",
	"p_mpi_hook_slurmstepd_task",
	"p_mpi_hook_client_prelaunch",
	"p_mpi_hook_client_fini"
};

static slurm_mpi_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

#if _DEBUG
/* Debugging information is invaluable to debug heterogeneous step support */
static inline void _log_env(char **env)
{
#if _DEBUG > 1
	int i;

	if (!env)
		return;

	for (i = 0; env[i]; i++)
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
	for (i = 0; i < job->node_tasks; i ++)
		info("gtid[%d]:%u", i, job->task[i]->gtid);
	for (i = 0; i < job->nnodes; i++)
		info("task_cnts[%d]:%u", i, job->task_cnts[i]);

	if ((job->het_job_id != 0) && (job->het_job_id != NO_VAL))
		info("het_job_id:%u", job->het_jobid);

	if (job->het_job_offset != NO_VAL);
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
	int i, j;

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
		for (i = 0; i < layout->node_cnt; i++) {
			info("tasks[%d]:%u", i, layout->tasks[i]);
			for (j = 0; j < layout->tasks[i]; j++) {
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

int _mpi_init (char *mpi_type)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "mpi";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		goto done;

	if (mpi_type == NULL) {
		mpi_type = slurm_conf.mpi_default;
	} else if (!xstrcmp(mpi_type, "openmpi")) {
		/*
		 * The openmpi plugin has been equivalent to none for a while.
		 * Translate so we can discard that duplicated no-op plugin.
		 */
		mpi_type = "none";
	}
	if (mpi_type == NULL) {
		error("No MPI default set.");
		retval = SLURM_ERROR;
		goto done;
	}

	if (!xstrcmp(mpi_type, "list")) {
		plugrack_t *mpi_rack = plugrack_create("mpi");
		plugrack_read_dir(mpi_rack, slurm_conf.plugindir);
		plugrack_print_mpi_plugins(mpi_rack);
		exit(0);
	}

	setenvf(NULL, "SLURM_MPI_TYPE", "%s", mpi_type);

	type = xstrdup_printf("mpi/%s", mpi_type);

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	xfree(type);
	slurm_mutex_unlock( &context_lock );
	return retval;
}

int mpi_hook_slurmstepd_init (char ***env)
{
	char *mpi_type = getenvp (*env, "SLURM_MPI_TYPE");

#if _DEBUG
	info("IN %s mpi_type:%s", __func__, mpi_type);
	_log_env(*env);
#else
	debug("mpi type = %s", mpi_type);
#endif

	if (_mpi_init(mpi_type) == SLURM_ERROR)
		return SLURM_ERROR;

	/*
	 * Unset env var so that "none" doesn't exist in salloc'ed env, but
	 * still keep it in srun if not none.
	 */
	if (!xstrcmp(mpi_type, "none"))
		unsetenvp (*env, "SLURM_MPI_TYPE");

	return SLURM_SUCCESS;
}

int mpi_hook_slurmstepd_prefork (const stepd_step_rec_t *job, char ***env)
{
#if _DEBUG
	info("IN %s", __func__);
	_log_env(*env);
	_log_step_rec(job);
#endif

	if (mpi_hook_slurmstepd_init(env) == SLURM_ERROR)
		return SLURM_ERROR;

	return (*(ops.slurmstepd_prefork))(job, env);
}

int mpi_hook_slurmstepd_task (const mpi_plugin_task_info_t *job, char ***env)
{
#if _DEBUG
	info("IN %s", __func__);
	_log_task_rec(job);
	_log_env(*env);
#endif

	if (mpi_hook_slurmstepd_init(env) == SLURM_ERROR)
		return SLURM_ERROR;

	return (*(ops.slurmstepd_init))(job, env);
}

int mpi_hook_client_init (char *mpi_type)
{
#if _DEBUG
	info("IN %s mpi_type:%s", __func__, mpi_type);
#else
	debug("mpi type = %s", mpi_type);
#endif

	if (_mpi_init(mpi_type) == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

mpi_plugin_client_state_t *
mpi_hook_client_prelaunch(const mpi_plugin_client_info_t *job, char ***env)
{
	mpi_plugin_client_state_t *rc;
#if _DEBUG
	info("IN %s", __func__);
	_log_env(*env);
	_log_mpi_rec(job);
#endif

	if (_mpi_init(NULL) < 0)
		return NULL;

	rc = (*(ops.client_prelaunch))(job, env);
#if _DEBUG
	_log_env(*env);
#endif
	return rc;
}

int mpi_hook_client_fini (mpi_plugin_client_state_t *state)
{
#if _DEBUG
	info("IN %s", __func__);
#endif

	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops.client_fini))(state);
}

int mpi_fini (void)
{
	int rc;

	slurm_mutex_lock(&context_lock);
	if (!g_context) {
		slurm_mutex_unlock(&context_lock);
		return SLURM_SUCCESS;
	}

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	slurm_mutex_unlock(&context_lock);
	return rc;
}
