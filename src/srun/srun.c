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

#include "launch.h"
#include "allocate.h"
#include "srun_job.h"
#include "opt.h"
#include "debugger.h"
#include "src/srun/srun_pty.h"
#include "multi_prog.h"
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

static struct termios termdefaults;
static uint32_t global_rc = 0;
static srun_job_t *job = NULL;

bool srun_max_timer = false;
bool srun_shutdown  = false;
int sig_array[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };

/*
 * forward declaration of static funcs
 */
static void  _pty_restore(void);
static void  _set_exit_code(void);
static void  _set_node_alias(void);
static int   _slurm_debug_env_val (void);
static char *_uint16_array_to_str(int count, const uint16_t *array);

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
	int debug_level;
	env_t *env = xmalloc(sizeof(env_t));
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	bool got_alloc = false;
	slurm_step_io_fds_t cio_fds = SLURM_STEP_IO_FDS_INITIALIZER;
	slurm_step_launch_callbacks_t step_callbacks;

	env->stepid = -1;
	env->procid = -1;
	env->localid = -1;
	env->nodeid = -1;
	env->cli = NULL;
	env->env = NULL;
	env->ckpt_dir = NULL;

	slurm_conf_init(NULL);
	debug_level = _slurm_debug_env_val();
	logopt.stderr_level += debug_level;
	log_init(xbasename(av[0]), logopt, 0, NULL);
	_set_exit_code();

	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );

	if (switch_init() != SLURM_SUCCESS )
		fatal("failed to initialize switch plugin");

	init_srun(ac, av, &logopt, debug_level, 1);
	create_srun_job(&job, &got_alloc, 0, 1);

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
	env->cpu_freq = opt.cpu_freq;
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
		env->partition = job->partition;
		/* If we didn't get the allocation don't overwrite the
		 * previous info.
		 */
		if (got_alloc)
			env->nhosts = job->nhosts;
		env->ntasks = job->ntasks;
		env->task_count = _uint16_array_to_str(job->nhosts, tasks);
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

	memset(&step_callbacks, 0, sizeof(step_callbacks));
	step_callbacks.step_signal   = launch_g_fwd_signal;

	/* re_launch: */
relaunch:
	pre_launch_srun_job(job, 0, 1);

	launch_common_set_stdio_fds(job, &cio_fds);

	if (!launch_g_step_launch(job, &cio_fds, &global_rc, &step_callbacks)) {
		if (launch_g_step_wait(job, got_alloc) == -1)
			goto relaunch;
	}

	fini_srun(job, got_alloc, &global_rc, 0);

	return (int)global_rc;
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

	if (array == NULL)
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

static void _pty_restore(void)
{
	/* STDIN is probably closed by now */
	if (tcsetattr(STDOUT_FILENO, TCSANOW, &termdefaults) < 0)
		fprintf(stderr, "tcsetattr: %s\n", strerror(errno));
}
