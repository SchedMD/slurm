/*****************************************************************************\
 **  pmix_agent.c - PMIx agent thread
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Y. Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <pthread.h>
#include <sched.h>
#include <poll.h>
#include <arpa/inet.h>
#include <time.h>

#include "pmixp_common.h"
#include "pmixp_server.h"
#include "pmixp_client.h"
#include "pmixp_state.h"
#include "pmixp_debug.h"
#include "pmixp_nspaces.h"
#include "pmixp_utils.h"
#include "pmixp_dconn.h"

static volatile bool _agent_is_running = false;
static volatile bool _timer_is_running = false;
static pthread_mutex_t _flag_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _run_flag_set(volatile bool *flag, bool val)
{
	slurm_mutex_lock(&_flag_mutex);
	*flag = val;
	slurm_mutex_unlock(&_flag_mutex);
}

static bool _run_flag_get(volatile bool *flag)
{
	bool rc;
	slurm_mutex_lock(&_flag_mutex);
	rc = *flag;
	slurm_mutex_unlock(&_flag_mutex);
	return rc;
}

static eio_handle_t *_io_handle = NULL;

static int _agent_spawned = 0, _timer_spawned = 0;
static pthread_t _agent_tid = 0;
static pthread_t _timer_tid = 0;

struct timer_data_t {
	int initialized;
	int work_in, work_out;
	int stop_in, stop_out;
};
static struct timer_data_t timer_data;

static bool _conn_readable(eio_obj_t *obj);
static int _server_conn_read(eio_obj_t *obj, List objs);
static int _timer_conn_read(eio_obj_t *obj, List objs);
static struct io_operations srv_ops = {
	.readable = &_conn_readable,
	.handle_read = &_server_conn_read
};

static struct io_operations to_ops = {
	.readable = &_conn_readable,
	.handle_read = &_timer_conn_read
};

static bool _conn_readable(eio_obj_t *obj)
{
	PMIXP_DEBUG("fd = %d", obj->fd);
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			close(obj->fd);
			obj->fd = -1;
		}
		PMIXP_DEBUG("    false, shutdown");
		return false;
	}
	return true;
}

static int _server_conn_read(eio_obj_t *obj, List objs)
{
	int fd;
	struct sockaddr addr;
	socklen_t size = sizeof(addr);
	int shutdown = 0;

	PMIXP_DEBUG("fd = %d", obj->fd);

	while (1) {
		/* Return early if fd is not now ready */
		if (!pmixp_fd_read_ready(obj->fd, &shutdown)) {
			if (shutdown) {
				obj->shutdown = true;
				if (shutdown < 0) {
					PMIXP_ERROR_NO(shutdown,
						       "sd=%d failure",
						       obj->fd);
				}
			}
			return 0;
		}

		while ((fd = accept(obj->fd, &addr, &size)) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) /* No more connections */
				return 0;
			if ((errno == ECONNABORTED) || (errno == EWOULDBLOCK)) {
				return 0;
			}
			PMIXP_ERROR_STD("accept()ing connection sd=%d",
					obj->fd);
			return 0;
		}

		if (pmixp_info_srv_usock_fd() == obj->fd) {
			PMIXP_DEBUG("SLURM PROTO: accepted connection: sd=%d",
				    fd);
			/* read command from socket and handle it */
			pmixp_server_slurm_conn(fd);
		} else if (pmixp_dconn_poll_fd() == obj->fd) {
			PMIXP_DEBUG("DIRECT PROTO: accepted connection: sd=%d",
				    fd);
			/* read command from socket and handle it */
			pmixp_server_direct_conn(fd);

		}
	}
	return 0;
}

static int _timer_conn_read(eio_obj_t *obj, List objs)
{
	char *tmpbuf[32];
	int shutdown;
	PMIXP_DEBUG("Timeout thread, fd = %d", obj->fd);

	/* drain everything from in fd */
	while (32 == pmixp_read_buf(obj->fd, tmpbuf, 32, &shutdown, false))
		;
	if (shutdown) {
		PMIXP_ERROR("readin from timer fd, shouldn't happen");
		obj->shutdown = true;
	}

	/* check direct modex requests */
	pmixp_dmdx_timeout_cleanup();

	/* check collective statuses */
	pmixp_state_coll_cleanup();

	/* cleanup server structures */
	pmixp_server_cleanup();

	return 0;
}

static void _shutdown_timeout_fds(void);

#define SETUP_FDS(fds) { \
	fd_set_nonblocking(fds[0]);	\
	fd_set_close_on_exec(fds[0]);	\
	fd_set_nonblocking(fds[1]);	\
	fd_set_close_on_exec(fds[1]);	\
	}

static int _setup_timeout_fds(void)
{
	int fds[2];

	timer_data.work_in = timer_data.work_out = -1;
	timer_data.stop_in = timer_data.stop_out = -1;

	if (pipe(fds)) {
		return SLURM_ERROR;
	}
	SETUP_FDS(fds);
	timer_data.work_in = fds[0];
	timer_data.work_out = fds[1];

	if (pipe(fds)) {
		_shutdown_timeout_fds();
		return SLURM_ERROR;
	}
	SETUP_FDS(fds);
	timer_data.stop_in = fds[0];
	timer_data.stop_out = fds[1];

	timer_data.initialized = 1;

	return SLURM_SUCCESS;
}

