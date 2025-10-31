/*****************************************************************************\
 *  agent.c - parallel background communication functions. This is where
 *	logic could be placed for broadcast communications.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  Derived from pdsh written by Jim Garlick <garlick1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
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
 *  for more than MessageTimeout seconds.
 *  The agent responds to slurmctld via a function call or an RPC as required.
 *  For example, informing slurmctld that some node is not responding.
 *
 *  All the state for each thread is maintained in thd_t struct, which is
 *  used by the watchdog thread as well as the communication threads.
\*****************************************************************************/

#include "config.h"

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/run_command.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/select.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/sackd_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/state_save.h"

#include "src/stepmgr/srun_comm.h"

#define MAX_RETRIES		100
#define MAX_RPC_PACK_CNT	100
#define RPC_PACK_MAX_AGE	1	/* Rebuild data over 1 seconds old */
#define DUMP_RPC_COUNT 		25
#define HOSTLIST_MAX_SIZE 	80
#define MAIL_PROG_TIMEOUT 120 /* Timeout in seconds */
#define AGENT_SHUTDOWN_WAIT 3

typedef enum {
	DSH_NEW,        /* Request not yet started */
	DSH_ACTIVE,     /* Request in progress */
	DSH_DONE,       /* Request completed normally */
	DSH_NO_RESP,    /* Request timed out */
	DSH_FAILED,     /* Request resulted in error */
	DSH_DUP_JOBID	/* Request resulted in duplicate job ID error */
} state_t;

typedef struct {
	bool work_done; 	/* assume all threads complete */
	int fail_cnt;		/* assume no threads failures */
	int no_resp_cnt;	/* assume all threads respond */
	int retry_cnt;		/* assume no required retries */
	int max_delay;
	time_t now;
} thd_complete_t;

typedef struct {
	pthread_t thread;		/* thread ID */
	state_t state;			/* thread state */
	time_t start_time;		/* start time */
	time_t end_time;		/* end time or delta time
					 * upon termination */
	slurm_addr_t *addr;		/* specific addr to send to
					 * will not do nodelist if set */
	hostlist_t *nodelist;		/* list of nodes to send to */
	char *nodename;			/* node to send to */
	list_t *ret_list;
} thd_t;

typedef struct {
	pthread_mutex_t thread_mutex;	/* agent specific mutex */
	pthread_cond_t thread_cond;	/* agent specific condition */
	uint32_t thread_count;		/* number of threads records */
	uint32_t threads_active;	/* currently active threads */
	uint16_t retry;			/* if set, keep trying */
	thd_t *thread_struct;		/* thread structures */
	bool get_reply;			/* flag if reply expected */
	uid_t r_uid;			/* receiver UID */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void **msg_args_pptr;		/* RPC data to be used */
	uint16_t msg_flags;		/* Flags to be added to msg*/
	uint16_t protocol_version;	/* if set, use this version */
	char *tls_cert;
} agent_info_t;

typedef struct {
	pthread_mutex_t *thread_mutex_ptr; /* pointer to agent specific
					    * mutex */
	pthread_cond_t *thread_cond_ptr;/* pointer to agent specific
					 * condition */
	uint32_t *threads_active_ptr;	/* currently active thread ptr */
	thd_t *thread_struct_ptr;	/* thread structures ptr */
	bool get_reply;			/* flag if reply expected */
	uid_t r_uid;			/* receiver UID */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void *msg_args_ptr;		/* ptr to RPC data to be used */
	uint16_t msg_flags;		/* Flags to be added to msg*/
	uint16_t protocol_version;	/* if set, use this version */
	char *tls_cert;
} task_info_t;

typedef struct {
	agent_arg_t* agent_arg_ptr;	/* The queued request */
	time_t       first_attempt;	/* Time of first check for batch
					 * launch RPC *only* */
	time_t       last_attempt;	/* Time of last xmit attempt */
} queued_request_t;

typedef struct {
	char *user_name;
	char *message;
	char **environment; /* MailProg environment variables */
} mail_info_t;

static void _agent_defer(void);
static void _agent_retry(int min_wait, bool wait_too);
static int  _batch_launch_defer(queued_request_t *queued_req_ptr);
static void _reboot_from_ctld(agent_arg_t *agent_arg_ptr);
static int  _signal_defer(queued_request_t *queued_req_ptr);
static inline int _comm_err(char *node_name, slurm_msg_type_t msg_type);
static void _list_delete_retry(void *retry_entry);
static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr);
static task_info_t *_make_task_data(agent_info_t *agent_info_ptr, int inx);
static void _notify_slurmctld_jobs(agent_info_t *agent_ptr);
static void _notify_slurmctld_nodes(agent_info_t *agent_ptr,
		int no_resp_cnt, int retry_cnt);
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count);
static void _queue_update_node(char *node_name);
static void _queue_update_srun(slurm_step_id_t *step_id);
static int  _setup_requeue(agent_arg_t *agent_arg_ptr, thd_t *thread_ptr,
			   int *count, int *spot);
static void *_thread_per_group_rpc(void *args);
static int   _valid_agent_arg(agent_arg_t *agent_arg_ptr);
static void *_wdog(void *args);

static mail_info_t *_mail_alloc(void);
static void  _mail_free(void *arg);
static void *_mail_proc(void *arg);
static char *_mail_type_str(uint16_t mail_type);
static char **_build_mail_env(job_record_t *job_ptr, uint32_t mail_type);

static pthread_mutex_t defer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mail_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t retry_mutex = PTHREAD_MUTEX_INITIALIZER;
static list_t *defer_list = NULL;	/* agent_arg_t list for requests
					 * requiring job write lock */
static list_t *mail_list = NULL;	/* pending e-mail requests */
static list_t *retry_list = NULL;	/* agent_arg_t list for retry */

static list_t *update_node_list = NULL;	/* node list for update */
static pthread_mutex_t update_nodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t update_nodes_cond = PTHREAD_COND_INITIALIZER;

static list_t *update_srun_list = NULL;
static pthread_mutex_t update_srun_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t update_srun_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;
static int agent_thread_cnt = 0;
static int mail_thread_cnt = 0;
static uint16_t message_timeout = NO_VAL16;

static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pending_cond = PTHREAD_COND_INITIALIZER;
static int pending_wait_time = NO_VAL16;
static bool pending_mail = false;
static pthread_t pending_thread_tid = 0;
static pthread_t nodes_update_tid = 0;
static pthread_t srun_update_tid = 0;
static bool pending_check_defer = false;

static bool run_scheduler    = false;

static uint32_t *rpc_stat_counts = NULL, *rpc_stat_types = NULL;
static uint32_t stat_type_count = 0;
static uint32_t rpc_count = 0;
static uint32_t *rpc_type_list;
static char **rpc_host_list = NULL;
static time_t cache_build_time = 0;

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
	int i, delay;
	pthread_t thread_wdog = 0;
	agent_arg_t *agent_arg_ptr = args;
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr;
	task_info_t *task_specific_ptr;
	time_t begin_time;
	bool spawn_retry_agent = false;
	int rpc_thread_cnt;
	static time_t sched_update = 0;
	static bool reboot_from_ctld = false;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "agent", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "agent");
	}
#endif

	log_flag(AGENT, "%s: Agent_cnt=%d agent_thread_cnt=%d with msg_type=%s retry_list_size=%d",
		 __func__, agent_cnt, agent_thread_cnt,
		 rpc_num2string(agent_arg_ptr->msg_type),
		 retry_list_size());

	slurm_mutex_lock(&agent_cnt_mutex);

	if (sched_update != slurm_conf.last_update) {
		reboot_from_ctld = false;
		if (xstrcasestr(slurm_conf.slurmctld_params,
		                "reboot_from_controller"))
			reboot_from_ctld = true;
		sched_update = slurm_conf.last_update;
	}

	rpc_thread_cnt = 2 + MIN(agent_arg_ptr->node_count, AGENT_THREAD_COUNT);
	while (1) {
		if (slurmctld_config.shutdown_time ||
		    ((agent_thread_cnt+rpc_thread_cnt) <= MAX_SERVER_THREADS)) {
			agent_cnt++;
			agent_thread_cnt += rpc_thread_cnt;
			break;
		} else {	/* wait for state change and retry */
			slurm_cond_wait(&agent_cnt_cond, &agent_cnt_mutex);
		}
	}
	slurm_mutex_unlock(&agent_cnt_mutex);
	if (slurmctld_config.shutdown_time)
		goto cleanup;

	/* basic argument value tests */
	begin_time = time(NULL);
	if (_valid_agent_arg(agent_arg_ptr))
		goto cleanup;

	if (reboot_from_ctld &&
	    (agent_arg_ptr->msg_type == REQUEST_REBOOT_NODES)) {
		_reboot_from_ctld(agent_arg_ptr);
		goto cleanup;
	}

	/* initialize the agent data structures */
	agent_info_ptr = _make_agent_info(agent_arg_ptr);
	thread_ptr = agent_info_ptr->thread_struct;

	/* start the watchdog thread */
	slurm_thread_create(&thread_wdog, _wdog, agent_info_ptr);

	log_flag(AGENT, "%s: New agent thread_count:%d threads_active:%d retry:%c get_reply:%c r_uid:%u msg_type:%s protocol_version:%hu",
		 __func__, agent_info_ptr->thread_count,
		 agent_info_ptr->threads_active,
		 agent_info_ptr->retry ? 'T' : 'F',
		 agent_info_ptr->get_reply ? 'T' : 'F',
		 agent_info_ptr->r_uid,
		 rpc_num2string(agent_arg_ptr->msg_type),
		 agent_info_ptr->protocol_version);

	/* start all the other threads (up to AGENT_THREAD_COUNT active) */
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		/* wait until "room" for another thread */
		slurm_mutex_lock(&agent_info_ptr->thread_mutex);
		while (agent_info_ptr->threads_active >=
		       AGENT_THREAD_COUNT) {
			slurm_cond_wait(&agent_info_ptr->thread_cond,
					&agent_info_ptr->thread_mutex);
		}

		/*
		 * create thread specific data,
		 * NOTE: freed from _thread_per_group_rpc()
		 */
		task_specific_ptr = _make_task_data(agent_info_ptr, i);

		slurm_thread_create(&thread_ptr[i].thread,
				    _thread_per_group_rpc,
				    task_specific_ptr);
		agent_info_ptr->threads_active++;
		slurm_mutex_unlock(&agent_info_ptr->thread_mutex);
	}

	/* Wait for termination of remaining threads */
	slurm_thread_join(thread_wdog);
	delay = (int) difftime(time(NULL), begin_time);
	if (delay > (slurm_conf.msg_timeout * 2)) {
		info("agent msg_type=%s ran for %d seconds",
		     rpc_num2string(agent_arg_ptr->msg_type),  delay);
	}
	slurm_mutex_lock(&agent_info_ptr->thread_mutex);
	while (agent_info_ptr->threads_active != 0) {
		slurm_cond_wait(&agent_info_ptr->thread_cond,
				&agent_info_ptr->thread_mutex);
	}
	for (i = 0; i < agent_info_ptr->thread_count; i++)
		slurm_thread_join(thread_ptr[i].thread);
	slurm_mutex_unlock(&agent_info_ptr->thread_mutex);

	log_flag(AGENT, "%s: end agent thread_count:%d threads_active:%d retry:%c get_reply:%c msg_type:%s protocol_version:%hu",
		 __func__, agent_info_ptr->thread_count,
		 agent_info_ptr->threads_active,
		 agent_info_ptr->retry ? 'T' : 'F',
		 agent_info_ptr->get_reply ? 'T' : 'F',
		 rpc_num2string(agent_arg_ptr->msg_type),
		 agent_info_ptr->protocol_version);

