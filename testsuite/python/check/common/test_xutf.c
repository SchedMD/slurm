/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test the xutf UTF-8 handler (src/common/xutf.c) directly.
 *
 * The cases pin the handler against The Unicode Standard, Version 15.0:
 *   - Table 3-7 "Well-Formed UTF-8 Byte Sequences" (the canonical accept/reject
 *     specification for UTF-8 byte sequences),
 *   - D14 noncharacters, D49 private-use, D71-D73 surrogates, D92 UTF-8 (the
 *     shortest-form / no-overlong requirement),
 *   - the U+0000..U+10FFFF codespace bounds,
 *   - the BOM signatures of Table 2-4,
 *   - the newline (5.8), space (Table 6-2) and C0/C1 control sets.
 *****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/xutf.h"

/*
 * Build a const utf8_t[] compound literal inline so a byte sequence can be
 * written as RB(0xED, 0xA0, 0x80). The literal has automatic storage tied to
 * the enclosing block, so it stays valid for the helper call it is passed to.
 */
#define RB(...) ((const utf8_t[]) { __VA_ARGS__ })

/* ------------------------------------------------------------------ helpers */

/* assert that src[0..len) decodes to exactly exp_code in exp_bytes bytes */
static void assert_read(const char *desc, const utf8_t *src, size_t len,
			bool check_valid, utf_code_t exp_code, int exp_bytes)
{
	utf_code_t utf = 0;
	int bytes = 0;
	int rc = utf8_read_character(src, src + len, &utf, &bytes, check_valid);

	ck_assert_msg(rc == SLURM_SUCCESS,
		      "%s: expected success, got rc=%d (%s)", desc, rc,
		      slurm_strerror(rc));
	ck_assert_msg(utf == exp_code,
		      "%s: expected U+%06" PRIX32 ", got U+%06" PRIX32, desc,
		      exp_code, utf);
	ck_assert_msg(bytes == exp_bytes, "%s: expected %d bytes, got %d", desc,
		      exp_bytes, bytes);
}

/*
 * assert that src[0..len) is rejected. If exp_rc is non-zero, the exact error
 * code must match. On rejection the handler sets *utf to UTF_REPLACEMENT_CODE
 * and *bytes to the resync count, which must equal exp_bytes exactly.
 *
 * exp_bytes is how far a scanning caller advances past an ill-formed sequence.
 * An off-by-one there silently swallows the character that follows it.
 */
static void assert_not_read(const char *desc, const utf8_t *src, size_t len,
			    bool check_valid, int exp_rc, int exp_bytes)
{
	utf_code_t utf = 0;
	int bytes = 0;
	int rc = utf8_read_character(src, src + len, &utf, &bytes, check_valid);

	ck_assert_msg(rc != SLURM_SUCCESS,
		      "%s: expected rejection, got success (U+%06" PRIX32 ")",
		      desc, utf);
	ck_assert_msg(rc == exp_rc, "%s: expected rc=%d (%s), got rc=%d (%s)",
		      desc, exp_rc, slurm_strerror(exp_rc), rc,
		      slurm_strerror(rc));
	ck_assert_msg(utf == UTF_REPLACEMENT_CODE,
		      "%s: utf not U+FFFD on error (got U+%06" PRIX32 ")", desc,
		      utf);
	ck_assert_msg(bytes == exp_bytes,
		      "%s: expected resync of %d bytes, got %d", desc,
		      exp_bytes, bytes);
	/* a resync must always make progress and stay inside the buffer */
	ck_assert_msg((bytes >= 1) && (bytes <= (int) len),
		      "%s: resync count out of range (got %d, len %zu)", desc,
		      bytes, len);
}

/* one code unit produced by the read-and-advance loop */
typedef struct {
	utf_code_t utf; /* decoded scalar, or UTF_REPLACEMENT_CODE */
	int bytes; /* bytes consumed producing it */
} utf_step_t;

/*
 * Feed src[0..len) through the read-and-advance loop that callers scanning a
 * whole string use, and assert the exact sequence of (code, advance) pairs it
 * produces against exp[0..exp_cnt).
 *
 * The advance decides how many U+FFFD a scanner emits and which byte it resumes
 * on. A loop that over-advances drops the valid character following an
 * ill-formed sequence without raising any error.
 *
 * NOTE: several of these expectations deliberately differ from Unicode 15.0 3.9
 * "U+FFFD Substitution of Maximal Subparts". The reader accepts any 0x80..0xBF
 * continuation byte rather than applying Table 3-7's per-lead second-byte
 * ranges, so it rejects by code value afterwards and emits one U+FFFD for the
 * whole sequence where 3.9 calls for one per byte (E0 80 80, ED A0 80,
 * F0 80 80 80, F4 90 80 80), and conversely splits E0 A0 into two where 3.9
 * gives one. These pin the handler as it behaves today; if the reader is ever
 * made Table 3-7 conformant, these are the expectations to update.
 */
static void assert_read_sequence(const char *desc, const utf8_t *src,
				 size_t len, const utf_step_t *exp,
				 size_t exp_cnt)
{
	const utf8_t *ptr = src;
	const utf8_t *end = src + len;
	size_t i = 0;

	while (ptr < end) {
		utf_code_t utf = 0;
		int bytes = 0;
		int rc = utf8_read_character(ptr, end, &utf, &bytes, true);

		ck_assert_msg(i < exp_cnt,
			      "%s: produced more than the %zu expected units",
			      desc, exp_cnt);
		ck_assert_msg((bytes >= 1) && (bytes <= (end - ptr)),
			      "%s: advance %d out of range (%ld remaining)",
			      desc, bytes, (long) (end - ptr));
		ck_assert_msg(utf == exp[i].utf,
			      "%s: unit %zu expected U+%06" PRIX32
			      ", got U+%06" PRIX32,
			      desc, i, exp[i].utf, utf);
		ck_assert_msg(bytes == exp[i].bytes,
			      "%s: unit %zu expected advance %d, got %d", desc,
			      i, exp[i].bytes, bytes);

		if (rc != SLURM_SUCCESS)
			ck_assert_msg(utf == UTF_REPLACEMENT_CODE,
				      "%s: gave U+%06" PRIX32 ", not U+FFFD",
				      desc, utf);

		ptr += bytes;
		i++;
	}

	ck_assert_msg(i == exp_cnt, "%s: expected %zu units, got %zu", desc,
		      exp_cnt, i);
	ck_assert_msg(ptr == end, "%s: buffer not fully consumed", desc);
}

/* call assert_read_sequence() with an inline expectation array */
#define ASSERT_SEQ(desc, src, len, ...) \
	do { \
		const utf_step_t _exp[] = { __VA_ARGS__ }; \
		assert_read_sequence((desc), (src), (len), _exp, \
				     ARRAY_SIZE(_exp)); \
	} while (false)

/* assert that utf encodes to exactly the exp[0..exp_len) byte sequence */
static void assert_write(const char *desc, utf_code_t utf, const utf8_t *exp,
			 int exp_len)
{
	utf8_t dst[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = 0;
	/*
	 * The public macro is what callers use; it handles ASCII inline without
	 * entering the function. test_write_ascii_direct() covers the
	 * function's own ASCII branch.
	 */
	int rc = utf8_write_character(utf, dst, &bytes);

	ck_assert_msg(rc == SLURM_SUCCESS, "%s: write rc=%d (%s)", desc, rc,
		      slurm_strerror(rc));
	ck_assert_msg(bytes == exp_len, "%s: expected %d bytes, got %d", desc,
		      exp_len, bytes);
	ck_assert_msg(!memcmp(dst, exp, exp_len),
		      "%s: encoded bytes do not match expected", desc);
}

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	/*
	 * xutf's LOG() is gated on DEBUG_FLAG_DATA plus debug5, and that trace
	 * is the practical way to find which of the 1114111 code points
	 * test_codespace_roundtrip failed on.
	 */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("xutf-test", log_opts, 0, NULL);
}

