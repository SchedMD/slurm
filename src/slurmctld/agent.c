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
 *  The functions below permit slurm to initiate parallel tasks as a 
 *  detached thread and let the functions below make sure the work happens. 
 *  For example, when a job's time limit is to be changed slurmctld needs 
 *  to notify the slurmd on every node to which the job was allocated.  
 *  We don't want to hang slurmctld's primary function (the job update RPC)  
 *  to perform this work, so it just initiates an agent to perform the work.  
 *  The agent is passed all details required to perform the work, so it will 
 *  be possible to execute the agent as an pthread, process, or even a daemon 
 *  on some other computer.
 *
 *  The main agent thread creates a separate thread for each node to be
 *  communicated with up to AGENT_THREAD_COUNT. A special watchdog thread  
 *  sends SIGLARM to any threads that have been active (in DSH_ACTIVE state)  
 *  for more than COMMAND_TIMEOUT seconds. 
 *  The agent responds to slurmctld via a function call or an RPC as required.
 *  For example, informing slurmctld that some node is not responding.
 *
 *  All the state for each thread is maintained in thd_t struct, which is 
 *  used by the watchdog thread as well as the communication threads.
 *
 *  NOTE: REQUEST_KILL_JOB  and REQUEST_KILL_TIMELIMIT responses are 
 *  handled immediately rather than in bulk upon completion of the RPC 
 *  to all nodes
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xsignal.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_api.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"

#if COMMAND_TIMEOUT == 1
#  define WDOG_POLL 		1	/* secs */
#else
#  define WDOG_POLL 		2	/* secs */
#endif

typedef enum { DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_NO_RESP, 
	       DSH_FAILED } state_t;

typedef struct thd {
	pthread_t thread;		/* thread ID */
	pthread_attr_t attr;		/* thread attributes */
	state_t state;			/* thread state */
	time_t time;			/* start time or delta time 
					 * at termination */
	struct sockaddr_in slurm_addr;	/* network address */
	char node_name[MAX_NAME_LEN];	/* node's name */
} thd_t;

typedef struct agent_info {
	pthread_mutex_t thread_mutex;	/* agent specific mutex */
	pthread_cond_t thread_cond;	/* agent specific condition */
	uint32_t thread_count;		/* number of threads records */
	uint32_t threads_active;	/* currently active threads */
	uint16_t retry;			/* if set, keep trying */
	thd_t *thread_struct;		/* thread structures */
	bool get_reply;			/* flag if reply expected */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void **msg_args_pptr;		/* RPC data to be used */
} agent_info_t;

typedef struct task_info {
	pthread_mutex_t *thread_mutex_ptr; /* pointer to agent specific 
					    * mutex */
	pthread_cond_t *thread_cond_ptr;/* pointer to agent specific
					 * condition */
	uint32_t *threads_active_ptr;	/* currently active thread ptr */
	thd_t *thread_struct_ptr;	/* thread structures ptr */
	bool get_reply;			/* flag if reply expected */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void *msg_args_ptr;		/* ptr to RPC data to be used */
} task_info_t;

static void _alarm_handler(int dummy);
static void _list_delete_retry(void *retry_entry);
static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr);
static task_info_t *_make_task_data(agent_info_t *agent_info_ptr, int inx);
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count);
static void _slurmctld_free_job_launch_msg(batch_job_launch_msg_t * msg);
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr);
static void *_thread_per_node_rpc(void *args);
static int   _valid_agent_arg(agent_arg_t *agent_arg_ptr);
static void *_wdog(void *args);

static pthread_mutex_t retry_mutex = PTHREAD_MUTEX_INITIALIZER;
static List retry_list = NULL;		/* agent_arg_t list for retry */

/*
 * agent - party responsible for transmitting an common RPC in parallel 
 *	across a set of nodes
 * IN pointer to agent_arg_t, which is xfree'd (including slurm_addr, 
 *	node_names and msg_args) upon completion if AGENT_IS_THREAD is set
 * RET always NULL (function format just for use as pthread)
 */
