/*****************************************************************************\
 *  openapi.h - OpenAPI plugin handler
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC.
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

/*
 * Based on OpenAPI 3.0.2 Standard:
 * 	https://github.com/OAI/OpenAPI-Specification/blob/master/versions/3.0.2.md
 */

#ifndef SLURMRESTD_OPENAPI_H
#define SLURMRESTD_OPENAPI_H

#include "src/common/data.h"
#include "src/common/http.h"
#include "src/common/list.h"
#include "src/common/openapi.h"
#include "src/common/plugrack.h"

#include "src/interfaces/data_parser.h"

/*
 * Callback from openapi caller.
 * we are not passing any http information to make this generic.
 * RET SLURM_SUCCESS or error to kill the connection
 */
typedef int (*openapi_handler_t)(
	const char *context_id, /* context id of client */
	http_request_method_t method, /* request method */
	data_t *parameters, /* openapi parameters */
	data_t *query, /* query sent by client */
	int tag, /* tag associated with path */
	data_t *resp, /* data to populate with response */
	void *auth, /* authentication context */
	data_parser_t *parser /* assigned data_parser or NULL */
);

typedef enum {
	OAS_FLAG_NONE = 0,
	OAS_FLAG_MANGLE_OPID = SLURM_BIT(0), /* mangle operationid */
	OAS_FLAG_SET_OPID = SLURM_BIT(1), /* set every operationid */
	/* Apply data_parser_g_specify() by all parsers and cleanup templates */
	OAS_FLAG_SET_DATA_PARSER_SPEC = SLURM_BIT(2),
	OAS_FLAG_MAX = SLURM_BIT(63) /* place holder */
} openapi_spec_flags_t;

/*
 * Register a given unique tag against a path.
 *
 * IN path - path to assign to given tag
 * RET -1 on error or >0 tag value for path.
 *
 * Can safely be called multiple times for same path.
 */
extern int register_path_tag(const char *path);

/*
 * Unregister a given unique tag against a path.
 *
 * IN tag - path tag to remove
 */
extern void unregister_path_tag(int tag);

/*
 * Find tag assigned to given path
 * IN path - split up path to match
 * IN/OUT params - on match, will populate any OAS parameters in path.
 * 	params must be DATA_TYPE_DICT.
 *
 * IN method - HTTP method to match
 * RET -1 if path tag was not found, or
 *     -2 if path tag was found, but method wasn't found within path tag, or
 *     the tag assigned to the given path.
 */
extern int find_path_tag(const data_t *path, data_t *params,
			 http_request_method_t method);

/*
 * Print registered methods for the requested tag at log level DEBUG4.
 */
extern void print_path_tag_methods(int tag);

/*
 * Init the OpenAPI data structs.
 * IN plugins_list - comma delimited list of plugins or "list"
 * 	pass NULL to load all found or "" to load none of them
 * IN listf - function to call if plugins="list" (may be NULL)
 * IN parsers_ptr - array of loaded data_parsers
 * RET SLURM_SUCCESS or error
 */
extern int init_openapi(const char *plugin_list, plugrack_foreach_t listf,
			data_parser_t **parsers_ptr);

/*
 * Free openapi
 */
extern void destroy_openapi(void);

/*
 * Joins all of the loaded specs into a single spec
 */
extern int get_openapi_specification(data_t *resp);

/*
 * Extracts the db_conn using given auth context
 * Note: This must be implemented in process calling openapi functions.
 */
extern void *openapi_get_db_conn(void *ctxt);

typedef struct {
	int rc;
	list_t *errors;
	list_t *warnings;
	data_parser_t *parser;
	const char *id; /* string identifying client (usually IP) */
	void *db_conn;
	http_request_method_t method;
	data_t *parameters;
	data_t *query;
	data_t *resp;
	data_t *parent_path;
	int tag;
} openapi_ctxt_t;

/*
 * Callback from openapi caller.
 * RET SLURM_SUCCESS or error to kill the connection
 */
typedef int (*openapi_ctxt_handler_t)(openapi_ctxt_t *ctxt);

/* Wraps ctxt callback to apply standardised response schema */
extern int wrap_openapi_ctxt_callback(const char *context_id,
				      http_request_method_t method,
				      data_t *parameters, data_t *query,
				      int tag, data_t *resp, void *auth,
				      data_parser_t *parser,
				      openapi_ctxt_handler_t callback,
				      const openapi_resp_meta_t *meta);

/*
 * Macro to make a single response dumping easy
 */
#define DUMP_OPENAPI_RESP_SINGLE(mtype, src, context_ptr)                    \
do {                                                                         \
	openapi_resp_single_t openapi_response = {                           \
		.response = src,                                             \
		.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME = context_ptr->errors,\
		.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME =                   \
			context_ptr->warnings,                               \
	};                                                                   \
	DATA_DUMP(context_ptr->parser, mtype, openapi_response, ctxt->resp); \
	list_flush(context_ptr->errors);                                     \
	list_flush(context_ptr->warnings);                                   \
} while (false)

/*
 * Add a response error
 * IN ctxt - connection context
 * IN why - description of error or NULL
 * IN error_code - Error number
 * IN source - Where the error was generated
 * RET value of error_code
 */
extern int openapi_resp_error(openapi_ctxt_t *ctxt, int error_code,
			      const char *source, const char *why, ...)
	__attribute__((format(printf, 4, 5)));
extern void openapi_resp_warn(openapi_ctxt_t *ctxt, const char *source,
			      const char *why, ...)
	__attribute__((format(printf, 3, 4)));

/*
 * Retrieve OpenAPI parameter
 * IN ctxt - connection context
 * IN required - error if parameter not found or valid
 * IN path - Path to parameter in query
 * IN caller - should be __func__
 * RET data_t ptr or NULL (on error or if not found)
 */
extern data_t *openapi_get_param(openapi_ctxt_t *ctxt, bool required,
				 const char *name, const char *caller);

/*
 * Retrieve OpenAPI string parameter
 * IN ctxt - connection context
 * IN required - error if parameter not found or valid
 * IN path - Path to parameter in query
 * IN caller - should be __func__
 * RET string or NULL (on error or if not found)
 */
extern char *openapi_get_str_param(openapi_ctxt_t *ctxt, bool required,
				   const char *name, const char *caller);
/*
 * Retrieve OpenAPI UNIX timestamp parameter
 * IN ctxt - connection context
 * IN required - error if parameter not found
 * IN name - Path to parameter in query
 * IN time_ptr - ptr to time_t to populate on successful parsing
 * 	A time=0 may be an error and return SLURM_SUCCESS due to
 * 	parse_time() being ambiguous.
 * IN caller - should be __func__
 * RET SLURM_SUCCESS or (ESLURM_REST_EMPTY_RESULT if not found) or error
 */
extern int openapi_get_date_param(openapi_ctxt_t *ctxt, bool required,
				  const char *name, time_t *time_ptr,
				  const char *caller);

#endif /* SLURMRESTD_OPENAPI_H */