static void teardown(void)
{
	log_fini();
}

/* ------------------------------ slurm_read_utf8_character: Table 3-7 */

/* Table 3-7 row 1: U+0000..U+007F (single ASCII byte) */
START_TEST(test_read_ascii)
{
	assert_read("U+0001", RB(0x01), 1, true, 0x01, 1);
	assert_read("U+0041 'A'", RB(0x41), 1, true, 0x41, 1);
	assert_read("U+007F boundary", RB(0x7F), 1, true, 0x7F, 1);

	/* U+0000 is rejected by policy (check_valid only) */
	assert_not_read("U+0000 NUL", RB(0x00), 1, true, ESLURM_UTF_NULL_CODE,
			1);
}

END_TEST

/* Table 3-7 row 2: U+0080..U+07FF, lead C2..DF */
START_TEST(test_read_two_byte)
{
	assert_read("U+0080 low boundary", RB(0xC2, 0x80), 2, true, 0x0080, 2);
	assert_read("U+00F1 'n-tilde'", RB(0xC3, 0xB1), 2, true, 0x00F1, 2);
	assert_read("U+07FF high boundary", RB(0xDF, 0xBF), 2, true, 0x07FF, 2);
}

END_TEST

/* Table 3-7 rows 3-6: U+0800..U+FFFF, leads E0,E1..EC,ED,EE..EF */
START_TEST(test_read_three_byte)
{
	assert_read("U+0800 row-3 low", RB(0xE0, 0xA0, 0x80), 3, true, 0x0800,
		    3);
	assert_read("U+0FFF row-3 high", RB(0xE0, 0xBF, 0xBF), 3, true, 0x0FFF,
		    3);
	assert_read("U+1000 row-4 low", RB(0xE1, 0x80, 0x80), 3, true, 0x1000,
		    3);
	assert_read("U+20AC euro", RB(0xE2, 0x82, 0xAC), 3, true, 0x20AC, 3);
	assert_read("U+CFFF row-4 high", RB(0xEC, 0xBF, 0xBF), 3, true, 0xCFFF,
		    3);
	assert_read("U+D000 row-5 low", RB(0xED, 0x80, 0x80), 3, true, 0xD000,
		    3);
	assert_read("U+D7FF row-5 high", RB(0xED, 0x9F, 0xBF), 3, true, 0xD7FF,
		    3);
	assert_read("U+FFFD replacement", RB(0xEF, 0xBF, 0xBD), 3, true, 0xFFFD,
		    3);
}

END_TEST

/* Table 3-7 rows 7-9: U+10000..U+10FFFF, leads F0,F1..F3,F4 */
START_TEST(test_read_four_byte)
{
	assert_read("U+10000 row-7 low", RB(0xF0, 0x90, 0x80, 0x80), 4, true,
		    0x10000, 4);
	assert_read("U+1F600 emoji", RB(0xF0, 0x9F, 0x98, 0x80), 4, true,
		    0x1F600, 4);
	assert_read("U+40000 row-8 low", RB(0xF1, 0x80, 0x80, 0x80), 4, true,
		    0x40000, 4);
	assert_read("U+80000 row-8 mid", RB(0xF2, 0x80, 0x80, 0x80), 4, true,
		    0x80000, 4);
	assert_read("U+E0100 plane-14", RB(0xF3, 0xA0, 0x84, 0x80), 4, true,
		    0xE0100, 4);
	/* U+100000..U+10FFFF (row 9, lead F4) is all private-use or
	 * noncharacters, so there is no valid plane-16 scalar to accept here;
	 * its boundaries are exercised by the private/noncharacter cases. */
}

END_TEST

/*
 * D92 / Table 3-7: a code point encoded in more bytes than the minimum is
 * ill-formed (overlong). The C0 and C1 lead bytes can only ever introduce an
 * overlong two-byte form, so the handler rejects them through the overlong
 * check (ESLURM_UTF8_INVALID_READ), not as structurally illegal leads.
 */
START_TEST(test_read_overlong)
{
	assert_not_read("overlong U+0000 (C0 80)", RB(0xC0, 0x80), 2, true,
			ESLURM_UTF8_INVALID_READ, 2);
	assert_not_read("overlong U+007F (C1 BF)", RB(0xC1, 0xBF), 2, true,
			ESLURM_UTF8_INVALID_READ, 2);
	assert_not_read("overlong U+0000 (E0 80 80)", RB(0xE0, 0x80, 0x80), 3,
			true, ESLURM_UTF8_INVALID_READ, 3);
	assert_not_read("overlong U+07FF (E0 9F BF)", RB(0xE0, 0x9F, 0xBF), 3,
			true, ESLURM_UTF8_INVALID_READ, 3);
	assert_not_read("overlong U+0000 (F0 80 80 80)",
			RB(0xF0, 0x80, 0x80, 0x80), 4, true,
			ESLURM_UTF8_INVALID_READ, 4);
	assert_not_read("overlong U+FFFF (F0 8F BF BF)",
			RB(0xF0, 0x8F, 0xBF, 0xBF), 4, true,
			ESLURM_UTF8_INVALID_READ, 4);
}

END_TEST

/*
 * D71-D73: surrogate code points U+D800..U+DFFF are ill-formed in UTF-8 and are
 * rejected by utf_is_valid() when check_valid is set.
 */
START_TEST(test_read_surrogate_bytes)
{
	assert_not_read("U+D800 surrogate", RB(0xED, 0xA0, 0x80), 3, true,
			ESLURM_UTF16_SURROGATE_CODE, 3);
	assert_not_read("U+DFFF surrogate", RB(0xED, 0xBF, 0xBF), 3, true,
			ESLURM_UTF16_SURROGATE_CODE, 3);
}

END_TEST

/* Codespace bound: U+10FFFF is the maximum; anything above is ill-formed. */
START_TEST(test_read_above_max)
{
	assert_not_read("U+110000 (F4 90 80 80)", RB(0xF4, 0x90, 0x80, 0x80), 4,
			true, ESLURM_UTF_INVALID_CODE, 4);
}

END_TEST

/*
 * Table 3-7 lists no row whose lead byte is F5..FF: those bytes can never begin
 * a sequence. This handler splits that set. F8..FF are refused structurally as
 * bad leads (test_read_illegal_lead), but the 4-byte branch masks with
 * (*src & 0xf8) == 0xf0, which matches F0..F7 -- so F5, F6 and F7 decode as
 * 4-byte leads, yielding U+140000..U+1FFFFF, and are caught only afterwards by
 * the codespace bound inside utf_is_valid().
 *
 * That makes F5..F7 the only Table 3-7 structural violation gated behind
 * check_valid; overlongs, bad continuations, truncation and F8..FF are all
 * unconditional. With check_valid=false these decode successfully to
 * out-of-codespace scalars (test_read_no_check_valid).
 *
 * If the reader is made Table 3-7 conformant, the expectation becomes
 * ESLURM_UTF8_INVALID_READ with a 1-byte resync.
 */
START_TEST(test_read_out_of_range_lead)
{
	assert_not_read("U+140000 (F5 80 80 80)", RB(0xF5, 0x80, 0x80, 0x80), 4,
			true, ESLURM_UTF_INVALID_CODE, 4);
	assert_not_read("U+180000 (F6 80 80 80)", RB(0xF6, 0x80, 0x80, 0x80), 4,
			true, ESLURM_UTF_INVALID_CODE, 4);
	assert_not_read("U+1FFFFF (F7 BF BF BF)", RB(0xF7, 0xBF, 0xBF, 0xBF), 4,
			true, ESLURM_UTF_INVALID_CODE, 4);

	/* a lone F5..F7 has no room for the 4-byte form it claims */
	assert_not_read("truncated lead (F5)", RB(0xF5), 1, true,
			ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);
	assert_not_read("truncated lead (F7)", RB(0xF7), 1, true,
			ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);
}

