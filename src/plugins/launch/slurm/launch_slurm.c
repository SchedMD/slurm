/*****************************************************************************\
 *  launch_slurm.c - Define job launch using slurm.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/slurm_xlator.h"
#include "src/api/pmi_server.h"
#include "src/srun/libsrun/allocate.h"
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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "launch SLURM plugin";
const char plugin_type[]        = "launch/slurm";
const uint32_t plugin_version   = 101;

static srun_job_t *local_srun_job = NULL;
static uint32_t *local_global_rc = NULL;

static task_state_t task_state;
static time_t launch_start_time;
static bool retry_step_begin = false;
static int  retry_step_cnt = 0;

extern int launch_p_step_terminate();
extern char **environ;

static char *_hostset_to_string(hostset_t hs)
{
	size_t n = 1024;
	size_t maxsize = 1024*64;
	char *str = NULL;

	do {
		str = xrealloc(str, n);
	} while (hostset_ranged_string(hs, n*=2, str) < 0 && (n < maxsize));

	/*
	 *  If string was truncated, indicate this with a '+' suffix.
	 */
	if (n >= maxsize)
		strcpy(str + (maxsize - 2), "+");

	return str;
}

/* Convert an array of task IDs into a list of host names
 * RET: the string, caller must xfree() this value */
