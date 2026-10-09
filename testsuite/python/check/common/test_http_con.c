/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  test_http_con.c - Test HTTP response header emission and closing idle
 *	connections for a conmgr quiesce
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
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

static const char *parser_types[] = {
	"http_parser/llhttp_parser",
	"http_parser/libhttp_parser",
};
/* Parser plugins that load here */
static const char *parsers[ARRAY_SIZE(parser_types)];
static int parser_count = 0;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int requests = 0;
/* Path of the request whose handler fails without a response */
static const char *fail_path = NULL;
static int closes = 0;
static slurm_err_t close_status = SLURM_SUCCESS;
static int con_arg = 0;
static int con_sv[2] = { -1, -1 };

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

static int _on_count_request(http_con_t *hcon, const char *name,
			     const http_con_request_t *request, void *arg)
{
	slurm_mutex_lock(&lock);
	requests++;
	slurm_mutex_unlock(&lock);

	if (fail_path && !xstrcmp(request->url.path, fail_path))
		return EPERM;

	return http_con_send_response(hcon, HTTP_STATUS_CODE_SUCCESS_NO_CONTENT,
				      NULL, false, NULL, NULL);
}

static void _on_close(const char *name, slurm_err_t status_code, void *arg)
{
	slurm_mutex_lock(&lock);
	closes++;
	close_status = status_code;
	slurm_mutex_unlock(&lock);
}

static void *_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	return arg;
}

/* Switch to http_con on first data as slurmctld and slurmd do */
static int _on_switch_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	static const http_con_server_events_t events = {
		.on_request = _on_count_request,
		.on_close = _on_close,
	};

	return http_con_assign_server(conmgr_args.ref, NULL, &events, NULL);
}

static void _init(int parser)
{
	static const conmgr_events_t events = {
		.on_connection = _on_connection,
		.on_data = _on_switch_data,
	};

	xfree(slurm_conf.http_parser_type);
	slurm_conf.http_parser_type = xstrdup(parsers[parser]);
	ck_assert(!http_parser_g_init());
	ck_assert(!url_parser_g_init());

	/* Longer than the test timeout: only http_con can end it in time */
	ck_assert(!conmgr_set_params("CONMGR_QUIESCE_TIMEOUT=60"));

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, con_sv), 0);

	workerpool_init(0, 0, NULL);
	conmgr_init(0);

	ck_assert_int_eq(conmgr_process_fd(CON_TYPE_RAW,
					   &conmgr_timeouts_disabled, con_sv[0],
					   con_sv[0], &events, 0, NULL, 0, NULL,
					   &con_arg),
			 SLURM_SUCCESS);
	ck_assert_int_eq(conmgr_run(false), SLURM_SUCCESS);
}

static void _fini(void)
{
	conmgr_request_shutdown();
	conmgr_fini();
	workerpool_fini();
	close(con_sv[1]);
}

static void _send(const char *data)
{
	ck_assert_int_eq(write(con_sv[1], data, strlen(data)), strlen(data));
}

/* Wait up to 2 seconds for count requests to be handled */
static bool _wait_for_requests(int count)
{
	for (int i = 0; i < 2000; i++) {
		int value;

		slurm_mutex_lock(&lock);
		value = requests;
		slurm_mutex_unlock(&lock);

		if (value >= count)
			return true;

		usleep(1000);
	}

	return false;
}

/*
 * Read until EOF, or nothing for 2 seconds, and count successful responses, how
 * many responses told the client that the connection is closing, how many
 * responses were not successful and how many of those were 503
 */
static int _read_responses(bool *eof_ptr, int *closing_ptr, int *failed_ptr,
			   int *unavailable_ptr)
{
	char buf[1024];
	char *responses = NULL;
	int count = 0;

	*eof_ptr = false;
	*closing_ptr = 0;
	*failed_ptr = 0;
	*unavailable_ptr = 0;

	while (true) {
		struct pollfd pfd = {
			.fd = con_sv[1],
			.events = POLLIN,
		};
		ssize_t bytes;

		if (poll(&pfd, 1, 2000) <= 0)
			break;

		if ((bytes = read(con_sv[1], buf, (sizeof(buf) - 1))) <= 0) {
			/* A close with unread input resets the connection */
			*eof_ptr = (!bytes || (errno == ECONNRESET));
			break;
		}

		buf[bytes] = '\0';
		xstrcat(responses, buf);
	}

	for (const char *at = responses;
	     at && (at = xstrstr(at, "HTTP/1.1 204")); at++)
		count++;

	for (const char *at = responses;
	     at && (at = xstrcasestr(at, "Connection: Close")); at++)
		(*closing_ptr)++;

	for (const char *at = responses; at && (at = xstrstr(at, "HTTP/1.1 "));
	     at++)
		(*failed_ptr)++;
	*failed_ptr -= count;

	for (const char *at = responses;
	     at && (at = xstrstr(at, "HTTP/1.1 503")); at++)
		(*unavailable_ptr)++;

	xfree(responses);
	return count;
}

