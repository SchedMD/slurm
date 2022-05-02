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

#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugstack.h"
#include "src/common/prep.h"
#include "src/common/run_command.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/job_container_plugin.h"
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


static int _ef(const char *p, int errnum)
{
	return error("prep_script_slurmd: glob: %s: %s", p, strerror(errno));
}

static List _script_list_create(const char *pattern)
{
	glob_t gl;
	List l = NULL;
	int rc;

	if (!pattern)
		return NULL;

	rc = glob(pattern, GLOB_ERR, _ef, &gl);

	switch (rc) {
	case 0:
		l = list_create(xfree_ptr);
		for (size_t i = 0; i < gl.gl_pathc; i++)
			list_push(l, xstrdup(gl.gl_pathv[i]));
		break;
	case GLOB_NOMATCH:
		break;
	case GLOB_NOSPACE:
		error("prep_script_slurmd: glob(3): Out of memory");
		break;
	case GLOB_ABORTED:
		error("prep_script_slurmd: cannot read dir %s: %m", pattern);
		break;
	default:
		error("Unknown glob(3) return code = %d", rc);
		break;
	}

	globfree(&gl);

	return l;
}

static int _run_subpath_command(void *x, void *arg)
{
	run_command_args_t *run_command_args = arg;
	char *resp;
	int rc = 0;

	xassert(run_command_args->script_argv);

	run_command_args->script_path = x;
	run_command_args->script_argv[0] = x;

	resp = run_command(run_command_args);

	if (*run_command_args->status) {
		if (WIFEXITED(*run_command_args->status))
			error("%s failed: rc:%u output:%s",
			      run_command_args->script_type,
			      WEXITSTATUS(*run_command_args->status),
			      resp);
		else if (WIFSIGNALED(*run_command_args->status))
			error("%s killed by signal %u output:%s",
			      run_command_args->script_type,
			      WTERMSIG(*run_command_args->status),
			      resp);
		else
			error("%s didn't run: status:%d reason:%s",
			      run_command_args->script_type,
			      *run_command_args->status,
			      resp);
		rc = -1;
	} else
		debug2("%s success rc:%d output:%s",
		       run_command_args->script_type,
		       *run_command_args->status,
		       resp);
	xfree(resp);

	return rc;
}

extern int slurmd_script(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog)
{
	char *name = is_epilog ? "epilog" : "prolog";
	char *path = is_epilog ? slurm_conf.epilog : slurm_conf.prolog;
	char **env = NULL;
	int status = 0;
	uint32_t jobid = job_env->jobid;

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
	    (!is_epilog && spank_has_prolog())) {
		if (!env)
			env = _build_env(job_env, cred, is_epilog);
		status = _run_spank_job_script(name, env, jobid);
	}

	if (path) {
		int timeout = slurm_conf.prolog_epilog_timeout;
		char *cmd_argv[2] = {0};
		List path_list;
		run_command_args_t run_command_args = {
			.container_join = job_env->container_join,
			.job_id = jobid,
			.script_argv = cmd_argv,
			.script_type = name,
			.status = &status,
		};

		if (!env)
			env = _build_env(job_env, cred, is_epilog);

		if (timeout == NO_VAL16)
			timeout = -1;
		else
			timeout *= 1000;

		run_command_args.env = env;
		run_command_args.max_wait = timeout;

		if (!(path_list = _script_list_create(path)))
			return error("%s: Unable to create list of paths [%s]",
				     name, path);
		list_for_each(
			path_list, _run_subpath_command, &run_command_args);
		FREE_NULL_LIST(path_list);
	}

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

	/*
	 * Overridden by the credential version if available.
	 * Remove two versions after 22.05.
	 */
	if (job_env->partition)
		setenvf(&env, "SLURM_JOB_PARTITION", "%s", job_env->partition);

	if (is_epilog)
		setenvf(&env, "SLURM_SCRIPT_CONTEXT", "epilog_slurmd");
	else
		setenvf(&env, "SLURM_SCRIPT_CONTEXT", "prolog_slurmd");

	if (is_epilog && (job_env->exit_code != INFINITE)) {
		int exit_code = 0, signal = 0;
		if (WIFEXITED(job_env->exit_code))
                        exit_code = WEXITSTATUS(job_env->exit_code);
		if (WIFSIGNALED(job_env->exit_code))
                        signal = WTERMSIG(job_env->exit_code);
		setenvf(&env, "SLURM_JOB_DERIVED_EC", "%u", job_env->derived_ec);
		setenvf(&env, "SLURM_JOB_EXIT_CODE", "%u", job_env->exit_code);
                setenvf(&env, "SLURM_JOB_EXIT_CODE2", "%d:%d", exit_code, signal);
	}

	if (cred) {
		slurm_cred_arg_t *cred_arg = slurm_cred_get_args(cred);

		if (cred_arg->job_account)
			setenvf(&env, "SLURM_JOB_ACCOUNT", "%s",
				cred_arg->job_account);
		if (cred_arg->job_comment)
			setenvf(&env, "SLURM_JOB_COMMENT", "%s",
				cred_arg->job_comment);
		if (cred_arg->job_constraints)
			setenvf(&env, "SLURM_JOB_CONSTRAINTS", "%s",
				cred_arg->job_constraints);
		if (cred_arg->cpu_array_count) {
			char *tmp = uint32_compressed_to_str(
				cred_arg->cpu_array_count,
				cred_arg->cpu_array,
				cred_arg->cpu_array_reps);
			setenvf(&env, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp);
			xfree(tmp);
		}
		if (cred_arg->job_ntasks)
			setenvf(&env, "SLURM_JOB_NTASKS", "%u",
				cred_arg->job_ntasks);
		if (cred_arg->job_nhosts)
			setenvf(&env, "SLURM_JOB_NUM_NODES", "%u",
				cred_arg->job_nhosts);
		if (cred_arg->job_partition)
			setenvf(&env, "SLURM_JOB_PARTITION", "%s",
				cred_arg->job_partition);
		if (cred_arg->job_reservation)
			setenvf(&env, "SLURM_JOB_RESERVATION", "%s",
				cred_arg->job_reservation);
		if (cred_arg->job_restart_cnt != INFINITE16)
			setenvf(&env, "SLURM_JOB_RESTART_COUNT", "%u",
				cred_arg->job_restart_cnt);
		if (cred_arg->job_std_err)
			setenvf(&env, "SLURM_JOB_STDERR", "%s",
				cred_arg->job_std_err);
		if (cred_arg->job_std_in)
			setenvf(&env, "SLURM_JOB_STDIN", "%s",
				cred_arg->job_std_in);
		if (cred_arg->job_std_out)
			setenvf(&env, "SLURM_JOB_STDOUT", "%s",
				cred_arg->job_std_out);

		slurm_cred_unlock_args(cred);
	}

	return env;
}

static int _run_spank_job_script(const char *mode, char **env, uint32_t job_id)
{
	pid_t cpid;
	int status = 0, timeout;
	int pfds[2];
	bool timed_out = false;

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

	if (slurm_conf.prolog_epilog_timeout == NO_VAL16)
		timeout = -1;
	else
		timeout = slurm_conf.prolog_epilog_timeout * 1000;
	if (run_command_waitpid_timeout(mode, cpid, &status, timeout,
					&timed_out) < 0) {
		/*
		 * waitpid returned an error and set errno;
		 * run_command_waitpid_timeout() already logged an error
		 */
		error("error calling waitpid() for spank/%s", mode);
		return SLURM_ERROR;
	} else if (timed_out) {
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
