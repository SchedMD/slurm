/****************************************************************************\
 *  io.c - process stdin, stdout, and stderr for parallel jobs.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
 *  UCRL-CODE-217948.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

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
#include "src/common/io_hdr.h"
#include "src/common/net.h"
#include "src/common/dist_tasks.h"

#include "src/srun/io.h"
#include "src/srun/srun_job.h"
#include "src/srun/opt.h"

#define MAX_RETRIES 3

static int    fmt_width       = 0;

static void     _init_stdio_eio_objs(srun_job_t *job);
static void	_handle_io_init_msg(int fd, srun_job_t *job);
static int      _read_io_init_msg(int fd, srun_job_t *job, char *host);
static int      _wid(int n);
static bool     _incoming_buf_free(srun_job_t *job);
static bool     _outgoing_buf_free(srun_job_t *job);

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
	srun_job_t *job;

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
	srun_job_t *job;

	/* outgoing variables */
	List msg_queue;
	struct io_buf *out_msg;
	int32_t out_remaining;
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
	srun_job_t *job;

	/* header contains destination of file input */
	struct slurm_io_header header;

	bool eof;
	bool was_blocking;
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
	srun_job_t *job = (srun_job_t *)obj->arg;

	debug3("Called _listening_socket_read");
	_handle_io_init_msg(obj->fd, job);

	return (0);
}

static void
_set_listensocks_nonblocking(srun_job_t *job)
{
	int i;
	for (i = 0; i < job->num_listen; i++) 
		fd_set_nonblocking(job->listensock[i]);
}

/**********************************************************************
 * IO server socket functions
 **********************************************************************/
