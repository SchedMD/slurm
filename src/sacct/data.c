/*****************************************************************************\
 *  data.c - data functions for sacct
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>.
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

#include "sacct.h"
#include "slurm/slurm.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/openapi.h"
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#define TARGET "/slurmdb/v0.0.38/jobs/"
#define PLUGIN "openapi/dbv0.0.38"
#define MAGIC_AUTH ((void *)0xd2ad8e8f)

openapi_handler_t dump_job = NULL;

extern void *openapi_get_db_conn(void *ctxt)
{
	xassert(ctxt == MAGIC_AUTH);

	/*
	 * There is no additional auth in sacct, so we just pass a magic value
	 * to make sure the correct request is received.
	 */

	return acct_db_conn;
}
extern int bind_operation_handler(const char *str_path,
				  openapi_handler_t callback, int callback_tag)
{
	debug3("%s: binding %s to 0x%"PRIxPTR,
	       __func__, str_path, (uintptr_t) callback);

	if (!xstrcmp(str_path, TARGET))
		dump_job = callback;

	return SLURM_SUCCESS;
}

extern int unbind_operation_handler(openapi_handler_t callback)
{
	/* no-op */
	return SLURM_SUCCESS;
}

static int _list_append(void *x, void *arg)
{
	char *src = x;
	data_t *list = arg;

	data_set_string(data_list_append(list), src);

	return SLURM_SUCCESS;
}

static int _list_step_append(void *x, void *arg)
{
	char str[1024] = {0};
	slurm_selected_step_t *step = x;
	data_t *list = arg;

	slurm_get_selected_step_id(str, sizeof(str), step);
	data_set_string(data_list_append(list), str);

	return SLURM_SUCCESS;
}

extern void dump_data(int argc, char **argv)
{
	openapi_t *oas = NULL;
	data_t *resp = data_new();
	char *out = NULL;
	slurmdb_job_cond_t *job_cond = params.job_cond;
	data_t *query = data_set_dict(data_new());
	char *ctxt;

	if (init_openapi(&oas, PLUGIN, NULL))
		fatal("unable to load openapi plugins");

	ctxt = fd_resolve_path(STDIN_FILENO);

	/*
	 * The job condition must be converted to the format expected by the
	 * plugin. This isn't the most efficent but it is client side.
	 */

	if (job_cond->usage_start) {
		char str[1024] = {0};
		slurm_make_time_str(&job_cond->usage_start, str, sizeof(str));
		data_set_string(data_key_set(query, "start_time"), str);
	}

	if (job_cond->usage_end) {
		char str[1024] = {0};
		slurm_make_time_str(&job_cond->usage_end, str, sizeof(str));
		data_set_string(data_key_set(query, "end_time"), str);
	}

	if (job_cond->used_nodes)
		data_set_string(data_key_set(query, "node"),
				job_cond->used_nodes);

	if (job_cond->step_list) {
		data_t *list = data_set_list(data_key_set(query, "step"));
		list_for_each(job_cond->step_list, _list_step_append, list);
	}

	/* flags */
	if (job_cond->flags & JOBCOND_FLAG_NO_STEP)
		data_set_bool(data_key_set(query, "skip_steps"), true);
	if (job_cond->flags & JOBCOND_FLAG_NO_WAIT)
		data_set_bool(data_key_set(query, "disable_wait_for_result"),
			      true);

	/* integer */
	if (job_cond->cpus_max)
		data_set_int(data_key_set(query, "cpus_max"),
			     job_cond->cpus_max);
	if (job_cond->cpus_min)
		data_set_int(data_key_set(query, "cpus_min"),
			     job_cond->cpus_min);
	if (job_cond->exitcode)
		data_set_int(data_key_set(query, "exit_code"),
			     job_cond->exitcode);
	if (job_cond->nodes_min)
		data_set_int(data_key_set(query, "nodes_min"),
			     job_cond->nodes_min);
	if (job_cond->nodes_max)
		data_set_int(data_key_set(query, "nodes_max"),
			     job_cond->nodes_max);

	/* CSV lists */
	if (job_cond->acct_list) {
		data_t *list = data_set_list(data_key_set(query, "account"));
		list_for_each(job_cond->acct_list, _list_append, list);
	}
	if (job_cond->associd_list) {
		data_t *list = data_set_list(data_key_set(query, "association"));
		list_for_each(job_cond->associd_list, _list_append, list);
	}
	if (job_cond->cluster_list) {
		data_t *list = data_set_list(data_key_set(query, "cluster"));
		list_for_each(job_cond->cluster_list, _list_append, list);
	}
	if (job_cond->constraint_list) {
		data_t *list = data_set_list(
			data_key_set(query, "constraints"));
		list_for_each(job_cond->constraint_list, _list_append, list);
	}
	if (job_cond->format_list) {
		data_t *list = data_set_list(data_key_set(query, "format"));
		list_for_each(job_cond->format_list, _list_append, list);
	}
	if (job_cond->groupid_list) {
		data_t *list = data_set_list(data_key_set(query, "groups"));
		list_for_each(job_cond->groupid_list, _list_append, list);
	}
	if (job_cond->jobname_list) {
		data_t *list = data_set_list(data_key_set(query, "job_name"));
		list_for_each(job_cond->jobname_list, _list_append, list);
	}
	if (job_cond->partition_list) {
		data_t *list = data_set_list(data_key_set(query, "partition"));
		list_for_each(job_cond->partition_list, _list_append, list);
	}
	if (job_cond->qos_list) {
		data_t *list = data_set_list(data_key_set(query, "qos"));
		list_for_each(job_cond->qos_list, _list_append, list);
	}
	if (job_cond->reason_list) {
		data_t *list = data_set_list(data_key_set(query, "reason"));
		list_for_each(job_cond->reason_list, _list_append, list);
	}
	if (job_cond->resv_list) {
		data_t *list = data_set_list(
			data_key_set(query, "reservation"));
		list_for_each(job_cond->resv_list, _list_append, list);
	}
	if (job_cond->state_list) {
		data_t *list = data_set_list(data_key_set(query, "state"));
		list_for_each(job_cond->state_list, _list_append, list);
	}
	if (job_cond->wckey_list) {
		data_t *list = data_set_list(data_key_set(query, "wckey"));
		list_for_each(job_cond->wckey_list, _list_append, list);
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		char *sq = NULL;

		data_g_serialize(&sq, query, MIME_TYPE_JSON,
				 DATA_SER_FLAGS_COMPACT);
		debug("%s: query: %s", __func__, sq);

		xfree(sq);
	}

	dump_job(ctxt, HTTP_REQUEST_GET, NULL, query, 0, resp, MAGIC_AUTH);

	data_g_serialize(&out, resp, params.mimetype, DATA_SER_FLAGS_PRETTY);

	printf("%s", out);

#ifdef MEMORY_LEAK_DEBUG
	xfree(ctxt);
	xfree(out);
	FREE_NULL_DATA(resp);
	FREE_NULL_DATA(query);
	destroy_openapi(oas);
#endif /* MEMORY_LEAK_DEBUG */
}
