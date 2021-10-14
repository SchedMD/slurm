/*****************************************************************************\
 *  container.c - slurmstepd container handling
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/run_command.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/read_oci_conf.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"

/*
 * We need a location inside of the container that is controlled by Slurm to
 * pass the startup script and I/O handling for batch steps.
 *
 * /tmp/slurm was choosen since runc will always mount it private
 */
#define SLURM_CONTAINER_BATCH_SCRIPT "/tmp/slurm/startup"
#define SLURM_CONTAINER_ENV_FILE "environment"
#define SLURM_CONTAINER_STDIN "/tmp/slurm/stdin"
#define SLURM_CONTAINER_STDOUT "/tmp/slurm/stdout"
#define SLURM_CONTAINER_STDERR "/tmp/slurm/stderr"

oci_conf_t *oci_conf = NULL;

static char *create_argv[] = {
	"/bin/sh", "-c", "echo 'create disabled'; exit 1", NULL };
static char *delete_argv[] = {
	"/bin/sh", "-c", "echo 'delete disabled'; exit 1", NULL };
static char *kill_argv[] = {
	"/bin/sh", "-c", "echo 'kill disabled'; exit 1", NULL };
static char *query_argv[] = {
	"/bin/sh", "-c", "echo 'query disabled'; exit 1", NULL };
static char *run_argv[] = {
	"/bin/sh", "-c", "echo 'run disabled'; exit 1", NULL };
static char *start_argv[] = {
	"/bin/sh", "-c", "echo 'start disabled'; exit 1", NULL };

static char *_generate_pattern(const char *pattern, stepd_step_rec_t *job,
			       int task_id, char *rootfs_path, char **cmd_args)
{
	char *buffer = NULL, *offset = NULL;

	if (!pattern)
		return NULL;

	for (const char *b = pattern; *b; b++) {
		if (*b == '%') {
			switch (*(++b)) {
			case '%':
				xstrfmtcatat(buffer, &offset, "%s", "%");
				break;
			case '@':
				for (char **arg = cmd_args; arg && *arg;
				     arg++) {
					if (arg != cmd_args)
						xstrfmtcatat(buffer, &offset,
							     " ");

					xstrfmtcatat(buffer, &offset, "'");

					/*
					 * POSIX 1003.1 2.2.2 only bans a single
					 * quote in single quotes for escaping
					 */

					for (char *c = *arg; *c != '\0'; c++) {
						if (*c == '\'')
							xstrfmtcatat(buffer,
								     &offset,
								     "'\"'\"'");

						xstrfmtcatat(buffer, &offset,
							     "%c", *c);
					}

					xstrfmtcatat(buffer, &offset, "'");
				}
				break;
			case 'b':
				xstrfmtcatat(buffer, &offset, "%s", job->cwd);
				break;
			case 'e':
				xstrfmtcatat(buffer, &offset, "%s/%s",
					     job->cwd, SLURM_CONTAINER_ENV_FILE);
				break;
			case 'j':
				xstrfmtcatat(buffer, &offset, "%u",
					     job->step_id.job_id);
				break;
			case 'n':
				xstrfmtcatat(buffer, &offset, "%s",
					     job->node_name);
				break;
			case 'r':
				xstrfmtcatat(buffer, &offset, "%s",
					     rootfs_path);
				break;
			case 's':
				xstrfmtcatat(buffer, &offset, "%u",
					     job->step_id.step_id);
				break;
			case 't':
				xstrfmtcatat(buffer, &offset, "%d", task_id);
				break;
			case 'u':
				xstrfmtcatat(buffer, &offset, "%s",
					     job->user_name);
				break;
			default:
				fatal("%s: unexpected replacement character: %c",
				      __func__, *b);
			}
		} else {
			xstrfmtcatat(buffer, &offset, "%c", *b);
		}
	}

	return buffer;
}

static int _mkpath(const char *path, uid_t uid, gid_t gid)
{
	if ((mkdir(path, 0750) < 0) && (errno != EEXIST)) {
		error("%s: mkdir(%s): %m", __func__, path);
		return errno;
	}

	if (chown(path, uid, gid) < 0) {
		error("%s: chown(%s): %m", __func__, path);
		return errno;
	}

	if (chmod(path, 0750) < 0) {
		error("%s: chmod(%s, 750): %m", __func__, path);
		return errno;
	}

	return SLURM_SUCCESS;
}

