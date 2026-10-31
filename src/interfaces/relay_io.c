/*****************************************************************************\
 *  relay_io.c - IO relay interface
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <pthread.h>

#include "src/common/data.h"
#include "src/common/events.h"
#include "src/common/io_hdr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xutf.h"

#include "src/interfaces/relay_io.h"

#define SRC_MAGIC 0x5c101a

/*
 * The default max number of bytes that can be read from a pipe at a time is
 * 64KiB. Allow for 2 max reads to backup before dropping.
 */
#define DEFAULT_SRC_MAX_BUF_SIZE (2 * 64 * 1024)

/*
 * Most output one visit to a source turns into records.
 *
 * Without a bound, a task producing faster than the relay can serialize keeps
 * the drain loop fed for as long as it runs and every other source waits,
 * until they fill up and start dropping. This is a full delivery from either
 * shape: slurmstepd reads a task 4KiB at a time and a pipe holds 64KiB, so a
 * source that just filled one is still emptied in a single visit.
 */
#define SRC_DRAIN_BYTES (64 * 1024)

/*
 * How long a source waits before trying again when its buffer refused to
 * grow. Long enough not to spin on an allocation that keeps failing, short
 * enough that a task is not noticeably held once memory frees up.
 */
#define SRC_NO_GROW_WAIT_USEC (USEC_IN_SEC / 10)

typedef struct {
	int (*init)(const char *spooldir);
	int (*fini)(void);
	int (*record)(const relay_io_record_t *record, size_t *consumed_ptr);
	int (*flush)(void);
	size_t (*log_limit)(void);
} relay_io_ops_t;

/* One task's stdout or stderr, and the output of it not yet made a record */
struct relay_io_src_s {
	int magic; /* DO NOT ALPHABETIZE */

	/* fixed for the life of the source */
	relay_io_ident_t ident;
	char *cluster; /* backing store for ident, which borrows */
	char *node;
	char *user_name;
	uint32_t step_id;
	uint32_t task_id_global;
	uint32_t task_id_local;
	relay_io_stream_t stream;
	bool unbuffered;

	/*
	 * Output held until it can be turned into records. The buffer's offset
	 * is how much of it is in use, so remaining_buf() is the free space.
	 *
	 * Guarded by this source's own mutex rather than srcs_mutex, so that a
	 * task appending output only ever waits on the one other task reading
	 * the same stream, never on the relay thread working through some
	 * other source.
	 */
	pthread_mutex_t mutex;
	pthread_cond_t room_cond; /* the relay thread made room in buf */
	buf_t *buf;
	bool new_data; /* output arrived since the relay thread last looked */
	bool dead; /* owner let go, the thread frees it */
	bool eof; /* no more output is coming */
	bool flushed; /* the tail has been emitted */
};

/*
 * These strings must be kept in the same order as the fields
 * declared for relay_io_ops_t.
 */
static const char *syms[] = {
	"relay_io_p_init",
	"relay_io_p_fini",
	"relay_io_p_record",
	"relay_io_p_flush",
	"relay_io_p_log_limit",
};

static relay_io_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Sources being relayed.
 *
 * Deliberately not a list_t: the relay thread has to release the lock while it
 * builds a record, and doing that inside list_for_each() would hold the list's
 * own lock across the release, deadlocking against a caller registering a new
 * source. A plain array leaves only the one lock to reason about.
 */
static relay_io_src_t **srcs = NULL;
static int srcs_count = 0;
static int srcs_alloc = 0;

/*
 * srcs_mutex guards the array above and the relay thread below, and is the
 * event's lock. It is deliberately never held while a source's buffer is
 * touched: waiting on an event needs one common lock, but the work does not.
 */
static pthread_mutex_t srcs_mutex = PTHREAD_MUTEX_INITIALIZER;
static event_signal_t srcs_event = EVENT_INITIALIZER("relay_io_srcs");

static pthread_t srcs_thread;
static bool srcs_thread_running = false;
static bool srcs_thread_joined = false;
static bool srcs_shutdown = false;

/* Most a source may hold before blocking to let the output drain. */
static size_t srcs_limit = 0;

