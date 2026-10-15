/*****************************************************************************\
 *  ring_buffer.h - bounded, lossy FIFO of formatted records
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

#ifndef _RELAY_IO_RING_BUFFER_H
#define _RELAY_IO_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bounded, lossy FIFO of formatted records, evicted whole and oldest first.
 * One producer may push while one consumer dequeues.
 */
typedef struct relay_io_ring_s relay_io_ring_t;

/*
 * Create a ring.
 * IN max_bytes - budget, in bytes, of retained record data
 * RET new ring, free with relay_io_ring_destroy()
 */
extern relay_io_ring_t *relay_io_ring_create(size_t max_bytes);

/*
 * Free a ring and everything still in it.
 * IN/OUT ring_ptr - ring to free, set to NULL
 */
extern void relay_io_ring_destroy(relay_io_ring_t **ring_ptr);

/*
 * Append a record, evicting the oldest records to stay in budget. Never
 * blocks, fails or logs.
 * IN ring - ring to append to
 * IN data - record, ownership passes to the ring, must be xmalloc'd
 * IN len - bytes in data
 */
extern void relay_io_ring_push(relay_io_ring_t *ring, char *data, size_t len);

/*
 * Return an undelivered record to the front of the ring, or drop it if it no
 * longer fits.
 * IN ring - ring to return the record to
 * IN data - record, ownership passes to the ring either way, must be
 *	xmalloc'd. It is freed if the record is dropped.
 * IN len - bytes in data
 * RET true if requeued, false if dropped
 */
extern bool relay_io_ring_requeue(relay_io_ring_t *ring, char *data,
				  size_t len);

/*
 * Remove the oldest record from the ring.
 * IN ring - ring to take from
 * OUT len_ptr - bytes in the returned record, may be NULL
 * RET record (must xfree()) or NULL if the ring is empty
 */
extern char *relay_io_ring_dequeue(relay_io_ring_t *ring, size_t *len_ptr);

/*
 * Report ring occupancy.
 * IN ring - ring to inspect
 * OUT bytes_ptr - bytes currently retained, may be NULL
 * OUT count_ptr - records currently retained, may be NULL
 * OUT dropped_ptr - records evicted since creation, may be NULL
 */
extern void relay_io_ring_stats(relay_io_ring_t *ring, size_t *bytes_ptr,
				uint32_t *count_ptr, uint64_t *dropped_ptr);

/*
 * Take the drop count that is due to be reported: the first drop, then one at
 * regular intervals.
 * IN ring - ring to check
 * RET cumulative drop count to report, 0 if none is due
 */
extern uint64_t relay_io_ring_drop_report(relay_io_ring_t *ring);

#endif
