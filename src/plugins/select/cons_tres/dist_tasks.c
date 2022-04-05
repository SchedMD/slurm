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
#include "../cons_common/dist_tasks.h"

/*
 * Check if we're at job tasks_per_node limit for a given node when allocating
 * tasks to a node.
 *
 * RETURNS rc
 *  rc > 0 if tpn limit exceeded
 *  rc == 0 if exactly at tpn limit
 *  rc < 0 if not at limit yet
 */
static int _at_tpn_limit(const uint32_t n, const job_record_t *job_ptr,
			  const char *tag, bool log_error)
{
	const job_resources_t *job_res = job_ptr->job_resrcs;
	const log_level_t log_lvl = log_error ? LOG_LEVEL_ERROR :
						LOG_LEVEL_INFO;
	int rc = -1;

	/* Special case where no limit is imposed - no overcommit */
	if (job_ptr->details->ntasks_per_node == 0)
		return rc;

	rc = job_res->tasks_per_node[n] - job_ptr->details->ntasks_per_node;

	/* Limit exceeded */
	if ((rc > 0) && (log_error || (slurm_conf.debug_flags &
				       DEBUG_FLAG_SELECT_TYPE)))
		log_var(log_lvl,
			"%s over tasks_per_node for %pJ node:%u task_per_node:%d max:%u",
			tag, job_ptr, n, job_res->tasks_per_node[n],
			job_ptr->details->ntasks_per_node);

	return rc;
}

/*
 * dist_tasks_compute_c_b - compute the number of tasks on each
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
extern int dist_tasks_compute_c_b(job_record_t *job_ptr,
				  uint32_t *gres_task_limit)
{
	bool over_subscribe = false;
	uint32_t n, tid, t, maxtasks, l;
	uint16_t *avail_cpus;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool log_over_subscribe = true;
	char *err_msg = NULL;
	uint16_t *vpus;
	bool space_remaining;
	int i, i_first, i_last, rem_cpus, rem_tasks;
	uint16_t cpus_per_task;

	if (!job_res)
		err_msg = "job_res is NULL";
	else if (!job_res->cpus)
		err_msg = "job_res->cpus is NULL";
	else if (!job_res->nhosts)
		err_msg = "job_res->nhosts is zero";
	if (err_msg) {
		error("Invalid allocation for %pJ: %s",
		      job_ptr, err_msg);
		return SLURM_ERROR;
	}

	vpus = xmalloc(job_res->nhosts * sizeof(uint16_t));

	if (job_ptr->details->cpus_per_task == 0)
		job_ptr->details->cpus_per_task = 1;
	cpus_per_task = job_ptr->details->cpus_per_task;

	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first >= 0)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first, n = 0; i <= i_last; i++) {
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		vpus[n++] = node_record_table_ptr[i]->tpc;
	}

	maxtasks = job_res->ncpus;
	avail_cpus = job_res->cpus;
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));
	job_res->tasks_per_node = xmalloc(job_res->nhosts * sizeof(uint16_t));

	/* ncpus is already set the number of tasks if overcommit is used */
	if (!job_ptr->details->overcommit && (cpus_per_task > 1)) {
		if (job_ptr->details->ntasks_per_node == 0) {
			maxtasks = maxtasks / cpus_per_task;
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
		error("changing task count from 0 to 1 for %pJ",
		      job_ptr);
		maxtasks = 1;
	}
	if (job_ptr->details->overcommit)
		log_over_subscribe = false;
	/* Start by allocating one task per node */
	space_remaining = false;
	tid = 0;
	for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
		if (avail_cpus[n]) {
			/* Ignore gres_task_limit for first task per node */
			tid++;
			job_res->tasks_per_node[n]++;
			for (l = 0; l < cpus_per_task; l++) {
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
		rem_tasks = rem_cpus / cpus_per_task;
		if (rem_tasks == 0)
			continue;
		for (t = 0; ((t < rem_tasks) && (tid < maxtasks)); t++) {
			if (((avail_cpus[n] - job_res->cpus[n]) <
			     cpus_per_task))
				break;
			if (!dist_tasks_tres_tasks_avail(
				    gres_task_limit, job_res, n))
				break;
			if (_at_tpn_limit(n, job_ptr, "fill allocated",
					  false) >= 0)
				break;
			tid++;
			job_res->tasks_per_node[n]++;
			for (l = 0; l < cpus_per_task; l++) {
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
	if (job_ptr->details->overcommit && !job_ptr->tres_per_task)
		maxtasks = 0;	/* Allocate have one_task_per_node */
	while (tid < maxtasks) {
		bool space_remaining = false;
		int over_limit = -1;
		if (over_subscribe && log_over_subscribe && (over_limit > 0)) {
			/*
			 * 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available CPUs
			 */
			error("oversubscribe for %pJ",
			      job_ptr);
			log_over_subscribe = false;	/* Log once per job */;
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			rem_tasks = vpus[n] / cpus_per_task;
			rem_tasks = MAX(rem_tasks, 1);
			for (t = 0; ((t < rem_tasks) && (tid < maxtasks)); t++){
				if (!over_subscribe) {
					if ((avail_cpus[n] - job_res->cpus[n]) <
					    cpus_per_task)
						break;
					if (!dist_tasks_tres_tasks_avail(
						    gres_task_limit,
						    job_res, n))
						break;
					over_limit = _at_tpn_limit(
						n, job_ptr,
						"fill additional",
						false);
					if (over_limit >= 0)
						break;
				}

				tid++;
				job_res->tasks_per_node[n]++;
				for (l = 0; l < cpus_per_task;
				     l++) {
					if (job_res->cpus[n] < avail_cpus[n])
						job_res->cpus[n]++;
				}
				if ((avail_cpus[n] - job_res->cpus[n]) >=
				    cpus_per_task)
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	xfree(avail_cpus);
	xfree(vpus);

	return SLURM_SUCCESS;
}
