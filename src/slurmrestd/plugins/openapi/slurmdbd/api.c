/*****************************************************************************\
 *  api.c - Slurm REST API openapi operations handlers
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

static const char *tags[] = {
	"slurmdb",
	NULL
};

#define OP_FLAGS                                          \
	(OP_BIND_DATA_PARSER | OP_BIND_OPENAPI_RESP_FMT | \
	 OP_BIND_REQUIRE_SLURMDBD)

const openapi_path_binding_t openapi_paths[] = {
	{
		.path = "/slurmdb/{data_parser}/job/{job_id}",
		.callback = op_handler_job,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get job info",
				.description = "This endpoint may return multiple job entries since job_id is not a unique key - only the tuple (cluster, job_id, start_time) is unique. If the requested job_id is a component of a heterogeneous job all components are returned.",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_JOBS_RESP,
					.description = "Job description",
				},
				.parameters = DATA_PARSER_OPENAPI_SLURMDBD_JOB_PARAM,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/config",
		.callback = op_handler_config,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Dump all configuration information",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_CONFIG_RESP,
					.description = "slurmdbd configuration",
				},
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Load all configuration information",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "slurmdbd configuration",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_CONFIG_RESP,
					.description = "Add or update config",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/tres/",
		.callback = op_handler_tres,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add TRES",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "TRES update result",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_TRES_RESP,
					.description = "TRES descriptions. Only works in developer mode.",
				},
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get TRES info",
				.response = {
					.type = DATA_PARSER_OPENAPI_TRES_RESP,
					.description = "List of TRES",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/qos/{qos}",
		.callback = op_handler_single_qos,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get QOS info",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_QOS_RESP,
					.description = "QOS information",
				},
				.parameters = DATA_PARSER_OPENAPI_SLURMDBD_QOS_PARAM,
				.query = DATA_PARSER_OPENAPI_SLURMDBD_QOS_QUERY,
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete QOS",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_QOS_REMOVED_RESP,
					.description = "results of ping test",
				},
				.parameters = DATA_PARSER_OPENAPI_SLURMDBD_QOS_PARAM,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/qos/",
		.callback = op_handler_multi_qos,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get QOS list",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_QOS_RESP,
					.description = "List of QOS",
				},
				.query = DATA_PARSER_QOS_CONDITION,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add or update QOSs",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "QOS update response",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_QOS_RESP,
					.description = "Description of QOS to add or update",
				},
				.query = DATA_PARSER_QOS_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/associations/",
		.callback = op_handler_associations,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Set associations info",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "status of associations update",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_ASSOCS_RESP,
					.description = "Job description",
				},
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get association list",
				.response = {
					.type = DATA_PARSER_OPENAPI_ASSOCS_RESP,
					.description = "List of associations",
				},
				.query = DATA_PARSER_ASSOC_CONDITION,
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete associations",
				.response = {
					.type = DATA_PARSER_OPENAPI_ASSOCS_REMOVED_RESP,
					.description = "List of associations deleted",
				},
				.query = DATA_PARSER_ASSOC_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/association/",
		.callback = op_handler_association,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get association info",
				.response = {
					.type = DATA_PARSER_OPENAPI_ASSOCS_RESP,
					.description = "List of associations",
				},
				.query = DATA_PARSER_ASSOC_CONDITION,
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete association",
				.response = {
					.type = DATA_PARSER_OPENAPI_ASSOCS_REMOVED_RESP,
					.description = "Status of associations delete request",
				},
				.query = DATA_PARSER_ASSOC_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/instances/",
		.callback = op_handler_instances,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get instance list",
				.response = {
					.type = DATA_PARSER_OPENAPI_INSTANCES_RESP,
					.description = "List of instances",
				},
				.query = DATA_PARSER_INSTANCE_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/instance/",
		.callback = op_handler_instance,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get instance info",
				.response = {
					.type = DATA_PARSER_OPENAPI_INSTANCES_RESP,
					.description = "List of instances",
				},
				.query = DATA_PARSER_INSTANCE_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/user/{name}",
		.callback = op_handler_user,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete user",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "Result of user delete request",
				},
				.parameters = DATA_PARSER_OPENAPI_USER_PARAM,
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get user info",
				.response = {
					.type = DATA_PARSER_OPENAPI_USERS_RESP,
					.description = "List of users",
				},
				.parameters = DATA_PARSER_OPENAPI_USER_PARAM,
				.query = DATA_PARSER_OPENAPI_USER_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/users_association/",
		.callback = op_handler_users_association,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add users with conditional association",
				.response = {
					.type = DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP_STR,
					.description = "Add list of users with conditional association",
				},
				.query = DATA_PARSER_OPENAPI_PARTITIONS_QUERY,
				.body = {
					.type = DATA_PARSER_OPENAPI_USERS_ADD_COND_RESP,
					.description = "Create users with conditional association",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/users/",
		.callback = op_handler_users,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Update users",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "Status of user update request",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_USERS_RESP,
					.description = "add or update user",
				},
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get user list",
				.response = {
					.type = DATA_PARSER_OPENAPI_USERS_RESP,
					.description = "List of users",
				},
				.query = DATA_PARSER_USER_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/cluster/{cluster_name}",
		.callback = op_handler_cluster,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete cluster",
				.response = {
					.type = DATA_PARSER_OPENAPI_CLUSTERS_REMOVED_RESP,
					.description = "Result of delete cluster request",
				},
				.query = DATA_PARSER_CLUSTER_CONDITION,
				.parameters = DATA_PARSER_OPENAPI_CLUSTER_PARAM,
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get cluster info",
				.response = {
					.type = DATA_PARSER_OPENAPI_CLUSTERS_RESP,
					.description = "Cluster information",
				},
				.query = DATA_PARSER_CLUSTER_CONDITION,
				.parameters = DATA_PARSER_OPENAPI_CLUSTER_PARAM,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/clusters/",
		.callback = op_handler_clusters,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get cluster list",
				.response = {
					.type = DATA_PARSER_OPENAPI_CLUSTERS_RESP,
					.description = "List of clusters",
				},
				.query = DATA_PARSER_OPENAPI_RESERVATION_QUERY,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Get cluster list",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "Result of modify clusters request",
				},
				.query = DATA_PARSER_OPENAPI_RESERVATION_QUERY,
				.body = {
					.type = DATA_PARSER_OPENAPI_CLUSTERS_RESP,
					.description = "Cluster add or update descriptions",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/wckey/{id}",
		.callback = op_handler_wckey,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete wckey",
				.response = {
					.type = DATA_PARSER_OPENAPI_WCKEY_REMOVED_RESP,
					.description = "Result of wckey deletion request",
				},
				.parameters = DATA_PARSER_OPENAPI_WCKEY_PARAM,
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get wckey info",
				.response = {
					.type = DATA_PARSER_OPENAPI_WCKEY_RESP,
					.description = "Description of wckey",
				},
				.parameters = DATA_PARSER_OPENAPI_WCKEY_PARAM,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/wckeys/",
		.callback = op_handler_wckeys,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get wckey list",
				.response = {
					.type = DATA_PARSER_OPENAPI_WCKEY_RESP,
					.description = "List of wckeys",
				},
				.query = DATA_PARSER_WCKEY_CONDITION,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add or update wckeys",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "Result of wckey addition or update request",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_WCKEY_RESP,
					.description = "wckeys description",
				},
				.query = DATA_PARSER_WCKEY_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/account/{account_name}",
		.callback = op_handler_account,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "Delete account",
				.response = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_REMOVED_RESP,
					.description = "Status of account deletion request",
				},
				.parameters = DATA_PARSER_OPENAPI_ACCOUNT_PARAM,
			},
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get account info",
				.response = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_RESP,
					.description = "List of accounts",
				},
				.parameters = DATA_PARSER_OPENAPI_ACCOUNT_PARAM,
				.query = DATA_PARSER_OPENAPI_ACCOUNT_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/accounts_association/",
		.callback = op_handler_accounts_association,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add accounts with conditional association",
				.response = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP_STR,
					.description = "Status of account addition request",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_ADD_COND_RESP,
					.description = "Add list of accounts with conditional association",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/accounts/",
		.callback = op_handler_accounts,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get account list",
				.response = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_RESP,
					.description = "List of accounts",
				},
				.query = DATA_PARSER_ACCOUNT_CONDITION,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "Add/update list of accounts",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "Status of account update request",
				},
				.body = {
					.type = DATA_PARSER_OPENAPI_ACCOUNTS_RESP,
					.description = "Description of accounts to update/create",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/jobs/",
		.callback = op_handler_jobs,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get job list",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_JOBS_RESP,
					.description = "List of jobs",
				},
				.query = DATA_PARSER_JOB_CONDITION,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/diag/",
		.callback = op_handler_diag,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "Get slurmdb diagnostics",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_STATS_RESP,
					.description = "Dictionary of statistics",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurmdb/{data_parser}/ping/",
		.callback = op_handler_ping,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "ping test",
				.response = {
					.type = DATA_PARSER_OPENAPI_SLURMDBD_PING_RESP,
					.description = "results of ping test",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{0}
};

extern int db_query_list_funcname(ctxt_t *ctxt, list_t **list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name, const char *caller,
				  bool ignore_empty_result)
{
	list_t *l;
	int rc = SLURM_SUCCESS;

	xassert(!*list);

	if (!ctxt->db_conn)
		return ESLURM_DB_CONNECTION;

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

extern int db_query_rc_funcname(ctxt_t *ctxt, list_t *list,
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
	list_t *changed;
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

extern void slurm_openapi_p_init(void)
{
	/* Check to see if we are running a supported accounting plugin */
	if (!is_spec_generation_only(false) && !slurm_with_slurmdbd()) {
		debug("%s: refusing to load. Slurm not configured with slurmdbd",
		      __func__);
		return;
	}
}

extern void slurm_openapi_p_fini(void)
{
}

extern int slurm_openapi_p_get_paths(const openapi_path_binding_t **paths_ptr,
				     const openapi_resp_meta_t **meta_ptr)
{
	/* Check to see if we are running a supported accounting plugin */
	if (!is_spec_generation_only(false) && !slurm_with_slurmdbd()) {
		debug("%s: refusing to load. Slurm not configured with slurmdbd",
		      __func__);
		return ESLURM_NOT_SUPPORTED;
	}

	*paths_ptr = openapi_paths;
	*meta_ptr = &plugin_meta;
	return SLURM_SUCCESS;
}
