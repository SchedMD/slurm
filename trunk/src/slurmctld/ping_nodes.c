/*****************************************************************************\
 *  ping_nodes.c - ping the slurmd daemons to test if they respond
 *	Note: there is a global node table (node_record_table_ptr)
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <time.h>
#include <string.h>

#include "src/common/hostlist.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/slurmctld.h"

/* Attempt to fork a thread at most MAX_RETRIES times before aborting */
#define MAX_RETRIES 10

/* Request that nodes re-register at most every MAX_REG_FREQUENCY pings */
#define MAX_REG_FREQUENCY 20

/* Spawn no more than MAX_REG_THREADS for node re-registration */
#define MAX_REG_THREADS (MAX_SERVER_THREADS - 2)

static pthread_mutex_t lock_mutex = PTHREAD_MUTEX_INITIALIZER;
static int ping_count = 0;

/*
 * is_ping_done - test if the last node ping cycle has completed.
 *	Use this to avoid starting a new set of ping requests before the 
 *	previous one completes
 * RET true if ping process is done, false otherwise
 */
bool is_ping_done (void)
{
	bool is_done = true;

	slurm_mutex_lock(&lock_mutex);
	if (ping_count)
		is_done = false;
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
	if (ping_count > 0)
		ping_count--;
	else
		fatal ("ping_count < 0");
	slurm_mutex_unlock(&lock_mutex);
}

/*
 * ping_nodes - check that all nodes and daemons are alive,  
 *	get nodes in UNKNOWN state to register
 */
