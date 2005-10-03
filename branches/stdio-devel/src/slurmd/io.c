/*****************************************************************************\
 * src/slurmd/io.c - I/O handling routines for slurmd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/slurmd/shm.h"
#include "src/slurmd/io.h"
#include "src/slurmd/fname.h"
#include "src/slurmd/slurmd.h"

typedef enum slurmd_fd_type {
	TASK_STDERR_FD = 0,
	TASK_STDOUT_FD,
	TASK_STDIN_FD,
	CLIENT_SOCKET,
} slurmd_fd_type_t;

static char *_io_str[] = 
{
	"task stderr",
	"task stdout",
	"task stdin",
	"client socket",
};

enum error_type {
	E_NONE,
	E_WRITE,
	E_READ,
	E_POLL
};

struct error_state {
	enum error_type e_type;
	int             e_last;
	int             e_count;
	time_t          e_time;
};

struct incoming_client_info {
	struct slurm_io_header header;
	struct io_buf *msg;
	int32_t remaining;
	bool eof;
};

struct outgoing_fd_info {
	List msg_queue;
	struct io_buf *msg;
	int32_t remaining;
};

struct task_in_info {
#ifndef NDEBUG
#define TASK_IN_MAGIC  0x10103
	int              magic;
#endif
	slurmd_job_t    *job;		 /* pointer back to job data   */

	struct outgoing_fd_info out;
};

struct task_out_info {
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

struct client_io_info {
#ifndef NDEBUG
#define CLIENT_IO_MAGIC  0x10102
	int                   magic;
#endif
	slurmd_job_t    *job;		 /* pointer back to job data   */

