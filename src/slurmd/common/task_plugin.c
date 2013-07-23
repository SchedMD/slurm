/*****************************************************************************\
 *  task_plugin.c - task launch plugin stub.
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <pthread.h>

#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/task_plugin.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurmd_task_ops {
	int	(*slurmd_batch_request)	    (uint32_t job_id,
					     batch_job_launch_msg_t *req);
	int	(*slurmd_launch_request)    (uint32_t job_id,
					     launch_tasks_request_msg_t *req,
					     uint32_t node_id);
	int	(*slurmd_reserve_resources) (uint32_t job_id,
					     launch_tasks_request_msg_t *req,
					     uint32_t node_id);
	int	(*slurmd_suspend_job)	    (uint32_t job_id);
	int	(*slurmd_resume_job)	    (uint32_t job_id);
	int	(*slurmd_release_resources) (uint32_t job_id);

	int	(*pre_setuid)		    (stepd_step_rec_t *job);
	int	(*pre_launch_priv)	    (stepd_step_rec_t *job);
	int	(*pre_launch)		    (stepd_step_rec_t *job);
	int	(*post_term)		    (stepd_step_rec_t *job,
					     stepd_step_task_info_t *task);
	int	(*post_step)		    (stepd_step_rec_t *job);
} slurmd_task_ops_t;

/*
 * Must be synchronized with slurmd_task_ops_t above.
 */
static const char *syms[] = {
	"task_p_slurmd_batch_request",
	"task_p_slurmd_launch_request",
	"task_p_slurmd_reserve_resources",
	"task_p_slurmd_suspend_job",
	"task_p_slurmd_resume_job",
	"task_p_slurmd_release_resources",
	"task_p_pre_setuid",
	"task_p_pre_launch_priv",
	"task_p_pre_launch",
	"task_p_post_term",
	"task_p_post_step",
};

static slurmd_task_ops_t *ops = NULL;
static plugin_context_t	**g_task_context = NULL;
static int			g_task_context_num = -1;
static pthread_mutex_t		g_task_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the task plugin.
 *
 * RET - slurm error code
 */
extern int slurmd_task_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "task";
	char *task_plugin_type = NULL;
	char *last = NULL, *task_plugin_list, *type = NULL;

	if ( init_run && (g_task_context_num >= 0) )
		return retval;

	slurm_mutex_lock( &g_task_context_lock );

	if ( g_task_context_num >= 0 )
		goto done;

	task_plugin_type = slurm_get_task_plugin();
	g_task_context_num = 0; /* mark it before anything else */
	if (task_plugin_type == NULL || task_plugin_type[0] == '\0')
		goto done;

	task_plugin_list = task_plugin_type;
	while ((type = strtok_r(task_plugin_list, ",", &last))) {
		xrealloc(ops,
			 sizeof(slurmd_task_ops_t) * (g_task_context_num + 1));
		xrealloc(g_task_context, (sizeof(plugin_context_t *)
					  * (g_task_context_num + 1)));
		if (strncmp(type, "task/", 5) == 0)
			type += 5; /* backward compatibility */
		type = xstrdup_printf("task/%s", type);
		g_task_context[g_task_context_num] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_task_context_num],
			syms, sizeof(syms));
		if (!g_task_context[g_task_context_num]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			xfree(type);
			retval = SLURM_ERROR;
			break;
		}

		xfree(type);
		g_task_context_num++;
		task_plugin_list = NULL; /* for next iteration */
	}
	init_run = true;

 done:
	slurm_mutex_unlock( &g_task_context_lock );
	xfree(task_plugin_type);

	if (retval != SLURM_SUCCESS)
		slurmd_task_fini();

	return retval;
}

/*
 * Terminate the task plugin, free memory.
 *
 * RET - slurm error code
 */
extern int slurmd_task_fini(void)
{
	int i, rc = SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	if (!g_task_context)
		goto done;

	init_run = false;
	for (i = 0; i < g_task_context_num; i++) {
		if (g_task_context[i]) {
			if (plugin_context_destroy(g_task_context[i]) !=
			    SLURM_SUCCESS) {
				rc = SLURM_ERROR;
			}
		}
	}

	xfree(ops);
	xfree(g_task_context);
	g_task_context_num = -1;

done:
	slurm_mutex_unlock( &g_task_context_lock );
	return rc;
}

/*
 * Slurmd has received a batch job launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_batch_request(uint32_t job_id,
				       batch_job_launch_msg_t *req)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].slurmd_batch_request))(job_id, req);
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd has received a launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_launch_request(uint32_t job_id,
				 launch_tasks_request_msg_t *req,
				 uint32_t node_id)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].slurmd_launch_request))
					(job_id, req, node_id);
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd is reserving resources for the task.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_reserve_resources(uint32_t job_id,
				    launch_tasks_request_msg_t *req,
				    uint32_t node_id )
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].slurmd_reserve_resources))
					(job_id, req, node_id);
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd is suspending a job.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_suspend_job(uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].slurmd_suspend_job))(job_id);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd is resuming a previously suspended job.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_resume_job(uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].slurmd_resume_job))(job_id);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd is releasing resources for the task.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_release_resources(uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].slurmd_release_resources))
				(job_id);
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a task launch is about to occur.
 * Run before setting UID to the user.
 *
 * RET - slurm error code
 */
extern int task_g_pre_setuid(stepd_step_rec_t *job)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].pre_setuid))(job);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note in privileged mode that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch_priv(stepd_step_rec_t *job)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].pre_launch_priv))(job);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch(stepd_step_rec_t *job)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].pre_launch))(job);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a task has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_term(stepd_step_rec_t *job,
		     stepd_step_task_info_t *task)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].post_term))(job, task);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a step has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_step(stepd_step_rec_t *job)
{
	int i, rc = SLURM_SUCCESS;

	if (slurmd_task_init())
		return SLURM_ERROR;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; ((i < g_task_context_num) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].post_step))(job);
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}
