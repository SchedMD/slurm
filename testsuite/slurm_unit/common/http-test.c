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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <check.h>

#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/read_config.h"

static void _test_schema(const char *str, const url_scheme_t scheme)
{
	url_scheme_t parsed = URL_SCHEME_INVALID;
	const char *dump = NULL;

	/* verify parsed scheme matches the expected */
	ck_assert(!url_get_scheme(str, strlen(str), &parsed));
	ck_assert_int_eq(scheme, parsed);

	/* verify dumping string and parsing again gets same result */
	dump = url_get_scheme_string(scheme);
	ck_assert(!url_get_scheme(dump, strlen(dump), &parsed));
	ck_assert_int_eq(parsed, scheme);
}

static void _test_schema_fail(const char *str)
{
	url_scheme_t scheme = URL_SCHEME_INVALID;
	ck_assert(url_get_scheme(str, strlen(str), &scheme));
}

START_TEST(test_url_scheme)
{
	url_scheme_t scheme;

	_test_schema("http", URL_SCHEME_HTTP);
	_test_schema("https", URL_SCHEME_HTTPS);

	ck_assert(url_get_scheme("\0\0\0fail", 5, &scheme));

	_test_schema_fail("invalid");
	_test_schema_fail("web+invalid");
	_test_schema_fail("web+web+http");
	_test_schema_fail("invalid+web");
	_test_schema_fail("http://localhost/tacos");
	_test_schema_fail("https://localhost/tacos");
}

END_TEST

Suite *suite_http(void)
{
	Suite *s = suite_create("HTTP");
	TCase *tc_core = tcase_create("HTTP");

	tcase_add_test(tc_core, test_url_scheme);

	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("http-test", log_opts, 0, NULL);

	SRunner *sr = srunner_create(suite_http());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
