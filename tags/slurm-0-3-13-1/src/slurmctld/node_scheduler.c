/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs 
 *	Note: there is a global node table (node_record_table_ptr) 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"

#define BUF_SIZE 1024
#define MAX_RETRIES 10

struct node_set {		/* set of nodes with same configuration */
	uint32_t cpus_per_node;
	uint32_t nodes;
	uint32_t weight;
	int feature;
	bitstr_t *my_bitmap;
};

static void _add_node_set_info(struct node_set *node_set_ptr, 
			       bitstr_t ** node_bitmap, 
			       int *node_cnt, int *cpu_cnt);
static int  _build_node_list(struct job_record *job_ptr, 
			     struct node_set **node_set_pptr,
			     int *node_set_size);
static bool _enough_nodes(int avail_nodes, int rem_nodes, int min_nodes,
			  int max_nodes);
static void _filter_nodes_in_set(struct node_set *node_set_ptr,
				 struct job_details *detail_ptr);
static int _match_feature(char *seek, char *available);
static int _nodes_in_sets(bitstr_t *req_bitmap, 
			  struct node_set * node_set_ptr, 
			  int node_set_size);
static void _node_load_bitmaps(bitstr_t * bitmap, bitstr_t ** no_load_bit, 
				bitstr_t ** light_load_bit, 
				bitstr_t ** heavy_load_bit);
static int _pick_best_layout(bitstr_t * bitmap, bitstr_t * req_bitmap,
			     int min_nodes, int max_nodes, int req_cpus,
			     int consecutive);
static int _pick_best_load(bitstr_t * bitmap, bitstr_t * req_bitmap,
			   int min_nodes, int max_nodes, int req_cpus,
			   int consecutive);
static int _pick_best_nodes(struct node_set *node_set_ptr,
			    int node_set_size, bitstr_t ** req_bitmap,
			    uint32_t req_cpus, 
			    uint32_t min_nodes, uint32_t max_nodes,
			    int contiguous, int shared,
			    uint32_t node_lim);
static int _valid_features(char *requested, char *available);


/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 * IN job_ptr - job being allocated resources
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	last_node_update - last update time of node table
 */
