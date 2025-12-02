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

#include "src/common/events.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/probes.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
	/* pthread_self() from thread */
	pthread_t id;
	bool detached;
	/* thread waiting for assignment */
	pthread_t requester;
	threadpool_func_t func;
	timespec_t requested;
	const char *thread_name;
	const char *func_name;
	void *arg;
	/* return from func() */
	void *ret;
} thread_t;

#define LOG_THREAD_MAGIC 0xabbb1119

typedef struct {
	int magic; /* LOG_THREAD_MAGIC */
	const char *type; /* zombie/pending */
	probe_log_t *log;
} log_thread_t;

static struct {
	/* true if enabled, false to ignore everything else */
	bool enabled;
	/* true if shutdown requested */
	bool shutdown;
	/* list_t * of thread_t * */
	list_t *pending;
	/* list_t * of thread_t * */
	list_t *zombies;
	/* Number of running threads */
	int running;
	/* Number of idle threads */
	int idle;
	/* counter of the threads run */
	uint64_t total_run;
	/* counter of the threads created */
	uint64_t total_created;
	/* peak thead count encountered */
	uint64_t peak_count;

	pthread_mutex_t mutex;

	struct {
		event_signal_t assign;
		event_signal_t assigned;
		event_signal_t assigned_ack;
		event_signal_t end;
		event_signal_t zombie;
	} events;

	struct {
		/* histogram of the latency from request to run */
		latency_histogram_t request;
		/* histogram of the time to run func() in threads */
		latency_histogram_t run;
		/* histogram of the latency to join threads */
		latency_histogram_t join;
	} histograms;

	struct {
		int preallocate;
		int preserve;
	} config;
} threadpool = {
	.enabled = false,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.events = {
		.assign = EVENT_INITIALIZER("THREADPOOL-ASSIGN-THREAD"),
		.assigned = EVENT_INITIALIZER("THREADPOOL-ASSIGNED-THREAD"),
		.assigned_ack = EVENT_INITIALIZER("THREADPOOL-ASSIGNED-ACK-THREAD"),
		.end = EVENT_INITIALIZER("THREADPOOL-END-THREAD"),
		.zombie = EVENT_INITIALIZER("THREADPOOL-ZOMBIE-THREAD"),
	},
	.histograms = {
		.request = LATENCY_HISTOGRAM_INITIALIZER,
		.run = LATENCY_HISTOGRAM_INITIALIZER,
		.join = LATENCY_HISTOGRAM_INITIALIZER,
	},
	.config = {
		.preallocate = THREADPOOL_DEFAULT_PREALLOCATE,
		.preserve = THREADPOOL_DEFAULT_PRESERVE,
	},
};

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(threadpool_create, slurm_threadpool_create);
strong_alias(threadpool_join, slurm_threadpool_join);
strong_alias(threadpool_init, slurm_threadpool_init);
strong_alias(threadpool_fini, slurm_threadpool_fini);

static int _join(const pthread_t id, const char *caller)
{
	int rc = EINVAL;
	void *ret = 0;

	if ((rc = pthread_join(id, &ret))) {
		error("%s->%s: pthread_join(id=0x%"PRIx64") failed: %s",
		      caller, __func__, (uint64_t) id, slurm_strerror(rc));
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

static int _match_thread_id(void *x, void *key)
{
	thread_t *thread = x;
	pthread_t id = (pthread_t) key;

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->id > 0);
	xassert(id > 0);

	return ((thread->id == id) ? 1 : 0);
}

static int _threadpool_join(const pthread_t id, const char *caller)
{
	thread_t *thread = NULL;
	const timespec_t start_ts = timespec_now();
	int rc = EINVAL;

	slurm_mutex_lock(&threadpool.mutex);

	do {
		if ((thread = list_remove_first(threadpool.zombies,
						_match_thread_id, (void *) id)))
			break;

		if (!threadpool.running)
			break;

		log_flag(THREAD, "%s->%s: waiting for thread id=0x%"PRIx64" with %d running threads",
			       caller, __func__, (uint64_t) id,
			       threadpool.running);
		EVENT_WAIT(&threadpool.events.zombie, &threadpool.mutex);
	} while (true);

	if (thread) {
		log_flag(THREAD, "%s->%s: joined pthread id=0x%"PRIx64" returned: 0x%"PRIxPTR,
			       caller, __func__, (uint64_t) thread->id,
			       (uintptr_t) thread->ret);

		xassert(!thread->detached);
		thread->detached = true;
		EVENT_BROADCAST(&threadpool.events.zombie);

		HISTOGRAM_ADD_DURATION(&threadpool.histograms.join, start_ts);
		rc = SLURM_SUCCESS;
	} else {
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" not found",
			       caller, __func__, (uint64_t) id);
		rc = ESRCH;
	}

	slurm_mutex_unlock(&threadpool.mutex);

	return rc;
}

