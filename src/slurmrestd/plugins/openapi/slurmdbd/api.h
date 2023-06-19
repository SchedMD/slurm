/*****************************************************************************\
 *  api.h - Slurm REST API openapi operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#ifndef SLURMRESTD_OPENAPI_SLURMDBD
#define SLURMRESTD_OPENAPI_SLURMDBD

#include "src/common/data.h"
#include "src/common/http.h"

#include "src/interfaces/data_parser.h"
#include "src/slurmrestd/openapi.h"

typedef openapi_ctxt_t ctxt_t;

#define resp_error(ctxt, error_code, source, why, ...) \
	openapi_resp_error(ctxt, error_code, source, why, ##__VA_ARGS__)
#define resp_warn(ctxt, source, why, ...) \
	openapi_resp_warn(ctxt, source, why, ##__VA_ARGS__)

/* ------------ generic typedefs for slurmdbd queries --------------- */

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

/* ------------ handlers for slurmdbd queries --------------- */

/*
 * Macro helper for Query database API for List output.
 * Converts the function name to string.
 */
#define db_query_list(ctxt, list, func, cond)                                 \
	db_query_list_funcname(ctxt, list, (db_list_query_func_t) func, cond, \
			       XSTRINGIFY(func), __func__, false)
/* query db but avoid error/warn on empty results */
#define db_query_list_xempty(ctxt, list, func, cond)                          \
	db_query_list_funcname(ctxt, list, (db_list_query_func_t) func, cond, \
			       XSTRINGIFY(func), __func__, true)

/*
 * Query database API for List output
 * IN ctxt - connection context
 * IN/OUT list - ptr to List ptr to populate with result (on success)
 * IN func - function ptr to call
 * IN cond - conditional to pass to func
 * IN func_name - string of func name (for errors)
 * IN ignore_empty_result - do not error/warn on empty results
 * RET SLURM_SUCCESS or error
 */
extern int db_query_list_funcname(ctxt_t *ctxt, List *list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name, const char *caller,
				  bool ignore_empty_result);

/*
 * Macro helper for Query database API for rc output.
 * Converts the function name to string.
 */
#define db_query_rc(ctxt, list, func)                               \
	db_query_rc_funcname(ctxt, list, (db_rc_query_func_t) func, \
			     XSTRINGIFY(func), __func__)

/*
 * Query database API for List output
 * IN ctxt - connection context
 * IN list - ptr to List to pass to func
 * IN func - function ptr to call
 * IN func_name - string of func name (for errors)
 * RET SLURM_SUCCESS or error
 */
extern int db_query_rc_funcname(ctxt_t *ctxt, List list,
				db_rc_query_func_t func, const char *func_name,
				const char *caller);

/*
 * Macro helper for modify database API for List output.
 * Converts the function name to string.
 */
#define db_modify_rc(ctxt, cond, obj, func)                                \
	db_modify_rc_funcname(ctxt, cond, obj, (db_rc_modify_func_t) func, \
			      XSTRINGIFY(func), __func__)

/*
 * Modify object in database API
 * IN ctxt - connection context
 * IN cond - ptr to filter conditional to pass to func
 * IN obj - ptr to obj to pass to func
 * IN func - function ptr to call
 * IN func_name - string of func name (for errors)
 * RET SLURM_SUCCESS or error
 */
extern int db_modify_rc_funcname(ctxt_t *ctxt, void *cond, void *obj,
				 db_rc_modify_func_t func,
				 const char *func_name, const char *caller);

#define db_query_commit(ctxt) db_query_commit_funcname(ctxt, __func__)

/*
 * Request database API to commit connection
 * IN ctxt - connection context
 */
extern void db_query_commit_funcname(ctxt_t *ctxt, const char *caller);

/* ------------ declarations for each operation --------------- */

extern void init_op_associations(void);
extern void destroy_op_associations(void);

extern void init_op_accounts(void);
extern void destroy_op_accounts(void);

extern void init_op_cluster(void);
extern void destroy_op_cluster(void);

extern void init_op_config(void);
extern void destroy_op_config(void);

extern void init_op_diag(void);
extern void destroy_op_diag(void);

extern void init_op_instances(void);
extern void destroy_op_instances(void);

extern void init_op_job(void);
extern void destroy_op_job(void);

extern void init_op_tres(void);
extern void destroy_op_tres(void);

extern void init_op_users(void);
extern void destroy_op_users(void);

extern void init_op_wckeys(void);
extern void destroy_op_wckeys(void);

extern void init_op_qos(void);
extern void destroy_op_qos(void);

/* register handler against each parser */
extern void bind_handler(const char *str_path, openapi_ctxt_handler_t callback,
			 int tag);

/*
 * Declarations for update handlers:
 * Most of the direct API calls need handlers to split up each updated entity
 * to check if an update or an add call is required. In some cases, the updates
 * must be handled via a diff of the current entity.
 *
 */

extern int update_accounts(ctxt_t *ctxt, bool commit, list_t *acct_list);
extern int update_associations(ctxt_t *ctxt, bool commit, list_t *assoc_list);
extern int update_clusters(ctxt_t *ctxt, bool commit, list_t *cluster_list);
extern int update_qos(ctxt_t *ctxt, bool commit, list_t *qos_list);
extern int update_tres(ctxt_t *ctxt, bool commit, list_t *tres_list);
extern int update_users(ctxt_t *ctxt, bool commit, list_t *user_list);
extern int update_wckeys(ctxt_t *ctxt, bool commit, list_t *wckey_list);

#endif
