/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test conmgr pollctl interrupt lost-wakeup (Ticket 25227).
 *****************************************************************************/

#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/conmgr/polling.h"

#include <check.h>

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("polling-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

static int _noop_event(int fd, pollctl_events_t events, void *arg)
{
	return SLURM_SUCCESS;
}

static int _fail_event(int fd, pollctl_events_t events, void *arg)
{
	ck_abort_msg("stale poll() event re-delivered for fd:%d events:0x%x",
		     fd, events);
	return SLURM_ERROR;
}

/*
 * Core of the lost-wakeup regression (Ticket 25227): request an interrupt
 * while not actively polling, then poll. The caller selects the polling
 * backend (default or POLL_MODE_POLL) before calling this.
 *
 * Before the fix, pollctl_interrupt() silently dropped the request when
 * pctl.polling was false. There is a window where a poll is queued but the
 * worker has not yet entered epoll_wait()/poll() and set pctl.polling; an
 * interrupt requested in that window (notably on shutdown) was lost and the
 * following pollctl_poll() then blocked forever in epoll_wait(timeout=-1) with
 * no further wakeup. With the fix the poll returns immediately; without it the
 * poll blocks and the test is killed at the tcase timeout (reported as a
 * failure).
 */
static void _run_interrupt_before_poll(void)
{
	int p[2] = { -1, -1 };

	ck_assert_int_eq(pipe(p), 0);

	pollctl_init(8);

	/*
	 * Link a real fd so the poll backend does not short-circuit on having
	 * only the internal interrupt pipe registered (fd_count <= 1).
	 */
	ck_assert_int_eq(pollctl_link_fd(p[0], PCTL_TYPE_READ_ONLY, "test",
					 __func__),
			 SLURM_SUCCESS);

	/* Request an interrupt while not polling - the lost-wakeup window. */
	pollctl_interrupt(__func__);

	/* Must return promptly instead of blocking in epoll_wait()/poll(). */
	ck_assert_int_eq(pollctl_poll(__func__), SLURM_SUCCESS);

	/* poll() leaves polling set until for_each_event() processes results. */
	ck_assert_int_eq(pollctl_for_each_event(_noop_event, NULL, "noop",
						__func__),
			 SLURM_SUCCESS);

	ck_assert_int_eq(pollctl_unlink_fd(p[0], "test", __func__),
			 SLURM_SUCCESS);
	pollctl_fini();

	close(p[0]);
	close(p[1]);
}

/* Exercise the fix on the default backend (epoll where available). */
START_TEST(test_interrupt_before_poll)
{
	_run_interrupt_before_poll();
}

END_TEST

/*
 * Exercise the same fix on the poll(2) backend. A standalone libcheck binary
 * never reads slurm.conf, so SlurmctldParameters=conmgr_use_poll does not
 * reach it; select the poll backend explicitly. pollctl_set_mode() may only be
 * called once per process, which is fine here because libcheck runs each test
 * case in its own forked child (CK_FORK, the default).
 */
START_TEST(test_interrupt_before_poll_poll)
{
	pollctl_set_mode(POLL_MODE_POLL);
	_run_interrupt_before_poll();
}

END_TEST

/*
 * Regression for the stale-revents half of Ticket 25227 on the poll(2)
 * backend: a revents value left in pctl.events[] by one poll() cycle must not
 * be re-delivered by a later cycle that skips poll().
 *
 * A real poll() cycle marks a slot readable (POLLIN), and for_each_event()
 * leaves that revents in the persistent array. The fd is then drained so it is
 * no longer readable, and an interrupt makes the next _poll() skip the blocking
 * poll(). With the fix that skip clears every slot's revents, so nothing is
 * delivered; without it for_each_event() re-scans and delivers the stale event
 * for a descriptor that is no longer ready (here _fail_event() aborts).
 */
START_TEST(test_stale_revents_poll)
{
	int p[2] = { -1, -1 };
	char buf[8];

	ck_assert_int_eq(pipe(p), 0);

	pollctl_set_mode(POLL_MODE_POLL);
	pollctl_init(8);

	ck_assert_int_eq(pollctl_link_fd(p[0], PCTL_TYPE_READ_ONLY, "test",
					 __func__),
			 SLURM_SUCCESS);

	/*
	 * Linking an fd requests an interrupt on the poll backend, so drain it
	 * with one poll cycle before running a real poll(). After this the
	 * pending interrupt is cleared and pctl.polling is false again.
	 */
	ck_assert_int_eq(pollctl_poll(__func__), SLURM_SUCCESS);
	ck_assert_int_eq(pollctl_for_each_event(_noop_event, NULL, "noop",
						__func__),
			 SLURM_SUCCESS);

	/*
	 * Cycle 1: make p[0] readable and run a real poll() so its events[] slot
	 * gets a non-zero revents that for_each_event() leaves in place.
	 */
	ck_assert_int_eq(write(p[1], "x", 1), 1);
	ck_assert_int_eq(pollctl_poll(__func__), SLURM_SUCCESS);
	ck_assert_int_eq(pollctl_for_each_event(_noop_event, NULL, "noop",
						__func__),
			 SLURM_SUCCESS);

	/*
	 * Drain p[0]: the fd is no longer readable, but the stale POLLIN revents
	 * is still sitting in its pctl.events[] slot.
	 */
	ck_assert_int_eq(read(p[0], buf, sizeof(buf)), 1);

	/*
	 * Cycle 2: request an interrupt so _poll() skips the blocking poll(),
	 * then assert the stale revents from cycle 1 is not re-delivered.
	 */
	pollctl_interrupt(__func__);
	ck_assert_int_eq(pollctl_poll(__func__), SLURM_SUCCESS);
	ck_assert_int_eq(pollctl_for_each_event(_fail_event, NULL, "fail",
						__func__),
			 SLURM_SUCCESS);

	ck_assert_int_eq(pollctl_unlink_fd(p[0], "test", __func__),
			 SLURM_SUCCESS);
	pollctl_fini();

	close(p[0]);
	close(p[1]);
}

END_TEST

int main(void)
{
	int number_failed;
	Suite *s;
	TCase *tc;
	SRunner *sr;

	s = suite_create("polling");
	tc = tcase_create("pollctl_interrupt");
	tcase_add_unchecked_fixture(tc, setup, teardown);

	/*
	 * Bound the runtime so the pre-fix hang (a blocking poll) is reported
	 * as a failure instead of stalling the suite indefinitely.
	 */
	tcase_set_timeout(tc, 10);
	tcase_add_test(tc, test_interrupt_before_poll);
	tcase_add_test(tc, test_interrupt_before_poll_poll);
	tcase_add_test(tc, test_stale_revents_poll);
	suite_add_tcase(s, tc);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
