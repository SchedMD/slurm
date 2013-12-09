/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs
 *	Note: there is a global node table (node_record_table_ptr)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if defined(__NetBSD__)
#include <sys/types.h> /* for pid_t */
#include <sys/signal.h> /* for SIGKILL */
#endif
#if defined(__FreeBSD__)
#include <signal.h>
#endif
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_priority.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"

#define MAX_FEATURES  32	/* max exclusive features "[fs1|fs2]"=2 */
#define MAX_RETRIES   10

struct node_set {		/* set of nodes with same configuration */
	uint16_t cpus_per_node;	/* NOTE: This is the minimum count,
				 * if FastSchedule==0 then individual
				 * nodes within the same configuration
				 * line (in slurm.conf) can actually
				 * have different CPU counts */
	uint32_t real_memory;
	uint32_t nodes;
	uint32_t weight;
	char     *features;
	bitstr_t *feature_bits;		/* XORed feature's position */
	bitstr_t *my_bitmap;		/* node bitmap */
};

static int  _build_node_list(struct job_record *job_ptr,
			     struct node_set **node_set_pptr,
			     int *node_set_size);
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *detail_ptr);
static int _match_feature(char *seek, struct node_set *node_set_ptr);
static int _nodes_in_sets(bitstr_t *req_bitmap,
			  struct node_set * node_set_ptr,
			  int node_set_size);
static int _pick_best_nodes(struct node_set *node_set_ptr,
			    int node_set_size, bitstr_t ** select_bitmap,
			    struct job_record *job_ptr,
			    struct part_record *part_ptr,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, bool test_only,
			    List preemptee_candidates,
			    List *preemptee_job_list, bool has_xand,
			    bitstr_t *exc_node_bitmap);
static bool _valid_feature_counts(struct job_details *detail_ptr,
				  bitstr_t *node_bitmap, bool *has_xor);
static bitstr_t *_valid_features(struct job_details *detail_ptr,
				 struct config_record *config_ptr);

static int _fill_in_gres_fields(struct job_record *job_ptr);

