/*****************************************************************************\
 *  scrun.c - Slurm OCI container runtime proxy
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

#define _XOPEN_SOURCE 600 /* putenv(), unsetenv() */
#define _GNU_SOURCE /* getopt_long(), get_current_dir_name() */
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/setproctitle.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/select.h"
#include "src/interfaces/serializer.h"

#include "scrun.h"

decl_static_data(usage_txt);

#define ANCHOR_FILE_TPL "scrun-%s-anchor-%s"

static char *slurm_conf_filename = NULL;
static int command_requested = -1;
log_options_t log_opt = LOG_OPTS_STDERR_ONLY;
log_facility_t log_fac = SYSLOG_FACILITY_USER;
char *log_file = NULL;
char *log_format = NULL;
oci_conf_t *oci_conf = NULL;

#ifndef OCI_VERSION
const char *OCI_VERSION = "1.0.0";
#endif

extern void update_logging(void)
{
	bool json = false;

	if (!log_file) {
		/* do nothing */
	} else if (!log_format) {
		json = true;
		log_opt.logfile_fmt = LOG_FILE_FMT_TIMESTAMP;
	} else if (!xstrcasecmp(log_format, "json")) {
		log_opt.logfile_fmt = LOG_FILE_FMT_JSON;
	} else {
		fatal("%s: unknown log format %s",
		      __func__, log_format);
	}

	log_alter(log_opt, log_fac, log_file);

	if (json) {
		/* docker requires RFC3339 timestamps */
		log_set_timefmt(LOG_FMT_RFC3339);
	}
}

/*
 * docker containerd example calls:
 *
 * runc --root /var/run/docker/runtime-runc/moby
 * --log /run/containerd/io.containerd.runtime.v2.task/moby/$LONG_HEX/log.json
 * --log-format json
 * create
 * --bundle /run/containerd/io.containerd.runtime.v2.task/moby/$LONG_HEX
 * --pid-file /run/containerd/io.containerd.runtime.v2.task/moby/$LONG_HEX/init.pid
 * $LONG_HEX
 */

static void _parse_create(int argc, char **argv)
{
#define OPT_LONG_BUNDLE 0x100
#define OPT_LONG_CONSOLE_SOCKET 0x101
#define OPT_LONG_NO_PIVOT 0x102
#define OPT_LONG_NO_NEW_KEYRING 0x103
#define OPT_LONG_PRESERVE_FDS 0x104
#define OPT_LONG_PID_FILE 0x105

	static const struct option long_options[] = {
		{ "bundle", required_argument, NULL, OPT_LONG_BUNDLE },
		{ "console-socket", required_argument, NULL,
		  OPT_LONG_CONSOLE_SOCKET },
		{ "no-pivot", no_argument, NULL, OPT_LONG_NO_PIVOT },
		{ "no-new-keyring", no_argument, NULL,
		  OPT_LONG_NO_NEW_KEYRING },
		{ "preserve-fds", no_argument, NULL, OPT_LONG_PRESERVE_FDS },
		{ "pid-file", required_argument, NULL, OPT_LONG_PID_FILE },
		{ NULL, 0, NULL, 0 }
	};
	int optionIndex = 0;
	int c = 0;

	if (get_log_level() >= LOG_LEVEL_DEBUG2) {
		for (int i = 0; i < argc; i++)
			debug2("create arg[%d]=%s", i, argv[i]);
	}

	while ((c = getopt_long(argc, argv, "b:", long_options,
				&optionIndex)) != -1) {
		switch (c) {
		case OPT_LONG_BUNDLE:
		case 'b':
			xfree(state.bundle);
			state.bundle = xstrdup(optarg);
			state.orig_bundle = xstrdup(optarg);
			break;
		case OPT_LONG_CONSOLE_SOCKET:
			xfree(state.console_socket);
			state.console_socket = xstrdup(optarg);
			break;
		case OPT_LONG_NO_PIVOT:
			info("WARNING: ignoring --no-pivot argument");
			break;
		case OPT_LONG_NO_NEW_KEYRING:
			info("WARNING: ignoring --no-new-keyring argument");
			break;
		case OPT_LONG_PRESERVE_FDS:
			info("WARNING: ignoring --preserve-fds argument");
			break;
		case OPT_LONG_PID_FILE:
			xfree(state.pid_file);
			state.pid_file = xstrdup(optarg);
			break;
		default:
			fatal("unknown argument: %s", argv[optopt]);
		}
	}

	if (optind == (argc - 1)) {
		state.id = xstrdup(argv[optind]);
	} else {
		fatal("container-id not provided");
	}

	if (optind < (argc - 1)) {
		fatal("unexpected argument %d: %s", optind, argv[optind]);
	}

	if (!state.bundle) {
		char *dir = get_current_dir_name();
		state.bundle = xstrdup(dir);
		free(dir);
	}
}