	struct incoming_client_info in;
	struct outgoing_fd_info out;
};

struct io_info {
	/* FIXME! Obsolete struct */
};

static void   _fatal_cleanup(void *);
static int    find_obj(void *obj, void *key);
/* static int    find_fd(void *obj, void *key); */
static int    _task_init_pipes(slurmd_task_info_t *t);
static int    _create_task_eio_objs(slurmd_job_t *);
static void * _io_thr(void *);
static int    _send_io_init_msg(int sock, srun_key_t *key, int nodeid);
static void   _io_connect_objs(io_obj_t *, io_obj_t *);
static int    _init_pipes(slurmd_job_t *job);
static int    _shutdown_task_obj(struct io_info *t);
static void   _send_eof_msg(struct task_out_info *out);
static struct io_buf *_task_build_message(struct task_out_info *out,
					  slurmd_job_t *job, cbuf_t cbuf);

static eio_obj_t *_eio_obj_create(int fd, void *arg, struct io_operations *ops);
static struct io_obj  * _io_obj_create(int fd, void *arg);
static struct io_info * _io_info_create(uint32_t id);
static struct io_obj  * _io_obj(slurmd_job_t *, slurmd_task_info_t *, int, int);
static void           * _io_thr(void *arg);

static void   _clear_error_state(struct io_info *io);
static int    _update_error_state(struct io_info *, enum error_type, int);

static struct io_operations * _ops_copy(struct io_operations *ops);

/* Slurmd I/O objects:
 * N   task   stderr, stdout objs (read-only)
 * N*M client stderr, stdout objs (read-write) (possibly a file)
 * N   task   stdin          objs (write only) (possibly a file)
 */

static bool _task_readable(io_obj_t *);
static bool _task_writable(io_obj_t *);
static int  _task_read(io_obj_t *, List);
static int  _task_write(io_obj_t *, List);
static int  _task_error(io_obj_t *, List);
static bool _client_readable(io_obj_t *);
static bool _client_writable(io_obj_t *);
static int  _client_read(io_obj_t *, List);
static int  _client_write(io_obj_t *, List);
static int  _client_error(io_obj_t *, List);
static int  _obj_close(io_obj_t *, List);

/* Task Output operations (TASK_STDOUT, TASK_STDERR)
 * These objects are never writable --
 * therefore no need for writeable and handle_write methods
 */
struct io_operations task_out_ops = {
        readable:	&_task_readable,
	handle_read:	&_task_read,
        handle_error:	&_task_error,
	handle_close:   &_obj_close
};

/* Task Input operations (TASK_STDIN)
 * input objects are never readable
 */
struct io_operations task_in_ops = {
	writable:	&_task_writable,
	handle_write:	&_task_write,
	handle_error:	&_task_error,
	handle_close:   &_obj_close
};

/* Normal client operations (CLIENT_STDOUT, CLIENT_STDERR, CLIENT_STDIN)
 * these methods apply to clients which are considered
 * "connected" i.e. in the case of srun, they've read
 * the so-called IO-header data
 */
struct io_operations client_ops = {
        readable:	&_client_readable,
	writable:	&_client_writable,
	handle_read:	&_client_read,
	handle_write:	&_client_write,
	handle_error:	&_client_error,
	handle_close:   &_obj_close
};

int
io_thread_start(slurmd_job_t *job) 
{
	pthread_attr_t attr;

	if (_init_pipes(job) == SLURM_FAILURE) {
		error("io_handler: init_pipes failed: %m");
		return SLURM_FAILURE;
	}

	/* create task event IO objects and append these to the objs list */
	if (_create_task_eio_objs(job) < 0)
		return SLURM_FAILURE;

	slurm_attr_init(&attr);

	if (pthread_create(&job->ioid, &attr, &_io_thr, (void *)job) != 0)
		fatal("pthread_create: %m");
	
	fatal_add_cleanup(&_fatal_cleanup, (void *) job);

	return 0;
}

static int
_xclose(int fd)
{
	int rc;
	do {
		rc = close(fd);
	} while (rc == -1 && errno == EINTR);
	return rc;
}

static void
_route_msg_task_to_client(eio_obj_t *obj)
{
	struct task_out_info *out = (struct task_out_info *)obj->arg;
	struct client_io_info *client;
	struct io_buf *msg = NULL;
	eio_obj_t *eio;
	ListIterator clients;

	/* Pack task output into messages for transfer to a client */
	while (cbuf_used(out->buf) > 0
	       && !list_is_empty(out->job->free_io_buf)) {
		debug3("cbuf_used = %d", cbuf_used(out->buf));
		msg = _task_build_message(out, out->job, out->buf);
		if (msg == NULL)
			return;

		debug3("\"%s\"", msg->data + io_hdr_packed_size());

		/* Add message to the msg_queue of all clients */
		clients = list_iterator_create(out->job->clients);
		while(eio = list_next(clients)) {
			client = (struct client_io_info *)eio->arg;
			debug3("======================== Enqueued message");
			xassert(client->magic == CLIENT_IO_MAGIC);
			if (list_enqueue(client->out.msg_queue, msg))
				msg->ref_count++;
		}
		list_iterator_destroy(clients);
	}
}

static void
_free_msg(struct io_buf *msg, slurmd_job_t *job)
{
	int i;

	msg->ref_count--;
	if (msg->ref_count == 0) {
		/* Put the message back on the free List */
		list_enqueue(job->free_io_buf, msg);

		/* Try packing messages from tasks' output cbufs */
		for (i = 0; i < job->ntasks; i++) {
			_route_msg_task_to_client(job->task[i]->err);
			if (list_is_empty(job->free_io_buf))
				break;
			_route_msg_task_to_client(job->task[i]->out);
			if (list_is_empty(job->free_io_buf))
				break;
		}

		/* Kick the event IO engine */
		eio_handle_signal_wake(job->eio);
	}
}

extern void
io_close_task_fds(slurmd_job_t *job)
{
	int i;

	for (i = 0; i < job->ntasks; i++) {
		close(job->task[i]->stdin);
		close(job->task[i]->stdout);
		close(job->task[i]->stderr);
	}
}

void 
io_close_all(slurmd_job_t *job)
{
	int i;

#if 0
	for (i = 0; i < job->ntasks; i++)
		_io_finalize(job->task[i]);
#endif

	/* No more debug info will be received by client after this point
	 */
	debug("Closing debug channel");
	close(STDERR_FILENO);

	/* Signal IO thread to close appropriate 
	 * client connections
	 */
	eio_handle_signal_wake(job->eio);
}

static void
_fatal_cleanup(void *arg)
{
	slurmd_job_t   *job = (slurmd_job_t *) arg;
	ListIterator    i;
	io_obj_t       *obj;
	struct io_info *io;

	error("in fatal_cleanup");

#if 0
	_task_read(job->task[0]->err, job->objs);

	i = list_iterator_create(job->objs);
	while((obj = list_next(i))) {
		io = (struct io_info *) obj->arg;
		if (obj->ops->writable && (*obj->ops->writable)(obj))
			_write(obj, job->objs);
	}
	list_iterator_destroy(i);
#endif
}

static void *
_io_thr(void *arg)
{
	slurmd_job_t *job = (slurmd_job_t *) arg;
	sigset_t set;

	/* A SIGHUP signal signals a reattach to the mgr thread.  We need
	 * to block SIGHUP from being delivered to this thread so the mgr
	 * thread will see the signal.
	 *
	 * FIXME!  It is conceivable that a SIGHUP could be delivered to
	 * this thread before we get a chance to block it.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	debug("IO handler started pid=%lu", (unsigned long) getpid());
	io_handle_events(job->eio);
	debug("IO handler exited");
	return (void *)1;
}

/*
 * Create an eio_obj_t for handling a task's stdin traffic
 */
static eio_obj_t *
_create_task_in_eio(int fd, slurmd_job_t *job)
{
	struct task_in_info *in = NULL;
	eio_obj_t *eio = NULL;

	in = (struct task_in_info *)xmalloc(sizeof(struct task_in_info));
#ifndef NDEBUG
	in->magic = TASK_IN_MAGIC;
#endif
	in->job = job;
	in->out.msg_queue = list_create(NULL); /* FIXME! Add destructor */
	in->out.msg = NULL;
	in->out.remaining = 0;

	eio = (eio_obj_t *)xmalloc(sizeof(eio_obj_t));
	eio->fd = fd;
	eio->arg = (void *)in;
	eio->ops = _ops_copy(&task_in_ops);
	eio->shutdown = false;

	return eio;
}

/*
 * Create an eio_obj_t for handling a task's stdout or stderr traffic
 */
static eio_obj_t *
_create_task_out_eio(int fd, uint16_t type,
		     slurmd_job_t *job, slurmd_task_info_t *task)
{
	struct task_out_info *out = NULL;
	eio_obj_t *eio = NULL;

	out = (struct task_out_info *)xmalloc(sizeof(struct task_out_info));
#ifndef NDEBUG
	out->magic = TASK_OUT_MAGIC;
#endif
	out->type = type;
	out->gtaskid = task->gtid;
	out->ltaskid = task->id;
	out->job = job;
	out->buf = cbuf_create(MAX_MSG_LEN, MAX_MSG_LEN*16);
	out->eof = false;
	out->eof_msg_sent = false;
	if (cbuf_opt_set(out->buf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) == -1)
		error("setting cbuf options");

	eio = (eio_obj_t *)xmalloc(sizeof(eio_obj_t));
	eio->fd = fd;
	eio->arg = (void *)out;
	eio->ops = _ops_copy(&task_out_ops);
	eio->shutdown = false;
	return eio;
}

static int
_create_task_eio_objs(slurmd_job_t *job)
{
	int          i;
	slurmd_task_info_t *t;
	eio_obj_t    *obj;

	for (i = 0; i < job->ntasks; i++) {
		t = job->task[i];

		t->in = _create_task_in_eio(t->to_stdin, job);
		list_append(job->objs, (void *)t->in );

		t->out = _create_task_out_eio(t->from_stdout,
					      SLURM_IO_STDOUT, job, t);
		list_append(job->objs, (void *)t->out);

		t->err = _create_task_out_eio(t->from_stderr,
					      SLURM_IO_STDERR, job, t);
		list_append(job->objs, (void *)t->err);
	}

	return SLURM_SUCCESS;
}

/*
 * Turn off obj's readable() function such that it is never
 * checked for readability
 */
static inline void
_obj_set_unreadable(io_obj_t *obj)
{
	obj->ops->readable = NULL;
}

static inline void
_obj_set_unwritable(io_obj_t *obj)
{
	obj->ops->writable = NULL;
}

static char * 
_local_filename (char *fname, int taskid)
{
	int id;

	if (fname == NULL)
		return (NULL);

	if ((id = fname_single_task_io (fname)) < 0) 
		return (fname);

	if (id != taskid)
		return ("/dev/null");

	return (NULL);
}


/* 
 * create initial client obj for this job step
 */
int
io_client_connect(slurmd_job_t *job)
{
	int i;
	srun_info_t *srun;
	int sock = -1;
	struct client_io_info *client;
	eio_obj_t *obj;

	debug2 ("adding IO connection (logical node rank %d)", job->nodeid);

	srun = list_peek(job->sruns);
	xassert(srun != NULL);

	if (srun->ioaddr.sin_addr.s_addr) {
		char         host[256];
		uint16_t     port;
		slurmd_get_addr(&srun->ioaddr, &port, host, sizeof(host));
		debug2("connecting IO back to %s:%d", host, ntohs(port));
	} 

	if ((sock = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail? 
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}

	fd_set_blocking(sock);  /* just in case... */

	_send_io_init_msg(sock, srun->key, job->nodeid);

	debug3("  back from _send_io_init_msg");
	fd_set_nonblocking(sock);
	fd_set_close_on_exec(sock);

	/* Now set up the eio object */
	client = xmalloc(sizeof(struct client_io_info));
#ifndef NDEBUG
	client->magic = CLIENT_IO_MAGIC;
#endif
	client->job = job;
	client->out.msg_queue = list_create(NULL); /* FIXME! Need desctructor */

	obj = _eio_obj_create(sock, (void *)client, _ops_copy(&client_ops));
	list_append(job->clients, (void *)obj);
	list_append(job->objs, (void *)obj);

	debug3("Now handling %d IO Client object(s)", list_count(job->clients));

	/* kick IO thread */
	eio_handle_signal_wake(job->eio);
	debug3("  test 3");

	return SLURM_SUCCESS;
}

int
io_new_clients(slurmd_job_t *job)
{
	return SLURM_ERROR;
#if 0
	return io_prepare_clients(job);
#endif
}

static int 
find_obj(void *obj, void *key)
{
	xassert(obj != NULL);
	xassert(key != NULL);

	return (obj == key);
}

static struct io_operations *
_ops_copy(struct io_operations *ops)
{
	struct io_operations *ret = xmalloc(sizeof(*ops));
	/* 
	 * Copy initial client_ops 
	 */
	*ret = *ops;
	return ret;
}


static io_obj_t *
_io_obj_create(int fd, void *arg)
{
	io_obj_t *obj = xmalloc(sizeof(*obj));
	obj->fd  = fd;
	obj->arg = arg;
	obj->ops = NULL;
	obj->shutdown = false;
	return obj;
}

static eio_obj_t *
_eio_obj_create(int fd, void *arg, struct io_operations *ops)
{
	eio_obj_t *obj = xmalloc(sizeof(*obj));
	obj->fd  = fd;
	obj->arg = arg;
	obj->ops = ops;
	obj->shutdown = false;
	return obj;
}

static int
_task_init_pipes(slurmd_task_info_t *t)
{
	int pin[2];
	int pout[2];
	int perr[2];

	if (  (pipe(pin)  < 0) 
	   || (pipe(pout) < 0) 
	   || (pipe(perr) < 0) ) {
		error("io_init_pipes: pipe: %m");
		return SLURM_FAILURE;
	}

	t->stdin = pin[0];
	t->to_stdin = pin[1];
	t->stdout = pout[1];
	t->from_stdout = pout[0];
	t->stderr = perr[1];
	t->from_stderr = perr[0];

	fd_set_close_on_exec(t->to_stdin);
	fd_set_close_on_exec(t->from_stdout);
	fd_set_close_on_exec(t->from_stderr);

	fd_set_nonblocking(t->to_stdin);
	fd_set_nonblocking(t->from_stdout);
	fd_set_nonblocking(t->from_stderr);

	return SLURM_SUCCESS;
}

static int
_init_pipes(slurmd_job_t *job)
{
	int i;
	for (i = 0; i < job->ntasks; i++) {
		if (_task_init_pipes(job->task[i]) == SLURM_FAILURE) {
			error("init_pipes <task %d> failed", i);
			return SLURM_FAILURE;
		}
	}
	return SLURM_SUCCESS;
}

static int
_send_io_init_msg(int sock, srun_key_t *key, int nodeid)
{
	struct slurm_io_init_msg msg;

	memcpy(msg.cred_signature, key->data, SLURM_CRED_SIGLEN);
	msg.nodeid = nodeid;

	error("msg.nodeid = %u", msg.nodeid);
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
	if (dup2(t->stdin, STDIN_FILENO  ) < 0) {
		error("dup2(stdin): %m");
		return SLURM_FAILURE;
	}

	if (dup2(t->stdout, STDOUT_FILENO) < 0) {
		error("dup2(stdout): %m");
		return SLURM_FAILURE;
	}

	if (dup2(t->stderr, STDERR_FILENO) < 0) {
		error("dup2(stderr): %m");
		return SLURM_FAILURE;
	}

	/* ignore errors on close */
	close(t->to_stdin );
	close(t->from_stdout);
	close(t->from_stderr);
	return SLURM_SUCCESS;
}

static int
_obj_close(io_obj_t *obj, List objs)
{

	fatal("_obj_close");
#if 0
	struct io_info *io = (struct io_info *) obj->arg;

	debug3("Need to close %d %s", io->id, _io_str[io->type]);

	if (close(obj->fd) < 0)
		error("close: %m");
	obj->fd = -1;

	if (_isa_client(io)) 
		_io_disconnect_client(io, objs);
	else 
		_shutdown_task_obj(io);

	return SLURM_SUCCESS;
#endif
}

static bool 
_client_readable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	debug3("Called _client_readable");
	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->in.eof) {
		debug3("  false");
		return false;
	}

	if (obj->shutdown) {
		debug3("  false, shutdown");
		shutdown(obj->fd, SHUT_RD);
		client->in.eof = true;
	}

	if (client->in.msg != NULL
	    || !list_is_empty(client->job->free_io_buf))
		return true;

	debug3("  false");
	return false;
}

static bool 
_client_writable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	debug3("Called _client_writable");
	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->out.msg != NULL)
		debug3("  client->out.msg != NULL");

	if (!list_is_empty(client->out.msg_queue))
		debug3("  client->out.msg_queue queue length = %d",
		       list_count(client->out.msg_queue));

	if (client->out.msg != NULL
	    || !list_is_empty(client->out.msg_queue))
		return true;

	debug3("  false");
	return false;
}

