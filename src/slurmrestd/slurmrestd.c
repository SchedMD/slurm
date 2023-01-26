/*****************************************************************************\
 *  slurmrestd.c - Slurm REST API daemon
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#define _GNU_SOURCE

#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/conmgr.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/openapi.h"
#include "src/interfaces/select.h"
#include "src/interfaces/serializer.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/rest_auth.h"

decl_static_data(usage_txt);

typedef struct {
	bool stdin_tty; /* running with a TTY for stdin */
	bool stdin_socket; /* running with a socket for stdin */
	bool stderr_tty; /* running with a TTY for stderr */
	bool stdout_tty; /* running with a TTY for stdout */
	bool stdout_socket; /* running with a socket for stdout */
	bool listen; /* running in listening daemon mode aka not INET mode */
} run_mode_t;

/* Debug level to use */
static int debug_level = 0;
/* detected run mode */
static run_mode_t run_mode = { 0 };
/* Listen string */
static List socket_listen = NULL;
static char *slurm_conf_filename = NULL;
/* Number of requested threads */
static int thread_count = 20;
/* User to become once loaded */
static uid_t uid = 0;
static gid_t gid = 0;

static char *rest_auth = NULL;
static plugin_handle_t *auth_plugin_handles = NULL;
static char **auth_plugin_types = NULL;
static size_t auth_plugin_count = 0;
static plugrack_t *auth_rack = NULL;

static char *oas_specs = NULL;
bool unshare_sysv = true;
bool unshare_files = true;
bool check_user = true;
bool become_user = false;

extern parsed_host_port_t *parse_host_port(const char *str);
extern void free_parse_host_port(parsed_host_port_t *parsed);

/* SIGPIPE handler - mostly a no-op */
static void _sigpipe_handler(int signum)
{
	debug5("%s: received SIGPIPE", __func__);
}

