/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/cpu_frequency.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/power.h"
#include "src/common/prep.h"
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_topology.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_route.h"
#include "src/common/strnatcmp.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/trigger_mgr.h"

#define FEATURE_MAGIC	0x34dfd8b5

/* Global variables */
List active_feature_list;	/* list of currently active features_records */
List avail_feature_list;	/* list of available features_records */
bool node_features_updated = true;
bool slurmctld_init_db = true;

static void _acct_restore_active_jobs(void);
static void _add_config_feature(List feature_list, char *feature,
				bitstr_t *node_bitmap);
static void _add_config_feature_inx(List feature_list, char *feature,
				    int node_inx);
static void _build_bitmaps(void);
static void _build_bitmaps_pre_select(void);
static int  _compare_hostnames(node_record_t **old_node_table,
			       int old_node_count, node_record_t **node_table,
			       int node_count);
static void _gres_reconfig(bool reconfig);
static void _init_all_slurm_conf(void);
static void _list_delete_feature(void *feature_entry);
static int _preserve_select_type_param(slurm_conf_t *ctl_conf_ptr,
                                       uint16_t old_select_type_p);
static void _purge_old_node_state(node_record_t **old_node_table_ptr,
				  int old_node_record_count);
static void _purge_old_part_state(List old_part_list, char *old_def_part_name);
static int  _reset_node_bitmaps(void *x, void *arg);
static void _restore_job_accounting();

static int  _restore_node_state(int recover, node_record_t **old_node_table_ptr,
				int old_node_record_count);
static int  _restore_part_state(List old_part_list, char *old_def_part_name,
				uint16_t flags);
static void _set_features(node_record_t **old_node_table_ptr,
			  int old_node_record_count, int recover);
static void _stat_slurm_dirs(void);
static int  _sync_nodes_to_comp_job(void);
static int  _sync_nodes_to_jobs(bool reconfig);
static int  _sync_nodes_to_active_job(job_record_t *job_ptr);
static void _sync_nodes_to_suspended_job(job_record_t *job_ptr);
static void _sync_part_prio(void);
static void _update_preempt(uint16_t old_enable_preempt);


/*
 * Setup the global response_cluster_rec
 */
static void _set_response_cluster_rec(void)
{
	if (response_cluster_rec)
		return;

	response_cluster_rec = xmalloc(sizeof(slurmdb_cluster_rec_t));
	response_cluster_rec->name = xstrdup(slurm_conf.cluster_name);
	if (slurm_conf.slurmctld_addr) {
		response_cluster_rec->control_host =
			xstrdup(slurm_conf.slurmctld_addr);
	} else {
		response_cluster_rec->control_host =
			xstrdup(slurm_conf.control_addr[0]);
	}
	response_cluster_rec->control_port = slurm_conf.slurmctld_port;
	response_cluster_rec->rpc_version = SLURM_PROTOCOL_VERSION;
	response_cluster_rec->plugin_id_select = select_get_plugin_id();
}

/*
 * Free the global response_cluster_rec
 */
extern void cluster_rec_free(void)
{
	if (response_cluster_rec) {
		xfree(response_cluster_rec->control_host);
		xfree(response_cluster_rec->name);
		xfree(response_cluster_rec);
	}
}

/* Verify that Slurm directories are secure, not world writable */
static void _stat_slurm_dirs(void)
{
	struct stat stat_buf;
	char *problem_dir = NULL;

	/*
	 * PluginDir may have multiple values, and is checked by
	 * _is_valid_path() instead
	 */

	if (slurm_conf.plugstack &&
	    !stat(slurm_conf.plugstack, &stat_buf) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "PlugStack";
	}
	if (!stat(slurm_conf.slurmd_spooldir, &stat_buf) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "SlurmdSpoolDir";
	}
	if (!stat(slurm_conf.state_save_location, &stat_buf) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "StateSaveLocation";
	}

	if (problem_dir) {
		error("################################################");
		error("###       SEVERE SECURITY VULERABILTY        ###");
		error("### %s DIRECTORY IS WORLD WRITABLE ###", problem_dir);
		error("###         CORRECT FILE PERMISSIONS         ###");
		error("################################################");
	}
}

/*
 * _reorder_nodes_by_rank - order node table in ascending order of node_rank
 * This depends on the TopologyPlugin, which may generate such a ranking.
 */
static int _sort_nodes_by_rank(const void *a, const void *b)
{
	node_record_t *n1 = *(node_record_t **)a;
	node_record_t *n2 = *(node_record_t **)b;

	if (!n1)
		return 1;
	if (!n2)
		return -1;

	return (n1->node_rank - n2->node_rank);
}

/*
 * _reorder_nodes_by_name - order node table in ascending order of name
 */
static int _sort_nodes_by_name(const void *a, const void *b)
{
	node_record_t *n1 = *(node_record_t **)a;
	node_record_t *n2 = *(node_record_t **)b;

	if (!n1)
		return 1;
	if (!n2)
		return -1;

	return strnatcmp(n1->name, n2->name);
}

static void _sort_node_record_table_ptr(void)
{
	int (*compare_fn)(const void *, const void *);

	if (slurm_topo_generate_node_ranking())
		compare_fn = &_sort_nodes_by_rank;
	else
		compare_fn = &_sort_nodes_by_name;

	qsort(node_record_table_ptr, node_record_count,
	      sizeof(node_record_t *), compare_fn);

	for (int i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i])
			node_record_table_ptr[i]->index = i;
	}

#if _DEBUG
	/* Log the results */
	node_record_t *node_ptr;
	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		info("node_rank[%d:%d]: %s",
		     node_ptr->index, node_ptr->node_rank, node_ptr->name);
	}
#endif
}

static void _add_nodes_with_feature(hostlist_t hl, char *feature)
{
	if (avail_feature_list) {
		char *feature_nodes;
		node_feature_t *node_feat_ptr;
		if (!(node_feat_ptr = list_find_first(avail_feature_list,
						      list_find_feature,
						      feature))) {
			debug2("unable to find nodeset feature '%s'", feature);
			return;
		}
		feature_nodes = bitmap2node_name(node_feat_ptr->node_bitmap);
		hostlist_push(hl, feature_nodes);
		xfree(feature_nodes);
	} else {
		node_record_t *node_ptr;
		/*
		 * The global feature bitmaps have not been set up at this
		 * point, so we'll have to scan through the node_record_table
		 * directly to locate the appropriate records.
		 */
		for (int i = 0; (node_ptr = next_node(&i)); i++) {
			char *features, *tmp, *tok, *last = NULL;

			if (!node_ptr->features)
				continue;

			features = tmp = xstrdup(node_ptr->features);

			while ((tok = strtok_r(tmp, ",", &last))) {
				if (!xstrcmp(tok, feature)) {
					hostlist_push_host(
						hl, node_ptr->name);
					break;
				}
				tmp = NULL;
			}
			xfree(features);
		}
	}
}

extern hostlist_t nodespec_to_hostlist(const char *nodes, char **nodesets)
{
	int count;
	slurm_conf_nodeset_t *ptr, **ptr_array;
	hostlist_t hl;
	node_record_t *node_ptr;

	if (nodesets)
		xfree(*nodesets);

	if (!xstrcasecmp(nodes, "ALL")) {
		if (!(hl = hostlist_create(NULL))) {
			error("%s: hostlist_create() error for %s", __func__, nodes);
			return NULL;
		}
		for (int i = 0; (node_ptr = next_node(&i)); i++)
			hostlist_push_host(hl, node_ptr->name);
		return hl;
	} else if (!(hl = hostlist_create(nodes))) {
		error("%s: hostlist_create() error for %s", __func__, nodes);
		return NULL;
	}

	if (!hostlist_count(hl)) {
		/* no need to look for nodests */
		return hl;
	}

	count = slurm_conf_nodeset_array(&ptr_array);
	for (int i = 0; i < count; i++) {
		ptr = ptr_array[i];

		/* swap the nodeset entry with the applicable nodes */
		if (hostlist_delete_host(hl, ptr->name)) {
			if (nodesets)
				xstrfmtcat(*nodesets, "%s%s",
					   *nodesets ? "," : "",
					   ptr->name);

			if (ptr->feature)
				_add_nodes_with_feature(hl, ptr->feature);

			if (ptr->nodes)
				hostlist_push(hl, ptr->nodes);
		}
	}

	hostlist_uniq(hl);
	return hl;
}

static void _init_bitmaps(void)
{
	/* initialize the idle and up bitmaps */
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(bf_ignore_node_bitmap);
	FREE_NULL_BITMAP(booting_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(future_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	FREE_NULL_BITMAP(rs_node_bitmap);
	avail_node_bitmap = bit_alloc(node_record_count);
	bf_ignore_node_bitmap = bit_alloc(node_record_count);
	booting_node_bitmap = bit_alloc(node_record_count);
	cg_node_bitmap = bit_alloc(node_record_count);
	future_node_bitmap = bit_alloc(node_record_count);
	idle_node_bitmap = bit_alloc(node_record_count);
	power_node_bitmap = bit_alloc(node_record_count);
	share_node_bitmap = bit_alloc(node_record_count);
	up_node_bitmap = bit_alloc(node_record_count);
	rs_node_bitmap = bit_alloc(node_record_count);
}

/*
 * _build_bitmaps_pre_select - recover some state for jobs and nodes prior to
 *	calling the select_* functions
 */
static void _build_bitmaps_pre_select(void)
{
	part_record_t *part_ptr;
	node_record_t *node_ptr;
	ListIterator part_iterator;

	/* scan partition table and identify nodes in each */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = list_next(part_iterator))) {
		if (build_part_bitmap(part_ptr) == ESLURM_INVALID_NODE_NAME)
			fatal("Invalid node names in partition %s",
					part_ptr->name);
	}
	list_iterator_destroy(part_iterator);

	/* initialize the configuration bitmaps */
	list_for_each(config_list, _reset_node_bitmaps, NULL);

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->config_ptr)
			bit_set(node_ptr->config_ptr->node_bitmap,
				node_ptr->index);
	}

	return;
}

static int _reset_node_bitmaps(void *x, void *arg)
{
	config_record_t *config_ptr = (config_record_t *) x;

	FREE_NULL_BITMAP(config_ptr->node_bitmap);
	config_ptr->node_bitmap = (bitstr_t *) bit_alloc(node_record_count);

	return 0;
}

static int _set_share_node_bitmap(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;

	if (!IS_JOB_RUNNING(job_ptr) ||
	    (job_ptr->node_bitmap == NULL)        ||
	    (job_ptr->details     == NULL)        ||
	    (job_ptr->details->share_res != 0))
		return 0;

	bit_and_not(share_node_bitmap, job_ptr->node_bitmap);

	return 0;
}

/*
 * Validate that nodes are addressable.
 */
static void _validate_slurmd_addr(void)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	slurm_addr_t slurm_addr;
	DEF_TIMERS;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	START_TIMER;
	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERING_DOWN(node_ptr) ||
		     IS_NODE_POWERED_DOWN(node_ptr)))
				continue;
		if (node_ptr->port == 0)
			node_ptr->port = slurm_conf.slurmd_port;
		slurm_set_addr(&slurm_addr, node_ptr->port,
			       node_ptr->comm_name);
		if (slurm_get_port(&slurm_addr))
			continue;
		error("%s: failure on %s", __func__, node_ptr->comm_name);
		node_ptr->node_state = NODE_STATE_FUTURE;
		node_ptr->port = 0;
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup("NO NETWORK ADDRESS FOUND");
		node_ptr->reason_time = time(NULL);
		node_ptr->reason_uid = slurm_conf.slurm_user_id;
	}

	END_TIMER2("_validate_slurmd_addr");
#endif
}

/*
 * _build_bitmaps - build node bitmaps to define which nodes are in which
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * RET 0 if no error, errno otherwise
 * Note: Operates on common variables, no arguments
 *	node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	part_list - pointer to global partition list
 */
static void _build_bitmaps(void)
{
	node_record_t *node_ptr;

	last_node_update = time(NULL);
	last_part_update = time(NULL);

	/* Set all bits, all nodes initially available for sharing */
	bit_set_all(share_node_bitmap);

	/* identify all nodes non-sharable due to non-sharing jobs */
	list_for_each(job_list, _set_share_node_bitmap, NULL);

	/* scan all nodes and identify which are up, idle and
	 * their configuration, resync DRAINED vs. DRAINING state */
	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		uint32_t drain_flag, job_cnt;

		if (node_ptr->name[0] == '\0')
			continue;	/* defunct */
		drain_flag = IS_NODE_DRAIN(node_ptr) |
			     IS_NODE_FAIL(node_ptr);
		job_cnt = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;

		if ((IS_NODE_IDLE(node_ptr) && (job_cnt == 0)) ||
		    IS_NODE_DOWN(node_ptr))
			bit_set(idle_node_bitmap, node_ptr->index);
		if (IS_NODE_POWERING_UP(node_ptr))
			bit_set(booting_node_bitmap, node_ptr->index);
		if (IS_NODE_COMPLETING(node_ptr))
			bit_set(cg_node_bitmap, node_ptr->index);
		if (IS_NODE_IDLE(node_ptr) ||
		    IS_NODE_ALLOCATED(node_ptr) ||
		    ((IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		      IS_NODE_REBOOT_ISSUED(node_ptr)) &&
		     ((node_ptr->next_state & NODE_STATE_FLAGS) &
		      NODE_RESUME))) {
			if ((drain_flag == 0) &&
			    (!IS_NODE_NO_RESPOND(node_ptr)))
				make_node_avail(node_ptr);
			bit_set(up_node_bitmap, node_ptr->index);
		}
		if (IS_NODE_POWERED_DOWN(node_ptr))
			bit_set(power_node_bitmap, node_ptr->index);
		if (IS_NODE_POWERING_DOWN(node_ptr)) {
			bit_set(power_node_bitmap, node_ptr->index);
			bit_clear(avail_node_bitmap, node_ptr->index);
		}
		if (IS_NODE_FUTURE(node_ptr))
			bit_set(future_node_bitmap, node_ptr->index);

		if ((IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		     IS_NODE_REBOOT_ISSUED(node_ptr)) &&
		    ((node_ptr->next_state & NODE_STATE_FLAGS) & NODE_RESUME))
			bit_set(rs_node_bitmap, node_ptr->index);
	}
}


