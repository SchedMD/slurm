/****************************************************************************\
 *  step_io.c - process stdin, stdout, and stderr for parallel jobs.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_cred.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/eio.h"
#include "src/common/io_hdr.h"
#include "src/common/net.h"
#include "src/common/write_labelled_message.h"

#include "src/api/step_io.h"
#include "src/api/step_launch.h"

#define MAX_RETRIES 3
#define STDIO_MAX_FREE_BUF 1024

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
	io_hdr_t header;
};

static struct io_buf *_alloc_io_buf(void);
#if 0
static void     _free_io_buf(struct io_buf *buf);
#endif
static void	_init_stdio_eio_objs(slurm_step_io_fds_t fds,
				     client_io_t *cio);
static void	_handle_io_init_msg(int fd, client_io_t *cio);
static int      _read_io_init_msg(int fd, client_io_t *cio, char *host);
static int      _wid(int n);
static bool     _incoming_buf_free(client_io_t *cio);
static bool     _outgoing_buf_free(client_io_t *cio);

/**********************************************************************
 * Listening socket declarations
 **********************************************************************/
static bool _listening_socket_readable(eio_obj_t *obj);
static int _listening_socket_read(eio_obj_t *obj, List objs);

struct io_operations listening_socket_ops = {
	readable:	&_listening_socket_readable,
	handle_read:	&_listening_socket_read
};

/**********************************************************************
 * IO server socket declarations
 **********************************************************************/
static bool _server_readable(eio_obj_t *obj);
static int _server_read(eio_obj_t *obj, List objs);
static bool _server_writable(eio_obj_t *obj);
static int _server_write(eio_obj_t *obj, List objs);

struct io_operations server_ops = {
        readable:	&_server_readable,
	handle_read:	&_server_read,
	writable:       &_server_writable,
	handle_write:   &_server_write
};

struct server_io_info {
	client_io_t *cio;
	int node_id;

	/* incoming variables */
	struct slurm_io_header header;
	struct io_buf *in_msg;
	int32_t in_remaining;
	bool in_eof;
	int remote_stdout_objs; /* active eio_obj_t's on the remote node */
	int remote_stderr_objs; /* active eio_obj_t's on the remote node */
	
	/* outgoing variables */
	List msg_queue;
	struct io_buf *out_msg;
	int32_t out_remaining;
	bool out_eof;
};

/**********************************************************************
 * File write declarations
 **********************************************************************/
static bool _file_writable(eio_obj_t *obj);
static int _file_write(eio_obj_t *obj, List objs);

struct io_operations file_write_ops = {
	writable:	&_file_writable,
	handle_write:	&_file_write,
};

struct file_write_info {
	client_io_t *cio;

	/* outgoing variables */
	List msg_queue;
	struct io_buf *out_msg;
	int32_t out_remaining;
	/* If taskid is (uint32_t)-1, output from all tasks is accepted,
	   otherwise only output from the specified task is accepted. */
	uint32_t taskid;
	uint32_t nodeid;
	bool eof;
};

/**********************************************************************
 * File read declarations
 **********************************************************************/
static bool _file_readable(eio_obj_t *obj);
static int _file_read(eio_obj_t *obj, List objs);

struct io_operations file_read_ops = {
	readable:	&_file_readable,
	handle_read:	&_file_read,
};

struct file_read_info {
	client_io_t *cio;

	/* header contains destination of file input */
	struct slurm_io_header header;
	uint32_t nodeid;

	bool eof;
};


/**********************************************************************
 * Listening socket functions
 **********************************************************************/
static bool 
_listening_socket_readable(eio_obj_t *obj)
{
	debug3("Called _listening_socket_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			close(obj->fd);
			obj->fd = -1;
		}
		debug2("  false, shutdown");
		return false;
	}
	return true;
}

static int
_listening_socket_read(eio_obj_t *obj, List objs)
{
	client_io_t *cio = (client_io_t *)obj->arg;

	debug3("Called _listening_socket_read");
	_handle_io_init_msg(obj->fd, cio);

	return (0);
}

static void
_set_listensocks_nonblocking(client_io_t *cio)
{
	int i;
	for (i = 0; i < cio->num_listen; i++) 
		fd_set_nonblocking(cio->listensock[i]);
}

/**********************************************************************
 * IO server socket functions
 **********************************************************************/
