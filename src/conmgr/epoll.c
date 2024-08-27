/*****************************************************************************\
 *  epoll.c - Definitions for epoll_*() handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/polling.h"
#include "src/conmgr/events.h"

/*
 * Size event count for 1 input and 1 output per connection and
 * interrupt pipe fd. Allocated once to avoid calling
 * xrecalloc() every time poll() is called.
 */
#define MAX_POLL_EVENTS(max_connections) ((max_connections * 2) + 1)

/* string used for interrupt name in logging to match style of others fds */
#define INTERRUPT_CON_NAME "interrupt"

/*
 * Need an arbitrary sized of bytes to ensure the pipe has been cleared of all
 * bytes in a single read() even though there should only ever be 1 byte.
 */
#define FLUSH_BUFFER_BYTES 100

/* Flags to be used for each type of fd */
#define T(type, events) { type, XSTRINGIFY(type), events, XSTRINGIFY(events) }
static const struct {
	pollctl_fd_type_t type;
	const char *type_string;
	uint32_t events;
	const char *events_string;
} fd_types[] = {
	T(PCTL_TYPE_INVALID, 0),
	T(PCTL_TYPE_UNSUPPORTED, 0),
	T(PCTL_TYPE_NONE, 0),
	T(PCTL_TYPE_CONNECTED, (EPOLLHUP | EPOLLERR | EPOLLET)),
	T(PCTL_TYPE_READ_ONLY, (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR |
				EPOLLET)),
	T(PCTL_TYPE_READ_WRITE,
	  (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET)),
	T(PCTL_TYPE_WRITE_ONLY, (EPOLLOUT | EPOLLHUP | EPOLLERR | EPOLLET)),
	T(PCTL_TYPE_LISTEN, (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLET)),
	T(PCTL_TYPE_INVALID_MAX, 0),
};
#undef T

#define T(flag) { flag, XSTRINGIFY(flag) }
static const struct {
	uint32_t flag;
	const char *string;
} epoll_events[] = {
	T(EPOLLIN),
	T(EPOLLOUT),
	T(EPOLLPRI),
	T(EPOLLERR),
	T(EPOLLHUP),
	T(EPOLLRDHUP),
	T(EPOLLET),
	T(EPOLLONESHOT),
	T(EPOLLWAKEUP),
#ifdef EPOLLEXCLUSIVE
	T(EPOLLEXCLUSIVE),
#endif
};
#undef T

#define PCTL_INITIALIZER \
{ \
	.mutex = PTHREAD_MUTEX_INITIALIZER, \
	.poll_return = EVENT_INITIALIZER("POLL_RETURN"), \
	.interrupt_return = EVENT_INITIALIZER("INTERRUPT_RETURN"), \
	.epoll = -1, \
	.interrupt =  { \
		.send = -1, \
		.receive = 1, \
	}, \
}

static struct pctl_s {
	pthread_mutex_t mutex;

	/* Is currently initialized */
	bool initialized;

	/* event to wait on pollctl_for_each_event() to return */
	event_signal_t poll_return;
	/* event to wait on pollctl_interrupt() to return */
	event_signal_t interrupt_return;

	/* True if actively polling() */
	bool polling;
	/* file descriptor for epoll */
	int epoll;
	/* array holding results of epoll */
	struct epoll_event *events;
	/* number of elements in events array */
	int events_count;
	/*
	 * Number of elements triggred in last epoll_wait().
	 * Only set when polling=true.
	 */
	int events_triggered;
	/* number of file descriptors currently registered */
	int fd_count;

	struct {
		/* pipe() used to break out of epoll() */
		int send;
		int receive;

		/* number of times interrupt requested */
		int requested;

		/* if a thread currently trying to send byte */
		bool sending;
	} interrupt;
} pctl = PCTL_INITIALIZER;

static int _link_fd(int fd, pollctl_fd_type_t type, const char *con_name,
		    const char *caller);
static void _unlink_fd(int fd, const char *con_name, const char *caller);

static const char *_type_to_string(pollctl_fd_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(fd_types); i++)
		if (fd_types[i].type == type)
			return fd_types[i].type_string;

	fatal_abort("should never execute");
}

