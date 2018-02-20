/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs
 *	Note: there is a global node table (node_record_table_ptr)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/group_cache.h"
#include "src/common/hostlist.h"
#include "src/common/layouts_mgr.h"
#include "src/common/list.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/power.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_topology.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"

#define MAX_FEATURES  32	/* max exclusive features "[fs1|fs2]"=2 */

struct node_set {		/* set of nodes with same configuration */
	uint16_t cpus_per_node;	/* NOTE: This is the minimum count,
				 * if FastSchedule==0 then individual
				 * nodes within the same configuration
				 * line (in slurm.conf) can actually
				 * have different CPU counts */
	uint64_t real_memory;
	uint32_t nodes;
	uint32_t weight;
	char     *features;
	bitstr_t *feature_bits;		/* XORed feature's position */
	bitstr_t *my_bitmap;		/* node bitmap */
};

static int  _build_node_list(struct job_record *job_ptr,
			     struct node_set **node_set_pptr,
			     int *node_set_size, char **err_msg,
			     bool test_only, bool can_reboot);
static int  _fill_in_gres_fields(struct job_record *job_ptr);
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *detail_ptr,
				 char **err_msg);

static bool _first_array_task(struct job_record *job_ptr);
static void _log_node_set(uint32_t job_id, struct node_set *node_set_ptr,
			  int node_set_size);
static int _match_feature(List feature_list, bitstr_t **inactive_bitmap);
static int _nodes_in_sets(bitstr_t *req_bitmap,
			  struct node_set * node_set_ptr,
			  int node_set_size);
static int _sort_node_set(const void *x, const void *y);
static int _pick_best_nodes(struct node_set *node_set_ptr,
			    int node_set_size, bitstr_t ** select_bitmap,
			    struct job_record *job_ptr,
			    struct part_record *part_ptr,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, bool test_only,
			    List preemptee_candidates,
			    List *preemptee_job_list, bool has_xand,
			    bitstr_t *exc_node_bitmap, bool resv_overlap);
static void _set_err_msg(bool cpus_ok, bool mem_ok, bool disk_ok,
			 bool job_mc_ok, char **err_msg);
static bitstr_t *_valid_features(struct job_record *job_ptr,
				 struct config_record *config_ptr,
				 bool can_reboot);

/*
 * _get_ntasks_per_core - Retrieve the value of ntasks_per_core from
 *	the given job_details record.  If it wasn't set, return 0xffff.
 *	Intended for use with the adjust_cpus_nppcu function.
 */
static uint16_t _get_ntasks_per_core(struct job_details *details) {

	if (details->mc_ptr)
		return details->mc_ptr->ntasks_per_core;
	else
		return 0xffff;
}

/*
 * _get_gres_alloc - Fill in the gres_alloc string field for a given
 *      job_record with the count of actually alllocated gres on each node
 * IN job_ptr - the job record whose "gres_alloc" field is to be constructed
 * RET Error number.  Currently not used (always set to 0).
 */
static int _get_gres_alloc(struct job_record *job_ptr)
{
	char                buf[128], *prefix="";
	char                gres_name[64];
	int                 i, rv;
	int                 node_cnt;
	int                 gres_type_count;
	int                 *gres_count_ids, *gres_count_vals;

	xstrcat(job_ptr->gres_alloc, "");
	if (!job_ptr->node_bitmap || !job_ptr->gres_list)
		return SLURM_SUCCESS;

	node_cnt = bit_set_count(job_ptr->node_bitmap);
	gres_type_count = list_count(job_ptr->gres_list);
	gres_count_ids  = xmalloc(sizeof(int) * gres_type_count);
	gres_count_vals = xmalloc(sizeof(int) * gres_type_count);
	rv = gres_plugin_job_count(job_ptr->gres_list, gres_type_count,
				   gres_count_ids, gres_count_vals);
	if (rv == SLURM_SUCCESS) {
		for (i = 0; i < gres_type_count; i++) {
			if (!gres_count_ids[i])
				break;
			gres_count_vals[i] *= node_cnt;
			/* Map the GRES type id back to a GRES type name. */
			gres_gresid_to_gresname(gres_count_ids[i], gres_name,
						sizeof(gres_name));
			sprintf(buf,"%s%s:%d", prefix, gres_name,
				gres_count_vals[i]);
			xstrcat(job_ptr->gres_alloc, buf);
			if (prefix[0] == '\0')
				prefix = ",";

			if (slurm_get_debug_flags() & DEBUG_FLAG_GRES) {
				debug("(%s:%d) job id: %u -- gres_alloc "
				      "substring=(%s)",
				      THIS_FILE, __LINE__, job_ptr->job_id, buf);
			}
		}
	}
	xfree(gres_count_ids);
	xfree(gres_count_vals);

	return rv;
}

/*
 * _get_gres_config - Fill in the gres_alloc string field for a given
 *      job_record with the count of gres on each node (e.g. for whole node
 *	allocations).
 * IN job_ptr - the job record whose "gres_alloc" field is to be constructed
 * RET Error number.  Currently not used (always set to 0).
 */
static int _get_gres_config(struct job_record *job_ptr)
{
	char                buf[128], *prefix="";
	List                gres_list;
	bitstr_t *	    node_bitmap = job_ptr->node_bitmap;
	struct node_record* node_ptr;
	int                 *gres_count_ids, *gres_count_vals;
	int                 *gres_count_ids_loc = NULL;
	int                 *gres_count_vals_loc = NULL;
	int                 i, ix, jx, kx, i_first, i_last, rv = 0;
	int                 count    = 0;
	int                 gres_type_count = 4; /* Guess number GRES types */
	int                 oldcount = 0;

	xstrcat(job_ptr->gres_alloc, "");
	if (node_bitmap) {
		i_first = bit_ffs(node_bitmap);
		i_last  = bit_fls(node_bitmap);
	} else {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
			debug("(%s:%d) job id: %u -- No nodes in bitmap of "
			      "job_record!",
			      THIS_FILE, __LINE__, job_ptr->job_id);
		return rv;
	}
	if (i_first == -1)      /* job has no nodes */
		i_last = -2;

	gres_count_ids  = xmalloc(sizeof(int) * gres_type_count);
	gres_count_vals = xmalloc(sizeof(int) * gres_type_count);

	/* Loop through each node allocated to the job tallying all GRES
	 * types found. */
	for (ix = i_first; ix <= i_last; ix++) {
		if (!bit_test(node_bitmap, ix))
			continue;

		node_ptr  = node_record_table_ptr + ix;
		gres_list = node_ptr->gres_list;
		if (gres_list)
			count = list_count(gres_list);
		else
			count = 0;

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
			debug("(%s:%d) job id: %u -- Count of "
			      "GRES types in the gres_list is: %d",
			      THIS_FILE, __LINE__, job_ptr->job_id, count);

		/* Only reallocate when there is an increase in size of the
		 * local arrays. */
		if (count > oldcount) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
				debug("(%s:%d) job id: %u -- Old GRES "
				      "count: %d New GRES count: %d",
				      THIS_FILE, __LINE__, job_ptr->job_id,
				      oldcount, count);

			/* Allocate arrays to hold each GRES type and its
			 * associated value found on this node.
			 */
			oldcount = count;
			i = count * sizeof(int);
			xrealloc(gres_count_ids_loc,  i);
			xrealloc(gres_count_vals_loc, i);
		}

		if (gres_list) {
			gres_plugin_node_count(gres_list, count,
					       gres_count_ids_loc,
					       gres_count_vals_loc,
					       GRES_VAL_TYPE_CONFIG);
		}

		/* Combine the local results into the master count results */
		for (jx = 0; jx < count; jx++) {
			int found = 0;

			/* Find matching GRES type. */
			for (kx = 0; kx < gres_type_count; kx++) {
				if (!gres_count_ids[kx])
					break;

				if (gres_count_ids_loc[jx] !=
				    gres_count_ids[kx])
					continue;

				/* If slot is found, update current value.*/
				gres_count_vals[kx] += gres_count_vals_loc[jx];
				found = 1;
				break;
			}

			/* If the local GRES type doesn't already appear in the
			 * list then add it. */
			if (!found) {
				/* If necessary, expand the array of GRES types
				 * being reported. */
				if (kx >= gres_type_count) {
					gres_type_count *= 2;
					i = gres_type_count * sizeof(int);
					xrealloc(gres_count_ids,  i);
					xrealloc(gres_count_vals, i);
				}
				gres_count_ids[kx]   = gres_count_ids_loc[jx];
				gres_count_vals[kx] += gres_count_vals_loc[jx];
			}
	 	}
	}
	xfree(gres_count_ids_loc);
	xfree(gres_count_vals_loc);

	/* Append value to the gres string. */
	for (jx = 0; jx < gres_type_count; jx++) {
		char gres_name[64];

		if (!gres_count_ids[jx])
			break;

		/* Map the GRES type id back to a GRES type name. */
		gres_gresid_to_gresname(gres_count_ids[jx], gres_name,
					sizeof(gres_name));

		sprintf(buf,"%s%s:%d", prefix, gres_name, gres_count_vals[jx]);
		xstrcat(job_ptr->gres_alloc, buf);
		if (prefix[0] == '\0')
			prefix = ",";

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
			debug("(%s:%d) job id: %u -- gres_alloc substring=(%s)",
			      THIS_FILE, __LINE__, job_ptr->job_id, buf);
	}
	xfree(gres_count_ids);
	xfree(gres_count_vals);

	return rv;
}

/*
 * _build_gres_alloc_string - Fill in the gres_alloc string field for a
 *      given job_record
 *	also claim required licenses and resources reserved by accounting
 *	policy association
 * IN job_ptr - the job record whose "gres_alloc" field is to be constructed
 * RET Error number.  Currently not used (always set to 0).
 */
static int _build_gres_alloc_string(struct job_record *job_ptr)
{
	static int          val_type = -1;

	if (val_type == -1) {
		char *select_type = slurm_get_select_type();
		/* Find out which select type plugin we have so we can decide
		 * what value to look for. */
		if (!xstrcmp(select_type, "select/cray"))
			val_type = GRES_VAL_TYPE_CONFIG;
		else
			val_type = GRES_VAL_TYPE_ALLOC;
		xfree(select_type);
	}

	if (val_type == GRES_VAL_TYPE_CONFIG)
		return _get_gres_config(job_ptr);
	else
		return _get_gres_alloc(job_ptr);
}

/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 *	also claim required licenses and resources reserved by accounting
 *	policy association
 * IN job_ptr - job being allocated resources
 */
extern void allocate_nodes(struct job_record *job_ptr)
{
	int i;
	struct node_record *node_ptr;
	bool has_cloud = false, has_cloud_power_save = false;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;

		if (IS_NODE_CLOUD(node_ptr)) {
			has_cloud = true;
			if (IS_NODE_POWER_SAVE(node_ptr))
				has_cloud_power_save = true;
		}
		make_node_alloc(node_ptr, job_ptr);
	}

	last_node_update = time(NULL);
	license_job_get(job_ptr);

	if (has_cloud) {
		if (has_cloud_power_save) {
			job_ptr->alias_list = xstrdup("TBD");
			job_ptr->wait_all_nodes = 1;
		} else
			set_job_alias_list(job_ptr);
	}

	return;
}

/* Set a job's alias_list string */
extern void set_job_alias_list(struct job_record *job_ptr)
{
	int i;
	struct node_record *node_ptr;

	xfree(job_ptr->alias_list);
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;

		if (IS_NODE_CLOUD(node_ptr)) {
			if (IS_NODE_POWER_SAVE(node_ptr)) {
				xfree(job_ptr->alias_list);
				job_ptr->alias_list = xstrdup("TBD");
				break;
			}
			if (job_ptr->alias_list)
				xstrcat(job_ptr->alias_list, ",");
			xstrcat(job_ptr->alias_list, node_ptr->name);
			xstrcat(job_ptr->alias_list, ":");
			xstrcat(job_ptr->alias_list, node_ptr->comm_name);
			xstrcat(job_ptr->alias_list, ":");
			xstrcat(job_ptr->alias_list, node_ptr->node_hostname);
		}
	}
}

/*
 * deallocate_nodes - for a given job, deallocate its nodes and make
 *	their state NODE_STATE_COMPLETING also release the job's licenses
 *	and resources reserved by accounting policy association
 * IN job_ptr - pointer to terminating job (already in some COMPLETING state)
 * IN timeout - true if job exhausted time limit, send REQUEST_KILL_TIMELIMIT
 *	RPC instead of REQUEST_TERMINATE_JOB
 * IN suspended - true if job was already suspended (node's run_job_cnt
 *	already decremented);
 * IN preempted - true if job is being preempted
 */
extern void deallocate_nodes(struct job_record *job_ptr, bool timeout,
			     bool suspended, bool preempted)
{
	static int select_serial = -1;
	int i;
	kill_job_msg_t *kill_job = NULL;
	agent_arg_t *agent_args = NULL;
	int down_node_cnt = 0;
	struct node_record *node_ptr;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#endif

	xassert(job_ptr);
	xassert(job_ptr->details);

	trace_job(job_ptr, __func__, "");

	if (select_serial == -1) {
		if (xstrcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}

	acct_policy_job_fini(job_ptr);
	if (select_g_job_fini(job_ptr) != SLURM_SUCCESS)
		error("select_g_job_fini(%u): %m", job_ptr->job_id);
	epilog_slurmctld(job_ptr);

	agent_args = xmalloc(sizeof(agent_arg_t));
	if (timeout)
		agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	else if (preempted)
		agent_args->msg_type = REQUEST_KILL_PREEMPTED;
	else
		agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->retry = 0;	/* re_kill_job() resends as needed */
	agent_args->hostlist = hostlist_create(NULL);
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	last_node_update    = time(NULL);
	kill_job->job_id    = job_ptr->job_id;
	kill_job->step_id   = NO_VAL;
	kill_job->job_state = job_ptr->job_state;
	kill_job->job_uid   = job_ptr->user_id;
	kill_job->nodes     = xstrdup(job_ptr->nodes);
	kill_job->time      = time(NULL);
	kill_job->start_time = job_ptr->start_time;
	kill_job->select_jobinfo = select_g_select_jobinfo_copy(
			job_ptr->select_jobinfo);
	kill_job->spank_job_env = xduparray(job_ptr->spank_job_env_size,
					    job_ptr->spank_job_env);
	kill_job->spank_job_env_size = job_ptr->spank_job_env_size;

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host &&
	    (front_end_ptr = job_ptr->front_end_ptr)) {
		agent_args->protocol_version = front_end_ptr->protocol_version;
		if (IS_NODE_DOWN(front_end_ptr)) {
			/* Issue the KILL RPC, but don't verify response */
			front_end_ptr->job_cnt_comp = 0;
			front_end_ptr->job_cnt_run  = 0;
			down_node_cnt++;
			if (job_ptr->node_bitmap_cg) {
				bit_nclear(job_ptr->node_bitmap_cg, 0,
					   node_record_count - 1);
			} else {
				error("deallocate_nodes: node_bitmap_cg is "
				      "not set");
				/* Create empty node_bitmap_cg */
				job_ptr->node_bitmap_cg =
					bit_alloc(node_record_count);
			}
			job_ptr->cpu_cnt  = 0;
			job_ptr->node_cnt = 0;
		} else {
			bool set_fe_comp = false;
			if (front_end_ptr->job_cnt_run) {
				front_end_ptr->job_cnt_run--;
			} else {
				error("%s: front_end %s job_cnt_run underflow",
				      __func__, front_end_ptr->name);
			}
			if (front_end_ptr->job_cnt_run == 0) {
				uint32_t state_flags;
				state_flags = front_end_ptr->node_state &
					      NODE_STATE_FLAGS;
				front_end_ptr->node_state = NODE_STATE_IDLE |
							    state_flags;
			}
			for (i = 0, node_ptr = node_record_table_ptr;
			     i < node_record_count; i++, node_ptr++) {
				if (!bit_test(job_ptr->node_bitmap, i))
					continue;
				make_node_comp(node_ptr, job_ptr, suspended);
				set_fe_comp = true;
			}
			if (set_fe_comp) {
				front_end_ptr->job_cnt_comp++;
				front_end_ptr->node_state |=
					NODE_STATE_COMPLETING;
			}
		}

		hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
		agent_args->node_count++;
	}
#else
	if (!job_ptr->node_bitmap_cg)
		build_cg_bitmap(job_ptr);
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!bit_test(job_ptr->node_bitmap_cg, i))
			continue;
		if (IS_NODE_DOWN(node_ptr) ||
		    IS_NODE_POWER_UP(node_ptr)) {
			/* Issue the KILL RPC, but don't verify response */
			down_node_cnt++;
			if (job_ptr->node_bitmap_cg == NULL) {
				error("deallocate_nodes: node_bitmap_cg is "
				      "not set");
				build_cg_bitmap(job_ptr);
			}
			bit_clear(job_ptr->node_bitmap_cg, i);
			job_update_tres_cnt(job_ptr, i);
			/*
			 * node_cnt indicates how many nodes we are waiting
			 * to get epilog complete messages from, so do not
			 * count down nodes. NOTE: The job's node_cnt will not
			 * match the number of entries in the node string
			 * during its completion.
			 */
			job_ptr->node_cnt--;
		}
		make_node_comp(node_ptr, job_ptr, suspended);

		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}
#endif

	if ((agent_args->node_count - down_node_cnt) == 0) {
		/* Can not wait for epilog complete to release licenses and
		 * update gang scheduling table */
		cleanup_completing(job_ptr);
	}

	if (agent_args->node_count == 0) {
		if ((job_ptr->details->expanding_jobid == 0) &&
		    (select_serial == 0)) {
			error("%s: job %u allocated no nodes to be killed on",
			      __func__, job_ptr->job_id);
		}
		slurm_free_kill_job_msg(kill_job);
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = kill_job;
	agent_queue_request(agent_args);
	return;
}

/*
 * For every element in the feature_list, identify the nodes with that feature
 * either active or available and set the feature_list's node_bitmap_active and
 * node_bitmap_avail fields accordingly.
 */
extern void find_feature_nodes(List feature_list, bool can_reboot)
{
	ListIterator feat_iter;
	job_feature_t  *job_feat_ptr;
	node_feature_t *node_feat_ptr;

	if (!feature_list)
		return;
	feat_iter = list_iterator_create(feature_list);
	while ((job_feat_ptr = (job_feature_t *) list_next(feat_iter))) {
		FREE_NULL_BITMAP(job_feat_ptr->node_bitmap_active);
		FREE_NULL_BITMAP(job_feat_ptr->node_bitmap_avail);
		node_feat_ptr = list_find_first(active_feature_list,
						list_find_feature,
						job_feat_ptr->name);
		if (node_feat_ptr && node_feat_ptr->node_bitmap) {
			job_feat_ptr->node_bitmap_active =
				bit_copy(node_feat_ptr->node_bitmap);
		} else {	/* This feature not active */
			job_feat_ptr->node_bitmap_active =
				bit_alloc(node_record_count);
		}
		if (can_reboot &&
		    node_features_g_changeable_feature(job_feat_ptr->name)) {
			node_feat_ptr = list_find_first(avail_feature_list,
							list_find_feature,
							job_feat_ptr->name);
			if (node_feat_ptr && node_feat_ptr->node_bitmap) {
				job_feat_ptr->node_bitmap_avail =
					bit_copy(node_feat_ptr->node_bitmap);
			} else {   /* This feature not available */
				job_feat_ptr->node_bitmap_avail =
					bit_alloc(node_record_count);
			}
		} else if (job_feat_ptr->node_bitmap_active) {
			job_feat_ptr->node_bitmap_avail =
				bit_copy(job_feat_ptr->node_bitmap_active);
		}
#if 1
{
		char *tmp1, *tmp2, *tmp3, *tmp4 = NULL;
		if (job_feat_ptr->op_code == FEATURE_OP_OR)
			tmp3 = "OR";
		else if (job_feat_ptr->op_code == FEATURE_OP_AND)
			tmp3 = "AND";
		else if (job_feat_ptr->op_code == FEATURE_OP_XOR)
			tmp3 = "XOR";
		else if (job_feat_ptr->op_code == FEATURE_OP_XAND)
			tmp3 = "XAND";
		else {
			xstrfmtcat(tmp4, "OTHER:%u", job_feat_ptr->op_code);
			tmp3 = tmp4;
		}
		tmp1 = bitmap2node_name(job_feat_ptr->node_bitmap_active);
		tmp2 = bitmap2node_name(job_feat_ptr->node_bitmap_avail);
		info("%s: FEAT:%s COUNT:%u PAREN:%d OP:%s ACTIVE:%s AVAIL:%s",
		     __func__, job_feat_ptr->name, job_feat_ptr->count,
		     job_feat_ptr->paren, tmp3, tmp1, tmp2);
		xfree(tmp1);
		xfree(tmp2);
		xfree(tmp4);
}
#endif
	}
	list_iterator_destroy(feat_iter);
}

