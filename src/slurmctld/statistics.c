/****************************************************************************\
 *  statistics.c - functions for sdiag command
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, December 2011
 *  Written by Alejandro Lucero <alucero@bsc.es>
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/id_util.h"
#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/xstring.h"
#include "src/common/slurmdbd_defs.h"

#include "src/interfaces/select.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/statistics.h"

typedef struct foreach_part_gen_stats {
	jobs_stats_t *js;
	nodes_stats_t *ns;
	partitions_stats_t *ps;
} foreach_part_gen_stats_t;

/* Pack all scheduling statistics */
extern buf_t *pack_all_stat(uint16_t protocol_version)
{
	buf_t *buffer;
	int agent_queue_size;
	int agent_count;
	int agent_thread_count;
	int slurmdbd_queue_size = 0;
	time_t now = time(NULL);

	if (acct_storage_g_get_data(acct_db_conn, ACCT_STORAGE_INFO_AGENT_COUNT,
				    &slurmdbd_queue_size) != SLURM_SUCCESS)
		slurmdbd_queue_size = 0;

	buffer = init_buf(BUF_SIZE);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(1, buffer); /* please remove on next version */

		pack_time(now, buffer);
		debug3("%s: time = %lu", __func__, last_proc_req_start);
		pack_time(last_proc_req_start, buffer);

		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		debug3("%s: server_thread_count = %u",
		       __func__, slurmctld_config.server_thread_count);
		pack32(slurmctld_config.server_thread_count, buffer);
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		agent_queue_size = retry_list_size();
		pack32(agent_queue_size, buffer);
		agent_count = get_agent_count();
		pack32(agent_count, buffer);
		agent_thread_count = get_agent_thread_count();
		pack32(agent_thread_count, buffer);
		pack32(slurmdbd_queue_size, buffer);
		pack32(slurmctld_diag_stats.latency, buffer);

		pack32(slurmctld_diag_stats.jobs_submitted, buffer);
		pack32(slurmctld_diag_stats.jobs_started, buffer);
		pack32(slurmctld_diag_stats.jobs_completed, buffer);
		pack32(slurmctld_diag_stats.jobs_canceled, buffer);
		pack32(slurmctld_diag_stats.jobs_failed, buffer);

		pack32(slurmctld_diag_stats.jobs_pending, buffer);
		pack32(slurmctld_diag_stats.jobs_running, buffer);
		pack_time(slurmctld_diag_stats.job_states_ts, buffer);

		pack32(slurmctld_diag_stats.schedule_cycle_max, buffer);
		pack32(slurmctld_diag_stats.schedule_cycle_last, buffer);
		pack32(slurmctld_diag_stats.schedule_cycle_sum, buffer);
		pack32(slurmctld_diag_stats.schedule_cycle_counter, buffer);
		pack32(slurmctld_diag_stats.schedule_cycle_depth, buffer);
		pack32_array(slurmctld_diag_stats.schedule_exit,
			     SCHEDULE_EXIT_COUNT, buffer);
		pack32(slurmctld_diag_stats.schedule_queue_len, buffer);

		pack32(slurmctld_diag_stats.backfilled_jobs, buffer);
		pack32(slurmctld_diag_stats.last_backfilled_jobs, buffer);
		pack32(slurmctld_diag_stats.bf_cycle_counter, buffer);
		pack64(slurmctld_diag_stats.bf_cycle_sum, buffer);
		pack32(slurmctld_diag_stats.bf_cycle_last, buffer);
		pack32(slurmctld_diag_stats.bf_last_depth, buffer);
		pack32(slurmctld_diag_stats.bf_last_depth_try, buffer);

		pack32(slurmctld_diag_stats.bf_queue_len, buffer);
		pack32(slurmctld_diag_stats.bf_cycle_max, buffer);
		pack_time(slurmctld_diag_stats.bf_when_last_cycle, buffer);
		pack32(slurmctld_diag_stats.bf_depth_sum, buffer);
		pack32(slurmctld_diag_stats.bf_depth_try_sum, buffer);
		pack32(slurmctld_diag_stats.bf_queue_len_sum, buffer);
		pack32(slurmctld_diag_stats.bf_table_size, buffer);
		pack32(slurmctld_diag_stats.bf_table_size_sum, buffer);

		pack32(slurmctld_diag_stats.bf_active, buffer);
		pack32(slurmctld_diag_stats.backfilled_het_jobs, buffer);
		pack32_array(slurmctld_diag_stats.bf_exit, BF_EXIT_COUNT,
			     buffer);
	}

	return buffer;
}

