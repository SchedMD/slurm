/*****************************************************************************\
 *  http.h - handling HTTP
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

#ifndef SLURMRESTD_HTTP_H
#define SLURMRESTD_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "src/common/http.h"
#include "src/common/list.h"

#include "src/conmgr/conmgr.h"

struct on_http_request_args_s;
typedef struct on_http_request_args_s on_http_request_args_t;

/*
 * Call back for each HTTP requested method.
 * This may be called several times in the same connection.
 * must call send_http_response().
 *
 * IN args see on_http_request_args_t
 * RET SLURM_SUCCESS or error to kill connection
 */
typedef int (*on_http_request_t)(on_http_request_args_t *args);

/* Opaque connection context */
typedef struct http_context_s http_context_t;

typedef struct on_http_request_args_s {
	const http_request_method_t method; /* HTTP request method */
	list_t *headers; /* list_t of http_header_t* from client */
	const char *path; /* requested URL path (may be NULL) */
	const char *query; /* requested URL query (may be NULL) */
	http_context_t *context; /* calling context (do not xfree) */
	conmgr_fd_ref_t *con; /* reference to connection */
	const char *name; /* connection name */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	const char *content_type; /* header content-type */
	const char *accept; /* header accepted content-types */
	const char *body; /* body sent by client or NULL (do not xfree) */
	const size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} on_http_request_args_t;

/*
 * Call back for new connection to setup HTTP
 *
 * IN fd file descriptor of new connection
 * RET ptr to context to hand to parse_http()
 */
typedef http_context_t *(*on_http_connection_t)(int fd);

/*
 * Parse HTTP and call on_http_request on each HTTP request
 * must call send_http_response() on success
 * IN con conmgr connection of client
 * IN context connection context to hand to callback (do not xfree)
 * RET SLURM_SUCCESS or error
 */
extern int parse_http(conmgr_fd_t *con, void *context);

typedef struct {
	conmgr_fd_t *con; /* assigned connection */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	http_status_code_t status_code; /* HTTP status code to send */
	/* list of http_header_entry_t to send (can be empty) */
	list_t *headers; /* list_t of http_header_t* from client */
	const char *body; /* body to send or NULL */
	size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} send_http_response_args_t;

/*
 * Send HTTP response
 * IN args arguments of response
 * RET SLURM_SUCCESS or error
 */
extern int send_http_response(const send_http_response_args_t *args);

/*
 * setup http context against a given new socket
 * IN fd file descriptor of socket (must be connected!)
 * IN on_http_request callback to call on each HTTP request
 * RET NULL on error or new http context (must xfree)
 */
extern http_context_t *setup_http_context(conmgr_fd_t *con,
					  on_http_request_t on_http_request);

/*
 * cleanup http context on finished connection
 * IN con - conmgr connection
 * IN context - context to connection to free
 */
extern void on_http_connection_finish(conmgr_fd_t *con, void *ctxt);

/*
 * Get (arbitrary) auth pointer from context
 * IN context - connection context
 * RET auth pointer or NULL
 */
extern void *http_context_get_auth(http_context_t *context);

/*
 * Set and Get (arbitrary) auth pointer from context
 * IN context - connection context
 * IN auth - (arbitrary) auth pointer to set into context
 * RET Prior auth pointer or auth arg if context==NULL
 */
extern void *http_context_set_auth(http_context_t *context, void *auth);

/*
 * Release and NULL auth pointer from context
 * IN context - connection context
 */
extern void http_context_free_null_auth(http_context_t *context);

#endif /* SLURMRESTD_HTTP_H */
