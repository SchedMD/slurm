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

#include <stdarg.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/ref.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"

#include "api.h"

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
const char plugin_name[] = "Slurm OpenAPI slurmctld";
const char plugin_type[] = "openapi/slurmctld";
const uint32_t plugin_id = 110;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* all active parsers */
static data_parser_t **parsers = NULL;
static pthread_mutex_t parsers_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	data_parser_t *parser;
	char *path;
} handler_t;

/* map parser to every bound URL */
static handler_t *handlers = NULL;
static int handler_count = 0;

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

	resp_error(ctxt, error_code, source, "%s", str);

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

	resp_warn(ctxt, source, "%s", str);

	xfree(str);
}

extern ctxt_t *init_connection(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = xmalloc(sizeof(*ctxt));

	ctxt->magic = MAGIC_CTXT;
	ctxt->id = context_id;
	ctxt->db_conn = openapi_get_db_conn(auth);
	ctxt->method = method;
	ctxt->parameters = parameters;
	ctxt->query = query;
	ctxt->resp = resp;
	ctxt->parent_path = data_set_list(data_new());

	ctxt->errors = list_create(free_openapi_resp_error);
	ctxt->warnings = list_create(free_openapi_resp_warning);

	if (!ctxt->db_conn)
		resp_error(ctxt, ESLURM_DB_CONNECTION, __func__,
			   "openapi_get_db_conn() failed to open slurmdb connection");

	if ((tag >= handler_count) || (tag < 0))
		fatal("%s: invalid tag %d", __func__, tag);

	debug("%s: [%s] %s %s using %s",
	      __func__, context_id, get_http_method_string(method),
	      handlers[tag].path,
	      data_parser_get_plugin(handlers[tag].parser));

	if ((ctxt->parser = data_parser_g_new(_on_error, _on_error, _on_error,
		ctxt, _on_warn, _on_warn, _on_warn, ctxt,
		data_parser_get_plugin(handlers[tag].parser), NULL, true))) {
	} else if (!ctxt->rc) {
		ctxt->rc = SLURM_PLUGIN_NAME_INVALID;
	}

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
	data_t *errors, *warnings, *meta;
	int rc;
	const openapi_resp_meta_t query_meta = {
		.plugin = {
			/*
			 * Compiler wants field to be const even though entire
			 * object is const requiring casting.
			 */
			.type = (char *) plugin_type,
			.name = (char *) plugin_name,
			.data_parser = (ctxt->parser ?
				(char *) data_parser_get_plugin(ctxt->parser) :
				NULL),
		},
		.client = {
			.source = (char *) ctxt->id,
		},
		.slurm = {
			.version = {
				.major = SLURM_MAJOR,
				.micro = SLURM_MICRO,
				.minor = SLURM_MINOR,
			},
			.release = SLURM_VERSION_STRING,
		}
	};

	if (data_get_type(ctxt->resp) == DATA_TYPE_NULL)
		data_set_dict(ctxt->resp);

	/* need to populate meta, errors and warnings */

	errors = data_key_set(ctxt->resp,
			      OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME);
	warnings = data_key_set(ctxt->resp,
				OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME);
	meta = data_key_set(ctxt->resp, OPENAPI_RESP_STRUCT_META_FIELD_NAME);

	/* none of the fields should be populated */
	xassert((data_get_type(errors) == DATA_TYPE_NULL));
	xassert((data_get_type(warnings) == DATA_TYPE_NULL));
	xassert((data_get_type(meta) == DATA_TYPE_NULL));

	{
		/* cast to remove const */
		void *ptr = (void *) &query_meta;
		DATA_DUMP(ctxt->parser, OPENAPI_META_PTR, ptr, meta);
	}
	if ((rc = DATA_DUMP(ctxt->parser, OPENAPI_ERRORS, ctxt->errors, errors))) {
		/* data_parser doesn't support OPENAPI_ERRORS parser */
		data_t *e =
			data_set_dict(data_list_append(data_set_list(errors)));
		data_set_string(data_key_set(e, "description"),
				"Requested data_parser plugin does not support OpenAPI plugin");
		data_set_int(data_key_set(e, "error_number"),
			     ESLURM_NOT_SUPPORTED);
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(ESLURM_NOT_SUPPORTED));
	}
	DATA_DUMP(ctxt->parser, OPENAPI_WARNINGS, ctxt->warnings, warnings);

	xassert(ctxt->magic == MAGIC_CTXT);

	rc = ctxt->rc;

	FREE_NULL_LIST(ctxt->errors);
	FREE_NULL_LIST(ctxt->warnings);
	FREE_NULL_DATA_PARSER(ctxt->parser);
	FREE_NULL_DATA(ctxt->parent_path);
	ctxt->magic = ~MAGIC_CTXT;
	xfree(ctxt);

	return rc;
}

