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

#include <ctype.h>
#include <limits.h>
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

#include "src/plugins/openapi/dbv0.0.39/api.h"

#define MAGIC_FOREACH_JOB 0xf8aefef3
#define MAGIC_FOREACH_SEARCH 0xf9aeaef3

static int _add_list_job_state(List char_list, char *values);

/* typedef for adding a function to add a char* to a List */
typedef int (*add_list_t) (List char_list, char *values);

typedef struct {
	int magic; /* MAGIC_FOREACH_JOB */
	data_t *jobs;
	ctxt_t *ctxt;
} foreach_job_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_SEARCH */
	ctxt_t *ctxt;
	slurmdb_job_cond_t *job_cond;
} foreach_query_search_t;

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
	add_list_t add_to;
} csv_list_t;

static const csv_list_t csv_lists[] = {
	{
		"account",
		offsetof(slurmdb_job_cond_t, acct_list),
		slurm_addto_char_list
	},
	{
		"association",
		offsetof(slurmdb_job_cond_t, associd_list),
		slurm_addto_char_list
	},
	{
		"cluster",
		offsetof(slurmdb_job_cond_t, cluster_list),
		slurm_addto_char_list
	},
	{
		"constraints",
		offsetof(slurmdb_job_cond_t, constraint_list),
		slurm_addto_char_list
	},
	{
		"format",
		offsetof(slurmdb_job_cond_t, format_list),
		slurm_addto_char_list
	},
	{
		"groups",
		offsetof(slurmdb_job_cond_t, groupid_list),
		slurm_addto_char_list
	},
	{
		"job_name",
		offsetof(slurmdb_job_cond_t, jobname_list),
		slurm_addto_char_list
	},
	{
		"partition",
		offsetof(slurmdb_job_cond_t, partition_list),
		slurm_addto_char_list
	},
	{
		"qos",
		offsetof(slurmdb_job_cond_t, qos_list),
		slurm_addto_char_list
	},
	{
		"reason",
		offsetof(slurmdb_job_cond_t, reason_list),
		slurm_addto_char_list
	},
	{
		"reservation",
		offsetof(slurmdb_job_cond_t, resv_list),
		slurm_addto_char_list
	},
	{
		"state",
		offsetof(slurmdb_job_cond_t, state_list),
		_add_list_job_state
	},
	{
		"users",
		offsetof(slurmdb_job_cond_t, userid_list),
		slurm_addto_char_list
	},
	{
		"wckey",
		offsetof(slurmdb_job_cond_t, wckey_list),
		slurm_addto_char_list
	},
};

static int _foreach_job(void *x, void *arg)
{
	slurmdb_job_rec_t *job = x;
	foreach_job_t *args = arg;
	data_t *jobs = data_list_append(args->jobs);

	xassert(args->magic == MAGIC_FOREACH_JOB);
	xassert(data_get_type(args->jobs) == DATA_TYPE_LIST);

	if (DATA_DUMP(args->ctxt->parser, JOB, *job, jobs))
		return -1;
	else
		return 1;
}

static data_for_each_cmd_t _foreach_list_entry(data_t *data, void *arg)
{
	List list = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	if (slurm_addto_char_list(list, data_get_string(data)) < 1)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _parse_csv_list(data_t *src, const char *key, List *list,
			   ctxt_t *ctxt, add_list_t add_to)
{
	if (!*list)
		*list = list_create(xfree_ptr);

	if (data_get_type(src) == DATA_TYPE_LIST) {
		if (data_list_for_each(src, _foreach_list_entry, *list) < 0)
			return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
					  "error parsing CSV in form of list");

		return SLURM_SUCCESS;
	}

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				  "format must be a string");

	if (add_to(*list, data_get_string(src)) < 1)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				  "Unable to parse CSV list");

	return SLURM_SUCCESS;
}

/*
 * Convert job state to numeric job state
 * The return value is the same as slurm_addto_char_list():
 *   the number of items added (zero on failure)
 */
