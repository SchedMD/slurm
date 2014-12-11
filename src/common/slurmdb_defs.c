/*****************************************************************************\
 *  slurmdb_defs.c - definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdlib.h>

#include "src/common/slurmdb_defs.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_strcasestr.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/parse_time.h"
#include "src/common/node_select.h"
#include "src/common/slurm_auth.h"
#include "src/slurmdbd/read_config.h"

#define FORMAT_STRING_SIZE 34

slurmdb_cluster_rec_t *working_cluster_rec = NULL;

static void _free_res_cond_members(slurmdb_res_cond_t *res_cond);
static void _free_res_rec_members(slurmdb_res_rec_t *res);

static void _free_assoc_rec_members(slurmdb_association_rec_t *assoc)
{
	if (assoc) {
		if (assoc->accounting_list)
			list_destroy(assoc->accounting_list);
		xfree(assoc->acct);
		xfree(assoc->cluster);
		xfree(assoc->parent_acct);
		xfree(assoc->partition);
		if (assoc->qos_list)
			list_destroy(assoc->qos_list);
		xfree(assoc->user);

		destroy_assoc_mgr_association_usage(assoc->usage);
	}
}

static void _free_clus_res_rec_members(slurmdb_clus_res_rec_t *clus_res)
{
	if (clus_res) {
		xfree(clus_res->cluster);
	}
}

static void _free_cluster_rec_members(slurmdb_cluster_rec_t *cluster)
{
	if (cluster) {
		if (cluster->accounting_list)
			list_destroy(cluster->accounting_list);
		xfree(cluster->control_host);
		xfree(cluster->dim_size);
		xfree(cluster->name);
		xfree(cluster->nodes);
		slurmdb_destroy_association_rec(cluster->root_assoc);
	}
}

static void _free_qos_rec_members(slurmdb_qos_rec_t *qos)
{
	if (qos) {
		xfree(qos->description);
		xfree(qos->name);
		FREE_NULL_BITMAP(qos->preempt_bitstr);
		if (qos->preempt_list)
			list_destroy(qos->preempt_list);
		destroy_assoc_mgr_qos_usage(qos->usage);
	}
}

static void _free_wckey_rec_members(slurmdb_wckey_rec_t *wckey)
{
	if (wckey) {
		if (wckey->accounting_list)
			list_destroy(wckey->accounting_list);
		xfree(wckey->cluster);
		xfree(wckey->name);
		xfree(wckey->user);
	}
}

static void _free_cluster_cond_members(slurmdb_cluster_cond_t *cluster_cond)
{
	if (cluster_cond) {
		if (cluster_cond->cluster_list)
			list_destroy(cluster_cond->cluster_list);
	}
}

static void _free_res_cond_members(slurmdb_res_cond_t *res_cond)
{
	if (res_cond) {
		FREE_NULL_LIST(res_cond->cluster_list);
		FREE_NULL_LIST(res_cond->description_list);
		FREE_NULL_LIST(res_cond->id_list);
		FREE_NULL_LIST(res_cond->manager_list);
		FREE_NULL_LIST(res_cond->name_list);
		FREE_NULL_LIST(res_cond->percent_list);
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

	/* Since all these assocations are on the same level we don't
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
	diff = strcmp(assoc_a->sort_name, assoc_b->sort_name);

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
	slurmdb_association_rec_t *assoc_a;
	slurmdb_association_rec_t *assoc_b;

	assoc_a = *(slurmdb_association_rec_t **)v1;
	assoc_b = *(slurmdb_association_rec_t **)v2;

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

static int _setup_cluster_rec(slurmdb_cluster_rec_t *cluster_rec)
{
	int plugin_id_select = 0;

	xassert(cluster_rec);

	if (!cluster_rec->control_port) {
		debug("Slurmctld on '%s' hasn't registered yet.",
		      cluster_rec->name);
		return SLURM_ERROR;
	}

	if (cluster_rec->rpc_version < 8) {
		debug("Slurmctld on '%s' must be running at least "
		      "SLURM 2.2 for cross-cluster communication.",
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
	if (cluster_rec->control_addr.sin_port == 0) {
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

static uint32_t _str_2_qos_flags(char *flags)
{
	if (slurm_strcasestr(flags, "DenyOnLimit"))
		return QOS_FLAG_DENY_LIMIT;

	if (slurm_strcasestr(flags, "EnforceUsageThreshold"))
		return QOS_FLAG_ENFORCE_USAGE_THRES;

	if (slurm_strcasestr(flags, "PartitionMinNodes"))
		return QOS_FLAG_PART_MIN_NODE;

	if (slurm_strcasestr(flags, "PartitionMaxNodes"))
		return QOS_FLAG_PART_MAX_NODE;

	if (slurm_strcasestr(flags, "PartitionTimeLimit"))
		return QOS_FLAG_PART_TIME_LIMIT;

	if (slurm_strcasestr(flags, "RequiresReservation"))
		return QOS_FLAG_REQ_RESV;

	if (slurm_strcasestr(flags, "NoReserve"))
		return QOS_FLAG_NO_RESERVE;

	return 0;
}

static uint32_t _str_2_res_flags(char *flags)
{
	return 0;
}

extern slurmdb_job_rec_t *slurmdb_create_job_rec()
{
	slurmdb_job_rec_t *job = xmalloc(sizeof(slurmdb_job_rec_t));
	memset(&job->stats, 0, sizeof(slurmdb_stats_t));
	job->array_task_id = NO_VAL;
	job->derived_ec = NO_VAL;
	job->stats.cpu_min = NO_VAL;
	job->state = JOB_PENDING;
	job->steps = list_create(slurmdb_destroy_step_rec);
	job->requid = -1;
	job->lft = (uint32_t)NO_VAL;
	job->resvid = (uint32_t)NO_VAL;

      	return job;
}

extern slurmdb_step_rec_t *slurmdb_create_step_rec()
{
	slurmdb_step_rec_t *step = xmalloc(sizeof(slurmdb_step_rec_t));
	memset(&step->stats, 0, sizeof(slurmdb_stats_t));
	step->stepid = (uint32_t)NO_VAL;
	step->state = NO_VAL;
	step->exitcode = NO_VAL;
	step->ncpus = (uint32_t)NO_VAL;
	step->elapsed = (uint32_t)NO_VAL;
	step->tot_cpu_sec = (uint32_t)NO_VAL;
	step->tot_cpu_usec = (uint32_t)NO_VAL;
	step->requid = -1;

	return step;
}

extern void slurmdb_destroy_user_rec(void *object)
{
	slurmdb_user_rec_t *slurmdb_user = (slurmdb_user_rec_t *)object;

	if (slurmdb_user) {
		if (slurmdb_user->assoc_list)
			list_destroy(slurmdb_user->assoc_list);
		if (slurmdb_user->coord_accts)
			list_destroy(slurmdb_user->coord_accts);
		xfree(slurmdb_user->default_acct);
		xfree(slurmdb_user->default_wckey);
		xfree(slurmdb_user->name);
		xfree(slurmdb_user->old_name);
		if (slurmdb_user->wckey_list)
			list_destroy(slurmdb_user->wckey_list);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_rec(void *object)
{
	slurmdb_account_rec_t *slurmdb_account =
		(slurmdb_account_rec_t *)object;

	if (slurmdb_account) {
		if (slurmdb_account->assoc_list)
			list_destroy(slurmdb_account->assoc_list);
		if (slurmdb_account->coordinators)
			list_destroy(slurmdb_account->coordinators);
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

extern void slurmdb_destroy_accounting_rec(void *object)
{
	slurmdb_accounting_rec_t *slurmdb_accounting =
		(slurmdb_accounting_rec_t *)object;

	if (slurmdb_accounting) {
		xfree(slurmdb_accounting);
	}
}

extern void slurmdb_destroy_association_rec(void *object)
{
	slurmdb_association_rec_t *slurmdb_association =
		(slurmdb_association_rec_t *)object;

	if (slurmdb_association) {
		_free_assoc_rec_members(slurmdb_association);
		xfree(slurmdb_association);
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

		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_rec(void *object)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	if (job) {
		xfree(job->account);
		xfree(job->alloc_gres);
		xfree(job->array_task_str);
		xfree(job->blockid);
		xfree(job->cluster);
		xfree(job->derived_es);
		xfree(job->jobname);
		xfree(job->partition);
		xfree(job->nodes);
		xfree(job->req_gres);
		xfree(job->resv_name);
		if (job->steps) {
			list_destroy(job->steps);
			job->steps = NULL;
		}
		xfree(job->user);
		xfree(job->wckey);
		xfree(job);
	}
}

extern void slurmdb_destroy_qos_rec(void *object)
{
	slurmdb_qos_rec_t *slurmdb_qos = (slurmdb_qos_rec_t *)object;
	if (slurmdb_qos) {
		_free_qos_rec_members(slurmdb_qos);
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
		xfree(slurmdb_resv->name);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv->node_inx);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_step_rec(void *object)
{
	slurmdb_step_rec_t *step = (slurmdb_step_rec_t *)object;
	if (step) {
		xfree(step->nodes);
		xfree(step->pid_str);
		xfree(step->stepname);
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

extern void slurmdb_destroy_report_assoc_rec(void *object)
{
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc =
		(slurmdb_report_assoc_rec_t *)object;
	if (slurmdb_report_assoc) {
		xfree(slurmdb_report_assoc->acct);
		xfree(slurmdb_report_assoc->cluster);
		xfree(slurmdb_report_assoc->parent_acct);
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
		if (slurmdb_report_user->acct_list)
			list_destroy(slurmdb_report_user->acct_list);
		if (slurmdb_report_user->assoc_list)
			list_destroy(slurmdb_report_user->assoc_list);
		xfree(slurmdb_report_user->name);
		xfree(slurmdb_report_user);
	}
}

extern void slurmdb_destroy_report_cluster_rec(void *object)
{
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster =
		(slurmdb_report_cluster_rec_t *)object;
	if (slurmdb_report_cluster) {
		if (slurmdb_report_cluster->assoc_list)
			list_destroy(slurmdb_report_cluster->assoc_list);
		xfree(slurmdb_report_cluster->name);
		if (slurmdb_report_cluster->user_list)
			list_destroy(slurmdb_report_cluster->user_list);
		xfree(slurmdb_report_cluster);
	}
}

extern void slurmdb_destroy_user_cond(void *object)
{
	slurmdb_user_cond_t *slurmdb_user = (slurmdb_user_cond_t *)object;

	if (slurmdb_user) {
		slurmdb_destroy_association_cond(slurmdb_user->assoc_cond);
		if (slurmdb_user->def_acct_list)
			list_destroy(slurmdb_user->def_acct_list);
		if (slurmdb_user->def_wckey_list)
			list_destroy(slurmdb_user->def_wckey_list);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_cond(void *object)
{
	slurmdb_account_cond_t *slurmdb_account =
		(slurmdb_account_cond_t *)object;

	if (slurmdb_account) {
		slurmdb_destroy_association_cond(slurmdb_account->assoc_cond);
		if (slurmdb_account->description_list)
			list_destroy(slurmdb_account->description_list);
		if (slurmdb_account->organization_list)
			list_destroy(slurmdb_account->organization_list);
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

extern void slurmdb_destroy_association_cond(void *object)
{
	slurmdb_association_cond_t *slurmdb_association =
		(slurmdb_association_cond_t *)object;

	if (slurmdb_association) {
		if (slurmdb_association->acct_list)
			list_destroy(slurmdb_association->acct_list);
		if (slurmdb_association->cluster_list)
			list_destroy(slurmdb_association->cluster_list);
		if (slurmdb_association->def_qos_id_list)
			list_destroy(slurmdb_association->def_qos_id_list);

		if (slurmdb_association->fairshare_list)
			list_destroy(slurmdb_association->fairshare_list);

		if (slurmdb_association->grp_cpu_mins_list)
			list_destroy(slurmdb_association->grp_cpu_mins_list);
		if (slurmdb_association->grp_cpu_run_mins_list)
			list_destroy(slurmdb_association->
				     grp_cpu_run_mins_list);
		if (slurmdb_association->grp_cpus_list)
			list_destroy(slurmdb_association->grp_cpus_list);
		if (slurmdb_association->grp_jobs_list)
			list_destroy(slurmdb_association->grp_jobs_list);
		if (slurmdb_association->grp_mem_list)
			list_destroy(slurmdb_association->grp_mem_list);
		if (slurmdb_association->grp_nodes_list)
			list_destroy(slurmdb_association->grp_nodes_list);
		if (slurmdb_association->grp_submit_jobs_list)
			list_destroy(slurmdb_association->grp_submit_jobs_list);
		if (slurmdb_association->grp_wall_list)
			list_destroy(slurmdb_association->grp_wall_list);

		if (slurmdb_association->id_list)
			list_destroy(slurmdb_association->id_list);

		if (slurmdb_association->max_cpu_mins_pj_list)
			list_destroy(slurmdb_association->max_cpu_mins_pj_list);
		if (slurmdb_association->max_cpu_run_mins_list)
			list_destroy(slurmdb_association->
				     max_cpu_run_mins_list);
		if (slurmdb_association->max_cpus_pj_list)
			list_destroy(slurmdb_association->max_cpus_pj_list);
		if (slurmdb_association->max_jobs_list)
			list_destroy(slurmdb_association->max_jobs_list);
		if (slurmdb_association->max_nodes_pj_list)
			list_destroy(slurmdb_association->max_nodes_pj_list);
		if (slurmdb_association->max_submit_jobs_list)
			list_destroy(slurmdb_association->max_submit_jobs_list);
		if (slurmdb_association->max_wall_pj_list)
			list_destroy(slurmdb_association->max_wall_pj_list);

		if (slurmdb_association->partition_list)
			list_destroy(slurmdb_association->partition_list);

		if (slurmdb_association->parent_acct_list)
			list_destroy(slurmdb_association->parent_acct_list);

		if (slurmdb_association->qos_list)
			list_destroy(slurmdb_association->qos_list);
		if (slurmdb_association->user_list)
			list_destroy(slurmdb_association->user_list);
		xfree(slurmdb_association);
	}
}

extern void slurmdb_destroy_event_cond(void *object)
{
	slurmdb_event_cond_t *slurmdb_event =
		(slurmdb_event_cond_t *)object;

	if (slurmdb_event) {
		if (slurmdb_event->cluster_list)
			list_destroy(slurmdb_event->cluster_list);
		if (slurmdb_event->node_list)
			list_destroy(slurmdb_event->node_list);
		if (slurmdb_event->reason_list)
			list_destroy(slurmdb_event->reason_list);
		if (slurmdb_event->reason_uid_list)
			list_destroy(slurmdb_event->reason_uid_list);
		if (slurmdb_event->state_list)
			list_destroy(slurmdb_event->state_list);
		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_cond(void *object)
{
	slurmdb_job_cond_t *job_cond =
		(slurmdb_job_cond_t *)object;

	if (job_cond) {
		if (job_cond->acct_list)
			list_destroy(job_cond->acct_list);
		if (job_cond->associd_list)
			list_destroy(job_cond->associd_list);
		if (job_cond->cluster_list)
			list_destroy(job_cond->cluster_list);
		if (job_cond->groupid_list)
			list_destroy(job_cond->groupid_list);
		if (job_cond->jobname_list)
			list_destroy(job_cond->jobname_list);
		if (job_cond->partition_list)
			list_destroy(job_cond->partition_list);
		if (job_cond->qos_list)
			list_destroy(job_cond->qos_list);
		if (job_cond->resv_list)
			list_destroy(job_cond->resv_list);
		if (job_cond->resvid_list)
			list_destroy(job_cond->resvid_list);
		if (job_cond->step_list)
			list_destroy(job_cond->step_list);
		if (job_cond->state_list)
			list_destroy(job_cond->state_list);
		xfree(job_cond->used_nodes);
		if (job_cond->userid_list)
			list_destroy(job_cond->userid_list);
		if (job_cond->wckey_list)
			list_destroy(job_cond->wckey_list);
		xfree(job_cond);
	}
}

extern void slurmdb_destroy_job_modify_cond(void *object)
{
	slurmdb_job_modify_cond_t *job_cond =
		(slurmdb_job_modify_cond_t *)object;

	if (job_cond) {
		xfree(job_cond->cluster);
		xfree(job_cond);
	}
}

extern void slurmdb_destroy_qos_cond(void *object)
{
	slurmdb_qos_cond_t *slurmdb_qos = (slurmdb_qos_cond_t *)object;
	if (slurmdb_qos) {
		if (slurmdb_qos->id_list)
			list_destroy(slurmdb_qos->id_list);
		if (slurmdb_qos->name_list)
			list_destroy(slurmdb_qos->name_list);
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
		if (slurmdb_resv->cluster_list)
			list_destroy(slurmdb_resv->cluster_list);
		if (slurmdb_resv->id_list)
			list_destroy(slurmdb_resv->id_list);
		if (slurmdb_resv->name_list)
			list_destroy(slurmdb_resv->name_list);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_txn_cond(void *object)
{
	slurmdb_txn_cond_t *slurmdb_txn = (slurmdb_txn_cond_t *)object;
	if (slurmdb_txn) {
		if (slurmdb_txn->acct_list)
			list_destroy(slurmdb_txn->acct_list);
		if (slurmdb_txn->action_list)
			list_destroy(slurmdb_txn->action_list);
		if (slurmdb_txn->actor_list)
			list_destroy(slurmdb_txn->actor_list);
		if (slurmdb_txn->cluster_list)
			list_destroy(slurmdb_txn->cluster_list);
		if (slurmdb_txn->id_list)
			list_destroy(slurmdb_txn->id_list);
		if (slurmdb_txn->info_list)
			list_destroy(slurmdb_txn->info_list);
		if (slurmdb_txn->name_list)
			list_destroy(slurmdb_txn->name_list);
		if (slurmdb_txn->user_list)
			list_destroy(slurmdb_txn->user_list);
		xfree(slurmdb_txn);
	}
}

extern void slurmdb_destroy_wckey_cond(void *object)
{
	slurmdb_wckey_cond_t *wckey = (slurmdb_wckey_cond_t *)object;

	if (wckey) {
		if (wckey->cluster_list)
			list_destroy(wckey->cluster_list);
		if (wckey->id_list)
			list_destroy(wckey->id_list);
		if (wckey->name_list)
			list_destroy(wckey->name_list);
		if (wckey->user_list)
			list_destroy(wckey->user_list);
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
		if (slurmdb_update->objects)
			list_destroy(slurmdb_update->objects);

		xfree(slurmdb_update);
	}
}

extern void slurmdb_destroy_used_limits(void *object)
{
	slurmdb_used_limits_t *slurmdb_used_limits =
		(slurmdb_used_limits_t *)object;

	if (slurmdb_used_limits) {
		xfree(slurmdb_used_limits);
	}
}

extern void slurmdb_destroy_update_shares_rec(void *object)
{
	xfree(object);
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
		if (slurmdb_hierarchical_rec->children) {
			list_destroy(slurmdb_hierarchical_rec->children);
		}
		xfree(slurmdb_hierarchical_rec);
	}
}

extern void slurmdb_destroy_selected_step(void *object)
{
	slurmdb_selected_step_t *step = (slurmdb_selected_step_t *)object;
	if (step) {
		xfree(step);
	}
}

extern void slurmdb_destroy_report_job_grouping(void *object)
{
	slurmdb_report_job_grouping_t *job_grouping =
		(slurmdb_report_job_grouping_t *)object;
	if (job_grouping) {
		if (job_grouping->jobs)
			list_destroy(job_grouping->jobs);
		xfree(job_grouping);
	}
}

extern void slurmdb_destroy_report_acct_grouping(void *object)
{
	slurmdb_report_acct_grouping_t *acct_grouping =
		(slurmdb_report_acct_grouping_t *)object;
	if (acct_grouping) {
		xfree(acct_grouping->acct);
		if (acct_grouping->groups)
			list_destroy(acct_grouping->groups);
		xfree(acct_grouping);
	}
}

extern void slurmdb_destroy_report_cluster_grouping(void *object)
{
	slurmdb_report_cluster_grouping_t *cluster_grouping =
		(slurmdb_report_cluster_grouping_t *)object;
	if (cluster_grouping) {
		xfree(cluster_grouping->cluster);
		if (cluster_grouping->acct_list)
			list_destroy(cluster_grouping->acct_list);
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

	if (cluster_names && !strcmp(cluster_names, "all"))
		all_clusters = 1;

	cluster_name = slurm_get_cluster_name();
	db_conn = acct_storage_g_get_connection(NULL, 0, 1, cluster_name);
	xfree(cluster_name);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	if (cluster_names && !all_clusters) {
		cluster_cond.cluster_list = list_create(slurm_destroy_char);
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
			if (_setup_cluster_rec(cluster_rec) != SLURM_SUCCESS) {
				list_delete_item(itr);
			}
		}
	} else {
		itr2 = list_iterator_create(cluster_cond.cluster_list);
		while ((cluster_name = list_next(itr2))) {
			while ((cluster_rec = list_next(itr))) {
				if (!strcmp(cluster_name, cluster_rec->name))
					break;
			}
			if (!cluster_rec) {
				error("No cluster '%s' known by database.",
				      cluster_name);
				goto next;
			}

			if (_setup_cluster_rec(cluster_rec) != SLURM_SUCCESS) {
				list_delete_item(itr);
			}
		next:
			list_iterator_reset(itr);
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);

end_it:
	if (cluster_cond.cluster_list)
		list_destroy(cluster_cond.cluster_list);
	acct_storage_g_close_connection(&db_conn);

	if (temp_list && !list_count(temp_list)) {
		list_destroy(temp_list);
		temp_list = NULL;
	}

	return temp_list;
}

extern void slurmdb_init_association_rec(slurmdb_association_rec_t *assoc,
					 bool free_it)
{
	if (!assoc)
		return;

	if (free_it)
		_free_assoc_rec_members(assoc);
	memset(assoc, 0, sizeof(slurmdb_association_rec_t));

	assoc->def_qos_id = NO_VAL;
	assoc->is_def = (uint16_t)NO_VAL;

	assoc->grp_cpu_mins = (uint64_t)NO_VAL;
	assoc->grp_cpu_run_mins = (uint64_t)NO_VAL;
	assoc->grp_cpus = NO_VAL;
	assoc->grp_jobs = NO_VAL;
	assoc->grp_mem = NO_VAL;
	assoc->grp_nodes = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	assoc->lft = NO_VAL;
	assoc->rgt = NO_VAL;
	/* assoc->level_shares = NO_VAL; */

	assoc->max_cpu_mins_pj = (uint64_t)NO_VAL;
	assoc->max_cpu_run_mins = (uint64_t)NO_VAL;
	assoc->max_cpus_pj = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_pj = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;

	/* assoc->shares_norm = (double)NO_VAL; */
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
	clus_res->percent_allowed = (uint16_t)NO_VAL;
}

