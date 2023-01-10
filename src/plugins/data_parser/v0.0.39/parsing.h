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
#include "api.h"
#include "parsers.h"

/*
 * Separator used to split up the source path.
 * OpenAPI specification 3.1.0 explictly requires $ref paths must be compliant
 * with RFC3986 URIs. It is expected that inside of "$ref" path that the
 * relative path use "/" as seperators and that the relative paths start with
 * "#".
 */
#define PATH_SEP "/"
#define PATH_REL "#"

/*
 * Set path string from parent_path
 * IN/OUT path_ptr - ptr to path string to set/replace
 * IN parent_path - data list with each path entry
 * RET ptr to path (to make logging easier)
 */
extern char *set_source_path(char **path_ptr, data_t *parent_path);

/*
 * Clone parent_path and append list index to last entry
 * IN parent_path - data list with each path entry
 * IN index - index of entry in list
 * RET new parent path (caller must release with FREE_NULL_DATA())
 */
extern data_t *clone_source_path_index(data_t *parent_path, int index);

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
