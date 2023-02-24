/*****************************************************************************\
 *  slurmdb_defs.c - definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include <stdlib.h>

#include "src/common/assoc_mgr.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/interfaces/select.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/common/slurm_time.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"

#define FORMAT_STRING_SIZE 34

slurmdb_cluster_rec_t *working_cluster_rec = NULL;

static void _free_res_cond_members(slurmdb_res_cond_t *res_cond);
static void _free_res_rec_members(slurmdb_res_rec_t *res);

strong_alias(get_qos_complete_str_bitstr, slurmdb_get_qos_complete_str_bitstr);

static void _free_clus_res_rec_members(slurmdb_clus_res_rec_t *clus_res)
{
	if (clus_res) {
		xfree(clus_res->cluster);
	}
}

static void _free_cluster_rec_members(slurmdb_cluster_rec_t *cluster)
{
	if (cluster) {
		FREE_NULL_LIST(cluster->accounting_list);
		xfree(cluster->control_host);
		xfree(cluster->dim_size);
		FREE_NULL_LIST(cluster->fed.feature_list);
		xfree(cluster->fed.name);
		slurm_persist_conn_destroy(cluster->fed.recv);
		slurm_persist_conn_destroy(cluster->fed.send);
		slurm_mutex_destroy(&cluster->lock);
		xfree(cluster->name);
		xfree(cluster->nodes);
		slurmdb_destroy_assoc_rec(cluster->root_assoc);
		FREE_NULL_LIST(cluster->send_rpc);
		xfree(cluster->tres_str);
	}
}

static void _free_federation_rec_members(slurmdb_federation_rec_t *federation)
{
	if (federation) {
		xfree(federation->name);
		FREE_NULL_LIST(federation->cluster_list);
	}
}

static void _free_wckey_rec_members(slurmdb_wckey_rec_t *wckey)
{
	if (wckey) {
		FREE_NULL_LIST(wckey->accounting_list);
		xfree(wckey->cluster);
		xfree(wckey->name);
		xfree(wckey->user);
	}
}

static void _free_cluster_cond_members(slurmdb_cluster_cond_t *cluster_cond)
{
	if (cluster_cond) {
		FREE_NULL_LIST(cluster_cond->cluster_list);
		FREE_NULL_LIST(cluster_cond->federation_list);
		FREE_NULL_LIST(cluster_cond->format_list);
		FREE_NULL_LIST(cluster_cond->plugin_id_select_list);
		FREE_NULL_LIST(cluster_cond->rpc_version_list);
	}
}

static void _free_federation_cond_members(slurmdb_federation_cond_t *fed_cond)
{
	if (fed_cond) {
		FREE_NULL_LIST(fed_cond->cluster_list);
		FREE_NULL_LIST(fed_cond->federation_list);
	}
}

static void _free_tres_cond_members(slurmdb_tres_cond_t *tres_cond)
{
	if (tres_cond) {
		FREE_NULL_LIST(tres_cond->id_list);
		FREE_NULL_LIST(tres_cond->name_list);
		FREE_NULL_LIST(tres_cond->type_list);
	}
}

static void _free_res_cond_members(slurmdb_res_cond_t *res_cond)
{
	if (res_cond) {
		FREE_NULL_LIST(res_cond->allowed_list);
		FREE_NULL_LIST(res_cond->cluster_list);
		FREE_NULL_LIST(res_cond->description_list);
		FREE_NULL_LIST(res_cond->id_list);
		FREE_NULL_LIST(res_cond->manager_list);
		FREE_NULL_LIST(res_cond->name_list);
		FREE_NULL_LIST(res_cond->server_list);
		FREE_NULL_LIST(res_cond->type_list);
	}
}

static void _free_res_rec_members(slurmdb_res_rec_t *res)
{
	if (res) {
		FREE_NULL_LIST(res->clus_res_list);
		slurmdb_destroy_clus_res_rec(res->clus_res_rec);
		xfree(res->description);
		xfree(res->manager);
		xfree(res->name);
		xfree(res->server);
	}
}


/*
 * Comparator used for sorting immediate children of acct_hierarchical_recs
 *
 * returns: -1 assoc_a < assoc_b   0: assoc_a == assoc_b   1: assoc_a > assoc_b
 *
 */

