/****************************************************************************\
 *  msg.c - process message traffic between srun and slurm daemons
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, et. al.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <time.h>
#include <stdlib.h>

#include <slurm/slurm_errno.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/mpi.h"
#include "src/common/forward.h"
#include "src/api/pmi_server.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/msg.h"
#include "src/srun/sigstr.h"
#include "src/srun/debugger.h"
#include "src/srun/allocate.h"
#include "src/srun/multi_prog.h"
#include "src/srun/signals.h"

#include "src/common/xstring.h"

#define LAUNCH_WAIT_SEC	 60	/* max wait to confirm launches, sec */
#define MAX_RETRIES 3		/* pthread_create retries */

static int    tasks_exited     = 0;
static uid_t  slurm_uid;
static slurm_fd slurmctld_fd   = (slurm_fd) NULL;

/*
 *  Static prototypes
 */
static void	_accept_msg_connection(srun_job_t *job, int fdnum);
static void	_confirm_launch_complete(srun_job_t *job);
static void	_dump_proctable(srun_job_t *job);
static void 	_exit_handler(srun_job_t *job, slurm_msg_t *exit_msg);
static void	_handle_msg(srun_job_t *job, slurm_msg_t *msg);
static inline bool _job_msg_done(srun_job_t *job);
static void	_launch_handler(srun_job_t *job, slurm_msg_t *resp);
static void     _do_poll_timeout(srun_job_t *job);
static int      _get_next_timeout(srun_job_t *job);
static void 	_msg_thr_poll(srun_job_t *job);
static void	_set_jfds_nonblocking(srun_job_t *job);
static void     _print_pid_list(const char *host, int ntasks, 
				uint32_t *pid, char *executable_name);
static void	_node_fail_handler(const char *nodelist, srun_job_t *job);

#define _poll_set_rd(_pfd, _fd) do {    \
	(_pfd).fd = _fd;                \
	(_pfd).events = POLLIN;         \
	} while (0)

#define _poll_set_wr(_pfd, _fd) do {    \
	(_pfd).fd = _fd;                \
	(_pfd).events = POLLOUT;        \
	} while (0)

#define _poll_rd_isset(pfd) ((pfd).revents & POLLIN )
#define _poll_wr_isset(pfd) ((pfd).revents & POLLOUT)
#define _poll_err(pfd)      ((pfd).revents & POLLERR)

static void _update_mpir_proctable(srun_job_t *job,
				   int nodeid, int ntasks, uint32_t *pid,
				   char *executable)
{
	static int tasks_recorded = 0;
	int i;
	char *name = NULL;
	
	/* some initialization */
	if (MPIR_proctable_size == 0) {
		MPIR_proctable_size = job->step_layout->task_cnt;
		MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC)
					 * MPIR_proctable_size);
		totalview_jobid = NULL;
		xstrfmtcat(totalview_jobid, "%u", job->jobid);
	}

	/* FIXME - possibly never, now that --attach is removed */
	/* opt.argv global will be NULL during an srun --attach */
	if (opt.argv == NULL) {
		opt.argc = 1;
		xrealloc(opt.argv, 2 * sizeof(char *));
		opt.argv[0] = executable;
		opt.argv[1] = NULL;
	}

	name = nodelist_nth_host(job->step_layout->node_list, nodeid);
	for (i = 0; i < ntasks; i++) {
		MPIR_PROCDESC *tv;
		int taskid;

		taskid = job->step_layout->tids[nodeid][i];

		tv = &MPIR_proctable[taskid];
		tv->host_name = xstrdup(name);
		tv->pid = pid[i];
		tv->executable_name = executable;
		tasks_recorded++;
	}
	free(name);
	/* if all tasks are now accounted for, set the debug state and
	   call the Breakpoint */
	if (tasks_recorded == job->step_layout->task_cnt) {
		if (opt.multi_prog)
			set_multi_name(ntasks);
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		MPIR_Breakpoint();
		if (opt.debugger_test)
			_dump_proctable(job);
	}
	
	return;
}

