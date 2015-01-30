/*****************************************************************************\
 *  slurmd/slurmstepd/task.c - task launching functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#ifdef HAVE_AIX
#  include <sys/checkpnt.h>
#endif

#include <sys/resource.h>

#include "slurm/slurm_errno.h"

#include "src/common/checkpoint.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/mpi.h"
#include "src/common/plugstack.h"
#include "src/slurmd/common/proctrack.h"
#include "src/common/switch.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/slurmd/slurmd/slurmd.h"
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
static char *_uint32_array_to_str(int array_len, const uint32_t *array);

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
		if (!strncmp(buf_ptr, "print ", 6)) {
			buf_ptr += 6;
			while (isspace(buf_ptr[0]))
				buf_ptr++;
			len = end_line - buf_ptr + 1;
			safe_write(1, buf_ptr, len);
		} else if (!strncmp(buf_ptr, "export ",7)) {
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
			if (!strcmp(name_ptr, "SLURM_PROLOG_CPU_MASK")) {
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
		} else if (!strncmp(buf_ptr, "unset ", 6)) {
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
 * RET 0 on success, -1 on failure.
 */
static int
_run_script_and_set_env(const char *name, const char *path,
			stepd_step_rec_t *job)
{
	int status, rc;
	pid_t cpid;
	int pfd[2];
	char buf[4096];
	FILE *f;

	xassert(job->env);
	if (path == NULL || path[0] == '\0')
		return 0;

	debug("[job %u] attempting to run %s [%s]", job->jobid, name, path);

	if (access(path, R_OK | X_OK) < 0) {
		error("Could not run %s [%s]: %m", name, path);
		return -1;
	}
	if (pipe(pfd) < 0) {
		error("executing %s: pipe: %m", name);
		return -1;
	}
	if ((cpid = fork()) < 0) {
		error("executing %s: fork: %m", name);
		return -1;
	}
	if (cpid == 0) {
		char *argv[2];

		argv[0] = xstrdup(path);
		argv[1] = NULL;
		close(1);
		if (dup(pfd[1]) == -1)
			error("couldn't do the dup: %m");
		close(2);
		close(0);
		close(pfd[0]);
		close(pfd[1]);
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(path, argv, job->env);
		error("execve(%s): %m", path);
		exit(127);
	}

	close(pfd[1]);
	f = fdopen(pfd[0], "r");
	if (f == NULL) {
		error("Cannot open pipe device");
		log_fini();
		exit(1);
	}
	while (feof(f) == 0) {
		if (fgets(buf, sizeof(buf) - 1, f) != NULL) {
			_proc_stdout(buf, job);
		}
	}
	fclose(f);

	while (1) {
		rc = waitpid(cpid, &status, 0);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			return 0;
		} else  {
			killpg(cpid, SIGKILL);  /* kill children too */
			return status;
		}
	}

	/* NOTREACHED */
}

/* Given a program name, translate it to a fully qualified pathname
 * as needed based upon the PATH environment variable */
static char *
_build_path(char* fname, char **prog_env)
{
	int i;
	char *path_env = NULL, *dir;
	char *file_name, *file_path;
	struct stat stat_buf;
	int len = 256;

	file_name = (char *)xmalloc(len);
	/* make copy of file name (end at white space) */
	snprintf(file_name, len, "%s", fname);
	for (i=0; i < len; i++) {
		if (file_name[i] == '\0')
			break;
		if (!isspace(file_name[i]))
			continue;
		file_name[i] = '\0';
		break;
	}

	/* check if already absolute path */
	if (file_name[0] == '/')
		return file_name;
	if (file_name[0] == '.') {
		file_path = (char *)xmalloc(len);
		dir = (char *)xmalloc(len);
		if (!getcwd(dir, len))
			error("getcwd failed: %m");
		snprintf(file_path, len, "%s/%s", dir, file_name);
		xfree(file_name);
		xfree(dir);
		return file_path;
	}

	/* search for the file using PATH environment variable */
	for (i=0; ; i++) {
		if (prog_env[i] == NULL)
			return file_name;
		if (strncmp(prog_env[i], "PATH=", 5))
			continue;
		path_env = xstrdup(&prog_env[i][5]);
		break;
	}

	file_path = (char *)xmalloc(len);
	dir = strtok(path_env, ":");
	while (dir) {
		snprintf(file_path, len, "%s/%s", dir, file_name);
		if (stat(file_path, &stat_buf) == 0)
			break;
		dir = strtok(NULL, ":");
	}
	if (dir == NULL)	/* not found */
		snprintf(file_path, len, "%s", file_name);

	xfree(file_name);
	xfree(path_env);
	return file_path;
}

static int
_setup_mpi(stepd_step_rec_t *job, int ltaskid)
{
	mpi_plugin_task_info_t info[1];

	info->jobid = job->jobid;
	info->stepid = job->stepid;
	info->nnodes = job->nnodes;
	info->nodeid = job->nodeid;
	info->ntasks = job->ntasks;
	info->ltasks = job->node_tasks;
	info->gtaskid = job->task[ltaskid]->gtid;
	info->ltaskid = job->task[ltaskid]->id;
	info->self = job->envtp->self;
	info->client = job->envtp->cli;

	return mpi_hook_slurmstepd_task(info, &job->env);
}


/*
 *  Current process is running as the user when this is called.
 */
