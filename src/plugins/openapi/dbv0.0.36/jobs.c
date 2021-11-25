/*****************************************************************************\
 *  job.c - Slurm REST API accounting job http operations handlers
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/dbv0.0.36/api.h"

#define MAGIC_FOREACH_JOB 0xf8aefef3
typedef struct {
	int magic;
	data_t *jobs;
	List tres_list;
	List qos_list;
	List assoc_list;
} foreach_job_t;

static int _foreach_job(void *x, void *arg)
{
	slurmdb_job_rec_t *job = x;
	foreach_job_t *args = arg;
	parser_env_t penv = {
		.g_qos_list = args->qos_list,
		.g_tres_list = args->tres_list,
		.g_assoc_list = args->assoc_list,
	};

	xassert(args->magic == MAGIC_FOREACH_JOB);

	if (dump(PARSE_JOB, job, data_set_dict(data_list_append(args->jobs)),
		 &penv))
		return -1;
	else
		return 1;
}

typedef struct {
	data_t *errors;
	slurmdb_job_cond_t *job_cond;
} foreach_query_search_t;

data_for_each_cmd_t _foreach_list_entry(data_t *data, void *arg)
{
	List list = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	if (slurm_addto_char_list(list, data_get_string(data)) < 1)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _parse_csv_list(data_t *src, const char *key, List *list,
			   data_t *errors)
{
	if (!*list)
		*list = list_create(xfree_ptr);

	if (data_get_type(src) == DATA_TYPE_LIST) {
		if (data_list_for_each(src, _foreach_list_entry, *list) < 0)
			return resp_error(errors, ESLURM_REST_INVALID_QUERY,
					  "error parsing CSV in form of list",
					  key);

		return SLURM_SUCCESS;
	}

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return resp_error(errors, ESLURM_REST_INVALID_QUERY,
				  "format must be a string", key);

	if (slurm_addto_char_list(*list, data_get_string(src)) < 1)
		return resp_error(errors, ESLURM_REST_INVALID_QUERY,
				  "Unable to parse CSV list", key);

	return SLURM_SUCCESS;
}

typedef struct {
	char *field;
	int offset;
} sint_t;
static const sint_t int_list[] = {
	{ "cpus_max", offsetof(slurmdb_job_cond_t, cpus_max) },
	{ "cpus_min", offsetof(slurmdb_job_cond_t, cpus_min) },
	{ "exit_code", offsetof(slurmdb_job_cond_t, exitcode) },
	{ "nodes_min", offsetof(slurmdb_job_cond_t, nodes_min) },
	{ "nodes_max", offsetof(slurmdb_job_cond_t, nodes_max) },
};

typedef struct {
	char *field;
	uint32_t flag;
} flag_t;
static const flag_t flags[] = {
	/* skipping JOBCOND_FLAG_DUP */
	{ "skip_steps", JOBCOND_FLAG_NO_STEP },
	/* skipping JOBCOND_FLAG_NO_TRUNC */
	/* skipping JOBCOND_FLAG_RUNAWAY */
	/* skipping JOBCOND_FLAG_WHOLE_HETJOB */
	/* skipping JOBCOND_FLAG_NO_WHOLE_HETJOB */
	{ "disable_wait_for_result", JOBCOND_FLAG_NO_WAIT },
	/* skipping JOBCOND_FLAG_NO_DEFAULT_USAGE */
};

typedef struct {
	char *field;
	int offset;
} csv_list_t;
static const csv_list_t csv_lists[] = {
	{ "account", offsetof(slurmdb_job_cond_t, acct_list) },
	{ "association", offsetof(slurmdb_job_cond_t, associd_list) },
	{ "cluster", offsetof(slurmdb_job_cond_t, cluster_list) },
	{ "constraints", offsetof(slurmdb_job_cond_t, constraint_list) },
	{ "format", offsetof(slurmdb_job_cond_t, format_list) },
	{ "groups", offsetof(slurmdb_job_cond_t, groupid_list) },
	{ "job_name", offsetof(slurmdb_job_cond_t, jobname_list) },
	{ "partition", offsetof(slurmdb_job_cond_t, partition_list) },
	{ "qos", offsetof(slurmdb_job_cond_t, qos_list) },
	{ "reason", offsetof(slurmdb_job_cond_t, reason_list) },
	{ "reservation", offsetof(slurmdb_job_cond_t, resv_list) },
	{ "state", offsetof(slurmdb_job_cond_t, state_list) },
	{ "wckey", offsetof(slurmdb_job_cond_t, wckey_list) },
};

