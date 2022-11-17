/*****************************************************************************\
 *  instant_on.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2022 Hewlett Packard Enterprise Development LP
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
#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif
#define CURL_TRACE 0	/* Turn on curl debug tracing */
#if CURL_TRACE
#include <ctype.h>
#endif

#include "src/common/slurm_xlator.h"

#include "switch_hpe_slingshot.h"

/* HTTP status values */
#define HTTP_OK                200
#define HTTP_NO_CONTENT        204
#define HTTP_REDIRECT          308
#define HTTP_UNAUTHORIZED      401
#define HTTP_FORBIDDEN         403
#define HTTP_SERVICE_UNAVAILABLE 503

typedef struct rest_conn {
	CURL *handle;	      /* CURL connection handle */
	char *data;	      /* Response data buffer */
	size_t datalen;	      /* Length of the response data */
	size_t datasiz;	      /* Size of the data buffer */
	const char *name;     /* Descriptive name for logging */
	const char *base_url; /* Base URL for connection */
	jlope_auth_t auth;    /* Authentication type (BASIC/OAUTH) */
} rest_conn_t;

static rest_conn_t jlope_conn;  /* Connection to jackaloped */

static bool instant_on_enabled = false;
static char *auth_header = NULL; /* cache OAUTH auth header here */
static char *_get_auth_header(bool cache_use);


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
 * CURL write callback to receive data
 */
