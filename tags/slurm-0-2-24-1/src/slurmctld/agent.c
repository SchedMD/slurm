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
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/slurmctld.h"

#if COMMAND_TIMEOUT == 1
#  define WDOG_POLL 		1	/* secs */
#else
#  define WDOG_POLL 		2	/* secs */
#endif
#define MAX_RETRIES	10

typedef enum {
	DSH_NEW,        /* Request not yet started */
	DSH_ACTIVE,     /* Request in progress */
	DSH_DONE,       /* Request completed normally */
	DSH_NO_RESP,    /* Request timed out */
	DSH_FAILED      /* Request resulted in error */
} state_t;

typedef struct thd {
	pthread_t thread;		/* thread ID */
	pthread_attr_t attr;		/* thread attributes */
	state_t state;			/* thread state */
	time_t start_time;		/* start time */
	time_t end_time;		/* end time or delta time 
					 * upon termination */
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

typedef struct queued_request {
	agent_arg_t* agent_arg_ptr;	/* The queued request */
	time_t       last_attempt;	/* Time of last xmit attempt */
} queued_request_t;

static void _alarm_handler(int dummy);
static inline void _comm_err(char *node_name);
static void _list_delete_retry(void *retry_entry);
static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr);
static task_info_t *_make_task_data(agent_info_t *agent_info_ptr, int inx);
static void _purge_agent_args(agent_arg_t *agent_arg_ptr);
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count);
static void _slurmctld_free_job_launch_msg(batch_job_launch_msg_t * msg);
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr);
static void *_thread_per_node_rpc(void *args);
static int   _valid_agent_arg(agent_arg_t *agent_arg_ptr);
static void *_wdog(void *args);

static pthread_mutex_t retry_mutex = PTHREAD_MUTEX_INITIALIZER;
static List retry_list = NULL;		/* agent_arg_t list for retry */
static bool run_scheduler = false;

/*
 * agent - party responsible for transmitting an common RPC in parallel 
 *	across a set of nodes
 * IN pointer to agent_arg_t, which is xfree'd (including slurm_addr, 
 *	node_names and msg_args) upon completion if AGENT_IS_THREAD is set
 * RET always NULL (function format just for use as pthread)
 */