static void _dump_proctable(srun_job_t *job)
{
	int node_inx, task_inx, taskid;
	int task_cnt;
	MPIR_PROCDESC *tv;

	for (node_inx=0; node_inx<job->nhosts; node_inx++) {
		task_cnt = job->step_layout->tasks[node_inx];
		for (task_inx = 0; task_inx < task_cnt; task_inx++) {
			taskid = job->step_layout->tids[node_inx][task_inx];
			tv = &MPIR_proctable[taskid];
			if (!tv)
				break;
			info("task:%d, host:%s, pid:%d, executable:%s",
			     taskid, tv->host_name, tv->pid,
			     tv->executable_name);
		}
	} 
}
	
void debugger_launch_failure(srun_job_t *job)
{
	if (opt.parallel_debug) {
		MPIR_debug_state = MPIR_DEBUG_ABORTING;
		MPIR_Breakpoint();
		if (opt.debugger_test)
			_dump_proctable(job);
	}
}

/*
 * Job has been notified of it's approaching time limit. 
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 * FIXME: We may want to signal the job or perform other action for this.
 * FIXME: How much lead time do we want for this message? Some jobs may 
 *	require tens of minutes to gracefully terminate.
 */
void timeout_handler(time_t timeout)
{
	static time_t last_timeout = 0;

	if (timeout != last_timeout) {
		last_timeout = timeout;
		verbose("job time limit to be reached at %s", 
			ctime(&timeout));
	}
}

/*
 * Job has been notified of a node's failure (at least the node's slurmd 
 * has stopped responding to slurmctld). It is possible that the user's 
 * job is continuing to execute on the specified nodes, but quite possibly 
 * not. The job will continue to execute given the --no-kill option. 
 * Otherwise all of the job's tasks and the job itself are killed..
 */
static void _node_fail_handler(const char *nodelist, srun_job_t *job)
{
	hostset_t fail_nodes, all_nodes;
	hostlist_iterator_t fail_itr;
	char *node;
	int num_node_ids;
	int *node_ids;
	int i, j;
	int node_id, num_tasks;

	/* now process the down nodes and tell the IO client about them */
	fail_nodes = hostset_create(nodelist);
	fail_itr = hostset_iterator_create(fail_nodes);
	num_node_ids = hostset_count(fail_nodes);
	node_ids = xmalloc(sizeof(int) * num_node_ids);

	all_nodes = hostset_create(job->step_layout->node_list);
	/* find the index number of each down node */
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < num_node_ids; i++) {
		node = hostlist_next(fail_itr);
		node_id = node_ids[i] = hostset_find(all_nodes, node);
		if (job->host_state[node_id] != SRUN_HOST_UNREACHABLE) {
			error("Node failure: %s.", node);
			job->host_state[node_id] = SRUN_HOST_UNREACHABLE;
		}
		free(node);

		/* find all of the tasks that should run on this failed node
		 * and mark them as having failed.
		 */
		num_tasks = job->step_layout->tasks[node_id];
		for (j = 0; j < num_tasks; j++) {
			int gtaskid;
			debug2("marking task %d done on failed node %d",
			       job->step_layout->tids[node_id][j], node_id);
			gtaskid = job->step_layout->tids[node_id][j];
			job->task_state[gtaskid] = SRUN_TASK_FAILED;
		}
	}
	slurm_mutex_unlock(&job->task_mutex);

	client_io_handler_downnodes(job->client_io, node_ids, num_node_ids);

	if (!opt.no_kill) {
		update_job_state(job, SRUN_JOB_FORCETERM);
		info("sending SIGINT to remaining tasks");
		fwd_signal(job, SIGINT, opt.max_threads);
	}
}

static bool _job_msg_done(srun_job_t *job)
{
	return (job->state >= SRUN_JOB_TERMINATED);
}

static void
_process_launch_resp(srun_job_t *job, launch_tasks_response_msg_t *msg)
{
	int nodeid = nodelist_find(job->step_layout->node_list,
				   msg->node_name);

	if ((nodeid < 0) || (nodeid >= job->nhosts)) {
		error ("Bad launch response from %s", msg->node_name);
		return;
	}
	pthread_mutex_lock(&job->task_mutex);
	job->host_state[nodeid] = SRUN_HOST_REPLIED;
	pthread_mutex_unlock(&job->task_mutex);

	_update_mpir_proctable(job,
			       nodeid, msg->count_of_pids,
			       msg->local_pids, opt.argv[0]);
	_print_pid_list( msg->node_name, msg->count_of_pids, 
			 msg->local_pids, opt.argv[0]     );
	return;
}