__attribute__((format(printf, 4, 5)))
extern int resp_error(ctxt_t *ctxt, int error_code, const char *source,
		      const char *why, ...)
{
	openapi_resp_error_t *e;

	xassert(ctxt->magic == MAGIC_CTXT);
	xassert(ctxt->errors);

	if (!ctxt->errors)
		return error_code;

	e = xmalloc(sizeof(*e));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		error("%s: [%s] parser=%s rc[%d]=%s -> %s",
		      (source ? source : __func__), ctxt->id,
		      data_parser_get_plugin(ctxt->parser), error_code,
		      slurm_strerror(error_code), str);

		e->description = str;
	}

	if (error_code) {
		e->num = error_code;

		if (!ctxt->rc)
			ctxt->rc = error_code;
	}

	if (source)
		e->source = xstrdup(source);

	list_append(ctxt->errors, e);

	return error_code;
}

__attribute__((format(printf, 3, 4)))
extern void resp_warn(ctxt_t *ctxt, const char *source, const char *why, ...)
{
	openapi_resp_warning_t *w;

	xassert(ctxt->magic == MAGIC_CTXT);
	xassert(ctxt->warnings);

	if (!ctxt->warnings)
		return;

	w = xmalloc(sizeof(*w));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		debug("%s: [%s] parser=%s WARNING: %s",
		      (source ? source : __func__), ctxt->id,
		      data_parser_get_plugin(ctxt->parser), str);

		w->description = str;
	}

	if (source)
		w->source = xstrdup(source);
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

extern int get_date_param(data_t *query, const char *param, time_t *time)
{
	data_t *data_update_time;
	if ((data_update_time = data_key_get(query, param))) {
		if (data_convert_type(data_update_time, DATA_TYPE_INT_64) ==
		    DATA_TYPE_INT_64)
			*time = data_get_int(data_update_time);
		else
			return ESLURM_REST_INVALID_QUERY;
	}
	return SLURM_SUCCESS;
}

static void _init_parsers()
{
	slurm_mutex_lock(&parsers_mutex);

	if (!parsers)
		parsers = data_parser_g_new_array(NULL, NULL, NULL, NULL, NULL,
						  NULL, NULL, NULL, NULL, NULL,
						  false);

	slurm_mutex_unlock(&parsers_mutex);
}

static data_for_each_cmd_t _foreach_remove_template(const char *key,
						    data_t *data, void *arg)
{
	/* remove every path with {data_parser} */

	if (!xstrstr(key, OPENAPI_DATA_PARSER_PARAM))
		return DATA_FOR_EACH_CONT;
	else
		return DATA_FOR_EACH_DELETE;
}

extern data_t *slurm_openapi_p_get_specification(openapi_spec_flags_t *flags)
{
	int i = 0;
	data_t *spec = NULL, *paths;

	*flags |= OAS_FLAG_SET_OPID;

	static_ref_json_to_data_t(spec, openapi_json);

	_init_parsers();

	slurm_mutex_lock(&parsers_mutex);
	for (; parsers[i]; i++) {
		int rc;

		if ((rc = data_parser_g_specify(parsers[i], spec))) {
			if (rc == ESLURM_NOT_SUPPORTED)
				FREE_NULL_DATA_PARSER(parsers[i]);
			else
				fatal("%s: parser specification failed: %s",
				      __func__, slurm_strerror(rc));
		}
	}

	/* condense parser array after removals */
	for (int j = 0; j < i; j++) {
		if (!parsers[j] && (j != (i - 1))) {
			i--;
			SWAP(parsers[j], parsers[i]);
		}
	}

	slurm_mutex_unlock(&parsers_mutex);

	/* scrub the paths with {data_parser} */
	paths = data_resolve_dict_path(spec, OPENAPI_PATHS_PATH);
	(void) data_dict_for_each(paths, _foreach_remove_template, NULL);

	return spec;
}

extern void bind_handler(const char *str_path, openapi_handler_t callback)
{
	int handler;

	slurm_mutex_lock(&parsers_mutex);
	for (int i = 0; parsers[i]; i++) {
		char *path = xstrdup(str_path);

		handler = handler_count;
		if (!handlers) {
			xassert(handler_count == 0);
			handler_count = 1;
			handlers = xcalloc(handler_count, sizeof(*handlers));
		} else {
			handler_count++;
			xrecalloc(handlers, handler_count, sizeof(*handlers));
		}

		xstrsubstitute(path, OPENAPI_DATA_PARSER_PARAM,
			       data_parser_get_plugin_version(parsers[i]));

		(void) bind_operation_handler(path, callback, handler);

		handlers[handler].parser = parsers[i];
		handlers[handler].path = path;
	}
	slurm_mutex_unlock(&parsers_mutex);
}

extern void slurm_openapi_p_init(void)
{
	_init_parsers();

	init_op_diag();
	init_op_jobs();
	init_op_nodes();
	init_op_partitions();
	init_op_reservations();
}

extern void slurm_openapi_p_fini(void)
{
	destroy_op_diag();
	destroy_op_jobs();
	destroy_op_nodes();
	destroy_op_partitions();
	destroy_op_reservations();

	slurm_mutex_lock(&parsers_mutex);
	xfree(handlers);
	handler_count = 0;
	FREE_NULL_DATA_PARSER_ARRAY(parsers, true);
	slurm_mutex_unlock(&parsers_mutex);
}
