/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test the relay_io record document builder
 * (src/plugins/relay_io/common/relay_io_common.c) directly.
 *
 * Every relay_io plugin renders a record through this one function, so the
 * cases pin the schema a consumer reads: the fields every record carries, the
 * ones that ride only on job events, how a step id and a sanitized body come
 * out, and where a body limit cuts.
 *****************************************************************************/

#include <check.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/relay_io.h"

/*
 * The document builder is a convenience library linked into each relay_io
 * plugin rather than part of libslurmfull, so there is no exported symbol to
 * link against. Build it into the test instead.
 */
#include "src/plugins/relay_io/common/relay_io_common.c"

/* ------------------------------------------------------------------ helpers */

static relay_io_record_t _base_record(void)
{
	return (relay_io_record_t) {
		.type = RELAY_IO_EVENT_LOG,
		.time = {
			.tv_sec = 1786000000,
			.tv_nsec = 123 * NSEC_IN_MSEC,
		},
		.ident = {
			.cluster = "testcluster",
			.node = "node1",
			.job_id = 4242,
			.sluid = 0x123456789abcdef0,
			.user_name = "testuser",
			.partition = "testpart",
			.account = "testacct",
			.restart_count = 2,
		},
		.step_id = 3,
		.task_id_global = 7,
		.task_id_local = 1,
		.stream = RELAY_IO_STREAM_STDOUT,
		.body = "hello\n",
		.body_len = 6,
	};
}

/* pack with a body limit and report what the body came out as */
static char *_packed_body(relay_io_record_t *rec, size_t limit,
			  size_t *consumed_ptr, bool *partial_ptr)
{
	data_t *d = data_new();
	data_t *partial = NULL;
	char *body = NULL;

	relay_io_record_pack_data(rec, d, limit, consumed_ptr);

	body = xstrdup(data_get_string(data_key_get(d, "body")));
	partial = data_key_get(d, "partial");
	if (partial_ptr)
		*partial_ptr = (partial && data_get_bool(partial));

	FREE_NULL_DATA(d);

	return body;
}

/* -------------------------------------------------------------------- tests */

START_TEST(test_record_log)
{
	relay_io_record_t rec = _base_record();
	data_t *d = data_new();
	data_t *slurm = NULL;

	relay_io_record_pack_data(&rec, d, SIZE_MAX, NULL);

	ck_assert(data_get_type(d) == DATA_TYPE_DICT);
	ck_assert_int_eq(data_get_int(data_key_get(d, "v")),
			 RELAY_IO_SCHEMA_VERSION);
	ck_assert_str_eq(data_get_string(data_key_get(d, "type")), "log");
	ck_assert_str_eq(data_get_string(data_key_get(d, "stream")), "stdout");
	ck_assert_str_eq(data_get_string(data_key_get(d, "body")), "hello\n");

	slurm = data_key_get(d, "slurm");
	ck_assert(data_get_type(slurm) == DATA_TYPE_DICT);
	ck_assert_str_eq(data_get_string(data_key_get(slurm, "cluster")),
			 "testcluster");
	ck_assert_str_eq(data_get_string(data_key_get(slurm, "node")), "node1");
	ck_assert_int_eq(data_get_int(data_key_get(slurm, "job_id")), 4242);
	ck_assert_str_eq(data_get_string(data_key_get(slurm, "user_name")),
			 "testuser");

	/*
	 * These describe the whole job, so they are not repeated on every
	 * line of its output.
	 */
	ck_assert(data_key_get(slurm, "restart_count") == NULL);
	ck_assert(data_key_get(slurm, "partition") == NULL);
	ck_assert(data_key_get(slurm, "account") == NULL);
	ck_assert_str_eq(data_get_string(data_key_get(slurm, "step_id")), "3");
	ck_assert_int_eq(data_get_int(data_key_get(slurm, "task_id_global")),
			 7);
	ck_assert_int_eq(data_get_int(data_key_get(slurm, "task_id_local")), 1);
	ck_assert(data_key_get(slurm, "sluid") != NULL);

	/* absent unless true, to keep records small */
	ck_assert(data_key_get(d, "partial") == NULL);
	ck_assert(data_key_get(d, "unbuffered") == NULL);

	FREE_NULL_DATA(d);
}

