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

#include "config.h"

#include <stdarg.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
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

const openapi_resp_meta_t plugin_meta = {
	.plugin = {
		.type = (char *) plugin_type,
		.name = (char *) plugin_name,
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
	"slurm",
	NULL
};

#define OP_FLAGS (OP_BIND_DATA_PARSER | OP_BIND_OPENAPI_RESP_FMT | \
		  OP_BIND_NO_SLURMDBD)

const openapi_path_binding_t openapi_paths[] = {
	{
		.path = "/slurm/{data_parser}/shares",
		.callback = op_handler_shares,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get fairshare info",
				.response = {
					.type = DATA_PARSER_OPENAPI_SHARES_RESP,
					.description = "shares information",
				},
				.query = DATA_PARSER_SHARES_REQ_MSG,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/reconfigure/",
		.callback = op_handler_reconfigure,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "request slurmctld reconfigure",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "reconfigure request result",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/diag/",
		.callback = op_handler_diag,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get diagnostics",
				.response = {
					.type = DATA_PARSER_OPENAPI_DIAG_RESP,
					.description = "diagnostic results",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/ping/",
		.callback = op_handler_ping,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "ping test",
				.response = {
					.type = DATA_PARSER_OPENAPI_PING_ARRAY_RESP,
					.description = "results of ping test",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/licenses/",
		.callback = op_handler_licenses,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get all Slurm tracked license info",
				.response = {
					.type = DATA_PARSER_OPENAPI_LICENSES_RESP,
					.description = "results of get all licenses",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/job/submit",
		.callback = op_handler_submit_job,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "submit new job",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_SUBMIT_RESPONSE,
					.description = "job submission response",
				},
				.body = {
					.type = DATA_PARSER_JOB_SUBMIT_REQ,
					.description = "Job description",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/job/allocate",
		.callback = op_handler_alloc_job,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "submit new job allocation without any steps that must be signaled to stop",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_ALLOC_RESP,
					.description = "job allocation response",
				},
				.body = {
					.type = DATA_PARSER_JOB_ALLOC_REQ,
					.description = "Job allocation description",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/jobs/",
		.callback = op_handler_jobs,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get list of jobs",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_INFO_RESP,
					.description = "job(s) information",
				},
				.query = DATA_PARSER_OPENAPI_JOB_INFO_QUERY,
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "send signal to list of jobs",
				.response = {
					.type = DATA_PARSER_OPENAPI_KILL_JOBS_RESP,
					.description = "description of jobs to signal",
				},
				.body = {
					.type = DATA_PARSER_KILL_JOBS_MSG,
					.description = "Signal or cancel jobs",
				},
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/jobs/state/",
		.callback = op_handler_job_states,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get list of job states",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_INFO_RESP,
					.description = "job(s) state information",
				},
				.query = DATA_PARSER_OPENAPI_JOB_STATE_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/job/{job_id}",
		.callback = op_handler_job,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get job info",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_INFO_RESP,
					.description = "job(s) information",
				},
				.parameters = DATA_PARSER_OPENAPI_JOB_INFO_PARAM,
				.query = DATA_PARSER_OPENAPI_JOB_INFO_QUERY,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "update job",
				.response = {
					.type = DATA_PARSER_OPENAPI_JOB_POST_RESPONSE,
					.description = "job update result",
				},
				.parameters = DATA_PARSER_OPENAPI_JOB_INFO_PARAM,
				.body = {
					.type = DATA_PARSER_JOB_DESC_MSG,
					.description = "Job update description",
				},
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "cancel or signal job",
				.response = {
					.type = DATA_PARSER_OPENAPI_KILL_JOB_RESP,
					.description = "job signal result",
				},
				.parameters = DATA_PARSER_OPENAPI_JOB_INFO_PARAM,
				.query = DATA_PARSER_OPENAPI_JOB_INFO_DELETE_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/nodes/",
		.callback = op_handler_nodes,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get node(s) info",
				.response = {
					.type = DATA_PARSER_OPENAPI_NODES_RESP,
					.description = "node(s) information",
				},
				.query = DATA_PARSER_OPENAPI_NODES_QUERY,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "batch update node(s)",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "batch node update request result",
				},
				.body = {
					.type = DATA_PARSER_UPDATE_NODE_MSG,
					.description = "Nodelist update description",
				}
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/node/{node_name}",
		.callback = op_handler_node,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get node info",
				.response = {
					.type = DATA_PARSER_OPENAPI_NODES_RESP,
					.description = "node information",
				},
				.parameters = DATA_PARSER_OPENAPI_NODE_PARAM,
				.query = DATA_PARSER_OPENAPI_NODES_QUERY,
			},
			{
				.method = HTTP_REQUEST_POST,
				.tags = tags,
				.summary = "update node properties",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "node update request result",
				},
				.parameters = DATA_PARSER_OPENAPI_NODE_PARAM,
				.body = {
					.type = DATA_PARSER_UPDATE_NODE_MSG,
					.description = "Node update description",
				},
			},
			{
				.method = HTTP_REQUEST_DELETE,
				.tags = tags,
				.summary = "delete node",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESP,
					.description = "node delete request result",
				},
				.parameters = DATA_PARSER_OPENAPI_NODE_PARAM,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/partitions/",
		.callback = op_handler_partitions,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get all partition info",
				.response = {
					.type = DATA_PARSER_OPENAPI_PARTITION_RESP,
					.description = "partition information",
				},
				.query = DATA_PARSER_OPENAPI_PARTITIONS_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/partition/{partition_name}",
		.callback = op_handler_partition,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get partition info",
				.response = {
					.type = DATA_PARSER_OPENAPI_PARTITION_RESP,
					.description = "partition information",
				},
				.parameters = DATA_PARSER_OPENAPI_PARTITION_PARAM,
				.query = DATA_PARSER_OPENAPI_PARTITIONS_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/reservations/",
		.callback = op_handler_reservations,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get all reservation info",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESERVATION_RESP,
					.description = "reservation information",
				},
				.query = DATA_PARSER_OPENAPI_RESERVATION_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{
		.path = "/slurm/{data_parser}/reservation/{reservation_name}",
		.callback = op_handler_reservation,
		.methods = (openapi_path_binding_method_t[]) {
			{
				.method = HTTP_REQUEST_GET,
				.tags = tags,
				.summary = "get reservation info",
				.response = {
					.type = DATA_PARSER_OPENAPI_RESERVATION_RESP,
					.description = "reservation information",
				},
				.parameters = DATA_PARSER_OPENAPI_RESERVATION_PARAM,
				.query = DATA_PARSER_OPENAPI_RESERVATION_QUERY,
			},
			{0}
		},
		.flags = OP_FLAGS,
	},
	{0}
};

extern void slurm_openapi_p_init(void)
{
}

extern void slurm_openapi_p_fini(void)
{
}

extern int slurm_openapi_p_get_paths(const openapi_path_binding_t **paths_ptr,
				     const openapi_resp_meta_t **meta_ptr)
{
	*paths_ptr = openapi_paths;
	*meta_ptr = &plugin_meta;
	return SLURM_SUCCESS;
}
