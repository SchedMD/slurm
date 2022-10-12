/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable
 *  resources policies.
 *****************************************************************************\
 *
 *  The following example below illustrates how four jobs are allocated
 *  across a cluster using when a processor consumable resource approach.
 *
 *  The example cluster is composed of 4 nodes (10 cpus in total):
 *  linux01 (with 2 processors),
 *  linux02 (with 2 processors),
 *  linux03 (with 2 processors), and
 *  linux04 (with 4 processors).
 *
 *  The four jobs are the following:
 *  1. srun -n 4 -N 4  sleep 120 &
 *  2. srun -n 3 -N 3 sleep 120 &
 *  3. srun -n 1 sleep 120 &
 *  4. srun -n 3 sleep 120 &
 *  The user launches them in the same order as listed above.
 *
 *  Using a processor consumable resource approach we get the following
 *  job allocation and scheduling:
 *
 *  The output of squeue shows that we have 3 out of the 4 jobs allocated
 *  and running. This is a 2 running job increase over the default Slurm
 *  approach.
 *
 *  Job 2, Job 3, and Job 4 are now running concurrently on the cluster.
 *
 *  [<snip>]# squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root  PD       0:00      1 (Resources)
 *     2        lsf    sleep     root   R       0:13      4 linux[01-04]
 *     3        lsf    sleep     root   R       0:09      3 linux[01-03]
 *     4        lsf    sleep     root   R       0:05      1 linux04
 *  [<snip>]#
 *
 *  Once Job 2 finishes, Job 5, which was pending, is allocated
 *  available resources and is then running as illustrated below:
 *
 *  [<snip>]# squeue4
 *   JOBID PARTITION    NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3        lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4        lsf    sleep     root   R       1:54      1 linux04
 *     5        lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 *
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 *
 *  [<snip>]#  squeue4
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root   R       1:52      3 xc14n[13-15]
 *  [<snip>]#
 *
 * The advantage of the consumable resource scheduling policy is that
 * the job throughput can increase dramatically.
 *
 *****************************************************************************
 *  Copyright (C) 2005-2008 Hewlett-Packard Development Company, L.P.
 *  Portions Copyright (C) 2010-2015 SchedMD <https://www.schedmd.com>.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear
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

#include <inttypes.h>
#include <time.h>

#include "dist_tasks.h"
#include "job_test.h"
#include "select_cons_res.h"
#include "../cons_common/gres_select_filter.h"

/* Enables module specific debugging */
#define _DEBUG 0

static int _eval_nodes(job_record_t *job_ptr, bitstr_t *node_map,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, avail_res_t **avail_res_array,
		       uint16_t cr_type,
		       bool prefer_alloc_nodes);
static int _eval_nodes_busy(job_record_t *job_ptr, bitstr_t *node_map,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array);
static int _eval_nodes_dfly(job_record_t *job_ptr, bitstr_t *node_map,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array,
			    uint16_t cr_type);
static int _eval_nodes_lln(job_record_t *job_ptr, bitstr_t *node_map,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes, avail_res_t **avail_res_array);
static int _eval_nodes_serial(job_record_t *job_ptr, bitstr_t *node_map,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes,
			      avail_res_t **avail_res_array);
static int _eval_nodes_spread(job_record_t *job_ptr, bitstr_t *node_map,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes,
			      avail_res_t **avail_res_array);
static int _eval_nodes_topo(job_record_t *job_ptr, bitstr_t *node_map,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array,
			    uint16_t cr_type);
static uint32_t _gres_sock_job_test(List job_gres_list, List node_gres_list,
				    bool use_total_gres, bitstr_t *cpu_bitmap,
				    int cpu_start_bit, int cpu_end_bit,
				    uint32_t job_id, char *node_name,
				    uint32_t node_i, uint32_t s_p_n);

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN node_i - Node index
 * IN s_p_n - Sockets per node required by this job or NO_VAL
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
static uint32_t _gres_sock_job_test(List job_gres_list, List node_gres_list,
				    bool use_total_gres, bitstr_t *core_bitmap,
				    int core_start_bit, int core_end_bit,
				    uint32_t job_id, char *node_name,
				    uint32_t node_i, uint32_t s_p_n)
{
	uint32_t core_cnt, sock_cnt, cores_per_sock;
	uint32_t *avail_cores, result_cores;
	bitstr_t **sock_core_bitmap, *other_node_cores;
	int i, j;
	int core_bit_cnt, core_inx, sock_inx, best_socket;

	if ((s_p_n == NO_VAL) || (core_bitmap == NULL) ||
	    ((sock_cnt = node_record_table_ptr[node_i]->tot_sockets) < 2) ||
	    (sock_cnt <= s_p_n)) {
		/* No socket filtering possible, use all sockets */
		return gres_job_test(job_gres_list, node_gres_list,
				     use_total_gres, core_bitmap,
				     core_start_bit, core_end_bit,
				     job_id, node_name, false);
	}

	/* Build local data structures */
	core_cnt = core_end_bit - core_start_bit + 1;
	cores_per_sock = core_cnt / sock_cnt;
	core_bit_cnt = bit_size(core_bitmap);
	sock_core_bitmap = xmalloc(sizeof(bitstr_t *) * sock_cnt);
	for (i = 0; i < sock_cnt; i++)
		sock_core_bitmap[i] = bit_alloc(core_bit_cnt);
	other_node_cores = bit_copy(core_bitmap);
	for (i = core_start_bit, core_inx = 0, sock_inx = 0;
	     i <= core_end_bit; i++) {
		if (core_inx >= cores_per_sock) {
			core_inx = 0;
			sock_inx++;
		}
		if (bit_test(core_bitmap, i)) {
			bit_set(sock_core_bitmap[sock_inx], i);
			bit_clear(other_node_cores, i);
		}
		core_inx++;
	}

	/* Determine how many cores are available from each socket starting
	 * position and moving forward by s_p_n sockets. In order to keep
	 * the overhead/time and complexity reasonable, we only consider
	 * using consecutive sockets. */
	avail_cores = xmalloc(sizeof(uint32_t) * sock_cnt);
	s_p_n = MAX(s_p_n, 1);
	s_p_n = MIN(s_p_n, sock_cnt);
	for (i = 0; i <= (sock_cnt - s_p_n); i++) {
		for (j = 1; j < s_p_n; j++)
			bit_or(sock_core_bitmap[i], sock_core_bitmap[i+j]);
		avail_cores[i] = gres_job_test(job_gres_list,
					       node_gres_list,
					       use_total_gres,
					       sock_core_bitmap[i],
					       core_start_bit,
					       core_end_bit,
					       job_id,
					       node_name,
					       false);
	}

	/* Identify the best sockets */
	best_socket = -1;
	for (i = 0; i <= (sock_cnt - s_p_n); i++) {
		if ((best_socket == -1) ||
		    (avail_cores[i] > avail_cores[best_socket]))
			best_socket = i;
	}
	result_cores = avail_cores[best_socket];
	bit_and(core_bitmap, sock_core_bitmap[best_socket]);
	bit_or(core_bitmap, other_node_cores);

	/* Free local data structures */
	bit_free(other_node_cores);
	for (i = 0; i < sock_cnt; i++)
		bit_free(sock_core_bitmap[i]);
	xfree(sock_core_bitmap);
	xfree(avail_cores);

	return result_cores;
}