static eio_obj_t *
_create_server_eio_obj(int fd, client_io_t *cio, int nodeid,
		       int stdout_objs, int stderr_objs)
{
	struct server_io_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct server_io_info *)xmalloc(sizeof(struct server_io_info));
	info->cio = cio;
	info->node_id = nodeid;
	info->in_msg = NULL;
	info->in_remaining = 0;
	info->in_eof = false;
	info->remote_stdout_objs = stdout_objs;
	info->remote_stderr_objs = stderr_objs;
	info->msg_queue = list_create(NULL); /* FIXME! Add destructor */
	info->out_msg = NULL;
	info->out_remaining = 0;
	info->out_eof = false;

	eio = eio_obj_create(fd, &server_ops, (void *)info);

	return eio;
}

static bool 
_server_readable(eio_obj_t *obj)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;

	debug4("Called _server_readable");

	if (!_outgoing_buf_free(s->cio)) {
		debug4("  false, free_io_buf is empty");
		return false;
	}

	if (s->in_eof) {
		debug4("  false, eof");
		return false;
	}

	if (s->remote_stdout_objs > 0 || s->remote_stderr_objs > 0) {
		debug4("remote_stdout_objs = %d", s->remote_stdout_objs);
		debug4("remote_stderr_objs = %d", s->remote_stderr_objs);
		return true;	
	}

	if (obj->shutdown) {
		if (obj->fd != -1) {
			close(obj->fd);
			obj->fd = -1;
			s->in_eof = true;
			s->out_eof = true;
		}
		debug3("  false, shutdown");
		return false;
	}

	debug3("  false");
	return false;
}