/*
 * _init_all_slurm_conf - initialize or re-initialize the slurm
 *	configuration values.
 * NOTE: We leave the job table intact
 * NOTE: Operates on common variables, no arguments
 */
static void _init_all_slurm_conf(void)
{
	char *conf_name = xstrdup(slurm_conf.slurm_conf);

	slurm_conf_reinit(conf_name);
	xfree(conf_name);

	init_node_conf();
	init_part_conf();
	init_job_conf();
}

static int _handle_downnodes_line(slurm_conf_downnodes_t *down)
{
	int error_code = 0;
	node_record_t *node_rec = NULL;
	hostlist_t alias_list = NULL;
	char *alias = NULL;
	int state_val = NODE_STATE_DOWN;

	if (down->state != NULL) {
		state_val = state_str2int(down->state, down->nodenames);
		if (state_val == NO_VAL) {
			error("Invalid State \"%s\"", down->state);
			goto cleanup;
		}
	}

	if ((alias_list = hostlist_create(down->nodenames)) == NULL) {
		error("Unable to create NodeName list from %s",
		      down->nodenames);
		error_code = errno;
		goto cleanup;
	}

	while ((alias = hostlist_shift(alias_list))) {
		node_rec = find_node_record(alias);
		if (node_rec == NULL) {
			error("DownNode \"%s\" does not exist!", alias);
			free(alias);
			continue;
		}

		if ((state_val != NO_VAL) &&
		    (state_val != NODE_STATE_UNKNOWN))
			node_rec->node_state = state_val;
		if (down->reason) {
			xfree(node_rec->reason);
			node_rec->reason = xstrdup(down->reason);
			node_rec->reason_time = time(NULL);
			node_rec->reason_uid = slurm_conf.slurm_user_id;
		}
		free(alias);
	}

cleanup:
	if (alias_list)
		hostlist_destroy(alias_list);
	return error_code;
}

static void _handle_all_downnodes(void)
{
	slurm_conf_downnodes_t *ptr, **ptr_array;
	int count;
	int i;

	count = slurm_conf_downnodes_array(&ptr_array);
	if (count == 0) {
		debug("No DownNodes");
		return;
	}

	for (i = 0; i < count; i++) {
		ptr = ptr_array[i];

		_handle_downnodes_line(ptr);
	}
}

/* Convert a comma delimited list of account names into a NULL terminated
 * array of pointers to strings. Call accounts_list_free() to release memory */
extern void accounts_list_build(char *accounts, char ***accounts_array)
{
	char *tmp_accts, *one_acct_name, *name_ptr = NULL, **tmp_array = NULL;
	int array_len = 0, array_used = 0;

	if (!accounts) {
		accounts_list_free(accounts_array);
		*accounts_array = NULL;
		return;
	}

	tmp_accts = xstrdup(accounts);
	one_acct_name = strtok_r(tmp_accts, ",", &name_ptr);
	while (one_acct_name) {
		if (array_len < array_used + 2) {
			array_len += 10;
			xrealloc(tmp_array, sizeof(char *) * array_len);
		}
		tmp_array[array_used++] = xstrdup(one_acct_name);
		one_acct_name = strtok_r(NULL, ",", &name_ptr);
	}
	xfree(tmp_accts);
	accounts_list_free(accounts_array);
	*accounts_array = tmp_array;
}
/* Free memory allocated for an account array by accounts_list_build() */
extern void accounts_list_free(char ***accounts_array)
{
	int i;

	if (*accounts_array == NULL)
		return;
	for (i = 0; accounts_array[0][i]; i++)
		xfree(accounts_array[0][i]);
	xfree(*accounts_array);
}

/* Convert a comma delimited list of QOS names into a bitmap */
extern void qos_list_build(char *qos, bitstr_t **qos_bits)
{
	char *tmp_qos, *one_qos_name, *name_ptr = NULL;
	slurmdb_qos_rec_t qos_rec, *qos_ptr = NULL;
	bitstr_t *tmp_qos_bitstr;
	int rc;
	assoc_mgr_lock_t locks = { .qos = READ_LOCK };

	if (!qos) {
		FREE_NULL_BITMAP(*qos_bits);
		return;
	}

	/* Lock here to avoid g_qos_count changing under us */
	assoc_mgr_lock(&locks);
	if (!g_qos_count) {
		error("We have no QOS on the system Ignoring invalid "
		      "Allow/DenyQOS value(s) %s",
		      qos);
		assoc_mgr_unlock(&locks);
		FREE_NULL_BITMAP(*qos_bits);
		*qos_bits = NULL;
		return;
	}

	tmp_qos_bitstr = bit_alloc(g_qos_count);
	tmp_qos = xstrdup(qos);
	one_qos_name = strtok_r(tmp_qos, ",", &name_ptr);
	while (one_qos_name) {
		memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
		qos_rec.name = one_qos_name;
		rc = assoc_mgr_fill_in_qos(acct_db_conn, &qos_rec,
					   accounting_enforce,
					   &qos_ptr, 1);
		if ((rc != SLURM_SUCCESS) || (qos_rec.id >= g_qos_count)) {
			error("Ignoring invalid Allow/DenyQOS value: %s",
			      one_qos_name);
		} else {
			bit_set(tmp_qos_bitstr, qos_rec.id);
		}
		one_qos_name = strtok_r(NULL, ",", &name_ptr);
	}
	assoc_mgr_unlock(&locks);
	xfree(tmp_qos);
	FREE_NULL_BITMAP(*qos_bits);
	*qos_bits = tmp_qos_bitstr;
}

/*
 * _build_single_partitionline_info - get a array of slurm_conf_partition_t
 *	structures from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
static int _build_single_partitionline_info(slurm_conf_partition_t *part)
{
	part_record_t *part_ptr;

	if (list_find_first(part_list, &list_find_part, part->name))
		fatal("%s: duplicate entry for partition %s",
		      __func__, part->name);

	part_ptr = create_part_record(part->name);

	if (part->default_flag) {
		if (default_part_name &&
		    xstrcmp(default_part_name, part->name)) {
			info("_parse_part_spec: changing default partition "
			     "from %s to %s", default_part_name, part->name);
			default_part_loc->flags &= (~PART_FLAG_DEFAULT);
		}
		xfree(default_part_name);
		default_part_name = xstrdup(part->name);
		default_part_loc = part_ptr;
		part_ptr->flags |= PART_FLAG_DEFAULT;
	}

	part_ptr->cpu_bind = part->cpu_bind;

	if (part->preempt_mode != NO_VAL16)
		part_ptr->preempt_mode = part->preempt_mode;

	if (part->disable_root_jobs == NO_VAL8) {
		if (slurm_conf.conf_flags & CTL_CONF_DRJ)
			part_ptr->flags |= PART_FLAG_NO_ROOT;
	} else if (part->disable_root_jobs) {
		part_ptr->flags |= PART_FLAG_NO_ROOT;
	} else {
		part_ptr->flags &= (~PART_FLAG_NO_ROOT);
	}
	if (part_ptr->flags & PART_FLAG_NO_ROOT)
		debug2("partition %s does not allow root jobs", part_ptr->name);

	if ((part->default_time != NO_VAL) &&
	    (part->default_time > part->max_time)) {
		info("partition %s DefaultTime exceeds MaxTime (%u > %u)",
		     part->name, part->default_time, part->max_time);
		part->default_time = NO_VAL;
	}

	if (part->exclusive_user)
		part_ptr->flags |= PART_FLAG_EXCLUSIVE_USER;
	if (part->hidden_flag)
		part_ptr->flags |= PART_FLAG_HIDDEN;
	if (part->root_only_flag)
		part_ptr->flags |= PART_FLAG_ROOT_ONLY;
	if (part->req_resv_flag)
		part_ptr->flags |= PART_FLAG_REQ_RESV;
	if (part->lln_flag)
		part_ptr->flags |= PART_FLAG_LLN;
	part_ptr->max_time       = part->max_time;
	part_ptr->def_mem_per_cpu = part->def_mem_per_cpu;
	part_ptr->default_time   = part->default_time;
	FREE_NULL_LIST(part_ptr->job_defaults_list);
	part_ptr->job_defaults_list =
		job_defaults_copy(part->job_defaults_list);
	part_ptr->max_cpus_per_node = part->max_cpus_per_node;
	part_ptr->max_share      = part->max_share;
	part_ptr->max_mem_per_cpu = part->max_mem_per_cpu;
	part_ptr->max_nodes      = part->max_nodes;
	part_ptr->max_nodes_orig = part->max_nodes;
	part_ptr->min_nodes      = part->min_nodes;
	part_ptr->min_nodes_orig = part->min_nodes;
	part_ptr->over_time_limit = part->over_time_limit;
	part_ptr->preempt_mode   = part->preempt_mode;
	part_ptr->priority_job_factor = part->priority_job_factor;
	part_ptr->priority_tier  = part->priority_tier;
	part_ptr->qos_char       = xstrdup(part->qos_char);
	part_ptr->resume_timeout = part->resume_timeout;
	part_ptr->state_up       = part->state_up;
	part_ptr->suspend_time   = part->suspend_time;
	part_ptr->suspend_timeout = part->suspend_timeout;
	part_ptr->grace_time     = part->grace_time;
	part_ptr->cr_type        = part->cr_type;

	part_ptr->allow_alloc_nodes = xstrdup(part->allow_alloc_nodes);
	part_ptr->allow_groups = xstrdup(part->allow_groups);
	part_ptr->alternate = xstrdup(part->alternate);
	part_ptr->nodes = xstrdup(part->nodes);
	part_ptr->orig_nodes = xstrdup(part->nodes);

	if (part->billing_weights_str) {
		set_partition_billing_weights(part->billing_weights_str,
					      part_ptr, true);
	}

	if (part->allow_accounts) {
		part_ptr->allow_accounts = xstrdup(part->allow_accounts);
		accounts_list_build(part_ptr->allow_accounts,
				    &part_ptr->allow_account_array);
	}

	if (part->allow_qos) {
		part_ptr->allow_qos = xstrdup(part->allow_qos);
		qos_list_build(part_ptr->allow_qos,&part_ptr->allow_qos_bitstr);
	}

	if (part->deny_accounts) {
		part_ptr->deny_accounts = xstrdup(part->deny_accounts);
		accounts_list_build(part_ptr->deny_accounts,
				    &part_ptr->deny_account_array);
	}

	if (part->deny_qos) {
		part_ptr->deny_qos = xstrdup(part->deny_qos);
		qos_list_build(part_ptr->deny_qos, &part_ptr->deny_qos_bitstr);
	}

	if (part->qos_char) {
		slurmdb_qos_rec_t qos_rec;
		part_ptr->qos_char = xstrdup(part->qos_char);

		memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
		qos_rec.name = part_ptr->qos_char;
		if (assoc_mgr_fill_in_qos(
			    acct_db_conn, &qos_rec, accounting_enforce,
			    (slurmdb_qos_rec_t **)&part_ptr->qos_ptr, 0)
		    != SLURM_SUCCESS) {
			fatal("Partition %s has an invalid qos (%s), "
			      "please check your configuration",
			      part_ptr->name, qos_rec.name);
		}
	}

	return 0;
}

/*
 * _build_all_partitionline_info - get a array of slurm_conf_partition_t
 *	structures from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
static int _build_all_partitionline_info(void)
{
	slurm_conf_partition_t **ptr_array;
	int count;
	int i;

	count = slurm_conf_partition_array(&ptr_array);
	if (count == 0)
		fatal("No PartitionName information available!");

	for (i = 0; i < count; i++)
		_build_single_partitionline_info(ptr_array[i]);

	return SLURM_SUCCESS;
}

static int _set_max_part_prio(void *x, void *arg)
{
	part_record_t *part_ptr = (part_record_t *) x;

	if (part_ptr->priority_job_factor > part_max_priority)
		part_max_priority = part_ptr->priority_job_factor;

	return 0;
}

static int _reset_part_prio(void *x, void *arg)
{
	part_record_t *part_ptr = (part_record_t *) x;

	/* protect against div0 if all partition priorities are zero */
	if (part_max_priority == 0) {
		part_ptr->norm_priority = 0;
		return 0;
	}

	part_ptr->norm_priority = (double)part_ptr->priority_job_factor /
				  (double)part_max_priority;

	return 0;
}

/* _sync_part_prio - Set normalized partition priorities */
static void _sync_part_prio(void)
{
	/* reset global value from part list */
	part_max_priority = DEF_PART_MAX_PRIORITY;
	list_for_each(part_list, _set_max_part_prio, NULL);
	/* renormalize values after finding new max */
	list_for_each(part_list, _reset_part_prio, NULL);
}

