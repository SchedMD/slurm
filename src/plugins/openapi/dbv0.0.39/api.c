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

#include <limits.h>

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/openapi.h"

#include "src/plugins/openapi/dbv0.0.39/api.h"

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
const char plugin_name[] = "Slurm OpenAPI DB v0.0.39";
const char plugin_type[] = "openapi/dbv0.0.39";
const uint32_t plugin_id = 102;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* parser to hold open the plugin contexts */
static data_parser_t *global_parser = NULL;

decl_static_data(openapi_json);

static bool _on_error(void *arg, data_parser_type_t type, int error_code,
		      const char *source, const char *why, ...)
{
	va_list ap;
	char *str;
	ctxt_t *ctxt = arg;

	xassert(ctxt->magic == MAGIC_CTXT);

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	resp_error(ctxt, error_code, str, "%s", source);

	return false;
}

static void _on_warn(void *arg, data_parser_type_t type, const char *source,
		     const char *why, ...)
{
	va_list ap;
	char *str;
	ctxt_t *ctxt = arg;

	xassert(ctxt->magic == MAGIC_CTXT);

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	resp_warn(ctxt, str, "%s", source);

	xfree(str);
}

extern ctxt_t *init_connection(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp, void *auth)
{
	data_t *plugin, *slurm, *slurmv, *meta, *errors, *warn, *client;
	ctxt_t *ctxt = xmalloc(sizeof(*ctxt));

	ctxt->magic = MAGIC_CTXT;
	ctxt->id = context_id;
	ctxt->db_conn = openapi_get_db_conn(auth);
	ctxt->method = method;
	ctxt->parameters = parameters;
	ctxt->query = query;
	ctxt->resp = resp;

	if (data_get_type(resp) != DATA_TYPE_DICT)
		data_set_dict(resp);

	meta = data_set_dict(data_key_set(resp, "meta"));
	plugin = data_set_dict(data_key_set(meta, "plugin"));
	client = data_set_dict(data_key_set(meta, "client"));
	slurm = data_set_dict(data_key_set(meta, "Slurm"));
	slurmv = data_set_dict(data_key_set(slurm, "version"));
	errors = data_set_list(data_key_set(resp, "errors"));
	warn = data_set_list(data_key_set(resp, "warnings"));

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
	data_set_string(data_key_set(plugin, "data_parser"), DATA_VERSION);
	data_set_string(data_key_set(client, "source"), context_id);

	ctxt->errors = errors;
	ctxt->warnings = warn;

	if (!ctxt->db_conn)
		resp_error(ctxt, ESLURM_DB_CONNECTION, __func__,
			   "openapi_get_db_conn() failed to open slurmdb connection");

	ctxt->parser = data_parser_g_new(_on_error, _on_error, _on_error, ctxt,
					 _on_warn, _on_warn, _on_warn, ctxt,
					 DATA_PLUGIN, NULL, true);
	if (!ctxt->parser)
		xassert(ctxt->rc);

	if (ctxt->parser && ctxt->db_conn) {
		xassert(!ctxt->rc);
		ctxt->rc = data_parser_g_assign(ctxt->parser,
						DATA_PARSER_ATTR_DBCONN_PTR,
						ctxt->db_conn);
		xassert(!ctxt->rc);
	}

	return ctxt;
}

extern int fini_connection(ctxt_t *ctxt)
{
	int rc;

	xassert(ctxt);
	xassert(ctxt->magic == MAGIC_CTXT);

	rc = ctxt->rc;

	FREE_NULL_DATA_PARSER(ctxt->parser);
	ctxt->magic = ~MAGIC_CTXT;
	xfree(ctxt);

	return rc;
}

__attribute__((format(printf, 4, 5)))
extern int resp_error(ctxt_t *ctxt, int error_code, const char *source,
		      const char *why, ...)
{
	data_t *e;

	xassert(ctxt->magic == MAGIC_CTXT);
	xassert(ctxt->errors);

	if (!ctxt->errors)
		return error_code;

	e = data_set_dict(data_list_append(ctxt->errors));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		error("%s[v0.0.39]:[%s] rc[%d]=%s -> %s",
		      (source ? source : __func__), ctxt->id, error_code,
		      slurm_strerror(error_code), str);

		data_set_string_own(data_key_set(e, "description"), str);
	}

	if (error_code) {
		data_set_int(data_key_set(e, "error_number"), error_code);
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(error_code));

		if (!ctxt->rc)
			ctxt->rc = error_code;
	}

	if (source)
		data_set_string(data_key_set(e, "source"), source);

	return error_code;
}

