/*****************************************************************************\
 *  eval_nodes.c - Determine order of nodes for job.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC
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

#include "eval_nodes.h"
#include "gres_filter.h"
#include "gres_sched.h"

#include "src/common/xstring.h"

typedef struct node_weight_struct {
	bitstr_t *node_bitmap;	/* bitmap of nodes with this weight */
	uint64_t weight;	/* priority of node for scheduling work on */
} node_weight_type;

/* Find node_weight_type element from list with same weight as node config */
static int _node_weight_find(void *x, void *key)
{
	node_weight_type *nwt = x;
	node_record_t *node_ptr = key;
	if (nwt->weight == node_ptr->sched_weight)
		return 1;
	return 0;
}

/* Free node_weight_type element from list */
static void _node_weight_free(void *x)
{
	node_weight_type *nwt = x;
	FREE_NULL_BITMAP(nwt->node_bitmap);
	xfree(nwt);
}

/* Sort list of node_weight_type reords in order of increasing node weight */
static int _node_weight_sort(void *x, void *y)
{
	node_weight_type *nwt1 = *(node_weight_type **) x;
	node_weight_type *nwt2 = *(node_weight_type **) y;
	if (nwt1->weight < nwt2->weight)
		return -1;
	if (nwt1->weight > nwt2->weight)
		return 1;
	return 0;
}

/*
 * Given a bitmap of available nodes, return a list of node_weight_type
 * records in order of increasing "weight" (priority)
 */
static List _build_node_weight_list(bitstr_t *node_bitmap)
{
	List node_list;
	node_record_t *node_ptr;
	node_weight_type *nwt;

	xassert(node_bitmap);
	/* Build list of node_weight_type records, one per node weight */
	node_list = list_create(_node_weight_free);
	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		nwt = list_find_first(node_list, _node_weight_find, node_ptr);
		if (!nwt) {
			nwt = xmalloc(sizeof(node_weight_type));
			nwt->node_bitmap = bit_alloc(node_record_count);
			nwt->weight = node_ptr->sched_weight;
			list_append(node_list, nwt);
		}
		bit_set(nwt->node_bitmap, i);
	}

	/* Sort the list in order of increasing node weight */
	list_sort(node_list, _node_weight_sort);

	return node_list;
}

/*
 * A variation of _eval_nodes() to select resources using busy nodes first.
 */
