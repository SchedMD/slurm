/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test the HTTP router.
 *****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <stdint.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_router.h"

/*
 * Sentinels returned by the stub handlers. A single int return value then
 * identifies which handler the router dispatched to. RC_NOT_FOUND and every
 * route sentinel must differ from each other and from SLURM_SUCCESS (0).
 */
enum {
	RC_NOT_FOUND = 1000,
	RC_GENERIC,
	RC_SUBMIT,
	RC_JOB_ID,
	RC_QOS_LIST,
	RC_QOS_ONE,
};

/*
 * Per-test observation state. Process-global, but each START_TEST runs in its
 * own forked child (libcheck default CK_FORK), so it is effectively fresh per
 * test; setup() also resets it.
 */
static struct {
	int call_count;
	int last_rc; /* sentinel of the last handler invoked */
	void *last_path_arg; /* path_arg the last route handler received */
	void *last_arg; /* arg passed to http_router_on_request() */
} obs;

/* Count of on_fini() callbacks observed during http_router_fini(). */
static int fini_calls;

static void reset_obs(void)
{
	obs = (typeof(obs)) { 0 };
}

/* ---------------------------------------------------------------------- */
/* Stub callbacks                                                         */
/* ---------------------------------------------------------------------- */

static int stub_not_found(http_con_t *hcon, const char *name,
			  const http_con_request_t *request, void *arg,
			  void *path_arg)
{
	obs.call_count++;
	obs.last_arg = arg;
	obs.last_path_arg = path_arg; /* router always passes NULL here */
	obs.last_rc = RC_NOT_FOUND;
	return RC_NOT_FOUND;
}

/* Generate a recorder that returns a fixed sentinel. hcon is never touched. */
#define DEFINE_STUB(fn, sentinel) \
	static int fn(http_con_t *hcon, const char *name, \
		      const http_con_request_t *request, void *arg, \
		      void *path_arg) \
	{ \
		obs.call_count++; \
		obs.last_arg = arg; \
		obs.last_path_arg = path_arg; \
		obs.last_rc = (sentinel); \
		return (sentinel); \
	}

DEFINE_STUB(stub_generic, RC_GENERIC)
DEFINE_STUB(stub_submit, RC_SUBMIT)
DEFINE_STUB(stub_job_id, RC_JOB_ID)
DEFINE_STUB(stub_qos_list, RC_QOS_LIST)
DEFINE_STUB(stub_qos_one, RC_QOS_ONE)

static void stub_fini(http_router_on_request_event_t on_request, void *path_arg)
{
	fini_calls++;
}

/* ---------------------------------------------------------------------- */
/* Helpers                                                                */
/* ---------------------------------------------------------------------- */

#define REQUEST_ARG ((void *) 0xA11C)

/* Issue a request for (method, path) and return the router's rc. */
static int do_request(http_request_method_t method, const char *path)
{
	http_con_request_t request = {
		.method = method,
		.url = {
			.path = (char *) path,
		},
	};

	return http_router_on_request(NULL, "test", &request, REQUEST_ARG);
}

/* ---------------------------------------------------------------------- */
/* Fixtures                                                               */
/* ---------------------------------------------------------------------- */

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("http-router-test", log_opts, 0, NULL);

	reset_obs();
	fini_calls = 0;

	/*
	 * Asserts on_not_found is set and that init is only called once per
	 * process. Safe here because each test runs in its own forked child.
	 */
	http_router_init(stub_not_found);
}

static void teardown(void)
{
	http_router_fini();
	log_fini();
}

/* ---------------------------------------------------------------------- */
/* (a) Endpoint sweep                                                     */
/* ---------------------------------------------------------------------- */

typedef struct {
	http_request_method_t method;
	const char *bind_path;
	const char *request_path;
} endpoint_case_t;

#define DP "v0.0.43"
#define SLURM_P "/slurm/" DP
#define DBD_P "/slurmdb/" DP
#define UTIL_P "/util/" DP

