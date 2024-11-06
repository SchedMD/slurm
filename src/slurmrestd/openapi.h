/*****************************************************************************\
 *  openapi.h - OpenAPI plugin handler
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

typedef enum {
	OP_BIND_INVALID = 0,
	OP_BIND_NONE = SLURM_BIT(1),
	OP_BIND_DATA_PARSER = SLURM_BIT(2), /* populate {data_parser} in URL */
	OP_BIND_OPENAPI_RESP_FMT = SLURM_BIT(3), /* populate errors,warnings,meta */
	OP_BIND_HIDDEN_OAS = SLURM_BIT(4), /* Hide from OpenAPI specification */
	OP_BIND_NO_SLURMDBD = SLURM_BIT(5), /* Do not prepare slurmdbd connection */
	OP_BIND_REQUIRE_SLURMDBD = SLURM_BIT(6), /* Require slurmdbd connection or don't call path */
	OP_BIND_INVALID_MAX = INFINITE16
} op_bind_flags_t;

typedef struct {
	http_request_method_t method;
	const char *const *tags;
	const char *summary;
	const char *description;
	struct {
		data_parser_type_t type;
		const char *description;
	} response;
	data_parser_type_t parameters;
	data_parser_type_t query;
	struct {
		data_parser_type_t type;
		const char *description;
		bool optional;
	} body;
} openapi_path_binding_method_t;

typedef struct {
	const char *path;
	openapi_ctxt_handler_t callback;
	const openapi_path_binding_method_t *methods;
	op_bind_flags_t flags;
} openapi_path_binding_t;

/*
 * Register a given unique tag against a path binding.
 *
 * IN in_path - string path to assign to given tag or
 * 	NULL (to use path in op_path)
 * IN op_path - Operation binding for path
 * IN meta - Meta information from plugin (or NULL)
 * IN parser - Relavent data_parser (or NULL)
 * IN/OUT tag_ptr - Sets tag on success
 * RET SLURM_SUCCESS or
 *	ESLURM_NOT_SUPPORTED: if data_parser doesnt support all types in method
 *	or any other Slurm error
 *
 * Can safely be called multiple times for same path.
 */
extern int register_path_binding(const char *in_path,
				 const openapi_path_binding_t *op_path,
				 const openapi_resp_meta_t *meta,
				 data_parser_t *parser, int *tag_ptr);

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
 * IN response_status_codes - HTTP_STATUS_NONE terminated array of HTTP status
 *	codes to generate or NULL for default
 * RET SLURM_SUCCESS or error
 */
extern int init_openapi(const char *plugin_list, plugrack_foreach_t listf,
			data_parser_t **parsers_ptr,
			const http_status_code_t *resp_status_codes);

/*
 * Free openapi
 */
extern void destroy_openapi(void);

/*
 * Extracts the db_conn using given auth context
 * Note: This must be implemented in process calling openapi functions.
 */
extern void *openapi_get_db_conn(void *ctxt);

/* Wraps ctxt callback to apply standardised response schema */
extern int wrap_openapi_ctxt_callback(const char *context_id,
				      http_request_method_t method,
				      data_t *parameters, data_t *query,
				      int tag, data_t *resp, void *auth,
				      data_parser_t *parser,
				      const openapi_path_binding_t *op_path,
				      const openapi_resp_meta_t *plugin_meta);

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
 * Generate OpenAPI specification
 * IN/OUT dst - data_t to populate with specification
 * RET SLURM_SUCCESS or error
 */
extern int generate_spec(data_t *dst);

/*
 * True if only generating an OAS
 * IN set - Set to true
 * RET true if only generating a spec file
 * Warning: Do not call with set=true after multithreading started
 */
extern bool is_spec_generation_only(bool set);

#endif /* SLURMRESTD_OPENAPI_H */