END_TEST

/* Table 3-7: bytes that are never valid leads, and stray continuation bytes. */
START_TEST(test_read_illegal_lead)
{
	/* every continuation byte 0x80..0xBF is ill-formed as a lead */
	for (int b = 0x80; b <= 0xBF; b++) {
		utf8_t seq[1] = { b };
		char desc[32];

		snprintf(desc, sizeof(desc), "stray continuation 0x%02X", b);
		assert_not_read(desc, seq, 1, true, ESLURM_UTF8_INVALID_READ,
				1);
	}

	/*
	 * F8..FF can never begin a UTF-8 sequence. F5..F7 are equally illegal
	 * leads per Table 3-7 but are not refused here; see
	 * test_read_out_of_range_lead().
	 */
	for (int b = 0xF8; b <= 0xFF; b++) {
		utf8_t seq[1] = { b };
		char desc[32];

		snprintf(desc, sizeof(desc), "illegal lead 0x%02X", b);
		assert_not_read(desc, seq, 1, true, ESLURM_UTF8_INVALID_READ,
				1);
	}
}

END_TEST

/*
 * Table 3-7 / D92: a multibyte sequence that runs past the end of the buffer is
 * an illegal termination; a non-continuation in byte 2/3/4 is an invalid byte.
 */
START_TEST(test_read_truncated)
{
	assert_not_read("2-byte truncated (C3)", RB(0xC3), 1, true,
			ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);
	assert_not_read("3-byte truncated (E2 82)", RB(0xE2, 0x82), 2, true,
			ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);
	assert_not_read("4-byte truncated (F0 9F 98)", RB(0xF0, 0x9F, 0x98), 3,
			true, ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);

	assert_not_read("bad byte 2 (C2 41)", RB(0xC2, 0x41), 2, true,
			ESLURM_UTF8_INVALID_BYTE_2, 1);
	assert_not_read("bad byte 2 (E2 41 82)", RB(0xE2, 0x41, 0x82), 3, true,
			ESLURM_UTF8_INVALID_BYTE_2, 1);
	assert_not_read("bad byte 2 (F0 41 80 80)", RB(0xF0, 0x41, 0x80, 0x80),
			4, true, ESLURM_UTF8_INVALID_BYTE_2, 1);
	assert_not_read("bad byte 3 (E2 82 41)", RB(0xE2, 0x82, 0x41), 3, true,
			ESLURM_UTF8_INVALID_BYTE_3, 2);
	assert_not_read("bad byte 3 (F0 9F 41 80)", RB(0xF0, 0x9F, 0x41, 0x80),
			4, true, ESLURM_UTF8_INVALID_BYTE_3, 2);
	assert_not_read("bad byte 4 (F0 9F 98 41)", RB(0xF0, 0x9F, 0x98, 0x41),
			4, true, ESLURM_UTF8_INVALID_BYTE_4, 3);
}

END_TEST

/*
 * Ill-formed sequences must resync safely: every code unit produced and every
 * byte advance is pinned exactly. See assert_read_sequence() for where these
 * expectations knowingly differ from Unicode 15.0 3.9.
 */
START_TEST(test_read_maximal_subpart)
{
	/* out-of-range 2nd byte / overlong / surrogate / above-max / bad lead */
	ASSERT_SEQ("overlong E0 80 80", RB(0xE0, 0x80, 0x80), 3,
		   { UTF_REPLACEMENT_CODE, 3 });
	ASSERT_SEQ("overlong E0 9F BF", RB(0xE0, 0x9F, 0xBF), 3,
		   { UTF_REPLACEMENT_CODE, 3 });
	ASSERT_SEQ("surrogate ED A0 80", RB(0xED, 0xA0, 0x80), 3,
		   { UTF_REPLACEMENT_CODE, 3 });
	ASSERT_SEQ("overlong F0 80 80 80", RB(0xF0, 0x80, 0x80, 0x80), 4,
		   { UTF_REPLACEMENT_CODE, 4 });
	ASSERT_SEQ("above max F4 90 80 80", RB(0xF4, 0x90, 0x80, 0x80), 4,
		   { UTF_REPLACEMENT_CODE, 4 });
	ASSERT_SEQ("overlong C0 AF", RB(0xC0, 0xAF), 2,
		   { UTF_REPLACEMENT_CODE, 2 });
	ASSERT_SEQ("bad lead F5 80 80 80", RB(0xF5, 0x80, 0x80, 0x80), 4,
		   { UTF_REPLACEMENT_CODE, 4 });
	ASSERT_SEQ("lone continuation 80", RB(0x80), 1,
		   { UTF_REPLACEMENT_CODE, 1 });

	/*
	 * Bad continuation byte followed by decodable ASCII. Each sequence is
	 * long enough to reach the continuation-byte check it is named for; a
	 * shorter buffer trips the length guard first and lands in the
	 * truncation cases below instead.
	 */
	ASSERT_SEQ("bad 2nd C2 41", RB(0xC2, 0x41), 2,
		   { UTF_REPLACEMENT_CODE, 1 }, { 0x41, 1 });
	ASSERT_SEQ("bad 2nd E0 41 42", RB(0xE0, 0x41, 0x42), 3,
		   { UTF_REPLACEMENT_CODE, 1 }, { 0x41, 1 }, { 0x42, 1 });
	ASSERT_SEQ("bad 2nd F0 41 42 43", RB(0xF0, 0x41, 0x42, 0x43), 4,
		   { UTF_REPLACEMENT_CODE, 1 }, { 0x41, 1 }, { 0x42, 1 },
		   { 0x43, 1 });
	ASSERT_SEQ("bad 3rd E2 82 41", RB(0xE2, 0x82, 0x41), 3,
		   { UTF_REPLACEMENT_CODE, 2 }, { 0x41, 1 });
	ASSERT_SEQ("bad 3rd F0 90 41 42", RB(0xF0, 0x90, 0x41, 0x42), 4,
		   { UTF_REPLACEMENT_CODE, 2 }, { 0x41, 1 }, { 0x42, 1 });
	ASSERT_SEQ("bad 4th F0 90 80 41", RB(0xF0, 0x90, 0x80, 0x41), 4,
		   { UTF_REPLACEMENT_CODE, 3 }, { 0x41, 1 });

	/* sequences truncated at the end of the buffer */
	ASSERT_SEQ("truncated E0", RB(0xE0), 1, { UTF_REPLACEMENT_CODE, 1 });
	ASSERT_SEQ("truncated E0 A0", RB(0xE0, 0xA0), 2,
		   { UTF_REPLACEMENT_CODE, 1 }, { UTF_REPLACEMENT_CODE, 1 });
	ASSERT_SEQ("truncated F0 90", RB(0xF0, 0x90), 2,
		   { UTF_REPLACEMENT_CODE, 1 }, { UTF_REPLACEMENT_CODE, 1 });
	ASSERT_SEQ("truncated F0 90 80", RB(0xF0, 0x90, 0x80), 3,
		   { UTF_REPLACEMENT_CODE, 1 }, { UTF_REPLACEMENT_CODE, 1 },
		   { UTF_REPLACEMENT_CODE, 1 });

	/* mixed valid and ill-formed, exercising resync across boundaries */
	ASSERT_SEQ("mixed valid/ill-formed",
		   RB(0x61, 0xF1, 0x80, 0x80, 0xE1, 0x80, 0xC2, 0x62, 0x80,
		      0x63, 0x80, 0xBF, 0x64),
		   13, { 0x61, 1 }, { UTF_REPLACEMENT_CODE, 3 },
		   { UTF_REPLACEMENT_CODE, 2 }, { UTF_REPLACEMENT_CODE, 1 },
		   { 0x62, 1 }, { UTF_REPLACEMENT_CODE, 1 }, { 0x63, 1 },
		   { UTF_REPLACEMENT_CODE, 1 }, { UTF_REPLACEMENT_CODE, 1 },
		   { 0x64, 1 });
}

