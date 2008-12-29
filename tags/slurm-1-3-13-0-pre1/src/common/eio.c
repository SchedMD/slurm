/*****************************************************************************\
 * src/common/eio.c - Event-based I/O for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#  include <config.h>
#endif 

#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/fd.h"
#include "src/common/eio.h"

/*
 * outside threads can stick new objects on the new_objs List and
 * the eio thread will move them to the main obj_list the next time
 * it wakes up.
 */
struct eio_handle_components {
#ifndef NDEBUG
#       define EIO_MAGIC 0xe1e10
	int  magic;
#endif
	int  fds[2];
	List obj_list;
	List new_objs;
};


/* Function prototypes
 */

static int          _poll_internal(struct pollfd *pfds, unsigned int nfds);
static unsigned int _poll_setup_pollfds(struct pollfd *, eio_obj_t **, List);
static void         _poll_dispatch(struct pollfd *, unsigned int, eio_obj_t **,
		                   List objList);
static void         _poll_handle_event(short revents, eio_obj_t *obj,
		                       List objList);

eio_handle_t *eio_handle_create(void)
{
	eio_handle_t *eio = xmalloc(sizeof(*eio));

	if (pipe(eio->fds) < 0) {
		error ("eio_create: pipe: %m");
		eio_handle_destroy(eio);
		return (NULL);
	}

	fd_set_nonblocking(eio->fds[0]);
	fd_set_close_on_exec(eio->fds[0]);
	fd_set_close_on_exec(eio->fds[1]);

	xassert(eio->magic = EIO_MAGIC);

	eio->obj_list = list_create(NULL); /* FIXME!  Needs destructor */
	eio->new_objs = list_create(NULL);

	return eio;
}

void eio_handle_destroy(eio_handle_t *eio)
{
	xassert(eio != NULL);
	xassert(eio->magic == EIO_MAGIC);
	close(eio->fds[0]);
	close(eio->fds[1]);
	/* FIXME - Destroy obj_list and new_objs */
	xassert(eio->magic = ~EIO_MAGIC);
	xfree(eio);
}

int eio_signal_shutdown(eio_handle_t *eio)
{
	char c = 1;
	if (write(eio->fds[1], &c, sizeof(char)) != 1) 
		return error("eio_handle_signal_shutdown: write; %m");
	return 0;
}

int eio_signal_wakeup(eio_handle_t *eio)
{
	char c = 0;
	if (write(eio->fds[1], &c, sizeof(char)) != 1) 
		return error("eio_handle_signal_wake: write; %m");
	return 0;
}

static void _mark_shutdown_true(List obj_list)
{
	ListIterator objs;
	eio_obj_t *obj;

	objs = list_iterator_create(obj_list);
	while ((obj = list_next(objs))) {
		obj->shutdown = true;
	}
	list_iterator_destroy(objs);
}

static int _eio_wakeup_handler(eio_handle_t *eio)
{
	char c = 0;
	int rc = 0;
	eio_obj_t *obj;

	while ((rc = (read(eio->fds[0], &c, 1)) > 0)) {
		if (c == 1)
			_mark_shutdown_true(eio->obj_list);
	}

	/* move new eio objects from the new_objs to the obj_list */
	while ((obj = list_dequeue(eio->new_objs))) {
		list_enqueue(eio->obj_list, obj);
	}

	if (rc < 0) return error("eio_clear: read: %m");

	return 0;
}

int eio_handle_mainloop(eio_handle_t *eio)
{
	int            retval  = 0;
	struct pollfd *pollfds = NULL;
	eio_obj_t    **map     = NULL;
	unsigned int   maxnfds = 0, nfds = 0;
	unsigned int   n       = 0;

	xassert (eio != NULL);
	xassert (eio->magic == EIO_MAGIC);

	for (;;) {

		/* Alloc memory for pfds and map if needed */
		n = list_count(eio->obj_list);
		if (maxnfds < n) {
			maxnfds = n;
			xrealloc(pollfds, (maxnfds+1) * sizeof(struct pollfd));
			xrealloc(map,     maxnfds     * sizeof(eio_obj_t *  ));
			/* 
			 * Note: xrealloc() also handles initial malloc 
			 */
		}

		debug4("eio: handling events for %d objects", 
		       list_count(eio->obj_list));
		nfds = _poll_setup_pollfds(pollfds, map, eio->obj_list);
		if (nfds <= 0) 
			goto done;

		/*
		 *  Setup eio handle signalling fd
		 */
		pollfds[nfds].fd     = eio->fds[0];
		pollfds[nfds].events = POLLIN;
		nfds++;

		xassert(nfds <= maxnfds + 1);

		if (_poll_internal(pollfds, nfds) < 0)
			goto error;

		if (pollfds[nfds-1].revents & POLLIN) 
			_eio_wakeup_handler(eio);

		_poll_dispatch(pollfds, nfds-1, map, eio->obj_list);
	}
  error:
	retval = -1;
  done:
	xfree(pollfds);
	xfree(map); 
	return retval;
}