extern void slurmdb_init_cluster_rec(slurmdb_cluster_rec_t *cluster,
				     bool free_it)
{
	if (!cluster)
		return;

	if (free_it)
		_free_cluster_rec_members(cluster);
	memset(cluster, 0, sizeof(slurmdb_cluster_rec_t));
	cluster->flags = NO_VAL;
}

extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos, bool free_it)
{
	if (!qos)
		return;

	if (free_it)
		_free_qos_rec_members(qos);
	memset(qos, 0, sizeof(slurmdb_qos_rec_t));

	qos->flags = QOS_FLAG_NOTSET;

	qos->grace_time = NO_VAL;
	qos->preempt_mode = (uint16_t)NO_VAL;
	qos->priority = NO_VAL;

	qos->grp_cpu_mins = (uint64_t)NO_VAL;
	qos->grp_cpu_run_mins = (uint64_t)NO_VAL;
	qos->grp_cpus = NO_VAL;
	qos->grp_jobs = NO_VAL;
	qos->grp_mem = NO_VAL;
	qos->grp_nodes = NO_VAL;
	qos->grp_submit_jobs = NO_VAL;
	qos->grp_wall = NO_VAL;

	qos->max_cpu_mins_pj = (uint64_t)NO_VAL;
	qos->max_cpu_run_mins_pu = (uint64_t)NO_VAL;
	qos->max_cpus_pj = NO_VAL;
	qos->max_cpus_pu = NO_VAL;
	qos->max_jobs_pu = NO_VAL;
	qos->max_nodes_pj = NO_VAL;
	qos->max_nodes_pu = NO_VAL;
	qos->max_submit_jobs_pu = NO_VAL;
	qos->max_wall_pj = NO_VAL;

	qos->min_cpus_pj = NO_VAL;

	qos->usage_factor = (double)NO_VAL;
	qos->usage_thres = (double)NO_VAL;
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
	res->percent_used = (uint16_t)NO_VAL;
	res->type = SLURMDB_RESOURCE_NOTSET;
}

