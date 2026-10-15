/*****************************************************************************\
 *  uds_writer.h - non-blocking writer for a UNIX domain socket
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

#ifndef _RELAY_IO_UDS_WRITER_H
#define _RELAY_IO_UDS_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/common/slurm_time.h"

/*
 * Drains a bounded ring into a UNIX domain socket from its own thread, so that
 * submitting a record never blocks. If the far end is absent or stops reading,
 * the oldest records are dropped and the connection is retried with a capped
 * backoff.
 */
typedef struct uds_writer_s uds_writer_t;

/*
 * Create a writer and start its thread.
 * IN path - socket to connect to, does not need to exist yet
 * IN max_bytes - ring budget in bytes
 * RET new writer, or NULL if path is unusable
 */
extern uds_writer_t *uds_writer_create(const char *path, size_t max_bytes);

/*
 * Stop the writer thread and free it, discarding anything still queued.
 * IN/OUT writer_ptr - writer to free, set to NULL
 * OUT dropped_ptr - records lost while this writer ran: evicted, too large to
 *	send, or discarded by the caller, counted after the thread stopped. NULL
 *	if unwanted.
 */
extern void uds_writer_destroy(uds_writer_t **writer_ptr,
			       uint64_t *dropped_ptr);

/*
 * Queue a record. Never blocks.
 * IN writer - writer to queue on
 * IN data - record, ownership passes to the writer, must be xmalloc'd
 * IN len - bytes in data
 */
extern void uds_writer_submit(uds_writer_t *writer, char *data, size_t len);

/*
 * Count a record the caller could not hand over in the writer's drop total.
 * IN writer - writer to account against
 */
extern void uds_writer_discard(uds_writer_t *writer);

/*
 * Wait for the queue to drain.
 * IN writer - writer to drain
 * IN timeout - how long to wait before giving up
 * RET SLURM_SUCCESS if everything was written, SLURM_ERROR on timeout
 */
extern int uds_writer_flush(uds_writer_t *writer, timespec_t timeout);

#endif
