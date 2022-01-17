/*****************************************************************************\
 *  Copyright (C) 2020 SchedMD LLC
 *  Written by Scott Hilton <scott@schedmd.com>
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
#include <stdlib.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xmalloc.h"

#define QOS_LIST_SIZE 3
char *qos_names[QOS_LIST_SIZE] = {"normal", "subpar", "great"};

List setup_qos_list(){
	slurmdb_qos_rec_t *qos = NULL;

	/* slurmdb_destroy_qos_rec not needed all pointers are local strings */
	List qos_list = list_create(xfree_ptr);

	for (int i = 0; i < QOS_LIST_SIZE; i++){
		qos = xmalloc(sizeof(*qos));
		qos->id = i + 1000;
		qos->name = qos_names[i];
		list_append(qos_list, qos);
		qos = NULL;
	}

	return qos_list;
}

// void debug_print(char *names, List char_list, int expected_count,
// 		 char **expected_strings)
// {
// 	char *string = NULL;
// 	ListIterator itr = list_iterator_create(char_list);
// 	info("names: %s", names);

// 	while((string = (char*)list_next(itr))){
// 		info("%s", string);
// 	}
// 	info("============");
// 	for (int i = 0; i < expected_count; i++){
// 		info("%s", expected_strings[i]);
// 	}
// 	info(" ");
// 	info(" ");
// }


void test(List qos_list, char *names, int option, int expected_count,
	  char **expected_strings)
{
	int count;
	ListIterator itr;
	List char_list = list_create(xfree_ptr);

	if (!qos_list)
		qos_list = setup_qos_list();

	count = slurmdb_addto_qos_char_list(char_list, qos_list, names, option);

	// debug_print(names, char_list, expected_count, expected_strings);

	if (count == SLURM_ERROR)
		ck_assert_int_eq(0, list_count(char_list));
	else
		ck_assert_int_eq(count, list_count(char_list));
	ck_assert_int_eq(count, expected_count);

	itr = list_iterator_create(char_list);
	for (int i = 0; i < count; i++){
		ck_assert_str_eq((char *)list_next(itr), expected_strings[i]);
	}
	list_iterator_destroy(itr);

	FREE_NULL_LIST(char_list);
	FREE_NULL_LIST(qos_list);
}

/*****************************************************************************
 * UNIT TESTS                                                                *
 ****************************************************************************/

START_TEST(all_three)
{
	test(NULL, "normal,subpar,great", 0, 3,
	     (char *[]){"1000", "1001", "1002"});
}
END_TEST

START_TEST(just_two)
{
	test(NULL, "great,subpar", 0, 2, (char *[]){"1002","1001"});
}
END_TEST

START_TEST(just_one)
{
	test(NULL, "subpar", 0, 1, (char *[]){"1001"});
}
END_TEST

START_TEST(empty)
{
	test(NULL, "", 0, 1, (char *[]){""});
}
END_TEST


START_TEST(different_order)
{
	test(NULL, "subpar,normal,great", 0, 3,
	     (char *[]){ "1001", "1000", "1002" });
}
END_TEST

START_TEST(quotes)
{
	test(NULL, "\"normal,subpar\",great", 0, 2,
	     (char *[]){"1000", "1001"});
}
END_TEST

START_TEST(insertion)
{
	test(NULL, "normal,subpar,great", (int)'+', 3,
	     (char *[]){"+1000", "+1001", "+1002"});
}
END_TEST

START_TEST(removal)
{
	test(NULL, "normal,subpar,great", (int)'-', 3,
	     (char *[]){"-1000", "-1001", "-1002"});
}
END_TEST

START_TEST(selected_insertion)
{
	test(NULL, "+normal,+subpar,+great", 0, 3,
	     (char *[]){"+1000", "+1001", "+1002"});
}
END_TEST

START_TEST(selected_removal)
{
	test(NULL, "-normal,-subpar,-great", 0, 3,
	     (char* []){"-1000", "-1001", "-1002"});
}
END_TEST

START_TEST(selected_mix)
{
	test(NULL, "-normal,+subpar,-great", 0, 3,
	     (char *[]){ "-1000", "+1001", "-1002" });
}
END_TEST

START_TEST(double_insertion)
{
	test(NULL, "+normal,+subpar,+great", (int)'+', 3,
	     (char *[]){ "+1000", "+1001", "+1002" });
}
END_TEST

START_TEST(double_insertion2)
{
	test(NULL, "++normal,++subpar,++great", 0, 3,
	     (char *[]){ "+1000", "+1001", "+1002" });
}
END_TEST

START_TEST(duplicates)
{
	test(NULL, "normal,normal,normal", 0, 1, (char *[]){"1000"});
}
END_TEST

START_TEST(apostrophe)
{
	char names[] = "adam\'s,normal"; /* must be local var; Will edit it */
	List qos_list = setup_qos_list();
	slurmdb_qos_rec_t *qos = xmalloc(sizeof(*qos));

	qos->id = 42;
	qos->name = "adam`s";
	list_append(qos_list, qos);

	test(qos_list, names, 0, 2, (char *[]){"42", "1000"});
}
END_TEST

START_TEST(commas_at_end)
{
	test(NULL, "normal,subpar,great,,,,,,", 0, 3,
	     (char *[]){ "1000", "1001", "1002" });
}
END_TEST

START_TEST(commas_between)
{
	test(NULL, "normal,,,,,,subpar,,,,,great", 0, 3,
	     (char *[]){ "1000", "1001", "1002" });
}
END_TEST