/*
 * _match_feature - determine which of the job features are now inactive
 * IN job_ptr - job requesting resource allocation
 * OUT inactive_bitmap - Nodes with this as inactive feature
 * RET 1 if some nodes with this inactive feature, 0 no inactive feature
 * NOTE: Currently fully supports only AND/OR of features, not XAND/XOR
 */
static int _match_feature(List feature_list, bitstr_t **inactive_bitmap)
{
	ListIterator job_feat_iter;
	job_feature_t *job_feat_ptr;
	int last_op = FEATURE_OP_AND, last_paren_op = FEATURE_OP_AND;
	int i, last_paren_cnt = 0;
	bitstr_t *feature_bitmap, *paren_bitmap = NULL, *work_bitmap;

	xassert(inactive_bitmap);

	if (!feature_list ||			/* nothing to look for */
	    (node_features_g_count() == 0))	/* No inactive features */
		return 0;

	feature_bitmap = bit_alloc(node_record_count);
	bit_set_all(feature_bitmap);
	work_bitmap = feature_bitmap;
	job_feat_iter = list_iterator_create(feature_list);
	while ((job_feat_ptr = (job_feature_t *) list_next(job_feat_iter))) {
		if (last_paren_cnt < job_feat_ptr->paren) {
			/* Start of expression in parenthesis */
			last_paren_op = last_op;
			last_op = FEATURE_OP_AND;
			paren_bitmap = bit_alloc(node_record_count);
			bit_set_all(paren_bitmap);
			work_bitmap = paren_bitmap;
		}

		if (job_feat_ptr->node_bitmap_avail) {
			if (last_op == FEATURE_OP_AND) {
				bit_and(work_bitmap,
					job_feat_ptr->node_bitmap_active);
			} else if (last_op == FEATURE_OP_OR) {
				bit_or(work_bitmap,
				       job_feat_ptr->node_bitmap_active);
			} else {	/* FEATURE_OP_XOR or FEATURE_OP_XAND */
				bit_and(work_bitmap,
				        job_feat_ptr->node_bitmap_active);
			}
		} else {	/* feature not found */
			if (last_op == FEATURE_OP_AND) {
				bit_clear_all(work_bitmap);
			}
		}

		if (last_paren_cnt > job_feat_ptr->paren) {
			/* End of expression in parenthesis */
			if (last_paren_op == FEATURE_OP_AND) {
				bit_and(feature_bitmap, work_bitmap);
			} else if (last_paren_op == FEATURE_OP_OR) {
				bit_or(feature_bitmap, work_bitmap);
			} else {	/* FEATURE_OP_XOR or FEATURE_OP_XAND */
				bit_and(feature_bitmap, work_bitmap);
			}
			FREE_NULL_BITMAP(paren_bitmap);
			work_bitmap = feature_bitmap;
		}

		last_op = job_feat_ptr->op_code;
		last_paren_cnt = job_feat_ptr->paren;
	}
	list_iterator_destroy(job_feat_iter);
#if 0
{
	char tmp[32];
	bit_fmt(tmp, sizeof(tmp), work_bitmap);
	info("%s: NODE_BITMAP:%s", __func__, tmp);
}
#endif
	FREE_NULL_BITMAP(paren_bitmap);
	i = bit_ffc(feature_bitmap);
	if (i == -1) {	/* No required node features inactive */
		FREE_NULL_BITMAP(feature_bitmap);
		return 0;
	}
	bit_not(feature_bitmap);
	*inactive_bitmap = feature_bitmap;
	return 1;
}

/*
 * For a given job, if the available nodes differ from those with currently
 *	active features, return a bitmap of nodes with the job's required
 *	features currently active
 * IN job_ptr - job requesting resource allocation
 * IN avail_bitmap - nodes currently available for this job
 * OUT active_bitmap - nodes with job's features currently active, NULL if
 *	identical to avail_bitmap
 * NOTE: Currently fully supports only AND/OR of features, not XAND/XOR
 */
extern void build_active_feature_bitmap(struct job_record *job_ptr,
					bitstr_t *avail_bitmap,
					bitstr_t **active_bitmap)
{
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *tmp_bitmap = NULL;
	bool can_reboot;

	*active_bitmap = NULL;
	if (!details_ptr->feature_list ||	/* nothing to look for */
	    (node_features_g_count() == 0))	/* No inactive features */
		return;

	can_reboot = node_features_g_user_update(job_ptr->user_id);
	find_feature_nodes(details_ptr->feature_list, can_reboot);
	if (_match_feature(details_ptr->feature_list, &tmp_bitmap) == 0)
		return;		/* No inactive features */

	bit_not(tmp_bitmap);
	if (bit_super_set(avail_bitmap, tmp_bitmap)) {
		FREE_NULL_BITMAP(tmp_bitmap);
		return;
	}
	bit_and(tmp_bitmap, avail_bitmap);
	*active_bitmap = tmp_bitmap;

	return;
}

/* Return bitmap of nodes with all specified features currently active */
extern bitstr_t *build_active_feature_bitmap2(char *reboot_features)
{
	char *tmp, *sep;
	bitstr_t *active_node_bitmap = NULL;
	node_feature_t *node_feat_ptr;

	if (!reboot_features || (reboot_features[0] == '\0')) {
		active_node_bitmap = bit_alloc(node_record_count);
		return active_node_bitmap;
	}

	tmp = xstrdup(reboot_features);
	sep = strchr(tmp, ',');
	if (sep) {
		sep[0] = '\0';
		node_feat_ptr = list_find_first(active_feature_list,
						list_find_feature, sep + 1);
		if (node_feat_ptr && node_feat_ptr->node_bitmap) {
			active_node_bitmap =
				bit_copy(node_feat_ptr->node_bitmap);
		} else {
			active_node_bitmap = bit_alloc(node_record_count);
		}
	}
	node_feat_ptr = list_find_first(active_feature_list, list_find_feature,
					tmp);
	if (node_feat_ptr && node_feat_ptr->node_bitmap) {
		if (active_node_bitmap) {
			bit_and(active_node_bitmap, node_feat_ptr->node_bitmap);
		} else {
			active_node_bitmap =
				bit_copy(node_feat_ptr->node_bitmap);
		}
	} else {
		if (active_node_bitmap) {
			bit_clear_all(active_node_bitmap);
		} else {
			active_node_bitmap = bit_alloc(node_record_count);
		}
	}
	xfree(tmp);

	return active_node_bitmap;
}

/*
 * Decide if a job can share nodes with other jobs based on the
 * following three input parameters:
 *
 * IN user_flag - may be 0 (do not share nodes), 1 (node sharing allowed),
 *                or any other number means "don't care"
 * IN part_max_share - current partition's node sharing policy
 * IN cons_res_flag - 1 if the consumable resources flag is enable, 0 otherwise
 *
 *
 * The followed table details the node SHARED state for the various scenarios
 *
 *					part=	part=	part=	part=
 *	cons_res	user_request	EXCLUS	NO	YES	FORCE
 *	--------	------------	------	-----	-----	-----
 *	no		default		whole	whole	whole	whole/O
 *	no		exclusive	whole	whole	whole	whole/O
 *	no		share=yes	whole	whole	whole/O	whole/O
 *	yes		default		whole	share	share	share/O
 *	yes		exclusive	whole	whole	whole	whole/O
 *	yes		share=yes	whole	share	share/O	share/O
 *
 * whole  = entire node is allocated to the job
 * share  = less than entire node may be allocated to the job
 * -/O    = resources can be over-committed (e.g. gang scheduled)
 *
 * part->max_share:
 *	&SHARED_FORCE 	= FORCE
 *	0		= EXCLUSIVE
 *	1		= NO
 *	> 1		= YES
 *
 * job_ptr->details->share_res:
 *	0		= default or share=no
 *	1		= share=yes
 *
 * job_ptr->details->whole_node:
 *				  0	= default
 *	WHOLE_NODE_REQUIRED	= 1	= exclusive
 *	WHOLE_NODE_USER		= 2	= user
 *	WHOLE_NODE_MCS		= 3	= mcs
 *
 * Return values:
 *	0 = requires idle nodes
 *	1 = can use non-idle nodes
 */
static int
_resolve_shared_status(struct job_record *job_ptr, uint16_t part_max_share,
		       int cons_res_flag)
{
#ifndef HAVE_BG
	if (job_ptr->reboot)
		return 0;
#endif

	/* no sharing if partition Shared=EXCLUSIVE */
	if (part_max_share == 0) {
		job_ptr->details->whole_node = 1;
		job_ptr->details->share_res = 0;
		return 0;
	}

	/* sharing if partition Shared=FORCE with count > 1 */
	if ((part_max_share & SHARED_FORCE) &&
	    ((part_max_share & (~SHARED_FORCE)) > 1)) {
		job_ptr->details->share_res = 1;
		return 1;
	}

	if (cons_res_flag) {
		if ((job_ptr->details->share_res  == 0) ||
		    (job_ptr->details->whole_node == WHOLE_NODE_REQUIRED)) {
			job_ptr->details->share_res = 0;
			return 0;
		}
		return 1;
	} else {
		job_ptr->details->whole_node = WHOLE_NODE_REQUIRED;
		if (part_max_share == 1) { /* partition configured Shared=NO */
			job_ptr->details->share_res = 0;
			return 0;
		}
		/* share if the user requested it */
		if (job_ptr->details->share_res == 1)
			return 1;
		job_ptr->details->share_res = 0;
		return 0;
	}
}

/*
 * Remove nodes from consideration for allocation based upon "ownership" by
 * other users
 * job_ptr IN - Job to be scheduled
 * usable_node_mask IN/OUT - Nodes available for use by this job's user
 */
extern void filter_by_node_owner(struct job_record *job_ptr,
				 bitstr_t *usable_node_mask)
{
	ListIterator job_iterator;
	struct job_record *job_ptr2;
	struct node_record *node_ptr;
	int i;

	if ((job_ptr->details->whole_node == WHOLE_NODE_USER) ||
	    (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)) {
		/* Need to remove all nodes allocated to any active job from
		 * any other user */
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr2 = (struct job_record *)
				   list_next(job_iterator))) {
			if (IS_JOB_PENDING(job_ptr2) ||
			    IS_JOB_COMPLETED(job_ptr2) ||
			    (job_ptr->user_id == job_ptr2->user_id) ||
			    !job_ptr2->node_bitmap)
				continue;
			bit_and_not(usable_node_mask, job_ptr2->node_bitmap);
		}
		list_iterator_destroy(job_iterator);
		return;
	}

	/* Need to filter out any nodes exclusively allocated to other users */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if ((node_ptr->owner != NO_VAL) &&
		    (node_ptr->owner != job_ptr->user_id))
			bit_clear(usable_node_mask, i);
	}
}

/*
 * Remove nodes from consideration for allocation based upon "mcs" by
 * other users
 * job_ptr IN - Job to be scheduled
 * usable_node_mask IN/OUT - Nodes available for use by this job's mcs
 */
extern void filter_by_node_mcs(struct job_record *job_ptr, int mcs_select,
			       bitstr_t *usable_node_mask)
{
	struct node_record *node_ptr;
	int i;

	/* Need to filter out any nodes allocated with other mcs */
	if (job_ptr->mcs_label && (mcs_select == 1)) {
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			/* if there is a mcs_label -> OK if it's the same */
			if ((node_ptr->mcs_label != NULL) &&
			     xstrcmp(node_ptr->mcs_label,job_ptr->mcs_label)) {
				bit_clear(usable_node_mask, i);
			}
			/* if no mcs_label -> OK if no jobs running */
			if ((node_ptr->mcs_label == NULL) &&
			    (node_ptr->run_job_cnt != 0)) {
				bit_clear(usable_node_mask, i);
			}
		}
	} else {
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count;i++, node_ptr++) {
			 if (node_ptr->mcs_label != NULL) {
				bit_clear(usable_node_mask, i);
			}
		}
	}
}

/*
 * Remove nodes from the "avail_node_bitmap" which need to be rebooted in order
 * to be used if the job's "delay_boot" time has not yet been reached.
 */
static void _filter_by_node_feature(struct job_record *job_ptr,
				    struct node_set *node_set_ptr,
				    int node_set_size)
{
	int i;

	if ((job_ptr->details == NULL) ||
	    ((job_ptr->details->begin_time != 0) &&
 	     ((job_ptr->details->begin_time + job_ptr->delay_boot) <=
	      time(NULL))))
		return;

	for (i = 0; i < node_set_size; i++) {
		if (node_set_ptr[i].weight != INFINITE)
			continue;
		bit_and_not(avail_node_bitmap, node_set_ptr[i].my_bitmap);
	}
}

/*
 * If the job has required feature counts, then accumulate those
 * required resources using multiple calls to _pick_best_nodes()
 * and adding those selected nodes to the job's required node list.
 * Upon completion, return job's requirements to match the values
 * which were in effect upon calling this function.
 * Input and output are the same as _pick_best_nodes().
 */
