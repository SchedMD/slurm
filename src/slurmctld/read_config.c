/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/port_mgr.h"
#include "src/common/read_config.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/state_save.h"
#include "src/common/strnatcmp.h"
#include "src/common/xstring.h"

#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/job_submit.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/preempt.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/sched_plugin.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"

#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

/* Global variables */
bool slurmctld_init_db = true;

static void _acct_restore_active_jobs(void);
static void _build_bitmaps(void);
static void _gres_reconfig(void);
static void _init_all_slurm_conf(void);
static int _preserve_select_type_param(slurm_conf_t *ctl_conf_ptr,
                                       uint16_t old_select_type_p);
static int  _reset_node_bitmaps(void *x, void *arg);
static void _restore_job_accounting();

static void _set_features(node_record_t **old_node_table_ptr,
			  int old_node_record_count, int recover);
static void _stat_slurm_dirs(void);
static int  _sync_nodes_to_comp_job(void);
static int _sync_nodes_to_jobs(void);
static int  _sync_nodes_to_active_job(job_record_t *job_ptr);
static void _sync_nodes_to_suspended_job(job_record_t *job_ptr);
static void _sync_part_prio(void);

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
		error("###       SEVERE SECURITY VULNERABILTY       ###");
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

	return slurm_sort_uint32_list_asc(&n1->node_rank, &n2->node_rank);
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

	if (topology_g_generate_node_ranking())
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

static void _add_nodes_with_feature(hostlist_t *hl, char *feature)
{
	node_record_t *node_ptr;
	bitstr_t *tmp_bitmap = bit_alloc(node_record_count);

	add_nodes_with_feature_to_bitmap(tmp_bitmap, feature);
	for (int i = 0; (node_ptr = next_node_bitmap(tmp_bitmap, &i)); i++) {
		hostlist_push_host(hl, node_ptr->name);
	}

	FREE_NULL_BITMAP(tmp_bitmap);
}

static void _add_all_nodes_to_hostlist(hostlist_t *hl)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node(&i)); i++)
		hostlist_push_host(hl, node_ptr->name);
}

extern hostlist_t *nodespec_to_hostlist(const char *nodes, bool uniq,
					char **nodesets)
{
	int count;
	slurm_conf_nodeset_t *ptr, **ptr_array;
	hostlist_t *hl;

	if (nodesets)
		xfree(*nodesets);

	if (!xstrcasecmp(nodes, "ALL")) {
		if (!(hl = hostlist_create(NULL))) {
			error("%s: hostlist_create() error for %s", __func__, nodes);
			return NULL;
		}
		_add_all_nodes_to_hostlist(hl);
		if (nodesets)
			*nodesets = xstrdup("ALL");
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

			/* Handle keywords for Nodes= in a NodeSet */
			if (!xstrcasecmp(ptr->nodes, "ALL")) {
				_add_all_nodes_to_hostlist(hl);
			} else if (ptr->nodes) {
				hostlist_push(hl, ptr->nodes);
			}
		}
	}

	if (xstrchr(nodes, '{'))
		parse_hostlist_functions(&hl);

	if (uniq)
		hostlist_uniq(hl);
	return hl;
}

static void _init_bitmaps(void)
{
	/* initialize the idle and up bitmaps */
	FREE_NULL_BITMAP(asap_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(bf_ignore_node_bitmap);
	FREE_NULL_BITMAP(booting_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(cloud_node_bitmap);
	FREE_NULL_BITMAP(future_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_down_node_bitmap);
	FREE_NULL_BITMAP(power_up_node_bitmap);
	FREE_NULL_BITMAP(rs_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	asap_node_bitmap = bit_alloc(node_record_count);
	avail_node_bitmap = bit_alloc(node_record_count);
	bf_ignore_node_bitmap = bit_alloc(node_record_count);
	booting_node_bitmap = bit_alloc(node_record_count);
	cg_node_bitmap = bit_alloc(node_record_count);
	cloud_node_bitmap = bit_alloc(node_record_count);
	future_node_bitmap = bit_alloc(node_record_count);
	idle_node_bitmap = bit_alloc(node_record_count);
	power_down_node_bitmap = bit_alloc(node_record_count);
	power_up_node_bitmap = bit_alloc(node_record_count);
	rs_node_bitmap = bit_alloc(node_record_count);
	share_node_bitmap = bit_alloc(node_record_count);
	up_node_bitmap = bit_alloc(node_record_count);
}

static void _build_part_bitmaps(void)
{
	part_record_t *part_ptr;
	list_itr_t *part_iterator;

	/* scan partition table and identify nodes in each */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = list_next(part_iterator))) {
		if (build_part_bitmap(part_ptr) == ESLURM_INVALID_NODE_NAME)
			fatal("Invalid node names in partition %s",
					part_ptr->name);
	}
	list_iterator_destroy(part_iterator);
}

static void _build_node_config_bitmaps(void)
{
	node_record_t *node_ptr;

	/* initialize the configuration bitmaps */
	list_for_each(config_list, _reset_node_bitmaps, NULL);

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->config_ptr)
			bit_set(node_ptr->config_ptr->node_bitmap,
				node_ptr->index);
	}
}