static size_t _rest_data_received(void *contents, size_t size, size_t nmemb,
				  void *userp)
{
	rest_conn_t *conn = userp;

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

/*
 * Disconnect and free memory
 */
static void _rest_disconnect(rest_conn_t *conn)
{
	if (!conn->name)
		return;
	debug("disconnecting from '%s' REST interface", conn->name);
	if (conn->handle)
		curl_easy_cleanup(conn->handle);
	xfree(conn->data);
	memset(conn, 0, sizeof(*conn));
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
 * Generic handle set up function for UNIX and network connections to use
 */
static bool _rest_connect(rest_conn_t *conn, const char *name,
			  const char *url, jlope_auth_t auth,
			  long timeout, long connect_timeout,
			  const char *user_name, const char *password)
{
	log_flag(SWITCH, "conn->handle=%p name='%s' url=%s auth=%u to=%lu cto=%lu user_name=%s",
		conn->handle, name, url, auth, timeout, connect_timeout,
		user_name);

	/* If we're already connected, do nothing */
	if (conn->handle != NULL)
		return true;

	conn->name = name;

	/* Set up the handle */
	conn->handle = curl_easy_init();
	if (conn->handle == NULL) {
		error("Couldn't initialize %s connection handle: %m",
			conn->name);
		goto err;
	}
	conn->base_url = url;
	conn->auth = auth;

	/* Set options that don't change between requests */
	CURL_SETOPT(conn->handle, CURLOPT_TIMEOUT, timeout);
	CURL_SETOPT(conn->handle, CURLOPT_CONNECTTIMEOUT, connect_timeout);
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
	if (conn->auth == SLINGSHOT_JLOPE_AUTH_BASIC && user_name && password) {
		CURL_SETOPT(conn->handle, CURLOPT_USERNAME, user_name);
		CURL_SETOPT(conn->handle, CURLOPT_PASSWORD, password);
	}
	return true;

err:
	_rest_disconnect(conn);
	return false;
}

/*
 * Issue a request, and return the HTTP status and JSON-decoded result.
 * Caller must free the returned response with json_object_put.
 */
static bool _rest_request(rest_conn_t *conn, long *status, json_object **resp)
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
	if (*status != HTTP_NO_CONTENT && *status != HTTP_FORBIDDEN &&
			*status != HTTP_UNAUTHORIZED) {
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
 * POST with JSON payload, and return the response (or NULL on error)
 */
static json_object *_rest_post(rest_conn_t *conn, const char *urlsuffix,
			       json_object *reqjson, long *status)
{
	json_object *respjson = NULL;
	struct curl_slist *headers = NULL;
	const char *req = NULL;
	bool use_cache = true;
	char *url = NULL;

	xassert(conn != NULL);
	xassert(urlsuffix != NULL);
	xassert(reqjson != NULL);

	/* Create full URL */
	url = xstrdup_printf("%s%s", conn->base_url, urlsuffix);

	/* Dump JSON to string */
	if (!(req = json_object_to_json_string(reqjson))) {
		error("Couldn't dump JSON request: %m");
		goto err;
	}

again:
	debug("%s POST url=%s data='%s'", conn->name, url, req);

	/* Create header list */
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (conn->auth == SLINGSHOT_JLOPE_AUTH_OAUTH) {
		const char *hdr = _get_auth_header(use_cache);
		if (!hdr)
			goto err;
		headers = curl_slist_append(headers, hdr);
	}

	/* Set up connection handle for the POST */
	CURL_SETOPT(conn->handle, CURLOPT_URL, url);
	CURL_SETOPT(conn->handle, CURLOPT_CUSTOMREQUEST, NULL);
	CURL_SETOPT(conn->handle, CURLOPT_POST, 1);
	CURL_SETOPT(conn->handle, CURLOPT_POSTFIELDS, req);
	CURL_SETOPT(conn->handle, CURLOPT_HTTPHEADER, headers);

	/* Issue the POST and get the response */
	if (!_rest_request(conn, status, &respjson))
		goto err;

	/* On a successful response, grab the self URL */
	if (*status == HTTP_OK) {
		debug("%s POST %s successful", conn->name, url);
	} else if ((*status == HTTP_FORBIDDEN || *status == HTTP_UNAUTHORIZED)
		   && (conn->auth == SLINGSHOT_JLOPE_AUTH_OAUTH) && use_cache) {
		/*
		 * on HTTP_{FORBIDDEN,UNAUTHORIZED}, free auth header
		 * and re-cache token
		 */
		curl_slist_free_all(headers);
		headers = NULL;
		use_cache = false;
		goto again;
	} else {
		_log_rest_detail(conn->name, "POST", url, respjson, *status);
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
 * Return buffer with contents of authentication file with
 * pathname <jlope_authdir>/<base>; strip any trailing newlines
 */
static char *_read_authfile(const char *base)
{
	char *fname = NULL;
	int fd = -1;
	struct stat statbuf;
	size_t siz = 0;
	char *buf = NULL;

	fname = xstrdup_printf("%s/%s", slingshot_config.jlope_authdir, base);
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
static void _clear_auth_header(void)
{
	if (auth_header) {
		memset(auth_header, 0, strlen(auth_header));
		xfree(auth_header);
		auth_header = NULL;
	}
}

/*
 * If needed, access a token service to get an OAUTH2 auth token;
 * return the authorization header (and cache in 'auth_header' global);
 * if 'cache_use' is set, return the cached auth_header if set
 */
static char *_get_auth_header(bool cache_use)
{
	char *client_id = NULL;
	char *client_secret = NULL;
	char *url = NULL;
	rest_conn_t conn = { 0 };  /* Connection to token service */
	char *req = NULL;
	json_object *respjson = NULL;
	json_object *tokjson = NULL;
	const char *token = NULL;
	long status = 0;

	/* Use what we've got if allowed */
	if (auth_header && cache_use)
		return auth_header;

	/* Get a new token from the token service */
	_clear_auth_header();

	/* Get the token URL and client_{id,secret}, create request string */
	client_id = _read_authfile(SLINGSHOT_JLOPE_AUTH_OAUTH_CLIENT_ID_FILE);
	if (!client_id)
		goto err;
	client_secret =
		_read_authfile(SLINGSHOT_JLOPE_AUTH_OAUTH_CLIENT_SECRET_FILE);
	if (!client_secret)
		goto err;
	url = _read_authfile(SLINGSHOT_JLOPE_AUTH_OAUTH_ENDPOINT_FILE);
	if (url)
		goto err;
	req = xstrdup_printf("grant_type=client_credentials&client_id=%s"
			     "&client_secret=%s", client_id, client_secret);

	/* Connect and POST request to OAUTH token endpoint */
	if (!_rest_connect(&conn, "OAUTH token grant", url,
			   SLINGSHOT_JLOPE_AUTH_NONE, SLINGSHOT_JLOPE_TIMEOUT,
			   SLINGSHOT_JLOPE_CONNECT_TIMEOUT, NULL, NULL))
		goto err;

	/* Set up connection handle for the POST */
	CURL_SETOPT(conn.handle, CURLOPT_URL, url);
	CURL_SETOPT(conn.handle, CURLOPT_CUSTOMREQUEST, NULL);
	CURL_SETOPT(conn.handle, CURLOPT_POST, 1);
	CURL_SETOPT(conn.handle, CURLOPT_POSTFIELDS, req);

	/* Issue the POST and get the response */
	if (!_rest_request(&conn, &status, &respjson))
		goto err;

	/* On a successful response, get the access_token out of it */
	if (status == HTTP_OK) {
		debug("%s POST %s successful", conn.name, url);
	} else {
		_log_rest_detail(conn.name, "POST", url, respjson, status);
		goto err;
	}

	/* Create an authentication header from the access_token */
	tokjson = json_object_object_get(respjson, "access_token");
	if (!tokjson || !(token = json_object_get_string(tokjson))) {
		error("Couldn't get auth token from OAUTH service: json='%s'",
		      json_object_to_json_string(respjson));
		goto err;
	}
	auth_header = xstrdup_printf("Authorization: Bearer %s", token);

err:
	xfree(client_id);
	xfree(client_secret);
	xfree(url);
	_rest_disconnect(&conn);
	xfree(req);
	json_object_put(respjson);
	return auth_header;
}

/*
 * Read any authentication files and connect to the jackalope daemon,
 * which implements a REST interface providing Instant On data
 */
extern bool slingshot_init_instant_on(void)
{
	char *basic_username = NULL;
	char *basic_passwd = NULL;

	if (slingshot_config.jlope_auth == SLINGSHOT_JLOPE_AUTH_NONE) {
		info("Slingshot Instant On support not configured");
		instant_on_enabled = false;
		return false;
	} else if (slingshot_config.jlope_auth == SLINGSHOT_JLOPE_AUTH_BASIC) {
		basic_username = SLINGSHOT_JLOPE_AUTH_BASIC_USER;
		if (!(basic_passwd = _read_authfile(
					SLINGSHOT_JLOPE_AUTH_BASIC_PWD_FILE)))
			goto err;
	} else if (slingshot_config.jlope_auth == SLINGSHOT_JLOPE_AUTH_OAUTH) {
		/* Attempt to get an OAUTH token for later use */
		if (!_get_auth_header(false))
			goto err;
	} else {
		error("Invalid jlope_auth value %u",
			slingshot_config.jlope_auth);
		goto err;
	}
	if (!_rest_connect(&jlope_conn, "Slingshot Jackalope daemon",
			   slingshot_config.jlope_url,
			   slingshot_config.jlope_auth,
			   SLINGSHOT_JLOPE_TIMEOUT,
			   SLINGSHOT_JLOPE_CONNECT_TIMEOUT,
			   basic_username, basic_passwd))
		goto err;

	debug("Connected to %s at %s using %s auth",
	      jlope_conn.name, jlope_conn.base_url,
	      (slingshot_config.jlope_auth == SLINGSHOT_JLOPE_AUTH_BASIC) ?
		"BASIC" : "OAUTH");

	instant_on_enabled = true;
	xfree(basic_passwd);
	return true;

err:
	info("Instant On support disabled due to errors");
	instant_on_enabled = false;
	xfree(basic_passwd);
	return false;
}

/*
 * Close connection to jackakoped REST interface, free memory
 */
extern void slingshot_fini_instant_on(void)
{
	_rest_disconnect(&jlope_conn);
	_clear_auth_header();
}

/*
 * Convert string node list (i.e. "nid00000[2-3]") into JSON
 * array of node names
 */
static json_object *_node_list_to_json_array(char *node_list, uint32_t node_cnt)
{
	hostlist_t hl = NULL;
	json_object *host_array = NULL;
	char *host;
	int ents;

	log_flag(SWITCH, "node_list=%s node_cnt=%u", node_list, node_cnt);
	if (!(host_array = json_object_new_array())) {
		error("Couldn't create host array");
		return NULL;
	}
	/* Optimization for single-node job steps */
	if (node_cnt == 1) {
		if (json_object_array_add(host_array,
				json_object_new_string(node_list))) {
			error("Couldn't add node list to host array");
			goto err;
		}
		return host_array;
	}
	if (!(hl = hostlist_create_dims(node_list, 0))) {
		error("Couldn't convert node list to hostlist");
		goto err;
	}
	for (ents = 0; (host = hostlist_shift_dims(hl, 0)); ents++) {
		if (json_object_array_add(host_array,
				json_object_new_string(host))) {
			error("Couldn't add host to host array");
			free(host);
			goto err;
		}
		free(host);
	}
	if (ents != node_cnt) {
		error("host_array ents %d != %u node_cnt", ents, node_cnt);
		goto err;
	}
	hostlist_destroy(hl);
	return host_array;
err:
	hostlist_destroy(hl);
	json_object_put(host_array);
	return NULL;
}

/*
 * Parse a single node's MAC address/device_name/numa_node arrays,
 * and append the info for each NIC to the job->nics[nicidx] array;
 * return the index of the array to use next (or -1 on error)
 */
static int _parse_jlope_node_json(slingshot_jobinfo_t *job,
				  int node_cnt, int nodeidx, int nicidx,
				  json_object *macs, json_object *devs,
				  json_object *numas)
{
	size_t macs_siz, devs_siz, numas_siz;

	if (!json_object_is_type(macs, json_type_array) ||
	    !json_object_is_type(devs, json_type_array) ||
	    !json_object_is_type(numas, json_type_array)) {
		error("Type error with jackaloped node response: macs=%d devs=%d numas=%d (should be %d)",
			json_object_get_type(macs), json_object_get_type(devs),
			json_object_get_type(numas), json_type_array);
		return -1;
	}
	macs_siz = json_object_array_length(macs);
	devs_siz = json_object_array_length(devs);
	numas_siz = json_object_array_length(numas);
	if (macs_siz != devs_siz || devs_siz != numas_siz) {
		error("Size error with jackaloped node response: macs=%zd devs=%zd numas=%zd",
			macs_siz, devs_siz, numas_siz);
		return -1;
	}

	/* Allocate/grow nics array if needed */
	if (nicidx >= job->num_nics) {
		if (job->num_nics == 0)
			job->num_nics = node_cnt * macs_siz;
		else
			job->num_nics += macs_siz;
		job->nics = xrecalloc(job->nics, job->num_nics,
				      sizeof(*(job->nics)));
		log_flag(SWITCH, "nics: nicidx/num_nics %d/%d",
			 nicidx, job->num_nics);
	}

	for (int i = 0; i < macs_siz; i++) {
		const char *mac = json_object_to_json_string(
					json_object_array_get_idx(macs, i));
		const char *dev = json_object_to_json_string(
					json_object_array_get_idx(devs, i));
		int numa = json_object_get_int(
					json_object_array_get_idx(numas, i));
		slingshot_hsn_nic_t *nic = &job->nics[nicidx];
		nic->nodeidx = nodeidx;
		nic->address_type = SLINGSHOT_ADDR_MAC;
		strlcpy(nic->address, mac, sizeof(nic->address));
		nic->numa_node = numa;
		strlcpy(nic->device_name, dev, sizeof(nic->device_name));

		log_flag(SWITCH, "nics[%d/%d].nodeidx=%d mac=%s dev=%s numa=%d",
			 nicidx, job->num_nics, nic->nodeidx, nic->address,
			 nic->device_name, nic->numa_node);
		nicidx++;
	}
	return nicidx;
}

/*
 * Parse the JSON response from jackaloped: 3 arrays of arrays of
 * MAC addresses, device names, and numa distances; looks like so:
 * {
 *   "mac": [["AA:BB:CC:DD:EE:FF", "FF:BB:CC:DD:EE:AA"]],
 *   "device": [["cxi0", "cxi1"]],
 *   "numa": [[126, 127]]
 * }
 * Add the information to the job->nics array to pass to slurmd
 */
static bool _parse_jlope_json(slingshot_jobinfo_t *job, json_object *resp,
			      int node_cnt)
{
	json_object *macs = json_object_object_get(resp, "mac");
	json_object *devs = json_object_object_get(resp, "device");
	json_object *numas = json_object_object_get(resp, "numa");
	size_t macs_siz, devs_siz, numas_siz;
	json_object *nodemacs, *nodedevs, *nodenumas;
	int nicidx = 0;

	if (!json_object_is_type(macs, json_type_array) ||
	    !json_object_is_type(devs, json_type_array) ||
	    !json_object_is_type(numas, json_type_array)) {
		error("Type error with jackaloped response: macs=%d devs=%d numas=%d (should be %d)",
			json_object_get_type(macs), json_object_get_type(devs),
			json_object_get_type(numas), json_type_array);
		return false;
	}
	macs_siz = json_object_array_length(macs);
	devs_siz = json_object_array_length(devs);
	numas_siz = json_object_array_length(numas);
	if (macs_siz != devs_siz || devs_siz != numas_siz ||
			numas_siz != node_cnt) {
		error("Size error with jackaloped response: macs=%zd devs=%zd numas=%zd nodes=%d",
			macs_siz, devs_siz, numas_siz, node_cnt);
		return false;
	}
	for (int i = nicidx = 0; i < macs_siz; i++) {
		nodemacs = json_object_array_get_idx(macs, i);
		nodedevs = json_object_array_get_idx(devs, i);
		nodenumas = json_object_array_get_idx(numas, i);
		if ((nicidx = _parse_jlope_node_json(job, node_cnt, i, nicidx,
				            nodemacs, nodedevs, nodenumas)) < 0)
			goto err;
	}

	/* Shrink 'nics' array if too large and attach to job struct */
	if (nicidx < job->num_nics) {
		job->num_nics = nicidx;
		job->nics = xrecalloc(job->nics, job->num_nics,
				      sizeof(*(job->nics)));
	}
	return true;

err:
	xfree(job->nics);
	return false;
}

/*
 * If configured with the jackaloped REST URL, contact jackaloped and
 * get Instant On data for the set of nodes in the job step
 */
extern bool slingshot_fetch_instant_on(slingshot_jobinfo_t *job,
				       char *node_list, uint32_t node_cnt)
{
	json_object *host_array = NULL;
	json_object *reqjson = NULL;
	json_object *respjson = NULL;
	long status = 0;
	bool rc = false;

	if (!slingshot_config.jlope_url || !instant_on_enabled)
		return true;

	if (!(host_array = _node_list_to_json_array(node_list, node_cnt)))
		goto out;
	if (!(reqjson = json_object_new_object()) ||
		json_object_object_add(reqjson, "hosts", host_array)) {
		error("Couldn't create instant on request json");
		json_object_put(host_array);
		goto out;
	}
	log_flag(SWITCH, "reqjson='%s'", json_object_to_json_string(reqjson));

	if (!(respjson = _rest_post(&jlope_conn, "/fabric/nics", reqjson,
				    &status))) {
		error("POST to jackaloped for instant on data failed: %ld",
		      status);
		goto out;
	}

	if (!(rc = _parse_jlope_json(job, respjson, node_cnt)))
		error("Couldn't parse jackaloped response: json='%s'",
		      json_object_to_json_string(respjson));

out:
	json_object_put(reqjson);
	json_object_put(respjson);
	return rc;
}