void *agent(void *args)
{
	int i, rc;
	pthread_attr_t attr_wdog;
	pthread_t thread_wdog;
	agent_arg_t *agent_arg_ptr = args;
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr;
	task_info_t *task_specific_ptr;

	/* basic argument value tests */
	if (_valid_agent_arg(agent_arg_ptr))
		goto cleanup;

	xsignal(SIGALRM, _alarm_handler);

	/* initialize the agent data structures */
	agent_info_ptr = _make_agent_info(agent_arg_ptr);
	thread_ptr = agent_info_ptr->thread_struct;

	/* start the watchdog thread */
	if (pthread_attr_init(&attr_wdog))
		fatal("pthread_attr_init error %m");
	if (pthread_attr_setdetachstate
	    (&attr_wdog, PTHREAD_CREATE_JOINABLE))
		error("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	if (pthread_attr_setscope(&attr_wdog, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create(&thread_wdog, &attr_wdog, _wdog,
			   (void *) agent_info_ptr)) {
		error("pthread_create error %m");
		sleep(1);	/* sleep and try once more */
		if (pthread_create(&thread_wdog, &attr_wdog, _wdog, args))
			fatal("pthread_create error %m");
	}
#if 	AGENT_THREAD_COUNT < 1
	fatal("AGENT_THREAD_COUNT value is invalid");
#endif
	/* start all the other threads (up to AGENT_THREAD_COUNT active) */
	for (i = 0; i < agent_info_ptr->thread_count; i++) {

		/* wait until "room" for another thread */
		slurm_mutex_lock(&agent_info_ptr->thread_mutex);
		while (agent_info_ptr->threads_active >=
		       AGENT_THREAD_COUNT) {
			pthread_cond_wait(&agent_info_ptr->thread_cond,
					  &agent_info_ptr->thread_mutex);
		}

		/* create thread specific data, NOTE: freed from 
		 *      _thread_per_node_rpc() */
		task_specific_ptr = _make_task_data(agent_info_ptr, i);

		if (pthread_attr_init(&thread_ptr[i].attr))
			fatal("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate(&thread_ptr[i].attr,
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope(&thread_ptr[i].attr,
					  PTHREAD_SCOPE_SYSTEM))
			error("pthread_attr_setscope error %m");
#endif
		while ((rc = pthread_create(&thread_ptr[i].thread,
					    &thread_ptr[i].attr,
					    _thread_per_node_rpc,
					    (void *) task_specific_ptr))) {
			error("pthread_create error %m");
			if (agent_info_ptr->threads_active)
				pthread_cond_wait(&agent_info_ptr->
						  thread_cond,
						  &agent_info_ptr->
						  thread_mutex);
			else {
				slurm_mutex_unlock(&agent_info_ptr->
						     thread_mutex);
				sleep(1);
				slurm_mutex_lock(&agent_info_ptr->
						   thread_mutex);
			}
		}

		agent_info_ptr->threads_active++;
		slurm_mutex_unlock(&agent_info_ptr->thread_mutex);
	}

	/* wait for termination of remaining threads */
	pthread_join(thread_wdog, NULL);

      cleanup:
#if AGENT_IS_THREAD
	if (agent_arg_ptr) {
		xfree(agent_arg_ptr->slurm_addr);
		xfree(agent_arg_ptr->node_names);
		if (agent_arg_ptr->msg_args) {
			if (agent_arg_ptr->msg_type ==
			    REQUEST_BATCH_JOB_LAUNCH)
				_slurmctld_free_job_launch_msg
				    (agent_arg_ptr->msg_args);
			else
				xfree(agent_arg_ptr->msg_args);
		}
		xfree(agent_arg_ptr);
	}
#endif

	if (agent_info_ptr) {
		xfree(agent_info_ptr->thread_struct);
		xfree(agent_info_ptr);
	}
	return NULL;
}

/* Basic validity test of agent argument */
static int _valid_agent_arg(agent_arg_t *agent_arg_ptr)
{
	xassert(agent_arg_ptr);
	xassert(agent_arg_ptr->slurm_addr);
	xassert(agent_arg_ptr->node_names);
	xassert((agent_arg_ptr->msg_type == REQUEST_KILL_JOB) ||
		(agent_arg_ptr->msg_type == REQUEST_KILL_TIMELIMIT) || 
		(agent_arg_ptr->msg_type == REQUEST_UPDATE_JOB_TIME) ||
		(agent_arg_ptr->msg_type == REQUEST_KILL_TASKS) || 
		(agent_arg_ptr->msg_type == REQUEST_PING) || 
		(agent_arg_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) || 
		(agent_arg_ptr->msg_type == REQUEST_SHUTDOWN) || 
		(agent_arg_ptr->msg_type == REQUEST_RECONFIGURE) || 
		(agent_arg_ptr->msg_type == REQUEST_NODE_REGISTRATION_STATUS));

	if (agent_arg_ptr->node_count == 0)
		return SLURM_FAILURE;	/* no messages to be sent */
	return SLURM_SUCCESS;
}