static int _reset_node_bitmaps(void *x, void *arg)
{
	config_record_t *config_ptr = x;

	FREE_NULL_BITMAP(config_ptr->node_bitmap);
	config_ptr->node_bitmap = bit_alloc(node_record_count);

	return 0;
}

static int _set_share_node_bitmap(void *x, void *arg)
{
	job_record_t *job_ptr = x;

	if (!IS_JOB_RUNNING(job_ptr) ||
	    (job_ptr->node_bitmap == NULL)        ||
	    (job_ptr->details     == NULL)        ||
	    (job_ptr->details->share_res != 0))
		return 0;

	bit_and_not(share_node_bitmap, job_ptr->node_bitmap);

	return 0;
}

#ifndef HAVE_FRONT_END
static void *_set_node_addrs(void *arg)
{
	list_t *nodes = arg;
	slurm_addr_t slurm_addr;
	node_record_t *node_ptr;

	while ((node_ptr = list_pop(nodes))) {
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

	return NULL;
}
#endif

/*
 * Validate that nodes are addressable.
 */
static void _validate_slurmd_addr(void)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	DEF_TIMERS;
	pthread_t *work_threads;
	int threads_num = 1;
	char *temp_str;
	list_t *nodes = list_create(NULL);
	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	START_TIMER;

	if ((temp_str = xstrcasestr(slurm_conf.slurmctld_params,
				    "validate_nodeaddr_threads="))) {
		int tmp_val = strtol(temp_str + 26, NULL, 10);
		if ((tmp_val >= 1) && (tmp_val <= 64))
			threads_num = tmp_val;
		else
			error("SlurmctldParameters option validate_nodeaddr_threads=%d out of range, ignored",
			      tmp_val);
	}


	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERING_DOWN(node_ptr) ||
		     IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_UP(node_ptr)))
				continue;
		if (node_ptr->port == 0)
			node_ptr->port = slurm_conf.slurmd_port;
		list_append(nodes, node_ptr);
	}

	work_threads = xcalloc(threads_num, sizeof(pthread_t));
	for (int i = 0; i < threads_num; i++)
		slurm_thread_create(&work_threads[i], _set_node_addrs, nodes);
	for (int i = 0; i < threads_num; i++)
		slurm_thread_join(work_threads[i]);
	xfree(work_threads);
	xassert(list_is_empty(nodes));
	FREE_NULL_LIST(nodes);

	END_TIMER2(__func__);
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
		if (!IS_NODE_FUTURE(node_ptr))
			bit_set(power_up_node_bitmap, node_ptr->index);

		if ((IS_NODE_IDLE(node_ptr) && (job_cnt == 0)) ||
		    IS_NODE_DOWN(node_ptr))
			bit_set(idle_node_bitmap, node_ptr->index);
		if (IS_NODE_POWERING_UP(node_ptr))
			bit_set(booting_node_bitmap, node_ptr->index);
		if (IS_NODE_COMPLETING(node_ptr))
			bit_set(cg_node_bitmap, node_ptr->index);
		if (IS_NODE_CLOUD(node_ptr))
			bit_set(cloud_node_bitmap, node_ptr->index);
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
		if (IS_NODE_POWERED_DOWN(node_ptr)) {
			bit_set(power_down_node_bitmap, node_ptr->index);
			bit_clear(power_up_node_bitmap, node_ptr->index);
		}
		if (IS_NODE_POWERING_DOWN(node_ptr)) {
			bit_set(power_down_node_bitmap, node_ptr->index);
			bit_clear(power_up_node_bitmap, node_ptr->index);
			bit_clear(avail_node_bitmap, node_ptr->index);
		}
		if (IS_NODE_FUTURE(node_ptr))
			bit_set(future_node_bitmap, node_ptr->index);

		if ((IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		     IS_NODE_REBOOT_ISSUED(node_ptr)) &&
		    ((node_ptr->next_state & NODE_STATE_FLAGS) & NODE_RESUME))
			bit_set(rs_node_bitmap, node_ptr->index);

		if (IS_NODE_REBOOT_ASAP(node_ptr))
			bit_set(asap_node_bitmap, node_ptr->index);
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
	hostlist_t *alias_list = NULL;
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

/*
 * Convert a comma delimited string of account names into a list containing
 * pointers to those associations.
 */