/*
 * Most user output allowed in one record, from the plugin. A line this long is
 * emitted without waiting for its newline: a task that never terminates a
 * line would otherwise grow a source's buffer without bound
 */
static size_t srcs_log_limit = 0;

static void _src_destroy(relay_io_src_t *src);

/*
 * Answer from the configuration rather than from load state, so that this can
 * be asked before any plugin is loaded. "none" must mean the same as unset:
 * anything that decides whether to divert a step's output has to agree with
 * what relay_io_g_init() loaded, or the output is dropped.
 */
extern bool relay_io_configured(void)
{
	return (slurm_conf.relay_io_plugin &&
		xstrcasecmp(slurm_conf.relay_io_plugin, "none"));
}

/*
 * Wake the relay thread.
 *
 * An event rather than a bare condition variable: the thread releases
 * srcs_mutex around every drain, so output can arrive at a source it has
 * already walked past while nothing is waiting yet. EVENT_SIGNAL() keeps that
 * signal pending and the next EVENT_WAIT() returns on it rather than sleeping
 * through it.
 */
static void _srcs_wake(void)
{
	slurm_mutex_lock(&srcs_mutex);
	EVENT_SIGNAL(&srcs_event);
	slurm_mutex_unlock(&srcs_mutex);
}

/*
 * Wake the relay thread on behalf of a task waiting for room.
 *
 * RET false once the thread has been joined, when nothing will ever drain a
 *	source again and a waiter has to give up rather than block forever
 */
static bool _srcs_wake_for_room(void)
{
	bool running;

	slurm_mutex_lock(&srcs_mutex);
	if ((running = srcs_thread_running))
		EVENT_SIGNAL(&srcs_event);
	slurm_mutex_unlock(&srcs_mutex);

	return running;
}

/*
 * A consumer ends a partial chain on the step end record, so output that
 * arrived after it would be joined to nothing. Sources stay valid after this,
 * but produce no further records.
 */
extern void relay_io_g_srcs_drain(void)
{
	slurm_mutex_lock(&srcs_mutex);

	if (!srcs_thread_running) {
		slurm_mutex_unlock(&srcs_mutex);
		return;
	}

	/*
	 * No more output is coming, so a trailing line that never got its
	 * newline is emitted rather than held for output that will not arrive.
	 */
	for (int i = 0; i < srcs_count; i++) {
		slurm_mutex_lock(&srcs[i]->mutex);
		srcs[i]->eof = true;
		slurm_mutex_unlock(&srcs[i]->mutex);
	}

	srcs_shutdown = true;
	EVENT_SIGNAL(&srcs_event);
	slurm_mutex_unlock(&srcs_mutex);

	slurm_thread_join(srcs_thread);

	slurm_mutex_lock(&srcs_mutex);
	srcs_thread_running = false;
	srcs_thread_joined = true;
	/* Make sure there are not sources left stuck waiting for room */
	for (int i = 0; i < srcs_count; i++) {
		slurm_mutex_lock(&srcs[i]->mutex);
		slurm_cond_broadcast(&srcs[i]->room_cond);
		slurm_mutex_unlock(&srcs[i]->mutex);
	}
	slurm_mutex_unlock(&srcs_mutex);
}

/*
 * Stop the relay thread and wait for it.
 *
 * The thread is the only caller of the plugin's record entry point, so joining
 * it before the plugin is unloaded is what keeps a record from being built
 * against text that has already been unmapped.
 */
static void _srcs_thread_fini(void)
{
	relay_io_g_srcs_drain();

	slurm_mutex_lock(&srcs_mutex);

	if (!srcs_thread_joined) {
		slurm_mutex_unlock(&srcs_mutex);
		return;
	}

	for (int i = 0; i < srcs_count; i++)
		_src_destroy(srcs[i]);
	srcs_count = 0;
	srcs_alloc = 0;
	xfree(srcs);
	slurm_mutex_unlock(&srcs_mutex);
}

/*
 * Emit one record for a span of a source's output.
 *
 * IN src - source the output came from
 * IN body - bytes to relay, need not be NULL terminated
 * IN len - number of bytes in body
 * IN flush - true to emit the span whole because no more bytes are coming
 * RET bytes emitted, which is short of len when the span ended mid character
 *	and the remainder was held back, or when the relay could not carry the
 *	whole span in one record
 */
