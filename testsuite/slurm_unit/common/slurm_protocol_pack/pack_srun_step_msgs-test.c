#include <check.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * SRUN_STEP_ALLOCATED reuses the step_create_response_msg pack/unpack
 * (the push of a step the stepmgr created at wake).  Round-trip the shape a
 * real push carries: scalars, strings and a populated step layout.  A cred
 * needs cred-plugin context, so it stays NULL here and the has-cred leg is
 * left to integration coverage.
 */
START_TEST(pack_step_allocated)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurm_msg_t msg = { { 0 } };
	step_create_resp_msg_t pack_resp = { 0 };
	step_create_resp_msg_t *unpack_resp;
	slurm_step_layout_t layout = { 0 };
	uint32_t tids0[] = { 0, 1 };
	uint32_t tids1[] = { 2 };
	uint16_t tasks[] = { 2, 1 };
	uint32_t *tids[] = { tids0, tids1 };

	layout.node_list = xstrdup("node[0-1]");
	layout.node_cnt = 2;
	layout.start_protocol_ver = SLURM_PROTOCOL_VERSION;
	layout.task_cnt = 3;
	layout.task_dist = SLURM_DIST_BLOCK;
	layout.tasks = tasks;
	layout.tids = tids;

	pack_resp.step_layout = &layout;
	pack_resp.step_id.job_id = 12345;
	pack_resp.step_id.step_id = 7;
	pack_resp.step_id.step_het_comp = NO_VAL;
	pack_resp.def_cpu_bind_type = CPU_BIND_TO_CORES;
	pack_resp.resv_ports = xstrdup("1000-1001");
	pack_resp.stepmgr = xstrdup("node0");
	pack_resp.state = JOB_RUNNING;

	msg.msg_type = SRUN_STEP_ALLOCATED;
	msg.protocol_version = SLURM_PROTOCOL_VERSION;
	msg.data = &pack_resp;

	rc = pack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	set_buf_offset(buf, 0);
	msg.data = NULL;
	rc = unpack_msg(&msg, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);

	unpack_resp = msg.data;
	ck_assert(unpack_resp != NULL);
	ck_assert(unpack_resp->step_id.job_id == pack_resp.step_id.job_id);
	ck_assert(unpack_resp->step_id.step_id == pack_resp.step_id.step_id);
	ck_assert(unpack_resp->step_id.step_het_comp ==
		  pack_resp.step_id.step_het_comp);
	ck_assert(unpack_resp->def_cpu_bind_type ==
		  pack_resp.def_cpu_bind_type);
	ck_assert(unpack_resp->state == pack_resp.state);
	ck_assert_str_eq(unpack_resp->resv_ports, pack_resp.resv_ports);
	ck_assert_str_eq(unpack_resp->stepmgr, pack_resp.stepmgr);

	ck_assert(unpack_resp->step_layout != NULL);
	ck_assert_str_eq(unpack_resp->step_layout->node_list, layout.node_list);
	ck_assert(unpack_resp->step_layout->node_cnt == layout.node_cnt);
	ck_assert(unpack_resp->step_layout->task_cnt == layout.task_cnt);
	ck_assert(unpack_resp->step_layout->task_dist == layout.task_dist);
	ck_assert(unpack_resp->step_layout->start_protocol_ver ==
		  layout.start_protocol_ver);
	ck_assert(unpack_resp->step_layout->tasks[0] == tasks[0]);
	ck_assert(unpack_resp->step_layout->tasks[1] == tasks[1]);
	ck_assert(unpack_resp->step_layout->tids[0][0] == tids0[0]);
	ck_assert(unpack_resp->step_layout->tids[0][1] == tids0[1]);
	ck_assert(unpack_resp->step_layout->tids[1][0] == tids1[0]);

	xfree(layout.node_list);
	xfree(pack_resp.resv_ports);
	xfree(pack_resp.stepmgr);
	free_buf(buf);
	slurm_free_msg_data(msg.msg_type, msg.data);
}

END_TEST

/*
 * Round-trip step dependencies with selected request protocols.
 * IN _i - index of the protocol and dependency combination to test
 */
START_TEST(pack_step_dependency_protocol)
{
	const uint16_t protocol_versions[] = {
		SLURM_26_11_PROTOCOL_VERSION,
		NO_VAL16,
		NO_VAL16,
		SLURM_26_05_PROTOCOL_VERSION,
	};
	slurmdb_cluster_rec_t cluster = {
		.rpc_version = SLURM_26_11_PROTOCOL_VERSION,
	};
	slurmdb_cluster_rec_t *saved_cluster = working_cluster_rec;
	step_create_req_msg_t request = {
		.step_dependency = "afterok:12345.0",
	};
	step_create_req_msg_t *unpacked_request = NULL;
	slurm_msg_t message = SLURM_MSG_INITIALIZER;
	buf_t *buffer = init_buf(1024);
	int rc = EINVAL;

	message.data = &request;
	message.msg_type = REQUEST_JOB_STEP_CREATE;
	message.protocol_version = protocol_versions[_i];
	working_cluster_rec = (_i == 2) ? &cluster : NULL;
	rc = pack_msg(&message, buffer);
	working_cluster_rec = saved_cluster;

	ck_assert_int_eq(rc, SLURM_SUCCESS);
	set_buf_offset(buffer, 0);
	message.data = NULL;
	rc = unpack_msg(&message, buffer);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	unpacked_request = message.data;
	if (message.protocol_version >= SLURM_26_11_PROTOCOL_VERSION)
		ck_assert_str_eq(unpacked_request->step_dependency,
				 request.step_dependency);
	else
		ck_assert(!unpacked_request->step_dependency);
	slurm_free_msg_data(message.msg_type, message.data);
	free_buf(buffer);
}

END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("Pack srun step msgs");
	TCase *tc_core = tcase_create("Pack srun step msgs");
	tcase_add_test(tc_core, pack_step_allocated);
	tcase_add_loop_test(tc_core, pack_step_dependency_protocol, 0, 4);
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
	srunner_add_suite(sr, suite(sr));

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