extern list_t *accounts_list_build(char *accounts, bool locked)
{
	char *tmp_accts, *one_acct_name, *name_ptr = NULL;
	list_t *acct_list = NULL;
	slurmdb_assoc_rec_t *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK };

	if (!accounts)
		return acct_list;

	if (!locked)
		assoc_mgr_lock(&locks);
	tmp_accts = xstrdup(accounts);
	one_acct_name = strtok_r(tmp_accts, ",", &name_ptr);
	while (one_acct_name) {
		slurmdb_assoc_rec_t assoc = {
			.acct = one_acct_name,
			.uid = NO_VAL,
		};

		if (assoc_mgr_fill_in_assoc(
			    acct_db_conn, &assoc,
			    accounting_enforce,
			    &assoc_ptr, true) != SLURM_SUCCESS) {
			if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) {
				error("%s: No association for account %s",
				      __func__, assoc.acct);
			} else {
				verbose("%s: No association for account %s",
					__func__, assoc.acct);
			}

		}
		if (assoc_ptr) {
			if (!acct_list)
				acct_list = list_create(NULL);
			list_append(acct_list, assoc_ptr);
		}

		one_acct_name = strtok_r(NULL, ",", &name_ptr);
	}
	xfree(tmp_accts);
	if (!locked)
		assoc_mgr_unlock(&locks);
	return acct_list;
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

	part_ptr = create_ctld_part_record(part->name);

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
		if (slurm_conf.conf_flags & CONF_FLAG_DRJ)
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
	if (part->exclusive_topo)
		part_ptr->flags |= PART_FLAG_EXCLUSIVE_TOPO;
	if (part->hidden_flag)
		part_ptr->flags |= PART_FLAG_HIDDEN;
	if (part->power_down_on_idle)
		part_ptr->flags |= PART_FLAG_PDOI;
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
	part_ptr->max_cpus_per_socket = part->max_cpus_per_socket;
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
		part_ptr->allow_accts_list =
			accounts_list_build(part_ptr->allow_accounts, false);
	}

	if (part->allow_qos) {
		part_ptr->allow_qos = xstrdup(part->allow_qos);
		qos_list_build(part_ptr->allow_qos,&part_ptr->allow_qos_bitstr);
	}

	if (part->deny_accounts) {
		part_ptr->deny_accounts = xstrdup(part->deny_accounts);
		part_ptr->deny_accts_list =
			accounts_list_build(part_ptr->deny_accounts, false);
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
		if (part_ptr->qos_ptr) {
			if ((part_ptr->qos_ptr->flags & QOS_FLAG_PART_QOS) &&
			    (part_ptr->qos_ptr->flags & QOS_FLAG_RELATIVE))
				fatal("QOS %s is a relative QOS. A relative QOS must be unique per partition. Please check your configuration and adjust accordingly",
				      part_ptr->qos_ptr->name);
			part_ptr->qos_ptr->flags |= QOS_FLAG_PART_QOS;
		}
	}

	return 0;
}

/*
 * _build_all_partitionline_info - get a array of slurm_conf_partition_t
 *	structures from the slurm.conf reader, build table, and set values
 * Note: Operates on common variables
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
static void _build_all_partitionline_info(void)
{
	slurm_conf_partition_t **ptr_array;
	int count;
	int i;

	count = slurm_conf_partition_array(&ptr_array);

	for (i = 0; i < count; i++)
		_build_single_partitionline_info(ptr_array[i]);
}

static int _set_max_part_prio(void *x, void *arg)
{
	part_record_t *part_ptr = x;

	if (part_ptr->priority_job_factor > part_max_priority)
		part_max_priority = part_ptr->priority_job_factor;

	return 0;
}

static int _reset_part_prio(void *x, void *arg)
{
	part_record_t *part_ptr = x;

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
	job_state_unset_flag(job_ptr, JOB_REQUEUE);

	return rc;
}

static void _requeue_job_node_failed(void)
{
	xassert(job_list);

	(void) list_for_each_nobreak(job_list,
				     _foreach_requeue_job_node_failed, NULL);
}

static void _abort_job(job_record_t *job_ptr, uint32_t job_state,
		       uint16_t state_reason, char *reason_string)
{
	time_t now = time(NULL);

	job_state_set(job_ptr, (job_state | JOB_COMPLETING));
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
	job_record_t *job_ptr = x;
	job_ptr->bit_flags &= (~HET_JOB_FLAG);
	return 0;
}

static int _mark_het_job_used(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	job_ptr->bit_flags |= HET_JOB_FLAG;
	return 0;
}

static int _test_het_job_used(void *x, void *arg)
{
	job_record_t *job_ptr = x;

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
	list_itr_t *job_iterator;
	job_record_t *job_ptr, *het_job_ptr;
	hostset_t *hs;
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
			if (job_ptr->het_job_id &&
			    (job_ptr->job_id == job_ptr->het_job_id)) {
				error("Invalid HetJob component %pJ HetJobIdSet=%s. Aborting and removing job.",
				      job_ptr,
				      job_ptr->het_job_id_set);
				_abort_job(job_ptr, JOB_FAILED, FAIL_SYSTEM,
					   "Invalid HetJob component");
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
	list_itr_t *step_iterator;
	step_record_t *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state < JOB_RUNNING)
			continue;
		FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
		if (step_ptr->step_layout &&
		    step_ptr->step_layout->node_list &&
		    (node_name2bitmap(step_ptr->step_layout->node_list, false,
				      &step_ptr->step_node_bitmap, NULL))) {
			error("Invalid step_node_list (%s) for %pS",
			      step_ptr->step_layout->node_list, step_ptr);
			delete_step_record(job_ptr, step_ptr);
		} else if (step_ptr->step_node_bitmap == NULL) {
			error("Missing node_list for %pS", step_ptr);
			delete_step_record(job_ptr, step_ptr);
		}
	}

	list_iterator_destroy (step_iterator);
}

static int _sync_detail_bitmaps(job_record_t *job_ptr)
{
	if (job_ptr->details == NULL)
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);

	if ((job_ptr->details->req_nodes) &&
	    (node_name2bitmap(job_ptr->details->req_nodes, false,
			      &job_ptr->details->req_node_bitmap, NULL))) {
		error("Invalid req_nodes (%s) for %pJ",
		      job_ptr->details->req_nodes, job_ptr);
		return SLURM_ERROR;
	}

	/*
	 * Ignore any errors if the exc_nodes list contains invalid entries.
	 * We can the pretty sure we won't schedule onto nodes that don't exist.
	 */
	FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	if (job_ptr->details->exc_nodes)
		node_name2bitmap(job_ptr->details->exc_nodes, false,
				 &job_ptr->details->exc_node_bitmap, NULL);

	/*
	 * If a nodelist has been provided with more nodes than are required
	 * for the job, translate this into an exclusion of all nodes except
	 * those requested.
	 */
	if (job_ptr->details->req_node_bitmap &&
	    (bit_set_count(job_ptr->details->req_node_bitmap) >
	     job_ptr->details->min_nodes)) {
		if (!job_ptr->details->exc_node_bitmap)
			job_ptr->details->exc_node_bitmap =
				bit_alloc(node_record_count);
		bit_or_not(job_ptr->details->exc_node_bitmap,
			   job_ptr->details->req_node_bitmap);
		FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
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
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	part_record_t *part_ptr;
	list_t *part_ptr_list = NULL;
	bool job_fail = false;
	time_t now = time(NULL);
	bool gang_flag = false;

	xassert(job_list);

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
			get_part_list(job_ptr->partition, &part_ptr_list,
				      &part_ptr, &err_part);
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
				     false,  &job_ptr->node_bitmap_cg, NULL)) {
			error("Invalid nodes_completing (%s) for %pJ",
			      job_ptr->nodes_completing, job_ptr);
			job_fail = true;
		}
		FREE_NULL_BITMAP(job_ptr->node_bitmap);
		if (job_ptr->nodes &&
		    node_name2bitmap(job_ptr->nodes, false,
				     &job_ptr->node_bitmap, NULL)) {
			error("Invalid nodes (%s) for %pJ",
			      job_ptr->nodes, job_ptr);
			job_fail = true;
		}
		FREE_NULL_BITMAP(job_ptr->node_bitmap_pr);
