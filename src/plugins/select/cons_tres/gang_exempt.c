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

#include "src/common/assoc_mgr.h"
#include "src/common/xhash.h"

#include "src/slurmctld/acct_policy.h"

/*
 * Cores that may not be shared: no job may be oversubscribed onto them under
 * gang scheduling. Holds the cores of jobs still within their
 * PreemptExemptTime.
 */
static bitstr_t **gang_exempt_cores = NULL;

/*
 * Cores from these jobs are exempt from GANG scheduling.
 */
static xhash_t *exempt_job_hash = NULL;

/* The second gang_exempt_cores was built for, and whether it is out of date. */
static time_t exempt_built_at = 0;
static bool exempt_stale = true;

/*
 * Return whether a job's cores are exempt from GANG scheduling.
 *
 * A job may be exempt until its PreemptExemptTime has elapsed, or if it's a
 * hetjob.
 *
 * Requires CONF_LOCK, JOB_LOCK, QOS_LOCK for acct_policy_get_preemptable_time()
 *
 * IN job_ptr - the job to test
 * RET true if the job's cores are exempt from sharing
 */
static bool _is_exempt(job_record_t *job_ptr)
{
	job_resources_t *job_resrcs = job_ptr->job_resrcs;

	if (!job_resrcs || !job_resrcs->core_bitmap || !job_resrcs->node_bitmap)
		return false;

	if (slurm_job_preempt_mode(job_ptr) != PREEMPT_MODE_SUSPEND)
		return false;

	return (time(NULL) < acct_policy_get_preemptable_time(job_ptr));
}

/*
 * Set one exempt job's allocated cores in gang_exempt_cores, allocating the
 * array and per-node bitmaps as needed.
 * IN job_ptr - the exempt job whose cores to mark
 */
static void _exempt_set_cores(job_record_t *job_ptr)
{
	job_resources_t *job_resrcs = job_ptr->job_resrcs;
	node_record_t *node_ptr = NULL;
	int core_cnt = 0;

	if (!gang_exempt_cores)
		gang_exempt_cores = build_core_array();

	/* See build_job_resources() for navigating job_resrcs bitmaps. */
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
}

/*
 * Key a job by its id. The entry is the id itself, so the item is
 * its own key.
 * IN item     - uint32_t job id from exempt_job_hash
 * OUT key     - the job id to hash on
 * OUT key_len - its length
 */
static void _exempt_key_id(void *item, const void **key, uint32_t *key_len)
{
	*key = item;
	*key_len = sizeof(uint32_t);
}

/*
 * Track job if not already tracked.
 * IN job_id - the job to start testing on every rebuild
 */
static void _exempt_track(uint32_t job_id)
{
	uint32_t *tracked;

	if (!exempt_job_hash)
		exempt_job_hash = xhash_init(_exempt_key_id, xfree_ptr);

	if (xhash_get(exempt_job_hash, &job_id, sizeof(job_id)))
		return;

	tracked = xmalloc(sizeof(*tracked));
	*tracked = job_id;
	xhash_add(exempt_job_hash, tracked);
}

/* State carried through one walk of exempt_job_hash. */
typedef struct {
	uint32_t *doomed; /* ids to drop, sized to the entry count */
	int doomed_cnt;
	int exempt_cnt;
} exempt_rebuild_args_t;

/*
 * Mark one offered job's cores, or select it for removal if it is gone or no
 * longer exempt. Requires the locks _is_exempt() documents.
 * IN item - uint32_t job id from exempt_job_hash
 * IN arg  - exempt_rebuild_args_t collecting the ids to drop
 */
static void _exempt_rebuild_foreach(void *item, void *arg)
{
	uint32_t job_id = *(uint32_t *) item;
	exempt_rebuild_args_t *args = arg;
	job_record_t *job_ptr = find_job_record(job_id);

	/*
	 * Resolve the id rather than holding the job pointer. job_fini() frees
	 * records without select_p_job_fini() running, and this is what drops
	 * an entry nothing else would.
	 */
	if (!job_ptr ||
	    !(IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) ||
	    !_is_exempt(job_ptr)) {
		args->doomed[args->doomed_cnt++] = job_id;
		return;
	}

	_exempt_set_cores(job_ptr);
	args->exempt_cnt++;
}

