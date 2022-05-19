/*****************************************************************************\
 *  salloc.c - Request a Slurm job allocation and
 *             launch a user-specified command.
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
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

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/resource.h> /* for struct rlimit */
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/cli_filter.h"
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getpgid(pid_t pid);
#endif

#define MAX_RETRIES	10
#define POLL_SLEEP	3	/* retry interval in seconds  */

char *argvzero = NULL;
char **command_argv;
int command_argc;
pid_t command_pid = -1;
char *work_dir = NULL;
static int is_interactive;

enum possible_allocation_states allocation_state = NOT_GRANTED;
pthread_cond_t  allocation_state_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t allocation_state_lock = PTHREAD_MUTEX_INITIALIZER;

static bool allocation_interrupted = false;
static bool allocation_revoked = false;
static bool exit_flag = false;
static bool is_het_job = false;
static bool suspend_flag = false;
static uint32_t my_job_id = 0;
static time_t last_timeout = 0;
static struct termios saved_tty_attributes;
static int het_job_limit = 0;
static bool _cli_filter_post_submit_run = false;

static void _exit_on_signal(int signo);
static int  _fill_job_desc_from_opts(job_desc_msg_t *desc);
static pid_t _fork_command(char **command);
static void _forward_signal(int signo);
static void _job_complete_handler(srun_job_complete_msg_t *msg);
static void _job_suspend_handler(suspend_msg_t *msg);
static void _match_job_name(job_desc_msg_t *desc_last, List job_req_list);
static void _node_fail_handler(srun_node_fail_msg_t *msg);
static void _pending_callback(uint32_t job_id);
static int  _proc_alloc(resource_allocation_response_msg_t *alloc);
static void _ring_terminal_bell(void);
static int  _set_cluster_name(void *x, void *arg);
static void _set_exit_code(void);
static void _set_rlimits(char **env);
static void _set_spank_env(void);
static void _set_submit_dir_env(void);
static void _signal_while_allocating(int signo);
static void _timeout_handler(srun_timeout_msg_t *msg);
static void _user_msg_handler(srun_user_msg_t *msg);
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc);
static void _salloc_cli_filter_post_submit(uint32_t jobid, uint32_t stepid);

bool salloc_shutdown = false;
/* Signals that are considered terminal before resource allocation. */
int sig_array[] = {
	SIGHUP, SIGINT, SIGQUIT, SIGPIPE,
	SIGTERM, SIGUSR1, SIGUSR2, 0
};

static void _reset_input_mode (void)
{
	/* SIGTTOU needs to be blocked per the POSIX spec:
	 * http://pubs.opengroup.org/onlinepubs/009695399/functions/tcsetattr.html
	 */
	int sig_block[] = { SIGTTOU, SIGTTIN, 0 };
	xsignal_block (sig_block);
	tcsetattr (STDIN_FILENO, TCSANOW, &saved_tty_attributes);
	/* If salloc was run as interactive, with job control, reset the
	 * foreground process group of the terminal to the process group of
	 * the parent pid before exiting */
	if (is_interactive)
		tcsetpgrp(STDIN_FILENO, getpgid(getppid()));
}

static int _set_cluster_name(void *x, void *arg)
{	job_desc_msg_t *desc = (job_desc_msg_t *) x;
	desc->origin_cluster = xstrdup(slurm_conf.cluster_name);
	return 0;
}

static int _copy_other_port(void *x, void *arg)
{
	job_desc_msg_t *desc = x;
	desc->other_port = *(uint16_t *)arg;

	return SLURM_SUCCESS;
}