static int
_server_read(eio_obj_t *obj, List objs)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;
	void *buf;
	int n;

	debug4("Entering _server_read");
	if (s->in_msg == NULL) {
		if (_outgoing_buf_free(s->cio)) {
			s->in_msg = list_dequeue(s->cio->free_outgoing);
		} else {
			debug("List free_outgoing is empty!");
			return SLURM_ERROR;
		}

		n = io_hdr_read_fd(obj->fd, &s->header);
		if (n <= 0) { /* got eof or error on socket read */
			if (s->cio->sls)
				step_launch_notify_io_failure(s->cio->sls, 
							      s->node_id);
			debug3("got error or unexpected eof "
			       "on _server_read header");
			close(obj->fd);
			obj->fd = -1;
			s->in_eof = true;
			s->out_eof = true;
			list_enqueue(s->cio->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		if (s->header.type == SLURM_IO_CONNECTION_TEST) {
			if (s->cio->sls)
				step_launch_clear_questionable_state(
					s->cio->sls, s->node_id);
			list_enqueue(s->cio->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;

		} else if (s->header.length == 0) { /* eof message */
			if (s->header.type == SLURM_IO_STDOUT) {
				s->remote_stdout_objs--;
				debug3(  "got eof-stdout msg on _server_read header");
			} else if (s->header.type == SLURM_IO_STDERR) {
				s->remote_stderr_objs--;
				debug3(  "got eof-stderr msg on _server_read header");
			} else
				error("Unrecognized output message type");
			list_enqueue(s->cio->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		s->in_remaining = s->header.length;
		s->in_msg->length = s->header.length;
		s->in_msg->header = s->header;
	}

	/*
	 * Read the body
	 */
	if (s->header.length != 0) {
		buf = s->in_msg->data + (s->in_msg->length - s->in_remaining);
	again:
		if ((n = read(obj->fd, buf, s->in_remaining)) < 0) {
			if (errno == EINTR)
				goto again;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return SLURM_SUCCESS;
			debug3("_server_read error: %m");
		}
		if (n <= 0) { /* got eof or unhandled error */
			if (s->cio->sls)
				step_launch_notify_io_failure(
					s->cio->sls, s->node_id);
			debug3("got error or unexpected eof "
			       "on _server_read body");
			close(obj->fd);
			obj->fd = -1;
			s->in_eof = true;
			s->out_eof = true;
			list_enqueue(s->cio->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}

		s->in_remaining -= n;
		if (s->in_remaining > 0)
			return SLURM_SUCCESS;
	}
	else {
		debug3("***** passing on eof message");
	}
	
	/*
	 * Route the message to the proper output
	 */
	{
		eio_obj_t *obj;
		struct file_write_info *info;

		s->in_msg->ref_count = 1;
		if (s->in_msg->header.type == SLURM_IO_STDOUT)
			obj = s->cio->stdout_obj;
		else
			obj = s->cio->stderr_obj;
		info = (struct file_write_info *) obj->arg;
		if (info->eof)
			/* this output is closed, discard message */
			list_enqueue(s->cio->free_outgoing, s->in_msg);
		else
			list_enqueue(info->msg_queue, s->in_msg);

		s->in_msg = NULL;
	}

	return SLURM_SUCCESS;
}

static bool 
_server_writable(eio_obj_t *obj)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;

	debug4("Called _server_writable");

	if (s->out_eof) {
		debug4("  false, eof");
		return false;
	}
	if (obj->shutdown == true) {
		debug4("  false, shutdown");
		return false;
	}
	if (s->out_msg != NULL
	    || !list_is_empty(s->msg_queue)) {
		debug4("  true, s->msg_queue length = %d",
		       list_count(s->msg_queue));
		return true;
	}

	debug4("  false");
	return false;
}

static int
_server_write(eio_obj_t *obj, List objs)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;
	void *buf;
	int n;

	debug4("Entering _server_write");

	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (s->out_msg == NULL) {
		s->out_msg = list_dequeue(s->msg_queue);
		if (s->out_msg == NULL) {
			debug3("_server_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		debug3("  dequeue successful, s->out_msg->length = %d", 
		       s->out_msg->length);
		s->out_remaining = s->out_msg->length;
	}

	debug3("  s->out_remaining = %d", s->out_remaining); 
	
	/*
	 * Write message to socket.
	 */
	buf = s->out_msg->data + (s->out_msg->length - s->out_remaining);
again:
	if ((n = write(obj->fd, buf, s->out_remaining)) < 0) {
		if (errno == EINTR) {
			goto again;
		} else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			debug3("  got EAGAIN in _server_write");
			return SLURM_SUCCESS;
		} else {
			error("_server_write write failed: %m");
			if (s->cio->sls)
				step_launch_notify_io_failure(s->cio->sls, 
							      s->node_id);
			s->out_eof = true;
			/* FIXME - perhaps we should free the message here? */
			return SLURM_ERROR;
		}
	}

	debug3("Wrote %d bytes to socket", n);
	s->out_remaining -= n;
	if (s->out_remaining > 0)
		return SLURM_SUCCESS;

	/*
	 * Free the message and prepare to send the next one.
	 */
	s->out_msg->ref_count--;
	if (s->out_msg->ref_count == 0) {
		pthread_mutex_lock(&s->cio->ioservers_lock);
		list_enqueue(s->cio->free_incoming, s->out_msg);
		pthread_mutex_unlock(&s->cio->ioservers_lock);
	} else
		debug3("  Could not free msg!!");
	s->out_msg = NULL;

	return SLURM_SUCCESS;
}

/**********************************************************************
 * File write functions
 **********************************************************************/
static eio_obj_t *
create_file_write_eio_obj(int fd, uint32_t taskid, uint32_t nodeid,
			  client_io_t *cio)
{
	struct file_write_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct file_write_info *)
		xmalloc(sizeof(struct file_write_info));
	info->cio = cio;
	info->msg_queue = list_create(NULL); /* FIXME! Add destructor */
	info->out_msg = NULL;
	info->out_remaining = 0;
	info->eof = false;
	info->taskid = taskid;
	info->nodeid = nodeid;

	eio = eio_obj_create(fd, &file_write_ops, (void *)info);

	return eio;
}


static bool _file_writable(eio_obj_t *obj)
{
	struct file_write_info *info = (struct file_write_info *) obj->arg;

	debug2("Called _file_writable");
	if (info->out_msg != NULL
	    || !list_is_empty(info->msg_queue))
		return true;

	debug3("  false");
	debug3("  eof is %s", info->eof ? "true" : "false");
	return false;
}

static int _file_write(eio_obj_t *obj, List objs)
{
	struct file_write_info *info = (struct file_write_info *) obj->arg;
	void *ptr;
	int n;

	debug2("Entering _file_write");
	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (info->out_msg == NULL) {
		info->out_msg = list_dequeue(info->msg_queue);
		if (info->out_msg == NULL) {
			debug3("_file_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		info->out_remaining = info->out_msg->length;
	}
	
	/*
	 * Write message to file.
	 */
	if (info->taskid != (uint32_t)-1
	    && info->out_msg->header.gtaskid != info->taskid) {
		/* we are ignoring messages not from info->taskid */
	} else if (!info->eof) {
		ptr = info->out_msg->data + (info->out_msg->length
					     - info->out_remaining);
		if ((n = write_labelled_message(obj->fd, ptr,
					        info->out_remaining,
					        info->out_msg->header.gtaskid,
					        info->cio->label,
					        info->cio->label_width)) < 0) {
			list_enqueue(info->cio->free_outgoing, info->out_msg);
			info->eof = true;
			return SLURM_ERROR;
		}
		debug3("  wrote %d bytes", n);
		info->out_remaining -= n;
		if (info->out_remaining > 0)
			return SLURM_SUCCESS;
	}

	/*
	 * Free the message.
	 */
	info->out_msg->ref_count--;
	if (info->out_msg->ref_count == 0)
		list_enqueue(info->cio->free_outgoing, info->out_msg);
	info->out_msg = NULL;
	debug2("Leaving  _file_write");

	return SLURM_SUCCESS;
}

/**********************************************************************
 * File read functions
 **********************************************************************/
static eio_obj_t *
create_file_read_eio_obj(int fd, uint32_t taskid, uint32_t nodeid,
			 client_io_t *cio)
{
	struct file_read_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct file_read_info *)
		xmalloc(sizeof(struct file_read_info));
	info->cio = cio;
	if (taskid == (uint32_t)-1) {
		info->header.type = SLURM_IO_ALLSTDIN;
		info->header.gtaskid = (uint16_t)-1;
	} else {
		info->header.type = SLURM_IO_STDIN;
		info->header.gtaskid = (uint16_t)taskid;
	}
	info->nodeid = nodeid;
	/* FIXME!  Need to set ltaskid based on gtaskid */
	info->header.ltaskid = (uint16_t)-1;
	info->eof = false;

	eio = eio_obj_create(fd, &file_read_ops, (void *)info);

	return eio;
}

static bool _file_readable(eio_obj_t *obj)
{
	struct file_read_info *info = (struct file_read_info *) obj->arg;

	debug2("Called _file_readable");

	if (info->cio->ioservers_ready < info->cio->num_nodes) {
		debug3("  false, all ioservers not yet initialized");
		return false;
	}

	if (info->eof) {
		debug3("  false, eof");
		return false;
	}
	if (obj->shutdown == true) {
		debug3("  false, shutdown");
		close(obj->fd);
		obj->fd = -1;
		info->eof = true;
		return false;
	}
	pthread_mutex_lock(&info->cio->ioservers_lock);
	if (_incoming_buf_free(info->cio)) {
		pthread_mutex_unlock(&info->cio->ioservers_lock);
		return true;
	}
	pthread_mutex_unlock(&info->cio->ioservers_lock);

	debug3("  false");
	return false;
}

static int _file_read(eio_obj_t *obj, List objs)
{
	struct file_read_info *info = (struct file_read_info *) obj->arg;
	struct io_buf *msg;
	io_hdr_t header;
	void *ptr;
	Buf packbuf;
	int len;

	debug2("Entering _file_read");
	pthread_mutex_lock(&info->cio->ioservers_lock);
	if (_incoming_buf_free(info->cio)) {
		msg = list_dequeue(info->cio->free_incoming);
	} else {
		debug3("  List free_incoming is empty, no file read");
		pthread_mutex_unlock(&info->cio->ioservers_lock);
		return SLURM_SUCCESS;
	}
	pthread_mutex_unlock(&info->cio->ioservers_lock);

	ptr = msg->data + io_hdr_packed_size();

again:
	if ((len = read(obj->fd, ptr, MAX_MSG_LEN)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug("_file_read returned %s",
				      errno==EAGAIN?"EAGAIN":"EWOULDBLOCK");
				pthread_mutex_lock(&info->cio->ioservers_lock);
				list_enqueue(info->cio->free_incoming, msg);
				pthread_mutex_unlock(&info->cio->ioservers_lock);
				return SLURM_SUCCESS;
			}
			/* Any other errors, we pretend we got eof */
			debug("Other error on _file_read: %m");
			len = 0;
	}
	if (len == 0) { /* got eof */
		debug3("got eof on _file_read");
		info->eof = true;
		/* send eof message, message with payload length 0 */
	}

	debug3("  read %d bytes from file", len);
	/*
	 * Pack header and build msg
	 */
	header = info->header;
	header.length = len;
	packbuf = create_buf(msg->data, io_hdr_packed_size());
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */
	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);
	debug3("  msg->length = %d", msg->length);

	/*
	 * Route the message to the correct IO servers
	 */
	if (header.type == SLURM_IO_ALLSTDIN) {
		int i;
		struct server_io_info *server;
		for (i = 0; i < info->cio->num_nodes; i++) {
			msg->ref_count++;
			if (info->cio->ioserver[i] == NULL)
				fatal("ioserver stream not yet initialized");
			server = info->cio->ioserver[i]->arg;
			list_enqueue(server->msg_queue, msg);
		}
	} else if (header.type == SLURM_IO_STDIN) {
		uint32_t nodeid;
		struct server_io_info *server;
		debug("SLURM_IO_STDIN");
		msg->ref_count = 1;
		nodeid = info->nodeid;
		debug3("  taskid %d maps to nodeid %ud", header.gtaskid, nodeid);
		if (nodeid == (uint32_t)-1) {
			error("A valid node id must be specified"
			      " for SLURM_IO_STDIN");
		} else {
			server = info->cio->ioserver[nodeid]->arg;
			list_enqueue(server->msg_queue, msg);
		}
	} else {
		fatal("Unsupported header.type");
	}
	msg = NULL;
	return SLURM_SUCCESS;
}


/**********************************************************************
 * General fuctions
 **********************************************************************/

static void *
_io_thr_internal(void *cio_arg)
{
	client_io_t *cio  = (client_io_t *) cio_arg;
	sigset_t set;

	xassert(cio != NULL);

	debug3("IO thread pid = %lu", (unsigned long) getpid());

	/* Block SIGHUP because it is interrupting file stream functions
	 * (fprintf, fflush, etc.) and causing data loss on stdout.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
 	pthread_sigmask(SIG_BLOCK, &set, NULL);

	_set_listensocks_nonblocking(cio);

	/* start the eio engine */
	eio_handle_mainloop(cio->eio);

	debug("IO thread exiting");

	return NULL;
}

static eio_obj_t *
_create_listensock_eio(int fd, client_io_t *cio)
{
	eio_obj_t *eio = NULL;

	eio = eio_obj_create(fd, &listening_socket_ops, (void *)cio);

	return eio;
}

static int
_read_io_init_msg(int fd, client_io_t *cio, char *host)
{
	struct slurm_io_init_msg msg;

	if (io_init_msg_read_from_fd(fd, &msg) != SLURM_SUCCESS) {
		error("failed reading io init message");
		goto fail;
	}
	if (io_init_msg_validate(&msg, cio->io_key) < 0) {
		goto fail; 
	}
	if (msg.nodeid >= cio->num_nodes) {
		error ("Invalid nodeid %d from %s", msg.nodeid, host);
		goto fail;
	}
	debug2("Validated IO connection from %s, node rank %u, sd=%d",
	       host, msg.nodeid, fd);

	net_set_low_water(fd, 1);
	debug3("msg.stdout_objs = %d", msg.stdout_objs);
	debug3("msg.stderr_objs = %d", msg.stderr_objs);
	/* sanity checks, just print warning */
	if (cio->ioserver[msg.nodeid] != NULL) {
		error("IO: Node %d already established stream!", msg.nodeid);
	} else if (bit_test(cio->ioservers_ready_bits, msg.nodeid)) {
		error("IO: Hey, you told me node %d was down!", msg.nodeid);
	}

	cio->ioserver[msg.nodeid] = _create_server_eio_obj(fd, cio, msg.nodeid,
							   msg.stdout_objs,
							   msg.stderr_objs);
	pthread_mutex_lock(&cio->ioservers_lock);
	bit_set(cio->ioservers_ready_bits, msg.nodeid);
	cio->ioservers_ready = bit_set_count(cio->ioservers_ready_bits);
	/* Normally using eio_new_initial_obj while the eio mainloop
	 * is running is not safe, but since this code is running
	 * inside of the eio mainloop there should be no problem.
	 */
	eio_new_initial_obj(cio->eio, cio->ioserver[msg.nodeid]);
	pthread_mutex_unlock(&cio->ioservers_lock);

	if (cio->sls)
		step_launch_clear_questionable_state(cio->sls, msg.nodeid);

	return SLURM_SUCCESS;

    fail:
	close(fd);
	return SLURM_ERROR;
}


static bool 
_is_fd_ready(int fd)
{
	struct pollfd pfd[1];
	int    rc;

	pfd[0].fd     = fd;
	pfd[0].events = POLLIN;

	rc = poll(pfd, 1, 10);

	return ((rc == 1) && (pfd[0].revents & POLLIN));
}


static void
_handle_io_init_msg(int fd, client_io_t *cio)
{
	int j;
	debug2("Activity on IO listening socket %d", fd);

	for (j = 0; j < 15; j++) {
		int sd;
		struct sockaddr addr;
		struct sockaddr_in *sin;
		socklen_t size = sizeof(addr);
		char buf[INET_ADDRSTRLEN];
		
		/* 
		 * Return early if fd is not now ready
		 */
		if (!_is_fd_ready(fd))
			return;

		while ((sd = accept(fd, &addr, &size)) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)	/* No more connections */
				return;
			if ((errno == ECONNABORTED) || 
			    (errno == EWOULDBLOCK)) {
				return;
			}
			error("Unable to accept new connection: %m\n");
			return;
		}

		sin = (struct sockaddr_in *) &addr;
		inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);

		debug3("Accepted IO connection: ip=%s sd=%d", buf, sd); 

		/*
		 * On AIX the new socket [sd] seems to inherit the O_NONBLOCK
		 * flag from the listening socket [fd], so we need to 
		 * explicitly set it back to blocking mode.
		 * (XXX: This should eventually be fixed by making
		 *  reads of IO headers nonblocking)
		 */
		fd_set_blocking(sd);

		/*
		 * Read IO header and update cio structure appropriately
		 */
		if (_read_io_init_msg(sd, cio, buf) < 0)
			continue;

		fd_set_nonblocking(sd);
	}
}