static char *_epoll_events_to_string(uint32_t events)
{
	char *str = NULL, *at = NULL;
	uint32_t matched = 0;

	if (!events)
		return xstrdup_printf("0");

	for (int i = 0; i < ARRAY_SIZE(epoll_events); i++) {
		if ((epoll_events[i].flag & events) == epoll_events[i].flag) {
			xstrfmtcatat(str, &at, "%s%s", (str ? "|" : ""),
				     epoll_events[i].string);
			matched |= epoll_events[i].flag;
		}
	}

	if (events ^ matched)
		xstrfmtcatat(str, &at, "%s0x%08"PRIx32, (str ? "|" : ""),
			     (events ^ matched));

	return str;
}

static uint32_t _fd_type_to_events(pollctl_fd_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(fd_types); i++)
		if (fd_types[i].type == type)
			return fd_types[i].events;

	fatal_abort("should never happen");
}

static const char *_fd_type_to_type_string(pollctl_fd_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(fd_types); i++)
		if (fd_types[i].type == type)
			return fd_types[i].type_string;

	fatal_abort("should never happen");
}

static const char *_fd_type_to_events_string(pollctl_fd_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(fd_types); i++)
		if (fd_types[i].type == type)
			return fd_types[i].events_string;

	fatal_abort("should never happen");
}

static void _check_pctl_magic(void)
{
#ifndef NDEBUG
	/* check file descriptors are not sane */
	xassert(pctl.initialized);
	xassert(pctl.epoll >= 0);
	xassert(pctl.interrupt.send >= 0);
	xassert(pctl.interrupt.receive >= 0);
	xassert(pctl.epoll != pctl.interrupt.send);
	xassert(pctl.epoll != pctl.interrupt.receive);
	xassert(pctl.interrupt.send != pctl.interrupt.receive);
	xassert(pctl.fd_count >= 0);

	xassert(pctl.interrupt.requested >= 0);
#endif /* !NDEBUG */
}

static void _atfork_child(void)
{
	/*
	 * Force pctl to return to default state before it was initialized at
	 * forking as all of the prior state is completely unusable.
	 */
	pctl = (struct pctl_s) PCTL_INITIALIZER;
}

static void _init(const int max_connections)
{
	int rc;

	slurm_mutex_lock(&pctl.mutex);

	if (pctl.initialized) {
		log_flag(CONMGR, "%s: Skipping. Already initialized", __func__);
		slurm_mutex_unlock(&pctl.mutex);
		return;
	}

	pctl.events_count = MAX_POLL_EVENTS(max_connections);

	if ((rc = pthread_atfork(NULL, NULL, _atfork_child)))
		fatal_abort("%s: pthread_atfork() failed: %s",
			    __func__, slurm_strerror(rc));

	{
		int fd[2] = { -1, -1 };
		if (pipe(fd))
			fatal("%s: unable to open unnamed pipe: %m", __func__);

		fd_set_nonblocking(fd[0]);
		fd_set_close_on_exec(fd[0]);
		pctl.interrupt.receive = fd[0];

		fd_set_blocking(fd[1]);
		fd_set_close_on_exec(fd[1]);
		pctl.interrupt.send = fd[1];
	}

	if ((pctl.epoll = epoll_create1(EPOLL_CLOEXEC)) < 0)
		fatal_abort("%s: epoll_create1(FD_CLOEXEC) failed which should never happen: %m",
			    __func__);

	pctl.events = xcalloc(pctl.events_count, sizeof(*pctl.events));
	pctl.initialized = true;

	_check_pctl_magic();

	if (_link_fd(pctl.interrupt.receive, PCTL_TYPE_READ_ONLY,
		     INTERRUPT_CON_NAME, __func__))
		fatal_abort("unable to monitor interrupt");

	slurm_mutex_unlock(&pctl.mutex);
}

