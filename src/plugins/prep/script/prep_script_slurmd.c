/*****************************************************************************\
 *  prep_script_slurmd.c - Prolog / Epilog handling
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include "src/interfaces/prep.h"
#include "src/common/run_command.h"
#include "src/common/spank.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/job_container.h"
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

static list_t *_script_list_create(const char *pattern)
{
	glob_t gl;
	list_t *l = NULL;
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
	uint32_t script_cnt = is_epilog ? slurm_conf.epilog_cnt :
					  slurm_conf.prolog_cnt;
	char **scripts = is_epilog ? slurm_conf.epilog : slurm_conf.prolog;
	char **env = NULL;
	int rc = SLURM_SUCCESS;

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
		rc = _run_spank_job_script(name, env, job_env->jobid);
	}

	if (script_cnt) {
		int status = 0;
		int timeout = slurm_conf.prolog_epilog_timeout;
		char *cmd_argv[2] = {0};
		list_t *path_list = NULL;
		run_command_args_t run_command_args = {
			.job_id = job_env->jobid,
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
		for (int i = 0; i < script_cnt; i++) {
			list_t *tmp_list = _script_list_create(scripts[i]);

			if (!tmp_list) {
				error("%s: Unable to create list of paths [%s]",
				      name, scripts[i]);
				return SLURM_ERROR;
			}

			if (path_list) {
				list_transfer(path_list, tmp_list);
				FREE_NULL_LIST(tmp_list);
			} else {
				path_list = tmp_list;
			}
		}
		list_for_each(
			path_list, _run_subpath_command, &run_command_args);
		FREE_NULL_LIST(path_list);
		if (status)
			rc = status;
	}

	env_array_free(env);

	return rc;
}

/* NOTE: call env_array_free() to free returned value */
static char **_build_env(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog)
{
	char **env = env_array_create();
	bool user_set = false;

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
	setenvf(&env, "SLURM_JOBID", "%u", job_env->jobid);

	if (job_env->het_job_id && (job_env->het_job_id != NO_VAL)) {
		/* Continue support for old hetjob terminology. */
		setenvf(&env, "SLURM_PACK_JOB_ID", "%u", job_env->het_job_id);
		setenvf(&env, "SLURM_HET_JOB_ID", "%u", job_env->het_job_id);
	}

	setenvf(&env, "SLURM_UID", "%u", job_env->uid);

	if (job_env->node_list) {
		setenvf(&env, "SLURM_NODELIST", "%s", job_env->node_list);
		setenvf(&env, "SLURM_JOB_NODELIST", "%s", job_env->node_list);
	}

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
		if (cred_arg->job_core_spec == NO_VAL16) {
			setenvf(&env, "SLURM_JOB_CORE_SPEC_COUNT", "0");
			setenvf(&env, "SLURM_JOB_CORE_SPEC_TYPE", "cores");
		} else if (cred_arg->job_core_spec & CORE_SPEC_THREAD) {
			setenvf(&env, "SLURM_JOB_CORE_SPEC_COUNT", "%u",
				cred_arg->job_core_spec & (~CORE_SPEC_THREAD));
			setenvf(&env, "SLURM_JOB_CORE_SPEC_TYPE", "threads");
		} else {
			setenvf(&env, "SLURM_JOB_CORE_SPEC_COUNT", "%u",
				cred_arg->job_core_spec);
			setenvf(&env, "SLURM_JOB_CORE_SPEC_TYPE", "cores");
		}
		if (cred_arg->job_constraints)
			setenvf(&env, "SLURM_JOB_CONSTRAINTS", "%s",
				cred_arg->job_constraints);
		if (cred_arg->job_end_time)
			setenvf(&env, "SLURM_JOB_END_TIME", "%lu",
				cred_arg->job_end_time);
		if (cred_arg->job_extra)
			setenvf(&env, "SLURM_JOB_EXTRA", "%s",
				cred_arg->job_extra);
		if (cred_arg->cpu_array_count) {
			char *tmp = uint32_compressed_to_str(
				cred_arg->cpu_array_count,
				cred_arg->cpu_array,
				cred_arg->cpu_array_reps);
			setenvf(&env, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp);
			xfree(tmp);
		}
		if (cred_arg->job_licenses)
			setenvf(&env, "SLURM_JOB_LICENSES", "%s",
				cred_arg->job_licenses);
		if (cred_arg->job_ntasks)
			setenvf(&env, "SLURM_JOB_NTASKS", "%u",
				cred_arg->job_ntasks);
		if (cred_arg->job_nhosts)
			setenvf(&env, "SLURM_JOB_NUM_NODES", "%u",
				cred_arg->job_nhosts);
		setenvf(&env, "SLURM_JOB_OVERSUBSCRIBE", "%s",
			job_share_string(cred_arg->job_oversubscribe));
		if (cred_arg->job_partition)
			setenvf(&env, "SLURM_JOB_PARTITION", "%s",
				cred_arg->job_partition);
		if (cred_arg->job_reservation)
			setenvf(&env, "SLURM_JOB_RESERVATION", "%s",
				cred_arg->job_reservation);
		if (cred_arg->job_restart_cnt != INFINITE16)
			setenvf(&env, "SLURM_JOB_RESTART_COUNT", "%u",
				cred_arg->job_restart_cnt);
		if (cred_arg->job_start_time)
			setenvf(&env, "SLURM_JOB_START_TIME", "%lu",
				cred_arg->job_start_time);
		if (cred_arg->job_std_err)
			setenvf(&env, "SLURM_JOB_STDERR", "%s",
				cred_arg->job_std_err);
		if (cred_arg->job_std_in)
			setenvf(&env, "SLURM_JOB_STDIN", "%s",
				cred_arg->job_std_in);
		if (cred_arg->job_std_out)
			setenvf(&env, "SLURM_JOB_STDOUT", "%s",
				cred_arg->job_std_out);
		if (cred_arg->id->pw_name) {
			user_set = true;
			setenvf(&env, "SLURM_JOB_USER", "%s",
				cred_arg->id->pw_name);
		}
		slurm_cred_unlock_args(cred);
	}

	if (!user_set) {
		char *user_name = uid_to_string(job_env->uid);
		setenvf(&env, "SLURM_JOB_USER", "%s", user_name);
		xfree(user_name);
	}

	return env;
}