static int
_wid(int n)
{
	int width = 1;
	n--;    /* For zero origin */
	while (n /= 10)
		width++;
	return width;
}

static struct io_buf *
_alloc_io_buf(void)
{
	struct io_buf *buf;

	buf = (struct io_buf *)xmalloc(sizeof(struct io_buf));
	if (!buf)
		return NULL;
	buf->ref_count = 0;
	buf->length = 0;
	/* The following "+ 1" is just temporary so I can stick a \0 at
	   the end and do a printf of the data pointer */
	buf->data = xmalloc(MAX_MSG_LEN + io_hdr_packed_size() + 1);
	if (!buf->data) {
		xfree(buf);
		return NULL;
	}

	return buf;
}

#if 0
static void
_free_io_buf(struct io_buf *buf)
{
	if (buf) {
		if (buf->data)
			xfree(buf->data);
		xfree(buf);
	}
}
#endif

static void
_init_stdio_eio_objs(slurm_step_io_fds_t fds, client_io_t *cio)
{
	/*
	 * build stdin eio_obj_t
	 */
	if (fds.in.fd > -1) {
		fd_set_close_on_exec(fds.in.fd);
		cio->stdin_obj = create_file_read_eio_obj(
			fds.in.fd, fds.in.taskid, fds.in.nodeid, cio);
		eio_new_initial_obj(cio->eio, cio->stdin_obj);
	}

	/*
	 * build stdout eio_obj_t
	 */
	if (fds.out.fd > -1) {
		cio->stdout_obj = create_file_write_eio_obj(
			fds.out.fd, fds.out.taskid, fds.out.nodeid, cio);
		eio_new_initial_obj(cio->eio, cio->stdout_obj);
	}

	/*
	 * build a seperate stderr eio_obj_t only if stderr is not sharing
	 * the stdout file descriptor and task filtering option.
	 */
	if (fds.err.fd == fds.out.fd
	    && fds.err.taskid == fds.out.taskid
	    && fds.err.nodeid == fds.out.nodeid) {
		debug3("stdout and stderr sharing a file");
		cio->stderr_obj = cio->stdout_obj;
	} else {
		if (fds.err.fd > -1) {
			cio->stderr_obj = create_file_write_eio_obj(
				fds.err.fd, fds.err.taskid, fds.err.nodeid, cio);
			eio_new_initial_obj(cio->eio, cio->stderr_obj);
		}
	}
}

