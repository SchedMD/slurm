/*****************************************************************************\
 *  http_con.h - handling HTTP connections
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

#ifndef SLURM_HTTP_CON_H
#define SLURM_HTTP_CON_H

#include "src/common/http.h"
#include "src/conmgr/conmgr.h"

/* Opaque struct for HTTP connection instance */
typedef struct http_con_s http_con_t;

typedef struct {
	/* Requested URL */
	url_t url;
	/* Request HTTP method */
	http_request_method_t method;
	/* list of each header received */
	list_t *headers;
	/* RFC2068-19.7.1 "Connection: keep-alive" header */
	bool keep_alive;
	/* RFC7230-6.1 "Connection: Close" */
	bool connection_close;
	/* RFC7231-5.1.1 expect requested */
	int expect;
	/* Accept Header */
	char *accept;
	/* Content-Type header */
	char *content_type;
	/* Content-Length header or -1 */
	ssize_t content_length;
	/* Content buffer (may have already been truncated) or NULL */
	buf_t *content;
	/* Number of Content bytes received for this request */
	ssize_t content_bytes;

	struct {
		uint16_t major;
		uint16_t minor;
	} http_version;
} http_con_request_t;

typedef struct {
	/*
	 * Called on HTTP request and headers and content received
	 * WARNING: Content-Length header may not be enforced before this is
	 *	called with complete=true or this may be skipped entirely for
	 *	unexpected EOF/error
	 * IN hcon - pointer to http connection
	 * IN name - connection name for logging
	 * IN request - pointer to parsed request
	 * IN arg - arbitrary pointer handed to http_con_assign_server()
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_request)(http_con_t *hcon, const char *name,
			  const http_con_request_t *request, void *arg);
	/*
	 * Call back when connection is closed.
	 * Note: hcon will already be free()ed before this callback.
	 * IN name - connection name for logging
	 * IN arg - arbitrary pointer handed to http_con_assign_server()
	 */
	void (*on_close)(const char *name, void *arg);
} http_con_server_events_t;

/* Get number of bytes needed to hold http_con_t instance */
extern const size_t http_con_bytes(void);

/*
 * Assign HTTP handlers to server connection
 * IN con - conmgr connection to parse incoming HTTP requests
 * IN hcon - pointer to bytes allocated for connection's state or NULL.
 * NOTE: hcon pointer must be valid until on_close() is called.
 * IN events - HTTP server callbacks for events
 * IN arg - arbitrary pointer to include in callbacks
 * RET SLURM_SUCCESS or error
 */
extern int http_con_assign_server(conmgr_fd_ref_t *con, http_con_t *hcon,
				  const http_con_server_events_t *events,
				  void *arg);
/*
 * Send HTTP response
 * IN status_code - HTTP status code to send
 * IN headers - list_t of http_header_t* to send (can be NULL or empty)
 * IN close_header - Include "Connection: Close" header
 * IN body - body contents to send or NULL
 * IN body_encoding - mime type for body (ignored if body is NULL) or NULL
 * RET SLURM_SUCCESS or error
 */
extern int http_con_send_response(http_con_t *hcon,
				  http_status_code_t status_code,
				  list_t *headers, bool close_header,
				  buf_t *body, const char *body_encoding);

#endif