void ping_nodes (void)
{
	static int offset = 0;	/* mutex via node table write lock on entry */
	int i, pos, retries = 0;
	time_t now, still_live_time, node_dead_time;
	static time_t last_ping_time = (time_t) 0;
	uint16_t base_state, no_resp_flag;
	hostlist_t ping_hostlist = hostlist_create("");
	hostlist_t reg_hostlist  = hostlist_create("");
	char host_str[64];

	int ping_buf_rec_size = 0;
	agent_arg_t *ping_agent_args;
	pthread_attr_t ping_attr_agent;
	pthread_t ping_thread_agent;

	int reg_buf_rec_size = 0;
	agent_arg_t *reg_agent_args;
	pthread_attr_t reg_attr_agent;
	pthread_t reg_thread_agent;

	ping_agent_args = xmalloc (sizeof (agent_arg_t));
	ping_agent_args->msg_type = REQUEST_PING;
	ping_agent_args->retry = 0;
	reg_agent_args = xmalloc (sizeof (agent_arg_t));
	reg_agent_args->msg_type = REQUEST_NODE_REGISTRATION_STATUS;
	reg_agent_args->retry = 0;

	/*
	 * If there are a large number of down nodes, the node ping
	 * can take a long time to complete: 
	 *  ping_time = down_nodes * agent_timeout / agent_parallelism
	 *  ping_time = down_nodes * 10_seconds / 10
	 *  ping_time = down_nodes (seconds)
	 * Because of this, we extend the SlurmdTimeout by the 
	 * time needed to complete a ping of all nodes.
	 */
	now = time (NULL);
	if ( (slurmctld_conf.slurmd_timeout == 0) || 
	     (last_ping_time == (time_t) 0) )
		node_dead_time = (time_t) 0;
	else
		node_dead_time = last_ping_time - slurmctld_conf.slurmd_timeout;
	still_live_time = now - slurmctld_conf.heartbeat_interval;
	last_ping_time  = now;

	offset += MAX_REG_THREADS;
	if ((offset > node_record_count) && 
	    (offset >= (MAX_REG_THREADS * MAX_REG_FREQUENCY)))
		offset = 0;

	for (i = 0; i < node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];

		if (node_ptr->last_response >= still_live_time)
			continue;

		base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
		no_resp_flag = node_ptr->node_state &   NODE_STATE_NO_RESPOND;
		if ((node_ptr->last_response != (time_t)0) &&
		    (node_ptr->last_response <= node_dead_time) &&
		    ((base_state != NODE_STATE_DOWN) &&
		     (base_state != NODE_STATE_COMPLETING) &&
		     (base_state != NODE_STATE_DRAINED))) {
			error ("Node %s not responding, setting DOWN", 
			       node_ptr->name);
			set_node_down(node_ptr->name, "Not responding");
			continue;
		}

		if (node_ptr->last_response == (time_t)0)
			node_ptr->last_response = slurmctld_conf.last_update;

		/* Request a node registration if its state is UNKNOWN or 
		 * on a periodic basis (about every MAX_REG_FREQUENCY ping, 
		 * this mechanism avoids an additional (per node) timer or 
		 * counter and gets updated configuration information 
		 * once in a while). We limit these requests since they 
		 * can generate a flood of incomming RPCs. */
		if ((base_state == NODE_STATE_UNKNOWN) || no_resp_flag ||
		    ((i >= offset) && (i < (offset + MAX_REG_THREADS)))) {
			(void) hostlist_push_host(reg_hostlist, node_ptr->name);
			if ((reg_agent_args->node_count+1) > 
						reg_buf_rec_size) {
				reg_buf_rec_size += 32;
				xrealloc ((reg_agent_args->slurm_addr), 
				          (sizeof (struct sockaddr_in) * 
					  reg_buf_rec_size));
				xrealloc ((reg_agent_args->node_names), 
				          (MAX_NAME_LEN * reg_buf_rec_size));
			}
			reg_agent_args->slurm_addr[reg_agent_args->node_count] = 
					node_ptr->slurm_addr;
			pos = MAX_NAME_LEN * reg_agent_args->node_count;
			strncpy (&reg_agent_args->node_names[pos],
			         node_ptr->name, MAX_NAME_LEN);
			reg_agent_args->node_count++;
			continue;
		}

		(void) hostlist_push_host(ping_hostlist, node_ptr->name);
		if ((ping_agent_args->node_count+1) > ping_buf_rec_size) {
			ping_buf_rec_size += 32;
			xrealloc ((ping_agent_args->slurm_addr), 
			          (sizeof (struct sockaddr_in) * 
				  ping_buf_rec_size));
			xrealloc ((ping_agent_args->node_names), 
			          (MAX_NAME_LEN * ping_buf_rec_size));
		}
		ping_agent_args->slurm_addr[ping_agent_args->node_count] = 
					node_ptr->slurm_addr;
		pos = MAX_NAME_LEN * ping_agent_args->node_count;
		strncpy (&ping_agent_args->node_names[pos],
		         node_ptr->name, MAX_NAME_LEN);
		ping_agent_args->node_count++;

	}

	if (ping_agent_args->node_count == 0)
		xfree (ping_agent_args);
	else {
		hostlist_uniq(ping_hostlist);
		hostlist_ranged_string(ping_hostlist, 
			sizeof(host_str), host_str);
		debug2 ("Spawning ping agent for %s", host_str);
		ping_begin();
		if (pthread_attr_init (&ping_attr_agent))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&ping_attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&ping_attr_agent, 
						PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		while (pthread_create (&ping_thread_agent, &ping_attr_agent, 
					agent, (void *)ping_agent_args)) {
			error ("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep (1); /* sleep and try again */
		}
	}

	if (reg_agent_args->node_count == 0)
		xfree (reg_agent_args);
	else {
		hostlist_uniq(reg_hostlist);
		hostlist_ranged_string(reg_hostlist, 
			sizeof(host_str), host_str);
		debug2 ("Spawning registration agent for %s", host_str);
		ping_begin();
		if (pthread_attr_init (&reg_attr_agent))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&reg_attr_agent, 
						 PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&reg_attr_agent, 
					   PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		while (pthread_create (&reg_thread_agent, &reg_attr_agent, 
					agent, (void *)reg_agent_args)) {
			error ("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep (1); /* sleep and try again */
		}
	}

	hostlist_destroy(ping_hostlist);
	hostlist_destroy(reg_hostlist);
}
