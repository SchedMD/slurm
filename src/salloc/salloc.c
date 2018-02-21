/*****************************************************************************\
 *  salloc.c - Request a SLURM job allocation and
 *             launch a user-specified command.
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"

#ifdef HAVE_BG
#include "src/common/node_select.h"
#include "src/plugins/select/bluegene/bg_enums.h"
#elif defined(HAVE_ALPS_CRAY)
#include "src/common/node_select.h"

#ifdef HAVE_REAL_CRAY
/*
 * On Cray installations, the libjob headers are not automatically installed
 * by default, while libjob.so always is, and kernels are > 2.6. Hence it is
 * simpler to just duplicate the single declaration here.
 */
extern uint64_t job_getjid(pid_t pid);
#endif
#endif

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getpgid(pid_t pid);
#endif

#define MAX_RETRIES	10
#define POLL_SLEEP	3	/* retry interval in seconds  */

char **command_argv;
int command_argc;
pid_t command_pid = -1;
uint64_t debug_flags = 0;
char *work_dir = NULL;
static int is_interactive;

enum possible_allocation_states allocation_state = NOT_GRANTED;
pthread_cond_t  allocation_state_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t allocation_state_lock = PTHREAD_MUTEX_INITIALIZER;

static bool allocation_interrupted = false;
static bool allocation_revoked = false;
static bool exit_flag = false;
static bool is_pack_job = false;
static bool suspend_flag = false;
static uint32_t my_job_id = 0;
static time_t last_timeout = 0;
static struct termios saved_tty_attributes;

static void _exit_on_signal(int signo);
static int  _fill_job_desc_from_opts(job_desc_msg_t *desc);
static pid_t _fork_command(char **command);
static void _forward_signal(int signo);
static void _job_complete_handler(srun_job_complete_msg_t *msg);
static void _job_suspend_handler(suspend_msg_t *msg);
static void _match_job_name(List job_req_list, char *job_name);
static void _node_fail_handler(srun_node_fail_msg_t *msg);
static void _pending_callback(uint32_t job_id);
static void _ping_handler(srun_ping_msg_t *msg);
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

#ifdef HAVE_BG
static int _wait_bluegene_block_ready(
			resource_allocation_response_msg_t *alloc);
static int _blocks_dealloc(void);
#else
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc);
#endif

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
	desc->origin_cluster = xstrdup(slurmctld_conf.cluster_name);
	return 0;
}

