/*****************************************************************************\
 *  rest.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2022-2023 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#define _GNU_SOURCE

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define CURL_TRACE 0	/* Turn on curl debug tracing */
#if CURL_TRACE
#include <ctype.h>
#endif

#include "src/common/slurm_xlator.h"
#include "src/curl/slurm_curl.h"

#include "switch_hpe_slingshot.h"
#include "rest.h"

static char *_read_authfile(const char *auth_dir, const char *base);
static void _clear_auth_header(slingshot_rest_conn_t *conn);
static bool _get_auth_header(slingshot_rest_conn_t *conn,
			     struct curl_slist **headers, bool cache_use);
/*
 * If an error response was received, log it
 */
static void _log_rest_detail(const char *name, const char *method,
			     const char *url, json_object *respjson,
			     long status)
{
	json_object *detail = NULL;

	if (!(detail = json_object_object_get(respjson, "detail"))) {
		error("%s %s %s status %ld no error details",
			name, method, url, status);
	} else {
		error("%s %s %s status %ld: %s", name, method, url, status,
			json_object_get_string(detail));
	}
}

/*
 * Internals of REST POST/PATCH/GET/DELETE calls, with retries, etc.
 */
static json_object *_rest_call(slingshot_rest_conn_t *conn,
			       http_request_method_t request_method,
			       const char *urlsuffix, json_object *reqjson,
			       long *status, bool not_found_ok)
{
	json_object *respjson = NULL;
	struct curl_slist *headers = NULL;
	const char *req = NULL;
	bool use_cache = true;
	char *response_str = NULL, *username = NULL, *password = NULL;
	char *url = NULL;

	xassert(conn != NULL);
	xassert(urlsuffix != NULL);

	/* Create full URL */
	url = xstrdup_printf("%s%s", conn->base_url, urlsuffix);

	/* If present, dump JSON payload to string */
	if (reqjson) {
		if (!(req = json_object_to_json_string(reqjson))) {
			error("Couldn't dump JSON request: %m");
			goto err;
		}
	}

again:
	debug("%s %s url=%s data='%s'", conn->name,
	      get_http_method_string(request_method), url, req);

	/* Create header list */
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!headers) {
		error("curl_slist_append failed to append Content-Type: %m");
		goto err;
	}
	if (!_get_auth_header(conn, &headers, use_cache))
		goto err;

	/* If using basic auth, add the user name and password */
	if ((conn->auth.auth_type == SLINGSHOT_AUTH_BASIC) &&
	    conn->auth.u.basic.user_name && conn->auth.u.basic.password) {
		username = conn->auth.u.basic.user_name;
		password = conn->auth.u.basic.password;
	}

	if (slurm_curl_request(req, url, username, password, headers,
			       conn->timeout, &response_str, status,
			       request_method, false))
		goto err;

	/* Decode response into JSON */
	if (response_str && response_str[0]) {
		enum json_tokener_error jerr;
		respjson = json_tokener_parse_verbose(response_str, &jerr);
		if (respjson == NULL) {
			error("Couldn't decode %s response: %s (data '%s')",
			      conn->name, json_tokener_error_desc(jerr),
			      response_str);
			goto err;
		}
	} else if (request_method != HTTP_REQUEST_DELETE) {
		debug("%s %s %s No response data received %ld, retrying",
		      conn->name, get_http_method_string(request_method), url,
		      *status);
		goto err;
	}

	if (((*status >= HTTP_OK) && (*status <= HTTP_LAST_OK)) ||
		(*status == HTTP_NOT_FOUND && not_found_ok)) {
		debug("%s %s %s successful (%ld)", conn->name,
		      get_http_method_string(request_method), url, *status);
	} else if (((*status == HTTP_UNAUTHORIZED) ||
		    (*status == HTTP_FORBIDDEN)) &&
		   (conn->auth.auth_type == SLINGSHOT_AUTH_OAUTH) &&
		   use_cache) {
		debug("%s %s %s unauthorized status %ld, retrying", conn->name,
		      get_http_method_string(request_method), url, *status);
		/*
		 * On HTTP_UNAUTHORIZED, free auth header and re-cache token
		 */
		curl_slist_free_all(headers);
		headers = NULL;
		use_cache = false;
		goto again;
	} else {
		_log_rest_detail(conn->name,
				 get_http_method_string(request_method), url,
				 respjson, *status);
		if (json_object_put(respjson))
			respjson = NULL;
		goto err;
	}

err:
	curl_slist_free_all(headers);
	xfree(url);
	xfree(response_str);
	return respjson; /* NULL on error */
}


