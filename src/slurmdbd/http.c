/*****************************************************************************\
 *  http.c - Implementation for handling HTTP requests
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "slurm/slurm_errno.h"

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_mime.h"
#include "src/common/http_request.h"
#include "src/common/http_router.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/http_auth.h"

#include "src/slurmdbd/http.h"
#include "src/slurmdbd/read_config.h"

static int _reply_error(http_con_t *hcon, const char *name,
			const http_con_request_t *request, int err,
			bool close_header)
{
	char *body = NULL, *at = NULL;
	int rc = EINVAL;

	xstrfmtcatat(body, &at, "slurmdbd HTTP server request for '%s %s':\n",
		     get_http_method_string(request->method),
		     request->url.path);

	if (err)
		xstrfmtcatat(body, &at, "Failed: %s\n", slurm_strerror(err));

	rc = http_con_send_response(hcon, http_status_from_error(err), NULL,
				    close_header,
				    &SHADOW_BUF_INITIALIZER(body, strlen(body)),
				    MIME_TYPE_TEXT);

	xfree(body);
	return rc;
}

static int _req_not_found(http_con_t *hcon, const char *name,
			  const http_con_request_t *request, void *arg,
			  void *path_arg)
{
	return _reply_error(hcon, name, request, ESLURM_URL_INVALID_PATH,
			    false);
}

static int _req_root(http_con_t *hcon, const char *name,
		     const http_con_request_t *request, void *arg,
		     void *path_arg)
{
	static const char body[] = "slurmdbd index of endpoints:\n";

	return http_con_send_response(hcon,
				      http_status_from_error(SLURM_SUCCESS),
				      NULL, false,
				      &SHADOW_BUF_INITIALIZER(body,
							      strlen(body)),
				      MIME_TYPE_TEXT);
}

extern void http_init(void)
{
	int rc = EINVAL;

	if ((rc = http_auth_g_init(slurmdbd_conf->http_auth_params, NULL,
				   NULL)))
		fatal("http authentication plugins failed to load: %s",
		      slurm_strerror(rc));

	http_router_init(_req_not_found);
	http_router_bind(HTTP_REQUEST_GET, "/", _req_root, NULL, NULL);
}

extern void http_fini(void)
{
	http_router_fini();
	http_auth_g_fini();
}

extern int on_http_connection(conmgr_fd_t *con)
{
	static const http_con_server_events_t events = {
		.on_request = http_router_on_request,
	};
	int rc = EINVAL;
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(con);

	rc = http_con_assign_server(ref, NULL, &events, NULL);

	CONMGR_CON_UNLINK(ref);

	return rc;
}
