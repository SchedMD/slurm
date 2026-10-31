/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#include "config.h"

#include <check.h>
#include <errno.h>

/* Include the interface to check lock ownership with controlled callbacks. */
#include "src/interfaces/runtime.c"

#define TEST_IDX 1
#define THREAD_COUNT 2

static pthread_mutex_t callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
static int active = 0;
static bool release_readers = false;
static bool argument_write_locked = false;
static bool reset_write_locked = false;
static bool export_write_locked = false;

static bool _write_locked(void)
{
	int rc = EINVAL;

	rc = pthread_rwlock_tryrdlock(&init_lock);
	if (!rc)
		slurm_rwlock_unlock(&init_lock);

	return ((rc == EBUSY) || (rc == EDEADLK));
}

static bool _on_arg(const runtime_arg_t *arg, const uint32_t het_job_inx,
		    data_t *value, bool set_by_cli, bool set_by_env,
		    bool set_by_data, bool early_pass)
{
	argument_write_locked = _write_locked();
	return (argument_write_locked &&
		!xstrcmp(data_get_string(value), "value"));
}

static const runtime_arg_t test_arg = {
	.argument = "runtime-lock-test",
	.environment.sbatch = "RUNTIME_TEST_ARG",
	.on_arg = _on_arg,
};

static int _apply_job_desc(const uint32_t het_job_inx,
			   runtime_context_t context, job_desc_msg_t *job_desc)
{
	slurm_mutex_lock(&callback_lock);
	active++;
	slurm_cond_broadcast(&callback_cond);
	while (!release_readers)
		slurm_cond_wait(&callback_cond, &callback_lock);
	slurm_mutex_unlock(&callback_lock);

	return SLURM_SUCCESS;
}

static void _reset_args(const uint32_t het_job_inx)
{
	reset_write_locked = _write_locked();
}

static int _apply_env(const uint32_t het_job_inx, const uint32_t het_job_offset,
		      runtime_context_t context, bool child, char ***envp)
{
	export_write_locked = _write_locked();
	return export_write_locked ? SLURM_SUCCESS : SLURM_ERROR;
}

static void _setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	log_init("runtime-test", log_opts, 0, NULL);

	slurm_rwlock_wrlock(&init_lock);
	ops = xcalloc(2, sizeof(*ops));
	g_context = xcalloc(2, sizeof(*g_context));
	loaded_types = xcalloc(2, sizeof(*loaded_types));
	loaded_types[TEST_IDX] = xstrdup("runtime/test");
	/* Reset only checks whether the context is non-NULL. */
	g_context[TEST_IDX] = (plugin_context_t *) ops;
	ops[TEST_IDX].cli_apply_job_desc = _apply_job_desc;
	ops[TEST_IDX].cli_apply_env = _apply_env;
	ops[TEST_IDX].cli_reset_args = _reset_args;
	g_context_cnt = 2;
	plugin_inited = PLUGIN_INITED;
	runtime_register_arg(TEST_IDX, &test_arg);
	slurm_rwlock_unlock(&init_lock);

	active = 0;
	release_readers = false;
	argument_write_locked = false;
	reset_write_locked = false;
	export_write_locked = false;
}

static void _teardown(void)
{
	slurm_rwlock_wrlock(&init_lock);
	memset(args, 0, sizeof(args));
	args_count = 0;
	xfree(loaded_types[TEST_IDX]);
	xfree(loaded_types);
	xfree(g_context);
	xfree(ops);
	g_context_cnt = 0;
	plugin_inited = PLUGIN_NOT_INITED;
	slurm_rwlock_unlock(&init_lock);

	log_fini();
}

static void *_apply_thread(void *arg)
{
	int *rc = arg;
	job_desc_msg_t job_desc = { 0 };

	*rc = runtime_g_cli_apply_job_desc(TEST_IDX, 0, RUNTIME_CTXT_SLURMRESTD,
					   &job_desc);
	return NULL;
}

