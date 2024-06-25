/*****************************************************************************\
 *  signal.c - signals for connection manager
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

#define _GNU_SOURCE
#include <dirent.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#define MAX_EPOLL_EVENTS 4

typedef struct {
#define MAGIC_SIGNAL_HANDLER 0xC20A444A
	int magic; /* MAGIC_SIGNAL_HANDLER */
	struct sigaction prior;
	struct sigaction new;
	int signal;
} signal_handler_t;

typedef struct {
#define MAGIC_SIGNAL_WORK 0xA201444A
	int magic; /* MAGIC_SIGNAL_WORK */
	int signal;
	conmgr_work_func_t func;
	void *arg;
	const char *tag;
} signal_work_t;

/* protects all of the static variables here */
pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

/* list of all registered signal handlers */
static signal_handler_t *signal_handlers = NULL;
static int signal_handler_count = 0;

/* list of all registered signal work */
static signal_work_t *signal_work = NULL;
static int signal_work_count = 0;

/* interrupt handler will send signal to this fd */
static int signal_fd_send = -1;
/* _signal_mgr() will monitor this fd for signals */
static int signal_fd_receive = -1;

static void _signal_handler(int signo)
{
	/*
	 * Per the sigaction man page:
	 * 	A child created via fork(2) inherits a copy of its parent's
	 * 	signal dispositions.
	 *
	 * Signal handler registration survives fork() but the signal_mgr()
	 * thread will be lost. Gracefully ignore signals when signal_fd_send is
	 * -1 to avoid trying to write a non-existent file descriptor.
	 */
	if (signal_fd_send < 0)
		return;

try_again:
	if (write(signal_fd_send, &signo, sizeof(signo)) != sizeof(signo)) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			goto try_again;

		/* TODO: replace with signal_safe_fatal() */
		fatal_abort("%s: unable to signal connection manager: %m",
			    __func__);
	}
}

/* caller must hold write lock */
static void _register_signal_handler(int signal)
{
	signal_handler_t *handler;

	for (int i = 0; i < signal_handler_count; i++) {
		xassert(signal_handlers[i].magic == MAGIC_SIGNAL_HANDLER);

		if (signal_handlers[i].signal == signal)
			return;
	}

	xrecalloc(signal_handlers, (signal_handler_count + 1),
		  sizeof(*signal_handlers));

	handler = &signal_handlers[signal_handler_count];
	handler->magic = MAGIC_SIGNAL_HANDLER;
	handler->signal = signal;
	handler->new.sa_handler = _signal_handler;

	if (sigaction(signal, &handler->new, &handler->prior))
		fatal("%s: unable to catch %s: %m",
		      __func__, strsignal(signal));

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char *signame = sig_num2name(handler->signal);

		log_flag(CONMGR, "%s: installed signal %s[%d] handler: Prior=0x%"PRIxPTR" is now replaced with New=0x%"PRIxPTR,
			 __func__, signame, signal,
			 (uintptr_t) handler->prior.sa_handler,
			 (uintptr_t) handler->new.sa_handler);
		xfree(signame);
	}

	signal_handler_count++;
}

/* caller must hold write lock */
static void _init_signal_handler(void)
{
	if (signal_handlers)
		return;

	for (int i = 0; i < signal_work_count; i++) {
		signal_work_t *work = &signal_work[i];
		xassert(work->magic == MAGIC_SIGNAL_WORK);

		_register_signal_handler(work->signal);
	}
}

/* caller must hold write lock */
static void _fini_signal_handler(void)
{
	for (int i = 0; i < signal_handler_count; i++) {
		signal_handler_t *handler = &signal_handlers[i];
		xassert(handler->magic == MAGIC_SIGNAL_HANDLER);

		if (sigaction(handler->signal, &handler->prior, &handler->new))
			fatal("%s: unable to restore %s: %m",
			      __func__, strsignal(handler->signal));

		/*
		 * Check what sigaction() swapped out from the current signal
		 * handler to catch when something else has replaced signal
		 * handler. This is assert exists to help us catch any code that
		 * changes the signal handlers outside of conmgr.
		 */
		xassert(handler->new.sa_handler == _signal_handler);

		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *signame = sig_num2name(handler->signal);

			log_flag(CONMGR, "%s: reverted signal %s[%d] handler: New=0x%"PRIxPTR" is now replaced with Prior=0x%"PRIxPTR,
				 __func__, signame,
				 handler->signal,
				 (uintptr_t) handler->new.sa_handler,
				 (uintptr_t) handler->prior.sa_handler);
			xfree(signame);
		}

		/*
		 * Check what sigaction() swapped out from the current signal
		 * handler to catch when something else has replaced the signal
		 * handler. This assert exists to help us catch any code that
		 * changes the signal handlers outside of conmgr.
		 */
		xassert(handler->new.sa_handler == _signal_handler);
	}

	xfree(signal_handlers);
	signal_handler_count = 0;
}

static void _on_signal(int signal)
{
	bool matched = false;

	for (int i = 0; i < signal_work_count; i++) {
		signal_work_t *work = &signal_work[i];
		xassert(work->magic == MAGIC_SIGNAL_WORK);

		if (work->signal != signal)
			continue;

		matched = true;
		add_work(true, NULL, work->func, CONMGR_WORK_TYPE_FIFO,
			  work->arg, work->tag);
	}

	if (!matched)
		warning("%s: caught and ignoring signal %s",
			__func__, strsignal(signal));
}

