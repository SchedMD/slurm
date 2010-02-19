/*****************************************************************************\
 * src/slurmd/slurmstepd/io.c - Standard I/O handling routines for slurmstepd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#ifdef HAVE_PTY_H
#  include <pty.h>
#endif

#ifdef HAVE_UTMP_H
#  include <utmp.h>
#endif

#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/eio.h"
#include "src/common/io_hdr.h"
#include "src/common/cbuf.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/write_labelled_message.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/fname.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"

/**********************************************************************
 * IO client socket declarations
 **********************************************************************/
static bool _client_readable(eio_obj_t *);
static bool _client_writable(eio_obj_t *);
static int  _client_read(eio_obj_t *, List);
static int  _client_write(eio_obj_t *, List);

struct io_operations client_ops = {
	readable:	&_client_readable,
	writable:	&_client_writable,
	handle_read:	&_client_read,
	handle_write:	&_client_write,
};

struct client_io_info {
#ifndef NDEBUG
#define CLIENT_IO_MAGIC  0x10102
	int                   magic;
#endif
	slurmd_job_t    *job;		 /* pointer back to job data   */

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
	int  label_width;

	/* true if writing to a file, false if writing to a socket */
	bool is_local_file;
};


static bool _local_file_writable(eio_obj_t *);
static int  _local_file_write(eio_obj_t *, List);

struct io_operations local_file_ops = {
	writable:	&_local_file_writable,
	handle_write:	&_local_file_write,
};


/**********************************************************************
 * Task write declarations
 **********************************************************************/
static bool _task_writable(eio_obj_t *);
static int  _task_write(eio_obj_t *, List);
static int _task_write_error(eio_obj_t *obj, List objs);

struct io_operations task_write_ops = {
	writable:	&_task_writable,
	handle_write:	&_task_write,
	handle_error:   &_task_write_error,
};

struct task_write_info {
#ifndef NDEBUG
#define TASK_IN_MAGIC  0x10103
	int              magic;
#endif
	slurmd_job_t    *job;		 /* pointer back to job data   */

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
	readable:	&_task_readable,
	handle_read:	&_task_read,
};

struct task_read_info {
#ifndef NDEBUG
#define TASK_OUT_MAGIC  0x10103
	int              magic;
#endif
	uint16_t         type;           /* type of IO object          */
	uint16_t         gtaskid;
	uint16_t         ltaskid;
	slurmd_job_t    *job;		 /* pointer back to job data   */
	cbuf_t           buf;
	bool		 eof;
	bool		 eof_msg_sent;
};

/**********************************************************************
 * Pseudo terminal declarations
 **********************************************************************/
struct window_info {
	slurmd_task_info_t *task;
	slurmd_job_t *job;
	slurm_fd pty_fd;
};
#ifdef HAVE_PTY_H
static void  _spawn_window_manager(slurmd_task_info_t *task, slurmd_job_t *job);
static void *_window_manager(void *arg);
#endif

/**********************************************************************
 * General declarations
 **********************************************************************/
static void *_io_thr(void *);
static int _send_io_init_msg(int sock, srun_key_t *key, slurmd_job_t *job);
static void _send_eof_msg(struct task_read_info *out);
static struct io_buf *_task_build_message(struct task_read_info *out,
					  slurmd_job_t *job, cbuf_t cbuf);