/* Send arg once the quiesce is requested and on_quiesce() had time to run */
static void *_send_after_quiesce(void *arg)
{
	const char *data = arg;
	const ssize_t bytes = strlen(data);

	/* Bounded so a quiesce that already ended fails the asserts instead */
	for (int i = 0; (i < 2000) && !conmgr_is_quiesced(); i++)
		usleep(1000);

	usleep(300000);

	/* Closed too early: leave it to the asserts on the responses read */
	if (write(con_sv[1], data, bytes) != bytes)
		error("%s: write() failed: %m", __func__);

	return NULL;
}

START_TEST(test_parser_loaded)
{
	ck_assert_msg(parser_count, "no http_parser plugin loads from %s",
		      slurm_conf.plugindir);
}

END_TEST

START_TEST(test_idle_closed_by_quiesce)
{
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	_init(_i);

	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\n");
	ck_assert(_wait_for_requests(1));

	/* Connection kept alive for the next request must not hold this */
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 1);
	ck_assert(eof);
	/* Answered before the quiesce so the connection was kept alive */
	ck_assert_int_eq(closing, 0);
	ck_assert_int_eq(failed, 0);

	_fini();

	ck_assert_int_eq(requests, 1);
	ck_assert_int_eq(closes, 1);
	ck_assert_int_eq(close_status, SLURM_SUCCESS);
}

END_TEST

START_TEST(test_request_underway_answered)
{
	pthread_t tid;
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	_init(_i);

	/* Second request stops after its request line */
	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\n");

	/*
	 * First request is handled while its data is parsed, so on_quiesce()
	 * can only run once the request line after it has been parsed too.
	 */
	ck_assert(_wait_for_requests(1));

	ck_assert(!pthread_create(&tid, NULL, _send_after_quiesce,
				  "Host: x\r\n\r\n"));
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);
	ck_assert(!pthread_join(tid, NULL));

	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 2);
	ck_assert(eof);
	/* Only the request underway when quiesced is told of the close */
	ck_assert_int_eq(closing, 1);
	ck_assert_int_eq(failed, 0);

	_fini();

	ck_assert_int_eq(requests, 2);
	ck_assert_int_eq(closes, 1);
	ck_assert_int_eq(close_status, SLURM_SUCCESS);
}

END_TEST

START_TEST(test_request_after_quiesce_rejected)
{
	pthread_t tid;
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	_init(_i);

	/* Second request stops after its request line */
	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\n");
	ck_assert(_wait_for_requests(1));

	/* Rest of the request underway arrives with another request after it */
	ck_assert(!pthread_create(
		&tid, NULL, _send_after_quiesce,
		"Host: x\r\n\r\nGET /c HTTP/1.1\r\nHost: x\r\n\r\n"));
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);
	ck_assert(!pthread_join(tid, NULL));

	/* The request after the quiesce is rejected, not handled */
	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 2);
	ck_assert(eof);
	ck_assert_int_eq(failed, 1);
	ck_assert_int_eq(unavailable, 1);
	/* The request underway and the rejection both announce the close */
	ck_assert_int_eq(closing, 2);

	_fini();

	ck_assert_int_eq(requests, 2);
	ck_assert_int_eq(closes, 1);
	/* Turning a request away while closing is not a connection failure */
	ck_assert_int_eq(close_status, SLURM_SUCCESS);
}

END_TEST

START_TEST(test_partial_request_line_answered)
{
	pthread_t tid;
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	_init(_i);

	/* Only the first byte of the second request has arrived */
	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\nG");
	ck_assert(_wait_for_requests(1));

	ck_assert(!pthread_create(&tid, NULL, _send_after_quiesce,
				  "ET /b HTTP/1.1\r\nHost: x\r\n\r\n"));
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);
	ck_assert(!pthread_join(tid, NULL));

	/* A request is underway from its first byte, so it is answered */
	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 2);
	ck_assert(eof);
	ck_assert_int_eq(closing, 1);
	ck_assert_int_eq(failed, 0);

	_fini();

	ck_assert_int_eq(requests, 2);
	ck_assert_int_eq(closes, 1);
	ck_assert_int_eq(close_status, SLURM_SUCCESS);
}

