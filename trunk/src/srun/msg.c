/****************************************************************************\
 *  msg.c - process message traffic between srun and slurm daemons
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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
#include <time.h>

#include <slurm/slurm_errno.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/srun/job.h"
#include "src/srun/opt.h"
#include "src/srun/io.h"
#include "src/srun/signals.h"

#ifdef HAVE_TOTALVIEW
#  include "src/srun/attach.h"
#endif

#define LAUNCH_WAIT_SEC	 60	/* max wait to confirm launches, sec */
#define POLL_TIMEOUT_MSEC	500

static time_t time_first_launch = 0;

static int   tasks_exited = 0;
static uid_t slurm_uid;

static void	_accept_msg_connection(job_t *job, int fdnum);
static void	_confirm_launch_complete(job_t *job);
static void 	_exit_handler(job_t *job, slurm_msg_t *exit_msg);
static void	_handle_msg(job_t *job, slurm_msg_t *msg);
static inline bool _job_msg_done(job_t *job);
static void	_launch_handler(job_t *job, slurm_msg_t *resp);
static void 	_msg_thr_poll(job_t *job);
static void	_set_jfds_nonblocking(job_t *job);
static char *	_taskid2hostname(int task_id, job_t * job);
static void     _print_pid_list(const char *host, int ntasks, 
				uint32_t *pid, char *executable_name);

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


#ifdef HAVE_TOTALVIEW
/* Convert node name to address string, eg. "123.45.67.8", 
 *	also return the index in the job table (-1 if not found) */
static char *
_node_name_to_addr(const char *name, job_t *job, int *inx)
{
	int i;
	char *buf = xmalloc(28);
	char *colon;

	for (i=0; i<job->nhosts; i++) {
		if (strcmp(name, job->host[i]))
			continue;
		slurm_print_slurm_addr(&job->slurmd_addr[i], buf, 128);
		/* This returns address:port, we need to remove ":port" */
		colon = strchr(buf, (int)':');
		if (colon)
			colon[0] = '\0'; 
		*inx = i;
		return buf;
	}

	error("_node_name_to_addr error on %s", name);
	*inx = -1;
	return NULL;
}

static void
_build_tv_list(job_t *job, char *host, int ntasks, uint32_t *pid)
{
	MPIR_PROCDESC * tv_tasks;
	int i, node_inx, task_id;
	char *node_addr;
	static int tasks_recorded = 0;

	node_addr = _node_name_to_addr(host, job, &node_inx);
	if ((node_addr == NULL) || (node_inx < 0))
		return;

	if (MPIR_proctable_size == 0) {
		MPIR_proctable_size = opt.nprocs;
		MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC) * opt.nprocs);
	}

	for (i = 0; i < ntasks; i++) {
		tasks_recorded++;
		task_id = job->tids[node_inx][i];
		tv_tasks = &MPIR_proctable[task_id];
		tv_tasks->host_name = node_addr;
		tv_tasks->executable_name = remote_argv[0];
		tv_tasks->pid = pid[i];
		debug("task=%d host=%s executable=%s pid=%d", task_id,
		      tv_tasks->host_name, tv_tasks->executable_name, 
		      tv_tasks->pid);
	}

	if (tasks_recorded == opt.nprocs) {
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		MPIR_Breakpoint(); 
	}
}


void tv_launch_failure(void)
{
	if (opt.totalview) {
		MPIR_debug_state = MPIR_DEBUG_ABORTING;
		MPIR_Breakpoint(); 
	}
}

void MPIR_Breakpoint(void)
{
	debug("In MPIR_Breakpoint");
	/* This just notifies TotalView that some event of interest occured */ 
}
#endif

static bool _job_msg_done(job_t *job)
{
	return (job->state >= SRUN_JOB_TERMINATED);
}

