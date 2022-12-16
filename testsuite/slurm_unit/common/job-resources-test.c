/*
 * Test of src/job_resources.c
 */
#include <stdlib.h>
#include <src/common/bitstring.h>
#include <src/common/job_resources.h>
#include <sys/time.h>
#include <check.h>

#define CORE_CNT 80
#define NODE_CNT 8

static void _free_job_res(job_resources_t *job)
{
	bit_free(job->core_bitmap);
	bit_free(job->node_bitmap);
	xfree(job->cores_per_socket);
	xfree(job->sockets_per_node);
	xfree(job->sock_core_rep_count);
	xfree(job);
}

static job_resources_t *_alloc_job_res(void)
{
	job_resources_t *job;

	job = xmalloc(sizeof(job_resources_t));
	job->core_bitmap = bit_alloc(CORE_CNT);
	job->node_bitmap = bit_alloc(NODE_CNT);
	job->cores_per_socket    = xmalloc(sizeof(uint32_t) * NODE_CNT);
	job->sockets_per_node    = xmalloc(sizeof(uint32_t) * NODE_CNT);
	job->sock_core_rep_count = xmalloc(sizeof(uint32_t) * NODE_CNT);

	return job;
}

START_TEST(test_job_resources_or)
{
	job_resources_t *job1, *job2;

	job1 = _alloc_job_res();
	job1->cores_per_socket[0] = 4;
	job1->sockets_per_node[0] = 2;
	job1->sock_core_rep_count[0] = 2;
	bit_set(job1->node_bitmap, 1);	/* Node 1, Cores 0-7 */
	bit_set(job1->core_bitmap, 1);
	bit_set(job1->core_bitmap, 6);
	bit_set(job1->node_bitmap, 6);	/* Node 6, Cores 8-15 */
	bit_set(job1->core_bitmap, 10);
	bit_set(job1->core_bitmap, 12);

	job2 = _alloc_job_res();
	job2->cores_per_socket[0] = 4;
	job2->sockets_per_node[0] = 2;
	job2->sock_core_rep_count[0] = 1;
	job2->cores_per_socket[1] = 5;
	job2->sockets_per_node[1] = 3;
	job2->sock_core_rep_count[1] = 1;
	bit_set(job2->node_bitmap, 1);	/* Node 1, Cores 0-7 */
	bit_set(job2->core_bitmap, 1);
	bit_set(job2->core_bitmap, 7);
	bit_set(job2->node_bitmap, 4);	/* Node 4, Cores 8-22 */
	bit_set(job2->core_bitmap, 8);
	bit_set(job2->core_bitmap, 12);
	bit_set(job2->core_bitmap, 22);

	if (job_resources_or(job1, job2) != 0) {
		ck_abort_msg("job_resources_or function fail");
	} else {
		ck_assert_msg(bit_set_count(job1->node_bitmap) == 3, "node_bitmap count good");
		ck_assert_msg(bit_test(job1->node_bitmap,1), "node 1 set");
		ck_assert_msg(bit_test(job1->node_bitmap,4), "node 4 set");
		ck_assert_msg(bit_test(job1->node_bitmap,6), "node 6 set");
		ck_assert_msg(job1->cores_per_socket[0] == 4, "cores_per_socket[0] value");
		ck_assert_msg(job1->sockets_per_node[0] == 2, "sockets_per_node[0] value");
		ck_assert_msg(job1->sock_core_rep_count[0] == 1, "sock_core_rep_count[0] value");
		ck_assert_msg(job1->cores_per_socket[1] == 5, "cores_per_socket[0] value");
		ck_assert_msg(job1->sockets_per_node[1] == 3, "sockets_per_node[0] value");
		ck_assert_msg(job1->sock_core_rep_count[1] == 1, "sock_core_rep_count[0] value");
		ck_assert_msg(job1->cores_per_socket[2] == 4, "cores_per_socket[0] value");
		ck_assert_msg(job1->sockets_per_node[2] == 2, "sockets_per_node[0] value");
		ck_assert_msg(job1->sock_core_rep_count[2] == 1, "sock_core_rep_count[0] value");
		ck_assert_msg(bit_set_count(job1->core_bitmap) == 8, "core_bitmap count good");
		ck_assert_msg(bit_test(job1->core_bitmap,1), "core 1 set");
		ck_assert_msg(bit_test(job1->core_bitmap,6), "core 6 set");
		ck_assert_msg(bit_test(job1->core_bitmap,7), "core 7 set");
		ck_assert_msg(bit_test(job1->core_bitmap,8), "core 8 set");
		ck_assert_msg(bit_test(job1->core_bitmap,12), "core 12 set");
		ck_assert_msg(bit_test(job1->core_bitmap,22), "core 22 set");
		ck_assert_msg(bit_test(job1->core_bitmap,25), "core 25 set");
		ck_assert_msg(bit_test(job1->core_bitmap,25), "core 27 set");
	}
	_free_job_res(job1);
	_free_job_res(job2);
}
END_TEST

