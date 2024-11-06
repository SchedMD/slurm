/*****************************************************************************\
 *  sackd.c
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

#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/daemonize.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsystemd.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/hash.h"

decl_static_data(usage_txt);

static bool daemonize = true;
static bool original = true;
static bool registered = false;
static bool under_systemd = false;
static char *conf_file = NULL;
static char *conf_server = NULL;
static char *dir = "/run/slurm/conf";

static char **main_argv = NULL;
static int listen_fd = -1;

static void *_try_to_reconfig(void *ptr);

static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	fprintf(stderr, "%s", txt);
	xfree(txt);
}

static void _parse_args(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	int c = 0, option_index = 0;
	char *str = NULL;

	enum {
		LONG_OPT_ENUM_START = 0x100,
		LONG_OPT_CONF_SERVER,
		LONG_OPT_SYSTEMD,
	};

	static struct option long_options[] = {
		{"conf-server", required_argument, 0, LONG_OPT_CONF_SERVER},
		{"systemd", no_argument, 0, LONG_OPT_SYSTEMD},
		{NULL, no_argument, 0, 'v'},
		{NULL, 0, 0, 0}
	};

	if ((str = getenv("SLURM_DEBUG_FLAGS")) &&
	    debug_str2flags(str, &slurm_conf.debug_flags))
		fatal("DebugFlags invalid: %s", str);

	if ((str = getenv("SACKD_DEBUG"))) {
		logopt.stderr_level = logopt.syslog_level = log_string2num(str);

		if (logopt.syslog_level == NO_VAL16)
			fatal("Invalid env SACKD_DEBUG: %s", str);
	}

	if ((str = getenv("SACKD_SYSLOG_DEBUG"))) {
		logopt.syslog_level = log_string2num(str);

		if (logopt.syslog_level == NO_VAL16)
			fatal("Invalid env SACKD_SYSLOG_DEBUG: %s", str);
	}

	if ((str = getenv("SACKD_STDERR_DEBUG"))) {
		logopt.stderr_level = log_string2num(str);

		if (logopt.stderr_level == NO_VAL16)
			fatal("Invalid env SACKD_STDERR_DEBUG: %s", str);
	}

	log_init(xbasename(argv[0]), logopt, 0, NULL);

	opterr = 0;
	while ((c = getopt_long(argc, argv, "Df:hv",
				long_options, &option_index)) != -1) {
		switch (c) {
		case (int) 'D':
			daemonize = 0;
			break;
		case (int) 'f':
			xfree(conf_file);
			conf_file = xstrdup(optarg);
			break;
		case (int) 'h':
			_usage();
			exit(0);
			break;
		case (int) 'v':
			logopt.stderr_level++;
			log_alter(logopt, 0, NULL);
			break;
		case LONG_OPT_CONF_SERVER:
			xfree(conf_server);
			conf_server = xstrdup(optarg);
			break;
		case LONG_OPT_SYSTEMD:
			under_systemd = true;
			break;
		default:
			_usage();
			exit(1);
			break;
		}
	}

	if (under_systemd && !daemonize)
		fatal("--systemd and -D options are mutually exclusive");

	if (under_systemd) {
		if (!getenv("NOTIFY_SOCKET"))
			fatal("Missing NOTIFY_SOCKET");
		daemonize = false;
	}
}

/*
 * Returns true when a local config file is found.
 * Will ensure conf_file is set to avoid slurm_conf_init()
 * needing to make this same decision again later.
 */
static bool _slurm_conf_file_exists(void)
{
	struct stat stat_buf;

	if (conf_file)
		return true;
	if ((conf_file = xstrdup(getenv("SLURM_CONF"))))
		return true;
	if (!stat(default_slurm_config_file, &stat_buf)) {
		conf_file = xstrdup(default_slurm_config_file);
		return true;
	}

	return false;
}

