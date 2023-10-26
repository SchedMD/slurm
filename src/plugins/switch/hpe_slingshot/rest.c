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

#include "switch_hpe_slingshot.h"
#include "rest.h"

static char *_read_authfile(const char *auth_dir, const char *base);
static void _clear_auth_header(slingshot_rest_conn_t *conn);
static bool _get_auth_header(slingshot_rest_conn_t *conn,
			     struct curl_slist **headers, bool cache_use);


/*
 * Wrapper around curl_easy_setopt that jumps to 'err' on failure
 */
#define CURL_SETOPT(handle, opt, param) { \
	CURLcode _ret = curl_easy_setopt(handle, opt, param); \
	if (_ret != CURLE_OK) { \
		error("Couldn't set CURL option %d: %s", \
			opt, curl_easy_strerror(_ret)); \
		goto err; \
	} \
}

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
 * CURL write callback to receive data
 */
static size_t _rest_data_received(void *contents, size_t size, size_t nmemb,
				  void *userp)
{
	slingshot_rest_conn_t *conn = userp;

	/* Resize buffer if needed */
	size_t realsize = size * nmemb;
	size_t newlen = conn->datalen + realsize + 1;
	if (newlen > conn->datasiz) {
		char *newdata = xrealloc(conn->data, newlen);
		conn->data = newdata;
		conn->datasiz = newlen;
	}

	/* Write new data to buffer */
	memcpy(conn->data + conn->datalen, contents, realsize);
	conn->datalen += realsize;
	conn->data[conn->datalen] = 0;
	return realsize;
}

/*
 * This is used to make sure the connection has a valid data reading
 * function. Without this function, during a DELETE attempt, it appears
 * that CURL will try to read from a NULL FILE object, think it is getting
 * FD 0, and hang trying to read from STDIN.
 */
static size_t _read_function(void *contents, size_t size, size_t nmemb,
			     void *userp)
{
	(void)contents;
	(void)size;
	(void)nmemb;
	(void)userp;
	return 0;
}

#if CURL_TRACE
/*
 * Callback for libcurl tracing - print out datatype and data
 */
static int _libcurl_trace(CURL *handle, curl_infotype type, char *data,
			  size_t size, void *userp)
{
	char *buf = xmalloc(size + 1);
	char *typestr = "unknown";

	if (type == CURLINFO_TEXT)
		typestr = "text";
	else if (type == CURLINFO_HEADER_OUT)
		typestr = "header_out";
	else if (type == CURLINFO_DATA_OUT)
		typestr = "data_out";
	else if (type == CURLINFO_SSL_DATA_OUT)
		typestr = "ssl_data_out";
	else if (type == CURLINFO_HEADER_IN)
		typestr = "header_in";
	else if (type == CURLINFO_DATA_IN)
		typestr = "data_in";
	else if (type == CURLINFO_SSL_DATA_IN)
		typestr = "ssl_data_in";

	while (size > 0 && (data[size-1] == '\n' || data[size-1] == '\r'))
		data[--size] = '\0';

	for (int i = 0; i < size; i++)
		buf[i] = isprint(data[i]) ? data[i] : '_';
	buf[size] = '\0';

	log_flag(SWITCH, "%s: '%s'", typestr, buf);
	xfree(buf);
	return 0;
}
#endif

/*
 * Disconnect from REST connection (don't free auth or URL data)
 */
extern void slingshot_rest_disconnect(slingshot_rest_conn_t *conn)
{
	if (!conn->name)
		return;
	debug("disconnecting from '%s' REST interface", conn->name);
	if (conn->handle) {
		curl_easy_cleanup(conn->handle);
		conn->handle = 0;
	}
	xfree(conn->data);
	conn->datalen = 0;
	conn->datasiz = 0;
}

/* Return a string corresponding to the passed-in authentication type */
static const char *_auth_type_tostr(slingshot_rest_auth_t auth_type)
{
	switch (auth_type) {
	case SLINGSHOT_AUTH_BASIC:
		return "BASIC";
	case SLINGSHOT_AUTH_OAUTH:
		return "OAUTH";
	case SLINGSHOT_AUTH_NONE:
		return "NONE";
	default:
		return "unknown auth_type";
	}
}

/*
 * Generic handle set up function for network connections to use
 */
