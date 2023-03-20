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
#include "src/common/oci_config.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#include "src/slurmd/slurmstepd/container.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"

/*
 * We need a location inside of the container that is controlled by Slurm to
 * pass the startup script and I/O handling for batch steps.
 *
 * /tmp/slurm was chosen since runc will always mount it private
 */
#define SLURM_CONTAINER_BATCH_SCRIPT "/tmp/slurm/startup"
#define SLURM_CONTAINER_ENV_FILE "environment"
#define SLURM_CONTAINER_STDIN "/tmp/slurm/stdin"
#define SLURM_CONTAINER_STDOUT "/tmp/slurm/stdout"
#define SLURM_CONTAINER_STDERR "/tmp/slurm/stderr"

oci_conf_t *oci_conf = NULL;

static char *create_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeCreate never configured in oci.conf'; exit 1", NULL };
static char *delete_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeDelete never configured in oci.conf'; exit 1", NULL };
static char *kill_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeKill never configured in oci.conf'; exit 1", NULL };
static char *query_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeQuery never configured in oci.conf'; exit 1", NULL };
static char *run_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeRun never configured in oci.conf'; exit 1", NULL };
static char *start_argv[] = {
	"/bin/sh", "-c", "echo 'RunTimeStart never configured in oci.conf'; exit 1", NULL };

static char *_get_config_path(stepd_step_rec_t *step);
static char *_generate_spooldir(stepd_step_rec_t *step,
				stepd_step_task_info_t *task);
static void _generate_patterns(stepd_step_rec_t *step,
			       stepd_step_task_info_t *task);

static void _dump_command_args(run_command_args_t *args, const char *caller)
{
	if (get_log_level() < LOG_LEVEL_DEBUG3)
		return;

	for (int i = 0; args->script_argv[i]; i++)
		debug3("%s: command argv[%d]=%s",
		       caller, i, args->script_argv[i]);
}

static void _pattern_argv(char **buffer, char **offset, char **cmd_args)
{
	for (char **arg = cmd_args; arg && *arg; arg++) {
		if (arg != cmd_args)
			xstrfmtcatat(*buffer, offset, " ");

		xstrfmtcatat(*buffer, offset, "'");

		/*
		 * POSIX 1003.1 2.2.2 only bans a single quote in single quotes
		 * for escaping
		 */

		for (char *c = *arg; *c != '\0'; c++) {
			if (*c == '\'')
				xstrfmtcatat(*buffer, offset, "'\"'\"'");

			xstrfmtcatat(*buffer, offset, "%c", *c);
		}

		xstrfmtcatat(*buffer, offset, "'");
	}
}

