/*  launch.c - Define job launch plugin functions.
*****************************************************************************
*  Copyright (C) 2012 SchedMD LLC
*  Written by Danny Auble <da@schedmd.com>
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

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include "src/srun/allocate.h"
#include "src/srun/fname.h"
#include "src/srun/launch.h"
#include "src/srun/multi_prog.h"
#include "src/srun/task_state.h"

#include "src/api/pmi_server.h"

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/net.h"
#include "src/common/xstring.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xsignal.h"
#include "src/interfaces/gres.h"

#ifndef OPEN_MPI_PORT_ERROR
/* This exit code indicates the launched Open MPI tasks could
 *	not open the reserved port. It was already open by some
 *	other process. */
#define OPEN_MPI_PORT_ERROR 108
#endif

#define MAX_STEP_RETRIES 4

static List local_job_list = NULL;
static uint32_t *local_global_rc = NULL;
static pthread_mutex_t launch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t het_job_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  start_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static slurm_opt_t *opt_save = NULL;

static List task_state_list = NULL;
static time_t launch_start_time;
static bool retry_step_begin = false;
static int  retry_step_cnt = 0;

extern char **environ;

static int _step_signal(int signal)
{
	srun_job_t *my_srun_job;
	ListIterator iter;
	int rc = SLURM_SUCCESS, rc2;

	if (!local_job_list) {
		debug("%s: local_job_list does not exist yet", __func__);
		return SLURM_ERROR;
	}

	iter = list_iterator_create(local_job_list);
	while ((my_srun_job = (srun_job_t *) list_next(iter))) {
		info("Terminating %ps", &my_srun_job->step_id);
		rc2 = slurm_kill_job_step(my_srun_job->step_id.job_id,
					  my_srun_job->step_id.step_id, signal);
		if (rc2)
			rc = rc2;
	}
	list_iterator_destroy(iter);
	return rc;
}

static char *_hostset_to_string(hostset_t hs)
{
	size_t n = 1024;
	size_t maxsize = 1024 * 64;
	char *str = NULL;

	do {
		str = xrealloc(str, n);
	} while ((hostset_ranged_string(hs, n*=2, str) < 0) && (n < maxsize));

	/*
	 *  If string was truncated, indicate this with a '+' suffix.
	 */
	if (n >= maxsize)
		strcpy(str + (maxsize - 2), "+");

	return str;
}

/*
 * Convert an array of task IDs into a list of host names
 * RET: the string, caller must xfree() this value
 */
static char *_task_ids_to_host_list(int ntasks, uint32_t *taskids,
				    srun_job_t *my_srun_job)
{
	int i, task_cnt = 0;
	hostset_t hs;
	char *hosts;
	slurm_step_layout_t *sl;

	if ((sl = launch_common_get_slurm_step_layout(my_srun_job)) == NULL)
		return (xstrdup("Unknown"));

	/*
	 * If overhead of determining the hostlist is too high then srun
	 * communications will timeout and fail, so return "Unknown" instead.
	 *
	 * See slurm_step_layout_host_id() in src/common/slurm_step_layout.c
	 * for details.
	 */
	for (i = 0; i < sl->node_cnt; i++) {
		task_cnt += sl->tasks[i];
	}
	if (task_cnt > 100000)
		return (xstrdup("Unknown"));

	hs = hostset_create(NULL);
	for (i = 0; i < ntasks; i++) {
		char *host = slurm_step_layout_host_name(sl, taskids[i]);
		if (host) {
			hostset_insert(hs, host);
			free(host);
		} else {
			error("Could not identify host name for task %u",
			      taskids[i]);
		}
	}

	hosts = _hostset_to_string(hs);
	hostset_destroy(hs);

	return (hosts);
}

/*
 * Convert an array of task IDs into a string.
 * RET: the string, caller must xfree() this value
 * NOTE: the taskids array is not necessarily in numeric order,
 *       so we use existing bitmap functions to format
 */
static char *_task_array_to_string(int ntasks, uint32_t *taskids,
				   srun_job_t *my_srun_job)
{
	bitstr_t *tasks_bitmap = NULL;
	char *str;
	int i;

	tasks_bitmap = bit_alloc(my_srun_job->ntasks);
	if (!tasks_bitmap) {
		error("bit_alloc: memory allocation failure");
		exit(error_exit);
	}
	for (i = 0; i < ntasks; i++)
		bit_set(tasks_bitmap, taskids[i]);
	str = xmalloc(2048);
	bit_fmt(str, 2048, tasks_bitmap);
	FREE_NULL_BITMAP(tasks_bitmap);

	return str;
}

static void _update_task_exit_state(task_state_t *task_state, uint32_t ntasks,
				    uint32_t *taskids, int abnormal)
{
	int i;
	task_state_type_t t = abnormal ? TS_ABNORMAL_EXIT : TS_NORMAL_EXIT;

	for (i = 0; i < ntasks; i++)
		task_state_update(task_state, taskids[i], t);
}

static int _kill_on_bad_exit(void)
{
	xassert(opt_save->srun_opt);
	if (!opt_save || (opt_save->srun_opt->kill_bad_exit == NO_VAL))
		return slurm_conf.kill_on_bad_exit;
	return opt_save->srun_opt->kill_bad_exit;
}

static void _setup_max_wait_timer(void)
{
	xassert(opt_save->srun_opt);
	/*
	 *  If these are the first tasks to finish we need to
	 *   start a timer to kill off the job step if the other
	 *   tasks don't finish within opt_save->srun_opt->max_wait seconds.
	 */
	verbose("First task exited. Terminating job in %ds",
		opt_save->srun_opt->max_wait);
	srun_max_timer = true;
	alarm(opt_save->srun_opt->max_wait);
}

static const char *_taskstr(int n)
{
	if (n == 1)
		return "task";
	else
		return "tasks";
}

static int _is_openmpi_port_error(int errcode)
{
	if (errcode != OPEN_MPI_PORT_ERROR)
		return 0;
	if (opt_save && (opt_save->srun_opt->resv_port_cnt == NO_VAL))
		return 0;
	if (difftime(time(NULL), launch_start_time) > slurm_conf.msg_timeout)
		return 0;
	return 1;
}

