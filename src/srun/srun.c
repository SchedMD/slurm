/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute
 *	parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#ifdef HAVE_AIX
#  undef HAVE_UNSETENV
#  include <sys/checkpnt.h>
#endif
#ifndef HAVE_UNSETENV
#  include "src/common/unsetenv.h"
#endif

#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <grp.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/mpi.h"
#include "src/common/net.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/launch.h"
#include "src/srun/allocate.h"
#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/debugger.h"
#include "src/srun/srun.h"
#include "src/srun/srun_pty.h"
#include "src/srun/multi_prog.h"
#include "src/api/pmi_server.h"
#include "src/api/step_ctx.h"
#include "src/api/step_launch.h"

#if defined (HAVE_DECL_STRSIGNAL) && !HAVE_DECL_STRSIGNAL
#  ifndef strsignal
 extern char *strsignal(int);
#  endif
#endif /* defined HAVE_DECL_STRSIGNAL && !HAVE_DECL_STRSIGNAL */

#ifndef OPEN_MPI_PORT_ERROR
/* This exit code indicates the launched Open MPI tasks could
 *	not open the reserved port. It was already open by some
 *	other process. */
#define OPEN_MPI_PORT_ERROR 108
#endif

#define MAX_RETRIES 20
#define MAX_ENTRIES 50

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2

mpi_plugin_client_info_t mpi_job_info[1];
static struct termios termdefaults;
uint32_t global_rc = 0;
srun_job_t *job = NULL;

#define MAX_STEP_RETRIES 4
time_t launch_start_time;
bool   retry_step_begin = false;
int    retry_step_cnt = 0;

bool srun_max_timer = false;
bool srun_shutdown  = false;
int sig_array[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };

/*
 * forward declaration of static funcs
 */
static int   _become_user (void);
static int   _call_spank_local_user (srun_job_t *job);
static void  _default_sigaction(int sig);
static void  _define_symbols(void);
static void  _handle_intr(void);
static void  _handle_pipe(void);
static void  _print_job_information(resource_allocation_response_msg_t *resp);
static void  _pty_restore(void);
static void  _run_srun_prolog (srun_job_t *job);
static void  _run_srun_epilog (srun_job_t *job);
static int   _run_srun_script (srun_job_t *job, char *script);
static void  _set_env_vars(resource_allocation_response_msg_t *resp);
static void  _set_exit_code(void);
static void  _set_node_alias(void);
static void  _step_opt_exclusive(void);
static void  _set_stdio_fds(srun_job_t *job, slurm_step_io_fds_t *cio_fds);
static void  _set_submit_dir_env(void);
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static int   _set_umask_env(void);
static void  _shepard_notify(int shepard_fd);
static int   _shepard_spawn(srun_job_t *job, bool got_alloc);
static int   _slurm_debug_env_val (void);
static void *_srun_signal_mgr(void *no_data);
static char *_uint16_array_to_str(int count, const uint16_t *array);
static int   _validate_relative(resource_allocation_response_msg_t *resp);

/*
 * from libvirt-0.6.2 GPL2
 *
 * console.c: A dumb serial console client
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
 *
 */
#ifndef HAVE_CFMAKERAW
void cfmakeraw(struct termios *attr)
{
	attr->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
				| INLCR | IGNCR | ICRNL | IXON);
	attr->c_oflag &= ~OPOST;
	attr->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	attr->c_cflag &= ~(CSIZE | PARENB);
	attr->c_cflag |= CS8;
}
#endif

int srun(int ac, char **av)
{
	resource_allocation_response_msg_t *resp;
	int debug_level;
	env_t *env = xmalloc(sizeof(env_t));
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	pthread_attr_t thread_attr;
	pthread_t signal_thread = (pthread_t) 0;
	bool got_alloc = false;
	int shepard_fd = -1;
	slurm_step_io_fds_t cio_fds;

	env->stepid = -1;
	env->procid = -1;
	env->localid = -1;
	env->nodeid = -1;
	env->cli = NULL;
	env->env = NULL;
	env->ckpt_dir = NULL;

	debug_level = _slurm_debug_env_val();
	logopt.stderr_level += debug_level;
	log_init(xbasename(av[0]), logopt, 0, NULL);
	_set_exit_code();

	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );

	/* This must happen before we spawn any threads
	 * which are not designed to handle them */
	if (xsignal_block(sig_array) < 0)
		error("Unable to block signals");
