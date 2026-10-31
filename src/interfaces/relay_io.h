/*****************************************************************************\
 *  relay_io.h - IO relay interface
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

#ifndef _INTERFACES_RELAY_IO_H
#define _INTERFACES_RELAY_IO_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/slurm_time.h"

/*
 * Version of the record schema documented in relay_io.shtml. Bump this
 * whenever the meaning of an existing field changes, so that a consumer can
 * tell one layout from another.
 */
#define RELAY_IO_SCHEMA_VERSION 1

typedef enum {
	RELAY_IO_STREAM_STDOUT = 0,
	RELAY_IO_STREAM_STDERR,
} relay_io_stream_t;

typedef enum {
	RELAY_IO_EVENT_LOG = 0,
	RELAY_IO_EVENT_JOB_START,
	RELAY_IO_EVENT_JOB_END,
	RELAY_IO_EVENT_STEP_START,
	RELAY_IO_EVENT_STEP_END,
} relay_io_event_t;

/*
 * Identity carried on every record, so a consumer can tie an event back to
 * where it was produced. All pointers are borrowed and must outlive the call.
 */
typedef struct {
	const char *account; /* job events only, may be NULL */
	const char *cluster; /* ClusterName */
	uint32_t job_id;
	const char *node; /* node the event was produced on */
	const char *partition; /* job events only, may be NULL */
	uint16_t restart_count; /* times the job has been requeued */
	sluid_t sluid;
	const char *user_name; /* submitting user, may be NULL */
} relay_io_ident_t;

/*
 * A single event. For RELAY_IO_EVENT_LOG the step, task, stream and body
 * fields describe one line, or one chunk of unbuffered output. For the
 * lifecycle events only ident, type and time are meaningful, except that the
 * step events also carry step_id.
 */
typedef struct {
	const char *body; /* borrowed, need not be NULL terminated */
	size_t body_len;
	relay_io_ident_t ident;
	bool partial; /* flushed without a trailing newline */
	uint32_t step_id; /* SLURM_BATCH_SCRIPT, or the step number */
	relay_io_stream_t stream;
	uint32_t task_id_global;
	uint32_t task_id_local;
	timespec_t time; /* when the event was produced */
	relay_io_event_t type;
	bool unbuffered; /* produced by a step using --unbuffered */
} relay_io_record_t;

/*
 * Load and initialize the configured relay_io plugin, if RelayIOPlugin is set.
 * IN spooldir - SlurmdSpoolDir, already %n expanded, used to locate sockets
 * RET SLURM_SUCCESS or error
 */
extern int relay_io_g_init(const char *spooldir);

/*
 * Unload the relay_io plugin.
 * RET SLURM_SUCCESS or error
 */
extern int relay_io_g_fini(void);

/*
 * Queue an event for relaying. Never blocks, and may drop the record.
 * IN record - event to relay, borrowed for the duration of the call
 * OUT consumed_ptr - bytes of record->body this record carries, never 0 for a
 *	non-empty body. The caller passes the rest in a further record. NULL if
 *	the caller has no body to place.
 * RET SLURM_SUCCESS or error, which does not indicate delivery
 */
extern int relay_io_g_record(const relay_io_record_t *record,
			     size_t *consumed_ptr);

/*
 * Check whether RelayIOPlugin names a plugin to load, without loading it.
 * RET true if RelayIOPlugin is set and is not "none"
 */
extern bool relay_io_configured(void);

/*
 * Resolve a job's --relay-io request against this daemon's configuration.
 * IN requested - value the job asked for, NO_VAL16 if it did not ask
 * RET true if this job's output should be relayed
 */
extern bool relay_io_requested(uint16_t requested);

/*
 * Narrow a job's resolved relay_io setting to one step on this node.
 * IN step_id - id of the step in question
 * IN relay_io - what slurmctld resolved for the job
 * RET true if this step's output should be relayed, which is never true for
 *	the extern step or on a node with no plugin configured
 */
extern bool relay_io_step_requested(uint32_t step_id, bool relay_io);

/*
 * Emit a job start record. The caller emits it once per node, and only for a
 * job that relays.
 * IN ident - identity of the job, copied
 */
extern void relay_io_g_job_start(const relay_io_ident_t *ident);

/*
 * Emit a job end record. The caller pairs it with relay_io_g_job_start().
 * IN ident - identity of the job, copied
 */
extern void relay_io_g_job_end(const relay_io_ident_t *ident);

/* One task's stdout or stderr, turned into records on the relay thread */
typedef struct relay_io_src_s relay_io_src_t;

/*
 * Register a source of task output.
 * IN ident - identity every record from this source carries, copied
 * IN step_id - step the task belongs to
 * IN task_id_global / task_id_local - task the output came from
 * IN stream - stdout or stderr
 * IN unbuffered - true for --unbuffered, to cut output into fixed size chunks
 *	rather than lines
 * RET source, or NULL when nothing is being relayed
 */
extern relay_io_src_t *relay_io_g_src_create(const relay_io_ident_t *ident,
					     uint32_t step_id,
					     uint32_t task_id_global,
					     uint32_t task_id_local,
					     relay_io_stream_t stream,
					     bool unbuffered);

/*
 * Hand task output to the relay. Blocks while the source is full.
 * IN src - source the bytes came from, NULL is a no-op
 * IN data - bytes as the task wrote them, neither sanitized nor NULL terminated
 * IN len - number of bytes in data
 */
extern void relay_io_g_src_append(relay_io_src_t *src, const char *data,
				  size_t len);

/*
 * Mark a source as finished, so that a trailing line with no newline is
 * emitted.
 * IN src - source that reached eof, NULL is a no-op
 */
extern void relay_io_g_src_eof(relay_io_src_t *src);

/*
 * Free a source. Without relay_io_g_src_eof() first, its trailing partial line
 * is lost.
 * IN/OUT src_ptr - source to free, set to NULL
 */
extern void relay_io_g_src_destroy(relay_io_src_t **src_ptr);

/*
 * Turn what every source still holds into records, and stop the relay thread.
 * Call it before the step end record is emitted.
 */
extern void relay_io_g_srcs_drain(void);

/*
 * Write out what the plugin still has buffered, giving up after a bounded
 * time.
 * RET SLURM_SUCCESS or error
 */
extern int relay_io_g_flush(void);

#endif
