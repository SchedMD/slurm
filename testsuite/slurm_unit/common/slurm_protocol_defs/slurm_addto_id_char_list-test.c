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
#include <pwd.h>
#include <grp.h>

char *name_user = NULL, *uid_user = NULL;
char *name_root = NULL, *uid_root = NULL;

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

void get_user(char **name, char **uid)
{
	uid_t u = geteuid();
	struct passwd *pw = getpwuid(u);
	if (!pw)
		ck_abort_msg("No user name found");
	xstrfmtcat(*uid, "%d", u);
	*name = xstrdup(pw->pw_name);
}

void get_root(char **name, char **uid)
{
	uid_t u = 0;
	struct passwd *pw = getpwuid(u);
	if (!pw)
		ck_abort_msg("No user name found");
	xstrfmtcat(*uid, "%d", u);
	*name = xstrdup(pw->pw_name);
}

void test(char *names, bool gid, int expected_count,
	  char **expected_strings)
{
	int count;
	ListIterator itr;
	List char_list = list_create(xfree_ptr);

	count = slurm_addto_id_char_list(char_list, names, gid);

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
}

/*****************************************************************************
 * UNIT TESTS                                                                *
 ****************************************************************************/

START_TEST(empty)
{
	test("", false, 0, (char *[]){});
}
END_TEST

START_TEST(user)
{
	test(name_user, false, 1, (char *[]){uid_user});
}
END_TEST

START_TEST(user_uid)
{
	test(uid_user, false, 1, (char *[]){uid_user});
}
END_TEST

START_TEST(root)
{
	test(name_root, false, 1, (char *[]){uid_root});
}
END_TEST

START_TEST(root_uid)
{
	test(uid_root, false, 1, (char *[]){uid_root});
}
END_TEST

START_TEST(both)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s", name_user, name_root);

	test(names, false, 2, (char *[]){uid_user, uid_root});
	xfree(names);
}
END_TEST

START_TEST(mix_name_and_uid)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s", uid_user, name_root);

	test(names, false, 2, (char *[]){uid_user, uid_root});
	xfree(names);
}
END_TEST

START_TEST(quotes)
{
	char *names = NULL;

	xstrfmtcat(names, "\"%s\",%s", name_user, name_root);

	test(names, false, 1, (char *[]){uid_user});
	xfree(names);
}
END_TEST

START_TEST(duplicates)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s,%s", name_user, uid_user, name_user);

	test(name_user, false, 1, (char *[]){uid_user});
	xfree(names);
}
END_TEST

START_TEST(user_groups_gid)
{
	const int max_groups = 15;
	char *names = NULL;
	int group_num = max_groups;
	gid_t groups[group_num];
	char  *groups_str[group_num];
	struct passwd *pw;

	pw = getpwnam(name_user);
	ck_assert(pw != NULL);

	getgrouplist(name_user, pw->pw_gid, groups, &group_num);
	ck_assert(group_num > 0);

	xstrfmtcat(names, "%d", groups[0]);
	for (int i = 1; i < MIN(group_num, max_groups); i++){
		xstrfmtcat(names, ",%d", groups[i]);
	}

	for (int i = 0; i < MIN(group_num, max_groups); i++){
		groups_str[i] = xstrdup_printf("%d", groups[i]);
	}

	test(names, true, MIN(group_num, max_groups), groups_str);

	for (int i = 0; i < MIN(group_num, max_groups); i++){
		xfree(groups_str[i]);
	}
	xfree(names);
}
END_TEST

START_TEST(user_groups_name)
{
	const int max_groups = 15;
	char *names = NULL;
	int group_num = max_groups;
	gid_t groups[group_num];
	char  *groups_str[group_num];
	struct passwd *pw;

	pw = getpwnam(name_user);
	ck_assert(pw != NULL);

	getgrouplist(name_user, pw->pw_gid, groups, &group_num);
	ck_assert(group_num > 0);

	xstrfmtcat(names, "%s", getgrgid(groups[0])->gr_name);
	for (int i = 1; i < MIN(group_num, max_groups); i++){
		xstrfmtcat(names, ",%s", getgrgid(groups[i])->gr_name);
	}

	for (int i = 0; i < MIN(group_num, max_groups); i++){
		groups_str[i] = xstrdup_printf("%d", groups[i]);
	}

	test(names, true, MIN(group_num, max_groups), groups_str);

	for (int i = 0; i < MIN(group_num, max_groups); i++){
		xfree(groups_str[i]);
	}
	xfree(names);
}
END_TEST