static void
update_running_tasks(srun_job_t *job, uint32_t nodeid)
{
	int i;

	debug2("updating %u running tasks for node %u", 
	       job->step_layout->tasks[nodeid], nodeid);
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->step_layout->tasks[nodeid]; i++) {
		uint32_t tid = job->step_layout->tids[nodeid][i];
		job->task_state[tid] = SRUN_TASK_RUNNING;
	}
	slurm_mutex_unlock(&job->task_mutex);
}

static void
update_failed_tasks(srun_job_t *job, uint32_t nodeid)
{
	int i;
	
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->step_layout->tasks[nodeid]; i++) {
		uint32_t tid = job->step_layout->tids[nodeid][i];
		job->task_state[tid] = SRUN_TASK_FAILED;
		tasks_exited++;
	}
	slurm_mutex_unlock(&job->task_mutex);

	if (tasks_exited == opt.nprocs) {
		debug2("all tasks exited");
		update_job_state(job, SRUN_JOB_TERMINATED);
	}
	slurm_mutex_unlock(&job->task_mutex);
}

static void
_launch_handler(srun_job_t *job, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = resp->data;
	int nodeid = nodelist_find(job->step_layout->node_list, 
				   msg->node_name);
		
	debug3("received launch resp from %s nodeid=%d", 
	       msg->node_name,
	       nodeid);
	
	if (msg->return_code != 0)  {

		error("%s: launch failed: %s", 
		      msg->node_name, slurm_strerror(msg->return_code));

		slurm_mutex_lock(&job->task_mutex);
		job->host_state[nodeid] = SRUN_HOST_REPLIED;
		slurm_mutex_unlock(&job->task_mutex);
		
		update_failed_tasks(job, nodeid);

		/*
		  if (!opt.no_kill) {
		  job->rc = 124;
		  update_job_state(job, SRUN_JOB_WAITING_ON_IO);
		  } else 
		  update_failed_tasks(job, nodeid);
		*/
		debugger_launch_failure(job);
		return;
	} else {
		_process_launch_resp(job, msg);
		update_running_tasks(job, nodeid);
	}
}

/* _confirm_launch_complete
 * confirm that all tasks registers a sucessful launch
 * pthread_exit with job kill on failure */
static void	
_confirm_launch_complete(srun_job_t *job)
{
	int i;
	char *name = NULL;

	printf("job->nhosts %d\n",job->nhosts);
		
	for (i=0; i<job->nhosts; i++) {
		printf("job->nhosts %d\n",job->nhosts);
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			name = nodelist_nth_host(job->step_layout->node_list,
						 i);
			error ("Node %s not responding, terminating job step",
			       name);
			free(name);
			info("sending Ctrl-C to remaining tasks");
			fwd_signal(job, SIGINT, opt.max_threads);
			job->rc = 124;
			update_job_state(job, SRUN_JOB_FAILED);
			pthread_exit(0);
		}
	}

	/*
	 *  Reset launch timeout so timer will no longer go off
	 */
	job->ltimeout = 0;
}

static void 
_print_exit_status(srun_job_t *job, hostlist_t hl, char *host, int status)
{
	char buf[MAXHOSTRANGELEN];
	char *corestr = "";
	bool signaled  = false;
	void (*print) (const char *, ...) = (void *) &error; 
		
	xassert(hl != NULL);

	slurm_mutex_lock(&job->state_mutex);
	signaled = job->signaled;
	slurm_mutex_unlock(&job->state_mutex);
	
	/*
	 *  Print message that task was signaled as verbose message
	 *    not error message if the user generated the signal.
	 */
	if (signaled) 
		print = &verbose;

	hostlist_ranged_string(hl, sizeof(buf), buf);

	if (status == 0) {
		verbose("%s: %s: Done", host, buf);
		return;
	}

#ifdef WCOREDUMP
	if (WCOREDUMP(status))
		corestr = " (core dumped)";
#endif

	if (WIFSIGNALED(status))
		(*print) ("%s: %s: %s%s", host, buf, sigstr(status), corestr); 
	else
		error ("%s: %s: Exited with exit code %d", 
		       host, buf, WEXITSTATUS(status));

	return;
}

