#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/assoc_mgr.h"

priority_factors_object_t pack_req = {0};
priority_factors_t pack_f_req = {0};

#ifndef NDEBUG
START_TEST(pack_null_resp)
{
	buf_t *buf = init_buf(1024);
	slurm_msg_t msg = {0};
	msg.protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	msg.msg_type = RESPONSE_PRIORITY_FACTORS;

	pack_msg(&msg, buf);

	free_buf(buf);
}
END_TEST
#endif

void setup()
{
	pack_req.prio_factors = &pack_f_req;
	pack_req.cluster_name = xstrdup("blah");
	pack_req.job_id = 12345;
	pack_req.partition = xstrdup("part");
	pack_req.user_id = 1111;

	pack_f_req.priority_age = 20;
	pack_f_req.priority_assoc = 21;
	pack_f_req.priority_fs = 22;
	pack_f_req.priority_js = 23;
	pack_f_req.priority_part = 24;
	pack_f_req.priority_qos = 25;
	pack_req.direct_prio = 0;
	pack_f_req.priority_site = 27;

	pack_f_req.tres_cnt = 4;
	pack_f_req.tres_weights = xmalloc(sizeof(double) * pack_f_req.tres_cnt);
	pack_f_req.tres_weights[0] = 30;
	pack_f_req.tres_weights[1] = 31;
	pack_f_req.tres_weights[2] = 32;
	pack_f_req.tres_weights[3] = 33;

	pack_f_req.priority_tres = xmalloc(sizeof(double) * pack_f_req.tres_cnt);
	pack_f_req.priority_tres[0] = 40;
	pack_f_req.priority_tres[1] = 41;
	pack_f_req.priority_tres[2] = 42;
	pack_f_req.priority_tres[3] = 43;

	assoc_mgr_tres_name_array = xmalloc(sizeof(char *) * pack_f_req.tres_cnt);
	assoc_mgr_tres_name_array[0] = xstrdup("hello1");
	assoc_mgr_tres_name_array[1] = xstrdup("hello2");
	assoc_mgr_tres_name_array[2] = xstrdup("hello3");
	assoc_mgr_tres_name_array[3] = xstrdup("hello4");

	pack_f_req.nice = 50;
}

void teardown()
{
}

void compare_test(priority_factors_response_msg_t *unpack_resp,
		  uint16_t protocol_version)
{
	priority_factors_object_t *unpack_req;
	priority_factors_t *prio_factors;

	ck_assert(unpack_resp->priority_factors_list);

	unpack_req = list_peek(unpack_resp->priority_factors_list);
	ck_assert(unpack_req);
	prio_factors = unpack_req->prio_factors;
	ck_assert(!unpack_req->cluster_name);
	ck_assert_uint_eq(unpack_req->job_id, pack_req.job_id);

	ck_assert(!xstrcmp(pack_req.partition, unpack_req->partition));
	ck_assert(pack_req.user_id == unpack_req->user_id);

	if (!pack_req.direct_prio) {
		ck_assert(pack_f_req.priority_age == prio_factors->priority_age);

		ck_assert(pack_f_req.priority_fs == prio_factors->priority_fs);
		ck_assert(pack_f_req.priority_js == prio_factors->priority_js);
		ck_assert(pack_f_req.priority_part == prio_factors->priority_part);
		ck_assert(pack_f_req.priority_qos == prio_factors->priority_qos);

		ck_assert(pack_f_req.tres_cnt == prio_factors->tres_cnt);
		for (int i = 0; i < pack_f_req.tres_cnt; i++)
			ck_assert(pack_f_req.tres_weights[i] == prio_factors->tres_weights[i]);

		for (int i = 0; i < pack_f_req.tres_cnt; i++)
			ck_assert(pack_f_req.priority_tres[i] == prio_factors->priority_tres[i]);

		for (int i = 0; i < pack_f_req.tres_cnt; i++)
			ck_assert(!xstrcmp(assoc_mgr_tres_name_array[i], prio_factors->tres_names[i]));

		ck_assert(pack_f_req.nice == prio_factors->nice);
		ck_assert(pack_f_req.priority_assoc == prio_factors->priority_assoc);
		ck_assert(pack_f_req.priority_site == prio_factors->priority_site);
	} else {
		ck_assert(pack_req.direct_prio == unpack_req->direct_prio);
		ck_assert(!prio_factors);
	}
}


void run_test_version(uint16_t protocol_version)
{
	int rc;
	buf_t *buf = init_buf(1024);

	priority_factors_response_msg_t resp_req = {0};
	resp_req.priority_factors_list = list_create(NULL);
	list_append(resp_req.priority_factors_list, &pack_req);

	slurm_msg_t msg = {0};
	msg.msg_type         = RESPONSE_PRIORITY_FACTORS;
	msg.protocol_version = protocol_version;
	msg.data             = &resp_req;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);

	msg.data = NULL;
	priority_factors_response_msg_t *unpack_resp;

	rc = unpack_msg(&msg, buf);
	unpack_resp = (priority_factors_response_msg_t *)msg.data;
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	compare_test(unpack_resp, protocol_version);

	free_buf(buf);
	slurm_free_msg_data(msg.msg_type, msg.data);
}

START_TEST(current_version)
{
	run_test_version(SLURM_PROTOCOL_VERSION);
}
END_TEST

START_TEST(one_back)
{
	run_test_version(SLURM_ONE_BACK_PROTOCOL_VERSION);
}
END_TEST

START_TEST(min_version)
{
	run_test_version(SLURM_MIN_PROTOCOL_VERSION);
}
END_TEST

/* test the direct_prio which will make a NULL priority_factors_t */
START_TEST(current_version_direct_prio)
{
	pack_req.direct_prio = 26;
	run_test_version(SLURM_PROTOCOL_VERSION);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("Pack priority_factors_objects_t");
	TCase *tc_core = tcase_create("Pack priority_factors_objects_t");
	tcase_add_unchecked_fixture(tc_core, setup, teardown);
#ifdef NDEBUG
       printf("Can't perform pack_null_resp test with NDEBUG set.\n");
#else
       if (srunner_fork_status(sr) != CK_NOFORK)
               tcase_add_test_raise_signal(tc_core, pack_null_resp, SIGABRT);
#endif
	tcase_add_test(tc_core, current_version);
	tcase_add_test(tc_core, one_back);
	tcase_add_test(tc_core, min_version);
	tcase_add_test(tc_core, current_version_direct_prio);
	suite_add_tcase(s, tc_core);
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

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