static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr)
{
	int i;
	agent_info_t *agent_info_ptr;
	thd_t *thread_ptr;

	agent_info_ptr = xmalloc(sizeof(agent_info_t));

	slurm_mutex_init(&agent_info_ptr->thread_mutex);
	if (pthread_cond_init(&agent_info_ptr->thread_cond, NULL))
		fatal("pthread_cond_init error %m");
	agent_info_ptr->thread_count   = agent_arg_ptr->node_count;
	agent_info_ptr->retry          = agent_arg_ptr->retry;
	agent_info_ptr->threads_active = 0;
	thread_ptr = xmalloc(agent_arg_ptr->node_count * sizeof(thd_t));
	agent_info_ptr->thread_struct  = thread_ptr;
	agent_info_ptr->msg_type       = agent_arg_ptr->msg_type;
	agent_info_ptr->msg_args_pptr  = &agent_arg_ptr->msg_args;
	if ((agent_arg_ptr->msg_type != REQUEST_SHUTDOWN) &&
	    (agent_arg_ptr->msg_type != REQUEST_RECONFIGURE))
		agent_info_ptr->get_reply = true;
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		thread_ptr[i].state      = DSH_NEW;
		thread_ptr[i].slurm_addr = agent_arg_ptr->slurm_addr[i];
		strncpy(thread_ptr[i].node_name,
			&agent_arg_ptr->node_names[i * MAX_NAME_LEN],
			MAX_NAME_LEN);
	}

	return agent_info_ptr;
}

static task_info_t *_make_task_data(agent_info_t *agent_info_ptr, int inx)
{
	task_info_t *task_info_ptr;
	task_info_ptr = xmalloc(sizeof(task_info_t));

	task_info_ptr->thread_mutex_ptr  = &agent_info_ptr->thread_mutex;
	task_info_ptr->thread_cond_ptr   = &agent_info_ptr->thread_cond;
	task_info_ptr->threads_active_ptr= &agent_info_ptr->threads_active;
	task_info_ptr->thread_struct_ptr = &agent_info_ptr->thread_struct[inx];
	task_info_ptr->get_reply         = agent_info_ptr->get_reply;
	task_info_ptr->msg_type          = agent_info_ptr->msg_type;
	task_info_ptr->msg_args_ptr      = *agent_info_ptr->msg_args_pptr;

	return task_info_ptr;
}

/* 
 * _wdog - Watchdog thread. Send SIGALRM to threads which have been active 
 *	for too long.
 * IN args - pointer to agent_info_t with info on threads to watch
 * Sleep for WDOG_POLL seconds between polls.
 */
static void *_wdog(void *args)
{
	int fail_cnt, no_resp_cnt;
	bool work_done;
	int i, delay, max_delay = 0;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
#if AGENT_IS_THREAD
	/* Locks: Write job and write node */
	slurmctld_lock_t node_write_lock =
	    { NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
#else
	int done_cnt;
	char *slurm_names;
#endif

	while (1) {
		work_done   = true;	/* assume all threads complete */
		fail_cnt    = 0;	/* assume no threads failures */
		no_resp_cnt = 0;	/* assume all threads respond */

		sleep(WDOG_POLL);

		slurm_mutex_lock(&agent_ptr->thread_mutex);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			switch (thread_ptr[i].state) {
			case DSH_ACTIVE:
				work_done = false;
				delay = difftime(time(NULL),
						 thread_ptr[i].time);
				if (delay >= COMMAND_TIMEOUT) {
					debug3("thd %d timed out\n", 
					       thread_ptr[i].thread);
					pthread_kill(thread_ptr[i].thread,
						     SIGALRM);
				}
				break;
			case DSH_NEW:
				work_done = false;
				break;
			case DSH_DONE:
				if (max_delay < (int) thread_ptr[i].time)
					max_delay =
					    (int) thread_ptr[i].time;
				break;
			case DSH_NO_RESP:
				no_resp_cnt++;
				break;
			case DSH_FAILED:
				fail_cnt++;
				break;
			}
		}
		if (work_done)
			break;
		slurm_mutex_unlock(&agent_ptr->thread_mutex);
	}

	/* Notify slurmctld of non-responding nodes */
	if (no_resp_cnt) {
#if AGENT_IS_THREAD
		/* Update node table data for non-responding nodes */
		lock_slurmctld(node_write_lock);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			if (thread_ptr[i].state == DSH_NO_RESP)
				node_not_resp(thread_ptr[i].node_name);
		}
		unlock_slurmctld(node_write_lock);
#else
		/* Build a list of all non-responding nodes and send 
		 * it to slurmctld */
		slurm_names = xmalloc(fail_cnt * MAX_NAME_LEN);
		fail_cnt = 0;
		for (i = 0; i < agent_ptr->thread_count; i++) {
			if (thread_ptr[i].state == DSH_NO_RESP) {
				strncpy(&slurm_names
					[MAX_NAME_LEN * fail_cnt],
					thread_ptr[i].node_name,
					MAX_NAME_LEN);
				error
				    ("agent/_wdog: node %s failed to respond",
				     thread_ptr[i].node_name);
				fail_cnt++;
			}
		}

		/* send RPC */
		fatal
		    ("Code development needed here if agent is not thread");

		xfree(slurm_names);
#endif
		if (agent_ptr->retry)
			_queue_agent_retry(agent_ptr, no_resp_cnt);
	}