END_TEST

/*
 * With check_valid=true the decoder also applies utf_is_valid() policy: it
 * rejects noncharacters, private-use and reserved scalars even though their
 * byte sequences are structurally well-formed.
 */
START_TEST(test_read_policy_rejects)
{
	assert_not_read("U+E000 private (EE 80 80)", RB(0xEE, 0x80, 0x80), 3,
			true, ESLURM_UTF_PRIVATE_CODE, 3);
	assert_not_read("U+FFFE noncharacter (EF BF BE)", RB(0xEF, 0xBF, 0xBE),
			3, true, ESLURM_UTF_NONCHARACTER_CODE, 3);
	assert_not_read("U+FFFF noncharacter (EF BF BF)", RB(0xEF, 0xBF, 0xBF),
			3, true, ESLURM_UTF_NONCHARACTER_CODE, 3);
	assert_not_read("U+FFF0 reserved (EF BF B0)", RB(0xEF, 0xBF, 0xB0), 3,
			true, ESLURM_UTF_RESERVED_CODE, 3);
	assert_not_read("U+10FFFF noncharacter (F4 8F BF BF)",
			RB(0xF4, 0x8F, 0xBF, 0xBF), 4, true,
			ESLURM_UTF_NONCHARACTER_CODE, 4);
	assert_not_read("U+100000 private (F4 80 80 80)",
			RB(0xF4, 0x80, 0x80, 0x80), 4, true,
			ESLURM_UTF_PRIVATE_CODE, 4);
}

END_TEST

/*
 * check_valid=false skips the utf_is_valid() policy and nothing else, and
 * nothing asserts on that path, so the mode returns whatever the byte pattern
 * decodes to -- including scalars the standard forbids.
 *
 * The reader has no structural surrogate or above-max check at all; it rejects
 * those solely via the policy call. So the mode is only observable in what it
 * ACCEPTS, which is what the second block below pins.
 */
START_TEST(test_read_no_check_valid)
{
	assert_read("U+0041 (no check)", RB(0x41), 1, false, 0x41, 1);
	assert_read("U+00F1 (no check)", RB(0xC3, 0xB1), 2, false, 0x00F1, 2);
	assert_read("U+20AC (no check)", RB(0xE2, 0x82, 0xAC), 3, false, 0x20AC,
		    3);
	assert_read("U+1F600 (no check)", RB(0xF0, 0x9F, 0x98, 0x80), 4, false,
		    0x1F600, 4);

	/*
	 * Policy-invalid but structurally parsable input passes straight
	 * through. Each of these is rejected when check_valid is set.
	 */
	assert_read("U+0000 NUL (no check)", RB(0x00), 1, false, 0x0, 1);
	assert_read("U+D800 surrogate (no check)", RB(0xED, 0xA0, 0x80), 3,
		    false, 0xD800, 3);
	assert_read("U+E000 private (no check)", RB(0xEE, 0x80, 0x80), 3, false,
		    0xE000, 3);
	assert_read("U+FFFF noncharacter (no check)", RB(0xEF, 0xBF, 0xBF), 3,
		    false, 0xFFFF, 3);
	assert_read("U+110000 above max (no check)", RB(0xF4, 0x90, 0x80, 0x80),
		    4, false, 0x110000, 4);
	assert_read("U+1FFFFF lead F7 (no check)", RB(0xF7, 0xBF, 0xBF, 0xBF),
		    4, false, 0x1FFFFF, 4);

	/* structural rejects are unconditional and still fire */
	assert_not_read("overlong C0 80 (no check)", RB(0xC0, 0x80), 2, false,
			ESLURM_UTF8_INVALID_READ, 2);
	assert_not_read("stray continuation 80 (no check)", RB(0x80), 1, false,
			ESLURM_UTF8_INVALID_READ, 1);
	assert_not_read("illegal lead F8 (no check)", RB(0xF8), 1, false,
			ESLURM_UTF8_INVALID_READ, 1);
	assert_not_read("truncated C3 (no check)", RB(0xC3), 1, false,
			ESLURM_UTF8_READ_ILLEGAL_TERMINATION, 1);
	assert_not_read("bad byte 2 (C2 41) (no check)", RB(0xC2, 0x41), 2,
			false, ESLURM_UTF8_INVALID_BYTE_2, 1);
}

END_TEST

/*
 * An empty range satisfies every xassert (src <= end, remaining >= 0) and is
 * the state a scanning loop reaches at string termination. It is also the only
 * path returning bytes = 0, so a caller advancing on bytes without checking rc
 * would spin.
 */
START_TEST(test_read_empty_range)
{
	const utf8_t *src = RB(0x41);
	utf_code_t utf = 0xDEAD;
	int bytes = -1;

	ck_assert_int_eq(utf8_read_character(src, src, &utf, &bytes, true),
			 EINVAL);
	ck_assert_int_eq(bytes, 0);
	ck_assert_int_eq(utf, 0);
}

END_TEST

/* The utf8_read_character() macro short-circuits the ASCII fast path. */
START_TEST(test_read_macro)
{
	const utf8_t *src;
	const utf8_t *end;
	utf_code_t utf = 0;
	int bytes = 0;

	src = RB(0x41);
	end = src + 1;
	ck_assert_int_eq(utf8_read_character(src, end, &utf, &bytes, true),
			 SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x41);
	ck_assert_int_eq(bytes, 1);

	/* non-ASCII delegates to slurm_read_utf8_character() */
	src = RB(0xE2, 0x82, 0xAC);
	end = src + 3;
	utf = 0;
	bytes = 0;
	ck_assert_int_eq(utf8_read_character(src, end, &utf, &bytes, true),
			 SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x20AC);
	ck_assert_int_eq(bytes, 3);
}

END_TEST

/* ----------------------------------------------- slurm_write_utf8_character */

START_TEST(test_write_valid)
{
	assert_write("U+0041 'A'", 0x41, RB(0x41), 1);
	assert_write("U+0080", 0x80, RB(0xC2, 0x80), 2);
	assert_write("U+07FF", 0x7FF, RB(0xDF, 0xBF), 2);
	assert_write("U+0800", 0x800, RB(0xE0, 0xA0, 0x80), 3);
	assert_write("U+20AC euro", 0x20AC, RB(0xE2, 0x82, 0xAC), 3);
	assert_write("U+FFFD replacement", 0xFFFD, RB(0xEF, 0xBF, 0xBD), 3);
	assert_write("U+10000", 0x10000, RB(0xF0, 0x90, 0x80, 0x80), 4);
	assert_write("U+1F600 emoji", 0x1F600, RB(0xF0, 0x9F, 0x98, 0x80), 4);
	assert_write("U+E0100 plane-14", 0xE0100, RB(0xF3, 0xA0, 0x84, 0x80),
		     4);
}

END_TEST

