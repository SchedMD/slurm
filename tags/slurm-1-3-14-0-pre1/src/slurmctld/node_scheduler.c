/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs 
 *	Note: there is a global node table (node_record_table_ptr) 
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"

#define MAX_FEATURES  32	/* max exclusive features "[fs1|fs2]"=2 */
#define MAX_RETRIES   10

struct node_set {		/* set of nodes with same configuration */
	uint32_t cpus_per_node;	/* NOTE: This is the minimum count,
				 * if FastSchedule==0 then individual 
				 * nodes within the same configuration 
				 * line (in slurm.conf) can actually 
				 * have different CPU counts */
	uint32_t real_memory;
	uint32_t nodes;
	uint32_t weight;
	char     *features;
	char	**feature_array;	/* POINTER, NOT COPIED */
	bitstr_t *feature_bits;
	bitstr_t *my_bitmap;
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
			    uint32_t req_nodes, bool test_only);
static bitstr_t *_valid_features(struct job_details *detail_ptr, 
				 struct config_record *config_ptr);


/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 *	also claim required licenses and resources reserved by accounting
 *	policy association
 * IN job_ptr - job being allocated resources
 */
extern void allocate_nodes(struct job_record *job_ptr)
{
	int i;

	last_node_update = time(NULL);

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i))
			make_node_alloc(&node_record_table_ptr[i], job_ptr);
	}

	license_job_get(job_ptr);
	return;
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
 */