static void _fini(void)
{
	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	if (!pctl.initialized) {
		slurm_mutex_unlock(&pctl.mutex);
		return;
	}

	while (pctl.interrupt.sending)
		EVENT_WAIT(&pctl.interrupt_return, &pctl.mutex);

	while (pctl.polling)
		EVENT_WAIT(&pctl.poll_return, &pctl.mutex);

#ifdef MEMORY_LEAK_DEBUG
	_unlink_fd(pctl.interrupt.receive, INTERRUPT_CON_NAME, __func__);

	fd_close(&pctl.interrupt.receive);
	fd_close(&pctl.interrupt.send);
	fd_close(&pctl.epoll);

	xfree(pctl.events);
	EVENT_FREE_MEMBERS(&pctl.poll_return);
	EVENT_FREE_MEMBERS(&pctl.interrupt_return);

	pctl.initialized = false;
#endif /* MEMORY_LEAK_DEBUG */

	slurm_mutex_unlock(&pctl.mutex);

	/*
	 * lock is never destroyed
	 * slurm_mutex_destroy(&pctl.mutex);
	 */
}

/* caller must hold pctl.mutex lock */
static int _link_fd(int fd, pollctl_fd_type_t type, const char *con_name,
		    const char *caller)
{
	struct epoll_event ev = {
		.events = _fd_type_to_events(type),
		.data.fd = fd,
	};

	if (epoll_ctl(pctl.epoll, EPOLL_CTL_ADD, ev.data.fd, &ev)) {
		int rc = errno;

		log_flag(CONMGR, "%s->%s: [EPOLL:%s] epoll_ctl(EPOLL_CTL_ADD, %d, %s) failed: %s",
			 caller, __func__, con_name, ev.data.fd,
			 _fd_type_to_events_string(type), slurm_strerror(rc));

		return rc;
	} else if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR)
		log_flag(CONMGR, "%s->%s: [EPOLL:%s] registered fd[%s]:%d for %s events",
			 caller, __func__, con_name,
			 _fd_type_to_type_string(type), fd,
			 _fd_type_to_events_string(type));

	pctl.fd_count++;
	return SLURM_SUCCESS;
}

static int _lock_link_fd(int fd, pollctl_fd_type_t type, const char *con_name,
			 const char *caller)
{
	int rc;

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();
	rc = _link_fd(fd, type, con_name, caller);
	slurm_mutex_unlock(&pctl.mutex);

	return rc;
}

static void _relink_fd(int fd, pollctl_fd_type_t type,
			    const char *con_name, const char *caller)
{
	struct epoll_event ev = {
		.events = _fd_type_to_events(type),
		.data.fd = fd,
	};

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	if (epoll_ctl(pctl.epoll, EPOLL_CTL_MOD, ev.data.fd, &ev)) {
		fatal_abort("%s->%s: [EPOLL:%s] epoll_ctl(EPOLL_CTL_MOD, %d, %s) failed: %m",
			    caller, __func__, con_name,
			    ev.data.fd, _fd_type_to_events_string(type));
	} else if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR)
		log_flag(CONMGR, "%s->%s: [EPOLL:%s] Modified fd[%s]:%d for %s events",
			 caller, __func__, con_name,
			 _fd_type_to_type_string(type), fd,
			 _fd_type_to_events_string(type));

	slurm_mutex_unlock(&pctl.mutex);
}

/* caller must hold pctl.mutex */
static void _unlink_fd(int fd, const char *con_name, const char *caller)
{
	_check_pctl_magic();

	if (epoll_ctl(pctl.epoll, EPOLL_CTL_DEL, fd, NULL))
		fatal_abort("%s->%s: [EPOLL:%s] epoll_ctl(EPOLL_CTL_DEL, %d) failed: %m",
			    caller, __func__, con_name, fd);
	else if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR)
		log_flag(CONMGR, "%s->%s: [EPOLL:%s] deregistered fd:%d events",
			 caller, __func__, con_name, fd);

	pctl.fd_count--;
}

static void _lock_unlink_fd(int fd, const char *con_name, const char *caller)
{
	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	_unlink_fd(fd, con_name, caller);

	slurm_mutex_unlock(&pctl.mutex);
}

static void _flush_interrupt(int intr_fd, uint32_t events, const char *caller)
{
	ssize_t event_read = -1;
	char buf[FLUSH_BUFFER_BYTES]; /* buffer for event_read */

	/* clear trash from the interrupt pipe */

	if ((event_read = read(intr_fd, buf, sizeof(buf)) < 0) &&
	    (errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR))
		fatal_abort("this should never happen read(%d)=%m", intr_fd);

	/* only 1 byte should ever get written to pipe at a time */
	xassert(event_read <= 1);

	slurm_mutex_lock(&pctl.mutex);

	log_flag(CONMGR, "%s->%s: [EPOLL:%s] read %zd bytes representing %d pending requests while sending=%c",
		 caller, __func__, INTERRUPT_CON_NAME, event_read,
		 pctl.interrupt.requested,
		 (pctl.interrupt.sending ? 'T' : 'F'));

	/* reset counter */
	pctl.interrupt.requested = 0;

	slurm_mutex_unlock(&pctl.mutex);
}