static void *_io_thr(void *arg);
static void _route_msg_task_to_client(eio_obj_t *obj);
static void _free_outgoing_msg(struct io_buf *msg, slurmd_job_t *job);
static void _free_incoming_msg(struct io_buf *msg, slurmd_job_t *job);
static void _free_all_outgoing_msgs(List msg_queue, slurmd_job_t *job);
static bool _incoming_buf_free(slurmd_job_t *job);
static bool _outgoing_buf_free(slurmd_job_t *job);
static int  _send_connection_okay_response(slurmd_job_t *job);
static struct io_buf *_build_connection_okay_message(slurmd_job_t *job);

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
		debug5("  false");
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
	    || _incoming_buf_free(client->job))
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
	 * to be intialized from the outgoing_cache, and then "obj" needs
	 * to be added to the List of clients.
	 */
	if (client->msg_queue == NULL) {
		ListIterator msgs;
		struct io_buf *msg;
		client->msg_queue = list_create(NULL); /* need destructor */
		msgs = list_iterator_create(client->job->outgoing_cache);
		if (!msgs)
			fatal("Could not allocate iterator");

		while ((msg = list_next(msgs))) {
			msg->ref_count++;
			list_enqueue(client->msg_queue, msg);
		}
		list_iterator_destroy(msgs);
		/* and now make this object visible to tasks */
		list_append(client->job->clients, (void *)obj);
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
		if (_incoming_buf_free(client->job)) {
			client->in_msg =
				list_dequeue(client->job->free_incoming);
		} else {
			debug5("  _client_read free_incoming is empty");
			return SLURM_SUCCESS;
		}
		n = io_hdr_read_fd(obj->fd, &client->header);
		if (n <= 0) { /* got eof or fatal error */
			debug5("  got eof or error _client_read header, n=%d", n);
			client->in_eof = true;
			list_enqueue(client->job->free_incoming, client->in_msg);
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
			list_enqueue(client->job->free_incoming, client->in_msg);
			client->in_msg = NULL;
			return SLURM_ERROR;
		}
		if (_send_connection_okay_response(client->job)) {
			/*
			 * If we get here because of a failed
			 * _send_connection_okay_response, it's because of a
			 * lack of buffer space in the output queue.  Just
			 * keep the current input message client->in_msg in
			 * place, and resend on the next call.
			 */
			return SLURM_SUCCESS;
		}
		list_enqueue(client->job->free_incoming, client->in_msg);
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
			list_enqueue(client->job->free_incoming, client->in_msg);
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
		slurmd_task_info_t *task;
		struct task_write_info *io;

		client->in_msg->ref_count = 0;
		if (client->header.type == SLURM_IO_ALLSTDIN) {
			for (i = 0; i < client->job->ntasks; i++) {
				task = client->job->task[i];
				io = (struct task_write_info *)task->in->arg;
				client->in_msg->ref_count++;
				list_enqueue(io->msg_queue, client->in_msg);
			}
			debug5("  message ref_count = %d", client->in_msg->ref_count);
		} else {
			for (i = 0; i < client->job->ntasks; i++) {
				task = client->job->task[i];
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
			_free_all_outgoing_msgs(client->msg_queue, client->job);
			return SLURM_SUCCESS;
		}
	}
	debug5("Wrote %d bytes to socket", n);
	client->out_remaining -= n;
	if (client->out_remaining > 0)
		return SLURM_SUCCESS;

	_free_outgoing_msg(client->out_msg, client->job);
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
	Buf header_tmp_buf;

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

	/* This code to make a buffer, fill it, unpack its contents, and free
	   it is just used to read the header to get the global task id. */
	header_tmp_buf = create_buf(client->out_msg->data,
				    client->out_msg->length);
	if (!header_tmp_buf)
		fatal("Failure to allocate memory for a message header");
	io_hdr_unpack(&header, header_tmp_buf);
	header_tmp_buf->head = NULL;
	free_buf(header_tmp_buf);

	/* A zero-length message indicates the end of a stream from one
	   of the tasks.  Just free the message and return. */
	if (header.length == 0) {
		_free_outgoing_msg(client->out_msg, client->job);
		client->out_msg = NULL;
		return SLURM_SUCCESS;
	}

	/* Write the message to the file. */
	buf = client->out_msg->data +
		(client->out_msg->length - client->out_remaining);

	n = write_labelled_message(obj->fd, buf, client->out_remaining,
				   header.gtaskid, client->labelio,
				   client->label_width);
	if (n < 0) {
		client->out_eof = true;
		_free_all_outgoing_msgs(client->msg_queue, client->job);
		return SLURM_ERROR;
	}

	client->out_remaining -= n;
	if (client->out_remaining == 0) {
		_free_outgoing_msg(client->out_msg, client->job);
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
_create_task_in_eio(int fd, slurmd_job_t *job)
{
	struct task_write_info *t = NULL;
	eio_obj_t *eio = NULL;

	t = (struct task_write_info *)xmalloc(sizeof(struct task_write_info));
#ifndef NDEBUG
	t->magic = TASK_IN_MAGIC;
#endif
	t->job = job;
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
			_free_incoming_msg(in->msg, in->job);
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
			_free_incoming_msg(in->msg, in->job);
			in->msg = NULL;
			return SLURM_ERROR;
		}
	}
	in->remaining -= n;
	if (in->remaining > 0)
		return SLURM_SUCCESS;

	_free_incoming_msg(in->msg, in->job);
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
		     slurmd_job_t *job, slurmd_task_info_t *task)
{
	struct task_read_info *out = NULL;
	eio_obj_t *eio = NULL;

	out = (struct task_read_info *)xmalloc(sizeof(struct task_read_info));
#ifndef NDEBUG
	out->magic = TASK_OUT_MAGIC;
#endif
	out->type = type;
	out->gtaskid = task->gtid;
	out->ltaskid = task->id;
	out->job = job;
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

	debug4("Entering _task_read for obj %x", obj);
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

	info("in _window_manager");
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
			error("read window size error: %m");
			return NULL;
		}
		memcpy(&winsz.cols, buf, 2);
		memcpy(&winsz.rows, buf+2, 2);
		ws.ws_col = ntohs(winsz.cols);
		ws.ws_row = ntohs(winsz.rows);
		debug("new pty size %u:%u", ws.ws_row, ws.ws_col);
		if (ioctl(win_info->task->to_stdin, TIOCSWINSZ, &ws))
			error("ioctl(TIOCSWINSZ): %s");
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
_spawn_window_manager(slurmd_task_info_t *task, slurmd_job_t *job)
{
	char *host, *port, *rows, *cols;
	slurm_fd pty_fd;
	slurm_addr pty_addr;
	uint16_t port_u;
	struct window_info *win_info;
	pthread_attr_t attr;
	pthread_t win_id;

#if 0
	/* NOTE: SLURM_LAUNCH_NODE_IPADDR is not available at this point */
	if (!(ip_addr = getenvp(job->env, "SLURM_LAUNCH_NODE_IPADDR"))) {
		error("SLURM_LAUNCH_NODE_IPADDR env var not set");
		return;
	}
#endif
	if (!(host = getenvp(job->env, "SLURM_SRUN_COMM_HOST"))) {
		error("SLURM_SRUN_COMM_HOST env var not set");
		return;
	}
	if (!(port = getenvp(job->env, "SLURM_PTY_PORT"))) {
		error("SLURM_PTY_PORT env var not set");
		return;
	}
	if (!(cols = getenvp(job->env, "SLURM_PTY_WIN_COL")))
		error("SLURM_PTY_WIN_COL env var not set");
	if (!(rows = getenvp(job->env, "SLURM_PTY_WIN_ROW")))
		error("SLURM_PTY_WIN_ROW env var not set");

	if (rows && cols) {
		struct winsize ws;
		ws.ws_col = atoi(cols);
		ws.ws_row = atoi(rows);
		debug("init pty size %u:%u", ws.ws_row, ws.ws_col);
		if (ioctl(task->to_stdin, TIOCSWINSZ, &ws))
			error("ioctl(TIOCSWINSZ): %s");
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
	win_info->job    = job;
	win_info->pty_fd = pty_fd;
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&win_id, &attr, &_window_manager, (void *) win_info))
		error("pthread_create(pty_conn): %m");
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
_init_task_stdio_fds(slurmd_task_info_t *task, slurmd_job_t *job)
{
	int file_flags = io_get_file_flags(job);

	/*
	 *  Initialize stdin
	 */
#ifdef HAVE_PTY_H
	if (job->pty) {
		/* All of the stdin fails unless EVERY
		 * task gets an eio object for stdin.
		 * Its not clear why that is. */
		if (task->gtid == 0) {
			int amaster, aslave;
			debug("  stdin uses a pty object");
			if (openpty(&amaster, &aslave, NULL, NULL, NULL) < 0) {
				error("stdin openpty: %m");
				return SLURM_ERROR;
			}
			task->stdin_fd = aslave;
			fd_set_close_on_exec(task->stdin_fd);
			task->to_stdin = amaster;
			fd_set_close_on_exec(task->to_stdin);
			fd_set_nonblocking(task->to_stdin);
			_spawn_window_manager(task, job);
			task->in = _create_task_in_eio(task->to_stdin, job);
			eio_new_initial_obj(job->eio, (void *)task->in);
		} else {
			xfree(task->ifname);
			task->ifname = xstrdup("/dev/null");
			task->stdin_fd = open("/dev/null", O_RDWR);
			fd_set_close_on_exec(task->stdin_fd);
			task->to_stdin = dup(task->stdin_fd);
			fd_set_nonblocking(task->to_stdin);
			task->in = _create_task_in_eio(task->to_stdin, job);
			eio_new_initial_obj(job->eio, (void *)task->in);
		}
	} else if (task->ifname != NULL) {
#else
	if (task->ifname != NULL) {
#endif
		/* open file on task's stdin */
		debug5("  stdin file name = %s", task->ifname);
		if ((task->stdin_fd = open(task->ifname, O_RDONLY)) == -1) {
			error("Could not open stdin file %s: %m", task->ifname);
			return SLURM_ERROR;
		}
		fd_set_close_on_exec(task->stdin_fd);
		task->to_stdin = -1;  /* not used */
	} else {
		/* create pipe and eio object */
		int pin[2];
		debug5("  stdin uses an eio object");
		if (pipe(pin) < 0) {
			error("stdin pipe: %m");
			return SLURM_ERROR;
		}
		task->stdin_fd = pin[0];
		fd_set_close_on_exec(task->stdin_fd);
		task->to_stdin = pin[1];
		fd_set_close_on_exec(task->to_stdin);
		fd_set_nonblocking(task->to_stdin);
		task->in = _create_task_in_eio(task->to_stdin, job);
		eio_new_initial_obj(job->eio, (void *)task->in);
	}

	/*
	 *  Initialize stdout
	 */
#ifdef HAVE_PTY_H
	if (job->pty) {
		if (task->gtid == 0) {
			task->stdout_fd = dup(task->stdin_fd);
			fd_set_close_on_exec(task->stdout_fd);
			task->from_stdout = dup(task->to_stdin);
			fd_set_close_on_exec(task->from_stdout);
			fd_set_nonblocking(task->from_stdout);
			task->out = _create_task_out_eio(task->from_stdout,
						 SLURM_IO_STDOUT, job, task);
			list_append(job->stdout_eio_objs, (void *)task->out);
			eio_new_initial_obj(job->eio, (void *)task->out);
		} else {
			xfree(task->ofname);
			task->ofname = xstrdup("/dev/null");
			task->stdout_fd = open("/dev/null", O_RDWR);
			fd_set_close_on_exec(task->stdout_fd);
			task->from_stdout = -1;  /* not used */
		}
	} else if (task->ofname != NULL &&
		   (!job->labelio || strcmp(task->ofname, "/dev/null")==0)) {
#else
	if (task->ofname != NULL &&
	    (!job->labelio || strcmp(task->ofname, "/dev/null")==0) ) {
#endif
		/* open file on task's stdout */
		debug5("  stdout file name = %s", task->ofname);
		task->stdout_fd = open(task->ofname, file_flags, 0666);
		if (task->stdout_fd == -1) {
			error("Could not open stdout file %s: %m",
			      task->ofname);
			return SLURM_ERROR;
		}
		fd_set_close_on_exec(task->stdout_fd);
		task->from_stdout = -1; /* not used */
	} else {
		/* create pipe and eio object */
		int pout[2];
		debug5("  stdout uses an eio object");
		if (pipe(pout) < 0) {
			error("stdout pipe: %m");
			return SLURM_ERROR;
		}
		task->stdout_fd = pout[1];
		fd_set_close_on_exec(task->stdout_fd);
		task->from_stdout = pout[0];
		fd_set_close_on_exec(task->from_stdout);
		fd_set_nonblocking(task->from_stdout);
		task->out = _create_task_out_eio(task->from_stdout,
						 SLURM_IO_STDOUT, job, task);
		list_append(job->stdout_eio_objs, (void *)task->out);
		eio_new_initial_obj(job->eio, (void *)task->out);
	}

	/*
	 *  Initialize stderr
	 */
#ifdef HAVE_PTY_H
	if (job->pty) {
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
			task->stderr_fd = open("/dev/null", O_RDWR);
			fd_set_close_on_exec(task->stderr_fd);
			task->from_stderr = -1;  /* not used */
		}
	} else if (task->efname != NULL &&
		   (!job->labelio || strcmp(task->efname, "/dev/null")==0)) {
#else
	if (task->efname != NULL &&
	    (!job->labelio || strcmp(task->efname, "/dev/null")==0) ) {
#endif
		/* open file on task's stdout */
		debug5("  stderr file name = %s", task->efname);
		task->stderr_fd = open(task->efname, file_flags, 0666);
		if (task->stderr_fd == -1) {
			error("Could not open stderr file %s: %m",
			      task->efname);
			return SLURM_ERROR;
		}
		fd_set_close_on_exec(task->stderr_fd);
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
						 SLURM_IO_STDERR, job, task);
		list_append(job->stderr_eio_objs, (void *)task->err);
		eio_new_initial_obj(job->eio, (void *)task->err);
	}

	return SLURM_SUCCESS;
}

int
io_init_tasks_stdio(slurmd_job_t *job)
{
	int i, rc = SLURM_SUCCESS, tmprc;

	for (i = 0; i < job->ntasks; i++) {
		tmprc = _init_task_stdio_fds(job->task[i], job);
		if (tmprc != SLURM_SUCCESS)
			rc = tmprc;
	}

	return rc;
}

int
io_thread_start(slurmd_job_t *job)
{
	pthread_attr_t attr;
	int rc = 0, retries = 0;

	slurm_attr_init(&attr);

	while (pthread_create(&job->ioid, &attr, &_io_thr, (void *)job)) {
		error("io_thread_start: pthread_create error %m");
		if (++retries > MAX_RETRIES) {
			error("io_thread_start: Can't create pthread");
			rc = -1;
			break;
		}
		usleep(10);	/* sleep and again */
	}

	slurm_attr_destroy(&attr);

	/*fatal_add_cleanup(&_fatal_cleanup, (void *) job);*/

	return rc;
}


void
_shrink_msg_cache(List cache, slurmd_job_t *job)
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
		_free_outgoing_msg(msg, job);
	}
}



