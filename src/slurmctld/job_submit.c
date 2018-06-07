/*****************************************************************************\
 *  job_submit.c - driver for job_submit plugin
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/locks.h"

typedef struct slurm_submit_ops {
	int		(*submit)	( struct job_descriptor *job_desc,
					  uint32_t submit_uid,
					  char **err_msg );
	int		(*modify)	( struct job_descriptor *job_desc,
					  struct job_record *job_ptr,
					  uint32_t submit_uid );
} slurm_submit_ops_t;

/*
 * Must be synchronized with slurm_submit_ops_t above.
 */
static const char *syms[] = {
	"job_submit",
	"job_modify"
};

static int g_context_cnt = -1;
static slurm_submit_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *submit_plugin_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the job submit plugin.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_plugin_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "job_submit";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	submit_plugin_list = slurm_get_job_submit_plugins();
	g_context_cnt = 0;
	if ((submit_plugin_list == NULL) || (submit_plugin_list[0] == '\0'))
		goto fini;

	names = submit_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops,
			 (sizeof(slurm_submit_ops_t) * (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (xstrncmp(type, "job_submit/", 11) == 0)
			type += 11; /* backward compatibility */
		type = xstrdup_printf("job_submit/%s", type);
		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next strtok_r() iteration */
	}
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		job_submit_plugin_fini();

	return rc;
}

/*
 * Terminate the job submit plugin. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_plugin_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i=0; i<g_context_cnt; i++) {
		if (g_context[i]) {
			j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(submit_plugin_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Perform reconfig, re-read any configuration files
 */
extern int job_submit_plugin_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_names = slurm_get_job_submit_plugins();
	bool plugin_change;

	if (!plugin_names && !submit_plugin_list)
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (plugin_names && submit_plugin_list &&
	    xstrcmp(plugin_names, submit_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&g_context_lock);

	if (plugin_change) {
		info("JobSubmitPlugins changed to %s", plugin_names);
		rc = job_submit_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = job_submit_plugin_init();
	}
	xfree(plugin_names);

	return rc;
}

/*
 * Execute the job_submit() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 * IN job_desc - Job request specification
 * IN submit_uid - User issuing job submit request
 * OUT err_msg - Custom error message to the user, caller to xfree results
 */
extern int job_submit_plugin_submit(struct job_descriptor *job_desc,
				    uint32_t submit_uid, char **err_msg)
{
	DEF_TIMERS;
	int i, rc;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	START_TIMER;
	rc = job_submit_plugin_init();
	slurm_mutex_lock(&g_context_lock);
	/* NOTE: On function entry read locks are set on config, job, node and
	 * partition structures. Do not attempt to unlock them and then
	 * lock again (say with a write lock) since doing so will trigger
	 * a deadlock with the g_context_lock above. */
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].submit))(job_desc, submit_uid, err_msg);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("job_submit_plugin_submit");

	return rc;
}

/*
 * Execute the job_modify() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 */
extern int job_submit_plugin_modify(struct job_descriptor *job_desc,
				    struct job_record *job_ptr,
				    uint32_t submit_uid)
{
	DEF_TIMERS;
	int i, rc;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	START_TIMER;
	rc = job_submit_plugin_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].modify))(job_desc, job_ptr, submit_uid);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("job_submit_plugin_modify");

	return rc;
}
