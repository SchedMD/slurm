/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Add tests validating use of uid/username cache and negative cache
 *****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xstring.h"

#include "src/common/uid.h"

static bool slow_lookup_enabled = false;
static long lookup_count = 0;

#define SLOW_LOOKUP_USEC 50000 /* 50 msec */
#define UID_BASE 100000
#define NUSERS 130000

static int fill_synthetic_pwd_entry(struct passwd *pwd, char *buf,
				    size_t buflen, int i,
				    struct passwd **result)
{
	/*
	 * buf layout:
	 * "ckuserNNNNNNN\0" "x\0" "\0" "\0" "\0" "\0"
	 */
	char *ptr = NULL;
	int n = snprintf(buf, buflen, "ckuser%07d", i);

	if ((n < 0) || ((size_t) n + 6 >= buflen)) {
		*result = NULL;
		return ERANGE;
	}

	ptr = buf;
	pwd->pw_name = ptr;
	ptr += n + 1;

	pwd->pw_passwd = ptr;
	*ptr++ = 'x';
	*ptr++ = '\0';

	pwd->pw_gecos = ptr;
	pwd->pw_dir = ptr;
	pwd->pw_shell = ptr;
	*ptr = '\0';

	pwd->pw_uid = UID_BASE + i;
	pwd->pw_gid = UID_BASE + i;

	*result = pwd;
	return 0;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen,
	       struct passwd **result)
{
	lookup_count += 1;
	if (slow_lookup_enabled)
		usleep(SLOW_LOOKUP_USEC);

	if (!xstrncmp(name, "ckuser", 6)) {
		int i = atoi(name + 6);
		if ((i >= 0) && (i < NUSERS))
			return fill_synthetic_pwd_entry(pwd, buf, buflen, i,
							result);
	}
	*result = NULL;
	return 0; /* not found */
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
	       struct passwd **result)
{
	lookup_count += 1;
	if (slow_lookup_enabled)
		usleep(SLOW_LOOKUP_USEC);

	if ((uid >= UID_BASE) && (uid < UID_BASE + NUSERS))
		return fill_synthetic_pwd_entry(pwd, buf, buflen,
						uid - UID_BASE, result);

	*result = NULL;
	return 0;
}

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_init("uid-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

START_TEST(test_uid_lookup_time)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	DEF_TIMERS;
	int rc;
	long timer_us = 0;
	uid_t u;
	char username[16];
	long sum = 0;

	uid_from_string_cache_enable();

	slow_lookup_enabled = false;
	lookup_count = 0;
	uid_cache_clear();
	START_TIMER;
	ck_assert(uid_from_string_cached("ckuser0000042", &u) == SLURM_SUCCESS);
	END_TIMER;
	ck_assert(u == UID_BASE + 42);
	timer_us = TIMER_DURATION_USEC();
	printf("uncached fast lookup: %s (%ld us)\n", TIMER_STR(), timer_us);

	uid_cache_clear();
	slow_lookup_enabled = true;
	START_TIMER;
	ck_assert(uid_from_string_cached("ckuser0000042", &u) == SLURM_SUCCESS);
	END_TIMER;
	ck_assert(u == UID_BASE + 42);
	timer_us = TIMER_DURATION_USEC();
	printf("uncached slow lookup: %s (%ld us)\n", TIMER_STR(), timer_us);
	ck_assert(timer_us > 50000);

	START_TIMER;
	ck_assert(uid_from_string_cached("ckuser0000042", &u) == SLURM_SUCCESS);
	END_TIMER;
	ck_assert(u == UID_BASE + 42);
	timer_us = TIMER_DURATION_USEC();
	printf("cached fast lookup: %s (%ld us)\n", TIMER_STR(), timer_us);
	ck_assert(timer_us < 50);

	uid_cache_clear();
	slow_lookup_enabled = false;
	lookup_count = 0;
	/* load cache up to 130000 users */
	sum = 0;
	for (int i = 0; i < NUSERS; i++) {
		START_TIMER;
		snprintf(username, sizeof(username), "ckuser%07d", i);
		rc = uid_from_string_cached(username, &u);
		END_TIMER;
		sum += TIMER_DURATION_USEC();
		ck_assert(rc == SLURM_SUCCESS);
		ck_assert(u == i + UID_BASE);
	}
	ck_assert(lookup_count == NUSERS);
	printf("Cache Load Time, Average: %0.2f us\n",
	       (double) sum / (double) NUSERS);

	sum = 0;
	for (int i = 0; i < NUSERS; i++) {
		START_TIMER;
		snprintf(username, sizeof(username), "ckuser%07d", i);
		rc = uid_from_string_cached(username, &u);
		END_TIMER;
		ck_assert(rc == SLURM_SUCCESS);
		ck_assert(u == i + UID_BASE);
		sum += TIMER_DURATION_USEC();
	}
	ck_assert(lookup_count == NUSERS);
	printf("Cache Read Time, Average: %0.2f us\n",
	       (double) sum / (double) NUSERS);

	/* measure time for uid-as-string search */
	START_TIMER;
	snprintf(username, sizeof(username), "%d", UID_BASE + 15151);
	rc = uid_from_string_cached(username, &u);
	END_TIMER;
	ck_assert(rc == SLURM_SUCCESS);
	ck_assert(u == UID_BASE + 15151);
	printf("Cache search time: %0.2f us\n", (double) TIMER_DURATION_USEC());

	uid_from_string_cache_disable();
#else
	ck_abort_msg(
		"uid_from_string caching does not exist until Slurm 26.05.0");
#endif
}

END_TEST

START_TEST(test_negative_cache)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	uid_t uid = 50;

	slow_lookup_enabled = true;
	lookup_count = 0;

	for (int i = 0; i < 10; i++) {
		int rc = uid_from_string_cached("fake_username", &uid);
		ck_assert(rc == SLURM_ERROR);
		ck_assert(uid == 50); /* make sure uid not updated */
	}
	ck_assert(lookup_count == 10);

	lookup_count = 0;
	uid_from_string_cache_enable();
	/* warm cache */
	uid_from_string_cached("fake_username", &uid);

	/* test cache */
	for (int i = 0; i < 10000; i++) {
		int rc = uid_from_string_cached("fake_username", &uid);
		ck_assert(rc == SLURM_ERROR);
		ck_assert(uid == 50); /* make sure uid not updated */
	}
	ck_assert(lookup_count == 1);
	uid_from_string_cache_disable();

	slow_lookup_enabled = false;
#else
	ck_abort_msg("uid negative cache does not exist until Slurm 26.05.0");
#endif
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("uid");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_negative_cache);
	tcase_add_test(tcase, test_uid_lookup_time);

	tcase_set_timeout(tcase, 20);

	Suite *suite = suite_create("uid");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