static void _launch_prolog(struct job_record *job_ptr);

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
 *	allocations.
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
		if (!strcmp(select_type, "select/cray"))
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
 * IN suspended - true if job was already suspended (node's job_run_cnt
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

	if (select_serial == -1) {
		if (strcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}

	license_job_return(job_ptr);
	acct_policy_job_fini(job_ptr);
	if (slurm_sched_g_freealloc(job_ptr) != SLURM_SUCCESS)
		error("slurm_sched_freealloc(%u): %m", job_ptr->job_id);
	if (select_g_job_fini(job_ptr) != SLURM_SUCCESS)
		error("select_g_job_fini(%u): %m", job_ptr->job_id);
	(void) epilog_slurmctld(job_ptr);

	agent_args = xmalloc(sizeof(agent_arg_t));
	if (timeout)
		agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	else if (preempted)
		agent_args->msg_type = REQUEST_KILL_PREEMPTED;
	else
		agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->retry = 0;	/* re_kill_job() resends as needed */
	agent_args->hostlist = hostlist_create("");
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
			if (front_end_ptr->job_cnt_run)
				front_end_ptr->job_cnt_run--;
			else {
				error("front_end %s job_cnt_run underflow",
				      front_end_ptr->name);
			}
			if (front_end_ptr->job_cnt_run == 0) {
				uint16_t state_flags;
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

		hostlist_push(agent_args->hostlist, job_ptr->batch_host);
		agent_args->node_count++;
	}
#else
	if (!job_ptr->node_bitmap_cg)
		build_cg_bitmap(job_ptr);
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!bit_test(job_ptr->node_bitmap_cg, i))
			continue;
		if (IS_NODE_DOWN(node_ptr)) {
			/* Issue the KILL RPC, but don't verify response */
			down_node_cnt++;
			if (job_ptr->node_bitmap_cg == NULL) {
				error("deallocate_nodes: node_bitmap_cg is "
				      "not set");
				build_cg_bitmap(job_ptr);
			}
			bit_clear(job_ptr->node_bitmap_cg, i);
			job_update_cpu_cnt(job_ptr, i);
			/* node_cnt indicates how many nodes we are waiting
			 * to get epilog complete messages from, so do not
			 * count down nodes. NOTE: The job's node_cnt will not
			 * match the number of entries in the node string
			 * during its completion. */
			job_ptr->node_cnt--;
		}
		make_node_comp(node_ptr, job_ptr, suspended);

		hostlist_push(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}
#endif

	if ((agent_args->node_count - down_node_cnt) == 0) {
		job_ptr->job_state &= (~JOB_COMPLETING);
		delete_step_records(job_ptr);
		slurm_sched_g_schedule();
	}

	if (agent_args->node_count == 0) {
		if ((job_ptr->details->expanding_jobid == 0) &&
		    (select_serial == 0)) {
			error("Job %u allocated no nodes to be killed on",
			      job_ptr->job_id);
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
 * _match_feature - determine if the desired feature is one of those available
 * IN seek - desired feature
 * IN node_set_ptr - Pointer to node_set being searched
 * RET 1 if found, 0 otherwise
 */
static int _match_feature(char *seek, struct node_set *node_set_ptr)
{
	struct features_record *feat_ptr;

	if (seek == NULL)
		return 1;	/* nothing to look for */

	feat_ptr = list_find_first(feature_list, list_find_feature,
				   (void *) seek);
	if (feat_ptr == NULL)
		return 0;	/* no such feature */

	if (bit_super_set(node_set_ptr->my_bitmap, feat_ptr->node_bitmap))
		return 1;	/* nodes have this feature */
	return 0;
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
 *	no		default/exclus	whole	whole	whole	share/O
 *	no		share=yes	whole	whole	share/O	share/O
 *	yes		default		whole	share	share/O	share/O
 *	yes		exclusive	whole	whole	whole	share/O
 *	yes		share=yes	whole	share	share/O	share/O
 *
 * whole   = whole node is allocated exclusively to the user
 * share   = nodes may be shared but the resources are not overcommitted
 * share/O = nodes are shared and the resources can be overcommitted
 *
 * part->max_share:
 *	&SHARED_FORCE 	= FORCE
 *	0		= EXCLUSIVE
 *	1		= NO
 *	> 1		= YES
 *
 * job_ptr->details->shared:
 *	(uint16_t)NO_VAL	= default
 *	0			= exclusive
 *	1			= share=yes
 *
 * Return values:
 *	0 = no sharing
 *	1 = user requested sharing
 *	2 = sharing enforced (either by partition or cons_res)
 * (cons_res plugin needs to distinguish between "enforced" and
 *  "requested" sharing)
 */
static int
_resolve_shared_status(uint16_t user_flag, uint16_t part_max_share,
		       int cons_res_flag)
{
	/* no sharing if part=EXCLUSIVE */
	if (part_max_share == 0)
		return 0;

	/* sharing if part=FORCE with count > 1 */
	if ((part_max_share & SHARED_FORCE) &&
	    ((part_max_share & (~SHARED_FORCE)) > 1))
		return 2;

	if (cons_res_flag) {
		/* sharing unless user requested exclusive */
		if (user_flag == 0)
			return 0;
		if (user_flag == 1)
			return 1;
		return 2;
	} else {
		/* no sharing if part=NO */
		if (part_max_share == 1)
			return 0;
		/* share if the user requested it */
		if (user_flag == 1)
			return 1;
	}
	return 0;
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
		  bitstr_t ** select_bitmap, struct job_record *job_ptr,
		  struct part_record *part_ptr,
		  uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes,
		  bool test_only, List *preemptee_job_list)
{
	uint32_t saved_min_nodes, saved_job_min_nodes;
	bitstr_t *saved_req_node_bitmap = NULL;
	uint32_t saved_min_cpus, saved_req_nodes;
	int rc, tmp_node_set_size;
	struct node_set *tmp_node_set_ptr;
	int error_code = SLURM_SUCCESS, i;
	bitstr_t *feature_bitmap, *accumulate_bitmap = NULL;
	bitstr_t *save_avail_node_bitmap = NULL, *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	List preemptee_candidates = NULL;
	bool has_xand = false;

	/* Mark nodes reserved for other jobs as off limit for this job.
	 * If the job has a reservation, we've already limited the contents
	 * of select_bitmap to those nodes */
	if (job_ptr->resv_name == NULL) {
		time_t start_res = time(NULL);
		rc = job_test_resv(job_ptr, &start_res, false, &resv_bitmap,
				   &exc_core_bitmap);
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
			bit_and(resv_bitmap, avail_node_bitmap);
			save_avail_node_bitmap = avail_node_bitmap;
			avail_node_bitmap = resv_bitmap;
			resv_bitmap = NULL;
		} else {
			FREE_NULL_BITMAP(resv_bitmap);
		}
	} else {
		time_t start_res = time(NULL);
		rc = job_test_resv(job_ptr, &start_res, false, &resv_bitmap,
				   &exc_core_bitmap);
		FREE_NULL_BITMAP(resv_bitmap);
		/* We do not care about return value.
		 * We are just interested in exc_core_bitmap creation */
	}

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
	/* Don't mess with max_cpus here since it is only set (as of
	 * 2.2 to be a limit and not user configurable. */
	job_ptr->details->min_cpus = 1;
	tmp_node_set_ptr = xmalloc(sizeof(struct node_set) * node_set_size);

	/* Accumulate nodes with required feature counts.
	 * Ignored if job_ptr->details->req_node_layout is set (by wiki2).
	 * Selected nodes become part of job's required node list. */
	preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
	if (job_ptr->details->feature_list &&
	    (job_ptr->details->req_node_layout == NULL)) {
		ListIterator feat_iter;
		struct feature_record *feat_ptr;
		feat_iter = list_iterator_create(
				job_ptr->details->feature_list);
		while ((feat_ptr = (struct feature_record *)
				list_next(feat_iter))) {
			if (feat_ptr->count == 0)
				continue;
			tmp_node_set_size = 0;
			/* _pick_best_nodes() is destructive of the node_set
			 * data structure, so we need to make a copy and then
			 * purge it */
			for (i=0; i<node_set_size; i++) {
				if (!_match_feature(feat_ptr->name,
						    node_set_ptr+i))
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
				tmp_node_set_size++;
			}
			feature_bitmap = NULL;
			min_nodes = feat_ptr->count;
			req_nodes = feat_ptr->count;
			job_ptr->details->min_nodes = feat_ptr->count;
			job_ptr->details->min_cpus = feat_ptr->count;
			if (*preemptee_job_list) {
				list_destroy(*preemptee_job_list);
				*preemptee_job_list = NULL;
			}
			error_code = _pick_best_nodes(tmp_node_set_ptr,
					tmp_node_set_size, &feature_bitmap,
					job_ptr, part_ptr, min_nodes,
					max_nodes, req_nodes, test_only,
					preemptee_candidates,
					preemptee_job_list, false,
					exc_core_bitmap);
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
			for (i=0; i<tmp_node_set_size; i++) {
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
					/* Don't make it required since we
					 * check value on each call to
					 * _pick_best_nodes() */
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
	info("job %u requires %d:%d:%d nodes %s err:%u",
	     job_ptr->job_id, min_nodes, req_nodes, max_nodes,
	     tmp_str, error_code);
	xfree(tmp_str);
}
#endif
	xfree(tmp_node_set_ptr);
	if (error_code == SLURM_SUCCESS) {
		if (*preemptee_job_list) {
			list_destroy(*preemptee_job_list);
			*preemptee_job_list = NULL;
		}
		error_code = _pick_best_nodes(node_set_ptr, node_set_size,
				select_bitmap, job_ptr, part_ptr, min_nodes,
				max_nodes, req_nodes, test_only,
				preemptee_candidates, preemptee_job_list,
				has_xand, exc_core_bitmap);
	}
#if 0
{
	char *tmp_str = bitmap2node_name(*select_bitmap);
	info("job %u allocated nodes:%s err:%u",
		job_ptr->job_id, tmp_str, error_code);
	xfree(tmp_str);
}
#endif
	if (preemptee_candidates)
		list_destroy(preemptee_candidates);

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
 * RET SLURM_SUCCESS on success,
 *	ESLURM_NODES_BUSY if request can not be satisfied now,
 *	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE if request can never
 *	be satisfied ,
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE if the job can not be
 *	initiated until the parition's configuration changes or
 *	ESLURM_NODE_NOT_AVAIL if required nodes are DOWN or DRAINED
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
 *         ever be satified.
 */
static int
_pick_best_nodes(struct node_set *node_set_ptr, int node_set_size,
		 bitstr_t ** select_bitmap, struct job_record *job_ptr,
		 struct part_record *part_ptr,
		 uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes,
		 bool test_only, List preemptee_candidates,
		 List *preemptee_job_list, bool has_xand,
		 bitstr_t *exc_core_bitmap)
{
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

	if (test_only)
		select_mode = SELECT_MODE_TEST_ONLY;
	else
		select_mode = SELECT_MODE_RUN_NOW;

	if ((job_ptr->details->min_nodes == 0) &&
	    (job_ptr->details->max_nodes == 0)) {
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
		info("_pick_best_nodes: empty node set for selection");
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

	shared = _resolve_shared_status(job_ptr->details->shared,
					part_ptr->max_share, cr_enabled);
	job_ptr->details->shared = shared;
	if (cr_enabled)
		job_ptr->cr_enabled = cr_enabled; /* CR enabled for this job */

	/* If job preemption is enabled, then do NOT limit the set of available
	 * nodes by their current 'sharable' or 'idle' setting */
	preempt_flag = slurm_preemption_enabled();

	if (job_ptr->details->req_node_bitmap) {  /* specific nodes required */
		/* We have already confirmed that all of these nodes have a
		 * usable configuration and are in the proper partition.
		 * Check that these nodes can be used by this job. */
		if (min_nodes != 0) {
			total_nodes = bit_set_count(
				job_ptr->details->req_node_bitmap);
		}
		if (total_nodes > max_nodes) {	/* exceeds node limit */
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}

		/* check the availability of these nodes */
		/* Should we check memory availability on these nodes? */
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_node_bitmap)) {
			return ESLURM_NODE_NOT_AVAIL;
		}

		if (!preempt_flag) {
			if (shared) {
				if (!bit_super_set(job_ptr->details->
						   req_node_bitmap,
						   share_node_bitmap)) {
					return ESLURM_NODES_BUSY;
				}
#ifndef HAVE_BG
				if (bit_overlap(job_ptr->details->
						req_node_bitmap,
						cg_node_bitmap)) {
					return ESLURM_NODES_BUSY;
				}
#endif
			} else {
				if (!bit_super_set(job_ptr->details->
						   req_node_bitmap,
						   idle_node_bitmap)) {
					return ESLURM_NODES_BUSY;
				}
				/* Note: IDLE nodes are not COMPLETING */
			}
#ifndef HAVE_BG
		} else if (bit_overlap(job_ptr->details->req_node_bitmap,
				       cg_node_bitmap)) {
			return ESLURM_NODES_BUSY;
#endif
		}

		/* still must go through select_g_job_test() to
		 * determine validity of request and/or perform
		 * set-up before job launch */
		total_nodes = 0;	/* reinitialize */
	}

	/* identify the min and max feature values for exclusive OR */
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

	debug3("_pick_best_nodes: job %u idle_nodes %u share_nodes %u",
		job_ptr->job_id, bit_set_count(idle_node_bitmap),
		bit_set_count(share_node_bitmap));
	/* Accumulate resources for this job based upon its required
	 * features (possibly with node counts). */
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
					bit_not(cg_node_bitmap);
					bit_and(node_set_ptr[i].my_bitmap,
						cg_node_bitmap);
					bit_not(cg_node_bitmap);
#endif
				} else {
					bit_and(node_set_ptr[i].my_bitmap,
						idle_node_bitmap);
					/* IDLE nodes are not COMPLETING */
				}
			} else {
#ifndef HAVE_BG
				bit_not(cg_node_bitmap);
				bit_and(node_set_ptr[i].my_bitmap,
					cg_node_bitmap);
				bit_not(cg_node_bitmap);
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
			avail_nodes = bit_set_count(avail_bitmap);
			tried_sched = false;	/* need to test these nodes */
			if ((shared || preempt_flag)	&&
			    ((i+1) < node_set_size)	&&
			    (node_set_ptr[i].weight ==
			     node_set_ptr[i+1].weight)) {
				/* Keep accumulating so we can pick the
				 * most lightly loaded nodes */
				continue;
			}

			if ((avail_nodes  < min_nodes)	||
			    ((avail_nodes >= min_nodes)	&&
			     (avail_nodes < req_nodes)	&&
			     ((i+1) < node_set_size)))
				continue;	/* Keep accumulating nodes */

			/* NOTE: select_g_job_test() is destructive of
			 * avail_bitmap, so save a backup copy */
			backup_bitmap = bit_copy(avail_bitmap);
			if (*preemptee_job_list) {
				list_destroy(*preemptee_job_list);
				*preemptee_job_list = NULL;
			}
			if (job_ptr->details->req_node_bitmap == NULL)
				bit_and(avail_bitmap, avail_node_bitmap);
			/* Only preempt jobs when all possible nodes are being
			 * considered for use, otherwise we would preempt jobs
			 * to use the lowest weight nodes. */
			if ((i+1) < node_set_size)
				preemptee_cand = NULL;
			else
				preemptee_cand = preemptee_candidates;
			pick_code = select_g_job_test(job_ptr,
						      avail_bitmap,
						      min_nodes,
						      max_nodes,
						      req_nodes,
						      select_mode,
						      preemptee_cand,
						      preemptee_job_list,
						      exc_core_bitmap);
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
			if (*preemptee_job_list) {
				list_destroy(*preemptee_job_list);
				*preemptee_job_list = NULL;
			}
			pick_code = select_g_job_test(job_ptr, avail_bitmap,
						      min_nodes, max_nodes,
						      req_nodes,
						      select_mode,
						      preemptee_candidates,
						      preemptee_job_list,
						      exc_core_bitmap);
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
				pick_code = select_g_job_test(job_ptr,
						avail_bitmap,
						min_nodes,
						max_nodes,
						req_nodes,
						SELECT_MODE_TEST_ONLY,
						preemptee_candidates, NULL,
						exc_core_bitmap);
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
				pick_code = select_g_job_test(job_ptr,
						total_bitmap,
						min_nodes,
						max_nodes,
						req_nodes,
						SELECT_MODE_TEST_ONLY,
						preemptee_candidates, NULL,
						exc_core_bitmap);
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

	/* The job is not able to start right now, return a
	 * value indicating when the job can start */
	if (!runable_ever) {
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info("_pick_best_nodes: job %u never runnable",
		     job_ptr->job_id);
	} else if (!runable_avail && !nodes_busy) {
		error_code = ESLURM_NODE_NOT_AVAIL;
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
			  int *error_code)
{
	ListIterator iter;
	struct job_record *job_ptr;
	uint16_t mode;
	int job_cnt = 0, rc = SLURM_SUCCESS;
	checkpoint_msg_t ckpt_msg;

	iter = list_iterator_create(preemptee_job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		mode = slurm_job_preempt_mode(job_ptr);
		if (mode == PREEMPT_MODE_CANCEL) {
			job_cnt++;
			if (!kill_pending)
				continue;
			if (slurm_job_check_grace(job_ptr) == SLURM_SUCCESS)
				continue;
			rc = job_signal(job_ptr->job_id, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been killed",
				     job_ptr->job_id);
			}
		} else if (mode == PREEMPT_MODE_CHECKPOINT) {
			job_cnt++;
			if (!kill_pending)
				continue;
			memset(&ckpt_msg, 0, sizeof(checkpoint_msg_t));
			ckpt_msg.op	   = CHECK_REQUEUE;
			ckpt_msg.job_id    = job_ptr->job_id;
			rc = job_checkpoint(&ckpt_msg, 0, -1,
					    (uint16_t) NO_VAL);
			if (rc == ESLURM_NOT_SUPPORTED) {
				memset(&ckpt_msg, 0, sizeof(checkpoint_msg_t));
				ckpt_msg.op	   = CHECK_VACATE;
				ckpt_msg.job_id    = job_ptr->job_id;
				rc = job_checkpoint(&ckpt_msg, 0, -1,
						    (uint16_t) NO_VAL);
			}
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been checkpointed",
				     job_ptr->job_id);
			}
		} else if (mode == PREEMPT_MODE_REQUEUE) {
			job_cnt++;
			if (!kill_pending)
				continue;
			rc = job_requeue(0, job_ptr->job_id, -1,
					 (uint16_t)NO_VAL, true);
			if (rc == SLURM_SUCCESS) {
				info("preempted job %u has been requeued",
				     job_ptr->job_id);
			}
		} else if ((mode == PREEMPT_MODE_SUSPEND) &&
			   (slurm_get_preempt_mode() & PREEMPT_MODE_GANG)) {
			debug("preempted job %u suspended by gang scheduler",
			      job_ptr->job_id);
		} else {
			error("Invalid preempt_mode: %u", mode);
			rc = SLURM_ERROR;
		}

		if (rc != SLURM_SUCCESS) {
			if ((mode != PREEMPT_MODE_CANCEL)
			    && (slurm_job_check_grace(job_ptr)
				== SLURM_SUCCESS))
				continue;

			rc = job_signal(job_ptr->job_id, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS)
				info("preempted job %u had to be killed",
				     job_ptr->job_id);
			else {
				info("preempted job %u kill failure %s",
				     job_ptr->job_id, slurm_strerror(rc));
			}
		}
	}
	list_iterator_destroy(iter);

	if (job_cnt > 0)
		*error_code = ESLURM_NODES_BUSY;
}

/*
 * select_nodes - select and allocate nodes to a specific job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they
 *	could be allocated now
 * IN select_node_bitmap - bitmap of nodes to be used for the
 *	job's resource allocation (not returned if NULL), caller
 *	must free
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
			bitstr_t **select_node_bitmap)
{
	int error_code = SLURM_SUCCESS, i, node_set_size = 0;
	bitstr_t *select_bitmap = NULL;
	struct node_set *node_set_ptr = NULL;
	struct part_record *part_ptr = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	time_t now = time(NULL);
	bool configuring = false;
	List preemptee_job_list = NULL;
	slurmdb_qos_rec_t *qos_ptr = NULL;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!acct_policy_job_runnable(job_ptr))
		return ESLURM_ACCOUNTING_POLICY;

	part_ptr = job_ptr->part_ptr;
	qos_ptr = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;

	/* identify partition */
	if (part_ptr == NULL) {
		part_ptr = find_part_record(job_ptr->partition);
		xassert(part_ptr);
		job_ptr->part_ptr = part_ptr;
		error("partition pointer reset for job %u, part %s",
		      job_ptr->job_id, job_ptr->partition);
	}

	if (job_ptr->priority == 0) {	/* user/admin hold */
		if ((job_ptr->state_reason != WAIT_HELD) &&
		    (job_ptr->state_reason != WAIT_HELD_USER)) {
			job_ptr->state_reason = WAIT_HELD;
		}
		return ESLURM_JOB_HELD;
	}

	/* build sets of usable nodes based upon their configuration */
	error_code = _build_node_list(job_ptr, &node_set_ptr, &node_set_size);
	if (error_code)
		return error_code;

	/* insure that selected nodes are in these node sets */
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

	/* On BlueGene systems don't adjust the min/max node limits
	   here.  We are working on midplane values. */
	if (qos_ptr && (qos_ptr->flags & QOS_FLAG_PART_MIN_NODE))
		min_nodes = job_ptr->details->min_nodes;
	else
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);
	if (job_ptr->details->max_nodes == 0)
		max_nodes = part_ptr->max_nodes;
	else if (qos_ptr && (qos_ptr->flags & QOS_FLAG_PART_MAX_NODE))
		max_nodes = job_ptr->details->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes,
				part_ptr->max_nodes);

	if (job_ptr->details->req_node_bitmap && job_ptr->details->max_nodes) {
		i = bit_set_count(job_ptr->details->req_node_bitmap);
		if (i > job_ptr->details->max_nodes) {
			info("Job %u required node list has more node than "
			     "the job can use (%d > %u)",
			     job_ptr->job_id, i, job_ptr->details->max_nodes);
			error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			goto cleanup;
		}
	}

	max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
	if (!job_ptr->limit_set_max_nodes && job_ptr->details->max_nodes)
		req_nodes = max_nodes;
	else
		req_nodes = min_nodes;
	/* info("nodes:%u:%u:%u", min_nodes, req_nodes, max_nodes); */

	if (max_nodes < min_nodes) {
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	} else {
		/* Select resources for the job here */
		error_code = _get_req_features(node_set_ptr, node_set_size,
					       &select_bitmap, job_ptr,
					       part_ptr, min_nodes, max_nodes,
					       req_nodes, test_only,
					       &preemptee_job_list);
	}
	/* set up the cpu_cnt here so we can decrement it as nodes
	 * free up. total_cpus is set within _get_req_features */
	job_ptr->cpu_cnt = job_ptr->total_cpus;

	if (!test_only && preemptee_job_list && (error_code == SLURM_SUCCESS)){
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
		_preempt_jobs(preemptee_job_list, kill_pending, &error_code);
		if ((error_code == ESLURM_NODES_BUSY) &&
		    (detail_ptr->preempt_start_time == 0)) {
  			detail_ptr->preempt_start_time = now;
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
			debug3("JobId=%u required nodes not avail",
			       job_ptr->job_id);
			job_ptr->state_reason = WAIT_NODE_NOT_AVAIL;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		} else if (error_code == ESLURM_RESERVATION_NOT_USABLE) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
		} else if ((job_ptr->state_reason == WAIT_BLOCK_MAX_ERR) ||
			   (job_ptr->state_reason == WAIT_BLOCK_D_ACTION)) {
			/* state_reason was already setup */
		} else {
			job_ptr->state_reason = WAIT_RESOURCES;
			xfree(job_ptr->state_desc);
			if (error_code == ESLURM_NODES_BUSY)
				slurm_sched_g_job_is_pending();
		}
		goto cleanup;
	}
	if (test_only) {	/* set if job not highest priority */
		slurm_sched_g_job_is_pending();
		error_code = SLURM_SUCCESS;
		goto cleanup;
	}

	/* This job may be getting requeued, clear vestigial
	 * state information before over-writing and leaking
	 * memory. */
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	xfree(job_ptr->nodes);

	job_ptr->node_bitmap = select_bitmap;

	/* we need to have these times set to know when the endtime
	 * is for the job when we place it
	 */
	job_ptr->start_time = job_ptr->time_last_active = now;
	if ((job_ptr->time_limit == NO_VAL) ||
	    ((job_ptr->time_limit > part_ptr->max_time) &&
	     (!qos_ptr || (qos_ptr && !(qos_ptr->flags
					& QOS_FLAG_PART_TIME_LIMIT))))) {
		if (part_ptr->default_time != NO_VAL)
			job_ptr->time_limit = part_ptr->default_time;
		else
			job_ptr->time_limit = part_ptr->max_time;
	}

	if (job_ptr->time_limit == INFINITE)
		job_ptr->end_time = job_ptr->start_time +
				    (365 * 24 * 60 * 60); /* secs in year */
	else
		job_ptr->end_time = job_ptr->start_time +
			(job_ptr->time_limit * 60);   /* secs */

	if (select_g_job_begin(job_ptr) != SLURM_SUCCESS) {
		/* Leave job queued, something is hosed */
		error("select_g_job_begin(%u): %m", job_ptr->job_id);
		error_code = ESLURM_NODES_BUSY;
		job_ptr->start_time = 0;
		job_ptr->time_last_active = 0;
		job_ptr->end_time = 0;
		job_ptr->node_bitmap = NULL;
		goto cleanup;
	}

	/* assign the nodes and stage_in the job */
	job_ptr->state_reason = WAIT_NO_REASON;
	xfree(job_ptr->state_desc);

	if (job_ptr->job_resrcs && job_ptr->job_resrcs->nodes)
		job_ptr->nodes = xstrdup(job_ptr->job_resrcs->nodes);
	else {
		error("Select plugin failed to set job resources, nodes");
		job_ptr->nodes = bitmap2node_name(select_bitmap);
	}
	select_bitmap = NULL;	/* nothing left to free */
	allocate_nodes(job_ptr);
	build_node_details(job_ptr, true);

	/* This could be set in the select plugin so we want to keep
	   the flag. */
	configuring = IS_JOB_CONFIGURING(job_ptr);

	job_ptr->job_state = JOB_RUNNING;
	if (nonstop_ops.job_begin)
		(nonstop_ops.job_begin)(job_ptr);

	if (configuring
	    || bit_overlap(job_ptr->node_bitmap, power_node_bitmap))
		job_ptr->job_state |= JOB_CONFIGURING;
	if (select_g_select_nodeinfo_set(job_ptr) != SLURM_SUCCESS) {
		error("select_g_select_nodeinfo_set(%u): %m", job_ptr->job_id);
		/* not critical ... by now */
	}
	if (job_ptr->mail_type & MAIL_JOB_BEGIN)
		mail_job_info(job_ptr, MAIL_JOB_BEGIN);

	slurmctld_diag_stats.jobs_started++;
	acct_policy_job_begin(job_ptr);

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
	if (!with_slurmdbd || (with_slurmdbd && job_ptr->db_index))
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	prolog_slurmctld(job_ptr);
	slurm_sched_g_newalloc(job_ptr);

	/* Request asynchronous launch of a prolog for a
	 * non batch job. For a batch job the prolog will be
	 * started synchroniously by slurmd. */
	if (job_ptr->batch_flag == 0 &&
		(slurmctld_conf.prolog_flags & PROLOG_FLAG_ALLOC)) {
		_launch_prolog(job_ptr);
	}

      cleanup:
	if (preemptee_job_list)
		list_destroy(preemptee_job_list);
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

	return error_code;
}

