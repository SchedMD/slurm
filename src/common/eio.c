/*****************************************************************************\
 * src/common/eio.c - Event-based I/O for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/eio.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/net.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(eio_handle_create,		slurm_eio_handle_create);
strong_alias(eio_handle_destroy,	slurm_eio_handle_destroy);
strong_alias(eio_handle_mainloop,	slurm_eio_handle_mainloop);
strong_alias(eio_message_socket_readable, slurm_eio_message_socket_readable);
strong_alias(eio_message_socket_accept,	slurm_eio_message_socket_accept);
strong_alias(eio_new_obj,		slurm_eio_new_obj);
strong_alias(eio_new_initial_obj,	slurm_eio_new_initial_obj);
strong_alias(eio_obj_create,		slurm_eio_obj_create);
strong_alias(eio_obj_destroy,		slurm_eio_obj_destroy);
strong_alias(eio_remove_obj,		slurm_eio_remove_obj);
strong_alias(eio_signal_shutdown,	slurm_eio_signal_shutdown);
strong_alias(eio_signal_wakeup,		slurm_eio_signal_wakeup);

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
	pthread_mutex_t shutdown_mutex;
	time_t shutdown_time;
	uint16_t shutdown_wait;
	List obj_list;
	List new_objs;
};


/* Function prototypes
 */

static int          _poll_internal(struct pollfd *pfds, unsigned int nfds,
				   time_t shutdown_time);
static unsigned int _poll_setup_pollfds(struct pollfd *, eio_obj_t **, List);
static void         _poll_dispatch(struct pollfd *, unsigned int, eio_obj_t **,
		                   List objList);
static void         _poll_handle_event(short revents, eio_obj_t *obj,
		                       List objList);


eio_handle_t *eio_handle_create(uint16_t shutdown_wait)
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

	xassert((eio->magic = EIO_MAGIC));

	eio->obj_list = list_create(eio_obj_destroy);
	eio->new_objs = list_create(eio_obj_destroy);

	slurm_mutex_init(&eio->shutdown_mutex);
	eio->shutdown_wait = DEFAULT_EIO_SHUTDOWN_WAIT;
	if (shutdown_wait > 0)
		eio->shutdown_wait = shutdown_wait;

	return eio;
}

void eio_handle_destroy(eio_handle_t *eio)
{
	xassert(eio != NULL);
	xassert(eio->magic == EIO_MAGIC);
	close(eio->fds[0]);
	close(eio->fds[1]);
	FREE_NULL_LIST(eio->obj_list);
	FREE_NULL_LIST(eio->new_objs);
	slurm_mutex_destroy(&eio->shutdown_mutex);

	xassert((eio->magic = ~EIO_MAGIC));
	xfree(eio);
}

bool eio_message_socket_readable(eio_obj_t *obj)
{
	debug3("Called eio_message_socket_readable %d %d",
	       obj->shutdown, obj->fd);
	xassert(obj);
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			close(obj->fd);
			obj->fd = -1;
		} else {
			debug2("  false");
		}
		return false;
	}
	return true;
}

int eio_message_socket_accept(eio_obj_t *obj, List objs)
{
	int fd;
	unsigned char *uc;
	unsigned short port;
	struct sockaddr_in addr;
	slurm_msg_t *msg = NULL;
	int len = sizeof(addr);

	debug3("Called eio_msg_socket_accept");

	xassert(obj);
	xassert(obj->ops->handle_msg);

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if ((errno == EAGAIN) ||
		    (errno == ECONNABORTED) ||
		    (errno == EWOULDBLOCK)) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		if ((errno == EMFILE)  ||
		    (errno == ENFILE)  ||
		    (errno == ENOBUFS) ||
		    (errno == ENOMEM)) {
			return SLURM_SUCCESS;
		}
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	net_set_keep_alive(fd);
	fd_set_close_on_exec(fd);
	fd_set_blocking(fd);

	/*
	 * Should not call slurm_get_addr() because the IP may not be
	 * in /etc/hosts.
	 */
	uc = (unsigned char *)&addr.sin_addr.s_addr;
	port = addr.sin_port;
	debug2("%s: got message connection from %u.%u.%u.%u:%hu %d",
	       __func__, uc[0], uc[1], uc[2], uc[3], ntohs(port), fd);
	fflush(stdout);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
again:
	if (slurm_receive_msg(fd, msg, obj->ops->timeout) != 0) {
		if (errno == EINTR)
			goto again;
		error("%s: slurm_receive_msg[%u.%u.%u.%u]: %m",
		      __func__, uc[0], uc[1], uc[2], uc[3]);
		goto cleanup;
	}

	(*obj->ops->handle_msg)(obj->arg, msg);

cleanup:
	if ((msg->conn_fd >= STDERR_FILENO) && (close(msg->conn_fd) < 0))
		error("%s: close(%d): %m", __func__, msg->conn_fd);
	slurm_free_msg(msg);

	return SLURM_SUCCESS;
}

int eio_signal_shutdown(eio_handle_t *eio)
{
	char c = 1;

	slurm_mutex_lock(&eio->shutdown_mutex);
	eio->shutdown_time = time(NULL);
	slurm_mutex_unlock(&eio->shutdown_mutex);
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

	while ((rc = (read(eio->fds[0], &c, 1)) > 0)) {
		if (c == 1)
			_mark_shutdown_true(eio->obj_list);
	}

	/* move new eio objects from the new_objs to the obj_list */
	list_transfer(eio->obj_list, eio->new_objs);

	if (rc < 0)
		return error("eio_clear: read: %m");

	return 0;
}