/* Examples based on slurmrestd but they don't need to be sync'ed */
static const endpoint_case_t endpoints[] = {
	{ HTTP_REQUEST_GET, SLURM_P "/shares", SLURM_P "/shares" },
	{ HTTP_REQUEST_GET, SLURM_P "/reconfigure/", SLURM_P "/reconfigure" },
	{ HTTP_REQUEST_GET, SLURM_P "/diag/", SLURM_P "/diag" },
	{ HTTP_REQUEST_GET, SLURM_P "/ping/", SLURM_P "/ping" },
	{ HTTP_REQUEST_GET, SLURM_P "/licenses/", SLURM_P "/licenses" },
	{ HTTP_REQUEST_GET, SLURM_P "/jobs/", SLURM_P "/jobs" },
	{ HTTP_REQUEST_GET, SLURM_P "/jobs/state/", SLURM_P "/jobs/state" },
	{ HTTP_REQUEST_GET, SLURM_P "/job/{job_id}", SLURM_P "/job/123" },
	{ HTTP_REQUEST_GET, SLURM_P "/job/{job_id}/requeue",
	  SLURM_P "/job/123/requeue" },
	{ HTTP_REQUEST_GET, SLURM_P "/nodes/", SLURM_P "/nodes" },
	{ HTTP_REQUEST_GET, SLURM_P "/node/{node_name}", SLURM_P "/node/n01" },
	{ HTTP_REQUEST_GET, SLURM_P "/partitions/", SLURM_P "/partitions" },
	{ HTTP_REQUEST_GET, SLURM_P "/partition/{partition_name}",
	  SLURM_P "/partition/debug" },
	{ HTTP_REQUEST_GET, SLURM_P "/reservations/", SLURM_P "/reservations" },
	{ HTTP_REQUEST_GET, SLURM_P "/reservation/{reservation_name}",
	  SLURM_P "/reservation/resv1" },
	{ HTTP_REQUEST_GET, SLURM_P "/resources/{job_id}",
	  SLURM_P "/resources/123" },
	{ HTTP_REQUEST_GET, SLURM_P "/conf", SLURM_P "/conf" },

	{ HTTP_REQUEST_POST, SLURM_P "/job/submit", SLURM_P "/job/submit" },
	{ HTTP_REQUEST_POST, SLURM_P "/job/allocate", SLURM_P "/job/allocate" },
	{ HTTP_REQUEST_POST, SLURM_P "/job/{job_id}", SLURM_P "/job/123" },
	{ HTTP_REQUEST_POST, SLURM_P "/jobs/requeue", SLURM_P "/jobs/requeue" },
	{ HTTP_REQUEST_POST, SLURM_P "/nodes/", SLURM_P "/nodes" },
	{ HTTP_REQUEST_POST, SLURM_P "/node/{node_name}", SLURM_P "/node/n01" },
	{ HTTP_REQUEST_POST, SLURM_P "/partitions/", SLURM_P "/partitions" },
	{ HTTP_REQUEST_POST, SLURM_P "/reservations/",
	  SLURM_P "/reservations" },
	{ HTTP_REQUEST_POST, SLURM_P "/reservation", SLURM_P "/reservation" },
	{ HTTP_REQUEST_POST, SLURM_P "/new/node/", SLURM_P "/new/node" },

	{ HTTP_REQUEST_DELETE, SLURM_P "/jobs/", SLURM_P "/jobs" },
	{ HTTP_REQUEST_DELETE, SLURM_P "/job/{job_id}", SLURM_P "/job/123" },
	{ HTTP_REQUEST_DELETE, SLURM_P "/node/{node_name}",
	  SLURM_P "/node/n01" },
	{ HTTP_REQUEST_DELETE, SLURM_P "/partition/{partition_name}",
	  SLURM_P "/partition/debug" },
	{ HTTP_REQUEST_DELETE, SLURM_P "/reservation/{reservation_name}",
	  SLURM_P "/reservation/resv1" },

	{ HTTP_REQUEST_GET, DBD_P "/job/{job_id}", DBD_P "/job/123" },
	{ HTTP_REQUEST_GET, DBD_P "/config", DBD_P "/config" },
	{ HTTP_REQUEST_GET, DBD_P "/conf", DBD_P "/conf" },
	{ HTTP_REQUEST_GET, DBD_P "/tres/", DBD_P "/tres" },
	{ HTTP_REQUEST_GET, DBD_P "/qos/{qos}", DBD_P "/qos/normal" },
	{ HTTP_REQUEST_GET, DBD_P "/qos/", DBD_P "/qos" },
	{ HTTP_REQUEST_GET, DBD_P "/associations/", DBD_P "/associations" },
	{ HTTP_REQUEST_GET, DBD_P "/association/", DBD_P "/association" },
	{ HTTP_REQUEST_GET, DBD_P "/instances/", DBD_P "/instances" },
	{ HTTP_REQUEST_GET, DBD_P "/instance/", DBD_P "/instance" },
	{ HTTP_REQUEST_GET, DBD_P "/user/{name}", DBD_P "/user/root" },
	{ HTTP_REQUEST_GET, DBD_P "/users/", DBD_P "/users" },
	{ HTTP_REQUEST_GET, DBD_P "/cluster/{cluster_name}",
	  DBD_P "/cluster/cluster1" },
	{ HTTP_REQUEST_GET, DBD_P "/clusters/", DBD_P "/clusters" },
	{ HTTP_REQUEST_GET, DBD_P "/wckey/{id}", DBD_P "/wckey/5" },
	{ HTTP_REQUEST_GET, DBD_P "/wckeys/", DBD_P "/wckeys" },
	{ HTTP_REQUEST_GET, DBD_P "/account/{account_name}",
	  DBD_P "/account/acct1" },
	{ HTTP_REQUEST_GET, DBD_P "/accounts/", DBD_P "/accounts" },
	{ HTTP_REQUEST_GET, DBD_P "/jobs/", DBD_P "/jobs" },
	{ HTTP_REQUEST_GET, DBD_P "/diag/", DBD_P "/diag" },
	{ HTTP_REQUEST_GET, DBD_P "/ping/", DBD_P "/ping" },

	{ HTTP_REQUEST_POST, DBD_P "/job/{job_id}", DBD_P "/job/123" },
	{ HTTP_REQUEST_POST, DBD_P "/config", DBD_P "/config" },
	{ HTTP_REQUEST_POST, DBD_P "/tres/", DBD_P "/tres" },
	{ HTTP_REQUEST_POST, DBD_P "/qos/", DBD_P "/qos" },
	{ HTTP_REQUEST_POST, DBD_P "/associations/", DBD_P "/associations" },
	{ HTTP_REQUEST_POST, DBD_P "/users_association/",
	  DBD_P "/users_association" },
	{ HTTP_REQUEST_POST, DBD_P "/users/", DBD_P "/users" },
	{ HTTP_REQUEST_POST, DBD_P "/clusters/", DBD_P "/clusters" },
	{ HTTP_REQUEST_POST, DBD_P "/wckeys/", DBD_P "/wckeys" },
	{ HTTP_REQUEST_POST, DBD_P "/accounts_association/",
	  DBD_P "/accounts_association" },
	{ HTTP_REQUEST_POST, DBD_P "/accounts/", DBD_P "/accounts" },
	{ HTTP_REQUEST_POST, DBD_P "/jobs/", DBD_P "/jobs" },

	{ HTTP_REQUEST_DELETE, DBD_P "/qos/{qos}", DBD_P "/qos/normal" },
	{ HTTP_REQUEST_DELETE, DBD_P "/associations/", DBD_P "/associations" },
	{ HTTP_REQUEST_DELETE, DBD_P "/association/", DBD_P "/association" },
	{ HTTP_REQUEST_DELETE, DBD_P "/user/{name}", DBD_P "/user/root" },
	{ HTTP_REQUEST_DELETE, DBD_P "/cluster/{cluster_name}",
	  DBD_P "/cluster/cluster1" },
	{ HTTP_REQUEST_DELETE, DBD_P "/wckey/{id}", DBD_P "/wckey/5" },
	{ HTTP_REQUEST_DELETE, DBD_P "/account/{account_name}",
	  DBD_P "/account/acct1" },

	{ HTTP_REQUEST_POST, UTIL_P "/hostnames", UTIL_P "/hostnames" },
	{ HTTP_REQUEST_POST, UTIL_P "/hostlist", UTIL_P "/hostlist" },
};