/* Callers of this function should already have locked cio->ioservers_lock */
static bool
_incoming_buf_free(client_io_t *cio)
{
	struct io_buf *buf;

	if (list_count(cio->free_incoming) > 0) {
		return true;
	} else if (cio->incoming_count < STDIO_MAX_FREE_BUF) {
		buf = _alloc_io_buf();
		if (buf != NULL) {
			list_enqueue(cio->free_incoming, buf);
			cio->incoming_count++;
			return true;
		}
	}
	return false;
}

static bool
_outgoing_buf_free(client_io_t *cio)
{
	struct io_buf *buf;

	if (list_count(cio->free_outgoing) > 0) {
		return true;
	} else if (cio->outgoing_count < STDIO_MAX_FREE_BUF) {
		buf = _alloc_io_buf();
		if (buf != NULL) {
			list_enqueue(cio->free_outgoing, buf);
			cio->outgoing_count++;
			return true;
		}
	}

	return false;
}

static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

client_io_t *
client_io_handler_create(slurm_step_io_fds_t fds,
			 int num_tasks,
			 int num_nodes,
			 slurm_cred_t *cred,
			 bool label)
{
	client_io_t *cio;
	int len;
	int i;
	uint32_t siglen;
	char *sig;

	cio = (client_io_t *)xmalloc(sizeof(client_io_t));
	if (cio == NULL)
		return NULL;

	cio->num_tasks = num_tasks;
	cio->num_nodes = num_nodes;

	cio->label = label;
	if (cio->label)
		cio->label_width = _wid(cio->num_tasks);
	else
		cio->label_width = 0;

	len = sizeof(uint32_t) * num_tasks;

	if (slurm_cred_get_signature(cred, &sig, &siglen) < 0) {
		error("client_io_handler_create, invalid credential");
		return NULL;
	}
	cio->io_key = (char *)xmalloc(siglen);
	memcpy(cio->io_key, sig, siglen);
	/* no need to free "sig", it is just a pointer into the credential */

	cio->eio = eio_handle_create();

	/* Compute number of listening sockets needed to allow
	 * all of the slurmds to establish IO streams with srun, without
	 * overstressing the TCP/IP backoff/retry algorithm
	 */
	cio->num_listen = _estimate_nports(num_nodes, 48);
	cio->listensock = (int *)xmalloc(cio->num_listen * sizeof(int));
	cio->listenport = (uint16_t *)xmalloc(cio->num_listen*sizeof(uint16_t));

	cio->ioserver = (eio_obj_t **)xmalloc(num_nodes*sizeof(eio_obj_t *));
	cio->ioservers_ready_bits = bit_alloc(num_nodes);
	cio->ioservers_ready = 0;
	pthread_mutex_init(&cio->ioservers_lock, NULL);

	_init_stdio_eio_objs(fds, cio);

	for (i = 0; i < cio->num_listen; i++) {
		eio_obj_t *obj;

		if (net_stream_listen(&cio->listensock[i],
				      (short *)&cio->listenport[i]) < 0) {
			fatal("unable to initialize stdio listen socket: %m");
		}
		debug("initialized stdio listening socket, port %d\n",
		      cio->listenport[i]);
		/*net_set_low_water(cio->listensock[i], 140);*/
		obj = _create_listensock_eio(cio->listensock[i], cio);
		eio_new_initial_obj(cio->eio, obj);
	}

	cio->free_incoming = list_create(NULL); /* FIXME! Needs destructor */
	cio->incoming_count = 0;
	for (i = 0; i < STDIO_MAX_FREE_BUF; i++) {
		list_enqueue(cio->free_incoming, _alloc_io_buf());
	}
	cio->free_outgoing = list_create(NULL); /* FIXME! Needs destructor */
	cio->outgoing_count = 0;
	for (i = 0; i < STDIO_MAX_FREE_BUF; i++) {
		list_enqueue(cio->free_outgoing, _alloc_io_buf());
	}
	cio->sls = NULL;

	return cio;
}

