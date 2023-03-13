/*****************************************************************************\
 *  jobs.c - Slurm REST API jobs http operations handlers
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

#include "src/common/env.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.39/api.h"

static int _op_handler_jobs(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);
	time_t update_time = 0; /* default to unix epoch */
	job_info_msg_t *job_info_ptr = NULL;
	int rc;

	debug4("%s: jobs handler called by %s", __func__, ctxt->id);

	if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
		goto cleanup;
	}

	if ((rc = get_date_param(query, "update_time", &update_time))) {
		resp_error(ctxt, rc, __func__,
			   "Unable to parse \"update_time\" field");
		goto cleanup;
	}

	rc = slurm_load_jobs(update_time, &job_info_ptr,
			     SHOW_ALL | SHOW_DETAIL);

	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		resp_warn(ctxt, __func__,
			  "No job changes since update_time=%ld",
			  update_time);
	} else if (rc) {
		resp_error(ctxt, rc, "slurm_load_jobs()",
			   "Unable to query jobs");
		goto cleanup;
	}

	DATA_DUMP(ctxt->parser, JOB_INFO_MSG, *job_info_ptr,
		  data_key_set(resp, "jobs"));

cleanup:
	slurm_free_job_info_msg(job_info_ptr);
	return fini_connection(ctxt);
}

static void _handle_job_get(ctxt_t *ctxt, slurm_selected_step_t *job_id)
{
	int rc = SLURM_SUCCESS;
	job_info_msg_t *job_info_ptr = NULL;
	uint32_t id;

	if (job_id->het_job_offset != NO_VAL)
		id = job_id->step_id.job_id + job_id->het_job_offset;
	else
		id = job_id->step_id.job_id;

	if (job_id->array_task_id != NO_VAL)
		resp_warn(ctxt, __func__, "Job array Ids are not currently supported for job searches. Showing all jobs in array instead.");
	if (job_id->step_id.step_id != NO_VAL)
		resp_warn(ctxt, __func__,
			  "Job steps are not supported for job searches. Showing whole job instead.");

	if ((rc = slurm_load_job(&job_info_ptr, id, SHOW_ALL | SHOW_DETAIL))) {
		char *id = NULL;

		fmt_job_id_string(job_id, &id);
		resp_error(ctxt, rc, __func__, "Unable to query JobId=%s",
			   id);

		xfree(id);
	} else {
		DATA_DUMP(ctxt->parser, JOB_INFO_MSG, *job_info_ptr,
			  data_key_set(ctxt->resp, "jobs"));
	}

	slurm_free_job_info_msg(job_info_ptr);
}

static void _handle_job_delete(ctxt_t *ctxt, slurm_selected_step_t *job_id)
{
	uint16_t signal = 0;
	data_t *dsignal = data_key_get(ctxt->query, "signal");

	if (!dsignal)
		signal = SIGKILL;
	else if (DATA_PARSE(ctxt->parser, SIGNAL, signal, dsignal,
			    ctxt->parent_path))
		return;

	if (slurm_kill_job(job_id->step_id.job_id, signal, KILL_FULL_JOB)) {
		/* Already signaled jobs are considered a success here */
		if (errno == ESLURM_ALREADY_DONE) {
			resp_warn(ctxt, __func__,
				  "Job was already sent signal %s",
				  strsignal(signal));
			return;
		}

		resp_error(ctxt, errno, "slurm_kill_job2()",
			   "unable to send signal %s to JobId=%u",
			   strsignal(signal), job_id->step_id.job_id);
	}
}

static void _job_post_update(ctxt_t *ctxt, data_t *djob, const char *script,
			     slurm_selected_step_t *job_id)
{
	job_array_resp_msg_t *resp = NULL;
	job_desc_msg_t *job = xmalloc(sizeof(*job));
	data_t *results = data_key_set(ctxt->resp, "results");

	slurm_init_job_desc_msg(job);

	data_set_string(data_list_append(ctxt->parent_path), "job");

	if (DATA_PARSE(ctxt->parser, JOB_DESC_MSG, *job, djob,
		       ctxt->parent_path))
		goto cleanup;

	if (script) {
		xfree(job->script);
		job->script = xstrdup(script);
	}

	if (job_id->step_id.job_id != NO_VAL)
		job->job_id = job_id->step_id.job_id;

	if (job_id->het_job_offset != NO_VAL)
		job->het_job_offset = job_id->het_job_offset;

	if (slurm_update_job2(job, &resp)) {
		resp_error(ctxt, errno, "slurm_update_job2()",
			   "Job update requested failed");
		goto cleanup;
	}

	DATA_DUMP(ctxt->parser, JOB_ARRAY_RESPONSE_MSG_PTR, resp, results);

	if (resp && resp->job_array_count) {
		/*
		 * Success may not give a resp ptr.
		 * TODO: backwards compatibility output.
		 */
		DATA_DUMP(ctxt->parser, STRING, resp->job_array_id[0],
			  data_key_set(ctxt->resp, "job_id"));
		/* msg does not provide the step_id cleanly */
		data_key_set(ctxt->resp, "step_id");
		/* msg not provided for updates */
		data_key_set(ctxt->resp, "job_submit_user_msg");
	}

cleanup:
	slurm_free_job_array_resp(resp);
	slurm_free_job_desc_msg(job);
}

