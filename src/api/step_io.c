/****************************************************************************\
 *  step_io.c - process stdin, stdout, and stderr for parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
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

#define STDIO_MAX_FREE_BUF 1024

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
	io_hdr_t header;
};

typedef struct kill_thread {
	pthread_t thread_id;
	int       secs;
} kill_thread_t;

static struct io_buf *_alloc_io_buf(void);
static void	_init_stdio_eio_objs(slurm_step_io_fds_t fds,
				     client_io_t *cio);
static void	_handle_io_init_msg(int fd, client_io_t *cio);
static int _read_io_init_msg(int fd, client_io_t *cio, slurm_addr_t *host);
static int      _wid(int n);
static bool     _incoming_buf_free(client_io_t *cio);
static bool     _outgoing_buf_free(client_io_t *cio);

/**********************************************************************
 * Listening socket declarations
 **********************************************************************/
static bool _listening_socket_readable(eio_obj_t *obj);
static int _listening_socket_read(eio_obj_t *obj, List objs);

struct io_operations listening_socket_ops = {
	.readable = &_listening_socket_readable,
	.handle_read = &_listening_socket_read
};

/**********************************************************************
 * IO server socket declarations
 **********************************************************************/
static bool _server_readable(eio_obj_t *obj);
static int _server_read(eio_obj_t *obj, List objs);
static bool _server_writable(eio_obj_t *obj);
static int _server_write(eio_obj_t *obj, List objs);

struct io_operations server_ops = {
	.readable = &_server_readable,
	.handle_read = &_server_read,
	.writable = &_server_writable,
	.handle_write = &_server_write
};

struct server_io_info {
	client_io_t *cio;
	int node_id;
	bool testing_connection;

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
	.writable = &_file_writable,
	.handle_write = &_file_write,
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
	.readable = &_file_readable,
	.handle_read = &_file_read,
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
			if (obj->fd > STDERR_FILENO)
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
	eio_obj_t *eio = NULL;
	struct server_io_info *info = xmalloc(sizeof(*info));

	info->cio = cio;
	info->node_id = nodeid;
	info->testing_connection = false;
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

	if (s->remote_stdout_objs > 0 || s->remote_stderr_objs > 0 ||
	    s->testing_connection) {
		debug4("remote_stdout_objs = %d", s->remote_stdout_objs);
		debug4("remote_stderr_objs = %d", s->remote_stderr_objs);
		return true;
	}