/* Reset all scheduling statistics
 * level IN - clear backfilled_jobs count if set */
extern void reset_stats(int level)
{
	slurmctld_diag_stats.proc_req_raw = 0;
	slurmctld_diag_stats.proc_req_threads = 0;
	slurmctld_diag_stats.schedule_cycle_max = 0;
	slurmctld_diag_stats.schedule_cycle_sum = 0;
	slurmctld_diag_stats.schedule_cycle_counter = 0;
	slurmctld_diag_stats.schedule_cycle_depth = 0;
	slurmctld_diag_stats.jobs_submitted = 0;
	slurmctld_diag_stats.jobs_started = 0;
	slurmctld_diag_stats.jobs_completed = 0;
	slurmctld_diag_stats.jobs_canceled = 0;
	slurmctld_diag_stats.jobs_failed = 0;

	memset(slurmctld_diag_stats.schedule_exit, 0,
	       sizeof(slurmctld_diag_stats.schedule_exit));

	/* Just resetting this value when reset requested explicitly */
	if (level)
		slurmctld_diag_stats.backfilled_jobs = 0;

	slurmctld_diag_stats.last_backfilled_jobs = 0;
	slurmctld_diag_stats.backfilled_het_jobs = 0;
	slurmctld_diag_stats.bf_cycle_counter = 0;
	slurmctld_diag_stats.bf_cycle_sum = 0;
	slurmctld_diag_stats.bf_cycle_last = 0;
	slurmctld_diag_stats.bf_depth_sum = 0;
	slurmctld_diag_stats.bf_depth_try_sum = 0;
	slurmctld_diag_stats.bf_queue_len = 0;
	slurmctld_diag_stats.bf_queue_len_sum = 0;
	slurmctld_diag_stats.bf_table_size_sum = 0;
	slurmctld_diag_stats.bf_cycle_max = 0;
	slurmctld_diag_stats.bf_last_depth = 0;
	slurmctld_diag_stats.bf_last_depth_try = 0;

	memset(slurmctld_diag_stats.bf_exit, 0,
	       sizeof(slurmctld_diag_stats.bf_exit));

	last_proc_req_start = time(NULL);
}

static void _free_job_stats(job_stats_t *j)
{
	xfree(j->user_name);
	xfree(j->partition);
	xfree(j->account);
	xfree(j);
}

static void _free_part_stats(partition_stats_t *s)
{
	xfree(s->name);
	xfree(s);
}

static void _free_ua_stats(ua_stats_t *ua)
{
	statistics_free_jobs(ua->s);
	xfree(ua->name);
	xfree(ua);
}

static int _find_ua_by_name(void *x, void *y)
{
	ua_stats_t *ua = x;

	if (!xstrcmp(ua->name, (char *) y))
		return 1;
	return 0;
}

