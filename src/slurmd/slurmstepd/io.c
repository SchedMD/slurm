/*****************************************************************************\
 * src/slurmd/slurmstepd/io.c - Standard I/O handling routines for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE /* for setresuid(3) */

#ifdef HAVE_PTY_H
#  include <pty.h>
#endif

#ifdef HAVE_UTMP_H
#  include <utmp.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "src/common/cbuf.h"
#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/io_hdr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/write_labelled_message.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/fname.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"

/**********************************************************************
 * IO client socket declarations
 **********************************************************************/
static bool _client_readable(eio_obj_t *);
static bool _client_writable(eio_obj_t *);
static int  _client_read(eio_obj_t *, List);
static int  _client_write(eio_obj_t *, List);

struct io_operations client_ops = {
	.readable = &_client_readable,
	.writable = &_client_writable,
	.handle_read = &_client_read,
	.handle_write = &_client_write,
};

#define CLIENT_IO_MAGIC 0x10102
struct client_io_info {
	int                   magic;
	stepd_step_rec_t *step; /* pointer back to step data */

	/* incoming variables */
	struct slurm_io_header header;
	struct io_buf *in_msg;
	int32_t in_remaining;
	bool in_eof;

	/* outgoing variables */
	List msg_queue;
	struct io_buf *out_msg;
	int32_t out_remaining;
	bool out_eof;

	/* For clients that only write stdout or stderr, and/or only
	   write for one task. -1 means accept output from any task. */
	int  ltaskid_stdout, ltaskid_stderr;
	bool labelio;
	int  taskid_width;

	/* true if writing to a file, false if writing to a socket */
	bool is_local_file;
};


static bool _local_file_writable(eio_obj_t *);
static int  _local_file_write(eio_obj_t *, List);

struct io_operations local_file_ops = {
	.writable = &_local_file_writable,
	.handle_write = &_local_file_write,
};


/**********************************************************************
 * Task write declarations
 **********************************************************************/
static bool _task_writable(eio_obj_t *);
static int  _task_write(eio_obj_t *, List);
static int _task_write_error(eio_obj_t *obj, List objs);

struct io_operations task_write_ops = {
	.writable = &_task_writable,
	.handle_write = &_task_write,
	.handle_error = &_task_write_error,
};

#define TASK_IN_MAGIC 0x10103
struct task_write_info {
	int              magic;
	stepd_step_rec_t *step; /* pointer back to step data */

	List msg_queue;
	struct io_buf *msg;
	int32_t remaining;
};

/**********************************************************************
 * Task read declarations
 **********************************************************************/
static bool _task_readable(eio_obj_t *);
static int  _task_read(eio_obj_t *, List);

struct io_operations task_read_ops = {
	.readable = &_task_readable,
	.handle_read = &_task_read,
};

#define TASK_OUT_MAGIC  0x10103
struct task_read_info {
	int              magic;
	uint16_t         type;           /* type of IO object          */
	uint16_t         gtaskid;
	uint16_t         ltaskid;
	stepd_step_rec_t *step; /* pointer back to step data */
	cbuf_t          *buf;
	bool		 eof;
	bool		 eof_msg_sent;
};

/**********************************************************************
 * Pseudo terminal declarations
 **********************************************************************/
struct window_info {
	stepd_step_task_info_t *task;
	stepd_step_rec_t *step;
	int pty_fd;
};
#ifdef HAVE_PTY_H
static void  _spawn_window_manager(stepd_step_task_info_t *task, stepd_step_rec_t *step);
static void *_window_manager(void *arg);
#endif

/**********************************************************************
 * General declarations
 **********************************************************************/
static void *_io_thr(void *);
static int _send_io_init_msg(int sock, srun_info_t *srun, stepd_step_rec_t *step,
			     bool init);
static void _send_eof_msg(struct task_read_info *out);
static struct io_buf *_task_build_message(struct task_read_info *out,
					  stepd_step_rec_t *step, cbuf_t *cbuf);
static void *_io_thr(void *arg);
static void _route_msg_task_to_client(eio_obj_t *obj);
static void _free_outgoing_msg(struct io_buf *msg, stepd_step_rec_t *step);
static void _free_incoming_msg(struct io_buf *msg, stepd_step_rec_t *step);
static void _free_all_outgoing_msgs(List msg_queue, stepd_step_rec_t *step);
static bool _incoming_buf_free(stepd_step_rec_t *step);
static bool _outgoing_buf_free(stepd_step_rec_t *step);
static int  _send_connection_okay_response(stepd_step_rec_t *step);
static struct io_buf *_build_connection_okay_message(stepd_step_rec_t *step);

/**********************************************************************
 * IO client socket functions
 **********************************************************************/
static bool
_client_readable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	debug5("Called _client_readable");
	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->in_eof) {
		debug5("  false, in_eof");
		/* We no longer want the _client_read() function to handle
		   errors on write now that the read side of the connection
		   is closed.  Setting handle_read to NULL will result in
		   the _client_write function handling errors, and closing
		   down the write end of the connection. */
		obj->ops->handle_read = NULL;
		return false;
	}

	if (obj->shutdown) {
		debug5("  false, shutdown");
		shutdown(obj->fd, SHUT_RD);
		client->in_eof = true;
		return false;
	}

	if (client->in_msg != NULL
	    || _incoming_buf_free(client->step))
		return true;

	debug5("  false");
	return false;
}

static bool
_client_writable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	debug5("Called _client_writable");
	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->out_eof == true) {
		debug5("  false, out_eof");
		return false;
	}

	/* If this is a newly attached client its msg_queue needs
	 * to be initialized from the outgoing_cache, and then "obj" needs
	 * to be added to the List of clients.
	 */
	if (client->msg_queue == NULL) {
		ListIterator msgs;
		struct io_buf *msg;
		client->msg_queue = list_create(NULL); /* need destructor */
		msgs = list_iterator_create(client->step->outgoing_cache);
		while ((msg = list_next(msgs))) {
			msg->ref_count++;
			list_enqueue(client->msg_queue, msg);
		}
		list_iterator_destroy(msgs);
		/* and now make this object visible to tasks */
		list_append(client->step->clients, (void *)obj);
	}

	if (client->out_msg != NULL)
		debug5("  client->out.msg != NULL");
	if (!list_is_empty(client->msg_queue))
		debug5("  client->out.msg_queue queue length = %d",
		       list_count(client->msg_queue));

	if (client->out_msg != NULL
	    || !list_is_empty(client->msg_queue))
		return true;

	debug5("  false");
	return false;
}