#ifndef HAVE_CRAY_EMULATION
	if (is_cray_system() || is_cray_select_type()) {
		error("operation not supported on Cray systems - use aprun(1)");
		exit(error_exit);
	}
#endif
	/* Initialize plugin stack, read options from plugins, etc.
	 */
	init_spank_env();
	if (spank_init(NULL) < 0) {
		error("Plug-in initialization failed");
		exit(error_exit);
		_define_symbols();
	}

	/* Be sure to call spank_fini when srun exits.
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	if (initialize_and_process_args(ac, av) < 0) {
		error ("srun initialization failed");
		exit (1);
	}
	record_ppid();

	if (spank_init_post_opt() < 0) {
		error("Plugin stack post-option processing failed.");
		exit(error_exit);
	}

	/* reinit log with new verbosity (if changed by command line)
	 */
	if (_verbose || opt.quiet) {
		/* If log level is already increased, only increment the
		 *   level to the difference of _verbose an LOG_LEVEL_INFO
		 */
		if ((_verbose -= (logopt.stderr_level - LOG_LEVEL_INFO)) > 0)
			logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	} else
		_verbose = debug_level;

	(void) _set_rlimit_env();
	_set_prio_process_env();
	(void) _set_umask_env();
	_set_submit_dir_env();

	/* Set up slurmctld message handler */
	slurmctld_msg_init();

	/* now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (opt.test_only) {
		int rc = allocate_test();
		if (rc) {
			slurm_perror("allocation failure");
			exit (1);
		}
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		job = job_create_noalloc();
		if (create_job_step(job, false) < 0) {
			exit(error_exit);
		}
	} else if ((resp = existing_allocation())) {
		select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET,
					&resp->node_cnt);
		if (opt.nodes_set_env && !opt.nodes_set_opt &&
		    (opt.min_nodes > resp->node_cnt)) {
			/* This signifies the job used the --no-kill option
			 * and a node went DOWN or it used a node count range
			 * specification, was checkpointed from one size and
			 * restarted at a different size */
			error("SLURM_NNODES environment varariable "
			      "conflicts with allocated node count (%u!=%u).",
			      opt.min_nodes, resp->node_cnt);
			/* Modify options to match resource allocation.
			 * NOTE: Some options are not supported */
			opt.min_nodes = resp->node_cnt;
			xfree(opt.alloc_nodelist);
			if (!opt.ntasks_set)
				opt.ntasks = opt.min_nodes;
		}
		if (opt.alloc_nodelist == NULL)
			opt.alloc_nodelist = xstrdup(resp->node_list);
		if (opt.exclusive)
			_step_opt_exclusive();
		_set_env_vars(resp);
		if (_validate_relative(resp))
			exit(error_exit);
		job = job_step_create_allocation(resp);
		slurm_free_resource_allocation_response_msg(resp);

		if (opt.begin != 0) {
			error("--begin is ignored because nodes"
			      " are already allocated.");
		}
		if (!job || create_job_step(job, false) < 0)
			exit(error_exit);
	} else {
		/* Combined job allocation and job step launch */
#if defined HAVE_FRONT_END && (!defined HAVE_BG || defined HAVE_BG_L_P || !defined HAVE_BG_FILES)
		uid_t my_uid = getuid();
		if ((my_uid != 0) &&
		    (my_uid != slurm_get_slurm_user_id())) {
			error("srun task launch not supported on this system");
			exit(error_exit);
		}
#endif
		if (opt.relative_set && opt.relative) {
			fatal("--relative option invalid for job allocation "
			      "request");
		}

		if (!opt.job_name_set_env && opt.job_name_set_cmd)
			setenvfs("SLURM_JOB_NAME=%s", opt.job_name);
		else if (!opt.job_name_set_env && opt.argc)
			setenvfs("SLURM_JOB_NAME=%s", opt.argv[0]);

		if ( !(resp = allocate_nodes()) )
			exit(error_exit);
		got_alloc = true;
		_print_job_information(resp);
		_set_env_vars(resp);
		if (_validate_relative(resp)) {
			slurm_complete_job(resp->job_id, 1);
			exit(error_exit);
		}
		job = job_create_allocation(resp);

		opt.exclusive = false;	/* not applicable for this step */
		opt.time_limit = NO_VAL;/* not applicable for step, only job */
		xfree(opt.constraints);	/* not applicable for this step */
		if (!opt.job_name_set_cmd && opt.job_name_set_env) {
			/* use SLURM_JOB_NAME env var */
			opt.job_name_set_cmd = true;
		}

		/*
		 *  Become --uid user
		 */
		if (_become_user () < 0)
			info("Warning: Unable to assume uid=%u", opt.uid);

		if (!job || create_job_step(job, true) < 0) {
			slurm_complete_job(resp->job_id, 1);
			exit(error_exit);
		}

		slurm_free_resource_allocation_response_msg(resp);
	}

	/*
	 *  Become --uid user
	 */
	if (_become_user () < 0)
		info("Warning: Unable to assume uid=%u", opt.uid);

	/*
	 * Spawn process to insure clean-up of job and/or step on abnormal
	 * termination
	 */
	shepard_fd = _shepard_spawn(job, got_alloc);

	/*
	 *  Enhance environment for job
	 */
	if (opt.cpus_set)
		env->cpus_per_task = opt.cpus_per_task;
	if (opt.ntasks_per_node != NO_VAL)
		env->ntasks_per_node = opt.ntasks_per_node;
	if (opt.ntasks_per_socket != NO_VAL)
		env->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core != NO_VAL)
		env->ntasks_per_core = opt.ntasks_per_core;
	env->distribution = opt.distribution;
	if (opt.plane_size != NO_VAL)
		env->plane_size = opt.plane_size;
	env->cpu_bind_type = opt.cpu_bind_type;
	env->cpu_bind = opt.cpu_bind;
	env->mem_bind_type = opt.mem_bind_type;
	env->mem_bind = opt.mem_bind;
	env->overcommit = opt.overcommit;
	env->slurmd_debug = opt.slurmd_debug;
	env->labelio = opt.labelio;
	env->comm_port = slurmctld_comm_addr.port;
	env->batch_flag = 0;
	if (job) {
		uint16_t *tasks = NULL;
		slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_TASKS,
				   &tasks);

		env->select_jobinfo = job->select_jobinfo;
		env->nodelist = job->nodelist;
		env->task_count = _uint16_array_to_str(
			job->nhosts, tasks);
		env->jobid = job->jobid;
		env->stepid = job->stepid;
	}
	if (opt.pty && (set_winsize(job) < 0)) {
		error("Not using a pseudo-terminal, disregarding --pty option");
		opt.pty = false;
	}
	if (opt.pty) {
		struct termios term;
		int fd = STDIN_FILENO;

		/* Save terminal settings for restore */
		tcgetattr(fd, &termdefaults);
		tcgetattr(fd, &term);
		/* Set raw mode on local tty */
		cfmakeraw(&term);
		tcsetattr(fd, TCSANOW, &term);
		atexit(&_pty_restore);

		block_sigwinch();
		pty_thread_create(job);
		env->pty_port = job->pty_port;
		env->ws_col   = job->ws_col;
		env->ws_row   = job->ws_row;
	}
	setup_env(env, opt.preserve_env);
	xfree(env->task_count);
	xfree(env);
	_set_node_alias();

	if (!signal_thread) {
		slurm_attr_init(&thread_attr);
		while (pthread_create(&signal_thread, &thread_attr,
				      _srun_signal_mgr, NULL)) {
			error("pthread_create error %m");
			sleep(1);
		}
		slurm_attr_destroy(&thread_attr);
	}

	/* re_launch: */