static int _statistics_part_aggregate_job(void *x, void *arg)
{
	partition_stats_t *ps = arg;
	job_stats_t *j = x;

	if (xstrcmp(j->partition, ps->name))
		return SLURM_SUCCESS;

	if (IS_JOB_BOOT_FAIL(j))
		ps->jobs_bootfail++;
	if (IS_JOB_CANCELLED(j))
		ps->jobs_cancelled++;
	if (IS_JOB_COMPLETE(j))
		ps->jobs_completed++;
	if (IS_JOB_DEADLINE(j))
		ps->jobs_deadline++;
	if (IS_JOB_FAILED(j))
		ps->jobs_failed++;
	if (IS_JOB_NODE_FAILED(j))
		ps->jobs_node_failed++;
	if (IS_JOB_OOM(j))
		ps->jobs_oom++;
	if (IS_JOB_PENDING(j)) {
		if ((j->state_reason == WAIT_HELD) ||
		    (j->state_reason == WAIT_HELD_USER)) {
			ps->jobs_hold += j->job_array_cnt;
		}
		ps->jobs_pending += j->job_array_cnt;
	}
	if (IS_JOB_PREEMPTED(j))
		ps->jobs_preempted++;
	if (IS_JOB_RUNNING(j))
		ps->jobs_running++;
	if (IS_JOB_SUSPENDED(j))
		ps->jobs_suspended++;
	if (IS_JOB_TIMEOUT(j))
		ps->jobs_timeout++;

	/* Derived job states */
	if (IS_JOB_COMPLETING(j))
		ps->jobs_completing++;
	if (IS_JOB_CONFIGURING(j))
		ps->jobs_configuring++;
	if (IS_JOB_POWER_UP_NODE(j))
		ps->jobs_powerup_node++;
	if (IS_JOB_REQUEUED(j))
		ps->jobs_requeued++;
	if (IS_JOB_STAGE_OUT(j))
		ps->jobs_stageout++;

	/* Custom metric for Slinky */
	if (j->state_reason & WAIT_PART_NODE_LIMIT)
		ps->jobs_wait_part_node_limit++;

	if (IS_JOB_RUNNING(j) || IS_JOB_SUSPENDED(j)) {
		ps->jobs_cpus_alloc += j->cpus_alloc;
		ps->jobs_memory_alloc += j->memory_alloc;
	}

	if (!IS_JOB_COMPLETED(j))
		ps->jobs++;

	/*
	 * Custom Slinky metrics for autoscaling
	 * Max of the min_nodes required of all pending jobs in that partition
	 * Max of the max_nodes required of all pending jobs in that partition
	 */
	if (IS_JOB_PENDING(j)) {
		if (!((j->state_reason == WAIT_HELD) ||
		      (j->state_reason == WAIT_HELD_USER))) {
			ps->jobs_max_job_nodes_nohold =
				MAX(ps->jobs_max_job_nodes_nohold,
				    j->max_nodes);
			ps->jobs_min_job_nodes_nohold =
				MAX(ps->jobs_min_job_nodes_nohold,
				    j->min_nodes);
		}
		ps->jobs_max_job_nodes =
			MAX(ps->jobs_max_job_nodes, j->max_nodes);
		ps->jobs_min_job_nodes =
			MAX(ps->jobs_min_job_nodes, j->min_nodes);
	}

	return SLURM_SUCCESS;
}

static void _statistics_part_aggregate_node(partition_stats_t *ps,
					    node_stats_t *ns)
{
	ps->nodes_cpus_alloc += ns->cpus_alloc;
	ps->nodes_cpus_efctv += ns->cpus_efctv;
	ps->nodes_cpus_idle += ns->cpus_idle;

	ps->nodes_mem_alloc += ns->mem_alloc;
	ps->nodes_mem_avail += ns->mem_avail;
	ps->nodes_mem_free += ns->mem_free;
	ps->nodes_mem_total += ns->mem_total;

	if (IS_NODE_FUTURE(ns))
		ps->nodes_future++;
	else if (IS_NODE_DOWN(ns))
		ps->nodes_down++;
	else if (IS_NODE_MIXED(ns))
		ps->nodes_mixed++;
	else if (IS_NODE_ALLOCATED(ns))
		ps->nodes_alloc++;
	else if (IS_NODE_IDLE(ns))
		ps->nodes_idle++;
	else if (IS_NODE_UNKNOWN(ns))
		ps->nodes_unknown++;

	/* Derived node states */
	else if (IS_NODE_MAINT(ns))
		ps->nodes_maint++;

	if (IS_NODE_DRAINING(ns))
		ps->nodes_draining++;
	else if (IS_NODE_DRAIN(ns))
		ps->nodes_drain++;

	if (IS_NODE_RES(ns))
		ps->nodes_resv++;
	if (IS_NODE_COMPLETING(ns))
		ps->nodes_cg++;

	if (IS_NODE_FAIL(ns))
		ps->nodes_fail++;
	if (IS_NODE_NO_RESPOND(ns))
		ps->nodes_no_resp++;
	if (IS_NODE_PLANNED(ns))
		ps->nodes_planned++;
	if (IS_NODE_REBOOT_REQUESTED(ns))
		ps->nodes_reboot_requested++;
}