#ifndef HAVE_FRONT_END
		if (job_ptr->nodes_pr &&
		    node_name2bitmap(job_ptr->nodes_pr, false,
				     &job_ptr->node_bitmap_pr, NULL)) {
			error("Invalid nodes_pr (%s) for %pJ",
			      job_ptr->nodes_pr, job_ptr);
			job_fail = true;
		}
#endif
		if (reset_node_bitmap(job_ptr))
			job_fail = true;
		if (!job_fail &&
		    job_ptr->job_resrcs &&
		    (slurm_select_cr_type() || gang_flag) &&
		    valid_job_resources(job_ptr->job_resrcs)) {
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
				job_state_set(job_ptr, JOB_NODE_FAIL);
			} else if (IS_JOB_RUNNING(job_ptr)) {
				job_ptr->end_time = time(NULL);
				job_state_set(job_ptr, (JOB_NODE_FAIL |
							JOB_COMPLETING));
				build_cg_bitmap(job_ptr);
				was_running = true;
			} else if (IS_JOB_SUSPENDED(job_ptr)) {
				job_ptr->end_time = job_ptr->suspend_time;
				job_state_set(job_ptr, (JOB_NODE_FAIL |
							JOB_COMPLETING));
				build_cg_bitmap(job_ptr);
				job_ptr->tot_sus_time +=
					difftime(now, job_ptr->suspend_time);
				jobacct_storage_g_job_suspend(acct_db_conn,
							      job_ptr);
				was_running = true;
			}
			job_ptr->state_reason = FAIL_DOWN_NODE;
			xfree(job_ptr->state_desc);
			job_ptr->exit_code = 1;
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
				job_state_set_flag(job_ptr, JOB_REQUEUE);

				/* Reset node_cnt to exclude vanished nodes */
				job_ptr->node_cnt = bit_set_count(
					job_ptr->node_bitmap_cg);
				/* Reset exit code from last run */
				job_ptr->exit_code = 0;
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
 * RET SLURM_SUCCESS if no error, otherwise an error code
 * Note: Operates on common variables only
 */