int main(int argc, char **argv)
{
	static bool env_cache_set = false;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t *desc = NULL, *first_job = NULL;
	List job_req_list = NULL, job_resp_list = NULL;
	resource_allocation_response_msg_t *alloc = NULL;
	time_t before, after;
	allocation_msg_thread_t *msg_thr = NULL;
	char **env = NULL;
	int status = 0;
	int retries = 0;
	pid_t pid  = getpid();
	pid_t tpgid = 0;
	pid_t rc_pid = 0;
	int i, j, rc = 0;
	uint32_t num_tasks = 0;
	bool het_job_fini = false;
	int het_job_argc, het_job_inx, het_job_argc_off;
	char **het_job_argv;
	static char *msg = "Slurm job queue full, sleeping and retrying.";
	slurm_allocation_callbacks_t callbacks;
	ListIterator iter_req, iter_resp;

	slurm_conf_init(NULL);
	log_init(xbasename(argv[0]), logopt, 0, NULL);
	argvzero = argv[0];
	_set_exit_code();

	if (spank_init_allocator() < 0) {
		error("Failed to initialize plugin stack");
		exit(error_exit);
	}

	/* Be sure to call spank_fini when salloc exits
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");


	het_job_argc = argc;
	het_job_argv = argv;
	for (het_job_inx = 0; !het_job_fini; het_job_inx++) {
		het_job_argc_off = -1;
		if (initialize_and_process_args(het_job_argc, het_job_argv,
						&het_job_argc_off,
						het_job_inx) < 0) {
			error("salloc parameter parsing");
			exit(error_exit);
		}
		if ((het_job_argc_off >= 0) &&
		    (het_job_argc_off < het_job_argc) &&
		    !xstrcmp(het_job_argv[het_job_argc_off], ":")) {
			/* het_job_argv[0] moves from "salloc" to ":" */
			het_job_argc -= het_job_argc_off;
			het_job_argv += het_job_argc_off;
		} else
			het_job_fini = true;

		/* reinit log with new verbosity (if changed by command line) */
		if (opt.verbose || opt.quiet) {
			logopt.stderr_level += opt.verbose;
			logopt.stderr_level -= opt.quiet;
			logopt.prefix_level = 1;
			log_alter(logopt, 0, NULL);
		}

		if (spank_init_post_opt() < 0) {
			error("Plugin stack post-option processing failed");
			exit(error_exit);
		}

		_set_spank_env();
		if (het_job_inx == 0)
			_set_submit_dir_env();
		if (opt.chdir && chdir(opt.chdir)) {
			error("chdir(%s): %m", opt.chdir);
			exit(error_exit);
		} else if (work_dir)
			opt.chdir = work_dir;

		if ((opt.get_user_env_time >= 0) && !env_cache_set) {
			bool no_env_cache = false;
			char *user;

			env_cache_set = true;
			if (!(user = uid_to_string_or_null(opt.uid))) {
				error("Invalid user id %u: %m",
				      (uint32_t) opt.uid);
				exit(error_exit);
			}

			if (xstrcasestr(slurm_conf.sched_params,
					"no_env_cache"))
				no_env_cache = true;

			env = env_array_user_default(user,
						     opt.get_user_env_time,
						     opt.get_user_env_mode,
						     no_env_cache);
			xfree(user);
			if (env == NULL)
				exit(error_exit);    /* error already logged */
			_set_rlimits(env);
		}

		if (desc && !job_req_list) {
			job_req_list = list_create(NULL);
			list_append(job_req_list, desc);
		}

		desc = slurm_opt_create_job_desc(&opt, true);
		if (_fill_job_desc_from_opts(desc) == -1)
			exit(error_exit);

		if (het_job_inx || !het_job_fini)
			set_env_from_opts(&opt, &env, het_job_inx);
		else
			set_env_from_opts(&opt, &env, -1);
		if (job_req_list)
			list_append(job_req_list, desc);
		if (!first_job)
			first_job = desc;
	}
	het_job_limit = het_job_inx;
	if (!desc) {
		fatal("%s: desc is NULL", __func__);
		exit(error_exit);    /* error already logged */
	}
	_match_job_name(desc, job_req_list);

	/*
	 * Job control for interactive salloc sessions: only if ...
	 *
	 * a) input is from a terminal (stdin has valid termios attributes),
	 * b) controlling terminal exists (non-negative tpgid),
	 * c) salloc is not run in allocation-only (--no-shell) mode,
	 * NOTE: d and e below are configuration dependent
	 * d) salloc runs in its own process group (true in interactive
	 *    shells that support job control),
	 * e) salloc has been configured at compile-time to support background
	 *    execution and is not currently in the background process group.
	 */
	if (tcgetattr(STDIN_FILENO, &saved_tty_attributes) < 0) {
		/*
		 * Test existence of controlling terminal (tpgid > 0)
		 * after first making sure stdin is not redirected.
		 */
	} else if ((tpgid = tcgetpgrp(STDIN_FILENO)) < 0) {
		if (!saopt.no_shell) {
			error("no controlling terminal: please set --no-shell");
			exit(error_exit);
		}
#ifdef SALLOC_RUN_FOREGROUND
	} else if ((!saopt.no_shell) && (pid == getpgrp())) {
		if (tpgid == pid)
			is_interactive = true;
		while (tcgetpgrp(STDIN_FILENO) != pid) {
			if (!is_interactive) {
				error("Waiting for program to be placed in "
				      "the foreground");
				is_interactive = true;
			}
			killpg(pid, SIGTTIN);
		}
	}