static char *_generate_pattern(const char *pattern, stepd_step_rec_t *step,
			       int task_id, char **cmd_args)
{
	step_container_t *c = step->container;
	char *buffer = NULL, *offset = NULL;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if (!pattern)
		return NULL;

	xassert((task_id == -1) || (step->node_tasks >= task_id));

	for (const char *b = pattern; *b; b++) {
		if (*b == '%') {
			switch (*(++b)) {
			case '%':
				xstrfmtcatat(buffer, &offset, "%s", "%");
				break;
			case '@':
				if (cmd_args)
					_pattern_argv(&buffer, &offset,
						      cmd_args);
				else
					xstrfmtcatat(buffer, &offset,
						     "\"/bin/false\"");
				break;
			case 'b':
				xstrfmtcatat(buffer, &offset, "%s", c->bundle);
				break;
			case 'e':
				xstrfmtcatat(buffer, &offset, "%s/%s",
					     c->spool_dir,
					     SLURM_CONTAINER_ENV_FILE);
				break;
			case 'j':
				xstrfmtcatat(buffer, &offset, "%u",
					     step->step_id.job_id);
				break;
			case 'm':
				xstrfmtcatat(buffer, &offset, "%s",
					     c->spool_dir);
				break;
			case 'n':
				xstrfmtcatat(buffer, &offset, "%s",
					     step->node_name);
				break;
			case 'p':
				if (task_id >= 0)
					xstrfmtcatat(buffer, &offset, "%u",
						     step->task[task_id]->pid);
				else
					xstrfmtcatat(buffer, &offset, "%u",
						     INFINITE);
				break;
			case 'r':
				xstrfmtcatat(buffer, &offset, "%s", c->rootfs);
				break;
			case 's':
				xstrfmtcatat(buffer, &offset, "%u",
					     step->step_id.step_id);
				break;
			case 't':
				xstrfmtcatat(buffer, &offset, "%d", task_id);
				break;
			case 'u':
				xstrfmtcatat(buffer, &offset, "%s",
					     step->user_name);
				break;
			case 'U':
				xstrfmtcatat(buffer, &offset, "%u", step->uid);
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

static int _mkdir(const char *pathname, mode_t mode, uid_t uid, gid_t gid)
{
	int rc;

	if ((rc = mkdir(pathname, mode)))
		rc = errno;
	else {
		/*
		 * Directory was successfully created so it needs user:group set
		 */
		if (chown(pathname, uid, gid) < 0) {
			error("%s: chown(%s): %m", __func__, pathname);
			return errno;
		}

		if (chmod(pathname, mode) < 0) {
			error("%s: chmod(%s, 750): %m", __func__, pathname);
			return errno;
		}

		debug("%s: created %s for %u:%u mode %o",
		      __func__, pathname, uid, gid, mode);

		return SLURM_SUCCESS;
	}

	if (rc == EEXIST)
		return SLURM_SUCCESS;

	error("%s: unable to mkdir(%s): %s",
	      __func__, pathname, slurm_strerror(rc));

	return rc;
}

/*
 * Create entire directory path while setting uid:gid for every newly created
 * directory.
 */
static int _mkpath(const char *pathname, uid_t uid, gid_t gid)
{
	static const mode_t mode = S_IRWXU | S_IRWXG;
	int rc;
	char *p, *dst;

	p = dst = xstrdup(pathname);

	while ((p = xstrchr(p + 1, '/'))) {
		*p = '\0';

		if ((rc = _mkdir(dst, mode, uid, gid)))
			goto cleanup;

		*p = '/';
	}

	/* final directory */
	rc = _mkdir(dst, mode, uid, gid);

cleanup:
	xfree(dst);
	return rc;
}

static int _load_config(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	int rc;
	buf_t *buffer = NULL;
	char *path = _get_config_path(step);

	xassert(c->magic == STEP_CONTAINER_MAGIC);
	xassert(!c->config);
	xassert(path);

	errno = SLURM_SUCCESS;
	if (!(buffer = create_mmap_buf(path))) {
		rc = errno;
		error("%s: unable to open: %s", __func__, path);
		goto cleanup;
	}

	if ((rc = serialize_g_string_to_data(&c->config, get_buf_data(buffer),
					     remaining_buf(buffer),
					     MIME_TYPE_JSON))) {
		error("%s: unable to parse %s: %s",
		      __func__, path, slurm_strerror(rc));
	}

cleanup:
	FREE_NULL_BUFFER(buffer);
	xfree(path);
	return rc;
}

static int _write_config(const stepd_step_rec_t *step, const char *jconfig,
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

	if (chown(jconfig, (uid_t) -1, (gid_t) step->gid) < 0) {
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

static bool _match_env(const data_t *data, void *needle)
{
	bool match;
	const char *needle_name = needle;
	char *name = NULL, *value;

	if (!data_get_string_converted(data, &name)) {
		xfree(name);
		return false;
	}

	value = xstrstr(name, "=");

	if (value)
		*value = '\0';

	match = !xstrcmp(name, needle_name);

	xfree(name);

	return match;
}

static int _modify_config(stepd_step_rec_t *step, stepd_step_task_info_t *task)
{
	step_container_t *c = step->container;
	int rc = SLURM_SUCCESS;
	data_t *mnts, *env, *args;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	/* Disable terminal to ensure stdin/err/out are used */
	data_set_bool(data_define_dict_path(c->config, "/process/terminal/"),
		      false);

	/* point to correct rootfs */
	data_set_string(data_define_dict_path(c->config, "/root/path/"),
			c->rootfs);

	mnts = data_define_dict_path(c->config, "/mounts/");
	if (data_get_type(mnts) != DATA_TYPE_LIST)
		data_set_list(mnts);

	if (c->mount_spool_dir) {
		data_t *mnt = data_set_dict(data_list_append(mnts));
		data_t *opt = data_set_list(data_key_set(mnt, "options"));
		data_set_string(data_key_set(mnt, "destination"),
				c->mount_spool_dir);
		data_set_string(data_key_set(mnt, "type"), "none");
		data_set_string(data_key_set(mnt, "source"), c->spool_dir);
		data_set_string(data_list_append(opt), "bind");
	}

	if (step->batch) {
		data_t *mnt, *opt;

		/*
		 * /dev/null has very special handling in runc and we must make
		 * sure to not conflict with that:
		 * https://github.com/opencontainers/runc/blob/master/libcontainer/rootfs_linux.go#L610-L613
		 */
		if (xstrcmp(step->task[0]->ifname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));
			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDIN);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					step->task[0]->ifname);
			data_set_string(data_list_append(opt), "bind");
		}

		/* Bind mount stdout */
		if (xstrcmp(step->task[0]->ofname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));

			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDOUT);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					step->task[0]->ofname);
			data_set_string(data_list_append(opt), "bind");
		}

		/* Bind mount stderr */
		if (xstrcmp(step->task[0]->efname, "/dev/null")) {
			data_t *mnt = data_set_dict(data_list_append(mnts));
			data_t *opt = data_set_list(
				data_key_set(mnt, "options"));

			data_set_string(data_key_set(mnt, "destination"),
					SLURM_CONTAINER_STDERR);
			data_set_string(data_key_set(mnt, "type"), "none");
			data_set_string(data_key_set(mnt, "source"),
					step->task[0]->efname);
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
				    step->task[0]->argv[0]);
		step->task[0]->argv[0] = xstrdup(SLURM_CONTAINER_BATCH_SCRIPT);
		data_set_string(data_list_append(opt), "bind");
		data_set_string(data_list_append(opt), "ro");
	}

	if (oci_conf->disable_hooks) {
		data_t *hooks = data_resolve_dict_path(c->config, "/hooks/");

		for (int i = 0; oci_conf->disable_hooks[i]; i++) {
			data_t *hook = data_key_get(hooks,
						    oci_conf->disable_hooks[i]);

			if (hook) {
				int count = 0;

				if (data_get_type(hook) == DATA_TYPE_LIST) {
					count = data_get_list_length(hook);
				} else {
					error("Invalid type for hook %s",
					      oci_conf->disable_hooks[i]);
				}

				debug("%s: hook %s found and disabled %d entries",
				      __func__, oci_conf->disable_hooks[i],
				      count);

				data_key_unset(hooks,
					       oci_conf->disable_hooks[i]);
			} else {
				debug("%s: hook %s not found",
				      __func__,  oci_conf->disable_hooks[i]);
			}
		}
	}

	/* overwrite environ with the final step->env contents */
	env = data_set_list(data_define_dict_path(c->config, "/process/env/"));
	for (char **ptr = step->env; *ptr; ptr++) {
		data_t *entry;
		char *name = xstrdup(*ptr);
		char *value = xstrstr(name, "=");

		if (value)
			*value = '\0';

		if (!(entry = data_list_find_first(env, _match_env, name)))
			entry = data_list_append(env);

		data_set_string(entry, *ptr);
		xfree(name);
	}

	args = data_define_dict_path(c->config, "/process/args/");
	data_set_list(args);

	/* move args to the config.json for runtime to handle */
	for (int i = 0; i < task->argc; i++) {
		data_set_string_own(data_list_append(args), task->argv[i]);
		task->argv[i] = NULL;
	}

	return rc;
}

