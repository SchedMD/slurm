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
#  include <config.h>
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

#include <unistd.h>
#include <errno.h>

#include <src/common/eio.h>
#include <src/common/cbuf.h>
#include <src/common/log.h>
#include <src/common/fd.h>
#include <src/common/list.h>
#include <src/common/xmalloc.h>

#include <src/slurmd/job.h>
#include <src/slurmd/shm.h>
#include <src/slurmd/io.h>

typedef enum slurmd_io_tupe {
	TASK_STDERR,
	TASK_STDOUT,
	TASK_STDIN,
	CLIENT_STDERR,
	CLIENT_STDOUT,
} slurmd_io_type_t;

static char *_io_str[] = 
{
	"task stderr",
	"task stdout",
	"task stdin",
	"client stderr",
	"client stdout"
};


struct io_info {
#ifndef NDEBUG
#define IO_MAGIC 0x10101
	int magic;
#endif
	uint32_t id;
	cbuf_t buf;
	List readers;
	List writers;
	slurmd_io_type_t type;
	unsigned eof:1;
	unsigned disconnected:1;
};


static int    _io_init_pipes(task_info_t *t);
static void   _io_prepare_clients(slurmd_job_t *);
static void   _io_prepare_tasks(slurmd_job_t *);
static void * _io_thr(void *);
static int    _io_write_header(struct io_info *, srun_info_t *);
static void   _io_connect_objs(io_obj_t *, io_obj_t *);
static int    _validate_io_list(List objList);
static int    _shutdown_task_obj(struct io_info *t);

static struct io_obj  * _io_obj_create(int fd, void *arg);
static struct io_info * _io_info_create(uint32_t id);
static struct io_obj  * _io_obj(int fd, uint id, int type);
static void           * _io_thr(void *arg);


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


struct io_operations task_out_ops = {
        readable:	&_readable,
	handle_read:	&_task_read,
        handle_error:	&_task_error
};

struct io_operations task_in_ops = {
	writable:	&_writable,
	handle_write:	&_write,
	handle_error:	&_task_error,
};
			
struct io_operations client_ops = {
	readable:	&_readable,
	writable:	&_writable,
	handle_read:	&_client_read,
	handle_write:	&_write,
	handle_error:	&_client_error,
};

int
io_spawn_handler(slurmd_job_t *job) 
{
	pthread_attr_t attr;
	
	if (io_init_pipes(job) == SLURM_FAILURE) {
		error("io_handler: init_pipes failed: %m");
		return SLURM_FAILURE;
	}

	/* create task IO objects and append these to the objs list
	 *
	 * XXX check for errors?
	 */
	_io_prepare_tasks(job);

	/* open 2*ntask initial connections or files for stdout/err 
	 * append these to objs list 
	 */
	_io_prepare_clients(job);

	if ((errno = pthread_attr_init(&attr)) != 0)
		error("pthread_attr_init: %m");

#ifdef PTHREAD_SCOPE_SYSTEM
	if ((errno = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) != 0)
		error("pthread_attr_setscope: %m");
#endif 
	xassert(_validate_io_list(job->objs));

	return pthread_create(&job->ioid, &attr, &_io_thr, (void *)job);
}

static int
_xclose(int fd)
{
	int rc;
	do rc = close(fd);
	while (rc == -1 && errno == EINTR);
	return rc;
}

/* Close child fds in parent */
static void
_io_finalize(task_info_t *t)
{
	if (_xclose(t->pin[0] ) < 0)
		error("close(stdin) : %m");
	if (_xclose(t->pout[1]) < 0)
		error("close(stdout): %m");
	if (_xclose(t->perr[1]) < 0)
		error("close(stderr): %m");
}

void 
io_close_all(slurmd_job_t *job)
{
	int i;
	for (i = 0; i < job->ntasks; i++)
		_io_finalize(job->task[i]);
}

static void *
_io_thr(void *arg)
{
	slurmd_job_t *job = (slurmd_job_t *) arg;
	io_handle_events(job->objs);
	verbose("IO handler exited");
	return (void *)1;
}

static void
_io_prepare_tasks(slurmd_job_t *job)
{
	int          i;
	srun_info_t *srun;
	task_info_t *t;

	srun = list_peek(job->sruns);

	for (i = 0; i < job->ntasks; i++) {
		t = job->task[i];

		t->in  = _io_obj(t->pin[1],  t->gid, TASK_STDIN );
		list_append(job->objs, (void *)t->in );

		t->out = _io_obj(t->pout[0], t->gid, TASK_STDOUT);
		list_append(job->objs, (void *)t->out);

		t->err = _io_obj(t->perr[0], t->gid, TASK_STDERR);
		list_append(job->objs, (void *)t->err);
	}
}

