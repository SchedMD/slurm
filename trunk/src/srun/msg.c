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

#include <errno.h>
#include <pthread.h>
#include <sys/poll.h>
#include <fcntl.h>

#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_errno.h>
#include <src/common/log.h>
#include <src/common/xassert.h>

#include <src/srun/job.h>
#include <src/srun/opt.h>

static int tasks_exited = 0;

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
		if (msg->srun_node_id >= 0 && msg->srun_node_id < job->nhosts)
			job->host_state[msg->srun_node_id] = 
				SRUN_HOST_REPLIED;
		else
			error("launch resp from %s has bad task_id %d",
				msg->node_name, msg->srun_node_id);
		pthread_mutex_unlock(&job->task_mutex);
	}

}

static void 
_exit_handler(job_t *job, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;

	if (msg->task_id < 0 || msg->task_id >= job->nhosts) {
		error("task exit resp has bad task_id %d",
			msg->task_id);
		return;
	}

	debug2("task %d exited with status %d", msg->task_id, msg->return_code);

	pthread_mutex_lock(&job->task_mutex);
	job->tstatus[msg->task_id] = msg->return_code;
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
	switch (msg->msg_type)
	{
		case RESPONSE_LAUNCH_TASKS:
			_launch_handler(job, msg);
			break;
		case MESSAGE_TASK_EXIT:
			_exit_handler(job, msg);
			break;
		case RESPONSE_REATTACH_TASKS_STREAMS:
			debug("recvd reattach response\n");
			break;
		default:
			error("received spurious message type: %d\n",
					msg->msg_type);
			break;
	}
	slurm_free_msg(msg);
}

static void
_accept_msg_connection(job_t *job, int fdnum)
{
	slurm_fd fd;
	slurm_msg_t *msg = NULL;
	slurm_addr  cli_addr;
	char addrbuf[256];

	if ((fd = slurm_accept_msg_conn(job->jfd[fdnum], &cli_addr)) < 0) {
		error("_accept_msg_connection/slurm_accept_msg_conn: %m");
		return;
	}

	slurm_print_slurm_addr(&cli_addr, addrbuf, 256);
	debug2("got message connection from %s", addrbuf);

	msg = xmalloc(sizeof(*msg));
	if (slurm_receive_msg(fd, msg) == SLURM_SOCKET_ERROR) {
		error("_accept_msg_connection/slurm_receive_msg: %m");
		xfree(msg);
	} else {

		msg->conn_fd = fd;
		_handle_msg(job, msg);
	}

	slurm_close_accepted_conn(fd);
	return;
}

static void
_set_jfds_nonblocking(job_t *job)
{
	int i;
	for (i = 0; i < job->njfds; i++) {
		if (fcntl(job->jfd[i], F_SETFL, O_NONBLOCK) < 0)
			error("Unable to set nonblocking I/O on jfd %d", i);
	}
}

static void 
_msg_thr_poll(job_t *job)
{
	struct pollfd *fds;
	nfds_t nfds = job->njfds;
	int i;

	fds = xmalloc(job->njfds * sizeof(*fds));

	_set_jfds_nonblocking(job);

	for (i = 0; i < job->njfds; i++)
		_poll_set_rd(fds[i], job->jfd[i]);

	while (1) {

		while (poll(fds, nfds, -1) < 0) {
			switch (errno) {
				case EINTR:
					continue;
					break;
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
			if (revents & POLLERR)
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
	_msg_thr_poll(job);
	return (void *)1;
}

void *
_msg_thr_one(void *arg)
{
	job_t *job = (job_t *) arg;
	slurm_fd fd;
	slurm_fd newfd;
	slurm_msg_t *msg = NULL;
	slurm_addr cli_addr;
	char addrbuf[256];

	xassert(job != NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	fd = job->jfd[0];

	while (1) {

		if ((newfd = slurm_accept_msg_conn(fd, &cli_addr)) < 0) {
			error("_msg_thr_one/slurm_accept_msg_conn: %m");
			break;
		}

		slurm_print_slurm_addr(&cli_addr, addrbuf, 256);
		debug2("got message connection from %s", addrbuf);


		msg = xmalloc(sizeof(*msg));
		if (slurm_receive_msg(newfd, msg) == SLURM_SOCKET_ERROR) {
			error("_msg_thr_one/slurm_receive_msg: %m");
			slurm_close_accepted_conn(newfd);
			break;
		}

		msg->conn_fd = newfd;
		_handle_msg(job, msg);
		slurm_close_accepted_conn(newfd);
	}

	/* reached only on receive error */
	return (void *)(0);
}