static int _read_signal(int fd, const char *con_name)
{
	int sig, rc, readable;

	if ((rc = fd_get_readable_bytes(fd, &readable, con_name)) ||
	    !readable) {
		log_flag(CONMGR, "%s: [%s] no pending bytes to read()",
			 __func__, con_name);
		return -1;
	}

	/*
	 * According to pipe(7), writes less than PIPE_BUF in size must be
	 * atomic. Posix.1 requries PIPE_BUF to be at least 512 bytes.
	 * Therefore, a write() of 4 bytes to a pipe is always atomic in Linux.
	 */
	xassert(readable >= sizeof(sig));

	safe_read(fd, &sig, sizeof(sig));

	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		char *str = sig_num2name(sig);
		log_flag(CONMGR, "%s: [%s] got signal: %s(%d)",
			 __func__, con_name, str, sig);
		xfree(str);
	}

	return sig;
rwfail:
	/* safe_read() should never pass these errors along */
	xassert(errno != EAGAIN);
	xassert(errno != EWOULDBLOCK);

	/* this should never happen! */
	fatal_abort("%s: Unexpected safe_read(%d) failure: %m", __func__, fd);
}

static void _on_signal_readable(int fd)
{
	static const char *con_name = "signal_fd_send";
	int sig = 0, count = 0;

	while ((sig = _read_signal(fd, con_name)) > 0) {
		count++;
		_on_signal(sig);
	}

	log_flag(CONMGR, "%s: caught %d signals", __func__, count);
}

extern void conmgr_add_signal_work(int signal, conmgr_work_func_t func,
				   void *arg, const char *tag)
{
	slurm_rwlock_wrlock(&lock);

	xrecalloc(signal_work, (signal_work_count + 1), sizeof(*signal_work));

	signal_work[signal_work_count] = (signal_work_t){
		.magic = MAGIC_SIGNAL_WORK,
		.signal = signal,
		.func = func,
		.arg = arg,
		.tag = tag,
	};

	signal_work_count++;

	/* register new signal handler since mgr thread already running */
	if (signal_fd_send >= 0)
		_register_signal_handler(signal);

	slurm_rwlock_unlock(&lock);
}

static void _signal_mgr(conmgr_fd_t *con, conmgr_work_type_t type,
			conmgr_work_status_t status, const char *tag, void *arg)
{
	/*
	 * socket_pair() used to relay signals. Socket provides an easy way to
	 * break out of epoll via shutdown() without another pipe or possibly
	 * losing a signal.
	 */
	int send = -1;
	int receive = 1;
	struct epoll_event events[MAX_EPOLL_EVENTS];
	int epoll = -1;
	bool shutdown = false;

	if (status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	/*
	 * locks cant be used in signal context but this should atleast flush
	 * any memory barriers
	 */
	slurm_rwlock_wrlock(&lock);

	_init_signal_handler();

	if (signal_fd_send != -1)
		fatal_abort("there can be only 1 thread of this function");

	{
		int fd[2] = { -1, -1 };

		if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fd))
			fatal_abort("%s: socketpair() failed: %m", __func__);

		fd_set_blocking(fd[0]);
		signal_fd_receive = receive = fd[0];
		fd_set_blocking(fd[1]);
		signal_fd_send = send = fd[1];
	}

	slurm_rwlock_unlock(&lock);

	if ((epoll = epoll_create1(EPOLL_CLOEXEC)) < 0)
		fatal_abort("epoll_create1() failed: %m");

	{
		struct epoll_event ev = {
			.events = (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR),
			.data.fd = receive,
		};

		if (epoll_ctl(epoll, EPOLL_CTL_ADD, ev.data.fd, &ev))
			fatal_abort("epoll_ctl() failed: %m");
	}

	while (!shutdown) {
		int nfds = epoll_wait(epoll, events, ARRAY_SIZE(events), -1);

		if (nfds < 0) {
			if (errno != EINTR)
				fatal_abort(
					"should never happen: epoll_wait()=%m");
			nfds = 0;
		}

		/* should only ever have 1 or 0 events here */
		xassert(nfds == 1 || !nfds);

		for (int i = 0; i < nfds; i++) {
			xassert(events[i].data.fd == receive);

			if (events[i].data.fd != receive)
				continue;

			if (events[i].events & EPOLLERR)
				fatal_abort("should never happen");

			if (events[i].events & (EPOLLHUP|EPOLLRDHUP)) {
				shutdown = true;

				log_flag(CONMGR, "%s: HANGUP received. Shutting down gracefully.",
					 __func__);
			}

			if (events[i].events & EPOLLIN)
				_on_signal_readable(receive);
		}
	}

	slurm_rwlock_wrlock(&lock);
	_fini_signal_handler();

	xassert(signal_fd_send == send);
	xassert((signal_fd_receive == receive) || (signal_fd_receive == -1));
	signal_fd_send = -1;
	signal_fd_receive = -1;
	slurm_rwlock_unlock(&lock);

	fd_close(&send);
	fd_close(&receive);
	fd_close(&epoll);
}

extern void signal_mgr_start(bool locked)
{
	bool start;

	slurm_rwlock_wrlock(&lock);
	start = (signal_fd_send < 0);
	slurm_rwlock_unlock(&lock);

	if (start)
		add_work(locked, NULL, _signal_mgr, CONMGR_WORK_TYPE_FIFO, NULL,
			 XSTRINGIFY(_signal_mgr));
}

extern void signal_mgr_stop(void)
{
	/* Not signal safe but should still work enough to fail gracefully */
	slurm_rwlock_rdlock(&lock);
	if (signal_fd_receive >= 0) {
		int rc;

		if ((rc = shutdown(signal_fd_receive, SHUT_RDWR)))
			rc = errno;

		log_flag(CONMGR, "%s: shutdown(signal_fd_receive=%d)=%s",
			 __func__, signal_fd_receive, slurm_strerror(rc));

		signal_fd_receive = -1;
	}
	slurm_rwlock_unlock(&lock);
}
