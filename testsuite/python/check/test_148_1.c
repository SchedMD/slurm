/*****************************************************************************\
 *  test_148_1.c - Test conmgr
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <fcntl.h>

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
	ck_assert(mgr.conf_delay_write_complete == 845);
	ck_assert(mgr.quiesce.conf_timeout.tv_sec == 3838);

	ck_assert(!conmgr_set_params(",,CONMGR_READ_TIMEOUT=9858,,,,,"));
	ck_assert(mgr.conf_read_timeout.tv_sec == 9858);

	ck_assert(!conmgr_set_params(
		"CONMGR_WRITE_TIMEOUT=3483,CONMGR_CONNECT_TIMEOUT=984"));
	ck_assert(mgr.conf_write_timeout.tv_sec == 3483);
	ck_assert(mgr.conf_connect_timeout.tv_sec == 984);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("conmgr");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_params);

	Suite *suite = suite_create("conmgr");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
