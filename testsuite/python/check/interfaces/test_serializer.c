/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*
 * This is a port of the old slurm_unit serializer-test.c. It runs under the
 * test_100_1 pytest meta-runner (compiled and run by atf.run_check_test() as a
 * single translation unit, so the large JSON data blobs are #include'd rather
 * than linked).
 *
 * It tests the serializer/json plugin for round-trip correctness.
 */

#define _GNU_SOURCE
#include <limits.h>

#if defined(__GLIBC__) && !defined(__UCLIBC__) && !defined(__MUSL__)
#include <features.h>
#if defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33) && defined(__SIZEOF_INT128__)
#define HAVE_MALLINFO2
#include <malloc.h>
#endif
#endif
#endif

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#include "public_datasets/twitter.json.c"
#include "public_datasets/noaa_global_temps.json.c"

#define BYTES_IN_MiB (1024 * 1024)

#ifdef HAVE_MALLINFO2

typedef unsigned __int128 uint128_t;

typedef struct {
	uint128_t arena; /* non-mmapped space allocated from system */
	uint128_t ordblks; /* number of free chunks */
	uint128_t smblks; /* number of fastbin blocks */
	uint128_t hblks; /* number of mmapped regions */
	uint128_t hblkhd; /* space in mmapped regions */
	uint128_t usmblks; /* always 0, preserved for backwards compatibility */
	uint128_t fsmblks; /* space available in freed fastbin blocks */
	uint128_t uordblks; /* total allocated space */
	uint128_t fordblks; /* total free space */
	uint128_t keepcost; /* top-most, releasable (via malloc_trim) space */
} mallinfo2_128_t;

typedef struct {
	struct mallinfo2 peak;
	mallinfo2_128_t total;
	int count;
} mem_track_t;

#endif /* HAVE_MALLINFO2 */

static const char *mime_types[] = {
	MIME_TYPE_YAML,
	MIME_TYPE_JSON,
};

static const serializer_flags_t flag_combinations[] = {
	SER_FLAGS_COMPACT,
	SER_FLAGS_PRETTY,
};

static const struct {
	const char *source;
	const char *tag;
	const int run_count; /* diff count to avoid test running too long */
} test_json[] = { { test_json1, "twitter-dataset", 25 },
		  { test_json2, "NOAA-ocean-temps", 50 } };

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("serializer-test", log_opts, 0, NULL);

	ck_assert(!slurm_conf_init(NULL));

	/*
	 * Load the serializer plugins on demand. The bound serializer for
	 * application/json is whichever plugin in PluginDir registers it; the
	 * test environment provides serializer/json (asserted by test_mimetype).
	 */
	serializer_required(MIME_TYPE_JSON);
	serializer_required(MIME_TYPE_YAML);
}

static void teardown(void)
{
	serializer_g_fini();
	slurm_conf_destroy();
	log_fini();
}

/* serialize src to a string, re-parse it, and verify the round-trip matches */
static void _test_run(const char *tag, const data_t *src, const char *mime_type,
		      const serializer_flags_t flags)
{
	char *output = NULL;
	size_t output_len = -1;
	data_t *verify_src = NULL;
	int rc;

	rc = serialize_g_data_to_string(&output, &output_len, src, mime_type,
					flags);
	ck_assert_int_eq(rc, 0);

	debug("dumped %s with %s:\n%s\n\n\n\n", tag, mime_type, output);

	rc = serialize_g_string_to_data(&verify_src, output, output_len,
					mime_type);
	ck_assert_int_eq(rc, 0);

	ck_assert_msg(data_check_match(src, verify_src, false),
		      "match verification failed");

	xfree(output);
	FREE_NULL_DATA(verify_src);
}

START_TEST(test_mimetype)
{
	const char *ptr = NULL;

	ck_assert(resolve_mime_type(MIME_TYPE_JSON, &ptr) != NULL);
	ck_assert(ptr != NULL);
	ck_assert(!xstrcmp(ptr, MIME_TYPE_JSON_PLUGIN));

	ptr = NULL;
	ck_assert(resolve_mime_type("application/jsonrequest", &ptr) != NULL);
	ck_assert(ptr != NULL);
	ck_assert(!xstrcmp(ptr, MIME_TYPE_JSON_PLUGIN));
}