static int _foreach_requeue_job_node_failed(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	job_record_t *het_job_leader;
	int rc = SLURM_SUCCESS;

	xassert(job_ptr->magic == JOB_MAGIC);

	if (!IS_JOB_NODE_FAILED(job_ptr) && !IS_JOB_REQUEUED(job_ptr))
		return SLURM_SUCCESS;

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (het_job_leader && het_job_leader->batch_flag &&
	    het_job_leader->details &&
	    het_job_leader->details->requeue &&
	    het_job_leader->part_ptr) {
		info("Requeue het job leader %pJ due to node failure on %pJ",
		     het_job_leader, job_ptr);
		if ((rc = job_requeue(0, het_job_leader->job_id, NULL, false,
				      0)))
			error("Unable to requeue %pJ: %s",
			      het_job_leader, slurm_strerror(rc));
	} else if (job_ptr->batch_flag && job_ptr->details &&
		   job_ptr->details->requeue && job_ptr->part_ptr) {
		info("Requeue job %pJ due to node failure",
		     job_ptr);
		if ((rc = job_requeue(0, job_ptr->job_id, NULL, false, 0)))
			error("Unable to requeue %pJ: %s",
			      job_ptr, slurm_strerror(rc));
	}

	job_ptr->job_state &= (~JOB_REQUEUE);

	return rc;
}

extern void _requeue_job_node_failed(void)
{
	xassert(job_list);

	(void) list_for_each_nobreak(job_list,
				     _foreach_requeue_job_node_failed, NULL);
}

static void _abort_job(job_record_t *job_ptr, uint32_t job_state,
		       uint16_t state_reason, char *reason_string)
{
	time_t now = time(NULL);

	job_ptr->job_state = job_state | JOB_COMPLETING;
	build_cg_bitmap(job_ptr);
	job_ptr->end_time = MIN(job_ptr->end_time, now);
	job_ptr->state_reason = state_reason;
	xfree(job_ptr->state_desc);
	job_ptr->state_desc = xstrdup(reason_string);
	job_completion_logger(job_ptr, false);
	if (job_ptr->job_state == JOB_NODE_FAIL) {
		/* build_cg_bitmap() may clear JOB_COMPLETING */
		epilog_slurmctld(job_ptr);
	}
}

static int _mark_het_job_unused(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_ptr->bit_flags &= (~HET_JOB_FLAG);
	return 0;
}

static int _mark_het_job_used(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_ptr->bit_flags |= HET_JOB_FLAG;
	return 0;
}

static int _test_het_job_used(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;

	if ((job_ptr->het_job_id == 0) || IS_JOB_FINISHED(job_ptr))
		return 0;
	if (job_ptr->bit_flags & HET_JOB_FLAG)
		return 0;

	error("Incomplete hetjob being aborted %pJ", job_ptr);
	_abort_job(job_ptr, JOB_FAILED, FAIL_SYSTEM, "incomplete hetjob");

	return 0;
}

/*
 * Validate heterogeneous jobs
 *
 * Make sure that every active (not yet complete) job has all of its components
 * and they are all in the same state. Also rebuild het_job_list.
 * If hetjob is corrupted, aborts and removes it from job_list.
 */
static void _validate_het_jobs(void)
{
	ListIterator job_iterator;
	job_record_t *job_ptr, *het_job_ptr;
	hostset_t hs;
	char *job_id_str;
	uint32_t job_id;
	bool het_job_valid;

	list_for_each(job_list, _mark_het_job_unused, NULL);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		/* Checking for corrupted hetjob components */
		if (job_ptr->het_job_offset != 0) {
			het_job_ptr = find_job_record(job_ptr->het_job_id);
			if (!het_job_ptr) {
				error("Could not find hetjob leader (JobId=%u) of %pJ. Aborting and removing job as it is corrupted.",
				      job_ptr->het_job_id, job_ptr);
				_abort_job(job_ptr, JOB_FAILED, FAIL_SYSTEM,
					   "invalid het_job_id_set");
				if (list_delete_item(job_iterator) != 1)
					error("Not able to remove the job.");
				continue;
			}
		}

		if ((job_ptr->het_job_id == 0) ||
		    (job_ptr->het_job_offset != 0))
			continue;
		/* active het job leader found */
		FREE_NULL_LIST(job_ptr->het_job_list);
		job_id_str = NULL;
		/* Need to wrap numbers with brackets for hostset functions */
		xstrfmtcat(job_id_str, "[%s]", job_ptr->het_job_id_set);
		hs = hostset_create(job_id_str);
		xfree(job_id_str);
		if (!hs) {
			error("%pJ has invalid het_job_id_set(%s). Aborting and removing job as it is corrupted.",
			      job_ptr, job_ptr->het_job_id_set);
			_abort_job(job_ptr, JOB_FAILED, FAIL_SYSTEM,
				   "invalid het_job_id_set");
			if (list_delete_item(job_iterator) != 1)
				error("Not able to remove the job.");
			continue;
		}
		job_ptr->het_job_list = list_create(NULL);
		het_job_valid = true;	/* assume valid for now */
		while (het_job_valid && (job_id_str = hostset_shift(hs))) {
			job_id = (uint32_t) strtoll(job_id_str, NULL, 10);
			het_job_ptr = find_job_record(job_id);
			if (!het_job_ptr) {
				error("Could not find JobId=%u, part of hetjob JobId=%u",
				      job_id, job_ptr->job_id);
				het_job_valid = false;
			} else if (het_job_ptr->het_job_id !=
				   job_ptr->job_id) {
				error("Invalid state of JobId=%u, part of hetjob JobId=%u",
				      job_id, job_ptr->job_id);
				het_job_valid = false;
			} else {
				list_append(job_ptr->het_job_list,
					    het_job_ptr);
			}
			free(job_id_str);
		}
		hostset_destroy(hs);
		if (het_job_valid) {
			list_for_each(job_ptr->het_job_list, _mark_het_job_used,
				      NULL);
		}
	}
	list_iterator_destroy(job_iterator);

	list_for_each(job_list, _test_het_job_used, NULL);
}

/* Log an error if SlurmdUser is not root and any cgroup plugin is used */
static void _test_cgroup_plugin_use(void)
{
	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		error("task/cgroup plugin will not work unless SlurmdUser is root");

	if (xstrstr(slurm_conf.proctrack_type, "cgroup"))
		error("proctrack/cgroup plugin will not work unless SlurmdUser is root");
}


static void _sync_steps_to_conf(job_record_t *job_ptr)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state < JOB_RUNNING)
			continue;
		FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
		if (step_ptr->step_layout &&
		    step_ptr->step_layout->node_list &&
		    (node_name2bitmap(step_ptr->step_layout->node_list, false,
				      &step_ptr->step_node_bitmap))) {
			error("Invalid step_node_list (%s) for %pS",
			      step_ptr->step_layout->node_list, step_ptr);
			delete_step_record(job_ptr, step_ptr);
		} else if (step_ptr->step_node_bitmap == NULL) {
			error("Missing node_list for %pS", step_ptr);
			delete_step_record(job_ptr, step_ptr);
		}
	}

	list_iterator_destroy (step_iterator);
	return;
}

static int _sync_detail_bitmaps(job_record_t *job_ptr)
{
	if (job_ptr->details == NULL)
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);

	if ((job_ptr->details->req_nodes) &&
	    (node_name2bitmap(job_ptr->details->req_nodes, false,
			      &job_ptr->details->req_node_bitmap))) {
		error("Invalid req_nodes (%s) for %pJ",
		      job_ptr->details->req_nodes, job_ptr);
		return SLURM_ERROR;
	}

	FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	if ((job_ptr->details->exc_nodes) &&
	    (node_name2bitmap(job_ptr->details->exc_nodes, true,
			      &job_ptr->details->exc_node_bitmap))) {
		error("Invalid exc_nodes (%s) for %pJ",
		      job_ptr->details->exc_nodes, job_ptr);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * _sync_jobs_to_conf - Sync current slurm.conf configuration for existing jobs.
 *	This should be called after rebuilding node, part, and gres information,
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
void _sync_jobs_to_conf(void)
{
	ListIterator job_iterator;
	job_record_t *job_ptr;
	part_record_t *part_ptr;
	List part_ptr_list = NULL;
	bool job_fail = false;
	time_t now = time(NULL);
	bool gang_flag = false;
	static uint32_t cr_flag = NO_VAL;

	xassert(job_list);

	if (cr_flag == NO_VAL) {
		cr_flag = 0;  /* call is no-op for select/linear and others */
		if (select_g_get_info_from_plugin(SELECT_CR_PLUGIN,
						  NULL, &cr_flag)) {
			cr_flag = NO_VAL;	/* error */
		}

	}
	if (slurm_conf.preempt_mode & PREEMPT_MODE_GANG)
		gang_flag = true;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		job_fail = false;

		/*
		 * This resets the req/exc node bitmaps, so even if the job is
		 * finished it still needs to happen just in case the job is
		 * requeued.
		 */
		if (_sync_detail_bitmaps(job_ptr)) {
			job_fail = true;
			if (job_ptr->details) {
				/*
				 * job can't be requeued because either
				 * req_nodes or exc_nodes can't be satisfied.
				 */
				job_ptr->details->requeue = false;
			}
		}

		/*
		 * While the job is completed at this point there is code in
		 * _job_requeue_op() that requires the part_ptr to be set in
		 * order to requeue a job.  We also need to set it to NULL if
		 * the partition was removed or we will be pointing at bad
		 * data.  This is the safest/easiest place to do it.
		 */

		if (job_ptr->partition == NULL) {
			error("No partition for %pJ", job_ptr);
			part_ptr = NULL;
			job_fail = true;
		} else {
			char *err_part = NULL;
			part_ptr = find_part_record(job_ptr->partition);
			if (part_ptr == NULL) {
				part_ptr_list = get_part_list(
					job_ptr->partition,
					&err_part);
				if (part_ptr_list) {
					part_ptr = list_peek(part_ptr_list);
					if (list_count(part_ptr_list) == 1)
						FREE_NULL_LIST(part_ptr_list);
				}
			}
			if (part_ptr == NULL) {
				error("Invalid partition (%s) for %pJ",
				      err_part, job_ptr);
				xfree(err_part);
				job_fail = true;
			}
		}
		job_ptr->part_ptr = part_ptr;
		FREE_NULL_LIST(job_ptr->part_ptr_list);
		if (part_ptr_list) {
			job_ptr->part_ptr_list = part_ptr_list;
			part_ptr_list = NULL;	/* clear for next job */
		}

		/*
		 * If the job is finished there is no reason to do anything
		 * below this.
		 */
		if (IS_JOB_COMPLETED(job_ptr))
			continue;

		FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
		if (job_ptr->nodes_completing &&
		    node_name2bitmap(job_ptr->nodes_completing,
				     false,  &job_ptr->node_bitmap_cg)) {
			error("Invalid nodes (%s) for %pJ",
			      job_ptr->nodes_completing, job_ptr);
			job_fail = true;
		}
		FREE_NULL_BITMAP(job_ptr->node_bitmap);
		if (job_ptr->nodes &&
		    node_name2bitmap(job_ptr->nodes, false,
				     &job_ptr->node_bitmap) && !job_fail) {
			error("Invalid nodes (%s) for %pJ",
			      job_ptr->nodes, job_ptr);
			job_fail = true;
		}
		FREE_NULL_BITMAP(job_ptr->node_bitmap_pr);
		if (job_ptr->nodes_pr &&
		    node_name2bitmap(job_ptr->nodes_pr,
				     false,  &job_ptr->node_bitmap_pr)) {
			error("Invalid nodes (%s) for %pJ",
			      job_ptr->nodes_pr, job_ptr);
			job_fail = true;
		}
		if (reset_node_bitmap(job_ptr))
			job_fail = true;
		if (!job_fail &&
		    job_ptr->job_resrcs && (cr_flag || gang_flag) &&
		    valid_job_resources(job_ptr->job_resrcs,
					node_record_table_ptr)) {
			error("Aborting %pJ due to change in socket/core configuration of allocated nodes",
			      job_ptr);
			job_fail = true;
		}
		if (!job_fail &&
		    gres_job_revalidate(job_ptr->gres_list_req)) {
			error("Aborting %pJ due to use of unsupported GRES options",
			      job_ptr);
			job_fail = true;
			if (job_ptr->details) {
				/* don't attempt to requeue job */
				job_ptr->details->requeue = false;
			}
		}

		if (!job_fail && job_ptr->job_resrcs &&
		    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) &&
		    gres_job_revalidate2(job_ptr->job_id,
					 job_ptr->gres_list_alloc,
					 job_ptr->job_resrcs->node_bitmap)) {
			/*
			 * This can be due to the job being allocated GRES
			 * which no longer exist (i.e. the GRES count on some
			 * allocated node changed since when the job started).
			 */
			error("Aborting %pJ due to use of invalid GRES configuration",
			      job_ptr);
			job_fail = true;
		}

		_sync_steps_to_conf(job_ptr);

		build_node_details(job_ptr, false); /* set node_addr */

		if (job_fail) {
			bool was_running = false;
			if (IS_JOB_PENDING(job_ptr)) {
				job_ptr->start_time =
					job_ptr->end_time = time(NULL);
				job_ptr->job_state = JOB_NODE_FAIL;
			} else if (IS_JOB_RUNNING(job_ptr)) {
				job_ptr->end_time = time(NULL);
				job_ptr->job_state =
					JOB_NODE_FAIL | JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
				was_running = true;
			} else if (IS_JOB_SUSPENDED(job_ptr)) {
				job_ptr->end_time = job_ptr->suspend_time;
				job_ptr->job_state =
					JOB_NODE_FAIL | JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
				job_ptr->tot_sus_time +=
					difftime(now, job_ptr->suspend_time);
				jobacct_storage_g_job_suspend(acct_db_conn,
							      job_ptr);
				was_running = true;
			}
			job_ptr->state_reason = FAIL_DOWN_NODE;
			xfree(job_ptr->state_desc);
			job_completion_logger(job_ptr, false);
			if (job_ptr->job_state == JOB_NODE_FAIL) {
				/* build_cg_bitmap() may clear JOB_COMPLETING */
				epilog_slurmctld(job_ptr);
			}
			if (was_running && job_ptr->batch_flag &&
			    job_ptr->details && job_ptr->details->requeue &&
			    job_ptr->part_ptr) {
				/*
				 * Mark for requeue
				 * see _requeue_job_node_failed()
				 */
				info("Attempting to requeue failed job %pJ",
				     job_ptr);
				job_ptr->job_state |= JOB_REQUEUE;

				/* Reset node_cnt to exclude vanished nodes */
				job_ptr->node_cnt = bit_set_count(
					job_ptr->node_bitmap_cg);
			}
		}
	}

	list_iterator_reset(job_iterator);
	/* This will reinitialize the select plugin database, which
	 * we can only do after ALL job's states and bitmaps are set
	 * (i.e. it needs to be in this second loop) */
	while ((job_ptr = list_next(job_iterator))) {
		if (select_g_select_nodeinfo_set(job_ptr) != SLURM_SUCCESS) {
			error("select_g_select_nodeinfo_set(%pJ): %m",
			      job_ptr);
		}
	}
	list_iterator_destroy(job_iterator);

	last_job_update = now;
}

