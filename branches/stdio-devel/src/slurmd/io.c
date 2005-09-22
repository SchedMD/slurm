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

#define MAX_MSG_LEN 1024

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
};

struct incoming_client_info {
	struct slurm_io_header header;
	struct io_buf *msg;
	int32_t remaining;
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
	int              type;           /* type of IO object          */
	slurmd_task_info_t     *task;    /* pointer back to task data  */
	slurmd_job_t    *job;		 /* pointer back to job data   */

	cbuf_t           buf;
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
static int    _io_init_pipes(slurmd_task_info_t *t);
static int    _io_prepare_tasks(slurmd_job_t *);
static void * _io_thr(void *);
static int    _send_io_init_msg(int sock, srun_key_t *key, int nodeid);
static void   _io_client_attach(io_obj_t *, io_obj_t *, io_obj_t *, 
		                List objList);
static void   _io_connect_objs(io_obj_t *, io_obj_t *);
static int    _shutdown_task_obj(struct io_info *t);
static int    _open_output_file(slurmd_job_t *job, slurmd_task_info_t *t, 
		                char *fname, slurmd_fd_type_t type);
static int    _open_stdin_file(slurmd_job_t *job, slurmd_task_info_t *t, 
		               srun_info_t *srun);

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
io_start_thread(slurmd_job_t *job) 
{
	pthread_attr_t attr;

	if (io_init_pipes(job) == SLURM_FAILURE) {
		error("io_handler: init_pipes failed: %m");
		return SLURM_FAILURE;
	}

	/* create task IO objects and append these to the objs list
	 */
	if (_io_prepare_tasks(job) < 0)
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


#if 0
/* 
 * Close child fds in parent as well as
 * any stdin io objs in job->objs
 * 
 */
static void
_io_finalize(slurmd_task_info_t *t)
{
	struct io_info *in  = t->in->arg;

	if (_xclose(t->stdin ) < 0)
		error("close(stdin) : %m");
	if (_xclose(t->stdout) < 0)
		error("close(stdout): %m");
	if (_xclose(t->stderr) < 0)
		error("close(stderr): %m");

	in->disconnected  = 1;

	/* Need to close all stdin writers
	 *
	 * We effectively close these writers by
	 * forcing them to be unreadable. This will
	 * prevent the IO thread from hanging waiting
	 * for stdin data. (While also not forcing the
	 * close of a pipe that is also writable)
	 */

	 
	if (in->writers) {
		ListIterator i;
		struct io_info *io;

		i = list_iterator_create(in->writers);
		while ((io = list_next(i))) {
			if (io->obj->fd > 0) {
				io->obj->ops->readable = NULL;
			}
		}
		list_iterator_destroy(i);
	}
}
#endif

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
	eio_handle_signal(job->eio);
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

static void
_handle_unprocessed_output(slurmd_job_t *job)
{
#if 0
	int i;
	slurmd_task_info_t    *t;
	struct io_info *io;
	List            readers;
	size_t          n = 0;
	/* XXX Do something with unwritten IO */
	for (i = 0; i < job->ntasks; i++) {
		if (!(t = job->task[i]))
			continue;
		if (!(readers = ((struct io_info *)t->out->arg)->readers))
			continue;
		if (!(io = list_peek(readers)))
			continue;

		if (io->buf && (n = cbuf_used(io->buf)))
			error("task %d: %ld bytes of stdout unprocessed", 
			      io->id, (long) n);

		if (!(readers = ((struct io_info *)t->err->arg)->readers))
			continue;
		if (!(io = list_peek(readers)))
			continue;

		if (io->buf && (n = cbuf_used(io->buf)))
			error("task %d: %ld bytes of stderr unprocessed", 
			      io->id, (long) n);
	}
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
	io_handle_events(job->eio, job->objs);
	debug("IO handler exited");
	_handle_unprocessed_output(job);
	return (void *)1;
}

static int
_io_prepare_tasks(slurmd_job_t *job)
{
	int          i;
	slurmd_task_info_t *t;
	io_obj_t    *obj;

	for (i = 0; i < job->ntasks; i++) {
		t = job->task[i];

#if 0
		t->in  = _io_obj(job, t, t->to_stdin,  TASK_STDIN );
		list_append(job->objs, (void *)t->in );

		t->out = _io_obj(job, t, t->from_stdout, TASK_STDOUT);
		list_append(job->objs, (void *)t->out);

		/* "ghost" stdout client buffers task data without sending 
		 * it anywhere
		 */
		obj    = _io_obj(job, t, -1,         CLIENT_STDOUT);
		_io_client_attach(obj, t->out, NULL, job->objs);

		t->err = _io_obj(job, t, t->from_stderr, TASK_STDERR);
		list_append(job->objs, (void *)t->err);

		/* "fake" stderr client buffers task data without sending 
		 * it anywhere
		 */
		obj    = _io_obj(job, t, -1,         CLIENT_STDERR);
		_io_client_attach(obj, t->err, NULL, job->objs);
#endif
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

	debug3("Now handling %d IO Client objects", list_count(job->clients));


	/* kick IO thread */
	eio_handle_signal(job->eio);

	return SLURM_SUCCESS;
}

int
io_new_clients(slurmd_job_t *job)
{
	return SLURM_SUCCESS;
#if 0
	return io_prepare_clients(job);
#endif
}

static int
_open_task_file(char *filename, int flags)
{
	int fd;
	if (filename == NULL)
		return -1;
	if ((fd = open(filename, flags, 0644))< 0) {
		error( "Unable to open `%s': %s", 
                       filename, slurm_strerror(errno) );
		return -1;
	}
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);
	return fd;
}

static int
_open_output_file(slurmd_job_t *job, slurmd_task_info_t *t, char *fmt, 
		  slurmd_fd_type_t type)
{
	int          fd     = -1;
	io_obj_t    *obj    = NULL;
	int          flags  = O_APPEND|O_WRONLY;
	char        *fname  = NULL;

	if (fmt == NULL)
		return SLURM_ERROR;

	if (!_local_filename (fmt, t->gtid))
		return SLURM_ERROR;

	fname = fname_create(job, fmt, t->gtid);
	if ((fd = _open_task_file(fname, flags)) > 0) {
		debug2 ("opened `%s' for task %d %s fd %d", 
		         fname, t->gtid, _io_str[type], fd);
		obj  = _io_obj(job, t, fd, type);
		_obj_set_unreadable(obj);
		xassert(obj->ops->writable != NULL);
		if (type == 0)
			_io_client_attach(obj, t->out, NULL, job->objs);
		else
			_io_client_attach(obj, t->err, NULL, job->objs);
	} 
	xfree(fname);

	return fd;
}

static int
_open_stdin_file(slurmd_job_t *job, slurmd_task_info_t *t, srun_info_t *srun)
{
	int       fd    = -1;
	io_obj_t *obj   = NULL;
	int       flags = O_RDONLY;
	char     *fname = fname_create(job, srun->ifname, t->gtid);

        if (!strcmp(fname, "/dev/null")) {
		/* AIX returns POLLERR when a file descriptor for /dev/null is
		 * polled, so we bypass the normal eio handling of stdin, and
		 * instead connect the task's stdin directly to /dev/null.
		 *
		 * Without eio the stdin pipe is no longer useful so we close
		 * both ends.  We reuse t->stdin to pass the file descriptor for
		 * /dev/null to io_prepare_child.
		 */
		close(t->stdin);
		close(t->to_stdin);
		/* io_prepare_child will do close(t->to_stdin), so set it to a
		 * number unlikely to conflict with new file descriptors.
		 */
		t->to_stdin = -1;
		if ((fd = open("/dev/null", flags)) < 0) {
			error("Unable to open /dev/null: %s",
			      slurm_strerror(errno));
			return -1;
		}
		debug("opened /dev/null for direct stdin use fd %d", fd);
		t->stdin = fd;
        } else if ((fd = _open_task_file(fname, flags)) > 0) {
		debug("opened `%s' for %s fd %d", fname, "stdin", fd);
		obj = _io_obj(job, t, fd, 0); 
		_io_client_attach(obj, NULL, t->in, job->objs);
	}
	xfree(fname);
	return fd;
}


/* Attach io obj "client" as a reader of 'writer' and a writer to 'reader'
 * if 'reader' is NULL client will have no readers.
 *
 */
static void
_io_client_attach(io_obj_t *client, io_obj_t *writer, 
		  io_obj_t *reader, List objList)
{
#if 0
	struct io_info *src = writer ? writer->arg : NULL;
	struct io_info *dst = reader ? reader->arg : NULL; 
	struct io_info *cli = client->arg;
	struct io_info *io;
	struct io_operations *opsptr = NULL;

	xassert((src != NULL) || (dst != NULL));
	xassert((src == NULL) || (src->magic == IO_MAGIC));
	xassert((dst == NULL) || (dst->magic == IO_MAGIC));

	if (writer == NULL) { 
		/* 
		 * Only connect new client to reader if the
		 * reader is still available.
		 * 
		 */
		if (reader->fd < 0 || dst->disconnected) {
			debug3("can't attach %s to closed %s",
				_io_str[cli->type], _io_str[dst->type]);
			_obj_close(client, objList);
			return;
		} 

		_io_connect_objs(client, reader);
		if (!list_find_first(objList, (ListFindF) find_obj, client))
			list_append(objList, client);
		return;
	}

	io = list_peek(src->readers);
	xassert((io == NULL) || (io->magic  == IO_MAGIC));

	/* Check to see if src's first reader has disconnected, 
	 * if so, replace the object with this client, if not, 
	 * append client to readers list
	 */
	if ((io != NULL) && (io->disconnected)) {
		/* Resurrect the ghost:
		 * Attached client inherits ghost client's cbuf 
		 * and eof, as well as place in reader list and 
		 * master objList. However, we need to reset the 
		 * file descriptor, operations structure, and 
		 * disconnected flag.
		 */
		xassert(io->obj->fd == -1);
		xassert(io->obj->ops->writable);

		io->obj->fd      = client->fd;
		io->disconnected = 0;

		opsptr = io->obj->ops;
		io->obj->ops     = _ops_copy(client->ops); 
		xfree(opsptr);

		/* 
		 * Delete old client which is now an empty vessel 
		 */
		list_delete_all(objList, (ListFindF)find_obj, client);

		/* 
		 * Rewind a few lines if possible
		 */
		cbuf_rewind_line(io->buf, 256, -1);

		/*
		 * connect resurrected client ("io") to reader 
		 * if (reader != NULL).
		 */
		if (reader != NULL) 
			_io_connect_objs(io->obj, reader);
		xassert(io->obj->ops->writable == &_writable);
	} else {
		char buf[1024];
		/* Append new client into readers list and master objList
		 * client still copies existing eof bit, though.
		 */
		if (io) {
			int n;
			cli->eof = io->eof;

			if ((n = cbuf_replay_line(io->buf, buf, 256, -1)) > 0)
				cbuf_write(cli->buf, buf, n, NULL);
		} 
		_io_connect_objs(writer, client);
		if (reader != NULL)
			_io_connect_objs(client, reader);

		/* Only append to objList if client is not already present.
		 * (connecting client would already be in objList)
		 */
		if (!list_find_first(objList, (ListFindF) find_obj, client))
			list_append(objList, client);
	}
#endif
}

#if 0
static void
_io_connect_objs(io_obj_t *obj1, io_obj_t *obj2)
{
	struct io_info *src = (struct io_info *) obj1->arg;
	struct io_info *dst = (struct io_info *) obj2->arg;
	xassert(src->magic == IO_MAGIC);
	xassert(dst->magic == IO_MAGIC);

	if (!list_find_first(src->readers, (ListFindF)find_obj, dst))
		list_append(src->readers, dst);
	else
		debug3("%s already in %s readers list!", 
			_io_str[dst->type], _io_str[src->type]);

	if (!list_find_first(dst->writers, (ListFindF)find_obj, src))
		list_append(dst->writers, src);
	else
		debug3("%s already in %s writers list!",
			_io_str[src->type], _io_str[dst->type]);
}
#endif

/*
static int
find_fd(void *obj, void *key)
{
	xassert(obj != NULL);
	xassert(key != NULL);

	return (((io_obj_t *)obj)->fd == *((int *)key));
}
*/

static int 
find_obj(void *obj, void *key)
{
	xassert(obj != NULL);
	xassert(key != NULL);

	return (obj == key);
}

/* delete the connection from src to dst, i.e. remove src
 * from dst->writers, and dst from src->readers
 */
#if 0
static void
_io_disconnect(struct io_info *src, struct io_info *dst)
{
	char *a, *b;
	xassert(src->magic == IO_MAGIC);
	xassert(src->readers != NULL);
	xassert(dst->magic == IO_MAGIC);
	xassert(dst->writers != NULL);
	a = _io_str[dst->type];
	b = _io_str[src->type]; 

	if (list_delete_all(src->readers, (ListFindF)find_obj, dst) <= 0)
		error("Unable to delete %s from %s readers list", a, b);

	if (list_delete_all(dst->writers, (ListFindF)find_obj, src) <= 0)
		error("Unable to delete %s from %s writers list", b, a);
}
#endif

#if 0
static void
_io_disconnect_client(struct io_info *client, List objs)
{
	bool   destroy = true;
	struct io_info *t;
	ListIterator    i;

	xassert(client->magic == IO_MAGIC);
	xassert(client == client->obj->arg);

	/* Our client becomes a ghost
	 */
	client->disconnected = 1;
		
	if (client->writers) {
		/* delete client from its writer->readers list 
		 */
		i = list_iterator_create(client->writers);
		while ((t = list_next(i))) {
			if (list_count(t->readers) > 1) 
				_io_disconnect(t, client);
			else
				destroy = false;
		}
		list_iterator_destroy(i);
	}

	if (client->readers) {
		/* delete client from its reader->writers list
		 */
		i = list_iterator_create(client->readers);
		while ((t = list_next(i)))
			_io_disconnect(client, t);
		list_iterator_destroy(i);
	}

	xassert(client == client->obj->arg);

	if (!destroy) 
		return;

	debug3("Going to destroy %s %d", _io_str[client->type], client->id);
	if (list_delete_all(objs, (ListFindF)find_obj, client->obj) <= 0)
		error("Unable to destroy %s %d (%p)", 
		      _io_str[client->type], client->id, client); 

	return;
}
#endif

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


io_obj_t *
_io_obj(slurmd_job_t *job, slurmd_task_info_t *t, int fd, int type)
{
#if 0
	struct io_info *io = _io_info_create(t->gtid);
	struct io_obj *obj = _io_obj_create(fd, (void *)io);

	xassert(io->magic == IO_MAGIC);
	xassert(type >= 0);

	io->type = type;
	switch (type) {
	 case TASK_STDERR:
	 case TASK_STDOUT:
		 obj->ops    = &task_out_ops;
		 io->readers = list_create(NULL);
		 break;
	 case TASK_STDIN:
		 obj->ops    = &task_in_ops;
		 io->buf     = cbuf_create(512, 4096);
		 io->writers = list_create(NULL);

		 /* Never overwrite stdin data
		  */
		 cbuf_opt_set(io->buf, CBUF_OPT_OVERWRITE, 0);
		 break;
	 case CLIENT_STDOUT:
		 io->readers = list_create(NULL);
	 case CLIENT_STDERR:
		 xfree(obj->ops);
		 obj->ops    = _ops_copy(&client_ops);
		 io->buf     = cbuf_create(1024, 1048576);
		 io->writers = list_create(NULL);

		 cbuf_opt_set(io->buf, CBUF_OPT_OVERWRITE, CBUF_WRAP_ONCE);
		 break;
	 case CLIENT_STDIN: 
		 xfree(obj->ops);
		 obj->ops    = _ops_copy(&client_ops);
		 _obj_set_unwritable(obj);
		 io->readers = list_create(NULL);
		 /* 
		  * Connected stdin still needs output buffer 
		  * (for connection header)
		  */
		 io->buf     = cbuf_create(256, 1024);
		 break;
	 default:
		 error("io: unknown I/O obj type %d", type);
	}

	io->disconnected = fd < 0 ? 1 : 0;

	/* io info pointers back to eio object, 
	 *   job, and task information
	 */
	io->obj  = obj;
	io->job  = job;
	io->task = t;

	xassert(io->task->gtid == io->id);

	return obj;
#endif
}

void
io_obj_destroy(io_obj_t *obj)
{
	struct io_info *io = (struct io_info *) obj->arg;

#if 0
	xassert(obj != NULL);
	xassert(io  != NULL);
	xassert(io->magic == IO_MAGIC);

	switch (io->type) {
         case TASK_STDERR:
	 case TASK_STDOUT:
		 list_destroy(io->readers);
		 break;
	 case TASK_STDIN:
		 cbuf_destroy(io->buf);
		 list_destroy(io->writers);
		 break;
	 case CLIENT_STDOUT:
		 list_destroy(io->readers);
		 xfree(obj->ops);
	 case CLIENT_STDERR:
		 cbuf_destroy(io->buf);
		 list_destroy(io->writers);
		 xfree(obj->ops);
		 break;
	 case CLIENT_STDIN:
		 cbuf_destroy(io->buf);
		 xfree(obj->ops);
		 list_destroy(io->readers);
		 break;
	 default:
		 error("unknown IO object type: %ld", (long) io->type);
	}

	xassert(io->magic = ~IO_MAGIC);
	xfree(io);
	xfree(obj);
#endif
}

static io_obj_t *
_io_obj_create(int fd, void *arg)
{
	io_obj_t *obj = xmalloc(sizeof(*obj));
	obj->fd  = fd;
	obj->arg = arg;
	obj->ops = NULL;
	return obj;
}

static eio_obj_t *
_eio_obj_create(int fd, void *arg, struct io_operations *ops)
{
	eio_obj_t *obj = xmalloc(sizeof(*obj));
	obj->fd  = fd;
	obj->arg = arg;
	obj->ops = ops;
	return obj;
}

#if 0
static struct io_info *
_io_info_create(uint32_t id)
{
	struct io_info *io = (struct io_info *) xmalloc(sizeof(*io));
	io->id           = id;
	io->job          = NULL;
	io->task         = NULL;
	io->obj          = NULL;
	io->buf          = NULL;
	io->type         = -1;
	io->readers      = NULL;
	io->writers      = NULL;
	io->eof          = 0;
	io->disconnected = 0;
	io->rw           = 0;
	xassert(io->magic = IO_MAGIC);
	return io;
}
#endif

int
io_init_pipes(slurmd_job_t *job)
{
	int i;
	for (i = 0; i < job->ntasks; i++) {
		if (_io_init_pipes(job->task[i]) == SLURM_FAILURE) {
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

static int
_io_init_pipes(slurmd_task_info_t *t)
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

/* prepare for child I/O:
 * dup stdin,stdout,stderr onto appropriate pipes and
 * close write end of stdin, and read end of stdout/err
 */
int 
io_prepare_child(slurmd_task_info_t *t)
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
	struct io_info *io = (struct io_info *) obj->arg;

#if 0
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

	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->in.msg == NULL
	    && list_is_empty(client->job->free_io_buf))
		return false;

	return true;
}

static bool 
_client_writable(eio_obj_t *obj)
{
	struct client_io_info *client = (struct client_io_info *) obj->arg;

	xassert(client->magic == CLIENT_IO_MAGIC);

	if (client->out.msg == NULL
	    && list_is_empty(client->out.msg_queue))
		return false;

	return true;
}

static bool 
_task_readable(eio_obj_t *obj)
{
	return false;
}

static bool 
_task_writable(eio_obj_t *obj)
{
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

	/*
	 * Free the message and prepare to send the next one.
	 */
	out->msg->ref_count--;
	if (out->msg->ref_count == 0)
		list_enqueue(in->job->free_io_buf, out->msg);
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

	/*
	 * Free the message and prepare to send the next one.
	 */
	out->msg->ref_count--;
	if (out->msg->ref_count == 0)
		list_enqueue(client->job->free_io_buf, out->msg);
	out->msg = NULL;

	return SLURM_SUCCESS;
}

static void
_do_attach(struct io_info *io)
{
#if 0
	slurmd_task_info_t    *t;
	struct io_operations *opsptr;
	
	xassert(io != NULL);
	xassert(io->magic == IO_MAGIC);

	opsptr = io->obj->ops;
	io->obj->ops = _ops_copy(&client_ops);
	xfree(opsptr);

	t  = io->task;

	switch (io->type) {
	case CLIENT_STDOUT:
		if (io->rw) {
			debug3("attaching task %d client stdout read-write",
			       io->id);
			_io_client_attach( io->obj, t->out, t->in, 
					   io->job->objs );
		} else {
			debug3("attaching task %d client stdout write-only",
			       io->id);
			_io_client_attach( io->obj, t->out, NULL, 
					   io->job->objs ); 
		}
		break;
	case CLIENT_STDERR:
		_io_client_attach(io->obj, t->err, NULL,  io->job->objs);
		break;
	case CLIENT_STDIN:
		_io_client_attach(io->obj, NULL, t->in, io->job->objs);
		break;
	default:
		error("Unknown client type %d in do_attach()", io->type);

	}
#endif
}

static int
_shutdown_task_obj(struct io_info *t)
{
	ListIterator i;
	struct io_info *r;

#if 0
	debug3("shutdown_task_obj: %d %s [%d readers, %d writers]", 
		t->id, _io_str[t->type], 
		(t->readers ? list_count(t->readers) : 0),
		(t->writers ? list_count(t->writers) : 0));

	t->disconnected = 1;

	if (!t->readers)
		return 0;

	/* Task objects do not get destroyed. 
	 * Simply propagate the EOF to the clients
	 *
	 * Only propagate EOF to readers
	 *
	 */
	i = list_iterator_create(t->readers);
	while ((r = list_next(i))) 
		r->eof = 1;
	list_iterator_destroy(i);

#endif 
	return 0;
}

static struct io_buf *
_task_build_message(struct task_out_info *out, slurmd_job_t *job, cbuf_t cbuf)
{
	struct io_buf *msg;
	char *ptr;
	Buf packbuf;
	bool will_truncate = false;
	int avail;
	struct slurm_io_header header;
	int n;

	msg = list_dequeue(job->free_io_buf);
	if (msg == NULL)
		return NULL;

	ptr = msg->data + io_hdr_packed_size();
	avail = cbuf_peek_line(cbuf, ptr, MAX_MSG_LEN, -1);
	if (avail >= MAX_MSG_LEN)
		will_truncate = true;

	if (!will_truncate) {
		n = cbuf_read_line(cbuf, ptr, MAX_MSG_LEN, -1);
	} else {
		n = cbuf_read(cbuf, ptr, MAX_MSG_LEN);
	}

	header.type = out->type;
	header.ltaskid = out->task->id;
	header.gtaskid = out->task->gtid;
	header.length = n;

	packbuf = create_buf(msg->data, io_hdr_packed_size());
	io_hdr_pack(&header, packbuf);

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
	ListIterator clients;
	int len;
	int n;

	xassert(out->magic == TASK_OUT_MAGIC);

	len = cbuf_free(out->buf);
	if (len > 0) {
again:
		if ((n = cbuf_write_from_fd(out->buf, obj->fd, len, NULL))
		    < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				error("_task_read returned EAGAIN");
				return SLURM_SUCCESS;
			}
			/* FIXME add error message */
			return SLURM_ERROR;
		}
		if (n == 0) {  /* got eof */
			debug3("got eof on task");
			_obj_close(obj, objs);
			return SLURM_SUCCESS;
		}
	}

	/* Pack task output into a message for transfer to a client */
	if (cbuf_used(out->buf) > 0 && !list_is_empty(out->job->free_io_buf)) {
		msg = _task_build_message(out, out->job, out->buf);
		if (msg == NULL)
			return SLURM_ERROR;
	}
	if (msg == NULL)
		return SLURM_SUCCESS;

	/* Add message to the msg_queue of all clients */
	clients = list_iterator_create(out->job->clients);
	while((client = list_next(clients))) {
		xassert(client->magic == CLIENT_IO_MAGIC);
		list_enqueue(client->out.msg_queue, msg);
	}
	list_iterator_destroy(clients);

	return SLURM_SUCCESS;
}

static int 
_task_error(io_obj_t *obj, List objs)
{
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
 * Only return when the all of the bytes have been read, or an unignorable
 * error has occurred.
 */
int _full_read(int fd, void *buf, size_t count)
{
	int n;
again:
	if ((n = read(fd, (void *) buf, count)) < 0) {
		if (errno == EINTR)
			goto again;
		/*_update_error_state(client, E_READ, errno);*/
		return -1;
	}

}

/*
 * Read and unpack an io_hdr_t from a file descriptor (socket).
 */
int io_hdr_read_fd(int fd, struct slurm_io_header *hdr)
{
	Buf buffer;
	int rc = SLURM_SUCCESS;

	buffer = init_buf(io_hdr_packed_size());
	_full_read(fd, buffer->head, io_hdr_packed_size());
	rc = io_hdr_unpack(hdr, buffer);
	free_buf(buffer);

	return rc;
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

	xassert(client->magic == CLIENT_IO_MAGIC);

	in = &client->in;
	/*
	 * Read the header, if a message read is not already in progress
	 */
	if (in->msg == NULL) {
		in->msg = list_dequeue(client->job->free_io_buf);
		if (in->msg == NULL) {
			debug("List free_io_buf is empty!");
			return SLURM_ERROR;
		}

		io_hdr_read_fd(obj->fd, &in->header);
		in->remaining = in->header.length;
		in->msg->length = in->header.length;
	}

	/*
	 * Read the body
	 */
	/* FIXME who allocates msg->data? */
	buf = in->msg->data + (in->msg->length - in->remaining);
again:
	if ((n = read(obj->fd, buf, in->remaining)) < 0) {
		if (errno == EINTR)
			goto again;
		/* FIXME handle error */
		return SLURM_ERROR;
	}
	in->remaining -= n;
	if (in->remaining > 0)
		return SLURM_SUCCESS;

	/*
	 * Route the message to its destination(s)
	 */
	if (in->header.type != SLURM_IO_STDIN) {
		error("Input from client is not labelled SLURM_IO_STDIN!");
		in->msg = NULL;
		return SLURM_ERROR;
	} else {
		int i;
		slurmd_task_info_t *task;
		struct task_in_info *io;

		if (in->header.gtaskid == SLURM_IO_ALLTASKS) {
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