END_TEST

START_TEST(test_parse_invalid)
{
	/* malformed JSON that the parser must reject */
	static const char
		*sf[] = {
			"\"taco",
			"taco\"",
			"[",
			"]",
			"{",
			"}",
			"[{",
			"{[",
			"{[}",
			"[{}",
			"[\"taco",
			"{\"taco",
			"{\"taco:",
			"{taco:",
			"{\"taco\":",
			"[taco:",
			"[\"taco\":",
			"[\"taco\",:",
			",,,,]",
			",:,,]",
			"\\,",
			":",
			",:,",
			"\"\\\"",
			"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[",
			"{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:test}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}",
			"{\"taco\"::taco}",
			"{::taco}",
			"\xFE",
			"\xFF",
			"\xFE\xFF",
			"\xFF\xFE",
			"\xFE\xFF\x00\x00",
			"\xFEtaco",
			"\xFFtaco",
			"\xFE\xFFtaco",
			"\xFF\xFEtaco",
			"\xFE\xFF\x00\x00taco",
			"\x01",
			"\x02",
			"\x03",
			"\x04",
			"\x05",
			"\x06",
			"\x07",
			"\x08",
			"\\u10FFFF",
			"\\u10FFFFFFFFFFFFFFFFFFFFFFF",
			"\\u0",
			"\\uTACOS",
			"\\u000TACOS",
			"tacos\"tacos\"taco\"\"",
			"*\"tacos\"taco\"\"",
			"*,0",
		};

	for (int i = 0; i < ARRAY_SIZE(sf); i++) {
		int rc;
		data_t *d = NULL;

		rc = serialize_g_string_to_data(&d, sf[i], strlen(sf[i]),
						MIME_TYPE_JSON);
		debug("expected fail source %d=%d -> %pD\n%s\n\n\n\n", i, rc, d,
		      sf[i]);
		ck_assert_ptr_null(d);

		FREE_NULL_DATA(d);
	}
}

END_TEST

