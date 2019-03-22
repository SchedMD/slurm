/*****************************************************************************\
 *  cli_filter.c - driver for the cli_filter plugin
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/cli_filter.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct cli_filter_ops {
	int		(*setup_defaults)(slurm_opt_t *opt, bool early);
	int		(*pre_submit)	 (slurm_opt_t *opt, int offset);
	void		(*post_submit)	 (int offset, uint32_t jobid,
					  uint32_t stepid);
} cli_filter_ops_t;

/*
 * Must be synchronized with cli_filter_ops_t above.
 */
static const char *syms[] = {
	"setup_defaults",
	"pre_submit",
	"post_submit"
};

static int g_context_cnt = -1;
static cli_filter_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *clifilter_plugin_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the cli filter plugin.
 *
 * Returns a SLURM errno.
 */
extern int cli_filter_plugin_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "cli_filter";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	clifilter_plugin_list = slurm_get_cli_filter_plugins();
	g_context_cnt = 0;
	if ((clifilter_plugin_list == NULL) || (clifilter_plugin_list[0] == '\0'))
		goto fini;

	names = clifilter_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrecalloc(ops, g_context_cnt + 1, sizeof(cli_filter_ops_t));
		xrecalloc(g_context, g_context_cnt + 1,
			  sizeof(plugin_context_t *));
		/* Permit both prefix and no-prefix for plugin names. */
		if (xstrncmp(type, "cli_filter/", 11) == 0)
			type += 11;
		type = xstrdup_printf("cli_filter/%s", type);
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
		cli_filter_plugin_fini();

	return rc;
}

/*
 * Terminate the cli filter plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int cli_filter_plugin_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < g_context_cnt; i++) {
		if (g_context[i]) {
			j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(clifilter_plugin_list);
	g_context_cnt = -1;

fini:
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

extern int cli_filter_plugin_setup_defaults(slurm_opt_t *opt, bool early)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = cli_filter_plugin_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].setup_defaults))(opt, early);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

extern int cli_filter_plugin_pre_submit(slurm_opt_t *opt, int offset)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = cli_filter_plugin_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].pre_submit))(opt, offset);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

extern void cli_filter_plugin_post_submit(int offset, uint32_t jobid,
					  uint32_t stepid)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = cli_filter_plugin_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		(*(ops[i].post_submit))(offset, jobid, stepid);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
}
