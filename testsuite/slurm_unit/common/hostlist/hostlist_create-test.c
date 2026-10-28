/*****************************************************************************\
 *  hostlist_create-test.c - Test limits on parsing a host list.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <errno.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Must match MAX_RANGES in src/common/hostlist.c */
#define RANGES_LIMIT (256 * 1024)

/*
 * Build "<prefix>[0,1,...,<cnt - 1>]<suffix>" so that the bracketed range list
 * contains cnt comma separated ranges.
 */
static char *_build_range_list(const char *prefix, int cnt, const char *suffix)
{
	char *str = NULL, *pos = NULL;

	xstrfmtcatat(str, &pos, "%s[", prefix);
	for (int i = 0; i < cnt; i++)
		xstrfmtcatat(str, &pos, "%s%d", (i ? "," : ""), i);
	xstrfmtcatat(str, &pos, "]%s", suffix);

	return str;
}

START_TEST(hostlist_create_ranges_at_limit)
{
	char *str = _build_range_list("n", RANGES_LIMIT, "");
	hostlist_t *hl = slurm_hostlist_create(str);

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_count(hl), RANGES_LIMIT);

	slurm_hostlist_destroy(hl);
	xfree(str);
}

END_TEST

START_TEST(hostlist_create_too_many_ranges)
{
	char *str = _build_range_list("n", RANGES_LIMIT + 1, "");

	errno = 0;
	ck_assert(!slurm_hostlist_create(str));
	ck_assert_int_eq(errno, EINVAL);

	xfree(str);
}

END_TEST

START_TEST(hostlist_create_too_many_prefix_ranges)
{
	char *str = _build_range_list("n", RANGES_LIMIT + 1, "x[0]");

	errno = 0;
	ck_assert(!slurm_hostlist_create(str));
	ck_assert_int_eq(errno, EINVAL);

	xfree(str);
}

END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *make_hostlist_create_suite(void)
{
	Suite *s = suite_create("hostlist_create");
	TCase *tc_core = tcase_create("hostlist_create");
	tcase_set_timeout(tc_core, 60);
	tcase_add_test(tc_core, hostlist_create_ranges_at_limit);
	tcase_add_test(tc_core, hostlist_create_too_many_ranges);
	tcase_add_test(tc_core, hostlist_create_too_many_prefix_ranges);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(make_hostlist_create_suite());

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