START_TEST(test_parse_valid)
{
	/* valid JSON that must parse, match, and round-trip */
	static const char *s[] = {
		"\"taco\"",
		"\"\\\"taco\\\"\"",
		"[ 100 ]",
		"[ 100.389 ]",
		"[ -100.389 ]",
		"[ 1.1238e10 ]",
		"[ -1.1238e10 ]",
		"{ \"taco\": \"tacos\" }",
		"[ \"taco1\", \"taco2\", ]",
		"[ \"taco1\", \"taco2\", \"taco3\" ]",
		"[ true, false ]",
		"{\t\t\t\n}",
		"{ }",
		"[ \"\\u0024\", \"\\u00a3\", \"\\u00c0\", \"\\u0418\", \"\\u0939\", \"\\u20ac\", \"\\ud55c\" ]",
		"[]",
		"[]",
		"{}",
		"[]",
		"[[]   \t]",
		"[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]",
		"[{\"test\":\"test\"}]",
		"{\"test\":[]}",
		"{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":\"test\"}}}}}}}}}}}}}}}}}}}}}}}}}}",
	};
	data_t *c[] = {
		data_set_string(data_new(), "taco"),
		data_set_string(data_new(), "\"taco\""),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
	};

	data_set_int(data_list_append(c[2]), 100);
	data_set_float(data_list_append(c[3]), 100.389);
	data_set_float(data_list_append(c[4]), -100.389);
	data_set_float(data_list_append(c[5]), 1.1238e10);
	data_set_float(data_list_append(c[6]), -1.1238e10);

	data_set_string(data_key_set(c[7], "taco"), "tacos");

	data_set_string(data_list_append(c[8]), "taco1");
	data_set_string(data_list_append(c[8]), "taco2");

	data_set_string(data_list_append(c[9]), "taco1");
	data_set_string(data_list_append(c[9]), "taco2");
	data_set_string(data_list_append(c[9]), "taco3");

	data_set_bool(data_list_append(c[10]), true);
	data_set_bool(data_list_append(c[10]), false);

	data_set_string(data_list_append(c[13]), "\U00000024");
	data_set_string(data_list_append(c[13]), "\U000000a3");
	data_set_string(data_list_append(c[13]), "\U000000c0");
	data_set_string(data_list_append(c[13]), "\U00000418");
	data_set_string(data_list_append(c[13]), "\U00000939");
	data_set_string(data_list_append(c[13]), "\U000020ac");
	data_set_string(data_list_append(c[13]), "\U0000d55c");

	data_set_list(data_list_append(c[18]));

	{
		data_t *t = c[19];
		for (int i = 0; i < 19; i++)
			t = data_set_list(data_list_append(t));
	}

	data_set_string(data_key_set(data_set_dict(data_list_append(c[20])),
				     "test"),
			"test");

	data_set_list(data_key_set(c[21], "test"));

	{
		data_t *t = c[22];
		for (int i = 0; i < 26; i++)
			t = data_set_dict(data_key_set(t, "test"));

		data_set_string(t, "test");
	}

	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		int rc;
		data_t *d = NULL;

		rc = serialize_g_string_to_data(&d, s[i], strlen(s[i]),
						MIME_TYPE_JSON);
		debug("expected pass source %d=%d -> %pD\n%s\n\n\n\n", i, rc, d,
		      s[i]);
		ck_assert_int_eq(rc, 0);

		data_convert_tree(d, DATA_TYPE_NONE);
		ck_assert_msg(data_check_match(c[i], d, false),
			      "verify failed: %s", s[i]);

		for (int f = 0; f < ARRAY_SIZE(flag_combinations); f++) {
			for (int m = 0; m < ARRAY_SIZE(mime_types); m++) {
				const char *mptr = NULL;
				const char *mime_type =
					resolve_mime_type(mime_types[m], &mptr);

				if (mime_type)
					_test_run(s[i], d, mime_type,
						  flag_combinations[f]);
				else
					debug("skipping test with %s",
					      mime_types[m]);
			}
		}

		FREE_NULL_DATA(d);
	}

	for (int i = 0; i < ARRAY_SIZE(c); i++)
		FREE_NULL_DATA(c[i]);
}

END_TEST

START_TEST(test_compliance_large)
{
	/*
	 * Verify that the same JSON file can be parsed and dumped with same
	 * contents.
	 */
	for (int i = 0; i < ARRAY_SIZE(test_json); i++) {
		int rc;
		data_t *data = NULL;
		const int len = strlen(test_json[i].source);

		debug("source %s:\n%s\n\n\n\n", test_json[i].tag,
		      test_json[i].source);

		rc = serialize_g_string_to_data(&data, test_json[i].source, len,
						MIME_TYPE_JSON);
		ck_assert_int_eq(rc, 0);

		for (int f = 0; f < ARRAY_SIZE(flag_combinations); f++) {
			for (int m = 0; m < ARRAY_SIZE(mime_types); m++) {
				const char *mptr = NULL;
				const char *mime_type =
					resolve_mime_type(mime_types[m], &mptr);

				if (mime_type)
					_test_run(test_json[i].tag, data,
						  mime_type,
						  flag_combinations[f]);
				else
					debug("skipping test with %s",
					      mime_types[m]);
			}
		}

		FREE_NULL_DATA(data);
	}
}

END_TEST

