/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/common/workerpool.h"

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)

#define THREAD_COUNT 4
#define WORK_COUNT 8

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* Total callbacks that have completed */
static int done = 0;
/* Callbacks currently executing */
static int active = 0;
/* Highest number of callbacks seen running at once */
static int peak = 0;
/* Hold blocking callbacks until the test releases them */
static bool release = false;

static void _reset(void)
{
	slurm_mutex_lock(&lock);
	done = 0;
	active = 0;
	peak = 0;
	release = false;
	slurm_mutex_unlock(&lock);
}

/* Increment the completion counter and signal the test thread */
static void _count_work(const bool shutdown, void *arg)
{
	slurm_mutex_lock(&lock);
	done++;
	slurm_cond_broadcast(&cond);
	slurm_mutex_unlock(&lock);
}

/*
 * Track concurrency: bump active/peak on entry, then block until the test
 * thread sets release so several callbacks overlap in flight.
 */
static void _blocking_work(const bool shutdown, void *arg)
{
	slurm_mutex_lock(&lock);
	active++;
	if (active > peak)
		peak = active;
	slurm_cond_broadcast(&cond);

	while (!release)
		slurm_cond_wait(&cond, &lock);

	active--;
	done++;
	slurm_cond_broadcast(&cond);
	slurm_mutex_unlock(&lock);
}

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
	log_init("workerpool-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

START_TEST(test_init_fini)
{
	workerpool_init(0, THREAD_COUNT, NULL);
	/* Re-init while running is a no-op */
	workerpool_init(0, THREAD_COUNT, NULL);

	workerpool_fini();
	/* Double fini must be safe */
	workerpool_fini();
}

END_TEST

START_TEST(test_enqueue)
{
	_reset();

	workerpool_init(0, THREAD_COUNT, NULL);

	for (int i = 0; i < WORK_COUNT; i++)
		workerpool_enqueue_normal(_count_work, NULL);

	/* Wait for every callback to complete */
	slurm_mutex_lock(&lock);
	while (done < WORK_COUNT)
		slurm_cond_wait(&cond, &lock);
	slurm_mutex_unlock(&lock);

	ck_assert_int_eq(done, WORK_COUNT);

	workerpool_fini();
}

END_TEST

START_TEST(test_concurrency)
{
	_reset();

	/* Configure an explicit, known thread count */
	workerpool_init(0, 0, "workerpool_threads=" XSTRINGIFY(THREAD_COUNT));

	for (int i = 0; i < WORK_COUNT; i++)
		workerpool_enqueue_normal(_blocking_work, NULL);

	/* Wait until at least two callbacks overlap, proving real concurrency */
	slurm_mutex_lock(&lock);
	while (active < 2)
		slurm_cond_wait(&cond, &lock);
	/* Release every blocked callback */
	release = true;
	slurm_cond_broadcast(&cond);
	while (done < WORK_COUNT)
		slurm_cond_wait(&cond, &lock);
	slurm_mutex_unlock(&lock);

	ck_assert_int_eq(done, WORK_COUNT);
	/* Concurrency happened but never exceeded the configured thread count */
	ck_assert_int_ge(peak, 2);
	ck_assert_int_le(peak, THREAD_COUNT);

	workerpool_fini();
}

END_TEST

START_TEST(test_conmgr_threads_alias)
{
	_reset();

	/* conmgr_threads= must be accepted as an alias for workerpool_threads= */
	workerpool_init(0, 0, "conmgr_threads=" XSTRINGIFY(THREAD_COUNT));

	for (int i = 0; i < WORK_COUNT; i++)
		workerpool_enqueue_normal(_count_work, NULL);

	slurm_mutex_lock(&lock);
	while (done < WORK_COUNT)
		slurm_cond_wait(&cond, &lock);
	slurm_mutex_unlock(&lock);

	ck_assert_int_eq(done, WORK_COUNT);

	workerpool_fini();
}

END_TEST

#endif /* SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,11,0) */

extern int main(int argc, char **argv)
{
	int failures;

	TCase *tcase = tcase_create("workerpool");
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	/* Concurrency test blocks worker threads; allow generous wall time */
	tcase_set_timeout(tcase, 60);
	tcase_add_test(tcase, test_init_fini);
	tcase_add_test(tcase, test_enqueue);
	tcase_add_test(tcase, test_concurrency);
	tcase_add_test(tcase, test_conmgr_threads_alias);
#endif

	Suite *suite = suite_create("workerpool");
	suite_add_tcase(suite, tcase);

	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
