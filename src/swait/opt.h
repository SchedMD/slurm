/*****************************************************************************\
 *  opt.h - swait command line option parsing.
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

#ifndef _SWAIT_OPT_H
#define _SWAIT_OPT_H

#include <inttypes.h>
#include <stdbool.h>

#include "slurm/slurm.h"

/*
 * Exit codes; keep in sync with EXIT STATUS in doc/man/man1/swait.1.
 * OK - the wait completed and the target was reported
 * ERROR - swait encountered an error
 * TIMEOUT - --timeout elapsed before the wait completed
 * UNOBSERVED - the set drained without the target being reported
 */
#define SWAIT_RC_OK 0
#define SWAIT_RC_ERROR 1
#define SWAIT_RC_TIMEOUT 2
#define SWAIT_RC_UNOBSERVED 3

typedef struct {
	uint32_t array_job_id; /* array master id from input, or NO_VAL */
	uint32_t array_task_id; /* task offset from input, or NO_VAL */
	bool follow; /* --follow: stream every step end until drain (ALL) */
	uint16_t mode; /* derived steps_sub_mode_t */
	bool quiet; /* --quiet */
	slurm_step_id_t target; /* SLUID and/or job_id from argv/env;
				 * for array tasks, job_id holds the per-task
				 * assigned id after ctld discovery; step_id is
				 * the STEP target or NO_VAL */
	uint32_t timeout; /* --timeout, seconds; 0 disables */
	int verbose; /* count of -v; bumps stderr log level */
} swait_opt_t;

extern swait_opt_t opt;

/*
 * Parse command line, fill in the global opt struct, and exit on errors.
 * IN argc - argument count
 * IN argv - argument vector
 */
extern void parse_command_line(int argc, char **argv);

#endif /* _SWAIT_OPT_H */
