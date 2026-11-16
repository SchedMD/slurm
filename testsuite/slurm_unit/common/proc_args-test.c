/*****************************************************************************\
 *  proc_args-test.c - unit test for memory size helpers in proc_args.c
 *****************************************************************************
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

#include <check.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "src/common/proc_args.h"
#include "src/common/xmalloc.h"

/*
 * mbytes_to_str() output is what slurm_option_get() returns for --mem,
 * --mem-per-cpu, --mem-per-gpu and --tmp, so it has to parse back to the
 * same value with str_to_mbytes().
 */
static void _check_mbytes_str(uint64_t mbytes, const char *expected)
{
	char *str = mbytes_to_str(mbytes);

	ck_assert_msg(str, "mbytes_to_str(%" PRIu64 ") returned NULL", mbytes);
	ck_assert_str_eq(str, expected);
	ck_assert_msg(str_to_mbytes(str) == mbytes,
		      "str_to_mbytes(\"%s\") != %" PRIu64, str, mbytes);
	xfree(str);
}

START_TEST(test_mbytes_to_str)
{
	ck_assert_msg(!mbytes_to_str(NO_VAL64), "NO_VAL64 must give NULL");

	/* --mem=0 requests all of the memory on each node */
	_check_mbytes_str(0, "0");

	_check_mbytes_str(1, "1");
	_check_mbytes_str(1023, "1023");
	_check_mbytes_str(1024, "1G");
	_check_mbytes_str(1536, "1536");
	_check_mbytes_str(3 * 1024, "3G");
	_check_mbytes_str(1024 * 1024, "1T");
}
END_TEST

START_TEST(test_str_to_mbytes)
{
	ck_assert_msg(str_to_mbytes("0") == 0, "\"0\"");
	ck_assert_msg(str_to_mbytes("0G") == 0, "\"0G\"");
	ck_assert_msg(str_to_mbytes("100") == 100, "\"100\"");
	ck_assert_msg(str_to_mbytes("1K") == 1, "\"1K\"");
	ck_assert_msg(str_to_mbytes("2G") == 2048, "\"2G\"");
	ck_assert_msg(str_to_mbytes("2GB") == 2048, "\"2GB\"");
	ck_assert_msg(str_to_mbytes("1T") == 1024 * 1024, "\"1T\"");
	ck_assert_msg(str_to_mbytes("-1") == NO_VAL64, "\"-1\"");
	ck_assert_msg(str_to_mbytes("1X") == NO_VAL64, "\"1X\"");
	ck_assert_msg(str_to_mbytes("") == NO_VAL64, "\"\"");
}
END_TEST

Suite *proc_args_suite(void)
{
	Suite *s = suite_create("proc_args");
	TCase *tc_core = tcase_create("proc_args");
	tcase_add_test(tc_core, test_mbytes_to_str);
	tcase_add_test(tc_core, test_str_to_mbytes);
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(proc_args_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
