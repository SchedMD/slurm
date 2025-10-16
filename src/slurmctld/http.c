/*****************************************************************************\
 *  http.c - Implementation for handling HTTP requests
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

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_router.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/http.h"
#include "src/slurmctld/slurmctld.h"

static int _reply_error(http_con_t *hcon, const char *name,
			const http_con_request_t *request, int err)
{
	char *body = NULL, *at = NULL;
	int rc = EINVAL;

	xstrfmtcatat(body, &at, "slurmctld HTTP server request for '%s %s':\n",
		     get_http_method_string(request->method),
		     request->url.path);

	if (err)
		xstrfmtcatat(body, &at, "Failed: %s\n", slurm_strerror(err));

	rc = http_con_send_response(hcon, http_status_from_error(err), NULL,
				    true,
				    &SHADOW_BUF_INITIALIZER(body, strlen(body)),
				    MIME_TYPE_TEXT);

	xfree(body);
	return rc;
}

static int _req_not_found(http_con_t *hcon, const char *name,
			  const http_con_request_t *request, void *arg)
{
	return _reply_error(hcon, name, request, ESLURM_REST_UNKNOWN_URL);
}

static int _req_root(http_con_t *hcon, const char *name,
		     const http_con_request_t *request, void *arg)
{
	static const char body[] =
		"slurmctld index of endpoints:\n"
		"  '/readyz': check slurmctld is servicing RPCs\n"
		"  '/livez': check slurmctld is running\n"
		"  '/healthz': check slurmctld is running\n";

	return http_con_send_response(hcon,
				      http_status_from_error(SLURM_SUCCESS),
				      NULL, true,
				      &SHADOW_BUF_INITIALIZER(body,
							      strlen(body)),
				      MIME_TYPE_TEXT);
}

static int _req_readyz(http_con_t *hcon, const char *name,
		       const http_con_request_t *request, void *arg)
{
	http_status_code_t status = HTTP_STATUS_CODE_SRVERR_INTERNAL;

	if (!listeners_quiesced() && is_primary() && !is_reconfiguring())
		status = HTTP_STATUS_CODE_SUCCESS_NO_CONTENT;

	return http_con_send_response(hcon, status, NULL, true, NULL, NULL);
}

static int _req_livez(http_con_t *hcon, const char *name,
		      const http_con_request_t *request, void *arg)
{
	return http_con_send_response(hcon, HTTP_STATUS_CODE_SUCCESS_NO_CONTENT,
				      NULL, true, NULL, NULL);
}

static int _req_healthz(http_con_t *hcon, const char *name,
			const http_con_request_t *request, void *arg)
{
	return http_con_send_response(hcon, HTTP_STATUS_CODE_SUCCESS_NO_CONTENT,
				      NULL, true, NULL, NULL);
}

extern void http_init(void)
{
	http_router_init(_req_not_found);
	http_router_bind(HTTP_REQUEST_GET, "/", _req_root);
	http_router_bind(HTTP_REQUEST_GET, "/readyz", _req_readyz);
	http_router_bind(HTTP_REQUEST_GET, "/livez", _req_livez);
	http_router_bind(HTTP_REQUEST_GET, "/healthz", _req_healthz);
}

extern void http_fini(void)
{
	http_router_fini();
}

extern int on_http_connection(conmgr_fd_t *con)
{
	static const http_con_server_events_t events = {
		.on_request = http_router_on_request,
	};
	int rc = EINVAL;
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(con);

	rc = http_con_assign_server(ref, NULL, &events, NULL);

	conmgr_fd_free_ref(&ref);

	return rc;
}