static int
_get_req_features(struct node_set *node_set_ptr, int node_set_size,
		  bitstr_t **select_bitmap, struct job_record *job_ptr,
		  struct part_record *part_ptr,
		  uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes,
		  bool test_only, List *preemptee_job_list, bool can_reboot)
{
	uint32_t saved_min_nodes, saved_job_min_nodes, saved_job_num_tasks;
	bitstr_t *saved_req_node_bitmap = NULL;
	bitstr_t *inactive_bitmap = NULL;
	uint32_t saved_min_cpus, saved_req_nodes;
	int rc, tmp_node_set_size;
	int mcs_select = 0;
	struct node_set *tmp_node_set_ptr, *prev_node_set_ptr;
	int error_code = SLURM_SUCCESS, i;
	bitstr_t *feature_bitmap, *accumulate_bitmap = NULL;
	bitstr_t *save_avail_node_bitmap = NULL, *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	List preemptee_candidates = NULL;
	bool has_xand = false;
	bool resv_overlap = false;
	uint32_t powercap;
	int layout_power;

	/*
	 * Mark nodes reserved for other jobs as off limit for this job.
	 * If the job has a reservation, we've already limited the contents
	 * of select_bitmap to those nodes. Assume node reboot required
	 * since we have not selected the compute nodes yet.
	 */
	if (job_ptr->resv_name == NULL) {
		time_t start_res = time(NULL);
		rc = job_test_resv(job_ptr, &start_res, false, &resv_bitmap,
				   &exc_core_bitmap, &resv_overlap, true);
		if (rc == ESLURM_NODES_BUSY) {
			save_avail_node_bitmap = avail_node_bitmap;
			avail_node_bitmap = bit_alloc(node_record_count);
			FREE_NULL_BITMAP(resv_bitmap);
			/* Continue executing through _pick_best_nodes() below
			 * in order reject job if it can never run */
		} else if (rc != SLURM_SUCCESS) {
			FREE_NULL_BITMAP(resv_bitmap);
			FREE_NULL_BITMAP(exc_core_bitmap);
			return ESLURM_NODES_BUSY;	/* reserved */
		} else if (resv_bitmap &&
			   (!bit_equal(resv_bitmap, avail_node_bitmap))) {
			int cnt_in, cnt_out;
			cnt_in = bit_set_count(avail_node_bitmap);
			bit_and(resv_bitmap, avail_node_bitmap);
			save_avail_node_bitmap = avail_node_bitmap;
			avail_node_bitmap = resv_bitmap;
			cnt_out = bit_set_count(avail_node_bitmap);
			if (cnt_in != cnt_out) {
				debug2("Advanced reservation removed %d nodes "
				       "from consideration for job %u",
				       (cnt_in - cnt_out), job_ptr->job_id);
			}
			resv_bitmap = NULL;
		} else {
			FREE_NULL_BITMAP(resv_bitmap);
		}
	} else {
		time_t start_res = time(NULL);
		/* We do not care about return value.
		 * We are just interested in exc_core_bitmap creation */
		(void) job_test_resv(job_ptr, &start_res, false, &resv_bitmap,
				     &exc_core_bitmap, &resv_overlap, true);
		FREE_NULL_BITMAP(resv_bitmap);
	}

	if (!save_avail_node_bitmap)
		save_avail_node_bitmap = bit_copy(avail_node_bitmap);
	bit_and_not(avail_node_bitmap, booting_node_bitmap);
	filter_by_node_owner(job_ptr, avail_node_bitmap);
	if (can_reboot && !test_only)
		_filter_by_node_feature(job_ptr, node_set_ptr, node_set_size);

	/* get mcs_select */
	mcs_select = slurm_mcs_get_select(job_ptr);
	filter_by_node_mcs(job_ptr, mcs_select, avail_node_bitmap);

	/* save job and request state */
	saved_min_nodes = min_nodes;
	saved_req_nodes = req_nodes;
	saved_job_min_nodes = job_ptr->details->min_nodes;
	if (job_ptr->details->req_node_bitmap) {
		accumulate_bitmap = job_ptr->details->req_node_bitmap;
		saved_req_node_bitmap = bit_copy(accumulate_bitmap);
		job_ptr->details->req_node_bitmap = NULL;
	}
	saved_min_cpus = job_ptr->details->min_cpus;
	/*
	 * Don't mess with max_cpus here since it is only set to be a limit
	 * and not user configurable.
	 */
	job_ptr->details->min_cpus = 1;
	tmp_node_set_ptr = xmalloc(sizeof(struct node_set) * node_set_size * 2);

	/* Accumulate nodes with required feature counts. */
	preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
	if (job_ptr->details->feature_list) {
		ListIterator feat_iter;
		job_feature_t *feat_ptr;
		int last_paren_cnt = 0, last_paren_opt = FEATURE_OP_AND;
		bitstr_t *paren_bitmap = NULL, *work_bitmap;
		uint64_t smallest_min_mem = INFINITE64;
		uint64_t orig_req_mem = job_ptr->details->pn_min_memory;

		feat_iter = list_iterator_create(
				job_ptr->details->feature_list);
		while ((feat_ptr = (job_feature_t *) list_next(feat_iter))) {
			bool sort_again = false;
			if (last_paren_cnt < feat_ptr->paren) {
				/* Start of expression in parenthesis */
				if (paren_bitmap) {
					error("%s@%d: Job %u has bad feature expression: %s",
					      __func__, __LINE__,
					      job_ptr->job_id,
					      job_ptr->details->features);
					bit_free(paren_bitmap);
				}
				paren_bitmap =
					bit_copy(feat_ptr->node_bitmap_avail);
				last_paren_opt = feat_ptr->op_code;
				last_paren_cnt = feat_ptr->paren;
				continue;
			} else if (last_paren_cnt > 0) {
				if (last_paren_opt == FEATURE_OP_AND) {
					bit_and(paren_bitmap,
						feat_ptr->node_bitmap_avail);
				} else {
					bit_or(paren_bitmap,
					       feat_ptr->node_bitmap_avail);
				}
				last_paren_opt = feat_ptr->op_code;
				last_paren_cnt = feat_ptr->paren;
				if (last_paren_cnt)
					continue;
				work_bitmap = paren_bitmap;
			} else
				work_bitmap = feat_ptr->node_bitmap_avail;
			if (feat_ptr->count == 0) {
				FREE_NULL_BITMAP(paren_bitmap);
				continue;
			}
			tmp_node_set_size = 0;
			/*
			 * _pick_best_nodes() is destructive of the node_set
			 * data structure, so we need to make a copy and then
			 * purge it
			 */
			for (i = 0; i < node_set_size; i++) {
				if (!bit_super_set(node_set_ptr[i].my_bitmap,
						   work_bitmap))
					continue;
				tmp_node_set_ptr[tmp_node_set_size].
					cpus_per_node =
					node_set_ptr[i].cpus_per_node;
				tmp_node_set_ptr[tmp_node_set_size].
					real_memory =
					node_set_ptr[i].real_memory;
				tmp_node_set_ptr[tmp_node_set_size].nodes =
					node_set_ptr[i].nodes;
				tmp_node_set_ptr[tmp_node_set_size].weight =
					node_set_ptr[i].weight;
				tmp_node_set_ptr[tmp_node_set_size].features =
					xstrdup(node_set_ptr[i].features);
				tmp_node_set_ptr[tmp_node_set_size].
					feature_bits =
					bit_copy(node_set_ptr[i].feature_bits);
				tmp_node_set_ptr[tmp_node_set_size].my_bitmap =
					bit_copy(node_set_ptr[i].my_bitmap);
				prev_node_set_ptr = tmp_node_set_ptr +
						    tmp_node_set_size;
				tmp_node_set_size++;

				if (test_only || !can_reboot ||
				    (prev_node_set_ptr->weight == INFINITE))
					continue;
				inactive_bitmap =
					bit_copy(node_set_ptr[i].my_bitmap);
				bit_and_not(inactive_bitmap,
					    feat_ptr->node_bitmap_active);
				if (bit_ffs(inactive_bitmap) == -1) {
					FREE_NULL_BITMAP(inactive_bitmap);
					continue;
				}
				sort_again = true;
				if (bit_equal(prev_node_set_ptr->my_bitmap,
					      inactive_bitmap)) {
					prev_node_set_ptr->weight = INFINITE;
					FREE_NULL_BITMAP(inactive_bitmap);
					continue;
				}
				tmp_node_set_ptr[tmp_node_set_size].
					cpus_per_node =
					node_set_ptr[i].cpus_per_node;
				tmp_node_set_ptr[tmp_node_set_size].
					real_memory =
					node_set_ptr[i].real_memory;
				tmp_node_set_ptr[tmp_node_set_size].weight =
					INFINITE;
				tmp_node_set_ptr[tmp_node_set_size].features =
					xstrdup(node_set_ptr[i].features);
				tmp_node_set_ptr[tmp_node_set_size].
					feature_bits =
					bit_copy(node_set_ptr[i].feature_bits);
				tmp_node_set_ptr[tmp_node_set_size].my_bitmap =
					bit_copy(tmp_node_set_ptr
					[tmp_node_set_size-1].my_bitmap);
				bit_and(tmp_node_set_ptr[tmp_node_set_size].
					my_bitmap, inactive_bitmap);
				tmp_node_set_ptr[tmp_node_set_size].nodes =
					bit_set_count(tmp_node_set_ptr
					[tmp_node_set_size].my_bitmap);
				bit_and_not(tmp_node_set_ptr[tmp_node_set_size-1].
					my_bitmap, inactive_bitmap);
				tmp_node_set_ptr[tmp_node_set_size-1].nodes =
					bit_set_count(tmp_node_set_ptr
					[tmp_node_set_size-1].my_bitmap);
				tmp_node_set_size++;
				FREE_NULL_BITMAP(inactive_bitmap);
			}
			FREE_NULL_BITMAP(paren_bitmap);
			feature_bitmap = NULL;
			min_nodes = feat_ptr->count;
			req_nodes = feat_ptr->count;
			saved_job_num_tasks = job_ptr->details->num_tasks;
			job_ptr->details->min_nodes = feat_ptr->count;
			job_ptr->details->min_cpus = feat_ptr->count;
			if (job_ptr->details->ntasks_per_node &&
			    job_ptr->details->num_tasks) {
				job_ptr->details->num_tasks = min_nodes *
					job_ptr->details->ntasks_per_node;
			}
			FREE_NULL_LIST(*preemptee_job_list);
			job_ptr->details->pn_min_memory = orig_req_mem;
			if (sort_again) {
				qsort(tmp_node_set_ptr, tmp_node_set_size,
				      sizeof(struct node_set), _sort_node_set);
			}
			error_code = _pick_best_nodes(tmp_node_set_ptr,
					tmp_node_set_size, &feature_bitmap,
					job_ptr, part_ptr, min_nodes,
					max_nodes, req_nodes, test_only,
					preemptee_candidates,
					preemptee_job_list, false,
					exc_core_bitmap, resv_overlap);
			job_ptr->details->num_tasks = saved_job_num_tasks;
			if (job_ptr->details->pn_min_memory) {
				if (job_ptr->details->pn_min_memory <
				    smallest_min_mem)
					smallest_min_mem =
						job_ptr->details->pn_min_memory;
				else
					job_ptr->details->pn_min_memory =
						smallest_min_mem;
			}
#if 0
{
			char *tmp_str = bitmap2node_name(feature_bitmap);
			info("job %u needs %u nodes with feature %s, "
			     "using %s, error_code=%d",
			     job_ptr->job_id, feat_ptr->count,
			     feat_ptr->name, tmp_str, error_code);
			xfree(tmp_str);
}
#endif
			for (i = 0; i < tmp_node_set_size; i++) {
				xfree(tmp_node_set_ptr[i].features);
				FREE_NULL_BITMAP(tmp_node_set_ptr[i].
						 feature_bits);
				FREE_NULL_BITMAP(tmp_node_set_ptr[i].
						 my_bitmap);
			}
			if (error_code != SLURM_SUCCESS)
				break;
			if (feature_bitmap) {
				if (feat_ptr->op_code == FEATURE_OP_XAND)
					has_xand = true;
				if (has_xand) {
					/*
					 * Don't make it required since we
					 * check value on each call to
					 * _pick_best_nodes()
					 */
				} else if (job_ptr->details->req_node_bitmap) {
					bit_or(job_ptr->details->
					       req_node_bitmap,
					       feature_bitmap);
				} else {
					job_ptr->details->req_node_bitmap =
						bit_copy(feature_bitmap);
				}
				if (accumulate_bitmap) {
					bit_or(accumulate_bitmap,
					       feature_bitmap);
					FREE_NULL_BITMAP(feature_bitmap);
				} else
					accumulate_bitmap = feature_bitmap;
			}
		}
		list_iterator_destroy(feat_iter);
		if (paren_bitmap) {
			error("%s@%d: Job %u has bad feature expression: %s",
			      __func__, __LINE__, job_ptr->job_id,
			      job_ptr->details->features);
			bit_free(paren_bitmap);
		}
	}

	/* restore most of job state and accumulate remaining resources */
	if (saved_req_node_bitmap) {
		FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
		job_ptr->details->req_node_bitmap =
				bit_copy(saved_req_node_bitmap);
	}
	if (accumulate_bitmap) {
		uint32_t node_cnt;
		if (job_ptr->details->req_node_bitmap) {
			bit_or(job_ptr->details->req_node_bitmap,
				accumulate_bitmap);
			FREE_NULL_BITMAP(accumulate_bitmap);
		} else
			job_ptr->details->req_node_bitmap = accumulate_bitmap;
		node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		job_ptr->details->min_cpus = MAX(saved_min_cpus, node_cnt);
		min_nodes = MAX(saved_min_nodes, node_cnt);
		job_ptr->details->min_nodes = min_nodes;
		req_nodes = MAX(min_nodes, req_nodes);
		if (req_nodes > max_nodes)
			error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	} else {
		min_nodes = saved_min_nodes;
		req_nodes = saved_req_nodes;
		job_ptr->details->min_cpus = saved_min_cpus;
		job_ptr->details->min_nodes = saved_job_min_nodes;
	}

#if 0
{
	char *tmp_str = bitmap2node_name(job_ptr->details->req_node_bitmap);
	info("job %u requires %d:%d:%d req_nodes:%s err:%u",
	     job_ptr->job_id, min_nodes, req_nodes, max_nodes,
	     tmp_str, error_code);
	xfree(tmp_str);
}
#endif
	xfree(tmp_node_set_ptr);
	if (error_code == SLURM_SUCCESS) {
		FREE_NULL_LIST(*preemptee_job_list);
		error_code = _pick_best_nodes(node_set_ptr, node_set_size,
				select_bitmap, job_ptr, part_ptr, min_nodes,
				max_nodes, req_nodes, test_only,
				preemptee_candidates, preemptee_job_list,
				has_xand, exc_core_bitmap, resv_overlap);
	}
#if 0
{
	char *tmp_str = bitmap2node_name(*select_bitmap);
	info("job %u allocated nodes:%s err:%u",
		job_ptr->job_id, tmp_str, error_code);
	xfree(tmp_str);
}
#endif

	/*
	 * PowerCapping logic : now that we have the list of selected nodes
	 * we need to ensure that using this nodes respects the amount of
	 * available power as returned by the capping logic.
	 * If it is not the case, then ensure that the job stays pending
	 * by returning a relevant error code:
	 *  ESLURM_POWER_NOT_AVAIL : if the current capping is blocking
	 *  ESLURM_POWER_RESERVED  : if the current capping and the power
	 *                           reservations are blocking
	 */
	if (error_code != SLURM_SUCCESS) {
		debug5("powercapping: checking job %u : skipped, not eligible",
		       job_ptr->job_id);
	} else if ((powercap = powercap_get_cluster_current_cap()) == 0) {
		debug5("powercapping: checking job %u : skipped, capping "
		       "disabled", job_ptr->job_id);
	} else if ((layout_power = which_power_layout()) == 0) {
		debug5("powercapping disabled %d", which_power_layout());
	} else if (!power_layout_ready()){
		debug3("powercapping: checking job %u : skipped, problems with "
		       "layouts, capping disabled", job_ptr->job_id);
	} else {
		uint32_t min_watts, max_watts, job_cap, tmp_pcap_cpu_freq = 0;
		uint32_t cur_max_watts, tmp_max_watts = 0;
		uint32_t cpus_per_node, *tmp_max_watts_dvfs = NULL;
		bitstr_t *tmp_bitmap;
		int k = 1, *allowed_freqs = NULL;
		float ratio = 0;
		bool reboot;

		/*
		 * centralized synchronization of all key/values
		 */
		layouts_entity_pull_kv("power", "Cluster", "CurrentSumPower");

		/*
		 * get current powercapping logic state (min,cur,max)
		 */
		max_watts = powercap_get_cluster_max_watts();
		min_watts = powercap_get_cluster_min_watts();
		cur_max_watts = powercap_get_cluster_current_max_watts();
		/*
		 * in case of INFINITE cap, set it to max watts as it
		 * is done in the powercapping logic
		 */
		if (powercap == INFINITE)
			powercap = max_watts;

		/*
		 * build a temporary bitmap using idle_node_bitmap and
		 * remove the selected bitmap from this bitmap.
		 * Then compute the amount of power required for such a
		 * configuration to check that is is allowed by the current
		 * power cap
		 */
		tmp_bitmap = bit_copy(idle_node_bitmap);
		bit_and_not(tmp_bitmap, *select_bitmap);
		if (layout_power == 1)
			tmp_max_watts =
				 powercap_get_node_bitmap_maxwatts(tmp_bitmap);
		else if (layout_power == 2) {
			allowed_freqs =
				 powercap_get_job_nodes_numfreq(*select_bitmap,
					  job_ptr->details->cpu_freq_min,
					  job_ptr->details->cpu_freq_max);
			if (allowed_freqs[0] != 0) {
				tmp_max_watts_dvfs =
					xmalloc(sizeof(uint32_t) *
						(allowed_freqs[0]+1));
			}
			if (job_ptr->details->min_nodes == 0) {
				error("%s: Job %u min_nodes is zero",
				      __func__, job_ptr->job_id);
				job_ptr->details->min_nodes = 1;
			}
			cpus_per_node = job_ptr->details->min_cpus /
					job_ptr->details->min_nodes;
			tmp_max_watts =
				powercap_get_node_bitmap_maxwatts_dvfs(
					tmp_bitmap, *select_bitmap,
					tmp_max_watts_dvfs, allowed_freqs,
					cpus_per_node);
		}
		bit_free(tmp_bitmap);

		/*
		 * get job cap based on power reservation on the system,
		 * if no reservation matches the job caracteristics, the
		 * powercap or the max_wattswill be returned.
		 * select the return code based on the impact of
		 * reservations on the failure
		 */
		reboot = node_features_reboot_test(job_ptr, *select_bitmap);
		job_cap = powercap_get_job_cap(job_ptr, time(NULL), reboot);

		if ((layout_power == 1) ||
		    ((layout_power == 2) && (allowed_freqs[0] == 0))) {
			if (tmp_max_watts > job_cap) {
				FREE_NULL_BITMAP(*select_bitmap);
				if ((job_cap < powercap) &&
				    (tmp_max_watts <= powercap))
					error_code = ESLURM_POWER_RESERVED;
				else
					error_code = ESLURM_POWER_NOT_AVAIL;
			}
		} else if (layout_power == 2) {
			if (((tmp_max_watts > job_cap) ||
			    (job_cap < powercap) ||
			    (powercap < max_watts)) && (tmp_max_watts_dvfs)) {

			/*
			 * Calculation of the CPU Frequency to set for the job:
			 * The optimal CPU Frequency is the maximum allowed
			 * CPU Frequency that all idle nodes could run so that
			 * the total power consumption of the cluster is below
			 * the powercap value.since the number of Idle nodes
			 * may change in every schedule the optimal CPU
			 * Frequency may also change from one job to another.
			 */
				k = powercap_get_job_optimal_cpufreq(job_cap,
							  allowed_freqs);
				while ((tmp_max_watts_dvfs[k] > job_cap) &&
				       (k < allowed_freqs[0] + 1)) {
					k++;
				}
				if (k == allowed_freqs[0] + 1) {
					if ((job_cap < powercap) &&
					    (tmp_max_watts_dvfs[k] <= powercap)){
						error_code =
							ESLURM_POWER_RESERVED;
					} else {
						error_code =
							ESLURM_POWER_NOT_AVAIL;
					}
				} else {
					tmp_max_watts = tmp_max_watts_dvfs[k];
					tmp_pcap_cpu_freq =
						powercap_get_cpufreq(
							*select_bitmap,
							allowed_freqs[k]);
				}

				job_ptr->details->cpu_freq_min = tmp_pcap_cpu_freq;
				job_ptr->details->cpu_freq_max = tmp_pcap_cpu_freq;
				job_ptr->details->cpu_freq_gov = 0x10;

			/*
			 * Since we alter the DVFS of jobs we need to deal with
			 * their time_limit to calculate the extra time needed
			 * for them to complete the execution without getting
			 * killed there should be a parameter to declare the
			 * effect of cpu frequency on execution time for the
			 * moment we use time_limit and time_min
			 * This has to be done to allow backfilling
			 */
				ratio = (1 + (float)allowed_freqs[k] /
					     (float)allowed_freqs[-1]);
				if ((job_ptr->time_limit != INFINITE) &&
				    (job_ptr->time_limit != NO_VAL))
					job_ptr->time_limit = (ratio *
						  job_ptr->time_limit);
				if ((job_ptr->time_min != INFINITE) &&
				    (job_ptr->time_min != NO_VAL))
					job_ptr->time_min = (ratio *
						  job_ptr->time_min);
			}
		}
		xfree(allowed_freqs);
		xfree(tmp_max_watts_dvfs);

		debug2("powercapping: checking job %u : min=%u cur=%u "
		       "[new=%u] [resv_cap=%u] [cap=%u] max=%u : %s",
		       job_ptr->job_id, min_watts, cur_max_watts,
		       tmp_max_watts, job_cap, powercap, max_watts,
		       slurm_strerror(error_code));
	}

	FREE_NULL_LIST(preemptee_candidates);

	/* restore job's initial required node bitmap */
	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	job_ptr->details->req_node_bitmap = saved_req_node_bitmap;
	job_ptr->details->min_cpus = saved_min_cpus;
	job_ptr->details->min_nodes = saved_job_min_nodes;

	/* Restore available node bitmap, ignoring reservations */
	if (save_avail_node_bitmap) {
		FREE_NULL_BITMAP(avail_node_bitmap);
		avail_node_bitmap = save_avail_node_bitmap;
	}
	FREE_NULL_BITMAP(exc_core_bitmap);

	return error_code;
}

/*
 * _pick_best_nodes - from a weight order list of all nodes satisfying a
 *	job's specifications, select the "best" for use
 * IN node_set_ptr - pointer to node specification information
 * IN node_set_size - number of entries in records pointed to by node_set_ptr
 * OUT select_bitmap - returns bitmap of selected nodes, must FREE_NULL_BITMAP
 * IN job_ptr - pointer to job being scheduled
 * IN part_ptr - pointer to the partition in which the job is being scheduled
 * IN min_nodes - minimum count of nodes required by the job
 * IN max_nodes - maximum count of nodes required by the job (0==no limit)
 * IN req_nodes - requested (or desired) count of nodes
 * IN test_only - do not actually allocate resources
 * IN/OUT preemptee_job_list - list of pointers to jobs to be preempted
 * IN exc_core_bitmap - cores which can not be used
 *	NULL on first entry
 * IN has_xand - set of the constraint list includes XAND operators *and*
 *		 we have already satisfied them all
 * in resv_overlap - designated reservation overlaps another reservation
 * RET SLURM_SUCCESS on success,
 *	ESLURM_NODES_BUSY if request can not be satisfied now,
 *	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE if request can never
 *	be satisfied,
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE if the job can not be
 *	initiated until the partition's configuration changes or
 *	ESLURM_NODE_NOT_AVAIL if required nodes are DOWN or DRAINED
 *	ESLURM_RESERVATION_BUSY if requested reservation overlaps another
 * NOTE: the caller must FREE_NULL_BITMAP memory pointed to by select_bitmap
 * Notes: The algorithm is
 *	1) If required node list is specified, determine implicitly required
 *	   processor and node count
 *	2) Determine how many disjoint required "features" are represented
 *	   (e.g. "FS1|FS2|FS3")
 *	3) For each feature: find matching node table entries, identify nodes
 *	   that are up and available (idle or shared) and add them to a bit
 *	   map
 *	4) Select_g_job_test() to select the "best" of those based upon
 *	   topology and/or workload
 *	5) If request can't be satisfied now, execute select_g_job_test()
 *	   against the list of nodes that exist in any state (perhaps DOWN
 *	   DRAINED or ALLOCATED) to determine if the request can
 *         ever be satisfied.
 */
