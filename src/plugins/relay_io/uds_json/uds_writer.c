/*****************************************************************************\
 *  uds_writer.c - non-blocking writer for a UNIX domain socket
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

#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/relay_io.h"

#include "../common/ring_buffer.h"

#include "uds_writer.h"

#define WRITER_MAGIC 0x5219a20

/* how long the thread sleeps when there is nothing to do */
#define IDLE_WAIT ((timespec_t) { .tv_nsec = (500 * NSEC_IN_MSEC) })

/* reconnect backoff, doubling from the first to the second */
#define BACKOFF_MIN ((timespec_t) { .tv_nsec = (250 * NSEC_IN_MSEC) })
#define BACKOFF_MAX ((timespec_t) { .tv_sec = 30 })

/* how long to wait for the socket to become writable before rechecking */
#define POLL_WAIT ((timespec_t) { .tv_nsec = (250 * NSEC_IN_MSEC) })

/* report an unsendable record the first time, then once per this many */
#define UNSENDABLE_LOG_INTERVAL 1000

typedef enum {
	UNTRUSTED,
	DIR_STAT_FAILED,
	DIR_NO_EXIST,
	DIR_BAD_OWNER,
	DIR_GRP_OTHER_WRITE,
	SOCKET_STAT_FAILED,
	SOCKET_NO_EXIST,
	SOCKET_NOT_A_SOCKET,
	SOCKET_BAD_OWNER,
	TRUSTED,
} trust_state_t;

/*
 * A record leaves the ring before it is written, so a partial write can be
 * resumed without the record being evicted underneath us.
 *
 * Only the writer thread assigns out, out_len, fd, backoff and
 * socket_trust, but uds_writer_flush() needs to know whether a record is
 * still in flight, so every transition of "out" between NULL and non-NULL is
 * made under the mutex. fd, backoff and socket_trust are never read
 * outside the writer thread and need no protection.
 */
struct uds_writer_s {
	int magic; /* DO NOT ALPHABETIZE */
	timespec_t backoff;
	pthread_cond_t cond;
	/*
	 * uds_writer_flush() has to tell "the queue is moving" from "nobody is
	 * going to take it", so these are published under the mutex rather
	 * than kept to the writer thread like fd and backoff.
	 */
	uint64_t connect_tries; /* failed connection attempts */
	bool connected;
	int fd;
	/* min message size dropped due to EMSGSIZE, guarded by mutex */
	size_t min_dropped_msg_size;
	pthread_mutex_t mutex;
	/*
	 * Submitting a record broadcasts, which wakes this thread early. That
	 * is what makes a queued record go out promptly, but it must not turn
	 * the reconnect backoff into a busy loop while the far end is down, so
	 * the next attempt is gated on a deadline rather than on the wait
	 * having elapsed.
	 */
	timespec_t next_connect;
	char *out; /* record in flight, see the note above the struct */
	size_t out_len;
	char *path;
	relay_io_ring_t *ring;
	bool running;
	bool shutdown;
	trust_state_t socket_trust; /* cur trust state, used to not over log */
	pthread_t thread;
	/* record both message loss types, guarded by mutex */
	uint64_t unsendable_by_build;
	uint64_t unsendable_by_size;
	/* unsendable count due to be reported, guarded by mutex */
	uint64_t unsendable_report_by_build;
	uint64_t unsendable_report_by_size;
};

/* IN delay - how long to wait, caller must hold the mutex */
static void _wait_locked(uds_writer_t *writer, timespec_t delay)
{
	timespec_t ts = { 0 };

	if (writer->shutdown)
		return;

	ts = timespec_add(timespec_now(), delay);

	slurm_cond_timedwait(&writer->cond, &writer->mutex, &ts);
}