static size_t _src_emit(relay_io_src_t *src, const char *body, size_t len,
			bool flush)
{
	relay_io_record_t record = { 0 };

	/*
	 * Splitting a character across two records would replace the lead byte
	 * in one record and the orphaned continuation bytes in the next,
	 * turning one character into two U+FFFD. Holding the fragment back
	 * until the rest of it arrives keeps the character whole.
	 */
	if (!flush)
		len -= utf8_incomplete_tail((const utf8_t *) body, len);

	if (!len)
		return 0;

	record = (relay_io_record_t) {
		.body = body,
		.body_len = len,
		.ident = src->ident,
		/*
		 * Only line buffered output has lines to be short of. An
		 * unbuffered record is cut at an arbitrary offset by
		 * definition, which "unbuffered" already says.
		 */
		.partial = (!src->unbuffered && (body[len - 1] != '\n')),
		.step_id = src->step_id,
		.stream = src->stream,
		.task_id_global = src->task_id_global,
		.task_id_local = src->task_id_local,
		.time = timespec_now(),
		.type = RELAY_IO_EVENT_LOG,
		.unbuffered = src->unbuffered,
	};

	/*
	 * A relay whose transport caps a record's size takes only as much of
	 * the span as it can carry, so the rest has to be offered again rather
	 * than assumed gone.
	 */
	(void) relay_io_g_record(&record, &len);

	return len;
}

/*
 * How much of a source's output is ready to become one record.
 *
 * With normal buffered I/O a record is one line, which is what a reader of the
 * job's output would have seen. With --unbuffered there is no line structure
 * to rely on, so output is emitted in fixed size chunks instead.
 *
 * IN src - source the output came from
 * IN data - output held by the source
 * IN len - number of bytes in data
 * IN flush - true when no more output is coming, so a trailing line that never
 *	got its newline is emitted rather than held
 * RET bytes to turn into the next record, 0 to wait for more output
 */
static size_t _src_next_span(relay_io_src_t *src, const char *data, size_t len,
			     bool flush)
{
	const char *nl = NULL;
	size_t span = 0;

	if (!len)
		return 0;

	/*
	 * --unbuffered exists to remove output latency, so a partial chunk is
	 * emitted rather than held back for the rest of one.
	 */
	if (src->unbuffered)
		span = MIN(len, SLURM_IO_MAX_MSG_LEN);
	else if ((nl = memchr(data, '\n', len)))
		span = ((nl - data) + 1); /* the newline belongs to its line */
	/* a line this long needs to be sent in multiple records */
	else if (flush || (len >= srcs_log_limit))
		span = len;

	return MIN(span, srcs_log_limit);
}

/*
 * Whether anything a source holds can become a record right now.
 *
 * The same question _src_emit() answers by trimming, asked without building
 * anything. A span that is only the start of a character whose remaining
 * bytes have not arrived yields no record, and treating that as work to do
 * would spin the relay thread on it.
 *
 * IN src - source to ask about
 * IN data - output held by the source
 * IN len - number of bytes in data
 * IN flush - true when no more output is coming
 * RET true when a record can be built
 */
static bool _src_can_emit(relay_io_src_t *src, const char *data, size_t len,
			  bool flush)
{
	size_t span = _src_next_span(src, data, len, flush);

	if (!span)
		return false;

	if (!flush)
		span -= utf8_incomplete_tail((const utf8_t *) data, span);

	return (span > 0);
}

/*
 * Turn what a source is holding into records.
 *
 * Takes only this source's lock, so a task appending to any other source is
 * never delayed by it, and the lock is dropped while a record is built. Only a
 * bounded copy is made while holding it, because appending output can grow the
 * buffer, which moves it.
 *
 * RET true when there was something to do, so the relay thread knows not to
 *	go back to sleep yet
 */
