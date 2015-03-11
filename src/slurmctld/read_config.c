/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_SYS_SYSLOG_H
#  include <sys/syslog.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/layouts_mgr.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_topology.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_route.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"
#include "src/common/strnatcmp.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/trigger_mgr.h"

bool slurmctld_init_db = 1;

static void _acct_restore_active_jobs(void);
static int  _build_bitmaps(void);
static void _build_bitmaps_pre_select(void);
static void _gres_reconfig(bool reconfig);
static int  _init_all_slurm_conf(void);
static int  _preserve_select_type_param(slurm_ctl_conf_t * ctl_conf_ptr,
					uint16_t old_select_type_p);
static int  _preserve_plugins(slurm_ctl_conf_t * ctl_conf_ptr,
				char *old_auth_type, char *old_checkpoint_type,
				char *old_crypto_type, char *old_sched_type,
				char *old_select_type, char *old_switch_type);
static void _purge_old_node_state(struct node_record *old_node_table_ptr,
				int old_node_record_count);
static void _purge_old_part_state(List old_part_list, char *old_def_part_name);
static int  _restore_job_dependencies(void);
static int  _restore_node_state(int recover,
				struct node_record *old_node_table_ptr,
				int old_node_record_count);
static int  _restore_part_state(List old_part_list, char *old_def_part_name,
				uint16_t flags);
static void _stat_slurm_dirs(void);
static int  _strcmp(const char *s1, const char *s2);
static int  _sync_nodes_to_comp_job(void);
static int  _sync_nodes_to_jobs(void);
static int  _sync_nodes_to_active_job(struct job_record *job_ptr);
static void _sync_nodes_to_suspended_job(struct job_record *job_ptr);
static void _sync_part_prio(void);
static int  _update_preempt(uint16_t old_enable_preempt);
#ifdef 	HAVE_ELAN
static void _validate_node_proc_count(void);
#endif
static int _compare_hostnames(struct node_record *old_node_table,
							  int old_node_count,
							  struct node_record *node_table,
							  int node_count);

/* Verify that Slurm directories are secure, not world writable */
static void _stat_slurm_dirs(void)
{
	struct stat stat_buf;
	char *problem_dir = NULL;

	if ((stat(slurmctld_conf.plugindir, &stat_buf) == 0) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "PluginDir";
	}
	if ((stat(slurmctld_conf.plugstack, &stat_buf) == 0) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "PlugStack";
	}
	if ((stat(slurmctld_conf.slurmd_spooldir, &stat_buf) == 0) &&
	    (stat_buf.st_mode & S_IWOTH)) {
		problem_dir = "SlurmdSpoolDir";
	}
	if ((stat(slurmctld_conf.state_save_location, &stat_buf) == 0) &&
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
 * _reorder_nodes_by_name - order node table in ascending order of name
 */
static void _reorder_nodes_by_name(void)
{
	struct node_record *node_ptr, *node_ptr2;
	int i, j, min_inx;

	/* Now we need to sort the node records */
	for (i = 0; i < node_record_count; i++) {
		min_inx = i;
		for (j = i + 1; j < node_record_count; j++) {
			if (strnatcmp(node_record_table_ptr[j].name,
				      node_record_table_ptr[min_inx].name) < 0)
				min_inx = j;
		}

		if (min_inx != i) {	/* swap records */
			struct node_record node_record_tmp;

			j = sizeof(struct node_record);
			node_ptr  = node_record_table_ptr + i;
			node_ptr2 = node_record_table_ptr + min_inx;

			memcpy(&node_record_tmp, node_ptr, j);
			memcpy(node_ptr, node_ptr2, j);
			memcpy(node_ptr2, &node_record_tmp, j);
		}
	}

#if _DEBUG
	/* Log the results */
	for (i=0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		info("node_rank[%d]: %s", i, node_ptr->name);
	}
#endif
}

/*
 * _reorder_nodes_by_rank - order node table in ascending order of node_rank
 * This depends on the TopologyPlugin and/or SelectPlugin, which may generate
 * such a ranking.
 */
static void _reorder_nodes_by_rank(void)
{
	struct node_record *node_ptr, *node_ptr2;
	int i, j, min_inx;
	uint32_t min_val;

	/* Now we need to sort the node records */
	for (i = 0; i < node_record_count; i++) {
		min_val = node_record_table_ptr[i].node_rank;
		min_inx = i;
		for (j = i + 1; j < node_record_count; j++) {
			if (node_record_table_ptr[j].node_rank < min_val) {
				min_val = node_record_table_ptr[j].node_rank;
				min_inx = j;
			}
		}

		if (min_inx != i) {	/* swap records */
			struct node_record node_record_tmp;

			j = sizeof(struct node_record);
			node_ptr  = node_record_table_ptr + i;
			node_ptr2 = node_record_table_ptr + min_inx;

			memcpy(&node_record_tmp, node_ptr, j);
			memcpy(node_ptr, node_ptr2, j);
			memcpy(node_ptr2, &node_record_tmp, j);
		}
	}

#if _DEBUG
	/* Log the results */
	for (i=0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		info("node_rank[%u]: %s", node_ptr->node_rank, node_ptr->name);
	}
#endif
}


/*
 * _build_bitmaps_pre_select - recover some state for jobs and nodes prior to
 *	calling the select_* functions
 */
static void _build_bitmaps_pre_select(void)
{
	struct part_record   *part_ptr;
	struct node_record   *node_ptr;
	ListIterator part_iterator;
	int i;

	/* scan partition table and identify nodes in each */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		FREE_NULL_BITMAP(part_ptr->node_bitmap);

		if ((part_ptr->nodes == NULL) || (part_ptr->nodes[0] == '\0')) {
			/* Partitions need a bitmap, even if empty */
			part_ptr->node_bitmap = bit_alloc(node_record_count);
			continue;
		}

		if (node_name2bitmap(part_ptr->nodes, false,
				     &part_ptr->node_bitmap)) {
			fatal("Invalid node names in partition %s",
			      part_ptr->name);
		}

		for (i=0; i<node_record_count; i++) {
			if (bit_test(part_ptr->node_bitmap, i) == 0)
				continue;
			node_ptr = &node_record_table_ptr[i];
			part_ptr->total_nodes++;
			if (slurmctld_conf.fast_schedule)
				part_ptr->total_cpus +=
					node_ptr->config_ptr->cpus;
			else
				part_ptr->total_cpus += node_ptr->cpus;
			node_ptr->part_cnt++;
			xrealloc(node_ptr->part_pptr, (node_ptr->part_cnt *
				sizeof(struct part_record *)));
			node_ptr->part_pptr[node_ptr->part_cnt-1] = part_ptr;
		}
	}
	list_iterator_destroy(part_iterator);
	return;
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
static int _build_bitmaps(void)
{
	int i, error_code = SLURM_SUCCESS;
	ListIterator config_iterator;
	struct config_record *config_ptr;
	struct job_record    *job_ptr;
	struct node_record   *node_ptr;
	ListIterator job_iterator;

	last_node_update = time(NULL);
	last_part_update = time(NULL);

	/* initialize the idle and up bitmaps */
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	avail_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	cg_node_bitmap    = (bitstr_t *) bit_alloc(node_record_count);
	idle_node_bitmap  = (bitstr_t *) bit_alloc(node_record_count);
	power_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	share_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	up_node_bitmap    = (bitstr_t *) bit_alloc(node_record_count);

	/* initialize the configuration bitmaps */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = (struct config_record *)
				      list_next(config_iterator))) {
		FREE_NULL_BITMAP(config_ptr->node_bitmap);
		config_ptr->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
	}
	list_iterator_destroy(config_iterator);

	/* Set all bits, all nodes initially available for sharing */
	bit_nset(share_node_bitmap, 0, (node_record_count-1));

	/* identify all nodes non-sharable due to non-sharing jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bitstr_t *tmp_bits;
		if (!IS_JOB_RUNNING(job_ptr) ||
		    (job_ptr->node_bitmap == NULL)        ||
		    (job_ptr->details     == NULL)        ||
		    (job_ptr->details->share_res != 0))
			continue;
		tmp_bits = bit_copy(job_ptr->node_bitmap);
		bit_not(tmp_bits);
		bit_and(share_node_bitmap, tmp_bits);
		FREE_NULL_BITMAP(tmp_bits);
	}
	list_iterator_destroy(job_iterator);

	/* scan all nodes and identify which are up, idle and
	 * their configuration, resync DRAINED vs. DRAINING state */
	for (i=0, node_ptr=node_record_table_ptr;
	     i<node_record_count; i++, node_ptr++) {
		uint32_t drain_flag, job_cnt;

		if (node_ptr->name[0] == '\0')
			continue;	/* defunct */
		drain_flag = IS_NODE_DRAIN(node_ptr) |
			     IS_NODE_FAIL(node_ptr);
		job_cnt = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;

		if ((IS_NODE_IDLE(node_ptr) && (job_cnt == 0)) ||
		    IS_NODE_DOWN(node_ptr))
			bit_set(idle_node_bitmap, i);
		if (IS_NODE_COMPLETING(node_ptr))
			bit_set(cg_node_bitmap, i);
		if (IS_NODE_IDLE(node_ptr) || IS_NODE_ALLOCATED(node_ptr)) {
			if ((drain_flag == 0) &&
			    (!IS_NODE_NO_RESPOND(node_ptr)))
				bit_set(avail_node_bitmap, i);
			bit_set(up_node_bitmap, i);
		}
		if (IS_NODE_POWER_SAVE(node_ptr))
			bit_set(power_node_bitmap, i);
		if (node_ptr->config_ptr)
			bit_set(node_ptr->config_ptr->node_bitmap, i);
	}

	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = (struct config_record *)
				      list_next(config_iterator))) {
		build_config_feature_list(config_ptr);
	}
	list_iterator_destroy(config_iterator);

	return error_code;
}