static int _find_config_ptr(void *x, void *arg)
{
	return (x == arg);
}

static void _preserve_dynamic_nodes(node_record_t **old_node_table_ptr,
				    int old_node_record_count,
				    List old_config_list)
{
	for (int i = 0; i < old_node_record_count; i++) {
		node_record_t *node_ptr = old_node_table_ptr[i];

		if (!node_ptr ||
		    !IS_NODE_DYNAMIC_NORM(node_ptr))
			continue;

		insert_node_record(node_ptr);
		old_node_table_ptr[i] = NULL;

		/*
		 * insert_node_record() appends node_ptr->config_ptr to the
		 * global config_list. remove from old config_list so it
		 * doesn't get free'd.
		 */
		list_remove_first(old_config_list, _find_config_ptr,
				  node_ptr->config_ptr);
	}
}

/*
 * read_slurm_conf - load the slurm configuration from the configured file.
 * read_slurm_conf can be called more than once if so desired.
 * IN recover - replace job, node and/or partition data with latest
 *              available information depending upon value
 *              0 = use no saved state information, rebuild everything from
 *		    slurm.conf contents
 *              1 = recover saved job and trigger state,
 *                  node DOWN/DRAIN/FAIL state and reason information
 *              2 = recover all saved state
 * IN reconfig - true if SIGHUP or "scontrol reconfig" and there is state in
 *		 memory to preserve, otherwise recover state from disk
 * RET SLURM_SUCCESS if no error, otherwise an error code
 * Note: Operates on common variables only
 */
int read_slurm_conf(int recover, bool reconfig)
{
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	int i, rc = 0, load_job_ret = SLURM_SUCCESS;
	int old_node_record_count = 0;
	node_record_t **old_node_table_ptr = NULL, *node_ptr;
	List old_part_list = NULL, old_config_list = NULL;
	char *old_def_part_name = NULL;
	char *old_auth_type = xstrdup(slurm_conf.authtype);
	char *old_bb_type = xstrdup(slurm_conf.bb_type);
	char *old_cred_type = xstrdup(slurm_conf.cred_type);
	uint16_t old_preempt_mode = slurm_conf.preempt_mode;
	char *old_preempt_type = xstrdup(slurm_conf.preempt_type);
	char *old_sched_type = xstrdup(slurm_conf.schedtype);
	char *old_select_type = xstrdup(slurm_conf.select_type);
	char *old_switch_type = xstrdup(slurm_conf.switch_type);
	char *state_save_dir = xstrdup(slurm_conf.state_save_location);
	uint16_t old_select_type_p = slurm_conf.select_type_param;
	bool cgroup_mem_confinement = false;
	uint32_t old_max_node_cnt = 0;

	/* initialization */
	START_TIMER;

	if (reconfig) {
		/*
		 * In order to re-use job state information,
		 * update nodes_completing string (based on node bitmaps)
		 */
		update_job_nodes_strings();

		/* save node and partition states for reconfig RPC */
		old_node_record_count = node_record_count;
		old_node_table_ptr    = node_record_table_ptr;
		old_max_node_cnt = slurm_conf.max_node_cnt;

		for (i = 0; i < node_record_count; i++) {
			if (!(node_ptr = old_node_table_ptr[i]))
				continue;
			/*
			 * Store the original configured CPU count somewhere
			 * (port is reused here for that purpose) so we can
			 * report changes in its configuration.
			 */
			node_ptr->port   = node_ptr->config_ptr->cpus;
			node_ptr->weight = node_ptr->config_ptr->weight;
		}
		old_config_list = config_list;
		config_list = NULL;
		FREE_NULL_LIST(front_end_list);
		node_record_table_ptr = NULL;
		node_record_count = 0;
		xhash_free(node_hash_table);
		old_part_list = part_list;
		part_list = NULL;
		old_def_part_name = default_part_name;
		default_part_name = NULL;
	}

	_init_all_slurm_conf();

	if (reconfig)
		cgroup_conf_reinit();
	else
		cgroup_conf_init();

	cgroup_mem_confinement = cgroup_memcg_job_confinement();

	if (slurm_conf.job_acct_oom_kill && cgroup_mem_confinement)
		fatal("Jobs memory is being constrained by both TaskPlugin cgroup and JobAcctGather plugin. This enables two incompatible memory enforcement mechanisms, one of them must be disabled.");
	else if (slurm_conf.job_acct_oom_kill)
		info("Memory enforcing by using JobAcctGather's mechanism is discouraged, task/cgroup is recommended where available.");
	else if (!cgroup_mem_confinement)
		info("No memory enforcing mechanism configured.");

	if (slurm_conf.slurmd_user_id != 0)
		_test_cgroup_plugin_use();

	if (slurm_topo_init() != SLURM_SUCCESS) {
		if (test_config) {
			error("Failed to initialize topology plugin");
			test_config_rc = 1;
		} else {
			fatal("Failed to initialize topology plugin");
		}
	}

	/* Build node and partition information based upon slurm.conf file */
	build_all_nodeline_info(false, slurmctld_tres_cnt);
	/* Increase node table to handle dyanmic nodes. */
	if (node_record_count < slurm_conf.max_node_cnt) {
		node_record_count = slurm_conf.max_node_cnt;
		grow_node_record_table_ptr();
	} else {
		/* Lock node_record_table_ptr from growing */
		slurm_conf.max_node_cnt = node_record_count;
	}
	if (reconfig &&
	    old_max_node_cnt &&
	    (old_max_node_cnt != slurm_conf.max_node_cnt)) {
		fatal("MaxNodeCount has changed (%u->%u) during reconfig, slurmctld must be restarted",
		      old_max_node_cnt, slurm_conf.max_node_cnt);
	}

	(void)acct_storage_g_reconfig(acct_db_conn, 0);
	build_all_frontend_info(false);
	if (reconfig) {
		if (_compare_hostnames(old_node_table_ptr,
				       old_node_record_count,
				       node_record_table_ptr,
				       node_record_count) < 0) {
			fatal("%s: hostnames inconsistency detected", __func__);
		}
	}
	_handle_all_downnodes();
	_build_all_partitionline_info();
	if (!reconfig) {
		restore_front_end_state(recover);

		/* currently load/dump_state_lite has to run before
		 * load_all_job_state. */

		/* load old config */
		load_config_state_lite();

		/* store new config */
		if (!test_config)
			dump_config_state_lite(); }
	update_logging();
	jobcomp_g_init(slurm_conf.job_comp_loc);
	if (sched_g_init() != SLURM_SUCCESS) {
		if (test_config) {
			error("Failed to initialize sched plugin");
			test_config_rc = 1;
		} else {
			fatal("Failed to initialize sched plugin");
		}
	}
	if (!reconfig && (old_preempt_mode & PREEMPT_MODE_GANG)) {
		/* gs_init() must immediately follow sched_g_init() */
		gs_init();
	}
	if (switch_init(1) != SLURM_SUCCESS) {
		if (test_config) {
			error("Failed to initialize switch plugin");
			test_config_rc = 1;
		} else {
			fatal("Failed to initialize switch plugin");
		}
	}

	if (default_part_loc == NULL)
		error("read_slurm_conf: default partition not set.");

	if (node_record_count < 1) {
		error("read_slurm_conf: no nodes configured.");
		test_config_rc = 1;
		_purge_old_node_state(old_node_table_ptr,
				      old_node_record_count);
		_purge_old_part_state(old_part_list, old_def_part_name);
		error_code = EINVAL;
		goto end_it;
	}

	/*
	 * Node reordering may be done by the topology plugin.
	 * Reordering the table must be done before hashing the
	 * nodes, and before any position-relative bitmaps are created.
	 */
	_sort_node_record_table_ptr();

	rehash_node();
	slurm_topo_build_config();
	route_g_reconfigure();
	if (reconfig)
		power_g_reconfig();

	rehash_jobs();
	_validate_slurmd_addr();

	_stat_slurm_dirs();

	_init_bitmaps();

	/*
	 * Set standard features and preserve the plugin controlled ones.
	 * A reconfig always imply load the state from slurm.conf
	 */
	if (reconfig) {		/* Preserve state from memory */
		if (old_node_table_ptr) {
			info("restoring original state of nodes");
			_set_features(old_node_table_ptr, old_node_record_count,
				      recover);
			rc = _restore_node_state(recover, old_node_table_ptr,
						 old_node_record_count);
			error_code = MAX(error_code, rc);  /* not fatal */

			_preserve_dynamic_nodes(old_node_table_ptr,
						old_node_record_count,
						old_config_list);
		}
		if (old_part_list && ((recover > 1) ||
		    (slurm_conf.reconfig_flags & RECONFIG_KEEP_PART_INFO))) {
			info("restoring original partition state");
			rc = _restore_part_state(old_part_list,
			                         old_def_part_name,
			                         slurm_conf.reconfig_flags);
			error_code = MAX(error_code, rc);  /* not fatal */
		} else if (old_part_list && (slurm_conf.reconfig_flags &
		                             RECONFIG_KEEP_PART_STAT)) {
			info("restoring original partition state only (up/down)");
			rc = _restore_part_state(old_part_list,
			                         old_def_part_name,
			                         slurm_conf.reconfig_flags);
			error_code = MAX(error_code, rc);  /* not fatal */
		}
		load_last_job_id();
		reset_first_job_id();
		(void) sched_g_reconfig();
	} else if (recover == 0) {	/* Build everything from slurm.conf */
		_set_features(node_record_table_ptr, node_record_count,
			      recover);
		load_last_job_id();
		reset_first_job_id();
		(void) sched_g_reconfig();
	} else if (recover == 1) {	/* Load job & node state files */
		(void) load_all_node_state(true);
		_set_features(node_record_table_ptr, node_record_count,
			      recover);
		(void) load_all_front_end_state(true);
		load_job_ret = load_all_job_state();
		sync_job_priorities();
	} else if (recover > 1) {	/* Load node, part & job state files */
		(void) load_all_node_state(false);
		_set_features(old_node_table_ptr, old_node_record_count,
			      recover);
		(void) load_all_front_end_state(false);
		(void) load_all_part_state();
		load_job_ret = load_all_job_state();
		sync_job_priorities();
	}

	_sync_part_prio();
	_build_bitmaps_pre_select();
	if ((select_g_node_init() != SLURM_SUCCESS) ||
	    (select_g_state_restore(state_save_dir) != SLURM_SUCCESS) ||
	    (select_g_job_init(job_list) != SLURM_SUCCESS)) {
		if (test_config) {
			error("Failed to initialize node selection plugin state");
			test_config_rc = 1;
		} else {
			fatal("Failed to initialize node selection plugin state, "
			      "Clean start required.");
		}
	}

	_gres_reconfig(reconfig);
	_sync_jobs_to_conf();		/* must follow select_g_job_init() */

	/*
	 * The burst buffer plugin must be initialized and state loaded before
	 * _sync_nodes_to_jobs(), which calls bb_g_job_init().
	 */
	if (reconfig)
		rc =  bb_g_reconfig();
	else
		rc = bb_g_load_state(true);
	error_code = MAX(error_code, rc);	/* not fatal */

	(void) _sync_nodes_to_jobs(reconfig);
	(void) sync_job_files();
	_purge_old_node_state(old_node_table_ptr, old_node_record_count);
	_purge_old_part_state(old_part_list, old_def_part_name);
	FREE_NULL_LIST(old_config_list);

	reserve_port_config(slurm_conf.mpi_params);

	if (license_update(slurm_conf.licenses) != SLURM_SUCCESS) {
		if (test_config) {
			error("Invalid Licenses value: %s",
			      slurm_conf.licenses);
			test_config_rc = 1;
		} else {
			fatal("Invalid Licenses value: %s",
			      slurm_conf.licenses);
		}
	}

	init_requeue_policy();
	init_depend_policy();

	/* NOTE: Run restore_node_features before _restore_job_accounting */
	restore_node_features(recover);

	if ((node_features_g_count() > 0) &&
	    (node_features_g_get_node(NULL) != SLURM_SUCCESS)) {
		error("failed to initialize node features");
		test_config_rc = 1;
	}

	/*
	 * _build_bitmaps() must follow node_features_g_get_node() and
	 * preceed build_features_list_*()
	 */
	_build_bitmaps();

	/* Active and available features can be different on -R */
	if ((node_features_g_count() == 0) && (recover != 2))
		build_feature_list_eq();
	else
		build_feature_list_ne();

	/*
	 * Must be at after nodes and partitons (e.g.
	 * _build_bitmaps_pre_select()) have been created and before
	 * _sync_nodes_to_comp_job().
	 */
	if (!test_config)
		set_cluster_tres(false);

	_validate_het_jobs();
	(void) _sync_nodes_to_comp_job();/* must follow select_g_node_init() */
	_requeue_job_node_failed();
	load_part_uid_allow_list(1);

	/* NOTE: Run load_all_resv_state() before _restore_job_accounting */
	if (reconfig) {
		load_all_resv_state(0);
	} else {
		load_all_resv_state(recover);
		if (recover >= 1) {
			trigger_state_restore();
			(void) sched_g_reconfig();
		}
	}
	 if (test_config)
		goto end_it;

	_restore_job_accounting();

	/* sort config_list by weight for scheduling */
	list_sort(config_list, &list_compare_config);

	/* Update plugins as possible */
	if (xstrcmp(old_auth_type, slurm_conf.authtype)) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = old_auth_type;
		rc =  ESLURM_INVALID_AUTHTYPE_CHANGE;
	}

	if (xstrcmp(old_bb_type, slurm_conf.bb_type)) {
		xfree(slurm_conf.bb_type);
		slurm_conf.bb_type = old_bb_type;
		old_bb_type = NULL;
		rc =  ESLURM_INVALID_BURST_BUFFER_CHANGE;
	}

	if (xstrcmp(old_cred_type, slurm_conf.cred_type)) {
		xfree(slurm_conf.cred_type);
		slurm_conf.cred_type = old_cred_type;
		old_cred_type = NULL;
		rc = ESLURM_INVALID_CRED_TYPE_CHANGE;
	}

	if (xstrcmp(old_sched_type, slurm_conf.schedtype)) {
		xfree(slurm_conf.schedtype);
		slurm_conf.schedtype = old_sched_type;
		old_sched_type = NULL;
		rc =  ESLURM_INVALID_SCHEDTYPE_CHANGE;
	}

	if (xstrcmp(old_select_type, slurm_conf.select_type)) {
		xfree(slurm_conf.select_type);
		slurm_conf.select_type = old_select_type;
		old_select_type = NULL;
		rc =  ESLURM_INVALID_SELECTTYPE_CHANGE;
	}

	if (xstrcmp(old_switch_type, slurm_conf.switch_type)) {
		xfree(slurm_conf.switch_type);
		slurm_conf.switch_type = old_switch_type;
		old_switch_type = NULL;
		rc = ESLURM_INVALID_SWITCHTYPE_CHANGE;
	}

	if ((slurm_conf.control_cnt < 2) ||
	    (slurm_conf.control_machine[1] == NULL))
		info("%s: backup_controller not specified", __func__);

	error_code = MAX(error_code, rc);	/* not fatal */

	if (xstrcmp(old_preempt_type, slurm_conf.preempt_type)) {
		info("Changing PreemptType from %s to %s",
		     old_preempt_type, slurm_conf.preempt_type);
		(void) slurm_preempt_fini();
		if (slurm_preempt_init() != SLURM_SUCCESS) {
			if (test_config) {
				error("failed to initialize preempt plugin");
				test_config_rc = 1;
			} else {
				fatal("failed to initialize preempt plugin");
			}
		}
	}
	_update_preempt(old_preempt_mode);

	/* Update plugin parameters as possible */
	rc = job_submit_plugin_reconfig();
	error_code = MAX(error_code, rc);	/* not fatal */
	rc = prep_g_reconfig();
	error_code = MAX(error_code, rc);	/* not fatal */
	rc = switch_g_reconfig();
	error_code = MAX(error_code, rc);	/* not fatal */
	if (reconfig) {
		rc = node_features_g_reconfig();
		error_code = MAX(error_code, rc); /* not fatal */
	}
	rc = _preserve_select_type_param(&slurm_conf, old_select_type_p);
	error_code = MAX(error_code, rc);	/* not fatal */

	/*
	 * Restore job accounting info if file missing or corrupted,
	 * an extremely rare situation
	 */
	if (load_job_ret)
		_acct_restore_active_jobs();

	/* Sync select plugin with synchronized job/node/part data */
	gres_reconfig();		/* Clear gres/mps counters */
	select_g_reconfigure();
	if (reconfig && (slurm_mcs_reconfig() != SLURM_SUCCESS))
		fatal("Failed to reconfigure mcs plugin");

	_set_response_cluster_rec();

	config_power_mgr();

	slurm_conf.last_update = time(NULL);