static int _poll(const char *caller)
{
	int nfds = -1, rc = SLURM_SUCCESS, events_count = 0, epoll = -1;
	int fd_count = 0;
	struct epoll_event *events = NULL;

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	/*
	 * Using pctl.polling as way to avoid touching pctl.events while not
	 * holding the mutex so poll can be done without the lock.
	 */
	xassert(!pctl.polling);
	xassert(!pctl.events_triggered);
	pctl.polling = true;
	events_count = pctl.events_count;
	epoll = pctl.epoll;
	fd_count = pctl.fd_count;
	events = pctl.events;

	log_flag(CONMGR, "%s->%s: [EPOLL] BEGIN: epoll_wait() with %d file descriptors",
		    caller, __func__, pctl.fd_count);

	slurm_mutex_unlock(&pctl.mutex);

	xassert(events_count > 0);

	if (fd_count <= 1) {
		/*
		 * No point in running poll() when only file descriptor is the
		 * interrupt pipe
		 */
		log_flag(CONMGR, "%s->%s: [EPOLL] skipping epoll_wait() with %d file descriptors",
			    caller, __func__, fd_count);
		nfds = 0;
	} else if ((nfds = epoll_wait(epoll, events, events_count, -1)) < 0) {
		rc = errno;
	}

	slurm_mutex_lock(&pctl.mutex);

	xassert(nfds <= pctl.events_count);

	log_flag(CONMGR, "%s->%s: [EPOLL] END: epoll_wait() with events for %d/%d file descriptors",
		   caller, __func__, nfds, pctl.fd_count);

	if (nfds > 0) {
		/* wait for pollctl_for_each_event() to do anything */
		pctl.events_triggered = nfds;
	} else if (!nfds) {
		log_flag(CONMGR, "%s->%s: [EPOLL] END: epoll_wait() reported 0 events for %d file descriptors",
			    caller, __func__, pctl.fd_count);
	} else if (rc == EINTR) {
		/* Treat EINTR as no events detected */
		nfds = 0;
		rc = SLURM_SUCCESS;

		log_flag(CONMGR, "%s->%s: [EPOLL] END: epoll_wait() interrupted by signal",
			    caller, __func__);
	} else {
		fatal_abort("%s->%s: [EPOLL] END: epoll_wait() failed: %m",
			    caller, __func__);
	}

	/* pctl.polling is set to false by pollctl_for_each_event() */
	xassert(pctl.polling);
	slurm_mutex_unlock(&pctl.mutex);

	return rc;
}

static int _for_each_event(pollctl_event_func_t func, void *arg,
			   const char *func_name, const char *caller)
{
	int nfds = -1, rc = SLURM_SUCCESS, intr_fd = -1;
	struct epoll_event *events = NULL;
	event_signal_t *poll_return = NULL;

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	xassert(pctl.polling);

	events = pctl.events;
	nfds = pctl.events_triggered;
	intr_fd = pctl.interrupt.receive;
	slurm_mutex_unlock(&pctl.mutex);

	for (int i = 0; !rc && (i < nfds); ++i) {
		char *events_str = NULL;

		if (events[i].data.fd == intr_fd) {
			_flush_interrupt(intr_fd, events[i].events, caller);
			continue;
		}

		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR)
			events_str = _epoll_events_to_string(events[i].events);

		log_flag(CONMGR, "%s->%s: [EPOLL] BEGIN: calling %s(fd:%d, (%s), 0x%"PRIxPTR")",
			 caller, __func__, func_name, events[i].data.fd,
			 events_str, (uintptr_t) arg);

		rc = func(events[i].data.fd, events[i].events, arg);

		log_flag(CONMGR, "%s->%s: [EPOLL] END: called %s(fd:%d, (%s), 0x%"PRIxPTR")=%s",
			 caller, __func__, func_name, events[i].data.fd,
			 events_str, (uintptr_t) arg, slurm_strerror(rc));

		xfree(events_str);
	}

	slurm_mutex_lock(&pctl.mutex);

	xassert(pctl.polling);
	pctl.polling = false;
	pctl.events_triggered = 0;
	poll_return = &pctl.poll_return;

	EVENT_BROADCAST(poll_return);
	slurm_mutex_unlock(&pctl.mutex);

	return rc;
}