extern bool slingshot_rest_connect(slingshot_rest_conn_t *conn)
{
	log_flag(SWITCH, "name='%s' url=%s auth=%u to=%u cto=%u",
		 conn->name, conn->base_url, conn->auth.auth_type,
		 conn->timeout, conn->connect_timeout);

	/* If we're already connected, do nothing */
	if (conn->handle != NULL)
		return true;

	/* Set up the handle */
	conn->handle = curl_easy_init();
	if (conn->handle == NULL) {
		error("Couldn't initialize %s connection handle: %m",
		      conn->name);
		goto err;
	}

	/* Set options that don't change between requests */
	CURL_SETOPT(conn->handle, CURLOPT_TIMEOUT, conn->timeout);
	CURL_SETOPT(conn->handle, CURLOPT_CONNECTTIMEOUT,
		    conn->connect_timeout);
	CURL_SETOPT(conn->handle, CURLOPT_WRITEFUNCTION, _rest_data_received);
	CURL_SETOPT(conn->handle, CURLOPT_WRITEDATA, conn);
	CURL_SETOPT(conn->handle, CURLOPT_READFUNCTION, _read_function);
	CURL_SETOPT(conn->handle, CURLOPT_FOLLOWLOCATION, 0);

#if CURL_TRACE
	CURL_SETOPT(conn->handle, CURLOPT_DEBUGFUNCTION, _libcurl_trace);
	CURL_SETOPT(conn->handle, CURLOPT_VERBOSE, 1L);
#endif

	/* These are needed to work with self-signed certificates */
	CURL_SETOPT(conn->handle, CURLOPT_SSL_VERIFYPEER, 0);
	CURL_SETOPT(conn->handle, CURLOPT_SSL_VERIFYHOST, 0);

	/* If using basic auth, add the user name and password */
	if ((conn->auth.auth_type == SLINGSHOT_AUTH_BASIC) &&
	    conn->auth.u.basic.user_name && conn->auth.u.basic.password) {
		CURL_SETOPT(conn->handle, CURLOPT_USERNAME,
			    conn->auth.u.basic.user_name);
		CURL_SETOPT(conn->handle, CURLOPT_PASSWORD,
			    conn->auth.u.basic.password);
	}

	debug("Connected to %s at %s using %s auth", conn->name, conn->base_url,
	      _auth_type_tostr(conn->auth.auth_type));

	return true;

err:
	slingshot_rest_disconnect(conn);
	return false;
}

/*
 * Issue a request, and return the HTTP status and JSON-decoded result.
 * Caller must free the returned response with json_object_put.
 */
static bool _rest_request(slingshot_rest_conn_t *conn, long *status,
			  json_object **resp)
{
	CURLcode ret;

	/* Reset received data buffer */
	if (conn->data != NULL && conn->datasiz > 0)
		memset(conn->data, 0, conn->datasiz);
	conn->datalen = 0;

	/* Issue the request */
	ret = curl_easy_perform(conn->handle);
	if (ret != CURLE_OK) {
		error("Couldn't perform %s request: %s", conn->name,
		      curl_easy_strerror(ret));
		return false;
	}

	/* Get the HTTP status of the response */
	ret = curl_easy_getinfo(conn->handle, CURLINFO_RESPONSE_CODE, status);
	if (ret != CURLE_OK) {
		error("Couldn't get %s response code: %s", conn->name,
		      curl_easy_strerror(ret));
		return false;
	}

	/* Decode response into JSON */
	if ((*status != HTTP_NO_CONTENT) && (*status != HTTP_FORBIDDEN) &&
	    (*status != HTTP_UNAUTHORIZED)) {
		enum json_tokener_error jerr;
		*resp = json_tokener_parse_verbose(conn->data, &jerr);
		if (*resp == NULL) {
			error("Couldn't decode jackaloped response: %s",
			      json_tokener_error_desc(jerr));
			return false;
		}
	}

	return true;
}

/*
 * Internals of REST POST/PATCH/GET/DELETE calls, with retries, etc.
 */