static int _generate_container_paths(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	int rc = SLURM_SUCCESS;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if (c->config) {
		if ((rc = data_retrieve_dict_path_string(c->config,
							 "/root/path/",
							 &c->rootfs))) {
			debug("%s: unable to find /root/path/", __func__);
			return rc;
		}

		if (c->rootfs[0] != '/') {
			/* always provide absolute path */
			char *t = NULL;

			xstrfmtcat(t, "%s/%s", c->bundle, c->rootfs);
			SWAP(c->rootfs, t);
			xfree(t);
		}
	} else {
		/* default to bundle path without config.json */
		c->rootfs = xstrdup(c->bundle);
	}

	/* generate step's spool_dir */
	if (oci_conf->mount_spool_dir) {
		c->mount_spool_dir =
			_generate_pattern(oci_conf->mount_spool_dir, step,
					  step->task[0]->id, NULL);
	} else {
		c->mount_spool_dir = xstrdup("/var/run/slurm/");
	}

	xassert(!c->spool_dir);
	c->spool_dir = _generate_spooldir(step, NULL);

	if ((rc = _mkpath(c->spool_dir, step->uid, step->gid)))
		fatal("%s: unable to create spool directory %s: %s",
		      __func__, c->spool_dir, slurm_strerror(rc));

	return rc;
}

