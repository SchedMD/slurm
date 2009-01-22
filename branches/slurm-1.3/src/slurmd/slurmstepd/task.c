/*****************************************************************************\
 *  slurmd/slurmstepd/task.c - task launching functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_PTY_H
#  include <pty.h>
#endif

#ifdef HAVE_UTMP_H
#  include <utmp.h>
#endif

#include <sys/resource.h>

#include <slurm/slurm_errno.h>

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/mpi.h"
#include "src/common/xmalloc.h"
#include "src/common/plugstack.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/ulimits.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/pdebug.h"

/*
 * Static prototype definitions.
 */
static void  _make_tmpdir(slurmd_job_t *job);
static void  _print_stdout(char *buf);
static int   _run_script_and_set_env(const char *name, const char *path, 
				     slurmd_job_t *job);
static void  _update_env(char *buf, char ***env);
static char *_uint32_array_to_str(int array_len, const uint32_t *array);

/* Search for "export NAME=value" records in buf and 
 * use them to add environment variables to env */
static void _update_env(char *buf, char ***env)
{
	char *tmp_ptr, *name_ptr, *val_ptr, *buf_ptr = buf;

	while ((tmp_ptr = strstr(buf_ptr, "export"))) {
		buf_ptr += 6;
		while (isspace(buf_ptr[0]))
			buf_ptr++;
		if (buf_ptr[0] == '=')	/* mal-formed */
			continue;
		name_ptr = buf_ptr;	/* start of env var name */
		while ((buf_ptr[0] != '=') && (buf_ptr[0] != '\0'))
			buf_ptr++;
		if (buf_ptr[0] == '\0')	/* mal-formed */
			continue;
		buf_ptr[0] = '\0';	/* end of env var name */
		buf_ptr++;
		val_ptr = buf_ptr;	/* start of env var value */
		while ((!isspace(buf_ptr[0])) && (buf_ptr[0] != '\0'))
			buf_ptr++;
		if (isspace(buf_ptr[0])) {
			buf_ptr[0] = '\0';/* end of env var value */
			buf_ptr++;
		}
		debug("name:%s:val:%s:",name_ptr,val_ptr);
		if (setenvf(env, name_ptr, "%s", val_ptr))
			error("Unable to set %s environment variable", 
			      name_ptr);
	}		
}

/* Search for "print <whatever>" records in buf and 
 * write that to the job's stdout */
