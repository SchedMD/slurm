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

#ifndef SLURMRESTD_OPENAPI_DB_V0037
#define SLURMRESTD_OPENAPI_DB_V0037

#include "config.h"

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/data.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/dbv0.0.37/parse.h"

#define CONFIG_OP_TAG 0xfffffffe

/*
 * Fill out boilerplate for every data response
 * RET ptr to errors dict
 */
extern data_t *populate_response_format(data_t *resp);

/*
 * Add a response error to errors
 * IN errors - data list to append a new error
 * IN why - description of error or NULL
 * IN error_code - Error number
 * IN source - Where the error was generated
 * RET value of error_code
 */
extern int resp_error(data_t *errors, int error_code, const char *why,
		      const char *source);

/* ------------ generic typedefs for slurmdbd queries --------------- */

/* Generic typedef for the DB query functions that return a list */
typedef List (*db_list_query_func_t)(void *db_conn, void *cond);
/*
 * Generic typedef for the DB query functions that takes a list and returns an
 * rc if the query was successful.
 */
typedef int (*db_rc_query_func_t)(void *db_conn, List list);

/* ------------ handlers for slurmdbd queries --------------- */

/*
 * Macro helper for Query database API for List output.
 * Converts the function name to string.
 */
#define db_query_list(errors, auth, list, func, cond)                        \
	db_query_list_funcname(errors, auth, list, \
			       (db_list_query_func_t)func, cond, #func)

/*
 * Query database API for List output
 * IN errors - data list to append a new error
 * IN auth - connection authentication attr
 * IN/OUT list - ptr to List ptr to populate with result (on success)
 * IN func - function ptr to call
 * IN cond - conditional to pass to func
 * IN func_name - string of func name (for errors)
 * RET SLURM_SUCCESS or error
 */
extern int db_query_list_funcname(data_t *errors, rest_auth_context_t *auth,
				  List *list, db_list_query_func_t func,
				  void *cond, const char *func_name);

/*
 * Macro helper for Query database API for rc output.
 * Converts the function name to string.
 */
#define db_query_rc(errors, auth, list, func) \
	db_query_rc_funcname(errors, auth, list, (db_rc_query_func_t)func, \
                             #func)

/*
 * Query database API for List output
 * IN errors - data list to append a new error
 * IN auth - connection authentication attr
 * IN list - ptr to List to pass to func
 * IN func - function ptr to call
 * IN func_name - string of func name (for errors)
 * RET SLURM_SUCCESS or error
 */
extern int db_query_rc_funcname(data_t *errors,
				rest_auth_context_t *auth, List list,
				db_rc_query_func_t func,
				const char *func_name);

/*
 * Request database API to commit connection
 * IN errors - data list to append a new error
 * IN auth - connection authentication attr
 * RET SLURM_SUCCESS or error
 */
extern int db_query_commit(data_t *errors, rest_auth_context_t *auth);

/* ------------ handlers for user requests --------------- */

/*
 * Retrieve parameter
 * IN path - Path to parameter in query
 * IN errors - data list to append a new error
 * IN parameters - paramets from http request
 * RET string or NULL on error
 */
extern char *get_str_param(const char *path, data_t *errors,
			   data_t *parameters);

/*
 * Retrieve List from query list
 * IN path - Path to parameter in query
 * IN errors - data list to append a new error
 * IN query - query from http request
 * RET List ptr or NULL on error
 */
extern data_t *get_query_key_list(const char *path, data_t *errors,
				   data_t *query);

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

#endif