void allocate_nodes(struct job_record *job_ptr)
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
int count_cpus(unsigned *bitmap)
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
 * IN timeout - true of job exhausted time limit, send REQUEST_KILL_TIMELIMIT
 *	RPC instead of REQUEST_KILL_JOB
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
void deallocate_nodes(struct job_record *job_ptr, bool timeout)
{
	int i;
	kill_job_msg_t *kill_job;
	agent_arg_t *agent_args;
	int buf_rec_size = 0, down_node_cnt = 0;
	uint16_t base_state, no_resp_flag;

	xassert(job_ptr);
	xassert(job_ptr->details);

	agent_args = xmalloc(sizeof(agent_arg_t));
	if (timeout)
		agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	else
		agent_args->msg_type = REQUEST_KILL_JOB;
	agent_args->retry = 1;
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	last_node_update = time(NULL);
	kill_job->job_id = job_ptr->job_id;
	kill_job->job_uid = job_ptr->user_id;

	for (i = 0; i < node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		base_state = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
		no_resp_flag = node_ptr->node_state & NODE_STATE_NO_RESPOND;
		if (base_state == NODE_STATE_DOWN) {
			/* Issue the KILL RPC, but don't verify response */
			down_node_cnt++;
			bit_clear(job_ptr->node_bitmap, i);
			job_ptr->node_cnt--;
		}
		if ((agent_args->node_count + 1) > buf_rec_size) {
			buf_rec_size += 128;
			xrealloc((agent_args->slurm_addr),
				 (sizeof(struct sockaddr_in) *
				  buf_rec_size));
			xrealloc((agent_args->node_names),
				 (MAX_NAME_LEN * buf_rec_size));
		}
		agent_args->slurm_addr[agent_args->node_count] =
		    node_ptr->slurm_addr;
		strncpy(&agent_args->
			node_names[MAX_NAME_LEN * agent_args->node_count],
			node_ptr->name, MAX_NAME_LEN);
		agent_args->node_count++;
		make_node_comp(node_ptr, job_ptr);
	}

	if ((agent_args->node_count - down_node_cnt) == 0)
		job_ptr->job_state &= (~JOB_COMPLETING);
	if (agent_args->node_count == 0) {
		error("Job %u allocated no nodes to be killed on",
		      job_ptr->job_id);
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
	char *tmp_available, *str_ptr3, *str_ptr4;
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
 * _pick_best_layout - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satify the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN req_bitmap - map of required nodes
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_cpus - count of required processors
 * IN consecutive - allocated nodes must be consecutive if set
 * RET zero on success, EINVAL otherwise
 * globals: node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	_pick_best_layout is called
 */
static int
_pick_best_layout(bitstr_t * bitmap, bitstr_t * req_bitmap,
		    int min_nodes, int max_nodes, 
		    int req_cpus, int consecutive)
{
	int i, index, error_code = EINVAL, sufficient;
	int *consec_nodes;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required 
				 * (in req_bitmap) */
	int consec_index, consec_size;
	int rem_cpus, rem_nodes;	/* remaining resources required */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_location = 0, best_fit_sufficient;

	xassert(bitmap);

	consec_index = 0;
	consec_size  = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = req_cpus;
	if (max_nodes)
		rem_nodes = max_nodes;
	else
		rem_nodes = min_nodes;
	for (index = 0; index < node_record_count; index++) {
		if (bit_test(bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			if (slurmctld_conf.fast_schedule)	
				/* don't bother checking each node */
				i = node_record_table_ptr[index].
				    config_ptr->cpus;
			else
				i = node_record_table_ptr[index].cpus;
			if (req_bitmap && bit_test(req_bitmap, index)) {
				if (consec_req[consec_index] == -1)
					/* first required node in set */
					consec_req[consec_index] = index;
				rem_cpus -= i;
				rem_nodes--;
			} else {	 /* node not required (yet) */
				bit_clear(bitmap, index); 
				consec_cpus[consec_index] += i;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = index - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus,
					 sizeof(int) * consec_size);
				xrealloc(consec_nodes,
					 sizeof(int) * consec_size);
				xrealloc(consec_start,
					 sizeof(int) * consec_size);
				xrealloc(consec_end,
					 sizeof(int) * consec_size);
				xrealloc(consec_req,
					 sizeof(int) * consec_size);
			}
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;

#ifdef EXTREME_DEBUG
	/* don't compile this, slows things down too much */
	debug3("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			debug3
			    ("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
			     node_record_table_ptr[consec_start[i]].name,
			     node_record_table_ptr[consec_end[i]].name,
			     consec_nodes[i], consec_cpus[i],
			     node_record_table_ptr[consec_req[i]].name);
		else
			debug3("start=%s, end=%s, nodes=%d, cpus=%d",
			       node_record_table_ptr[consec_start[i]].name,
			       node_record_table_ptr[consec_end[i]].name,
			       consec_nodes[i], consec_cpus[i]);
	}
#endif

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient = ((consec_nodes[i] >= rem_nodes)
				      && (consec_cpus[i] >= rem_cpus));

			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||	
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||	
			    ((sufficient == 0) && 
			     (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_location = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		if (consecutive && 
		    ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes, 
				     min_nodes, max_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				if (slurmctld_conf.fast_schedule)
					rem_cpus -= node_record_table_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= node_record_table_ptr[i].
							cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				/* if (bit_test(bitmap, i)) 
					continue;  cleared above earlier */
				bit_set(bitmap, i);
				rem_nodes--;
				if (slurmctld_conf.fast_schedule)
					rem_cpus -= node_record_table_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= node_record_table_ptr[i].
							cpus;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				if (slurmctld_conf.fast_schedule)
					rem_cpus -= node_record_table_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= node_record_table_ptr[i].
							cpus;
			}
		}
		if (consecutive || 
		    ((rem_nodes <= 0) && (rem_cpus <= 0))) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_location] = 0;
		consec_nodes[best_fit_location] = 0;
	}
	if (error_code && (rem_cpus <= 0) && 
	    max_nodes  && ((max_nodes - rem_nodes) >= min_nodes))
		error_code = SLURM_SUCCESS;

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/*
 * _pick_best_load - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satify the request.
 * 	"best" is defined as the least loaded nodes
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN req_bitmap - map of required nodes
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_cpus - count of required processors
 * IN consecutive - allocated nodes must be consecutive if set
 * RET zero on success, EINVAL otherwise
 * globals: node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	_pick_best_load is called
 */
static int
_pick_best_load(bitstr_t * bitmap, bitstr_t * req_bitmap,
		int min_nodes, int max_nodes, 
		int req_cpus, int consecutive)
{
	bitstr_t *no_load_bit, *light_load_bit, *heavy_load_bit;
	int error_code;

	_node_load_bitmaps(bitmap, &no_load_bit, &light_load_bit, 
			&heavy_load_bit);

	/* first try to use idle nodes */
	bit_and(bitmap, no_load_bit);
	FREE_NULL_BITMAP(no_load_bit);
	error_code = _pick_best_layout(bitmap, req_bitmap, min_nodes, 
				max_nodes, req_cpus, consecutive);

	/* now try to use idle and lightly loaded nodes */
	if (error_code) {
		bit_or(bitmap, light_load_bit);
		error_code = _pick_best_layout(bitmap, req_bitmap, min_nodes, 
				max_nodes, req_cpus, consecutive);
	} 
	FREE_NULL_BITMAP(light_load_bit);

	/* now try to use all possible nodes */
	if (error_code) {
		bit_or(bitmap, heavy_load_bit);
		error_code = _pick_best_layout(bitmap, req_bitmap, min_nodes, 
				max_nodes, req_cpus, consecutive);
	}
	FREE_NULL_BITMAP(heavy_load_bit);

	return error_code;
}

/* 
 * _node_load_bitmaps - given a bitmap of nodes, create three new bitmaps
 *	indicative of the load on those nodes
 * IN bitmap             - map of nodes to test
 * OUT no_load_bitmap    - nodes from bitmap with no jobs
 * OUT light_load_bitmap - nodes from bitmap with one job
 * OUT heavy_load_bitmap - nodes from bitmap with two or more jobs
 * NOTE: caller must free the created bitmaps
 */
static void
_node_load_bitmaps(bitstr_t * bitmap, bitstr_t ** no_load_bit, 
		bitstr_t ** light_load_bit, bitstr_t ** heavy_load_bit)
{
	int i, load;
	bitoff_t size = bit_size(bitmap);
	bitstr_t *bitmap0 = bit_alloc(size);
	bitstr_t *bitmap1 = bit_alloc(size);
	bitstr_t *bitmap2 = bit_alloc(size);

	if ((bitmap0 == NULL) || (bitmap1 == NULL) || (bitmap2 == NULL))
		fatal("bit_alloc malloc failure");

	for (i = 0; i < size; i++) {
		if (!bit_test(bitmap, i))
			continue;
		load = node_record_table_ptr[i].run_job_cnt;
		if      (load == 0)
			bit_set(bitmap0, i);
		else if (load == 1)
			bit_set(bitmap1, i);
		else
			bit_set(bitmap2, i);
	}
	
	*no_load_bit    = bitmap0;
	*light_load_bit = bitmap1;
	*heavy_load_bit = bitmap2;
}

static bool 
_enough_nodes(int avail_nodes, int rem_nodes, int min_nodes, int max_nodes)
{
	int needed_nodes;

	if (max_nodes)
		needed_nodes = rem_nodes + min_nodes - max_nodes;
	else
		needed_nodes = rem_nodes;

	return(avail_nodes >= needed_nodes);
}


/*
 * _pick_best_nodes - from a weigh order list of all nodes satisfying a 
 *	job's specifications, select the "best" for use
 * IN node_set_ptr - pointer to node specification information
 * IN node_set_size - number of entries in records pointed to by node_set_ptr
 * IN/OUT req_bitmap - pointer to bitmap of specific nodes required by the 
 *	job, could be NULL, returns bitmap of selected nodes, must xfree
 * IN req_cpus - count of cpus required by the job
 * IN min_nodes - minimum count of nodes required by the job
 * IN max_nodes - maximum count of nodes required by the job (0==no limit)
 * IN contiguous - 1 if allocated nodes must be contiguous, 0 otherwise
 * IN shared - set to 1 if nodes may be shared, 0 otherwise
 * IN node_lim - maximum number of nodes permitted for job, 
 *	INFINITE for no limit (partition limit)
 * RET SLURM_SUCCESS on success, 
 *	ESLURM_NODES_BUSY if request can not be satisfied now, 
 *	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE if request can never 
 *	be satisfied , or
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE if the job can not be 
 *	initiated until the parition's configuration changes
 * NOTE: the caller must xfree memory pointed to by req_bitmap
 * Notes: The algorithm is
 *	1) If required node list is specified, determine implicitly required
 *	   processor and node count 
 *	2) Determine how many disjoint required "features" are represented 
 *	   (e.g. "FS1|FS2|FS3")
 *	3) For each feature: find matching node table entries, identify nodes 
 *	   that are up and available (idle or shared) and add them to a bit 
 *	   map
 *	4) If nodes _not_ shared then call _pick_best_layout() to select the 
 *	   "best" of those based upon topology, else call _pick_best_load()
 *	   to pick the "best" nodes in terms of workload
 *	5) If request can't be satified now, execute _pick_best_layout() 
 *	   against the list of nodes that exist in any state (perhaps down 
 *	   or busy) to determine if the request can ever be satified.
 */
static int
_pick_best_nodes(struct node_set *node_set_ptr, int node_set_size,
		 bitstr_t ** req_bitmap, uint32_t req_cpus,
		 uint32_t min_nodes, uint32_t max_nodes,
		 int contiguous, int shared, uint32_t node_lim)
{
	int error_code = SLURM_SUCCESS, i, j, pick_code;
	int total_nodes = 0, total_cpus = 0;	/* total resources configured 
						 * in partition */
	int avail_nodes = 0, avail_cpus = 0;	/* resources available for 
						 * use now */
	bitstr_t *avail_bitmap = NULL, *total_bitmap = NULL;
	int max_feature, min_feature;
	bool runable_ever  = false;	/* Job can ever run */
	bool runable_avail = false;	/* Job can run with available nodes */

	if (node_set_size == 0) {
		info("_pick_best_nodes: empty node set for selection");
		return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	if (*req_bitmap) {	/* specific nodes required */
		/* we have already confirmed that all of these nodes have a
		 * usable configuration and are in the proper partition */
		if (min_nodes != 0)
			total_nodes = bit_set_count(*req_bitmap);
		if (req_cpus != 0)
			total_cpus = count_cpus(*req_bitmap);
		if ((max_nodes != 0) &&
		    (total_nodes > max_nodes)) {
			info("_pick_best_nodes: required nodes exceed limit");
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
		if ((node_lim != INFINITE) && (total_nodes > node_lim)) {
			/* exceed partition node limit */
			return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
		if ((min_nodes <= total_nodes) && 
		    (max_nodes <= min_nodes  ) &&
		    (req_cpus  <= total_cpus )) {
			if (!bit_super_set(*req_bitmap, avail_node_bitmap))
				return ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
			if (shared) {
				if (!bit_super_set(*req_bitmap, 
							share_node_bitmap))
					return ESLURM_NODES_BUSY;
			} else {
				if (!bit_super_set(*req_bitmap, 
							idle_node_bitmap))
					return ESLURM_NODES_BUSY;
			}
			return SLURM_SUCCESS;	/* user can have selected 
						 * nodes, we're done! */
		}
		total_nodes = total_cpus = 0;	/* reinitialize */
	}

	/* identify how many feature sets we have (e.g. "[fs1|fs2|fs3|fs4]" */
	max_feature = min_feature = node_set_ptr[0].feature;
	for (i = 1; i < node_set_size; i++) {
		if (node_set_ptr[i].feature > max_feature)
			max_feature = node_set_ptr[i].feature;
		if (node_set_ptr[i].feature < min_feature)
			min_feature = node_set_ptr[i].feature;
	}

	for (j = min_feature; j <= max_feature; j++) {
		for (i = 0; i < node_set_size; i++) {
			if (node_set_ptr[i].feature != j)
				continue;
			if (!runable_ever)
				_add_node_set_info(&node_set_ptr[i],
						   &total_bitmap, 
						   &total_nodes, &total_cpus);
			bit_and(node_set_ptr[i].my_bitmap, avail_node_bitmap);
			if (shared)
				bit_and(node_set_ptr[i].my_bitmap,
					share_node_bitmap);
			else
				bit_and(node_set_ptr[i].my_bitmap,
					idle_node_bitmap);
			node_set_ptr[i].nodes =
				bit_set_count(node_set_ptr[i].my_bitmap);
			_add_node_set_info(&node_set_ptr[i], &avail_bitmap, 
					   &avail_nodes, &avail_cpus);
			if ((*req_bitmap) &&
			    (!bit_super_set(*req_bitmap, avail_bitmap)))
				continue;
			if ((avail_nodes  < min_nodes) ||
			    ((max_nodes   > min_nodes) && 
			     (avail_nodes < max_nodes)))
				continue;	/* Keep accumulating nodes */
			if (slurmctld_conf.fast_schedule
			&& (avail_cpus   < req_cpus))
				continue;	/* Keep accumulating CPUs */

			if (shared)
				pick_code = _pick_best_load(avail_bitmap, 
							*req_bitmap, min_nodes,
							max_nodes, req_cpus, 
							contiguous);
			else
				pick_code = _pick_best_layout(avail_bitmap, 
							*req_bitmap, min_nodes,
							max_nodes, req_cpus, 
							contiguous);

			if (pick_code == SLURM_SUCCESS) {
				if ((node_lim != INFINITE) && 
				    (bit_set_count(avail_bitmap) > node_lim)) {
					/* end of tests for this feature */
					avail_nodes = 0; 
					break;
				}
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(*req_bitmap);
				*req_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			}
		}

		/* try to get max_nodes now for this feature */
		if ((max_nodes   >  min_nodes) && 
		    (avail_nodes >= min_nodes) &&
		    (avail_nodes <  max_nodes)) {
			pick_code = _pick_best_layout(avail_bitmap, 
							*req_bitmap, min_nodes,
							max_nodes, req_cpus, 
							contiguous);
			if ((pick_code == SLURM_SUCCESS) &&
			    ((node_lim == INFINITE) ||
			     (bit_set_count(avail_bitmap) <= node_lim))) {
				FREE_NULL_BITMAP(total_bitmap);
				FREE_NULL_BITMAP(*req_bitmap);
				*req_bitmap = avail_bitmap;
				return SLURM_SUCCESS;
			}
		}

		/* determine if job could possibly run (if all configured 
		 * nodes available) */
		if ((!runable_ever || !runable_avail) &&
		    (total_nodes >= min_nodes) && 
		    ((*req_bitmap == NULL) ||
		     (bit_super_set(*req_bitmap, total_bitmap)))) {
			if (!runable_avail) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = bit_copy(total_bitmap);
				if (avail_bitmap == NULL)
					fatal("bit_copy malloc failure");
				bit_and(avail_bitmap, avail_node_bitmap);
				pick_code = _pick_best_layout(
							avail_bitmap, 
							*req_bitmap, min_nodes,
							max_nodes, req_cpus, 
							contiguous);
				if (pick_code == SLURM_SUCCESS) {
					runable_ever  = true;
					if ((node_lim == INFINITE) ||
					    (bit_set_count(avail_bitmap) <=
					     node_lim))
						runable_avail = true;
				}
			}
			if (!runable_ever) {
				pick_code = _pick_best_layout(
							total_bitmap, 
							*req_bitmap, min_nodes,
							max_nodes, req_cpus, 
							contiguous);
				if (pick_code == SLURM_SUCCESS)
					runable_ever = true;
			}
		}
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(total_bitmap);
		if (error_code != 0)
			break;
	}

	/* The job is not able to start right now, return a 
	 * value indicating when the job can start */
	if (!runable_ever) {
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info("_pick_best_nodes: job never runnable");
	}
	if (!runable_avail)
		error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (error_code == SLURM_SUCCESS)
		error_code = ESLURM_NODES_BUSY;
	return error_code;
}


/*
 * _add_node_set_info - add info in node_set_ptr to node_bitmap
 * IN node_set_ptr - node set info
 * IN/OUT node_bitmap - add nodes in set to this bitmap
 * IN/OUT node_cnt - add count of nodes in set to this total
 * IN/OUT cpu_cnt - add count of cpus in set to this total
 */
static void
_add_node_set_info(struct node_set *node_set_ptr, 
		   bitstr_t ** node_bitmap, 
		   int *node_cnt, int *cpu_cnt)
{
	if (*node_bitmap)
		bit_or(*node_bitmap, node_set_ptr->my_bitmap);
	else {
		*node_bitmap = bit_copy(node_set_ptr->my_bitmap);
		if (*node_bitmap == NULL)
			fatal("bit_copy malloc failure");
	}
	*node_cnt += node_set_ptr->nodes;
	*cpu_cnt  += node_set_ptr->nodes *
		     node_set_ptr->cpus_per_node;
}

/*
 * select_nodes - select and allocate nodes to a specific job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they  
 *	could be allocated now
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
int select_nodes(struct job_record *job_ptr, bool test_only)
{
	int error_code = SLURM_SUCCESS, i, shared, node_set_size = 0;
	bitstr_t *req_bitmap = NULL;
	struct node_set *node_set_ptr = NULL;
	struct part_record *part_ptr = job_ptr->part_ptr;
	uint32_t min_nodes, max_nodes, part_node_limit;
	int super_user = false;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	/* insure that partition exists and is up */
	if (part_ptr == NULL) {
		part_ptr = find_part_record(job_ptr->partition);
		xassert(part_ptr);
		job_ptr->part_ptr = part_ptr;
		error("partition pointer reset for job %u, part %s",
		      job_ptr->job_id, job_ptr->partition);
	}

	/* Confirm that partition is up and has compatible nodes limits */
	if ((job_ptr->user_id == 0) || (job_ptr->user_id == getuid()))
		super_user = true;
	else if (
	    (part_ptr->state_up == 0) ||
	    ((job_ptr->time_limit != NO_VAL) &&
	     (job_ptr->time_limit > part_ptr->max_time)) ||
	    ((job_ptr->details->max_nodes != 0) &&	/* no node limit */
	     (job_ptr->details->max_nodes < part_ptr->min_nodes)) ||
	    (job_ptr->details->min_nodes > part_ptr->max_nodes)) {
		job_ptr->priority = 1;	/* move to end of queue */
		last_job_update = time(NULL);
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
			info("No nodes satify requirements for JobId=%u",
			     job_ptr->job_id);
			goto cleanup;
		}
		req_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		if (req_bitmap == NULL)
			fatal("bit_copy malloc failure");
	}

	/* pick the nodes providing a best-fit */
	if (super_user) {
		min_nodes = job_ptr->details->min_nodes;
		part_node_limit = INFINITE;
	} else {
		min_nodes = MAX(job_ptr->details->min_nodes, 
				part_ptr->min_nodes);
		part_node_limit = part_ptr->max_nodes;
	}
	if (super_user || (job_ptr->details->max_nodes == 0) ||
	    (part_ptr->max_nodes == INFINITE))
		max_nodes = job_ptr->details->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes, 
				part_ptr->max_nodes);

 	if (part_ptr->shared == SHARED_FORCE)	/* shared=force */
 		shared = 1;
	else if (part_ptr->shared == SHARED_NO)	/* can't share */
		shared = 0;
	else
		shared = job_ptr->details->shared;

	error_code = _pick_best_nodes(node_set_ptr, node_set_size,
				      &req_bitmap, job_ptr->details->num_procs,
				      min_nodes, max_nodes,
				      job_ptr->details->contiguous, 
				      shared, part_node_limit);
	if (error_code) {
		if (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			debug3("JobId=%u not runnable with present config",
			       job_ptr->job_id);
			job_ptr->priority = 1;	/* Move to end of queue */
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

	/* assign the nodes and stage_in the job */
	job_ptr->nodes = bitmap2node_name(req_bitmap);
	job_ptr->node_bitmap = req_bitmap;
	job_ptr->details->shared = shared;
	allocate_nodes(job_ptr);
	build_node_details(job_ptr);
	req_bitmap = NULL;
	job_ptr->job_state = JOB_RUNNING;
	job_ptr->start_time = job_ptr->time_last_active = time(NULL);
	if (job_ptr->time_limit == NO_VAL)
		job_ptr->time_limit = part_ptr->max_time;
	if (job_ptr->time_limit == INFINITE)
		job_ptr->end_time = job_ptr->start_time + 
				    (365 * 24 * 60 * 60); /* secs in year */
	else
		job_ptr->end_time = job_ptr->start_time + 
				    (job_ptr->time_limit * 60);   /* secs */

      cleanup:
	FREE_NULL_BITMAP(req_bitmap);
	if (node_set_ptr) {
		for (i = 0; i < node_set_size; i++)
			FREE_NULL_BITMAP(node_set_ptr[i].my_bitmap);
		xfree(node_set_ptr);
	}
	return error_code;
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
	int tmp_feature, check_node_config, config_filter = 0;
	struct job_details *detail_ptr = job_ptr->details;
	bitstr_t *exc_node_mask = NULL;

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
		tmp_feature = _valid_features(job_ptr->details->features,
					      config_ptr->feature);
		if (tmp_feature == 0)
			continue;

		if ((detail_ptr->min_procs  > config_ptr->cpus       ) || 
		    (detail_ptr->min_memory > config_ptr->real_memory) || 
		    (detail_ptr->min_tmp_disk > config_ptr->tmp_disk))
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
		if (exc_node_mask)
			bit_and(node_set_ptr[node_set_inx].my_bitmap,
				exc_node_mask);
		node_set_ptr[node_set_inx].nodes =
			bit_set_count(node_set_ptr[node_set_inx].my_bitmap);
		if (check_node_config && 
		    (node_set_ptr[node_set_inx].nodes != 0))
			_filter_nodes_in_set(&node_set_ptr[node_set_inx], 
					     detail_ptr);

		if (node_set_ptr[node_set_inx].nodes == 0) {
			FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
			continue;
		}
		node_set_ptr[node_set_inx].cpus_per_node =
		    config_ptr->cpus;
		node_set_ptr[node_set_inx].weight =
		    config_ptr->weight;
		node_set_ptr[node_set_inx].feature = tmp_feature;
		debug("found %d usable nodes from config containing %s",
		     node_set_ptr[node_set_inx].nodes, config_ptr->nodes);

		node_set_inx++;
		xrealloc(node_set_ptr,
			 sizeof(struct node_set) * (node_set_inx + 2));
		node_set_ptr[node_set_inx + 1].my_bitmap = NULL;
	}
	list_iterator_destroy(config_iterator);
	/* eliminate last (incomplete) node_set record */
	FREE_NULL_BITMAP(node_set_ptr[node_set_inx].my_bitmap);
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

	if (slurmctld_conf.fast_schedule) {	/* test config records */
		struct config_record *node_con = NULL;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;
			node_con = node_record_table_ptr[i].config_ptr;
			if ((job_con->min_procs    <= node_con->cpus)        &&
			    (job_con->min_memory   <= node_con->real_memory) &&
			    (job_con->min_tmp_disk <= node_con->tmp_disk))
				continue;

			bit_clear(node_set_ptr->my_bitmap, i);
			if ((--(node_set_ptr->nodes)) == 0)
				break;
		}

	} else {	/* fast_schedule == 0, test individual node records */
		struct node_record   *node_ptr = NULL;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(node_set_ptr->my_bitmap, i) == 0)
				continue;
			node_ptr = &node_record_table_ptr[i];
			if ((job_con->min_procs    <= node_ptr->cpus)        &&
			    (job_con->min_memory   <= node_ptr->real_memory) &&
			    (job_con->min_tmp_disk <= node_ptr->tmp_disk))
				continue;

			bit_clear(node_set_ptr->my_bitmap, i);
			if ((--(node_set_ptr->nodes)) == 0)
				break;
		}
	}
}

/*
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
void build_node_details(struct job_record *job_ptr)
{
	hostlist_t host_list = NULL;
	struct node_record *node_ptr;
	char *this_node_name;
	int node_inx = 0, cpu_inx = -1;

	if ((job_ptr->node_bitmap == NULL) || (job_ptr->nodes == NULL)) {
		/* No nodes allocated, we're done... */
		job_ptr->num_cpu_groups = 0;
		job_ptr->node_cnt = 0;
		job_ptr->cpus_per_node = NULL;
		job_ptr->cpu_count_reps = NULL;
		job_ptr->node_addr = NULL;
		return;
	}

	job_ptr->num_cpu_groups = 0;
	job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap);
	xrealloc(job_ptr->cpus_per_node, 
		(sizeof(uint32_t) * job_ptr->node_cnt));
	xrealloc(job_ptr->cpu_count_reps, 
		(sizeof(uint32_t) * job_ptr->node_cnt));
	xrealloc(job_ptr->node_addr, 
		(sizeof(slurm_addr) * job_ptr->node_cnt));
	/* Use hostlist here to insure ordering of info matches that of srun */
	if ((host_list = hostlist_create(job_ptr->nodes)) == NULL)
		fatal("hostlist_create error for %s: %m", job_ptr->nodes);

	while ((this_node_name = hostlist_shift(host_list))) {
		node_ptr = find_node_record(this_node_name);
		if (node_ptr) {
			int usable_cpus;
			if (slurmctld_conf.fast_schedule)
				usable_cpus = node_ptr->config_ptr->cpus;
			else
				usable_cpus = node_ptr->cpus;
			memcpy(&job_ptr->node_addr[node_inx++],
			       &node_ptr->slurm_addr, sizeof(slurm_addr));
			if ((cpu_inx == -1) ||
			    (job_ptr->cpus_per_node[cpu_inx] !=
			     usable_cpus)) {
				cpu_inx++;
				job_ptr->cpus_per_node[cpu_inx] =
				    usable_cpus;
				job_ptr->cpu_count_reps[cpu_inx] = 1;
			} else
				job_ptr->cpu_count_reps[cpu_inx]++;

		} else {
			error("Invalid node %s in JobId=%u",
			      this_node_name, job_ptr->job_id);
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);
	if (job_ptr->node_cnt != node_inx) {
		error("Node count mismatch for JobId=%u (%u,%u)",
		      job_ptr->job_id, job_ptr->node_cnt, node_inx);
		job_ptr->node_cnt = node_inx;
	}
	job_ptr->num_cpu_groups = cpu_inx + 1;
}