/*
 * Launch prolog via RPC to slurmd. This is useful when we need to run
 * prolog at allocation stage. Then we ask slurmd to launch the prolog
 * asynchroniously and wait on REQUEST_COMPLETE_PROLOG message from slurmd.
 */
static void _launch_prolog(struct job_record *job_ptr)
{
	prolog_launch_msg_t *prolog_msg_ptr;
	agent_arg_t *agent_arg_ptr;
	prolog_msg_ptr = (prolog_launch_msg_t *)
				xmalloc(sizeof(prolog_launch_msg_t));

	xassert(job_ptr);
	xassert(job_ptr->batch_host);
	xassert(prolog_msg_ptr);

	/* Locks: Write job */
	job_ptr->state_reason = WAIT_PROLOG;

	prolog_msg_ptr->job_id = job_ptr->job_id;
	prolog_msg_ptr->uid = job_ptr->user_id;
	prolog_msg_ptr->gid = job_ptr->group_id;
	prolog_msg_ptr->alias_list = xstrdup(job_ptr->alias_list);
	prolog_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	prolog_msg_ptr->std_err = xstrdup(job_ptr->details->std_err);
	prolog_msg_ptr->std_out = xstrdup(job_ptr->details->std_out);
	prolog_msg_ptr->work_dir = xstrdup(job_ptr->details->work_dir);
	prolog_msg_ptr->spank_job_env_size = job_ptr->spank_job_env_size;
	prolog_msg_ptr->spank_job_env = xduparray(job_ptr->spank_job_env_size,
														job_ptr->spank_job_env);

	agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->retry = 0;
	agent_arg_ptr->node_count = 1;
  #ifdef HAVE_FRONT_END
      xassert(job_ptr->front_end_ptr);
      xassert(job_ptr->front_end_ptr->name);
      agent_arg_ptr->hostlist = hostlist_create(job_ptr->front_end_ptr->name);
  #else
      agent_arg_ptr->hostlist = hostlist_create(job_ptr->batch_host);
  #endif
	agent_arg_ptr->msg_type = REQUEST_LAUNCH_PROLOG;
	agent_arg_ptr->msg_args = (void *) prolog_msg_ptr;

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
	uint32_t ngres_req;
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
			if (ngres_req == NO_VAL)
				ngres_req = 0;

			/* Append value to the gres string. */
			snprintf(buf, sizeof(buf), "%s%s:%u",
				 prefix, subtok,
				 ngres_req * job_ptr->node_cnt);

			xstrcat(job_ptr->gres_req, buf);

			if (prefix[0] == '\0')
				prefix = ",";
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_GRES) {
				debug("(%s:%d) job id:%u -- ngres_req:"
				      "%u, gres_req substring = (%s)",
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
	struct features_record *feature_ptr;

	if (key == NULL)
		return 1;

	feature_ptr = (struct features_record *) feature_entry;
	if (strcmp(feature_ptr->name, (char *) key) == 0)
		return 1;
	return 0;
}

/*
 * _valid_feature_counts - validate a job's features can be satisfied
 *	by the selected nodes (NOTE: does not process XOR or XAND operators)
 * IN detail_ptr - job details
 * IN/OUT node_bitmap - nodes available for use, clear if unusable
 * RET true if valid, false otherwise
 */
static bool _valid_feature_counts(struct job_details *detail_ptr,
				  bitstr_t *node_bitmap, bool *has_xor)
{
	ListIterator job_feat_iter;
	struct feature_record *job_feat_ptr;
	struct features_record *feat_ptr;
	int have_count = false, last_op = FEATURE_OP_AND;
	bitstr_t *feature_bitmap, *tmp_bitmap;
	bool rc = true;

	xassert(detail_ptr);
	xassert(node_bitmap);
	xassert(has_xor);

	*has_xor = false;
	if (detail_ptr->feature_list == NULL)	/* no constraints */
		return rc;

	feature_bitmap = bit_copy(node_bitmap);
	job_feat_iter = list_iterator_create(detail_ptr->feature_list);
	while ((job_feat_ptr = (struct feature_record *)
			list_next(job_feat_iter))) {
		feat_ptr = list_find_first(feature_list, list_find_feature,
					   (void *) job_feat_ptr->name);
		if (feat_ptr) {
			if (last_op == FEATURE_OP_AND)
				bit_and(feature_bitmap, feat_ptr->node_bitmap);
			else if (last_op == FEATURE_OP_OR)
				bit_or(feature_bitmap, feat_ptr->node_bitmap);
			else {	/* FEATURE_OP_XOR or FEATURE_OP_XAND */
				*has_xor = true;
				bit_or(feature_bitmap, feat_ptr->node_bitmap);
			}
		} else {	/* feature not found */
			if (last_op == FEATURE_OP_AND) {
				bit_nclear(feature_bitmap, 0,
					   (node_record_count - 1));
			}
		}
		last_op = job_feat_ptr->op_code;
		if (job_feat_ptr->count)
			have_count = true;
	}
	list_iterator_destroy(job_feat_iter);

	if (have_count) {
		job_feat_iter = list_iterator_create(detail_ptr->
						     feature_list);
		while ((job_feat_ptr = (struct feature_record *)
				list_next(job_feat_iter))) {
			if (job_feat_ptr->count == 0)
				continue;
			feat_ptr = list_find_first(feature_list,
						   list_find_feature,
						   (void *)job_feat_ptr->name);
			if (!feat_ptr) {
				rc = false;
				break;
			}
			tmp_bitmap = bit_copy(feature_bitmap);
			bit_and(tmp_bitmap, feat_ptr->node_bitmap);
			if (bit_set_count(tmp_bitmap) < job_feat_ptr->count)
				rc = false;
			FREE_NULL_BITMAP(tmp_bitmap);
			if (!rc)
				break;
		}
		list_iterator_destroy(job_feat_iter);
		FREE_NULL_BITMAP(feature_bitmap);
	} else {
		bit_and(node_bitmap, feature_bitmap);
		FREE_NULL_BITMAP(feature_bitmap);
	}

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
			       bitstr_t *avail_bitmap)
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
	for (i=0; i< node_record_count; i++) {
		if (!bit_test(avail_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		config_ptr = node_ptr->config_ptr;
		if (slurmctld_conf.fast_schedule) {
			if ((detail_ptr->pn_min_cpus  > config_ptr->cpus)   ||
			    ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) >
			      config_ptr->real_memory) 			     ||
			    ((detail_ptr->pn_min_memory & (MEM_PER_CPU)) &&
			     ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) *
			      detail_ptr->pn_min_cpus) >
			     config_ptr->real_memory) 			     ||
			    (detail_ptr->pn_min_tmp_disk >
			     config_ptr->tmp_disk)) {
				bit_clear(avail_bitmap, i);
				continue;
			}
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node > config_ptr->sockets) &&
			      (mc_ptr->sockets_per_node != (uint16_t) NO_VAL)) ||
			     ((mc_ptr->cores_per_socket > config_ptr->cores)   &&
			      (mc_ptr->cores_per_socket != (uint16_t) NO_VAL)) ||
			     ((mc_ptr->threads_per_core > config_ptr->threads) &&
			      (mc_ptr->threads_per_core != (uint16_t) NO_VAL)))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		} else {
			if ((detail_ptr->pn_min_cpus > node_ptr->cpus)     ||
			    ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) >
			      node_ptr->real_memory)                        ||
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
			      (mc_ptr->sockets_per_node != (uint16_t) NO_VAL)) ||
			     ((mc_ptr->cores_per_socket > node_ptr->cores)     &&
			      (mc_ptr->cores_per_socket != (uint16_t) NO_VAL)) ||
			     ((mc_ptr->threads_per_core > node_ptr->threads)   &&
			      (mc_ptr->threads_per_core != (uint16_t) NO_VAL)))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		}
	}

	if (!_valid_feature_counts(detail_ptr, avail_bitmap, &has_xor))
		return EINVAL;

	return SLURM_SUCCESS;
}

