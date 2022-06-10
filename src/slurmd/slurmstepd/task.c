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
#include "src/common/gres.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/plugstack.h"
#include "src/common/run_command.h"
#include "src/common/slurm_mpi.h"
#include "src/common/strlcpy.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/ulimits.h"

/*
 * Static prototype definitions.
 */
static void  _make_tmpdir(stepd_step_rec_t *job);
static int   _run_script_and_set_env(const char *name, const char *path,
				     stepd_step_rec_t *job);
static void  _proc_stdout(char *buf, stepd_step_rec_t *job);

/*
 * Process TaskProlog output
 * "export NAME=value"	adds environment variables
 * "unset  NAME"	clears an environment variable
 * "print  <whatever>"	writes that to the job's stdout
 */
static void _proc_stdout(char *buf, stepd_step_rec_t *job)
{
	bool end_buf = false;
	int len;
	char *buf_ptr, *name_ptr, *val_ptr;
	char *end_line, *equal_ptr;
	char ***env = &job->env;

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
				job->cpu_bind_type = CPU_BIND_MASK;
				xfree(job->cpu_bind);
				job->cpu_bind = xstrdup(val_ptr);
				if (task_g_pre_launch(job)) {
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
 * job IN/OUT: pointer to associated job, can update job->env
 *	if prolog
 * RET the exit status of the script or 1 on generic error and 0 on success
 */
static int
_run_script_and_set_env(const char *name, const char *path,
			stepd_step_rec_t *job)
{
	int status = 0, rc = 0;
	char *argv[2];
	char *buf = NULL;
	run_command_args_t args = {
		.job_id = job->step_id.job_id,
		.max_wait = -1,
		.script_path = path,
		.script_type = name,
		.status = &status
	};

	if (path == NULL || path[0] == '\0')
		return rc;

	xassert(job->env);
	setenvf(&job->env, "SLURM_SCRIPT_CONTEXT", "prolog_task");
	args.env = job->env;

	argv[0] = xstrdup(path);
	argv[1] = NULL;
	args.script_argv = argv;

	debug("[job %u] attempting to run %s [%s]",
	      job->step_id.job_id, name, path);
	buf = run_command(&args);

	if (WIFEXITED(status)) {
		if (buf)
			_proc_stdout(buf, job);
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
_setup_mpi(stepd_step_rec_t *job, int ltaskid)
{
	mpi_plugin_task_info_t info[1];

	if (job->het_job_id && (job->het_job_id != NO_VAL))
		info->step_id.job_id   = job->het_job_id;
	else
		info->step_id.job_id   = job->step_id.job_id;

	if (job->het_job_offset != NO_VAL) {
		info->step_id.step_id  = job->step_id.step_id;
		info->step_id.step_het_comp  = job->step_id.step_het_comp;
		info->nnodes  = job->het_job_nnodes;
		info->nodeid  = job->het_job_node_offset + job->nodeid;
		info->ntasks  = job->het_job_ntasks;
		info->ltasks  = job->node_tasks;
		info->gtaskid = job->het_job_task_offset +
				job->task[ltaskid]->gtid;
		info->ltaskid = job->task[ltaskid]->id;
		info->self    = job->envtp->self;
		info->client  = job->envtp->cli;
	} else {
		info->step_id.step_id  = job->step_id.step_id;
		info->step_id.step_het_comp  = job->step_id.step_het_comp;
		info->nnodes  = job->nnodes;
		info->nodeid  = job->nodeid;
		info->ntasks  = job->ntasks;
		info->ltasks  = job->node_tasks;
		info->gtaskid = job->task[ltaskid]->gtid;
		info->ltaskid = job->task[ltaskid]->id;
		info->self    = job->envtp->self;
		info->client  = job->envtp->cli;
	}

	return mpi_g_slurmstepd_task(info, &job->env);
}

/*
 *  Current process is running as the user when this is called.
 */
extern void exec_task(stepd_step_rec_t *job, int local_proc_id)
{
	int fd, j;
	stepd_step_task_info_t *task = job->task[local_proc_id];
	char **tmp_env;
	int saved_errno, status;
	uint32_t node_offset = 0, task_offset = 0;

	if (job->het_job_node_offset != NO_VAL)
		node_offset = job->het_job_node_offset;
	if (job->het_job_task_offset != NO_VAL)
		task_offset = job->het_job_task_offset;

	for (j = 0; j < job->node_tasks; j++)
		xstrfmtcat(job->envtp->sgtids, "%s%u", j ? "," : "",
			   job->task[j]->gtid + task_offset);

	if (job->het_job_id != NO_VAL)
		job->envtp->jobid = job->het_job_id;
	else
		job->envtp->jobid = job->step_id.job_id;
	job->envtp->stepid = job->step_id.step_id;
	job->envtp->nodeid = job->nodeid + node_offset;
	job->envtp->cpus_on_node = job->cpus;
	job->envtp->procid = task->gtid + task_offset;
	job->envtp->localid = task->id;
	job->envtp->task_pid = getpid();
	job->envtp->distribution = job->task_dist;
	job->envtp->cpu_bind = xstrdup(job->cpu_bind);
	job->envtp->cpu_bind_type = job->cpu_bind_type;
	job->envtp->cpu_freq_min = job->cpu_freq_min;
	job->envtp->cpu_freq_max = job->cpu_freq_max;
	job->envtp->cpu_freq_gov = job->cpu_freq_gov;
	job->envtp->mem_bind = xstrdup(job->mem_bind);
	job->envtp->mem_bind_type = job->mem_bind_type;
	job->envtp->distribution = -1;
	job->envtp->batch_flag = job->batch;
	job->envtp->uid = job->uid;
	job->envtp->user_name = xstrdup(job->user_name);

	/*
	 * Modify copy of job's environment. Do not alter in place or
	 * concurrent searches of the environment can generate invalid memory
	 * references.
	 */
	job->envtp->env = env_array_copy((const char **) job->env);
	setup_env(job->envtp, false);
	setenvf(&job->envtp->env, "SLURM_JOB_GID", "%u", job->gid);
	setenvf(&job->envtp->env, "SLURMD_NODENAME", "%s", conf->node_name);
	if (job->tres_bind) {
		setenvf(&job->envtp->env, "SLURMD_TRES_BIND", "%s",
			job->tres_bind);
	}
	if (job->tres_freq) {
		setenvf(&job->envtp->env, "SLURMD_TRES_FREQ", "%s",
			job->tres_freq);
	}
	tmp_env = job->env;
	job->env = job->envtp->env;
	env_array_free(tmp_env);
	job->envtp->env = NULL;

	xfree(job->envtp->task_count);

	if (!job->batch && (job->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (job->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		if (switch_g_job_attach(job->switch_job, &job->env,
					job->nodeid, (uint32_t) local_proc_id,
					job->nnodes, job->ntasks,
					task->gtid + task_offset) < 0) {
			error("Unable to attach to interconnect: %m");
			log_fini();
			_exit(1);
		}

		if (_setup_mpi(job, local_proc_id) != SLURM_SUCCESS) {
			error("Unable to configure MPI plugin: %m");
			log_fini();
			_exit(1);
		}
	}

	/* task-specific pre-launch activities */

	/* task plugin hook */
	if (task_g_pre_launch(job)) {
		error("Failed to invoke task plugins: task_p_pre_launch error");
		_exit(1);
	}
	if (!job->batch && (job->step_id.step_id != SLURM_INTERACTIVE_STEP) &&
	    (job->accel_bind_type || job->tres_bind)) {
		/*
		 * Modify copy of job's environment as needed for GRES. Do not
		 * alter in place or concurrent searches of the environment can
		 * generate invalid memory references.
		 */
		job->envtp->env = env_array_copy((const char **) job->env);
		gres_g_task_set_env(&job->envtp->env, job->step_gres_list,
				    job->accel_bind_type, job->tres_bind,
				    local_proc_id);
		tmp_env = job->env;
		job->env = job->envtp->env;
		env_array_free(tmp_env);
	}

	if (spank_user_task(job, local_proc_id) < 0) {
		error("Failed to invoke spank plugin stack");
		_exit(1);
	}

#ifdef WITH_SELINUX
	if (setexeccon(job->selinux_context)) {
		error("Failed to set SELinux context to %s: %m",
		      job->selinux_context);
		_exit(1);
	}
#else
	if (job->selinux_context) {
		error("Built without SELinux support but context was specified");
		_exit(1);
	}
#endif

	if (slurm_conf.task_prolog) {
		status = _run_script_and_set_env("slurm task_prolog",
						 slurm_conf.task_prolog, job);
		if (status) {
			error("TaskProlog failed status=%d", status);
			_exit(status);
		}
	}
	if (job->task_prolog) {
		status = _run_script_and_set_env("user task_prolog",
						 job->task_prolog, job);
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
		_make_tmpdir(job);

	if (!job->batch)
		pdebug_stop_current(job);
	if (job->env == NULL) {
		debug("job->env is NULL");
		job->env = (char **)xmalloc(sizeof(char *));
		job->env[0] = (char *)NULL;
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
		task->argv[0] = _build_path(task->argv[0], job->env);
	}


	/* Do this last so you don't worry too much about the users
	   limits including the slurmstepd in with it.
	*/
	if (set_user_limits(job) < 0) {
		debug("Unable to set user limits");
		log_fini();
		_exit(5);
	}

	/*
	 * If argv[0] ends with '/' it indicates that srun was called with
	 * --bcast with destination dir instead of file name. So match the
	 * convention used by _rpc_file_bcast().
	 */
	if (task->argv[0][strlen(task->argv[0]) - 1] == '/') {
		xstrfmtcat(task->argv[0], "slurm_bcast_%u.%u_%s",
			   job->step_id.job_id, job->step_id.step_id,
			   job->node_name);
	}

	if (job->container)
		container_run(job, task);

	execve(task->argv[0], task->argv, job->env);
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
_make_tmpdir(stepd_step_rec_t *job)
{
	char *tmpdir;

	if (!(tmpdir = getenvp(job->env, "TMPDIR")))
		setenvf(&job->env, "TMPDIR", "/tmp"); /* task may want it set */
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
		setenvf(&job->env, "TMPDIR", "/tmp");
	}

	return;
}