relaunch:
	_run_srun_prolog(job);
	if (_call_spank_local_user (job) < 0) {
		error("Failure in local plugin stack");
		exit(error_exit);
	}
	memset(&cio_fds, 0, sizeof(slurm_step_io_fds_t));
	_set_stdio_fds(job, &cio_fds);

	if (launch_g_step_launch(job, &cio_fds, &global_rc,
				 got_alloc, &srun_shutdown) == -1)
		goto relaunch;

	if (got_alloc) {
		cleanup_allocation();

		/* send the controller we were cancelled */
		if (job->state >= SRUN_JOB_CANCELLED)
			slurm_complete_job(job->jobid, NO_VAL);
		else
			slurm_complete_job(job->jobid, global_rc);
	}
	_shepard_notify(shepard_fd);

	if (signal_thread) {
		srun_shutdown = true;
		pthread_kill(signal_thread, SIGINT);
		pthread_join(signal_thread,  NULL);
	}
	_run_srun_epilog(job);
	slurm_step_ctx_destroy(job->step_ctx);
	mpir_cleanup();
	log_fini();

	if (WIFEXITED(global_rc))
		global_rc = WEXITSTATUS(global_rc);
	return (int)global_rc;
}

static int _call_spank_local_user (srun_job_t *job)
{
	struct spank_launcher_job_info info[1];

	info->uid = opt.uid;
	info->gid = opt.gid;
	info->jobid = job->jobid;
	info->stepid = job->stepid;
	info->step_layout = launch_common_get_slurm_step_layout(job);
	info->argc = opt.argc;
	info->argv = opt.argv;

	return spank_local_user(info);
}


