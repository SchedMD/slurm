/*****************************************************************************\
 *  ping_nodes.c - ping the slurmd daemons to test if they respond
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

#include "src/common/hostlist.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/slurmctld.h"

/* Request that nodes re-register at most every MAX_REG_FREQUENCY pings */
#define MAX_REG_FREQUENCY 20

/* Log an error for ping that takes more than 100 seconds to complete */
#define PING_TIMEOUT 100

static pthread_mutex_t lock_mutex = PTHREAD_MUTEX_INITIALIZER;
static int ping_count = 0;
static time_t ping_start = 0;

/*
 * is_ping_done - test if the last node ping cycle has completed.
 *	Use this to avoid starting a new set of ping requests before the
 *	previous one completes
 * RET true if ping process is done, false otherwise
 */
bool is_ping_done (void)
{
	static bool ping_msg_sent = false;
	bool is_done = true;

	slurm_mutex_lock(&lock_mutex);
	if (ping_count) {
		is_done = false;
		if (!ping_msg_sent &&
		    (difftime(time(NULL), ping_start) >= PING_TIMEOUT)) {
			error("Node ping apparently hung, "
			      "many nodes may be DOWN or configured "
			      "SlurmdTimeout should be increased");
			ping_msg_sent = true;
		}
	} else
		ping_msg_sent = false;
	slurm_mutex_unlock(&lock_mutex);

	return is_done;
}

/*
 * ping_begin - record that a ping cycle has begin. This can be called more
 *	than once (for REQUEST_PING and simultaneous REQUEST_NODE_REGISTRATION
 *	for selected nodes). Matching ping_end calls must be made for each
 *	before is_ping_done returns true.
 */
void ping_begin (void)
{
	slurm_mutex_lock(&lock_mutex);
	ping_count++;
	ping_start = time(NULL);
	slurm_mutex_unlock(&lock_mutex);
}

/*
 * ping_end - record that a ping cycle has ended. This can be called more
 *	than once (for REQUEST_PING and simultaneous REQUEST_NODE_REGISTRATION
 *	for selected nodes). Matching ping_end calls must be made for each
 *	before is_ping_done returns true.
 */
void ping_end (void)
{
	slurm_mutex_lock(&lock_mutex);
	ping_count--;

	if (ping_count == 0) /* no more running ping cycles */
		ping_start = 0;
	else if (ping_count < 0)
		fatal("ping_count < 0");

	slurm_mutex_unlock(&lock_mutex);
}

/*
 * ping_nodes - check that all nodes and daemons are alive,
 *	get nodes in UNKNOWN state to register
 */