/* send 1 byte without lock */
static int _intr_send_byte(int fd, const char *caller)
{
	DEF_TIMERS;
	char buf[] = "1";

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR)
		START_TIMER;

	/* send 1 byte of trash to wake up poll() */
	safe_write(fd, buf, 1);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		END_TIMER3(NULL, 0);
		log_flag(CONMGR, "%s->%s: [EPOLL] interrupt byte sent in %s",
			 caller, __func__, TIME_STR);
	}

	return SLURM_SUCCESS;
rwfail:
	return errno;
}

static void _interrupt(const char *caller)
{
	event_signal_t *interrupt_return = NULL;
	int rc, fd = -1;

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	if (!pctl.polling) {
		log_flag(CONMGR, "%s->%s: [EPOLL] skipping sending interrupt when not actively poll()ing",
			 caller, __func__);
	} else {
		pctl.interrupt.requested++;

		/* Check for duplicate requests. */
		if (pctl.interrupt.requested == 1) {
			fd = pctl.interrupt.send;
			xassert(!pctl.interrupt.sending);
			pctl.interrupt.sending = true;
			interrupt_return = &pctl.interrupt_return;

			log_flag(CONMGR, "%s->%s: [EPOLL] sending interrupt requests=%d",
				 caller, __func__,
				 pctl.interrupt.requested);
		} else {
			log_flag(CONMGR, "%s->%s: [EPOLL] skipping sending another interrupt requests=%d sending=%c",
				 caller, __func__,
				 pctl.interrupt.requested,
				 (pctl.interrupt.sending ? 'T' : 'F'));
		}
	}

	slurm_mutex_unlock(&pctl.mutex);

	if (fd < 0)
		return;

	if ((rc = _intr_send_byte(fd, caller))) {
		error("%s->%s: [EPOLL] write(%d) failed: %s",
		      caller, __func__, fd, slurm_strerror(errno));
	}

	slurm_mutex_lock(&pctl.mutex);
	_check_pctl_magic();

	log_flag(CONMGR, "%s->%s: [EPOLL] interrupt sent requests=%d polling=%c",
		 caller, __func__, pctl.interrupt.requested,
		 (pctl.polling ? 'T' : 'F'));

	xassert(fd == pctl.interrupt.send);
	xassert(pctl.interrupt.sending);
	pctl.interrupt.sending = false;

	EVENT_BROADCAST(interrupt_return);
	slurm_mutex_unlock(&pctl.mutex);
}

static bool _events_can_read(pollctl_events_t events)
{
	/*
	 * Allow read()/write() to catch EPOLLRDHUP AND EPOLLHUP as there may
	 * still be more bytes the fd's buffers and we don't want to close() the
	 * connection yet either to drop those buffers on the floor.
	 */
	return (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP));
}

static bool _events_can_write(pollctl_events_t events)
{
	return (events & (EPOLLOUT | EPOLLRDHUP | EPOLLHUP));
}

static bool _events_has_error(pollctl_events_t events)
{
	return (events & EPOLLERR);
}

static bool _events_has_hangup(pollctl_events_t events)
{
	return (events & (EPOLLRDHUP | EPOLLHUP));
}

const poll_funcs_t epoll_funcs = {
	.mode = POLL_MODE_EPOLL,
	.init = _init,
	.fini = _fini,
	.type_to_string = _type_to_string,
	.link_fd = _lock_link_fd,
	.relink_fd = _relink_fd,
	.unlink_fd = _lock_unlink_fd,
	.poll = _poll,
	.for_each_event = _for_each_event,
	.interrupt = _interrupt,
	.events_can_read = _events_can_read,
	.events_can_write = _events_can_write,
	.events_has_error = _events_has_error,
	.events_has_hangup = _events_has_hangup,
};