void
exec_task(stepd_step_rec_t *job, int i)
{
	uint32_t *gtids;		/* pointer to arrary of ranks */
	int fd, j;
	stepd_step_task_info_t *task = job->task[i];
	char **tmp_env;

	if (i == 0)
		_make_tmpdir(job);

	gtids = xmalloc(job->node_tasks * sizeof(uint32_t));
	for (j = 0; j < job->node_tasks; j++)
		gtids[j] = job->task[j]->gtid;
	job->envtp->sgtids = _uint32_array_to_str(job->node_tasks, gtids);
	xfree(gtids);

	job->envtp->jobid = job->jobid;
	job->envtp->stepid = job->stepid;
	job->envtp->nodeid = job->nodeid;
	job->envtp->cpus_on_node = job->cpus;
	job->envtp->procid = task->gtid;
	job->envtp->localid = task->id;
	job->envtp->task_pid = getpid();
	job->envtp->distribution = job->task_dist;
	job->envtp->cpu_bind = xstrdup(job->cpu_bind);
	job->envtp->cpu_bind_type = job->cpu_bind_type;
	job->envtp->cpu_freq = job->cpu_freq;
	job->envtp->mem_bind = xstrdup(job->mem_bind);
	job->envtp->mem_bind_type = job->mem_bind_type;
	job->envtp->distribution = -1;
	job->envtp->ckpt_dir = xstrdup(job->ckpt_dir);
	job->envtp->batch_flag = job->batch;
	job->envtp->uid = job->uid;
	job->envtp->user_name = xstrdup(job->user_name);

	/* Modify copy of job's environment. Do not alter in place or
	 * concurrent searches of the environment can generate invalid memory
	 * references. */
	job->envtp->env = env_array_copy((const char **) job->env);
	setup_env(job->envtp, false);
	setenvf(&job->envtp->env, "SLURMD_NODENAME", "%s", conf->node_name);
	tmp_env = job->env;
	job->env = job->envtp->env;
	env_array_free(tmp_env);
	job->envtp->env = NULL;

	xfree(job->envtp->task_count);

	if (task->argv[0] && *task->argv[0] != '/') {
		/*
		 * Normally the client (srun) expands the command name
		 * to a fully qualified path, but in --multi-prog mode it
		 * is left up to the server to search the PATH for the
		 * executable.
		 */
		task->argv[0] = _build_path(task->argv[0], job->env);
	}

	if (!job->batch) {
		if (switch_g_job_attach(job->switch_job, &job->env,
					job->nodeid, (uint32_t) i, job->nnodes,
					job->ntasks, task->gtid) < 0) {
			error("Unable to attach to interconnect: %m");
			log_fini();
			exit(1);
		}

		if (_setup_mpi(job, i) != SLURM_SUCCESS) {
			error("Unable to configure MPI plugin: %m");
			log_fini();
			exit(1);
		}
	}

	/* task-specific pre-launch activities */

	/* task plugin hook */
	if (task_g_pre_launch(job)) {
		error ("Failed task affinity setup");
		exit (1);
	}

	if (spank_user_task (job, i) < 0) {
		error ("Failed to invoke task plugin stack");
		exit (1);
	}

	if (conf->task_prolog) {
		char *my_prolog;
		slurm_mutex_lock(&conf->config_mutex);
		my_prolog = xstrdup(conf->task_prolog);
		slurm_mutex_unlock(&conf->config_mutex);
		_run_script_and_set_env("slurm task_prolog",
					my_prolog, job);
		xfree(my_prolog);
	}
	if (job->task_prolog) {
		_run_script_and_set_env("user task_prolog",
					job->task_prolog, job);
	}

	if (!job->batch)
		pdebug_stop_current(job);
	if (job->env == NULL) {
		debug("job->env is NULL");
		job->env = (char **)xmalloc(sizeof(char *));
		job->env[0] = (char *)NULL;
	}

	if (job->restart_dir) {
		info("restart from %s", job->restart_dir);
		/* no return on success */
		checkpoint_restart_task(job, job->restart_dir, task->gtid);
		error("Restart task failed: %m");
		exit(errno);
	}

	if (task->argv[0] == NULL) {
		error("No executable program specified for this task");
		exit(2);
	}

	/* Do this last so you don't worry too much about the users
	   limits including the slurmstepd in with it.
	*/
	if (set_user_limits(job) < 0) {
		debug("Unable to set user limits");
		log_fini();
		exit(5);
	}

	execve(task->argv[0], task->argv, job->env);

	/*
	 * print error message and clean up if execve() returns:
	 */
	if ((errno == ENOENT) &&
	    ((fd = open(task->argv[0], O_RDONLY)) >= 0)) {
		char buf[256], *eol;
		int sz;
		sz = read(fd, buf, sizeof(buf));
		if ((sz >= 3) && (strncmp(buf, "#!", 2) == 0)) {
			eol = strchr(buf, '\n');
			if (eol)
				eol[0] = '\0';
			else
				buf[sizeof(buf)-1] = '\0';
			error("execve(): bad interpreter(%s): %m", buf+2);
			exit(errno);
		}
	}
	error("execve(): %s: %m", task->argv[0]);
	exit(errno);
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

/*
 * Return a string representation of an array of uint32_t elements.
 * Each value in the array is printed in decimal notation and elements
 * are separated by a comma.
 *
 * Returns an xmalloc'ed string.  Free with xfree().
 */
static char *_uint32_array_to_str(int array_len, const uint32_t *array)
{
	int i;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if (array == NULL)
		return str;

	for (i = 0; i < array_len; i++) {

		if (i == array_len-1) /* last time through loop */
			sep = "";
		xstrfmtcat(str, "%u%s", array[i], sep);
	}

	return str;
}
