/*****************************************************************************\
 *  slurmd/slurmstepd/task.c - task launching functions for slurmstepd
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <assert.h>

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

#include <slurm/slurm_errno.h>

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

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
static char *_signame(int signo);
static void  _setup_spawn_io(slurmd_job_t *job);
static int   _run_script_and_set_env(const char *name, const char *path, 
				     slurmd_job_t *job);
static void  _update_env(char *buf, char ***env);


static void _setup_spawn_io(slurmd_job_t *job)
{
	srun_info_t *srun;
	int fd = -1;

	srun = list_peek(job->sruns);
	xassert(srun);
	if ((fd = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect spawn io stream: %m");
		exit(1);
	}

	if (dup2(fd, STDIN_FILENO) == -1) {
		error("dup2 over STDIN_FILENO: %m");
		exit(1);
	}
	if (dup2(fd, STDOUT_FILENO) == -1) {
		error("dup2 over STDOUT_FILENO: %m");
		exit(1);
	}
	if (dup2(fd, STDERR_FILENO) == -1) {
		error("dup2 over STDERR_FILENO: %m");
		exit(1);
	}
		
	if (fd > 2)
		(void) close(fd);
}

/* Search for "export NAME=value" records in buf and 
 * use them to add environment variables to env */
static void
_update_env(char *buf, char ***env)
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
			error("Unable to set %s environment variable", name_ptr);
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
		debug("Not running %s [%s]: %m", name, path);
		return 0;
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
		setpgrp();
		execve(path, argv, job->env);
		error("execve(): %m");
		exit(127);
	}

	close(pfd[1]);
	while ((nread = read(pfd[0], buf, sizeof(buf))) > 0) {
		buf[nread] = 0;
		//debug("read %d:%s:", nread, buf);
		_update_env(buf, &job->env);
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

/*
 *  Current process is running as the user when this is called.
 */
void
exec_task(slurmd_job_t *job, int i, int waitfd)
{
	char c;
	int rc;
	slurmd_task_info_t *t = NULL;

	if (chdir(job->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
		      job->cwd);
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			log_fini();
			exit(4);
		}
	}

	if ((!job->spawn_task) && (set_user_limits(job) < 0)) {
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

	job->envtp->jobid = job->jobid;
	job->envtp->stepid = job->stepid;
	job->envtp->nodeid = job->nodeid;
	job->envtp->cpus_on_node = job->cpus;
	job->envtp->env = job->env;
	
	t = job->task[i];
	job->envtp->procid = t->gtid;
	job->envtp->localid = t->id;
	job->envtp->task_pid = getpid();
	job->envtp->cpu_bind = xstrdup(job->cpu_bind);
	job->envtp->cpu_bind_type = job->cpu_bind_type;
	
	/* need to take this out in 1.2 */
	job->envtp->distribution = SLURM_DIST_UNKNOWN;
	setup_env(job->envtp);
	
	job->env = job->envtp->env;
	job->envtp->env = NULL;
	xfree(job->envtp->task_count);
	
	if (!job->batch) {
		if (interconnect_attach(job->switch_job, &job->env,
				job->nodeid, (uint32_t) i, job->nnodes,
				job->nprocs, job->task[i]->gtid) < 0) {
			error("Unable to attach to interconnect: %m");
			log_fini();
			exit(1);
		}

		slurmd_mpi_init (job, job->task[i]->gtid);
	
		pdebug_stop_current(job);
	}

	/* 
	 * If io_prepare_child() is moved above interconnect_attach()
	 * this causes EBADF from qsw_attach(). Why?
	 */
	if (job->spawn_task)
		_setup_spawn_io(job);
	else
		io_dup_stdio(job->task[i]);

	/* task-specific pre-launch activities */
	pre_launch(job);
	
	if (job->task_prolog) {
		_run_script_and_set_env("user task_prolog",
					job->task_prolog, job); 
	} else if (conf->task_prolog) {
		char *my_prolog;
		slurm_mutex_lock(&conf->config_mutex);
		my_prolog = xstrdup(conf->task_prolog);
		slurm_mutex_unlock(&conf->config_mutex);
		_run_script_and_set_env("slurm task_prolog",
					my_prolog, job);
		xfree(my_prolog);
	}
	

	log_fini();
	execve(job->argv[0], job->argv, job->env);

	/* 
	 * error() and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}

/*
 *  Translate a signal number to recognizable signal name.
 *    Returns signal name or "signal <num>" 
 */
static char *
_signame(int signo)
{
	int i;
	static char str[10];
	static struct {
		int s_num;
		char * s_name;
	} sigtbl[] = {   
		{SIGHUP, "SIGHUP" }, {SIGINT, "SIGINT" }, {SIGQUIT,"SIGQUIT"},
		{SIGABRT,"SIGABRT"}, {SIGUSR1,"SIGUSR1"}, {SIGUSR2,"SIGUSR2"},
		{SIGPIPE,"SIGPIPE"}, {SIGALRM,"SIGALRM"}, {SIGTERM,"SIGTERM"},
		{SIGCHLD,"SIGCHLD"}, {SIGCONT,"SIGCONT"}, {SIGSTOP,"SIGSTOP"},
		{SIGTSTP,"SIGTSTP"}, {SIGTTIN,"SIGTTIN"}, {SIGTTOU,"SIGTTOU"},
		{SIGURG, "SIGURG" }, {SIGXCPU,"SIGXCPU"}, {SIGXFSZ,"SIGXFSZ"},
		{0, NULL}
	};

	for (i = 0; ; i++) {
		if ( sigtbl[i].s_num == signo )
			return sigtbl[i].s_name;
		if ( sigtbl[i].s_num == 0 )
			break;
	}

	snprintf(str, 9, "signal %d", signo);
	return str;
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