static char *_task_ids_to_host_list(int ntasks, uint32_t taskids[])
{
	int i;
	hostset_t hs;
	char *hosts;
	slurm_step_layout_t *sl;

	if ((sl = launch_common_get_slurm_step_layout(local_srun_job)) == NULL)
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

/* Convert an array of task IDs into a string.
 * RET: the string, caller must xfree() this value
 * NOTE: the taskids array is not necessarily in numeric order,
 *       so we use existing bitmap functions to format */
static char *_task_array_to_string(int ntasks, uint32_t taskids[])
{
	bitstr_t *tasks_bitmap = NULL;
	char *str;
	int i;

	tasks_bitmap = bit_alloc(local_srun_job->ntasks);
	if (!tasks_bitmap) {
		error("bit_alloc: memory allocation failure");
		exit(error_exit);
	}
	for (i=0; i<ntasks; i++)
		bit_set(tasks_bitmap, taskids[i]);
	str = xmalloc(2048);
	bit_fmt(str, 2048, tasks_bitmap);
	FREE_NULL_BITMAP(tasks_bitmap);

	return str;
}

static void _update_task_exit_state(
	uint32_t ntasks, uint32_t taskids[], int abnormal)
{
	int i;
	task_state_type_t t = abnormal ? TS_ABNORMAL_EXIT : TS_NORMAL_EXIT;

	for (i = 0; i < ntasks; i++)
		task_state_update(task_state, taskids[i], t);
}

static int _kill_on_bad_exit(void)
{
	if (opt.kill_bad_exit == NO_VAL)
		return slurm_get_kill_on_bad_exit();
	return opt.kill_bad_exit;
}

static void _setup_max_wait_timer(void)
{
	/*  If these are the first tasks to finish we need to
	 *   start a timer to kill off the job step if the other
	 *   tasks don't finish within opt.max_wait seconds.
	 */
	verbose("First task exited. Terminating job in %ds.", opt.max_wait);
	srun_max_timer = true;
	alarm(opt.max_wait);
}

static const char *_taskstr(int n)
{
	if (n == 1)
		return "task";
	else
		return "tasks";
}

static int
_is_openmpi_port_error(int errcode)
{
	if (errcode != OPEN_MPI_PORT_ERROR)
		return 0;
	if (opt.resv_port_cnt == NO_VAL)
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
	int taskid;
	int i;

	if (msg->count_of_pids)
		verbose("Node %s, %d tasks started",
			msg->node_name, msg->count_of_pids);
	else
		/* This message should be displayed through the api,
		   hense it is a debug2 instead of an error.
		*/
		debug2("No tasks started on node %s: %s",
		      msg->node_name, slurm_strerror(msg->return_code));


	for (i = 0; i < msg->count_of_pids; i++) {
		taskid = msg->task_ids[i];
		table = &MPIR_proctable[taskid];
		table->host_name = xstrdup(msg->node_name);
		/* table->executable_name is set elsewhere */
		table->pid = msg->local_pids[i];

		if (msg->return_code == 0) {
			task_state_update(task_state,
					  taskid, TS_START_SUCCESS);
		} else {
			task_state_update(task_state,
					  taskid, TS_START_FAILURE);
		}
	}

}

static void _task_finish(task_exit_msg_t *msg)
{
	char *tasks;
	char *hosts;
	uint32_t rc = 0;
	int normal_exit = 0;
	static int reduce_task_exit_msg = -1;
	static int msg_printed = 0, last_task_exit_rc;

	const char *task_str = _taskstr(msg->num_tasks);

	if (reduce_task_exit_msg == -1) {
		char *ptr = getenv("SLURM_SRUN_REDUCE_TASK_EXIT_MSG");
		if (ptr && atoi(ptr) != 0)
			reduce_task_exit_msg = 1;
		else
			reduce_task_exit_msg = 0;
	}

	verbose("Received task exit notification for %d %s (status=0x%04x).",
		msg->num_tasks, task_str, msg->return_code);

	tasks = _task_array_to_string(msg->num_tasks, msg->task_id_list);
	hosts = _task_ids_to_host_list(msg->num_tasks, msg->task_id_list);

	if (WIFEXITED(msg->return_code)) {
		if ((rc = WEXITSTATUS(msg->return_code)) == 0) {
			verbose("%s: %s %s: Completed", hosts, task_str, tasks);
			normal_exit = 1;
		}
		else if (_is_openmpi_port_error(rc)) {
			_handle_openmpi_port_error(tasks, hosts,
						   local_srun_job->step_ctx);
		} else {
			if (reduce_task_exit_msg == 0 ||
			    msg_printed == 0 ||
			    msg->return_code != last_task_exit_rc) {
				error("%s: %s %s: Exited with exit code %d",
				      hosts, task_str, tasks, rc);
				msg_printed = 1;
			}
		}
		if (!WIFEXITED(*local_global_rc)
		    || (rc > WEXITSTATUS(*local_global_rc)))
			*local_global_rc = msg->return_code;
	}
	else if (WIFSIGNALED(msg->return_code)) {
		const char *signal_str = strsignal(WTERMSIG(msg->return_code));
		char * core_str = "";
#ifdef WCOREDUMP
		if (WCOREDUMP(msg->return_code))
			core_str = " (core dumped)";
#endif
		if (local_srun_job->state >= SRUN_JOB_CANCELLED) {
			verbose("%s: %s %s: %s%s",
				hosts, task_str, tasks, signal_str, core_str);
		} else {
			if (reduce_task_exit_msg == 0 ||
			    msg_printed == 0 ||
			    msg->return_code != last_task_exit_rc) {
				error("%s: %s %s: %s%s",
				      hosts, task_str, tasks, signal_str,
				      core_str);
				msg_printed = 1;
			}
		}
		if (*local_global_rc == NO_VAL)
			*local_global_rc = msg->return_code;
	}

	xfree(tasks);
	xfree(hosts);

	_update_task_exit_state(msg->num_tasks, msg->task_id_list,
				!normal_exit);

	if (task_state_first_abnormal_exit(task_state)
	    && _kill_on_bad_exit())
		launch_p_step_terminate();

	if (task_state_first_exit(task_state) && (opt.max_wait > 0))
		_setup_max_wait_timer();

	last_task_exit_rc = msg->return_code;
}

/* Load the multi_prog config file into argv, pass the  entire file contents
 * in order to avoid having to read the file on every node. We could parse
 * the infomration here too for loading the MPIR records for TotalView */
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
	task_state_destroy(task_state);

	return SLURM_SUCCESS;
}

