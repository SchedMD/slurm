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
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/slurmd/job.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/io.h"
#include "src/slurmd/fname.h"
#include "src/slurmd/slurmd.h"

typedef enum slurmd_io_tupe {
	TASK_STDERR = 0,
	TASK_STDOUT,
	TASK_STDIN,
	CLIENT_STDERR,
	CLIENT_STDOUT,
	CLIENT_STDIN,
} slurmd_io_type_t;

static char *_io_str[] = 
{
	"task stderr",
	"task stdout",
	"task stdin",
	"client stderr",
	"client stdout",
	"client stdin",
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


/* The IO information structure
 */
struct io_info {
#ifndef NDEBUG
#define IO_MAGIC  0x10101
	int              magic;
#endif
	uint32_t         id;             /* global task id             */
	io_obj_t        *obj;            /* pointer back to eio object */
	slurmd_job_t    *job;		 /* pointer back to job data   */
	task_info_t     *task;           /* pointer back to task data  */
	cbuf_t           buf;		 /* IO buffer	               */
	List             readers;        /* list of current readers    */
	List             writers;        /* list of current writers    */
	slurmd_io_type_t type;           /* type of IO object          */

	struct error_state err;          /* error state information    */

	unsigned         eof:1;          /* obj recvd or generated EOF */

	unsigned         disconnected:1; /* signifies that fd is not 
					  * connected to anything
					  * (e.g. A "ghost" client attached
					  * to a task.)
					  */

	unsigned         rw:1;            /* 1 if client is read-write
					   * capable, 0 otherwize
					   */
};


static void   _fatal_cleanup(void *);
static int    find_obj(void *obj, void *key);
/* static int    find_fd(void *obj, void *key); */
static int    _io_init_pipes(task_info_t *t);
static int    _io_prepare_tasks(slurmd_job_t *);
static void * _io_thr(void *);
static int    _io_write_header(struct io_info *, srun_info_t *);
static void   _io_client_attach(io_obj_t *, io_obj_t *, io_obj_t *, 
		                List objList);
static void   _io_connect_objs(io_obj_t *, io_obj_t *);
static int    _shutdown_task_obj(struct io_info *t);
static bool   _isa_client(struct io_info *io);
static int    _open_output_file(slurmd_job_t *job, task_info_t *t, 
		                char *fname, slurmd_io_type_t type);
static int    _open_stdin_file(slurmd_job_t *job, task_info_t *t, 
		               srun_info_t *srun);

static struct io_obj  * _io_obj_create(int fd, void *arg);
static struct io_info * _io_info_create(uint32_t id);
static struct io_obj  * _io_obj(slurmd_job_t *, task_info_t *, int, int);
static void           * _io_thr(void *arg);

static void   _clear_error_state(struct io_info *io);
static int    _update_error_state(struct io_info *, enum error_type, int);

#ifndef NDEBUG
static bool   _isa_task(struct io_info *io);
#endif

static struct io_operations * _ops_copy(struct io_operations *ops);

/* Slurmd I/O objects:
 * N   task   stderr, stdout objs (read-only)
 * N*M client stderr, stdout objs (read-write) (possibly a file)
 * N   task   stdin          objs (write only) (possibly a file)
 */

static bool _readable(io_obj_t *);
static bool _writable(io_obj_t *);
static int  _write(io_obj_t *, List);
static int  _task_read(io_obj_t *, List);
static int  _client_read(io_obj_t *, List);
static int  _task_error(io_obj_t *, List);
static int  _client_error(io_obj_t *, List);
static int  _connecting_write(io_obj_t *, List);
static int  _obj_close(io_obj_t *, List);

/* Task Output operations (TASK_STDOUT, TASK_STDERR)
 * These objects are never writable --
 * therefore no need for writeable and handle_write methods
 */
struct io_operations task_out_ops = {
        readable:	&_readable,
	handle_read:	&_task_read,
        handle_error:	&_task_error,
	handle_close:   &_obj_close
};

/* Task Input operations (TASK_STDIN)
 * input objects are never readable
 */
struct io_operations task_in_ops = {
	writable:	&_writable,
	handle_write:	&_write,
	handle_error:	&_task_error,
	handle_close:   &_obj_close
};
			
/* Normal client operations (CLIENT_STDOUT, CLIENT_STDERR, CLIENT_STDIN)
 * these methods apply to clients which are considered
 * "connected" i.e. in the case of srun, they've read
 * the so-called IO-header data
 */
struct io_operations client_ops = {
        readable:	&_readable,
	writable:	&_writable,
	handle_read:	&_client_read,
	handle_write:	&_write,
	handle_error:	&_client_error,
	handle_close:   &_obj_close
};


/* Connecting client operations --
 * clients use a connecting write until they've
 * written out the IO header data. Not until that
 * point will clients be able to read regular 
 * stdout/err data, so we treat them special
 */
struct io_operations connecting_client_ops = {
        writable:	&_writable,
        handle_write:	&_connecting_write,
        handle_error:   &_client_error,
	handle_close:   &_obj_close
};


#ifndef NDEBUG
static int    _validate_io_list(List objList);
#endif /* NDEBUG */

/* 
 * Empty SIGHUP handler used to interrupt EIO thread 
 * system calls
 */
static void
_hup_handler(int sig) {;}

int
io_spawn_handler(slurmd_job_t *job) 
{
	pthread_attr_t attr;

	xsignal(SIGHUP, &_hup_handler);
	
	if (io_init_pipes(job) == SLURM_FAILURE) {
		error("io_handler: init_pipes failed: %m");
		return SLURM_FAILURE;
	}

	/* create task IO objects and append these to the objs list
	 */
	if (_io_prepare_tasks(job) < 0)
		return SLURM_FAILURE;

	if ((errno = pthread_attr_init(&attr)) != 0)
		error("pthread_attr_init: %m");

#ifdef PTHREAD_SCOPE_SYSTEM
	if ((errno = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) != 0)
		error("pthread_attr_setscope: %m");
#endif 
	xassert(_validate_io_list(job->objs));

	pthread_create(&job->ioid, &attr, &_io_thr, (void *)job);
	
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


/* 
 * Close child fds in parent as well as
 * any stdin io objs in job->objs
 * 
 */
static void
_io_finalize(task_info_t *t)
{
	struct io_info *in  = t->in->arg;

	if (_xclose(t->pin[0] ) < 0)
		error("close(stdin) : %m");
	if (_xclose(t->pout[1]) < 0)
		error("close(stdout): %m");
	if (_xclose(t->perr[1]) < 0)
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

void 
io_close_all(slurmd_job_t *job)
{
	int i;

	for (i = 0; i < job->ntasks; i++)
		_io_finalize(job->task[i]);

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

	_task_read(job->task[0]->err, job->objs);

	i = list_iterator_create(job->objs);
	while((obj = list_next(i))) {
		io = (struct io_info *) obj->arg;
		if ((*obj->ops->writable)(obj))
			_write(obj, job->objs);
	}
	list_iterator_destroy(i);
}

static void
_handle_unprocessed_output(slurmd_job_t *job)
{
	int i;
	task_info_t    *t;
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
}

static void *
_io_thr(void *arg)
{
	slurmd_job_t *job = (slurmd_job_t *) arg;
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
	task_info_t *t;
	io_obj_t    *obj;

	for (i = 0; i < job->ntasks; i++) {
		t = job->task[i];

		t->in  = _io_obj(job, t, t->pin[1],  TASK_STDIN );
		list_append(job->objs, (void *)t->in );

		t->out = _io_obj(job, t, t->pout[0], TASK_STDOUT);
		list_append(job->objs, (void *)t->out);

		/* "ghost" stdout client buffers task data without sending 
		 * it anywhere
		 */
		obj    = _io_obj(job, t, -1,         CLIENT_STDOUT);
		_io_client_attach(obj, t->out, NULL, job->objs);

		t->err = _io_obj(job, t, t->perr[0], TASK_STDERR);
		list_append(job->objs, (void *)t->err);

		/* "fake" stderr client buffers task data without sending 
		 * it anywhere
		 */
		obj    = _io_obj(job, t, -1,         CLIENT_STDERR);
		_io_client_attach(obj, t->err, NULL, job->objs);
	}

	xassert(_validate_io_list(job->objs));

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

static int
_io_add_connecting(slurmd_job_t *job, task_info_t *t, srun_info_t *srun, 
		   slurmd_io_type_t type)
{
	io_obj_t *obj  = NULL;
	int       sock = -1;

	debug3("in io_add_connecting");

	if ((sock = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect io: %m");
		/* XXX retry or silently fail? 
		 *     fail for now.
		 */
		return SLURM_ERROR;
	}
		
	fd_set_nonblocking(sock);
	fd_set_close_on_exec(sock);

	obj      = _io_obj(job, t, sock, type);
	obj->ops = _ops_copy(&connecting_client_ops);
	_io_write_header(obj->arg, srun);

	if ((type == CLIENT_STDOUT) && !srun->ifname) {
		struct io_info *io = obj->arg;
		/* This is the only read-write capable client
		 * at this time: a connected CLIENT_STDOUT
		 */
		io->rw = 1;
	}

	list_append(job->objs, (void *)obj);

	debug3("Now handling %d IO objects", list_count(job->objs));

	return SLURM_SUCCESS;
}

/*
 * If filename is given for stdout/err/in, open appropriate file,
 * otherwise create a connecting client back to srun process.
 */
static int
_io_prepare_one(slurmd_job_t *j, task_info_t *t, srun_info_t *s)
{
	int retval = SLURM_SUCCESS;
	/* Try hard to get stderr connected to something
	 */
	if (  (_open_output_file(j, t, s->efname, CLIENT_STDERR) < 0)
	   && (_io_add_connecting(j, t, s, CLIENT_STDERR)        < 0) )
		retval = SLURM_FAILURE;

	if (s->ofname) {
		if (_open_output_file(j, t, s->ofname, CLIENT_STDOUT) < 0)
			retval = SLURM_FAILURE;
	} else {
		_io_add_connecting(j, t, s, CLIENT_STDOUT); 
	}

	if (s->ifname) {
		if (_open_stdin_file(j, t, s) < 0)
			retval = SLURM_FAILURE;
	} else if (s->ofname) {
		_io_add_connecting(j, t, s, CLIENT_STDIN);
	}

	if (!list_find_first(t->srun_list, (ListFindF) find_obj, s)) {
		debug3("appending new client to srun_list for task %d", t->gid);
		list_append(t->srun_list, (void *) s);
	}

	return retval;
}

/* 
 * create initial client objs for N tasks
 */
int
io_prepare_clients(slurmd_job_t *job)
{
	int          i;
	int          retval = SLURM_SUCCESS;
	srun_info_t *srun;

	srun = list_peek(job->sruns);
	xassert(srun != NULL);

	if (srun->ofname && (fname_trunc_all(job, srun->ofname) < 0))
		goto error;

	if (  srun->efname  
	   && (!srun->ofname || (strcmp(srun->ofname, srun->efname) != 0))) {
		if (fname_trunc_all(job, srun->efname) < 0)
			goto error;
	}

	if (srun->ioaddr.sin_addr.s_addr) {
		char         host[256];
		short        port;
		slurmd_get_addr(&srun->ioaddr, &port, host, sizeof(host));
		debug2("connecting IO back to %s:%d", host, ntohs(port));
	} 

	/* Connect stdin/out/err to either a remote srun or
	 * local file
	 */
	for (i = 0; i < job->ntasks; i++) {
		if (_io_prepare_one(job, job->task[i], srun) < 0)
			retval = SLURM_ERROR;

		/* kick IO thread */
		eio_handle_signal(job->eio);
	}

	return retval;

   error:
	/* 
	 * Try to open stderr connection for errors
	 */
	_io_add_connecting(job, job->task[0], srun, CLIENT_STDERR);
	eio_handle_signal(job->eio);
	return SLURM_FAILURE;
}

int
io_new_clients(slurmd_job_t *job)
{
	return io_prepare_clients(job);
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
_open_output_file(slurmd_job_t *job, task_info_t *t, char *fmt, 
		  slurmd_io_type_t type)
{
	int          fd     = -1;
	io_obj_t    *obj    = NULL;
	int          flags  = O_APPEND|O_WRONLY;
	char        *fname  ;

	if (fmt == NULL)
		return SLURM_ERROR;

	xassert((type == CLIENT_STDOUT) || (type == CLIENT_STDERR));

	fname = fname_create(job, fmt, t->gid);
	if ((fd = _open_task_file(fname, flags)) > 0) {
		debug("opened `%s' for %s fd %d", fname, _io_str[type], fd);
		obj  = _io_obj(job, t, fd, type);
		_obj_set_unreadable(obj);
		xassert(obj->ops->writable != NULL);
		if (type == CLIENT_STDOUT)
			_io_client_attach(obj, t->out, NULL, job->objs);
		else
			_io_client_attach(obj, t->err, NULL, job->objs);
	} 
	xfree(fname);

	xassert(_validate_io_list(job->objs));

	return fd;
}

static int
_open_stdin_file(slurmd_job_t *job, task_info_t *t, srun_info_t *srun)
{
	int       fd    = -1;
	io_obj_t *obj   = NULL;
	int       flags = O_RDONLY;
	char     *fname = fname_create(job, srun->ifname, t->gid);
	
	if ((fd = _open_task_file(fname, flags)) > 0) {
		debug("opened `%s' for %s fd %d", fname, "stdin", fd);
		obj = _io_obj(job, t, fd, CLIENT_STDIN); 
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

	xassert(_validate_io_list(objList));
}

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

static void
_io_disconnect_client(struct io_info *client, List objs)
{
	bool   destroy = true;
	struct io_info *t;
	ListIterator    i;

	xassert(client->magic == IO_MAGIC);
	xassert(_isa_client(client));
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

#ifndef NDEBUG
static bool
_isa_task(struct io_info *io)
{
	xassert(io->magic == IO_MAGIC);
	return ((io->type == TASK_STDOUT)
		|| (io->type == TASK_STDERR)
		|| (io->type == TASK_STDIN ));
}
#endif

static bool
_isa_client(struct io_info *io)
{
	xassert(io->magic == IO_MAGIC);
	return ((io->type == CLIENT_STDOUT)
		|| (io->type == CLIENT_STDERR)
		|| (io->type == CLIENT_STDIN ));
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


io_obj_t *
_io_obj(slurmd_job_t *job, task_info_t *t, int fd, int type)
{
	struct io_info *io = _io_info_create(t->gid);
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

	xassert(io->task->gid == io->id);

	return obj;
}

void
io_obj_destroy(io_obj_t *obj)
{
	struct io_info *io = (struct io_info *) obj->arg;

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
_io_write_header(struct io_info *client, srun_info_t *srun)
{
	io_hdr_t hdr;

	memcpy(hdr.key, srun->key->data, SLURM_IO_KEY_SIZE);
	hdr.taskid = client->id;

	if ((client->type == CLIENT_STDOUT) || (client->type == CLIENT_STDIN)) 
		hdr.type = SLURM_IO_STDOUT; 
	else
		hdr.type = SLURM_IO_STDERR;

	if (io_hdr_write_cb(client->buf, &hdr) < 0) {
		error ("Unable to write io header: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int
_io_init_pipes(task_info_t *t)
{
	if (  (pipe(t->pin)  < 0) 
	   || (pipe(t->pout) < 0) 
	   || (pipe(t->perr) < 0) ) {
		error("io_init_pipes: pipe: %m");
		return SLURM_FAILURE;
	}

	fd_set_close_on_exec(t->pin[1]);
	fd_set_close_on_exec(t->pout[0]);
	fd_set_close_on_exec(t->perr[0]);

	fd_set_nonblocking(t->pin[1]);
	fd_set_nonblocking(t->pout[0]);
	fd_set_nonblocking(t->perr[0]);

	return SLURM_SUCCESS;
}

/* prepare for child I/O:
 * dup stdin,stdout,stderr onto appropriate pipes and
 * close write end of stdin, and read end of stdout/err
 */
int 
io_prepare_child(task_info_t *t)
{
	if (dup2(t->pin[0], STDIN_FILENO  ) < 0) {
		error("dup2(stdin): %m");
		return SLURM_FAILURE;
	}

	if (dup2(t->pout[1], STDOUT_FILENO) < 0) {
		error("dup2(stdout): %m");
		return SLURM_FAILURE;
	}

	if (dup2(t->perr[1], STDERR_FILENO) < 0) {
		error("dup2(stderr): %m");
		return SLURM_FAILURE;
	}

	/* ignore errors on close */
	close(t->pin[1] );
	close(t->pout[0]);
	close(t->perr[0]);
	return SLURM_SUCCESS;
}

static int
_obj_close(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);
	xassert(_validate_io_list(objs));

	debug3("Need to close %d %s", io->id, _io_str[io->type]);

	if (close(obj->fd) < 0)
		error("close: %m");
	obj->fd = -1;

	if (_isa_client(io)) 
		_io_disconnect_client(io, objs);
	else 
		_shutdown_task_obj(io);

	xassert(_validate_io_list(objs));

	return SLURM_SUCCESS;
}


static int 
_min_free (struct io_info *reader, int *lenp)
{
	int nfree = cbuf_free (reader->buf);
	if (nfree < *lenp)
		*lenp = nfree;
	return (0);
}


static int 
_max_readable (struct io_info *io, int max)
{
	if (!io->readers)
		return (0);
	/*
	 * Determine the maximum amount of data we will
	 * safely be able to read (starting at max)
	 */
	list_for_each (io->readers, (ListForF) _min_free, (void *) &max);
	return (max);
}

static bool 
_readable(io_obj_t *obj)
{
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);

	if (io->disconnected || io->eof || (obj->fd < 0))
		return (false);

	if (_max_readable(io, 1024) == 0)
		return (false);

	return (true);
}

static bool 
_writable(io_obj_t *obj)
{
	bool rc;
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);

	debug3("_writable(): [%p] task %d fd %d %s [%d %d %d]", io,
	       io->id, obj->fd, _io_str[io->type], io->disconnected, 
	       cbuf_used(io->buf), io->eof);

	rc = ((io->obj->fd > 0) 
	      && !io->disconnected 
	      && ((cbuf_used(io->buf) > 0) || io->eof));

	if ((io->type == CLIENT_STDERR) && (io->id == 0))
		rc = rc || (log_has_data() && !io->disconnected);

	if (rc)
		debug3("%d %s is writable", io->id, _io_str[io->type]);

	return rc;
}

static int
_write(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;
	int n = 0;

	xassert(io->magic == IO_MAGIC);
	xassert(io->type >= 0);

	if (io->disconnected)
		return 0;

	if (io->id == 0)
		log_flush();

	debug3("Need to write %d bytes to %s %d", 
		cbuf_used(io->buf), _io_str[io->type], io->id);

	/*  
	 *  If obj has recvd EOF, and there is no more data to write
	 *   (or there are many pending errors for this object),
	 *   close the descriptor and remove object from event lists.
	 */
	if ( io->eof 
	   && (  (cbuf_used(io->buf) == 0) 
	      || (io->err.e_count > 1)    ) ) {
		_obj_close(obj, objs);
		return 0;
	}

	while ((n = cbuf_read_to_fd(io->buf, obj->fd, 1)) < 0) {
		switch (errno) {
		case EAGAIN:
			return 0; 
			break;
		case EPIPE:
		case EINVAL:
		case EBADF:
		case ECONNRESET: 
			_obj_close(obj, objs); 
			break;
		default:
			_update_error_state(io, E_WRITE, errno);
		}
		return -1;
	}

	debug3("Wrote %d bytes to %s %d", n, _io_str[io->type], io->id);

	return 0;
}

static void
_do_attach(struct io_info *io)
{
	task_info_t    *t;
	struct io_operations *opsptr;
	
	xassert(io != NULL);
	xassert(io->magic == IO_MAGIC);
	xassert(_isa_client(io));

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
}

/* Write method for client objects which are connecting back to the
 * remote host
 */
static int
_connecting_write(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;
	int n;

	xassert(io->magic == IO_MAGIC);
	xassert(_isa_client(io));

	debug3("Need to write %d bytes to connecting %s %d", 
		cbuf_used(io->buf), _io_str[io->type], io->id);
	while ((n = cbuf_read_to_fd(io->buf, obj->fd, -1)) < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) 
			continue;
		if ((errno == EPIPE) || (errno == EINVAL) || (errno == EBADF))
			_obj_close(obj, objs);
		else
			error("write failed: <task %d>: %m", io->id);
		return -1;
	}
	debug3("Wrote %d bytes to %s %d", n, _io_str[io->type], io->id);

	/* If we've written the contents of the buffer, this is
	 * a connecting client no longer -- it may now be attached
	 * to the appropriate task.
	 */
	if (cbuf_used(io->buf) == 0)
		_do_attach(io);

	return 0;
}


static int
_shutdown_task_obj(struct io_info *t)
{
	ListIterator i;
	struct io_info *r;

	xassert(_isa_task(t));

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

	xassert(_validate_io_list(t->job->objs));	

	return 0;
}

static int
_task_read(io_obj_t *obj, List objs)
{
	struct io_info *r, *t;
	char buf[4096]; /* XXX Configurable? */
	ssize_t n, len = sizeof(buf);
	ListIterator i;

	t = (struct io_info *) obj->arg;

	xassert(t->magic == IO_MAGIC);
	xassert((t->type == TASK_STDOUT) || (t->type == TASK_STDERR));
	xassert(_validate_io_list(objs));

   again:
	if ((n = read(obj->fd, (void *) buf, len)) < 0) {
		if (errno == EINTR)
			goto again;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		        error("%s %d: read returned EAGAIN",
			       _io_str[t->type], t->id);
			return 0;
		}
		_update_error_state(t, E_READ, errno);
		return -1;
	}
	debug3("read %d bytes from %s %d", n, _io_str[t->type], t->id);

	if (n == 0) {  /* got eof */
		debug3("got eof on task %ld", (long) t->id);
		_obj_close(obj, objs);
		return 0;
	}

	/* copy buf to all readers */
	i = list_iterator_create(t->readers);
	while((r = list_next(i))) {
		int dropped;
		xassert(r->magic == IO_MAGIC);
		n = cbuf_write(r->buf, (void *) buf, n, &dropped);
		debug3("wrote %ld bytes into %s buf (fd=%d)", 
		       (long) n, _io_str[r->type], r->obj->fd);
		if (dropped > 0) {
			debug3("dropped %d bytes from %s buf", 
				dropped, _io_str[r->type]);
		}
	}
	list_iterator_destroy(i);

	return 0;
}

static int 
_task_error(io_obj_t *obj, List objs)
{
	int size, err;
	struct io_info *t = (struct io_info *) obj->arg;
	xassert(t->magic == IO_MAGIC);

	if (getsockopt(obj->fd, SOL_SOCKET, SO_ERROR, &err, &size) < 0)
		error ("getsockopt: %m");
	_update_error_state(t, E_POLL, err);
	_obj_close(obj, objs);
	return -1;
}
static int 
_client_read(io_obj_t *obj, List objs)
{
	struct io_info *client = (struct io_info *) obj->arg;
	struct io_info *reader;
	char            buf[4096]; 
	int             dropped  = 0;
	ssize_t         n        = 0;
	ssize_t         len      = sizeof(buf);
	ListIterator    i        = NULL;

	xassert(client->magic == IO_MAGIC);
	xassert(_validate_io_list(objs));
	xassert(_isa_client(client));

	len = _max_readable (client, len);

   again:
	if ((n = read(obj->fd, (void *) buf, len)) < 0) {
		if (errno == EINTR)
			goto again;
		_update_error_state(client, E_READ, errno);
		return -1;
	}

	debug3("read %d bytes from %s %d", n, _io_str[client->type],
			client->id);

	if (n == 0)  { /* got eof, pass this eof to readers */
		debug3("task %d [%s fd %d] read closed", 
		       client->id, _io_str[client->type],  obj->fd);
		/*
		 * Do not read from this stdin any longer
		 */
		_obj_set_unreadable(obj); 

		/* 
		 * Loop through this client's readers, 
		 * noting that EOF was recvd only if this
		 * client is the only writer
		 */
		if (client->readers) {
			i = list_iterator_create(client->readers);
			while((reader = list_next(i))) {
				if (list_count(reader->writers) == 1)
			 		reader->eof = 1;
				else
					debug3("can't send EOF to stdin");
				
			}
			list_iterator_destroy(i);
		}

		/* It is unsafe to close CLIENT_STDOUT
		 */
		if (client->type == CLIENT_STDIN)
			_obj_close(obj, client->job->objs);

		return 0;
	}

	if (client->type == CLIENT_STDERR) {
		/* unsigned long int signo = strtoul(buf, NULL, 10); */
		/* return kill(client->id, signo); */
		return 0;
	}

	/* 
	 * Copy cbuf to all readers 
	 */
	i = list_iterator_create(client->readers);
	while((reader = list_next(i))) {
		n = cbuf_write(reader->buf, (void *) buf, n, &dropped);
		if (dropped > 0)
			error("Dropped %d bytes stdin data to task %d", 
			      dropped, client->id);
	}
	list_iterator_destroy(i);

	return 0;
}

static int 
_client_error(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;
	socklen_t size = sizeof(int);
	int err = 0;

	xassert(io->magic == IO_MAGIC);

	if (getsockopt(obj->fd, SOL_SOCKET, SO_ERROR, &err, &size) < 0)
		error ("getsockopt: %m");

	if (err != ECONNRESET) /* Do not log connection resets */
		_update_error_state(io, E_POLL, err);

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

static void
_clear_error_state(struct io_info *io)
{
	io->err.e_time  = time(NULL);
	io->err.e_count = 0;
}

static void
_error_print(struct io_info *io)
{
	struct error_state *err = &io->err;

	if (!err->e_count) {
		error("%s: <task %d> %s: %s", 
		      err_string(err->e_type), io->id, _io_str[io->type],
		      slurm_strerror(err->e_last));
	} else {
		error("%s: <task %d> %s: %s (repeated %d times)", 
		      err_string(err->e_type), io->id, _io_str[io->type],
		      slurm_strerror(err->e_last), err->e_count);
	}
}


static int
_update_error_state(struct io_info *io, enum error_type type, int err) 
{
	xassert(io != NULL);
	xassert(io->magic == IO_MAGIC);
	xassert(err > 0);

	if (  (io->err.e_type == type)
	   && (io->err.e_last == err ) ) {
		/* 
		 * If the current and last error were the same,
		 *  update the error counter
		 */
		io->err.e_count++;

		/*
		 * If it has been less than 5 seconds since the
		 *  original error, don't print anything.
		 */
		if (  ((io->err.e_time + 5) > time(NULL))
		   && (io->err.e_count < 65000)         ) 
			return 0;

	} else {
		/*
		 * Update error values
		 */
		io->err.e_count = 0;
		io->err.e_type  = type;
		io->err.e_last  = err;
		io->err.e_time  = time(NULL);
	}

	_error_print(io);

	if (io->err.e_count > 0)
		_clear_error_state(io);

	return 0;
}

#ifndef NDEBUG
static void
_validate_task_out(struct io_info *t, int type)
{
	ListIterator i;
	struct io_info *r;

	xassert(t->magic == IO_MAGIC);
	xassert(!t->writers);
	i = list_iterator_create(t->readers);
	while ((r = list_next(i))) {
		xassert(r->magic == IO_MAGIC);
		xassert(r->type == type);
	}
	list_iterator_destroy(i);
}

static void
_validate_task_in(struct io_info *t)
{
	ListIterator i;
	struct io_info *r;

	xassert(t->magic == IO_MAGIC);
	xassert(!t->readers);
	i = list_iterator_create(t->writers);
	while ((r = list_next(i))) {
		xassert(r->magic == IO_MAGIC);
		xassert((r->type == CLIENT_STDOUT) 
		        || (r->type == CLIENT_STDIN));
	}
	list_iterator_destroy(i);
}


static void
_validate_client_stdout(struct io_info *client)
{
	ListIterator i;
	struct io_info *t;

	xassert(client->magic == IO_MAGIC);
	xassert(client->obj->ops->writable != NULL);
	
	i = list_iterator_create(client->readers);
	while ((t = list_next(i))) {
		xassert(t->magic == IO_MAGIC);
		xassert(t->type  == TASK_STDIN);
	}
	list_iterator_destroy(i);

	i = list_iterator_create(client->writers);
	while ((t = list_next(i))) {
		xassert(t->magic == IO_MAGIC);
		xassert(t->type  == TASK_STDOUT);
	}
	list_iterator_destroy(i);
}

static void
_validate_client_stderr(struct io_info *client)
{
	ListIterator i;
	struct io_info *t;

	xassert(client->magic == IO_MAGIC);
	xassert(!client->readers);
	xassert(client->obj->ops->writable != NULL);

	i = list_iterator_create(client->writers);
	while ((t = list_next(i))) {
		xassert(t->magic == IO_MAGIC);
		xassert(t->type  == TASK_STDERR);
	}
	list_iterator_destroy(i);
}

static void
_validate_client_stdin(struct io_info *client)
{
	ListIterator i;
	struct io_info *t;

	xassert(client->magic == IO_MAGIC);
	xassert(!client->writers);
	i = list_iterator_create(client->readers);
	while ((t = list_next(i))) {
		xassert(t->magic == IO_MAGIC);
		xassert(t->type  == TASK_STDIN);
	}
	list_iterator_destroy(i);
}

static int 
_validate_io_list(List objList)
{
	io_obj_t *obj;
	int retval = 1;
	ListIterator i = list_iterator_create(objList);
	while ((obj = list_next(i))) {
		struct io_info *io = (struct io_info *) obj->arg;

		xassert(io->obj == obj);

		switch (io->type) {
		case TASK_STDOUT:
			_validate_task_out(io, CLIENT_STDOUT);
			break;
		case TASK_STDERR:
			_validate_task_out(io, CLIENT_STDERR);
			break;
		case TASK_STDIN:
			_validate_task_in(io);
			break;
		case CLIENT_STDERR:
			_validate_client_stderr(io);
			break;
		case CLIENT_STDOUT:
			_validate_client_stdout(io);
			break;
		case CLIENT_STDIN:
			_validate_client_stdin(io);
		}
	}
	list_iterator_destroy(i);
	return retval;
}
#endif /* NDEBUG */