static int
_client_read(eio_obj_t *obj, List objs)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;
	void *buf;
	int n;

	debug4("Entering _client_read");
	xassert(client->magic == CLIENT_IO_MAGIC);

	/*
	 * Read the header, if a message read is not already in progress
	 */
	if (client->in_msg == NULL) {
		if (_incoming_buf_free(client->step)) {
			client->in_msg =
				list_dequeue(client->step->free_incoming);
		} else {
			debug5("  _client_read free_incoming is empty");
			return SLURM_SUCCESS;
		}
		n = io_hdr_read_fd(obj->fd, &client->header);
		if (n <= 0) { /* got eof or fatal error */
			debug5("  got eof or error _client_read header, n=%d", n);
			client->in_eof = true;
			list_enqueue(client->step->free_incoming,
				     client->in_msg);
			client->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		debug5("client->header.length = %u", client->header.length);
		if (client->header.length > MAX_MSG_LEN)
			error("Message length of %u exceeds maximum of %u",
			      client->header.length, MAX_MSG_LEN);
		client->in_remaining = client->header.length;
		client->in_msg->length = client->header.length;
	}

	/*
	 * Read the body
	 */
	if (client->header.type == SLURM_IO_CONNECTION_TEST) {
		if (client->header.length != 0) {
			debug5("  error in _client_read: bad connection test");
			list_enqueue(client->step->free_incoming,
				     client->in_msg);
			client->in_msg = NULL;
			return SLURM_ERROR;
		}
		if (_send_connection_okay_response(client->step)) {
			/*
			 * If we get here because of a failed
			 * _send_connection_okay_response, it's because of a
			 * lack of buffer space in the output queue.  Just
			 * keep the current input message client->in_msg in
			 * place, and resend on the next call.
			 */
			return SLURM_SUCCESS;
		}
		list_enqueue(client->step->free_incoming, client->in_msg);
		client->in_msg = NULL;
		return SLURM_SUCCESS;
	} else if (client->header.length == 0) { /* zero length is an eof message */
		debug5("  got stdin eof message!");
	} else {
		buf = client->in_msg->data +
			(client->in_msg->length - client->in_remaining);
	again:
		if ((n = read(obj->fd, buf, client->in_remaining)) < 0) {
			if (errno == EINTR)
				goto again;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				debug5("_client_read returned %s",
					errno == EAGAIN ? "EAGAIN" : "EWOULDBLOCK");
				return SLURM_SUCCESS;
			}
			debug5("  error in _client_read: %m");
		}
		if (n <= 0) { /* got eof (or unhandled error) */
			debug5("  got eof on _client_read body");
			client->in_eof = true;
			list_enqueue(client->step->free_incoming,
				     client->in_msg);
			client->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		client->in_remaining -= n;
		if (client->in_remaining > 0)
			return SLURM_SUCCESS;
/* 		*(char *)(buf + n) = '\0'; */
/* 		debug5("\"%s\"", buf); */
	}

	/*
	 * Route the message to its destination(s)
	 */
	if (client->header.type != SLURM_IO_STDIN
	    && client->header.type != SLURM_IO_ALLSTDIN) {
		error("Input client->header.type is not valid!");
		client->in_msg = NULL;
		return SLURM_ERROR;
	} else {
		int i;
		stepd_step_task_info_t *task;
		struct task_write_info *io;

		client->in_msg->ref_count = 0;
		if (client->header.type == SLURM_IO_ALLSTDIN) {
			for (i = 0; i < client->step->node_tasks; i++) {
				task = client->step->task[i];
				io = (struct task_write_info *)task->in->arg;
				client->in_msg->ref_count++;
				list_enqueue(io->msg_queue, client->in_msg);
			}
			debug5("  message ref_count = %d", client->in_msg->ref_count);
		} else {
			for (i = 0; i < client->step->node_tasks; i++) {
				task = client->step->task[i];
				if (task->in == NULL)
					continue;
				io = (struct task_write_info *)task->in->arg;
				if (task->gtid != client->header.gtaskid)
					continue;
				client->in_msg->ref_count++;
				list_enqueue(io->msg_queue, client->in_msg);
				break;
			}
		}
	}
	client->in_msg = NULL;
	debug4("Leaving  _client_read");
	return SLURM_SUCCESS;
}

/*
 * Write outgoing packed messages to the client socket.
 */
static int
_client_write(eio_obj_t *obj, List objs)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;
	void *buf;
	int n;

	xassert(client->magic == CLIENT_IO_MAGIC);

	debug4("Entering _client_write");

	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (client->out_msg == NULL) {
		client->out_msg = list_dequeue(client->msg_queue);
		if (client->out_msg == NULL) {
			debug5("_client_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		debug5("  dequeue successful, client->out_msg->length = %d",
			client->out_msg->length);
		client->out_remaining = client->out_msg->length;
	}

	debug5("  client->out_remaining = %d", client->out_remaining);

	/*
	 * Write message to socket.
	 */
	buf = client->out_msg->data +
		(client->out_msg->length - client->out_remaining);
again:
	if ((n = write(obj->fd, buf, client->out_remaining)) < 0) {
		if (errno == EINTR) {
			goto again;
		} else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			debug5("_client_write returned EAGAIN");
			return SLURM_SUCCESS;
		} else {
			client->out_eof = true;
			_free_all_outgoing_msgs(client->msg_queue,
						client->step);
			return SLURM_SUCCESS;
		}
	}
	if (n < client->out_remaining) {
		error("Only wrote %d of %d bytes to socket",
		      n, client->out_remaining);
	} else
		debug5("Wrote %d bytes to socket", n);
	client->out_remaining -= n;
	if (client->out_remaining > 0)
		return SLURM_SUCCESS;

	_free_outgoing_msg(client->out_msg, client->step);
	client->out_msg = NULL;

	return SLURM_SUCCESS;
}


static bool
_local_file_writable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->out_eof == true)
		return false;

	if (client->out_msg != NULL || !list_is_empty(client->msg_queue))
		return true;

	return false;
}


/*
 * The slurmstepd writes I/O to a file, possibly adding a label.
 */
static int
_local_file_write(eio_obj_t *obj, List objs)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;
	void *buf;
	int n;
	struct slurm_io_header header;
	buf_t *header_tmp_buf;

	xassert(client->magic == CLIENT_IO_MAGIC);
	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (client->out_msg == NULL) {
		client->out_msg = list_dequeue(client->msg_queue);
		if (client->out_msg == NULL) {
			return SLURM_SUCCESS;
		}
		client->out_remaining = client->out_msg->length -
					io_hdr_packed_size();
	}

	/*
	 * This code to make a buffer, fill it, unpack its contents, and free
	 * it is just used to read the header to get the global task id.
	 */
	header_tmp_buf = create_buf(client->out_msg->data,
				    client->out_msg->length);
	if (!header_tmp_buf) {
		fatal("Failure to allocate memory for a message header");
		return SLURM_ERROR;	/* Fix CLANG false positive error */
	}
	io_hdr_unpack(&header, header_tmp_buf);
	header_tmp_buf->head = NULL;	/* CLANG false positive bug here */
	FREE_NULL_BUFFER(header_tmp_buf);

	/*
	 * A zero-length message indicates the end of a stream from one
	 * of the tasks.  Just free the message and return.
	 */
	if (header.length == 0) {
		_free_outgoing_msg(client->out_msg, client->step);
		client->out_msg = NULL;
		return SLURM_SUCCESS;
	}

	/* Write the message to the file. */
	buf = client->out_msg->data +
		(client->out_msg->length - client->out_remaining);
	n = write_labelled_message(obj->fd, buf, client->out_remaining,
				   header.gtaskid, client->step->het_job_offset,
				   client->step->het_job_task_offset,
				   client->labelio, client->taskid_width);
	if (n < 0) {
		client->out_eof = true;
		_free_all_outgoing_msgs(client->msg_queue, client->step);
		return SLURM_ERROR;
	}

	client->out_remaining -= n;
	if (client->out_remaining == 0) {
		_free_outgoing_msg(client->out_msg, client->step);
		client->out_msg = NULL;
	}
	return SLURM_SUCCESS;
}




/**********************************************************************
 * Task write functions
 **********************************************************************/
/*
 * Create an eio_obj_t for handling a task's stdin traffic
 */
static eio_obj_t *
_create_task_in_eio(int fd, stepd_step_rec_t *step)
{
	struct task_write_info *t = xmalloc(sizeof(*t));
	eio_obj_t *eio = NULL;

	t->magic = TASK_IN_MAGIC;
	t->step = step;
	t->msg_queue = list_create(NULL); /* FIXME! Add destructor */
	t->msg = NULL;
	t->remaining = 0;

	eio = eio_obj_create(fd, &task_write_ops, (void *)t);

	return eio;
}

