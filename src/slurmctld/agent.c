/*****************************************************************************\
 *  agent.c - parallel background communication functions. This is where  
 *	logic could be placed for broadcast communications.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, et. al.
 *  Derived from pdsh written by Jim Garlick <garlick1@llnl.gov>
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
 *****************************************************************************
 *  Theory of operation:
 *
 *  The functions below permit slurmctld to initiate parallel tasks as a 
 *  detached thread and let the functions below make sure the work happens. 
 *  For example, when a job step completes slurmctld needs to revoke credentials 
 *  for that job step on every node it was allocated to. We don't want to 
 *  hang slurmctld's primary functions to perform this work, so it just 
 *  initiates an agent to perform the work. The agent is passed all details 
 *  required to perform the work, so it will be possible to execute the 
 *  agent as an pthread, process, or even a daemon on some other computer.
 *
 *  The main thread creates a separate thread for each node to be communicated 
 *  with up to AGENT_THREAD_COUNT. A special watchdog thread sends SIGLARM to 
 *  any threads that have been active (in DSH_ACTIVE state) for more than 
 *  COMMAND_TIMEOUT seconds. 
 *  The agent responds to slurmctld via an RPC as required.
 *  For example, informing slurmctld that some node is not responding.
 *
 *  All the state for each thread is maintailed in thd_t struct, which is 
 *  used by the watchdog thread as well as the communication threads.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <src/common/log.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/slurmctld/agent.h>

#if COMMAND_TIMEOUT == 1
#define WDOG_POLL 		1	/* secs */
#else
#define WDOG_POLL 		2	/* secs */
#endif

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
        time_t 		time;   		/* time stamp for start or delta time */
	struct sockaddr_in slurm_addr;		/* network address */
} thd_t;

typedef struct agent_info {
	pthread_mutex_t	thread_mutex;		/* agent specific mutex */
	pthread_cond_t	thread_cond;		/* agent specific condition */
	uint32_t	thread_count;		/* number of threads records */
	uint32_t	threads_active;		/* count of currently active threads */
	thd_t 		*thread_struct;		/* thread structures */
	slurm_msg_type_t msg_type;		/* RPC to be issued */
	void		*msg_args;		/* RPC data to be used */
} agent_info_t;

typedef struct task_info {
	pthread_mutex_t	*thread_mutex;		/* agent specific mutex */
	pthread_cond_t	*thread_cond;		/* agent specific condition */
	uint32_t	*threads_active;	/* count of currently active threads */
	thd_t 		*thread_struct;		/* thread structures */
	slurm_msg_type_t msg_type;		/* RPC to be issued */
	void		*msg_args;		/* RPC data to be used */
} task_info_t;

static void *thread_per_node_rpc (void *args);
static void *wdog (void *args);

/*
 * agent - party responsible for transmitting an common RPC in parallel across a set of nodes
 * input: pointer to agent_arg_t, which is xfree'd upon completion if AGENT_IS_THREAD is set
 */
