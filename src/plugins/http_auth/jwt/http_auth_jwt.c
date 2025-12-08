/*****************************************************************************\
 *  http_auth_jwt.c - Slurm HTTP auth jwt plugin
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

#include "config.h"

#include <stdint.h>

#include "src/common/http_con.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "slurm/slurm_errno.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/http_auth.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "HTTP JWT authentication";
const char plugin_type[] = "http_auth/jwt";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for http_auth plugins: */
const uint32_t plugin_id = HTTP_AUTH_PLUGIN_JWT;

#define HTTP_HEADER_USER_TOKEN "X-SLURM-USER-TOKEN"
#define HTTP_HEADER_AUTH "Authorization"
#define HTTP_HEADER_AUTH_BEARER "Bearer "
#define HTTP_HEADER_USER_NAME "X-SLURM-USER-NAME"

extern int http_auth_p_init(void)
{
	return SLURM_SUCCESS;
}

extern void http_auth_p_fini(void)
{
	/* do nothing */
}

static int _headers(const char **token_ptr, const char **user_name_ptr,
		    const char *name, const http_con_request_t *request)
{
	const char *token = NULL, *user_name = NULL, *bearer = NULL;

	token = find_http_header(request->headers, HTTP_HEADER_USER_TOKEN);
	bearer = find_http_header(request->headers, HTTP_HEADER_AUTH);
	user_name = find_http_header(request->headers, HTTP_HEADER_USER_NAME);

	if (!token && !user_name && !bearer) {
		debug3("%s: [%s] skipping token authentication",
		       __func__, name);
		return ESLURM_AUTH_SKIP;
	}

	if (!token && !bearer) {
		error("%s: [%s] missing header user token: %s",
		      __func__, name, HTTP_HEADER_USER_TOKEN);
		return ESLURM_AUTH_CRED_INVALID;
	} else if (token && bearer) {
		error("%s: [%s] mutually exclusive headers %s and %s found. Rejecting ambiguous authentication request.",
		      __func__, name, HTTP_HEADER_USER_TOKEN, HTTP_HEADER_AUTH);
		return ESLURM_AUTH_CRED_INVALID;
	}

	if (bearer) {
		if (!xstrncmp(HTTP_HEADER_AUTH_BEARER, bearer,
			      strlen(HTTP_HEADER_AUTH_BEARER))) {
			token = (bearer + strlen(HTTP_HEADER_AUTH_BEARER));
		} else {
			error("%s: [%s] unexpected format for %s header: %s",
			      __func__, name, HTTP_HEADER_AUTH, bearer);
			return ESLURM_AUTH_CRED_INVALID;
		}
	}

	*token_ptr = token;
	*user_name_ptr = user_name;
	return SLURM_SUCCESS;
}

extern int http_auth_p_authenticate(uid_t *uid_ptr, http_con_t *hcon,
				    const char *name,
				    const http_con_request_t *request)
{
	const char *token = NULL, *user_name = NULL;
	void *cred = NULL;
	int rc = ESLURM_AUTH_CRED_INVALID;
	uid_t uid = SLURM_AUTH_NOBODY;

	if ((rc = _headers(&token, &user_name, name, request)))
		return rc;

	if ((cred = auth_g_cred_generate(AUTH_PLUGIN_JWT, token, user_name)) &&
	    !(rc = auth_g_verify(cred, slurm_conf.authinfo)))
		uid = auth_g_get_uid(cred);

	if (uid == SLURM_AUTH_NOBODY)
		rc = ESLURM_AUTH_NOBODY;

	if (uid_ptr)
		*uid_ptr = uid;

	auth_g_destroy(cred);
	return rc;
}

extern int http_auth_p_proxy_token(http_con_t *hcon, const char *name,
				   const http_con_request_t *request)
{
	const char *token = NULL, *user_name = NULL;
	int rc = ESLURM_AUTH_CRED_INVALID;

	if ((rc = _headers(&token, &user_name, name, request)))
		return rc;

	return auth_g_thread_config(token, user_name);
}
