/*****************************************************************************\
 *  Copyright (C) 2021 SchedMD LLC
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
#include <signal.h>

#include "slurm/slurm.h"
#include "src/common/hostlist.h"

/* these are not in slurm.h */
char *slurm_hostlist_nth(hostlist_t, int);
int slurm_hostlist_delete_nth(hostlist_t, int);
char *slurm_hostset_nth(hostset_t, int);
hostset_t slurm_hostset_create(const char*);
void slurm_hostset_destroy(hostset_t);
int slurm_hostset_count(hostset_t);

#ifndef NDEBUG
/* note: only works in CK_FORK mode */
START_TEST(hostlist_nth_lo_assert_check)
{
	hostlist_t hl = slurm_hostlist_create("host1,host2");
	ck_assert_ptr_ne(hl, NULL);

	/* expect SIGABRT */
	/* index must be >= 0 */
	slurm_hostlist_nth(hl, -1);
}
END_TEST

/* note: only works in CK_FORK mode */
START_TEST(hostlist_delete_nth_hi_assert_check)
{
	hostlist_t hl = slurm_hostlist_create("host1,host2");
	ck_assert_ptr_ne(hl, NULL);

	/* expect SIGABRT */
	/* index must be < 2 */
	slurm_hostlist_delete_nth(hl, 2);
}
END_TEST
#endif

START_TEST(hostlist_nth_check)
{
	hostlist_t hl = NULL;
	char *p;
	int n;

	ck_assert_ptr_eq(slurm_hostlist_nth(hl, 0), NULL);

	hl = slurm_hostlist_create("host[1-3],host5");
	ck_assert_ptr_ne(hl, NULL);

	n = slurm_hostlist_count(hl);
	ck_assert_int_eq(n, 4);

	p = slurm_hostlist_nth(hl, 0);
	ck_assert_str_eq(p, "host1");
	free(p);

	p = slurm_hostlist_nth(hl, 1);
	ck_assert_str_eq(p, "host2");
	free(p);

	p = slurm_hostlist_nth(hl, 2);
	ck_assert_str_eq(p, "host3");
	free(p);

	p = slurm_hostlist_nth(hl, 3);
	ck_assert_str_eq(p, "host5");
	free(p);

	ck_assert_int_eq(slurm_hostlist_delete_nth(hl, 0), 1);
	ck_assert_int_eq(slurm_hostlist_count(hl), n-1);

	ck_assert_int_eq(slurm_hostlist_delete_nth(hl, 0), 1);
	ck_assert_int_eq(slurm_hostlist_count(hl), n-2);

	ck_assert_int_eq(slurm_hostlist_delete_nth(hl, 0), 1);
	ck_assert_int_eq(slurm_hostlist_count(hl), n-3);

	ck_assert_int_eq(slurm_hostlist_delete_nth(hl, 0), 1);
	ck_assert_int_eq(slurm_hostlist_count(hl), n-4);

	slurm_hostlist_destroy(hl);
}
END_TEST

START_TEST(hostset_nth_check)
{
	hostset_t hs;
	char *p;
	int n;

	hs = slurm_hostset_create("two[1-2]");
	ck_assert(hs != NULL);

	n = slurm_hostset_count(hs);
	ck_assert_int_eq(n, 2);

	p = slurm_hostset_nth(hs, 0);
	ck_assert_str_eq(p, "two1");
	free(p);

	p = slurm_hostset_nth(hs, 1);
	ck_assert_str_eq(p, "two2");
	free(p);

	slurm_hostset_destroy(hs);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *make_nonassert_suite(void)
{
	Suite *s = suite_create("host_nth_check_nonassert");
	TCase *tc_core = tcase_create("host_nth_check_nonassert");
	tcase_add_test(tc_core, hostlist_nth_check);
	tcase_add_test(tc_core, hostset_nth_check);
	suite_add_tcase(s, tc_core);
	return s;
}

#ifndef NDEBUG
Suite *make_assert_suite(void)
{
	Suite *s = suite_create("host_nth_check_assert");
	TCase *tc_core = tcase_create("host_nth_check_assert");
	tcase_add_test_raise_signal(tc_core, hostlist_nth_lo_assert_check, SIGABRT);
	tcase_add_test_raise_signal(tc_core, hostlist_delete_nth_hi_assert_check, SIGABRT);
	suite_add_tcase(s, tc_core);
	return s;
}
#endif

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(make_nonassert_suite());

#ifdef NDEBUG
	printf("Can't perform assert tests with NDEBUG set.\n");
#else
	/* assert tests can only be run in forking mode */
	if (srunner_fork_status(sr) == CK_FORK)
		srunner_add_suite(sr, make_assert_suite());
	else
		printf("Skipping assert tests since not in forking mode.\n");
#endif

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