static void
_process_launch_resp(job_t *job, launch_tasks_response_msg_t *msg)
{
	if ((msg->srun_node_id >= 0) 
	    && (msg->srun_node_id < job->nhosts)) {

		pthread_mutex_lock(&job->task_mutex);
		job->host_state[msg->srun_node_id] = SRUN_HOST_REPLIED;
		pthread_mutex_unlock(&job->task_mutex);
#ifdef HAVE_TOTALVIEW
		_build_tv_list( job, msg->node_name, msg->count_of_pids,
				msg->local_pids );
#endif
		_print_pid_list( msg->node_name, msg->count_of_pids, 
				 msg->local_pids, remote_argv[0] );

	} else {
		error("launch resp from %s has bad task id %d",
				msg->node_name, msg->srun_node_id);
#ifdef HAVE_TOTALVIEW
		tv_launch_failure();
#endif
	}
}

static void
update_running_tasks(job_t *job, uint32_t nodeid)
{
	int i;
	debug2("updating %d running tasks for node %d", 
			job->ntask[nodeid], nodeid);
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[nodeid]; i++) {
		uint32_t tid = job->tids[nodeid][i];
		job->task_state[tid] = SRUN_TASK_RUNNING;
	}
	slurm_mutex_unlock(&job->task_mutex);
}

static void
update_failed_tasks(job_t *job, uint32_t nodeid)
{
	int i;
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[nodeid]; i++) {
		uint32_t tid = job->tids[nodeid][i];
		if (job->err[tid] == WAITING_FOR_IO)
			job->err[tid] = IO_DONE;
		if (job->out[tid] == WAITING_FOR_IO)
			job->out[tid] = IO_DONE;
		job->task_state[tid] = SRUN_TASK_FAILED;
		tasks_exited++;
	}
	slurm_mutex_unlock(&job->task_mutex);

	if (tasks_exited == opt.nprocs) {
		debug2("all tasks exited");
		update_job_state(job, SRUN_JOB_TERMINATED);
	}
		
}

static void
_launch_handler(job_t *job, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = resp->data;

	debug2("received launch resp from %s nodeid=%d", msg->node_name,
			msg->srun_node_id);
	
	if (msg->return_code != 0)  {

		error("%s: launch failed: %s", 
		       msg->node_name, slurm_strerror(msg->return_code));

		slurm_mutex_lock(&job->task_mutex);
		job->host_state[msg->srun_node_id] = SRUN_HOST_REPLIED;
		slurm_mutex_unlock(&job->task_mutex);

		if (!opt.no_kill)
			update_job_state(job, SRUN_JOB_FAILED);
		else 
			update_failed_tasks(job, msg->srun_node_id);
#ifdef HAVE_TOTALVIEW
		tv_launch_failure();
#endif
		return;
	} else {
		_process_launch_resp(job, msg);
		update_running_tasks(job, msg->srun_node_id);
	}
}

/* _confirm_launch_complete
 * confirm that all tasks registers a sucessful launch
 * pthread_exit with job kill on failure */
static void	
_confirm_launch_complete(job_t *job)
{
	int i;

	for (i=0; i<job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			error ("Node %s not responding, terminiating job step",
			       job->host[i]);
			update_job_state(job, SRUN_JOB_FAILED);
			pthread_exit(0);
		}
	}
}

