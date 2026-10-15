/*****************************************************************************\
 *  ring_buffer.c - bounded, lossy FIFO of formatted records
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

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "ring_buffer.h"

#define RING_MAGIC 0x5219a10

/* how many drops between reports, after the first one */
#define RING_DROP_LOG_INTERVAL 1000

typedef struct {
	char *data;
	size_t len;
} ring_rec_t;

/*
 * Records are stored whole, so that eviction never leaves the front of a
 * record behind to be emitted as a truncated document.
 */
struct relay_io_ring_s {
	int magic; /* DO NOT ALPHABETIZE */
	size_t bytes; /* retained record data */
	uint64_t drop_report; /* dropped count due to be reported, 0 if none */
	uint64_t dropped; /* cumulative evictions */
	size_t max_bytes;
	pthread_mutex_t mutex;
	list_t *records;
};

static void _ring_rec_destroy(void *x)
{
	ring_rec_t *rec = x;

	if (!rec)
		return;

	xfree(rec->data);
	xfree(rec);
}

/*
 * Count one dropped record and decide whether it is due to be reported.
 *
 * Reporting only at teardown hides a relay that has been losing output for the
 * whole life of a job, so the first drop is always reported and then one in
 * every RING_DROP_LOG_INTERVAL. Reporting every drop would replace the lost
 * output with an equal volume of log lines.
 *
 * Caller must hold ring->mutex.
 *
 * IN ring - ring the record was dropped from
 */
static void _ring_drop(relay_io_ring_t *ring)
{
	ring->dropped++;

	if ((ring->dropped == 1) || !(ring->dropped % RING_DROP_LOG_INTERVAL))
		ring->drop_report = ring->dropped;
}

extern relay_io_ring_t *relay_io_ring_create(size_t max_bytes)
{
	relay_io_ring_t *ring = xmalloc(sizeof(*ring));

	*ring = (relay_io_ring_t) {
		.magic = RING_MAGIC,
		.records = list_create(_ring_rec_destroy),
		.max_bytes = max_bytes,
	};
	slurm_mutex_init(&ring->mutex);

	return ring;
}

extern void relay_io_ring_destroy(relay_io_ring_t **ring_ptr)
{
	relay_io_ring_t *ring = NULL;

	if (!ring_ptr || !(ring = *ring_ptr))
		return;

	xassert(ring->magic == RING_MAGIC);

	FREE_NULL_LIST(ring->records);
	slurm_mutex_destroy(&ring->mutex);
	ring->magic = 0;
	xfree(ring);

	*ring_ptr = NULL;
}

/*
 * Push never logs, so that a thread which must not wait on the log lock can
 * call it. The consumer reports drops, see relay_io_ring_drop_report(). A
 * record larger than the whole budget is still kept rather than evicting
 * itself, so that an oversized line is delivered rather than lost.
 */
extern void relay_io_ring_push(relay_io_ring_t *ring, char *data, size_t len)
{
	ring_rec_t *rec = NULL;

	xassert(ring);
	xassert(ring->magic == RING_MAGIC);

	if (!data)
		return;

	if (!len) {
		xfree(data);
		return;
	}

	rec = xmalloc(sizeof(*rec));
	rec->data = data;
	rec->len = len;

	slurm_mutex_lock(&ring->mutex);

	list_enqueue(ring->records, rec);
	ring->bytes += len;

	/*
	 * Keep at least one record, so that a record bigger than the whole
	 * budget is delivered instead of evicting itself on the way in.
	 */
	while ((ring->bytes > ring->max_bytes) &&
	       (list_count(ring->records) > 1)) {
		ring_rec_t *old = list_dequeue(ring->records);

		ring->bytes -= old->len;
		_ring_drop(ring);
		_ring_rec_destroy(old);
	}

	slurm_mutex_unlock(&ring->mutex);
}

/*
 * Drop the returned record rather than evict records that arrived while it was
 * away, which have at least as much claim on the space. An empty ring still
 * takes an oversized record, as push does, so that whether it is delivered
 * does not depend on a reconnect.
 */
extern bool relay_io_ring_requeue(relay_io_ring_t *ring, char *data, size_t len)
{
	ring_rec_t *rec = NULL;
	bool kept = false;

	xassert(ring);
	xassert(ring->magic == RING_MAGIC);

	if (!data)
		return false;

	if (!len) {
		xfree(data);
		return false;
	}

	slurm_mutex_lock(&ring->mutex);

	/*
	 * Accept it if it fits, or if the ring is empty. The second case
	 * mirrors relay_io_ring_push(), so a record larger than the whole
	 * budget behaves the same whether or not it has been through a
	 * failed delivery.
	 */
	if (((ring->bytes + len) <= ring->max_bytes) ||
	    !list_count(ring->records)) {
		rec = xmalloc(sizeof(*rec));
		rec->data = data;
		rec->len = len;
		list_push(ring->records, rec);
		ring->bytes += len;
		kept = true;
	} else {
		_ring_drop(ring);
	}

	slurm_mutex_unlock(&ring->mutex);

	if (!kept)
		xfree(data);

	return kept;
}

/*
 * The record leaves the ring entirely, so a concurrent push cannot evict it
 * while the consumer is part way through writing it.
 */
extern char *relay_io_ring_dequeue(relay_io_ring_t *ring, size_t *len_ptr)
{
	ring_rec_t *rec = NULL;
	char *data = NULL;
	size_t len = 0;

	xassert(ring);
	xassert(ring->magic == RING_MAGIC);

	slurm_mutex_lock(&ring->mutex);

	if ((rec = list_dequeue(ring->records))) {
		data = rec->data;
		len = rec->len;
		ring->bytes -= len;
		xfree(rec);
	}

	slurm_mutex_unlock(&ring->mutex);

	if (len_ptr)
		*len_ptr = len;

	return data;
}

extern void relay_io_ring_stats(relay_io_ring_t *ring, size_t *bytes_ptr,
				uint32_t *count_ptr, uint64_t *dropped_ptr)
{
	xassert(ring);
	xassert(ring->magic == RING_MAGIC);

	slurm_mutex_lock(&ring->mutex);

	if (bytes_ptr)
		*bytes_ptr = ring->bytes;
	if (count_ptr)
		*count_ptr = list_count(ring->records);
	if (dropped_ptr)
		*dropped_ptr = ring->dropped;

	slurm_mutex_unlock(&ring->mutex);
}

extern uint64_t relay_io_ring_drop_report(relay_io_ring_t *ring)
{
	uint64_t report = 0;

	xassert(ring);
	xassert(ring->magic == RING_MAGIC);

	slurm_mutex_lock(&ring->mutex);
	report = ring->drop_report;
	ring->drop_report = 0;
	slurm_mutex_unlock(&ring->mutex);

	return report;
}
