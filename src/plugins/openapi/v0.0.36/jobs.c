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

#include "config.h"

#define _GNU_SOURCE

#include <search.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/ref.h"
#include "src/common/select.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/strlcpy.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.36/api.h"

typedef struct {
	const char *param;
	int optval;
	bool disabled;
} params_t;

static struct hsearch_data hash_params = { 0 };
/* track array of parameter names that have been forced to lower case */
static char **lower_param_names = NULL;

/*
 * This needs to match common_options in src/common/slurm_opt.c
 * for every optval (aliases allowed)
 *
 * This list *should* be alphabetically sorted by the param string.
 *
 * Some of these options have been disabled as they are inappropriate for
 * use through slurmrestd (e.g., "burst_buffer_file"), others were disabled
 * by executive fiat.
 */
const params_t job_params[] = {
	{ "accelerator_binding", LONG_OPT_ACCEL_BIND, true },
	{ "account", 'A' },
	{ "account_gather_frequency", LONG_OPT_ACCTG_FREQ },
	{ "allocation_node_list", LONG_OPT_ALLOC_NODELIST, true },
	{ "array", 'a' },
	{ "argv", LONG_OPT_ARGV },
	{ "batch_features", LONG_OPT_BATCH },
	{ "begin_time", 'b' },
	{ "bell", LONG_OPT_BELL, true },
	{ "burst_buffer_file", LONG_OPT_BURST_BUFFER_FILE, true },
	{ "burst_buffer", LONG_OPT_BURST_BUFFER_SPEC },
	{ "cluster_constraint", LONG_OPT_CLUSTER_CONSTRAINT },
	{ "cluster", LONG_OPT_CLUSTER, true },
	{ "clusters", 'M', true },
	{ "comment", LONG_OPT_COMMENT },
	{ "compress", LONG_OPT_COMPRESS, true },
	{ "constraint", 'C' },
	{ "constraints", 'C' },
	{ "contiguous", LONG_OPT_CONTIGUOUS, true },
	{ "core_specification", 'S' },
	{ "cores_per_socket", LONG_OPT_CORESPERSOCKET },
	{ "cpu_binding hint", LONG_OPT_HINT },
	{ "cpu_binding", LONG_OPT_CPU_BIND, true },
	{ "cpu_frequency", LONG_OPT_CPU_FREQ },
	{ "cpus_per_gpu", LONG_OPT_CPUS_PER_GPU },
	{ "cpus_per_task", 'c' },
	{ "current_working_directory", 'D' },
	{ "cwd", 'D' },
	{ "deadline", LONG_OPT_DEADLINE },
	{ "debugger_test", LONG_OPT_DEBUGGER_TEST, true },
	{ "delay_boot", LONG_OPT_DELAY_BOOT },
	{ "dependency", 'd' },
	{ "disable_status", 'X', true },
	{ "distribution", 'm' },
	{ "environment", LONG_OPT_ENVIRONMENT },
	{ "epilog", LONG_OPT_EPILOG, true },
	{ "exclude_nodes", 'x' },
	{ "excluded_nodes", 'x', true },
	{ "exclusive", LONG_OPT_EXCLUSIVE },
	{ "export_file", LONG_OPT_EXPORT_FILE, true },
	{ "export", LONG_OPT_EXPORT, true },
	{ "extra_node_info", 'B', true },
	{ "get_user_environment", LONG_OPT_GET_USER_ENV },
	{ "gpu_binding", LONG_OPT_GPU_BIND },
	{ "gpu_frequency", LONG_OPT_GPU_FREQ },
	{ "gpus", 'G' },
	{ "gpus_per_node", LONG_OPT_GPUS_PER_NODE },
	{ "gpus_per_socket", LONG_OPT_GPUS_PER_SOCKET },
	{ "gpus_per_task", LONG_OPT_GPUS_PER_TASK },
	{ "gres_flags", LONG_OPT_GRES_FLAGS },
	{ "gres", LONG_OPT_GRES },
	{ "group_id", LONG_OPT_GID, true },
	{ "help", 'h', true },
	{ "hold", 'H' },
	{ "ignore_pbs", LONG_OPT_IGNORE_PBS, true },
	{ "immediate", 'I', true },
	{ "job_id", LONG_OPT_JOBID, true },
	{ "job_name", 'J' },
	{ "kill_command", 'K', true },
	{ "kill_on_bad_exit", 'K', true },
	{ "kill_on_invalid_dependency", LONG_OPT_KILL_INV_DEP },
	{ "kill_on_invalid dependency", LONG_OPT_KILL_INV_DEP },
	{ "label", 'l', true },
	{ "license", 'L' },
	{ "licenses", 'L' },
	{ "mail_type", LONG_OPT_MAIL_TYPE },
	{ "mail_user", LONG_OPT_MAIL_USER },
	{ "max_threads", 'T', true },
	{ "msc_label", LONG_OPT_MCS_LABEL },
	{ "memory_binding", LONG_OPT_MEM_BIND },
	{ "memory_per_CPU", LONG_OPT_MEM_PER_CPU },
	{ "memory_per_GPU", LONG_OPT_MEM_PER_GPU },
	{ "memory_per_node", LONG_OPT_MEM },
	{ "message_timeout", LONG_OPT_MSG_TIMEOUT, true },
	{ "minimum_CPUs_per_node", LONG_OPT_MINCPUS },
	{ "minimum_nodes", LONG_OPT_USE_MIN_NODES },
	{ "mpi", LONG_OPT_MPI, true },
	{ "multiple_program", LONG_OPT_MULTI, true },
	{ "name", 'J' },
	{ "network", LONG_OPT_NETWORK, true },
	{ "nice", LONG_OPT_NICE },
	{ "no_allocation", 'Z', true },
	{ "no_bell", LONG_OPT_NO_BELL, true },
	/* security implications to trying to read a user file */
	{ "nodefile", 'F', true },
	{ "nodelist", 'w' },
	{ "node_list", 'w' },
	{ "nodes", 'N' },
	{ "no_kill", 'k' },
	{ "no_requeue", LONG_OPT_NO_REQUEUE }, /* not in OAS */
	{ "no_shell", LONG_OPT_NO_SHELL, true },
	{ "open_mode", LONG_OPT_OPEN_MODE },
	{ "overcommit", 'O', true },
	{ "oversubscribe", 's', true },
	{ "hetjob_group", LONG_OPT_HET_GROUP, true },
	{ "parsable", LONG_OPT_PARSABLE, true },
	{ "partition", 'p' },
	{ "power_flags", LONG_OPT_POWER, true },
	{ "preserve_environment", 'E', true },
	{ "priority", LONG_OPT_PRIORITY, true },
	{ "profile", LONG_OPT_PROFILE },
	{ "prolog", LONG_OPT_PROLOG, true },
	{ "propagate", LONG_OPT_PROPAGATE, true },
	{ "pty", LONG_OPT_PTY, true },
	{ "qos", 'q' },
	{ "quiet", 'Q', true },
	{ "quit_on_interrupt", LONG_OPT_QUIT_ON_INTR, true },
	{ "reboot", LONG_OPT_REBOOT, true },
	{ "relative", 'r', true },
	{ "requeue", LONG_OPT_REQUEUE },
	{ "required_nodes", 'w', true },
	{ "required_switches", LONG_OPT_SWITCHES, true },
	{ "reservation", LONG_OPT_RESERVATION },
	{ "reserve_port", LONG_OPT_RESV_PORTS, true },
	{ "reserve_ports", LONG_OPT_RESV_PORTS, true },
	{ "signal", LONG_OPT_SIGNAL },
	{ "slurmd_debug", LONG_OPT_SLURMD_DEBUG, true },
	{ "sockets_per_node", LONG_OPT_SOCKETSPERNODE },
	{ "spread_job", LONG_OPT_SPREAD_JOB },
	{ "standard_error", 'e' },
	{ "standard_in", 'i' },
	{ "standard_input", 'i' },
	{ "standard_out", 'o' },
	{ "standard_output", 'o' },
	{ "task_epilog", LONG_OPT_TASK_EPILOG, true },
	{ "task_prolog", LONG_OPT_TASK_PROLOG, true },
	{ "tasks", 'n' },
	{ "ntasks", 'n' },
	{ "tasks_per_core", LONG_OPT_NTASKSPERCORE },
	{ "ntasks_per_core", LONG_OPT_NTASKSPERCORE },
	{ "ntasks_per_gpu", LONG_OPT_NTASKSPERGPU },
	{ "tasks_per_node", LONG_OPT_NTASKSPERNODE },
	{ "ntasks_per_node", LONG_OPT_NTASKSPERNODE },
	{ "tasks_per_socket", LONG_OPT_NTASKSPERSOCKET },
	{ "ntasks_per_socket", LONG_OPT_NTASKSPERSOCKET },
	{ "ntasks_per_tres", LONG_OPT_NTASKSPERTRES },
	{ "temporary_disk_per_node", LONG_OPT_TMP },
	{ "test_only", LONG_OPT_TEST_ONLY },
	{ "thread_specification", LONG_OPT_THREAD_SPEC },
	{ "threads_per_Core", LONG_OPT_THREADSPERCORE },
	{ "threads", 'T', true },
	{ "time_limit", 't' },
	{ "time minimum", LONG_OPT_TIME_MIN },
	/* Handler for LONG_OPT_TRES_PER_JOB never defined
	 * { "TRES per job", LONG_OPT_TRES_PER_JOB, true },
	 */
	{ "umask", LONG_OPT_UMASK },
	{ "unbuffered", 'u', true },
	{ "unknown", '?', true },
	{ "usage", LONG_OPT_USAGE, true },
	{ "user_id", LONG_OPT_UID, true },
	{ "version", 'V', true },
	{ "verbose", 'v', true },
	{ "wait_all_nodes", LONG_OPT_WAIT_ALL_NODES },
	{ "wait_for_switch", LONG_OPT_SWITCH_WAIT, true },
	{ "wait", 'W', true },
	{ "wckey", LONG_OPT_WCKEY },
	{ "wrap", LONG_OPT_WCKEY, true },
	{ "x11", LONG_OPT_X11, true },
};
static const int param_count = (sizeof(job_params) / sizeof(params_t));

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_JOBS,
	URL_TAG_JOB,
	URL_TAG_JOB_SUBMIT,
} url_tag_t;