static void _print_stdout(char *buf)
{
	char *tmp_ptr, *buf_ptr = buf;

	while ((tmp_ptr = strstr(buf_ptr, "print "))) {
		if ((tmp_ptr != buf_ptr) && (tmp_ptr[-1] != '\n')) {
			/* Skip "print " if not at start of a line */
			buf_ptr +=6;
			continue;
		}
		buf_ptr = tmp_ptr + 6;
		tmp_ptr = strchr(buf_ptr, '\n');
		if (tmp_ptr) {
			write(1, buf_ptr, (tmp_ptr - buf_ptr + 1));
			buf_ptr = tmp_ptr + 1;
		} else {
			write(1, buf_ptr, strlen(buf_ptr));
			break;
		}
	}		
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
_run_script_and_set_env(const char *name, const char *path, slurmd_job_t *job)
{
	int status, rc, nread;
	pid_t cpid;
	int pfd[2];
	char buf[4096];

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
		dup(pfd[1]);
		close(2);
		close(0);
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(path, argv, job->env);
		error("execve(): %m");
		exit(127);
	}

	close(pfd[1]);
	while ((nread = read(pfd[0], buf, sizeof(buf))) > 0) {
		buf[nread] = 0;
		//debug("read %d:%s:", nread, buf);
		_update_env(buf, &job->env);
		_print_stdout(buf);
	}

	close(pfd[0]);
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
_setup_mpi(slurmd_job_t *job, int ltaskid)
{
	mpi_plugin_task_info_t info[1];

	info->jobid = job->jobid;
	info->stepid = job->stepid;
	info->nnodes = job->nnodes;
	info->nodeid = job->nodeid;
	info->ntasks = job->nprocs;
	info->ltasks = job->ntasks;
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
exec_task(slurmd_job_t *job, int i, int waitfd)
{
	char c;
	uint32_t *gtids;		/* pointer to arrary of ranks */
	int fd, j;
	int rc;
	slurmd_task_info_t *task = job->task[i];

#ifdef HAVE_PTY_H
	/* Execute login_tty() before setpgid() calls */
	if (job->pty && (task->gtid == 0)) {
		if (login_tty(task->stdin_fd))
			error("login_tty: %m");
		else
			debug3("login_tty good");
	}
#endif

	if (set_user_limits(job) < 0) {
		debug("Unable to set user limits");
		log_fini();
		exit(5);
	}

	if (i == 0)
		_make_tmpdir(job);

        /*
	 * Stall exec until all tasks have joined the same process group
	 */
        if ((rc = read (waitfd, &c, sizeof (c))) != 1) {
	        error ("_exec_task read failed, fd = %d, rc=%d: %m", waitfd, rc);
		log_fini();
		exit(1);
	}
	close(waitfd);

	gtids = xmalloc(job->ntasks * sizeof(uint32_t));
	for (j = 0; j < job->ntasks; j++)
		gtids[j] = job->task[j]->gtid;
	job->envtp->sgtids = _uint32_array_to_str(job->ntasks, gtids);
	xfree(gtids);

	job->envtp->jobid = job->jobid;
	job->envtp->stepid = job->stepid;
	job->envtp->nodeid = job->nodeid;
	job->envtp->cpus_on_node = job->cpus;
	job->envtp->env = job->env;
	job->envtp->procid = task->gtid;
	job->envtp->localid = task->id;
	job->envtp->task_pid = getpid();
	job->envtp->distribution = job->task_dist;
	job->envtp->plane_size   = job->plane_size;
	job->envtp->cpu_bind = xstrdup(job->cpu_bind);
	job->envtp->cpu_bind_type = job->cpu_bind_type;
	job->envtp->mem_bind = xstrdup(job->mem_bind);
	job->envtp->mem_bind_type = job->mem_bind_type;
	job->envtp->distribution = -1;
	job->envtp->ckpt_path = xstrdup(job->ckpt_path);
	setup_env(job->envtp);
	setenvf(&job->envtp->env, "SLURMD_NODENAME", "%s", conf->node_name);
	job->env = job->envtp->env;
	job->envtp->env = NULL;
	xfree(job->envtp->task_count);

	if (job->multi_prog && task->argv[0]) {
		/*
		 * Normally the client (srun) expands the command name
		 * to a fully qualified path, but in --multi-prog mode it
		 * is left up to the server to search the PATH for the
		 * executable.
		 */
		task->argv[0] = _build_path(task->argv[0], job->env);
	}
	
	if (!job->batch) {
		if (interconnect_attach(job->switch_job, &job->env,
				job->nodeid, (uint32_t) i, job->nnodes,
				job->nprocs, task->gtid) < 0) {
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

#ifdef HAVE_PTY_H
	if (job->pty && (task->gtid == 0)) {
		/* Need to perform the login_tty() before all tasks
		 * register and the process groups are reset, otherwise
		 * login_tty() gets disabled */
	} else
#endif
		io_dup_stdio(task);

	/* task-specific pre-launch activities */

	if (spank_user_task (job, i) < 0) {
		error ("Failed to invoke task plugin stack");
		exit (1);
	}

	/* task plugin hook */
	pre_launch(job);
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

	if (task->argv[0] == NULL) {
		error("No executable program specified for this task");
		exit(2);
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
_make_tmpdir(slurmd_job_t *job)
{
	char *tmpdir;

	if (!(tmpdir = getenvp(job->env, "TMPDIR")))
		return;

	if ((mkdir(tmpdir, 0700) < 0) && (errno != EEXIST))
		error ("Unable to create TMPDIR [%s]: %m", tmpdir);

	return;
}

/*
 * Return a string representation of an array of uint32_t elements.
 * Each value in the array is printed in decimal notation and elements
 * are seperated by a comma.  
 * 
 * Returns an xmalloc'ed string.  Free with xfree().
 */
static char *_uint32_array_to_str(int array_len, const uint32_t *array)
{
	int i;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if(array == NULL)
		return str;

	for (i = 0; i < array_len; i++) {
  
		if (i == array_len-1) /* last time through loop */
			sep = "";
		xstrfmtcat(str, "%u%s", array[i], sep);
	}
	
	return str;
}


