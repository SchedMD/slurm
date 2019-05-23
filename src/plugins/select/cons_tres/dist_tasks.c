/*****************************************************************************\
 *  dist_tasks.c - Assign task count for each resource.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Derived in large part from select/cons_res plugin
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

#include "select_cons_tres.h"
#include "job_test.h"

/*
 * Max boards supported for best-fit across boards
 * Larger board configurations may require new algorithm for good performance
 */
#define MAX_BOARDS 8

/*
 * Combination counts
 * comb_counts[n-1][k-1] = number of combinations of
 *   k items from a set of n items
 *
 * Formula is n!/k!(n-k)!
 */
static uint32_t comb_counts[MAX_BOARDS][MAX_BOARDS] =
  {{1,0,0,0,0,0,0,0},
   {2,1,0,0,0,0,0,0},
   {3,3,1,0,0,0,0,0},
   {4,6,4,1,0,0,0,0},
   {5,10,10,5,1,0,0,0},
   {6,15,20,15,6,1,0,0},
   {7,21,35,35,21,7,1,0},
   {8,28,56,70,56,28,8,1}};
static int *sockets_core_cnt = NULL;

static void _block_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type);
static void _clear_spec_cores(struct job_record *job_ptr,
			      bitstr_t **core_array);
static int _cmp_int_ascend(const void *a, const void *b);
static int _cmp_int_descend(const void *a, const void *b);
static int _cmp_sock(const void *a, const void *b);
static int _compute_c_b_task_dist(struct job_record *job_ptr,
				  uint32_t *gres_task_limit);
static int _compute_plane_dist(struct job_record *job_ptr,
			       uint32_t *gres_task_limit);
static int _cyclic_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type, bool preempt_mode);
static void _gen_combs(int *comb_list, int n, int k);
static inline void _log_select_maps(char *loc, struct job_record *job_ptr);

/*
 * sync up core bitmap arrays with job_resources_t struct using a best-fit
 * approach on the available resources on each node
 *
 * "Best-fit" means:
 * 1st priority: Use smallest number of boards with sufficient
 *               available resources
 * 2nd priority: Use smallest number of sockets with sufficient
 *               available resources
 * 3rd priority: Use board combination with the smallest number
 *               of available resources
 * 4th priority: Use higher-numbered boards/sockets/cores first
 *
 * The job_resources_t struct can include threads based upon configuration
 */