typedef struct {
	int rc;
	bool het_job;
	List jobs;
	job_desc_msg_t *job;
} job_parse_list_t;

static void _list_delete_job_desc_msg_t(void *_p)
{
	xassert(_p);
	slurm_free_job_desc_msg(_p);
}

#define _parse_error(...)                                                    \
	do {                                                                 \
		const char *error_string = xstrdup_printf(__VA_ARGS__);      \
		error("%s", error_string);                                   \
		data_t *error = data_list_append(errors);                    \
		data_set_dict(error);                                        \
		data_set_string(data_key_set(error, "error"), error_string); \
		xfree(error_string);                                         \
		if (errno) {                                                 \
			rc = errno;                                          \
			errno = 0;                                           \
		} else                                                       \
			rc = SLURM_ERROR;                                    \
		data_set_int(data_key_set(error, "error_code"), rc);         \
	} while (0)

typedef struct {
	slurm_opt_t *opt;
	data_t *errors;
} job_foreach_params_t;

static data_for_each_cmd_t _per_job_param(const char *key, const data_t *data,
					  void *arg)
{
	int rc;
	char lkey[256];
	job_foreach_params_t *args = arg;
	data_t *errors = args->errors;
	params_t *p = NULL;
	ENTRY e = { .key = lkey };
	ENTRY *re = NULL;

	/* clone key to force all lower characters */
	strlcpy(lkey, key, sizeof(lkey));
	xstrtolower(lkey);

	if (!(rc = hsearch_r(e, FIND, &re, &hash_params))) {
		_parse_error("Unknown key \"%s\": %m", lkey);
		return DATA_FOR_EACH_FAIL;
	}

	p = re->data;
	if (p->disabled) {
		_parse_error("Disabled key: \"%s\"", p->param);
		return DATA_FOR_EACH_FAIL;
	}

	if ((rc = slurm_process_option_data(args->opt, p->optval, data,
					    errors))) {
		_parse_error("process failed for key %s with error: %s",
			     key, slurm_strerror(rc));
		return DATA_FOR_EACH_FAIL;
	}

	return DATA_FOR_EACH_CONT;
}

/*
 * copied from _fill_job_desc_from_opts() in src/sbatch/sbatch.c
 * Returns 0 on success, -1 on failure
 */
static int _fill_job_desc_from_opts(slurm_opt_t *opt, job_desc_msg_t *desc)
{
	const sbatch_opt_t *sbopt = opt->sbatch_opt;

	if (!desc)
		return -1;

	if (!opt->job_name)
		desc->name = xstrdup("openapi");

	desc->array_inx = xstrdup(sbopt->array_inx);
	desc->batch_features = sbopt->batch_features;
	desc->container = xstrdup(opt->container);

	desc->wait_all_nodes = sbopt->wait_all_nodes;

	env_array_free(desc->environment);
	desc->environment = env_array_copy((const char **) opt->environment);

	if (sbopt->export_file) {
		error("%s: rejecting request to use load environment from file: %s",
		      __func__, sbopt->export_file);
		return -1;
	}
	if (opt->export_env) {
		/*
		 * job environment is loaded directly via data_t list and not
		 * via the --export command.
		 */
		error("%s: rejecting request to control export environment: %s",
		      __func__, opt->export_env);
		return -1;
	}
	if (opt->get_user_env_time >= 0) {
		env_array_overwrite(&desc->environment,
				    "SLURM_GET_USER_ENV", "1");
	}

	if ((opt->distribution & SLURM_DIST_STATE_BASE) ==
	    SLURM_DIST_ARBITRARY) {
		env_array_overwrite_fmt(&desc->environment,
					"SLURM_ARBITRARY_NODELIST",
					"%s", desc->req_nodes);
	}

	desc->env_size = envcount(desc->environment);

	/* Disable sending uid/gid as it is handled by auth layer */
	desc->user_id = NO_VAL;
	desc->group_id = NO_VAL;

	desc->argc     = sbopt->script_argc;
	desc->argv     = sbopt->script_argv;
	desc->std_err  = xstrdup(opt->efname);
	desc->std_in   = xstrdup(opt->ifname);
	desc->std_out  = xstrdup(opt->ofname);

	if (sbopt->requeue != NO_VAL)
		desc->requeue = sbopt->requeue;

	return 0;
}