static void _parse_version(int argc, char **argv)
{
	/* does nothing */
}

/*
 * docker containerd example calls:
 *
 * runc --root /var/run/docker/runtime-runc/moby
 * --log /run/containerd/io.containerd.runtime.v2.task/moby/$LONG_HEX/log.json
 * --log-format json
 * start $LONG_HEX
 *
 */
static void _parse_start(int argc, char **argv)
{
	if (argc != 2)
		fatal("Unexpected arguments");

	state.id = xstrdup(argv[1]);
}

static void _parse_state(int argc, char **argv)
{
	if (argc != 2)
		fatal("Unexpected arguments");

	state.id = xstrdup(argv[1]);
}

static void _parse_kill(int argc, char **argv)
{
	int signal;

	if ((argc > 3) || (argc < 2))
		fatal("Unexpected arguments");

	state.id = xstrdup(argv[1]);

	if (argc != 3) {
		debug("defaulting to SIGTERM");
		signal = SIGTERM;
	} else {
		if (isdigit(argv[2][0])) {
			signal = atoi(argv[2]);
		} else {
			signal = sig_name2num(argv[2]);
		}

		if ((signal < 1) || (signal >= SIGRTMAX))
			fatal("Invalid requested signal: %s", argv[2]);
	}

	state.requested_signal = signal;
}

static void _parse_delete(int argc, char **argv)
{
#define OPT_LONG_FORCE 0x100
	static const struct option long_options[] = {
		{ "force", no_argument, NULL, OPT_LONG_FORCE },
		{ NULL, 0, NULL, 0 }
	};
	int optionIndex = 0;
	int c = 0;

	if (get_log_level() >= LOG_LEVEL_DEBUG2) {
		for (int i = 0; i < argc; i++)
			debug2("delete arg[%d]=%s", i, argv[i]);
	}

	while ((c = getopt_long(argc, argv, "f", long_options, &optionIndex)) !=
	       -1) {
		switch (c) {
		case OPT_LONG_FORCE:
		case 'f':
			state.force = true;
			break;
		default:
			error("%s: unknown argument: %s",
			      __func__, argv[optopt]);
		}
	}

	if (optind == (argc - 1)) {
		state.id = xstrdup(argv[optind]);
	} else {
		fatal("container-id not provided");
	}

	if (optind < (argc - 1)) {
		fatal("unexpected argument %d: %s", optind, argv[optind]);
	}
}

/*
 * _usage - print a message describing the command line arguments of slurmrestd
 */
static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	fprintf(stderr, "%s", txt);
	xfree(txt);
}

