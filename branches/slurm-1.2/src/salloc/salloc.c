/*****************************************************************************\
 *  salloc.c - Request a SLURM job allocation and
 *             launch a user-specified command.
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-226842.
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

#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/read_config.h"
#include "src/common/env.h"

#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"
#include "src/salloc/msg.h"

#define MAX_RETRIES 3

char **command_argv;
int command_argc;
pid_t command_pid = -1;
enum possible_allocation_states allocation_state = NOT_GRANTED;
pthread_mutex_t allocation_state_lock = PTHREAD_MUTEX_INITIALIZER;

static bool exit_flag = false;
static bool allocation_interrupted = false;
static uint32_t pending_job_id = 0;

static int fill_job_desc_from_opts(job_desc_msg_t *desc);
static void ring_terminal_bell(void);
static int fork_command(char **command);
static void _pending_callback(uint32_t job_id);
static void _ignore_signal(int signo);
static void _exit_on_signal(int signo);
static void _signal_while_allocating(int signo);

int main(int argc, char *argv[])
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t desc;
	resource_allocation_response_msg_t *alloc;
	time_t before, after;
	salloc_msg_thread_t *msg_thr;
	char **env = NULL;
	int status = 0;
	int errnum = 0;
	int retries = 0;
	pid_t pid;
	pid_t rc_pid = 0;
	int rc = 0;
	static char *msg = "Slurm job queue full, sleeping and retrying.";

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	if (initialize_and_process_args(argc, argv) < 0) {
		fatal("salloc parameter parsing");
	}
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	if (opt.cwd && chdir(opt.cwd)) {
		error("chdir(%s): %m", opt.cwd);
		exit(1);
	}

	if (opt.get_user_env_time >= 0) {
		struct passwd *pw;
		pw = getpwuid(opt.uid);
		if (pw == NULL) {
			error("getpwuid(%u): %m", (uint32_t)opt.uid);
			exit(1);
		}
		env = env_array_user_default(pw->pw_name,
					     opt.get_user_env_time,
					     opt.get_user_env_mode);
		if (env == NULL)
			exit(1);    /* error already logged */
	}

	/*
	 * Request a job allocation
	 */
	slurm_init_job_desc_msg(&desc);
	if (fill_job_desc_from_opts(&desc) == -1) {
		exit(1);
	}

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = msg_thr_create(&desc.other_port);
	desc.other_hostname = xshort_hostname();

	xsignal(SIGHUP, _signal_while_allocating);
	xsignal(SIGINT, _signal_while_allocating);
	xsignal(SIGQUIT, _signal_while_allocating);
	xsignal(SIGPIPE, _signal_while_allocating);
	xsignal(SIGTERM, _signal_while_allocating);
	xsignal(SIGUSR1, _signal_while_allocating);
	xsignal(SIGUSR2, _signal_while_allocating);

	before = time(NULL);
	while ((alloc = slurm_allocate_resources_blocking(&desc, opt.max_wait,
					_pending_callback)) == NULL) {
		if ((errno != ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) ||
		    (retries >= MAX_RETRIES))
			break;
		if (retries == 0)
			error(msg);
		else
			debug(msg);
		sleep (++retries);
	}

	if (alloc == NULL) {
		if (allocation_interrupted) {
			/* cancelled by signal */
		} else if (errno == EINTR) {
			error("Interrupted by signal."
			      "  Allocation request rescinded.");
		} else {
			error("Failed to allocate resources: %m");
		}
		msg_thr_destroy(msg_thr);
		exit(1);
	}
	after = time(NULL);

	xsignal(SIGHUP, _exit_on_signal);
	xsignal(SIGINT, _ignore_signal);
	xsignal(SIGQUIT, _ignore_signal);
	xsignal(SIGPIPE, _ignore_signal);
	xsignal(SIGTERM, _ignore_signal);
	xsignal(SIGUSR1, _ignore_signal);
	xsignal(SIGUSR2, _ignore_signal);

	/*
	 * Allocation granted!
	 */
	info("Granted job allocation %d", alloc->job_id);
	if (opt.bell == BELL_ALWAYS
	    || (opt.bell == BELL_AFTER_DELAY
		&& ((after - before) > DEFAULT_BELL_DELAY))) {
		ring_terminal_bell();
	}
	if (opt.no_shell)
		exit(0);
	if (allocation_interrupted) {
		/* salloc process received a signal after
		 * slurm_allocate_resources_blocking returned with the
		 * allocation, but before the new signal handlers were
		 * registered.
		 */
		goto relinquish;
	}

	/*
	 * Run the user's command.
	 */
	env_array_for_job(&env, alloc);
	/* Add default task count for srun, if not already set */
	if (opt.nprocs_set)
		env_array_append_fmt(&env, "SLURM_NPROCS", "%d", opt.nprocs);
	if (opt.overcommit) {
		env_array_append_fmt(&env, "SLURM_OVERCOMMIT", "%d", 
			opt.overcommit);
	}
	env_array_set_environment(env);
	env_array_free(env);
	pthread_mutex_lock(&allocation_state_lock);
	if (allocation_state == REVOKED) {
		error("Allocation was revoked before command could be run");
		pthread_mutex_unlock(&allocation_state_lock);
		return 1;
	} else {
		allocation_state = GRANTED;
		command_pid = pid = fork_command(command_argv);
	}
	pthread_mutex_unlock(&allocation_state_lock);

	/*
	 * Wait for command to exit, OR for waitpid to be interrupted by a
	 * signal.  Either way, we are going to release the allocation next.
	 */
	if (pid > 0) {
		while ((rc_pid = waitpid(pid, &status, 0)) == -1) {
			if (exit_flag)
				break;
			if (errno == EINTR)
				continue;
		}
		errnum = errno;
		if (rc_pid == -1 && errnum != EINTR)
			error("waitpid for %s failed: %m", command_argv[0]);
	}

	/*
	 * Relinquish the job allocation (if not already revoked).
	 */