extern int threadpool_join(const pthread_t id, const char *caller)
{
	if (!id) {
		log_flag(THREAD, "%s->%s: Ignoring invalid pthread id=0x0",
		       caller, __func__);
		return SLURM_SUCCESS;
	}

	if (threadpool.enabled)
		return _threadpool_join(id, caller);
	else
		return _join(id, caller);
}

static void _set_thread_name(const char *name)
{
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL))
		error("%s: cannot set process name to %s %m", __func__, name);
#endif
}

static void _thread_free(thread_t *thread)
{
	if (!thread)
		return;

	xassert(thread->magic == THREAD_MAGIC);

	thread->magic = ~THREAD_MAGIC;
	xfree(thread->thread_name);
	xfree(thread);
}

static void _run(thread_t *thread)
{
	const timespec_t start = timespec_now();

	xassert(thread->magic == THREAD_MAGIC);

	if (thread->thread_name)
		_set_thread_name(thread->thread_name);

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";
		timespec_diff_ns_t diff =
			timespec_diff_ns(start, thread->requested);

		(void) timespec_ctime(diff.diff, false, ts, sizeof(ts));

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: %s thread calling %s(0x%"PRIxPTR") after %s",
			 __func__,
			 (thread->thread_name ? thread->thread_name : DEFAULT_THREAD_NAME),
			 (uint64_t) thread->id,
			 (thread->detached ? "detached" : "attached"),
			 thread->func_name, (uintptr_t) thread->arg, ts);
	}

	if (threadpool.enabled)
		HISTOGRAM_ADD_DURATION(&threadpool.histograms.request,
				       thread->requested);

	thread->ret = thread->func(thread->arg);

	if (threadpool.enabled)
		HISTOGRAM_ADD_DURATION(&threadpool.histograms.run, start);

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";
		timespec_diff_ns_t diff =
			timespec_diff_ns(timespec_now(), start);

		(void) timespec_ctime(diff.diff, false, ts, sizeof(ts));

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: %s thread called %s(0x%"PRIxPTR")=0x%"PRIxPTR" for %s",
			 __func__,
			 (thread->thread_name ? thread->thread_name : DEFAULT_THREAD_NAME),
			 (uint64_t) thread->id,
			 (thread->detached ? "detached" : "attached"),
			 thread->func_name,
			 (uintptr_t) thread->arg, (uintptr_t) thread->ret, ts);
	}
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_wait_ack(thread_t *thread)
{
	timespec_t start_ts = { 0, 0 };
	pthread_t requester = thread->requester;

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		start_ts = timespec_now();
		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: waiting for requester 0x%"PRIx64" to acknowledge assignment",
			 __func__, (thread->thread_name ?  thread->thread_name :
				    DEFAULT_THREAD_NAME),
			 (uint64_t) thread->id, (uint64_t) requester);
	}

	while (thread->requester)
		EVENT_WAIT(&threadpool.events.assigned_ack, &threadpool.mutex);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_THREAD) && start_ts.tv_sec) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";
		timespec_diff_ns_t diff =
			timespec_diff_ns(timespec_now(), start_ts);

		(void) timespec_ctime(diff.diff, false, ts, sizeof(ts));

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: acknowledged by requester 0x%"PRIx64" after %s",
			 __func__,
			 (thread->thread_name ?  thread->thread_name :
			  DEFAULT_THREAD_NAME), (uint64_t) thread->id,
			 (uint64_t) requester, ts);
	}
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_zombie(thread_t *thread)
{
	timespec_t start_ts = { 0, 0 };

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		start_ts = timespec_now();
		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: waiting to be joined",
			 __func__, (thread->thread_name ?  thread->thread_name :
				    DEFAULT_THREAD_NAME),
			 (uint64_t) thread->id);
	}

	list_append(threadpool.zombies, thread);

	while (!thread->detached) {
		EVENT_BROADCAST(&threadpool.events.zombie);
		EVENT_WAIT(&threadpool.events.zombie, &threadpool.mutex);
	}

	if ((slurm_conf.debug_flags & DEBUG_FLAG_THREAD) && start_ts.tv_sec) {
		char ts[CTIME_STR_LEN] = "UNKNOWN";
		timespec_diff_ns_t diff =
			timespec_diff_ns(timespec_now(), start_ts);

		(void) timespec_ctime(diff.diff, false, ts, sizeof(ts));

		log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: joined after waiting %s",
			 __func__,
			 (thread->thread_name ?  thread->thread_name :
			  DEFAULT_THREAD_NAME), (uint64_t) thread->id, ts);
	}

	/* join thread should have removed ptr from zombie list */
	xassert(!list_delete_ptr(threadpool.zombies, thread));
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_prerun(thread_t *thread)
{
	xassert(thread->magic == THREAD_MAGIC);

	threadpool.total_run++;

	xassert(!thread->id);
	thread->id = pthread_self();

	threadpool.idle--;
	threadpool.running++;

	if ((threadpool.idle + threadpool.running) > threadpool.peak_count)
		threadpool.peak_count = (threadpool.idle + threadpool.running);

	xassert(threadpool.idle >= 0);
	xassert(threadpool.running > 0);

	EVENT_BROADCAST(&threadpool.events.assigned);
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_postrun(thread_t *thread)
{
	threadpool.running--;
	threadpool.idle++;

	xassert(threadpool.idle > 0);
	xassert(threadpool.running >= 0);

	if (thread->requester)
		_threadpool_wait_ack(thread);

	if (!thread->detached)
		_threadpool_zombie(thread);

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->detached);
	xassert(!thread->requester);
}