static bool
_task_writable(eio_obj_t *obj)
{
	struct task_write_info *t = (struct task_write_info *) obj->arg;

	debug5("Called _task_writable");

	if (obj->fd == -1) {
		debug5("  false, fd == -1");
		return false;
	}

	if (t->msg != NULL || list_count(t->msg_queue) > 0) {
		debug5("  true, list_count = %d", list_count(t->msg_queue));
		return true;
	}

	debug5("  false (list_count = %d)", list_count(t->msg_queue));
	return false;
}

static int
_task_write_error(eio_obj_t *obj, List objs)
{
	debug4("Called _task_write_error, closing fd %d", obj->fd);

	close(obj->fd);
	obj->fd = -1;

	return SLURM_SUCCESS;
}

static int
_task_write(eio_obj_t *obj, List objs)
{
	struct task_write_info *in = (struct task_write_info *) obj->arg;
	void *buf;
	int n;

	debug4("Entering _task_write");
	xassert(in->magic == TASK_IN_MAGIC);

	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (in->msg == NULL) {
		in->msg = list_dequeue(in->msg_queue);
		if (in->msg == NULL) {
			debug5("_task_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		if (in->msg->length == 0) { /* eof message */
			close(obj->fd);
			obj->fd = -1;
			_free_incoming_msg(in->msg, in->step);
			in->msg = NULL;
			return SLURM_SUCCESS;
		}
		in->remaining = in->msg->length;
	}

	/*
	 * Write message to pipe.
	 */
	buf = in->msg->data + (in->msg->length - in->remaining);
again:
	if ((n = write(obj->fd, buf, in->remaining)) < 0) {
		if (errno == EINTR)
			goto again;
		else if (errno == EAGAIN || errno == EWOULDBLOCK)
			return SLURM_SUCCESS;
		else {
			close(obj->fd);
			obj->fd = -1;
			_free_incoming_msg(in->msg, in->step);
			in->msg = NULL;
			return SLURM_ERROR;
		}
	}
	in->remaining -= n;
	if (in->remaining > 0)
		return SLURM_SUCCESS;

	_free_incoming_msg(in->msg, in->step);
	in->msg = NULL;

	return SLURM_SUCCESS;
}

/**********************************************************************
 * Task read functions
 **********************************************************************/
/*
 * Create an eio_obj_t for handling a task's stdout or stderr traffic
 */
static eio_obj_t *
_create_task_out_eio(int fd, uint16_t type,
		     stepd_step_rec_t *step, stepd_step_task_info_t *task)
{
	struct task_read_info *out = xmalloc(sizeof(*out));
	eio_obj_t *eio = NULL;

	out->magic = TASK_OUT_MAGIC;
	out->type = type;
	out->gtaskid = task->gtid;
	out->ltaskid = task->id;
	out->step = step;
	out->buf = cbuf_create(MAX_MSG_LEN, MAX_MSG_LEN*4);
	out->eof = false;
	out->eof_msg_sent = false;
	if (cbuf_opt_set(out->buf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) == -1)
		error("setting cbuf options");

	eio = eio_obj_create(fd, &task_read_ops, (void *)out);

	return eio;
}

static bool
_task_readable(eio_obj_t *obj)
{
	struct task_read_info *out = (struct task_read_info *)obj->arg;

	debug5("Called _task_readable, task %d, %s", out->gtaskid,
	       out->type == SLURM_IO_STDOUT ? "STDOUT" : "STDERR");

	if (out->eof_msg_sent) {
		debug5("  false, eof message sent");
		return false;
	}
	if (cbuf_free(out->buf) > 0) {
		debug5("  cbuf_free = %d", cbuf_free(out->buf));
		return true;
	}

	debug5("  false");
	return false;
}

/*
 * Read output (stdout or stderr) from a task into a cbuf.  The cbuf
 * allows whole lines to be packed into messages if line buffering
 * is requested.
 */
static int
_task_read(eio_obj_t *obj, List objs)
{
	struct task_read_info *out = (struct task_read_info *)obj->arg;
	int len;
	int rc = -1;

	xassert(out->magic == TASK_OUT_MAGIC);

	debug4("Entering _task_read for obj %zx", (size_t)obj);
	len = cbuf_free(out->buf);
	if (len > 0 && !out->eof) {
again:
		if ((rc = cbuf_write_from_fd(out->buf, obj->fd, len, NULL))
		    < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug5("_task_read returned EAGAIN");
				return SLURM_SUCCESS;
			}
			debug5("  error in _task_read: %m");
		}
		if (rc <= 0) {  /* got eof */
			debug5("  got eof on task");
			out->eof = true;
		}
	}

	debug5("************************ %d bytes read from task %s", rc,
	       out->type == SLURM_IO_STDOUT ? "STDOUT" : "STDERR");

	/*
	 * Put the message in client outgoing queues
	 */
	_route_msg_task_to_client(obj);

	/*
	 * Send the eof message
	 */
	if (cbuf_used(out->buf) == 0 && out->eof && !out->eof_msg_sent) {
		_send_eof_msg(out);
	}

	return SLURM_SUCCESS;
}

/**********************************************************************
 * Pseudo terminal functions
 **********************************************************************/
#ifdef HAVE_PTY_H
static void *_window_manager(void *arg)
{
	struct window_info *win_info = (struct window_info *) arg;
	pty_winsz_t winsz;
	ssize_t len;
	struct winsize ws;
	struct pollfd ufds;
	char buf[4];

	ufds.fd = win_info->pty_fd;
	ufds.events = POLLIN;

	while (1) {
		if (poll(&ufds, 1, -1) <= 0) {
			if (errno == EINTR)
				continue;
			error("poll(pty): %m");
			break;
		}
		if (!(ufds.revents & POLLIN)) {
			/* ((ufds.revents & POLLHUP) ||
			 *  (ufds.revents & POLLERR)) */
			break;
		}
		len = slurm_read_stream(win_info->pty_fd, buf, 4);
		if ((len == -1) && ((errno == EINTR) || (errno == EAGAIN)))
			continue;
		if (len < 4) {
			if (errno != SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT) {
				error("%s: read window size error: %m",
				      __func__);
			}
			return NULL;
		}
		memcpy(&winsz.cols, buf, 2);
		memcpy(&winsz.rows, buf+2, 2);
		ws.ws_col = ntohs(winsz.cols);
		ws.ws_row = ntohs(winsz.rows);
		debug("new pty size %u:%u", ws.ws_row, ws.ws_col);
		if (ioctl(win_info->task->to_stdin, TIOCSWINSZ, &ws))
			error("ioctl(TIOCSWINSZ): %s", strerror(errno));
		if (kill(win_info->task->pid, SIGWINCH)) {
			if (errno == ESRCH)
				break;
			error("kill(%d, SIGWINCH): %m",
				(int)win_info->task->pid);
		}
	}
	return NULL;
}