START_TEST(test_job_resources_and)
{
	job_resources_t *job1, *job2;

	job1 = _alloc_job_res();
	job1->cores_per_socket[0] = 4;
	job1->sockets_per_node[0] = 2;
	job1->sock_core_rep_count[0] = 2;
	bit_set(job1->node_bitmap, 0);	/* Node 0, Cores 0-7 */
	bit_set(job1->core_bitmap, 1);
	bit_set(job1->core_bitmap, 5);
	bit_set(job1->core_bitmap, 6);
	bit_set(job1->node_bitmap, 2);	/* Node 2, Cores 8-15 */
	bit_set(job1->core_bitmap, 8);
	bit_set(job1->core_bitmap, 10);
	bit_set(job1->core_bitmap, 12);
	bit_set(job1->core_bitmap, 15);

	job2 = _alloc_job_res();
	job2->cores_per_socket[0] = 5;
	job2->sockets_per_node[0] = 3;
	job2->sock_core_rep_count[0] = 1;
	job2->cores_per_socket[1] = 4;
	job2->sockets_per_node[1] = 2;
	job2->sock_core_rep_count[1] = 1;
	bit_set(job2->node_bitmap, 1);	/* Node 0, Cores 0-14 */
	bit_set(job2->core_bitmap, 1);
	bit_set(job2->core_bitmap, 2);
	bit_set(job2->core_bitmap, 6);
	bit_set(job2->node_bitmap, 2);	/* Node 2, Cores 15-22 */
	bit_set(job2->core_bitmap, 15);
	bit_set(job2->core_bitmap, 16);
	bit_set(job2->core_bitmap, 22);

	if (job_resources_and(job1, job2) != 0) {
		ck_abort_msg("job_resources_and function fail");
	} else {
		ck_assert_msg(bit_set_count(job1->node_bitmap) == 2, "node_bitmap count good");
		ck_assert_msg(bit_test(job1->node_bitmap,0), "node 0 set");
		ck_assert_msg(bit_test(job1->node_bitmap,2), "node 2 set");
		ck_assert_msg(!bit_test(job1->node_bitmap,4), "node 4 unset");
		ck_assert_msg(job1->cores_per_socket[0] == 4, "cores_per_socket[0] value");
		ck_assert_msg(job1->sockets_per_node[0] == 2, "sockets_per_node[0] value");
		ck_assert_msg(bit_set_count(job1->core_bitmap) == 2, "core_bitmap count good");
		ck_assert_msg(bit_test(job1->core_bitmap,8), "core 8 set");
		ck_assert_msg(bit_test(job1->core_bitmap,15), "core 15 set");
	}

	_free_job_res(job1);
	_free_job_res(job2);
}
END_TEST

Suite *job_resources_suite(void)
{
	Suite *s = suite_create("job_resources");
	TCase *tc_core = tcase_create("job_resources");

	tcase_add_test(tc_core, test_job_resources_or);
	tcase_add_test(tc_core, test_job_resources_and);

	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	/* Start the actual libcheck code */
	int number_failed;
	SRunner *sr = srunner_create(job_resources_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
