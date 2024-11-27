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

#include <signal.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#define SIGNAL_FD_FAILED -250

typedef struct {
#define MAGIC_SIGNAL_HANDLER 0xC20A444A
	int magic; /* MAGIC_SIGNAL_HANDLER */
	struct sigaction prior;
	struct sigaction new;
	int signal;
} signal_handler_t;

/* protects all of the static variables here */
pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

/* protected by lock */
static bool one_time_init = false;

/* list of all registered signal handlers */
static signal_handler_t *signal_handlers = NULL;
static int signal_handler_count = 0;

/* list of all registered signal work */
static work_t **signal_work = NULL;
static int signal_work_count = 0;

/* interrupt handler (_signal_handler()) will send signal to this fd */
static volatile sig_atomic_t signal_fd = -1;
static conmgr_fd_t *signal_con = NULL;

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
	if (signal_fd < 0)
		return;

try_again:
	if (write(signal_fd, &signo, sizeof(signo)) != sizeof(signo)) {
		if ((errno == EPIPE) || (errno == EBADF)) {
			/*
			 * write() after conmgr shutdown before reading that
			 * signal_fd was closed. Ignoring this race condition
			 * entirely. Set signal_fd to an invalid value that is
			 * not -1 to distinguish it from the normal "unset"
			 * state.
			 */
			signal_fd = SIGNAL_FD_FAILED;
			return;
		}

		if (errno == EINTR)
			goto try_again;

		/*
		 * Drop signal as the buffer is already full which means
		 * something bad has already happened and having the exact
		 * signal numbers isn't going to make much difference.
		 */
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return;

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
		work_t *work = signal_work[i];
		xassert(work->magic == MAGIC_WORK);

		_register_signal_handler(work->control.on_signal_number);
	}
}

/* mgr.mutex should be locked */
static void _on_signal(int signal)
{
	bool matched = false;

	slurm_rwlock_rdlock(&lock);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char *str = sig_num2name(signal);
		log_flag(CONMGR, "%s: [%s] got signal: %s(%d)",
			 __func__, signal_con->name, str, signal);
		xfree(str);
	}

	for (int i = 0; i < signal_work_count; i++) {
		work_t *work = signal_work[i];

		xassert(work->magic == MAGIC_WORK);

		if (work->control.on_signal_number != signal)
			continue;

		matched = true;
		add_work(true, NULL, work->callback, work->control,
			 ~CONMGR_WORK_DEP_SIGNAL, __func__);
	}

	slurm_rwlock_unlock(&lock);

	if (!matched)
		warning("%s: caught and ignoring signal %s",
			__func__, strsignal(signal));
}

extern void add_work_signal(work_t *work)
{
	xassert(!work->con);
	xassert(work->control.depend_type & CONMGR_WORK_DEP_SIGNAL);
	xassert(work->control.on_signal_number > 0);

	slurm_rwlock_wrlock(&lock);

	xrecalloc(signal_work, (signal_work_count + 1), sizeof(*signal_work));

	signal_work[signal_work_count] = work;
	signal_work_count++;

	/*
	 * Directly register new signal handler since connection already started
	 * and init_signal_handler() already ran
	 */
	if (signal_con)
		_register_signal_handler(work->control.on_signal_number);

	slurm_rwlock_unlock(&lock);
}

static void *_on_connection(conmgr_fd_t *con, void *arg)
{
	slurm_rwlock_wrlock(&lock);

	_init_signal_handler();
	signal_con = con;

	slurm_rwlock_unlock(&lock);

	return con;
}

static int _on_data(conmgr_fd_t *con, void *arg)
{
	const void *data = NULL;
	size_t bytes = 0, read = 0;
	int signo;

	xassert(arg == con);

	conmgr_fd_get_in_buffer(con, &data, &bytes);

	slurm_mutex_lock(&mgr.mutex);
	while ((read + sizeof(signo)) <= bytes) {
		signo = *(int *) (data + read);

		_on_signal(signo);

		read += sizeof(signo);
	}
	slurm_mutex_unlock(&mgr.mutex);

	conmgr_fd_mark_consumed_in_buffer(con, read);

	return SLURM_SUCCESS;
}