static void _block_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type)
{
	uint32_t c, s, i, j, n, b, z, size, csize, core_cnt;
	uint16_t cpus, num_bits, vpus = 1;
	uint16_t cpus_per_task = job_ptr->details->cpus_per_task;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool alloc_cores = false, alloc_sockets = false;
	uint16_t ntasks_per_core = 0xffff;
	int tmp_cpt = 0;
	int count, core_min, b_min, elig, s_min, comb_idx, sock_idx;
	int elig_idx, comb_brd_idx, sock_list_idx, comb_min, board_num;
	int sock_per_comb;
	int *boards_core_cnt;
	int *sort_brds_core_cnt;
	int *board_combs;
	int *socket_list;
	int *elig_brd_combs;
	int *elig_core_cnt;
	bool *sockets_used;
	uint16_t boards_nb;
	uint16_t nboards_nb;
	uint16_t sockets_nb;
	uint16_t ncores_nb;
	uint16_t nsockets_nb;
	uint16_t sock_per_brd;
	uint16_t req_cores,best_fit_cores = 0;
	uint32_t best_fit_location = 0;
	uint64_t ncomb_brd;
	bool sufficient, best_fit_sufficient;

	if (!job_res)
		return;
	if (!job_res->core_bitmap) {
		error("%s: %s: core_bitmap for %pJ is NULL",
		      plugin_type, __func__, job_ptr);
		return;
	}
	if (bit_ffs(job_res->core_bitmap) == -1) {
		error("%s: %s: core_bitmap for %pJ has no bits set",
		      plugin_type, __func__, job_ptr);
		return;
	}

	if (cr_type & CR_SOCKET)
		alloc_sockets = true;
	else if (cr_type & CR_CORE)
		alloc_cores = true;

	if (job_ptr->details->mc_ptr) {
		multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			ntasks_per_core = mc_ptr->ntasks_per_core;
		}
	}

	size  = bit_size(job_res->node_bitmap);
	csize = bit_size(job_res->core_bitmap);

	sockets_nb  = select_node_record[0].sockets;
	sockets_core_cnt = xmalloc(sockets_nb * sizeof(int));
	sockets_used = xmalloc(sockets_nb * sizeof(bool));
	boards_nb = select_node_record[0].boards;
	boards_core_cnt = xmalloc(boards_nb * sizeof(int));
	sort_brds_core_cnt = xmalloc(boards_nb * sizeof(int));

	for (c = 0, i = 0, n = 0; n < size; n++) {
		if (!bit_test(job_res->node_bitmap, n))
			continue;

		core_cnt = 0;
		ncores_nb = select_node_record[n].cores;
		nsockets_nb = select_node_record[n].sockets;
		nboards_nb = select_node_record[n].boards;
		num_bits =  nsockets_nb * ncores_nb;

		if ((c + num_bits) > csize) {
			error("%s: %s index error", plugin_type, __func__);
			break;
		}

		cpus = job_res->cpus[i];
		vpus = vpus_per_core(job_ptr->details, n);

		/* compute still required cores on the node */
		req_cores = cpus / vpus;
		if (cpus % vpus)
			req_cores++;

		/*
		 * figure out core cnt if task requires more than one core and
		 * tasks_per_core is 1
		 */
		if ((ntasks_per_core == 1) &&
		    (cpus_per_task > vpus)) {
			/* how many cores a task will consume */
			int cores_per_task = (cpus_per_task + vpus - 1) / vpus;
			int tasks = cpus / cpus_per_task;
			req_cores = tasks * cores_per_task;
		}

		if (nboards_nb > MAX_BOARDS) {
			debug3("%s: %s: node[%u]: exceeds max boards; "
			       "doing best-fit across sockets only",
			       plugin_type, __func__, n);
			nboards_nb = 1;
		}

		if (nsockets_nb > sockets_nb) {
			sockets_nb = nsockets_nb;
			xrealloc(sockets_core_cnt, sockets_nb * sizeof(int));
			xrealloc(sockets_used,sockets_nb * sizeof(bool));
		}

		if (nboards_nb > boards_nb) {
			boards_nb = nboards_nb;
			xrealloc(boards_core_cnt, boards_nb * sizeof(int));
			xrealloc(sort_brds_core_cnt, boards_nb * sizeof(int));
		}

		/* Count available cores on each socket and board */
		if (nsockets_nb >= nboards_nb) {
			sock_per_brd = nsockets_nb / nboards_nb;
		} else {
			error("%s: %s: Node socket count lower than board count "
			      "(%u < %u), %pJ node %s",
			      plugin_type, __func__, nsockets_nb, nboards_nb,
			      job_ptr, node_record_table_ptr[n].name);
			sock_per_brd = 1;
		}
		for (b = 0; b < nboards_nb; b++) {
			boards_core_cnt[b] = 0;
			sort_brds_core_cnt[b] = 0;
		}
		for (s = 0; s < nsockets_nb; s++) {
			sockets_core_cnt[s] = 0;
			sockets_used[s] = false;
			b = s / sock_per_brd;
			for (j = c + (s * ncores_nb);
			     j < c + ((s+1) * ncores_nb); j++) {
				if (bit_test(job_res->core_bitmap, j)) {
					sockets_core_cnt[s]++;
					boards_core_cnt[b]++;
					sort_brds_core_cnt[b]++;
				}
			}
		}

		/* Sort boards in descending order of available core count */
		qsort(sort_brds_core_cnt, nboards_nb, sizeof(int),
		      _cmp_int_descend);
		/*
		 * Determine minimum number of boards required for the
		 * allocation (b_min)
		 */
		count = 0;
		for (b = 0; b < nboards_nb; b++) {
			count += sort_brds_core_cnt[b];
			if (count >= req_cores)
				break;
		}
		b_min = b + 1;
		if (b_min > nboards_nb) {
			char core_str[64];
			bit_fmt(core_str, 64, job_res->core_bitmap);
			error("%s: b_min > nboards_nb (%d > %u) node:%s core_bitmap:%s",
			      __func__, b_min, nboards_nb,
			      node_record_table_ptr[n].name, core_str);
			break;
		}
		sock_per_comb = b_min * sock_per_brd;

		/* Allocate space for list of board combinations */
		ncomb_brd = comb_counts[nboards_nb-1][b_min-1];
		board_combs = xmalloc(ncomb_brd * b_min * sizeof(int));
		/* Generate all combinations of b_min boards on the node */
		_gen_combs(board_combs, nboards_nb, b_min);

		/*
		 * Determine which combinations have enough available cores
		 * for the allocation (eligible board combinations)
		 */
		elig_brd_combs = xmalloc(ncomb_brd * sizeof(int));
		elig_core_cnt = xmalloc(ncomb_brd * sizeof(int));
		elig = 0;
		for (comb_idx = 0; comb_idx < ncomb_brd; comb_idx++) {
			count = 0;
			for (comb_brd_idx = 0; comb_brd_idx < b_min;
			     comb_brd_idx++) {
				board_num = board_combs[(comb_idx * b_min)
							+ comb_brd_idx];
				count += boards_core_cnt[board_num];
			}
			if (count >= req_cores) {
				elig_brd_combs[elig] = comb_idx;
				elig_core_cnt[elig] = count;
				elig++;
			}
		}

		/*
		 * Allocate space for list of sockets for each eligible board
		 * combination
		 */
		socket_list = xmalloc(elig * sock_per_comb * sizeof(int));

		/*
		 * Generate sorted list of sockets for each eligible board
		 * combination, and find combination with minimum number
		 * of sockets and minimum number of CPUs required for the
		 * allocation
		 */
		s_min = sock_per_comb;
		comb_min = 0;
		core_min = sock_per_comb * ncores_nb;
		for (elig_idx = 0; elig_idx < elig; elig_idx++) {
			comb_idx = elig_brd_combs[elig_idx];
			for (comb_brd_idx = 0; comb_brd_idx < b_min;
							comb_brd_idx++) {
				board_num = board_combs[(comb_idx * b_min)
							+ comb_brd_idx];
				sock_list_idx = (elig_idx * sock_per_comb) +
						(comb_brd_idx * sock_per_brd);
				for (sock_idx = 0; sock_idx < sock_per_brd;
				     sock_idx++) {
					socket_list[sock_list_idx + sock_idx]
						= (board_num * sock_per_brd)
							+ sock_idx;
				}
			}
			/*
			 * Sort this socket list in descending order of
			 * available core count
			 */
			qsort(&socket_list[elig_idx*sock_per_comb],
			      sock_per_comb, sizeof (int), _cmp_sock);
			/*
			 * Determine minimum number of sockets required for
			 * the allocation from this socket list
			 */
			count = 0;
			for (b = 0; b < sock_per_comb; b++) {
				sock_idx =
				socket_list[(int)((elig_idx*sock_per_comb)+b)];
				count += sockets_core_cnt[sock_idx];
				if (count >= req_cores)
					break;
			}
			b++;
			/*
			 * Use board combination with minimum number
			 * of required sockets and minimum number of CPUs
			 */
			if ((b < s_min) ||
			    ((b == s_min) &&
			     (elig_core_cnt[elig_idx] <= core_min))) {
				s_min = b;
				comb_min = elig_idx;
				core_min = elig_core_cnt[elig_idx];
			}
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: node[%u]: required CPUs:%u min req boards:%u,",
			     plugin_type, __func__, n, cpus, b_min);
			info("%s: %s: node[%u]: min req sockets:%u min avail cores:%u",
			     plugin_type, __func__, n, s_min, core_min);
		}
		/*
		 * Re-sort socket list for best-fit board combination in
		 * ascending order of socket number
		 */
		qsort(&socket_list[comb_min * sock_per_comb], sock_per_comb,
		      sizeof (int), _cmp_int_ascend);

		xfree(board_combs);
		xfree(elig_brd_combs);
		xfree(elig_core_cnt);

		/*
		 * select cores from the sockets of the best-fit board
		 * combination using a best-fit approach
		 */
		tmp_cpt = cpus_per_task;
		while (cpus > 0) {
			best_fit_cores = 0;
			best_fit_sufficient = false;

			/* search for the socket with best fit */
			for (z = 0; z < sock_per_comb; z++) {
				s = socket_list[(comb_min*sock_per_comb)+z];
				sufficient = sockets_core_cnt[s] >= req_cores;
				if ((best_fit_cores == 0) ||
				    (sufficient && !best_fit_sufficient ) ||
				    (sufficient &&
				     (sockets_core_cnt[s] < best_fit_cores)) ||
				    (!sufficient &&
				     (sockets_core_cnt[s] > best_fit_cores))) {
					best_fit_cores = sockets_core_cnt[s];
					best_fit_location = s;
					best_fit_sufficient = sufficient;
				}
			}

			/* check that we have found a usable socket */
			if (best_fit_cores == 0)
				break;

			j = best_fit_location;
			if (sock_per_brd)
				j /= sock_per_brd;
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("%s: %s: using node[%u]: "
				     "board[%u]: socket[%u]: %u cores available",
				     plugin_type, __func__, n, j,
				     best_fit_location,
				     sockets_core_cnt[best_fit_location]);
			}

			sockets_used[best_fit_location] = true;
			for (j = (c + (best_fit_location * ncores_nb));
			     j < (c + ((best_fit_location + 1) * ncores_nb));
			     j++ ) {
				/*
				 * if no more CPUs to select
				 * release remaining cores unless
				 * we are allocating whole sockets
				 */
				if (cpus == 0) {
					if (alloc_sockets) {
						bit_set(job_res->core_bitmap,j);
						core_cnt++;
					} else {
						bit_clear(job_res->core_bitmap,j);
					}
					continue;
				}

				/*
				 * remove cores from socket count and
				 * cpus count using hyperthreading requirement
				 */
				if (bit_test(job_res->core_bitmap, j)) {
					sockets_core_cnt[best_fit_location]--;
					core_cnt++;
					if (cpus < vpus)
						cpus = 0;
					else if ((ntasks_per_core == 1) &&
						 (cpus_per_task > vpus)) {
						int used = MIN(tmp_cpt, vpus);
						cpus -= used;

						if (tmp_cpt <= used)
							tmp_cpt = cpus_per_task;
						else
							tmp_cpt -= used;
					} else {
						cpus -= vpus;
					}
				} else if (alloc_sockets) {
					/*
					 * If the core is not used, add it
					 * anyway if allocating whole sockets
					 */
					bit_set(job_res->core_bitmap, j);
					core_cnt++;
				}
			}

			/* loop again if more CPUs required */
			if (cpus > 0)
				continue;

			/* release remaining cores of the unused sockets */
			for (s = 0; s < nsockets_nb; s++) {
				if (sockets_used[s])
					continue;
				bit_nclear(job_res->core_bitmap,
					   c+(s*ncores_nb),
					   c+((s+1)*ncores_nb)-1);
			}

		}

		xfree(socket_list);
		if (cpus > 0) {
			/*
			 * CPUs count should NEVER be greater than the number
			 * of set bits in the core bitmap for a given node
			 */
			error("%s: %s: CPUs computation error",
			     plugin_type,  __func__);
			break;
		}

		/* adjust cpus count of the current node */
		if ((alloc_cores || alloc_sockets) &&
		    (select_node_record[n].vpus >= 1)) {
			job_res->cpus[i] = core_cnt *
					   select_node_record[n].vpus;
		}
		i++;

		/* move c to the next node in core_bitmap */
		c += num_bits;
	}

	xfree(boards_core_cnt);
	xfree(sort_brds_core_cnt);
	xfree(sockets_core_cnt);
	xfree(sockets_used);
}

