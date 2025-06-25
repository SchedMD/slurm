/*****************************************************************************\
 *  gres_filter.c - Filters used on gres to determine order of nodes for job.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "gres_filter.h"

typedef struct {
	bitstr_t *avail_core; /* cores available on this node, UPDATED */
	uint16_t *avail_cpus; /* Count of available CPUs on the node, UPDATED */
	uint16_t cores_per_socket; /* Count of cores per socket on the node */
	uint16_t cpus_per_core; /* Count of CPUs per core on the node */
	uint16_t cr_type;
	bool enforce_binding; /* GRES must be co-allocated with cores */
	bool first_pass; /* set if first scheduling attempt for this job,
			  * use co-located GRES and cores if possible */
	bool has_cpus_per_gres;
	job_record_t *job_ptr; /* job's pointer */
	uint32_t *max_tasks_this_node; /* Max tasks that can start on this node
					* or NO_VAL, UPDATED */
	gres_mc_data_t *mc_ptr; /* job's multi-core specs, NO_VAL and INFINITE
				 * mapped to zero */
	uint32_t *min_cores_this_node;
	uint32_t *min_tasks_this_node; /* Min tasks that can start on this node,
					* UPDATED */
	int node_i;
	char *node_name; /* name of the node */
	int rem_nodes; /* node count left to allocate, including this node */
	uint16_t res_cores_per_gpu;
	uint16_t *res_cores_per_sock;
	bool *req_sock; /* Required socket */
	uint16_t sockets; /* Count of sockets on the node */
	int *socket_index; /* Socket indexes */
	uint32_t task_cnt_incr; /* original value of min_tasks_this_node */
	int tot_core_cnt;
} foreach_gres_filter_sock_core_args_t;

static uint16_t *avail_cores_per_sock = NULL;

static uint64_t _shared_gres_task_limit(gres_job_state_t *gres_js,
					bool use_total_gres,
					bool one_task_sharing,
					gres_node_state_t *gres_ns)
{
	int task_limit = 0, cnt, task_cnt;
	gres_node_state_t *alt_gres_ns =
		gres_ns->alt_gres ? gres_ns->alt_gres->gres_data : NULL;

	for (int i = 0; i < gres_ns->topo_cnt; i++)
	{
		if (gres_js->type_id &&
		    gres_js->type_id != gres_ns->topo_type_id[i])
			continue;
		if (!use_total_gres &&
		    alt_gres_ns && alt_gres_ns->gres_bit_alloc &&
		    gres_ns->topo_gres_bitmap && gres_ns->topo_gres_bitmap[i] &&
		    bit_overlap_any(gres_ns->topo_gres_bitmap[i],
				    alt_gres_ns->gres_bit_alloc))
			continue; /* Skip alt gres that are currently used */
		cnt = gres_ns->topo_gres_cnt_avail[i];

		if (!use_total_gres)
			cnt -= gres_ns->topo_gres_cnt_alloc[i];

		if (one_task_sharing)
			task_cnt = (cnt >= gres_js->gres_per_task) ? 1 : 0;
		else
			task_cnt = cnt / gres_js->gres_per_task;

		if (slurm_conf.select_type_param &
		    SELECT_MULTIPLE_SHARING_GRES_PJ)
			task_limit += task_cnt;
		else
			task_limit = MAX(task_limit, task_cnt);
	}
	return task_limit;
}

static void _estimate_cpus_per_gres(uint32_t ntasks_per_job,
				    uint64_t gres_per_job,
				    uint32_t cpus_per_task,
				    uint16_t *cpus_per_gres)
{
	if (!ntasks_per_job || (ntasks_per_job == NO_VAL) || !gres_per_job)
		return;

	if ((ntasks_per_job >= gres_per_job) &&
	    !(ntasks_per_job % gres_per_job)) {
		/*
		 * If we have more tasks than gres and tasks is multiple of
		 * gres we want to attempt placing tasks on CPUs on the same
		 * sockets as the GPU
		 */
		uint64_t tasks_per_gres = ntasks_per_job / gres_per_job;
		*cpus_per_gres = tasks_per_gres * cpus_per_task;
	} else if (!(gres_per_job % ntasks_per_job)) {
		/*
		 * If we have more gres than tasks, but gres is multiple of
		 * tasks we attempt symmetrical distribution of tasks
		 */
		uint64_t gres_per_task = gres_per_job / ntasks_per_job;
		if (!(cpus_per_task % gres_per_task)) {
			/*
			 * If cpus_per_task is multiple of gres_per_task we
			 * attempt giving each GPU same number of CPUs
			 * For instance --gpus=8 -n2 -c8 will result in
			 * first_pass attempting --cpus-per-gres=2, but
			 * in case of ---gpus=8 -n2 -c3 we don't attempt that
			 * since it's not well defined.
			 */
			*cpus_per_gres = cpus_per_task / gres_per_task;
		}
	}
}