static void _send_conf_cb(int write_fd, void *arg)
{
	char *spank_mode = arg;

	if (send_slurmd_conf_lite(write_fd, conf) < 0)
		error("%s: Failed to send slurmd conf to slurmstepd for spank/%s",
		       __func__, spank_mode);
}

static int _run_spank_job_script(const char *mode, char **env, uint32_t job_id)
{
	int status;
	char *argv[4];
	char *resp = NULL;
	run_command_args_t run_command_args = {
		.env = env,
		.job_id = job_id,
		.script_path = conf->stepd_loc,
		.script_type = mode,
		.status = &status,
		.write_to_child = true,
	};

	if (slurm_conf.prolog_epilog_timeout == NO_VAL16)
		run_command_args.max_wait = -1;
	else
		run_command_args.max_wait =
			slurm_conf.prolog_epilog_timeout * 1000;

	argv[0] = (char *) conf->stepd_loc;
	argv[1] = "spank";
	argv[2] = (char *) mode;
	argv[3] = NULL;

	run_command_args.script_argv = argv;
	run_command_args.cb = _send_conf_cb;
	run_command_args.cb_arg = (void *) mode;

	debug("%s: calling %s spank %s", __func__, conf->stepd_loc, mode);
	resp = run_command(&run_command_args);

	if (run_command_args.timed_out)
		error("spank/%s timed out", mode);
	if (status)
		error("spank/%s returned status 0x%04x response=%s",
		      mode, status, resp);
	else
		debug2("spank/%s returned success, response=%s",
		      mode, resp);
	xfree(resp);
	/*
	 *  No longer need SPANK option env vars in environment
	 */
	spank_clear_remote_options_env (env);

	return status;
}
