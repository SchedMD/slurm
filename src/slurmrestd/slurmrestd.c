/*****************************************************************************\
 *  slurmrestd.c - Slurm REST API daemon
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

#define _GNU_SOURCE

#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/select.h"
#include "src/interfaces/serializer.h"
#include "src/interfaces/tls.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/rest_auth.h"

#define OPT_LONG_MAX_CON 0x100
#define OPT_LONG_AUTOCOMP 0x101
#define OPT_LONG_GEN_OAS 0x102

#define SLURM_CONF_DISABLED "/dev/null"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define unshare(_) (false)
#endif

decl_static_data(usage_txt);

uint32_t slurm_daemon = IS_SLURMRESTD;

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
static int debug_increase = 0;
/* detected run mode */
static run_mode_t run_mode = { 0 };
/* Listen string */
static list_t *socket_listen = NULL;
static char *slurm_conf_filename = NULL;
/* Number of requested threads */
static int thread_count = 0;
/* Max number of connections */
static int max_connections = 124;
/* User to become once loaded */
static uid_t uid = 0;
static gid_t gid = 0;
static bool dump_spec_requested = false;

static char *rest_auth = NULL;
static plugin_handle_t *auth_plugin_handles = NULL;
static char **auth_plugin_types = NULL;
static size_t auth_plugin_count = 0;
static plugrack_t *auth_rack = NULL;

static char *oas_specs = NULL;
static char *data_parser_plugins = NULL;
static data_parser_t **parsers = NULL;
static bool unshare_sysv = true;
static bool unshare_files = true;
static bool check_user = true;
static bool become_user = false;
static http_status_code_t *response_status_codes = NULL;

extern parsed_host_port_t *parse_host_port(const char *str);
extern void free_parse_host_port(parsed_host_port_t *parsed);

static void _plugrack_foreach_list(const char *full_type, const char *fq_path,
				   const plugin_handle_t id, void *arg)
{
	fprintf(stdout, "%s\n", full_type);
}

/* SIGPIPE handler - mostly a no-op */
static void _sigpipe_handler(conmgr_callback_args_t conmgr_args, void *arg)
{
	debug5("%s: received SIGPIPE", __func__);
}

static void _set_max_connections(const char *buffer)
{
	max_connections = slurm_atoul(buffer);

	if (max_connections < 1)
		fatal("Invalid max connection count: %s", buffer);

	debug3("%s: setting max_connections=%d", __func__, max_connections);
}