static int
_pick_best_nodes(struct node_set *node_set_ptr, int node_set_size,
		 bitstr_t ** select_bitmap, struct job_record *job_ptr,
		 struct part_record *part_ptr,
		 uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes,
		 bool test_only, List preemptee_candidates,
		 List *preemptee_job_list, bool has_xand,
		 bitstr_t *exc_core_bitmap, bool resv_overlap)
{
	struct node_record *node_ptr;
	int error_code = SLURM_SUCCESS, i, j, pick_code;
	int total_nodes = 0, avail_nodes = 0;
	bitstr_t *avail_bitmap = NULL, *total_bitmap = NULL;
	bitstr_t *backup_bitmap = NULL;
	bitstr_t *possible_bitmap = NULL;
	int max_feature, min_feature;
	bool runable_ever  = false;	/* Job can ever run */
	bool runable_avail = false;	/* Job can run with available nodes */
	bool tried_sched = false;	/* Tried to schedule with avail nodes */
	static uint32_t cr_enabled = NO_VAL;
	bool preempt_flag = false;
	bool nodes_busy = false;
	int shared = 0, select_mode;
	List preemptee_cand;

	/*
	 * Since you could potentially have multiple features and the
	 * job might not request memory we need to keep track of a minimum
	 * from the selected features.  This is to fulfill commit
	 * 700e7b1d4e9.
	 * If no memory is requested but we are running with
	 * CR_*_MEMORY and the request is for
	 * nodes of different memory sizes we need to reset the
	 * pn_min_memory as select_g_job_test can
	 * alter that making it so the order of contraints
	 * matter since the first pass through this will set the
	 * pn_min_memory based on that first constraint and if
	 * it isn't smaller than all the other requests they
	 * will fail.  We have to keep track of the
	 * memory for accounting, these next 2 variables do this for us.
	 */
	uint64_t smallest_min_mem = INFINITE64;
	uint64_t orig_req_mem = job_ptr->details->pn_min_memory;

	if (test_only)
		select_mode = SELECT_MODE_TEST_ONLY;
	else
		select_mode = SELECT_MODE_RUN_NOW;

	if ((job_ptr->details->min_nodes == 0) &&
	    (job_ptr->details->max_nodes == 0)) {
		/* Zero compute node job (burst buffer use only) */
		avail_bitmap = bit_alloc(node_record_count);
		pick_code = select_g_job_test(job_ptr,
					      avail_bitmap,
					      0, 0, 0,
					      select_mode,
					      preemptee_candidates,
					      preemptee_job_list,
					      exc_core_bitmap);

		if (pick_code == SLURM_SUCCESS) {
			*select_bitmap = avail_bitmap;
			return SLURM_SUCCESS;
		} else {
			bit_free(avail_bitmap);
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
	} else if (node_set_size == 0) {
		info("%s: empty node set for selection", __func__);
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	/* Are Consumable Resources enabled?  Check once. */
	if (cr_enabled == NO_VAL) {
		cr_enabled = 0;	/* select/linear and bluegene are no-ops */
		error_code = select_g_get_info_from_plugin (SELECT_CR_PLUGIN,
							    NULL, &cr_enabled);
		if (error_code != SLURM_SUCCESS) {
			cr_enabled = NO_VAL;
			return error_code;
		}
	}

	shared = _resolve_shared_status(job_ptr, part_ptr->max_share,
					cr_enabled);
	if (cr_enabled)
		job_ptr->cr_enabled = cr_enabled; /* CR enabled for this job */

	/*
	 * If job preemption is enabled, then do NOT limit the set of available
	 * nodes by their current 'sharable' or 'idle' setting
	 */
	preempt_flag = slurm_preemption_enabled();

	if (job_ptr->details->req_node_bitmap) {  /* specific nodes required */
		/*
		 * We have already confirmed that all of these nodes have a
		 * usable configuration and are in the proper partition.
		 * Check that these nodes can be used by this job.
		 */
		if (min_nodes != 0) {
			total_nodes = bit_set_count(
				job_ptr->details->req_node_bitmap);
		}
		if (total_nodes > max_nodes) {	/* exceeds node limit */
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
		if ((job_ptr->details->core_spec != NO_VAL16) &&
		    ((job_ptr->details->core_spec & CORE_SPEC_THREAD) == 0)) {
			i = bit_ffs(job_ptr->details->req_node_bitmap);
			if (i >= 0) {
				node_ptr = node_record_table_ptr + i;
				if (slurmctld_conf.fast_schedule) {
					j = node_ptr->config_ptr->sockets *
					    node_ptr->config_ptr->cores;
				} else {
					j = node_ptr->sockets * node_ptr->cores;
				}
			}
			if ((i >= 0) && (job_ptr->details->core_spec >= j)) {
				if (part_ptr->name) {
					info("%s: job %u never runnable in partition %s",
					     __func__, job_ptr->job_id,
					     part_ptr->name);
				} else {
					info("%s: job %u never runnable",
					     __func__, job_ptr->job_id);
				}
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
		}

		/*
		 * Check the availability of these nodes.
		 * Should we check memory availability on these nodes?
		 */
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_node_bitmap)) {
			return ESLURM_NODE_NOT_AVAIL;
		}

		/*
		 * Still must go through select_g_job_test() to determine the
		 * validity of request and/or perform set-up before job launch
		 */
		total_nodes = 0;	/* reinitialize */
	}

	/* identify the min and max feature values for possible exclusive OR */
	max_feature = -1;
	min_feature = MAX_FEATURES;
	for (i = 0; i < node_set_size; i++) {
		j = bit_ffs(node_set_ptr[i].feature_bits);
		if ((j >= 0) && (j < min_feature))
			min_feature = j;
		j = bit_fls(node_set_ptr[i].feature_bits);
		if ((j >= 0) && (j > max_feature))
			max_feature = j;
	}

	debug3("%s: job %u idle_nodes %u share_nodes %u", __func__,
		job_ptr->job_id, bit_set_count(idle_node_bitmap),
		bit_set_count(share_node_bitmap));
	/*
	 * Accumulate resources for this job based upon its required
	 * features (possibly with node counts).
	 */
	for (j = min_feature; j <= max_feature; j++) {
		if (job_ptr->details->req_node_bitmap) {
			bool missing_required_nodes = false;
			for (i = 0; i < node_set_size; i++) {
				if (!bit_test(node_set_ptr[i].feature_bits, j))
					continue;
				if (avail_bitmap) {
					bit_or(avail_bitmap,
					       node_set_ptr[i].my_bitmap);
				} else {
					avail_bitmap = bit_copy(node_set_ptr[i].
								my_bitmap);
				}
			}
			if (!bit_super_set(job_ptr->details->req_node_bitmap,
					   avail_bitmap))
				missing_required_nodes = true;

			if (missing_required_nodes)
				continue;
			FREE_NULL_BITMAP(avail_bitmap);
			avail_bitmap = bit_copy(job_ptr->details->
						req_node_bitmap);
		}
		for (i = 0; i < node_set_size; i++) {
			int count1 = 0, count2 = 0;
			if (!has_xand &&
			    !bit_test(node_set_ptr[i].feature_bits, j))
				continue;

			if (total_bitmap) {
				bit_or(total_bitmap,
				       node_set_ptr[i].my_bitmap);
			} else {
				total_bitmap = bit_copy(
						node_set_ptr[i].my_bitmap);
			}

			if (node_set_ptr[i].weight == INFINITE) {
				/* Node reboot required */
				count1 = bit_set_count(node_set_ptr[i].
						       my_bitmap);
				bit_and(node_set_ptr[i].my_bitmap,
					idle_node_bitmap);
				count2 = bit_set_count(node_set_ptr[i].
						       my_bitmap);
				if (count1 != count2)
					nodes_busy = true;
			}

			bit_and(node_set_ptr[i].my_bitmap, avail_node_bitmap);
			if (!nodes_busy) {
				count1 = bit_set_count(node_set_ptr[i].
						       my_bitmap);
			}
			if (!preempt_flag) {
				if (shared) {
					bit_and(node_set_ptr[i].my_bitmap,
						share_node_bitmap);
#ifndef HAVE_BG
					bit_and_not(node_set_ptr[i].my_bitmap,
						    cg_node_bitmap);
#endif
				} else {
					bit_and(node_set_ptr[i].my_bitmap,
						idle_node_bitmap);
					/* IDLE nodes are not COMPLETING */
				}
			} else {
#ifndef HAVE_BG
				bit_and_not(node_set_ptr[i].my_bitmap,
					    cg_node_bitmap);
#endif
			}
			if (!nodes_busy) {
				count2 = bit_set_count(node_set_ptr[i].
						       my_bitmap);
				if (count1 != count2)
					nodes_busy = true;
			}
			if (avail_bitmap) {
				bit_or(avail_bitmap,
				       node_set_ptr[i].my_bitmap);
			} else {
				avail_bitmap = bit_copy(node_set_ptr[i].
							my_bitmap);
			}

			tried_sched = false;	/* need to test these nodes */

			if ((shared || preempt_flag ||
			    (switch_record_cnt > 1))     &&
			    ((i+1) < node_set_size)	 &&
			    (min_feature == max_feature) &&
			    (node_set_ptr[i].weight ==
			     node_set_ptr[i+1].weight)) {
				/* Keep accumulating so we can pick the
				 * most lightly loaded nodes */
				continue;
			}

			avail_nodes = bit_set_count(avail_bitmap);
			if ((avail_nodes  < min_nodes)	||
			    ((avail_nodes >= min_nodes)	&&
			     (avail_nodes < req_nodes)	&&
			     ((i+1) < node_set_size)))
				continue;	/* Keep accumulating nodes */

			/* NOTE: select_g_job_test() is destructive of
			 * avail_bitmap, so save a backup copy */
			backup_bitmap = bit_copy(avail_bitmap);
			FREE_NULL_LIST(*preemptee_job_list);
			if (job_ptr->details->req_node_bitmap == NULL)
				bit_and(avail_bitmap, avail_node_bitmap);
			/* Only preempt jobs when all possible nodes are being
			 * considered for use, otherwise we would preempt jobs
			 * to use the lowest weight nodes. */
			if ((i+1) < node_set_size)
				preemptee_cand = NULL;
			else
				preemptee_cand = preemptee_candidates;

			job_ptr->details->pn_min_memory = orig_req_mem;
			pick_code = select_g_job_test(job_ptr,
						      avail_bitmap,
						      min_nodes,
						      max_nodes,
						      req_nodes,
						      select_mode,
						      preemptee_cand,
						      preemptee_job_list,
						      exc_core_bitmap);
			if (job_ptr->details->pn_min_memory) {
				if (job_ptr->details->pn_min_memory <
				    smallest_min_mem)
					smallest_min_mem =
						job_ptr->details->pn_min_memory;
				else
					job_ptr->details->pn_min_memory =
						smallest_min_mem;
			}

#if 0
{
			char *tmp_str1 = bitmap2node_name(backup_bitmap);
			char *tmp_str2 = bitmap2node_name(avail_bitmap);
			info("pick job:%u err:%d nodes:%u:%u:%u mode:%u "
			     "select %s of %s",
			     job_ptr->job_id, pick_code,
			     min_nodes, req_nodes, max_nodes, select_mode,
			     tmp_str2, tmp_str1);
			xfree(tmp_str1);
			xfree(tmp_str2);
}
#endif
			if (pick_code == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(backup_bitmap);
				if (bit_set_count(avail_bitmap) > max_nodes) {
					/* end of tests for this feature */
					avail_nodes = 0;
					break;
				}
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(possible_bitmap);
				*select_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			} else {
				tried_sched = true;	/* test failed */
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = backup_bitmap;
			}
		} /* for (i = 0; i < node_set_size; i++) */

		/* try to get req_nodes now for this feature */
		if (avail_bitmap && (!tried_sched)	&&
		    (avail_nodes >= min_nodes)		&&
		    ((job_ptr->details->req_node_bitmap == NULL) ||
		     bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_bitmap))) {
			FREE_NULL_LIST(*preemptee_job_list);
			job_ptr->details->pn_min_memory = orig_req_mem;
			pick_code = select_g_job_test(job_ptr, avail_bitmap,
						      min_nodes, max_nodes,
						      req_nodes,
						      select_mode,
						      preemptee_candidates,
						      preemptee_job_list,
						      exc_core_bitmap);

			if (job_ptr->details->pn_min_memory) {
				if (job_ptr->details->pn_min_memory <
				    smallest_min_mem)
					smallest_min_mem =
						job_ptr->details->pn_min_memory;
				else
					job_ptr->details->pn_min_memory =
						smallest_min_mem;
			}

			if ((pick_code == SLURM_SUCCESS) &&
			     (bit_set_count(avail_bitmap) <= max_nodes)) {
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(possible_bitmap);
				*select_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			}
		}

		/* determine if job could possibly run (if all configured
		 * nodes available) */
		if (total_bitmap)
			total_nodes = bit_set_count(total_bitmap);
		if (total_bitmap			&&
		    (!runable_ever || !runable_avail)	&&
		    (total_nodes >= min_nodes)		&&
		    ((job_ptr->details->req_node_bitmap == NULL) ||
		     (bit_super_set(job_ptr->details->req_node_bitmap,
					total_bitmap)))) {
			avail_nodes = bit_set_count(avail_bitmap);
			if (!runable_avail && (avail_nodes >= min_nodes)) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = bit_copy(total_bitmap);
				bit_and(avail_bitmap, avail_node_bitmap);
				job_ptr->details->pn_min_memory = orig_req_mem;
				pick_code = select_g_job_test(job_ptr,
						avail_bitmap,
						min_nodes,
						max_nodes,
						req_nodes,
						SELECT_MODE_TEST_ONLY,
						preemptee_candidates, NULL,
						exc_core_bitmap);

				if (job_ptr->details->pn_min_memory) {
					if (job_ptr->details->pn_min_memory <
					    smallest_min_mem)
						smallest_min_mem =
							job_ptr->details->
							pn_min_memory;
					else
						job_ptr->details->
							pn_min_memory =
							smallest_min_mem;
				}

				if (pick_code == SLURM_SUCCESS) {
					runable_ever  = true;
					if (bit_set_count(avail_bitmap) <=
					     max_nodes)
						runable_avail = true;
					FREE_NULL_BITMAP(possible_bitmap);
					possible_bitmap = avail_bitmap;
					avail_bitmap = NULL;
				}
			}
			if (!runable_ever) {
				job_ptr->details->pn_min_memory = orig_req_mem;
				pick_code = select_g_job_test(job_ptr,
						total_bitmap,
						min_nodes,
						max_nodes,
						req_nodes,
						SELECT_MODE_TEST_ONLY,
						preemptee_candidates, NULL,
						exc_core_bitmap);

				if (job_ptr->details->pn_min_memory) {
					if (job_ptr->details->pn_min_memory <
					    smallest_min_mem)
						smallest_min_mem =
							job_ptr->details->
							pn_min_memory;
					else
						job_ptr->details->
							pn_min_memory =
							smallest_min_mem;
				}

				if (pick_code == SLURM_SUCCESS) {
					FREE_NULL_BITMAP(possible_bitmap);
					possible_bitmap = total_bitmap;
					total_bitmap = NULL;
					runable_ever = true;
				}
			}
		}
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(total_bitmap);
		if (error_code != SLURM_SUCCESS)
			break;
	}
	FREE_NULL_BITMAP(avail_bitmap);

	/* The job is not able to start right now, return a
	 * value indicating when the job can start */
	if (!runable_ever && resv_overlap) {
		error_code = ESLURM_RESERVATION_BUSY;
		return error_code;
	}
	if (!runable_ever) {
		if (part_ptr->name) {
			info("%s: job %u never runnable in partition %s",
			     __func__, job_ptr->job_id, part_ptr->name);
		} else {
			info("%s: job %u never runnable",
			     __func__, job_ptr->job_id);
		}
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	} else if (!runable_avail && !nodes_busy) {
		error_code = ESLURM_NODE_NOT_AVAIL;
	} else if (!preempt_flag && job_ptr->details->req_node_bitmap) {
		/* specific nodes required */
		if (shared) {
			if (!bit_super_set(job_ptr->details->req_node_bitmap,
					   share_node_bitmap)) {
				error_code = ESLURM_NODES_BUSY;
			}
#ifndef HAVE_BG
			if (bit_overlap(job_ptr->details->req_node_bitmap,
					cg_node_bitmap)) {
				error_code = ESLURM_NODES_BUSY;
			}
#endif
		} else if (!bit_super_set(job_ptr->details->req_node_bitmap,
					  idle_node_bitmap)) {
			error_code = ESLURM_NODES_BUSY;
			/* Note: IDLE nodes are not COMPLETING */
		}
#ifndef HAVE_BG
	} else if (job_ptr->details->req_node_bitmap &&
		   bit_overlap(job_ptr->details->req_node_bitmap,
			       cg_node_bitmap)) {
		error_code = ESLURM_NODES_BUSY;
#endif
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = ESLURM_NODES_BUSY;
		*select_bitmap = possible_bitmap;
	} else {
		FREE_NULL_BITMAP(possible_bitmap);
	}
	return error_code;
}

static void _preempt_jobs(List preemptee_job_list, bool kill_pending,
			  int *error_code, uint32_t preemptor)
{
	ListIterator iter;
	struct job_record *job_ptr;
	uint16_t mode;
	int job_cnt = 0, rc;
	checkpoint_msg_t ckpt_msg;

	iter = list_iterator_create(preemptee_job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		rc = SLURM_SUCCESS;
		mode = slurm_job_preempt_mode(job_ptr);
		if (mode == PREEMPT_MODE_CANCEL) {
			job_cnt++;
			if (!kill_pending)
				continue;
			if (slurm_job_check_grace(job_ptr, preemptor)
			    == SLURM_SUCCESS)
				continue;
			rc = job_signal(job_ptr->job_id, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been killed to "
				     "reclaim resources for job %u",
				     job_ptr->job_id, preemptor);
			}
		} else if (mode == PREEMPT_MODE_CHECKPOINT) {
			job_cnt++;
			if (!kill_pending)
				continue;
			memset(&ckpt_msg, 0, sizeof(checkpoint_msg_t));
			ckpt_msg.op	   = CHECK_REQUEUE;
			ckpt_msg.job_id    = job_ptr->job_id;
			rc = job_checkpoint(&ckpt_msg, 0, -1,
					    NO_VAL16);
			if (rc == ESLURM_NOT_SUPPORTED) {
				memset(&ckpt_msg, 0, sizeof(checkpoint_msg_t));
				ckpt_msg.op	   = CHECK_VACATE;
				ckpt_msg.job_id    = job_ptr->job_id;
				rc = job_checkpoint(&ckpt_msg, 0, -1,
						    NO_VAL16);
			}
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been checkpointed to"
				     " reclaim resources for job %u",
				     job_ptr->job_id, preemptor);
			}
		} else if (mode == PREEMPT_MODE_REQUEUE) {
			job_cnt++;
			if (!kill_pending)
				continue;
			rc = job_requeue(0, job_ptr->job_id, NULL, true, 0);
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been requeued to "
				     "reclaim resources for job %u",
				     job_ptr->job_id, preemptor);
			}
		} else if ((mode == PREEMPT_MODE_SUSPEND) &&
			   (slurmctld_conf.preempt_mode & PREEMPT_MODE_GANG)) {
			debug("preempted job %u suspended by gang scheduler "
			      "to reclaim resources for job %u",
			      job_ptr->job_id, preemptor);
		} else if (mode == PREEMPT_MODE_OFF) {
			error("%s: Invalid preempt_mode %u for job %u",
			      __func__, mode, job_ptr->job_id);
			continue;
		}

		if (rc != SLURM_SUCCESS) {
			if ((mode != PREEMPT_MODE_CANCEL)
			    && (slurm_job_check_grace(job_ptr, preemptor)
				== SLURM_SUCCESS))
				continue;

			rc = job_signal(job_ptr->job_id, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS) {
				info("%s: preempted job %u had to be killed",
				     __func__, job_ptr->job_id);
			} else {
				info("%s: preempted job %u kill failure %s",
				     __func__, job_ptr->job_id,
				     slurm_strerror(rc));
			}
		}
	}
	list_iterator_destroy(iter);

	if (job_cnt > 0)
		*error_code = ESLURM_NODES_BUSY;
}