/* invalid input is replaced with U+FFFD (3 bytes), never emitted raw */
START_TEST(test_write_invalid_to_replacement)
{
	const utf8_t rep[] = { 0xEF, 0xBF, 0xBD }; /* U+FFFD */

	assert_write("U+0000 -> replacement", 0x0, rep, 3);
	assert_write("U+D800 surrogate -> replacement", 0xD800, rep, 3);
	assert_write("U+FFFE noncharacter -> replacement", 0xFFFE, rep, 3);
	assert_write("U+E000 private -> replacement", 0xE000, rep, 3);
	assert_write("U+10FFFF noncharacter -> replacement", 0x10FFFF, rep, 3);
	assert_write("U+110000 above max -> replacement", 0x110000, rep, 3);
	/*
	 * utf_code_t is unsigned, so -1 converts to 0xFFFFFFFF at the parameter
	 * boundary and is rejected for being above U+10FFFF, not for being
	 * negative.
	 */
	assert_write("0xFFFFFFFF -> replacement", -1, rep, 3);
}

END_TEST

/*
 * The utf8_write_character() macro handles ASCII without entering the function,
 * so reach the function's own ASCII branch by calling it directly. Safe despite
 * slurm_is_utf_valid() asserting non-ASCII: the function tests validity via the
 * utf_is_valid() macro, which short-circuits ASCII and never calls it.
 */
START_TEST(test_write_ascii_direct)
{
	utf8_t dst[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = 0;

	ck_assert_int_eq(slurm_write_utf8_character(0x41, dst, &bytes, true),
			 SLURM_SUCCESS);
	ck_assert_int_eq(bytes, 1);
	ck_assert_int_eq(dst[0], 0x41);

	ck_assert_int_eq(slurm_write_utf8_character(0x7F, dst, &bytes, true),
			 SLURM_SUCCESS);
	ck_assert_int_eq(bytes, 1);
	ck_assert_int_eq(dst[0], 0x7F);

	/* log=false must behave identically */
	ck_assert_int_eq(slurm_write_utf8_character(0x41, dst, &bytes, false),
			 SLURM_SUCCESS);
	ck_assert_int_eq(bytes, 1);
	ck_assert_int_eq(dst[0], 0x41);
}

END_TEST

/* ----------------------------------------------------- utf_is_valid() */

START_TEST(test_is_utf_valid)
{
	/* valid scalars across the byte-length ranges */
	ck_assert_int_eq(utf_is_valid(0x0001), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x007F), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x0080), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x07FF), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x0800), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0xD7FF), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0xFDCF), SLURM_SUCCESS); /* below FDD0 */
	ck_assert_int_eq(utf_is_valid(0xFDF0), SLURM_SUCCESS); /* above FDEF */
	ck_assert_int_eq(utf_is_valid(0xFEFF),
			 SLURM_SUCCESS); /* BOM is valid */
	ck_assert_int_eq(utf_is_valid(0xFFFD), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x10000), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0x1FFFD), SLURM_SUCCESS);
	ck_assert_int_eq(utf_is_valid(0xE0100), SLURM_SUCCESS);

	/* U+0000 and out-of-range */
	ck_assert_int_eq(utf_is_valid(0x0), ESLURM_UTF_NULL_CODE);
	/* unsigned utf_code_t: -1 converts to 0xFFFFFFFF, i.e. above U+10FFFF */
	ck_assert_int_eq(utf_is_valid(-1), ESLURM_UTF_INVALID_CODE);
	ck_assert_int_eq(utf_is_valid(0x110000), ESLURM_UTF_INVALID_CODE);

	/* surrogates */
	ck_assert_int_eq(utf_is_valid(0xD800), ESLURM_UTF16_SURROGATE_CODE);
	ck_assert_int_eq(utf_is_valid(0xDFFF), ESLURM_UTF16_SURROGATE_CODE);

	/* noncharacters: U+FDD0..U+FDEF and U+nFFFE/U+nFFFF for every plane */
	ck_assert_int_eq(utf_is_valid(0xFDD0), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0xFDEF), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0xFFFE), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0xFFFF), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0x1FFFE), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0x2FFFE), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0xFFFFE), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0x10FFFE), ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf_is_valid(0x10FFFF), ESLURM_UTF_NONCHARACTER_CODE);

	/* private-use: all three D49 ranges */
	ck_assert_int_eq(utf_is_valid(0xE000), ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf_is_valid(0xF8FF), ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf_is_valid(0xF0000), ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf_is_valid(0xFFFFD), ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf_is_valid(0x100000), ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf_is_valid(0x10FFFD), ESLURM_UTF_PRIVATE_CODE);

	/* reserved U+FFF0..U+FFF8 */
	ck_assert_int_eq(utf_is_valid(0xFFF0), ESLURM_UTF_RESERVED_CODE);
	ck_assert_int_eq(utf_is_valid(0xFFF8), ESLURM_UTF_RESERVED_CODE);
}

END_TEST

/* ----------------------------------------------------- utf16_to_coding() */