/*
 * _build_node_list - identify which nodes could be allocated to a job
 *	based upon node features, memory, processors, etc. Note that a
 *	bitmap is set to indicate which of the job's features that the
 *	nodes satisfy.
 * IN job_ptr - pointer to node to be scheduled
 * OUT node_set_pptr - list of node sets which could be used for the job
 * OUT node_set_size - number of node_set entries
 * RET error code
 */
static int _build_node_list(struct job_record *job_ptr,
			    struct node_set **node_set_pptr,
			    int *node_set_size)
{
	int adj_cpus, i, node_set_inx, power_cnt, rc;
	struct node_set *node_set_ptr;
	struct config_record *config_ptr;
	struct part_record *part_ptr = job_ptr->part_ptr;
	ListIterator config_iterator;
	int check_node_config, config_filter = 0;
	struct job_details *detail_ptr = job_ptr->details;
	bitstr_t *power_up_bitmap = NULL, *usable_node_mask = NULL;
	multi_core_data_t *mc_ptr = detail_ptr->mc_ptr;
	bitstr_t *tmp_feature;
	uint32_t max_weight = 0;
	bool has_xor = false;

	if (job_ptr->resv_name) {
		/* Limit node selection to those in selected reservation */
		time_t start_res = time(NULL);
		rc = job_test_resv(job_ptr, &start_res, false,
				   &usable_node_mask, NULL);
		if (rc != SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
			if (rc == ESLURM_INVALID_TIME_VALUE)
				return ESLURM_RESERVATION_NOT_USABLE;

			if (rc == ESLURM_NODES_BUSY)
				return ESLURM_NODES_BUSY;

			/* Defunct reservation or accesss denied */
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
		if ((detail_ptr->req_node_bitmap) &&
		    (!bit_super_set(detail_ptr->req_node_bitmap,
				    usable_node_mask))) {
			job_ptr->state_reason = WAIT_RESERVATION;
			xfree(job_ptr->state_desc);
			FREE_NULL_BITMAP(usable_node_mask);
			/* Required nodes outside of the reservation */
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
	}
	if ((job_ptr->details->min_nodes == 0) &&
	    (job_ptr->details->max_nodes == 0)) {
		*node_set_pptr = NULL;
		*node_set_size = 0;
		return SLURM_SUCCESS;
	}

	node_set_inx = 0;
	node_set_ptr = (struct node_set *)
			xmalloc(sizeof(struct node_set) * 2);
	node_set_ptr[node_set_inx+1].my_bitmap = NULL;
	if (detail_ptr->exc_node_bitmap) {
		if (usable_node_mask) {
			bit_not(detail_ptr->exc_node_bitmap);
			bit_and(usable_node_mask, detail_ptr->exc_node_bitmap);
			bit_not(detail_ptr->exc_node_bitmap);
		} else {
			usable_node_mask =
				bit_copy(detail_ptr->exc_node_bitmap);
			bit_not(usable_node_mask);
		}
	} else if (usable_node_mask == NULL) {
		usable_node_mask = bit_alloc(node_record_count);
		bit_nset(usable_node_mask, 0, (node_record_count - 1));
	}

	if (!_valid_feature_counts(detail_ptr, usable_node_mask, &has_xor)) {
		info("No job %u feature requirements can not be met",
		     job_ptr->job_id);
		FREE_NULL_BITMAP(usable_node_mask);
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	config_iterator = list_iterator_create(config_list);

	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
		config_filter = 0;
		adj_cpus = adjust_cpus_nppcu(_get_ntasks_per_core(detail_ptr),
					     config_ptr->threads,
					     config_ptr->cpus);
		if ((detail_ptr->pn_min_cpus     >  adj_cpus) ||
		    ((detail_ptr->pn_min_memory & (~MEM_PER_CPU)) >
		      config_ptr->real_memory)                               ||
		    (detail_ptr->pn_min_tmp_disk > config_ptr->tmp_disk))
			config_filter = 1;
		if (mc_ptr &&
		    (((mc_ptr->sockets_per_node > config_ptr->sockets) &&
		      (mc_ptr->sockets_per_node != (uint16_t) NO_VAL)) ||
		     ((mc_ptr->cores_per_socket > config_ptr->cores)   &&
		      (mc_ptr->cores_per_socket != (uint16_t) NO_VAL)) ||
		     ((mc_ptr->threads_per_core > config_ptr->threads) &&
		      (mc_ptr->threads_per_core != (uint16_t) NO_VAL))))
			config_filter = 1;

		/* since nodes can register with more resources than defined */
		/* in the configuration, we want to use those higher values */
		/* for scheduling, but only as needed (slower) */
		if (slurmctld_conf.fast_schedule) {
			if (config_filter)
				continue;
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
					     detail_ptr);
		}
		if (node_set_ptr[node_set_inx].nodes == 0) {
			FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
			continue;
		}

		if (has_xor) {
			tmp_feature = _valid_features(job_ptr->details,
						      config_ptr);
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
		node_set_ptr[node_set_inx].weight =
			config_ptr->weight;
		max_weight = MAX(max_weight, config_ptr->weight);
		node_set_ptr[node_set_inx].features =
			xstrdup(config_ptr->feature);
		node_set_ptr[node_set_inx].feature_bits = tmp_feature;
		debug2("found %d usable nodes from config containing %s",
		       node_set_ptr[node_set_inx].nodes, config_ptr->nodes);

		node_set_inx++;
		xrealloc(node_set_ptr,
			 sizeof(struct node_set) * (node_set_inx + 2));
		node_set_ptr[node_set_inx + 1].my_bitmap = NULL;
	}
	list_iterator_destroy(config_iterator);
	/* eliminate last (incomplete) node_set record */
	xfree(node_set_ptr[node_set_inx].features);
	FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
	FREE_NULL_BITMAP(node_set_ptr[node_set_inx].feature_bits);
	FREE_NULL_BITMAP(usable_node_mask);

	if (node_set_inx == 0) {
		info("No nodes satisfy job %u requirements in partition %s",
		     job_ptr->job_id, job_ptr->part_ptr->name);
		xfree(node_set_ptr);
		if (job_ptr->resv_name) {
			job_ptr->state_reason = WAIT_RESERVATION;
			return ESLURM_NODES_BUSY;
		}
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	/* If any nodes are powered down, put them into a new node_set
	 * record with a higher scheduling weight. This means we avoid
	 * scheduling jobs on powered down nodes where possible. */
	for (i = (node_set_inx-1); i >= 0; i--) {
		power_cnt = bit_overlap(node_set_ptr[i].my_bitmap,
					power_node_bitmap);
		if (power_cnt == 0)
			continue;	/* no nodes powered down */
		if (power_cnt == node_set_ptr[i].nodes) {
			node_set_ptr[i].weight += max_weight;	/* avoid all */
			continue;	/* all nodes powered down */
		}

		/* Some nodes powered down, others up, split record */
		node_set_ptr[node_set_inx].cpus_per_node =
			node_set_ptr[i].cpus_per_node;
		node_set_ptr[node_set_inx].real_memory =
			node_set_ptr[i].real_memory;
		node_set_ptr[node_set_inx].nodes = power_cnt;
		node_set_ptr[i].nodes -= power_cnt;
		node_set_ptr[node_set_inx].weight =
			node_set_ptr[i].weight + max_weight;
		node_set_ptr[node_set_inx].features =
			xstrdup(node_set_ptr[i].features);
		node_set_ptr[node_set_inx].feature_bits =
			bit_copy(node_set_ptr[i].feature_bits);
		node_set_ptr[node_set_inx].my_bitmap =
			bit_copy(node_set_ptr[i].my_bitmap);
		bit_and(node_set_ptr[node_set_inx].my_bitmap,
			power_node_bitmap);
		if (power_up_bitmap == NULL) {
			power_up_bitmap = bit_copy(power_node_bitmap);
			bit_not(power_up_bitmap);
		}
		bit_and(node_set_ptr[i].my_bitmap, power_up_bitmap);

		node_set_inx++;
		xrealloc(node_set_ptr,
			 sizeof(struct node_set) * (node_set_inx + 2));
		node_set_ptr[node_set_inx + 1].my_bitmap = NULL;
	}
	FREE_NULL_BITMAP(power_up_bitmap);

	*node_set_size = node_set_inx;
	*node_set_pptr = node_set_ptr;
	return SLURM_SUCCESS;
}

/* Remove from the node set any nodes which lack sufficient resources
 *	to satisfy the job's request */
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *job_con)
{
	int adj_cpus, i;
	multi_core_data_t *mc_ptr = job_con->mc_ptr;

	if (slurmctld_conf.fast_schedule) {	/* test config records */
		struct config_record *node_con = NULL;
		for (i = 0; i < node_record_count; i++) {
			int job_ok = 0, job_mc_ptr_ok = 0;
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;
			node_con = node_record_table_ptr[i].config_ptr;
			adj_cpus = adjust_cpus_nppcu(_get_ntasks_per_core(job_con),
						     node_con->threads,
						     node_con->cpus);
			if ((job_con->pn_min_cpus     <= adj_cpus)           &&
			    ((job_con->pn_min_memory & (~MEM_PER_CPU)) <=
			      node_con->real_memory)                         &&
			    (job_con->pn_min_tmp_disk <= node_con->tmp_disk))
				job_ok = 1;
			if (mc_ptr &&
			    (((mc_ptr->sockets_per_node <= node_con->sockets)  ||
			      (mc_ptr->sockets_per_node == (uint16_t) NO_VAL)) &&
			     ((mc_ptr->cores_per_socket <= node_con->cores)    ||
			      (mc_ptr->cores_per_socket == (uint16_t) NO_VAL)) &&
			     ((mc_ptr->threads_per_core <= node_con->threads)  ||
			      (mc_ptr->threads_per_core == (uint16_t) NO_VAL))))
				job_mc_ptr_ok = 1;
			if (job_ok && (!mc_ptr || job_mc_ptr_ok))
				continue;

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
			      (mc_ptr->sockets_per_node == (uint16_t) NO_VAL)) &&
			     ((mc_ptr->cores_per_socket <= node_ptr->cores)    ||
			      (mc_ptr->cores_per_socket == (uint16_t) NO_VAL)) &&
			     ((mc_ptr->threads_per_core <= node_ptr->threads)  ||
			      (mc_ptr->threads_per_core == (uint16_t) NO_VAL))))
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
		job_ptr->node_addr = NULL;
		return;
	}

	/* Use hostlist here to insure ordering of info matches that of srun */
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
		if (job_ptr->batch_host == NULL)
			job_ptr->batch_host = xstrdup(this_node_name);
		free(this_node_name);
	}
	hostlist_destroy(host_list);
	if (job_ptr->node_cnt != node_inx) {
		error("Node count mismatch for JobId=%u (%u,%u)",
		      job_ptr->job_id, job_ptr->node_cnt, node_inx);
	}
}