static bool _src_drain(relay_io_src_t *src, char *scratch, bool shutdown)
{
	size_t off = 0, used, limit;
	bool flush, last;

	slurm_mutex_lock(&src->mutex);

	if (!src->new_data && (!src->eof || src->flushed)) {
		slurm_mutex_unlock(&src->mutex);
		return false;
	}

	/*
	 * Only what was already waiting, and no more than one visit's worth.
	 * Output appended while this runs is left for the next visit, so a
	 * source with a task behind it cannot hold the relay thread while
	 * every other source waits.
	 *
	 * Never less than one record, whatever the plugin's record size is: a
	 * visit that could not reach the length that forces an unterminated
	 * line out would emit nothing and be asked straight back, and the
	 * thread would spin on a source it can never make progress on.
	 */
	used = get_buf_offset(src->buf);
	if (shutdown)
		limit = used; /* All connections ended sent everything */
	else
		limit = MIN(used, MAX(SRC_DRAIN_BYTES, srcs_log_limit));

	/*
	 * Only the visit that reaches the end of a finished stream may emit a
	 * line that never got its newline, or a character whose remaining
	 * bytes will never arrive. A capped visit stops short of the end, so
	 * treating it as the end would cut a line, and a multi-byte character,
	 * at the cap.
	 */
	flush = (src->eof && (limit >= used));

	while (off < limit) {
		size_t span, consumed;
		char *emit_buffer = NULL;

		span = _src_next_span(src, (get_buf_data(src->buf) + off),
				      (limit - off), flush);

		if (!span)
			break; /* the rest needs output still to come */

		/*
		 * While src->buf size has not reached srcs_limit copy the data
		 * into scratch buffer because locks are released
		 * during the relatively expensive _src_emit() call to prevent
		 * blocking task output from being queued for this source which
		 * may reallocate the buffer.
		 *
		 * Once src->buf reaches is max size it won't be reallocated
		 * again. Skip the unneeded copy.
		 */
		if (size_buf(src->buf) >= srcs_limit)
			emit_buffer = get_buf_data(src->buf) + off;
		else
			emit_buffer =
				memcpy(scratch, (get_buf_data(src->buf) + off),
				       span);

		/*
		 * Only the span reaching the end of a finished stream may hold
		 * back nothing. Every earlier span has output after it, so a
		 * character split across the cut still has its remaining bytes
		 * waiting and must not be emitted as a replacement character.
		 */
		last = (flush && ((off + span) >= limit));

		slurm_mutex_unlock(&src->mutex);

		consumed = _src_emit(src, emit_buffer, span, last);

		slurm_mutex_lock(&src->mutex);

		if (!consumed) {
			/*
			 * The span was only the start of a character whose
			 * remaining bytes have not arrived. Whether the rest
			 * is reachable is settled below, against everything
			 * held rather than just this visit's share of it.
			 */
			break;
		}

		off += consumed;
	}

	/*
	 * Compacted once rather than after every record: each one was taken
	 * from the head, so a single move puts what is left back at the front.
	 * Output appended while a record was being built is at the tail, which
	 * is why the length is read again here.
	 */
	used = get_buf_offset(src->buf);
	xassert(used >= off);
	if (off) {
		memmove(get_buf_data(src->buf), (get_buf_data(src->buf) + off),
			(used - off));
		used -= off;
		set_buf_offset(src->buf, used);

		/* a task waiting for room can have what was just freed */
		slurm_cond_broadcast(&src->room_cond);
	}

	/*
	 * This visit only looked at what was waiting when it started, so
	 * whether to come straight back is decided from everything held now.
	 * Claiming there is nothing left would strand a record until the next
	 * append; claiming the opposite would spin.
	 */
	if (!_src_can_emit(src, get_buf_data(src->buf), used,
			   (src->eof && !src->flushed)))
		src->new_data = false;

	if (src->eof && !used)
		src->flushed = true;

	slurm_mutex_unlock(&src->mutex);

	return true;
}

