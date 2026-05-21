/*****************************************************************************\
 *  http_auth_jwt.c - Slurm HTTP auth jwt plugin
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

#include "slurm/slurm_errno.h"

#include "src/common/http_con.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"

#include "src/plugins/auth/jwt/auth_jwt.h"

#define HTTP_HEADER_USER_TOKEN "X-SLURM-USER-TOKEN"
#define HTTP_HEADER_AUTH "Authorization"
#define HTTP_HEADER_AUTH_BEARER "Bearer "
#define HTTP_HEADER_USER_NAME "X-SLURM-USER-NAME"

static auth_context_t http_ctxt = { 0 };

extern int http_auth_p_init(const char *auth_info)
{
	static bool run_once = false;

	if (run_once)
		return SLURM_SUCCESS;

	run_once = true;

	parse_auth_params(&http_ctxt, auth_info);
	init_jwks(&http_ctxt, auth_info);
	init_hs256(&http_ctxt, auth_info);

	return SLURM_SUCCESS;
}

extern void http_auth_p_fini(void)
{
	FREE_NULL_DATA(http_ctxt.jwks);
	FREE_NULL_BUFFER(http_ctxt.key);
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
	int rc = ESLURM_AUTH_CRED_INVALID;
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	auth_token_t *cred = NULL;

	/* Always set UID to nobody */
	if (uid_ptr)
		*uid_ptr = SLURM_AUTH_NOBODY;

	if ((rc = _headers(&token, &user_name, name, request)))
		return rc;

	if (!(cred = auth_p_create(NULL, geteuid(), NULL, 0)))
		return ENOMEM;

	cred_set_token(cred, token, user_name);

	if (!(rc = cred_verify(&http_ctxt, cred))) {
		cred_get_ids(&http_ctxt, cred, &uid, &gid);

		if (((uid == SLURM_AUTH_NOBODY) || (gid == SLURM_AUTH_NOBODY)))
			rc = ESLURM_AUTH_NOBODY;

		if (uid_ptr)
			*uid_ptr = uid;
	}

	auth_p_destroy(cred);
	return rc;
}

extern int http_auth_p_proxy_token(http_con_t *hcon, const char *name,
				   const http_con_request_t *request)
{
	const char *token = NULL, *user_name = NULL;
	int rc = ESLURM_AUTH_CRED_INVALID;

	if ((rc = _headers(&token, &user_name, name, request)))
		return rc;

	return auth_p_thread_config(token, user_name);
}
