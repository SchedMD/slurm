/*****************************************************************************\
 *  job_resilience.c - adaptive job resilience on node failure
 *****************************************************************************
 *  Copyright (C) 2026 Gustavo Lima <gustcol@gmail.com>
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

#include "config.h"

#include <stdlib.h>
#include <time.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_resilience.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "src/stepmgr/gres_stepmgr.h"
#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

#define RESILIENCE_DEFAULT_MIN_HEALTH 70

/*
 * Minimum cluster health (percentage of nodes up) required to shrink a job
 * instead of killing or requeuing it. Configured through
 * SchedulerParameters=resilience_min_health=<1-100>.
 */
static int _min_health(void)
{
	static int min_health = -1;
	static time_t conf_time = 0;
	char *tmp;

	if (conf_time != slurm_conf.last_update) {
		min_health = RESILIENCE_DEFAULT_MIN_HEALTH;
		if ((tmp = xstrcasestr(slurm_conf.sched_params,
				       "resilience_min_health="))) {
			int val = atoi(tmp + strlen("resilience_min_health="));
			if ((val >= 1) && (val <= 100))
				min_health = val;
			else
				error("Invalid SchedulerParameters resilience_min_health=%d, using %d",
				      val, RESILIENCE_DEFAULT_MIN_HEALTH);
		}
		conf_time = slurm_conf.last_update;
	}

	return min_health;
}

extern int job_resilience_cluster_health(void)
{
	xassert(verify_lock(NODE_LOCK, READ_LOCK));

	if (active_node_record_count <= 0)
		return 0;

	return (bit_set_count(up_node_bitmap) * 100) / active_node_record_count;
}

extern bool job_resilience_eligible(job_record_t *job_ptr)
{
	int health, min_health;

	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));

	if (!(job_ptr->bit_flags & ADAPTIVE_RESILIENCE))
		return false;

	/* Nodes are not usable yet while the job is still configuring */
	if (!IS_JOB_RUNNING(job_ptr) || IS_JOB_CONFIGURING(job_ptr))
		return false;

	/* The job must keep at least one node after the shrink */
	if (!job_ptr->details || !job_ptr->job_resrcs ||
	    (job_ptr->node_cnt <= 1))
		return false;

	/* Interactive allocations keep the default node failure handling */
	if (!job_ptr->batch_flag)
		return false;

	/*
	 * Node failure handling for heterogeneous jobs is dispatched to every
	 * component, so a component would be shrunk for a node it does not
	 * own. Leave hetjobs to the regular handling.
	 */
	if (job_ptr->het_job_id)
		return false;

	health = job_resilience_cluster_health();
	min_health = _min_health();
	if (health < min_health) {
		info("%s: %pJ not shrunk, cluster health %d%% is below resilience_min_health=%d%%",
		     __func__, job_ptr, health, min_health);
		return false;
	}

	return true;
}

extern int job_resilience_shrink(job_record_t *job_ptr, node_record_t *node_ptr)
{
	bitstr_t *orig_job_node_bitmap;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	if (!job_ptr->job_resrcs || (job_ptr->node_cnt <= 1))
		return SLURM_ERROR;

	if (!job_ptr->resilience_shrink_time) {
		job_ptr->resilience_shrink_time = time(NULL);
		job_ptr->resilience_orig_node_cnt = job_ptr->node_cnt;
		FREE_NULL_BITMAP(job_ptr->resilience_orig_bitmap);
		job_ptr->resilience_orig_bitmap =
			bit_copy(job_ptr->node_bitmap);
	}

	info("%s: removing failed node %s from %pJ, now %u of %u nodes",
	     __func__, node_ptr->name, job_ptr, job_ptr->node_cnt - 1,
	     job_ptr->resilience_orig_node_cnt);

	/* Same sequence as the kill_on_node_fail=0 handling in job_mgr.c */
	srun_node_fail(job_ptr, node_ptr->name);
	job_pre_resize_acctg(job_ptr);
	kill_step_on_node(job_ptr, node_ptr, true);
	orig_job_node_bitmap = bit_copy(job_ptr->job_resrcs->node_bitmap);
	excise_node_from_job(job_ptr, node_ptr);
	rebuild_step_bitmaps(job_ptr, orig_job_node_bitmap);
	FREE_NULL_BITMAP(orig_job_node_bitmap);
	(void) gs_job_start(job_ptr);
	gres_stepmgr_job_build_details(job_ptr->gres_list_alloc, job_ptr->nodes,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str,
				       &job_ptr->gres_used);
	job_post_resize_acctg(job_ptr);

	job_ptr->state_reason = WAIT_RESILIENCE_RECOVERY;
	xfree(job_ptr->state_desc);
	xstrfmtcat(job_ptr->state_desc,
		   "Lost node %s, running on %u of %u nodes", node_ptr->name,
		   job_ptr->node_cnt, job_ptr->resilience_orig_node_cnt);
	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

static int _foreach_node_recovered(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	node_record_t *node_ptr = arg;

	if (!(job_ptr->bit_flags & ADAPTIVE_RESILIENCE) ||
	    !job_ptr->resilience_shrink_time || !IS_JOB_RUNNING(job_ptr))
		return 0;
	if (!job_ptr->resilience_orig_bitmap ||
	    !bit_test(job_ptr->resilience_orig_bitmap, node_ptr->index) ||
	    bit_test(job_ptr->node_bitmap, node_ptr->index))
		return 0;

	/*
	 * Growing the allocation back is left to a later change; for now
	 * only report that the node is available again.
	 */
	info("%s: node %s lost by %pJ is back in service, job running on %u of %u nodes",
	     __func__, node_ptr->name, job_ptr, job_ptr->node_cnt,
	     job_ptr->resilience_orig_node_cnt);
	xfree(job_ptr->state_desc);
	xstrfmtcat(job_ptr->state_desc,
		   "Node %s recovered, running on %u of %u nodes",
		   node_ptr->name, job_ptr->node_cnt,
		   job_ptr->resilience_orig_node_cnt);
	last_job_update = time(NULL);

	return 0;
}

extern void job_resilience_node_recovered(node_record_t *node_ptr)
{
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	list_for_each(job_list, _foreach_node_recovered, node_ptr);
}

extern void job_resilience_reset(job_record_t *job_ptr)
{
	job_ptr->resilience_shrink_time = 0;
	job_ptr->resilience_orig_node_cnt = 0;
	FREE_NULL_BITMAP(job_ptr->resilience_orig_bitmap);
}