END_TEST

START_TEST(test_empty_line_closed_by_quiesce)
{
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	_init(_i);

	/* RFC9112-2.2: an empty line before a request line is ignored */
	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\n\r\n");
	ck_assert(_wait_for_requests(1));

	/* An empty line is not a request underway, so it must not hold this */
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 1);
	ck_assert(eof);
	ck_assert_int_eq(closing, 0);
	ck_assert_int_eq(failed, 0);

	_fini();

	ck_assert_int_eq(requests, 1);
	ck_assert_int_eq(closes, 1);
	ck_assert_int_eq(close_status, SLURM_SUCCESS);
}

END_TEST

START_TEST(test_failed_request_underway_rejected)
{
	pthread_t tid;
	bool eof = false;
	int closing = 0;
	int failed = 0;
	int unavailable = 0;

	fail_path = "/b";
	_init(_i);

	/* Second request stops after its request line */
	_send("GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\n");
	ck_assert(_wait_for_requests(1));

	/* Request underway fails with another request after it */
	ck_assert(!pthread_create(
		&tid, NULL, _send_after_quiesce,
		"Host: x\r\n\r\nGET /c HTTP/1.1\r\nHost: x\r\n\r\n"));
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);
	ck_assert(!pthread_join(tid, NULL));

	/* Only the failure is answered, once, and it announces the close */
	ck_assert_int_eq(_read_responses(&eof, &closing, &failed, &unavailable),
			 1);
	ck_assert(eof);
	ck_assert_int_eq(failed, 1);
	ck_assert_int_eq(unavailable, 0);
	ck_assert_int_eq(closing, 1);

	_fini();

	/* The request after the failed one is never handled */
	ck_assert_int_eq(requests, 2);
	ck_assert_int_eq(closes, 1);
	ck_assert_int_eq(close_status, EPERM);
}

END_TEST

/* Check if parser plugin loads without keeping it loaded in this process */
static bool _parser_available(const char *type)
{
	int status = 0;
	pid_t pid;

	if (!(pid = fork())) {
		slurm_conf.http_parser_type = xstrdup(type);
		_exit(http_parser_g_init() ? 1 : 0);
	}

	return ((pid > 0) && (waitpid(pid, &status, 0) == pid) &&
		WIFEXITED(status) && !WEXITSTATUS(status));
}

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	TCase *tcase = tcase_create("http_con");
	TCase *quiesce_tcase = tcase_create("http_con_quiesce");
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

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	log_init("http_con-test", log_opts, 0, NULL);

	/* Plugin dir without a config source, as setup() does */
	if (!slurm_conf.plugindir)
		slurm_conf.plugindir = xstrdup(default_plugin_path);

	/* Writing to a connection closed early fails asserts, not the test */
	signal(SIGPIPE, SIG_IGN);

	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	for (int i = 0; i < ARRAY_SIZE(parser_types); i++) {
		if (_parser_available(parser_types[i]))
			parsers[parser_count++] = parser_types[i];
	}

	/*
	 * No shared fixture: these tests load the parser plugin themselves.
	 * Each also runs a full conmgr lifecycle, so it gets the same timeout.
	 */
	tcase_set_timeout(quiesce_tcase, 30);
	tcase_add_test(quiesce_tcase, test_parser_loaded);
	tcase_add_loop_test(quiesce_tcase, test_idle_closed_by_quiesce, 0,
			    parser_count);
	tcase_add_loop_test(quiesce_tcase, test_request_underway_answered, 0,
			    parser_count);
	tcase_add_loop_test(quiesce_tcase, test_request_after_quiesce_rejected,
			    0, parser_count);
	tcase_add_loop_test(quiesce_tcase, test_partial_request_line_answered,
			    0, parser_count);
	tcase_add_loop_test(quiesce_tcase, test_empty_line_closed_by_quiesce, 0,
			    parser_count);
	tcase_add_loop_test(quiesce_tcase,
			    test_failed_request_underway_rejected, 0,
			    parser_count);
	suite_add_tcase(suite, quiesce_tcase);

	/* Create and run the runner */
	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