end_it:
	xfree(old_auth_type);
	xfree(old_bb_type);
	xfree(old_cred_type);
	xfree(old_preempt_type);
	xfree(old_sched_type);
	xfree(old_select_type);
	xfree(old_switch_type);
	xfree(state_save_dir);

	END_TIMER2("read_slurm_conf");
	return error_code;

}

/* Add feature to list
 * feature_list IN - destination list, either active_feature_list or
 *	avail_feature_list
 * feature IN - name of the feature to add
 * node_bitmap IN - bitmap of nodes with named feature */
static void _add_config_feature(List feature_list, char *feature,
				bitstr_t *node_bitmap)
{
	node_feature_t *feature_ptr;
	ListIterator feature_iter;
	bool match = false;

	/* If feature already in avail_feature_list, just update the bitmap */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		if (xstrcmp(feature, feature_ptr->name))
			continue;
		bit_or(feature_ptr->node_bitmap, node_bitmap);
		match = true;
		break;
	}
	list_iterator_destroy(feature_iter);

	if (!match) {	/* Need to create new avail_feature_list record */
		feature_ptr = xmalloc(sizeof(node_feature_t));
		feature_ptr->magic = FEATURE_MAGIC;
		feature_ptr->name = xstrdup(feature);
		feature_ptr->node_bitmap = bit_copy(node_bitmap);
		list_append(feature_list, feature_ptr);
	}
}

/* Add feature to list
 * feature_list IN - destination list, either active_feature_list or
 *	avail_feature_list
 * feature IN - name of the feature to add
 * node_inx IN - index of the node with named feature */
static void _add_config_feature_inx(List feature_list, char *feature,
				    int node_inx)
{
	node_feature_t *feature_ptr;
	ListIterator feature_iter;
	bool match = false;

	/* If feature already in avail_feature_list, just update the bitmap */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		if (xstrcmp(feature, feature_ptr->name))
			continue;
		bit_set(feature_ptr->node_bitmap, node_inx);
		match = true;
		break;
	}
	list_iterator_destroy(feature_iter);

	if (!match) {	/* Need to create new avail_feature_list record */
		feature_ptr = xmalloc(sizeof(node_feature_t));
		feature_ptr->magic = FEATURE_MAGIC;
		feature_ptr->name = xstrdup(feature);
		feature_ptr->node_bitmap = bit_alloc(node_record_count);
		bit_set(feature_ptr->node_bitmap, node_inx);
		list_append(feature_list, feature_ptr);
	}
}

/* _list_delete_feature - delete an entry from the feature list,
 *	see list.h for documentation */
static void _list_delete_feature(void *feature_entry)
{
	node_feature_t *feature_ptr = (node_feature_t *) feature_entry;

	xassert(feature_ptr);
	xassert(feature_ptr->magic == FEATURE_MAGIC);
	xfree (feature_ptr->name);
	FREE_NULL_BITMAP (feature_ptr->node_bitmap);
	xfree (feature_ptr);
}

/*
 * For a configuration where available_features == active_features,
 * build new active and available feature lists
 */
extern void build_feature_list_eq(void)
{
	ListIterator config_iterator;
	config_record_t *config_ptr;
	node_feature_t *active_feature_ptr, *avail_feature_ptr;
	ListIterator feature_iter;
	char *tmp_str, *token, *last = NULL;

	FREE_NULL_LIST(active_feature_list);
	FREE_NULL_LIST(avail_feature_list);
	active_feature_list = list_create(_list_delete_feature);
	avail_feature_list = list_create(_list_delete_feature);

	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr->feature) {
			tmp_str = xstrdup(config_ptr->feature);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature(avail_feature_list, token,
						    config_ptr->node_bitmap);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
	}
	list_iterator_destroy(config_iterator);

	/* Copy avail_feature_list to active_feature_list */
	feature_iter = list_iterator_create(avail_feature_list);
	while ((avail_feature_ptr = list_next(feature_iter))) {
		active_feature_ptr = xmalloc(sizeof(node_feature_t));
		active_feature_ptr->magic = FEATURE_MAGIC;
		active_feature_ptr->name = xstrdup(avail_feature_ptr->name);
		active_feature_ptr->node_bitmap =
			bit_copy(avail_feature_ptr->node_bitmap);
		list_append(active_feature_list, active_feature_ptr);
	}
	list_iterator_destroy(feature_iter);
}

/*
 * Log contents of avail_feature_list and active_feature_list
 */
extern void log_feature_lists(void)
{
	node_feature_t *feature_ptr;
	char *node_str;
	ListIterator feature_iter;

	feature_iter = list_iterator_create(avail_feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		node_str = bitmap2node_name(feature_ptr->node_bitmap);
		info("AVAIL FEATURE:%s NODES:%s", feature_ptr->name, node_str);
		xfree(node_str);
	}
	list_iterator_destroy(feature_iter);

	feature_iter = list_iterator_create(active_feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		node_str = bitmap2node_name(feature_ptr->node_bitmap);
		info("ACTIVE FEATURE:%s NODES:%s", feature_ptr->name, node_str);
		xfree(node_str);
	}
	list_iterator_destroy(feature_iter);
}

/*
 * For a configuration where available_features != active_features,
 * build new active and available feature lists
 */
extern void build_feature_list_ne(void)
{
	node_record_t *node_ptr;
	char *tmp_str, *token, *last = NULL;
	int i;

	FREE_NULL_LIST(active_feature_list);
	FREE_NULL_LIST(avail_feature_list);
	active_feature_list = list_create(_list_delete_feature);
	avail_feature_list = list_create(_list_delete_feature);

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->features_act) {
			tmp_str = xstrdup(node_ptr->features_act);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature_inx(active_feature_list,
							token, node_ptr->index);
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
		if (node_ptr->features) {
			tmp_str = xstrdup(node_ptr->features);
			token = strtok_r(tmp_str, ",", &last);
			while (token) {
				_add_config_feature_inx(avail_feature_list,
							token, node_ptr->index);
				if (!node_ptr->features_act) {
					_add_config_feature_inx(
							active_feature_list,
							token, node_ptr->index);
				}
				token = strtok_r(NULL, ",", &last);
			}
			xfree(tmp_str);
		}
	}
}

/*
 * Update active_feature_list or avail_feature_list
 * feature_list IN - List to update: active_feature_list or avail_feature_list
 * new_features IN - New active_features
 * node_bitmap IN - Nodes with the new active_features value
 */
extern void update_feature_list(List feature_list, char *new_features,
				bitstr_t *node_bitmap)
{
	node_feature_t *feature_ptr;
	ListIterator feature_iter;
	char *tmp_str, *token, *last = NULL;

	/*
	 * Clear these nodes from the feature_list record,
	 * then restore as needed
	 */
	feature_iter = list_iterator_create(feature_list);
	while ((feature_ptr = list_next(feature_iter))) {
		bit_and_not(feature_ptr->node_bitmap, node_bitmap);
	}
	list_iterator_destroy(feature_iter);

	if (new_features) {
		tmp_str = xstrdup(new_features);
		token = strtok_r(tmp_str, ",", &last);
		while (token) {
			_add_config_feature(feature_list, token, node_bitmap);
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_str);
	}
	node_features_updated = true;
}

static void _gres_reconfig(bool reconfig)
{
	node_record_t *node_ptr;
	char *gres_name;
	int i;
	bool gres_loaded = false;

	if (reconfig) {
		gres_reconfig();
		goto grab_includes;
	}

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->gres)
			gres_name = node_ptr->gres;
		else
			gres_name = node_ptr->config_ptr->gres;
		gres_init_node_config(gres_name, &node_ptr->gres_list);
		if (!IS_NODE_CLOUD(node_ptr))
			continue;

		/*
		 * Load in GRES for node now. By default Slurm gets this
		 * information when the node registers for the first
		 * time, which can take a while for a node in the cloud
		 * to boot.
		 */
		gres_g_node_config_load(
			node_ptr->config_ptr->cpus, node_ptr->name,
			node_ptr->gres_list, NULL, NULL);
		gres_node_config_validate(
			node_ptr->name, node_ptr->config_ptr->gres,
			&node_ptr->gres, &node_ptr->gres_list,
			node_ptr->config_ptr->threads,
			node_ptr->config_ptr->cores,
			node_ptr->config_ptr->tot_sockets,
			slurm_conf.conf_flags & CTL_CONF_OR, NULL);

		gres_loaded = true;
	}