static char *_generate_spooldir(stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	char *path = NULL;
	int id = -1;
	char **argv = NULL;

	if (task) {
		id = task->id;
		argv = task->argv;
	}

	if (oci_conf->container_path) {
		path = _generate_pattern(oci_conf->container_path, step, id,
					 argv);
	} else if (step->step_id.step_id == SLURM_BATCH_SCRIPT) {
		xstrfmtcat(path, "%s/oci-job%05u-batch/task-%05u/",
			   conf->spooldir, step->step_id.job_id, id);
	} else if (step->step_id.step_id == SLURM_INTERACTIVE_STEP) {
		xstrfmtcat(path, "%s/oci-job%05u-interactive/task-%05u/",
			   conf->spooldir, step->step_id.job_id, id);
	} else {
		xstrfmtcat(path, "%s/oci-job%05u-%05u/task-%05u/",
			   conf->spooldir, step->step_id.job_id,
			   step->step_id.step_id, id);
	}

	return path;
}

extern void container_task_init(stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	int rc;
	step_container_t *c = step->container;

	if (!oci_conf) {
		debug2("%s: ignoring step container when oci.conf not configured",
		       __func__);
		return;
	}

	xassert(c->spool_dir);
	xfree(c->spool_dir);

	/* re-generate out the spool_dir now we know the task */
	c->spool_dir = _generate_spooldir(step, task);

	if ((rc = _mkpath(c->spool_dir, step->uid, step->gid)))
		fatal("%s: unable to create spool directory %s: %s",
		      __func__, c->spool_dir, slurm_strerror(rc));
}

static char *_get_config_path(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	char *path = NULL;

	if (!step->container)
		return NULL;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	/* OCI runtime spec reqires config.json to be in root of bundle */
	xstrfmtcat(path, "%s/config.json", c->bundle);

	return path;
}

static data_for_each_cmd_t _foreach_config_env(const data_t *data, void *arg)
{
	int rc;
	stepd_step_rec_t *step = arg;
	char *name = NULL, *value;

	if (data_get_string_converted(data, &name))
		return DATA_FOR_EACH_FAIL;

	value = xstrstr(name, "=");

	if (value) {
		*value = '\0';
		value++;
	}

	rc = setenvf(&step->env, name, "%s", value);

	xfree(name);

	return (rc ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT);
}