START_TEST(test_utf16_to_coding)
{
	utf_code_t utf = 0;

	/* BMP scalar (no low surrogate) */
	ck_assert_int_eq(utf16_to_coding(0x0041, 0, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x0041);
	ck_assert_int_eq(utf16_to_coding(0xD7FF, 0, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0xD7FF);

	/* valid surrogate pairs */
	ck_assert_int_eq(utf16_to_coding(0xD834, 0xDD1E, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x1D11E); /* G clef */
	ck_assert_int_eq(utf16_to_coding(0xD800, 0xDC00, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x10000); /* lowest supplementary */

	/* lone / mismatched surrogates */
	ck_assert_int_eq(utf16_to_coding(0xD800, 0, &utf),
			 ESLURM_UTF16_SURROGATE_CODE);
	ck_assert_int_eq(utf16_to_coding(0xDC00, 0, &utf),
			 ESLURM_UTF16_SURROGATE_CODE);
	ck_assert_int_eq(utf16_to_coding(0x0041, 0xDC00, &utf),
			 ESLURM_UTF16_SURROGATE_CODE);
	ck_assert_int_eq(utf16_to_coding(0xD800, 0x0041, &utf),
			 ESLURM_UTF16_SURROGATE_CODE);

	/*
	 * Pairs whose low-surrogate offset has its top bit set (v % 0x400 >=
	 * 0x200); the pairs above all have an offset below 0x200.
	 */
	ck_assert_int_eq(utf16_to_coding(0xD800, 0xDF00, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x10300);
	ck_assert_int_eq(utf16_to_coding(0xD83D, 0xDE00, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x1F600);

	/* a pair that resolves to a policy-rejected scalar */
	ck_assert_int_eq(utf16_to_coding(0xDBFF, 0xDFFF, &utf),
			 ESLURM_UTF_NONCHARACTER_CODE); /* U+10FFFF */
	ck_assert_int_eq(utf16_to_coding(0xDB80, 0xDC00, &utf),
			 ESLURM_UTF_PRIVATE_CODE); /* U+F0000 */
}

END_TEST

/* --------------------------------------------------- utf16_from_coding() */

START_TEST(test_utf16_from_coding)
{
	utf16_t high = 0, low = 0;
	utf_code_t utf = 0;

	/* BMP scalar (no low surrogate) */
	ck_assert_int_eq(utf16_from_coding(0x0041, &high, &low), SLURM_SUCCESS);
	ck_assert_int_eq(high, 0x0041);
	ck_assert_int_eq(low, 0);
	ck_assert_int_eq(utf16_from_coding(0xD7FF, &high, &low), SLURM_SUCCESS);
	ck_assert_int_eq(high, 0xD7FF);
	ck_assert_int_eq(low, 0);

	/* supplementary scalars split into surrogate pairs */
	ck_assert_int_eq(utf16_from_coding(0x1D11E, &high, &low),
			 SLURM_SUCCESS);
	ck_assert_int_eq(high, 0xD834); /* G clef */
	ck_assert_int_eq(low, 0xDD1E);
	ck_assert_int_eq(utf16_from_coding(0x10000, &high, &low),
			 SLURM_SUCCESS);
	ck_assert_int_eq(high, 0xD800); /* lowest supplementary */
	ck_assert_int_eq(low, 0xDC00);

	/*
	 * Scalars whose offset into the supplementary plane has bit 0x200 set;
	 * U+1D11E (0x11E) and U+10000 (0x000) both leave it clear.
	 */
	ck_assert_int_eq(utf16_from_coding(0x10300, &high, &low),
			 SLURM_SUCCESS);
	ck_assert_int_eq(high, 0xD800);
	ck_assert_int_eq(low, 0xDF00);
	ck_assert_int_eq(utf16_from_coding(0x1F600, &high, &low),
			 SLURM_SUCCESS);
	ck_assert_int_eq(high, 0xD83D);
	ck_assert_int_eq(low, 0xDE00);

	/* round-trip: from_coding() then to_coding() returns the scalar */
	ck_assert_int_eq(utf16_from_coding(0x1D11E, &high, &low),
			 SLURM_SUCCESS);
	ck_assert_int_eq(utf16_to_coding(high, low, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x1D11E);
	ck_assert_int_eq(utf16_from_coding(0x00E9, &high, &low), SLURM_SUCCESS);
	ck_assert_int_eq(utf16_to_coding(high, low, &utf), SLURM_SUCCESS);
	ck_assert_int_eq(utf, 0x00E9);

	/* invalid scalars are rejected with the utf_is_valid() error */
	ck_assert_int_eq(utf16_from_coding(0x0, &high, &low),
			 ESLURM_UTF_NULL_CODE);
	ck_assert_int_eq(utf16_from_coding(0xD800, &high, &low),
			 ESLURM_UTF16_SURROGATE_CODE);
	ck_assert_int_eq(utf16_from_coding(0xE000, &high, &low),
			 ESLURM_UTF_PRIVATE_CODE);
	ck_assert_int_eq(utf16_from_coding(0x10FFFF, &high, &low),
			 ESLURM_UTF_NONCHARACTER_CODE);
	ck_assert_int_eq(utf16_from_coding(0x110000, &high, &low),
			 ESLURM_UTF_INVALID_CODE);
}

END_TEST

/* --------------------------------------------- is_utf16 surrogate macros */

START_TEST(test_utf16_surrogate_macros)
{
	ck_assert(utf16_is_high_surrogate(0xD800));
	ck_assert(utf16_is_high_surrogate(0xDBFF));
	ck_assert(!utf16_is_high_surrogate(0xD7FF));
	ck_assert(!utf16_is_high_surrogate(0xDC00));

	ck_assert(utf16_is_low_surrogate(0xDC00));
	ck_assert(utf16_is_low_surrogate(0xDFFF));
	ck_assert(!utf16_is_low_surrogate(0xDBFF));
	ck_assert(!utf16_is_low_surrogate(0xE000));
}

END_TEST

/* ----------------------------------------------- utf_read_encoding_schema() */

START_TEST(test_read_encoding_schema)
{
	const utf8_t utf8[] = UTF8_BYTE_ORDER_MARK_SEQ;
	const utf8_t utf16be[] = UTF16BE_BYTE_ORDER_MARK_SEQ;
	const utf8_t utf16le[] = UTF16LE_BYTE_ORDER_MARK_SEQ;
	const utf8_t utf32be[] = UTF32BE_BYTE_ORDER_MARK_SEQ;
	const utf8_t utf32le[] = UTF32LE_BYTE_ORDER_MARK_SEQ;
	const utf8_t none[] = { 0x41, 0x42, 0x43, 0x44 };
	int bytes = -1;

	ck_assert_int_eq(utf_read_encoding_schema(utf8, utf8 + sizeof(utf8),
						  &bytes),
			 UTF_8_ENCODING);
	ck_assert_int_eq(bytes, sizeof(utf8));
	ck_assert_int_eq(utf_read_encoding_schema(utf16be,
						  utf16be + sizeof(utf16be),
						  &bytes),
			 UTF_16BE_ENCODING);
	ck_assert_int_eq(bytes, sizeof(utf16be));
	ck_assert_int_eq(utf_read_encoding_schema(utf16le,
						  utf16le + sizeof(utf16le),
						  &bytes),
			 UTF_16LE_ENCODING);
	ck_assert_int_eq(bytes, sizeof(utf16le));
	ck_assert_int_eq(utf_read_encoding_schema(utf32be,
						  utf32be + sizeof(utf32be),
						  &bytes),
			 UTF_32BE_ENCODING);
	ck_assert_int_eq(bytes, sizeof(utf32be));

	/*
	 * The UTF-32LE BOM (FF FE 00 00) begins with the UTF-16LE BOM (FF FE);
	 * it must still be detected as UTF-32LE.
	 */
	ck_assert_int_eq(utf_read_encoding_schema(utf32le,
						  utf32le + sizeof(utf32le),
						  &bytes),
			 UTF_32LE_ENCODING);
	ck_assert_int_eq(bytes, sizeof(utf32le));

	ck_assert_int_eq(utf_read_encoding_schema(none, none + sizeof(none),
						  &bytes),
			 UTF_UNKNOWN_ENCODING);
	ck_assert_int_eq(bytes, 0);

	/* too short to hold any BOM */
	ck_assert_int_eq(utf_read_encoding_schema(utf8, utf8, &bytes),
			 UTF_UNKNOWN_ENCODING);
	ck_assert_int_eq(bytes, 0);

	/*
	 * Buffers holding a proper prefix of a BOM, which exercise the
	 * sizeof(bom) <= bytes guards that keep each memcmp() inside the buffer.
	 */
	ck_assert_int_eq(utf_read_encoding_schema(utf8, utf8 + 1, &bytes),
			 UTF_UNKNOWN_ENCODING); /* EF */
	ck_assert_int_eq(bytes, 0);
	ck_assert_int_eq(utf_read_encoding_schema(utf8, utf8 + 2, &bytes),
			 UTF_UNKNOWN_ENCODING); /* EF BB */
	ck_assert_int_eq(bytes, 0);
	ck_assert_int_eq(utf_read_encoding_schema(utf32be, utf32be + 3, &bytes),
			 UTF_UNKNOWN_ENCODING); /* 00 00 FE */
	ck_assert_int_eq(bytes, 0);

	/*
	 * A 3-byte prefix of the UTF-32LE BOM is too short for UTF-32LE but is
	 * still a complete UTF-16LE BOM.
	 */
	ck_assert_int_eq(utf_read_encoding_schema(utf32le, utf32le + 3, &bytes),
			 UTF_16LE_ENCODING); /* FF FE 00 */
	ck_assert_int_eq(bytes, sizeof(utf16le));
}

END_TEST

/* ----------------------------------------------- classification macros */

START_TEST(test_is_utf8_newline)
{
	/* Unicode 5.8: CR, LF, VT, FF, NEL, LS, PS */
	ck_assert(utf8_is_newline(0x0A)); /* LF */
	ck_assert(utf8_is_newline(0x0B)); /* VT */
	ck_assert(utf8_is_newline(0x0C)); /* FF */
	ck_assert(utf8_is_newline(0x0D)); /* CR */
	ck_assert(utf8_is_newline(0x85)); /* NEL */
	ck_assert(utf8_is_newline(0x2028)); /* LS */
	ck_assert(utf8_is_newline(0x2029)); /* PS */

	/*
	 * Deliberate superset of 5.8: the macro also treats the C0 separators
	 * FS/GS/RS/US as newlines, so U+001C..U+001F are simultaneously newline
	 * and control (test_is_utf8_control).
	 */
	ck_assert(utf8_is_newline(0x1C)); /* FS */
	ck_assert(utf8_is_newline(0x1D)); /* GS */
	ck_assert(utf8_is_newline(0x1E)); /* RS */
	ck_assert(utf8_is_newline(0x1F)); /* US */

	ck_assert(!utf8_is_newline(0x41)); /* 'A' */
	ck_assert(!utf8_is_newline(0x20)); /* space */
	ck_assert(!utf8_is_newline(0x09)); /* tab */
	ck_assert(!utf8_is_newline(0x1B)); /* ESC, below the FS..US run */
	ck_assert(!utf8_is_newline(0x2027)); /* just below LS */
	ck_assert(!utf8_is_newline(0x202A)); /* just above PS */
}

END_TEST

START_TEST(test_is_utf8_space)
{
	/* Table 6-2 spaces (a representative span) */
	ck_assert(utf8_is_space(0x20)); /* space */
	ck_assert(utf8_is_space(0x09)); /* tab (treated as horizontal space) */
	ck_assert(utf8_is_space(0xA0)); /* no-break space */
	ck_assert(utf8_is_space(0x1680)); /* ogham space mark */
	ck_assert(utf8_is_space(0x2000)); /* en quad */
	ck_assert(utf8_is_space(0x200A)); /* hair space */
	ck_assert(utf8_is_space(0x202F)); /* narrow no-break space */
	ck_assert(utf8_is_space(0x205F)); /* medium mathematical space */
	ck_assert(utf8_is_space(0x3000)); /* ideographic space */

	/* deliberate superset of Table 6-2: these four are not spaces there */
	ck_assert(utf8_is_space(0x180E)); /* mongolian vowel separator */
	ck_assert(utf8_is_space(0x200B)); /* zero width space */
	ck_assert(utf8_is_space(0x2800)); /* braille pattern blank */
	ck_assert(utf8_is_space(0xFFA0)); /* halfwidth hangul filler */

	ck_assert(!utf8_is_space(0x41));
	ck_assert(!utf8_is_space(0x0A)); /* newline is not horizontal space */
	ck_assert(!utf8_is_space(0x200C)); /* ZWNJ: control, not space */
	ck_assert(!utf8_is_space(0x1FFF)); /* just below the en quad run */
}

END_TEST

START_TEST(test_is_utf8_control)
{
	/* C0 controls (minus tab/newlines), DEL, C1 controls */
	ck_assert(utf8_is_control(0x00));
	ck_assert(utf8_is_control(0x08));
	ck_assert(utf8_is_control(0x1F));
	ck_assert(utf8_is_control(0x7F)); /* DEL */
	ck_assert(utf8_is_control(0x80)); /* C1 */
	ck_assert(utf8_is_control(0x9F)); /* C1 */
	ck_assert(utf8_is_control(0x200C)); /* zero width non-joiner */

	ck_assert(utf8_is_control(0x0E)); /* SO, low end of 0x0E..0x1F */
	ck_assert(utf8_is_control(0x1C)); /* FS: control *and* newline */
	ck_assert(utf8_is_control(0x200D)); /* zero width joiner */
	ck_assert(utf8_is_control(0x2060)); /* word joiner */
	ck_assert(utf8_is_control(0x206F)); /* nominal digit shapes */
	ck_assert(utf8_is_control(0xFFF9)); /* interlinear annotation anchor */
	ck_assert(utf8_is_control(0xFFFB)); /* interlinear annotation term. */
	ck_assert(utf8_is_control(0x34F)); /* combining grapheme joiner */
	ck_assert(utf8_is_control(0x61C)); /* arabic letter mark */
	ck_assert(utf8_is_control(0x115F)); /* hangul jamo block */
	ck_assert(utf8_is_control(0x1160)); /* hangul jungseong filler */
	ck_assert(utf8_is_control(0x3164)); /* hangul filler */
	ck_assert(utf8_is_control(0xE0001)); /* language tag (deprecated) */

	ck_assert(!utf8_is_control(0x09)); /* tab */
	ck_assert(!utf8_is_control(0x0A)); /* newline */
	ck_assert(!utf8_is_control(0x41)); /* 'A' */
	ck_assert(!utf8_is_control(0x20)); /* space */
	ck_assert(!utf8_is_control(0xA0)); /* NBSP is a space */
	ck_assert(!utf8_is_control(0x0D)); /* CR is a newline */
}

END_TEST

START_TEST(test_is_utf8_whitespace)
{
	ck_assert(utf8_is_whitespace(0x20)); /* space */
	ck_assert(utf8_is_whitespace(0x09)); /* tab */
	ck_assert(utf8_is_whitespace(0x0A)); /* newline */
	ck_assert(utf8_is_whitespace(0x3000)); /* ideographic space */
	ck_assert(utf8_is_whitespace(0x2028)); /* line separator */

	ck_assert(utf8_is_whitespace(0x1C)); /* newline half of the union */
	ck_assert(utf8_is_whitespace(0x180E)); /* space half of the union */

	ck_assert(!utf8_is_whitespace(0x41));
	ck_assert(!utf8_is_whitespace(0x00));
	ck_assert(!utf8_is_whitespace(0x200C)); /* control, neither half */
}

END_TEST

/* --------------------------------------------------- utf8_get_loggable() */

START_TEST(test_get_utf8_loggable)
{
	/* Control Pictures substitutions */
	ck_assert_int_eq(utf8_get_loggable(0x00), 0x2400); /* NUL */
	ck_assert_int_eq(utf8_get_loggable(0x0A), 0x240A); /* LF */
	ck_assert_int_eq(utf8_get_loggable(0x1F), 0x241F); /* US */
	ck_assert_int_eq(utf8_get_loggable(0x20), 0x2420); /* SPACE */
	ck_assert_int_eq(utf8_get_loggable(0x7F), 0x2421); /* DEL */
	ck_assert_int_eq(utf8_get_loggable(0x85), UTF_RETURN_SYMBOL_CODE);

	/*
	 * Bidi controls. An unsubstituted LRO/RLO in a log line reorders
	 * everything after it, which is the Trojan-Source log-spoofing vector.
	 * They are also the only table entries past U+0085, so they guard the
	 * table staying sorted: the lookup breaks as soon as an entry exceeds
	 * the code, so a reordering drops them to the control fallback below.
	 */
	ck_assert_int_eq(utf8_get_loggable(0x200E), 0x2AAA); /* LRM */
	ck_assert_int_eq(utf8_get_loggable(0x200F), 0x2AAB); /* RLM */
	ck_assert_int_eq(utf8_get_loggable(0x202A), 0x2AAA); /* LRE */
	ck_assert_int_eq(utf8_get_loggable(0x202B), 0x2AAB); /* RLE */
	ck_assert_int_eq(utf8_get_loggable(0x202C), 0x2AA4); /* PDF */
	ck_assert_int_eq(utf8_get_loggable(0x202D), 0x2AAA); /* LRO */
	ck_assert_int_eq(utf8_get_loggable(0x202E), 0x2AAB); /* RLO */

	/*
	 * The fallbacks for codes with no table entry. These are the only
	 * producers of their respective constants.
	 */
	ck_assert_int_eq(utf8_get_loggable(0x2028), UTF_RETURN_SYMBOL_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x2029), UTF_RETURN_SYMBOL_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x00A0), UTF_SPACE_REPLACEMENT_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x3000), UTF_SPACE_REPLACEMENT_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x0080),
			 UTF_CONTROL_REPLACEMENT_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x009F),
			 UTF_CONTROL_REPLACEMENT_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x200C),
			 UTF_CONTROL_REPLACEMENT_CODE);

	/* a normal printable scalar is returned unchanged */
	ck_assert_int_eq(utf8_get_loggable(0x41), 0x41);
	ck_assert_int_eq(utf8_get_loggable(0x20AC), 0x20AC);

	/* an invalid scalar that is not in the substitution table -> U+FFFD */
	ck_assert_int_eq(utf8_get_loggable(0xD800), UTF_REPLACEMENT_CODE);
	ck_assert_int_eq(utf8_get_loggable(0x110000), UTF_REPLACEMENT_CODE);
}

END_TEST

/* -------------------------------------- utf_encoding_scheme_to_string */

START_TEST(test_encoding_scheme_to_string)
{
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_8_ENCODING),
			   "UTF-8"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_16BE_ENCODING),
			   "UTF-16BE"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_16LE_ENCODING),
			   "UTF-16LE"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_32BE_ENCODING),
			   "UTF-32BE"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_32LE_ENCODING),
			   "UTF-32LE"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_UNKNOWN_ENCODING),
			   "UNKNOWN"));
	/* the lookup falls through to xassert(false) if a row goes missing */
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_INVALID),
			   "INVALID"));
	ck_assert(!xstrcmp(utf_encoding_scheme_to_string(UTF_INVALID_MAX),
			   "INVALID"));
}

