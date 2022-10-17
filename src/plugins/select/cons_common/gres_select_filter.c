/*****************************************************************************\
 *  gres_select_filter.c - filters used in the select plugin
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

static void _job_core_filter(gres_state_t *gres_state_job,
			     gres_state_t *gres_state_node,
			     bool use_total_gres, bitstr_t *core_bitmap,
			     int core_start_bit, int core_end_bit,
			     char *node_name)
{
	int i, j, core_ctld;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_busy_dev = gres_use_busy_dev(gres_state_node, use_total_gres);

	if (!gres_ns->topo_cnt || !core_bitmap ||	/* No topology info */
	    !gres_js->gres_per_node)		/* No job GRES */
		return;

	/* Determine which specific cores can be used */
	avail_core_bitmap = bit_copy(core_bitmap);
	bit_nclear(avail_core_bitmap, core_start_bit, core_end_bit);
	for (i = 0; i < gres_ns->topo_cnt; i++) {
		if (gres_ns->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (gres_ns->topo_gres_cnt_alloc[i] >=
		     gres_ns->topo_gres_cnt_avail[i]))
			continue;
		if (use_busy_dev &&
		    (gres_ns->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (gres_js->type_name &&
		    (!gres_ns->topo_type_name[i] ||
		     (gres_js->type_id != gres_ns->topo_type_id[i])))
			continue;
		if (!gres_ns->topo_core_bitmap[i]) {
			FREE_NULL_BITMAP(avail_core_bitmap);	/* No filter */
			return;
		}
		core_ctld = core_end_bit - core_start_bit + 1;
		gres_validate_node_cores(gres_ns, core_ctld, node_name);
		core_ctld = bit_size(gres_ns->topo_core_bitmap[i]);
		for (j = 0; j < core_ctld; j++) {
			if (bit_test(gres_ns->topo_core_bitmap[i], j)) {
				bit_set(avail_core_bitmap, core_start_bit + j);
			}
		}
	}
	bit_and(core_bitmap, avail_core_bitmap);
	FREE_NULL_BITMAP(avail_core_bitmap);
}

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES)
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_node_config_validate()
 * IN use_total_gres - if set then consider all GRES resources as available,
 *		       and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores
 *                      (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first cores
 * IN core_end_bit - index into core_bitmap for this node's last cores
 */
extern void gres_select_filter_cons_res(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *core_bitmap,
					int core_start_bit, int core_end_bit,
					char *node_name)
{
	ListIterator  job_gres_iter;
	gres_state_t *gres_state_job, *gres_state_node;

	if ((job_gres_list == NULL) || (core_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(core_bitmap, core_start_bit, core_end_bit);
		return;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_state_node = list_find_first(node_gres_list, gres_find_id,
						  &gres_state_job->plugin_id);
		if (gres_state_node == NULL) {
			/* node lack resources required by the job */
			bit_nclear(core_bitmap, core_start_bit, core_end_bit);
			break;
		}

		_job_core_filter(gres_state_job,
				 gres_state_node,
				 use_total_gres, core_bitmap,
				 core_start_bit, core_end_bit,
				 node_name);
	}
	list_iterator_destroy(job_gres_iter);

	return;
}

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
 *                     gres_sched_create_sock_gres_list()
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
extern int gres_select_filter_remove_unusable(List sock_gres_list,
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
	ListIterator sock_gres_iter;
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
		 * gres_sched_create_sock_gres_list()
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
static int _sock_gres_sort(void *x, void *y)
{
	sock_gres_t *sock_gres1 = *(sock_gres_t **) x;
	sock_gres_t *sock_gres2 = *(sock_gres_t **) y;
	gres_node_state_t *gres_ns1 = sock_gres1->gres_state_node ?
		sock_gres1->gres_state_node->gres_data : NULL;
	gres_node_state_t *gres_ns2 = sock_gres2->gres_state_node ?
		sock_gres2->gres_state_node->gres_data : NULL;
	gres_job_state_t *gres_js1 = sock_gres1->gres_state_job ?
		sock_gres1->gres_state_job->gres_data : NULL;
	gres_job_state_t *gres_js2 = sock_gres2->gres_state_job ?
		sock_gres2->gres_state_job->gres_data : NULL;

	int weight1 = 0, weight2 = 0;

	if (gres_ns1 && !gres_ns1->topo_cnt)
		weight1 += 0x02;
	if (gres_js1 && !gres_js1->gres_per_socket)
		weight1 += 0x01;

	if (gres_ns2 && !gres_ns2->topo_cnt)
		weight2 += 0x02;
	if (gres_js2 && !gres_js2->gres_per_socket)
		weight2 += 0x01;

	return weight1 - weight2;
}

static int _sort_sockets_by_avail_cores(const void *x, const void *y,
					void *socket_avail_cores)
{
	uint16_t *sockets = (uint16_t *)socket_avail_cores;
	return (sockets[*(int *)y] - sockets[*(int *)x]);
}

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by
 *	gres_sched_create_sock_gres_list()
 * IN sockets - Count of sockets on the node
 * IN cores_per_socket - Count of cores per socket on the node
 * IN cpus_per_core - Count of CPUs per core on the node
 * IN avail_cpus - Count of available CPUs on the node, UPDATED
 * IN min_tasks_this_node - Minimum count of tasks that can be started on this
 *                          node, UPDATED
 * IN max_tasks_this_node - Maximum count of tasks that can be started on this
 *                          node or NO_VAL, UPDATED
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN first_pass - set if first scheduling attempt for this job, use
 *		   co-located GRES and cores if possible
 * IN avail_core - cores available on this node, UPDATED
 */
extern void gres_select_filter_sock_core(gres_mc_data_t *mc_ptr,
					 List sock_gres_list,
					 uint16_t sockets,
					 uint16_t cores_per_socket,
					 uint16_t cpus_per_core,
					 uint16_t *avail_cpus,
					 uint32_t *min_tasks_this_node,
					 uint32_t *max_tasks_this_node,
					 int rem_nodes,
					 bool enforce_binding,
					 bool first_pass,
					 bitstr_t *avail_core)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	int tot_core_cnt = 0;
	uint32_t task_cnt_incr;
	bool *req_sock; /* Required socket */
	int *socket_index; /* Socket indexes */
	uint16_t *avail_cores_per_sock;
	bool has_cpus_per_gres = false;

	if (*max_tasks_this_node == 0)
		return;

	xassert(avail_core);
	avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	for (int s = 0; s < sockets; s++) {
		for (int c = 0; c < cores_per_socket; c++) {
			int i = (s * cores_per_socket) + c;
			if (bit_test(avail_core, i))
				avail_cores_per_sock[s]++;
		}
		tot_core_cnt += avail_cores_per_sock[s];
	}

	task_cnt_incr = *min_tasks_this_node;
	req_sock = xcalloc(sockets, sizeof(bool));
	socket_index = xcalloc(sockets, sizeof(int));

	list_sort(sock_gres_list, _sock_gres_sort);
	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		gres_job_state_t *gres_js;
		bool sufficient_gres;
		uint64_t cnt_avail_total, max_tasks;
		uint64_t max_gres = 0, rem_gres = 0;
		uint16_t avail_cores_tot = 0, cpus_per_gres;
		int min_core_cnt, req_cpus, rem_sockets, sock_cnt = 0;

		if (!sock_gres->gres_state_job)
			continue;
		gres_js = sock_gres->gres_state_job->gres_data;
		if (gres_js->gres_per_job &&
		    (gres_js->total_gres < gres_js->gres_per_job)) {
			rem_gres = gres_js->gres_per_job -
				gres_js->total_gres;
		}

		/*
		 * gres_select_filter_remove_unusable() sets
		 * sock_gres->max_node_gres
		 * for mem_per_gres enforcement; use it to set GRES limit for
		 * this node (max_gres).
		 */
		if (sock_gres->max_node_gres) {
			if (rem_gres && (rem_gres < sock_gres->max_node_gres))
				max_gres = rem_gres;
			else
				max_gres = sock_gres->max_node_gres;
		}
		rem_nodes = MAX(rem_nodes, 1);
		rem_sockets = MAX(1, mc_ptr->sockets_per_node);
		if (max_gres &&
		    ((gres_js->gres_per_node > max_gres) ||
		     ((gres_js->gres_per_socket * rem_sockets) > max_gres))) {
			*max_tasks_this_node = 0;
			break;
		}
		if (gres_js->gres_per_node && gres_js->gres_per_task) {
			max_tasks = gres_js->gres_per_node /
				gres_js->gres_per_task;
			if ((max_tasks == 0) ||
			    (max_tasks > *max_tasks_this_node) ||
			    (max_tasks < *min_tasks_this_node)) {
				*max_tasks_this_node = 0;
				break;
			}
			if ((*max_tasks_this_node == NO_VAL) ||
			    (*max_tasks_this_node > max_tasks))
				*max_tasks_this_node = max_gres;
		}

		min_core_cnt = MAX(*min_tasks_this_node, 1) *
			MAX(mc_ptr->cpus_per_task, 1);
		min_core_cnt = (min_core_cnt + cpus_per_core - 1) /
			cpus_per_core;

		if (gres_js->cpus_per_gres) {
			cpus_per_gres = gres_js->cpus_per_gres;
			has_cpus_per_gres = true;
		} else if (gres_js->ntasks_per_gres &&
			 (gres_js->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = gres_js->ntasks_per_gres *
				mc_ptr->cpus_per_task;
		else {
			cpus_per_gres = gres_js->def_cpus_per_gres;
			if (cpus_per_gres)
				has_cpus_per_gres = true;
		}

		/* Filter out unusable GRES by socket */
		cnt_avail_total = sock_gres->cnt_any_sock;
		sufficient_gres = false;
		for (int s = 0; s < sockets; s++)
			socket_index[s] = s;
		qsort_r(socket_index, sockets, sizeof(int),
			_sort_sockets_by_avail_cores, avail_cores_per_sock);

		for (int j = 0; j < sockets; j++) {
			uint64_t cnt_avail_sock, tot_gres_sock;
			int s;
			/*
			 * Test for sufficient gres_per_socket
			 *
			 * Start with socket with most cores available,
			 * so we know that we have max number of cores on socket
			 * with allocated GRES.
			 */
			s = socket_index[j];

			if (sock_gres->cnt_by_sock) {
				cnt_avail_sock = sock_gres->cnt_by_sock[s];
			} else
				cnt_avail_sock = 0;

			/*
			 * If enforce binding number of gres allocated per
			 * socket has to be limited by cpus_per_gres
			 */
			if ((enforce_binding || first_pass) && cpus_per_gres) {
				int max_gres_socket = (avail_cores_per_sock[s] *
						       cpus_per_core) /
					cpus_per_gres;
				cnt_avail_sock = MIN(cnt_avail_sock,
						     max_gres_socket);
			}

			tot_gres_sock = sock_gres->cnt_any_sock +
				cnt_avail_sock;
			if ((gres_js->gres_per_socket > tot_gres_sock) ||
			    (tot_gres_sock == 0)) {
				/*
				 * Insufficient GRES on this socket
				 * GRES removed here won't be used in 2nd pass
				 */
				if (((gres_js->gres_per_socket >
				      tot_gres_sock) ||
				     enforce_binding) &&
				    sock_gres->cnt_by_sock) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
				if (first_pass &&
				    (tot_core_cnt > min_core_cnt)) {
					for (int c = cores_per_socket - 1;
					     c >= 0; c--) {
						int i = (s * cores_per_socket) + c;
						if (!bit_test(avail_core, i))
							continue;
						bit_clear(avail_core, i);

						avail_cores_per_sock[s]--;
						if (bit_set_count(avail_core) *
						    cpus_per_core <
						    *avail_cpus) {
							*avail_cpus -=
								cpus_per_core;
						}
						if (--tot_core_cnt <=
						    min_core_cnt)
							break;
					}
				}
			}

			avail_cores_tot += avail_cores_per_sock[s];
			/* Test for available cores on this socket */
			if ((enforce_binding || first_pass) &&
			    (avail_cores_per_sock[s] == 0))
				continue;

			cnt_avail_total += cnt_avail_sock;
			if (!sufficient_gres) {
				req_sock[s] = true;
				sock_cnt++;
			}

			if (!sock_gres->cnt_any_sock &&
			    ((max_gres && (max_gres >= cnt_avail_total)) ||
			     (gres_js->gres_per_node &&
			      (cnt_avail_total >= gres_js->gres_per_node)))) {
				/*
				 * Sufficient gres will leave remaining CPUs as
				 * !req_sock. We do this only when we
				 * collected enough and all collected gres of
				 * considered type are bound to socket.
				 */
				sufficient_gres = true;
			}
		}

		if (cpus_per_gres) {
			if (max_gres)
				max_gres = MIN((*avail_cpus / cpus_per_gres),
					       max_gres);
			else
				max_gres = *avail_cpus / cpus_per_gres;
			cnt_avail_total = MIN(cnt_avail_total, max_gres);
		}
		if ((cnt_avail_total == 0) ||
		    (gres_js->gres_per_node > cnt_avail_total) ||
		    (gres_js->gres_per_task > cnt_avail_total)) {
			*max_tasks_this_node = 0;
		}
		if (gres_js->gres_per_task) {
			max_tasks = cnt_avail_total / gres_js->gres_per_task;
			*max_tasks_this_node = MIN(*max_tasks_this_node,
						   max_tasks);
		}

		/*
		 * min_tasks_this_node and max_tasks_this_node must be multiple
		 * of original min_tasks_this_node value. This is to support
		 * ntasks_per_* option and we just need to select a count of
		 * tasks, sockets, etc. Round the values down.
		 */
		*min_tasks_this_node = (*min_tasks_this_node / task_cnt_incr) *
			task_cnt_incr;
		*max_tasks_this_node = (*max_tasks_this_node / task_cnt_incr) *
			task_cnt_incr;

		if (*max_tasks_this_node == 0)
			break;

		/*
		 * Remove cores on not required sockets when enforce-binding,
		 * this has to happen also when max_tasks_this_node == NO_VAL
		 */
		if (enforce_binding || first_pass) {
			for (int s = 0; s < sockets; s++) {
				if (req_sock[s])
					continue;
				for (int c = cores_per_socket - 1; c >= 0; c--) {
					int i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);
					if (bit_set_count(avail_core) *
					    cpus_per_core < *avail_cpus) {
						*avail_cpus -= cpus_per_core;
					}
					avail_cores_tot--;
					avail_cores_per_sock[s]--;
				}
			}
		}

		if (*max_tasks_this_node == NO_VAL) {
			if (cpus_per_gres) {
				int i = *avail_cpus / cpus_per_gres;
				sock_gres->total_cnt =
					MIN(i, sock_gres->total_cnt);
			}
			log_flag(SELECT_TYPE, "max_tasks_this_node is set to NO_VAL, won't clear non-needed cores");
			continue;
		}
		if (*max_tasks_this_node < *min_tasks_this_node) {
			error("%s: min_tasks_this_node:%u > max_tasks_this_node:%u",
			      __func__,
			      *min_tasks_this_node,
			      *max_tasks_this_node);
		}

		/*
		 * Determine how many cores are needed for this job.
		 * Consider rounding errors if cpus_per_task not divisible
		 * by cpus_per_core
		 */
		req_cpus = *max_tasks_this_node;
		if (mc_ptr->cpus_per_task) {
			int threads_per_core, removed_tasks = 0;
			int efctv_cpt = mc_ptr->cpus_per_task;

			if (mc_ptr->threads_per_core)
				threads_per_core =
					MIN(cpus_per_core,
					    mc_ptr->threads_per_core);
			else
				threads_per_core = cpus_per_core;

			if ((mc_ptr->ntasks_per_core == 1) &&
			    (efctv_cpt % threads_per_core)) {
				efctv_cpt /= threads_per_core;
				efctv_cpt++;
				efctv_cpt *= threads_per_core;
			}

			req_cpus *= efctv_cpt;

			while (*max_tasks_this_node >= *min_tasks_this_node) {
				/* round up by full threads per core */
				req_cpus += threads_per_core - 1;
				req_cpus /= threads_per_core;
				if (req_cpus <= avail_cores_tot) {
					if (removed_tasks)
						log_flag(SELECT_TYPE, "settings required_cores=%d by max_tasks_this_node=%u(reduced=%d) cpus_per_task=%d cpus_per_core=%d threads_per_core:%d",
							 req_cpus,
							 *max_tasks_this_node,
							 removed_tasks,
							 mc_ptr->cpus_per_task,
							 cpus_per_core,
							 mc_ptr->
							 threads_per_core);
					break;
				}
				removed_tasks++;
				(*max_tasks_this_node)--;
				req_cpus = *max_tasks_this_node;
				req_cpus *= efctv_cpt;
			}
		}
		if (cpus_per_gres) {
			int i;
			if (gres_js->gres_per_node) {
				i = gres_js->gres_per_node;
				log_flag(SELECT_TYPE, "estimating required CPUs gres_per_node=%"PRIu64,
					 gres_js->gres_per_node);
			} else if (gres_js->gres_per_socket) {
				i = gres_js->gres_per_socket * sock_cnt;
				log_flag(SELECT_TYPE, "estimating required CPUs gres_per_socket=%"PRIu64,
					 gres_js->gres_per_socket);
			} else if (gres_js->gres_per_task) {
				i = gres_js->gres_per_task *
					*max_tasks_this_node;
				log_flag(SELECT_TYPE, "estimating required CPUs max_tasks_this_node=%u gres_per_task=%"PRIu64,
					 *max_tasks_this_node,
					 gres_js->gres_per_task);
			} else if (cnt_avail_total) {
				i = cnt_avail_total;
				log_flag(SELECT_TYPE, "estimating required CPUs cnt_avail_total=%"PRIu64,
					 cnt_avail_total);
			} else {
				i = 1;
				log_flag(SELECT_TYPE, "estimating required CPUs default to 1 task");
			}
			i *= cpus_per_gres;
			i = (i + cpus_per_core - 1) / cpus_per_core;
			if (req_cpus < i)
				log_flag(SELECT_TYPE, "Increasing req_cpus=%d from cpus_per_gres=%d cpus_per_core=%u",
					 i, cpus_per_gres, cpus_per_core);
			req_cpus = MAX(req_cpus, i);
		}

		if (req_cpus > avail_cores_tot) {
			log_flag(SELECT_TYPE, "Job cannot run on node required CPUs:%d > aval_cores_tot:%d",
				 req_cpus, avail_cores_tot);
			*max_tasks_this_node = 0;
			break;
		}

		/*
		 * Clear extra avail_core bits on sockets we don't need
		 * up to required number of cores based on max_tasks_this_node.
		 * In case of enforce-binding those are already cleared.
		 */
		if ((avail_cores_tot > req_cpus) &&
		    !enforce_binding && !first_pass) {
			for (int s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cpus)
					break;
				if (req_sock[s])
					continue;
				for (int c = cores_per_socket - 1; c >= 0; c--) {
					int i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);
					if (bit_set_count(avail_core) *
					    cpus_per_core < *avail_cpus) {
						*avail_cpus -= cpus_per_core;
					}
					avail_cores_tot--;
					avail_cores_per_sock[s]--;
					if (avail_cores_tot == req_cpus)
						break;
				}
			}
		}

		/*
		 * Clear extra avail_core bits on sockets we do need, but
		 * spread them out so that every socket has some cores
		 * available to use with the nearby GRES that we do need.
		 */
		while (avail_cores_tot > req_cpus) {
			int full_socket = -1;
			for (int s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cpus)
					break;
				if (!req_sock[s] ||
				    (avail_cores_per_sock[s] == 0))
					continue;
				if ((full_socket == -1) ||
				    (avail_cores_per_sock[full_socket] <
				     avail_cores_per_sock[s])) {
					full_socket = s;
				}
			}
			if (full_socket == -1)
				break;
			for (int c = cores_per_socket - 1; c >= 0; c--) {
				int i = (full_socket * cores_per_socket) + c;
				if (!bit_test(avail_core, i))
					continue;
				bit_clear(avail_core, i);
				if (bit_set_count(avail_core) * cpus_per_core <
				    *avail_cpus) {
					*avail_cpus -= cpus_per_core;
				}
				avail_cores_per_sock[full_socket]--;
				avail_cores_tot--;
				break;
			}
		}
		if (cpus_per_gres) {
			int i = *avail_cpus / cpus_per_gres;
			sock_gres->total_cnt = MIN(i, sock_gres->total_cnt);
			if ((gres_js->gres_per_node > sock_gres->total_cnt) ||
			    (gres_js->gres_per_task > sock_gres->total_cnt)) {
				*max_tasks_this_node = 0;
			}
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_per_sock);
	xfree(req_sock);
	xfree(socket_index);

	if (!has_cpus_per_gres &&
	    ((mc_ptr->cpus_per_task > 1) ||
	     !(slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE))) {
		/*
		 * Only adjust *avail_cpus for the maximum task count if
		 * cpus_per_task is explicitly set. There is currently no way
		 * to tell if cpus_per_task==1 is explicitly set by the job
		 * when SelectTypeParameters includes CR_ONE_TASK_PER_CORE.
		 */
		*avail_cpus = MIN(*avail_cpus,
				  *max_tasks_this_node * mc_ptr->cpus_per_task);
	}
}