/*
 * _valid_features - Determine if the requested features are satisfied by
 *	the available nodes. This is only used for XOR operators.
 * IN details_ptr - job requirement details, includes requested features
 * IN config_ptr - node's configuration record
 * RET NULL if request is not satisfied, otherwise a bitmap indicating
 *	which mutually exclusive features are satisfied. For example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns a bitmap with
 *	the third bit set. For another example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs1,fs3") returns a bitmap
 *	with the first and third bits set. The function returns a bitmap
 *	with the first bit set if requirements are satisfied without a
 *	mutually exclusive feature list.
 */
static bitstr_t *_valid_features(struct job_details *details_ptr,
				 struct config_record *config_ptr)
{
	bitstr_t *result_bits = (bitstr_t *) NULL;
	ListIterator feat_iter;
	struct feature_record *job_feat_ptr;
	struct features_record *feat_ptr;
	int last_op = FEATURE_OP_AND, position = 0;

	result_bits = bit_alloc(MAX_FEATURES);
	if (details_ptr->feature_list == NULL) {	/* no constraints */
		bit_set(result_bits, 0);
		return result_bits;
	}

	feat_iter = list_iterator_create(details_ptr->feature_list);
	while ((job_feat_ptr = (struct feature_record *)
			list_next(feat_iter))) {
		if ((job_feat_ptr->op_code == FEATURE_OP_XAND) ||
		    (job_feat_ptr->op_code == FEATURE_OP_XOR)  ||
		    (last_op == FEATURE_OP_XAND) ||
		    (last_op == FEATURE_OP_XOR)) {
			feat_ptr = list_find_first(feature_list,
						   list_find_feature,
						   (void *)job_feat_ptr->name);
			if (feat_ptr &&
			    bit_super_set(config_ptr->node_bitmap,
					  feat_ptr->node_bitmap)) {
				bit_set(result_bits, position);
			}
			position++;
		}
		last_op = job_feat_ptr->op_code;
	}
	list_iterator_destroy(feat_iter);