static int
_send_connection_okay_response(slurmd_job_t *job)
{
	eio_obj_t *eio;
	ListIterator clients;
	struct io_buf *msg;
	struct client_io_info *client;

	msg = _build_connection_okay_message(job);
	if (!msg) {
		error(  "Could not send connection okay message because of "
			"lack of buffer space.");
		return SLURM_ERROR;
	}

	clients = list_iterator_create(job->clients);
	if (!clients)
		fatal("Could not allocate memory");

	while((eio = list_next(clients))) {
		client = (struct client_io_info *)eio->arg;
		if (client->out_eof || client->is_local_file)
			continue;

		debug5("Sent connection okay message");
		xassert(client->magic == CLIENT_IO_MAGIC);
		if (list_enqueue(client->msg_queue, msg))
			msg->ref_count++;
	}
	list_iterator_destroy(clients);

	return SLURM_SUCCESS;
}



static struct io_buf *
_build_connection_okay_message(slurmd_job_t *job)
{
	struct io_buf *msg;
	Buf packbuf;
	struct slurm_io_header header;

	if (_outgoing_buf_free(job)) {
		msg = list_dequeue(job->free_outgoing);
	} else {
		return NULL;
	}

	header.type = SLURM_IO_CONNECTION_TEST;
	header.ltaskid = 0;  /* Unused */
	header.gtaskid = 0;  /* Unused */
	header.length = 0;

	packbuf = create_buf(msg->data, io_hdr_packed_size());
	if (!packbuf)
		fatal("Failure to allocate memory for a message header");
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size();
	msg->ref_count = 0; /* make certain it is initialized */

	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);

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
	       && _outgoing_buf_free(out->job)) {
		debug5("cbuf_used = %d", cbuf_used(out->buf));
		msg = _task_build_message(out, out->job, out->buf);
		if (msg == NULL)
			return;

		/* Add message to the msg_queue of all clients */
		clients = list_iterator_create(out->job->clients);
		if (!clients)
			fatal("Could not allocate iterator");

		while((eio = list_next(clients))) {
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
			if (list_enqueue(client->msg_queue, msg))
				msg->ref_count++;
		}
		list_iterator_destroy(clients);

		/* Update the outgoing message cache */
		if (list_enqueue(out->job->outgoing_cache, msg)) {
			msg->ref_count++;
			_shrink_msg_cache(out->job->outgoing_cache, out->job);
		}
	}
}

