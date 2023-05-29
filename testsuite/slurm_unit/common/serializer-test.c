/*****************************************************************************\
 *  Copyright (C) 2023 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <check.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#include "./serializer-test.data.c"

START_TEST(test_parse)
{
	/* should fail */
	static const char *sf[] = {
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
		"[]]",
		"{}}",
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
		"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[",
		"{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:test}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}",
		"{\"taco\"::taco}",
		"{::taco}",
		"\xFE",
		"\xFF",
		"\xFE\xFF",
		"\xFF\xFE",
		/* "\x00\x00\xFE\xFF", - can't test this with cstrings */
		"\xFE\xFF\x00\x00",
		"\xFEtaco",
		"\xFFtaco",
		"\xFE\xFFtaco",
		"\xFF\xFEtaco",
		/* "\x00\x00\xFE\xFFtaco", - can't test this with cstrings */
		"\xFE\xFF\x00\x00taco",
		"\x01",
		"\x02",
		"\x03",
		"\x04",
		"\x05",
		"\x06",
		"\x07",
		"\x08",
		"\"taco\"\"",
		"\"\"\"",
		"\"\"taco\"",
		"\"\"\"\"",
		"\\u10FFFF",
		"\\u10FFFFFFFFFFFFFFFFFFFFFFF",
		"\\u0",
		"\\uTACOS",
		"\\u000TACOS",
		"tacos\"tacos\"taco\"\"",
		"*\"tacos\"taco\"\"",
		"*,0",
	};
	/* should parse */
	static const char *s[] = {
		"\"taco\"",
		"taco",
		"100",
		"100.389",
		"-100.389",
		"1.1238e10",
		"-1.1238e10",
		"{ \"taco\": \"tacos\" }",
		"[ \"taco1\", \"taco2\", ]",
		"[ ,\"taco1\", \"taco2\", \"taco3\",,,,, ]",
		"[ true, false, Infinity, inf, +inf, -inf, -Infinity, +Infinity, nan, null, ~ ]",
		"{\t\t\t\n\"dict\": \t\r\n\n\n\n\n\n\n"
			"[//this is a comment\n"
				"{\"true\": TRUE, \"false\t\n\": FALSE},\t  \t     "
				"{    \"inf\": [ INFINITY, INF, +inf, -inf, -INFINITY, -INFINITY ]}   \t\t\t,"
				"{"
					"\"nan\": { \"nan\": [-NaN, +NaN, NaN]},"
					"\"number0\": 0,"
					"\"number1\": 1,"
					"\"true\": true,"
					"\"NULL\": [NULL, ~]"
				"}, "
				"{ \"items\": "
					"{"
						"\"taco\": \"taco\", "
						"\"some numbers\t\": ["
							"100, 12342.22, -232.22, +32.2323, 1e10, -1e10, 1121.3422e3, -3223.33e121"
						"]"
					"}, empty: [], empty2: {}\t\n\r\n"
				"}"
			"]/* this is also a comment */"
		"}/*******[],{}///********/\n\n\n\t\r\n\t\t\t\n\n\n",
		"{ taco: \"\\u0074\\u0061\\u0063\\u006f\\u0073\", taco_key_unquoted_222:\t\t\ttaco_unquoted_1, test_hex:0x1028242322, 0x82382232: \"hex tacos ffeb=\\uffeb\" }",
		"[ \"\\u0024\", \"\\u00a3\", \"\\u00c0\", \"\\u0418\", \"\\u0939\", \"\\u20ac\", \"\\ud55c\", \"\\u10348\", \"\\uE01EF\" ]",
		" ",
		"null",
		"{}",
		"[]",
		"[[]   \t]",
		"[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]",
		"[{test:test,,,,,,,,,,,,,,,,,,,,,,,,,,,}]",
		"{test:[]}",
		"{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:test}}}}}}}}}}}}}}}}}}}}}}}}}}",
	};
	data_t *c[] = {
		data_set_string(data_new(), "taco"),
		data_set_string(data_new(), "taco"),
		data_set_int(data_new(), 100),
		data_set_float(data_new(), 100.389),
		data_set_float(data_new(), -100.389),
		data_set_float(data_new(), 1.1238e10),
		data_set_float(data_new(), -1.1238e10),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_new(),
		data_new(),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
	};

	data_set_string(data_key_set(c[7], "taco"), "tacos");

	data_set_string(data_list_append(c[8]), "taco1");
	data_set_string(data_list_append(c[8]), "taco2");

	data_set_string(data_list_append(c[9]), "taco1");
	data_set_string(data_list_append(c[9]), "taco2");
	data_set_string(data_list_append(c[9]), "taco3");

	data_set_bool(data_list_append(c[10]), true);
	data_set_bool(data_list_append(c[10]), false);
	data_set_float(data_list_append(c[10]), INFINITY);
	data_set_float(data_list_append(c[10]), INFINITY);
	data_set_float(data_list_append(c[10]), INFINITY);
	data_set_float(data_list_append(c[10]), -INFINITY);
	data_set_float(data_list_append(c[10]), -INFINITY);
	data_set_float(data_list_append(c[10]), -INFINITY);
	data_set_float(data_list_append(c[10]), NAN);
	data_set_null(data_list_append(c[10]));
	data_set_null(data_list_append(c[10]));

	{
		data_t *d, *d2, *d3, *d4;
		d = data_set_list(data_key_set(c[11], "dict"));
		d2 = data_set_dict(data_list_append(d));
		data_set_bool(data_key_set(d2, "true"), true);
		data_set_bool(data_key_set(d2, "false\t\n"), false);
		d2 = data_set_dict(data_list_append(d));
		d3 = data_set_list(data_key_set(d2, "inf"));
		data_set_float(data_list_append(d3), INFINITY);
		data_set_float(data_list_append(d3), INFINITY);
		data_set_float(data_list_append(d3), INFINITY);
		data_set_float(data_list_append(d3), -INFINITY);
		data_set_float(data_list_append(d3), -INFINITY);
		data_set_float(data_list_append(d3), -INFINITY);
		d2 = data_set_dict(data_list_append(d));
		d3 = data_set_dict(data_key_set(d2, "nan"));
		d4 = data_set_list(data_key_set(d3, "nan"));
		data_set_float(data_list_append(d4), -NAN);
		data_set_float(data_list_append(d4), NAN);
		data_set_float(data_list_append(d4), NAN);
		data_set_int(data_set_dict(data_key_set(d2, "number0")), 0);
		data_set_int(data_set_dict(data_key_set(d2, "number1")), 1);
		data_set_bool(data_set_dict(data_key_set(d2, "true")), true);
		d3 = data_set_list(data_key_set(d2, "NULL"));
		data_set_null(data_list_append(d3));
		data_set_null(data_list_append(d3));
		d2 = data_set_dict(data_list_append(d));
		d3 = data_set_dict(data_key_set(d2, "items"));
		data_set_string(data_key_set(d3, "taco"), "taco");
		d4 = data_set_list(data_key_set(d3, "some numbers\t"));
		data_set_int(data_list_append(d4), 100);
		data_set_float(data_list_append(d4), 12342.22);
		data_set_float(data_list_append(d4), -232.22);
		data_set_float(data_list_append(d4), 32.2323);
		data_set_float(data_list_append(d4), 1e10);
		data_set_float(data_list_append(d4), -1e10);
		data_set_float(data_list_append(d4), 1121.3422e3);
		data_set_float(data_list_append(d4), -3223.33e121);
		data_set_list(data_key_set(d2, "empty"));
		data_set_dict(data_key_set(d2, "empty2"));
	}

	data_set_string(data_key_set(c[12], "taco"), "tacos");
	data_set_string(data_key_set(c[12], "taco_key_unquoted_222"),
			"taco_unquoted_1");
	data_set_int(data_key_set(c[12], "test_hex"), 0x1028242322);
	data_set_string(data_key_set(c[12], "2184716850"),
			"hex tacos ffeb=\uffeb");

	data_set_string(data_list_append(c[13]), "\U00000024");
	data_set_string(data_list_append(c[13]), "\U000000a3");
	data_set_string(data_list_append(c[13]), "\U000000c0");
	data_set_string(data_list_append(c[13]), "\U00000418");
	data_set_string(data_list_append(c[13]), "\U00000939");
	data_set_string(data_list_append(c[13]), "\U000020ac");
	data_set_string(data_list_append(c[13]), "\U0000d55c");
	data_set_string(data_list_append(c[13]), "\U00010348");
	data_set_string(data_list_append(c[13]), "\U000E01EF");

	data_set_list(data_list_append(c[18]));

	{
		data_t *t = c[19];
		for (int i = 0; i < 19; i++)
			t = data_set_list(data_list_append(t));
	}

	data_set_string(data_key_set(data_set_dict(data_list_append(c[20])), "test"), "test");

	data_set_list(data_key_set(c[21], "test"));

	{
		data_t *t = c[22];
		for (int i = 0; i < 26; i++)
			t = data_set_dict(data_key_set(t, "test"));

		data_set_string(t, "test");
	}

	for (int i = 0; i < ARRAY_SIZE(sf); i++) {
		int rc;
		data_t *d = NULL;

		debug("source:\n%s\n\n\n\n", sf[i]);

		rc = serialize_g_string_to_data(&d, sf[i], strlen(sf[i]),
						MIME_TYPE_JSON);
		ck_assert_int_ne(rc, 0);

		FREE_NULL_DATA(d);
	}

	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		data_t *d = NULL;
		char *str = NULL;
		size_t len;

		debug("source:\n%s\n\n\n\n", s[i]);

		ck_assert_int_eq(
			serialize_g_string_to_data(&d, s[i], strlen(s[i]),
						   MIME_TYPE_JSON), 0);

		data_convert_tree(d, DATA_TYPE_NONE);
		ck_assert_msg(data_check_match(c[i], d, false),
			      "verify failed: %s", s[i]);

		ck_assert_msg(!serialize_g_data_to_string(&str, &len, d,
							  MIME_TYPE_JSON,
							  SER_FLAGS_PRETTY),
			      "dump failed: %s", s[i]);

		debug("source:\n%s\n\n\n\n", s[i]);
		debug("dumped:\n%s\n\n\n\n", str);

		FREE_NULL_DATA(d);

		ck_assert_int_eq(serialize_g_string_to_data(&d, str,
							    strlen(str),
							    MIME_TYPE_JSON),
				 0);

		ck_assert_msg(data_check_match(c[i], d, false),
			      "reserialize match failed: %s", s[i]);

		FREE_NULL_DATA(d);
		FREE_NULL_DATA(c[i]);
		xfree(str);
	}
}
END_TEST