static bool _enough_nodes(int avail_nodes, int rem_nodes,
			  uint32_t min_nodes, uint32_t req_nodes)
{
	int needed_nodes;

	if (req_nodes > min_nodes)
		needed_nodes = rem_nodes + min_nodes - req_nodes;
	else
		needed_nodes = rem_nodes;

	return (avail_nodes >= needed_nodes);
}

static void _cpus_to_use(int *avail_cpus, int rem_cpus, int rem_nodes,
			 struct job_details *details_ptr, uint16_t *cpu_cnt,
			 int node_inx, uint16_t cr_type)
{
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all CPUs on this node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= common_cpus_per_core(details_ptr, node_inx);
	if (cr_type & CR_SOCKET)
		resv_cpus *= node_record_table_ptr[node_inx]->cores;
	rem_cpus -= resv_cpus;

	if (*avail_cpus > rem_cpus) {
		*avail_cpus = MAX(rem_cpus, (int)details_ptr->pn_min_cpus);
		/* Round up CPU count to CPU in allocation unit (e.g. core) */
		*cpu_cnt = *avail_cpus;
	}
}

/* this is the heart of the selection process */
static int _eval_nodes(job_record_t *job_ptr, bitstr_t *node_map,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, avail_res_t **avail_res_array,
		       uint16_t cr_type,
		       bool prefer_alloc_nodes)
{
	int i, j, error_code = SLURM_ERROR;
	int *consec_nodes;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required
				 * (in req_bitmap) */
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	int avail_cpus;
	bool required_node;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map    = details_ptr->req_node_bitmap;

	xassert(node_map);

	if (bit_set_count(node_map) < min_nodes)
		return error_code;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, node_map)))
		return error_code;

	if (job_ptr->bit_flags & SPREAD_JOB) {
		/* Spread the job out over many nodes */
		return _eval_nodes_spread(job_ptr, node_map,
					  min_nodes, max_nodes, req_nodes,
					  avail_res_array);
	}

	if (prefer_alloc_nodes && !details_ptr->contiguous) {
		/* Select resource on busy nodes first in order to leave
		 * idle resources free for as long as possible so that longer
		 * running jobs can get more easily started by the backfill
		 * scheduler plugin */
		return _eval_nodes_busy(job_ptr, node_map,
				       min_nodes, max_nodes, req_nodes,
				       avail_res_array);
	}

	if ((cr_type & CR_LLN) ||
	    (job_ptr->part_ptr &&
	     (job_ptr->part_ptr->flags & PART_FLAG_LLN))) {
		/* Select resource on the Least Loaded Node */
		return _eval_nodes_lln(job_ptr, node_map,
				       min_nodes, max_nodes, req_nodes,
				       avail_res_array);
	}

	if (pack_serial_at_end &&
	    (details_ptr->min_cpus == 1) && (req_nodes == 1)) {
		/* Put serial jobs at the end of the available node list
		 * rather than using a best-fit algorithm, which fragments
		 * resources. */
		return _eval_nodes_serial(job_ptr, node_map,
					  min_nodes, max_nodes, req_nodes,
					  avail_res_array);
	}

	if (switch_record_cnt && switch_record_table &&
	    ((topo_optional == false) || job_ptr->req_switch)) {
		/* Perform optimized resource selection based upon topology */
		if (have_dragonfly) {
			return _eval_nodes_dfly(job_ptr, node_map,
						min_nodes, max_nodes, req_nodes,
						avail_res_array, cr_type);
		} else {
			return _eval_nodes_topo(job_ptr, node_map,
						min_nodes, max_nodes, req_nodes,
						avail_res_array, cr_type);
		}
	}

	consec_size = 50;	/* start allocation for 50 sets of
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	for (i = 0; i < node_record_count; i++) {
		if (req_map)
			required_node = bit_test(req_map, i);
		else
			required_node = false;
		if (bit_test(node_map, i)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = i;
			avail_cpus = avail_res_array[i]->avail_cpus;
			if ((max_nodes > 0) && required_node) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = i;
				}
				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &avail_res_array[i]->avail_cpus,
					     i, cr_type);
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decrement max limit */
				max_nodes--;
			} else {	/* node not selected (yet) */
				bit_clear(node_map, i);
				consec_cpus[consec_index] += avail_cpus;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = i - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus, sizeof(int)*consec_size);
				xrealloc(consec_nodes,sizeof(int)*consec_size);
				xrealloc(consec_start,sizeof(int)*consec_size);
				xrealloc(consec_end,  sizeof(int)*consec_size);
				xrealloc(consec_req,  sizeof(int)*consec_size);
			}
			consec_cpus[consec_index]  = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index]   = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = i - 1;

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < consec_index; i++) {
			info("cons_res: eval_nodes:%d consec "
			     "c=%d n=%d b=%d e=%d r=%d",
			     i, consec_cpus[i], consec_nodes[i],
			     consec_start[i], consec_end[i], consec_req[i]);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0)) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;	/* no usable nodes here */

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap &&
			    (consec_req[i] == -1))
				continue;  /* not required nodes */

			sufficient = (consec_cpus[i] >= rem_cpus) &&
				     _enough_nodes(consec_nodes[i], rem_nodes,
						   min_nodes, req_nodes);

			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			    (!sufficient && (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_index = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap) {
				/* Must wait for all required nodes to be
				 * in a single consecutive block */
				int j, other_blocks = 0;
				for (j = (i+1); j < consec_index; j++) {
					if (consec_req[j] != -1) {
						other_blocks = 1;
						break;
					}
				}
				if (other_blocks) {
					best_fit_nodes = 0;
					break;
				}
			}
		}
		if (best_fit_nodes == 0)
			break;

		if (details_ptr->contiguous &&
		    ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes,
				     min_nodes, req_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i)) {
					/* required node already in set */
					continue;
				}
				avail_cpus = avail_res_array[i]->avail_cpus;
				if (avail_cpus <= 0)
					continue;

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &avail_res_array[i]->avail_cpus,
					     i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("1 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;
				avail_cpus = avail_res_array[i]->avail_cpus;
				if (avail_cpus <= 0)
					continue;

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &avail_res_array[i]->avail_cpus,
					     i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("2 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
		} else {
			/* No required nodes, try best fit single node */
			int *cpus_array = NULL, array_len;
			int best_fit = -1, best_size = 0;
			int first = consec_start[best_fit_index];
			int last  = consec_end[best_fit_index];
			if (rem_nodes <= 1) {
				array_len =  last - first + 1;
				cpus_array = xmalloc(sizeof(int) * array_len);
				for (i = first, j = 0; i <= last; i++, j++) {
					if (bit_test(node_map, i))
						continue;
					cpus_array[j] =
						avail_res_array[i]->avail_cpus;
					if (cpus_array[j] < rem_cpus)
						continue;
					if ((best_fit == -1) ||
					    (cpus_array[j] < best_size)) {
						best_fit = j;
						best_size = cpus_array[j];
						if (best_size == rem_cpus)
							break;
					}
				}
				/* If we found a single node to use,
				 * clear cpu counts for all other nodes */
				for (i = first, j = 0;
				     ((i <= last) && (best_fit != -1));
				     i++, j++) {
					if (j != best_fit)
						cpus_array[j] = 0;
				}
			}

			for (i = first, j = 0; i <= last; i++, j++) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;

				if (cpus_array)
					avail_cpus = cpus_array[j];
				else
					avail_cpus =
						avail_res_array[i]->avail_cpus;
				if (avail_cpus <= 0)
					continue;

				if ((max_nodes == 1) &&
				    (avail_cpus < rem_cpus)) {
					/* Job can only take one more node and
					 * this one has insufficient CPU */
					continue;
				}

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &avail_res_array[i]->avail_cpus,
					     i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("3 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
			xfree(cpus_array);
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0)) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

fini:	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources using as many nodes as
 * possible. Based upon _eval_nodes_busy().
 */
static int _eval_nodes_spread(job_record_t *job_ptr, bitstr_t *node_map,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes, avail_res_t **avail_res_array)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus = 0;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = avail_res_array[i]->avail_cpus;
				if ((avail_cpus > 0) && (max_nodes > 0)) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (node_record_count - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		return error_code;
	}

	for (i = i_start; i <= i_end; i++) {
		avail_cpus = avail_res_array[i]->avail_cpus;
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if (max_nodes <= 0)
				break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, node_record_count); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources starting with allocated
 * nodes. Based upon _eval_nodes_lln().
 */
static int _eval_nodes_busy(job_record_t *job_ptr, bitstr_t *node_map,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus = 0;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = avail_res_array[i]->avail_cpus;
				if ((avail_cpus > 0) && (max_nodes > 0)) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (node_record_count - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		return error_code;
	}

	/* Start by using nodes that already have a job running */
	for (i = i_start; i <= i_end; i++) {
		if (bit_test(node_map, i) ||
		    bit_test(idle_node_bitmap, i))
			continue;
		avail_cpus = avail_res_array[i]->avail_cpus;
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if ((max_nodes <= 0) ||
			    ((rem_cpus <= 0) && (rem_nodes <= 0)))
				break;
		}
	}

	/* Now try to use idle nodes */
	for (i = i_start; i <= i_end; i++) {
		if (bit_test(node_map, i) ||
		    !bit_test(idle_node_bitmap, i))
			continue;
		avail_cpus = avail_res_array[i]->avail_cpus;
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if ((max_nodes <= 0) ||
			    ((rem_cpus <= 0) && (rem_nodes <= 0)))
				break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, node_record_count); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources on the least loaded nodes */
static int _eval_nodes_lln(job_record_t *job_ptr, bitstr_t *node_map,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes, avail_res_t **avail_res_array)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	int last_max_cpu_cnt = -1;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = avail_res_array[i]->avail_cpus;
				if (max_nodes > 0) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (node_record_count - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/* Accumulate nodes from those with highest available CPU count.
	 * Logic is optimized for small node/CPU count allocations.
	 * For larger allocation, use list_sort(). */
	while (((rem_cpus > 0) || (rem_nodes > 0)) && (max_nodes > 0)) {
		int max_cpu_idx = -1;
		for (i = i_start; i <= i_end; i++) {
			if (bit_test(node_map, i))
				continue;
			if ((max_cpu_idx == -1) ||
			    (avail_res_array[max_cpu_idx]->max_cpus <
			     avail_res_array[i]->max_cpus)) {
				max_cpu_idx = i;
				if (avail_res_array[max_cpu_idx]->max_cpus ==
				    last_max_cpu_cnt)
					break;
			}
		}
		if ((max_cpu_idx == -1) ||
		    (avail_res_array[max_cpu_idx]->avail_cpus == 0))
			break;
		last_max_cpu_cnt = avail_res_array[max_cpu_idx]->max_cpus;
		avail_cpus = avail_res_array[max_cpu_idx]->avail_cpus;
		if (avail_cpus) {
			rem_cpus -= avail_cpus;
			bit_set(node_map, max_cpu_idx);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
		} else {
			break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, node_record_count); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

fini:	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources at the end of the node
 * list to reduce fragmentation */
static int _eval_nodes_serial(job_record_t *job_ptr, bitstr_t *node_map,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes, avail_res_t **avail_res_array)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = avail_res_array[i]->avail_cpus;
				if (max_nodes > 0) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (node_record_count - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	while (((rem_cpus > 0) || (rem_nodes > 0)) && (max_nodes > 0)) {
		int max_cpu_idx = -1;
		for (i = i_end; i >= i_start; i--) {
			if (bit_test(node_map, i))
				continue;
			if (avail_res_array[i]->avail_cpus) {
				max_cpu_idx = i;
				break;
			}
		}
		if ((max_cpu_idx == -1) ||
		    (avail_res_array[max_cpu_idx]->avail_cpus == 0))
			break;
		avail_cpus = avail_res_array[max_cpu_idx]->avail_cpus;
		if (avail_cpus) {
			rem_cpus -= avail_cpus;
			bit_set(node_map, max_cpu_idx);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
		} else {
			break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, node_record_count); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

fini:	return error_code;
}

/*
 * A network topology aware version of _eval_nodes().
 * NOTE: The logic here is almost identical to that of _job_test_topo()
 *       in select_linear.c. Any bug found here is probably also there.
 */
static int _eval_nodes_topo(job_record_t *job_ptr, bitstr_t *bitmap,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array,
			    uint16_t cr_type)
{
	bitstr_t **switches_bitmap = NULL;	/* nodes on this switch */
	int       *switches_cpu_cnt = NULL;	/* total CPUs on switch */
	int       *switches_node_cnt = NULL;	/* total nodes on switch */
	int       *switches_required = NULL;	/* set if has required node */
	int        leaf_switch_count = 0;   /* Count of leaf node switches used */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int avail_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0, best_fit_sufficient;
	bool sufficient;
	long time_waiting = 0;

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
	}

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_required = xmalloc(sizeof(int)        * switch_record_cnt);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i=0; i<switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		bit_or(avail_nodes_bitmap, switches_bitmap[i]);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, switches_bitmap[i])) {
			switches_required[i] = 1;
		}
	}
	bit_nclear(bitmap, 0, node_record_count - 1);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i=0; i<switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switches_node_cnt[i]) {
				node_names = bitmap2node_name(
						switches_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switches_node_cnt[i], node_names,
			     switches_required[i],
			     switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that specific required nodes are linked together */
	if (req_nodes_bitmap) {
		rc = SLURM_ERROR;
		for (i=0; i<switch_record_cnt; i++) {
			if (bit_super_set(req_nodes_bitmap,
					  switches_bitmap[i])) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
		if ( rc == SLURM_ERROR ) {
			info("%pJ requires nodes that are not linked together",
			     job_ptr);
			goto fini;
		}
	}

	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		first = bit_ffs(req_nodes_bitmap);
		last  = bit_fls(req_nodes_bitmap);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			if (!bit_test(req_nodes_bitmap, i))
				continue;
			if (max_nodes <= 0) {
				info("%pJ requires nodes than allowed",
				     job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			bit_set(bitmap, i);
			bit_clear(avail_nodes_bitmap, i);
			avail_cpus = avail_res_array[i]->avail_cpus;
			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&avail_cpus, rem_cpus, min_rem_nodes,
				     job_ptr->details, &avail_res_array[i]->avail_cpus, i, cr_type);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			for (j=0; j<switch_record_cnt; j++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				/* keep track of the accumulated resources */
				switches_required[j] += avail_cpus;
			}
		}
		/* Compute CPUs already allocated to required nodes */
		if ((job_ptr->details->max_cpus != NO_VAL) &&
		    (total_cpus > job_ptr->details->max_cpus)) {
			info("%pJ can't use required node due to max CPU limit",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Update bitmaps and node counts for higher-level switches */
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(avail_nodes_bitmap, i)) {
					/* cleared from lower level */
					bit_clear(switches_bitmap[j], i);
					switches_node_cnt[j]--;
				} else {
					switches_cpu_cnt[j] += avail_res_array[i]->avail_cpus;
				}
			}
		}
	} else {
		/* No specific required nodes, calculate CPU counts */
		for (j=0; j<switch_record_cnt; j++) {
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				switches_cpu_cnt[j] += avail_res_array[i]->avail_cpus;
			}
		}
	}

	/* Determine lowest level switch satisfying request with best fit
	 * in respect of the specific required nodes if specified
	 */
	best_fit_inx = -1;
	for (j=0; j<switch_record_cnt; j++) {
		if ((switches_cpu_cnt[j] < rem_cpus) ||
		    (!_enough_nodes(switches_node_cnt[j], rem_nodes,
				    min_nodes, req_nodes)))
			continue;
		if ((best_fit_inx != -1) && (req_nodes > min_nodes) &&
		    (switches_node_cnt[best_fit_inx] < req_nodes) &&
		    (switches_node_cnt[best_fit_inx] < switches_node_cnt[j])) {
			/* Try to get up to the requested node count */
			best_fit_inx = -1;
		}

		/*
		 * If first possibility OR
		 * first required switch OR
		 * lower level switch OR
		 * same level but tighter switch (less resource waste) OR
		 * 2 required switches of same level and nodes count
		 * but the latter accumulated cpus amount is bigger than
		 * the former one
		 */
		if ((best_fit_inx == -1) ||
		    (!switches_required[best_fit_inx] && switches_required[j]) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])) ||
		    ((switches_required[best_fit_inx] && switches_required[j]) &&
		     (switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] == switches_node_cnt[best_fit_inx]) &&
		     switches_required[best_fit_inx] < switches_required[j]) ) {
			/* If first possibility OR */
			/* current best switch not required OR */
			/* current best switch required but this */
			/* better one too */
			if ( best_fit_inx == -1 ||
			     !switches_required[best_fit_inx] ||
			     (switches_required[best_fit_inx] &&
			      switches_required[j]) )
				best_fit_inx = j;
		}
	}
	if (best_fit_inx == -1) {
		debug2("%pJ: best_fit topology failure: no switch currently has sufficient resource to satisfy the request",
		       job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	if (!switches_required[best_fit_inx] && req_nodes_bitmap ) {
		debug("%pJ: best_fit topology failure: no switch including requested nodes and satisfying the request found",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	bit_and(avail_nodes_bitmap, switches_bitmap[best_fit_inx]);

	/* Identify usable leafs (within higher switch having best fit) */
	for (j=0; j<switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	/* Use required switches first to minimize the total amount */
	/* of switches */
	/* compute best-switch nodes available array */
	while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
		int *cpus_array = NULL, array_len;
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			sufficient = (switches_cpu_cnt[j] >= rem_cpus) &&
				     _enough_nodes(switches_node_cnt[j],
						   rem_nodes, min_nodes,
						   req_nodes);
			/* If first possibility OR */
			/* first required switch OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest OR */
			/* 2 required switches of same level and cpus count */
			/* but the latter accumulated cpus amount is bigger */
			/* than the former one */
			if ((best_fit_nodes == 0) ||
			    (!switches_required[best_fit_location] &&
			     switches_required[j] ) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient &&
			     (switches_cpu_cnt[j] < best_fit_cpus)) ||
			    ((sufficient == 0) &&
			     (switches_cpu_cnt[j] > best_fit_cpus)) ||
			    (switches_required[best_fit_location] &&
			     switches_required[j] &&
			     switches_cpu_cnt[best_fit_location] ==
			     switches_cpu_cnt[j] &&
			     switches_required[best_fit_location] <
			     switches_required[j]) ) {
				/* If first possibility OR */
				/* current best switch not required OR */
				/* current best switch required but this */
				/* better one too */
				if ((best_fit_nodes == 0) ||
				    !switches_required[best_fit_location] ||
				    (switches_required[best_fit_location] &&
				     switches_required[j])) {
					best_fit_cpus =  switches_cpu_cnt[j];
					best_fit_nodes = switches_node_cnt[j];
					best_fit_location = j;
					best_fit_sufficient = sufficient;
				}
			}
		}
		if (best_fit_nodes == 0)
			break;

		leaf_switch_count++;
		/* Use select nodes from this leaf */
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);

		/* compute best-switch nodes available cpus array */
		array_len = last - first + 1;
		cpus_array = xmalloc(sizeof(int) * array_len);
		for (i=first, j=0; ((i<=last) && (first>=0)); i++, j++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				cpus_array[j] = 0;
			else
				cpus_array[j] = avail_res_array[i]->avail_cpus;
		}

		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count>job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}

		/* accumulate resources from this leaf on a best-fit basis */
		while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
			/* pick a node using a best-fit approach */
			/* if rem_cpus < 0, then we will search for nodes
			 * with lower free cpus nb first
			 */
			int suff = 0, bfsuff = 0, bfloc = 0 , bfsize = 0;
			int ca_bfloc = 0;
			for (i=first, j=0; ((i<=last) && (first>=0));
			     i++, j++) {
				if (cpus_array[j] == 0)
					continue;
				suff = cpus_array[j] >= rem_cpus;
				if ( (bfsize == 0) ||
				     (suff && !bfsuff) ||
				     (suff && (cpus_array[j] < bfsize)) ||
				     (!suff && (cpus_array[j] > bfsize)) ) {
					bfsuff = suff;
					bfloc = i;
					bfsize = cpus_array[j];
					ca_bfloc = j;
				}
			}

			/* no node found, break */
			if (bfsize == 0)
				break;

			/* clear resources of this node from the switch */
			bit_clear(switches_bitmap[best_fit_location],bfloc);
			switches_node_cnt[best_fit_location]--;

			switches_cpu_cnt[best_fit_location] -= bfsize;
			cpus_array[ca_bfloc] = 0;

			/* if this node was already selected in an other */
			/* switch, skip it */
			if (bit_test(bitmap, bfloc)) {
				continue;
			}

			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&bfsize, rem_cpus, min_rem_nodes,
				     job_ptr->details,
				     &avail_res_array[bfloc]->avail_cpus, bfloc,
				     cr_type);

			/* enforce the max_cpus limit */
			if ((job_ptr->details->max_cpus != NO_VAL) &&
			    (total_cpus+bfsize > job_ptr->details->max_cpus)) {
				debug2("5 can't use this node since it "
				       "would put us over the limit");
				continue;
			}

			/* take the node into account */
			bit_set(bitmap, bfloc);
			total_cpus += bfsize;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= bfsize;
		}

		/* free best-switch nodes available cpus array */
		xfree(cpus_array);

		/* mark this switch as processed */
		switches_node_cnt[best_fit_location] = 0;

	}

	if ((rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = SLURM_ERROR;

 fini:	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	if (switches_bitmap) {
		for (i = 0; i < switch_record_cnt; i++) {
			FREE_NULL_BITMAP(switches_bitmap[i]);
		}
	}
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	return rc;
}

/*
 * A dragonfly network topology aware version of _eval_nodes().
 * NOTE: The logic here is almost identical to that of _job_test_topo()
 *       in select_linear.c. Any bug found here is probably also there.
 */
static int _eval_nodes_dfly(job_record_t *job_ptr, bitstr_t *bitmap,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, avail_res_t **avail_res_array,
			    uint16_t cr_type)
{
	bitstr_t  *switch_use_bitmap = NULL;	/* leaf switches used */
	bitstr_t **switches_bitmap = NULL;	/* nodes on this switch */
	int       *switches_cpu_cnt = NULL;	/* total CPUs on switch */
	int       *switches_node_cnt = NULL;	/* total nodes on switch */
	int       *switches_node_use = NULL;	/* nodes from switch used */
	int        leaf_switch_count = 0;	/* Count of leaf node switches used */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int avail_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0;
	long time_waiting = 0;
	int req_switch_cnt = 0;
	int req_switch_id = -1;

	if (job_ptr->req_switch > 1) {
		/* Maximum leaf switch count >1 probably makes no sense */
		info("%s: Resetting %pJ leaf switch count from %u to 0",
		     __func__, job_ptr, job_ptr->req_switch);
		job_ptr->req_switch = 0;
	}
	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
	}

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switch_use_bitmap = bit_alloc(switch_record_cnt);
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_use = xmalloc(sizeof(int)        * switch_record_cnt);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		bit_or(avail_nodes_bitmap, switches_bitmap[i]);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
	}
	bit_nclear(bitmap, 0, node_record_count - 1);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switches_node_cnt[i]) {
				node_names = bitmap2node_name(
						switches_bitmap[i]);
			}
			debug("switch=%s nodes=%u:%s speed:%u",
			      switch_record_table[i].name,
			      switches_node_cnt[i], node_names,
			      switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that specific required nodes are linked together */
	if (req_nodes_bitmap) {
		rc = SLURM_ERROR;
		for (i = 0; i < switch_record_cnt; i++) {
			if (bit_super_set(req_nodes_bitmap,
					  switches_bitmap[i])) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
		if ( rc == SLURM_ERROR ) {
			info("%pJ requires nodes that are not linked together",
			     job_ptr);
			goto fini;
		}
	}

	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		first = bit_ffs(req_nodes_bitmap);
		last  = bit_fls(req_nodes_bitmap);
		for (i = first; ((i <= last) && (first >= 0)); i++) {
			if (!bit_test(req_nodes_bitmap, i))
				continue;
			if (max_nodes <= 0) {
				info("%pJ requires nodes than allowed",
				     job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			bit_set(bitmap, i);
			bit_clear(avail_nodes_bitmap, i);
			avail_cpus = avail_res_array[i]->avail_cpus;
			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&avail_cpus, rem_cpus, min_rem_nodes,
				     job_ptr->details, &avail_res_array[i]->avail_cpus, i, cr_type);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			for (j = 0; j < switch_record_cnt; j++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				switches_node_use[j]++;
				if (switch_record_table[j].level == 0) {
					req_switch_cnt++;
					req_switch_id = j;
				}
			}
		}
		/* Compute CPUs already allocated to required nodes */
		if ((job_ptr->details->max_cpus != NO_VAL) &&
		    (total_cpus > job_ptr->details->max_cpus)) {
			info("%pJ can't use required node due to max CPU limit",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Update bitmaps and node counts for higher-level switches */
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i = first; i <= last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(avail_nodes_bitmap, i)) {
					/* cleared from lower level */
					bit_clear(switches_bitmap[j], i);
					switches_node_cnt[j]--;
				} else {
					switches_cpu_cnt[j] += avail_res_array[i]->avail_cpus;
				}
			}
		}
	} else {
		/* No specific required nodes, calculate CPU counts */
		for (j = 0; j < switch_record_cnt; j++) {
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i = first; i <= last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				switches_cpu_cnt[j] += avail_res_array[i]->avail_cpus;
			}
		}
	}

	/* Determine lowest level switch satisfying request with best fit
	 * in respect of the specific required nodes if specified
	 */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switches_cpu_cnt[j] < rem_cpus) ||
		    (!_enough_nodes(switches_node_cnt[j], rem_nodes,
				    min_nodes, req_nodes)))
			continue;
		if ((best_fit_inx != -1) && (req_nodes > min_nodes) &&
		    (switches_node_cnt[best_fit_inx] < req_nodes) &&
		    (switches_node_cnt[best_fit_inx] < switches_node_cnt[j])) {
			/* Try to get up to the requested node count */
			best_fit_inx = -1;
		}

		if ((req_switch_cnt == 1) && (req_switch_id == j)) {
			best_fit_inx = j;
			break;
		}

		/*
		 * If first possibility OR
		 * lower level switch OR
		 * same level but tighter switch (less resource waste) OR
		 * 2 required switches of same level and nodes count
		 * but the latter accumulated CPUs count is bigger than
		 * the former one
		 */
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx]))){
			best_fit_inx = j;
		}
	}
	if (best_fit_inx == -1) {
		debug2("%pJ: best_fit topology failure: no switch currently has sufficient resource to satisfy the request",
		       job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	bit_and(avail_nodes_bitmap, switches_bitmap[best_fit_inx]);

	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from leafs on a best-fit or round-robin basis */
	while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
		int *cpus_array = NULL, array_len;
		best_fit_cpus = best_fit_nodes = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;

			/* If multiple leaf switches must be used, prefer use
			 * of leaf switches with fewest number of idle CPUs.
			 * This results in more leaf switches being used and
			 * achieves better network bandwidth. */
			if ((best_fit_nodes == 0) ||
			    (switches_node_use[best_fit_location] >
			     switches_node_use[j]) ||
			    ((switches_node_use[best_fit_location] ==
			      switches_node_use[j]) &&
			     (switches_cpu_cnt[j] < best_fit_cpus))) {
				best_fit_cpus =  switches_cpu_cnt[j];
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
			}
		}

		if (best_fit_nodes == 0)
			break;

		/* Use select nodes from this leaf */
		bit_set(switch_use_bitmap, best_fit_location);
		leaf_switch_count = bit_set_count(switch_use_bitmap);
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);

		/* compute best-switch nodes available CPUs array */
		array_len = last - first + 1;
		cpus_array = xmalloc(sizeof(int) * array_len);
		for (i = first, j = 0; ((i <= last) && (first >= 0)); i++, j++){
			if (!bit_test(switches_bitmap
				      [best_fit_location], i))
				cpus_array[j] = 0;
			else
				cpus_array[j] = avail_res_array[i]->avail_cpus;
		}

		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count > job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}

		/* accumulate resources from this leaf on a best-fit basis */
		while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
			/* pick a node using a best-fit approach */
			/* if rem_cpus < 0, then we will search for nodes
			 * with lower free cpus nb first
			 */
			int suff = 0, bfsuff = 0, bfloc = 0 , bfsize = 0;
			int ca_bfloc = 0;
			for (i = first, j = 0; ((i <= last) && (first >= 0));
			     i++, j++) {
				if (cpus_array[j] == 0)
					continue;
				suff = cpus_array[j] >= rem_cpus;
				if ( (bfsize == 0) ||
				     (suff && !bfsuff) ||
				     (suff && (cpus_array[j] < bfsize)) ||
				     (!suff && (cpus_array[j] > bfsize)) ) {
					bfsuff = suff;
					bfloc = i;
					bfsize = cpus_array[j];
					ca_bfloc = j;
				}
			}

			/* no node found, break */
			if (bfsize == 0)
				break;

			/* clear resources of this node from the switch */
			bit_clear(switches_bitmap[best_fit_location], bfloc);
			switches_node_cnt[best_fit_location]--;
			switches_node_use[best_fit_location]++;
			switches_cpu_cnt[best_fit_location] -= bfsize;
			cpus_array[ca_bfloc] = 0;

			/* if this node was already selected in an other */
			/* switch, skip it */
			if (bit_test(bitmap, bfloc)) {
				continue;
			}

			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&bfsize, rem_cpus, min_rem_nodes,
				     job_ptr->details,
				     &avail_res_array[bfloc]->avail_cpus, bfloc,
				     cr_type);

			/* enforce the max_cpus limit */
			if ((job_ptr->details->max_cpus != NO_VAL) &&
			    (total_cpus+bfsize > job_ptr->details->max_cpus)) {
				debug2("5 can't use this node since it "
				       "would put us over the limit");
				continue;
			}

			/* take the node into account */
			bit_set(bitmap, bfloc);
			total_cpus += bfsize;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= bfsize;
			if (job_ptr->req_switch != 1)
				break;
		}

		/* free best-switch nodes available cpus array */
		xfree(cpus_array);
	}

	if ((rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = SLURM_ERROR;

 fini:	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	if (switches_bitmap) {
		for (i = 0; i < switch_record_cnt; i++) {
			FREE_NULL_BITMAP(switches_bitmap[i]);
		}
	}
	FREE_NULL_BITMAP(switch_use_bitmap);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_node_use);

	return rc;
}

