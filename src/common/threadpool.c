/*****************************************************************************\
 *  threadpool.c - Thread Pool
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

#include "config.h"

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <pthread.h>
#include <stdint.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/xmalloc.h"

/*
 * From man prctl:
 *	If the length of the  string, including the terminating null byte,
 *	exceeds 16 bytes, the string is silently truncated.
 */
#define PRCTL_BUF_BYTES 17
/* default thread name for logging */
#define DEFAULT_THREAD_NAME "thread"
#define CTIME_STR_LEN 72

#ifdef PTHREAD_SCOPE_SYSTEM
#define slurm_attr_init(attr) \
	do { \
		int err = pthread_attr_init(attr); \
		if (err) { \
			errno = err; \
			fatal("pthread_attr_init: %m"); \
		} \
		/* we want 1:1 threads if there is a choice */ \
		err = pthread_attr_setscope(attr, PTHREAD_SCOPE_SYSTEM); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setscope: %m"); \
		} \
		err = pthread_attr_setstacksize(attr, STACK_SIZE); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setstacksize: %m"); \
		} \
	} while (0)
#else
#define slurm_attr_init(attr) \
	do { \
		int err = pthread_attr_init(attr); \
		if (err) { \
			errno = err; \
			fatal("pthread_attr_init: %m"); \
		} \
		err = pthread_attr_setstacksize(attr, STACK_SIZE); \
		if (err) { \
			errno = err; \
			error("pthread_attr_setstacksize: %m"); \
		} \
	} while (0)
#endif

#define THREAD_MAGIC 0xA434F4D2

typedef struct {
	int magic; /* THREAD_MAGIC  */
	bool detached;
	threadpool_func_t func;
	timespec_t requested;
	const char *thread_name;
	const char *func_name;
	void *arg;
} thread_t;

extern int threadpool_join(const pthread_t id, const char *caller)
{
	int rc = EINVAL;
	void *ret = 0;

	if (!id) {
		log_flag(THREAD, "%s->%s: Ignoring invalid pthread id=0x0",
		       caller, __func__);
		return SLURM_SUCCESS;
	}

	if ((rc = pthread_join(id, &ret))) {
		error("%s->%s: pthread_join(id=0x%"PRIx64") failed: %m",
		      caller, __func__, (uint64_t) id);
		return rc;
	}

	if (ret == PTHREAD_CANCELED)
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" was cancelled",
		       caller, __func__, (uint64_t) id);
	else
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" returned: 0x%"PRIxPTR,
		       caller, __func__, (uint64_t) id, (uintptr_t) ret);

	return rc;
}

static void _set_thread_name(const char *name)
{
#if HAVE_SYS_PRCTL_H
	xassert(strlen(name) < PRCTL_BUF_BYTES);

	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL))
		error("%s: cannot set process name to %s %m", __func__, name);
#endif
}

static void _thread_free(thread_t *thread)
{
#ifdef MEMORY_LEAK_DEBUG
	xassert(thread);
	xassert(thread->magic == THREAD_MAGIC);

	thread->magic = ~THREAD_MAGIC;
	xfree(thread);
#endif
}

static void *_thread(void *arg)
{
	thread_t *thread = arg;
	void *ret = NULL;
	timespec_t start = { 0 }, end = { 0 };

	xassert(thread->magic == THREAD_MAGIC);

	if (thread->thread_name)
		_set_thread_name(thread->thread_name);

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";

		start = timespec_now();
		if (thread->requested.tv_sec) {
			timespec_diff_ns_t diff =
				timespec_diff_ns(start, thread->requested);
			timespec_ctime(diff.diff, false, ts, sizeof(ts));
		}

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: %s thread calling %s(0x%"PRIxPTR") after %s",
			 __func__,
			 (thread->thread_name ? thread->thread_name : DEFAULT_THREAD_NAME),
			 (uint64_t) pthread_self(),
			 (thread->detached ? "detached" : "attached"),
			 thread->func_name, (uintptr_t) thread->arg, ts);
	}

	ret = thread->func(thread->arg);

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";

		end = timespec_now();
		if (start.tv_sec) {
			timespec_diff_ns_t diff = timespec_diff_ns(end, start);
			timespec_ctime(diff.diff, false, ts, sizeof(ts));
		}

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: %s thread called %s(0x%"PRIxPTR")=0x%"PRIxPTR" for %s",
			 __func__,
			 (thread->thread_name ? thread->thread_name : DEFAULT_THREAD_NAME),
			 (uint64_t) pthread_self(),
			 (thread->detached ? "detached" : "attached"),
			 thread->func_name,
			 (uintptr_t) thread->arg, (uintptr_t) ret, ts);
	}

	_thread_free(thread);

	return ret;
}

static void _free_attr(pthread_attr_t *attr)
{
	int rc = EINVAL;

	if ((rc = pthread_attr_destroy(attr)))
		fatal("%s: pthread_attr_destroy failed: %s",
		      __func__, slurm_strerror(rc));
}

extern int threadpool_create(threadpool_func_t func, const char *func_name,
			     void *arg, const bool detached,
			     const char *thread_name, pthread_t *id_ptr,
			     const char *caller)
{
	pthread_t id = 0;
	pthread_attr_t attr;
	int rc = EINVAL;
	thread_t *thread = xmalloc(sizeof(*thread));

	*thread = (thread_t) {
		.magic = THREAD_MAGIC,
		.detached = detached,
		.thread_name = thread_name,
		.func = func,
		.func_name = func_name,
		.arg = arg,
	};

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD)
		thread->requested = timespec_now();

	slurm_attr_init(&attr);

	if (id_ptr)
		*id_ptr = 0;

	if (detached &&
	    (rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
		fatal("%s->%s: pthread_attr_setdetachstate failed: %s",
		      caller, __func__, slurm_strerror(rc));

	/* Pass ownership of thread to _thread() on success */
	rc = pthread_create(&id, &attr, _thread, thread);

	if (rc) {
		error("%s->%s: pthread_create() failed: %s",
		      caller, __func__, slurm_strerror(rc));
		_thread_free(thread);
	} else {
		log_flag(THREAD, "%s->%s: pthread_create() created new %s pthread id=0x%"PRIx64" for %s()",
			 caller, __func__, (detached ? "detached" : "attached"),
			 (uint64_t) id, func_name);
	}

	_free_attr(&attr);

	if (id_ptr)
		*id_ptr = id;

	return rc;
}