data_for_each_cmd_t _foreach_step(data_t *data, void *arg)
{
	List list = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	if (slurm_addto_step_list(list, data_get_string(data)) < 1)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

data_for_each_cmd_t _foreach_query_search(const char *key, data_t *data,
					  void *arg)
{
	foreach_query_search_t *args = arg;
	data_t *errors = args->errors;

	if (!xstrcasecmp("start_time", key)) {
		if (args->job_cond->flags & JOBCOND_FLAG_NO_DEFAULT_USAGE) {
			resp_error(
				errors, ESLURM_REST_INVALID_QUERY,
				"start_time and submit_time are mutually exclusive",
				key);
			return DATA_FOR_EACH_FAIL;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Time format must be a string", key);
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_start = parse_time(data_get_string(data),
							 1);

		if (!args->job_cond->usage_start) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Unable to parse time format", key);
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("end_time", key)) {
		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Time format must be a string", key);
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_end = parse_time(data_get_string(data),
						       1);

		if (!args->job_cond->usage_end) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Unable to parse time format", key);
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("submit_time", key)) {
		if (args->job_cond->usage_start) {
			resp_error(
				errors, ESLURM_REST_INVALID_QUERY,
				"start_time and submit_time are mutually exclusive",
				key);
			return DATA_FOR_EACH_FAIL;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Time format must be a string", key);
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_start = parse_time(data_get_string(data),
							 1);

		if (!args->job_cond->usage_start) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Unable to parse time format", key);
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->flags |= JOBCOND_FLAG_NO_DEFAULT_USAGE;

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("node", key)) {
		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "format must be a string", key);
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->used_nodes = xstrdup(
			data_get_string_const(data));

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("step", key)) {
		if (!args->job_cond->step_list)
			args->job_cond->step_list = list_create(
				slurm_destroy_selected_step);

		if (data_get_type(data) == DATA_TYPE_LIST) {
			if (data_list_for_each(data, _foreach_step,
					       args->job_cond->step_list) < 0) {
				(void) resp_error(
					errors, ESLURM_REST_INVALID_QUERY,
					"error parsing steps in form of list",
					key);
				return DATA_FOR_EACH_FAIL;
			}

			return DATA_FOR_EACH_CONT;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "format must be a string", key);
			return DATA_FOR_EACH_FAIL;
		}

		slurm_addto_step_list(args->job_cond->step_list,
				      data_get_string(data));

		if (!list_count(args->job_cond->step_list)) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "Unable to parse job/step format", key);
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	for (int i = 0; i < ARRAY_SIZE(csv_lists); i++) {
		if (!xstrcasecmp(csv_lists[i].field, key)) {
			List *list = (((void *) args->job_cond) +
				      csv_lists[i].offset);

			if (_parse_csv_list(data, key, list, errors))
				return DATA_FOR_EACH_FAIL;

			return DATA_FOR_EACH_CONT;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(flags); i++) {
		if (!xstrcasecmp(flags[i].field, key)) {
			if (data_convert_type(data, DATA_TYPE_BOOL) !=
			    DATA_TYPE_BOOL) {
				resp_error(errors, ESLURM_REST_INVALID_QUERY,
					   "must be an Boolean", key);
				return DATA_FOR_EACH_FAIL;
			}

			if (data_get_bool(data))
				args->job_cond->flags |= flags[i].flag;
			else
				args->job_cond->flags &= ~flags[i].flag;

			return DATA_FOR_EACH_CONT;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(int_list); i++) {
		if (!xstrcasecmp(int_list[i].field, key)) {
			int32_t *t = (((void *) args->job_cond) +
				      int_list[i].offset);

			if (data_convert_type(data, DATA_TYPE_INT_64) !=
			    DATA_TYPE_INT_64) {
				resp_error(errors, ESLURM_REST_INVALID_QUERY,
					   "must be an integer", key);
				return DATA_FOR_EACH_FAIL;
			}

			*t = data_get_int(data);

			return DATA_FOR_EACH_CONT;
		}
	}

	resp_error(errors, ESLURM_REST_INVALID_QUERY, "Unknown Query field",
		   NULL);
	return DATA_FOR_EACH_FAIL;
}

static int _dump_jobs(const char *context_id, http_request_method_t method,
		      data_t *parameters, data_t *query, int tag, data_t *resp,
		      void *auth, data_t *errors, slurmdb_job_cond_t *job_cond)
{
	int rc = SLURM_SUCCESS;
	slurmdb_assoc_cond_t assoc_cond = {
		.with_deleted = 1,
		.without_parent_info = 1,
		.without_parent_limits = 1,
	};
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	foreach_job_t args = {
		.magic = MAGIC_FOREACH_JOB,
		.jobs = data_set_list(data_key_set(resp, "jobs")),
	};
	List jobs = NULL;

	if (!db_query_list(errors, auth, &jobs, slurmdb_jobs_get, job_cond) &&
	    !db_query_list(errors, auth, &args.assoc_list,
			   slurmdb_associations_get, &assoc_cond) &&
	    !db_query_list(errors, auth, &args.qos_list, slurmdb_qos_get,
			   &qos_cond) &&
	    !db_query_list(errors, auth, &args.tres_list, slurmdb_tres_get,
			   &tres_cond) &&
	    (list_for_each(jobs, _foreach_job, &args) < 0))
		rc = ESLURM_DATA_CONV_FAILED;

	FREE_NULL_LIST(args.tres_list);
	FREE_NULL_LIST(args.qos_list);
	FREE_NULL_LIST(args.assoc_list);
	FREE_NULL_LIST(jobs);

	return rc;
}

/* based on get_data() in sacct/options.c */
extern int op_handler_jobs(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	data_t *errors = populate_response_format(resp);

	if (query && data_get_dict_length(query)) {
		slurmdb_job_cond_t job_cond = {
			/*
			 * default to grabbing all information
			 * based on _init_params()
			 */
			.flags = (JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC |
				  JOBCOND_FLAG_WHOLE_HETJOB),
			.db_flags = SLURMDB_JOB_FLAG_NOTSET,
		};
		foreach_query_search_t args = {
			.errors = errors,
			.job_cond = &job_cond,
		};

		if (data_dict_for_each(query, _foreach_query_search, &args) < 0)
			return SLURM_ERROR;

		return _dump_jobs(context_id, method, parameters, query, tag,
				  resp, auth, errors, &job_cond);
	} else
		return _dump_jobs(context_id, method, parameters, query, tag,
				  resp, auth, errors, NULL);
}

/* based on get_data() in sacct/options.c */
static int _op_handler_job(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	char *jobid;
	slurmdb_job_cond_t job_cond = {
		.flags = (JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC |
			  JOBCOND_FLAG_WHOLE_HETJOB),
		.db_flags = SLURMDB_JOB_FLAG_NOTSET,
	};

	if ((jobid = get_str_param("job_id", errors, parameters))) {
		job_cond.step_list = list_create(slurm_destroy_selected_step);
		slurm_addto_step_list(job_cond.step_list, jobid);
	} else
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc)
		rc = _dump_jobs(context_id, method, parameters, query, tag,
				resp, auth, errors, &job_cond);

	FREE_NULL_LIST(job_cond.step_list);
	return rc;
}

extern void init_op_job(void)
{
	bind_operation_handler("/slurmdb/v0.0.36/jobs/", op_handler_jobs, 0);
	bind_operation_handler("/slurmdb/v0.0.36/job/{job_id}", _op_handler_job,
			       0);
}

extern void destroy_op_job(void)
{
	unbind_operation_handler(_op_handler_job);
	unbind_operation_handler(op_handler_jobs);
}
