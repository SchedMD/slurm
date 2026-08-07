/*****************************************************************************\
 *  gang_exempt.c - Track cores exempt from GANG oversubscription
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "src/slurmctld/acct_policy.h"

bitstr_t **gang_exempt_cores = NULL;

static int _add_exempt_cores(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_resources_t *job_resrcs;
	node_record_t *node_ptr;
	int core_cnt = 0;

	if (!IS_JOB_RUNNING(job_ptr))
		return 0;
	job_resrcs = job_ptr->job_resrcs;
	if (!job_resrcs || !job_resrcs->core_bitmap || !job_resrcs->node_bitmap)
		return 0;
	if (slurm_job_preempt_mode(job_ptr) != PREEMPT_MODE_SUSPEND)
		return 0;
	if (!acct_policy_is_job_preempt_exempt(job_ptr))
		return 0;

	log_flag(SELECT_TYPE, "%pJ is exempt from SUSPEND preemption via PreemptExemptTime, adding cores to gang_exempt_cores",
		 job_ptr);

	/*
	 * For each node in the job, create/set a corresponding core bitmap in
	 * gang_exempt_cores that will be used later to prevent pending jobs
	 * from SUSPEND preempting this job.
	 *
	 * See build_job_resources() for navigating job_resrcs bitmaps.
	 */
	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs->node_bitmap, &i)); i++) {
		if (!gang_exempt_cores[i])
			gang_exempt_cores[i] = bit_alloc(node_ptr->tot_cores);

		if (job_resrcs->whole_node == 1) {
			bit_set_all(gang_exempt_cores[i]);
		} else {
			for (int c = 0; c < node_ptr->tot_cores; c++) {
				if (bit_test(job_resrcs->core_bitmap,
					     core_cnt + c))
					bit_set(gang_exempt_cores[i], c);
			}
		}
		core_cnt += node_ptr->tot_cores;
	}

	return 0;
}

/*
 * Rebuild array of cores currently exempt from preemption.  Caches the result
 * and skips the rebuild if already called within the same second.
 * PreemptExemptTime has one-second granularity so this is safe, and it avoids
 * iterating over job_list for every job_test() called.
 */
extern void gang_exempt_rebuild(void)
{
	static time_t last_rebuild = 0;
	time_t now = time(NULL);

	/* Only rebuild the core array at most once per second */
	if (last_rebuild == now)
		return;
	last_rebuild = now;

	if (!gang_exempt_cores)
		gang_exempt_cores = build_core_array();
	else
		clear_core_array(gang_exempt_cores);

	list_for_each(job_list, _add_exempt_cores, NULL);

	core_array_log("gang_exempt_cores", NULL, gang_exempt_cores);
}