/*
 * Select one specific GRES topo entry (set GRES bitmap) for this job on this
 *	node based upon per-node resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _pick_specific_topo(struct job_resources *job_res, int node_inx,
				int job_node_inx, sock_gres_t *sock_gres,
				uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, rc, s, t;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int *used_sock = NULL, alloc_gres_cnt = 0;
	uint64_t gres_per_bit;
	bool use_busy_dev = gres_use_busy_dev(sock_gres->gres_state_node, 0);

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_per_bit = gres_js->gres_per_node;
	gres_ns = sock_gres->gres_state_node->gres_data;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				break;
			}
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * First: Try to select a GRES local to allocated socket with
	 *	sufficient resources.
	 * Second: Use available GRES with sufficient resources.
	 * Third: Use any available GRES.
	 */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     (s < sock_cnt) && (alloc_gres_cnt == 0); s++) {
		if ((s >= 0) && !used_sock[s])
			continue;
		for (t = 0; t < gres_ns->topo_cnt; t++) {
			if (use_busy_dev &&
			    (gres_ns->topo_gres_cnt_alloc[t] == 0))
				continue;
			if (gres_ns->topo_gres_cnt_alloc    &&
			    gres_ns->topo_gres_cnt_avail    &&
			    ((gres_ns->topo_gres_cnt_avail[t] -
			      gres_ns->topo_gres_cnt_alloc[t]) <
			     gres_per_bit))
				continue;	/* Insufficient resources */
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, t)))
				continue;  /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], t)))
				continue;   /* GRES not on this socket */
			bit_set(gres_js->gres_bit_select[node_inx], t);
			gres_js->gres_cnt_node_select[node_inx] +=
				gres_per_bit;
			alloc_gres_cnt += gres_per_bit;
			break;
		}
	}

	/* Select available GRES with sufficient resources */
	for (t = 0; (t < gres_ns->topo_cnt) && (alloc_gres_cnt == 0); t++) {
		if (use_busy_dev &&
		    (gres_ns->topo_gres_cnt_alloc[t] == 0))
			continue;
		if (gres_ns->topo_gres_cnt_alloc    &&
		    gres_ns->topo_gres_cnt_avail    &&
		    gres_ns->topo_gres_cnt_avail[t] &&
		    ((gres_ns->topo_gres_cnt_avail[t] -
		      gres_ns->topo_gres_cnt_alloc[t]) < gres_per_bit))
			continue;	/* Insufficient resources */
		bit_set(gres_js->gres_bit_select[node_inx], t);
		gres_js->gres_cnt_node_select[node_inx] += gres_per_bit;
		alloc_gres_cnt += gres_per_bit;
		break;
	}

	/* Select available GRES with any resources */
	for (t = 0; (t < gres_ns->topo_cnt) && (alloc_gres_cnt == 0); t++) {
		if (gres_ns->topo_gres_cnt_alloc    &&
		    gres_ns->topo_gres_cnt_avail    &&
		    gres_ns->topo_gres_cnt_avail[t])
			continue;	/* No resources */
		bit_set(gres_js->gres_bit_select[node_inx], t);
		gres_js->gres_cnt_node_select[node_inx] += gres_per_bit;
		alloc_gres_cnt += gres_per_bit;
	}

	xfree(used_sock);
}

