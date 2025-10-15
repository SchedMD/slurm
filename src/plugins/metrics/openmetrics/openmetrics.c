/*****************************************************************************\
 *  openmetrics.c - OpenMetrics plugin source file
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

#define _GNU_SOURCE

#include "openmetrics.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xhash.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/metrics.h"
#include "src/slurmctld/statistics.h"

const char plugin_name[] = "OpenMetrics plugin";
const char plugin_type[] = "metrics/openmetrics";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define PLUGIN_ID 0xcafebeef

typedef struct openmetrics_set {
	xhash_t *full_hash; /* metrics table exact {name,[key,val]*} lookup */
	xhash_t *name_hash; /* table of lists of metrics with the same name */
} openmetrics_set_t;

typedef struct foreach_dump_metric_args {
	char **str;
	char **pos;
} foreach_dump_metric_args_t;

typedef struct foreach_stats_parse_metric {
	char *str;
	char *pfx;
	metric_set_t *set;
} foreach_stats_parse_metric_t;

#define ADD_METRIC_KEYVAL_PFX(set, type, data, pfx, name, desc, otype, key, val) \
	_metrics_create_kv(set, DATA_PARSER_##type, (void *) &(data),	\
			   sizeof(data), pfx, XSTRINGIFY(name), desc,	\
			   METRIC_TYPE_##otype, key, val)

#define ADD_METRIC_KEYVAL(set, type, data, name, desc, otype, key, val)	\
	_metrics_create_kv(set, DATA_PARSER_##type, (void *) &(data),	\
			   sizeof(data), NULL, "slurm_" XSTRINGIFY(name), \
			   desc, METRIC_TYPE_##otype, key, val)

#define ADD_METRIC(set, type, data, name, desc, otype)			\
	_metrics_create_kv(set, DATA_PARSER_##type, (void *) &(data),	\
			   sizeof(data), NULL, "slurm_" XSTRINGIFY(name), \
			   desc, METRIC_TYPE_##otype, NULL, NULL)

extern int init(void)
{
	debug("loading %s", plugin_name);
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloading %s", plugin_name);
}

static void _free_openmetrics_set(openmetrics_set_t *ometrics_set_ptr)
{
	xassert(ometrics_set_ptr);
	xhash_free_ptr(&ometrics_set_ptr->name_hash);
	xhash_free_ptr(&ometrics_set_ptr->full_hash);
	xfree(ometrics_set_ptr);
}

static char *_make_hash_id(char *name, metric_keyval_t **keyval)
{
	char *pos = NULL, *id = NULL;

	xstrfmtcatat(id, &pos, "%s", name);

	if (keyval && *keyval) {
		for (int i = 0; keyval[i]->key; i++) {
			xstrfmtcatat(id, &pos, ":%s=%s", keyval[i]->key,
				     keyval[i]->val);
		}
	}

	return id;
}

/* Free item from xhash_t. Called from function ptr */
static void _free_xhash_name_list(void *item)
{
	list_t *l = item;
	FREE_NULL_LIST(l);
}

/* Free item from xhash_t. Called from function ptr */
static void _free_xhash_full(void *item)
{
	metric_t *m = item;
	metrics_free_metric(m);
}

/* Fetch key from xhash_t item. Called from function ptr */
static void _make_xhash_id_name(void *item, const char **key, uint32_t *key_len)
{
	list_t *l = (list_t *) item;
	metric_t *m = list_peek(l);

	*key = m->name;
	*key_len = strlen(*key);
}

/* Fetch key from xhash_t item. Called from function ptr */
static void _make_xhash_id(void *item, const char **key, uint32_t *key_len)
{
	metric_t *m = (metric_t *) item;
	*key = m->id;
	*key_len = strlen(*key);
}

static openmetrics_set_t *_init_ometrics_set()
{
	openmetrics_set_t *ometrics_set = xmalloc(sizeof(*ometrics_set));
	ometrics_set->full_hash = xhash_init(_make_xhash_id, _free_xhash_full);
	ometrics_set->name_hash =
		xhash_init(_make_xhash_id_name, _free_xhash_name_list);
	return ometrics_set;
}

static openmetrics_set_t *_check_set(metric_set_t *set)
{
	if (!set || (set->plugin_id != PLUGIN_ID)) {
		error("%s: invalid namespace", __func__);
		return NULL;
	}
	return (openmetrics_set_t *) set->arg;
}

extern int metrics_p_free_set(metric_set_t *set)
{
	openmetrics_set_t *ometrics_set;

	if (!set)
		return SLURM_SUCCESS;

	if (!(ometrics_set = _check_set(set)))
		return SLURM_ERROR;

	_free_openmetrics_set(ometrics_set);
	set->plugin_id = 0;
	set->plugin_type = "";
	xfree(set);

	return SLURM_SUCCESS;
}

static metric_set_t *_metrics_new_set(void)
{
	metric_set_t *set = xmalloc(sizeof(*set));
	openmetrics_set_t *ometrics_set = _init_ometrics_set();

	*set = (metric_set_t) {
		.plugin_id = PLUGIN_ID,
		.arg = (void *) ometrics_set,
		.plugin_type = plugin_type,
	};
	return set;
}

static int _metrics_add(metric_set_t *set, metric_t *m)
{
	char *hash_id;
	list_t *name_list;
	openmetrics_set_t *ometrics_set;

	if (!(ometrics_set = _check_set(set)))
		return SLURM_ERROR;

	hash_id = _make_hash_id(m->name, m->keyval);

	if (xhash_get_str(ometrics_set->full_hash, hash_id)) {
		error("Duplicate key when adding metric: %s", m->name);
		xfree(hash_id);
		return SLURM_ERROR;
	}

	if (!m->id)
		m->id = hash_id;
	else
		xfree(hash_id);

	xhash_add(ometrics_set->full_hash, m);

	name_list = xhash_get_str(ometrics_set->name_hash, m->name);
	if (!name_list) {
		name_list = list_create(NULL); /* metrics are not freed here */
		list_append(name_list, m);
		xhash_add(ometrics_set->name_hash, name_list);
	} else {
		list_append(name_list, m);
	}

	return SLURM_SUCCESS;
}

static void _dump_metric_value(char **str, char **p, const metric_t *m)
{
	switch (m->type) {
	case DATA_PARSER_UINT16:
		xstrfmtcatat(*str, p, "%hu", *(uint16_t *) m->data);
		break;
	case DATA_PARSER_UINT32:
		xstrfmtcatat(*str, p, "%u", *(uint32_t *) m->data);
		break;
	case DATA_PARSER_UINT64:
		xstrfmtcatat(*str, p, "%llu", *(unsigned long long *) m->data);
		break;
	case DATA_PARSER_TIMESTAMP:
		xstrfmtcatat(*str, p, "%ld", *(time_t *) m->data);
		break;
	case DATA_PARSER_INT32:
		xstrfmtcatat(*str, p, "%d", *(int32_t *) m->data);
		break;
	case DATA_PARSER_INT64:
		xstrfmtcatat(*str, p, "%lld", *(long long *) m->data);
		break;
	case DATA_PARSER_FLOAT64:
		xstrfmtcatat(*str, p, "%lf", *(double *) m->data);
		break;
	case DATA_PARSER_FLOAT128:
		xstrfmtcatat(*str, p, "%Lf", *(long double *) m->data);
		break;
	default:
		xstrfmtcatat(*str, p, "NaN");
		break;
	}
}

static int _dump_metric_no_desc(void *x, void *arg)
{
	metric_t *m = x;
	char **str = ((foreach_dump_metric_args_t *) arg)->str;
	char **p = ((foreach_dump_metric_args_t *) arg)->pos;
	metric_keyval_t *kv;

	if (!m->keyval) {
		xstrfmtcatat(*str, p, "%s ", m->name);
		_dump_metric_value(str, p, m);
		xstrfmtcatat(*str, p, "\n");
		return SLURM_SUCCESS;
	}

	xstrfmtcatat(*str, p, "%s{", m->name);
	for (int i = 0;; i++) {
		kv = m->keyval[i];
		if (!kv->key)
			break;
		xstrfmtcatat(*str, p, "%s=\"%s\"", kv->key, kv->val);
		if (m->keyval[i + 1] && m->keyval[i + 1]->key)
			xstrfmtcatat(*str, p, ",");
	}
	xstrfmtcatat(*str, p, "} ");
	_dump_metric_value(str, p, m);
	xstrfmtcatat(*str, p, "\n");

	return SLURM_SUCCESS;
}

static void _dump_metric_desc(metric_t *m, foreach_dump_metric_args_t *arg)
{
	char **str = arg->str;
	char **p = arg->pos;

	xstrfmtcatat(*str, p, "# HELP %s %s\n", m->name, m->desc);
	xstrfmtcatat(*str, p, "# TYPE %s %s\n", m->name,
		     openmetrics_type_str[m->attr]);
}

static void _dump_metrics_from_list(void *item, void *args)
{
	list_t *l = (list_t *) item;
	metric_t *first;

	first = list_peek(l);
	_dump_metric_desc(first, args);
	list_for_each_ro(l, _dump_metric_no_desc, args);
}

extern int metrics_p_dump(metric_set_t *set, char **buf)
{
	openmetrics_set_t *ometrics_set;
	char *p = NULL;
	foreach_dump_metric_args_t args = { .str = buf, .pos = &p };

	if (!(ometrics_set = _check_set(set)) || !buf || *buf)
		return SLURM_ERROR;

	xhash_walk(ometrics_set->name_hash, _dump_metrics_from_list, &args);

	return SLURM_SUCCESS;
}

static void _metrics_create_kv(metric_set_t *set, data_parser_type_t type,
			       void *data, ssize_t sz_data, char *pfx,
			       char *name, char *desc,
			       openmetrics_type_t ometric_type, char *key,
			       char *val)
{
	metric_t *metric;
	metric_keyval_t **kv = NULL;
	char *pfx_name = NULL;

	if ((key && val) && (*key && *val)) {
		kv = xcalloc(2, sizeof(*kv));
		kv[0] = xmalloc(sizeof(**kv));
		kv[0]->key = xstrdup(key);
		kv[0]->val = xstrdup(val);
		/* sentinel */
		kv[1] = xmalloc(sizeof(**kv));
		kv[1]->key = NULL;
		kv[1]->val = NULL;
	}

	if (pfx) {
		xstrfmtcat(pfx_name, "slurm_%s_%s", pfx, name);
		name = pfx_name;
	}
	metric = metrics_create_metric(set, type, data, sz_data, name, desc,
				       ometric_type, kv);
	if (_metrics_add(set, metric)) {
		if (key)
			error("Cannot add metric %s{%s=%s}", name, key, val);
		else
			error("Cannot add metric %s", name);
		metrics_free_metric(metric);
	} else {
		if (key)
			log_flag(METRICS, "Added metric %s{%s=%s}",
				 name, key, val);
		else
			log_flag(METRICS, "Added metric %s", name);
	}
	xfree(pfx_name);
}

extern metric_set_t *metrics_p_parse_nodes_metrics(nodes_stats_t *stats)
{
	uint16_t total_node_cnt = 0;
	metric_set_t *set = _metrics_new_set();

	for (int i = 0; i < stats->node_stats_count; i++) {
		if (!stats->node_stats_table[i])
			continue;
		node_stats_t *n = stats->node_stats_table[i];
		ADD_METRIC_KEYVAL(set, UINT16, n->cpus_total, node_cpus, "Total number of cpus in the node", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT16, n->cpus_alloc, node_cpus_alloc, "Allocated cpus in the node", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT16, n->cpus_efctv, node_cpus_effective, "CPUs allocatable to jobs not reserved for system usage", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT16, n->cpus_idle, node_cpus_idle, "Idle cpus in the node", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT64, n->mem_alloc, node_memory_alloc_bytes, "Bytes allocated to jobs in the node", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT64, n->mem_avail, node_memory_effective_bytes, "Memory allocatable to jobs not reserved for system usage", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT64, n->mem_free, node_memory_free_bytes, "Free memory in bytes of the node", GAUGE, "node", n->name);
		ADD_METRIC_KEYVAL(set, UINT64, n->mem_total, node_memory_bytes, "Total memory in bytes of the node", GAUGE, "node", n->name);
		total_node_cnt++;
	}
	ADD_METRIC(set, UINT16, total_node_cnt, nodes, "Total number of nodes", GAUGE);

	ADD_METRIC(set, UINT16, stats->alloc, nodes_alloc, "Number of nodes in Allocated state", GAUGE);
	ADD_METRIC(set, UINT16, stats->cg, nodes_completing, "Number of nodes with Completing flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->down, nodes_down, "Number of nodes in Down state", GAUGE);
	ADD_METRIC(set, UINT16, stats->drain, nodes_drain, "Number of nodes with Drain flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->draining, nodes_draining, "Number of nodes in draining condition (Drain state with active jobs)", GAUGE);
	ADD_METRIC(set, UINT16, stats->fail, nodes_fail, "Number of nodes with Fail flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->future, nodes_future, "Number of nodes in Future state", GAUGE);
	ADD_METRIC(set, UINT16, stats->idle, nodes_idle, "Number of nodes in Idle state", GAUGE);
	ADD_METRIC(set, UINT16, stats->invalid_reg, nodes_invalid_reg, "Number of nodes with Invalid Registration flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->maint, nodes_maint, "Number of nodes with Maintenance flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->mixed, nodes_mixed, "Number of nodes in Mixed state", GAUGE);
	ADD_METRIC(set, UINT16, stats->no_resp, nodes_noresp, "Number of nodes with Not Responding flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->planned, nodes_planned, "Number of nodes with Planned flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->reboot_requested, nodes_reboot_req, "Number of nodes with Reboot Requested flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->resv, nodes_resv, "Number of nodes with Reserved flag", GAUGE);
	ADD_METRIC(set, UINT16, stats->unknown, nodes_unknown, "Number of nodes in Unknown state", GAUGE);

	return set;
}

extern metric_set_t *metrics_p_parse_jobs_metrics(jobs_stats_t *stats)
{
	metric_set_t *set = _metrics_new_set();

	ADD_METRIC(set, UINT32, stats->bootfail, jobs_bootfail, "Number of jobs in BootFail state", GAUGE);
	ADD_METRIC(set, UINT32, stats->cancelled, jobs_cancelled, "Number of jobs in Cancelled state", GAUGE);
	ADD_METRIC(set, UINT32, stats->completed, jobs_completed, "Number of jobs in Completed state", GAUGE);
	ADD_METRIC(set, UINT32, stats->completing, jobs_completing, "Number of jobs in Completing state", GAUGE);
	ADD_METRIC(set, UINT32, stats->configuring, jobs_configuring, "Number of jobs in Configuring state", GAUGE);
	ADD_METRIC(set, UINT16, stats->cpus_alloc, jobs_cpus_alloc, "Total number of Cpus allocated by jobs", GAUGE);
	ADD_METRIC(set, UINT32, stats->deadline, jobs_deadline, "Number of jobs in Deadline state", GAUGE);
	ADD_METRIC(set, UINT32, stats->failed, jobs_failed, "Number of jobs in Failed state", GAUGE);
	ADD_METRIC(set, UINT32, stats->hold, jobs_hold, "Number of jobs in Hold state", GAUGE);
	ADD_METRIC(set, UINT32, stats->job_cnt, jobs, "Total number of jobs", GAUGE);
	ADD_METRIC(set, UINT64, stats->memory_alloc, jobs_memory_alloc, "Total memory bytes allocated by jobs", GAUGE);
	ADD_METRIC(set, UINT32, stats->node_failed, jobs_node_failed, "Number of jobs in Node Failed state", GAUGE);
	ADD_METRIC(set, UINT16, stats->nodes_alloc, jobs_nodes_alloc, "Total number of nodes allocated by jobs", GAUGE);
	ADD_METRIC(set, UINT32, stats->oom, jobs_outofmemory, "Number of jobs in Out of Memory state", GAUGE);
	ADD_METRIC(set, UINT32, stats->pending, jobs_pending, "Number of jobs in Pending state", GAUGE);
	ADD_METRIC(set, UINT32, stats->powerup_node, jobs_powerup_node, "Number of jobs in PowerUp Node state", GAUGE);
	ADD_METRIC(set, UINT32, stats->preempted, jobs_preempted, "Number of jobs in Preempted state", GAUGE);
	ADD_METRIC(set, UINT32, stats->requeued, jobs_requeued, "Number of jobs in Requeued state", GAUGE);
	ADD_METRIC(set, UINT32, stats->running, jobs_running, "Number of jobs in Running state", GAUGE);
	ADD_METRIC(set, UINT32, stats->stageout, jobs_stageout, "Number of jobs in StageOut state", GAUGE);
	ADD_METRIC(set, UINT32, stats->suspended, jobs_suspended, "Number of jobs in Suspended state", GAUGE);
	ADD_METRIC(set, UINT32, stats->timeout, jobs_timeout, "Number of jobs in Timeout state", GAUGE);

	return set;
}

static int _part_stats_to_metric(void *x, void *arg)
{
	partition_stats_t *ps = x;
	metric_set_t *set = (metric_set_t *) arg;

	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs, partition_jobs, "Number of jobs in this partition", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_bootfail, partition_jobs_bootfail, "Number of jobs in BootFail state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_cancelled, partition_jobs_cancelled, "Number of jobs in Cancelled state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_completed, partition_jobs_completed, "Number of jobs in Completed state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_completing, partition_jobs_completing, "Number of jobs in Completing state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_configuring, partition_jobs_configuring, "Number of jobs in Configuring state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->jobs_cpus_alloc, partition_jobs_cpus_alloc, "Total number of Cpus allocated by jobs", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_deadline, partition_jobs_deadline, "Number of jobs in Deadline state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_failed, partition_jobs_failed, "Number of jobs in Failed state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_hold, partition_jobs_hold, "Number of jobs in Hold state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->jobs_max_job_nodes, partition_jobs_max_job_nodes, "Max of the max_nodes required of all pending jobs in that partition", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->jobs_max_job_nodes_nohold, partition_jobs_max_job_nodes_nohold, "Max of the max_nodes required of all pending jobs in that partition excluding Held jobs", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT64, ps->jobs_memory_alloc, partition_jobs_memory_alloc, "Total memory bytes allocated by jobs", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->jobs_min_job_nodes, partition_jobs_min_job_nodes, "Max of the min_nodes required of all pending jobs in that partition", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->jobs_min_job_nodes_nohold, partition_jobs_min_job_nodes_nohold, "Max of the min_nodes required of all pending jobs in that partition excluding Held jobs", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_node_failed, partition_jobs_node_failed, "Number of jobs in Node Failed state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_oom, partition_jobs_outofmemory, "Number of jobs in Out of Memory state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_pending, partition_jobs_pending, "Number of jobs in Pending state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_powerup_node, partition_jobs_powerup_node, "Number of jobs in PowerUp Node state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_preempted, partition_jobs_preempted, "Number of jobs in Preempted state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_requeued, partition_jobs_requeued, "Number of jobs in Requeued state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_running, partition_jobs_running, "Number of jobs in Running state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_stageout, partition_jobs_stageout, "Number of jobs in StageOut state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_suspended, partition_jobs_suspended, "Number of jobs in Suspended state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_timeout, partition_jobs_timeout, "Number of jobs in Timeout state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->jobs_wait_part_node_limit, partition_jobs_wait_part_node_limit, "Jobs wait partition node limit", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_alloc, partition_nodes_alloc, "Nodes allocated", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_cg, partition_nodes_cg, "Nodes in completing state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_cpus_efctv, partition_nodes_cpus_efctv, "Number of effective CPUs on all nodes, excludes CoreSpec", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_cpus_idle, partition_nodes_cpus_idle, "Number of idle CPUs on all nodes", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_cpus_alloc,partition_nodes_cpus_alloc, "Number of allocated cpus", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_down, partition_nodes_down, "Nodes in Down state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_drain, partition_nodes_drain, "Nodes in Drain state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_draining, partition_nodes_draining, "Number of nodes in draining condition (Drain state with active jobs)", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_fail, partition_nodes_fail, "Nodes in Fail state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_future, partition_nodes_future, "Nodes in Future state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_idle, partition_nodes_idle, "Nodes in Idle state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_maint, partition_nodes_maint, "Nodes in maintenance state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT64, ps->nodes_mem_alloc, partition_nodes_mem_alloc, "Amount of allocated memory of all nodes", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT64, ps->nodes_mem_avail, partition_nodes_mem_avail, "Amount of available memory of all nodes", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT64, ps->nodes_mem_free, partition_nodes_mem_free, "Amount of free memory in all nodes", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT64, ps->nodes_mem_total, partition_nodes_mem_tot, "Total amount of memory of all nodes", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_mixed, partition_nodes_mixed, "Nodes in Mixed state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_no_resp, partition_nodes_no_resp, "Nodes in Not Responding state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_planned, partition_nodes_planned, "Nodes in Planned state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_reboot_requested, partition_nodes_reboot_requested, "Nodes with Reboot Requested flag", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_resv, partition_nodes_resv, "Nodes with Reserved flag", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->nodes_unknown, partition_nodes_unknown, "Nodes in Unknown state", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT32, ps->total_cpus, partition_cpus, "Partition total cpus", GAUGE, "partition", ps->name);
	ADD_METRIC_KEYVAL(set, UINT16, ps->total_nodes, partition_nodes, "Partition total nodes", GAUGE, "partition", ps->name);

	return SLURM_SUCCESS;
}

extern metric_set_t *metrics_p_parse_parts_metrics(partitions_stats_t *stats)
{
	metric_set_t *set = _metrics_new_set();
	uint32_t part_cnt = list_count(stats->parts);

	ADD_METRIC(set, UINT32, part_cnt, partitions, "Total number of partitions", GAUGE);
	list_for_each_ro(stats->parts, _part_stats_to_metric, set);

	return set;
}

static int _ua_stats_to_metric(void *x, void *arg)
{
	ua_stats_t *ua = x;
	jobs_stats_t *js = ua->s;
	metric_set_t *set = ((foreach_stats_parse_metric_t *) arg)->set;
	char *key = ((foreach_stats_parse_metric_t *) arg)->str;
	char *pfx = ((foreach_stats_parse_metric_t *) arg)->pfx;

	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->bootfail, pfx, jobs_bootfail, "Number of jobs in BootFail state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->cancelled, pfx, jobs_cancelled, "Number of jobs in Cancelled state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->completed, pfx, jobs_completed, "Number of jobs in Completed state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->completing, pfx, jobs_completing, "Number of jobs in Completing state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->configuring, pfx, jobs_configuring, "Number of jobs in Configuring state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT16, js->cpus_alloc, pfx, jobs_cpus_alloc, "Total number of Cpus allocated by jobs", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->deadline, pfx, jobs_deadline, "Number of jobs in Deadline state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->failed, pfx, jobs_failed, "Number of jobs in Failed state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->hold, pfx, jobs_hold, "Number of jobs in Hold state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->job_cnt, pfx, jobs, "Total number of jobs", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->memory_alloc, pfx, jobs_memory_alloc, "Total memory bytes allocated by jobs", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->node_failed, pfx, jobs_node_failed, "Number of jobs in Node Failed state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT16, js->nodes_alloc, pfx, jobs_nodes_alloc, "Total number of nodes allocated by jobs", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->oom, pfx, jobs_outofmemory, "Number of jobs in Out of Memory state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->pending, pfx, jobs_pending, "Number of jobs in Pending state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->powerup_node, pfx, jobs_powerup_node, "Number of jobs in PowerUp Node state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->preempted, pfx, jobs_preempted, "Number of jobs in Preempted state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->requeued, pfx, jobs_requeued, "Number of jobs in Requeued state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->running, pfx, jobs_running, "Number of jobs in Running state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->stageout, pfx, jobs_stageout, "Number of jobs in StageOut state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->suspended, pfx, jobs_suspended, "Number of jobs in Suspended state", GAUGE, key, ua->name);
	ADD_METRIC_KEYVAL_PFX(set, UINT32, js->timeout, pfx, jobs_timeout, "Number of jobs in Timeout state", GAUGE, key, ua->name);

	return SLURM_SUCCESS;
}

extern metric_set_t *metrics_p_parse_ua_metrics(users_accts_stats_t *stats)
{
	metric_set_t *set = _metrics_new_set();
	foreach_stats_parse_metric_t args;

	args.set = set;

	args.pfx = "user";
	args.str = "username";
	list_for_each_ro(stats->users, _ua_stats_to_metric, &args);

	args.pfx = "account";
	args.str = "account";
	list_for_each_ro(stats->accounts, _ua_stats_to_metric, &args);

	return set;
}

extern metric_set_t *metrics_p_parse_sched_metrics(scheduling_stats_t *s)
{
	metric_set_t *set = _metrics_new_set();

	ADD_METRIC(set, UINT32, s->agent_count, agent_cnt, "Number of agent threads", GAUGE);
	ADD_METRIC(set, UINT32, s->agent_queue_size, agent_queue_size, "Outgoing RPC retry queue length", GAUGE);
	ADD_METRIC(set, UINT32, s->agent_thread_count, agent_thread_cnt, "Total active agent-created threads", GAUGE);
	ADD_METRIC(set, UINT32, s->bf_depth_mean, bf_depth_mean, "Mean backfill cycle depth", GAUGE);
	ADD_METRIC(set, UINT32, s->bf_mean_cycle, bf_mean_cycle, "Mean backfill cycle time", GAUGE);
	ADD_METRIC(set, UINT32, s->bf_mean_table_sz, bf_mean_table_sz, "Mean backfill table size", GAUGE);
	ADD_METRIC(set, UINT32, s->bf_queue_len_mean, bf_queue_len_mean, "Mean backfill queue length", GAUGE);
	ADD_METRIC(set, UINT32, s->bf_try_depth_mean, bf_try_depth_mean, "Mean depth attempts in backfill", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->backfilled_het_jobs, backfilled_het_jobs, "Heterogeneous components backfilled", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->backfilled_jobs, backfilled_jobs, "Total backfilled jobs since reset", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_active, bf_active, "Backfill scheduler active jobs", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_cycle_counter, bf_cycle_cnt, "Backfill cycle count", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_cycle_last, bf_cycle_last, "Last backfill cycle time", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_cycle_max, bf_cycle_max, "Max backfill cycle time", GAUGE);
	ADD_METRIC(set, UINT64, s->diag_stats->bf_cycle_sum, bf_cycle_tot, "Sum of backfill cycle times", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_depth_sum, bf_depth_tot, "Sum of backfill job depths", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_depth_try_sum, bf_depth_try_tot, "Sum of backfill depth attempts", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_last_depth, bf_last_depth, "Last backfill depth", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_last_depth_try, bf_last_depth_try, "Last backfill depth attempts", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_queue_len, bf_queue_len, "Backfill queue length", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_queue_len_sum, bf_queue_len_tot, "Sum of backfill queue lengths", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_table_size, bf_table_size, "Backfill table size", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_table_size_sum, bf_table_size_tot, "Sum of backfill table sizes", GAUGE);
	ADD_METRIC(set, TIMESTAMP, s->diag_stats->bf_when_last_cycle, bf_when_last_cycle, "Timestamp of last backfill cycle", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_canceled, sdiag_jobs_canceled, "Jobs canceled since reset", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_completed, sdiag_jobs_completed, "Jobs completed since reset", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_failed, sdiag_jobs_failed, "Jobs failed since reset", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_pending, sdiag_jobs_pending, "Jobs pending at timestamp", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_running, sdiag_jobs_running, "Jobs running at timestamp", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_started, sdiag_jobs_started, "Jobs started since reset", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->jobs_submitted, sdiag_jobs_submitted, "Jobs submitted since reset", GAUGE);
	ADD_METRIC(set, TIMESTAMP, s->diag_stats->job_states_ts, sdiag_job_states_ts, "Job states timestamp", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->last_backfilled_jobs, last_backfilled_jobs, "Backfilled jobs since last cycle", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->latency, sdiag_latency, "Measurement latency", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_cycle_counter, schedule_cycle_cnt, "Scheduling cycle count", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_cycle_depth, schedule_cycle_depth, "Processed jobs depth total", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_cycle_last, schedule_cycle_last, "Last scheduling cycle time", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_cycle_max, schedule_cycle_max, "Max scheduling cycle time", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_cycle_sum, schedule_cycle_tot, "Sum of scheduling cycle times", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_queue_len, schedule_queue_len, "Jobs pending queue length", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_END], sched_exit_end , "End of job queue", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_MAX_DEPTH], sched_exit_max_depth, "Hit default_queue_depth", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_MAX_JOB_START], sched_exit_max_job_start, "Hit sched_max_job_start", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_LIC], sched_exit_lic, "Blocked on licenses", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_RPC_CNT], sched_exit_rpc_cnt, "Hit max_rpc_cnt", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->schedule_exit[SCHEDULE_EXIT_TIMEOUT], sched_exit_timeout, "Timeout (max_sched_time)", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_END], bf_exit_end, "End of job queue", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_MAX_JOB_START], bf_exit_max_job_start, "Hit bf_max_job_start", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_MAX_JOB_TEST], bf_exit_max_job_test, "Hit bf_max_job_test", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_STATE_CHANGED], bf_exit_state_changed, "System state changed", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_TABLE_LIMIT], bf_exit_table_limit, "Hit table size limit (bf_node_space_size)", GAUGE);
	ADD_METRIC(set, UINT32, s->diag_stats->bf_exit[BF_EXIT_TIMEOUT], bf_exit_timeout, "Timeout (bf_max_time)", GAUGE);
	ADD_METRIC(set, UINT32, s->sched_mean_cycle, sched_mean_cycle, "Mean scheduling cycle time", GAUGE);
	ADD_METRIC(set, UINT32, s->sched_mean_depth_cycle, sched_mean_depth_cycle, "Mean depth of scheduling cycles", GAUGE);
	ADD_METRIC(set, UINT32, s->server_thread_count, server_thread_cnt, "Active slurmctld threads count", GAUGE);
	ADD_METRIC(set, UINT32, s->slurmdbd_queue_size, slurmdbd_queue_size, "Queued messages to SlurmDBD", GAUGE);
	ADD_METRIC(set, UINT64, s->last_proc_req_start, last_proc_req_start, "Timestamp of last process request start", GAUGE);
	ADD_METRIC(set, TIMESTAMP, s->time, sched_stats_timestamp, "Statistics snapshot timestamp", GAUGE);

	return set;
}