int main(int argc, char **argv)
{
	static bool env_cache_set = false;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_desc_msg_t *desc = NULL, *first_job = NULL;
	List job_req_list = NULL, job_resp_list = NULL;
	resource_allocation_response_msg_t *alloc = NULL;
	time_t before, after;
	allocation_msg_thread_t *msg_thr;
	char **env = NULL, *cluster_name;
	int status = 0;
	int retries = 0;
	pid_t pid  = getpid();
	pid_t tpgid = 0;
	pid_t rc_pid = 0;
	int i, j, rc = 0;
	bool pack_fini = false;
	int pack_argc, pack_inx, pack_argc_off;
	char **pack_argv;
	static char *msg = "Slurm job queue full, sleeping and retrying.";
	slurm_allocation_callbacks_t callbacks;
	ListIterator iter_req, iter_resp;

	slurm_conf_init(NULL);
	debug_flags = slurm_get_debug_flags();
	log_init(xbasename(argv[0]), logopt, 0, NULL);
	_set_exit_code();

	if (spank_init_allocator() < 0) {
		error("Failed to initialize plugin stack");
		exit(error_exit);
	}

	/* Be sure to call spank_fini when salloc exits
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");


	pack_argc = argc;
	pack_argv = argv;
	for (pack_inx = 0; !pack_fini; pack_inx++) {
		pack_argc_off = -1;
		if (initialize_and_process_args(pack_argc, pack_argv,
						&pack_argc_off) < 0) {
			error("salloc parameter parsing");
			exit(error_exit);
		}
		if ((pack_argc_off >= 0) && (pack_argc_off < pack_argc) &&
		    !xstrcmp(pack_argv[pack_argc_off], ":")) {
			/* pack_argv[0] moves from "salloc" to ":" */
			pack_argc -= pack_argc_off;
			pack_argv += pack_argc_off;
		} else
			pack_fini = true;

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
		if (pack_inx == 0)
			_set_submit_dir_env();
		if (opt.cwd && chdir(opt.cwd)) {
			error("chdir(%s): %m", opt.cwd);
			exit(error_exit);
		}

		if ((opt.get_user_env_time >= 0) && !env_cache_set) {
			bool no_env_cache = false;
			char *sched_params;
			char *user = uid_to_string(opt.uid);

			env_cache_set = true;
			if (xstrcmp(user, "nobody") == 0) {
				error("Invalid user id %u: %m",
				      (uint32_t) opt.uid);
				exit(error_exit);
			}

			sched_params = slurm_get_sched_params();
			no_env_cache = (sched_params &&
					strstr(sched_params, "no_env_cache"));
			xfree(sched_params);

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
		desc = xmalloc(sizeof(job_desc_msg_t));
		slurm_init_job_desc_msg(desc);
		if (_fill_job_desc_from_opts(desc) == -1)
			exit(error_exit);
		if (job_req_list)
			list_append(job_req_list, desc);
		if (!first_job)
			first_job = desc;
	}
	if (!desc) {
		fatal("%s: desc is NULL", __func__);
		exit(error_exit);    /* error already logged */
	}
	_match_job_name(job_req_list, opt.job_name);
	if (!job_req_list)
		desc->bitflags &= (~JOB_SALLOC_FLAG);

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
#ifdef HAVE_ALPS_CRAY
		verbose("no controlling terminal");
#else
		if (!saopt.no_shell) {
			error("no controlling terminal: please set --no-shell");
			exit(error_exit);
		}
