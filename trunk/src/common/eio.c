/*****************************************************************************\
 * src/common/eio.c - Event-based I/O for slurm
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

#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/fd.h"
#include "src/common/eio.h"


struct eio_handle_components {
#ifndef NDEBUG
#       define EIO_MAGIC 0xe1e10
	int  magic;
#endif
	int  fds[2];
};


/* Function prototypes
 */

static int          _poll_loop_internal(eio_t eio, List objs);
static int          _poll_internal(struct pollfd *pfds, unsigned int nfds);
static unsigned int _poll_setup_pollfds(struct pollfd *, io_obj_t **, List);
static void         _poll_dispatch(struct pollfd *, unsigned int, io_obj_t **,
		                   List objList);
static void         _poll_handle_event(short revents, io_obj_t *obj,
		                       List objList);

eio_t eio_handle_create(void)
{
	eio_t eio = xmalloc(sizeof(*eio));

	if (pipe(eio->fds) < 0) {
		error ("eio_create: pipe: %m");
		eio_handle_destroy(eio);
		return (NULL);
	}

	fd_set_nonblocking(eio->fds[0]);

	xassert(eio->magic = EIO_MAGIC);

	return eio;
}

void eio_handle_destroy(eio_t eio)
{
	xassert(eio != NULL);
	xassert(eio->magic == EIO_MAGIC);
	close(eio->fds[0]);
	close(eio->fds[1]);
	xassert(eio->magic = ~EIO_MAGIC);
	xfree(eio);
}

int eio_handle_signal(eio_t eio)
{
	char c = 0;
	if (write(eio->fds[1], &c, sizeof(char)) != 1) 
		return error("eio_signal: write; %m");
	return 0;
}

static int _eio_clear(eio_t eio)
{
	char buf[1024];
	int rc = 0;

	while ((rc = (read(eio->fds[0], buf, 1024)) > 0))  {;}

	if (rc < 0) return error("eio_clear: read: %m");

	return 0;
}

int io_handle_events(eio_t eio, List objs)
{
	xassert (eio != NULL);
	xassert (eio->magic == EIO_MAGIC);

	return _poll_loop_internal(eio, objs);
}

static int
_poll_loop_internal(eio_t eio, List objs)
{
	int            retval  = 0;
	struct pollfd *pollfds = NULL;
	io_obj_t     **map     = NULL;
	unsigned int   maxnfds = 0, nfds = 0;
	unsigned int   n       = 0;

	for (;;) {

		/* Alloc memory for pfds and map if needed */                  
		if (maxnfds < (n = list_count(objs))) {
			maxnfds = n;
			xrealloc(pollfds, (maxnfds+1) * sizeof(struct pollfd));
			xrealloc(map,     maxnfds     * sizeof(io_obj_t *   ));
			/* 
			 * Note: xrealloc() also handles initial malloc 
			 */
		}

		debug3("eio: handling events for %d objects", 
				list_count(objs));
		/*
		 *  Clear any pending eio signals
		 */
		_eio_clear(eio);

		if ((nfds = _poll_setup_pollfds(pollfds, map, objs)) <= 0) 
			goto done;

		/*
		 *  Setup eio handle poll fd
		 */
		pollfds[nfds].fd     = eio->fds[0];
		pollfds[nfds].events = POLLIN;
		nfds++;

		xassert(nfds <= maxnfds + 1);


		if (_poll_internal(pollfds, nfds) < 0)
			goto error;

		if (pollfds[nfds-1].revents & POLLIN) 
			_eio_clear(eio);

		_poll_dispatch(pollfds, nfds-1, map, objs);
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
_is_writable(io_obj_t *obj)
{
	return (obj->ops->writable && (*obj->ops->writable)(obj));
}

static bool
_is_readable(io_obj_t *obj)
{
	return (obj->ops->readable && (*obj->ops->readable)(obj));
}

static unsigned int
_poll_setup_pollfds(struct pollfd *pfds, io_obj_t *map[], List l)
{
	ListIterator  i    = list_iterator_create(l);
	io_obj_t     *obj  = NULL;
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
_poll_dispatch(struct pollfd *pfds, unsigned int nfds, io_obj_t *map[], 
	       List objList)
{
	int i;
	ListIterator iter;
	io_obj_t *obj;

	for (i = 0; i < nfds; i++) {
		if (pfds[i].revents > 0)
			_poll_handle_event(pfds[i].revents, map[i], objList);
	}

	iter = list_iterator_create(objList);
	while ((obj = list_next(iter))) {
		if (_is_writable(obj) && obj->ops->handle_write)
			(*obj->ops->handle_write) (obj, objList);
	}
	list_iterator_destroy(iter);
}

static void
_poll_handle_event(short revents, io_obj_t *obj, List objList)
{
	if ((revents & POLLERR ) && obj->ops->handle_error) {
		if ((*obj->ops->handle_error) (obj, objList) < 0) 
			return;
	}

	if (((revents & POLLIN) || (revents & POLLHUP))
	    && obj->ops->handle_read ) {
		(*obj->ops->handle_read ) (obj, objList);
	} else if ((revents & POLLHUP) && obj->ops->handle_close) {
		(*obj->ops->handle_close) (obj, objList);
	}

	if ((revents & POLLOUT) && obj->ops->handle_write) {
		(*obj->ops->handle_write) (obj, objList);
	}
}


