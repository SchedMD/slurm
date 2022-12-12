/*****************************************************************************\
 *  api.h - Slurm data parsing handlers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#ifndef DATA_PARSER_EVENTS
#define DATA_PARSER_EVENTS

#include "parsers.h"

typedef enum {
	PARSE_INVALID = 0,
	PARSING = 0xeaea,
	DUMPING = 0xaeae,
	QUERYING = 0xdaab, /* only used for prereqs currently */
} parse_op_t;

/*
 * helper to call the correct error hook
 * IN op - operation type
 * IN type - parsing type
 * IN args - ptr to args
 * IN error_code - numeric code of this error
 * IN source - which slurmdb function triggered error or sent it
 * IN caller - function calling source()
 * IN why - long form explanation of error
 * RET SLURM_SUCCESS to ignore error or error code to fail on
 */
extern int on_error(parse_op_t op, data_parser_type_t type, args_t *args,
		    int error_code, const char *source, const char *caller,
		    const char *why, ...) __attribute__((format(printf, 7, 8)));

/*
 * helper to call the correct warning hook
 * IN op - operation type
 * IN type - parsing type
 * IN args - ptr to args
 * IN source - which slurmdb function triggered warning or sent it
 * IN caller - function calling source()
 * IN why - long form explanation of warning
 */
extern void on_warn(parse_op_t op, data_parser_type_t type, args_t *args,
		    const char *source, const char *caller, const char *why,
		    ...) __attribute__((format(printf, 6, 7)));
#endif