grab_includes:
	if (!gres_loaded) {
		/*
		 * Parse the gres.conf for any Include files to push with
		 * configless files. Reading the file, without loading the
		 * options, will add the Include files to conf_includes_list and
		 * will be sent with configless.
		 */
		gres_parse_config_dummy();
	}
}
/*
 * Configure node features.
 * IN old_node_table_ptr IN - Previous nodes information
 * IN old_node_record_count IN - Count of previous nodes information
 * IN recover - replace node features data depending upon value.
 *              0, 1 - use data from config record, built using slurm.conf
 *              2 = use data from node record, built from saved state
 */
static void _set_features(node_record_t **old_node_table_ptr,
			  int old_node_record_count, int recover)
{
	node_record_t *node_ptr, *old_node_ptr;
	char *tmp, *tok, *sep;
	int i, node_features_cnt = node_features_g_count();

	for (i = 0; i < old_node_record_count; i++) {
		if (!(old_node_ptr = old_node_table_ptr[i]))
			continue;

		node_ptr  = find_node_record(old_node_ptr->name);

		if (node_ptr == NULL)
			continue;

		/*
		 * Load all from state, ignore what has been read from
		 * slurm.conf. Features in node record just a placeholder
		 * for restore_node_features() to set up new config records.
		 */
		if (recover == 2) {
			xfree(node_ptr->features);
			xfree(node_ptr->features_act);
			node_ptr->features = old_node_ptr->features;
			node_ptr->features_act = old_node_ptr->features_act;
			old_node_ptr->features = NULL;
			old_node_ptr->features_act = NULL;
			continue;
		}

		xfree(node_ptr->features_act);
		node_ptr->features_act = xstrdup(node_ptr->features);

		if (node_features_cnt == 0)
			continue;

		/* If we are here, there's a node_features plugin active */

		/*
		 * The subset of plugin-controlled features_available
		 * and features_active found in the old node_ptr for this node
		 * are copied into new node respective fields.
		 * This will make that KNL modes are preserved while doing a
		 * reconfigure. Otherwise, we should wait until node is
		 * registered to get KNL available and active features.
		 */
		if (old_node_ptr->features != NULL) {
			char *save_ptr = NULL;
			if (node_ptr->features)
				sep = ",";
			else
				sep = "";
			tmp = xstrdup(old_node_ptr->features);
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if (node_features_g_changeable_feature(tok)) {
					xstrfmtcat(node_ptr->features,
						   "%s%s", sep, tok);
					sep = ",";
				}
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		}

		if (old_node_ptr->features_act != NULL) {
			char *save_ptr = NULL;
			if (node_ptr->features_act)
				sep = ",";
			else
				sep = "";
			tmp = xstrdup(old_node_ptr->features_act);
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if (node_features_g_changeable_feature(tok)) {
					xstrfmtcat(node_ptr->features_act,
						   "%s%s", sep, tok);
					sep = ",";
				}
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		}
	}
}
/* Restore node state and size information from saved records which match
 * the node registration message. If a node was re-configured to be down or
 * drained, we set those states. We only recover a node's Features if
 * recover==2. */
static int _restore_node_state(int recover,
			       node_record_t **old_node_table_ptr,
			       int old_node_record_count)
{
	node_record_t *node_ptr, *old_node_ptr;
	int i, rc = SLURM_SUCCESS;
	hostset_t hs = NULL;
	bool power_save_mode = false;

	if (slurm_conf.suspend_program && slurm_conf.resume_program)
		power_save_mode = true;

	for (i = 0; (node_ptr = next_node(&i)); i++)
		node_ptr->not_responding = true;

	for (i = 0; i < old_node_record_count; i++) {
		bool cloud_flag = false, drain_flag = false, down_flag = false;
		dynamic_plugin_data_t *tmp_select_nodeinfo;

		if (!(old_node_ptr = old_node_table_ptr[i]))
			continue;
		node_ptr  = find_node_record(old_node_ptr->name);
		if (node_ptr == NULL)
			continue;

		node_ptr->not_responding = false;
		if (IS_NODE_CLOUD(node_ptr))
			cloud_flag = true;
		if (IS_NODE_DOWN(node_ptr))
			down_flag = true;
		if (IS_NODE_DRAIN(node_ptr))
			drain_flag = true;
		if ( IS_NODE_FUTURE(old_node_ptr) &&
		    !IS_NODE_FUTURE(node_ptr)) {
			/* Replace FUTURE state with new state, but preserve
			 * state flags (e.g. POWER) */
			node_ptr->node_state =
				(node_ptr->node_state     & NODE_STATE_BASE) |
				(old_node_ptr->node_state & NODE_STATE_FLAGS);
			/*
			 * If node was FUTURE, then it wasn't up so mark it as
			 * powered_down.
			 */
			if (cloud_flag)
				node_ptr->node_state |= NODE_STATE_POWERED_DOWN;
		} else {
			node_ptr->node_state = old_node_ptr->node_state;
		}

		if (cloud_flag)
			node_ptr->node_state |= NODE_STATE_CLOUD;
		if (down_flag) {
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state |= NODE_STATE_DOWN;
		}
		if (drain_flag)
			node_ptr->node_state |= NODE_STATE_DRAIN;
		if ((!power_save_mode) &&
		    (IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr) ||
		     IS_NODE_POWERING_UP(node_ptr))) {
			node_ptr->node_state &= (~NODE_STATE_POWERED_DOWN);
			node_ptr->node_state &= (~NODE_STATE_POWERING_DOWN);
			node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
			if (hs)
				hostset_insert(hs, node_ptr->name);
			else
				hs = hostset_create(node_ptr->name);
		}

		if (IS_NODE_DYNAMIC_FUTURE(node_ptr) ||
		    (IS_NODE_CLOUD(node_ptr) &&
		     !IS_NODE_POWERED_DOWN(node_ptr))) {
			/* Preserve NodeHostname + NodeAddr set by scontrol */
			set_node_comm_name(node_ptr,
					   old_node_ptr->comm_name,
					   old_node_ptr->node_hostname);
		}

		node_ptr->last_response = old_node_ptr->last_response;
		node_ptr->protocol_version = old_node_ptr->protocol_version;
		node_ptr->cpu_load = old_node_ptr->cpu_load;

		/* make sure we get the old state from the select
		 * plugin, just swap it out to avoid possible memory leak */
		tmp_select_nodeinfo = node_ptr->select_nodeinfo;
		node_ptr->select_nodeinfo = old_node_ptr->select_nodeinfo;
		old_node_ptr->select_nodeinfo = tmp_select_nodeinfo;

		if (old_node_ptr->port != node_ptr->config_ptr->cpus) {
			rc = ESLURM_NEED_RESTART;
			error("Configured cpu count change on %s (%u to %u)",
			      node_ptr->name, old_node_ptr->port,
			      node_ptr->config_ptr->cpus);
		}

		node_ptr->boot_time     = old_node_ptr->boot_time;
		node_ptr->boot_req_time = old_node_ptr->boot_req_time;
		node_ptr->power_save_req_time =
			old_node_ptr->power_save_req_time;
		node_ptr->cpus          = old_node_ptr->cpus;
		node_ptr->cores         = old_node_ptr->cores;
		xfree(node_ptr->cpu_spec_list);
		node_ptr->cpu_spec_list = old_node_ptr->cpu_spec_list;
		old_node_ptr->cpu_spec_list = NULL;
		node_ptr->core_spec_cnt = old_node_ptr->core_spec_cnt;
		node_ptr->last_busy     = old_node_ptr->last_busy;
		node_ptr->boards        = old_node_ptr->boards;
		node_ptr->tot_sockets       = old_node_ptr->tot_sockets;
		node_ptr->threads       = old_node_ptr->threads;
		node_ptr->real_memory   = old_node_ptr->real_memory;
		node_ptr->mem_spec_limit = old_node_ptr->mem_spec_limit;
		node_ptr->slurmd_start_time = old_node_ptr->slurmd_start_time;
		node_ptr->tmp_disk      = old_node_ptr->tmp_disk;
		node_ptr->weight        = old_node_ptr->weight;
		node_ptr->tot_cores = node_ptr->tot_sockets * node_ptr->cores;

		node_ptr->sus_job_cnt   = old_node_ptr->sus_job_cnt;

		FREE_NULL_LIST(node_ptr->gres_list);
		node_ptr->gres_list = old_node_ptr->gres_list;
		old_node_ptr->gres_list = NULL;

		node_ptr->comment = old_node_ptr->comment;
		old_node_ptr->comment = NULL;

		node_ptr->extra = old_node_ptr->extra;
		old_node_ptr->extra = NULL;

		if (node_ptr->reason == NULL) {
			/* Recover only if not explicitly set in slurm.conf */
			node_ptr->reason = old_node_ptr->reason;
			node_ptr->reason_time = old_node_ptr->reason_time;
			old_node_ptr->reason = NULL;
		}
		if (recover == 2) {
			xfree(node_ptr->gres);
			node_ptr->gres = old_node_ptr->gres;
			old_node_ptr->gres = NULL;
		}
		if (old_node_ptr->arch) {
			xfree(node_ptr->arch);
			node_ptr->arch = old_node_ptr->arch;
			old_node_ptr->arch = NULL;
		}
		if (old_node_ptr->os) {
			xfree(node_ptr->os);
			node_ptr->os = old_node_ptr->os;
			old_node_ptr->os = NULL;
		}
		if (old_node_ptr->node_spec_bitmap) {
			FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
			node_ptr->node_spec_bitmap =
				old_node_ptr->node_spec_bitmap;
			old_node_ptr->node_spec_bitmap = NULL;
		}
	}

	if (hs) {
		char node_names[128];
		hostset_ranged_string(hs, sizeof(node_names), node_names);
		info("Cleared POWER_SAVE flag from nodes %s", node_names);
		hostset_destroy(hs);
		hs = NULL;
	}

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->not_responding)
			continue;
		node_ptr->not_responding = false;
		if (hs)
			hostset_insert(hs, node_ptr->name);
		else
			hs = hostset_create(node_ptr->name);
	}
	if (hs) {
		char node_names[128];
		hostset_ranged_string(hs, sizeof(node_names), node_names);
		error("Nodes added to configuration (%s)", node_names);
		error("Reboot of all slurm daemons is recommended");
		hostset_destroy(hs);
	}

	return rc;
}

/* Purge old node state information */
static void _purge_old_node_state(node_record_t **old_node_table_ptr,
				  int old_node_record_count)
{
	int i;

	if (old_node_table_ptr) {
		for (i = 0; i < old_node_record_count; i++)
			if (old_node_table_ptr[i])
				purge_node_rec(old_node_table_ptr[i]);
		xfree(old_node_table_ptr);
	}
}