static void _disconnect(uds_writer_t *writer)
{
	char *out = NULL;
	size_t out_len = 0;

	if (writer->fd >= 0) {
		(void) close(writer->fd);
		writer->fd = -1;
	}

	slurm_mutex_lock(&writer->mutex);
	writer->connected = false;
	slurm_cond_broadcast(&writer->cond);
	slurm_mutex_unlock(&writer->mutex);

	if (!writer->out)
		return;

	out = writer->out;
	out_len = writer->out_len;

	/*
	 * A datagram is sent whole or not at all, so a record still held here
	 * was never partly delivered and can go back to the front of the
	 * queue to be sent after reconnecting. There is no half a record.
	 */
	(void) relay_io_ring_requeue(writer->ring, out, out_len);

	/*
	 * "out" now belongs to the ring or has been freed. Clear it only
	 * afterwards, so a concurrent flush never sees a moment where the
	 * record is in neither place and concludes the queue has drained.
	 */
	slurm_mutex_lock(&writer->mutex);
	writer->out = NULL;
	writer->out_len = 0;
	slurm_cond_broadcast(&writer->cond);
	slurm_mutex_unlock(&writer->mutex);
}

/*
 * Refuse a socket path other users can create entries in.
 *
 * _socket_is_trusted() checks whoever is bound to the path, but it cannot do
 * so without a window: a datagram socket carries no peer credentials, so the
 * check has to happen before the connect rather than after it. A directory
 * only root or SlurmdUser can write closes that window, because then nobody
 * else can put anything at the path to begin with.
 *
 * Components above the directory are the site's to get right, as they are for
 * SlurmdSpoolDir.
 *
 * IN path - socket path whose directory to check
 * IN st - stat pointer to pass back uid information
 * RET TRUSTED when the directory is safe to keep the socket in
 */
static trust_state_t _dir_is_trusted(const char *path, struct stat *st)
{
	char *dir = xstrdup(path);
	char *sep = xstrrchr(dir, '/');
	trust_state_t rc = UNTRUSTED;

	xassert(st);

	if (!sep) {
		/* relative to wherever the daemon happens to be */
		xfree(dir);
		dir = xstrdup(".");
	} else if (sep == dir) {
		sep[1] = '\0'; /* directly under / */
	} else {
		sep[0] = '\0';
	}

	if (stat(dir, st) < 0) {
		rc = DIR_STAT_FAILED;
		if (errno == ENOENT)
			rc = DIR_NO_EXIST;
		goto done;
	}

	if ((st->st_uid != 0) && (st->st_uid != slurm_conf.slurmd_user_id)) {
		rc = DIR_BAD_OWNER;
		goto done;
	}

	/*
	 * The sticky bit is no help here. It stops a user removing the
	 * scraper's socket, but not binding the path first while the scraper
	 * is down, which is all an attacker needs.
	 */
	if (st->st_mode & (S_IWGRP | S_IWOTH)) {
		rc = DIR_GRP_OTHER_WRITE;
		goto done;
	}

	rc = TRUSTED;
done:
	xfree(dir);

	return rc;
}

/*
 * Refuse to send to a socket an unprivileged user could have bound.
 *
 * The daemons connect as root, so the kernel's own permission checks do not
 * apply to them and a symlink would be followed. Whoever is bound to the path
 * receives the output of every relayed job on the node.
 *
 * Checked on every attempt rather than once at startup. The scraper is
 * allowed to come up after the daemons, so there is usually nothing to look
 * at when the writer is created, and the path is free for anyone to take for
 * as long as the scraper is down.
 *
 * RET TRUSTED when the socket is safe to send to
 */
static trust_state_t _socket_is_trusted(const char *path, struct stat *st)
{
	trust_state_t rc = UNTRUSTED;

	/* lstat(), so that a symlink is rejected rather than followed */
	if (lstat(path, st) < 0) {
		rc = SOCKET_STAT_FAILED;
		/* socket doesn't exist yet, the normal wait for the scraper */
		if (errno == ENOENT)
			rc = SOCKET_NO_EXIST;
	} else if (!S_ISSOCK(st->st_mode)) {
		rc = SOCKET_NOT_A_SOCKET;
	} else if ((st->st_uid != 0) &&
		   (st->st_uid != slurm_conf.slurmd_user_id)) {
		rc = SOCKET_BAD_OWNER;
	} else {
		rc = TRUSTED;
	}

	return rc;
}