static void _parse_env(void)
{
	char *buffer = NULL;

	if ((buffer = getenv("SCRUN_DEBUG"))) {
		log_opt.stderr_level = log_string2num(buffer);
		log_opt.syslog_level = log_opt.stderr_level;
		log_opt.logfile_level = log_opt.stderr_level;

		if (log_opt.stderr_level <= 0)
			fatal("Invalid env SCRUN_DEBUG=%s", buffer);

		update_logging();

		debug("%s: SCRUN_DEBUG=%s",
		      __func__, log_num2string(log_opt.stderr_level));
	}

	if ((buffer = getenv("SCRUN_STDERR_DEBUG"))) {
		log_opt.stderr_level = log_string2num(buffer);

		if (log_opt.stderr_level <= 0)
			fatal("Invalid env SCRUN_STDERR_DEBUG=%s", buffer);

		update_logging();

		debug("%s: SCRUN_STDERR_DEBUG=%s",
		      __func__, log_num2string(log_opt.stderr_level));
	}

	if ((buffer = getenv("SCRUN_SYSLOG_DEBUG"))) {
		log_opt.syslog_level = log_string2num(buffer);

		if (log_opt.syslog_level <= 0)
			fatal("Invalid env SCRUN_SYSLOG_DEBUG=%s", buffer);

		update_logging();

		debug("%s: SCRUN_SYSLOG_DEBUG=%s",
		      __func__, log_num2string(log_opt.syslog_level));
	}

	if ((buffer = getenv("SCRUN_FILE_DEBUG"))) {
		log_opt.logfile_level = log_string2num(buffer);

		if (log_opt.logfile_level <= 0)
			fatal("Invalid env SCRUN_FILE_DEBUG=%s", buffer);

		update_logging();

		debug("%s: SCRUN_FILE_DEBUG=%s",
		      __func__, log_num2string(log_opt.logfile_level));
	}
}

/* SIGPIPE handler - mostly a no-op */
static void _sigpipe_handler(int signum)
{
	ssize_t wrote;
	static const char *msg = "scrun: received SIGPIPE";
	/*
	 * Can't use normal logging due to possible dead lock:
	 * debug5("%s: received SIGPIPE", __func__);
	 *
	 * try best effort to log to stderr:
	 */
	wrote = write(STDERR_FILENO, msg, strlen(msg));
	if (wrote < 0)
		fatal("%s: unable to log SIGPIPE: %m", __func__);
}

static void _disable_sigpipe(void)
{
	struct sigaction sigpipe_handler = {
		.sa_handler = _sigpipe_handler,
	};

	if (sigaction(SIGPIPE, &sigpipe_handler, NULL) == -1)
		fatal("%s: unable to control SIGPIPE: %m", __func__);
}

/*
 * Get the path to the assigned unix socket for the container.
 * populates state.anchor_socket if not already set.
 */
static void _get_anchor_socket(void)
{
	char *socket, *user;
	uid_t uid = getuid();
	slurm_hash_t hash = {
		.type = HASH_PLUGIN_K12,
	};

	xassert(!state.anchor_socket);

	if (!(user = uid_to_string_or_null(uid)))
		fatal("Unable to lookup user name for uid:%u", uid);

	socket = xstrdup_printf(ANCHOR_FILE_TPL, user, state.id);

	/*
	 * Container ids are arbitrarily long but unix sockets have a very fixed
	 * max. so generate a nice unique string and then hash it to have the
	 * anchor socket path.
	 */
	hash_g_compute(socket, strlen(socket), NULL, 0, &hash);

	state.anchor_socket = xstrdup_printf(
		"%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x", state.root_dir,
		(int) hash.hash[0], (int) hash.hash[1], (int) hash.hash[2],
		(int) hash.hash[3], (int) hash.hash[4], (int) hash.hash[5],
		(int) hash.hash[6], (int) hash.hash[7], (int) hash.hash[8]);

	debug("%s: anchor socket hash: %s -> %s",
	      __func__, socket, state.anchor_socket);

	xfree(socket);
	xfree(user);
}