static int _add_list_job_state(List char_list, char *values)
{
	int rc = 0;
	char *last = NULL, *vdup, *value;

	vdup = xstrdup(values);
	value = strtok_r(vdup, ",", &last);
	while (value) {
		char *id_str;
		uint32_t id = NO_VAL;

		if (isdigit(value[0])) {
			unsigned long id_ul;
			errno = 0;
			id_ul = slurm_atoul(value);
			/*
			 * Since zero is a valid value, we have to check if
			 * errno is also set to know if it was an error.
			 */
			if ((!id_ul && errno) || (id_ul == ULONG_MAX))
				break;
			id = (uint32_t) id_ul;
		} else {
			if ((id = job_state_num(value)) == NO_VAL)
				break;
			else
				id = JOB_STATE_BASE & id;
		}

		if (id >= JOB_END) {
			break;
		}

		id_str = xstrdup_printf("%u", id);
		rc = slurm_addto_char_list(char_list, id_str);
		xfree(id_str);

		value = strtok_r(NULL, ",", &last);
	}
	xfree(vdup);

	return rc;
}

static data_for_each_cmd_t _foreach_step(data_t *data, void *arg)
{
	List list = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	if (slurm_addto_step_list(list, data_get_string(data)) < 1)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_query_search(const char *key,
						 data_t *data,
						 void *arg)
{
	foreach_query_search_t *args = arg;
	ctxt_t *ctxt = args->ctxt;

	xassert(args->magic == MAGIC_FOREACH_SEARCH);

	if (!xstrcasecmp("start_time", key)) {
		if (args->job_cond->flags & JOBCOND_FLAG_NO_DEFAULT_USAGE) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "start_time and submit_time are mutually exclusive");
			return DATA_FOR_EACH_FAIL;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Time format must be a string");
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_start = parse_time(data_get_string(data),
							 1);

		if (!args->job_cond->usage_start) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Unable to parse time format");
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("end_time", key)) {
		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Time format must be a string");
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_end = parse_time(data_get_string(data),
						       1);

		if (!args->job_cond->usage_end) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Unable to parse time format");
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("submit_time", key)) {
		if (args->job_cond->usage_start) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				"start_time and submit_time are mutually exclusive");
			return DATA_FOR_EACH_FAIL;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Time format must be a string");
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->usage_start = parse_time(data_get_string(data),
							 1);

		if (!args->job_cond->usage_start) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Unable to parse time format");
			return DATA_FOR_EACH_FAIL;
		}

		args->job_cond->flags |= JOBCOND_FLAG_NO_DEFAULT_USAGE;

		return DATA_FOR_EACH_CONT;
	}

	if (!xstrcasecmp("node", key)) {
		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "format must be a string");
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
				resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
					"error parsing steps in form of list");
				return DATA_FOR_EACH_FAIL;
			}

			return DATA_FOR_EACH_CONT;
		}

		if (data_convert_type(data, DATA_TYPE_STRING) !=
		    DATA_TYPE_STRING) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "format must be a string");
			return DATA_FOR_EACH_FAIL;
		}

		slurm_addto_step_list(args->job_cond->step_list,
				      data_get_string(data));

		if (!list_count(args->job_cond->step_list)) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "Unable to parse job/step format");
			return DATA_FOR_EACH_FAIL;
		}

		return DATA_FOR_EACH_CONT;
	}

	for (int i = 0; i < ARRAY_SIZE(csv_lists); i++) {
		if (!xstrcasecmp(csv_lists[i].field, key)) {
			List *list = (((void *) args->job_cond) +
				      csv_lists[i].offset);

			if (_parse_csv_list(data, key, list, ctxt,
					    csv_lists[i].add_to))
				return DATA_FOR_EACH_FAIL;

			if (!xstrcasecmp("groups", key)) {
				List list2 = list_create(xfree_ptr);
				if (list_for_each_ro(*list, groupname_to_gid,
						     list2) < 0) {
					list_destroy(list2);
					resp_error(ctxt,
						   ESLURM_REST_MISSING_GID, key,
						   "error resolving GID from group name");
					return DATA_FOR_EACH_FAIL;
				}
				list_destroy(*list);
				*list = list2;
			} else if (!xstrcasecmp("users", key)) {
				List list2 = list_create(xfree_ptr);
				if (list_for_each_ro(*list, username_to_uid,
						     list2) < 0) {
					list_destroy(list2);
					resp_error(ctxt,
						   ESLURM_REST_MISSING_UID, key,
						   "error resolving UID from user name");
					return DATA_FOR_EACH_FAIL;
				}
				list_destroy(*list);
				*list = list2;
			}

			return DATA_FOR_EACH_CONT;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(flags); i++) {
		if (!xstrcasecmp(flags[i].field, key)) {
			if (data_convert_type(data, DATA_TYPE_BOOL) !=
			    DATA_TYPE_BOOL) {
				resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
					   "must be an Boolean");
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
				resp_error(ctxt, ESLURM_REST_INVALID_QUERY, key,
					   "must be an integer");
				return DATA_FOR_EACH_FAIL;
			}

			*t = data_get_int(data);

			return DATA_FOR_EACH_CONT;
		}
	}

	resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
		   "unexpected Query field: %s", key);
	return DATA_FOR_EACH_FAIL;
}