#if AGENT_IS_THREAD
	/* Update last_response on responding nodes */
	lock_slurmctld(node_write_lock);
	for (i = 0; i < agent_ptr->thread_count; i++) {
		if (thread_ptr[i].state == DSH_FAILED)
			set_node_down(thread_ptr[i].node_name);
		if (thread_ptr[i].state == DSH_DONE)
			node_did_resp(thread_ptr[i].node_name);
	}
	unlock_slurmctld(node_write_lock);
	/* attempt to schedule when all nodes registered, not 
	 * after each node, the overhead would be too high */
	if ((agent_ptr->msg_type == REQUEST_KILL_TIMELIMIT) ||
	    (agent_ptr->msg_type == REQUEST_KILL_JOB))
		schedule();
#else
	/* Build a list of all responding nodes and send it to slurmctld to 
	 * update time stamps */
	done_cnt = agent_ptr->thread_count - fail_cnt - no_resp_cnt;
	slurm_names = xmalloc(done_cnt * MAX_NAME_LEN);
	done_cnt = 0;
	for (i = 0; i < agent_ptr->thread_count; i++) {
		if (thread_ptr[i].state == DSH_DONE)
			strncpy(&slurm_names[MAX_NAME_LEN * done_cnt],
				thread_ptr[i].node_name, MAX_NAME_LEN);
			done_cnt++;
		}
	}
	/* need support for node failures here too */

	/* send RPC */
	fatal("Code development needed here if agent is not thread");

	xfree(slurm_addr);
#endif
	if (max_delay)
		debug("agent maximum delay %d seconds", max_delay);

	slurm_mutex_unlock(&agent_ptr->thread_mutex);
	return (void *) NULL;
}

/*
 * _thread_per_node_rpc - thread to issue an RPC on a collection of nodes
 * IN/OUT args - pointer to task_info_t, xfree'd on completion
 */
