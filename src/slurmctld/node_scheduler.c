/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs 
 *	Note: there is a global node table (node_record_table_ptr) 
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/agent.h"
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
	bitstr_t *feature_bits;
	bitstr_t *my_bitmap;
};

static int _add_node_set_info(struct node_set *node_set_ptr, 
			      bitstr_t ** node_bitmap, 
			      int *node_cnt, int *cpu_cnt, 
			      const int mem_cnt, int cr_enabled,
			      struct job_record *job);
static int  _build_feature_list(struct job_record *job_ptr);
static int  _build_node_list(struct job_record *job_ptr, 
			     struct node_set **node_set_pptr,
			     int *node_set_size);
static void _feature_list_delete(void *x);
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *detail_ptr);
static int _job_count_bitmap(bitstr_t * bitmap, bitstr_t * jobmap,
			     int job_cnt); 
static int _match_feature(char *seek, char *available);
static int _nodes_in_sets(bitstr_t *req_bitmap, 
			  struct node_set * node_set_ptr, 
			  int node_set_size);
static int _pick_best_load(struct job_record *job_ptr, bitstr_t * bitmap, 
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, bool test_only);
static int _pick_best_nodes(struct node_set *node_set_ptr,
			    int node_set_size, bitstr_t ** select_bitmap,
			    struct job_record *job_ptr,
			    struct part_record *part_ptr,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes);
static void _print_feature_list(uint32_t job_id, List feature_list);
static bitstr_t *_valid_features(struct job_details *detail_ptr, 
				 char *available);


/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 * IN job_ptr - job being allocated resources
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	last_node_update - last update time of node table
 */
extern void allocate_nodes(struct job_record *job_ptr)
{
	int i;

	last_node_update = time(NULL);

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i))
			make_node_alloc(&node_record_table_ptr[i], job_ptr);
	}
	return;
}


/*
 * count_cpus - report how many cpus are associated with the identified nodes 
 * IN bitmap - map of nodes to tally
 * RET cpu count
 * globals: node_record_count - number of nodes configured
 *	node_record_table_ptr - pointer to global node table
 */
extern int count_cpus(unsigned *bitmap)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(bitmap, i) != 1)
			continue;
		if (slurmctld_conf.fast_schedule)
			sum += node_record_table_ptr[i].config_ptr->cpus;
		else
			sum += node_record_table_ptr[i].cpus;
	}
	return sum;
}


/*
 * deallocate_nodes - for a given job, deallocate its nodes and make 
 *	their state NODE_STATE_COMPLETING
 * IN job_ptr - pointer to terminating job (already in some COMPLETING state)
 * IN timeout - true if job exhausted time limit, send REQUEST_KILL_TIMELIMIT
 *	RPC instead of REQUEST_TERMINATE_JOB
 * IN suspended - true if job was already suspended (node's job_run_cnt 
 *	already decremented);
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
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
	last_node_update = time(NULL);
	kill_job->job_id  = job_ptr->job_id;
	kill_job->job_uid = job_ptr->user_id;
	kill_job->nodes   = xstrdup(job_ptr->nodes);
	kill_job->time    = time(NULL);
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
		delete_step_records(job_ptr, 1);
		slurm_sched_schedule();
	}
	if (agent_args->node_count == 0) {
		error("Job %u allocated no nodes to be killed on",
		      job_ptr->job_id);
		xfree(kill_job->nodes);
		select_g_free_jobinfo(&kill_job->select_jobinfo);
		xfree(kill_job);
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
 * IN available - comma separated list of available features
 * RET 1 if found, 0 otherwise
 */
static int _match_feature(char *seek, char *available)
{
	char *tmp_available = NULL, *str_ptr3 = NULL, *str_ptr4 = NULL;
	int found;

	if (seek == NULL)
		return 1;	/* nothing to look for */
	if (available == NULL)
		return SLURM_SUCCESS;	/* nothing to find */

	tmp_available = xstrdup(available);
	found = 0;
	str_ptr3 = (char *) strtok_r(tmp_available, ",", &str_ptr4);
	while (str_ptr3) {
		if (strcmp(seek, str_ptr3) == 0) {	/* we have a match */
			found = 1;
			break;
		}
		str_ptr3 = (char *) strtok_r(NULL, ",", &str_ptr4);
	}

	xfree(tmp_available);
	return found;
}


/*
 * _pick_best_load - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as the least loaded nodes
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * RET zero on success, EINVAL otherwise
 * globals: node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	_pick_best_load is called
 */