static int _sort_sockets_by_avail_cores(const void *x, const void *y)
{
	return slurm_sort_uint16_list_desc(&avail_cores_per_sock[*(int *)x],
					   &avail_cores_per_sock[*(int *)y]);
}

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

	return slurm_sort_int_list_asc(&weight1, &weight2);
}

static uint64_t _set_max_gres(gres_job_state_t *gres_js,
			      sock_gres_t *sock_gres,
			      int rem_nodes,
			      int rem_sockets)
{
	uint64_t max_gres = 0, rem_gres = 0;

	if (gres_js->gres_per_job) {
		if (gres_js->total_gres < gres_js->gres_per_job) {
			rem_gres = gres_js->gres_per_job -
				gres_js->total_gres;
			/* At least one gres per node */
			rem_gres -= (rem_nodes - 1);
		} else {
			uint64_t min_socket_gres =
				gres_js->gres_per_socket * rem_sockets;
			/*
			* If gres_per_job has been met, satisfy other
			* per node conditions or at least one gres per
			* node.
			*/
			if (gres_js->gres_per_node)
				rem_gres = gres_js->gres_per_node;
			else {
				rem_gres = min_socket_gres;
				rem_gres = MAX(gres_js->gres_per_task,
					       rem_gres);
				rem_gres = MAX(1, rem_gres);
			}
		}
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
	return max_gres;
}

static void _reduce_restricted_cores(bitstr_t *avail_core,
				     uint16_t *avail_cpus,
				     uint16_t *avail_cores_tot,
				     uint16_t *res_core_tot,
				     bitstr_t *res_cores,
				     uint16_t *res_cores_per_sock,
				     uint64_t max_res_cores,
				     uint16_t sockets,
				     uint16_t cores_per_socket,
				     uint16_t cpus_per_core,
				     bool *req_sock,
				     bool enforce_binding,
				     bool first_pass)
{
	int cnt;

	/*
	 * In the case where cores have been reduced to
	 * min_core_cnt partial res_cores_per_gpu might be left.
	 */
	if (!enforce_binding && !first_pass) {
		for (int s = 0; s < sockets; s++) {
			if (*res_core_tot <= max_res_cores)
				break;
			if (req_sock[s] || !res_cores_per_sock[s])
				continue;
			for (int c = 0; c < cores_per_socket; c++) {
				int i = (s * cores_per_socket) + c;
				if (!bit_test(res_cores, i) ||
				    !bit_test(avail_core, i))
					continue;
				bit_clear(avail_core, i);
				avail_cores_per_sock[s]--;
				(*avail_cores_tot)--;
				(*res_core_tot)--;
				res_cores_per_sock[s]--;
				if (*res_core_tot <= max_res_cores)
					break;
			}

		}
	}
	for (int s = 0; s < sockets; s++) {
		if (*res_core_tot <= max_res_cores)
			break;
		if (!req_sock[s] || !res_cores_per_sock[s])
			continue;
		for (int c = 0; c < cores_per_socket; c++) {
			int i = (s * cores_per_socket) + c;
			if (!bit_test(res_cores, i) || !bit_test(avail_core, i))
				continue;
			bit_clear(avail_core, i);
			avail_cores_per_sock[s]--;
			(*avail_cores_tot)--;
			(*res_core_tot)--;
			res_cores_per_sock[s]--;
			if (*res_core_tot <= max_res_cores)
				break;
		}

	}

	cnt = *avail_cores_tot * cpus_per_core;
	if (*avail_cpus < cnt)
		*avail_cpus = cnt;
}

static int _foreach_gres_filter_sock_core(void *x, void *arg)
{
	sock_gres_t *sock_gres = x;
	foreach_gres_filter_sock_core_args_t *args = arg;
	int removed_tasks, efctv_cpt;
	gres_job_state_t *gres_js;
	bool sufficient_gres;
	uint64_t cnt_avail_total, max_tasks;
	uint64_t max_gres = 0;
	uint16_t avail_cores_tot = 0;
	uint16_t res_core_tot = 0;
	uint16_t cpus_per_gres = 0;
	int min_core_cnt, req_cores, rem_sockets, req_sock_cnt = 0;
	bool is_res_gpu = false;

	/*
	 * sock_gres->total_cnt is a value used by gres_sched_add
	 * it may be decreased by gres_select_filter_sock_core
	 * in first_pass, but in 2nd pass we should start
	 * from the value set by gres_select_filter_remove_unusable
	 */
	if (args->first_pass && !sock_gres->total_cnt_before_filter)
		sock_gres->total_cnt_before_filter = sock_gres->total_cnt;
	else if (sock_gres->total_cnt_before_filter)
		sock_gres->total_cnt = sock_gres->total_cnt_before_filter;

	if (!sock_gres->gres_state_job)
		return 0;
	gres_js = sock_gres->gres_state_job->gres_data;

	if (!(args->cr_type & SELECT_SOCKET) &&
	    (sock_gres->gres_state_job->plugin_id ==
	     gres_get_gpu_plugin_id()) &&
	    args->res_cores_per_gpu && gres_js->res_gpu_cores &&
	    gres_js->res_gpu_cores[args->node_i])
		is_res_gpu = true;

	if (is_res_gpu) {
		bitstr_t *res_cores =
			bit_copy(gres_js->res_gpu_cores[args->node_i]);
		bit_and(res_cores, args->avail_core);
		xfree(args->res_cores_per_sock);
		args->res_cores_per_sock =
			xcalloc(args->sockets, sizeof(uint16_t));
		for (int s = 0; s < args->sockets; s++) {
			int start_core = s * args->cores_per_socket;
			int end_core = start_core + args->cores_per_socket;
			(args->res_cores_per_sock)[s] =
				bit_set_count_range(res_cores, start_core,
						    end_core);
			res_core_tot += (args->res_cores_per_sock)[s];
		}
		FREE_NULL_BITMAP(res_cores);
	}

	args->rem_nodes = MAX(args->rem_nodes, 1);
	rem_sockets = MAX(1, args->mc_ptr->sockets_per_node);
	max_gres =
		_set_max_gres(gres_js, sock_gres, args->rem_nodes, rem_sockets);
	if (max_gres &&
	    ((gres_js->gres_per_node > max_gres) ||
	     ((gres_js->gres_per_socket * rem_sockets) > max_gres))) {
		*(args->max_tasks_this_node) = 0;
		return -1;
	}
	if (gres_js->gres_per_node && gres_js->gres_per_task) {
		max_tasks = gres_js->gres_per_node / gres_js->gres_per_task;
		if ((max_tasks == 0) ||
		    (max_tasks > *(args->max_tasks_this_node)) ||
		    (max_tasks < *(args->min_tasks_this_node))) {
			*(args->max_tasks_this_node) = 0;
			return -1;
		}
		if ((*(args->max_tasks_this_node) == NO_VAL) ||
		    (*(args->max_tasks_this_node) > max_tasks))
			*(args->max_tasks_this_node) = max_gres;
	}

	min_core_cnt = MAX(*(args->min_tasks_this_node), 1) *
		       args->mc_ptr->cpus_per_task;
	min_core_cnt = ROUNDUP(min_core_cnt, args->cpus_per_core);

	if (gres_js->cpus_per_gres) {
		cpus_per_gres = gres_js->cpus_per_gres;
		args->has_cpus_per_gres = true;
	} else if (gres_js->ntasks_per_gres &&
		   (gres_js->ntasks_per_gres != NO_VAL16)) {
		cpus_per_gres =
			gres_js->ntasks_per_gres * args->mc_ptr->cpus_per_task;
	} else if (gres_js->def_cpus_per_gres) {
		cpus_per_gres = gres_js->def_cpus_per_gres;
		args->has_cpus_per_gres = true;
	} else if (args->first_pass &&
		   !(gres_id_shared(sock_gres->gres_state_job->config_flags))) {
		_estimate_cpus_per_gres(args->mc_ptr->ntasks_per_job,
					gres_js->gres_per_job,
					args->mc_ptr->cpus_per_task,
					&cpus_per_gres);
		/*
		 * Reservations (job_id == 0) are core based, so if we
		 * are dealing with GRES here we need to convert the
		 * DefCPUPerGPU to be cores instead of cpus.
		 */
		if (!args->job_ptr->job_id) {
			cpus_per_gres =
				ROUNDUP(cpus_per_gres, args->cpus_per_core);
		}
	}

	/* Filter out unusable GRES by socket */
	cnt_avail_total = sock_gres->cnt_any_sock;
	sufficient_gres = false;
	for (int s = 0; s < args->sockets; s++)
		(args->socket_index)[s] = s;
	qsort(args->socket_index, args->sockets, sizeof(int),
	      _sort_sockets_by_avail_cores);

	for (int j = 0; j < args->sockets; j++) {
		uint64_t cnt_avail_sock, tot_gres_sock;
		int s;
		/*
		 * Test for sufficient gres_per_socket
		 *
		 * Start with socket with most cores available,
		 * so we know that we have max number of cores on socket
		 * with allocated GRES.
		 */
		s = (args->socket_index)[j];

		if (sock_gres->cnt_by_sock) {
			cnt_avail_sock = sock_gres->cnt_by_sock[s];
		} else
			cnt_avail_sock = 0;

		/*
		 * If enforce binding number of gres allocated per
		 * socket has to be limited by cpus_per_gres
		 */
		if ((args->enforce_binding || args->first_pass) &&
		    cpus_per_gres) {
			int max_gres_socket = ((avail_cores_per_sock[s] *
						args->cpus_per_core) /
					       cpus_per_gres);
			cnt_avail_sock = MIN(cnt_avail_sock, max_gres_socket);
		}

		tot_gres_sock = sock_gres->cnt_any_sock + cnt_avail_sock;
		if ((gres_js->gres_per_socket > tot_gres_sock) ||
		    (tot_gres_sock == 0)) {
			/* Insufficient GRES on this socket */
			if (((gres_js->gres_per_socket > tot_gres_sock) ||
			     args->enforce_binding) &&
			    sock_gres->cnt_by_sock) {
				sock_gres->total_cnt -=
					sock_gres->cnt_by_sock[s];
				sock_gres->cnt_by_sock[s] = 0;
			}
			if (args->first_pass &&
			    (args->tot_core_cnt > min_core_cnt)) {
				for (int c = args->cores_per_socket - 1; c >= 0;
				     c--) {
					int i = (s * args->cores_per_socket) +
						c;
					int cnt;
					if (!bit_test(args->avail_core, i))
						continue;
					bit_clear(args->avail_core, i);

					avail_cores_per_sock[s]--;
					args->tot_core_cnt--;
					cnt = args->tot_core_cnt *
						args->cpus_per_core;
					if (cnt < *(args->avail_cpus))
						*(args->avail_cpus) = cnt;
					if (is_res_gpu &&
					    bit_test(
						gres_js->
						res_gpu_cores[args->node_i],
						i)) {
						res_core_tot--;
						(args->res_cores_per_sock)[s]--;
					}
					if (args->tot_core_cnt <= min_core_cnt)
						break;
					if (!avail_cores_per_sock[s])
						break;
				}

				if (!avail_cores_per_sock[s]) {
					int start = s * args->cores_per_socket;
					int end = (s + 1) *
						args->cores_per_socket;
					bit_nclear(args->avail_core, start,
						   end - 1);
					if (is_res_gpu) {
						res_core_tot = 0;
						(args->res_cores_per_sock)[s] =
							0;
					}
				}
			}
		}

		avail_cores_tot += avail_cores_per_sock[s];
		/* Test for available cores on this socket */
		if ((args->enforce_binding || args->first_pass) &&
		    (avail_cores_per_sock[s] == 0))
			continue;

		cnt_avail_total += cnt_avail_sock;
		if ((!sufficient_gres && cnt_avail_sock) ||
		    sock_gres->cnt_any_sock) {
			/*
			 * Mark the socked required only if it
			 * contributed to cnt_avail_total or we use
			 * GRES that is not bound to any socket
			 */
			(args->req_sock)[s] = true;
			req_sock_cnt++;
		}

		if ((max_gres && (cnt_avail_total >= max_gres)) ||
		    (gres_js->gres_per_node &&
		     (cnt_avail_total >= gres_js->gres_per_node))) {
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
			max_gres = MIN((*(args->avail_cpus) / cpus_per_gres),
				       max_gres);
		else
			max_gres = *(args->avail_cpus) / cpus_per_gres;
		cnt_avail_total = MIN(cnt_avail_total, max_gres);
	}
	if (max_gres)
		cnt_avail_total = MIN(cnt_avail_total, max_gres);
	if (gres_js->gres_per_node)
		cnt_avail_total = MIN(gres_js->gres_per_node, cnt_avail_total);
	if ((cnt_avail_total == 0) ||
	    (gres_js->gres_per_node > cnt_avail_total) ||
	    (gres_js->gres_per_task > cnt_avail_total)) {
		*(args->max_tasks_this_node) = 0;
	}
	if (cpus_per_gres && cnt_avail_total) {
		uint32_t gres_cpus;

		/*
		 * Limit max_tasks_this_node per the cpus_per_gres
		 * request. req_cores is initialized to
		 * max_tasks_this_node, and req_cores needs to be
		 * limited by cpus_per_gres.
		 */
		gres_cpus = cpus_per_gres * cnt_avail_total;

		if ((gres_cpus < (*(args->min_tasks_this_node) *
				  args->mc_ptr->cpus_per_task)) ||
		    (gres_cpus < args->job_ptr->details->pn_min_cpus)) {
			/*
			 * cpus_per_gres may end up requesting fewer
			 * cpus than tasks on the node. In this case,
			 * ignore cpus_per_gres and instead set
			 * max_tasks to min_tasks.
			 */
			*(args->max_tasks_this_node) =
				*(args->min_tasks_this_node);
		} else {
			uint32_t gres_tasks;

			/* Truncate: round down */
			gres_tasks = gres_cpus / args->mc_ptr->cpus_per_task;
			*(args->max_tasks_this_node) =
				MIN(*(args->max_tasks_this_node), gres_tasks);
		}
	}
	if (gres_js->gres_per_task) {
		if (gres_id_shared(sock_gres->gres_state_job->config_flags))
			max_tasks = _shared_gres_task_limit(
				gres_js, sock_gres->use_total_gres,
				(args->job_ptr->bit_flags &
				 GRES_ONE_TASK_PER_SHARING),
				sock_gres->gres_state_node->gres_data);
		else
			max_tasks = cnt_avail_total / gres_js->gres_per_task;

		*(args->max_tasks_this_node) =
			MIN(*(args->max_tasks_this_node), max_tasks);
		/* Gres per node takes priority in selection */
		if (!gres_js->gres_per_node)
			cnt_avail_total = MIN((*(args->max_tasks_this_node) *
					       gres_js->gres_per_task),
					      cnt_avail_total);
	}

	if (gres_js->ntasks_per_gres &&
	    (gres_js->ntasks_per_gres != NO_VAL16)) {
		max_tasks = cnt_avail_total * gres_js->ntasks_per_gres;
		while (max_tasks > *(args->max_tasks_this_node)) {
			max_tasks -= gres_js->ntasks_per_gres;
			/* Gres per node takes priority in selection */
			if (!gres_js->gres_per_node)
				cnt_avail_total--;
		}
		*(args->max_tasks_this_node) =
			MIN(*(args->max_tasks_this_node), max_tasks);
	}

	/*
	 * min_tasks_this_node and max_tasks_this_node must be multiple
	 * of original min_tasks_this_node value. This is to support
	 * ntasks_per_* option and we just need to select a count of
	 * tasks, sockets, etc. Round the values down.
	 */
	*(args->min_tasks_this_node) =
		(*(args->min_tasks_this_node) / args->task_cnt_incr) *
		args->task_cnt_incr;
	*(args->max_tasks_this_node) =
		(*(args->max_tasks_this_node) / args->task_cnt_incr) *
		args->task_cnt_incr;

	if (*(args->max_tasks_this_node) == 0)
		return -1;

	/*
	 * Remove cores on not required sockets when enforce-binding,
	 * this has to happen also when max_tasks_this_node == NO_VAL
	 */
	if ((req_sock_cnt != args->sockets) &&
	    (args->enforce_binding || args->first_pass)) {
		for (int s = 0; s < args->sockets; s++) {
			if ((args->req_sock)[s])
				continue;
			for (int c = args->cores_per_socket - 1; c >= 0; c--) {
				int i = (s * args->cores_per_socket) + c;
				int cnt;
				if (!bit_test(args->avail_core, i))
					continue;
				bit_clear(args->avail_core, i);

				avail_cores_tot--;
				avail_cores_per_sock[s]--;

				cnt = avail_cores_tot * args->cpus_per_core;
				if (cnt < *(args->avail_cpus))
					*(args->avail_cpus) = cnt;

				if (res_core_tot &&
					bit_test(gres_js->
						res_gpu_cores[args->node_i],
						i)) {
					res_core_tot--;
					(args->res_cores_per_sock)[s]--;
				}

			}
		}
	}

	/*
	 * If the gres is a gpu and RestrictedCoresPerGPU is configured
	 * remove restricted cores that can not be used according to
	 * cnt_avail_total.
	 */
	if (is_res_gpu) {
		uint64_t max_res_cores;
		max_res_cores = cnt_avail_total * args->res_cores_per_gpu;

		_reduce_restricted_cores(args->avail_core, args->avail_cpus,
					 &avail_cores_tot, &res_core_tot,
					 gres_js->res_gpu_cores[args->node_i],
					 args->res_cores_per_sock,
					 max_res_cores, args->sockets,
					 args->cores_per_socket,
					 args->cpus_per_core, args->req_sock,
					 args->enforce_binding,
					 args->first_pass);
	}

	if (*(args->max_tasks_this_node) == NO_VAL) {
		if (cpus_per_gres) {
			int i = *(args->avail_cpus) / cpus_per_gres;
			sock_gres->total_cnt = MIN(i, sock_gres->total_cnt);
		}
		log_flag(SELECT_TYPE, "Node %s: max_tasks_this_node is set to NO_VAL, won't clear non-needed cores",
			 args->node_name);
		return 0;
	}
	if (*(args->max_tasks_this_node) < *(args->min_tasks_this_node)) {
		error("%s: Node %s: min_tasks_this_node:%u > max_tasks_this_node:%u",
		      __func__, args->node_name, *(args->min_tasks_this_node),
		      *(args->max_tasks_this_node));
	}

	/*
	 * Determine how many cores are needed for this job.
	 * Consider rounding errors if cpus_per_task not divisible
	 * by cpus_per_core
	 */
	req_cores = *(args->max_tasks_this_node);
	removed_tasks = 0;
	efctv_cpt = args->mc_ptr->cpus_per_task;

	if ((args->mc_ptr->ntasks_per_core == 1) &&
	    (efctv_cpt % args->cpus_per_core)) {
		efctv_cpt /= args->cpus_per_core;
		efctv_cpt++;
		efctv_cpt *= args->cpus_per_core;
	}

	req_cores *= efctv_cpt;

	while (*(args->max_tasks_this_node) >= *(args->min_tasks_this_node)) {
		/* round up by full threads per core */
		req_cores = ROUNDUP(req_cores, args->cpus_per_core);
		if (req_cores <= avail_cores_tot) {
			if (removed_tasks)
				log_flag(SELECT_TYPE, "Node %s: settings required_cores=%d by max_tasks_this_node=%u(reduced=%d) cpus_per_task=%d cpus_per_core=%d threads_per_core:%d",
					 args->node_name, req_cores,
					 *(args->max_tasks_this_node),
					 removed_tasks,
					 args->mc_ptr->cpus_per_task,
					 args->cpus_per_core,
					 args->mc_ptr->threads_per_core);
			break;
		}
		removed_tasks++;
		(*(args->max_tasks_this_node))--;
		req_cores = *(args->max_tasks_this_node);
		req_cores *= efctv_cpt;

		if (!gres_js->gres_per_node &&
		    (gres_js->gres_per_task ||
		     (gres_js->ntasks_per_gres &&
		      (gres_js->ntasks_per_gres != NO_VAL16)))) {
			uint32_t gres_limit;

			if (gres_js->gres_per_task)
				gres_limit = *(args->max_tasks_this_node) *
					gres_js->gres_per_task;
			else
				gres_limit = *(args->max_tasks_this_node) /
					gres_js->ntasks_per_gres;
			if (cnt_avail_total > gres_limit)
				cnt_avail_total = gres_limit;
			if (is_res_gpu) {
				uint32_t max_res_cores =
					cnt_avail_total *
					args->res_cores_per_gpu;
				_reduce_restricted_cores(
					args->avail_core, args->avail_cpus,
					&avail_cores_tot, &res_core_tot,
					gres_js->res_gpu_cores[args->node_i],
					args->res_cores_per_sock, max_res_cores,
					args->sockets, args->cores_per_socket,
					args->cpus_per_core, args->req_sock,
					args->enforce_binding,
					args->first_pass);
			}
		}
	}

	if (cpus_per_gres) {
		int i;
		if (gres_js->gres_per_node) {
			i = gres_js->gres_per_node;
			log_flag(SELECT_TYPE, "Node %s: estimating req_cores gres_per_node=%"PRIu64,
				 args->node_name, gres_js->gres_per_node);
		} else if (gres_js->gres_per_socket) {
			i = gres_js->gres_per_socket * req_sock_cnt;
			log_flag(SELECT_TYPE, "Node %s: estimating req_cores gres_per_socket=%"PRIu64,
				 args->node_name, gres_js->gres_per_socket);
		} else if (gres_js->gres_per_task) {
			i = gres_js->gres_per_task *
				*(args->max_tasks_this_node);
			log_flag(SELECT_TYPE, "Node %s: estimating req_cores max_tasks_this_node=%u gres_per_task=%"PRIu64,
				 args->node_name, *(args->max_tasks_this_node),
				 gres_js->gres_per_task);
		} else if (cnt_avail_total) {
			i = cnt_avail_total;
			log_flag(SELECT_TYPE, "Node %s: estimating req_cores cnt_avail_total=%"PRIu64,
				 args->node_name, cnt_avail_total);
		} else {
			i = 1;
			log_flag(SELECT_TYPE, "Node %s: estimating req_cores default to 1 task",
				 args->node_name);
		}
		i *= cpus_per_gres;
		/* max tasks is based on cpus */
		*(args->max_tasks_this_node) =
			MIN(i, *(args->max_tasks_this_node));
		i = ROUNDUP(i, args->cpus_per_core);
		if (req_cores < i)
			log_flag(SELECT_TYPE, "Node %s: Increasing req_cores=%d from cpus_per_gres=%d cpus_per_core=%u",
				 args->node_name, i, cpus_per_gres,
				 args->cpus_per_core);
		req_cores = MAX(req_cores, i);
	}
	/*
	 * Ensure that the number required cores is at least equal to
	 * the number of required sockets if enforce-binding.
	 */
	if (args->enforce_binding && (req_cores < req_sock_cnt)) {
		req_cores = req_sock_cnt;
	}

	/*
	 * Test against both avail_cores_tot and *avail_cpus.
	 *
	 * - avail_cores_tot: the number of cores that are available on
	 *   this node
	 * - *avail_cpus: the number of cpus the job can use on this
	 *   node based on the job constraints.
	 *
	 * For example, assume a node has 16 cores, 2 threads per core. and
	 * Assume that 4 cores are in use by other jobs. If a job's
	 * constraints only allow the job to use 2 cpus:
	 *
	 * avail_cores_tot is 12 (16 cores total minus 4 cores in use)
	 * *avail_cpus is 2
	 */
	if (req_cores > avail_cores_tot) {
		log_flag(SELECT_TYPE, "Job cannot run on node %s: req_cores:%d > aval_cores_tot:%d",
			 args->node_name, req_cores, avail_cores_tot);
		*(args->max_tasks_this_node) = 0;
		return -1;
	}

	/*
	 * Only reject if enforce_binding=true, since a job may be able
	 * to run on fewer cores than required by GRES if
	 * enforce_binding=false.
	 */
	if (args->enforce_binding &&
	    ((req_cores * args->cpus_per_core) > *(args->avail_cpus))) {
		log_flag(SELECT_TYPE, "Job cannot run on node %s: avail_cpus=%u < %u (required cores %u * cpus_per_core %u",
			 args->node_name, *(args->avail_cpus),
			 (req_cores * args->cpus_per_core), req_cores,
			 args->cpus_per_core);
		*(args->max_tasks_this_node) = 0;
		return -1;
	}

	/*
	 * Clear extra cores on sockets we don't need
	 * up to required number of cores based on max_tasks_this_node.
	 * In case of enforce-binding those are already cleared.
	 */
	if (!(args->cr_type & SELECT_SOCKET) && (avail_cores_tot > req_cores) &&
	    !args->enforce_binding && !args->first_pass &&
	    (req_sock_cnt != args->sockets)) {
		for (int s = 0; s < args->sockets; s++) {
			int cnt;
			int remove_cores = avail_cores_tot - req_cores;
			if (avail_cores_tot == req_cores)
				break;
			if ((args->req_sock)[s])
				continue;

			remove_cores =
				MIN(remove_cores, avail_cores_per_sock[s]);
			avail_cores_per_sock[s] -= remove_cores;
			avail_cores_tot -= remove_cores;
			cnt = avail_cores_tot * args->cpus_per_core;
			if (cnt < *(args->avail_cpus))
				*(args->avail_cpus) = cnt;

			if (!avail_cores_per_sock[s]) {
				int start = s * args->cores_per_socket;
				int end = (s + 1) * args->cores_per_socket;
				bit_nclear(args->avail_core, start, end - 1);
				if (is_res_gpu) {
					res_core_tot = 0;
					(args->res_cores_per_sock)[s] = 0;
				}
			}
		}
	}

	/*
	 * Clear extra avail_core bits on sockets we do need, but
	 * spread them out so that every socket has some cores
	 * available to use with the nearby GRES that we do need.
	 */
	while (!(args->cr_type & SELECT_SOCKET) &&
	       (req_sock_cnt && (avail_cores_tot > req_cores))) {
		int full_socket = -1;
		int cnt;
		for (int s = 0; s < args->sockets; s++) {
			if (avail_cores_tot == req_cores)
				break;
			if (!(args->req_sock)[s] ||
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
		avail_cores_per_sock[full_socket]--;
		avail_cores_tot--;

		cnt = avail_cores_tot * args->cpus_per_core;
		if (cnt < *(args->avail_cpus))
			*(args->avail_cpus) = cnt;

		if (!avail_cores_per_sock[full_socket]) {
			int start = full_socket * args->cores_per_socket;
			int end = (full_socket + 1) * args->cores_per_socket;
			bit_nclear(args->avail_core, start, end - 1);
			if (is_res_gpu) {
				res_core_tot = 0;
				(args->res_cores_per_sock)[full_socket] = 0;
			}
		}
	}
	if (cpus_per_gres) {
		int i = *(args->avail_cpus) / cpus_per_gres;
		sock_gres->total_cnt = MIN(i, sock_gres->total_cnt);
		if ((gres_js->gres_per_node > sock_gres->total_cnt) ||
		    (gres_js->gres_per_task > sock_gres->total_cnt)) {
			*(args->max_tasks_this_node) = 0;
		}
	}

	sock_gres->total_cnt = MIN(cnt_avail_total, sock_gres->total_cnt);

	/*
	 * Set a minimum required core count to fulfill the job's
	 * cpus_per_gres request or enforce_binding. Without
	 * enforce_binding a job may run on fewer cores than required
	 * for optimal binding.
	 */
	if (args->enforce_binding || args->has_cpus_per_gres)
		*(args->min_cores_this_node) =
			MIN(*(args->min_cores_this_node), req_cores);

	return 0;
}

extern void gres_filter_sock_core(job_record_t *job_ptr,
				  gres_mc_data_t *mc_ptr,
				  list_t *sock_gres_list,
				  uint16_t sockets,
				  uint16_t cores_per_socket,
				  uint16_t cpus_per_core,
				  uint16_t *avail_cpus,
				  uint32_t *min_tasks_this_node,
				  uint32_t *max_tasks_this_node,
				  uint32_t *min_cores_this_node,
				  int rem_nodes,
				  bool enforce_binding,
				  bool first_pass,
				  bitstr_t *avail_core,
				  char *node_name,
				  uint16_t cr_type,
				  uint16_t res_cores_per_gpu,
				  int node_i,
				  uint16_t **cores_per_sock_limit)
{
	foreach_gres_filter_sock_core_args_t args = {
		.job_ptr = job_ptr,
		.mc_ptr = mc_ptr,
		.sockets = sockets,
		.cores_per_socket = cores_per_socket,
		.cpus_per_core = cpus_per_core,
		.avail_cpus = avail_cpus,
		.min_tasks_this_node = min_tasks_this_node,
		.max_tasks_this_node = max_tasks_this_node,
		.min_cores_this_node = min_cores_this_node,
		.rem_nodes = rem_nodes,
		.enforce_binding = enforce_binding,
		.first_pass = first_pass,
		.avail_core = avail_core,
		.node_name = node_name,
		.cr_type = cr_type,
		.res_cores_per_gpu = res_cores_per_gpu,
		.node_i = node_i,
		.task_cnt_incr = *min_tasks_this_node,
	};

	xassert(mc_ptr->cpus_per_task);

	*min_cores_this_node = NO_VAL;

	if (*max_tasks_this_node == 0)
		return;

	if (mc_ptr->threads_per_core)
		args.cpus_per_core =
			MIN(cpus_per_core, mc_ptr->threads_per_core);

	xassert(avail_core);
	avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	for (int s = 0; s < sockets; s++) {
		int start_core = s * cores_per_socket;
		int end_core = start_core + cores_per_socket;
		avail_cores_per_sock[s] =
			bit_set_count_range(avail_core, start_core, end_core);
		args.tot_core_cnt += avail_cores_per_sock[s];
	}

	args.req_sock = xcalloc(sockets, sizeof(bool));
	args.socket_index = xcalloc(sockets, sizeof(int));

	list_sort(sock_gres_list, _sock_gres_sort);
	(void) list_for_each(sock_gres_list, _foreach_gres_filter_sock_core,
			     &args);

	xfree(args.req_sock);
	xfree(args.socket_index);
	xfree(args.res_cores_per_sock);

	if (*max_tasks_this_node) {
		if (*cores_per_sock_limit)
			xfree(*cores_per_sock_limit);
		*cores_per_sock_limit = avail_cores_per_sock;
		avail_cores_per_sock = NULL;
	}
	xfree(avail_cores_per_sock);

	if (!(*max_tasks_this_node) || (*min_cores_this_node == NO_VAL))
		*min_cores_this_node = 0;

	if (!args.has_cpus_per_gres &&
	    ((mc_ptr->cpus_per_task > 1) ||
	     !(slurm_conf.select_type_param & SELECT_ONE_TASK_PER_CORE))) {
		/*
		 * Only adjust *avail_cpus for the maximum task count if
		 * cpus_per_task is explicitly set. There is currently no way
		 * to tell if cpus_per_task==1 is explicitly set by the job
		 * when SelectTypeParameters includes SELECT_ONE_TASK_PER_CORE.
		 */

		*avail_cpus =
			MIN(*avail_cpus,
			    MAX(*max_tasks_this_node * mc_ptr->cpus_per_task,
				*min_cores_this_node * cpus_per_core));
	}
}