static void _parse_env(void)
{
	char *buffer = NULL;

	if ((buffer = getenv("SLURMRESTD_DEBUG")) != NULL) {
		debug_level = log_string2num(buffer);

		if (debug_level <= 0)
			fatal("Invalid env SLURMRESTD_DEBUG: %s", buffer);
	}

	if ((buffer = getenv("SLURMRESTD_LISTEN")) != NULL) {
		/* split comma delimited list */
		char *toklist = xstrdup(buffer);
		char *ptr1 = NULL, *ptr2 = NULL;

		ptr1 = strtok_r(toklist, ",", &ptr2);
		while (ptr1) {
			list_append(socket_listen, xstrdup(ptr1));
			ptr1 = strtok_r(NULL, ",", &ptr2);
		}
		xfree(toklist);
	}

	if ((buffer = getenv("SLURMRESTD_AUTH_TYPES"))) {
		xfree(rest_auth);
		rest_auth = xstrdup(buffer);
	}

	if ((buffer = getenv("SLURMRESTD_OPENAPI_PLUGINS")) != NULL) {
		xfree(oas_specs);
		oas_specs = xstrdup(buffer);
	}

	if ((buffer = getenv("SLURMRESTD_SECURITY"))) {
		char *token = NULL, *save_ptr = NULL;
		char *toklist = xstrdup(buffer);

		token = strtok_r(toklist, ",", &save_ptr);
		while (token) {
			if (!xstrcasecmp(token, "disable_unshare_sysv")) {
				unshare_sysv = false;
			} else if (!xstrcasecmp(token,
						"disable_unshare_files")) {
				unshare_files = false;
			} else if (!xstrcasecmp(token, "disable_user_check")) {
				check_user = false;
			} else if (!xstrcasecmp(token, "become_user")) {
				become_user = true;
			} else {
				fatal("Unexpected value in SLURMRESTD_SECURITY=%s",
				      token);
			}
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(toklist);
	}
}

static void _examine_stdin(void)
{
	struct stat status = { 0 };

	if (fstat(STDIN_FILENO, &status))
		fatal("unable to stat STDIN: %m");

	if ((status.st_mode & S_IFMT) == S_IFSOCK)
		run_mode.stdin_socket = true;

	if (isatty(STDIN_FILENO))
		run_mode.stdin_tty = true;
}

static void _examine_stderr(void)
{
	struct stat status = { 0 };

	if (fstat(STDERR_FILENO, &status))
		fatal("unable to stat STDERR: %m");

	if (isatty(STDERR_FILENO))
		run_mode.stderr_tty = true;
}

static void _examine_stdout(void)
{
	struct stat status = { 0 };

	if (fstat(STDOUT_FILENO, &status))
		fatal("unable to stat STDOUT: %m");

	if ((status.st_mode & S_IFMT) == S_IFSOCK)
		run_mode.stdout_socket = true;

	if (isatty(STDOUT_FILENO))
		run_mode.stdout_tty = true;
}

static void _setup_logging(int argc, char **argv)
{
	/* Default to logging as a daemon */
	log_options_t logopt = LOG_OPTS_INITIALIZER;
	log_facility_t fac = SYSLOG_FACILITY_DAEMON;

	/* increase debug level as requested */
	logopt.syslog_level += debug_level;

	if (run_mode.stderr_tty) {
		/* Log to stderr if it is a tty */
		logopt = (log_options_t) LOG_OPTS_STDERR_ONLY;
		fac = SYSLOG_FACILITY_USER;
		logopt.stderr_level += debug_level;
	}

	if (log_init(xbasename(argv[0]), logopt, fac, NULL))
		fatal("Unable to setup logging: %m");
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

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static void _parse_commandline(int argc, char **argv)
{
	int c = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "a:f:g:hs:t:u:vV")) != -1) {
		switch (c) {
		case 'a':
			xfree(rest_auth);
			rest_auth = xstrdup(optarg);
			break;
		case 'f':
			xfree(slurm_conf_filename);
			slurm_conf_filename = xstrdup(optarg);
			break;
		case 'g':
			if (gid_from_string(optarg, &gid))
				fatal("Unable to resolve gid: %s", optarg);
			break;
		case 'h':
			_usage();
			exit(0);
			break;
		case 's':
			xfree(oas_specs);
			oas_specs = xstrdup(optarg);
			break;
		case 't':
			thread_count = atoi(optarg);
			break;
		case 'u':
			if (uid_from_string(optarg, &uid))
				fatal("Unable to resolve user: %s", optarg);
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		default:
			_usage();
			exit(1);
		}
	}

	while (optind < argc) {
		list_append(socket_listen, xstrdup(argv[optind]));
		optind++;
	}
}

/*
 * slurmrestd is merely a translator from REST to Slurm.
 * Try to lock down any extra unneeded permissions.
 */
static void _lock_down(void)
{
	if ((getuid() == SLURM_AUTH_NOBODY) || (getgid() == SLURM_AUTH_NOBODY))
		fatal("slurmrestd must not be run as nobody");

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		fatal("Unable to disable new privileges: %m");
	if (unshare_sysv && unshare(CLONE_SYSVSEM))
		fatal("Unable to unshare System V namespace: %m");
	if (unshare_files && unshare(CLONE_FILES))
		fatal("Unable to unshare file descriptors: %m");
	if (gid && setgroups(0, NULL))
		fatal("Unable to drop supplementary groups: %m");
	if (uid != 0 && (gid == 0))
		gid = gid_from_uid(uid);
	if (gid != 0 && setgid(gid))
		fatal("Unable to setgid: %m");
	if (uid != 0 && setuid(uid))
		fatal("Unable to setuid: %m");
	if (check_user && !become_user && (getuid() == 0))
		fatal("slurmrestd should not be run as the root user.");
	if (check_user && !become_user && (getgid() == 0))
		fatal("slurmrestd should not be run with the root goup.");

	if (become_user && getuid())
		fatal("slurmrestd must run as root in become_user mode");
	else if (check_user && (slurm_conf.slurm_user_id == getuid()))
		fatal("slurmrestd should not be run as SlurmUser");

	if (become_user && getgid())
		fatal("slurmrestd must run as root in become_user mode");
	else if (check_user &&
		 (gid_from_uid(slurm_conf.slurm_user_id) == getgid()))
		fatal("slurmrestd should not be run with SlurmUser's group.");
}