	if (obj->shutdown) {
		if (obj->fd != -1) {
			if (obj->fd > STDERR_FILENO)
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
			if (n < 0) {	/* Error */
				if (obj->shutdown) {
					verbose("%s: Dropped pending I/O for terminated task",
						__func__);
				} else {
					if (getenv("SLURM_PTY_PORT") == NULL) {
						error("%s: fd %d error reading header: %m",
						      __func__, obj->fd);
					}
					if (s->cio->sls) {
						step_launch_notify_io_failure(
							s->cio->sls,
							s->node_id);
					}
				}
			}
			if (obj->fd > STDERR_FILENO)
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
			s->testing_connection = false;
			return SLURM_SUCCESS;

		} else if (s->header.length == 0) { /* eof message */
			if (s->header.type == SLURM_IO_STDOUT) {
				s->remote_stdout_objs--;
				debug3( "got eof-stdout msg on _server_read "
					"header");
			} else if (s->header.type == SLURM_IO_STDERR) {
				s->remote_stderr_objs--;
				debug3( "got eof-stderr msg on _server_read "
					"header");
			} else
				error("Unrecognized output message type");
			/* If all remote eios are gone, shutdown
			 * the i/o channel with stepd.
			 */
			if (s->remote_stdout_objs == 0
				&& s->remote_stderr_objs == 0) {
				obj->shutdown = true;
			}
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
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				return SLURM_SUCCESS;
			if (errno == ECONNRESET) {
				/* The slurmstepd writes the message (header
				 * plus data) in a single write(). We read the
				 * header above OK, but the data can't be read.
				 * I've confirmed the full write completes and
				 * the file is closed at slurmstepd shutdown.
				 * The reason for this error is unknown. -Moe */
				debug("Stdout/err from task %u may be "
				      "incomplete due to a network error",
				      s->header.gtaskid);
			} else {
				debug3("_server_read error: %m");
			}
		}
		if (n <= 0) { /* got eof or unhandled error */
			error("%s: fd %d got error or unexpected eof reading message body",
				  __func__, obj->fd);
			if (s->cio->sls)
				step_launch_notify_io_failure(
					s->cio->sls, s->node_id);
			if (obj->fd > STDERR_FILENO)
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
		slurm_mutex_lock(&s->cio->ioservers_lock);
		list_enqueue(s->cio->free_incoming, s->out_msg);
		slurm_mutex_unlock(&s->cio->ioservers_lock);
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
	eio_obj_t *eio = NULL;
	struct file_write_info *info = xmalloc(sizeof(*info));

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

	debug2("Entering %s", __func__);
	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (info->out_msg == NULL) {
		info->out_msg = list_dequeue(info->msg_queue);
		if (info->out_msg == NULL) {
			debug3("%s: nothing in the queue", __func__);
			return SLURM_SUCCESS;
		}
		info->out_remaining = info->out_msg->length;
	}

	/*
	 * Write message to file.
	 */
	if ((info->taskid != (uint32_t) -1) &&
	    (info->out_msg->header.gtaskid != info->taskid)) {
		/* we are ignoring messages not from info->taskid */
	} else if (!info->eof) {
		ptr = info->out_msg->data + (info->out_msg->length
					     - info->out_remaining);
		if ((n = write_labelled_message(obj->fd, ptr,
					        info->out_remaining,
					        info->out_msg->header.gtaskid,
					        info->cio->het_job_offset,
					        info->cio->het_job_task_offset,
					        info->cio->label,
					        info->cio->taskid_width)) < 0) {
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
	debug2("Leaving  %s", __func__);

	return SLURM_SUCCESS;
}

/**********************************************************************
 * File read functions
 **********************************************************************/
static eio_obj_t *
create_file_read_eio_obj(int fd, uint32_t taskid, uint32_t nodeid,
			 client_io_t *cio)
{
	eio_obj_t *eio = NULL;
	struct file_read_info *info = xmalloc(sizeof(*info));

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
	struct file_read_info *read_info = (struct file_read_info *) obj->arg;

	debug2("Called _file_readable");

	if (read_info->cio->ioservers_ready < read_info->cio->num_nodes) {
		debug3("  false, all ioservers not yet initialized");
		return false;
	}

	if (read_info->eof) {
		debug3("  false, eof");
		return false;
	}
	if (obj->shutdown == true) {
		debug3("  false, shutdown");
		if (obj->fd > STDERR_FILENO)
			close(obj->fd);
		obj->fd = -1;
		read_info->eof = true;
		return false;
	}
	slurm_mutex_lock(&read_info->cio->ioservers_lock);
	if (_incoming_buf_free(read_info->cio)) {
		slurm_mutex_unlock(&read_info->cio->ioservers_lock);
		return true;
	}
	slurm_mutex_unlock(&read_info->cio->ioservers_lock);

	debug3("  false");
	return false;
}

static int _file_read(eio_obj_t *obj, List objs)
{
	struct file_read_info *info = (struct file_read_info *) obj->arg;
	struct io_buf *msg;
	io_hdr_t header;
	void *ptr;
	buf_t *packbuf;
	int len;

	debug2("Entering _file_read");
	slurm_mutex_lock(&info->cio->ioservers_lock);
	if (_incoming_buf_free(info->cio)) {
		msg = list_dequeue(info->cio->free_incoming);
	} else {
		debug3("  List free_incoming is empty, no file read");
		slurm_mutex_unlock(&info->cio->ioservers_lock);
		return SLURM_SUCCESS;
	}
	slurm_mutex_unlock(&info->cio->ioservers_lock);

	ptr = msg->data + io_hdr_packed_size();

again:
	if ((len = read(obj->fd, ptr, MAX_MSG_LEN)) < 0) {
		if (errno == EINTR)
			goto again;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			debug("_file_read returned %s",
			      errno==EAGAIN?"EAGAIN":"EWOULDBLOCK");
			slurm_mutex_lock(&info->cio->ioservers_lock);
			list_enqueue(info->cio->free_incoming, msg);
			slurm_mutex_unlock(&info->cio->ioservers_lock);
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
	/* free the packbuf structure, but not the memory to which it points */
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
				/* client_io_handler_abort() or
				 * client_io_handler_downnodes() called */
				verbose("ioserver stream of node %d not yet "
					"initialized", i);
			else {
				server = info->cio->ioserver[i]->arg;
				list_enqueue(server->msg_queue, msg);
			}
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

static int _read_io_init_msg(int fd, client_io_t *cio, slurm_addr_t *host)
{
	io_init_msg_t msg = { 0 };

	if (io_init_msg_read_from_fd(fd, &msg) != SLURM_SUCCESS) {
		error("failed reading io init message");
		goto fail;
	}
	if (io_init_msg_validate(&msg, cio->io_key, cio->io_key_len) < 0) {
		goto fail;
	}
	if (msg.nodeid >= cio->num_nodes) {
		error ("Invalid nodeid %d from %pA", msg.nodeid, host);
		goto fail;
	}
	debug2("Validated IO connection from %pA, node rank %u, sd=%d",
	       host, msg.nodeid, fd);

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
	slurm_mutex_lock(&cio->ioservers_lock);
	bit_set(cio->ioservers_ready_bits, msg.nodeid);
	cio->ioservers_ready = bit_set_count(cio->ioservers_ready_bits);
	/*
	 * Normally using eio_new_initial_obj while the eio mainloop
	 * is running is not safe, but since this code is running
	 * inside of the eio mainloop there should be no problem.
	 */
	eio_new_initial_obj(cio->eio, cio->ioserver[msg.nodeid]);
	slurm_mutex_unlock(&cio->ioservers_lock);

	if (cio->sls)
		step_launch_clear_questionable_state(cio->sls, msg.nodeid);

	xfree(msg.io_key);
	return SLURM_SUCCESS;

    fail:
	xfree(msg.io_key);
	if (fd > STDERR_FILENO)
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
		slurm_addr_t addr;

		/*
		 * Return early if fd is not now ready
		 */
		if (!_is_fd_ready(fd))
			return;

		while ((sd = slurm_accept_msg_conn(fd, &addr)) < 0) {
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

		debug3("Accepted IO connection: ip=%pA sd=%d", &addr, sd);

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
		if (_read_io_init_msg(sd, cio, &addr) < 0)
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
	struct io_buf *buf = xmalloc(sizeof(*buf));

	buf->ref_count = 0;
	buf->length = 0;
	/* The following "+ 1" is just temporary so I can stick a \0 at
	   the end and do a printf of the data pointer */
	buf->data = xmalloc(MAX_MSG_LEN + io_hdr_packed_size() + 1);

	return buf;
}

static void _free_io_buf(void *ptr)
{
	struct io_buf *buf = (struct io_buf *) ptr;

	if (!buf)
		return;

	xfree(buf->data);
	xfree(buf);
}

static void
_init_stdio_eio_objs(slurm_step_io_fds_t fds, client_io_t *cio)
{
	/*
	 * build stdin eio_obj_t
	 */
	if (fds.input.fd > -1) {
		fd_set_close_on_exec(fds.input.fd);
		cio->stdin_obj = create_file_read_eio_obj(
			fds.input.fd, fds.input.taskid, fds.input.nodeid, cio);
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
				fds.err.fd, fds.err.taskid,
				fds.err.nodeid, cio);
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

client_io_t *client_io_handler_create(slurm_step_io_fds_t fds, int num_tasks,
				      int num_nodes, slurm_cred_t *cred,
				      bool label, uint32_t het_job_offset,
				      uint32_t het_job_task_offset)
{
	int i;
	uint32_t siglen;
	char *sig;
	uint16_t *ports;
	client_io_t *cio = xmalloc(sizeof(*cio));

	cio->num_tasks   = num_tasks;
	cio->num_nodes   = num_nodes;
	cio->het_job_offset = het_job_offset;
	cio->het_job_task_offset = het_job_task_offset;

	cio->label = label;
	if (cio->label)
		cio->taskid_width = _wid(cio->num_tasks);
	else
		cio->taskid_width = 0;

	if (slurm_cred_get_signature(cred, &sig, &siglen) < 0) {
		error("%s: invalid credential", __func__);
		return NULL;
	}
	cio->io_key = xmalloc(siglen);
	cio->io_key_len = siglen;
	memcpy(cio->io_key, sig, siglen);
	/* no need to free "sig", it is just a pointer into the credential */

	cio->eio = eio_handle_create(slurm_conf.eio_timeout);

	/* Compute number of listening sockets needed to allow
	 * all of the slurmds to establish IO streams with srun, without
	 * overstressing the TCP/IP backoff/retry algorithm
	 */
	cio->num_listen = _estimate_nports(num_nodes, 48);
	cio->listensock = xcalloc(cio->num_listen, sizeof(int));
	cio->listenport = xcalloc(cio->num_listen, sizeof(uint16_t));

	cio->ioserver = xcalloc(num_nodes, sizeof(eio_obj_t *));
	cio->ioservers_ready_bits = bit_alloc(num_nodes);
	cio->ioservers_ready = 0;
	slurm_mutex_init(&cio->ioservers_lock);

	_init_stdio_eio_objs(fds, cio);
	ports = slurm_get_srun_port_range();

	for (i = 0; i < cio->num_listen; i++) {
		eio_obj_t *obj;
		int cc;

		if (ports)
			cc = net_stream_listen_ports(&cio->listensock[i],
						     &cio->listenport[i],
						     ports, false);
		else
			cc = net_stream_listen(&cio->listensock[i],
					       &cio->listenport[i]);
		if (cc < 0) {
			fatal("unable to initialize stdio listen socket: %m");
		}
		debug("initialized stdio listening socket, port %d",
		      cio->listenport[i]);
		obj = _create_listensock_eio(cio->listensock[i], cio);
		eio_new_initial_obj(cio->eio, obj);
	}

	cio->free_incoming = list_create(_free_io_buf);
	cio->incoming_count = 0;
	for (i = 0; i < STDIO_MAX_FREE_BUF; i++) {
		list_enqueue(cio->free_incoming, _alloc_io_buf());
	}
	cio->free_outgoing = list_create(_free_io_buf);
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
	xsignal(SIGTTIN, SIG_IGN);

	slurm_thread_create(&cio->ioid, _io_thr_internal, cio);

	debug("Started IO server thread (%lu)", (unsigned long) cio->ioid);

	return SLURM_SUCCESS;
}

static void *_kill_thr(void *args)
{
	kill_thread_t *kt = ( kill_thread_t *) args;
	unsigned int pause = kt->secs;
	do {
		pause = sleep(pause);
	} while (pause > 0);
	pthread_cancel(kt->thread_id);
	xfree(kt);
	return NULL;
}

static void _delay_kill_thread(pthread_t thread_id, int secs)
{
	kill_thread_t *kt = xmalloc(sizeof(kill_thread_t));

	kt->thread_id = thread_id;
	kt->secs = secs;
	slurm_thread_create_detached(NULL, _kill_thr, kt);
}

int
client_io_handler_finish(client_io_t *cio)
{
	if (cio == NULL)
		return SLURM_SUCCESS;

	eio_signal_shutdown(cio->eio);
	/* Make the thread timeout consistent with
	 * EIO_SHUTDOWN_WAIT
	 */
	_delay_kill_thread(cio->ioid, 180);
	if (pthread_join(cio->ioid, NULL) < 0) {
		error("Waiting for client io pthread: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

void
client_io_handler_destroy(client_io_t *cio)
{
	if (cio == NULL)
		return;

	/* FIXME - perhaps should make certain that IO engine is shutdown
	   (by calling client_io_handler_finish()) before freeing anything */

	slurm_mutex_destroy(&cio->ioservers_lock);
	FREE_NULL_BITMAP(cio->ioservers_ready_bits);
	xfree(cio->ioserver); /* need to destroy the obj first? */
	xfree(cio->listenport);
	xfree(cio->listensock);
	eio_handle_destroy(cio->eio);
	xfree(cio->io_key);
	FREE_NULL_LIST(cio->free_incoming);
	FREE_NULL_LIST(cio->free_outgoing);
	xfree(cio);
}

void
client_io_handler_downnodes(client_io_t *cio,
			    const int* node_ids, int num_node_ids)
{
	int i;
	int node_id;
	struct server_io_info *info;
	void *tmp;

	if (cio == NULL)
		return;

	slurm_mutex_lock(&cio->ioservers_lock);
	for (i = 0; i < num_node_ids; i++) {
		node_id = node_ids[i];
		if (node_id >= cio->num_nodes || node_id < 0)
			continue;
		if (bit_test(cio->ioservers_ready_bits, node_id)
		    && cio->ioserver[node_id] != NULL) {
			tmp = cio->ioserver[node_id]->arg;
			info = (struct server_io_info *)tmp;
			info->remote_stdout_objs = 0;
			info->remote_stderr_objs = 0;
			info->testing_connection = false;
			cio->ioserver[node_id]->shutdown = true;
		} else {
			bit_set(cio->ioservers_ready_bits, node_id);
			cio->ioservers_ready =
				bit_set_count(cio->ioservers_ready_bits);
		}
	}
	slurm_mutex_unlock(&cio->ioservers_lock);

	eio_signal_wakeup(cio->eio);
}


void
client_io_handler_abort(client_io_t *cio)
{
	struct server_io_info *io_info;
	int i;

	if (cio == NULL)
		return;
	slurm_mutex_lock(&cio->ioservers_lock);
	for (i = 0; i < cio->num_nodes; i++) {
		if (!bit_test(cio->ioservers_ready_bits, i)) {
			bit_set(cio->ioservers_ready_bits, i);
			cio->ioservers_ready =
				bit_set_count(cio->ioservers_ready_bits);
		} else if (cio->ioserver[i] != NULL) {
			io_info = (struct server_io_info *)cio->ioserver[i]->arg;
			/* Trick the server eio_obj_t into closing its
			 * connection. */
			io_info->remote_stdout_objs = 0;
			io_info->remote_stderr_objs = 0;
			io_info->testing_connection = false;
			cio->ioserver[i]->shutdown = true;
		}
	}
	slurm_mutex_unlock(&cio->ioservers_lock);
}


int client_io_handler_send_test_message(client_io_t *cio, int node_id,
					bool *sent_message)
{
	struct io_buf *msg;
	io_hdr_t header;
	buf_t *packbuf;
	struct server_io_info *server;
	int rc = SLURM_SUCCESS;
	slurm_mutex_lock(&cio->ioservers_lock);

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
	server = (struct server_io_info *)cio->ioserver[node_id]->arg;

	/* In this case, the I/O connection has closed so can't send a test
	   message.  This error case is handled elsewhere. */
	if (server->out_eof) {
		goto done;
	}

	/*
	 * enqueue a test message, which would be ignored by the slurmstepd
	 */
	memset(&header, 0, sizeof(header));
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
		/* free the packbuf, but not the memory to which it points */
		packbuf->head = NULL;
		free_buf(packbuf);

		list_enqueue( server->msg_queue, msg );

		if (eio_signal_wakeup(cio->eio) != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
			goto done;
		}
		server->testing_connection = true;
		if (sent_message)
			*sent_message = true;
	} else {
		rc = SLURM_ERROR;
		goto done;
	}
done:
	slurm_mutex_unlock(&cio->ioservers_lock);
	return rc;
}
