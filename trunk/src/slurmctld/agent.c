/*****************************************************************************\
 *  agent.c - parallel background communication functions
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
 *  with. A special watchdog thread sends SIGLARM to any threads that have been 
 *  active (in DSH_ACTIVE state) for more than COMMAND_TIMEOUT seconds. 
 *  The agent responds to slurmctld via an RPC as required.
 *  For example, informing slurmctld that some node is not responding.
 *
 *  All the state for each thread is contained in thd_t struct, which is 
 *  passed to the agent upon startup and freed upon completion.
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

typedef struct task_info {
	pthread_mutex_t	*thread_mutex;		/* agent specific mutex */
	pthread_cond_t	*thread_cond;		/* agent specific condition */
	uint32_t	*threads_active;	/* count of currently active threads */
	thd_t 		*thread_struct;		/* thread structures */
	slurm_msg_type_t msg_type;		/* RPC to be issued */
	void		*msg_args;		/* RPC data to be used */
} task_info_t;

static void *thread_revoke_job_cred (void *args);
static void *wdog (void *args);

/*
 * agent - party responsible for performing some task in parallel across a set of nodes
 * input: pointer to agent_info_t, which is xfree'd upon completion if AGENT_IS_THREAD is set
 */
void *
agent (void *args)
{
	int i, rc;
	pthread_attr_t attr_wdog;
	pthread_t thread_wdog;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
	task_info_t *task_specific_ptr;

	/* basic argument value tests */
	if (agent_ptr->thread_count == 0)
		goto cleanup;
	if (thread_ptr == NULL)
		error ("agent_revoke_job_cred passed null thread_struct");
	if (agent_ptr->msg_type != REQUEST_REVOKE_JOB_CREDENTIAL)
		fatal ("agent_revoke_job_cred passed invaid message type %d", agent_ptr->msg_type);

	/* initialize the thread data structure */
	if (pthread_mutex_init (&agent_ptr->thread_mutex, NULL))
		fatal ("agent_revoke_job_cred passed invaid invalid thread_mutex address");
	if (pthread_cond_init (&agent_ptr->thread_cond, NULL))
		fatal ("agent_revoke_job_cred passed invaid invalid thread_cond address");
	agent_ptr->threads_active = 0;
	for (i = 0; i < agent_ptr->thread_count; i++) {
		thread_ptr[i].state = DSH_NEW;
	}

	/* start the watchdog thread */
	if (pthread_attr_init (&attr_wdog))
		error ("pthread_attr_init errno %d", errno);
	if (pthread_attr_setdetachstate (&attr_wdog, PTHREAD_CREATE_DETACHED))
		error ("pthread_attr_setdetachstate errno %d", errno);
#ifdef PTHREAD_SCOPE_SYSTEM
	pthread_attr_setscope (&attr_wdog, PTHREAD_SCOPE_SYSTEM);
#endif
	if (pthread_create (&thread_wdog, &attr_wdog, wdog, args)) {
		error ("pthread_create errno %d", errno);
		sleep (1); /* sleep and try once more */
		if (pthread_create (&thread_wdog, &attr_wdog, wdog, args))
			fatal ("pthread_create errno %d", errno);
	}

	/* start all the other threads (at most AGENT_THREAD_COUNT active at once) */
	for (i = 0; i < agent_ptr->thread_count; i++) {
		
		/* wait until "room" for another thread */	
		pthread_mutex_lock (&agent_ptr->thread_mutex);
     		if (AGENT_THREAD_COUNT == agent_ptr->threads_active)
			pthread_cond_wait (&agent_ptr->thread_cond, &agent_ptr->thread_mutex);
 
		/* create thread */
		task_specific_ptr 			= malloc (sizeof (task_info_t));
		task_specific_ptr->thread_mutex		= &agent_ptr->thread_mutex;
		task_specific_ptr->thread_cond		= &agent_ptr->thread_cond;
		task_specific_ptr->threads_active	= &agent_ptr->thread_count;
		task_specific_ptr->thread_struct	= &thread_ptr[i];
		task_specific_ptr->msg_type		= agent_ptr->msg_type;
		task_specific_ptr->msg_args		= &agent_ptr->msg_args;

		pthread_attr_init (&thread_ptr[i].attr);
		pthread_attr_setdetachstate (&thread_ptr[i].attr, PTHREAD_CREATE_JOINABLE);
#ifdef PTHREAD_SCOPE_SYSTEM
		pthread_attr_setscope (&thread_ptr[i].attr, PTHREAD_SCOPE_SYSTEM);
#endif
		if (agent_ptr->msg_type != REQUEST_REVOKE_JOB_CREDENTIAL) {
			rc = pthread_create (&thread_ptr[i].thread, &thread_ptr[i].attr, 
				thread_revoke_job_cred, (void *) task_specific_ptr);

			agent_ptr->threads_active++;
			pthread_mutex_unlock (&agent_ptr->thread_mutex);

			if (rc) {
				error ("pthread_create errno %d\n", errno);
				/* execute task within this thread */
				thread_revoke_job_cred ((void *) task_specific_ptr);
			}
		}
        }

	/* wait for termination of remaining threads */
	pthread_mutex_lock(&agent_ptr->thread_mutex);
     	while (agent_ptr->threads_active > 0)
		pthread_cond_wait(&agent_ptr->thread_cond, &agent_ptr->thread_mutex);
	pthread_join (thread_wdog, NULL);
	return NULL;

cleanup:
#if AGENT_IS_THREAD
	if (thread_ptr)
		xfree (thread_ptr);
	if (agent_ptr->msg_args)
		xfree (agent_ptr->msg_args);
	xfree (agent_ptr);
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

	while (1) {
		work_done = 1;	/* assume all threads complete for now */
		fail_cnt = 0;	/* assume all threads complete sucessfully for now */
		sleep (WDOG_POLL);
		min_start = time(NULL) - COMMAND_TIMEOUT;

		pthread_mutex_lock (&agent_ptr->thread_mutex);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			switch (thread_ptr[i].state) {
				case DSH_ACTIVE:
					work_done = 0;
					if (thread_ptr[i].start < min_start)
						pthread_kill(thread_ptr[i].thread, SIGALRM);
					break;
				case DSH_NEW:
					work_done = 0;
					break;
				case DSH_DONE:
					break;
				case DSH_FAILED:
					fail_cnt++;
					break;
			}
		}
		pthread_mutex_unlock (&agent_ptr->thread_mutex);
		if (work_done)
			break;
	}

	/* Notify slurmctld of non-responding nodes */
	if (fail_cnt) {
		char *node_list_ptr;

		for (i = 0; i < agent_ptr->thread_count; i++) {
			if (thread_ptr[i].state == DSH_FAILED)
				xstrcat (node_list_ptr, thread_ptr[i].node_name);
		}

		/* send RPC */
		/* the following nodes are not responding... */

		xfree (node_list_ptr);
	}

	pthread_exit (NULL);
}