/* Return true if this job record is
 * 1) not a job array OR
 * 2) the first task of a job array to begin execution */
static bool _first_array_task(struct job_record *job_ptr)
{
	struct job_record *meta_job_ptr;

	if (job_ptr->array_task_id == NO_VAL)
		return true;

	meta_job_ptr = find_job_record(job_ptr->array_job_id);
	if (!meta_job_ptr || !meta_job_ptr->array_recs) {
		error("%s: Could not find meta job record for job %u",
		      __func__, job_ptr->array_job_id);
		return true;
	}
	if ((meta_job_ptr->array_recs->tot_run_tasks == 1) &&	/* This task */
	    (meta_job_ptr->array_recs->tot_comp_tasks == 0))
		return true;

	return false;
}

/*
 * This job has zero node count. It is only designed to create or destroy
 * persistent burst buffer resources. Terminate it now.
 */
static void _end_null_job(struct job_record *job_ptr)
{
	time_t now = time(NULL);

	job_ptr->exit_code = 0;
	gres_plugin_job_clear(job_ptr->gres_list);
	job_ptr->job_state = JOB_RUNNING;
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	xfree(job_ptr->nodes);
	xfree(job_ptr->sched_nodes);
	job_ptr->start_time = now;
	job_ptr->state_reason = WAIT_NO_REASON;
	xfree(job_ptr->state_desc);
	job_ptr->time_last_active = now;
	if (!job_ptr->step_list)
		job_ptr->step_list = list_create(NULL);

	job_array_post_sched(job_ptr);
	(void) bb_g_job_begin(job_ptr);
	job_array_start(job_ptr);
	rebuild_job_part_list(job_ptr);
	if ((job_ptr->mail_type & MAIL_JOB_BEGIN) &&
	    ((job_ptr->mail_type & MAIL_ARRAY_TASKS) ||
	     _first_array_task(job_ptr)))
		mail_job_info(job_ptr, MAIL_JOB_BEGIN);
	slurmctld_diag_stats.jobs_started++;
	/* Call job_set_alloc_tres() before acct_policy_job_begin() */
	job_set_alloc_tres(job_ptr, false);
	acct_policy_job_begin(job_ptr);
	/*
	 * If run with slurmdbd, this is handled out of band in the job if
	 * happening right away.  If the job has already become eligible and
	 * registered in the db then the start message.
	 */
	jobacct_storage_job_start_direct(acct_db_conn, job_ptr);
	prolog_slurmctld(job_ptr);

	job_ptr->end_time = now;
	job_ptr->job_state = JOB_COMPLETE;
	job_completion_logger(job_ptr, false);
	acct_policy_job_fini(job_ptr);
	if (select_g_job_fini(job_ptr) != SLURM_SUCCESS)
		error("select_g_job_fini(%u): %m", job_ptr->job_id);
	epilog_slurmctld(job_ptr);
}

/*
 * select_nodes - select and allocate nodes to a specific job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they
 *	could be allocated now
 * IN select_node_bitmap - bitmap of nodes to be used for the
 *	job's resource allocation (not returned if NULL), caller
 *	must free
 * IN unavail_node_str - Nodes which are currently unavailable.
 * OUT err_msg - if not NULL set to error message for job, caller must xfree
 * RET 0 on success, ESLURM code from slurm_errno.h otherwise
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition
 *	config_list - global list of node configuration info
 * Notes: The algorithm is
 *	1) Build a table (node_set_ptr) of nodes with the requisite
 *	   configuration. Each table entry includes their weight,
 *	   node_list, features, etc.
 *	2) Call _pick_best_nodes() to select those nodes best satisfying
 *	   the request, (e.g. best-fit or other criterion)
 *	3) Call allocate_nodes() to perform the actual allocation
 */
extern int select_nodes(struct job_record *job_ptr, bool test_only,
			bitstr_t **select_node_bitmap, char *unavail_node_str,
			char **err_msg)
{
	int bb, error_code = SLURM_SUCCESS, i, node_set_size = 0;
	bitstr_t *select_bitmap = NULL;
	struct node_set *node_set_ptr = NULL;
	struct part_record *part_ptr = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	time_t now = time(NULL);
	bool configuring = false;
	List preemptee_job_list = NULL;
	uint32_t selected_node_cnt = NO_VAL;
	uint64_t tres_req_cnt[slurmctld_tres_cnt];
	bool can_reboot;
	uint32_t qos_flags = 0;
	/* QOS Read lock */
	assoc_mgr_lock_t qos_read_lock =
		{ READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
		  NO_LOCK, NO_LOCK, NO_LOCK };
	assoc_mgr_lock_t job_read_locks = { READ_LOCK, NO_LOCK, READ_LOCK,
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!acct_policy_job_runnable_pre_select(job_ptr, false))
		return ESLURM_ACCOUNTING_POLICY;

	part_ptr = job_ptr->part_ptr;

	/* identify partition */
	if (part_ptr == NULL) {
		part_ptr = find_part_record(job_ptr->partition);
		xassert(part_ptr);
		job_ptr->part_ptr = part_ptr;
		error("partition pointer reset for job %u, part %s",
		      job_ptr->job_id, job_ptr->partition);
	}

	/* Quick check to see if this QOS is allowed on this partition. */
	assoc_mgr_lock(&qos_read_lock);
	if (job_ptr->qos_ptr)
		qos_flags = job_ptr->qos_ptr->flags;
	if ((error_code = part_policy_valid_qos(job_ptr->part_ptr,
				job_ptr->qos_ptr, job_ptr)) != SLURM_SUCCESS) {
		assoc_mgr_unlock(&qos_read_lock);
		return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	}

	/* Quick check to see if this account is allowed on this partition. */
	if ((error_code = part_policy_valid_acct(
		     job_ptr->part_ptr,
		     job_ptr->assoc_ptr ? job_ptr->assoc_ptr->acct : NULL,
		     job_ptr))
	    != SLURM_SUCCESS) {
		assoc_mgr_unlock(&qos_read_lock);
		return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	}
	assoc_mgr_unlock(&qos_read_lock);

	if (job_ptr->priority == 0) {	/* user/admin hold */
		if (job_ptr->state_reason != FAIL_BAD_CONSTRAINTS
		    && (job_ptr->state_reason != FAIL_BURST_BUFFER_OP)
		    && (job_ptr->state_reason != WAIT_HELD)
		    && (job_ptr->state_reason != WAIT_HELD_USER)
		    && (job_ptr->state_reason != WAIT_MAX_REQUEUE)) {
			job_ptr->state_reason = WAIT_HELD;
		}
		return ESLURM_JOB_HELD;
	}

	bb = bb_g_job_test_stage_in(job_ptr, test_only);
	if (bb != 1) {
		if ((bb == -1) &&
		    (job_ptr->state_reason == FAIL_BURST_BUFFER_OP))
			return ESLURM_BURST_BUFFER_WAIT; /* Fatal BB event */
		xfree(job_ptr->state_desc);
		last_job_update = now;
		if (bb == 0)
			job_ptr->state_reason = WAIT_BURST_BUFFER_STAGING;
		else
			job_ptr->state_reason = WAIT_BURST_BUFFER_RESOURCE;
		return ESLURM_BURST_BUFFER_WAIT;
	}

	if ((job_ptr->details->min_nodes == 0) &&
	    (job_ptr->details->max_nodes == 0)) {
		if (!job_ptr->burst_buffer)
			return ESLURM_INVALID_NODE_COUNT;
		if (!test_only)
			_end_null_job(job_ptr);
		return SLURM_SUCCESS;
	}

	/* build sets of usable nodes based upon their configuration */
	can_reboot = node_features_g_user_update(job_ptr->user_id);
	error_code = _build_node_list(job_ptr, &node_set_ptr, &node_set_size,
				      err_msg, test_only, can_reboot);
	if (error_code)
		return error_code;
	if (node_set_ptr == NULL)	/* Should never be true */
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

	qsort(node_set_ptr, node_set_size, sizeof(struct node_set),
	      _sort_node_set);
	_log_node_set(job_ptr->job_id, node_set_ptr, node_set_size);

	/* ensure that selected nodes are in these node sets */
	if (job_ptr->details->req_node_bitmap) {
		error_code = _nodes_in_sets(job_ptr->details->req_node_bitmap,
					    node_set_ptr, node_set_size);
		if (error_code) {
			info("No nodes satisfy requirements for JobId=%u "
			     "in partition %s",
			     job_ptr->job_id, job_ptr->part_ptr->name);
			goto cleanup;
		}
	}

	/* enforce both user's and partition's node limits if the qos
	 * isn't set to override them */
	/* info("req: %u-%u, %u", job_ptr->details->min_nodes, */
	/*    job_ptr->details->max_nodes, part_ptr->max_nodes); */
	error_code = get_node_cnts(job_ptr, qos_flags, part_ptr,
				   &min_nodes, &req_nodes, &max_nodes);

	if ((error_code == ESLURM_ACCOUNTING_POLICY) ||
	    (error_code == ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE))
		goto cleanup;
	else if (error_code != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
		/* Select resources for the job here */
		job_array_pre_sched(job_ptr);
		error_code = _get_req_features(node_set_ptr, node_set_size,
					       &select_bitmap, job_ptr,
					       part_ptr, min_nodes, max_nodes,
					       req_nodes, test_only,
					       &preemptee_job_list, can_reboot);
	}

	/* Set this guess here to give the user tools an idea
	 * of how many nodes Slurm is planning on giving the job.
	 * This needs to be done on success or not.  It means the job
	 * could run on nodes.  We only set the wag once to avoid
	 * having to go through the bit logic multiple times.
	 */
	if (select_bitmap
	    && ((error_code == SLURM_SUCCESS) || !job_ptr->node_cnt_wag)) {
#ifdef HAVE_BG
		xassert(job_ptr->select_jobinfo);
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &selected_node_cnt);
		if (selected_node_cnt == NO_VAL) {
			/* This should never happen */
			selected_node_cnt = bit_set_count(select_bitmap);
			error("node_cnt not available at %s:%d\n",
			      __FILE__, __LINE__);
		}
#else
		selected_node_cnt = bit_set_count(select_bitmap);
#endif
		job_ptr->node_cnt_wag = selected_node_cnt;
	} else
		selected_node_cnt = req_nodes;

	memcpy(tres_req_cnt, job_ptr->tres_req_cnt, sizeof(tres_req_cnt));
	tres_req_cnt[TRES_ARRAY_CPU] =
		(uint64_t)(job_ptr->total_cpus ?
			   job_ptr->total_cpus : job_ptr->details->min_cpus);
	tres_req_cnt[TRES_ARRAY_MEM] = job_get_tres_mem(
		job_ptr->details->pn_min_memory,
		tres_req_cnt[TRES_ARRAY_CPU],
		selected_node_cnt);
	tres_req_cnt[TRES_ARRAY_NODE] = (uint64_t)selected_node_cnt;

	assoc_mgr_lock(&job_read_locks);
	gres_set_job_tres_cnt(job_ptr->gres_list,
			      selected_node_cnt,
			      tres_req_cnt,
			      true);

	tres_req_cnt[TRES_ARRAY_BILLING] =
		assoc_mgr_tres_weighted(tres_req_cnt,
					job_ptr->part_ptr->billing_weights,
					slurmctld_conf.priority_flags, true);

	if (!test_only && (selected_node_cnt != NO_VAL) &&
	    !acct_policy_job_runnable_post_select(job_ptr, tres_req_cnt, true)) {
		assoc_mgr_unlock(&job_read_locks);
		/* If there was an reason we couldn't schedule before hand we
		 * want to check if an accounting limit was also breached.  If
		 * it was we want to override the other reason so if we are
		 * backfilling we don't reserve resources if we don't have to.
		 */
		if (error_code != SLURM_SUCCESS)
			debug2("Replacing scheduling error code for job %u from '%s' to 'Accounting policy'",
			       job_ptr->job_id,
			       slurm_strerror(error_code));
		error_code = ESLURM_ACCOUNTING_POLICY;
		goto cleanup;
	}
	assoc_mgr_unlock(&job_read_locks);

	/* set up the cpu_cnt here so we can decrement it as nodes
	 * free up. total_cpus is set within _get_req_features */
	job_ptr->cpu_cnt = job_ptr->total_cpus;

	if (!test_only && preemptee_job_list
	    && (error_code == SLURM_SUCCESS)) {
		struct job_details *detail_ptr = job_ptr->details;
		time_t now = time(NULL);
		bool kill_pending = true;
		if ((detail_ptr->preempt_start_time != 0) &&
		    (detail_ptr->preempt_start_time >
		     (now - slurmctld_conf.kill_wait -
		      slurmctld_conf.msg_timeout))) {
			/* Job preemption may still be in progress,
			 * do not cancel or requeue any more jobs yet */
			kill_pending = false;
		}
		_preempt_jobs(preemptee_job_list, kill_pending, &error_code,
			      job_ptr->job_id);
		if ((error_code == ESLURM_NODES_BUSY) &&
		    (detail_ptr->preempt_start_time == 0)) {
  			detail_ptr->preempt_start_time = now;
			job_ptr->preempt_in_progress = true;
		}
	}
	if (error_code) {
		/* Fatal errors for job here */
		if (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			/* Too many nodes requested */
			debug3("JobId=%u not runnable with present config",
			       job_ptr->job_id);
			job_ptr->state_reason = WAIT_PART_NODE_LIMIT;
			xfree(job_ptr->state_desc);
			last_job_update = now;

		/* Non-fatal errors for job below */
		} else if (error_code == ESLURM_NODE_NOT_AVAIL) {
			/* Required nodes are down or drained */
			char *node_str = NULL, *unavail_node = NULL;
			debug3("JobId=%u required nodes not avail",
			       job_ptr->job_id);
			job_ptr->state_reason = WAIT_NODE_NOT_AVAIL;
			xfree(job_ptr->state_desc);
			if (unavail_node_str) {	/* Set in few cases */
				node_str = unavail_node_str;
			} else {
				bitstr_t *unavail_bitmap;
				unavail_bitmap = bit_copy(avail_node_bitmap);
				bit_not(unavail_bitmap);
				if (job_ptr->details  &&
				    job_ptr->details->req_node_bitmap &&
				    bit_overlap(unavail_bitmap,
					   job_ptr->details->req_node_bitmap)) {
					bit_and(unavail_bitmap,
						job_ptr->details->
						req_node_bitmap);
				}
				if (bit_ffs(unavail_bitmap) != -1) {
					unavail_node = bitmap2node_name(
								unavail_bitmap);
					node_str = unavail_node;
				}
				FREE_NULL_BITMAP(unavail_bitmap);
			}
			if (node_str) {
				xstrfmtcat(job_ptr->state_desc,
					   "ReqNodeNotAvail, "
					   "UnavailableNodes:%s",
					   node_str);
			} else {
				xstrfmtcat(job_ptr->state_desc,
					   "ReqNodeNotAvail, May be reserved "
					   "for other job");
			}
			xfree(unavail_node);
			last_job_update = now;
		} else if ((error_code == ESLURM_RESERVATION_NOT_USABLE) ||
			   (error_code == ESLURM_RESERVATION_BUSY)) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
		} else if ((job_ptr->state_reason == WAIT_BLOCK_MAX_ERR) ||
			   (job_ptr->state_reason == WAIT_BLOCK_D_ACTION)) {
			/* state_reason was already setup */
		} else if ((job_ptr->state_reason == WAIT_HELD) &&
			   (job_ptr->priority == 0)) {
			/* Held by select plugin due to some failure */
		} else {
			if (error_code == ESLURM_POWER_NOT_AVAIL)
				job_ptr->state_reason = WAIT_POWER_NOT_AVAIL;
			else if (error_code == ESLURM_POWER_RESERVED)
				job_ptr->state_reason = WAIT_POWER_RESERVED;
			else
				job_ptr->state_reason = WAIT_RESOURCES;
			xfree(job_ptr->state_desc);
		}
		goto cleanup;
	}

	if (test_only) {	/* set if job not highest priority */
		error_code = SLURM_SUCCESS;
		goto cleanup;
	}

	/*
	 * This job may be getting requeued, clear vestigial state information
	 * before over-writing and leaking memory or referencing old GRES or
	 * step data.
	 */
	job_ptr->bit_flags &= ~JOB_KILL_HURRY;
	job_ptr->job_state &= ~JOB_POWER_UP_NODE;
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	xfree(job_ptr->nodes);
	xfree(job_ptr->sched_nodes);
	job_ptr->exit_code = 0;
	gres_plugin_job_clear(job_ptr->gres_list);
	if (!job_ptr->step_list)
		job_ptr->step_list = list_create(NULL);

	job_ptr->node_bitmap = select_bitmap;
	select_bitmap = NULL;	/* nothing left to free */

	/*
	 * we need to have these times set to know when the endtime
	 * is for the job when we place it
	 */
	job_ptr->start_time = job_ptr->time_last_active = now;
	if ((job_ptr->limit_set.time != ADMIN_SET_LIMIT) &&
	    ((job_ptr->time_limit == NO_VAL) ||
	     ((job_ptr->time_limit > part_ptr->max_time) &&
	      !(qos_flags & QOS_FLAG_PART_TIME_LIMIT)))) {
		if (part_ptr->default_time != NO_VAL)
			job_ptr->time_limit = part_ptr->default_time;
		else
			job_ptr->time_limit = part_ptr->max_time;
		job_ptr->limit_set.time = 1;
	}

	job_end_time_reset(job_ptr);

	job_array_post_sched(job_ptr);
	if (bb_g_job_begin(job_ptr) != SLURM_SUCCESS) {
		/* Leave job queued, something is hosed */
		error_code = ESLURM_INVALID_BURST_BUFFER_REQUEST;
		error("bb_g_job_begin(%u): %s", job_ptr->job_id,
		      slurm_strerror(error_code));
		job_ptr->start_time = 0;
		job_ptr->time_last_active = 0;
		job_ptr->end_time = 0;
		job_ptr->priority = 0;
		job_ptr->state_reason = WAIT_HELD;
		last_job_update = now;
		goto cleanup;
	}
	if (select_g_job_begin(job_ptr) != SLURM_SUCCESS) {
		/* Leave job queued, something is hosed */
		error("select_g_job_begin(%u): %m", job_ptr->job_id);

		/* Cancel previously started job */
		(void) bb_g_job_revoke_alloc(job_ptr);

		error_code = ESLURM_NODES_BUSY;
		job_ptr->start_time = 0;
		job_ptr->time_last_active = 0;
		job_ptr->end_time = 0;
		job_ptr->state_reason = WAIT_RESOURCES;
		last_job_update = now;
		goto cleanup;
	}

	/* assign the nodes and stage_in the job */
	job_ptr->state_reason = WAIT_NO_REASON;
	xfree(job_ptr->state_desc);

	if (job_ptr->job_resrcs && job_ptr->job_resrcs->nodes)
		job_ptr->nodes = xstrdup(job_ptr->job_resrcs->nodes);
	else {
		error("Select plugin failed to set job resources, nodes");
		/* Do not attempt to allocate the select_bitmap nodes since
		 * select plugin failed to set job resources */

		/* Cancel previously started job */
		(void) bb_g_job_revoke_alloc(job_ptr);

		error_code = ESLURM_NODES_BUSY;
		job_ptr->start_time = 0;
		job_ptr->time_last_active = 0;
		job_ptr->end_time = 0;
		job_ptr->state_reason = WAIT_RESOURCES;
		last_job_update = now;
		goto cleanup;
	}

	/* This could be set in the select plugin so we want to keep the flag */
	configuring = IS_JOB_CONFIGURING(job_ptr);

	job_ptr->job_state = JOB_RUNNING;

	if (select_g_select_nodeinfo_set(job_ptr) != SLURM_SUCCESS) {
		error("select_g_select_nodeinfo_set(%u): %m", job_ptr->job_id);
		if (!job_ptr->job_resrcs) {
			/* If we don't exit earlier the empty job_resrcs might
			 * be dereferenced later */

			/* Cancel previously started job */
			(void) bb_g_job_revoke_alloc(job_ptr);

			error_code = ESLURM_NODES_BUSY;
			job_ptr->start_time = 0;
			job_ptr->time_last_active = 0;
			job_ptr->end_time = 0;
			job_ptr->state_reason = WAIT_RESOURCES;
			job_ptr->job_state = JOB_PENDING;
			last_job_update = now;
			goto cleanup;
		}
	}

	allocate_nodes(job_ptr);
	job_array_start(job_ptr);
	build_node_details(job_ptr, true);
	rebuild_job_part_list(job_ptr);

	if (nonstop_ops.job_begin)
		(nonstop_ops.job_begin)(job_ptr);

	if ((job_ptr->mail_type & MAIL_JOB_BEGIN) &&
	    ((job_ptr->mail_type & MAIL_ARRAY_TASKS) ||
	     _first_array_task(job_ptr)))
		mail_job_info(job_ptr, MAIL_JOB_BEGIN);

	slurmctld_diag_stats.jobs_started++;

	/* job_set_alloc_tres has to be done before acct_policy_job_begin */
	job_set_alloc_tres(job_ptr, false);
	acct_policy_job_begin(job_ptr);

	job_claim_resv(job_ptr);

	/* Update the job_record's gres and gres_alloc fields with
	 * strings representing the amount of each GRES type requested
	 *  and allocated. */
	_fill_in_gres_fields(job_ptr);
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
		debug("(%s:%d) job id: %u -- job_record->gres: (%s), "
		      "job_record->gres_alloc: (%s)",
		      THIS_FILE, __LINE__, job_ptr->job_id,
		      job_ptr->gres, job_ptr->gres_alloc);

	/* If ran with slurmdbd this is handled out of band in the
	 * job if happening right away.  If the job has already
	 * become eligible and registered in the db then the start
	 * message. */
	jobacct_storage_job_start_direct(acct_db_conn, job_ptr);

	prolog_slurmctld(job_ptr);
	reboot_job_nodes(job_ptr);
	gs_job_start(job_ptr);
	power_g_job_start(job_ptr);

	if (bit_overlap(job_ptr->node_bitmap, power_node_bitmap))
		job_ptr->job_state |= JOB_POWER_UP_NODE;
	if (configuring || IS_JOB_POWER_UP_NODE(job_ptr) ||
	    !bit_super_set(job_ptr->node_bitmap, avail_node_bitmap)) {
		/* This handles nodes explicitly requesting node reboot */
		job_ptr->job_state |= JOB_CONFIGURING;
	}

	/* Request asynchronous launch of a prolog for a
	 * non-batch job as long as the node is not configuring for
	 * a reboot first.  Job state could be changed above so we need to
	 * recheck its state to see if it's currently configuring.
	 * PROLOG_FLAG_CONTAIN also turns on PROLOG_FLAG_ALLOC. */
	if ((slurmctld_conf.prolog_flags & PROLOG_FLAG_ALLOC) &&
	    (!IS_JOB_CONFIGURING(job_ptr)))
		launch_prolog(job_ptr);

      cleanup:
	if (job_ptr->array_recs && job_ptr->array_recs->task_id_bitmap &&
	    !IS_JOB_STARTED(job_ptr) &&
	    (bit_ffs(job_ptr->array_recs->task_id_bitmap) != -1)) {
		job_ptr->array_task_id = NO_VAL;
	}
	FREE_NULL_LIST(preemptee_job_list);
	if (select_node_bitmap)
		*select_node_bitmap = select_bitmap;
	else
		FREE_NULL_BITMAP(select_bitmap);
	if (node_set_ptr) {
		for (i = 0; i < node_set_size; i++) {
			xfree(node_set_ptr[i].features);
			FREE_NULL_BITMAP(node_set_ptr[i].my_bitmap);
			FREE_NULL_BITMAP(node_set_ptr[i].feature_bits);
		}
		xfree(node_set_ptr);
	}

	if (error_code != SLURM_SUCCESS)
		FREE_NULL_BITMAP(job_ptr->node_bitmap);