static void *_srcs_thread(void *arg)
{
	char *scratch = xmalloc(srcs_log_limit);

	slurm_mutex_lock(&srcs_mutex);

	while (true) {
		bool shutdown = srcs_shutdown;
		bool worked = false;

		for (int i = 0; i < srcs_count; i++) {
			relay_io_src_t *src = srcs[i];

			/*
			 * Released for the drain itself: only this thread ever
			 * frees a source, so the pointer stays good, and
			 * holding this lock would make every task appending
			 * output wait on unrelated work.
			 */
			slurm_mutex_unlock(&srcs_mutex);
			worked |= _src_drain(src, scratch, shutdown);
			slurm_mutex_lock(&srcs_mutex);
		}

		for (int i = 0; i < srcs_count; i++) {
			relay_io_src_t *src = srcs[i];
			bool done;

			slurm_mutex_lock(&src->mutex);
			done = (src->dead && !get_buf_offset(src->buf));
			slurm_mutex_unlock(&src->mutex);

			if (!done)
				continue;

			_src_destroy(src);
			srcs_count--;
			srcs[i] = srcs[srcs_count];
			i--;
		}

		/*
		 * Every source is at eof by now, which made the last pass emit
		 * the remaining data.
		 */
		if (shutdown)
			break;

		/*
		 * A pass only takes SRC_DRAIN_BYTES from each source when not
		 * shutting down, so continue until no more work can be done
		 * now.
		 */
		if (!worked)
			EVENT_WAIT(&srcs_event, &srcs_mutex);
	}

	slurm_mutex_unlock(&srcs_mutex);

	xfree(scratch);

	return NULL;
}

static void _src_destroy(relay_io_src_t *src)
{
	if (!src)
		return;

	xassert(src->magic == SRC_MAGIC);

	slurm_mutex_destroy(&src->mutex);
	slurm_cond_destroy(&src->room_cond);
	FREE_NULL_BUFFER(src->buf);
	xfree(src->cluster);
	xfree(src->node);
	xfree(src->user_name);
	src->magic = 0;
	xfree(src);
}

extern relay_io_src_t *relay_io_g_src_create(const relay_io_ident_t *ident,
					     uint32_t step_id,
					     uint32_t task_id_global,
					     uint32_t task_id_local,
					     relay_io_stream_t stream,
					     bool unbuffered)
{
	relay_io_src_t *src = NULL;

	xassert(ident);

	if (plugin_inited != PLUGIN_INITED)
		return NULL;

	src = xmalloc(sizeof(*src));
	*src = (relay_io_src_t) {
		.magic = SRC_MAGIC,
		.cluster = xstrdup(ident->cluster),
		.node = xstrdup(ident->node),
		.stream = stream,
		.step_id = step_id,
		.task_id_global = task_id_global,
		.task_id_local = task_id_local,
		.unbuffered = unbuffered,
		.user_name = xstrdup(ident->user_name),
	};

	/* the record builder borrows these, so they have to outlive it */
	src->ident = (relay_io_ident_t) {
		.cluster = src->cluster,
		.job_id = ident->job_id,
		.node = src->node,
		.restart_count = ident->restart_count,
		.sluid = ident->sluid,
		.user_name = src->user_name,
	};

	slurm_mutex_init(&src->mutex);
	slurm_cond_init(&src->room_cond, NULL);
	src->buf = init_buf(BUF_SIZE);

	slurm_mutex_lock(&srcs_mutex);

	if (!srcs_thread_running) {
		srcs_log_limit = (*(ops.log_limit))();
		/*
		 * Allow for at least 2 messages to backup in buffer, but hard
		 * limit at MAX_BUF_SIZE
		 */
		srcs_limit = MAX(DEFAULT_SRC_MAX_BUF_SIZE, srcs_log_limit * 2);
		srcs_limit = MIN(srcs_limit, MAX_BUF_SIZE);

		/* a relay with no record cap still needs the runaway guard */
		if (!srcs_log_limit)
			srcs_log_limit = srcs_limit / 2;
		srcs_shutdown = false;
		srcs_thread_running = true;
		slurm_thread_create(NULL, &srcs_thread, _srcs_thread, NULL);
	}

	if (srcs_count == srcs_alloc) {
		srcs_alloc = MAX((srcs_alloc * 2), 8);
		xrecalloc(srcs, srcs_alloc, sizeof(*srcs));
	}
	srcs[srcs_count++] = src;

	slurm_mutex_unlock(&srcs_mutex);

	return src;
}

/*
 * Hold the caller rather than drop: a refusal would cut at whatever byte the
 * buffer filled on, splicing lines together and splitting multi-byte
 * characters. The task backs up through its own pipe instead. Only a stopped
 * relay thread makes this return without taking everything.
 */