static int _slurm_debug_env_val (void)
{
	long int level = 0;
	const char *val;

	if ((val = getenv ("SLURM_DEBUG"))) {
		char *p;
		if ((level = strtol (val, &p, 10)) < -LOG_LEVEL_INFO)
			level = -LOG_LEVEL_INFO;
		if (p && *p != '\0')
			level = 0;
	}
	return ((int) level);
}

/*
 * Return a string representation of an array of uint32_t elements.
 * Each value in the array is printed in decimal notation and elements
 * are separated by a comma.  If sequential elements in the array
 * contain the same value, the value is written out just once followed
 * by "(xN)", where "N" is the number of times the value is repeated.
 *
 * Example:
 *   The array "1, 2, 1, 1, 1, 3, 2" becomes the string "1,2,1(x3),3,2"
 *
 * Returns an xmalloc'ed string.  Free with xfree().
 */
static char *_uint16_array_to_str(int array_len, const uint16_t *array)
{
	int i;
	int previous = 0;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if(array == NULL)
		return str;

	for (i = 0; i < array_len; i++) {
		if ((i+1 < array_len)
		    && (array[i] == array[i+1])) {
				previous++;
				continue;
		}

		if (i == array_len-1) /* last time through loop */
			sep = "";
		if (previous > 0) {
			xstrfmtcat(str, "%u(x%u)%s",
				   array[i], previous+1, sep);
		} else {
			xstrfmtcat(str, "%u%s", array[i], sep);
		}
		previous = 0;
	}

	return str;
}

static void
_print_job_information(resource_allocation_response_msg_t *resp)
{
	int i;
	char *str = NULL;
	char *sep = "";

	if (!_verbose)
		return;

	xstrfmtcat(str, "jobid %u: nodes(%u):`%s', cpu counts: ",
		   resp->job_id, resp->node_cnt, resp->node_list);

	for (i = 0; i < resp->num_cpu_groups; i++) {
		xstrfmtcat(str, "%s%u(x%u)",
			   sep, resp->cpus_per_node[i],
			   resp->cpu_count_reps[i]);
		sep = ",";
	}
	verbose("%s", str);
	xfree(str);
}

/* Set SLURM_UMASK environment variable with current state */
static int _set_umask_env(void)
{
	char mask_char[5];
	mode_t mask;

	if (getenv("SLURM_UMASK"))	/* use this value */
		return SLURM_SUCCESS;

	mask = (int)umask(0);
	umask(mask);

	sprintf(mask_char, "0%d%d%d",
		((mask>>6)&07), ((mask>>3)&07), mask&07);
	if (setenvf(NULL, "SLURM_UMASK", "%s", mask_char) < 0) {
		error ("unable to set SLURM_UMASK in environment");
		return SLURM_FAILURE;
	}
	debug ("propagating UMASK=%s", mask_char);
	return SLURM_SUCCESS;
}

