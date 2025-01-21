/*****************************************************************************\
 *  gres_select_filter.c - filters used in the select plugin
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from code previously in common/gres.c
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

#define _GNU_SOURCE

#include <stdlib.h>

#include "src/common/slurm_xlator.h"

#include "gres_select_filter.h"

/* Used to indicate when sock_gres->bits_any_sock should be tested */
#define ANY_SOCK_TEST -1

static int64_t *nonalloc_gres = NULL;

static int *sorting_links_cnt = NULL;

typedef struct {
	job_record_t *job_ptr;
	int job_node_inx;
	int *job_fini;
	int node_inx;
	node_record_t *node_ptr;
	int *rc;
	int rem_node_cnt;
	uint16_t sock_cnt;
	gres_mc_data_t *tres_mc_ptr;
	uint32_t ***tasks_per_node_socket;
	uint32_t *used_cores_on_sock;
	uint32_t used_core_cnt;
	uint32_t used_sock_cnt;
} select_and_set_args_t;

static uint32_t _get_task_cnt_node(uint32_t *tasks_per_socket, int sock_cnt);

static bool *_build_avail_cores_by_sock(bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock)
{
	bool *avail_cores_by_sock = xcalloc(sockets, sizeof(bool));
	int s, c, i, lim = 0;

	lim = bit_size(core_bitmap);
	for (s = 0; s < sockets; s++) {
		for (c = 0; c < cores_per_sock; c++) {
			i = (s * cores_per_sock) + c;
			if (i >= lim)
				goto fini;	/* should never happen */
			if (bit_test(core_bitmap, i)) {
				avail_cores_by_sock[s] = true;
				break;
			}
		}
	}

fini:	return avail_cores_by_sock;
}

/* Set max_node_gres if it is unset or greater than val */
static bool _set_max_node_gres(sock_gres_t *sock_gres, uint64_t val)
{
	if (val &&
	    ((sock_gres->max_node_gres == 0) ||
	     (sock_gres->max_node_gres > val))) {
		sock_gres->max_node_gres = val;
		return true;
	}

	return false;
}

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list - list of sock_gres_t entries built by
 *                     gres_sock_list_create()
 * IN avail_mem - memory available for the job
 * IN max_cpus - maximum CPUs available on this node (limited by specialized
 *               cores and partition CPUs-per-node)
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN core_bitmap - Identification of available cores on this node
 * IN sockets - Count of sockets on the node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN cpus_per_core - Count of CPUs per core on this node
 * IN sock_per_node - sockets requested by job per node or NO_VAL
 * IN task_per_node - tasks requested by job per node or NO_VAL16
 * IN cpus_per_task - Count of CPUs per task
 * IN whole_node - we are requesting the whole node or not
 * OUT avail_gpus - Count of available GPUs on this node
 * OUT near_gpus - Count of GPUs available on sockets with available CPUs
 * RET - 0 if job can use this node, -1 otherwise (some GRES limit prevents use)
 */
