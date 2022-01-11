/*****************************************************************************\
 *  Copyright (C) 2021 SchedMD LLC
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

#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

// void debug_print(List char_list, int expected_count, char **expected_strings){
// 	slurm_selected_step_t *selected_step = NULL;
// 	ListIterator itr = list_iterator_create(char_list);
// 	char buf[64];
// 	while((selected_step = list_next(itr))){
// 		// info("job_id %d, step_id %d step_het_comp %d, array_task_id %d, het_job_offset %d",
// 		//      selected_step->step_id.job_id,
// 		//      selected_step->step_id.step_id,
// 		//      selected_step->step_id.step_het_comp,
// 		//      selected_step->array_task_id,
// 		//      selected_step->het_job_offset);
// 		log_build_step_id_str(&(selected_step->step_id), buf,
// 				      sizeof(buf), STEP_ID_FLAG_NO_PREFIX);
// 		info("%s", buf);
// 	}
// 	info("===========");
// 	for (int i = 0; i < expected_count; i++){
// 		info("%s", expected_strings[i]);
// 	}
// 	info(" ");
// 	info(" ");
// }

void test(char *names, int expected_count,
	  char **expected_strings)
{
	int count;
	ListIterator itr;
	List char_list = list_create(xfree_ptr);
	char buf[64];
	slurm_selected_step_t *selected_step = NULL;

	count = slurm_addto_step_list(char_list, names);

	// debug_print(char_list, expected_count, expected_strings);

	ck_assert_int_eq(count, list_count(char_list));
	ck_assert_int_eq(count, expected_count);

	itr = list_iterator_create(char_list);
	for (int i = 0; i < count; i++){
		selected_step = list_next(itr);
		log_build_step_id_str(&(selected_step->step_id), buf,
				      sizeof(buf), STEP_ID_FLAG_NO_PREFIX);
		ck_assert_str_eq(buf, expected_strings[i]);
	}
	list_iterator_destroy(itr);

	FREE_NULL_LIST(char_list);
}

/*****************************************************************************
 * UNIT TESTS                                                                *
 ****************************************************************************/

START_TEST(empty)
{
	test("", 0, (char *[]){});
}
END_TEST

START_TEST(job)
{
	test("123", 1, (char *[]){"123"});
}
END_TEST

START_TEST(step)
{
	test("123.1", 1, (char *[]){"123.1"});
}
END_TEST

START_TEST(batch_step)
{
	test("123.batch", 1, (char *[]){"123.batch"});
}
END_TEST

START_TEST(all_three)
{
	test("123,123.1,123.batch", 3,
	     (char *[]){"123", "123.1", "123.batch"});
}
END_TEST

START_TEST(different_order)
{
	test("123.1,123,123.batch", 3,
	     (char *[]){ "123.1", "123", "123.batch" });
}
END_TEST

START_TEST(quotes)
{
	test("\"123,123.1\",123.batch", 2,
	     (char *[]){"123", "123.1"});
}
END_TEST

START_TEST(duplicates)
{
	test("123.batch,123.batch,123.batch", 1, (char *[]){"123.batch"});
}
END_TEST

START_TEST(null_names)
{
	List char_list = list_create(NULL);
	int count = slurm_addto_step_list(char_list, NULL);
	ck_assert_int_eq(count, 0);
	FREE_NULL_LIST(char_list);
}
END_TEST

START_TEST(commas_at_end)
{
	test("123,123.1,123.batch,,,,,", 3,
	     (char *[]){ "123", "123.1", "123.batch" });
}
END_TEST

START_TEST(commas_between)
{
	test("123,,,,,,123.1,,,,,123.batch", 3,
	     (char *[]){ "123", "123.1", "123.batch" });
}
END_TEST

START_TEST(commas_at_start)
{
	test(",,,,,123,123.1,123.batch", 3,
	     (char *[]){ "123", "123.1", "123.batch" });
}
END_TEST

START_TEST(comma_at_end) /* if there is a space after comma */
{
	test("123,", 1, (char *[]){"123"});
}
END_TEST


/*****************************************************************************
 * UNIT TESTS with error messages                                            *
 ****************************************************************************/

START_TEST(spaces_between)
{
	test("123, 123.1, 123.batch", 1,
	     (char *[]){ "123", "123.1", "123.batch" });
}
END_TEST

START_TEST(null_char_list)
{
	int count = slurm_addto_step_list(NULL, "123");
	ck_assert_int_eq(count, 0);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("slurm_addto_step_list");

	TCase *tc_core = tcase_create("right_tests");
	tcase_add_test(tc_core, empty);
	tcase_add_test(tc_core, job);
	tcase_add_test(tc_core, step);
	tcase_add_test(tc_core, batch_step);
	tcase_add_test(tc_core, all_three);
	tcase_add_test(tc_core, different_order);
	tcase_add_test(tc_core, quotes);
	tcase_add_test(tc_core, duplicates);
	tcase_add_test(tc_core, null_names);
	tcase_add_test(tc_core, commas_between);
	tcase_add_test(tc_core, commas_at_start);
	tcase_add_test(tc_core, commas_at_end);
	tcase_add_test(tc_core, comma_at_end);

	TCase *tc_error = tcase_create("error_tests");
	if (srunner_fork_status(sr) != CK_NOFORK) {
		/* Bad step ids cause slurm to exit 1 with fatal. */
		tcase_add_exit_test(tc_error, spaces_between, 1);
	}

	tcase_add_test(tc_error, null_char_list);

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

	srunner_run(sr, "slurm_addto_step_list", "right_tests", CK_VERBOSE);
	printf("\nNow running tests that print error messages\n");
	srunner_run(sr, "slurm_addto_step_list", "error_tests", CK_VERBOSE);
	// srunner_run_all(sr, CK_123);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
