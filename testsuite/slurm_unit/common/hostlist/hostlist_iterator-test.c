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

#include <check.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"

/* these are not in slurm.h */
hostlist_iterator_t *slurm_hostlist_iterator_create(hostlist_t *hl);
void slurm_hostlist_iterator_destroy(hostlist_iterator_t *i);
void slurm_hostlist_iterator_reset(hostlist_iterator_t *i);
int slurm_hostlist_remove(hostlist_iterator_t *i);
char *slurm_hostlist_next(hostlist_iterator_t *i);

static hostlist_t *_init_hostlist(char **hosts, int host_cnt)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);

	for (int i = 0; i < host_cnt; i++) {
		slurm_hostlist_push(hl, hosts[i]);
	}

	return hl;
}

START_TEST(hostlist_iterator_loop_check)
{
	int expected_host_cnt = 5;
	char *expected_hosts[5] = { "a1", "a2", "a3", "b1", "c1" };
	hostlist_t *hl = _init_hostlist(expected_hosts, expected_host_cnt);
	hostlist_iterator_t *hi = slurm_hostlist_iterator_create(hl);
	int cnt = 0;
	char *host_name = NULL;

	/* verify it loops though all hosts in the correct order */
	while ((host_name = slurm_hostlist_next(hi))) {
		ck_assert_str_eq(host_name, expected_hosts[cnt]);
		cnt++;
		free(host_name);
	}

	ck_assert_int_eq(expected_host_cnt, cnt);

	/* verify resetting the iterator loops through all nodes again */
	cnt = 0;
	slurm_hostlist_iterator_reset(hi);
	while ((host_name = slurm_hostlist_next(hi))) {
		ck_assert_str_eq(host_name, expected_hosts[cnt]);
		cnt++;
		free(host_name);
	}

	ck_assert_int_eq(expected_host_cnt, cnt);

	slurm_hostlist_iterator_destroy(hi);
	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(hostlist_iterator_remove_check)
{
	int expected_host_cnt = 11;
	char *expected_hosts[11] = { "a1", "b1", "b2", "b3", "c1", "d1",
				     "d2", "d3", "d4", "d5", "e1" };
	int delete_host_cnt = 5;
	char *delete_hosts[5] = { "a1", "c1", "d2", "d5", "e1" };
	char *expected_deleted_str[5] = {
		"b[1-3],c1,d[1-5],e1", "b[1-3],d[1-5],e1", "b[1-3],d[1,3-5],e1",
		"b[1-3],d[1,3-4],e1", "b[1-3],d[1,3-4]"
	};
	hostlist_t *hl = _init_hostlist(expected_hosts, expected_host_cnt);
	hostlist_iterator_t *hi = slurm_hostlist_iterator_create(hl);
	int cnt = 0;
	char *host_name = NULL;
	char *hostlist_str = NULL;

	/* verify each host name is hit once while removing hosts */
	while ((host_name = slurm_hostlist_next(hi))) {
		ck_assert_str_eq(host_name, expected_hosts[cnt]);
		cnt++;

		for (int i = 0; i < delete_host_cnt; i++) {
			if (!strcmp(host_name, delete_hosts[i])) {
				/* verify host was actually removed from hl */
				slurm_hostlist_remove(hi);
				hostlist_str =
					slurm_hostlist_ranged_string_xmalloc(
						hl);
				ck_assert_str_eq(hostlist_str,
						 expected_deleted_str[i]);
				xfree(hostlist_str);
				break;
			}
		}

		free(host_name);
	}

	ck_assert_int_eq(expected_host_cnt, cnt);

	slurm_hostlist_iterator_destroy(hi);
	slurm_hostlist_destroy(hl);
}

END_TEST

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	Suite *s = suite_create("hostlist_iterator");
	TCase *tc_core = tcase_create("hostlist_iterator");
	SRunner *sr;
	int number_failed;

	tcase_add_test(tc_core, hostlist_iterator_loop_check);
	tcase_add_test(tc_core, hostlist_iterator_remove_check);

	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