START_TEST(commas_at_start)
{
	test(NULL, ",,,,,,normal,subpar,great", 0, 3,
	     (char *[]){ "1000", "1001", "1002" });
}
END_TEST

START_TEST(comma_at_end) /* if there is a space after comma */
{
	test(NULL, "normal,", 0, 1, (char *[]){"1000"});
}
END_TEST

/*****************************************************************************
 * UNIT TESTS with error messages                                            *
 ****************************************************************************/

START_TEST(insert_and_set)
{
	test(NULL, "+normal,subpar,great", 0, SLURM_ERROR, (char *[]){});
}
END_TEST

START_TEST(insert_and_set2)
{
	test(NULL, "+normal,subpar", 0, SLURM_ERROR, (char *[]){});
}
END_TEST

START_TEST(set_and_insert)
{
	test(NULL, "normal,+subpar,+great", 0, SLURM_ERROR, (char *[]){});
}
END_TEST

START_TEST(set_and_insert2)
{
	test(NULL, "normal,+subpar", 0, SLURM_ERROR, (char *[]){});
}
END_TEST

START_TEST(wrong_qos)
{
	test(NULL, "nonexistent,bad,odd", 0, SLURM_ERROR, NULL);
}
END_TEST

START_TEST(wrong_qos_at_end)
{
	test(NULL, "normal,subpar,nonexistent", 0, SLURM_ERROR,
	     (char *[]){});
}
END_TEST

START_TEST(wrong_qos_between)
{
	test(NULL, "normal,nonexistent,subpar", 0, SLURM_ERROR, (char *[]){});
}
END_TEST

START_TEST(wrong_qos_at_start)
{
	test(NULL, "nonexistent,normal,subpar", 0, SLURM_ERROR, NULL);
}
END_TEST

START_TEST(spaces_between)
{
	test(NULL, "normal, subpar, great", 0, SLURM_ERROR,
	     (char *[]){});
}
END_TEST

START_TEST(quotes2)
{
	test(NULL, "\"normal,subpar,\"great", 0, 2,
	     (char *[]){"1000", "1001"});
}
END_TEST

START_TEST(null_char_list)
{
	List qos_list = setup_qos_list();
	int count = slurmdb_addto_qos_char_list(NULL, qos_list, "normal", 0);
	ck_assert_int_eq(count, 0);
	FREE_NULL_LIST(qos_list);
}
END_TEST

START_TEST(null_qos_list)
{
	List char_list = list_create(NULL);
	int count = slurmdb_addto_qos_char_list(char_list, NULL, "normal", 0);
	ck_assert_int_eq(count, SLURM_ERROR);
	ck_assert_int_eq(0, list_count(char_list));
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(null_names)
{
	List char_list = list_create(NULL);
	List qos_list = setup_qos_list();
	int count = slurmdb_addto_qos_char_list(char_list, qos_list, NULL, 0);
	ck_assert_int_eq(count, 0);
	ck_assert_int_eq(count, list_count(char_list));
	FREE_NULL_LIST(char_list);
	FREE_NULL_LIST(qos_list);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("addto_qos_char_list");

	TCase *tc_core = tcase_create("right_tests");
	tcase_add_test(tc_core, all_three);
	tcase_add_test(tc_core, just_two);
	tcase_add_test(tc_core, just_one);
	tcase_add_test(tc_core, empty);
	tcase_add_test(tc_core, different_order);
	tcase_add_test(tc_core, quotes);
	tcase_add_test(tc_core, insertion);
	tcase_add_test(tc_core, removal);
	tcase_add_test(tc_core, selected_insertion);
	tcase_add_test(tc_core, selected_removal);
	tcase_add_test(tc_core, selected_mix);
	tcase_add_test(tc_core, double_insertion);
	tcase_add_test(tc_core, double_insertion2);
	tcase_add_test(tc_core, duplicates);
	tcase_add_test(tc_core, apostrophe);
	tcase_add_test(tc_core, commas_at_end);
	tcase_add_test(tc_core, commas_between);
	tcase_add_test(tc_core, commas_at_start);
	tcase_add_test(tc_core, comma_at_end);


	TCase *tc_error = tcase_create("error_tests");
	tcase_add_test(tc_error, insert_and_set);
	tcase_add_test(tc_error, insert_and_set2);
	tcase_add_test(tc_error, set_and_insert);
	tcase_add_test(tc_error, set_and_insert2);
	tcase_add_test(tc_error, wrong_qos);
	tcase_add_test(tc_error, wrong_qos_at_end);
	tcase_add_test(tc_error, wrong_qos_between);
	tcase_add_test(tc_error, wrong_qos_at_start);
	tcase_add_test(tc_error, commas_at_end);
	tcase_add_test(tc_error, commas_between);
	tcase_add_test(tc_error, commas_at_start);
	tcase_add_test(tc_error, spaces_between);
	tcase_add_test(tc_error, quotes2);
	tcase_add_test(tc_error, null_char_list);
	tcase_add_test(tc_error, null_qos_list);
	tcase_add_test(tc_error, null_names);

	suite_add_tcase(s, tc_core);
	suite_add_tcase(s, tc_error);
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

	srunner_run(sr, "addto_qos_char_list", "right_tests", CK_VERBOSE);
	printf("\nNow running tests that print error messages\n");
	srunner_run(sr, "addto_qos_char_list", "error_tests", CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
