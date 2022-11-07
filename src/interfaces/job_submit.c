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

#include "src/interfaces/job_submit.h"

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

typedef struct slurm_submit_ops {
	int (*submit)(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg);
	int (*modify)(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid, char **err_msg);
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
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Initialize the job submit plugin.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_g_init(bool locked)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *tmp_plugin_list, *names;
	char *plugin_type = "job_submit";
	char *type;

	if (!locked)
		slurm_rwlock_wrlock(&context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	submit_plugin_list = xstrdup(slurm_conf.job_submit_plugins);
	g_context_cnt = 0;
	if ((submit_plugin_list == NULL) || (submit_plugin_list[0] == '\0'))
		goto fini;

	tmp_plugin_list = xstrdup(submit_plugin_list);
	names = tmp_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrecalloc(ops, g_context_cnt + 1, sizeof(slurm_submit_ops_t));
		xrecalloc(g_context, g_context_cnt + 1,
			  sizeof(plugin_context_t *));
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
	xfree(tmp_plugin_list);

fini:
	if (rc != SLURM_SUCCESS)
		job_submit_g_fini(true);

	if (!locked)
		slurm_rwlock_unlock(&context_lock);

	return rc;
}

/*
 * Terminate the job submit plugin. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_g_fini(bool locked)
{
	int i, j, rc = SLURM_SUCCESS;

	if (!locked)
		slurm_rwlock_wrlock(&context_lock);
	if (g_context_cnt < 0)
		goto fini;

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

fini:
	if (!locked)
		slurm_rwlock_unlock(&context_lock);
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
extern int job_submit_g_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	bool plugin_change;

	if (!slurm_conf.job_submit_plugins && !submit_plugin_list)
		return rc;

	slurm_rwlock_wrlock(&context_lock);
	if (xstrcmp(slurm_conf.job_submit_plugins, submit_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;

	if (plugin_change) {
		info("JobSubmitPlugins changed to %s",
		     slurm_conf.job_submit_plugins);
		rc = job_submit_g_fini(true);
		if (rc == SLURM_SUCCESS)
			rc = job_submit_g_init(true);
	}
	slurm_rwlock_unlock(&context_lock);

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
extern int job_submit_g_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
			       char **err_msg)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	START_TIMER;

	/* Set to NO_VAL so that it can only be set by the job submit plugin. */
	job_desc->site_factor = NO_VAL;

	slurm_rwlock_rdlock(&context_lock);
	xassert(g_context_cnt >= 0);
	/*
	 * NOTE: On function entry read locks are set on config, job, node and
	 * partition structures. Do not attempt to unlock them and then
	 * lock again (say with a write lock) since doing so will trigger
	 * a deadlock with the context_lock above.
	 */
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].submit))(job_desc, submit_uid, err_msg);
	slurm_rwlock_unlock(&context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Execute the job_modify() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 */
extern int job_submit_g_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
			       uint32_t submit_uid, char **err_msg)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	START_TIMER;

	/* Set to NO_VAL so that it can only be set by the job submit plugin. */
	job_desc->site_factor = NO_VAL;

	slurm_rwlock_rdlock(&context_lock);
	xassert(g_context_cnt >= 0);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].modify))(job_desc, job_ptr, submit_uid, err_msg);
	slurm_rwlock_unlock(&context_lock);
	END_TIMER2(__func__);

	return rc;
}