static void _check_unlocked(void)
{
	int rc = EINVAL;

	rc = pthread_rwlock_trywrlock(&init_lock);
	if (!rc)
		slurm_rwlock_unlock(&init_lock);
	ck_assert_int_eq(rc, 0);
}

START_TEST(test_parallel_apply)
{
	pthread_t threads[THREAD_COUNT];
	int rc[THREAD_COUNT] = { EINVAL, EINVAL };
	int write_rc = EINVAL;

	for (int i = 0; i < THREAD_COUNT; i++)
		ck_assert_int_eq(pthread_create(&threads[i], NULL,
						_apply_thread, &rc[i]),
				 0);

	slurm_mutex_lock(&callback_lock);
	while (active < THREAD_COUNT)
		slurm_cond_wait(&callback_cond, &callback_lock);
	/* Both callbacks overlap, but unloading/reallocation must be excluded. */
	write_rc = pthread_rwlock_trywrlock(&init_lock);
	if (!write_rc)
		slurm_rwlock_unlock(&init_lock);
	/* Release the readers before asserting so a failure cannot hang. */
	release_readers = true;
	slurm_cond_broadcast(&callback_cond);
	slurm_mutex_unlock(&callback_lock);

	for (int i = 0; i < THREAD_COUNT; i++) {
		ck_assert_int_eq(pthread_join(threads[i], NULL), 0);
		ck_assert_int_eq(rc[i], SLURM_SUCCESS);
	}
	ck_assert_int_eq(write_rc, EBUSY);
	_check_unlocked();
}

END_TEST

START_TEST(test_argument_writes)
{
	char *environment[] = { "RUNTIME_TEST_ARG=value", NULL };
	char **envp = environment;

	ck_assert_int_eq(runtime_g_cli_process_arg(TEST_IDX, 0,
						   RUNTIME_OPTVAL_START,
						   "value", false, false),
			 SLURM_SUCCESS);
	ck_assert(argument_write_locked);
	argument_write_locked = false;
	ck_assert(runtime_g_cli_process_env(TEST_IDX, 0, RUNTIME_CTXT_SBATCH,
					    &envp, false));
	ck_assert(argument_write_locked);
	runtime_g_cli_reset_args(0);
	ck_assert(reset_write_locked);
	ck_assert_int_eq(runtime_g_cli_apply_env(TEST_IDX, 0, NO_VAL,
						 RUNTIME_CTXT_SBATCH, false,
						 NULL),
			 SLURM_SUCCESS);
	ck_assert(export_write_locked);
	_check_unlocked();
}

END_TEST

START_TEST(test_error_unlocks)
{
	char *environment[] = { "RUNTIME_TEST_ARG=reject", NULL };
	char **envp = environment;

	ck_assert_int_eq(runtime_g_cli_process_arg(TEST_IDX, 0,
						   RUNTIME_OPTVAL_START,
						   "reject", false, false),
			 SLURM_ERROR);
	_check_unlocked();
	ck_assert(!runtime_g_cli_process_env(TEST_IDX, 0, RUNTIME_CTXT_SBATCH,
					     &envp, false));
	_check_unlocked();
	ck_assert_int_eq(runtime_g_cli_process_arg(TEST_IDX, 0, 0, NULL, false,
						   false),
			 ENOENT);
	_check_unlocked();
}

END_TEST

int main(void)
{
	Suite *suite = suite_create("runtime");
	TCase *tc = tcase_create("locking");
	SRunner *runner = NULL;
	int failed = 0;

	tcase_add_checked_fixture(tc, _setup, _teardown);
	tcase_set_timeout(tc, 10);
	tcase_add_test(tc, test_parallel_apply);
	tcase_add_test(tc, test_argument_writes);
	tcase_add_test(tc, test_error_unlocks);
	suite_add_tcase(suite, tc);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_NORMAL);
	failed = srunner_ntests_failed(runner);
	srunner_free(runner);

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
