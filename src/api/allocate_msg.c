/*****************************************************************************\
 *  allocate_msg.c - Message handler for communication with with
 *                       the slurmctld during an allocation.
 *  $Id: allocate_msg.c 11641 2007-06-05 23:03:51Z jette $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
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

struct allocation_msg_thread {
	slurm_allocation_callbacks_t callback;
	eio_handle_t *handle;
	pthread_t id;
};

static uid_t slurm_uid;
static void _handle_msg(struct allocation_msg_thread *msg_thr,
			slurm_msg_t *msg);
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

extern allocation_msg_thread_t *slurm_allocation_msg_thr_create(
	uint16_t *port,
	const slurm_allocation_callbacks_t *callbacks)
{
	pthread_attr_t attr;
	int sock = -1;
	eio_obj_t *obj;
	struct allocation_msg_thread *msg_thr = NULL;

	debug("Entering slurm_allocation_msg_thr_create()");

	slurm_uid = (uid_t) slurm_get_slurm_user_id();
	msg_thr = (struct allocation_msg_thread *)xmalloc(
		sizeof(struct allocation_msg_thread));

	/* Initialize the callback pointers */
	if (callbacks != NULL) {
		/* copy the user specified callback pointers */
		memcpy(&(msg_thr->callback), callbacks,
		       sizeof(slurm_allocation_callbacks_t));
	} else {
		/* set all callbacks to NULL */
		memset(&(msg_thr->callback), 0,
		       sizeof(slurm_allocation_callbacks_t));
	}

	if (net_stream_listen(&sock, (short *)port) < 0) {
		error("unable to intialize step launch listening socket: %m");
		xfree(msg_thr);
		return NULL;
	}
	debug("port from net_stream_listen is %hu", *port);

	obj = eio_obj_create(sock, &message_socket_ops, (void *)msg_thr);

	msg_thr->handle = eio_handle_create();
	eio_new_initial_obj(msg_thr->handle, obj);
	pthread_mutex_lock(&msg_thr_start_lock);
	slurm_attr_init(&attr);
	if (pthread_create(&msg_thr->id, &attr,
			   _msg_thr_internal, (void *)msg_thr->handle) != 0) {
		error("pthread_create of message thread: %m");
		slurm_attr_destroy(&attr);
		eio_handle_destroy(msg_thr->handle);
		xfree(msg_thr);
		return NULL;
	}
	slurm_attr_destroy(&attr);
	/* Wait until the message thread has blocked signals
	   before continuing. */
	pthread_cond_wait(&msg_thr_start_cond, &msg_thr_start_lock);
	pthread_mutex_unlock(&msg_thr_start_lock);

	return (allocation_msg_thread_t *)msg_thr;
}

extern void slurm_allocation_msg_thr_destroy(
	allocation_msg_thread_t *arg)
{
	struct allocation_msg_thread *msg_thr =
		(struct allocation_msg_thread *)arg;
	if (msg_thr == NULL)
		return;

	debug2("slurm_allocation_msg_thr_destroy: clearing up message thread");
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
	struct allocation_msg_thread *msg_thr = 
		(struct allocation_msg_thread *)obj->arg;

	int fd;
	unsigned char *uc;
	short        port;
	struct sockaddr_un addr;
	slurm_msg_t *msg = NULL;
	int len = sizeof(addr);
	
	debug2("Called _msg_socket_accept");

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
	debug2("allocation got message connection from %u.%u.%u.%u:%hu",
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
	
	_handle_msg(msg_thr, msg); /* handle_msg frees msg->data */
cleanup:
	if ((msg->conn_fd >= 0) && slurm_close_accepted_conn(msg->conn_fd) < 0)
		error ("close(%d): %m", msg->conn_fd);
	slurm_free_msg(msg);

	return SLURM_SUCCESS;
}


static void _handle_node_fail(struct allocation_msg_thread *msg_thr, 
			      slurm_msg_t *msg)
{
	srun_node_fail_msg_t *nf = (srun_node_fail_msg_t *)msg->data;

	if (msg_thr->callback.node_fail != NULL)
		(msg_thr->callback.node_fail)(nf);

	slurm_free_srun_node_fail_msg(msg->data);
}

/*
 * Job has been notified of it's approaching time limit. 
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 */
static void _handle_timeout(struct allocation_msg_thread *msg_thr, 
			    slurm_msg_t *msg)
{
	srun_timeout_msg_t *to = (srun_timeout_msg_t *)msg->data;

	debug3("received timeout message");
	
	if (msg_thr->callback.timeout != NULL)
		(msg_thr->callback.timeout)(to);

	slurm_free_srun_timeout_msg(msg->data);
}

static void _handle_user_msg(struct allocation_msg_thread *msg_thr, 
			     slurm_msg_t *msg)
{
	srun_user_msg_t *um = (srun_user_msg_t *)msg->data;
	debug3("received user message");
	
	if (msg_thr->callback.user_msg != NULL)
		(msg_thr->callback.user_msg)(um);
	
	slurm_free_srun_user_msg(msg->data);
}

static void _handle_ping(struct allocation_msg_thread *msg_thr, 
			     slurm_msg_t *msg)
{
	srun_ping_msg_t *ping = (srun_ping_msg_t *)msg->data;
	debug3("received ping message");
	slurm_send_rc_msg(msg, SLURM_SUCCESS);

	if (msg_thr->callback.ping != NULL)
		(msg_thr->callback.ping)(ping);
	
	slurm_free_srun_ping_msg(msg->data);
}
static void _handle_job_complete(struct allocation_msg_thread *msg_thr, 
				 slurm_msg_t *msg)
{
	srun_job_complete_msg_t *comp = (srun_job_complete_msg_t *)msg->data;
	debug3("job complete message received");

	if (msg_thr->callback.job_complete != NULL)
		(msg_thr->callback.job_complete)(comp);
	
	slurm_free_srun_job_complete_msg(msg->data);
}

static void
_handle_msg(struct allocation_msg_thread *msg_thr, slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->auth_cred, NULL);
	uid_t uid = getuid();
	
	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type) {
	case SRUN_PING:
		_handle_ping(msg_thr, msg);
		break;
	case SRUN_JOB_COMPLETE:
		_handle_job_complete(msg_thr, msg);
		break;
	case SRUN_TIMEOUT:
		_handle_timeout(msg_thr, msg);
		break;
	case SRUN_USER_MSG:
		_handle_user_msg(msg_thr, msg);
		break;
	case SRUN_NODE_FAIL:
		_handle_node_fail(msg_thr, msg);
		break;
	default:
		error("received spurious message type: %d\n",
		      msg->msg_type);
		break;
	}
	return;
}