cleanup:
	purge_agent_args(agent_arg_ptr);

	if (agent_info_ptr) {
		xfree(agent_info_ptr->thread_struct);
		xfree(agent_info_ptr);
	}
	slurm_mutex_lock(&agent_cnt_mutex);

	if (agent_cnt > 0) {
		agent_cnt--;
	} else {
		error("agent_cnt underflow");
		agent_cnt = 0;
	}
	if (agent_thread_cnt >= rpc_thread_cnt) {
		agent_thread_cnt -= rpc_thread_cnt;
	} else {
		error("agent_thread_cnt underflow");
		agent_thread_cnt = 0;
	}

	if ((agent_thread_cnt + AGENT_THREAD_COUNT + 2) < MAX_SERVER_THREADS)
		spawn_retry_agent = true;

	slurm_cond_broadcast(&agent_cnt_cond);
	slurm_mutex_unlock(&agent_cnt_mutex);

	if (spawn_retry_agent)
		agent_trigger(RPC_RETRY_INTERVAL, true, false);

	return NULL;
}

/* Basic validity test of agent argument */
static int _valid_agent_arg(agent_arg_t *agent_arg_ptr)
{
	int hostlist_cnt;

	xassert(agent_arg_ptr);
	xassert(agent_arg_ptr->hostlist);

	if (agent_arg_ptr->node_count == 0)
		return SLURM_ERROR;	/* no messages to be sent */
	hostlist_cnt = hostlist_count(agent_arg_ptr->hostlist);
	if (agent_arg_ptr->node_count != hostlist_cnt) {
		error("%s: node_count RPC different from hosts listed (%d!=%d)",
		     __func__, agent_arg_ptr->node_count, hostlist_cnt);
		return SLURM_ERROR;	/* no messages to be sent */
	}
	if (!agent_arg_ptr->r_uid_set) {
		error("%s: r_uid not set for message:%u ",
		      __func__, agent_arg_ptr->msg_type);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static agent_info_t *_make_agent_info(agent_arg_t *agent_arg_ptr)
{
	agent_info_t *agent_info_ptr = NULL;
	thd_t *thread_ptr = NULL;
	int thr_count = 0;
	char *name = NULL;
	bool split;

	agent_info_ptr = xmalloc(sizeof(agent_info_t));
	slurm_mutex_init(&agent_info_ptr->thread_mutex);
	slurm_cond_init(&agent_info_ptr->thread_cond, NULL);
	agent_info_ptr->thread_count   = agent_arg_ptr->node_count;
	agent_info_ptr->retry          = agent_arg_ptr->retry;
	agent_info_ptr->threads_active = 0;
	thread_ptr = xcalloc(agent_info_ptr->thread_count, sizeof(thd_t));
	agent_info_ptr->thread_struct  = thread_ptr;
	agent_info_ptr->r_uid = agent_arg_ptr->r_uid;
	agent_info_ptr->msg_type       = agent_arg_ptr->msg_type;
	agent_info_ptr->msg_args_pptr  = &agent_arg_ptr->msg_args;
	agent_info_ptr->msg_flags = agent_arg_ptr->msg_flags;
	agent_info_ptr->protocol_version = agent_arg_ptr->protocol_version;
	agent_info_ptr->tls_cert = agent_arg_ptr->tls_cert;

	if (!agent_info_ptr->thread_count)
		return agent_info_ptr;

	xassert(agent_arg_ptr->node_count ==
		hostlist_count(agent_arg_ptr->hostlist));

	if ((agent_arg_ptr->msg_type != REQUEST_JOB_NOTIFY)	&&
	    (agent_arg_ptr->msg_type != REQUEST_REBOOT_NODES)	&&
	    (agent_arg_ptr->msg_type != REQUEST_RECONFIGURE)	&&
	    (agent_arg_ptr->msg_type != REQUEST_RECONFIGURE_SACKD) &&
	    (agent_arg_ptr->msg_type != REQUEST_RECONFIGURE_WITH_CONFIG) &&
	    (agent_arg_ptr->msg_type != REQUEST_SHUTDOWN)	&&
	    (agent_arg_ptr->msg_type != SRUN_TIMEOUT)		&&
	    (agent_arg_ptr->msg_type != SRUN_NODE_FAIL)		&&
	    (agent_arg_ptr->msg_type != SRUN_REQUEST_SUSPEND)	&&
	    (agent_arg_ptr->msg_type != SRUN_USER_MSG)		&&
	    (agent_arg_ptr->msg_type != SRUN_STEP_MISSING)	&&
	    (agent_arg_ptr->msg_type != SRUN_STEP_SIGNAL)	&&
	    (agent_arg_ptr->msg_type != SRUN_JOB_COMPLETE)) {
		/* Sending message to a possibly large number of slurmd.
		 * Push all message forwarding to slurmd in order to
		 * offload as much work from slurmctld as possible. */
		split = false;
		agent_info_ptr->get_reply = true;
	} else {
		/* Message is going to one node (for srun) or we want
		 * it to get processed ASAP (SHUTDOWN or RECONFIGURE).
		 * Send the message directly to each node. */
		split = true;
	}
	if (agent_arg_ptr->addr || !split) {
		thread_ptr[0].state = DSH_NEW;
		if (agent_arg_ptr->addr) {
			name = hostlist_shift(agent_arg_ptr->hostlist);
			thread_ptr[0].addr = agent_arg_ptr->addr;
			thread_ptr[0].nodename = xstrdup(name);
			if (agent_arg_ptr->node_count > 1)
				error("%s: you will only be sending this to %s",
				      __func__, name);
			free(name);
			log_flag(AGENT, "%s: sending msg_type %s to node %s",
				 __func__,
				 rpc_num2string(agent_arg_ptr->msg_type),
				 thread_ptr[0].nodename);
		} else {
			thread_ptr[0].nodelist = agent_arg_ptr->hostlist;
			thread_ptr[0].addr = NULL;
			if (slurm_conf.debug_flags & DEBUG_FLAG_AGENT) {
				char *buf;
				buf = hostlist_ranged_string_xmalloc(
						agent_arg_ptr->hostlist);
				debug("%s: sending msg_type %s to nodes %s",
				      __func__,
				      rpc_num2string(agent_arg_ptr->msg_type),
				      buf);
				xfree(buf);
			}
		}
		agent_info_ptr->thread_count = 1;
		return agent_info_ptr;
	}

	hostlist_uniq(agent_arg_ptr->hostlist);
	while (thr_count < agent_info_ptr->thread_count) {
		name = hostlist_shift(agent_arg_ptr->hostlist);
		if (!name) {
			debug3("no more nodes to send to");
			break;
		}
		thread_ptr[thr_count].state = DSH_NEW;
		thread_ptr[thr_count].addr = NULL;
		thread_ptr[thr_count].nodename = xstrdup(name);
		log_flag(AGENT, "%s: sending msg_type %s to node %s",
			 __func__, rpc_num2string(agent_arg_ptr->msg_type),
			 thread_ptr[thr_count].nodename);
		free(name);
		thr_count++;
	}
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
	task_info_ptr->r_uid = agent_info_ptr->r_uid;
	task_info_ptr->msg_type          = agent_info_ptr->msg_type;
	task_info_ptr->msg_args_ptr      = *agent_info_ptr->msg_args_pptr;
	task_info_ptr->msg_flags = agent_info_ptr->msg_flags;
	task_info_ptr->protocol_version  = agent_info_ptr->protocol_version;
	task_info_ptr->tls_cert = agent_info_ptr->tls_cert;

	return task_info_ptr;
}

static void _update_wdog_state(thd_t *thread_ptr,
			       state_t *state,
			       thd_complete_t *thd_comp)
{
	switch (*state) {
	case DSH_ACTIVE:
		thd_comp->work_done = false;
		if (thread_ptr->end_time <= thd_comp->now) {
			log_flag(AGENT, "%s: agent thread %lu timed out",
				 __func__, (unsigned long) thread_ptr->thread);
			(void) pthread_kill(thread_ptr->thread, SIGUSR1);
			thread_ptr->end_time += message_timeout;
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
	case DSH_DUP_JOBID:
		thd_comp->fail_cnt++;
		break;
	}
}

/*
 * _wdog - Watchdog thread. Send SIGUSR1 to threads which have been active
 *	for too long.
 * IN args - pointer to agent_info_t with info on threads to watch
 * Sleep between polls with exponential times (from 0.005 to 1.0 second)
 */
static void *_wdog(void *args)
{
	bool srun_agent = false, sack_agent = false;
	int i;
	agent_info_t *agent_ptr = (agent_info_t *) args;
	thd_t *thread_ptr = agent_ptr->thread_struct;
	unsigned long usec = 5000;
	list_itr_t *itr;
	thd_complete_t thd_comp;
	ret_data_info_t *ret_data_info = NULL;

	if ( (agent_ptr->msg_type == SRUN_JOB_COMPLETE)			||
	     (agent_ptr->msg_type == SRUN_REQUEST_SUSPEND)		||
	     (agent_ptr->msg_type == SRUN_STEP_MISSING)			||
	     (agent_ptr->msg_type == SRUN_STEP_SIGNAL)			||
	     (agent_ptr->msg_type == SRUN_NODE_FAIL)			||
	     (agent_ptr->msg_type == SRUN_PING)				||
	     (agent_ptr->msg_type == SRUN_TIMEOUT)			||
	     (agent_ptr->msg_type == SRUN_USER_MSG)			||
	     (agent_ptr->msg_type == RESPONSE_RESOURCE_ALLOCATION)	||
	     (agent_ptr->msg_type == RESPONSE_HET_JOB_ALLOCATION) )
		srun_agent = true;
	if (agent_ptr->msg_type == REQUEST_RECONFIGURE_SACKD)
		sack_agent = true;

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
				while ((ret_data_info = list_next(itr))) {
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

	if (sack_agent) {
		if (thread_ptr[0].state != DSH_DONE)
			sackd_mgr_remove_node(thread_ptr[0].nodename);
	} else if (srun_agent) {
		_notify_slurmctld_jobs(agent_ptr);
	} else if (agent_ptr->msg_type != REQUEST_SHUTDOWN) {
		_notify_slurmctld_nodes(agent_ptr,
					thd_comp.no_resp_cnt,
					thd_comp.retry_cnt);
	}

	for (i = 0; i < agent_ptr->thread_count; i++) {
		FREE_NULL_LIST(thread_ptr[i].ret_list);
		xfree(thread_ptr[i].nodename);
	}

	if (thd_comp.max_delay)
		log_flag(AGENT, "%s: agent maximum delay %d seconds",
			 __func__, thd_comp.max_delay);

	slurm_mutex_unlock(&agent_ptr->thread_mutex);
	return NULL;
}

static void _notify_slurmctld_jobs(agent_info_t *agent_ptr)
{
	slurm_step_id_t step_id = {
		.job_id = 0,
		.step_id = NO_VAL,
		.step_het_comp = NO_VAL,
	};
	thd_t *thread_ptr = agent_ptr->thread_struct;

	if        (agent_ptr->msg_type == SRUN_PING) {
		srun_ping_msg_t *msg = *agent_ptr->msg_args_pptr;
		step_id.job_id  = msg->job_id;
	} else if (agent_ptr->msg_type == SRUN_TIMEOUT) {
		srun_timeout_msg_t *msg = *agent_ptr->msg_args_pptr;
		memcpy(&step_id, &msg->step_id, sizeof(step_id));
	} else if (agent_ptr->msg_type == RESPONSE_RESOURCE_ALLOCATION) {
		resource_allocation_response_msg_t *msg =
			*agent_ptr->msg_args_pptr;
		step_id.job_id = msg->step_id.job_id;
		step_id.sluid = msg->step_id.sluid;
	} else if (agent_ptr->msg_type == RESPONSE_HET_JOB_ALLOCATION) {
		list_t *het_alloc_list = *agent_ptr->msg_args_pptr;
		resource_allocation_response_msg_t *msg;
		if (!het_alloc_list || (list_count(het_alloc_list) == 0))
			return;
		msg = list_peek(het_alloc_list);
		step_id.job_id = msg->step_id.job_id;
		step_id.sluid = msg->step_id.sluid;
	} else if ((agent_ptr->msg_type == SRUN_JOB_COMPLETE)		||
		   (agent_ptr->msg_type == SRUN_REQUEST_SUSPEND)	||
		   (agent_ptr->msg_type == SRUN_STEP_MISSING)		||
		   (agent_ptr->msg_type == SRUN_STEP_SIGNAL)		||
		   (agent_ptr->msg_type == SRUN_USER_MSG)) {
		return;		/* no need to note srun response */
	} else if (agent_ptr->msg_type == SRUN_NODE_FAIL) {
		return;		/* no need to note srun response */
	} else {
		error("%s: invalid msg_type %u", __func__, agent_ptr->msg_type);
		return;
	}

	if (thread_ptr[0].state == DSH_DONE)
		_queue_update_srun(&step_id);
}

static void _notify_slurmctld_nodes(agent_info_t *agent_ptr,
				    int no_resp_cnt, int retry_cnt)
{
	list_itr_t *itr = NULL;
	ret_data_info_t *ret_data_info = NULL;
	state_t state;
	int is_ret_list = 1;
	/* Locks: Read config, write node */
	slurmctld_lock_t node_write_lock =
		{ .conf = READ_LOCK, .node = WRITE_LOCK };
	thd_t *thread_ptr = agent_ptr->thread_struct;
	int i;
	bool locked = false;

	/* Notify slurmctld of non-responding nodes */
	if (no_resp_cnt) {
		/* Update node table data for non-responding nodes */
		if (agent_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
			/* Requeue the request */
			batch_job_launch_msg_t *launch_msg_ptr =
					*agent_ptr->msg_args_pptr;
			/* Locks: Write job, write node, read federation */
			slurmctld_lock_t job_write_lock =
				{ .job  = WRITE_LOCK,
				  .node = WRITE_LOCK,
				  .fed  = READ_LOCK };

			lock_slurmctld(job_write_lock);
			job_complete(&launch_msg_ptr->step_id,
				     slurm_conf.slurm_user_id, true, false, 0);
			unlock_slurmctld(job_write_lock);
		}
	}
	if (retry_cnt && agent_ptr->retry)
		_queue_agent_retry(agent_ptr, retry_cnt);

	/* Update last_response on responding nodes */
	for (i = 0; i < agent_ptr->thread_count; i++) {
		char *down_msg, **node_names;
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
				node_names = &ret_data_info->node_name;
				resp_type = ret_data_info->type;
			} else
				node_names = &thread_ptr[i].nodename;

			if (!is_ret_list && !(*node_names))
				goto finished;

			switch (state) {
			case DSH_NO_RESP:
				if (!locked) {
					locked = true;
					lock_slurmctld(node_write_lock);
				}
				node_not_resp(*node_names,
					      thread_ptr[i].start_time,
					      resp_type);
				break;
			case DSH_FAILED:
				if (!locked) {
					locked = true;
					lock_slurmctld(node_write_lock);
				}
				drain_nodes(*node_names, "Prolog/Epilog failure",
				            slurm_conf.slurm_user_id);
				down_msg = ", set to state DRAIN";
				error("Prolog/Epilog failure on nodes %s%s",
				      *node_names, down_msg);
				break;
			case DSH_DUP_JOBID:
				if (!locked) {
					locked = true;
					lock_slurmctld(node_write_lock);
				}
				drain_nodes(*node_names, "Duplicate jobid",
				            slurm_conf.slurm_user_id);
				down_msg = ", set to state DRAIN";
				error("Duplicate jobid on nodes %s%s",
				      *node_names, down_msg);
				break;
			case DSH_DONE:
				/*
				 * Process now if we've already obtained the
				 * lock. Otherwise delegate to the dedicated
				 * processing thread.
				 */
				if (locked) {
					node_did_resp(*node_names);
				} else {
					_queue_update_node(*node_names);
					*node_names = NULL;
				}
				break;
			default:
				error("unknown state returned for %s",
				      *node_names);
				break;
			}
			if (!is_ret_list)
				goto finished;
		}
		list_iterator_destroy(itr);
finished:	;
	}
	if (locked)
		unlock_slurmctld(node_write_lock);

	if (run_scheduler) {
		run_scheduler = false;
		/* below functions all have their own locking */
		queue_job_scheduler();
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
		verbose("agent/is_node_resp: node:%s RPC:%s : %m",
			node_name, rpc_num2string(msg_type));
	return rc;
}

/* return a value for which WEXITSTATUS() returns 1 */
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
	bool is_kill_msg, srun_agent, sack_agent;
	list_t *ret_list = NULL;
	list_itr_t *itr;
	ret_data_info_t *ret_data_info = NULL;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	/* Lock: Read node */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Lock: Write node */
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	uint32_t job_id;

	xassert(args != NULL);
	is_kill_msg = (	(msg_type == REQUEST_KILL_TIMELIMIT)	||
			(msg_type == REQUEST_KILL_PREEMPTED)	||
			(msg_type == REQUEST_TERMINATE_JOB) );
	srun_agent = (	(msg_type == SRUN_PING)			||
			(msg_type == SRUN_JOB_COMPLETE)		||
			(msg_type == SRUN_STEP_MISSING)		||
			(msg_type == SRUN_STEP_SIGNAL)		||
			(msg_type == SRUN_TIMEOUT)		||
			(msg_type == SRUN_USER_MSG)		||
			(msg_type == RESPONSE_RESOURCE_ALLOCATION) ||
			(msg_type == SRUN_NODE_FAIL) );
	sack_agent = (msg_type == REQUEST_RECONFIGURE_SACKD);

	thread_ptr->start_time = time(NULL);

	slurm_mutex_lock(thread_mutex_ptr);
	thread_ptr->state = DSH_ACTIVE;
	thread_ptr->end_time = thread_ptr->start_time + message_timeout;
	slurm_mutex_unlock(thread_mutex_ptr);

	/* send request message */
	slurm_msg_t_init(&msg);

	if (task_ptr->protocol_version)
		msg.protocol_version = task_ptr->protocol_version;

	msg.msg_type = msg_type;
	msg.data     = task_ptr->msg_args_ptr;
	slurm_msg_set_r_uid(&msg, task_ptr->r_uid);
	msg.flags |= task_ptr->msg_flags;
	msg.tls_cert = task_ptr->tls_cert;
	task_ptr->tls_cert = NULL;

	if (thread_ptr->nodename)
		log_flag(AGENT, "%s: sending %s to %s", __func__,
			 rpc_num2string(msg_type), thread_ptr->nodename);
	else if (slurm_conf.debug_flags & DEBUG_FLAG_AGENT) {
		char *tmp_str;
		tmp_str = hostlist_ranged_string_xmalloc(thread_ptr->nodelist);
		debug("%s: sending %s to %s", __func__,
		      rpc_num2string(msg_type), tmp_str);
		xfree(tmp_str);
	}

	if (task_ptr->get_reply) {
		if (thread_ptr->addr) {
			msg.address = *thread_ptr->addr;

			if (!(ret_list = slurm_send_addr_recv_msgs(
				     &msg, thread_ptr->nodename, 0))) {
				error("%s: no ret_list given", __func__);
				goto cleanup;
			}
		} else if (thread_ptr->nodelist) {
			if (!(ret_list = start_msg_tree(thread_ptr->nodelist,
							&msg, 0))) {
				error("%s: no ret_list given", __func__);
				goto cleanup;
			}
		} else {
			if (!(ret_list = slurm_send_recv_msgs(
				thread_ptr->nodename, &msg, 0))) {
				error("%s: no ret_list given", __func__);
				goto cleanup;
			}
		}
	} else {
		if (thread_ptr->addr) {
			//info("got the address");
			msg.address = *thread_ptr->addr;
		} else {
			//info("no address given");
			xassert(thread_ptr->nodename);
			if (slurm_conf_get_addr(thread_ptr->nodename,
					        &msg.address, msg.flags)
			    == SLURM_ERROR) {
				error("%s: can't find address for host %s, check slurm.conf",
				      __func__, thread_ptr->nodename);
				goto cleanup;
			}
		}
		//info("sending %u to %s", msg_type, thread_ptr->nodename);
		if ((msg_type == SRUN_JOB_COMPLETE) ||
		    (msg_type == SRUN_STEP_SIGNAL)) {
			/*
			 * The srun runs as a single thread, while the kernel
			 * listen() may be queuing messages for further
			 * processing. If we get our SYN in the listen queue
			 * at the same time the last MESSAGE_TASK_EXIT is being
			 * processed, srun may exit meaning this message is
			 * never received, leading to a series of error
			 * messages from slurm_send_only_node_msg().
			 * So, we use this different function that blindly
			 * flings the message out and disregards any
			 * communication problems that may arise.
			 */
			slurm_send_msg_maybe(&msg);
			thread_state = DSH_DONE;
		} else if (slurm_send_only_node_msg(&msg) == SLURM_SUCCESS) {
			thread_state = DSH_DONE;
		} else {
			if (!sack_agent && !srun_agent) {
				lock_slurmctld(node_read_lock);
				_comm_err(thread_ptr->nodename, msg_type);
				unlock_slurmctld(node_read_lock);
			}
		}
		goto cleanup;
	}

	//info("got %d messages back", list_count(ret_list));
	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
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
			reset_node_free_mem(ret_data_info->node_name,
					    ping_resp->free_mem);
			unlock_slurmctld(node_write_lock);
		}
		/* SPECIAL CASE: Mark node as IDLE if job already complete */
		if (is_kill_msg &&
		    (rc == ESLURMD_KILL_JOB_ALREADY_COMPLETE)) {
			kill_job_msg_t *kill_job;
			kill_job = (kill_job_msg_t *)
				task_ptr->msg_args_ptr;
			job_record_t *job_ptr = NULL;
			rc = SLURM_SUCCESS;
			lock_slurmctld(job_write_lock);
			if (!(job_ptr = find_job(&kill_job->step_id)))
				debug("%s: unable to find %pI to mark epilog completed on node=%s with return_code=%u",
				      __func__, &kill_job->step_id,
				      ret_data_info->node_name, rc);
			else if (job_epilog_complete(job_ptr,
						     ret_data_info->node_name,
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

		/* SPECIAL CASE: Requeue/hold non-startable batch job,
		 * Requeue job prolog failure or duplicate job ID */
		if ((msg_type == REQUEST_BATCH_JOB_LAUNCH) &&
		    (rc != SLURM_SUCCESS) && (rc != ESLURMD_PROLOG_FAILED) &&
		    (rc != ESLURM_DUPLICATE_JOB_ID) &&
		    (ret_data_info->type != RESPONSE_FORWARD_FAILED)) {
			batch_job_launch_msg_t *launch_msg_ptr =
				task_ptr->msg_args_ptr;
			info("Killing non-startable batch %pI: %s",
			     &launch_msg_ptr->step_id, slurm_strerror(rc));
			thread_state = DSH_DONE;
			ret_data_info->err = thread_state;
			lock_slurmctld(job_write_lock);
			job_complete(&launch_msg_ptr->step_id,
				     slurm_conf.slurm_user_id, false, false,
				     _wif_status());
			unlock_slurmctld(job_write_lock);
			continue;
		} else if ((msg_type == RESPONSE_RESOURCE_ALLOCATION) &&
			   (rc == SLURM_COMMUNICATIONS_CONNECTION_ERROR)) {
			/* Communication issue to srun that launched the job
			 * Cancel rather than leave a stray-but-empty job
			 * behind on the allocated nodes. */
			resource_allocation_response_msg_t *msg_ptr =
				task_ptr->msg_args_ptr;
			info("Killing interactive %pI: %s",
			     &msg_ptr->step_id, slurm_strerror(rc));
			thread_state = DSH_FAILED;
			lock_slurmctld(job_write_lock);
			job_complete(&msg_ptr->step_id,
				     slurm_conf.slurm_user_id, false, false,
				     _wif_status());
			unlock_slurmctld(job_write_lock);
			continue;
		} else if ((msg_type == RESPONSE_HET_JOB_ALLOCATION) &&
			   (rc == SLURM_COMMUNICATIONS_CONNECTION_ERROR)) {
			/* Communication issue to srun that launched the job
			 * Cancel rather than leave a stray-but-empty job
			 * behind on the allocated nodes. */
			list_t *het_alloc_list = task_ptr->msg_args_ptr;
			resource_allocation_response_msg_t *msg_ptr;
			if (!het_alloc_list ||
			    (list_count(het_alloc_list) == 0))
				continue;
			msg_ptr = list_peek(het_alloc_list);
			info("Killing interactive %pI: %s",
			     &msg_ptr->step_id, slurm_strerror(rc));
			thread_state = DSH_FAILED;
			lock_slurmctld(job_write_lock);
			job_complete(&msg_ptr->step_id,
				     slurm_conf.slurm_user_id, false, false,
				     _wif_status());
			unlock_slurmctld(job_write_lock);
			continue;
		}

		if (msg_type == REQUEST_SIGNAL_TASKS) {
			job_record_t *job_ptr;
			signal_tasks_msg_t *msg_ptr =
				task_ptr->msg_args_ptr;

			if ((msg_ptr->signal == SIGCONT) ||
			    (msg_ptr->signal == SIGSTOP)) {
				job_id = msg_ptr->step_id.job_id;
				lock_slurmctld(job_write_lock);
				job_ptr = find_job_record(job_id);
				if (job_ptr == NULL) {
					info("%s: invalid JobId=%u",
					     __func__, job_id);
				} else if (rc == SLURM_SUCCESS) {
					if (msg_ptr->signal == SIGSTOP) {
						job_state_set_flag(job_ptr,
								   JOB_STOPPED);
					} else { // SIGCONT
						job_state_unset_flag(
							job_ptr, JOB_STOPPED);
					}
				}

				if (job_ptr)
					job_state_unset_flag(job_ptr,
							     JOB_SIGNALING);

				unlock_slurmctld(job_write_lock);
			}
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
		case ESLURM_DUPLICATE_JOB_ID:
			thread_state = DSH_DUP_JOBID;
			break;
		case ESLURM_INVALID_JOB_ID:
			/* Not indicative of a real error */
		case ESLURMD_STEP_NOTRUNNING:
			/* Not indicative of a real error */
			log_flag(AGENT, "%s: RPC to node %s failed, job not running",
				 __func__, ret_data_info->node_name);
			thread_state = DSH_DONE;
			break;
		default:
			if (!sack_agent && !srun_agent) {
				if (ret_data_info->err)
					errno = ret_data_info->err;
				else
					errno = rc;
				lock_slurmctld(node_read_lock);
				rc = _comm_err(ret_data_info->node_name,
					       msg_type);
				unlock_slurmctld(node_read_lock);
			}

			if (sack_agent || srun_agent)
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
	if (!ret_list && (msg_type == REQUEST_SIGNAL_TASKS)) {
		job_record_t *job_ptr;
		signal_tasks_msg_t *msg_ptr =
			task_ptr->msg_args_ptr;
		if ((msg_ptr->signal == SIGCONT) ||
		    (msg_ptr->signal == SIGSTOP)) {
			job_id = msg_ptr->step_id.job_id;
			lock_slurmctld(job_write_lock);
			job_ptr = find_job_record(job_id);
			if (job_ptr)
				job_state_unset_flag(job_ptr, JOB_SIGNALING);
			unlock_slurmctld(job_write_lock);
		}
	}
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
	slurm_cond_signal(thread_cond_ptr);
	slurm_mutex_unlock(thread_mutex_ptr);
	return NULL;
}

static int _setup_requeue(agent_arg_t *agent_arg_ptr, thd_t *thread_ptr,
			  int *count, int *spot)
{
	node_record_t *node_ptr;
	ret_data_info_t *ret_data_info = NULL;
	list_itr_t *itr;
	int rc = 0;

	itr = list_iterator_create(thread_ptr->ret_list);
	while ((ret_data_info = list_next(itr))) {
		log_flag(AGENT, "%s: got err of %d",
			 __func__, ret_data_info->err);
		if (ret_data_info->err != DSH_NO_RESP)
			continue;

		node_ptr = find_node_record(ret_data_info->node_name);
		if (node_ptr &&
		    (IS_NODE_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr) ||
		     IS_NODE_POWERED_DOWN(node_ptr))) {
			--(*count);
		} else if (agent_arg_ptr) {
			debug("%s: got the name %s to resend out of %d",
			      __func__, ret_data_info->node_name, *count);
			hostlist_push_host(agent_arg_ptr->hostlist,
				      ret_data_info->node_name);
			++(*spot);
		}

		if (*spot == *count) {
			rc = 1;
			break;
		}
	}
	list_iterator_destroy(itr);

	return rc;
}

/*
 * _queue_agent_retry - Queue any failed RPCs for later replay
 * IN agent_info_ptr - pointer to info on completed agent requests
 * IN count - number of agent requests which failed, count to requeue
 */
static void _queue_agent_retry(agent_info_t * agent_info_ptr, int count)
{
	node_record_t *node_ptr;
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

	set_agent_arg_r_uid(agent_arg_ptr, agent_info_ptr->r_uid);

	j = 0;
	for (i = 0; i < agent_info_ptr->thread_count; i++) {
		if (!thread_ptr[i].ret_list) {
			if (thread_ptr[i].state != DSH_NO_RESP)
				continue;

			debug("got the name %s to resend",
			      thread_ptr[i].nodename);
			node_ptr = find_node_record(thread_ptr[i].nodename);
			if (node_ptr &&
			    (IS_NODE_DOWN(node_ptr) ||
			     IS_NODE_POWERING_DOWN(node_ptr) ||
			     IS_NODE_POWERED_DOWN(node_ptr))) {
				/* Do not re-send RPC to DOWN node */
				if (count)
					count--;
			} else {
				hostlist_push_host(agent_arg_ptr->hostlist,
						   thread_ptr[i].nodename);
				j++;
			}
			if (j == count)
				break;
		} else {
			if (_setup_requeue(agent_arg_ptr, &thread_ptr[i],
					   &count, &j))
				break;
		}
	}
	if (count == 0) {
		/* All non-responding nodes are DOWN.
		 * Do not requeue, but discard this RPC */
		hostlist_destroy(agent_arg_ptr->hostlist);
		*(agent_info_ptr->msg_args_pptr) = agent_arg_ptr->msg_args;
		xfree(agent_arg_ptr);
		return;
	}
	if (count != j) {
		error("agent: Retry count (%d) != actual count (%d)",
			count, j);
		agent_arg_ptr->node_count = j;
	}
	debug2("Queue RPC msg_type=%s, nodes=%d for retry",
	       rpc_num2string(agent_arg_ptr->msg_type), j);

	/* add the request to a list */
	queued_req_ptr = xmalloc(sizeof(queued_request_t));
	queued_req_ptr->agent_arg_ptr = agent_arg_ptr;
	queued_req_ptr->last_attempt  = time(NULL);
	slurm_mutex_lock(&retry_mutex);
	if (retry_list == NULL)
		retry_list = list_create(_list_delete_retry);
	list_append(retry_list, queued_req_ptr);
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
	purge_agent_args(queued_req_ptr->agent_arg_ptr);
	xfree(queued_req_ptr);
}

/* Start a thread to manage queued agent requests */
static void *_agent_init(void *arg)
{
	int min_wait;
	bool mail_too;
	struct timespec ts = {0, 0};
	time_t last_defer_attempt = (time_t) 0;

	while (true) {
		slurm_mutex_lock(&pending_mutex);
		while (!slurmctld_config.shutdown_time &&
		       !pending_mail && !pending_check_defer &&
		       (pending_wait_time == NO_VAL16)) {
			ts.tv_sec  = time(NULL) + 2;
			slurm_cond_timedwait(&pending_cond, &pending_mutex,
					     &ts);
		}
		if (slurmctld_config.shutdown_time) {
			if (!retry_list_size() ||
			    (slurmctld_config.shutdown_time +
			     AGENT_SHUTDOWN_WAIT < time(NULL))) {
				slurm_mutex_unlock(&pending_mutex);
				break;
			}
		}
		mail_too = pending_mail;
		min_wait = pending_wait_time;
		pending_mail = false;
		pending_wait_time = NO_VAL16;
		slurm_mutex_unlock(&pending_mutex);

		if ((last_defer_attempt + 2 < last_job_update) ||
		    pending_check_defer) {
			last_defer_attempt = time(NULL);
			pending_check_defer = false;
			_agent_defer();
		}

		_agent_retry(min_wait, mail_too);
	}

	return NULL;
}

static int _foreach_node_did_resp(void *x, void *arg)
{
	node_did_resp((char *) x);
	return 1;
}

/* Start a thread to manage queued agent requests */
static void *_agent_nodes_update(void *arg)
{
	struct timespec ts = {0, 0};
	slurmctld_lock_t node_write_lock =
		{ .conf = READ_LOCK, .node = WRITE_LOCK };

	slurm_mutex_lock(&update_nodes_mutex);
	while (true) {
		ts.tv_sec = time(NULL) + 2;
		slurm_cond_timedwait(&update_nodes_cond, &update_nodes_mutex,
				     &ts);

		if (slurmctld_config.shutdown_time) {
			slurm_mutex_unlock(&update_nodes_mutex);
			break;
		}

		if (list_count(update_node_list)) {
			lock_slurmctld(node_write_lock);
			list_delete_all(update_node_list,
					_foreach_node_did_resp, NULL);
			unlock_slurmctld(node_write_lock);
		}
		ping_nodes_update();
	}

	return NULL;
}

static void _queue_update_node(char *node_name)
{
	slurm_mutex_lock(&update_nodes_mutex);
	if (!update_node_list)
		update_node_list = list_create(xfree_ptr);
	list_append(update_node_list, node_name);
	slurm_mutex_unlock(&update_nodes_mutex);
}

static int _foreach_srun_response(void *x, void *arg)
{
	srun_response(x);
	return 1;
}

/* Start a thread to manage queued agent requests */
static void *_agent_srun_update(void *arg)
{
	struct timespec ts = {0, 0};
	slurmctld_lock_t job_write_lock = { .job = WRITE_LOCK };

	slurm_mutex_lock(&update_srun_mutex);
	while (true) {
		ts.tv_sec = time(NULL) + 2;
		slurm_cond_timedwait(&update_srun_cond, &update_srun_mutex,
				     &ts);
		if (slurmctld_config.shutdown_time)
			break;

		if (!list_count(update_srun_list))
			continue;

		lock_slurmctld(job_write_lock);
		list_delete_all(update_srun_list, _foreach_srun_response, NULL);
		unlock_slurmctld(job_write_lock);
	}
	slurm_mutex_unlock(&update_srun_mutex);

	return NULL;
}

static void _queue_update_srun(slurm_step_id_t *step_id)
{
	slurm_step_id_t *queue_step_id = xmalloc(sizeof(*queue_step_id));

	memcpy(queue_step_id, step_id, sizeof(*step_id));

	list_append(update_srun_list, queue_step_id);

	/*
	 * This may or may not wake the _agent_srun_update thread.
	 * But - we intentionally do not want to claim the &update_srun_mutex
	 * here which would be the only way to ensure it was asleep, as that
	 * would block us from queuing additional work while it was blocked
	 * waiting for the job write lock.
	 */
	slurm_cond_signal(&update_srun_cond);
}

extern void agent_init(void)
{
	if (pending_thread_tid) {
		error("%s: thread already running", __func__);
		return;
	}

	update_srun_list = list_create(xfree_ptr);

	slurm_thread_create(&pending_thread_tid, _agent_init, NULL);
	slurm_thread_create(&nodes_update_tid, _agent_nodes_update, NULL);
	slurm_thread_create(&srun_update_tid, _agent_srun_update, NULL);
}

extern void agent_fini(void)
{
	struct timespec ts = {0, 0};
	int rc = 0;

	/*
	 * Wait until we know that slurmctld_config.shutdown_time set. This way,
	 * each helper thread that is checking slurmctld_config.shutdown_time to
	 * know when to shutdown can immediately end its slurm_cond_timedwait()
	 * loop rather than waiting for the next loop.
	 */
	slurm_mutex_lock(&slurmctld_config.shutdown_lock);
	while (!slurmctld_config.shutdown_time) {
		slurm_cond_wait(&slurmctld_config.shutdown_cond,
				&slurmctld_config.shutdown_lock);
	}
	slurm_mutex_unlock(&slurmctld_config.shutdown_lock);

	agent_trigger(999, true, true);

	slurm_mutex_lock(&update_nodes_mutex);
	slurm_cond_broadcast(&update_nodes_cond);
	slurm_mutex_unlock(&update_nodes_mutex);

	slurm_mutex_lock(&update_srun_mutex);
	slurm_cond_broadcast(&update_srun_cond);
	slurm_mutex_unlock(&update_srun_mutex);

	slurm_thread_join(pending_thread_tid);
	slurm_thread_join(nodes_update_tid);
	slurm_thread_join(srun_update_tid);

	ts.tv_sec = time(NULL) + AGENT_SHUTDOWN_WAIT;

	slurm_mutex_lock(&agent_cnt_mutex);
	slurm_cond_broadcast(&agent_cnt_cond);
	while (agent_thread_cnt) {
                rc = pthread_cond_timedwait(&agent_cnt_cond, &agent_cnt_mutex,
					    &ts);
		if (rc == ETIMEDOUT) {
			error("%s: left %d agent threads active", __func__,
			      agent_thread_cnt);
			break;
		}
	}
	slurm_mutex_unlock(&agent_cnt_mutex);

	FREE_NULL_LIST(update_srun_list);
}

/*
 * agent_trigger - Request processing of pending RPCs
 * IN min_wait - Minimum wait time between re-issue of a pending RPC
 * IN mail_too - Send pending email too, note this performed using a
 *	fork/waitpid, so it can take longer than just creating a pthread
 *	to send RPCs
 * IN check_defer - force defer_list check
 */
extern void agent_trigger(int min_wait, bool mail_too, bool check_defer)
{
	log_flag(AGENT, "%s: pending_wait_time=%d->%d mail_too=%c->%c Agent_cnt=%d agent_thread_cnt=%d retry_list_size=%d",
		 __func__, pending_wait_time, min_wait,
		 mail_too ?  'T' : 'F', pending_mail ? 'T' : 'F',
		 agent_cnt, agent_thread_cnt, retry_list_size());

	slurm_mutex_lock(&pending_mutex);
	if ((pending_wait_time == NO_VAL16) ||
	    (pending_wait_time >  min_wait))
		pending_wait_time = min_wait;
	if (mail_too)
		pending_mail = mail_too;
	if (check_defer)
		pending_check_defer = check_defer;
	slurm_cond_broadcast(&pending_cond);
	slurm_mutex_unlock(&pending_mutex);
}

/* agent_pack_pending_rpc_stats - pack counts of pending RPCs into a buffer */
extern void agent_pack_pending_rpc_stats(buf_t *buffer)
{
	time_t now;
	int i;
	queued_request_t *queued_req_ptr = NULL;
	agent_arg_t *agent_arg_ptr = NULL;
	list_itr_t *list_iter;

	now = time(NULL);
	if (difftime(now, cache_build_time) <= RPC_PACK_MAX_AGE)
		goto pack_it;	/* Send cached data */
	cache_build_time = now;

	if (rpc_stat_counts) {	/* Clear existing data */
		stat_type_count = 0;
		memset(rpc_stat_counts, 0, sizeof(uint32_t) * MAX_RPC_PACK_CNT);
		memset(rpc_stat_types,  0, sizeof(uint32_t) * MAX_RPC_PACK_CNT);

		rpc_count = 0;
		/* the other variables need not be cleared */
	} else {		/* Allocate buffers for data */
		stat_type_count = 0;
		rpc_stat_counts = xcalloc(MAX_RPC_PACK_CNT, sizeof(uint32_t));
		rpc_stat_types  = xcalloc(MAX_RPC_PACK_CNT, sizeof(uint32_t));

		rpc_count = 0;
		rpc_host_list = xcalloc(DUMP_RPC_COUNT, sizeof(char *));
		for (i = 0; i < DUMP_RPC_COUNT; i++) {
			rpc_host_list[i] = xmalloc(HOSTLIST_MAX_SIZE);
		}
		rpc_type_list = xcalloc(DUMP_RPC_COUNT, sizeof(uint32_t));
	}

	slurm_mutex_lock(&retry_mutex);
	if (retry_list) {
		list_iter = list_iterator_create(retry_list);
		/* iterate through list, find type slot or make a new one */
		while ((queued_req_ptr = list_next(list_iter))) {
			agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
			if (rpc_count < DUMP_RPC_COUNT) {
				rpc_type_list[rpc_count] =
						agent_arg_ptr->msg_type;
				hostlist_ranged_string(agent_arg_ptr->hostlist,
						HOSTLIST_MAX_SIZE,
						rpc_host_list[rpc_count]);
				rpc_count++;
			}
			for (i = 0; i < MAX_RPC_PACK_CNT; i++) {
				if (rpc_stat_types[i] == 0) {
					rpc_stat_types[i] =
						agent_arg_ptr->msg_type;
					stat_type_count++;
				} else if (rpc_stat_types[i] !=
					   agent_arg_ptr->msg_type)
					continue;
				rpc_stat_counts[i]++;
				break;
			}
		}
		list_iterator_destroy(list_iter);
	}
	slurm_mutex_unlock(&retry_mutex);

pack_it:
	pack32_array(rpc_stat_types,  stat_type_count, buffer);
	pack32_array(rpc_stat_counts, stat_type_count, buffer);

	pack32_array(rpc_type_list, rpc_count, buffer);
	packstr_array(rpc_host_list, rpc_count, buffer);
}

static void _agent_defer(void)
{
	int rc = -1;
	queued_request_t *queued_req_ptr = NULL;
	agent_arg_t *agent_arg_ptr = NULL;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock = { .job = WRITE_LOCK };

	lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&defer_mutex);
	if (defer_list) {
		list_t *tmp_list = NULL;
		/* first try to find a new (never tried) record */
		while ((queued_req_ptr = list_pop(defer_list))) {
			agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
			if (agent_arg_ptr->msg_type ==
			    REQUEST_BATCH_JOB_LAUNCH)
				rc = _batch_launch_defer(queued_req_ptr);
			else if (agent_arg_ptr->msg_type ==
				 REQUEST_SIGNAL_TASKS)
				rc = _signal_defer(queued_req_ptr);
			else
				fatal("%s: Invalid message type (%s)",
				      __func__,
				      rpc_num2string(agent_arg_ptr->msg_type));

			if (rc == -1) {   /* abort request */
				purge_agent_args(
					queued_req_ptr->agent_arg_ptr);
				xfree(queued_req_ptr);
			} else if (rc == 0) {
				/* ready to process now, move to retry_list */
				slurm_mutex_lock(&retry_mutex);
				if (!retry_list)
					retry_list =
						list_create(_list_delete_retry);
				list_append(retry_list, queued_req_ptr);
				slurm_mutex_unlock(&retry_mutex);
			} else if (rc == 1) {
				if (!tmp_list)
					tmp_list =
						list_create(_list_delete_retry);
				list_append(tmp_list, (void *)queued_req_ptr);
			}
		}

		if (tmp_list) {
			list_transfer(defer_list, tmp_list);
			FREE_NULL_LIST(tmp_list);
		}
	}

	slurm_mutex_unlock(&defer_mutex);
	unlock_slurmctld(job_write_lock);
}

static int _find_request(void *x, void *key)
{
	queued_request_t *queued_req_ptr = x;
	double *before = key;

	if (!(*before) && queued_req_ptr->last_attempt == 0)
		return 1;
	else if (queued_req_ptr->last_attempt < *before)
		return 1;

	return 0;
}

/* Do the work requested by agent_retry (retry pending RPCs).
 * This is a separate thread so the job records can be locked */
static void _agent_retry(int min_wait, bool mail_too)
{
	time_t now = time(NULL);
	queued_request_t *queued_req_ptr = NULL;
	agent_arg_t *agent_arg_ptr = NULL;
	mail_info_t *mi = NULL;
	int list_size = 0, agent_started = 0;

next:
	slurm_mutex_lock(&retry_mutex);
	if (retry_list && !list_size) {
		static time_t last_msg_time = (time_t) 0;
		uint32_t msg_type[5] = {0, 0, 0, 0, 0};
		int i = 0;
		list_size = list_count(retry_list);
		if (((list_size > 100) &&
		     (difftime(now, last_msg_time) > 300)) ||
		    ((list_size > 0) &&
		     (slurm_conf.debug_flags & DEBUG_FLAG_AGENT))) {
			/* Note sizable backlog (retry_list_size()) of work */
			list_itr_t *retry_iter;
			retry_iter = list_iterator_create(retry_list);
			while ((queued_req_ptr = list_next(retry_iter))) {
				agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
				msg_type[i++] = agent_arg_ptr->msg_type;
				if (i == 5)
					break;
			}
			list_iterator_destroy(retry_iter);
			info("   retry_list retry_list_size:%d msg_type=%s,%s,%s,%s,%s",
			     list_size, rpc_num2string(msg_type[0]),
			     rpc_num2string(msg_type[1]),
			     rpc_num2string(msg_type[2]),
			     rpc_num2string(msg_type[3]),
			     rpc_num2string(msg_type[4]));
			last_msg_time = now;
		}
	}

	if (get_agent_thread_count() + AGENT_THREAD_COUNT + 2 >
	    MAX_SERVER_THREADS) {
		/* too much work already */
		slurm_mutex_unlock(&retry_mutex);
		return;
	}

	if (retry_list) {
		/* first try to find a new (never tried) record */
		double key = 0;

		queued_req_ptr = list_remove_first(retry_list, _find_request,
						   &key);
	}

	if (retry_list && (queued_req_ptr == NULL)) {
		/* now try to find a requeue request that is
		 * relatively old */
		double before = difftime(now, min_wait);

		queued_req_ptr = list_remove_first(retry_list, _find_request,
						   &before);
	}
	slurm_mutex_unlock(&retry_mutex);

	if (queued_req_ptr) {
		agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
		xfree(queued_req_ptr);
		if (agent_arg_ptr) {
			debug2("Spawning RPC agent for msg_type %s",
			       rpc_num2string(agent_arg_ptr->msg_type));
			slurm_thread_create_detached(agent, agent_arg_ptr);
			agent_started++;
		} else
			error("agent_retry found record with no agent_args");

		if ((list_size > agent_started) && !LOTS_OF_AGENTS) {
			log_flag(AGENT, "%s: created %d agent, try to start more",
				 __func__, agent_started);
			goto next;
		}
	} else if (mail_too) {
		slurm_mutex_lock(&agent_cnt_mutex);
		slurm_mutex_lock(&mail_mutex);
		while (mail_list && (agent_thread_cnt < MAX_SERVER_THREADS) &&
		       (mail_thread_cnt < MAX_MAIL_THREADS)) {
			mi = (mail_info_t *) list_dequeue(mail_list);
			if (!mi)
				break;

			mail_thread_cnt++;
			agent_thread_cnt++;
			slurm_thread_create_detached(_mail_proc, mi);
		}
		slurm_mutex_unlock(&mail_mutex);
		slurm_mutex_unlock(&agent_cnt_mutex);
	}
}

/*
 * agent_queue_request - put a new request on the queue for execution or
 * 	execute now if not too busy
 * IN agent_arg_ptr - the request to enqueue
 */
void agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	queued_request_t *queued_req_ptr = NULL;

	if ((AGENT_THREAD_COUNT + 2) >= MAX_SERVER_THREADS)
		fatal("AGENT_THREAD_COUNT value is too high relative to MAX_SERVER_THREADS");

	if (message_timeout == NO_VAL16) {
		message_timeout = MAX(slurm_conf.msg_timeout, 30);
	}

	if (agent_arg_ptr->msg_type == REQUEST_SHUTDOWN) {
		pthread_t agent_thread = 0;
		slurm_thread_create(&agent_thread, agent, agent_arg_ptr);
		slurm_thread_join(agent_thread);
		return;
	}

	queued_req_ptr = xmalloc(sizeof(queued_request_t));
	queued_req_ptr->agent_arg_ptr = agent_arg_ptr;
/*	queued_req_ptr->last_attempt  = 0; Implicit */

	if (((agent_arg_ptr->msg_type == REQUEST_BATCH_JOB_LAUNCH) &&
	     (_batch_launch_defer(queued_req_ptr) != 0)) ||
	    ((agent_arg_ptr->msg_type == REQUEST_SIGNAL_TASKS) &&
	     (_signal_defer(queued_req_ptr) != 0))) {
		slurm_mutex_lock(&defer_mutex);
		if (defer_list == NULL)
			defer_list = list_create(_list_delete_retry);
		list_append(defer_list, (void *)queued_req_ptr);
		slurm_mutex_unlock(&defer_mutex);
	} else {
		slurm_mutex_lock(&retry_mutex);
		if (retry_list == NULL)
			retry_list = list_create(_list_delete_retry);
		list_append(retry_list, (void *)queued_req_ptr);
		slurm_mutex_unlock(&retry_mutex);
	}
	/* now process the request in a separate pthread
	 * (if we can create another pthread to do so) */
	agent_trigger(999, false, false);
}

/* agent_purge - purge all pending RPC requests */
extern void agent_purge(void)
{
	int i;

	slurm_mutex_lock(&retry_mutex);
	FREE_NULL_LIST(retry_list);
	slurm_mutex_unlock(&retry_mutex);

	slurm_mutex_lock(&defer_mutex);
	FREE_NULL_LIST(defer_list);
	slurm_mutex_unlock(&defer_mutex);

	slurm_mutex_lock(&mail_mutex);
	FREE_NULL_LIST(mail_list);
	slurm_mutex_unlock(&mail_mutex);

	slurm_mutex_lock(&update_nodes_mutex);
	FREE_NULL_LIST(update_node_list);
	slurm_mutex_unlock(&update_nodes_mutex);

	xfree(rpc_stat_counts);
	xfree(rpc_stat_types);
	xfree(rpc_type_list);
	if (rpc_host_list) {
		for (i = 0; i < DUMP_RPC_COUNT; i++)
			xfree(rpc_host_list[i]);
		xfree(rpc_host_list);
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

extern int get_agent_thread_count(void)
{
	int cnt;

	slurm_mutex_lock(&agent_cnt_mutex);
	cnt = agent_thread_cnt;
	slurm_mutex_unlock(&agent_cnt_mutex);

	return cnt;
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
		env_array_free(mi->environment);
		xfree(mi);
	}
}

/*
 * Initializes MailProg environment and sets main part of variables, however
 * additional variables are set in _set_job_time and _set_job_term_info to avoid
 * code duplication.
 */
static char **_build_mail_env(job_record_t *job_ptr, uint32_t mail_type)
{
	char **my_env = job_common_env_vars(job_ptr,
					    ((mail_type & MAIL_JOB_END) ||
					     (mail_type & MAIL_JOB_FAIL)));

	setenvf(&my_env, "SLURM_JOB_STATE", "%s",
		job_state_string(job_ptr->job_state & JOB_STATE_BASE));

	setenvf(&my_env, "SLURM_JOB_MAIL_TYPE", "%s",
		_mail_type_str(mail_type));

	return my_env;
}

/* process an email request and free the record */
static void *_mail_proc(void *arg)
{
	mail_info_t *mi = (mail_info_t *) arg;
	int status;
	char *result = NULL;
	char *argv[5] = {
		slurm_conf.mail_prog, "-s", mi->message, mi->user_name, NULL};

	status = slurmscriptd_run_mail(slurm_conf.mail_prog, 5, argv,
				       mi->environment, MAIL_PROG_TIMEOUT,
				       &result);
	if (status)
		error("MailProg returned error, it's output was '%s'", result);
	else if (result && (strlen(result) > 0))
		debug("MailProg output was '%s'.", result);
	else
		debug2("No output from MailProg, exit code=%d", status);
	xfree(result);
	_mail_free(mi);
	slurm_mutex_lock(&agent_cnt_mutex);
	slurm_mutex_lock(&mail_mutex);
	if (agent_thread_cnt)
		agent_thread_cnt--;
	else
		error("agent_thread_cnt underflow");
	if (mail_thread_cnt)
		mail_thread_cnt--;
	else
		error("mail_thread_cnt underflow");
	slurm_mutex_unlock(&mail_mutex);
	slurm_mutex_unlock(&agent_cnt_mutex);

	return NULL;
}

static char *_mail_type_str(uint16_t mail_type)
{
	if (mail_type == MAIL_INVALID_DEPEND)
		return "Invalid dependency";
	if (mail_type == MAIL_JOB_BEGIN)
		return "Began";
	if (mail_type == MAIL_JOB_END)
		return "Ended";
	if (mail_type == MAIL_JOB_FAIL)
		return "Failed";
	if (mail_type == MAIL_JOB_REQUEUE)
		return "Requeued";
	if (mail_type == MAIL_JOB_STAGE_OUT)
		return "StageOut/Teardown";
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

static void _set_job_time(job_record_t *job_ptr, uint16_t mail_type,
			  char *buf, int buf_len, char ***env)
{
	time_t interval = NO_VAL;
	int msg_len;

	buf[0] = '\0';
	if ((mail_type == MAIL_JOB_BEGIN) && job_ptr->start_time &&
	    job_ptr->details && job_ptr->details->submit_time) {
		interval = job_ptr->start_time - job_ptr->details->submit_time;
		snprintf(buf, buf_len, ", Queued time ");
		msg_len = 14;
		secs2time_str(interval, buf+msg_len, buf_len-msg_len);
		setenvf(env, "SLURM_JOB_QUEUED_TIME", "%s", buf+msg_len);
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
		msg_len = 11;
		secs2time_str(interval, buf+msg_len, buf_len-msg_len);
		setenvf(env, "SLURM_JOB_RUN_TIME", "%s", buf+msg_len);
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
		msg_len = 11;
		secs2time_str(interval, buf+msg_len, buf_len-msg_len);
		setenvf(env, "SLURM_JOB_RUN_TIME", "%s", buf+msg_len);
		return;
	}

	if ((mail_type == MAIL_JOB_STAGE_OUT) && job_ptr->end_time) {
		interval = time(NULL) - job_ptr->end_time;
		snprintf(buf, buf_len, " time ");
		msg_len = 11;
		secs2time_str(interval, buf+msg_len, buf_len-msg_len);
		setenvf(env, "SLURM_JOB_STAGE_OUT_TIME", "%s", buf+msg_len);
		return;
	}
}

static void _set_job_term_info(job_record_t *job_ptr, uint16_t mail_type,
			       char *buf, int buf_len, char ***env)
{
	buf[0] = '\0';

	if ((mail_type == MAIL_JOB_END) || (mail_type == MAIL_JOB_FAIL)) {
		uint16_t base_state;
		uint32_t exit_status_min, exit_status_max;
		int exit_code_min, exit_code_max;

		base_state = job_ptr->job_state & JOB_STATE_BASE;
		if (job_ptr->array_recs &&
		    !(job_ptr->mail_type & MAIL_ARRAY_TASKS)) {
			/* Summarize array tasks. */
			exit_status_min = job_ptr->array_recs->min_exit_code;
			exit_status_max = job_ptr->array_recs->max_exit_code;
			if (WIFEXITED(exit_status_min) &&
			    WIFEXITED(exit_status_max)) {
				char *state_string;
				exit_code_min = WEXITSTATUS(exit_status_min);
				exit_code_max = WEXITSTATUS(exit_status_max);
				if ((exit_code_min == 0) && (exit_code_max > 0))
					state_string = "Mixed";
				else {
					state_string =
						job_state_string(base_state);
				}
				snprintf(buf, buf_len, ", %s, ExitCode [%d-%d]",
					 state_string, exit_code_min,
					 exit_code_max);
				setenvf(env, "SLURM_JOB_EXIT_CODE_MIN", "%d",
					exit_code_min);
				setenvf(env, "SLURM_JOB_EXIT_CODE_MAX", "%d",
					exit_code_max);
			} else if (WIFSIGNALED(exit_status_max)) {
				exit_code_max = WTERMSIG(exit_status_max);
				snprintf(buf, buf_len, ", %s, MaxSignal [%d]",
					 "Mixed", exit_code_max);
				setenvf(env, "SLURM_JOB_TERM_SIGNAL_MAX", "%d",
					exit_code_max);
			} else if (WIFEXITED(exit_status_max)) {
				exit_code_max = WEXITSTATUS(exit_status_max);
				snprintf(buf, buf_len, ", %s, MaxExitCode [%d]",
					 "Mixed", exit_code_max);
				setenvf(env, "SLURM_JOB_EXIT_CODE_MAX", "%d",
					exit_code_max);
			} else {
				snprintf(buf, buf_len, ", %s",
					 job_state_string(base_state));
				setenvf(env, "SLURM_JOB_EXIT_CODE_MAX", "%s",
					"0");
			}

			if (job_ptr->array_recs->array_flags &
			    ARRAY_TASK_REQUEUED)
				strncat(buf, ", with requeued tasks",
					buf_len - strlen(buf) - 1);
		} else {
			exit_status_max = job_ptr->exit_code;
			if (WIFEXITED(exit_status_max)) {
				exit_code_max = WEXITSTATUS(exit_status_max);
				snprintf(buf, buf_len, ", %s, ExitCode %d",
					 job_state_string(base_state),
					 exit_code_max);
				setenvf(env, "SLURM_JOB_EXIT_CODE_MAX", "%d",
					exit_code_max);
			} else {
				snprintf(buf, buf_len, ", %s",
					 job_state_string(base_state));
				setenvf(env, "SLURM_JOB_EXIT_CODE_MAX", "%s",
					"0");
			}
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
extern void mail_job_info(job_record_t *job_ptr, uint16_t mail_type)
{
	char job_time[128], term_msg[128];
	mail_info_t *mi;

	/*
	 * Send mail only for first component (leader) of a hetjob,
	 * not the individual job components.
	 */
	if (job_ptr->het_job_id && (job_ptr->het_job_offset != 0))
		return;

	mi = _mail_alloc();
	mi->user_name = xstrdup(job_ptr->mail_user);

	/* Use job array master record, if available */
	if (!(job_ptr->mail_type & MAIL_ARRAY_TASKS) &&
	    (job_ptr->array_task_id != NO_VAL) && !job_ptr->array_recs) {
		job_record_t *master_job_ptr;
		master_job_ptr = find_job_record(job_ptr->array_job_id);
		if (master_job_ptr && master_job_ptr->array_recs)
			job_ptr = master_job_ptr;
	}

	mi->environment = _build_mail_env(job_ptr, mail_type);
	_set_job_time(job_ptr, mail_type, job_time, sizeof(job_time),
		      &mi->environment);
	_set_job_term_info(job_ptr, mail_type, term_msg, sizeof(term_msg),
			   &mi->environment);
	if (job_ptr->array_recs && !(job_ptr->mail_type & MAIL_ARRAY_TASKS)) {
		mi->message = xstrdup_printf("Slurm Array Summary Job_id=%u_* (%u) Name=%s "
					     "%s%s",
					     job_ptr->array_job_id,
					     job_ptr->job_id, job_ptr->name,
					     _mail_type_str(mail_type),
					     term_msg);
	} else if (job_ptr->array_task_id != NO_VAL) {
		mi->message = xstrdup_printf("Slurm Array Task Job_id=%u_%u (%u) Name=%s "
					     "%s%s%s",
					     job_ptr->array_job_id,
					     job_ptr->array_task_id,
					     job_ptr->job_id, job_ptr->name,
					     _mail_type_str(mail_type),
					     job_time, term_msg);
	} else {
		mi->message = xstrdup_printf("Slurm Job_id=%u Name=%s %s%s%s",
					     job_ptr->job_id, job_ptr->name,
					     _mail_type_str(mail_type),
					     job_time, term_msg);
	}
	debug("email msg to %s: %s", mi->user_name, mi->message);

	slurm_mutex_lock(&mail_mutex);
	if (!mail_list)
		mail_list = list_create(_mail_free);
	list_enqueue(mail_list, mi);
	slurm_mutex_unlock(&mail_mutex);
}

/* Test if a batch launch request should be deferred
 * RET -1: abort the request, pending job cancelled
 *      0: execute the request now
 *      1: defer the request
 */
static int _batch_launch_defer(queued_request_t *queued_req_ptr)
{
	agent_arg_t *agent_arg_ptr;
	batch_job_launch_msg_t *launch_msg_ptr;
	time_t now = time(NULL);
	job_record_t *job_ptr;
	int nodes_ready = 0, tmp = 0;

	agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
	if (difftime(now, queued_req_ptr->last_attempt) < 10) {
		/* Reduce overhead by only testing once every 10 secs */
		return 1;
	}

	launch_msg_ptr = (batch_job_launch_msg_t *)agent_arg_ptr->msg_args;
	job_ptr = find_job(&launch_msg_ptr->step_id);
	if ((job_ptr == NULL) ||
	    (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) {
		info("agent(batch_launch): removed pending request for cancelled %pI",
		     &launch_msg_ptr->step_id);
		return -1;	/* job cancelled while waiting */
	}

	if (job_ptr->details && job_ptr->details->prolog_running) {
		debug2("%s: JobId=%u still waiting on %u prologs",
		       __func__, job_ptr->job_id,
		       job_ptr->details->prolog_running);
		return 1;
	}

	if (job_ptr->wait_all_nodes) {
		(void) job_node_ready(&launch_msg_ptr->step_id, &tmp);
		if (tmp ==
		    (READY_JOB_STATE | READY_NODE_STATE | READY_PROLOG_STATE)) {
			nodes_ready = 1;
		}
	} else {
		node_record_t *node_ptr;
		char *hostname;

		hostname = hostlist_deranged_string_xmalloc(
					agent_arg_ptr->hostlist);
		node_ptr = find_node_record(hostname);
		if (node_ptr == NULL) {
			error("agent(batch_launch) removed pending request for %pI, missing node %s",
			      &launch_msg_ptr->step_id, hostname);
			xfree(hostname);
			return -1;	/* invalid request?? */
		}
		xfree(hostname);
		if (!IS_NODE_POWERED_DOWN(node_ptr) &&
		    !IS_NODE_POWERING_DOWN(node_ptr) &&
		    !IS_NODE_NO_RESPOND(node_ptr)) {
			nodes_ready = 1;
		}
	}

	if ((slurm_conf.prolog_flags & PROLOG_FLAG_DEFER_BATCH) &&
	    (job_ptr->state_reason == WAIT_PROLOG)) {
		if (job_ptr->node_bitmap_pr &&
		    (slurm_conf.debug_flags &
		     (DEBUG_FLAG_TRACE_JOBS | DEBUG_FLAG_AGENT))) {
			char *tmp_pr;
			tmp_pr = bitmap2node_name(job_ptr->node_bitmap_pr);
			verbose("%s: JobId=%u still waiting on prologs on %s",
			       __func__, job_ptr->job_id, tmp_pr);
			xfree(tmp_pr);
		} else {
			debug2("%s: JobId=%u still waiting on node prologs",
			       __func__, job_ptr->job_id);
		}
		return 1;
	}

	if (nodes_ready) {
		if (IS_JOB_CONFIGURING(job_ptr))
			job_config_fini(job_ptr);
		queued_req_ptr->last_attempt = (time_t) 0;
		return 0;
	}

	if (queued_req_ptr->last_attempt == 0) {
		queued_req_ptr->first_attempt = now;
		queued_req_ptr->last_attempt  = now;
	} else if (difftime(now, queued_req_ptr->first_attempt) >=
		   slurm_conf.resume_timeout) {
		/* Nodes will get marked DOWN and job requeued, if possible */
		error("agent waited too long for nodes to respond, abort launch of JobId=%u",
		      job_ptr->job_id);
		return -1;
	}

	queued_req_ptr->last_attempt  = now;
	return 1;
}

/* Test if a job signal request should be deferred
 * RET -1: abort the request
 *      0: execute the request now
 *      1: defer the request
 */
static int _signal_defer(queued_request_t *queued_req_ptr)
{
	agent_arg_t *agent_arg_ptr;
	signal_tasks_msg_t *signal_msg_ptr;
	time_t now = time(NULL);
	job_record_t *job_ptr;

	agent_arg_ptr = queued_req_ptr->agent_arg_ptr;
	signal_msg_ptr = (signal_tasks_msg_t *)agent_arg_ptr->msg_args;

	if (!(job_ptr = find_job(&signal_msg_ptr->step_id))) {
		info("agent(signal_task): removed pending request for cancelled %pI",
		     &signal_msg_ptr->step_id);
		return -1;	/* job cancelled while waiting */
	}

	if (job_ptr->state_reason != WAIT_PROLOG)
		return 0;

	if (queued_req_ptr->first_attempt == 0) {
		queued_req_ptr->first_attempt = now;
	} else if (difftime(now, queued_req_ptr->first_attempt) >=
	           (2 * slurm_conf.batch_start_timeout)) {
		error("agent waited too long for nodes to respond, abort signal of %pJ",
		      job_ptr);
		return -1;
	}

	return 1;
}

/* Return length of agent's retry_list */
extern int retry_list_size(void)
{
	if (retry_list == NULL)
		return 0;
	return list_count(retry_list);
}

static void _reboot_from_ctld(agent_arg_t *agent_arg_ptr)
{
	char *argv[4], *pname;
	uint32_t argc;
	int rc, status = 0;
	reboot_msg_t *reboot_msg = agent_arg_ptr->msg_args;

	if (!agent_arg_ptr->hostlist) {
		error("%s: hostlist is NULL", __func__);
		return;
	}
	if ((!slurm_conf.reboot_program) ||
	    (!slurm_conf.reboot_program[0])) {
		error("%s: Requested reboot from slurmctld but RebootProgram is not defined", __func__);
		return;
	}

	pname = strrchr(slurm_conf.reboot_program, '/');
	if (pname)
		argv[0] = pname + 1;
	else
		argv[0] = slurm_conf.reboot_program;
	argv[1] = hostlist_deranged_string_xmalloc(agent_arg_ptr->hostlist);
	if (reboot_msg && reboot_msg->features) {
		argc = 4;
		argv[2] = reboot_msg->features;
		argv[3] = NULL;
	} else {
		argc = 3;
		argv[2] = NULL;
	}

	status = slurmscriptd_run_reboot(slurm_conf.reboot_program, argc, argv);
	if (WIFEXITED(status)) {
		rc = WEXITSTATUS(status);
		if (rc != 0) {
			error("RebootProgram exit status of %d",
			      rc);
		}
	} else if (WIFSIGNALED(status)) {
		error("RebootProgram signaled: %s",
		      strsignal(WTERMSIG(status)));
	}

	xfree(argv[1]);
}
