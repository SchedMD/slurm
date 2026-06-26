/*****************************************************************************\
 *  test_148_1.c - Test conmgr
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

static slurm_addr_t listen_addr = { 0 };

#if defined(__linux__)
static void setup(void)
{
	int fd;
	char buffer[1024] = { 0 };
	uid_t uid = getuid();
	uid_t gid = getgid();
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("conmgr-test", log_opts, 0, NULL);

	/* Set the listen_addr */
	slurm_set_addr(&listen_addr, 80, "localhost");

	/* Create new network namespace */
	ck_assert(!unshare(CLONE_NEWNET | CLONE_NEWUSER));

	/* Map to root user/group */
	fd = open("/proc/self/uid_map", O_WRONLY);
	ck_assert(snprintf(buffer, ARRAY_SIZE(buffer), "0 %d 1", uid) > 0);
	ck_assert(write(fd, buffer, strlen(buffer)) == strlen(buffer));
	ck_assert(!close(fd));

	fd = open("/proc/self/gid_map", O_WRONLY);
	ck_assert(snprintf(buffer, ARRAY_SIZE(buffer), "0 %d 1", gid) > 0);
	ck_assert(write(fd, buffer, strlen(buffer) == strlen(buffer)));
	ck_assert(!close(fd));

	static const char STR_DENY[] = "deny";
	fd = open("/proc/self/setgroups", O_WRONLY);
	ck_assert(fd >= 0);
	ck_assert(write(fd, STR_DENY, strlen(STR_DENY)) == strlen(STR_DENY));
	ck_assert(!close(fd));

	/* Activate loopback in network namespace */
	ck_assert(!system("ip link set lo up"));
}
#else
static void setup(void)
{
	/* do nothing */
}
#endif

static void teardown(void)
{
	log_fini();
}

START_TEST(test_params)
{
	ck_assert(!conmgr_set_params(
		"CONMGR_THREADS=93,CONMGR_MAX_CONNECTIONS=3484"));
	ck_assert(mgr.workers.conf_threads == 93);
	ck_assert(mgr.conf_max_connections == 3484);

	ck_assert(!conmgr_set_params(
		"CONMGR_WAIT_WRITE_DELAY=845,,,,CONMGR_QUIESCE_TIMEOUT=3838"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.write_complete.tv_sec == 845);
	ck_assert(!mgr.timeouts.write_complete.tv_nsec);
	ck_assert(mgr.timeouts.quiesce.tv_sec == 3838);
	ck_assert(!mgr.timeouts.quiesce.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_delay_write_complete == 845);
	ck_assert(mgr.quiesce.conf_timeout.tv_sec == 3838);
#endif

	ck_assert(!conmgr_set_params(",,CONMGR_READ_TIMEOUT=9858,,,,,"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.read.tv_sec == 9858);
	ck_assert(!mgr.timeouts.read.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_read_timeout.tv_sec == 9858);
#endif

	ck_assert(!conmgr_set_params(
		"CONMGR_WRITE_TIMEOUT=3483,CONMGR_CONNECT_TIMEOUT=984"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.write.tv_sec == 3483);
	ck_assert(!mgr.timeouts.write.tv_nsec);
	ck_assert(mgr.timeouts.connect.tv_sec == 984);
	ck_assert(!mgr.timeouts.connect.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_write_timeout.tv_sec == 3483);
	ck_assert(mgr.conf_connect_timeout.tv_sec == 984);
#endif
}

END_TEST

START_TEST(test_reinit)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(25,11,0)
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
#else
	ck_abort_msg("conmgr_init() has different arguments for versions < 25.11");
#endif
}

END_TEST

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
static int shutdown_arg = 0;
static bool on_finish_called = false;
static slurm_err_t on_finish_status_code = SLURM_SUCCESS;

static void *_shutdown_on_connection(conmgr_callback_args_t conmgr_args,
				     void *arg)
{
	/*
	 * The connection is established and tracked by conmgr now. Request a
	 * shutdown so that close_all_connections() tears it down while it is
	 * still open, which is what should set SLURM_SHUTTING_DOWN.
	 */
	conmgr_request_shutdown();
	return arg;
}

static int _noop_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	return SLURM_SUCCESS;
}

static void _capture_on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	on_finish_called = true;
	on_finish_status_code = conmgr_args.status_code;
}

START_TEST(test_status_code_on_shutdown)
{
	static const conmgr_events_t events = {
		.on_connection = _shutdown_on_connection,
		.on_data = _noop_on_data,
		.on_finish = _capture_on_finish,
	};
	int sv[2] = { -1, -1 };

	on_finish_called = false;
	on_finish_status_code = SLURM_SUCCESS;

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	conmgr_init(0, 0, 0);

	/* Hand one end of the socketpair to conmgr as a peer connection */
	ck_assert_int_eq(conmgr_process_fd(CON_TYPE_RAW,
					   &conmgr_timeouts_disabled, sv[0],
					   sv[0], &events, 0, NULL, 0, NULL,
					   &shutdown_arg),
			 SLURM_SUCCESS);

	/*
	 * Blocks until the shutdown requested from on_connection tears the
	 * connection down. conmgr_fini() guarantees all workers (and thus the
	 * on_finish callback) have completed before it returns.
	 */
	ck_assert_int_eq(conmgr_run(true), SLURM_SUCCESS);
	conmgr_fini();

	ck_assert(on_finish_called);
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Ticket 24952: SLURM_SHUTTING_DOWN added in 26.11+ */
	ck_assert_int_eq(on_finish_status_code, SLURM_SHUTTING_DOWN);
#else
	ck_assert_int_eq(on_finish_status_code, SLURM_SUCCESS);
#endif

	close(sv[1]);
}

END_TEST
#endif

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("conmgr");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_params);
	tcase_add_test(tcase, test_reinit);
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	tcase_add_test(tcase, test_status_code_on_shutdown);
#endif

	Suite *suite = suite_create("conmgr");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