extern void relay_io_g_src_append(relay_io_src_t *src, const char *data,
				  size_t len)
{
	bool had_data = false;
	bool running = true;

	if (!src || !data || !len)
		return;

	xassert(src->magic == SRC_MAGIC);

	slurm_mutex_lock(&src->mutex);

	/*
	 * Take what fits and wait for the rest rather than lose it. A refusal
	 * cuts at whatever byte the buffer filled on, which splices the head
	 * of one line on to the tail of another and can cut a multi-byte
	 * character in half, so the output a consumer gets is corrupt rather
	 * than merely short. Holding the caller here instead backs the task
	 * up through its own pipe, which is the only way to slow a task down
	 * that does not involve losing what it already wrote.
	 */
	while (len) {
		size_t used = get_buf_offset(src->buf);
		size_t room = ((srcs_limit > used) ? (srcs_limit - used) : 0);
		size_t take = MIN(len, room);
		bool no_grow = false;

		/* nobody is going to read this source again */
		if (src->dead)
			break;

		if (take) {
			if (!try_grow_buf_remaining(src->buf, take)) {
				memcpy((get_buf_data(src->buf) + used), data,
				       take);
				set_buf_offset(src->buf, (used + take));
				had_data = src->new_data;
				src->new_data = true;
				data += take;
				len -= take;
				continue;
			}

			/*
			 * Under the limit but the buffer will not grow into
			 * it, so the room being there says nothing and
			 * looking again would spin. Only the relay taking
			 * what is already here can help.
			 */
			no_grow = true;
		}

		/*
		 * The relay thread may have gone back to sleep with this
		 * buffer full, so wake it before waiting on it. Its lock
		 * outranks this one, so drop this one across the wake and
		 * then look again: the room being waited for may have been
		 * made in the meantime.
		 */
		slurm_mutex_unlock(&src->mutex);
		running = _srcs_wake_for_room();
		slurm_mutex_lock(&src->mutex);

		/* the thread has been joined, so the room never comes */
		if (!running)
			break;

		if (!no_grow && (get_buf_offset(src->buf) < srcs_limit))
			continue;

		if (no_grow) {
			/*
			 * A drain broadcasts, but an allocation that keeps
			 * failing with nothing held here has nothing to
			 * drain, so come back on a timer rather than wait
			 * for a wake that may never come.
			 */
			timespec_t ts = timespec_add(
				timespec_now(),
				TIMESPEC_FROM_USEC(SRC_NO_GROW_WAIT_USEC));

			slurm_cond_timedwait(&src->room_cond, &src->mutex, &ts);
			continue;
		}

		slurm_cond_wait(&src->room_cond, &src->mutex);
	}

	slurm_mutex_unlock(&src->mutex);

	/*
	 * Only when the relay had caught up. While it still has output of ours
	 * to work through it is either running or already about to be woken,
	 * so waking it per append would serialize every writer on srcs_mutex
	 * for nothing.
	 */
	if (!had_data)
		_srcs_wake();
}

extern void relay_io_g_src_eof(relay_io_src_t *src)
{
	if (!src)
		return;

	xassert(src->magic == SRC_MAGIC);

	slurm_mutex_lock(&src->mutex);
	src->eof = true;
	slurm_mutex_unlock(&src->mutex);

	_srcs_wake();
}

extern void relay_io_g_src_destroy(relay_io_src_t **src_ptr)
{
	relay_io_src_t *src = NULL;

	if (!src_ptr || !(src = *src_ptr))
		return;

	xassert(src->magic == SRC_MAGIC);

	/*
	 * Handed to the relay thread rather than freed here: it may be part
	 * way through turning this source's output into records, and it still
	 * has a trailing line to emit.
	 *
	 * Releasing a source says as much about the output ending as reaching
	 * eof does, and the thread only reclaims a source once it holds
	 * nothing. Without this a source let go mid-line would hold that line
	 * for the life of the process, never emitting it and never being
	 * reclaimed.
	 */
	slurm_mutex_lock(&src->mutex);
	src->dead = true;
	src->eof = true;
	/* nothing will be read from this source again, so stop waiting on it */
	slurm_cond_broadcast(&src->room_cond);
	slurm_mutex_unlock(&src->mutex);

	_srcs_wake();

	*src_ptr = NULL;
}

