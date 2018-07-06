/*****************************************************************************\
 *  launch_slurm.c - Define job launch using slurm.
 *****************************************************************************
 *  Copyright (C) 2012-2017 SchedMD LLC
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

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/common/slurm_opt.h"
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_resource_info.h"
#include "src/api/pmi_server.h"
#include "src/srun/libsrun/allocate.h"
#include "src/srun/libsrun/fname.h"
#include "src/srun/libsrun/launch.h"
#include "src/srun/libsrun/multi_prog.h"

#include "src/plugins/launch/slurm/task_state.h"

#ifndef OPEN_MPI_PORT_ERROR
/* This exit code indicates the launched Open MPI tasks could
 *	not open the reserved port. It was already open by some
 *	other process. */
#define OPEN_MPI_PORT_ERROR 108
#endif

#define MAX_STEP_RETRIES 4

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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "launch Slurm plugin";
const char plugin_type[]        = "launch/slurm";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static List local_job_list = NULL;
static uint32_t *local_global_rc = NULL;
static pthread_mutex_t launch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pack_lock   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  start_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static slurm_opt_t *opt_save = NULL;

static List task_state_list = NULL;
static time_t launch_start_time;
static bool retry_step_begin = false;
static int  retry_step_cnt = 0;

static int _step_signal(int signal);
extern char **environ;

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

static void _update_task_exit_state(task_state_t task_state, uint32_t ntasks,
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
		return slurm_get_kill_on_bad_exit();
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
	if (difftime(time(NULL), launch_start_time) > slurm_get_msg_timeout())
		return 0;
	return 1;
}

static void
_handle_openmpi_port_error(const char *tasks, const char *hosts,
			   slurm_step_ctx_t *step_ctx)
{
	uint32_t job_id, step_id;
	char *msg = "retrying";

	if (!retry_step_begin) {
		retry_step_begin = true;
		retry_step_cnt++;
	}

	if (retry_step_cnt >= MAX_STEP_RETRIES)
		msg = "aborting";
	error("%s: tasks %s unable to claim reserved port, %s.",
	      hosts, tasks, msg);

	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_JOBID, &job_id);
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_STEPID, &step_id);
	info("Terminating job step %u.%u", job_id, step_id);
	slurm_kill_job_step(job_id, step_id, SIGKILL);
}