void *agent(void *args)
{
	int i, rc, retries = 0;
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
	while (pthread_create(&thread_wdog, &attr_wdog, _wdog,
				(void *) agent_info_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		sleep(1);	/* sleep and again */
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
	_purge_agent_args(agent_arg_ptr);
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
	int fail_cnt, no_resp_cnt, retry_cnt;
	bool work_done;
	int i, max_delay = 0;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
	time_t now;
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
		retry_cnt   = 0;	/* assume no required retries */

		sleep(WDOG_POLL);
		now = time(NULL);

		slurm_mutex_lock(&agent_ptr->thread_mutex);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			switch (thread_ptr[i].state) {
			case DSH_ACTIVE:
				work_done = false;
				if (thread_ptr[i].end_time <= now) {
					debug3("agent thread %lu timed out\n", 
					       (unsigned long) thread_ptr[i].thread);
					if (pthread_kill(thread_ptr[i].thread,
						     SIGALRM) == ESRCH)
						thread_ptr[i].state = DSH_NO_RESP;
				}
				break;
			case DSH_NEW:
				work_done = false;
				break;
			case DSH_DONE:
				if (max_delay < (int)thread_ptr[i].end_time)
					max_delay = (int)thread_ptr[i].end_time;
				break;
			case DSH_NO_RESP:
				no_resp_cnt++;
				retry_cnt++;
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
				node_not_resp(thread_ptr[i].node_name,
				              thread_ptr[i].start_time);
		}
		if (agent_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
			/* Requeue the request */
			batch_job_launch_msg_t *launch_msg_ptr = 
					*agent_ptr->msg_args_pptr;
			uint32_t job_id = launch_msg_ptr->job_id;
			info("Non-responding node, requeue JobId=%u", job_id);
			job_complete(job_id, 0, true, 0);
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
		fatal("Code development needed here if agent is not thread");

		xfree(slurm_names);
#endif
	}
	if (retry_cnt && agent_ptr->retry)
		_queue_agent_retry(agent_ptr, retry_cnt);

#if AGENT_IS_THREAD
	/* Update last_response on responding nodes */
	lock_slurmctld(node_write_lock);
	for (i = 0; i < agent_ptr->thread_count; i++) {
		if (thread_ptr[i].state == DSH_FAILED)
			set_node_down(thread_ptr[i].node_name, 
			              "Prolog/epilog failure");
		if (thread_ptr[i].state == DSH_DONE)
			node_did_resp(thread_ptr[i].node_name);
	}
	unlock_slurmctld(node_write_lock);

	if (run_scheduler) {
		run_scheduler = false;
		schedule();	/* has own locks */
	}
	if ((agent_ptr->msg_type == REQUEST_PING) ||
	    (agent_ptr->msg_type == REQUEST_NODE_REGISTRATION_STATUS))
		ping_end();
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
		debug2("agent maximum delay %d seconds", max_delay);

	slurm_mutex_unlock(&agent_ptr->thread_mutex);
	return (void *) NULL;
}

/* Report a communications error for specified node */
static inline void _comm_err(char *node_name)
{
#if AGENT_IS_THREAD
	if (is_node_resp (node_name))
#endif
		error("agent/send_recv_msg: %s: %m", node_name);
}

/*
 * _thread_per_node_rpc - thread to issue an RPC on a collection of nodes
 * IN/OUT args - pointer to task_info_t, xfree'd on completion
 */
static void *_thread_per_node_rpc(void *args)
{
	int rc = SLURM_SUCCESS, timeout = 0;
	slurm_msg_t msg;
	task_info_t *task_ptr = (task_info_t *) args;
	thd_t *thread_ptr = task_ptr->thread_struct_ptr;
	state_t thread_state = DSH_NO_RESP;
	slurm_msg_type_t msg_type = task_ptr->msg_type;
	bool is_kill_msg;
#if AGENT_IS_THREAD
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = { 
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
#endif
	xassert(args != NULL);

	slurm_mutex_lock(task_ptr->thread_mutex_ptr);
	thread_ptr->state = DSH_ACTIVE;
	thread_ptr->start_time = time(NULL);
	slurm_mutex_unlock(task_ptr->thread_mutex_ptr);

	is_kill_msg = ((msg_type == REQUEST_KILL_TIMELIMIT) ||
	               (msg_type == REQUEST_KILL_JOB)     );

	/* send request message */
	msg.address  = thread_ptr->slurm_addr;
	msg.msg_type = msg_type;
	msg.data     = task_ptr->msg_args_ptr;

	thread_ptr->end_time = thread_ptr->start_time + COMMAND_TIMEOUT;
	if (task_ptr->get_reply) {
		if (slurm_send_recv_rc_msg(&msg, &rc, timeout) < 0) {
			_comm_err(thread_ptr->node_name);
			goto cleanup;
		}
	} else {
		if (slurm_send_only_node_msg(&msg) < 0)
			_comm_err(thread_ptr->node_name);
		else
			thread_state = DSH_DONE;
		goto cleanup;
	}

#if AGENT_IS_THREAD
	/* SPECIAL CASE: Mark node as IDLE if job already complete */
	if (is_kill_msg && (rc == ESLURMD_KILL_JOB_ALREADY_COMPLETE)) {
		kill_job_msg_t *kill_job;
		kill_job = (kill_job_msg_t *) task_ptr->msg_args_ptr;
		rc = SLURM_SUCCESS;
		lock_slurmctld(job_write_lock);
		if (job_epilog_complete(kill_job->job_id, 
		                        thread_ptr->node_name, rc))
			run_scheduler = true;
		unlock_slurmctld(job_write_lock);
	}

	/* SPECIAL CASE: Kill non-startable batch job */
	if ((msg_type == REQUEST_BATCH_JOB_LAUNCH) && rc) {
		batch_job_launch_msg_t *launch_msg_ptr = task_ptr->msg_args_ptr;
		uint32_t job_id = launch_msg_ptr->job_id;
		info("Killing non-startable batch job %u: %s", 
			job_id, slurm_strerror(rc));
		thread_state = DSH_DONE;
		lock_slurmctld(job_write_lock);
		job_complete(job_id, 0, false, 1);
		unlock_slurmctld(job_write_lock);
		goto cleanup;
	}
#endif

	switch (rc) {
	case SLURM_SUCCESS:
/*		debug3("agent processed RPC to node %s", 
			thread_ptr->node_name); */
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
	case ESLURM_INVALID_JOB_ID:  /* Not indicative of a real error */
	case ESLURMD_JOB_NOTRUNNING: /* Not indicative of a real error */
		debug2("agent processed RPC to node %s: %s",
		       thread_ptr->node_name, slurm_strerror(rc));
		thread_state = DSH_DONE;
		break;
	case ESLURMD_KILL_JOB_FAILED:		/* non-killable process */
		info("agent KILL_JOB RPC to node %s FAILED",
		       thread_ptr->node_name);
		thread_state = DSH_FAILED;
		break;
	default:
		error("agent error from host %s for msg type %d: %s", 
		      thread_ptr->node_name, task_ptr->msg_type, 
		      slurm_strerror(rc));
		thread_state = DSH_DONE;
	}

      cleanup:
	slurm_mutex_lock(task_ptr->thread_mutex_ptr);
	thread_ptr->state = thread_state;
	thread_ptr->end_time = (time_t) difftime(time(NULL), 
						thread_ptr->start_time);

	/* Signal completion so another thread can replace us */
	(*task_ptr->threads_active_ptr)--;
	slurm_mutex_unlock(task_ptr->thread_mutex_ptr);
	pthread_cond_signal(task_ptr->thread_cond_ptr);

	xfree(args);
	return (void *) NULL;
}

/*
 * SIGALRM handler.  We are really interested in interrupting hung communictions
 * and causing them to return EINTR. Multiple interupts might be required.
 */
static void _alarm_handler(int dummy)
{
	xsignal(SIGALRM, _alarm_handler);
}

/*
 * _queue_agent_retry - Queue any failed RPCs for later replay
 * IN agent_info_ptr - pointer to info on completed agent requests
 * IN count - number of agent requests which failed, count to requeue
 */
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count)
{
	agent_arg_t *agent_arg_ptr;
	queued_request_t *queued_req_ptr = NULL;
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
	if (count != j) {
		error("agent: Retry count (%d) != actual count (%d)", 
			count, j);
		agent_arg_ptr->node_count = j;
	}
	debug2("Queue RPC msg_type=%u, nodes=%d for retry", 
	       agent_arg_ptr->msg_type, j);

	/* add the requeust to a list */
	queued_req_ptr = xmalloc(sizeof(queued_request_t));
	queued_req_ptr->agent_arg_ptr = agent_arg_ptr;
	queued_req_ptr->last_attempt  = time(NULL);
	slurm_mutex_lock(&retry_mutex);
	if (retry_list == NULL) {
		retry_list = list_create(&_list_delete_retry);
		if (retry_list == NULL)
			fatal("list_create failed");
	}
	if (list_append(retry_list, (void *) queued_req_ptr) == 0)
		fatal("list_append failed");
	slurm_mutex_unlock(&retry_mutex);
}

/*
 * _list_delete_retry - delete an entry from the retry list, 
 *	see common/list.h for documentation
 */
static void _list_delete_retry(void *retry_entry)
{
	queued_request_t *queued_req_ptr;

	if (! retry_entry)
		return;

	queued_req_ptr = (queued_request_t *) retry_entry;
	_purge_agent_args(queued_req_ptr->agent_arg_ptr);
	xfree(queued_req_ptr);
}


/*
 * agent_retry - Agent for retrying pending RPCs. One pending request is 
 *	issued if it has been pending for at least min_wait seconds
 * IN min_wait - Minimum wait time between re-issue of a pending RPC
 * RET count of queued requests remaining
 */
extern int agent_retry (int min_wait)
{
	int list_size = 0;
	time_t now = time(NULL);
	queued_request_t *queued_req_ptr = NULL;

	slurm_mutex_lock(&retry_mutex);
	if (retry_list) {
		double age = 0;
		list_size = list_count(retry_list);
		queued_req_ptr = (queued_request_t *) list_peek(retry_list);
		if (queued_req_ptr) {
			age = difftime(now, queued_req_ptr->last_attempt);
			if (age > min_wait)
				queued_req_ptr = (queued_request_t *) 
					list_pop(retry_list);
			else /* too new */
				queued_req_ptr = NULL;
		}
	}
	slurm_mutex_unlock(&retry_mutex);

	if (queued_req_ptr) {
		agent_arg_t *agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
		xfree(queued_req_ptr);
		if (agent_arg_ptr)
			_spawn_retry_agent(agent_arg_ptr);
		else
			error("agent_retry found record with no agent_args");
	}

	return list_size;
}

/*
 * agent_queue_request - put a new request on the queue for later execution
 * IN agent_arg_ptr - the request to enqueue
 */
void agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	queued_request_t *queued_req_ptr = NULL;

	queued_req_ptr = xmalloc(sizeof(queued_request_t));
	queued_req_ptr->agent_arg_ptr = agent_arg_ptr;
/*	queued_req_ptr->last_attempt  = 0; Implicit */

	slurm_mutex_lock(&retry_mutex);
	if (retry_list == NULL) {
		retry_list = list_create(&_list_delete_retry);
		if (retry_list == NULL)
			fatal("list_create failed");
	}
	list_prepend(retry_list, (void *)queued_req_ptr);
	slurm_mutex_unlock(&retry_mutex);
}

/* _spawn_retry_agent - pthread_create an agent for the given task */
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr)
{
	int retries = 0;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if (agent_arg_ptr == NULL)
		return;

	debug2("Spawning RPC retry agent for msg_type %u", 
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
	while (pthread_create(&thread_agent, &attr_agent,
			agent, (void *) agent_arg_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		sleep(1);	/* sleep and try again */
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
	if (retry_list == NULL)
		return;

	slurm_mutex_lock(&retry_mutex);
	list_destroy(retry_list);
	retry_list = NULL;
	slurm_mutex_unlock(&retry_mutex);
}

static void _purge_agent_args(agent_arg_t *agent_arg_ptr)
{
	if (agent_arg_ptr == NULL)
		return;

	xfree(agent_arg_ptr->slurm_addr);
	xfree(agent_arg_ptr->node_names);
	if (agent_arg_ptr->msg_args) {
		if (agent_arg_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH)
			_slurmctld_free_job_launch_msg(agent_arg_ptr->msg_args);
		else
			xfree(agent_arg_ptr->msg_args);
		}
	xfree(agent_arg_ptr);
}