static void *_thread_per_node_rpc(void *args)
{
	int rc = SLURM_SUCCESS;
	int timeout = 0;
	slurm_msg_t msg;
	task_info_t *task_ptr = (task_info_t *) args;
	thd_t *thread_ptr = task_ptr->thread_struct_ptr;
	state_t thread_state = DSH_NO_RESP;

#if AGENT_IS_THREAD
	struct node_record *node_ptr;
	/* Locks: Write write node */
	slurmctld_lock_t node_write_lock =
	    { NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
#endif
	xassert(args != NULL);

	slurm_mutex_lock(task_ptr->thread_mutex_ptr);
	thread_ptr->state = DSH_ACTIVE;
	thread_ptr->time = time(NULL);
	slurm_mutex_unlock(task_ptr->thread_mutex_ptr);

	/* send request message */
	msg.address  = thread_ptr->slurm_addr;
	msg.msg_type = task_ptr->msg_type;
	msg.data     = task_ptr->msg_args_ptr;

	if (task_ptr->msg_type == REQUEST_KILL_TIMELIMIT) 
		timeout = slurmctld_conf.kill_wait;

	if (slurm_send_recv_rc_msg(&msg, &rc, timeout) < 0) {
		error("agent: %s: %m", thread_ptr->node_name);
		goto cleanup;
	}

	if (!task_ptr->get_reply) {
		thread_state = DSH_DONE;
		goto cleanup;
	}


#if AGENT_IS_THREAD
	/* SPECIAL CASE: Immediately mark node as IDLE on job kill reply */
	if ((task_ptr->msg_type == REQUEST_KILL_TIMELIMIT) ||
	     (task_ptr->msg_type == REQUEST_KILL_JOB)) {
		lock_slurmctld(node_write_lock);
		if ((node_ptr = find_node_record(thread_ptr->node_name))) {
			kill_job_msg_t *kill_job;
			kill_job = (kill_job_msg_t *) task_ptr->msg_args_ptr;
			debug3("Kill job_id %u on node %s ",
			       kill_job->job_id, thread_ptr->node_name);
			make_node_idle(node_ptr, 
				       find_job_record(kill_job->job_id));
			/* scheduler(); Overhead too high, 
			 * only do when last node registers */
		}
		unlock_slurmctld(node_write_lock);
	}
#endif

	switch (rc) {
	case SLURM_SUCCESS:
		debug3("agent processed RPC to node %s", thread_ptr->node_name);
		thread_state = DSH_DONE;
		break;
	case ESLURMD_EPILOG_FAILED:
		error("Epilog failure on host %s, setting DOWN", 
		      thread_ptr->node_name);
		thread_state = DSH_FAILED;
		break;
	case ESLURMD_PROLOG_FAILED:
		error("Prolog failure on host %s, setting DOWN",
		      thread_ptr->node_name);
		thread_state = DSH_FAILED;
		break;
	case ESLURM_INVALID_JOB_ID: /* Not indicative of a real error */
		debug2("agent processed RPC to node %s, error %s",
		       thread_ptr->node_name, "Invalid Job Id");
		thread_state = DSH_DONE;
		break;

	default:
		error("agent error from host %s: %s", 
		      thread_ptr->node_name, slurm_strerror(rc));
		thread_state = DSH_DONE;
	}

      cleanup:
	slurm_mutex_lock(task_ptr->thread_mutex_ptr);
	thread_ptr->state = thread_state;
	thread_ptr->time = (time_t) difftime(time(NULL), thread_ptr->time);

	/* Signal completion so another thread can replace us */
	(*task_ptr->threads_active_ptr)--;
	pthread_cond_signal(task_ptr->thread_cond_ptr);
	slurm_mutex_unlock(task_ptr->thread_mutex_ptr);

	xfree(args);
	return (void *) NULL;
}

/*
 * SIGALRM handler.  This is just a stub because we are really interested
 * in interrupting connect() in k4cmd/rcmd or select() in rsh() below and
 * causing them to return EINTR.
 */
static void _alarm_handler(int dummy)
{
}


/*
 * _queue_agent_retry - Queue any failed RPCs for later replay
 * IN agent_info_ptr - pointer to info on completed agent requests
 * IN count - number of agent requests which failed, count to requeue
 */
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count)
{
	agent_arg_t *agent_arg_ptr;
	thd_t *thread_ptr = agent_info_ptr->thread_struct;
	int i, j;

	if (count == 0)
		return;

	/* build agent argument with just the RPCs to retry */
	agent_arg_ptr = xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->node_count = count;
	agent_arg_ptr->retry = 1;
	agent_arg_ptr->slurm_addr = xmalloc(sizeof(struct sockaddr_in)
					    * count);
	agent_arg_ptr->node_names = xmalloc(MAX_NAME_LEN * count);
	agent_arg_ptr->msg_type = agent_info_ptr->msg_type;
	agent_arg_ptr->msg_args = *(agent_info_ptr->msg_args_pptr);
	*(agent_info_ptr->msg_args_pptr) = NULL;

	j = 0;
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		if (thread_ptr[i].state != DSH_NO_RESP)
			continue;
		agent_arg_ptr->slurm_addr[j] = thread_ptr[i].slurm_addr;
		strncpy(&agent_arg_ptr->node_names[j * MAX_NAME_LEN],
			thread_ptr[i].node_name, MAX_NAME_LEN);
		if ((++j) == count)
			break;
	}

	/* add the requeust to a list */
	slurm_mutex_lock(&retry_mutex);
	if (retry_list == NULL) {
		retry_list = list_create(&_list_delete_retry);
		if (retry_list == NULL)
			fatal("list_create failed");
	}
	if (list_enqueue(retry_list, (void *) agent_arg_ptr) == 0)
		fatal("list_append failed");
	slurm_mutex_unlock(&retry_mutex);
}