/*
 * POST with JSON payload, and return the response (or NULL on error)
 */
extern json_object *slingshot_rest_post(slingshot_rest_conn_t *conn,
					const char *urlsuffix,
					json_object *reqjson, long *status)
{
	return _rest_call(conn, HTTP_REQUEST_POST, urlsuffix, reqjson, status,
			  false);
}

/*
 * PATCH with JSON payload, and return the response (or NULL on error)
 */
extern json_object *slingshot_rest_patch(slingshot_rest_conn_t *conn,
					 const char *urlsuffix,
					 json_object *reqjson, long *status)
{
	return _rest_call(conn, HTTP_REQUEST_PATCH, urlsuffix, reqjson, status,
			  true);
}

/*
 * Do a GET from the requested URL; return the JSON response,
 * or NULL on error
 */
extern json_object *slingshot_rest_get(slingshot_rest_conn_t *conn,
				       const char *urlsuffix, long *status)
{
	return _rest_call(conn, HTTP_REQUEST_GET, urlsuffix, NULL, status,
			  true);
}

/*
 * DELETE the given URL; return true on success
 */
extern bool slingshot_rest_delete(slingshot_rest_conn_t *conn,
				  const char *urlsuffix, long *status)
{
	/* Only delete if we successfully POSTed before */
	if (!conn || !conn->base_url)
		return false;

	/* Don't need the response. Just free it with json_object_put */
	json_object_put(_rest_call(conn, HTTP_REQUEST_DELETE, urlsuffix, NULL,
				   status, false));

	if ((*status >= HTTP_OK) && (*status <= HTTP_LAST_OK))
		return true;
	else
		return false;
}

/*
 * Generic handle set up function for network connections to use
 */
extern bool slingshot_rest_connection(slingshot_rest_conn_t *conn,
				      const char *url,
				      slingshot_rest_auth_t auth_type,
				      const char *auth_dir,
				      const char *basic_user,
				      const char *basic_pwdfile,
				      int timeout,
				      int connect_timeout,
				      const char *conn_name)
{
	memset(conn, 0, sizeof(*conn));
	switch (auth_type) {
	case SLINGSHOT_AUTH_BASIC:
		conn->auth.auth_type = auth_type;
		conn->auth.u.basic.user_name = xstrdup(basic_user);
		if (!(conn->auth.u.basic.password = _read_authfile(
						auth_dir, basic_pwdfile)))
			return false;
		break;
	case SLINGSHOT_AUTH_OAUTH:
	case SLINGSHOT_AUTH_NONE:
		conn->auth.auth_type = auth_type;
		break;
	default:
		error("Invalid auth_type value %u", auth_type);
		return false;
	}
	conn->name = xstrdup(conn_name);
	conn->base_url = xstrdup(url);
	conn->auth.auth_dir = xstrdup(auth_dir);
	conn->timeout = timeout;
	conn->connect_timeout = connect_timeout;

	/*
	 * Attempt to get an OAUTH token for later use
	 * (returns immediately if not OAUTH)
	 */
	if (!_get_auth_header(conn, NULL, false))
		return false;

	return true;
}

/*
 * Free data (including auth data) in this connection
 */
extern void slingshot_rest_destroy_connection(slingshot_rest_conn_t *conn)
{
	xfree(conn->name);
	xfree(conn->base_url);
	if (conn->auth.auth_type == SLINGSHOT_AUTH_BASIC) {
		xfree(conn->auth.u.basic.user_name);
		if (conn->auth.u.basic.password) {
			memset((void *) conn->auth.u.basic.password, 0,
			       strlen(conn->auth.u.basic.password));
			xfree(conn->auth.u.basic.password);
		}
	}
	xfree(conn->auth.auth_dir);
	_clear_auth_header(conn);
}

/*
 * Return buffer with contents of authentication file with
 * pathname <auth_dir>/<base>; strip any trailing newlines
 */
static char *_read_authfile(const char *auth_dir, const char *base)
{
	char *fname = NULL;
	int fd = -1;
	struct stat statbuf;
	size_t siz = 0;
	char *buf = NULL;

	fname = xstrdup_printf("%s/%s", auth_dir, base);
	fd = open(fname, O_RDONLY);
	if (fd == -1) {
		error("Couldn't open %s: %m", fname);
		goto rwfail;
	}
	if (fstat(fd, &statbuf) == -1) {
		error("fstat failed on %s: %m", fname);
		goto rwfail;
	}
	siz = statbuf.st_size;
	buf = xmalloc(siz + 1);
	safe_read(fd, buf, siz);
	while (siz > 0 && buf[siz - 1] == '\n')
		siz--;
	buf[siz] = '\0';

	xfree(fname);
	close(fd);
	return buf;

rwfail:
	xfree(fname);
	xfree(buf);
	close(fd);
	return NULL;
}