/* thread_revoke_job_cred - thread to revoke a credential on a collection of nodes */
static void *
thread_revoke_job_cred (void *args)
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
	thread_ptr->start = time(NULL);
	pthread_mutex_unlock (task_ptr->thread_mutex);

	/* accept SIGALRM */
	if (sigemptyset (&set))
		error ("sigemptyset errno %d", errno);
	if (sigaddset (&set, SIGALRM))
		error ("sigaddset errno %d on SIGALRM", errno);

	/* init message connection for message communication with slurmd */
	if ( ( sockfd = slurm_open_msg_conn (& thread_ptr->slurm_addr) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_open_msg_conn error for %s", 
			thread_ptr->node_name);
		goto cleanup;
	}

	/* send request message */
	request_msg . msg_type = task_ptr->msg_type ;
	request_msg . data = task_ptr->msg_args ; 
	if ( ( rc = slurm_send_node_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_send_node_msg error for %s",
			thread_ptr->node_name);
		goto cleanup;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_receive_msg error for %s", 
			thread_ptr->node_name);
		goto cleanup;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_shutdown_msg_conn error for %s", 
			thread_ptr->node_name);
		goto cleanup;
	}
	if ( msg_size ) {
		error ("thread_revoke_job_cred/msg_size error %d for %s", 
			msg_size, thread_ptr->node_name);
		goto cleanup;
	}

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc)
				error ("thread_revoke_job_cred/rc error %d for %s", 
					rc, thread_ptr->node_name);
			else
				thread_state = DSH_DONE;

			break ;
		default:
			error ("thread_revoke_job_cred/msg_type error %d for %s",
				response_msg.msg_type, thread_ptr->node_name);
			break ;
	}

cleanup:
	pthread_mutex_lock (task_ptr->thread_mutex);
	thread_ptr->state = thread_state;
	thread_ptr->finish = time(NULL);

	/* Signal completion so another thread can replace us */
	(*task_ptr->threads_active)--;
	pthread_cond_signal(task_ptr->thread_cond);
	pthread_mutex_unlock (task_ptr->thread_mutex);

	pthread_exit ((void *)NULL);
}
