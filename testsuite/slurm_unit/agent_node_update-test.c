/*****************************************************************************\
 *  agent_node_update-test.c - Test explicit wakeup for queued ping responses.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/common/threadpool.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/ping_nodes.h"

static int _schedule_create(void *args, bool detached, pthread_t *thread_id);
static int _schedule_join(pthread_t thread_id);
static int _wait_for_node_update(pthread_cond_t *cond, pthread_mutex_t *mutex,
				 const struct timespec *deadline);
static int _unexpected_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
static int _record_cond_signal(pthread_cond_t *cond);
static void _record_completion(void);

/* Discard transport callbacks so section GC can remove their dependencies. */
#define threadpool_create(func, func_name, arg, detached, name, id, caller) \
	_schedule_create((arg), (detached), (id))
#define threadpool_join(id, caller) _schedule_join(id)
#define pthread_cond_timedwait(cond, mutex, deadline) \
	_wait_for_node_update((cond), (mutex), (deadline))
#define pthread_cond_wait(cond, mutex) _unexpected_cond_wait((cond), (mutex))
#define pthread_cond_signal(cond) _record_cond_signal(cond)
/* Observe broadcast_end() before checking the wakeup. */
#define broadcast_end(...) _record_completion()

/* GCC 4.8 diagnoses discarded callbacks at the end of the translation unit. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "src/slurmctld/agent.c"

#undef threadpool_create
#undef threadpool_join
#undef pthread_cond_timedwait
#undef pthread_cond_wait
#undef pthread_cond_signal
#undef broadcast_end

/* Included production headers may define NDEBUG in release builds. */
#undef NDEBUG
#include <assert.h>

slurmctld_config_t slurmctld_config = { 0 };

static agent_arg_t *ping_args = NULL;
static agent_info_t *scheduled_agent = NULL;
static task_info_t *scheduled_task = NULL;
static int created_threads = 0;
static int joined_threads = 0;
static int updater_waits = 0;
static int updater_signals = 0;
static int completions = 0;
static int responses = 0;
static bool controller_locked = false;

/*
 * Record the watchdog and transport task without starting real threads.
 * IN args - production thread argument
 * IN detached - whether the requested thread is detached
 * OUT thread_id - synthetic joinable thread ID
 * RET SLURM_SUCCESS
 */
static int _schedule_create(void *args, bool detached, pthread_t *thread_id)
{
	assert(!detached);
	assert(thread_id);
	assert(args);
	created_threads++;
	assert(created_threads <= 2);
	*thread_id = (pthread_t) created_threads;
	if (created_threads == 1) {
		scheduled_agent = args;
		assert(scheduled_agent->msg_type == REQUEST_PING);
		assert(scheduled_agent->get_reply);
		assert(scheduled_agent->thread_count == 1);
	} else {
		scheduled_task = args;
		assert(scheduled_task->msg_type == REQUEST_PING);
		assert(scheduled_task->thread_struct_ptr ==
		       scheduled_agent->thread_struct);
	}
	return SLURM_SUCCESS;
}

/*
 * Complete transport before running the real watchdog and notifier.
 * IN thread_id - synthetic thread ID to join
 * RET SLURM_SUCCESS
 */
static int _schedule_join(pthread_t thread_id)
{
	assert(created_threads == 2);
	assert(joined_threads < 2);
	assert(thread_id == (pthread_t) (joined_threads + 1));
	if (!joined_threads) {
		assert(scheduled_task);
		slurm_mutex_lock(scheduled_task->thread_mutex_ptr);
		assert(*scheduled_task->threads_active_ptr == 1);
		assert(scheduled_task->thread_struct_ptr->state == DSH_NEW);
		scheduled_task->thread_struct_ptr->state = DSH_DONE;
		scheduled_task->thread_struct_ptr->nodename = xstrdup("node0");
		scheduled_task->thread_struct_ptr->end_time = 0;
		(*scheduled_task->threads_active_ptr)--;
		slurm_mutex_unlock(scheduled_task->thread_mutex_ptr);
		xfree(scheduled_task);
		assert(!_wdog(scheduled_agent));
	}
	joined_threads++;
	return SLURM_SUCCESS;
}

/*
 * Run the ping lifecycle while the updater is parked, then require its signal.
 * IN cond - updater condition
 * IN/OUT mutex - real queue mutex, released while the agent runs
 * IN deadline - production timed-wait deadline
 * RET zero after an explicit signal, or after requesting shutdown
 */
static int _wait_for_node_update(pthread_cond_t *cond, pthread_mutex_t *mutex,
				 const struct timespec *deadline)
{
	assert(cond == &update_nodes_cond);
	assert(mutex == &update_nodes_mutex);
	assert(deadline);
	assert(updater_waits < 2);
	if (!updater_waits) {
		assert(!slurmctld_config.shutdown_time);
		assert(!list_count(update_node_list));
		slurm_mutex_unlock(mutex);
		assert(!agent(ping_args));
		ping_args = NULL;
		/* A missing signal must fail before this wait can return. */
		assert(updater_signals == 1);
		assert(completions == 1);
		assert(created_threads == 2);
		assert(joined_threads == 2);
		assert(!agent_cnt);
		assert(!agent_thread_cnt);
		assert(!responses);
		assert(list_count(update_node_list) == 1);
		slurm_mutex_lock(mutex);
	} else {
		assert(responses == 1);
		assert(!list_count(update_node_list));
		slurmctld_config.shutdown_time = 1;
	}
	updater_waits++;
	return 0;
}