/* this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result */
extern int choose_nodes(job_record_t *job_ptr, bitstr_t *node_map,
			bitstr_t **avail_core, uint32_t min_nodes,
			uint32_t max_nodes, uint32_t req_nodes,
			avail_res_t **avail_res_array, uint16_t cr_type,
			bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr)
{
	int i, count, ec, most_cpus = 0, i_first, i_last;
	bitstr_t *origmap, *reqmap = NULL;
	int rem_node_cnt, rem_cpu_cnt = 0;

	if (job_ptr->details->req_node_bitmap)
		reqmap = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	i_first = bit_ffs(node_map);
	if (i_first >= 0)
		i_last = bit_fls(node_map);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_map, i))
			continue;
		/* Make sure we don't say we can use a node exclusively
		 * that is bigger than our max cpu count. */
		if (((job_ptr->details->whole_node == 1) &&
		     (job_ptr->details->max_cpus != NO_VAL) &&
		     (job_ptr->details->max_cpus <
		      avail_res_array[i]->avail_cpus)) ||
		/* OR node has no CPUs */
		    (avail_res_array[i]->avail_cpus < 1)) {
			if (reqmap && bit_test(reqmap, i)) {
				/* can't clear a required node! */
				return SLURM_ERROR;
			}
			bit_clear(node_map, i);
		}
	}

	if (job_ptr->details->num_tasks &&
	    (max_nodes > job_ptr->details->num_tasks))
		max_nodes = MAX(job_ptr->details->num_tasks, min_nodes);

	origmap = bit_copy(node_map);

	ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes, req_nodes,
			 avail_res_array, cr_type, prefer_alloc_nodes);

	if (ec == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(origmap);
		return ec;
	}

	/* This nodeset didn't work. To avoid a possible knapsack problem,
	 * incrementally remove nodes with low cpu counts and retry */
	for (i = 0; i < node_record_count; i++) {
		if (avail_res_array[i]) {
			most_cpus = MAX(most_cpus,
					avail_res_array[i]->avail_cpus);
			rem_cpu_cnt += avail_res_array[i]->avail_cpus;
		}
	}

	for (count = 1; count < most_cpus; count++) {
		bool no_change = true, no_more_remove = false;
		bit_or(node_map, origmap);
		rem_node_cnt = bit_set_count(node_map);
		for (i = 0; i < node_record_count; i++) {
			if (!bit_test(node_map, i))
				continue;
			if ((avail_res_array[i]->avail_cpus > 0) &&
			    (avail_res_array[i]->avail_cpus <= count)) {
				if (reqmap && bit_test(reqmap, i))
					continue;
				rem_cpu_cnt -= avail_res_array[i]->avail_cpus;
				if (rem_cpu_cnt < job_ptr->details->min_cpus) {
					/* Can not remove this node */
					no_more_remove = true;
					break;
				}
				no_change = false;
				bit_clear(node_map, i);
				bit_clear(origmap, i);
				if ((--rem_node_cnt <= min_nodes) ||
				    (rem_cpu_cnt ==
				     job_ptr->details->min_cpus)) {
					/* Can not remove any more nodes */
					no_more_remove = true;
					break;
				}
			}
		}
		if (no_change)
			continue;
		ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes,
				 req_nodes, avail_res_array, cr_type,
				 prefer_alloc_nodes);
		if ((ec == SLURM_SUCCESS) || no_more_remove) {
			FREE_NULL_BITMAP(origmap);
			return ec;
		}
	}
	FREE_NULL_BITMAP(origmap);
	return ec;
}