/* Restore partition information from saved records */
static int  _restore_part_state(List old_part_list, char *old_def_part_name,
				uint16_t flags)
{
	int rc = SLURM_SUCCESS;
	ListIterator part_iterator;
	part_record_t *old_part_ptr, *part_ptr;

	if (!old_part_list)
		return rc;

	/* For each part in list, find and update recs */
	part_iterator = list_iterator_create(old_part_list);
	while ((old_part_ptr = list_next(part_iterator))) {
		xassert(old_part_ptr->magic == PART_MAGIC);
		part_ptr = find_part_record(old_part_ptr->name);
		if (part_ptr) {
			if ( !(flags & RECONFIG_KEEP_PART_INFO) &&
			     (flags & RECONFIG_KEEP_PART_STAT)	) {
				if (part_ptr->state_up != old_part_ptr->state_up) {
					info("Partition %s State differs from "
					     "slurm.conf", part_ptr->name);
					part_ptr->state_up = old_part_ptr->state_up;
				}
				continue;
			}
			/* Current partition found in slurm.conf,
			 * report differences from slurm.conf configuration */
			if (xstrcmp(part_ptr->allow_accounts,
				    old_part_ptr->allow_accounts)) {
				error("Partition %s AllowAccounts differs from slurm.conf",
				      part_ptr->name);
				xfree(part_ptr->allow_accounts);
				part_ptr->allow_accounts =
					xstrdup(old_part_ptr->allow_accounts);
				accounts_list_build(part_ptr->allow_accounts,
						&part_ptr->allow_account_array);
			}
			if (xstrcmp(part_ptr->allow_alloc_nodes,
				    old_part_ptr->allow_alloc_nodes)) {
				error("Partition %s AllowNodes differs from slurm.conf",
				      part_ptr->name);
				xfree(part_ptr->allow_alloc_nodes);
				part_ptr->allow_alloc_nodes =
					xstrdup(old_part_ptr->allow_alloc_nodes);
			}
			if (xstrcmp(part_ptr->allow_groups,
				    old_part_ptr->allow_groups)) {
				error("Partition %s AllowGroups differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_groups);
				part_ptr->allow_groups = xstrdup(old_part_ptr->
								 allow_groups);
			}
			if (xstrcmp(part_ptr->allow_qos,
				    old_part_ptr->allow_qos)) {
				error("Partition %s AllowQos differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_qos);
				part_ptr->allow_qos = xstrdup(old_part_ptr->
								 allow_qos);
				qos_list_build(part_ptr->allow_qos,
					       &part_ptr->allow_qos_bitstr);
			}
			if (xstrcmp(part_ptr->alternate,
				    old_part_ptr->alternate)) {
				error("Partition %s Alternate differs from slurm.conf",
				      part_ptr->name);
				xfree(part_ptr->alternate);
				part_ptr->alternate =
					xstrdup(old_part_ptr->alternate);
			}
			if (part_ptr->def_mem_per_cpu !=
			    old_part_ptr->def_mem_per_cpu) {
				error("Partition %s DefMemPerCPU differs from slurm.conf",
				      part_ptr->name);
				part_ptr->def_mem_per_cpu =
					old_part_ptr->def_mem_per_cpu;
			}
			if (part_ptr->default_time !=
			    old_part_ptr->default_time) {
				error("Partition %s DefaultTime differs from slurm.conf",
				      part_ptr->name);
				part_ptr->default_time =
					old_part_ptr->default_time;
			}
			if (xstrcmp(part_ptr->deny_accounts,
				    old_part_ptr->deny_accounts)) {
				error("Partition %s DenyAccounts differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->deny_accounts);
				part_ptr->deny_accounts =
					xstrdup(old_part_ptr->deny_accounts);
				accounts_list_build(part_ptr->deny_accounts,
						&part_ptr->deny_account_array);
			}
			if (xstrcmp(part_ptr->deny_qos,
				    old_part_ptr->deny_qos)) {
				error("Partition %s DenyQos differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->deny_qos);
				part_ptr->deny_qos = xstrdup(old_part_ptr->
							     deny_qos);
				qos_list_build(part_ptr->deny_qos,
					       &part_ptr->deny_qos_bitstr);
			}
			if ((part_ptr->flags & PART_FLAG_HIDDEN) !=
			    (old_part_ptr->flags & PART_FLAG_HIDDEN)) {
				error("Partition %s Hidden differs from "
				      "slurm.conf", part_ptr->name);
				if (old_part_ptr->flags & PART_FLAG_HIDDEN)
					part_ptr->flags |= PART_FLAG_HIDDEN;
				else
					part_ptr->flags &= (~PART_FLAG_HIDDEN);
			}
			if ((part_ptr->flags & PART_FLAG_NO_ROOT) !=
			    (old_part_ptr->flags & PART_FLAG_NO_ROOT)) {
				error("Partition %s DisableRootJobs differs "
				      "from slurm.conf", part_ptr->name);
				if (old_part_ptr->flags & PART_FLAG_NO_ROOT)
					part_ptr->flags |= PART_FLAG_NO_ROOT;
				else
					part_ptr->flags &= (~PART_FLAG_NO_ROOT);
			}
			if ((part_ptr->flags & PART_FLAG_EXCLUSIVE_USER) !=
			    (old_part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)) {
				error("Partition %s ExclusiveUser differs "
				      "from slurm.conf", part_ptr->name);
				if (old_part_ptr->flags &
				    PART_FLAG_EXCLUSIVE_USER) {
					part_ptr->flags |=
						PART_FLAG_EXCLUSIVE_USER;
				} else {
					part_ptr->flags &=
						(~PART_FLAG_EXCLUSIVE_USER);
				}
			}
			if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) !=
			    (old_part_ptr->flags & PART_FLAG_ROOT_ONLY)) {
				error("Partition %s RootOnly differs from "
				      "slurm.conf", part_ptr->name);
				if (old_part_ptr->flags & PART_FLAG_ROOT_ONLY)
					part_ptr->flags |= PART_FLAG_ROOT_ONLY;
				else
					part_ptr->flags &= (~PART_FLAG_ROOT_ONLY);
			}
			if ((part_ptr->flags & PART_FLAG_REQ_RESV) !=
			    (old_part_ptr->flags & PART_FLAG_REQ_RESV)) {
				error("Partition %s ReqResv differs from "
				      "slurm.conf", part_ptr->name);
				if (old_part_ptr->flags & PART_FLAG_REQ_RESV)
					part_ptr->flags |= PART_FLAG_REQ_RESV;
				else
					part_ptr->flags &= (~PART_FLAG_REQ_RESV);
			}
			if ((part_ptr->flags & PART_FLAG_LLN) !=
			    (old_part_ptr->flags & PART_FLAG_LLN)) {
				error("Partition %s LLN differs from "
				      "slurm.conf", part_ptr->name);
				if (old_part_ptr->flags & PART_FLAG_LLN)
					part_ptr->flags |= PART_FLAG_LLN;
				else
					part_ptr->flags &= (~PART_FLAG_LLN);
			}
			if (part_ptr->grace_time != old_part_ptr->grace_time) {
				error("Partition %s GraceTime differs from slurm.conf",
				      part_ptr->name);
				part_ptr->grace_time = old_part_ptr->grace_time;
			}
			if (part_ptr->max_cpus_per_node !=
			    old_part_ptr->max_cpus_per_node) {
				error("Partition %s MaxCPUsPerNode differs from slurm.conf"
				      " (%u != %u)",
				      part_ptr->name,
				      part_ptr->max_cpus_per_node,
				      old_part_ptr->max_cpus_per_node);
				part_ptr->max_cpus_per_node =
					old_part_ptr->max_cpus_per_node;
			}
			if (part_ptr->max_mem_per_cpu !=
			    old_part_ptr->max_mem_per_cpu) {
				error("Partition %s MaxMemPerNode/MaxMemPerCPU differs from slurm.conf"
				      " (%"PRIu64" != %"PRIu64")",
				      part_ptr->name,
				      part_ptr->max_mem_per_cpu,
				      old_part_ptr->max_mem_per_cpu);
				part_ptr->max_mem_per_cpu =
					old_part_ptr->max_mem_per_cpu;
			}
			if (part_ptr->max_nodes_orig !=
			    old_part_ptr->max_nodes_orig) {
				error("Partition %s MaxNodes differs from "
				      "slurm.conf (%u != %u)", part_ptr->name,
				       part_ptr->max_nodes_orig,
				       old_part_ptr->max_nodes_orig);
				part_ptr->max_nodes = old_part_ptr->
						      max_nodes_orig;
				part_ptr->max_nodes_orig = old_part_ptr->
							   max_nodes_orig;
			}
			if (part_ptr->max_share != old_part_ptr->max_share) {
				error("Partition %s OverSubscribe differs from slurm.conf",
				      part_ptr->name);
				part_ptr->max_share = old_part_ptr->max_share;
			}
			if (part_ptr->max_time != old_part_ptr->max_time) {
				error("Partition %s MaxTime differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->max_time = old_part_ptr->max_time;
			}
			if (part_ptr->min_nodes_orig !=
			    old_part_ptr->min_nodes_orig) {
				error("Partition %s MinNodes differs from "
				      "slurm.conf (%u != %u)", part_ptr->name,
				       part_ptr->min_nodes_orig,
				       old_part_ptr->min_nodes_orig);
				part_ptr->min_nodes = old_part_ptr->
						      min_nodes_orig;
				part_ptr->min_nodes_orig = old_part_ptr->
							   min_nodes_orig;
			}
			if (xstrcmp(part_ptr->nodes, old_part_ptr->nodes)) {
				error("Partition %s Nodes differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->nodes);
				part_ptr->nodes = xstrdup(old_part_ptr->nodes);
				xfree(part_ptr->orig_nodes);
				part_ptr->orig_nodes =
					xstrdup(old_part_ptr->orig_nodes);
			}
			if (part_ptr->over_time_limit !=
			    old_part_ptr->over_time_limit) {
				error("Partition %s OverTimeLimit differs from slurm.conf",
				      part_ptr->name);
				part_ptr->over_time_limit =
					old_part_ptr->over_time_limit;
			}
			if (part_ptr->preempt_mode !=
			    old_part_ptr->preempt_mode) {
				error("Partition %s PreemptMode differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->preempt_mode = old_part_ptr->
							 preempt_mode;
			}
			if (part_ptr->priority_job_factor !=
			    old_part_ptr->priority_job_factor) {
				error("Partition %s PriorityJobFactor differs "
				      "from slurm.conf", part_ptr->name);
				part_ptr->priority_job_factor =
					old_part_ptr->priority_job_factor;
			}
			if (part_ptr->priority_tier !=
			    old_part_ptr->priority_tier) {
				error("Partition %s PriorityTier differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->priority_tier =
					old_part_ptr->priority_tier;
			}
			if (xstrcmp(part_ptr->qos_char,
				    old_part_ptr->qos_char)) {
				error("Partition %s QOS differs from slurm.conf",
				      part_ptr->name);
				xfree(part_ptr->qos_char);
				part_ptr->qos_char =
					xstrdup(old_part_ptr->qos_char);
				part_ptr->qos_ptr = old_part_ptr->qos_ptr;
			}
			if (part_ptr->state_up != old_part_ptr->state_up) {
				error("Partition %s State differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->state_up = old_part_ptr->state_up;
			}
		} else {
			if ( !(flags & RECONFIG_KEEP_PART_INFO) &&
			     (flags & RECONFIG_KEEP_PART_STAT) ) {
				info("Partition %s missing from slurm.conf, "
				     "not restoring it", old_part_ptr->name);
				continue;
			}
			error("Partition %s missing from slurm.conf, "
			      "restoring it", old_part_ptr->name);
			part_ptr = create_part_record(old_part_ptr->name);

			part_ptr->allow_accounts =
				xstrdup(old_part_ptr->allow_accounts);
			accounts_list_build(part_ptr->allow_accounts,
					 &part_ptr->allow_account_array);
			part_ptr->allow_alloc_nodes =
				xstrdup(old_part_ptr->allow_alloc_nodes);
			part_ptr->allow_groups = xstrdup(old_part_ptr->
							 allow_groups);
			part_ptr->allow_qos = xstrdup(old_part_ptr->
						      allow_qos);
			qos_list_build(part_ptr->allow_qos,
				       &part_ptr->allow_qos_bitstr);
			part_ptr->def_mem_per_cpu =
				old_part_ptr->def_mem_per_cpu;
			part_ptr->default_time = old_part_ptr->default_time;
			part_ptr->deny_accounts = xstrdup(old_part_ptr->
							  deny_accounts);
			accounts_list_build(part_ptr->deny_accounts,
					 &part_ptr->deny_account_array);
			part_ptr->deny_qos = xstrdup(old_part_ptr->
						     deny_qos);
			qos_list_build(part_ptr->deny_qos,
				       &part_ptr->deny_qos_bitstr);
			part_ptr->flags = old_part_ptr->flags;
			part_ptr->grace_time = old_part_ptr->grace_time;
			part_ptr->job_defaults_list =
				job_defaults_copy(old_part_ptr->job_defaults_list);
			part_ptr->max_cpus_per_node =
				old_part_ptr->max_cpus_per_node;
			part_ptr->max_mem_per_cpu =
				old_part_ptr->max_mem_per_cpu;
			part_ptr->max_nodes = old_part_ptr->max_nodes;
			part_ptr->max_nodes_orig = old_part_ptr->
						   max_nodes_orig;
			part_ptr->max_share = old_part_ptr->max_share;
			part_ptr->max_time = old_part_ptr->max_time;
			part_ptr->min_nodes = old_part_ptr->min_nodes;
			part_ptr->min_nodes_orig = old_part_ptr->
						   min_nodes_orig;
			part_ptr->nodes = xstrdup(old_part_ptr->nodes);
			part_ptr->orig_nodes =
				xstrdup(old_part_ptr->orig_nodes);
			part_ptr->over_time_limit =
				old_part_ptr->over_time_limit;
			part_ptr->preempt_mode = old_part_ptr->preempt_mode;
			part_ptr->priority_job_factor =
				old_part_ptr->priority_job_factor;
			part_ptr->priority_tier = old_part_ptr->priority_tier;
			part_ptr->qos_char =
				xstrdup(old_part_ptr->qos_char);
			part_ptr->qos_ptr = old_part_ptr->qos_ptr;
			part_ptr->state_up = old_part_ptr->state_up;
		}
	}
	list_iterator_destroy(part_iterator);

	if (old_def_part_name &&
	    ((default_part_name == NULL) ||
	     xstrcmp(old_def_part_name, default_part_name))) {
		part_ptr = find_part_record(old_def_part_name);
		if (part_ptr) {
			error("Default partition reset to %s",
			      old_def_part_name);
			default_part_loc  = part_ptr;
			xfree(default_part_name);
			default_part_name = xstrdup(old_def_part_name);
		}
	}

	return rc;
}

/* Purge old partition state information */
static void _purge_old_part_state(List old_part_list, char *old_def_part_name)
{
	xfree(old_def_part_name);

	if (!old_part_list)
		return;
	FREE_NULL_LIST(old_part_list);
}

/*
 * _preserve_select_type_param - preserve original plugin parameters.
 *	Daemons and/or commands must be restarted for some
 *	select plugin value changes to take effect.
 * RET zero or error code
 */
static int _preserve_select_type_param(slurm_conf_t *ctl_conf_ptr,
                                       uint16_t old_select_type_p)
{
	int rc = SLURM_SUCCESS;

	/* SelectTypeParameters cannot change */
	if (old_select_type_p) {
		if (old_select_type_p != ctl_conf_ptr->select_type_param) {
			ctl_conf_ptr->select_type_param = old_select_type_p;
			rc = ESLURM_INVALID_SELECTTYPE_CHANGE;
		}
	}
	return rc;
}

/* Start or stop the gang scheduler module as needed based upon changes in
 *	configuration */
static void _update_preempt(uint16_t old_preempt_mode)
{
	uint16_t new_preempt_mode = slurm_conf.preempt_mode;

	if ((old_preempt_mode & PREEMPT_MODE_GANG) ==
	    (new_preempt_mode & PREEMPT_MODE_GANG))
		return;
	/* GANG bits for old,new are either 0,1 or 1,0 */
	if (new_preempt_mode & PREEMPT_MODE_GANG) {
		info("Enabling gang scheduling");
		gs_init();
	} else {
		info("Disabling gang scheduling");
		gs_wake_jobs();
		gs_fini();
	}
}

/*
 * _sync_nodes_to_jobs - sync node state to job states on slurmctld restart.
 *	This routine marks nodes allocated to a job as busy no matter what
 *	the node's last saved state
 * RET count of nodes having state changed
 * Note: Operates on common variables, no arguments
 */
static int _sync_nodes_to_jobs(bool reconfig)
{
	job_record_t *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (!reconfig &&
		    job_ptr->details && job_ptr->details->prolog_running) {
			job_ptr->details->prolog_running = 0;
			if (IS_JOB_CONFIGURING(job_ptr)) {
				prolog_slurmctld(job_ptr);
				(void) bb_g_job_begin(job_ptr);
			}
		}

		if (job_ptr->node_bitmap == NULL)
			;
		else if (IS_JOB_RUNNING(job_ptr) || IS_JOB_COMPLETING(job_ptr))
			update_cnt += _sync_nodes_to_active_job(job_ptr);
		else if (IS_JOB_SUSPENDED(job_ptr))
			_sync_nodes_to_suspended_job(job_ptr);

	}
	list_iterator_destroy(job_iterator);

	if (update_cnt) {
		info("_sync_nodes_to_jobs updated state of %d nodes",
		     update_cnt);
	}
	sync_front_end_state();
	return update_cnt;
}

/* For jobs which are in state COMPLETING, deallocate the nodes and
 * issue the RPC to kill the job */
static int _sync_nodes_to_comp_job(void)
{
	job_record_t *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((job_ptr->node_bitmap) && IS_JOB_COMPLETING(job_ptr)) {

			/* If the controller is reconfiguring
			 * and the job is in completing state
			 * and the slurmctld epilog is already
			 * running which means deallocate_nodes()
			 * was alredy called, do invoke it again
			 * and don't start another epilog.
			 */
			if (job_ptr->epilog_running == true)
				continue;

			update_cnt++;
			info("%s: %pJ in completing state", __func__, job_ptr);
			if (!job_ptr->node_bitmap_cg)
				build_cg_bitmap(job_ptr);

			/* deallocate_nodes will remove this job from
			 * the system before it was added, so add it
			 * now
			 */
			if (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)
				acct_policy_job_begin(job_ptr);

			if (job_ptr->front_end_ptr)
				job_ptr->front_end_ptr->job_cnt_run++;
			deallocate_nodes(job_ptr, false, false, false);
			/* The job in completing state at slurmctld restart or
			 * reconfiguration, do not log completion again.
			 * job_completion_logger(job_ptr, false); */
		}
	}
	list_iterator_destroy(job_iterator);
	if (update_cnt)
		info("%s: completing %d jobs", __func__, update_cnt);
	return update_cnt;
}

/* Synchronize states of nodes and active jobs (RUNNING or COMPLETING state)
 * RET count of jobs with state changes */
static int _sync_nodes_to_active_job(job_record_t *job_ptr)
{
	int i, cnt = 0;
	uint32_t node_flags;
	node_record_t *node_ptr;

	if (job_ptr->node_bitmap_cg) /* job completing */
		job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap_cg);
	else
		job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap);
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (job_ptr->node_bitmap_cg) { /* job completing */
			if (!bit_test(job_ptr->node_bitmap_cg, node_ptr->index))
				continue;
		} else if (!bit_test(job_ptr->node_bitmap, node_ptr->index))
			continue;

		if ((job_ptr->details &&
		     (job_ptr->details->whole_node == WHOLE_NODE_USER)) ||
		    (job_ptr->part_ptr &&
		     (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER))) {
			node_ptr->owner_job_cnt++;
			node_ptr->owner = job_ptr->user_id;
		}

		if (slurm_mcs_get_select(job_ptr) == 1) {
			xfree(node_ptr->mcs_label);
			node_ptr->mcs_label = xstrdup(job_ptr->mcs_label);
		}

		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

		node_ptr->run_job_cnt++; /* NOTE:
				* This counter moved to comp_job_cnt
				* by _sync_nodes_to_comp_job() */
		if ((job_ptr->details) && (job_ptr->details->share_res == 0))
			node_ptr->no_share_job_cnt++;

		if (IS_NODE_DOWN(node_ptr)              &&
		    IS_JOB_RUNNING(job_ptr)             &&
		    (job_ptr->kill_on_node_fail == 0)   &&
		    (job_ptr->node_cnt > 1)) {
			/* This should only happen if a job was running
			 * on a node that was newly configured DOWN */
			int save_accounting_enforce;
			info("Removing failed node %s from %pJ",
			     node_ptr->name, job_ptr);
			/*
			 * Disable accounting here. Accounting reset for all
			 * jobs in _restore_job_accounting()
			 */
			save_accounting_enforce = accounting_enforce;
			accounting_enforce &= (~ACCOUNTING_ENFORCE_LIMITS);
			job_pre_resize_acctg(job_ptr);
			srun_node_fail(job_ptr, node_ptr->name);
			kill_step_on_node(job_ptr, node_ptr, true);
			excise_node_from_job(job_ptr, node_ptr);
			job_post_resize_acctg(job_ptr);
			accounting_enforce = save_accounting_enforce;
		} else if (IS_NODE_DOWN(node_ptr) && IS_JOB_RUNNING(job_ptr)) {
			info("Killing %pJ on DOWN node %s",
			     job_ptr, node_ptr->name);
			_abort_job(job_ptr, JOB_NODE_FAIL, FAIL_DOWN_NODE,
				   NULL);
			cnt++;
		} else if (IS_NODE_IDLE(node_ptr)) {
			cnt++;
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		}
	}

	if ((IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) &&
	    (job_ptr->front_end_ptr != NULL))
		job_ptr->front_end_ptr->job_cnt_run++;

	return cnt;
}

/* Synchronize states of nodes and suspended jobs */
static void _sync_nodes_to_suspended_job(job_record_t *job_ptr)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (bit_test(job_ptr->node_bitmap, node_ptr->index) == 0)
			continue;

		node_ptr->sus_job_cnt++;
	}
	return;
}