__attribute__((format(printf, 3, 4)))
extern void resp_warn(ctxt_t *ctxt, const char *source, const char *why, ...)
{
	data_t *w;

	xassert(ctxt->magic == MAGIC_CTXT);
	xassert(ctxt->warnings);

	if (!ctxt->warnings)
		return;

	w = data_set_dict(data_list_append(ctxt->warnings));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		debug("%s[v0.0.39]:[%s] WARNING: %s",
		      (source ? source : __func__), ctxt->id, str);

		data_set_string_own(data_key_set(w, "description"), str);
	}

	if (source)
		data_set_string(data_key_set(w, "source"), source);
}

extern int db_query_list_funcname(ctxt_t *ctxt, List *list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name, const char *caller,
				  bool ignore_empty_result)
{
	List l;
	int rc = SLURM_SUCCESS;

	xassert(ctxt->magic == MAGIC_CTXT);
	xassert(!*list);
	xassert(ctxt->db_conn);

	errno = 0;
	l = func(ctxt->db_conn, cond);

	if (errno) {
		rc = errno;
		FREE_NULL_LIST(l);
	} else if (!l) {
		rc = ESLURM_REST_INVALID_QUERY;
	}

	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		if (ignore_empty_result) {
			resp_warn(ctxt, caller,
				  "%s(0x%" PRIxPTR ") reports nothing changed",
				  func_name, (uintptr_t) ctxt->db_conn);
			rc = SLURM_SUCCESS;
		}
	}

	if (rc) {
		return resp_error(ctxt, rc, caller, "%s(0x%" PRIxPTR ") failed",
				  func_name, (uintptr_t) ctxt->db_conn);
	}

	if (!list_count(l)) {
		FREE_NULL_LIST(l);

		if (!ignore_empty_result) {
			resp_warn(ctxt, caller,
				  "%s(0x%" PRIxPTR ") found nothing",
				  func_name, (uintptr_t) ctxt->db_conn);
		}
	} else {
		*list = l;
	}

	return rc;
}

extern int db_query_rc_funcname(ctxt_t *ctxt, List list,
				db_rc_query_func_t func, const char *func_name,
				const char *caller)
{
	int rc;

	xassert(ctxt->magic == MAGIC_CTXT);

	if ((rc = func(ctxt->db_conn, list)))
		return resp_error(ctxt, rc, caller, "%s(0x%" PRIxPTR ") failed",
				  func_name, (uintptr_t) ctxt->db_conn);

	return rc;
}

extern int db_modify_rc_funcname(ctxt_t *ctxt, void *cond, void *obj,
				 db_rc_modify_func_t func,
				 const char *func_name, const char *caller)
{
	List changed;
	int rc = SLURM_SUCCESS;

	xassert(ctxt->magic == MAGIC_CTXT);

	errno = 0;
	if (!(changed = func(ctxt->db_conn, cond, obj))) {
		if (errno)
			rc = errno;
		else
			rc = SLURM_ERROR;

		return resp_error(ctxt, rc, caller, "%s(0x%" PRIxPTR ") failed",
				  func_name, (uintptr_t) ctxt->db_conn);
	}

	FREE_NULL_LIST(changed);
	return rc;
}

extern void db_query_commit_funcname(ctxt_t *ctxt, const char *caller)
{
	int rc;

	xassert(!ctxt->rc);
	xassert(ctxt->magic == MAGIC_CTXT);

	if ((rc = slurmdb_connection_commit(ctxt->db_conn, true)))
		resp_error(ctxt, rc, caller,
			   "slurmdb_connection_commit(0x%" PRIxPTR ") failed",
			   (uintptr_t) ctxt->db_conn);
}

extern char *get_str_param_funcname(const char *path, ctxt_t *ctxt,
				    const char *caller)
{
	char *str = NULL;
	data_t *dbuf;

	xassert(ctxt->magic == MAGIC_CTXT);

	if (!ctxt->parameters) {
		resp_warn(ctxt, caller, "No parameters provided");
	} else if (!(dbuf = data_key_get(ctxt->parameters, path))) {
		resp_warn(ctxt, caller, "Parameter %s not found", path);
	} else if (data_convert_type(dbuf, DATA_TYPE_STRING) !=
		   DATA_TYPE_STRING) {
		resp_warn(ctxt, caller, "Parameter %s incorrect format %s",
			  path, data_type_to_string(data_get_type(dbuf)));
	} else if (!(str = data_get_string(dbuf)) || !str[0]) {
		resp_warn(ctxt, caller, "Parameter %s empty", path);
		str = NULL;
	}

	return str;
}

