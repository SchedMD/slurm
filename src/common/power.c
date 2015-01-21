/*****************************************************************************\
 *  src/common/power.hc - Generic power management plugin wrapper functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/power.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, the plugin will stop
 * working.  If you need to add fields, add them at the end of the structure.
 */
typedef struct slurm_power_ops {
} slurm_power_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_power_ops_t.
 */
static const char *syms[] = {
};

static int g_context_cnt = -1;
static slurm_power_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *power_plugin_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int power_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "power";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	power_plugin_list = slurm_get_power_plugin();
	g_context_cnt = 0;
	if ((power_plugin_list == NULL) || (power_plugin_list[0] == '\0'))
		goto fini;

	names = power_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurm_power_ops_t)*(g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (strncmp(type, "power/", 6) == 0)
			type += 6; /* backward compatibility */
		type = xstrdup_printf("power/%s", type);
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
		names = NULL; /* for next iteration */
	}
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		power_g_fini();

	return rc;
}

extern int power_g_fini(void)
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
	xfree(power_plugin_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}