static data_t *_read_config(const char *jconfig)
{
	int rc;
	buf_t *buffer = NULL;
	data_t *config = NULL;

	if (!(buffer = create_mmap_buf(jconfig))) {
		error("%s: unable to open: %s", __func__, jconfig);
		goto cleanup;
	}

	if ((rc = data_g_deserialize(&config, get_buf_data(buffer),
				     remaining_buf(buffer), MIME_TYPE_JSON))) {
		error("%s: unable to parse config.json: %s",
		      __func__, slurm_strerror(rc));
	}

cleanup:
	free_buf(buffer);
	buffer = NULL;

	return config;
}

static int _write_config(const stepd_step_rec_t *job, const char *jconfig,
			 const char *out)
{
	int outfd = -1;
	int rc = SLURM_SUCCESS;

	outfd = open(jconfig, (O_WRONLY | O_CREAT | O_EXCL), 0600);
	if (outfd < 0) {
		error("%s: unable to open %s: %m",
		      __func__, jconfig);
		goto rwfail;
	}

	safe_write(outfd, out, strlen(out));

	if (fsync_and_close(outfd, jconfig)) {
		outfd = -1;
		error("%s: failure sync and close of config: %s",
		      __func__, slurm_strerror(rc));
		goto rwfail;
	}

	outfd = -1;

	if (chown(jconfig, (uid_t) -1, (gid_t) job->gid) < 0) {
		error("%s: chown(%s): %m", __func__, jconfig);
		goto rwfail;
	}

	if (chmod(jconfig, 0750) < 0) {
		error("%s: chmod(%s, 750): %m", __func__, jconfig);
		goto rwfail;
	}

	return rc;

rwfail:
	rc = errno;

	if (outfd >= 0)
		close(outfd);

	return rc;
}