	return result_bits;
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
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#endif

	xassert(job_ptr);
	xassert(job_ptr->details);

	kill_hostlist = hostlist_create("");

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->hostlist = hostlist_create("");
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

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host &&
	    (front_end_ptr = find_front_end_record(job_ptr->batch_host))) {
		if (IS_NODE_DOWN(front_end_ptr)) {
			for (i = 0, node_ptr = node_record_table_ptr;
			     i < node_record_count; i++, node_ptr++) {
				if ((job_ptr->node_bitmap_cg == NULL) ||
				    (!bit_test(job_ptr->node_bitmap_cg, i)))
					continue;
				bit_clear(job_ptr->node_bitmap_cg, i);
				job_update_cpu_cnt(job_ptr, i);
				if (node_ptr->comp_job_cnt)
					(node_ptr->comp_job_cnt)--;
				if ((job_ptr->node_cnt > 0) &&
				    ((--job_ptr->node_cnt) == 0)) {
					last_node_update = time(NULL);
					job_ptr->job_state &= (~JOB_COMPLETING);
					delete_step_records(job_ptr);
					slurm_sched_g_schedule();
				}
			}
		} else if (!IS_NODE_NO_RESPOND(front_end_ptr)) {
			(void) hostlist_push_host(kill_hostlist,
						  job_ptr->batch_host);
			hostlist_push(agent_args->hostlist,
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
			job_update_cpu_cnt(job_ptr, i);
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			if ((job_ptr->node_cnt > 0) &&
			    ((--job_ptr->node_cnt) == 0)) {
				job_ptr->job_state &= (~JOB_COMPLETING);
				delete_step_records(job_ptr);
				slurm_sched_g_schedule();
				last_node_update = time(NULL);
			}
		} else if (!IS_NODE_NO_RESPOND(node_ptr)) {
			(void)hostlist_push_host(kill_hostlist, node_ptr->name);
			hostlist_push(agent_args->hostlist, node_ptr->name);
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