static int _get_part_statistics(void *x, void *arg)
{
	part_record_t *part_ptr = x;
	partitions_stats_t *ps = ((foreach_part_gen_stats_t *) arg)->ps;
	nodes_stats_t *ns = ((foreach_part_gen_stats_t *) arg)->ns;
	jobs_stats_t *js = ((foreach_part_gen_stats_t *) arg)->js;
	partition_stats_t *s = xmalloc(sizeof(*s));

	s->total_cpus = part_ptr->total_cpus;
	s->total_nodes = part_ptr->total_nodes;
	s->name = xstrdup(part_ptr->name);

	for (int i = 0; i < ns->node_stats_count; i++) {
		if (!bit_test(part_ptr->node_bitmap, i))
			continue;
		_statistics_part_aggregate_node(s, ns->node_stats_table[i]);
	}
	list_for_each_ro(js->jobs, _statistics_part_aggregate_job, s);

	list_append(ps->parts, s);

	return SLURM_SUCCESS;
}

static int _fill_jobs_statistics(void *x, void *arg)
{
	job_record_t *j = x;
	jobs_stats_t *js = arg;
	job_stats_t *new = xmalloc(sizeof(*new));

	new->job_array_cnt = (j->array_recs && j->array_recs->task_cnt) ?
				     j->array_recs->task_cnt :
				     1;

	if (IS_JOB_BOOT_FAIL(j))
		js->bootfail++;
	if (IS_JOB_CANCELLED(j))
		js->cancelled++;
	if (IS_JOB_COMPLETE(j))
		js->completed++;
	if (IS_JOB_DEADLINE(j))
		js->deadline++;
	if (IS_JOB_FAILED(j))
		js->failed++;
	if (IS_JOB_NODE_FAILED(j))
		js->node_failed++;
	if (IS_JOB_OOM(j))
		js->oom++;
	if (IS_JOB_PENDING(j)) {
		if ((j->state_reason == WAIT_HELD) ||
		    (j->state_reason == WAIT_HELD_USER)) {
			js->hold += new->job_array_cnt;
		}
		js->pending += new->job_array_cnt;
	}
	if (IS_JOB_PREEMPTED(j))
		js->preempted++;
	if (IS_JOB_RUNNING(j))
		js->running++;
	if (IS_JOB_SUSPENDED(j))
		js->suspended++;
	if (IS_JOB_TIMEOUT(j))
		js->timeout++;

	/* Derived job states */
	if (IS_JOB_COMPLETING(j))
		js->completing++;
	if (IS_JOB_CONFIGURING(j))
		js->configuring++;
	if (IS_JOB_POWER_UP_NODE(j))
		js->powerup_node++;
	if (IS_JOB_REQUEUED(j))
		js->requeued++;
	if (IS_JOB_STAGE_OUT(j))
		js->stageout++;

	/* Store individual records */
	new->job_id = j->job_id;
	new->partition = xstrdup(j->part_ptr->name);
	if (!j->user_name)
		new->user_name = user_from_job(j);
	else
		new->user_name = xstrdup(j->user_name);
	new->account = xstrdup(j->account);

	if (IS_JOB_RUNNING(j) || IS_JOB_SUSPENDED(j)) {
		new->cpus_alloc = j->total_cpus;
		new->nodes_alloc = j->total_nodes;
		new->memory_alloc =
			(j->tres_alloc_cnt ? j->tres_alloc_cnt[TRES_ARRAY_MEM] :
					     0);
	}

	/*
	 * Custom Slinky metrics for autoscaling
	 * Max of the min_nodes required of all pending jobs in that partition
	 * Max of the max_nodes required of all pending jobs in that partition
	 */
	if (IS_JOB_PENDING(j) && j->details) {
		new->min_nodes = j->details->min_nodes;
		new->max_nodes = MAX(j->details->max_nodes, new->min_nodes);
	}
	new->job_state = j->job_state;
	new->state_reason = j->state_reason;
	list_append(js->jobs, new);

	js->job_cnt += new->job_array_cnt;

	return SLURM_SUCCESS;
}