#ifdef HAVE_MALLINFO2
static void _track_mem(mem_track_t *track)
{
	struct mallinfo2 mi = mallinfo2();

	track->peak.arena = MAX(mi.arena, track->peak.arena);
	track->peak.ordblks = MAX(mi.ordblks, track->peak.ordblks);
	track->peak.hblks = MAX(mi.hblks, track->peak.hblks);
	track->peak.hblkhd = MAX(mi.hblkhd, track->peak.hblkhd);
	track->peak.usmblks = MAX(mi.usmblks, track->peak.usmblks);
	track->peak.fsmblks = MAX(mi.fsmblks, track->peak.fsmblks);
	track->peak.uordblks = MAX(mi.uordblks, track->peak.uordblks);
	track->peak.fordblks = MAX(mi.fordblks, track->peak.fordblks);
	track->peak.keepcost = MAX(mi.keepcost, track->peak.keepcost);

	track->total.arena += mi.arena;
	track->total.ordblks += mi.ordblks;
	track->total.hblks += mi.hblks;
	track->total.hblkhd += mi.hblkhd;
	track->total.usmblks += mi.usmblks;
	track->total.fsmblks += mi.fsmblks;
	track->total.uordblks += mi.uordblks;
	track->total.fordblks += mi.fordblks;
	track->total.keepcost += mi.keepcost;

	track->count++;
}

static void _print_tracked_mem(mem_track_t *track, const char *type)
{
	struct mallinfo2 avg = {
		.arena = track->total.arena / track->count,
		.ordblks = track->total.ordblks / track->count,
		.hblks = track->total.hblks / track->count,
		.hblkhd = track->total.hblkhd / track->count,
		.usmblks = track->total.usmblks / track->count,
		.fsmblks = track->total.fsmblks / track->count,
		.uordblks = track->total.uordblks / track->count,
		.fordblks = track->total.fordblks / track->count,
		.keepcost = track->total.keepcost / track->count,
	};

	printf("\t%s Total non-mmapped bytes (arena):       %zu/%zuB %zu/%zuMiB\n",
	       type, avg.arena, track->peak.arena, (avg.arena / 1048576),
	       (track->peak.arena / 1048576));
	printf("\t%s # of free chunks (ordblks):            %zu/%zu\n", type,
	       avg.ordblks, track->peak.ordblks);
	printf("\t%s # of free fastbin blocks (smblks):     %zu/%zu\n", type,
	       avg.smblks, track->peak.smblks);
	printf("\t%s # of mapped regions (hblks):           %zu/%zu\n", type,
	       avg.hblks, track->peak.hblks);
	printf("\t%s Bytes in mapped regions (hblkhd):      %zu/%zu\n", type,
	       avg.hblkhd, track->peak.hblkhd);
	printf("\t%s Max. total allocated space (usmblks):  %zu/%zu\n", type,
	       avg.usmblks, track->peak.usmblks);
	printf("\t%s Free bytes held in fastbins (fsmblks): %zu/%zu\n", type,
	       avg.fsmblks, track->peak.fsmblks);
	printf("\t%s Total allocated space (uordblks):      %zu/%zu\n", type,
	       avg.uordblks, track->peak.uordblks);
	printf("\t%s Total free space (fordblks):           %zu/%zu\n", type,
	       avg.fordblks, track->peak.fordblks);
	printf("\t%s Topmost releasable block (keepcost):   %zu/%zu\n\n", type,
	       avg.keepcost, track->peak.keepcost);
}
#else /* !HAVE_MALLINFO2 */
/* avoid compile errors for undefined/unused variables */
typedef struct {
	int a;
	int b;
} mem_track_t;

#define _track_mem(track) ((void) (track))
#define _print_tracked_mem(track, type) ((void) (track))
#endif /* !HAVE_MALLINFO2 */