/*
 * based on _fill_job_desc_from_opts from sbatch
 */
static job_desc_msg_t *_parse_job_desc(const data_t *job, data_t *errors,
				       bool update_only)
{
	int rc = SLURM_SUCCESS;
	job_desc_msg_t *req = NULL;
	char *opt_string = NULL;
	sbatch_opt_t sbopt = { 0 };
	slurm_opt_t opt = { .sbatch_opt = &sbopt };
	struct option *spanked = slurm_option_table_create(&opt, &opt_string);

	job_foreach_params_t args = {
		.opt = &opt,
		.errors = errors,
	};

	slurm_reset_all_options(&opt, true);
	if (data_dict_for_each_const(job, _per_job_param, &args) < 0) {
		rc = ESLURM_REST_FAIL_PARSING;
		goto cleanup;
	}

	req = slurm_opt_create_job_desc(&opt, !update_only);
	if (!update_only)
		req->task_dist = SLURM_DIST_UNKNOWN;

	if (_fill_job_desc_from_opts(&opt, req)) {
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (!update_only && (!req->environment || !req->env_size)) {
		/*
		 * Jobs provided via data must have their environment
		 * setup or they will simply be rejected. Error now instead of
		 * bothering the controller.
		 */
		data_t *err = data_set_dict(data_list_append(errors));
		rc = ESLURM_ENVIRONMENT_MISSING;
		data_set_string(data_key_set(err, "error"),
				"environment must be set");
		data_set_int(data_key_set(err, "error_code"), rc);
		goto cleanup;
	}
	xassert(req->env_size == envcount(req->environment));

cleanup:
	slurm_free_options_members(&opt);
	slurm_option_table_destroy(spanked);
	xfree(opt_string);

	if (rc) {
		slurm_free_job_desc_msg(req);
		return NULL;
	}

	/*
	 * Add generated environment variables to match
	 * _opt_verify() in src/sbatch/opt.c
	 */
	if (req->name)
		env_array_overwrite(&req->environment, "SLURM_JOB_NAME",
				    req->name);

	if (req->open_mode) {
		/* Propage mode to spawned job using environment variable */
		if (req->open_mode == OPEN_MODE_APPEND)
			env_array_overwrite(&req->environment,
					    "SLURM_OPEN_MODE", "a");
		else
			env_array_overwrite(&req->environment,
					    "SLURM_OPEN_MODE", "t");
	}

	if (req->dependency)
		env_array_overwrite(&req->environment, "SLURM_JOB_DEPENDENCY",
				    req->dependency);

	/* intentionally skipping SLURM_EXPORT_ENV */

	if (req->profile) {
		char tmp[128];
		acct_gather_profile_to_string_r(req->profile, tmp);
		env_array_overwrite(&req->environment, "SLURM_PROFILE", tmp);
	}

	if (req->acctg_freq)
		env_array_overwrite(&req->environment, "SLURM_ACCTG_FREQ",
				    req->acctg_freq);

#ifdef HAVE_NATIVE_CRAY
	if (req->network)
		env_array_overwrite(&req->environment, "SLURM_NETWORK",
				    req->network);
#endif

	if (req->cpu_freq_min || req->cpu_freq_max || req->cpu_freq_gov) {
		char *tmp = cpu_freq_to_cmdline(req->cpu_freq_min,
						req->cpu_freq_max,
						req->cpu_freq_gov);

		if (tmp)
			env_array_overwrite(&req->environment,
					    "SLURM_CPU_FREQ_REQ", tmp);

		xfree(tmp);
	}

	/* update size of env in case it changed */
	req->env_size = envcount(req->environment);

	return req;
}

typedef struct {
	size_t i;
	data_t *errors;
	char *script;
	bool update_only;
	job_parse_list_t *rc;
} _parse_job_component_t;

static data_for_each_cmd_t _parse_job_component(const data_t *data, void *arg)
{
	_parse_job_component_t *j = arg;
	job_desc_msg_t *job_desc;
	job_parse_list_t *rc = j->rc;

	if ((job_desc = _parse_job_desc(data, j->errors, j->update_only))) {
		if (j->script) {
			//assign script to first job of het jobs
			job_desc->script = j->script;
			j->script = NULL;
		}

		list_append(rc->jobs, job_desc);
	} else { /* parsing failed */
		data_t *error = data_list_append(j->errors);
		char *error_string = xstrdup_printf(
			"%s: unexpected failure parsing het job: %zd",
			__func__, j->i);
		data_set_dict(error);
		data_set_string(data_key_set(error, "error"), error_string);
		//error("%s", error_string);
		xfree(error_string);
		rc->rc = ESLURM_REST_FAIL_PARSING;
		return DATA_FOR_EACH_FAIL;
	}

	(j->i)++;
	return DATA_FOR_EACH_CONT;
}

static job_parse_list_t _parse_job_list(const data_t *jobs, char *script,
					data_t *errors, bool update_only)
{
	job_parse_list_t rc = { 0 };
	xassert(update_only || script);

	if (jobs == NULL)
		rc.rc = ESLURM_REST_INVALID_JOBS_DESC;
	else if (data_get_type(jobs) == DATA_TYPE_LIST) {
		_parse_job_component_t j = {
			.rc = &rc,
			.script = script,
			.update_only = update_only,
			.errors = errors,
		};

		rc.het_job = true;
		rc.jobs = list_create(_list_delete_job_desc_msg_t);
		rc.rc = SLURM_SUCCESS;

		data_list_for_each_const(jobs, _parse_job_component, &j);

		if (rc.rc)
			FREE_NULL_LIST(rc.jobs);
	} else if (data_get_type(jobs) == DATA_TYPE_DICT) {
		rc.het_job = false;
		rc.job = _parse_job_desc(jobs, errors, update_only);

		if (rc.job) {
			rc.job->script = script;
			rc.rc = SLURM_SUCCESS;
		} else
			rc.rc = ESLURM_REST_FAIL_PARSING;
	} else
		rc.rc = ESLURM_REST_INVALID_JOBS_DESC;

	return rc;
}

static data_t *dump_job_info(slurm_job_info_t *job, data_t *jd)
{
	xassert(data_get_type(jd) == DATA_TYPE_NULL);
	data_set_dict(jd);
	data_set_string(data_key_set(jd, "account"), job->account);
	data_set_int(data_key_set(jd, "accrue_time"), job->accrue_time);
	data_set_string(data_key_set(jd, "admin_comment"), job->admin_comment);
	/* alloc_node intentionally skipped */
	data_set_int(data_key_set(jd, "array_job_id"), job->array_job_id);
	if (job->array_task_id == NO_VAL)
		data_set_null(data_key_set(jd, "array_task_id"));
	else
		data_set_int(data_key_set(jd, "array_task_id"),
			     job->array_task_id);
	data_set_int(data_key_set(jd, "array_max_tasks"), job->array_max_tasks);
	data_set_string(data_key_set(jd, "array_task_string"),
			job->array_task_str);
	data_set_int(data_key_set(jd, "association_id"), job->assoc_id);
	data_set_string(data_key_set(jd, "batch_features"),
			job->batch_features);
	data_set_bool(data_key_set(jd, "batch_flag"), job->batch_flag == 1);
	data_set_string(data_key_set(jd, "batch_host"), job->batch_host);
	data_t *bitflags = data_key_set(jd, "flags");
	data_set_list(bitflags);
	if (job->bitflags & KILL_INV_DEP)
		data_set_string(data_list_append(bitflags), "KILL_INV_DEP");
	if (job->bitflags & NO_KILL_INV_DEP)
		data_set_string(data_list_append(bitflags), "NO_KILL_INV_DEP");
	if (job->bitflags & HAS_STATE_DIR)
		data_set_string(data_list_append(bitflags), "HAS_STATE_DIR");
	if (job->bitflags & BACKFILL_TEST)
		data_set_string(data_list_append(bitflags), "BACKFILL_TEST");
	if (job->bitflags & GRES_ENFORCE_BIND)
		data_set_string(data_list_append(bitflags),
				"GRES_ENFORCE_BIND");
	if (job->bitflags & TEST_NOW_ONLY)
		data_set_string(data_list_append(bitflags), "TEST_NOW_ONLY");
	if (job->bitflags & SPREAD_JOB)
		data_set_string(data_list_append(bitflags), "SPREAD_JOB");
	if (job->bitflags & USE_MIN_NODES)
		data_set_string(data_list_append(bitflags), "USE_MIN_NODES");
	if (job->bitflags & JOB_KILL_HURRY)
		data_set_string(data_list_append(bitflags), "JOB_KILL_HURRY");
	if (job->bitflags & TRES_STR_CALC)
		data_set_string(data_list_append(bitflags), "TRES_STR_CALC");
	if (job->bitflags & SIB_JOB_FLUSH)
		data_set_string(data_list_append(bitflags), "SIB_JOB_FLUSH");
	if (job->bitflags & HET_JOB_FLAG)
		data_set_string(data_list_append(bitflags), "HET_JOB_FLAG");
	if (job->bitflags & JOB_CPUS_SET)
		data_set_string(data_list_append(bitflags), "JOB_CPUS_SET ");
	if (job->bitflags & TOP_PRIO_TMP)
		data_set_string(data_list_append(bitflags), "TOP_PRIO_TMP");
	if (job->bitflags & JOB_ACCRUE_OVER)
		data_set_string(data_list_append(bitflags), "JOB_ACCRUE_OVER");
	if (job->bitflags & GRES_DISABLE_BIND)
		data_set_string(data_list_append(bitflags),
				"GRES_DISABLE_BIND");
	if (job->bitflags & JOB_WAS_RUNNING)
		data_set_string(data_list_append(bitflags), "JOB_WAS_RUNNING");
	if (job->bitflags & JOB_MEM_SET)
		data_set_string(data_list_append(bitflags), "JOB_MEM_SET");
	if (job->bitflags & JOB_RESIZED)
		data_set_string(data_list_append(bitflags), "JOB_RESIZED");
	/* boards_per_node intentionally omitted */
	data_set_string(data_key_set(jd, "burst_buffer"), job->burst_buffer);
	data_set_string(data_key_set(jd, "burst_buffer_state"),
			job->burst_buffer_state);
	data_set_string(data_key_set(jd, "cluster"), job->cluster);
	data_set_string(data_key_set(jd, "cluster_features"),
			job->cluster_features);
	data_set_string(data_key_set(jd, "command"), job->command);
	data_set_string(data_key_set(jd, "comment"), job->comment);
	if (job->contiguous != NO_VAL16)
		data_set_bool(data_key_set(jd, "contiguous"),
			      job->contiguous == 1);
	else
		data_set_null(data_key_set(jd, "contiguous"));
	if (job->core_spec == NO_VAL16) {
		data_set_null(data_key_set(jd, "core_spec"));
		data_set_null(data_key_set(jd, "thread_spec"));
	} else {
		if (CORE_SPEC_THREAD & job->core_spec) {
			data_set_int(data_key_set(jd, "core_spec"),
				     job->core_spec);
			data_set_null(data_key_set(jd, "thread_spec"));
		} else {
			data_set_int(data_key_set(jd, "thread_spec"),
				     (job->core_spec & ~CORE_SPEC_THREAD));
			data_set_null(data_key_set(jd, "core_spec"));
		}
	}
	if (job->cores_per_socket == NO_VAL16)
		data_set_null(data_key_set(jd, "cores_per_socket"));
	else
		data_set_int(data_key_set(jd, "cores_per_socket"),
			     job->cores_per_socket);
	//skipped cpu_bind and cpu_bind_type per description
	if (job->billable_tres == (double)NO_VAL)
		data_set_null(data_key_set(jd, "billable_tres"));
	else
		data_set_float(data_key_set(jd, "billable_tres"),
			       job->billable_tres);
	if (job->cpu_freq_min == NO_VAL)
		data_set_null(data_key_set(jd, "cpus_per_task"));
	else
		data_set_int(data_key_set(jd, "cpus_per_task"),
			     job->cpus_per_task);
	if (job->cpu_freq_min == NO_VAL)
		data_set_null(data_key_set(jd, "cpu_frequency_minimum"));
	else
		data_set_int(data_key_set(jd, "cpu_frequency_minumum"),
			     job->cpu_freq_min);
	if (job->cpu_freq_max == NO_VAL)
		data_set_null(data_key_set(jd, "cpu_frequency_maximum"));
	else
		data_set_int(data_key_set(jd, "cpu_frequency_maximum"),
			     job->cpu_freq_max);
	if (job->cpu_freq_gov == NO_VAL)
		data_set_null(data_key_set(jd, "cpu_frequency_governor"));
	else
		data_set_int(data_key_set(jd, "cpu_frequency_governor"),
			     job->cpu_freq_gov);
	data_set_string(data_key_set(jd, "cpus_per_tres"), job->cpus_per_tres);
	data_set_int(data_key_set(jd, "deadline"), job->deadline);
	if (job->delay_boot == NO_VAL)
		data_set_null(data_key_set(jd, "delay_boot"));
	else
		data_set_int(data_key_set(jd, "delay_boot"), job->delay_boot);
	data_set_string(data_key_set(jd, "dependency"), job->dependency);
	data_set_int(data_key_set(jd, "derived_exit_code"), job->derived_ec);
	data_set_int(data_key_set(jd, "eligible_time"), job->eligible_time);
	data_set_int(data_key_set(jd, "end_time"), job->end_time);
	data_set_string(data_key_set(jd, "excluded_nodes"), job->exc_nodes);
	/* exc_node_inx intentionally omitted */
	data_set_int(data_key_set(jd, "exit_code"), job->exit_code);
	data_set_string(data_key_set(jd, "features"), job->features);
	data_set_string(data_key_set(jd, "federation_origin"),
			job->fed_origin_str);
	data_set_string(data_key_set(jd, "federation_siblings_active"),
			job->fed_siblings_active_str);
	data_set_string(data_key_set(jd, "federation_siblings_viable"),
			job->fed_siblings_viable_str);
	data_t *gres_detail = data_key_set(jd, "gres_detail");
	data_set_list(gres_detail);
	for (size_t i = 0; i < job->gres_detail_cnt; ++i)
		data_set_string(data_list_append(gres_detail),
				job->gres_detail_str[i]);
	if (job->group_id == NO_VAL)
		data_set_null(data_key_set(jd, "group_id"));
	else
		data_set_int(data_key_set(jd, "group_id"), job->group_id);
	if (job->job_id == NO_VAL)
		data_set_null(data_key_set(jd, "job_id"));
	else
		data_set_int(data_key_set(jd, "job_id"), job->job_id);
	data_t *jrsc = data_key_set(jd, "job_resources");
	data_set_dict(jrsc);
	if (job->job_resrcs) {
		/* based on log_job_resources() */
		job_resources_t *j = job->job_resrcs;
		data_set_string(data_key_set(jrsc, "nodes"), j->nodes);
		const size_t array_size = bit_size(j->core_bitmap);
		size_t sock_inx = 0, sock_reps = 0, bit_inx = 0;

		data_set_int(data_key_set(jrsc, "allocated_cpus"), j->ncpus);
		data_set_int(data_key_set(jrsc, "allocated_hosts"), j->nhosts);

		data_t *nodes = data_key_set(jrsc, "allocated_nodes");
		data_set_dict(nodes);
		for (size_t node_inx = 0; node_inx < j->nhosts; node_inx++) {
			data_t *node = data_key_set_int(nodes, node_inx);
			data_set_dict(node);
			data_t *sockets = data_key_set(node, "sockets");
			data_t *cores = data_key_set(node, "cores");
			data_set_dict(sockets);
			data_set_dict(cores);
			const size_t bit_reps =
				((size_t) j->sockets_per_node[sock_inx]) *
				((size_t) j->cores_per_socket[sock_inx]);

			if (sock_reps >= j->sock_core_rep_count[sock_inx]) {
				sock_inx++;
				sock_reps = 0;
			}
			sock_reps++;

			if (j->memory_allocated)
				data_set_int(data_key_set(node,
							  "memory"),
					     j->memory_allocated[node_inx]);

			data_set_int(data_key_set(node, "cpus"),
				     j->cpus[node_inx]);

			for (size_t i = 0; i < bit_reps; i++) {
				if (bit_inx >= array_size) {
					error("%s: array size wrong", __func__);
					xassert(false);
					break;
				}
				if (bit_test(j->core_bitmap, bit_inx)) {
					data_t *socket = data_key_set_int(
						sockets,
						(i /
						 j->cores_per_socket[sock_inx]));
					data_t *core = data_key_set_int(
						cores,
						(i %
						 j->cores_per_socket[sock_inx]));

					if (bit_test(j->core_bitmap_used,
						     bit_inx)) {
						data_set_string(socket,
								"assigned");
						data_set_string(core,
								"assigned");
					} else {
						data_set_string(socket,
								"unassigned");
						data_set_string(core,
								"unassigned");
					}
				}
				bit_inx++;
			}
		}
	}
	data_set_string(data_key_set(jd, "job_state"),
			job_state_string(job->job_state));
	data_set_int(data_key_set(jd, "last_sched_evaluation"), job->last_sched_eval);
	data_set_string(data_key_set(jd, "licenses"), job->licenses);
	if (job->max_cpus == NO_VAL)
		data_set_null(data_key_set(jd, "max_cpus"));
	else
		data_set_int(data_key_set(jd, "max_cpus"), job->max_cpus);
	if (job->max_nodes == NO_VAL)
		data_set_null(data_key_set(jd, "max_nodes"));
	else
		data_set_int(data_key_set(jd, "max_nodes"), job->max_nodes);
	data_set_string(data_key_set(jd, "mcs_label"), job->mcs_label);
	data_set_string(data_key_set(jd, "memory_per_tres"), job->mem_per_tres);
	data_set_string(data_key_set(jd, "name"), job->name);
	/* network intentionally omitted */
	data_set_string(data_key_set(jd, "nodes"), job->nodes);
	if (job->nice == NO_VAL || job->nice == NICE_OFFSET)
		data_set_null(data_key_set(jd, "nice"));
	else
		data_set_int(data_key_set(jd, "nice"), job->nice - NICE_OFFSET);
	/* node_index intentionally omitted */
	if (job->ntasks_per_core == NO_VAL16 ||
	    job->ntasks_per_core == INFINITE16)
		data_set_null(data_key_set(jd, "tasks_per_core"));
	else
		data_set_int(data_key_set(jd, "tasks_per_core"),
			     job->ntasks_per_core);
	data_set_int(data_key_set(jd, "tasks_per_node"), job->ntasks_per_node);
	if (job->ntasks_per_socket == NO_VAL16 ||
	    job->ntasks_per_socket == INFINITE16)
		data_set_null(data_key_set(jd, "tasks_per_socket"));
	else
		data_set_int(data_key_set(jd, "tasks_per_socket"),
			     job->ntasks_per_socket);
	data_set_int(data_key_set(jd, "tasks_per_board"),
		     job->ntasks_per_board);
	if (job->num_tasks != NO_VAL && job->num_tasks != INFINITE)
		data_set_int(data_key_set(jd, "cpus"), job->num_cpus);
	else
		data_set_null(data_key_set(jd, "cpus"));
	data_set_int(data_key_set(jd, "node_count"), job->num_nodes);
	if (job->num_tasks != NO_VAL && job->num_tasks != INFINITE)
		data_set_int(data_key_set(jd, "tasks"), job->num_tasks);
	else
		data_set_null(data_key_set(jd, "tasks"));
	data_set_int(data_key_set(jd, "het_job_id"), job->het_job_id);
	data_set_string(data_key_set(jd, "het_job_id_set"),
			job->het_job_id_set);
	data_set_int(data_key_set(jd, "het_job_offset"), job->het_job_offset);
	data_set_string(data_key_set(jd, "partition"), job->partition);
	if (job->pn_min_memory & MEM_PER_CPU) {
		data_set_null(data_key_set(jd, "memory_per_node"));
		data_set_int(data_key_set(jd, "memory_per_cpu"),
			     (job->pn_min_memory & ~MEM_PER_CPU));
	} else if (job->pn_min_memory) {
		data_set_int(data_key_set(jd, "memory_per_node"),
			     job->pn_min_memory);
		data_set_null(data_key_set(jd, "memory_per_cpu"));
	} else {
		data_set_null(data_key_set(jd, "memory_per_node"));
		data_set_null(data_key_set(jd, "memory_per_cpu"));
	}
	data_set_int(data_key_set(jd, "minimum_cpus_per_node"), job->pn_min_cpus);
	data_set_int(data_key_set(jd, "minimum_tmp_disk_per_node"),
		     job->pn_min_tmp_disk);
	/* power_flags intentionally omitted */
	data_set_int(data_key_set(jd, "preempt_time"), job->preempt_time);
	data_set_int(data_key_set(jd, "pre_sus_time"), job->pre_sus_time);
	if (job->priority == NO_VAL || job->priority == INFINITE)
		data_set_null(data_key_set(jd, "priority"));
	else
		data_set_int(data_key_set(jd, "priority"), job->priority);
	if (job->profile == ACCT_GATHER_PROFILE_NOT_SET)
		data_set_null(data_key_set(jd, "profile"));
	else {
		//based on acct_gather_profile_to_string
		data_t *profile = data_key_set(jd, "profile");
		data_set_list(profile);

		if (job->profile == ACCT_GATHER_PROFILE_NONE)
			data_set_string(data_list_append(profile), "None");
		if (job->profile & ACCT_GATHER_PROFILE_ENERGY)
			data_set_string(data_list_append(profile), "Energy");
		if (job->profile & ACCT_GATHER_PROFILE_LUSTRE)
			data_set_string(data_list_append(profile), "Lustre");
		if (job->profile & ACCT_GATHER_PROFILE_NETWORK)
			data_set_string(data_list_append(profile), "Network");
		if (job->profile & ACCT_GATHER_PROFILE_TASK)
			data_set_string(data_list_append(profile), "Task");
	}
	data_set_string(data_key_set(jd, "qos"), job->qos);
	data_set_bool(data_key_set(jd, "reboot"), job->reboot);
	data_set_string(data_key_set(jd, "required_nodes"), job->req_nodes);
	/* skipping req_node_inx */
	data_set_bool(data_key_set(jd, "requeue"), job->requeue);
	data_set_int(data_key_set(jd, "resize_time"), job->resize_time);
	data_set_int(data_key_set(jd, "restart_cnt"), job->restart_cnt);
	data_set_string(data_key_set(jd, "resv_name"), job->resv_name);
	/* sched_nodes intentionally omitted */
	/* select_jobinfo intentionally omitted */
	switch (job->shared) {
	case JOB_SHARED_NONE:
		data_set_string(data_key_set(jd, "shared"), "none");
		break;
	case JOB_SHARED_OK:
		data_set_string(data_key_set(jd, "shared"), "shared");
		break;
	case JOB_SHARED_USER:
		data_set_string(data_key_set(jd, "shared"), "user");
		break;
	case JOB_SHARED_MCS:
		data_set_string(data_key_set(jd, "shared"), "mcs");
		break;
	case NO_VAL16:
		data_set_null(data_key_set(jd, "shared"));
		break;
	default:
		data_set_int(data_key_set(jd, "shared"), job->shared);
		xassert(false);
		break;
	}
	data_t *sflags = data_key_set(jd, "show_flags");
	data_set_list(sflags);
	if (job->show_flags & SHOW_ALL)
		data_set_string(data_list_append(sflags), "SHOW_ALL");
	if (job->show_flags & SHOW_DETAIL)
		data_set_string(data_list_append(sflags), "SHOW_DETAIL");
	if (job->show_flags & SHOW_MIXED)
		data_set_string(data_list_append(sflags), "SHOW_MIXED");
	if (job->show_flags & SHOW_LOCAL)
		data_set_string(data_list_append(sflags), "SHOW_LOCAL");
	if (job->show_flags & SHOW_SIBLING)
		data_set_string(data_list_append(sflags), "SHOW_SIBLING");
	if (job->show_flags & SHOW_FEDERATION)
		data_set_string(data_list_append(sflags), "SHOW_FEDERATION");
	if (job->show_flags & SHOW_FUTURE)
		data_set_string(data_list_append(sflags), "SHOW_FUTURE");
	data_set_int(data_key_set(jd, "sockets_per_board"),
		     job->sockets_per_board);
	if (job->sockets_per_node == NO_VAL16)
		data_set_null(data_key_set(jd, "sockets_per_node"));
	else
		data_set_int(data_key_set(jd, "sockets_per_node"),
			     job->sockets_per_node);
	data_set_int(data_key_set(jd, "start_time"), job->start_time);
	/* start_protocol_ver intentionally omitted */
	data_set_string(data_key_set(jd, "state_description"), job->state_desc);
	data_set_string(data_key_set(jd, "state_reason"),
			job_reason_string(job->state_reason));
	data_set_string(data_key_set(jd, "standard_error"), job->std_err);
	data_set_string(data_key_set(jd, "standard_input"), job->std_in);
	data_set_string(data_key_set(jd, "standard_output"), job->std_out);
	data_set_int(data_key_set(jd, "submit_time"), job->submit_time);
	data_set_int(data_key_set(jd, "suspend_time"), job->suspend_time);
	data_set_string(data_key_set(jd, "system_comment"),
			job->system_comment);
	if (job->time_limit != INFINITE)
		data_set_int(data_key_set(jd, "time_limit"), job->time_limit);
	else
		data_set_null(data_key_set(jd, "time_limit"));
	if (job->time_min != INFINITE)
		data_set_int(data_key_set(jd, "time_minimum"), job->time_min);
	else
		data_set_null(data_key_set(jd, "time_minimum"));
	if (job->threads_per_core == NO_VAL16)
		data_set_null(data_key_set(jd, "threads_per_core"));
	else
		data_set_int(data_key_set(jd, "threads_per_core"),
			     job->threads_per_core);
	data_set_string(data_key_set(jd, "tres_bind"), job->tres_bind);
	data_set_string(data_key_set(jd, "tres_freq"), job->tres_freq);
	data_set_string(data_key_set(jd, "tres_per_job"), job->tres_per_job);
	data_set_string(data_key_set(jd, "tres_per_node"), job->tres_per_node);
	data_set_string(data_key_set(jd, "tres_per_socket"),
			job->tres_per_socket);
	data_set_string(data_key_set(jd, "tres_per_task"), job->tres_per_task);
	data_set_string(data_key_set(jd, "tres_req_str"), job->tres_req_str);
	data_set_string(data_key_set(jd, "tres_alloc_str"),
			job->tres_alloc_str);
	data_set_int(data_key_set(jd, "user_id"), job->user_id);
	data_set_string(data_key_set(jd, "user_name"), job->user_name);
	/* wait4switch intentionally omitted */
	data_set_string(data_key_set(jd, "wckey"), job->wckey);
	data_set_string(data_key_set(jd, "current_working_directory"),
			job->work_dir);

	return jd;
}

static int _op_handler_jobs(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	job_info_msg_t *job_info_ptr = NULL;
	(void) populate_response_format(resp);
	data_t *jobs = data_set_list(data_key_set(resp, "jobs"));

	debug4("%s: jobs handler called by %s", __func__, context_id);

	rc = slurm_load_jobs((time_t)NULL, &job_info_ptr,
			     SHOW_ALL|SHOW_DETAIL);

	if (rc == SLURM_SUCCESS && job_info_ptr &&
	    job_info_ptr->record_count) {
		for (size_t i = 0; i < job_info_ptr->record_count; ++i) {
			dump_job_info(job_info_ptr->job_array + i,
				      data_list_append(jobs));
		}
	}

	slurm_free_job_info_msg(job_info_ptr);

	return rc;
}

#define _job_error(...)                                                      \
	do {                                                                 \
		const char *error_string = xstrdup_printf(__VA_ARGS__);      \
		error("%s", error_string);                                   \
		data_t *error = data_list_append(errors);                    \
		data_set_dict(error);                                        \
		data_set_string(data_key_set(error, "error"), error_string); \
		xfree(error_string);                                         \
		if (errno) {                                                 \
			rc = errno;                                          \
			errno = 0;                                           \
		} else                                                       \
			rc = SLURM_ERROR;                                    \
		data_set_int(data_key_set(error, "error_code"), errno);      \
	} while (0)

static int _handle_job_get(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, const uint32_t job_id,
			   data_t *const errors)
{
	int rc = SLURM_SUCCESS;
	job_info_msg_t *job_info_ptr = NULL;
	rc = slurm_load_job(&job_info_ptr, job_id, SHOW_ALL|SHOW_DETAIL);
	data_t *jobs = data_set_list(data_key_set(resp, "jobs"));

	if (!rc && job_info_ptr && job_info_ptr->record_count) {
		for (size_t i = 0; i < job_info_ptr->record_count; ++i) {
			dump_job_info(job_info_ptr->job_array + i,
				      data_list_append(jobs));
		}
	} else {
		_job_error("%s: unknown job %d", __func__, job_id);
	}

	slurm_free_job_info_msg(job_info_ptr);

	return rc;
}

static int _handle_job_delete(const char *context_id,
			      http_request_method_t method,
			      data_t *parameters, data_t *query, int tag,
			      data_t *resp, const uint32_t job_id,
			      data_t *const errors, const int signal)
{
	if (slurm_kill_job(job_id, signal, KILL_FULL_JOB)) {
		int rc;

		/* Already signaled jobs are considered a success here */
		if (errno == ESLURM_ALREADY_DONE)
			return SLURM_SUCCESS;

		_job_error("%s: unable to kill job %d with signal %d: %s",
			   __func__, job_id, signal, slurm_strerror(errno));
		return rc;
	}

	return SLURM_SUCCESS;
}

static int _handle_job_post(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp,
			    const uint32_t job_id, data_t *const errors)
{
	int rc = SLURM_SUCCESS;

	if (get_log_level() >= LOG_LEVEL_DEBUG5) {
		char *buffer = NULL;

		data_g_serialize(&buffer, query, MIME_TYPE_JSON,
				 DATA_SER_FLAGS_COMPACT);
		debug5("%s: job update from %s: %s", __func__, context_id,
		       buffer);

		xfree(buffer);
	}

	job_parse_list_t jobs_rc = _parse_job_list(query, NULL, errors,
						   true);

	if (jobs_rc.rc) {
		_job_error("%s: job parsing failed for %s",
			   __func__, context_id);
	} else {
		debug3("%s: job parsing successful for %s",
		       __func__, context_id);
		rc = jobs_rc.rc;
		if (jobs_rc.het_job) {
			_job_error("%s: unexpected het job request from %s",
				   __func__, context_id);
		} else {
			job_array_resp_msg_t *resp = NULL;
			errno = 0;
			jobs_rc.job->job_id = job_id;
			debug5("%s: sending job_id:%d update for %s",
			       __func__, job_id, context_id);

			rc = slurm_update_job2(jobs_rc.job, &resp);

			if (rc)
				_job_error("%s: job update from %s failed: %s",
					   __func__, context_id,
					   slurm_strerror(errno));
			else if (resp && resp->error_code && resp->error_code)
				_job_error(
					"%s: job array update from %s failed with error_code: %s",
					__func__, context_id,
					slurm_strerror(*resp->error_code));

			slurm_free_job_desc_msg(jobs_rc.job);
			slurm_free_job_array_resp(resp);
		}
	}

	return rc;
}

static int _op_handler_job(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *data_jobid;
	uint32_t job_id = 0;
	data_t *errors = populate_response_format(resp);
	debug4("%s: job handler %s called by %s with tag %d",
	       __func__, get_http_method_string(method), context_id, tag);

	if (!parameters)
		_job_error("%s: [%s] missing request parameters",
			   __func__, context_id);
	else if (!(data_jobid = data_key_get(parameters, "job_id")))
		_job_error("%s: [%s] missing job_id in parameters",
			   __func__, context_id);
	else if (data_get_type(data_jobid) != DATA_TYPE_INT_64)
		_job_error("%s: [%s] invalid job_id data type",
			   __func__, context_id);
	else if (data_get_int(data_jobid) == 0)
		_job_error("%s: [%s] job_id is zero",
			   __func__, context_id);
	else if (data_get_int(data_jobid) >= NO_VAL)
		_job_error("%s: [%s] job_id too large: %" PRId64,
			   __func__, context_id, data_get_int(data_jobid));
	else
		job_id = data_get_int(data_jobid);

	if (rc) {
	} else if (tag == URL_TAG_JOB && method == HTTP_REQUEST_GET) {
		rc = _handle_job_get(context_id, method, parameters, query, tag,
				     resp, job_id, errors);
	} else if (tag == URL_TAG_JOB &&
		   method == HTTP_REQUEST_DELETE) {
		int signal = 0;
		data_t *dsignal = data_key_get(query, "signal");

		if (data_get_type(dsignal) == DATA_TYPE_INT_64)
			signal = data_get_int(dsignal);
		else if (data_get_type(dsignal) == DATA_TYPE_STRING)
			signal = sig_name2num(data_get_string(dsignal));
		else
			signal = SIGKILL;

		if (signal < 1 || signal >= SIGRTMAX)
			_job_error("%s: invalid signal: %d", __func__, signal);
		else
			rc = _handle_job_delete(context_id, method, parameters,
						query, tag, resp, job_id,
						errors, signal);
	} else if (tag == URL_TAG_JOB &&
		   method == HTTP_REQUEST_POST) {
		rc = _handle_job_post(context_id, method, parameters, query,
				      tag, resp, job_id, errors);
	} else {
		_job_error("%s: unknown request", __func__);
	}

	return rc;
}

static int _op_handler_submit_job_post(const char *context_id,
				       http_request_method_t method,
				       data_t *parameters, data_t *query,
				       int tag, data_t *d, data_t *errors)
{
	int rc = SLURM_SUCCESS;
	submit_response_msg_t *resp = NULL;
	char *script = NULL;

	if (!query) {
		error("%s: [%s] unexpected empty query for job",
		      __func__, context_id);
		rc = ESLURM_REST_INVALID_QUERY;
		goto finish;
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG5) {
		char *buffer = NULL;

		data_g_serialize(&buffer, query, MIME_TYPE_JSON,
				 DATA_SER_FLAGS_COMPACT);
		debug5("%s: job submit query from %s: %s",
		       __func__, context_id, buffer);
		xfree(buffer);

		data_g_serialize(&buffer, parameters, MIME_TYPE_JSON,
				 DATA_SER_FLAGS_COMPACT);
		debug5("%s: job submit parameters from %s: %s",
		       __func__, context_id, buffer);
		xfree(buffer);
	}

	if (data_retrieve_dict_path_string(query, "script", &script)) {
		error("%s: unexpected missing script for job from %s",
		      __func__, context_id);
		rc = ESLURM_JOB_SCRIPT_MISSING;
		goto finish;
	}

	if (!rc) {
		job_parse_list_t jobs_rc = { 0 };
		data_t *jobs = data_key_get(query, "job");

		if (!jobs)
			/* allow jobs too since we can take a list of jobs */
			jobs = data_key_get(query, "jobs");

		if (!jobs) {
			error("%s: [%s] missing job specification field",
			      __func__, context_id);
			rc = ESLURM_REST_INVALID_JOBS_DESC;
		} else if ((jobs_rc = _parse_job_list(jobs, script, errors,
						      false))
				   .rc) {
			error("%s: job parsing failed for %s",
			      __func__, context_id);
			rc = jobs_rc.rc;
		} else {
			debug3("%s: job parsing successful for %s",
			       __func__, context_id);
			rc = jobs_rc.rc;
			if (jobs_rc.het_job) {
				if (slurm_submit_batch_het_job(jobs_rc.jobs,
							       &resp))
					rc = errno;
				list_destroy(jobs_rc.jobs);
			} else {
				if (slurm_submit_batch_job(jobs_rc.job, &resp))
					rc = errno;
				slurm_free_job_desc_msg(jobs_rc.job);
			}
		}
	}

	if (!rc) {
		xassert(resp);
		debug5("%s: job_id:%d step_id:%d error_code:%d message: %s for job submission from %s",
		       __func__, resp->job_id, resp->step_id, resp->error_code,
		       resp->job_submit_user_msg, context_id);

		data_set_int(data_key_set(d, "job_id"), resp->job_id);
		switch (resp->step_id) {
		case SLURM_PENDING_STEP:
			data_set_string(data_key_set(d, "step_id"),
					"PENDING");
			break;
		case SLURM_BATCH_SCRIPT:
			data_set_string(data_key_set(d, "step_id"),
					"BATCH");
			break;
		case SLURM_EXTERN_CONT:
			data_set_string(data_key_set(d, "step_id"),
					"EXTERN");
			break;
		case SLURM_INTERACTIVE_STEP:
			data_set_string(data_key_set(d, "step_id"),
					"INTERACTIVE");
			break;
		default:
			data_set_int(data_key_set(d, "step_id"),
				     resp->step_id);
			break;
		}
		if (resp->error_code) {
			data_t *error = data_list_append(errors);
			data_set_dict(error);
			data_set_int(data_key_set(error, "error_code"),
				     resp->error_code);
			data_set_string(data_key_set(error, "error"),
					slurm_strerror(resp->error_code));
		}
		data_set_string(data_key_set(d, "job_submit_user_msg"),
				resp->job_submit_user_msg);
	}

finish:
	if (rc) {
		data_t *error = data_set_dict(data_list_append(errors));
		data_set_int(data_key_set(error, "error_code"), rc);
		data_set_string(data_key_set(error, "error"),
				slurm_strerror(rc));

		debug5("%s: [%s] job submission failed with %d: %s",
		       __func__, context_id, rc, slurm_strerror(rc));
	}

	slurm_free_submit_response_response_msg(resp);

	return rc;
}

static int _op_handler_submit_job(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query, int tag,
				  data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);

	debug4("%s: job submit handler %s called by %s with tag %d",
	       __func__, get_http_method_string(method), context_id, tag);

	if (tag == URL_TAG_JOB_SUBMIT && method == HTTP_REQUEST_POST)
		rc = _op_handler_submit_job_post(context_id, method, parameters,
						 query, tag, resp, errors);
	else {
		data_t *errord = data_list_append(errors);
		data_set_dict(errord);
		data_set_int(data_key_set(errord, "error_code"),
			     ESLURM_INVALID_JOB_ID);
		data_set_string(data_key_set(errord, "error"),
				"unexpected HTTP method");

		error("%s: [%s] job submission failed unexpected method:%s tag:%d",
		      __func__, context_id,
		      get_http_method_string(method), tag);

		/* TODO: find better error */
		rc = ESLURM_INVALID_JOB_ID;
	}

	return rc;
}