/*
 * Check if the socket path is safe to connect to
 */
static bool _dir_socket_is_trusted(uds_writer_t *writer)
{
	struct stat st = { 0 };
	trust_state_t trust_state;
	const char *dir_err = "directory holding the ";
	/*
	 * info() rather than error() in slurmstepd, for the reason given
	 * above _producer_drops_report()
	 */
	log_level_t level = running_in_slurmstepd() ?
		LOG_LEVEL_INFO :
		LOG_LEVEL_ERROR;
	const char *path = writer->path;

	trust_state = _dir_is_trusted(path, &st);
	if (trust_state == TRUSTED)
		trust_state = _socket_is_trusted(path, &st);

	/* skip logging again for the same reason */
	if (trust_state == writer->socket_trust)
		return (trust_state == TRUSTED);

	switch (trust_state) {
	case DIR_NO_EXIST:
	case SOCKET_NO_EXIST:
		/* the scraper is not running, not an error */
		info("[%s] not connecting, the %srelay_io socket is not present",
		     path, ((trust_state == DIR_NO_EXIST) ? dir_err : ""));
		break;
	case DIR_STAT_FAILED:
	case SOCKET_STAT_FAILED:
		log_var(level, "[%s] not connecting, cannot stat the %srelay_io socket: %m",
			path,
			((trust_state == DIR_STAT_FAILED) ? dir_err : ""));
		break;
	case DIR_BAD_OWNER:
	case SOCKET_BAD_OWNER:
		log_var(level, "[%s] not connecting, the %srelay_io socket is owned by uid %u, it must be owned by root or SlurmdUser",
			path, ((trust_state == DIR_BAD_OWNER) ? dir_err : ""),
			st.st_uid);
		break;
	case DIR_GRP_OTHER_WRITE:
		log_var(level, "[%s] not connecting, the directory holding the relay_io socket has group or other write permissions",
			path);
		break;
	case SOCKET_NOT_A_SOCKET:
		log_var(level, "[%s] not connecting, the relay_io socket is not a socket",
			path);
		break;
	case UNTRUSTED:
		/* this shouldn't happen */
		log_var(level, "[%s] not connecting, unspecified error marking relay_io socket as untrusted",
			path);
		break;
	case TRUSTED:
		/* success */
		break;
	default:
		fatal("Invalid relay_io socket trust state (%d)", trust_state);
	}

	/* UNTRUSTED should only be set on writer creation */
	if ((writer->socket_trust != UNTRUSTED) && (trust_state == TRUSTED))
		debug("[%s] relay_io socket trust state resolved", path);

	writer->socket_trust = trust_state;
	return (trust_state == TRUSTED);
}

/*
 * Try to connect.
 * RET true if connected
 */
static bool _connect(uds_writer_t *writer)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	socklen_t len;
	int fd = -1;

	/* uds_writer_create() refuses a path that does not fit */
	xassert(strlen(writer->path) < sizeof(addr.sun_path));

	if (!_dir_socket_is_trusted(writer))
		return false;

	strlcpy(addr.sun_path, writer->path, sizeof(addr.sun_path));
	len = sockaddr_fixlen((struct sockaddr *) &addr, sizeof(addr));

	/*
	 * Datagrams, so that one record is one message. The far end needs no
	 * framing to find record boundaries, and a record is delivered whole
	 * or not at all.
	 *
	 * Non-blocking, so that a full receive queue comes back as EAGAIN
	 * rather than parking this thread: uds_writer_destroy() joins it with
	 * no timeout, and a blocked send() here would hang step teardown.
	 */
	if ((fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
			 0)) < 0) {
		/*
		 * info() rather than error(), for the reason given above
		 * _producer_drops_report(). Worth saying at all, unlike the
		 * other connect failures, because a descriptor shortage stops
		 * the relay for as long as it lasts and is not self
		 * announcing.
		 */
		info("[%s] relay socket() failed: %m", writer->path);
		return false;
	}

	/*
	 * No handshake for a datagram socket. This only records the default
	 * destination and fails outright when nothing is bound to the path,
	 * which is expected whenever the far end is not up yet, so it is only
	 * visible under the debug flag rather than filling the log at every
	 * backoff interval.
	 */
	if (connect(fd, (struct sockaddr *) &addr, len) < 0) {
		log_flag(PROTOCOL, "%s: [%s] connect() failed: %m",
			 __func__, writer->path);
		(void) close(fd);
		return false;
	}

	writer->fd = fd;
	writer->backoff = BACKOFF_MIN;

	slurm_mutex_lock(&writer->mutex);
	writer->connected = true;
	slurm_mutex_unlock(&writer->mutex);

	log_flag(PROTOCOL, "%s: [%s] connected", __func__, writer->path);

	return true;
}

