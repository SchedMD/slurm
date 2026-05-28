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
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * From man prctl:
 *	If the length of the  string, including the terminating null byte,
 *	exceeds 16 bytes, the string is silently truncated.
 */
#define PRCTL_BUF_BYTES 17
/* default thread name for logging */
#define CTIME_STR_LEN 72
/*
 * Avoid compiler warnings for id not fitting in PRCTL_BUF_BYTES by using %hu
 * and uint16_t instead of %d. This risks the index rolling over on longer lived
 * daemons.
 */
#define THREAD_NAME_FMT "worker[%hu]"

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

typedef enum {
	THREAD_STATE_INVALID = 0,
	THREAD_STATE_NEW,
	THREAD_STATE_CLONING,
	/* pending assignment of hardware thread */
	THREAD_STATE_PENDING,
	THREAD_STATE_ASSIGNED,
	/* Waiting for requester to acknowledge assignment */
	THREAD_STATE_ASSIGNED_WAIT,
	/* Requester acknowledged assignment */
	THREAD_STATE_ASSIGNED_ACK,
	THREAD_STATE_RUNNING,
	/* Waiting for join() */
	THREAD_STATE_ZOMBIE,
	THREAD_STATE_COMPLETE,
	THREAD_STATE_INVALID_MAX
} thread_state_t;

#define T(x, str) [x] = { x, str }

static const struct {
	thread_state_t state;
	const char *str;
} thread_states[] = {
	T(THREAD_STATE_NEW, "NEW"),
	T(THREAD_STATE_CLONING, "CLONING"),
	T(THREAD_STATE_PENDING, "PENDING"),
	T(THREAD_STATE_ASSIGNED, "ASSIGNED"),
	T(THREAD_STATE_ASSIGNED_WAIT, "ASSIGNED_WAIT"),
	T(THREAD_STATE_ASSIGNED_ACK, "ASSIGNED_ACK"),
	T(THREAD_STATE_RUNNING, "RUNNING"),
	T(THREAD_STATE_ZOMBIE, "ZOMBIE"),
	T(THREAD_STATE_COMPLETE, "COMPLETE"),
};

#define THREAD_MAGIC 0xA434F4D2

typedef struct {
	int magic; /* THREAD_MAGIC  */
	thread_state_t state;
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
	/* Count of threads currently in THREAD_STATE_ZOMBIE */
	int zombies;
	/* list_t * of thread_t * */
	list_t *attached;
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
		event_signal_t detach;
		event_signal_t end;
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
		.detach = EVENT_INITIALIZER("THREADPOOL-DETACH-THREAD"),
		.end = EVENT_INITIALIZER("THREADPOOL-END-THREAD"),
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

static const char *_thread_state_str(const thread_state_t state)
{
	xassert(state > THREAD_STATE_INVALID);
	xassert(state < THREAD_STATE_INVALID_MAX);

	return thread_states[state].str;
}

static int _match_thread_id(void *x, void *key)
{
	const thread_t *thread = x;
	const pthread_t *id_ptr = key;

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->state > THREAD_STATE_INVALID);
	xassert(thread->state < THREAD_STATE_INVALID_MAX);
	xassert(*id_ptr > 0);

	return (pthread_equal(thread->id, *id_ptr) ? 1 : 0);
}

#ifndef NDEBUG

static int _match_thread_ptr(void *x, void *y)
{
	thread_t *thread = x;
	thread_t *key = y;

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->state > THREAD_STATE_INVALID);
	xassert(thread->state < THREAD_STATE_INVALID_MAX);
	xassert(key->magic == THREAD_MAGIC);

	return ((thread == key) ? 1 : 0);
}

#endif

