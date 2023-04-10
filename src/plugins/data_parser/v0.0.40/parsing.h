/*****************************************************************************\
 *  parsing.h - Slurm data parsing handlers
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

#ifndef DATA_PARSER_PARSING
#define DATA_PARSER_PARSING

#include "src/interfaces/data_parser.h"
#include "src/interfaces/openapi.h"
#include "api.h"
#include "parsers.h"

/*
 * All parsing uses a parent path (list of path components) to track parsing
 * path to provide client a useful error/warning message about issues. OpenAPI
 * specifies how path strings are to be constructed.
 */
#define set_source_path(path_ptr, parent_path) \
	openapi_fmt_rel_path_str(path_ptr, parent_path)
#define clone_source_path_index(parent_path, index) \
	openapi_fork_rel_path_list(parent_path, index)

/*
 * Remove macros to avoid calling them after this point since all calls should
 * be done against PARSE() or DUMP() instead.
 */
#undef DATA_DUMP
#undef DATA_PARSE

extern int dump(void *src, ssize_t src_bytes, const parser_t *const parser,
		data_t *dst, args_t *args);
#define DUMP(type, src, dst, args)                                            \
	dump(&src, sizeof(src), find_parser_by_type(DATA_PARSER_##type), dst, \
	     args)

extern int parse(void *dst, ssize_t dst_bytes, const parser_t *const parser,
		 data_t *src, args_t *args, data_t *parent_path);
#define PARSE(type, dst, src, parent_path, args)                               \
	parse(&dst, sizeof(dst), find_parser_by_type(DATA_PARSER_##type), src, \
	      args, parent_path)

#endif
