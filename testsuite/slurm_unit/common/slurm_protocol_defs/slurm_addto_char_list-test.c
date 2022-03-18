/*****************************************************************************\
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Jeff DeGraw <jeff@schedmd.com>
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

#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

START_TEST(easy)
{
	int count;
	char *names = "hi,this,that";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 3);
	ck_assert_str_eq((char*)list_next(itr), "hi");
	ck_assert_str_eq((char*)list_next(itr), "this");
	ck_assert_str_eq((char*)list_next(itr), "that");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(commas_at_end)
{
	int count;
	char *names = "hi,this,that,,,,,,,,,,,,,,,,,,,,,,,,";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 3);
	ck_assert_str_eq((char*)list_next(itr), "hi");
	ck_assert_str_eq((char*)list_next(itr), "this");
	ck_assert_str_eq((char*)list_next(itr), "that");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(commas_at_start)
{
	int count;
	char *names = ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,hi,this,that";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 3);
	ck_assert_str_eq((char*)list_next(itr), "hi");
	ck_assert_str_eq((char*)list_next(itr), "this");
	ck_assert_str_eq((char*)list_next(itr), "that");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(commas_between)
{
	int count;
	char *names = "hi,,,,,,,this,,,,,,,,that";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 3);
	ck_assert_str_eq((char*)list_next(itr), "hi");
	ck_assert_str_eq((char*)list_next(itr), "this");
	ck_assert_str_eq((char*)list_next(itr), "that");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(null_list)
{
	int count;
	char *names = "hi,this,that";
	List char_list = NULL;

	count = slurm_addto_char_list(char_list, names);
	ck_assert_int_eq(count, 0);

	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(brackets) {
	int count;
	char *names = "hi[0-2],,hi[2-3],,this";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 5);
	ck_assert_str_eq((char*)list_next(itr), "hi0");
	ck_assert_str_eq((char*)list_next(itr), "hi1");
	ck_assert_str_eq((char*)list_next(itr), "hi2");
	ck_assert_str_eq((char*)list_next(itr), "hi3");
	ck_assert_str_eq((char*)list_next(itr), "this");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(duplicates) {
	int count;
	char *names = "hi,hi,hi,hi";
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 1);
	ck_assert_str_eq((char*)list_next(itr), "hi");

	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(spaces) {
	int count;
	char *names = "hi, this, that";
	List char_list = list_create(xfree_ptr);

	count = slurm_addto_char_list(char_list, names);
	ck_assert_int_eq(count, 3);

	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(quotes)
{
	int count;
	char *names = xstrdup("\"hi,this\'");
	List char_list = list_create(xfree_ptr);
	ListIterator itr;

	count = slurm_addto_char_list(char_list, names);
	itr = list_iterator_create(char_list);
	ck_assert_int_eq(count, 2);
	ck_assert_str_eq((char*)list_next(itr), "hi");
	ck_assert_str_eq((char*)list_next(itr), "this`");

	xfree(names);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(char_list);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("Slurm addto_char_list_t");
	TCase *tc_core = tcase_create("Slurm addto_char_list_t");
	tcase_add_test(tc_core, easy);
	tcase_add_test(tc_core, commas_at_end);
	tcase_add_test(tc_core, commas_at_start);
	tcase_add_test(tc_core, commas_between);
	tcase_add_test(tc_core, null_list);
	tcase_add_test(tc_core, brackets);
	tcase_add_test(tc_core, duplicates);
	tcase_add_test(tc_core, spaces);
	tcase_add_test(tc_core, quotes);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(NULL);
	//srunner_set_fork_status(sr, CK_NOFORK);
	srunner_add_suite(sr, suite(sr));

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