END_TEST

START_TEST(test_record_log_flags)
{
	relay_io_record_t rec = _base_record();
	data_t *d = data_new();

	rec.stream = RELAY_IO_STREAM_STDERR;
	rec.partial = true;
	rec.unbuffered = true;

	relay_io_record_pack_data(&rec, d, SIZE_MAX, NULL);

	ck_assert_str_eq(data_get_string(data_key_get(d, "stream")), "stderr");
	ck_assert(data_get_bool(data_key_get(d, "partial")));
	ck_assert(data_get_bool(data_key_get(d, "unbuffered")));

	FREE_NULL_DATA(d);
}

END_TEST

START_TEST(test_record_step_id_names)
{
	relay_io_record_t rec = _base_record();
	data_t *d = data_new();

	rec.step_id = SLURM_BATCH_SCRIPT;
	relay_io_record_pack_data(&rec, d, SIZE_MAX, NULL);
	ck_assert_str_eq(data_get_string(data_key_get(data_key_get(d, "slurm"),
						      "step_id")),
			 "batch");
	FREE_NULL_DATA(d);

	d = data_new();
	rec.step_id = SLURM_INTERACTIVE_STEP;
	relay_io_record_pack_data(&rec, d, SIZE_MAX, NULL);
	ck_assert_str_eq(data_get_string(data_key_get(data_key_get(d, "slurm"),
						      "step_id")),
			 "interactive");
	FREE_NULL_DATA(d);
}

END_TEST

START_TEST(test_record_identity_may_be_absent)
{
	relay_io_record_t rec = _base_record();
	data_t *d = data_new();
	data_t *slurm = NULL;

	/* the credential does not always carry these */
	rec.type = RELAY_IO_EVENT_JOB_START;
	rec.ident.user_name = NULL;
	rec.ident.partition = NULL;
	rec.ident.account = NULL;

	relay_io_record_pack_data(&rec, d, SIZE_MAX, NULL);

	slurm = data_key_get(d, "slurm");

	/* left out entirely rather than emitted as null */
	ck_assert(data_key_get(slurm, "user_name") == NULL);
	ck_assert(data_key_get(slurm, "partition") == NULL);
	ck_assert(data_key_get(slurm, "account") == NULL);

	/* and the rest of the record is unaffected */
	ck_assert_int_eq(data_get_int(data_key_get(slurm, "job_id")), 4242);

	FREE_NULL_DATA(d);
}

END_TEST

/* The Unicode replacement character, U+FFFD, as UTF-8. */
#define REPL "\xef\xbf\xbd"

/* What a NUL becomes, since it would truncate the serialized string. */
#define NUL_ESC "\\u0000"

typedef struct {
	const char *input;
	size_t in_len;
	const char *expected;
} sanitize_data_t;