/* Set SLURM_SUBMIT_DIR environment variable with current state */
static void _set_submit_dir_env(void)
{
	char buf[MAXPATHLEN + 1];

	if ((getcwd(buf, MAXPATHLEN)) == NULL) {
		error("getcwd failed: %m");
		exit(error_exit);
	}

	if (setenvf(NULL, "SLURM_SUBMIT_DIR", "%s", buf) < 0) {
		error("unable to set SLURM_SUBMIT_DIR in environment");
		return;
	}
}

/*
 * _set_prio_process_env
 *
 * Set the internal SLURM_PRIO_PROCESS environment variable to support
 * the propagation of the users nice value and the "PropagatePrioProcess"
 * config keyword.
 */
static void  _set_prio_process_env(void)
{
	int retval;

	errno = 0; /* needed to detect a real failure since prio can be -1 */

	if ((retval = getpriority (PRIO_PROCESS, 0)) == -1)  {
		if (errno) {
			error ("getpriority(PRIO_PROCESS): %m");
			return;
		}
	}

	if (setenvf (NULL, "SLURM_PRIO_PROCESS", "%d", retval) < 0) {
		error ("unable to set SLURM_PRIO_PROCESS in environment");
		return;
	}

	debug ("propagating SLURM_PRIO_PROCESS=%d", retval);
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

static void _set_env_vars(resource_allocation_response_msg_t *resp)
{
	char *tmp;

	if (!getenv("SLURM_JOB_CPUS_PER_NODE")) {
		tmp = uint32_compressed_to_str(resp->num_cpu_groups,
					       resp->cpus_per_node,
					       resp->cpu_count_reps);
		if (setenvf(NULL, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp) < 0) {
			error("unable to set SLURM_JOB_CPUS_PER_NODE in "
			      "environment");
		}
		xfree(tmp);
	}

	if (resp->alias_list) {
		if (setenv("SLURM_NODE_ALIASES", resp->alias_list, 1) < 0) {
			error("unable to set SLURM_NODE_ALIASES in "
			      "environment");
		}
	} else {
		unsetenv("SLURM_NODE_ALIASES");
	}

	return;
}

static int _validate_relative(resource_allocation_response_msg_t *resp)
{

	if (opt.relative_set &&
	    ((opt.relative + opt.min_nodes) > resp->node_cnt)) {
		if (opt.nodes_set_opt) {  /* -N command line option used */
			error("--relative and --nodes option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      opt.relative, opt.min_nodes, resp->node_cnt);
		} else {		/* SLURM_NNODES option used */
			error("--relative and SLURM_NNODES option incompatible "
			      "with count of allocated nodes (%d+%d>%d)",
			      opt.relative, opt.min_nodes, resp->node_cnt);
		}
		return -1;
	}
	return 0;
}

/* Set SLURM_RLIMIT_* environment variables with current resource
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(void)
{
	int                  rc = SLURM_SUCCESS;
	struct rlimit        rlim[1];
	unsigned long        cur;
	char                 name[64], *format;
	slurm_rlimits_info_t *rli;

	/* Modify limits with any command-line options */
	if (opt.propagate && parse_rlimits( opt.propagate, PROPAGATE_RLIMITS)){
		error( "--propagate=%s is not valid.", opt.propagate );
		exit(error_exit);
	}

	for (rli = get_slurm_rlimits_info(); rli->name != NULL; rli++ ) {

		if (rli->propagate_flag != PROPAGATE_RLIMITS)
			continue;

		if (getrlimit (rli->resource, rlim) < 0) {
			error ("getrlimit (RLIMIT_%s): %m", rli->name);
			rc = SLURM_FAILURE;
			continue;
		}

		cur = (unsigned long) rlim->rlim_cur;
		snprintf(name, sizeof(name), "SLURM_RLIMIT_%s", rli->name);
		if (opt.propagate && rli->propagate_flag == PROPAGATE_RLIMITS)
			/*
			 * Prepend 'U' to indicate user requested propagate
			 */
			format = "U%lu";
		else
			format = "%lu";

		if (setenvf (NULL, name, format, cur) < 0) {
			error ("unable to set %s in environment", name);
			rc = SLURM_FAILURE;
			continue;
		}

		debug ("propagating RLIMIT_%s=%lu", rli->name, cur);
	}

	/*
	 *  Now increase NOFILE to the max available for this srun
	 */
	if (getrlimit (RLIMIT_NOFILE, rlim) < 0)
	 	return (error ("getrlimit (RLIMIT_NOFILE): %m"));

	if (rlim->rlim_cur < rlim->rlim_max) {
		rlim->rlim_cur = rlim->rlim_max;
		if (setrlimit (RLIMIT_NOFILE, rlim) < 0)
			return (error ("Unable to increase max no. files: %m"));
	}

	return rc;
}

static void _set_node_alias(void)
{
	char *aliases, *save_ptr = NULL, *tmp;
	char *addr, *hostname, *slurm_name;

	tmp = getenv("SLURM_NODE_ALIASES");
	if (!tmp)
		return;
	aliases = xstrdup(tmp);
	slurm_name = strtok_r(aliases, ":", &save_ptr);
	while (slurm_name) {
		addr = strtok_r(NULL, ":", &save_ptr);
		if (!addr)
			break;
		slurm_reset_alias(slurm_name, addr, addr);
		hostname = strtok_r(NULL, ",", &save_ptr);
		if (!hostname)
			break;
		slurm_name = strtok_r(NULL, ":", &save_ptr);
	}
	xfree(aliases);
}

static int _become_user (void)
{
	char *user = uid_to_string(opt.uid);
	gid_t gid = gid_from_uid(opt.uid);

	if (strcmp(user, "nobody") == 0)
		return (error ("Invalid user id %u: %m", opt.uid));

	if (opt.uid == getuid ())
		return (0);

	if ((opt.egid != (gid_t) -1) && (setgid (opt.egid) < 0))
		return (error ("setgid: %m"));

	initgroups (user, gid); /* Ignore errors */
	xfree(user);

	if (setuid (opt.uid) < 0)
		return (error ("setuid: %m"));

	return (0);
}

static void _run_srun_prolog (srun_job_t *job)
{
	int rc;

	if (opt.prolog && strcasecmp(opt.prolog, "none") != 0) {
		rc = _run_srun_script(job, opt.prolog);
		debug("srun prolog rc = %d", rc);
	}
}

static void _run_srun_epilog (srun_job_t *job)
{
	int rc;

	if (opt.epilog && strcasecmp(opt.epilog, "none") != 0) {
		rc = _run_srun_script(job, opt.epilog);
		debug("srun epilog rc = %d", rc);
	}
}

static int _run_srun_script (srun_job_t *job, char *script)
{
	int status;
	pid_t cpid;
	int i;
	char **args = NULL;

	if (script == NULL || script[0] == '\0')
		return 0;

	if (access(script, R_OK | X_OK) < 0) {
		info("Access denied for %s: %m", script);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error ("run_srun_script: fork: %m");
		return -1;
	}
	if (cpid == 0) {

		/* set the scripts command line arguments to the arguments
		 * for the application, but shifted one higher
		 */
		args = xmalloc(sizeof(char *) * 1024);
		args[0] = script;
		for (i = 0; i < opt.argc; i++) {
			args[i+1] = opt.argv[i];
		}
		args[i+1] = NULL;
		execv(script, args);
		error("help! %m");
		exit(127);
	}

	do {
		if (waitpid(cpid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid: %m");
			return 0;
		} else
			return status;
	} while(1);

	/* NOTREACHED */
}

static int
_is_local_file (fname_t *fname)
{
	if (fname->name == NULL)
		return 1;

	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

static void
_set_stdio_fds(srun_job_t *job, slurm_step_io_fds_t *cio_fds)
{
	bool err_shares_out = false;
	int file_flags;

	if (opt.open_mode == OPEN_MODE_APPEND)
		file_flags = O_CREAT|O_WRONLY|O_APPEND;
	else if (opt.open_mode == OPEN_MODE_TRUNCATE)
		file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
	else {
		slurm_ctl_conf_t *conf;
		conf = slurm_conf_lock();
		if (conf->job_file_append)
			file_flags = O_CREAT|O_WRONLY|O_APPEND;
		else
			file_flags = O_CREAT|O_WRONLY|O_APPEND|O_TRUNC;
		slurm_conf_unlock();
	}

	/*
	 * create stdin file descriptor
	 */
	if (_is_local_file(job->ifname)) {
		if ((job->ifname->name == NULL) ||
		    (job->ifname->taskid != -1)) {
			cio_fds->in.fd = STDIN_FILENO;
		} else {
			cio_fds->in.fd = open(job->ifname->name, O_RDONLY);
			if (cio_fds->in.fd == -1) {
				error("Could not open stdin file: %m");
				exit(error_exit);
			}
		}
		if (job->ifname->type == IO_ONE) {
			cio_fds->in.taskid = job->ifname->taskid;
			cio_fds->in.nodeid = slurm_step_layout_host_id(
				launch_common_get_slurm_step_layout(job),
				job->ifname->taskid);
		}
	}

	/*
	 * create stdout file descriptor
	 */
	if (_is_local_file(job->ofname)) {
		if ((job->ofname->name == NULL) ||
		    (job->ofname->taskid != -1)) {
			cio_fds->out.fd = STDOUT_FILENO;
		} else {
			cio_fds->out.fd = open(job->ofname->name,
					       file_flags, 0644);
			if (cio_fds->out.fd == -1) {
				error("Could not open stdout file: %m");
				exit(error_exit);
			}
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !strcmp(job->ofname->name, job->efname->name)) {
			err_shares_out = true;
		}
	}

	/*
	 * create seperate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
		cio_fds->err.taskid = cio_fds->out.taskid;
	} else if (_is_local_file(job->efname)) {
		if ((job->efname->name == NULL) ||
		    (job->efname->taskid != -1)) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(job->efname->name,
					       file_flags, 0644);
			if (cio_fds->err.fd == -1) {
				error("Could not open stderr file: %m");
				exit(error_exit);
			}
		}
	}
}

/* Plugins must be able to resolve symbols.
 * Since srun statically links with src/api/libslurmhelper rather than
 * dynamicaly linking with libslurm, we need to reference all needed
 * symbols within srun. None of the functions below are actually
 * used, but we need to load the symbols. */
static void _define_symbols(void)
{
	/* needed by mvapich and mpichgm */
	slurm_signal_job_step(NO_VAL, NO_VAL, 0);
}

static void _pty_restore(void)
{
	/* STDIN is probably closed by now */
	if (tcsetattr(STDOUT_FILENO, TCSANOW, &termdefaults) < 0)
		fprintf(stderr, "tcsetattr: %s\n", strerror(errno));
}

/* opt.exclusive is set, disable user task layout controls */
static void _step_opt_exclusive(void)
{
	if (!opt.ntasks_set) {
		error("--ntasks must be set with --exclusive");
		exit(error_exit);
	}
	if (opt.relative_set) {
		error("--relative disabled, incompatible with --exclusive");
		exit(error_exit);
	}
	if (opt.exc_nodes) {
		error("--exclude is incompatible with --exclusive");
		exit(error_exit);
	}
}

/* Return the number of microseconds between tv1 and tv2 with a maximum
 * a maximum value of 10,000,000 to prevent overflows */
static long _diff_tv_str(struct timeval *tv1,struct timeval *tv2)
{
	long delta_t;

	delta_t  = MIN((tv2->tv_sec - tv1->tv_sec), 10);
	delta_t *= 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	return delta_t;
}

static void _handle_intr(void)
{
	static struct timeval last_intr = { 0, 0 };
	static struct timeval last_intr_sent = { 0, 0 };
	struct timeval now;

	gettimeofday(&now, NULL);
	if (!opt.quit_on_intr && (_diff_tv_str(&last_intr, &now) > 1000000)) {
		if  (opt.disable_status) {
			info("sending Ctrl-C to job %u.%u",
			     job->jobid, job->stepid);
			launch_g_fwd_signal(SIGINT);
		} else if (job->state < SRUN_JOB_FORCETERM) {
			info("interrupt (one more within 1 sec to abort)");
			launch_g_print_status();
		} else {
			info("interrupt (abort already in progress)");
			launch_g_print_status();
		}
		last_intr = now;
	} else  { /* second Ctrl-C in half as many seconds */
		update_job_state(job, SRUN_JOB_CANCELLED);
		/* terminate job */
		if (job->state < SRUN_JOB_FORCETERM) {
			if (_diff_tv_str(&last_intr_sent, &now) < 1000000) {
				job_force_termination(job);
				launch_g_fwd_signal(SIGKILL);
				return;
			}

			info("sending Ctrl-C to job %u.%u",
			     job->jobid, job->stepid);
			last_intr_sent = now;
			launch_g_fwd_signal(SIGINT);
		} else
			job_force_termination(job);

		launch_g_fwd_signal(SIGKILL);
	}
}
static void _default_sigaction(int sig)
{
	struct sigaction act;
	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return;
	}
	if (act.sa_handler != SIG_IGN)
		return;

	act.sa_handler = SIG_DFL;
	if (sigaction(sig, &act, NULL))
		error("sigaction(%d): %m", sig);
}

static void _handle_pipe(void)
{
	static int ending = 0;

	if (ending)
		return;
	ending = 1;
	launch_g_fwd_signal(SIGKILL);
}

/* _srun_signal_mgr - Process daemon-wide signals */
static void *_srun_signal_mgr(void *no_data)
{
	int sig;
	int i, rc;
	sigset_t set;

	/* Make sure no required signals are ignored (possibly inherited) */
	for (i = 0; sig_array[i]; i++)
		_default_sigaction(sig_array[i]);
	while (!srun_shutdown) {
		xsignal_sigset_create(sig_array, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGINT:
			if (!srun_shutdown)
				_handle_intr();
			break;
		case SIGQUIT:
			info("Quit");
			/* continue with slurm_step_launch_abort */
		case SIGTERM:
		case SIGHUP:
			/* No need to call job_force_termination here since we
			 * are ending the job now and we don't need to update
			 * the state. */
			info("forcing job termination");
			launch_g_fwd_signal(SIGKILL);
			break;
		case SIGCONT:
			info("got SIGCONT");
			break;
		case SIGPIPE:
			_handle_pipe();
			break;
		case SIGALRM:
			if (srun_max_timer) {
				info("First task exited %ds ago", opt.max_wait);
				launch_g_print_status();
				launch_g_step_terminate();
			}
			break;
		default:
			launch_g_fwd_signal(sig);
			break;
		}
	}
	return NULL;
}

static void  _shepard_notify(int shepard_fd)
{
	int rc;

	while (1) {
		rc = write(shepard_fd, "", 1);
		if (rc == -1) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("write(shepard): %m");
		}
		break;
	}
	close(shepard_fd);
}

static int _shepard_spawn(srun_job_t *job, bool got_alloc)
{
	int shepard_pipe[2], rc;
	pid_t shepard_pid;
	char buf[1];

	if (pipe(shepard_pipe)) {
		error("pipe: %m");
		return -1;
	}

	shepard_pid = fork();
	if (shepard_pid == -1) {
		error("fork: %m");
		return -1;
	}
	if (shepard_pid != 0) {
		close(shepard_pipe[0]);
		return shepard_pipe[1];
	}

	/* Wait for parent to notify of completion or I/O error on abort */
	close(shepard_pipe[1]);
	while (1) {
		rc = read(shepard_pipe[0], buf, 1);
		if (rc == 1) {
			exit(0);
		} else if (rc == 0) {
			break;	/* EOF */
		} else if (rc == -1) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			break;
		}
	}

	(void) slurm_terminate_job_step(job->jobid, job->stepid);
	if (got_alloc)
		slurm_complete_job(job->jobid, NO_VAL);
	exit(0);
	return -1;
}