#else
	} else if ((!saopt.no_shell) && (getpgrp() == tcgetpgrp(STDIN_FILENO))) {
		is_interactive = true;
	}
#endif
	/*
	 * Reset saved tty attributes at exit, in case a child
	 * process died before properly resetting terminal.
	 */
	if (is_interactive)
		atexit(_reset_input_mode);
	if (opt.gid != getgid()) {
		if (setgid(opt.gid) < 0) {
			error("setgid: %m");
			exit(error_exit);
		}
	}

	/* If can run on multiple clusters find the earliest run time
	 * and run it there */
	if (opt.clusters) {
		if (job_req_list) {
			rc = slurmdb_get_first_het_job_cluster(job_req_list,
					opt.clusters, &working_cluster_rec);
		} else {
			rc = slurmdb_get_first_avail_cluster(desc,
					opt.clusters, &working_cluster_rec);
		}
		if (rc != SLURM_SUCCESS) {
			print_db_notok(opt.clusters, 0);
			exit(error_exit);
		}
	}
	if (job_req_list)
		(void) list_for_each(job_req_list, _set_cluster_name, NULL);
	else
		desc->origin_cluster = xstrdup(slurm_conf.cluster_name);

	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = _job_suspend_handler;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;
	/*
	 * Create message thread to handle pings and such from slurmctld.
	 * salloc --no-shell jobs aren't interactive, so they won't respond to
	 * srun_ping(), so we don't want to kill it after InactiveLimit seconds.
	 * Not creating this thread will leave other_port == 0, and will
	 * prevent slurmctld from killing the salloc --no-shell job.
	 */
	if (!saopt.no_shell) {
		msg_thr = slurm_allocation_msg_thr_create(&first_job->other_port,
							  &callbacks);
		if (job_req_list)
			list_for_each(job_req_list, _copy_other_port,
				      &first_job->other_port);
	}

	/* NOTE: Do not process signals in separate pthread. The signal will
	 * cause slurm_allocate_resources_blocking() to exit immediately. */
	for (i = 0; sig_array[i]; i++)
		xsignal(sig_array[i], _signal_while_allocating);

	/*
	 * This option is a bit odd - it's not actually used as part of the
	 * allocation, but instead just needs to be propagated to an interactive
	 * step launch correctly to make it convenient for the user.
	 * Thus the seemingly out of place copy here.
	 */
	desc->container = xstrdup(opt.container);

	before = time(NULL);
	while (true) {
		if (job_req_list) {
			is_het_job = true;
			job_resp_list = slurm_allocate_het_job_blocking(
					job_req_list, opt.immediate,
					_pending_callback);
			if (job_resp_list)
				break;
		} else {
			alloc = slurm_allocate_resources_blocking(desc,
					opt.immediate, _pending_callback);
			if (alloc)
				break;
		}
		if (((errno != ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) &&
		     (errno != EAGAIN)) || (retries >= MAX_RETRIES))
			break;
		if (retries == 0)
			error("%s", msg);
		else
			debug("%s", msg);
		sleep(++retries);
	}

	/* If the requested uid is different than ours, become that uid */
	if (getuid() != opt.uid) {
		/* drop extended groups before changing uid/gid */
		if ((setgroups(0, NULL) < 0)) {
			error("setgroups: %m");
			exit(error_exit);
		}
		if (setuid(opt.uid) < 0) {
			error("setuid: %m");
			exit(error_exit);
		}
	}

	if (!alloc && !job_resp_list) {
		if (allocation_interrupted) {
			/* cancelled by signal */
			info("Job aborted due to signal");
		} else if (errno == EINTR) {
			error("Interrupted by signal. Allocation request rescinded.");
		} else if (opt.immediate &&
			   ((errno == ETIMEDOUT) ||
			    (errno == ESLURM_NOT_TOP_PRIORITY) ||
			    (errno == ESLURM_NODES_BUSY))) {
			error("Unable to allocate resources: %m");
			error_exit = immediate_exit;
		} else {
			error("Job submit/allocate failed: %m");
		}
		if (msg_thr)
			slurm_allocation_msg_thr_destroy(msg_thr);
		exit(error_exit);
	} else if (job_resp_list && !allocation_interrupted) {
		/* Allocation granted to regular job */
		i = 0;
		iter_resp = list_iterator_create(job_resp_list);
		while ((alloc = list_next(iter_resp))) {
			if (i == 0) {
				my_job_id = alloc->job_id;
				info("Granted job allocation %u", my_job_id);
			}
			log_flag(HETJOB, "Hetjob ID %u+%u (%u) on nodes %s",
			         my_job_id, i, alloc->job_id, alloc->node_list);
			i++;
			if (_proc_alloc(alloc) != SLURM_SUCCESS) {
				list_iterator_destroy(iter_resp);
				goto relinquish;
			}
		}
		list_iterator_destroy(iter_resp);
	} else if (!allocation_interrupted) {
		/* Allocation granted to regular job */
		my_job_id = alloc->job_id;

		print_multi_line_string(alloc->job_submit_user_msg,
					-1, LOG_LEVEL_INFO);
		info("Granted job allocation %u", my_job_id);

		if (_proc_alloc(alloc) != SLURM_SUCCESS)
			goto relinquish;
	}

	_salloc_cli_filter_post_submit(my_job_id, NO_VAL);

	after = time(NULL);
	if ((saopt.bell == BELL_ALWAYS) ||
	     ((saopt.bell == BELL_AFTER_DELAY) &&
	      ((after - before) > DEFAULT_BELL_DELAY))) {
		_ring_terminal_bell();
	}
	if (saopt.no_shell)
		exit(0);
	if (allocation_interrupted) {
		if (alloc)
			my_job_id = alloc->job_id;
		/* salloc process received a signal after
		 * slurm_allocate_resources_blocking returned with the
		 * allocation, but before the new signal handlers were
		 * registered.
		 */
		goto relinquish;
	}

	/*
	 * Set environment variables
	 */
	if (job_resp_list) {
		bool num_tasks_always_set = true;

		i = list_count(job_req_list);
		j = list_count(job_resp_list);
		if (i != j) {
			error("Job component count mismatch, submit/response "
			      "count mismatch (%d != %d)", i, j);
			goto relinquish;
		}
		/* Continue support for old hetjob terminology. */
		env_array_append_fmt(&env, "SLURM_PACK_SIZE", "%d", i);
		env_array_append_fmt(&env, "SLURM_HET_SIZE", "%d", i);

		i = 0;
		iter_req = list_iterator_create(job_req_list);
		iter_resp = list_iterator_create(job_resp_list);
		while ((desc = list_next(iter_req))) {
			alloc = list_next(iter_resp);

			if (alloc && desc &&
			    (desc->bitflags & JOB_NTASKS_SET)) {
				if (desc->ntasks_per_node != NO_VAL16)
					desc->num_tasks =
						alloc->node_cnt *
						desc->ntasks_per_node;
				else if (alloc->node_cnt > desc->num_tasks)
					desc->num_tasks = alloc->node_cnt;
			}
			if ((desc->num_tasks != NO_VAL) && num_tasks_always_set)
				num_tasks += desc->num_tasks;
			else {
				num_tasks = 0;
				num_tasks_always_set = false;
			}
			if (env_array_for_job(&env, alloc, desc, i++) !=
			    SLURM_SUCCESS)
				goto relinquish;
		}
		list_iterator_destroy(iter_resp);
		list_iterator_destroy(iter_req);
	} else {
		if (alloc && desc && (desc->bitflags & JOB_NTASKS_SET)) {
			if (desc->ntasks_per_node != NO_VAL16)
				desc->num_tasks =
					alloc->node_cnt * desc->ntasks_per_node;
			else if (alloc->node_cnt > desc->num_tasks)
				desc->num_tasks = alloc->node_cnt;
		}
		if (desc->num_tasks != NO_VAL)
			num_tasks += desc->num_tasks;
		if (env_array_for_job(&env, alloc, desc, -1) != SLURM_SUCCESS)
			goto relinquish;
	}

	if (num_tasks) {
		env_array_append_fmt(&env, "SLURM_NTASKS", "%d", num_tasks);
		env_array_append_fmt(&env, "SLURM_NPROCS", "%d", num_tasks);
	}

	if (working_cluster_rec && working_cluster_rec->name) {
		env_array_append_fmt(&env, "SLURM_CLUSTER_NAME", "%s",
				     working_cluster_rec->name);
	} else
		env_array_append_fmt(&env, "SLURM_CLUSTER_NAME", "%s",
				     slurm_conf.cluster_name);

	env_array_set_environment(env);
	env_array_free(env);
	slurm_mutex_lock(&allocation_state_lock);
	if (allocation_state == REVOKED) {
		error("Allocation was revoked for job %u before command could be run",
		      my_job_id);
		slurm_cond_broadcast(&allocation_state_cond);
		slurm_mutex_unlock(&allocation_state_lock);
		if (slurm_complete_job(my_job_id, status) != 0) {
			error("Unable to clean up allocation for job %u: %m",
			      my_job_id);
		}
		return 1;
 	}
	allocation_state = GRANTED;
	slurm_cond_broadcast(&allocation_state_cond);
	slurm_mutex_unlock(&allocation_state_lock);

	/*  Ensure that salloc has initial terminal foreground control.  */
	if (is_interactive) {
		/*
		 * Ignore remaining job-control signals (other than those in
		 * sig_array, which at this state act like SIG_IGN).
		 */
		xsignal(SIGTSTP, SIG_IGN);
		xsignal(SIGTTIN, SIG_IGN);
		xsignal(SIGTTOU, SIG_IGN);

		pid = getpid();
		setpgid(pid, pid);

		tcsetpgrp(STDIN_FILENO, pid);
	}
	slurm_mutex_lock(&allocation_state_lock);
	if (suspend_flag)
		slurm_cond_wait(&allocation_state_cond, &allocation_state_lock);
	command_pid = _fork_command(command_argv);
	slurm_cond_broadcast(&allocation_state_cond);
	slurm_mutex_unlock(&allocation_state_lock);

	/*
	 * Wait for command to exit, OR for waitpid to be interrupted by a
	 * signal.  Either way, we are going to release the allocation next.
	 */
	if (command_pid > 0) {
		setpgid(command_pid, command_pid);
		if (is_interactive)
			tcsetpgrp(STDIN_FILENO, command_pid);

		/* NOTE: Do not process signals in separate pthread.
		 * The signal will cause waitpid() to exit immediately. */
		xsignal(SIGHUP,  _exit_on_signal);
		/* Use WUNTRACED to treat stopped children like terminated ones */
		do {
			rc_pid = waitpid(command_pid, &status, WUNTRACED);
		} while (WIFSTOPPED(status) || ((rc_pid == -1) && (!exit_flag)));
		if ((rc_pid == -1) && (errno != EINTR))
			error("waitpid for %s failed: %m", command_argv[0]);
	}

	if (is_interactive)
		tcsetpgrp(STDIN_FILENO, pid);

	/*
	 * Relinquish the job allocation (if not already revoked).
	 */
