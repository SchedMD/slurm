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
	int i, pos, age, retries = 0;
	time_t now;
	uint16_t base_state;

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
	now = time (NULL);

	offset += MAX_REG_THREADS;
	if ((offset > node_record_count) && 
	    (offset >= (MAX_REG_THREADS * MAX_REG_FREQUENCY)))
		offset = 0;

	for (i = 0; i < node_record_count; i++) {
		base_state = node_record_table_ptr[i].node_state & 
				(~NODE_STATE_NO_RESPOND);
		age = difftime (now, node_record_table_ptr[i].last_response);
		if (age < slurmctld_conf.heartbeat_interval)
			continue;

		if ((node_record_table_ptr[i].last_response != (time_t)0) &&
		    (slurmctld_conf.slurmd_timeout != 0) &&
		    (age >= slurmctld_conf.slurmd_timeout) &&
		    ((base_state != NODE_STATE_DOWN)     &&
		     (base_state != NODE_STATE_DRAINING) &&
		     (base_state != NODE_STATE_DRAINED))) {
			error ("Node %s not responding, setting DOWN", 
			       node_record_table_ptr[i].name);
			set_node_down(node_record_table_ptr[i].name, 
					"Not responding");
			continue;
		}

		if (node_record_table_ptr[i].last_response == (time_t)0)
			node_record_table_ptr[i].last_response = 
						slurmctld_conf.last_update;

		/* Request a node registration if its state is UNKNOWN or 
		 * on a periodic basis (about every MAX_REG_FREQUENCY ping, 
		 * this mechanism avoids an additional (per node) timer or 
		 * counter and gets updated configuration information 
		 * once in a while). We limit these requests since they 
		 * can generate a flood of incomming RPCs. */
		if ((base_state == NODE_STATE_UNKNOWN) || 
		    ((i >= offset) && (i < (offset + MAX_REG_THREADS)))) {
			debug3 ("attempt to register %s now", 
			        node_record_table_ptr[i].name);
			if ((reg_agent_args->node_count+1) > 
						reg_buf_rec_size) {
				reg_buf_rec_size += 32;
				xrealloc ((reg_agent_args->slurm_addr), 
				          (sizeof (struct sockaddr_in) * 
					  reg_buf_rec_size));
				xrealloc ((reg_agent_args->node_names), 
				          (MAX_NAME_LEN * reg_buf_rec_size));
			}
			reg_agent_args->slurm_addr[
					reg_agent_args->node_count] = 
					node_record_table_ptr[i].slurm_addr;
			pos = MAX_NAME_LEN * reg_agent_args->node_count;
			strncpy (&reg_agent_args->node_names[pos],
			         node_record_table_ptr[i].name, MAX_NAME_LEN);
			reg_agent_args->node_count++;
			continue;
		}

		debug3 ("ping %s now", node_record_table_ptr[i].name);

		if ((ping_agent_args->node_count+1) > ping_buf_rec_size) {
			ping_buf_rec_size += 32;
			xrealloc ((ping_agent_args->slurm_addr), 
			          (sizeof (struct sockaddr_in) * 
				  ping_buf_rec_size));
			xrealloc ((ping_agent_args->node_names), 
			          (MAX_NAME_LEN * ping_buf_rec_size));
		}
		ping_agent_args->slurm_addr[ping_agent_args->node_count] = 
					node_record_table_ptr[i].slurm_addr;
		pos = MAX_NAME_LEN * ping_agent_args->node_count;
		strncpy (&ping_agent_args->node_names[pos],
		         node_record_table_ptr[i].name, MAX_NAME_LEN);
		ping_agent_args->node_count++;

	}

	if (ping_agent_args->node_count == 0)
		xfree (ping_agent_args);
	else {
		debug2 ("Spawning ping agent");
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
		debug2 ("Spawning node registration agent");
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
}
