/*****************************************************************************\
 *  batch_mgr.c - functions for batch job management (spawn and monitor job)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define EXTREME_DEBUG 1
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/common/util_signals.h>

#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>
#include <src/slurmd/circular_buffer.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>
#include <src/slurmd/interconnect.h>

#include <src/common/util_signals.h>
#include <src/common/log.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_defs.h>

#include <src/slurmd/batch_mgr.h>
#include <src/slurmd/slurmd.h>

void dump_batch_desc (batch_job_launch_msg_t *batch_job_launch_msg);
static void *batch_exec_thread(void *arg);

/* launch_batch_job - establish the environment and launch a batch job script */
int 
launch_batch_job (batch_job_launch_msg_t *batch_job_launch_msg)
{
#if EXTREME_DEBUG
	dump_batch_desc (batch_job_launch_msg);
#endif
	batch_exec_thread (batch_job_launch_msg);

	return SLURM_SUCCESS;
}

void
dump_batch_desc (batch_job_launch_msg_t *batch_job_launch_msg)
{
	int i;

	debug3 ("Launching batch job: job_id=%u, user_id=%u, nodes=%s",
		batch_job_launch_msg->job_id, batch_job_launch_msg->user_id, 
		batch_job_launch_msg->nodes);
	debug3 ("    work_dir=%s, stdin=%s",
		batch_job_launch_msg->work_dir, batch_job_launch_msg->stdin);
	debug3 ("    stdout=%s, stderr=%s",
		batch_job_launch_msg->stdout, batch_job_launch_msg->stderr);
	debug3 ("    script=%s", batch_job_launch_msg->script);
	for (i=0; i<batch_job_launch_msg->argc; i++) {
		debug3 ("    argv[%d]=%s", i, batch_job_launch_msg->argv[i]);
	}
	for (i=0; i<batch_job_launch_msg->env_size; i++) {
		debug3 ("    environment[%d]=%s", i, batch_job_launch_msg->environment[i]);
	}
}

void *batch_exec_thread(void *arg)
{
	batch_job_launch_msg_t *batch_job_launch_msg = arg;
	int rc;
	int cpid;
	struct passwd *pwd;
	int task_return_code;
	int local_errno;
	log_options_t log_opts_def = LOG_OPTS_STDERR_ONLY;


#define FORK_ERROR -1
#define CHILD_PROCCESS 0
	switch ((cpid = fork())) {
	case FORK_ERROR:
		break;

	case CHILD_PROCCESS:
		/* log init stuff */
		log_init("slurmd", log_opts_def, 0, NULL);

		unblock_all_signals();

		posix_signal_ignore(SIGTTOU);	/* ignore tty output */
		posix_signal_ignore(SIGTTIN);	/* ignore tty input */
		posix_signal_ignore(SIGTSTP);	/* ignore user */

		/* get passwd file info */
		if ((pwd = getpwuid(batch_job_launch_msg->user_id)) == NULL) {
			error("user id not found in passwd file");
			_exit(SLURM_FAILURE);
		}

		/* setgid */
		if ((rc = setgid(pwd->pw_gid)) < 0) {
			error("setgid failed: %m ");
		//	_exit(SLURM_FAILURE);
		}

		/* initgroups */
		if (( getuid() == (uid_t)0 ) &&
		    ( initgroups(pwd->pw_name, pwd->pw_gid) ) < 0) {
			error("initgroups() failed: %m");
			//_exit(SLURM_FAILURE);
		}

		/* setuid */
		if ((rc = setuid(batch_job_launch_msg->user_id)) < 0) {
			error("setuid() failed: %m");
			_exit(SLURM_FAILURE);
		}

		/* run bash and cmdline */
		if ((chdir(batch_job_launch_msg->work_dir)) < 0) {
			error("cannot chdir to `%s,' going to /tmp instead",
					batch_job_launch_msg->work_dir);
			if ((chdir("/tmp")) < 0) {
				error("couldn't chdir to `/tmp' either. dying.");
				_exit(SLURM_FAILURE);
			}
		}
		close ( STDIN_FILENO ) ;
		if ((dup2(open(batch_job_launch_msg->stdin,O_RDONLY),STDIN_FILENO)) < 0 ) {
			error("cannot open stdin file '%s,'", batch_job_launch_msg->stdin );
		}

		close ( STDOUT_FILENO ) ;
		if ((dup2(open(batch_job_launch_msg->stdout,O_RDWR|O_TRUNC|O_APPEND|O_CREAT, 0644 ),STDOUT_FILENO)) < 0 ) {
			error("cannot open stdout file '%s,'", batch_job_launch_msg->stdout );
		}

		close ( STDERR_FILENO ) ;
		if ((dup2(open(batch_job_launch_msg->stderr,O_RDWR|O_TRUNC|O_APPEND|O_CREAT, 0644 ),STDERR_FILENO)) < 0 ) {
			error("cannot open stderr file '%s,'", batch_job_launch_msg->stderr );
		}
		
		execve(batch_job_launch_msg->argv[0], batch_job_launch_msg->argv, batch_job_launch_msg->environment );

		/* error if execve returns
		 * clean up */
		error("execve(): %s: %m", batch_job_launch_msg->argv[0]);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		local_errno = errno;
		_exit(local_errno);
		break;

	default:		/*parent proccess */
		debug("forked pid %ld", cpid);
		//task_start->exec_pid = cpid;
		/* order below is very important 
		 * deadlock can occur if you mess with it - ask me how I know :)
		 */

		debug3("calling waitpid(%ld)", cpid);
		/* 2   */ waitpid(cpid, &task_return_code, 0);
		break;
	}
	return (void *) SLURM_SUCCESS; /* XXX: I think this is wrong */
}