static void _aggregate_job_to_jobs(jobs_stats_t *s, job_stats_t *j)
{
	if (IS_JOB_BOOT_FAIL(j))
		s->bootfail++;
	if (IS_JOB_CANCELLED(j))
		s->cancelled++;
	if (IS_JOB_COMPLETE(j))
		s->completed++;
	if (IS_JOB_DEADLINE(j))
		s->deadline++;
	if (IS_JOB_FAILED(j))
		s->failed++;
	if (IS_JOB_NODE_FAILED(j))
		s->node_failed++;
	if (IS_JOB_OOM(j))
		s->oom++;
	if (IS_JOB_PENDING(j)) {
		if ((j->state_reason == WAIT_HELD) ||
		    (j->state_reason == WAIT_HELD_USER)) {
			s->hold += j->job_array_cnt;
		}
		s->pending += j->job_array_cnt;
	}
	if (IS_JOB_PREEMPTED(j))
		s->preempted++;
	if (IS_JOB_RUNNING(j))
		s->running++;
	if (IS_JOB_SUSPENDED(j))
		s->suspended++;
	if (IS_JOB_TIMEOUT(j))
		s->timeout++;

	/* Derived job states */
	if (IS_JOB_COMPLETING(j))
		s->completing++;
	if (IS_JOB_CONFIGURING(j))
		s->configuring++;
	if (IS_JOB_POWER_UP_NODE(j))
		s->powerup_node++;
	if (IS_JOB_REQUEUED(j))
		s->requeued++;
	if (IS_JOB_STAGE_OUT(j))
		s->stageout++;

	if (IS_JOB_RUNNING(j) || IS_JOB_SUSPENDED(j)) {
		s->cpus_alloc += j->cpus_alloc;
		s->memory_alloc += j->memory_alloc;
		s->nodes_alloc += j->nodes_alloc;
	}

	s->job_cnt++;
}

static int _get_users_accts(void *x, void *args)
{
	users_accts_stats_t *s = args;
	job_stats_t *j = x;
	ua_stats_t *us, *as;

	us = list_find_first(s->users, _find_ua_by_name, j->user_name);
	as = list_find_first(s->accounts, _find_ua_by_name, j->account);

	if (!us) {
		us = xmalloc(sizeof(*us));
		us->s = xmalloc(sizeof(*us->s));
		us->name = xstrdup(j->user_name);
		list_append(s->users, us);
	}
	if (!as) {
		as = xmalloc(sizeof(*as));
		as->s = xmalloc(sizeof(*as->s));
		as->name = xstrdup(j->account);
		list_append(s->accounts, as);
	}

	_aggregate_job_to_jobs(us->s, j);
	_aggregate_job_to_jobs(as->s, j);

	return SLURM_SUCCESS;
}

extern jobs_stats_t *statistics_get_jobs(bool lock)
{
	slurmctld_lock_t job_read_lock = { READ_LOCK, READ_LOCK, NO_LOCK,
					   READ_LOCK, READ_LOCK };
	jobs_stats_t *s = xmalloc(sizeof(*s));

	s->jobs = list_create((ListDelF) _free_job_stats);

	if (lock)
		lock_slurmctld(job_read_lock);

	list_for_each_ro(job_list, _fill_jobs_statistics, s);

	if (lock)
		unlock_slurmctld(job_read_lock);

	return s;
}