static void
_handle_openmpi_port_error(const char *tasks, const char *hosts,
			   slurm_step_ctx_t *step_ctx)
{
	slurm_step_id_t step_id = step_ctx->step_req->step_id;
	char *msg = "retrying";

	if (!retry_step_begin) {
		retry_step_begin = true;
		retry_step_cnt++;
	}

	if (retry_step_cnt >= MAX_STEP_RETRIES)
		msg = "aborting";
	error("%s: tasks %s unable to claim reserved port, %s.",
	      hosts, tasks, msg);

	info("Terminating job step %ps", &step_id);
	slurm_kill_job_step(step_id.job_id, step_id.step_id, SIGKILL);
}

static char *_mpir_get_host_name(char *node_name)
{
	if ((xstrcasestr(slurm_conf.launch_params, "mpir_use_nodeaddr")))
		return slurm_conf_get_nodeaddr(node_name);

	return xstrdup(node_name);
}

static void _task_start(launch_tasks_response_msg_t *msg)
{
	MPIR_PROCDESC *table;
	uint32_t local_task_id, global_task_id;
	int i;
	task_state_t *task_state;

	if (msg->count_of_pids) {
		verbose("Node %s, %d tasks started",
			msg->node_name, msg->count_of_pids);
	} else {
		/*
		 * This message should be displayed through the API,
		 * hence it is a debug2() instead of error().
		 */
		debug2("No tasks started on node %s: %s",
		       msg->node_name, slurm_strerror(msg->return_code));
	}

	task_state = task_state_find(&msg->step_id, task_state_list);
	if (!task_state) {
		error("%s: Could not locate task state for %ps",
		      __func__, &msg->step_id);
	}
	for (i = 0; i < msg->count_of_pids; i++) {
		local_task_id = msg->task_ids[i];
		global_task_id = task_state_global_id(task_state,local_task_id);
		if (global_task_id >= MPIR_proctable_size) {
			error("%s: task_id too large (%u >= %d)", __func__,
			      global_task_id, MPIR_proctable_size);
			continue;
		}
		table = &MPIR_proctable[global_task_id];
		table->host_name = _mpir_get_host_name(msg->node_name);
		/* table->executable_name set in mpir_set_executable_names() */
		table->pid = msg->local_pids[i];
		if (!task_state) {
			error("%s: Could not update task state for task ID %u",
			      __func__, global_task_id);
		} else if (msg->return_code == 0) {
			task_state_update(task_state, local_task_id,
					  TS_START_SUCCESS);
		} else {
			task_state_update(task_state, local_task_id,
					  TS_START_FAILURE);

		}
	}

}

static int _find_step(void *object, void *key)
{
	srun_job_t *srun_job = (srun_job_t *)object;
	slurm_step_id_t *step_id = (slurm_step_id_t *)key;

	return verify_step_id(&srun_job->step_id, step_id);
}

/*
 * Find the task_state structure for a given job_id, step_id and/or het group
 * on a list. Specify values of NO_VAL for values that are not to be matched
 * Returns NULL if not found
 */
static srun_job_t *_find_srun_job(slurm_step_id_t *step_id)
{
	if (!local_job_list)
		return NULL;

	return list_find_first(local_job_list, _find_step, step_id);
}

static void _task_finish(task_exit_msg_t *msg)
{
	char *tasks = NULL, *hosts = NULL;
	bool build_task_string = false;
	uint32_t rc = 0;
	int normal_exit = 0;
	static int reduce_task_exit_msg = -1;
	static int msg_printed = 0, oom_printed = 0, last_task_exit_rc;
	task_state_t *task_state;
	const char *task_str = _taskstr(msg->num_tasks);
	srun_job_t *my_srun_job = _find_srun_job(&msg->step_id);

	if (!my_srun_job) {
		error("Ignoring exit message from unrecognized %ps",
		      &msg->step_id);
		return;
	}

	if (reduce_task_exit_msg == -1) {
		char *ptr = getenv("SLURM_SRUN_REDUCE_TASK_EXIT_MSG");
		if (ptr && atoi(ptr) != 0)
			reduce_task_exit_msg = 1;
		else
			reduce_task_exit_msg = 0;
	}

	verbose("Received task exit notification for %d %s of %ps (status=0x%04x).",
		msg->num_tasks, task_str, &msg->step_id, msg->return_code);

	/*
	 * Only build the "tasks" and "hosts" strings as needed.
	 * Building them can take multiple milliseconds
	 */
	if (((msg->return_code & 0xff) == SIG_OOM) && !oom_printed) {
		build_task_string = true;
	} else if (WIFEXITED(msg->return_code)) {
		if ((rc = WEXITSTATUS(msg->return_code)) == 0) {
			if (get_log_level() >= LOG_LEVEL_VERBOSE)
				build_task_string = true;
		} else {
			build_task_string = true;
		}

	} else if (WIFSIGNALED(msg->return_code)) {
		if (my_srun_job->state >= SRUN_JOB_CANCELLED) {
			if (get_log_level() >= LOG_LEVEL_VERBOSE)
				build_task_string = true;
		} else {
			build_task_string = true;
		}
	}
	if (build_task_string) {
		tasks = _task_array_to_string(msg->num_tasks,
					      msg->task_id_list, my_srun_job);
		hosts = _task_ids_to_host_list(msg->num_tasks,
					       msg->task_id_list, my_srun_job);
	}

	slurm_mutex_lock(&launch_lock);
	if ((msg->return_code & 0xff) == SIG_OOM) {
		if (!oom_printed)
			error("%s: %s %s: Out Of Memory", hosts, task_str,
			     tasks);
		oom_printed = 1;
		*local_global_rc = msg->return_code;
	} else if (WIFEXITED(msg->return_code)) {
		if ((rc = WEXITSTATUS(msg->return_code)) == 0) {
			verbose("%s: %s %s: Completed", hosts, task_str, tasks);
			normal_exit = 1;
		} else if (_is_openmpi_port_error(rc)) {
			_handle_openmpi_port_error(tasks, hosts,
						   my_srun_job->step_ctx);
		} else if ((reduce_task_exit_msg == 0) ||
			   (msg_printed == 0) ||
			   (msg->return_code != last_task_exit_rc)) {
			error("%s: %s %s: Exited with exit code %d",
			      hosts, task_str, tasks, rc);
			msg_printed = 1;
		}
		if (((*local_global_rc & 0xff) != SIG_OOM) &&
		    (!WIFSIGNALED(*local_global_rc)) &&
		    (!WIFEXITED(*local_global_rc) ||
		     (rc > WEXITSTATUS(*local_global_rc))))
			*local_global_rc = msg->return_code;
	} else if (WIFSIGNALED(msg->return_code)) {
		const char *signal_str = strsignal(WTERMSIG(msg->return_code));
		char *core_str = "";
#ifdef WCOREDUMP
		if (WCOREDUMP(msg->return_code))
			core_str = " (core dumped)";
#endif
		if (my_srun_job->state >= SRUN_JOB_CANCELLED) {
			verbose("%s: %s %s: %s%s",
				hosts, task_str, tasks, signal_str, core_str);
		} else if ((reduce_task_exit_msg == 0) ||
			   (msg_printed == 0) ||
			   (msg->return_code != last_task_exit_rc)) {
			error("%s: %s %s: %s%s",
			      hosts, task_str, tasks, signal_str, core_str);
			msg_printed = 1;
		}
		/*
		 * Even though lower numbered signals can be stronger than
		 * higher numbered signals, keep the highest signal so that it's
		 * predicatable to the user.
		 */
		rc = WTERMSIG(msg->return_code);
		if (((*local_global_rc & 0xff) != SIG_OOM) &&
		    (!WIFSIGNALED(*local_global_rc) ||
		     (rc > WTERMSIG(*local_global_rc))))
			*local_global_rc = msg->return_code;
	}
	xfree(tasks);
	xfree(hosts);

	task_state = task_state_find(&msg->step_id, task_state_list);
	if (task_state) {
		_update_task_exit_state(task_state, msg->num_tasks,
					msg->task_id_list, !normal_exit);
	} else {
		error("%s: Could not find task state for %ps", __func__,
		      &msg->step_id);
	}

	if (task_state_first_abnormal_exit(task_state_list) &&
	    _kill_on_bad_exit())
		(void) _step_signal(SIG_TERM_KILL);

	if (task_state_first_exit(task_state_list) && opt_save &&
	    (opt_save->srun_opt->max_wait > 0))
		_setup_max_wait_timer();

	last_task_exit_rc = msg->return_code;
	slurm_mutex_unlock(&launch_lock);
}

