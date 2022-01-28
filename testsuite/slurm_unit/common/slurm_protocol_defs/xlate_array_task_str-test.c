/*****************************************************************************\
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

START_TEST(null_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(array_task_str == NULL);
	ck_assert(array_bitmap == NULL);

	array_task_str = xstrdup("");
	array_bitmap = bit_alloc(1);
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(array_task_str[0] == '\0');
	ck_assert(array_bitmap == NULL);

	/* Test must have 0x at front */
	array_task_str = xstrdup("h");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "h"));
	ck_assert(array_bitmap == NULL);

	array_task_str = xstrdup("hello");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "hello"));
	ck_assert(array_bitmap == NULL);
}
END_TEST

START_TEST(good_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	array_task_str = xstrdup("0x7");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "0-2"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "0-2"));

	array_task_str = xstrdup("0x9C6");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "1-2,6-8,11"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "1-2,6-8,11"));

	array_task_str = xstrdup("0x9C6");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "1-2,6-8,11%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "1-2,6-8,11"));

	/* Max task count */
	array_task_str = xstrdup("0x9C6");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "1-2,6-8,11%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "1-2,6-8,11"));

	/* Stepped task */
	array_task_str = xstrdup("0x55554");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "2-18:2%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "2,4,6,8,10,12,14,16,18"));

	/* Broken up stepped task */
	array_task_str = xstrdup("0x45174");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "2,4-6,8,12,14,18%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "2,4-6,8,12,14,18"));

	/* test with array_bitmap -- frees task_bitmap */
	array_task_str = xstrdup("0x55154");
	xlate_array_task_str(&array_task_str, 9, NULL);
	ck_assert(!xstrcmp(array_task_str, "2,4,6,8,12,14,16,18%9"));

}
END_TEST

START_TEST(BITSTR_LEN_no_max_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	/* SLURM_BITSTR_LEN */
	setenv("SLURM_BITSTR_LEN", "10", 1);
	array_task_str = xstrdup("0x55154");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "2,4,6,..."));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "2,4,6,8,12,14,16,18"));
}
END_TEST

START_TEST(BITSTR_LEN_with_max_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	/* SLURM_BITSTR_LEN */
	setenv("SLURM_BITSTR_LEN", "10", 1);
	array_task_str = xstrdup("0x55154");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "2,4,6,...%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "2,4,6,8,12,14,16,18"));
}
END_TEST

START_TEST(BITSTR_LEN_negative_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	setenv("SLURM_BITSTR_LEN", "-1", 1);
	array_task_str = xstrdup("0x5555555555155");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44..."));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50"));
}
END_TEST

START_TEST(BITSTR_LEN_negative_max_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	setenv("SLURM_BITSTR_LEN", "-1", 1);
	array_task_str = xstrdup("0x5555555555155");
	xlate_array_task_str(&array_task_str, 9, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44...%9"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50"));
}
END_TEST

START_TEST(BITSTR_LEN_65_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	setenv("SLURM_BITSTR_LEN", "65", 1);
	array_task_str = xstrdup("0x5555555555155");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,..."));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50"));
}
END_TEST

START_TEST(BITSTR_LEN_0_test)
{
	bitstr_t *array_bitmap = NULL;
	char *array_task_str = NULL;

	setenv("SLURM_BITSTR_LEN", "0", 1);
	array_task_str = xstrdup("0x5555555555155");
	xlate_array_task_str(&array_task_str, 0, &array_bitmap);
	ck_assert(!xstrcmp(array_task_str, "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50"));
	ck_assert(!xstrcmp(bit_fmt_full(array_bitmap), "0,2,4,6,8,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50"));
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("bit_unfmt_hexmask test");
	TCase *tc_core = tcase_create("Testing bit_unfmt_hexmask");

	tcase_add_test(tc_core, null_test);
	tcase_add_test(tc_core, good_test);

	/* has to be run in forked mode because bitstr_len is a static */
	tcase_add_test(tc_core, BITSTR_LEN_no_max_test);
	tcase_add_test(tc_core, BITSTR_LEN_with_max_test);
	tcase_add_test(tc_core, BITSTR_LEN_negative_test);
	tcase_add_test(tc_core, BITSTR_LEN_negative_max_test);
	tcase_add_test(tc_core, BITSTR_LEN_65_test);
	tcase_add_test(tc_core, BITSTR_LEN_0_test);

	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(suite());

	//srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
