/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test the relay_io record ring buffer
 * (src/plugins/relay_io/common/ring_buffer.c) directly.
 *
 * The ring is what keeps producing an event from ever blocking a daemon, so
 * the cases pin the behaviour the relay depends on: that it is a FIFO, that
 * exceeding the byte budget evicts the oldest records rather than the newest,
 * that a record larger than the whole budget is kept instead of evicting
 * itself, that a requeued record goes back to the front, and that the drop
 * counter and byte accounting track what actually happened.
 *****************************************************************************/

#include <check.h>

#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/xmalloc.h"

/*
 * The ring is a convenience library linked into each relay_io plugin rather
 * than part of libslurmfull, so there is no exported symbol to link against.
 * Build it into the test instead.
 */
#include "src/plugins/relay_io/common/ring_buffer.c"

/* ------------------------------------------------------------------ helpers */

/* push a NULL terminated string as a record, without its terminator */
static void _push(relay_io_ring_t *ring, const char *str)
{
	size_t len = strlen(str);
	char *buf = xmalloc(len);

	memcpy(buf, str, len);
	relay_io_ring_push(ring, buf, len);
}

/* requeue a NULL terminated string as a record, without its terminator */
static bool _requeue(relay_io_ring_t *ring, const char *str)
{
	size_t len = strlen(str);
	char *buf = xmalloc(len);

	memcpy(buf, str, len);

	return relay_io_ring_requeue(ring, buf, len);
}

/* dequeue and compare against an expected string, then free */
static void _expect(relay_io_ring_t *ring, const char *expected)
{
	size_t len = 0;
	char *got = relay_io_ring_dequeue(ring, &len);

	ck_assert_msg(got != NULL, "expected \"%s\", ring was empty", expected);
	ck_assert_msg(len == strlen(expected), "expected %zu bytes, got %zu",
		      strlen(expected), len);
	ck_assert_msg(!memcmp(got, expected, len),
		      "expected \"%s\", got \"%.*s\"", expected, (int) len,
		      got);

	xfree(got);
}

/* -------------------------------------------------------------------- tests */

START_TEST(test_ring_empty)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);
	size_t bytes = 1;
	uint32_t count = 1;
	uint64_t dropped = 1;

	ck_assert(relay_io_ring_dequeue(ring, NULL) == NULL);

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 0);
	ck_assert_int_eq(count, 0);
	ck_assert_int_eq(dropped, 0);

	relay_io_ring_destroy(&ring);
	ck_assert(ring == NULL);
}

END_TEST

START_TEST(test_ring_fifo)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);
	size_t bytes = 0;
	uint32_t count = 0;

	_push(ring, "first");
	_push(ring, "second");
	_push(ring, "third");

	relay_io_ring_stats(ring, &bytes, &count, NULL);
	ck_assert_int_eq(count, 3);
	ck_assert_int_eq(bytes,
			 strlen("first") + strlen("second") + strlen("third"));

	_expect(ring, "first");
	_expect(ring, "second");
	_expect(ring, "third");
	ck_assert(relay_io_ring_dequeue(ring, NULL) == NULL);

	/* dequeueing must give the bytes back */
	relay_io_ring_stats(ring, &bytes, &count, NULL);
	ck_assert_int_eq(bytes, 0);
	ck_assert_int_eq(count, 0);

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_evicts_oldest)
{
	relay_io_ring_t *ring = relay_io_ring_create(10);
	size_t bytes = 0;
	uint32_t count = 0;
	uint64_t dropped = 0;

	_push(ring, "aaaa");
	_push(ring, "bbbb");

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 8);
	ck_assert_int_eq(count, 2);
	ck_assert_int_eq(dropped, 0);

	/* pushes past the budget, so the oldest record goes */
	_push(ring, "cccc");

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 8);
	ck_assert_int_eq(count, 2);
	ck_assert_int_eq(dropped, 1);

	_expect(ring, "bbbb");
	_expect(ring, "cccc");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_evicts_several)
{
	relay_io_ring_t *ring = relay_io_ring_create(10);
	uint32_t count = 0;
	uint64_t dropped = 0;

	_push(ring, "aa");
	_push(ring, "bb");
	_push(ring, "cc");
	_push(ring, "dd");
	_push(ring, "ee");

	/* one big record should push out several small ones at once */
	_push(ring, "ffffffff");

	relay_io_ring_stats(ring, NULL, &count, &dropped);
	ck_assert_int_eq(count, 2);
	ck_assert_int_eq(dropped, 4);

	_expect(ring, "ee");
	_expect(ring, "ffffffff");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_oversized_record_kept)
{
	relay_io_ring_t *ring = relay_io_ring_create(4);
	size_t bytes = 0;
	uint32_t count = 0;
	uint64_t dropped = 0;

	/* a record larger than the whole budget must not evict itself */
	_push(ring, "aaaaaaaaaa");

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 10);
	ck_assert_int_eq(count, 1);
	ck_assert_int_eq(dropped, 0);

	/* but the next push does evict it */
	_push(ring, "bb");

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 2);
	ck_assert_int_eq(count, 1);
	ck_assert_int_eq(dropped, 1);

	_expect(ring, "bb");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_push_nothing)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);
	uint32_t count = 1;

	relay_io_ring_push(ring, NULL, 0);
	relay_io_ring_push(ring, NULL, 10);
	/* a zero length record is dropped, and its buffer is freed */
	relay_io_ring_push(ring, xmalloc(1), 0);

	relay_io_ring_stats(ring, NULL, &count, NULL);
	ck_assert_int_eq(count, 0);

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_destroy_non_empty)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);

	_push(ring, "still here");
	_push(ring, "and here");

	/* records left behind must be freed with the ring */
	relay_io_ring_destroy(&ring);
	ck_assert(ring == NULL);

	/* destroying twice, or a NULL ring, is harmless */
	relay_io_ring_destroy(&ring);
	relay_io_ring_destroy(NULL);
}