static void
_spawn_window_manager(stepd_step_task_info_t *task, stepd_step_rec_t *step)
{
	char *host, *port, *rows, *cols;
	int pty_fd;
	slurm_addr_t pty_addr;
	uint16_t port_u;
	struct window_info *win_info;

#if 0
	/* NOTE: SLURM_LAUNCH_NODE_IPADDR is not available at this point */
	if (!(ip_addr = getenvp(step->env, "SLURM_LAUNCH_NODE_IPADDR"))) {
		error("SLURM_LAUNCH_NODE_IPADDR env var not set");
		return;
	}
#endif
	if (!(host = getenvp(step->env, "SLURM_SRUN_COMM_HOST"))) {
		error("SLURM_SRUN_COMM_HOST env var not set");
		return;
	}
	if (!(port = getenvp(step->env, "SLURM_PTY_PORT"))) {
		error("SLURM_PTY_PORT env var not set");
		return;
	}
	if (!(cols = getenvp(step->env, "SLURM_PTY_WIN_COL")))
		error("SLURM_PTY_WIN_COL env var not set");
	if (!(rows = getenvp(step->env, "SLURM_PTY_WIN_ROW")))
		error("SLURM_PTY_WIN_ROW env var not set");

	if (rows && cols) {
		struct winsize ws;
		ws.ws_col = atoi(cols);
		ws.ws_row = atoi(rows);
		debug("init pty size %u:%u", ws.ws_row, ws.ws_col);
		if (ioctl(task->to_stdin, TIOCSWINSZ, &ws))
			error("ioctl(TIOCSWINSZ): %s", strerror(errno));
	}

	port_u = atoi(port);
	slurm_set_addr(&pty_addr, port_u, host);
	pty_fd = slurm_open_msg_conn(&pty_addr);
	if (pty_fd < 0) {
		error("slurm_open_msg_conn(pty_conn) %s,%u: %m",
			host, port_u);
		return;
	}

	win_info = xmalloc(sizeof(struct window_info));
	win_info->task   = task;
	win_info->step    = step;
	win_info->pty_fd = pty_fd;
	slurm_thread_create_detached(NULL, _window_manager, win_info);
}
#endif

/**********************************************************************
 * General fuctions
 **********************************************************************/

/*
 * This function sets the close-on-exec flag on all opened file descriptors.
 * io_dup_stdio will will remove the close-on-exec flags for just one task's
 * file descriptors.
 */