static bool 
_task_readable(eio_obj_t *obj)
{
	struct task_out_info *out = (struct task_out_info *)obj->arg;

	debug3("Called _task_readable, task %d, %s", out->gtaskid,
	       out->type == SLURM_IO_STDOUT ? "STDOUT" : "STDERR");

	if (out->eof_msg_sent) {
		debug3("  false, eof message sent");
		return false;
	}
	if (cbuf_free(out->buf) > 0) {
		debug3("  cbuf_free = %d", cbuf_free(out->buf));
		return true;
	}

	debug3("  false");
	return false;
}

static bool 
_task_writable(eio_obj_t *obj)
{
	struct task_in_info *in = (struct task_in_info *) obj->arg;
	struct outgoing_fd_info *out = &in->out;

	debug3("Called _task_writable");

	if (out->msg != NULL || list_count(out->msg_queue) > 0)
		return true;

	debug3("  false (list_count = %d)", list_count(out->msg_queue));
	return false;
}

static int
_task_write(io_obj_t *obj, List objs)
{
	struct task_in_info *in = (struct task_in_info *) obj->arg;
	struct outgoing_fd_info *out;
	void *buf;
	int n;

	xassert(in->magic == TASK_IN_MAGIC);

	out = &in->out;

	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (out->msg == NULL) {
		out->msg = list_dequeue(out->msg_queue);
		if (out->msg == NULL) {
			debug3("_task_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		if (out->msg->length == 0) { /* eof message */
			close(obj->fd);
			obj->fd = -1;
			_free_msg(out->msg, in->job);
			out->msg = NULL;
			return SLURM_SUCCESS;
		}
		out->remaining = out->msg->length;
	}

	/*
	 * Write message to socket.
	 */
	buf = out->msg->data + (out->msg->length - out->remaining);
again:
	if ((n = write(obj->fd, buf, out->remaining)) < 0) {
		if (errno == EINTR)
			goto again;
		/* FIXME handle error */
		return SLURM_ERROR;
	}
	out->remaining -= n;
	if (out->remaining > 0)
		return SLURM_SUCCESS;

	_free_msg(out->msg, in->job);
	out->msg = NULL;

	return SLURM_SUCCESS;

}


/*
 * Write outgoing packed messages to the client socket.
 */
static int
_client_write(io_obj_t *obj, List objs)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;
	struct outgoing_fd_info *out;
	void *buf;
	int n;

	xassert(client->magic == CLIENT_IO_MAGIC);

	debug2("Entering _client_write");
	out = &client->out;

	/*
	 * If we aren't already in the middle of sending a message, get the
	 * next message from the queue.
	 */
	if (out->msg == NULL) {
		out->msg = list_dequeue(out->msg_queue);
		if (out->msg == NULL) {
			debug3("_client_write: nothing in the queue");
			return SLURM_SUCCESS;
		}
		debug3("  dequeue successful, out->msg->length = %d", out->msg->length);
		out->remaining = out->msg->length;
	}

	debug3("  out->remaining = %d", out->remaining); 

	/*
	 * Write message to socket.
	 */
	buf = out->msg->data + (out->msg->length - out->remaining);
again:
	if ((n = write(obj->fd, buf, out->remaining)) < 0) {
		if (errno == EINTR)
			goto again;
		/* FIXME handle error */
		return SLURM_ERROR;
	}
	debug3("Wrote %d bytes to socket", n);
	out->remaining -= n;
	if (out->remaining > 0)
		return SLURM_SUCCESS;

	_free_msg(out->msg, client->job);
	out->msg = NULL;

	return SLURM_SUCCESS;
}