static void _shutdown_timeout_fds(void)
{
	if (0 <= timer_data.work_in) {
		close(timer_data.work_in);
		timer_data.work_in = -1;
	}
	if (0 <= timer_data.work_out) {
		close(timer_data.work_out);
		timer_data.work_out = -1;
	}
	if (0 <= timer_data.stop_in) {
		close(timer_data.stop_in);
		timer_data.stop_in = -1;
	}
	if (0 <= timer_data.stop_out) {
		close(timer_data.stop_out);
		timer_data.stop_out = -1;
	}
}

/*
 * main loop of agent thread
 */
static void *_agent_thread(void *unused)
{
	PMIXP_DEBUG("Start agent thread");
	eio_obj_t *obj;
	int preval;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &preval);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &preval);

	_io_handle = eio_handle_create(0);

	obj = eio_obj_create(pmixp_info_srv_usock_fd(), &srv_ops,
			     (void *)(-1));
	eio_new_initial_obj(_io_handle, obj);

	obj = eio_obj_create(timer_data.work_in, &to_ops, (void *)(-1));
	eio_new_initial_obj(_io_handle, obj);

	pmixp_info_io_set(_io_handle);

	if (PMIXP_DCONN_PROGRESS_SW == pmixp_dconn_progress_type()) {
		obj = eio_obj_create(pmixp_dconn_poll_fd(), &srv_ops,
				     (void *)(-1));
		eio_new_initial_obj(_io_handle, obj);
	} else {
		pmixp_dconn_regio(_io_handle);
	}

	_run_flag_set(&_agent_is_running, true);

	eio_handle_mainloop(_io_handle);

	PMIXP_DEBUG("agent thread exit");
	eio_handle_destroy(_io_handle);

	_run_flag_set(&_agent_is_running, false);

	return NULL;
}

static void *_pmix_timer_thread(void *unused)
{
	struct pollfd pfds[1];
	int preval;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &preval);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &preval);

	PMIXP_DEBUG("Start timer thread");

	pfds[0].fd = timer_data.stop_in;
	pfds[0].events = POLLIN;

	_run_flag_set(&_timer_is_running, true);

	/* our job is to sleep 1 sec and then trigger
	 * the timer event in the main loop */

	while (1) {
		/* during normal operation there should be no
		 * activity on the stop fd.
		 * So normally we need to exit by the timeout.
		 * This forses periodic timer events (once each second) */
		int ret = poll(pfds, 1, 1000);
		char c = 1;
		if (0 < ret) {
			/* there was an event on stop_fd, exit */
			break;
		}
		/* activate main thread's timer event */
		write(timer_data.work_out, &c, 1);
	}

	_run_flag_set(&_timer_is_running, false);

	return NULL;
}

int pmixp_agent_start(void)
{
	_setup_timeout_fds();

	/* start agent thread */
	slurm_thread_create_detached(&_agent_tid, _agent_thread, NULL);
	_agent_spawned = 1;

	/* wait for the agent thread to initialize */
	while (!_run_flag_get(&_agent_is_running)) {
		sched_yield();
	}

	/* Check if a ping-pong run was requested by user
	 * NOTE: enabled only if `--enable-debug` configuration
	 * option was passed
	 */
	if (pmixp_server_want_pp()) {
		pmixp_server_run_pp();
	}

	/* Check if a collective test was requested by user
	 * NOTE: enabled only if `--enable-debug` configuration
	 * option was passed
	 */
	if (pmixp_server_want_cperf()) {
		pmixp_server_run_cperf();
	}

	PMIXP_DEBUG("agent thread started: tid = %lu",
		    (unsigned long) _agent_tid);

	slurm_thread_create_detached(&_timer_tid, _pmix_timer_thread, NULL);
	_timer_spawned = 1;

	/* wait for the agent thread to initialize */
	while (!_run_flag_get(&_timer_is_running)) {
		sched_yield();
	}

	PMIXP_DEBUG("timer thread started: tid = %lu",
		    (unsigned long) _timer_tid);

	return SLURM_SUCCESS;
}

int pmixp_agent_stop(void)
{
	char c = 1;
	if (_run_flag_get(&_agent_is_running)) {
		eio_signal_shutdown(_io_handle);
		/* wait for the agent thread to stop */
		while (_run_flag_get(&_agent_is_running)) {
			sched_yield();
		}
	}
	if (_agent_spawned) {
		pthread_cancel(_agent_tid);
	}

	if (timer_data.initialized) {
		/* cancel timer */
		write(timer_data.stop_out, &c, 1);
		while (_run_flag_get(&_timer_is_running) ) {
			sched_yield();
		}
		/* close timer fds */
		_shutdown_timeout_fds();
	}

	if (_timer_spawned) {
		pthread_cancel(_timer_tid);
	}
	return SLURM_SUCCESS;
}