static void _job_submit_rc(ctxt_t *ctxt, submit_response_msg_t *resp,
			   const char *src)
{
	int rc = resp->error_code;

	if (!rc)
		return;

	if (rc == ESLURM_JOB_HELD) {
		/* Job submitted with held state is not an error */
		resp_warn(ctxt, "slurm_submit_batch_job()",
			"%s", slurm_strerror(rc));
		return;
	}

	resp_error(ctxt, rc, src, NULL);
}

static void _job_post_submit(ctxt_t *ctxt, data_t *djob, const char *script)
{
	submit_response_msg_t *resp = NULL;
	job_desc_msg_t *job = xmalloc(sizeof(*job));

	slurm_init_job_desc_msg(job);

	data_set_string(data_list_append(ctxt->parent_path), "job");

	if (DATA_PARSE(ctxt->parser, JOB_DESC_MSG, *job, djob,
		       ctxt->parent_path))
		goto cleanup;

	if (script) {
		xfree(job->script);
		job->script = xstrdup(script);
	}

	if (slurm_submit_batch_job(job, &resp)) {
		resp_error(ctxt, errno, "slurm_submit_batch_job()",
			   "Batch job submission failed");
		goto cleanup;
	}

	debug3("%s:[%s] job submitted -> job_id:%d step_id:%d rc:%d message:%s",
	       __func__, ctxt->id, resp->job_id, resp->step_id,
	       resp->error_code, resp->job_submit_user_msg);

	DATA_DUMP(ctxt->parser, JOB_SUBMIT_RESPONSE_MSG, *resp,
		  data_key_set(ctxt->resp, "result"));

	/* TODO: backwards compatibility output */
	DATA_DUMP(ctxt->parser, UINT32, resp->job_id,
		  data_key_set(ctxt->resp, "job_id"));
	DATA_DUMP(ctxt->parser, STEP_ID, resp->step_id,
		  data_key_set(ctxt->resp, "step_id"));
	DATA_DUMP(ctxt->parser, STRING, resp->job_submit_user_msg,
		  data_key_set(ctxt->resp, "job_submit_user_msg"));

	_job_submit_rc(ctxt, resp, "slurm_submit_batch_job()");

cleanup:
	slurm_free_submit_response_response_msg(resp);
	slurm_free_job_desc_msg(job);
}

static void _job_post_het_submit(ctxt_t *ctxt, data_t *djobs,
				 const char *script)
{
	submit_response_msg_t *resp = NULL;
	list_t *jobs = NULL;

	data_set_string(data_list_append(ctxt->parent_path), "jobs");

	if (DATA_PARSE(ctxt->parser, JOB_DESC_MSG_LIST, jobs, djobs,
		       ctxt->parent_path))
		goto cleanup;

	if (!jobs || !list_count(jobs)) {
		resp_error(ctxt, errno, __func__,
			   "Refusing HET job submission without any components");
		goto cleanup;
	}

	if (list_count(jobs) > MAX_HET_JOB_COMPONENTS) {
		resp_error(ctxt, errno, __func__,
			   "Refusing HET job submission too many components: %d > %u",
			   list_count(jobs), MAX_HET_JOB_COMPONENTS);
		goto cleanup;
	}

	if (script) {
		job_desc_msg_t *j = list_peek(jobs);

		if (!j->script)
			j->script = xstrdup(script);
	}

	if (slurm_submit_batch_het_job(jobs, &resp)) {
		resp_error(ctxt, errno, "slurm_submit_batch_het_job()",
			   "HET job submission failed");
		goto cleanup;
	}

	debug3("%s:[%s] HET job submitted -> job_id:%d step_id:%d rc:%d message:%s",
	       __func__, ctxt->id, resp->job_id, resp->step_id,
	       resp->error_code, resp->job_submit_user_msg);

	DATA_DUMP(ctxt->parser, JOB_SUBMIT_RESPONSE_MSG, *resp,
		  data_key_set(ctxt->resp, "result"));

	/* TODO: backwards compatibility output */
	DATA_DUMP(ctxt->parser, UINT32, resp->job_id,
		  data_key_set(ctxt->resp, "job_id"));
	DATA_DUMP(ctxt->parser, UINT32, resp->step_id,
		  data_key_set(ctxt->resp, "step_id"));
	DATA_DUMP(ctxt->parser, STRING, resp->job_submit_user_msg,
		  data_key_set(ctxt->resp, "job_submit_user_msg"));

	_job_submit_rc(ctxt, resp, "slurm_submit_batch_het_job()");

cleanup:
	slurm_free_submit_response_response_msg(resp);
	FREE_NULL_LIST(jobs);
}