#ifdef HAVE_BG
	if (error_code != SLURM_SUCCESS)
		free_job_resources(&job_ptr->job_resrcs);
#endif

	return error_code;
}

/*
 * get_node_cnts - determine the number of nodes for the requested job.
 * IN job_ptr - pointer to the job record.
 * IN qos_flags - Flags of the job_ptr's qos.  This is so we don't have to send
 *                in a pointer or lock the qos read lock before calling.
 * IN part_ptr - pointer to the job's partition.
 * OUT min_nodes - The minimum number of nodes for the job.
 * OUT req_nodes - The number of node the select plugin should target.
 * OUT max_nodes - The max number of nodes for the job.
 * RET SLURM_SUCCESS on success, ESLURM code from slurm_errno.h otherwise.
 */
extern int get_node_cnts(struct job_record *job_ptr,
			 uint32_t qos_flags,
			 struct part_record *part_ptr,
			 uint32_t *min_nodes,
			 uint32_t *req_nodes, uint32_t *max_nodes)
{
	int error_code = SLURM_SUCCESS, i;
	uint32_t acct_max_nodes;
	uint32_t wait_reason = 0;

	xassert(job_ptr);
	xassert(part_ptr);

	/* On BlueGene systems don't adjust the min/max node limits
	 * here.  We are working on midplane values. */
	if (qos_flags & QOS_FLAG_PART_MIN_NODE)
		*min_nodes = job_ptr->details->min_nodes;
	else
		*min_nodes = MAX(job_ptr->details->min_nodes,
				 part_ptr->min_nodes);
	if (!job_ptr->details->max_nodes)
		*max_nodes = part_ptr->max_nodes;
	else if (qos_flags & QOS_FLAG_PART_MAX_NODE)
		*max_nodes = job_ptr->details->max_nodes;
	else
		*max_nodes = MIN(job_ptr->details->max_nodes,
				 part_ptr->max_nodes);

	if (job_ptr->details->req_node_bitmap && job_ptr->details->max_nodes) {
		i = bit_set_count(job_ptr->details->req_node_bitmap);
		if (i > job_ptr->details->max_nodes) {
			info("Job %u required node list has more node than "
			     "the job can use (%d > %u)",
			     job_ptr->job_id, i, job_ptr->details->max_nodes);
			error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			goto end_it;
		}
	}

	/* Don't call functions in MIN/MAX it will result in the
	 * function being called multiple times. */
	acct_max_nodes = acct_policy_get_max_nodes(job_ptr, &wait_reason);
	*max_nodes = MIN(*max_nodes, acct_max_nodes);
	*max_nodes = MIN(*max_nodes, 500000);	/* prevent overflows */

	if (!job_ptr->limit_set.tres[TRES_ARRAY_NODE] &&
	    job_ptr->details->max_nodes &&
	    !(job_ptr->bit_flags & USE_MIN_NODES))
		*req_nodes = *max_nodes;
	else
		*req_nodes = *min_nodes;

	if (acct_max_nodes < *min_nodes) {
		error_code = ESLURM_ACCOUNTING_POLICY;
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = wait_reason;
		goto end_it;
	} else if (*max_nodes < *min_nodes) {
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		goto end_it;
	}
end_it:
	return error_code;
}

/*
 * Launch prolog via RPC to slurmd. This is useful when we need to run
 * prolog at allocation stage. Then we ask slurmd to launch the prolog
 * asynchroniously and wait on REQUEST_COMPLETE_PROLOG message from slurmd.
 */
extern void launch_prolog(struct job_record *job_ptr)
{
	prolog_launch_msg_t *prolog_msg_ptr;
	agent_arg_t *agent_arg_ptr;
	job_resources_t *job_resrcs_ptr;
	slurm_cred_arg_t cred_arg;
#ifndef HAVE_FRONT_END
	int i;
#endif

	xassert(job_ptr);

#ifdef HAVE_FRONT_END
	/* For a batch job the prolog will be
	 * started synchroniously by slurmd.
	 */
	if (job_ptr->batch_flag)
		return;
#endif

	prolog_msg_ptr = xmalloc(sizeof(prolog_launch_msg_t));

	/* Locks: Write job */
	if ((slurmctld_conf.prolog_flags & PROLOG_FLAG_ALLOC) &&
	    !(slurmctld_conf.prolog_flags & PROLOG_FLAG_NOHOLD))
		job_ptr->state_reason = WAIT_PROLOG;

	prolog_msg_ptr->job_id = job_ptr->job_id;
	prolog_msg_ptr->uid = job_ptr->user_id;
	prolog_msg_ptr->gid = job_ptr->group_id;
	if (!job_ptr->user_name)
		job_ptr->user_name = uid_to_string_or_null(job_ptr->user_id);
	prolog_msg_ptr->user_name = xstrdup(job_ptr->user_name);
	prolog_msg_ptr->alias_list = xstrdup(job_ptr->alias_list);
	prolog_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	prolog_msg_ptr->partition = xstrdup(job_ptr->partition);
	prolog_msg_ptr->std_err = xstrdup(job_ptr->details->std_err);
	prolog_msg_ptr->std_out = xstrdup(job_ptr->details->std_out);
	prolog_msg_ptr->work_dir = xstrdup(job_ptr->details->work_dir);
	prolog_msg_ptr->x11 = job_ptr->details->x11;
	if (prolog_msg_ptr->x11) {
		prolog_msg_ptr->x11_magic_cookie =
				xstrdup(job_ptr->details->x11_magic_cookie);
		prolog_msg_ptr->x11_target_host = xstrdup(job_ptr->alloc_node);
		prolog_msg_ptr->x11_target_port = job_ptr->details->x11_target_port;
	}
	prolog_msg_ptr->spank_job_env_size = job_ptr->spank_job_env_size;
	prolog_msg_ptr->spank_job_env = xduparray(job_ptr->spank_job_env_size,
						  job_ptr->spank_job_env);

	xassert(job_ptr->job_resrcs);
	job_resrcs_ptr = job_ptr->job_resrcs;
	memset(&cred_arg, 0, sizeof(slurm_cred_arg_t));
	cred_arg.jobid               = job_ptr->job_id;
	cred_arg.stepid              = SLURM_EXTERN_CONT;
	cred_arg.uid                 = job_ptr->user_id;
	cred_arg.gid                 = job_ptr->group_id;
	if (slurmctld_config.send_groups_in_cred) {
		/* fill in the job_record field if not yet filled in */
		if (!job_ptr->user_name)
			job_ptr->user_name = uid_to_string_or_null(job_ptr->user_id);
		/* this may still be null, in which case the client will handle */
		cred_arg.user_name = job_ptr->user_name; /* avoid extra copy */
		/* lookup and send extended gids list */
		if (!job_ptr->ngids || !job_ptr->gids)
			job_ptr->ngids = group_cache_lookup(job_ptr->user_id,
							    job_ptr->group_id,
							    job_ptr->user_name,
							    &job_ptr->gids);
		cred_arg.ngids = job_ptr->ngids;
		cred_arg.gids = job_ptr->gids; /* avoid extra copy */
	}
	cred_arg.x11                 = job_ptr->details->x11;
	cred_arg.job_core_spec       = job_ptr->details->core_spec;
	cred_arg.job_gres_list       = job_ptr->gres_list;
	cred_arg.job_nhosts          = job_ptr->job_resrcs->nhosts;
	cred_arg.job_constraints     = job_ptr->details->features;
	cred_arg.job_mem_limit       = job_ptr->details->pn_min_memory;
	cred_arg.step_mem_limit      = job_ptr->details->pn_min_memory;
	cred_arg.cores_per_socket    = job_resrcs_ptr->cores_per_socket;
	cred_arg.job_core_bitmap     = job_resrcs_ptr->core_bitmap;
	cred_arg.step_core_bitmap    = job_resrcs_ptr->core_bitmap;
	cred_arg.sockets_per_node    = job_resrcs_ptr->sockets_per_node;
	cred_arg.sock_core_rep_count = job_resrcs_ptr->sock_core_rep_count;

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	cred_arg.job_hostlist    = job_ptr->batch_host;
	cred_arg.step_hostlist   = job_ptr->batch_host;
#else
	cred_arg.job_hostlist    = job_ptr->job_resrcs->nodes;
	cred_arg.step_hostlist   = job_ptr->job_resrcs->nodes;
#endif

	prolog_msg_ptr->cred = slurm_cred_create(slurmctld_config.cred_ctx,
						 &cred_arg,
						 SLURM_PROTOCOL_VERSION);

	agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->retry = 0;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->front_end_ptr);
	xassert(job_ptr->front_end_ptr->name);
	agent_arg_ptr->protocol_version =
		job_ptr->front_end_ptr->protocol_version;
	agent_arg_ptr->hostlist = hostlist_create(job_ptr->front_end_ptr->name);
	agent_arg_ptr->node_count = 1;
#else
	agent_arg_ptr->hostlist = hostlist_create(job_ptr->nodes);
	agent_arg_ptr->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if (agent_arg_ptr->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_arg_ptr->protocol_version =
				node_record_table_ptr[i].protocol_version;
	}

	agent_arg_ptr->node_count = job_ptr->node_cnt;
#endif
	agent_arg_ptr->msg_type = REQUEST_LAUNCH_PROLOG;
	agent_arg_ptr->msg_args = (void *) prolog_msg_ptr;

	/* At least on a Cray we have to treat this as a real step, so
	 * this is where to do it.
	 */
	if (slurmctld_conf.prolog_flags & PROLOG_FLAG_CONTAIN)
		select_g_step_start(build_extern_step(job_ptr));

	/* Launch the RPC via agent */
	agent_queue_request(agent_arg_ptr);
}

/*
 * Update a job_record's gres (required GRES)
 * and gres_alloc (allocated GRES) fields according
 * to the information found in the job_record and its
 * substructures.
 * IN job_ptr - A job's job_record.
 * RET an integer representing any potential errors--
 *     currently not used.
 */

static int _fill_in_gres_fields(struct job_record *job_ptr)
{
	char buf[128];
	char *   tok, *   last = NULL, *prefix = "";
	char *subtok, *sublast = NULL;
	char *req_config  = job_ptr->gres;
	char *tmp_str;
	uint64_t ngres_req;
	int      rv = SLURM_SUCCESS;

	/* First build the GRES requested field. */
	if ((req_config == NULL) || (req_config[0] == '\0')) {
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES)
			debug("(%s:%d) job id: %u -- job_record->gres "
			      "is empty or NULL; this is OK if no GRES "
			      "was requested",
			      THIS_FILE, __LINE__, job_ptr->job_id);

		if (job_ptr->gres_req == NULL)
			xstrcat(job_ptr->gres_req, "");

	} else if (job_ptr->node_cnt > 0
		   && job_ptr->gres_req == NULL) {
		/* job_ptr->gres_req is rebuilt/replaced here */
		tmp_str = xstrdup(req_config);

		tok = strtok_r(tmp_str, ",", &last);
		while (tok) {
			/* Tokenize tok so that we discard the colon and
			 * everything after it. Then use gres_get_value_by_type
			 * to find the associated count.
			 */
			subtok = strtok_r(tok, ":", &sublast);

			/* Retrieve the number of GRES requested/required. */
			ngres_req = gres_get_value_by_type(job_ptr->gres_list,
							   subtok);

			/* In the event that we somehow have a valid
			 * GRES type but don't find a quantity for it,
			 * we simply write ":0" for the quantity.
			 */
			if (ngres_req == NO_VAL64)
				ngres_req = 0;

			/* Append value to the gres string. */
			snprintf(buf, sizeof(buf), "%s%s:%"PRIu64,
				 prefix, subtok,
				 ngres_req * job_ptr->node_cnt);

			xstrcat(job_ptr->gres_req, buf);

			if (prefix[0] == '\0')
				prefix = ",";
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES) {
				debug("(%s:%d) job id:%u -- ngres_req:"
				      "%"PRIu64", gres_req substring = (%s)",
				      THIS_FILE, __LINE__,
				      job_ptr->job_id, ngres_req, buf);
			}

			tok = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_str);
	}

	if ( !job_ptr->gres_alloc || (job_ptr->gres_alloc[0] == '\0') ) {
		/* Now build the GRES allocated field. */
		rv = _build_gres_alloc_string(job_ptr);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES) {
			debug("(%s:%d) job id: %u -- job_record->gres: (%s), "
			      "job_record->gres_alloc: (%s)",
			      THIS_FILE, __LINE__, job_ptr->job_id,
			      job_ptr->gres, job_ptr->gres_alloc);
		}
	}

	return rv;
}

/*
 * list_find_feature - find an entry in the feature list, see list.h for
 *	documentation
 * IN key - is feature name or NULL for all features
 * RET 1 if found, 0 otherwise
 */
extern int list_find_feature(void *feature_entry, void *key)
{
	node_feature_t *feature_ptr;

	if (key == NULL)
		return 1;

	feature_ptr = (node_feature_t *) feature_entry;
	if (xstrcmp(feature_ptr->name, (char *) key) == 0)
		return 1;
	return 0;
}

/*
 * valid_feature_counts - validate a job's features can be satisfied
 *	by the selected nodes (NOTE: does not process XOR or XAND operators)
 * IN job_ptr - job to operate on
 * IN use_active - if set, then only consider nodes with the identified features
 *	active, otherwise use available features
 * IN/OUT node_bitmap - nodes available for use, clear if unusable
 * OUT has_xor - set if XOR/XAND found in feature expresion
 * RET true if valid, false otherwise
 */