#define ENDPOINT_COUNT ARRAY_SIZE(endpoints)

/* path_arg for endpoint i is (i + 1) so it is never NULL. */
static void *endpoint_path_arg(int i)
{
	return (void *) (intptr_t) (i + 1);
}

static void bind_all_endpoints(void)
{
	for (int i = 0; i < ENDPOINT_COUNT; i++)
		http_router_bind(endpoints[i].method, endpoints[i].bind_path,
				 stub_generic, stub_fini, endpoint_path_arg(i));
}

START_TEST(test_endpoint_sweep)
{
	int rc;

	/* Bind the full realistic tree so coexisting routes are exercised. */
	bind_all_endpoints();

	rc = do_request(endpoints[_i].method, endpoints[_i].request_path);

	ck_assert_int_eq(rc, RC_GENERIC);
	ck_assert_int_eq(obs.last_rc, RC_GENERIC);
	ck_assert_ptr_eq(obs.last_path_arg, endpoint_path_arg(_i));
}

END_TEST

/* ---------------------------------------------------------------------- */
/* (b) RFC 3986 / HTTP method matrix                                      */
/* ---------------------------------------------------------------------- */

START_TEST(test_methods_isolation)
{
	static const http_request_method_t others[] = {
		HTTP_REQUEST_POST,    HTTP_REQUEST_PUT,     HTTP_REQUEST_DELETE,
		HTTP_REQUEST_OPTIONS, HTTP_REQUEST_HEAD,    HTTP_REQUEST_PATCH,
		HTTP_REQUEST_TRACE,   HTTP_REQUEST_INVALID,
	};

	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL, NULL);

	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs"), RC_GENERIC);

	for (int i = 0; i < ARRAY_SIZE(others); i++)
		ck_assert_int_eq(do_request(others[i], "/jobs"), RC_NOT_FOUND);
}

