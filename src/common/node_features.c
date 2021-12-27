/*****************************************************************************\
 *  node_features.c - Infrastructure for changing a node's features on user
 *	demand
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
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
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct node_features_ops {
	uint32_t(*boot_time)	(void);
	bool    (*changeable_feature) (char *feature);
	int	(*get_node)	(char *node_list);
	int	(*job_valid)	(char *job_features);
	char *	(*job_xlate)	(char *job_features);
	bitstr_t * (*get_node_bitmap) (void);
	int     (*overlap)      (bitstr_t *active_bitmap);
	bool	(*node_power)	(void);
	int	(*node_set)	(char *active_features);
	void	(*node_state)	(char **avail_modes, char **current_mode);
	int	(*node_update)	(char *active_features, bitstr_t *node_bitmap);
	bool	(*node_update_valid) (void *node_ptr,
				      update_node_msg_t *update_node_msg);
	char *	(*node_xlate)	(char *new_features, char *orig_features,
				 char *avail_features, int node_inx);
	char *	(*node_xlate2)	(char *new_features);
	void	(*step_config)	(bool mem_sort, bitstr_t *numa_bitmap);
	uint32_t(*reboot_weight)(void);
	int	(*reconfig)	(void);
	bool	(*user_update)	(uid_t uid);
	void	(*get_config)	(config_plugin_params_t *p);
} node_features_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for node_features_ops_t.
 */
static const char *syms[] = {
	"node_features_p_boot_time",
	"node_features_p_changeable_feature",
	"node_features_p_get_node",
	"node_features_p_job_valid",
	"node_features_p_job_xlate",
	"node_features_p_get_node_bitmap",
	"node_features_p_overlap",
	"node_features_p_node_power",
	"node_features_p_node_set",
	"node_features_p_node_state",
	"node_features_p_node_update",
	"node_features_p_node_update_valid",
	"node_features_p_node_xlate",
	"node_features_p_node_xlate2",
	"node_features_p_step_config",
	"node_features_p_reboot_weight",
	"node_features_p_reconfig",
	"node_features_p_user_update",
	"node_features_p_get_config"
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
	char *plugin_type = "node_features";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	node_features_plugin_list = xstrdup(slurm_conf.node_features_plugins);
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
		if (xstrncmp(type, "node_features/", 14) == 0)
			type += 14; /* backward compatibility */
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

/* Return count of node_feature plugins configured */
extern int node_features_g_count(void)
{
	int rc;

	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	rc = g_context_cnt;
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

/* Perform set up for step launch
 * mem_sort IN - Trigger sort of memory pages (KNL zonesort)
 * numa_bitmap IN - NUMA nodes allocated to this job */
extern void node_features_g_step_config(bool mem_sort, bitstr_t *numa_bitmap)
{
	DEF_TIMERS;
	int i;

	START_TIMER;
	if (node_features_g_init() != SLURM_SUCCESS)
		return;
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++)
		(*(ops[i].step_config))(mem_sort, numa_bitmap);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
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
	END_TIMER2(__func__);

	return rc;
}

/* Return TRUE if this (one) feature name is under this plugin's control */
extern bool node_features_g_changeable_feature(char *feature)
{
	DEF_TIMERS;
	int i;
	bool changeable = false;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && !changeable); i++)
		changeable = (*(ops[i].changeable_feature))(feature);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return changeable;
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
	END_TIMER2(__func__);

	return rc;
}

/* Test if a job's feature specification is valid */
extern int node_features_g_job_valid(char *job_features)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].job_valid))(job_features);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/*
 * Translate a job's feature request to the node features needed at boot time.
 *	If multiple MCDRAM or NUMA values are ORed, pick the first ones.
 * IN job_features - job's --constraint specification
 * RET comma-delimited features required on node reboot. Must xfree to release
 *     memory
 */
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
	END_TIMER2(__func__);

	return node_features;
}

/* Return bitmap of KNL nodes, NULL if none identified */
extern bitstr_t *node_features_g_get_node_bitmap(void)
{
	DEF_TIMERS;
	bitstr_t *node_bitmap = NULL;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		node_bitmap = (*(ops[i].get_node_bitmap))();
		if (node_bitmap)
			break;
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return node_bitmap;
}

/* Return count of bits in active_bitmap that are in the features bitmap */
extern int node_features_g_overlap(bitstr_t *active_bitmap)
{
	DEF_TIMERS;
	int cnt = 0;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++)
		cnt += (*(ops[i].overlap))(active_bitmap);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return cnt;
}

/* Return true if the plugin requires PowerSave mode for booting nodes */
extern bool node_features_g_node_power(void)
{
	DEF_TIMERS;
	bool node_power = false;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		node_power = (*(ops[i].node_power))();
		if (node_power)
			break;
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return node_power;
}