END_TEST

START_TEST(test_ring_embedded_nul)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);
	char *buf = xmalloc(4);
	size_t len = 0;
	char *got = NULL;

	/* records are byte ranges, not strings */
	memcpy(buf, "a\0b", 3);
	relay_io_ring_push(ring, buf, 3);

	got = relay_io_ring_dequeue(ring, &len);
	ck_assert(got != NULL);
	ck_assert_int_eq(len, 3);
	ck_assert(!memcmp(got, "a\0b", 3));
	xfree(got);

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_requeue_goes_to_front)
{
	relay_io_ring_t *ring = relay_io_ring_create(64);

	_push(ring, "second");
	_push(ring, "third");

	/* a record put back must stay next in line, not go to the back */
	ck_assert(_requeue(ring, "first"));

	_expect(ring, "first");
	_expect(ring, "second");
	_expect(ring, "third");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_requeue_dropped_when_full)
{
	relay_io_ring_t *ring = relay_io_ring_create(10);
	size_t bytes = 0;
	uint32_t count = 0;
	uint64_t dropped = 0;

	_push(ring, "aaaaa");
	_push(ring, "bbbbb");

	/*
	 * Whatever arrived while this record was away has at least as much
	 * claim on the space, so the record being put back is dropped rather
	 * than evicting them.
	 */
	ck_assert(!_requeue(ring, "cc"));

	relay_io_ring_stats(ring, &bytes, &count, &dropped);
	ck_assert_int_eq(bytes, 10);
	ck_assert_int_eq(count, 2);
	ck_assert_int_eq(dropped, 1);

	_expect(ring, "aaaaa");
	_expect(ring, "bbbbb");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_requeue_oversized_into_empty)
{
	relay_io_ring_t *ring = relay_io_ring_create(4);
	uint32_t count = 0;

	/*
	 * Matches push: an oversized record is kept when nothing else is
	 * waiting, so whether it survives cannot depend on having been
	 * through a failed delivery.
	 */
	ck_assert(_requeue(ring, "aaaaaaaaaa"));

	relay_io_ring_stats(ring, NULL, &count, NULL);
	ck_assert_int_eq(count, 1);

	_expect(ring, "aaaaaaaaaa");

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_drop_report)
{
	relay_io_ring_t *ring = relay_io_ring_create(4);
	uint64_t dropped = 0;

	ck_assert_int_eq(relay_io_ring_drop_report(ring), 0);

	/* the first drop is due at once, and taking it clears it */
	_push(ring, "aaaa");
	_push(ring, "bbbb");
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 1);
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 0);

	/* then nothing is due until the next interval */
	for (int i = 2; i < RING_DROP_LOG_INTERVAL; i++)
		_push(ring, "cccc");
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 0);

	/* a report not yet taken gives way to the latest count */
	for (int i = 0; i <= RING_DROP_LOG_INTERVAL; i++)
		_push(ring, "dddd");
	ck_assert_int_eq(relay_io_ring_drop_report(ring),
			 (2 * RING_DROP_LOG_INTERVAL));
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 0);

	/* taking a report does not change the cumulative count */
	relay_io_ring_stats(ring, NULL, NULL, &dropped);
	ck_assert_int_eq(dropped, (2 * RING_DROP_LOG_INTERVAL));

	relay_io_ring_destroy(&ring);
}

END_TEST

START_TEST(test_ring_drop_report_requeue)
{
	relay_io_ring_t *ring = relay_io_ring_create(4);

	_push(ring, "aaaa");

	/* a requeue that does not fit is a drop like any other */
	ck_assert(!_requeue(ring, "bb"));
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 1);
	ck_assert_int_eq(relay_io_ring_drop_report(ring), 0);

	relay_io_ring_destroy(&ring);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	TCase *tcase = tcase_create("ring_buffer");
	Suite *suite = suite_create("ring_buffer");
	SRunner *sr = NULL;

	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("test_ring_buffer", log_opts, 0, NULL);

	tcase_add_test(tcase, test_ring_empty);
	tcase_add_test(tcase, test_ring_fifo);
	tcase_add_test(tcase, test_ring_evicts_oldest);
	tcase_add_test(tcase, test_ring_evicts_several);
	tcase_add_test(tcase, test_ring_oversized_record_kept);
	tcase_add_test(tcase, test_ring_push_nothing);
	tcase_add_test(tcase, test_ring_destroy_non_empty);
	tcase_add_test(tcase, test_ring_embedded_nul);
	tcase_add_test(tcase, test_ring_requeue_goes_to_front);
	tcase_add_test(tcase, test_ring_requeue_dropped_when_full);
	tcase_add_test(tcase, test_ring_requeue_oversized_into_empty);
	tcase_add_test(tcase, test_ring_drop_report);
	tcase_add_test(tcase, test_ring_drop_report_requeue);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