/*
 * _init_all_slurm_conf - initialize or re-initialize the slurm
 *	configuration values.
 * RET 0 if no error, otherwise an error code.
 * NOTE: We leave the job table intact
 * NOTE: Operates on common variables, no arguments
 */
static int _init_all_slurm_conf(void)
{
	int error_code;
	char *conf_name = xstrdup(slurmctld_conf.slurm_conf);

	slurm_conf_reinit(conf_name);
	xfree(conf_name);

	if ((error_code = init_node_conf()))
		return error_code;

	if ((error_code = init_part_conf()))
		return error_code;

	if ((error_code = init_job_conf()))
		return error_code;

	return 0;
}

static int _handle_downnodes_line(slurm_conf_downnodes_t *down)
{
	int error_code = 0;
	struct node_record *node_rec = NULL;
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
			node_rec->reason_uid = getuid();
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
 * _build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 *	default_node_record - default node configuration values
 */
static int _build_all_nodeline_info(void)
{
	int rc;

	/* Load the node table here */
	rc = build_all_nodeline_info(false);
	rc = MAX(build_all_frontend_info(false), rc);

	/* Now perform operations on the node table as needed by slurmctld */
#ifdef HAVE_BG
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	char *node_000 = NULL;
	struct node_record *node_rec = NULL;
	if (conf->node_prefix)
		node_000 = xstrdup(conf->node_prefix);
	slurm_conf_unlock();
#if (SYSTEM_DIMENSIONS == 3)
	xstrcat(node_000, "000");
#endif
#if (SYSTEM_DIMENSIONS == 4)
	xstrcat(node_000, "0000");
#endif
#if (SYSTEM_DIMENSIONS == 5)
	xstrcat(node_000, "00000");
#endif
	node_rec = find_node_record(node_000);
	if (node_rec == NULL)
		info("WARNING: No node %s configured", node_000);
	xfree(node_000);
}
#endif	/* HAVE_BG */

	return rc;
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
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!qos) {
		FREE_NULL_BITMAP(*qos_bits);
		*qos_bits = NULL;
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
	struct part_record *part_ptr;

	part_ptr = list_find_first(part_list, &list_find_part, part->name);
	if (part_ptr == NULL) {
		part_ptr = create_part_record();
		xfree(part_ptr->name);
		part_ptr->name = xstrdup(part->name);
	} else {
		/* FIXME - maybe should be fatal? */
		error("_parse_part_spec: duplicate entry for partition %s, "
		      "ignoring", part->name);
		return EEXIST;
	}

	if (part->default_flag) {
		if (default_part_name &&
		    strcmp(default_part_name, part->name)) {
			info("_parse_part_spec: changing default partition "
			     "from %s to %s", default_part_name, part->name);
			default_part_loc->flags &= (~PART_FLAG_DEFAULT);
		}
		xfree(default_part_name);
		default_part_name = xstrdup(part->name);
		default_part_loc = part_ptr;
		part_ptr->flags |= PART_FLAG_DEFAULT;
	}

	if (part->preempt_mode != (uint16_t) NO_VAL)
		part_ptr->preempt_mode = part->preempt_mode;

	if (part->disable_root_jobs == (uint16_t)NO_VAL) {
		if (slurmctld_conf.disable_root_jobs)
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
	part_ptr->max_cpus_per_node = part->max_cpus_per_node;
	part_ptr->max_share      = part->max_share;
	part_ptr->max_mem_per_cpu = part->max_mem_per_cpu;
	part_ptr->max_nodes      = part->max_nodes;
	part_ptr->max_nodes_orig = part->max_nodes;
	part_ptr->min_nodes      = part->min_nodes;
	part_ptr->min_nodes_orig = part->min_nodes;
	part_ptr->preempt_mode   = part->preempt_mode;
	part_ptr->priority       = part->priority;
	part_ptr->state_up       = part->state_up;
	part_ptr->grace_time     = part->grace_time;
	part_ptr->cr_type        = part->cr_type;

	if (part->allow_accounts) {
		xfree(part_ptr->allow_accounts);
		part_ptr->allow_accounts = xstrdup(part->allow_accounts);
		accounts_list_build(part_ptr->allow_accounts,
				    &part_ptr->allow_account_array);
	}

	if (part->allow_groups) {
		xfree(part_ptr->allow_groups);
		part_ptr->allow_groups = xstrdup(part->allow_groups);
	}

	if (part->allow_qos) {
		xfree(part_ptr->allow_qos);
		part_ptr->allow_qos = xstrdup(part->allow_qos);
		qos_list_build(part_ptr->allow_qos,&part_ptr->allow_qos_bitstr);
	}

	if (part->deny_accounts) {
		xfree(part_ptr->deny_accounts);
		part_ptr->deny_accounts = xstrdup(part->deny_accounts);
		accounts_list_build(part_ptr->deny_accounts,
				    &part_ptr->deny_account_array);
	}

	if (part->deny_qos) {
		xfree(part_ptr->deny_qos);
		part_ptr->deny_qos = xstrdup(part->deny_qos);
		qos_list_build(part_ptr->deny_qos, &part_ptr->deny_qos_bitstr);
	}

 	if (part->allow_alloc_nodes) {
 		if (part_ptr->allow_alloc_nodes) {
 			int cnt_tot, cnt_uniq;
 			hostlist_t hl = hostlist_create(part_ptr->
							allow_alloc_nodes);

 			hostlist_push(hl, part->allow_alloc_nodes);
 			cnt_tot = hostlist_count(hl);
 			hostlist_uniq(hl);
 			cnt_uniq = hostlist_count(hl);
 			if (cnt_tot != cnt_uniq) {
 				fatal("Duplicate Allowed Allocating Nodes for "
				      "Partition %s", part->name);
 			}
 			xfree(part_ptr->allow_alloc_nodes);
 			part_ptr->allow_alloc_nodes =
				hostlist_ranged_string_xmalloc(hl);
 			hostlist_destroy(hl);
 		} else {
 			part_ptr->allow_alloc_nodes =
					xstrdup(part->allow_alloc_nodes);
 		}
 	}
	if (part->alternate) {
		xfree(part_ptr->alternate);
		part_ptr->alternate = xstrdup(part->alternate);
	}
	if (part->nodes) {
		if (part_ptr->nodes) {
			int cnt_tot, cnt_uniq;
			hostlist_t hl = hostlist_create(part_ptr->nodes);

			hostlist_push(hl, part->nodes);
			cnt_tot = hostlist_count(hl);
			hostlist_uniq(hl);
			cnt_uniq = hostlist_count(hl);
			if (cnt_tot != cnt_uniq) {
				fatal("Duplicate Nodes for Partition %s",
				      part->name);
			}
			xfree(part_ptr->nodes);
			part_ptr->nodes = hostlist_ranged_string_xmalloc(hl);
			hostlist_destroy(hl);
		} else {
			part_ptr->nodes = xstrdup(part->nodes);
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

/* _sync_part_prio - Set normalized partition priorities */
static void _sync_part_prio(void)
{
	ListIterator itr = NULL;
	struct part_record *part_ptr = NULL;

	part_max_priority = 0;
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		if (part_ptr->priority > part_max_priority)
			part_max_priority = part_ptr->priority;
	}
	list_iterator_destroy(itr);

	if (part_max_priority) {
		itr = list_iterator_create(part_list);
		while ((part_ptr = list_next(itr))) {
			part_ptr->norm_priority = (double)part_ptr->priority /
						  (double)part_max_priority;
		}
		list_iterator_destroy(itr);
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
	int error_code, i, rc, load_job_ret = SLURM_SUCCESS;
	int old_node_record_count = 0;
	struct node_record *old_node_table_ptr = NULL, *node_ptr;
	bool do_reorder_nodes = false;
	List old_part_list = NULL;
	char *old_def_part_name = NULL;
	char *old_auth_type       = xstrdup(slurmctld_conf.authtype);
	uint16_t old_preempt_mode = slurmctld_conf.preempt_mode;
	char *old_checkpoint_type = xstrdup(slurmctld_conf.checkpoint_type);
	char *old_crypto_type     = xstrdup(slurmctld_conf.crypto_type);
	char *old_preempt_type    = xstrdup(slurmctld_conf.preempt_type);
	char *old_sched_type      = xstrdup(slurmctld_conf.schedtype);
	char *old_select_type     = xstrdup(slurmctld_conf.select_type);
	char *old_switch_type     = xstrdup(slurmctld_conf.switch_type);
	char *state_save_dir      = xstrdup(slurmctld_conf.state_save_location);
	char *mpi_params;
	uint16_t old_select_type_p = slurmctld_conf.select_type_param;

	/* initialization */
	START_TIMER;

	if (reconfig) {
		/* in order to re-use job state information,
		 * update nodes_completing string (based on node bitmaps) */
		update_job_nodes_completing();

		/* save node and partition states for reconfig RPC */
		old_node_record_count = node_record_count;
		old_node_table_ptr    = node_record_table_ptr;
		for (i=0, node_ptr=old_node_table_ptr; i<node_record_count;
		     i++, node_ptr++) {
			xfree(node_ptr->features);
			node_ptr->features = xstrdup(
				node_ptr->config_ptr->feature);
			/* Store the original configured CPU count somewhere
			 * (port is reused here for that purpose) so we can
			 * report changes in its configuration. */
			node_ptr->port   = node_ptr->config_ptr->cpus;
			node_ptr->weight = node_ptr->config_ptr->weight;
		}
		node_record_table_ptr = NULL;
		node_record_count = 0;
		xhash_free (node_hash_table);
		node_hash_table = NULL;
		old_part_list = part_list;
		part_list = NULL;
		old_def_part_name = default_part_name;
		default_part_name = NULL;
	}

	if ((error_code = _init_all_slurm_conf())) {
		node_record_table_ptr = old_node_table_ptr;
		node_record_count = old_node_record_count;
		part_list = old_part_list;
		default_part_name = old_def_part_name;
		return error_code;
	}

	if (slurm_layouts_init() != SLURM_SUCCESS)
		fatal("Failed to initialize the layouts framework");

	if (slurm_topo_init() != SLURM_SUCCESS)
		fatal("Failed to initialize topology plugin");

	/* Build node and partition information based upon slurm.conf file */
	_build_all_nodeline_info();
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
	if (!reconfig)
		restore_front_end_state(recover);

	update_logging();
	g_slurm_jobcomp_init(slurmctld_conf.job_comp_loc);
	if (slurm_sched_init() != SLURM_SUCCESS)
		fatal("Failed to initialize sched plugin");
	if (!reconfig && (old_preempt_mode & PREEMPT_MODE_GANG) &&
	    (gs_init() != SLURM_SUCCESS)) {
		/* gs_init() must immediately follow slurm_sched_init() */
		fatal("Failed to initialize gang scheduler");
	}
	if (switch_init() != SLURM_SUCCESS)
		fatal("Failed to initialize switch plugin");

	if (default_part_loc == NULL)
		error("read_slurm_conf: default partition not set.");

	if (node_record_count < 1) {
		error("read_slurm_conf: no nodes configured.");
		_purge_old_node_state(old_node_table_ptr,
				      old_node_record_count);
		_purge_old_part_state(old_part_list, old_def_part_name);
		return EINVAL;
	}

	/*
	 * Node reordering needs to be done by the topology and/or select
	 * plugin. Reordering the table must be done before hashing the
	 * nodes, and before any position-relative bitmaps are created.
	 */
	do_reorder_nodes |= slurm_topo_generate_node_ranking();
	do_reorder_nodes |= select_g_node_ranking(node_record_table_ptr,
						  node_record_count);
	if (do_reorder_nodes)
		_reorder_nodes_by_rank();
	else
		_reorder_nodes_by_name();

	rehash_node();
	slurm_topo_build_config();
	route_g_reconfigure();

	rehash_jobs();
	set_slurmd_addr();

	_stat_slurm_dirs();

	/*
	 * Load the layouts configuration.
	 * Only load it at init time, not during reconfiguration stages.
	 * It requires a full restart to switch to a new configuration for now.
	 */
	if (!reconfig && (slurm_layouts_load_config() != SLURM_SUCCESS))
		fatal("Failed to load the layouts framework configuration");

	if (reconfig) {		/* Preserve state from memory */
		if (old_node_table_ptr) {
			info("restoring original state of nodes");
			rc = _restore_node_state(recover, old_node_table_ptr,
						 old_node_record_count);
			error_code = MAX(error_code, rc);  /* not fatal */
		}
		if (old_part_list && ((recover > 1) ||
		    (slurmctld_conf.reconfig_flags & RECONFIG_KEEP_PART_INFO))) {
			info("restoring original partition state");
			rc = _restore_part_state(old_part_list,
						 old_def_part_name,
						 slurmctld_conf.reconfig_flags);
			error_code = MAX(error_code, rc);  /* not fatal */
		} else if (old_part_list && (slurmctld_conf.reconfig_flags &
					     RECONFIG_KEEP_PART_STAT)) {
			info("restoring original partition state only (up/down)");
			rc = _restore_part_state(old_part_list,
						 old_def_part_name,
						 slurmctld_conf.reconfig_flags);
			error_code = MAX(error_code, rc);  /* not fatal */
		}
		load_last_job_id();
		reset_first_job_id();
		(void) slurm_sched_g_reconfig();
	} else if (recover == 0) {	/* Build everything from slurm.conf */
		load_last_job_id();
		reset_first_job_id();
		(void) slurm_sched_g_reconfig();
	} else if (recover == 1) {	/* Load job & node state files */
		(void) load_all_node_state(true);
		(void) load_all_front_end_state(true);
		load_job_ret = load_all_job_state();
		sync_job_priorities();
	} else if (recover > 1) {	/* Load node, part & job state files */
		(void) load_all_node_state(false);
		(void) load_all_front_end_state(false);
		(void) load_all_part_state();
		load_job_ret = load_all_job_state();
		sync_job_priorities();
	}

	_sync_part_prio();
	_build_bitmaps_pre_select();
	if ((select_g_node_init(node_record_table_ptr, node_record_count)
	     != SLURM_SUCCESS)						||
	    (select_g_block_init(part_list) != SLURM_SUCCESS)		||
	    (select_g_state_restore(state_save_dir) != SLURM_SUCCESS)	||
	    (select_g_job_init(job_list) != SLURM_SUCCESS)) {
		fatal("failed to initialize node selection plugin state, "
		      "Clean start required.");
	}
	xfree(state_save_dir);
	_gres_reconfig(reconfig);
	reset_job_bitmaps();		/* must follow select_g_job_init() */

	(void) _sync_nodes_to_jobs();
	(void) sync_job_files();
	_purge_old_node_state(old_node_table_ptr, old_node_record_count);
	_purge_old_part_state(old_part_list, old_def_part_name);

	if ((rc = _build_bitmaps()))
		fatal("_build_bitmaps failure");
	mpi_params = slurm_get_mpi_params();
	reserve_port_config(mpi_params);
	xfree(mpi_params);

	if (license_update(slurmctld_conf.licenses) != SLURM_SUCCESS)
		fatal("Invalid Licenses value: %s", slurmctld_conf.licenses);

	init_requeue_policy();

	/* NOTE: Run restore_node_features before _restore_job_dependencies */
	restore_node_features(recover);
	_restore_job_dependencies();
#ifdef 	HAVE_ELAN
	_validate_node_proc_count();
#endif
	(void) _sync_nodes_to_comp_job();/* must follow select_g_node_init() */
	load_part_uid_allow_list(1);

	if (reconfig) {
		load_all_resv_state(0);
	} else {
		load_all_resv_state(recover);
		if (recover >= 1) {
			trigger_state_restore();
			(void) slurm_sched_g_reconfig();
		}
	}

	/* sort config_list by weight for scheduling */
	list_sort(config_list, &list_compare_config);

	/* Update plugins as possible */
	rc = _preserve_plugins(&slurmctld_conf,
			       old_auth_type, old_checkpoint_type,
			       old_crypto_type, old_sched_type,
			       old_select_type, old_switch_type);
	error_code = MAX(error_code, rc);	/* not fatal */

	if (strcmp(old_preempt_type, slurmctld_conf.preempt_type)) {
		info("Changing PreemptType from %s to %s",
		     old_preempt_type, slurmctld_conf.preempt_type);
		(void) slurm_preempt_fini();
		if (slurm_preempt_init() != SLURM_SUCCESS)
			fatal( "failed to initialize preempt plugin" );
	}
	xfree(old_preempt_type);
	rc = _update_preempt(old_preempt_mode);
	error_code = MAX(error_code, rc);	/* not fatal */

	/* Update plugin parameters as possible */
	rc = job_submit_plugin_reconfig();
	error_code = MAX(error_code, rc);	/* not fatal */
	rc = switch_g_reconfig();
	error_code = MAX(error_code, rc);	/* not fatal */
	rc = _preserve_select_type_param(&slurmctld_conf, old_select_type_p);
	error_code = MAX(error_code, rc);	/* not fatal */

	/* Restore job accounting info if file missing or corrupted,
	 * an extremely rare situation */
	if (load_job_ret)
		_acct_restore_active_jobs();

	/* Sync select plugin with synchronized job/node/part data */
	select_g_reconfigure();

	slurmctld_conf.last_update = time(NULL);
	END_TIMER2("read_slurm_conf");
	return error_code;
}

static void _gres_reconfig(bool reconfig)
{
	struct node_record *node_ptr;
	char *gres_name;
	bool gres_changed;
	int i;

	if (reconfig) {
		gres_plugin_reconfig(&gres_changed);
	} else {
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (node_ptr->gres)
				gres_name = node_ptr->gres;
			else
				gres_name = node_ptr->config_ptr->gres;
			gres_plugin_init_node_config(node_ptr->name, gres_name,
						     &node_ptr->gres_list);
		}
	}
}

/* Restore node state and size information from saved records which match
 * the node registration message. If a node was re-configured to be down or
 * drained, we set those states. We only recover a node's Features if
 * recover==2. */
static int _restore_node_state(int recover,
			       struct node_record *old_node_table_ptr,
			       int old_node_record_count)
{
	struct node_record *node_ptr, *old_node_ptr;
	int i, rc = SLURM_SUCCESS;
	hostset_t hs = NULL;
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	bool power_save_mode = false;

	if (conf->suspend_program && conf->resume_program)
		power_save_mode = true;
	slurm_conf_unlock();


	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	     i++, node_ptr++) {
		node_ptr->not_responding = true;
	}

	for (i=0, old_node_ptr=old_node_table_ptr; i<old_node_record_count;
	     i++, old_node_ptr++) {
		uint32_t drain_flag = false, down_flag = false;
		dynamic_plugin_data_t *tmp_select_nodeinfo;

		node_ptr  = find_node_record(old_node_ptr->name);
		if (node_ptr == NULL)
			continue;

		node_ptr->not_responding = false;
		if (IS_NODE_DOWN(node_ptr))
			down_flag = true;
		if (IS_NODE_DRAIN(node_ptr))
			drain_flag = true;
		node_ptr->node_state = old_node_ptr->node_state;
		if (down_flag) {
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state |= NODE_STATE_DOWN;
		}
		if (drain_flag)
			node_ptr->node_state |= NODE_STATE_DRAIN;
		if ((!power_save_mode) &&
		    (IS_NODE_POWER_SAVE(node_ptr) ||
		     IS_NODE_POWER_UP(node_ptr))) {
			node_ptr->node_state &= (~NODE_STATE_POWER_SAVE);
			node_ptr->node_state &= (~NODE_STATE_POWER_UP);
			if (hs)
				hostset_insert(hs, node_ptr->name);
			else
				hs = hostset_create(node_ptr->name);
		}

		if (IS_NODE_CLOUD(node_ptr) && !IS_NODE_POWER_SAVE(node_ptr)) {
			/* Preserve NodeHostname + NodeAddr set by scontrol */
			xfree(node_ptr->comm_name);
			node_ptr->comm_name = old_node_ptr->comm_name;
			old_node_ptr->comm_name = NULL;
			xfree(node_ptr->node_hostname);
			node_ptr->node_hostname = old_node_ptr->node_hostname;
			old_node_ptr->node_hostname = NULL;
			slurm_reset_alias(node_ptr->name, node_ptr->comm_name,
					  node_ptr->node_hostname);
		}

		node_ptr->last_response = old_node_ptr->last_response;

		/* make sure we get the old state from the select
		 * plugin, just swap it out to avoid possible memory leak */
		tmp_select_nodeinfo = node_ptr->select_nodeinfo;
		node_ptr->select_nodeinfo = old_node_ptr->select_nodeinfo;
		old_node_ptr->select_nodeinfo = tmp_select_nodeinfo;

#ifndef HAVE_BG
		/* If running on a BlueGene system the cpus never
		   change so just skip this.
		*/
		if (old_node_ptr->port != node_ptr->config_ptr->cpus) {
			rc = ESLURM_NEED_RESTART;
			error("Configured cpu count change on %s (%u to %u)",
			      node_ptr->name, old_node_ptr->port,
			      node_ptr->config_ptr->cpus);
		}
#endif
		node_ptr->boot_time     = old_node_ptr->boot_time;
		node_ptr->cpus          = old_node_ptr->cpus;
		node_ptr->cores         = old_node_ptr->cores;
		xfree(node_ptr->cpu_spec_list);
		node_ptr->cpu_spec_list = old_node_ptr->cpu_spec_list;
		old_node_ptr->cpu_spec_list = NULL;
		node_ptr->core_spec_cnt = old_node_ptr->core_spec_cnt;
		node_ptr->last_idle     = old_node_ptr->last_idle;
		node_ptr->boards        = old_node_ptr->boards;
		node_ptr->sockets       = old_node_ptr->sockets;
		node_ptr->threads       = old_node_ptr->threads;
		node_ptr->real_memory   = old_node_ptr->real_memory;
		node_ptr->mem_spec_limit = old_node_ptr->mem_spec_limit;
		node_ptr->slurmd_start_time = old_node_ptr->slurmd_start_time;
		node_ptr->tmp_disk      = old_node_ptr->tmp_disk;
		node_ptr->weight        = old_node_ptr->weight;

		node_ptr->sus_job_cnt   = old_node_ptr->sus_job_cnt;

		if (node_ptr->gres_list)
			list_destroy(node_ptr->gres_list);
		node_ptr->gres_list = old_node_ptr->gres_list;
		old_node_ptr->gres_list = NULL;

		if (node_ptr->reason == NULL) {
			/* Recover only if not explicitly set in slurm.conf */
			node_ptr->reason = old_node_ptr->reason;
			node_ptr->reason_time = old_node_ptr->reason_time;
			old_node_ptr->reason = NULL;
		}
		if (recover == 2) {
			/* NOTE: features in node record just a placeholder
			 * for restore_node_features() to set up new config
			 * records. */
			xfree(node_ptr->features);
			node_ptr->features = old_node_ptr->features;
			old_node_ptr->features = NULL;
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

	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	     i++, node_ptr++) {
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
static void _purge_old_node_state(struct node_record *old_node_table_ptr,
				int old_node_record_count)
{
	int i;
	struct node_record *node_ptr;

	node_ptr = old_node_table_ptr;
	for (i=0; i< old_node_record_count; i++, node_ptr++)
		purge_node_rec(node_ptr);

	xfree(old_node_table_ptr);
}

/* Variant of strcmp that will accept NULL string pointers */
static int  _strcmp(const char *s1, const char *s2)
{
	if ((s1 != NULL) && (s2 == NULL))
		return 1;
	if ((s1 == NULL) && (s2 == NULL))
		return 0;
	if ((s1 == NULL) && (s2 != NULL))
		return -1;
	return strcmp(s1, s2);
}

/* Restore partition information from saved records */
static int  _restore_part_state(List old_part_list, char *old_def_part_name,
				uint16_t flags)
{
	int rc = SLURM_SUCCESS;
	ListIterator part_iterator;
	struct part_record *old_part_ptr, *part_ptr;

	if (!old_part_list)
		return rc;

	/* For each part in list, find and update recs */
	part_iterator = list_iterator_create(old_part_list);
	while ((old_part_ptr = (struct part_record *)
			       list_next(part_iterator))) {
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
			if (_strcmp(part_ptr->allow_accounts,
				    old_part_ptr->allow_accounts)) {
				error("Partition %s AllowAccounts differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_accounts);
				part_ptr->allow_accounts =
					xstrdup(old_part_ptr->allow_accounts);
			}
			if (_strcmp(part_ptr->allow_groups,
				    old_part_ptr->allow_groups)) {
				error("Partition %s AllowGroups differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_groups);
				part_ptr->allow_groups = xstrdup(old_part_ptr->
								 allow_groups);
				accounts_list_build(part_ptr->allow_accounts,
						&part_ptr->allow_account_array);
			}
			if (_strcmp(part_ptr->allow_qos,
				    old_part_ptr->allow_qos)) {
				error("Partition %s AllowQos differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_qos);
				part_ptr->allow_qos = xstrdup(old_part_ptr->
								 allow_qos);
				qos_list_build(part_ptr->allow_qos,
					       &part_ptr->allow_qos_bitstr);
			}
			if (_strcmp(part_ptr->deny_accounts,
				    old_part_ptr->deny_accounts)) {
				error("Partition %s DenyAccounts differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->deny_accounts);
				part_ptr->deny_accounts =
					xstrdup(old_part_ptr->deny_accounts);
				accounts_list_build(part_ptr->deny_accounts,
						&part_ptr->deny_account_array);
			}
			if (_strcmp(part_ptr->deny_qos,
				    old_part_ptr->deny_qos)) {
				error("Partition %s DenyQos differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->deny_qos);
				part_ptr->deny_qos = xstrdup(old_part_ptr->
							     deny_qos);
				qos_list_build(part_ptr->deny_qos,
					       &part_ptr->deny_qos_bitstr);
			}
			if (_strcmp(part_ptr->allow_alloc_nodes,
				    old_part_ptr->allow_alloc_nodes)) {
				error("Partition %s AllowNodes differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->allow_alloc_nodes);
				part_ptr->allow_alloc_nodes =
					xstrdup(old_part_ptr->
						allow_alloc_nodes);
			}
			if (part_ptr->default_time !=
			    old_part_ptr->default_time) {
				error("Partition %s DefaultTime differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->default_time = old_part_ptr->
							 default_time;
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
				error("Partition %s Shared differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->max_share = old_part_ptr->max_share;
			}
			if (part_ptr->max_time != old_part_ptr->max_time) {
				error("Partition %s MaxTime differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->max_time = old_part_ptr->max_time;
			}
			if (part_ptr->grace_time != old_part_ptr->grace_time) {
				error("Partition %s GraceTime differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->grace_time = old_part_ptr->grace_time;
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
			if (_strcmp(part_ptr->nodes, old_part_ptr->nodes)) {
				error("Partition %s Nodes differs from "
				      "slurm.conf", part_ptr->name);
				xfree(part_ptr->nodes);
				part_ptr->nodes = xstrdup(old_part_ptr->nodes);
			}
			if (part_ptr->preempt_mode !=
			    old_part_ptr->preempt_mode) {
				error("Partition %s PreemptMode differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->preempt_mode = old_part_ptr->
							 preempt_mode;
			}
			if (part_ptr->priority != old_part_ptr->priority) {
				error("Partition %s Priority differs from "
				      "slurm.conf", part_ptr->name);
				part_ptr->priority = old_part_ptr->priority;
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
			part_ptr = create_part_record();
			part_ptr->name = xstrdup(old_part_ptr->name);
			part_ptr->allow_alloc_nodes = xstrdup(old_part_ptr->
							    allow_alloc_nodes);
			part_ptr->allow_accounts = xstrdup(old_part_ptr->
							   allow_accounts);
			accounts_list_build(part_ptr->allow_accounts,
					 &part_ptr->allow_account_array);
			part_ptr->allow_groups = xstrdup(old_part_ptr->
							 allow_groups);
			part_ptr->allow_qos = xstrdup(old_part_ptr->
						      allow_qos);
			qos_list_build(part_ptr->allow_qos,
				       &part_ptr->allow_qos_bitstr);
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
			part_ptr->max_nodes = old_part_ptr->max_nodes;
			part_ptr->max_nodes_orig = old_part_ptr->
						   max_nodes_orig;
			part_ptr->max_share = old_part_ptr->max_share;
			part_ptr->max_time = old_part_ptr->max_time;
			part_ptr->grace_time = old_part_ptr->grace_time;
			part_ptr->min_nodes = old_part_ptr->min_nodes;
			part_ptr->min_nodes_orig = old_part_ptr->
						   min_nodes_orig;
			part_ptr->nodes = xstrdup(old_part_ptr->nodes);
			part_ptr->priority = old_part_ptr->priority;
			part_ptr->state_up = old_part_ptr->state_up;
		}
	}
	list_iterator_destroy(part_iterator);

	if (old_def_part_name &&
	    ((default_part_name == NULL) ||
	     strcmp(old_def_part_name, default_part_name))) {
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
	list_destroy(old_part_list);
}

/*
 * _preserve_select_type_param - preserve original plugin parameters.
 *	Daemons and/or commands must be restarted for some
 *	select plugin value changes to take effect.
 * RET zero or error code
 */
static int  _preserve_select_type_param(slurm_ctl_conf_t *ctl_conf_ptr,
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
static int _update_preempt(uint16_t old_preempt_mode)
{
	uint16_t new_preempt_mode = slurm_get_preempt_mode();

	if ((old_preempt_mode & PREEMPT_MODE_GANG) ==
	    (new_preempt_mode & PREEMPT_MODE_GANG))
		return SLURM_SUCCESS;

	if (new_preempt_mode & PREEMPT_MODE_GANG) {
		info("Enabling gang scheduling");
		return gs_init();
	}

	if (old_preempt_mode == PREEMPT_MODE_GANG) {
		info("Disabling gang scheduling");
		gs_wake_jobs();
		return gs_fini();
	}

	error("Invalid gang scheduling mode change");
	return EINVAL;
}

/*
 * _preserve_plugins - preserve original plugin values over reconfiguration
 *	as required. daemons and/or commands must be restarted for some
 *	plugin value changes to take effect.
 * RET zero or error code
 */
static int  _preserve_plugins(slurm_ctl_conf_t * ctl_conf_ptr,
		char *old_auth_type, char *old_checkpoint_type,
		char *old_crypto_type, char *old_sched_type,
		char *old_select_type, char *old_switch_type)
{
	int rc = SLURM_SUCCESS;

	if (old_auth_type) {
		if (strcmp(old_auth_type, ctl_conf_ptr->authtype)) {
			xfree(ctl_conf_ptr->authtype);
			ctl_conf_ptr->authtype = old_auth_type;
			rc =  ESLURM_INVALID_AUTHTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_auth_type);
	}

	if (old_checkpoint_type) {
		if (strcmp(old_checkpoint_type,
				ctl_conf_ptr->checkpoint_type)) {
			xfree(ctl_conf_ptr->checkpoint_type);
			ctl_conf_ptr->checkpoint_type = old_checkpoint_type;
			rc =  ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE;
		} else  /* free duplicate value */
			xfree(old_checkpoint_type);
	}

	if (old_crypto_type) {
		if (strcmp(old_crypto_type,
				ctl_conf_ptr->crypto_type)) {
			xfree(ctl_conf_ptr->crypto_type);
			ctl_conf_ptr->crypto_type = old_crypto_type;
			rc = ESLURM_INVALID_CRYPTO_TYPE_CHANGE;
		} else
			xfree(old_crypto_type);
	}

	if (old_sched_type) {
		if (strcmp(old_sched_type, ctl_conf_ptr->schedtype)) {
			xfree(ctl_conf_ptr->schedtype);
			ctl_conf_ptr->schedtype = old_sched_type;
			rc =  ESLURM_INVALID_SCHEDTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_sched_type);
	}


	if (old_select_type) {
		if (strcmp(old_select_type, ctl_conf_ptr->select_type)) {
			xfree(ctl_conf_ptr->select_type);
			ctl_conf_ptr->select_type = old_select_type;
			rc =  ESLURM_INVALID_SELECTTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_select_type);
	}

	if (old_switch_type) {
		if (strcmp(old_switch_type, ctl_conf_ptr->switch_type)) {
			xfree(ctl_conf_ptr->switch_type);
			ctl_conf_ptr->switch_type = old_switch_type;
			rc = ESLURM_INVALID_SWITCHTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_switch_type);
	}

	if (ctl_conf_ptr->backup_controller == NULL)
		info("read_slurm_conf: backup_controller not specified.");

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
	struct job_record *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->node_bitmap == NULL)
			continue;

		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_COMPLETING(job_ptr))
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
	struct job_record *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
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
			/* This needs to be set up for the priority
			   plugin and this happens before it is
			   normally set up so do it now.
			*/
			if (!cluster_cpus)
				set_cluster_cpus();

			info("%s: Job %u in completing state",
			     __func__, job_ptr->job_id);
			if (!job_ptr->node_bitmap_cg)
				build_cg_bitmap(job_ptr);
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
static int _sync_nodes_to_active_job(struct job_record *job_ptr)
{
	int i, cnt = 0;
	uint32_t node_flags;
	struct node_record *node_ptr = node_record_table_ptr;

	if (job_ptr->node_bitmap_cg) /* job completing */
		job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap_cg);
	else
		job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap);
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (job_ptr->node_bitmap_cg) { /* job completing */
			if (bit_test(job_ptr->node_bitmap_cg, i) == 0)
				continue;
		} else if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;

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
			info("Removing failed node %s from job_id %u",
			     node_ptr->name, job_ptr->job_id);
			/* Disable accounting here. Accounting reset for all
			 * jobs in _restore_job_dependencies() */
			save_accounting_enforce = accounting_enforce;
			accounting_enforce &= (~ACCOUNTING_ENFORCE_LIMITS);
			job_pre_resize_acctg(job_ptr);
			srun_node_fail(job_ptr->job_id, node_ptr->name);
			kill_step_on_node(job_ptr, node_ptr, true);
			excise_node_from_job(job_ptr, node_ptr);
			job_post_resize_acctg(job_ptr);
			accounting_enforce = save_accounting_enforce;
		} else if (IS_NODE_DOWN(node_ptr) && IS_JOB_RUNNING(job_ptr)) {
			time_t now = time(NULL);
			info("Killing job %u on DOWN node %s",
			     job_ptr->job_id, node_ptr->name);
			job_ptr->job_state = JOB_NODE_FAIL | JOB_COMPLETING;
			build_cg_bitmap(job_ptr);
			job_ptr->end_time = MIN(job_ptr->end_time, now);
			job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
			job_ptr->state_reason = FAIL_DOWN_NODE;
			xfree(job_ptr->state_desc);
			job_completion_logger(job_ptr, false);
			if (job_ptr->job_state == JOB_NODE_FAIL) {
				/* build_cg_bitmap() may clear JOB_COMPLETING */
				epilog_slurmctld(job_ptr);
			}
			cnt++;
		} else if (IS_NODE_IDLE(node_ptr)) {
			cnt++;
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		}
	}

	if (IS_JOB_RUNNING(job_ptr) && job_ptr->front_end_ptr)
		job_ptr->front_end_ptr->job_cnt_run++;

	return cnt;
}

/* Synchronize states of nodes and suspended jobs */
static void _sync_nodes_to_suspended_job(struct job_record *job_ptr)
{
	int i;
	struct node_record *node_ptr = node_record_table_ptr;

	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;

		node_ptr->sus_job_cnt++;
	}
	return;
}

#ifdef 	HAVE_ELAN
/* Every node in a given partition must have the same processor count
 * at present, ensured by this function. */
static void _validate_node_proc_count(void)
{
	ListIterator part_iterator;
	struct part_record *part_ptr;
	struct node_record *node_ptr;
	int first_bit, last_bit, i, node_size, part_size;

	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		first_bit = bit_ffs(part_ptr->node_bitmap);
		last_bit = bit_fls(part_ptr->node_bitmap);
		part_size = -1;
		for (i = first_bit; i <= last_bit; i++) {
			if (bit_test(part_ptr->node_bitmap, i) == 0)
				continue;
			node_ptr = node_record_table_ptr + i;

			if (slurmctld_conf.fast_schedule)
				node_size = node_ptr->config_ptr->cpus;
			else if (node_ptr->cpus < node_ptr->config_ptr->cpus)
				continue;    /* node too small, will be DOWN */
			else if (IS_NODE_DOWN(node_ptr))
				continue;
			else
				node_size = node_ptr->cpus;

			if (part_size == -1)
				part_size = node_size;
			else if (part_size != node_size)
				fatal("Partition %s has inconsistent "
					"processor count", part_ptr->name);
		}
	}
	list_iterator_destroy(part_iterator);
}
#endif

/*
 * _restore_job_dependencies - Build depend_list and license_list for every job
 *	also reset the running job count for scheduling policy
 */
static int _restore_job_dependencies(void)
{
	int error_code = SLURM_SUCCESS, rc;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	char *new_depend;
	bool valid = true;
	List license_list;

	assoc_mgr_clear_used_info();
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->array_recs)
			job_ptr->array_recs->tot_run_tasks = 0;
	}

	list_iterator_reset(job_iterator);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		(void) build_feature_list(job_ptr);

		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			job_array_start(job_ptr);

		if (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) {
			if (!IS_JOB_FINISHED(job_ptr))
				acct_policy_add_job_submit(job_ptr);
			if (IS_JOB_RUNNING(job_ptr) ||
			    IS_JOB_SUSPENDED(job_ptr))
				acct_policy_job_begin(job_ptr);
		}

		license_list = license_validate(job_ptr->licenses, &valid);
		FREE_NULL_LIST(job_ptr->license_list);
		if (valid)
			job_ptr->license_list = license_list;
		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			license_job_get(job_ptr);

		if ((job_ptr->details == NULL) ||
		    (job_ptr->details->dependency == NULL))
			continue;
		new_depend = job_ptr->details->dependency;
		job_ptr->details->dependency = NULL;
		rc = update_job_dependency(job_ptr, new_depend);
		if (rc != SLURM_SUCCESS) {
			error("Invalid dependencies discarded for job %u: %s",
				job_ptr->job_id, new_depend);
			error_code = rc;
		}
		xfree(new_depend);
	}
	list_iterator_destroy(job_iterator);
	return error_code;
}

/* Flush accounting information on this cluster, then for each running or
 * suspended job, restore its state in the accounting system */
static void _acct_restore_active_jobs(void)
{
	struct job_record *job_ptr;
	ListIterator job_iterator;
	struct step_record *step_ptr;
	ListIterator step_iterator;

	info("Reinitializing job accounting state");
	acct_storage_g_flush_jobs_on_cluster(acct_db_conn,
					     time(NULL));
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_SUSPENDED(job_ptr))
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr)) {
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
			step_iterator = list_iterator_create(
				job_ptr->step_list);
			while ((step_ptr = (struct step_record *)
					   list_next(step_iterator))) {
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
static int
_compare_hostnames(struct node_record *old_node_table,
				   int old_node_count,
				   struct node_record *node_table,
				   int node_count)
{
	int cc;
	int set_size;
	char *old_ranged;
	char *ranged;
	hostset_t old_set;
	hostset_t set;

	if (old_node_count != node_count) {
		error("%s: node count has changed before reconfiguration "
		      "from %d to %d. You have to restart slurmctld.",
		      __func__, old_node_count, node_count);
		return -1;
	}

	old_set = hostset_create("");
	for (cc = 0; cc < old_node_count; cc++)
		hostset_insert(old_set, old_node_table[cc].name);

	set = hostset_create("");
	for (cc = 0; cc < node_count; cc++)
		hostset_insert(set, node_table[cc].name);

	set_size = MAXHOSTNAMELEN * node_count * sizeof(char)
		+ node_count * sizeof(char) + 1;

	old_ranged = xmalloc(set_size);
	ranged = xmalloc(set_size);

	hostset_ranged_string(old_set, set_size, old_ranged);
	hostset_ranged_string(set, set_size, ranged);

	cc = 0;
	if (strcmp(old_ranged, ranged) != 0) {
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
	Buf buffer = init_buf(high_buffer_size);

	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	packstr(slurmctld_conf.accounting_storage_type, buffer);

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/last_config_lite",
				  slurmctld_conf.state_save_location);
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
	int data_allocated, data_read = 0;
	uint32_t data_size = 0, uint32_tmp = 0;
	uint16_t ver = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	char *last_accounting_storage_type = NULL;

	/* Always ignore .old file */
	state_file = xstrdup_printf("%s/last_config_lite",
				    slurmctld_conf.state_save_location);

	//info("looking at the %s file", state_file);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug2("No last_config_lite file (%s) to recover", state_file);
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in last_conf_lite header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION) {
		error("***********************************************");
		error("Can not recover last_conf_lite, incompatible version, "
		      "got %u <= %u", ver, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	safe_unpackstr_xmalloc(&last_accounting_storage_type,
			       &uint32_tmp, buffer);

	xassert(slurmctld_conf.accounting_storage_type);

	if (last_accounting_storage_type
	    && !strcmp(last_accounting_storage_type,
		       slurmctld_conf.accounting_storage_type))
		slurmctld_init_db = 0;
	xfree(last_accounting_storage_type);

	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (buffer)
		free_buf(buffer);

	return SLURM_ERROR;
}