START_TEST(test_mimetype)
{
	ck_assert(resolve_mime_type(MIME_TYPE_JSON));
}
END_TEST


START_TEST(test_bandwidth)
{
#define TEST_BW_RUN_COUNT 250
	DEF_TIMERS;
	int rc;
	data_t *data = NULL;
	const int test_json_len = strlen(test_json);
	char *output = NULL;
	size_t output_len = 0;
	uint64_t read_times = 0, write_times = 0;
	uint64_t total_written = 0, total_read = 0;
	double read_avg, write_avg;
	double read_diff, write_diff, read_rate, write_rate;
	double read_rate_bytes, write_rate_bytes;

	for (int i = 0; i < TEST_BW_RUN_COUNT; i++) {
		FREE_NULL_DATA(data);

		START_TIMER;
		rc = serialize_g_string_to_data(&data, test_json,
						test_json_len, MIME_TYPE_JSON);
		END_TIMER3(__func__, INFINITE);

		total_read += test_json_len;
		read_times += DELTA_TIMER;

		ck_assert_int_eq(rc, 0);
	}

	if (true)
	for (int i = 0; i < TEST_BW_RUN_COUNT; i++) {
		START_TIMER;
		rc = serialize_g_data_to_string(&output, &output_len, data,
						MIME_TYPE_JSON,
						SER_FLAGS_PRETTY);
		END_TIMER3(__func__, INFINITE);

		total_written += output_len;
		write_times += DELTA_TIMER;

		xfree(output);
		output_len = 0;

		ck_assert_int_eq(rc, 0);
	}

	FREE_NULL_DATA(data);

	read_diff = read_times / TEST_BW_RUN_COUNT;
	write_diff = write_times / TEST_BW_RUN_COUNT;

	read_avg = total_read / TEST_BW_RUN_COUNT;
	write_avg = total_written / TEST_BW_RUN_COUNT;

	/* (bytes / usec) * (1000000 usec / 1 sec) * (1 MiB / 1024*1024 bytes) */
	read_rate_bytes = (read_avg  / read_diff) * NSEC_IN_MSEC;
	write_rate_bytes = (write_avg / write_diff) * NSEC_IN_MSEC;

	read_rate = read_rate_bytes / 1048576;
	write_rate = write_rate_bytes / 1048576;

	printf("\n\navg per %u runs:\n\tread=%f usec\n\twrite=%f usec\n\n",
	       TEST_BW_RUN_COUNT, read_diff, write_diff);

	printf("\tread=%f MiB/sec \n\twrite=%f MiB/sec\n\n",
	       read_rate, write_rate);
}
END_TEST