relinquish:
	slurm_mutex_lock(&allocation_state_lock);
	if (allocation_state != REVOKED) {
		slurm_mutex_unlock(&allocation_state_lock);

		info("Relinquishing job allocation %u", my_job_id);
		if ((slurm_complete_job(my_job_id, status) != 0) &&
		    (slurm_get_errno() != ESLURM_ALREADY_DONE))
			error("Unable to clean up job allocation %u: %m",
			      my_job_id);
		slurm_mutex_lock(&allocation_state_lock);
		allocation_state = REVOKED;
	}
	slurm_cond_broadcast(&allocation_state_cond);
	slurm_mutex_unlock(&allocation_state_lock);

	slurm_free_resource_allocation_response_msg(alloc);
	if (msg_thr)
		slurm_allocation_msg_thr_destroy(msg_thr);

	/*
	 * Figure out what return code we should use.  If the user's command
	 * exited normally, return the user's return code.
	 */
	rc = 1;
	if (rc_pid != -1) {
		if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
		} else if (WIFSTOPPED(status)) {
			/* Terminate stopped child process */
			_forward_signal(SIGKILL);
		} else if (WIFSIGNALED(status)) {
			verbose("Command \"%s\" was terminated by signal %d",
				command_argv[0], WTERMSIG(status));
			/* if we get these signals we return a normal
			 * exit since this was most likely sent from the
			 * user */
			switch (WTERMSIG(status)) {
			case SIGHUP:
			case SIGINT:
			case SIGQUIT:
			case SIGKILL:
				rc = 0;
				break;
			default:
				break;
			}
		}
	}

