/*****************************************************************************\
 *  launch_aprun.c - Define job launch using Cray's aprun.
 *
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#include <stdlib.h>
#include <ctype.h>

#include "src/common/slurm_xlator.h"
#include "src/srun/libsrun/launch.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "launch aprun plugin";
const char plugin_type[]        = "launch/aprun";
const uint32_t plugin_version   = 101;

static pid_t aprun_pid = 0;

/* Convert a SLURM hostlist expression into the equivalent node index
 * value expression.
 */
static char *_get_nids(char *nodelist)
{
	char *nids;
	int i = 0, i2 = 0;

	if (!nodelist)
		return NULL;
//	info("got %s", nodelist);
	nids = xmalloc(sizeof(char) * strlen(nodelist));
	while (nodelist[i] && !isdigit(nodelist[i]))
		i++;

	while (nodelist[i] && nodelist[i] != ']')
		nids[i2++] = nodelist[i++];

//	info("returning %s", nids);
	return nids;
}

static void _unblock_signals (void)
{
	sigset_t set;
	int i;

	for (i = 0; sig_array[i]; i++) {
		/* eliminate pending signals, then set to default */
		xsignal(sig_array[i], SIG_IGN);
		xsignal(sig_array[i], SIG_DFL);
	}
	sigemptyset(&set);
	xsignal_set_mask (&set);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern int launch_p_setup_srun_opt(char **rest)
{
	if (opt.test_only) {
		error("--test-only not supported with aprun");
		exit (1);
	} else if (opt.no_alloc) {
		error("--no-allocate not supported with aprun");
		exit (1);
	}
	if (opt.slurmd_debug != LOG_LEVEL_QUIET) {
		error("--slurmd-debug not supported with aprun");
		opt.slurmd_debug = LOG_LEVEL_QUIET;
	}

	opt.argc++;

	/* We need to do +2 here just in case multi-prog is needed (we
	 * add an extra argv on so just make space for it).
	 */
	opt.argv = (char **) xmalloc((opt.argc + 2) * sizeof(char *));

	opt.argv[0] = xstrdup("aprun");
	/* Set default job name to the executable name rather than
	 * "aprun" */
	if (!opt.job_name_set_cmd && (1 < opt.argc)) {
		opt.job_name_set_cmd = true;
		opt.job_name = xstrdup(rest[0]);
	}

	return 1;
}

extern int launch_p_handle_multi_prog_verify(int command_pos)
{
	return 0;
}

extern int launch_p_create_job_step(srun_job_t *job, bool use_all_cpus,
				    void (*signal_function)(int),
				    sig_atomic_t *destroy_job)
{
	/* FIXME: todo */
	return 0;
}

extern int launch_p_step_launch(
	srun_job_t *job, slurm_step_io_fds_t *cio_fds, uint32_t *global_rc,
	void (*signal_function)(int))
{
	int rc = 0;

	aprun_pid = fork();
	if (aprun_pid < 0) {
		error("fork: %m");
		return 1;
	} else if (aprun_pid > 0) {
		if (waitpid(aprun_pid, &rc, 0) < 0)
			error("Unable to reap aprun child process");
		*global_rc = rc;
		/* Just because waitpid returns something doesn't mean
		   this function failed so always set it back to 0.
		*/
		rc = 0;
	} else {
		setpgrp();
		_unblock_signals();
		/* dup stdio onto our open fds */
		if ((dup2(cio_fds->in.fd, 0) == -1) ||
		    (dup2(cio_fds->out.fd, 1) == -1) ||
		    (dup2(cio_fds->err.fd, 2) == -1)) {
			error("dup2: %m");
			return 1;
		}

		execvp(opt.argv[0], opt.argv);
		error("execv(aprun) error: %m");
		return 1;
	}

	return rc;
}

extern int launch_p_step_wait(srun_job_t *job, bool got_alloc)
{
	return SLURM_SUCCESS;
}

extern int launch_p_step_terminate(void)
{
	return SLURM_SUCCESS;
}


extern void launch_p_print_status(void)
{

}

extern void launch_p_fwd_signal(int signal)
{
	if (aprun_pid)
		kill(aprun_pid, signal);
}