static int _merge_step_config_env(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	data_t *env = data_resolve_dict_path(c->config, "/process/env/");

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if (!env)
		return SLURM_SUCCESS;

	xassert(!oci_conf->ignore_config_json);

	if (data_list_for_each_const(env, _foreach_config_env, step) < 0)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

extern int setup_container(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	int rc;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if ((rc = get_oci_conf(&oci_conf)) && (rc != ENOENT)) {
		error("%s: error loading oci.conf: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, step, c->bundle);
		return ESLURM_CONTAINER_NOT_CONFIGURED;
	}

	if ((rc = data_init())) {
		error("Unable to init data structures: %s",
		      slurm_strerror(rc));
		goto error;
	}

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("Unable to load JSON plugin: %s",
		      slurm_strerror(rc));
		goto error;
	}

	if (!oci_conf->ignore_config_json) {
		if ((rc = _load_config(step)))
			goto error;

		if ((rc = _merge_step_config_env(step)))
			goto error;
	}

	if ((rc = _generate_container_paths(step)))
		goto error;

error:
	if (rc)
		error("%s: container setup failed: %s",
		      __func__, slurm_strerror(rc));

	return rc;
}

static data_t *_get_container_state()
{
	int rc = SLURM_ERROR;
	data_t *state = NULL;
	char *out;
	run_command_args_t run_command_args = {
		.max_wait = -1,
		.script_argv = query_argv,
		.script_path = query_argv[0],
		.script_type = "RunTimeQuery",
		.status = &rc,
	};

	/* request container get deleted if known at all any more */
	_dump_command_args(&run_command_args, __func__);
	out = run_command(&run_command_args);
	debug("%s: RunTimeQuery rc:%u output:%s", __func__, rc, out);

	if (!out || !out[0] || rc) {
		error("%s: RunTimeQuery failed rc:%u output:%s", __func__, rc, out);
		return NULL;
	}

	if (serialize_g_string_to_data(&state, out, strlen(out),
				       MIME_TYPE_JSON)) {
		error("%s: unable to parse container state: %s",
		      __func__, out);
		log_flag_hex(STEPS, out, strlen(out),
			     "unable to parse container state response");
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
	char *status = NULL;
	run_command_args_t run_command_args = {
		.max_wait = -1,
	};

	if (!oci_conf->ignore_config_json &&
	    !(status = _get_container_status())) {
		debug("container already dead");
	} else if (!xstrcasecmp(status, "running")) {
		run_command_args.script_argv = kill_argv;
		run_command_args.script_path = kill_argv[0];
		run_command_args.script_type = "RunTimeKill";

		for (int t = 0; t < 10; t++) {
			char *out;
			int kill_status = SLURM_ERROR;
			run_command_args.status = &kill_status;

			xfree(status);
			status = _get_container_status();

			if (!oci_conf->ignore_config_json &&
			    (!status || !xstrcasecmp(status, "stopped")))
				break;

			out = run_command(&run_command_args);
			debug("%s: RunTimeKill rc:%u output:%s",
			      __func__, kill_status, out);
			xfree(out);

			if (oci_conf->ignore_config_json)
				break;

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
		run_command_args.script_argv = delete_argv;
		run_command_args.script_path = delete_argv[0];
		run_command_args.script_type = "RunTimeDelete";
		run_command_args.status = &delete_status;
		_dump_command_args(&run_command_args, __func__);
		out = run_command(&run_command_args);
		debug("%s: RunTimeDelete rc:%u output:%s",
		      __func__, delete_status, out);
		xfree(out);
		xfree(status);
	}
}

static void _run(stepd_step_rec_t *step, stepd_step_task_info_t *task)
{
	debug3("%s: executing: %s", __func__, run_argv[2]);
	execv(run_argv[0], run_argv);
	fatal("execv(%s) failed: %m", run_argv[0]);
}

static void _create_start(stepd_step_rec_t *step,
			  stepd_step_task_info_t *task)
{
	int stime = 250, rc = SLURM_ERROR;
	char *out;
	run_command_args_t run_command_args = {
		.max_wait = -1,
		.status = &rc,
	};

	if (oci_conf->ignore_config_json)
		fatal("IgnoreFileConfigJson=true and RunTimeStart are mutually exclusive");

	run_command_args.script_argv = create_argv;
	run_command_args.script_path = create_argv[0];
	run_command_args.script_type = "RunTimeCreate";
	_dump_command_args(&run_command_args, __func__);
	out = run_command(&run_command_args);
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

	run_command_args.script_argv = start_argv;
	run_command_args.script_path = start_argv[0];
	run_command_args.script_type = "RunTimeStart";
	_dump_command_args(&run_command_args, __func__);
	out = run_command(&run_command_args);
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

static void _generate_patterns(stepd_step_rec_t *step,
			       stepd_step_task_info_t *task)
{
	static bool generated = false;
	char *gen;
	int id = -1;
	char **argv = NULL;

	if (generated)
		return;

	generated = true;

	if (task) {
		id = task->id;
		argv = task->argv;
	}

	gen = _generate_pattern(oci_conf->runtime_create, step, id, argv);
	if (gen)
		create_argv[2] = gen;

	gen = _generate_pattern(oci_conf->runtime_delete, step, id, argv);
	if (gen)
		delete_argv[2] = gen;

	gen = _generate_pattern(oci_conf->runtime_kill, step, id, argv);
	if (gen)
		kill_argv[2] = gen;

	gen = _generate_pattern(oci_conf->runtime_query, step, id, argv);
	if (gen)
		query_argv[2] = gen;

	gen = _generate_pattern(oci_conf->runtime_run, step, id, argv);

	if (gen)
		run_argv[2] = gen;

	gen = _generate_pattern(oci_conf->runtime_start, step, id, argv);
	if (gen)
		start_argv[2] = gen;
}

extern void container_run(stepd_step_rec_t *step,
			  stepd_step_task_info_t *task)
{
	step_container_t *c = step->container;
	int rc;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, step, c->bundle);
		return;
	}

	if (oci_conf->env_exclude_set) {
		char **env = env_array_exclude((const char **) step->env,
					       &oci_conf->env_exclude);
#ifdef MEMORY_LEAK_DEBUG
		env_array_free(step->env);
#endif
		step->env = env;
	}

	if (c->config) {
		int rc;
		char *out = NULL;
		char *jconfig = NULL;

		/* create new config.json in spooldir */
		xstrfmtcat(jconfig, "%s/config.json", c->spool_dir);

		if ((rc = _modify_config(step, task)))
			fatal("%s: configuring container failed: %s",
			      __func__, slurm_strerror(rc));

		if ((rc = serialize_g_data_to_string(&out, NULL, c->config,
						     MIME_TYPE_JSON,
						     SER_FLAGS_PRETTY))) {
			fatal("%s: serialization of config failed: %s",
			      __func__, slurm_strerror(rc));
		}

		FREE_NULL_DATA(c->config);

		if ((rc = _write_config(step, jconfig, out)))
			fatal("%s: unable to write %s: %s",
			      __func__, jconfig, slurm_strerror(rc));

		debug("%s: wrote %s", __func__, jconfig);

		/*
		 * Swap bundle path to spool directory to ensure runtime uses
		 * correct config.json
		 */
		xfree(c->bundle);
		c->bundle = xstrdup(c->spool_dir);

		xfree(out);
		xfree(jconfig);
	}

	if (oci_conf->create_env_file) {
		char *envfile = NULL;
		bool nl = (oci_conf->create_env_file ==
			   NEWLINE_TERMINATED_ENV_FILE);

		/* keep _generate_pattern() in sync with this path */
		xstrfmtcat(envfile, "%s/%s", c->spool_dir,
			   SLURM_CONTAINER_ENV_FILE);

		if ((rc = env_array_to_file(envfile, (const char **) step->env,
				       nl)))
			fatal("%s: unable to write %s: %s",
			      __func__, envfile, slurm_strerror(rc));

		if (chown(envfile, step->uid, step->gid) < 0)
			fatal("%s: chown(%s): %m", __func__, envfile);

		if (!rc && chmod(envfile, 0750) < 0)
			error("%s: chmod(%s, 750): %m", __func__, envfile);

		debug("%s: wrote %s", __func__, envfile);

		xfree(envfile);
	}

	if (oci_conf->runtime_env_exclude_set) {
		extern char **environ;
		char **env = env_array_exclude((const char **) environ,
					       &oci_conf->runtime_env_exclude);

#ifdef MEMORY_LEAK_DEBUG
		env_unset_environment();
#endif
		environ = env;
	}

	debug4("%s: setting cwd from %s to %s",
	       __func__, step->cwd, c->spool_dir);
	xfree(step->cwd);
	step->cwd = xstrdup(c->spool_dir);

	_generate_patterns(step, task);

	if (oci_conf->runtime_run)
		_run(step, task);
	else
		_create_start(step, task);
}

extern void cleanup_container(stepd_step_rec_t *step)
{
	step_container_t *c = step->container;
	char *path;

	xassert(c->magic == STEP_CONTAINER_MAGIC);

	if (!oci_conf) {
		debug("%s: OCI Container not configured. Ignoring %pS requested container: %s",
		      __func__, step, c->bundle);
		return;
	}

	/* cleanup may be called without ever setting up container */
	_generate_patterns(step, NULL);

	_kill_container();

	if (oci_conf->disable_cleanup)
		goto done;

	if (!oci_conf->ignore_config_json && (step->node_tasks > 0)) {
		/* clear every config.json and task dir */
		for (int i = 0; i < step->node_tasks; i++) {
			char *jconfig = NULL;

			path = _generate_spooldir(step, step->task[i]);
			xstrfmtcat(jconfig, "%s/config.json", path);

			if ((unlink(jconfig) < 0) && (errno != ENOENT))
				error("unlink(%s): %m", jconfig);
			xfree(jconfig);

			if (rmdir(path) && (errno != ENOENT))
				error("rmdir(%s): %m", path);
			xfree(path);
		}
	}

	if (oci_conf->create_env_file) {
		char *envfile = NULL;

		/* keep _generate_pattern() in sync with this path */
		xstrfmtcat(envfile, "%s/%s", c->spool_dir,
			   SLURM_CONTAINER_ENV_FILE);

		if (unlink(envfile) && (errno != ENOENT))
			error("unlink(%s): %m", envfile);

		xfree(envfile);
	}

done:
	FREE_NULL_OCI_CONF(oci_conf);
}