static void
_send_eof_msg(struct task_out_info *out)
{
	struct client_io_info *client;
	struct io_buf *msg = NULL;
	eio_obj_t *eio;
	ListIterator clients;
	struct slurm_io_header header;
	Buf packbuf;

	debug2("Entering _send_eof_msg");
	msg = list_dequeue(out->job->free_io_buf);
	if (msg == NULL) {
		debug3("  free msg list empty, unable to send eof_msg");
		return;
	}

	header.type = out->type;
	header.ltaskid = out->ltaskid;
	header.gtaskid = out->gtaskid;
	header.length = 0; /* eof */

	packbuf = create_buf(msg->data, io_hdr_packed_size());
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* Add eof message to the msg_queue of all clients */
	clients = list_iterator_create(out->job->clients);
	while(eio = list_next(clients)) {
		client = (struct client_io_info *)eio->arg;
		debug3("======================== Enqueued message");
		xassert(client->magic == CLIENT_IO_MAGIC);
		if (list_enqueue(client->out.msg_queue, msg))
			msg->ref_count++;
	}
	list_iterator_destroy(clients);

	out->eof_msg_sent = true;
	debug2("Leaving  _send_eof_msg");
}


static struct io_buf *
_task_build_message(struct task_out_info *out, slurmd_job_t *job, cbuf_t cbuf)
{
	struct io_buf *msg;
	char *ptr;
	Buf packbuf;
	bool must_truncate = false;
	int avail;
	struct slurm_io_header header;
	int n;

	debug2("Entering _task_build_message");
	msg = list_dequeue(job->free_io_buf);
	if (msg == NULL)
		return NULL;
	ptr = msg->data + io_hdr_packed_size();
	avail = cbuf_peek_line(cbuf, ptr, MAX_MSG_LEN, 1);
	if (avail >= MAX_MSG_LEN)
		must_truncate = true;

	if (must_truncate) {
		n = cbuf_read(cbuf, ptr, MAX_MSG_LEN);
	} else {
		n = cbuf_read_line(cbuf, ptr, MAX_MSG_LEN, -1);
		if (n == 0) {
			debug3("  partial line in buffer, ignoring");
			debug2("Leaving  _task_build_message");
			list_enqueue(job->free_io_buf, msg);
			return NULL;
		}
	}

	header.type = out->type;
	header.ltaskid = out->ltaskid;
	header.gtaskid = out->gtaskid;
	header.length = n;

	debug3("  header.length = %d", n);
	packbuf = create_buf(msg->data, io_hdr_packed_size());
	io_hdr_pack(&header, packbuf);
	msg->length = io_hdr_packed_size() + header.length;
	msg->ref_count = 0; /* make certain it is initialized */

	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);
	      
	debug2("Leaving  _task_build_message");
	return msg;
}