/* Remove any specialized cores from those allocated to the job */
static void _clear_spec_cores(struct job_record *job_ptr,
			      bitstr_t **core_array)
{
	int i, i_first, i_last;
	int alloc_node = -1, alloc_core = -1, c;
	job_resources_t *job_res = job_ptr->job_resrcs;
	multi_core_data_t *mc_ptr = NULL;

	if (job_ptr->details && job_ptr->details->mc_ptr)
		mc_ptr = job_ptr->details->mc_ptr;

	bit_set_all(job_res->core_bitmap);

	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		job_res->cpus[++alloc_node] = 0;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			alloc_core++;
			if (bit_test(core_array[i], c)) {
				uint16_t tpc = select_node_record[i].vpus;
				if (mc_ptr &&
				    (mc_ptr->threads_per_core != NO_VAL16) &&
				    (mc_ptr->threads_per_core < tpc))
					tpc = mc_ptr->threads_per_core;

				job_res->cpus[alloc_node] += tpc;
			} else {
				bit_clear(job_res->core_bitmap, alloc_core);
			}
		}
	}
}

/* qsort compare function for ascending int list */
static int _cmp_int_ascend(const void *a, const void *b)
{
	return (*(int*)a - *(int*)b);
}

/* qsort compare function for descending int list */
static int _cmp_int_descend(const void *a, const void *b)
{
	return (*(int*)b - *(int*)a);
}


/* qsort compare function for board combination socket list
 * NOTE: sockets_core_cnt is a global symbol in this module */
static int _cmp_sock(const void *a, const void *b)
{
	return (sockets_core_cnt[*(int*)b] - sockets_core_cnt[*(int*)a]);
}

/* Return true if more tasks can be allocated for this job on this node */
static bool _tres_tasks_avail(uint32_t *gres_task_limit,
			      job_resources_t *job_res, uint32_t node_offset)
{
	if (!gres_task_limit || !job_res)
		return true;
	if (gres_task_limit[node_offset] > job_res->tasks_per_node[node_offset])
		return true;
	return false;
}