extern int read_slurm_conf(int recover)
{
	DEF_TIMERS;
	int error_code = SLURM_SUCCESS;
	int rc = 0, load_job_ret = SLURM_SUCCESS;
	char *old_auth_type = xstrdup(slurm_conf.authtype);
	char *old_bb_type = xstrdup(slurm_conf.bb_type);
	char *old_cred_type = xstrdup(slurm_conf.cred_type);
	char *old_job_container_type = xstrdup(slurm_conf.job_container_plugin);
	char *old_preempt_type = xstrdup(slurm_conf.preempt_type);
	char *old_sched_type = xstrdup(slurm_conf.schedtype);
	char *old_select_type = xstrdup(slurm_conf.select_type);
	char *old_switch_type = xstrdup(slurm_conf.switch_type);
	char *state_save_dir = xstrdup(slurm_conf.state_save_location);
	char *tmp_ptr = NULL;
	uint16_t old_select_type_p = slurm_conf.select_type_param;
	bool cgroup_mem_confinement = false;
	uint16_t reconfig_flags = slurm_conf.reconfig_flags;

	/* initialization */
	START_TIMER;

	_init_all_slurm_conf();

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

	if (topology_g_init() != SLURM_SUCCESS)
		fatal("Failed to initialize topology plugin");

	if (xstrcasestr(slurm_conf.slurmctld_params, "enable_stepmgr") &&
	    !(slurm_conf.prolog_flags & PROLOG_FLAG_CONTAIN))
		fatal("STEP_MGR not supported without PrologFlags=contain");

	/* Build node and partition information based upon slurm.conf file */
	if ((error_code = build_all_nodeline_info(false, slurmctld_tres_cnt)))
	    goto end_it;
	/* Increase node table to handle dynamic nodes. */
	if ((slurm_conf.max_node_cnt != NO_VAL) &&
	    node_record_count < slurm_conf.max_node_cnt) {
		node_record_count = slurm_conf.max_node_cnt;
		grow_node_record_table_ptr();
	} else {
		/* Lock node_record_table_ptr from growing */
		slurm_conf.max_node_cnt = node_record_count;
	}
	if (slurm_conf.max_node_cnt == 0) {
		/*
		 * Set to 1 so bitmaps will be created but don't allow any nodes
		 * to be created.
		 */
		node_record_count = 1;
		grow_node_record_table_ptr();
	}

	bit_cache_init(node_record_count);

	(void)acct_storage_g_reconfig(acct_db_conn, 0);
	build_all_frontend_info(false);
	_handle_all_downnodes();
	_build_all_partitionline_info();
	restore_front_end_state(recover);

	/*
	 * Currently load/dump_state_lite has to run before load_all_job_state.
	 * FIXME: this stores a single string, this should probably move into
	 * the job state file as it's only pertinent to job accounting.
	 */
	load_config_state_lite();
	dump_config_state_lite();

	update_logging();
	if (jobcomp_g_init() != SLURM_SUCCESS)
		fatal("Failed to initialize jobcomp plugin");
	if (controller_init_scheduling(
		(slurm_conf.preempt_mode & PREEMPT_MODE_GANG)) != SLURM_SUCCESS) {
		fatal("Failed to initialize the various schedulers");
	}

	if (default_part_loc == NULL)
		error("%s: default partition not set.", __func__);

	if (node_record_count < 1) {
		error("%s: no nodes configured.", __func__);
		error_code = EINVAL;
		goto end_it;
	}

	/*
	 * Node reordering may be done by the topology plugin.
	 * Reordering the table must be done before hashing the
	 * nodes, and before any position-relative bitmaps are created.
	 *
	 * Sort the nodes read in from the slurm.conf first before restoring
	 * the dynamic nodes from the state file to prevent dynamic nodes from
	 * being sorted -- which can cause problems with heterogenous jobs and
	 * the order of the sockets changing on startup.
	 */
	_sort_node_record_table_ptr();

	/*
	 * Load node state which includes dynamic nodes so that dynamic nodes
	 * can be included in topology.
	 */
	if (recover == 0) {		/* Build everything from slurm.conf */
		_set_features(node_record_table_ptr, node_record_count,
			      recover);
	} else if (recover == 1) {	/* Load job & node state files */
		(void) load_all_node_state(true);
		_set_features(node_record_table_ptr, node_record_count,
			      recover);
		(void) load_all_front_end_state(true);
	} else if (recover > 1) {	/* Load node, part & job state files */
		(void) load_all_node_state(false);
		_set_features(NULL, 0, recover);
		(void) load_all_front_end_state(false);
	}

	rehash_node();
	topology_g_build_config();

	rehash_jobs();
	_validate_slurmd_addr();

	_stat_slurm_dirs();

	_init_bitmaps();

	/*
	 * Set standard features and preserve the plugin controlled ones.
	 */
	if (recover == 0) {		/* Build everything from slurm.conf */
		load_last_job_id();
		reset_first_job_id();
		controller_reconfig_scheduling();
	} else if (recover == 1) {	/* Load job & node state files */
		load_job_ret = load_all_job_state();
	} else if (recover > 1) {	/* Load node, part & job state files */
		reconfig_flags |= RECONFIG_KEEP_PART_INFO;
		load_job_ret = load_all_job_state();
	}
	(void) load_all_part_state(reconfig_flags);

	/*
	 * _build_node_config_bitmaps() must be called before
	 * build_features_list_*() and before restore_node_features()
	 */
	_build_node_config_bitmaps();
	/* _gres_reconfig needs to happen before restore_node_features */
	_gres_reconfig();
	/* NOTE: Run restore_node_features before _restore_job_accounting */
	restore_node_features(recover);

	if ((node_features_g_count() > 0) &&
	    (node_features_g_get_node(NULL) != SLURM_SUCCESS))
		error("failed to initialize node features");

	/*
	 * _build_bitmaps() must follow node_features_g_get_node() and
	 * precede build_features_list_*()
	 */
	_build_bitmaps();

	/* Active and available features can be different on -R */
	if ((node_features_g_count() == 0) && (recover != 2))
		node_features_build_list_eq();
	else
		node_features_build_list_ne();

	_sync_part_prio();
	_build_part_bitmaps(); /* Must be called after build_feature_list_*() */

	if ((select_g_node_init() != SLURM_SUCCESS) ||
	    (select_g_state_restore(state_save_dir) != SLURM_SUCCESS) ||
	    (select_g_job_init(job_list) != SLURM_SUCCESS))
		fatal("Failed to initialize node selection plugin state, Clean start required.");

	/*
	 * config_power_mgr() Must be after node and partitions have been loaded
	 * and before any calls to power_save_test().
	 */
	config_power_mgr();

	_sync_jobs_to_conf();		/* must follow select_g_job_init() */

	/*
	 * The burst buffer plugin must be initialized and state loaded before
	 * _sync_nodes_to_jobs(), which calls bb_g_job_init().
	 */
	rc = bb_g_load_state(true);
	error_code = MAX(error_code, rc);	/* not fatal */

	(void) _sync_nodes_to_jobs();
	(void) sync_job_files();

	reserve_port_config(slurm_conf.mpi_params, job_list);

	if (license_update(slurm_conf.licenses) != SLURM_SUCCESS)
		fatal("Invalid Licenses value: %s", slurm_conf.licenses);

	init_requeue_policy();
	init_depend_policy();

	/*
	 * Must be at after nodes and partitions (e.g.
	 * _build_part_bitmaps()) have been created and before
	 * _sync_nodes_to_comp_job().
	 */
	set_cluster_tres(false);

	_validate_het_jobs();
	(void) _sync_nodes_to_comp_job();/* must follow select_g_node_init() */
	_requeue_job_node_failed();
	load_part_uid_allow_list(true);

	/* NOTE: Run load_all_resv_state() before _restore_job_accounting */
	load_all_resv_state(recover);
	if (recover >= 1) {
		trigger_state_restore();
		controller_reconfig_scheduling();
	}

	_restore_job_accounting();

	/* sort config_list by weight for scheduling */
	list_sort(config_list, &list_compare_config);

	/* Update plugins as possible */
	if (xstrcmp(old_auth_type, slurm_conf.authtype)) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = old_auth_type;
		old_auth_type = NULL;
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

	if (xstrcmp(old_job_container_type, slurm_conf.job_container_plugin)) {
		xfree(slurm_conf.job_container_plugin);
		slurm_conf.job_container_plugin = old_job_container_type;
		old_job_container_type = NULL;
		rc =  ESLURM_INVALID_JOB_CONTAINER_CHANGE;
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
		(void) preempt_g_fini();
		if (preempt_g_init() != SLURM_SUCCESS)
			fatal("failed to initialize preempt plugin");
	}

	/* Update plugin parameters as possible */
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

	_set_response_cluster_rec();

	consolidate_config_list(true, true);
	cloud_dns = xstrcasestr(slurm_conf.slurmctld_params, "cloud_dns");
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "max_powered_nodes="))) {
		max_powered_nodes =
			strtol(tmp_ptr + strlen("max_powered_nodes="),
			       NULL, 10);
	}

	slurm_conf.last_update = time(NULL);