START_TEST(test_compliance)
{
	int rc;
	data_t *data = NULL, *verify_data = NULL;
	const int len = strlen(test_json);
	char *output = NULL;
	size_t output_len = -1;

	debug("source:\n%s\n\n\n\n", test_json);

	rc = serialize_g_string_to_data(&data, test_json, len, MIME_TYPE_JSON);
	ck_assert_int_eq(rc, 0);

	rc = serialize_g_data_to_string(&output, &output_len, data,
					MIME_TYPE_JSON, SER_FLAGS_PRETTY);
	ck_assert_int_eq(rc, 0);

	debug("dumped:\n%s\n\n\n\n", output);

	rc = serialize_g_string_to_data(&verify_data , output, output_len,
					MIME_TYPE_JSON);
	ck_assert_int_eq(rc, 0);

	ck_assert_msg(data_check_match(data, verify_data, false),
		      "match verification failed");

	FREE_NULL_DATA(data);
	FREE_NULL_DATA(verify_data);
}
END_TEST

extern Suite *suite_data(void)
{
	Suite *s = suite_create("Serializer");
	TCase *tc_core = tcase_create("Serializer");

	/* we are abusing JSON so tests will take a while */
	tcase_set_timeout(tc_core, 3000);

	if (true) {
		tcase_add_test(tc_core, test_mimetype);
		tcase_add_test(tc_core, test_parse);
		tcase_add_test(tc_core, test_compliance);
	}
	if (true) {
		tcase_add_test(tc_core, test_bandwidth);
	}

	suite_add_tcase(s, tc_core);
	return s;
}