static int _modify_config(stepd_step_rec_t *job, data_t *config,
			  char *rootfs_path)
{
	int rc = SLURM_SUCCESS;
	data_t *mnts, *env;
	char **cmd_env = NULL;

	/* Disable terminal to ensure stdin/err/out are used */
	data_set_bool(data_define_dict_path(config, "/process/terminal/"),
		      false);

	/* point to correct rootfs */
	data_set_string_fmt(data_define_dict_path(config, "/root/path/"),
			    "%s/rootfs", job->container);

	mnts = data_define_dict_path(config, "/mounts/");
	if (data_get_type(mnts) != DATA_TYPE_LIST)
		data_set_list(mnts);

	if (job->batch) {
		data_t *mnt, *opt;

		/*
		 * /dev/null has very special handling in runc and we must make
		 * sure to not conflict with that:
		 * https://github.com/opencontainers/runc/blob/master/libcontainer/rootfs_linux.go#L610-L613
		 */
		if (xstrcmp(job->task[0]->ifname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));
			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDIN);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					job->task[0]->ifname);
			data_set_string(data_list_append(opt), "bind");
		}

		/* Bind mount stdout */
		if (xstrcmp(job->task[0]->ofname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));

			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDOUT);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					job->task[0]->ofname);
			data_set_string(data_list_append(opt), "bind");
		}

		/* Bind mount stderr */
		if (xstrcmp(job->task[0]->efname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));

			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDERR);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					job->task[0]->efname);
			data_set_string(data_list_append(opt), "bind");
		}

		/*
		 * Add bind mount of the batch script to allow
		 * the container to execute it directly
		 */
		mnt = data_set_dict(data_list_append(mnts));
		opt = data_set_list(data_key_set(mnt, "options"));

		data_set_string(data_key_set(mnt, "destination"),
				SLURM_CONTAINER_BATCH_SCRIPT);
		data_set_string(data_key_set(mnt, "type"), "none");
		data_set_string_own(data_key_set(mnt, "source"),
				    job->task[0]->argv[0]);
		job->task[0]->argv[0] = xstrdup(SLURM_CONTAINER_BATCH_SCRIPT);
		data_set_string(data_list_append(opt), "bind");
		data_set_string(data_list_append(opt), "ro");
	}

	env = data_define_dict_path(config, "/process/env/");
	if (data_get_type(env) != DATA_TYPE_LIST)
		data_set_list(env);

	if (oci_conf->create_env_file) {
		cmd_env = env_array_create();
		env_array_merge(&cmd_env, (const char **) job->env);
	}

	/* set/append requested env */
	for (char **ptr = job->env; *ptr != NULL; ptr++) {
		data_set_string(data_list_append(env), *ptr);

		if (oci_conf->create_env_file) {
			char *name = xstrdup(*ptr);
			char *value = xstrstr(name, "=");

			if (value) {
				*value = '\0';
				value++;
			}

			env_array_append(&cmd_env, name, value);

			xfree(name);
		}
	}

	if (oci_conf->create_env_file) {
		char *envfile = NULL;

		/* keep _generate_pattern() in sync with this path */
		xstrfmtcat(envfile, "%s/%s",
			   job->cwd, SLURM_CONTAINER_ENV_FILE);

		rc = env_array_to_file(envfile, (const char **) cmd_env);

		if (!rc && chown(envfile, job->uid, job->gid) < 0) {
			error("%s: chown(%s): %m", __func__, envfile);
			rc = errno;
		}

		if (!rc && chmod(envfile, 0750) < 0) {
			error("%s: chmod(%s, 750): %m", __func__, envfile);
			rc = errno;
		}

		xfree(envfile);
	}

	/* Overwrite args */
	if (job->node_tasks <= 0) {
		/* should have been caught at submission */
		error("%s: no node tasks?", __func__);

		return ESLURM_BAD_TASK_COUNT;
	} else if (job->node_tasks > 1) {
		/* should have been caught at submission */
		error("%s: unexpected number of tasks %u > 1",
		      __func__, job->node_tasks);

		return ESLURM_BAD_TASK_COUNT;
	} else {
		/* just 1 task */
		stepd_step_task_info_t *task = job->task[0];
		data_t *args = data_set_list(
			data_define_dict_path(config, "/process/args/"));
		char *gen, **old_argv = task->argv;

		/* move args to the config.json for runtime to handle */
		for (int i = 0; i < task->argc; i++) {
			data_set_string_own(data_list_append(args),
					    task->argv[i]);
			task->argv[i] = NULL;
		}

		/*
		 * containers do not use task->argv but we will leave a canary
		 * to catch any code paths that avoid the container code
		 */
		xassert(job->argv == task->argv);
		job->argv = task->argv = xcalloc(4, sizeof(*task->argv));
		task->argv[0] = xstrdup("/bin/sh");
		task->argv[1] = xstrdup("-c");
		task->argv[2] = xstrdup("echo 'this should never execute with a container'; exit 1");

		/*
		 * Generate all the operations for later while we have all the
		 * info needed
		 */
		gen = _generate_pattern(oci_conf->runtime_create, job, task->id,
					rootfs_path, old_argv);
		if (gen)
			create_argv[2] = gen;

		gen = _generate_pattern(oci_conf->runtime_delete, job, task->id,
					rootfs_path, old_argv);
		if (gen)
			delete_argv[2] = gen;

		gen = _generate_pattern(oci_conf->runtime_kill, job, task->id,
					rootfs_path, old_argv);
		if (gen)
			kill_argv[2] = gen;

		gen = _generate_pattern(oci_conf->runtime_query, job, task->id,
					rootfs_path, old_argv);
		if (gen)
			query_argv[2] = gen;

		gen = _generate_pattern(oci_conf->runtime_run, job, task->id,
					rootfs_path, old_argv);

		if (gen)
			run_argv[2] = gen;

		gen = _generate_pattern(oci_conf->runtime_start, job, task->id,
					rootfs_path, old_argv);
		if (gen)
			start_argv[2] = gen;

		env_array_free(old_argv);
	}

	env_array_free(cmd_env);

	return rc;
}

