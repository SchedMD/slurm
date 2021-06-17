/*****************************************************************************\
 *  prep_script_slurmctld.c - PrologSlurmctld / EpilogSlurmctld handling
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/prep.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"

#include "prep_script.h"

typedef struct {
	void (*callback) (int rc, uint32_t job_id);
	bool is_epilog;
	uint32_t job_id;
	char *script;
	char **my_env;
} run_script_arg_t;

static char **_build_env(job_record_t *job_ptr, bool is_epilog);
static void *_run_script(void *arg);

extern void slurmctld_script(job_record_t *job_ptr, bool is_epilog)
{
	run_script_arg_t *script_arg = xmalloc(sizeof(*script_arg));

	if (!is_epilog)
		script_arg->callback = prolog_slurmctld_callback;
	else
		script_arg->callback = epilog_slurmctld_callback;

	script_arg->is_epilog = is_epilog;
	script_arg->job_id = job_ptr->job_id;
	if (!is_epilog)
		script_arg->script = xstrdup(slurm_conf.prolog_slurmctld);
	else
		script_arg->script = xstrdup(slurm_conf.epilog_slurmctld);
	script_arg->my_env = _build_env(job_ptr, is_epilog);

	slurmscriptd_run_prepilog(job_ptr->job_id, is_epilog,
				  script_arg->script, script_arg->my_env);

	debug2("%s: creating a new thread for JobId=%u",
	       __func__, script_arg->job_id);
	slurm_thread_create_detached(NULL, _run_script, script_arg);
}

static void _destroy_run_script_arg(run_script_arg_t *script_arg)
{
	xfree(script_arg->script);
	for (int i=0; script_arg->my_env[i]; i++)
		xfree(script_arg->my_env[i]);
	xfree(script_arg->my_env);
	xfree(script_arg);
}

static void *_run_script(void *arg)
{
	run_script_arg_t *script_arg = (run_script_arg_t *) arg;
	pid_t cpid;
	int status, wait_rc;
	char *argv[2];

	argv[0] = script_arg->script;
	argv[1] = NULL;

	if ((cpid = fork()) < 0) {
		status = SLURM_ERROR;
		error("slurmctld_script fork error: %m");
		goto fini;
	} else if (cpid == 0) {
		/* child process */
		closeall(0);
		setpgid(0, 0);
		execve(argv[0], argv, script_arg->my_env);
		_exit(127);
	}

	/* Start tracking this new process */
	track_script_rec_add(script_arg->job_id, cpid, pthread_self());

	while (1) {
		wait_rc = waitpid_timeout(__func__, cpid, &status,
					  slurm_conf.prolog_epilog_timeout);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("%s: waitpid error: %m", __func__);
			break;
		} else if (wait_rc > 0) {
			break;
		}
	}

	if (track_script_broadcast(pthread_self(), status)) {
		info("slurmctld_script JobId=%u %s killed by signal %u",
		     script_arg->job_id,
		     script_arg->is_epilog ? "epilog" : "prolog",
		     WTERMSIG(status));
	} else if (status != 0) {
		error("%s JobId=%u %s exit status %u:%u", __func__,
		      script_arg->job_id,
		      script_arg->is_epilog ? "epilog" : "prolog",
		      WEXITSTATUS(status), WTERMSIG(status));
	} else {
		debug2("%s JobId=%u %s completed", __func__,
		       script_arg->job_id,
		       script_arg->is_epilog ? "epilog" : "prolog");
	}

fini:
	/* let the PrEp plugin control know we've finished */
	if (script_arg->callback)
		(*(script_arg->callback))(status, script_arg->job_id);

	_destroy_run_script_arg(script_arg);

	/*
	 * Use pthread_self here instead of track_script_rec->tid to avoid any
	 * potential for race.
	 */
	track_script_remove(pthread_self());
	return NULL;
}

static char **_build_env(job_record_t *job_ptr, bool is_epilog)
{
	char **my_env;

	my_env = job_common_env_vars(job_ptr, is_epilog);
	setenvf(&my_env, "SLURM_SCRIPT_CONTEXT", "%s_slurmctld",
		is_epilog ? "epilog" : "prolog");

	return my_env;
}