extern int main(void)
{
	int number_failed, fd, rc;
	char *slurm_unit_conf_filename;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	const char slurm_unit_conf_content[] =
		"ClusterName=slurm_unit\n"
		"PluginDir=" SLURM_PREFIX "/lib/slurm/\n"
		"SlurmctldHost=slurm_unit\n";
	const size_t csize = sizeof(slurm_unit_conf_content);

	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("serializer-test", log_opts, 0, NULL);

	/* Call slurm_init() with a mock slurm.conf*/
	slurm_unit_conf_filename = xstrdup("slurm_unit.conf-XXXXXX");
	if ((fd = mkstemp(slurm_unit_conf_filename)) == -1) {
		error("error creating slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	} else {
		debug("fake slurm.conf created: %s", slurm_unit_conf_filename);
	}

	/*
	 * PluginDir=. is needed as loading the slurm.conf will check for the
	 * existence of the dir. As 'make check' doesn't install anything the
	 * normal PluginDir might not exist. As we don't load any plugins for
	 * these test this should be ok.
	 */
	rc = write(fd, slurm_unit_conf_content, csize);
	if (rc < csize) {
		error("error writing slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	}

	/* Do not load any plugins, we are only testing slurm_opt */
	if (slurm_conf_init(slurm_unit_conf_filename)) {
		error("slurm_conf_init() failed");
		return EXIT_FAILURE;
	}

	unlink(slurm_unit_conf_filename);
	xfree(slurm_unit_conf_filename);
	close(fd);

	if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL)) {
		error("serializer_g_init() failed");
		return EXIT_FAILURE;
	}

	SRunner *sr = srunner_create(suite_data());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	serializer_g_fini();
	slurm_fini();
	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