extern int gres_select_filter_remove_unusable(list_t *sock_gres_list,
					      uint64_t avail_mem,
					      uint16_t max_cpus,
					      bool enforce_binding,
					      bitstr_t *core_bitmap,
					      uint16_t sockets,
					      uint16_t cores_per_sock,
					      uint16_t cpus_per_core,
					      uint32_t sock_per_node,
					      uint16_t task_per_node,
					      uint16_t cpus_per_task,
					      bool whole_node,
					      uint16_t *avail_gpus,
					      uint16_t *near_gpus)
{
	list_itr_t *sock_gres_iter;
	sock_gres_t *sock_gres;
	bool *avail_cores_by_sock = NULL;
	uint64_t max_gres, mem_per_gres = 0, near_gres_cnt = 0;
	uint16_t cpus_per_gres;
	int s, rc = 0;

	*avail_gpus = 0;
	*near_gpus = 0;
	if (!core_bitmap || !sock_gres_list ||
	    (list_count(sock_gres_list) == 0))
		return rc;

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		uint64_t min_gres = 1, tmp_u64;
		gres_job_state_t *gres_js = NULL;
		xassert(sock_gres->gres_state_job);

		gres_js = sock_gres->gres_state_job->gres_data;
		if (whole_node)
			min_gres = sock_gres->total_cnt;
		else if (gres_js->gres_per_node)
			min_gres = gres_js-> gres_per_node;
		if (gres_js->gres_per_socket) {
			tmp_u64 = gres_js->gres_per_socket;
			if (sock_per_node != NO_VAL)
				tmp_u64 *= sock_per_node;
			min_gres = MAX(min_gres, tmp_u64);
		}
		if (gres_js->gres_per_task) {
			tmp_u64 = gres_js->gres_per_task;
			if (task_per_node != NO_VAL16)
				tmp_u64 *= task_per_node;
			min_gres = MAX(min_gres, tmp_u64);
		}

		if (gres_js->cpus_per_gres)
			cpus_per_gres = gres_js->cpus_per_gres;
		else if (gres_js->ntasks_per_gres &&
			 (gres_js->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = gres_js->ntasks_per_gres *
				cpus_per_task;
		else
			cpus_per_gres = gres_js->def_cpus_per_gres;
		if (cpus_per_gres) {
			max_gres = max_cpus / cpus_per_gres;
			if ((max_gres == 0) ||
			    (gres_js->gres_per_node > max_gres) ||
			    (gres_js->gres_per_task > max_gres) ||
			    (gres_js->gres_per_socket > max_gres)){
				log_flag(SELECT_TYPE, "Insufficient CPUs for any GRES: max_gres (%"PRIu64") = max_cpus (%d) / cpus_per_gres (%d)",
					 max_gres, max_cpus, cpus_per_gres);
				rc = -1;
				break;
			}
		}

		if (gres_js->mem_per_gres)
			mem_per_gres = gres_js->mem_per_gres;
		else
			mem_per_gres = gres_js->def_mem_per_gres;
		if (mem_per_gres && (avail_mem != NO_VAL64)) {
			/* NO_VAL64 is set by caller if CR_MEMORY not in use */
			if (mem_per_gres <= avail_mem) {
				sock_gres->max_node_gres = avail_mem /
					mem_per_gres;
			} else {
				log_flag(SELECT_TYPE, "Insufficient memory for any GRES: mem_per_gres (%"PRIu64") > avail_mem (%"PRIu64")",
					 mem_per_gres, avail_mem);
				rc = -1;
				break;
			}
		}

		if (sock_gres->cnt_by_sock && !avail_cores_by_sock)
			avail_cores_by_sock =_build_avail_cores_by_sock(
				core_bitmap, sockets, cores_per_sock);

		/*
		 * NOTE: gres_per_socket enforcement is performed by
		 * _build_sock_gres_by_topo(), called by
		 * gres_sock_list_create()
		 */
		if (sock_gres->cnt_by_sock && enforce_binding) {
			for (s = 0; s < sockets; s++) {
				if (!avail_cores_by_sock[s]) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
			}
			near_gres_cnt = sock_gres->total_cnt;
		} else if (sock_gres->cnt_by_sock) { /* NO enforce_binding */
			near_gres_cnt = sock_gres->total_cnt;
			for (s = 0; s < sockets; s++) {
				if (!avail_cores_by_sock[s]) {
					near_gres_cnt -=
						sock_gres->cnt_by_sock[s];
				}
			}
		} else {
			near_gres_cnt = sock_gres->total_cnt;
		}

		if (!whole_node) {
			/* If gres_per_node isn't set, try gres_per_job */
			if (!_set_max_node_gres(
				    sock_gres,
				    gres_js->gres_per_node))
				(void)_set_max_node_gres(
					sock_gres,
					gres_js->gres_per_job);
		}

		/* Avoid max_node_gres with ntasks_per_gres and whole node */
		if (cpus_per_gres &&
		    ((gres_js->ntasks_per_gres == NO_VAL16) ||
		     !whole_node)) {
			int cpu_cnt;
			cpu_cnt = bit_set_count(core_bitmap);
			cpu_cnt *= cpus_per_core;
			max_gres = cpu_cnt / cpus_per_gres;
			if (max_gres == 0) {
				log_flag(SELECT_TYPE, "max_gres == 0 == cpu_cnt (%d) / cpus_per_gres (%d)",
					 cpu_cnt, cpus_per_gres);
				rc = -1;
				break;
			} else if ((sock_gres->max_node_gres == 0) ||
				   (sock_gres->max_node_gres > max_gres)) {
				sock_gres->max_node_gres = max_gres;
			}
		}
		if (mem_per_gres && (avail_mem != NO_VAL64)) {
			/* NO_VAL64 is set by caller if CR_MEMORY not in use */
			max_gres = avail_mem / mem_per_gres;
			sock_gres->total_cnt = MIN(sock_gres->total_cnt,
						   max_gres);
		}
		if ((sock_gres->total_cnt < min_gres) ||
		    ((sock_gres->max_node_gres != 0) &&
		     (sock_gres->max_node_gres < min_gres))) {
			log_flag(SELECT_TYPE, "min_gres (%"PRIu64") is > max_node_gres (%"PRIu64") or sock_gres->total_cnt (%"PRIu64")",
				 min_gres, sock_gres->max_node_gres,
				 sock_gres->total_cnt);
			rc = -1;
			break;
		}

		if (gres_id_sharing(sock_gres->gres_state_job->plugin_id)) {
			*avail_gpus += sock_gres->total_cnt;
			if (sock_gres->max_node_gres &&
			    (sock_gres->max_node_gres < near_gres_cnt))
				near_gres_cnt = sock_gres->max_node_gres;
			if (*near_gpus + near_gres_cnt < 0xff)
				*near_gpus += near_gres_cnt;
			else /* overflow */
				*near_gpus = 0xff;
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_by_sock);

	return rc;
}

/* Order GRES scheduling. Schedule GRES requiring specific sockets first */
static void _init_gres_per_bit_select(gres_job_state_t *gres_js, int node_inx)
{
	if (!gres_js->gres_per_bit_select) {
		gres_js->gres_per_bit_select = xcalloc(gres_js->total_node_cnt,
						       sizeof(bitstr_t *));
	}
	gres_js->gres_per_bit_select[node_inx] = xcalloc(
		bit_size(gres_js->gres_bit_select[node_inx]), sizeof(uint64_t));
}

static void _pick_shared_gres_topo(sock_gres_t *sock_gres, bool use_busy_dev,
				   bool use_single_dev, bool no_repeat,
				   int node_inx, int socket_index,
				   uint64_t *gres_needed, int *topo_index)
{
	uint64_t cnt_to_alloc, cnt_avail;
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;
	bitstr_t *sock_bits;

	if (socket_index == ANY_SOCK_TEST) {
		if (sock_gres->bits_any_sock)
			sock_bits = sock_gres->bits_any_sock;
		else
			return;
	} else {
		if (sock_gres->bits_by_sock &&
		    sock_gres->bits_by_sock[socket_index])
			sock_bits = sock_gres->bits_by_sock[socket_index];
		else
			return;
	}

	if (!gres_ns->topo_gres_cnt_alloc || !gres_ns->topo_gres_cnt_avail) {
		error("topo_gres_cnt_alloc or avail not set. This should never happen.");
		return;
	}

	for (int j = 0; (j < gres_ns->topo_cnt) && *gres_needed; j++) {
		int t = topo_index ? topo_index[j] : j;
		if (gres_js->type_id &&
		    (gres_js->type_id != gres_ns->topo_type_id[t]))
			continue;
		if (use_busy_dev && (gres_ns->topo_gres_cnt_alloc[t] == 0))
			continue;
		cnt_avail = gres_ns->topo_gres_cnt_avail[t] -
			gres_js->gres_per_bit_select[node_inx][t];
		if (!sock_gres->use_total_gres) /* Subtract other jobs */
			cnt_avail -= gres_ns->topo_gres_cnt_alloc[t];
		if  (cnt_avail < (use_single_dev ? *gres_needed : 1))
			continue; /* Insufficient resources */
		if (!bit_test(sock_bits, t))
			continue; /* GRES not on this socket */
		if (no_repeat &&
		    bit_test(gres_js->gres_bit_select[node_inx], t))
			continue;

		cnt_to_alloc = MIN(cnt_avail, *gres_needed);

		if (!cnt_to_alloc)
			continue;

		bit_set(gres_js->gres_bit_select[node_inx], t);
		gres_js->gres_cnt_node_select[node_inx] += cnt_to_alloc;
		gres_js->gres_per_bit_select[node_inx][t] += cnt_to_alloc;
		*gres_needed -= cnt_to_alloc;
	}
}

static int _sort_topo_by_avail_cnt(const void *x, const void *y)
{
	return slurm_sort_int64_list_desc(&nonalloc_gres[*(int *) x],
					  &nonalloc_gres[*(int *) y]);
}

static int *_get_sorted_topo_by_least_loaded(gres_node_state_t *gres_ns)
{
	int *topo_index = xcalloc(gres_ns->topo_cnt, sizeof(int));
	nonalloc_gres = xcalloc(gres_ns->topo_cnt, sizeof(int64_t));
	for (int t = 0; t < gres_ns->topo_cnt; t++) {
		topo_index[t] = t;

		if (!gres_ns->topo_gres_cnt_avail[t])
			continue;

		/*
		 * This is to prefer the "least loaded" device, defined
		 * as the ratio of free to total counts. For instance
		 * if we have 4/5 idle on GRES A and 7/10 idle on GRES
		 * B, we want A. (0.7 < 0.8)
		 * Use fixed-point math here to avoid floating-point -
		 * the gres_cnt_avail for the node is the smallest value
		 * that'll make the result distinguishable.
		 */
		nonalloc_gres[t] = gres_ns->topo_gres_cnt_avail[t];
		nonalloc_gres[t] -= gres_ns->topo_gres_cnt_alloc[t];
		nonalloc_gres[t] *= gres_ns->gres_cnt_avail;
		nonalloc_gres[t] /= gres_ns->topo_gres_cnt_avail[t];
	}
	qsort(topo_index, gres_ns->topo_cnt, sizeof(int),
	      _sort_topo_by_avail_cnt);
	xfree(nonalloc_gres);

	return topo_index;
}

static void _pick_shared_gres(uint64_t *gres_needed, uint32_t *used_sock,
			      sock_gres_t *sock_gres, int node_inx,
			      bool use_busy_dev, bool use_single_dev,
			      bool no_repeat, bool enforce_binding,
			      uint32_t job_id, uint32_t total_res_gres,
			      uint32_t *res_gres_per_sock,
			      int sock_with_res_cnt, bool *satisfy_res_gres)
{
	int *topo_index = NULL;
	uint64_t sock_gres_needed;
	uint64_t extra_gres;

	if (total_res_gres && (total_res_gres > *gres_needed)) {
		error("%s: Needed less gres then required by allocated restricted cores (%"PRIu64" < %d). Increasing needed gres for job %u on node %d",
		      __func__, *gres_needed, total_res_gres, job_id, node_inx);
		*gres_needed = total_res_gres;
	}

	if (use_single_dev && total_res_gres && (sock_with_res_cnt > 1)) {
		/*
		 * Have to allocate gres accross more than one socket.
		 * This is assuming one socket per gres configuration line.
		 */
		*satisfy_res_gres = false;
		return;
	}

	if (slurm_conf.select_type_param & LL_SHARED_GRES) {
		topo_index = _get_sorted_topo_by_least_loaded(
			sock_gres->gres_state_node->gres_data);
	}

	/*
	 * First: Try to select sharing gres with affinity to this
	 *	socket with sufficient available shared gres.
	 * Second: Try to select sharing gres with affinity to any
	 *	socket with sufficient available shared gres.
	 * Third: Try to select single sharing gres with sufficient available
	 *	gres.
	 */

	for (int s = 0; (s < sock_gres->sock_cnt) && *gres_needed; s++) {
		if (!used_sock[s])
			continue;
		if (res_gres_per_sock && !use_single_dev) {
			sock_gres_needed = res_gres_per_sock[s];
			if (*gres_needed < total_res_gres)
				extra_gres = *gres_needed - total_res_gres;
			else
				extra_gres = 0;
			sock_gres_needed += extra_gres;

			_pick_shared_gres_topo(sock_gres, use_busy_dev,
					       use_single_dev, no_repeat,
					       node_inx, s, &sock_gres_needed,
					       topo_index);

			if (sock_gres_needed > extra_gres) {
				*satisfy_res_gres = false;
				xfree(topo_index);
				return;
			}

			*gres_needed -= sock_gres_needed;
			total_res_gres -= res_gres_per_sock[s];
		} else {
			if (res_gres_per_sock && !res_gres_per_sock[s])
				continue;
			_pick_shared_gres_topo(sock_gres, use_busy_dev,
					       use_single_dev, no_repeat,
					       node_inx, s, gres_needed,
					       topo_index);
			if (res_gres_per_sock && *gres_needed) {
				*satisfy_res_gres = false;
				xfree(topo_index);
				return;
			}
		}
	}

	if (*gres_needed)
		_pick_shared_gres_topo(sock_gres, use_busy_dev, use_single_dev,
				       no_repeat, node_inx, ANY_SOCK_TEST,
				       gres_needed, topo_index);

	if (*gres_needed && !enforce_binding) {
		for (int s = 0;
		     (s < sock_gres->sock_cnt) && *gres_needed;
		     s++) {
			/* Only test the sockets we ignored before */
			if (used_sock[s])
				continue;
			_pick_shared_gres_topo(sock_gres, use_busy_dev,
					       use_single_dev, no_repeat,
					       node_inx, s, gres_needed,
					       topo_index);
		}
	}

	xfree(topo_index);
}

/*
 * Select GRES topo entries (set GRES bitmap) for this job on this
 *	node based upon per-node shared gres request.
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN/OUT - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 */
static int _set_shared_node_bits(int node_inx, int job_node_inx,
				 sock_gres_t *sock_gres, uint32_t job_id,
				 bool enforce_binding, uint32_t *used_sock,
				 uint32_t total_res_gres,
				 uint32_t *res_gres_per_sock,
				 int sock_with_res_cnt)
{
	int rc = SLURM_SUCCESS;
	gres_job_state_t *gres_js;
	uint64_t gres_needed = 0;
	bool use_busy_dev = gres_use_busy_dev(sock_gres->gres_state_node, 0);
	bool satisfy_res_gres = true;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_needed = gres_js->gres_per_node;

	/* Try to select a single sharing gres with sufficient available gres */
	_pick_shared_gres(&gres_needed, used_sock, sock_gres,
			  node_inx, use_busy_dev, true, false, enforce_binding,
			  job_id, total_res_gres, res_gres_per_sock,
			  sock_with_res_cnt, &satisfy_res_gres);

	if (gres_needed &&
	    (slurm_conf.select_type_param & MULTIPLE_SHARING_GRES_PJ)) {
		/* Select sharing gres with any available shared gres */
		satisfy_res_gres = true;
		_pick_shared_gres(&gres_needed, used_sock, sock_gres, node_inx,
				  use_busy_dev, false, false, enforce_binding,
				  job_id, total_res_gres, res_gres_per_sock,
				  sock_with_res_cnt, &satisfy_res_gres);
	}

	if (!satisfy_res_gres) {
		error("Not enough shared gres on required sockets to satisfy allocated restricted gpu cores for job %u on node %d",
		      job_id, node_inx);
		rc = ESLURM_INVALID_GRES;
	} else if (gres_needed) {
		error("Not enough shared gres available to satisfy gres per node request for job %u on node %d",
		      job_id, node_inx);
		rc = ESLURM_INVALID_GRES;
	}

	return rc;
}

/*
 * Select GRES topo entries (set GRES bitmap) for this job on this
 *	node based upon per-node shared gres request.
 * node_inx IN - global node index
 * sock_gres IN/OUT - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tasks_per_socket IN - Task count for each socket
 */
static int _set_shared_task_bits(int node_inx,
				 sock_gres_t *sock_gres,
				 uint32_t job_id,
				 bool enforce_binding,
				 bool no_task_sharing,
				 uint32_t *tasks_per_socket,
				 uint32_t total_res_gres,
				 uint32_t *res_gres_per_sock,
				 uint32_t sock_with_res_cnt)
{
	gres_job_state_t *gres_js;
	int rc = SLURM_SUCCESS;
	bool use_busy_dev = gres_use_busy_dev(sock_gres->gres_state_node, 0);
	bool satisfy_res_gres = true;

	if (!tasks_per_socket) {
		error("%s: tasks_per_socket unset for job %u on node %s",
		      __func__, job_id, node_record_table_ptr[node_inx]->name);
		return SLURM_ERROR;
	}

	gres_js = sock_gres->gres_state_job->gres_data;

	if (!(slurm_conf.select_type_param & MULTIPLE_SHARING_GRES_PJ)) {
		/* Allow only one sharing gres for the entire job */
		uint64_t gres_needed = gres_js->gres_per_task *
			_get_task_cnt_node(tasks_per_socket,
					   sock_gres->sock_cnt);
		if (no_task_sharing)
			error("one-task-per-sharing requires MULTIPLE_SHARING_GRES_PJ to be set. Ignoring flag for job %u on node %d",
			      job_id, node_inx);

		_pick_shared_gres(&gres_needed, tasks_per_socket, sock_gres,
				  node_inx, use_busy_dev, true, false,
				  enforce_binding, job_id, total_res_gres,
				  res_gres_per_sock, sock_with_res_cnt,
				  &satisfy_res_gres);
		if (gres_needed) {
			error("Not enough shared gres available on one sharing gres to satisfy gres per task request for job %u on node %d",
			      job_id, node_inx);
			rc = ESLURM_INVALID_GRES;
		}
	} else {
		/* Allow only one sharing gres per task */
		uint32_t *used_sock = xcalloc(sock_gres->sock_cnt, sizeof(int));
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			used_sock[s] = 1;
			uint32_t sock_res_gres = 0;
			uint32_t used_res = 0;
			if (res_gres_per_sock && res_gres_per_sock[s]) {
				sock_res_gres = res_gres_per_sock[s];

				if ((tasks_per_socket[s] *
				     gres_js->gres_per_task) < sock_res_gres) {
					error("Requested too few gres to satisfy allocated restricted cores for job %u on node %d",
					      job_id, node_inx);
					rc = ESLURM_INVALID_GRES;
					break;
				}
			}

			for (int i = 0; i < tasks_per_socket[s]; i++) {
				uint64_t gres_needed = gres_js->gres_per_task;
				uint32_t this_task_res_gres = 0;
				if (sock_res_gres)
					this_task_res_gres =
						MIN(gres_needed,
						    (sock_res_gres - used_res));
				_pick_shared_gres(&gres_needed, used_sock,
						  sock_gres, node_inx,
						  use_busy_dev, true,
						  no_task_sharing,
						  enforce_binding, job_id,
						  this_task_res_gres,
						  res_gres_per_sock, 1,
						  &satisfy_res_gres);
				if (sock_res_gres)
					used_res += this_task_res_gres;
				if (!satisfy_res_gres) {
					error("Not enough shared gres on required sockets to satisfy allocated restricted gpu cores for job %u on node %d",
					      job_id, node_inx);
					rc = ESLURM_INVALID_GRES;
				} else if (gres_needed) {
					error("Not enough shared gres available to satisfy gres per task request for job %u on node %d (%"PRIu64"/%"PRIu64" still needed)",
					      job_id, node_inx, gres_needed,
					      gres_js->gres_per_task);
					rc = ESLURM_INVALID_GRES;
					break;
				}
			}
			used_sock[s] = 0;
		}
		xfree(used_sock);
	}
	return rc;
}

static int _compare_gres_by_links(const void *x, const void *y)
{
	return sorting_links_cnt[*(int *) x] - sorting_links_cnt[*(int *) y];
}

static void _update_and_sort_by_links(int *sorted_gres, int *links_cnt,
				      int gres_inx, int gres_cnt,
				      gres_node_state_t *gres_ns)
{
	/* Add links for the gres just selected */
	for (int l = 0; (l < gres_cnt); l++) {
		if ((l == gres_inx) ||
		    bit_test(gres_ns->gres_bit_alloc, l))
			continue;
		links_cnt[l] += gres_ns->links_cnt[gres_inx][l];
	}

	/* Sort gres by most linked to all previously selected gres */
	sorting_links_cnt = links_cnt;
	qsort(sorted_gres, gres_cnt, sizeof(int), _compare_gres_by_links);
	sorting_links_cnt = NULL;
}

static uint64_t _pick_gres_topo(sock_gres_t *sock_gres, int gres_needed,
				int node_inx, int socket_index,
				int *sorted_gres, int *links_cnt)
{
	uint64_t gres_still_needed, gres_cnt;
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;
	bitstr_t *sock_bits;

	if (socket_index == ANY_SOCK_TEST) {
		if (sock_gres->bits_any_sock)
			sock_bits = sock_gres->bits_any_sock;
		else
			return 0;
	} else {
		if (sock_gres->bits_by_sock &&
		    sock_gres->bits_by_sock[socket_index])
			sock_bits = sock_gres->bits_by_sock[socket_index];
		else
			return 0;
	}

	gres_still_needed = gres_needed;
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);

	for (int i = 0; i < gres_cnt && gres_still_needed; i++) {
		int g = sorted_gres ? sorted_gres[i] : i;
		if (!bit_test(sock_bits, g))
			continue; /* GRES not on this socket */
		if (bit_test(gres_js->gres_bit_select[node_inx], g))
			continue; /* Already selected for this job */
		if (!sock_gres->use_total_gres &&
		    bit_test(gres_ns->gres_bit_alloc, g))
			continue; /* Already allocated to other jobs */
		bit_set(gres_js->gres_bit_select[node_inx], g);
		gres_js->gres_cnt_node_select[node_inx]++;
		gres_still_needed--;
		if (links_cnt && sorted_gres) {
			i = 0; /* Start over on for updated sorted_gres */
			_update_and_sort_by_links(sorted_gres, links_cnt, g,
						  gres_cnt, gres_ns);
		}
	}
	return gres_needed - gres_still_needed;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-socket resource specification
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_sock_bits(int node_inx, int job_node_inx,
			   sock_gres_t *sock_gres, uint32_t job_id,
			   gres_mc_data_t *tres_mc_ptr,
			   uint32_t *used_cores_on_sock,
			   uint32_t *res_gres_per_sock, uint32_t total_res_gres,
			   uint32_t used_sock_cnt, bool enforce_binding)
{
	int gres_cnt;
	uint16_t sock_cnt = 0;
	int g, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int *links_cnt = NULL, *sorted_gres = NULL;
	uint64_t gres_needed, gres_this_sock;
	uint32_t *used_sock = used_cores_on_sock;
	bool allocated_array_copy = false;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	sock_cnt = sock_gres->sock_cnt;
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);

	if (tres_mc_ptr && tres_mc_ptr->sockets_per_node     &&
	    (tres_mc_ptr->sockets_per_node != used_sock_cnt) &&
	    gres_ns->gres_bit_alloc && sock_gres->bits_by_sock) {
		used_sock = xcalloc(sock_gres->sock_cnt, sizeof(int));
		memcpy(used_sock, used_cores_on_sock,
		       sock_gres->sock_cnt * sizeof(int));
		allocated_array_copy = true;
		if (tres_mc_ptr->sockets_per_node > used_sock_cnt) {
			/* Somehow we have too few sockets in job allocation */
			error("%s: Inconsistent requested/allocated socket count (%d > %d) for job %u on node %d",
			      __func__, tres_mc_ptr->sockets_per_node,
			      used_sock_cnt, job_id, node_inx);
			for (s = 0; s < sock_cnt; s++) {
				if (used_sock[s] || !sock_gres->bits_by_sock[s])
					continue;
				/* Determine currently free GRES by socket */
				used_sock[s] = bit_set_count(
					sock_gres->bits_by_sock[s]) -
					bit_overlap(
						sock_gres->bits_by_sock[s],
						gres_ns->gres_bit_alloc);
				if ((used_sock[s] == 0) ||
				    (used_sock[s] < gres_js->gres_per_socket)){
					used_sock[s] = 0;
				} else if (++used_sock_cnt ==
					   tres_mc_ptr->sockets_per_node) {
					break;
				}
			}
		} else {
			/* May have needed extra CPUs, exceeding socket count */
			debug("%s: Inconsistent requested/allocated socket count (%d < %d) for job %u on node %d",
			      __func__, tres_mc_ptr->sockets_per_node,
			      used_sock_cnt, job_id, node_inx);
			for (s = 0; s < sock_cnt; s++) {
				if (!used_sock[s] ||
				    !sock_gres->bits_by_sock[s])
					continue;
				/* Determine currently free GRES by socket */
				used_sock[s] = bit_set_count(
					sock_gres->bits_by_sock[s]) -
					bit_overlap(
						sock_gres->bits_by_sock[s],
						gres_ns->gres_bit_alloc);
				if (used_sock[s] == 0)
					used_sock_cnt--;
			}
			/* Exclude sockets with low GRES counts */
			while (tres_mc_ptr->sockets_per_node > used_sock_cnt) {
				int low_sock_inx = -1;
				for (s = sock_cnt - 1; s >= 0; s--) {
					if (used_sock[s] == 0)
						continue;
					if ((low_sock_inx == -1) ||
					    (used_sock[s] <
					     used_sock[low_sock_inx]))
						low_sock_inx = s;
				}
				if (low_sock_inx == -1)
					break;
				used_sock[low_sock_inx] = 0;
				used_sock_cnt--;
			}
		}
	}

	if (gres_ns->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		sorted_gres = xcalloc(gres_cnt, sizeof(int));
		/* Add index for each gres. They will be sorted later */
		for (g = 0; g < gres_cnt; g++) {
			sorted_gres[g] = g;
		}
	}


	gres_needed = used_sock_cnt * gres_js->gres_per_socket;
	gres_needed -= total_res_gres;
	/*
	 * Now pick specific GRES for these sockets.
	 */
	for (s = 0; s < sock_cnt; s++) {
		if (!used_sock[s])
			continue;
		gres_this_sock = gres_js->gres_per_socket;
		if (res_gres_per_sock && res_gres_per_sock[s]) {
			if (res_gres_per_sock[s] < gres_this_sock)
				gres_this_sock -= res_gres_per_sock[s];
			else
				continue;
		}
		gres_needed -= _pick_gres_topo(sock_gres,
					       gres_this_sock,
					       node_inx, s, sorted_gres,
					       links_cnt);
	}
	if (gres_needed) {
		/* Add GRES unconstrained by socket as needed */
		gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
						node_inx, ANY_SOCK_TEST,
						sorted_gres, links_cnt);
	}

	if (gres_needed) {
		/* Allocate extra to other used sockets if needed */
		for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
			if (!used_sock[s])
				continue;
			gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
						       node_inx, s, sorted_gres,
						       links_cnt);
		}
	}

	if (gres_needed && !enforce_binding) {
		/* Allocate extra to other unused sockets if needed */
		for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
			if (used_sock[s])
				continue;
			gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
						       node_inx, s, sorted_gres,
						       links_cnt);
		}
	}

	if (gres_needed) {
		/* Something bad happened on task layout for this GRES type */
		error("%s: Insufficient gres/%s allocated for job %u on node_inx %u (gres still needed %"PRIu64")",
		      __func__, sock_gres->gres_state_job->gres_name, job_id,
		      node_inx, gres_needed);
	}

	xfree(links_cnt);
	xfree(sorted_gres);
	if (allocated_array_copy)
		xfree(used_sock);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use only socket-local GRES
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * rem_nodes IN - count of nodes remaining to place resources on
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 * cpus_per_core IN - CPUs per core on this node
 * RET 0:more work, 1:fini
 */