extern void deallocate_nodes(struct job_record *job_ptr, bool timeout, 
		bool suspended)
{
	int i;
	kill_job_msg_t *kill_job = NULL;
	agent_arg_t *agent_args = NULL;
	int down_node_cnt = 0;
	uint16_t base_state;

	xassert(job_ptr);
	xassert(job_ptr->details);

	license_job_return(job_ptr);
	acct_policy_job_fini(job_ptr);
	if (slurm_sched_freealloc(job_ptr) != SLURM_SUCCESS)
		error("slurm_sched_freealloc(%u): %m", job_ptr->job_id);
	if (select_g_job_fini(job_ptr) != SLURM_SUCCESS)
		error("select_g_job_fini(%u): %m", job_ptr->job_id);
	
	agent_args = xmalloc(sizeof(agent_arg_t));
	if (timeout)
		agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	else
		agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->retry = 0;	/* re_kill_job() resends as needed */
	agent_args->hostlist = hostlist_create("");
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	last_node_update    = time(NULL);
	kill_job->job_id    = job_ptr->job_id;
	kill_job->job_state = job_ptr->job_state;
	kill_job->job_uid   = job_ptr->user_id;
	kill_job->nodes     = xstrdup(job_ptr->nodes);
	kill_job->time      = time(NULL);
	kill_job->select_jobinfo = select_g_copy_jobinfo(
			job_ptr->select_jobinfo);

	for (i = 0; i < node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN) {
			/* Issue the KILL RPC, but don't verify response */
			down_node_cnt++;
			bit_clear(job_ptr->node_bitmap, i);
			job_ptr->node_cnt--;
		}
		make_node_comp(node_ptr, job_ptr, suspended);
#ifdef HAVE_FRONT_END		/* Operate only on front-end */
		if (agent_args->node_count > 0)
			continue;
#endif
		hostlist_push(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}

	if ((agent_args->node_count - down_node_cnt) == 0) {
		job_ptr->job_state &= (~JOB_COMPLETING);
		delete_step_records(job_ptr, 0);
		slurm_sched_schedule();
	}
	
	if (agent_args->node_count == 0) {
		error("Job %u allocated no nodes to be killed on",
		      job_ptr->job_id);
		xfree(kill_job->nodes);
		select_g_free_jobinfo(&kill_job->select_jobinfo);
		xfree(kill_job);
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
	int i;

	if (seek == NULL)
		return 1;	/* nothing to look for */
	if (node_set_ptr->feature_array == NULL)
		return 0;	/* nothing to find */

	for (i=0; node_set_ptr->feature_array[i]; i++) {
		if (strcmp(seek, node_set_ptr->feature_array[i]) == 0)
			return 1;	/* this is it */
	}
	return 0;	/* not found */
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
 * RET - 1 if nodes can be shared, 0 if nodes cannot be shared
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
 */
static int
_resolve_shared_status(uint16_t user_flag, uint16_t part_max_share,
		       int cons_res_flag)
{
	/* no sharing if part=EXCLUSIVE */
	if (part_max_share == 0)
		return 0;
	/* sharing if part=FORCE */
	if (part_max_share & SHARED_FORCE)
		return 1;

	if (cons_res_flag) {
		/* sharing unless user requested exclusive */
		if (user_flag == 0)
			return 0;
		return 1;
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
		  bool test_only)
{
	uint32_t saved_min_nodes, saved_job_min_nodes;
	bitstr_t *saved_req_node_bitmap = NULL;
	uint32_t saved_num_procs, saved_req_nodes;
	int tmp_node_set_size;
	struct node_set *tmp_node_set_ptr;
	int error_code = SLURM_SUCCESS, i;
	bitstr_t *feature_bitmap, *accumulate_bitmap = NULL;

	/* save job and request state */
	saved_min_nodes = min_nodes;
	saved_req_nodes = req_nodes;
	saved_job_min_nodes = job_ptr->details->min_nodes;
	if (job_ptr->details->req_node_bitmap) {
		accumulate_bitmap = job_ptr->details->req_node_bitmap;
		saved_req_node_bitmap = bit_copy(accumulate_bitmap);
		job_ptr->details->req_node_bitmap = NULL;
	}
	saved_num_procs = job_ptr->num_procs;
	job_ptr->num_procs = 1;
	tmp_node_set_ptr = xmalloc(sizeof(struct node_set) * node_set_size);

	/* Accumulate nodes with required feature counts.
	 * Ignored if job_ptr->details->req_node_layout is set (by wiki2).
	 * Selected nodes become part of job's required node list. */
	if (job_ptr->details->feature_list &&
	    (job_ptr->details->req_node_layout == NULL)) {
		ListIterator feat_iter;
		struct feature_record *feat_ptr;
		feat_iter = list_iterator_create(job_ptr->details->feature_list);
		while ((feat_ptr = (struct feature_record *)
				list_next(feat_iter))) {
			if (feat_ptr->count == 0)
				continue;
			tmp_node_set_size = 0;
			/* _pick_best_nodes() is destructive of the node_set
			 * data structure, so we need to make a copy then 
			 * purge it */
			for (i=0; i<node_set_size; i++) {
				if (!_match_feature(feat_ptr->name, 
						    node_set_ptr+i))
					continue;
				tmp_node_set_ptr[tmp_node_set_size].cpus_per_node =
					node_set_ptr[i].cpus_per_node;
				tmp_node_set_ptr[tmp_node_set_size].real_memory =
					node_set_ptr[i].real_memory;
				tmp_node_set_ptr[tmp_node_set_size].nodes =
					node_set_ptr[i].nodes;
				tmp_node_set_ptr[tmp_node_set_size].weight =
					node_set_ptr[i].weight;
				tmp_node_set_ptr[tmp_node_set_size].features = 
					xstrdup(node_set_ptr[i].features);
				tmp_node_set_ptr[tmp_node_set_size].feature_array =
					node_set_ptr[i].feature_array;
				tmp_node_set_ptr[tmp_node_set_size].feature_bits = 
					bit_copy(node_set_ptr[i].feature_bits);
				tmp_node_set_ptr[tmp_node_set_size].my_bitmap = 
					bit_copy(node_set_ptr[i].my_bitmap);
				tmp_node_set_size++;
			}
			feature_bitmap = NULL;
			min_nodes = feat_ptr->count;
			req_nodes = feat_ptr->count;
			job_ptr->details->min_nodes = feat_ptr->count;
			job_ptr->num_procs = feat_ptr->count;
			error_code = _pick_best_nodes(tmp_node_set_ptr, 
					tmp_node_set_size, &feature_bitmap, 
					job_ptr, part_ptr, min_nodes, 
					max_nodes, req_nodes, test_only);
#if 0
{
			char *tmp_str = bitmap2node_name(feature_bitmap);
			info("job %u needs %u nodes with feature %s, using %s", 
				job_ptr->job_id, feat_ptr->count, 
				feat_ptr->name, tmp_str);
			xfree(tmp_str);
}
#endif
			for (i=0; i<tmp_node_set_size; i++) {
				xfree(tmp_node_set_ptr[i].features);
				FREE_NULL_BITMAP(tmp_node_set_ptr[i].feature_bits);
				FREE_NULL_BITMAP(tmp_node_set_ptr[i].my_bitmap);
			}
			if (error_code != SLURM_SUCCESS)
				break;
			if (feature_bitmap) {
				if (job_ptr->details->req_node_bitmap) {
					bit_or(job_ptr->details->req_node_bitmap,
					       feature_bitmap);
				} else {
					job_ptr->details->req_node_bitmap =
						bit_copy(feature_bitmap);
				}
				if (accumulate_bitmap) {
					bit_or(accumulate_bitmap, feature_bitmap);
					bit_free(feature_bitmap);
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
		job_ptr->num_procs = MAX(saved_num_procs, node_cnt);
		min_nodes = MAX(saved_min_nodes, node_cnt);
		job_ptr->details->min_nodes = min_nodes;
		req_nodes = MAX(min_nodes, req_nodes);
		if (req_nodes > max_nodes)
			error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	} else {
		min_nodes = saved_min_nodes;
		req_nodes = saved_req_nodes;
		job_ptr->num_procs = saved_num_procs;
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
		error_code = _pick_best_nodes(node_set_ptr, node_set_size,
				select_bitmap, job_ptr, part_ptr, min_nodes, 
				max_nodes, req_nodes, test_only);
	}
#if 0
{
	char *tmp_str = bitmap2node_name(*select_bitmap);
	info("job %u allocated nodes %s err:%u", 
		job_ptr->job_id, tmp_str, error_code);
	xfree(tmp_str);
}
#endif

	/* restore job's initial required node bitmap */
	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	job_ptr->details->req_node_bitmap = saved_req_node_bitmap;
	job_ptr->num_procs = saved_num_procs;
	job_ptr->details->min_nodes = saved_job_min_nodes;

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
 * RET SLURM_SUCCESS on success, 
 *	ESLURM_NODES_BUSY if request can not be satisfied now, 
 *	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE if request can never 
 *	be satisfied , or
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE if the job can not be 
 *	initiated until the parition's configuration changes
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
		 bool test_only)
{
	int error_code = SLURM_SUCCESS, i, j, pick_code;
	int total_nodes = 0, avail_nodes = 0;	
	bitstr_t *avail_bitmap = NULL, *total_bitmap = NULL;
	bitstr_t *backup_bitmap = NULL;
	bitstr_t *possible_bitmap = NULL;
	bitstr_t *partially_idle_node_bitmap = NULL;
	int max_feature, min_feature;
	bool runable_ever  = false;	/* Job can ever run */
	bool runable_avail = false;	/* Job can run with available nodes */
	bool tried_sched = false;	/* Tried to schedule with avail nodes */
	static uint32_t cr_enabled = NO_VAL;
	select_type_plugin_info_t cr_type = SELECT_TYPE_INFO_NONE; 
	int shared = 0, select_mode;

	if (test_only)
		select_mode = SELECT_MODE_TEST_ONLY;
	else
		select_mode = SELECT_MODE_RUN_NOW;

	if (node_set_size == 0) {
		info("_pick_best_nodes: empty node set for selection");
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	/* Are Consumable Resources enabled?  Check once. */
	if (cr_enabled == NO_VAL) {
		cr_enabled = 0;	/* select/linear and bluegene are no-ops */
		error_code = select_g_get_info_from_plugin (SELECT_CR_PLUGIN, 
							    &cr_enabled);
		if (error_code != SLURM_SUCCESS) {
			cr_enabled = NO_VAL;
			return error_code;
		}
	}

	shared = _resolve_shared_status(job_ptr->details->shared,
					part_ptr->max_share, cr_enabled);
	job_ptr->details->shared = shared;

	if (cr_enabled) {
		/* Determine which nodes might be used by this job based upon
		 * its ability to share resources */
		job_ptr->cr_enabled = cr_enabled; /* CR enabled for this job */

		cr_type = (select_type_plugin_info_t) slurmctld_conf.
							select_type_param;
                debug3("Job %u shared %d cr_enabled %d CR type %d num_procs %d", 
		     job_ptr->job_id, shared, cr_enabled, cr_type, 
		     job_ptr->num_procs);

		if (shared == 0) {
			partially_idle_node_bitmap = bit_copy(idle_node_bitmap);
		} else {
			/* Update partially_idle_node_bitmap to reflect the
			 * idle and partially idle nodes */
			error_code = select_g_get_info_from_plugin (
					SELECT_BITMAP, 
					&partially_idle_node_bitmap);
			if (error_code != SLURM_SUCCESS) {
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
				return error_code;
			}
		}
        }

	if (job_ptr->details->req_node_bitmap) {  /* specific nodes required */
		/* We have already confirmed that all of these nodes have a
		 * usable configuration and are in the proper partition.
		 * Check that these nodes can be used by this job. */
		if (min_nodes != 0) {
			total_nodes = bit_set_count(
				job_ptr->details->req_node_bitmap);
		}
		if (total_nodes > max_nodes) {	/* exceeds node limit */
			FREE_NULL_BITMAP(partially_idle_node_bitmap);
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}

		/* check the availability of these nodes */
		/* Should we check memory availability on these nodes? */
		if (!bit_super_set(job_ptr->details->req_node_bitmap, 
				   avail_node_bitmap)) {
			FREE_NULL_BITMAP(partially_idle_node_bitmap);
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}

		if (partially_idle_node_bitmap) {
			if (!bit_super_set(job_ptr->details->req_node_bitmap, 
					   partially_idle_node_bitmap)) {
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
				return ESLURM_NODES_BUSY;
			}
		}
		if (shared) {
			if (!bit_super_set(job_ptr->details->req_node_bitmap, 
					   share_node_bitmap)) {
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
				return ESLURM_NODES_BUSY;
			}
		} else {
			if (!bit_super_set(job_ptr->details->req_node_bitmap, 
					   idle_node_bitmap)) {
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
				return ESLURM_NODES_BUSY;
			}
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

	/* Accumulate resources for this job based upon its required 
	 * features (possibly with node counts). */
	for (j = min_feature; j <= max_feature; j++) {
		for (i = 0; i < node_set_size; i++) {
			if (!bit_test(node_set_ptr[i].feature_bits, j))
				continue;

			if (total_bitmap)
				bit_or(total_bitmap, node_set_ptr[i].my_bitmap);
			else {
				total_bitmap = bit_copy(
						node_set_ptr[i].my_bitmap);
				if (total_bitmap == NULL)
					fatal("bit_copy malloc failure");
			}

			bit_and(node_set_ptr[i].my_bitmap, avail_node_bitmap);
			if (partially_idle_node_bitmap) {
				bit_and(node_set_ptr[i].my_bitmap,
					partially_idle_node_bitmap);
			}
			if (shared) {
				bit_and(node_set_ptr[i].my_bitmap,
					share_node_bitmap);
			} else {
				bit_and(node_set_ptr[i].my_bitmap,
					idle_node_bitmap);
			}
			if (avail_bitmap)
				bit_or(avail_bitmap, node_set_ptr[i].my_bitmap);
			else {
				avail_bitmap = bit_copy(
					node_set_ptr[i].my_bitmap);
				if (avail_bitmap == NULL)
					fatal("bit_copy malloc failure");
			}
			avail_nodes = bit_set_count(avail_bitmap);
			tried_sched = false;	/* need to test these nodes */

			if (shared && ((i+1) < node_set_size) && 
			    (node_set_ptr[i].weight == node_set_ptr[i+1].weight)) {
				/* Keep accumulating so we can pick the
				 * most lightly loaded nodes */
				continue;
			}

			if ((job_ptr->details->req_node_bitmap) &&
			    (!bit_super_set(job_ptr->details->req_node_bitmap, 
					avail_bitmap)))
				continue;

			if ((avail_nodes  < min_nodes) ||
			    ((req_nodes   > min_nodes) &&
			     (avail_nodes < req_nodes)))
				continue;	/* Keep accumulating nodes */

			/* NOTE: select_g_job_test() is destructive of
			 * avail_bitmap, so save a backup copy */
			backup_bitmap = bit_copy(avail_bitmap);
			pick_code = select_g_job_test(job_ptr, 
						      avail_bitmap, 
						      min_nodes, 
						      max_nodes,
						      req_nodes,
						      select_mode);
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
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
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
		if (avail_bitmap && (!tried_sched)
		&&  (avail_nodes >= min_nodes)
		&&  ((job_ptr->details->req_node_bitmap == NULL) ||
		     bit_super_set(job_ptr->details->req_node_bitmap, 
                                        avail_bitmap))) {
			pick_code = select_g_job_test(job_ptr, avail_bitmap, 
						      min_nodes, max_nodes,
						      req_nodes, 
						      select_mode);
			if ((pick_code == SLURM_SUCCESS) &&
			     (bit_set_count(avail_bitmap) <= max_nodes)) {
				FREE_NULL_BITMAP(partially_idle_node_bitmap);
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
		if (total_bitmap
		&&  (!runable_ever || !runable_avail)
		&&  (total_nodes >= min_nodes)
		&&  ((job_ptr->details->req_node_bitmap == NULL) ||
		     (bit_super_set(job_ptr->details->req_node_bitmap, 
					total_bitmap)))) {
			if (!runable_avail) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = bit_copy(total_bitmap);
				if (avail_bitmap == NULL)
					fatal("bit_copy malloc failure");
				bit_and(avail_bitmap, avail_node_bitmap);
				pick_code = select_g_job_test(job_ptr, 
							      avail_bitmap, 
							      min_nodes, 
							      max_nodes,
							      req_nodes,
							      SELECT_MODE_TEST_ONLY);
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
							      SELECT_MODE_TEST_ONLY);
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
	if (!runable_avail)
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (!runable_ever) {
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info("_pick_best_nodes: job %u never runnable", job_ptr->job_id);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = ESLURM_NODES_BUSY;
		*select_bitmap = possible_bitmap; 
	} else {
		FREE_NULL_BITMAP(possible_bitmap);
	}
	FREE_NULL_BITMAP(partially_idle_node_bitmap);
	return error_code;
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
	struct part_record *part_ptr = job_ptr->part_ptr;
	uint32_t min_nodes, max_nodes, req_nodes;
	enum job_state_reason fail_reason;
	time_t now = time(NULL);

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!acct_policy_job_runnable(job_ptr))
		return ESLURM_ACCOUNTING_POLICY;

	/* identify partition */
	if (part_ptr == NULL) {
		part_ptr = find_part_record(job_ptr->partition);
		xassert(part_ptr);
		job_ptr->part_ptr = part_ptr;
		error("partition pointer reset for job %u, part %s",
		      job_ptr->job_id, job_ptr->partition);
	}

	/* Confirm that partition is up and has compatible nodes limits */
	fail_reason = WAIT_NO_REASON;
	if (part_ptr->state_up == 0)
		fail_reason = WAIT_PART_STATE;
	else if (job_ptr->priority == 0)	/* user or administrator hold */
		fail_reason = WAIT_HELD;
	else if ((job_ptr->time_limit != NO_VAL) &&
		 (job_ptr->time_limit > part_ptr->max_time))
		fail_reason = WAIT_PART_TIME_LIMIT;
	else if (((job_ptr->details->max_nodes != 0) &&
	          (job_ptr->details->max_nodes < part_ptr->min_nodes)) ||
	         (job_ptr->details->min_nodes > part_ptr->max_nodes))
		 fail_reason = WAIT_PART_NODE_LIMIT;
	if (fail_reason != WAIT_NO_REASON) {
		job_ptr->state_reason = fail_reason;
		last_job_update = now;
		if (job_ptr->priority == 0)	/* user/admin hold */
			return ESLURM_JOB_HELD;
		job_ptr->priority = 1;	/* sys hold, move to end of queue */
		return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	}

	/* build sets of usable nodes based upon their configuration */
	error_code = _build_node_list(job_ptr, &node_set_ptr, &node_set_size);
	if (error_code)
		return error_code;

	/* insure that selected nodes in these node sets */
	if (job_ptr->details->req_node_bitmap) {
		error_code = _nodes_in_sets(job_ptr->details->req_node_bitmap, 
					    node_set_ptr, node_set_size);
		if (error_code) {
			info("No nodes satisfy requirements for JobId=%u",
			     job_ptr->job_id);
			goto cleanup;
		}
	}

	/* enforce both user's and partition's node limits */
	/* info("req: %u-%u, %u", job_ptr->details->min_nodes,
	   job_ptr->details->max_nodes, part_ptr->max_nodes); */

	min_nodes = MAX(job_ptr->details->min_nodes, part_ptr->min_nodes);
	if (job_ptr->details->max_nodes == 0)
		max_nodes = part_ptr->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes, 
				part_ptr->max_nodes);
	max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
	if (job_ptr->details->max_nodes)
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
					       req_nodes, test_only);
	}

	if (error_code) {
		if (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			/* Required nodes are down or 
			 * too many nodes requested */
			debug3("JobId=%u not runnable with present config",
			       job_ptr->job_id);
			job_ptr->state_reason = WAIT_PART_NODE_LIMIT;
			if (job_ptr->priority != 0)  /* Move to end of queue */
				job_ptr->priority = 1;
			last_job_update = now;
		} else {
			job_ptr->state_reason = WAIT_RESOURCES;
			if (error_code == ESLURM_NODES_BUSY)
				slurm_sched_job_is_pending();
		}
		goto cleanup;
	}
	if (test_only) {	/* set if job not highest priority */
		slurm_sched_job_is_pending();
		error_code = SLURM_SUCCESS;
		goto cleanup;
	}

	/* This job may be getting requeued, clear vestigial 
	 * state information before over-writting and leaking 
	 * memory. */
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	xfree(job_ptr->nodes);

	job_ptr->node_bitmap = select_bitmap;

	/* we need to have these times set to know when the endtime
	 * is for the job when we place it
	 */
	job_ptr->start_time = job_ptr->time_last_active = now;
	if (job_ptr->time_limit == NO_VAL)
		job_ptr->time_limit = part_ptr->max_time;
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
		goto cleanup;
	}

	/* assign the nodes and stage_in the job */
	job_ptr->state_reason = WAIT_NO_REASON;
	job_ptr->nodes = bitmap2node_name(select_bitmap);
	select_bitmap = NULL;	/* nothing left to free */
	allocate_nodes(job_ptr);
	build_node_details(job_ptr);
	job_ptr->job_state = JOB_RUNNING;
	if (select_g_update_nodeinfo(job_ptr) != SLURM_SUCCESS) {
		error("select_g_update_nodeinfo(%u): %m", job_ptr->job_id);
		/* not critical ... by now */
	}
	if (job_ptr->mail_type & MAIL_JOB_BEGIN)
		mail_job_info(job_ptr, MAIL_JOB_BEGIN);

	acct_policy_job_begin(job_ptr);

	jobacct_storage_g_job_start(
		acct_db_conn, slurmctld_cluster_name, job_ptr);

	slurm_sched_newalloc(job_ptr);

      cleanup:
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
 * job_req_node_filter - job reqeust node filter.
 *	clear from a bitmap the nodes which can not be used for a job
 *	test memory size, required features, processor count, etc.
 * NOTE: Does not support exclusive OR of features or feature counts.
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
	bitstr_t *feature_bitmap = NULL;

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
		feature_bitmap = _valid_features(detail_ptr, config_ptr);
		if ((feature_bitmap == NULL) || (!bit_test(feature_bitmap, 0))) {
			bit_clear(avail_bitmap, i);
			continue;
		}
		FREE_NULL_BITMAP(feature_bitmap);
		if (slurmctld_conf.fast_schedule) {
			if ((detail_ptr->job_min_procs    > config_ptr->cpus       )
			||  ((detail_ptr->job_min_memory & (~MEM_PER_CPU)) > 
			      config_ptr->real_memory) 
			||  (detail_ptr->job_min_tmp_disk > config_ptr->tmp_disk)) {
				bit_clear(avail_bitmap, i);
				continue;
			}
			if (mc_ptr
			&&  ((mc_ptr->min_sockets     > config_ptr->sockets  )
			||   (mc_ptr->min_cores       > config_ptr->cores    )
			||   (mc_ptr->min_threads     > config_ptr->threads  )
			||   (mc_ptr->job_min_sockets > config_ptr->sockets  )
			||   (mc_ptr->job_min_cores   > config_ptr->cores    )
			||   (mc_ptr->job_min_threads > config_ptr->threads  ))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		} else {
			if ((detail_ptr->job_min_procs    > node_ptr->cpus       )
			||  ((detail_ptr->job_min_memory & (~MEM_PER_CPU)) >
			      node_ptr->real_memory) 
			||  (detail_ptr->job_min_tmp_disk > node_ptr->tmp_disk)) {
				bit_clear(avail_bitmap, i);
				continue;
			}
			if (mc_ptr
			&&  ((mc_ptr->min_sockets     > node_ptr->sockets  )
			||   (mc_ptr->min_cores       > node_ptr->cores    )
			||   (mc_ptr->min_threads     > node_ptr->threads  )
			||   (mc_ptr->job_min_sockets > node_ptr->sockets  )
			||   (mc_ptr->job_min_cores   > node_ptr->cores    )
			||   (mc_ptr->job_min_threads > node_ptr->threads  ))) {
				bit_clear(avail_bitmap, i);
				continue;
			}
		}
	}
	FREE_NULL_BITMAP(feature_bitmap);
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
	int node_set_inx;
	struct node_set *node_set_ptr;
	struct config_record *config_ptr;
	struct part_record *part_ptr = job_ptr->part_ptr;
	ListIterator config_iterator;
	int check_node_config, config_filter = 0;
	struct job_details *detail_ptr = job_ptr->details;
	bitstr_t *exc_node_mask = NULL;
	multi_core_data_t *mc_ptr = detail_ptr->mc_ptr;
	bitstr_t *tmp_feature;

	node_set_inx = 0;
	node_set_ptr = (struct node_set *) 
			xmalloc(sizeof(struct node_set) * 2);
	node_set_ptr[node_set_inx+1].my_bitmap = NULL;
	if (detail_ptr->exc_node_bitmap) {
		exc_node_mask = bit_copy(detail_ptr->exc_node_bitmap);
		if (exc_node_mask == NULL)
			fatal("bit_copy malloc failure");
		bit_not(exc_node_mask);
	}

	config_iterator = list_iterator_create(config_list);
	if (config_iterator == NULL)
		fatal("list_iterator_create malloc failure");

	while ((config_ptr = (struct config_record *) 
			list_next(config_iterator))) {

		config_filter = 0;
		if ((detail_ptr->job_min_procs    > config_ptr->cpus       )
		||  ((detail_ptr->job_min_memory & (~MEM_PER_CPU)) > 
		      config_ptr->real_memory) 
		||  (detail_ptr->job_min_tmp_disk > config_ptr->tmp_disk))
			config_filter = 1;
		if (mc_ptr
		&&  ((mc_ptr->min_sockets      > config_ptr->sockets    )
		||   (mc_ptr->min_cores        > config_ptr->cores      )
		||   (mc_ptr->min_threads      > config_ptr->threads    )
		||   (mc_ptr->job_min_sockets  > config_ptr->sockets    )
		||   (mc_ptr->job_min_cores    > config_ptr->cores      )
		||   (mc_ptr->job_min_threads  > config_ptr->threads    )))
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
		if (node_set_ptr[node_set_inx].my_bitmap == NULL)
			fatal("bit_copy malloc failure");
		bit_and(node_set_ptr[node_set_inx].my_bitmap,
			part_ptr->node_bitmap);
		if (exc_node_mask) {
			bit_and(node_set_ptr[node_set_inx].my_bitmap,
				exc_node_mask);
		}
		node_set_ptr[node_set_inx].nodes =
			bit_set_count(node_set_ptr[node_set_inx].my_bitmap);
		if (check_node_config 
		&&  (node_set_ptr[node_set_inx].nodes != 0)) {
			_filter_nodes_in_set(&node_set_ptr[node_set_inx], 
					     detail_ptr);
		}
		if (node_set_ptr[node_set_inx].nodes == 0) {
			FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
			continue;
		}

		tmp_feature = _valid_features(job_ptr->details, config_ptr);
		if (tmp_feature == NULL) {
			FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
			continue;
		}
		/* NOTE: Must bit_free(tmp_feature) to avoid memory leak */

		node_set_ptr[node_set_inx].cpus_per_node =
			config_ptr->cpus;
		node_set_ptr[node_set_inx].real_memory =
			config_ptr->real_memory;		
		node_set_ptr[node_set_inx].weight =
			config_ptr->weight;
		node_set_ptr[node_set_inx].features = 
			xstrdup(config_ptr->feature);
		node_set_ptr[node_set_inx].feature_array = 
			config_ptr->feature_array;	/* NOTE: NOT COPIED */
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
	FREE_NULL_BITMAP(exc_node_mask);

	if (node_set_inx == 0) {
		info("No nodes satisfy job %u requirements", 
		     job_ptr->job_id);
		xfree(node_set_ptr);
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	*node_set_size = node_set_inx;
	*node_set_pptr = node_set_ptr;
	return SLURM_SUCCESS;
}

/* Remove from the node set any nodes which lack sufficient resources 
 *	to satisfy the job's request */
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *job_con)
{
	int i;
	multi_core_data_t *mc_ptr = job_con->mc_ptr;

	if (slurmctld_conf.fast_schedule) {	/* test config records */
		struct config_record *node_con = NULL;
		for (i = 0; i < node_record_count; i++) {
			int job_ok = 0, job_mc_ptr_ok = 0;
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;

			node_con = node_record_table_ptr[i].config_ptr;
			if ((job_con->job_min_procs    <= node_con->cpus)
			&&  ((job_con->job_min_memory & (~MEM_PER_CPU)) <= 
			      node_con->real_memory)
			&&  (job_con->job_min_tmp_disk <= node_con->tmp_disk))
				job_ok = 1;
			if (mc_ptr
			&&  ((mc_ptr->min_sockets      <= node_con->sockets)
			&&   (mc_ptr->min_cores        <= node_con->cores  )
			&&   (mc_ptr->min_threads      <= node_con->threads)
			&&   (mc_ptr->job_min_sockets  <= node_con->sockets)
			&&   (mc_ptr->job_min_cores    <= node_con->cores  )
			&&   (mc_ptr->job_min_threads  <= node_con->threads)))
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
			if ((job_con->job_min_procs    <= node_ptr->cpus)
			&&  ((job_con->job_min_memory & (~MEM_PER_CPU)) <= 
			      node_ptr->real_memory)
			&&  (job_con->job_min_tmp_disk <= node_ptr->tmp_disk))
				job_ok = 1;
			if (mc_ptr
			&&  ((mc_ptr->min_sockets      <= node_ptr->sockets)
			&&   (mc_ptr->min_cores        <= node_ptr->cores  )
			&&   (mc_ptr->min_threads      <= node_ptr->threads)
			&&   (mc_ptr->job_min_sockets  <= node_ptr->sockets)
			&&   (mc_ptr->job_min_cores    <= node_ptr->cores  )
			&&   (mc_ptr->job_min_threads  <= node_ptr->threads)))
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
			if (scratch_bitmap == NULL)
				fatal("bit_copy malloc failure");
		}
	}

	if ((scratch_bitmap == NULL)
	    || (bit_super_set(req_bitmap, scratch_bitmap) != 1))
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

	FREE_NULL_BITMAP(scratch_bitmap);
	return error_code;
}

/* Update record of a job's allocated processors for each step */
static void _alloc_step_cpus(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	if (job_ptr->step_list == NULL)
		return;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator))) {
		step_alloc_lps(step_ptr);
	}
	list_iterator_destroy(step_iterator);
}

/*
 * build_node_details - set cpu counts and addresses for allocated nodes:
 *	cpu_count_reps, cpus_per_node, node_addr, node_cnt, num_cpu_groups
 * IN job_ptr - pointer to a job record
 */
extern void build_node_details(struct job_record *job_ptr)
{
	hostlist_t host_list = NULL;
	struct node_record *node_ptr;
	char *this_node_name;
        int error_code = SLURM_SUCCESS;
	int node_inx = 0, cpu_inx = -1;
        int cr_count = 0;
	uint32_t total_procs = 0;

	if ((job_ptr->node_bitmap == NULL) || (job_ptr->nodes == NULL)) {
		/* No nodes allocated, we're done... */
		job_ptr->num_cpu_groups = 0;
		job_ptr->node_cnt = 0;
		job_ptr->cpus_per_node = NULL;
		job_ptr->cpu_count_reps = NULL;
		job_ptr->node_addr = NULL;
		job_ptr->alloc_lps_cnt = 0;
		xfree(job_ptr->alloc_lps);
		xfree(job_ptr->used_lps);
		return;
	}

	job_ptr->num_cpu_groups = 0;
	
	/* Use hostlist here to insure ordering of info matches that of srun */
	if ((host_list = hostlist_create(job_ptr->nodes)) == NULL)
		fatal("hostlist_create error for %s: %m", job_ptr->nodes);

	job_ptr->node_cnt = hostlist_count(host_list);	

	xrealloc(job_ptr->cpus_per_node, 
		 (sizeof(uint32_t) * job_ptr->node_cnt));
	xrealloc(job_ptr->cpu_count_reps, 
		 (sizeof(uint32_t) * job_ptr->node_cnt));
	xrealloc(job_ptr->node_addr, 
		 (sizeof(slurm_addr) * job_ptr->node_cnt));	

	job_ptr->alloc_lps_cnt = job_ptr->node_cnt;
	xrealloc(job_ptr->alloc_lps,
		 (sizeof(uint32_t) * job_ptr->node_cnt));
	xrealloc(job_ptr->used_lps,
		 (sizeof(uint32_t) * job_ptr->node_cnt));

	while ((this_node_name = hostlist_shift(host_list))) {
		node_ptr = find_node_record(this_node_name);
		     		
		if (node_ptr) {
			uint16_t usable_lps = 0;
#ifdef HAVE_BG
			if(job_ptr->node_cnt == 1) {
				memcpy(&job_ptr->node_addr[node_inx++],
				       &node_ptr->slurm_addr, 
				       sizeof(slurm_addr));
				cpu_inx++;
				
				job_ptr->cpus_per_node[cpu_inx] =
					job_ptr->num_procs;
				total_procs += job_ptr->num_procs;
				job_ptr->cpu_count_reps[cpu_inx] = 1;
				job_ptr->alloc_lps[0] = job_ptr->num_procs;
				job_ptr->used_lps[0]  = 0;
				goto cleanup;
			}
#endif
			error_code = select_g_get_extra_jobinfo( 
				node_ptr, job_ptr, SELECT_AVAIL_CPUS, 
				&usable_lps);
			if (error_code == SLURM_SUCCESS) {
				if (job_ptr->alloc_lps) {
					job_ptr->used_lps[cr_count] = 0;
					job_ptr->alloc_lps[cr_count++] =
								usable_lps;
				}
			} else {
				error("Unable to get extra jobinfo "
				      "from JobId=%u", job_ptr->job_id);
				/* Job is likely completed according to 
				 * select plugin */
				if (job_ptr->alloc_lps) {
					job_ptr->used_lps[cr_count] = 0;
					job_ptr->alloc_lps[cr_count++] = 0;
				}
			}
			
			memcpy(&job_ptr->node_addr[node_inx++],
			       &node_ptr->slurm_addr, sizeof(slurm_addr));

			if ((cpu_inx == -1) ||
			    (job_ptr->cpus_per_node[cpu_inx] !=
			     usable_lps)) {
				cpu_inx++;
				job_ptr->cpus_per_node[cpu_inx] =
					usable_lps;
				job_ptr->cpu_count_reps[cpu_inx] = 1;
			} else
				job_ptr->cpu_count_reps[cpu_inx]++;
			total_procs +=  usable_lps;

		} else {
			error("Invalid node %s in JobId=%u",
			      this_node_name, job_ptr->job_id);
		}
#ifdef HAVE_BG
 cleanup:	
#endif
		free(this_node_name);
	}
	hostlist_destroy(host_list);
	if (job_ptr->node_cnt != node_inx) {
		error("Node count mismatch for JobId=%u (%u,%u)",
		      job_ptr->job_id, job_ptr->node_cnt, node_inx);
	}
	job_ptr->num_cpu_groups = cpu_inx + 1;
	job_ptr->total_procs = total_procs;
	_alloc_step_cpus(job_ptr);	/* reset counters */
}

/*
 * _valid_features - determine if the requested features are satisfied by
 *	the available nodes
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
	struct feature_record *feat_ptr;
	int found, last_op, position = 0, result;
	int save_op = FEATURE_OP_AND, save_result=1;

	if (details_ptr->feature_list == NULL) {/* no constraints */
		result_bits = bit_alloc(MAX_FEATURES);
		bit_set(result_bits, 0);
		return result_bits;
	}

	result = 1;				/* assume good for now */
	last_op = FEATURE_OP_AND;
	feat_iter = list_iterator_create(details_ptr->feature_list);
	while ((feat_ptr = (struct feature_record *) list_next(feat_iter))) {
		found = 0;
		if (feat_ptr->count)
			found = 1;
		else if (config_ptr->feature_array) {
			int i;
			for (i=0; config_ptr->feature_array[i]; i++) {
				if (strcmp(feat_ptr->name, 
					   config_ptr->feature_array[i]))
					continue;
				found = 1;
				break;
			}
		}

		if ((last_op == FEATURE_OP_XOR) ||
		    (feat_ptr->op_code == FEATURE_OP_XOR)) {
			if (position == 0) {
				save_op = last_op;
				save_result = result;
				result = found;
			} else
				result |= found;

			if (!result_bits)
				result_bits = bit_alloc(MAX_FEATURES);

			if (!found)
				;
			else if (position < MAX_FEATURES)
				bit_set(result_bits, position);
			else
				error("_valid_features: overflow");
			position++;

			if (feat_ptr->op_code != FEATURE_OP_XOR) {
				if (save_op == FEATURE_OP_OR)
					result |= save_result;
				else /* (save_op == FEATURE_OP_AND) */
					result &= save_result;
			}
		} else if (last_op == FEATURE_OP_OR) {
			result |= found;
		} else if (last_op == FEATURE_OP_AND) {
			result &= found;
		}
		last_op = feat_ptr->op_code;
	}
	list_iterator_destroy(feat_iter);

	if (result) {
		if (!result_bits) {
			result_bits = bit_alloc(MAX_FEATURES);
			bit_set(result_bits, 0);
		}
	} else {
		FREE_NULL_BITMAP(result_bits);
	}

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
	hostlist_t kill_hostlist = hostlist_create("");
	char host_str[64];
	static uint32_t last_job_id = 0;

	xassert(job_ptr);
	xassert(job_ptr->details);

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->hostlist = hostlist_create("");
	agent_args->retry = 0;
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	kill_job->job_id    = job_ptr->job_id;
	kill_job->job_uid   = job_ptr->user_id;
	kill_job->job_state = job_ptr->job_state;
	kill_job->time      = time(NULL);
	kill_job->select_jobinfo = select_g_copy_jobinfo(
			job_ptr->select_jobinfo);

	for (i = 0; i < node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];
		if ((job_ptr->node_bitmap == NULL) ||
		    (bit_test(job_ptr->node_bitmap, i) == 0))
			continue;
		if ((node_ptr->node_state & NODE_STATE_BASE) 
				== NODE_STATE_DOWN) {
			/* Consider job already completed */
			bit_clear(job_ptr->node_bitmap, i);
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			if ((--job_ptr->node_cnt) == 0) {
				last_node_update = time(NULL);
				job_ptr->job_state &= (~JOB_COMPLETING);
				delete_step_records(job_ptr, 0);
				slurm_sched_schedule();
			}
			continue;
		}
		if (node_ptr->node_state & NODE_STATE_NO_RESPOND)
			continue;
		(void) hostlist_push_host(kill_hostlist, node_ptr->name);
#ifdef HAVE_FRONT_END		/* Operate only on front-end */
		if (agent_args->node_count > 0)
			continue;
#endif
		hostlist_push(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}

	if (agent_args->node_count == 0) {
		xfree(kill_job);
		xfree(agent_args);
		hostlist_destroy(kill_hostlist);
		return;
	}
	hostlist_uniq(kill_hostlist);
	hostlist_ranged_string(kill_hostlist, 
			sizeof(host_str), host_str);
#ifdef HAVE_BG
	if (job_ptr->job_id != last_job_id) {
		info("Resending TERMINATE_JOB request JobId=%u BPlist=%s",
			job_ptr->job_id, host_str);
	} else {
		debug("Resending TERMINATE_JOB request JobId=%u BPlist=%s",
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
	last_job_id = job_ptr->job_id;
	hostlist_destroy(kill_hostlist);
	agent_args->msg_args = kill_job;
	agent_queue_request(agent_args);
	return;
}