static void _job_post(ctxt_t *ctxt, slurm_selected_step_t *job_id)
{
	data_t *djobs, *djob, *dscript;
	const char *script = NULL;

	if ((slurm_conf.debug_flags & DEBUG_FLAG_NET_RAW) && ctxt->query) {
		char *buffer = NULL;

		serialize_g_data_to_string(&buffer, NULL, ctxt->query,
					   MIME_TYPE_JSON, SER_FLAGS_COMPACT);

		log_flag(NET_RAW, "%s:[%s] job POST: %s",
		       __func__, ctxt->id, buffer);

		xfree(buffer);
	}

	if (!ctxt->query) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "unexpected empty query for job");
		return;
	}
	if (data_get_type(ctxt->query) != DATA_TYPE_DICT) {
		resp_error(ctxt, ESLURM_DATA_EXPECTED_DICT, __func__,
			   "Job query must be a dictionary");
		return;
	}

	/* TODO: script is backwards compatibility only */
	dscript = data_key_get(ctxt->query, "script");
	djob = data_key_get(ctxt->query, "job");
	djobs = data_key_get(ctxt->query, "jobs");

	if (dscript && (!(script = data_get_string(dscript)) || !script[0])) {
		if (!job_id || (job_id->step_id.job_id == NO_VAL))
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				   "Populated \"script\" field is required for job submission");
		else
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				   "Populated \"script\" field is required for JobId=%u update",
				   job_id->step_id.job_id);

		return;
	}
	if (djob && djobs) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Specify only one \"job\" or \"jobs\" fields but never both");
		return;
	}
	if (!djob && !djobs) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Specifing either \"job\" or \"jobs\" fields are required to job %s",
			   (!job_id || (job_id->step_id.job_id == NO_VAL) ?
			    " update" : "submission"));
		return;
	}
	if (job_id && djobs) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Specify only \"job\" field for updating an existing job");
		return;
	}

	if (djob) {
		if (data_get_type(djob) != DATA_TYPE_DICT) {
			resp_error(ctxt, ESLURM_DATA_EXPECTED_DICT, __func__,
				   "\"job\" field must be a dictionary with job properties");
			return;
		}

		if (job_id)
			_job_post_update(ctxt, djob, script, job_id);
		else
			_job_post_submit(ctxt, djob, script);
	} else {
		_job_post_het_submit(ctxt, djobs, script);
	}
}

static int _op_handler_job(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	int rc;
	slurm_selected_step_t job_id;
	char *job_id_str;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc)
		goto done;

	if (!(job_id_str = get_str_param("job_id", ctxt)))
		goto done;

	if ((rc = unfmt_job_id_string(job_id_str, &job_id))) {
		resp_error(ctxt, rc, __func__, "Failure parsing \"%s\"",
			   job_id_str);
		goto done;
	}

	if ((job_id.step_id.job_id == NO_VAL) || (job_id.step_id.job_id <= 0)
	    || (job_id.step_id.job_id >= MAX_JOB_ID)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Invalid JobID=%u rejected",
			   job_id.step_id.job_id);
		goto done;
	}

	if (method == HTTP_REQUEST_GET) {
		_handle_job_get(ctxt, &job_id);
	} else if (method == HTTP_REQUEST_DELETE) {
		_handle_job_delete(ctxt, &job_id);
	} else if (method == HTTP_REQUEST_POST) {
		_job_post(ctxt, &job_id);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

done:
	return fini_connection(ctxt);
}

static int _op_handler_submit_job(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query,
				  int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (method == HTTP_REQUEST_POST) {
		_job_post(ctxt, NULL);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern void init_op_jobs(void)
{
	bind_operation_handler("/slurm/v0.0.39/job/submit",
			       _op_handler_submit_job, 0);
	bind_operation_handler("/slurm/v0.0.39/jobs/", _op_handler_jobs, 0);
	bind_operation_handler("/slurm/v0.0.39/job/{job_id}", _op_handler_job,
			       0);
}

extern void destroy_op_jobs(void)
{
	unbind_operation_handler(_op_handler_submit_job);
	unbind_operation_handler(_op_handler_job);
	unbind_operation_handler(_op_handler_jobs);
}
