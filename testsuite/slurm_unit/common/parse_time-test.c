/*****************************************************************************\
 *  parse_time-test.c - unit test for parse_time.c
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Chad Vizino <chad@schedmd.com>
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

#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "src/common/parse_time.h"

START_TEST(test_time_str2secs)
{
	ck_assert_int_eq(time_str2secs(NULL), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs(""), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("INVALID TIME"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("-1"), (int)INFINITE);
	ck_assert_int_eq(time_str2secs("INFINITE"), (int)INFINITE);
	ck_assert_int_eq(time_str2secs("infinite"), (int)INFINITE);
	ck_assert_int_eq(time_str2secs("UNLIMITED"), (int)INFINITE);
	ck_assert_int_eq(time_str2secs("unlimited"), (int)INFINITE);
	ck_assert_int_eq(time_str2secs("LONG --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- INVALID TIME"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("0"), 0);
	ck_assert_int_eq(time_str2secs("60"), 60*60);
	ck_assert_int_eq(time_str2secs("60:15"), 60*60 + 15);
	ck_assert_int_eq(time_str2secs("60:0"), 60*60);
	ck_assert_int_eq(time_str2secs("60:"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("60:-10"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("-60:10"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("1:60:15"), 1*60*60 + 60*60 + 15);
	ck_assert_int_eq(time_str2secs("2:60:15"), 2*60*60 + 60*60 + 15);
	ck_assert_int_eq(time_str2secs("0:0:15"), 15);
	ck_assert_int_eq(time_str2secs("0:60:0"), 60*60);
	ck_assert_int_eq(time_str2secs("0:0:0"), 0);
	ck_assert_int_eq(time_str2secs("-0:-0:-0"), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs(" 0:0:0 "), (int)NO_VAL);
	ck_assert_int_eq(time_str2secs("0-1:60:15"), 1*60*60 + 60*60 + 15);
	ck_assert_int_eq(time_str2secs("1-1:60:15"), 1*60*60*24 + 1*60*60 + 60*60 + 15);
	ck_assert_int_eq(time_str2secs("365-1:60:15"), 365*60*60*24 + 1*60*60 + 60*60 + 15);
	ck_assert_int_eq(time_str2secs("365-0:0:0"), 365*60*60*24);
}
END_TEST

Suite *parse_time_suite(void)
{
	Suite *s = suite_create("parse_time");
	TCase *tc_core = tcase_create("parse_time");
	tcase_add_test(tc_core, test_time_str2secs);
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(parse_time_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
