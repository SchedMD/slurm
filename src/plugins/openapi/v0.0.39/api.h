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

#ifndef SLURMRESTD_OPENAPI_V0039
#define SLURMRESTD_OPENAPI_V0039

#include "src/common/data.h"
#include "src/interfaces/data_parser.h"

#define DATA_VERSION "v0.0.39"
#define DATA_PLUGIN "data_parser/" DATA_VERSION
#define MAGIC_CTXT 0xafbb0fae

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
	data_t *parent_path;
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

extern int get_date_param(data_t *query, const char *param, time_t *time);

/* ------------ declarations for each operation --------------- */

extern void init_op_diag(void);
extern void init_op_jobs(void);
extern void init_op_nodes(void);
extern void init_op_partitions(void);
extern void init_op_reservations(void);
extern void destroy_op_diag(void);
extern void destroy_op_jobs(void);
extern void destroy_op_nodes(void);
extern void destroy_op_partitions(void);
extern void destroy_op_reservations(void);

#endif