static void
_free_incoming_msg(struct io_buf *msg, slurmd_job_t *job)
{
	msg->ref_count--;
	if (msg->ref_count == 0) {
		/* Put the message back on the free List */
		list_enqueue(job->free_incoming, msg);

		/* Kick the event IO engine */
		eio_signal_wakeup(job->eio);
	}
}

static void
_free_outgoing_msg(struct io_buf *msg, slurmd_job_t *job)
{
	int i;

	msg->ref_count--;
	if (msg->ref_count == 0) {
		/* Put the message back on the free List */
		list_enqueue(job->free_outgoing, msg);

		/* Try packing messages from tasks' output cbufs */
		if (job->task == NULL)
			return;
		for (i = 0; i < job->ntasks; i++) {
			if (job->task[i]->err != NULL) {
				_route_msg_task_to_client(job->task[i]->err);
				if (!_outgoing_buf_free(job))
					break;
			}
			if (job->task[i]->out != NULL) {
				_route_msg_task_to_client(job->task[i]->out);
				if (!_outgoing_buf_free(job))
					break;
			}
		}
		/* Kick the event IO engine */
		eio_signal_wakeup(job->eio);
	}
}

static void
_free_all_outgoing_msgs(List msg_queue, slurmd_job_t *job)
{
	ListIterator msgs;
	struct io_buf *msg;

	msgs = list_iterator_create(msg_queue);
	if (!msgs)
		fatal("Could not allocate iterator");
	while((msg = list_next(msgs))) {
		_free_outgoing_msg(msg, job);
	}
	list_iterator_destroy(msgs);
}