/*
 * Load the multi_prog config file into argv, pass the  entire file contents
 * in order to avoid having to read the file on every node. We could parse
 * the infomration here too for loading the MPIR records for TotalView
 */
static void _load_multi(int *argc, char **argv)
{
	int config_fd, data_read = 0, i;
	struct stat stat_buf;
	char *data_buf;

	if ((config_fd = open(argv[0], O_RDONLY)) == -1) {
		error("Could not open multi_prog config file %s",
		      argv[0]);
		exit(error_exit);
	}
	if (fstat(config_fd, &stat_buf) == -1) {
		error("Could not stat multi_prog config file %s",
		      argv[0]);
		exit(error_exit);
	}
	if (stat_buf.st_size > 60000) {
		error("Multi_prog config file %s is too large",
		      argv[0]);
		exit(error_exit);
	}
	data_buf = xmalloc(stat_buf.st_size + 1);
	while ((i = read(config_fd, &data_buf[data_read], stat_buf.st_size
			 - data_read)) != 0) {
		if (i < 0) {
			error("Error reading multi_prog config file %s",
			      argv[0]);
			exit(error_exit);
		} else
			data_read += i;
	}
	close(config_fd);

	for (i = *argc+1; i > 1; i--)
		argv[i] = argv[i-1];
	argv[1] = data_buf;
	*argc += 1;
}