static eio_obj_t *
_create_server_eio_obj(int fd, srun_job_t *job,
		       int stdout_objs, int stderr_objs)
{
	struct server_io_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct server_io_info *)xmalloc(sizeof(struct server_io_info));
	info->job = job;
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

	if (!_outgoing_buf_free(s->job)) {
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
		if (_outgoing_buf_free(s->job)) {
			s->in_msg = list_dequeue(s->job->free_outgoing);
		} else {
			debug("List free_outgoing is empty!");
			return SLURM_ERROR;
		}

		n = io_hdr_read_fd(obj->fd, &s->header);
		if (n <= 0) { /* got eof or error on socket read */
			debug3(  "got eof or error on _server_read header");
			close(obj->fd);
			obj->fd = -1;
			s->in_eof = true;
			s->out_eof = true;
			list_enqueue(s->job->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		if (s->header.length == 0) { /* eof message */
			if (s->header.type == SLURM_IO_STDOUT)
				s->remote_stdout_objs--;
			else if (s->header.type == SLURM_IO_STDERR)
				s->remote_stderr_objs--;
			else
				error("Unrecognized output message type");
			list_enqueue(s->job->free_outgoing, s->in_msg);
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
			debug3(  "got eof or error on _server_read body");
			close(obj->fd);
			obj->fd = -1;
			s->in_eof = true;
			s->out_eof = true;
			list_enqueue(s->job->free_outgoing, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}

/* 		*(char *)(buf + n) = '\0'; */
/* 		debug3("\"%s\"", buf); */
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
			obj = s->job->stdout_obj;
		else
			obj = s->job->stderr_obj;
		info = (struct file_write_info *) obj->arg;
		if (info->eof)
			/* this output is closed, discard message */
			list_enqueue(s->job->free_outgoing, s->in_msg);
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
	if (s->out_msg->ref_count == 0)
		list_enqueue(s->job->free_incoming, s->out_msg);
	else
		debug3("  Could not free msg!!");
	s->out_msg = NULL;

	return SLURM_SUCCESS;
}

/**********************************************************************
 * File write functions
 **********************************************************************/
extern eio_obj_t *
create_file_write_eio_obj(int fd, srun_job_t *job)
{
	struct file_write_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct file_write_info *)
		xmalloc(sizeof(struct file_write_info));
	info->job = job;
	info->msg_queue = list_create(NULL); /* FIXME! Add destructor */
	info->out_msg = NULL;
	info->out_remaining = 0;
	info->eof = false;

	eio = eio_obj_create(fd, &file_write_ops, (void *)info);

	return eio;
}

static int _write_label(int fd, int taskid)
{
	int n;
	int left = fmt_width + 2;
	char buf[16];
	void *ptr = buf;

	snprintf(buf, 16, "%0*d: ", fmt_width, taskid);
	while (left > 0) {
	again:
		if ((n = write(fd, ptr, fmt_width+2)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_label");
				goto again;
			}
			error("In _write_label: %m");
			return SLURM_ERROR;
		}
		left -= n;
		ptr += n;
	}

	return SLURM_SUCCESS;
}

static int _write_newline(int fd)
{
	int n;

	debug2("Called _write_newline");
again:
	if ((n = write(fd, "\n", 1)) < 0) {
		if (errno == EINTR
		    || errno == EAGAIN
		    || errno == EWOULDBLOCK) {
			goto again;
		}
		error("In _write_newline: %m");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * Blocks until write is complete, regardless of the file
 * descriptor being in non-blocking mode.
 */
static int _write_line(int fd, void *buf, int len)
{
	int n;
	int left = len;
	void *ptr = buf;

	debug2("Called _write_line");
	while (left > 0) {
	again:
		if ((n = write(fd, ptr, left)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_line");
				goto again;
			}
			return -1;
		}
		left -= n;
		ptr += n;
	}
	
	return len;
}


/*
 * Write as many lines from the message as possible.  Return
 * the number of bytes from the message that have been written,
 * or 0 on eof, or -1 on error.
 *
 * Prepend a label of the task number if labelio parameter was
 * specified.
 *
 * If the message ends in a partial line (line does not end
 * in a '\n'), then add a newline to the output file, but only
 * in labelio mode.
 */
static int _write_msg(int fd, void *buf, int len, int taskid)
{
	void *start;
	void *end;
	int remaining = len;
	int written = 0;
	int line_len;
	int rc = SLURM_SUCCESS;

	while (remaining > 0) {
		start = buf + written;
		end = memchr(start, '\n', remaining);
		if (opt.labelio)
			if (_write_label(fd, taskid) != SLURM_SUCCESS)
				goto done;
		if (end == NULL) { /* no newline found */
			rc = _write_line(fd, start, remaining);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
			if (opt.labelio)
				if (_write_newline(fd) != SLURM_SUCCESS)
					goto done;
		} else {
			line_len = (int)(end - start) + 1;
			rc = _write_line(fd, start, line_len);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
		}

	}
done:
	if (written > 0)
		return written;
	else
		return rc;
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
	if (!info->eof) {
		ptr = info->out_msg->data + (info->out_msg->length
					     - info->out_remaining);
		if ((n = _write_msg(obj->fd, ptr,
				    info->out_remaining,
				    info->out_msg->header.gtaskid)) < 0) {
			list_enqueue(info->job->free_outgoing, info->out_msg);
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
		list_enqueue(info->job->free_outgoing, info->out_msg);
	info->out_msg = NULL;
	debug2("Leaving  _file_write");

	return SLURM_SUCCESS;
}

/**********************************************************************
 * File read functions
 **********************************************************************/
extern eio_obj_t *
create_file_read_eio_obj(int fd, srun_job_t *job,
			 uint16_t type, uint16_t gtaskid)
{
	struct file_read_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct file_read_info *)
		xmalloc(sizeof(struct file_read_info));
	info->job = job;
	info->header.type = type;
	info->header.gtaskid = gtaskid;
	/* FIXME!  Need to set ltaskid based on gtaskid */
	info->header.ltaskid = (uint16_t)-1;
	info->eof = false;

	if (fd_is_blocking(fd)) {
		fd_set_nonblocking(fd);
		info->was_blocking = true;
	} else {
		info->was_blocking = false;
	}
	eio = eio_obj_create(fd, &file_read_ops, (void *)info);

	return eio;
}

static bool _file_readable(eio_obj_t *obj)
{
	struct file_read_info *info = (struct file_read_info *) obj->arg;

	debug2("Called _file_readable");

	if (info->job->ioservers_ready < info->job->nhosts) {
		debug3("  false, all ioservers not yet initialized");
		return false;
	}

	if (info->eof) {
		debug3("  false, eof");
		return false;
	}
	if (obj->shutdown == true) {
		debug3("  false, shutdown");
		/* if the file descriptor was in blocking mode before we set it
		 * to O_NONBLOCK, then set it back to blocking mode before
		 * closing */
		if (info->was_blocking)
			fd_set_blocking(obj->fd);
		close(obj->fd);
		obj->fd = -1;
		info->eof = true;
		return false;
	}
	if (_incoming_buf_free(info->job))
		return true;

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
	if (_incoming_buf_free(info->job)) {
		msg = list_dequeue(info->job->free_incoming);
	} else {
		debug3("  List free_incoming is empty, no file read");
		return SLURM_SUCCESS;
	}

	ptr = msg->data + io_hdr_packed_size();

again:
	if ((len = read(obj->fd, ptr, MAX_MSG_LEN)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug("_file_read returned %s",
				      errno==EAGAIN?"EAGAIN":"EWOULDBLOCK");
				list_enqueue(info->job->free_incoming, msg);
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
		for (i = 0; i < info->job->nhosts; i++) {
			msg->ref_count++;
			if (info->job->ioserver[i] == NULL)
				fatal("ioserver stream not yet initialized");
			server = info->job->ioserver[i]->arg;
			list_enqueue(server->msg_queue, msg);
		}
	} else if (header.type == SLURM_IO_STDIN) {
		int nodeid;
		struct server_io_info *server;
		debug("SLURM_IO_STDIN");
		msg->ref_count = 1;
		nodeid = step_layout_host_id(info->job->step_layout, 
				             header.gtaskid);
		debug3("  taskid %d maps to nodeid %d", header.gtaskid, nodeid);
		server = info->job->ioserver[nodeid]->arg;
		list_enqueue(server->msg_queue, msg);
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
_io_thr_internal(void *job_arg)
{
	srun_job_t *job  = (srun_job_t *) job_arg;
	sigset_t set;

	xassert(job != NULL);

	debug3("IO thread pid = %lu", (unsigned long) getpid());

	/* Block SIGHUP because it is interrupting file stream functions
	 * (fprintf, fflush, etc.) and causing data loss on stdout.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
 	pthread_sigmask(SIG_BLOCK, &set, NULL);

	_set_listensocks_nonblocking(job);

	/* start the eio engine */
	eio_handle_mainloop(job->eio);

	debug("IO thread exiting");

	return NULL;
}

static eio_obj_t *
_create_listensock_eio(int fd, srun_job_t *job)
{
	eio_obj_t *eio = NULL;

	eio = eio_obj_create(fd, &listening_socket_ops, (void *)job);

	return eio;
}

int
io_thr_create(srun_job_t *job)
{
	int i, retries = 0;
	pthread_attr_t attr;

	if (opt.labelio)
		fmt_width = _wid(opt.nprocs);

	if (!opt.allocate && !opt.batch)
		_init_stdio_eio_objs(job);

	for (i = 0; i < job->num_listen; i++) {
		eio_obj_t *obj;

		if (net_stream_listen(&job->listensock[i],
				      &job->listenport[i]) < 0)
			fatal("unable to initialize stdio listen socket: %m");
		debug("initialized stdio listening socket, port %d\n",
		      ntohs(job->listenport[i]));
		/*net_set_low_water(job->listensock[i], 140);*/
		obj = _create_listensock_eio(job->listensock[i], job);
		eio_new_initial_obj(job->eio, obj);
	}

	xsignal(SIGTTIN, SIG_IGN);

	slurm_attr_init(&attr);
	while ((errno = pthread_create(&job->ioid, &attr,
				      &_io_thr_internal, (void *) job))) {
		if (++retries > MAX_RETRIES) {
			error ("pthread_create error %m");
			slurm_attr_destroy(&attr);
			return SLURM_ERROR;
		}
		sleep(1);	/* sleep and try again */
	}
	slurm_attr_destroy(&attr);
	debug("Started IO server thread (%lu)", (unsigned long) job->ioid);

	return SLURM_SUCCESS;
}

static int
_read_io_init_msg(int fd, srun_job_t *job, char *host)
{
	struct slurm_io_init_msg msg;
	char *sig;
	int siglen;

	if (io_init_msg_read_from_fd(fd, &msg) != SLURM_SUCCESS) {
		error("failed reading io init message");
		goto fail;
	}
	if (slurm_cred_get_signature(job->cred, &sig, &siglen) < 0) {
		error ("Couldn't get existing cred signature");
		goto fail;
	}
	if (io_init_msg_validate(&msg, sig) < 0) {
		goto fail; 
	}
	if (msg.nodeid >= job->nhosts) {
		error ("Invalid nodeid %d from %s", msg.nodeid, host);
		goto fail;
	}
	debug2("Validated IO connection from %s, node rank %u, sd=%d",
	       host, msg.nodeid, fd);

	net_set_low_water(fd, 1);
	debug3("msg.stdout_objs = %d", msg.stdout_objs);
	debug3("msg.stderr_objs = %d", msg.stderr_objs);
	job->ioserver[msg.nodeid] = _create_server_eio_obj(fd, job,
							   msg.stdout_objs,
							   msg.stderr_objs);
	/* Normally using eio_new_initial_obj while the eio mainloop
	 * is running is not safe, but since this code is running
	 * inside of the eio mainloop there should be no problem.
	 */
	eio_new_initial_obj(job->eio, job->ioserver[msg.nodeid]);
	job->ioservers_ready++;

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
_handle_io_init_msg(int fd, srun_job_t *job)
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
		 * flag from the listening socket [fd], so we need to explicitly
		 * set it back to blocking mode.
		 * (XXX: This should eventually be fixed by making
		 *  reads of IO headers nonblocking)
		 */
		fd_set_blocking(sd);

		/*
		 * Read IO header and update job structure appropriately
		 */
		if (_read_io_init_msg(sd, job, buf) < 0)
			continue;

		fd_set_nonblocking(sd);
	}
}


/*
 * io_node_fail - Some nodes have failed.  Identify affected I/O streams.
 * Flag them as done and signal the I/O thread.
 */
extern int 
io_node_fail(char *nodelist, srun_job_t *job)
{
	hostlist_t fail_list = hostlist_create(nodelist);
	char *node_name;
	int node_inx;
	int rc = SLURM_SUCCESS;

	if (!fail_list) {
		error("Invalid node list `%s' specified", nodelist);
		return SLURM_ERROR;
 	}

	while ( (node_name = hostlist_shift(fail_list)) ) {
		for (node_inx=0; node_inx<job->nhosts; node_inx++) {
			if (strcmp(node_name, 
				   job->step_layout->host[node_inx]))
				continue;
			break;
		}
		if(node_inx < job->nhosts) 
			job->ioserver[node_inx]->shutdown = true;
		else {
			error("Invalid node name `%s' specified for job", 
			      node_name);
			rc = SLURM_ERROR;
		}
	}

	eio_signal_wakeup(job->eio);
	hostlist_destroy(fail_list);
	return rc;
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

static int
_is_local_file (io_filename_t *fname)
{
	if (fname->name == NULL)
		return 1;
	
	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

static void
_init_stdio_eio_objs(srun_job_t *job)
{
	int infd, outfd, errfd;
	bool err_shares_out = false;

	/*
	 * build stdin eio_obj_t
	 */
	if (_is_local_file(job->ifname)) {
		uint16_t type, destid;
		if (job->ifname->name == NULL || job->ifname->taskid != -1) {
			infd = STDIN_FILENO;
		} else {
			infd = open(job->ifname->name, O_RDONLY);
			if (infd == -1)
				fatal("Could not open stdin file: %m");
		}
		fd_set_close_on_exec(infd);
		if (job->ifname->type == IO_ONE) {
			type = SLURM_IO_STDIN;
			destid = job->ifname->taskid;
		} else {
			type = SLURM_IO_ALLSTDIN;
			destid = -1;
		}
		job->stdin_obj = create_file_read_eio_obj(infd, job,
							  type, destid);
		eio_new_initial_obj(job->eio, job->stdin_obj);
	}

	/*
	 * build stdout eio_obj_t
	 */
	if (_is_local_file(job->ofname)) {
		int refcount;
		if (job->ofname->name == NULL) {
			outfd = STDOUT_FILENO;
		} else {
			outfd = open(job->ofname->name,
				     O_CREAT|O_WRONLY|O_TRUNC, 0644);
			if (outfd == -1)
				fatal("Could not open stdout file: %m");
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !strcmp(job->ofname->name, job->efname->name)) {
			refcount = job->ntasks * 2;
			err_shares_out = true;
		} else {
			refcount = job->ntasks;
		}
		job->stdout_obj = create_file_write_eio_obj(outfd, job);
		eio_new_initial_obj(job->eio, job->stdout_obj);
	}

	/*
	 * build a seperate stderr eio_obj_t only if stderr is not sharing
	 * the stdout eio_obj_t
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		job->stderr_obj = job->stdout_obj;
	} else if (_is_local_file(job->efname)) {
		int refcount;
		if (job->efname->name == NULL) {
			errfd = STDERR_FILENO;
		} else {
			errfd = open(job->efname->name,
				     O_CREAT|O_WRONLY|O_TRUNC, 0644);
			if (errfd == -1)
				fatal("Could not open stderr file: %m");
		}
		refcount = job->ntasks;
		job->stderr_obj = create_file_write_eio_obj(errfd, job);
		eio_new_initial_obj(job->eio, job->stderr_obj);
	}
}

static bool
_incoming_buf_free(srun_job_t *job)
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
_outgoing_buf_free(srun_job_t *job)
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