extern void slurmdb_init_wckey_rec(slurmdb_wckey_rec_t *wckey, bool free_it)
{
	if (!wckey)
		return;

	if (free_it)
		_free_wckey_rec_members(wckey);
	memset(wckey, 0, sizeof(slurmdb_wckey_rec_t));
	wckey->is_def = (uint16_t)NO_VAL;
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
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;

	if (!qos_list) {
		error("We need a qos list to translate");
		return NULL;
	} else if (!level) {
		debug2("no level");
		return "";
	}

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (level == qos->id)
			break;
	}
	list_iterator_destroy(itr);
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
		if (!strcasecmp(working_level, qos->name))
			break;
	}
	list_iterator_destroy(itr);
	if (qos)
		return qos->id;
	else
		return NO_VAL;
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
	if (flags & QOS_FLAG_PART_TIME_LIMIT)
		xstrcat(qos_flags, "PartitionTimeLimit,");
	if (flags & QOS_FLAG_REQ_RESV)
		xstrcat(qos_flags, "RequiresReservation,");

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
	default:
		return "Unknown";
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
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level)
{
	if (!level) {
		return SLURMDB_ADMIN_NOTSET;
	} else if (!strncasecmp(level, "None", 1)) {
		return SLURMDB_ADMIN_NONE;
	} else if (!strncasecmp(level, "Operator", 1)) {
		return SLURMDB_ADMIN_OPERATOR;
	} else if (!strncasecmp(level, "SuperUser", 1)
		   || !strncasecmp(level, "Admin", 1)) {
		return SLURMDB_ADMIN_SUPER_USER;
	} else {
		return SLURMDB_ADMIN_NOTSET;
	}
}