#endif
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
	if (opt.gid != (gid_t) -1) {
		if (setgid(opt.gid) < 0) {
			error("setgid: %m");
			exit(error_exit);
		}
	}

	/* If can run on multiple clusters find the earliest run time
	 * and run it there */
	if (opt.clusters) {
		if (job_req_list) {
			rc = slurmdb_get_first_pack_cluster(job_req_list,
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
		desc->origin_cluster = xstrdup(slurmctld_conf.cluster_name);

	callbacks.ping = _ping_handler;
	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = _job_suspend_handler;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;
	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&first_job->other_port,
						  &callbacks);

	/* NOTE: Do not process signals in separate pthread. The signal will
	 * cause slurm_allocate_resources_blocking() to exit immediately. */
	for (i = 0; sig_array[i]; i++)
		xsignal(sig_array[i], _signal_while_allocating);

	before = time(NULL);
	while (true) {
		if (job_req_list) {
			is_pack_job = true;
			job_resp_list = slurm_allocate_pack_job_blocking(
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
	if ((getuid() != opt.uid) && (opt.uid != (uid_t) -1)) {
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
		slurm_allocation_msg_thr_destroy(msg_thr);
		exit(error_exit);
	} else if (job_resp_list && !allocation_interrupted) {
		/* Allocation granted to regular job */
		i = 0;
		iter_resp = list_iterator_create(job_resp_list);
		while ((alloc = (resource_allocation_response_msg_t *)
				list_next(iter_resp))) {
			if (i == 0) {
				my_job_id = alloc->job_id;
				info("Granted job allocation %u", my_job_id);
			}
			if (debug_flags & DEBUG_FLAG_HETERO_JOBS) {
				info("Pack job ID %u+%u (%u) on nodes %s",
				     my_job_id, i, alloc->job_id,
				     alloc->node_list);
			}
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

		if (alloc)
			print_multi_line_string(
				alloc->job_submit_user_msg, -1);
		info("Granted job allocation %u", my_job_id);

		if (_proc_alloc(alloc) != SLURM_SUCCESS)
			goto relinquish;
	}

	after = time(NULL);
	if ((saopt.bell == BELL_ALWAYS) ||
	     ((saopt.bell == BELL_AFTER_DELAY) &&
	      ((after - before) > DEFAULT_BELL_DELAY))) {
		_ring_terminal_bell();
	}
	if (saopt.no_shell)
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
	 * Set environment variables
	 */
	if (job_resp_list) {
		i = list_count(job_req_list);
		j = list_count(job_resp_list);
		if (i != j) {
			error("Job component count mismatch, submit/response "
			      "count mismatch (%d != %d)", i, j);
			goto relinquish;
		}
		env_array_append_fmt(&env, "SLURM_PACK_SIZE", "%d", i);

		i = 0;
		iter_req = list_iterator_create(job_req_list);
		iter_resp = list_iterator_create(job_resp_list);
		while ((desc = (job_desc_msg_t *) list_next(iter_req))) {
			alloc = (resource_allocation_response_msg_t *)
				list_next(iter_resp);
			if (env_array_for_job(&env, alloc, desc, i++) !=
			    SLURM_SUCCESS)
				goto relinquish;
		}
		list_iterator_destroy(iter_resp);
		list_iterator_destroy(iter_req);
	} else {
		if (env_array_for_job(&env, alloc, desc, -1) != SLURM_SUCCESS)
			goto relinquish;
	}

	if (working_cluster_rec && working_cluster_rec->name) {
		env_array_append_fmt(&env, "SLURM_CLUSTER_NAME", "%s",
				     working_cluster_rec->name);
	} else if ((cluster_name = slurm_get_cluster_name())) {
		env_array_append_fmt(&env, "SLURM_CLUSTER_NAME", "%s",
				     cluster_name);
		xfree(cluster_name);
	}

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
		setenvf(NULL, "SLURM_WORKING_CLUSTER", "%s:%s:%d:%d",
			working_cluster_rec->name,
			working_cluster_rec->control_host,
			working_cluster_rec->control_port,
			working_cluster_rec->rpc_version);
	}

#ifdef HAVE_BG
	if (!_wait_bluegene_block_ready(alloc)) {
		if (!allocation_interrupted)
			error("Something is wrong with the boot of the block.");
		return SLURM_ERROR;
	}
#else
	if (!_wait_nodes_ready(alloc)) {
		if (!allocation_interrupted)
			error("Something is wrong with the boot of the nodes.");
		return SLURM_ERROR;
	}
#endif
	return SLURM_SUCCESS;
}


/* Copy job name from last component to all pack job components unless
 * explicitly set. The default value comes from _salloc_default_command()
 * and is "sh". */
static void _match_job_name(List job_req_list, char *job_name)
{
	int cnt, i = 1;
	ListIterator iter;
	job_desc_msg_t *desc = NULL;

	if (!job_req_list)
		return;

	cnt = list_count(job_req_list);
	if (cnt < 2)
		return;

	iter = list_iterator_create(job_req_list);
	while ((desc = (job_desc_msg_t *) list_next(iter))) {
		if ((i++ < cnt) && (desc->bitflags & JOB_SALLOC_FLAG)) {
			xfree(desc->name);
			desc->name = xstrdup(job_name);
		}
		desc->bitflags &= (~JOB_SALLOC_FLAG);
	}
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
#if defined HAVE_ALPS_CRAY && defined HAVE_REAL_CRAY
	uint64_t pagg_id = job_getjid(getpid());
	/*
	 * Interactive sessions require pam_job.so in /etc/pam.d/common-session
	 * since creating sgi_job containers requires root permissions. This is
	 * the only exception where we allow the fallback of using the SID to
	 * confirm the reservation (caught later, in do_basil_confirm).
	 */
	if (pagg_id == (uint64_t)-1) {
		error("No SGI job container ID detected - please enable the "
		      "Cray job service via /etc/init.d/job");
	} else {
		if (!desc->select_jobinfo)
			desc->select_jobinfo = select_g_select_jobinfo_alloc();

		select_g_select_jobinfo_set(desc->select_jobinfo,
					    SELECT_JOBDATA_PAGG_ID, &pagg_id);
	}
#endif
	desc->contiguous = opt.contiguous ? 1 : 0;
	if (opt.core_spec != NO_VAL16)
		desc->core_spec = opt.core_spec;
	desc->extra = xstrdup(opt.extra);
	desc->features = xstrdup(opt.constraints);
	desc->cluster_features = xstrdup(opt.c_constraints);
	desc->gres = xstrdup(opt.gres);
	if (opt.immediate == 1)
		desc->immediate = 1;
	if (saopt.default_job_name)
		desc->bitflags |= JOB_SALLOC_FLAG;
	desc->name = xstrdup(opt.job_name);
	desc->reservation = xstrdup(opt.reservation);
	desc->profile  = opt.profile;
	desc->wckey  = xstrdup(opt.wckey);

	desc->x11 = opt.x11;
	if (desc->x11) {
		desc->x11_magic_cookie = xstrdup(opt.x11_magic_cookie);
		desc->x11_target_port = opt.x11_target_port;
	}

	desc->cpu_freq_min = opt.cpu_freq_min;
	desc->cpu_freq_max = opt.cpu_freq_max;
	desc->cpu_freq_gov = opt.cpu_freq_gov;

	if (opt.req_switch >= 0)
		desc->req_switch = opt.req_switch;
	if (opt.wait4switch >= 0)
		desc->wait4switch = opt.wait4switch;

	desc->req_nodes = xstrdup(opt.nodelist);
	desc->exc_nodes = xstrdup(opt.exc_nodes);
	desc->partition = xstrdup(opt.partition);
	desc->min_nodes = opt.min_nodes;

	if (opt.max_nodes)
		desc->max_nodes = opt.max_nodes;
	else if (opt.nodes_set)
		desc->max_nodes = opt.min_nodes;

	desc->user_id = opt.uid;
	desc->group_id = opt.gid;
	if (opt.dependency)
		desc->dependency = xstrdup(opt.dependency);

	if (opt.mem_bind)
		desc->mem_bind       = xstrdup(opt.mem_bind);
	if (opt.mem_bind_type)
		desc->mem_bind_type  = opt.mem_bind_type;
	if (opt.plane_size != NO_VAL)
		desc->plane_size     = opt.plane_size;
	desc->task_dist  = opt.distribution;
	if (opt.plane_size != NO_VAL)
		desc->plane_size = opt.plane_size;

	if (opt.licenses)
		desc->licenses = xstrdup(opt.licenses);
	desc->network = xstrdup(opt.network);
	if (opt.nice != NO_VAL)
		desc->nice = NICE_OFFSET + opt.nice;
	if (opt.priority)
		desc->priority = opt.priority;
	desc->mail_type = opt.mail_type;
	if (opt.mail_user)
		desc->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		desc->begin_time = opt.begin;
	if (opt.deadline)
		desc->deadline = opt.deadline;
	if (saopt.burst_buffer)
		desc->burst_buffer = saopt.burst_buffer;
	if (opt.account)
		desc->account = xstrdup(opt.account);
	if (opt.acctg_freq)
		desc->acctg_freq = xstrdup(opt.acctg_freq);
	if (opt.comment)
		desc->comment = xstrdup(opt.comment);
	if (opt.qos)
		desc->qos = xstrdup(opt.qos);

	if (opt.cwd)
		desc->work_dir = xstrdup(opt.cwd);
	else if (work_dir)
		desc->work_dir = xstrdup(work_dir);

	if (opt.hold)
		desc->priority     = 0;
#ifdef HAVE_BG
	if (opt.geometry[0] > 0) {
		int i;
		for (i = 0; i < SYSTEM_DIMENSIONS; i++)
			desc->geometry[i] = opt.geometry[i];
	}
#endif
	memcpy(desc->conn_type, opt.conn_type, sizeof(desc->conn_type));

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

	/* job constraints */
	if (opt.pn_min_cpus > -1)
		desc->pn_min_cpus = opt.pn_min_cpus;
	if (opt.pn_min_memory > -1)
		desc->pn_min_memory = opt.pn_min_memory;
	else if (opt.mem_per_cpu > -1)
		desc->pn_min_memory = opt.mem_per_cpu | MEM_PER_CPU;
	if (opt.pn_min_tmp_disk > -1)
		desc->pn_min_tmp_disk = opt.pn_min_tmp_disk;
	if (opt.overcommit) {
		desc->min_cpus = opt.min_nodes;
		desc->overcommit = opt.overcommit;
	} else if (opt.cpus_set)
		desc->min_cpus = opt.ntasks * opt.cpus_per_task;
	else
		desc->min_cpus = opt.ntasks;
	if (opt.ntasks_set)
		desc->num_tasks = opt.ntasks;
	if (opt.cpus_set)
		desc->cpus_per_task = opt.cpus_per_task;
	if (opt.ntasks_per_node)
		desc->ntasks_per_node = opt.ntasks_per_node;
	if (opt.ntasks_per_socket > -1)
		desc->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core > -1)
		desc->ntasks_per_core = opt.ntasks_per_core;

	/* node constraints */
	if (opt.sockets_per_node != NO_VAL)
		desc->sockets_per_node = opt.sockets_per_node;
	if (opt.cores_per_socket != NO_VAL)
		desc->cores_per_socket = opt.cores_per_socket;
	if (opt.threads_per_core != NO_VAL)
		desc->threads_per_core = opt.threads_per_core;

	if (opt.no_kill)
		desc->kill_on_node_fail = 0;
	if (opt.time_limit  != NO_VAL)
		desc->time_limit = opt.time_limit;
	if (opt.time_min  != NO_VAL)
		desc->time_min = opt.time_min;
	if (opt.job_flags)
		desc->bitflags |= opt.job_flags;
	desc->shared = opt.shared;
	desc->job_id = opt.jobid;

	desc->wait_all_nodes = saopt.wait_all_nodes;
	if (opt.warn_signal)
		desc->warn_signal = opt.warn_signal;
	if (opt.warn_time)
		desc->warn_time = opt.warn_time;

	if (opt.spank_job_env_size) {
		/* NOTE: Not copying array, but shared memory */
		desc->spank_job_env      = opt.spank_job_env;
		desc->spank_job_env_size = opt.spank_job_env_size;
	}

	if (opt.power_flags)
		desc->power_flags = opt.power_flags;
	if (opt.mcs_label)
		desc->mcs_label = xstrdup(opt.mcs_label);
	if (opt.delay_boot != NO_VAL)
		desc->delay_boot = opt.delay_boot;
	if (opt.cpus_set)
		desc->bitflags |= JOB_CPUS_SET;
	if (opt.ntasks_set)
		desc->bitflags |= JOB_NTASKS_SET;

	desc->clusters = xstrdup(opt.clusters);

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
		error("fork failed: %m");
	} else if (pid == 0) {
		/* child */
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

		execvp(command[0], command);

		/* should only get here if execvp failed */
		error("Unable to exec command \"%s\"", command[0]);
		exit(error_exit);
	}
	/* parent returns */
	return pid;
}

static void _pending_callback(uint32_t job_id)
{
	info("Pending job allocation %u", job_id);
	my_job_id = job_id;
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
	if (my_job_id && (my_job_id != comp->job_id)) {
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

			if (saopt.kill_command_signal_set)
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
		verbose("Job step %u.%u is finished.",
			comp->job_id, comp->step_id);
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

static void _ping_handler(srun_ping_msg_t *msg)
{
	/* the api will respond so there really is nothing to do here */
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

#ifdef HAVE_BG
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_bluegene_block_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	char *block_id = NULL;
	int cur_delay = 0;
	int max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
			(BG_INCR_BLOCK_BOOT * alloc->node_cnt);

	select_g_select_jobinfo_get(alloc->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &block_id);

	for (i = 0; (cur_delay < max_delay); i++) {
		if (i == 1)
			info("Waiting for block %s to become ready for job",
			     block_id);
		if (i) {
			sleep(POLL_SLEEP);
			rc = _blocks_dealloc();
			if ((rc == 0) || (rc == -1))
				cur_delay += POLL_SLEEP;
			debug("still waiting");
		}

		rc = slurm_job_node_ready(alloc->job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = 1;
			break;
		}
		if (allocation_interrupted || allocation_revoked)
			break;
	}
	if (is_ready)
     		info("Block %s is ready for job", block_id);
	else if (!allocation_interrupted)
		error("Block %s still not ready", block_id);
	else	/* allocation_interrupted and slurmctld not responing */
		is_ready = 0;

	xfree(block_id);

	return is_ready;
}

/*
 * Test if any BG blocks are in deallocating state since they are
 * probably related to this job we will want to sleep longer
 * RET	1:  deallocate in progress
 *	0:  no deallocate in progress
 *     -1: error occurred
 */
static int _blocks_dealloc(void)
{
	static block_info_msg_t *bg_info_ptr = NULL, *new_bg_ptr = NULL;
	int rc = 0, error_code = 0, i;

	if (bg_info_ptr) {
		error_code = slurm_load_block_info(bg_info_ptr->last_update,
						   &new_bg_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_block_info_msg(bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_block_info((time_t) NULL,
						   &new_bg_ptr, SHOW_ALL);
	}

	if (error_code) {
		error("slurm_load_block_info: %s",
		      slurm_strerror(slurm_get_errno()));
		return -1;
	}
	for (i = 0; i<new_bg_ptr->record_count; i++) {
		if (new_bg_ptr->block_array[i].state == BG_BLOCK_TERM) {
			rc = 1;
			break;
		}
	}
	bg_info_ptr = new_bg_ptr;
	return rc;
}
#else
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	int cur_delay = 0;
	int suspend_time, resume_time, max_delay;
	bool job_killed = false;

	suspend_time = slurm_get_suspend_timeout();
	resume_time  = slurm_get_resume_timeout();
	if (suspend_time || resume_time) {
		max_delay = suspend_time + resume_time;
		max_delay *= 5;		/* Allow for ResumeRate support */
	} else {
		max_delay = 300;	/* Wait to 5 min for PrologSlurmctld */
	}

	if (alloc->alias_list && !xstrcmp(alloc->alias_list, "TBD"))
		saopt.wait_all_nodes = 1;	/* Wait for boot & addresses */
	if (saopt.wait_all_nodes == NO_VAL16)
		saopt.wait_all_nodes = 0;

	for (i = 0; (cur_delay < max_delay); i++) {
		if (i) {
			if (i == 1)
				info("Waiting for resource configuration");
			else
				debug("still waiting");
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(alloc->job_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0) {	/* job killed */
			job_killed = true;
			break;
		}
		if ((rc & READY_JOB_STATE) && 
		    ((rc & READY_NODE_STATE) || !saopt.wait_all_nodes)) {
			is_ready = 1;
			break;
		}
		if (allocation_interrupted || allocation_revoked)
			break;
	}
	if (is_ready) {
		resource_allocation_response_msg_t *resp;
		char *tmp_str;
		if (i > 0)
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
		if (job_killed) {
			error("Job allocation %u has been revoked",
			      alloc->job_id);
			allocation_interrupted = true;
		} else
			error("Nodes %s are still not ready", alloc->node_list);
	} else	/* allocation_interrupted or slurmctld not responing */
		is_ready = 0;

	return is_ready;
}
#endif	/* HAVE_BG */