/*
 * _compute_task_c_b_task_dist - compute the number of tasks on each
 * of the node for the cyclic and block distribution. We need to do
 * this in the case of consumable resources so that we have an exact
 * count for the needed hardware resources which will be used later to
 * update the different used resources per node structures.
 *
 * The most common case is when we have more resources than needed. In
 * that case we just "take" what we need and "release" the remaining
 * resources for other jobs. In the case where we oversubscribe the
 * processing units (PUs) we keep the initial set of resources.
 *
 * IN/OUT job_ptr - pointer to job being scheduled. The per-node
 *                  job_res->cpus array is recomputed here.
 * IN gres_task_limit - array of task limits based upon job's GRES specification
 *			offset based upon bits set in
 *			job_ptr->job_resrcs->node_bitmap
 */
static int _compute_c_b_task_dist(struct job_record *job_ptr,
				  uint32_t *gres_task_limit)
{
	bool over_subscribe = false;
	uint32_t n, tid, t, maxtasks, l;
	uint16_t *avail_cpus;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool log_over_subscribe = true;
	char *err_msg = NULL;
	uint16_t *vpus;
	bool space_remaining = false;
	bool test_tres_tasks = true;
	int i, i_first, i_last, rem_cpus, rem_tasks;

	if (!job_res)
		err_msg = "job_res is NULL";
	else if (!job_res->cpus)
		err_msg = "job_res->cpus is NULL";
	else if (!job_res->nhosts)
		err_msg = "job_res->nhosts is zero";
	if (err_msg) {
		error("%s: %s: Invalid allocation for %pJ: %s",
		      plugin_type, __func__, job_ptr, err_msg);
		return SLURM_ERROR;
	}

	vpus = xmalloc(job_res->nhosts * sizeof(uint16_t));
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first >= 0)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first, n = 0; i <= i_last; i++) {
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		vpus[n++] = select_node_record[i].vpus;
	}

	maxtasks = job_res->ncpus;
	avail_cpus = job_res->cpus;
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));
	job_res->tasks_per_node = xmalloc(job_res->nhosts * sizeof(uint16_t));

	/* ncpus is already set the number of tasks if overcommit is used */
	if (!job_ptr->details->overcommit &&
	    (job_ptr->details->cpus_per_task > 1)) {
		if (job_ptr->details->ntasks_per_node == 0) {
			maxtasks = maxtasks / job_ptr->details->cpus_per_task;
		} else {
			maxtasks = job_ptr->details->ntasks_per_node *
				   job_res->nhosts;
		}
	}

	/*
	 * Safe guard if the user didn't specified a lower number of
	 * CPUs than cpus_per_task or didn't specify the number.
	 */
	if (!maxtasks) {
		error("%s: %s: changing task count from 0 to 1 for %pJ",
		      plugin_type, __func__, job_ptr);
		maxtasks = 1;
	}
	if (job_ptr->details->cpus_per_task == 0)
		job_ptr->details->cpus_per_task = 1;
	if (job_ptr->details->overcommit)
		log_over_subscribe = false;
	/* Start by allocating one task per node */
	space_remaining = false;
	tid = 0;
	for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
		if (avail_cpus[n] || over_subscribe) {
			/* Ignore gres_task_limit for first task per node */
			tid++;
			job_res->tasks_per_node[n]++;
			for (l = 0; l < job_ptr->details->cpus_per_task; l++) {
				if (job_res->cpus[n] < avail_cpus[n])
					job_res->cpus[n]++;
			}
			if (job_res->cpus[n] < avail_cpus[n])
				space_remaining = true;
		}
	}
	if (!space_remaining)
		over_subscribe = true;

	/* Next fill out the CPUs on the cores already allocated to this job */
	for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
		rem_cpus = job_res->cpus[n] % vpus[n];
		rem_tasks = rem_cpus / job_ptr->details->cpus_per_task;
		if (rem_tasks == 0)
			continue;
		for (t = 0; ((t < rem_tasks) && (tid < maxtasks)); t++) {
			if (!over_subscribe &&
			    ((avail_cpus[n] - job_res->cpus[n]) <
			     job_ptr->details->cpus_per_task))
				break;
			if (!_tres_tasks_avail(gres_task_limit, job_res, n))
				break;
			tid++;
			job_res->tasks_per_node[n]++;
			for (l = 0; l < job_ptr->details->cpus_per_task; l++) {
				if (job_res->cpus[n] < avail_cpus[n])
					job_res->cpus[n]++;
			}
		}
	}

	/*
	 * Next distribute additional tasks, packing the cores or sockets as
	 * appropriate to avoid allocating more CPUs than needed. For example,
	 * with core allocations and 2 processors per core, we don't want to
	 * partially populate some cores on some nodes and allocate extra
	 * cores on other nodes. So "srun -n20 hostname" should not launch 7
	 * tasks on node 0, 7 tasks on node 1, and 6 tasks on node 2.  It should
	 * launch 8 tasks on node, 8 tasks on node 1, and 4 tasks on node 2.
	 */
	if (job_ptr->details->overcommit && (job_ptr->tres_per_task == 0))
		maxtasks = 0;	/* Allocate have one_task_per_node */
	for (i = 0; tid < maxtasks; i++) {
		bool space_remaining = false;
		if (over_subscribe && log_over_subscribe) {
			/*
			 * 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available CPUs
			 */
			error("%s: %s: oversubscribe for %pJ",
			      plugin_type, __func__, job_ptr);
			log_over_subscribe = false;	/* Log once per job */;
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			rem_tasks = vpus[n] / job_ptr->details->cpus_per_task;
			rem_tasks = MAX(rem_tasks, 1);
			for (t = 0; ((t < rem_tasks) && (tid < maxtasks)); t++){
				if (!over_subscribe &&
				    ((avail_cpus[n] - job_res->cpus[n]) <
				     job_ptr->details->cpus_per_task))
					break;
				if (!_tres_tasks_avail(gres_task_limit,
						       job_res, n))
					break;
				tid++;
				job_res->tasks_per_node[n]++;
				for (l = 0; l < job_ptr->details->cpus_per_task;
				     l++) {
					if (job_res->cpus[n] < avail_cpus[n])
						job_res->cpus[n]++;
				}
				if ((avail_cpus[n] - job_res->cpus[n]) >=
				    job_ptr->details->cpus_per_task)
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	xfree(avail_cpus);
	xfree(vpus);

	if (job_ptr->details->overcommit && job_ptr->tres_per_task)
		maxtasks = job_ptr->details->num_tasks;
	/*
	 * Distribute any remaining tasks (without dedicated CPUs) evenly
	 * across nodes
	 */
	while (tid < maxtasks) {
		bool more_tres_tasks = false;
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			if (test_tres_tasks &&
			    !_tres_tasks_avail(gres_task_limit, job_res, n))
				continue;
			more_tres_tasks = true;
			tid++;
			job_res->tasks_per_node[n]++;
		}
		if (!more_tres_tasks)
			test_tres_tasks = false;
	}

	return SLURM_SUCCESS;
}

/* distribute blocks (planes) of tasks cyclically */
static int _compute_plane_dist(struct job_record *job_ptr,
			       uint32_t *gres_task_limit)
{
	bool over_subscribe = false;
	uint32_t n, i, p, tid, maxtasks, l;
	uint16_t *avail_cpus, plane_size = 1;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool log_over_subscribe = true;
	bool test_tres_tasks = true;

	if (!job_res || !job_res->cpus || !job_res->nhosts) {
		error("%s: %s: invalid allocation for %pJ",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	maxtasks = job_res->ncpus;
	avail_cpus = job_res->cpus;

	if (job_ptr->details->cpus_per_task > 1)
		 maxtasks = maxtasks / job_ptr->details->cpus_per_task;

	if (job_ptr->details->mc_ptr)
		plane_size = job_ptr->details->mc_ptr->plane_size;
	if (plane_size <= 0) {
		error("%s: %s: invalid plane_size", plugin_type, __func__);
		return SLURM_ERROR;
	}
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));
	job_res->tasks_per_node = xmalloc(job_res->nhosts * sizeof(uint16_t));
	if (job_ptr->details->overcommit)
		log_over_subscribe = false;
	for (tid = 0, i = 0; (tid < maxtasks); i++) { /* cycle counter */
		bool space_remaining = false;
		if (over_subscribe && log_over_subscribe) {
			/*
			 * 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available CPUs
			 */
			error("%s: %s: oversubscribe for %pJ",
			      plugin_type, __func__, job_ptr);
			log_over_subscribe = false;	/* Log once per job */;
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			bool more_tres_tasks = false;
			for (p = 0; p < plane_size && (tid < maxtasks); p++) {
				if (test_tres_tasks &&
				    !_tres_tasks_avail(gres_task_limit,
						       job_res, n))
					continue;
				more_tres_tasks = true;
				if ((job_res->cpus[n] < avail_cpus[n]) ||
				    over_subscribe) {
					tid++;
					job_res->tasks_per_node[n]++;
					for (l = 0;
					     l <job_ptr->details->cpus_per_task;
					     l++) {
						if (job_res->cpus[n] <
						    avail_cpus[n])
							job_res->cpus[n]++;
					}
				}
			}
			if (!more_tres_tasks)
				test_tres_tasks = false;
			if (job_res->cpus[n] < avail_cpus[n])
				space_remaining = true;
		}
		if (!space_remaining) {
			over_subscribe = true;
		}
	}
	xfree(avail_cpus);
	return SLURM_SUCCESS;
}

/*
 * Sync up the core_bitmap with the CPU array using cyclic distribution
 *
 * The CPU array contains the distribution of CPUs, which can include
 * virtual CPUs (hyperthreads)
 */
static int _cyclic_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type, bool preempt_mode)
{
	uint32_t c, i, j, k, s, n;
	uint32_t *sock_start, *sock_end, size, csize, core_cnt;
	uint16_t cps = 0, cpus, vpus, sockets, sock_size, orig_cpu_cnt;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bitstr_t *core_map;
	bool *sock_used, *sock_avoid;
	bool alloc_cores = false, alloc_sockets = false;
	uint16_t ntasks_per_socket = 0xffff;
	uint16_t ntasks_per_core = 0xffff;
	int error_code = SLURM_SUCCESS;
	int tmp_cpt = 0; /* cpus_per_task */

	if ((job_res == NULL) || (job_res->core_bitmap == NULL) ||
	    (job_ptr->details == NULL))
		return error_code;

	if (cr_type & CR_SOCKET)
		alloc_sockets = true;
	else if (cr_type & CR_CORE)
		alloc_cores = true;

	core_map = job_res->core_bitmap;
	if (job_ptr->details->mc_ptr) {
		multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			ntasks_per_core = mc_ptr->ntasks_per_core;
		}

		if (mc_ptr->ntasks_per_socket)
			ntasks_per_socket = mc_ptr->ntasks_per_socket;
	}

	sock_size  = select_node_record[0].sockets;
	sock_avoid = xmalloc(sock_size * sizeof(bool));
	sock_start = xmalloc(sock_size * sizeof(uint32_t));
	sock_end   = xmalloc(sock_size * sizeof(uint32_t));
	sock_used  = xmalloc(sock_size * sizeof(bool));

	size  = bit_size(job_res->node_bitmap);
	csize = bit_size(core_map);
	for (c = 0, i = 0, n = 0; n < size; n++) {
		if (bit_test(job_res->node_bitmap, n) == 0)
			continue;
		sockets = select_node_record[n].sockets;
		cps     = select_node_record[n].cores;
		vpus    = vpus_per_core(job_ptr->details, n);

		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: %pJ node %s vpus %u cpus %u",
			     plugin_type, __func__, job_ptr,
			     select_node_record[n].node_ptr->name,
			     vpus, job_res->cpus[i]);
		}

		if ((c + (sockets * cps)) > csize) {
			error("%s: %s: index error", plugin_type, __func__);
			break;
		}

		if (sockets > sock_size) {
			sock_size = sockets;
			xrealloc(sock_avoid, sock_size * sizeof(bool));
			xrealloc(sock_start, sock_size * sizeof(uint32_t));
			xrealloc(sock_end,   sock_size * sizeof(uint32_t));
			xrealloc(sock_used,  sock_size * sizeof(bool));
		}

		for (s = 0; s < sockets; s++) {
			sock_start[s] = c + (s * cps);
			sock_end[s]   = sock_start[s] + cps;
			sock_avoid[s] = false;
			sock_used[s]  = false;
		}
		core_cnt = 0;
		cpus = job_res->cpus[i];

		if (ntasks_per_socket != 0xffff) {
			int x_cpus, cpus_per_socket;
			uint32_t total_cpus = 0;
			uint32_t *cpus_cnt;

			cpus_per_socket = ntasks_per_socket *
					  job_ptr->details->cpus_per_task;
			cpus_cnt = xmalloc(sizeof(uint32_t) * sockets);
			for (s = 0; s < sockets; s++) {
				for (j = sock_start[s]; j < sock_end[s]; j++) {
					if (bit_test(core_map, j))
						cpus_cnt[s] += vpus;
				}
				total_cpus += cpus_cnt[s];
			}
			for (s = 0; s < sockets && total_cpus > cpus; s++) {
				if (cpus_cnt[s] > cpus_per_socket) {
					x_cpus = cpus_cnt[s] - cpus_per_socket;
					cpus_cnt[s] = cpus_per_socket;
					total_cpus -= x_cpus;
				}
			}
			for (s = 0; s < sockets && total_cpus > cpus; s++) {
				if ((cpus_cnt[s] <= cpus_per_socket) &&
				    (total_cpus - cpus_cnt[s] >= cpus)) {
					sock_avoid[s] = true;
					total_cpus -= cpus_cnt[s];
				}
			}
			xfree(cpus_cnt);
		} else if (job_ptr->details->cpus_per_task > 1) {
			/* Try to pack all CPUs of each tasks on one socket. */
			uint32_t *cpus_cnt, cpus_per_task;

			cpus_per_task = job_ptr->details->cpus_per_task;
			cpus_cnt = xmalloc(sizeof(uint32_t) * sockets);
			for (s = 0; s < sockets; s++) {
				for (j = sock_start[s]; j < sock_end[s]; j++) {
					if (bit_test(core_map, j))
						cpus_cnt[s] += vpus;
				}
				cpus_cnt[s] -= (cpus_cnt[s] % cpus_per_task);
			}
			tmp_cpt = cpus_per_task;
			for (s = 0; ((s < sockets) && (cpus > 0)); s++) {
				while ((sock_start[s] < sock_end[s]) &&
				       (cpus_cnt[s] > 0) && (cpus > 0)) {
					if (bit_test(core_map, sock_start[s])) {
						int used;
						sock_used[s] = true;
						core_cnt++;

						if ((ntasks_per_core == 1) &&
						    (cpus_per_task > vpus)) {
							used = MIN(tmp_cpt,
								   vpus);
							if (tmp_cpt <= used)
								tmp_cpt = cpus_per_task;
							else
								tmp_cpt -= used;
						} else
							used = vpus;

						if (cpus_cnt[s] < vpus)
							cpus_cnt[s] = 0;
						else
							cpus_cnt[s] -= used;
						if (cpus < vpus)
							cpus = 0;
						else
							cpus -= used;
					}
					sock_start[s]++;
				}
			}
			xfree(cpus_cnt);
		}

		orig_cpu_cnt = cpus;
		while (cpus > 0) {
			uint16_t prev_cpus = cpus;
			for (s = 0; s < sockets && cpus > 0; s++) {
				if (sock_avoid[s])
					continue;
				while (sock_start[s] < sock_end[s]) {
					if (bit_test(core_map, sock_start[s])) {
						sock_used[s] = true;
						core_cnt++;
						break;
					} else
						sock_start[s]++;
				}
				if (sock_start[s] == sock_end[s])
					/* this socket is unusable */
					continue;
				if (cpus < vpus)
					cpus = 0;
				else
					cpus -= vpus;
				sock_start[s]++;
			}
			if (prev_cpus != cpus)
				 continue;
			if (job_ptr->details->overcommit) {
				/* We've got all the CPUs that we need */
				break;
			}
			if (!preempt_mode) {
				/* we're stuck! */
				char *core_str = NULL, *sock_str = NULL, *sep;
				for (j = 0, k = c; j < (cps * sockets);
				     j++, k++) {
					if (!bit_test(core_map, k))
						continue;
					if (sock_str)
						sep = ",";
					else
						sep = "";
					xstrfmtcat(core_str, "%s%d", sep, j);
				}
				if (!core_str)
					core_str = xstrdup("NONE");
				for (s = 0; s < sockets; s++) {
					if (!sock_avoid[s])
						continue;
					if (sock_str)
						sep = ",";
					else
						sep = "";
					xstrfmtcat(sock_str, "%s%d", sep, s);
				}
				if (!sock_str)
					sock_str = xstrdup("NONE");
				job_ptr->priority = 0;
				job_ptr->state_reason = WAIT_HELD;
				error("%s: %s: sync loop not progressing, holding %pJ, "
				      "tried to use %u CPUs on node %s core_map:%s avoided_sockets:%s vpus:%u",
				      plugin_type, __func__, job_ptr,
				      orig_cpu_cnt,
				      select_node_record[n].node_ptr->name,
				      core_str, sock_str, vpus);
				xfree(core_str);
				xfree(sock_str);
			}
			error_code = SLURM_ERROR;
			goto fini;
		}

		/*
		 * clear the rest of the cores in each socket
		 * FIXME: do we need min_core/min_socket checks here?
		 */
		for (s = 0; s < sockets; s++) {
			if (sock_start[s] == sock_end[s])
				continue;
			if (!alloc_sockets || !sock_used[s]) {
				bit_nclear(core_map, sock_start[s],
					   sock_end[s]-1);
			}
			if ((select_node_record[n].vpus >= 1) &&
			    (alloc_sockets || alloc_cores) && sock_used[s]) {
				for (j = sock_start[s]; j < sock_end[s]; j++) {
					/* Mark all cores as used */
					if (alloc_sockets)
						bit_set(core_map, j);
					if (bit_test(core_map, j))
						core_cnt++;
				}
			}
		}
		if ((alloc_cores || alloc_sockets) &&
		    (select_node_record[n].vpus >= 1)) {
			job_res->cpus[i] = core_cnt *
					   select_node_record[n].vpus;
		}
		i++;
		/* advance 'c' to the beginning of the next node */
		c += sockets * cps;
	}
