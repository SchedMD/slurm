#include <errno.h>
#include <pthread.h>

#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/log.h>
#include <src/common/xassert.h>

#include "job.h"
#include "opt.h"

static int tasks_exited = 0;

static void
_launch_handler(job_t *job, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = 
		(launch_tasks_response_msg_t *) resp->data;

	debug2("recieved launch resp from %s", msg->node_name);
	
	if (msg->return_code != 0)  {
		error("recvd return code %d from %s", msg->return_code,
				msg->node_name);
		return;
	} else {
		/* job->host_state[msg->host_id] = SRUN_HOST_REPLIED; */
	}

}

static void 
_exit_handler(job_t *job, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;
	debug2("task %d exited with status %d", msg->task_id, msg->return_code);
	pthread_mutex_lock(&job->task_mutex);
	job->tstatus[msg->task_id] = msg->return_code;
	job->task_state[msg->task_id]  = SRUN_TASK_EXITED;
	pthread_mutex_unlock(&job->task_mutex);

	if (++tasks_exited == opt.nprocs) {
		debug2("all tasks exited");
		pthread_mutex_lock(&job->state_mutex);
		job->state = SRUN_JOB_OVERDONE;
		pthread_cond_signal(&job->state_cond);
		pthread_mutex_unlock(&job->state_mutex);
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
			error("recieved spurious message type: %d\n",
					msg->msg_type);
			break;
	}
	slurm_free_msg(msg);
}


void *
msg_thr(void *arg)
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

	fd = job->jfd;

	while (1) {

		if ((newfd = slurm_accept_msg_conn(fd, &cli_addr)) < 0) {
			error("slurm_accept_msg_conn: rc=%d", errno);
			break;
		}

		slurm_print_slurm_addr(&cli_addr, addrbuf, 256);
		debug2("got message connection from %s", addrbuf);


		msg = xmalloc(sizeof(*msg));
		if (slurm_receive_msg(newfd, msg) == SLURM_SOCKET_ERROR) {
			error("slurm_recieve_msg: rc=%d\n", errno);
			break;
		}

		msg->conn_fd = newfd;

		_handle_msg(job, msg);

		slurm_close_accepted_conn(newfd);

	}

	/* not reached */
	return (void *)(0);
}


