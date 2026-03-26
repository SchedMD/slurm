/*****************************************************************************\
 *  http_router.h - Route HTTP requests
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

#ifndef SLURM_HTTP_ROUTER_H
#define SLURM_HTTP_ROUTER_H

#include "src/common/http.h"
#include "src/common/http_con.h"

/*
 * Called on bound HTTP request and headers and content received
 * @see http_con_server_events_t::on_request()
 * IN hcon - pointer to http connection
 * IN name - connection name for logging
 * IN request - pointer to parsed request
 * IN arg - arbitrary pointer handed to http_con_assign_server()
 * IN path_arg - arbitrary pointer handed to http_router_bind()
 * RET SLURM_SUCCESS to continue parsing to error to stop
 */
typedef int (*http_router_on_request_event_t)(http_con_t *hcon,
					      const char *name,
					      const http_con_request_t *request,
					      void *arg, void *path_arg);

/*
 * Initialize HTTP router
 * IN on_not_found - callback for when request doesn't match any bound paths
 */
extern void http_router_init(http_router_on_request_event_t on_not_found);

/* Cleanup HTTP router */
extern void http_router_fini(void);

/*
 * Bind to path in HTTP router
 * IN hrouter - http router to bind path events
 * IN method - HTTP method to bind at path
 * IN path - string HTTP URL path to bind
 * IN on_request - callbacks for on_request() event
 * IN path_arg - arbitrary pointer to pass to on_request()
 */
extern void http_router_bind(http_request_method_t method, const char *path,
			     http_router_on_request_event_t on_request,
			     void *path_arg);

/* Callback to have HTTP router match method and path to a bound callback */
extern int http_router_on_request(http_con_t *hcon, const char *name,
				  const http_con_request_t *request, void *arg);

#endif
