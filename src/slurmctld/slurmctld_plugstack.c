/*****************************************************************************\
 *  slurmctld_plugstack.c - driver for slurmctld plugstack plugin
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
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
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld_plugstack.h"

slurm_nonstop_ops_t nonstop_ops = { NULL, NULL, NULL };

typedef struct slurmctld_plugstack_ops {
	void	(*get_config)	(config_plugin_params_t *p);
} slurmctld_plugstack_ops_t;

/*
 * Must be synchronized with slurmctld_plugstack_t above.
 */
static const char *syms[] = {
	"slurmctld_plugstack_p_get_config"
};

static int g_context_cnt = -1;
static slurmctld_plugstack_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *slurmctld_plugstack_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the slurmctld plugstack plugin.
 *
 * Returns a Slurm errno.
 */
extern int slurmctld_plugstack_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "slurmctld_plugstack";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	slurmctld_plugstack_list = slurm_get_slurmctld_plugstack();
	g_context_cnt = 0;
	if ((slurmctld_plugstack_list == NULL) ||
	    (slurmctld_plugstack_list[0] == '\0'))
		goto fini;

	names = slurmctld_plugstack_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurmctld_plugstack_ops_t) *
			      (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (xstrncmp(type, "slurmctld/", 10) == 0)
			type += 10; /* backward compatibility */
		type = xstrdup_printf("slurmctld/%s", type);
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
		slurmctld_plugstack_fini();

	return rc;
}

/*
 * Terminate the slurmctld plugstack plugin. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int slurmctld_plugstack_fini(void)
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
	xfree(slurmctld_plugstack_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 * Gets the configuration for all slurmctld plugins in a List of
 * config_plugin_params_t elements. For each plugin this consists on:
 * - Plugin name
 * - List of key,pairs
 * Returns List or NULL.
 */
extern List slurmctld_plugstack_g_get_config(void)
{
	DEF_TIMERS;
	int i, rc;
	List conf_list = NULL;
	config_plugin_params_t *p;

	START_TIMER;
	rc = slurmctld_plugstack_init();

	if (g_context_cnt > 0)
		conf_list = list_create(destroy_config_plugin_params);

	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		p = xmalloc(sizeof(config_plugin_params_t));
		p->key_pairs = list_create(destroy_config_key_pair);

		(*(ops[i].get_config))(p);

		if (!p->name)
			destroy_config_plugin_params(p);
		else
			list_append(conf_list, p);
	}
	slurm_mutex_unlock(&g_context_lock);

	END_TIMER2("slurmctld_plugstack_g_get_config");

	return conf_list;
}