extern bool valid_feature_counts(struct job_record *job_ptr, bool use_active,
				 bitstr_t *node_bitmap, bool *has_xor)
{
	struct job_details *detail_ptr = job_ptr->details;
	ListIterator job_feat_iter;
	job_feature_t *job_feat_ptr;
	int last_op = FEATURE_OP_AND, last_paren_op = FEATURE_OP_AND;
	int last_paren_cnt = 0;
	bitstr_t *feature_bitmap, *paren_bitmap = NULL;
	bitstr_t *tmp_bitmap, *work_bitmap;
	bool have_count = false, rc = true, user_update;

	xassert(detail_ptr);
	xassert(node_bitmap);
	xassert(has_xor);

	*has_xor = false;
	if (detail_ptr->feature_list == NULL)	/* no constraints */
		return rc;

	user_update = node_features_g_user_update(job_ptr->user_id);
	find_feature_nodes(detail_ptr->feature_list, user_update);
	feature_bitmap = bit_copy(node_bitmap);
	work_bitmap = feature_bitmap;
	job_feat_iter = list_iterator_create(detail_ptr->feature_list);
	while ((job_feat_ptr = (job_feature_t *) list_next(job_feat_iter))) {
		if (last_paren_cnt < job_feat_ptr->paren) {
			/* Start of expression in parenthesis */
			last_paren_op = last_op;
			last_op = FEATURE_OP_AND;
			if (paren_bitmap) {
				if (job_ptr->job_id) {
					error("%s: Job %u has bad feature expression: %s",
					      __func__, job_ptr->job_id,
					      detail_ptr->features);
				} else {
					error("%s: Reservation has bad feature expression: %s",
					      __func__, detail_ptr->features);
				}
				bit_free(paren_bitmap);
			}
			paren_bitmap = bit_copy(node_bitmap);
			work_bitmap = paren_bitmap;
		}

		if (use_active)
			tmp_bitmap = job_feat_ptr->node_bitmap_active;
		else
			tmp_bitmap = job_feat_ptr->node_bitmap_avail;
		if (tmp_bitmap) {
			if (last_op == FEATURE_OP_AND) {
				bit_and(work_bitmap, tmp_bitmap);
			} else if (last_op == FEATURE_OP_OR) {
				bit_or(work_bitmap, tmp_bitmap);
			} else {	/* FEATURE_OP_XOR or FEATURE_OP_XAND */
				*has_xor = true;
				bit_or(work_bitmap, tmp_bitmap);
			}
		} else {	/* feature not found */
			if (last_op == FEATURE_OP_AND)
				bit_clear_all(work_bitmap);
		}
		if (job_feat_ptr->count)
			have_count = true;

		if (last_paren_cnt > job_feat_ptr->paren) {
			/* End of expression in parenthesis */
			if (last_paren_op == FEATURE_OP_AND) {
				bit_and(feature_bitmap, work_bitmap);
			} else if (last_paren_op == FEATURE_OP_OR) {
				bit_or(feature_bitmap, work_bitmap);
			} else {	/* FEATURE_OP_XOR or FEATURE_OP_XAND */
				*has_xor = true;
				bit_or(feature_bitmap, work_bitmap);
			}
			FREE_NULL_BITMAP(paren_bitmap);
			work_bitmap = feature_bitmap;
		}

		last_op = job_feat_ptr->op_code;
		last_paren_cnt = job_feat_ptr->paren;
	}
	list_iterator_destroy(job_feat_iter);
	if (!have_count)
		bit_and(node_bitmap, work_bitmap);
	FREE_NULL_BITMAP(feature_bitmap);
	FREE_NULL_BITMAP(paren_bitmap);
#if 0
{
	char tmp[32];
	bit_fmt(tmp, sizeof(tmp), node_bitmap);
	info("%s: RC:%d NODE_BITMAP:%s", __func__, rc, tmp);
}
#endif
	return rc;
}

/*
 * job_req_node_filter - job reqeust node filter.
 *	clear from a bitmap the nodes which can not be used for a job
 *	test memory size, required features, processor count, etc.
 * NOTE: Does not support exclusive OR of features.
 *	It just matches first element of XOR and ignores count.
 * IN job_ptr - pointer to node to be scheduled
 * IN/OUT bitmap - set of nodes being considered for use
 * RET SLURM_SUCCESS or EINVAL if can't filter (exclusive OR of features)
 */
extern int job_req_node_filter(struct job_record *job_ptr,
			       bitstr_t *avail_bitmap, bool test_only)
{
	int i;
	struct job_details *detail_ptr = job_ptr->details;
	multi_core_data_t *mc_ptr;
	struct node_record *node_ptr;
	struct config_record *config_ptr;
	bool has_xor = false;

	if (detail_ptr == NULL) {
		error("job_req_node_filter: job %u has no details",
		      job_ptr->job_id);
		return EINVAL;
	}

	mc_ptr = detail_ptr->mc_ptr;
	for (i = 0; i < node_record_count; i++) {
		if (!bit_test(avail_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		config_ptr = node_ptr->config_ptr;
		if (slurmctld_conf.fast_schedule) {
			if ((detail_ptr->pn_min_cpus  > config_ptr->cpus)   ||
			    ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) >
			      config_ptr->real_memory) 			    ||
			    ((detail_ptr->pn_min_memory & (MEM_PER_CPU)) &&
			     ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) *
			      detail_ptr->pn_min_cpus) >
			      config_ptr->real_memory) 			    ||
			    (detail_ptr->pn_min_tmp_disk >
			     config_ptr->tmp_disk)) {
				bit_clear(avail_bitmap, i);
				continue;
			}
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node > config_ptr->sockets) &&
			      (mc_ptr->sockets_per_node != NO_VAL16)) ||
			     ((mc_ptr->cores_per_socket > config_ptr->cores)   &&
			      (mc_ptr->cores_per_socket != NO_VAL16)) ||
			     ((mc_ptr->threads_per_core > config_ptr->threads) &&
			      (mc_ptr->threads_per_core != NO_VAL16)))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		} else {
			if ((detail_ptr->pn_min_cpus > node_ptr->cpus)     ||
			    ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) >
			     node_ptr->real_memory)			   ||
			    ((detail_ptr->pn_min_memory & (MEM_PER_CPU)) &&
			     ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) *
			      detail_ptr->pn_min_cpus) >
			      node_ptr->real_memory) 			   ||
			    (detail_ptr->pn_min_tmp_disk >
			     node_ptr->tmp_disk)) {
				bit_clear(avail_bitmap, i);
				continue;
			}
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node > node_ptr->sockets)   &&
			      (mc_ptr->sockets_per_node != NO_VAL16)) ||
			     ((mc_ptr->cores_per_socket > node_ptr->cores)     &&
			      (mc_ptr->cores_per_socket != NO_VAL16)) ||
			     ((mc_ptr->threads_per_core > node_ptr->threads)   &&
			      (mc_ptr->threads_per_core != NO_VAL16)))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		}
	}

	if (!valid_feature_counts(job_ptr, false, avail_bitmap, &has_xor))
		return EINVAL;

	return SLURM_SUCCESS;
}

/* Return the count of nodes which have never registered for service,
 * so we don't know their memory size, etc. */
static int _no_reg_nodes(void)
{
	static int node_count = -1;
	struct node_record *node_ptr;
	int inx;

	if (node_count == 0)	/* No need to keep testing */
		return node_count;
	node_count = 0;
	for (inx = 0, node_ptr = node_record_table_ptr; inx < node_record_count;
	     inx++, node_ptr++) {
		if (node_ptr->last_response == 0)
			node_count++;
	}
	return node_count;
}

/*
 * _build_node_list - identify which nodes could be allocated to a job
 *	based upon node features, memory, processors, etc. Note that a
 *	bitmap is set to indicate which of the job's features that the
 *	nodes satisfy.
 * IN job_ptr - pointer to node to be scheduled
 * OUT node_set_pptr - list of node sets which could be used for the job
 * OUT node_set_size - number of node_set entries
 * OUT err_msg - error message for job, caller must xfree
 * IN  test_only - true if only testing if job can be started at some point
 * IN can_reboot - if true node can use any available feature,
 *     else job can use only active features
 * RET error code
 */
static int _build_node_list(struct job_record *job_ptr,
			    struct node_set **node_set_pptr,
			    int *node_set_size, char **err_msg, bool test_only,
			    bool can_reboot)
{
	int adj_cpus, i, node_set_inx, node_set_len, power_cnt, rc;
	struct node_set *node_set_ptr, *prev_node_set_ptr;
	struct config_record *config_ptr;
	struct part_record *part_ptr = job_ptr->part_ptr;
	ListIterator config_iterator;
	int check_node_config;
	struct job_details *detail_ptr = job_ptr->details;
	bitstr_t *usable_node_mask = NULL;
	bitstr_t *inactive_bitmap = NULL;
	multi_core_data_t *mc_ptr = detail_ptr->mc_ptr;
	bitstr_t *tmp_feature;
	bool has_xor = false;
	bool resv_overlap = false;

	if (job_ptr->resv_name) {
		/*
		 * Limit node selection to those in selected reservation.
		 * Assume node reboot required since we have not selected the
		 * compute nodes yet.
		 */
		time_t start_res = time(NULL);
		rc = job_test_resv(job_ptr, &start_res, false,
				   &usable_node_mask, NULL, &resv_overlap,
				   true);
		if (rc != SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
			if (rc == ESLURM_INVALID_TIME_VALUE)
				return ESLURM_RESERVATION_NOT_USABLE;

			if (rc == ESLURM_NODES_BUSY)
				return ESLURM_NODES_BUSY;

			if (err_msg) {
				xfree(*err_msg);
				*err_msg = xstrdup("Problem using reservation");
			}
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
		if ((detail_ptr->req_node_bitmap) &&
		    (!bit_super_set(detail_ptr->req_node_bitmap,
				    usable_node_mask))) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
			FREE_NULL_BITMAP(usable_node_mask);
			if (err_msg) {
				xfree(*err_msg);
				*err_msg = xstrdup("Required nodes outside of "
						   "the reservation");
			}
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
	}


	if (detail_ptr->exc_node_bitmap) {
		if (usable_node_mask) {
			bit_and_not(usable_node_mask, detail_ptr->exc_node_bitmap);
		} else {
			usable_node_mask =
				bit_copy(detail_ptr->exc_node_bitmap);
			bit_not(usable_node_mask);
		}
	} else if (usable_node_mask == NULL) {
		usable_node_mask = bit_alloc(node_record_count);
		bit_nset(usable_node_mask, 0, (node_record_count - 1));
	}

	if (!valid_feature_counts(job_ptr, false, usable_node_mask, &has_xor)) {
		info("Job %u feature requirements can not be satisfied",
		     job_ptr->job_id);
		FREE_NULL_BITMAP(usable_node_mask);
		if (err_msg) {
			xfree(*err_msg);
			*err_msg = xstrdup("Node feature requirements can not "
					   "be satisfied");
		}
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	(void) _match_feature(job_ptr->details->feature_list, &inactive_bitmap);
	node_set_inx = 0;
	node_set_len = list_count(config_list) * 4 + 1;
	node_set_ptr = (struct node_set *)
			xmalloc(sizeof(struct node_set) * node_set_len);
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
		bool cpus_ok = false, mem_ok = false, disk_ok = false;
		bool job_mc_ok = false, config_filter = false;
		adj_cpus = adjust_cpus_nppcu(_get_ntasks_per_core(detail_ptr),
					     config_ptr->threads,
					     config_ptr->cpus);
		if (detail_ptr->pn_min_cpus <= adj_cpus)
			cpus_ok = true;
		if ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) <=
		    config_ptr->real_memory)
			mem_ok = true;
		if (detail_ptr->pn_min_tmp_disk <= config_ptr->tmp_disk)
			disk_ok = true;
		if (!mc_ptr)
			job_mc_ok = true;
		if (mc_ptr &&
		    (((mc_ptr->sockets_per_node <= config_ptr->sockets) ||
		      (mc_ptr->sockets_per_node == NO_VAL16))  &&
		     ((mc_ptr->cores_per_socket <= config_ptr->cores)   ||
		      (mc_ptr->cores_per_socket == NO_VAL16))  &&
		     ((mc_ptr->threads_per_core <= config_ptr->threads) ||
		      (mc_ptr->threads_per_core == NO_VAL16))))
			job_mc_ok = true;
		config_filter = !(cpus_ok && mem_ok && disk_ok && job_mc_ok);

		/* since nodes can register with more resources than defined */
		/* in the configuration, we want to use those higher values */
		/* for scheduling, but only as needed (slower) */
		if (slurmctld_conf.fast_schedule) {
			if (config_filter) {
				_set_err_msg(cpus_ok, mem_ok, disk_ok,
					     job_mc_ok, err_msg);
				continue;
			}
			check_node_config = 0;
		} else if (config_filter) {
			check_node_config = 1;
		} else
			check_node_config = 0;

		node_set_ptr[node_set_inx].my_bitmap =
			bit_copy(config_ptr->node_bitmap);
		bit_and(node_set_ptr[node_set_inx].my_bitmap,
			part_ptr->node_bitmap);
		if (usable_node_mask) {
			bit_and(node_set_ptr[node_set_inx].my_bitmap,
				usable_node_mask);
		}
		node_set_ptr[node_set_inx].nodes =
			bit_set_count(node_set_ptr[node_set_inx].my_bitmap);
		if (check_node_config &&
		    (node_set_ptr[node_set_inx].nodes != 0)) {
			_filter_nodes_in_set(&node_set_ptr[node_set_inx],
					     detail_ptr, err_msg);
		}
		if (node_set_ptr[node_set_inx].nodes == 0) {
			FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
			continue;
		}

		if (has_xor) {
			tmp_feature = _valid_features(job_ptr, config_ptr,
						      can_reboot);
			if (tmp_feature == NULL) {
				FREE_NULL_BITMAP(node_set_ptr[node_set_inx].
						 my_bitmap);
				continue;
			}
		} else {
			/* We've already filtered for AND/OR features */
			tmp_feature = bit_alloc(MAX_FEATURES);
			bit_set(tmp_feature, 0);
		}
		/* NOTE: FREE_NULL_BITMAP(tmp_feature) to avoid memory leak */

		node_set_ptr[node_set_inx].cpus_per_node =
			config_ptr->cpus;
		node_set_ptr[node_set_inx].real_memory =
			config_ptr->real_memory;
		node_set_ptr[node_set_inx].weight = config_ptr->weight;
		node_set_ptr[node_set_inx].features =
			xstrdup(config_ptr->feature);
		node_set_ptr[node_set_inx].feature_bits = tmp_feature;
		debug2("found %d usable nodes from config containing %s",
		       node_set_ptr[node_set_inx].nodes, config_ptr->nodes);
		prev_node_set_ptr = node_set_ptr + node_set_inx;
		node_set_inx++;
		if (node_set_inx >= node_set_len) {
			error("%s: node_set buffer filled", __func__);
			break;
		}
		if (test_only || !can_reboot)
			continue;

		if (!inactive_bitmap)	/* All features active */
			continue;
		if (bit_super_set(prev_node_set_ptr->my_bitmap,
				  inactive_bitmap)) {
			/* All nodes require reboot, just change weight */
			prev_node_set_ptr->weight = INFINITE;
			continue;
		}
		/*
		 * Split the node set record in two:
		 * one set to reboot, one set available now
		 */
		node_set_ptr[node_set_inx].cpus_per_node = config_ptr->cpus;
		node_set_ptr[node_set_inx].features =
			xstrdup(config_ptr->feature);
		node_set_ptr[node_set_inx].feature_bits = bit_copy(tmp_feature);
		node_set_ptr[node_set_inx].my_bitmap =
			bit_copy(node_set_ptr[node_set_inx-1].my_bitmap);
		bit_and(node_set_ptr[node_set_inx].my_bitmap, inactive_bitmap);
		node_set_ptr[node_set_inx].nodes = bit_set_count(
			node_set_ptr[node_set_inx].my_bitmap);
		node_set_ptr[node_set_inx].real_memory =
			config_ptr->real_memory;
		node_set_ptr[node_set_inx].weight = INFINITE;
		bit_and_not(node_set_ptr[node_set_inx-1].my_bitmap,
			    inactive_bitmap);
		node_set_ptr[node_set_inx-1].nodes -= bit_set_count(
			node_set_ptr[node_set_inx-1].my_bitmap);
		node_set_inx++;
		if (node_set_inx >= node_set_len) {
			error("%s: node_set buffer filled", __func__);
			break;
		}
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(inactive_bitmap);
	/* eliminate any incomplete node_set record */
	xfree(node_set_ptr[node_set_inx].features);
	FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
	FREE_NULL_BITMAP(node_set_ptr[node_set_inx].feature_bits);
	FREE_NULL_BITMAP(usable_node_mask);

	if (node_set_inx == 0) {
		rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info("%s: No nodes satisfy job %u requirements in partition %s",
		     __func__, job_ptr->job_id, job_ptr->part_ptr->name);
		xfree(node_set_ptr);
		xfree(job_ptr->state_desc);
		if (job_ptr->resv_name) {
			job_ptr->state_reason = WAIT_RESERVATION;
			rc = ESLURM_NODES_BUSY;
		} else if ((slurmctld_conf.fast_schedule == 0) &&
			   (_no_reg_nodes() > 0)) {
			rc = ESLURM_NODES_BUSY;
		} else {
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
		}
		return rc;
	}

	/*
	 * Clear message about any nodes which fail to satisfy specific
	 * job requirements as there are some nodes which can be used
	 */
	if (err_msg)
		xfree(*err_msg);

	/*
	 * If any nodes are powered down, put them into a new node_set
	 * record with a higher scheduling weight. This means we avoid
	 * scheduling jobs on powered down nodes where possible.
	 */
	for (i = (node_set_inx-1); i >= 0; i--) {
		power_cnt = bit_overlap(node_set_ptr[i].my_bitmap,
					power_node_bitmap);
		if (power_cnt == 0)
			continue;	/* no nodes powered down */
		if (power_cnt == node_set_ptr[i].nodes) {
			if (node_set_ptr[i].weight != INFINITE)
				node_set_ptr[i].weight = INFINITE;
			continue;	/* all nodes powered down */
		}

		/* Some nodes powered down, others up, split record */
		node_set_ptr[node_set_inx].cpus_per_node =
			node_set_ptr[i].cpus_per_node;
		node_set_ptr[node_set_inx].real_memory =
			node_set_ptr[i].real_memory;
		node_set_ptr[node_set_inx].nodes = power_cnt;
		node_set_ptr[i].nodes -= power_cnt;
		node_set_ptr[node_set_inx].weight = node_set_ptr[i].weight;
		if (node_set_ptr[node_set_inx].weight != INFINITE)
			node_set_ptr[node_set_inx].weight = INFINITE;
		node_set_ptr[node_set_inx].features =
			xstrdup(node_set_ptr[i].features);
		node_set_ptr[node_set_inx].feature_bits =
			bit_copy(node_set_ptr[i].feature_bits);
		node_set_ptr[node_set_inx].my_bitmap =
			bit_copy(node_set_ptr[i].my_bitmap);
		bit_and(node_set_ptr[node_set_inx].my_bitmap,
			power_node_bitmap);
		bit_and_not(node_set_ptr[i].my_bitmap, power_node_bitmap);

		node_set_inx++;
		if (node_set_inx >= node_set_len) {
			error("%s: node_set buffer filled", __func__);
			break;
		}
	}

	*node_set_size = node_set_inx;
	*node_set_pptr = node_set_ptr;
	return SLURM_SUCCESS;
}

static int _sort_node_set(const void *x, const void *y)
{
	struct node_set *node_set_ptr1 = (struct node_set *) x;
	struct node_set *node_set_ptr2 = (struct node_set *) y;

	if (node_set_ptr1->weight < node_set_ptr2->weight)
		return -1;
	if (node_set_ptr1->weight > node_set_ptr2->weight)
		return 1;
	return 0;
}

static void _log_node_set(uint32_t job_id, struct node_set *node_set_ptr,
			  int node_set_size)
{
/* Used for debugging purposes only */
#if 0
	char *node_list;
	int i;

	info("NodeSet for job %u", job_id);
	for (i = 0; i < node_set_size; i++) {
		node_list = bitmap2node_name(node_set_ptr[i].my_bitmap);
		info("NodeSet[%d] Nodes:%s Weight:%u", i, node_list,
		     node_set_ptr[i].weight);
		xfree(node_list);
	}
#endif
}

static void _set_err_msg(bool cpus_ok, bool mem_ok, bool disk_ok,
			 bool job_mc_ok, char **err_msg)
{
	if (!err_msg)
		return;
	if (!cpus_ok) {
		xfree(*err_msg);
		*err_msg = xstrdup("CPU count per node can not be satisfied");
		return;
	}
	if (!mem_ok) {
		xfree(*err_msg);
		*err_msg = xstrdup("Memory specification can not be satisfied");
		return;
	}
	if (!disk_ok) {
		xfree(*err_msg);
		*err_msg = xstrdup("Temporary disk specification can not be "
				   "satisfied");
		return;
	}
	if (!job_mc_ok) {
		xfree(*err_msg);
		*err_msg = xstrdup("Socket, core and/or thread specification "
				   "can not be satisfied");
		return;
	}
}

