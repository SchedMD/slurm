/*****************************************************************************\
 *  task_plugin.c - task launch plugin stub.
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#define _GNU_SOURCE
#include <pthread.h>
#include <ctype.h>

#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/task.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurmd_task_ops {
	int	(*slurmd_batch_request)	    (batch_job_launch_msg_t *req);
	int	(*slurmd_launch_request)    (launch_tasks_request_msg_t *req,
					     uint32_t node_id, char **err_msg);

	int	(*pre_setuid)		    (stepd_step_rec_t *step);
	int	(*pre_launch_priv)	    (stepd_step_rec_t *step,
					     uint32_t node_tid,
					     uint32_t global_tid);
	int	(*pre_launch)		    (stepd_step_rec_t *step);
	int	(*post_term)		    (stepd_step_rec_t *step,
					     stepd_step_task_info_t *task);
	int	(*post_step)		    (stepd_step_rec_t *step);
	int	(*add_pid)	    	    (pid_t pid);
} slurmd_task_ops_t;

/*
 * Must be synchronized with slurmd_task_ops_t above.
 */
static const char *syms[] = {
	"task_p_slurmd_batch_request",
	"task_p_slurmd_launch_request",
	"task_p_pre_setuid",
	"task_p_pre_launch_priv",
	"task_p_pre_launch",
	"task_p_post_term",
	"task_p_post_step",
	"task_p_add_pid",
};

static slurmd_task_ops_t *ops = NULL;
static plugin_context_t	**g_task_context = NULL;
static int			g_task_context_num = -1;
static pthread_mutex_t		g_task_context_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Initialize the task plugin.
 *
 * RET - slurm error code
 */
extern int task_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "task";
	char *task_plugin_type = NULL;
	char *last = NULL, *task_plugin_list, *type = NULL;

	slurm_mutex_lock( &g_task_context_lock );

	if ( g_task_context_num >= 0 )
		goto done;

	g_task_context_num = 0; /* mark it before anything else */
	if (!slurm_conf.task_plugin)
		goto done;

	task_plugin_list = task_plugin_type = xstrdup(slurm_conf.task_plugin);
	while ((type = strtok_r(task_plugin_list, ",", &last))) {
		xrealloc(ops,
			 sizeof(slurmd_task_ops_t) * (g_task_context_num + 1));
		xrealloc(g_task_context, (sizeof(plugin_context_t *)
					  * (g_task_context_num + 1)));
		if (xstrncmp(type, "task/", 5) == 0)
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

 done:
	slurm_mutex_unlock( &g_task_context_lock );
	xfree(task_plugin_type);

	if (retval != SLURM_SUCCESS)
		task_g_fini();

	return retval;
}

/*
 * Terminate the task plugin, free memory.
 *
 * RET - slurm error code
 */
extern int task_g_fini(void)
{
	int i, rc = SLURM_SUCCESS, rc2;

	slurm_mutex_lock( &g_task_context_lock );
	if (!g_task_context)
		goto done;

	for (i = 0; i < g_task_context_num; i++) {
		if (g_task_context[i]) {
			rc2 = plugin_context_destroy(g_task_context[i]);
			if (rc2 != SLURM_SUCCESS) {
				debug("%s: %s: %s", __func__,
				      g_task_context[i]->type,
				      slurm_strerror(rc2));
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
extern int task_g_slurmd_batch_request(batch_job_launch_msg_t *req)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].slurmd_batch_request))(req);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Slurmd has received a launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_launch_request(launch_tasks_request_msg_t *req,
					uint32_t node_id, char **err_msg)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].slurmd_launch_request)) (req, node_id, err_msg);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
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
extern int task_g_pre_setuid(stepd_step_rec_t *step)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].pre_setuid))(step);
		if (rc != SLURM_SUCCESS) {
			error("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note in privileged mode that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch_priv(stepd_step_rec_t *step, uint32_t node_tid,
				  uint32_t global_tid)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].pre_launch_priv))(step, node_tid, global_tid);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch(stepd_step_rec_t *step)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].pre_launch))(step);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a task has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_term(stepd_step_rec_t *step,
		     stepd_step_task_info_t *task)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].post_term))(step, task);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Note that a step has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_step(stepd_step_rec_t *step)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].post_step))(step);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

/*
 * Keep track of a pid.
 *
 * RET - slurm error code
 */
extern int task_g_add_pid(pid_t pid)
{
	int i, rc = SLURM_SUCCESS;

	xassert(g_task_context_num >= 0);

	if (!g_task_context_num)
		return SLURM_SUCCESS;

	slurm_mutex_lock( &g_task_context_lock );
	for (i = 0; i < g_task_context_num; i++) {
		rc = (*(ops[i].add_pid))(pid);
		if (rc != SLURM_SUCCESS) {
			debug("%s: %s: %s", __func__,
			      g_task_context[i]->type, slurm_strerror(rc));
			break;
		}
	}
	slurm_mutex_unlock( &g_task_context_lock );

	return (rc);
}

extern void task_slurm_chkaffinity(cpu_set_t *mask, stepd_step_rec_t *step,
				   int statval, uint32_t node_tid)
{
#if defined(__APPLE__)
	fatal("%s: not supported on macOS", __func__);
#else
	char *bind_type, *action, *status, *units;
	char mstr[CPU_SET_HEX_STR_SIZE];

	if (!(step->cpu_bind_type & CPU_BIND_VERBOSE))
		return;

	if (statval)
		status = " FAILED";
	else
		status = "";

	if (step->cpu_bind_type & CPU_BIND_NONE) {
		action = "";
		units  = "";
		bind_type = "NONE";
	} else {
		action = " set";
		if (step->cpu_bind_type & CPU_BIND_TO_THREADS)
			units = "-threads";
		else if (step->cpu_bind_type & CPU_BIND_TO_CORES)
			units = "-cores";
		else if (step->cpu_bind_type & CPU_BIND_TO_SOCKETS)
			units = "-sockets";
		else if (step->cpu_bind_type & CPU_BIND_TO_LDOMS)
			units = "-ldoms";
		else
			units = "";
		if (step->cpu_bind_type & CPU_BIND_MAP) {
			bind_type = "MAP ";
		} else if (step->cpu_bind_type & CPU_BIND_MASK) {
			bind_type = "MASK";
		} else if (step->cpu_bind_type & CPU_BIND_LDRANK) {
			bind_type = "LDRANK";
		} else if (step->cpu_bind_type & CPU_BIND_LDMAP) {
			bind_type = "LDMAP ";
		} else if (step->cpu_bind_type & CPU_BIND_LDMASK) {
			bind_type = "LDMASK";
		} else if (step->cpu_bind_type & (~CPU_BIND_VERBOSE)) {
			bind_type = "UNK ";
		} else {
			action = "";
			bind_type = "NULL";
		}
	}

	fprintf(stderr, "cpu-bind%s=%s - "
			"%s, task %2u %2u [%u]: mask 0x%s%s%s\n",
			units, bind_type,
			step->node_name,
			step->task[node_tid]->gtid,
			node_tid,
			step->task[node_tid]->pid,
			task_cpuset_to_str(mask, mstr),
			action,
			status);
#endif
}
