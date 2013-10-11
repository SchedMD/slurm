/*****************************************************************************\
 *  job_container_plugin.c - job container plugin stub.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette
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

#include "src/slurmd/common/job_container_plugin.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct job_container_ops {
	int	(*container_p_create)	(uint32_t job_id);
	int	(*container_p_add_cont)	(uint32_t job_id, uint64_t cont_id);
	int	(*container_p_add_pid)	(uint32_t job_id, pid_t pid, uid_t uid);
	int	(*container_p_delete)	(uint32_t job_id);
	int	(*container_p_restore)	(char *dir_name, bool recover);
	void	(*container_p_reconfig)	(void);

} job_container_ops_t;

/*
 * Must be synchronized with job_container_ops_t above.
 */
static const char *syms[] = {
	"container_p_create",
	"container_p_add_cont",
	"container_p_add_pid",
	"container_p_delete",
	"container_p_restore",
	"container_p_reconfig",
};

static job_container_ops_t	*ops = NULL;
static plugin_context_t		**g_container_context = NULL;
static int			g_container_context_num = -1;
static pthread_mutex_t		g_container_context_lock =
					PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the job container plugin.
 *
 * RET - slurm error code
 */
extern int job_container_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "job_container";
	char *container_plugin_type = NULL;
	char *last = NULL, *job_container_plugin_list, *job_container = NULL;

	if (init_run && (g_container_context_num >= 0))
		return retval;

	slurm_mutex_lock(&g_container_context_lock);

	if (g_container_context_num >= 0)
		goto done;

	container_plugin_type = slurm_get_job_container_plugin();
	g_container_context_num = 0; /* mark it before anything else */
	if ((container_plugin_type == NULL) ||
	    (container_plugin_type[0] == '\0'))
		goto done;

	job_container_plugin_list = container_plugin_type;
	while ((job_container =
		strtok_r(job_container_plugin_list, ",", &last))) {
		xrealloc(ops,
			 sizeof(job_container_ops_t) *
			 (g_container_context_num + 1));
		xrealloc(g_container_context, (sizeof(plugin_context_t *)
					  * (g_container_context_num + 1)));
		if (strncmp(job_container, "job_container/", 14) == 0)
			job_container += 14; /* backward compatibility */
		job_container = xstrdup_printf("job_container/%s",
					       job_container);
		g_container_context[g_container_context_num] =
			plugin_context_create(
				plugin_type, job_container,
				(void **)&ops[g_container_context_num],
				syms, sizeof(syms));
		if (!g_container_context[g_container_context_num]) {
			error("cannot create %s context for %s",
			      plugin_type, job_container);
			xfree(job_container);
			retval = SLURM_ERROR;
			break;
		}

		xfree(job_container);
		g_container_context_num++;
		job_container_plugin_list = NULL; /* for next iteration */
	}
	init_run = true;

 done:
	slurm_mutex_unlock(&g_container_context_lock);
	xfree(container_plugin_type);

	if (retval != SLURM_SUCCESS)
		job_container_fini();

	return retval;
}

/*
 * Terminate the job container plugin, free memory.
 *
 * RET - slurm error code
 */
extern int job_container_fini(void)
{
	int i, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_container_context_lock);
	if (!g_container_context)
		goto done;

	init_run = false;
	for (i = 0; i < g_container_context_num; i++) {
		if (g_container_context[i]) {
			if (plugin_context_destroy(g_container_context[i])
			    != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
			}
		}
	}

	xfree(ops);
	xfree(g_container_context);
	g_container_context_num = -1;

done:
	slurm_mutex_unlock(&g_container_context_lock);
	return rc;
}

/* Create a container for the specified job */
extern int container_g_create(uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS;

	if (job_container_init())
		return SLURM_ERROR;

	for (i = 0; ((i < g_container_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].container_p_create))(job_id);
	}

	return rc;
}

/* Add a process to the specified job's container.
 * A proctrack containter will be generated containing the process
 * before container_g_add_cont() is called (see below). */
extern int container_g_add_pid(uint32_t job_id, pid_t pid, uid_t uid)
{
	int i, rc = SLURM_SUCCESS;

	if (job_container_init())
		return SLURM_ERROR;

	for (i = 0; ((i < g_container_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].container_p_add_pid))(job_id, pid, uid);
	}

	return rc;
}

/* Add a proctrack container (PAGG) to the specified job's container
 * The PAGG will be the job's cont_id returned by proctrack/sgi_job */
extern int container_g_add_cont(uint32_t job_id, uint64_t cont_id)
{
	int i, rc = SLURM_SUCCESS;

	if (job_container_init())
		return SLURM_ERROR;

	for (i = 0; ((i < g_container_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].container_p_add_cont))(job_id, cont_id);
	}

	return rc;
}

/* Delete the container for the specified job */
extern int container_g_delete(uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS;

	if (job_container_init())
		return SLURM_ERROR;

	for (i = 0; ((i < g_container_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].container_p_delete))(job_id);
	}

	return rc;
}

/* Restore container information */
extern int container_g_restore(char * dir_name, bool recover)
{
	int i, rc = SLURM_SUCCESS;

	if (job_container_init())
		return SLURM_ERROR;

	for (i = 0; ((i < g_container_context_num) && (rc == SLURM_SUCCESS));
	     i++) {
		rc = (*(ops[i].container_p_restore))(dir_name, recover);
	}

	return rc;
}

/* Note change in configuration (e.g. "DebugFlag=JobContainer" set) */
extern void container_g_reconfig(void)
{
	int i;

	(void) job_container_init();

	for (i = 0; i < g_container_context_num;i++) {
		(*(ops[i].container_p_reconfig))();
	}

	return;
}

