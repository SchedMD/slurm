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
#include <sys/poll.h>
#include <time.h>

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_errno.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/srun/job.h"
#include "src/srun/opt.h"

#ifdef HAVE_TOTALVIEW
#include "src/srun/attach.h"
#endif

#define MAX_MSG_WAIT_SEC	 60	/* max wait to confirm launches, sec */
#define POLL_TIMEOUT_MSEC	500
static time_t time_last_msg;

static int tasks_exited = 0;
static uint32_t slurm_user_id;

static void	_accept_msg_connection(job_t *job, int fdnum);
static void	_confirm_launch_complete(job_t *job);
static void 	_exit_handler(job_t *job, slurm_msg_t *exit_msg);
static void	_handle_msg(job_t *job, slurm_msg_t *msg);
static void	_launch_handler(job_t *job, slurm_msg_t *resp);
static void 	_msg_thr_poll(job_t *job);
static void	_set_jfds_nonblocking(job_t *job);

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


static void
_launch_handler(job_t *job, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = 
		(launch_tasks_response_msg_t *) resp->data;

	debug2("received launch resp from %s nodeid=%d", msg->node_name,
			msg->srun_node_id);
	
	if (msg->return_code != 0)  {
		error("recvd return code %d from %s", msg->return_code,
				msg->node_name);
		return;
	} else {	
		pthread_mutex_lock(&job->task_mutex);
		if ((msg->srun_node_id >= 0) && 
		    (msg->srun_node_id < job->nhosts)) {
			job->host_state[msg->srun_node_id] = 
				SRUN_HOST_REPLIED;
#ifdef HAVE_TOTALVIEW
			if (opt.totalview) {
				MPIR_PROCDESC * tv_tasks;
				tv_tasks = 
					&MPIR_proctable[MPIR_proctable_size++];
				tv_tasks->host_name = msg->node_name;
				msg->node_name = NULL;	/* nothing to free */
				tv_tasks->executable_name = opt.progname;
				tv_tasks->pid = msg->local_pid;
			}
#endif
		} else
			error("launch resp from %s has bad task_id %d",
				msg->node_name, msg->srun_node_id);
		pthread_mutex_unlock(&job->task_mutex);
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
	reattach_tasks_response_msg_t *resp = msg->data;

	slurm_mutex_lock(&job->task_mutex);
	if ((resp->srun_node_id >= 0) && (resp->srun_node_id < job->nhosts)) {
		job->host_state[resp->srun_node_id] = SRUN_HOST_REPLIED;
	}
	slurm_mutex_unlock(&job->task_mutex);

}

static void 
_exit_handler(job_t *job, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;

	if ((msg->task_id < 0) || (msg->task_id >= opt.nprocs)) {
		error("task exit resp has bad task_id %d",
		      msg->task_id);
		return;
	}

	debug2("task %d exited with status %d", msg->task_id, 
	       msg->return_code);

	pthread_mutex_lock(&job->task_mutex);
	job->tstatus[msg->task_id] = msg->return_code;
	if (msg->return_code)
		job->task_state[msg->task_id]  = SRUN_TASK_FAILED;
	else
		job->task_state[msg->task_id]  = SRUN_TASK_EXITED;
	pthread_mutex_unlock(&job->task_mutex);

	tasks_exited++;
	if (tasks_exited == opt.nprocs) {
		debug2("all tasks exited");
		update_job_state(job, SRUN_JOB_OVERDONE);
	}
}

static void
_handle_msg(job_t *job, slurm_msg_t *msg)
{
	uid_t    req_uid = slurm_auth_uid(msg->cred);
	if ((req_uid != slurm_user_id) && (req_uid != 0)) {
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
			debug("recvd reattach response\n");
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
	time_last_msg = time(NULL);

	while (1) {
		while ((rc = poll(fds, nfds, POLL_TIMEOUT_MSEC)) <= 0) {
			if (rc == 0) {	/* timeout */
				i = time(NULL) - time_last_msg;
				if (job->state == SRUN_JOB_FAILED)
					pthread_exit(0);
				else if (check_launch_msg_sent)
					;
				else if (i > MAX_MSG_WAIT_SEC) {
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

		time_last_msg = time(NULL);
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
	slurm_user_id = slurm_get_slurm_user_id();
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

