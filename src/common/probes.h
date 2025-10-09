/*****************************************************************************\
 *  probes.h - Daemon liveness and readiness probes
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

#ifndef SLURM_PROBES_H
#define SLURM_PROBES_H

#include "src/common/pack.h"

typedef enum {
	PROBE_RC_INVALID = 0,
	PROBE_RC_UNKNOWN, /* service state is unknown */
	PROBE_RC_DOWN, /* service is down or failed */
	PROBE_RC_ONLINE, /* service is online */
	PROBE_RC_BUSY, /* service is too busy for requests */
	PROBE_RC_READY, /* service is ready for requests */
	PROBE_RC_INVALID_MAX,
} probe_status_t;

/* Opaque struct to handle probe verbose logging */
typedef struct probe_log_s probe_log_t;

/*
 * Callback to query service status
 * IN log -
 *	!Null: pass to probe_log() for logging verbose status
 *	Null: logging not requested
 * RET status of service
 */
typedef probe_status_t (*probe_query_t)(probe_log_t *log);

/*
 * Register probe query function
 * IN name - name of service to log
 * IN query - callback function to query to poll status
 */
extern void probe_register(const char *name, probe_query_t query);

/*
 * Log verbose status for service
 * IN log - log pointer from probe_query_t log arg
 * NOTE: Call probe_log() macro instead
 */
extern void probe_logger(probe_log_t *log, const char *caller, const char *fmt,
			 ...) __attribute__((format(printf, 3, 4)));

#define probe_log(log, fmt, ...) \
	do { \
		if (log) \
			probe_logger(log, __func__, fmt, ##__VA_ARGS__); \
	} while (0)

extern void probe_init(void);
extern void probe_fini(void);

/*
 * Run probes
 * IN name - name of probe to run or NULL for all probes
 * IN verbose - True to enable verbose logging
 * IN/OUT output - (ignored if verbose=false)
 *	!NULL: buffer to populate with verbose logs
 *	NULL: log to info()
 * caller - __func__ from caller
 * RET (lowest) status of all probes run
 */
extern probe_status_t probe_run(bool verbose, const char *name, buf_t *output,
				const char *caller);

#endif
