/*****************************************************************************\
 *  api.c - Slurm REST API openapi operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <math.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/dbv0.0.38/api.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Slurm OpenAPI DB v0.0.38";
const char plugin_type[] = "openapi/dbv0.0.38";
const uint32_t plugin_id = 102;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

decl_static_data(openapi_json);

extern data_t *populate_response_format(data_t *resp)
{
	data_t *plugin, *slurm, *slurmv, *meta;

	if (data_get_type(resp) != DATA_TYPE_NULL) {
		xassert(data_get_type(resp) == DATA_TYPE_DICT);
		return data_key_get(resp, "errors");
	}

	data_set_dict(resp);

	meta = data_set_dict(data_key_set(resp, "meta"));
	plugin = data_set_dict(data_key_set(meta, "plugin"));
	slurm = data_set_dict(data_key_set(meta, "Slurm"));
	slurmv = data_set_dict(data_key_set(slurm, "version"));

	data_set_string(data_key_set(slurm, "release"), SLURM_VERSION_STRING);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "major"),
						 SLURM_MAJOR),
				 DATA_TYPE_INT_64);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "micro"),
						 SLURM_MICRO),
				 DATA_TYPE_INT_64);
	(void) data_convert_type(data_set_string(data_key_set(slurmv, "minor"),
						 SLURM_MINOR),
				 DATA_TYPE_INT_64);

	data_set_string(data_key_set(plugin, "type"), plugin_type);
	data_set_string(data_key_set(plugin, "name"), plugin_name);

	return data_set_list(data_key_set(resp, "errors"));
}

extern int resp_error(data_t *errors, int error_code, const char *why,
		      const char *source)
{
	data_t *e = data_set_dict(data_list_append(errors));

	if (why)
		data_set_string(data_key_set(e, "description"), why);

	if (error_code) {
		data_set_int(data_key_set(e, "error_number"), error_code);
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(error_code));
	}

	if (source)
		data_set_string(data_key_set(e, "source"), source);

	return error_code;
}

extern int db_query_list_funcname(data_t *errors, rest_auth_context_t *auth,
				  List *list, db_list_query_func_t func,
				  void *cond, const char *func_name)
{
	List l;
	void *db_conn;

	xassert(!*list);
	xassert(auth);
	xassert(errors);

	errno = 0;
	if (!(db_conn = openapi_get_db_conn(auth))) {
		return resp_error(errors, ESLURM_DB_CONNECTION,
				  "Failed connecting to slurmdbd", func_name);
	}

	l = func(db_conn, cond);

	if (errno) {
		FREE_NULL_LIST(l);
		return resp_error(errors, errno, NULL, func_name);
	} else if (!l) {
		return resp_error(errors, ESLURM_REST_INVALID_QUERY,
				  "Unknown error with query", func_name);
	} else if (!list_count(l)) {
		FREE_NULL_LIST(l);
		return resp_error(errors, ESLURM_REST_EMPTY_RESULT,
				  "Nothing found", func_name);
	}

	*list = l;
	return SLURM_SUCCESS;
}

extern int db_query_rc_funcname(data_t *errors,
				rest_auth_context_t *auth, List list,
				db_rc_query_func_t func,
				const char *func_name)
{
	int rc;
	void *db_conn;

	if (!(db_conn = openapi_get_db_conn(auth))) {
		return resp_error(errors, ESLURM_DB_CONNECTION,
				  "Failed connecting to slurmdbd", func_name);
	}

	rc = func(db_conn, list);

	if (rc)
		return resp_error(errors, rc, NULL, func_name);

	return rc;
}

extern int db_modify_rc_funcname(data_t *errors, rest_auth_context_t *auth,
				 void *cond, void *obj,
				 db_rc_modify_func_t func,
				 const char *func_name)
{
	List changed;
	int rc = SLURM_SUCCESS;
	void *db_conn;

	if (!(db_conn = openapi_get_db_conn(auth))) {
		return resp_error(errors, ESLURM_DB_CONNECTION,
				  "Failed connecting to slurmdbd", func_name);
	}

	errno = 0;
	if (!(changed = func(db_conn, cond, obj))) {
		if (errno)
			rc = errno;
		else
			rc = SLURM_ERROR;

		return resp_error(errors, rc, NULL, func_name);
	}

	FREE_NULL_LIST(changed);

	return rc;
}

extern int db_query_commit(data_t *errors, rest_auth_context_t *auth)
{
	int rc;
	void *db_conn;

	if (!(db_conn = openapi_get_db_conn(auth))) {
		return resp_error(errors, ESLURM_DB_CONNECTION,
				  "Failed connecting to slurmdbd",
				  __func__);
	}
	rc = slurmdb_connection_commit(db_conn, true);

	if (rc)
		return resp_error(errors, rc, NULL,
				  "slurmdb_connection_commit");

	return rc;
}

extern char *get_str_param(const char *path, data_t *errors, data_t *parameters)
{
	char *str = NULL;
	data_t *dbuf;

	if (!parameters) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "No parameters provided", "HTTP parameters");
	} else if (!(dbuf = data_key_get(parameters, path))) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "Parameter not found", path);
	} else if (data_convert_type(dbuf, DATA_TYPE_STRING) !=
		   DATA_TYPE_STRING) {
		resp_error(errors, ESLURM_DATA_CONV_FAILED,
			   "Parameter incorrect format", path);
	} else if (!(str = data_get_string(dbuf)) || !str[0]) {
		resp_error(errors, ESLURM_REST_EMPTY_RESULT, "Parameter empty",
			   path);
		str = NULL;
	}

	return str;
}

extern data_t * get_query_key_list(const char *path, data_t *errors,
				   data_t *query)
{
	data_t *dst = NULL;

	if (!query)
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "No query provided", "HTTP query");
	else if (!(dst = data_key_get(query, path)))
		resp_error(errors, ESLURM_DATA_PATH_NOT_FOUND,
			   "Query parameter not found", path);
	else if (data_get_type(dst) != DATA_TYPE_LIST) {
		resp_error(errors, ESLURM_DATA_PATH_NOT_FOUND,
			   "Query parameter must be a list", path);
		dst = NULL;
	}

	return dst;
}

const data_t *slurm_openapi_p_get_specification(openapi_spec_flags_t *flags)
{
	data_t *spec = NULL;

	*flags |= OAS_FLAG_MANGLE_OPID;

	static_ref_json_to_data_t(spec, openapi_json);

	return spec;
}

extern void slurm_openapi_p_init(void)
{
	/* Check to see if we are running a supported accounting plugin */
	if (!slurm_with_slurmdbd()) {
		fatal("%s: slurm not configured with slurmdbd", __func__);
	}

	init_op_accounts();
	init_op_associations();
	init_op_config();
	init_op_cluster();
	init_op_diag();
	init_op_job();
	init_op_qos();
	init_op_tres();
	init_op_users();
	init_op_wckeys();
}

extern void slurm_openapi_p_fini(void)
{
	destroy_op_accounts();
	destroy_op_associations();
	destroy_op_cluster();
	destroy_op_config();
	destroy_op_diag();
	destroy_op_job();
	destroy_op_qos();
	destroy_op_tres();
	destroy_op_users();
	destroy_op_wckeys();
}