static const struct {
	char *command;
	void (*parse)(int argc, char **argv);
	int (*func)(void);
	bool get_anchor_socket;
} commands[] = {
	{ "create", _parse_create, command_create, true },
	{ "start", _parse_start, command_start, true },
	{ "state", _parse_state, command_state, true },
	{ "kill", _parse_kill, command_kill, true },
	{ "delete", _parse_delete, command_delete, true },
	{ "version", _parse_version, command_version, false },
};

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static int _parse_commandline(int argc, char **argv)
{
#define OPT_LONG_CGROUP_MANAGER 0x100
#define OPT_LONG_DEBUG 0x101
#define OPT_LONG_LOG_FILE 0x102
#define OPT_LONG_LOG_FORMAT 0x103
#define OPT_LONG_ROOT 0x104
#define OPT_LONG_ROOTLESS 0x105
#define OPT_LONG_SYSTEMD_CGROUP 0x106
#define OPT_LONG_HELP 0x107
#define OPT_LONG_USAGE 0x108
#define OPT_LONG_VERSION 0x109

	static const struct option long_options[] = {
		{ "cgroup-manager", required_argument, NULL,
		  OPT_LONG_CGROUP_MANAGER },
		{ "debug", no_argument, NULL, OPT_LONG_DEBUG },
		{ "log", required_argument, NULL, OPT_LONG_LOG_FILE },
		{ "log-format", required_argument, NULL, OPT_LONG_LOG_FORMAT },
		{ "root", required_argument, NULL, OPT_LONG_ROOT },
		{ "rootless", required_argument, NULL, OPT_LONG_ROOTLESS },
		{ "systemd-cgroup", no_argument, NULL,
		  OPT_LONG_SYSTEMD_CGROUP },
		{ "help", no_argument, NULL, OPT_LONG_HELP },
		{ "usage", no_argument, NULL, OPT_LONG_USAGE },
		{ "version", no_argument, NULL, OPT_LONG_VERSION },
		{ NULL, 0, NULL, 0 }
	};
	int optionIndex = 0;
	int c = 0;
	int index;

	/* stop processing on first non-arg in getopt_long() */
	if (putenv("POSIXLY_CORRECT=1"))
		fatal("Unable to set POSIXLY_CORRECT in environment: %m");

	optind = 0;
	while ((c = getopt_long(argc, argv, "f:vV?", long_options,
				&optionIndex)) != -1) {
		switch (c) {
		case OPT_LONG_CGROUP_MANAGER:
			info("WARNING: ignoring --cgroup-manager argument");
			break;
		case OPT_LONG_LOG_FILE :
			xfree(log_file);
			log_file = xstrdup(optarg);
			debug("%s: logging to %s", __func__, log_file);
			break;
		case OPT_LONG_LOG_FORMAT:
			xfree(log_format);
			log_format = xstrdup(optarg);
			break;
		case 'f':
			xfree(slurm_conf_filename);
			slurm_conf_filename = xstrdup(optarg);
			break;
		case OPT_LONG_DEBUG:
			log_opt.stderr_level = LOG_LEVEL_DEBUG;
			break;
		case 'v':
			log_opt.stderr_level++;
			break;
		case OPT_LONG_VERSION:
			exit(command_version());
		case 'V':
			break;
		case OPT_LONG_ROOT:
			xfree(state.root_dir);
			state.root_dir = xstrdup(optarg);
			break;
		case OPT_LONG_ROOTLESS:
			info("WARNING: ignoring --rootless argument");
			break;
		case OPT_LONG_SYSTEMD_CGROUP:
			info("WARNING: ignoring --systemd-cgroup argument");
			break;
		case '?':
		case OPT_LONG_HELP:
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		default:
			_usage();
			exit(1);
		}
	}

	if (optind >= argc)
		fatal("command not provided");

	for (int i = 0; i < ARRAY_SIZE(commands); i++)
		if (!xstrcasecmp(argv[optind], commands[i].command))
			command_requested = i;

	if (command_requested == -1)
		fatal("unknown command: %s", argv[optind]);

	/* return next value to start parsing again */
	index = optind + 1;

	/* clear all parsing state for next run */
	optarg = NULL;
	optopt = 0;
	optind = 0;

	/* do not unset POSIXLY_CORRECT as later parsing will get corrupted */

	return index;
}

static int _try_tmp_path(const char *path)
{
	int rc;

	/*
	 * Attempt to verify a given path to act as --root. This check is not
	 * perfect and could easily be considered a TOCTOU violation. That is
	 * not the purpose of this check since this path must be valid for all
	 * calls to scrun and the actually security is checked later when
	 * attempting to either create the spool dir or to access the anchor
	 * socket.
	 */

	if ((rc = access(path, W_OK|R_OK))) {
		debug("%s: access to %s denied: %m", __func__, path);
		return rc;
	}

	debug("%s: access to %s allowed", __func__, path);

	/* update root path */
	xfree(state.root_dir);
	state.root_dir = xstrdup(path);

	return rc;
}