#ifdef MEMORY_LEAK_DEBUG
	select_g_fini();
	slurm_reset_all_options(&opt, false);
	slurm_auth_fini();
	slurm_conf_destroy();
	log_fini();
#endif /* MEMORY_LEAK_DEBUG */

	return rc;
}

/* Initial processing of resource allocation response, including waiting for
 * compute nodes to be ready for use.
 * Ret SLURM_SUCCESS on success */
static int _proc_alloc(resource_allocation_response_msg_t *alloc)
{
	static int elem = 0;

	if ((elem++ == 0) && alloc->working_cluster_rec) {
		slurm_setup_remote_working_cluster(alloc);

		/* set env for srun's to find the right cluster */
		setenvf(NULL, "SLURM_WORKING_CLUSTER", "%s:%s:%d:%d:%d",
			working_cluster_rec->name,
			working_cluster_rec->control_host,
			working_cluster_rec->control_port,
			working_cluster_rec->rpc_version,
			select_get_plugin_id());
	}

	if (!_wait_nodes_ready(alloc)) {
		if (!allocation_interrupted)
			error("Something is wrong with the boot of the nodes.");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


/* Copy job name from last component to all hetjob components unless
 * explicitly set. The default value comes from _salloc_default_command()
 * and is "sh". */
static void _match_job_name(job_desc_msg_t *desc_last, List job_req_list)
{
	ListIterator iter;
	job_desc_msg_t *desc = NULL;
	char *name;

	if (!desc_last)
		return;

	if (!desc_last->name && command_argv[0])
		desc_last->name = xstrdup(xbasename(command_argv[0]));
	name = desc_last->name;

	if (!job_req_list)
		return;

	iter = list_iterator_create(job_req_list);
	while ((desc = list_next(iter)))
		if (!desc->name)
			desc->name = xstrdup(name);

	list_iterator_destroy(iter);
}

static void _set_exit_code(void)
{
	int i;
	char *val;

	if ((val = getenv("SLURM_EXIT_ERROR"))) {
		i = atoi(val);
		if (i == 0)
			error("SLURM_EXIT_ERROR has zero value");
		else
			error_exit = i;
	}

	if ((val = getenv("SLURM_EXIT_IMMEDIATE"))) {
		i = atoi(val);
		if (i == 0)
			error("SLURM_EXIT_IMMEDIATE has zero value");
		else
			immediate_exit = i;
	}
}

/* Propagate SPANK environment via SLURM_SPANK_ environment variables */
static void _set_spank_env(void)
{
	int i;

	for (i = 0; i < opt.spank_job_env_size; i++) {
		if (setenvfs("SLURM_SPANK_%s", opt.spank_job_env[i]) < 0) {
			error("unable to set %s in environment",
			      opt.spank_job_env[i]);
		}
	}
}

/* Set SLURM_SUBMIT_DIR and SLURM_SUBMIT_HOST environment variables within
 * current state */
static void _set_submit_dir_env(void)
{
	char host[256];

	work_dir = xmalloc(MAXPATHLEN + 1);
	if ((getcwd(work_dir, MAXPATHLEN)) == NULL)
		error("getcwd failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_DIR", "%s", work_dir) < 0)
		error("unable to set SLURM_SUBMIT_DIR in environment");

	if ((gethostname(host, sizeof(host))))
		error("gethostname_short failed: %m");
	else if (setenvf(NULL, "SLURM_SUBMIT_HOST", "%s", host) < 0)
		error("unable to set SLURM_SUBMIT_HOST in environment");
}

/* Returns 0 on success, -1 on failure */
static int _fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	if (!desc)
		return -1;

	desc->wait_all_nodes = saopt.wait_all_nodes;

	return 0;
}