static int
_is_local_file (fname_t *fname)
{
	if (fname->name == NULL)
		return 1;

	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

static char **_build_user_env(srun_job_t *job, slurm_opt_t *opt_local)
{
	char **dest_array = NULL;
	char *tmp_env, *tok, *save_ptr = NULL, *eq_ptr, *value;
	bool all;

	if (!opt_local->export_env) {
		all = true;
	} else {
		all = false;
		tmp_env = xstrdup(opt_local->export_env);
		tok = find_quote_token(tmp_env, ",", &save_ptr);
		while (tok) {
			if (xstrcasecmp(tok, "ALL") == 0)
				all = true;

			if (!xstrcasecmp(tok, "NONE"))
				break;
			eq_ptr = strchr(tok, '=');
			if (eq_ptr) {
				eq_ptr[0] = '\0';
				value = eq_ptr + 1;
				env_array_overwrite(&dest_array, tok, value);
			} else {
				value = getenv(tok);
				if (value) {
					env_array_overwrite(&dest_array, tok,
							    value);
				}
			}
			tok = find_quote_token(NULL, ",", &save_ptr);
		}
		xfree(tmp_env);
	}

	if (!job->env)
		fatal("%s: job env is NULL", __func__);
	else if (all)
		env_array_merge(&dest_array, (const char **) job->env);
	else
		env_array_merge_slurm(&dest_array, (const char **) job->env);

	return dest_array;
}

static void _task_state_del(void *x)
{
	task_state_t *task_state = (task_state_t *)x;

	task_state_destroy(task_state);
}

/*
 * Return only after all hetjob components reach this point (or timeout)
 */
static void _wait_all_het_job_comps_started(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	static int start_cnt = 0;
	static int total_cnt = -1;
	struct timeval  now;
	struct timespec timeout;
	int rc;
	xassert(srun_opt);

	slurm_mutex_lock(&start_mutex);
	if (total_cnt == -1)
		total_cnt = srun_opt->het_step_cnt;
	start_cnt++;
	while (start_cnt < total_cnt) {
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + 10;	/* 10 sec delay max */
		timeout.tv_nsec = now.tv_usec * 1000;
		rc = pthread_cond_timedwait(&start_cond, &start_mutex,
					    &timeout);
		if (rc == ETIMEDOUT)
			break;
	}
	slurm_cond_broadcast(&start_cond);
	slurm_mutex_unlock(&start_mutex);
}

/*
 * Initialize context for plugin
 */
extern int launch_init(void)
{
	int retval = SLURM_SUCCESS;

	return retval;
}

extern int location_fini(void)
{
	FREE_NULL_LIST(task_state_list);

	return SLURM_SUCCESS;
}

extern slurm_step_layout_t *launch_common_get_slurm_step_layout(srun_job_t *job)
{
	return (!job || !job->step_ctx) ?
		NULL : job->step_ctx->step_resp->step_layout;
}

static job_step_create_request_msg_t *_create_job_step_create_request(
	slurm_opt_t *opt_local, bool use_all_cpus, srun_job_t *job)
{
	char *add_tres = NULL;
	srun_opt_t *srun_opt = opt_local->srun_opt;
	job_step_create_request_msg_t *step_req = xmalloc(sizeof(*step_req));
	List tmp_gres_list = NULL;
	int rc;

	xassert(srun_opt);

	step_req->host = xshort_hostname();
	step_req->cpu_freq_min = opt_local->cpu_freq_min;
	step_req->cpu_freq_max = opt_local->cpu_freq_max;
	step_req->cpu_freq_gov = opt_local->cpu_freq_gov;

	if (opt_local->cpus_per_gpu) {
		xstrfmtcat(step_req->cpus_per_tres, "gres:gpu:%d",
			   opt_local->cpus_per_gpu);
	}

	step_req->exc_nodes = xstrdup(opt_local->exclude);
	step_req->features = xstrdup(opt_local->constraint);

	if (srun_opt->exclusive)
		step_req->flags |= SSF_EXCLUSIVE;
	if (srun_opt->overlap_force)
		step_req->flags |= SSF_OVERLAP_FORCE;
	if (opt_local->overcommit)
		step_req->flags |= SSF_OVERCOMMIT;
	if (opt_local->no_kill)
		step_req->flags |= SSF_NO_KILL;
	if (srun_opt->interactive) {
		debug("interactive step launch request");
		step_req->flags |= SSF_INTERACTIVE;
	}

	if (opt_local->immediate == 1)
		step_req->immediate = opt_local->immediate;

	step_req->max_nodes = job->nhosts;
	if (opt_local->max_nodes &&
	    (opt_local->max_nodes < step_req->max_nodes))
		step_req->max_nodes = opt_local->max_nodes;

	if (opt_local->mem_per_gpu != NO_VAL64)
		xstrfmtcat(step_req->mem_per_tres, "gres:gpu:%"PRIu64,
			   opt.mem_per_gpu);

	step_req->min_nodes = job->nhosts;
	if (opt_local->min_nodes &&
	    (opt_local->min_nodes < step_req->min_nodes))
		step_req->min_nodes = opt_local->min_nodes;

	/*
	 * If the number of CPUs was specified (cpus_set==true), then we need to
	 * set exact = true. Otherwise the step will be allocated the wrong
	 * number of CPUs (and therefore the wrong amount memory if using
	 * mem_per_cpu).
	 */
	if (opt_local->overcommit) {
		if (use_all_cpus)	/* job allocation created by srun */
			step_req->cpu_count = job->cpu_count;
		else
			step_req->cpu_count = step_req->min_nodes;
	} else if (opt_local->cpus_set) {
		step_req->cpu_count =
			opt_local->ntasks * opt_local->cpus_per_task;
		if (srun_opt->whole)
			info("Ignoring --whole since -c/--cpus-per-task used");
		else if (!srun_opt->exact)
			verbose("Implicitly setting --exact, because -c/--cpus-per-task given.");
		srun_opt->exact = true;
	} else if (opt_local->gpus_per_task && opt_local->cpus_per_gpu) {
		char *save_ptr = NULL, *tmp_str, *tok, *sep;
		int gpus_per_task = 0;

		tmp_str = xstrdup(opt_local->gpus_per_task);

		tok = strtok_r(tmp_str, ",", &save_ptr);
		while (tok) {
			int tmp = 0;
			sep = xstrchr(tok, ':');
			if (sep)
				tmp += atoi(sep + 1);
			else
				tmp += atoi(tok);
			if (tmp > 0)
				gpus_per_task += tmp;
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
		step_req->cpu_count = opt_local->ntasks * gpus_per_task *
				      opt_local->cpus_per_gpu;
	} else if (opt_local->ntasks_set ||
		   (opt_local->ntasks_per_tres != NO_VAL) ||
		   (opt_local->ntasks_per_gpu != NO_VAL)) {
		step_req->cpu_count = opt_local->ntasks;
	} else if (use_all_cpus) {	/* job allocation created by srun */
		step_req->cpu_count = job->cpu_count;
	} else {
		step_req->cpu_count = opt_local->ntasks;
	}

	if (slurm_option_set_by_cli(opt_local, 'J'))
		step_req->name = opt_local->job_name;
	else if (srun_opt->cmd_name)
		step_req->name = srun_opt->cmd_name;
	else
		step_req->name = sropt.cmd_name;

	step_req->network = xstrdup(opt_local->network);
	step_req->node_list = xstrdup(opt_local->nodelist);

	if (opt_local->ntasks_per_tres != NO_VAL)
		step_req->ntasks_per_tres = opt_local->ntasks_per_tres;
	else if (opt_local->ntasks_per_gpu != NO_VAL)
		step_req->ntasks_per_tres = opt_local->ntasks_per_gpu;
	else
		step_req->ntasks_per_tres = NO_VAL16;

	step_req->num_tasks = opt_local->ntasks;

	if (opt_local->ntasks_per_core != NO_VAL)
		step_req->ntasks_per_core = opt_local->ntasks_per_core;
	else
		step_req->ntasks_per_core = INFINITE16;

	if (opt_local->mem_per_cpu != NO_VAL64)
		step_req->pn_min_memory = opt_local->mem_per_cpu | MEM_PER_CPU;
	else if (opt_local->pn_min_memory != NO_VAL64)
		step_req->pn_min_memory = opt_local->pn_min_memory;

	step_req->relative = srun_opt->relative;

	if (srun_opt->resv_port_cnt != NO_VAL) {
		step_req->resv_port_cnt = srun_opt->resv_port_cnt;
	} else {
#if defined(HAVE_NATIVE_CRAY)
		/*
		 * On Cray systems default to reserving one port, or one
		 * more than the number of multi prog commands, for Cray PMI
		 */
		step_req->resv_port_cnt = (srun_opt->multi_prog ?
					   srun_opt->multi_prog_cmds + 1 : 1);
#else
		step_req->resv_port_cnt = NO_VAL16;
#endif
	}

	step_req->srun_pid = (uint32_t) getpid();
	step_req->step_het_comp_cnt = opt_local->step_het_comp_cnt;
	step_req->step_het_grps = xstrdup(opt_local->step_het_grps);
	memcpy(&step_req->step_id, &job->step_id, sizeof(step_req->step_id));
	step_req->array_task_id = srun_opt->array_task_id;

	step_req->submit_line = xstrdup(opt_local->submit_line);

	if (opt_local->threads_per_core != NO_VAL) {
		step_req->threads_per_core = opt.threads_per_core;
	} else
		step_req->threads_per_core = NO_VAL16;

	/*
	 * FIXME: tres_bind is really gres_bind. This should be fixed in the
	 * future.
	 */

	if (!opt_local->tres_bind &&
	    ((opt_local->ntasks_per_tres != NO_VAL) ||
	     (opt_local->ntasks_per_gpu != NO_VAL))) {
		/* Implicit single GPU binding with ntasks-per-tres/gpu */
		if (opt_local->ntasks_per_tres != NO_VAL)
			xstrfmtcat(opt_local->tres_bind, "gpu:single:%d",
				   opt_local->ntasks_per_tres);
		else
			xstrfmtcat(opt_local->tres_bind, "gpu:single:%d",
				   opt_local->ntasks_per_gpu);
	}

	/*
	 * FIXME: tres_per_task Should be handled in src/common/slurm_opt.c
	 * _validate_tres_per_task(). But we should probably revisit this to get
	 * rid of gpus_per_task completely.
	 */
	if (!opt_local->tres_bind && opt_local->gpus_per_task) {
		/* Implicit GPU binding with gpus_per_task */
		xstrfmtcat(opt_local->tres_bind, "gpu:per_task:%s",
			   opt_local->gpus_per_task);
	}

	step_req->tres_bind = xstrdup(opt_local->tres_bind);
	step_req->tres_freq = xstrdup(opt_local->tres_freq);

	xstrfmtcat(step_req->tres_per_step, "%scpu:%u",
		   step_req->tres_per_step ? "," : "",
		   step_req->cpu_count);
	xfmt_tres(&step_req->tres_per_step, "gres:gpu", opt_local->gpus);

	xfmt_tres(&step_req->tres_per_node, "gres:gpu",
		  opt_local->gpus_per_node);
	if (opt_local->gres)
		add_tres = opt_local->gres;
	else
		add_tres = getenv("SLURM_STEP_GRES");
	if (add_tres) {
		if (step_req->tres_per_node) {
			xstrfmtcat(step_req->tres_per_node, ",%s", add_tres);
		} else
			step_req->tres_per_node = xstrdup(add_tres);
	}

	xfmt_tres(&step_req->tres_per_socket, "gres:gpu",
		  opt_local->gpus_per_socket);

	if (opt_local->cpus_set)
		xstrfmtcat(step_req->tres_per_task, "%scpu:%u",
			   step_req->tres_per_task ? "," : "",
			   opt_local->cpus_per_task);
	xfmt_tres(&step_req->tres_per_task, "gres:gpu",
		  opt_local->gpus_per_task);

	if (opt_local->time_limit != NO_VAL)
		step_req->time_limit = opt_local->time_limit;

	step_req->user_id = opt_local->uid;

	step_req->container = xstrdup(opt_local->container);
	xfree(step_req->container_id);
	step_req->container_id = xstrdup(opt_local->container_id);

	rc = gres_step_state_validate(step_req->cpus_per_tres,
				     step_req->tres_per_step,
				     step_req->tres_per_node,
				     step_req->tres_per_socket,
				     step_req->tres_per_task,
				     step_req->mem_per_tres,
				     step_req->ntasks_per_tres,
				     step_req->min_nodes,
				     &tmp_gres_list,
				     NULL, job->step_id.job_id,
				     NO_VAL, &step_req->num_tasks,
				     &step_req->cpu_count, NULL);
	FREE_NULL_LIST(tmp_gres_list);
	if (rc) {
		error("%s", slurm_strerror(rc));
		return NULL;
	}

	step_req->plane_size = NO_VAL16;

	switch (opt_local->distribution & SLURM_DIST_NODESOCKMASK) {
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_CFULL:
	case SLURM_DIST_BLOCK_CFULL:
		step_req->task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			step_req->plane_size = opt_local->ntasks_per_node;
		break;
	case SLURM_DIST_PLANE:
		step_req->task_dist = SLURM_DIST_PLANE;
		step_req->plane_size = opt_local->plane_size;
		break;
	default:
	{
		uint16_t base_dist;
		/* Leave distribution set to unknown if taskcount <= nodes and
		 * memory is set to 0. step_mgr will handle the mem=0 case. */
		if (!opt_local->mem_per_cpu || !opt_local->pn_min_memory ||
		    srun_opt->interactive)
			base_dist = SLURM_DIST_UNKNOWN;
		else
			base_dist = (step_req->num_tasks <=
				     step_req->min_nodes) ?
				SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		opt_local->distribution &= SLURM_DIST_STATE_FLAGS;
		opt_local->distribution |= base_dist;
		step_req->task_dist = opt_local->distribution;
		if (opt_local->ntasks_per_node != NO_VAL)
			step_req->plane_size = opt_local->ntasks_per_node;
		break;
	}
	}

	/*
	 * This must be handled *after* we potentially set srun_opt->exact
	 * above.
	 */
	if (!srun_opt->exact)
		step_req->flags |= SSF_WHOLE;

	return step_req;
}

extern void launch_common_set_stdio_fds(srun_job_t *job,
					slurm_step_io_fds_t *cio_fds,
					slurm_opt_t *opt_local)
{
	bool err_shares_out = false;
	int file_flags;

	if (opt_local->open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (opt_local->open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_conf_t *conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}

	/*
	 * create stdin file descriptor
	 */
	if (_is_local_file(job->ifname)) {
		if ((job->ifname->name == NULL) ||
		    (job->ifname->taskid != -1)) {
			cio_fds->input.fd = STDIN_FILENO;
		} else {
			cio_fds->input.fd = open(job->ifname->name, O_RDONLY);
			if (cio_fds->input.fd == -1) {
				error("Could not open stdin file: %m");
				exit(error_exit);
			}
		}
		if (job->ifname->type == IO_ONE) {
			cio_fds->input.taskid = job->ifname->taskid;
			cio_fds->input.nodeid = slurm_step_layout_host_id(
				launch_common_get_slurm_step_layout(job),
				job->ifname->taskid);
		}
	}

	/*
	 * create stdout file descriptor
	 */
	if (_is_local_file(job->ofname)) {
		if ((job->ofname->name == NULL) ||
		    (job->ofname->taskid != -1)) {
			cio_fds->out.fd = STDOUT_FILENO;
		} else {
			cio_fds->out.fd = open(job->ofname->name,
					       file_flags, 0644);
			if (errno == ENOENT) {
				mkdirpath(job->ofname->name, 0755, false);
				cio_fds->out.fd = open(job->ofname->name,
						       file_flags, 0644);
			}
			if (cio_fds->out.fd == -1) {
				error("Could not open stdout file: %m");
				exit(error_exit);
			}
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !xstrcmp(job->ofname->name, job->efname->name)) {
			err_shares_out = true;
		}
	}

	/*
	 * create separate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
		cio_fds->err.taskid = cio_fds->out.taskid;
	} else if (_is_local_file(job->efname)) {
		if ((job->efname->name == NULL) ||
		    (job->efname->taskid != -1)) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(job->efname->name,
					       file_flags, 0644);
			if (errno == ENOENT) {
				mkdirpath(job->efname->name, 0755, false);
				cio_fds->err.fd = open(job->efname->name,
						       file_flags, 0644);
			}
			if (cio_fds->err.fd == -1) {
				error("Could not open stderr file: %m");
				exit(error_exit);
			}
		}
	}
}

/*
 * Return TRUE if the job step create request should be retried later
 * (i.e. the errno set by step_ctx_create_timeout() is recoverable).
 */
extern bool launch_common_step_retry_errno(int rc)
{
	if ((rc == EAGAIN) ||
	    (rc == ESLURM_DISABLED) ||
	    (rc == ESLURM_INTERCONNECT_BUSY) ||
	    (rc == ESLURM_NODES_BUSY) ||
	    (rc == ESLURM_PORTS_BUSY) ||
	    (rc == SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT))
		return true;
	return false;
}

extern int launch_g_setup_srun_opt(char **rest, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);
	if (srun_opt->debugger_test)
		MPIR_being_debugged = 1;

	/*
	 * We need to do +2 here just in case multi-prog is needed
	 * (we add an extra argv on so just make space for it).
	 */
	opt_local->argv = xcalloc((opt_local->argc + 2), sizeof(char *));

	return SLURM_SUCCESS;
}

extern int launch_g_handle_multi_prog_verify(int command_pos,
					     slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (srun_opt->multi_prog) {
		if (opt_local->argc < 1) {
			error("configuration file not specified");
			exit(error_exit);
		}
		_load_multi(&opt_local->argc, opt_local->argv);
		if (verify_multi_name(opt_local->argv[command_pos], opt_local))
			exit(error_exit);
		return 1;
	} else
		return 0;
}

extern int launch_g_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	int i, j, rc;
	unsigned long step_wait = 0;
	uint16_t slurmctld_timeout;
	slurm_step_layout_t *step_layout;
	job_step_create_request_msg_t *step_req;

	xassert(srun_opt);

	if (!job) {
		error("launch_common_create_job_step: no job given");
		return SLURM_ERROR;
	}

	/* Validate minimum and maximum node counts */
	if (opt_local->min_nodes && opt_local->max_nodes &&
	    (opt_local->min_nodes > opt_local->max_nodes)) {
		error ("Minimum node count > maximum node count (%d > %d)",
		       opt_local->min_nodes, opt_local->max_nodes);
		return SLURM_ERROR;
	}
#if !defined HAVE_FRONT_END
	if (opt_local->min_nodes && (opt_local->min_nodes > job->nhosts)) {
		error ("Minimum node count > allocated node count (%d > %d)",
		       opt_local->min_nodes, job->nhosts);
		return SLURM_ERROR;
	}
#endif

	step_req = _create_job_step_create_request(
		opt_local, use_all_cpus, job);

	if (!step_req)
		return SLURM_ERROR;

	if (step_req->array_task_id != NO_VAL)
		debug("requesting job %u_%u, user %u, nodes %u including (%s)",
		      step_req->step_id.job_id, step_req->array_task_id,
		      step_req->user_id, step_req->min_nodes,
		      step_req->node_list);
	else
		debug("requesting job %u, user %u, nodes %u including (%s)",
		      step_req->step_id.job_id, step_req->user_id,
		      step_req->min_nodes, step_req->node_list);
	debug("cpus %u, tasks %u, name %s, relative %u",
	      step_req->cpu_count, step_req->num_tasks,
	      step_req->name, step_req->relative);

	for (i = 0; (!(*destroy_job)); i++) {
		if (srun_opt->no_alloc) {
			job->step_ctx = step_ctx_create_no_alloc(
				step_req, job->step_id.step_id);
		} else {
			if (opt_local->immediate) {
				step_wait = MAX(1, opt_local->immediate -
						   difftime(time(NULL),
							    srun_begin_time)) *
					    1000;
			} else {
				slurmctld_timeout = MIN(300, MAX(60,
					slurm_conf.slurmctld_timeout));
				step_wait = ((getpid() % 10) +
					     slurmctld_timeout) * 1000;
			}
			job->step_ctx = step_ctx_create_timeout(
						step_req, step_wait);
		}
		if (job->step_ctx != NULL) {
			job->step_ctx->verbose_level = opt_local->verbose;
			if (i > 0) {
				info("Step created for %ps",
				     &step_req->step_id);
			}
			break;
		}
		rc = slurm_get_errno();

		if (((opt_local->immediate != 0) &&
		     ((opt_local->immediate == 1) ||
		      (difftime(time(NULL), srun_begin_time) >=
		       opt_local->immediate))) ||
		    ((rc != ESLURM_PROLOG_RUNNING) &&
		     !launch_common_step_retry_errno(rc))) {
			error("Unable to create step for job %u: %m",
			      step_req->step_id.job_id);
			slurm_free_job_step_create_request_msg(step_req);
			return SLURM_ERROR;
		}

		if (i == 0) {
			if (rc == ESLURM_PROLOG_RUNNING) {
				verbose("Resources allocated for job %u and "
					"being configured, please wait",
					step_req->step_id.job_id);
			} else {
				info("Job %u step creation temporarily disabled, retrying (%s)",
				     step_req->step_id.job_id,
				     slurm_strerror(rc));
			}
			xsignal_unblock(sig_array);
			for (j = 0; sig_array[j]; j++)
				xsignal(sig_array[j], signal_function);
		} else {
			if (rc == ESLURM_PROLOG_RUNNING)
				verbose("Job %u step creation still disabled, retrying (%s)",
					step_req->step_id.job_id,
					slurm_strerror(rc));
			else
				info("Job %u step creation still disabled, retrying (%s)",
				     step_req->step_id.job_id,
				     slurm_strerror(rc));
		}

		if (*destroy_job) {
			/* cancelled by signal */
			break;
		}
	}
	if (i > 0) {
		xsignal_block(sig_array);
		if (*destroy_job) {
			info("Cancelled pending step for job %u",
			     step_req->step_id.job_id);
			slurm_free_job_step_create_request_msg(step_req);
			return SLURM_ERROR;
		}
	}

	job->step_id.job_id = step_req->step_id.job_id;
	job->step_id.step_id = step_req->step_id.step_id;

	step_layout = launch_common_get_slurm_step_layout(job);
	if (!step_layout) {
		info("No step_layout given for pending step for job %u",
		     step_req->step_id.job_id);
		slurm_free_job_step_create_request_msg(step_req);
		return SLURM_ERROR;
	}

	if (job->ntasks != step_layout->task_cnt)
		job->ntasks = step_layout->task_cnt;

	/*
	 *  Number of hosts in job may not have been initialized yet if
	 *    --jobid was used or only SLURM_JOB_ID was set in user env.
	 *    Reset the value here just in case.
	 */
	job->nhosts = step_layout->node_cnt;

	/*
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job, opt_local);

	/* set the jobid for totalview */
	if (!totalview_jobid) {
		xstrfmtcat(totalview_jobid,  "%u", job->step_id.job_id);
		xstrfmtcat(totalview_stepid, "%u", job->step_id.step_id);
	}

	return SLURM_SUCCESS;
}

extern int launch_g_step_launch(srun_job_t *job, slurm_step_io_fds_t *cio_fds,
				uint32_t *global_rc,
				slurm_step_launch_callbacks_t *step_callbacks,
				slurm_opt_t *opt_local)
{
	srun_job_t *local_srun_job;
	srun_opt_t *srun_opt = opt_local->srun_opt;
	slurm_step_launch_params_t launch_params;
	slurm_step_launch_callbacks_t callbacks;
	int rc = SLURM_SUCCESS;
	task_state_t *task_state;
	bool first_launch = false;
	char tmp_str[128];
	xassert(srun_opt);

	slurm_step_launch_params_t_init(&launch_params);
	memcpy(&callbacks, step_callbacks, sizeof(callbacks));

	task_state = task_state_find(&job->step_id, task_state_list);

	if (!task_state) {
		task_state = task_state_create(&job->step_id, job->ntasks,
					       job->het_job_task_offset);
		slurm_mutex_lock(&het_job_lock);
		if (!local_job_list)
			local_job_list = list_create(NULL);
		if (!task_state_list)
			task_state_list = list_create(_task_state_del);
		slurm_mutex_unlock(&het_job_lock);
		local_srun_job = job;
		local_global_rc = global_rc;
		list_append(local_job_list, local_srun_job);
		list_append(task_state_list, task_state);
		first_launch = true;
	} else {
		/* Launching extra POE tasks */
		task_state_alter(task_state, job->ntasks);
	}

	launch_params.alias_list = job->alias_list;
	launch_params.argc = opt_local->argc;
	launch_params.argv = opt_local->argv;
	launch_params.multi_prog = srun_opt->multi_prog ? true : false;
	launch_params.container = xstrdup(opt_local->container);
	launch_params.cwd = opt_local->chdir;
	launch_params.slurmd_debug = srun_opt->slurmd_debug;
	launch_params.buffered_stdio = !srun_opt->unbuffered;
	launch_params.labelio = srun_opt->labelio ? true : false;
	launch_params.remote_output_filename = fname_remote_string(job->ofname);
	launch_params.remote_input_filename  = fname_remote_string(job->ifname);
	launch_params.remote_error_filename  = fname_remote_string(job->efname);
	launch_params.het_job_node_offset = job->het_job_node_offset;
	launch_params.het_job_id  = job->het_job_id;
	launch_params.het_job_nnodes = job->het_job_nnodes;
	launch_params.het_job_ntasks = job->het_job_ntasks;
	launch_params.het_job_offset = job->het_job_offset;
	launch_params.het_job_step_cnt = srun_opt->het_step_cnt;
	launch_params.het_job_task_offset = job->het_job_task_offset;
	launch_params.het_job_task_cnts = job->het_job_task_cnts;
	launch_params.het_job_tids = job->het_job_tids;
	launch_params.het_job_tid_offsets = job->het_job_tid_offsets;
	launch_params.het_job_node_list = job->het_job_node_list;
	launch_params.partition = job->partition;
	launch_params.profile = opt_local->profile;
	launch_params.task_prolog = srun_opt->task_prolog;
	launch_params.task_epilog = srun_opt->task_epilog;

	if (!srun_opt->cpu_bind_type &&
	    job->step_ctx->step_resp->def_cpu_bind_type)
		srun_opt->cpu_bind_type =
			job->step_ctx->step_resp->def_cpu_bind_type;
	if (get_log_level() >= LOG_LEVEL_VERBOSE) {
		slurm_sprint_cpu_bind_type(tmp_str, srun_opt->cpu_bind_type);
		verbose("CpuBindType=%s", tmp_str);
	}
	launch_params.cpu_bind = srun_opt->cpu_bind;
	launch_params.cpu_bind_type = srun_opt->cpu_bind_type;

	launch_params.mem_bind = opt_local->mem_bind;
	launch_params.mem_bind_type = opt_local->mem_bind_type;
	launch_params.accel_bind_type = srun_opt->accel_bind_type;
	launch_params.open_mode = opt_local->open_mode;
	if (opt_local->acctg_freq)
		launch_params.acctg_freq = opt_local->acctg_freq;
	launch_params.pty = srun_opt->pty;
	if (opt_local->cpus_set)
		launch_params.cpus_per_task	= opt_local->cpus_per_task;
	else
		launch_params.cpus_per_task	= 1;
	launch_params.threads_per_core   = opt_local->threads_per_core;
	launch_params.cpu_freq_min       = opt_local->cpu_freq_min;
	launch_params.cpu_freq_max       = opt_local->cpu_freq_max;
	launch_params.cpu_freq_gov       = opt_local->cpu_freq_gov;
	launch_params.tres_bind          = opt_local->tres_bind;
	launch_params.tres_freq          = opt_local->tres_freq;
	launch_params.task_dist          = opt_local->distribution;
	launch_params.preserve_env       = srun_opt->preserve_env;
	launch_params.spank_job_env      = opt_local->spank_job_env;
	launch_params.spank_job_env_size = opt_local->spank_job_env_size;
	launch_params.ntasks_per_board   = job->ntasks_per_board;
	launch_params.ntasks_per_core    = job->ntasks_per_core;
	launch_params.ntasks_per_tres    = job->ntasks_per_tres;
	launch_params.ntasks_per_socket  = job->ntasks_per_socket;
	launch_params.no_alloc           = srun_opt->no_alloc;
	launch_params.mpi_plugin_name = srun_opt->mpi_type;
	launch_params.env = _build_user_env(job, opt_local);

	memcpy(&launch_params.local_fds, cio_fds, sizeof(slurm_step_io_fds_t));

	if (MPIR_being_debugged) {
		launch_params.parallel_debug = true;
		pmi_server_max_threads(1);
	} else {
		launch_params.parallel_debug = false;
	}
	/*
	 * Normally this isn't used, but if an outside process (other
	 * than srun (poe) is using this logic to launch tasks then we
	 * can use this to signal the step.
	 */
	callbacks.task_start = _task_start;
	/*
	 * If poe is using this code with multi-prog it always returns
	 * 1 for each task which could be confusing since no real
	 * error happened.
	 */
	if (!launch_params.multi_prog
	    || (!callbacks.step_signal
		|| (callbacks.step_signal == launch_g_fwd_signal))) {
		callbacks.task_finish = _task_finish;
		slurm_mutex_lock(&launch_lock);
		if (!opt_save) {
			/*
			 * Save opt_local parameters since _task_finish()
			 * will lack the values
			 */
			opt_save = xmalloc(sizeof(slurm_opt_t));
			memcpy(opt_save, opt_local, sizeof(slurm_opt_t));
			opt_save->srun_opt = xmalloc(sizeof(srun_opt_t));
			memcpy(opt_save->srun_opt, srun_opt,
			       sizeof(srun_opt_t));
		}
		slurm_mutex_unlock(&launch_lock);
	}

	update_job_state(job, SRUN_JOB_LAUNCHING);
	launch_start_time = time(NULL);
	if (first_launch) {
		if (slurm_step_launch(job->step_ctx, &launch_params,
				      &callbacks) != SLURM_SUCCESS) {
			rc = errno;
			*local_global_rc = errno;
			error("Application launch failed: %m");
			slurm_step_launch_abort(job->step_ctx);
			slurm_step_launch_wait_finish(job->step_ctx);
			goto cleanup;
		}
	} else {
		if (slurm_step_launch_add(job->step_ctx, job->step_ctx,
					  &launch_params, job->nodelist)
			!= SLURM_SUCCESS) {
			rc = errno;
			*local_global_rc = errno;
			error("Application launch add failed: %m");
			slurm_step_launch_abort(job->step_ctx);
			slurm_step_launch_wait_finish(job->step_ctx);
			goto cleanup;
		}
	}

	update_job_state(job, SRUN_JOB_STARTING);
	if (slurm_step_launch_wait_start(job->step_ctx) == SLURM_SUCCESS) {
		update_job_state(job, SRUN_JOB_RUNNING);
		/*
		 * Only set up MPIR structures if the step launched correctly
		 */
		if (srun_opt->multi_prog) {
			mpir_set_multi_name(job->ntasks,
					    launch_params.argv[0]);
		} else {
			mpir_set_executable_names(launch_params.argv[0],
						  job->het_job_task_offset,
						  job->ntasks);
		}

		_wait_all_het_job_comps_started(opt_local);
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		if (srun_opt->debugger_test)
			mpir_dump_proctable();
		else if (srun_opt->parallel_debug)
			MPIR_Breakpoint(job);
	} else {
		info("%ps aborted before step completely launched.",
		     &job->step_id);
	}

cleanup:
	return rc;
}

extern int launch_g_step_wait(srun_job_t *job, bool got_alloc,
			      slurm_opt_t *opt_local)
{
	int rc = 0;

	slurm_step_launch_wait_finish(job->step_ctx);
	if ((MPIR_being_debugged == 0) && retry_step_begin &&
	    (retry_step_cnt < MAX_STEP_RETRIES) &&
	     (job->het_job_id == NO_VAL)) {	/* Not hetjob step */
		retry_step_begin = false;
		step_ctx_destroy(job->step_ctx);
		if (got_alloc)
			rc = create_job_step(job, true, opt_local);
		else
			rc = create_job_step(job, false, opt_local);
		if (rc < 0)
			exit(error_exit);
		rc = -1;
	}
	return rc;
}

extern int launch_g_step_terminate(void)
{
	return _step_signal(SIGKILL);
}

extern void launch_g_print_status(void)
{
	task_state_print(task_state_list, (log_f)slurm_info);
}

extern void launch_g_fwd_signal(int signal)
{
	srun_job_t *my_srun_job;
	ListIterator iter;

	if (!local_job_list) {
		debug("%s: local_job_list does not exist yet", __func__);
		return;
	}

	iter = list_iterator_create(local_job_list);
	while ((my_srun_job = (srun_job_t *) list_next(iter))) {
		switch (signal) {
		case SIGKILL:
			slurm_step_launch_abort(my_srun_job->step_ctx);
			break;
		default:
			slurm_step_launch_fwd_signal(my_srun_job->step_ctx,
						     signal);
			break;
		}
	}
	list_iterator_destroy(iter);
}