static int _eval_nodes_busy(topology_eval_t *topo_eval)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int idle_test;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(topo_eval->node_map);
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	bool all_done = false, gres_per_job;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		topo_eval->max_nodes = MIN(topo_eval->max_nodes,
					   details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);

	i_start = bit_ffs(topo_eval->node_map);
	if (i_start >= 0)
		i_end = bit_fls(topo_eval->node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(topo_eval->node_map, i);
				continue;
			}
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (topo_eval->max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus,
					       min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += topo_eval->avail_cpus;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			topo_eval->max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(topo_eval->node_map, req_map);
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, topo_eval->node_map);
	} else {
		bit_clear_all(topo_eval->node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * Start by using nodes that already have a job running.
	 * Then try to use idle nodes.
	 */
	if (topo_eval->max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = list_next(iter))) {
		for (idle_test = 0; idle_test < 2; idle_test++) {
			for (i = i_start; i <= i_end; i++) {
				if (!avail_res_array[i] ||
				    !avail_res_array[i]->avail_cpus)
					continue;
				/* Node not available or already selected */
				if (!bit_test(nwt->node_bitmap, i) ||
				    bit_test(topo_eval->node_map, i))
					continue;
				if (((idle_test == 0) &&
				     bit_test(idle_node_bitmap, i)) ||
				    ((idle_test == 1) &&
				     !bit_test(idle_node_bitmap, i)))
					continue;
				eval_nodes_select_cores(topo_eval, i,
							min_rem_nodes);
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list,
						&topo_eval->avail_cpus);
				}
				if (topo_eval->avail_cpus == 0)
					continue;
				total_cpus += topo_eval->avail_cpus;
				rem_cpus -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				bit_set(topo_eval->node_map, i);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    gres_sched_test(job_ptr->gres_list_req,
						    job_ptr->job_id)) {
					error_code = SLURM_SUCCESS;
					all_done = true;
					break;
				}
				if (topo_eval->max_nodes == 0) {
					all_done = true;
					break;
				}
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(topo_eval->node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

static int _eval_nodes_consec(topology_eval_t *topo_eval)
{
	int i, j, error_code = SLURM_ERROR;
	int *consec_cpus;	/* how many CPUs we can add from this
				 * consecutive set of nodes */
	List *consec_gres;	/* how many GRES we can add from this
				 * consecutive set of nodes */
	int *consec_nodes;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required
				 * (in req_bitmap) */
	uint64_t *consec_weight; /* node scheduling weight */
	node_record_t *node_ptr = NULL;
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	bool new_best;
	uint64_t best_weight = 0;
	int64_t rem_max_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	bool gres_per_job, required_node;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	uint16_t *avail_cpu_per_node = NULL;

	topo_eval->avail_cpus = 0;

	/* make allocation for 50 sets of consecutive nodes, expand as needed */
	consec_size = 50;
	consec_cpus   = xcalloc(consec_size, sizeof(int));
	consec_nodes  = xcalloc(consec_size, sizeof(int));
	consec_start  = xcalloc(consec_size, sizeof(int));
	consec_end    = xcalloc(consec_size, sizeof(int));
	consec_req    = xcalloc(consec_size, sizeof(int));
	consec_weight = xcalloc(consec_size, sizeof(uint64_t));

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	consec_weight[consec_index] = NO_VAL64;

	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req))) {
		rem_nodes = MIN(min_nodes, req_nodes);
		consec_gres = xcalloc(consec_size, sizeof(List));
	} else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);

	/*
	 * If there are required nodes, first determine the resources they
	 * provide, then select additional resources as needed in next loop
	 */
	if (req_map) {
		int count = 0;
		uint16_t *arbitrary_tpn = job_ptr->details->arbitrary_tpn;
		for (i = 0;
		     ((node_ptr = next_node_bitmap(req_map, &i)) &&
		      (topo_eval->max_nodes > 0));
		     i++) {
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			if (arbitrary_tpn) {
				int req_cpus = arbitrary_tpn[count++];
				if ((details_ptr->cpus_per_task != NO_VAL16) &&
				    (details_ptr->cpus_per_task != 0))
					req_cpus *= details_ptr->cpus_per_task;

				req_cpus = MAX(req_cpus,
					       (int) details_ptr->pn_min_cpus);
				req_cpus = MAX(req_cpus,
					       details_ptr->min_gres_cpu);

				if (topo_eval->avail_cpus < req_cpus) {
					debug("%pJ required node %s needed %d cpus but only has %d",
					      job_ptr, node_ptr->name, req_cpus,
					      topo_eval->avail_cpus);
					goto fini;
				}
				topo_eval->avail_cpus = req_cpus;

				avail_res_array[i]->avail_cpus =
					topo_eval->avail_cpus;
				avail_res_array[i]->avail_res_cnt =
					avail_res_array[i]->avail_cpus +
					avail_res_array[i]->avail_gpus;

			} else
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus == 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
			total_cpus += topo_eval->avail_cpus;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(topo_eval->node_map, req_map);
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
	}

	for (i = 0; next_node(&i); i++) { /* For each node */
		if ((consec_index + 1) >= consec_size) {
			consec_size *= 2;
			xrecalloc(consec_cpus, consec_size, sizeof(int));
			xrecalloc(consec_nodes, consec_size, sizeof(int));
			xrecalloc(consec_start, consec_size, sizeof(int));
			xrecalloc(consec_end, consec_size, sizeof(int));
			xrecalloc(consec_req, consec_size, sizeof(int));
			xrecalloc(consec_weight, consec_size, sizeof(uint64_t));
			if (gres_per_job) {
				xrecalloc(consec_gres,
					  consec_size, sizeof(List));
			}
		}
		if (req_map)
			required_node = bit_test(req_map, i);
		else
			required_node = false;
		if (!bit_test(topo_eval->node_map, i)) {
			node_ptr = NULL;    /* Use as flag, avoid second test */
		} else if (required_node) {
			node_ptr = node_record_table_ptr[i];
		} else {
			node_ptr = node_record_table_ptr[i];
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			if (topo_eval->avail_cpus == 0) {
				bit_clear(topo_eval->node_map, i);
				node_ptr = NULL;
			}
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
		}
		/*
		 * If job requested contiguous nodes,
		 * do not worry about matching node weights
		 */
		if (node_ptr &&
		    !details_ptr->contiguous &&
		    (consec_weight[consec_index] != NO_VAL64) && /* Init value*/
		    (node_ptr->sched_weight != consec_weight[consec_index])) {
			/* End last consecutive set, setup start of next set */
			if (consec_nodes[consec_index] == 0) {
				/* Only required nodes, re-use consec record */
				consec_req[consec_index] = -1;
			} else {
				/* End last set, setup for start of next set */
				consec_end[consec_index]   = i - 1;
				consec_req[++consec_index] = -1;
			}
		}
		if (node_ptr) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = i;
			if (required_node) {
				/*
				 * Required node, resources counters updated
				 * in above loop, leave bitmap set
				 */
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = i;
				}
				continue;
			}

			/* node not selected (yet) */
			bit_clear(topo_eval->node_map, i);
			consec_cpus[consec_index] += topo_eval->avail_cpus;
			consec_nodes[consec_index]++;
			if (gres_per_job) {
				gres_sched_consec(
					&consec_gres[consec_index],
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
			consec_weight[consec_index] = node_ptr->sched_weight;
		} else if (consec_nodes[consec_index] == 0) {
			/* Only required nodes, re-use consec record */
			consec_req[consec_index] = -1;
			consec_weight[consec_index] = NO_VAL64;
		} else {
			/* End last set, setup for start of next set */
			consec_end[consec_index]   = i - 1;
			consec_req[++consec_index] = -1;
			consec_weight[consec_index] = NO_VAL64;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = i - 1;

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (consec_index == 0) {
			info("consec_index is zero");
		}
		for (i = 0; i < consec_index; i++) {
			char *gres_str = NULL, *gres_print = "";
			bitstr_t *host_bitmap;
			char *host_list;
			if (gres_per_job) {
				gres_str = gres_sched_str(consec_gres[i]);
				if (gres_str) {
					xstrcat(gres_str, " ");
					gres_print = gres_str;
				}
			}

			host_bitmap = bit_alloc(node_record_count);
			bit_nset(host_bitmap, consec_start[i], consec_end[i]);
			host_list = bitmap2node_name(host_bitmap);
			info("set:%d consec CPUs:%d nodes:%d:%s %sbegin:%d end:%d required:%d weight:%"PRIu64,
			     i, consec_cpus[i], consec_nodes[i],
			     host_list, gres_print, consec_start[i],
			     consec_end[i], consec_req[i], consec_weight[i]);
			FREE_NULL_BITMAP(host_bitmap);
			xfree(gres_str);
			xfree(host_list);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * accumulate nodes from these sets of consecutive nodes until
	 * sufficient resources have been accumulated
	 */
	while (consec_index && (topo_eval->max_nodes > 0)) {
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
				     eval_nodes_enough_nodes(
					     consec_nodes[i], rem_nodes,
					     min_nodes, req_nodes);
			if (sufficient && gres_per_job) {
				sufficient = gres_sched_sufficient(
					job_ptr->gres_list_req, consec_gres[i]);
			}

			/*
			 * if first possibility OR
			 * contains required nodes OR
			 * lowest node weight
			 */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (consec_weight[i] < best_weight))
				new_best = true;
			else
				new_best = false;
			/*
			 * If equal node weight
			 * first set large enough for request OR
			 * tightest fit (less resource/CPU waste) OR
			 * nothing yet large enough, but this is biggest
			 */
			if (!new_best && (consec_weight[i] == best_weight) &&
			    ((sufficient && (best_fit_sufficient == 0)) ||
			     (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			     (!sufficient &&
			      (consec_cpus[i] > best_fit_cpus))))
				new_best = true;
			/*
			 * if first continuous node set large enough
			 */
			if (!new_best && !best_fit_sufficient &&
			    details_ptr->contiguous && sufficient)
				new_best = true;
			if (new_best) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_index = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
				best_weight = consec_weight[i];
			}

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap) {
				/*
				 * Must wait for all required nodes to be
				 * in a single consecutive block
				 */
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

		if (details_ptr->contiguous && !best_fit_sufficient)
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/*
			 * This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes
			 */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_index]; i++) {
				if ((topo_eval->max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(topo_eval->node_map, i)) {
					/* required node already in set */
					continue;
				}
				if (avail_cpu_per_node[i] == 0)
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[i];

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list,
						&topo_eval->avail_cpus);
				}
				total_cpus += topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				rem_cpus -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((topo_eval->max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(topo_eval->node_map, i))
					continue;
				if (avail_cpu_per_node[i] == 0)
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[i];

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list,
						&topo_eval->avail_cpus);
				}
				total_cpus += topo_eval->avail_cpus;
				rem_cpus -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
			}
		} else {
			/* No required nodes, try best fit single node */
			int best_fit = -1, best_size = 0;
			int first = consec_start[best_fit_index];
			int last  = consec_end[best_fit_index];
			if (rem_nodes <= 1) {
				for (i = first, j = 0; i <= last; i++, j++) {
					if (bit_test(topo_eval->node_map, i) ||
					    !avail_res_array[i])
						continue;
					if (avail_cpu_per_node[i] < rem_cpus)
						continue;
					if (gres_per_job &&
					    !gres_sched_sufficient(
						    job_ptr->gres_list_req,
						    avail_res_array[i]->
						    sock_gres_list)) {
						continue;
					}
					if ((best_fit == -1) ||
					    (avail_cpu_per_node[i] <best_size)){
						best_fit = i;
						best_size =
							avail_cpu_per_node[i];
						if (best_size == rem_cpus)
							break;
					}
				}
				/*
				 * If we found a single node to use,
				 * clear CPU counts for all other nodes
				 */
				if (best_fit != -1) {
					for (i = first; i <= last; i++) {
						if (i == best_fit)
							continue;
						avail_cpu_per_node[i] = 0;
					}
				}
			}

			for (i = first, j = 0; i <= last; i++, j++) {
				if ((topo_eval->max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(topo_eval->node_map, i) ||
				    !avail_res_array[i])
					continue;

				topo_eval->avail_cpus = avail_cpu_per_node[i];
				if (topo_eval->avail_cpus <= 0)
					continue;

				if ((topo_eval->max_nodes == 1) &&
				    (topo_eval->avail_cpus < rem_cpus)) {
					/*
					 * Job can only take one more node and
					 * this one has insufficient CPU
					 */
					continue;
				}

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list,
						&topo_eval->avail_cpus);
				}
				total_cpus += topo_eval->avail_cpus;
				rem_cpus -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
			}
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id) &&
	    eval_nodes_enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

