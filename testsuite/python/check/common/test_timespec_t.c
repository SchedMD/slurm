/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>
#include <time.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/xassert.h"

#ifndef NDEBUG
#undef ck_assert
#define ck_assert(x) xassert(x)
#endif

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("timespec_t-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

START_TEST(test_now)
{
	timespec_t ts_now = timespec_now();
	time_t t_now = time(NULL);

	ck_assert(ts_now.tv_sec > 0);
	ck_assert(t_now > 0);
	ck_assert(t_now - ts_now.tv_sec < 2);

	ts_now = timespec_normalize(ts_now);

	ck_assert(ts_now.tv_sec > 0);
	ck_assert(t_now > 0);
	ck_assert(t_now - ts_now.tv_sec < 2);
}

END_TEST

START_TEST(test_normalize)
{
	timespec_t x = { 10, 10 };
	timespec_t y = { 5, 5 };

	x = timespec_normalize(x);
	ck_assert(x.tv_nsec == 10);
	ck_assert(x.tv_sec == 10);

	y = timespec_normalize(y);
	ck_assert(y.tv_nsec == 5);
	ck_assert(y.tv_sec == 5);

	x = (timespec_t) { 10, (10 * NSEC_IN_SEC) };
	y = (timespec_t) { 5, (5 * NSEC_IN_SEC) };

	x = timespec_normalize(x);
	ck_assert(x.tv_nsec == 20);
	ck_assert(x.tv_sec == 0);

	y = timespec_normalize(y);
	ck_assert(y.tv_nsec == 10);
	ck_assert(y.tv_sec == 0);
}

END_TEST

START_TEST(test_compare)
{
	timespec_t x = { 10, 10 };
	timespec_t y = { 5, 5 };

	ck_assert(timespec_is_after(x, y));
	ck_assert(!timespec_is_after(y, x));
}

END_TEST

START_TEST(test_add)
{
	timespec_t x = { 10, 1 };
	timespec_t y = { 5, 2 };
	timespec_t t1 = { 0 }, t2 = { 0 }, t3 = { 0 };

	t1 = timespec_add(x, y);
	ck_assert(t1.tv_sec == 15);
	ck_assert(t1.tv_nsec == 3);

	t2 = timespec_add(y, x);
	ck_assert(t2.tv_sec == 15);
	ck_assert(t2.tv_nsec == 3);

	t3 = timespec_add(t1, t2);
	ck_assert(t3.tv_sec == 30);
	ck_assert(t3.tv_nsec == 6);
}

END_TEST

START_TEST(test_rem)
{
	timespec_t x = { 10, 4 };
	timespec_t y = { 5, 2 };
	timespec_t t1 = { 0 }, t2 = { 0 }, t3 = { 0 };

	t1 = timespec_rem(x, y);
	ck_assert(t1.tv_sec == 5);
	ck_assert(t1.tv_nsec == 2);

	/* Negative math is rejected currently */
	t2 = timespec_rem(y, x);
	//ck_assert(t2.tv_sec == -5);
	//ck_assert(t2.tv_nsec == -2);
	ck_assert(t2.tv_sec == 0);
	ck_assert(t2.tv_nsec == 0);

	t3 = timespec_rem(t1, t2);
	//ck_assert(t3.tv_sec == 0);
	//ck_assert(t3.tv_nsec == 0);
	ck_assert(t3.tv_sec == 5);
	ck_assert(t3.tv_nsec == 2);
}

END_TEST

START_TEST(test_diff)
{
	timespec_t x = { 10, 4 };
	timespec_t y = { 5, 2 };
	timespec_diff_ns_t diff;

	ck_assert(timespec_diff(x, y) == 5);
	ck_assert(timespec_diff(y, x) == -5);

	diff = timespec_diff_ns(x, y);
	ck_assert(diff.after);
	ck_assert(diff.diff.tv_sec == 5);
	ck_assert(diff.diff.tv_nsec == 2);

	diff = timespec_diff_ns(y, x);
	ck_assert(!diff.after);
	ck_assert(diff.diff.tv_sec == 5);
	ck_assert(diff.diff.tv_nsec == 2);
}

END_TEST

START_TEST(test_to_secs)
{
	timespec_t x = { 10, 4 };
	double secs = (double) 10 + ((double) 4 / NSEC_IN_SEC);

	ck_assert(timespec_to_secs(x) == secs);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("timespec_t");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_now);
	tcase_add_test(tcase, test_compare);
	tcase_add_test(tcase, test_normalize);
	tcase_add_test(tcase, test_add);
	tcase_add_test(tcase, test_rem);
	tcase_add_test(tcase, test_diff);
	tcase_add_test(tcase, test_to_secs);

	Suite *suite = suite_create("timespec_t");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
