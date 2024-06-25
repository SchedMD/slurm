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
#include <signal.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

static void _signal_handler(int signo)
{
	/*
	 * Per the sigaction man page:
	 * 	A child created via fork(2) inherits a copy of its parent's
	 * 	signal dispositions.
	 *
	 * Signal handler registration survives fork() but mgr will get reset to
	 * CONMGR_DEFAULT. Gracefully ignore signals when mgr.signal_fd is
	 * -1 to avoid trying to write a non-existant file descriptor.
	 */
	if (mgr.signal_fd[1] < 0)
		return;

try_again:
	if (write(mgr.signal_fd[1], &signo, sizeof(signo)) != sizeof(signo)) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			goto try_again;

		log_reinit();
		fatal("%s: unable to signal connection manager: %m", __func__);
	}
}

static void _register_signal_handler(int signal)
{
	signal_handler_t *handler;

	for (int i = 0; i < mgr.signal_handler_count; i++) {
		xassert(mgr.signal_handlers[i].magic == MAGIC_SIGNAL_HANDLER);

		if (mgr.signal_handlers[i].signal == signal)
			return;
	}

	xrecalloc(mgr.signal_handlers, (mgr.signal_handler_count + 1),
		  sizeof(*mgr.signal_handlers));

	handler = &mgr.signal_handlers[mgr.signal_handler_count];
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

	mgr.signal_handler_count++;
}

extern void init_signal_handler(void)
{
	if (mgr.signal_handlers)
		return;

	for (int i = 0; i < mgr.signal_work_count; i++) {
		signal_work_t *work = &mgr.signal_work[i];
		xassert(work->magic == MAGIC_SIGNAL_WORK);

		_register_signal_handler(work->signal);
	}
}

extern void fini_signal_handler(void)
{
	for (int i = 0; i < mgr.signal_handler_count; i++) {
		signal_handler_t *handler = &mgr.signal_handlers[i];
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

	xfree(mgr.signal_handlers);
	mgr.signal_handler_count = 0;
}

/*
 * Notify connection manager that there has been a change event
 */
extern void signal_change(bool locked)
{
	DEF_TIMERS;
	char buf[] = "1";
	int rc;

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	if (mgr.event_signaled) {
		mgr.event_signaled++;
		log_flag(CONMGR, "%s: sent %d times",
			 __func__, mgr.event_signaled);
		goto done;
	} else {
		log_flag(CONMGR, "%s: sending", __func__);
		mgr.event_signaled = 1;
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);

try_again:
	START_TIMER;
	/* send 1 byte of trash */
	rc = write(mgr.event_fd[1], buf, 1);
	END_TIMER2("write to event_fd");
	if (rc != 1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			log_flag(CONMGR, "%s: trying again: %m", __func__);
			goto try_again;
		}

		fatal("%s: unable to signal connection manager: %m", __func__);
	}

	log_flag(CONMGR, "%s: sent in %s", __func__, TIME_STR);

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

done:
	/* wake up _watch() */
	slurm_cond_broadcast(&mgr.cond);

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

static void _on_signal(int signal)
{
	bool matched = false;

	for (int i = 0; i < mgr.signal_work_count; i++) {
		signal_work_t *work = &mgr.signal_work[i];
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

extern void handle_signals(void *ptr)
{
	int sig, count = 0, fd = -1;
	static const char *con_name = "mgr.signal_fd[0]";

	xassert(ptr == NULL);

	slurm_mutex_lock(&mgr.mutex);
	fd = mgr.signal_fd[0];
	slurm_mutex_unlock(&mgr.mutex);
	xassert(fd >= 0);

	while (true) {
		int readable = 0;
		int rc;

		while ((sig = _read_signal(fd, con_name)) > 0) {
			count++;
			_on_signal(sig);
		}

		log_flag(CONMGR, "%s: caught %d signals", __func__, count);

		slurm_mutex_lock(&mgr.mutex);

		xassert(mgr.signaled);
		xassert(mgr.read_signals_active);
		xassert(fd == mgr.signal_fd[0]);

		/*
		 * Catch if another signal has been caught while the existing
		 * backlog of signals in the pipe were being processed.
		 */
		rc = fd_get_readable_bytes(mgr.signal_fd[0], &readable,
					   con_name);

		if (!rc && (readable > 0)) {
			slurm_mutex_unlock(&mgr.mutex);
			/* reset signal counter */
			count = 0;
			/* try again as there was a signal sent */
			continue;
		}

		/* Remove signal flags as all signals have read */
		mgr.signaled = false;
		mgr.read_signals_active = false;

		/* wake up _watch_loop() */
		slurm_cond_broadcast(&mgr.cond);
		slurm_mutex_unlock(&mgr.mutex);
		break;
	}
}

extern void add_signal_work(int signal, conmgr_work_func_t func, void *arg,
			    const char *tag)
{
	xrecalloc(mgr.signal_work, (mgr.signal_work_count + 1),
		  sizeof(*mgr.signal_work));

	mgr.signal_work[mgr.signal_work_count] = (signal_work_t){
		.magic = MAGIC_SIGNAL_WORK,
		.signal = signal,
		.func = func,
		.arg = arg,
		.tag = tag,
	};

	mgr.signal_work_count++;

	/* Call sigaction() if needed */
	if (mgr.watching)
		_register_signal_handler(signal);
}

extern void conmgr_add_signal_work(int signal, conmgr_work_func_t func,
				   void *arg, const char *tag)
{
	slurm_mutex_lock(&mgr.mutex);

	if (mgr.shutdown_requested) {
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	if (mgr.watching)
		fatal_abort("signal work must be added before conmgr is run");

	add_signal_work(signal, func, arg, tag);
	slurm_mutex_unlock(&mgr.mutex);
}