/*
 * Reject unscheduled waits instead of hanging on an unexpected agent path.
 * IN cond - unexpected condition
 * IN mutex - unexpected mutex
 * RET EINVAL if assertions are disabled
 */
static int _unexpected_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	assert(false);
	return EINVAL;
}

/*
 * Require the queued reply and completion before the updater's explicit signal.
 * IN cond - condition being signaled
 * RET pthread_cond_signal() result
 */
static int _record_cond_signal(pthread_cond_t *cond)
{
	if (cond == &update_nodes_cond) {
		assert(completions == 1);
		assert(!responses);
		assert(list_count(update_node_list) == 1);
		assert(!xstrcmp(list_peek(update_node_list), "node0"));
		updater_signals++;
		assert(updater_signals == 1);
	}
	return pthread_cond_signal(cond);
}

/* Record cyclic completion without depending on the shared ping gate API. */
static void _record_completion(void)
{
	assert(!completions);
	assert(list_count(update_node_list) == 1);
	completions++;
}

/*
 * Record the controller locks used to apply the real notification queue.
 * IN lock_levels - requested controller locks
 */
void lock_slurmctld(slurmctld_lock_t lock_levels)
{
	assert(!controller_locked);
	assert(lock_levels.conf == READ_LOCK);
	assert(lock_levels.node == WRITE_LOCK);
	controller_locked = true;
}

/*
 * Release the recorded controller locks.
 * IN lock_levels - controller locks being released
 */
void unlock_slurmctld(slurmctld_lock_t lock_levels)
{
	assert(controller_locked);
	assert(lock_levels.conf == READ_LOCK);
	assert(lock_levels.node == WRITE_LOCK);
	controller_locked = false;
}

/*
 * Verify the production updater drains exactly the successful ping response.
 * IN name - responding node name, owned by the production queue
 */
void node_did_resp(char *name)
{
	assert(controller_locked);
	assert(updater_signals == 1);
	assert(completions == 1);
	assert(!xstrcmp(name, "node0"));
	responses++;
	assert(responses == 1);
}

/*
 * Trap unrelated job completion.
 * IN msg - job completion request
 * IN step_id - completed job step
 * IN requeue - whether to requeue
 * IN node_fail - whether a node failed
 * IN job_exit_rc - job result
 * RET EINVAL if assertions are disabled
 */
int job_complete(slurm_msg_t *msg, slurm_step_id_t *step_id, bool requeue,
		 bool node_fail, job_exit_rc_t job_exit_rc)
{
	assert(false);
	return EINVAL;
}

/*
 * Trap unrelated job lookup.
 * IN job_id - job to look up
 * RET NULL if assertions are disabled
 */
job_record_t *find_job_record(uint32_t job_id)
{
	assert(false);
	return NULL;
}

/*
 * Trap unrelated prolog lookup.
 * IN job_ptr - job to inspect
 * RET false if assertions are disabled
 */
bool is_prolog_running(job_record_t *job_ptr)
{
	assert(false);
	return false;
}

/*
 * Trap a failed ping response.
 * IN name - node that did not respond
 * IN msg_time - request time
 * IN resp_type - response type
 */
void node_not_resp(char *name, time_t msg_time, slurm_msg_type_t resp_type)
{
	assert(false);
}

/*
 * Trap unrelated node draining.
 * IN nodes - nodes to drain
 * IN reason - drain reason
 * IN reason_uid - user requesting drain
 * RET EINVAL if assertions are disabled
 */
int drain_nodes(char *nodes, char *reason, uint32_t reason_uid)
{
	assert(false);
	return EINVAL;
}

/* Trap unrelated job scheduling. */
void queue_job_scheduler(void)
{
	assert(false);
}

/*
 * Trap unrelated sackd removal.
 * IN node - sackd node to remove
 */
void sackd_mgr_remove_node(char *node)
{
	assert(false);
}

int main(void)
{
	ping_args = xmalloc(sizeof(*ping_args));
	ping_args->broadcast_class = BC_PING;
	ping_args->msg_type = REQUEST_PING;
	ping_args->node_count = 1;
	ping_args->hostlist = hostlist_create("node0");
	ping_args->protocol_version = SLURM_PROTOCOL_VERSION;
	set_agent_arg_r_uid(ping_args, SLURM_AUTH_UID_ANY);

	assert(!_agent_nodes_update(NULL));
	assert(updater_waits == 2);
	assert(updater_signals == 1);
	assert(completions == 1);
	assert(responses == 1);
	assert(!controller_locked);
	assert(!ping_args);
	assert(!list_count(update_node_list));
	FREE_NULL_LIST(update_node_list);
	return 0;
}
