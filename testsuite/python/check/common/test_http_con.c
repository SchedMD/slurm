/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  test_http_con.c - Test HTTP response header emission
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <sys/socket.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/conmgr/conmgr.h"
#include "src/interfaces/http_parser.h"
#include "src/interfaces/url_parser.h"

/* Issue 50528: conmgr redesigned */
#include "src/common/workerpool.h"

/*
 * Registered as a checked fixture (see main()), so this runs inside the
 * forked test child and a failed ck_assert is attributed to the test and
 * logged in the XML results like any other test failure. In an unchecked
 * fixture the same failure would kill the runner before any test is logged,
 * leaving an empty suite that the pytest harness cannot parse.
 */
static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("http_con-test", log_opts, 0, NULL);

	/*
	 * http_con parses requests with the http_parser and url_parser
	 * plugins, so the plugin dir has to be set. slurm_init() is not used
	 * for this: it needs a config source and fatal()s without one, which
	 * would make this test depend on a configured cluster.
	 */
	if (!slurm_conf.plugindir)
		slurm_conf.plugindir = xstrdup(default_plugin_path);

	/*
	 * The http_parser plugins are optional at build time: when neither
	 * library is found auxdir/x_ac_http_parser.m4 only warns and no
	 * plugin is built, so this can fail on an otherwise good tree.
	 */
	ck_assert_msg((http_parser_g_init() == SLURM_SUCCESS),
		      "no http_parser plugin was built; configure with "
		      "--with-llhttp-parser or --with-libhttp-parser");
	/* url_parser/internal is built unconditionally */
	ck_assert_msg(
		(url_parser_g_init() == SLURM_SUCCESS),
		"cannot create url_parser context; url_parser/internal is "
		"always built, so the plugin dir is likely wrong: %s",
		slurm_conf.plugindir);
}

static void teardown(void)
{
	/* Both g_fini() are no-ops when the matching g_init() failed */
	url_parser_g_fini();
	http_parser_g_fini();
	log_fini();
}

/*
 * A response header is "<name>: <value>\r\n". http_con.c formats that into a
 * MAX_HEADER_BYTES (80) stack buffer and, before ticket 25659, returned ENOMEM
 * for anything that did not fit. Sweep value lengths across that boundary and
 * well past it so no single size is load bearing.
 *
 * "Location: " is 10 bytes and CRLF is 2, so a value of 68 bytes is the first
 * that used to fail. 90 also crosses _vprintf()'s 100 byte initial allocation,
 * which is the heap path the fix depends on.
 */
static const size_t header_value_lengths[] = {
	1, 16, 66, 67, 68, 69, 79, 80, 81, 90, 128, 1024, 8192,
};
#define VALUE_LENGTH_COUNT \
	(sizeof(header_value_lengths) / sizeof(header_value_lengths[0]))

static const char HEADER_NAME[] = "Location";

/* State shared with the conmgr callbacks, reset per iteration */
static char *header_value = NULL;
static int send_rc = SLURM_ERROR;
static bool on_request_called = false;

static int _on_request(http_con_t *hcon, const char *name,
		       const http_con_request_t *request, void *arg)
{
	list_t *headers = list_create((ListDelF) free_http_header);

	on_request_called = true;

	list_append(headers, http_header_new(HEADER_NAME, header_value));

	send_rc = http_con_send_response(hcon,
					 HTTP_STATUS_CODE_REDIRECT_SEE_OTHER,
					 headers, true, NULL, NULL);

	FREE_NULL_LIST(headers);

	/* One request per connection is enough; let conmgr_run() return */
	conmgr_request_shutdown();

	return SLURM_SUCCESS;
}

/*
 * Mirror how slurmctld hands a connection to http_con: the switch does it from
 * on_data(), not on_connection(), because on_connection()'s return value
 * replaces the connection arg that http_con_assign_server() just installed.
 */
static int _on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	static const http_con_server_events_t http_events = {
		.on_request = _on_request,
	};
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(conmgr_args.con);
	int rc = http_con_assign_server(ref, NULL, &http_events, NULL);

	CONMGR_CON_UNLINK(ref);

	return rc;
}

/* Read everything the server wrote, until EOF */
static char *_drain(int fd)
{
	char *out = NULL;
	char buf[4096];
	ssize_t n;

	while ((n = read(fd, buf, sizeof(buf))) > 0)
		xstrncat(out, buf, n);

	return out;
}