static int _sort_children_list(void *v1, void *v2)
{
	int diff = 0;
	slurmdb_hierarchical_rec_t *assoc_a;
	slurmdb_hierarchical_rec_t *assoc_b;

	assoc_a = *(slurmdb_hierarchical_rec_t **)v1;
	assoc_b = *(slurmdb_hierarchical_rec_t    **)v2;

	/* Since all these associations are on the same level we don't
	 * have to check the lfts
	 */

	/* check to see if this is a user association or an account.
	 * We want the accounts at the bottom
	 */
	if (assoc_a->assoc->user && !assoc_b->assoc->user)
		return -1;
	else if (!assoc_a->assoc->user && assoc_b->assoc->user)
		return 1;

	/* Sort by alpha */
	diff = xstrcmp(assoc_a->sort_name, assoc_b->sort_name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;

}

/*
 * Comparator used for sorting immediate children of acct_hierarchical_recs
 *
 * returns: -1 assoc_a < assoc_b   0: assoc_a == assoc_b   1: assoc_a > assoc_b
 *
 */
static int _sort_assoc_by_lft_dec(void *v1, void *v2)
{
	slurmdb_assoc_rec_t *assoc_a;
	slurmdb_assoc_rec_t *assoc_b;

	assoc_a = *(slurmdb_assoc_rec_t **)v1;
	assoc_b = *(slurmdb_assoc_rec_t **)v2;

	if (assoc_a->lft == assoc_b->lft)
		return 0;
	if (assoc_a->lft > assoc_b->lft)
		return 1;
	return -1;
}

static int _sort_slurmdb_hierarchical_rec_list(
	List slurmdb_hierarchical_rec_list)
{
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	ListIterator itr;

	if (!list_count(slurmdb_hierarchical_rec_list))
		return SLURM_SUCCESS;

	list_sort(slurmdb_hierarchical_rec_list, (ListCmpF)_sort_children_list);

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while((slurmdb_hierarchical_rec = list_next(itr))) {
		if (list_count(slurmdb_hierarchical_rec->children))
			_sort_slurmdb_hierarchical_rec_list(
				slurmdb_hierarchical_rec->children);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _append_hierarchical_children_ret_list(
	List ret_list, List slurmdb_hierarchical_rec_list)
{
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	ListIterator itr;

	if (!ret_list)
		return SLURM_ERROR;

	if (!list_count(slurmdb_hierarchical_rec_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while((slurmdb_hierarchical_rec = list_next(itr))) {
		list_append(ret_list, slurmdb_hierarchical_rec->assoc);

		if (list_count(slurmdb_hierarchical_rec->children))
			_append_hierarchical_children_ret_list(
				ret_list, slurmdb_hierarchical_rec->children);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static char *_get_qos_list_str(List qos_list)
{
	char *qos_char = NULL;
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;

	if (!qos_list)
		return NULL;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (qos_char)
			xstrfmtcat(qos_char, ",%s", qos->name);
		else
			xstrcat(qos_char, qos->name);
	}
	list_iterator_destroy(itr);

	return qos_char;
}

extern int slurmdb_setup_cluster_rec(slurmdb_cluster_rec_t *cluster_rec)
{
	int plugin_id_select = 0;

	xassert(cluster_rec);

	if (!cluster_rec->control_port) {
		debug("Slurmctld on '%s' hasn't registered yet.",
		      cluster_rec->name);
		return SLURM_ERROR;
	}

	if ((plugin_id_select = select_get_plugin_id_pos(
		     cluster_rec->plugin_id_select)) == SLURM_ERROR) {
		error("Cluster '%s' has an unknown select plugin_id %u",
		      cluster_rec->name,
		      cluster_rec->plugin_id_select);
		return SLURM_ERROR;
	}
	cluster_rec->plugin_id_select = plugin_id_select;

	slurm_set_addr(&cluster_rec->control_addr,
		       cluster_rec->control_port,
		       cluster_rec->control_host);
	if (slurm_addr_is_unspec(&cluster_rec->control_addr)) {
		error("Unable to establish control "
		      "machine address for '%s'(%s:%u)",
		      cluster_rec->name,
		      cluster_rec->control_host,
		      cluster_rec->control_port);
		return SLURM_ERROR;
	}

	if (cluster_rec->dimensions > 1) {
		int number, i, len;
		char *nodes = cluster_rec->nodes;

		cluster_rec->dim_size = xmalloc(sizeof(int) *
						cluster_rec->dimensions);
		len = strlen(nodes);
		i = len - cluster_rec->dimensions;
		if (nodes[len-1] == ']')
			i--;

		if (i > 0) {
			number = xstrntol(nodes + i, NULL,
					  cluster_rec->dimensions, 36);
			hostlist_parse_int_to_array(
				number, cluster_rec->dim_size,
				cluster_rec->dimensions, 36);
			/* all calculations this is for should
			 * be expecting 0 not to count as a
			 * number so add 1 to it. */
			for (i=0; i<cluster_rec->dimensions; i++)
				cluster_rec->dim_size[i]++;
		}
	}

	return SLURM_SUCCESS;
}

extern void slurmdb_job_cond_def_start_end(slurmdb_job_cond_t *job_cond)
{
	time_t now = time(NULL);

	if (!job_cond ||
	    (job_cond->flags & JOBCOND_FLAG_RUNAWAY) ||
	    (job_cond->flags & JOBCOND_FLAG_NO_DEFAULT_USAGE))
		return;
	/*
	 * Defaults for start (S) and end (E) times:
	 * - with -j and -s:
	 *   -S defaults to Epoch 0
	 *   -E defaults to -S (unless no -S then Now)
	 * - with only -j (NOT -s)
	 *   -S defaults to Epoch 0
	 *   -E defaults to Now
	 * - with only -s (NOT -j):
	 *   -S defaults to Now
	 *   -E defaults to -S
	 * - without either -j nor -s:
	 *   -S defaults to Midnight
	 *   -E defaults to Now
	 */
	if (job_cond->state_list && list_count(job_cond->state_list)) {
		if (!job_cond->usage_start &&
		    (!job_cond->step_list || !list_count(job_cond->step_list)))
			job_cond->usage_start = now;

		if (job_cond->usage_start && !job_cond->usage_end)
			job_cond->usage_end = job_cond->usage_start;
	} else if (!job_cond->step_list || !list_count(job_cond->step_list)) {
		if (!job_cond->usage_start) {
			struct tm start_tm;
			job_cond->usage_start = now;
			if (!localtime_r(&job_cond->usage_start, &start_tm)) {
				error("Couldn't get localtime from %ld",
				      (long)job_cond->usage_start);
			} else {
				start_tm.tm_sec = 0;
				start_tm.tm_min = 0;
				start_tm.tm_hour = 0;
				job_cond->usage_start = slurm_mktime(&start_tm);
			}
		}
	}

	if (!job_cond->usage_end)
		job_cond->usage_end = now;

	/*
	 * The query will be exclusive of the end time, that is [S,E).
	 * We must adjust E when E==S to include S, and when E==Now to
	 * include the current second.
	 */
	if ((job_cond->usage_end == job_cond->usage_start) ||
	    (job_cond->usage_end == now))
		job_cond->usage_end++;
}

static uint32_t _str_2_qos_flags(char *flags)
{
	if (xstrcasestr(flags, "DenyOnLimit"))
		return QOS_FLAG_DENY_LIMIT;

	if (xstrcasestr(flags, "EnforceUsageThreshold"))
		return QOS_FLAG_ENFORCE_USAGE_THRES;

	if (xstrcasestr(flags, "PartitionMinNodes"))
		return QOS_FLAG_PART_MIN_NODE;

	if (xstrcasestr(flags, "PartitionMaxNodes"))
		return QOS_FLAG_PART_MAX_NODE;

	if (xstrcasestr(flags, "PartitionTimeLimit"))
		return QOS_FLAG_PART_TIME_LIMIT;

	if (xstrcasestr(flags, "RequiresReservation"))
		return QOS_FLAG_REQ_RESV;

	if (xstrcasestr(flags, "OverPartQOS"))
		return QOS_FLAG_OVER_PART_QOS;

	if (xstrcasestr(flags, "NoReserve"))
		return QOS_FLAG_NO_RESERVE;

	if (xstrcasestr(flags, "NoDecay"))
		return QOS_FLAG_NO_DECAY;

	if (xstrcasestr(flags, "UsageFactorSafe"))
		return QOS_FLAG_USAGE_FACTOR_SAFE;

	return 0;
}

static uint32_t _str_2_res_flags(char *flags)
{

	if (xstrcasestr(flags, "Absolute"))
		return SLURMDB_RES_FLAG_ABSOLUTE;

	return 0;
}

static uint32_t _str_2_job_flags(char *flags)
{
	if (xstrcasestr(flags, "None"))
		return SLURMDB_JOB_FLAG_NONE;

	if (xstrcasestr(flags, "SchedSubmit"))
		return SLURMDB_JOB_FLAG_SUBMIT;

	if (xstrcasestr(flags, "SchedMain"))
		return SLURMDB_JOB_FLAG_SCHED;

	if (xstrcasestr(flags, "SchedBackfill"))
		return SLURMDB_JOB_FLAG_BACKFILL;

	if (xstrcasestr(flags, "StartRecieved"))
		return SLURMDB_JOB_FLAG_START_R;

	return SLURMDB_JOB_FLAG_NOTSET;
}

static int _sort_local_cluster(void *v1, void *v2)
{
	local_cluster_rec_t* rec_a = *(local_cluster_rec_t**)v1;
	local_cluster_rec_t* rec_b = *(local_cluster_rec_t**)v2;

	if (rec_a->start_time < rec_b->start_time)
		return -1;
	else if (rec_a->start_time > rec_b->start_time)
		return 1;

	if (rec_a->preempt_cnt < rec_b->preempt_cnt)
		return -1;
	else if (rec_a->preempt_cnt > rec_b->preempt_cnt)
		return 1;

	if (!xstrcmp(slurm_conf.cluster_name, rec_a->cluster_rec->name))
		return -1;
	else if (!xstrcmp(slurm_conf.cluster_name, rec_b->cluster_rec->name))
		return 1;

	return 0;
}

static local_cluster_rec_t * _job_will_run (job_desc_msg_t *req)
{
	local_cluster_rec_t *local_cluster = NULL;
	will_run_response_msg_t *will_run_resp;
	char buf[256];
	int rc;

	rc = slurm_job_will_run2(req, &will_run_resp);

	if (rc >= 0) {
		slurm_make_time_str(&will_run_resp->start_time,
				    buf, sizeof(buf));
		debug("Job %u to start at %s on cluster %s using %u processors on nodes %s in partition %s",
		      will_run_resp->job_id, buf, working_cluster_rec->name,
		      will_run_resp->proc_cnt, will_run_resp->node_list,
		      will_run_resp->part_name);

		local_cluster = xmalloc(sizeof(local_cluster_rec_t));
		local_cluster->cluster_rec = working_cluster_rec;
		local_cluster->start_time = will_run_resp->start_time;

		if (will_run_resp->preemptee_job_id) {
			ListIterator itr;
			uint32_t *job_id_ptr;
			char *job_list = NULL, *sep = "";
			local_cluster->preempt_cnt = list_count(
				will_run_resp->preemptee_job_id);
			itr = list_iterator_create(will_run_resp->
						   preemptee_job_id);
			while ((job_id_ptr = list_next(itr))) {
				if (job_list)
					sep = ",";
				xstrfmtcat(job_list, "%s%u",
					   sep, *job_id_ptr);
			}
			list_iterator_destroy(itr);
			debug("  Preempts: %s", job_list);
			xfree(job_list);
		}

		slurm_free_will_run_response_msg(will_run_resp);
	}

	return local_cluster;
}

static int _set_qos_bit_from_string(bitstr_t *valid_qos, char *name)
{
	void (*my_function) (bitstr_t *b, bitoff_t bit);
	bitoff_t bit = 0;

	xassert(valid_qos);

	if (!name)
		return SLURM_ERROR;

	if (name[0] == '-') {
		name++;
		my_function = bit_clear;
	} else if (name[0] == '+') {
		name++;
		my_function = bit_set;
	} else
		my_function = bit_set;

	if ((bit = atoi(name)) >= bit_size(valid_qos))
		return SLURM_ERROR;

	(*(my_function))(valid_qos, bit);

	return SLURM_SUCCESS;
}

static void _add_arch_rec(slurmdb_assoc_rec_t *assoc_rec,
			  List arch_rec_list, xhash_t *all_parents)
{
	slurmdb_hierarchical_rec_t *arch_rec =
		xmalloc(sizeof(slurmdb_hierarchical_rec_t));

	arch_rec->children =
		list_create(slurmdb_destroy_hierarchical_rec);
	arch_rec->assoc = assoc_rec;

	if (!assoc_rec->parent_id)
		arch_rec->sort_name = assoc_rec->cluster;
	else if (assoc_rec->user)
		arch_rec->sort_name = assoc_rec->user;
	else
		arch_rec->sort_name = assoc_rec->acct;

	assoc_rec->rgt = 0;
	list_append(arch_rec_list, arch_rec);
	if (!assoc_rec->user) /* Users are never parent assocs */
		xhash_add(all_parents, arch_rec);
}

static char *_create_hash_rec_id(slurmdb_assoc_rec_t *assoc, bool parent)
{
	/*
	 * Add comma to key to distinguish between id and cluster name
	 * .e.g
	 * associd: 11 cluster: foo
	 * associd: 1  cluster: 1foo
	 */
	return xstrdup_printf("%u,%s",
			      parent ? assoc->parent_id : assoc->id,
			      assoc->cluster);
}

static void _arch_hash_rec_id(void *item, const char **key, uint32_t *key_len)
{
	slurmdb_hierarchical_rec_t *arch_rec = item;

	xfree(arch_rec->key);
	arch_rec->key = _create_hash_rec_id(arch_rec->assoc, false);
	*key = arch_rec->key;
	*key_len = strlen(*key);
}

static void _find_create_parent(slurmdb_assoc_rec_t *assoc_rec, List assoc_list,
				List arch_rec_list, xhash_t *all_parents)
{
	slurmdb_assoc_rec_t *par_assoc_rec = NULL;
	slurmdb_hierarchical_rec_t *par_arch_rec = NULL;

	if (assoc_rec->parent_id) {
		char *key = _create_hash_rec_id(assoc_rec, true);
		par_arch_rec = xhash_get(all_parents, key, strlen(key));
		if (par_arch_rec) {
			_add_arch_rec(assoc_rec, par_arch_rec->children,
				      all_parents);
			xfree(key);
			return;
		}

		if (!(par_assoc_rec = list_find_first(
			      assoc_list, slurmdb_find_assoc_in_list,
			      &assoc_rec->parent_id))) {

			/* This means we weren't starting at root */
			_add_arch_rec(assoc_rec, arch_rec_list,
				      all_parents);
			xfree(key);
			return;
		}

		_find_create_parent(par_assoc_rec, assoc_list, arch_rec_list,
				    all_parents);

		/* Now that it has been added lets try again */
		par_arch_rec = xhash_get(all_parents, key, strlen(key));
		xfree(key);
		if (par_arch_rec) {
			_add_arch_rec(assoc_rec, par_arch_rec->children,
				      all_parents);
			return;
		}
		error("%s: no parent found, this should never happen",
		      __func__);
	} else
		_add_arch_rec(assoc_rec, arch_rec_list, all_parents);

	return;
}

extern slurmdb_job_rec_t *slurmdb_create_job_rec()
{
	slurmdb_job_rec_t *job = xmalloc(sizeof(slurmdb_job_rec_t));
	job->array_task_id = NO_VAL;
	job->derived_ec = NO_VAL;
	job->state = JOB_PENDING;
	job->steps = list_create(slurmdb_destroy_step_rec);
	job->requid = -1;
	job->lft = NO_VAL;
	job->resvid = NO_VAL;

      	return job;
}

extern slurmdb_step_rec_t *slurmdb_create_step_rec()
{
	slurmdb_step_rec_t *step = xmalloc(sizeof(slurmdb_step_rec_t));
	memset(&step->stats, 0, sizeof(slurmdb_stats_t));
	step->step_id.step_id = NO_VAL;
	step->step_id.step_het_comp = NO_VAL;
	step->state = NO_VAL;
	step->exitcode = NO_VAL;
	step->elapsed = NO_VAL;
	step->tot_cpu_sec = NO_VAL;
	step->tot_cpu_usec = NO_VAL;
	step->requid = -1;

	return step;
}

extern slurmdb_assoc_usage_t *slurmdb_create_assoc_usage(int tres_cnt)
{
	slurmdb_assoc_usage_t *usage;
	int alloc_size;

	if (!tres_cnt)
		fatal("%s: You need to give a tres_cnt to call this function",
		      __func__);

	usage =	xmalloc(sizeof(slurmdb_assoc_usage_t));

	usage->level_shares = NO_VAL;
	usage->shares_norm = (double)NO_VAL64;
	usage->usage_efctv = 0;
	usage->usage_norm = (long double)NO_VAL;
	usage->usage_raw = 0;
	usage->level_fs = 0;
	usage->fs_factor = 0;

	usage->tres_cnt = tres_cnt;

	alloc_size = sizeof(uint64_t) * tres_cnt;
	usage->grp_used_tres = xmalloc(alloc_size);
	usage->grp_used_tres_run_secs = xmalloc(alloc_size);

	usage->usage_tres_raw = xmalloc(sizeof(long double) * tres_cnt);

	return usage;
}

extern slurmdb_qos_usage_t *slurmdb_create_qos_usage(int tres_cnt)
{
	slurmdb_qos_usage_t *usage =
		xmalloc(sizeof(slurmdb_qos_usage_t));

	if (tres_cnt) {
		int alloc_size = sizeof(uint64_t) * tres_cnt;
		usage->tres_cnt = tres_cnt;
		usage->grp_used_tres_run_secs = xmalloc(alloc_size);
		usage->grp_used_tres = xmalloc(alloc_size);
		usage->usage_tres_raw = xmalloc(sizeof(long double) * tres_cnt);
	}

	return usage;
}

extern void slurmdb_destroy_assoc_usage(void *object)
{
	slurmdb_assoc_usage_t *usage =
		(slurmdb_assoc_usage_t *)object;

	if (usage) {
		FREE_NULL_LIST(usage->children_list);
		FREE_NULL_BITMAP(usage->grp_node_bitmap);
		xfree(usage->grp_node_job_cnt);
		xfree(usage->grp_used_tres_run_secs);
		xfree(usage->grp_used_tres);
		xfree(usage->usage_tres_raw);
		FREE_NULL_BITMAP(usage->valid_qos);
		xfree(usage);
	}
}

extern void slurmdb_destroy_bf_usage(void *object)
{
	slurmdb_destroy_bf_usage_members(object);
	xfree(object);
}

extern void slurmdb_destroy_bf_usage_members(void *object)
{
	return;
}

extern void slurmdb_destroy_qos_usage(void *object)
{
	slurmdb_qos_usage_t *usage =
		(slurmdb_qos_usage_t *)object;

	if (usage) {
		FREE_NULL_LIST(usage->acct_limit_list);
		FREE_NULL_BITMAP(usage->grp_node_bitmap);
		xfree(usage->grp_node_job_cnt);
		xfree(usage->grp_used_tres_run_secs);
		xfree(usage->grp_used_tres);
		FREE_NULL_LIST(usage->job_list);
		xfree(usage->usage_tres_raw);
		FREE_NULL_LIST(usage->user_limit_list);
		xfree(usage);
	}
}


extern void slurmdb_destroy_user_rec(void *object)
{
	slurmdb_user_rec_t *slurmdb_user = (slurmdb_user_rec_t *)object;

	if (slurmdb_user) {
		FREE_NULL_LIST(slurmdb_user->assoc_list);
		FREE_NULL_LIST(slurmdb_user->coord_accts);
		xfree(slurmdb_user->default_acct);
		xfree(slurmdb_user->default_wckey);
		xfree(slurmdb_user->name);
		xfree(slurmdb_user->old_name);
		FREE_NULL_LIST(slurmdb_user->wckey_list);
		slurmdb_destroy_bf_usage(slurmdb_user->bf_usage);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_rec(void *object)
{
	slurmdb_account_rec_t *slurmdb_account =
		(slurmdb_account_rec_t *)object;

	if (slurmdb_account) {
		FREE_NULL_LIST(slurmdb_account->assoc_list);
		FREE_NULL_LIST(slurmdb_account->coordinators);
		xfree(slurmdb_account->description);
		xfree(slurmdb_account->name);
		xfree(slurmdb_account->organization);
		xfree(slurmdb_account);
	}
}

extern void slurmdb_destroy_coord_rec(void *object)
{
	slurmdb_coord_rec_t *slurmdb_coord =
		(slurmdb_coord_rec_t *)object;

	if (slurmdb_coord) {
		xfree(slurmdb_coord->name);
		xfree(slurmdb_coord);
	}
}

extern void slurmdb_destroy_cluster_accounting_rec(void *object)
{
	slurmdb_cluster_accounting_rec_t *clusteracct_rec =
		(slurmdb_cluster_accounting_rec_t *)object;

	if (clusteracct_rec) {
		slurmdb_destroy_tres_rec_noalloc(
			&clusteracct_rec->tres_rec);
		xfree(clusteracct_rec);
	}
}

extern void slurmdb_destroy_clus_res_rec(void *object)
{
	slurmdb_clus_res_rec_t *slurmdb_clus_res =
		(slurmdb_clus_res_rec_t *)object;

	if (slurmdb_clus_res) {
		_free_clus_res_rec_members(slurmdb_clus_res);
		xfree(slurmdb_clus_res);
	}
}

extern void slurmdb_destroy_cluster_rec(void *object)
{
	slurmdb_cluster_rec_t *slurmdb_cluster =
		(slurmdb_cluster_rec_t *)object;

	if (slurmdb_cluster) {
		_free_cluster_rec_members(slurmdb_cluster);
		xfree(slurmdb_cluster);
	}
}

extern void slurmdb_destroy_federation_rec(void *object)
{
	slurmdb_federation_rec_t *slurmdb_federation =
		(slurmdb_federation_rec_t *)object;

	if (slurmdb_federation) {
		_free_federation_rec_members(slurmdb_federation);
		xfree(slurmdb_federation);
	}
}

extern void slurmdb_destroy_accounting_rec(void *object)
{
	slurmdb_accounting_rec_t *slurmdb_accounting =
		(slurmdb_accounting_rec_t *)object;

	if (slurmdb_accounting) {
		slurmdb_destroy_tres_rec_noalloc(
			&slurmdb_accounting->tres_rec);
		xfree(slurmdb_accounting);
	}
}

extern void slurmdb_free_assoc_rec_members(slurmdb_assoc_rec_t *assoc)
{
	if (assoc) {
		FREE_NULL_LIST(assoc->accounting_list);
		xfree(assoc->acct);
		xfree(assoc->cluster);
		xfree(assoc->comment);
		xfree(assoc->grp_tres);
		xfree(assoc->grp_tres_ctld);
		xfree(assoc->grp_tres_mins);
		xfree(assoc->grp_tres_mins_ctld);
		xfree(assoc->grp_tres_run_mins);
		xfree(assoc->grp_tres_run_mins_ctld);
		xfree(assoc->max_tres_mins_pj);
		xfree(assoc->max_tres_mins_ctld);
		xfree(assoc->max_tres_run_mins);
		xfree(assoc->max_tres_run_mins_ctld);
		xfree(assoc->max_tres_pj);
		xfree(assoc->max_tres_ctld);
		xfree(assoc->max_tres_pn);
		xfree(assoc->max_tres_pn_ctld);
		xfree(assoc->parent_acct);
		xfree(assoc->partition);
		FREE_NULL_LIST(assoc->qos_list);
		xfree(assoc->user);

		/* Account with previously deleted users */
		if (assoc->leaf_usage != assoc->usage)
			slurmdb_destroy_assoc_usage(assoc->leaf_usage);
		/*
		 * Be crazy safe and set this to NULL as it should never be used
		 * again!
		 */
		assoc->leaf_usage = NULL;

		slurmdb_destroy_assoc_usage(assoc->usage);
		/* NOTE assoc->user_rec is a soft reference, do not free here */
		assoc->user_rec = NULL;
		slurmdb_destroy_bf_usage(assoc->bf_usage);
	}
}

extern void slurmdb_destroy_assoc_rec(void *object)
{
	slurmdb_assoc_rec_t *slurmdb_assoc =
		(slurmdb_assoc_rec_t *)object;

	if (slurmdb_assoc) {
		slurmdb_free_assoc_rec_members(slurmdb_assoc);
		xfree(slurmdb_assoc);
	}
}

extern void slurmdb_destroy_event_rec(void *object)
{
	slurmdb_event_rec_t *slurmdb_event =
		(slurmdb_event_rec_t *)object;

	if (slurmdb_event) {
		xfree(slurmdb_event->cluster);
		xfree(slurmdb_event->cluster_nodes);
		xfree(slurmdb_event->node_name);
		xfree(slurmdb_event->reason);
		xfree(slurmdb_event->tres_str);

		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_rec(void *object)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	if (job) {
		xfree(job->account);
		xfree(job->admin_comment);
		xfree(job->array_task_str);
		xfree(job->blockid);
		xfree(job->cluster);
		xfree(job->constraints);
		xfree(job->container);
		xfree(job->derived_es);
		xfree(job->env);
		xfree(job->extra);
		xfree(job->failed_node);
		xfree(job->jobname);
		xfree(job->licenses);
		xfree(job->mcs_label);
		xfree(job->partition);
		xfree(job->nodes);
		xfree(job->resv_name);
		xfree(job->script);
		FREE_NULL_LIST(job->steps);
		xfree(job->submit_line);
		xfree(job->system_comment);
		xfree(job->tres_alloc_str);
		xfree(job->tres_req_str);
		xfree(job->user);
		xfree(job->wckey);
		xfree(job->work_dir);
		xfree(job);
	}
}

extern void slurmdb_free_qos_rec_members(slurmdb_qos_rec_t *qos)
{
	if (qos) {
		xfree(qos->description);
		xfree(qos->grp_tres);
		xfree(qos->grp_tres_ctld);
		xfree(qos->grp_tres_mins);
		xfree(qos->grp_tres_mins_ctld);
		xfree(qos->grp_tres_run_mins);
		xfree(qos->grp_tres_run_mins_ctld);
		xfree(qos->max_tres_mins_pj);
		xfree(qos->max_tres_mins_pj_ctld);
		xfree(qos->max_tres_run_mins_pa);
		xfree(qos->max_tres_run_mins_pa_ctld);
		xfree(qos->max_tres_run_mins_pu);
		xfree(qos->max_tres_run_mins_pu_ctld);
		xfree(qos->max_tres_pa);
		xfree(qos->max_tres_pa_ctld);
		xfree(qos->max_tres_pj);
		xfree(qos->max_tres_pj_ctld);
		xfree(qos->max_tres_pn);
		xfree(qos->max_tres_pn_ctld);
		xfree(qos->max_tres_pu);
		xfree(qos->max_tres_pu_ctld);
		xfree(qos->min_tres_pj);
		xfree(qos->min_tres_pj_ctld);
		xfree(qos->name);
		FREE_NULL_BITMAP(qos->preempt_bitstr);
		FREE_NULL_LIST(qos->preempt_list);
		slurmdb_destroy_qos_usage(qos->usage);
	}
}

extern void slurmdb_destroy_qos_rec(void *object)
{
	slurmdb_qos_rec_t *slurmdb_qos = (slurmdb_qos_rec_t *)object;
	if (slurmdb_qos) {
		slurmdb_free_qos_rec_members(slurmdb_qos);
		xfree(slurmdb_qos);
	}
}

extern void slurmdb_destroy_reservation_rec(void *object)
{
	slurmdb_reservation_rec_t *slurmdb_resv =
		(slurmdb_reservation_rec_t *)object;
	if (slurmdb_resv) {
		xfree(slurmdb_resv->assocs);
		xfree(slurmdb_resv->cluster);
		xfree(slurmdb_resv->comment);
		xfree(slurmdb_resv->name);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv->node_inx);
		xfree(slurmdb_resv->tres_str);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_step_rec(void *object)
{
	slurmdb_step_rec_t *step = (slurmdb_step_rec_t *)object;
	if (step) {
		xfree(step->container);
		xfree(step->nodes);
		xfree(step->pid_str);
		slurmdb_free_slurmdb_stats_members(&step->stats);
		xfree(step->stepname);
		xfree(step->submit_line);
		xfree(step->tres_alloc_str);
		xfree(step);
	}
}

extern void slurmdb_destroy_res_rec(void *object)
{
	slurmdb_res_rec_t *slurmdb_res =
		(slurmdb_res_rec_t *)object;

	if (slurmdb_res) {
		_free_res_rec_members(slurmdb_res);
		xfree(slurmdb_res);
	}
}

extern void slurmdb_destroy_txn_rec(void *object)
{
	slurmdb_txn_rec_t *slurmdb_txn = (slurmdb_txn_rec_t *)object;
	if (slurmdb_txn) {
		xfree(slurmdb_txn->accts);
		xfree(slurmdb_txn->actor_name);
		xfree(slurmdb_txn->clusters);
		xfree(slurmdb_txn->set_info);
		xfree(slurmdb_txn->users);
		xfree(slurmdb_txn->where_query);
		xfree(slurmdb_txn);
	}
}

extern void slurmdb_destroy_wckey_rec(void *object)
{
	slurmdb_wckey_rec_t *wckey = (slurmdb_wckey_rec_t *)object;

	if (wckey) {
		_free_wckey_rec_members(wckey);
		xfree(wckey);
	}
}

extern void slurmdb_destroy_archive_rec(void *object)
{
	slurmdb_archive_rec_t *arch_rec = (slurmdb_archive_rec_t *)object;

	if (arch_rec) {
		xfree(arch_rec->archive_file);
		xfree(arch_rec->insert);
		xfree(arch_rec);
	}
}

extern void slurmdb_destroy_tres_rec_noalloc(void *object)
{
	slurmdb_tres_rec_t *tres_rec = (slurmdb_tres_rec_t *)object;

	if (!tres_rec)
		return;

	xfree(tres_rec->name);
	xfree(tres_rec->type);
}

extern void slurmdb_destroy_tres_rec(void *object)
{
	slurmdb_tres_rec_t *tres_rec = (slurmdb_tres_rec_t *)object;

	if (tres_rec) {
		slurmdb_destroy_tres_rec_noalloc(tres_rec);
		xfree(tres_rec);
	}
}

extern void slurmdb_destroy_report_assoc_rec(void *object)
{
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc =
		(slurmdb_report_assoc_rec_t *)object;
	if (slurmdb_report_assoc) {
		xfree(slurmdb_report_assoc->acct);
		xfree(slurmdb_report_assoc->cluster);
		xfree(slurmdb_report_assoc->parent_acct);
		FREE_NULL_LIST(slurmdb_report_assoc->tres_list);
		xfree(slurmdb_report_assoc->user);
		xfree(slurmdb_report_assoc);
	}
}

extern void slurmdb_destroy_report_user_rec(void *object)
{
	slurmdb_report_user_rec_t *slurmdb_report_user =
		(slurmdb_report_user_rec_t *)object;
	if (slurmdb_report_user) {
		xfree(slurmdb_report_user->acct);
		FREE_NULL_LIST(slurmdb_report_user->acct_list);
		FREE_NULL_LIST(slurmdb_report_user->assoc_list);
		xfree(slurmdb_report_user->name);
		FREE_NULL_LIST(slurmdb_report_user->tres_list);
		xfree(slurmdb_report_user);
	}
}

extern void slurmdb_destroy_report_cluster_rec(void *object)
{
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster =
		(slurmdb_report_cluster_rec_t *)object;
	if (slurmdb_report_cluster) {
		FREE_NULL_LIST(slurmdb_report_cluster->assoc_list);
		xfree(slurmdb_report_cluster->name);
		FREE_NULL_LIST(slurmdb_report_cluster->tres_list);
		FREE_NULL_LIST(slurmdb_report_cluster->user_list);
		xfree(slurmdb_report_cluster);
	}
}

extern void slurmdb_destroy_user_cond(void *object)
{
	slurmdb_user_cond_t *slurmdb_user = (slurmdb_user_cond_t *)object;

	if (slurmdb_user) {
		slurmdb_destroy_assoc_cond(slurmdb_user->assoc_cond);
		FREE_NULL_LIST(slurmdb_user->def_acct_list);
		FREE_NULL_LIST(slurmdb_user->def_wckey_list);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_cond(void *object)
{
	slurmdb_account_cond_t *slurmdb_account =
		(slurmdb_account_cond_t *)object;

	if (slurmdb_account) {
		slurmdb_destroy_assoc_cond(slurmdb_account->assoc_cond);
		FREE_NULL_LIST(slurmdb_account->description_list);
		FREE_NULL_LIST(slurmdb_account->organization_list);
		xfree(slurmdb_account);
	}
}

extern void slurmdb_destroy_cluster_cond(void *object)
{
	slurmdb_cluster_cond_t *slurmdb_cluster =
		(slurmdb_cluster_cond_t *)object;

	if (slurmdb_cluster) {
		_free_cluster_cond_members(slurmdb_cluster);
		xfree(slurmdb_cluster);
	}
}

extern void slurmdb_destroy_federation_cond(void *object)
{
	slurmdb_federation_cond_t *slurmdb_federation =
		(slurmdb_federation_cond_t *)object;

	if (slurmdb_federation) {
		_free_federation_cond_members(slurmdb_federation);
		xfree(slurmdb_federation);
	}
}

extern void slurmdb_destroy_tres_cond(void *object)
{
	slurmdb_tres_cond_t *slurmdb_tres =
		(slurmdb_tres_cond_t *)object;

	if (slurmdb_tres) {
		_free_tres_cond_members(slurmdb_tres);
		xfree(slurmdb_tres);
	}
}

extern void slurmdb_destroy_assoc_cond(void *object)
{
	slurmdb_assoc_cond_t *slurmdb_assoc =
		(slurmdb_assoc_cond_t *)object;

	if (slurmdb_assoc) {
		FREE_NULL_LIST(slurmdb_assoc->acct_list);
		FREE_NULL_LIST(slurmdb_assoc->cluster_list);
		FREE_NULL_LIST(slurmdb_assoc->def_qos_id_list);
		FREE_NULL_LIST(slurmdb_assoc->id_list);
		FREE_NULL_LIST(slurmdb_assoc->partition_list);
		FREE_NULL_LIST(slurmdb_assoc->parent_acct_list);
		FREE_NULL_LIST(slurmdb_assoc->qos_list);
		FREE_NULL_LIST(slurmdb_assoc->user_list);
		xfree(slurmdb_assoc);
	}
}

extern void slurmdb_destroy_event_cond(void *object)
{
	slurmdb_event_cond_t *slurmdb_event =
		(slurmdb_event_cond_t *)object;

	if (slurmdb_event) {
		FREE_NULL_LIST(slurmdb_event->cluster_list);
		FREE_NULL_LIST(slurmdb_event->format_list);
		FREE_NULL_LIST(slurmdb_event->reason_list);
		FREE_NULL_LIST(slurmdb_event->reason_uid_list);
		FREE_NULL_LIST(slurmdb_event->state_list);
		xfree(slurmdb_event->node_list);
		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_cond(void *object)
{
	slurmdb_job_cond_t *job_cond =
		(slurmdb_job_cond_t *)object;

	if (job_cond) {
		FREE_NULL_LIST(job_cond->acct_list);
		FREE_NULL_LIST(job_cond->associd_list);
		FREE_NULL_LIST(job_cond->cluster_list);
		FREE_NULL_LIST(job_cond->constraint_list);
		FREE_NULL_LIST(job_cond->groupid_list);
		FREE_NULL_LIST(job_cond->jobname_list);
		FREE_NULL_LIST(job_cond->partition_list);
		FREE_NULL_LIST(job_cond->qos_list);
		FREE_NULL_LIST(job_cond->reason_list);
		FREE_NULL_LIST(job_cond->resv_list);
		FREE_NULL_LIST(job_cond->resvid_list);
		FREE_NULL_LIST(job_cond->step_list);
		FREE_NULL_LIST(job_cond->state_list);
		xfree(job_cond->used_nodes);
		FREE_NULL_LIST(job_cond->userid_list);
		FREE_NULL_LIST(job_cond->wckey_list);
		xfree(job_cond);
	}
}

extern void slurmdb_destroy_qos_cond(void *object)
{
	slurmdb_qos_cond_t *slurmdb_qos = (slurmdb_qos_cond_t *)object;
	if (slurmdb_qos) {
		FREE_NULL_LIST(slurmdb_qos->id_list);
		FREE_NULL_LIST(slurmdb_qos->name_list);
		xfree(slurmdb_qos);
	}
}

extern void slurmdb_destroy_res_cond(void *object)
{
	slurmdb_res_cond_t *slurmdb_res =
		(slurmdb_res_cond_t *)object;
	if (slurmdb_res) {
		_free_res_cond_members(slurmdb_res);
		xfree(slurmdb_res);
	}
}

extern void slurmdb_destroy_reservation_cond(void *object)
{
	slurmdb_reservation_cond_t *slurmdb_resv =
		(slurmdb_reservation_cond_t *)object;
	if (slurmdb_resv) {
		FREE_NULL_LIST(slurmdb_resv->cluster_list);
		FREE_NULL_LIST(slurmdb_resv->id_list);
		FREE_NULL_LIST(slurmdb_resv->name_list);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_txn_cond(void *object)
{
	slurmdb_txn_cond_t *slurmdb_txn = (slurmdb_txn_cond_t *)object;
	if (slurmdb_txn) {
		FREE_NULL_LIST(slurmdb_txn->acct_list);
		FREE_NULL_LIST(slurmdb_txn->action_list);
		FREE_NULL_LIST(slurmdb_txn->actor_list);
		FREE_NULL_LIST(slurmdb_txn->cluster_list);
		FREE_NULL_LIST(slurmdb_txn->id_list);
		FREE_NULL_LIST(slurmdb_txn->info_list);
		FREE_NULL_LIST(slurmdb_txn->name_list);
		FREE_NULL_LIST(slurmdb_txn->user_list);
		xfree(slurmdb_txn);
	}
}

extern void slurmdb_destroy_wckey_cond(void *object)
{
	slurmdb_wckey_cond_t *wckey = (slurmdb_wckey_cond_t *)object;

	if (wckey) {
		FREE_NULL_LIST(wckey->cluster_list);
		FREE_NULL_LIST(wckey->id_list);
		FREE_NULL_LIST(wckey->name_list);
		FREE_NULL_LIST(wckey->user_list);
		xfree(wckey);
	}
}

extern void slurmdb_destroy_archive_cond(void *object)
{
	slurmdb_archive_cond_t *arch_cond = (slurmdb_archive_cond_t *)object;

	if (arch_cond) {
		xfree(arch_cond->archive_dir);
		xfree(arch_cond->archive_script);
		slurmdb_destroy_job_cond(arch_cond->job_cond);
		xfree(arch_cond);

	}
}

extern void slurmdb_destroy_update_object(void *object)
{
	slurmdb_update_object_t *slurmdb_update =
		(slurmdb_update_object_t *) object;

	if (slurmdb_update) {
		FREE_NULL_LIST(slurmdb_update->objects);
		xfree(slurmdb_update);
	}
}

extern void slurmdb_destroy_used_limits(void *object)
{
	slurmdb_used_limits_t *slurmdb_used_limits =
		(slurmdb_used_limits_t *)object;

	if (slurmdb_used_limits) {
		xfree(slurmdb_used_limits->acct);
		FREE_NULL_BITMAP(slurmdb_used_limits->node_bitmap);
		xfree(slurmdb_used_limits->node_job_cnt);
		xfree(slurmdb_used_limits->tres);
		xfree(slurmdb_used_limits->tres_run_mins);
		xfree(slurmdb_used_limits);
	}
}

extern void slurmdb_destroy_print_tree(void *object)
{
	slurmdb_print_tree_t *slurmdb_print_tree =
		(slurmdb_print_tree_t *)object;

	if (slurmdb_print_tree) {
		xfree(slurmdb_print_tree->name);
		xfree(slurmdb_print_tree->print_name);
		xfree(slurmdb_print_tree->spaces);
		xfree(slurmdb_print_tree);
	}
}

extern void slurmdb_destroy_hierarchical_rec(void *object)
{
	/* Most of this is pointers to something else that will be
	 * destroyed elsewhere.
	 */
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec =
		(slurmdb_hierarchical_rec_t *)object;
	if (slurmdb_hierarchical_rec) {
		xfree(slurmdb_hierarchical_rec->key);
		FREE_NULL_LIST(slurmdb_hierarchical_rec->children);
		xfree(slurmdb_hierarchical_rec);
	}
}

extern void slurmdb_destroy_report_job_grouping(void *object)
{
	slurmdb_report_job_grouping_t *job_grouping =
		(slurmdb_report_job_grouping_t *)object;
	if (job_grouping) {
		FREE_NULL_LIST(job_grouping->jobs);
		FREE_NULL_LIST(job_grouping->tres_list);
		xfree(job_grouping);
	}
}

extern void slurmdb_destroy_report_acct_grouping(void *object)
{
	slurmdb_report_acct_grouping_t *acct_grouping =
		(slurmdb_report_acct_grouping_t *)object;
	if (acct_grouping) {
		xfree(acct_grouping->acct);
		FREE_NULL_LIST(acct_grouping->groups);
		FREE_NULL_LIST(acct_grouping->tres_list);
		xfree(acct_grouping);
	}
}

extern void slurmdb_destroy_report_cluster_grouping(void *object)
{
	slurmdb_report_cluster_grouping_t *cluster_grouping =
		(slurmdb_report_cluster_grouping_t *)object;
	if (cluster_grouping) {
		xfree(cluster_grouping->cluster);
		FREE_NULL_LIST(cluster_grouping->acct_list);
		FREE_NULL_LIST(cluster_grouping->tres_list);
		xfree(cluster_grouping);
	}
}

extern List slurmdb_get_info_cluster(char *cluster_names)
{
	slurmdb_cluster_rec_t *cluster_rec = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List temp_list = NULL;
	char *cluster_name = NULL;
	void *db_conn = NULL;
	ListIterator itr, itr2;
	bool all_clusters = 0;

	if (cluster_names && !xstrcasecmp(cluster_names, "all"))
		all_clusters = 1;

	db_conn = acct_storage_g_get_connection(0, NULL, 1,
						slurm_conf.cluster_name);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	if (cluster_names && !all_clusters) {
		cluster_cond.cluster_list = list_create(xfree_ptr);
		slurm_addto_char_list(cluster_cond.cluster_list, cluster_names);
	}

	if (!(temp_list = acct_storage_g_get_clusters(db_conn, getuid(),
						      &cluster_cond))) {
		error("Problem talking to database");
		goto end_it;
	}
	itr = list_iterator_create(temp_list);
	if (!cluster_names || all_clusters) {
		while ((cluster_rec = list_next(itr))) {
			if (slurmdb_setup_cluster_rec(cluster_rec) !=
			    SLURM_SUCCESS) {
				list_delete_item(itr);
			}
		}
	} else {
		itr2 = list_iterator_create(cluster_cond.cluster_list);
		while ((cluster_name = list_next(itr2))) {
			while ((cluster_rec = list_next(itr))) {
				if (!xstrcmp(cluster_name, cluster_rec->name))
					break;
			}
			if (!cluster_rec) {
				error("No cluster '%s' known by database.",
				      cluster_name);
				goto next;
			}

			if (slurmdb_setup_cluster_rec(cluster_rec) !=
			    SLURM_SUCCESS) {
				list_delete_item(itr);
			}
		next:
			list_iterator_reset(itr);
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);

end_it:
	FREE_NULL_LIST(cluster_cond.cluster_list);
	acct_storage_g_close_connection(&db_conn);

	if (temp_list && !list_count(temp_list)) {
		FREE_NULL_LIST(temp_list);
	}

	return temp_list;
}

extern void slurmdb_init_assoc_rec(slurmdb_assoc_rec_t *assoc,
					 bool free_it)
{
	if (!assoc)
		return;

	if (free_it)
		slurmdb_free_assoc_rec_members(assoc);
	memset(assoc, 0, sizeof(slurmdb_assoc_rec_t));

	assoc->def_qos_id = NO_VAL;
	assoc->is_def = NO_VAL16;

	/* assoc->grp_tres_mins = NULL; */
	/* assoc->grp_tres_run_mins = NULL; */
	/* assoc->grp_tres = NULL; */
	assoc->grp_jobs = NO_VAL;
	assoc->grp_jobs_accrue = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	assoc->lft = NO_VAL;
	assoc->rgt = NO_VAL;
	/* assoc->level_shares = NO_VAL; */

	/* assoc->max_tres_mins_pj = NULL; */
	/* assoc->max_tres_run_mins = NULL; */
	/* assoc->max_tres_pj = NULL; */
	assoc->max_jobs = NO_VAL;
	assoc->max_jobs_accrue = NO_VAL;
	assoc->min_prio_thresh = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;

	assoc->priority = NO_VAL;

	/* assoc->shares_norm = NO_VAL64; */
	assoc->shares_raw = NO_VAL;

	/* assoc->usage_efctv = 0; */
	/* assoc->usage_norm = (long double)NO_VAL; */
	/* assoc->usage_raw = 0; */
}

extern void slurmdb_init_clus_res_rec(slurmdb_clus_res_rec_t *clus_res,
				      bool free_it)
{
	if (!clus_res)
		return;

	if (free_it)
		_free_clus_res_rec_members(clus_res);
	memset(clus_res, 0, sizeof(slurmdb_clus_res_rec_t));
	clus_res->allowed = NO_VAL;
}

extern void slurmdb_init_cluster_rec(slurmdb_cluster_rec_t *cluster,
				     bool free_it)
{
	if (!cluster)
		return;

	if (free_it)
		_free_cluster_rec_members(cluster);
	memset(cluster, 0, sizeof(slurmdb_cluster_rec_t));
	cluster->flags      = NO_VAL;
	cluster->fed.state  = NO_VAL;
	slurm_mutex_init(&cluster->lock);
}

extern void slurmdb_init_federation_rec(slurmdb_federation_rec_t *federation,
					bool free_it)
{
	if (!federation)
		return;

	if (free_it)
		_free_federation_rec_members(federation);
	memset(federation, 0, sizeof(slurmdb_federation_rec_t));
	federation->flags = FEDERATION_FLAG_NOTSET;
}

extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos, bool free_it,
				 uint32_t init_val)
{
	if (!qos)
		return;

	if (free_it)
		slurmdb_free_qos_rec_members(qos);
	memset(qos, 0, sizeof(slurmdb_qos_rec_t));

	qos->flags = QOS_FLAG_NOTSET;

	qos->grace_time = init_val;
	qos->preempt_mode = (uint16_t)init_val;
	qos->preempt_exempt_time = init_val;
	qos->priority = init_val;

	/* qos->grp_tres_mins = NULL; */
	/* qos->grp_tres_run_mins = NULL; */
	/* qos->grp_tres = NULL; */
	qos->grp_jobs = init_val;
	qos->grp_jobs_accrue = init_val;
	qos->grp_submit_jobs = init_val;
	qos->grp_wall = init_val;

	qos->limit_factor = (double)init_val;

	/* qos->max_tres_mins_pj = NULL; */
	/* qos->max_tres_run_mins_pa = NULL; */
	/* qos->max_tres_run_mins_pu = NULL; */
	/* qos->max_tres_pa = NULL; */
	/* qos->max_tres_pj = NULL; */
	/* qos->max_tres_pu = NULL; */
	qos->max_jobs_pa = init_val;
	qos->max_jobs_pu = init_val;
	qos->max_jobs_accrue_pa = init_val;
	qos->max_jobs_accrue_pu = init_val;
	qos->min_prio_thresh = init_val;
	qos->max_submit_jobs_pa = init_val;
	qos->max_submit_jobs_pu = init_val;
	qos->max_wall_pj = init_val;

	/* qos->min_tres_pj = NULL; */

	qos->usage_factor = (double)init_val;
	qos->usage_thres = (double)init_val;
}

extern void slurmdb_init_res_rec(slurmdb_res_rec_t *res,
				 bool free_it)
{
	if (!res)
		return;

	if (free_it)
		_free_res_rec_members(res);
	memset(res, 0, sizeof(slurmdb_res_rec_t));
	res->count = NO_VAL;
	res->flags = SLURMDB_RES_FLAG_NOTSET;
	res->id = NO_VAL;
	res->last_consumed = NO_VAL;
	res->allocated = NO_VAL;
	res->type = SLURMDB_RESOURCE_NOTSET;
}

extern void slurmdb_init_wckey_rec(slurmdb_wckey_rec_t *wckey, bool free_it)
{
	if (!wckey)
		return;

	if (free_it)
		_free_wckey_rec_members(wckey);
	memset(wckey, 0, sizeof(slurmdb_wckey_rec_t));
	wckey->is_def = NO_VAL16;
}

extern void slurmdb_init_tres_cond(slurmdb_tres_cond_t *tres,
				    bool free_it)
{
	if (!tres)
		return;

	if (free_it)
		_free_tres_cond_members(tres);
	memset(tres, 0, sizeof(slurmdb_tres_cond_t));
	tres->count = NO_VAL;
}

extern void slurmdb_init_cluster_cond(slurmdb_cluster_cond_t *cluster,
				      bool free_it)
{
	if (!cluster)
		return;

	if (free_it)
		_free_cluster_cond_members(cluster);
	memset(cluster, 0, sizeof(slurmdb_cluster_cond_t));
	cluster->flags = NO_VAL;
}

extern void slurmdb_init_federation_cond(slurmdb_federation_cond_t *federation,
					 bool free_it)
{
	if (!federation)
		return;

	if (free_it)
		_free_federation_cond_members(federation);
	memset(federation, 0, sizeof(slurmdb_federation_cond_t));
}

extern void slurmdb_init_res_cond(slurmdb_res_cond_t *res,
				  bool free_it)
{
	if (!res)
		return;

	if (free_it)
		_free_res_cond_members(res);
	memset(res, 0, sizeof(slurmdb_res_cond_t));
	res->flags = SLURMDB_RES_FLAG_NOTSET;
}

extern char *slurmdb_qos_str(List qos_list, uint32_t level)
{
	slurmdb_qos_rec_t *qos = NULL;

	if (!qos_list) {
		error("We need a qos list to translate");
		return NULL;
	} else if (!level) {
		debug2("no level");
		return "";
	}

	qos = list_find_first(qos_list, slurmdb_find_qos_in_list, &level);
	if (qos)
		return qos->name;
	else
		return NULL;
}

extern uint32_t str_2_slurmdb_qos(List qos_list, char *level)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	char *working_level = NULL;

	if (!qos_list) {
		error("We need a qos list to translate");
		return NO_VAL;
	} else if (!level) {
		debug2("no level");
		return 0;
	}
	if (level[0] == '+' || level[0] == '-')
		working_level = level+1;
	else
		working_level = level;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (!xstrcasecmp(working_level, qos->name))
			break;
	}
	list_iterator_destroy(itr);
	if (qos)
		return qos->id;
	else
		return NO_VAL;
}

extern char *slurmdb_federation_flags_str(uint32_t flags)
{
	char *federation_flags = NULL;

	if (flags & FEDERATION_FLAG_NOTSET)
		return xstrdup("NotSet");

#if 0
	/* Remove when there are actually flags since the flags will be
	 * comma-separated. */
	if (federation_flags)
		federation_flags[strlen(federation_flags)-1] = '\0';
#endif

	return federation_flags;
}

static uint32_t _str_2_federation_flags(char *flags)
{
	return 0;
}

extern uint32_t str_2_federation_flags(char *flags, int option)
{
	uint32_t federation_flags = 0;
	char *token, *my_flags, *last = NULL;

	if (!flags) {
		error("We need a federation flags string to translate");
		return FEDERATION_FLAG_NOTSET;
	} else if (atoi(flags) == -1) {
		/* clear them all */
		federation_flags = INFINITE;
		federation_flags &= (~FEDERATION_FLAG_NOTSET &
				     ~FEDERATION_FLAG_ADD);
		return federation_flags;
	}

	my_flags = xstrdup(flags);
	token = strtok_r(my_flags, ",", &last);
	while (token) {
		federation_flags |= _str_2_federation_flags(token);
		token = strtok_r(NULL, ",", &last);
	}
	xfree(my_flags);

	if (!federation_flags)
		federation_flags = FEDERATION_FLAG_NOTSET;
	else if (option == '+')
		federation_flags |= FEDERATION_FLAG_ADD;
	else if (option == '-')
		federation_flags |= FEDERATION_FLAG_REMOVE;

	return federation_flags;
}

extern char *slurmdb_cluster_fed_states_str(uint32_t state)
{
	int  base        = (state & CLUSTER_FED_STATE_BASE);
	bool drain_flag  = (state & CLUSTER_FED_STATE_DRAIN);
	bool remove_flag = (state & CLUSTER_FED_STATE_REMOVE);

	if (base == CLUSTER_FED_STATE_ACTIVE) {
		if (remove_flag && drain_flag)
			return "DRAIN+REMOVE";
		else if (drain_flag)
			return "DRAIN";
		else
			return "ACTIVE";
	} else if (base == CLUSTER_FED_STATE_INACTIVE) {
		if (remove_flag && drain_flag)
			return "DRAINED+REMOVE";
		else if (drain_flag)
			return "DRAINED";
		else
			return "INACTIVE";
	} else if (base == CLUSTER_FED_STATE_NA)
		return "NA";

	return "?";
}

extern uint32_t str_2_cluster_fed_states(char *state)
{
	uint32_t fed_state = 0;

	if (!state) {
		error("We need a cluster federation state string to translate");
		return SLURM_ERROR;
	}

	if (!xstrncasecmp(state, "Active", strlen(state)))
		fed_state = CLUSTER_FED_STATE_ACTIVE;
	else if (!xstrncasecmp(state, "Inactive", strlen(state)))
		fed_state = CLUSTER_FED_STATE_INACTIVE;
	else if (!xstrncasecmp(state, "DRAIN", strlen(state))) {
		fed_state = CLUSTER_FED_STATE_ACTIVE;
		fed_state |= CLUSTER_FED_STATE_DRAIN;
	} else if (!xstrncasecmp(state, "DRAIN+REMOVE", strlen(state))) {
		fed_state = CLUSTER_FED_STATE_ACTIVE;
		fed_state |= (CLUSTER_FED_STATE_DRAIN |
			      CLUSTER_FED_STATE_REMOVE);
	}

	return fed_state;
}

extern char *slurmdb_job_flags_str(uint32_t flags)
{
	char *job_flags = NULL;

	if (flags == SLURMDB_JOB_FLAG_NONE)
		return xstrdup("None");

	if (flags & SLURMDB_JOB_FLAG_NOTSET)
		xstrcat(job_flags, "SchedNotSet");
	else if (flags & SLURMDB_JOB_FLAG_SUBMIT)
		xstrcat(job_flags, "SchedSubmit");
	else if (flags & SLURMDB_JOB_FLAG_SCHED)
		xstrcat(job_flags, "SchedMain");
	else if (flags & SLURMDB_JOB_FLAG_BACKFILL)
		xstrcat(job_flags, "SchedBackfill");

	if (flags & SLURMDB_JOB_FLAG_START_R)
		xstrfmtcat(job_flags, "%sStartRecieved", job_flags ? "," : "");

	return job_flags;
}

extern uint32_t str_2_job_flags(char *flags)
{
	uint32_t job_flags = 0;
	char *token, *my_flags, *last = NULL;

	if (!flags) {
		error("We need a server job flags string to translate");
		return SLURMDB_JOB_FLAG_NONE;
	}

	my_flags = xstrdup(flags);
	token = strtok_r(my_flags, ",", &last);
	while (token) {
		job_flags |= _str_2_job_flags(token);
		if (job_flags & SLURMDB_JOB_FLAG_NOTSET) {
			error("%s: Invalid job flag %s", __func__, token);
			xfree(my_flags);
			return SLURMDB_JOB_FLAG_NOTSET;
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(my_flags);

	return job_flags;
}

extern char *slurmdb_qos_flags_str(uint32_t flags)
{
	char *qos_flags = NULL;

	if (flags & QOS_FLAG_NOTSET)
		return xstrdup("NotSet");

	if (flags & QOS_FLAG_ADD)
		xstrcat(qos_flags, "Add,");
	if (flags & QOS_FLAG_REMOVE)
		xstrcat(qos_flags, "Remove,");
	if (flags & QOS_FLAG_DENY_LIMIT)
		xstrcat(qos_flags, "DenyOnLimit,");
	if (flags & QOS_FLAG_ENFORCE_USAGE_THRES)
		xstrcat(qos_flags, "EnforceUsageThreshold,");
	if (flags & QOS_FLAG_NO_RESERVE)
		xstrcat(qos_flags, "NoReserve,");
	if (flags & QOS_FLAG_PART_MAX_NODE)
		xstrcat(qos_flags, "PartitionMaxNodes,");
	if (flags & QOS_FLAG_PART_MIN_NODE)
		xstrcat(qos_flags, "PartitionMinNodes,");
	if (flags & QOS_FLAG_OVER_PART_QOS)
		xstrcat(qos_flags, "OverPartQOS,");
	if (flags & QOS_FLAG_PART_TIME_LIMIT)
		xstrcat(qos_flags, "PartitionTimeLimit,");
	if (flags & QOS_FLAG_REQ_RESV)
		xstrcat(qos_flags, "RequiresReservation,");
	if (flags & QOS_FLAG_NO_DECAY)
		xstrcat(qos_flags, "NoDecay,");
	if (flags & QOS_FLAG_USAGE_FACTOR_SAFE)
		xstrcat(qos_flags, "UsageFactorSafe,");

	if (qos_flags)
		qos_flags[strlen(qos_flags)-1] = '\0';

	return qos_flags;
}

extern uint32_t str_2_qos_flags(char *flags, int option)
{
	uint32_t qos_flags = 0;
	char *token, *my_flags, *last = NULL;

	if (!flags) {
		error("We need a qos flags string to translate");
		return QOS_FLAG_NOTSET;
	} else if (atoi(flags) == -1) {
		/* clear them all */
		qos_flags = INFINITE;
		qos_flags &= (~QOS_FLAG_NOTSET &
			      ~QOS_FLAG_ADD);
		return qos_flags;
	}

	my_flags = xstrdup(flags);
	token = strtok_r(my_flags, ",", &last);
	while (token) {
		qos_flags |= _str_2_qos_flags(token);
		token = strtok_r(NULL, ",", &last);
	}
	xfree(my_flags);

	if (!qos_flags)
		qos_flags = QOS_FLAG_NOTSET;
	else if (option == '+')
		qos_flags |= QOS_FLAG_ADD;
	else if (option == '-')
		qos_flags |= QOS_FLAG_REMOVE;


	return qos_flags;
}

extern char *slurmdb_res_flags_str(uint32_t flags)
{
	char *res_flags = NULL;

	if (flags & SLURMDB_RES_FLAG_NOTSET)
		return xstrdup("NotSet");

	if (flags & SLURMDB_RES_FLAG_ADD)
		xstrcat(res_flags, "Add,");
	if (flags & SLURMDB_RES_FLAG_REMOVE)
		xstrcat(res_flags, "Remove,");
	if (flags & SLURMDB_RES_FLAG_ABSOLUTE)
		xstrcat(res_flags, "Absolute,");

	if (res_flags)
		res_flags[strlen(res_flags)-1] = '\0';

	return res_flags;
}

extern uint32_t str_2_res_flags(char *flags, int option)
{
	uint32_t res_flags = 0;
	char *token, *my_flags, *last = NULL;

	if (!flags) {
		error("We need a server resource flags string to translate");
		return SLURMDB_RES_FLAG_NOTSET;
	} else if (atoi(flags) == -1) {
		/* clear them all */
		res_flags = INFINITE;
		res_flags &= (SLURMDB_RES_FLAG_NOTSET &
			      ~SLURMDB_RES_FLAG_ADD);
		return res_flags;
	}

	my_flags = xstrdup(flags);
	token = strtok_r(my_flags, ",", &last);
	while (token) {
		res_flags |= _str_2_res_flags(token);
		token = strtok_r(NULL, ",", &last);
	}
	xfree(my_flags);

	if (!res_flags)
		res_flags = SLURMDB_RES_FLAG_NOTSET;
	else if (option == '+')
		res_flags |= SLURMDB_RES_FLAG_ADD;
	else if (option == '-')
		res_flags |= SLURMDB_RES_FLAG_REMOVE;


	return res_flags;
}

extern char *slurmdb_res_type_str(slurmdb_resource_type_t type)
{
	switch (type) {
	case SLURMDB_RESOURCE_NOTSET:
		return "Not Set";
		break;
	case SLURMDB_RESOURCE_LICENSE:
		return "License";
		break;
	}
	return "Unknown";
}

extern char *slurmdb_admin_level_str(slurmdb_admin_level_t level)
{
	switch(level) {
	case SLURMDB_ADMIN_NOTSET:
		return "Not Set";
		break;
	case SLURMDB_ADMIN_NONE:
		return "None";
		break;
	case SLURMDB_ADMIN_OPERATOR:
		return "Operator";
		break;
	case SLURMDB_ADMIN_SUPER_USER:
		return "Administrator";
		break;
	}
	return "Unknown";
}

extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level)
{
	if (!level) {
		return SLURMDB_ADMIN_NOTSET;
	} else if (!xstrncasecmp(level, "None", 1)) {
		return SLURMDB_ADMIN_NONE;
	} else if (!xstrncasecmp(level, "Operator", 1)) {
		return SLURMDB_ADMIN_OPERATOR;
	} else if (!xstrncasecmp(level, "SuperUser", 1)
		   || !xstrncasecmp(level, "Admin", 1)) {
		return SLURMDB_ADMIN_SUPER_USER;
	} else {
		return SLURMDB_ADMIN_NOTSET;
	}
}

/* This reorders the list into a alphabetical hierarchy returned in a
 * separate list. The original list is not affected. */
extern List slurmdb_get_hierarchical_sorted_assoc_list(
	List assoc_list, bool use_lft)
{
	List slurmdb_hierarchical_rec_list;
	List ret_list = list_create(NULL);

	if (use_lft)
		slurmdb_hierarchical_rec_list =
			slurmdb_get_acct_hierarchical_rec_list(assoc_list);
	else
		slurmdb_hierarchical_rec_list =
			slurmdb_get_acct_hierarchical_rec_list_no_lft(
				assoc_list);

	_append_hierarchical_children_ret_list(ret_list,
					       slurmdb_hierarchical_rec_list);
	FREE_NULL_LIST(slurmdb_hierarchical_rec_list);

	return ret_list;
}

/* This reorders the list into a alphabetical hierarchy. */
extern void slurmdb_sort_hierarchical_assoc_list(
	List assoc_list, bool use_lft)
{
	List slurmdb_hierarchical_rec_list;

	if (use_lft)
		slurmdb_hierarchical_rec_list =
			slurmdb_get_acct_hierarchical_rec_list(assoc_list);
	else
		slurmdb_hierarchical_rec_list =
			slurmdb_get_acct_hierarchical_rec_list_no_lft(
				assoc_list);

	/* Clear all the pointers out of the list without freeing the
	   memory since we will just add them back in later.
	*/
	while (list_pop(assoc_list)) {
	}

	_append_hierarchical_children_ret_list(assoc_list,
					       slurmdb_hierarchical_rec_list);
	FREE_NULL_LIST(slurmdb_hierarchical_rec_list);
}

/* Build a hierarchical list using only association id's along with
 * parent id's.  This method is slower than the non _no_lft function
 * below, but it is needed if the lft and rgt's ever get messed up.
 * Each association in here will result in a 0 rgt afterwards.
 */
extern List slurmdb_get_acct_hierarchical_rec_list_no_lft(List assoc_list)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	xhash_t *all_parents = xhash_init(_arch_hash_rec_id, NULL);
	List arch_rec_list = list_create(slurmdb_destroy_hierarchical_rec);
	ListIterator itr;
	/* DEF_TIMERS; */
	/* START_TIMER; */

	itr = list_iterator_create(assoc_list);
	while ((assoc = list_next(itr))) {
		if (assoc->rgt == 0) // already processed
			continue;

		_find_create_parent(assoc, assoc_list,
				    arch_rec_list, all_parents);
	}
	list_iterator_destroy(itr);
	/* END_TIMER; */
	/* info("took %s", TIME_STR); */
	xhash_free(all_parents);
//	info("got %d", list_count(arch_rec_list));
	_sort_slurmdb_hierarchical_rec_list(arch_rec_list);

	return arch_rec_list;
}

extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list)
{
	slurmdb_hierarchical_rec_t *par_arch_rec = NULL;
	slurmdb_hierarchical_rec_t *last_acct_parent = NULL;
	slurmdb_hierarchical_rec_t *last_parent = NULL;
	slurmdb_hierarchical_rec_t *arch_rec = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;

	xhash_t *all_parents = xhash_init(_arch_hash_rec_id, NULL);
	List arch_rec_list =
		list_create(slurmdb_destroy_hierarchical_rec);
	ListIterator itr;

	/*
	 * The list should already be sorted by lfts, do it anyway
	 * just to make sure it is correct.
	 */
	list_sort(assoc_list, (ListCmpF)_sort_assoc_by_lft_dec);
	itr = list_iterator_create(assoc_list);

	while((assoc = list_next(itr))) {
		arch_rec = xmalloc(sizeof(slurmdb_hierarchical_rec_t));
		arch_rec->children =
			list_create(slurmdb_destroy_hierarchical_rec);
		arch_rec->assoc = assoc;

		/*
		 * To speed things up we are first looking if we have
		 * a parent_id to look for.  If that doesn't work see
		 * if the last parent we had was what we are looking
		 * for.  Then if that isn't panning out look at the
		 * last account parent.  If still we don't have it we
		 * will look for it in the list.  If it isn't there we
		 * will just add it to the parent and call it good
		 */
		if (!assoc->parent_id) {
			arch_rec->sort_name = assoc->cluster;
			list_append(arch_rec_list, arch_rec);
			xhash_add(all_parents, arch_rec);
			continue;
		}

		if (assoc->user)
			arch_rec->sort_name = assoc->user;
		else
			arch_rec->sort_name = assoc->acct;

		if (last_parent &&
		    (assoc->parent_id == last_parent->assoc->id) &&
		    !xstrcmp(assoc->cluster, last_parent->assoc->cluster)) {
			par_arch_rec = last_parent;
		} else if (last_acct_parent &&
			   (assoc->parent_id == last_acct_parent->assoc->id) &&
			   !xstrcmp(assoc->cluster,
				    last_acct_parent->assoc->cluster)) {
			par_arch_rec = last_acct_parent;
		} else {
			char *key = _create_hash_rec_id(assoc, true);
			par_arch_rec = xhash_get(all_parents, key, strlen(key));
			xfree(key);
			if (par_arch_rec) {
				last_parent = par_arch_rec;
				if (!assoc->user)
					last_acct_parent = par_arch_rec;
			}
		}

		if (!par_arch_rec) {
			list_append(arch_rec_list, arch_rec);
			last_parent = last_acct_parent = arch_rec;
		} else
			list_append(par_arch_rec->children, arch_rec);

		if (!assoc->user) /* Users are never parent assocs */
			xhash_add(all_parents, arch_rec);
	}
	list_iterator_destroy(itr);

	xhash_free(all_parents);
/*	info("got %d", list_count(arch_rec_list)); */
	_sort_slurmdb_hierarchical_rec_list(arch_rec_list);

	return arch_rec_list;
}

/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list)
{
	ListIterator itr = NULL;
	slurmdb_print_tree_t *slurmdb_print_tree = NULL;
	slurmdb_print_tree_t *par_slurmdb_print_tree = NULL;

	if (!tree_list)
		return NULL;

	itr = list_iterator_create(tree_list);
	while((slurmdb_print_tree = list_next(itr))) {
		/* we don't care about users in this list.  They are
		   only there so we don't leak memory */
		if (slurmdb_print_tree->user)
			continue;

		if (!xstrcmp(name, slurmdb_print_tree->name))
			break;
		else if (parent && !xstrcmp(parent, slurmdb_print_tree->name))
			par_slurmdb_print_tree = slurmdb_print_tree;

	}
	list_iterator_destroy(itr);

	if (parent && slurmdb_print_tree)
		return slurmdb_print_tree->print_name;

	slurmdb_print_tree = xmalloc(sizeof(slurmdb_print_tree_t));
	slurmdb_print_tree->name = xstrdup(name);
	if (par_slurmdb_print_tree)
		slurmdb_print_tree->spaces =
			xstrdup_printf(" %s", par_slurmdb_print_tree->spaces);
	else
		slurmdb_print_tree->spaces = xstrdup("");

	/* user account */
	if (name[0] == '|') {
		slurmdb_print_tree->print_name = xstrdup_printf(
			"%s%s", slurmdb_print_tree->spaces, parent);
		slurmdb_print_tree->user = 1;
	} else
		slurmdb_print_tree->print_name = xstrdup_printf(
			"%s%s", slurmdb_print_tree->spaces, name);

	list_append(tree_list, slurmdb_print_tree);

	return slurmdb_print_tree->print_name;
}

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	char *temp_char = NULL;

	xassert(valid_qos);

	if (!qos_list)
		return SLURM_ERROR;

	itr = list_iterator_create(qos_list);
	while((temp_char = list_next(itr)))
		_set_qos_bit_from_string(valid_qos, temp_char);
	list_iterator_destroy(itr);

	return rc;
}

extern const char *rollup_interval_to_string(int interval)
{
	switch (interval) {
	case DBD_ROLLUP_HOUR:
		return "Hour";
	case DBD_ROLLUP_DAY:
		return "Day";
	case DBD_ROLLUP_MONTH:
		return "Month";
	default:
		return "Unknown";
	}
}

extern int set_qos_bitstr_from_string(bitstr_t *valid_qos, char *names)
{
	int rc = SLURM_SUCCESS;
	int i=0, start=0;
	char *name = NULL;

	xassert(valid_qos);

	if (!names)
		return SLURM_ERROR;

	/* skip the first comma if it is one */
	if (names[i] == ',')
		i++;

	start = i;
	while (names[i]) {
		//info("got %d - %d = %d", i, start, i-start);
		if (names[i] == ',') {
			/* If there is a comma at the end just
			   ignore it */
			if (!names[i+1])
				break;

			name = xstrndup(names+start, (i-start));
			/* info("got %s %d", name, i-start); */
			_set_qos_bit_from_string(valid_qos, name);
			xfree(name);
			i++;
			start = i;
		}
		i++;
	}

	name = xstrndup(names+start, (i-start));
	/* info("got %s %d", name, i-start); */
	_set_qos_bit_from_string(valid_qos, name);
	xfree(name);

	return rc;
}

extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	int i = 0;

	if (!qos_list || !list_count(qos_list)
	    || !valid_qos || (bit_ffs(valid_qos) == -1))
		return xstrdup("");

	temp_list = list_create(NULL);

	for(i=0; i<bit_size(valid_qos); i++) {
		if (!bit_test(valid_qos, i))
			continue;
		if ((temp_char = slurmdb_qos_str(qos_list, i)))
			list_append(temp_list, temp_char);
	}
	print_this = slurm_char_list_to_xstr(temp_list);
	FREE_NULL_LIST(temp_list);

	if (!print_this)
		return xstrdup("");

	return print_this;
}

extern List get_qos_name_list(List qos_list, List num_qos_list)
{
	List temp_list;
	char *temp_char;
	ListIterator itr;
	int option;

	if (!qos_list || !list_count(qos_list)
	    || !num_qos_list || !list_count(num_qos_list))
		return NULL;

	temp_list = list_create(xfree_ptr);

	itr = list_iterator_create(num_qos_list);
	while((temp_char = list_next(itr))) {
		option = 0;
		if (temp_char[0] == '+' || temp_char[0] == '-') {
			option = temp_char[0];
			temp_char++;
		}
		temp_char = slurmdb_qos_str(qos_list, atoi(temp_char));
		if (temp_char) {
			if (option)
				list_append(temp_list, xstrdup_printf(
						    "%c%s", option, temp_char));
			else
				list_append(temp_list, xstrdup(temp_char));
		}
	}
	list_iterator_destroy(itr);
	return temp_list;
}

extern char *get_qos_complete_str(List qos_list, List num_qos_list)
{
	List temp_list;
	char *print_this;

	if (!qos_list || !list_count(qos_list) || !num_qos_list ||
	    !list_count(num_qos_list))
		return xstrdup("");

	temp_list = get_qos_name_list(qos_list, num_qos_list);

	print_this = slurm_char_list_to_xstr(temp_list);
	FREE_NULL_LIST(temp_list);

	if (!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_classification_str(uint16_t class)
{
	bool classified = class & SLURMDB_CLASSIFIED_FLAG;
	slurmdb_classification_type_t type = class & SLURMDB_CLASS_BASE;

	switch(type) {
	case SLURMDB_CLASS_NONE:
		return NULL;
		break;
	case SLURMDB_CLASS_CAPACITY:
		if (classified)
			return "*Capacity";
		else
			return "Capacity";
		break;
	case SLURMDB_CLASS_CAPABILITY:
		if (classified)
			return "*Capability";
		else
			return "Capability";
		break;
	case SLURMDB_CLASS_CAPAPACITY:
		if (classified)
			return "*Capapacity";
		else
			return "Capapacity";
		break;
	default:
		if (classified)
			return "*Unknown";
		else
			return "Unknown";
		break;
	}
}

extern uint16_t str_2_classification(char *class)
{
	uint16_t type = 0;
	if (!class)
		return type;

	if (xstrcasestr(class, "capac"))
		type = SLURMDB_CLASS_CAPACITY;
	else if (xstrcasestr(class, "capab"))
		type = SLURMDB_CLASS_CAPABILITY;
	else if (xstrcasestr(class, "capap"))
		type = SLURMDB_CLASS_CAPAPACITY;

	if (xstrcasestr(class, "*"))
		type |= SLURMDB_CLASSIFIED_FLAG;
	else if (xstrcasestr(class, "class"))
		type |= SLURMDB_CLASSIFIED_FLAG;

	return type;
}

extern char *slurmdb_problem_str_get(uint16_t problem)
{
	slurmdb_problem_type_t type = problem;

	switch(type) {
	case SLURMDB_PROBLEM_NOT_SET:
		return NULL;
		break;
	case SLURMDB_PROBLEM_ACCT_NO_ASSOC:
		return "Account has no Associations";
		break;
	case SLURMDB_PROBLEM_ACCT_NO_USERS:
		return "Account has no users";
		break;
	case SLURMDB_PROBLEM_USER_NO_ASSOC:
		return "User has no Associations";
		break;
	case SLURMDB_PROBLEM_USER_NO_UID:
		return "User does not have a uid";
		break;
	default:
		return "Unknown";
		break;
	}
}

extern uint16_t str_2_slurmdb_problem(char *problem)
{
	uint16_t type = 0;

	if (!problem)
		return type;

	if (xstrcasestr(problem, "account no assocs"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if (xstrcasestr(problem, "account no users"))
		type = SLURMDB_PROBLEM_ACCT_NO_USERS;
	else if (xstrcasestr(problem, "user no assocs"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if (xstrcasestr(problem, "user no uid"))
		type = SLURMDB_PROBLEM_USER_NO_UID;

	return type;
}

extern void log_assoc_rec(slurmdb_assoc_rec_t *assoc_ptr,
			  List qos_list)
{
	xassert(assoc_ptr);

	if (get_log_level() < LOG_LEVEL_DEBUG2)
		return;

	debug2("association rec id : %u", assoc_ptr->id);
	debug2("  acct             : %s", assoc_ptr->acct);
	debug2("  cluster          : %s", assoc_ptr->cluster);
	debug2("  comment          : %s", assoc_ptr->comment);

	if (assoc_ptr->shares_raw == INFINITE)
		debug2("  RawShares        : NONE");
	else if (assoc_ptr->shares_raw != NO_VAL)
		debug2("  RawShares        : %u", assoc_ptr->shares_raw);

	if (assoc_ptr->def_qos_id)
		debug2("  Default QOS      : %s",
		       slurmdb_qos_str(qos_list, assoc_ptr->def_qos_id));
	else
		debug2("  Default QOS      : NONE");

	debug2("  GrpTRESMins      : %s",
	       assoc_ptr->grp_tres_mins ?
	       assoc_ptr->grp_tres_mins : "NONE");
	debug2("  GrpTRESRunMins   : %s",
	       assoc_ptr->grp_tres_run_mins ?
	       assoc_ptr->grp_tres_run_mins : "NONE");
	debug2("  GrpTRES          : %s",
	       assoc_ptr->grp_tres ?
	       assoc_ptr->grp_tres : "NONE");

	if (assoc_ptr->grp_jobs == INFINITE)
		debug2("  GrpJobs          : NONE");
	else if (assoc_ptr->grp_jobs != NO_VAL)
		debug2("  GrpJobs          : %u", assoc_ptr->grp_jobs);

	if (assoc_ptr->grp_jobs_accrue == INFINITE)
		debug2("  GrpJobsAccrue    : NONE");
	else if (assoc_ptr->grp_jobs_accrue != NO_VAL)
		debug2("  GrpJobsAccrue    : %u", assoc_ptr->grp_jobs_accrue);

	if (assoc_ptr->grp_submit_jobs == INFINITE)
		debug2("  GrpSubmitJobs    : NONE");
	else if (assoc_ptr->grp_submit_jobs != NO_VAL)
		debug2("  GrpSubmitJobs    : %u", assoc_ptr->grp_submit_jobs);

	if (assoc_ptr->grp_wall == INFINITE)
		debug2("  GrpWall          : NONE");
	else if (assoc_ptr->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->grp_wall,
			      time_buf, sizeof(time_buf));
		debug2("  GrpWall          : %s", time_buf);
	}

	debug2("  MaxTRESMins      : %s",
	       assoc_ptr->max_tres_mins_pj ?
	       assoc_ptr->max_tres_mins_pj : "NONE");
	debug2("  MaxTRESRunMins   : %s",
	       assoc_ptr->max_tres_run_mins ?
	       assoc_ptr->max_tres_run_mins : "NONE");
	debug2("  MaxTRESPerJob    : %s",
	       assoc_ptr->max_tres_pj ?
	       assoc_ptr->max_tres_pj : "NONE");
	debug2("  MaxTRESPerNode   : %s",
	       assoc_ptr->max_tres_pn ?
	       assoc_ptr->max_tres_pn : "NONE");

	if (assoc_ptr->max_jobs == INFINITE)
		debug2("  MaxJobs          : NONE");
	else if (assoc_ptr->max_jobs != NO_VAL)
		debug2("  MaxJobs          : %u", assoc_ptr->max_jobs);

	if (assoc_ptr->max_jobs_accrue == INFINITE)
		debug2("  MaxJobsAccrue    : NONE");
	else if (assoc_ptr->max_jobs_accrue != NO_VAL)
		debug2("  MaxJobsAccrue    : %u", assoc_ptr->max_jobs_accrue);

	if (assoc_ptr->min_prio_thresh == INFINITE)
		debug2("  MinPrioThresh    : NONE");
	else if (assoc_ptr->min_prio_thresh != NO_VAL)
		debug2("  MinPrioThresh    : %u", assoc_ptr->min_prio_thresh);

	if (assoc_ptr->max_submit_jobs == INFINITE)
		debug2("  MaxSubmitJobs    : NONE");
	else if (assoc_ptr->max_submit_jobs != NO_VAL)
		debug2("  MaxSubmitJobs    : %u", assoc_ptr->max_submit_jobs);

	if (assoc_ptr->max_wall_pj == INFINITE)
		debug2("  MaxWall          : NONE");
	else if (assoc_ptr->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->max_wall_pj,
			      time_buf, sizeof(time_buf));
		debug2("  MaxWall          : %s", time_buf);
	}

	if (assoc_ptr->qos_list) {
		char *temp_char = get_qos_complete_str(qos_list,
						       assoc_ptr->qos_list);
		if (temp_char) {
			debug2("  Qos              : %s", temp_char);
			xfree(temp_char);
			if (assoc_ptr->usage && assoc_ptr->usage->valid_qos) {
				temp_char = get_qos_complete_str_bitstr(
					qos_list, assoc_ptr->usage->valid_qos);
				debug3("  Valid Qos        : %s", temp_char);
				xfree(temp_char);
			}
		}
	} else {
		debug2("  Qos              : %s", "Normal");
	}

	if (assoc_ptr->parent_acct)
		debug2("  ParentAccount    : %s", assoc_ptr->parent_acct);
	if (assoc_ptr->partition)
		debug2("  Partition        : %s", assoc_ptr->partition);
	if (assoc_ptr->user)
		debug2("  User             : %s(%u)",
		       assoc_ptr->user, assoc_ptr->uid);

	if (assoc_ptr->usage) {
		if (!fuzzy_equal(assoc_ptr->usage->shares_norm, NO_VAL))
			debug2("  NormalizedShares : %f",
			       assoc_ptr->usage->shares_norm);

		if (assoc_ptr->usage->level_shares != NO_VAL)
			debug2("  LevelShares      : %u",
			       assoc_ptr->usage->level_shares);


		debug2("  UsedJobs         : %u", assoc_ptr->usage->used_jobs);
		debug2("  RawUsage         : %Lf", assoc_ptr->usage->usage_raw);
	}
}

extern int slurmdb_report_set_start_end_time(time_t *start, time_t *end)
{
	time_t my_time = time(NULL);
	time_t temp_time;
	struct tm start_tm;
	struct tm end_tm;
	int sent_start = (*start), sent_end = (*end);

//	info("now got %d and %d sent", (*start), (*end));
	/* Default is going to be the last day */
	if (!sent_end) {
		if (!localtime_r(&my_time, &end_tm)) {
			error("Couldn't get localtime from end %ld",
			      (long)my_time);
			return SLURM_ERROR;
		}
		end_tm.tm_hour = 0;
		//(*end) = slurm_mktime(&end_tm);
	} else {
		temp_time = sent_end;
		if (!localtime_r(&temp_time, &end_tm)) {
			error("Couldn't get localtime from user end %ld",
			      (long)my_time);
			return SLURM_ERROR;
		}
		if (end_tm.tm_sec >= 30)
			end_tm.tm_min++;
		if (end_tm.tm_min >= 30)
			end_tm.tm_hour++;
	}

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	(*end) = slurm_mktime(&end_tm);

	if (!sent_start) {
		if (!localtime_r(&my_time, &start_tm)) {
			error("Couldn't get localtime from start %ld",
			      (long)my_time);
			return SLURM_ERROR;
		}
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		//(*start) = slurm_mktime(&start_tm);
	} else {
		temp_time = sent_start;
		if (!localtime_r(&temp_time, &start_tm)) {
			error("Couldn't get localtime from user start %ld",
			      (long)my_time);
			return SLURM_ERROR;
		}
		if (start_tm.tm_sec >= 30)
			start_tm.tm_min++;
		if (start_tm.tm_min >= 30)
			start_tm.tm_hour++;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	(*start) = slurm_mktime(&start_tm);

	if ((*end)-(*start) < 3600)
		(*end) = (*start) + 3600;
/* 	info("now got %d and %d sent", (*start), (*end)); */
/* 	char start_char[20]; */
/* 	char end_char[20]; */
/* 	time_t my_start = (*start); */
/* 	time_t my_end = (*end); */

/* 	slurm_make_time_str(&my_start,  */
/* 			    start_char, sizeof(start_char)); */
/* 	slurm_make_time_str(&my_end, */
/* 			    end_char, sizeof(end_char)); */
/* 	info("which is %s - %s", start_char, end_char); */
	return SLURM_SUCCESS;
}

/* Convert a string to a duration in Months or Days
 * input formats:
 *   <integer>                defaults to Months
 *   <integer>Months
 *   <integer>Days
 *   <integer>Hours
 *
 * output:
 *   SLURMDB_PURGE_MONTHS | <integer>  if input is in Months
 *   SLURMDB_PURGE_DAYS   | <integer>  if input is in Days
 *   SLURMDB_PURGE_HOURS  | <integer>  if input in in Hours
 *   0 on error
 */
extern uint32_t slurmdb_parse_purge(char *string)
{
	int i = 0;
	uint32_t purge = NO_VAL;

	xassert(string);

	while(string[i]) {
		if ((string[i] >= '0') && (string[i] <= '9')) {
			if (purge == NO_VAL)
				purge = 0;
                        purge = (purge * 10) + (string[i] - '0');
                } else
			break;
		i++;
	}

	if (purge != NO_VAL) {
		int len = strlen(string+i);
		if (!len || !xstrncasecmp("months", string+i, MAX(len, 1))) {
			purge |= SLURMDB_PURGE_MONTHS;
		} else if (!xstrncasecmp("hours", string+i, MAX(len, 1))) {
			purge |= SLURMDB_PURGE_HOURS;
		} else if (!xstrncasecmp("days", string+i, MAX(len, 1))) {
			purge |= SLURMDB_PURGE_DAYS;
		} else {
			error("Invalid purge unit '%s', valid options "
			      "are hours, days, or months", string+i);
			purge = NO_VAL;
		}
	} else
		error("Invalid purge string '%s'", string);

	return purge;
}

extern char *slurmdb_purge_string(uint32_t purge, char *string, int len,
				  bool with_archive)
{
	uint32_t units;

	if (purge == NO_VAL) {
		snprintf(string, len, "NONE");
		return string;
	}

	units = SLURMDB_PURGE_GET_UNITS(purge);
	if (SLURMDB_PURGE_IN_HOURS(purge)) {
		if (with_archive && SLURMDB_PURGE_ARCHIVE_SET(purge))
			snprintf(string, len, "%u hours*", units);
		else
			snprintf(string, len, "%u hours", units);
	} else if (SLURMDB_PURGE_IN_DAYS(purge)) {
		if (with_archive && SLURMDB_PURGE_ARCHIVE_SET(purge))
			snprintf(string, len, "%u days*", units);
		else
			snprintf(string, len, "%u days", units);
	} else {
		if (with_archive && SLURMDB_PURGE_ARCHIVE_SET(purge))
			snprintf(string, len, "%u months*", units);
		else
			snprintf(string, len, "%u months", units);
	}

	return string;
}

typedef struct {
	bool add_set;
	bool equal_set;
	int option;
	List qos_list;
} qos_char_list_args_t;

static int _slurmdb_addto_qos_char_list_internal(List char_list, char *name,
						 void *args_in)
{
	char *tmp_name;
	uint32_t id;
	qos_char_list_args_t *args = args_in;

	int tmp_option = args->option;
	if ((name[0] == '+') || (name[0] == '-')) {
		tmp_option = name[0];
		name++;
	}

	id = str_2_slurmdb_qos(args->qos_list, name);
	if (id == NO_VAL) {
		char *tmp = _get_qos_list_str(args->qos_list);
		error("You gave a bad qos '%s'. Valid QOS's are %s", name, tmp);
		xfree(tmp);
		list_flush(char_list);
		return SLURM_ERROR;
	}

	if (tmp_option) {
		if (args->equal_set) {
			error("You can't set qos equal to something and then add or subtract from it in the same line");
			list_flush(char_list);
			return SLURM_ERROR;
		}
		args->add_set = 1;
		tmp_name = xstrdup_printf("%c%u", tmp_option, id);
	} else {
		if (args->add_set) {
			error("You can't set qos equal to something and then add or subtract from it in the same line");
			list_flush(char_list);
			return SLURM_ERROR;
		}
		args->equal_set = 1;
		tmp_name = xstrdup_printf("%u", id);
	}

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

extern int slurmdb_addto_qos_char_list(List char_list, List qos_list,
				       char *names, int option)
{
	int count;
	qos_char_list_args_t args = {0};

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	if (!xstrcmp(names, "")) {
		list_append(char_list, xstrdup(""));
		return 1;
	}

	args.option = option;
	args.qos_list = qos_list;

	count = slurm_parse_char_list(char_list, names, &args,
				      _slurmdb_addto_qos_char_list_internal);
	if (!count) {
		error("You gave me an empty qos list");
	}

	return count;
}

extern int slurmdb_send_accounting_update_persist(
	List update_list, slurm_persist_conn_t *persist_conn)
{
	slurm_msg_t req;
	slurm_msg_t resp;
	accounting_update_msg_t msg = {0};
	int rc;

	xassert(persist_conn);

	if (persist_conn->fd == PERSIST_CONN_NOT_INITED) {
		if (slurm_persist_conn_open(persist_conn) !=
		    SLURM_SUCCESS) {
			error("slurmdb_send_accounting_update_persist: Unable to open connection to registered cluster %s.",
			      persist_conn->cluster_name);
			persist_conn->fd = PERSIST_CONN_NOT_INITED;
		}
	}

	msg.update_list = update_list;
	msg.rpc_version = req.protocol_version = persist_conn->version;
	slurm_msg_t_init(&req);
	req.msg_type = ACCOUNTING_UPDATE_MSG;
	req.conn = persist_conn;
	req.data = &msg;

	/* resp is inited in slurm_send_recv_msg */
	rc = slurm_send_recv_msg(0, &req, &resp, 0);

	if (rc != SLURM_SUCCESS) {
		error("update cluster: %m to %s at %s(%hu)",
		      persist_conn->cluster_name,
		      persist_conn->rem_host,
		      persist_conn->rem_port);
	} else {
		rc = slurm_get_return_code(resp.msg_type, resp.data);
		slurm_free_return_code_msg(resp.data);
	}
	//info("got rc of %d", rc);
	return rc;
}

/*
 * send_accounting_update - send update to controller of cluster
 * IN update_list: updates to send
 * IN cluster: name of cluster
 * IN host: control host of cluster
 * IN port: control port of cluster
 * IN rpc_version: rpc version of cluster
 * RET:  error code
 */
extern int slurmdb_send_accounting_update(List update_list, char *cluster,
					  char *host, uint16_t port,
					  uint16_t rpc_version)
{
	accounting_update_msg_t msg;
	slurm_msg_t req;
	slurm_msg_t resp;
	int i, rc;

	// Set highest version that we can use
	if (rpc_version > SLURM_PROTOCOL_VERSION) {
		rpc_version = SLURM_PROTOCOL_VERSION;
	}
	memset(&msg, 0, sizeof(accounting_update_msg_t));
	msg.rpc_version = rpc_version;
	msg.update_list = update_list;

	debug("sending updates to %s at %s(%hu) ver %hu",
	      cluster, host, port, rpc_version);

	slurm_msg_t_init(&req);
	slurm_set_addr(&req.address, port, host);

	req.protocol_version = rpc_version;
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);

	req.msg_type = ACCOUNTING_UPDATE_MSG;
	if (slurmdbd_conf)
		req.flags = SLURM_GLOBAL_AUTH_KEY;
	req.data = &msg;
	slurm_msg_t_init(&resp);

	for (i = 0; i < 4; i++) {
		/* Retry if the slurmctld can connect, but is not responding */
		rc = slurm_send_recv_node_msg(&req, &resp, 0);
		if ((rc == SLURM_SUCCESS) ||
		    (errno != SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT))
			break;
	}
	if (rc != SLURM_SUCCESS) {
		error("update cluster: %m to %s at %s(%hu)",
		      cluster, host, port);
		rc = SLURM_ERROR;
	} else
		rc = slurm_get_return_code(resp.msg_type, resp.data);

	if (resp.auth_cred)
		auth_g_destroy(resp.auth_cred);

	slurm_free_return_code_msg(resp.data);

	//info("got rc of %d", rc);
	return rc;
}

extern slurmdb_report_cluster_rec_t *slurmdb_cluster_rec_2_report(
	slurmdb_cluster_rec_t *cluster)
{
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster;
	slurmdb_cluster_accounting_rec_t *accting = NULL;
	slurmdb_tres_rec_t *tres_rec;
	ListIterator itr = NULL;
	int count;

	xassert(cluster);
	slurmdb_report_cluster = xmalloc(sizeof(slurmdb_report_cluster_rec_t));
	slurmdb_report_cluster->name = xstrdup(cluster->name);

	if (!(count = list_count(cluster->accounting_list)))
		return slurmdb_report_cluster;

	/* get the amount of time and the average count
	   during the time we are looking at */
	itr = list_iterator_create(cluster->accounting_list);
	while ((accting = list_next(itr)))
		slurmdb_add_cluster_accounting_to_tres_list(
			accting, &slurmdb_report_cluster->tres_list);
	list_iterator_destroy(itr);

	itr = list_iterator_create(slurmdb_report_cluster->tres_list);
	while ((tres_rec = list_next(itr)))
		tres_rec->count /= tres_rec->rec_count;
	list_iterator_destroy(itr);

	return slurmdb_report_cluster;
}

/*
 * get the first cluster that will run a job
 * IN: req - description of resource allocation request
 * IN: cluster_names - comma separated string of cluster names
 * OUT: cluster_rec - record of selected cluster or NULL if none found or
 * 		      cluster_names is NULL
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 *
 * Note: Cluster_rec needs to be freed with slurmdb_destroy_cluster_rec() when
 * called
 * Note: The will_runs are not threaded. Currently it relies on the
 * working_cluster_rec to pack the job_desc's jobinfo. See previous commit for
 * an example of how to thread this.
 */
extern int slurmdb_get_first_avail_cluster(job_desc_msg_t *req,
	char *cluster_names, slurmdb_cluster_rec_t **cluster_rec)
{
	local_cluster_rec_t *local_cluster = NULL;
	int rc = SLURM_SUCCESS;
	char local_hostname[64];
	ListIterator itr;
	List cluster_list = NULL;
	List ret_list = NULL;
	List tried_feds = NULL;

	*cluster_rec = NULL;
	cluster_list = slurmdb_get_info_cluster(cluster_names);

	/* return if we only have 1 or less clusters here */
	if (!cluster_list || !list_count(cluster_list)) {
		rc = SLURM_ERROR;
		goto end_it;
	} else if (list_count(cluster_list) == 1) {
		*cluster_rec = list_pop(cluster_list);
		goto end_it;
	}

	if ((req->alloc_node == NULL) &&
	    (gethostname_short(local_hostname, sizeof(local_hostname)) == 0)) {
		req->alloc_node = local_hostname;
	}

	if (working_cluster_rec)
		*cluster_rec = working_cluster_rec;

	tried_feds = list_create(NULL);
	ret_list = list_create(xfree_ptr);
	itr = list_iterator_create(cluster_list);
	while ((working_cluster_rec = list_next(itr))) {
		/* only try one cluster from each federation */
		if (working_cluster_rec->fed.id &&
		    list_find_first(tried_feds, slurm_find_char_in_list,
				    working_cluster_rec->fed.name))
			continue;

		if ((local_cluster = _job_will_run(req))) {
			list_append(ret_list, local_cluster);
			if (working_cluster_rec->fed.id)
				list_append(tried_feds,
					    working_cluster_rec->fed.name);
		} else {
			error("Problem with submit to cluster %s: %m",
			      working_cluster_rec->name);
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(tried_feds);

	/* restore working_cluster_rec in case it was already set */
	if (*cluster_rec) {
		working_cluster_rec = *cluster_rec;
		*cluster_rec = NULL;
	}

	if (req->alloc_node == local_hostname)
		req->alloc_node = NULL;

	if (!list_count(ret_list)) {
		error("Can't run on any of the specified clusters");
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* sort the list so the first spot is on top */
	list_sort(ret_list, (ListCmpF)_sort_local_cluster);
	local_cluster = list_peek(ret_list);

	/* prevent cluster_rec from being freed when cluster_list is destroyed */
	itr = list_iterator_create(cluster_list);
	while ((*cluster_rec = list_next(itr))) {
		if (*cluster_rec == local_cluster->cluster_rec) {
			list_remove(itr);
			break;
		}
	}
	list_iterator_destroy(itr);
end_it:
	FREE_NULL_LIST(ret_list);
	FREE_NULL_LIST(cluster_list);

	return rc;
}

/* Report the latest start time for any hetjob component on this cluster.
 * Return NULL if any component can not run here */
static local_cluster_rec_t * _het_job_will_run(List job_req_list)
{
	local_cluster_rec_t *local_cluster = NULL, *tmp_cluster;
	job_desc_msg_t *req;
	ListIterator iter;

	iter = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(iter))) {
		tmp_cluster = _job_will_run(req);
		if (!tmp_cluster) {	/* Some het component can't run here */
			xfree(local_cluster);
			break;
		}
		if (!local_cluster) {
			local_cluster = tmp_cluster;
			tmp_cluster = NULL;
		} else if (local_cluster->start_time < tmp_cluster->start_time)
			local_cluster->start_time = tmp_cluster->start_time;
		xfree(tmp_cluster);
	}
	list_iterator_destroy(iter);

	return local_cluster;
}

/*
 * get the first cluster that will run a heterogeneous job
 * IN: req - description of resource allocation request
 * IN: cluster_names - comma separated string of cluster names
 * OUT: cluster_rec - record of selected cluster or NULL if none found or
 * 		      cluster_names is NULL
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 *
 * Note: Cluster_rec needs to be freed with slurmdb_destroy_cluster_rec() when
 * called
 * Note: The will_runs are not threaded. Currently it relies on the
 * working_cluster_rec to pack the job_desc's jobinfo. See previous commit for
 * an example of how to thread this.
 */
extern int slurmdb_get_first_het_job_cluster(List job_req_list,
	char *cluster_names, slurmdb_cluster_rec_t **cluster_rec)
{
	job_desc_msg_t *req;
	local_cluster_rec_t *local_cluster = NULL;
	int rc = SLURM_SUCCESS;
	char local_hostname[64] = "";
	ListIterator itr;
	List cluster_list = NULL;
	List ret_list = NULL;
	List tried_feds = NULL;

	*cluster_rec = NULL;
	cluster_list = slurmdb_get_info_cluster(cluster_names);

	/* return if we only have 1 or less clusters here */
	if (!cluster_list || !list_count(cluster_list)) {
		rc = SLURM_ERROR;
		goto end_it;
	} else if (list_count(cluster_list) == 1) {
		*cluster_rec = list_pop(cluster_list);
		goto end_it;
	}

	(void) gethostname_short(local_hostname, sizeof(local_hostname));
	itr = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(itr))) {
		if ((req->alloc_node == NULL) && local_hostname[0])
			req->alloc_node = local_hostname;
	}
	list_iterator_destroy(itr);

	if (working_cluster_rec)
		*cluster_rec = working_cluster_rec;

	tried_feds = list_create(NULL);
	ret_list = list_create(xfree_ptr);
	itr = list_iterator_create(cluster_list);
	while ((working_cluster_rec = list_next(itr))) {
		/* only try one cluster from each federation */
		if (working_cluster_rec->fed.id &&
		    list_find_first(tried_feds, slurm_find_char_in_list,
				    working_cluster_rec->fed.name))
			continue;
		if ((local_cluster = _het_job_will_run(job_req_list))) {
			list_append(ret_list, local_cluster);
			if (working_cluster_rec->fed.id)
				list_append(tried_feds,
					    working_cluster_rec->fed.name);
		} else {
			error("Problem with submit to cluster %s: %m",
			      working_cluster_rec->name);
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(tried_feds);

	/* restore working_cluster_rec in case it was already set */
	if (*cluster_rec) {
		working_cluster_rec = *cluster_rec;
		*cluster_rec = NULL;
	}

	itr = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(itr))) {
		if (req->alloc_node == local_hostname)
			req->alloc_node = NULL;
	}
	list_iterator_destroy(itr);

	if (!list_count(ret_list)) {
		error("Can't run on any of the specified clusters");
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* sort the list so the first spot is on top */
	list_sort(ret_list, (ListCmpF)_sort_local_cluster);
	local_cluster = list_peek(ret_list);

	/* prevent cluster_rec from being freed when cluster_list is destroyed */
	itr = list_iterator_create(cluster_list);
	while ((*cluster_rec = list_next(itr))) {
		if (*cluster_rec == local_cluster->cluster_rec) {
			list_remove(itr);
			break;
		}
	}
	list_iterator_destroy(itr);
end_it:
	FREE_NULL_LIST(ret_list);
	FREE_NULL_LIST(cluster_list);

	return rc;
}

extern void slurmdb_copy_assoc_rec_limits(slurmdb_assoc_rec_t *out,
					  slurmdb_assoc_rec_t *in)
{
	out->grp_jobs = in->grp_jobs;
	out->grp_jobs_accrue = in->grp_jobs_accrue;
	out->grp_submit_jobs = in->grp_submit_jobs;
	xfree(out->grp_tres);
	out->grp_tres = xstrdup(in->grp_tres);
	xfree(out->grp_tres_mins);
	out->grp_tres_mins = xstrdup(in->grp_tres_mins);
	xfree(out->grp_tres_run_mins);
	out->grp_tres_run_mins = xstrdup(in->grp_tres_run_mins);
	out->grp_wall = in->grp_wall;

	out->max_jobs = in->max_jobs;
	out->max_jobs_accrue = in->max_jobs_accrue;
	out->min_prio_thresh = in->min_prio_thresh;
	out->max_submit_jobs = in->max_submit_jobs;
	xfree(out->max_tres_pj);
	out->max_tres_pj = xstrdup(in->max_tres_pj);
	xfree(out->max_tres_pn);
	out->max_tres_pn = xstrdup(in->max_tres_pn);
	xfree(out->max_tres_mins_pj);
	out->max_tres_mins_pj =	xstrdup(in->max_tres_mins_pj);
	xfree(out->max_tres_run_mins);
	out->max_tres_run_mins = xstrdup(in->max_tres_run_mins);
	out->max_wall_pj = in->max_wall_pj;

	out->priority = in->priority;
	out->comment = xstrdup(in->comment);

	FREE_NULL_LIST(out->qos_list);
	out->qos_list = slurm_copy_char_list(in->qos_list);
}

extern void slurmdb_copy_cluster_rec(slurmdb_cluster_rec_t *out,
				     slurmdb_cluster_rec_t *in)
{
	out->classification   = in->classification;
	xfree(out->control_host);
	out->control_host     = xstrdup(in->control_host);
	out->control_port     = in->control_port;
	out->dimensions       = in->dimensions;
	xfree(out->fed.name);
	out->fed.name         = xstrdup(in->fed.name);
	out->fed.id           = in->fed.id;
	out->fed.state        = in->fed.state;
	out->flags            = in->flags;
	xfree(out->name);
	out->name             = xstrdup(in->name);
	xfree(out->nodes);
	out->nodes            = xstrdup(in->nodes);
	out->plugin_id_select = in->plugin_id_select;
	out->rpc_version      = in->rpc_version;
	xfree(out->tres_str);
	out->tres_str         = xstrdup(in->tres_str);

	slurmdb_destroy_assoc_rec(out->root_assoc);
	if (in->root_assoc) {
		out->root_assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(out->root_assoc, 0);
		slurmdb_copy_assoc_rec_limits( out->root_assoc, in->root_assoc);
	}

	FREE_NULL_LIST(out->fed.feature_list);
	if (in->fed.feature_list) {
		out->fed.feature_list = list_create(xfree_ptr);
		slurm_char_list_copy(out->fed.feature_list,
				     in->fed.feature_list);
	}

	/* Not copied currently:
	 * accounting_list
	 * control_addr
	 * dim_size
	 * fed.recv
	 * fed.send
	 */
}

extern void slurmdb_copy_federation_rec(slurmdb_federation_rec_t *out,
					slurmdb_federation_rec_t *in)
{
	xfree(out->name);
	out->name     = xstrdup(in->name);
	out->flags    = in->flags;

	FREE_NULL_LIST(out->cluster_list);
	if (in->cluster_list) {
		slurmdb_cluster_rec_t *cluster_in = NULL;
		ListIterator itr  = list_iterator_create(in->cluster_list);
		out->cluster_list = list_create(slurmdb_destroy_cluster_rec);
		while ((cluster_in = list_next(itr))) {
			slurmdb_cluster_rec_t *cluster_out =
				xmalloc(sizeof(slurmdb_cluster_rec_t));
			slurmdb_init_cluster_rec(cluster_out, 0);
			slurmdb_copy_cluster_rec(cluster_out, cluster_in);
			list_append(out->cluster_list, cluster_out);
		}
		list_iterator_destroy(itr);
	}
}

extern void slurmdb_copy_qos_rec_limits(slurmdb_qos_rec_t *out,
					slurmdb_qos_rec_t *in)
{
	out->flags = in->flags;
	out->grace_time = in->grace_time;
	out->grp_jobs = in->grp_jobs;
	out->grp_jobs_accrue = in->grp_jobs_accrue;
	out->grp_submit_jobs = in->grp_submit_jobs;
	xfree(out->grp_tres);
	out->grp_tres = xstrdup(in->grp_tres);
	xfree(out->grp_tres_mins);
	out->grp_tres_mins = xstrdup(in->grp_tres_mins);
	xfree(out->grp_tres_run_mins);
	out->grp_tres_run_mins = xstrdup(in->grp_tres_run_mins);
	out->grp_wall = in->grp_wall;

	out->limit_factor = in->limit_factor;

	out->max_jobs_pa = in->max_jobs_pa;
	out->max_jobs_pu = in->max_jobs_pu;
	out->max_jobs_accrue_pa = in->max_jobs_accrue_pa;
	out->max_jobs_accrue_pu = in->max_jobs_accrue_pu;
	out->max_submit_jobs_pa = in->max_submit_jobs_pa;
	out->max_submit_jobs_pu = in->max_submit_jobs_pu;
	xfree(out->max_tres_mins_pj);
	out->max_tres_mins_pj =	xstrdup(in->max_tres_mins_pj);
	xfree(out->max_tres_pa);
	out->max_tres_pa = xstrdup(in->max_tres_pa);
	xfree(out->max_tres_pj);
	out->max_tres_pj = xstrdup(in->max_tres_pj);
	xfree(out->max_tres_pn);
	out->max_tres_pn = xstrdup(in->max_tres_pn);
	xfree(out->max_tres_pu);
	out->max_tres_pu = xstrdup(in->max_tres_pu);
	xfree(out->max_tres_run_mins_pa);
	out->max_tres_run_mins_pa = xstrdup(in->max_tres_run_mins_pa);
	xfree(out->max_tres_run_mins_pu);
	out->max_tres_run_mins_pu = xstrdup(in->max_tres_run_mins_pu);
	out->max_wall_pj = in->max_wall_pj;
	out->min_prio_thresh = in->min_prio_thresh;
	xfree(out->min_tres_pj);
	out->min_tres_pj = xstrdup(in->min_tres_pj);

	FREE_NULL_LIST(out->preempt_list);
	out->preempt_list = slurm_copy_char_list(in->preempt_list);

	out->preempt_mode = in->preempt_mode;
	out->preempt_exempt_time = in->preempt_exempt_time;

	out->priority = in->priority;

	out->usage_factor = in->usage_factor;
	out->usage_thres = in->usage_thres;
}

extern slurmdb_tres_rec_t *slurmdb_copy_tres_rec(slurmdb_tres_rec_t *tres)
{
	slurmdb_tres_rec_t *tres_out = NULL;

	if (!tres)
		return tres_out;

	tres_out = xmalloc_nz(sizeof(slurmdb_tres_rec_t));
	memcpy(tres_out, tres, sizeof(slurmdb_tres_rec_t));
	tres_out->name = xstrdup(tres->name);
	tres_out->type = xstrdup(tres->type);

	return tres_out;
}

extern List slurmdb_copy_tres_list(List tres)
{
	slurmdb_tres_rec_t *tres_rec = NULL;
	ListIterator itr;
	List tres_out;

	if (!tres)
		return NULL;

	tres_out = list_create(slurmdb_destroy_tres_rec);

	itr = list_iterator_create(tres);
	while ((tres_rec = list_next(itr)))
		list_append(tres_out, slurmdb_copy_tres_rec(tres_rec));
	list_iterator_destroy(itr);

	return tres_out;
}

extern List slurmdb_diff_tres_list(List tres_list_old, List tres_list_new)
{
	slurmdb_tres_rec_t *tres_rec = NULL, *tres_rec_old;
	ListIterator itr;
	List tres_out;

	if (!tres_list_new || !list_count(tres_list_new))
		return NULL;

	tres_out = slurmdb_copy_tres_list(tres_list_new);

	itr = list_iterator_create(tres_out);
	while ((tres_rec = list_next(itr))) {
		if (!(tres_rec_old = list_find_first(tres_list_old,
						     slurmdb_find_tres_in_list,
						     &tres_rec->id)))
			continue;
		if (tres_rec_old->count == tres_rec->count)
			list_delete_item(itr);
	}
	list_iterator_destroy(itr);

	return tres_out;
}

extern char *slurmdb_tres_string_combine_lists(
	List tres_list_old, List tres_list_new)
{
	slurmdb_tres_rec_t *tres_rec = NULL, *tres_rec_old;
	ListIterator itr;
	char *tres_str = NULL;

	if (!tres_list_new || !list_count(tres_list_new))
		return NULL;

	itr = list_iterator_create(tres_list_new);
	while ((tres_rec = list_next(itr))) {
		if (!(tres_rec_old = list_find_first(tres_list_old,
						     slurmdb_find_tres_in_list,
						     &tres_rec->id))
		    || (tres_rec_old->count == INFINITE64))
			continue;
		if (tres_str)
			xstrcat(tres_str, ",");
		xstrfmtcat(tres_str, "%u=%"PRIu64,
			   tres_rec->id, tres_rec->count);
	}
	list_iterator_destroy(itr);

	return tres_str;
}

/* caller must xfree this char * returned */
extern char *slurmdb_make_tres_string(List tres, uint32_t flags)
{
	char *tres_str = NULL;
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec;

	if (!tres)
		return tres_str;

	itr = list_iterator_create(tres);
	while ((tres_rec = list_next(itr))) {
		if ((flags & TRES_STR_FLAG_REMOVE) &&
		    (tres_rec->count == INFINITE64))
			continue;

		if ((flags & TRES_STR_FLAG_SIMPLE) || !tres_rec->type)
			xstrfmtcat(tres_str, "%s%u=%"PRIu64,
				   (tres_str ||
				    (flags & TRES_STR_FLAG_COMMA1)) ? "," : "",
				   tres_rec->id, tres_rec->count);

		else
			xstrfmtcat(tres_str, "%s%s%s%s=%"PRIu64,
				   (tres_str ||
				    (flags & TRES_STR_FLAG_COMMA1)) ? "," : "",
				   tres_rec->type,
				   tres_rec->name ? "/" : "",
				   tres_rec->name ? tres_rec->name : "",
				   tres_rec->count);
	}
	list_iterator_destroy(itr);

	return tres_str;
}

extern char *slurmdb_make_tres_string_from_arrays(char **tres_names,
						  uint64_t *tres_cnts,
						  uint32_t tres_cnt,
						  uint32_t flags)
{
	char *tres_str = NULL;
	int i;

	if (!tres_names || !tres_cnts)
		return tres_str;

	for (i=0; i<tres_cnt; i++) {
		if ((tres_cnts[i] == INFINITE64) &&
		    (flags & TRES_STR_FLAG_REMOVE))
			continue;
		xstrfmtcat(tres_str, "%s%s=%"PRIu64,
			   tres_str ? "," : "", tres_names[i], tres_cnts[i]);
	}

	return tres_str;
}

extern char *slurmdb_make_tres_string_from_simple(
	char *tres_in, List full_tres_list, int spec_unit,
	uint32_t convert_flags, uint32_t tres_str_flags, char *nodes)
{
	char *tres_str = NULL;
	char *tmp_str = tres_in;
	int id;
	uint64_t count;
	slurmdb_tres_rec_t *tres_rec;
	char *node_name = NULL;
	List char_list = NULL;

	if (!full_tres_list || !tmp_str || !tmp_str[0]
	    || tmp_str[0] < '0' || tmp_str[0] > '9')
		return tres_str;

	while (tmp_str) {
		id = atoi(tmp_str);
		if (id <= 0) {
			error("slurmdb_make_tres_string_from_simple: no id "
			      "found at %s instead", tmp_str);
			goto get_next;
		}

		if (!(tres_rec = list_find_first(
			      full_tres_list, slurmdb_find_tres_in_list,
			      &id))) {
			debug("No tres known by id %d", id);
			goto get_next;
		}

		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("slurmdb_make_tres_string_from_simple: "
			      "no value found");
			break;
		}
		count = slurm_atoull(++tmp_str);

		if (count == NO_VAL64)
			goto get_next;

		if (tres_str)
			xstrcat(tres_str, ",");
		if (!tres_rec->type)
			xstrfmtcat(tres_str, "%u=", tres_rec->id);

		else
			xstrfmtcat(tres_str, "%s%s%s=",
				   tres_rec->type,
				   tres_rec->name ? "/" : "",
				   tres_rec->name ? tres_rec->name : "");
		if (count != INFINITE64) {
			if (nodes) {
				node_name = find_hostname(count, nodes);
				xstrfmtcat(tres_str, "%s", node_name);
				xfree(node_name);
			} else if (tres_str_flags & TRES_STR_FLAG_BYTES) {
				/* This mean usage */
				char outbuf[FORMAT_STRING_SIZE];
				if (tres_rec->id == TRES_CPU) {
					count /= CPU_TIME_ADJ;
					secs2time_str((time_t)count, outbuf,
						      FORMAT_STRING_SIZE);
				} else
					convert_num_unit((double)count, outbuf,
							 sizeof(outbuf),
							 UNIT_NONE,
							 spec_unit,
							 convert_flags);
				xstrfmtcat(tres_str, "%s", outbuf);
			} else if ((tres_rec->id == TRES_MEM) ||
				   !xstrcasecmp(tres_rec->name, "gpumem") ||
				   !xstrcasecmp(tres_rec->type, "bb")) {
				char outbuf[FORMAT_STRING_SIZE];
				convert_num_unit((double)count, outbuf,
						 sizeof(outbuf), UNIT_MEGA,
						 spec_unit, convert_flags);
				xstrfmtcat(tres_str, "%s", outbuf);
			} else {
				xstrfmtcat(tres_str, "%"PRIu64, count);
			}
		} else
			xstrfmtcat(tres_str, "NONE");

		if (!(tres_str_flags & TRES_STR_FLAG_SORT_ID)) {
			if (!char_list)
				char_list = list_create(xfree_ptr);
			list_append(char_list, tres_str);
			tres_str = NULL;
		}
	get_next:
		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	if (char_list) {
		tres_str = slurm_char_list_to_xstr(char_list);
		FREE_NULL_LIST(char_list);
	}

	return tres_str;
}

extern char *slurmdb_format_tres_str(
	char *tres_in, List full_tres_list, bool simple)
{
	char *tres_str = NULL;
	char *val_unit = NULL;
	char *tmp_str = tres_in;
	uint64_t count;
	slurmdb_tres_rec_t *tres_rec;

	if (!full_tres_list || !tmp_str || !tmp_str[0])
		return tres_str;

	if (tmp_str[0] == ',')
		tmp_str++;

	while (tmp_str) {
		if (tmp_str[0] >= '0' && tmp_str[0] <= '9') {
			int id = atoi(tmp_str);
			if (id <= 0) {
				error("%s: cannot convert %s to ID.",
				      __func__, tmp_str);
				return NULL;
			}

			if (!(tres_rec = list_find_first(
				      full_tres_list, slurmdb_find_tres_in_list,
				      &id))) {
				error("%s: no TRES known by id %d",
				      __func__, id);
				return NULL;
			}
		} else {
			int end = 0;
			char *tres_name;

			while (tmp_str[end]) {
				if (tmp_str[end] == '=')
					break;
				end++;
			}
			if (!tmp_str[end]) {
				error("%s: no TRES id found for %s",
				      __func__, tmp_str);
				return NULL;
			}
			tres_name = xstrndup(tmp_str, end);
			if (!(tres_rec = list_find_first(
				      full_tres_list,
				      slurmdb_find_tres_in_list_by_type,
				      tres_name))) {
				error("%s: no TRES known by type %s",
				      __func__, tres_name);
				xfree(tres_name);
				return NULL;
			}
			xfree(tres_name);
		}

		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("%s: no value given as TRES type/id.", __func__);
			return NULL;
		}
		count = strtoull(++tmp_str, &val_unit, 10);
		if (val_unit && *val_unit != ',' && *val_unit != '\0' &&
		    tres_rec->type) {
			int base_unit =
				slurmdb_get_tres_base_unit(tres_rec->type);
			int convert_val =
				get_convert_unit_val(base_unit, *val_unit);
			if (convert_val > 0)
				count *= convert_val;
		}

		if (tres_str)
			xstrcat(tres_str, ",");
		if (simple || !tres_rec->type)
			xstrfmtcat(tres_str, "%u=%"PRIu64"",
				   tres_rec->id, count);

		else
			xstrfmtcat(tres_str, "%s%s%s=%"PRIu64"",
				   tres_rec->type,
				   tres_rec->name ? "/" : "",
				   tres_rec->name ? tres_rec->name : "",
				   count);
		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return tres_str;
}

/*
 * Comparator used for sorting tres by id
 *
 * returns: -1 tres_a < tres_b   0: tres_a == tres_b   1: tres_a > tres_b
 *
 */
extern int slurmdb_sort_tres_by_id_asc(void *v1, void *v2)
{
	slurmdb_tres_rec_t *tres_a = *(slurmdb_tres_rec_t **)v1;
	slurmdb_tres_rec_t *tres_b = *(slurmdb_tres_rec_t **)v2;

	if ((tres_a->id > TRES_STATIC_CNT) &&
	    (tres_b->id > TRES_STATIC_CNT)) {
		int diff = xstrcmp(tres_a->type, tres_b->type);

		if (diff < 0)
			return -1;
		else if (diff > 0)
			return 1;

		diff = xstrcmp(tres_a->name, tres_b->name);

		if (diff < 0)
			return -1;
		else if (diff > 0)
			return 1;
	}

	if (tres_a->id < tres_b->id)
		return -1;
	else if (tres_a->id > tres_b->id)
		return 1;

	return 0;
}

/* This only works on a simple id=count list, not on a formatted list */
extern void slurmdb_tres_list_from_string(
	List *tres_list, const char *tres, uint32_t flags)
{
	const char *tmp_str = tres;
	int id;
	uint64_t count;
	slurmdb_tres_rec_t *tres_rec;
	int remove_found = 0;
	xassert(tres_list);

	if (!tres || !tres[0])
		return;

	if (tmp_str[0] == ',')
		tmp_str++;

	while (tmp_str) {
		id = atoi(tmp_str);
		/* 0 isn't a valid tres id */
		if (id <= 0) {
			error("slurmdb_tres_list_from_string: no id "
			      "found at %s instead", tmp_str);
			break;
		}
		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("slurmdb_tres_list_from_string: "
			      "no value found %s", tres);
			break;
		}
		count = slurm_atoull(++tmp_str);

		if (!*tres_list)
			*tres_list = list_create(slurmdb_destroy_tres_rec);

		if (!(tres_rec = list_find_first(
			      *tres_list, slurmdb_find_tres_in_list, &id))) {
			tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
			tres_rec->id = id;
			tres_rec->count = count;
			list_append(*tres_list, tres_rec);
			if (count == INFINITE64)
				remove_found++;
		} else if (flags & TRES_STR_FLAG_REPLACE) {
			debug2("TRES %u was already here with count %"PRIu64", "
			       "replacing with %"PRIu64,
			      tres_rec->id, tres_rec->count, count);
			tres_rec->count = count;
		} else if (flags & TRES_STR_FLAG_SUM) {
			if (count != INFINITE64) {
				if (tres_rec->count == INFINITE64)
					tres_rec->count = count;
				else
					tres_rec->count += count;
			}
		} else if (flags & TRES_STR_FLAG_MAX) {
			if (count != INFINITE64) {
				if (tres_rec->count == INFINITE64)
					tres_rec->count = count;
				else
					tres_rec->count =
						MAX(tres_rec->count, count);
			}
		} else if (flags & TRES_STR_FLAG_MIN) {
			if (count != INFINITE64) {
				if (tres_rec->count == INFINITE64)
					tres_rec->count = count;
				else
					tres_rec->count =
						MIN(tres_rec->count, count);
			}
		}

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	if (remove_found && (flags & TRES_STR_FLAG_REMOVE)) {
		/* here we will remove the tres we don't want in the
		   string */
		uint64_t inf64 = INFINITE64;
		int removed;

		if ((removed = list_delete_all(
			     *tres_list,
			     slurmdb_find_tres_in_list_by_count,
			     &inf64)) != remove_found)
			debug("slurmdb_tres_list_from_string: "
			      "was expecting to remove %d, but removed %d",
			      remove_found, removed);
	}

	if (*tres_list && (flags & TRES_STR_FLAG_SORT_ID))
		list_sort(*tres_list, (ListCmpF)slurmdb_sort_tres_by_id_asc);

	return;
}

extern char *slurmdb_combine_tres_strings(
	char **tres_str_old, char *tres_str_new, uint32_t flags)
{
	List tres_list = NULL;

	xassert(tres_str_old);

	/* If a new string is being added concat it onto the old
	 * string, then send it to slurmdb_tres_list_from_string which
	 * will make it a unique list if flags doesn't contain
	 * TRES_STR_FLAG_ONLY_CONCAT.
	 */
	if (tres_str_new && tres_str_new[0])
		xstrfmtcat(*tres_str_old, "%s%s%s",
			   (flags & (TRES_STR_FLAG_COMMA1 |
				     TRES_STR_FLAG_ONLY_CONCAT)) ? "," : "",
			   (*tres_str_old && tres_str_new[0] != ',') ? "," : "",
			   tres_str_new);

	if (flags & TRES_STR_FLAG_ONLY_CONCAT)
		goto endit;

	slurmdb_tres_list_from_string(&tres_list, *tres_str_old, flags);
	xfree(*tres_str_old);

	/* Always make it a simple string */
	flags |= TRES_STR_FLAG_SIMPLE;

	/* Make a new string from the combined */
	*tres_str_old = slurmdb_make_tres_string(tres_list, flags);

	FREE_NULL_LIST(tres_list);
endit:
	/* Send back a blank string instead of NULL. */
	if (!*tres_str_old && (flags & TRES_STR_FLAG_NO_NULL))
		*tres_str_old = xstrdup("");

	return *tres_str_old;
}

extern slurmdb_tres_rec_t *slurmdb_find_tres_in_string(
	char *tres_str_in, int id)
{
	slurmdb_tres_rec_t *tres_rec = NULL;
	char *tmp_str = tres_str_in;

	if (!tmp_str || !tmp_str[0])
		return tres_rec;

	while (tmp_str) {
		if (id == atoi(tmp_str)) {
			if (!(tmp_str = strchr(tmp_str, '='))) {
				error("%s: no value found", __func__);
				break;
			}
			tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
			tres_rec->id = id;
			tres_rec->count = slurm_atoull(++tmp_str);
			return tres_rec;
		}

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return tres_rec;
}

extern uint64_t slurmdb_find_tres_count_in_string(char *tres_str_in, int id)
{
	char *tmp_str = tres_str_in;

	if (!tmp_str || !tmp_str[0])
		return INFINITE64;

	while (tmp_str) {
		if (id == atoi(tmp_str)) {
			if (!(tmp_str = strchr(tmp_str, '='))) {
				error("slurmdb_find_tres_count_in_string: "
				      "no value found");
				break;
			}
			return slurm_atoull(++tmp_str);
		}

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return INFINITE64;
}

extern int slurmdb_find_qos_in_list_by_name(void *x, void *key)
{
	slurmdb_qos_rec_t *qos_rec = (slurmdb_qos_rec_t *)x;
	char *name = (char *)key;

	if (!xstrcmp(qos_rec->name, name))
		return 1;

	return 0;
}

extern int slurmdb_find_qos_in_list(void *x, void *key)
{
	slurmdb_qos_rec_t *qos_rec = (slurmdb_qos_rec_t *)x;
	uint32_t qos_id = *(uint32_t *)key;

	if (qos_rec->id == qos_id)
		return 1;

	return 0;
}

extern int slurmdb_find_selected_step_in_list(void *x, void *key)
{
	slurm_selected_step_t *selected_step = (slurm_selected_step_t *)x;
	slurm_selected_step_t *query_step = (slurm_selected_step_t *)key;

	if (!memcmp(&query_step->step_id, &selected_step->step_id,
		    sizeof(query_step->step_id)) &&
	    (query_step->array_task_id == selected_step->array_task_id) &&
	    (query_step->het_job_offset == selected_step->het_job_offset))
		return 1;

	return 0;
}

extern int slurmdb_find_assoc_in_list(void *x, void *key)
{
	slurmdb_assoc_rec_t *assoc_rec = (slurmdb_assoc_rec_t *)x;
	uint32_t assoc_id = *(uint32_t *)key;

	if (assoc_rec->id == assoc_id)
		return 1;

	return 0;
}

extern int slurmdb_find_update_object_in_list(void *x, void *key)
{
	slurmdb_update_object_t *update_object = (slurmdb_update_object_t *)x;
	slurmdb_update_type_t type = *(slurmdb_update_type_t *)key;

	if (update_object->type == type)
		return 1;

	return 0;
}


extern int slurmdb_find_tres_in_list(void *x, void *key)
{
	slurmdb_tres_rec_t *tres_rec = (slurmdb_tres_rec_t *)x;
	uint32_t tres_id = *(uint32_t *)key;

	if (tres_rec->id == tres_id)
		return 1;

	return 0;
}

extern int slurmdb_find_tres_in_list_by_count(void *x, void *key)
{
	slurmdb_tres_rec_t *tres_rec = (slurmdb_tres_rec_t *)x;
	uint64_t count = *(uint64_t *)key;

	if (tres_rec->count == count)
		return 1;

	return 0;
}

extern int slurmdb_find_tres_in_list_by_type(void *x, void *key)
{
	slurmdb_tres_rec_t *tres_rec = (slurmdb_tres_rec_t *)x;
	char *type = (char *)key;
	int end = 0;
	bool found = false;

	while (type[end]) {
		if (type[end] == '/') {
			found = true;
			break;
		}
		end++;
	}

	if (!xstrncasecmp(tres_rec->type, type, end)) {
		if ((!found && !tres_rec->name) ||
		    (found && !xstrcasecmp(tres_rec->name, type + end + 1))) {
			return 1;
		}
	}

	return 0;
}

extern int slurmdb_find_cluster_in_list(void *x, void *key)
{
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)x;
	char *name = (char *)key;

	if (!xstrcmp(object->name, name))
		return 1;

	return 0;
}

extern int slurmdb_find_cluster_accting_tres_in_list(void *x, void *key)
{
	slurmdb_cluster_accounting_rec_t *object =
		(slurmdb_cluster_accounting_rec_t *)x;
	uint32_t tres_id = *(uint32_t *)key;

	if (object->tres_rec.id == tres_id)
		return 1;

	return 0;
}

extern int slurmdb_add_cluster_accounting_to_tres_list(
	slurmdb_cluster_accounting_rec_t *accting,
	List *tres)
{
	slurmdb_tres_rec_t *tres_rec = NULL;

	if (!*tres)
		*tres = list_create(slurmdb_destroy_tres_rec);
	else
		tres_rec = list_find_first(*tres,
					   slurmdb_find_tres_in_list,
					   &accting->tres_rec.id);

	if (!tres_rec) {
		tres_rec = slurmdb_copy_tres_rec(&accting->tres_rec);
		if (!tres_rec) {
			error("slurmdb_copy_tres_rec returned NULL");
			return SLURM_ERROR;
		}
		list_push(*tres, tres_rec);
	}

	tres_rec->alloc_secs += accting->alloc_secs
		+ accting->down_secs + accting->idle_secs
		+ accting->plan_secs + accting->pdown_secs;
	tres_rec->count += accting->tres_rec.count;
	tres_rec->rec_count++;

	return SLURM_SUCCESS;
}

extern int slurmdb_add_accounting_to_tres_list(
	slurmdb_accounting_rec_t *accting,
	List *tres)
{
	slurmdb_tres_rec_t *tres_rec = NULL;

	if (!*tres)
		*tres = list_create(slurmdb_destroy_tres_rec);
	else
		tres_rec = list_find_first(*tres,
					   slurmdb_find_tres_in_list,
					   &accting->tres_rec.id);

	if (!tres_rec) {
		tres_rec = slurmdb_copy_tres_rec(&accting->tres_rec);
		if (!tres_rec) {
			error("slurmdb_copy_tres_rec returned NULL");
			return SLURM_ERROR;
		}
		list_push(*tres, tres_rec);
	}

	tres_rec->alloc_secs += accting->alloc_secs;

	return SLURM_SUCCESS;
}

extern int slurmdb_add_time_from_count_to_tres_list(
	slurmdb_tres_rec_t *tres_in, List *tres, time_t elapsed)
{
	slurmdb_tres_rec_t *tres_rec = NULL;

	if (!elapsed)
		return SLURM_SUCCESS;

	if (!*tres)
		*tres = list_create(slurmdb_destroy_tres_rec);
	else
		tres_rec = list_find_first(*tres,
					   slurmdb_find_tres_in_list,
					   &tres_in->id);

	if (!tres_rec) {
		tres_rec = slurmdb_copy_tres_rec(tres_in);
		if (!tres_rec) {
			error("slurmdb_copy_tres_rec returned NULL");
			return SLURM_ERROR;
		}
		list_push(*tres, tres_rec);
	}

	tres_rec->alloc_secs +=
		((uint64_t)tres_in->count * (uint64_t)elapsed);

	return SLURM_SUCCESS;
}

extern int slurmdb_sum_accounting_list(
	slurmdb_cluster_accounting_rec_t *accting,
	List *total_tres_acct)
{
	slurmdb_cluster_accounting_rec_t *total_acct = NULL;

	if (!*total_tres_acct)
		*total_tres_acct = list_create(
			slurmdb_destroy_cluster_accounting_rec);
	else
		total_acct = list_find_first(
			*total_tres_acct,
			slurmdb_find_cluster_accting_tres_in_list,
			&accting->tres_rec.id);

	if (!total_acct) {
		total_acct = xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));
		total_acct->tres_rec.id = accting->tres_rec.id;
		list_push(*total_tres_acct, total_acct);
	}

	total_acct->alloc_secs += accting->alloc_secs;
	total_acct->down_secs  += accting->down_secs;
	total_acct->idle_secs  += accting->idle_secs;
	total_acct->plan_secs  += accting->plan_secs;
	total_acct->over_secs  += accting->over_secs;
	total_acct->pdown_secs += accting->pdown_secs;
	total_acct->tres_rec.count += accting->tres_rec.count;
	total_acct->tres_rec.rec_count++;

	return SLURM_SUCCESS;
}

extern void slurmdb_transfer_acct_list_2_tres(
	List accounting_list, List *tres)
{
	ListIterator itr;
	slurmdb_accounting_rec_t *accting = NULL;

	xassert(accounting_list);
	xassert(tres);

	/* get the amount of time this assoc used
	   during the time we are looking at */
	itr = list_iterator_create(accounting_list);
	while ((accting = list_next(itr)))
		slurmdb_add_accounting_to_tres_list(accting, tres);
	list_iterator_destroy(itr);
}

extern void slurmdb_transfer_tres_time(
	List *tres_list_out, char *tres_str, int elapsed)
{
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec = NULL;
	List job_tres_list = NULL;

	xassert(tres_list_out);

	slurmdb_tres_list_from_string(&job_tres_list, tres_str,
				      TRES_STR_FLAG_NONE);

	if (!job_tres_list)
		return;

	/* get the amount of time this assoc used
	   during the time we are looking at */
	itr = list_iterator_create(job_tres_list);
	while ((tres_rec = list_next(itr)))
		slurmdb_add_time_from_count_to_tres_list(
			tres_rec, tres_list_out, elapsed);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(job_tres_list);
}

extern int slurmdb_get_tres_base_unit(char *tres_type)
{
	int ret_unit = UNIT_NONE;
	if ((!xstrcasecmp(tres_type, "mem")) ||
	    (!xstrcasecmp(tres_type, "bb"))) {
		ret_unit = UNIT_MEGA;
	}

	return ret_unit;
}

extern char *slurmdb_ave_tres_usage(char *tres_string, int tasks)
{
	List tres_list = NULL;
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec = NULL;
	uint32_t flags = TRES_STR_FLAG_SIMPLE + TRES_STR_FLAG_REPLACE;
	char *ret_tres_str = NULL;

	if (!tres_string || (tres_string[0] == '\0'))
		return NULL;

	slurmdb_tres_list_from_string(&tres_list, tres_string, flags);
	if (!tres_list) {
		error("%s: couldn't make tres_list from \'%s\'", __func__,
		      tres_string);
		return ret_tres_str;
	}

	itr = list_iterator_create(tres_list);
	while ((tres_rec = list_next(itr)))
		tres_rec->count /= (uint64_t)tasks;
	list_iterator_destroy(itr);

	ret_tres_str = slurmdb_make_tres_string(tres_list, flags);
	FREE_NULL_LIST(tres_list);

	return ret_tres_str;
}

extern void slurmdb_destroy_rpc_obj(void *object)
{
	slurmdb_rpc_obj_t *rpc_obj = (slurmdb_rpc_obj_t *)object;

	if (!rpc_obj)
		return;

	xfree(rpc_obj);
}

extern void slurmdb_destroy_rollup_stats(void *object)
{
	slurmdb_rollup_stats_t *rollup_stats = (slurmdb_rollup_stats_t *)object;

	if (!rollup_stats)
		return;

	xfree(rollup_stats->cluster_name);
	xfree(rollup_stats);
}

extern void slurmdb_free_stats_rec_members(void *object)
{
	slurmdb_stats_rec_t *rpc_stats = (slurmdb_stats_rec_t *)object;

	if (!rpc_stats)
		return;

	slurmdb_destroy_rollup_stats(rpc_stats->dbd_rollup_stats);

	FREE_NULL_LIST(rpc_stats->rollup_stats);
	FREE_NULL_LIST(rpc_stats->rpc_list);
	FREE_NULL_LIST(rpc_stats->user_list);
}

extern void slurmdb_destroy_stats_rec(void *object)
{
	if (!object)
		return;

	slurmdb_free_stats_rec_members(object);
	xfree(object);
}

extern void slurmdb_free_slurmdb_stats_members(slurmdb_stats_t *stats)
{
	if (stats) {
		xfree(stats->tres_usage_in_ave);
		xfree(stats->tres_usage_in_max);
		xfree(stats->tres_usage_in_max_nodeid);
		xfree(stats->tres_usage_in_max_taskid);
		xfree(stats->tres_usage_in_min);
		xfree(stats->tres_usage_in_min_nodeid);
		xfree(stats->tres_usage_in_min_taskid);
		xfree(stats->tres_usage_in_tot);
		xfree(stats->tres_usage_out_ave);
		xfree(stats->tres_usage_out_max);
		xfree(stats->tres_usage_out_max_nodeid);
		xfree(stats->tres_usage_out_max_taskid);
		xfree(stats->tres_usage_out_min);
		xfree(stats->tres_usage_out_min_nodeid);
		xfree(stats->tres_usage_out_min_taskid);
		xfree(stats->tres_usage_out_tot);
	}
}

extern void slurmdb_destroy_slurmdb_stats(slurmdb_stats_t *stats)
{
	slurmdb_free_slurmdb_stats_members(stats);
	xfree(stats);
}

/*
 * Comparator for sorting jobs by submit time. 0 (unknown) submit time
 * is mapped to INFINITE
 */
extern int slurmdb_job_sort_by_submit_time(void *v1, void *v2)
{
	time_t time1 = (*(slurmdb_job_rec_t **)v1)->submit;
	time_t time2 = (*(slurmdb_job_rec_t **)v2)->submit;

	/*
	 * Sanity check submits should never be 0, but if somehow that does
	 * happen treat it as the highest number.
	 */
	time1 = time1 ? time1 : INFINITE;
	time2 = time2 ? time2 : INFINITE;

	if (time1 < time2)
		return -1;
	else if (time1 > time2)
		return 1;
	return 0;
}

extern void slurmdb_merge_grp_node_usage(bitstr_t **grp_node_bitmap1,
					 uint16_t **grp_node_job_cnt1,
					 bitstr_t *grp_node_bitmap2,
					 uint16_t *grp_node_job_cnt2)
{
	if (!grp_node_bitmap2)
		return;

	if (!grp_node_bitmap1) {
		error("%s: grp_node_bitmap1 is NULL", __func__);
		return;
	}

	if (!grp_node_job_cnt1) {
		error("%s: grp_node_job_cnt1 is NULL", __func__);
		return;
	}

	if (*grp_node_bitmap1)
		bit_or(*grp_node_bitmap1, grp_node_bitmap2);
	else
		*grp_node_bitmap1 = bit_copy(grp_node_bitmap2);

	if (!*grp_node_job_cnt1)
		*grp_node_job_cnt1 =
			xcalloc(bit_size(*grp_node_bitmap1), sizeof(uint16_t));

	for (int i = 0; next_node_bitmap(grp_node_bitmap2, &i); i++) {
		(*grp_node_job_cnt1)[i] +=
			grp_node_job_cnt2 ? grp_node_job_cnt2[i] : 1;
	}
}

extern char *slurmdb_get_job_id_str(slurmdb_job_rec_t *job)
{
	char *id = NULL;

	if (job->array_task_str) {
		xlate_array_task_str(
			&job->array_task_str,
			job->array_max_tasks, NULL);
		id = xstrdup_printf("%u_[%s]",
				    job->array_job_id,
				    job->array_task_str);
	} else if (job->array_task_id != NO_VAL)
		id = xstrdup_printf("%u_%u",
				    job->array_job_id,
				    job->array_task_id);
	else if (job->het_job_id)
		id = xstrdup_printf("%u+%u",
				    job->het_job_id,
				    job->het_job_offset);
	else
		id = xstrdup_printf("%u", job->jobid);

	return id;

}