extern int relay_io_g_init(const char *spooldir)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "relay_io";
	char *type = NULL;

	xassert(running_in_slurmd_stepd());

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!relay_io_configured()) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	/*
	 * RelayIOPlugin is documented as taking the full plugin name,
	 * "relay_io/uds_json", which is what plugin_context_create() wants.
	 * The bare flavor is still accepted, so a site that writes just
	 * "uds_json" keeps working.
	 */
	if (xstrchr(slurm_conf.relay_io_plugin, '/'))
		type = xstrdup(slurm_conf.relay_io_plugin);
	else
		type = xstrdup_printf("%s/%s", plugin_type,
				      slurm_conf.relay_io_plugin);

	g_context = plugin_context_create(plugin_type, type, (void **) &ops,
					  syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	if ((rc = (*(ops.init))(spooldir)) != SLURM_SUCCESS) {
		error("%s: %s failed to initialize: %s",
		      __func__, type, slurm_strerror(rc));
		(void) plugin_context_destroy(g_context);
		g_context = NULL;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	xfree(type);
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int relay_io_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	/* before the lock: the thread takes it to emit what it still holds */
	_srcs_thread_fini();

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited == PLUGIN_INITED) {
		/*
		 * The relay thread has just handed over everything it was
		 * holding, so this is the only chance to get it written. A
		 * caller that flushed before calling here flushed too early to
		 * cover the tail of every stream.
		 */
		(void) (*(ops.flush))();

		rc = (*(ops.fini))();
	}

	if (g_context) {
		int rc2 = plugin_context_destroy(g_context);

		if (!rc)
			rc = rc2;
		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);

	return rc;
}

/*
 * Relaying is best effort by design, and relay_io_g_record() sits on the path
 * that every line of task output takes. So unlike most interfaces these two
 * do not xassert() on an uninitialized plugin and do not take the lock: a
 * misconfigured or failed plugin has to degrade to doing nothing rather than
 * abort a running job, and plugin_inited only changes at init and fini.
 */
extern int relay_io_g_record(const relay_io_record_t *record,
			     size_t *consumed_ptr)
{
	xassert(record);

	/*
	 * Seed this before dispatching so that a plugin which never cuts a
	 * body, or which returns early, still reads as having taken all of it.
	 * A caller told nothing was consumed would hold the bytes and try
	 * again, and the buffer behind it would grow without bound.
	 */
	if (consumed_ptr)
		*consumed_ptr = record->body_len;

	if (plugin_inited != PLUGIN_INITED)
		return SLURM_SUCCESS;

	return (*(ops.record))(record, consumed_ptr);
}

extern int relay_io_g_flush(void)
{
	if (plugin_inited != PLUGIN_INITED)
		return SLURM_SUCCESS;

	return (*(ops.flush))();
}

extern bool relay_io_requested(uint16_t requested)
{
	if (!relay_io_configured())
		return false;

	if (requested != NO_VAL16)
		return requested;

	return (slurm_conf.conf_flags & CONF_FLAG_RELAY_IO);
}

extern bool relay_io_step_requested(uint32_t step_id, bool relay_io)
{
	if (step_id == SLURM_EXTERN_CONT)
		return false;
	return (relay_io && relay_io_configured());
}

extern void relay_io_g_job_start(const relay_io_ident_t *ident)
{
	relay_io_record_t record = { 0 };

	xassert(ident);

	if (plugin_inited != PLUGIN_INITED)
		return;

	record = (relay_io_record_t) {
		.type = RELAY_IO_EVENT_JOB_START,
		.time = timespec_now(),
		.ident = *ident,
	};

	(void) relay_io_g_record(&record, NULL);
}

extern void relay_io_g_job_end(const relay_io_ident_t *ident)
{
	relay_io_record_t record = { 0 };

	xassert(ident);

	if (plugin_inited != PLUGIN_INITED)
		return;

	record = (relay_io_record_t) {
		.type = RELAY_IO_EVENT_JOB_END,
		.time = timespec_now(),
		.ident = *ident,
	};

	(void) relay_io_g_record(&record, NULL);
}