/*
 * Clear OAUTH authentication header
 */
static void _clear_auth_header(slingshot_rest_conn_t *conn)
{
	if (conn->auth.u.oauth.auth_cache) {
		memset((void *)conn->auth.u.oauth.auth_cache, 0,
		       strlen(conn->auth.u.oauth.auth_cache));
		xfree(conn->auth.u.oauth.auth_cache);
	}
}

/*
 * If needed, access a token service to get an OAUTH2 auth token;
 * on success, cache the authorization header in conn->auth.u.oauth.auth_cache,
 * add the header to *headers and return true;
 * if 'cache_use' is set, return the cached auth_header if set
 */
static bool _get_auth_header(slingshot_rest_conn_t *conn,
			     struct curl_slist **headers, bool cache_use)
{
	bool retval = false;
	char *client_id = NULL;
	char *client_secret = NULL;
	char *url = NULL;
	slingshot_rest_conn_t token_conn = { 0 };  /* Conn to token service */
	char *req = NULL, *response_str = NULL;
	json_object *respjson = NULL;
	json_object *tokjson = NULL;
	const char *token = NULL;
	long status = 0;
	struct curl_slist *newhdrs = NULL;

	/* Just return if not OAUTH */
	if (conn->auth.auth_type != SLINGSHOT_AUTH_OAUTH)
		return true;

	/* Use token service to get token unless cache_use set (or 1st call) */
	if (!cache_use || !conn->auth.u.oauth.auth_cache) {

		/* Get a new token from the token service */
		_clear_auth_header(conn);

		/* Get the token URL and client_{id,secret}, create request */
		url = _read_authfile(conn->auth.auth_dir,
				     SLINGSHOT_AUTH_OAUTH_ENDPOINT_FILE);
		if (!url)
			goto err;

		client_id = _read_authfile(conn->auth.auth_dir,
					   SLINGSHOT_AUTH_OAUTH_CLIENT_ID_FILE);
		if (!client_id)
			goto err;
		client_secret =
			_read_authfile(conn->auth.auth_dir,
				       SLINGSHOT_AUTH_OAUTH_CLIENT_SECRET_FILE);
		if (!client_secret)
			goto err;
		req = xstrdup_printf("grant_type=client_credentials"
				     "&client_id=%s&client_secret=%s"
				     "&scope=openid",
				     client_id, client_secret);

		if (slurm_curl_request(req, url, NULL, NULL, NULL,
				       SLINGSHOT_TOKEN_TIMEOUT, &response_str,
				       &status, HTTP_REQUEST_POST, false))
			goto err;

		/* Decode response into JSON */
		if (response_str && response_str[0]) {
			enum json_tokener_error jerr;
			respjson = json_tokener_parse_verbose(response_str,
							      &jerr);
			if (respjson == NULL) {
				error("Couldn't decode %s response: %s (data '%s')",
				      conn->name, json_tokener_error_desc(jerr),
				      response_str);
				goto err;
			}
		} else {
			error("%s No http response recieved. Status: %ld",
			      conn->name, status);
			goto err;
		}

		/* On a successful response, get the access_token out of it */
		if (status == HTTP_OK) {
			debug("%s POST %s successful", token_conn.name, url);
		} else {
			_log_rest_detail(token_conn.name, "POST", url,
					 respjson, status);
			goto err;
		}

		/* Create an authentication header from the access_token */
		tokjson = json_object_object_get(respjson, "access_token");
		if (!tokjson || !(token = json_object_get_string(tokjson))) {
			error("Couldn't get auth token from OAUTH service: json='%s'",
			      json_object_to_json_string(respjson));
			goto err;
		}
		conn->auth.u.oauth.auth_cache = xstrdup_printf(
					"Authorization: Bearer %s", token);
	}

	/* Append new header and return */
	if (!headers) {
		retval = true;
	} else if (!(newhdrs = curl_slist_append(*headers,
					conn->auth.u.oauth.auth_cache))) {
		error("curl_slist_append couldn't add OAUTH header");
		/* retval already false */
	} else {
		*headers = newhdrs;
		retval = true;
	}

err:
	xfree(client_id);
	xfree(client_secret);
	xfree(url);
	slingshot_rest_destroy_connection(&token_conn);
	xfree(req);
	xfree(response_str);
	json_object_put(respjson);
	return retval;
}
