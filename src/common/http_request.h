/*****************************************************************************\
 *  http_request.h - HTTP on request handler
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

#ifndef SLURM_HTTP_REQUEST_H
#define SLURM_HTTP_REQUEST_H

#include "src/common/http.h"
#include "src/common/http_con.h"

#include "src/interfaces/data_parser.h"

/* Opaque event state passed between _on_request() and http_request_reply() */
typedef struct http_request_event_s http_request_event_t;

/*
 * Called after successful HTTP authentication and MIME negotiation.
 * IN event - opaque request event state (pass to http_request_reply())
 * IN hcon - pointer to HTTP connection
 * IN name - connection name for logging
 * IN uid - authenticated user id
 * IN method - HTTP request method
 * IN request - pointer to parsed HTTP request
 * IN arg - arbitrary pointer handed to http_request_bind()
 * RET SLURM_SUCCESS or error
 */
typedef int (*http_request_on_request_t)(http_request_event_t *event,
					 http_con_t *hcon, const char *name,
					 const uid_t uid,
					 http_request_method_t method,
					 const http_con_request_t *request,
					 void *arg);
/*
 * Called when HTTP request processing encounters an error (e.g.
 * authentication failure or unsupported MIME type).
 * IN event - opaque request event state
 * IN hcon - pointer to HTTP connection
 * IN name - connection name for logging
 * IN request - pointer to parsed HTTP request
 * IN err - error code describing the failure
 * RET SLURM_SUCCESS or error
 */
typedef int (*http_request_on_error_t)(http_request_event_t *event,
				       http_con_t *hcon, const char *name,
				       const http_con_request_t *request,
				       int err);

/*
 * Bind an HTTP path to request and error callbacks. Handles authentication
 * and MIME negotiation before invoking on_request. When reply_type is not
 * DATA_PARSER_TYPE_INVALID, the path is registered once per parser version
 * that supports the given type (substituting OPENAPI_DATA_PARSER_PARAM in
 * path with the parser version string).
 * IN parsers - NULL-terminated array of data parsers
 * IN method - HTTP method to bind
 * IN path - URL path to bind (may contain OPENAPI_DATA_PARSER_PARAM)
 * IN on_request - callback for authenticated requests
 * IN on_error - callback for request processing errors
 * IN reply_type - data parser type for serializing replies, or
 *	DATA_PARSER_TYPE_INVALID for untyped replies
 * IN arg - arbitrary pointer passed through to on_request callback
 */
extern void http_request_bind(data_parser_t **parsers,
			      http_request_method_t method, const char *path,
			      http_request_on_request_t on_request,
			      http_request_on_error_t on_error,
			      data_parser_type_t reply_type, void *arg);

/*
 * Send an HTTP response for a bound request. When a parser is associated with
 * the binding and reply is non-NULL, the reply is serialized using the bound
 * data parser type and the negotiated write MIME type. When no parser is
 * associated, the raw reply bytes are sent as text. When reply is NULL, an
 * empty response with the appropriate status code is sent.
 * IN event - opaque request event state from on_request callback
 * IN rc - result code (mapped to HTTP status via http_status_from_error())
 * IN headers - optional list of additional HTTP headers (or NULL)
 * IN close_header - if true, include Connection: close header
 * IN reply - pointer to response data to serialize (or NULL)
 * IN reply_bytes - size of reply data in bytes
 * RET SLURM_SUCCESS or error
 */
extern int http_request_reply(http_request_event_t *event, int rc,
			      list_t *headers, bool close_header, void *reply,
			      size_t reply_bytes);

#endif