static void _parse_env(void)
{
	char *buffer = NULL;

	if ((buffer = getenv("SLURMRESTD_DEBUG")) != NULL) {
		debug_level = log_string2num(buffer);

		if ((debug_level < 0) || (debug_level == NO_VAL16))
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

	if ((buffer = getenv("SLURMRESTD_MAX_CONNECTIONS")))
		_set_max_connections(buffer);

	if ((buffer = getenv("SLURMRESTD_OPENAPI_PLUGINS")) != NULL) {
		xfree(oas_specs);
		oas_specs = xstrdup(buffer);
	}

	if ((buffer = getenv("SLURMRESTD_DATA_PARSER_PLUGINS")) != NULL) {
		xfree(data_parser_plugins);
		data_parser_plugins = xstrdup(buffer);
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
#ifdef NDEBUG
				fatal_abort("SLURMRESTD_SECURITY=disable_user_check should only be used for development. Disabling the user check to run slurmrestd as root or SlurmUser will allow anyone to run any command on the cluster as root.");
#endif /* NDEBUG */
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

	if ((buffer = getenv("SLURMRESTD_RESPONSE_STATUS_CODES"))) {
		char *token = NULL, *save_ptr = NULL;
		char *toklist = xstrdup(buffer);
		int count = 0;

		token = strtok_r(toklist, ",", &save_ptr);
		while (token) {
			http_status_code_t code = get_http_status_code(token);

			if (code == HTTP_STATUS_NONE)
				fatal("Unable to parse %s as HTTP status code",
				      token);

			xrecalloc(response_status_codes, (count + 2),
				  sizeof(*response_status_codes));

			response_status_codes[count] = code;
			count++;

			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(toklist);

		if (response_status_codes)
			response_status_codes[count] = HTTP_STATUS_NONE;
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

	/*
	 * Set debug level as requested.
	 * debug_level is set to the value of SLURMRESTD_DEBUG.
	 * SLURMRESTD_DEBUG sets the debug level if -v's are not given.
	 * debug_increase is the command line option -v, which applies on top
	 * of the default log level (info).
	 */
	if (debug_increase)
		debug_level = MIN((LOG_LEVEL_INFO + debug_increase),
				  (LOG_LEVEL_END - 1));
	else if (!debug_level)
		debug_level = LOG_LEVEL_INFO;

	logopt.syslog_level = debug_level;

	if (run_mode.stderr_tty) {
		/* Log to stderr if it is a tty */
		logopt = (log_options_t) LOG_OPTS_STDERR_ONLY;
		fac = SYSLOG_FACILITY_USER;
		logopt.stderr_level = debug_level;
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
 * Load only required plugins to dump OpenAPI Specification to stdout
 */
__attribute__((noreturn))
static void dump_spec(int argc, char **argv)
{
	const char *dump_mime_types[] = { MIME_TYPE_JSON, NULL };
	int rc = SLURM_SUCCESS;
	data_t *spec = data_new();
	char *output = NULL;
	size_t output_len = 0;

	_setup_logging(argc, argv);

	(void) is_spec_generation_only(true);

	/* Load slurm.conf if possible and ignore if it fails */
	if (!xstrcmp(slurm_conf_filename, SLURM_CONF_DISABLED)) {
		/* Avoid another part of Slurm from trying to load slurm.conf */
		setenvfs("SLURM_CONF="SLURM_CONF_DISABLED);
	} else if (!xstrcmp(getenv("SLURM_CONF"), SLURM_CONF_DISABLED)) {
		; /* Do not try to load slurm.conf */
	} else if ((rc = slurm_conf_init(slurm_conf_filename))) {
		debug("Unable to load %s: %s",
		      slurm_conf_filename, slurm_strerror(rc));
	}

	serializer_required(MIME_TYPE_JSON);

	if (!(parsers = data_parser_g_new_array(NULL, NULL, NULL, NULL, NULL,
						NULL, NULL, NULL,
						data_parser_plugins, NULL,
						false)))
		fatal("Unable to initialize data_parser plugins");

	if ((rc = init_operations(parsers)))
		fatal("Unable to initialize operations structures: %s",
		      slurm_strerror(rc));

	if (init_openapi(oas_specs, NULL, parsers, response_status_codes))
		fatal("Unable to initialize OpenAPI structures");

	if ((rc = generate_spec(spec, dump_mime_types)))
		fatal("Unable to generate OpenAPI Specification: %s",
		      slurm_strerror(rc));

	if ((rc = serialize_g_data_to_string(&output, &output_len, spec,
					     MIME_TYPE_JSON, SER_FLAGS_PRETTY)))
		fatal("Unable to dump OpenAPI Specification: %s",
		      slurm_strerror(rc));

	fprintf(stdout, "%s", output);
	fflush(stdout);

	_exit(rc);
}

/*
 * _parse_commandline - parse and process any command line arguments
 * IN argc - number of command line arguments
 * IN argv - the command line arguments
 * IN/OUT conf_ptr - pointer to current configuration, update as needed
 */
static void _parse_commandline(int argc, char **argv)
{
	static struct option long_options[] = {
		{ "autocomplete", required_argument, NULL, OPT_LONG_AUTOCOMP },
		{ "help", no_argument, NULL, 'h' },
		{ "max-connections", required_argument, NULL, OPT_LONG_MAX_CON },
		{ "generate-openapi-spec", no_argument, NULL, OPT_LONG_GEN_OAS },
		{ NULL, required_argument, NULL, 'a' },
		{ NULL, required_argument, NULL, 'd' },
		{ NULL, required_argument, NULL, 'f' },
		{ NULL, required_argument, NULL, 'g' },
		{ NULL, no_argument, NULL, 'h' },
		{ NULL, required_argument, NULL, 's' },
		{ NULL, required_argument, NULL, 't' },
		{ NULL, required_argument, NULL, 'u' },
		{ NULL, no_argument, NULL, 'v' },
		{ NULL, no_argument, NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};
	int c = 0, option_index = 0;

	opterr = 0;

	while ((c = getopt_long(argc, argv, "a:d:f:g:hs:t:u:vV", long_options,
				&option_index)) != -1) {
		switch (c) {
		case 'a':
			xfree(rest_auth);
			rest_auth = xstrdup(optarg);
			break;
		case 'd':
			if (!xstrcasecmp(optarg, "list")) {
				fprintf(stderr, "Possible data_parser plugins:\n");
				parsers = data_parser_g_new_array(
					NULL, NULL, NULL, NULL, NULL, NULL,
					NULL, NULL, optarg,
					_plugrack_foreach_list, false);
				exit(SLURM_SUCCESS);
			}

			xfree(data_parser_plugins);
			data_parser_plugins = xstrdup(optarg);
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
			debug_increase++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case OPT_LONG_MAX_CON:
			_set_max_connections(optarg);
			break;
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_GEN_OAS:
			dump_spec_requested = true;
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
 * Check for supplementary group that could result in an unintended privilege
 * escalation
 */
static void _check_gids(void)
{
	gid_t *gids = NULL;
	bool need_drop = false;
	int gid_count = getgroups(0, NULL);

	if (gid_count < 0)
		fatal("%s: getgroups(0, NULL) failed: %m", __func__);

	if (!gid_count)
		return;

	gids = xcalloc(gid_count, sizeof(*gids));

	if ((gid_count = getgroups(gid_count, gids)) < 0)
		fatal("%s: getgroups() failed: %m", __func__);

	for (int i = 0; i < gid_count; i++) {
		/*
		 * Ignore same gid being in supplementary groups
		 * as it won't change permissions
		 */
		if (gids[i] == gid)
			continue;

		need_drop = true;
		debug("%s: Supplementary group %d needs to be dropped",
		      __func__, gids[i]);
	}

	xfree(gids);

	if (!need_drop)
		return;

	debug("%s: Dropping all supplementary groups", __func__);

	if (!setgroups(0, NULL))
		return;

#ifdef __linux__
	if (errno == EPERM)
		fatal("slurmrestd process lacks CAP_SETGID to drop supplementary groups. Supplementary groups must be removed from slurmrestd user (uid=%d,gid=%d) prior to starting slurmrestd.",
		      uid, gid);
#endif /* __linux__ */

	fatal("Unable to drop supplementary groups: %m");
}

/*
 * slurmrestd is merely a translator from REST to Slurm.
 * Try to lock down any extra unneeded permissions.
 */
static void _lock_down(void)
{
	if ((getuid() == SLURM_AUTH_NOBODY) || (getgid() == SLURM_AUTH_NOBODY))
		fatal("slurmrestd must not be run as nobody");

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		fatal("Unable to disable new privileges: %m");
#endif

	if (unshare_sysv && unshare(CLONE_SYSVSEM))
		fatal("Unable to unshare System V namespace: %m");
	if (unshare_files && unshare(CLONE_FILES))
		fatal("Unable to unshare file descriptors: %m");

	if (uid != 0 && (gid == 0))
		gid = gid_from_uid(uid);
	if (gid)
		_check_gids();
	if (gid != 0 && setgid(gid))
		fatal("Unable to setgid: %m");
	if (uid != 0 && setuid(uid))
		fatal("Unable to setuid: %m");

	if (become_user && getuid())
		fatal("slurmrestd must run as root in become_user mode");

	if (become_user && getgid())
		fatal("slurmrestd must run as root in become_user mode");

#ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		error("%s: Unable to set process as dumpable: %m", __func__);
#endif
}

/*
 * Check slurmrestd is not running as SlurmUser unless check_user is false.
 */
static void _check_user(void)
{
	int gid_count = 0;

	if (!check_user)
		return;

	if (getuid() == SLURM_AUTH_NOBODY)
		fatal("slurmrestd should not be run as nobody(%d)",
		      SLURM_AUTH_NOBODY);
	if (getgid() == SLURM_AUTH_NOBODY)
		fatal("slurmrestd should not be run with nobody(%d) group.",
		      SLURM_AUTH_NOBODY);

	if (slurm_conf.slurm_user_id == getuid())
		fatal("slurmrestd should not be run as SlurmUser");
	if (gid_from_uid(slurm_conf.slurm_user_id) == getgid())
		fatal("slurmrestd should not be run with SlurmUser's group.");

	if (!getuid() && !become_user)
		fatal("slurmrestd should not be run as the root user.");
	if (!getgid() && !become_user)
		fatal("slurmrestd should not be run with the root group.");

	if ((gid_count = getgroups(0, NULL)) > 0) {
		gid_t *list = xcalloc(gid_count, sizeof(*list));

		if (getgroups(gid_count, list) != gid_count)
			fatal_abort("Inconsistent getgroups() group counts. This should never happen");

		for (int i = 0; i < gid_count; i++) {
			if (list[i] == slurm_conf.slurm_user_id)
				fatal("slurmrestd should not be run with SlurmUser's group.");

			if (!list[i] && !become_user)
				fatal("slurmrestd should not be run with the root group.");

			if (list[i] == SLURM_AUTH_NOBODY)
				fatal("slurmrestd should not be run with nobody(%d) group.",
				      SLURM_AUTH_NOBODY);
		}

		xfree(list);
	} else if (gid_count < 0) {
		fatal_abort("getgroups()=%d failed[%d]: %m", errno, gid_count);
	}
}

/* simple wrapper to hand over operations router in http context */
static void *_setup_http_context(conmgr_fd_t *con, void *arg)
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

static void _on_signal_interrupt(conmgr_callback_args_t conmgr_args, void *arg)
{
	info("%s: caught SIGINT. Shutting down.", __func__);
	conmgr_request_shutdown();
}


static void _inet_on_finish(conmgr_fd_t *con, void *ctxt)
{
	on_http_connection_finish(con, ctxt);
	conmgr_request_shutdown();
}

int main(int argc, char **argv)
{
	int rc = SLURM_SUCCESS, parse_rc = SLURM_SUCCESS;
	socket_listen = list_create(xfree_ptr);
	static const conmgr_events_t conmgr_events = {
		.on_data = parse_http,
		.on_connection = _setup_http_context,
		.on_finish = on_http_connection_finish,
		.on_fingerprint = on_fingerprint_tls,
	};
	static const conmgr_events_t inet_events = {
		.on_data = parse_http,
		.on_connection = _setup_http_context,
		.on_finish = _inet_on_finish,
	};
	static const conmgr_callbacks_t callbacks = {
		.parse = parse_host_port,
		.free_parse = free_parse_host_port,
	};

	_parse_env();
	_parse_commandline(argc, argv);

	if (dump_spec_requested)
		dump_spec(argc, argv);

	/* attempt to release all unneeded permissions */
	_lock_down();

	_examine_stdin();
	_examine_stderr();
	_examine_stdout();
	_setup_logging(argc, argv);

	run_mode.listen = !list_is_empty(socket_listen);

	slurm_init(slurm_conf_filename);
	_check_user();

	/* Load serializers if they are present */
	serializer_required(MIME_TYPE_JSON);
	if (getenv("SLURMRESTD_YAML"))
		serializer_required(MIME_TYPE_YAML);
	serializer_required(MIME_TYPE_URL_ENCODED);

	/* This checks if slurmrestd is running in inetd mode */
	conmgr_init((run_mode.listen ? thread_count : CONMGR_THREAD_COUNT_MIN),
		    max_connections, callbacks);

	/*
	 * Attempt to load TLS plugin and then attempt to load the certificate
	 * or give user warning TLS will not be supported
	 */
	if (!tls_g_init() && tls_available() &&
	    (rc = tls_g_load_own_cert(NULL, 0, NULL, 0))) {
		debug("Disabling TLS support due to failure loading TLS certificate");

		if ((rc = tls_g_fini()))
			fatal("Unable to unload TLS plugin: %s",
			      slurm_strerror(rc));
	}

	conmgr_add_work_signal(SIGINT, _on_signal_interrupt, NULL);
	conmgr_add_work_signal(SIGPIPE, _sigpipe_handler, NULL);

	auth_rack = plugrack_create("rest_auth");
	plugrack_read_dir(auth_rack, slurm_conf.plugindir);

	if (rest_auth && !xstrcasecmp(rest_auth, "list")) {
		fprintf(stderr, "Possible REST authentication plugins:\n");
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

	if (!(parsers = data_parser_g_new_array(NULL, NULL, NULL, NULL, NULL,
						NULL, NULL, NULL,
						data_parser_plugins, NULL,
						false))) {
		fatal("Unable to initialize data_parser plugins");
	}
	xfree(data_parser_plugins);

	if (init_operations(parsers))
		fatal("Unable to initialize operations structures");

	if (oas_specs && !xstrcasecmp(oas_specs, "list")) {
		fprintf(stderr, "Possible OpenAPI plugins:\n");
		init_openapi(oas_specs, _plugrack_foreach_list, NULL, NULL);
		exit(0);
	} else if (init_openapi(oas_specs, NULL, parsers,
				response_status_codes))
		fatal("Unable to initialize OpenAPI structures");

	xfree(oas_specs);

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
		if ((rc = conmgr_process_fd(CON_TYPE_RAW, STDIN_FILENO,
					    STDOUT_FILENO, &inet_events,
					    CON_FLAG_NONE, NULL, 0,
					    NULL, operations_router)))
			fatal("%s: unable to process stdin: %s",
			      __func__, slurm_strerror(rc));

		/* fail on first error if this is piped process */
		conmgr_set_exit_on_error(true);
	} else if (run_mode.listen) {
		mode_t mask = umask(0);

		if (conmgr_create_listen_sockets(CON_TYPE_RAW, CON_FLAG_NONE,
						 socket_listen, &conmgr_events,
						 operations_router))
			fatal("Unable to create sockets");

		umask(mask);

		FREE_NULL_LIST(socket_listen);
		debug("%s: server listen mode activated", __func__);
	}

	rc = conmgr_run(true);

	/*
	 * Capture if there were issues during parsing in inet mode.
	 * Inet mode expects connection errors to propagate upwards as
	 * connection errors so they can be logged appropriately.
	 */
	if (conmgr_get_exit_on_error())
		parse_rc = conmgr_get_error();

	/* cleanup everything */
	destroy_rest_auth();
	destroy_operations();
	destroy_openapi();
	conmgr_fini();
	FREE_NULL_DATA_PARSER_ARRAY(parsers, false);
	serializer_g_fini();
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
	acct_storage_g_fini();
	slurm_fini();
	hash_g_fini();
	conn_g_fini();
	cred_g_fini();
	auth_g_fini();
	log_fini();

	/* send parsing RC if there were no higher level errors */
	return (rc ? rc : parse_rc);
}
