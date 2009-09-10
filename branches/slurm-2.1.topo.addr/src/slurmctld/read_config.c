/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_topology.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/basil_interface.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
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

static void _acct_restore_active_jobs(void);
static int  _build_bitmaps(void);
static void _build_bitmaps_pre_select(void);
static int  _init_all_slurm_conf(void);
static int  _preserve_select_type_param(slurm_ctl_conf_t * ctl_conf_ptr, 
					select_type_plugin_info_t 
					old_select_type_p);
static int  _preserve_plugins(slurm_ctl_conf_t * ctl_conf_ptr, 
				char *old_auth_type, char *old_checkpoint_type,
				char *old_crypto_type, char *old_sched_type, 
				char *old_select_type, char *old_switch_type);
static void _purge_old_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count);
static int  _restore_job_dependencies(void);
static int  _restore_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count);
static int  _sync_nodes_to_comp_job(void);
static int  _sync_nodes_to_jobs(void);
static int  _sync_nodes_to_active_job(struct job_record *job_ptr);
static int  _update_preempt(uint16_t old_enable_preempt);
#ifdef 	HAVE_ELAN
static void _validate_node_proc_count(void);
#endif

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
	if (part_iterator == NULL)
		fatal ("memory allocation failure");

	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		FREE_NULL_BITMAP(part_ptr->node_bitmap);

		if ((part_ptr->nodes == NULL) || (part_ptr->nodes[0] == '\0'))
			continue;

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
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(power_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	idle_node_bitmap  = (bitstr_t *) bit_alloc(node_record_count);
	avail_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	power_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	share_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	up_node_bitmap    = (bitstr_t *) bit_alloc(node_record_count);
	if ((idle_node_bitmap     == NULL) ||
	    (avail_node_bitmap    == NULL) ||
	    (power_node_bitmap    == NULL) ||
	    (share_node_bitmap    == NULL) ||
	    (up_node_bitmap       == NULL)) 
		fatal ("bit_alloc malloc failure");

	/* initialize the configuration bitmaps */
	config_iterator = list_iterator_create(config_list);
	if (config_iterator == NULL)
		fatal ("memory allocation failure");
	while ((config_ptr = (struct config_record *)
				      list_next(config_iterator))) {
		FREE_NULL_BITMAP(config_ptr->node_bitmap);
		config_ptr->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
		if (config_ptr->node_bitmap == NULL)
			fatal ("bit_alloc malloc failure");
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
		    (job_ptr->details->shared != 0))
			continue;
		tmp_bits = bit_copy(job_ptr->node_bitmap);
		if (tmp_bits == NULL)
			fatal ("bit_copy malloc failure");
		bit_not(tmp_bits);
		bit_and(share_node_bitmap, tmp_bits);
		bit_free(tmp_bits);
	}
	list_iterator_destroy(job_iterator);

	/* scan all nodes and identify which are up, idle and 
	 * their configuration, resync DRAINED vs. DRAINING state */
	for (i=0, node_ptr=node_record_table_ptr;
	     i<node_record_count; i++, node_ptr++) {
		uint16_t drain_flag, job_cnt;

		if (node_ptr->name[0] == '\0')
			continue;	/* defunct */
		drain_flag = IS_NODE_DRAIN(node_ptr) | 
			     IS_NODE_FAIL(node_ptr);
		job_cnt = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;

		if ((IS_NODE_IDLE(node_ptr) && (job_cnt == 0)) ||
		    IS_NODE_DOWN(node_ptr))
			bit_set(idle_node_bitmap, i);
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
	if (config_iterator == NULL)
		fatal ("memory allocation failure");
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
		state_val = state_str2int(down->state);
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

	/* Now perform operations on the node table as needed by slurmctld */
#ifdef HAVE_3D
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	char *node_000 = NULL;
	struct node_record *node_rec = NULL;
	if (conf->node_prefix)
		node_000 = xstrdup(conf->node_prefix);
	slurm_conf_unlock();
	xstrcat(node_000, "000");
	node_rec = find_node_record(node_000);
	if (node_rec == NULL)
		fatal("No node %s configured", node_000);
	xfree(node_000);
}
#endif	/* HAVE_3D */

#ifndef HAVE_BG
	slurm_topo_build_config();