/* Remove from the node set any nodes which lack sufficient resources
 *	to satisfy the job's request */
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *job_con,
				 char **err_msg)
{
	int adj_cpus, i;
	multi_core_data_t *mc_ptr = job_con->mc_ptr;

	if (slurmctld_conf.fast_schedule) {	/* test config records */
		struct config_record *node_con = NULL;
		for (i = 0; i < node_record_count; i++) {
			bool cpus_ok = false, mem_ok = false, disk_ok = false;
			bool job_mc_ok = false;
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;
			node_con = node_record_table_ptr[i].config_ptr;
			adj_cpus = adjust_cpus_nppcu(_get_ntasks_per_core(job_con),
						     node_con->threads,
						     node_con->cpus);

			if (job_con->pn_min_cpus <= adj_cpus)
				cpus_ok = true;
			if ((job_con->pn_min_memory & (~MEM_PER_CPU)) <=
			    node_con->real_memory)
				mem_ok = true;
			if (job_con->pn_min_tmp_disk <= node_con->tmp_disk)
				disk_ok = true;
			if (!mc_ptr)
				job_mc_ok = true;
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node <= node_con->sockets)  ||
			      (mc_ptr->sockets_per_node == NO_VAL16)) &&
			     ((mc_ptr->cores_per_socket <= node_con->cores)    ||
			      (mc_ptr->cores_per_socket == NO_VAL16)) &&
			     ((mc_ptr->threads_per_core <= node_con->threads)  ||
			      (mc_ptr->threads_per_core == NO_VAL16))))
				job_mc_ok = true;
			if (cpus_ok && mem_ok && disk_ok && job_mc_ok)
				continue;

			_set_err_msg(cpus_ok, mem_ok, disk_ok, job_mc_ok,
				     err_msg);
			bit_clear(node_set_ptr->my_bitmap, i);
			if ((--(node_set_ptr->nodes)) == 0)
				break;
		}

	} else {	/* fast_schedule == 0, test individual node records */
		struct node_record   *node_ptr = NULL;
		for (i = 0; i < node_record_count; i++) {
			int job_ok = 0, job_mc_ptr_ok = 0;
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;

			node_ptr = &node_record_table_ptr[i];
			adj_cpus = adjust_cpus_nppcu(_get_ntasks_per_core(job_con),
						     node_ptr->threads,
						     node_ptr->cpus);
			if ((job_con->pn_min_cpus     <= adj_cpus)            &&
			    ((job_con->pn_min_memory & (~MEM_PER_CPU)) <=
			      node_ptr->real_memory)                          &&
			    (job_con->pn_min_tmp_disk <= node_ptr->tmp_disk))
				job_ok = 1;
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node <= node_ptr->sockets)  ||
			      (mc_ptr->sockets_per_node == NO_VAL16)) &&
			     ((mc_ptr->cores_per_socket <= node_ptr->cores)    ||
			      (mc_ptr->cores_per_socket == NO_VAL16)) &&
			     ((mc_ptr->threads_per_core <= node_ptr->threads)  ||
			      (mc_ptr->threads_per_core == NO_VAL16))))
				job_mc_ptr_ok = 1;
			if (job_ok && (!mc_ptr || job_mc_ptr_ok))
				continue;

			bit_clear(node_set_ptr->my_bitmap, i);
			if ((--(node_set_ptr->nodes)) == 0)
				break;
		}
	}
}

/*
 * _nodes_in_sets - Determine if required nodes are included in node_set(s)
 * IN req_bitmap - nodes specifically required by the job
 * IN node_set_ptr - sets of valid nodes
 * IN node_set_size - count of node_set entries
 * RET 0 if in set, otherwise an error code
 */
static int _nodes_in_sets(bitstr_t *req_bitmap,
			  struct node_set * node_set_ptr,
			  int node_set_size)
{
	bitstr_t *scratch_bitmap = NULL;
	int error_code = SLURM_SUCCESS, i;

	for (i=0; i<node_set_size; i++) {
		if (scratch_bitmap)
			bit_or(scratch_bitmap,
			       node_set_ptr[i].my_bitmap);
		else {
			scratch_bitmap =
			    bit_copy(node_set_ptr[i].my_bitmap);
		}
	}

	if ((scratch_bitmap == NULL)
	    || (bit_super_set(req_bitmap, scratch_bitmap) != 1))
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

	FREE_NULL_BITMAP(scratch_bitmap);
	return error_code;
}

/*
 * build_node_details - sets addresses for allocated nodes
 * IN job_ptr - pointer to a job record
 * IN new_alloc - set if new job allocation, cleared if state recovery
 */
extern void build_node_details(struct job_record *job_ptr, bool new_alloc)
{
	hostlist_t host_list = NULL;
	struct node_record *node_ptr;
	char *this_node_name;
	int node_inx = 0;

	if ((job_ptr->node_bitmap == NULL) || (job_ptr->nodes == NULL)) {
		/* No nodes allocated, we're done... */
		job_ptr->node_cnt = 0;
		xfree(job_ptr->node_addr);
		return;
	}

	/* Use hostlist here to ensure ordering of info matches that of srun */
	if ((host_list = hostlist_create(job_ptr->nodes)) == NULL)
		fatal("hostlist_create error for %s: %m", job_ptr->nodes);
	job_ptr->total_nodes = job_ptr->node_cnt = hostlist_count(host_list);
	xrealloc(job_ptr->node_addr,
		 (sizeof(slurm_addr_t) * job_ptr->node_cnt));

#ifdef HAVE_FRONT_END
	if (new_alloc) {
		/* Find available front-end node and assign it to this job */
		xfree(job_ptr->batch_host);
		job_ptr->front_end_ptr = assign_front_end(job_ptr);
		if (job_ptr->front_end_ptr) {
			job_ptr->batch_host = xstrdup(job_ptr->
						      front_end_ptr->name);
		}
	} else if (job_ptr->batch_host) {
		/* Reset pointer to this job's front-end node */
		job_ptr->front_end_ptr = assign_front_end(job_ptr);
		if (!job_ptr->front_end_ptr)
			xfree(job_ptr->batch_host);
	}
#else
	xfree(job_ptr->batch_host);
#endif

	while ((this_node_name = hostlist_shift(host_list))) {
		if ((node_ptr = find_node_record(this_node_name))) {
			memcpy(&job_ptr->node_addr[node_inx++],
			       &node_ptr->slurm_addr, sizeof(slurm_addr_t));
		} else {
			error("Invalid node %s in JobId=%u",
			      this_node_name, job_ptr->job_id);
		}
		if (!job_ptr->batch_host && !job_ptr->batch_features) {
			/*
			 * Do not select until launch_job() as node features
			 * might be changed by node_features plugin between
			 * allocation time (now) and launch.
			 */
			job_ptr->batch_host = xstrdup(this_node_name);
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);
	if (job_ptr->node_cnt != node_inx) {
		error("Node count mismatch for JobId=%u (%u,%u)",
		      job_ptr->job_id, job_ptr->node_cnt, node_inx);
	}
}

/*
 * Set "batch_host" for this job based upon it's "batch_features" and
 * "node_bitmap". Selection is performed on a best-effort basis (i.e. if no
 * node satisfies the batch_features specification then pick first node).
 * Execute this AFTER any node feature changes are made by the node_features
 * plugin.
 *
 * Return SLURM_SUCCESS or error code
 */
extern int pick_batch_host(struct job_record *job_ptr)
{
	int i, i_first;
	struct node_record *node_ptr;
	char *tmp, *tok, sep, last_sep = '&';
	node_feature_t *feature_ptr;
	ListIterator feature_iter;
	bitstr_t *feature_bitmap;

	if (job_ptr->batch_host)
		return SLURM_SUCCESS;

	if (!job_ptr->node_bitmap) {
		error("%s: Job %u lacks a node_bitmap", __func__,
		      job_ptr->job_id);
		return SLURM_ERROR;
	}

	i_first = bit_ffs(job_ptr->node_bitmap);
	if (i_first < 0) {
		error("%s: Job %u allocated no nodes", __func__,
		      job_ptr->job_id);
		return SLURM_ERROR;
	}
	if (!job_ptr->batch_features) {
		/* Run batch script on first node of job allocation */
		node_ptr = node_record_table_ptr + i_first;
		job_ptr->batch_host = xstrdup(node_ptr->name);
		return SLURM_SUCCESS;
	}

	feature_bitmap = bit_copy(job_ptr->node_bitmap);
	tmp = xstrdup(job_ptr->batch_features);
	tok = tmp;
	for (i = 0; ; i++) {
		if (tmp[i] == '&')
			sep = '&';
		else if (tmp[i] == '|')
			sep = '|';
		else if (tmp[i] == '\0')
			sep = '\0';
		else
			continue;
		tmp[i] = '\0';

		feature_iter = list_iterator_create(active_feature_list);
		while ((feature_ptr = (node_feature_t *)
				      list_next(feature_iter))) {
			if (xstrcmp(feature_ptr->name, tok))
				continue;
			if (last_sep == '&') {
				bit_and(feature_bitmap,
					feature_ptr->node_bitmap);
			} else {
				bit_or(feature_bitmap,
				       feature_ptr->node_bitmap);
			}
			break;
		}
		list_iterator_destroy(feature_iter);
		if (!feature_ptr)	/* No match */
			bit_clear_all(feature_bitmap);
		if (sep == '\0')
			break;
		tok = tmp + i + 1;
		last_sep = sep;
	}
	xfree(tmp);

	bit_and(feature_bitmap, job_ptr->node_bitmap);
	if ((i = bit_ffs(feature_bitmap)) >= 0)
		node_ptr = node_record_table_ptr + i;
	else
		node_ptr = node_record_table_ptr + i_first;
	job_ptr->batch_host = xstrdup(node_ptr->name);
	FREE_NULL_BITMAP(feature_bitmap);

	return SLURM_SUCCESS;
}

/*
 * _valid_features - Determine if the requested features are satisfied by
 *	the available nodes. This is only used for XOR operators.
 * IN job_ptr - job being scheduled
 * IN config_ptr - node's configuration record
 * IN can_reboot - if true node can use any available feature,
 *	else job can use only active features
 * RET NULL if request is not satisfied, otherwise a bitmap indicating
 *	which mutually exclusive features are satisfied. For example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns a bitmap with
 *	the third bit set. For another example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs1,fs3") returns a bitmap
 *	with the first and third bits set. The function returns a bitmap
 *	with the first bit set if requirements are satisfied without a
 *	mutually exclusive feature list.
 */
static bitstr_t *_valid_features(struct job_record *job_ptr,
				 struct config_record *config_ptr,
				 bool can_reboot)
{
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *result_node_bitmap = NULL, *paren_node_bitmap = NULL;
	bitstr_t *working_node_bitmap;
	ListIterator feat_iter;
	job_feature_t *job_feat_ptr;
	int last_op = FEATURE_OP_AND, paren_op = FEATURE_OP_AND;
	int last_paren = 0, position = 0;

	result_node_bitmap = bit_alloc(MAX_FEATURES);
	if (details_ptr->feature_list == NULL) {	/* no constraints */
		bit_set(result_node_bitmap, 0);
		return result_node_bitmap;
	}

	feat_iter = list_iterator_create(details_ptr->feature_list);
	while ((job_feat_ptr = (job_feature_t *) list_next(feat_iter))) {
		if (job_feat_ptr->paren > last_paren) {
			/* Combine features within parenthesis */
			paren_node_bitmap =
				bit_copy(job_feat_ptr->node_bitmap_avail);
			last_paren = job_feat_ptr->paren;
			paren_op = job_feat_ptr->op_code;
			while ((job_feat_ptr = (job_feature_t *)
					       list_next(feat_iter))) {
				if ((paren_op == FEATURE_OP_AND) &&
				     can_reboot) {
					bit_and(paren_node_bitmap,
						job_feat_ptr->node_bitmap_avail);
				} else if (paren_op == FEATURE_OP_AND) {
					bit_and(paren_node_bitmap,
						job_feat_ptr->node_bitmap_active);
				} else if ((paren_op == FEATURE_OP_OR) &&
					   can_reboot) {
					bit_or(paren_node_bitmap,
					       job_feat_ptr->node_bitmap_avail);
				} else if (paren_op == FEATURE_OP_OR) {
					bit_or(paren_node_bitmap,
					       job_feat_ptr->node_bitmap_active);
				} else {
					error("%s: Bad feature expression for job %u: %s",
					      __func__, job_ptr->job_id,
					      details_ptr->features);
					break;
				}
				paren_op = job_feat_ptr->op_code;
				if (job_feat_ptr->paren < last_paren) {
					last_paren = job_feat_ptr->paren;
					break;
				}
			}
			working_node_bitmap = paren_node_bitmap;
		} else {
			working_node_bitmap = job_feat_ptr->node_bitmap_avail;
		}

		if (!job_feat_ptr) {
			error("%s: Bad feature expression for job %u: %s",
			      __func__, job_ptr->job_id, details_ptr->features);
		}
		if ((job_feat_ptr->op_code == FEATURE_OP_XAND) ||
		    (job_feat_ptr->op_code == FEATURE_OP_XOR)  ||
		    ((job_feat_ptr->op_code == FEATURE_OP_END)  &&
		     ((last_op == FEATURE_OP_XAND) ||
		      (last_op == FEATURE_OP_XOR)))) {
			if (bit_super_set(config_ptr->node_bitmap,
					  working_node_bitmap)) {
				bit_set(result_node_bitmap, position);
			}
			position++;
			last_op = job_feat_ptr->op_code;
		}
		FREE_NULL_BITMAP(paren_node_bitmap);
	}
	list_iterator_destroy(feat_iter);

#if 1
{
	char tmp[64];
	bit_fmt(tmp, sizeof(tmp), result_node_bitmap);
	info("CONFIG_FEATURE:%s FEATURE_XOR_BITS:%s", config_ptr->feature, tmp);
}
#endif

	return result_node_bitmap;
}

/*
 * re_kill_job - for a given job, deallocate its nodes for a second time,
 *	basically a cleanup for failed deallocate() calls
 * IN job_ptr - pointer to terminating job (already in some COMPLETING state)
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
extern void re_kill_job(struct job_record *job_ptr)
{
	int i;
	kill_job_msg_t *kill_job;
	agent_arg_t *agent_args;
	hostlist_t kill_hostlist;
	char *host_str = NULL;
	static uint32_t last_job_id = 0;
	struct node_record *node_ptr;
	struct step_record *step_ptr;
	ListIterator step_iterator;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#endif

	xassert(job_ptr);
	xassert(job_ptr->details);

	kill_hostlist = hostlist_create(NULL);

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->hostlist = hostlist_create(NULL);
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	agent_args->retry = 0;
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	kill_job->job_id    = job_ptr->job_id;
	kill_job->step_id   = NO_VAL;
	kill_job->job_uid   = job_ptr->user_id;
	kill_job->job_state = job_ptr->job_state;
	kill_job->time      = time(NULL);
	kill_job->start_time = job_ptr->start_time;
	kill_job->select_jobinfo = select_g_select_jobinfo_copy(
				   job_ptr->select_jobinfo);
	kill_job->spank_job_env = xduparray(job_ptr->spank_job_env_size,
					    job_ptr->spank_job_env);
	kill_job->spank_job_env_size = job_ptr->spank_job_env_size;

	/* On a Cray system this will start the NHC early so it is
	 * able to gather any information it can from the apparent
	 * unkillable processes.
	 * NOTE: do not do a list_for_each here, that will hold on the list
	 * lock while processing the entire list which could
	 * potentially be needed to lock again in
	 * select_g_step_finish which could potentially call
	 * post_job_step which calls delete_step_record which locks
	 * the list to create a list_iterator on the same list and
	 * could cause deadlock :).
	 */
	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->step_id == SLURM_PENDING_STEP)
			continue;
		select_g_step_finish(step_ptr, true);
	}
	list_iterator_destroy(step_iterator);

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host &&
	    (front_end_ptr = find_front_end_record(job_ptr->batch_host))) {
		agent_args->protocol_version = front_end_ptr->protocol_version;
		if (IS_NODE_DOWN(front_end_ptr)) {
			for (i = 0, node_ptr = node_record_table_ptr;
			     i < node_record_count; i++, node_ptr++) {
				if ((job_ptr->node_bitmap_cg == NULL) ||
				    (!bit_test(job_ptr->node_bitmap_cg, i)))
					continue;
				bit_clear(job_ptr->node_bitmap_cg, i);
				job_update_tres_cnt(job_ptr, i);
				if (node_ptr->comp_job_cnt)
					(node_ptr->comp_job_cnt)--;
				if ((job_ptr->node_cnt > 0) &&
				    ((--job_ptr->node_cnt) == 0)) {
					last_node_update = time(NULL);
					cleanup_completing(job_ptr);
					batch_requeue_fini(job_ptr);
					last_node_update = time(NULL);
				}
			}
		} else if (!IS_NODE_NO_RESPOND(front_end_ptr)) {
			(void) hostlist_push_host(kill_hostlist,
						  job_ptr->batch_host);
			hostlist_push_host(agent_args->hostlist,
				      job_ptr->batch_host);
			agent_args->node_count++;
		}
	}
#else
	for (i = 0; i < node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		if ((job_ptr->node_bitmap_cg == NULL) ||
		    (bit_test(job_ptr->node_bitmap_cg, i) == 0)) {
			continue;
		} else if (IS_NODE_DOWN(node_ptr)) {
			/* Consider job already completed */
			bit_clear(job_ptr->node_bitmap_cg, i);
			job_update_tres_cnt(job_ptr, i);
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			if ((job_ptr->node_cnt > 0) &&
			    ((--job_ptr->node_cnt) == 0)) {
				cleanup_completing(job_ptr);
				batch_requeue_fini(job_ptr);
				last_node_update = time(NULL);
			}
		} else if (!IS_NODE_NO_RESPOND(node_ptr)) {
			(void)hostlist_push_host(kill_hostlist, node_ptr->name);
			if (agent_args->protocol_version >
			    node_ptr->protocol_version)
				agent_args->protocol_version =
					node_ptr->protocol_version;
			hostlist_push_host(agent_args->hostlist,
					   node_ptr->name);
			agent_args->node_count++;
		}
	}
#endif

	if (agent_args->node_count == 0) {
		slurm_free_kill_job_msg(kill_job);
		if (agent_args->hostlist)
			hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		hostlist_destroy(kill_hostlist);
		return;
	}
	hostlist_uniq(kill_hostlist);
	host_str = hostlist_ranged_string_xmalloc(kill_hostlist);
#ifdef HAVE_BG
	if (job_ptr->job_id != last_job_id) {
		info("Resending TERMINATE_JOB request JobId=%u Midplanelist=%s",
		     job_ptr->job_id, host_str);
	} else {
		debug("Resending TERMINATE_JOB request JobId=%u "
		      "Midplanelist=%s",
		      job_ptr->job_id, host_str);
	}
#else
	if (job_ptr->job_id != last_job_id) {
		info("Resending TERMINATE_JOB request JobId=%u Nodelist=%s",
		     job_ptr->job_id, host_str);
	} else {
		debug("Resending TERMINATE_JOB request JobId=%u Nodelist=%s",
		      job_ptr->job_id, host_str);
	}
#endif

	xfree(host_str);
	last_job_id = job_ptr->job_id;
	hostlist_destroy(kill_hostlist);
	agent_args->msg_args = kill_job;
	agent_queue_request(agent_args);
	return;
}