static void _establish_config_source(void)
{
	config_response_msg_t *configs;

	if (!conf_server && _slurm_conf_file_exists()) {
		debug("%s: config will load from file", __func__);
		return;
	}

	/* Reconfigured child process does not need to fetch configs again. */
	if (getenv("SACKD_RECONF_LISTEN_FD")) {
		xstrfmtcat(conf_file, "%s/slurm.conf", dir);
		registered = true;
		return;
	}

	/*
	 * Attempt to create cache dir.
	 * If that fails, attempt to destroy it, then make a new directory.
	 * If that fails again, we're out of luck.
	 */
	if (mkdir(dir, 0755) < 0) {
		(void) rmdir_recursive(dir, true);
		if (mkdir(dir, 0755) < 0)
			fatal("%s: failed to create a clean cache dir at %s",
			      __func__, dir);
	}

	while (!(configs = fetch_config(conf_server, CONFIG_REQUEST_SACKD))) {
		error("Failed to load configs from slurmctld. Retrying in 10 seconds.");
		sleep(10);
	}

	registered = true;

	if (write_configs_to_conf_cache(configs, dir))
		fatal("%s: failed to write configs to cache", __func__);

	slurm_free_config_response_msg(configs);
	xstrfmtcat(conf_file, "%s/slurm.conf", dir);
}

static int _on_msg(conmgr_fd_t *con, slurm_msg_t *msg, int unpack_rc, void *arg)
{
	if (unpack_rc) {
		error("%s: [%s] rejecting malformed RPC and closing connection: %s",
		      __func__, conmgr_fd_get_name(con),
		      slurm_strerror(unpack_rc));
		slurm_free_msg(msg);
		return unpack_rc;
	} else if (!msg->auth_ids_set) {
		error("%s: [%s] rejecting %s RPC with missing user auth",
		      __func__, conmgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type));
		slurm_free_msg(msg);
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	} else if (msg->auth_uid != slurm_conf.slurm_user_id) {
		error("%s: [%s] rejecting %s RPC with user:%u != SlurmUser:%u",
		      __func__, conmgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type), msg->auth_uid,
		      slurm_conf.slurm_user_id);
		slurm_free_msg(msg);
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}

	switch (msg->msg_type) {
	case REQUEST_RECONFIGURE_SACKD:
		info("reconfigure requested by slurmd");
		if (write_configs_to_conf_cache(msg->data, dir))
			error("%s: failed to write configs to cache", __func__);
		slurm_thread_create_detached(_try_to_reconfig, NULL);
		/* no need to respond */
		break;
	default:
		error("%s: [%s] unexpected message %u",
		      __func__, conmgr_fd_get_name(con), msg->msg_type);
	}

	slurm_free_msg(msg);
	conmgr_queue_close_fd(con);
	return SLURM_SUCCESS;
}

static void _listen_for_reconf(void)
{
	int rc = SLURM_SUCCESS;
	static const conmgr_events_t events = {
		.on_msg = _on_msg,
	};

	if (getenv("SACKD_RECONF_LISTEN_FD")) {
		listen_fd = atoi(getenv("SACKD_RECONF_LISTEN_FD"));
	} else if ((listen_fd =
		slurm_init_msg_engine_port(slurm_conf.slurmd_port)) < 0) {
		error("%s: failed to open port: %m", __func__);
		return;
	}

	if ((rc = conmgr_process_fd_listen(listen_fd, CON_TYPE_RPC, &events,
					   CON_FLAG_NONE, NULL)))
		fatal("%s: conmgr refused fd=%d: %s",
		      __func__, listen_fd, slurm_strerror(rc));
}

static void _on_sigint(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGINT. Shutting down.");
	conmgr_request_shutdown();
}

static void _on_sighup(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGHUP. Reconfiguring.");
	slurm_thread_create_detached(_try_to_reconfig, NULL);
}

static void _on_sigusr2(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGUSR2. Ignoring.");
}

static void _on_sigpipe(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("Caught SIGPIPE. Ignoring.");
}

