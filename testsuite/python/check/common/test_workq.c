/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/common/workq.h"

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)

#define WORK_COUNT 16

/* Number of times any callback has run since the last reset */
static int run_count = 0;

/*
 * Order in which priorities were observed running. Single-threaded
 * workq_run(false) drains in the calling thread so no locking is needed.
 */
static workq_priority_t run_order[WORK_COUNT * 2];
static int run_order_len = 0;

static void _reset(void)
{
	run_count = 0;
	run_order_len = 0;
}

static void _count_work(const bool shutdown, void *arg)
{
	run_count++;
}

static void _record_normal(const bool shutdown, void *arg)
{
	ck_assert(run_order_len < ARRAY_SIZE(run_order));
	run_order[run_order_len++] = WORKQ_PRIORITY_NORMAL;
}

static void _record_idle(const bool shutdown, void *arg)
{
	ck_assert(run_order_len < ARRAY_SIZE(run_order));
	run_order[run_order_len++] = WORKQ_PRIORITY_IDLE;
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
	log_init("workq-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

START_TEST(test_init_fini)
{
	workq_t *workq = workq_init(NULL, NULL);

	ck_assert(workq != NULL);
	ck_assert(workq_bytes() > 0);

	workq_fini(workq);

	/* FREE_NULL_WORKQ() must tolerate a NULL pointer */
	workq = NULL;
	FREE_NULL_WORKQ(workq);
	ck_assert(workq == NULL);
}

END_TEST

START_TEST(test_enqueue_run)
{
	workq_t *workq = workq_init(NULL, NULL);
	workq_allocator_t *alloc = workq_allocator(workq, WORK_COUNT, "test");

	_reset();

	for (int i = 0; i < WORK_COUNT; i++)
		workq_enqueue(workq, alloc, WORKQ_PRIORITY_NORMAL, _count_work,
			      "_count_work", __func__, NULL);

	/* Non-blocking run drains every pending work in this thread */
	workq_run(workq, false);
	ck_assert_int_eq(run_count, WORK_COUNT);

	/* Re-running an empty workq is a no-op, not a hang */
	workq_run(workq, false);
	ck_assert_int_eq(run_count, WORK_COUNT);

	FREE_NULL_WORKQ(workq);
}

END_TEST

START_TEST(test_empty_run)
{
	workq_t *workq = workq_init(NULL, NULL);

	_reset();

	/* Draining a workq that never had work enqueued must not block */
	workq_run(workq, false);
	ck_assert_int_eq(run_count, 0);

	FREE_NULL_WORKQ(workq);
}

END_TEST

START_TEST(test_priority)
{
	workq_t *workq = workq_init(NULL, NULL);
	workq_allocator_t *alloc =
		workq_allocator(workq, WORK_COUNT * 2, "test");
	int normal_seen = 0;
	bool idle_started = false;

	_reset();

	/* Interleave IDLE and NORMAL submissions */
	for (int i = 0; i < WORK_COUNT; i++) {
		workq_enqueue(workq, alloc, WORKQ_PRIORITY_IDLE, _record_idle,
			      "_record_idle", __func__, NULL);
		workq_enqueue(workq, alloc, WORKQ_PRIORITY_NORMAL,
			      _record_normal, "_record_normal", __func__, NULL);
	}

	workq_run(workq, false);

	ck_assert_int_eq(run_order_len, WORK_COUNT * 2);

	/* Every NORMAL work must drain before any IDLE work runs */
	for (int i = 0; i < run_order_len; i++) {
		if (run_order[i] == WORKQ_PRIORITY_NORMAL) {
			ck_assert(!idle_started);
			normal_seen++;
		} else {
			ck_assert(run_order[i] == WORKQ_PRIORITY_IDLE);
			idle_started = true;
		}
	}
	ck_assert_int_eq(normal_seen, WORK_COUNT);

	FREE_NULL_WORKQ(workq);
}

END_TEST

#endif /* SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,11,0) */

extern int main(int argc, char **argv)
{
	int failures;

	TCase *tcase = tcase_create("workq");
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_init_fini);
	tcase_add_test(tcase, test_enqueue_run);
	tcase_add_test(tcase, test_empty_run);
	tcase_add_test(tcase, test_priority);
#endif

	Suite *suite = suite_create("workq");
	suite_add_tcase(suite, tcase);

	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
