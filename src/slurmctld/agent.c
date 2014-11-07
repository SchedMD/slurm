/*****************************************************************************\
 *  agent.c - parallel background communication functions. This is where
 *	logic could be placed for broadcast communications.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  Derived from pdsh written by Jim Garlick <garlick1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include "src/common/forward.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/uid.h"
#include "src/common/xsignal.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/srun_comm.h"

#define MAX_RETRIES		100

typedef enum {
	DSH_NEW,        /* Request not yet started */
	DSH_ACTIVE,     /* Request in progress */
	DSH_DONE,       /* Request completed normally */
	DSH_NO_RESP,    /* Request timed out */
	DSH_FAILED      /* Request resulted in error */
} state_t;

typedef struct thd_complete {
	bool work_done; 	/* assume all threads complete */
	int fail_cnt;		/* assume no threads failures */
	int no_resp_cnt;	/* assume all threads respond */
	int retry_cnt;		/* assume no required retries */
	int max_delay;
	time_t now;
} thd_complete_t;

typedef struct thd {
	pthread_t thread;		/* thread ID */
	pthread_attr_t attr;		/* thread attributes */
	state_t state;			/* thread state */
	time_t start_time;		/* start time */
	time_t end_time;		/* end time or delta time
					 * upon termination */
	slurm_addr_t *addr;		/* specific addr to send to
					 * will not do nodelist if set */
	char *nodelist;			/* list of nodes to send to */
	List ret_list;
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
	uint16_t protocol_version;	/* if set, use this version */
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
	uint16_t protocol_version;	/* if set, use this version */
} task_info_t;

typedef struct queued_request {
	agent_arg_t* agent_arg_ptr;	/* The queued request */
	time_t       first_attempt;	/* Time of first check for batch
					 * launch RPC *only* */
	time_t       last_attempt;	/* Time of last xmit attempt */
} queued_request_t;

typedef struct mail_info {
	char *user_name;
	char *message;
} mail_info_t;

static void _sig_handler(int dummy);
static int  _batch_launch_defer(queued_request_t *queued_req_ptr);
static inline int _comm_err(char *node_name, slurm_msg_type_t msg_type);
static void _list_delete_retry(void *retry_entry);
static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr);
static task_info_t *_make_task_data(agent_info_t *agent_info_ptr, int inx);
static void _notify_slurmctld_jobs(agent_info_t *agent_ptr);
static void _notify_slurmctld_nodes(agent_info_t *agent_ptr,
		int no_resp_cnt, int retry_cnt);
static void _purge_agent_args(agent_arg_t *agent_arg_ptr);
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count);
static int _setup_requeue(agent_arg_t *agent_arg_ptr, thd_t *thread_ptr,
			  int count, int *spot);
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr);
static void *_thread_per_group_rpc(void *args);
static int   _valid_agent_arg(agent_arg_t *agent_arg_ptr);
static void *_wdog(void *args);

static mail_info_t *_mail_alloc(void);
static void  _mail_free(void *arg);
static void  _mail_proc(mail_info_t *mi);
static char *_mail_type_str(uint16_t mail_type);

static pthread_mutex_t retry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mail_mutex  = PTHREAD_MUTEX_INITIALIZER;
static List retry_list = NULL;		/* agent_arg_t list for retry */
static List mail_list = NULL;		/* pending e-mail requests */

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;

static bool run_scheduler    = false;
static bool wiki2_sched      = false;
static bool wiki2_sched_test = false;

/*
 * agent - party responsible for transmitting an common RPC in parallel
 *	across a set of nodes. Use agent_queue_request() if immediate
 *	execution is not essential.
 * IN pointer to agent_arg_t, which is xfree'd (including hostlist,
 *	and msg_args) upon completion
 * RET always NULL (function format just for use as pthread)
 */
void *agent(void *args)
{
	int i, delay, rc, retries = 0;
	pthread_attr_t attr_wdog;
	pthread_t thread_wdog = 0;
	agent_arg_t *agent_arg_ptr = args;
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr;
	task_info_t *task_specific_ptr;
	time_t begin_time;
	bool spawn_retry_agent = false;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "slurmctld_agent", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m",
		      __func__, "slurmctld_agent");
	}
#endif

#if 0
	info("Agent_cnt is %d of %d with msg_type %d",
	     agent_cnt, MAX_AGENT_CNT, agent_arg_ptr->msg_type);
