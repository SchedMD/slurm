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
#include <errno.h>

#include <src/common/xmalloc.h>
#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/eio.h>

/* Function prototypes
 */

static int          _poll_loop_internal(List objs);
static int          _poll_internal(struct pollfd *pfds, unsigned int nfds);
static unsigned int _poll_setup_pollfds(struct pollfd *, io_obj_t **, List);
static void         _poll_dispatch(struct pollfd *, unsigned int, io_obj_t **,
		                   List objList);
static void         _poll_handle_event(short revents, io_obj_t *obj,
		                       List objList);

int io_handle_events(List objs)
{
	return _poll_loop_internal(objs);
}

static int
_poll_loop_internal(List objs)
{
	struct pollfd *pollfds;
	io_obj_t     **map;
	unsigned int   n, maxnfds = 0, nfds = 0;

	for (;;) {

		/* Alloc memory for pfds and map if needed */                  
		if (maxnfds < (n = list_count(objs))) {
			maxnfds = n;
			xrealloc(pollfds, maxnfds*sizeof(*pollfds));
			xrealloc(map, maxnfds*sizeof(*pollfds));
			/* 
			 * Note: xrealloc() also handles initial malloc 
			 */
		}

		if ((nfds = _poll_setup_pollfds(pollfds, map, objs)) <= 0)
			return 0;

		if (_poll_internal(pollfds, nfds) <= 0)
			break;

		_poll_dispatch(pollfds, nfds, map, objs);
	}
	return -1;
}

static int
_poll_internal(struct pollfd *pfds, unsigned int nfds)
{               
	int n;          
	while ((n = poll(pfds, nfds, -1)) < 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			continue;
		default:
			error("poll: %m");
			return -1;
		}
	}
	verbose("poll returned %d");
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

	while ((obj = (io_obj_t *) list_next(i))) {
		verbose("processing fd %d", obj->fd);
		if (_is_writable(obj) && _is_readable(obj)) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLOUT | POLLIN;
			map[nfds] = obj;
			nfds++;
		} else if (_is_readable(obj)) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLIN;
			map[nfds] = obj;
			nfds++;
		} else if (_is_writable(obj)) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLOUT;
			map[nfds] = obj;
			nfds++;
		}
	}
	list_iterator_destroy(i);
	return (nfds > 0) ? nfds - 1 : 0;
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
	}
	if ((revents & POLLOUT) && obj->ops->handle_write) {
		(*obj->ops->handle_write) (obj, objList);
	}
}


