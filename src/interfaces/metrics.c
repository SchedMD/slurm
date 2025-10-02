/*****************************************************************************\
 *  metrics plugin interface
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "metrics.h"
#include "config.h"

#include "slurm/slurm.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Symbols provided by the plugin */
typedef struct {
	int (*dump)(metric_set_t *set, char **buf);
	int (*free_set)(metric_set_t *set);
	metric_set_t *(*parse_jobs_metrics)(jobs_stats_t *s);
	metric_set_t *(*parse_nodes_metrics)(nodes_stats_t *s);
	metric_set_t *(*parse_parts_metrics)(partitions_stats_t *s);
	metric_set_t *(*parse_sched_metrics)(scheduling_stats_t *s);
	metric_set_t *(*parse_ua_metrics)(users_accts_stats_t *s);
} slurm_ops_t;

static const char *syms[] = {
	"metrics_p_dump",
	"metrics_p_free_set",
	"metrics_p_parse_jobs_metrics",
	"metrics_p_parse_nodes_metrics",
	"metrics_p_parse_parts_metrics",
	"metrics_p_parse_sched_metrics",
	"metrics_p_parse_ua_metrics",
};

static slurm_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;

extern int metrics_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "metrics";
	char *type = NULL;

	slurm_mutex_lock(&g_context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!(type = xstrdup(slurm_conf.metrics_type))) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	if (!(g_context =
		      plugin_context_create(plugin_type, type, (void **) &ops,
					    syms, sizeof(syms)))) {
		error("cannot create %s context for %s", plugin_type, type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	xfree(type);
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

extern void metrics_g_fini(void)
{
	slurm_mutex_lock(&g_context_lock);
	if (g_context) {
		(void) plugin_context_destroy(g_context);
		g_context = NULL;
	}
	plugin_inited = PLUGIN_NOT_INITED;
	slurm_mutex_unlock(&g_context_lock);
}

static int _keyval_cmp(const void *x, const void *y)
{
	metric_keyval_t *kv1 = *(metric_keyval_t **) x;
	metric_keyval_t *kv2 = *(metric_keyval_t **) y;

	return xstrcmp(kv1->key, kv2->key);
}

static metric_keyval_t **_keyval_sort(metric_keyval_t **kv)
{
	int cnt = 0;

	if (!kv)
		return kv;

	while (kv[cnt]->key)
		cnt++;

	qsort(kv, cnt, sizeof(metric_keyval_t *), _keyval_cmp);

	return kv;
}

extern metric_t *metrics_create_metric(metric_set_t *set,
				       data_parser_type_t type, void *data,
				       ssize_t sz_data, char *name, char *desc,
				       int attr, metric_keyval_t **kv)
{
	metric_t *metric = xmalloc(sizeof(*metric));

	metric->attr = attr;
	metric->data = xmalloc(sz_data);
	memcpy(metric->data, data, sz_data);
	metric->desc = xstrdup(desc);
	metric->keyval = _keyval_sort(kv);
	metric->name = xstrdup(name);
	metric->set = set;
	metric->type = type;

	return metric;
}

static void _free_metric_keyval_array(metric_keyval_t **kv_array)
{
	metric_keyval_t *kv = NULL;

	if (!kv_array)
		return;

	for (int i = 0; (kv = kv_array[i]); i++) {
		if (!kv->key) {
			/* Sentinel. */
			xfree(kv);
			break;
		}
		xfree(kv->key);
		xfree(kv->val);
		xfree(kv);
	}

	xfree(kv_array);
}

extern void metrics_free_metric(metric_t *m)
{
	if (!m)
		return;

	xfree(m->data);
	xfree(m->desc);
	xfree(m->id);
	_free_metric_keyval_array(m->keyval);
	xfree(m->name);
	m->set = NULL; /* Do not free */
	xfree(m);
}

extern int metrics_g_dump(metric_set_t *set, char **buf)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.dump))(set, buf);
}

extern int metrics_g_free_set(metric_set_t *set)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.free_set))(set);
}

extern metric_set_t *metrics_g_parse_jobs_metrics(jobs_stats_t *s)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.parse_jobs_metrics))(s);
}

extern metric_set_t *metrics_g_parse_nodes_metrics(nodes_stats_t *s)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.parse_nodes_metrics))(s);
}

extern metric_set_t *metrics_g_parse_parts_metrics(partitions_stats_t *s)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.parse_parts_metrics))(s);
}

extern metric_set_t *metrics_g_parse_sched_metrics(scheduling_stats_t *s)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.parse_sched_metrics))(s);
}

extern metric_set_t *metrics_g_parse_ua_metrics(users_accts_stats_t *s)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	return (*(ops.parse_ua_metrics))(s);
}