/*
 * Return count of sockets allocated to this job on this node
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * RET socket count
 */
static int _get_sock_cnt(struct job_resources *job_res, int node_inx,
			 int job_node_inx)
{
	int core_offset, used_sock_cnt = 0;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, rc, s;

	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count", __func__);
		return 1;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset", __func__);
		return 1;
	}
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i)))
				used_sock_cnt++;
		}
	}
	if (used_sock_cnt == 0) {
		error("%s: No allocated cores found", __func__);
		return 1;
	}
	return used_sock_cnt;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-socket resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_sock_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, l, rc, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int *used_sock = NULL, used_sock_cnt = 0;
	int *links_cnt = NULL, best_link_cnt = 0;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				used_sock_cnt++;
				break;
			}
		}
	}
	if (tres_mc_ptr && tres_mc_ptr->sockets_per_node     &&
	    (tres_mc_ptr->sockets_per_node != used_sock_cnt) &&
	    gres_ns->gres_bit_alloc && sock_gres->bits_by_sock) {
		if (tres_mc_ptr->sockets_per_node > used_sock_cnt) {
			/* Somehow we have too few sockets in job allocation */
			error("%s: Inconsistent requested/allocated socket count "
			      "(%d > %d) for job %u on node %d",
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
			debug("%s: Inconsistent requested/allocated socket count "
			      "(%d < %d) for job %u on node %d",
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

	/*
	 * Identify the available GRES with best connectivity
	 * (i.e. higher link_cnt)
	 */
	if (gres_ns->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		for (g = 0; g < gres_cnt; g++) {
			if (bit_test(gres_ns->gres_bit_alloc, g))
				continue;
			for (l = 0; l < gres_cnt; l++) {
				if ((l == g) ||
				    bit_test(gres_ns->gres_bit_alloc, l))
					continue;
				links_cnt[l] += gres_ns->links_cnt[g][l];
			}
		}
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * Try to use GRES with best connectivity (higher link_cnt values)
	 */
	for (s = 0; s < sock_cnt; s++) {
		if (!used_sock[s])
			continue;
		i = 0;
		for (l = best_link_cnt;
		     ((l >= 0) && (i < gres_js->gres_per_socket)); l--) {
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_by_sock ||
				    !sock_gres->bits_by_sock[s] ||
				    !bit_test(sock_gres->bits_by_sock[s], g))
					continue;  /* GRES not on this socket */
				if (gres_ns->gres_bit_alloc &&
				    bit_test(gres_ns->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				if (gres_js->gres_bit_select[node_inx] &&
				    bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				if (++i == gres_js->gres_per_socket)
					break;
			}
		}
		if ((i < gres_js->gres_per_socket) &&
		    sock_gres->bits_any_sock) {
			/* Add GRES unconstrained by socket as needed */
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_any_sock ||
				    !bit_test(sock_gres->bits_any_sock, g))
					continue;  /* GRES not on this socket */
				if (gres_ns->gres_bit_alloc &&
				    bit_test(gres_ns->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				if (gres_js->gres_bit_select[node_inx] &&
				    bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				if (++i == gres_js->gres_per_socket)
					break;
			}
		}
	}
	xfree(links_cnt);
	xfree(used_sock);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use only socket-local GRES
 * job_res IN - job resource allocation
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
static int _set_job_bits1(struct job_resources *job_res, int node_inx,
			  int job_node_inx, int rem_nodes,
			  sock_gres_t *sock_gres, uint32_t job_id,
			  gres_mc_data_t *tres_mc_ptr, uint16_t cpus_per_core)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, rc, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int *cores_on_sock = NULL, alloc_gres_cnt = 0;
	int max_gres, pick_gres, total_cores = 0;
	int fini = 0;
	uint16_t cpus_per_gres = 0;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	if (gres_js->gres_per_job == gres_js->total_gres)
		fini = 1;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}
	xassert(job_res->core_bitmap);
	if (job_node_inx == 0)
		gres_js->total_gres = 0;
	max_gres = gres_js->gres_per_job - gres_js->total_gres -
		(rem_nodes - 1);
	cores_on_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				cores_on_sock[s]++;
				total_cores++;
			}
		}
	}
	if (gres_js->cpus_per_gres) {
		cpus_per_gres = gres_js->cpus_per_gres;
	} else if (gres_js->ntasks_per_gres &&
		   (gres_js->ntasks_per_gres != NO_VAL16)) {
		cpus_per_gres = gres_js->ntasks_per_gres *
			tres_mc_ptr->cpus_per_task;
	}
	if (cpus_per_gres) {
		max_gres = MIN(max_gres,
			       ((total_cores * cpus_per_core) /
				cpus_per_gres));
	}
	if ((max_gres > 1) && (gres_ns->link_len == gres_cnt))
		pick_gres  = NO_VAL16;
	else
		pick_gres = max_gres;
	/*
	 * Now pick specific GRES for these sockets.
	 * First select all GRES that we might possibly use, starting with
	 * those not constrained by socket, then contrained by socket.
	 * Then remove those which are not required and not "best".
	 */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     ((s < sock_cnt) && (alloc_gres_cnt < pick_gres)); s++) {
		if ((s >= 0) && !cores_on_sock[s])
			continue;
		for (g = 0; ((g < gres_cnt) && (alloc_gres_cnt < pick_gres));
		     g++) {
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;   /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(gres_ns->gres_bit_alloc, g) ||
			    bit_test(gres_js->gres_bit_select[node_inx], g))
				continue;   /* Already allocated GRES */
			bit_set(gres_js->gres_bit_select[node_inx], g);
			gres_js->gres_cnt_node_select[node_inx]++;
			alloc_gres_cnt++;
			gres_js->total_gres++;
		}
	}
	if (alloc_gres_cnt == 0) {
		for (s = 0; ((s < sock_cnt) && (alloc_gres_cnt == 0)); s++) {
			if (cores_on_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_by_sock ||
				    !sock_gres->bits_by_sock[s] ||
				    !bit_test(sock_gres->bits_by_sock[s], g))
					continue; /* GRES not on this socket */
				if (bit_test(gres_ns->gres_bit_alloc, g) ||
				    bit_test(gres_js->
					     gres_bit_select[node_inx], g))
					continue; /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				alloc_gres_cnt++;
				gres_js->total_gres++;
				break;
			}
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
			gres_js->total_gres--;
		}
	}

	xfree(cores_on_sock);
	if (gres_js->total_gres >= gres_js->gres_per_job)
		fini = 1;
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use any GRES on the node
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 * RET 0:more work, 1:fini
 */