/* Case insensitive string match */
static bool _match_case_string(const char *key, data_t *data, void *needle_ptr)
{
	const char *needle = needle_ptr;
	return !xstrcasecmp(key, needle);
}

extern data_t *get_query_key_list_funcname(const char *path, ctxt_t *ctxt,
					   data_t **parent_path,
					   const char *caller)
{
	char *path_str = NULL;
	data_t *dst = NULL;

	/* start parent path data list */
	xassert(parent_path);
	xassert(!*parent_path);
	xassert(ctxt->magic == MAGIC_CTXT);

	*parent_path = data_set_list(data_new());
	openapi_append_rel_path(*parent_path, path);

	if (!ctxt->query) {
		resp_warn(ctxt, caller, "empty HTTP query while looking for %s",
			  openapi_fmt_rel_path_str(&path_str, *parent_path));
		goto cleanup;
	}

	if (data_get_type(ctxt->query) != DATA_TYPE_DICT) {
		resp_warn(ctxt, caller,
			  "expected HTTP query to be a dictionary instead of %s while searching for %s",
			  data_type_to_string(data_get_type(ctxt->query)),
			  openapi_fmt_rel_path_str(&path_str, *parent_path));
		goto cleanup;
	}

	if (!(dst = data_dict_find_first(ctxt->query, _match_case_string,
					 (void *) path))) {
		resp_warn(ctxt, caller, "unable to find %s in HTTP query",
			  openapi_fmt_rel_path_str(&path_str, *parent_path));
		goto cleanup;
	}

	if (data_get_type(dst) != DATA_TYPE_LIST) {
		resp_warn(ctxt, caller, "%s must be a list but found %s",
			  openapi_fmt_rel_path_str(&path_str, *parent_path),
			  data_type_to_string(data_get_type(dst)));
		goto cleanup;
	}

cleanup:
	xfree(path_str);
	return dst;
}

const data_t *slurm_openapi_p_get_specification(openapi_spec_flags_t *flags)
{
	static data_parser_t *parser;
	data_t *spec = NULL;

	*flags |= OAS_FLAG_MANGLE_OPID;

	static_ref_json_to_data_t(spec, openapi_json);

	parser = data_parser_g_new(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				   NULL, DATA_PLUGIN, NULL, false);
	(void) data_parser_g_specify(parser, spec);
	data_parser_g_free(parser, false);

	return spec;
}

extern void slurm_openapi_p_init(void)
{
	/* Check to see if we are running a supported accounting plugin */
	if (!slurm_with_slurmdbd()) {
		fatal("%s: slurm not configured with slurmdbd", __func__);
	}

	xassert(!global_parser);
	global_parser = data_parser_g_new(NULL, NULL, NULL, NULL, NULL, NULL,
					  NULL, NULL, DATA_PLUGIN, NULL, false);

	init_op_accounts();
	init_op_associations();
	init_op_cluster();
	init_op_config();
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

	data_parser_g_free(global_parser, false);
	global_parser = NULL;
}

extern int groupname_to_gid(void *x, void *arg)
{
	char *group = x, *p = NULL;
	List list = arg;
	long num;
	gid_t gid;

	xassert(group);
	xassert(list);

	/* Already a GID? */
	errno = 0;
	num = strtol(group, &p, 10);
	if (!errno && !*p && (group != p) && (num >= 0) && (num < INT_MAX)) {
		group = xstrdup(group);
		list_append(list, group);
		return SLURM_SUCCESS;
	}

	/* Get the underlying GID */
	if (gid_from_string(group, &gid)) {
		error("Group name (%s) is not valid", group);
		return SLURM_ERROR;
	}

	/* Store the GID */
	group = xstrdup_printf("%u", gid);
	list_append(list, group);
	return SLURM_SUCCESS;
}

extern int username_to_uid(void *x, void *arg)
{
	char *user = x, *p = NULL;
	List list = arg;
	long num;
	uid_t uid;

	xassert(user);
	xassert(list);

	/* Already an UID? */
	errno = 0;
	num = strtol(user, &p, 10);
	if (!errno && !*p && (user != p) && (num >= 0) && (num < INT_MAX)) {
		user = xstrdup(user);
		list_append(list, user);
		return SLURM_SUCCESS;
	}

	/* Get the underlying UID */
	if (uid_from_string(user, &uid)) {
		error("User name (%s) is not valid", user);
		return SLURM_ERROR;
	}

	/* Store the UID */
	user = xstrdup_printf("%u", uid);
	list_append(list, user);
	return SLURM_SUCCESS;
}
