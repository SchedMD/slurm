#include <errno.h>
#include <pthread.h>

#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/log.h>
#include <src/common/xassert.h>

#include "job.h"

static void 
_exit_handler(slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg = (task_exit_msg_t *) exit_msg->data;
	verbose("task %d exited with status %d", msg->task_id, msg->return_code);
}

static void
_handle_msg(slurm_msg_t *msg)
{
	switch (msg->msg_type)
	{
		case RESPONSE_LAUNCH_TASKS:
			debug("recvd launch tasks response\n");
			break;
		case MESSAGE_TASK_EXIT:
			debug("task exited\n");
			_exit_handler(msg);
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

		_handle_msg(msg);

		slurm_close_accepted_conn(newfd);

	}

	/* not reached */
	return (void *)(0);
}