/*
 * Account for a record that will never be sent.
 *
 * Updates writer->min_dropped_msg_size if due_to_size is true, and
 * sets unsendable_report_* if the drops are due to be logged.
 *
 * Return true if the drops should be logged.
 */
static bool _unsendable_drop(uds_writer_t *writer, bool due_to_size)
{
	uint64_t total;

	slurm_mutex_lock(&writer->mutex);
	if (due_to_size) {
		writer->unsendable_by_size++;
		writer->min_dropped_msg_size =
			MIN(writer->out_len, writer->min_dropped_msg_size);
	} else {
		writer->unsendable_by_build++;
	}

	total = writer->unsendable_by_size + writer->unsendable_by_build;

	if ((total == 1) || !(total % UNSENDABLE_LOG_INTERVAL)) {
		writer->unsendable_report_by_size = writer->unsendable_by_size;
		writer->unsendable_report_by_build =
			writer->unsendable_by_build;
	} else {
		total = 0;
	}
	slurm_mutex_unlock(&writer->mutex);

	return total;
}

/*
 * Report what the producers counted but did not log.
 *
 * The producer is the relay thread, and it must not wait on the log lock. A
 * slurmstepd thread can hold that lock while it writes to a full pipe whose
 * reader waits for the relay thread to make room.
 *
 * At info() rather than error() on purpose: slurmstepd's stderr is the batch
 * job's own output file, and _slurmd_job_log_init() passes error() and above
 * through to it, so complaining there would corrupt the output of the very job
 * being relayed. Rate limited because a task writing long binary lines would
 * otherwise produce one line of log per line of output.
 */
static void _producer_drops_report(uds_writer_t *writer)
{
	uint64_t dropped = relay_io_ring_drop_report(writer->ring);
	uint64_t msg_size = 0;
	uint64_t unbuilt = 0;
	size_t min_drop_size = 0;

	slurm_mutex_lock(&writer->mutex);
	msg_size = writer->unsendable_report_by_size;
	min_drop_size = writer->min_dropped_msg_size;
	unbuilt = writer->unsendable_report_by_build;
	writer->unsendable_report_by_size = 0;
	writer->unsendable_report_by_build = 0;
	slurm_mutex_unlock(&writer->mutex);

	if (dropped)
		info("dropped %"PRIu64" record%s so far, the relay is not draining fast enough",
		     dropped, ((dropped == 1) ? "" : "s"));
	if (unbuilt)
		info("[%s] relay dropped %"PRIu64" record%s that could not be built",
		     writer->path, unbuilt, ((unbuilt == 1) ? "" : "s"));
	if (msg_size)
		info("[%s] relay dropped %"PRIu64" record%s due to EMSGSIZE, the smallest being %zu bytes",
		     writer->path, msg_size, ((msg_size == 1) ? "" : "s"),
		     min_drop_size);
}

/*
 * Take the next record from the ring and make it the buffer being written.
 * RET true if there is now something to write
 */