START_TEST(comma_at_end) /* if there is a space after comma */
{
	char *names = NULL;

	xstrfmtcat(names, "%s,", name_user);

	test(names, false, 1, (char *[]){uid_user});
	xfree(names);}
END_TEST

START_TEST(commas_at_end)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s,,,,", name_user, name_root);

	test(names, false, 2, (char *[]){uid_user, uid_root});
	xfree(names);
}
END_TEST

START_TEST(commas_between)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,,,,%s", name_user, name_root);

	test(names, false, 2, (char *[]){uid_user, uid_root});
	xfree(names);
}
END_TEST

START_TEST(commas_at_start)
{
	char *names = NULL;

	xstrfmtcat(names, ",,,,,%s,%s", name_user, name_root);

	test(names, false, 2, (char *[]){uid_user, uid_root});
	xfree(names);
}
END_TEST

/*****************************************************************************
 * UNIT TESTS with error messages                                            *
 ****************************************************************************/

START_TEST(bad_name)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s,%s", uid_user, "NotUser", uid_root);

	test(names, false, SLURM_ERROR, (char *[]){});
	xfree(names);
}
END_TEST

START_TEST(bad_uid)
{
	char *names = NULL;

	xstrfmtcat(names, "%s,%s,%s", uid_user, "1234567", uid_root);

	test(names, false, SLURM_ERROR, (char *[]){});
	xfree(names);
}
END_TEST

START_TEST(bad_group_name)
{
	char *names = NULL;

	xstrfmtcat(names, "%s", "NotGroup");

	test(names, true, SLURM_ERROR, (char *[]){});
	xfree(names);
}
END_TEST

START_TEST(bad_gid)
{
	char *names = NULL;

	xstrfmtcat(names, "%s", "1234567");

	test(names, true, SLURM_ERROR, (char *[]){});
	xfree(names);
}
END_TEST

START_TEST(spaces_between)
{
	char *names = NULL;

	xstrfmtcat(names, "%s, %s", name_user, name_root);

	test(names, false, SLURM_ERROR, (char *[]){});
	xfree(names);
}
END_TEST

START_TEST(null_char_list)
{
	int count = slurm_addto_id_char_list(NULL, name_user, 0);
	ck_assert_int_eq(count, 0);
}
END_TEST

START_TEST(null_names)
{
	List char_list = list_create(NULL);
	int count = slurm_addto_id_char_list(char_list, NULL, 0);
	ck_assert_int_eq(count, 0);
	FREE_NULL_LIST(char_list);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("slurm_addto_id_char_list");

	TCase *tc_core = tcase_create("right_tests");
	tcase_add_test(tc_core, empty);
	tcase_add_test(tc_core, user);
	tcase_add_test(tc_core, user_uid);
	tcase_add_test(tc_core, root);
	tcase_add_test(tc_core, root_uid);
	tcase_add_test(tc_core, both);
	tcase_add_test(tc_core, mix_name_and_uid);
	tcase_add_test(tc_core, quotes);
	tcase_add_test(tc_core, duplicates);
	tcase_add_test(tc_core, user_groups_gid);
	tcase_add_test(tc_core, user_groups_name);
	tcase_add_test(tc_core, comma_at_end);
	tcase_add_test(tc_core, commas_at_end);
	tcase_add_test(tc_core, commas_between);
	tcase_add_test(tc_core, commas_at_start);

	TCase *tc_error = tcase_create("error_tests");
	tcase_add_test(tc_error, bad_name);
	tcase_add_test(tc_error, bad_uid);
	tcase_add_test(tc_error, bad_group_name);
	tcase_add_test(tc_error, bad_gid);
	tcase_add_test(tc_error, spaces_between);

	tcase_add_test(tc_error, null_char_list);
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

	get_user(&name_user, &uid_user);
	get_root(&name_root, &uid_root);

	srunner_run(sr, "slurm_addto_id_char_list", "right_tests", CK_VERBOSE);
	printf("\nNow running tests that print error messages\n");
	srunner_run(sr, "slurm_addto_id_char_list", "error_tests", CK_VERBOSE);
	// srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	xfree(name_user);
	xfree(uid_user);
	xfree(name_root);
	xfree(uid_root);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
