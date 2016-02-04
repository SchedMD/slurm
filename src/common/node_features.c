/*****************************************************************************\
 *  node_features.c - Infrastructure for changing a node's features on user
 *	demand
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, this plugins will stop
 * working.  If you need to add fields, add them to the end of the structure.
 */
typedef struct node_features_ops {
	int	(*get_node)	(char *node_list);
	int	(*reconfig)	(void);
	char *	(*job_xlate)	(char *job_features);
} node_features_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for node_features_ops_t.
 */
static const char *syms[] = {
	"node_features_p_get_node",
	"node_features_p_reconfig",
	"node_features_p_job_xlate"
};

static int g_context_cnt = -1;
static node_features_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static char *node_features_plugin_list = NULL;
static bool init_run = false;

/* Perform plugin initialization: read configuration files, etc. */
extern int node_features_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "knl";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	node_features_plugin_list = slurm_get_node_features_plugins();
	g_context_cnt = 0;
	if ((node_features_plugin_list == NULL) ||
	    (node_features_plugin_list[0] == '\0'))
		goto fini;

	names = node_features_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops,
			 (sizeof(node_features_ops_t) * (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (strncmp(type, "node_features/", 4) == 0)
			type += 4; /* backward compatibility */
		type = xstrdup_printf("node_features/%s", type);
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
		node_features_g_fini();

	return rc;
}

/* Perform plugin termination: save state, free memory, etc. */
extern int node_features_g_fini(void)
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
	xfree(node_features_plugin_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/* Reset plugin configuration information */
extern int node_features_g_reconfig(void)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].reconfig))();
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("node_features_g_reconfig");

	return rc;
}

/* Update active and available features on specified nodes, sets features on
 * all nodes is node_list is NULL */
extern int node_features_g_get_node(char *node_list)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].get_node))(node_list);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("node_features_g_get_node");

	return rc;
}

/* Translate a job's feature specification to node boot options
 * RET node boot options, must be xfreed */
extern char *node_features_g_job_xlate(char *job_features)
{
	DEF_TIMERS;
	char *node_features = NULL, *tmp_str;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		tmp_str = (*(ops[i].job_xlate))(job_features);
		if (tmp_str) {
			if (node_features) {
				xstrfmtcat(node_features, ",%s", tmp_str);
				xfree(tmp_str);
			} else {
				node_features = tmp_str;
			}
		}
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("node_features_g_job_xlate");

	return node_features;
}
