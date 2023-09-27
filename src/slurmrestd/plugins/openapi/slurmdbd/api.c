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
const char plugin_name[] = "Slurm OpenAPI slurmdbd";
const char plugin_type[] = "openapi/slurmdbd";
const uint32_t plugin_id = 111;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

decl_static_data(openapi_json);

const openapi_resp_meta_t plugin_meta = {
	.plugin = {
		.type = (char *) plugin_type,
		.name = (char *) plugin_name,
	},
	.client = {
		.uid = SLURM_AUTH_NOBODY,
		.gid = SLURM_AUTH_NOBODY,
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

extern int db_query_list_funcname(ctxt_t *ctxt, List *list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name, const char *caller,
				  bool ignore_empty_result)
{
	List l;
	int rc = SLURM_SUCCESS;

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

	if ((rc = slurmdb_connection_commit(ctxt->db_conn, true)))
		resp_error(ctxt, rc, caller,
			   "slurmdb_connection_commit(0x%" PRIxPTR ") failed",
			   (uintptr_t) ctxt->db_conn);
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
			  data_get_type_string(ctxt->query),
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
			  data_get_type_string(dst));
		goto cleanup;
	}

cleanup:
	xfree(path_str);
	return dst;
}

extern void bind_handler(const char *str_path, openapi_ctxt_handler_t callback,
			 int tag)
{
	bind_operation_ctxt_handler(str_path, callback, tag, &plugin_meta);
}

const data_t *slurm_openapi_p_get_specification(openapi_spec_flags_t *flags)
{
	data_t *spec = NULL;

	*flags |= OAS_FLAG_SET_OPID | OAS_FLAG_SET_DATA_PARSER_SPEC;

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
	init_op_cluster();
	init_op_config();
	init_op_diag();
	init_op_instances();
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
	destroy_op_instances();
	destroy_op_job();
	destroy_op_qos();
	destroy_op_tres();
	destroy_op_users();
	destroy_op_wckeys();
}