static void
_die_if_signaled(srun_job_t *job, int status)
{
	bool signaled  = false;

	slurm_mutex_lock(&job->state_mutex);
	signaled = job->signaled;
	slurm_mutex_unlock(&job->state_mutex);

	if (WIFSIGNALED(status) && !signaled) {
		job->rc = 128 + WTERMSIG(status);
		update_job_state(job, SRUN_JOB_FAILED);
	}
}

static void 
_exit_handler(srun_job_t *job, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg       = (task_exit_msg_t *) exit_msg->data;
	hostlist_t       hl        = hostlist_create(NULL);
	int              task0     = msg->task_id_list[0];
	char            *host      = NULL;
	int              status    = msg->return_code;
	int              i;
	char             buf[1024];
	
	if (!(host = slurm_step_layout_host_name(job->step_layout, task0)))
		host = "Unknown host";
	debug2("exited host %s", host);
	if (!job->etimeout && !tasks_exited) 
		job->etimeout = time(NULL) + opt.max_exit_timeout;

	for (i = 0; i < msg->num_tasks; i++) {
		uint32_t taskid = msg->task_id_list[i];

		if ((taskid < 0) || (taskid >= opt.nprocs)) {
			error("task exit resp has bad task id %d", taskid);
			continue;
		}

		snprintf(buf, sizeof(buf), "task%d", taskid);
		hostlist_push(hl, buf);

		slurm_mutex_lock(&job->task_mutex);
		job->tstatus[taskid] = status;
		if (status) 
			job->task_state[taskid] = SRUN_TASK_ABNORMAL_EXIT;
		else {
			job->task_state[taskid] = SRUN_TASK_EXITED;
		}

		slurm_mutex_unlock(&job->task_mutex);

		tasks_exited++;
		debug2("looking for %d got %d", opt.nprocs, tasks_exited);
		if ((tasks_exited == opt.nprocs) 
		    || (mpi_hook_client_single_task_per_node () 
			&& (tasks_exited == job->nhosts))) {
			debug2("All tasks exited");
			update_job_state(job, SRUN_JOB_TERMINATED);
		}
	}
	
	_print_exit_status(job, hl, host, status);

	hostlist_destroy(hl);

	_die_if_signaled(job, status);

	/*
	 * When a task terminates with a non-zero exit code and the
	 * "--kill-on-bad-exit" option is set, terminate the entire job.
	 */
	if (status != 0 && opt.kill_bad_exit)
	{
		static int first_time = 1;

		/* Only kill the job once. */
		if (first_time)
		{
			debug("Terminating job due to a non-zero exit code");

			first_time = 0;

			srun_job_kill(job);
		}
	}
}

static void
_handle_msg(srun_job_t *job, slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred);
	uid_t uid     = getuid();
	int rc;
	srun_timeout_msg_t *to;
	srun_node_fail_msg_t *nf;
	
	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type)
	{
	case RESPONSE_LAUNCH_TASKS:
		debug("received task launch response");
		_launch_handler(job, msg);
		slurm_free_launch_tasks_response_msg(msg->data);
		break;
	case MESSAGE_TASK_EXIT:
		debug2("task_exit received");
		_exit_handler(job, msg);
		slurm_free_task_exit_msg(msg->data);
		break;
	case SRUN_PING:
		debug3("slurmctld ping received");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_srun_ping_msg(msg->data);
		break;
	case SRUN_JOB_COMPLETE:
		debug3("job complete received");
		/* FIXME: do something here */
		slurm_free_srun_job_complete_msg(msg->data);
		break;
	case SRUN_TIMEOUT:
		verbose("timeout received");
		to = msg->data;
		timeout_handler(to->timeout);
		slurm_free_srun_timeout_msg(msg->data);
		break;
	case SRUN_NODE_FAIL:
		verbose("node_fail received");
		nf = msg->data;
		_node_fail_handler(nf->nodelist, job);
		slurm_free_srun_node_fail_msg(msg->data);
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		debug3("resource allocation response received");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_resource_allocation_response_msg(msg->data);
		break;
	case PMI_KVS_PUT_REQ:
		debug3("PMI_KVS_PUT_REQ received");
		rc = pmi_kvs_put((struct kvs_comm_set *) msg->data);
		slurm_send_rc_msg(msg, rc);
		break;
	case PMI_KVS_GET_REQ:
		debug3("PMI_KVS_GET_REQ received");
		rc = pmi_kvs_get((kvs_get_msg_t *) msg->data);
		slurm_send_rc_msg(msg, rc);
		slurm_free_get_kvs_msg((kvs_get_msg_t *) msg->data);
		break;
	default:
		error("received spurious message type: %d\n",
		      msg->msg_type);
		break;
	}
	return;
}