relinquish:
	pthread_mutex_lock(&allocation_state_lock);
	if (allocation_state != REVOKED) {
		info("Relinquishing job allocation %d", alloc->job_id);
		if (slurm_complete_job(alloc->job_id, 0) != 0)
			error("Unable to clean up job allocation %d: %m",
			      alloc->job_id);
		else
			allocation_state = REVOKED;
	}
	pthread_mutex_unlock(&allocation_state_lock);

	slurm_free_resource_allocation_response_msg(alloc);
	msg_thr_destroy(msg_thr);

	/*
	 * Figure out what return code we should use.  If the user's command
	 * exitted normally, return the user's return code.
	 */
	rc = 1;
	if (rc_pid != -1) {
		if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			verbose("Command \"%s\" was terminated by signal %d",
				command_argv[0], WTERMSIG(status));
		}
	}

	return rc;
}


/* Returns 0 on success, -1 on failure */
static int fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	desc->contiguous = opt.contiguous ? 1 : 0;
	desc->features = opt.constraints;
	desc->immediate = opt.immediate ? 1 : 0;
	desc->name = opt.job_name;
	desc->req_nodes = opt.nodelist;
	desc->exc_nodes = opt.exc_nodes;
	desc->partition = opt.partition;
	desc->min_nodes = opt.min_nodes;
	if (opt.max_nodes)
		desc->max_nodes = opt.max_nodes;
	desc->user_id = opt.uid;
	desc->group_id = opt.gid;
	desc->dependency = opt.dependency;
	if (opt.nice)
		desc->nice = NICE_OFFSET + opt.nice;
	desc->mail_type = opt.mail_type;
	if (opt.mail_user)
		desc->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		desc->begin_time = opt.begin;
	if (opt.account)
		desc->account = xstrdup(opt.account);
	if (opt.comment)
		desc->comment = xstrdup(opt.comment);

	if (opt.hold)
		desc->priority     = 0;
#if SYSTEM_DIMENSIONS
	if (opt.geometry[0] > 0) {
		int i;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			desc->geometry[i] = opt.geometry[i];
	}
#endif
	if (opt.conn_type != -1)
		desc->conn_type = opt.conn_type;
	if (opt.reboot)
		desc->reboot = 1;
	if (opt.no_rotate)
		desc->rotate = 0;
	if (opt.blrtsimage)
		desc->blrtsimage = xstrdup(opt.blrtsimage);
	if (opt.linuximage)
		desc->linuximage = xstrdup(opt.linuximage);
	if (opt.mloaderimage)
		desc->mloaderimage = xstrdup(opt.mloaderimage);
	if (opt.ramdiskimage)
		desc->ramdiskimage = xstrdup(opt.ramdiskimage);
	if (opt.mincpus > -1)
		desc->job_min_procs = opt.mincpus;
	if (opt.minsockets > -1)
		desc->job_min_sockets = opt.minsockets;
	if (opt.mincores > -1)
		desc->job_min_cores = opt.mincores;
	if (opt.minthreads > -1)
		desc->job_min_threads = opt.minthreads;
	if (opt.realmem > -1)
		desc->job_min_memory = opt.realmem;
	if (opt.tmpdisk > -1)
		desc->job_min_tmp_disk = opt.tmpdisk;
	if (opt.overcommit) {
		desc->num_procs = opt.min_nodes;
		desc->overcommit = opt.overcommit;
	} else
		desc->num_procs = opt.nprocs * opt.cpus_per_task;
	if (opt.nprocs_set)
		desc->num_tasks = opt.nprocs;
	if (opt.cpus_set)
		desc->cpus_per_task = opt.cpus_per_task;
	if (opt.no_kill)
		desc->kill_on_node_fail = 0;
	if (opt.time_limit  != NO_VAL)
		desc->time_limit = opt.time_limit;
	desc->shared = opt.shared;
	desc->job_id = opt.jobid;

	return 0;
}

static void ring_terminal_bell(void)
{
        if (isatty(STDOUT_FILENO)) {
                fprintf(stdout, "\a");
                fflush(stdout);
        }
}

/* returns the pid of the forked command, or <0 on error */
static pid_t fork_command(char **command)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		error("fork failed: %m");
	} else if (pid == 0) {
		/* child */
		execvp(command[0], command);

		/* should only get here if execvp failed */
		error("Unable to exec command \"%s\"", command[0]);
		exit(1);
	}
	/* parent returns */
	return pid;
}

static void _pending_callback(uint32_t job_id)
{
	info("Pending job allocation %u", job_id);
	pending_job_id = job_id;
}

static void _signal_while_allocating(int signo)
{
	allocation_interrupted = true;
	if (pending_job_id != 0) {
		slurm_complete_job(pending_job_id, 0);
	}
}

static void _ignore_signal(int signo)
{
	/* do nothing */
}

static void _exit_on_signal(int signo)
{
	exit_flag = true;
}