/* This reorders the list into a alphabetical hierarchy returned in a
 * separate list.  The orginal list is not affected */
extern List slurmdb_get_hierarchical_sorted_assoc_list(List assoc_list)
{
	List slurmdb_hierarchical_rec_list =
		slurmdb_get_acct_hierarchical_rec_list(assoc_list);
	List ret_list = list_create(NULL);

	_append_hierarchical_children_ret_list(ret_list,
					       slurmdb_hierarchical_rec_list);
	list_destroy(slurmdb_hierarchical_rec_list);

	return ret_list;
}

/* This reorders the list into a alphabetical hierarchy. */
extern void slurmdb_sort_hierarchical_assoc_list(List assoc_list)
{
	List slurmdb_hierarchical_rec_list =
		slurmdb_get_acct_hierarchical_rec_list(assoc_list);
	/* Clear all the pointers out of the list without freeing the
	   memory since we will just add them back in later.
	*/
	while(list_pop(assoc_list)) {
	}

	_append_hierarchical_children_ret_list(assoc_list,
					       slurmdb_hierarchical_rec_list);
	list_destroy(slurmdb_hierarchical_rec_list);
}

extern List slurmdb_get_acct_hierarchical_rec_list(List assoc_list)
{
	slurmdb_hierarchical_rec_t *par_arch_rec = NULL;
	slurmdb_hierarchical_rec_t *last_acct_parent = NULL;
	slurmdb_hierarchical_rec_t *last_parent = NULL;
	slurmdb_hierarchical_rec_t *arch_rec = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	List total_assoc_list = list_create(NULL);
	List arch_rec_list =
		list_create(slurmdb_destroy_hierarchical_rec);
	ListIterator itr, itr2;

	/* The list should already be sorted by lfts, do it anyway
	 * just to make sure it is correct. */
	list_sort(assoc_list, (ListCmpF)_sort_assoc_by_lft_dec);
	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(total_assoc_list);

	while((assoc = list_next(itr))) {
		arch_rec = xmalloc(sizeof(slurmdb_hierarchical_rec_t));
		arch_rec->children =
			list_create(slurmdb_destroy_hierarchical_rec);
		arch_rec->assoc = assoc;

		/* To speed things up we are first looking if we have
		   a parent_id to look for.  If that doesn't work see
		   if the last parent we had was what we are looking
		   for.  Then if that isn't panning out look at the
		   last account parent.  If still we don't have it we
		   will look for it in the list.  If it isn't there we
		   will just add it to the parent and call it good
		*/
		if (!assoc->parent_id) {
			arch_rec->sort_name = assoc->cluster;

			list_append(arch_rec_list, arch_rec);
			list_append(total_assoc_list, arch_rec);

			continue;
		}

		if (assoc->user)
			arch_rec->sort_name = assoc->user;
		else
			arch_rec->sort_name = assoc->acct;

		if (last_parent && assoc->parent_id == last_parent->assoc->id
		    && !strcmp(assoc->cluster, last_parent->assoc->cluster)) {
			par_arch_rec = last_parent;
		} else if (last_acct_parent
			   && (assoc->parent_id == last_acct_parent->assoc->id)
			   && !strcmp(assoc->cluster,
				      last_acct_parent->assoc->cluster)) {
			par_arch_rec = last_acct_parent;
		} else {
			list_iterator_reset(itr2);
			while((par_arch_rec = list_next(itr2))) {
				if (assoc->parent_id == par_arch_rec->assoc->id
				    && !strcmp(assoc->cluster,
					       par_arch_rec->assoc->cluster)) {
					if (assoc->user)
						last_parent = par_arch_rec;
					else
						last_parent
							= last_acct_parent
							= par_arch_rec;
					break;
				}
			}
		}

		if (!par_arch_rec) {
			list_append(arch_rec_list, arch_rec);
			last_parent = last_acct_parent = arch_rec;
		} else
			list_append(par_arch_rec->children, arch_rec);

		list_append(total_assoc_list, arch_rec);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	list_destroy(total_assoc_list);
//	info("got %d", list_count(arch_rec_list));
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

		if (!strcmp(name, slurmdb_print_tree->name))
			break;
		else if (parent && !strcmp(parent, slurmdb_print_tree->name))
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
	bitoff_t bit = 0;
	int rc = SLURM_SUCCESS;
	char *temp_char = NULL;
	void (*my_function) (bitstr_t *b, bitoff_t bit);

	xassert(valid_qos);

	if (!qos_list)
		return SLURM_ERROR;

	itr = list_iterator_create(qos_list);
	while((temp_char = list_next(itr))) {
		if (temp_char[0] == '-') {
			temp_char++;
			my_function = bit_clear;
		} else if (temp_char[0] == '+') {
			temp_char++;
			my_function = bit_set;
		} else
			my_function = bit_set;
		bit = atoi(temp_char);
		if (bit >= bit_size(valid_qos)) {
			rc = SLURM_ERROR;
			break;
		}
		(*(my_function))(valid_qos, bit);
	}
	list_iterator_destroy(itr);

	return rc;
}

extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
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
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if (print_this)
			xstrfmtcat(print_this, ",%s", temp_char);
		else
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

	if (!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_qos_complete_str(List qos_list, List num_qos_list)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
	int option = 0;

	if (!qos_list || !list_count(qos_list)
	    || !num_qos_list || !list_count(num_qos_list))
		return xstrdup("");

	temp_list = list_create(slurm_destroy_char);

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
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if (print_this)
			xstrfmtcat(print_this, ",%s", temp_char);
		else
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

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

	if (slurm_strcasestr(class, "capac"))
		type = SLURMDB_CLASS_CAPACITY;
	else if (slurm_strcasestr(class, "capab"))
		type = SLURMDB_CLASS_CAPABILITY;
	else if (slurm_strcasestr(class, "capap"))
		type = SLURMDB_CLASS_CAPAPACITY;

	if (slurm_strcasestr(class, "*"))
		type |= SLURMDB_CLASSIFIED_FLAG;
	else if (slurm_strcasestr(class, "class"))
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

	if (slurm_strcasestr(problem, "account no associations"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if (slurm_strcasestr(problem, "account no users"))
		type = SLURMDB_PROBLEM_ACCT_NO_USERS;
	else if (slurm_strcasestr(problem, "user no associations"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if (slurm_strcasestr(problem, "user no uid"))
		type = SLURMDB_PROBLEM_USER_NO_UID;

	return type;
}

extern void log_assoc_rec(slurmdb_association_rec_t *assoc_ptr,
			  List qos_list)
{
	xassert(assoc_ptr);

	debug2("association rec id : %u", assoc_ptr->id);
	debug2("  acct             : %s", assoc_ptr->acct);
	debug2("  cluster          : %s", assoc_ptr->cluster);

	if (assoc_ptr->shares_raw == INFINITE)
		debug2("  RawShares        : NONE");
	else if (assoc_ptr->shares_raw != NO_VAL)
		debug2("  RawShares        : %u", assoc_ptr->shares_raw);

	if (assoc_ptr->def_qos_id)
		debug2("  Default QOS      : %s",
		       slurmdb_qos_str(qos_list, assoc_ptr->def_qos_id));
	else
		debug2("  Default QOS      : NONE");

	if (assoc_ptr->grp_cpu_mins == INFINITE)
		debug2("  GrpCPUMins       : NONE");
	else if (assoc_ptr->grp_cpu_mins != NO_VAL)
		debug2("  GrpCPUMins       : %"PRIu64"",
		       assoc_ptr->grp_cpu_mins);

	if (assoc_ptr->grp_cpu_run_mins == INFINITE)
		debug2("  GrpCPURunMins    : NONE");
	else if (assoc_ptr->grp_cpu_run_mins != NO_VAL)
		debug2("  GrpCPURunMins    : %"PRIu64"",
		       assoc_ptr->grp_cpu_run_mins);

	if (assoc_ptr->grp_cpus == INFINITE)
		debug2("  GrpCPUs          : NONE");
	else if (assoc_ptr->grp_cpus != NO_VAL)
		debug2("  GrpCPUs          : %u", assoc_ptr->grp_cpus);

	if (assoc_ptr->grp_jobs == INFINITE)
		debug2("  GrpJobs          : NONE");
	else if (assoc_ptr->grp_jobs != NO_VAL)
		debug2("  GrpJobs          : %u", assoc_ptr->grp_jobs);

	if (assoc_ptr->grp_mem == INFINITE)
		debug2("  GrpMemory        : NONE");
	else if (assoc_ptr->grp_mem != NO_VAL)
		debug2("  GrpMemory        : %u", assoc_ptr->grp_mem);

	if (assoc_ptr->grp_nodes == INFINITE)
		debug2("  GrpNodes         : NONE");
	else if (assoc_ptr->grp_nodes != NO_VAL)
		debug2("  GrpNodes         : %u", assoc_ptr->grp_nodes);

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

	if (assoc_ptr->max_cpu_mins_pj == INFINITE)
		debug2("  MaxCPUMins       : NONE");
	else if (assoc_ptr->max_cpu_mins_pj != NO_VAL)
		debug2("  MaxCPUMins       : %"PRIu64"",
		       assoc_ptr->max_cpu_mins_pj);

	if (assoc_ptr->max_cpu_run_mins == INFINITE)
		debug2("  MaxCPURunMins    : NONE");
	else if (assoc_ptr->max_cpu_run_mins != NO_VAL)
		debug2("  MaxCPURunMins    : %"PRIu64"",
		       assoc_ptr->max_cpu_run_mins);

	if (assoc_ptr->max_cpus_pj == INFINITE)
		debug2("  MaxCPUs          : NONE");
	else if (assoc_ptr->max_cpus_pj != NO_VAL)
		debug2("  MaxCPUs          : %u", assoc_ptr->max_cpus_pj);

	if (assoc_ptr->max_jobs == INFINITE)
		debug2("  MaxJobs          : NONE");
	else if (assoc_ptr->max_jobs != NO_VAL)
		debug2("  MaxJobs          : %u", assoc_ptr->max_jobs);

	if (assoc_ptr->max_nodes_pj == INFINITE)
		debug2("  MaxNodes         : NONE");
	else if (assoc_ptr->max_nodes_pj != NO_VAL)
		debug2("  MaxNodes         : %u", assoc_ptr->max_nodes_pj);

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
		//(*end) = mktime(&end_tm);
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
	end_tm.tm_isdst = -1;
	(*end) = mktime(&end_tm);

	if (!sent_start) {
		if (!localtime_r(&my_time, &start_tm)) {
			error("Couldn't get localtime from start %ld",
			      (long)my_time);
			return SLURM_ERROR;
		}
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		//(*start) = mktime(&start_tm);
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
	start_tm.tm_isdst = -1;
	(*start) = mktime(&start_tm);

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
 *   <integer>H
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
		if (!len || !strncasecmp("months", string+i, MAX(len, 1))) {
			purge |= SLURMDB_PURGE_MONTHS;
		} else if (!strncasecmp("hours", string+i, MAX(len, 1))) {
			purge |= SLURMDB_PURGE_HOURS;
		} else if (!strncasecmp("days", string+i, MAX(len, 1))) {
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

extern int slurmdb_addto_qos_char_list(List char_list, List qos_list,
				       char *names, int option)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	uint32_t id=0;
	int count = 0;
	int equal_set = 0;
	int add_set = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	if (!qos_list || !list_count(qos_list)) {
		debug2("No real qos_list");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					int tmp_option = option;
					if (names[start] == '+'
					    || names[start] == '-') {
						tmp_option = names[start];
						start++;
					}
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					id = str_2_slurmdb_qos(qos_list, name);
					if (id == NO_VAL) {
						char *tmp = _get_qos_list_str(
							qos_list);
						error("You gave a bad qos "
						      "'%s'.  Valid QOS's are "
						      "%s",
						      name, tmp);
						xfree(tmp);
						xfree(name);
						break;
					}
					xfree(name);

					if (tmp_option) {
						if (equal_set) {
							error("You can't set "
							      "qos equal to "
							      "something and "
							      "then add or "
							      "subtract from "
							      "it in the same "
							      "line");
							break;
						}
						add_set = 1;
						name = xstrdup_printf(
							"%c%u", tmp_option, id);
					} else {
						if (add_set) {
							error("You can't set "
							      "qos equal to "
							      "something and "
							      "then add or "
							      "subtract from "
							      "it in the same "
							      "line");
							break;
						}
						equal_set = 1;
						name = xstrdup_printf("%u", id);
					}
					while((tmp_char = list_next(itr))) {
						if (!strcasecmp(tmp_char, name))
							break;
					}
					list_iterator_reset(itr);

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
				} else if (!(i-start)) {
					list_append(char_list, xstrdup(""));
					count++;
				}

				i++;
				start = i;
				if (!names[i]) {
					error("There is a problem with "
					      "your request.  It appears you "
					      "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			int tmp_option = option;
			if (names[start] == '+' || names[start] == '-') {
				tmp_option = names[start];
				start++;
			}
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));

			id = str_2_slurmdb_qos(qos_list, name);
			if (id == NO_VAL) {
				char *tmp = _get_qos_list_str(qos_list);
				error("You gave a bad qos "
				      "'%s'.  Valid QOS's are "
				      "%s",
				      name, tmp);
				xfree(tmp);
				xfree(name);
				goto end_it;
			}
			xfree(name);

			if (tmp_option) {
				if (equal_set) {
					error("You can't set "
					      "qos equal to "
					      "something and "
					      "then add or "
					      "subtract from "
					      "it in the same "
					      "line");
					goto end_it;
				}
				name = xstrdup_printf(
					"%c%u", tmp_option, id);
			} else {
				if (add_set) {
					error("You can't set "
					      "qos equal to "
					      "something and "
					      "then add or "
					      "subtract from "
					      "it in the same "
					      "line");
					goto end_it;
				}
				name = xstrdup_printf("%u", id);
			}
			while((tmp_char = list_next(itr))) {
				if (!strcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		} else if (!(i-start)) {
			list_append(char_list, xstrdup(""));
			count++;
		}
	}
	if (!count) {
		error("You gave me an empty qos list");
	}

end_it:
	list_iterator_destroy(itr);
	return count;
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
	slurm_set_addr_char(&req.address, port, host);

	/* We standarized on SLURM_PROTOCOL_VERSION in 14.03 in 15.03
	   this check can go away as well as the rpc_version of the
	   accounting_update_msg_t.
	*/
	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION)
		req.protocol_version = rpc_version;

	req.msg_type = ACCOUNTING_UPDATE_MSG;
	if (slurmdbd_conf)
		req.flags = SLURM_GLOBAL_AUTH_KEY;
	req.data = &msg;
	slurm_msg_t_init(&resp);

	for (i = 0; i < 4; i++) {
		/* Retry if the slurmctld can connect, but is not responding */
		rc = slurm_send_recv_node_msg(&req, &resp, 0);
		if ((rc == 0) || (errno != SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT))
			break;
	}
	if ((rc != 0) || ! resp.auth_cred) {
		error("update cluster: %m to %s at %s(%hu)",
		      cluster, host, port);
		rc = SLURM_ERROR;
	}
	if (resp.auth_cred)
		g_slurm_auth_destroy(resp.auth_cred);

	switch (resp.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *)resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
		break;
	default:
		if (rc != SLURM_ERROR)
			error("Unknown response message %u", resp.msg_type);
		rc = SLURM_ERROR;
		break;
	}
	//info("got rc of %d", rc);
	return rc;
}

extern slurmdb_report_cluster_rec_t *slurmdb_cluster_rec_2_report(
	slurmdb_cluster_rec_t *cluster)
{
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster;
	slurmdb_cluster_accounting_rec_t *accting = NULL;
	ListIterator cluster_itr = NULL;
	int count;

	xassert(cluster);
	slurmdb_report_cluster = xmalloc(sizeof(slurmdb_report_cluster_rec_t));
	slurmdb_report_cluster->name = xstrdup(cluster->name);

	if (!(count = list_count(cluster->accounting_list)))
		return slurmdb_report_cluster;

	/* get the amount of time and the average cpu count
	   during the time we are looking at */
	cluster_itr = list_iterator_create(cluster->accounting_list);
	while((accting = list_next(cluster_itr))) {
		slurmdb_report_cluster->cpu_secs += accting->alloc_secs
			+ accting->down_secs + accting->idle_secs
			+ accting->resv_secs + accting->pdown_secs;
		slurmdb_report_cluster->cpu_count += accting->cpu_count;
		slurmdb_report_cluster->consumed_energy += accting->consumed_energy;
	}
	list_iterator_destroy(cluster_itr);

	slurmdb_report_cluster->cpu_count /= count;

	return slurmdb_report_cluster;
}

extern char *slurmdb_get_selected_step_id(
	char *job_id_str, int len,
	slurmdb_selected_step_t *selected_step)
{
	char id[FORMAT_STRING_SIZE];

	xassert(selected_step);

	if (selected_step->array_task_id != NO_VAL)
		snprintf(id, FORMAT_STRING_SIZE,
			 "%u_%u",
			 selected_step->jobid,
			 selected_step->array_task_id);
	else
		snprintf(id, FORMAT_STRING_SIZE,
			 "%u",
			 selected_step->jobid);

	if (selected_step->stepid != NO_VAL)
		snprintf(job_id_str, len, "%s.%u",
			 id, selected_step->stepid);
	else
		snprintf(job_id_str, len, "%s", id);

	return job_id_str;
}