static int _set_job_bits2(struct job_resources *job_res, int node_inx,
			  int job_node_inx, sock_gres_t *sock_gres,
			  uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int i, g, l, rc, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int fini = 0;
	int best_link_cnt = 0, best_inx = -1;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	if (gres_js->gres_per_job == gres_js->total_gres) {
		fini = 1;
		return fini;
	}
	if (!gres_js->gres_bit_select ||
	    !gres_js->gres_bit_select[node_inx]) {
		error("%s: gres_bit_select NULL for job %u on node %d",
		      __func__, job_id, node_inx);
		return SLURM_ERROR;
	}
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	/*
	 * Identify the GRES (if any) that we want to use as a basis for
	 * maximizing link count (connectivity of the GRES).
	 */
	xassert(job_res->core_bitmap);
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	if ((gres_js->gres_per_job > gres_js->total_gres) &&
	    (gres_ns->link_len == gres_cnt)) {
		for (g = 0; g < gres_cnt; g++) {
			if (!bit_test(gres_js->gres_bit_select[node_inx], g))
				continue;
			best_inx = g;
			for (s = 0; s < gres_cnt; s++) {
				best_link_cnt = MAX(gres_ns->links_cnt[s][g],
						    best_link_cnt);
			}
			break;
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * Start with GRES available from any socket, then specific sockets
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (gres_js->gres_per_job > gres_js->total_gres));
	     l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) &&
		      (gres_js->gres_per_job > gres_js->total_gres)); s++) {
			for (g = 0;
			     ((g < gres_cnt) &&
			      (gres_js->gres_per_job >gres_js->total_gres));
			     g++) {
				if ((l > 0) &&
				    (gres_ns->links_cnt[best_inx][g] < l))
					continue;   /* Want better link count */
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;  /* GRES not avail any sock */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(gres_ns->gres_bit_alloc, g) ||
				    bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				gres_js->total_gres++;
			}
		}
	}
	if (gres_js->gres_per_job == gres_js->total_gres)
		fini = 1;
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-node resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_node_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, l, rc, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int *used_sock = NULL, alloc_gres_cnt = 0;
	int *links_cnt = NULL, best_link_cnt = 0;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				break;
			}
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * First: Try to place one GRES per socket in this job's allocation.
	 * Second: Try to place additional GRES on allocated sockets.
	 * Third: Use any additional available GRES.
	 */
	if (gres_ns->link_len == gres_cnt)
		links_cnt = xcalloc(gres_cnt, sizeof(int));
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     ((s < sock_cnt) && (alloc_gres_cnt < gres_js->gres_per_node));
	     s++) {
		if ((s >= 0) && !used_sock[s])
			continue;
		for (g = 0; g < gres_cnt; g++) {
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;   /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(gres_js->gres_bit_select[node_inx], g) ||
			    bit_test(gres_ns->gres_bit_alloc, g))
				continue;   /* Already allocated GRES */
			bit_set(gres_js->gres_bit_select[node_inx], g);
			gres_js->gres_cnt_node_select[node_inx]++;
			alloc_gres_cnt++;
			for (l = 0; links_cnt && (l < gres_cnt); l++) {
				if ((l == g) ||
				    bit_test(gres_ns->gres_bit_alloc, l))
					continue;
				links_cnt[l] += gres_ns->links_cnt[g][l];
			}
			break;
		}
	}

	if (links_cnt) {
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Try to place additional GRES on allocated sockets. Favor use of
	 * GRES which are best linked to GRES which have already been selected.
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (alloc_gres_cnt < gres_js->gres_per_node)); l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) &&
		      (alloc_gres_cnt < gres_js->gres_per_node)); s++) {
			if ((s >= 0) && !used_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;/* GRES not avail any socket */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g) ||
				    bit_test(gres_ns->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				alloc_gres_cnt++;
				if (alloc_gres_cnt >= gres_js->gres_per_node)
					break;
			}
		}
	}

	/*
	 * Use any additional available GRES. Again, favor use of GRES
	 * which are best linked to GRES which have already been selected.
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (alloc_gres_cnt < gres_js->gres_per_node)); l--) {
		for (s = 0;
		     ((s < sock_cnt) &&
		      (alloc_gres_cnt < gres_js->gres_per_node)); s++) {
			if (used_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if (!sock_gres->bits_by_sock ||
				    !sock_gres->bits_by_sock[s] ||
				    !bit_test(sock_gres->bits_by_sock[s], g))
					continue;  /* GRES not on this socket */
				if (bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g) ||
				    bit_test(gres_ns->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],
					g);
				gres_js->gres_cnt_node_select[node_inx]++;
				alloc_gres_cnt++;
				if (alloc_gres_cnt >= gres_js->gres_per_node)
					break;
			}
		}
	}

	xfree(links_cnt);
	xfree(used_sock);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-task resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * sock_gres IN - job/node request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_task_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr,
			   uint32_t **tasks_per_node_socket)
{
	uint16_t sock_cnt = 0;
	int gres_cnt, g, l, s;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	uint32_t total_tasks = 0;
	uint64_t total_gres_cnt = 0, total_gres_goal;
	int *links_cnt = NULL, best_link_cnt = 0;

	gres_js = sock_gres->gres_state_job->gres_data;
	gres_ns = sock_gres->gres_state_node->gres_data;
	sock_cnt = sock_gres->sock_cnt;
	gres_cnt = bit_size(gres_js->gres_bit_select[node_inx]);
	if (gres_ns->link_len == gres_cnt)
		links_cnt = xcalloc(gres_cnt, sizeof(int));

	/* First pick GRES for acitve sockets */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     s < sock_cnt; s++) {
		if ((s > 0) &&
		    (!tasks_per_node_socket[node_inx] ||
		     (tasks_per_node_socket[node_inx][s] == 0)))
			continue;
		total_tasks += tasks_per_node_socket[node_inx][s];
		total_gres_goal = total_tasks * gres_js->gres_per_task;
		for (g = 0; g < gres_cnt; g++) {
			if (total_gres_cnt >= total_gres_goal)
				break;
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;  /* GRES not avail any sock */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(gres_ns->gres_bit_alloc, g))
				continue;   /* Already allocated GRES */
			if (bit_test(gres_js->gres_bit_select[node_inx], g))
				continue;   /* Already allocated GRES */
			bit_set(gres_js->gres_bit_select[node_inx], g);
			gres_js->gres_cnt_node_select[node_inx]++;
			total_gres_cnt++;
			for (l = 0; links_cnt && (l < gres_cnt); l++) {
				if ((l == g) ||
				    bit_test(gres_ns->gres_bit_alloc, l))
					continue;
				links_cnt[l] += gres_ns->links_cnt[g][l];
			}
		}
	}

	if (links_cnt) {
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Next pick additional GRES as needed. Favor use of GRES which
	 * are best linked to GRES which have already been selected.
	 */
	total_gres_goal = total_tasks * gres_js->gres_per_task;
	for (l = best_link_cnt;
	     ((l >= 0) && (total_gres_cnt < total_gres_goal)); l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) && (total_gres_cnt < total_gres_goal));
		     s++) {
			for (g = 0;
			     ((g < gres_cnt) &&
			      (total_gres_cnt < total_gres_goal)); g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;  /* GRES not avail any sock */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(gres_ns->gres_bit_alloc, g) ||
				    bit_test(gres_js->
					     gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(gres_js->gres_bit_select[node_inx],g);
				gres_js->gres_cnt_node_select[node_inx]++;
				total_gres_cnt++;
			}
		}
	}
	xfree(links_cnt);

	if (total_gres_cnt < total_gres_goal) {
		/* Something bad happened on task layout for this GRES type */
		error("%s: Insufficient gres/%s allocated for job %u on node_inx %u "
		      "(%"PRIu64" < %"PRIu64")",  __func__,
		      sock_gres->gres_state_job->gres_name, job_id, node_inx,
		      total_gres_cnt, total_gres_goal);
	}
}