static int _set_job_bits1(int node_inx, int job_node_inx, int rem_nodes,
			  sock_gres_t *sock_gres, uint32_t job_id,
			  gres_mc_data_t *tres_mc_ptr, uint16_t cpus_per_core,
			  uint32_t *cores_on_sock, uint32_t total_cores,
			  uint32_t total_res_gres, bool enforce_binding)
{
	int gres_cnt;
	uint16_t sock_cnt = 0;
	int g, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int alloc_gres_cnt = 0;
	int max_gres, pick_gres;
	int fini = 0;
	uint16_t cpus_per_gres = 0;
	float gres_needed_per_core;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	if (gres_js->gres_per_job == gres_js->total_gres)
		fini = 1;
	sock_cnt = sock_gres->sock_cnt;
	if (job_node_inx == 0)
		gres_js->total_gres = 0;
	max_gres = gres_js->gres_per_job - gres_js->total_gres -
		(rem_nodes - 1);
	max_gres = MIN(max_gres, sock_gres->total_cnt);
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	if (gres_js->cpus_per_gres) {
		cpus_per_gres = gres_js->cpus_per_gres;
	} else if (gres_js->ntasks_per_gres &&
		   (gres_js->ntasks_per_gres != NO_VAL16)) {
		cpus_per_gres = gres_js->ntasks_per_gres *
			tres_mc_ptr->cpus_per_task;
	}
	if (cpus_per_gres) {
		if (tres_mc_ptr->threads_per_core)
			cpus_per_core = MIN(cpus_per_core,
					    tres_mc_ptr->threads_per_core);
		max_gres = MIN(max_gres,
			       ((total_cores * cpus_per_core) /
				cpus_per_gres));
	}
	if (total_res_gres && (max_gres <= total_res_gres)) {
		gres_js->total_gres += total_res_gres;
		fini = 1;
		return fini;
	} else
		max_gres -= total_res_gres;

	if ((max_gres > 1) && (gres_ns->link_len == gres_cnt))
		pick_gres  = NO_VAL16;
	else {
		/*
		 * max_gres can be < 1 if gres_per_job < rem_nodes. Pick at
		 * least one gpu on the node anyway.
		 * For example --gpus=typeA:2,typeB:1 where there is only one
		 * typeA on each node then two nodes are required. Because this
		 * is not a heterogenous job a typeB gpu does have to be
		 * allocated on each node even though they only requested one
		 * for the job.
		 */
		pick_gres = MAX(max_gres, 1);
	}

	gres_needed_per_core = (float)pick_gres / (float)total_cores;

	/*
	 * Now pick specific GRES for these sockets.
	 * First select all GRES that we might possibly use
	 * Then remove those which are not required and not "best".
	 */
	for (s = 0; ((s < sock_cnt) && (alloc_gres_cnt < pick_gres)); s++) {
		if (!cores_on_sock[s])
			continue;
		int sock_gres_needed = MIN(pick_gres - alloc_gres_cnt,
					   (int)(cores_on_sock[s] *
						 gres_needed_per_core));
		alloc_gres_cnt += _pick_gres_topo(sock_gres,
						  sock_gres_needed,
						  node_inx, s, NULL, NULL);
	}
	if (alloc_gres_cnt < pick_gres)
		alloc_gres_cnt += _pick_gres_topo(sock_gres,
						  (pick_gres - alloc_gres_cnt),
						  node_inx, ANY_SOCK_TEST, NULL,
						  NULL);
	for (s = 0; ((s < sock_cnt) && (alloc_gres_cnt < pick_gres)); s++) {
		if (!cores_on_sock[s])
			continue;
		alloc_gres_cnt += _pick_gres_topo(sock_gres,
						  (pick_gres - alloc_gres_cnt),
						  node_inx, s, NULL, NULL);
	}

	if (alloc_gres_cnt == 0 && !enforce_binding) {
		for (s = 0; ((s < sock_cnt) && (alloc_gres_cnt == 0)); s++) {
			if (cores_on_sock[s])
				continue;
			alloc_gres_cnt += _pick_gres_topo(
				sock_gres, 1, node_inx, s, NULL, NULL);
		}
	}
	if (alloc_gres_cnt == 0) {
		error("%s: job %u failed to find any available GRES on node %d",
		      __func__, job_id, node_inx);
	}
	/* Now pick the "best" max_gres GRES with respect to link counts. */
	if (alloc_gres_cnt > max_gres) {
		int best_link_cnt = -1, best_inx = -1;
		for (s = 0; s < gres_cnt; s++) {
			if (!bit_test(gres_js->gres_bit_select[node_inx], s))
				continue;
			for (g = s + 1; g < gres_cnt; g++) {
				if (!bit_test(gres_js->
					      gres_bit_select[node_inx], g))
					continue;
				if (gres_ns->links_cnt[s][g] <=
				    best_link_cnt)
					continue;
				best_link_cnt = gres_ns->links_cnt[s][g];
				best_inx = s;
			}
		}
		while ((alloc_gres_cnt > max_gres) && (best_link_cnt != -1)) {
			int worst_inx = -1, worst_link_cnt = NO_VAL16;
			for (g = 0; g < gres_cnt; g++) {
				if (g == best_inx)
					continue;
				if (!bit_test(gres_js->
					      gres_bit_select[node_inx], g))
					continue;
				if (gres_ns->links_cnt[best_inx][g] >=
				    worst_link_cnt)
					continue;
				worst_link_cnt =
					gres_ns->links_cnt[best_inx][g];
				worst_inx = g;
			}
			if (worst_inx == -1) {
				error("%s: error managing links_cnt", __func__);
				break;
			}
			bit_clear(gres_js->gres_bit_select[node_inx],
				  worst_inx);
			gres_js->gres_cnt_node_select[node_inx]--;
			alloc_gres_cnt--;
		}
	}
	gres_js->total_gres += alloc_gres_cnt + total_res_gres;

	if (gres_js->total_gres >= gres_js->gres_per_job)
		fini = 1;
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use any GRES on the node
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 * RET 0:more work, 1:fini
 */
static int _set_job_bits2(int node_inx, int job_node_inx,
			  sock_gres_t *sock_gres,
			  uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int gres_cnt;
	int g, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int fini = 0;
	int *links_cnt = NULL, *sorted_gres = NULL;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	if (gres_js->gres_per_job <= gres_js->total_gres) {
		fini = 1;
		return fini;
	}
	if (!gres_js->gres_bit_select ||
	    !gres_js->gres_bit_select[node_inx]) {
		error("%s: gres_bit_select NULL for job %u on node %d",
		      __func__, job_id, node_inx);
		return SLURM_ERROR;
	}

	/*
	 * Identify the GRES (if any) that we want to use as a basis for
	 * maximizing link count (connectivity of the GRES).
	 */
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	if ((gres_js->gres_per_job > gres_js->total_gres) &&
	    (gres_ns->link_len == gres_cnt)) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		sorted_gres = xcalloc(gres_cnt, sizeof(int));
		/* Add index for each gres. They will be sorted later */
		for (g = 0; g < gres_cnt; g++) {
			sorted_gres[g] = g;
		}

		/* Add links for all gres already selected */
		for (g = 0; g < gres_cnt; g++) {
			if (!bit_test(gres_js->gres_bit_select[node_inx], g))
				continue;
			for (int l = 0; (l < gres_cnt); l++) {
				if ((l == g) ||
				    bit_test(gres_ns->gres_bit_alloc, l))
					continue;
				links_cnt[l] += gres_ns->links_cnt[g][l];
			}
		}
		/* Sort gres by most linked to all previously selected gres */
		sorting_links_cnt = links_cnt;
		qsort(sorted_gres, gres_cnt, sizeof(int),
		      _compare_gres_by_links);
		sorting_links_cnt = NULL;
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * Start with GRES available from any socket, then specific sockets
	 */
	for (s = 0; ((s < sock_gres->sock_cnt) &&
		     (gres_js->gres_per_job > gres_js->total_gres));
	     s++) {
		gres_js->total_gres += _pick_gres_topo(
			sock_gres, gres_js->gres_per_job - gres_js->total_gres,
			node_inx, s, sorted_gres, links_cnt);
	}
	if (gres_js->gres_per_job > gres_js->total_gres)
		gres_js->total_gres += _pick_gres_topo(
			sock_gres, gres_js->gres_per_job - gres_js->total_gres,
			node_inx, ANY_SOCK_TEST, sorted_gres, links_cnt);

	if (gres_js->gres_per_job <= gres_js->total_gres)
		fini = 1;

	xfree(links_cnt);
	xfree(sorted_gres);
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-node resource specification
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_node_bits(int node_inx, int job_node_inx,
			   sock_gres_t *sock_gres, uint32_t job_id,
			   uint32_t *used_cores_on_sock, uint32_t used_core_cnt,
			   uint32_t total_res_gres, bool enforce_binding)
{
	int gres_cnt;
	uint16_t sock_cnt = 0;
	int g, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	uint32_t gres_needed;
	int *links_cnt = NULL, *sorted_gres = NULL;
	float gres_needed_per_core;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	sock_cnt =  sock_gres->sock_cnt;

	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	gres_needed = gres_js->gres_per_node - total_res_gres;
	gres_needed_per_core = (float)gres_needed / (float)used_core_cnt;

	if (!gres_needed)
		return;

	if (gres_ns->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		sorted_gres = xcalloc(gres_cnt, sizeof(int));
		/* Add index for each gres. They will be sorted later */
		for (g = 0; g < gres_cnt; g++) {
			sorted_gres[g] = g;
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * First: Try to place GRES on sockets relative to allocated cores cnts.
	 * Second: Try to place additional GRES on allocated sockets.
	 * Third: Use any additional available GRES.
	 */
	for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
		if (!used_cores_on_sock[s])
			continue;
		int sock_gres_needed = MIN(gres_needed,
					   (int)(used_cores_on_sock[s] *
						 gres_needed_per_core));
		gres_needed -= _pick_gres_topo(sock_gres, sock_gres_needed,
					       node_inx, s, sorted_gres,
					       links_cnt);
	}

	if (gres_needed) {
		gres_needed -= _pick_gres_topo(sock_gres, gres_needed, node_inx,
					       ANY_SOCK_TEST, sorted_gres,
					       links_cnt);
	}

	/* Try to place additional GRES on allocated sockets. */
	for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
		if (!used_cores_on_sock[s])
			continue;
		gres_needed -= _pick_gres_topo(sock_gres, gres_needed, node_inx,
					       s, sorted_gres, links_cnt);
	}

	/* Use any additional available GRES.  */
	if (gres_needed && !enforce_binding) {
		for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
			/* Sockets we ignored before */
			if (used_cores_on_sock[s])
				continue;
			gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
						       node_inx, s, sorted_gres,
						       links_cnt);
		}
	}
	xfree(links_cnt);
	xfree(sorted_gres);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-task resource specification
 * node_inx IN - global node index
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tasks_per_socket IN - Task count for each socket

 */
static void _set_task_bits(int node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, uint32_t *tasks_per_socket,
			   uint32_t total_res_gres, bool enforce_binding)
{
	uint16_t sock_cnt = 0;
	int gres_cnt, g, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	uint64_t gres_needed, sock_gres_needed;
	int *links_cnt = NULL, *sorted_gres = NULL;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	sock_cnt = sock_gres->sock_cnt;
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);

	if (!tasks_per_socket) {
		error("%s: tasks_per_socket unset for job %u on node %s",
		      __func__, job_id, node_record_table_ptr[node_inx]->name);
		return;
	}

	if (gres_ns->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		sorted_gres = xcalloc(gres_cnt, sizeof(int));
		/* Add index for each gres. They will be sorted later */
		for (g = 0; g < gres_cnt; g++) {
			sorted_gres[g] = g;
		}
	}

	gres_needed = _get_task_cnt_node(tasks_per_socket, sock_cnt) *
		gres_js->gres_per_task;
	gres_needed -= total_res_gres;

	/* First pick GRES for acitve sockets */
	for (s = 0; s < sock_cnt; s++) {
		if (!tasks_per_socket[s])
			continue;
		sock_gres_needed = MIN(gres_needed,
				       (tasks_per_socket[s] *
					gres_js->gres_per_task));
		gres_needed -= _pick_gres_topo(sock_gres, sock_gres_needed,
					       node_inx, s, sorted_gres,
					       links_cnt);
	}
	if (gres_needed)
		gres_needed -= _pick_gres_topo(sock_gres, gres_needed, node_inx,
					       ANY_SOCK_TEST, sorted_gres,
					       links_cnt);


	if (gres_needed && !enforce_binding) {
		/*
		 * Were unable to find gres on sockets that matched tasks.
		 * Trying sockets now.
		 */
		for (s = 0; ((s < sock_cnt) && gres_needed); s++) {
			gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
						       node_inx, s, sorted_gres,
						       links_cnt);
		}
	}
	xfree(links_cnt);
	xfree(sorted_gres);

	if (gres_needed) {
		/* Something bad happened on task layout for this GRES type */
		error("%s: Insufficient gres/%s allocated for job %u on node_inx %u (gres still needed %"PRIu64", total requested: %"PRIu64")",
		      __func__, sock_gres->gres_state_job->gres_name, job_id,
		      node_inx, gres_needed,
		      _get_task_cnt_node(tasks_per_socket, sock_cnt) *
		      gres_js->gres_per_task);
	}
}