static void *_thread(void *arg)
{
	thread_t *thread = arg;

	if (!threadpool.enabled) {
		void *ret = NULL;

		xassert(thread->magic == THREAD_MAGIC);

		_run(thread);

		ret = thread->ret;
		_thread_free(thread);

		return ret;
	}

	slurm_mutex_lock(&threadpool.mutex);

	xassert(!thread || (thread->magic == THREAD_MAGIC));

	threadpool.idle++;

#ifndef NDEBUG
	if ((threadpool.running + threadpool.idle) > THREADPOOL_MAX_THREADS)
		warning("%s: threadpool is over capacity %d/%d",
			__func__, (threadpool.running + threadpool.idle),
			THREADPOOL_MAX_THREADS);
#endif

	do {
		if (thread) {
			_threadpool_prerun(thread);

			slurm_mutex_unlock(&threadpool.mutex);
			_run(thread);
			slurm_mutex_lock(&threadpool.mutex);

			_threadpool_postrun(thread);

			slurm_mutex_unlock(&threadpool.mutex);
			_thread_free(thread);
			thread = NULL;
			slurm_mutex_lock(&threadpool.mutex);
		}

		if ((thread = list_pop(threadpool.pending))) {
			xassert(thread->magic == THREAD_MAGIC);
			xassert(!thread->id);
			continue;
		}

		if (threadpool.shutdown) {
			log_flag(THREAD, "%s: [0x%"PRIx64"] exiting due to shutdown",
				 __func__, (uint64_t) pthread_self());
			break;
		}

		if (threadpool.idle > threadpool.config.preserve) {
			log_flag(THREAD, "%s: [0x%"PRIx64"] exiting due to %d/%d idle threads",
				 __func__, (uint64_t) pthread_self(),
				 threadpool.idle, threadpool.config.preserve);
			break;
		}

		log_flag(THREAD, "%s: [0x%"PRIx64"] waiting for pending thread work with %d/%d idle threads",
			 __func__, (uint64_t) pthread_self(), threadpool.idle,
			 threadpool.config.preserve);

		xassert(list_is_empty(threadpool.pending));
		xassert(threadpool.idle > 0);
		EVENT_WAIT(&threadpool.events.assign, &threadpool.mutex);
	} while (true);

	threadpool.idle--;
	EVENT_BROADCAST(&threadpool.events.end);
	slurm_mutex_unlock(&threadpool.mutex);

	return NULL;
}