/* Build array to identify task count for each node-socket pair */
static uint32_t **_build_tasks_per_node_sock(struct job_resources *job_res,
					     uint8_t overcommit,
					     gres_mc_data_t *tres_mc_ptr)
{
	uint32_t **tasks_per_node_socket;
	int i, i_first, i_last, j, node_cnt, job_node_inx = 0;
	int c, s, core_offset;
	int cpus_per_task = 1, cpus_per_node, cpus_per_core;
	int task_per_node_limit = 0;
	int32_t rem_tasks, excess_tasks;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;

	rem_tasks = tres_mc_ptr->ntasks_per_job;
	node_cnt = bit_size(job_res->node_bitmap);
	tasks_per_node_socket = xcalloc(node_cnt, sizeof(uint32_t *));
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		int tasks_per_node = 0;
		if (!bit_test(job_res->node_bitmap, i))
			continue;
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
		} else if (job_res->tasks_per_node &&
			   job_res->tasks_per_node[job_node_inx]) {
			task_per_node_limit =
				job_res->tasks_per_node[job_node_inx];
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
			if (tres_mc_ptr->cpus_per_task)
				cpus_per_task = tres_mc_ptr->cpus_per_task;
			else
				cpus_per_task = 1;
			task_per_node_limit = cpus_per_node / cpus_per_task;
		}
		core_offset = get_job_resources_offset(job_res, job_node_inx++,
						       0, 0);
		cpus_per_core = node_record_table_ptr[i]->tpc;
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
		for (i = i_first; (rem_tasks > 0) && (i <= i_last); i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
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
	if (rem_tasks > 0)	/* This should never happen */
		error("%s: rem_tasks not zero (%d > 0)", __func__, rem_tasks);

	return tasks_per_node_socket;
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
static uint32_t _get_task_cnt_node(uint32_t **tasks_per_node_socket,
				   int node_inx, int sock_cnt)
{
	uint32_t task_cnt = 0;

	if (!tasks_per_node_socket || !tasks_per_node_socket[node_inx]) {
		error("%s: tasks_per_node_socket is NULL", __func__);
		return 1;	/* Best guess if no data structure */
	}

	for (int s = 0; s < sock_cnt; s++)
		task_cnt += tasks_per_node_socket[node_inx][s];

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

/*
 * Make final GRES selection for the job
 * sock_gres_list IN - per-socket GRES details, one record per allocated node
 * job_id IN - job ID for logging
 * job_res IN - job resource allocation
 * overcommit IN - job's ability to overcommit resources
 * tres_mc_ptr IN - job's multi-core options
 * RET SLURM_SUCCESS or error code
 */
extern int gres_select_filter_select_and_set(List *sock_gres_list,
					     uint32_t job_id,
					     struct job_resources *job_res,
					     uint8_t overcommit,
					     gres_mc_data_t *tres_mc_ptr)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns;
	int i, i_first, i_last, node_inx = -1, gres_cnt;
	int node_cnt, rem_node_cnt;
	int job_fini = -1;	/* -1: not applicable, 0: more work, 1: fini */
	uint32_t **tasks_per_node_socket = NULL;
	int rc = SLURM_SUCCESS;

	if (!job_res || !job_res->node_bitmap)
		return SLURM_ERROR;

	node_cnt = bit_size(job_res->node_bitmap);
	rem_node_cnt = bit_set_count(job_res->node_bitmap);
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		node_record_t *node_ptr;
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		sock_gres_iter =
			list_iterator_create(sock_gres_list[++node_inx]);
		node_ptr = node_record_table_ptr[i];
		while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))){
			gres_js = sock_gres->gres_state_job->gres_data;
			gres_ns = sock_gres->gres_state_node->gres_data;
			if (!gres_js || !gres_ns)
				continue;
			if (gres_js->gres_per_task &&	/* Data needed */
			    !tasks_per_node_socket) {	/* Not built yet */
				tasks_per_node_socket =
					_build_tasks_per_node_sock(
						job_res,
						overcommit,
						tres_mc_ptr);
			}
			if (gres_js->total_node_cnt == 0) {
				gres_js->total_node_cnt = node_cnt;
				gres_js->total_gres = 0;
			}
			if (!gres_js->gres_cnt_node_select) {
				gres_js->gres_cnt_node_select =
					xcalloc(node_cnt, sizeof(uint64_t));
			}
			if (i == i_first)	/* Reinitialize counter */
				gres_js->total_gres = 0;

			if (gres_ns->topo_cnt == 0) {
				/* No topology, just set a count */
				if (gres_js->gres_per_node) {
					gres_js->gres_cnt_node_select[i] =
						gres_js->gres_per_node;
				} else if (gres_js->gres_per_socket) {
					gres_js->gres_cnt_node_select[i] =
						gres_js->gres_per_socket;
					gres_js->gres_cnt_node_select[i] *=
						_get_sock_cnt(job_res, i,
							      node_inx);
				} else if (gres_js->gres_per_task) {
					gres_js->gres_cnt_node_select[i] =
						gres_js->gres_per_task;
					gres_js->gres_cnt_node_select[i] *=
						_get_task_cnt_node(
							tasks_per_node_socket,
							i,
							node_ptr->tot_sockets);
				} else if (gres_js->gres_per_job) {
					gres_js->gres_cnt_node_select[i] =
						_get_job_cnt(sock_gres,
							     gres_ns,
							     rem_node_cnt);
				}
				gres_js->total_gres +=
					gres_js->gres_cnt_node_select[i];
				continue;
			}

			/* Working with topology, need to pick specific GRES */
			if (!gres_js->gres_bit_select) {
				gres_js->gres_bit_select =
					xcalloc(node_cnt, sizeof(bitstr_t *));
			}
			gres_cnt = _get_gres_node_cnt(gres_ns, node_inx);
			FREE_NULL_BITMAP(gres_js->gres_bit_select[i]);
			gres_js->gres_bit_select[i] = bit_alloc(gres_cnt);
			gres_js->gres_cnt_node_select[i] = 0;

			if (gres_js->gres_per_node &&
			    gres_id_shared(
				    sock_gres->gres_state_job->config_flags)) {
				/* gres/mps: select specific topo bit for job */
				_pick_specific_topo(job_res, i, node_inx,
						    sock_gres, job_id,
						    tres_mc_ptr);
			} else if (gres_js->gres_per_node) {
				_set_node_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr);
			} else if (gres_js->gres_per_socket) {
				_set_sock_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr);
			} else if (gres_js->gres_per_task) {
				_set_task_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr,
					       tasks_per_node_socket);
			} else if (gres_js->gres_per_job) {
				job_fini = _set_job_bits1(
					job_res, i, node_inx,
					rem_node_cnt, sock_gres,
					job_id, tres_mc_ptr,
					node_ptr->tpc);
			} else {
				error("%s job %u job_spec lacks GRES counter",
				      __func__, job_id);
			}
			if (job_fini == -1) {
				/*
				 * _set_job_bits1() updates total_gres counter,
				 * this handle other cases.
				 */
				gres_js->total_gres +=
					gres_js->gres_cnt_node_select[i];
			}
		}
		rem_node_cnt--;
		list_iterator_destroy(sock_gres_iter);
	}

	if (job_fini == 0) {
		/*
		 * Need more GRES to satisfy gres-per-job option with bitmaps.
		 * This logic will make use of GRES that are not on allocated
		 * sockets and are thus generally less desirable to use.
		 */
		node_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			sock_gres_iter = list_iterator_create(
				sock_gres_list[++node_inx]);
			while ((sock_gres = (sock_gres_t *)
				list_next(sock_gres_iter))) {
				if (!sock_gres->gres_state_job->gres_data ||
				    !sock_gres->gres_state_node->gres_data)
					continue;
				job_fini = _set_job_bits2(job_res, i, node_inx,
							  sock_gres, job_id,
							  tres_mc_ptr);
				if (job_fini == 1)
					break;
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