fini:	xfree(sock_avoid);
	xfree(sock_start);
	xfree(sock_end);
	xfree(sock_used);
	return error_code;
}

/*
 * Generate all combinations of k integers from the
 * set of integers 0 to n-1.
 * Return combinations in comb_list.
 *
 * Example: For k = 2 and n = 4, there are six
 *          combinations:
 *          {0,1},{0,2},{0,3},{1,2},{1,3},{2,3}
 *
 */
static void _gen_combs(int *comb_list, int n, int k)
{
	int i, b;
	int *comb = xmalloc(k * sizeof(int));

	/* Setup comb for the initial combination */
	for (i = 0; i < k; i++)
		comb[i] = i;
	b = 0;

	/* Generate all the other combinations */
	while (1) {
		for (i = 0; i < k; i++) {
			comb_list[b + i] = comb[i];
		}
		b += k;
		i = k - 1;
		++comb[i];
		while ((i >= 0) && (comb[i] >= n - k + 1 + i)) {
			--i;
			++comb[i];
		}

		if (comb[0] > n - k)
			break; /* No more combinations */

		for (i = i + 1; i < k; ++i)
			comb[i] = comb[i - 1] + 1;
	}
	xfree(comb);
}

/* CPUs already selected for jobs, just distribute the tasks */
static int _set_task_dist(struct job_record *job_ptr)
{
	uint32_t n, i, tid = 0, maxtasks;
	uint16_t *avail_cpus;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool log_over_subscribe = true;
	char *err_msg = NULL;
	int plane_size = 1;

	if (!job_ptr->tres_per_task)	/* Task layout for GRES not required */
		return SLURM_SUCCESS;

	if (!job_res)
		err_msg = "job_res is NULL";
	else if (!job_res->cpus)
		err_msg = "job_res->cpus is NULL";
	else if (!job_res->nhosts)
		err_msg = "job_res->nhosts is zero";
	if (err_msg) {
		error("%s: %s: Invalid allocation for %pJ: %s",
		      plugin_type, __func__, job_ptr, err_msg);
		return SLURM_ERROR;
	}

	if ((job_ptr->details->task_dist & SLURM_DIST_STATE_BASE) ==
	    SLURM_DIST_PLANE) {
		if (job_ptr->details->mc_ptr)
			plane_size = job_ptr->details->mc_ptr->plane_size;
		if (plane_size <= 0) {
			error("%s: %s: invalid plane_size", plugin_type, __func__);
			return SLURM_ERROR;
		}
	}

	i = job_res->nhosts * sizeof(uint16_t);
	avail_cpus = xmalloc(i);
	memcpy(avail_cpus, job_res->cpus, i);
	job_res->tasks_per_node = xmalloc(i);
	maxtasks = job_res->ncpus;

	/* ncpus is already set the number of tasks if overcommit is used */
	if (!job_ptr->details->overcommit &&
	    (job_ptr->details->cpus_per_task > 1)) {
		if (job_ptr->details->ntasks_per_node == 0) {
			maxtasks = maxtasks / job_ptr->details->cpus_per_task;
		} else {
			maxtasks = job_ptr->details->ntasks_per_node *
				   job_res->nhosts;
		}
	}

	/*
	 * Safe guard if the user didn't specified a lower number of
	 * cpus than cpus_per_task or didn't specify the number.
	 */
	if (!maxtasks) {
		error("%s: %s: changing task count from 0 to 1 for %pJ",
		      plugin_type, __func__, job_ptr);
		maxtasks = 1;
	}
	if (job_ptr->details->cpus_per_task == 0)
		job_ptr->details->cpus_per_task = 1;

	/* First put one task on each node node */
	for (n = 0; n < job_res->nhosts; n++) {
		tid++;
		job_res->tasks_per_node[n] = 1;
		if (job_ptr->details->cpus_per_task > avail_cpus[n]) {
			if (!job_ptr->details->overcommit) {
				error("%s: %s: avail_cpus underflow on node %d for %pJ",
				      plugin_type, __func__, n, job_ptr);
			}
			avail_cpus[n] = 0;
		} else {
			avail_cpus[n] -= job_ptr->details->cpus_per_task;
		}
	}

	/* Distrubute remaining tasks per plane size */
	while (maxtasks > tid) {
		uint32_t last_tid = tid;
		for (n = 0; n < job_res->nhosts; n++) {
			if (job_ptr->details->cpus_per_task > avail_cpus[n])
				continue;
			i = MAX(job_res->tasks_per_node[n] % plane_size, 1);
			i = MIN(i,
				avail_cpus[n] /job_ptr->details->cpus_per_task);
			i = MIN(i, maxtasks - tid);
			job_res->tasks_per_node[n] += i;
			tid += i;
			avail_cpus[n] -= (i * job_ptr->details->cpus_per_task);
		}
		if (last_tid == tid)
			break;
	}

	/* If more tasks than resources, distribute them evenly */
	if (!job_ptr->details->overcommit)
		log_over_subscribe = true;
	while (maxtasks > tid) {
		if (log_over_subscribe) {
			/*
			 * 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available CPUs
			 */
			error("%s: %s: oversubscribe for %pJ",
			      plugin_type, __func__, job_ptr);
			log_over_subscribe = false;
		}
		for (n = 0; n < job_res->nhosts; n++) {
			i = MIN(plane_size, maxtasks - tid);
			job_res->tasks_per_node[n] += i;
			tid += i;
		}
	}
	xfree(avail_cpus);

	return SLURM_SUCCESS;
}

