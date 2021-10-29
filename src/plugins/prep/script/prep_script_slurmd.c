/*****************************************************************************\
 *  prep_script_slurmd.c - Prolog / Epilog handling
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

#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugstack.h"
#include "src/common/prep.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/job_container_plugin.h"
#include "src/slurmd/common/run_script.h"
#include "src/slurmd/slurmd/req.h"

#include "prep_script.h"

#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

static char **_build_env(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog);
static int _run_spank_job_script(const char *mode, char **env, uint32_t job_id);

extern int slurmd_script(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog)
{
	char *name = is_epilog ? "epilog" : "prolog";
	char *path = is_epilog ? slurm_conf.epilog : slurm_conf.prolog;
	char **env = _build_env(job_env, cred, is_epilog);
	int status = 0, rc;
	uint32_t jobid = job_env->jobid;
	int timeout = slurm_conf.prolog_epilog_timeout;

	if (timeout == NO_VAL16)
		timeout = -1;

#ifdef HAVE_NATIVE_CRAY
	if (job_env->het_job_id && (job_env->het_job_id != NO_VAL))
		jobid = job_env->het_job_id;
#endif

	/*
	 *  Always run both spank prolog/epilog and real prolog/epilog script,
	 *   even if spank plugins fail. (May want to alter this in the future)
	 *   If both "script" mechanisms fail, prefer to return the "real"
	 *   prolog/epilog status.
	 */
	if ((is_epilog && spank_has_epilog()) ||
	    (!is_epilog && spank_has_prolog()))
		status = _run_spank_job_script(name, env, jobid);
	if ((rc = run_script(name, path, jobid, timeout, env, job_env->uid)))
		status = rc;

	env_array_free(env);

	return status;
}

/* NOTE: call env_array_free() to free returned value */
static char **_build_env(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog)
{
	char **env = env_array_create();
	bool user_name_set = 0;

	env[0] = NULL;
	if (!valid_spank_job_env(job_env->spank_job_env,
				 job_env->spank_job_env_size,
				 job_env->uid)) {
		/* If SPANK job environment is bad, log it and do not use */
		job_env->spank_job_env_size = 0;
		job_env->spank_job_env = (char **) NULL;
	}
	/*
	 * User-controlled environment variables, such as those set through
	 * SPANK, must be prepended with SPANK_ or some other safe prefix.
	 * Otherwise, a malicious user could cause arbitrary code to execute
	 * during the prolog/epilog as root.
	 */
	if (job_env->spank_job_env_size)
		env_array_merge(&env, (const char **) job_env->spank_job_env);
	if (job_env->gres_job_env)
		env_array_merge(&env, (const char **) job_env->gres_job_env);

	setenvf(&env, "SLURMD_NODENAME", "%s", conf->node_name);
	setenvf(&env, "SLURM_CONF", "%s", conf->conffile);
	setenvf(&env, "SLURM_CLUSTER_NAME", "%s", slurm_conf.cluster_name);
	setenvf(&env, "SLURM_JOB_ID", "%u", job_env->jobid);
	setenvf(&env, "SLURM_JOB_UID", "%u", job_env->uid);
	setenvf(&env, "SLURM_JOB_GID", "%u", job_env->gid);
	setenvf(&env, "SLURM_JOB_WORK_DIR", "%s", job_env->work_dir);

#ifndef HAVE_NATIVE_CRAY
	/* uid_to_string on a cray is a heavy call, so try to avoid it */
	if (!job_env->user_name) {
		job_env->user_name = uid_to_string(job_env->uid);
		user_name_set = true;
	}
#endif

	setenvf(&env, "SLURM_JOB_USER", "%s", job_env->user_name);
	if (user_name_set)
		xfree(job_env->user_name);

	setenvf(&env, "SLURM_JOBID", "%u", job_env->jobid);

	if (job_env->het_job_id && (job_env->het_job_id != NO_VAL)) {
		/* Continue support for old hetjob terminology. */
		setenvf(&env, "SLURM_PACK_JOB_ID", "%u", job_env->het_job_id);
		setenvf(&env, "SLURM_HET_JOB_ID", "%u", job_env->het_job_id);
	}

	setenvf(&env, "SLURM_UID", "%u", job_env->uid);

	if (job_env->node_aliases)
		setenvf(&env, "SLURM_NODE_ALIASES", "%s",
			job_env->node_aliases);

	if (job_env->node_list)
		setenvf(&env, "SLURM_NODELIST", "%s", job_env->node_list);

	if (job_env->partition)
		setenvf(&env, "SLURM_JOB_PARTITION", "%s", job_env->partition);

	if (is_epilog)
		setenvf(&env, "SLURM_SCRIPT_CONTEXT", "epilog_slurmd");
	else
		setenvf(&env, "SLURM_SCRIPT_CONTEXT", "prolog_slurmd");

	if (cred) {
		slurm_cred_arg_t cred_arg;
		slurm_cred_get_args(cred, &cred_arg);
		setenvf(&env, "SLURM_JOB_CONSTRAINTS", "%s",
			cred_arg.job_constraints);
		slurm_cred_free_args(&cred_arg);
	}

	return env;
}

static int _run_spank_job_script(const char *mode, char **env, uint32_t job_id)
{
	pid_t cpid;
	int status = 0, timeout;
	int pfds[2];

	if (pipe(pfds) < 0) {
		error("%s: pipe: %m", __func__);
		return SLURM_ERROR;
	}

	fd_set_close_on_exec(pfds[1]);

	debug("%s: calling %s spank %s", __func__, conf->stepd_loc, mode);

	if ((cpid = fork()) < 0) {
		error("%s: fork failed executing spank %s: %m", __func__, mode);
		return SLURM_ERROR;
	} else if (cpid == 0) {
		/* Child Process */
		/* Run slurmstepd spank [prolog|epilog] */
		char *argv[4] = {
			(char *) conf->stepd_loc,
			"spank",
			(char *) mode,
			NULL };

		/*
		 * container_g_join() needs to be called in the child process
		 * to avoid a race condition if this process makes a file
		 * before we add the pid to the container in the parent.
		 */
		if (container_g_join(job_id, getuid()) != SLURM_SUCCESS)
			error("container_g_join(%u): %m", job_id);

		if (dup2(pfds[0], STDIN_FILENO) < 0)
			fatal("dup2: %m");
		setpgid(0, 0);
		execve(argv[0], argv, env);

		error("execve(%s): %m", argv[0]);
		_exit(127);
	}

	/* Parent Process */

	close(pfds[0]);

	if (send_slurmd_conf_lite(pfds[1], conf) < 0)
		error ("Failed to send slurmd conf to slurmstepd\n");
	close(pfds[1]);

	/*
	 * Likely bug: prolog_epilog_timeout is NO_VAL16 if not configured,
	 * leading to this timeout being huge. I suspect a 120-second cap is
	 * meant here, but I'm leaving this behavior in place for the moment.
	 */
	timeout = MAX(slurm_conf.prolog_epilog_timeout, 120);
	if (waitpid_timeout(mode, cpid, &status, timeout) < 0) {
		error("spank/%s timed out after %u secs", mode, timeout);
		return SLURM_ERROR;
	}

	if (status)
		error("spank/%s returned status 0x%04x", mode, status);

	/*
	 *  No longer need SPANK option env vars in environment
	 */
	spank_clear_remote_options_env (env);

	return status;
}