static int _join(const pthread_t id, const char *caller)
{
	int rc = EINVAL;
	void *ret = NULL;

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

static int _threadpool_on_detach(thread_t *thread, const char *caller)
{
	log_flag(THREAD, "%s->%s: detached pthread id=0x%"PRIx64,
		       caller, __func__, (uint64_t) thread->id);

	if (!list_delete_ptr(threadpool.attached, thread))
		fatal_abort("this should never happen");

	if (thread->state == THREAD_STATE_ZOMBIE) {
		xassert(threadpool.zombies > 0);
		threadpool.zombies--;
	}

	xassert(!thread->detached);
	thread->detached = true;

	EVENT_BROADCAST(&threadpool.events.detach);

	return SLURM_SUCCESS;
}

static int _threadpool_join(const pthread_t id, const char *caller)
{
	thread_t *thread = NULL;
	const timespec_t start_ts = timespec_now();

	slurm_mutex_lock(&threadpool.mutex);

	while ((thread = list_find_first(threadpool.attached, _match_thread_id,
					 (void *) &id))) {
		xassert(thread->magic == THREAD_MAGIC);
		xassert(thread->state > THREAD_STATE_NEW);
		xassert(thread->state < THREAD_STATE_COMPLETE);
		xassert(!thread->detached);

		if (thread->state > THREAD_STATE_RUNNING) {
			const int rc = _threadpool_on_detach(thread, caller);

			log_flag(THREAD, "%s->%s: joined %s thread id=0x%"PRIx64" returned: 0x%"PRIxPTR,
				 caller, __func__,
				 _thread_state_str(thread->state),
				 (uint64_t) thread->id,
				 (uintptr_t) thread->ret);

			slurm_mutex_unlock(&threadpool.mutex);

			HISTOGRAM_ADD_DURATION(&threadpool.histograms.join,
					       start_ts);
			return rc;
		}

		log_flag(THREAD, "%s->%s: waiting for %s thread id=0x%"PRIx64" with %d running threads after %s",
			       caller, __func__,
			       _thread_state_str(thread->state), (uint64_t) id,
			       threadpool.running,
			       TIMESPEC_ELAPSED_STR(start_ts));

		EVENT_WAIT(&threadpool.events.detach, &threadpool.mutex);
	}

	log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" does not exist after %s",
		 caller, __func__, (uint64_t) id,
		 TIMESPEC_ELAPSED_STR(start_ts));
	slurm_mutex_unlock(&threadpool.mutex);

	return ESRCH;
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

static const char *_thread_default_name(void)
{
	static __thread char name[PRCTL_BUF_BYTES] = { 0 };
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static int thread_count = 0;
	uint16_t index = -1;

	if (name[0])
		return name;

	slurm_mutex_lock(&mutex);
	index = thread_count++;
	slurm_mutex_unlock(&mutex);

	(void) snprintf(name, sizeof(name), THREAD_NAME_FMT, index);

	return name;
}

static const char *_thread_name(thread_t *thread)
{
	const char *name = NULL;

	if (thread)
		name = thread->thread_name;

	if (!name)
		name = _thread_default_name();

	return name;
}

static void _set_thread_name(thread_t *thread)
{
#if HAVE_SYS_PRCTL_H
	const char *name = _thread_name(thread);

	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL))
		error("%s: cannot set process name to %s %m", __func__, name);
#endif
}

/*
 * Takes ownership of pointer.
 * Caller must not own threadpool.mutex lock.
 */
static void _thread_free(thread_t **thread_ptr)
{
	thread_t *thread = *thread_ptr;
	*thread_ptr = NULL;

	if (!thread)
		return;

#ifndef NDEBUG
	xassert(thread->magic == THREAD_MAGIC);

	if (threadpool.enabled) {
		/* All threads must be detached at cleanup */
		xassert(thread->detached);
		xassert(thread->state == THREAD_STATE_COMPLETE);
		xassert(!thread->requester);

		slurm_mutex_lock(&threadpool.mutex);

		/* check for dangling pointers */
		xassert(!list_find_first(threadpool.attached, _match_thread_ptr,
					 thread));
		xassert(!list_find_first(threadpool.pending, _match_thread_ptr,
					 thread));

		slurm_mutex_unlock(&threadpool.mutex);
	}
#endif

	thread->magic = ~THREAD_MAGIC;
	thread->state = THREAD_STATE_INVALID_MAX;
	xfree(thread->thread_name);
	xfree(thread);
}