/* Enable detailed logging of cr_dist() node and core bitmaps */
static inline void _log_select_maps(char *loc, struct job_record *job_ptr)
{
	job_resources_t *job_res = job_ptr->job_resrcs;
	char tmp[100];
	int i;

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s %pJ", __func__, loc, job_ptr);
		if (job_res->node_bitmap) {
			bit_fmt(tmp, sizeof(tmp), job_res->node_bitmap);
			info("  node_bitmap:%s", tmp);
		}
		if (job_res->core_bitmap) {
			bit_fmt(tmp, sizeof(tmp), job_res->core_bitmap);
			info("  core_bitmap:%s", tmp);
		}
		if (job_res->cpus) {
			for (i = 0; i < job_res->nhosts; i++) {
				info("  avail_cpus[%d]:%u", i,
				     job_res->cpus[i]);
			}
		}
		if (job_res->tasks_per_node) {
			for (i = 0; i < job_res->nhosts; i++) {
				info("  tasks_per_node[%d]:%u", i,
				     job_res->tasks_per_node[i]);
			}
		}
	}
}

/*
 * To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many cores are needed on each node.
 *
 * This routine is a slightly modified "version" of the routine
 * _task_layout_block in src/common/dist_tasks.c. We do not need to
 * assign tasks to job->hostid[] and job->tids[][] at this point so
 * the core allocation is the same for cyclic and block.
 *
 * For the consumable resources support we need to determine what
 * "node/Core/thread"-tuplets will be allocated for a given job.
 * In the past we assumed that we only allocated one task per PU
 * (processing unit, the lowest allocatable logical processor,
 * core or thread depending upon configuration) and didn't allow
 * the use of overcommit. We have changed this philosophy and are now
 * allowing people to overcommit their resources and expect the system
 * administrator to enable the task/affinity plug-in which will then
 * bind all of a job's tasks to its allocated resources thereby
 * avoiding interference between co-allocated running jobs.
 *
 * In the consumable resources environment we need to determine the
 * layout schema within slurmctld.
 *
 * We have a core_bitmap of all available cores. All we're doing here
 * is removing cores that are not needed based on the task count, and
 * the choice of cores to remove is based on the distribution:
 * - "cyclic" removes cores "evenly", starting from the last socket,
 * - "block" removes cores from the "last" socket(s)
 * - "plane" removes cores "in chunks"
 *
 * IN job_ptr - job to be allocated resources
 * IN cr_type - allocation type (sockets, cores, etc.)
 * IN preempt_mode - true if testing with simulated preempted jobs
 * IN core_array - system-wide bitmap of cores originally available to
 *		the job, only used to identify specialized cores
 * IN gres_task_limit - array of task limits based upon job GRES specification,
 *		offset based upon bits set in job_ptr->job_resrcs->node_bitmap
 */
