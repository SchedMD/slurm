/*****************************************************************************\
 *  slurmdb_helpers.h - slurmdb helpers
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

#ifndef DATA_PARSER_SLURMDB_HELPERS
#define DATA_PARSER_SLURMDB_HELPERS

#include "api.h"
#include "events.h"
#include "parsers.h"

/* Generic typedef for the DB query functions that return a list */
typedef List (*db_list_query_func_t)(void *db_conn, void *cond);
/*
 * Generic typedef for the DB query functions that takes a list and returns an
 * rc if the query was successful.
 */
typedef int (*db_rc_query_func_t)(void *db_conn, List list);
/*
 * Generic typedef for the DB modify functions that takes an object record and
 * returns an List if the query was successful or NULL on error
 */
typedef List (*db_rc_modify_func_t)(void *db_conn, void **cond, void *obj);

/*
 * Macro helper for Query database API for List output.
 * Converts the function name to string.
 */
#define _db_query_list(op, type, args, list, func, cond)                 \
	db_query_list_funcname(op, type, args, list,                     \
			       (db_list_query_func_t) func, cond, #func, \
			       __func__)

/*
 * Query database API for List output
 * IN op - current parser op
 * IN type - parser type
 * IN args - parser ptr
 * IN/OUT list - ptr to List ptr to populate with result (on success)
 * IN func - function ptr to call
 * IN cond - conditional to pass to func
 * IN func_name - string of func name (for errors)
 * RET SLURM_SUCCESS or error
 */
extern int db_query_list_funcname(parse_op_t op, data_parser_type_t type,
				  args_t *args, List *list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name,
				  const char *func_caller_name);

extern int resolve_qos(parse_op_t op, const parser_t *const parser,
		       slurmdb_qos_rec_t **qos_ptr, data_t *src, args_t *args,
		       data_t *parent_path, const char *caller,
		       bool ignore_failure);

extern int load_prereqs_funcname(parse_op_t op, const parser_t *const parser,
				 args_t *args, const char *func_name);
#define load_prereqs(op, parser, args) \
	load_prereqs_funcname(op, parser, args, __func__)

/* Attempt to logically match x to y by the attributes of an assoc */
extern int compare_assoc(const slurmdb_assoc_rec_t *x,
			 const slurmdb_assoc_rec_t *y);

extern int fuzzy_match_tres(slurmdb_tres_rec_t *tres,
			    slurmdb_tres_rec_t *needle);

#endif