int eio_handle_mainloop(eio_handle_t *eio)
{
	int            retval  = 0;
	struct pollfd *pollfds = NULL;
	eio_obj_t    **map     = NULL;
	unsigned int   maxnfds = 0, nfds = 0;
	unsigned int   n       = 0;
	time_t shutdown_time;

	xassert (eio != NULL);
	xassert (eio->magic == EIO_MAGIC);

	while (1) {
		/* Alloc memory for pfds and map if needed */
		n = list_count(eio->obj_list);
		if (maxnfds < n) {
			maxnfds = n;
			xrealloc(pollfds, (maxnfds+1) * sizeof(struct pollfd));
			xrealloc(map, maxnfds * sizeof(eio_obj_t *));
			/*
			 * Note: xrealloc() also handles initial malloc
			 */
		}
		if (!pollfds)  /* Fix for CLANG false positive */
			goto done;

		debug4("eio: handling events for %d objects",
		       list_count(eio->obj_list));
		nfds = _poll_setup_pollfds(pollfds, map, eio->obj_list);
		if (nfds <= 0)
			goto done;

		/*
		 *  Setup eio handle signaling fd
		 */
		pollfds[nfds].fd     = eio->fds[0];
		pollfds[nfds].events = POLLIN;
		nfds++;

		xassert(nfds <= maxnfds + 1);

		/* Get shutdown_time to pass to _poll_internal */
		slurm_mutex_lock(&eio->shutdown_mutex);
		shutdown_time = eio->shutdown_time;
		slurm_mutex_unlock(&eio->shutdown_mutex);
		if (_poll_internal(pollfds, nfds, shutdown_time) < 0)
			goto error;

		/* See if we've been told to shut down by eio_signal_shutdown */
		if (pollfds[nfds-1].revents & POLLIN)
			_eio_wakeup_handler(eio);

		_poll_dispatch(pollfds, nfds - 1, map, eio->obj_list);

		slurm_mutex_lock(&eio->shutdown_mutex);
		shutdown_time = eio->shutdown_time;
		slurm_mutex_unlock(&eio->shutdown_mutex);
		if (shutdown_time &&
		    (difftime(time(NULL), shutdown_time)>=eio->shutdown_wait)) {
			error("%s: Abandoning IO %d secs after job shutdown "
			      "initiated", __func__, eio->shutdown_wait);
			break;
		}
	}
  error:
	retval = -1;
  done:
	xfree(pollfds);
	xfree(map);
	return retval;
}

static int
_poll_internal(struct pollfd *pfds, unsigned int nfds, time_t shutdown_time)
{
	int n, timeout;

	if (shutdown_time)
		timeout = 1000;	/* Return every 1000 msec during shutdown */
	else
		timeout = -1;
	while ((n = poll(pfds, nfds, timeout)) < 0) {
		switch (errno) {
		case EINTR:
			return 0;
		case EAGAIN:
			continue;
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

	if (!pfds) {	/* Fix for CLANG false positive */
		fatal("pollfd data structure is null");
		return nfds;
	}

	while ((obj = list_next(i))) {
		writable = _is_writable(obj);
		readable = _is_readable(obj);
		if (writable && readable) {
			pfds[nfds].fd     = obj->fd;
#ifdef POLLRDHUP
/* Available since Linux 2.6.17 */
			pfds[nfds].events = POLLOUT | POLLIN |
					    POLLHUP | POLLRDHUP;
#else
			pfds[nfds].events = POLLOUT | POLLIN | POLLHUP;
#endif
			map[nfds]         = obj;
			nfds++;
		} else if (readable) {
			pfds[nfds].fd     = obj->fd;
#ifdef POLLRDHUP
/* Available since Linux 2.6.17 */
			pfds[nfds].events = POLLIN | POLLRDHUP;
#else
			pfds[nfds].events = POLLIN;
#endif
			map[nfds]         = obj;
			nfds++;
		} else if (writable) {
			pfds[nfds].fd     = obj->fd;
			pfds[nfds].events = POLLOUT | POLLHUP;
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
		} else if (obj->ops->handle_write) {
			(*obj->ops->handle_write) (obj, objList);
		} else {
			debug("No handler for %s on fd %d",
			      revents & POLLERR ? "POLLERR" : "POLLNVAL",
			      obj->fd);
			obj->shutdown = true;
		}
		return;
	}

	if ((revents & POLLHUP) && ((revents & POLLIN) == 0)) {
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

void eio_obj_destroy(void *arg)
{
	eio_obj_t *obj = (eio_obj_t *)arg;
	if (obj) {
		/* If the obj->fd is still open we need it to be to be
		   sure we get the possible extra output that may be
		   on the port.  see test7.11.
		*/
		/* if (obj->fd != -1) { */
		/* 	close(obj->fd); */
		/* 	obj->fd = -1; */
		/* } */
		xfree(obj->ops);
		xfree(obj);
	}
}


/*
 * Add an eio_obj_t "obj" to an eio_handle_t "eio"'s internal object list.
 *
 * This function can only be used to initialize "eio"'s list before
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

bool eio_remove_obj(eio_obj_t *obj, List objs)
{
	ListIterator i;
	eio_obj_t *obj1;
	bool ret = false;

	xassert(obj != NULL);

	i  = list_iterator_create(objs);
	while ((obj1 = list_next(i))) {
		if (obj1 == obj) {
			list_delete_item(i);
			ret = true;
			break;
		}
	}
	list_iterator_destroy(i);
	return ret;
}