/*
 * Build license_list for every job.
 * Reset accounting for every job.
 * Reset the running job count for scheduling policy.
 * This must be called after load_all_resv_state() and restore_node_features().
 */
static void _restore_job_accounting(void)
{
	job_record_t *job_ptr;
	ListIterator job_iterator;
	bool valid = true;
	List license_list;

	assoc_mgr_clear_used_info();

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->array_recs)
			job_ptr->array_recs->tot_run_tasks = 0;
	}

	list_iterator_reset(job_iterator);
	while ((job_ptr = list_next(job_iterator))) {
		(void) build_feature_list(job_ptr, false);
		(void) build_feature_list(job_ptr, true);

		if (job_ptr->details->features_use ==
		    job_ptr->details->features)
			job_ptr->details->feature_list_use =
				job_ptr->details->feature_list;
		else if (job_ptr->details->features_use ==
			 job_ptr->details->prefer)
			job_ptr->details->feature_list_use =
				job_ptr->details->prefer_list;

		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			job_array_start(job_ptr);

		if (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) {
			if (!IS_JOB_FINISHED(job_ptr))
				acct_policy_add_job_submit(job_ptr);
			if (IS_JOB_RUNNING(job_ptr) ||
			    IS_JOB_SUSPENDED(job_ptr)) {
				acct_policy_job_begin(job_ptr);
				job_claim_resv(job_ptr);
			} else if (IS_JOB_PENDING(job_ptr) &&
				   job_ptr->details &&
				   job_ptr->details->accrue_time) {
				/*
				 * accrue usage was cleared above with
				 * assoc_mgr_clear_used_info(). Clear accrue
				 * time so that _handle_add_accrue() will add
				 * the usage back.
				 */
				time_t save_accrue_time =
					job_ptr->details->accrue_time;
				job_ptr->details->accrue_time = 0;
				acct_policy_add_accrue_time(job_ptr, false);
				if (job_ptr->details->accrue_time)
					job_ptr->details->accrue_time =
						save_accrue_time;
			}
		}

		license_list = license_validate(job_ptr->licenses, false, false,
						job_ptr->tres_req_cnt, &valid);
		FREE_NULL_LIST(job_ptr->license_list);
		if (valid) {
			job_ptr->license_list = license_list;
			xfree(job_ptr->licenses);
			job_ptr->licenses =
				license_list_to_string(license_list);
		}

		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			license_job_get(job_ptr);

	}
	list_iterator_destroy(job_iterator);
}

/* Flush accounting information on this cluster, then for each running or
 * suspended job, restore its state in the accounting system */
static void _acct_restore_active_jobs(void)
{
	job_record_t *job_ptr;
	ListIterator job_iterator;
	step_record_t *step_ptr;
	ListIterator step_iterator;

	info("Reinitializing job accounting state");
	acct_storage_g_flush_jobs_on_cluster(acct_db_conn,
					     time(NULL));
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_SUSPENDED(job_ptr))
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr)) {
			if (job_ptr->db_index != NO_VAL64)
				job_ptr->db_index = 0;
			step_iterator = list_iterator_create(
				job_ptr->step_list);
			while ((step_ptr = list_next(step_iterator))) {
				jobacct_storage_g_step_start(acct_db_conn,
							     step_ptr);
			}
			list_iterator_destroy (step_iterator);
		}
	}
	list_iterator_destroy(job_iterator);
}

/* _compare_hostnames()
 */
static int _compare_hostnames(node_record_t **old_node_table,
			      int old_node_count, node_record_t **node_table,
			      int node_count)
{
	int cc;
	int set_size;
	char *old_ranged;
	char *ranged;
	hostset_t old_set;
	hostset_t set;

	/*
	 * Don't compare old DYNAMIC_NORM nodes because they don't rely on
	 * fanout communications. Plus they haven't been loaded from state yet
	 * into the new node_record_table_ptr.
	 */
	old_set = hostset_create("");
	for (cc = 0; cc < old_node_count; cc++)
		if (old_node_table[cc] &&
		    !IS_NODE_DYNAMIC_NORM(old_node_table[cc]))
			hostset_insert(old_set, old_node_table[cc]->name);

	set = hostset_create("");
	for (cc = 0; cc < node_count; cc++)
		if (node_table && node_table[cc])
			hostset_insert(set, node_table[cc]->name);

	set_size = HOST_NAME_MAX * node_count + node_count + 1;

	old_ranged = xmalloc(set_size);
	ranged = xmalloc(set_size);

	hostset_ranged_string(old_set, set_size, old_ranged);
	hostset_ranged_string(set, set_size, ranged);

	if (hostset_count(old_set) != hostset_count(set)) {
		error("%s: node count has changed before reconfiguration "
		      "from %d to %d. You have to restart slurmctld.",
		      __func__, hostset_count(old_set), hostset_count(set));
		return -1;
	}

	cc = 0;
	if (xstrcmp(old_ranged, ranged) != 0) {
		error("%s: node names changed before reconfiguration. "
		      "You have to restart slurmctld.", __func__);
		cc = -1;
	}

	hostset_destroy(old_set);
	hostset_destroy(set);
	xfree(old_ranged);
	xfree(ranged);

	return cc;
}

extern int dump_config_state_lite(void)
{
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	buf_t *buffer = init_buf(high_buffer_size);

	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	packstr(slurm_conf.accounting_storage_type, buffer);

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/last_config_lite",
	                          slurm_conf.state_save_location);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);

	END_TIMER2("dump_config_state_lite");
	return error_code;

}

extern int load_config_state_lite(void)
{
	uint32_t uint32_tmp = 0;
	uint16_t ver = 0;
	char *state_file;
	buf_t *buffer;
	time_t buf_time;
	char *last_accounting_storage_type = NULL;

	/* Always ignore .old file */
	state_file = xstrdup_printf("%s/last_config_lite",
	                            slurm_conf.state_save_location);

	//info("looking at the %s file", state_file);
	if (!(buffer = create_mmap_buf(state_file))) {
		debug2("No last_config_lite file (%s) to recover", state_file);
		xfree(state_file);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpack16(&ver, buffer);
	debug3("Version in last_conf_lite header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		if (!ignore_state_errors)
			fatal("Can not recover last_conf_lite, incompatible version, (%u not between %d and %d), start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.",
			      ver, SLURM_MIN_PROTOCOL_VERSION,
			      SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		error("Can not recover last_conf_lite, incompatible version, "
		      "(%u not between %d and %d)",
		      ver, SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	} else {
		safe_unpack_time(&buf_time, buffer);
		safe_unpackstr_xmalloc(&last_accounting_storage_type,
				       &uint32_tmp, buffer);
	}
	xassert(slurm_conf.accounting_storage_type);

	if (last_accounting_storage_type
	    && !xstrcmp(last_accounting_storage_type,
	                slurm_conf.accounting_storage_type))
		slurmctld_init_db = 0;
	xfree(last_accounting_storage_type);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete last_config_lite checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete last_config_lite checkpoint file");
	free_buf(buffer);

	return SLURM_ERROR;
}
