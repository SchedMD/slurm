/****************************************************************************\
 *  io.c - process stdin, stdout, and stderr for parallel jobs.
 *  $Id$
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

#include "src/srun/io.h"
#include "src/srun/srun_job.h"
#include "src/srun/opt.h"

static int    fmt_width       = 0;

static void	_handle_io_init_msg(int fd, srun_job_t *job);
static ssize_t	_readx(int fd, char *buf, size_t maxbytes);
static int      _read_io_init_msg(int fd, srun_job_t *job, char *host);
static int      _wid(int n);

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
};


/**********************************************************************
 * Listening socket functions
 **********************************************************************/
static bool 
_listening_socket_readable(eio_obj_t *obj)
{
	debug3("Called _listening_socket_readable");
	if (obj->shutdown == true) {
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
_create_server_eio_obj(int fd, srun_job_t *job)
{
	struct server_io_info *info = NULL;
	eio_obj_t *eio = NULL;

	info = (struct server_io_info *)xmalloc(sizeof(struct server_io_info));
	info->job = job;
	info->in_msg = NULL;
	info->in_remaining = 0;
	info->in_eof = false;
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
	struct file_write_info *fout, *ferr;
	int i;

	debug2("Called _server_readable");

	if (s->in_eof) {
		debug3("  false, eof");
		return false;
	}

	for (i = 0; i < s->job->ntasks; i++) {
		fout = s->job->iostdout[i]->arg;
		ferr = s->job->iostderr[i]->arg;
		if (fout->eof == false) {
			debug3("  task %d stdout no eof", i);
		}
		if (ferr->eof == false) {
			debug3("  task %d stderr no eof", i);
		}
		if (fout->eof == false || ferr->eof == false)
			return true;
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

	debug3("Entering _server_read");
	if (s->in_msg == NULL) {
		s->in_msg = list_dequeue(s->job->free_io_buf);
		if (s->in_msg == NULL) {
			debug("List free_io_buf is empty!");
			return SLURM_ERROR;
		}

		n = io_hdr_read_fd(obj->fd, &s->header);
		if (n == 0) { /* got eof on socket read */
			debug3(  "got eof on _server_read header");
			s->in_eof = true;
			list_enqueue(s->job->free_io_buf, s->in_msg);
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
			/* FIXME handle error */
			return SLURM_ERROR;
		}
		if (n == 0) { /* got eof  */
			debug3(  "got eof on _server_read body");
			s->in_eof = true;
			list_enqueue(s->job->free_io_buf, s->in_msg);
			s->in_msg = NULL;
			return SLURM_SUCCESS;
		}
		debug3("  read %d bytes", n);
		debug3("\"%s\"", buf);
		s->in_remaining -= n;
		if (s->in_remaining > 0)
			return SLURM_SUCCESS;
	}

	/*
	 * Route the message to the proper output
	 */
	{
		eio_obj_t *obj;
		struct file_write_info *info;

		s->in_msg->ref_count = 1;
		if (s->in_msg->header.type == SLURM_IO_STDOUT)
			obj = s->job->iostdout[s->in_msg->header.gtaskid];
		else
			obj = s->job->iostderr[s->in_msg->header.gtaskid];
		info = (struct file_write_info *) obj->arg;
		list_enqueue(info->msg_queue, s->in_msg);

		s->in_msg = NULL;
	}

	return SLURM_SUCCESS;
}

static bool 
_server_writable(eio_obj_t *obj)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;

	debug3("Called _server_writable");
	if (s->out_msg != NULL)
		debug3("  s->out_msg != NULL");
	if (!list_is_empty(s->msg_queue))
		debug3("  s->msg_queue queue length = %d",
		       list_count(s->msg_queue));

	if (obj->shutdown == true) {
		debug3("  false, shutdown");
		return false;
	}
	if (s->out_msg != NULL
	    || !list_is_empty(s->msg_queue))
		return true;

	debug3("  false");
	return false;
}

static int
_server_write(eio_obj_t *obj, List objs)
{
	struct server_io_info *s = (struct server_io_info *) obj->arg;
	void *buf;
	int n;

	debug2("Entering _server_write");

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
		debug3("  dequeue successful, s->out_msg->length = %d", s->out_msg->length);
		s->out_remaining = s->out_msg->length;
	}

	debug3("  s->out_remaining = %d", s->out_remaining); 
	
	/*
	 * Write message to socket.
	 */
	buf = s->out_msg->data + (s->out_msg->length - s->out_remaining);
again:
	if ((n = write(obj->fd, buf, s->out_remaining)) < 0) {
		if (errno == EINTR)
			goto again;
		/* FIXME handle error */
		return SLURM_ERROR;
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
		list_enqueue(s->job->free_io_buf, s->out_msg);
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

static void _write_label(int fd, int taskid)
{
	char buf[16];

	snprintf(buf, 16, "%0*d: ", fmt_width, taskid);
	/* FIXME - Need to handle return code */
	write(fd, buf, fmt_width+2);
}

static void _write_newline(int fd)
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
		/* FIXME handle error */
	}
}

/*
 * Blocks until write is complete, regardless of the file
 * descriptor being in non-blocking mode.
 */
static int _write_line(int fd, void *buf, int len)
{
	int n;
	int left = len;

	debug2("Called _write_line");
	while (left > 0) {
	again:
		if ((n = write(fd, buf, left)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_line");
				goto again;
			}
			/* FIXME handle error */
			return -1;
		}
		left -= n;
	}
	
	return len;
}

static int _write_msg(int fd, void *buf, int len, int taskid)
{
	void *start;
	void *end;
	int line_len;
	int rc;
	
	/* FIXME - should loop here, write as many lines as in the message */
	start = buf;
	end = memchr(start, '\n', len);
	if (opt.labelio)
		_write_label(fd, taskid);
	if (end == NULL) { /* no newline found */
		rc = _write_line(fd, start, len);
		_write_newline(fd);
	} else {
		line_len = (int)(end - start) + 1;
		rc = _write_line(fd, start, line_len);
	}

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
		if (info->out_msg->length == 0) /* eof */
			info->eof = true;
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
		list_enqueue(info->job->free_io_buf, info->out_msg);
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
		close(obj->fd);
		obj->fd = -1;
		info->eof = true;
		return false;
	}
	if (!list_is_empty(info->job->free_io_buf))
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
	msg = list_dequeue(info->job->free_io_buf);
	if (msg == NULL) {
		debug3("  List free_io_buf is empty, no file read");
		return SLURM_SUCCESS;
	}

	ptr = msg->data + io_hdr_packed_size();

again:
	if ((len = read(obj->fd, ptr, MAX_MSG_LEN)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				error("_file_read returned EAGAIN");
				goto again;
			}
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
		fatal("Not yet implemented");
#if 0
		int nodeid;
		struct server_io_info *server;
		msg->ref_count = 1;
		nodeid = info->job->taskid_to_nodeid[header.gtaskid];
		server = info->job->ioserver[nodeid]->arg;
		list_enqueue(server->msg_queue, msg);
#endif		
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

static FILE *
_fopen(char *filename)
{
	FILE *fp;

	xassert(filename != NULL);

	if (!(fp = fopen(filename, "w"))) 
		error ("Unable to open `%s' for writing: %m", filename);

	return fp;
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
	int i;
	pthread_attr_t attr;

	if (opt.labelio)
		fmt_width = _wid(opt.nprocs);

	for (i = 0; i < job->num_listen; i++) {
		eio_obj_t *obj;

		if (net_stream_listen(&job->listensock[i],
				      &job->listenport[i]) < 0)
			fatal("unable to initialize stdio listen socket: %m");
		debug("initialized stdio listening socket, port %d\n",
		      ntohs(job->listenport[i]));
		/*net_set_low_water(job->listensock[i], 140);*/
		obj = _create_listensock_eio(job->listensock[i], job);
		list_enqueue(job->eio_objs, obj);
	}

	/* FIXME - Need to open files here (or perhaps earlier) */

	xsignal(SIGTTIN, SIG_IGN);

	slurm_attr_init(&attr);
	if (errno = pthread_create(&job->ioid, &attr,
				   &_io_thr_internal, (void *) job))
		return SLURM_ERROR;

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
	job->ioserver[msg.nodeid] = _create_server_eio_obj(fd, job);
	list_enqueue(job->eio_objs, job->ioserver[msg.nodeid]);
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
	debug2("Activity on IO server socket %d", fd);

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

static ssize_t 
_readx(int fd, char *buf, size_t maxbytes)
{
	size_t n;

	if ((n = read(fd, (void *) buf, maxbytes)) < 0) {
		if (errno == EINTR)
			return -1;
		if ((errno == EAGAIN) || 
		    (errno == EWOULDBLOCK))
			return -1;
		error("readx fd %d: %m", fd, n);
		return -1; /* shutdown socket, cleanup. */
	}
	return n;
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

	if (!fail_list) {
		error("Invalid node list `%s' specified", nodelist);
		return SLURM_ERROR;
 	}

	while ( (node_name = hostlist_shift(fail_list)) ) {
		for (node_inx=0; node_inx<job->nhosts; node_inx++) {
			if (strcmp(node_name, job->host[node_inx]))
				continue;
			break;
		}
	}

	eio_signal_wakeup(job->eio);
	hostlist_destroy(fail_list);
	return SLURM_SUCCESS;
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
	buf->data = xmalloc(MAX_MSG_LEN + io_hdr_packed_size());
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