static void _on_finish(conmgr_fd_t *con, void *arg)
{
	int fd;

	xassert(arg == con);

	slurm_rwlock_wrlock(&lock);

	fd = signal_fd;
	signal_fd = -1;

	xassert(fd != -1);
	fd_close(&fd);

	xassert(con == signal_con);
	xassert(signal_con);
	signal_con = NULL;

	slurm_rwlock_unlock(&lock);
}

static void _atfork_child(void)
{
	/*
	 * Force state to return to default state before it was initialized at
	 * forking as all of the prior state is completely unusable.
	 */
	lock = (pthread_rwlock_t) PTHREAD_RWLOCK_INITIALIZER;
	one_time_init = false;
	signal_handlers = NULL;
	signal_handler_count = 0;
	signal_work = NULL;
	signal_work_count = 0;
	signal_fd = -1;
	signal_con = NULL;
}

extern void signal_mgr_start(conmgr_callback_args_t conmgr_args, void *arg)
{
	static const conmgr_events_t events = {
		.on_connection = _on_connection,
		.on_data = _on_data,
		.on_finish = _on_finish,
	};
	int fd[2] = { -1, -1 };
	int rc;

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	slurm_rwlock_wrlock(&lock);

	if (signal_fd >= 0) {
		slurm_rwlock_unlock(&lock);
		log_flag(CONMGR, "%s: skipping - already initialized",
			 __func__);
		return;
	}

	if (pipe(fd))
		fatal_abort("%s: pipe() failed: %m", __func__);

	if (!one_time_init) {
		if ((rc = pthread_atfork(NULL, NULL, _atfork_child)))
			fatal_abort("%s: pthread_atfork() failed: %s",
				    __func__, slurm_strerror(rc));
		one_time_init = true;
	}

	xassert(signal_fd == -1);
	xassert(!signal_con);

	fd_set_close_on_exec(fd[0]);
	fd_set_close_on_exec(fd[1]);

	fd_set_nonblocking(fd[1]);
	signal_fd = fd[1];

	slurm_rwlock_unlock(&lock);

	if (add_connection(CON_TYPE_RAW, NULL, fd[0], -1, &events,
			    CON_FLAG_NONE, NULL, 0, false, NULL, NULL)) {
		fatal_abort("%s: [fd:%d] unable to a register new connection",
			    __func__, fd[0]);
	}
}

extern void signal_mgr_stop(void)
{
	slurm_rwlock_rdlock(&lock);

	if (signal_con)
		close_con(true, signal_con);

	slurm_rwlock_unlock(&lock);
}

extern bool is_signal_connection(conmgr_fd_t *con)
{
	bool match;

	slurm_rwlock_rdlock(&lock);
	match = (signal_con == con);
	slurm_rwlock_unlock(&lock);

	return match;
}

extern bool signal_mgr_has_incoming(void)
{
	bool has_data = false;

	slurm_rwlock_rdlock(&lock);

	if (signal_con) {
		if (signal_con->input_fd >= 0) {
			int readable = -1;

			/*
			 * Force (blocking) check of signal_fd since poll() may
			 * have not run yet but need to make sure to catch any
			 * pending data if possible.
			 *
			 * Ignore failure of FIONREAD here as we only care if
			 * there is pending data
			 */
			(void) fd_get_readable_bytes(signal_con->input_fd,
						     &readable,
						     signal_con->name);

			if (readable > 0)
				has_data = true;
		}

		if (!has_data)
			has_data =
				con_flag(signal_con, FLAG_CAN_READ) ||
				(signal_con->in &&
				 get_buf_offset(signal_con->in)) ||
				(signal_con->work &&
				 !list_is_empty(signal_con->work)) ||
				(signal_con->write_complete_work &&
				 !list_is_empty(signal_con->write_complete_work));
	}

	slurm_rwlock_unlock(&lock);

	return has_data;
}
