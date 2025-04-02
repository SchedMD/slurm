/*****************************************************************************\
 *  api.h - Slurm data parsing handlers
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

#ifndef DATA_PARSER_API
#define DATA_PARSER_API

#include "src/interfaces/data_parser.h"

/*
 * These macros are defined by the Makefile.am:
 * DATA_VERSION
 * PLUGIN_ID
 */

#define MAGIC_ARGS 0x2ea1bebb
#define is_fast_mode(args) (args->flags & FLAG_FAST)
#define is_complex_mode(args) (args->flags & FLAG_COMPLEX_VALUES)
#define is_prefer_refs_mode(args) (~args->flags & FLAG_MINIMIZE_REFS)
#define is_inline_enums_mode(args) (args->flags & FLAG_INLINE_ENUMS)

typedef enum {
	FLAG_NONE = 0,
	/* only dump the OpenAPI Specification instead of the requested data */
	FLAG_SPEC_ONLY = SLURM_BIT(0),
	/* attempt to run as fast as possible, skipping more expensive checks */
	FLAG_FAST = SLURM_BIT(1),
	/* use null/false/Infinity/NaN for *_NO_VALs */
	FLAG_COMPLEX_VALUES = SLURM_BIT(2),
	/*
	 * Deprecated in v0.0.42 (as default behavior)
	 * TODO: Remove flag in v0.0.45
	 * Prefer $ref over expanding schema inline in OpenAPI specification
	 * Set FLAG_MINIMIZE_REFS to deactivate this default.
	 */
	FLAG_PREFER_REFS = SLURM_BIT(3),
	/* Prefer to inline $ref into schemas when possible in OpenAPI specification */
	FLAG_MINIMIZE_REFS = SLURM_BIT(4),
	/* Prefer to inline $ref into schema for flag arrays */
	FLAG_INLINE_ENUMS = SLURM_BIT(5),
} data_parser_flags_t;

typedef struct {
	int magic; /* MAGIC_ARGS */
	data_parser_on_error_t on_parse_error;
	data_parser_on_error_t on_dump_error;
	data_parser_on_error_t on_query_error;
	void *error_arg;
	data_parser_on_warn_t on_parse_warn;
	data_parser_on_warn_t on_dump_warn;
	data_parser_on_warn_t on_query_warn;
	void *warn_arg;
	void *db_conn;
	bool close_db_conn;
	list_t *tres_list;
	list_t *qos_list;
	list_t *assoc_list;
	data_parser_flags_t flags;
} args_t;

#endif