/*
 * Read output (stdout or stderr) from a task into a cbuf.  The cbuf
 * allows whole lines to be packed into messages if line buffering
 * is requested.
 */
static int
_task_read(eio_obj_t *obj, List objs)
{
	struct task_out_info *out = (struct task_out_info *)obj->arg;
	struct client_io_info *client;
	struct io_buf *msg = NULL;
	eio_obj_t *eio;
	ListIterator clients;
	int len;
	int rc = -1;

	xassert(out->magic == TASK_OUT_MAGIC);

	debug2("Entering _task_read");
	len = cbuf_free(out->buf);
	if (len > 0) {
again:
		if ((rc = cbuf_write_from_fd(out->buf, obj->fd, len, NULL))
		    < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				error("_task_read returned EAGAIN");
				return SLURM_SUCCESS;
			}
			/* FIXME add error message */
			debug3("  error in _task_read");
			return SLURM_ERROR;
		}
		if (rc == 0) {  /* got eof */
			debug3("  got eof on task");
			out->eof = true;
		}
	}

	debug3("************************ %d bytes read from task %s", rc,
	       out->type == SLURM_IO_STDOUT ? "STDOUT" : "STDERR");


	/*
	 * Put the message in client outgoing queues
	 */
	_route_msg_task_to_client(obj);

	/*
	 * Send the eof message
	 */
	if (cbuf_used(out->buf) == 0 && out->eof) {
		_send_eof_msg(out);
	}

	return SLURM_SUCCESS;
}

