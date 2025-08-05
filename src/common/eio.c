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

#define _GNU_SOURCE	/* For POLLRDHUP */

#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef POLLRDHUP
#define POLLRDHUP POLLHUP
#endif

#include "src/common/fd.h"
#include "src/common/eio.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/net.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/conn.h"

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
 * outside threads can stick new objects on the new_objs list and
 * the eio thread will move them to the main obj_list the next time
 * it wakes up.
 */
#define EIO_MAGIC 0xe1e10
struct eio_handle_components {
	int  magic;
	int  fds[2];
	pthread_mutex_t shutdown_mutex;
	time_t shutdown_time;
	uint16_t shutdown_wait;
	list_t *obj_list;
	list_t *new_objs;
	list_t *del_objs;
};

typedef struct {
	eio_obj_t **map;
	unsigned int *nfds_ptr;
	struct pollfd *pfds;
} foreach_pollfd_t;

/* Function prototypes */

static int _poll_internal(struct pollfd *pfds, unsigned int nfds,
			  eio_obj_t *map[], time_t shutdown_time);
static unsigned int _poll_setup_pollfds(struct pollfd *pfds, eio_obj_t *map[],
					list_t *l);
static void _poll_dispatch(struct pollfd *pfds, unsigned int nfds,
			   eio_obj_t *map[], list_t *objList,
			   list_t *del_objs);
static void _poll_handle_event(short revents, eio_obj_t *obj, list_t *objList,
			       list_t *del_objs);

eio_handle_t *eio_handle_create(uint16_t shutdown_wait)
{
	eio_handle_t *eio = xmalloc(sizeof(*eio));

	eio->magic = EIO_MAGIC;

	if (pipe2(eio->fds, O_CLOEXEC) < 0) {
		error("%s: pipe: %m", __func__);
		eio_handle_destroy(eio);
		return (NULL);
	}

	fd_set_nonblocking(eio->fds[0]);

	eio->obj_list = list_create(eio_obj_destroy);
	eio->new_objs = list_create(eio_obj_destroy);
	eio->del_objs = list_create(eio_obj_destroy);

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
	FREE_NULL_LIST(eio->del_objs);
	slurm_mutex_destroy(&eio->shutdown_mutex);

	eio->magic = ~EIO_MAGIC;
	xfree(eio);
}

bool eio_message_socket_readable(eio_obj_t *obj)
{
	xassert(obj);
	debug3("%s: shutdown %d fd %d", __func__, obj->shutdown, obj->fd);

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

int eio_message_socket_accept(eio_obj_t *obj, list_t *objs)
{
	void *conn = NULL;
	int fd;
	slurm_addr_t addr;
	slurm_msg_t *msg = NULL;

	debug3("%s: start", __func__);

	xassert(obj);
	xassert(obj->ops->handle_msg);

	while (!(conn = slurm_accept_msg_conn(obj->fd, &addr))) {
		if (errno == EINTR)
			continue;
		if ((errno == EAGAIN) ||
		    (errno == ECONNABORTED) ||
		    (errno == EWOULDBLOCK)) {
			return SLURM_SUCCESS;
		}
		error_in_daemon("Error on msg accept socket: %m");
		if ((errno == EMFILE)  ||
		    (errno == ENFILE)  ||
		    (errno == ENOBUFS) ||
		    (errno == ENOMEM)) {
			return SLURM_SUCCESS;
		}
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	fd = conn_g_get_fd(conn);
	net_set_keep_alive(fd);
	fd_set_blocking(fd);

	debug2("%s: got message connection from %pA %d", __func__, &addr, fd);
	fflush(stdout);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);
again:
	if (slurm_receive_msg(conn, msg, obj->ops->timeout) != 0) {
		if (errno == EINTR)
			goto again;
		error_in_daemon("%s: slurm_receive_msg[%pA]: %m",
				__func__, &addr);
		goto cleanup;
	}

	msg->tls_conn = conn;
	(*obj->ops->handle_msg)(obj->arg, msg);

cleanup:
	/* may be adopted by the handle_msg routine */
	if (msg->tls_conn)
		conn_g_destroy(conn, true);
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
		return error("%s: write; %m", __func__);
	return 0;
}

int eio_signal_wakeup(eio_handle_t *eio)
{
	char c = 0;
	if (write(eio->fds[1], &c, sizeof(char)) != 1)
		return error("%s: write; %m", __func__);
	return 0;
}

static int _mark_shutdown_true(void *x, void *arg)
{
	eio_obj_t *obj = x;

	obj->shutdown = true;
	return 0;
}

static int _eio_wakeup_handler(eio_handle_t *eio)
{
	char c = 0;
	int rc = 0;

	while ((rc = (read(eio->fds[0], &c, 1)) > 0)) {
		if (c == 1)
			list_for_each(eio->obj_list, _mark_shutdown_true, NULL);
	}

	/* move new eio objects from the new_objs to the obj_list */
	list_transfer(eio->obj_list, eio->new_objs);

	if (rc < 0)
		return error("%s: read: %m", __func__);

	return 0;
}

static int _close_eio_socket(void *x, void *key)
{
	eio_obj_t *e = (eio_obj_t *) x;
	time_t *now = (time_t *) key;

	if (difftime(*now, e->close_time) < DEFAULT_EIO_SHUTDOWN_WAIT)
		return 0;

	debug4("%s closing eio->fd: %d", __func__, e->fd);
	close(e->fd);
	e->fd = -1;

	return 1;
}

int eio_handle_mainloop(eio_handle_t *eio)
{
	int            retval  = 0;
	struct pollfd *pollfds = NULL;
	eio_obj_t    **map     = NULL;
	unsigned int   maxnfds = 0, nfds = 0;
	unsigned int   n       = 0;
	time_t shutdown_time, now;

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

		debug4("eio: handling events for %u objects", n);
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

		if (_poll_internal(pollfds, nfds, map, shutdown_time) < 0)
			goto error;

		/* See if we've been told to shut down by eio_signal_shutdown */
		if (pollfds[nfds-1].revents & POLLIN)
			_eio_wakeup_handler(eio);

		_poll_dispatch(pollfds, nfds - 1, map, eio->obj_list,
			       eio->del_objs);

		slurm_mutex_lock(&eio->shutdown_mutex);
		shutdown_time = eio->shutdown_time;
		slurm_mutex_unlock(&eio->shutdown_mutex);
		if (shutdown_time &&
		    (difftime(time(NULL), shutdown_time)>=eio->shutdown_wait)) {
			error("%s: Abandoning IO %d secs after job shutdown initiated",
			      __func__, eio->shutdown_wait);
			break;
		}

		/*
		 * Close and remove all expired eio objects at every wakeup.
		 */
		now = time(NULL);
		list_delete_all(eio->del_objs, _close_eio_socket, &now);
	}

error:
	retval = -1;
done:
	now = 0;
	list_delete_all(eio->del_objs, _close_eio_socket, &now);
	xfree(pollfds);
	xfree(map);
	return retval;
}

