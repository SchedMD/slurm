/*****************************************************************************\
 *  Copyright (C) 2019 SchedMD LLC
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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <check.h>

#include <src/common/xmalloc.h>
#include <src/common/xstring.h>

typedef struct {
	const char *input;
	const char *expected;
} xstrtrim_data_t;

static xstrtrim_data_t xstrtrim_data[] = {
	{"",""},
	{" a ", "a"},
	{"", ""},
	{" a ", "a"},
	{"   ", ""},
	{"	   ", ""},
	/* test with spaces */
	{" aaaaaaaa ", "aaaaaaaa"},
	{"  aaaaaaaa ", "aaaaaaaa"},
	{"  aaaaaaaa  ", "aaaaaaaa"},
	{" aaaaaaaa  ", "aaaaaaaa"},
	{"           aaaaaaaa ", "aaaaaaaa"},
	{"           aaaaaaaa           ", "aaaaaaaa"},
	{"aaaaaaaa           ", "aaaaaaaa"},
	{"aaaaaaaa", "aaaaaaaa"},
	{"aa   aa  aa   aa", "aa   aa  aa   aa"},
	{"      aa   aa  aa   aa", "aa   aa  aa   aa"},
	{"      aa   aa  aa   aa       ", "aa   aa  aa   aa"},
	{"aa   aa  aa   aa       ", "aa   aa  aa   aa"},
	/* test with spaces and tabs */
	{"	", ""},
	{"  	", ""},
	{"  	  ", ""},
	{"  	  	", ""},
	{"	aaaaaaaa	", "aaaaaaaa"},
	{"           	aaaaaaaa	", "aaaaaaaa"},
	{"           	aaaaaaaa           	", "aaaaaaaa"},
	{"aaaaaaaa	           ", "aaaaaaaa"},
	{"aaaaaaaa", "aaaaaaaa"},
	{"aa   	aa  aa   	aa", "aa   	aa  aa   	aa"},
	{"      	aa   aa  	 aa   	aa", "aa   aa  	 aa   	aa"},
	{"	      aa   aa  aa   aa       	", "aa   aa  aa   aa"},
	{"aa   aa  aa   aa       	", "aa   aa  aa   aa"},
	{"aa   aa  aa   aa       	", "aa   aa  aa   aa"}
};

/* check xstrtrim against given expected xstrtrim result */
START_TEST(test_xstrtrim)
{
	const char *input = xstrtrim_data[_i].input;
	const char *expected = xstrtrim_data[_i].expected;
	char *buf = xstrdup(input);

	xstrtrim(buf);
	ck_assert_msg(strcmp(buf, expected) == 0, "check xstrtrim: \"%s\" -> \"%s\" == \"%s\"",
		      input, buf, expected);

	xfree(buf);
}
END_TEST

Suite *xstring_suite(void)
{
	Suite *s = suite_create("xstring");
	TCase *tc_core = tcase_create("Core");
	tcase_add_loop_test(tc_core, test_xstrtrim, 0 , sizeof(xstrtrim_data) /
			    sizeof(xstrtrim_data_t) );
	suite_add_tcase(s, tc_core);
	return s;
}

int main(int argc, char *argv[])
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("xstring-test", log_opts, 0, NULL);

	int number_failed;
	SRunner *sr = srunner_create(xstring_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