end_it:
	xfree(old_auth_type);
	xfree(old_bb_type);
	xfree(old_cred_type);
	xfree(old_job_container_type);
	xfree(old_preempt_type);
	xfree(old_sched_type);
	xfree(old_select_type);
	xfree(old_switch_type);
	xfree(state_save_dir);

	END_TIMER2(__func__);
	return error_code;

}

static void _gres_reconfig(void)
{
	node_record_t *node_ptr;
	char *gres_name;
	int i;

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
		if (gres_g_node_config_load(node_ptr->config_ptr->cpus,
					    node_ptr->name, node_ptr->gres_list,
					    NULL, NULL) != SLURM_SUCCESS)
			continue; /* No need to validate if load failed */

		gres_node_config_validate(
			node_ptr->name, node_ptr->config_ptr->gres,
			&node_ptr->gres, &node_ptr->gres_list,
			node_ptr->config_ptr->threads,
			node_ptr->config_ptr->cores,
			node_ptr->config_ptr->tot_sockets,
			slurm_conf.conf_flags & CONF_FLAG_OR, NULL);
	}
}

/*
 * Append changeable features in old_features and not in features to features.
 */
static void _merge_changeable_features(char *old_features, char **features)
{
	char *save_ptr_old = NULL;
	char *tok_old, *tmp_old, *tok_new;
	char *sep;

	if (*features)
		sep = ",";
	else
		sep = "";

	/* Merge features strings, skipping duplicates */
	tmp_old = xstrdup(old_features);
	for (tok_old = strtok_r(tmp_old, ",", &save_ptr_old);
	     tok_old;
	     tok_old = strtok_r(NULL, ",", &save_ptr_old)) {
		bool match = false;

		if (!node_features_g_changeable_feature(tok_old))
			continue;

		if (*features) {
			char *tmp_new, *save_ptr_new = NULL;

			/* Check if old feature already exists in features string */
			tmp_new = xstrdup(*features);
			for (tok_new = strtok_r(tmp_new, ",", &save_ptr_new);
			     tok_new;
			     tok_new = strtok_r(NULL, ",", &save_ptr_new)) {
				if (!xstrcmp(tok_old, tok_new)) {
					match = true;
					break;
				}
			}
			xfree(tmp_new);
		}

		if (match)
			continue;

		xstrfmtcat(*features, "%s%s", sep, tok_old);
		sep = ",";
	}
	xfree(tmp_old);
}

