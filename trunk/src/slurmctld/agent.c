/*****************************************************************************\
 *  agent.c - parallel background communication functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, et. al.
 *  Derived from dsh written by Jim Garlick <garlick1@llnl.gov>
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
 *  initiates an agent to perform the work. 
 *
 *  The main thread creates a separate thread for each node to be communicated 
 *  with. A special watchdog thread sends SIGLARM to any threads that have been 
 *  active (in DSH_ACTIVE state) for more than COMMAND_TIMEOUT seconds.  
 *
 *  All the state for a thread is contained in thd_t struct.  An array of
 *  these structures is declared globally so signal handlers can access.
 *  The array is initialized by dsh() below, and the rsh() function for each
 *  thread is passed the element corresponding to one connection.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <src/slurmctld/slurmctld.h>

#define AGENT_THREAD_COUNT	10
#define COMMAND_TIMEOUT 	10	/* secs */
#define INTR_TIME		1 	/* secs */
#define WDOG_POLL 		2	/* secs */

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
        time_t 		start;   		/* time stamp for start */
        time_t 		finish;  		/* time stamp for finish */
	struct sockaddr_in slurm_addr;		/* network address */
	char node_name[MAX_NAME_LEN];		/* name of the node */
} thd_t;

/*
 * Mutex and condition variable for implementing `fanout'.  When a thread
 * terminates, it decrements threadcount and signals threadcount_cond.
 * The main, once it has spawned the fanout number of threads, suspends itself
 * until a thread termintates.
 */
static pthread_mutex_t 	threadcount_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	threadcount_cond  = PTHREAD_COND_INITIALIZER;
static int       	threadcount = 0;

void *thread_revoke_job_cred (void *arg);
void *wdog (void *args);

/* 
 * Watchdog thread.  Send SIGALRM to 
 *   - threads which have been active for too long
 * Sleep for WDOG_POLL seconds between polls (actually sleep for COMMAND_TIMEOUT
 * on the first iteration).
 */
static void *
wdog (void *args)
{
	int i;

	for (;;) {
	    for (i = 0; t[i].host != NULL; i++) {
		switch (t[i].state) {
		    case DSH_ACTIVE:
			if (t[i].start + COMMAND_TIMEOUT < time(NULL))
			    pthread_kill(t[i].thread, SIGALRM);
		    	break;
		    case DSH_NEW:
		    case DSH_DONE:
			break;
		}
	    }
	    sleep(i == 0 ? COMMAND_TIMEOUT : WDOG_POLL);
	}
	return NULL;
}

/* agent_revoke_job_cred - thread responsible for revoking all credentials on all 
 *	nodes for a particular job */
void *
agent_revoke_job_cred (void *arg)
{
	int i, rc;
	pthread_attr_t attr_wdog;
	pthread_t thread_wdog;
	revoke_credential_msg_t revoke_job_cred;

	node_count = ?
	revoke_job_cred = ?

	/* start the watchdog thread */
	if (pthread_attr_init (&attr_wdog))
		error ("pthread_attr_init errno %d", errno);
	if (pthread_attr_setdetachstate (&attr_wdog, PTHREAD_CREATE_DETACHED))
		error ("pthread_attr_setdetachstate errno %d", errno);
	if (pthread_create (&thread_wdog, &attr_wdog, wdog, (void *)t) {
		error ("pthread_create errno %d", errno);
		pthread_exit (errno);
	}

	/* start all the other threads (at most AGENT_THREAD_COUNT active at once) */
	for (i = 0; i < node_count; i++) {
		
		/* wait until "room" for another thread */	
		pthread_mutex_lock (&threadcount_mutex);

     		if (AGENT_THREAD_COUNT == threadcount)
			pthread_cond_wait (&threadcount_cond, &threadcount_mutex);
 
		/* create thread */
		pthread_attr_init (&t[i].attr);
		pthread_attr_setdetachstate (&t[i].attr, PTHREAD_CREATE_DETACHED);
#ifdef PTHREAD_SCOPE_SYSTEM
		/* we want 1:1 threads if there is a choice */
		pthread_attr_setscope (&t[i].attr, PTHREAD_SCOPE_SYSTEM);
#endif
		rc = pthread_create (&t[i].thread, &t[i].attr, thread_revoke_job_cred, (void *)&t[i]));
		threadcount++;
		pthread_mutex_unlock(&threadcount_mutex);

		if (rc) {
			error ("pthread_create errno %d\n", errno);
			thread_revoke_job_cred ((void *)&t[i]));
		}
        }

	/* wait for termination of remaining threads */
	pthread_mutex_lock(&threadcount_mutex);
     	while (threadcount > 0)
		pthread_cond_wait(&threadcount_cond, &threadcount_mutex);

}

/* thread_revoke_job_cred - thread to revoke a credential on every node upon a job's completion */
void *
thread_revoke_job_cred (void *arg)
{
	sigset_t set;
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;
	thd_t *thd_ptr = (thd_t *)arg;

	thd_ptr->state = DSH_ACTIVE;
	thd_ptr->start = time(NULL);

	/* accept SIGALRM */
	if (sigemptyset (&set))
		error ("sigemptyset errno %d", errno);
	if (sigaddset (&set, SIGALRM))
		error ("sigaddset errno %d on SIGALRM", errno);

	/* init message connection for message communication with slurmd */
	if ( ( sockfd = slurm_open_msg_conn (& thd_ptr -> slurm_addr) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_open_msg_conn error for %s", thd_ptr->node_name);
		goto cleanup;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_REVOKE_JOB_CREDENTIAL ;
	request_msg . data = revoke_job_cred_ptr ; 
	if ( ( rc = slurm_send_node_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_send_node_msg error for %s", thd_ptr->node_name);
		goto cleanup;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_receive_msg error for %s", thd_ptr->node_name);
		goto cleanup;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR ) {
		error ("thread_revoke_job_cred/slurm_shutdown_msg_conn error for %s", thd_ptr->node_nam);
		goto cleanup;
	}
	if ( msg_size ) {
		error ("thread_revoke_job_cred/msg_size error %d for %s", msg_size, thd_ptr->node_nam);
		goto cleanup;
	}

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc)
				error ("thread_revoke_job_cred/rc error %d for %s", rc, thd_ptr->node_nam);
			break ;
		default:
				error ("thread_revoke_job_cred/msg_type error %d for %s",
				       response_msg.msg_type, thd_ptr->node_nam);
			break ;
	}

cleanup:
	thd_ptr->state = DSH_DONE;
	thd_ptr->finish = time(NULL);

	/* Signal completion so another thread can replace us */
	pthread_mutex_lock(&threadcount_mutex);
	threadcount--;
	pthread_cond_signal(&threadcount_cond);
	pthread_mutex_unlock(&threadcount_mutex);

	pthread_exit ((void *)NULL);
}