static int
_pick_best_load(struct job_record *job_ptr, bitstr_t * bitmap, 
		uint32_t min_nodes, uint32_t max_nodes, 
		uint32_t req_nodes, bool test_only)
{
	bitstr_t *basemap;
	int i, error_code = EINVAL, node_cnt = 0, prev_cnt = 0, set_cnt;

	basemap = bit_copy(bitmap);
	if (basemap == NULL)
		fatal("bit_copy malloc failure");

	set_cnt = bit_set_count(bitmap);
	if ((set_cnt < min_nodes) ||
	    ((req_nodes > min_nodes) && (set_cnt < req_nodes)))
		return error_code;	/* not usable */

	for (i=0; node_cnt<set_cnt; i++) {
		node_cnt = _job_count_bitmap(basemap, bitmap, i);
		if ((node_cnt == 0) || (node_cnt == prev_cnt))
			continue;	/* nothing new to test */
		if ((node_cnt < min_nodes) ||
		    ((req_nodes > min_nodes) && (node_cnt < req_nodes)))
			continue;	/* need more nodes */
		error_code = select_g_job_test(job_ptr, bitmap, 
					       min_nodes, max_nodes, 
					       req_nodes, test_only);
		if (!error_code)
			break;
		prev_cnt = node_cnt;
	}

	FREE_NULL_BITMAP(basemap);
	return error_code;
}

/*
 * Set the bits in 'jobmap' that correspond to bits in the 'bitmap'
 * that are running 'job_cnt' jobs or less, and clear the rest.
 */