static bool _next_out(uds_writer_t *writer)
{
	size_t len = 0;
	char *data = NULL;

	/*
	 * Dequeue and publish have to look atomic to uds_writer_flush(), which
	 * calls the writer drained once the ring is empty and out is NULL. A
	 * record caught between the two would be reported as flushed and then
	 * dropped by the fini that follows. Taking writer->mutex before the
	 * ring lock is the order flush already uses, so this cannot invert.
	 */
	slurm_mutex_lock(&writer->mutex);
	if ((data = relay_io_ring_dequeue(writer->ring, &len))) {
		writer->out = data;
		writer->out_len = len;
	}
	slurm_mutex_unlock(&writer->mutex);

	return data;
}

/* Write as much of the current buffer as the socket will take. */
static void _write_out(uds_writer_t *writer)
{
	struct pollfd pfd = {
		.fd = writer->fd,
		.events = POLLOUT,
	};
	ssize_t n;
	int rc;

	if ((rc = poll(&pfd, 1, timespec_to_msec(POLL_WAIT))) < 0) {
		if (errno == EINTR)
			return;
		log_flag(PROTOCOL, "%s: [%s] poll() failed: %m",
			 __func__, writer->path);
		_disconnect(writer);
		return;
	}

	if (!rc)
		return; /* still not writable, try again */

	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
		log_flag(PROTOCOL, "%s: [%s] peer went away",
			 __func__, writer->path);
		_disconnect(writer);
		return;
	}

	/*
	 * MSG_NOSIGNAL rather than write(), so that a reader that vanished
	 * mid-write cannot raise SIGPIPE in a daemon that may not be ignoring
	 * it at that moment.
	 */
	/*
	 * One writable wakeup drains as many records as the kernel will take.
	 * The receive queue holds only net.unix.max_dgram_qlen datagrams, so
	 * sending a single record per poll() would leave the ring evicting
	 * records that the far end was ready to accept.
	 */
	while (writer->out) {
		bool shutdown;

		slurm_mutex_lock(&writer->mutex);
		shutdown = writer->shutdown;
		slurm_mutex_unlock(&writer->mutex);

		/*
		 * Draining as much as the kernel will take is what keeps the
		 * ring from evicting, but it must not hold the join open for
		 * however long that takes. Leave the record in hand for
		 * _disconnect() to requeue.
		 */
		if (shutdown)
			return;

		n = send(writer->fd, writer->out, writer->out_len,
			 MSG_NOSIGNAL);

		if (n < 0) {
			if (errno == EINTR)
				continue;

			/*
			 * The receive queue is full. Keep this record and try
			 * again: dropping is the ring's job, so that what is
			 * lost is the oldest record rather than whichever one
			 * happened to be in hand.
			 */
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				return;

			/*
			 * Too large for a datagram, so it will never be sent
			 * and retrying would wedge everything behind it, so
			 * drop it instead.
			 */
			if (errno == EMSGSIZE) {
				(void) _unsendable_drop(writer, true);
			} else {
				log_flag(PROTOCOL, "%s: [%s] send() failed: %m",
					 __func__, writer->path);
				_disconnect(writer);
				return;
			}
		}

		/* sent whole or discarded, either way this record is done */
		slurm_mutex_lock(&writer->mutex);
		xfree(writer->out);
		writer->out_len = 0;
		/* let a waiting flush() see that the queue moved */
		slurm_cond_broadcast(&writer->cond);
		slurm_mutex_unlock(&writer->mutex);

		(void) _next_out(writer);
	}
}