static int 
_task_error(io_obj_t *obj, List objs)
{
	debug3("eio detected _task_error");
#if 0
	if (getsockopt(obj->fd, SOL_SOCKET, SO_ERROR, &err, &size) < 0)
		error ("_task_error getsockopt: %m");
	else
		_update_error_state(t, E_POLL, err);
	_obj_close(obj, objs);
#endif
	return -1;
}


/*
 * Read from a client socket.
 *
 * 1) Read message header, if not already read in a previous call to
 *    _client_read.  Function will not return until entire header has
 *    been read.
 * 2) Read message body in non-blocking fashion.
 * 3) Enqueue message in task stdin List.
 */
static int 
_client_read(eio_obj_t *obj, List objs)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;
	struct incoming_client_info *in;
	void *buf;
	int n;

	debug2("Entering _client_read");
	xassert(client->magic == CLIENT_IO_MAGIC);

	in = &client->in;
	/*
	 * Read the header, if a message read is not already in progress
	 */
	if (in->msg == NULL) {
		in->msg = list_dequeue(client->job->free_io_buf);
		if (in->msg == NULL) {
			debug3("  _client_read free_io_buf is empty");
			return SLURM_SUCCESS;
		}
		n = io_hdr_read_fd(obj->fd, &in->header);
		if (n == 0) { /* got eof on socket read */
			debug3("  got eof on _client_read header");
			in->eof = true;
			list_enqueue(client->job->free_io_buf, in->msg);
			in->msg = NULL;
			return SLURM_SUCCESS;
		}
		in->remaining = in->header.length;
		in->msg->length = in->header.length;
	}

	/*
	 * Read the body
	 */
	if (in->header.length == 0) { /* zero length is an eof message */
		debug3("  got stdin eof message!");
	} else {
		buf = in->msg->data + (in->msg->length - in->remaining);
	again:
		if ((n = read(obj->fd, buf, in->remaining)) < 0) {
			if (errno == EINTR)
				goto again;
			/* FIXME handle error */
			return SLURM_ERROR;
		}
		if (n == 0) { /* got eof */
			debug3("  got eof on _client_read body");
			in->eof = true;
			list_enqueue(client->job->free_io_buf, in->msg);
			in->msg = NULL;
			return SLURM_SUCCESS;
		}
		debug3("  read %d bytes", n);
		debug3("\"%s\"", buf);
		in->remaining -= n;
		if (in->remaining > 0)
			return SLURM_SUCCESS;
	}

	/*
	 * Route the message to its destination(s)
	 */
	if (in->header.type != SLURM_IO_STDIN
	    && in->header.type != SLURM_IO_ALLSTDIN) {
		error("Input in->header.type is not valid!");
		in->msg = NULL;
		return SLURM_ERROR;
	} else {
		int i;
		slurmd_task_info_t *task;
		struct task_in_info *io;

		in->msg->ref_count = 0;
		if (in->header.type == SLURM_IO_ALLSTDIN) {
			for (i = 0; i < client->job->ntasks; i++) {
				task = client->job->task[i];
				io = (struct task_in_info *)(task->in->arg);
				list_enqueue(io->out.msg_queue, in->msg);
				in->msg->ref_count++;
			}
		} else {
			for (i = 0; i < client->job->ntasks; i++) {
				task = client->job->task[i];
				io = (struct task_in_info *)task->in->arg;
				if (task->gtid != in->header.gtaskid)
					continue;
				list_enqueue(io->out.msg_queue, in->msg);
				in->msg->ref_count++;
				break;
			}
		}
	}
	client->in.msg = NULL;
	return SLURM_SUCCESS;
}

static int 
_client_error(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;
	socklen_t size = sizeof(int);
	int err = 0;

	fatal("_client_error");

#if 0
	xassert(io->magic == IO_MAGIC);

	if (getsockopt(obj->fd, SOL_SOCKET, SO_ERROR, &err, &size) < 0)
		error ("_client_error getsockopt: %m");
	else if (err != ECONNRESET) /* Do not log connection resets */
		_update_error_state(io, E_POLL, err);
#endif

	return 0;
}

static char *
err_string(enum error_type type)
{
	switch (type) {
	case E_NONE:
		return "";
	case E_WRITE:
		return "write failed";
	case E_READ:
		return "read failed";
	case E_POLL:
		return "poll error";
	}

	return "";
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