void *
agent (void *args)
{
	int i, rc;
	pthread_attr_t attr_wdog;
	pthread_t thread_wdog;
	agent_arg_t *agent_arg_ptr;
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr;
	task_info_t *task_specific_ptr;

	/* basic argument value tests */
	if (agent_arg_ptr->addr_count == 0)
		goto cleanup;
	if (agent_arg_ptr->slurm_addr == NULL)
		error ("agent passed null address list");
	if (agent_arg_ptr->msg_type != REQUEST_REVOKE_JOB_CREDENTIAL)
		fatal ("agent passed invaid message type %d", agent_arg_ptr->msg_type);

	/* initialize the data structures */
	agent_info_ptr = xmalloc (sizeof (agent_info_t));
	thread_ptr = xmalloc (agent_arg_ptr->addr_count * sizeof (thd_t));
	if (pthread_mutex_init (&agent_info_ptr->thread_mutex, NULL))
		fatal ("agent: pthread_mutex_init error %m");
	if (pthread_cond_init (&agent_info_ptr->thread_cond, NULL))
		fatal ("agent: pthread_cond_init error %m");
	agent_info_ptr->thread_count = agent_arg_ptr->addr_count;
	agent_info_ptr->threads_active = 0;
	agent_info_ptr->thread_struct = thread_ptr;
	agent_info_ptr->msg_type = agent_arg_ptr->msg_type;
	agent_info_ptr->msg_args = agent_arg_ptr->msg_args;
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		thread_ptr[i].state = DSH_NEW;
	}

	/* start the watchdog thread */
	if (pthread_attr_init (&attr_wdog))
		fatal ("agent: pthread_attr_init error %m");
	if (pthread_attr_setdetachstate (&attr_wdog, PTHREAD_CREATE_DETACHED))
		error ("agent: pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	if (pthread_attr_setscope (&attr_wdog, PTHREAD_SCOPE_SYSTEM))
		error ("agent: pthread_attr_setscope error %m");
#endif
	if (pthread_create (&thread_wdog, &attr_wdog, wdog, (void *)agent_info_ptr)) {
		error ("agent: pthread_create error %m");
		sleep (1); /* sleep and try once more */
		if (pthread_create (&thread_wdog, &attr_wdog, wdog, args))
			fatal ("agent: pthread_create error %m");
	}

	/* start all the other threads (at most AGENT_THREAD_COUNT active at once) */
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		
		/* wait until "room" for another thread */	
		pthread_mutex_lock (&agent_info_ptr->thread_mutex);
     		if (AGENT_THREAD_COUNT == agent_info_ptr->threads_active)
			pthread_cond_wait (&agent_info_ptr->thread_cond, &agent_info_ptr->thread_mutex);
 
		/* create thread */
		task_specific_ptr 			= xmalloc (sizeof (task_info_t));
		task_specific_ptr->thread_mutex		= &agent_info_ptr->thread_mutex;
		task_specific_ptr->thread_cond		= &agent_info_ptr->thread_cond;
		task_specific_ptr->threads_active	= &agent_info_ptr->thread_count;
		task_specific_ptr->thread_struct	= &thread_ptr[i];
		task_specific_ptr->msg_type		= agent_info_ptr->msg_type;
		task_specific_ptr->msg_args		= &agent_info_ptr->msg_args;

		pthread_attr_init (&thread_ptr[i].attr);
		pthread_attr_setdetachstate (&thread_ptr[i].attr, PTHREAD_CREATE_JOINABLE);
#ifdef PTHREAD_SCOPE_SYSTEM
		pthread_attr_setscope (&thread_ptr[i].attr, PTHREAD_SCOPE_SYSTEM);
#endif
		if (agent_info_ptr->msg_type != REQUEST_REVOKE_JOB_CREDENTIAL) {
			rc = pthread_create (&thread_ptr[i].thread, &thread_ptr[i].attr, 
				thread_per_node_rpc, (void *) task_specific_ptr);

			agent_info_ptr->threads_active++;
			pthread_mutex_unlock (&agent_info_ptr->thread_mutex);

			if (rc) {
				error ("pthread_create error %m");
				/* execute task within this thread */
				thread_per_node_rpc ((void *) task_specific_ptr);
			}
		}
        }

	/* wait for termination of remaining threads */
	pthread_mutex_lock(&agent_info_ptr->thread_mutex);
     	while (agent_info_ptr->threads_active > 0)
		pthread_cond_wait(&agent_info_ptr->thread_cond, &agent_info_ptr->thread_mutex);
	pthread_join (thread_wdog, NULL);
	return NULL;

cleanup:
#if AGENT_IS_THREAD
	if (agent_arg_ptr->slurm_addr)
		xfree (agent_arg_ptr->slurm_addr);
	if (agent_arg_ptr->msg_args)
		xfree (agent_arg_ptr->msg_args);
	xfree (agent_arg_ptr);
#endif
	return NULL;
}

/* 
 * wdog - Watchdog thread. Send SIGALRM to threads which have been active for too long.
 *	Sleep for WDOG_POLL seconds between polls.
 */