END_TEST

/* --------------------------------------------------- string wrappers */

START_TEST(test_utf8_string_wrappers)
{
	/* U+00E9 (2 bytes) followed by 'a': 3 bytes, 2 characters */
	const utf8_t src[] = { 0xC3, 0xA9, 'a', 0 };
	const utf8_t ascii[] = { 'a', 'b', 'c', 0 };
	utf8_t *dup = utf8_dup(src);
	utf8_t *ndup = utf8_ndup(src, 2);
	utf8_t *split = utf8_ndup(src, 1);
	utf8_t *adup = utf8_dup(ascii);

	/* all three wrappers count BYTES, not characters */
	ck_assert_int_eq(utf8_strlen(src), 3);
	ck_assert_int_eq(utf8_strlen(ascii), 3);

	ck_assert_ptr_nonnull(dup);
	ck_assert_int_eq(utf8_strlen(dup), 3);
	ck_assert(!memcmp(dup, src, 4));

	/* n counts bytes, so this keeps the whole 2-byte character */
	ck_assert_ptr_nonnull(ndup);
	ck_assert_int_eq(utf8_strlen(ndup), 2);
	ck_assert_int_eq(ndup[0], 0xC3);
	ck_assert_int_eq(ndup[1], 0xA9);

	/* n does not character-align: this truncates mid-character */
	ck_assert_ptr_nonnull(split);
	ck_assert_int_eq(utf8_strlen(split), 1);
	ck_assert_int_eq(split[0], 0xC3);

	ck_assert(adup && !xstrcmp((const char *) adup, "abc"));

	xfree(dup);
	xfree(ndup);
	xfree(split);
	xfree(adup);
}