static sanitize_data_t sanitize_data[] = {
	/* already valid input is copied through untouched */
	{ "", 0, "" },
	{ "hello\n", 6, "hello\n" },
	{ "\xc3\xa9", 2, "\xc3\xa9" }, /* U+00E9 */
	{ "\xe2\x82\xac", 3, "\xe2\x82\xac" }, /* U+20AC */
	{ "\xf0\x9f\x92\xa9", 4, "\xf0\x9f\x92\xa9" }, /* U+1F4A9 */
	{ "\xe0\xa0\x80", 3, "\xe0\xa0\x80" }, /* U+0800, lowest 3 byte */

	/* a stray continuation byte is one ill-formed subpart */
	{ "\x80", 1, REPL },
	{ "\xbf", 1, REPL },

	/*
	 * A sequence cut off by the end of the input is replaced a byte at a
	 * time, since nothing that follows can complete it.
	 */
	{ "\xc3", 1, REPL },
	{ "\xe2\x82", 2, REPL REPL },
	{ "\xf0\x9f\x92", 3, REPL REPL REPL },

	/*
	 * A sequence that parses but does not encode a usable code is one
	 * ill-formed subpart, however many bytes it spans.
	 */
	{ "\xc0\x80", 2, REPL }, /* overlong */
	{ "\xc1\xbf", 2, REPL }, /* overlong */
	{ "\xe0\x80\x80", 3, REPL }, /* overlong */
	{ "\xf0\x80\x80\x80", 4, REPL }, /* overlong */
	{ "\xed\xa0\x80", 3, REPL }, /* surrogate */
	{ "\xed\xbf\xbf", 3, REPL }, /* surrogate */
	{ "\xf4\x90\x80\x80", 4, REPL }, /* above U+10FFFF */
	{ "\xf5\x80\x80\x80", 4, REPL }, /* lead byte out of range */

	/* bytes that can never appear in UTF-8 */
	{ "\xfe", 1, REPL },
	{ "\xff", 1, REPL },

	/* a NUL is escaped rather than replaced, so the byte is not lost */
	{ "a\0b", 3, "a" NUL_ESC "b" },
	{ "\0", 1, NUL_ESC },

	/* good and bad mixed together */
	{ "ok\xff"
	  "bad\n",
	  7, "ok" REPL "bad\n" },
	{ "\xe2\x82\xac"
	  "x",
	  4,
	  "\xe2\x82\xac"
	  "x" },
	{ "a\xe2\x82"
	  "b",
	  4, "a" REPL "b" },
};

/*
 * Every record body is made well formed UTF-8, whatever the task wrote.
 *
 * This is the contract a consumer relies on: a record is JSON, and JSON text
 * has to be valid Unicode, so no byte of task output may reach the wire as
 * itself unless it is already part of a well formed sequence.
 */
START_TEST(test_record_body_sanitize_table)
{
	relay_io_record_t rec = _base_record();
	char *body = NULL;

	rec.body = sanitize_data[_i].input;
	rec.body_len = sanitize_data[_i].in_len;

	body = _packed_body(&rec, SIZE_MAX, NULL, NULL);

	ck_assert_msg(body != NULL, "sanitize[%d]: got NULL", _i);
	ck_assert_msg(!strcmp(body, sanitize_data[_i].expected),
		      "sanitize[%d]: got \"%s\", want \"%s\"", _i, body,
		      sanitize_data[_i].expected);

	xfree(body);
}

END_TEST

START_TEST(test_body_limit_cuts_and_reports)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 0;
	bool partial = false;
	char *body = NULL;

	rec.body = "0123456789";
	rec.body_len = 10;

	/* a limit larger than the body takes all of it and is not partial */
	body = _packed_body(&rec, SIZE_MAX, &consumed, &partial);
	ck_assert_int_eq(consumed, 10);
	ck_assert_str_eq(body, "0123456789");
	ck_assert(!partial);
	xfree(body);

	/* a limit inside the body cuts there and says so */
	body = _packed_body(&rec, 6, &consumed, &partial);
	ck_assert_int_eq(consumed, 6);
	ck_assert_str_eq(body, "012345");
	ck_assert(partial);
	xfree(body);

	/* an exact fit is not a cut */
	body = _packed_body(&rec, 10, &consumed, &partial);
	ck_assert_int_eq(consumed, 10);
	ck_assert(!partial);
	xfree(body);
}

END_TEST

/*
 * An escaped NUL is the widest a single input byte can become, so a limit
 * below that would leave no limit a body could be cut at. The builder raises
 * one that low rather than returning nothing.
 */
START_TEST(test_body_limit_has_a_floor)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 0;
	char *body = NULL;

	rec.body = "0123456789";
	rec.body_len = 10;

	body = _packed_body(&rec, 1, &consumed, NULL);
	ck_assert_int_eq(consumed, NULL_ESCAPE_LEN);
	ck_assert_str_eq(body, "012345");
	xfree(body);

	/* one NUL is exactly what the floor leaves room for */
	rec.body = "\0!";
	rec.body_len = 2;

	body = _packed_body(&rec, 0, &consumed, NULL);
	ck_assert_int_eq(consumed, 1);
	ck_assert_str_eq(body, NULL_ESCAPE_SEQUENCE);
	xfree(body);
}