static void
_reattach_handler(job_t *job, slurm_msg_t *msg)
{
	int i;
	reattach_tasks_response_msg_t *resp = msg->data;

	if ((resp->srun_node_id < 0) || (resp->srun_node_id >= job->nhosts)) {
		error ("Invalid reattach response recieved~");
		return;
	}

	slurm_mutex_lock(&job->task_mutex);
	job->host_state[resp->srun_node_id] = SRUN_HOST_REPLIED;
	slurm_mutex_unlock(&job->task_mutex);

	if (resp->return_code != 0) {
		if (job->stepid == NO_VAL) { 
			error ("Unable to attach to job %d: %s", 
			       job->jobid, slurm_strerror(resp->return_code));
			update_job_state(job, SRUN_JOB_FAILED);
		} else {
			error ("Unable to attach to step %d.%d on node %d: %s",
			       job->jobid, job->stepid, resp->srun_node_id,
			       slurm_strerror(resp->return_code));
		}
	}

	/* 
	 * store global task id information as returned from slurmd
	 */
	job->tids[resp->srun_node_id]  = xmalloc( resp->ntasks * 
						  sizeof(uint32_t) );
	job->ntask[resp->srun_node_id] = resp->ntasks;      
	for (i = 0; i < resp->ntasks; i++) {
		job->tids[resp->srun_node_id][i] = resp->gids[i];
	}

#if HAVE_TOTALVIEW
	if ((remote_argc == 0) && (resp->executable_name)) {
		remote_argc = 1;
		xrealloc(remote_argv, 2 * sizeof(char *));
		remote_argv[0] = resp->executable_name;
		resp->executable_name = NULL; /* nothing left to free */
		remote_argv[1] = NULL;
	}
	_build_tv_list(job, resp->node_name, resp->ntasks, resp->local_pids);
#endif
	_print_pid_list(resp->node_name, resp->ntasks, resp->local_pids, 
			resp->executable_name);

	update_running_tasks(job, resp->srun_node_id);

}

static void 
_exit_handler(job_t *job, slurm_msg_t *exit_msg)
{
	int i;
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;

	for (i=0; i<msg->num_tasks; i++) {
		uint32_t taskid = msg->task_id_list[i];

		if ((taskid < 0) || (taskid >= opt.nprocs)) {
			error("task exit resp has bad task id %d", taskid);
			return;
		}

		if (msg->return_code)
			verbose("task %d from node %s exited with status %d",  
			        taskid, _taskid2hostname(taskid, job), 
			        msg->return_code);
		else
			debug("task %d exited with status 0",  taskid);

		slurm_mutex_lock(&job->task_mutex);
		job->tstatus[taskid] = msg->return_code;
		if (msg->return_code) 
			job->task_state[taskid] = SRUN_TASK_FAILED;
		else {
			if (   (job->err[taskid] != IO_DONE) 
			    || (job->out[taskid] != IO_DONE) )
				job->task_state[taskid] = SRUN_TASK_IO_WAIT;
			else
				job->task_state[taskid] = SRUN_TASK_EXITED;
		}
		slurm_mutex_unlock(&job->task_mutex);

		tasks_exited++;
		if (tasks_exited == opt.nprocs) {
			debug2("All tasks exited");
			update_job_state(job, SRUN_JOB_TERMINATED);
		}
	}
}

static char *   _taskid2hostname (int task_id, job_t * job)
{
	int i, j, id = 0;

	if (opt.distribution == SRUN_DIST_BLOCK) {
		for (i=0; ((i<job->nhosts) && (id<opt.nprocs)); i++) {
			id += job->ntask[i];
			if (task_id < id)
				return job->host[i];
		}

	} else {	/* cyclic */
		for (j=0; (id<opt.nprocs); j++) {	/* cycle counter */
			for (i=0; ((i<job->nhosts) && (id<opt.nprocs)); i++) {
				if (j >= job->cpus[i])
					continue;
				if (task_id == (id++))
					return job->host[i];
			}
		}
	}

	return "Unknown";
}

static void
_handle_msg(job_t *job, slurm_msg_t *msg)
{
	uid_t req_uid = slurm_auth_uid(msg->cred);
	uid_t uid     = getuid();

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type)
	{
		case RESPONSE_LAUNCH_TASKS:
			_launch_handler(job, msg);
			slurm_free_launch_tasks_response_msg(msg->data);
			break;
		case MESSAGE_TASK_EXIT:
			_exit_handler(job, msg);
			slurm_free_task_exit_msg(msg->data);
			break;
		case RESPONSE_REATTACH_TASKS:
			debug2("recvd reattach response\n");
			_reattach_handler(job, msg);
			slurm_free_reattach_tasks_response_msg(msg->data);
			break;
		default:
			error("received spurious message type: %d\n",
					msg->msg_type);
			break;
	}
	slurm_free_msg(msg);
	return;
}