/* Build array to identify task count for each node-socket pair */
static int _build_tasks_per_node_sock(struct job_resources *job_res,
				      uint8_t overcommit,
				      gres_mc_data_t *tres_mc_ptr,
				      uint32_t ***tasks_per_node_socket_ptr)
{
	uint32_t **tasks_per_node_socket;
	int rc = SLURM_SUCCESS, j, node_cnt, job_node_inx = 0;
	int c, s, core_offset;
	int cpus_per_task = 1, cpus_per_node, cpus_per_core;
	int task_per_node_limit = 0;
	int32_t rem_tasks, excess_tasks;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	node_record_t *node_ptr;

	rem_tasks = tres_mc_ptr->ntasks_per_job;
	node_cnt = bit_size(job_res->node_bitmap);
	tasks_per_node_socket = xcalloc(node_cnt, sizeof(uint32_t *));
	for (int i = 0; (node_ptr = next_node_bitmap(job_res->node_bitmap, &i));
	     i++) {
		int tasks_per_node = 0;
		if (get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
					  &cores_per_socket_cnt)) {
			error("%s: failed to get socket/core count", __func__);
			/* Set default of 1 task on socket 0 */
			tasks_per_node_socket[i] = xmalloc(sizeof(uint32_t));
			tasks_per_node_socket[i][0] = 1;
			rem_tasks--;
			continue;
		}
		tasks_per_node_socket[i] = xcalloc(sock_cnt, sizeof(uint32_t));
		if (tres_mc_ptr->ntasks_per_node) {
			task_per_node_limit = tres_mc_ptr->ntasks_per_node;
			cpus_per_task = MAX(1, job_res->cpus[job_node_inx] /
						       job_res->tasks_per_node
							       [job_node_inx]);
		} else if (job_res->tasks_per_node &&
			   job_res->tasks_per_node[job_node_inx]) {
			task_per_node_limit =
				job_res->tasks_per_node[job_node_inx];
			cpus_per_task =
				MAX(1, job_res->cpus[job_node_inx] /
				    job_res->tasks_per_node[job_node_inx]);
		} else {
			/*
			 * NOTE: We should never get here.
			 * cpus_per_node reports CPUs actually used by this
			 * job on this node. Divide by cpus_per_task to yield
			 * valid task count on this node. This can be bad on
			 * cores with more than one thread and job fails to
			 * use all threads.
			 */
			error("%s: tasks_per_node not set", __func__);
			cpus_per_node = get_job_resources_cpus(job_res,
							       job_node_inx);
			if (cpus_per_node < 1) {
				error("%s: failed to get cpus_per_node count",
				      __func__);
				/* Set default of 1 task on socket 0 */
				tasks_per_node_socket[i][0] = 1;
				rem_tasks--;
				continue;
			}
			xassert(tres_mc_ptr->cpus_per_task);
			cpus_per_task = tres_mc_ptr->cpus_per_task;
			task_per_node_limit = cpus_per_node / cpus_per_task;
		}
		core_offset = get_job_resources_offset(job_res, job_node_inx++,
						       0, 0);
		if (tres_mc_ptr->threads_per_core)
			cpus_per_core = MIN(node_ptr->tpc,
					    tres_mc_ptr->threads_per_core);
		else
			cpus_per_core = node_ptr->tpc;

		for (s = 0; s < sock_cnt; s++) {
			int tasks_per_socket = 0, tpc, skip_cores = 0;
			for (c = 0; c < cores_per_socket_cnt; c++) {
				j = (s * cores_per_socket_cnt) + c;
				j += core_offset;
				if (!bit_test(job_res->core_bitmap, j))
					continue;
				if (skip_cores > 0) {
					skip_cores--;
					continue;
				}
				if (tres_mc_ptr->ntasks_per_core) {
					tpc = tres_mc_ptr->ntasks_per_core;
				} else {
					tpc = cpus_per_core / cpus_per_task;
					if (tpc < 1) {
						tpc = 1;
						skip_cores = cpus_per_task /
							cpus_per_core;
						skip_cores--;	/* This core */
					}
					/* Start with 1 task per core */
				}
				tasks_per_node_socket[i][s] += tpc;
				tasks_per_node += tpc;
				tasks_per_socket += tpc;
				rem_tasks -= tpc;
				if (task_per_node_limit) {
					if (tasks_per_node >
					    task_per_node_limit) {
						excess_tasks = tasks_per_node -
							task_per_node_limit;
						tasks_per_node_socket[i][s] -=
							excess_tasks;
						rem_tasks += excess_tasks;
					}
					if (tasks_per_node >=
					    task_per_node_limit) {
						s = sock_cnt;
						break;
					}
				}
				/* NOTE: No support for ntasks_per_board */
				if (tres_mc_ptr->ntasks_per_socket) {
					if (tasks_per_socket >
					    tres_mc_ptr->ntasks_per_socket) {
						excess_tasks =
							tasks_per_socket -
							tres_mc_ptr->
							ntasks_per_socket;
						tasks_per_node_socket[i][s] -=
							excess_tasks;
						rem_tasks += excess_tasks;
					}
					if (tasks_per_socket >=
					    tres_mc_ptr->ntasks_per_socket) {
						break;
					}
				}
			}
		}
	}
	while ((rem_tasks > 0) && overcommit) {
		for (int i = 0;
		     ((rem_tasks > 0) &&
		      next_node_bitmap(job_res->node_bitmap, &i));
		     i++) {
			for (s = 0; (rem_tasks > 0) && (s < sock_cnt); s++) {
				for (c = 0; c < cores_per_socket_cnt; c++) {
					j = (s * cores_per_socket_cnt) + c;
					if (!bit_test(job_res->core_bitmap, j))
						continue;
					tasks_per_node_socket[i][s]++;
					rem_tasks--;
					break;
				}
			}
		}
	}
	if (rem_tasks > 0) { /* This should never happen */
		error("%s: rem_tasks not zero (%d > 0)", __func__, rem_tasks);
		rc = ESLURM_INVALID_GRES;
	}

	*tasks_per_node_socket_ptr = tasks_per_node_socket;
	return rc;
}