void ping_nodes (void)
{
	static bool restart_flag = true;	/* system just restarted */
	static int offset = 0;	/* mutex via node table write lock on entry */
	static int max_reg_threads = 0;	/* max node registration threads
					 * this can include DOWN nodes, so
					 * limit the number to avoid huge
					 * communication delays */
	int i;
	time_t now = time(NULL), still_live_time, node_dead_time;
	static time_t last_ping_time = (time_t) 0;
	hostlist_t down_hostlist = NULL;
	char *host_str = NULL;
	agent_arg_t *ping_agent_args = NULL;
	agent_arg_t *reg_agent_args = NULL;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr = NULL;
#else
	struct node_record *node_ptr = NULL;
	time_t old_cpu_load_time = now - slurmctld_conf.slurmd_timeout;
	time_t old_free_mem_time = now - slurmctld_conf.slurmd_timeout;
#endif

	ping_agent_args = xmalloc (sizeof (agent_arg_t));
	ping_agent_args->msg_type = REQUEST_PING;
	ping_agent_args->retry = 0;
	ping_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	ping_agent_args->hostlist = hostlist_create(NULL);

	reg_agent_args = xmalloc (sizeof (agent_arg_t));
	reg_agent_args->msg_type = REQUEST_NODE_REGISTRATION_STATUS;
	reg_agent_args->retry = 0;
	reg_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	reg_agent_args->hostlist = hostlist_create(NULL);

	/*
	 * If there are a large number of down nodes, the node ping
	 * can take a long time to complete:
	 *  ping_time = down_nodes * agent_timeout / agent_parallelism
	 *  ping_time = down_nodes * 10_seconds / 10
	 *  ping_time = down_nodes (seconds)
	 * Because of this, we extend the SlurmdTimeout by the
	 * time needed to complete a ping of all nodes.
	 */
	if ((slurmctld_conf.slurmd_timeout == 0) ||
	    (last_ping_time == (time_t) 0)) {
		node_dead_time = (time_t) 0;
	} else {
		node_dead_time = last_ping_time -
				 slurmctld_conf.slurmd_timeout;
	}
	still_live_time = now - (slurmctld_conf.slurmd_timeout / 3);
	last_ping_time  = now;

	if (max_reg_threads == 0) {
		max_reg_threads = MAX(slurm_get_tree_width(), 1);
	}
	offset += max_reg_threads;
	if ((offset > node_record_count) &&
	    (offset >= (max_reg_threads * MAX_REG_FREQUENCY)))
		offset = 0;

#ifdef HAVE_FRONT_END
	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if ((slurmctld_conf.slurmd_timeout == 0)	&&
		    (!restart_flag)				&&
		    (!IS_NODE_UNKNOWN(front_end_ptr))		&&
		    (!IS_NODE_NO_RESPOND(front_end_ptr)))
			continue;

		if ((front_end_ptr->last_response != (time_t) 0)     &&
		    (front_end_ptr->last_response <= node_dead_time) &&
		    (!IS_NODE_DOWN(front_end_ptr))) {
			if (down_hostlist)
				(void) hostlist_push_host(down_hostlist,
					front_end_ptr->name);
			else {
				down_hostlist =
					hostlist_create(front_end_ptr->name);
				if (!down_hostlist) {
					fatal("invalid front_end list: %s",
					      front_end_ptr->name);
				}
			}
			set_front_end_down(front_end_ptr, "Not responding");
			front_end_ptr->not_responding = false;
			continue;
		}

		if (restart_flag) {
			front_end_ptr->last_response =
				slurmctld_conf.last_update;
		}

		/* Request a node registration if its state is UNKNOWN or
		 * on a periodic basis (about every MAX_REG_FREQUENCY ping,
		 * this mechanism avoids an additional (per node) timer or
		 * counter and gets updated configuration information
		 * once in a while). We limit these requests since they
		 * can generate a flood of incoming RPCs. */
		if (IS_NODE_UNKNOWN(front_end_ptr) || restart_flag ||
		    ((i >= offset) && (i < (offset + max_reg_threads)))) {
			if (reg_agent_args->protocol_version >
			    front_end_ptr->protocol_version)
				reg_agent_args->protocol_version =
					front_end_ptr->protocol_version;
			hostlist_push_host(reg_agent_args->hostlist,
				      front_end_ptr->name);
			reg_agent_args->node_count++;
			continue;
		}

		if ((!IS_NODE_NO_RESPOND(front_end_ptr)) &&
		    (front_end_ptr->last_response >= still_live_time))
			continue;

		/* The problems that exist on a normal system with
		 * hierarchical communication don't exist on a
		 * front-end system, so it is ok to ping none
		 * responding or down front-end nodes. */
		/* if (IS_NODE_NO_RESPOND(front_end_ptr) && */
		/*     IS_NODE_DOWN(front_end_ptr)) */
		/* 	continue; */

		if (ping_agent_args->protocol_version >
		    front_end_ptr->protocol_version)
			ping_agent_args->protocol_version =
				front_end_ptr->protocol_version;
		hostlist_push_host(ping_agent_args->hostlist,
				   front_end_ptr->name);
		ping_agent_args->node_count++;
	}
#else
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (IS_NODE_FUTURE(node_ptr) || IS_NODE_POWER_SAVE(node_ptr))
			continue;
		if ((slurmctld_conf.slurmd_timeout == 0) &&
		    (!restart_flag)			 &&
		    (!IS_NODE_UNKNOWN(node_ptr))         &&
		    (!IS_NODE_NO_RESPOND(node_ptr)))
			continue;

		if ((node_ptr->last_response != (time_t) 0)     &&
		    (node_ptr->last_response <= node_dead_time) &&
		    (!IS_NODE_DOWN(node_ptr))) {
			if (down_hostlist)
				(void) hostlist_push_host(down_hostlist,
					node_ptr->name);
			else {
				down_hostlist =
					hostlist_create(node_ptr->name);
				if (!down_hostlist) {
					fatal("Invalid host name: %s",
					      node_ptr->name);
				}
			}
			set_node_down_ptr(node_ptr, "Not responding");
			node_ptr->not_responding = false;  /* logged below */
			continue;
		}

		/* If we are resuming nodes from power save we need to
		   keep the larger last_response so we don't
		   accidentally mark them as "unexpectedly rebooted".
		*/
		if (restart_flag
		    && (node_ptr->last_response < slurmctld_conf.last_update))
			node_ptr->last_response = slurmctld_conf.last_update;

		/* Request a node registration if its state is UNKNOWN or
		 * on a periodic basis (about every MAX_REG_FREQUENCY ping,
		 * this mechanism avoids an additional (per node) timer or
		 * counter and gets updated configuration information
		 * once in a while). We limit these requests since they
		 * can generate a flood of incoming RPCs. */
		if (IS_NODE_UNKNOWN(node_ptr) || (node_ptr->boot_time == 0) ||
		    ((i >= offset) && (i < (offset + max_reg_threads)))) {
			if (reg_agent_args->protocol_version >
			    node_ptr->protocol_version)
				reg_agent_args->protocol_version =
					node_ptr->protocol_version;
			hostlist_push_host(reg_agent_args->hostlist,
					   node_ptr->name);
			reg_agent_args->node_count++;
			continue;
		}

		if ((!IS_NODE_NO_RESPOND(node_ptr)) &&
		    (node_ptr->last_response >= still_live_time) &&
		    (node_ptr->cpu_load_time >= old_cpu_load_time) &&
		    (node_ptr->free_mem_time >= old_free_mem_time))
			continue;

		/* Do not keep pinging down nodes since this can induce
		 * huge delays in hierarchical communication fail-over */
		if (IS_NODE_NO_RESPOND(node_ptr) && IS_NODE_DOWN(node_ptr))
			continue;

		if (ping_agent_args->protocol_version >
		    node_ptr->protocol_version)
			ping_agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(ping_agent_args->hostlist, node_ptr->name);
		ping_agent_args->node_count++;
	}