static void _generate_bundle_path(stepd_step_rec_t *job, char *rootfs_path)
{
	char *path = NULL;

	/* write new config.json in spool dir or requested pattern */
	if (oci_conf->container_path) {
		path = _generate_pattern(oci_conf->container_path, job,
					 job->task[0]->id, rootfs_path, NULL);
	} else if (job->step_id.step_id == SLURM_BATCH_SCRIPT) {
		xstrfmtcat(path, "%s/oci-job%05u-batch/", conf->spooldir,
			   job->step_id.job_id);
	} else if (job->step_id.step_id == SLURM_INTERACTIVE_STEP) {
		xstrfmtcat(path, "%s/oci-job%05u-interactive/", conf->spooldir,
			   job->step_id.job_id);
	} else {
		xstrfmtcat(path, "%s/oci-job%05u-%05u/", conf->spooldir,
			   job->step_id.job_id, job->step_id.step_id);
	}

	debug4("%s: swapping cwd from %s to %s", __func__, job->cwd, path);
	xfree(job->cwd);
	job->cwd = path;
}

extern void setup_container(stepd_step_rec_t *job)
{
	int rc;
	char *jconfig = NULL;
	data_t *config = NULL;
	char *out = NULL;
	char *rootfs_path = NULL;

	if ((rc = get_oci_conf(&oci_conf)) && (rc != ENOENT))
		fatal("Error loading oci.conf: %s", slurm_strerror(rc));

	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, job, job->container);
		return;
	}

	if ((rc = data_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("Unable to load JSON plugin: %s",
		      slurm_strerror(rc));
		goto error;
	}

	/* OCI runtime spec reqires config.json to be in root of bundle */
	xstrfmtcat(jconfig, "%s/config.json", job->container);

	config = _read_config(jconfig);
	if (!config)
		goto error;

	xfree(jconfig);

	if ((rc = data_retrieve_dict_path_string(config, "/root/path/",
						 &rootfs_path))) {
		debug("%s: unable to find /root/path/", __func__);
		goto error;
	}

	if (rootfs_path[0] != '/') {
		/* always provide absolute path */
		char *t = NULL;

		xstrfmtcat(t, "%s/%s", job->container, rootfs_path);
		xfree(rootfs_path);
		rootfs_path = t;
	}

	_generate_bundle_path(job, rootfs_path);

	if ((rc = _mkpath(job->cwd, job->uid, job->gid)))
		goto error;

	xstrfmtcat(jconfig, "%s/config.json", job->cwd);

	if ((rc = _modify_config(job, config, rootfs_path)))
		goto error;

	if ((rc = data_g_serialize(&out, config, MIME_TYPE_JSON,
				   DATA_SER_FLAGS_PRETTY))) {
		error("%s: serialization of config failed: %s",
		      __func__, slurm_strerror(rc));
		goto error;
	}

	FREE_NULL_DATA(config);

	if ((rc = _write_config(job, jconfig, out)))
	    goto error;

error:
	if (rc)
		error("%s: container setup failed: %s",
		      __func__, slurm_strerror(rc));
	xfree(rootfs_path);
	xfree(out);
	xfree(jconfig);
	FREE_NULL_DATA(config);
	xfree(jconfig);
}

static data_t *_get_container_state()
{
	int rc = SLURM_ERROR;
	data_t *state = NULL;
	char *out;

	/* request container get deleted if known at all any more */
	out = run_command("RunTimeQuery", query_argv[0], query_argv, NULL, -1,
			  0, &rc);
	debug("%s: RunTimeQuery rc:%u output:%s", __func__, rc, out);

	if (!out || !out[0]) {
		error("%s: RunTimeQuery failed rc:%u output:%s", __func__, rc, out);
		return NULL;
	}

	if (data_g_deserialize(&state, out, strlen(out), MIME_TYPE_JSON)) {
		error("%s: unable to parse JSON: %s",
		      __func__, out);
	}

	xfree(out);

	return state;
}

static char *_get_container_status()
{
	char *state = NULL;
	data_t *dstate = _get_container_state();

	if (!dstate)
		return NULL;

	if (data_retrieve_dict_path_string(dstate, "/status/", &state))
		debug("%s: unable to find /status", __func__);

	return state;
}