extern void
io_close_task_fds(slurmd_job_t *job)
{
	int i;

	for (i = 0; i < job->ntasks; i++) {
		close(job->task[i]->stdin_fd);
		close(job->task[i]->stdout_fd);
		close(job->task[i]->stderr_fd);
	}
}

void
io_close_all(slurmd_job_t *job)
{
	int devnull;
#if 0
	int i;
	for (i = 0; i < job->ntasks; i++)
		_io_finalize(job->task[i]);
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
			error("Unable to dup /dev/null onto stderr\n");
	}

	/* Signal IO thread to close appropriate
	 * client connections
	 */
	eio_signal_shutdown(job->eio);
}

void
io_close_local_fds(slurmd_job_t *job)
{
	ListIterator clients;
	eio_obj_t *eio;
	int rc;
	struct client_io_info *client;

	if (job == NULL || job->clients == NULL)
		return;

	clients = list_iterator_create(job->clients);
	if (!clients)
		fatal("Could not allocate iterator");
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
}



static void *
_io_thr(void *arg)
{
	slurmd_job_t *job = (slurmd_job_t *) arg;
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
	rc = eio_handle_mainloop(job->eio);
	debug("IO handler exited, rc=%d", rc);
	return (void *)1;
}

/*
 *  Add a client to the job's client list that will write stdout and/or
 *  stderr from the slurmstepd.  The slurmstepd handles the write when
 *  a file is created per node or per task, and the output needs to be
 *  modified in some way, like labelling lines with the task number.
 */