int
client_io_handler_start(client_io_t *cio)
{
	int retries = 0;
	pthread_attr_t attr;

	xsignal(SIGTTIN, SIG_IGN);

	slurm_attr_init(&attr);
	while ((errno = pthread_create(&cio->ioid, &attr,
				      &_io_thr_internal, (void *) cio))) {
		if (++retries > MAX_RETRIES) {
			error ("pthread_create error %m");
			slurm_attr_destroy(&attr);
			return SLURM_ERROR;
		}
		sleep(1);	/* sleep and try again */
	}
	slurm_attr_destroy(&attr);
	debug("Started IO server thread (%lu)", (unsigned long) cio->ioid);

	return SLURM_SUCCESS;
}

int
client_io_handler_finish(client_io_t *cio)
{
	eio_signal_shutdown(cio->eio);
	if (pthread_join(cio->ioid, NULL) < 0) {
		error("Waiting for client io pthread: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

void
client_io_handler_destroy(client_io_t *cio)
{
	xassert(cio);

	/* FIXME - perhaps should make certain that IO engine is shutdown
	   (by calling client_io_handler_finish()) before freeing anything */

	pthread_mutex_destroy(&cio->ioservers_lock);
	bit_free(cio->ioservers_ready_bits);
	xfree(cio->ioserver); /* need to destroy the obj first? */
	xfree(cio->listenport);
	xfree(cio->listensock);
	eio_handle_destroy(cio->eio);
	xfree(cio->io_key);
	xfree(cio);
}

void
client_io_handler_downnodes(client_io_t *cio,
			    const int* node_ids, int num_node_ids)
{
	int i;
	int node_id;

	if (cio == NULL)
		return;
	pthread_mutex_lock(&cio->ioservers_lock);
	for (i = 0; i < num_node_ids; i++) {
		node_id = node_ids[i];
		if (node_id >= cio->num_nodes || node_id < 0)
			continue;
		if (bit_test(cio->ioservers_ready_bits, node_id)
		    && cio->ioserver[node_id] != NULL) {
			cio->ioserver[node_id]->shutdown = true;
		} else {
			bit_set(cio->ioservers_ready_bits, node_id);
			cio->ioservers_ready =
				bit_set_count(cio->ioservers_ready_bits);
		}
	}
	pthread_mutex_unlock(&cio->ioservers_lock);

	eio_signal_wakeup(cio->eio);
}


void
client_io_handler_abort(client_io_t *cio)
{
	struct server_io_info *info;
	int i;

	pthread_mutex_lock(&cio->ioservers_lock);
	for (i = 0; i < cio->num_nodes; i++) {
		if (!bit_test(cio->ioservers_ready_bits, i)) {
			bit_set(cio->ioservers_ready_bits, i);
			cio->ioservers_ready =
				bit_set_count(cio->ioservers_ready_bits);
		} else if (cio->ioserver[i] != NULL) {
			info = (struct server_io_info *)cio->ioserver[i]->arg;
			/* Trick the server eio_obj_t into closing its
			 * connection. */
			info->remote_stdout_objs = 0;
			info->remote_stderr_objs = 0;
			cio->ioserver[i]->shutdown = true;
		}
	}
	pthread_mutex_unlock(&cio->ioservers_lock);
}


int client_io_handler_send_test_message(client_io_t *cio, int node_id, 
					bool *sent_message)
{
	struct io_buf *msg;
	io_hdr_t header;
	Buf packbuf;
	struct server_io_info *server;
	int rc = SLURM_SUCCESS;
	pthread_mutex_lock(&cio->ioservers_lock);

	server = (struct server_io_info *)cio->ioserver[node_id]->arg;
	if (sent_message)
		*sent_message = false;

	/* In this case, the I/O connection has not yet been established.
	   A problem might go undetected here, if a task appears to get 
	   launched correctly, but fails before it can make its I/O 
	   connection.  TODO:  Set a timer, see if the task has checked in
	   within some timeout, and abort the job if not. */
	if (cio->ioserver[node_id] == NULL) {
		goto done;
	}

	/* In this case, the I/O connection has closed and the task exited, 
	   so there's no need to send this test message. */
	if (server->out_eof || 
	    (server->remote_stdout_objs <= 0 && server->remote_stderr_objs <= 0)) {
		goto done;
	}

	/* 
	 * enqueue a test message, which would be ignored by the slurmstepd
	 */
	header.type = SLURM_IO_CONNECTION_TEST;
	header.gtaskid = 0;  /* Unused */
	header.ltaskid = 0;  /* Unused */
	header.length = 0;

	if (_incoming_buf_free(cio)) {
		msg = list_dequeue(cio->free_incoming);

		msg->length = io_hdr_packed_size();
		msg->ref_count = 1;
		msg->header = header;

		packbuf = create_buf(msg->data, io_hdr_packed_size());
		io_hdr_pack(&header, packbuf);
		/* free the Buf packbuf, but not the memory to which it points*/
		packbuf->head = NULL;
		free_buf(packbuf);

		list_enqueue( server->msg_queue, msg );

		if (eio_signal_wakeup(cio->eio) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
			goto done;
		}
		if (sent_message)
			*sent_message = true;
	} else {
		rc = SLURM_ERROR;
		goto done;
	}
done:
	pthread_mutex_unlock(&cio->ioservers_lock);
	return rc;
}