#endif
	slurm_mutex_lock(&agent_cnt_mutex);
	if (!wiki2_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/wiki2") == 0)
			wiki2_sched = true;
		xfree(sched_type);
		wiki2_sched_test = true;
	}

	while (1) {
		if (slurmctld_config.shutdown_time ||
		    (agent_cnt < MAX_AGENT_CNT)) {
			agent_cnt++;
			break;
		} else {	/* wait for state change and retry */
			pthread_cond_wait(&agent_cnt_cond, &agent_cnt_mutex);
		}
	}
	slurm_mutex_unlock(&agent_cnt_mutex);
	if (slurmctld_config.shutdown_time)
		goto cleanup;

	/* basic argument value tests */
	begin_time = time(NULL);
	if (_valid_agent_arg(agent_arg_ptr))
		goto cleanup;

	/* initialize the agent data structures */
	agent_info_ptr = _make_agent_info(agent_arg_ptr);
	thread_ptr = agent_info_ptr->thread_struct;

	/* start the watchdog thread */
	slurm_attr_init(&attr_wdog);
	if (pthread_attr_setdetachstate
	    (&attr_wdog, PTHREAD_CREATE_JOINABLE))
		error("pthread_attr_setdetachstate error %m");
	while (pthread_create(&thread_wdog, &attr_wdog, _wdog,
			      (void *) agent_info_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		usleep(10000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_wdog);
#if 	AGENT_THREAD_COUNT < 1
	fatal("AGENT_THREAD_COUNT value is invalid");
#endif
	debug2("got %d threads to send out",agent_info_ptr->thread_count);
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
		 *      _thread_per_group_rpc() */
		task_specific_ptr = _make_task_data(agent_info_ptr, i);

		slurm_attr_init(&thread_ptr[i].attr);
		if (pthread_attr_setdetachstate(&thread_ptr[i].attr,
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		while ((rc = pthread_create(&thread_ptr[i].thread,
					    &thread_ptr[i].attr,
					    _thread_per_group_rpc,
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
				usleep(10000);	/* sleep and retry */
				slurm_mutex_lock(&agent_info_ptr->
						   thread_mutex);
			}
		}
		slurm_attr_destroy(&thread_ptr[i].attr);
		agent_info_ptr->threads_active++;
		slurm_mutex_unlock(&agent_info_ptr->thread_mutex);
	}

	/* Wait for termination of remaining threads */
	pthread_join(thread_wdog, NULL);
	delay = (int) difftime(time(NULL), begin_time);
	if (delay > (slurm_get_msg_timeout() * 2)) {
		info("agent msg_type=%u ran for %d seconds",
			agent_arg_ptr->msg_type,  delay);
	}
	slurm_mutex_lock(&agent_info_ptr->thread_mutex);
	while (agent_info_ptr->threads_active != 0) {
		pthread_cond_wait(&agent_info_ptr->thread_cond,
				&agent_info_ptr->thread_mutex);
	}
	slurm_mutex_unlock(&agent_info_ptr->thread_mutex);

      cleanup:
	_purge_agent_args(agent_arg_ptr);

	if (agent_info_ptr) {
		xfree(agent_info_ptr->thread_struct);
		xfree(agent_info_ptr);
	}
	slurm_mutex_lock(&agent_cnt_mutex);

	if (agent_cnt > 0)
		agent_cnt--;
	else {
		error("agent_cnt underflow");
		agent_cnt = 0;
	}

	if (agent_cnt && (agent_cnt < MAX_AGENT_CNT))
		spawn_retry_agent = true;

	pthread_cond_broadcast(&agent_cnt_cond);
	slurm_mutex_unlock(&agent_cnt_mutex);

	if (spawn_retry_agent)
		agent_retry(RPC_RETRY_INTERVAL, true);

	return NULL;
}

/* Basic validity test of agent argument */
static int _valid_agent_arg(agent_arg_t *agent_arg_ptr)
{
	xassert(agent_arg_ptr);
	xassert(agent_arg_ptr->hostlist);

	if (agent_arg_ptr->node_count == 0)
		return SLURM_FAILURE;	/* no messages to be sent */
	if (agent_arg_ptr->node_count
	    != hostlist_count(agent_arg_ptr->hostlist)) {
		error("you said you were going to send to %d "
		     "hosts but I only have %d",
		     agent_arg_ptr->node_count,
		     hostlist_count(agent_arg_ptr->hostlist));
		return SLURM_FAILURE;	/* no messages to be sent */
	}
	return SLURM_SUCCESS;
}

static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr)
{
	int i = 0, j = 0;
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr = NULL;
	int *span = NULL;
	int thr_count = 0;
	hostlist_t hl = NULL;
	char *name = NULL;

	agent_info_ptr = xmalloc(sizeof(agent_info_t));
	slurm_mutex_init(&agent_info_ptr->thread_mutex);
	if (pthread_cond_init(&agent_info_ptr->thread_cond, NULL))
		fatal("pthread_cond_init error %m");
	agent_info_ptr->thread_count   = agent_arg_ptr->node_count;
	agent_info_ptr->retry          = agent_arg_ptr->retry;
	agent_info_ptr->threads_active = 0;
	thread_ptr = xmalloc(agent_info_ptr->thread_count * sizeof(thd_t));
	memset(thread_ptr, 0, (agent_info_ptr->thread_count * sizeof(thd_t)));
	agent_info_ptr->thread_struct  = thread_ptr;
	agent_info_ptr->msg_type       = agent_arg_ptr->msg_type;
	agent_info_ptr->msg_args_pptr  = &agent_arg_ptr->msg_args;
	agent_info_ptr->protocol_version = agent_arg_ptr->protocol_version;

	if ((agent_arg_ptr->msg_type != REQUEST_JOB_NOTIFY)	&&
	    (agent_arg_ptr->msg_type != REQUEST_REBOOT_NODES)	&&
	    (agent_arg_ptr->msg_type != REQUEST_RECONFIGURE)	&&
	    (agent_arg_ptr->msg_type != REQUEST_SHUTDOWN)	&&
	    (agent_arg_ptr->msg_type != SRUN_EXEC)		&&
	    (agent_arg_ptr->msg_type != SRUN_TIMEOUT)		&&
	    (agent_arg_ptr->msg_type != SRUN_NODE_FAIL)		&&
	    (agent_arg_ptr->msg_type != SRUN_REQUEST_SUSPEND)	&&
	    (agent_arg_ptr->msg_type != SRUN_USER_MSG)		&&
	    (agent_arg_ptr->msg_type != SRUN_STEP_MISSING)	&&
	    (agent_arg_ptr->msg_type != SRUN_STEP_SIGNAL)	&&
	    (agent_arg_ptr->msg_type != SRUN_JOB_COMPLETE)) {
#ifdef HAVE_FRONT_END
		span = set_span(agent_arg_ptr->node_count,
				agent_arg_ptr->node_count);
#else
		/* Sending message to a possibly large number of slurmd.
		 * Push all message forwarding to slurmd in order to
		 * offload as much work from slurmctld as possible. */
		span = set_span(agent_arg_ptr->node_count, 1);
#endif
		agent_info_ptr->get_reply = true;
	} else {
		/* Message is going to one node (for srun) or we want
		 * it to get processed ASAP (SHUTDOWN or RECONFIGURE).
		 * Send the message directly to each node. */
		span = set_span(agent_arg_ptr->node_count,
				agent_arg_ptr->node_count);
	}
	i = 0;
	while(i < agent_info_ptr->thread_count) {
		thread_ptr[thr_count].state      = DSH_NEW;
		thread_ptr[thr_count].addr = agent_arg_ptr->addr;
		name = hostlist_shift(agent_arg_ptr->hostlist);
		if (!name) {
			debug3("no more nodes to send to");
			break;
		}
		hl = hostlist_create(name);
		if (thread_ptr[thr_count].addr && span[thr_count]) {
			debug("warning: you will only be sending this to %s",
			      name);
			span[thr_count] = 0;
		}
		free(name);
		i++;
		for (j = 0; j < span[thr_count]; j++) {
			name = hostlist_shift(agent_arg_ptr->hostlist);
			if (!name)
				break;
			hostlist_push_host(hl, name);
			free(name);
			i++;
		}
		hostlist_uniq(hl);
		thread_ptr[thr_count].nodelist =
			hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
#if 0
		info("sending msg_type %u to nodes %s",
		     agent_arg_ptr->msg_type, thread_ptr[thr_count].nodelist);
#endif
		thr_count++;
	}
	xfree(span);
	agent_info_ptr->thread_count = thr_count;
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
	task_info_ptr->protocol_version  = agent_info_ptr->protocol_version;

	return task_info_ptr;
}

static void _update_wdog_state(thd_t *thread_ptr,
			       state_t *state,
			       thd_complete_t *thd_comp)
{
	switch(*state) {
	case DSH_ACTIVE:
		thd_comp->work_done = false;
		if (thread_ptr->end_time <= thd_comp->now) {
			debug3("agent thread %lu timed out",
			       (unsigned long) thread_ptr->thread);
			if (pthread_kill(thread_ptr->thread, SIGUSR1) == ESRCH)
				*state = DSH_NO_RESP;
			else
				thread_ptr->end_time += COMMAND_TIMEOUT;
		}
		break;
	case DSH_NEW:
		thd_comp->work_done = false;
		break;
	case DSH_DONE:
		if (thd_comp->max_delay < (int)thread_ptr->end_time)
			thd_comp->max_delay = (int)thread_ptr->end_time;
		break;
	case DSH_NO_RESP:
		thd_comp->no_resp_cnt++;
		thd_comp->retry_cnt++;
		break;
	case DSH_FAILED:
		thd_comp->fail_cnt++;
		break;
	}
}

/*
 * _wdog - Watchdog thread. Send SIGUSR1 to threads which have been active
 *	for too long.
 * IN args - pointer to agent_info_t with info on threads to watch
 * Sleep between polls with exponential times (from 0.125 to 1.0 second)
 */
static void *_wdog(void *args)
{
	bool srun_agent = false;
	int i;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
	unsigned long usec = 5000;
	ListIterator itr;
	thd_complete_t thd_comp;
	ret_data_info_t *ret_data_info = NULL;

	if ( (agent_ptr->msg_type == SRUN_JOB_COMPLETE)			||
	     (agent_ptr->msg_type == SRUN_REQUEST_SUSPEND)		||
	     (agent_ptr->msg_type == SRUN_STEP_MISSING)			||
	     (agent_ptr->msg_type == SRUN_STEP_SIGNAL)			||
	     (agent_ptr->msg_type == SRUN_EXEC)				||
	     (agent_ptr->msg_type == SRUN_NODE_FAIL)			||
	     (agent_ptr->msg_type == SRUN_PING)				||
	     (agent_ptr->msg_type == SRUN_TIMEOUT)			||
	     (agent_ptr->msg_type == SRUN_USER_MSG)			||
	     (agent_ptr->msg_type == RESPONSE_RESOURCE_ALLOCATION) )
		srun_agent = true;

	thd_comp.max_delay = 0;

	while (1) {
		thd_comp.work_done   = true;/* assume all threads complete */
		thd_comp.fail_cnt    = 0;   /* assume no threads failures */
		thd_comp.no_resp_cnt = 0;   /* assume all threads respond */
		thd_comp.retry_cnt   = 0;   /* assume no required retries */
		thd_comp.now         = time(NULL);

		usleep(usec);
		usec = MIN((usec * 2), 1000000);

		slurm_mutex_lock(&agent_ptr->thread_mutex);
		for (i = 0; i < agent_ptr->thread_count; i++) {
			//info("thread name %s",thread_ptr[i].node_name);
			if (!thread_ptr[i].ret_list) {
				_update_wdog_state(&thread_ptr[i],
						   &thread_ptr[i].state,
						   &thd_comp);
			} else {
				itr = list_iterator_create(
					thread_ptr[i].ret_list);
				while((ret_data_info = list_next(itr))) {
					_update_wdog_state(&thread_ptr[i],
							   &ret_data_info->err,
							   &thd_comp);
				}
				list_iterator_destroy(itr);
			}
		}
		if (thd_comp.work_done)
			break;

		slurm_mutex_unlock(&agent_ptr->thread_mutex);
	}

	if (srun_agent) {
		_notify_slurmctld_jobs(agent_ptr);
	} else {
		_notify_slurmctld_nodes(agent_ptr,
					thd_comp.no_resp_cnt,
					thd_comp.retry_cnt);
	}

	for (i = 0; i < agent_ptr->thread_count; i++) {
		if (thread_ptr[i].ret_list)
			list_destroy(thread_ptr[i].ret_list);
		xfree(thread_ptr[i].nodelist);
	}

	if (thd_comp.max_delay)
		debug2("agent maximum delay %d seconds", thd_comp.max_delay);

	slurm_mutex_unlock(&agent_ptr->thread_mutex);
	return (void *) NULL;
}

static void _notify_slurmctld_jobs(agent_info_t *agent_ptr)
{
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock =
	    { NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uint32_t job_id = 0, step_id = 0;
	thd_t *thread_ptr = agent_ptr->thread_struct;

	if        (agent_ptr->msg_type == SRUN_PING) {
		srun_ping_msg_t *msg = *agent_ptr->msg_args_pptr;
		job_id  = msg->job_id;
		step_id = msg->step_id;
	} else if (agent_ptr->msg_type == SRUN_TIMEOUT) {
		srun_timeout_msg_t *msg = *agent_ptr->msg_args_pptr;
		job_id  = msg->job_id;
		step_id = msg->step_id;
	} else if (agent_ptr->msg_type == RESPONSE_RESOURCE_ALLOCATION) {
		resource_allocation_response_msg_t *msg =
			*agent_ptr->msg_args_pptr;
		job_id  = msg->job_id;
		step_id = NO_VAL;
	} else if ((agent_ptr->msg_type == SRUN_JOB_COMPLETE)		||
		   (agent_ptr->msg_type == SRUN_REQUEST_SUSPEND)	||
		   (agent_ptr->msg_type == SRUN_STEP_MISSING)		||
		   (agent_ptr->msg_type == SRUN_STEP_SIGNAL)		||
		   (agent_ptr->msg_type == SRUN_EXEC)			||
		   (agent_ptr->msg_type == SRUN_USER_MSG)) {
		return;		/* no need to note srun response */
	} else if (agent_ptr->msg_type == SRUN_NODE_FAIL) {
		return;		/* no need to note srun response */
	} else {
		error("_notify_slurmctld_jobs invalid msg_type %u",
			agent_ptr->msg_type);
		return;
	}
	lock_slurmctld(job_write_lock);
	if  (thread_ptr[0].state == DSH_DONE) {
		srun_response(job_id, step_id);
	}

	unlock_slurmctld(job_write_lock);
}

static void _notify_slurmctld_nodes(agent_info_t *agent_ptr,
				    int no_resp_cnt, int retry_cnt)
{
	ListIterator itr = NULL;
	ret_data_info_t *ret_data_info = NULL;
	state_t state;
	int is_ret_list = 1;
	/* Locks: Read config, write job, write node */
	slurmctld_lock_t node_write_lock =
	    { READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	thd_t *thread_ptr = agent_ptr->thread_struct;
	int i;

	/* Notify slurmctld of non-responding nodes */
	if (no_resp_cnt) {
		/* Update node table data for non-responding nodes */
		lock_slurmctld(node_write_lock);
		if (agent_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
			/* Requeue the request */
			batch_job_launch_msg_t *launch_msg_ptr =
					*agent_ptr->msg_args_pptr;
			uint32_t job_id = launch_msg_ptr->job_id;
			job_complete(job_id, 0, true, false, 0);
		}
		unlock_slurmctld(node_write_lock);
	}
	if (retry_cnt && agent_ptr->retry)
		_queue_agent_retry(agent_ptr, retry_cnt);

	/* Update last_response on responding nodes */
	lock_slurmctld(node_write_lock);
	for (i = 0; i < agent_ptr->thread_count; i++) {
		char *down_msg, *node_names;
		slurm_msg_type_t resp_type = RESPONSE_SLURM_RC;

		if (!thread_ptr[i].ret_list) {
			state = thread_ptr[i].state;
			is_ret_list = 0;
			goto switch_on_state;
		}
		is_ret_list = 1;

		itr = list_iterator_create(thread_ptr[i].ret_list);
		while ((ret_data_info = list_next(itr))) {
			state = ret_data_info->err;
		switch_on_state:
			if (is_ret_list) {
				node_names = ret_data_info->node_name;
				resp_type = ret_data_info->type;
			} else
				node_names = thread_ptr[i].nodelist;

			switch(state) {
			case DSH_NO_RESP:
				node_not_resp(node_names,
					      thread_ptr[i].start_time,
					      resp_type);
				break;
			case DSH_FAILED:
#ifdef HAVE_FRONT_END
				down_msg = "";
#else
				set_node_down(node_names,
					      "Prolog/Epilog failure");
				down_msg = ", set to state DOWN";
#endif
				error("Prolog/Epilog failure on nodes %s%s",
				      node_names, down_msg);
				break;
			case DSH_DONE:
				node_did_resp(node_names);
				break;
			default:
				error("unknown state returned for %s",
				      node_names);
				break;
			}
			if (!is_ret_list)
				goto finished;
		}
		list_iterator_destroy(itr);
finished:	;
	}
	unlock_slurmctld(node_write_lock);
	if (run_scheduler) {
		run_scheduler = false;
		/* below functions all have their own locking */
		if (schedule(0))	{
			schedule_job_save();
			schedule_node_save();
		}
	}
	if ((agent_ptr->msg_type == REQUEST_PING) ||
	    (agent_ptr->msg_type == REQUEST_HEALTH_CHECK) ||
	    (agent_ptr->msg_type == REQUEST_ACCT_GATHER_UPDATE) ||
	    (agent_ptr->msg_type == REQUEST_NODE_REGISTRATION_STATUS))
		ping_end();
}

/* Report a communications error for specified node
 * This also gets logged as a non-responsive node */
static inline int _comm_err(char *node_name, slurm_msg_type_t msg_type)
{
	int rc = 1;

	if ((rc = is_node_resp (node_name)))
		verbose("agent/is_node_resp: node:%s rpc:%d : %m",
			node_name, msg_type);
	return rc;
}

/* return a value for wihc WEXITSTATUS returns 1 */
static int _wif_status(void)
{
	static int rc = 0;
	int i;

	if (rc)
		return rc;

	rc = 1;
	for (i=0; i<64; i++) {
		if (WEXITSTATUS(rc))
			return rc;
		rc = rc << 1;
	}
	error("Could not identify WEXITSTATUS");
	rc = 1;
	return rc;
}

/*
 * _thread_per_group_rpc - thread to issue an RPC for a group of nodes
 *                         sending message out to one and forwarding it to
 *                         others if necessary.
 * IN/OUT args - pointer to task_info_t, xfree'd on completion
 */
static void *_thread_per_group_rpc(void *args)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t msg;
	task_info_t *task_ptr = (task_info_t *) args;
	/* we cache some pointers from task_info_t because we need
	 * to xfree args before being finished with their use. xfree
	 * is required for timely termination of this pthread because
	 * xfree could lock it at the end, preventing a timely
	 * thread_exit */
	pthread_mutex_t *thread_mutex_ptr   = task_ptr->thread_mutex_ptr;
	pthread_cond_t  *thread_cond_ptr    = task_ptr->thread_cond_ptr;
	uint32_t        *threads_active_ptr = task_ptr->threads_active_ptr;
	thd_t           *thread_ptr         = task_ptr->thread_struct_ptr;
	state_t thread_state = DSH_NO_RESP;
	slurm_msg_type_t msg_type = task_ptr->msg_type;
	bool is_kill_msg, srun_agent;
	List ret_list = NULL;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	int sig_array[2] = {SIGUSR1, 0};
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	/* Lock: Read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	/* Lock: Write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };

	xassert(args != NULL);
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sig_array);
	is_kill_msg = (	(msg_type == REQUEST_KILL_TIMELIMIT)	||
			(msg_type == REQUEST_KILL_PREEMPTED)	||
			(msg_type == REQUEST_TERMINATE_JOB) );
	srun_agent = (	(msg_type == SRUN_PING)			||
			(msg_type == SRUN_EXEC)			||
			(msg_type == SRUN_JOB_COMPLETE)		||
			(msg_type == SRUN_STEP_MISSING)		||
			(msg_type == SRUN_STEP_SIGNAL)		||
			(msg_type == SRUN_TIMEOUT)		||
			(msg_type == SRUN_USER_MSG)		||
			(msg_type == RESPONSE_RESOURCE_ALLOCATION) ||
			(msg_type == SRUN_NODE_FAIL) );

	thread_ptr->start_time = time(NULL);

	slurm_mutex_lock(thread_mutex_ptr);
	thread_ptr->state = DSH_ACTIVE;
	thread_ptr->end_time = thread_ptr->start_time + COMMAND_TIMEOUT;
	slurm_mutex_unlock(thread_mutex_ptr);

	/* send request message */
	slurm_msg_t_init(&msg);

	if (task_ptr->protocol_version)
		msg.protocol_version = task_ptr->protocol_version;

	msg.msg_type = msg_type;
	msg.data     = task_ptr->msg_args_ptr;
#if 0
 	info("sending message type %u to %s", msg_type, thread_ptr->nodelist);
#endif
	if (task_ptr->get_reply) {
		if (thread_ptr->addr) {
			msg.address = *thread_ptr->addr;

			if (!(ret_list = slurm_send_addr_recv_msgs(
				     &msg, thread_ptr->nodelist, 0))) {
				error("_thread_per_group_rpc: "
				      "no ret_list given");
				goto cleanup;
			}


		} else {
			if (!(ret_list = slurm_send_recv_msgs(
				     thread_ptr->nodelist,
				     &msg, 0, true))) {
				error("_thread_per_group_rpc: "
				      "no ret_list given");
				goto cleanup;
			}
		}
	} else {
		if (thread_ptr->addr) {
			//info("got the address");
			msg.address = *thread_ptr->addr;
		} else {
			//info("no address given");
			if (slurm_conf_get_addr(thread_ptr->nodelist,
					       &msg.address) == SLURM_ERROR) {
				error("_thread_per_group_rpc: "
				      "can't find address for host %s, "
				      "check slurm.conf",
				      thread_ptr->nodelist);
				goto cleanup;
			}
		}
		//info("sending %u to %s", msg_type, thread_ptr->nodelist);
		if (slurm_send_only_node_msg(&msg) == SLURM_SUCCESS) {
			thread_state = DSH_DONE;
		} else {
			if (!srun_agent) {
				lock_slurmctld(node_read_lock);
				_comm_err(thread_ptr->nodelist, msg_type);
				unlock_slurmctld(node_read_lock);
			}
		}
		goto cleanup;
	}

	//info("got %d messages back", list_count(ret_list));
	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr)) != NULL) {
		rc = slurm_get_return_code(ret_data_info->type,
					   ret_data_info->data);
		/* SPECIAL CASE: Record node's CPU load */
		if (ret_data_info->type == RESPONSE_PING_SLURMD) {
			ping_slurmd_resp_msg_t *ping_resp;
			ping_resp = (ping_slurmd_resp_msg_t *)
				    ret_data_info->data;
			lock_slurmctld(node_write_lock);
			reset_node_load(ret_data_info->node_name,
					ping_resp->cpu_load);
			unlock_slurmctld(node_write_lock);
		}
		/* SPECIAL CASE: Mark node as IDLE if job already complete */
		if (is_kill_msg &&
		    (rc == ESLURMD_KILL_JOB_ALREADY_COMPLETE)) {
			kill_job_msg_t *kill_job;
			kill_job = (kill_job_msg_t *)
				task_ptr->msg_args_ptr;
			rc = SLURM_SUCCESS;
			lock_slurmctld(job_write_lock);
			if (job_epilog_complete(kill_job->job_id,
						ret_data_info->
						node_name,
						rc))
				run_scheduler = true;
			unlock_slurmctld(job_write_lock);
		}

		/* SPECIAL CASE: Record node's CPU load */
		if (ret_data_info->type == RESPONSE_ACCT_GATHER_UPDATE) {
			lock_slurmctld(node_write_lock);
			update_node_record_acct_gather_data(
				ret_data_info->data);
			unlock_slurmctld(node_write_lock);
		}

		/* SPECIAL CASE: Kill non-startable batch job,
		 * Requeue the job on ESLURMD_PROLOG_FAILED */
		if ((msg_type == REQUEST_BATCH_JOB_LAUNCH) &&
		    (rc != SLURM_SUCCESS) && (rc != ESLURMD_PROLOG_FAILED) &&
		    (ret_data_info->type != RESPONSE_FORWARD_FAILED)) {
			batch_job_launch_msg_t *launch_msg_ptr =
				task_ptr->msg_args_ptr;
			uint32_t job_id = launch_msg_ptr->job_id;
			info("Killing non-startable batch job %u: %s",
			     job_id, slurm_strerror(rc));
			thread_state = DSH_DONE;
			ret_data_info->err = thread_state;
			lock_slurmctld(job_write_lock);
			job_complete(job_id, 0, false, false, _wif_status());
			unlock_slurmctld(job_write_lock);
			continue;
		}

		if (((msg_type == REQUEST_SIGNAL_TASKS) ||
		     (msg_type == REQUEST_TERMINATE_TASKS)) &&
		     (rc == ESRCH)) {
			/* process is already dead, not a real error */
			rc = SLURM_SUCCESS;
		}

		switch (rc) {
		case SLURM_SUCCESS:
			/* debug("agent processed RPC to node %s", */
			/*       ret_data_info->node_name); */
			thread_state = DSH_DONE;
			break;
		case SLURM_UNKNOWN_FORWARD_ADDR:
			error("We were unable to forward message to '%s'.  "
			      "Make sure the slurm.conf for each slurmd "
			      "contain all other nodes in your system.",
			      ret_data_info->node_name);
			thread_state = DSH_NO_RESP;
			break;
		case ESLURMD_EPILOG_FAILED:
			error("Epilog failure on host %s, "
			      "setting DOWN",
			      ret_data_info->node_name);

			thread_state = DSH_FAILED;
			break;
		case ESLURMD_PROLOG_FAILED:
			thread_state = DSH_FAILED;
			break;
		case ESLURM_INVALID_JOB_ID:
			/* Not indicative of a real error */
		case ESLURMD_JOB_NOTRUNNING:
			/* Not indicative of a real error */
			debug2("agent processed RPC to node %s: %s",
			       ret_data_info->node_name,
			       slurm_strerror(rc));

			thread_state = DSH_DONE;
			break;
		default:
			if (!srun_agent) {
				if (ret_data_info->err)
					errno = ret_data_info->err;
				else
					errno = rc;
				lock_slurmctld(node_read_lock);
				rc = _comm_err(ret_data_info->node_name,
					       msg_type);
				unlock_slurmctld(node_read_lock);
			}

			if (srun_agent)
				thread_state = DSH_FAILED;
			else if (rc || (ret_data_info->type ==
					RESPONSE_FORWARD_FAILED))
				/* check if a forward failed */
				thread_state = DSH_NO_RESP;
			else {	/* some will fail that don't mean anything went
				 * bad like a job term request on a job that is
				 * already finished, we will just exit on those
				 * cases */
				thread_state = DSH_DONE;
			}
		}
		ret_data_info->err = thread_state;
	}
	list_iterator_destroy(itr);

cleanup:
	xfree(args);

	/* handled at end of thread just in case resend is needed */
	destroy_forward(&msg.forward);
	slurm_mutex_lock(thread_mutex_ptr);
	thread_ptr->ret_list = ret_list;
	thread_ptr->state = thread_state;
	thread_ptr->end_time = (time_t) difftime(time(NULL),
						 thread_ptr->start_time);
	/* Signal completion so another thread can replace us */
	(*threads_active_ptr)--;
	pthread_cond_signal(thread_cond_ptr);
	slurm_mutex_unlock(thread_mutex_ptr);
	return (void *) NULL;
}