static void _task_start(launch_tasks_response_msg_t *msg)
{
	MPIR_PROCDESC *table;
	uint32_t local_task_id, global_task_id;
	int i;
	task_state_t task_state;

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

	task_state = task_state_find(msg->job_id, msg->step_id, NO_VAL,
				     task_state_list);
	if (!task_state) {
		error("%s: Could not locate task state for step %u.%u",
		      __func__, msg->job_id, msg->step_id);
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
		table->host_name = xstrdup(msg->node_name);
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

static void _task_finish(task_exit_msg_t *msg)
{
	char *tasks = NULL, *hosts = NULL;
	bool build_task_string = false;
	uint32_t rc = 0;
	int normal_exit = 0;
	static int reduce_task_exit_msg = -1;
	static int msg_printed = 0, oom_printed = 0, last_task_exit_rc;
	task_state_t task_state;
	const char *task_str = _taskstr(msg->num_tasks);
	srun_job_t *my_srun_job;
	ListIterator iter;

	iter = list_iterator_create(local_job_list);
	while ((my_srun_job = (srun_job_t *) list_next(iter))) {
		if ((my_srun_job->jobid  == msg->job_id) &&
		    (my_srun_job->stepid == msg->step_id))
			break;
	}
	list_iterator_destroy(iter);
	if (!my_srun_job) {
		error("Ignoring exit message from unrecognized step %u.%u",
		      msg->job_id, msg->step_id);
		return;
	}

	if (reduce_task_exit_msg == -1) {
		char *ptr = getenv("SLURM_SRUN_REDUCE_TASK_EXIT_MSG");
		if (ptr && atoi(ptr) != 0)
			reduce_task_exit_msg = 1;
		else
			reduce_task_exit_msg = 0;
	}

	verbose("Received task exit notification for %d %s of step %u.%u (status=0x%04x).",
		msg->num_tasks, task_str, msg->job_id, msg->step_id,
		msg->return_code);

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

	task_state = task_state_find(msg->job_id, msg->step_id, NO_VAL,
				     task_state_list);
	if (task_state) {
		_update_task_exit_state(task_state, msg->num_tasks,
					msg->task_id_list, !normal_exit);
	} else {
		error("%s: Could not find task state for step %u.%u", __func__,
		      msg->job_id, msg->step_id);
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

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	FREE_NULL_LIST(task_state_list);

	return SLURM_SUCCESS;
}

extern int launch_p_setup_srun_opt(char **rest, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);
	if (srun_opt->debugger_test && srun_opt->parallel_debug)
		MPIR_being_debugged = 1;

	/*
	 * We need to do +2 here just in case multi-prog is needed
	 * (we add an extra argv on so just make space for it).
	 */
	srun_opt->argv = xmalloc((srun_opt->argc + 2) * sizeof(char *));

	return 0;
}

extern int launch_p_handle_multi_prog_verify(int command_pos,
					     slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	if (srun_opt->multi_prog) {
		if (srun_opt->argc < 1) {
			error("configuration file not specified");
			exit(error_exit);
		}
		_load_multi(&srun_opt->argc, srun_opt->argv);
		if (verify_multi_name(srun_opt->argv[command_pos], opt_local))
			exit(error_exit);
		return 1;
	} else
		return 0;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job,
				    slurm_opt_t *opt_local)
{
	if (launch_common_create_job_step(job, use_all_cpus,
					  signal_function, destroy_job,
					  opt_local) != SLURM_SUCCESS)
		return SLURM_ERROR;

	/* set the jobid for totalview */
	if (!totalview_jobid) {
		xstrfmtcat(totalview_jobid,  "%u", job->jobid);
		xstrfmtcat(totalview_stepid, "%u", job->stepid);
	}

	return SLURM_SUCCESS;
}

static char **_build_user_env(srun_job_t *job, slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	char **dest_array = NULL;
	char *tmp_env, *tok, *save_ptr = NULL, *eq_ptr, *value;
	bool all;
	xassert(srun_opt);

	if (!srun_opt->export_env) {
		all = true;
	} else {
		all = false;
		tmp_env = xstrdup(srun_opt->export_env);
		tok = strtok_r(tmp_env, ",", &save_ptr);
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
			tok = strtok_r(NULL, ",", &save_ptr);
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
	task_state_t task_state = (task_state_t) x;

	task_state_destroy(task_state);
}

/*
 * Return only after all pack job components reach this point (or timeout)
 */
static void _wait_all_pack_started(slurm_opt_t *opt_local)
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
		total_cnt = srun_opt->pack_step_cnt;
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

extern int launch_p_step_launch(srun_job_t *job, slurm_step_io_fds_t *cio_fds,
				uint32_t *global_rc,
				slurm_step_launch_callbacks_t *step_callbacks,
				slurm_opt_t *opt_local)
{
	srun_job_t *local_srun_job;
	srun_opt_t *srun_opt = opt_local->srun_opt;
	slurm_step_launch_params_t launch_params;
	slurm_step_launch_callbacks_t callbacks;
	int rc = SLURM_SUCCESS;
	task_state_t task_state;
	bool first_launch = false;
	uint32_t def_cpu_bind_type = 0;
	char tmp_str[128];
	xassert(srun_opt);

	slurm_step_launch_params_t_init(&launch_params);
	memcpy(&callbacks, step_callbacks, sizeof(callbacks));

	task_state = task_state_find(job->jobid, job->stepid, job->pack_offset,
				     task_state_list);
	if (!task_state) {
		task_state = task_state_create(job->jobid, job->stepid,
					       job->pack_offset, job->ntasks,
					       job->pack_task_offset);
		slurm_mutex_lock(&pack_lock);
		if (!local_job_list)
			local_job_list = list_create(NULL);
		if (!task_state_list)
			task_state_list = list_create(_task_state_del);
		slurm_mutex_unlock(&pack_lock);
		local_srun_job = job;
		local_global_rc = global_rc;
		list_append(local_job_list, local_srun_job);
		list_append(task_state_list, task_state);
		first_launch = true;
	} else {
		/* Launching extra POE tasks */
		task_state_alter(task_state, job->ntasks);
	}

	launch_params.gid = opt_local->gid;
	launch_params.alias_list = job->alias_list;
	launch_params.argc = srun_opt->argc;
	launch_params.argv = srun_opt->argv;
	launch_params.multi_prog = srun_opt->multi_prog ? true : false;
	launch_params.cwd = opt_local->cwd;
	launch_params.slurmd_debug = srun_opt->slurmd_debug;
	launch_params.buffered_stdio = !srun_opt->unbuffered;
	launch_params.labelio = srun_opt->labelio ? true : false;
	launch_params.remote_output_filename = fname_remote_string(job->ofname);
	launch_params.remote_input_filename  = fname_remote_string(job->ifname);
	launch_params.remote_error_filename  = fname_remote_string(job->efname);
	launch_params.node_offset = job->node_offset;
	launch_params.pack_jobid  = job->pack_jobid;
	launch_params.pack_nnodes = job->pack_nnodes;
	launch_params.pack_ntasks = job->pack_ntasks;
	launch_params.pack_offset = job->pack_offset;
	launch_params.pack_task_offset = job->pack_task_offset;
	launch_params.pack_task_cnts = job->pack_task_cnts;
	launch_params.pack_tids = job->pack_tids;
	launch_params.pack_node_list = job->pack_node_list;
	launch_params.partition = job->partition;
	launch_params.profile = opt_local->profile;
	launch_params.task_prolog = srun_opt->task_prolog;
	launch_params.task_epilog = srun_opt->task_epilog;

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_DEF_CPU_BIND_TYPE,
			   &def_cpu_bind_type);
	if (slurm_verify_cpu_bind(NULL, &srun_opt->cpu_bind,
				  &srun_opt->cpu_bind_type,
				  def_cpu_bind_type)) {
		return SLURM_ERROR;
	}
	slurm_sprint_cpu_bind_type(tmp_str, srun_opt->cpu_bind_type);
	verbose("CpuBindType=%s", tmp_str);
	launch_params.cpu_bind = srun_opt->cpu_bind;
	launch_params.cpu_bind_type = srun_opt->cpu_bind_type;

	launch_params.mem_bind = opt_local->mem_bind;
	launch_params.mem_bind_type = opt_local->mem_bind_type;
	launch_params.accel_bind_type = srun_opt->accel_bind_type;
	launch_params.open_mode = srun_opt->open_mode;
	if (opt_local->acctg_freq)
		launch_params.acctg_freq = opt_local->acctg_freq;
	launch_params.pty = srun_opt->pty;
	if (opt_local->cpus_set)
		launch_params.cpus_per_task	= opt_local->cpus_per_task;
	else
		launch_params.cpus_per_task	= 1;
	launch_params.cpu_freq_min       = opt_local->cpu_freq_min;
	launch_params.cpu_freq_max       = opt_local->cpu_freq_max;
	launch_params.cpu_freq_gov       = opt_local->cpu_freq_gov;
	launch_params.tres_bind          = opt_local->tres_bind;
	launch_params.tres_freq          = opt_local->tres_freq;
	launch_params.task_dist          = opt_local->distribution;
	launch_params.ckpt_dir		 = srun_opt->ckpt_dir;
	launch_params.restart_dir        = srun_opt->restart_dir;
	launch_params.preserve_env       = srun_opt->preserve_env;
	launch_params.spank_job_env      = opt_local->spank_job_env;
	launch_params.spank_job_env_size = opt_local->spank_job_env_size;
	launch_params.user_managed_io    = srun_opt->user_managed_io;
	launch_params.ntasks_per_board   = job->ntasks_per_board;
	launch_params.ntasks_per_core    = job->ntasks_per_core;
	launch_params.ntasks_per_socket  = job->ntasks_per_socket;
	launch_params.no_alloc           = srun_opt->no_alloc;
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
				      &callbacks,
				      srun_opt->pack_step_cnt)
				!= SLURM_SUCCESS) {
			rc = errno;
			*local_global_rc = errno;
			error("Application launch failed: %m");
			slurm_step_launch_abort(job->step_ctx);
			slurm_step_launch_wait_finish(job->step_ctx);
			goto cleanup;
		}
	} else {
		if (slurm_step_launch_add(job->step_ctx, job->step_ctx,
					  &launch_params, job->nodelist,
					  job->fir_nodeid) != SLURM_SUCCESS) {
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
					    launch_params.argv[0],
					    launch_params.cwd);
		} else {
			mpir_set_executable_names(launch_params.argv[0],
						  job->pack_task_offset,
						  job->ntasks);
		}

		_wait_all_pack_started(opt_local);
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		if (srun_opt->debugger_test)
			mpir_dump_proctable();
		else if (srun_opt->parallel_debug)
			MPIR_Breakpoint(job);
	} else {
		info("Job step %u.%u aborted before step completely launched.",
		     job->jobid, job->stepid);
	}

cleanup:
	return rc;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc,
			      slurm_opt_t *opt_local)
{
	int rc = 0;

	slurm_step_launch_wait_finish(job->step_ctx);
	if ((MPIR_being_debugged == 0) && retry_step_begin &&
	    (retry_step_cnt < MAX_STEP_RETRIES) &&
	     (job->pack_jobid == NO_VAL)) {	/* Not pack step */
		retry_step_begin = false;
		slurm_step_ctx_destroy(job->step_ctx);
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
		info("Terminating job step %u.%u",
		      my_srun_job->jobid, my_srun_job->stepid);
		rc2 = slurm_kill_job_step(my_srun_job->jobid,
					  my_srun_job->stepid, signal);
		if (rc2)
			rc = rc2;
	}
	list_iterator_destroy(iter);
	return rc;
}

extern int launch_p_step_terminate(void)
{
	return _step_signal(SIGKILL);

}

extern void launch_p_print_status(void)
{
	task_state_print(task_state_list, (log_f)info);
}

extern void launch_p_fwd_signal(int signal)
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