/*
 * can_job_run_on_node - Given the job requirements, determine which
 *                       resources from the given node (if any) can be
 *                       allocated to this job. Returns the number of
 *                       cpus that can be used by this node and a bitmap
 *                       of available resources for allocation.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not known)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - Determine if job could ever run, ignore allocated memory
 *		      check
 * IN will_run      - Determining when a pending job can start
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * RET Available resources. Call _array() to release memory.
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
extern avail_res_t *can_job_run_on_node(job_record_t *job_ptr,
					bitstr_t **in_core_map,
					const uint32_t node_i,
					uint32_t s_p_n,
					node_use_record_t *node_usage,
					uint16_t cr_type,
					bool test_only, bool will_run,
					bitstr_t **in_part_core_map)
{
	uint16_t cpus;
	avail_res_t *avail_res = NULL;
	uint64_t avail_mem, req_mem;
	uint32_t gres_cores, gres_cpus;
	int core_start_bit, core_end_bit, cpu_alloc_size, i;
	bool disable_binding;
	node_record_t *node_ptr = node_record_table_ptr[node_i];
	List gres_list;
	bitstr_t *core_map = NULL;
	bitstr_t *part_core_map = NULL;

	if (in_core_map && *in_core_map)
		core_map = *in_core_map;

	if (in_part_core_map && *in_part_core_map)
		part_core_map = *in_part_core_map;

	if (((job_ptr->bit_flags & BACKFILL_TEST) == 0) &&
	    !test_only && !will_run && IS_NODE_COMPLETING(node_ptr)) {
		/* Do not allocate more jobs to nodes with completing jobs,
		 * backfill scheduler independently handles completing nodes */
		return NULL;
	}

	core_start_bit = cr_get_coremap_offset(node_i);
	core_end_bit   = cr_get_coremap_offset(node_i+1) - 1;
	if (node_usage[node_i].gres_list)
		gres_list = node_usage[node_i].gres_list;
	else
		gres_list = node_ptr->gres_list;

	disable_binding = job_ptr->bit_flags & GRES_DISABLE_BIND;
	if (!disable_binding) {
		gres_select_filter_cons_res(job_ptr->gres_list_req, gres_list,
					    test_only, core_map, core_start_bit,
					    core_end_bit, node_ptr->name);
	}
	if (disable_binding || (s_p_n == NO_VAL)) {
		gres_cores = gres_job_test(job_ptr->gres_list_req,
					   gres_list, test_only,
					   core_map, core_start_bit,
					   core_end_bit, job_ptr->job_id,
					   node_ptr->name,
					   disable_binding);
	} else {
		gres_cores = _gres_sock_job_test(job_ptr->gres_list_req,
						 gres_list, test_only,
						 core_map, core_start_bit,
						 core_end_bit, job_ptr->job_id,
						 node_ptr->name, node_i, s_p_n);
	}
	if (gres_cores == 0)
		return NULL;

	avail_res = common_allocate(job_ptr, core_map, part_core_map,
				    node_i, &cpu_alloc_size, NULL, cr_type);

	if (avail_res)
		cpus = avail_res->avail_cpus;
	else
		cpus = 0;

	if (cr_type & CR_MEMORY) {
		/* Memory Check: check pn_min_memory to see if:
		 *          - this node has enough memory (MEM_PER_CPU == 0)
		 *          - there are enough free_cores (MEM_PER_CPU == 1)
		 */
		req_mem   = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
		avail_mem = node_ptr->real_memory - node_ptr->mem_spec_limit;
		if (!test_only)
			avail_mem -= node_usage[node_i].alloc_memory;
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			/* memory is per-cpu */
			if (!(job_ptr->bit_flags & BF_WHOLE_NODE_TEST) &&
			    ((req_mem * cpus) > avail_mem) &&
			    (job_ptr->details->whole_node == 1)) {
				cpus = 0;
			} else if (!(cr_type & CR_CPU) &&
				   job_ptr->details->mc_ptr &&
				   (job_ptr->details->mc_ptr->
				    ntasks_per_core == 1) &&
				   job_ptr->details->cpus_per_task == 1) {
				/*
				 * In this scenario, CPUs represents cores and
				 * the cpu/core count will be inflated later on
				 * to include all of the threads on a core. So
				 * we need to compare apples to apples and only
				 * remove 1 cpu/core at a time.
				 */
				while ((cpus > 0) &&
				       ((req_mem *
					 ((uint64_t) cpus *
					  (uint64_t) node_ptr->tpc))
					 > avail_mem))
					cpus -= 1;
			} else {
				while ((req_mem * cpus) > avail_mem) {
					if (cpus >= cpu_alloc_size) {
						cpus -= cpu_alloc_size;
					} else {
						cpus = 0;
						break;
					}
				}
			}

			if (job_ptr->details->cpus_per_task > 1) {
				i = cpus % job_ptr->details->cpus_per_task;
				cpus -= i;
			}
			if (cpus < job_ptr->details->ntasks_per_node)
				cpus = 0;
			/* FIXME: Need to recheck min_cores, etc. here */
		} else {
			/* memory is per node */
			if (req_mem > avail_mem)
				cpus = 0;
		}
	}

	gres_cpus = gres_cores;
	if (gres_cpus != NO_VAL)
		gres_cpus *= node_ptr->tpc;
	if ((gres_cpus < job_ptr->details->ntasks_per_node) ||
	    ((job_ptr->details->cpus_per_task > 1) &&
	     (gres_cpus < job_ptr->details->cpus_per_task)))
		gres_cpus = 0;

	while (gres_cpus < cpus) {
		if ((int) cpus < cpu_alloc_size) {
			debug3("cons_res: cpu_alloc_size > cpus, cannot continue (node: %s)",
			       node_ptr->name);
			cpus = 0;
			break;
		} else {
			cpus -= cpu_alloc_size;
		}
	}

	if (cpus == 0)
		bit_nclear(core_map, core_start_bit, core_end_bit);

	log_flag(SELECT_TYPE, "cons_res: can_job_run_on_node: %u cpus on %s(%d), mem %"PRIu64"/%"PRIu64,
	         cpus, node_ptr->name, node_usage[node_i].node_state,
		 node_usage[node_i].alloc_memory, node_ptr->real_memory);
	avail_res->avail_cpus = cpus;

	return avail_res;
}