extern void init_op_jobs(void)
{
	lower_param_names = xcalloc(sizeof(char *), param_count);

	if (!hcreate_r(param_count, &hash_params))
		fatal("%s: unable to create hash table: %m",
		      __func__);

	/* populate hash table with all parameter names */
	for (int i = 0; i < param_count; i++) {
		ENTRY e = {
			.key = xstrdup(job_params[i].param),
			.data = (void *)&job_params[i],
		};
		ENTRY *re = NULL;

		lower_param_names[i] = e.key;

		/* force all lower characters */
		xstrtolower(e.key);

		if (!hsearch_r(e, ENTER, &re, &hash_params))
			fatal("%s: unable to populate hash table: %m",
			      __func__);
	}

	bind_operation_handler("/slurm/v0.0.36/jobs/", _op_handler_jobs,
			       URL_TAG_JOBS);
	bind_operation_handler("/slurm/v0.0.36/job/{job_id}", _op_handler_job,
			       URL_TAG_JOB);
	bind_operation_handler("/slurm/v0.0.36/job/submit",
			       _op_handler_submit_job, URL_TAG_JOB_SUBMIT);
}

extern void destroy_op_jobs(void)
{
	hdestroy_r(&hash_params);
	for (int i = 0; i < param_count; i++)
		xfree(lower_param_names[i]);
	xfree(lower_param_names);

	unbind_operation_handler(_op_handler_submit_job);
	unbind_operation_handler(_op_handler_job);
	unbind_operation_handler(_op_handler_jobs);
}