/*
 * _list_delete_retry - delete an entry from the retry list, 
 *	see common/list.h for documentation
 */
static void _list_delete_retry(void *retry_entry)
{
	agent_arg_t *agent_arg_ptr;	/* pointer to part_record */

	agent_arg_ptr = (agent_arg_t *) retry_entry;
	xfree(agent_arg_ptr->slurm_addr);
	xfree(agent_arg_ptr->node_names);
#if AGENT_IS_THREAD
	xfree(agent_arg_ptr->msg_args);
#endif
	xfree(agent_arg_ptr);
}


/*
 * agent_retry - Agent for retrying pending RPCs (top one on the queue), 
 * IN args - unused
 * RET always NULL (function format just for use as pthread)
 */
void *agent_retry(void *args)
{
	agent_arg_t *agent_arg_ptr = NULL;

	slurm_mutex_lock(&retry_mutex);
	if (retry_list)
		agent_arg_ptr = (agent_arg_t *) list_dequeue(retry_list);
	slurm_mutex_unlock(&retry_mutex);

	if (agent_arg_ptr)
		_spawn_retry_agent(agent_arg_ptr);

	return NULL;
}

/* retry_pending - retry all pending RPCs for the given node name
 * IN node_name - name of a node to executing pending RPCs for */
void retry_pending(char *node_name)
{
	int list_size = 0, i, j, found;
	agent_arg_t *agent_arg_ptr = NULL;

	slurm_mutex_lock(&retry_mutex);
	if (retry_list) {
		list_size = list_count(retry_list);
	}
	for (i = 0; i < list_size; i++) {
		agent_arg_ptr = (agent_arg_t *) list_dequeue(retry_list);
		found = 0;
		for (j = 0; j < agent_arg_ptr->node_count; j++) {
			if (strncmp
			    (&agent_arg_ptr->node_names[j * MAX_NAME_LEN],
			     node_name, MAX_NAME_LEN))
				continue;
			found = 1;
			break;
		}
		if (found)	/* issue this RPC */
			_spawn_retry_agent(agent_arg_ptr);
		else		/* put the RPC back on the queue */
			list_enqueue(retry_list, (void *) agent_arg_ptr);
	}
	slurm_mutex_unlock(&retry_mutex);
}

/* _spawn_retry_agent - pthread_crate an agent for the given task */
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr)
{
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if (agent_arg_ptr == NULL)
		return;

	debug3("Spawning RPC retry agent for msg_type %u", 
	       agent_arg_ptr->msg_type);
	if (pthread_attr_init(&attr_agent))
		fatal("pthread_attr_init error %m");
	if (pthread_attr_setdetachstate(&attr_agent,
					PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	if (pthread_attr_setscope(&attr_agent, PTHREAD_SCOPE_SYSTEM))
		error("pthread_attr_setscope error %m");
#endif
	if (pthread_create(&thread_agent, &attr_agent,
			   agent, (void *) agent_arg_ptr)) {
		error("pthread_create error %m");
		sleep(1);	/* sleep and try once more */
		if (pthread_create(&thread_agent, &attr_agent,
				   agent, (void *) agent_arg_ptr))
			fatal("pthread_create error %m");
	}
}

/* _slurmctld_free_job_launch_msg is a variant of slurm_free_job_launch_msg
 *	because all environment variables currently loaded in one xmalloc 
 *	buffer (see get_job_env()), which is different from how slurmd 
 *	assembles the data from a message
 */
static void _slurmctld_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	if (msg) {
		if (msg->environment) {
			xfree(msg->environment[0]);
			xfree(msg->environment);
		}
		slurm_free_job_launch_msg(msg);
	}
}

/* agent_purge - purge all pending RPC requests */
void agent_purge(void)
{
	retry_list = list_create(NULL);

	slurm_mutex_lock(&retry_mutex);
	if (retry_list == NULL)
		list_destroy(retry_list);
	slurm_mutex_unlock(&retry_mutex);
}