static json_object *_rest_call(slingshot_rest_conn_t *conn, const char *type,
			       const char *urlsuffix, json_object *reqjson,
			       long *status, bool not_found_ok)
{
	json_object *respjson = NULL;
	struct curl_slist *headers = NULL;
	const char *req = NULL;
	bool use_cache = true;
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
	debug("%s %s url=%s data='%s'", conn->name, type, url, req);

	/* Create header list */
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!headers) {
		error("curl_slist_append failed to append Content-Type: %m");
		goto err;
	}
	if (!_get_auth_header(conn, &headers, use_cache))
		goto err;

	/* Set up connection handle for the specific operation */
	CURL_SETOPT(conn->handle, CURLOPT_URL, url);
	CURL_SETOPT(conn->handle, CURLOPT_HTTPHEADER, headers);
	if (!strcmp(type, "POST")) {
		CURL_SETOPT(conn->handle, CURLOPT_CUSTOMREQUEST, NULL);
		CURL_SETOPT(conn->handle, CURLOPT_POST, 1);
		CURL_SETOPT(conn->handle, CURLOPT_POSTFIELDS, req);
		CURL_SETOPT(conn->handle, CURLOPT_HTTPGET, 0);
	} else if (!strcmp(type, "PATCH")) {
		CURL_SETOPT(conn->handle, CURLOPT_CUSTOMREQUEST, "PATCH");
		CURL_SETOPT(conn->handle, CURLOPT_POST, 1);
		CURL_SETOPT(conn->handle, CURLOPT_POSTFIELDS, req);
		CURL_SETOPT(conn->handle, CURLOPT_HTTPGET, 0);
	} else if (!strcmp(type, "GET")) {
		CURL_SETOPT(conn->handle, CURLOPT_CUSTOMREQUEST, NULL);
		CURL_SETOPT(conn->handle, CURLOPT_POST, 0);
		CURL_SETOPT(conn->handle, CURLOPT_POSTFIELDS, NULL);
		CURL_SETOPT(conn->handle, CURLOPT_HTTPGET, 1);
	} else if (!strcmp(type, "DELETE")) {
		CURL_SETOPT(conn->handle, CURLOPT_CUSTOMREQUEST, "DELETE");
		CURL_SETOPT(conn->handle, CURLOPT_POST, 0);
		CURL_SETOPT(conn->handle, CURLOPT_POSTFIELDS, NULL);
		CURL_SETOPT(conn->handle, CURLOPT_HTTPGET, 0);
	}

	/* Issue the REST request and get the response (if any) */
	if (!_rest_request(conn, status, &respjson))
		goto err;

	if (((*status >= HTTP_OK) && (*status <= HTTP_LAST_OK)) ||
		(*status == HTTP_NOT_FOUND && not_found_ok)) {
		debug("%s %s %s successful (%ld)",
		      conn->name, type, url, *status);
	} else if ((*status == HTTP_FORBIDDEN || *status == HTTP_UNAUTHORIZED)
		   && (conn->auth.auth_type == SLINGSHOT_AUTH_OAUTH)
		   && use_cache) {
		debug("%s %s %s unauthorized status %ld, retrying",
		      conn->name, type, url, *status);
		/*
		 * on HTTP_{FORBIDDEN,UNAUTHORIZED}, free auth header
		 * and re-cache token
		 */
		curl_slist_free_all(headers);
		headers = NULL;
		use_cache = false;
		goto again;
	} else {
		_log_rest_detail(conn->name, type, url, respjson, *status);
		goto err;
	}

	curl_slist_free_all(headers);
	xfree(url);
	return respjson;

err:
	curl_slist_free_all(headers);
	xfree(url);
	json_object_put(respjson);
	return NULL;
}


/*
 * POST with JSON payload, and return the response (or NULL on error)
 */
extern json_object *slingshot_rest_post(slingshot_rest_conn_t *conn,
					const char *urlsuffix,
					json_object *reqjson, long *status)
{
	return _rest_call(conn, "POST", urlsuffix, reqjson, status, false);
}

/*
 * PATCH with JSON payload, and return the response (or NULL on error)
 */
extern json_object *slingshot_rest_patch(slingshot_rest_conn_t *conn,
					 const char *urlsuffix,
					 json_object *reqjson, long *status)
{
	return _rest_call(conn, "PATCH", urlsuffix, reqjson, status, true);
}

/*
 * Do a GET from the requested URL; return the JSON response,
 * or NULL on error
 */
extern json_object *slingshot_rest_get(slingshot_rest_conn_t *conn,
				       const char *urlsuffix, long *status)
{
	return _rest_call(conn, "GET", urlsuffix, NULL, status, true);
}

/*
 * DELETE the given URL; return true on success
 */
extern bool slingshot_rest_delete(slingshot_rest_conn_t *conn,
				  const char *urlsuffix, long *status)
{
	json_object *respjson = NULL;
	bool rc = true;

	/* Only delete if we successfully POSTed before */
	if (!conn || !conn->handle || !conn->base_url)
		return false;

	respjson = _rest_call(conn, "DELETE", urlsuffix, NULL, status, false);
	if (!respjson)
		rc = false;
	json_object_put(respjson);
	return rc;
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
	slingshot_rest_disconnect(conn);
	xfree(conn->name);
	xfree(conn->base_url);
	if (conn->auth.auth_type == SLINGSHOT_AUTH_BASIC) {
		xfree(conn->auth.u.basic.user_name);
		memset((void *)conn->auth.u.basic.password, 0,
			strlen(conn->auth.u.basic.password));
		xfree(conn->auth.u.basic.password);
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
	char *req = NULL;
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
		xstrcat(url, "/fabric/login");

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
				     "&client_id=%s&client_secret=%s",
				     client_id, client_secret);

		/* Connect and POST request to OAUTH token endpoint */
		if (!slingshot_rest_connection(&token_conn, url,
				       SLINGSHOT_AUTH_NONE, NULL, NULL, NULL,
				       SLINGSHOT_TOKEN_TIMEOUT,
				       SLINGSHOT_TOKEN_CONNECT_TIMEOUT,
				       "OAUTH token grant"))
			goto err;

		if (!slingshot_rest_connect(&token_conn))
			goto err;

		/* Set up connection handle for the POST */
		CURL_SETOPT(token_conn.handle, CURLOPT_URL, url);
		CURL_SETOPT(token_conn.handle, CURLOPT_CUSTOMREQUEST, NULL);
		CURL_SETOPT(token_conn.handle, CURLOPT_POST, 1);
		CURL_SETOPT(token_conn.handle, CURLOPT_POSTFIELDS, req);

		/* Issue the POST and get the response */
		if (!_rest_request(&token_conn, &status, &respjson))
			goto err;

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
	json_object_put(respjson);
	return retval;
}
