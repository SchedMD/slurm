#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backfill.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/job_features.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "sched_harness.h"

#include <check.h>

/*
 * Test simple backfill situation
 *
 * 1st job uses 1 node
 * 2nd job requests all nodes and can't run.
 * 3rd job requests remaining 31 nodes and gets backfilled.
 */
START_TEST(test_backfill_1)
{
	job_record_t *job_ptr;
	uint32_t now = time(NULL);

	/* job_id, priority, nodes, num_tasks, segment_size, time_limit, licenses */
	__add_job(0, 10, 1, 1, 0, 10, NULL);
	__add_job(0, 5, 32, 32, 0, 10, NULL);
	__add_job(0, 1, 31, 31, 0, 5, NULL);

	__attempt_backfill();

	list_for_each(job_list, sched_harness_print_job, &now);

	job_ptr = find_job_record(1);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 1 RUNNING");

	job_ptr = find_job_record(2);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 2 !RUNNING");

	job_ptr = find_job_record(3);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 3 RUNNING");
}

END_TEST

/*
 * Test for starving jobs described in scenario in Ticket 20847
 *
 * Bigger, lower priority jobs were jumping ahead of smaller, higher priority
 * jobs.
 *
 * Higher priority jobs had no start time.
 */
START_TEST(test_backfill_2)
{
	uint32_t now = time(NULL);

	/* job_id, priority, nodes, num_tasks, segment_size, time_limit, licenses */
	__add_job(0, 10, 6, 6, 0, 10, NULL);
	__add_job(0, 9, 27, 27, 0, 15, NULL);
	__add_job(0, 8, 28, 28, 0, 14, NULL);
	__add_job(0, 7, 29, 29, 0, 13, NULL);
	__add_job(0, 6, 30, 30, 0, 12, NULL);
	__add_job(0, 5, 5, 5, 0, 10, NULL);
	__add_job(0, 5, 5, 5, 0, 10, NULL);
	/* This job would jump ahead of the priority 6 job */
	__add_job(0, 1, 30, 30, 0, 11, NULL);

	__attempt_backfill();
	list_for_each(job_list, sched_harness_print_job, &now);

	for (int i = 1; i < 9; i++) {
		for (int j = 1; j < 9; j++) {
			job_record_t *job1_ptr = find_job_record(i);
			job_record_t *job2_ptr = find_job_record(j);
			if (!job1_ptr || !job2_ptr)
				continue;
			if ((job1_ptr->priority > job2_ptr->priority) &&
			    (job1_ptr->details->min_nodes <=
			     job2_ptr->details->min_nodes) &&
			    (job2_ptr->start_time) &&
			    (!job1_ptr->start_time ||
			     (job1_ptr->start_time > job2_ptr->start_time)))
				ck_abort_msg("Wrong backfill order");
		}
	}
}

END_TEST

/*
 * Test BF_MAX_JOB_TEST (default=500)
 *
 * Submit 1000 jobs.
 * Check if 500 jobs are backfilled.
 * 501th job shouldn't have a start time.
 */
START_TEST(test_backfill_3)
{
	job_record_t *job1_ptr;
	job_record_t *job2_ptr;

	for (int i = 0; i < 1000; i++) {
		/* job_id, priority, nodes, num_tasks, segment_size, time_limit, licenses */
		__add_job(0, 10, 6, 6, 0, 10, NULL);
	}

	__attempt_backfill();

	job1_ptr = find_job_record(500);
	job2_ptr = find_job_record(501);
	ck_assert_msg((job1_ptr->start_time && !job2_ptr->start_time),
		      "Completed testing 500 (bf_max_job_test) jobs");

	/*
	 * uint32_t now = time(NULL);
	 * list_for_each(job_list, sched_harness_print_job, &now);
	 */
}

END_TEST

/*
 * Test basic simplest backfiling of licences
 */
START_TEST(test_backfill_lic_1)
{
	uint32_t now = time(NULL);
	job_record_t *job_ptr;

	/* job_id, priority, nodes, num_tasks, segment_size, time_limit, licenses */
	__add_job(1, 10, 1, 1, 0, 10, "lic1");
	__add_job(2, 9, 1, 1, 0, 10, "lic1");
	__add_job(3, 8, 1, 1, 0, 10, "lic1");
	__add_job(4, 7, 1, 1, 0, 10, NULL);

	__attempt_backfill();
	list_for_each(job_list, sched_harness_print_job, &now);

	job_ptr = find_job_record(1);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 1 RUNNING");

	job_ptr = find_job_record(2);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 2 !RUNNING");

	job_ptr = find_job_record(3);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 3 !RUNNING");

	job_ptr = find_job_record(4);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 4 RUNNING");
}

END_TEST

/*
 * Test for wrong start_time scenario in Issue 50271
 */
START_TEST(test_backfill_lic_2)
{
	uint32_t now = time(NULL);
	job_record_t *job1_ptr, *job2_ptr;
	part_record_t *part_ptr = find_part_record("test");

	part_ptr->max_share = 1;

	for (int i = 0; i < 12; i++) {
		/* job_id, priority, nodes, num_tasks, segment_size, time_limit, licenses */
		__add_job(0, 10, 1, 1, 0, 10, "lic2");
	}
	__attempt_backfill();
	list_for_each(job_list, sched_harness_print_job, &now);

	job1_ptr = find_job_record(7);
	job2_ptr = find_job_record(12);

	if (job1_ptr->start_time != job2_ptr->start_time)
		ck_abort_msg("Wrong start_time");
}

END_TEST

int main(int argc, char *argv[])
{
	int number_failed = 0;
	sched_harness_init("backfill-test", argc, argv);

	if (!sched_harness_emulating()) {
		Suite *s = suite_create("backfill");
		SRunner *sr = srunner_create(s);
		TCase *tc = tcase_create("backfill");

		tcase_set_timeout(tc, 10);

		tcase_add_test(tc, test_backfill_1);
		tcase_add_test(tc, test_backfill_2);
		tcase_add_test(tc, test_backfill_3);

		tcase_add_test(tc, test_backfill_lic_1);
		tcase_add_test(tc, test_backfill_lic_2);

		suite_add_tcase(s, tc);

		srunner_run_all(sr, CK_ENV);
		number_failed += srunner_ntests_failed(sr);
		srunner_free(sr);
	} else {
		uint32_t now;
		sched_harness_load_test();
		now = time(NULL);
		__attempt_backfill();
		list_for_each(job_list, sched_harness_print_job, &now);
	}

	sched_harness_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