#endif	/* ! HAVE_BG */

	return rc;
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
		verbose("_parse_part_spec: duplicate entry for partition %s",
			part->name);
	}

	if (part->default_flag) {
		if (default_part_name
		&&  strcmp(default_part_name, part->name))
			info("_parse_part_spec: changing default partition "
				"from %s to %s", 
				default_part_name, part->name);
		xfree(default_part_name);
		default_part_name = xstrdup(part->name);
		default_part_loc = part_ptr;
	}
	if(part->disable_root_jobs == (uint16_t)NO_VAL)
		part_ptr->disable_root_jobs = slurmctld_conf.disable_root_jobs;
	else
		part_ptr->disable_root_jobs = part->disable_root_jobs;
	
	if(part_ptr->disable_root_jobs) 
		debug2("partition %s does not allow root jobs", part_ptr->name);

	if ((part->default_time != NO_VAL) &&
	    (part->default_time > part->max_time)) {
		info("partition %s DefaultTime exceeds MaxTime (%u > %u)",
		     part->default_time, part->max_time);
		part->default_time = NO_VAL;
	}

	part_ptr->hidden         = part->hidden_flag ? 1 : 0;
	part_ptr->max_time       = part->max_time;
	part_ptr->default_time   = part->default_time;
	part_ptr->max_share      = part->max_share;
	part_ptr->max_nodes      = part->max_nodes;
	part_ptr->max_nodes_orig = part->max_nodes;
	part_ptr->min_nodes      = part->min_nodes;
	part_ptr->min_nodes_orig = part->min_nodes;
	part_ptr->priority       = part->priority;
	part_ptr->root_only      = part->root_only_flag ? 1 : 0;
	part_ptr->state_up       = part->state_up_flag ? 1 : 0;
	if (part->allow_groups) {
		xfree(part_ptr->allow_groups);
		part_ptr->allow_groups = xstrdup(part->allow_groups);
	}
 	if (part->allow_alloc_nodes) {
 		if (part_ptr->allow_alloc_nodes) {
 			int cnt_tot, cnt_uniq, buf_size;
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
 			buf_size = strlen(part_ptr->allow_alloc_nodes) + 1 +
				   strlen(part->allow_alloc_nodes) + 1;
 			xfree(part_ptr->allow_alloc_nodes);
 			part_ptr->allow_alloc_nodes = xmalloc(buf_size);
 			hostlist_ranged_string(hl, buf_size, 
					       part_ptr->allow_alloc_nodes);
 			hostlist_destroy(hl);
 		} else {
 			part_ptr->allow_alloc_nodes = 
					xstrdup(part->allow_alloc_nodes);
 		}
 	}
	if (part->nodes) {
		if (part_ptr->nodes) {
			int cnt_tot, cnt_uniq, buf_size;
			hostlist_t hl = hostlist_create(part_ptr->nodes);
			
			hostlist_push(hl, part->nodes);
			cnt_tot = hostlist_count(hl);
			hostlist_uniq(hl);
			cnt_uniq = hostlist_count(hl);
			if (cnt_tot != cnt_uniq) {
				fatal("Duplicate Nodes for Partition %s",
					part->name);
			}
			buf_size = strlen(part_ptr->nodes) + 1 +
				   strlen(part->nodes) + 1;
			xfree(part_ptr->nodes);
			part_ptr->nodes = xmalloc(buf_size);
			hostlist_ranged_string(hl, buf_size, part_ptr->nodes);
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
static int _build_all_partitionline_info()
{
	slurm_conf_partition_t *part, **ptr_array;
	int count;
	int i;
	ListIterator itr = NULL;
			
	count = slurm_conf_partition_array(&ptr_array);
	if (count == 0)
		fatal("No PartitionName information available!");

	for (i = 0; i < count; i++) {
		part = ptr_array[i];

		_build_single_partitionline_info(part);
		if(part->priority > part_max_priority) 
			part_max_priority = part->priority;
	}

	/* set up the normalized priority of the partitions */
	if(part_max_priority) {
		struct part_record *part_ptr = NULL;

		itr = list_iterator_create(part_list);
		while((part_ptr = list_next(itr))) {
			part_ptr->norm_priority = (double)part_ptr->priority 
				/ (double)part_max_priority;
		}
		list_iterator_destroy(itr);
	}

	return SLURM_SUCCESS;
}

/*
 * read_slurm_conf - load the slurm configuration from the configured file. 
 * read_slurm_conf can be called more than once if so desired.
 * IN recover - replace job, node and/or partition data with last saved 
 *              state information depending upon value
 *              0 = use no saved state information
 *              1 = recover saved job and trigger state, 
 *                  node DOWN/DRAIN/FAIL state and reason information
 *              2 = recover all state saved from last slurmctld shutdown
 * RET SLURM_SUCCESS if no error, otherwise an error code
 * Note: Operates on common variables only
 */
int read_slurm_conf(int recover)
{
	DEF_TIMERS;
	int error_code, i, rc, load_job_ret = SLURM_SUCCESS;
	int old_node_record_count;
	struct node_record *old_node_table_ptr;
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
	select_type_plugin_info_t old_select_type_p = 
		(select_type_plugin_info_t) slurmctld_conf.select_type_param;

	/* initialization */
	START_TIMER;

	if (recover == 0) {
		/* in order to re-use job state information,
		 * update nodes_completing string (based on node_bitmap) */
		update_job_nodes_completing();
	}

	/* save node states for reconfig RPC */
	old_node_record_count = node_record_count;
	old_node_table_ptr    = node_record_table_ptr;
	for (i=0; i<node_record_count; i++) {
		xfree(old_node_table_ptr[i].arch);
		xfree(old_node_table_ptr[i].features);
		xfree(old_node_table_ptr[i].os);
		old_node_table_ptr[i].features = xstrdup(
			old_node_table_ptr[i].config_ptr->feature);
		/* Store the original configured CPU count somewhere
		 * (port is reused here for that purpose) so we can
		 * report changes in its configuration. */
		old_node_table_ptr[i].port = old_node_table_ptr[i].
					     config_ptr->cpus;
	}
	node_record_table_ptr = NULL;
	node_record_count = 0;

	if ((error_code = _init_all_slurm_conf())) {
		node_record_table_ptr = old_node_table_ptr;
		return error_code;
	}

	if (slurm_topo_init() != SLURM_SUCCESS)
		fatal("Failed to initialize topology plugin");

	_build_all_nodeline_info();
	_handle_all_downnodes();
	_build_all_partitionline_info();

	update_logging();
	g_slurm_jobcomp_init(slurmctld_conf.job_comp_loc);
	if (slurm_sched_init() != SLURM_SUCCESS)
		fatal("Failed to initialize sched plugin");
	if (switch_init() != SLURM_SUCCESS)
		fatal("Failed to initialize switch plugin");

	if (default_part_loc == NULL)
		error("read_slurm_conf: default partition not set.");

	if (node_record_count < 1) {
		error("read_slurm_conf: no nodes configured.");
		_purge_old_node_state(old_node_table_ptr, 
				      old_node_record_count);
		return EINVAL;
	}

	rehash_node();
	rehash_jobs();
	set_slurmd_addr();

	if (recover > 1) {	/* Load node, part and job info */
		(void) load_all_node_state(false);
		(void) load_all_part_state();
		load_job_ret = load_all_job_state();
	} else if (recover == 1) {	/* Load job info only */
		(void) load_all_node_state(true);
		load_job_ret = load_all_job_state();
	} else {	/* Load no info, preserve all state */
		if (old_node_table_ptr) {
			info("restoring original state of nodes");
			rc = _restore_node_state(old_node_table_ptr, 
						 old_node_record_count);
			error_code = MAX(error_code, rc);  /* not fatal */
		}
		load_last_job_id();
		reset_first_job_id();
		(void) slurm_sched_reconfig();
		xfree(state_save_dir);
	}

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
	reset_job_bitmaps();		/* must follow select_g_job_init() */

	(void) _sync_nodes_to_jobs();
	(void) sync_job_files();
	_purge_old_node_state(old_node_table_ptr, old_node_record_count);

	if ((rc = _build_bitmaps()))
		fatal("_build_bitmaps failure");
	mpi_params = slurm_get_mpi_params();
	reserve_port_config(mpi_params);
	xfree(mpi_params);

	license_free();
	if (license_init(slurmctld_conf.licenses) != SLURM_SUCCESS)
		fatal("Invalid Licenses value: %s", slurmctld_conf.licenses);

	_restore_job_dependencies();
	restore_node_features();
#ifdef 	HAVE_ELAN
	_validate_node_proc_count();
#endif
	(void) _sync_nodes_to_comp_job();/* must follow select_g_node_init() */
	load_part_uid_allow_list(1);

	load_all_resv_state(recover);
	if (recover >= 1)
		(void) trigger_state_restore();

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
	rc = _preserve_select_type_param(&slurmctld_conf,
					 old_select_type_p);
	error_code = MAX(error_code, rc);	/* not fatal */

	/* Restore job accounting info if file missing or corrupted,
	 * an extremely rare situation */
	if (load_job_ret)
		_acct_restore_active_jobs();

#ifdef HAVE_CRAY_XT
	basil_query();
#endif

	/* Sync select plugin with synchronized job/node/part data */
	select_g_reconfigure();

	slurmctld_conf.last_update = time(NULL);
	END_TIMER2("read_slurm_conf");
	return error_code;
}


/* Restore node state and size information from saved records.
 * If a node was re-configured to be down or drained, we set those states */
static int _restore_node_state(struct node_record *old_node_table_ptr, 
			       int old_node_record_count)
{
	struct node_record *node_ptr;
	int i, rc = SLURM_SUCCESS;
	hostset_t hs = NULL;
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	bool power_save_mode = false;

	if (conf->suspend_program && conf->resume_program)
		power_save_mode = true;
	slurm_conf_unlock();

	for (i = 0; i < old_node_record_count; i++) {
		uint16_t drain_flag = false, down_flag = false;
		node_ptr  = find_node_record(old_node_table_ptr[i].name);
		if (node_ptr == NULL)
			continue;

		if (IS_NODE_DOWN(node_ptr))
			down_flag = true;
		if (IS_NODE_DRAIN(node_ptr))
			drain_flag = true;
		node_ptr->node_state = old_node_table_ptr[i].node_state;
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

		node_ptr->last_response = old_node_table_ptr[i].last_response;
		if (old_node_table_ptr[i].port != node_ptr->config_ptr->cpus) {
			rc = ESLURM_NEED_RESTART;
			error("Configured cpu count change on %s (%u to %u)", 
			      node_ptr->name, old_node_table_ptr[i].port, 
			      node_ptr->config_ptr->cpus);
		}
		node_ptr->cpus          = old_node_table_ptr[i].cpus;
		node_ptr->sockets       = old_node_table_ptr[i].sockets;
		node_ptr->cores         = old_node_table_ptr[i].cores;
		node_ptr->threads       = old_node_table_ptr[i].threads;
		node_ptr->real_memory   = old_node_table_ptr[i].real_memory;
		node_ptr->tmp_disk      = old_node_table_ptr[i].tmp_disk;
		if (node_ptr->reason == NULL) {
			/* Recover only if not explicitly set in slurm.conf */
			node_ptr->reason	= old_node_table_ptr[i].reason;
			old_node_table_ptr[i].reason = NULL;
		}
		if (old_node_table_ptr[i].features) {
			xfree(node_ptr->features);
			node_ptr->features = old_node_table_ptr[i].features;
			old_node_table_ptr[i].features = NULL;
		}
		if (old_node_table_ptr[i].arch) {
			xfree(node_ptr->arch);
			node_ptr->arch = old_node_table_ptr[i].arch;
			old_node_table_ptr[i].arch = NULL;
		}
		if (old_node_table_ptr[i].os) {
			xfree(node_ptr->os);
			node_ptr->os = old_node_table_ptr[i].os;
			old_node_table_ptr[i].os = NULL;
		}
	}

	if (hs) {
		char node_names[128];
		hostset_ranged_string(hs, sizeof(node_names), node_names);
		info("Cleared POWER_SAVE flag from nodes %s", node_names);
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


/*
 * _preserve_select_type_param - preserve original plugin parameters.
 *	Daemons and/or commands must be restarted for some 
 *	select plugin value changes to take effect.
 * RET zero or error code
 */
static int  _preserve_select_type_param(slurm_ctl_conf_t *ctl_conf_ptr, 
		   select_type_plugin_info_t old_select_type_p)
{
	int rc = SLURM_SUCCESS;
	
        /* SelectTypeParameters cannot change */ 
	if (old_select_type_p) {
		if (old_select_type_p != ctl_conf_ptr->select_type_param) {
			ctl_conf_ptr->select_type_param = (uint16_t) 
				old_select_type_p;
			rc =  ESLURM_INVALID_SELECTTYPE_CHANGE;
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
	}
	list_iterator_destroy(job_iterator);

	if (update_cnt)
		info("_sync_nodes_to_jobs updated state of %d nodes",
		     update_cnt);
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
			update_cnt++;
			info("Killing job_id %u", job_ptr->job_id);
			deallocate_nodes(job_ptr, false, false);
			job_completion_logger(job_ptr);
		}
	}
	list_iterator_destroy(job_iterator);
	if (update_cnt)
		info("_sync_nodes_to_comp_job completing %d jobs",
			update_cnt);
	return update_cnt;
}

/* Synchronize states of nodes and active jobs (RUNNING or COMPLETING state)
 * RET count of jobs with state changes */
static int _sync_nodes_to_active_job(struct job_record *job_ptr)
{
	int i, cnt = 0;
	uint16_t node_flags;
	struct node_record *node_ptr = node_record_table_ptr;

	job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap);
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;

		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
 
		node_ptr->run_job_cnt++; /* NOTE:
				* This counter moved to comp_job_cnt 
				* by _sync_nodes_to_comp_job() */
		if ((IS_JOB_RUNNING(job_ptr) || IS_JOB_COMPLETING(job_ptr)) &&
		    (job_ptr->details) && (job_ptr->details->shared == 0))
			node_ptr->no_share_job_cnt++;

		if (IS_NODE_DOWN(node_ptr)              &&
		    IS_JOB_RUNNING(job_ptr)             &&
		    (job_ptr->kill_on_node_fail == 0)   &&
		    (job_ptr->node_cnt > 1)) {
			/* This should only happen if a job was running 
			 * on a node that was newly configured DOWN */
			info("Removing failed node %s from job_id %u",
			     node_ptr->name, job_ptr->job_id);
			srun_node_fail(job_ptr->job_id, node_ptr->name);
			kill_step_on_node(job_ptr, node_ptr);
			excise_node_from_job(job_ptr, node_ptr);
		} else if (IS_NODE_DOWN(node_ptr)) {
			time_t now = time(NULL);
			info("Killing job %u on DOWN node %s",
			     job_ptr->job_id, node_ptr->name);
			job_ptr->job_state = JOB_NODE_FAIL | JOB_COMPLETING;
			job_ptr->end_time = MIN(job_ptr->end_time, now);
			job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
			job_ptr->state_reason = FAIL_DOWN_NODE;
			xfree(job_ptr->state_desc);
			job_completion_logger(job_ptr);
			cnt++;
		} else if (IS_NODE_UNKNOWN(node_ptr) || 
			   IS_NODE_IDLE(node_ptr)) {
			cnt++;
			node_ptr->node_state =
				NODE_STATE_ALLOCATED | node_flags;
		} 
	}
	return cnt;
}

#ifdef 	HAVE_ELAN
/* Every node in a given partition must have the same processor count 
 * at present, this function insure it */
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
 *	also reset the runing job count for scheduling policy
 */
static int _restore_job_dependencies(void)
{
	int error_code = SLURM_SUCCESS, rc;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	char *new_depend;
	bool valid;
	List license_list;

	assoc_mgr_clear_used_info();
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) {
			if (IS_JOB_RUNNING(job_ptr) || 
			    IS_JOB_SUSPENDED(job_ptr))
				acct_policy_job_begin(job_ptr);
			if (!IS_JOB_FINISHED(job_ptr))
				acct_policy_add_job_submit(job_ptr);
		}

		license_list = license_validate(job_ptr->licenses, &valid);
		if (job_ptr->license_list)
			list_destroy(job_ptr->license_list);
		if (valid)
			job_ptr->license_list = license_list;
		if (IS_JOB_RUNNING(job_ptr)) 
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
					     slurmctld_cluster_name,
					     time(NULL));
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_SUSPENDED(job_ptr))
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr)) {
			jobacct_storage_g_job_start(
				acct_db_conn, slurmctld_cluster_name, job_ptr);
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
