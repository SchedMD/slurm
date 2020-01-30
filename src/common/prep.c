/*****************************************************************************\
 *  prep.c - driver for PrEpPlugins ('Pr'olog and 'Ep'ilog)
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/prep.h"

typedef struct {
	int (*register_callbacks)(prep_callbacks_t *callbacks);
	int (*prolog)(job_env_t *job_env, slurm_cred_t *cred);
	int (*epilog)(job_env_t *job_env, slurm_cred_t *cred);
	int (*prolog_slurmctld)(job_record_t *job_ptr, bool *async);
	int (*epilog_slurmctld)(job_record_t *job_ptr, bool *async);
} prep_ops_t;

/*
 * Must be synchronized with prep_ops_t above.
 */
static const char *syms[] = {
	"prep_p_register_callbacks",
	"prep_p_prolog",
	"prep_p_epilog",
	"prep_p_prolog_slurmctld",
	"prep_p_epilog_slurmctld",
};

static int g_context_cnt = -1;
static prep_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *prep_plugin_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the PrEpPlugins.
 *
 * Returns a Slurm errno.
 */
extern int prep_plugin_init(prep_callbacks_t *callbacks)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *tmp_plugin_list, *names;
	char *plugin_type = "prep";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	prep_plugin_list = slurm_get_prep_plugins();
	g_context_cnt = 0;
	if ((prep_plugin_list == NULL) || (prep_plugin_list[0] == '\0'))
		goto fini;

	tmp_plugin_list = xstrdup(prep_plugin_list);
	names = tmp_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrecalloc(ops, g_context_cnt + 1, sizeof(prep_ops_t));
		xrecalloc(g_context, g_context_cnt + 1,
			  sizeof(plugin_context_t *));

		if (xstrncmp(type, "prep/", 5) == 0)
			type += 5; /* backward compatibility */
		type = xstrdup_printf("prep/%s", type);

		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("%s: cannot create %s context for %s",
			      __func__, plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		if (callbacks)
			(*(ops[g_context_cnt].register_callbacks))(callbacks);

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next strtok_r() iteration */
	}
	init_run = true;
	xfree(tmp_plugin_list);

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		prep_plugin_fini();

	return rc;
}

/*
 * Terminate the PrEpPlugins and free associated memory.
 *
 * Returns a Slurm errno.
 */
extern int prep_plugin_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (int i = 0; i < g_context_cnt; i++) {
		if (g_context[i]) {
			int j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(prep_plugin_list);
	g_context_cnt = -1;

fini:
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 * Perform reconfig, re-read any configuration files
 */
extern int prep_plugin_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_names = slurm_get_prep_plugins();
	bool plugin_change = false;

	if (!plugin_names && !prep_plugin_list)
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (xstrcmp(plugin_names, prep_plugin_list))
		plugin_change = true;
	slurm_mutex_unlock(&g_context_lock);

	if (plugin_change) {
		info("%s: PrEpPlugins changed to %s", __func__, plugin_names);
		rc = prep_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = prep_plugin_init(NULL);
	}
	xfree(plugin_names);

	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

extern int prep_prolog(job_env_t *job_env, slurm_cred_t *cred)
{
	DEF_TIMERS;
	int rc;

	START_TIMER;

	rc = prep_plugin_init(NULL);
	slurm_mutex_lock(&g_context_lock);
	for (int i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].prolog))(job_env, cred);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

extern int prep_epilog(job_env_t *job_env, slurm_cred_t *cred)
{
	DEF_TIMERS;
	int rc;

	START_TIMER;

	rc = prep_plugin_init(NULL);
	slurm_mutex_lock(&g_context_lock);
	for (int i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].epilog))(job_env, cred);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

extern void prep_prolog_slurmctld(job_record_t *job_ptr)
{
	DEF_TIMERS;
	int rc;

	START_TIMER;

	rc = prep_plugin_init(NULL);
	slurm_mutex_lock(&g_context_lock);
	for (int i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		bool async = false;

		rc = (*(ops[i].prolog_slurmctld))(job_ptr, &async);

		if (async)
			job_ptr->prep_prolog_cnt++;
	}

	if (job_ptr->prep_prolog_cnt && job_ptr->details) {
		job_ptr->details->prolog_running++;
		job_ptr->job_state |= JOB_CONFIGURING;
	}

	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
}

extern void prep_epilog_slurmctld(job_record_t *job_ptr)
{
	DEF_TIMERS;
	int rc;

	START_TIMER;

	rc = prep_plugin_init(NULL);
	slurm_mutex_lock(&g_context_lock);
	for (int i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		bool async = false;

		rc = (*(ops[i].epilog_slurmctld))(job_ptr, &async);

		if (async)
			job_ptr->prep_epilog_cnt++;
	}

	if (job_ptr->prep_epilog_cnt)
		job_ptr->epilog_running = true;

	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
}
