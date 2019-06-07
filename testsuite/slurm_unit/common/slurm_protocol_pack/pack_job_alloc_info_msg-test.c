#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_common.h"

START_TEST(invalid_protocol)
{
	int rc;
	slurm_msg_t msg = {0};
	msg.msg_type = REQUEST_JOB_ALLOCATION_INFO;

	rc = unpack_msg(&msg, NULL);
	ck_assert_int_eq(rc, SLURM_ERROR);
}
END_TEST

#ifndef NDEBUG
START_TEST(pack_null_req)
{
	Buf buf = init_buf(1024);
	slurm_msg_t msg = {0};
	msg.protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	msg.msg_type = REQUEST_JOB_ALLOCATION_INFO;

	pack_msg(&msg, buf);

	free_buf(buf);
}
END_TEST
#endif

START_TEST(pack_back2_req_null_ptrs)
{
	int rc;
	Buf buf = init_buf(1024);

	slurm_msg_t msg = {0};
	job_alloc_info_msg_t pack_req = {0};
	pack_req.job_id = 12345;

	msg.msg_type         = REQUEST_JOB_ALLOCATION_INFO;
	msg.protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	msg.data             = &pack_req;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);


	msg.data = NULL;
	job_alloc_info_msg_t *unpack_req;

	rc = unpack_msg(&msg, buf);
	unpack_req = (job_alloc_info_msg_t *)msg.data;
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(unpack_req);
	ck_assert(!unpack_req->req_cluster);
	ck_assert_uint_eq(unpack_req->job_id, pack_req.job_id);

	free_buf(buf);
	slurm_free_msg_data(msg.msg_type, msg.data);
}
END_TEST

START_TEST(pack_back2_req)
{
	int rc;
	Buf buf = init_buf(1024);

	slurm_msg_t msg = {0};
	job_alloc_info_msg_t pack_req = {0};
	pack_req.job_id = 12345;
	pack_req.req_cluster = xstrdup("blah");

	msg.msg_type         = REQUEST_JOB_ALLOCATION_INFO;
	msg.protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	msg.data             = &pack_req;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);


	msg.data = NULL;
	job_alloc_info_msg_t *unpack_req;

	rc = unpack_msg(&msg, buf);
	unpack_req = (job_alloc_info_msg_t *)msg.data;
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(unpack_req);
	//ck_assert(!unpack_req->req_cluster); /* >= 17.11 */
	ck_assert_uint_eq(unpack_req->job_id, pack_req.job_id);

	free_buf(buf);
	xfree(pack_req.req_cluster);
	slurm_free_msg_data(msg.msg_type, msg.data);
}
END_TEST

START_TEST(pack_back1_req_null_ptrs)
{
	int rc;
	Buf buf = init_buf(1024);

	slurm_msg_t msg = {0};
	job_alloc_info_msg_t pack_req = {0};
	pack_req.job_id = 12345;

	msg.msg_type         = REQUEST_JOB_ALLOCATION_INFO;
	msg.protocol_version = SLURM_ONE_BACK_PROTOCOL_VERSION;
	msg.data             = &pack_req;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);

	msg.data = NULL;
	job_alloc_info_msg_t *unpack_req;

	rc = unpack_msg(&msg, buf);
	unpack_req = (job_alloc_info_msg_t *)msg.data;
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(unpack_req);
	ck_assert(!unpack_req->req_cluster);
	ck_assert_uint_eq(unpack_req->job_id, pack_req.job_id);

	free_buf(buf);
	slurm_free_msg_data(msg.msg_type, msg.data);
}
END_TEST

START_TEST(pack_back1_req)
{
	int rc;
	Buf buf = init_buf(1024);

	slurm_msg_t msg = {0};
	job_alloc_info_msg_t pack_req = {0};
	pack_req.job_id = 12345;
	pack_req.req_cluster = xstrdup("blah");

	msg.msg_type         = REQUEST_JOB_ALLOCATION_INFO;
	msg.protocol_version = SLURM_ONE_BACK_PROTOCOL_VERSION;
	msg.data             = &pack_req;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);


	msg.data = NULL;
	job_alloc_info_msg_t *unpack_req;

	rc = unpack_msg(&msg, buf);
	unpack_req = (job_alloc_info_msg_t *)msg.data;
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(unpack_req);
	ck_assert_ptr_ne(unpack_req->req_cluster, pack_req.req_cluster);
	ck_assert_str_eq(unpack_req->req_cluster, pack_req.req_cluster);
	ck_assert_uint_eq(unpack_req->job_id, pack_req.job_id);

	free_buf(buf);
	xfree(pack_req.req_cluster);
	slurm_free_msg_data(msg.msg_type, msg.data);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(SRunner *sr)
{
	Suite* s = suite_create("Pack job_alloc_info_msg_t");
	TCase* tc_core = tcase_create("Pack pack_job_alloc_info_msg_t");
	tcase_add_test(tc_core, invalid_protocol);
#ifdef NDEBUG
       printf("Can't perform pack_null_req test with NDEBUG set.\n");
#else
       if (srunner_fork_status(sr) != CK_NOFORK)
               tcase_add_test_raise_signal(tc_core, pack_null_req, SIGABRT);
#endif
	tcase_add_test(tc_core, pack_back2_req_null_ptrs);
	tcase_add_test(tc_core, pack_back2_req);
	tcase_add_test(tc_core, pack_back1_req_null_ptrs);
	tcase_add_test(tc_core, pack_back1_req);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner* sr = srunner_create(NULL);
	//srunner_set_fork_status(sr, CK_NOFORK);
	srunner_add_suite(sr, suite(sr));

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