/* NOTE: One extra FD for incoming slurmctld messages */
static void
_accept_msg_connection(srun_job_t *job, int fdnum)
{
	slurm_fd     fd = (slurm_fd) 0;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	unsigned char *uc;
	short        port;
	int          timeout = 0;	/* slurm default value */

	if (fdnum < job->njfds)
		fd = slurm_accept_msg_conn(job->jfd[fdnum], &cli_addr);
	else
		fd = slurm_accept_msg_conn(slurmctld_fd, &cli_addr);

	if (fd < 0) {
		error("Unable to accept connection: %m");
		return;
	}

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&cli_addr.sin_addr.s_addr;
	port = cli_addr.sin_port;
	debug2("got message connection from %u.%u.%u.%u:%hu",
	       uc[0], uc[1], uc[2], uc[3], ntohs(port));

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
		
	/* multiple jobs (easily induced via no_alloc) and highly
	 * parallel jobs using PMI sometimes result in slow message 
	 * responses and timeouts. Raise the default timeout for srun. */
	timeout = slurm_get_msg_timeout() * 8000;
again:
	if(slurm_receive_msg(fd, msg, timeout) != 0) {
		if (errno == EINTR) {
			goto again;
		}
		error("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		goto cleanup;
	}
		
	_handle_msg(job, msg); /* handle_msg frees msg->data */
cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", msg->conn_fd);
	slurm_free_msg(msg);
	

	return;
}


static void
_set_jfds_nonblocking(srun_job_t *job)
{
	int i;
	for (i = 0; i < job->njfds; i++) 
		fd_set_nonblocking(job->jfd[i]);
}

/*
 *  Call poll() with a timeout. (timeout argument is in seconds)
 * NOTE: One extra FD for incoming slurmctld messages
 */
static int
_do_poll(srun_job_t *job, struct pollfd *fds, int timeout)
{
	nfds_t nfds = (job->njfds + 1);
	int rc, to;

	if (timeout > 0)
		to = timeout * 1000;
	else
		to = timeout;

	while ((rc = poll(fds, nfds, to)) < 0) {
		switch (errno) {
		case EAGAIN:
		case EINTR:  continue;
		case ENOMEM:
		case EINVAL:
		case EFAULT: fatal("poll: %m");
		default:     error("poll: %m. Continuing...");
			continue;
		}
	}

	return rc;
}


/*
 *  Get the next timeout in seconds from now.
 */
static int 
_get_next_timeout(srun_job_t *job)
{
	int timeout = -1;

	if (!job->ltimeout && !job->etimeout)
		return -1;

	if (!job->ltimeout)
		timeout = job->etimeout - time(NULL);
	else if (!job->etimeout)
		timeout = job->ltimeout - time(NULL);
	else 
		timeout = job->ltimeout < job->etimeout ? 
			job->ltimeout - time(NULL) : 
			job->etimeout - time(NULL);

	return timeout;
}

/*
 *  Handle the two poll timeout cases:
 *    1. Job launch timed out
 *    2. Exit timeout has expired (either print a message or kill job)
 */
static void
_do_poll_timeout(srun_job_t *job)
{
	time_t now = time(NULL);

	if ((job->ltimeout > 0) && (job->ltimeout <= now)) 
		_confirm_launch_complete(job);

	if ((job->etimeout > 0) && (job->etimeout <= now)) {
		if (!opt.max_wait)
			info("Warning: first task terminated %ds ago", 
			     opt.max_exit_timeout);
		else {
			error("First task exited %ds ago", opt.max_wait);
			report_task_status(job);
			update_job_state(job, SRUN_JOB_FAILED);
		}
		job->etimeout = 0;
	}
}

/* NOTE: One extra FD for incoming slurmctld messages */
static void 
_msg_thr_poll(srun_job_t *job)
{
	struct pollfd *fds;
	int i;
	
	fds = xmalloc((job->njfds + 1) * sizeof(*fds));

	_set_jfds_nonblocking(job);
		
	for (i = 0; i < job->njfds; i++)
		_poll_set_rd(fds[i], job->jfd[i]);
	_poll_set_rd(fds[i], slurmctld_fd);
	
	while (!_job_msg_done(job)) {
		if (_do_poll(job, fds, _get_next_timeout(job)) == 0) {
			_do_poll_timeout(job);
			continue;
		}
		
		for (i = 0; i < (job->njfds + 1) ; i++) {
			unsigned short revents = fds[i].revents;
			if ((revents & POLLERR) || 
			    (revents & POLLHUP) ||
			    (revents & POLLNVAL))
				error("poll error on jfd %d: %m", fds[i].fd);
			else if (revents & POLLIN) 
				_accept_msg_connection(job, i);
		}
		
	}
	
	xfree(fds);	/* if we were to break out of while loop */
}

void *
msg_thr(void *arg)
{
	srun_job_t *job = (srun_job_t *) arg;
	debug3("msg thread pid = %lu", (unsigned long) getpid());

	slurm_uid = (uid_t) slurm_get_slurm_user_id();

	_msg_thr_poll(job);

	debug3("msg thread done");	
	return (void *)1;
}

/*
 * Create the message handling pthread.
 *
 * NOTE: call this before creating any pthreads to avoid having forked process 
 * hang on localtime_t() mutex locked in parent processes pthread.
 */
extern int 
msg_thr_create(srun_job_t *job)
{
	int i, retries = 0;
	pthread_attr_t attr;
	int rc;
	
	set_allocate_job(job);

	for (i = 0; i < job->njfds; i++) {
		if ((job->jfd[i] = slurm_init_msg_engine_port(0)) < 0)
			fatal("init_msg_engine_port: %m");
		if (slurm_get_stream_addr(job->jfd[i], 
					  &job->jaddr[i]) 
		    < 0)
			fatal("slurm_get_stream_addr: %m");
		debug("initialized job control port %d\n",
		      ntohs(((struct sockaddr_in)
			     job->jaddr[i]).sin_port));
	}

	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
thread_create_retry:
	rc = pthread_create(&job->msg_tid, &attr, &msg_thr, (void *)job);
	if (rc) {
		if (++retries > MAX_RETRIES)
			fatal("Can't create pthread");
		sleep(1);
		goto thread_create_retry;
	}

	slurm_attr_destroy(&attr);
	debug("Started message thread (%lu)", (unsigned long) job->msg_tid);
		
	return SLURM_SUCCESS;
}

static void
_print_pid_list(const char *host, int ntasks, uint32_t *pid, 
		char *executable_name)
{
	if (_verbose) {
		int i;
		hostlist_t pids = hostlist_create(NULL);
		char buf[MAXHOSTRANGELEN];
		
		for (i = 0; i < ntasks; i++) {
			snprintf(buf, sizeof(buf), "pids:%d", pid[i]);
			hostlist_push(pids, buf);
		}
		
		hostlist_ranged_string(pids, sizeof(buf), buf);
		verbose("%s: %s %s", host, executable_name, buf);
	}
}

/* Set up port to handle messages from slurmctld */
extern slurm_fd slurmctld_msg_init(void)
{
	slurm_addr slurm_address;
	uint16_t port;

	if (slurmctld_fd)	/* May set early for queued job allocation */
		return slurmctld_fd;

	slurmctld_fd = -1;
	slurmctld_comm_addr.hostname = NULL;
	slurmctld_comm_addr.port = 0;

	if ((slurmctld_fd = slurm_init_msg_engine_port(0)) < 0)
		fatal("slurm_init_msg_engine_port error %m");
	if (slurm_get_stream_addr(slurmctld_fd, &slurm_address) < 0)
		fatal("slurm_get_stream_addr error %m");
	fd_set_nonblocking(slurmctld_fd);
	/* hostname is not set,  so slurm_get_addr fails
	   slurm_get_addr(&slurm_address, &port, hostname, sizeof(hostname)); */
	port = ntohs(slurm_address.sin_port);
	slurmctld_comm_addr.hostname = xstrdup(opt.ctrl_comm_ifhn);
	slurmctld_comm_addr.port     = port;
	debug2("slurmctld messages to host=%s,port=%u", 
	       slurmctld_comm_addr.hostname, 
	       slurmctld_comm_addr.port);

	return slurmctld_fd;
}


