/*****************************************************************************\
 *  srun.c - srun handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#define _GNU_SOURCE /* posix_openpt() */

#include <fcntl.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/daemonize.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/setproctitle.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/scrun/scrun.h"

typedef struct {
	char **cmd;
	int i;
} add_arg_t;

data_for_each_cmd_t _add_argv_entry(data_t *data, void *arg)
{
	add_arg_t *args = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		fatal("invalid args data type");

	args->cmd[args->i] = xstrdup(data_get_string_const(data));
	args->i++;

	return DATA_FOR_EACH_CONT;
}

extern char **create_argv(data_t *args)
{
	add_arg_t fargs = {0};

	fargs.cmd = xcalloc(data_get_list_length(args) + 1, sizeof(char *));

	if (data_list_for_each(args, _add_argv_entry, &fargs) < 0)
		fatal("error creating command");

	return fargs.cmd;
}

static void _exec_add(data_t *data, const char *arg)
{
	data_set_string(data_list_append(data), arg);
}

extern void exec_srun_container()
{
	data_t *args, *container_args;
	char *jobid;
	int tty = state.pts;

	args = data_set_list(data_new());

	read_lock_state();

	if (!state.jobid)
		fatal("Unable to start step without a JobId");

	jobid = xstrdup_printf("%u", state.jobid);

	_exec_add(args, "/bin/sh");
	_exec_add(args, "-c");
	_exec_add(args, "exec \"$0\" \"$@\"");

	if (oci_conf->srun_path)
		_exec_add(args, oci_conf->srun_path);
	else /* let sh find srun from PATH */
		_exec_add(args, "srun");

	for (int i = 0; oci_conf->srun_args && oci_conf->srun_args[i]; i++)
		_exec_add(args, oci_conf->srun_args[i]);

	_exec_add(args, "--jobid");
	_exec_add(args, jobid);
	xfree(jobid);
	_exec_add(args, "--job-name");
	_exec_add(args, "scrun");
	_exec_add(args, "--no-kill");
	_exec_add(args, "--container-id");
	_exec_add(args, state.id);
	_exec_add(args, "--container");
	_exec_add(args, state.bundle);
	_exec_add(args, "--export");
	_exec_add(args, "NONE");
	if (state.requested_terminal)
		_exec_add(args, "--pty");
	_exec_add(args, "--");

	container_args = data_resolve_dict_path(state.config, "/process/args/");
	if (!container_args ||
	    data_get_type(container_args) != DATA_TYPE_LIST) {
		fatal("/process/args/ is not a list in config.json");
	} else if (data_get_type(container_args) != DATA_TYPE_LIST) {
		fatal("/process/args/ is not a list in config.json");
	} else {
		data_t *jargs;
		char **argvl;
		const data_t *join[] = { args, container_args, NULL };

		jargs = data_list_join(join, true);
		argvl = create_argv(jargs);
		unlock_state();

		if (get_log_level() >= LOG_LEVEL_DEBUG) {
			for (int i = 0; argvl[i]; i++)
				debug("srun argv[%d]=%s", i, argvl[i]);
		}

		if (state.ptm != -1) {
			/*
			 * Start new session and set pts as controlling tty only
			 * if it was made by anchor.
			 */
			if (setsid() == (pid_t) -1)
				fatal("%s: setsid() failed: %m", __func__);
			if (ioctl(tty, TIOCSCTTY, 0))
				fatal("%s: ioctl(%d, TIOCSCTTY, 0) failed: %m",
				      __func__, tty);
			if (dup2(tty, STDIN_FILENO) != STDIN_FILENO)
				fatal("%s: dup2(%d, STDIN_FILENO) failed: %m",
				      __func__, tty);
			if (dup2(tty, STDOUT_FILENO) != STDOUT_FILENO)
				fatal("%s: dup2(%d, STDOUT_FILENO) failed: %m",
				      __func__, tty);
			if (dup2(tty, STDERR_FILENO) != STDERR_FILENO)
				fatal("%s: dup2(%d, STDERR_FILENO) failed: %m",
				      __func__, tty);
			closeall(STDERR_FILENO + 1);
			log_reinit();
		}

		if (state.requested_terminal && !isatty(STDIN_FILENO))
			fatal("requested_terminal=t but isatty(STDIN_FILENO)=0: %m");

		if (execve(argvl[0], argvl, state.job_env))
			fatal("execv() failed: %m");
	}
}