static void _ring_terminal_bell(void)
{
        if (isatty(STDOUT_FILENO)) {
                fprintf(stdout, "\a");
                fflush(stdout);
        }
}

/* returns the pid of the forked command, or <0 on error */
static pid_t _fork_command(char **command)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		error("%s: fork failed: %m",
		      __func__);
	} else if (pid == 0) {
		/* child */
		char *cwd = opt.chdir ? opt.chdir : work_dir;
		char *cpath = search_path(cwd, command[0], true, X_OK, true);

		xassert(cwd);
		if (!cpath) {
			error("%s: Unable to find command \"%s\"",
			      __func__, command[0]);
			_exit(error_exit);
		}

		setpgid(getpid(), 0);

		/*
		 * Reset job control signals.
		 * Suspend (TSTP) is not restored (ignored, as in the parent):
		 * shells with job-control override this and look after their
		 * processes.
		 * Suspending single commands is more complex and would require
		 * adding full shell-like job control to salloc.
		 */
		xsignal(SIGINT, SIG_DFL);
		xsignal(SIGQUIT, SIG_DFL);
		xsignal(SIGTTIN, SIG_DFL);
		xsignal(SIGTTOU, SIG_DFL);

		execvp(cpath, command);

		/* should only get here if execvp failed */
		error("%s: Unable to exec command \"%s\": %m",
		      __func__, cpath);
		xfree(cpath);
		_exit(error_exit);
	}
	/* parent returns */
	return pid;
}