fini:	xfree(avail_cpu_per_node);
	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	xfree(consec_weight);
	if (gres_per_job) {
		for (i = 0; i < consec_size; i++)
			FREE_NULL_LIST(consec_gres[i]);
		xfree(consec_gres);
	}

	return error_code;
}

static int _eval_nodes_lln(topology_eval_t *topo_eval)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(topo_eval->node_map);
	bool all_done = false, gres_per_job;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		topo_eval->max_nodes = MIN(topo_eval->max_nodes,
					   details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);

	i_start = bit_ffs(topo_eval->node_map);
	if (i_start >= 0)
		i_end = bit_fls(topo_eval->node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(topo_eval->node_map, i);
				continue;
			}
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (topo_eval->max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus, min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus <= 0) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += topo_eval->avail_cpus;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			topo_eval->max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(topo_eval->node_map, req_map);
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, topo_eval->node_map);
	} else {
		bit_clear_all(topo_eval->node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * Accumulate nodes from those with highest available CPU count.
	 * Logic is optimized for small node/CPU count allocations.
	 * For larger allocation, use list_sort().
	 */
	if (topo_eval->max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = list_next(iter))) {
		int last_max_cpu_cnt = -1;
		while (!all_done) {
			int max_cpu_idx = -1;
			uint16_t max_cpu_avail_cpus = 0;
			for (i = i_start; i <= i_end; i++) {
				/* Node not available or already selected */
				if (!bit_test(nwt->node_bitmap, i) ||
				    bit_test(topo_eval->node_map, i))
					continue;
				eval_nodes_select_cores(topo_eval, i,
							min_rem_nodes);
				eval_nodes_cpus_to_use(topo_eval, i,
						       rem_max_cpus,
						       min_rem_nodes);
				if (topo_eval->avail_cpus == 0)
					continue;
				/*
				 * Find the "least-loaded" node at the current
				 * node-weight level. This is defined as the
				 * node with the greatest ratio of available to
				 * total cpus. (But shift the divisors around
				 * to avoid any floating-point math.)
				 */
				if ((max_cpu_idx == -1) ||
				    ((avail_res_array[max_cpu_idx]->max_cpus *
				      node_record_table_ptr[i]->cpus) <
				     (avail_res_array[i]->max_cpus *
				      node_record_table_ptr[max_cpu_idx]->
				      cpus))) {
					max_cpu_idx = i;
					max_cpu_avail_cpus =
						topo_eval->avail_cpus;
					if (avail_res_array[max_cpu_idx]->
					    max_cpus == last_max_cpu_cnt)
						break;
				}
			}
			if ((max_cpu_idx == -1) ||
			    (max_cpu_avail_cpus == 0)) {
				/* No more usable nodes left, get next weight */
				break;
			}
			i = max_cpu_idx;
			topo_eval->avail_cpus = max_cpu_avail_cpus;
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&topo_eval->avail_cpus);
			}
			last_max_cpu_cnt = avail_res_array[i]->max_cpus;
			total_cpus += topo_eval->avail_cpus;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			bit_set(topo_eval->node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (topo_eval->max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(topo_eval->node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources at the end of the node
 * list to reduce fragmentation
 */
static int _eval_nodes_serial(topology_eval_t *topo_eval)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(topo_eval->node_map);
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	bool all_done = false, gres_per_job;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		topo_eval->max_nodes = MIN(topo_eval->max_nodes,
					   details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);

	i_start = bit_ffs(topo_eval->node_map);
	if (i_start >= 0)
		i_end = bit_fls(topo_eval->node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(topo_eval->node_map, i);
				continue;
			}
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (topo_eval->max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus, min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += topo_eval->avail_cpus;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			topo_eval->max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(topo_eval->node_map, req_map);
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, topo_eval->node_map);
	} else {
		bit_clear_all(topo_eval->node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	if (topo_eval->max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = list_next(iter))) {
		for (i = i_end;
		     ((i >= i_start) && (topo_eval->max_nodes > 0));
		     i--) {
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus)
				continue;
			/* Node not available or already selected */
			if (!bit_test(nwt->node_bitmap, i) ||
			    bit_test(topo_eval->node_map, i))
				continue;
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus, min_rem_nodes);
			if (topo_eval->avail_cpus == 0)
				continue;
			total_cpus += topo_eval->avail_cpus;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			bit_set(topo_eval->node_map, i);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&topo_eval->avail_cpus);
			}
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (topo_eval->max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(topo_eval->node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;

}

/*
 * A variation of _eval_nodes() to select resources using as many nodes as
 * possible.
 */
static int _eval_nodes_spread(topology_eval_t *topo_eval)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(topo_eval->node_map);
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	bool all_done = false, gres_per_job;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		topo_eval->max_nodes = MIN(topo_eval->max_nodes,
					   details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);

	i_start = bit_ffs(topo_eval->node_map);
	if (i_start >= 0)
		i_end = bit_fls(topo_eval->node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(topo_eval->node_map, i);
				continue;
			}
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (topo_eval->max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus, min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]-> sock_gres_list,
					&topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += topo_eval->avail_cpus;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			topo_eval->max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(topo_eval->node_map, req_map);
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, topo_eval->node_map);
	} else {
		bit_clear_all(topo_eval->node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	if (topo_eval->max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = list_next(iter))) {
		for (i = i_start; i <= i_end; i++) {
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus)
				continue;
			/* Node not available or already selected */
			if (!bit_test(nwt->node_bitmap, i) ||
			    bit_test(topo_eval->node_map, i))
				continue;
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			eval_nodes_cpus_to_use(topo_eval, i,
					       rem_max_cpus, min_rem_nodes);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&topo_eval->avail_cpus);
			}
			if (topo_eval->avail_cpus == 0)
				continue;
			total_cpus += topo_eval->avail_cpus;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			bit_set(topo_eval->node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (topo_eval->max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(topo_eval->node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

extern int eval_nodes(topology_eval_t *topo_eval)
{
	job_details_t *details_ptr = topo_eval->job_ptr->details;
	static bool pack_serial_at_end = false;

	static bool set = false;

	if (!set) {
		if (xstrcasestr(slurm_conf.sched_params, "pack_serial_at_end"))
			pack_serial_at_end = true;
		else
			pack_serial_at_end = false;
		set = true;
	}

	xassert(topo_eval->node_map);
	if (bit_set_count(topo_eval->node_map) < topo_eval->min_nodes)
		return SLURM_ERROR;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, topo_eval->node_map)))
		return SLURM_ERROR;

	if (topo_eval->trump_others && topo_eval->eval_nodes) {
		int rc = topo_eval->eval_nodes(topo_eval);
		if (rc != ESLURM_NOT_SUPPORTED)
			return rc;
	}

	if (topo_eval->job_ptr->bit_flags & SPREAD_JOB) {
		/* Spread the job out over many nodes */
		return _eval_nodes_spread(topo_eval);
	}

	if (topo_eval->prefer_alloc_nodes && !details_ptr->contiguous) {
		/*
		 * Select resource on busy nodes first in order to leave
		 * idle resources free for as long as possible so that longer
		 * running jobs can get more easily started by the backfill
		 * scheduler plugin
		 */
		return _eval_nodes_busy(topo_eval);
	}


	if ((topo_eval->cr_type & CR_LLN) ||
	    (topo_eval->job_ptr->part_ptr &&
	     (topo_eval->job_ptr->part_ptr->flags & PART_FLAG_LLN))) {
		/* Select resource on the Least Loaded Node */
		return _eval_nodes_lln(topo_eval);
	}

	if (pack_serial_at_end &&
	    (details_ptr->min_cpus == 1) && (topo_eval->req_nodes == 1)) {
		/*
		 * Put serial jobs at the end of the available node list
		 * rather than using a best-fit algorithm, which fragments
		 * resources.
		 */
		return _eval_nodes_serial(topo_eval);
	}

	if (topo_eval->eval_nodes) {
		int rc = topo_eval->eval_nodes(topo_eval);
		if (rc != ESLURM_NOT_SUPPORTED)
			return rc;
	}

	return _eval_nodes_consec(topo_eval);
}

extern void eval_nodes_cpus_to_use(topology_eval_t *topo_eval, int node_inx,
				   int64_t rem_max_cpus, int rem_nodes)
{
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	avail_res_t *avail_res = topo_eval->avail_res_array[node_inx];
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all resources on node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= job_mgr_determine_cpus_per_core(details_ptr, node_inx);
	if (topo_eval->cr_type & CR_SOCKET)
		resv_cpus *= node_record_table_ptr[node_inx]->cores;
	rem_max_cpus -= resv_cpus;
	if (topo_eval->avail_cpus > rem_max_cpus) {
		topo_eval->avail_cpus = MAX(rem_max_cpus,
					    (int)details_ptr->pn_min_cpus);
		if (avail_res->gres_min_cpus)
			topo_eval->avail_cpus =
				MAX(topo_eval->avail_cpus,
				    avail_res->gres_min_cpus);
		else
			topo_eval->avail_cpus =
				MAX(topo_eval->avail_cpus,
				    details_ptr->min_gres_cpu);
		/* Round up CPU count to CPU in allocation unit (e.g. core) */
		avail_res->avail_cpus = topo_eval->avail_cpus;
	}
	avail_res->avail_res_cnt = avail_res->avail_cpus +
				   avail_res->avail_gpus;
}

extern void eval_nodes_select_cores(topology_eval_t *topo_eval,
				    int node_inx, int rem_nodes)
{
	bitstr_t **avail_core = topo_eval->avail_core;
	uint16_t *avail_cpus = &topo_eval->avail_cpus;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint16_t cr_type = topo_eval->cr_type;
	bool enforce_binding = topo_eval->enforce_binding;
	bool first_pass = topo_eval->first_pass;
	job_record_t *job_ptr = topo_eval->job_ptr;
	gres_mc_data_t *mc_ptr = topo_eval->mc_ptr;

	uint32_t min_tasks_this_node = 0, max_tasks_this_node = 0;
	uint32_t min_cores_this_node = 0;
	job_details_t *details_ptr = job_ptr->details;
	node_record_t *node_ptr = node_record_table_ptr[node_inx];

	xassert(mc_ptr->cpus_per_task);

	rem_nodes = MIN(rem_nodes, 1);	/* If range of node counts */
	if (mc_ptr->ntasks_per_node) {
		min_tasks_this_node = mc_ptr->ntasks_per_node;
		max_tasks_this_node = mc_ptr->ntasks_per_node;
	} else if (mc_ptr->ntasks_per_board) {
		min_tasks_this_node = mc_ptr->ntasks_per_board;
		max_tasks_this_node = mc_ptr->ntasks_per_board *
				      node_ptr->boards;
	} else if (mc_ptr->ntasks_per_socket) {
		min_tasks_this_node = mc_ptr->ntasks_per_socket;
		max_tasks_this_node = mc_ptr->ntasks_per_socket *
				      node_ptr->tot_sockets;
	} else if (mc_ptr->ntasks_per_core) {
		min_tasks_this_node = mc_ptr->ntasks_per_core;
		max_tasks_this_node = mc_ptr->ntasks_per_core *
				      (node_ptr->tot_cores -
				       node_ptr->core_spec_cnt);
	} else if (details_ptr && details_ptr->ntasks_per_tres &&
		   (details_ptr->ntasks_per_tres != NO_VAL16)) {
		/* Node ranges not allowed with --ntasks-per-gpu */
		if ((details_ptr->min_nodes != NO_VAL) &&
		    (details_ptr->min_nodes != 0) &&
		    (details_ptr->min_nodes == details_ptr->max_nodes)) {
			min_tasks_this_node = details_ptr->num_tasks /
				details_ptr->min_nodes;
			max_tasks_this_node = min_tasks_this_node;
		} else {
			min_tasks_this_node = details_ptr->ntasks_per_tres;
			max_tasks_this_node = details_ptr->num_tasks;
		}
	} else if (details_ptr && (details_ptr->max_nodes == 1)) {
		if ((details_ptr->num_tasks == NO_VAL) ||
		    (details_ptr->num_tasks == 0)) {
			min_tasks_this_node = 1;
			max_tasks_this_node = NO_VAL;
		} else {
			min_tasks_this_node = details_ptr->num_tasks;
			max_tasks_this_node = details_ptr->num_tasks;
		}
	} else if (details_ptr &&
		   ((details_ptr->num_tasks == 1) ||
		    ((details_ptr->num_tasks == details_ptr->min_nodes) &&
		     (details_ptr->num_tasks == details_ptr->max_nodes)))) {
		min_tasks_this_node = 1;
		max_tasks_this_node = 1;
	} else {
		min_tasks_this_node = 1;
		max_tasks_this_node = NO_VAL;
	}
	/* Determine how many tasks can be started on this node */
	if ((!details_ptr || !details_ptr->overcommit)) {
		int alloc_tasks = avail_res_array[node_inx]->avail_cpus /
			      mc_ptr->cpus_per_task;
		if (alloc_tasks < min_tasks_this_node)
			max_tasks_this_node = 0;
		else if ((max_tasks_this_node == NO_VAL) ||
			 (alloc_tasks < max_tasks_this_node))
			max_tasks_this_node = alloc_tasks;
	}

	*avail_cpus = avail_res_array[node_inx]->avail_cpus;
	/*
	 * _allocate_sc() filters available cpus and cores if the job does
	 * not request gres. If the job requests gres, _allocate_sc() defers
	 * filtering cpus and cores so that gres_select_filter_sock_core() can
	 * do it.
	 */
	if (job_ptr->gres_list_req) {
		gres_filter_sock_core(
			job_ptr,
			mc_ptr,
			avail_res_array[node_inx]->sock_gres_list,
			avail_res_array[node_inx]->sock_cnt,
			node_ptr->cores, node_ptr->tpc, avail_cpus,
			&min_tasks_this_node, &max_tasks_this_node,
			&min_cores_this_node,
			rem_nodes, enforce_binding, first_pass,
			avail_core[node_inx],
			node_record_table_ptr[node_inx]->name,
			cr_type);
	}
	if (max_tasks_this_node == 0) {
		*avail_cpus = 0;
	} else if ((slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
		   ((mc_ptr->ntasks_per_core == INFINITE16) ||
		    (mc_ptr->ntasks_per_core == 0)) &&
		   details_ptr && (details_ptr->min_gres_cpu == 0)) {
		*avail_cpus = bit_set_count(avail_core[node_inx]);
	}
	avail_res_array[node_inx]->gres_min_cpus =
		job_mgr_determine_cpus_per_core(job_ptr->details, node_inx) *
		min_cores_this_node;
	avail_res_array[node_inx]->gres_max_tasks = max_tasks_this_node;
}

extern int64_t eval_nodes_get_rem_max_cpus(
	job_details_t *details_ptr, int rem_nodes)
{
	int64_t rem_max_cpus = details_ptr->min_cpus;

	if (details_ptr->max_cpus != NO_VAL)
		rem_max_cpus = details_ptr->max_cpus;
	if (details_ptr->min_gres_cpu)
		rem_max_cpus = MAX(rem_max_cpus,
				   details_ptr->min_gres_cpu * rem_nodes);
	if (details_ptr->min_job_gres_cpu)
		rem_max_cpus = MAX(rem_max_cpus, details_ptr->min_job_gres_cpu);

	return rem_max_cpus;

}

extern int eval_nodes_topo_weight_find(void *x, void *key)
{
	topo_weight_info_t *nw = x;
	topo_weight_info_t *nw_key = key;
	if (nw->weight == nw_key->weight)
		return 1;
	return 0;
}

extern int eval_nodes_topo_node_find(void *x, void *key)
{
	topo_weight_info_t *nw = x;
	bitstr_t *nw_key = key;
	if (bit_overlap_any(nw->node_bitmap, nw_key))
		return 1;
	return 0;
}

extern void eval_nodes_topo_weight_free(void *x)
{
	topo_weight_info_t *nw = x;
	FREE_NULL_BITMAP(nw->node_bitmap);
	xfree(nw);
}

extern int eval_nodes_topo_weight_log(void *x, void *arg)
{
	topo_weight_info_t *nw = x;
	char *node_names = bitmap2node_name(nw->node_bitmap);
	info("Topo:%s weight:%"PRIu64, node_names, nw->weight);
	xfree(node_names);
	return 0;
}

extern int eval_nodes_topo_weight_sort(void *x, void *y)
{
	topo_weight_info_t *nwt1 = *(topo_weight_info_t **) x;
	topo_weight_info_t *nwt2 = *(topo_weight_info_t **) y;
	if (nwt1->weight < nwt2->weight)
		return -1;
	if (nwt1->weight > nwt2->weight)
		return 1;
	return 0;
}

extern bool eval_nodes_enough_nodes(int avail_nodes, int rem_nodes,
				    uint32_t min_nodes, uint32_t req_nodes)
{
	int needed_nodes;

	if (req_nodes > min_nodes)
		needed_nodes = rem_nodes + min_nodes - req_nodes;
	else
		needed_nodes = rem_nodes;

	return (avail_nodes >= needed_nodes);
}
