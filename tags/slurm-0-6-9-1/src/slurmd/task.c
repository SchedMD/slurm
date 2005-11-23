/*****************************************************************************\
 *  slurmd/task.c - task launching functions for slurmd
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"

#include "src/slurmd/task.h"
#include "src/slurmd/ulimits.h"
#include "src/slurmd/io.h"
#include "src/slurmd/proctrack.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/pdebug.h"


/*
 * Static prototype definitions.
 */
static void  _make_tmpdir(slurmd_job_t *job);
static char *_signame(int signo);
static void  _cleanup_file_descriptors(slurmd_job_t *job);
static void  _setup_spawn_io(slurmd_job_t *job);


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

/* Close write end of stdin (at the very least)
 */
static void
_cleanup_file_descriptors(slurmd_job_t *j)
{
	int i;
	for (i = 0; i < j->ntasks; i++) {
		slurmd_task_info_t *t = j->task[i];
		/*
		 * Ignore errors on close()
		 */
		close(t->pin[1]); 
		close(t->pout[0]);
		close(t->perr[0]);
	}
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
			exit(4);
		}
	}

	if ((!job->spawn_task) && (set_user_limits(job) < 0)) {
		debug("Unable to set user limits");
		exit(5);
	}

	if (i == 0)
		_make_tmpdir(job);


        /*
	 * Stall exec until all tasks have joined the same process group
	 */
        if ((rc = read (waitfd, &c, sizeof (c))) != 1) {
	        error ("_exec_task read failed, fd = %d, rc=%d: %m", waitfd, rc);
		exit(1);
	}
	close(waitfd);

	_cleanup_file_descriptors(job);

	job->envtp->jobid = job->jobid;
	job->envtp->stepid = job->stepid;
	job->envtp->nodeid = job->nodeid;
	job->envtp->cpus_on_node = job->cpus;
	job->envtp->env = job->env;
	
	t = job->task[i];
	job->envtp->procid = t->gtid;
	job->envtp->localid = t->id;
		
	setup_env(job->envtp);
	job->env = job->envtp->env;
	job->envtp->env = NULL;
	xfree(job->envtp->task_count);
	
	if (!job->batch) {
		if (interconnect_attach(job->switch_job, &job->env,
				job->nodeid, (uint32_t) i, job->nnodes,
				job->nprocs, job->task[i]->gtid) < 0) {
			error("Unable to attach to interconnect: %m");
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
		io_prepare_child(job->task[i]);

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