static void *_writer_thread(void *arg)
{
	uds_writer_t *writer = arg;

	xassert(writer->magic == WRITER_MAGIC);

	while (true) {
		/* make sure to report drops from last iteration on shutdown */
		_producer_drops_report(writer);

		slurm_mutex_lock(&writer->mutex);
		if (writer->shutdown) {
			slurm_mutex_unlock(&writer->mutex);
			break;
		}
		slurm_mutex_unlock(&writer->mutex);

		if (writer->fd < 0) {
			bool too_soon;

			/*
			 * Check and wait under one lock, so that a flush
			 * resetting next_connect cannot broadcast in between
			 * and leave this thread asleep until the next poll.
			 */
			slurm_mutex_lock(&writer->mutex);
			if ((too_soon = timespec_is_after(writer->next_connect,
							  timespec_now())))
				_wait_locked(writer, IDLE_WAIT);
			slurm_mutex_unlock(&writer->mutex);

			if (too_soon)
				continue;

			if (!_connect(writer)) {
				slurm_mutex_lock(&writer->mutex);
				writer->connect_tries++;
				writer->next_connect =
					timespec_add(timespec_now(),
						     writer->backoff);
				slurm_cond_broadcast(&writer->cond);
				_wait_locked(writer, writer->backoff);
				slurm_mutex_unlock(&writer->mutex);

				writer->backoff = timespec_add(writer->backoff,
							       writer->backoff);
				if (timespec_is_after(writer->backoff,
						      BACKOFF_MAX))
					writer->backoff = BACKOFF_MAX;
				continue;
			}
		}

		if (!writer->out && !_next_out(writer)) {
			/* nothing queued, sleep until something is */
			slurm_mutex_lock(&writer->mutex);
			if (!writer->shutdown)
				_wait_locked(writer, IDLE_WAIT);
			slurm_mutex_unlock(&writer->mutex);
			continue;
		}

		_write_out(writer);
	}

	_disconnect(writer);

	slurm_mutex_lock(&writer->mutex);
	writer->running = false;
	slurm_cond_broadcast(&writer->cond);
	slurm_mutex_unlock(&writer->mutex);

	return NULL;
}

extern uds_writer_t *uds_writer_create(const char *path, size_t max_bytes)
{
	struct sockaddr_un addr;
	uds_writer_t *writer = NULL;

	if (!path || !path[0]) {
		error("no socket path given");
		return NULL;
	}

	/*
	 * slurmstepd changes into the job's working directory and reconnects
	 * at any time, so a relative path would resolve somewhere the user
	 * controls.
	 */
	if (path[0] != '/') {
		error("[%s] socket path must be absolute", path);
		return NULL;
	}

	/*
	 * The path will not get shorter so reject here. Returning NULL leaves
	 * the plugin unloaded.
	 */
	if (strlen(path) >= sizeof(addr.sun_path)) {
		error("[%s] path is too long for a UNIX socket, it must be under %zu characters",
		      path, sizeof(addr.sun_path));
		return NULL;
	}

	writer = xmalloc(sizeof(*writer));
	*writer = (uds_writer_t) {
		.magic = WRITER_MAGIC,
		.path = xstrdup(path),
		.ring = relay_io_ring_create(max_bytes),
		.fd = -1,
		.backoff = BACKOFF_MIN,
		.running = true,
		.min_dropped_msg_size = SIZE_MAX,
	};
	slurm_mutex_init(&writer->mutex);
	slurm_cond_init(&writer->cond, NULL);

	/*
	 * The first attempt is made here rather than left to the thread, so
	 * that it happens on the caller's terms. slurmstepd calls this while
	 * it is still privileged, but glibc applies a later seteuid() to
	 * every thread, so an attempt from the writer thread can land in the
	 * window where the step runs as the job's user and be refused by a
	 * socket only root may write. Reconnects are still subject to that.
	 */
	(void) _connect(writer);

	slurm_thread_create(NULL, &writer->thread, _writer_thread, writer);

	return writer;
}

