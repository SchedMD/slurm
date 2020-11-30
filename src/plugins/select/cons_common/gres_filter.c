/*****************************************************************************\
 *  gres_filter.c - filters used in the select plugin
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

#include "gres_filter.h"

static uint32_t gpu_plugin_id = NO_VAL, mps_plugin_id = NO_VAL;

static void _job_core_filter(void *job_gres_data, void *node_gres_data,
			     bool use_total_gres, bitstr_t *core_bitmap,
			     int core_start_bit, int core_end_bit,
			     char *node_name,
			     uint32_t plugin_id)
{
	int i, j, core_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *) job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_busy_dev = false;

	if (!node_gres_ptr->topo_cnt || !core_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_per_node)		/* No job GRES */
		return;

	if (mps_plugin_id == NO_VAL)
		mps_plugin_id = gres_plugin_build_id("mps");

	if (!use_total_gres &&
	    (plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/* Determine which specific cores can be used */
	avail_core_bitmap = bit_copy(core_bitmap);
	bit_nclear(avail_core_bitmap, core_start_bit, core_end_bit);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i]))
			continue;
		if (use_busy_dev &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i]) {
			FREE_NULL_BITMAP(avail_core_bitmap);	/* No filter */
			return;
		}
		core_ctld = core_end_bit - core_start_bit + 1;
		gres_validate_node_cores(node_gres_ptr, core_ctld, node_name);
		core_ctld = bit_size(node_gres_ptr->topo_core_bitmap[i]);
		for (j = 0; j < core_ctld; j++) {
			if (bit_test(node_gres_ptr->topo_core_bitmap[i], j)) {
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
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all GRES resources as available,
 *		       and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores
 *                      (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first cores
 * IN core_end_bit - index into core_bitmap for this node's last cores
 */
extern void gres_filter_cons_res(List job_gres_list, List node_gres_list,
				 bool use_total_gres,
				 bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *node_name)
{
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if ((job_gres_list == NULL) || (core_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(core_bitmap, core_start_bit, core_end_bit);
		return;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		node_gres_ptr = list_find_first(node_gres_list, gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack resources required by the job */
			bit_nclear(core_bitmap, core_start_bit, core_end_bit);
			break;
		}

		_job_core_filter(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data,
				 use_total_gres, core_bitmap,
				 core_start_bit, core_end_bit,
				 node_name,
				 job_gres_ptr->plugin_id);
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

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list - list of sock_gres_t entries built by
 *                     gres_plugin_job_test2()
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
extern int gres_filter_remove_unusable(List sock_gres_list,
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

	if (gpu_plugin_id == NO_VAL)
		gpu_plugin_id = gres_plugin_build_id("gpu");

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		uint64_t min_gres = 1, tmp_u64;
		if (sock_gres->job_specs) {
			gres_job_state_t *job_gres_ptr = sock_gres->job_specs;
			if (whole_node)
				min_gres = sock_gres->total_cnt;
			else if (job_gres_ptr->gres_per_node)
				min_gres = job_gres_ptr-> gres_per_node;
			if (job_gres_ptr->gres_per_socket) {
				tmp_u64 = job_gres_ptr->gres_per_socket;
				if (sock_per_node != NO_VAL)
					tmp_u64 *= sock_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
			if (job_gres_ptr->gres_per_task) {
				tmp_u64 = job_gres_ptr->gres_per_task;
				if (task_per_node != NO_VAL16)
					tmp_u64 *= task_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
		}
		if (!sock_gres->job_specs)
			cpus_per_gres = 0;
		else if (sock_gres->job_specs->cpus_per_gres)
			cpus_per_gres = sock_gres->job_specs->cpus_per_gres;
		else if (sock_gres->job_specs->ntasks_per_gres &&
			 (sock_gres->job_specs->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = sock_gres->job_specs->ntasks_per_gres *
				cpus_per_task;
		else
			cpus_per_gres = sock_gres->job_specs->def_cpus_per_gres;
		if (cpus_per_gres) {
			max_gres = max_cpus / cpus_per_gres;
			if ((max_gres == 0) ||
			    (sock_gres->job_specs->gres_per_node > max_gres) ||
			    (sock_gres->job_specs->gres_per_task > max_gres) ||
			    (sock_gres->job_specs->gres_per_socket > max_gres)){
				log_flag(GRES, "%s: Insufficient CPUs for any GRES: max_gres (%"PRIu64") = max_cpus (%d) / cpus_per_gres (%d)",
					 __func__, max_gres, max_cpus,
					 cpus_per_gres);
				rc = -1;
				break;
			}
		}
		if (!sock_gres->job_specs)
			mem_per_gres = 0;
		else if (sock_gres->job_specs->mem_per_gres)
			mem_per_gres = sock_gres->job_specs->mem_per_gres;
		else
			mem_per_gres = sock_gres->job_specs->def_mem_per_gres;
		if (mem_per_gres && avail_mem) {
			if (mem_per_gres <= avail_mem) {
				sock_gres->max_node_gres = avail_mem /
							   mem_per_gres;
			} else {
				log_flag(GRES, "%s: Insufficient memory for any GRES: mem_per_gres (%"PRIu64") > avail_mem (%"PRIu64")",
					 __func__, mem_per_gres, avail_mem);
				rc = -1;
				break;
			}
		}
		if (sock_gres->cnt_by_sock || enforce_binding) {
			if (!avail_cores_by_sock) {
				avail_cores_by_sock =_build_avail_cores_by_sock(
							core_bitmap, sockets,
							cores_per_sock);
			}
		}
		/*
		 * NOTE: gres_per_socket enforcement is performed by
		 * _build_sock_gres_by_topo(), called by gres_plugin_job_test2()
		 */
		if (sock_gres->cnt_by_sock && enforce_binding) {
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
			}
			near_gres_cnt = sock_gres->total_cnt;
		} else if (sock_gres->cnt_by_sock) { /* NO enforce_binding */
			near_gres_cnt = sock_gres->total_cnt;
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					near_gres_cnt -=
						sock_gres->cnt_by_sock[s];
				}
			}
		} else {
			near_gres_cnt = sock_gres->total_cnt;
		}
		if (sock_gres->job_specs && !whole_node &&
		    sock_gres->job_specs->gres_per_node) {
			if ((sock_gres->max_node_gres == 0) ||
			    (sock_gres->max_node_gres >
			     sock_gres->job_specs->gres_per_node)) {
				sock_gres->max_node_gres =
					sock_gres->job_specs->gres_per_node;
			}
		}
		/* Avoid max_node_gres with ntasks_per_gres and whole node */
		if (cpus_per_gres &&
		    ((sock_gres->job_specs->ntasks_per_gres == NO_VAL16) ||
		     !whole_node)) {
			int cpu_cnt;
			cpu_cnt = bit_set_count(core_bitmap);
			cpu_cnt *= cpus_per_core;
			max_gres = cpu_cnt / cpus_per_gres;
			if (max_gres == 0) {
				log_flag(GRES, "%s: max_gres == 0 == cpu_cnt (%d) / cpus_per_gres (%d)",
					 __func__, cpu_cnt, cpus_per_gres);
				rc = -1;
				break;
			} else if ((sock_gres->max_node_gres == 0) ||
				   (sock_gres->max_node_gres > max_gres)) {
				sock_gres->max_node_gres = max_gres;
			}
		}
		if (mem_per_gres) {
			max_gres = avail_mem / mem_per_gres;
			sock_gres->total_cnt = MIN(sock_gres->total_cnt,
						   max_gres);
		}
		if ((sock_gres->total_cnt < min_gres) ||
		    ((sock_gres->max_node_gres != 0) &&
		     (sock_gres->max_node_gres < min_gres))) {
			log_flag(GRES, "%s: min_gres (%"PRIu64") is > max_node_gres (%"PRIu64") or sock_gres->total_cnt (%"PRIu64")",
				 __func__, min_gres, sock_gres->max_node_gres,
				 sock_gres->total_cnt);
			rc = -1;
			break;
		}

		if (sock_gres->plugin_id == gpu_plugin_id) {
			 *avail_gpus += sock_gres->total_cnt;
			if (sock_gres->max_node_gres &&
			    (sock_gres->max_node_gres < near_gres_cnt))
				near_gres_cnt = sock_gres->max_node_gres;
			if (*near_gpus < 0xff)	/* avoid overflow */
				*near_gpus += near_gres_cnt;
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
	int weight1 = 0, weight2 = 0;

	if (sock_gres1->node_specs && !sock_gres1->node_specs->topo_cnt)
		weight1 += 0x02;
	if (sock_gres1->job_specs && !sock_gres1->job_specs->gres_per_socket)
		weight1 += 0x01;

	if (sock_gres2->node_specs && !sock_gres2->node_specs->topo_cnt)
		weight2 += 0x02;
	if (sock_gres2->job_specs && !sock_gres2->job_specs->gres_per_socket)
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
 * IN sock_gres_list - list of sock_gres_t entries built by gres_plugin_job_test2()
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
extern void gres_filter_sock_core(gres_mc_data_t *mc_ptr,
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
	gres_job_state_t *job_specs;
	int i, j, c, s, sock_cnt = 0, req_cores, rem_sockets, full_socket;
	int tot_core_cnt = 0, min_core_cnt = 1;
	uint64_t cnt_avail_sock, cnt_avail_total, max_gres = 0, rem_gres = 0;
	uint64_t tot_gres_sock, max_tasks;
	uint32_t task_cnt_incr;
	bool *req_sock = NULL;	/* Required socket */
	int *socket_index = NULL; /* Socket indexes */
	uint16_t *avail_cores_per_sock, cpus_per_gres;
	uint16_t avail_cores_tot;

	if (*max_tasks_this_node == 0)
		return;

	xassert(avail_core);
	avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	for (s = 0; s < sockets; s++) {
		for (c = 0; c < cores_per_socket; c++) {
			i = (s * cores_per_socket) + c;
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
		bool sufficient_gres;
		job_specs = sock_gres->job_specs;
		if (!job_specs)
			continue;
		if (job_specs->gres_per_job &&
		    (job_specs->total_gres < job_specs->gres_per_job)) {
			rem_gres = job_specs->gres_per_job -
				   job_specs->total_gres;
		}

		/*
		 * gres_plugin_job_core_filter2() sets sock_gres->max_node_gres
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
		    ((job_specs->gres_per_node > max_gres) ||
		     ((job_specs->gres_per_socket * rem_sockets) > max_gres))) {
			*max_tasks_this_node = 0;
			break;
		}
		if (job_specs->gres_per_node && job_specs->gres_per_task) {
			max_tasks = job_specs->gres_per_node /
				    job_specs->gres_per_task;
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

		if (job_specs->cpus_per_gres)
			cpus_per_gres = job_specs->cpus_per_gres;
		else if (job_specs->ntasks_per_gres &&
			 (job_specs->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = job_specs->ntasks_per_gres *
				mc_ptr->cpus_per_task;
		else
			cpus_per_gres = job_specs->def_cpus_per_gres;

		/* Filter out unusable GRES by socket */
		avail_cores_tot = 0;
		cnt_avail_total = sock_gres->cnt_any_sock;
		sufficient_gres = false;
		for (s = 0; s < sockets; s++)
			socket_index[s] = s;
		qsort_r(socket_index, sockets, sizeof(int),
			_sort_sockets_by_avail_cores, avail_cores_per_sock);

		for (j = 0; j < sockets; j++) {
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
			if ((job_specs->gres_per_socket > tot_gres_sock) ||
			    (tot_gres_sock == 0)) {
				/*
				 * Insufficient GRES on this socket
				 * GRES removed here won't be used in 2nd pass
				 */
				if (((job_specs->gres_per_socket >
				      tot_gres_sock) ||
				     enforce_binding) &&
				    sock_gres->cnt_by_sock) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
				if (first_pass &&
				    (tot_core_cnt > min_core_cnt)) {
					for (c = cores_per_socket - 1;
					     c >= 0; c--) {
						i = (s * cores_per_socket) + c;
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

			if (job_specs->gres_per_node &&
			    (cnt_avail_total >= job_specs->gres_per_node) &&
			    !sock_gres->cnt_any_sock) {
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
			max_gres = *avail_cpus / cpus_per_gres;
			cnt_avail_total = MIN(cnt_avail_total, max_gres);
		}
		if ((cnt_avail_total == 0) ||
		    (job_specs->gres_per_node > cnt_avail_total) ||
		    (job_specs->gres_per_task > cnt_avail_total)) {
			*max_tasks_this_node = 0;
		}
		if (job_specs->gres_per_task) {
			max_tasks = cnt_avail_total / job_specs->gres_per_task;
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
			for (s = 0; s < sockets; s++) {
				if (req_sock[s])
					continue;
				for (c = cores_per_socket - 1; c >= 0; c--) {
					i = (s * cores_per_socket) + c;
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
				i = *avail_cpus / cpus_per_gres;
				sock_gres->total_cnt =
					MIN(i, sock_gres->total_cnt);
			}
			log_flag(GRES, "%s: max_tasks_this_node is set to NO_VAL, won't clear non-needed cores",
				 __func__);
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
		req_cores = *max_tasks_this_node;
		if (mc_ptr->cpus_per_task) {
			int threads_per_core, removed_tasks = 0;

			if (mc_ptr->threads_per_core)
				threads_per_core =
					MIN(cpus_per_core,
					    mc_ptr->threads_per_core);
			else
				threads_per_core = cpus_per_core;

			req_cores *= mc_ptr->cpus_per_task;

			while (*max_tasks_this_node >= *min_tasks_this_node) {
				/* round up by full threads per core */
				req_cores += threads_per_core - 1;
				req_cores /= threads_per_core;
				if (req_cores <= avail_cores_tot) {
					if (removed_tasks)
						log_flag(GRES, "%s: settings required_cores=%d by max_tasks_this_node=%u(reduced=%d) cpus_per_task=%d cpus_per_core=%d threads_per_core:%d",
							 __func__,
							 req_cores,
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
				req_cores = *max_tasks_this_node;
			}
		}
		if (cpus_per_gres) {
			if (job_specs->gres_per_node) {
				i = job_specs->gres_per_node;
				log_flag(GRES, "%s: estimating req_cores gres_per_node=%"PRIu64,
					 __func__, job_specs->gres_per_node);
			} else if (job_specs->gres_per_socket) {
				i = job_specs->gres_per_socket * sock_cnt;
				log_flag(GRES, "%s: estimating req_cores gres_per_socket=%"PRIu64,
					 __func__, job_specs->gres_per_socket);
			} else if (job_specs->gres_per_task) {
				i = job_specs->gres_per_task *
				    *max_tasks_this_node;
				log_flag(GRES, "%s: estimating req_cores max_tasks_this_node=%u gres_per_task=%"PRIu64,
					 __func__,
					 *max_tasks_this_node,
					 job_specs->gres_per_task);
			} else if (cnt_avail_total) {
				i = cnt_avail_total;
				log_flag(GRES, "%s: estimating req_cores cnt_avail_total=%"PRIu64,
					 __func__, cnt_avail_total);
			} else {
				i = 1;
				log_flag(GRES, "%s: estimating req_cores default to 1 task",
					 __func__);
			}
			i *= cpus_per_gres;
			i = (i + cpus_per_core - 1) / cpus_per_core;
			if (req_cores < i)
				log_flag(GRES, "%s: Increasing req_cores=%d from cpus_per_gres=%d cpus_per_core=%"PRIu16,
					 __func__, i, cpus_per_gres,
					 cpus_per_core);
			req_cores = MAX(req_cores, i);
		}

		if (req_cores > avail_cores_tot) {
			log_flag(GRES, "%s: Job cannot run on node req_cores:%d > aval_cores_tot:%d",
				 __func__, req_cores, avail_cores_tot);
			*max_tasks_this_node = 0;
			break;
		}

		/*
		 * Clear extra avail_core bits on sockets we don't need
		 * up to required number of cores based on max_tasks_this_node.
		 * In case of enforce-binding those are already cleared.
		 */
		if ((avail_cores_tot > req_cores) &&
		     !enforce_binding && !first_pass) {
			for (s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cores)
					break;
				if (req_sock[s])
					continue;
				for (c = cores_per_socket - 1; c >= 0; c--) {
					i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);
					if (bit_set_count(avail_core) *
					    cpus_per_core < *avail_cpus) {
						*avail_cpus -= cpus_per_core;
					}
					avail_cores_tot--;
					avail_cores_per_sock[s]--;
					if (avail_cores_tot == req_cores)
						break;
				}
			}
		}

		/*
		 * Clear extra avail_core bits on sockets we do need, but
		 * spread them out so that every socket has some cores
		 * available to use with the nearby GRES that we do need.
		 */
		while (avail_cores_tot > req_cores) {
			full_socket = -1;
			for (s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cores)
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
			s = full_socket;
			for (c = cores_per_socket - 1; c >= 0; c--) {
				i = (s * cores_per_socket) + c;
				if (!bit_test(avail_core, i))
					continue;
				bit_clear(avail_core, i);
				if (bit_set_count(avail_core) * cpus_per_core <
				    *avail_cpus) {
					*avail_cpus -= cpus_per_core;
				}
				avail_cores_per_sock[s]--;
				avail_cores_tot--;
				break;
			}
		}
		if (cpus_per_gres) {
			i = *avail_cpus / cpus_per_gres;
			sock_gres->total_cnt = MIN(i, sock_gres->total_cnt);
			if ((job_specs->gres_per_node > sock_gres->total_cnt) ||
			    (job_specs->gres_per_task > sock_gres->total_cnt)) {
				*max_tasks_this_node = 0;
			}
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_per_sock);
	xfree(req_sock);
	xfree(socket_index);

	if ((mc_ptr->cpus_per_task > 1) ||
	    !(slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE)) {
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