#if 0
/*
 * create initial file objs for N tasks
 */
static void
_io_prepare_files(slurmd_job_t *job)
{
	int       i, fd;
	int       err_flags = O_WRONLY | O_CREAT | O_EXCL;
	int       out_flags = O_WRONLY | O_CREAT | O_EXCL;
	int       in_flags  = O_RDONLY | O_CREAT | O_EXCL;
	char     *filename;
	io_obj_t *obj;

	if (job->outf) {
		if 
		for (i = 0; i < job->ntasks; i++) {
			char *buf[4096];
			snprintf(buf, 4096, job->outf, i);
			if (open(buf, out_flags) < 0)
				error("can't open file `%s': %m", buf);
			
		}
	}
}
#endif

/* 
 * create initial client objs for N tasks
 */
static void
_io_prepare_clients(slurmd_job_t *job)
{
	int          i, sock;
	io_obj_t    *obj;
	srun_info_t *srun;

	xassert(list_count(job->sruns) == 1);

	srun = list_peek(job->sruns);

	/* create sockets for stdout/err 
	 */
	for (i = 0; i < job->ntasks; i++) {
		task_info_t *t = job->task[i];

		sock = (int) slurm_open_stream(&srun->ioaddr);
		if (sock < 1) {
			error("connect io: %m");
			return;
		}
		fd_set_nonblocking(sock);
		fd_set_close_on_exec(sock);
		obj  = _io_obj(sock, t->gid, CLIENT_STDOUT);
		_io_write_header(obj->arg, srun);
		list_append(job->objs, obj);

		_io_connect_objs(t->out, obj);
		_io_connect_objs(obj, t->in );

		sock = (int) slurm_open_stream(&srun->ioaddr);
		fd_set_nonblocking(sock);
		fd_set_close_on_exec(sock);
		obj  = _io_obj(sock, t->gid, CLIENT_STDERR);
		_io_write_header(obj->arg, srun);
		list_append(job->objs, obj);

		_io_connect_objs(t->err, obj);
	}
}

static void
_io_connect_objs(io_obj_t *obj1, io_obj_t *obj2)
{
	struct io_info *src = (struct io_info *) obj1->arg;
	struct io_info *dst = (struct io_info *) obj2->arg;
	xassert(src->magic == IO_MAGIC);
	xassert(dst->magic == IO_MAGIC);
	list_append(src->readers, dst);
	list_append(dst->writers, src);
}

static int
_validate_task_out(struct io_info *t, int type)
{
	ListIterator i;
	struct io_info *r;
	int retval = 1;

	xassert(t->magic == IO_MAGIC);
	
	if (t->writers)
		retval = 0;

	i = list_iterator_create(t->readers);
	while ((r = list_next(i))) {
		if (r->type != type) {
			fatal("_validate_io: %s reader is %s",
					_io_str[t->type],
					_io_str[r->type]);
		}
	}
	list_iterator_destroy(i);

	return retval;
}

static int
_validate_task_in(struct io_info *t)
{
	ListIterator i;
	struct io_info *r;
	int retval = 1;

	xassert(t->magic == IO_MAGIC);

	if (t->readers)
		retval = 0;

	i = list_iterator_create(t->writers);
	while ((r = list_next(i)) != NULL) {
		if (r->magic != IO_MAGIC) {
			error("_validate_io: %s writer is invalid", 
					_io_str[t->type]);
			return 0;
		}
		if (r->type != CLIENT_STDOUT) {
			error("_validate_io: %s writer is %s",
					_io_str[t->type],
					_io_str[r->type]);
			retval = 0;
		}
	}
	list_iterator_destroy(i);

	return retval;
}


static int
_validate_client_stdout(struct io_info *client)
{
	ListIterator i;
	struct io_info *t;
	int retval = 1;

	xassert(client->magic == IO_MAGIC);
	
	i = list_iterator_create(client->readers);
	while ((t = list_next(i))) {
		if (t->type != TASK_STDIN) {
			error("_validate_io: client stdin reader is %s",
					_io_str[t->type]);
			retval = 0;
		}
	}
	list_iterator_destroy(i);

	i = list_iterator_create(client->writers);
	while ((t = list_next(i))) {
		if (t->type != TASK_STDOUT) {
			error("_validate_io: client stdout writer is %s",
					_io_str[t->type]);
			retval = 0;
		}
	}
	list_iterator_destroy(i);
	
	return retval;
}