static int
_poll_internal(struct pollfd *pfds, unsigned int nfds)
{               
	int n;          
	while ((n = poll(pfds, nfds, -1)) < 0) {
		switch (errno) {
		case EINTR : return 0;
		case EAGAIN: continue;
		default:
			error("poll: %m");
			return -1;
		}
	}
	return n;
}

static bool
_is_writable(eio_obj_t *obj)
{
	return (obj->ops->writable && (*obj->ops->writable)(obj));
}

static bool
_is_readable(eio_obj_t *obj)
{
	return (obj->ops->readable && (*obj->ops->readable)(obj));
}

static unsigned int
_poll_setup_pollfds(struct pollfd *pfds, eio_obj_t *map[], List l)
{
	ListIterator  i    = list_iterator_create(l);
	eio_obj_t    *obj  = NULL;
	unsigned int  nfds = 0;
	bool          readable, writable;

	while ((obj = list_next(i))) {
		writable = _is_writable(obj);
		readable = _is_readable(obj);
		if (writable && readable) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLOUT | POLLIN;
			map[nfds]         = obj;
			nfds++;
		} else if (readable) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLIN;
			map[nfds]         = obj;
			nfds++;
		} else if (writable) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLOUT;
			map[nfds]         = obj;
			nfds++;
		}
	}
	list_iterator_destroy(i);
	return nfds;
}

static void
_poll_dispatch(struct pollfd *pfds, unsigned int nfds, eio_obj_t *map[], 
	       List objList)
{
	int i;

	for (i = 0; i < nfds; i++) {
		if (pfds[i].revents > 0)
			_poll_handle_event(pfds[i].revents, map[i], objList);
	}
}

static void
_poll_handle_event(short revents, eio_obj_t *obj, List objList)
{
	bool read_called = false;
	bool write_called = false;

	if (revents & (POLLERR|POLLNVAL)) {
		if (obj->ops->handle_error) {
			(*obj->ops->handle_error) (obj, objList);
		} else if (obj->ops->handle_read) {
			(*obj->ops->handle_read) (obj, objList);
			read_called = true;
		} else if (obj->ops->handle_write) {
			(*obj->ops->handle_write) (obj, objList);
			write_called = true;
		} else {
			debug("No handler for %s on fd %d",
			      revents & POLLERR ? "POLLERR" : "POLLNVAL",
			      obj->fd);
			obj->shutdown = true;
		}
		return;
	}

	if (revents & POLLHUP) {
		if (obj->ops->handle_close) {
			(*obj->ops->handle_close) (obj, objList);
		} else if (obj->ops->handle_read) {
			if (!read_called) {
				(*obj->ops->handle_read) (obj, objList);
				read_called = true;
			}
		} else if (obj->ops->handle_write) {
			if (!write_called) {
				(*obj->ops->handle_write) (obj, objList);
				write_called = true;
			}
		} else {
			debug("No handler for POLLHUP");
			obj->shutdown = true;
		}
	}

	if (revents & POLLIN) {
		if (obj->ops->handle_read) {
			if (!read_called) {
				(*obj->ops->handle_read ) (obj, objList);
				read_called = true;
			}
		} else {
			debug("No handler for POLLIN");
			obj->shutdown = true;
		}
	}

	if (revents & POLLOUT) {
		if (obj->ops->handle_write) {
			if (!write_called) {
				(*obj->ops->handle_write) (obj, objList);
				write_called = true;
			}
		} else {
			debug("No handler for POLLOUT");
			obj->shutdown = true;
		}
	}
}

static struct io_operations *
_ops_copy(struct io_operations *ops)
{
	struct io_operations *ret = xmalloc(sizeof(*ops));

	/* Copy initial client_ops */
	*ret = *ops;
	return ret;
}

eio_obj_t *
eio_obj_create(int fd, struct io_operations *ops, void *arg)
{
	eio_obj_t *obj = xmalloc(sizeof(*obj));
	obj->fd  = fd;
	obj->arg = arg;
	obj->ops = _ops_copy(ops);
	obj->shutdown = false;
	return obj;
}

void eio_obj_destroy(eio_obj_t *obj)
{
	if (obj) {
		if (obj->ops) {
			xfree(obj->ops);
		}
		xfree(obj);
	}
}


/*
 * Add an eio_obj_t "obj" to an eio_handle_t "eio"'s internal object list.
 *
 * This function can only be used to intialize "eio"'s list before
 * calling eio_handle_mainloop.  If it is used after the eio engine's
 * mainloop has started, segfaults are likely.
 */
void eio_new_initial_obj(eio_handle_t *eio, eio_obj_t *obj)
{
	xassert(eio != NULL);
	xassert(eio->magic == EIO_MAGIC);
	
	list_enqueue(eio->obj_list, obj);
}

/*
 * Queue an eio_obj_t "obj" for inclusion in an already running
 * eio_handle_t "eio"'s internal object list.
 */
void eio_new_obj(eio_handle_t *eio, eio_obj_t *obj)
{
	xassert(eio != NULL);
	xassert(eio->magic == EIO_MAGIC);

	list_enqueue(eio->new_objs, obj);
	eio_signal_wakeup(eio);
}