static void _pending_callback(uint32_t job_id)
{
	info("Pending job allocation %u", job_id);
	my_job_id = job_id;

	/* call cli_filter post_submit here so it runs while allocating */
	_salloc_cli_filter_post_submit(my_job_id, NO_VAL);
}

/*
 * Run cli_filter_post_submit on all opt structures
 * Convenience function since this might need to run in two spots
 * uses a static bool to prevent multiple executions
 */
static void _salloc_cli_filter_post_submit(uint32_t jobid, uint32_t stepid)
{
	int idx = 0;
	if (_cli_filter_post_submit_run)
		return;
	for (idx = 0; idx < het_job_limit; idx++)
		cli_filter_g_post_submit(idx, jobid, stepid);

	_cli_filter_post_submit_run = true;
}

static void _exit_on_signal(int signo)
{
	_forward_signal(signo);
	exit_flag = true;
}

static void _forward_signal(int signo)
{
	if (command_pid > 0)
		killpg(command_pid, signo);
}

static void _signal_while_allocating(int signo)
{
	allocation_interrupted = true;
	if (my_job_id != 0) {
		slurm_complete_job(my_job_id, NO_VAL);
	}
}

/* This typically signifies the job was cancelled by scancel */
static void _job_complete_handler(srun_job_complete_msg_t *comp)
{
	if (!is_het_job && my_job_id && (my_job_id != comp->job_id)) {
		error("Ignoring job_complete for job %u because our job ID is %u",
		      comp->job_id, my_job_id);
		return;
	}

	if (comp->step_id == NO_VAL) {
		slurm_mutex_lock(&allocation_state_lock);
		if (allocation_state != REVOKED) {
			/* If the allocation_state is already REVOKED, then
			 * no need to print this message.  We probably
			 * relinquished the allocation ourself.
			 */
			if (last_timeout && (last_timeout < time(NULL))) {
				info("Job %u has exceeded its time limit and "
				     "its allocation has been revoked.",
				     comp->job_id);
			} else {
				info("Job allocation %u has been revoked.",
				     comp->job_id);
				allocation_revoked = true;
			}
		}
		allocation_state = REVOKED;
		slurm_cond_broadcast(&allocation_state_cond);
		slurm_mutex_unlock(&allocation_state_lock);
		/*
		 * Clean up child process: only if the forked process has not
		 * yet changed state (waitpid returning 0).
		 */
		if ((command_pid > -1) &&
		    (waitpid(command_pid, NULL, WNOHANG) == 0)) {
			int signal = 0;

			if (is_interactive) {
				pid_t tpgid = tcgetpgrp(STDIN_FILENO);
				/*
				 * This happens if the command forks further
				 * subprocesses, e.g. a user shell (since we
				 * are ignoring TSTP, the process must have
				 * originated from salloc). Notify foreground
				 * process about pending termination.
				 */
				if (tpgid != command_pid && tpgid != getpgrp())
					killpg(tpgid, SIGHUP);
			}

			if (saopt.kill_command_signal)
				signal = saopt.kill_command_signal;
#ifdef SALLOC_KILL_CMD
			else if (is_interactive)
				signal = SIGHUP;
			else
				signal = SIGTERM;
#endif
			if (signal) {
				 verbose("Sending signal %d to command \"%s\","
					 " pid %d",
					 signal, command_argv[0], command_pid);
				if (suspend_flag)
					_forward_signal(SIGCONT);
				_forward_signal(signal);
			}
		}
	} else {
		verbose("%ps is finished.", &comp->step_id);
	}
}

static void _job_suspend_handler(suspend_msg_t *msg)
{
	if (msg->op == SUSPEND_JOB) {
		verbose("job has been suspended");
	} else if (msg->op == RESUME_JOB) {
		verbose("job has been resumed");
	}
}