static void _threadpool_run(thread_t *thread)
{
	const timespec_t start = timespec_now();
	threadpool_func_t func = thread->func;
	void *arg = thread->arg;
	void *ret = NULL;

	xassert(thread->magic == THREAD_MAGIC);

	_set_thread_name(thread);

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: %s thread calling %s(0x%"PRIxPTR") after %s",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (thread->detached ? "detached" : "attached"),
		 thread->func_name, (uintptr_t) thread->arg,
		 TIMESPEC_DURATION_STR(thread->requested, start));

	HISTOGRAM_ADD_DURATION(&threadpool.histograms.request,
			       thread->requested);

	slurm_mutex_unlock(&threadpool.mutex);

	ret = func(arg);

	slurm_mutex_lock(&threadpool.mutex);

	thread->ret = ret;

	HISTOGRAM_ADD_DURATION(&threadpool.histograms.run, start);

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: %s thread called %s(0x%"PRIxPTR")=0x%"PRIxPTR" for %s",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (thread->detached ? "detached" : "attached"),
		 thread->func_name,
		 (uintptr_t) thread->arg, (uintptr_t) thread->ret,
		 TIMESPEC_ELAPSED_STR(start));
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_wait_ack(thread_t *thread)
{
	const timespec_t start_ts = timespec_now();
	pthread_t requester = thread->requester;

	xassert(thread->state == THREAD_STATE_ASSIGNED);
	thread->state = THREAD_STATE_ASSIGNED_WAIT;

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: waiting for requester 0x%"PRIx64" to acknowledge assignment",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (uint64_t) requester);

	while (thread->requester)
		EVENT_WAIT(&threadpool.events.assigned_ack, &threadpool.mutex);

	xassert(thread->state == THREAD_STATE_ASSIGNED_ACK);

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: acknowledged by requester 0x%"PRIx64" after %s",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (uint64_t) requester, TIMESPEC_ELAPSED_STR(start_ts));
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_zombie(thread_t *thread)
{
	const timespec_t start_ts = timespec_now();

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: waiting to be joined",
		 __func__, _thread_name(thread), (uint64_t) thread->id);

	/* Thread must exist in the attached list until detached */
	xassert(list_find_first(threadpool.attached, _match_thread_ptr,
				thread));
	xassert(thread->state == THREAD_STATE_RUNNING);

	thread->state = THREAD_STATE_ZOMBIE;
	threadpool.zombies++;
	xassert(threadpool.zombies > 0);

	EVENT_BROADCAST(&threadpool.events.detach);

	while (!thread->detached)
		EVENT_WAIT(&threadpool.events.detach, &threadpool.mutex);

	/* Thread must not exist in the attached list after being detached */
	xassert(!list_find_first(threadpool.attached, _match_thread_ptr,
				 thread));

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: joined after waiting %s",
		 __func__, _thread_name(thread),
		 (uint64_t) thread->id, TIMESPEC_ELAPSED_STR(start_ts));
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_prerun(thread_t *thread)
{
	xassert(thread->magic == THREAD_MAGIC);
	xassert(!thread->id);
	xassert((thread->state == THREAD_STATE_PENDING) ||
		(thread->state == THREAD_STATE_CLONING));

	thread->id = pthread_self();
	thread->state = THREAD_STATE_ASSIGNED;

	threadpool.total_run++;

	threadpool.idle--;
	threadpool.running++;

	if ((threadpool.idle + threadpool.running) > threadpool.peak_count)
		threadpool.peak_count = (threadpool.idle + threadpool.running);

	xassert(threadpool.idle >= 0);
	xassert(threadpool.running > 0);

	EVENT_BROADCAST(&threadpool.events.assigned);

	if (thread->requester)
		_threadpool_wait_ack(thread);

	thread->state = THREAD_STATE_RUNNING;
}

