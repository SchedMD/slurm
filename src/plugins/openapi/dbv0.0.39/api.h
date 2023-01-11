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

#ifndef SLURMRESTD_OPENAPI_DB_V0039
#define SLURMRESTD_OPENAPI_DB_V0039

#include "src/common/data.h"
#include "src/common/http.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/openapi.h"

#define DATA_VERSION "v0.0.39"
#define DATA_PLUGIN "data_parser/" DATA_VERSION
#define CONFIG_OP_TAG 0xfffffffe
#define MAGIC_CTXT 0xaffb0ffe

typedef struct {
	int magic; /* MAGIC_CTXT */
	int rc;
	data_t *errors;
	data_t *warnings;
	data_parser_t *parser;
	const char *id; /* string identifying client (usually IP) */
	void *db_conn;
	http_request_method_t method;
	data_t *parameters;
	data_t *query;
	data_t *resp;
} ctxt_t;

/*
 * Initiate connection context.
 *
 * This function is expected to be called in the callback handlers from
 * operations router. It will setup everything required for handling the client
 * request including tracking errors and warnings.
 *
 * IN context_id - string ident for client
 * IN method - HTTP method of request
 * IN parameters - data list of client supplied HTTP parameters
 * IN query - data list of client supplied HTTP querys
 * IN tag - callback assigned tag
 * IN auth - auth ptr reference
 */
extern ctxt_t *init_connection(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp,
			       void *auth);

/* provides RC for connection and releases connection context */
extern int fini_connection(ctxt_t *ctxt);

/*
 * Add a response error
 * IN ctxt - connection context
 * IN why - description of error or NULL
 * IN error_code - Error number
 * IN source - Where the error was generated
 * RET value of error_code
 */
extern int resp_error(ctxt_t *ctxt, int error_code, const char *source,
		      const char *why, ...)
	__attribute__((format(printf, 4, 5)));
extern void resp_warn(ctxt_t *ctxt, const char *source, const char *why, ...)
	__attribute__((format(printf, 3, 4)));

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

/* ------------ handlers for user requests --------------- */

#define get_str_param(path, ctxt) get_str_param_funcname(path, ctxt, __func__)

/*
 * Retrieve parameter
 * IN path - Path to parameter in query
 * IN ctxt - connection context
 * RET string or NULL on error
 */
extern char *get_str_param_funcname(const char *path, ctxt_t *ctxt,
				    const char *caller);

#define get_query_key_list(path, ctxt, parent_path) \
	get_query_key_list_funcname(path, ctxt, parent_path, __func__)

/*
 * Retrieve List from query list
 * IN path - Path to parameter in query (case insensitive)
 * IN ctxt - connection context
 * IN query - query from http request
 * IN/OUT parent_path - data to init to hold parse path
 * RET List ptr or NULL on error
 */
extern data_t *get_query_key_list_funcname(const char *path, ctxt_t *ctxt,
					   data_t **parent_path,
					   const char *caller);

/* ------------ declarations for each operation --------------- */

extern void init_op_associations(void);
extern void destroy_op_associations(void);
extern int op_handler_associations(const char *context_id,
				   http_request_method_t method,
				   data_t *parameters, data_t *query, int tag,
				   data_t *resp, void *auth);

extern void init_op_accounts(void);
extern void destroy_op_accounts(void);
extern int op_handler_accounts(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp,
			       void *auth);

extern void init_op_cluster(void);
extern void destroy_op_cluster(void);
extern int op_handler_clusters(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp,
			       void *auth);

extern void init_op_config(void);
extern void destroy_op_config(void);

extern void init_op_diag(void);
extern void destroy_op_diag(void);

extern void init_op_job(void);
extern void destroy_op_job(void);
extern int op_handler_jobs(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth);

extern void init_op_tres(void);
extern void destroy_op_tres(void);
extern int op_handler_tres(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth);

extern void init_op_users(void);
extern void destroy_op_users(void);
extern int op_handler_users(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp, void *auth);

extern void init_op_wckeys(void);
extern void destroy_op_wckeys(void);
extern int op_handler_wckeys(const char *context_id,
			     http_request_method_t method,
			     data_t *parameters, data_t *query, int tag,
			     data_t *resp, void *auth);

extern void init_op_qos(void);
extern void destroy_op_qos(void);
extern int op_handler_qos(const char *context_id, http_request_method_t method,
			  data_t *parameters, data_t *query, int tag,
			  data_t *resp, void *auth);

extern int username_to_uid(void *x, void *arg);
extern int groupname_to_gid(void *x, void *arg);

#endif
