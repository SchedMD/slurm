/*****************************************************************************\
 *  rest.h - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2023 Hewlett Packard Enterprise Development LP
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

#ifndef _SWITCH_REST_H_
#define _SWITCH_REST_H_

#include <stdbool.h>
#include <stdint.h>
#include <curl/curl.h>
#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include "switch_hpe_slingshot.h"

/* HTTP status values */
#define HTTP_OK                200
#define HTTP_NO_CONTENT        204
#define HTTP_LAST_OK           299	/* HTTP success codes are 200-299 */
#define HTTP_REDIRECT          308
#define HTTP_UNAUTHORIZED      401
#define HTTP_FORBIDDEN         403
#define HTTP_NOT_FOUND         404
#define HTTP_SERVICE_UNAVAILABLE 503

/*
 * Values/directories/filenames for jackaloped/fabric manager
 * BASIC/OAUTH authentication
 */
typedef struct {
	/* slingshot_rest_auth_t is defined in switch_hpe_slingshot.h */
	slingshot_rest_auth_t auth_type;
	const char *auth_dir;
	union {
		struct {
			char *user_name;
			char *password;
		} basic;
		struct {
			char *auth_cache;
		} oauth;
	} u;
} slingshot_rest_authdata_t;

typedef struct {
	CURL *handle;            /* CURL connection handle */
	char *data;              /* Response data buffer */
	size_t datalen;          /* Length of the response data */
	size_t datasiz;          /* Size of the data buffer */
	const char *name;        /* Descriptive name for logging */
	char *base_url;          /* The current site URL */
	slingshot_rest_authdata_t auth; /* Authorization method/data */
	unsigned short timeout;  /* Communication timeout */
	unsigned short connect_timeout; /* Connection timeout */
} slingshot_rest_conn_t;

#define SLINGSHOT_AUTH_BASIC_STR  "BASIC" /* fm_auth token */
#define SLINGSHOT_AUTH_OAUTH_STR  "OAUTH" /* fm_auth token */
#define SLINGSHOT_AUTH_OAUTH_CLIENT_ID_FILE     "client-id"
#define SLINGSHOT_AUTH_OAUTH_CLIENT_SECRET_FILE "client-secret"
#define SLINGSHOT_AUTH_OAUTH_ENDPOINT_FILE      "endpoint"
#define SLINGSHOT_FM_AUTH_BASIC_USER "cxi"   /* user name for BASIC auth */
#define SLINGSHOT_FM_AUTH_BASIC_DIR          "/etc/fmsim"
#define SLINGSHOT_FM_AUTH_BASIC_PWD_FILE     "passwd"
#define SLINGSHOT_FM_AUTH_OAUTH_DIR          "/etc/wlm-client-auth"
#define SLINGSHOT_FM_TIMEOUT            10   /* fabric manager REST call tout */
#define SLINGSHOT_FM_CONNECT_TIMEOUT    10   /* fabric manager REST connect " */
#define SLINGSHOT_TOKEN_TIMEOUT         10   /* OAUTH token REST call timeout */
#define SLINGSHOT_TOKEN_CONNECT_TIMEOUT 10   /* OAUTH token REST connect " */

/* global functions */
/* NOTE: all strings are copied to the conn struct */
bool slingshot_rest_connection(slingshot_rest_conn_t *conn,
			       const char *url,
			       slingshot_rest_auth_t auth_type,
			       const char *auth_dir,
			       const char *basic_user,
			       const char *basic_pwdfile,
			       int timeout,
			       int conn_timeout,
			       const char *conn_name);
void slingshot_rest_destroy_connection(slingshot_rest_conn_t *conn);
json_object *slingshot_rest_post(slingshot_rest_conn_t *conn,
				 const char *urlsuffix, json_object *reqjson,
				 long *status);
json_object *slingshot_rest_patch(slingshot_rest_conn_t *conn,
				  const char *urlsuffix, json_object *reqjson,
				  long *status);
json_object *slingshot_rest_get(slingshot_rest_conn_t *conn,
			        const char *urlsuffix, long *status);
bool slingshot_rest_delete(slingshot_rest_conn_t *conn, const char *urlsuffix,
			   long *status);
#endif