static void *
wdog (void *args)
{
	int i, fail_cnt, work_done;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
	time_t min_start;
	time_t max_time;

	while (1) {
		work_done = 1;	/* assume all threads complete for now */
		fail_cnt = 0;	/* assume all threads complete sucessfully for now */
		max_time = 0;
		sleep (WDOG_POLL);
		min_start = time(NULL) - COMMAND_TIMEOUT;

		pthread_mutex_lock (&agent_ptr->thread_mutex);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			switch (thread_ptr[i].state) {
				case DSH_ACTIVE:
					work_done = 0;
					if (thread_ptr[i].time < min_start)
						pthread_kill(thread_ptr[i].thread, SIGALRM);
					break;
				case DSH_NEW:
					work_done = 0;
					break;
				case DSH_DONE:
					if (max_time < thread_ptr[i].time)
						max_time = thread_ptr[i].time;
					break;
				case DSH_FAILED:
					fail_cnt++;
					break;
			}
		}
		pthread_mutex_unlock (&agent_ptr->thread_mutex);
		if (work_done) {
			info ("max time used %ld msec", (long) max_time);
			break;
		}
	}

	/* Notify slurmctld of non-responding nodes */
	if (fail_cnt) {
		struct sockaddr_in *slurm_addr;

		slurm_addr = xmalloc (fail_cnt * sizeof (struct sockaddr_in));
		fail_cnt = 0;
		for (i = 0; i < agent_ptr->thread_count; i++) {
			if (thread_ptr[i].state != DSH_FAILED)
				continue;
			/* build a list of slurm_addr's */
			slurm_addr[fail_cnt++] = thread_ptr[i].slurm_addr;

		}

		info ("agent/wdog: %d nodes failed to respond", fail_cnt);
		/* send RPC */

		xfree (slurm_addr);
	}

	return (void *) NULL;
}

/* thread_per_node_rpc - thread to revoke a credential on a collection of nodes */
static void *
thread_per_node_rpc (void *args)
{
	sigset_t set;
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;
	task_info_t *task_ptr = (task_info_t *) args;
	thd_t *thread_ptr = task_ptr->thread_struct;
	state_t thread_state  = DSH_FAILED;

	pthread_mutex_lock (task_ptr->thread_mutex);
	thread_ptr->state = DSH_ACTIVE;
	thread_ptr->time = time (NULL);
	pthread_mutex_unlock (task_ptr->thread_mutex);

	/* accept SIGALRM */
	sigemptyset (&set);
	sigaddset (&set, SIGALRM);

	/* init message connection for message communication with slurmd */
	if ( ( sockfd = slurm_open_msg_conn (& thread_ptr->slurm_addr) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_per_node_rpc/slurm_open_msg_conn error %m");
		goto cleanup;
	}

	/* send request message */
	request_msg . msg_type = task_ptr->msg_type ;
	request_msg . data = task_ptr->msg_args ; 
	if ( ( rc = slurm_send_node_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_per_node_rpc/slurm_send_node_msg error %m");
		goto cleanup;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_per_node_rpc/slurm_receive_msg error %m");
		goto cleanup;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_per_node_rpc/slurm_shutdown_msg_conn error %m");
		goto cleanup;
	}
	if ( msg_size ) {
		error ("thread_per_node_rpc/msg_size error %d", msg_size);
		goto cleanup;
	}

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc)
				error ("thread_per_node_rpc/rc error %d", rc);
			else
				thread_state = DSH_DONE;

			break ;
		default:
			error ("thread_per_node_rpc bad msg_type %d",response_msg.msg_type);
			break ;
	}

cleanup:
	pthread_mutex_lock (task_ptr->thread_mutex);
	thread_ptr->state = thread_state;
	thread_ptr->time = time(NULL) - thread_ptr->time;

	/* Signal completion so another thread can replace us */
	(*task_ptr->threads_active)--;
	pthread_cond_signal(task_ptr->thread_cond);
	pthread_mutex_unlock (task_ptr->thread_mutex);

	return (void *) NULL;
}

