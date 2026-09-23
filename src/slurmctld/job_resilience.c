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
#include "src/common/identity.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_resilience.h"
#include "src/slurmctld/job_scheduler.h"
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

/*
 * Start an allocation-only job for the same user on node_ptr with an
 * "expand:<jobid>" dependency on job_ptr and merge it into job_ptr. This is
 * the same sequence a user follows by hand to grow a running job.
 */
static int _regrow_with_node(job_record_t *job_ptr, node_record_t *node_ptr)
{
	job_desc_msg_t *job_desc = xmalloc(sizeof(*job_desc));
	job_details_t *detail_ptr = job_ptr->details;
	job_record_t *helper_ptr = NULL;
	char *err_msg = NULL;
	int rc;

	slurm_init_job_desc_msg(job_desc);
	job_desc->user_id = job_ptr->user_id;
	job_desc->group_id = job_ptr->group_id;
	job_desc->id = copy_identity(job_ptr->id);
	xstrfmtcat(job_desc->name, "resilience-regrow-%u", job_ptr->job_id);
	job_desc->partition = xstrdup(job_ptr->part_ptr->name);
	job_desc->account = xstrdup(job_ptr->account);
	if (job_ptr->qos_ptr)
		job_desc->qos = xstrdup(job_ptr->qos_ptr->name);
	job_desc->wckey = xstrdup(job_ptr->wckey);
	job_desc->work_dir =
		xstrdup(detail_ptr->work_dir ? detail_ptr->work_dir : "/");
	job_desc->alloc_node = xstrdup("slurmctld");
	xstrfmtcat(job_desc->dependency, "expand:%u", job_ptr->job_id);
	job_desc->req_nodes = xstrdup(node_ptr->name);
	job_desc->min_nodes = 1;
	job_desc->max_nodes = 1;
	job_desc->immediate = 1;
	job_desc->het_job_offset = NO_VAL;
	job_desc->time_limit = job_ptr->time_limit;
	if (detail_ptr->whole_node & WHOLE_NODE_REQUIRED)
		job_desc->shared = JOB_SHARED_NONE;
	if (detail_ptr->ntasks_per_node)
		job_desc->ntasks_per_node = detail_ptr->ntasks_per_node;
	if (detail_ptr->cpus_per_task)
		job_desc->cpus_per_task = detail_ptr->cpus_per_task;
	job_desc->pn_min_memory = detail_ptr->pn_min_memory;

	/*
	 * Submitted as uid 0 with a node list so job_allocate() does not
	 * treat it as a fragmentation risk; the allocation itself is charged
	 * to the job's user, account and QOS as usual.
	 */
	rc = job_allocate(job_desc, 1, false, NULL, true, 0, false, &helper_ptr,
			  &err_msg, SLURM_PROTOCOL_VERSION);
	slurm_free_job_desc_msg(job_desc);
	if (err_msg) {
		debug("%s: %pJ regrow on %s: %s", __func__, job_ptr,
		      node_ptr->name, err_msg);
		xfree(err_msg);
	}
	if (!helper_ptr || !IS_JOB_RUNNING(helper_ptr)) {
		info("%s: could not allocate %s to grow %pJ: %s", __func__,
		     node_ptr->name, job_ptr, slurm_strerror(rc));
		if (helper_ptr && !IS_JOB_FINISHED(helper_ptr))
			(void) job_signal(helper_ptr, SIGKILL, 0, 0, false);
		return SLURM_ERROR;
	}

	rc = job_expand_merge(helper_ptr);
	if (rc != SLURM_SUCCESS) {
		error("%s: could not merge %pJ into %pJ: %s", __func__,
		      helper_ptr, job_ptr, slurm_strerror(rc));
		(void) job_signal(helper_ptr, SIGKILL, 0, 0, false);
		return rc;
	}

	info("%s: %pJ grown back onto %s, now %u of %u nodes", __func__,
	     job_ptr, node_ptr->name, job_ptr->node_cnt,
	     job_ptr->resilience_orig_node_cnt);
	return SLURM_SUCCESS;
}

static int _foreach_collect_shrunk(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	list_t *shrunk_jobs = arg;

	if ((job_ptr->bit_flags & ADAPTIVE_RESILIENCE) &&
	    job_ptr->resilience_shrink_time &&
	    job_ptr->resilience_orig_bitmap && IS_JOB_RUNNING(job_ptr) &&
	    !IS_JOB_CONFIGURING(job_ptr) &&
	    (job_ptr->node_cnt < job_ptr->resilience_orig_node_cnt))
		list_append(shrunk_jobs, job_ptr);

	return 0;
}

static void _regrow_job(job_record_t *job_ptr)
{
	bitstr_t *cand_bitmap = bit_copy(job_ptr->resilience_orig_bitmap);
	node_record_t *node_ptr;

	/* Original nodes the job no longer holds and that are idle again */
	bit_and_not(cand_bitmap, job_ptr->node_bitmap);
	bit_and(cand_bitmap, avail_node_bitmap);
	bit_and(cand_bitmap, idle_node_bitmap);

	for (int i = 0; (node_ptr = next_node_bitmap(cand_bitmap, &i)); i++) {
		if (job_ptr->node_cnt >= job_ptr->resilience_orig_node_cnt)
			break;
		if (_regrow_with_node(job_ptr, node_ptr) != SLURM_SUCCESS)
			break;
		xfree(job_ptr->state_desc);
		if (job_ptr->node_cnt >= job_ptr->resilience_orig_node_cnt) {
			job_ptr->state_reason = WAIT_NO_REASON;
			job_resilience_reset(job_ptr);
		} else {
			xstrfmtcat(
				job_ptr->state_desc,
				"Regained node %s, running on %u of %u nodes",
				node_ptr->name, job_ptr->node_cnt,
				job_ptr->resilience_orig_node_cnt);
		}
		last_job_update = time(NULL);
	}
	FREE_NULL_BITMAP(cand_bitmap);
}

extern void job_resilience_regrow_all(void)
{
	list_t *shrunk_jobs;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	if (!permit_job_expansion())
		return;

	/*
	 * Collect first: allocating the helper job appends to job_list, which
	 * can not happen while job_list is being iterated.
	 */
	shrunk_jobs = list_create(NULL);
	list_for_each(job_list, _foreach_collect_shrunk, shrunk_jobs);
	if (list_count(shrunk_jobs)) {
		job_record_t *job_ptr;
		while ((job_ptr = list_pop(shrunk_jobs)))
			_regrow_job(job_ptr);
	}
	FREE_NULL_LIST(shrunk_jobs);
}