extern int cr_dist(struct job_record *job_ptr, const uint16_t cr_type,
		   bool preempt_mode, bitstr_t **core_array,
		   uint32_t *gres_task_limit)
{
	int error_code;
	bool one_task_per_node = false;

	/*
	 * Zero size jobs are supported for the creation and deletion of
	 * persistent burst buffers.
	 */
	if (job_ptr->details->min_nodes == 0)
		return SLURM_SUCCESS;

	if (job_ptr->details->core_spec != NO_VAL16) {
		/*
		 * The job has been allocated all non-specialized cores.
		 * Just set the task distribution for tres_per_task support.
		 */
		error_code = _set_task_dist(job_ptr);
		if (error_code != SLURM_SUCCESS)
			return error_code;
		return SLURM_SUCCESS;
	}

	if ((job_ptr->job_resrcs->node_req == NODE_CR_RESERVED) ||
	    (job_ptr->details->whole_node == 1)) {
		/*
		 * The job has been allocated an EXCLUSIVE set of nodes,
		 * so it gets all of the bits in the core_array except for
		 * specialized cores. Set the task distribution for
		 * tres_per_task support.
		 */
		_clear_spec_cores(job_ptr, core_array);
		error_code = _set_task_dist(job_ptr);
		if (error_code != SLURM_SUCCESS)
			return error_code;
		return SLURM_SUCCESS;
	}

	if (job_ptr->details->overcommit && (job_ptr->tres_per_task == 0))
		one_task_per_node = true;
	_log_select_maps("cr_dist/start", job_ptr);
	if (((job_ptr->details->task_dist & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_PLANE) && !one_task_per_node) {
		/* Perform plane distribution on the job_resources_t struct */
		error_code = _compute_plane_dist(job_ptr, gres_task_limit);
		if (error_code != SLURM_SUCCESS)
			return error_code;
	} else {
		/* Perform cyclic distribution on the job_resources_t struct */
		error_code = _compute_c_b_task_dist(job_ptr, gres_task_limit);
		if (error_code != SLURM_SUCCESS)
			return error_code;
	}
	_log_select_maps("cr_dist/middle", job_ptr);

	/*
	 * now sync up the core_bitmap with the job_resources_t struct
	 * based on the given distribution AND resource setting
	 */
	if (!(cr_type & CR_CORE) && !(cr_type & CR_SOCKET)) {
		_block_sync_core_bitmap(job_ptr, cr_type);
		return SLURM_SUCCESS;
	}

	/*
	 * If SelectTypeParameters mentions to use a block distribution for
	 * cores by default, use that kind of distribution if no particular
	 * cores distribution specified.
	 * Note : cyclic cores distribution, which is the default, is treated
	 * by the next code block
	 */
	if (slurmctld_conf.select_type_param & CR_CORE_DEFAULT_DIST_BLOCK) {
		switch (job_ptr->details->task_dist & SLURM_DIST_NODEMASK) {
		case SLURM_DIST_ARBITRARY:
		case SLURM_DIST_BLOCK:
		case SLURM_DIST_CYCLIC:
		case SLURM_DIST_UNKNOWN:
			_block_sync_core_bitmap(job_ptr, cr_type);
			return SLURM_SUCCESS;
		}
	}

	/*
	 * Determine the number of logical processors per node needed
	 * for this job. Make sure below matches the layouts in
	 * lllp_distribution in plugins/task/affinity/dist_task.c (FIXME)
	 */
	switch (job_ptr->details->task_dist & SLURM_DIST_NODESOCKMASK) {
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_PLANE:
		_block_sync_core_bitmap(job_ptr, cr_type);
		break;
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_BLOCK_CFULL:
	case SLURM_DIST_CYCLIC_CFULL:
	case SLURM_DIST_UNKNOWN:
		error_code = _cyclic_sync_core_bitmap(job_ptr, cr_type,
						      preempt_mode);
		break;
	default:
		error("%s: %s: invalid task_dist entry",
		      plugin_type, __func__);
		return SLURM_ERROR;
	}

	_log_select_maps("cr_dist/fini", job_ptr);
	return error_code;
}