/* simple wrapper to hand over operations router in http context */
static void *_setup_http_context(con_mgr_fd_t *con, void *arg)
{
	xassert(operations_router == arg);
	return setup_http_context(con, operations_router);
}

static void _auth_plugrack_foreach(const char *full_type, const char *fq_path,
				   const plugin_handle_t id, void *arg)
{
	auth_plugin_count += 1;
	xrecalloc(auth_plugin_handles, auth_plugin_count,
		  sizeof(*auth_plugin_handles));
	xrecalloc(auth_plugin_types, auth_plugin_count,
		  sizeof(*auth_plugin_types));

	auth_plugin_types[auth_plugin_count - 1] = xstrdup(full_type);
	auth_plugin_handles[auth_plugin_count - 1] = id;

	debug5("%s: auth plugin type:%s path:%s",
	       __func__, full_type, fq_path);
}

static void _plugrack_foreach_list(const char *full_type, const char *fq_path,
				   const plugin_handle_t id, void *arg)
{
	info("%s", full_type);
}

static int _op_handler_openapi(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp, void *auth)
{
	return get_openapi_specification(openapi_state, resp);
}

int main(int argc, char **argv)
{
	int rc = SLURM_SUCCESS, parse_rc = SLURM_SUCCESS;
	struct sigaction sigpipe_handler = { .sa_handler = _sigpipe_handler };
	socket_listen = list_create(xfree_ptr);
	con_mgr_t *conmgr = NULL;
	con_mgr_events_t conmgr_events = {
		.on_data = parse_http,
		.on_connection = _setup_http_context,
		.on_finish = on_http_connection_finish,
	};
	static const con_mgr_callbacks_t callbacks = {
		.parse = parse_host_port,
		.free_parse = free_parse_host_port,
	};

	if (sigaction(SIGPIPE, &sigpipe_handler, NULL) == -1)
		fatal("%s: unable to control SIGPIPE: %m", __func__);

	_parse_env();
	_parse_commandline(argc, argv);

	/* attempt to release all unneeded permissions */
	_lock_down();

	_examine_stdin();
	_examine_stderr();
	_examine_stdout();
	_setup_logging(argc, argv);

	run_mode.listen = !list_is_empty(socket_listen);

	slurm_init(slurm_conf_filename);

	if (thread_count < 2)
		fatal("Request at least 2 threads for processing");
	if (thread_count > 1024)
		fatal("Excessive thread count");

	if (data_init())
		fatal("Unable to initialize data static structures");

	if (serializer_g_init(NULL, NULL))
		fatal("Unable to initialize serializers");

	if (!(conmgr = init_con_mgr((run_mode.listen ? thread_count : 1),
				    callbacks)))
		fatal("Unable to initialize connection manager");

	if (init_operations())
		fatal("Unable to initialize operations structures");

	auth_rack = plugrack_create("rest_auth");
	plugrack_read_dir(auth_rack, slurm_conf.plugindir);

	if (rest_auth && !xstrcasecmp(rest_auth, "list")) {
		info("Possible REST authentication plugins:");
		plugrack_foreach(auth_rack, _plugrack_foreach_list, NULL);
		exit(0);
	} else if (rest_auth) {
		/* User provide which plugins they want */
		char *type, *last = NULL;

		type = strtok_r(rest_auth, ",", &last);
		while (type) {
			xstrtrim(type);

			/* Permit both prefix and no-prefix for plugin names. */
			if (xstrncmp(type, "rest_auth/", 10) == 0)
				type += 10;
			type = xstrdup_printf("rest_auth/%s", type);
			xstrtrim(type);

			_auth_plugrack_foreach(type, NULL,
					       PLUGIN_INVALID_HANDLE, NULL);

			xfree(type);
			type = strtok_r(NULL, ",", &last);
		}

		xfree(rest_auth);
	} else /* Add all possible */
		plugrack_foreach(auth_rack, _auth_plugrack_foreach, NULL);

	if (!auth_plugin_count)
		fatal("No authentication plugins to load.");

	for (size_t i = 0; i < auth_plugin_count; i++) {
		if ((auth_plugin_handles[i] == PLUGIN_INVALID_HANDLE) &&
		    (auth_plugin_handles[i] =
		     plugrack_use_by_type(auth_rack, auth_plugin_types[i])) ==
		    PLUGIN_INVALID_HANDLE)
				fatal("Unable to find plugin: %s",
				      auth_plugin_types[i]);
	}

	if (init_rest_auth(become_user, auth_plugin_handles, auth_plugin_count))
		fatal("Unable to initialize rest authentication");

	if (oas_specs && !xstrcasecmp(oas_specs, "list")) {
		info("Possible OpenAPI plugins:");
		exit(init_openapi(&openapi_state, oas_specs,
				  _plugrack_foreach_list));
	} else if (init_openapi(&openapi_state, oas_specs, NULL))
		fatal("Unable to initialize OpenAPI structures");

	xfree(oas_specs);
	bind_operation_handler("/openapi.yaml", _op_handler_openapi, 0);
	bind_operation_handler("/openapi.json", _op_handler_openapi, 0);
	bind_operation_handler("/openapi", _op_handler_openapi, 0);
	bind_operation_handler("/openapi/v3", _op_handler_openapi, 0);

	/* Sanity check modes */
	if (run_mode.stdin_socket) {
		char *in = fd_resolve_path(STDIN_FILENO);
		char *out = fd_resolve_path(STDOUT_FILENO);

		if (in && out && xstrcmp(in, out))
			fatal("STDIN and STDOUT must be same socket");

		xfree(in);
		xfree(out);
	}

	if (run_mode.stdin_tty)
		debug("Interactive mode activated (TTY detected on STDIN)");

	if (!run_mode.listen) {
		if ((rc = con_mgr_process_fd(conmgr, CON_TYPE_RAW, STDIN_FILENO,
					     STDOUT_FILENO, conmgr_events, NULL,
					     0, operations_router)))
			fatal("%s: unable to process stdin: %s",
			      __func__, slurm_strerror(rc));

		/* fail on first error if this is piped process */
		conmgr->exit_on_error = true;
	} else if (run_mode.listen) {
		mode_t mask = umask(0);

		if (con_mgr_create_sockets(conmgr, CON_TYPE_RAW, socket_listen,
					   conmgr_events, operations_router))
			fatal("Unable to create sockets");

		umask(mask);

		FREE_NULL_LIST(socket_listen);
		debug("%s: server listen mode activated", __func__);
	}

	rc = con_mgr_run(conmgr);

	/*
	 * Capture if there were issues during parsing in inet mode.
	 * Inet mode expects connection errors to propagate upwards as
	 * connection errors so they can be logged appropriately.
	 */
	if (conmgr->exit_on_error)
		parse_rc = conmgr->error;

	unbind_operation_handler(_op_handler_openapi);

	/* cleanup everything */
	destroy_rest_auth();
	destroy_operations();
	destroy_openapi(openapi_state);
	openapi_state = NULL;
	free_con_mgr(conmgr);

	serializer_g_fini();
	data_fini();
	for (size_t i = 0; i < auth_plugin_count; i++) {
		plugrack_release_by_type(auth_rack, auth_plugin_types[i]);
		xfree(auth_plugin_types[i]);
	}
	xfree(auth_plugin_types);
	if ((rc = plugrack_destroy(auth_rack)))
		fatal_abort("unable to clean up plugrack: %s",
			    slurm_strerror(rc));
	auth_rack = NULL;

	xfree(auth_plugin_handles);
	slurm_fini();
	log_fini();

	/* send parsing RC if there were no higher level errors */
	return (rc ? rc : parse_rc);
}
