/*****************************************************************************\
 *  gres_filter.c - Filters used on gres to determine order of nodes for job.
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

#include "gres_filter.h"

static uint16_t *avail_cores_per_sock = NULL;

static uint64_t _shared_gres_task_limit(gres_job_state_t *gres_js,
					bool use_total_gres,
					gres_node_state_t *gres_ns)
{
	int task_limit = 0, cnt;
	for (int i = 0; i < gres_ns->topo_cnt; i++)
	{
		if (gres_js->type_id &&
		    gres_js->type_id != gres_ns->topo_type_id[i])
			continue;

		cnt = gres_ns->topo_gres_cnt_avail[i];

		if (!use_total_gres)
			cnt -= gres_ns->topo_gres_cnt_alloc[i];

		if ((slurm_conf.select_type_param & MULTIPLE_SHARING_GRES_PJ))
			task_limit += cnt / gres_js->gres_per_task;
		else
			task_limit = MAX(task_limit,
					 (cnt / gres_js->gres_per_task));
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
	return (avail_cores_per_sock[*(int *)y] -
		avail_cores_per_sock[*(int *)x]);
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

	return weight1 - weight2;
}

extern void gres_filter_sock_core(job_record_t *job_ptr,
				  gres_mc_data_t *mc_ptr,
				  List sock_gres_list,
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
				  uint16_t cr_type)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	int tot_core_cnt = 0;
	uint32_t task_cnt_incr;
	bool *req_sock; /* Required socket */
	int *socket_index; /* Socket indexes */
	bool has_cpus_per_gres = false;
	int removed_tasks, efctv_cpt;

	xassert(mc_ptr->cpus_per_task);

	*min_cores_this_node = NO_VAL;

	if (*max_tasks_this_node == 0)
		return;

	xassert(avail_core);
	avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	for (int s = 0; s < sockets; s++) {
		int start_core = s * cores_per_socket;
		int end_core = start_core + cores_per_socket;
		avail_cores_per_sock[s] = bit_set_count_range(avail_core,
							      start_core,
							      end_core);
		tot_core_cnt += avail_cores_per_sock[s];
	}

	task_cnt_incr = *min_tasks_this_node;
	req_sock = xcalloc(sockets, sizeof(bool));
	socket_index = xcalloc(sockets, sizeof(int));

	list_sort(sock_gres_list, _sock_gres_sort);
	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = list_next(sock_gres_iter))) {
		gres_job_state_t *gres_js;
		bool sufficient_gres;
		uint64_t cnt_avail_total, max_tasks;
		uint64_t max_gres = 0, rem_gres = 0;
		uint16_t avail_cores_tot = 0;
		uint16_t cpus_per_gres = 0;
		int min_core_cnt, req_cores, rem_sockets, req_sock_cnt = 0;
		int threads_per_core;

		/*
		 * sock_gres->total_cnt is a value used by gres_sched_add
		 * it may be decreased by gres_select_filter_sock_core
		 * in first_pass, but in 2nd pass we should start
		 * from the value set by gres_select_filter_remove_unusable
		 */
		if (first_pass && !sock_gres->total_cnt_before_filter)
			sock_gres->total_cnt_before_filter =
				sock_gres->total_cnt;
		else
			sock_gres->total_cnt =
				sock_gres->total_cnt_before_filter;

		if (mc_ptr->threads_per_core)
			threads_per_core =
				MIN(cpus_per_core,
				    mc_ptr->threads_per_core);
		else
			threads_per_core = cpus_per_core;

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
			mc_ptr->cpus_per_task;
		min_core_cnt = (min_core_cnt + cpus_per_core - 1) /
			cpus_per_core;

		if (gres_js->cpus_per_gres) {
			cpus_per_gres = gres_js->cpus_per_gres;
			has_cpus_per_gres = true;
		} else if (gres_js->ntasks_per_gres &&
			 (gres_js->ntasks_per_gres != NO_VAL16)) {
			cpus_per_gres = gres_js->ntasks_per_gres *
				mc_ptr->cpus_per_task;
		} else if (gres_js->def_cpus_per_gres) {
			cpus_per_gres = gres_js->def_cpus_per_gres;
			has_cpus_per_gres = true;
		} else if (first_pass &&
			   !(gres_id_shared(sock_gres->gres_state_job->
					    config_flags))) {
			_estimate_cpus_per_gres(mc_ptr->ntasks_per_job,
						gres_js->gres_per_job,
						mc_ptr->cpus_per_task,
						&cpus_per_gres);
			/*
			 * Reservations (job_id == 0) are core based, so if we
			 * are dealing with GRES here we need to convert the
			 * DefCPUPerGPU to be cores instead of cpus.
			 */
			if (!job_ptr->job_id) {
				cpus_per_gres =
					ROUNDUP(cpus_per_gres, cpus_per_core);
			}
		}

		/* Filter out unusable GRES by socket */
		cnt_avail_total = sock_gres->cnt_any_sock;
		sufficient_gres = false;
		for (int s = 0; s < sockets; s++)
			socket_index[s] = s;
		qsort(socket_index, sockets, sizeof(int),
		      _sort_sockets_by_avail_cores);

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
						int cnt;
						if (!bit_test(avail_core, i))
							continue;
						bit_clear(avail_core, i);

						avail_cores_per_sock[s]--;
						tot_core_cnt--;
						cnt = tot_core_cnt *
							cpus_per_core;
						if (cnt < *avail_cpus)
							*avail_cpus = cnt;
						if (tot_core_cnt <=
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
			if ((!sufficient_gres && cnt_avail_sock) ||
			    sock_gres->cnt_any_sock) {
				/*
				 * Mark the socked required only if it
				 * contributed to cnt_avail_total or we use
				 * GRES that is not bound to any socket
				 */
				req_sock[s] = true;
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
			if (gres_id_shared(
				    sock_gres->gres_state_job->config_flags))
				max_tasks = _shared_gres_task_limit(
					gres_js, sock_gres->use_total_gres,
					sock_gres->gres_state_node->gres_data);
			else
				max_tasks = cnt_avail_total /
					    gres_js->gres_per_task;

			*max_tasks_this_node = MIN(*max_tasks_this_node,
						   max_tasks);
		}

		if (gres_js->ntasks_per_gres) {
			max_tasks = cnt_avail_total * gres_js->ntasks_per_gres;
			*max_tasks_this_node = MIN(*max_tasks_this_node,
						   max_tasks);
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

			if (gres_cpus <
			    (*min_tasks_this_node * mc_ptr->cpus_per_task)) {
				/*
				 * cpus_per_gres may end up requesting fewer
				 * cpus than tasks on the node. In this case,
				 * ignore cpus_per_gres and instead set
				 * max_tasks to min_tasks.
				 */
				*max_tasks_this_node = *min_tasks_this_node;
			} else {
				uint32_t gres_tasks;

				/* Truncate: round down */
				gres_tasks = gres_cpus / mc_ptr->cpus_per_task;
				*max_tasks_this_node =
					MIN(*max_tasks_this_node, gres_tasks);
			}
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
		if ((req_sock_cnt != sockets) &&
		    (enforce_binding || first_pass)) {
			for (int s = 0; s < sockets; s++) {
				if (req_sock[s])
					continue;
				for (int c = cores_per_socket - 1; c >= 0; c--) {
					int i = (s * cores_per_socket) + c;
					int cnt;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);

					avail_cores_tot--;
					avail_cores_per_sock[s]--;

					cnt = avail_cores_tot * cpus_per_core;
					if (cnt < *avail_cpus)
						*avail_cpus = cnt;

				}
			}
		}

		if (*max_tasks_this_node == NO_VAL) {
			if (cpus_per_gres) {
				int i = *avail_cpus / cpus_per_gres;
				sock_gres->total_cnt =
					MIN(i, sock_gres->total_cnt);
			}
			log_flag(SELECT_TYPE, "Node %s: max_tasks_this_node is set to NO_VAL, won't clear non-needed cores",
				 node_name);
			continue;
		}
		if (*max_tasks_this_node < *min_tasks_this_node) {
			error("%s: Node %s: min_tasks_this_node:%u > max_tasks_this_node:%u",
			      __func__,
			      node_name,
			      *min_tasks_this_node,
			      *max_tasks_this_node);
		}

		/*
		 * Determine how many cores are needed for this job.
		 * Consider rounding errors if cpus_per_task not divisible
		 * by cpus_per_core
		 */
		req_cores = *max_tasks_this_node;
		removed_tasks = 0;
		efctv_cpt = mc_ptr->cpus_per_task;

		if ((mc_ptr->ntasks_per_core == 1) &&
		    (efctv_cpt % threads_per_core)) {
			efctv_cpt /= threads_per_core;
			efctv_cpt++;
			efctv_cpt *= threads_per_core;
		}

		req_cores *= efctv_cpt;

		while (*max_tasks_this_node >= *min_tasks_this_node) {
			/* round up by full threads per core */
			req_cores = ROUNDUP(req_cores, threads_per_core);
			if (req_cores <= avail_cores_tot) {
				if (removed_tasks)
					log_flag(SELECT_TYPE, "Node %s: settings required_cores=%d by max_tasks_this_node=%u(reduced=%d) cpus_per_task=%d cpus_per_core=%d threads_per_core:%d",
						 node_name,
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
			req_cores *= efctv_cpt;
		}
		if (cpus_per_gres) {
			int i;
			if (gres_js->gres_per_node) {
				i = gres_js->gres_per_node;
				log_flag(SELECT_TYPE, "Node %s: estimating req_cores gres_per_node=%"PRIu64,
					 node_name,
					 gres_js->gres_per_node);
			} else if (gres_js->gres_per_socket) {
				i = gres_js->gres_per_socket * req_sock_cnt;
				log_flag(SELECT_TYPE, "Node %s: estimating req_cores gres_per_socket=%"PRIu64,
					 node_name,
					 gres_js->gres_per_socket);
			} else if (gres_js->gres_per_task) {
				i = gres_js->gres_per_task *
					*max_tasks_this_node;
				log_flag(SELECT_TYPE, "Node %s: estimating req_cores max_tasks_this_node=%u gres_per_task=%"PRIu64,
					 node_name,
					 *max_tasks_this_node,
					 gres_js->gres_per_task);
			} else if (cnt_avail_total) {
				i = cnt_avail_total;
				log_flag(SELECT_TYPE, "Node %s: estimating req_cores cnt_avail_total=%"PRIu64,
					 node_name,
					 cnt_avail_total);
			} else {
				i = 1;
				log_flag(SELECT_TYPE, "Node %s: estimating req_cores default to 1 task",
					 node_name);
			}
			i *= cpus_per_gres;
			/* max tasks is based on cpus */
			*max_tasks_this_node = MIN(i, *max_tasks_this_node);
			i = (i + cpus_per_core - 1) / cpus_per_core;
			if (req_cores < i)
				log_flag(SELECT_TYPE, "Node %s: Increasing req_cores=%d from cpus_per_gres=%d cpus_per_core=%u",
					 node_name,
					 i, cpus_per_gres, cpus_per_core);
			req_cores = MAX(req_cores, i);
		}
		/*
		 * Ensure that the number required cores is at least equal to
		 * the number of required sockets if enforce-binding.
		 */
		if (enforce_binding && (req_cores < req_sock_cnt)) {
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
				 node_name,
				 req_cores, avail_cores_tot);
			*max_tasks_this_node = 0;
			break;
		}

		/*
		 * Only reject if enforce_binding=true, since a job may be able
		 * to run on fewer cores than required by GRES if
		 * enforce_binding=false.
		 */
		if (enforce_binding &&
		    ((req_cores * threads_per_core) > *avail_cpus)) {
			log_flag(SELECT_TYPE, "Job cannot run on node %s: avail_cpus=%u < %u (required cores %u * threads_per_core %u",
				 node_name,
				 *avail_cpus, req_cores * threads_per_core,
				 req_cores, threads_per_core);
			*max_tasks_this_node = 0;
			break;
		}

		/*
		 * Clear extra avail_core bits on sockets we don't need
		 * up to required number of cores based on max_tasks_this_node.
		 * In case of enforce-binding those are already cleared.
		 */
		if (!(cr_type & CR_SOCKET) &&
		    (avail_cores_tot > req_cores) &&
		    !enforce_binding && !first_pass &&
		    (req_sock_cnt != sockets)) {
			for (int s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cores)
					break;
				if (req_sock[s])
					continue;
				for (int c = cores_per_socket - 1; c >= 0; c--) {
					int i = (s * cores_per_socket) + c;
					int cnt;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);

					avail_cores_tot--;
					avail_cores_per_sock[s]--;

					cnt = avail_cores_tot * cpus_per_core;
					if (cnt < *avail_cpus)
						*avail_cpus = cnt;

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
		while (!(cr_type & CR_SOCKET) &&
		       (req_sock_cnt && (avail_cores_tot > req_cores))) {
			int full_socket = -1;
			for (int s = 0; s < sockets; s++) {
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
			for (int c = cores_per_socket - 1; c >= 0; c--) {
				int i = (full_socket * cores_per_socket) + c;
				int cnt;
				if (!bit_test(avail_core, i))
					continue;
				bit_clear(avail_core, i);

				avail_cores_per_sock[full_socket]--;
				avail_cores_tot--;

				cnt = avail_cores_tot * cpus_per_core;
				if (cnt < *avail_cpus)
					*avail_cpus = cnt;

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

		/*
		 * Set a minimum required core count to fulfill the job's
		 * cpus_per_gres request or enforce_binding. Without
		 * enforce_binding a job may run on fewer cores than required
		 * for optimal binding.
		 */
		if (enforce_binding || has_cpus_per_gres)
			*min_cores_this_node =
				MIN(*min_cores_this_node, req_cores);
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

	if (!(*max_tasks_this_node) || (*min_cores_this_node == NO_VAL))
		*min_cores_this_node = 0;
}