static int
_validate_client_stderr(struct io_info *client)
{
	ListIterator i;
	struct io_info *t;
	int retval = 1;

	xassert(client->magic == IO_MAGIC);

	if (client->readers)
		retval = 0;
	
	i = list_iterator_create(client->writers);
	while ((t = list_next(i))) {
		if (t->type != TASK_STDERR) {
			error("_validate_io: client stderr writer is %s",
					_io_str[t->type]);
			retval = 0;
		}
	}
	list_iterator_destroy(i);

	return retval;
}

static int 
_validate_io_list(List objList)
{
	io_obj_t *obj;
	int retval = 1;
	ListIterator i = list_iterator_create(objList);
	while ((obj = list_next(i))) {
		struct io_info *io = (struct io_info *) obj->arg;
		switch (io->type) {
		case TASK_STDOUT:
			xassert(_validate_task_out(io, CLIENT_STDOUT));
			break;
		case TASK_STDERR:
			xassert(_validate_task_out(io, CLIENT_STDERR));
			break;
		case TASK_STDIN:
			xassert(_validate_task_in(io));
			break;
		case CLIENT_STDERR:
			xassert(_validate_client_stderr(io));
			break;
		case CLIENT_STDOUT:
			xassert(_validate_client_stdout(io));
			break;
		}
	}
	list_iterator_destroy(i);
	return retval;
}

static int 
find_obj(struct io_info *obj, struct io_info *key)
{
	xassert(obj != NULL);
	xassert(key != NULL);

	return (obj == key);
}


static void
_io_disconnect_client(struct io_info *client)
{
	struct io_info *t;
	int n;

	xassert(client->magic == IO_MAGIC);
	client->disconnected = 1;
		
	/* delete client from its writer->readers list 
	 * (a client should have only one writer) 
	 */
	t = list_peek(client->writers);

	xassert(!t || t->type == TASK_STDERR || t->type == TASK_STDOUT);
	if (t && list_count(t->readers) > 1) {
	        n = list_delete_all(t->readers, (ListFindF)find_obj, client);
		if (n <= 0)
			error("deleting client from readers");
	}

	if (!client->readers)
		return;

	/* delete STDOUT client from its reader->writers list
	 * (a client obj should have only one reader)
	 */
	t = list_peek(client->readers);
	if (t) {
	        n = list_delete_all(t->writers, (ListFindF)find_obj, client);
		if (n <= 0)
			error("deleting client from readers");
	}
}


io_obj_t *
_io_obj(int fd, uint32_t id, int type)
{
	struct io_info *io = _io_info_create(id);
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
		 io->buf     = cbuf_create(512, 10240);
		 io->writers = list_create(NULL);
		 break;
	 case CLIENT_STDOUT:
		 io->readers = list_create(NULL);
	 case CLIENT_STDERR:
		 obj->ops    = &client_ops;
		 io->buf     = cbuf_create(16, 1048576);
		 io->writers = list_create(NULL);
		 break;
	 default:
		 error("io: unknown I/O obj type %d", type);
	}
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
	 case CLIENT_STDERR:
		 cbuf_destroy(io->buf);
		 list_destroy(io->writers);
		 break;
	 default:
		 error("unknown IO object type: %ld", io->type);
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
	xassert(io->magic = IO_MAGIC);
	io->id  = id;
	io->buf = NULL;
	io->type = -1;
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
	slurm_io_stream_header_t hdr;
	char *buf;
	int retval;
	int size   = sizeof(hdr);
	Buf buffer = init_buf(size);

	hdr.version = SLURM_PROTOCOL_VERSION;
	memcpy(hdr.key, srun->key->data, SLURM_SSL_SIGNATURE_LENGTH);
	hdr.task_id = client->id;
	hdr.type    = client->type == CLIENT_STDOUT ? 0 : 1;

	pack_io_stream_header(&hdr, buffer);

	/* XXX Shouldn't have to jump through these hoops to 
	 * support slurm Buf type. Need a better way to do this
	 */
	size   = buffer->processed;
	buf    = xfer_buf_data(buffer);
	retval = cbuf_write(client->buf, buf, size, NULL);
	xfree(buf);
	return retval;
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

static bool 
_readable(io_obj_t *obj)
{
	bool rc;
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);

	rc = (!io->disconnected && !io->eof && (obj->fd > 0));

	return rc;
}

static bool 
_writable(io_obj_t *obj)
{
	bool rc;
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);

	rc = (!io->disconnected 
		&& ((cbuf_used(io->buf) > 0) || io->eof));

	return rc;
}