static void *_try_to_reconfig(void *ptr)
{
	extern char **environ;
	struct rlimit rlim;
	char **child_env;
	pid_t pid;
	int to_parent[2] = {-1, -1}, auth_fd = -1;
	int close_skip[] = { -1, -1, -1, -1 }, skip_index = 0;

	if ((auth_fd = auth_g_get_reconfig_fd(AUTH_PLUGIN_SLURM)) >= 0)
		close_skip[skip_index++] = auth_fd;

	conmgr_quiesce(__func__);

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		error("getrlimit(RLIMIT_NOFILE): %m");
		rlim.rlim_cur = 4096;
	}

	child_env = env_array_copy((const char **) environ);
	setenvf(&child_env, "SACKD_RECONF", "1");
	if (listen_fd != -1) {
		setenvf(&child_env, "SACKD_RECONF_LISTEN_FD", "%d", listen_fd);
		fd_set_noclose_on_exec(listen_fd);
		close_skip[skip_index++] = listen_fd;
	}

	if (!daemonize && !under_systemd)
		goto start_child;

	if (pipe(to_parent))
		fatal("%s: pipe() failed: %m", __func__);

	setenvf(&child_env, "SACKD_RECONF_PARENT_FD", "%d", to_parent[1]);
	close_skip[skip_index++] = to_parent[1];

	if ((pid = fork()) < 0) {
		fatal("%s: fork() failed: %m", __func__);
	} else if (pid > 0) {
		pid_t grandchild_pid;
		int rc;
		/*
		 * Close the input side of the pipe so the read() will return
		 * immediately if the child process fatal()s.
		 * Otherwise we'd be stuck here indefinitely assuming another
		 * internal thread might write something to the pipe.
		 */
		(void) close(to_parent[1]);
		safe_read(to_parent[0], &grandchild_pid, sizeof(pid_t));

		info("Relinquishing control to new sackd process");
		if (under_systemd) {
			/*
			 * Ensure child has exited.
			 * Grandchild should be owned by init.
			 */
			waitpid(pid, &rc, 0);
			xsystemd_change_mainpid(grandchild_pid);
		}
		_exit(0);

rwfail:
		close(to_parent[0]);
		env_array_free(child_env);
		waitpid(pid, &rc, 0);
		info("Resuming operation, reconfigure failed.");
		conmgr_unquiesce(__func__);
		return NULL;
	}

start_child:
	closeall_except(3, close_skip);

	/*
	 * This second fork() ensures that the new grandchild's parent is init,
	 * which avoids a nuisance warning from systemd of:
	 * "Supervising process 123456 which is not our child. We'll most likely not notice when it exits"
	 */
	if (under_systemd) {
		if ((pid = fork()) < 0)
			fatal("fork() failed: %m");
		else if (pid)
			exit(0);
	}

	execve(main_argv[0], main_argv, child_env);
	fatal("execv() failed: %m");
}

static void _notify_parent_of_success(void)
{
	char *parent_fd_env = getenv("SACKD_RECONF_PARENT_FD");
	pid_t pid = getpid();
	int fd = -1;

	if (!parent_fd_env)
		return;

	fd = atoi(getenv("SACKD_RECONF_PARENT_FD"));
	info("child started successfully");
	safe_write(fd, &pid, sizeof(pid_t));
	(void) close(fd);
	return;

rwfail:
	error("failed to notify parent, may have two processes running now");
	(void) close(fd);
	return;
}

extern int main(int argc, char **argv)
{
	conmgr_callbacks_t callbacks = {NULL, NULL};
	main_argv = argv;
	_parse_args(argc, argv);

	if (getenv("SACKD_RECONF"))
		original = false;

	if (original && daemonize)
		if (xdaemon())
			error("daemon(): %m");

	conmgr_init(0, 0, callbacks);

	conmgr_add_work_signal(SIGINT, _on_sigint, NULL);
	conmgr_add_work_signal(SIGHUP, _on_sighup, NULL);
	conmgr_add_work_signal(SIGUSR2, _on_sigusr2, NULL);
	conmgr_add_work_signal(SIGPIPE, _on_sigpipe, NULL);

	_establish_config_source();
	slurm_conf_init(conf_file);

	if (getuid() != slurm_conf.slurm_user_id) {
		char *user = uid_to_string(getuid());
		warning("sackd running as %s instead of SlurmUser(%s)",
			user, slurm_conf.slurm_user_name);
		xfree(user);
	}

	if (auth_g_init())
		fatal("auth_g_init() failed");
	if (hash_g_init())
		fatal("hash_g_init() failed");

	if (registered)
		_listen_for_reconf();

	if (!original)
		_notify_parent_of_success();
	else if (under_systemd)
		xsystemd_change_mainpid(getpid());

	info("running");
	conmgr_run(true);

	xfree(conf_file);
	xfree(conf_server);
	return 0;
}
