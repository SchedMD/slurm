/*****************************************************************************\
 *  ping_nodes-test.c - Test separation of previously unresponsive nodes.
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
#include <time.h>

#include "src/common/hostlist.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

static time_t _get_wall_time(time_t *now);
static void _capture_ping(agent_arg_t *args);

#define time(now) _get_wall_time(now)
#define agent_queue_request(args) _capture_ping(args)

#include "src/slurmctld/ping_nodes.c"

#undef time
#undef agent_queue_request

#undef NDEBUG
#include <assert.h>

bool cloud_dns = false;
bool dynamic_node_dns = false;

static int queued_agents = 0;

/*
 * Supply a fixed wall clock for the ping sweep.
 * OUT now - optional wall timestamp
 * RET fake wall timestamp
 */
static time_t _get_wall_time(time_t *now)
{
	if (now)
		*now = 1000;
	return 1000;
}

/*
 * Reject unexpected DOWN transitions during the first sweep.
 * IN node_ptr - node being marked DOWN
 * IN reason - reason for the transition
 */
extern void set_node_down_ptr(node_record_t *node_ptr, char *reason)
{
	assert(false);
}

/*
 * Verify each queued agent and consume its arguments.
 * IN args - queued agent arguments, ownership taken
 */
static void _capture_ping(agent_arg_t *args)
{
	char *name = hostlist_shift(args->hostlist);

	assert(args->node_count == 1);
	assert(name);
	assert(!hostlist_count(args->hostlist));
	assert(args->r_uid_set);
	if (!queued_agents) {
		assert(args->msg_type == REQUEST_PING);
		assert(!xstrcmp(name, "node1"));
	} else if (queued_agents == 1) {
		assert(args->msg_type == REQUEST_PING);
		assert(!xstrcmp(name, "node2"));
	} else {
		assert(queued_agents == 2);
		assert(args->msg_type == REQUEST_NODE_REGISTRATION_STATUS);
		assert(!xstrcmp(name, "node0"));
	}
	queued_agents++;
	xfree(name);
	purge_agent_args(args);
}

int main(void)
{
	node_record_t *nodes = xcalloc(4, sizeof(*nodes));

	node_record_count = active_node_record_count = 4;
	node_record_table_ptr = xcalloc(4, sizeof(*node_record_table_ptr));
	for (int i = 0; i < 4; i++) {
		node_record_table_ptr[i] = &nodes[i];
		nodes[i].index = i;
		nodes[i].name = xstrdup_printf("node%d", i);
		nodes[i].node_state = NODE_STATE_IDLE;
		nodes[i].boot_time = 1;
		nodes[i].last_response = 999;
		nodes[i].cpu_load_time = nodes[i].free_mem_time = 999;
		nodes[i].protocol_version = SLURM_PROTOCOL_VERSION;
	}
	slurm_conf.slurmd_timeout = 90;
	slurm_conf.tree_width = 1;
	/* A routine healthy ping can be due at 2 * SlurmdTimeout / 3. */
	nodes[1].last_response = 940;
	/* A failed RPC, not response age, identifies the separate population. */
	nodes[2].node_state |= NODE_STATE_NO_RESPOND;

	ping_nodes();
	assert(queued_agents == 3);
	assert(broadcast_in_flight[BC_PING] == 3);
	for (int i = 0; i < 3; i++)
		broadcast_end(BC_PING);
	assert(broadcast_done(BC_PING));

	for (int i = 0; i < 4; i++)
		xfree(nodes[i].name);
	xfree(node_record_table_ptr);
	xfree(nodes);
	node_record_count = active_node_record_count = 0;
	return 0;
}