extern nodes_stats_t *statistics_get_nodes(bool lock)
{
	/*
	 * Locks: Read config, write node (reset allocated CPU count in some
	 * select plugins), read part (for part_is_visible)
	 */
	slurmctld_lock_t node_write_lock = { READ_LOCK, NO_LOCK, WRITE_LOCK,
					     READ_LOCK, NO_LOCK };
	node_record_t *node_ptr;
	nodes_stats_t *s = xmalloc(sizeof(*s));

	if (lock)
		lock_slurmctld(node_write_lock);

	select_g_select_nodeinfo_set_all();

	s->node_stats_table =
		xcalloc(node_record_count, sizeof(*s->node_stats_table));
	s->node_stats_count = node_record_count;

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		uint16_t idle_cpus = 0;
		node_stats_t *n;

		n = xmalloc(sizeof(*n));

		idle_cpus = node_ptr->cpus_efctv - node_ptr->alloc_cpus;
		n->name = xstrdup(node_ptr->name);
		n->cpus_alloc = node_ptr->alloc_cpus;
		n->cpus_efctv = node_ptr->cpus_efctv;
		n->cpus_idle = idle_cpus;
		n->cpus_total = node_ptr->cpus;
		n->mem_alloc = node_ptr->alloc_memory;
		n->mem_avail = node_ptr->real_memory - node_ptr->mem_spec_limit;
		n->mem_free =
			(node_ptr->free_mem == NO_VAL64 ? 0 :
							  node_ptr->free_mem);
		n->mem_total = node_ptr->real_memory;
		n->node_state = node_ptr->node_state;
		s->node_stats_table[i] = n;

		/*
		 * Base states are unique but can be combined with any other
		 * derived node state
		 */

		/* Base states */
		if (IS_NODE_FUTURE(node_ptr))
			s->future++;
		else if (IS_NODE_DOWN(node_ptr))
			s->down++;
		else if ((idle_cpus && (idle_cpus < node_ptr->cpus_efctv)) ||
			 (node_ptr->alloc_tres_fmt_str &&
			  (idle_cpus == node_ptr->cpus_efctv))) {
			n->node_state &= NODE_STATE_FLAGS;
			n->node_state |= NODE_STATE_MIXED;
			s->mixed++;
			/*
			 * The MIXED state is not set by the controller, it must
			 * be inferred - e.g. see also _set_node_mixed()
			 */
		} else if (IS_NODE_ALLOCATED(node_ptr))
			s->alloc++;
		else if (IS_NODE_IDLE(node_ptr))
			s->idle++;
		else if (IS_NODE_UNKNOWN(node_ptr))
			s->unknown++;

		/* Derived node states */
		if (IS_NODE_COMPLETING(node_ptr))
			s->cg++;

		if (IS_NODE_DRAINING(node_ptr))
			s->draining++;
		else if (IS_NODE_DRAIN(node_ptr))
			s->drain++;

		if (IS_NODE_FAIL(node_ptr))
			s->fail++;
		if (IS_NODE_INVALID_REG(node_ptr))
			s->invalid_reg++;
		if (IS_NODE_MAINT(node_ptr))
			s->maint++;
		if (IS_NODE_NO_RESPOND(node_ptr))
			s->no_resp++;
		if (IS_NODE_PLANNED(node_ptr))
			s->planned++;
		if (IS_NODE_REBOOT_REQUESTED(node_ptr))
			s->reboot_requested++;
		if (IS_NODE_RES(node_ptr))
			s->resv++;
	}

	if (lock)
		unlock_slurmctld(node_write_lock);

	return s;
}