static int
_job_count_bitmap(bitstr_t * bitmap, bitstr_t * jobmap, int job_cnt) 
{
	int i, count = 0;
	bitoff_t size = bit_size(bitmap);

	for (i = 0; i < size; i++) {
		if (bit_test(bitmap, i) &&
		    (node_record_table_ptr[i].run_job_cnt <= job_cnt)) {
			bit_set(jobmap, i);
			count++;
		} else {
			bit_clear(jobmap, i);
		}
	}
	return count;
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
 */
static int
_resolve_shared_status(uint16_t user_flag, uint16_t part_max_share,
		       int cons_res_flag)
{
	int shared;

	if (cons_res_flag) {
		/*
		 * Consumable resources will always share nodes by default,
		 * the partition or user has to explicitly disable sharing to
		 * get exclusive nodes.
		 */
		if ((part_max_share == 0) || (user_flag == 0))
			shared = 0;
		else
			shared = 1;
	} else {
		/* The partition sharing option is only used if
		 * the consumable resources plugin is NOT in use.
		 */
		if (part_max_share & SHARED_FORCE)  /* shared=force */
			shared = 1;
		else if (part_max_share <= 1)	/* can't share */
			shared = 0;
		else
			shared = (user_flag == 1) ? 1 : 0;
	}

	return shared;
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
		  uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes)
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
	if (job_ptr->details->req_node_bitmap)
		saved_req_node_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
	job_ptr->details->req_node_bitmap = NULL;
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
		while((feat_ptr = (struct feature_record *)
				list_next(feat_iter))) {
			if (feat_ptr->count == 0)
				continue;
			tmp_node_set_size = 0;
			/* _pick_best_nodes() is destructive of the node_set
			 * data structure, so we need to copy then purge */
			for (i=0; i<node_set_size; i++) {
				if (!_match_feature(feat_ptr->name, 
						node_set_ptr[i].features))
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
					max_nodes, req_nodes);
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
	min_nodes = saved_min_nodes;
	req_nodes = saved_req_nodes;
	job_ptr->details->min_nodes = saved_job_min_nodes;
	job_ptr->num_procs = saved_num_procs;
	if (saved_req_node_bitmap) {
		job_ptr->details->req_node_bitmap = 
				bit_copy(saved_req_node_bitmap);
	}
	if (accumulate_bitmap) {
		if (job_ptr->details->req_node_bitmap) {
			bit_or(job_ptr->details->req_node_bitmap, 
				accumulate_bitmap);
			FREE_NULL_BITMAP(accumulate_bitmap);
		} else
			job_ptr->details->req_node_bitmap = accumulate_bitmap;
	}
	xfree(tmp_node_set_ptr);
	if (error_code == SLURM_SUCCESS) {
		error_code = _pick_best_nodes(node_set_ptr, node_set_size,
				select_bitmap, job_ptr, part_ptr, min_nodes, 
				max_nodes, req_nodes);
	}

	/* restore job's initial required node bitmap */
	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	job_ptr->details->req_node_bitmap = saved_req_node_bitmap;


	return error_code;
}

/*
 * _pick_best_nodes - from a weigh order list of all nodes satisfying a 
 *	job's specifications, select the "best" for use
 * IN node_set_ptr - pointer to node specification information
 * IN node_set_size - number of entries in records pointed to by node_set_ptr
 * OUT select_bitmap - returns bitmap of selected nodes, must FREE_NULL_BITMAP
 * IN job_ptr - pointer to job being scheduled
 * IN part_ptr - pointer to the partition in which the job is being scheduled
 * IN min_nodes - minimum count of nodes required by the job
 * IN max_nodes - maximum count of nodes required by the job (0==no limit)
 * IN req_nodes - requested (or desired) count of nodes
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
 *	4) If nodes _not_ shared then call select_g_job_test() to select the 
 *	   "best" of those based upon topology, else call _pick_best_load()
 *	   to pick the "best" nodes in terms of workload
 *	5) If request can't be satisfied now, execute select_g_job_test() 
 *	   against the list of nodes that exist in any state (perhaps DOWN 
 *	   DRAINED or ALLOCATED) to determine if the request can
 *         ever be satified.
 */
static int
_pick_best_nodes(struct node_set *node_set_ptr, int node_set_size,
		 bitstr_t ** select_bitmap, struct job_record *job_ptr,
		 struct part_record *part_ptr,
		 uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes)
{
	int error_code = SLURM_SUCCESS, i, j, pick_code;
	int total_nodes = 0, total_cpus = 0; 
	uint32_t total_mem = 0; /* total_: total resources configured in
			      partition */
	int avail_nodes = 0, avail_cpus = 0;	
	int avail_mem = 0; /* avail_: resources available for use now */
	bitstr_t *avail_bitmap = NULL, *total_bitmap = NULL;
	bitstr_t *backup_bitmap = NULL;
	bitstr_t *partially_idle_node_bitmap = NULL, *possible_bitmap = NULL;
	int max_feature, min_feature;
	bool runable_ever  = false;	/* Job can ever run */
	bool runable_avail = false;	/* Job can run with available nodes */
	bool pick_light_load = false;
	uint32_t cr_enabled = 0;
	int shared = 0;
	select_type_plugin_info_t cr_type = SELECT_TYPE_INFO_NONE; 

	if (node_set_size == 0) {
		info("_pick_best_nodes: empty node set for selection");
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

        /* Is Consumable Resources enabled? */
        error_code = select_g_get_info_from_plugin (SELECT_CR_PLUGIN, 
						    &cr_enabled);
        if (error_code != SLURM_SUCCESS)
                return error_code;

	shared = _resolve_shared_status(job_ptr->details->shared,
					part_ptr->max_share, cr_enabled);
	job_ptr->details->shared = shared;

        if (cr_enabled) {
		shared = 0;
		job_ptr->cr_enabled = cr_enabled; /* CR enabled for this job */

		cr_type = (select_type_plugin_info_t) slurmctld_conf.
							select_type_param;
		if (cr_type == CR_MEMORY) {
			shared = 1; 	/* Sharing set when only memory 
					 * as a CR is enabled */
		} else if ((cr_type == CR_SOCKET) 
			   || (cr_type == CR_CORE) 
			   || (cr_type == CR_CPU)) {
			job_ptr->details->job_max_memory = 0;
		}

                debug3("Job %u in exclusive mode? "
		     "%d cr_enabled %d CR type %d num_procs %d", 
		     job_ptr->job_id, 
		     job_ptr->details->shared ? 0 : 1,
		     cr_enabled,
		     cr_type, 
		     job_ptr->num_procs);

		if (job_ptr->details->shared == 0) {
			partially_idle_node_bitmap = bit_copy(idle_node_bitmap);
		} else {
			/* Update partially_idle_node_bitmap to reflect the
			 * idle and partially idle nodes */
			error_code = select_g_get_info_from_plugin (
					SELECT_BITMAP, 
					&partially_idle_node_bitmap);
		}

                if (error_code != SLURM_SUCCESS) {
                       FREE_NULL_BITMAP(partially_idle_node_bitmap);
                       return error_code;
                }
        }

	if (job_ptr->details->req_node_bitmap) {  /* specific nodes required */
		/* we have already confirmed that all of these nodes have a
		 * usable configuration and are in the proper partition */
		if (min_nodes != 0) {
			total_nodes = bit_set_count(
				job_ptr->details->req_node_bitmap);
		}
		if (job_ptr->num_procs != 0) {
			if (cr_enabled) {
				uint16_t tmp16;
				if ((cr_type == CR_MEMORY)
				    || (cr_type == CR_SOCKET_MEMORY)
				    || (cr_type == CR_CORE_MEMORY)
				    || (cr_type == CR_CPU_MEMORY)) {
					/* Check if the requested amount of
					 * memory is available */
					error_code = select_g_get_extra_jobinfo (
					    NULL, 
					    job_ptr, 
					    SELECT_AVAIL_MEMORY, 
					    &total_mem);
					if (error_code != SLURM_SUCCESS) {
						FREE_NULL_BITMAP(
							partially_idle_node_bitmap);
						return ESLURM_NODES_BUSY;
					}
				}
				error_code = select_g_get_extra_jobinfo (
					NULL, 
					job_ptr, 
					SELECT_CPU_COUNT, 
					&tmp16);
				if (error_code != SLURM_SUCCESS) {
					FREE_NULL_BITMAP(
						partially_idle_node_bitmap);
					return error_code;
				}
				total_cpus = (int) tmp16;
			} else { 
				total_cpus = count_cpus(
					job_ptr->details->req_node_bitmap);
			}
		}
		if (total_nodes > max_nodes) {
			/* exceeds node limit */
                        if (cr_enabled) 
                                FREE_NULL_BITMAP(partially_idle_node_bitmap);
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
		if ((min_nodes <= total_nodes) && 
		    (max_nodes <= min_nodes) &&
		    (job_ptr->num_procs <= total_cpus )) {
			if (!bit_super_set(job_ptr->details->req_node_bitmap, 
                                        avail_node_bitmap)) {
				if (cr_enabled) { 
					FREE_NULL_BITMAP(
						partially_idle_node_bitmap);
				}
				return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
			}

			/* shared needs to be checked before cr_enabled
			 * to make sure that CR_MEMORY works correctly */
			if (shared) {
				if (!bit_super_set(job_ptr->details->
						   req_node_bitmap, 
						   share_node_bitmap)) {
					if (cr_enabled) {
						FREE_NULL_BITMAP(
							partially_idle_node_bitmap);
					}
					return ESLURM_NODES_BUSY;
				}
			} else if (cr_enabled) {
				if (!bit_super_set(job_ptr->details->
						   req_node_bitmap, 
						   partially_idle_node_bitmap)) {
					FREE_NULL_BITMAP(
					  partially_idle_node_bitmap);
					return ESLURM_NODES_BUSY;
				}
			} else {
				if (!bit_super_set(job_ptr->details->
						   req_node_bitmap, 
						   idle_node_bitmap)) {
					return ESLURM_NODES_BUSY;
				}
			}
			/* still must go through select_g_job_test() to 
			 * determine validity of request and/or perform
			 * set-up before job launch */
		}
		total_nodes = total_cpus = 0;	/* reinitialize */
	}

#ifndef HAVE_BG
	if (shared)
		pick_light_load = true;
#endif
		
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
		
	for (j = min_feature; j <= max_feature; j++) {
		for (i = 0; i < node_set_size; i++) {

			if (!bit_test(node_set_ptr[i].feature_bits, j))
				continue;
			if (!runable_ever) {
				int cr_disabled = 0;
				total_mem = 0;
				error_code = _add_node_set_info(
					&node_set_ptr[i],
					&total_bitmap, 
					&total_nodes, 
					&total_cpus,
					total_mem, 
					cr_disabled,
					job_ptr);
				if (error_code != SLURM_SUCCESS) {
					if (cr_enabled) {
						FREE_NULL_BITMAP(
							partially_idle_node_bitmap);
					}
					FREE_NULL_BITMAP(avail_bitmap);
					FREE_NULL_BITMAP(total_bitmap);
					FREE_NULL_BITMAP(possible_bitmap);
					return error_code;
				}
			}
			bit_and(node_set_ptr[i].my_bitmap, avail_node_bitmap);

			/* shared needs to be checked before cr_enabled
			 * to make sure that CR_MEMORY works correctly. */ 
			if (shared) {
				bit_and(node_set_ptr[i].my_bitmap,
					share_node_bitmap);
			} else if (cr_enabled) {
				bit_and(node_set_ptr[i].my_bitmap,
					partially_idle_node_bitmap);
			} else {
				bit_and(node_set_ptr[i].my_bitmap,
					idle_node_bitmap);
			}
			node_set_ptr[i].nodes =
				bit_set_count(node_set_ptr[i].my_bitmap);
			avail_mem = job_ptr->details->job_max_memory;
			error_code = _add_node_set_info(&node_set_ptr[i], 
							&avail_bitmap, 
                                                        &avail_nodes, 
							&avail_cpus, 
							avail_mem,
							cr_enabled,
							job_ptr);
                        if (error_code != SLURM_SUCCESS) {
				if (cr_enabled) { 
					FREE_NULL_BITMAP(
						partially_idle_node_bitmap);
				}
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(avail_bitmap);
				FREE_NULL_BITMAP(possible_bitmap);
				return error_code;
			}
			if (pick_light_load)
				continue; /* Keep accumulating */
			if (avail_nodes == 0)
				continue; /* Keep accumulating */
			if ((job_ptr->details->req_node_bitmap) &&
			    (!bit_super_set(job_ptr->details->req_node_bitmap, 
					avail_bitmap)))
				continue;
			if ((avail_nodes  < min_nodes) ||
			    ((req_nodes   > min_nodes) && 
			     (avail_nodes < req_nodes)))
				continue;	/* Keep accumulating nodes */
			if (avail_cpus   < job_ptr->num_procs)
				continue;	/* Keep accumulating CPUs */

			/* NOTE: select_g_job_test() is destructive of
			 * avail_bitmap, so save a backup copy */
			backup_bitmap = bit_copy(avail_bitmap);
			pick_code = select_g_job_test(job_ptr, 
						      avail_bitmap, 
						      min_nodes, 
						      max_nodes,
						      req_nodes,
						      false);

			if (pick_code == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(backup_bitmap);
				if (bit_set_count(avail_bitmap) > max_nodes) {
					/* end of tests for this feature */
					avail_nodes = 0; 
					break;
				}
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(possible_bitmap);
				if (cr_enabled) {
					FREE_NULL_BITMAP(
						partially_idle_node_bitmap);
				}
				*select_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			} else {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = backup_bitmap;
			}
		} /* for (i = 0; i < node_set_size; i++) */

		/* try picking the lightest load from all
		   available nodes with this feature set */
		if (pick_light_load) {
			backup_bitmap = bit_copy(avail_bitmap);
			pick_code = _pick_best_load(job_ptr, 
						    avail_bitmap, 
						    min_nodes, 
						    max_nodes,
						    req_nodes,
						    false);
			if (pick_code == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(backup_bitmap);
				if (bit_set_count(avail_bitmap) > max_nodes) {
					avail_nodes = 0; 
				} else {
					FREE_NULL_BITMAP(total_bitmap);
					FREE_NULL_BITMAP(possible_bitmap);
					if (cr_enabled) {
						FREE_NULL_BITMAP(
						    partially_idle_node_bitmap);
					}
					*select_bitmap = avail_bitmap;
					return SLURM_SUCCESS;
				}
			} else {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = backup_bitmap;
			}
		}

		/* try to get req_nodes now for this feature */
		if (avail_bitmap
		&&  (req_nodes   >  min_nodes) 
		&&  (avail_nodes >= min_nodes)
		&&  (avail_nodes <  req_nodes)
		&&  ((job_ptr->details->req_node_bitmap == NULL) ||
		     bit_super_set(job_ptr->details->req_node_bitmap, 
                                        avail_bitmap))) {
			pick_code = select_g_job_test(job_ptr, avail_bitmap, 
						      min_nodes, max_nodes,
						      req_nodes, false);
			if ((pick_code == SLURM_SUCCESS) &&
			     (bit_set_count(avail_bitmap) <= max_nodes)) {
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(possible_bitmap);
				if (cr_enabled) { 
					FREE_NULL_BITMAP(
						partially_idle_node_bitmap);
				}
				*select_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			}
		}

		/* determine if job could possibly run (if all configured 
		 * nodes available) */

		if (total_bitmap
		&&  (!runable_ever || !runable_avail)
		&&  (total_nodes >= min_nodes)
		&&  ((slurmctld_conf.fast_schedule == 0) ||
		     (total_cpus >= job_ptr->num_procs))
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
							      true);
                                if (cr_enabled)
                                        job_ptr->cr_enabled = 1;
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
							      true);
                                if (cr_enabled)
                                        job_ptr->cr_enabled = 1;
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

        if (cr_enabled) 
                FREE_NULL_BITMAP(partially_idle_node_bitmap);

	/* The job is not able to start right now, return a 
	 * value indicating when the job can start */
	if (!runable_avail)
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (!runable_ever) {
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info("_pick_best_nodes %u : job never runnable", job_ptr->job_id);
	}

	if (error_code == SLURM_SUCCESS) {
		error_code = ESLURM_NODES_BUSY;
		*select_bitmap = possible_bitmap; 
	} else {
		FREE_NULL_BITMAP(possible_bitmap);
	}
	return error_code;
}


/*
 * _add_node_set_info - add info in node_set_ptr to node_bitmap
 * IN node_set_ptr    - node set info
 * IN/OUT node_bitmap - add nodes in set to this bitmap
 * IN/OUT node_cnt    - add count of nodes in set to this total
 * IN/OUT cpu_cnt     - add count of cpus in set to this total
 * IN/OUT mem_cnt     - add count of memory in set to this total
 * IN cr_enabled      - specify if consumable resources (of processors) is enabled
 * IN job_ptr         - the job to be updated
 */
static int
_add_node_set_info(struct node_set *node_set_ptr, 
		   bitstr_t ** node_bitmap, 
		   int *node_cnt, int *cpu_cnt, 
		   const int mem_cnt, int cr_enabled,
		   struct job_record * job_ptr)
{
        int error_code = SLURM_SUCCESS, i;
	int this_cpu_cnt, this_mem_cnt;
	uint32_t alloc_mem;
	uint16_t alloc_cpus;
	uint32_t job_id = job_ptr->job_id;

        xassert(node_set_ptr->my_bitmap);

        if (cr_enabled == 0) {
		if (*node_bitmap)
			bit_or(*node_bitmap, node_set_ptr->my_bitmap);
		else {
			*node_bitmap = bit_copy(node_set_ptr->my_bitmap);
			if (*node_bitmap == NULL)
				fatal("bit_copy malloc failure");
		}
                *node_cnt += node_set_ptr->nodes;
		if (slurmctld_conf.fast_schedule) {
			*cpu_cnt  += node_set_ptr->nodes * 
				node_set_ptr->cpus_per_node;
		} else {
			for (i = 0; i < node_record_count; i++) {
				if (bit_test (node_set_ptr->my_bitmap, i) == 0)
					continue;
				*cpu_cnt  += node_record_table_ptr[i].cpus;
			}
		}
        } else {
		int ll; /* layout array index */
		uint16_t * layout_ptr = NULL;
		if (job_ptr->details)
			layout_ptr = job_ptr->details->req_node_layout;

                for (i = 0, ll = -1; i < node_record_count; i++) {
			if (layout_ptr &&
			    bit_test(job_ptr->details->req_node_bitmap, i)) {
				ll ++;
			}
                        if (bit_test (node_set_ptr->my_bitmap, i) == 0)
				continue;
                        alloc_cpus = 0;
                        error_code = select_g_get_select_nodeinfo(
				&node_record_table_ptr[i], 
				SELECT_ALLOC_CPUS, 
				&alloc_cpus);
                        if (error_code != SLURM_SUCCESS) {
				error("cons_res: Invalid Node reference %s", 
				      node_record_table_ptr[i].name);
				return error_code;
			}
                        alloc_mem = 0;
                        error_code = select_g_get_select_nodeinfo(
				&node_record_table_ptr[i], 
				SELECT_ALLOC_MEMORY, 
				&alloc_mem);
                        if (error_code != SLURM_SUCCESS) {
				error("cons_res: Invalid Node reference %s", 
				      node_record_table_ptr[i]. name);
				return error_code;
			}

			/* Determine processors and memory available for use */
			if (slurmctld_conf.fast_schedule) {
				this_cpu_cnt = node_set_ptr->cpus_per_node - 
					alloc_cpus;
				this_mem_cnt = (node_set_ptr->real_memory - 
					alloc_mem) - mem_cnt;
			} else {
				this_cpu_cnt = node_record_table_ptr[i].cpus -
					alloc_cpus;
				this_mem_cnt = (node_record_table_ptr[i].real_memory -
					alloc_mem) - mem_cnt;
			}                       

			debug3("_add_node_set_info %u %s this_cpu_cnt %d"
				" this_mem_cnt %d", 
				job_id, node_record_table_ptr[i].name, 
				this_cpu_cnt, this_mem_cnt);

			if (layout_ptr &&
			    bit_test(job_ptr->details->req_node_bitmap, i)) {
				this_cpu_cnt = MIN(this_cpu_cnt, layout_ptr[ll]);
				debug3("_add_node_set_info %u %s this_cpu_cnt"
				       " limited by task layout %d: %u",
					job_id, node_record_table_ptr[i].name,
					ll, layout_ptr[ll]);
			} else if (layout_ptr) {
				this_cpu_cnt = 0;
			}

			if ((this_cpu_cnt > 0) && (this_mem_cnt > 0)) {
				*node_cnt += 1;
				*cpu_cnt  += this_cpu_cnt;
				
				if (*node_bitmap)
					bit_or(*node_bitmap, node_set_ptr->my_bitmap);
				else {
					*node_bitmap = bit_copy(node_set_ptr->
								my_bitmap);
					if (*node_bitmap == NULL)
						fatal("bit_copy malloc failure");
				}
			}
                }
        }
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
	int super_user = false;
	enum job_state_reason fail_reason;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if ((job_ptr->user_id == 0) || (job_ptr->user_id == getuid()))
		super_user = true;

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
	else if (super_user)
		;	/* ignore any time or node count limits */
	else if ((job_ptr->time_limit != NO_VAL) &&
		 (job_ptr->time_limit > part_ptr->max_time))
		fail_reason = WAIT_PART_TIME_LIMIT;
	else if (((job_ptr->details->max_nodes != 0) &&
	          (job_ptr->details->max_nodes < part_ptr->min_nodes)) ||
	         (job_ptr->details->min_nodes > part_ptr->max_nodes))
		 fail_reason = WAIT_PART_NODE_LIMIT;
	if (fail_reason != WAIT_NO_REASON) {
		job_ptr->state_reason = fail_reason;
		last_job_update = time(NULL);
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
	if (super_user) {
		min_nodes = job_ptr->details->min_nodes;
	} else {
		min_nodes = MAX(job_ptr->details->min_nodes, 
				part_ptr->min_nodes);
	}
	if (job_ptr->details->max_nodes == 0) {
		if (super_user)
			max_nodes = INFINITE;
		else
			max_nodes = part_ptr->max_nodes;
	} else if (super_user)
		max_nodes = job_ptr->details->max_nodes;
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
		error_code = _get_req_features(node_set_ptr, node_set_size,
					       &select_bitmap, job_ptr,
					       part_ptr, min_nodes, max_nodes,
					       req_nodes);
	}

	if (error_code) {
		job_ptr->state_reason = WAIT_RESOURCES;
		if (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			/* Required nodes are down or 
			 * too many nodes requested */
			debug3("JobId=%u not runnable with present config",
			       job_ptr->job_id);
			if (job_ptr->priority != 0)  /* Move to end of queue */
				job_ptr->priority = 1;
			last_job_update = time(NULL);
		} else if (error_code == ESLURM_NODES_BUSY)
			slurm_sched_job_is_pending();
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
	if (select_g_job_begin(job_ptr) != SLURM_SUCCESS) {
		/* Leave job queued, something is hosed */
		error("select_g_job_begin(%u): %m", job_ptr->job_id);
		error_code = ESLURM_NODES_BUSY;
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
	job_ptr->start_time = job_ptr->time_last_active = time(NULL);
	if (job_ptr->time_limit == NO_VAL)
		job_ptr->time_limit = part_ptr->max_time;
	if (job_ptr->time_limit == INFINITE)
		job_ptr->end_time = job_ptr->start_time + 
				    (365 * 24 * 60 * 60); /* secs in year */
	else
		job_ptr->end_time = job_ptr->start_time + 
				    (job_ptr->time_limit * 60);   /* secs */
	if (job_ptr->mail_type & MAIL_JOB_BEGIN)
		mail_job_info(job_ptr, MAIL_JOB_BEGIN);

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

static void _print_feature_list(uint32_t job_id, List feature_list)
{
	ListIterator feat_iter;
	struct feature_record *feat_ptr;
	char *buf = NULL, tmp[16];
	int bracket = 0;

	if (feature_list == NULL) {
		info("Job %u feature list is empty", job_id);
		return;
	}

	feat_iter = list_iterator_create(feature_list);
	while((feat_ptr = (struct feature_record *)list_next(feat_iter))) {
		if (feat_ptr->op_code == FEATURE_OP_XOR) {
			if (bracket == 0)
				xstrcat(buf, "[");
			bracket = 1;
		}
		xstrcat(buf, feat_ptr->name);
		if (feat_ptr->count) {
			snprintf(tmp, sizeof(tmp), "*%u", feat_ptr->count);
			xstrcat(buf, tmp);
		}
		if (bracket && (feat_ptr->op_code != FEATURE_OP_XOR)) {
			xstrcat(buf, "]");
			bracket = 0;
		}
		if (feat_ptr->op_code == FEATURE_OP_AND)
			xstrcat(buf, "&");
		else if ((feat_ptr->op_code == FEATURE_OP_OR) ||
			 (feat_ptr->op_code == FEATURE_OP_XOR))
			xstrcat(buf, "|");
	}
	list_iterator_destroy(feat_iter);
	info("Job %u feature list: %s", job_id, buf);
}

static void _feature_list_delete(void *x)
{
	struct feature_record *feature = (struct feature_record *)x;
	xfree(feature->name);
	xfree(feature);
}

/*
 * _build_feature_list - Translate a job's feature string into a feature_list
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
static int _build_feature_list(struct job_record *job_ptr)
{
	struct job_details *detail_ptr = job_ptr->details;
	char *tmp_requested, *str_ptr1, *str_ptr2, *feature = NULL;
	int bracket = 0, count = 0, i;
	bool have_count = false, have_or = false;
	struct feature_record *feat;

	if (detail_ptr->features == NULL)	/* no constraints */
		return SLURM_SUCCESS;
	if (detail_ptr->feature_list)		/* already processed */
		return SLURM_SUCCESS;

	tmp_requested = xstrdup(detail_ptr->features);
	str_ptr1 = tmp_requested;
	detail_ptr->feature_list = list_create(_feature_list_delete);
	for (i=0; ; i++) {
		if (tmp_requested[i] == '*') {
			tmp_requested[i] = '\0';
			have_count = true;
			count = strtol(&tmp_requested[i+1], &str_ptr2, 10);
			if ((feature == NULL) || (count <= 0)) {
				info("Job %u invalid constraint %s", 
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
			i = str_ptr2 - tmp_requested - 1;
		} else if (tmp_requested[i] == '&') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket != 0)) {
				info("Job %u invalid constraint %s", 
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
			feat = xmalloc(sizeof(struct feature_record));
			feat->name = xstrdup(feature);
			feat->count = count;
			feat->op_code = FEATURE_OP_AND;
			list_append(detail_ptr->feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '|') {
			tmp_requested[i] = '\0';
			have_or = true;
			if (feature == NULL) {
				info("Job %u invalid constraint %s", 
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
			feat = xmalloc(sizeof(struct feature_record));
			feat->name = xstrdup(feature);
			feat->count = count;
			if (bracket)
				feat->op_code = FEATURE_OP_XOR;
			else
				feat->op_code = FEATURE_OP_OR;
			list_append(detail_ptr->feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '[') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || bracket) {
				info("Job %u invalid constraint %s", 
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
			bracket++;
		} else if (tmp_requested[i] == ']') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket == 0)) {
				info("Job %u invalid constraint %s", 
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			}
			bracket = 0;
		} else if (tmp_requested[i] == '\0') {
			if (feature) {
				feat = xmalloc(sizeof(struct feature_record));
				feat->name = xstrdup(feature);
				feat->count = count;
				feat->op_code = FEATURE_OP_END;
				list_append(detail_ptr->feature_list, feat);
			}
			break;
		} else if (feature == NULL) {
			feature = &tmp_requested[i];
		}
	}
	xfree(tmp_requested);
	if (have_count && have_or) {
		info("Job %u invalid constraint (OR with feature count): %s", 
			job_ptr->job_id, detail_ptr->features);
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	_print_feature_list(job_ptr->job_id, detail_ptr->feature_list);
	return SLURM_SUCCESS;
}

/*
 * _build_node_list - identify which nodes could be allocated to a job
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

	if (detail_ptr->features && (detail_ptr->feature_list == NULL)) {
		int error_code = _build_feature_list(job_ptr);
		if (error_code)
			return error_code;
	}
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
		||  (detail_ptr->job_min_memory   > config_ptr->real_memory) 
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

		tmp_feature = _valid_features(job_ptr->details,
					      config_ptr->feature);
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
			&&  (job_con->job_min_memory   <= node_con->real_memory)
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
			&&  (job_con->job_min_memory   <= node_ptr->real_memory)
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
				job_ptr->cpu_count_reps[cpu_inx] = 1;
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
				xfree(job_ptr->alloc_lps);
				xfree(job_ptr->used_lps); 
				job_ptr->alloc_lps_cnt = 0;
				error("Unable to get extra jobinfo "
				      "from JobId=%u", job_ptr->job_id);
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
}

/*
 * _valid_features - determine if the requested features are satisfied by
 *	those available
 * IN details_ptr - job requirement details, includes requested features
 * IN available - available features (on a node)
 * RET NULL if request is not satisfied, otherwise a bitmap indicating 
 *	which mutually exclusive features are satisfied. For example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns a bitmap with
 *	the third bit set. For another example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs1,fs3") returns a bitmap 
 *	with the first and third bits set. This function returns a bitmap
 *	with the first bit set if requirements are satisfied without a 
 *	mutually exclusive feature list.
 */
static bitstr_t *_valid_features(struct job_details *details_ptr, 
				 char *available)
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
	if (available == NULL)			/* no features */
		return result_bits;

	result = 1;				/* assume good for now */
	last_op = FEATURE_OP_AND;
	feat_iter = list_iterator_create(details_ptr->feature_list);
	while ((feat_ptr = (struct feature_record *) list_next(feat_iter))) {
		if (feat_ptr->count)
			found = 1;	/* handle feature counts elsewhere */
		else
			found = _match_feature(feat_ptr->name, available);

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
	kill_job->job_id  = job_ptr->job_id;
	kill_job->job_uid = job_ptr->user_id;
	kill_job->time    = time(NULL);
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
