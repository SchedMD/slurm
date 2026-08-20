/*****************************************************************************\
 *  topology_test.c - node selection tests that need a large cluster
 *
 *  Node selection costs that are invisible on a small cluster can starve the
 *  schedulers on a large cluster. This program starts an in-process scheduler
 *  over several thousand nodes described by its slurm.conf and drives jobs
 *  through it.
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

#include <stdio.h>
#include <stdlib.h>

#include "backfill.h"
#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/macros.h"

#include "src/slurmctld/slurmctld.h"

#include "sched_harness.h"

#include <check.h>

#define FRAG_STRIDE 100

/* Make every FRAG_STRIDE'th node unavailable. */
static void _fragment_cluster(void)
{
	for (int i = FRAG_STRIDE - 1; i < node_record_count; i += FRAG_STRIDE) {
		bit_clear(avail_node_bitmap, i);
		bit_clear(idle_node_bitmap, i);
		bit_clear(share_node_bitmap, i);
		bit_clear(up_node_bitmap, i);
	}
}

/*
 * Confirm the harness came up over the configured cluster before running any
 * other tests.
 */
START_TEST(test_cluster_is_up)
{
	ck_assert_msg(node_record_count == 16384,
		      "expected 16384 nodes, found %d", node_record_count);
	ck_assert_msg(find_part_record("test") != NULL, "no test partition");
	ck_assert_msg(bit_set_count(avail_node_bitmap) == node_record_count,
		      "expected every node to be available");
}

END_TEST

/*
 * A --contiguous job that cannot fit must be given up on quickly.
 *
 * Every hundredth node is made unavailable, so the longest run of consecutive
 * available nodes is 99, and a 200 node contiguous request can never be
 * satisfied. The cost of determining that the job won't fit should be cheap.
 * Node selection used to respond to this by dropping one candidate node and
 * evaluating the whole cluster again, thousands of times, even though removing
 * candidates could only split or shorten the runs it had already rejected. This
 * took seconds per job test at this size and caused scheduler starvation
 * (ticket 25458).
 *
 * The test case timeout is the assertion. With the correct behavior this runs
 * in tens of milliseconds; unbounded it takes tens of seconds and check kills
 * it. There is no threshold to tune because the difference is three orders of
 * magnitude.
 */
START_TEST(test_contiguous_no_fit_is_cheap)
{
	job_record_t *job_ptr;

	_fragment_cluster();

	/* job_id, priority, nodes, num_tasks, segment_size, time_limit */
	job_ptr = __add_job(0, 10, 200, 200, 0, 10, NULL);
	job_ptr->details->contiguous = 1;

	__attempt_backfill();

	ck_assert_msg(!IS_JOB_RUNNING(job_ptr),
		      "a contiguous job with no run of 200 nodes must not run");
}

END_TEST

int main(int argc, char *argv[])
{
	int number_failed = 0;
	Suite *s;
	SRunner *sr;
	TCase *tc;

	sched_harness_init("topology-test", argc, argv);

	s = suite_create("topology");
	sr = srunner_create(s);
	tc = tcase_create("topology");

	/*
	 * Set a timeout long enough that with the correct behavior a slow
	 * machine won't exceed it, and short enough that the broken behavior
	 * will exceed it.
	 */
	tcase_set_timeout(tc, 5);

	tcase_add_test(tc, test_cluster_is_up);
	tcase_add_test(tc, test_contiguous_no_fit_is_cheap);

	suite_add_tcase(s, tc);

	srunner_run_all(sr, CK_ENV);
	number_failed += srunner_ntests_failed(sr);
	srunner_free(sr);

	sched_harness_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
