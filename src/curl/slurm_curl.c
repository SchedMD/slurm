/*****************************************************************************\
 *  slurm_curl.c
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

#include <curl/curl.h>
#include <inttypes.h>

#include "src/common/http.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/xstring.h"
#include "src/curl/slurm_curl.h"
#include "src/interfaces/acct_gather_profile.h"
#include "src/interfaces/serializer.h"

/* Type for handling HTTP responses */
struct http_response {
	char *message;
	size_t size;
};

/*
 * Wrapper around CURL_SETOPT that jumps to 'err' on failure
 */
#define CURL_SETOPT(handle, opt, param) { \
	CURLcode _ret = curl_easy_setopt(handle, opt, param); \
	if (_ret != CURLE_OK) { \
		error("Couldn't set CURL option %d: %s", \
			opt, curl_easy_strerror(_ret)); \
		goto err; \
	} \
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

	while ((size > 0) &&
	       ((data[size - 1] == '\n') || (data[size - 1] == '\r')))
		data[--size] = '\0';

	for (int i = 0; i < size; i++)
		buf[i] = isprint(data[i]) ? data[i] : '_';
	buf[size] = '\0';

	log_flag(SWITCH, "%s: '%s'", typestr, buf);
	xfree(buf);
	return 0;
}
#endif

/* Callback to handle the HTTP response */
static size_t _write_callback(void *contents, size_t size, size_t nmemb,
			      void *userp)
{
	size_t realsize = size * nmemb;
	struct http_response *mem = (struct http_response *) userp;

	mem->message = xrealloc(mem->message, mem->size + realsize + 1);

	memcpy(&(mem->message[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->message[mem->size] = 0;

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
	(void) contents;
	(void) size;
	(void) nmemb;
	(void) userp;
	return 0;
}

extern int slurm_curl_request(const char *data, const char *url,
			      const char *username, const char *password,
			      struct curl_slist *headers, uint32_t timeout,
			      char **response_str, long *response_code,
			      http_request_method_t request_method,
			      bool verify_cert)
{
	CURL *c = NULL;
	CURLcode res;
	struct http_response chunk;
	int rc = SLURM_SUCCESS;

	DEF_TIMERS;
	START_TIMER;

	if ((c = curl_easy_init()) == NULL) {
		error("%s: curl_easy_init: %m", __func__);
		rc = SLURM_ERROR;
		goto cleanup_easy_init;
	}

	chunk.message = xmalloc(1);
	chunk.size = 0;

	if (headers)
		CURL_SETOPT(c, CURLOPT_HTTPHEADER, headers);
	if (password)
		CURL_SETOPT(c, CURLOPT_PASSWORD, password);
	if (username)
		CURL_SETOPT(c, CURLOPT_USERNAME, username);
	CURL_SETOPT(c, CURLOPT_READFUNCTION, _read_function);
	CURL_SETOPT(c, CURLOPT_WRITEFUNCTION, _write_callback);
	CURL_SETOPT(c, CURLOPT_WRITEDATA, (void *) &chunk);
	CURL_SETOPT(c, CURLOPT_TIMEOUT, timeout);
	CURL_SETOPT(c, CURLOPT_URL, url);

	if (!verify_cert) {
		/* These are needed to work with self-signed certificates */
		CURL_SETOPT(c, CURLOPT_SSL_VERIFYPEER, 0);
		CURL_SETOPT(c, CURLOPT_SSL_VERIFYHOST, 0);
	}

#if CURL_TRACE
	CURL_SETOPT(c, CURLOPT_DEBUGFUNCTION, _libcurl_trace);
	CURL_SETOPT(c, CURLOPT_VERBOSE, 1L);
#endif

	switch (request_method) {
	case HTTP_REQUEST_POST:
		CURL_SETOPT(c, CURLOPT_CUSTOMREQUEST, NULL);
		CURL_SETOPT(c, CURLOPT_POST, 1);
		CURL_SETOPT(c, CURLOPT_POSTFIELDS, data);
		CURL_SETOPT(c, CURLOPT_HTTPGET, 0);
		break;
	case HTTP_REQUEST_PATCH:
		CURL_SETOPT(c, CURLOPT_CUSTOMREQUEST, "PATCH");
		CURL_SETOPT(c, CURLOPT_POST, 1);
		CURL_SETOPT(c, CURLOPT_POSTFIELDS, data);
		CURL_SETOPT(c, CURLOPT_HTTPGET, 0);
		break;
	case HTTP_REQUEST_GET:
		CURL_SETOPT(c, CURLOPT_CUSTOMREQUEST, NULL);
		CURL_SETOPT(c, CURLOPT_POST, 0);
		CURL_SETOPT(c, CURLOPT_POSTFIELDS, NULL);
		CURL_SETOPT(c, CURLOPT_HTTPGET, 1);
		break;
	case HTTP_REQUEST_DELETE:
		CURL_SETOPT(c, CURLOPT_CUSTOMREQUEST, "DELETE");
		CURL_SETOPT(c, CURLOPT_POST, 0);
		CURL_SETOPT(c, CURLOPT_POSTFIELDS, NULL);
		CURL_SETOPT(c, CURLOPT_HTTPGET, 0);
		break;
	default:
		error("%s: Unable to process this request: %s", __func__,
		      get_http_method_string(request_method));
	}

	if ((res = curl_easy_perform(c)) != CURLE_OK) {
		error("%s: curl_easy_perform failed to send data to %s. Reason: %s",
		      __func__, url, curl_easy_strerror(res));
		rc = SLURM_ERROR;
		goto err;
	}

	if ((res = curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE,
				     response_code)) != CURLE_OK) {
		error("%s: curl_easy_getinfo response code failed: %s",
		      __func__, curl_easy_strerror(res));
		rc = SLURM_ERROR;
		goto err;
	}

	*response_str = chunk.message;
	chunk.message = NULL;

err:
	xfree(chunk.message);
cleanup_easy_init:
	curl_easy_cleanup(c);

	END_TIMER;
	log_flag(PROFILE, "%s: took %s to send data", __func__, TIME_STR);

	return rc;
}

extern int slurm_curl_init(void)
{
	if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
		error("%s: curl_global_init: %m", __func__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int slurm_curl_fini(void)
{
	curl_global_cleanup();

	return SLURM_SUCCESS;
}
