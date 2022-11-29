/*****************************************************************************\
 *  slurmd/slurmstepd/task.c - task launching functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#endif

/* FIXME: Come up with a real solution for EUID instead of substituting RUID */
#if defined(__NetBSD__)
#define eaccess(p,m) (access((p),(m)))
#define HAVE_EACCESS 1
#endif

#include "slurm/slurm_errno.h"

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/run_command.h"
#include "src/common/spank.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/mpi.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/ulimits.h"

/*
 * Static prototype definitions.
 */
static void  _make_tmpdir(stepd_step_rec_t *step);
static int   _run_script_and_set_env(const char *name, const char *path,
				     stepd_step_rec_t *step);
static void  _proc_stdout(char *buf, stepd_step_rec_t *step);

/*
 * Process TaskProlog output
 * "export NAME=value"	adds environment variables
 * "unset  NAME"	clears an environment variable
 * "print  <whatever>"	writes that to the step's stdout
 */
static void _proc_stdout(char *buf, stepd_step_rec_t *step)
{
	bool end_buf = false;
	int len;
	char *buf_ptr, *name_ptr, *val_ptr;
	char *end_line, *equal_ptr;
	char ***env = &step->env;

	buf_ptr = buf;
	while (buf_ptr[0]) {
		end_line = strchr(buf_ptr, '\n');
		if (!end_line) {
			end_line = buf_ptr + strlen(buf_ptr);
			end_buf = true;
		}
		if (!xstrncmp(buf_ptr, "print ", 6)) {
			buf_ptr += 6;
			while (isspace(buf_ptr[0]))
				buf_ptr++;
			len = end_line - buf_ptr + 1;
			safe_write(1, buf_ptr, len);
		} else if (!xstrncmp(buf_ptr, "export ",7)) {
			name_ptr = buf_ptr + 7;
			while (isspace(name_ptr[0]))
				name_ptr++;
			equal_ptr = strchr(name_ptr, '=');
			if (!equal_ptr || (equal_ptr > end_line))
				goto rwfail;
			val_ptr = equal_ptr + 1;
			while (isspace(equal_ptr[-1]))
				equal_ptr--;
			equal_ptr[0] = '\0';
			end_line[0] = '\0';
			if (!xstrcmp(name_ptr, "SLURM_PROLOG_CPU_MASK")) {
				step->cpu_bind_type = CPU_BIND_MASK;
				xfree(step->cpu_bind);
				step->cpu_bind = xstrdup(val_ptr);
				if (task_g_pre_launch(step)) {
					error("Failed SLURM_PROLOG_CPU_MASK "
					      "setup");
					exit(1);
				}
			}
			debug("export name:%s:val:%s:", name_ptr, val_ptr);
			if (setenvf(env, name_ptr, "%s", val_ptr)) {
				error("Unable to set %s environment variable",
				      buf_ptr);
			}
			equal_ptr[0] = '=';
			if (end_buf)
				end_line[0] = '\0';
			else
				end_line[0] = '\n';
		} else if (!xstrncmp(buf_ptr, "unset ", 6)) {
			name_ptr = buf_ptr + 6;
			while (isspace(name_ptr[0]))
				name_ptr++;
			if ((name_ptr[0] == '\n') || (name_ptr[0] == '\0'))
				goto rwfail;
			while (isspace(end_line[-1]))
				end_line--;
			end_line[0] = '\0';
			debug(" unset name:%s:", name_ptr);
			unsetenvp(*env, name_ptr);
			if (end_buf)
				end_line[0] = '\0';
			else
				end_line[0] = '\n';
		}

rwfail:		 /* process rest of script output */
		if (end_buf)
			break;
		buf_ptr = end_line + 1;
	}
	return;
}

/*
 * Run a task prolog script.  Also read the stdout of the script and set
 * 	environment variables in the task's environment as specified
 *	in the script's standard output.
 * name IN: class of program ("system prolog", "user prolog", etc.)
 * path IN: pathname of program to run
 * step IN/OUT: pointer to associated step, can update step->env
 *	if prolog
 * RET the exit status of the script or 1 on generic error and 0 on success
 */