static void _test_bandwidth_str(const char *tag, const char *source,
				const int run_count)
{
	int rc;
	data_t *data = NULL;
	const int test_json_len = strlen(source);
	char *output = NULL;
	size_t output_len = 0;
	timespec_t read_times = { 0, 0 }, write_times = { 0, 0 };
	uint64_t total_written = 0, total_read = 0;
	timespec_t fastest_write = { UINT_MAX, 0 };
	timespec_t fastest_read = { UINT_MAX, 0 };
	double read_avg, write_avg;
	double read_diff, write_diff, read_rate, write_rate;
	double read_rate_bytes, write_rate_bytes;
	double fastest_read_rate_bytes, fastest_write_rate_bytes;
	double fastest_read_rate, fastest_write_rate;
	mem_track_t read_mem = { 0 }, write_mem = { 0 };

	for (int i = 0; i < run_count; i++) {
		DEF_TIMERS;
		timespec_t duration = { 0, 0 };

		FREE_NULL_DATA(data);

		_track_mem(&read_mem);

		START_TIMER;
		rc = serialize_g_string_to_data(&data, source, test_json_len,
						MIME_TYPE_JSON);
		END_TIMER3(__func__, INFINITE);
		duration = timespec_diff_ns(TIMER_END_TS, TIMER_START_TS).diff;

		_track_mem(&read_mem);

		total_read += test_json_len;
		read_times = timespec_add(read_times, duration);

		if (timespec_is_after(fastest_read, duration))
			fastest_read = duration;

		ck_assert_int_eq(rc, 0);
	}

	for (int i = 0; i < run_count; i++) {
		DEF_TIMERS;
		timespec_t duration = { 0, 0 };

		_track_mem(&write_mem);

		START_TIMER;
		rc = serialize_g_data_to_string(&output, &output_len, data,
						MIME_TYPE_JSON,
						SER_FLAGS_PRETTY);
		END_TIMER3(__func__, INFINITE);
		duration = timespec_diff_ns(TIMER_END_TS, TIMER_START_TS).diff;

		_track_mem(&write_mem);

		total_written += output_len;
		write_times = timespec_add(write_times, duration);

		if (timespec_is_after(fastest_write, duration))
			fastest_write = duration;

		xfree(output);
		output_len = 0;

		ck_assert_int_eq(rc, 0);
	}

	FREE_NULL_DATA(data);

	read_diff = timespec_to_secs(read_times) / run_count;
	write_diff = timespec_to_secs(write_times) / run_count;

	read_avg = total_read / run_count;
	write_avg = total_written / run_count;

	/* (bytes / sec) * (1 MiB / 1024*1024 bytes) */
	read_rate_bytes = (read_avg / read_diff);
	write_rate_bytes = (write_avg / write_diff);
	fastest_read_rate_bytes = read_avg / timespec_to_secs(fastest_read);
	fastest_write_rate_bytes = write_avg / timespec_to_secs(fastest_write);

	read_rate = read_rate_bytes / BYTES_IN_MiB;
	write_rate = write_rate_bytes / BYTES_IN_MiB;
	fastest_read_rate = fastest_read_rate_bytes / BYTES_IN_MiB;
	fastest_write_rate = fastest_write_rate_bytes / BYTES_IN_MiB;

	printf("%s: %u runs:\n", tag, run_count);

	printf("\tfastest read=%lf sec\n\tfastest write=%lf sec\n\n",
	       timespec_to_secs(fastest_read), timespec_to_secs(fastest_write));

	printf("\tfastest read=%f MiB/sec \n\tfastest write=%f MiB/sec\n\n",
	       fastest_read_rate, fastest_write_rate);

	printf("\tavg read=%lf sec\n\tavg write=%lf sec\n\n", read_diff,
	       write_diff);

	printf("\tavg read=%f MiB/sec \n\tavg write=%f MiB/sec\n\n", read_rate,
	       write_rate);

	_print_tracked_mem(&read_mem, "read");
	_print_tracked_mem(&write_mem, "write");
}

START_TEST(test_bandwidth)
{
	for (int i = 0; i < ARRAY_SIZE(test_json); i++)
		_test_bandwidth_str(test_json[i].tag, test_json[i].source,
				    test_json[i].run_count);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	TCase *tcase = tcase_create("serializer");
	Suite *suite = suite_create("serializer");
	SRunner *sr = NULL;

	tcase_add_unchecked_fixture(tcase, setup, teardown);
	/* generous timeout: the bandwidth and large-dataset cases run a while */
	tcase_set_timeout(tcase, 3000);

	tcase_add_test(tcase, test_mimetype);
	tcase_add_test(tcase, test_parse_invalid);
	tcase_add_test(tcase, test_parse_valid);
	tcase_add_test(tcase, test_compliance_large);
	tcase_add_test(tcase, test_bandwidth);

	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