static void _set_root()
{
	const char *epath;
	char *path;
	int rc;

	/*
	 * Attempt to guess best place for root since scrun may be in a user
	 * namespace. Based partially on
	 * https://refspecs.linuxfoundation.org/fhs.shtml
	 */

	/* Try systemd provided tmpdir first */
	if ((epath = getenv("XDG_RUNTIME_DIR")) && !_try_tmp_path(epath))
		return;

	if (!getuid())
		fatal("scrun is being run as root and is likely inside of a username space. Refusing to guess path for --root. It must be explicilty provided.");

	path = xstrdup_printf("/run/user/%d/", getuid());
	rc = _try_tmp_path(path);
	xfree(path);
	if (!rc)
		return;

	if ((epath = getenv("TMPDIR"))) {
		/* assume this is not user specific tmpdir */
		path = xstrdup_printf("%s/%d/", epath, getuid());
		rc = _try_tmp_path(path);
		xfree(path);
		if (!rc)
			return;
	}

	fatal("Unable to determine value for --root. It must be explicitly provided.");
}

extern int main(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int argv_offset;
	char **command_argv;
	int command_argc;

	if (log_init(xbasename(argv[0]), log_opt, log_fac, log_file))
		fatal("Unable to setup logging: %m");

	init_setproctitle(argc, argv);
	_parse_env();
	argv_offset = _parse_commandline(argc, argv) - 1;

	if ((rc = slurm_conf_init(slurm_conf_filename)))
		fatal("%s: Unable to load Slurm configuration: %s", __func__,
		      slurm_strerror(rc));
	if ((rc = hash_g_init()))
		fatal("%s: Unable to load hash plugins: %s", __func__,
		      slurm_strerror(rc));
	if ((rc = select_g_init(false)))
		fatal("%s: Unable to select plugins: %s", __func__,
		      slurm_strerror(rc));
	if ((rc = gres_init()))
		fatal("%s: Unable to GRES plugins: %s", __func__,
		      slurm_strerror(rc));
	if ((rc = get_oci_conf(&oci_conf)))
		fatal("%s: unable to load oci.conf: %s",
		      __func__, slurm_strerror(rc));

	init_state();

	if (!state.root_dir || !state.root_dir[0])
		_set_root();

	if ((rc = data_init()))
		fatal("%s: error loading data: %s", __func__,
		      slurm_strerror(rc));

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL)))
		fatal("%s: error loading JSON parser: %s", __func__,
		      slurm_strerror(rc));

	if (get_log_level() >= LOG_LEVEL_DEBUG2) {
		for (int i = 0; i < argc; i++)
			debug2("%s: %s argv[%d]=%s",
			       __func__, xbasename(argv[0]), i, argv[i]);
	}

	/* extract command from arguments */
	command_argc = argc - argv_offset;
	command_argv = xcalloc(command_argc, sizeof(*command_argv));
	command_argv[0] = argv[0];
	for (int j = 1, i = (argv_offset + 1); i < argc; i++, j++)
		command_argv[j] = argv[i];

	_disable_sigpipe();
	xassert(!state.id);

	commands[command_requested].parse(command_argc, command_argv);

	if (commands[command_requested].get_anchor_socket) {
		xassert(state.id && state.id[0]);
		_get_anchor_socket();
	}

	rc = commands[command_requested].func();

#ifdef MEMORY_LEAK_DEBUG
	destroy_state();
	FREE_NULL_OCI_CONF(oci_conf);
	xfree(slurm_conf_filename);
	xfree(command_argv);
	slurm_auth_fini();
	fini_setproctitle();
	data_fini();
	gres_fini();
	select_g_fini();
	log_fini();
	slurm_conf_destroy();
#endif /* MEMORY_LEAK_DEBUG */

	return rc;
}