extern void uds_writer_destroy(uds_writer_t **writer_ptr, uint64_t *dropped_ptr)
{
	uds_writer_t *writer = NULL;
	uint64_t dropped = 0;
	uint32_t queued = 0;

	if (!writer_ptr || !(writer = *writer_ptr))
		return;

	xassert(writer->magic == WRITER_MAGIC);

	slurm_mutex_lock(&writer->mutex);
	writer->shutdown = true;
	slurm_cond_broadcast(&writer->cond);
	slurm_mutex_unlock(&writer->mutex);

	slurm_thread_join(writer->thread);

	/*
	 * Read the tally now rather than before the join: the writer thread is
	 * finished, so anything it lost on the way out is already counted.
	 */
	relay_io_ring_stats(writer->ring, NULL, &queued, &dropped);
	dropped += writer->unsendable_by_size + writer->unsendable_by_build;

	/*
	 * Records still in the ring, and the one in flight, die with this
	 * writer. They were never evicted, so the ring has not counted them.
	 */
	dropped += queued;
	if (writer->out)
		dropped++;
	if (dropped_ptr)
		*dropped_ptr = dropped;

	relay_io_ring_destroy(&writer->ring);
	slurm_mutex_destroy(&writer->mutex);
	slurm_cond_destroy(&writer->cond);
	xfree(writer->out);
	xfree(writer->path);
	writer->magic = 0;
	xfree(writer);

	*writer_ptr = NULL;
}

extern void uds_writer_discard(uds_writer_t *writer)
{
	xassert(writer);
	xassert(writer->magic == WRITER_MAGIC);

	/* the writer thread logs it, see _producer_drops_report() */
	if (_unsendable_drop(writer, false))
		slurm_cond_broadcast(&writer->cond);
}

extern void uds_writer_submit(uds_writer_t *writer, char *data, size_t len)
{
	xassert(writer);
	xassert(writer->magic == WRITER_MAGIC);

	if (!data)
		return;

	relay_io_ring_push(writer->ring, data, len);

	slurm_mutex_lock(&writer->mutex);
	slurm_cond_broadcast(&writer->cond);
	slurm_mutex_unlock(&writer->mutex);
}

extern int uds_writer_flush(uds_writer_t *writer, timespec_t timeout)
{
	int rc = SLURM_ERROR;
	bool asked = false;
	uint64_t tries = 0;
	timespec_t start = { 0 }, deadline = { 0 };

	xassert(writer);
	xassert(writer->magic == WRITER_MAGIC);

	start = timespec_now();
	deadline = timespec_add(start, timeout);

	slurm_mutex_lock(&writer->mutex);
	while (writer->running) {
		size_t bytes = 0;

		relay_io_ring_stats(writer->ring, &bytes, NULL, NULL);
		if (!bytes && !writer->out) {
			rc = SLURM_SUCCESS;
			break;
		}

		/*
		 * The writer thread is on its way out, so nothing is going to
		 * send what is left. _wait_locked() returns at once once this
		 * is set, so waiting here would only spin out the timeout.
		 */
		if (writer->shutdown)
			break;

		/*
		 * Nothing is connected, so have the writer thread try to
		 * connect one last time, skipping its backoff, before giving
		 * up. If that fails then there is no where to send the queued
		 * data. Every relayed step ends with a flush, and a scraper
		 * that is down must not add the whole timeout to each of them.
		 */
		if (!writer->connected) {
			if (!asked) {
				asked = true;
				tries = writer->connect_tries;
				writer->next_connect = (timespec_t) { 0 };
			} else if (writer->connect_tries != tries) {
				break;
			}
		}

		if (!timespec_is_after(deadline, timespec_now()))
			break;

		slurm_cond_broadcast(&writer->cond);
		_wait_locked(writer, POLL_WAIT);
	}
	slurm_mutex_unlock(&writer->mutex);

	if (rc != SLURM_SUCCESS) {
		uint32_t count = 0;

		relay_io_ring_stats(writer->ring, NULL, &count, NULL);
		/*
		 * Whoever asked for the drain is about to stop or re-exec, so
		 * these records are not going to be sent by anyone. Say so
		 * rather than let them disappear with the process. The
		 * duration is the real one, which is well short of the
		 * timeout when the far end was already known to be down.
		 */
		info("[%s] relay gave up draining after %s, losing %u queued record%s",
		     writer->path, TIMESPEC_ELAPSED_STR(start), count,
		     ((count == 1) ? "" : "s"));
	}

	return rc;
}