static void _dump_jobs(ctxt_t *ctxt, slurmdb_job_cond_t *job_cond)
{
	foreach_job_t args = {
		.magic = MAGIC_FOREACH_JOB,
		.ctxt = ctxt,
	};
	List jobs = NULL;

	args.jobs = data_set_list(data_key_set(ctxt->resp, "jobs"));

	/* set cluster by default if not specified */
	if (job_cond &&
	    (!job_cond->cluster_list ||
	     list_is_empty(job_cond->cluster_list))) {
		FREE_NULL_LIST(job_cond->cluster_list);
		job_cond->cluster_list = list_create(xfree_ptr);
		list_append(job_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	if (!db_query_list(ctxt, &jobs, slurmdb_jobs_get, job_cond) && jobs)
		list_for_each(jobs, _foreach_job, &args);

	FREE_NULL_LIST(jobs);

	if (job_cond)
		FREE_NULL_LIST(job_cond->cluster_list);
}

/* based on get_data() in sacct/options.c */
extern int op_handler_jobs(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc) {
		/* no-op - already logged */
	} else if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	} else if (query && data_get_dict_length(query)) {
		slurmdb_job_cond_t job_cond = {
			/*
			 * default to grabbing all information
			 * based on _init_params()
			 */
			.flags = (JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC),
			.db_flags = SLURMDB_JOB_FLAG_NOTSET,
		};
		foreach_query_search_t args = {
			.magic = MAGIC_FOREACH_SEARCH,
			.ctxt = ctxt,
			.job_cond = &job_cond,
		};

		if (data_dict_for_each(query, _foreach_query_search, &args) >=
		    0)
			_dump_jobs(ctxt, &job_cond);
	} else {
		_dump_jobs(ctxt, NULL);
	}

	return fini_connection(ctxt);
}

/* based on get_data() in sacct/options.c */
static int _op_handler_job(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	char *jobid;
	slurmdb_job_cond_t job_cond = {
		.flags = (JOBCOND_FLAG_DUP | JOBCOND_FLAG_NO_TRUNC),
		.db_flags = SLURMDB_JOB_FLAG_NOTSET,
	};
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc) {
		/* no-op - already logged */
	} else if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	} else if ((jobid = get_str_param("job_id", ctxt))) {
		job_cond.step_list = list_create(slurm_destroy_selected_step);
		slurm_addto_step_list(job_cond.step_list, jobid);

		_dump_jobs(ctxt, &job_cond);
	}

	FREE_NULL_LIST(job_cond.step_list);
	return fini_connection(ctxt);
}

extern void init_op_job(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/jobs/", op_handler_jobs, 0);
	bind_operation_handler("/slurmdb/v0.0.39/job/{job_id}", _op_handler_job,
			       0);
}

extern void destroy_op_job(void)
{
	unbind_operation_handler(_op_handler_job);
	unbind_operation_handler(op_handler_jobs);
}