int
io_create_local_client(const char *filename, int file_flags,
		       slurmd_job_t *job, bool labelio,
		       int stdout_tasks, int stderr_tasks)
{
	int fd = -1;
	struct client_io_info *client;
	eio_obj_t *obj;
	int tmp;

	fd = open(filename, file_flags, 0666);
	if (fd == -1) {
		return ESLURMD_IO_ERROR;
	}
	fd_set_close_on_exec(fd);

	/* Now set up the eio object */
	client = xmalloc(sizeof(struct client_io_info));
#ifndef NDEBUG
	client->magic = CLIENT_IO_MAGIC;
#endif
	client->job = job;
	client->msg_queue = list_create(NULL); /* FIXME - destructor */

	client->ltaskid_stdout = stdout_tasks;
	client->ltaskid_stderr = stderr_tasks;
	client->labelio = labelio;
	client->is_local_file = true;

	client->label_width = 1;
	tmp = job->ntasks-1;
	while ((tmp /= 10) > 0)
		client->label_width++;


	obj = eio_obj_create(fd, &local_file_ops, (void *)client);
	list_append(job->clients, (void *)obj);
	eio_new_initial_obj(job->eio, (void *)obj);
	debug5("Now handling %d IO Client object(s)", list_count(job->clients));

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
io_initial_client_connect(srun_info_t *srun, slurmd_job_t *job,
			  int stdout_tasks, int stderr_tasks)
{
	int sock = -1;
	struct client_io_info *client;
	eio_obj_t *obj;

	debug4 ("adding IO connection (logical node rank %d)", job->nodeid);

	if (srun->ioaddr.sin_addr.s_addr) {
		char         ip[256];
		uint16_t     port;
		slurm_get_ip_str(&srun->ioaddr, &port, ip, sizeof(ip));
		if (ntohs(port) == 0) {
			debug3("No IO connection requested");
			return SLURM_SUCCESS;
		}
		debug4("connecting IO back to %s:%d", ip, ntohs(port));
	}

	if ((sock = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail?
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}

	fd_set_blocking(sock);  /* just in case... */

	_send_io_init_msg(sock, srun->key, job);

	debug5("  back from _send_io_init_msg");
	fd_set_nonblocking(sock);
	fd_set_close_on_exec(sock);

	/* Now set up the eio object */
	client = xmalloc(sizeof(struct client_io_info));
#ifndef NDEBUG
	client->magic = CLIENT_IO_MAGIC;
#endif
	client->job = job;
	client->msg_queue = list_create(NULL); /* FIXME - destructor */

	client->ltaskid_stdout = stdout_tasks;
	client->ltaskid_stderr = stderr_tasks;
	client->labelio = false;
	client->label_width = 0;
	client->is_local_file = false;

	obj = eio_obj_create(sock, &client_ops, (void *)client);
	list_append(job->clients, (void *)obj);
	eio_new_initial_obj(job->eio, (void *)obj);
	debug5("Now handling %d IO Client object(s)", list_count(job->clients));

	return SLURM_SUCCESS;
}

/*
 * Initiate a TCP connection back to a waiting client (e.g. srun).
 *
 * Create a new eio client object and wake up the eio engine so that
 * it can see the new object.
 */
int
io_client_connect(srun_info_t *srun, slurmd_job_t *job)
{
	int sock = -1;
	struct client_io_info *client;
	eio_obj_t *obj;

	debug4 ("adding IO connection (logical node rank %d)", job->nodeid);

	if (srun->ioaddr.sin_addr.s_addr) {
		char         ip[256];
		uint16_t     port;
		slurm_get_ip_str(&srun->ioaddr, &port, ip, sizeof(ip));
		debug4("connecting IO back to %s:%d", ip, ntohs(port));
	}

	if ((sock = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail?
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}

	fd_set_blocking(sock);  /* just in case... */

	_send_io_init_msg(sock, srun->key, job);

	debug5("  back from _send_io_init_msg");
	fd_set_nonblocking(sock);
	fd_set_close_on_exec(sock);

	/* Now set up the eio object */
	client = xmalloc(sizeof(struct client_io_info));
#ifndef NDEBUG
	client->magic = CLIENT_IO_MAGIC;
#endif
	client->job = job;
	client->msg_queue = NULL; /* initialized in _client_writable */

	client->ltaskid_stdout = -1;     /* accept from all tasks */
	client->ltaskid_stderr = -1;     /* accept from all tasks */
	client->labelio = false;
	client->label_width = 0;
	client->is_local_file = false;

	/* client object adds itself to job->clients in _client_writable */

	obj = eio_obj_create(sock, &client_ops, (void *)client);
	eio_new_obj(job->eio, (void *)obj);

	debug5("New IO Client object added");

	return SLURM_SUCCESS;
}

static int
_send_io_init_msg(int sock, srun_key_t *key, slurmd_job_t *job)
{
	struct slurm_io_init_msg msg;

	memcpy(msg.cred_signature, key->data, SLURM_IO_KEY_SIZE);
	msg.nodeid = job->nodeid;
	if (job->stdout_eio_objs == NULL)
		msg.stdout_objs = 0;
	else
		msg.stdout_objs = list_count(job->stdout_eio_objs);
	if (job->stderr_eio_objs == NULL)
		msg.stderr_objs = 0;
	else
		msg.stderr_objs = list_count(job->stderr_eio_objs);

	if (io_init_msg_write_to_fd(sock, &msg) != SLURM_SUCCESS) {
		error("Couldn't sent slurm_io_init_msg");
		return SLURM_ERROR;
	}


	return SLURM_SUCCESS;
}

/*
 * dup the appropriate file descriptors onto the task's
 * stdin, stdout, and stderr.
 *
 * Close the server's end of the stdio pipes.
 */
int
io_dup_stdio(slurmd_task_info_t *t)
{
	if (dup2(t->stdin_fd, STDIN_FILENO  ) < 0) {
		error("dup2(stdin): %m");
		return SLURM_FAILURE;
	}
	fd_set_noclose_on_exec(STDIN_FILENO);

	if (dup2(t->stdout_fd, STDOUT_FILENO) < 0) {
		error("dup2(stdout): %m");
		return SLURM_FAILURE;
	}
	fd_set_noclose_on_exec(STDOUT_FILENO);

	if (dup2(t->stderr_fd, STDERR_FILENO) < 0) {
		error("dup2(stderr): %m");
		return SLURM_FAILURE;
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
	Buf packbuf;

	debug4("Entering _send_eof_msg");
	out->eof_msg_sent = true;

	if (_outgoing_buf_free(out->job)) {
		msg = list_dequeue(out->job->free_outgoing);
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
	if (!packbuf)
		fatal("Failure to allocate memory for a message header");

	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* Add eof message to the msg_queue of all clients */
	clients = list_iterator_create(out->job->clients);
	if (!clients)
		fatal("Could not allocate iterator");
	while((eio = list_next(clients))) {
		client = (struct client_io_info *)eio->arg;
		debug5("======================== Enqueued eof message");
		xassert(client->magic == CLIENT_IO_MAGIC);

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

		if (list_enqueue(client->msg_queue, msg))
			msg->ref_count++;
	}
	list_iterator_destroy(clients);

	debug4("Leaving  _send_eof_msg");
}



static struct io_buf *
_task_build_message(struct task_read_info *out, slurmd_job_t *job, cbuf_t cbuf)
{
	struct io_buf *msg;
	char *ptr;
	Buf packbuf;
	bool must_truncate = false;
	int avail;
	struct slurm_io_header header;
	int n;

	debug4("Entering _task_build_message");
	if (_outgoing_buf_free(job)) {
		msg = list_dequeue(job->free_outgoing);
	} else {
		return NULL;
	}
	ptr = msg->data + io_hdr_packed_size();

	if (job->buffered_stdio) {
		avail = cbuf_peek_line(cbuf, ptr, MAX_MSG_LEN, 1);
		if (avail >= MAX_MSG_LEN)
			must_truncate = true;
		else if (avail == 0 && cbuf_used(cbuf) >= MAX_MSG_LEN)
			must_truncate = true;
	}

	debug5("  buffered_stdio is %s", job->buffered_stdio ? "true" : "false");
	debug5("  must_truncate  is %s", must_truncate ? "true" : "false");

	/*
	 * If eof has been read from a tasks stdout or stderr, we need to
	 * ignore normal line buffering and send the buffer immediately.
	 * Hence the "|| out->eof".
	 */
	if (must_truncate || !job->buffered_stdio || out->eof) {
		n = cbuf_read(cbuf, ptr, MAX_MSG_LEN);
	} else {
		n = cbuf_read_line(cbuf, ptr, MAX_MSG_LEN, -1);
		if (n == 0) {
			debug5("  partial line in buffer, ignoring");
			debug4("Leaving  _task_build_message");
			list_enqueue(job->free_outgoing, msg);
			return NULL;
		}
	}

	header.type = out->type;
	header.ltaskid = out->ltaskid;
	header.gtaskid = out->gtaskid;
	header.length = n;

	debug5("  header.length = %d", n);
	packbuf = create_buf(msg->data, io_hdr_packed_size());
	if (!packbuf)
		fatal("Failure to allocate memory for a message header");
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);

	debug4("Leaving  _task_build_message");
	return msg;
}

struct io_buf *
alloc_io_buf(void)
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
_incoming_buf_free(slurmd_job_t *job)
{
	struct io_buf *buf;

	if (list_count(job->free_incoming) > 0) {
		return true;
	} else if (job->incoming_count < STDIO_MAX_FREE_BUF) {
		buf = alloc_io_buf();
		if (buf != NULL) {
			list_enqueue(job->free_incoming, buf);
			job->incoming_count++;
			return true;
		}
	}

	return false;
}

static bool
_outgoing_buf_free(slurmd_job_t *job)
{
	struct io_buf *buf;

	if (list_count(job->free_outgoing) > 0) {
		return true;
	} else if (job->outgoing_count < STDIO_MAX_FREE_BUF) {
		buf = alloc_io_buf();
		if (buf != NULL) {
			list_enqueue(job->free_outgoing, buf);
			job->outgoing_count++;
			return true;
		}
	}

	return false;
}

/**********************************************************************
 * Functions specific to "user managed" IO
 **********************************************************************/
static int
_user_managed_io_connect(srun_info_t *srun, uint32_t gtid)
{
	int fd;
	task_user_managed_io_msg_t user_io_msg;
	slurm_msg_t msg;

	slurm_msg_t_init(&msg);
	msg.msg_type = TASK_USER_MANAGED_IO_STREAM;
	msg.data = &user_io_msg;
	user_io_msg.task_id = gtid;

	fd = slurm_open_msg_conn(&srun->resp_addr);
	if (fd == -1)
		return -1;

	if (slurm_send_node_msg(fd, &msg) == -1) {
		close(fd);
		return -1;
	}
	return fd;
}

/*
 * This function sets the close-on-exec flag on the socket descriptor.
 * io_dup_stdio will will remove the close-on-exec flag for just one task's
 * file descriptors.
 */
int
user_managed_io_client_connect(int ntasks, srun_info_t *srun,
			       slurmd_task_info_t **tasks)
{
	int fd;
	int i;

	for (i = 0; i < ntasks; i++) {
		fd = _user_managed_io_connect(srun, tasks[i]->gtid);
		if (fd == -1)
			return SLURM_ERROR;
		fd_set_close_on_exec(fd);
		tasks[i]->stdin_fd = fd;
		tasks[i]->to_stdin = -1;
		tasks[i]->stdout_fd = fd;
		tasks[i]->from_stdout = -1;
		tasks[i]->stderr_fd = fd;
		tasks[i]->from_stderr = -1;
	}

	return SLURM_SUCCESS;
}


void
io_find_filename_pattern( slurmd_job_t *job,
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

	for (ii = 0; ii < job->ntasks; ii++) {
		if (job->task[ii]->ofname == NULL) {
			of_num_null++;
			of_lastnull = ii;
		} else if (strcmp(job->task[ii]->ofname, "/dev/null")==0) {
			of_num_devnull++;
		}

		if (job->task[ii]->efname == NULL) {
			ef_num_null++;
			ef_lastnull = ii;
		} else if (strcmp(job->task[ii]->efname, "/dev/null")==0) {
			ef_num_devnull++;
		}
	}
	if (of_num_null == job->ntasks)
		*outpattern = SLURMD_ALL_NULL;

	if (ef_num_null == job->ntasks)
		*errpattern = SLURMD_ALL_NULL;

	if (of_num_null == 1 && of_num_devnull == job->ntasks-1)
		*outpattern = SLURMD_ONE_NULL;

	if (ef_num_null == 1 && ef_num_devnull == job->ntasks-1)
		*errpattern = SLURMD_ONE_NULL;

	if (*outpattern == SLURMD_ALL_NULL && *errpattern == SLURMD_ALL_NULL)
		*same_out_err_files = true;

	if (*outpattern == SLURMD_ONE_NULL && *errpattern == SLURMD_ONE_NULL &&
	    of_lastnull == ef_lastnull)
		*same_out_err_files = true;

	if (*outpattern != SLURMD_UNKNOWN && *errpattern != SLURMD_UNKNOWN)
		return;

	for (ii = 1; ii < job->ntasks; ii++) {
		if (!job->task[ii]->ofname || !job->task[0]->ofname ||
		    strcmp(job->task[ii]->ofname, job->task[0]->ofname) != 0)
			of_all_same = false;

		if (!job->task[ii]->efname || !job->task[0]->efname ||
		    strcmp(job->task[ii]->efname, job->task[0]->efname) != 0)
			ef_all_same = false;
	}

	if (of_all_same && *outpattern == SLURMD_UNKNOWN)
		*outpattern = SLURMD_ALL_SAME;

	if (ef_all_same && *errpattern == SLURMD_UNKNOWN)
		*errpattern = SLURMD_ALL_SAME;

	if (job->task[0]->ofname && job->task[0]->efname &&
	    strcmp(job->task[0]->ofname, job->task[0]->efname)==0)
		*same_out_err_files = true;

	if (*outpattern != SLURMD_UNKNOWN && *errpattern != SLURMD_UNKNOWN)
		return;

	for (ii = 0; ii < job->ntasks-1; ii++) {
		for (jj = ii+1; jj < job->ntasks; jj++) {

			if (!job->task[ii]->ofname || !job->task[jj]->ofname ||
			    strcmp(job->task[ii]->ofname,
				   job->task[jj]->ofname) == 0)
				of_all_unique = false;

			if (!job->task[ii]->efname || !job->task[jj]->efname ||
			    strcmp(job->task[ii]->efname,
				   job->task[jj]->efname) == 0)
				ef_all_unique = false;
		}
	}

	if (of_all_unique)
		*outpattern = SLURMD_ALL_UNIQUE;

	if (ef_all_unique)
		*errpattern = SLURMD_ALL_UNIQUE;

	if (of_all_unique && ef_all_unique) {
		*same_out_err_files = true;
		for (ii = 0; ii < job->ntasks; ii++) {
			if (job->task[ii]->ofname &&
			    job->task[ii]->efname &&
			    strcmp(job->task[ii]->ofname,
				   job->task[ii]->efname) != 0) {
				*same_out_err_files = false;
				break;
			}
		}
	}
}


int
io_get_file_flags(slurmd_job_t *job)
{
	slurm_ctl_conf_t *conf;
	int file_flags;

	/* set files for opening stdout/err */
	if (job->open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (job->open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}
	return file_flags;
}