static void _preserve_active_features(const char *available,
				      const char *old_active,
				      char **active)
{
	char *old_feature, *saveptr_old;
	char *tmp_old_active;

	if (!available || !old_active)
		return;

	tmp_old_active = xstrdup(old_active);
	for (old_feature = strtok_r(tmp_old_active, ",", &saveptr_old);
	     old_feature;
	     old_feature = strtok_r(NULL, ",", &saveptr_old)) {
		char *new_feature, *saveptr_avail;
		char *tmp_avail;

		if (!node_features_g_changeable_feature(old_feature))
			continue;

		tmp_avail = xstrdup(available);
		for (new_feature = strtok_r(tmp_avail, ",", &saveptr_avail);
		     new_feature;
		     new_feature = strtok_r(NULL, ",", &saveptr_avail)) {
			if (!xstrcmp(old_feature, new_feature)) {
				xstrfmtcat(*active, "%s%s",
					   *active ? "," : "", old_feature);
				break;
			}
		}
		xfree(tmp_avail);
	}
	xfree(tmp_old_active);
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
	int i, node_features_cnt = node_features_g_count();

	for (i = 0; i < old_node_record_count; i++) {
		char *old_features_act;

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

		/* No changeable features so active == available */
		if (node_features_cnt == 0) {
			xfree(node_ptr->features_act);
			node_ptr->features_act = xstrdup(node_ptr->features);
			continue;
		}

		/* If we are here, there's a node_features plugin active */

		/*
		 * Changeable features may be listed in the slurm.conf along
		 * with the non-changeable features (e.g. cloud nodes). So
		 * filter out the changeable features and leave only the
		 * non-changeable features. non-changeable features are active
		 * by default.
		 */
		old_features_act = node_ptr->features_act;
		node_ptr->features_act =
			filter_out_changeable_features(node_ptr->features);

		/*
		 * Preserve active features on startup but make sure they are a
		 * subset of available features -- in case available features
		 * were changed.
		 *
		 * features_act has all non-changeable features now. We need to
		 * add back previous active features that are in available
		 * features.
		 *
		 * For cloud nodes, changeable features are added in slurm.conf.
		 * This will preserve the cloud active features on startup. When
		 * changeable features aren't defined in slurm.conf then
		 * features_act will be reset to all non-changeable features
		 * read in from slurm.conf and will expect to get the available
		 * and active features from the slurmd.
		 */
		_preserve_active_features(node_ptr->features, old_features_act,
					  &node_ptr->features_act);
		xfree(old_features_act);

		/*
		 * On startup, node_record_table_ptr is passed as
		 * old_node_table_ptr so no need to merge features.
		 */
		if (node_ptr == old_node_ptr)
			continue;

		/*
		 * The subset of plugin-controlled features_available
		 * and features_active found in the old node_ptr for this node
		 * are copied into new node respective fields.
		 * This will make that KNL modes are preserved while doing a
		 * reconfigure. Otherwise, we should wait until node is
		 * registered to get KNL available and active features.
		 */
		if (old_node_ptr->features != NULL) {
			_merge_changeable_features(old_node_ptr->features,
						   &node_ptr->features);
		}

		if (old_node_ptr->features_act != NULL) {
			_merge_changeable_features(old_node_ptr->features_act,
						   &node_ptr->features_act);
		}
	}
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

/*
 * _sync_nodes_to_jobs - sync node state to job states on slurmctld restart.
 *	This routine marks nodes allocated to a job as busy no matter what
 *	the node's last saved state
 * RET count of nodes having state changed
 * Note: Operates on common variables, no arguments
 */
static int _sync_nodes_to_jobs(void)
{
	job_record_t *job_ptr;
	list_itr_t *job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->details && job_ptr->details->prolog_running) {
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
	list_itr_t *job_iterator;
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
				acct_policy_job_begin(job_ptr, false);

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
	int cnt = 0;
	uint32_t node_flags;
	node_record_t *node_ptr;
	bitstr_t *node_bitmap, *orig_job_node_bitmap = NULL;

	if (job_ptr->node_bitmap_cg) /* job completing */
		node_bitmap = job_ptr->node_bitmap_cg;
	else
		node_bitmap = job_ptr->node_bitmap;

	job_ptr->node_cnt = bit_set_count(node_bitmap);
	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		if ((job_ptr->details &&
		     (job_ptr->details->whole_node & WHOLE_NODE_USER)) ||
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

		if (IS_JOB_COMPLETING(job_ptr) && job_ptr->epilog_running) {
			/*
			 * _sync_nodes_to_comp_job() won't call
			 * deallocate_nodes()/make_node_comp() if the
			 * EpilogSlurmctld is still running to decrement
			 * run_job_cnt and increment comp_job_cnt, so just
			 * increment comp_job_cnt now.
			 */
			node_ptr->comp_job_cnt++;
		} else {
			/*
			 * run_job_cnt will be decremented by
			 * deallocate_nodes()/make_node_comp() in
			 * _sync_nodes_to_comp_job().
			 */
			node_ptr->run_job_cnt++;
		}

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
			if (job_ptr->job_resrcs &&
			    job_ptr->job_resrcs->node_bitmap) {
				/*
				 * node_bitmap is eventually changed within
				 * extract_job_resources_node() so we need to
				 * copy it before that.
				 */
				if (!orig_job_node_bitmap)
					orig_job_node_bitmap = bit_copy(
						job_ptr->job_resrcs->
						node_bitmap);
			} else {
				error("We resized job %pJ, but the original node bitmap is unavailable. Unable to resize step node bitmaps for job's steps, this should never happen",
				      job_ptr);
			}
			job_pre_resize_acctg(job_ptr);
			srun_node_fail(job_ptr, node_ptr->name);
			kill_step_on_node(job_ptr, node_ptr, true);
			excise_node_from_job(job_ptr, node_ptr);
			job_post_resize_acctg(job_ptr);
			accounting_enforce = save_accounting_enforce;
		} else if (IS_NODE_DOWN(node_ptr) && IS_JOB_RUNNING(job_ptr)) {
			info("Killing %pJ on DOWN node %s",
			     job_ptr, node_ptr->name);
			job_ptr->exit_code = 1;
			_abort_job(job_ptr, JOB_NODE_FAIL, FAIL_DOWN_NODE,
				   NULL);
			cnt++;
		} else if (IS_NODE_IDLE(node_ptr)) {
			cnt++;
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		}
	}

	/* If the job was resized then resize the bitmaps of the job's steps */
	if (orig_job_node_bitmap)
		rebuild_step_bitmaps(job_ptr, orig_job_node_bitmap);
	FREE_NULL_BITMAP(orig_job_node_bitmap);

	if ((IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) &&
	    (job_ptr->front_end_ptr != NULL))
		job_ptr->front_end_ptr->job_cnt_run++;

	set_initial_job_alias_list(job_ptr);

	return cnt;
}