END_TEST

START_TEST(test_slash_normalization)
{
	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL, NULL);

	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs/"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "//jobs"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "//jobs//"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs////"), RC_GENERIC);
}

END_TEST

START_TEST(test_percent_decode_valid)
{
	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL, NULL);
	/* A literal segment that contains a decoded '/' from %2F. */
	http_router_bind(HTTP_REQUEST_GET, "/a%2Fb", stub_generic, NULL, NULL);

	/* %6A and %6a both decode to 'j'. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/%6Aobs"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/%6aobs"), RC_GENERIC);

	/* %2F is decoded into the segment, not treated as a separator. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a%2Fb"), RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b"), RC_NOT_FOUND);
}

END_TEST

START_TEST(test_percent_decode_invalid)
{
	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL, NULL);

	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs%00x"),
			 RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs%FFx"),
			 RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs%ZZx"),
			 RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs%4"), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs%"), RC_NOT_FOUND);
}

END_TEST

START_TEST(test_illegal_chars)
{
	static const char *paths[] = {
		"/jobs ", "/jo,bs", "/jobs?x", "/jo:bs",
		"/jo@bs", "/jo!bs", "/jo#bs",
	};

	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL, NULL);

	for (int i = 0; i < ARRAY_SIZE(paths); i++)
		ck_assert_int_eq(do_request(HTTP_REQUEST_GET, paths[i]),
				 RC_NOT_FOUND);
}

END_TEST

START_TEST(test_literal_brace_request)
{
	http_router_bind(HTTP_REQUEST_GET, "/job/{job_id}", stub_job_id, NULL,
			 NULL);

	/* A literal '{' in a request is invalid -> not found. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/job/{x}"),
			 RC_NOT_FOUND);
	/* Positive control: the template still resolves a concrete value. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/job/123"), RC_JOB_ID);
}

END_TEST

START_TEST(test_dot_segment_not_normalized)
{
	http_router_bind(HTTP_REQUEST_GET, "/a/b", stub_generic, NULL, NULL);

	/* The router does not collapse "." or ".." dot-segments. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/./b"), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b/.."), RC_NOT_FOUND);
	/* Positive control. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b"), RC_GENERIC);
}

END_TEST

START_TEST(test_case_sensitivity)
{
	http_router_bind(HTTP_REQUEST_GET, "/Jobs", stub_generic, NULL, NULL);

	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs"), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/Jobs"), RC_GENERIC);
}

END_TEST

START_TEST(test_exact_before_template)
{
	/* Bind the template first to prove literals still win. */
	http_router_bind(HTTP_REQUEST_POST, SLURM_P "/job/{job_id}",
			 stub_job_id, NULL, NULL);
	http_router_bind(HTTP_REQUEST_POST, SLURM_P "/job/submit", stub_submit,
			 NULL, NULL);
	http_router_bind(HTTP_REQUEST_POST, SLURM_P "/job/allocate",
			 stub_generic, NULL, NULL);

	ck_assert_int_eq(do_request(HTTP_REQUEST_POST, SLURM_P "/job/submit"),
			 RC_SUBMIT);
	ck_assert_int_eq(do_request(HTTP_REQUEST_POST, SLURM_P "/job/allocate"),
			 RC_GENERIC);
	ck_assert_int_eq(do_request(HTTP_REQUEST_POST, SLURM_P "/job/999"),
			 RC_JOB_ID);
}