#endif

	restart_flag = false;
	if (ping_agent_args->node_count == 0) {
		hostlist_destroy(ping_agent_args->hostlist);
		xfree (ping_agent_args);
	} else {
		hostlist_uniq(ping_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				ping_agent_args->hostlist);
		debug("Spawning ping agent for %s", host_str);
		xfree(host_str);
		ping_begin();
		agent_queue_request(ping_agent_args);
	}

	if (reg_agent_args->node_count == 0) {
		hostlist_destroy(reg_agent_args->hostlist);
		xfree (reg_agent_args);
	} else {
		hostlist_uniq(reg_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				reg_agent_args->hostlist);
		debug("Spawning registration agent for %s %d hosts",
		      host_str, reg_agent_args->node_count);
		xfree(host_str);
		ping_begin();
		agent_queue_request(reg_agent_args);
	}

	if (down_hostlist) {
		hostlist_uniq(down_hostlist);
		host_str = hostlist_ranged_string_xmalloc(down_hostlist);
		error("Nodes %s not responding, setting DOWN", host_str);
		xfree(host_str);
		hostlist_destroy(down_hostlist);
	}
}

/* Spawn health check function for every node that is not DOWN */
extern void run_health_check(void)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	struct node_record *node_ptr;
	int node_test_cnt = 0, node_limit, node_states, run_cyclic;
	static int base_node_loc = -1;
	static time_t cycle_start_time = (time_t) 0;
#endif
	int i;
	char *host_str = NULL;
	agent_arg_t *check_agent_args = NULL;

	/* Sync plugin internal data with
	 * node select_nodeinfo. This is important
	 * after reconfig otherwise select_nodeinfo
	 * will not return the correct number of
	 * allocated cpus.
	 */
	select_g_select_nodeinfo_set_all();

#ifdef HAVE_FRONT_END
	check_agent_args = xmalloc (sizeof (agent_arg_t));
	check_agent_args->msg_type = REQUEST_HEALTH_CHECK;
	check_agent_args->retry = 0;
	check_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	check_agent_args->hostlist = hostlist_create(NULL);
	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if (IS_NODE_NO_RESPOND(front_end_ptr))
			continue;
		if (check_agent_args->protocol_version >
		    front_end_ptr->protocol_version)
			check_agent_args->protocol_version =
				front_end_ptr->protocol_version;
		hostlist_push_host(check_agent_args->hostlist,
				   front_end_ptr->name);
		check_agent_args->node_count++;
	}