START_TEST(test_response_header_length)
{
	static const conmgr_events_t events = {
		.on_data = _on_data,
	};
	static const char REQUEST[] =
		"GET /metrics HTTP/1.1\r\nHost: localhost\r\n\r\n";
	int sv[2] = { -1, -1 };
	char *response = NULL, *expected = NULL;
	const size_t value_length = header_value_lengths[_i];
	/*
	 * Every step's result is captured into a local so all the cleanup can
	 * run before the first ck_assert: under CK_FORK=no (how a developer
	 * runs this under gdb) a failed assert longjmps past anything below
	 * it, and would otherwise leak the buffers and the sv[1] fd on each of
	 * the remaining iterations.
	 */
	int socketpair_rc, conmgr_rc = SLURM_ERROR, run_rc = SLURM_ERROR;
	ssize_t wrote = -1;
	bool got_response = false, status_line_ok = false, header_ok = false;
	char response_head[201] = "";

	send_rc = SLURM_ERROR;
	on_request_called = false;

	header_value = xmalloc(value_length + 1);
	memset(header_value, 'h', value_length);

	socketpair_rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	if (!socketpair_rc) {
		/*
		 * Queue the request before conmgr runs so the parser has
		 * something to hand to _on_request() as soon as the connection
		 * is polled. A short write would leave conmgr_run() waiting for
		 * the rest of the request, so only continue on a full write.
		 */
		wrote = write(sv[1], REQUEST, (sizeof(REQUEST) - 1));
		if (wrote == (ssize_t) (sizeof(REQUEST) - 1)) {
			/*
			 * Issue 50528: conmgr redesigned.
			 *
			 * Note: this lifecycle only works once per process:
			 * conmgr_fini() never resets mgr.initialized, so a
			 * later conmgr_init() is a no-op against torn-down
			 * state and conmgr_process_fd() crashes. Under
			 * CK_FORK=no only the first iteration survives; the
			 * default fork mode runs each iteration in a fresh
			 * child and is unaffected.
			 */
			workerpool_init(0, 0, NULL);
			conmgr_init(0);

			conmgr_rc = conmgr_process_fd(CON_TYPE_RAW,
						      &conmgr_timeouts_disabled,
						      sv[0], sv[0], &events, 0,
						      NULL, 0, NULL, NULL);
			if (conmgr_rc == SLURM_SUCCESS)
				run_rc = conmgr_run(true);

			conmgr_fini();
			workerpool_fini();
		}
	}

	if (run_rc == SLURM_SUCCESS) {
		response = _drain(sv[1]);
		got_response = (response != NULL);

		/*
		 * The status line must survive the switch to the heap buffer
		 * too, so a response that carries the header but is otherwise
		 * malformed fails.
		 */
		status_line_ok =
			got_response &&
			(xstrstr(response, "HTTP/1.1 303 ") == response);

		/* The whole header must reach the wire, terminated, not truncated */
		xstrfmtcat(expected, "%s: %s\r\n", HEADER_NAME, header_value);
		header_ok =
			got_response && (xstrstr(response, expected) != NULL);

		/* Keep a prefix for the failure messages past xfree(response) */
		if (got_response)
			snprintf(response_head, sizeof(response_head), "%s",
				 response);
	}

	/*
	 * sv[0] is only closed here if conmgr did not take it over:
	 * conmgr_process_fd() assumes ownership of the fd on success and
	 * conmgr_fini() above already closed it.
	 */
	if ((sv[0] >= 0) && (conmgr_rc != SLURM_SUCCESS))
		close(sv[0]);
	if (sv[1] >= 0)
		close(sv[1]);
	xfree(header_value);
	xfree(expected);
	xfree(response);

	ck_assert_int_eq(socketpair_rc, 0);
	ck_assert_int_eq(wrote, (ssize_t) (sizeof(REQUEST) - 1));
	ck_assert_int_eq(conmgr_rc, SLURM_SUCCESS);
	ck_assert_int_eq(run_rc, SLURM_SUCCESS);

	ck_assert_msg(on_request_called,
		      "the request was never parsed for a %zu byte value",
		      value_length);

	/*
	 * Ticket 25659: this returned ENOMEM for any header that did not fit
	 * the stack buffer, and the response was emitted without it.
	 */
	ck_assert_msg(
		(send_rc == SLURM_SUCCESS),
		"http_con_send_response() returned %d (%s) for a %zu byte header value",
		send_rc, slurm_strerror(send_rc), value_length);

	ck_assert_msg(got_response,
		      "no response was written for a %zu byte header value",
		      value_length);

	ck_assert_msg(
		status_line_ok,
		"the response does not start with a 303 status line for a %zu byte header value, response starts with:\n%s",
		value_length, response_head);

	ck_assert_msg(
		header_ok,
		"the complete %zu byte \"%s\" header is not in the response, response starts with:\n%s",
		value_length, HEADER_NAME, response_head);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	TCase *tcase = tcase_create("http_con");
	Suite *suite = suite_create("http_con");
	SRunner *sr = NULL;

	/*
	 * Checked fixture, not unchecked: setup() asserts that the optional
	 * http_parser plugin loaded, and a fixture failure must be attributed
	 * to the test so it lands in the XML results. An unchecked fixture
	 * runs in the runner process, where the same failure kills the runner
	 * before any test is logged and leaves an empty suite that the pytest
	 * harness cannot parse.
	 */
	tcase_add_checked_fixture(tcase, setup, teardown);
	/*
	 * Each iteration runs a full conmgr lifecycle, which does not fit
	 * libcheck's 4 second default under coverage or a loaded runner.
	 */
	tcase_set_timeout(tcase, 30);
	tcase_add_loop_test(tcase, test_response_header_length, 0,
			    VALUE_LENGTH_COUNT);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