static int
_init_task_stdio_fds(stepd_step_task_info_t *task, stepd_step_rec_t *step)
{
	int file_flags = io_get_file_flags(step);

	/*
	 *  Initialize stdin
	 */
#ifdef HAVE_PTY_H
	if (step->flags & LAUNCH_PTY) {
		/* All of the stdin fails unless EVERY
		 * task gets an eio object for stdin.
		 * Its not clear why that is. */
		if (task->gtid == 0) {
			debug("  stdin uses a pty object");
#if HAVE_SETRESUID
			/*
			 *  openpty(3) calls grantpt(3), which sets
			 *   the owner of the pty device to the *real*
			 *   uid of the caller. Because of this, we must
			 *   change our uid temporarily to that of the
			 *   user (now the effective uid). We have to
			 *   use setresuid(2) so that we keep a saved uid
			 *   of root, and can regain previous permissions
			 *   after the call to openpty.
			 */
			if (setresuid(geteuid(), geteuid(), 0) < 0)
				error ("pre openpty: setresuid: %m");
#endif
			if (openpty(&task->to_stdin, &task->stdin_fd,
				    NULL, NULL, NULL) < 0) {
				error("stdin openpty: %m");
				return SLURM_ERROR;
			}
#if HAVE_SETRESUID
			if (setresuid(0, getuid(), 0) < 0)
				error ("post openpty: setresuid: %m");
#endif
			fd_set_close_on_exec(task->stdin_fd);
			fd_set_close_on_exec(task->to_stdin);
			fd_set_nonblocking(task->to_stdin);
			_spawn_window_manager(task, step);
			task->in = _create_task_in_eio(task->to_stdin, step);
			eio_new_initial_obj(step->eio, (void *)task->in);
		} else {
			xfree(task->ifname);
			task->ifname = xstrdup("/dev/null");
			task->stdin_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
			if (task->stdin_fd < 0) {
				error("Unable to open /dev/null: %m");
				return SLURM_ERROR;
			}
			task->to_stdin = dup(task->stdin_fd);
			fd_set_nonblocking(task->to_stdin);
			task->in = _create_task_in_eio(task->to_stdin, step);
			eio_new_initial_obj(step->eio, (void *)task->in);
		}
	} else if (task->ifname != NULL) {
#else
	if (task->ifname != NULL) {
#endif
		int count = 0;
		/* open file on task's stdin */
		debug5("  stdin file name = %s", task->ifname);
		do {
			task->stdin_fd = open(task->ifname, (O_RDONLY |
							     O_CLOEXEC));
			++count;
		} while (task->stdin_fd == -1 && errno == EINTR && count < 10);
		if (task->stdin_fd == -1) {
			error("Could not open stdin file %s: %m", task->ifname);
			return SLURM_ERROR;
		}
		task->to_stdin = -1;  /* not used */
	} else {
		/* create pipe and eio object */
		int pin[2];

		debug5("  stdin uses an eio object");
		if (pipe2(pin, O_CLOEXEC) < 0) {
			error("stdin pipe: %m");
			return SLURM_ERROR;
		}
		task->stdin_fd = pin[0];
		task->to_stdin = pin[1];
		fd_set_nonblocking(task->to_stdin);
		task->in = _create_task_in_eio(task->to_stdin, step);
		eio_new_initial_obj(step->eio, (void *)task->in);
	}

	/*
	 *  Initialize stdout
	 */
#ifdef HAVE_PTY_H
	if (step->flags & LAUNCH_PTY) {
		if (task->gtid == 0) {
			task->stdout_fd = dup(task->stdin_fd);
			fd_set_close_on_exec(task->stdout_fd);
			task->from_stdout = dup(task->to_stdin);
			fd_set_close_on_exec(task->from_stdout);
			fd_set_nonblocking(task->from_stdout);
			task->out = _create_task_out_eio(task->from_stdout,
						 SLURM_IO_STDOUT, step, task);
			list_append(step->stdout_eio_objs, (void *)task->out);
			eio_new_initial_obj(step->eio, (void *)task->out);
		} else {
			xfree(task->ofname);
			task->ofname = xstrdup("/dev/null");
			task->stdout_fd = open("/dev/null", O_RDWR, O_CLOEXEC);
			task->from_stdout = -1;  /* not used */
		}
	} else if ((task->ofname != NULL) &&
		   (((step->flags & LAUNCH_LABEL_IO) == 0) ||
		    (xstrcmp(task->ofname, "/dev/null") == 0))) {
#else
	if (task->ofname != NULL &&
	    (((step->flags & LAUNCH_LABEL_IO) == 0) ||
	     xstrcmp(task->ofname, "/dev/null") == 0)) {
#endif
		int count = 0;
		/* open file on task's stdout */
		debug5("  stdout file name = %s", task->ofname);
		do {
			task->stdout_fd = open(task->ofname,
					       file_flags | O_CLOEXEC, 0666);
			if (!count && (errno == ENOENT)) {
				mkdirpath(task->ofname, 0755, false);
				errno = EINTR;
			}
			++count;
		} while (task->stdout_fd == -1 && errno == EINTR && count < 10);
		if (task->stdout_fd == -1) {
			error("Could not open stdout file %s: %m",
			      task->ofname);
			return SLURM_ERROR;
		}
		task->from_stdout = -1; /* not used */
	} else {
		/* create pipe and eio object */
		int pout[2];
#if HAVE_PTY_H
		struct termios tio;
		if (!(step->flags & LAUNCH_BUFFERED_IO)) {
#if HAVE_SETRESUID
			if (setresuid(geteuid(), geteuid(), 0) < 0)
				error("%s: %u setresuid() %m",
				      __func__, geteuid());
#endif
			if (openpty(pout, pout + 1, NULL, NULL, NULL) < 0) {
				error("%s: stdout openpty: %m", __func__);
				return SLURM_ERROR;
			}
			memset(&tio, 0, sizeof(tio));
			if (tcgetattr(pout[1], &tio) == 0) {
				tio.c_oflag &= ~OPOST;
				if (tcsetattr(pout[1], 0, &tio) != 0)
					error("%s: tcsetattr: %m", __func__);
			}
#if HAVE_SETRESUID
			if (setresuid(0, getuid(), 0) < 0)
				error("%s 0 setresuid() %m", __func__);
#endif
		} else {
			debug5("  stdout uses an eio object");
			if (pipe(pout) < 0) {
				error("stdout pipe: %m");
				return SLURM_ERROR;
			}
		}
#else
		debug5("  stdout uses an eio object");
		if (pipe(pout) < 0) {
			error("stdout pipe: %m");
			return SLURM_ERROR;
		}
#endif
		task->stdout_fd = pout[1];
		fd_set_close_on_exec(task->stdout_fd);
		task->from_stdout = pout[0];
		fd_set_close_on_exec(task->from_stdout);
		fd_set_nonblocking(task->from_stdout);
		task->out = _create_task_out_eio(task->from_stdout,
						 SLURM_IO_STDOUT, step, task);
		list_append(step->stdout_eio_objs, (void *)task->out);
		eio_new_initial_obj(step->eio, (void *)task->out);
	}

	/*
	 *  Initialize stderr
	 */
#ifdef HAVE_PTY_H
	if (step->flags & LAUNCH_PTY) {
		if (task->gtid == 0) {
			/* Make a file descriptor for the task to write to, but
			   don't make a separate one read from, because in pty
			   mode we can't distinguish between stdout and stderr
			   coming from the remote shell.  Both streams from the
			   shell will go to task->stdout_fd, which is okay in
			   pty mode because any output routed through the stepd
			   will be displayed. */
			task->stderr_fd = dup(task->stdin_fd);
			fd_set_close_on_exec(task->stderr_fd);
			task->from_stderr = -1;
		} else {
			xfree(task->efname);
			task->efname = xstrdup("/dev/null");
			task->stderr_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
			task->from_stderr = -1;  /* not used */
		}

	} else if ((task->efname != NULL) &&
		   (((step->flags & LAUNCH_LABEL_IO) == 0) ||
		    (xstrcmp(task->efname, "/dev/null") == 0))) {
#else
	if ((task->efname != NULL) &&
	    (((step->flags & LAUNCH_LABEL_IO) == 0) ||
	     (xstrcmp(task->efname, "/dev/null") == 0))) {
#endif
		int count = 0;
		/* open file on task's stdout */
		debug5("  stderr file name = %s", task->efname);
		do {
			task->stderr_fd = open(task->efname,
					       file_flags | O_CLOEXEC, 0666);
			if (!count && (errno == ENOENT)) {
				mkdirpath(task->efname, 0755, false);
				errno = EINTR;
			}
			++count;
		} while (task->stderr_fd == -1 && errno == EINTR && count < 10);
		if (task->stderr_fd == -1) {
			error("Could not open stderr file %s: %m",
			      task->efname);
			return SLURM_ERROR;
		}
		task->from_stderr = -1; /* not used */
	} else {
		/* create pipe and eio object */
		int perr[2];
		debug5("  stderr uses an eio object");
		if (pipe(perr) < 0) {
			error("stderr pipe: %m");
			return SLURM_ERROR;
		}
		task->stderr_fd = perr[1];
		fd_set_close_on_exec(task->stderr_fd);
		task->from_stderr = perr[0];
		fd_set_close_on_exec(task->from_stderr);
		fd_set_nonblocking(task->from_stderr);
		task->err = _create_task_out_eio(task->from_stderr,
						 SLURM_IO_STDERR, step, task);
		list_append(step->stderr_eio_objs, (void *)task->err);
		eio_new_initial_obj(step->eio, (void *)task->err);
	}

	return SLURM_SUCCESS;
}

int
io_init_tasks_stdio(stepd_step_rec_t *step)
{
	int i, rc = SLURM_SUCCESS, tmprc;

	for (i = 0; i < step->node_tasks; i++) {
		tmprc = _init_task_stdio_fds(step->task[i], step);
		if (tmprc != SLURM_SUCCESS)
			rc = tmprc;
	}

	return rc;
}

extern void io_thread_start(stepd_step_rec_t *step)
{
	slurm_mutex_lock(&step->io_mutex);
	slurm_thread_create_detached(NULL, _io_thr, step);
	step->io_running = true;
	slurm_mutex_unlock(&step->io_mutex);
}

void
_shrink_msg_cache(List cache, stepd_step_rec_t *step)
{
	struct io_buf *msg;
	int over = 0;
	int count;
	int i;

	count = list_count(cache);
	if (count > STDIO_MAX_MSG_CACHE)
		over = count - STDIO_MAX_MSG_CACHE;

	for (i = 0; i < over; i++) {
		msg = list_dequeue(cache);
		/* FIXME - following call MIGHT lead to too much recursion */
		_free_outgoing_msg(msg, step);
	}
}



static int
_send_connection_okay_response(stepd_step_rec_t *step)
{
	eio_obj_t *eio;
	ListIterator clients;
	struct io_buf *msg;
	struct client_io_info *client;

	msg = _build_connection_okay_message(step);
	if (!msg) {
		error(  "Could not send connection okay message because of "
			"lack of buffer space.");
		return SLURM_ERROR;
	}

	clients = list_iterator_create(step->clients);
	while ((eio = list_next(clients))) {
		client = (struct client_io_info *)eio->arg;
		if (client->out_eof || client->is_local_file)
			continue;

		debug5("Sent connection okay message");
		xassert(client->magic == CLIENT_IO_MAGIC);
		list_enqueue(client->msg_queue, msg);
		msg->ref_count++;
	}
	list_iterator_destroy(clients);

	return SLURM_SUCCESS;
}



static struct io_buf *
_build_connection_okay_message(stepd_step_rec_t *step)
{
	struct io_buf *msg;
	buf_t *packbuf;
	struct slurm_io_header header;

	if (_outgoing_buf_free(step)) {
		msg = list_dequeue(step->free_outgoing);
	} else {
		return NULL;
	}

	header.type = SLURM_IO_CONNECTION_TEST;
	header.ltaskid = 0;  /* Unused */
	header.gtaskid = 0;  /* Unused */
	header.length = 0;

	packbuf = create_buf(msg->data, io_hdr_packed_size());
	if (!packbuf) {
		fatal("Failure to allocate memory for a message header");
		return msg;	/* Fix for CLANG false positive error */
	}
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size();
	msg->ref_count = 0; /* make certain it is initialized */

	/* free packbuf, but not the memory to which it points */
	packbuf->head = NULL;	/* CLANG false positive bug here */
	FREE_NULL_BUFFER(packbuf);

	return msg;
}



static void
_route_msg_task_to_client(eio_obj_t *obj)
{
	struct task_read_info *out = (struct task_read_info *)obj->arg;
	struct client_io_info *client;
	struct io_buf *msg = NULL;
	eio_obj_t *eio;
	ListIterator clients;

	/* Pack task output into messages for transfer to a client */
	while (cbuf_used(out->buf) > 0
	       && _outgoing_buf_free(out->step)) {
		debug5("cbuf_used = %d", cbuf_used(out->buf));
		msg = _task_build_message(out, out->step, out->buf);
		if (msg == NULL)
			return;

		/* Add message to the msg_queue of all clients */
		clients = list_iterator_create(out->step->clients);
		while ((eio = list_next(clients))) {
			client = (struct client_io_info *)eio->arg;
			if (client->out_eof == true)
				continue;

			/* Some clients only take certain I/O streams */
			if (out->type==SLURM_IO_STDOUT) {
				if (client->ltaskid_stdout != -1 &&
				    client->ltaskid_stdout != out->ltaskid)
					continue;
			}
			if (out->type==SLURM_IO_STDERR) {
				if (client->ltaskid_stderr != -1 &&
				    client->ltaskid_stderr != out->ltaskid)
					continue;
			}

			debug5("======================== Enqueued message");
			xassert(client->magic == CLIENT_IO_MAGIC);
			list_enqueue(client->msg_queue, msg);
			msg->ref_count++;
		}
		list_iterator_destroy(clients);

		/* Update the outgoing message cache */
		list_enqueue(out->step->outgoing_cache, msg);
		msg->ref_count++;
		_shrink_msg_cache(out->step->outgoing_cache, out->step);
	}
}

static void
_free_incoming_msg(struct io_buf *msg, stepd_step_rec_t *step)
{
	msg->ref_count--;
	if (msg->ref_count == 0) {
		/* Put the message back on the free List */
		list_enqueue(step->free_incoming, msg);

		/* Kick the event IO engine */
		eio_signal_wakeup(step->eio);
	}
}

static void
_free_outgoing_msg(struct io_buf *msg, stepd_step_rec_t *step)
{
	int i;

	msg->ref_count--;
	if (msg->ref_count == 0) {
		/* Put the message back on the free List */
		list_enqueue(step->free_outgoing, msg);

		/* Try packing messages from tasks' output cbufs */
		if (step->task == NULL)
			return;
		for (i = 0; i < step->node_tasks; i++) {
			if (step->task[i]->err != NULL) {
				_route_msg_task_to_client(step->task[i]->err);
				if (!_outgoing_buf_free(step))
					break;
			}
			if (step->task[i]->out != NULL) {
				_route_msg_task_to_client(step->task[i]->out);
				if (!_outgoing_buf_free(step))
					break;
			}
		}
		/* Kick the event IO engine */
		eio_signal_wakeup(step->eio);
	}
}

static void
_free_all_outgoing_msgs(List msg_queue, stepd_step_rec_t *step)
{
	ListIterator msgs;
	struct io_buf *msg;

	msgs = list_iterator_create(msg_queue);
	while((msg = list_next(msgs))) {
		_free_outgoing_msg(msg, step);
	}
	list_iterator_destroy(msgs);
}

/* Close I/O file descriptors created by slurmstepd. The connections have
 * all been moved to the spawned tasks stdin/out/err file descriptors. */
extern void
io_close_task_fds(stepd_step_rec_t *step)
{
	int i;

	for (i = 0; i < step->node_tasks; i++) {
		close(step->task[i]->stdin_fd);
		close(step->task[i]->stdout_fd);
		close(step->task[i]->stderr_fd);
	}
}

void
io_close_all(stepd_step_rec_t *step)
{
	int devnull;
#if 0
	int i;
	for (i = 0; i < step->node_tasks; i++)
		_io_finalize(step->task[i]);
#endif

	/* No more debug info will be received by client after this point
	 */
	debug("Closing debug channel");

	/*
	 * Send stderr to /dev/null since debug channel is closing
	 *  and log facility may still try to write to stderr.
	 */
	if ((devnull = open("/dev/null", O_RDWR)) < 0) {
		error("Could not open /dev/null: %m");
	} else {
		if (dup2(devnull, STDERR_FILENO) < 0)
			error("Unable to dup /dev/null onto stderr");
		(void) close(devnull);
	}

	/* Signal IO thread to close appropriate
	 * client connections
	 */
	eio_signal_shutdown(step->eio);
}

void
io_close_local_fds(stepd_step_rec_t *step)
{
	ListIterator clients;
	eio_obj_t *eio;
	int rc;
	struct client_io_info *client;

	if (step == NULL || step->clients == NULL)
		return;

	clients = list_iterator_create(step->clients);
	while((eio = list_next(clients))) {
		client = (struct client_io_info *)eio->arg;
		if (client->is_local_file) {
			if (eio->fd >= 0) {
				do {
					rc = close(eio->fd);
				} while (rc == -1 && errno == EINTR);
				eio->fd = -1;
			}
		}
	}
	list_iterator_destroy(clients);
}



static void *
_io_thr(void *arg)
{
	stepd_step_rec_t *step = (stepd_step_rec_t *) arg;
	sigset_t set;
	int rc;

	/* A SIGHUP signal signals a reattach to the mgr thread.  We need
	 * to block SIGHUP from being delivered to this thread so the mgr
	 * thread will see the signal.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	sigaddset(&set, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	debug("IO handler started pid=%lu", (unsigned long) getpid());
	rc = eio_handle_mainloop(step->eio);
	debug("IO handler exited, rc=%d", rc);
	slurm_mutex_lock(&step->io_mutex);
	step->io_running = false;
	slurm_cond_broadcast(&step->io_cond);
	slurm_mutex_unlock(&step->io_mutex);
	return (void *)1;
}

/*
 *  Add a client to the step's client list that will write stdout and/or
 *  stderr from the slurmstepd.  The slurmstepd handles the write when
 *  a file is created per node or per task, and the output needs to be
 *  modified in some way, like labelling lines with the task number.
 */
int
io_create_local_client(const char *filename, int file_flags,
		       stepd_step_rec_t *step, bool labelio,
		       int stdout_tasks, int stderr_tasks)
{
	int fd = -1;
	struct client_io_info *client;
	eio_obj_t *obj;
	int tmp;

	fd = open(filename, file_flags | O_CLOEXEC, 0666);
	if (fd == -1) {
		return ESLURMD_IO_ERROR;
	}

	/* Now set up the eio object */
	client = xmalloc(sizeof(*client));
	client->magic = CLIENT_IO_MAGIC;
	client->step = step;
	client->msg_queue = list_create(NULL); /* FIXME - destructor */

	client->ltaskid_stdout = stdout_tasks;
	client->ltaskid_stderr = stderr_tasks;
	client->labelio = labelio;
	client->is_local_file = true;

	client->taskid_width = 1;
	tmp = step->node_tasks - 1;
	while ((tmp /= 10) > 0)
		client->taskid_width++;


	obj = eio_obj_create(fd, &local_file_ops, (void *)client);
	list_append(step->clients, (void *)obj);
	eio_new_initial_obj(step->eio, (void *)obj);
	debug5("Now handling %d IO Client object(s)", list_count(step->clients));

	return SLURM_SUCCESS;
}

/*
 * Create the initial TCP connection back to a waiting client (e.g. srun).
 *
 * Since this is the first client connection and the IO engine has not
 * yet started, we initialize the msg_queue as an empty list and
 * directly add the eio_obj_t to the eio handle with eio_new_initial_obj.
 *
 * We assume that if the port is zero the client does not wish us to connect
 * an IO stream.
 */
int
io_initial_client_connect(srun_info_t *srun, stepd_step_rec_t *step,
			  int stdout_tasks, int stderr_tasks)
{
	int sock = -1;
	struct client_io_info *client;
	eio_obj_t *obj;

	debug4 ("adding IO connection (logical node rank %d)", step->nodeid);

	if (!slurm_addr_is_unspec(&srun->ioaddr)) {
		if (slurm_get_port(&srun->ioaddr) == 0) {
			debug3("No IO connection requested");
			return SLURM_SUCCESS;
		}
		debug4("connecting IO back to %pA", &srun->ioaddr);
	}

	if ((sock = (int) slurm_open_stream(&srun->ioaddr, true)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail?
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}

	fd_set_blocking(sock);  /* just in case... */
	_send_io_init_msg(sock, srun, step, true);

	debug5("  back from _send_io_init_msg");
	fd_set_nonblocking(sock);

	/* Now set up the eio object */
	client = xmalloc(sizeof(*client));
	client->magic = CLIENT_IO_MAGIC;
	client->step = step;
	client->msg_queue = list_create(NULL); /* FIXME - destructor */

	client->ltaskid_stdout = stdout_tasks;
	client->ltaskid_stderr = stderr_tasks;
	client->labelio = false;
	client->taskid_width = 0;
	client->is_local_file = false;

	obj = eio_obj_create(sock, &client_ops, (void *)client);
	list_append(step->clients, (void *)obj);
	eio_new_initial_obj(step->eio, (void *)obj);
	debug5("Now handling %d IO Client object(s)",
	       list_count(step->clients));

	return SLURM_SUCCESS;
}

/*
 * Initiate a TCP connection back to a waiting client (e.g. srun).
 *
 * Create a new eio client object and wake up the eio engine so that
 * it can see the new object.
 */
int
io_client_connect(srun_info_t *srun, stepd_step_rec_t *step)
{
	int sock = -1;
	struct client_io_info *client;
	eio_obj_t *obj;

	debug4 ("adding IO connection (logical node rank %d)", step->nodeid);

	if (!slurm_addr_is_unspec(&srun->ioaddr)) {
		debug4("connecting IO back to %pA", &srun->ioaddr);
	}

	if ((sock = (int) slurm_open_stream(&srun->ioaddr, true)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail?
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}

	fd_set_blocking(sock);  /* just in case... */
	_send_io_init_msg(sock, srun, step, false);

	debug5("  back from _send_io_init_msg");
	fd_set_nonblocking(sock);

	/* Now set up the eio object */
	client = xmalloc(sizeof(*client));
	client->magic = CLIENT_IO_MAGIC;
	client->step = step;
	client->msg_queue = NULL; /* initialized in _client_writable */

	client->ltaskid_stdout = -1;     /* accept from all tasks */
	client->ltaskid_stderr = -1;     /* accept from all tasks */
	client->labelio = false;
	client->taskid_width = 0;
	client->is_local_file = false;

	/* client object adds itself to step->clients in _client_writable */

	obj = eio_obj_create(sock, &client_ops, (void *)client);
	eio_new_obj(step->eio, (void *)obj);

	debug5("New IO Client object added");

	return SLURM_SUCCESS;
}

static int
_send_io_init_msg(int sock, srun_info_t *srun, stepd_step_rec_t *step, bool init)
{
	io_init_msg_t msg;

	msg.io_key = xmalloc(srun->key->len);
	msg.io_key_len = srun->key->len;
	memcpy(msg.io_key, srun->key->data, srun->key->len);
	msg.nodeid = step->nodeid;
	msg.version = srun->protocol_version;

	/*
	 * The initial message does not need the node_offset it is needed for
	 * sattach
	 */
	if (!init && (step->step_id.step_het_comp != NO_VAL))
		msg.nodeid += step->het_job_node_offset;

	if (step->stdout_eio_objs == NULL)
		msg.stdout_objs = 0;
	else
		msg.stdout_objs = list_count(step->stdout_eio_objs);
	if (step->stderr_eio_objs == NULL)
		msg.stderr_objs = 0;
	else
		msg.stderr_objs = list_count(step->stderr_eio_objs);

	if (io_init_msg_write_to_fd(sock, &msg) != SLURM_SUCCESS) {
		error("Couldn't sent slurm_io_init_msg");
		xfree(msg.io_key);
		return SLURM_ERROR;
	}

	xfree(msg.io_key);

	return SLURM_SUCCESS;
}

/*
 * dup the appropriate file descriptors onto the task's
 * stdin, stdout, and stderr.
 *
 * Close the server's end of the stdio pipes.
 */
int
io_dup_stdio(stepd_step_task_info_t *t)
{
	if (dup2(t->stdin_fd, STDIN_FILENO  ) < 0) {
		error("dup2(stdin): %m");
		return SLURM_ERROR;
	}
	fd_set_noclose_on_exec(STDIN_FILENO);

	if (dup2(t->stdout_fd, STDOUT_FILENO) < 0) {
		error("dup2(stdout): %m");
		return SLURM_ERROR;
	}
	fd_set_noclose_on_exec(STDOUT_FILENO);

	if (dup2(t->stderr_fd, STDERR_FILENO) < 0) {
		error("dup2(stderr): %m");
		return SLURM_ERROR;
	}
	fd_set_noclose_on_exec(STDERR_FILENO);

	return SLURM_SUCCESS;
}

static void
_send_eof_msg(struct task_read_info *out)
{
	struct client_io_info *client;
	struct io_buf *msg = NULL;
	eio_obj_t *eio;
	ListIterator clients;
	struct slurm_io_header header;
	buf_t *packbuf;

	debug4("Entering _send_eof_msg");
	out->eof_msg_sent = true;

	if (_outgoing_buf_free(out->step)) {
		msg = list_dequeue(out->step->free_outgoing);
	} else {
		/* eof message must be allowed to allocate new memory
		   because _task_readable() will return "true" until
		   the eof message is enqueued.  For instance, if
		   a poll returns POLLHUP on the incoming task pipe,
		   put there are no outgoing message buffers available,
		   the slurmstepd will start spinning. */
		msg = alloc_io_buf();
	}

	header.type = out->type;
	header.ltaskid = out->ltaskid;
	header.gtaskid = out->gtaskid;
	header.length = 0; /* eof */

	packbuf = create_buf(msg->data, io_hdr_packed_size());
	if (!packbuf) {
		fatal("Failure to allocate memory for a message header");
		return;	/* Fix for CLANG false positive error */
	}

	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* free packbuf, but not the memory to which it points */
	packbuf->head = NULL;	/* CLANG false positive bug here */
	FREE_NULL_BUFFER(packbuf);

	/* Add eof message to the msg_queue of all clients */
	clients = list_iterator_create(out->step->clients);
	while ((eio = list_next(clients))) {
		client = (struct client_io_info *)eio->arg;
		debug5("======================== Enqueued eof message");
		xassert(client->magic == CLIENT_IO_MAGIC);

		/* Send eof message to all clients */
		list_enqueue(client->msg_queue, msg);
		msg->ref_count++;
	}
	list_iterator_destroy(clients);
	if (msg->ref_count == 0)
		free_io_buf(msg);

	debug4("Leaving  _send_eof_msg");
}



static struct io_buf *_task_build_message(struct task_read_info *out,
					  stepd_step_rec_t *step, cbuf_t *cbuf)
{
	struct io_buf *msg;
	char *ptr;
	buf_t *packbuf;
	bool must_truncate = false;
	int avail;
	struct slurm_io_header header;
	int n;
	bool buffered_stdio = step->flags & LAUNCH_BUFFERED_IO;

	debug4("%s: Entering...", __func__);

	if (_outgoing_buf_free(step)) {
		msg = list_dequeue(step->free_outgoing);
	} else {
		return NULL;
	}

	ptr = msg->data + io_hdr_packed_size();

	if (buffered_stdio) {
		avail = cbuf_peek_line(cbuf, ptr, MAX_MSG_LEN, 1);
		if (avail >= MAX_MSG_LEN)
			must_truncate = true;
		else if (avail == 0 && cbuf_used(cbuf) >= MAX_MSG_LEN)
			must_truncate = true;
	}

	debug5("%s: buffered_stdio is %s", __func__,
	       buffered_stdio ? "true" : "false");
	debug5("%s: must_truncate  is %s", __func__,
	       must_truncate ? "true" : "false");

	/*
	 * If eof has been read from a tasks stdout or stderr, we need to
	 * ignore normal line buffering and send the buffer immediately.
	 * Hence the "|| out->eof".
	 */
	if (must_truncate || !buffered_stdio || out->eof) {
		n = cbuf_read(cbuf, ptr, MAX_MSG_LEN);
	} else {
		n = cbuf_read_line(cbuf, ptr, MAX_MSG_LEN, -1);
		if (n == 0) {
			debug5("  partial line in buffer, ignoring");
			debug4("Leaving  _task_build_message");
			list_enqueue(step->free_outgoing, msg);
			return NULL;
		}
	}

	header.type = out->type;
	header.ltaskid = out->ltaskid;
	header.gtaskid = out->gtaskid;
	header.length = n;

	debug4("%s: header.length = %d", __func__, n);
	packbuf = create_buf(msg->data, io_hdr_packed_size());
	if (!packbuf) {
		fatal("Failure to allocate memory for a message header");
		return msg;	/* Fix for CLANG false positive error */
	}
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* free packbuf, but not the memory to which it points */
	packbuf->head = NULL;	/* CLANG false positive bug here */
	FREE_NULL_BUFFER(packbuf);

	debug4("%s: Leaving", __func__);
	return msg;
}

struct io_buf *
alloc_io_buf(void)
{
	struct io_buf *buf = xmalloc(sizeof(*buf));

	buf->ref_count = 0;
	buf->length = 0;
	/* The following "+ 1" is just temporary so I can stick a \0 at
	   the end and do a printf of the data pointer */
	buf->data = xmalloc(MAX_MSG_LEN + io_hdr_packed_size() + 1);

	return buf;
}

void
free_io_buf(struct io_buf *buf)
{
	if (buf) {
		if (buf->data)
			xfree(buf->data);
		xfree(buf);
	}
}

/* This just determines if there's space to hold more of the stdin stream */
static bool
_incoming_buf_free(stepd_step_rec_t *step)
{
	struct io_buf *buf;

	if (list_count(step->free_incoming) > 0) {
		return true;
	} else if (step->incoming_count < STDIO_MAX_FREE_BUF) {
		buf = alloc_io_buf();
		list_enqueue(step->free_incoming, buf);
		step->incoming_count++;
		return true;
	}

	return false;
}

static bool
_outgoing_buf_free(stepd_step_rec_t *step)
{
	struct io_buf *buf;

	if (list_count(step->free_outgoing) > 0) {
		return true;
	} else if (step->outgoing_count < STDIO_MAX_FREE_BUF) {
		buf = alloc_io_buf();
		list_enqueue(step->free_outgoing, buf);
		step->outgoing_count++;
		return true;
	}

	return false;
}

void
io_find_filename_pattern( stepd_step_rec_t *step,
			  slurmd_filename_pattern_t *outpattern,
			  slurmd_filename_pattern_t *errpattern,
			  bool *same_out_err_files )
{
	int ii, jj;
	int of_num_null = 0, ef_num_null = 0;
	int of_num_devnull = 0, ef_num_devnull = 0;
	int of_lastnull = -1, ef_lastnull = -1;
	bool of_all_same = true, ef_all_same = true;
	bool of_all_unique = true, ef_all_unique = true;

	*outpattern = SLURMD_UNKNOWN;
	*errpattern = SLURMD_UNKNOWN;
	*same_out_err_files = false;

	for (ii = 0; ii < step->node_tasks; ii++) {
		if (step->task[ii]->ofname == NULL) {
			of_num_null++;
			of_lastnull = ii;
		} else if (xstrcmp(step->task[ii]->ofname, "/dev/null")==0) {
			of_num_devnull++;
		}

		if (step->task[ii]->efname == NULL) {
			ef_num_null++;
			ef_lastnull = ii;
		} else if (xstrcmp(step->task[ii]->efname, "/dev/null")==0) {
			ef_num_devnull++;
		}
	}
	if (of_num_null == step->node_tasks)
		*outpattern = SLURMD_ALL_NULL;

	if (ef_num_null == step->node_tasks)
		*errpattern = SLURMD_ALL_NULL;

	if (of_num_null == 1 && of_num_devnull == step->node_tasks-1)
		*outpattern = SLURMD_ONE_NULL;

	if (ef_num_null == 1 && ef_num_devnull == step->node_tasks-1)
		*errpattern = SLURMD_ONE_NULL;

	if (*outpattern == SLURMD_ALL_NULL && *errpattern == SLURMD_ALL_NULL)
		*same_out_err_files = true;

	if (*outpattern == SLURMD_ONE_NULL && *errpattern == SLURMD_ONE_NULL &&
	    of_lastnull == ef_lastnull)
		*same_out_err_files = true;

	if (*outpattern != SLURMD_UNKNOWN && *errpattern != SLURMD_UNKNOWN)
		return;

	for (ii = 1; ii < step->node_tasks; ii++) {
		if (!step->task[ii]->ofname || !step->task[0]->ofname ||
		    xstrcmp(step->task[ii]->ofname, step->task[0]->ofname) != 0)
			of_all_same = false;

		if (!step->task[ii]->efname || !step->task[0]->efname ||
		    xstrcmp(step->task[ii]->efname, step->task[0]->efname) != 0)
			ef_all_same = false;
	}

	if (of_all_same && *outpattern == SLURMD_UNKNOWN)
		*outpattern = SLURMD_ALL_SAME;

	if (ef_all_same && *errpattern == SLURMD_UNKNOWN)
		*errpattern = SLURMD_ALL_SAME;

	if (step->task[0]->ofname && step->task[0]->efname &&
	    xstrcmp(step->task[0]->ofname, step->task[0]->efname)==0)
		*same_out_err_files = true;

	if (*outpattern != SLURMD_UNKNOWN && *errpattern != SLURMD_UNKNOWN)
		return;

	for (ii = 0; ii < step->node_tasks-1; ii++) {
		for (jj = ii+1; jj < step->node_tasks; jj++) {

			if (!step->task[ii]->ofname ||
			    !step->task[jj]->ofname ||
			    xstrcmp(step->task[ii]->ofname,
				    step->task[jj]->ofname) == 0)
				of_all_unique = false;

			if (!step->task[ii]->efname ||
			    !step->task[jj]->efname ||
			    xstrcmp(step->task[ii]->efname,
				    step->task[jj]->efname) == 0)
				ef_all_unique = false;
		}
	}

	if (of_all_unique)
		*outpattern = SLURMD_ALL_UNIQUE;

	if (ef_all_unique)
		*errpattern = SLURMD_ALL_UNIQUE;

	if (of_all_unique && ef_all_unique) {
		*same_out_err_files = true;
		for (ii = 0; ii < step->node_tasks; ii++) {
			if (step->task[ii]->ofname &&
			    step->task[ii]->efname &&
			    xstrcmp(step->task[ii]->ofname,
				    step->task[ii]->efname) != 0) {
				*same_out_err_files = false;
				break;
			}
		}
	}
}


int
io_get_file_flags(stepd_step_rec_t *step)
{
	int file_flags;

	/* set files for opening stdout/err */
	if (step->open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (step->open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_conf_t *conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}
	return file_flags;
}