END_TEST

START_TEST(test_body_limit_keeps_characters_whole)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 0;
	char *body = NULL;

	/* six 2 byte characters */
	rec.body = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
	rec.body_len = 12;

	/* a limit landing mid character keeps the character out */
	body = _packed_body(&rec, 7, &consumed, NULL);
	ck_assert_int_eq(consumed, 6);
	ck_assert_str_eq(body, "\xc3\xa9\xc3\xa9\xc3\xa9");
	xfree(body);

	body = _packed_body(&rec, 9, &consumed, NULL);
	ck_assert_int_eq(consumed, 8);
	xfree(body);

	/* a character wider than what is left is kept out whole */
	rec.body = "\xf0\x9f\x92\xa9\xf0\x9f\x92\xa9";
	rec.body_len = 8;

	body = _packed_body(&rec, 7, &consumed, NULL);
	ck_assert_int_eq(consumed, 4);
	ck_assert_str_eq(body, "\xf0\x9f\x92\xa9");
	xfree(body);
}

END_TEST

START_TEST(test_body_limit_cuts_where_sanitizer_does)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 0;
	char *body = NULL;

	/* three bad bytes, each its own maximal subpart, then an ascii byte */
	rec.body = "\xff\xfe\xfdZ";
	rec.body_len = 4;

	/*
	 * a cut counts the replacements the input bytes became, and consumed
	 * reports how many input bytes produced them
	 */
	body = _packed_body(&rec, 6, &consumed, NULL);
	ck_assert_int_eq(consumed, 2);
	ck_assert_str_eq(body, "\xef\xbf\xbd\xef\xbf\xbd");
	xfree(body);

	body = _packed_body(&rec, 9, &consumed, NULL);
	ck_assert_int_eq(consumed, 3);
	ck_assert_str_eq(body, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd");
	xfree(body);
}

END_TEST

START_TEST(test_body_limit_unbuffered_is_never_partial)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 0;
	bool partial = false;
	char *body = NULL;

	rec.body = "0123456789";
	rec.body_len = 10;
	rec.unbuffered = true;

	/*
	 * An unbuffered record is cut at an arbitrary offset by definition,
	 * which "unbuffered" already says, so a size cut adds no partial.
	 */
	body = _packed_body(&rec, 6, &consumed, &partial);
	ck_assert_int_eq(consumed, 6);
	ck_assert(!partial);
	xfree(body);
}

END_TEST

START_TEST(test_body_limit_ignored_without_a_body)
{
	relay_io_record_t rec = _base_record();
	size_t consumed = 1;
	data_t *d = data_new();

	rec.type = RELAY_IO_EVENT_JOB_START;

	/* a job event has no body to cut, so a limit changes nothing */
	relay_io_record_pack_data(&rec, d, 1, &consumed);
	ck_assert_int_eq(consumed, 0);
	ck_assert(!data_key_get(d, "body"));
	ck_assert_str_eq(data_get_string(data_key_get(d, "type")), "job_start");

	FREE_NULL_DATA(d);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	TCase *tcase = tcase_create("relay_io_common");
	Suite *suite = suite_create("relay_io_common");
	SRunner *sr = NULL;

	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("test_relay_io_common", log_opts, 0, NULL);

	tcase_add_test(tcase, test_record_log);
	tcase_add_test(tcase, test_record_log_flags);
	tcase_add_test(tcase, test_record_step_id_names);
	tcase_add_loop_test(tcase, test_record_body_sanitize_table, 0,
			    (sizeof(sanitize_data) / sizeof(sanitize_data_t)));
	tcase_add_test(tcase, test_record_identity_may_be_absent);
	tcase_add_test(tcase, test_body_limit_cuts_and_reports);
	tcase_add_test(tcase, test_body_limit_has_a_floor);
	tcase_add_test(tcase, test_body_limit_keeps_characters_whole);
	tcase_add_test(tcase, test_body_limit_cuts_where_sanitizer_does);
	tcase_add_test(tcase, test_body_limit_unbuffered_is_never_partial);
	tcase_add_test(tcase, test_body_limit_ignored_without_a_body);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