static int
_write(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;
	int n;

	xassert(io->magic == IO_MAGIC);
	xassert(io->type >= 0);

	if (io->disconnected)
		return 0;

	debug3("Need to write %ld bytes to %s %d", 
		cbuf_used(io->buf), _io_str[io->type], io->id);


	if (io->eof && (cbuf_used(io->buf) == 0)) {
		if (close(obj->fd) < 0)
			error("close: %m");
		obj->fd = -1;
		if (io->type == CLIENT_STDERR || io->type == CLIENT_STDOUT)
			_io_disconnect_client(io);
		else
			_shutdown_task_obj(io);
		list_delete_all(objs, (ListFindF)find_obj, obj); 
		return 0;
	}

	while ((n = cbuf_read_to_fd(io->buf, obj->fd, -1)) < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) 
			continue;
		error("task <%ld> write failed: %m", io->id);
		if (io->type == CLIENT_STDERR || io->type == CLIENT_STDOUT)
			_io_disconnect_client(io);
		else
			_shutdown_task_obj(io);
		return -1;
	}

	debug3("Wrote %d bytes to %s %d", 
		 n, _io_str[io->type], io->id);

	return 0;
}

/* */
static int
_shutdown_task_obj(struct io_info *t)
{
	List l;
	ListIterator i;
	struct io_info *r;

	l = (t->type == TASK_STDIN) ? t->writers : t->readers;
	
	i = list_iterator_create(l);
	while ((r = list_next(i))) {
		List rlist = (t->type == TASK_STDIN) ? r->readers : r->writers;
		r->eof = 1;
		list_delete_all(rlist, (ListFindF) find_obj, t);
	}
	list_iterator_destroy(i);

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
		error("Unable to read from task %ld fd %d errno %d %m", 
				t->id, obj->fd, errno);
		return -1;
	}
	debug3("read %d bytes from %s %d", 
		n, _io_str[t->type], t->id);

	if (n == 0) {  /* got eof */
		debug3("got eof on task %ld", t->id);
		_shutdown_task_obj(t);
		close(obj->fd);
		obj->fd = -1;
		if (list_delete_all(objs, (ListFindF) find_obj, obj) <= 0)
			error("Unable to remove task object from list");
		return 0;
	}

	/* copy buf to all readers */
	i = list_iterator_create(t->readers);
	while((r = list_next(i))) {
		n = cbuf_write(r->buf, (void *) buf, n, NULL);
		debug3("wrote %ld bytes into %s buf", n, 
				_io_str[r->type]);
	}
	list_iterator_destroy(i);

	return 0;
}

static int 
_task_error(io_obj_t *obj, List objs)
{
	struct io_info *t = (struct io_info *) obj->arg;
	xassert(t->magic == IO_MAGIC);

	error("error on %s %d", _io_str[t->type], t->id);
	_shutdown_task_obj(t);
	obj->fd = -1;
	list_delete_all(objs, (ListFindF) find_obj, obj);

	xassert(_validate_io_list(objs));
	return -1;
}

static int 
_client_read(io_obj_t *obj, List objs)
{
	struct io_info *client = (struct io_info *) obj->arg;
	struct io_info *reader;
	char buf[1024]; /* XXX Configurable? */
	ssize_t n, len = sizeof(buf);
	ListIterator i;

	xassert(client->magic == IO_MAGIC);
	xassert(_validate_io_list(objs));
	xassert((client->type == CLIENT_STDOUT) 
		 || (client->type == CLIENT_STDERR));

   again:
	if ((n = read(obj->fd, (void *) buf, len)) < 0) {
		if (errno == EINTR)
			goto again;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			fatal("client read");
		error("read from client %ld: %m", client->id);
		return -1;
	}

	debug("read %d bytes from %s %d", n, _io_str[client->type],
			client->id);

	if (n == 0)  { /* got eof, disconnect this client */
		debug3("client %d closed connection", client->id);
		if (!client->disconnected)
			_io_disconnect_client(client);
		xassert(_validate_io_list(objs));
		return 0;
	}

	if (client->type == CLIENT_STDERR) {
		/* unsigned long int signo = strtoul(buf, NULL, 10); */
		/* return kill(client->id, signo); */
		return 0;
	}

	/* copy buf to all readers 
	 * XXX Client should never have more than one reader,
	 *     unless we choose to support this? 
	 */
	i = list_iterator_create(client->readers);
	while((reader = list_next(i))) {
		n = cbuf_write(reader->buf, (void *) buf, n, NULL);
	}
	list_iterator_destroy(i);

	return 0;
}

static int 
_client_error(io_obj_t *obj, List objs)
{
	struct io_info *io = (struct io_info *) obj->arg;

	xassert(io->magic == IO_MAGIC);

	error("%s task %d", _io_str[io->type], io->id); 
	return 0;
}