/* Synchronize states of nodes and suspended jobs */
static void _sync_nodes_to_suspended_job(job_record_t *job_ptr)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		node_ptr->sus_job_cnt++;
	}

	set_initial_job_alias_list(job_ptr);
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
	list_itr_t *job_iterator;
	bool valid = true;
	list_t *license_list = NULL;

	assoc_mgr_clear_used_info();

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->array_recs)
			job_ptr->array_recs->tot_run_tasks = 0;
	}

	list_iterator_reset(job_iterator);
	while ((job_ptr = list_next(job_iterator))) {
		(void) build_feature_list(job_ptr, false, false);
		(void) build_feature_list(job_ptr, true, false);

		if (job_ptr->details->features_use ==
		    job_ptr->details->features)
			job_ptr->details->feature_list_use =
				job_ptr->details->feature_list;
		else if (job_ptr->details->features_use ==
			 job_ptr->details->prefer)
			job_ptr->details->feature_list_use =
				job_ptr->details->prefer_list;
		(void) extra_constraints_parse(job_ptr->extra,
					       &job_ptr->extra_constraints);

		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			job_array_start(job_ptr);

		if (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) {
			if (!IS_JOB_FINISHED(job_ptr))
				acct_policy_add_job_submit(job_ptr, false);
			if (IS_JOB_RUNNING(job_ptr) ||
			    IS_JOB_SUSPENDED(job_ptr)) {
				acct_policy_job_begin(job_ptr, false);
				resv_replace_update(job_ptr);
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
			license_job_get(job_ptr, true);

	}
	list_iterator_destroy(job_iterator);
}

/* Flush accounting information on this cluster, then for each running or
 * suspended job, restore its state in the accounting system */
static void _acct_restore_active_jobs(void)
{
	job_record_t *job_ptr;
	list_itr_t *job_iterator;
	step_record_t *step_ptr;
	list_itr_t *step_iterator;

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

extern int dump_config_state_lite(void)
{
	static uint32_t high_buffer_size = (1024 * 1024);
	int error_code = 0;
	buf_t *buffer = init_buf(high_buffer_size);

	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	packstr(slurm_conf.accounting_storage_type, buffer);

	/* write the buffer to file */
	error_code = save_buf_to_state("last_config_lite", buffer,
				       &high_buffer_size);

	FREE_NULL_BUFFER(buffer);

	END_TIMER2(__func__);
	return error_code;
}

extern int load_config_state_lite(void)
{
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
		FREE_NULL_BUFFER(buffer);
		return EFAULT;
	} else {
		safe_unpack_time(&buf_time, buffer);
		safe_unpackstr(&last_accounting_storage_type, buffer);
	}

	if (last_accounting_storage_type
	    && !xstrcmp(last_accounting_storage_type,
	                slurm_conf.accounting_storage_type))
		slurmctld_init_db = 0;
	xfree(last_accounting_storage_type);

	FREE_NULL_BUFFER(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete last_config_lite checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete last_config_lite checkpoint file");
	FREE_NULL_BUFFER(buffer);

	return SLURM_ERROR;
}