END_TEST

/* --------------------------------------------------- codespace capstone */

/*
 * Exhaustive completeness check across the entire U+0001..U+10FFFF codespace:
 * every code point is encoded and decoded back. Valid scalars must round-trip
 * exactly; invalid scalars must be encoded as the U+FFFD replacement, which
 * itself decodes back to U+FFFD.
 */
START_TEST(test_codespace_roundtrip)
{
	long valid = 0, invalid = 0;

	for (utf_code_t cp = 1; cp <= 0x10FFFF; cp++) {
		utf8_t buf[UTF8_CHAR_MAX_BYTES] = { 0 };
		int wbytes = 0, rbytes = 0;
		utf_code_t rcode = 0;
		int vrc = utf_is_valid(cp);

		ck_assert_int_eq(utf8_write_character(cp, buf, &wbytes),
				 SLURM_SUCCESS);
		ck_assert_msg(utf8_read_character(buf, buf + wbytes, &rcode,
						  &rbytes,
						  true) == SLURM_SUCCESS,
			      "readback failed for U+%06" PRIX32, cp);
		ck_assert_int_eq(rbytes, wbytes);

		if (!vrc) {
			ck_assert_msg(rcode == cp,
				      "roundtrip U+%06" PRIX32
				      " -> U+%06" PRIX32,
				      cp, rcode);
			valid++;
		} else {
			ck_assert_msg(rcode == UTF_REPLACEMENT_CODE,
				      "invalid U+%06" PRIX32
				      " not replaced (got U+%06" PRIX32 ")",
				      cp, rcode);
			invalid++;
		}
	}

	printf("codespace roundtrip: %ld valid, %ld invalid scalars\n", valid,
	       invalid);

	/*
	 * The round-trip above classifies each code point with the same
	 * utf_is_valid() the reader uses, so on its own it holds under any
	 * policy, right or wrong. These totals are what make the sweep an
	 * assertion about the policy itself, and they are exact:
	 *
	 *	  2048 surrogates	U+D800..U+DFFF
	 *	    66 noncharacters	U+FDD0..U+FDEF (32) + U+nFFFE/U+nFFFF
	 *				for planes 0..16 (34)
	 *	137468 private use	U+E000..U+F8FF (6400)
	 *				+ U+F0000..U+FFFFD (65534)
	 *				+ U+100000..U+10FFFD (65534)
	 *	     9 reserved		U+FFF0..U+FFF8
	 *	------
	 *	139591 invalid, and 1114111 - 139591 = 974520 valid.
	 *
	 * The ranges do not overlap: U+nFFFE/U+nFFFF sit above the end of every
	 * private-use range, and U+FFF0..U+FFF8 is disjoint from both.
	 */
	ck_assert_int_eq(invalid, 139591);
	ck_assert_int_eq(valid, 974520);
	ck_assert_int_eq(valid + invalid, 0x10FFFF);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	TCase *tcase = tcase_create("xutf");
	Suite *suite = suite_create("xutf");
	SRunner *sr = NULL;

	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_set_timeout(tcase, 120);

	tcase_add_test(tcase, test_read_ascii);
	tcase_add_test(tcase, test_read_two_byte);
	tcase_add_test(tcase, test_read_three_byte);
	tcase_add_test(tcase, test_read_four_byte);
	tcase_add_test(tcase, test_read_overlong);
	tcase_add_test(tcase, test_read_surrogate_bytes);
	tcase_add_test(tcase, test_read_above_max);
	tcase_add_test(tcase, test_read_out_of_range_lead);
	tcase_add_test(tcase, test_read_illegal_lead);
	tcase_add_test(tcase, test_read_truncated);
	tcase_add_test(tcase, test_read_maximal_subpart);
	tcase_add_test(tcase, test_read_policy_rejects);
	tcase_add_test(tcase, test_read_no_check_valid);
	tcase_add_test(tcase, test_read_empty_range);
	tcase_add_test(tcase, test_read_macro);
	tcase_add_test(tcase, test_write_valid);
	tcase_add_test(tcase, test_write_invalid_to_replacement);
	tcase_add_test(tcase, test_write_ascii_direct);
	tcase_add_test(tcase, test_is_utf_valid);
	tcase_add_test(tcase, test_utf16_to_coding);
	tcase_add_test(tcase, test_utf16_from_coding);
	tcase_add_test(tcase, test_utf16_surrogate_macros);
	tcase_add_test(tcase, test_read_encoding_schema);
	tcase_add_test(tcase, test_is_utf8_newline);
	tcase_add_test(tcase, test_is_utf8_space);
	tcase_add_test(tcase, test_is_utf8_control);
	tcase_add_test(tcase, test_is_utf8_whitespace);
	tcase_add_test(tcase, test_get_utf8_loggable);
	tcase_add_test(tcase, test_encoding_scheme_to_string);
	tcase_add_test(tcase, test_utf8_string_wrappers);
	tcase_add_test(tcase, test_codespace_roundtrip);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