static void _free_tasks_per_node_sock(uint32_t **tasks_per_node_socket,
				      int node_cnt)
{
	if (!tasks_per_node_socket)
		return;

	for (int n = 0; n < node_cnt; n++)
		xfree(tasks_per_node_socket[n]);
	xfree(tasks_per_node_socket);
}

/* Return the count of tasks for a job on a given node */
static uint32_t _get_task_cnt_node(uint32_t *tasks_per_socket, int sock_cnt)
{
	uint32_t task_cnt = 0;

	if (!tasks_per_socket) {
		error("%s: tasks_per_socket is NULL", __func__);
		return 1;	/* Best guess if no data structure */
	}

	for (int s = 0; s < sock_cnt; s++)
		task_cnt += tasks_per_socket[s];

	return task_cnt;
}

/* Determine maximum GRES allocation count on this node; no topology */
static uint64_t _get_job_cnt(sock_gres_t *sock_gres,
			     gres_node_state_t *gres_ns, int rem_node_cnt)
{
	uint64_t avail_gres, max_gres;
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;

	avail_gres = gres_ns->gres_cnt_avail - gres_ns->gres_cnt_alloc;
	/* Ensure at least one GRES per node on remaining nodes */
	max_gres = gres_js->gres_per_job - gres_js->total_gres -
		(rem_node_cnt - 1);
	max_gres = MIN(avail_gres, max_gres);

	return max_gres;
}