/* caller must hold threadpool.mutex lock */
static void _threadpool_postrun(thread_t *thread)
{
	xassert(thread->magic == THREAD_MAGIC);
	xassert(pthread_equal(thread->id, pthread_self()));
	xassert(thread->state == THREAD_STATE_RUNNING);

	threadpool.running--;
	threadpool.idle++;

	xassert(threadpool.idle > 0);
	xassert(threadpool.running >= 0);

	if (!thread->detached)
		_threadpool_zombie(thread);

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->detached);
	xassert(!thread->requester);
	xassert(thread->id > 0);

	thread->state = THREAD_STATE_COMPLETE;
	thread->id = 0;
}

static void *_thread(void *arg)
{
	thread_t *thread = arg;
	void *ret = NULL;
	const timespec_t start = timespec_now();

	xassert(!threadpool.enabled);
	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->state == THREAD_STATE_CLONING);

	_set_thread_name(thread);

	thread->state = THREAD_STATE_RUNNING;

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] BEGIN: %s thread calling %s(0x%"PRIxPTR") after %s",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (thread->detached ? "detached" : "attached"),
		 thread->func_name, (uintptr_t) thread->arg,
		 TIMESPEC_DURATION_STR(thread->requested, start));

	ret = thread->func(thread->arg);

	log_flag(THREAD, "%s: [%s@0x%"PRIx64"] END: %s thread called %s(0x%"PRIxPTR")=0x%"PRIxPTR" for %s",
		 __func__, _thread_name(thread), (uint64_t) thread->id,
		 (thread->detached ? "detached" : "attached"),
		 thread->func_name,
		 (uintptr_t) thread->arg, (uintptr_t) ret,
		 TIMESPEC_ELAPSED_STR(start));

	thread->state = THREAD_STATE_COMPLETE;
	_thread_free(&thread);

	return ret;
}

static void *_threadpool_thread(void *arg)
{
	thread_t *thread = arg;

	xassert(threadpool.enabled);

	slurm_mutex_lock(&threadpool.mutex);

	threadpool.total_created++;
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
			_threadpool_run(thread);
			_threadpool_postrun(thread);

			slurm_mutex_unlock(&threadpool.mutex);
			_thread_free(&thread);
			slurm_mutex_lock(&threadpool.mutex);
		}

		if ((thread = list_pop(threadpool.pending)))
			continue;

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

/*
 * Wait for thread assignment if the thread ID pointer needs to be populated.
 * Caller must hold threadpool.mutex lock.
 */
static void _assign_wait(thread_t *thread, pthread_t *id_ptr,
			 const char *caller)
{
	while (!thread->id) {
		xassert(thread->magic == THREAD_MAGIC);
		xassert((thread->state == THREAD_STATE_PENDING) ||
			(thread->state == THREAD_STATE_CLONING));
		xassert(pthread_equal(thread->requester, pthread_self()));

		log_flag(THREAD, "%s->%s: waiting for assignment for %s() with %d idle threads",
			 caller, __func__, thread->func_name, threadpool.idle);

		EVENT_SIGNAL(&threadpool.events.assign);
		EVENT_WAIT(&threadpool.events.assigned, &threadpool.mutex);
	}

	xassert(thread->magic == THREAD_MAGIC);
	xassert(thread->state == THREAD_STATE_ASSIGNED_WAIT);
	xassert(pthread_equal(thread->requester, pthread_self()));
	xassert(thread->id);

	thread->requester = 0;
	*id_ptr = thread->id;
	thread->state = THREAD_STATE_ASSIGNED_ACK;

	log_flag(THREAD, "%s->%s: assigned pthread id=0x%"PRIx64" for %s()",
		 caller, __func__, (uint64_t) thread->id,
		 thread->func_name);

	EVENT_BROADCAST(&threadpool.events.assigned_ack);
}