static void _free_attr(pthread_attr_t *attr)
{
	int rc = EINVAL;

	if ((rc = pthread_attr_destroy(attr)))
		fatal("%s: pthread_attr_destroy failed: %s",
		      __func__, slurm_strerror(rc));
}

static int _new_thread(thread_t *thread, pthread_t *id_ptr, const char *caller)
{
	pthread_t id = 0;
	pthread_attr_t attr;
	int rc = EINVAL;

	xassert((!thread && threadpool.enabled) ||
		(thread->magic == THREAD_MAGIC));

	if ((rc = pthread_attr_init(&attr)))
		fatal("%s->%s: pthread_attr_init() failed: %s",
		      caller, __func__, slurm_strerror(rc));

#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if ((rc = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)))
		fatal("%s->%s: pthread_attr_setscope(PTHREAD_SCOPE_SYSTEM) failed: %s",
		      caller, __func__, slurm_strerror(rc));
#endif

	if ((rc = pthread_attr_setstacksize(&attr, STACK_SIZE)))
		fatal("%s->%s: pthread_attr_setstacksize(%u) failed: %s",
		      caller, __func__, STACK_SIZE, slurm_strerror(rc));

	if (id_ptr)
		*id_ptr = 0;

	/* All threadpool threads are always detached */
	if ((threadpool.enabled || thread->detached) &&
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
		xassert(threadpool.enabled || thread->func_name);
		log_flag(THREAD, "%s->%s: pthread_create() created new %spthread id=0x%"PRIx64" for %s%s",
			 caller, __func__,
			 (threadpool.enabled ? "" :
			  (thread->detached ? "detached " : "attached ")),
			 (uint64_t) id,
			 (threadpool.enabled ? "threadpool" :
			  thread->func_name), (threadpool.enabled ? "" : "()"));

		if (threadpool.enabled) {
			slurm_mutex_lock(&threadpool.mutex);
			threadpool.total_created++;
			slurm_mutex_unlock(&threadpool.mutex);
		}
	}

	_free_attr(&attr);

	if (id_ptr)
		*id_ptr = id;

	return rc;
}

/*
 * True if there is at least 1 thread ready to run.
 * Caller must hold threadpool.mutex lock.
 */
static bool _thread_available(void)
{
	const int pending = list_count(threadpool.pending);
	const int idle = threadpool.idle;
	const int zombies = list_count(threadpool.zombies);

	/*
	 * The number of idle threads not stuck as zombies must be greater than
	 * the current count of pending thread requested to have at least one
	 * thread available to run.
	 */
	return ((idle - zombies) > pending);
}

static bool _assign(thread_t *thread, pthread_t *id_ptr, const char *caller)
{
	slurm_mutex_lock(&threadpool.mutex);

	xassert(thread->magic == THREAD_MAGIC);
	xassert(!thread->requester);

	if (!_thread_available()) {
		log_flag(THREAD, "%s->%s: zero available idle threads for %s()",
			 caller, __func__, thread->func_name);
		slurm_mutex_unlock(&threadpool.mutex);
		return false;
	}

	if (id_ptr) {
		/*
		 * Only assign requester to have the thread signal upon
		 * assignment but to skip waiting otherwise
		 */
		thread->requester = pthread_self();
	}

	list_append(threadpool.pending, thread);

	if (!id_ptr) {
		/*
		 * No need to wait for assignment from pending list after waking
		 * up an idle thread to accept the work
		 */
		EVENT_SIGNAL(&threadpool.events.assign);
		slurm_mutex_unlock(&threadpool.mutex);
		return true;
	}

	/*
	 * Need to wait for thread assignment if the thread ID pointer needs to
	 * be populated
	 */
	while (!thread->id) {
		xassert(thread->magic == THREAD_MAGIC);
		xassert(thread->requester == pthread_self());
		xassert(threadpool.idle > 0);

		EVENT_SIGNAL(&threadpool.events.assign);
		EVENT_WAIT(&threadpool.events.assigned, &threadpool.mutex);
	}

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->requester == pthread_self());
	xassert(thread->id);

	thread->requester = 0;
	*id_ptr = thread->id;

	log_flag(THREAD, "%s->%s: assigned pthread id=0x%"PRIx64" for %s()",
		 caller, __func__, (uint64_t) thread->id,
		 thread->func_name);

	EVENT_BROADCAST(&threadpool.events.assigned_ack);

	/* thread should have removed ptr from pending list */
	xassert(!list_delete_ptr(threadpool.pending, thread));

	slurm_mutex_unlock(&threadpool.mutex);
	return true;
}