/*
 * _valid_features - determine if the requested features are satisfied by
 *	those available
 * IN requested - requested features (by a job)
 * IN available - available features (on a node)
 * RET 0 if request is not satisfied, otherwise an integer indicating which 
 *	mutually exclusive feature is satisfied. for example
 *	_valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns 3. see the 
 *	slurm administrator and user guides for details. returns 1 if 
 *	requirements are satisfied without mutually exclusive feature list.
 */
static int _valid_features(char *requested, char *available)
{
	char *tmp_requested, *str_ptr1;
	int bracket, found, i, option, position, result;
	int last_op;		/* last operation 0 for or, 1 for and */
	int save_op = 0, save_result = 0;	/* for bracket support */

	if (requested == NULL)
		return 1;	/* no constraints */
	if (available == NULL)
		return 0;	/* no features */

	tmp_requested = xstrdup(requested);
	bracket = option = position = 0;
	str_ptr1 = tmp_requested;	/* start of feature name */
	result = last_op = 1;	/* assume good for now */
	for (i = 0;; i++) {
		if (tmp_requested[i] == (char) NULL) {
			if (strlen(str_ptr1) == 0)
				break;
			found = _match_feature(str_ptr1, available);
			if (last_op == 1)	/* and */
				result &= found;
			else	/* or */
				result |= found;
			break;
		}

		if (tmp_requested[i] == '&') {
			if (bracket != 0) {
				debug("_valid_features: parsing failure on %s",
					requested);
				result = 0;
				break;
			}
			tmp_requested[i] = (char) NULL;
			found = _match_feature(str_ptr1, available);
			if (last_op == 1)	/* and */
				result &= found;
			else	/* or */
				result |= found;
			str_ptr1 = &tmp_requested[i + 1];
			last_op = 1;	/* and */

		} else if (tmp_requested[i] == '|') {
			tmp_requested[i] = (char) NULL;
			found = _match_feature(str_ptr1, available);
			if (bracket != 0) {
				if (found)
					option = position;
				position++;
			}
			if (last_op == 1)	/* and */
				result &= found;
			else	/* or */
				result |= found;
			str_ptr1 = &tmp_requested[i + 1];
			last_op = 0;	/* or */

		} else if (tmp_requested[i] == '[') {
			bracket++;
			position = 1;
			save_op = last_op;
			save_result = result;
			last_op = result = 1;
			str_ptr1 = &tmp_requested[i + 1];

		} else if (tmp_requested[i] == ']') {
			tmp_requested[i] = (char) NULL;
			found = _match_feature(str_ptr1, available);
			if (found)
				option = position;
			result |= found;
			if (save_op == 1)	/* and */
				result &= save_result;
			else	/* or */
				result |= save_result;
			if ((tmp_requested[i + 1] == '&')
			    && (bracket == 1)) {
				last_op = 1;
				str_ptr1 = &tmp_requested[i + 2];
			} else if ((tmp_requested[i + 1] == '|')
				   && (bracket == 1)) {
				last_op = 0;
				str_ptr1 = &tmp_requested[i + 2];
			} else if ((tmp_requested[i + 1] == (char) NULL)
				   && (bracket == 1)) {
				break;
			} else {
				debug("_valid_features: parsing failure on %s",
					requested);
				result = 0;
				break;
			}
			bracket = 0;
		}
	}

	if (position)
		result *= option;
	xfree(tmp_requested);
	return result;
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
	int buf_rec_size = 0;
	hostlist_t kill_hostlist = hostlist_create("");
	char host_str[64];

	xassert(job_ptr);
	xassert(job_ptr->details);

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_KILL_JOB;
	agent_args->retry = 0;
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	kill_job->job_id = job_ptr->job_id;
	kill_job->job_uid = job_ptr->user_id;

	for (i = 0; i < node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];
		if ((job_ptr->node_bitmap == NULL) ||
		    (bit_test(job_ptr->node_bitmap, i) == 0))
			continue;
		if ((node_ptr->node_state & (~NODE_STATE_NO_RESPOND))
				== NODE_STATE_DOWN) {
			/* Consider job already completed */
			bit_clear(job_ptr->node_bitmap, i);
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			if ((--job_ptr->node_cnt) == 0) {
				last_node_update = time(NULL);
				delete_all_step_records(job_ptr);
				job_ptr->job_state &= (~JOB_COMPLETING);
			}
			continue;
		}
		if (node_ptr->node_state & NODE_STATE_NO_RESPOND)
			continue;
		(void) hostlist_push_host(kill_hostlist, node_ptr->name);
		if ((agent_args->node_count + 1) > buf_rec_size) {
			buf_rec_size += 128;
			xrealloc((agent_args->slurm_addr),
				 (sizeof(struct sockaddr_in) * buf_rec_size));
			xrealloc((agent_args->node_names),
				 (MAX_NAME_LEN * buf_rec_size));
		}
		agent_args->slurm_addr[agent_args->node_count] =
		    node_ptr->slurm_addr;
		strncpy(&agent_args->
			node_names[MAX_NAME_LEN * agent_args->node_count],
			node_ptr->name, MAX_NAME_LEN);
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
	info("Resending KILL_JOB request JobId=%u Nodelist=%s",
			job_ptr->job_id, host_str);
	hostlist_destroy(kill_hostlist);

	agent_args->msg_args = kill_job;
	agent_queue_request(agent_args);
	return;
}