/*
 * Re-derive gang_exempt_cores as the union of the cores held by the offered
 * jobs which are still exempt, dropping the rest. Jobs may share cores, so the
 * union cannot be maintained one job at a time.
 * IN now - the current time
 */
static void _exempt_rebuild(time_t now)
{
	assoc_mgr_lock_t locks = { .qos = READ_LOCK };
	int offered = 0, exempt = 0;
	DEF_TIMERS;

	START_TIMER;

	/* Clear, not free: freeing would reallocate a bitmap per node. */
	if (gang_exempt_cores)
		clear_core_array(gang_exempt_cores);

	if (exempt_job_hash && (offered = xhash_count(exempt_job_hash))) {
		exempt_rebuild_args_t args = {
			.doomed = xcalloc(offered, sizeof(*args.doomed)),
		};

		/* Taken once here rather than per job in _is_exempt(). */
		assoc_mgr_lock(&locks);
		xhash_walk(exempt_job_hash, _exempt_rebuild_foreach, &args);
		assoc_mgr_unlock(&locks);

		for (int i = 0; i < args.doomed_cnt; i++)
			xhash_delete(exempt_job_hash, &args.doomed[i],
				     sizeof(args.doomed[i]));

		exempt = args.exempt_cnt;
		xfree(args.doomed);
	}

	exempt_built_at = now;
	exempt_stale = false;

	END_TIMER;
	/*
	 * Cost is dominated by marking cores, which is a per-core loop over
	 * every node each exempt job holds, so it grows with the cores those
	 * jobs span rather than with their number.
	 */
	log_flag(SELECT_TYPE, "rebuilt gang_exempt_cores: %d job(s) offered, %d still exempt, took %s",
		 offered, exempt, TIMER_STR());

	if (gang_exempt_cores)
		core_array_log("gang_exempt_cores", NULL, gang_exempt_cores);
}

/*
 * Return the cores that may not be shared, brought up to date first. This is
 * the only way to reach the array, so a caller cannot read it stale.
 * RET the exempt cores, or NULL if nothing is exempt
 */
extern bitstr_t **gang_exempt_get_cores(void)
{
	time_t now = time(NULL);

	if (!gang_mode)
		return NULL;

	/*
	 * Re-derive on a change, and once a second regardless, since a
	 * PreemptExemptTime may have elapsed. PreemptExemptTime has one-second
	 * granularity, so that is as often as this can matter.
	 */
	if (exempt_stale || (exempt_built_at != now))
		_exempt_rebuild(now);

	return gang_exempt_cores;
}

/*
 * Offer job_ptr's cores as exempt. Needs no locks: whether the job really is
 * exempt is settled by the next read of the exempt cores, which happens before
 * anything can be placed on them.
 * IN job_ptr - the job that now holds an allocation
 */
extern void gang_exempt_add_job(job_record_t *job_ptr)
{
	if (!gang_mode)
		return;

	_exempt_track(job_ptr->job_id);
	exempt_stale = true;

	log_flag(SELECT_TYPE, "%pJ offered to gang_exempt_cores", job_ptr);
}

/*
 * Stop offering job_ptr. A no-op for a job that was never offered.
 * IN job_ptr - the job that no longer holds an allocation
 */
extern void gang_exempt_remove_job(job_record_t *job_ptr)
{
	if (!gang_mode || !exempt_job_hash)
		return;

	/* Tested first so a job that was never offered stays a silent no-op. */
	if (!xhash_get(exempt_job_hash, &job_ptr->job_id,
		       sizeof(job_ptr->job_id)))
		return;

	xhash_delete(exempt_job_hash, &job_ptr->job_id,
		     sizeof(job_ptr->job_id));
	exempt_stale = true;

	log_flag(SELECT_TYPE, "%pJ dropped from gang_exempt_cores", job_ptr);
}

/* Mark the exempt cores stale, so the next read of them re-derives them. */
extern void gang_exempt_mark_stale(void)
{
	if (gang_mode)
		exempt_stale = true;
}

/* Discard the exempt cores at a node count that may have changed. */
extern void gang_exempt_node_init(void)
{
	/* Sized to node_record_count, so discard rather than clear. */
	free_core_array(&gang_exempt_cores);
	exempt_stale = true;
}

/* Release everything the exempt set holds. */
extern void gang_exempt_fini(void)
{
	xhash_free(exempt_job_hash);
	free_core_array(&gang_exempt_cores);
	exempt_built_at = 0;
	exempt_stale = true;
}