static int _new_thread(thread_t *thread, pthread_t *id_ptr, const char *caller)
{
	pthread_t id = 0;
	pthread_attr_t attr;
	int rc = EINVAL;
	const char *func_name = (thread ? thread->func_name : NULL);
	const bool detached = (thread && thread->detached);

	/* only threadpool will have pre-allocated threads */
	xassert(thread || threadpool.enabled);

	if (thread) {
		xassert(thread->magic == THREAD_MAGIC);
		xassert(thread->state == THREAD_STATE_NEW);
		xassert(!thread->id);
		xassert(!thread->requester);
		thread->state = THREAD_STATE_CLONING;

		if (id_ptr)
			thread->requester = pthread_self();
	}

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

	/* All threadpool threads are always detached */
	if ((threadpool.enabled || detached) &&
	    (rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
		fatal("%s->%s: pthread_attr_setdetachstate failed: %s",
		      caller, __func__, slurm_strerror(rc));

	/* Pass ownership of thread to new thread on success */
	if ((rc = pthread_create(&id, &attr,
				 (threadpool.enabled ? _threadpool_thread :
						       _thread),
				 thread)))
		fatal("%s->%s: pthread_create() failed: %s",
		      caller, __func__, slurm_strerror(rc));

	/*
	 * Wait until thread->id is populated after the thread starts or
	 * there is a race to the next call of join() or detach()
	 */
	if (id_ptr) {
		if (threadpool.enabled) {
			slurm_mutex_lock(&threadpool.mutex);

			_assign_wait(thread, id_ptr, caller);
			xassert(pthread_equal(thread->id, id));

			slurm_mutex_unlock(&threadpool.mutex);
		} else {
			xassert(id > 0);
			*id_ptr = id;
		}
	}

	xassert(threadpool.enabled || func_name);
	log_flag(THREAD, "%s->%s: pthread_create() created new %spthread id=0x%"PRIx64" for %s%s",
		 caller, __func__, (threadpool.enabled ? "" :
				    (detached ?  "detached " : "attached ")),
		 (uint64_t) id, (threadpool.enabled ? "threadpool" : func_name),
		 (threadpool.enabled ? "" : "()"));

	_free_attr(&attr);

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
	const int zombies = threadpool.zombies;

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
	xassert(thread->state > THREAD_STATE_INVALID);
	xassert(thread->state < THREAD_STATE_INVALID_MAX);

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

	thread->state = THREAD_STATE_PENDING;
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

	_assign_wait(thread, id_ptr, caller);

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

	if (id_ptr)
		*id_ptr = 0;

#ifndef NDEBUG
	if (thread_name && strlen(thread_name) >= PRCTL_BUF_BYTES)
		warning("%s: Thread name truncated[%zu/%zu]: %s",
			caller, (uint64_t) strlen(thread_name),
			(uint64_t) PRCTL_BUF_BYTES, thread_name);
#endif

	*thread = (thread_t) {
		.magic = THREAD_MAGIC,
		.state = THREAD_STATE_NEW,
		.detached = detached,
		.thread_name = xstrdup(thread_name),
		.func = func,
		.func_name = func_name,
		.arg = arg,
		.requested = timespec_now(),
	};

	if (threadpool.enabled) {
		if (!detached) {
			slurm_mutex_lock(&threadpool.mutex);
			list_append(threadpool.attached, thread);
			slurm_mutex_unlock(&threadpool.mutex);
		}

		if (_assign(thread, id_ptr, caller))
			return SLURM_SUCCESS;
	}

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
				threadpool.enabled = true;
			else if (!xstrcasecmp(value, "disabled"))
				threadpool.enabled = false;
			else
				fatal("Invalid parameter %s", tok);

			log_flag(THREAD, "%s: threadpool is %s",
				 __func__,
				 (threadpool.enabled ? "enabled" : "disabled"));
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

	probe_log(logt->log, "thread[%s@0x%"PRIx64"]: func=%s(0x%"PRIxPTR") type=%s state=%s detached=%c requester=0x%"PRIx64" ret=0x%"PRIxPTR,
		  thread->thread_name, (uint64_t) thread->id,
		  thread->func_name, (uintptr_t) thread->arg, logt->type,
		  _thread_state_str(thread->state),
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
		"state: shutdown:%c pending:%d zombies:%d attached:%d running:%d idle:%d total_run:%" PRIu64 " total_created:%" PRIu64 " peak_count:%" PRIu64,
		BOOL_CHARIFY(threadpool.shutdown),
		list_count(threadpool.pending), threadpool.zombies,
		list_count(threadpool.attached), threadpool.running,
		threadpool.idle, threadpool.total_run, threadpool.total_created,
		threadpool.peak_count);

	logt.type = "pending";
	(void) list_for_each_ro(threadpool.pending, _log_thread, &logt);
	logt.type = "attached";
	(void) list_for_each_ro(threadpool.attached, _log_thread, &logt);

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

static probe_status_t _probe(probe_log_t *log, void *arg)
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

	slurm_mutex_lock(&threadpool.mutex);

	if (threadpool.pending || threadpool.shutdown || !threadpool.enabled) {
		slurm_mutex_unlock(&threadpool.mutex);
		return;
	}

	xassert(!threadpool.shutdown);
	xassert(threadpool.enabled);

	xassert(!threadpool.pending);
	threadpool.pending = list_create(NULL);

	xassert(!threadpool.attached);
	threadpool.attached = list_create(NULL);

	preallocate = threadpool.config.preallocate;

	slurm_mutex_unlock(&threadpool.mutex);

	probe_register("threadpool", _probe, NULL);

	xassert(preallocate >= 0);

	for (int i = 0; i < preallocate; i++)
		_new_thread(NULL, NULL, __func__);

	log_flag(THREAD, "%s: started with %d threads preallocated",
		 __func__, preallocate);
}

extern void threadpool_fini(void)
{
	slurm_mutex_lock(&threadpool.mutex);

	if (!threadpool.enabled) {
		slurm_mutex_unlock(&threadpool.mutex);
		return;
	}

	/*
	 * Never change threadpool.enabled to false to avoid race conditions of
	 * checking if threadpool was ever enabled
	 */

	threadpool.shutdown = true;

	/* Wake every idle worker to clean up */
	EVENT_BROADCAST(&threadpool.events.assign);

	slurm_mutex_unlock(&threadpool.mutex);
}

static int _threadpool_detach(const pthread_t id, const char *caller)
{
	thread_t *thread = NULL;
	int rc = EINVAL;

	slurm_mutex_lock(&threadpool.mutex);

	if (!(thread = list_find_first(threadpool.attached, _match_thread_id,
				       (void *) &id))) {
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" not found",
			       caller, __func__, (uint64_t) id);
		rc = ESRCH;
	} else {
		rc = _threadpool_on_detach(thread, caller);
	}

	slurm_mutex_unlock(&threadpool.mutex);

	return rc;
}

static int _detach(const pthread_t id, const char *caller)
{
	int rc = EINVAL;

	if ((rc = pthread_detach(id)))
		error("%s->%s: pthread_detach(id=0x%"PRIx64") failed: %s",
		      caller, __func__, (uint64_t) id, slurm_strerror(rc));

	return rc;
}

extern int threadpool_detach(const pthread_t id, const char *caller)
{
	if (!id) {
		log_flag(THREAD, "%s->%s: Ignoring invalid pthread id=0x0",
		       caller, __func__);
		return SLURM_SUCCESS;
	}

	if (threadpool.enabled)
		return _threadpool_detach(id, caller);
	else
		return _detach(id, caller);
}