static void _kill_container()
{
	int stime = 2500;
	char *status = _get_container_status();

	if (!status) {
		debug("container already dead");
	} else if (!xstrcasecmp(status, "running")) {
		for (int t = 0; t < 10; t++) {
			char *out;
			int kill_status = SLURM_ERROR;

			xfree(status);
			status = _get_container_status();

			if (!status || !xstrcasecmp(status, "stopped"))
				break;

			out = run_command("RunTimeKill", kill_argv[0],
					  kill_argv, NULL, -1, 0, &kill_status);
			debug("%s: RunTimeKill rc:%u output:%s",
			      __func__, kill_status, out);
			xfree(out);

			/*
			 * use exp backoff up to 1s to wait for the container to
			 * cleanup.
			 *
			 * OCI runtime doesnt provide any way but to poll to see
			 * if the container has been squashed
			 */
			debug("%s: sleeping %dusec to query state again",
			      __func__, stime);
			usleep(stime);

			if (stime > 1000000)
				stime = 1000000;
			else
				stime *= 2;
		}
	}

	if (status) {
		int delete_status = SLURM_ERROR;
		char *out;

		/* request container get deleted if known at all any more */
		out = run_command("RunTimeDelete", delete_argv[0],
				  delete_argv, NULL, -1, 0, &delete_status);
		debug("%s: RunTimeDelete rc:%u output:%s",
		      __func__, delete_status, out);
		xfree(out);
		xfree(status);
	}
}

static void _run(stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
	execv(run_argv[0], run_argv);
	fatal("execv(%s) failed: %m", run_argv[0]);
}

static void _create_start(stepd_step_rec_t *job,
			  stepd_step_task_info_t *task)
{
	int stime = 250, rc = SLURM_ERROR;
	char *out;

	out = run_command("RunTimeCreate", create_argv[0],
			  create_argv, NULL, -1, 0, &rc);
	debug("%s: RunTimeCreate rc:%u output:%s", __func__, rc, out);
	xfree(out);

	/* have to wait here until state finds the container or fail out */
	for (int t = 0; t <= 10; t++) {
		char *status = _get_container_status();

		if (!status) {
			if (t == 10)
				fatal("container never started");

			/* state called before create done */
			if (stime > 1000000)
				stime = 1000000;
			else
				stime *= 2;

			usleep(stime);
			continue;
		}

		debug("container in %s state", status);

		if (!xstrcasecmp(status, "creating")) {
			/* wait for creation to finish */
			xfree(status);
			usleep(250);
		} else if (!xstrcasecmp(status, "created")) {
			xfree(status);
			break;
		} else {
			fatal("%s: unexpected container status: %s",
			      __func__, status);
		}
	}

	out = run_command("RunTimeStart", start_argv[0], start_argv, NULL, -1,
			  0, &rc);
	debug("%s: RunTimeStart rc:%u output:%s", __func__, rc, out);
	xfree(out);

	/*
	 * the inital PID is now dead but the container could still be running
	 * but it likely is running outside of slurmstepd's process group
	 */

	stime = 2500;
	while (true) {
		char *status = _get_container_status();

		if (!status || xstrcasecmp(status, "running")) {
			debug("container no longer running: %s", status);
			xfree(status);
			break;
		}

		xfree(status);

		/* increate wait times exp */
		if (stime > 1000000)
			stime = 1000000;
		else
			stime *= 2;

		usleep(stime);
	}

	/*
	 * since the parent process has exited, kill off the container to kill
	 * any orphan processes
	 */
	_kill_container();

	_exit(rc);
}

extern void container_run(stepd_step_rec_t *job,
			  stepd_step_task_info_t *task)
{
	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, job, job->container);
		return;
	}

	if (oci_conf->runtime_run)
		_run(job, task);
	else
		_create_start(job, task);

}

extern void cleanup_container(stepd_step_rec_t *job)
{
	char *jconfig = NULL;

	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, job, job->container);
		return;
	}

	xstrfmtcat(jconfig, "%s/config.json", job->cwd);

	if (unlink(jconfig) < 0)
		error("unlink(%s): %m", jconfig);

	xfree(jconfig);

	if (rmdir(job->cwd))
		error("rmdir(%s): %m", jconfig);

	_kill_container();

	FREE_NULL_OCI_CONF(oci_conf);
}