static bool _peek_internal(eio_obj_t *map[], unsigned int obj_cnt)
{
	bool data_on_any_conn = false;

	for (int i = 0; i < obj_cnt; i++) {
		eio_obj_t *obj = map[i];

		if (obj->conn && (obj->data_on_conn = conn_g_peek(obj->conn)))
			data_on_any_conn = true;
	}

	return data_on_any_conn;
}

static int _poll_internal(struct pollfd *pfds, unsigned int nfds,
			  eio_obj_t *map[], time_t shutdown_time)
{
	int n, timeout;

	if (shutdown_time)
		timeout = 1000;	/* Return every 1000 msec during shutdown */
	else
		timeout = 60000;

	/*
	 * If there is data to be read on the connection, don't block, simply
	 * read whatever events are already available.
	 */
	if (_peek_internal(map, nfds - 1))
		timeout = 0;

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

static bool _is_writable(eio_obj_t *obj)
{
	return (obj->ops->writable && (*obj->ops->writable)(obj));
}

static bool _is_readable(eio_obj_t *obj)
{
	return (obj->ops->readable && (*obj->ops->readable)(obj));
}

static int _foreach_helper_setup_pollfds(void *x, void *arg)
{
	eio_obj_t *obj = x;
	foreach_pollfd_t *hargs = arg;
	struct pollfd *pfds = hargs->pfds;
	eio_obj_t **map = hargs->map;
	unsigned int nfds = *hargs->nfds_ptr;
	bool readable, writable;

	writable = _is_writable(obj);
	readable = _is_readable(obj);
	if (writable && readable) {
		pfds[nfds].fd     = obj->fd;
		pfds[nfds].events = POLLOUT | POLLIN | POLLHUP | POLLRDHUP;
		map[nfds]         = obj;
	} else if (readable) {
		pfds[nfds].fd     = obj->fd;
		pfds[nfds].events = POLLIN | POLLRDHUP;
		map[nfds]         = obj;
	} else if (writable) {
		pfds[nfds].fd     = obj->fd;
		pfds[nfds].events = POLLOUT | POLLHUP;
		map[nfds]         = obj;
	}

	if (writable || readable)
		(*hargs->nfds_ptr)++;

	return 0;
}

static unsigned int _poll_setup_pollfds(struct pollfd *pfds, eio_obj_t *map[],
					list_t *l)
{
	unsigned int  nfds = 0;
	foreach_pollfd_t args = {
		.pfds = pfds,
		.map = map,
		.nfds_ptr = &nfds
	};

	if (!pfds) {	/* Fix for CLANG false positive */
		fatal("%s: pollfd data structure is null", __func__);
		return nfds;
	}

	list_for_each(l, _foreach_helper_setup_pollfds, &args);

	return nfds;
}

static void _poll_dispatch(struct pollfd *pfds, unsigned int nfds,
			   eio_obj_t *map[], list_t *objList,
			   list_t *del_objs)
{
	int i;

	for (i = 0; i < nfds; i++) {
		if ((pfds[i].revents > 0) || map[i]->data_on_conn)
			_poll_handle_event(pfds[i].revents, map[i], objList,
					   del_objs);
	}
}

static void _poll_handle_event(short revents, eio_obj_t *obj, list_t *objList,
			       list_t *del_objs)
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
		goto end;
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

	if ((revents & POLLIN) || (obj->data_on_conn)) {
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

end:
	if (obj->ops->handle_cleanup) {
		(*obj->ops->handle_cleanup) (obj, objList, del_objs);
	}
}

static struct io_operations *_ops_copy(struct io_operations *ops)
{
	struct io_operations *ret = xmalloc(sizeof(*ops));

	/* Copy initial client_ops */
	*ret = *ops;
	return ret;
}

eio_obj_t *eio_obj_create(int fd, struct io_operations *ops, void *arg)
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
		conn_g_destroy(obj->conn, false);
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

extern bool eio_remove_obj(eio_obj_t *obj, list_t *objs)
{
	xassert(obj != NULL);

	return list_delete_ptr(objs, obj);
}