extern partitions_stats_t *statistics_get_parts(nodes_stats_t *ns,
						jobs_stats_t *js, bool lock)
{
	/* Locks: Read configuration and partition */
	slurmctld_lock_t part_read_lock = { READ_LOCK, NO_LOCK, NO_LOCK,
					    READ_LOCK, NO_LOCK };
	foreach_part_gen_stats_t p = { 0 };
	partitions_stats_t *ps = xmalloc(sizeof(*ps));

	p.ps = ps;
	p.ns = ns;
	p.js = js;

	ps->parts = list_create((ListDelF) _free_part_stats);

	if (lock)
		lock_slurmctld(part_read_lock);

	list_for_each_ro(part_list, _get_part_statistics, &p);

	if (lock)
		unlock_slurmctld(part_read_lock);

	return ps;
}

extern scheduling_stats_t *statistics_get_sched(void)
{
	scheduling_stats_t *s = xmalloc(sizeof(*s));

	s->agent_queue_size = retry_list_size();
	s->agent_count = get_agent_count();
	s->agent_thread_count = get_agent_thread_count();
	s->diag_stats = xmalloc(sizeof(*(s->diag_stats)));
	memcpy(s->diag_stats, &slurmctld_diag_stats,
	       sizeof(slurmctld_diag_stats));

	/* extended diag stats */
	if (s->diag_stats->schedule_cycle_counter > 0) {
		s->sched_mean_cycle = s->diag_stats->schedule_cycle_sum /
				      s->diag_stats->schedule_cycle_counter;
		s->sched_mean_depth_cycle =
			s->diag_stats->schedule_cycle_depth /
			s->diag_stats->schedule_cycle_counter;
	}
	if (s->diag_stats->bf_cycle_counter > 0) {
		s->bf_depth_mean = s->diag_stats->bf_depth_sum /
				   s->diag_stats->bf_cycle_counter;
		s->bf_try_depth_mean = s->diag_stats->bf_depth_try_sum /
				       s->diag_stats->bf_cycle_counter;
		s->bf_queue_len_mean = s->diag_stats->bf_queue_len_sum /
				       s->diag_stats->bf_cycle_counter;
		s->bf_mean_table_sz = s->diag_stats->bf_table_size_sum /
				      s->diag_stats->bf_cycle_counter;
		s->bf_mean_cycle = s->diag_stats->bf_cycle_sum /
				   s->diag_stats->bf_cycle_counter;
	}
	s->last_proc_req_start = last_proc_req_start;
	if (acct_storage_g_get_data(acct_db_conn, ACCT_STORAGE_INFO_AGENT_COUNT,
				    &s->slurmdbd_queue_size) != SLURM_SUCCESS)
		s->slurmdbd_queue_size = 0;
	s->time = time(NULL);
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	s->server_thread_count = slurmctld_config.server_thread_count;
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

	return s;
}

extern users_accts_stats_t *statistics_get_users_accounts(jobs_stats_t *js)
{
	users_accts_stats_t *ua = xmalloc(sizeof(*ua));

	ua->users = list_create((ListDelF) _free_ua_stats);
	ua->accounts = list_create((ListDelF) _free_ua_stats);

	list_for_each(js->jobs, _get_users_accts, ua);

	return ua;
}

extern void statistics_free_jobs(jobs_stats_t *s)
{
	FREE_NULL_LIST(s->jobs);
	xfree(s);
}

extern void statistics_free_parts(partitions_stats_t *s)
{
	FREE_NULL_LIST(s->parts);
	xfree(s);
}

extern void statistics_free_nodes(nodes_stats_t *s)
{
	for (int i = 0; i < s->node_stats_count; i++) {
		if (!s->node_stats_table[i])
			continue;
		xfree(s->node_stats_table[i]->name);
		xfree(s->node_stats_table[i]);
	}
	xfree(s->node_stats_table);
	xfree(s);
}

extern void statistics_free_sched(scheduling_stats_t *s)
{
	xfree(s->diag_stats);
	xfree(s);
}

extern void statistics_free_users_accounts(users_accts_stats_t *s)
{
	FREE_NULL_LIST(s->users);
	FREE_NULL_LIST(s->accounts);
	xfree(s);
}
