/*****************************************************************************\
 *  src/salloc/msg.c - Message handler for salloc
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/net.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_auth.h"
#include "src/common/eio.h"
#include "src/common/xsignal.h"

#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"
#include "src/salloc/msg.h"

struct salloc_msg_thread {
	eio_handle_t *handle;
	pthread_t id;
};

static uid_t slurm_uid;
static void _handle_msg(slurm_msg_t *msg);
static bool _message_socket_readable(eio_obj_t *obj);
static int _message_socket_accept(eio_obj_t *obj, List objs);
static pthread_mutex_t msg_thr_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t msg_thr_start_cond = PTHREAD_COND_INITIALIZER;
static struct io_operations message_socket_ops = {
	readable:	&_message_socket_readable,
	handle_read:	&_message_socket_accept
};

static void *_msg_thr_internal(void *arg)
{
	int signals[] = {SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM,
			 SIGUSR1, SIGUSR2, 0};

	debug("Entering _msg_thr_internal");
	xsignal_block(signals);
	pthread_mutex_lock(&msg_thr_start_lock);
	pthread_cond_signal(&msg_thr_start_cond);
	pthread_mutex_unlock(&msg_thr_start_lock);
	eio_handle_mainloop((eio_handle_t *)arg);
	debug("Leaving _msg_thr_internal");

	return NULL;
}

extern salloc_msg_thread_t *msg_thr_create(uint16_t *port)
{
	int sock = -1;
	eio_obj_t *obj;
	salloc_msg_thread_t *msg_thr = NULL;

	debug("Entering _msg_thr_create()");
	slurm_uid = (uid_t) slurm_get_slurm_user_id();
	msg_thr = (salloc_msg_thread_t *)xmalloc(sizeof(salloc_msg_thread_t));

	if (net_stream_listen(&sock, (short *)port) < 0) {
		error("unable to intialize step launch listening socket: %m");
		xfree(msg_thr);
		return NULL;
	}
	debug("port from net_stream_listen is %hu", *port);

	obj = eio_obj_create(sock, &message_socket_ops, NULL);

	msg_thr->handle = eio_handle_create();
	eio_new_initial_obj(msg_thr->handle, obj);
	pthread_mutex_lock(&msg_thr_start_lock);
	if (pthread_create(&msg_thr->id, NULL,
			   _msg_thr_internal, (void *)msg_thr->handle) != 0) {
		error("pthread_create of message thread: %m");
		eio_handle_destroy(msg_thr->handle);
		xfree(msg_thr);
		return NULL;
	}
	/* Wait until the message thread has blocked signals
	   before continuing. */
	pthread_cond_wait(&msg_thr_start_cond, &msg_thr_start_lock);
	pthread_mutex_unlock(&msg_thr_start_lock);

	return msg_thr;
}

extern void msg_thr_destroy(salloc_msg_thread_t *msg_thr)
{
	if (msg_thr == NULL)
		return;

	eio_signal_shutdown(msg_thr->handle);
	pthread_join(msg_thr->id, NULL);
	eio_handle_destroy(msg_thr->handle);
	xfree(msg_thr);
}


static bool _message_socket_readable(eio_obj_t *obj)
{
	debug3("Called _message_socket_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			close(obj->fd);
			obj->fd = -1;
			/*_wait_for_connections();*/
		} else {
			debug2("  false");
		}
		return false;
	} else {
		return true;
	}
}

static int _message_socket_accept(eio_obj_t *obj, List objs)
{
	int fd;
	unsigned char *uc;
	short        port;
	struct sockaddr_un addr;
	slurm_msg_t *msg = NULL;
	int len = sizeof(addr);
	
	debug3("Called _msg_socket_accept");

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN
		    || errno == ECONNABORTED
		    || errno == EWOULDBLOCK) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	fd_set_close_on_exec(fd);
	fd_set_blocking(fd);

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&((struct sockaddr_in *)&addr)->sin_addr.s_addr;
	port = ((struct sockaddr_in *)&addr)->sin_port;
	debug2("got message connection from %u.%u.%u.%u:%hu",
	       uc[0], uc[1], uc[2], uc[3], ntohs(port));
	fflush(stdout);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
again:
	if(slurm_receive_msg(fd, msg, 0) != 0) {
		printf("error on slurm_recieve_msg\n");
		fflush(stdout);
		if (errno == EINTR) {
			goto again;
		}
		error("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		goto cleanup;
	}
	
	_handle_msg(msg); /* handle_msg frees msg->data */
cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", msg->conn_fd);
	slurm_free_msg(msg);

	return SLURM_SUCCESS;
}


static void _handle_node_fail(slurm_msg_t *msg)
{
	srun_node_fail_msg_t *nf = (srun_node_fail_msg_t *)msg->data;

	error("Node failure on %s", nf->nodelist);

	slurm_free_srun_node_fail_msg(msg->data);
}

/*
 * Job has been notified of it's approaching time limit. 
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 */
static void _handle_timeout(slurm_msg_t *msg)
{
	static time_t last_timeout = 0;
	srun_timeout_msg_t *to = (srun_timeout_msg_t *)msg->data;

	debug3("received timeout message");
	if (to->timeout != last_timeout) {
		last_timeout = to->timeout;
		info("Job allocation time limit to be reached at %s",
		     ctime(&to->timeout));
	}

	slurm_free_srun_timeout_msg(msg->data);
}

static void _handle_user_msg(slurm_msg_t *msg)
{
	srun_user_msg_t *um;

	um = msg->data;
	info("%s", um->msg);
	slurm_free_srun_user_msg(msg->data);
}

static void _handle_job_complete(slurm_msg_t *msg)
{
	srun_job_complete_msg_t *comp = (srun_job_complete_msg_t *)msg->data;
	debug3("job complete message received");

	if (comp->step_id == NO_VAL) {
		pthread_mutex_lock(&allocation_state_lock);
		if (allocation_state != REVOKED) {
			/* If the allocation_state is already REVOKED, then
			 * no need to print this message.  We probably
			 * relinquished the allocation ourself.
			 */
			info("Job allocation %u has been revoked.",
			     comp->job_id);
		}
		if (allocation_state == GRANTED
		    && command_pid > -1
		    && opt.kill_command_signal_set) {
			verbose("Sending signal %d to command \"%s\", pid %d",
				opt.kill_command_signal,
				command_argv[0], command_pid);
			kill(command_pid, opt.kill_command_signal);
		}
		allocation_state = REVOKED;
		pthread_mutex_unlock(&allocation_state_lock);
	} else {
		verbose("Job step %u.%u is finished.",
			comp->job_id, comp->step_id);
	}
	slurm_free_srun_job_complete_msg(msg->data);
}

static void
_handle_msg(slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred);
	uid_t uid = getuid();
	
	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type) {
	case SRUN_PING:
		debug("received ping message");
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		slurm_free_srun_ping_msg(msg->data);
		break;
	case SRUN_JOB_COMPLETE:
		_handle_job_complete(msg);
		break;
	case SRUN_TIMEOUT:
		_handle_timeout(msg);
		break;
	case SRUN_USER_MSG:
		_handle_user_msg(msg);
		break;
	case SRUN_NODE_FAIL:
		_handle_node_fail(msg);
		break;
	default:
		error("received spurious message type: %d\n",
		      msg->msg_type);
		break;
	}
	return;
}