END_TEST

START_TEST(test_handler_and_template_coexist)
{
	http_router_bind(HTTP_REQUEST_GET, DBD_P "/qos/", stub_qos_list, NULL,
			 NULL);
	http_router_bind(HTTP_REQUEST_GET, DBD_P "/qos/{qos}", stub_qos_one,
			 NULL, NULL);

	/* Handler on the node itself. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, DBD_P "/qos"),
			 RC_QOS_LIST);
	/* Templated child of the same node. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, DBD_P "/qos/normal"),
			 RC_QOS_ONE);
}

END_TEST

START_TEST(test_prefix_and_overlong)
{
	http_router_bind(HTTP_REQUEST_GET, "/a/b/c", stub_generic, NULL, NULL);

	/* Intermediate nodes have no handler. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a"), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b"), RC_NOT_FOUND);
	/* Overlong path past the bound leaf. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b/c/d"),
			 RC_NOT_FOUND);
	/* A long request segment must not match and must not crash. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET,
				    "/a/b/c/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
			 RC_NOT_FOUND);
	/* Exact match. */
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/a/b/c"), RC_GENERIC);
}

END_TEST

START_TEST(test_root_and_empty)
{
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/"), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, ""), RC_NOT_FOUND);
	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, NULL), RC_NOT_FOUND);
}

END_TEST

START_TEST(test_path_arg_passthrough)
{
	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, NULL,
			 (void *) 0xBEEF);

	ck_assert_int_eq(do_request(HTTP_REQUEST_GET, "/jobs"), RC_GENERIC);
	ck_assert_ptr_eq(obs.last_path_arg, (void *) 0xBEEF);
	ck_assert_ptr_eq(obs.last_arg, REQUEST_ARG);
}

END_TEST

START_TEST(test_fini_calls_on_fini)
{
	http_router_bind(HTTP_REQUEST_GET, "/jobs", stub_generic, stub_fini,
			 NULL);
	http_router_bind(HTTP_REQUEST_GET, "/nodes", stub_generic, stub_fini,
			 NULL);

	ck_assert_int_eq(fini_calls, 0);

	http_router_fini();

	ck_assert_int_eq(fini_calls, 2);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;

	TCase *tcase = tcase_create("http_router");
	tcase_add_unchecked_fixture(tcase, setup, teardown);

	/* (b) RFC 3986 / HTTP method matrix */
	tcase_add_test(tcase, test_methods_isolation);
	tcase_add_test(tcase, test_slash_normalization);
	tcase_add_test(tcase, test_percent_decode_valid);
	tcase_add_test(tcase, test_percent_decode_invalid);
	tcase_add_test(tcase, test_illegal_chars);
	tcase_add_test(tcase, test_literal_brace_request);
	tcase_add_test(tcase, test_dot_segment_not_normalized);
	tcase_add_test(tcase, test_case_sensitivity);
	tcase_add_test(tcase, test_exact_before_template);
	tcase_add_test(tcase, test_handler_and_template_coexist);
	tcase_add_test(tcase, test_prefix_and_overlong);
	tcase_add_test(tcase, test_root_and_empty);
	tcase_add_test(tcase, test_path_arg_passthrough);
	tcase_add_test(tcase, test_fini_calls_on_fini);

	/* (a) endpoint sweep over every slurmrestd (method, path) pair */
	tcase_add_loop_test(tcase, test_endpoint_sweep, 0, ENDPOINT_COUNT);

	Suite *suite = suite_create("http_router");
	suite_add_tcase(suite, tcase);

	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