static int
_run_script_and_set_env(const char *name, const char *path,
			stepd_step_rec_t *step)
{
	int status = 0, rc = 0;
	char *argv[2];
	char *buf = NULL;
	run_command_args_t args = {
		.job_id = step->step_id.job_id,
		.max_wait = -1,
		.script_path = path,
		.script_type = name,
		.status = &status
	};

	if (path == NULL || path[0] == '\0')
		return rc;

	xassert(step->env);
	setenvf(&step->env, "SLURM_SCRIPT_CONTEXT", "prolog_task");
	args.env = step->env;

	argv[0] = xstrdup(path);
	argv[1] = NULL;
	args.script_argv = argv;

	debug("[job %u] attempting to run %s [%s]",
	      step->step_id.job_id, name, path);
	buf = run_command(&args);

	if (WIFEXITED(status)) {
		if (buf)
			_proc_stdout(buf, step);
		rc = WEXITSTATUS(status);
	} else {
		error("%s did not exit normally. reason: %s", name, buf);
		rc = 1;
	}

	xfree(argv[0]);
	xfree(buf);
	return rc;
}

/* Given a program name, translate it to a fully qualified pathname as needed
 * based upon the PATH environment variable and current working directory
 * Returns xmalloc()'d string that must be xfree()'d */
static char *_build_path(char *fname, char **prog_env)
{
	char *path_env = NULL, *dir = NULL;
	char *file_name, *last = NULL;
	struct stat stat_buf;
	int len = PATH_MAX;

	if (!fname)
		return NULL;

	file_name = (char *) xmalloc(len);

	/* check if already absolute path */
	if (fname[0] == '/') {
		/* copy and ensure null termination */
		strlcpy(file_name, fname, len);
		return file_name;
	}

	if (fname[0] == '.') {
		dir = xmalloc(len);
		if (!getcwd(dir, len))
			error("getcwd failed: %m");
		snprintf(file_name, len, "%s/%s", dir, fname);
		xfree(dir);
		return file_name;
	}

	/* search for the file using PATH environment variable */
	path_env = xstrdup(getenvp(prog_env, "PATH"));
	if (path_env)
		dir = strtok_r(path_env, ":", &last);
	while (dir) {
		snprintf(file_name, len, "%s/%s", dir, fname);
		if ((stat(file_name, &stat_buf) == 0)
		    && (! S_ISDIR(stat_buf.st_mode)))
			break;
		dir = strtok_r(NULL, ":", &last);
	}
	if (dir == NULL)	/* not found */
		strlcpy(file_name, fname, len);

	xfree(path_env);
	return file_name;
}

static int
_setup_mpi(stepd_step_rec_t *step, int ltaskid)
{
	mpi_task_info_t info[1];

	if (step->het_job_id && (step->het_job_id != NO_VAL))
		info->step_id.job_id   = step->het_job_id;
	else
		info->step_id.job_id   = step->step_id.job_id;

	if (step->het_job_offset != NO_VAL) {
		info->step_id.step_id  = step->step_id.step_id;
		info->step_id.step_het_comp  = step->step_id.step_het_comp;
		info->nnodes  = step->het_job_nnodes;
		info->nodeid  = step->het_job_node_offset + step->nodeid;
		info->ntasks  = step->het_job_ntasks;
		info->ltasks  = step->node_tasks;
		info->gtaskid = step->het_job_task_offset +
				step->task[ltaskid]->gtid;
		info->ltaskid = step->task[ltaskid]->id;
		info->self    = step->envtp->self;
		info->client  = step->envtp->cli;
	} else {
		info->step_id.step_id  = step->step_id.step_id;
		info->step_id.step_het_comp  = step->step_id.step_het_comp;
		info->nnodes  = step->nnodes;
		info->nodeid  = step->nodeid;
		info->ntasks  = step->ntasks;
		info->ltasks  = step->node_tasks;
		info->gtaskid = step->task[ltaskid]->gtid;
		info->ltaskid = step->task[ltaskid]->id;
		info->self    = step->envtp->self;
		info->client  = step->envtp->cli;
	}

	return mpi_g_slurmstepd_task(info, &step->env);
}

/*
 *  Current process is running as the user when this is called.
 */