/*
 * Signal handler.  We are really interested in interrupting hung communictions
 * and causing them to return EINTR. Multiple interupts might be required.
 */
static void _sig_handler(int dummy)
{
}

static int _setup_requeue(agent_arg_t *agent_arg_ptr, thd_t *thread_ptr,
			  int count, int *spot)
{
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr = list_iterator_create(thread_ptr->ret_list);
	while((ret_data_info = list_next(itr))) {
		debug2("got err of %d", ret_data_info->err);
		if (ret_data_info->err != DSH_NO_RESP)
			continue;

		debug("got the name %s to resend out of %d",
		      ret_data_info->node_name, count);

		if (agent_arg_ptr) {
			hostlist_push_host(agent_arg_ptr->hostlist,
				      ret_data_info->node_name);

			if ((++(*spot)) == count) {
				list_iterator_destroy(itr);
				return 1;
			}
		}
	}
	list_iterator_destroy(itr);
	return 0;
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
	agent_arg_ptr->hostlist = hostlist_create(NULL);
	agent_arg_ptr->msg_type = agent_info_ptr->msg_type;
	agent_arg_ptr->msg_args = *(agent_info_ptr->msg_args_pptr);
	*(agent_info_ptr->msg_args_pptr) = NULL;

	j = 0;
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		if (!thread_ptr[i].ret_list) {
			if (thread_ptr[i].state != DSH_NO_RESP)
				continue;

			debug("got the name %s to resend",
			      thread_ptr[i].nodelist);
			hostlist_push_host(agent_arg_ptr->hostlist,
				      thread_ptr[i].nodelist);

			if ((++j) == count)
				break;
		} else {
			if (_setup_requeue(agent_arg_ptr, &thread_ptr[i],
					  count, &j))
				break;
		}
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
		retry_list = list_create(_list_delete_retry);
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
 * IN mai_too - Send pending email too, note this performed using a
 *	fork/waitpid, so it can take longer than just creating  a pthread
 *	to send RPCs
 * RET count of queued requests remaining
 */
extern int agent_retry (int min_wait, bool mail_too)
{
	int list_size = 0, rc;
	time_t now = time(NULL);
	queued_request_t *queued_req_ptr = NULL;
	agent_arg_t *agent_arg_ptr = NULL;
	ListIterator retry_iter;

	slurm_mutex_lock(&retry_mutex);
	if (retry_list) {
		static time_t last_msg_time = (time_t) 0;
		uint32_t msg_type[5] = {0, 0, 0, 0, 0}, i = 0;
		list_size = list_count(retry_list);
		if ((list_size > MAX_AGENT_CNT) &&
		    (difftime(now, last_msg_time) > 300)) {
			/* Note sizable backlog of work */
			info("WARNING: agent retry_list size is %d",
				list_size);
			retry_iter = list_iterator_create(retry_list);
			while ((queued_req_ptr = (queued_request_t *)
					list_next(retry_iter))) {
				agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
				msg_type[i++] = agent_arg_ptr->msg_type;
				if (i == 5)
					break;
			}
			list_iterator_destroy(retry_iter);
			info("   retry_list msg_type=%u,%u,%u,%u,%u",
				msg_type[0], msg_type[1], msg_type[2],
				msg_type[3], msg_type[4]);
			last_msg_time = now;
		}
	}

	slurm_mutex_lock(&agent_cnt_mutex);
	if (agent_cnt >= MAX_AGENT_CNT) {	/* too much work already */
		slurm_mutex_unlock(&agent_cnt_mutex);
		slurm_mutex_unlock(&retry_mutex);
		return list_size;
	}
	slurm_mutex_unlock(&agent_cnt_mutex);

	if (retry_list) {
		/* first try to find a new (never tried) record */

		retry_iter = list_iterator_create(retry_list);
		while ((queued_req_ptr = (queued_request_t *)
				list_next(retry_iter))) {
			rc = _batch_launch_defer(queued_req_ptr);
			if (rc == -1) {		/* abort request */
				_purge_agent_args(queued_req_ptr->
						  agent_arg_ptr);
				xfree(queued_req_ptr);
				list_remove(retry_iter);
				list_size--;
				continue;
			}
			if (rc > 0)
				continue;
 			if (queued_req_ptr->last_attempt == 0) {
				list_remove(retry_iter);
				list_size--;
				break;
			}
		}
		list_iterator_destroy(retry_iter);
	}

	if (retry_list && (queued_req_ptr == NULL)) {
		/* now try to find a requeue request that is
		 * relatively old */
		double age = 0;

		retry_iter = list_iterator_create(retry_list);
		/* next try to find an older record to retry */
		while ((queued_req_ptr = (queued_request_t *)
				list_next(retry_iter))) {
			rc = _batch_launch_defer(queued_req_ptr);
			if (rc == -1) { 	/* abort request */
				_purge_agent_args(queued_req_ptr->
						  agent_arg_ptr);
				xfree(queued_req_ptr);
				list_remove(retry_iter);
				list_size--;
				continue;
			}
			if (rc > 0)
				continue;
			age = difftime(now, queued_req_ptr->last_attempt);
			if (age > min_wait) {
				list_remove(retry_iter);
				list_size--;
				break;
			}
		}
		list_iterator_destroy(retry_iter);
	}
	slurm_mutex_unlock(&retry_mutex);

	if (queued_req_ptr) {
		agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
		xfree(queued_req_ptr);
		if (agent_arg_ptr) {
			_spawn_retry_agent(agent_arg_ptr);
		} else
			error("agent_retry found record with no agent_args");
	} else if (mail_too) {
		mail_info_t *mi = NULL;
		slurm_mutex_lock(&mail_mutex);
		if (mail_list)
			mi = (mail_info_t *) list_dequeue(mail_list);
		slurm_mutex_unlock(&mail_mutex);
		if (mi)
			_mail_proc(mi);
	}

	return list_size;
}

/*
 * agent_queue_request - put a new request on the queue for execution or
 * 	execute now if not too busy
 * IN agent_arg_ptr - the request to enqueue
 */
void agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	queued_request_t *queued_req_ptr = NULL;

	if (agent_arg_ptr->msg_type == REQUEST_SHUTDOWN) {
		/* execute now */
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		int rc;
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
				(&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		rc = pthread_create(&thread_agent, &attr_agent,
				    agent, (void *) agent_arg_ptr);
		slurm_attr_destroy(&attr_agent);
		if (rc == 0) {
			usleep(10000);	/* give agent a chance to start */
			return;
		}
	}

	queued_req_ptr = xmalloc(sizeof(queued_request_t));
	queued_req_ptr->agent_arg_ptr = agent_arg_ptr;
/*	queued_req_ptr->last_attempt  = 0; Implicit */

	slurm_mutex_lock(&retry_mutex);

	if (retry_list == NULL) {
		retry_list = list_create(_list_delete_retry);
		if (retry_list == NULL)
			fatal("list_create failed");
	}
	list_append(retry_list, (void *)queued_req_ptr);
	slurm_mutex_unlock(&retry_mutex);

	/* now process the request in a separate pthread
	 * (if we can create another pthread to do so) */
	agent_retry(999, false);
}

/* _spawn_retry_agent - pthread_create an agent for the given task */
static void _spawn_retry_agent(agent_arg_t * agent_arg_ptr)
{
	int retries = 0;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;

	if (agent_arg_ptr == NULL)
		return;

	debug2("Spawning RPC agent for msg_type %s",
	       rpc_num2string(agent_arg_ptr->msg_type));
	slurm_attr_init(&attr_agent);
	if (pthread_attr_setdetachstate(&attr_agent,
					PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	while (pthread_create(&thread_agent, &attr_agent,
			agent, (void *) agent_arg_ptr)) {
		error("pthread_create error %m");
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		usleep(10000);	/* sleep and retry */
	}
	slurm_attr_destroy(&attr_agent);
}

/* slurmctld_free_batch_job_launch_msg is a variant of
 *	slurm_free_job_launch_msg because all environment variables currently
 *	loaded in one xmalloc buffer (see get_job_env()), which is different
 *	from how slurmd assembles the data from a message
 */
extern void slurmctld_free_batch_job_launch_msg(batch_job_launch_msg_t * msg)
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
	if (retry_list) {
		slurm_mutex_lock(&retry_mutex);
		list_destroy(retry_list);
		retry_list = NULL;
		slurm_mutex_unlock(&retry_mutex);
	}
	if (mail_list) {
		slurm_mutex_lock(&mail_mutex);
		list_destroy(mail_list);
		mail_list = NULL;
		slurm_mutex_unlock(&mail_mutex);
	}
}
extern int get_agent_count(void)
{
	int cnt;

	slurm_mutex_lock(&agent_cnt_mutex);
	cnt = agent_cnt;
	slurm_mutex_unlock(&agent_cnt_mutex);

	return cnt;
}

static void _purge_agent_args(agent_arg_t *agent_arg_ptr)
{
	if (agent_arg_ptr == NULL)
		return;

	hostlist_destroy(agent_arg_ptr->hostlist);
	xfree(agent_arg_ptr->addr);
	if (agent_arg_ptr->msg_args) {
		if (agent_arg_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH)
			slurmctld_free_batch_job_launch_msg(agent_arg_ptr->
							    msg_args);
		else if (agent_arg_ptr->msg_type ==
				RESPONSE_RESOURCE_ALLOCATION)
			slurm_free_resource_allocation_response_msg(
					agent_arg_ptr->msg_args);
		else if ((agent_arg_ptr->msg_type == REQUEST_ABORT_JOB)      ||
			 (agent_arg_ptr->msg_type == REQUEST_TERMINATE_JOB)  ||
			 (agent_arg_ptr->msg_type == REQUEST_KILL_PREEMPTED) ||
			 (agent_arg_ptr->msg_type == REQUEST_KILL_TIMELIMIT))
			slurm_free_kill_job_msg(agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == SRUN_USER_MSG)
			slurm_free_srun_user_msg(agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == SRUN_EXEC)
			slurm_free_srun_exec_msg(agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == SRUN_NODE_FAIL)
			slurm_free_srun_node_fail_msg(agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == SRUN_STEP_MISSING)
			slurm_free_srun_step_missing_msg(
				agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == SRUN_STEP_SIGNAL)
			slurm_free_job_step_kill_msg(
				agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == REQUEST_JOB_NOTIFY)
			slurm_free_job_notify_msg(agent_arg_ptr->msg_args);
		else if (agent_arg_ptr->msg_type == REQUEST_SUSPEND_INT)
			slurm_free_suspend_int_msg(agent_arg_ptr->msg_args);
		else
			xfree(agent_arg_ptr->msg_args);
	}
	xfree(agent_arg_ptr);
}

static mail_info_t *_mail_alloc(void)
{
	return xmalloc(sizeof(mail_info_t));
}

static void _mail_free(void *arg)
{
	mail_info_t *mi = (mail_info_t *) arg;

	if (mi) {
		xfree(mi->user_name);
		xfree(mi->message);
		xfree(mi);
	}
}

/* process an email request and free the record */
static void _mail_proc(mail_info_t *mi)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {		/* error */
		error("fork(): %m");
	} else if (pid == 0) {	/* child */
		int fd, i;
		for (i = 0; i < 1024; i++)
			(void) close(i);
		fd = open("/dev/null", O_RDWR); // fd = 0
		if (dup(fd) == -1)		// fd = 1
			error("Couldn't do a dup for 1: %m");
		if (dup(fd) == -1)		// fd = 2
			error("Couldn't do a dup for 2 %m");
		execle(slurmctld_conf.mail_prog, "mail",
			"-s", mi->message, mi->user_name,
			NULL, NULL);
		error("Failed to exec %s: %m",
			slurmctld_conf.mail_prog);
		exit(1);
	} else {		/* parent */
		waitpid(pid, NULL, 0);
	}
	_mail_free(mi);
	return;
}

static char *_mail_type_str(uint16_t mail_type)
{
	if (mail_type == MAIL_JOB_BEGIN)
		return "Began";
	if (mail_type == MAIL_JOB_END)
		return "Ended";
	if (mail_type == MAIL_JOB_FAIL)
		return "Failed";
	if (mail_type == MAIL_JOB_REQUEUE)
		return "Requeued";
	if (mail_type == MAIL_JOB_TIME100)
		return "Reached time limit";
	if (mail_type == MAIL_JOB_TIME90)
		return "Reached 90% of time limit";
	if (mail_type == MAIL_JOB_TIME80)
		return "Reached 80% of time limit";
	if (mail_type == MAIL_JOB_TIME50)
		return "Reached 50% of time limit";
	return "unknown";
}

static void _set_job_time(struct job_record *job_ptr, uint16_t mail_type,
			  char *buf, int buf_len)
{
	time_t interval = NO_VAL;

	buf[0] = '\0';
	if ((mail_type == MAIL_JOB_BEGIN) && job_ptr->start_time &&
	    job_ptr->details && job_ptr->details->submit_time) {
		interval = job_ptr->start_time - job_ptr->details->submit_time;
		snprintf(buf, buf_len, ", Queued time ");
		secs2time_str(interval, buf+14, buf_len-14);
		return;
	}

	if (((mail_type == MAIL_JOB_END) || (mail_type == MAIL_JOB_FAIL) ||
	     (mail_type == MAIL_JOB_REQUEUE)) &&
	    (job_ptr->start_time && job_ptr->end_time)) {
		if (job_ptr->suspend_time) {
			interval  = job_ptr->end_time - job_ptr->suspend_time;
			interval += job_ptr->pre_sus_time;
		} else
			interval = job_ptr->end_time - job_ptr->start_time;
		snprintf(buf, buf_len, ", Run time ");
		secs2time_str(interval, buf+11, buf_len-11);
		return;
	}

	if (((mail_type == MAIL_JOB_TIME100) ||
	     (mail_type == MAIL_JOB_TIME90)  ||
	     (mail_type == MAIL_JOB_TIME80)  ||
	     (mail_type == MAIL_JOB_TIME50)) && job_ptr->start_time) {
		if (job_ptr->suspend_time) {
			interval  = time(NULL) - job_ptr->suspend_time;
			interval += job_ptr->pre_sus_time;
		} else
			interval = time(NULL) - job_ptr->start_time;
		snprintf(buf, buf_len, ", Run time ");
		secs2time_str(interval, buf+11, buf_len-11);
	}
}

static void _set_job_term_info(struct job_record *job_ptr, uint16_t mail_type,
			       char *buf, int buf_len)
{
	uint16_t base_state = job_ptr->job_state & JOB_STATE_BASE;

	buf[0] = '\0';
	if ((mail_type == MAIL_JOB_END) || (mail_type == MAIL_JOB_FAIL)) {
		if (WIFEXITED(job_ptr->exit_code)) {
			int exit_code = WEXITSTATUS(job_ptr->exit_code);
			snprintf(buf, buf_len, ", %s, ExitCode %d",
				 job_state_string(base_state), exit_code);
		} else {
			snprintf(buf, buf_len, ", %s",
				 job_state_string(base_state));
		}
	} else if (buf_len > 0) {
		buf[0] = '\0';
	}
}

/*
 * mail_job_info - Send e-mail notice of job state change
 * IN job_ptr - job identification
 * IN state_type - job transition type, see MAIL_JOB in slurm.h
 */
extern void mail_job_info (struct job_record *job_ptr, uint16_t mail_type)
{
	char job_time[128], term_msg[128];
	mail_info_t *mi = _mail_alloc();

	if (!job_ptr->mail_user)
		mi->user_name = uid_to_string((uid_t)job_ptr->user_id);
	else
		mi->user_name = xstrdup(job_ptr->mail_user);

	_set_job_time(job_ptr, mail_type, job_time, sizeof(job_time));
	_set_job_term_info(job_ptr, mail_type, term_msg, sizeof(term_msg));
	if (job_ptr->array_task_id != NO_VAL) {
		mi->message = xstrdup_printf("SLURM Job_id=%u_%u (%u) Name=%s "
					     "%s%s%s",
					     job_ptr->array_job_id,
					     job_ptr->array_task_id,
					     job_ptr->job_id, job_ptr->name,
					     _mail_type_str(mail_type),
					     job_time, term_msg);
	} else {
		mi->message = xstrdup_printf("SLURM Job_id=%u Name=%s %s%s%s",
					     job_ptr->job_id, job_ptr->name,
					     _mail_type_str(mail_type),
					     job_time, term_msg);
	}
	info("email msg to %s: %s", mi->user_name, mi->message);

	slurm_mutex_lock(&mail_mutex);
	if (!mail_list) {
		mail_list = list_create(_mail_free);
		if (!mail_list)
			fatal("list_create failed");
	}
	(void) list_enqueue(mail_list, (void *) mi);
	slurm_mutex_unlock(&mail_mutex);
	return;
}

/* Test if a batch launch request should be defered
 * RET -1: abort the request, pending job cancelled
 *      0: execute the request now
 *      1: defer the request
 */
static int _batch_launch_defer(queued_request_t *queued_req_ptr)
{
	agent_arg_t *agent_arg_ptr;
	batch_job_launch_msg_t *launch_msg_ptr;
	time_t now = time(NULL);
	struct job_record  *job_ptr;
	int delay_time, nodes_ready = 0, tmp;

	agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
	if (agent_arg_ptr->msg_type != REQUEST_BATCH_JOB_LAUNCH)
		return 0;

	if (difftime(now, queued_req_ptr->last_attempt) < 10) {
		/* Reduce overhead by only testing once every 10 secs */
		return 1;
	}

	launch_msg_ptr = (batch_job_launch_msg_t *)agent_arg_ptr->msg_args;
	job_ptr = find_job_record(launch_msg_ptr->job_id);
	if ((job_ptr == NULL) ||
	    (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) {
		info("agent(batch_launch): removed pending request for "
		     "cancelled job %u",
		     launch_msg_ptr->job_id);
		return -1;	/* job cancelled while waiting */
	}

	if (job_ptr->wait_all_nodes) {
		(void) job_node_ready(launch_msg_ptr->job_id, &tmp);
		if (tmp == (READY_JOB_STATE | READY_NODE_STATE)) {
			nodes_ready = 1;
			if (launch_msg_ptr->alias_list &&
			    !strcmp(launch_msg_ptr->alias_list, "TBD")) {
				/* Update launch RPC with correct node
				 * aliases */
				struct job_record *job_ptr;
				job_ptr = find_job_record(launch_msg_ptr->
							  job_id);
				xfree(launch_msg_ptr->alias_list);
				launch_msg_ptr->alias_list = xstrdup(job_ptr->
								     alias_list);
			}
		}
	} else {
#ifdef HAVE_FRONT_END
		nodes_ready = 1;
#else
		struct node_record *node_ptr;
		char *hostname;

		hostname = hostlist_deranged_string_xmalloc(
					agent_arg_ptr->hostlist);
		node_ptr = find_node_record(hostname);
		if (node_ptr == NULL) {
			error("agent(batch_launch) removed pending request for "
			      "job %u, missing node %s",
			      launch_msg_ptr->job_id, hostname);
			xfree(hostname);
			return -1;	/* invalid request?? */
		}
		xfree(hostname);
		if (!IS_NODE_POWER_SAVE(node_ptr) &&
		    !IS_NODE_NO_RESPOND(node_ptr)) {
			nodes_ready = 1;
		}
#endif
	}

	delay_time = difftime(now, job_ptr->start_time);
	if (nodes_ready) {
		/* ready to launch, adjust time limit for boot time */
		if (delay_time && (job_ptr->time_limit != INFINITE) &&
		    (!wiki2_sched)) {
			verbose("Job %u launch delayed by %d secs, "
				"updating end_time",
				launch_msg_ptr->job_id, delay_time);
			job_ptr->end_time += delay_time;
		}
		queued_req_ptr->last_attempt = (time_t) 0;
		return 0;
	}

	if (queued_req_ptr->last_attempt == 0) {
		queued_req_ptr->first_attempt = now;
		queued_req_ptr->last_attempt  = now;
	} else if (difftime(now, queued_req_ptr->first_attempt) >=
				 slurm_get_resume_timeout()) {
		error("agent waited too long for nodes to respond, "
		      "sending batch request anyway...");
		if (delay_time && (job_ptr->time_limit != INFINITE) &&
		    (!wiki2_sched)) {
			verbose("Job %u launch delayed by %d secs, "
				"updating end_time",
				launch_msg_ptr->job_id, delay_time);
			job_ptr->end_time += delay_time;
		}
		queued_req_ptr->last_attempt = (time_t) 0;
		return 0;
	}

	queued_req_ptr->last_attempt  = now;
	return 1;
}

/* Return length of agent's retry_list */
extern int retry_list_size(void)
{
	if (retry_list == NULL)
		return 0;
	return list_count(retry_list);
}