/* Return count of GRES on this node */
static int _get_gres_node_cnt(gres_node_state_t *gres_ns, int node_inx)
{
	int gres_cnt = 0;

	if (gres_ns->gres_bit_alloc) {
		gres_cnt = bit_size(gres_ns->gres_bit_alloc);
		return gres_cnt;
	}

	/* This logic should be redundant */
	if (gres_ns->topo_gres_bitmap && gres_ns->topo_gres_bitmap[0]) {
		gres_cnt = bit_size(gres_ns->topo_gres_bitmap[0]);
		return gres_cnt;
	}

	/* This logic should also be redundant */
	gres_cnt = 0;
	for (int i = 0; i < gres_ns->topo_cnt; i++)
		gres_cnt += gres_ns->topo_gres_cnt_avail[i];
	return gres_cnt;
}

static int _get_node_sock_specs(struct job_resources *job_res,
				uint16_t *sock_cnt,
				uint16_t *cores_per_socket_cnt,
				int *core_offset,
				uint32_t job_node_inx)
{
	if (get_job_resources_cnt(job_res, job_node_inx, sock_cnt,
				  cores_per_socket_cnt) != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count", __func__);
		return SLURM_ERROR;
	}
	*core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (*core_offset < 0) {
		error("%s: Invalid core offset", __func__);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/* Set array of allocated cores for each socket on this node */
static int _set_used_cnts(select_and_set_args_t *args)
{
	struct job_resources *job_res = args->job_ptr->job_resrcs;
	int core_offset;
	uint16_t cores_per_socket_cnt = 0;
	int socket_inx, begin, core_cnt;

	xassert(job_res->core_bitmap);

	/* Confirm output values are not set yet */
	xassert(args->used_cores_on_sock == NULL);
	xassert(args->used_core_cnt == 0);
	xassert(args->used_sock_cnt == 0);
	xassert(args->sock_cnt == 0);

	_get_node_sock_specs(job_res, &(args->sock_cnt), &cores_per_socket_cnt,
			     &core_offset, args->job_node_inx);

	args->used_cores_on_sock = xcalloc(args->sock_cnt, sizeof(int));
	for (socket_inx = 0; socket_inx < args->sock_cnt; socket_inx++) {
		begin = core_offset + (socket_inx * cores_per_socket_cnt);
		core_cnt = bit_set_count_range(job_res->core_bitmap, begin,
					       begin + cores_per_socket_cnt);
		args->used_cores_on_sock[socket_inx] += core_cnt;
		args->used_core_cnt += core_cnt;
		if (core_cnt)
			args->used_sock_cnt++;
	}

	if (args->used_sock_cnt == 0) {
		error("%s: No allocated cores found", __func__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _set_res_core_bits(uint32_t **res_gres_per_sock,
			      uint32_t *total_res_gres,
			      int *sock_with_res_cnt,
			      select_and_set_args_t *args,
			      sock_gres_t *sock_gres)
{
	struct job_resources *job_res = args->job_ptr->job_resrcs;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int core_offset;
	int socket_inx, begin, end;
	int gres_cnt;
	int *links_cnt = NULL, *sorted_gres = NULL;
	uint16_t cores_per_socket_cnt = 0;
	uint16_t sock_cnt = 0;
	uint32_t gres_needed;
	uint32_t res_cores_per_gpu =
		node_record_table_ptr[args->node_inx]->res_cores_per_gpu;

	xassert(job_res->core_bitmap);

	*total_res_gres = 0;
	*sock_with_res_cnt = 0;
	if (*res_gres_per_sock)
		xfree(*res_gres_per_sock);

	if (!res_cores_per_gpu)
		return SLURM_SUCCESS;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	gres_cnt = bit_size(gres_js->gres_bit_select[args->node_inx]);

	if (_get_node_sock_specs(job_res, &sock_cnt,
				 &cores_per_socket_cnt, &core_offset,
				 args->job_node_inx) != SLURM_SUCCESS)
		return SLURM_ERROR;

	*res_gres_per_sock = xcalloc(sock_cnt, sizeof(int));
	for (socket_inx = 0; socket_inx < sock_cnt; socket_inx++) {
		begin = core_offset + (socket_inx * cores_per_socket_cnt);
		end =  begin + cores_per_socket_cnt;
		for (int i = begin; i < end; i++) {
			int j = i - core_offset;
			if (bit_test(job_res->core_bitmap, i) &&
			    bit_test(gres_js->res_gpu_cores[args->node_inx], j))
				((*res_gres_per_sock)[socket_inx])++;
		}
		(*res_gres_per_sock)[socket_inx] =
			ROUNDUP((*res_gres_per_sock)[socket_inx],
				res_cores_per_gpu);
		*total_res_gres += (*res_gres_per_sock)[socket_inx];
		if ((*res_gres_per_sock)[socket_inx])
			(*sock_with_res_cnt)++;
	}

	if (gres_id_shared(sock_gres->gres_state_job->config_flags)) {
		if (*sock_with_res_cnt > 1 &&
		    !(slurm_conf.select_type_param & MULTIPLE_SHARING_GRES_PJ)){
			/*
			 * Have to allocate gres accross more then one socket.
			 * This is assuming one socket per gres configueration
			 * line.
			 */
			error("Restricted gpu cores on multiple sockets which requires MULTIPLE_SHARING_GRES_PJ to be set");
			return ESLURM_INVALID_GRES;
		}
		/* Allow shared gres to allocate their own */
		return SLURM_SUCCESS;
	}

	if (gres_ns->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		sorted_gres = xcalloc(gres_cnt, sizeof(int));
		/* Add index for each gres. They will be sorted later */
		for (int g = 0; g < gres_cnt; g++) {
			sorted_gres[g] = g;
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 */
	for (socket_inx = 0; socket_inx < sock_cnt; socket_inx++) {
		/*
		 * Having multiple gpu types on the same socket could result it
		 * picking the wrong gpu type here if the job request non-typed
		 * gpus.
		 */
		gres_needed = (*res_gres_per_sock)[socket_inx];
		gres_needed -= _pick_gres_topo(sock_gres, gres_needed,
					       args->node_inx, socket_inx,
					       sorted_gres, links_cnt);
		if (gres_needed) {
			(*res_gres_per_sock)[socket_inx] -= gres_needed;
			error("%s: More restricted gpu cores allocated then should be possible for job %u on node %d",
			      __func__, args->job_ptr->job_id, args->node_inx);
		}
	}
	xfree(links_cnt);
	xfree(sorted_gres);

	return SLURM_SUCCESS;
}

static int _select_and_set_node(void *x, void *arg)
{
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	struct job_resources *job_res;
	int node_cnt, gres_cnt;
	uint32_t job_id;
	bool enforce_binding;

	node_record_t *node_ptr;
	job_record_t *job_ptr;
	gres_mc_data_t *tres_mc_ptr;
	uint32_t **tasks_per_node_socket;
	uint32_t *res_gres_per_sock = NULL;
	uint32_t total_res_gres = 0;
	int sock_with_res_cnt = 0;
	int node_inx, job_node_inx, rem_node_cnt;
	int *job_fini, *rc;

	sock_gres_t *sock_gres = x;
	select_and_set_args_t *args = arg;

	node_ptr = args->node_ptr;
	job_ptr = args->job_ptr;
	tres_mc_ptr = args->tres_mc_ptr;
	node_inx = args->node_inx;
	job_node_inx = args->job_node_inx;
	rem_node_cnt = args->rem_node_cnt;
	job_fini = args->job_fini;
	rc = args->rc;

	job_res = job_ptr->job_resrcs;
	job_id = job_ptr->job_id;
	node_cnt = bit_size(job_res->node_bitmap);
	enforce_binding = job_ptr->bit_flags & GRES_ENFORCE_BIND;
	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	if (!gres_js || !gres_ns)
		return 0;
	if ((gres_js->gres_per_node ||
	     gres_js->gres_per_socket ||
	     gres_js->gres_per_job) && /* Data needed */
	    !args->used_cores_on_sock) { /* Not built yet */
		*rc = _set_used_cnts(args);
		if (*rc != SLURM_SUCCESS)
			return -1;
	}
	if (gres_js->gres_per_task && /* Data needed */
	    !*args->tasks_per_node_socket) { /* Not built yet */
		*rc = _build_tasks_per_node_sock(job_res,
						 job_ptr->details->overcommit,
						 tres_mc_ptr,
						 args->tasks_per_node_socket);
		if (*rc != SLURM_SUCCESS)
			return -1;
	}

	tasks_per_node_socket = *args->tasks_per_node_socket;

	if (gres_js->total_node_cnt == 0) {
		gres_js->total_node_cnt = node_cnt;
		gres_js->total_gres = 0;
	}
	if (!gres_js->gres_cnt_node_select) {
		gres_js->gres_cnt_node_select = xcalloc(node_cnt,
							sizeof(uint64_t));
	}

	/* Reinitialize counter */
	if (node_inx == bit_ffs(job_res->node_bitmap))
		gres_js->total_gres = 0;

	if (gres_ns->topo_cnt == 0) {
		/* No topology, just set a count */
		if (gres_js->gres_per_node) {
			gres_js->gres_cnt_node_select[node_inx] =
				gres_js->gres_per_node;
		} else if (gres_js->gres_per_socket) {
			gres_js->gres_cnt_node_select[node_inx] =
				gres_js->gres_per_socket;
			gres_js->gres_cnt_node_select[node_inx] *=
				args->used_sock_cnt;
		} else if (gres_js->gres_per_task) {
			gres_js->gres_cnt_node_select[node_inx] =
				gres_js->gres_per_task;
			gres_js->gres_cnt_node_select[node_inx] *=
				_get_task_cnt_node(
					tasks_per_node_socket[node_inx],
					node_ptr->tot_sockets);
		} else if (gres_js->gres_per_job) {
			gres_js->gres_cnt_node_select[node_inx] = _get_job_cnt(
				sock_gres, gres_ns, rem_node_cnt);
		}
		gres_js->total_gres += gres_js->gres_cnt_node_select[node_inx];
		return 0;
	}

	/* Working with topology, need to pick specific GRES */
	if (!gres_js->gres_bit_select) {
		gres_js->gres_bit_select = xcalloc(node_cnt,
						   sizeof(bitstr_t *));
	}
	gres_cnt = _get_gres_node_cnt(gres_ns, job_node_inx);
	FREE_NULL_BITMAP(gres_js->gres_bit_select[node_inx]);
	gres_js->gres_bit_select[node_inx] = bit_alloc(gres_cnt);
	gres_js->gres_cnt_node_select[node_inx] = 0;

	if (gres_js->res_gpu_cores && gres_js->res_gpu_cores[node_inx]) {
		*rc = _set_res_core_bits(&res_gres_per_sock, &total_res_gres,
					 &sock_with_res_cnt, args, sock_gres);
		if (*rc != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	if (gres_id_shared(sock_gres->gres_state_job->config_flags)) {
		_init_gres_per_bit_select(gres_js, node_inx);
		if (gres_js->gres_per_node) {
			*rc = _set_shared_node_bits(
				node_inx, job_node_inx, sock_gres, job_id,
				enforce_binding, args->used_cores_on_sock,
				total_res_gres, res_gres_per_sock,
				sock_with_res_cnt);
		} else if (gres_js->gres_per_task) {
			*rc = _set_shared_task_bits(
				node_inx, sock_gres, job_id, enforce_binding,
				(job_ptr->bit_flags &
				 GRES_ONE_TASK_PER_SHARING),
				tasks_per_node_socket[node_inx], total_res_gres,
				res_gres_per_sock, sock_with_res_cnt);
		} else {
			error("%s job %u job_spec lacks valid shared GRES counter",
			      __func__, job_id);
			*rc = ESLURM_INVALID_GRES;
		}
	} else if (gres_js->gres_per_node) {
		_set_node_bits(node_inx, job_node_inx, sock_gres, job_id,
			       args->used_cores_on_sock, args->used_core_cnt,
			       total_res_gres, enforce_binding);
	} else if (gres_js->gres_per_socket) {
		_set_sock_bits(node_inx, job_node_inx, sock_gres, job_id,
			       tres_mc_ptr, args->used_cores_on_sock,
			       res_gres_per_sock, total_res_gres,
			       args->used_sock_cnt, enforce_binding);
	} else if (gres_js->gres_per_task) {
		_set_task_bits(node_inx, sock_gres, job_id,
			       tasks_per_node_socket[node_inx], total_res_gres,
			       enforce_binding);
	} else if (gres_js->gres_per_job) {
		int tmp = _set_job_bits1(node_inx, job_node_inx, rem_node_cnt,
					 sock_gres, job_id, tres_mc_ptr,
					 node_ptr->tpc,
					 args->used_cores_on_sock,
					 args->used_core_cnt, total_res_gres,
					 enforce_binding);
		if (*job_fini != 0)
			*job_fini = tmp;
	} else {
		error("%s job %u job_spec lacks GRES counter", __func__,
		      job_id);
	}
	if ((*job_fini) == -1) {
		/*
		 * _set_job_bits1() updates total_gres counter,
		 * this handle other cases.
		 */
		gres_js->total_gres += gres_js->gres_cnt_node_select[node_inx];
	}
	xfree(res_gres_per_sock);
	return 0;
}

/*
 * Make final GRES selection for the job
 * sock_gres_list IN - per-socket GRES details, one record per allocated node
 * IN job_ptr - job's pointer
 * tres_mc_ptr IN - job's multi-core options
 * RET SLURM_SUCCESS or error code
 */
extern int gres_select_filter_select_and_set(list_t **sock_gres_list,
					     job_record_t *job_ptr,
					     gres_mc_data_t *tres_mc_ptr)
{
	list_itr_t *sock_gres_iter;
	sock_gres_t *sock_gres;
	int i, job_node_inx = 0;
	int node_cnt, rem_node_cnt;
	int job_fini = -1;	/* -1: not applicable, 0: more work, 1: fini */
	uint32_t **tasks_per_node_socket = NULL, job_id;
	int rc = SLURM_SUCCESS;
	node_record_t *node_ptr;
	struct job_resources *job_res = job_ptr->job_resrcs;
	select_and_set_args_t select_and_set_args = {
		.job_ptr = job_ptr,
		.tres_mc_ptr = tres_mc_ptr,
		.tasks_per_node_socket = &tasks_per_node_socket,
		.job_fini = &job_fini,
		.rc = &rc,
	};

	if (!job_res || !job_res->node_bitmap)
		return SLURM_ERROR;

	job_id = job_ptr->job_id;
	node_cnt = bit_size(job_res->node_bitmap);
	rem_node_cnt = bit_set_count(job_res->node_bitmap);
	for (i = 0;
	     ((node_ptr = next_node_bitmap(job_res->node_bitmap, &i)) &&
	      (rc == SLURM_SUCCESS));
	     i++) {
		select_and_set_args.job_node_inx = job_node_inx;
		select_and_set_args.node_inx = i;
		select_and_set_args.node_ptr = node_ptr;
		select_and_set_args.rem_node_cnt = rem_node_cnt;

		/*
		 * These variables are set and used in _select_and_set_node().
		 * We xfree used_cores_on_sock after the list_for_each().
		 */
		select_and_set_args.used_cores_on_sock = NULL;
		select_and_set_args.used_core_cnt = 0;
		select_and_set_args.used_sock_cnt = 0;
		select_and_set_args.sock_cnt = 0;

		(void) list_for_each(sock_gres_list[job_node_inx],
				     _select_and_set_node,
				     &select_and_set_args);
		job_node_inx++;
		rem_node_cnt--;
		xfree(select_and_set_args.used_cores_on_sock);
	}

	if (job_fini == 0) {
		/*
		 * Need more GRES to satisfy gres-per-job option with bitmaps.
		 * This logic will make use of GRES that are not on allocated
		 * sockets and are thus generally less desirable to use.
		 */
		job_node_inx = -1;
		for (i = 0; next_node_bitmap(job_res->node_bitmap, &i); i++) {
			job_fini = -1;
			sock_gres_iter = list_iterator_create(
				sock_gres_list[++job_node_inx]);
			while ((sock_gres = (sock_gres_t *)
				list_next(sock_gres_iter))) {
				bool tmp;
				if (!sock_gres->gres_state_job->gres_data ||
				    !sock_gres->gres_state_node->gres_data)
					continue;
				tmp = _set_job_bits2(i, job_node_inx,
						     sock_gres, job_id,
						     tres_mc_ptr);
				if (job_fini != 0)
					job_fini = tmp;
			}
			list_iterator_destroy(sock_gres_iter);
			if (job_fini == 1)
				break;
		}
		if (job_fini == 0) {
			error("%s job %u failed to satisfy gres-per-job counter",
			      __func__, job_id);
			rc = ESLURM_NODE_NOT_AVAIL;
		}
	}
	_free_tasks_per_node_sock(tasks_per_node_socket, node_cnt);

	return rc;
}