extern int launch_p_setup_srun_opt(char **rest)
{
	if (opt.debugger_test && opt.parallel_debug)
		MPIR_being_debugged  = 1;

	/* We need to do +2 here just incase multi-prog is needed (we
	   add an extra argv on so just make space for it).
	*/
	opt.argv = (char **) xmalloc((opt.argc + 2) * sizeof(char *));

	return 0;
}

extern int launch_p_handle_multi_prog_verify(int command_pos)
{
	if (opt.multi_prog) {
		if (opt.argc < 1) {
			error("configuration file not specified");
			exit(error_exit);
		}
		_load_multi(&opt.argc, opt.argv);
		if (verify_multi_name(opt.argv[command_pos], &opt.ntasks,
				      &opt.ntasks_set, &opt.multi_prog_cmds))
			exit(error_exit);
		return 1;
	} else
		return 0;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job)
{
	/* set the jobid for totalview */
	totalview_jobid = NULL;
	xstrfmtcat(totalview_jobid, "%u", job->jobid);
	totalview_stepid = NULL;
	xstrfmtcat(totalview_stepid, "%u", job->stepid);

	return launch_common_create_job_step(job, use_all_cpus,
					     signal_function,
					     destroy_job);
}

static char **_build_user_env(void)
{
	char **dest_array = NULL;
	char *tmp_env, *tok, *save_ptr = NULL, *eq_ptr, *value;
	bool all;

	all = false;
	tmp_env = xstrdup(opt.export_env);
	tok = strtok_r(tmp_env, ",", &save_ptr);
	while (tok) {

		if (strcasecmp(tok, "ALL") == 0)
			all = true;

		if (!strcasecmp(tok, "NONE"))
			break;
		eq_ptr = strchr(tok, '=');
		if (eq_ptr) {
			eq_ptr[0] = '\0';
			value = eq_ptr + 1;
			env_array_overwrite(&dest_array, tok, value);
		} else {
			value = getenv(tok);
			if (value)
				env_array_overwrite(&dest_array, tok, value);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_env);

	if (all)
		env_array_merge(&dest_array, (const char **)environ);
	else
		env_array_merge_slurm(&dest_array, (const char **)environ);

	return dest_array;
}

extern int launch_p_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds, uint32_t *global_rc,
	slurm_step_launch_callbacks_t *step_callbacks)
{
	slurm_step_launch_params_t launch_params;
	slurm_step_launch_callbacks_t callbacks;
	int rc = 0;
	bool first_launch = 0;

	slurm_step_launch_params_t_init(&launch_params);
	memcpy(&callbacks, step_callbacks, sizeof(callbacks));

	if (!task_state) {
		task_state = task_state_create(job->ntasks);
		local_srun_job = job;
		local_global_rc = global_rc;
		*local_global_rc = NO_VAL;
		first_launch = 1;
	} else
		task_state_alter(task_state, job->ntasks);

	launch_params.gid = opt.gid;
	launch_params.alias_list = job->alias_list;
	launch_params.argc = opt.argc;
	launch_params.argv = opt.argv;
	launch_params.multi_prog = opt.multi_prog ? true : false;
	launch_params.cwd = opt.cwd;
	launch_params.slurmd_debug = opt.slurmd_debug;
	launch_params.buffered_stdio = opt.unbuffered;
	launch_params.labelio = opt.labelio ? true : false;
	launch_params.remote_output_filename =fname_remote_string(job->ofname);
	launch_params.remote_input_filename = fname_remote_string(job->ifname);
	launch_params.remote_error_filename = fname_remote_string(job->efname);
	launch_params.partition = job->partition;
	launch_params.profile = opt.profile;
	launch_params.task_prolog = opt.task_prolog;
	launch_params.task_epilog = opt.task_epilog;
	launch_params.cpu_bind = opt.cpu_bind;
	launch_params.cpu_bind_type = opt.cpu_bind_type;
	launch_params.mem_bind = opt.mem_bind;
	launch_params.mem_bind_type = opt.mem_bind_type;
	launch_params.open_mode = opt.open_mode;
	if (opt.acctg_freq >= 0)
		launch_params.acctg_freq = opt.acctg_freq;
	launch_params.pty = opt.pty;
	if (opt.cpus_set)
		launch_params.cpus_per_task	= opt.cpus_per_task;
	else
		launch_params.cpus_per_task	= 1;
	launch_params.cpu_freq          = opt.cpu_freq;
	launch_params.task_dist         = opt.distribution;
	launch_params.ckpt_dir		= opt.ckpt_dir;
	launch_params.restart_dir       = opt.restart_dir;
	launch_params.preserve_env      = opt.preserve_env;
	launch_params.spank_job_env     = opt.spank_job_env;
	launch_params.spank_job_env_size = opt.spank_job_env_size;
	launch_params.user_managed_io   = opt.user_managed_io;

	if (opt.export_env)
		launch_params.env = _build_user_env();

	memcpy(&launch_params.local_fds, cio_fds, sizeof(slurm_step_io_fds_t));

	if (MPIR_being_debugged) {
		launch_params.parallel_debug = true;
		pmi_server_max_threads(1);
	} else {
		launch_params.parallel_debug = false;
	}
	/* Normally this isn't used, but if an outside process (other
	   than srun (poe) is using this logic to launch tasks then we
	   can use this to signal the step.
	*/
	callbacks.task_start = _task_start;
	/* If poe is using this code with multi-prog it always returns
	   1 for each task which could be confusing since no real
	   error happened.
	*/
	if (!launch_params.multi_prog
	    || (!callbacks.step_signal
		|| (callbacks.step_signal == launch_g_fwd_signal))) {
		callbacks.task_finish = _task_finish;
	}

	mpir_init(job->ntasks);

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
		if (slurm_step_launch_add(job->step_ctx, &launch_params,
					  job->nodelist, job->fir_nodeid)
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
		/* Only set up MPIR structures if the step launched
		 * correctly. */
		if (opt.multi_prog)
			mpir_set_multi_name(job->ntasks,
					    launch_params.argv[0]);
		else
			mpir_set_executable_names(launch_params.argv[0]);
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;

		if (opt.debugger_test)
			mpir_dump_proctable();
		else
			MPIR_Breakpoint(job);
	} else {
		info("Job step %u.%u aborted before step completely launched.",
		     job->jobid, job->stepid);
	}

cleanup:

	return rc;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc)
{
	int rc = 0;

	slurm_step_launch_wait_finish(job->step_ctx);
	if ((MPIR_being_debugged == 0) && retry_step_begin &&
	    (retry_step_cnt < MAX_STEP_RETRIES)) {
		retry_step_begin = false;
		slurm_step_ctx_destroy(job->step_ctx);
		if (got_alloc) {
			if (create_job_step(job, true) < 0)
				exit(error_exit);
		} else {
			if (create_job_step(job, false) < 0)
				exit(error_exit);
		}
		task_state_destroy(task_state);
		rc = -1;
	}
	return rc;
}

extern int launch_p_step_terminate(void)
{
	info("Terminating job step %u.%u",
	     local_srun_job->jobid, local_srun_job->stepid);
	return slurm_kill_job_step(local_srun_job->jobid,
				   local_srun_job->stepid, SIGKILL);
}


extern void launch_p_print_status(void)
{
	task_state_print(task_state, (log_f)info);
}

extern void launch_p_fwd_signal(int signal)
{
	switch (signal) {
	case SIGKILL:
		slurm_step_launch_abort(local_srun_job->step_ctx);
		break;
	default:
		slurm_step_launch_fwd_signal(local_srun_job->step_ctx, signal);
		break;
	}
}
