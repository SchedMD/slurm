/*****************************************************************************\
 *  health_check_cycle-test.c - Test monotonic health-check chunk pacing.
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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/common/hostlist.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xstring.h"
#include "src/interfaces/select.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/ping_nodes.h"

static int _get_monotonic_time(clockid_t clock_id, struct timespec *now);
static time_t _get_wall_time(time_t *now);
static int _refresh_select_nodeinfo(void);
static void _capture_health_check(agent_arg_t *args);

#define clock_gettime(clock_id, now) _get_monotonic_time((clock_id), (now))
#define time(now) _get_wall_time(now)
#define select_g_select_nodeinfo_set_all() _refresh_select_nodeinfo()
#define agent_queue_request(args) _capture_health_check(args)

#include "src/slurmctld/ping_nodes.c"

#undef clock_gettime
#undef time
#undef select_g_select_nodeinfo_set_all
#undef agent_queue_request

/* Included production headers may define NDEBUG in release builds. */
#undef NDEBUG
#include <assert.h>

bool cloud_dns = false;
bool dynamic_node_dns = false;

static timespec_t monotonic_now = { 0 };
static time_t wall_now = 0;
static int monotonic_reads = 0;
static int select_refreshes = 0;
static int queued_batches = 0;
static int expected_first_node = 0;
static int expected_node_count = 10;

/*
 * Supply the monotonic clock without replacing production timespec math.
 * IN clock_id - requested clock, which must be CLOCK_MONOTONIC
 * OUT now - fake monotonic timestamp
 * RET zero
 */
static int _get_monotonic_time(clockid_t clock_id, struct timespec *now)
{
	assert(clock_id == CLOCK_MONOTONIC);
	assert(now);
	*now = monotonic_now;
	monotonic_reads++;
	return 0;
}

/*
 * Supply wall time independently of the chunk-pacing clock.
 * OUT now - optional wall timestamp
 * RET fake wall timestamp
 */
static time_t _get_wall_time(time_t *now)
{
	if (now)
		*now = wall_now;
	return wall_now;
}

/*
 * Record select refreshes without loading a plugin.
 * RET SLURM_SUCCESS
 */
static int _refresh_select_nodeinfo(void)
{
	select_refreshes++;
	return SLURM_SUCCESS;
}

/*
 * Verify each real node-iteration result and consume its production arguments.
 * IN args - queued agent arguments, ownership taken
 */
static void _capture_health_check(agent_arg_t *args)
{
	assert(args->msg_type == REQUEST_HEALTH_CHECK);
	assert(!args->msg_args);
	assert(args->r_uid_set);
	assert(args->protocol_version == SLURM_26_05_PROTOCOL_VERSION);
	assert(args->node_count == expected_node_count);
	assert(hostlist_count(args->hostlist) == expected_node_count);
	for (int i = 0; i < expected_node_count; i++) {
		char *name = hostlist_shift(args->hostlist);

		assert(name);
		assert(!xstrcmp(name,
				node_record_table_ptr[expected_first_node + i]
					->name));
		xfree(name);
	}
	assert(!hostlist_count(args->hostlist));
	expected_first_node = (expected_first_node + expected_node_count) % 40;
	queued_batches++;
	purge_agent_args(args);
}

/*
 * Check one cyclic invocation against its dispatch and clock-read counts.
 * IN seconds - fake monotonic seconds
 * IN nanoseconds - fake monotonic nanoseconds
 * IN wall_time - independent fake wall timestamp
 * IN expected_batches - cumulative queued batch count
 */
static void _check_cycle_chunk(time_t seconds, long nanoseconds,
			       time_t wall_time, int expected_batches)
{
	int previous_reads = monotonic_reads;

	monotonic_now.tv_sec = seconds;
	monotonic_now.tv_nsec = nanoseconds;
	wall_now = wall_time;
	run_health_check();
	assert(monotonic_reads == previous_reads + 1);
	assert(queued_batches == expected_batches);
	assert(select_refreshes == expected_batches);
}

int main(void)
{
	node_record_t *nodes = xcalloc(40, sizeof(*nodes));

	node_record_count = active_node_record_count = 40;
	node_record_table_ptr = xcalloc(40, sizeof(*node_record_table_ptr));
	for (int i = 0; i < 40; i++) {
		node_record_table_ptr[i] = &nodes[i];
		nodes[i].index = i;
		nodes[i].name = xstrdup_printf("node%02d", i);
		nodes[i].node_state = NODE_STATE_IDLE;
		nodes[i].protocol_version = SLURM_26_05_PROTOCOL_VERSION;
	}
	slurm_conf.conf_flags = 0;
	slurm_conf.health_check_interval = 60;
	slurm_conf.health_check_node_state =
		HEALTH_CHECK_NODE_ANY | HEALTH_CHECK_CYCLE;

	_check_cycle_chunk(100, 900000000, 1000, 1);
	_check_cycle_chunk(101, 900000000, 100000, 1);
	_check_cycle_chunk(102, 899999999, 200000, 1);
	_check_cycle_chunk(102, 900000000, 900, 2);
	_check_cycle_chunk(102, 900000001, 300000, 2);
	_check_cycle_chunk(104, 900000000, 800, 3);
	_check_cycle_chunk(106, 900000000, 700, 4);
	assert(!expected_first_node);

	/* Non-CYCLE checks must not consult or wait on the monotonic clock. */
	slurm_conf.health_check_node_state = HEALTH_CHECK_NODE_ANY;
	expected_node_count = 40;
	for (int i = 0; i < 2; i++) {
		run_health_check();
		assert(queued_batches == 5 + i);
		assert(select_refreshes == 5 + i);
		assert(monotonic_reads == 7);
		assert(!expected_first_node);
	}

	for (int i = 0; i < 40; i++)
		xfree(nodes[i].name);
	xfree(node_record_table_ptr);
	xfree(nodes);
	node_record_count = active_node_record_count = 0;
	return 0;
}