extern void exec_task(stepd_step_rec_t *step, int local_proc_id)
{
	int fd, j;
	stepd_step_task_info_t *task = step->task[local_proc_id];
	char **tmp_env;
	int saved_errno, status;
	uint32_t node_offset = 0, task_offset = 0;

	if (step->container)
		container_task_init(step, task);

	if (step->het_job_node_offset != NO_VAL)
		node_offset = step->het_job_node_offset;
	if (step->het_job_task_offset != NO_VAL)
		task_offset = step->het_job_task_offset;

	for (j = 0; j < step->node_tasks; j++)
		xstrfmtcat(step->envtp->sgtids, "%s%u", j ? "," : "",
			   step->task[j]->gtid + task_offset);

	if (step->het_job_id != NO_VAL)
		step->envtp->jobid = step->het_job_id;
	else
		step->envtp->jobid = step->step_id.job_id;
	step->envtp->stepid = step->step_id.step_id;
	step->envtp->nodeid = step->nodeid + node_offset;
	step->envtp->cpus_on_node = step->cpus;
	step->envtp->procid = task->gtid + task_offset;
	step->envtp->localid = task->id;
	step->envtp->task_pid = getpid();
	step->envtp->distribution = step->task_dist;
	step->envtp->cpu_bind = xstrdup(step->cpu_bind);
	step->envtp->cpu_bind_type = step->cpu_bind_type;
	step->envtp->cpu_freq_min = step->cpu_freq_min;
	step->envtp->cpu_freq_max = step->cpu_freq_max;
	step->envtp->cpu_freq_gov = step->cpu_freq_gov;
	step->envtp->mem_bind = xstrdup(step->mem_bind);
	step->envtp->mem_bind_type = step->mem_bind_type;
	step->envtp->distribution = -1;
	step->envtp->batch_flag = step->batch;
	step->envtp->uid = step->uid;
	step->envtp->job_end_time = step->job_end_time;
	step->envtp->job_licenses = xstrdup(step->job_licenses);
	step->envtp->job_start_time = step->job_start_time;
	step->envtp->user_name = xstrdup(step->user_name);

	/*
	 * Modify copy of step's environment. Do not alter in place or
	 * concurrent searches of the environment can generate invalid memory
	 * references.
	 */
	step->envtp->env = env_array_copy((const char **) step->env);
	setup_env(step->envtp, false);
	setenvf(&step->envtp->env, "SLURM_JOB_GID", "%u", step->gid);
	setenvf(&step->envtp->env, "SLURMD_NODENAME", "%s", conf->node_name);
	if (step->tres_bind) {
		setenvf(&step->envtp->env, "SLURMD_TRES_BIND", "%s",
			step->tres_bind);
	}
	if (step->tres_freq) {
		setenvf(&step->envtp->env, "SLURMD_TRES_FREQ", "%s",
			step->tres_freq);
	}
	tmp_env = step->env;
	step->env = step->envtp->env;
	env_array_free(tmp_env);
	step->envtp->env = NULL;

	xfree(step->envtp->task_count);

	if (!step->batch && (step->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		if (switch_g_job_attach(step->switch_job, &step->env,
					step->nodeid, (uint32_t) local_proc_id,
					step->nnodes, step->ntasks,
					task->gtid + task_offset) < 0) {
			error("Unable to attach to interconnect: %m");
			log_fini();
			_exit(1);
		}

		if (_setup_mpi(step, local_proc_id) != SLURM_SUCCESS) {
			error("Unable to configure MPI plugin: %m");
			log_fini();
			_exit(1);
		}
	}

	/* task-specific pre-launch activities */

	/* task plugin hook */
	if (task_g_pre_launch(step)) {
		error("Failed to invoke task plugins: task_p_pre_launch error");
		_exit(1);
	}
	if (!step->batch && (step->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (step->accel_bind_type || step->tres_bind)) {
		/*
		 * Modify copy of step's environment as needed for GRES. Do not
		 * alter in place or concurrent searches of the environment can
		 * generate invalid memory references.
		 */
		step->envtp->env = env_array_copy((const char **) step->env);
		gres_g_task_set_env(step, local_proc_id);
		tmp_env = step->env;
		step->env = step->envtp->env;
		env_array_free(tmp_env);
	}

	/*
	 * test7.21 calls slurm_load_job() as an example of weird things people
	 * may do within a SPANK stack. That will deadlock if we don't drop the
	 * lock here.
	 */
	auth_setuid_unlock();
	if (spank_user_task(step, local_proc_id) < 0) {
		error("Failed to invoke spank plugin stack");
		_exit(1);
	}
	auth_setuid_lock();

#ifdef WITH_SELINUX
	if (setexeccon(step->selinux_context)) {
		error("Failed to set SELinux context to %s: %m",
		      step->selinux_context);
		_exit(1);
	}
#else
	if (step->selinux_context) {
		error("Built without SELinux support but context was specified");
		_exit(1);
	}
#endif

	if (slurm_conf.task_prolog) {
		status = _run_script_and_set_env("slurm task_prolog",
						 slurm_conf.task_prolog, step);
		if (status) {
			error("TaskProlog failed status=%d", status);
			_exit(status);
		}
	}
	if (step->task_prolog) {
		status = _run_script_and_set_env("user task_prolog",
						 step->task_prolog, step);
		if (status) {
			error("--task-prolog failed status=%d", status);
			_exit(status);
		}
	}

	/*
	 * Set TMPDIR after running prolog scripts, since TMPDIR
	 * might be set or changed in one of the prolog scripts.
	 */
	if (local_proc_id == 0)
		_make_tmpdir(step);

	if (!step->batch)
		pdebug_stop_current(step);
	if (step->env == NULL) {
		debug("step->env is NULL");
		step->env = (char **)xmalloc(sizeof(char *));
		step->env[0] = (char *)NULL;
	}

	if (task->argv[0] == NULL) {
		error("No executable program specified for this task");
		_exit(2);
	}

	if (*task->argv[0] != '/') {
		/*
		 * Handle PATH resolution for the command to launch.
		 * Need to handle this late so that SPANK and other plugins
		 * have a chance to manipulate the PATH and/or change the
		 * filesystem namespaces into the final arrangement, which
		 * may affect which executable we select.
		 */
		task->argv[0] = _build_path(task->argv[0], step->env);
	}


	/* Do this last so you don't worry too much about the users
	   limits including the slurmstepd in with it.
	*/
	set_user_limits(step, 0);

	/*
	 * If argv[0] ends with '/' it indicates that srun was called with
	 * --bcast with destination dir instead of file name. So match the
	 * convention used by _rpc_file_bcast().
	 */
	if (task->argv[0][strlen(task->argv[0]) - 1] == '/') {
		xstrfmtcat(task->argv[0], "slurm_bcast_%u.%u_%s",
			   step->step_id.job_id, step->step_id.step_id,
			   step->node_name);
	}

	if (step->container)
		container_run(step, task);

	execve(task->argv[0], task->argv, step->env);
	saved_errno = errno;

	/*
	 * print error message and clean up if execve() returns:
	 */
	if ((errno == ENOENT) &&
	    ((fd = open(task->argv[0], O_RDONLY)) >= 0)) {
		char buf[256], *eol;
		int sz;
		sz = read(fd, buf, sizeof(buf));
		if ((sz >= 3) && (xstrncmp(buf, "#!", 2) == 0)) {
			buf[sizeof(buf)-1] = '\0';
			eol = strchr(buf, '\n');
			if (eol)
				eol[0] = '\0';
			slurm_seterrno(saved_errno);
			error("execve(): bad interpreter(%s): %m", buf+2);
			_exit(errno);
		}
	}
	slurm_seterrno(saved_errno);
	error("execve(): %s: %m", task->argv[0]);
	_exit(errno);
}

static void
_make_tmpdir(stepd_step_rec_t *step)
{
	char *tmpdir;

	if (!(tmpdir = getenvp(step->env, "TMPDIR")))
		setenvf(&step->env, "TMPDIR", "/tmp"); /* task may want it set */
	else if (mkdir(tmpdir, 0700) < 0) {
		struct stat st;
		int mkdir_errno = errno;

		if (stat(tmpdir, &st)) { /* does the file exist ? */
			/* show why we were not able to create it */
			error("Unable to create TMPDIR [%s]: %s",
			      tmpdir, strerror(mkdir_errno));
		} else if (!S_ISDIR(st.st_mode)) {  /* is it a directory? */
			error("TMPDIR [%s] is not a directory", tmpdir);
		}

		/* Eaccess wasn't introduced until glibc 2.4 but euidaccess
		 * has been around for a while.  So to make sure we
		 * still work with older systems we include this check.
		 */

#if defined(HAVE_FACCESSAT)
		else if (faccessat(AT_FDCWD, tmpdir, X_OK|W_OK, AT_EACCESS))
#elif defined(HAVE_EACCESS)
		else if (eaccess(tmpdir, X_OK|W_OK)) /* check permissions */
#else
		else if (euidaccess(tmpdir, X_OK|W_OK))
#endif
			error("TMPDIR [%s] is not writeable", tmpdir);
		else
			return;

		error("Setting TMPDIR to /tmp");
		setenvf(&step->env, "TMPDIR", "/tmp");
	}

	return;
}