static void
_accept_msg_connection(job_t *job, int fdnum)
{
	slurm_fd fd;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	char         host[256];
	short        port;

	if ((fd = slurm_accept_msg_conn(job->jfd[fdnum], &cli_addr)) < 0) {
		error("_accept_msg_connection/slurm_accept_msg_conn: %m");
		return;
	}

	slurm_get_addr(&cli_addr, &port, host, sizeof(host));
	debug2("got message connection from %s:%d", host, ntohs(port));

	msg = xmalloc(sizeof(*msg));
  again:
	if (slurm_receive_msg(fd, msg) == SLURM_SOCKET_ERROR) {
		if (errno == EINTR)
			goto again;
		error("_accept_msg_connection/slurm_receive_msg(%s): %m",
				host);
		xfree(msg);
	} else {

		msg->conn_fd = fd;
		_handle_msg(job, msg); /* handle_msg frees msg */
	}

	slurm_close_accepted_conn(fd);
	return;
}

static void
_set_jfds_nonblocking(job_t *job)
{
	int i;
	for (i = 0; i < job->njfds; i++) 
		fd_set_nonblocking(job->jfd[i]);
}

static void 
_msg_thr_poll(job_t *job)
{
	struct pollfd *fds;
	nfds_t nfds = job->njfds;
	int i, rc;
	static bool check_launch_msg_sent = false;

	fds = xmalloc(job->njfds * sizeof(*fds));

	_set_jfds_nonblocking(job);

	for (i = 0; i < job->njfds; i++)
		_poll_set_rd(fds[i], job->jfd[i]);
	time_first_launch = time(NULL);

	while (!_job_msg_done(job)) {
		while ((!_job_msg_done(job)) &&
		       ((rc = poll(fds, nfds, POLL_TIMEOUT_MSEC)) <= 0)) {
			if (rc == 0) {	/* timeout */
				if (check_launch_msg_sent)
					;
				else if ((time(NULL) - time_first_launch) > 
				         LAUNCH_WAIT_SEC) {
					_confirm_launch_complete(job);
					check_launch_msg_sent = true; 
				}
				continue;
			}

			switch (errno) {
				case EINTR: continue;
				case ENOMEM:
				case EFAULT:
					fatal("poll: %m");
					break;
				default:
					error("poll: %m. trying again");
					break;
			}
		}

		for (i = 0; i < job->njfds; i++) {
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
	job_t *job = (job_t *) arg;

	debug3("msg thread pid = %ld", getpid());
	slurm_uid = (uid_t) slurm_get_slurm_user_id();
	_msg_thr_poll(job);
	return (void *)1;
}

int 
msg_thr_create(job_t *job)
{
	int i;
	pthread_attr_t attr;

	for (i = 0; i < job->njfds; i++) {
		if ((job->jfd[i] = slurm_init_msg_engine_port(0)) < 0)
			fatal("init_msg_engine_port: %m");
		if (slurm_get_stream_addr(job->jfd[i], &job->jaddr[i]) < 0)
			fatal("slurm_get_stream_addr: %m");
		debug("initialized job control port %d\n",
		      ntohs(((struct sockaddr_in)job->jaddr[i]).sin_port));
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if ((errno = pthread_create(&job->jtid, &attr, &msg_thr, 
			            (void *)job)))
		fatal("Unable to start message thread: %m");

	debug("Started msg server thread (%d)", job->jtid);

	return SLURM_SUCCESS;
}
 
static void
_print_pid_list(const char *host, int ntasks, uint32_t *pid, 
		char *executable_name)
{
	if (_verbose) {
		int i;
		hostlist_t pids = hostlist_create(NULL);
		char buf[1024];

		for (i = 0; i < ntasks; i++) {
			snprintf(buf, sizeof(buf), "pids:%d", pid[i]);
			hostlist_push(pids, buf);
		}

		hostlist_ranged_string(pids, sizeof(buf), buf);
		verbose("%s: %s %s", host, executable_name, buf);
	}
}


