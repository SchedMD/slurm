/*****************************************************************************\
 *  job.c - Slurm REST API accounting job http operations handlers
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "src/slurmrestd/operations.h"

static void _dump_jobs(ctxt_t *ctxt, slurmdb_job_cond_t *job_cond)
{
	list_t *jobs = NULL;

	/* set cluster by default if not specified */
	if (job_cond &&
	    (!job_cond->cluster_list ||
	     list_is_empty(job_cond->cluster_list))) {
		FREE_NULL_LIST(job_cond->cluster_list);
		job_cond->cluster_list = list_create(xfree_ptr);
		list_append(job_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	if (!db_query_list(ctxt, &jobs, slurmdb_jobs_get, job_cond))
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_JOBS_RESP, jobs,
					 ctxt);

	FREE_NULL_LIST(jobs);

	if (job_cond)
		FREE_NULL_LIST(job_cond->cluster_list);
}

/* based on get_data() in sacct/options.c */
extern int op_handler_jobs(ctxt_t *ctxt)
{
	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	} else if (ctxt->query && data_get_dict_length(ctxt->query)) {
		slurmdb_job_cond_t *job_cond = NULL;

		if (DATA_PARSE(ctxt->parser, JOB_CONDITION_PTR, job_cond,
			       ctxt->query, ctxt->parent_path)) {
			return resp_error(ctxt, ESLURM_REST_INVALID_QUERY,
					  __func__,
					  "Rejecting request. Failure parsing query parameters");
		}

		/*
		 * default to grabbing all information based on _init_params()
		 */
		if (!job_cond->db_flags)
			job_cond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
		if (!job_cond->flags)
			job_cond->flags =
				(JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC);

		slurmdb_job_cond_def_start_end(job_cond);

		if (!job_cond->cluster_list)
			job_cond->cluster_list = list_create(xfree_ptr);

		if (list_is_empty(job_cond->cluster_list))
			list_append(job_cond->cluster_list,
				    xstrdup(slurm_conf.cluster_name));

		_dump_jobs(ctxt, job_cond);

		slurmdb_destroy_job_cond(job_cond);
	} else {
		_dump_jobs(ctxt, NULL);
	}

	return SLURM_SUCCESS;
}

static void _job_post_update(ctxt_t *ctxt, slurm_selected_step_t *job_id)
{
	int rc = SLURM_SUCCESS;
	list_t *ret_list = NULL;
	slurmdb_job_rec_t *job = NULL;
	slurmdb_job_cond_t job_cond = { 0 };

	job_cond.db_flags = SLURMDB_JOB_FLAG_NOTSET;
	job_cond.flags = JOBCOND_FLAG_NO_DEFAULT_USAGE;

	if (DATA_PARSE(ctxt->parser, JOB_MODIFY_PTR, job, ctxt->query,
		       ctxt->parent_path)) {
		resp_error(
			ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			"Rejecting request. Failure parsing job update request.");
		goto cleanup;
	}

	job_cond.step_list = list_create(slurm_destroy_selected_step);
	list_append(job_cond.step_list, job_id);

	if (db_modify_list(ctxt, &ret_list, &job_cond, job,
			   slurmdb_job_modify)) {
		resp_error(ctxt, rc, "slurmdb_job_modify()",
			   "Job update requested failed");
		goto cleanup;
	}

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_JOB_MODIFY_RESP, ret_list, ctxt);

cleanup:
	FREE_NULL_LIST(ret_list);
	slurmdb_destroy_job_cond_members(&job_cond);
	slurmdb_destroy_job_rec(job);
}

/* based on get_data() in sacct/options.c */
extern int op_handler_job(ctxt_t *ctxt)
{
	openapi_job_param_t params = { 0 };

	if ((ctxt->method != HTTP_REQUEST_GET) &&
	    (ctxt->method != HTTP_REQUEST_POST)) {
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_SLURMDBD_JOB_PARAM, params,
		       ctxt->parameters, ctxt->parent_path)) {
		return resp_error(
			ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			"Rejecting request. Failure parsing query parameters");
	}

	if (ctxt->method == HTTP_REQUEST_GET) {
		slurmdb_job_cond_t job_cond = {
			.flags = (JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC),
			.db_flags = SLURMDB_JOB_FLAG_NOTSET,
		};
		job_cond.step_list = list_create(slurm_destroy_selected_step);
		list_append(job_cond.step_list, params.id);

		_dump_jobs(ctxt, &job_cond);

		FREE_NULL_LIST(job_cond.step_list);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		_job_post_update(ctxt, params.id);
	} else {
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));
	}

	return SLURM_SUCCESS;
}
