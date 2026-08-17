/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/env.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"

/*
 * Mirrors MAX_ENV_STRLEN, which is private to src/common/env.c. setenvf()
 * rejects a variable once strlen(name) + strlen(value) + 2 reaches this
 * value, where the 2 accounts for the '=' and the terminating NUL.
 */
#define TEST_MAX_ENV_STRLEN (32 * 4096)

#define TEST_NAME "SETENVF_CHECK"

/* Largest value setenvf() accepts for TEST_NAME */
#define TEST_MAX_VALUE_LEN (TEST_MAX_ENV_STRLEN - sizeof(TEST_NAME) - 2)

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	log_init("env-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	unsetenv(TEST_NAME);
	log_fini();
}

/* Return a NUL terminated string of "len" 'a' characters */
static char *_make_value(size_t len)
{
	char *value = xmalloc(len + 1);

	memset(value, 'a', len);

	return value;
}

START_TEST(test_invalid_name)
{
	char **env = env_array_create();

	ck_assert_int_eq(setenvf(NULL, NULL, "%s", "value"), EINVAL);
	ck_assert_int_eq(setenvf(NULL, "", "%s", "value"), EINVAL);
	ck_assert_int_eq(setenvf(&env, NULL, "%s", "value"), EINVAL);
	ck_assert_int_eq(setenvf(&env, "", "%s", "value"), EINVAL);

	env_array_free(env);
}

END_TEST

START_TEST(test_set_process_env)
{
	ck_assert_int_eq(setenvf(NULL, TEST_NAME, "%s", "value"), 0);
	ck_assert_str_eq(getenv(TEST_NAME), "value");

	/* an existing variable is overwritten */
	ck_assert_int_eq(setenvf(NULL, TEST_NAME, "%d", 42), 0);
	ck_assert_str_eq(getenv(TEST_NAME), "42");

	unsetenv(TEST_NAME);
}

END_TEST

START_TEST(test_set_env_array)
{
	char **env = env_array_create();

	ck_assert_int_eq(setenvf(&env, TEST_NAME, "%s", "value"), 0);
	ck_assert_str_eq(getenvp(env, TEST_NAME), "value");

	/* an existing variable is overwritten */
	ck_assert_int_eq(setenvf(&env, TEST_NAME, "%d", 42), 0);
	ck_assert_str_eq(getenvp(env, TEST_NAME), "42");

	/* the process environment must not have been touched */
	ck_assert_ptr_null(getenv(TEST_NAME));

	env_array_free(env);
}

END_TEST

START_TEST(test_too_long)
{
	char *value = _make_value(TEST_MAX_VALUE_LEN + 1);

	/*
	 * Repeat to make the total obvious under a manual valgrind run: each
	 * rejected variable used to leak a 256KiB buffer. This harness does
	 * not run under valgrind, so this loop does not itself regression
	 * test the leak fix -- it only pins the return code and that the
	 * variable is left unset. The value alone is enough to trip the
	 * check, so no pathological name is needed.
	 */
	for (int i = 0; i < 10; i++)
		ck_assert_int_eq(setenvf(NULL, TEST_NAME, "%s", value), ENOMEM);

	ck_assert_ptr_null(getenv(TEST_NAME));

	xfree(value);
}

END_TEST

START_TEST(test_length_boundary)
{
	char **env = env_array_create();
	char *value = _make_value(TEST_MAX_VALUE_LEN + 1);

	/* strlen(name) + strlen(value) + 2 == MAX_ENV_STRLEN - 1 */
	value[TEST_MAX_VALUE_LEN] = '\0';
	ck_assert_int_eq(setenvf(&env, TEST_NAME, "%s", value), 0);
	ck_assert_str_eq(getenvp(env, TEST_NAME), value);

	/* one more byte makes it exactly MAX_ENV_STRLEN */
	value[TEST_MAX_VALUE_LEN] = 'a';
	ck_assert_int_eq(setenvf(&env, TEST_NAME, "%s", value), ENOMEM);

	/* the rejected value must not have replaced the accepted one */
	ck_assert_int_eq(strlen(getenvp(env, TEST_NAME)), TEST_MAX_VALUE_LEN);

	xfree(value);
	env_array_free(env);
}

END_TEST

START_TEST(test_setenvfs_too_long)
{
	char *value = _make_value(TEST_MAX_ENV_STRLEN);

	ck_assert_int_eq(setenvfs("%s=%s", TEST_NAME, value), ENOMEM);
	ck_assert_ptr_null(getenv(TEST_NAME));

	xfree(value);
}

END_TEST

/*
 * setenv() only fails when it cannot allocate, which cannot be provoked from
 * a test. Interpose on it instead: run-tests builds these with
 * -Wl,--export-dynamic, so this definition takes precedence over libc's for
 * the call setenvfs() makes. It passes through to the real setenv() unless a
 * test asks for a failure, so the other tests in this file are unaffected.
 */
static bool fail_setenv = false;
static int setenv_calls = 0;

int setenv(const char *name, const char *value, int overwrite)
{
	static int (*real_setenv)(const char *, const char *, int) = NULL;

	if (!real_setenv)
		real_setenv = dlsym(RTLD_NEXT, "setenv");

	setenv_calls++;

	if (fail_setenv) {
		errno = ENOMEM;
		return -1;
	}

	return real_setenv(name, value, overwrite);
}

START_TEST(test_setenvfs_success)
{
	setenv_calls = 0;

	ck_assert_int_eq(setenvfs("%s=%s", TEST_NAME, "value"), 0);
	ck_assert_str_eq(getenv(TEST_NAME), "value");

	/* the interposed setenv() must actually be the one that ran */
	ck_assert_int_eq(setenv_calls, 1);
}

END_TEST

START_TEST(test_setenvfs_setenv_failure)
{
	fail_setenv = true;
	setenv_calls = 0;

	/* the errno setenv() set, not its -1 */
	ck_assert_int_eq(setenvfs("%s=%s", TEST_NAME, "value"), ENOMEM);
	ck_assert_int_eq(setenv_calls, 1);
	ck_assert_ptr_null(getenv(TEST_NAME));

	fail_setenv = false;
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;

	TCase *tcase = tcase_create("env");
	tcase_add_checked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_invalid_name);
	tcase_add_test(tcase, test_set_process_env);
	tcase_add_test(tcase, test_set_env_array);
	tcase_add_test(tcase, test_too_long);
	tcase_add_test(tcase, test_length_boundary);
	tcase_add_test(tcase, test_setenvfs_too_long);
	tcase_add_test(tcase, test_setenvfs_success);
	tcase_add_test(tcase, test_setenvfs_setenv_failure);

	Suite *suite = suite_create("env");
	suite_add_tcase(suite, tcase);

	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