#else
	node_limit = 0;
	run_cyclic = slurmctld_conf.health_check_node_state &
		     HEALTH_CHECK_CYCLE;
	node_states = slurmctld_conf.health_check_node_state &
		      (~HEALTH_CHECK_CYCLE);
	if (run_cyclic) {
		time_t now = time(NULL);
		if (cycle_start_time == (time_t) 0)
			cycle_start_time = now;
		else if (base_node_loc >= 0)
			;	/* mid-cycle */
		else if (difftime(now, cycle_start_time) <
			 slurmctld_conf.health_check_interval) {
			return;	/* Wait to start next cycle */
		}
		cycle_start_time = now;
		/* Determine how many nodes we want to test on each call of
		 * run_health_check() to spread out the work. */
		node_limit = (node_record_count * 2) /
			     slurmctld_conf.health_check_interval;
		node_limit = MAX(node_limit, 10);
	}
	if ((node_states != HEALTH_CHECK_NODE_ANY) &&
	    (node_states != HEALTH_CHECK_NODE_IDLE)) {
		/* Update each node's alloc_cpus count */
		select_g_select_nodeinfo_set_all();
	}

	check_agent_args = xmalloc (sizeof (agent_arg_t));
	check_agent_args->msg_type = REQUEST_HEALTH_CHECK;
	check_agent_args->retry = 0;
	check_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	check_agent_args->hostlist = hostlist_create(NULL);
	for (i = 0; i < node_record_count; i++) {
		if (run_cyclic) {
			if (node_test_cnt++ >= node_limit)
				break;
			base_node_loc++;
			if (base_node_loc >= node_record_count) {
				base_node_loc = -1;
				break;
			}
			node_ptr = node_record_table_ptr + base_node_loc;
		} else {
			node_ptr = node_record_table_ptr + i;
		}
		if (IS_NODE_NO_RESPOND(node_ptr) || IS_NODE_FUTURE(node_ptr) ||
		    IS_NODE_POWER_SAVE(node_ptr))
			continue;
		if (node_states != HEALTH_CHECK_NODE_ANY) {
			uint16_t cpus_total, cpus_used = 0;
			if (slurmctld_conf.fast_schedule) {
				cpus_total = node_ptr->config_ptr->cpus;
			} else {
				cpus_total = node_ptr->cpus;
			}
			if (!IS_NODE_IDLE(node_ptr)) {
				select_g_select_nodeinfo_get(
						node_ptr->select_nodeinfo,
						SELECT_NODEDATA_SUBCNT,
						NODE_STATE_ALLOCATED,
						&cpus_used);
			}
			/* Here the node state is inferred from
			 * the cpus allocated on it.
			 * - cpus_used == 0
			 *       means node is idle
			 * - cpus_used < cpus_total
			 *       means the node is in mixed state
			 * else cpus_used == cpus_total
			 *       means the node is allocated
			 */
			if (cpus_used == 0) {
				if (!(node_states & HEALTH_CHECK_NODE_IDLE))
					continue;
				if (!IS_NODE_IDLE(node_ptr))
					continue;
			} else if (cpus_used < cpus_total) {
				if (!(node_states & HEALTH_CHECK_NODE_MIXED))
					continue;
			} else {
				if (!(node_states & HEALTH_CHECK_NODE_ALLOC))
					continue;
			}
		}
		if (check_agent_args->protocol_version >
		    node_ptr->protocol_version)
			check_agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(check_agent_args->hostlist, node_ptr->name);
		check_agent_args->node_count++;
	}
	if (run_cyclic && (i >= node_record_count))
		base_node_loc = -1;
#endif

	if (check_agent_args->node_count == 0) {
		hostlist_destroy(check_agent_args->hostlist);
		xfree (check_agent_args);
	} else {
		hostlist_uniq(check_agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(
				check_agent_args->hostlist);
		debug("Spawning health check agent for %s", host_str);
		xfree(host_str);
		ping_begin();
		agent_queue_request(check_agent_args);
	}
}

/* Update acct_gather data for every node that is not DOWN */
extern void update_nodes_acct_gather_data(void)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	struct node_record *node_ptr;
#endif
	int i;
	char *host_str = NULL;
	agent_arg_t *agent_args = NULL;

	agent_args = xmalloc (sizeof (agent_arg_t));
	agent_args->msg_type = REQUEST_ACCT_GATHER_UPDATE;
	agent_args->retry = 0;
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	agent_args->hostlist = hostlist_create(NULL);

#ifdef HAVE_FRONT_END
	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if (IS_NODE_NO_RESPOND(front_end_ptr))
			continue;
		if (agent_args->protocol_version >
		    front_end_ptr->protocol_version)
			agent_args->protocol_version =
				front_end_ptr->protocol_version;

		hostlist_push_host(agent_args->hostlist, front_end_ptr->name);
		agent_args->node_count++;
	}
#else
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (IS_NODE_NO_RESPOND(node_ptr) || IS_NODE_FUTURE(node_ptr) ||
		    IS_NODE_POWER_SAVE(node_ptr))
			continue;
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}
#endif

	if (agent_args->node_count == 0) {
		hostlist_destroy(agent_args->hostlist);
		xfree (agent_args);
	} else {
		hostlist_uniq(agent_args->hostlist);
		host_str = hostlist_ranged_string_xmalloc(agent_args->hostlist);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_ENERGY)
			info("Updating acct_gather data for %s", host_str);
		xfree(host_str);
		ping_begin();
		agent_queue_request(agent_args);
	}
}