/* Set's the node's active features based upon job constraints.
 * NOTE: Executed by the slurmd daemon.
 * IN active_features - New active features
 * RET error code */
extern int node_features_g_node_set(char *active_features)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].node_set))(active_features);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}

/* Get this node's current and available MCDRAM and NUMA settings from BIOS.
 * avail_modes IN/OUT - available modes, must be xfreed
 * current_mode IN/OUT - current modes, must be xfreed */
extern void node_features_g_node_state(char **avail_modes, char **current_mode)
{
	DEF_TIMERS;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		(*(ops[i].node_state))(avail_modes, current_mode);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);
}

/* Note the active features associated with a set of nodes have been updated.
 * Specifically update the node's "hbm" GRES and "CpuBind" values as needed.
 * IN active_features - New active features
 * IN node_bitmap - bitmap of nodes changed
 * RET error code */
extern int node_features_g_node_update(char *active_features,
				       bitstr_t *node_bitmap)
{
	DEF_TIMERS;
	int i, rc = SLURM_SUCCESS;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(ops[i].node_update))(active_features, node_bitmap);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return rc;
}


/*
 * Return TRUE if the specified node update request is valid with respect
 * to features changes (i.e. don't permit a non-KNL node to set KNL features).
 *
 * node_ptr IN - Pointer to node_record_t record
 * update_node_msg IN - Pointer to update request
 */
extern bool node_features_g_node_update_valid(void *node_ptr,
					update_node_msg_t *update_node_msg)
{
	DEF_TIMERS;
	bool update_valid = true;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		update_valid = (*(ops[i].node_update_valid))(node_ptr,
							     update_node_msg);
		if (!update_valid)
			break;
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return update_valid;
}

/*
 * Translate a node's feature specification by replacing any features associated
 *	with this plugin in the original value with the new values, preserving
 *	any features that are not associated with this plugin
 * IN new_features - newly active features
 * IN orig_features - original active features
 * IN avail_features - original available features
 * IN node_inx - index of node in node table
 * RET node's new merged features, must be xfreed
 */
extern char *node_features_g_node_xlate(char *new_features, char *orig_features,
					char *avail_features, int node_inx)
{
	DEF_TIMERS;
	char *new_value = NULL, *tmp_str;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);

	if (!g_context_cnt)
		new_value = xstrdup(new_features);

	for (i = 0; i < g_context_cnt; i++) {
		if (new_value)
			tmp_str = new_value;
		else if (orig_features)
			tmp_str = xstrdup(orig_features);
		else
			tmp_str = NULL;
		new_value = (*(ops[i].node_xlate))(new_features, tmp_str,
						   avail_features, node_inx);
		xfree(tmp_str);

	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return new_value;
}

/* Translate a node's new feature specification into a "standard" ordering
 * RET node's new merged features, must be xfreed */
extern char *node_features_g_node_xlate2(char *new_features)
{
	DEF_TIMERS;
	char *new_value = NULL, *tmp_str;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);

	if (!g_context_cnt)
		new_value = xstrdup(new_features);

	for (i = 0; i < g_context_cnt; i++) {
		if (new_value)
			tmp_str = xstrdup(new_value);
		else
			tmp_str = xstrdup(new_features);
		new_value = (*(ops[i].node_xlate2))(tmp_str);
		xfree(tmp_str);

	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return new_value;
}

/* Determine if the specified user can modify the currently available node
 * features */
extern bool node_features_g_user_update(uid_t uid)
{
	DEF_TIMERS;
	bool result = true;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (result == true)); i++) {
		result = (*(ops[i].user_update))(uid);
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return result;
}

/* Return estimated reboot time, in seconds */
extern uint32_t node_features_g_boot_time(void)
{
	DEF_TIMERS;
	uint32_t boot_time = 0;
	int i;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++) {
		boot_time = MAX(boot_time, (*(ops[i].boot_time))());
	}
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return boot_time;
}

/* Get node features plugin configuration */
extern List node_features_g_get_config(void)
{
	DEF_TIMERS;
	int i, rc;
	List conf_list = NULL;
	config_plugin_params_t *p;

	START_TIMER;
	rc = node_features_g_init();

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
	END_TIMER2(__func__);

	return conf_list;
}

/*
 * Return node "weight" field if reboot required to change mode
 */
extern uint32_t node_features_g_reboot_weight(void)
{
	DEF_TIMERS;
	uint32_t weight = INFINITE - 1;

	START_TIMER;
	(void) node_features_g_init();
	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt > 0)
		weight = (*(ops[0].reboot_weight))();
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2(__func__);

	return weight;
}