extern int threadpool_create(threadpool_func_t func, const char *func_name,
			     void *arg, const bool detached,
			     const char *thread_name, pthread_t *id_ptr,
			     const char *caller)
{
	thread_t *thread = xmalloc(sizeof(*thread));

#ifndef NDEBUG
	if (thread_name && strlen(thread_name) >= PRCTL_BUF_BYTES)
		warning("%s: Thread name truncated[%zu/%zu]: %s",
			caller, (uint64_t) strlen(thread_name),
			(uint64_t) PRCTL_BUF_BYTES, thread_name);
#endif

	*thread = (thread_t) {
		.magic = THREAD_MAGIC,
		.detached = detached,
		.thread_name = xstrdup(thread_name),
		.func = func,
		.func_name = func_name,
		.arg = arg,
		.requested = timespec_now(),
	};

	if (threadpool.enabled && _assign(thread, id_ptr, caller))
		return SLURM_SUCCESS;

	return _new_thread(thread, id_ptr, caller);
}

static void _parse_params(const int default_count, const char *params)
{
	char *tmp_str = NULL, *tok = NULL, *saveptr = NULL;

	if (default_count > 0)
		threadpool.config.preallocate = default_count;

	if (!params)
		return;

	tmp_str = xstrdup(params);
	tok = strtok_r(tmp_str, ",", &saveptr);
	while (tok) {
		if (!xstrncasecmp(tok, THREADPOOL_PARAM,
				  strlen(THREADPOOL_PARAM))) {
			const char *value = (tok + strlen(THREADPOOL_PARAM));

			if (!xstrcasecmp(value, "enabled"))
				; /* ignore as enabled by default */
			else if (!xstrcasecmp(value, "disabled"))
				threadpool.shutdown = true;
			else
				fatal("Invalid parameter %s", tok);

			log_flag(THREAD, "%s: threadpool is %s",
				 __func__, (threadpool.shutdown ? "disabled" :
					    "enabled"));
		} else if (
			!xstrncasecmp(tok, THREADPOOL_PARAM_PREALLOCATE,
				      strlen(THREADPOOL_PARAM_PREALLOCATE))) {
			const unsigned long count = slurm_atoul(
				tok + strlen(THREADPOOL_PARAM_PREALLOCATE));

			if (count > THREADPOOL_MAX_THREADS)
				fatal("%s: invalid parameter %s",
				      __func__, tok);

			threadpool.config.preallocate = count;

			log_flag(THREAD, "%s: preallocate %lu threads",
				 __func__, count);
		} else if (!xstrncasecmp(tok, THREADPOOL_PARAM_PRESERVE,
					 strlen(THREADPOOL_PARAM_PRESERVE))) {
			const unsigned long count =
				slurm_atoul(tok +
					    strlen(THREADPOOL_PARAM_PRESERVE));

			if (count > THREADPOOL_MAX_THREADS)
				fatal("%s: invalid parameter %s",
				      __func__, tok);

			threadpool.config.preserve = count;

			log_flag(THREAD, "%s: preserve %lu threads",
				 __func__, count);
		} else {
			log_flag(THREAD, "%s: threadpool ignoring parameter %s",
				 __func__, tok);
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	xfree(tmp_str);
}

static int _log_thread(void *x, void *arg)
{
	log_thread_t *logt = arg;
	thread_t *thread = x;

	xassert(logt->magic == LOG_THREAD_MAGIC);
	xassert(thread->magic == THREAD_MAGIC);

	probe_log(logt->log, "thread[%s@0x%"PRIx64"]: func=%s(0x%"PRIxPTR") type=%s detached=%c requester=0x%"PRIx64" ret=0x%"PRIxPTR,
		  thread->thread_name, (uint64_t) thread->id,
		  thread->func_name, (uintptr_t) thread->arg, logt->type,
		  BOOL_CHARIFY(thread->detached), (uint64_t) thread->requester,
		  (uintptr_t) thread->ret);

	return 0;
}

/* Caller must hold threadpool.mutex lock */
static void _probe_verbose(probe_log_t *log)
{
	log_thread_t logt = {
		.magic = LOG_THREAD_MAGIC,
		.log = log,
	};
	char histogram[LATENCY_METRIC_HISTOGRAM_STR_LEN] = { 0 };

	probe_log(log, "config: preallocate:%d preserve:%d",
		  threadpool.config.preallocate, threadpool.config.preserve);

	probe_log(
		log,
		"state: shutdown:%c pending:%d zombies:%d running:%d idle:%d total_run:%" PRIu64 " total_created:%" PRIu64 " peak_count:%" PRIu64,
		BOOL_CHARIFY(threadpool.shutdown),
		list_count(threadpool.pending), list_count(threadpool.zombies),
		threadpool.running, threadpool.idle, threadpool.total_run,
		threadpool.total_created, threadpool.peak_count);

	logt.type = "pending";
	(void) list_for_each_ro(threadpool.pending, _log_thread, &logt);
	logt.type = "zombie";
	(void) list_for_each_ro(threadpool.zombies, _log_thread, &logt);

	(void) latency_histogram_print_labels(histogram, sizeof(histogram));
	probe_log(log, "histogram: %s", histogram);

	(void) latency_histogram_print(&threadpool.histograms.request,
				       histogram, sizeof(histogram));
	probe_log(log, "request histogram: %s", histogram);

	(void) latency_histogram_print(&threadpool.histograms.run, histogram,
				       sizeof(histogram));
	probe_log(log, "run histogram: %s", histogram);

	(void) latency_histogram_print(&threadpool.histograms.join, histogram,
				       sizeof(histogram));
	probe_log(log, "join histogram: %s", histogram);
}

static probe_status_t _probe(probe_log_t *log)
{
	probe_status_t status = PROBE_RC_UNKNOWN;

	slurm_mutex_lock(&threadpool.mutex);

	if (log)
		_probe_verbose(log);

	if (!threadpool.enabled)
		status = PROBE_RC_UNKNOWN;
	else if (threadpool.shutdown)
		status = PROBE_RC_ONLINE;
	else if ((threadpool.running + threadpool.idle) >
		 THREADPOOL_MAX_THREADS)
		status = PROBE_RC_BUSY;
	else
		status = PROBE_RC_READY;

	slurm_mutex_unlock(&threadpool.mutex);

	return status;
}

extern void threadpool_init(const int default_count, const char *params)
{
	int preallocate = -1;

	_parse_params(default_count, params);

	if (threadpool.enabled || threadpool.shutdown)
		return;

	slurm_mutex_lock(&threadpool.mutex);

	xassert(!threadpool.shutdown);

	threadpool.enabled = true;

	xassert(!threadpool.pending);
	threadpool.pending = list_create((ListDelF) _thread_free);

	xassert(!threadpool.zombies);
	threadpool.zombies = list_create((ListDelF) _thread_free);

	preallocate = threadpool.config.preallocate;

	slurm_mutex_unlock(&threadpool.mutex);

	probe_register("threadpool", _probe);

	xassert(preallocate >= 0);

	for (int i = 0; i < preallocate; i++)
		_new_thread(NULL, NULL, __func__);

	log_flag(THREAD, "%s: started with %d threads preallocated",
		 __func__, preallocate);
}

extern void threadpool_fini(void)
{
	if (!threadpool.enabled)
		return;

	/*
	 * Never change threadpool.enabled to false to avoid race conditions of
	 * checking if threadpool was ever enabled
	 */
	slurm_mutex_lock(&threadpool.mutex);

	threadpool.shutdown = true;

	slurm_mutex_unlock(&threadpool.mutex);
}