/*
 * Job has been notified of it's approaching time limit.
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 * FIXME: We may want to signal the job or perform other action for this.
 * FIXME: How much lead time do we want for this message? Some jobs may
 *	require tens of minutes to gracefully terminate.
 */
static void _timeout_handler(srun_timeout_msg_t *msg)
{
	if (msg->timeout != last_timeout) {
		last_timeout = msg->timeout;
		verbose("Job allocation time limit to be reached at %s",
			slurm_ctime2(&msg->timeout));
	}
}

static void _user_msg_handler(srun_user_msg_t *msg)
{
	info("%s", msg->msg);
}

static void _node_fail_handler(srun_node_fail_msg_t *msg)
{
	error("Node failure on %s", msg->nodelist);
}

static void _set_rlimits(char **env)
{
	slurm_rlimits_info_t *rli;
	char env_name[32] = "SLURM_RLIMIT_";
	char *env_value, *p;
	struct rlimit r;
	rlim_t env_num;
	int header_len = sizeof("SLURM_RLIMIT_");

	for (rli = get_slurm_rlimits_info(); rli->name; rli++) {
		if (rli->propagate_flag != PROPAGATE_RLIMITS)
			continue;
		if ((header_len + strlen(rli->name)) >= sizeof(env_name)) {
			error("%s: env_name(%s) too long", __func__, env_name);
			continue;
		}
		strcpy(&env_name[header_len - 1], rli->name);
		env_value = getenvp(env, env_name);
		if (env_value == NULL)
			continue;
		unsetenvp(env, env_name);
		if (getrlimit(rli->resource, &r) < 0) {
			error("getrlimit(%s): %m", env_name+6);
			continue;
		}
		env_num = strtol(env_value, &p, 10);
		if (p && (p[0] != '\0')) {
			error("Invalid environment %s value %s",
			      env_name, env_value);
			continue;
		}
		if (r.rlim_cur == env_num)
			continue;
		r.rlim_cur = (rlim_t) env_num;
		if (setrlimit(rli->resource, &r) < 0) {
			error("setrlimit(%s): %m", env_name+6);
			continue;
		}
	}
}

/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	int cur_delay = 0;
	int max_delay;
	bool job_killed = false;

	if (slurm_conf.suspend_timeout || slurm_conf.resume_timeout) {
		max_delay = slurm_conf.suspend_timeout +
			    slurm_conf.resume_timeout;
		max_delay *= 5;		/* Allow for ResumeRate support */
	} else {
		max_delay = 300;	/* Wait to 5 min for PrologSlurmctld */
	}

	if (alloc->alias_list && !xstrcmp(alloc->alias_list, "TBD"))
		saopt.wait_all_nodes = 1;	/* Wait for boot & addresses */
	if (saopt.wait_all_nodes == NO_VAL16)
		saopt.wait_all_nodes = 0;

	for (i = 0; (cur_delay < max_delay); i++) {

		if (i == 1) {
			/*
			 * Only sleep a short time on the first miss.
			 */
			usleep(500); /* Not adding sub-sec to cur_delay */
		} else if (i) {
			if (i == 2)
				info("Waiting for resource configuration");
			else
				debug("still waiting");
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(alloc->job_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if (allocation_interrupted || allocation_revoked)
			break;
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0) {	/* job killed */
			job_killed = true;
			break;
		}
		if ((rc & READY_JOB_STATE) &&
		    (rc & READY_PROLOG_STATE) &&
		    ((rc & READY_NODE_STATE) || !saopt.wait_all_nodes)) {
			is_ready = 1;
			break;
		}
	}
	if (is_ready) {
		resource_allocation_response_msg_t *resp;
		char *tmp_str;
		if (i > 1)
     			info("Nodes %s are ready for job", alloc->node_list);
		if (alloc->alias_list && !xstrcmp(alloc->alias_list, "TBD") &&
		    (slurm_allocation_lookup(alloc->job_id, &resp)
		     == SLURM_SUCCESS)) {
			tmp_str = alloc->alias_list;
			alloc->alias_list = resp->alias_list;
			resp->alias_list = tmp_str;
			slurm_free_resource_allocation_response_msg(resp);
		}
	} else if (!allocation_interrupted) {
		if (job_killed || allocation_revoked) {
			error("Job allocation %u has been revoked",
			      alloc->job_id);
			allocation_interrupted = true;
		} else
			error("Nodes %s are still not ready", alloc->node_list);
	} else	/* allocation_interrupted or slurmctld not responing */
		is_ready = 0;

	return is_ready;
}
