/*****************************************************************************\
 *  test_kv_protocol.c - KV data serialization and protocol-version tests
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
#include <string.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/pack_data.h"
#include "src/common/part_record.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/slurmdb_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/hash.h"

/* Isolate job-descriptor unpacking from the unrelated hash plugin. */
extern int hash_g_compute(char *input, int len, char *custom_str, int cs_len,
			  slurm_hash_t *hash)
{
	memset(hash->hash, 0, sizeof(hash->hash));
	return sizeof(hash->hash);
}

static const uint16_t versions[] = {
	SLURM_26_11_PROTOCOL_VERSION,
	SLURM_26_05_PROTOCOL_VERSION,
	SLURM_25_11_PROTOCOL_VERSION,
	SLURM_MIN_PROTOCOL_VERSION,
};

static data_t *_document(void)
{
	data_t *data = data_set_dict(data_new());
	data_t *list = data_set_list(data_key_set(data, "values"));

	data_set_int(data_list_append(list), 123456789);
	data_set_bool(data_list_append(list), true);
	data_set_string(data_list_append(list), "quoted \"metadata\"");
	data_set_null(data_list_append(list));
	return data;
}

START_TEST(test_kv_update_requests)
{
	job_desc_msg_t job = { 0 };
	update_node_msg_t node = { 0 };
	partition_info_t part = { 0 };
	resv_desc_msg_t resv = { 0 };
	slurm_msg_t send = { 0 }, recv = { 0 };
	data_t **kv = NULL, *received = NULL;
	kv_op_t *op = NULL, received_op = KV_OP_NONE;
	uint16_t version = versions[(_i / 2) % 4];
	buf_t *buffer = init_buf(0);
	uint32_t length = 0;

	switch ((_i / 8) % 4) {
	case 0:
		slurm_init_job_desc_msg(&job);
		send.msg_type = REQUEST_UPDATE_JOB;
		send.data = &job;
		kv = &job.kv;
		op = &job.kv_op;
		break;
	case 1:
		slurm_init_update_node_msg(&node);
		send.msg_type = REQUEST_UPDATE_NODE;
		send.data = &node;
		kv = &node.kv;
		op = &node.kv_op;
		break;
	case 2:
		slurm_init_part_desc_msg(&part);
		send.msg_type = REQUEST_UPDATE_PARTITION;
		send.data = &part;
		kv = &part.kv;
		op = &part.kv_op;
		break;
	case 3:
		slurm_init_resv_desc_msg(&resv);
		send.msg_type = REQUEST_UPDATE_RESERVATION;
		send.data = &resv;
		kv = &resv.kv;
		op = &resv.kv_op;
		break;
	}
	if (_i % 2)
		*kv = _document();
	*op = (kv_op_t) (_i / 32);
	send.protocol_version = recv.protocol_version = version;
	recv.msg_type = send.msg_type;
	ck_assert_int_eq(pack_msg(&send, buffer), SLURM_SUCCESS);
	length = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	if ((*op == KV_OP_NONE) && *kv &&
	    (version >= SLURM_26_11_PROTOCOL_VERSION)) {
		ck_assert_int_eq(unpack_msg(&recv, buffer), SLURM_ERROR);
		ck_assert_ptr_null(recv.data);
		goto cleanup;
	}
	ck_assert_int_eq(unpack_msg(&recv, buffer), SLURM_SUCCESS);
	ck_assert_uint_eq(get_buf_offset(buffer), length);

	switch ((_i / 8) % 4) {
	case 0:
		received = ((job_desc_msg_t *) recv.data)->kv;
		received_op = ((job_desc_msg_t *) recv.data)->kv_op;
		break;
	case 1:
		received = ((update_node_msg_t *) recv.data)->kv;
		received_op = ((update_node_msg_t *) recv.data)->kv_op;
		break;
	case 2:
		received = ((partition_info_t *) recv.data)->kv;
		received_op = ((partition_info_t *) recv.data)->kv_op;
		break;
	case 3:
		received = ((resv_desc_msg_t *) recv.data)->kv;
		received_op = ((resv_desc_msg_t *) recv.data)->kv_op;
		break;
	}
	if (version >= SLURM_26_11_PROTOCOL_VERSION) {
		ck_assert_int_eq(received_op, *op);
		ck_assert_ptr_nonnull(received);
		if (*kv) {
			ck_assert(data_check_match(*kv, received, false));
		} else {
			ck_assert_int_eq(data_get_type(received),
					 DATA_TYPE_NULL);
		}
	} else {
		ck_assert_ptr_null(received);
		ck_assert_int_eq(received_op, KV_OP_NONE);
	}

cleanup:
	FREE_NULL_DATA(*kv);
	slurm_free_msg_data(recv.msg_type, recv.data);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_kv_operation_wire_width)
{
	update_node_msg_t node = { 0 };
	slurm_msg_t msg = {
		.data = &node,
		.msg_type = REQUEST_UPDATE_NODE,
		.protocol_version = SLURM_26_11_PROTOCOL_VERSION,
	};
	buf_t *buffer = init_buf(0), *expected = init_buf(0);

	slurm_init_update_node_msg(&node);
	node.kv_op = (kv_op_t) _i;
	node.cpu_bind = 0x12345678;
	ck_assert_int_eq(pack_msg(&msg, buffer), SLURM_SUCCESS);
	packnull(expected);
	packnull(expected);
	ck_assert_int_eq(pack_data(NULL, msg.protocol_version, expected),
			 SLURM_SUCCESS);
	pack16((uint16_t) node.kv_op, expected);
	pack32(node.cpu_bind, expected);
	ck_assert(get_buf_offset(buffer) >= get_buf_offset(expected));
	ck_assert_msg(!memcmp(get_buf_data(buffer), get_buf_data(expected),
			      get_buf_offset(expected)),
		      "KV operation must occupy exactly two wire bytes");
	FREE_NULL_BUFFER(buffer);
	FREE_NULL_BUFFER(expected);
}

END_TEST

START_TEST(test_kv_partition_state)
{
	part_record_t *part = part_record_create(), *out = NULL;
	buf_t *buffer = init_buf(0);
	uint16_t version = versions[_i];
	uint32_t length = 0;

	part->kv = _document();
	part_record_pack(part, buffer, version);
	length = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	ck_assert_int_eq(part_record_unpack(&out, buffer, version),
			 SLURM_SUCCESS);
	ck_assert_uint_eq(get_buf_offset(buffer), length);
	if (version >= SLURM_26_11_PROTOCOL_VERSION) {
		ck_assert(data_check_match(part->kv, out->kv, false));
	} else {
		ck_assert_ptr_null(out->kv);
	}

	part_record_delete(part);
	part_record_delete(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_kv_accounting_reservation)
{
	slurmdb_reservation_rec_t resv = { 0 }, *out = NULL;
	buf_t *buffer = init_buf(0);
	uint16_t version = versions[_i / 2];
	uint32_t length = 0;

	resv.kv = _document();
	slurmdb_pack_reservation_rec((_i % 2) ? &resv : NULL, version, buffer);
	length = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	ck_assert_int_eq(slurmdb_unpack_reservation_rec((void **) &out, version,
							buffer),
			 SLURM_SUCCESS);
	ck_assert_uint_eq(get_buf_offset(buffer), length);
	if (version >= SLURM_26_11_PROTOCOL_VERSION) {
		ck_assert_ptr_nonnull(out->kv);
		if (_i % 2) {
			ck_assert(data_check_match(resv.kv, out->kv, false));
		} else {
			ck_assert_int_eq(data_get_type(out->kv),
					 DATA_TYPE_NULL);
		}
	} else {
		ck_assert_ptr_null(out->kv);
	}

	FREE_NULL_DATA(resv.kv);
	slurmdb_destroy_reservation_rec(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_kv_accounting_job)
{
	slurmdb_job_rec_t job = { 0 }, *out = NULL;
	buf_t *buffer = init_buf(0);
	uint16_t version = versions[_i];
	uint32_t length = 0;

	job.kv = _document();
	slurmdb_pack_job_rec(&job, version, buffer);
	length = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	ck_assert_int_eq(slurmdb_unpack_job_rec((void **) &out, version,
						buffer),
			 SLURM_SUCCESS);
	ck_assert_uint_eq(get_buf_offset(buffer), length);
	if (version >= SLURM_26_11_PROTOCOL_VERSION) {
		ck_assert(data_check_match(job.kv, out->kv, false));
	} else {
		ck_assert_ptr_null(out->kv);
	}

	FREE_NULL_DATA(job.kv);
	slurmdb_destroy_job_rec(out);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_kv_accounting_messages)
{
	dbd_job_start_msg_t start = { 0 };
	dbd_job_comp_msg_t complete = { 0 };
	slurmdbd_msg_t send = { 0 }, recv = { 0 };
	data_t **kv = NULL, *received = NULL;
	uint16_t version = versions[(_i / 2) % 4];
	buf_t *buffer = NULL;
	uint32_t length = 0;

	if (_i / 8) {
		send.msg_type = DBD_JOB_COMPLETE;
		send.data = &complete;
		kv = &complete.kv;
	} else {
		start.node_inx = "";
		send.msg_type = DBD_JOB_START;
		send.data = &start;
		kv = &start.kv;
	}
	if (_i % 2)
		*kv = _document();
	buffer = pack_slurmdbd_msg(&send, version);
	ck_assert_ptr_nonnull(buffer);
	length = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	ck_assert_int_eq(unpack_slurmdbd_msg(&recv, version, buffer),
			 SLURM_SUCCESS);
	ck_assert_uint_eq(get_buf_offset(buffer), length);
	received = (_i / 8) ? ((dbd_job_comp_msg_t *) recv.data)->kv :
			      ((dbd_job_start_msg_t *) recv.data)->kv;
	if (version >= SLURM_26_11_PROTOCOL_VERSION) {
		ck_assert_ptr_nonnull(received);
		if (*kv) {
			ck_assert(data_check_match(*kv, received, false));
		} else {
			ck_assert_int_eq(data_get_type(received),
					 DATA_TYPE_NULL);
		}
	} else {
		ck_assert_ptr_null(received);
	}

	FREE_NULL_DATA(*kv);
	slurmdbd_free_msg(&recv);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

START_TEST(test_kv_malformed)
{
	slurm_msg_t msg = {
		.msg_type = REQUEST_UPDATE_NODE,
		.protocol_version = SLURM_26_11_PROTOCOL_VERSION,
	};
	buf_t *buffer = init_buf(0);

	packnull(buffer);
	packnull(buffer);
	pack8(0xff, buffer);
	set_buf_offset(buffer, 0);
	ck_assert_int_ne(unpack_msg(&msg, buffer), SLURM_SUCCESS);
	ck_assert_ptr_null(msg.data);
	FREE_NULL_BUFFER(buffer);
}

END_TEST

extern int main(void)
{
	Suite *suite = suite_create("kv_protocol");
	TCase *tcase = tcase_create("kv_protocol");
	SRunner *runner = NULL;
	int failures = 0;

	tcase_add_loop_test(tcase, test_kv_update_requests, 0, 128);
	tcase_add_loop_test(tcase, test_kv_operation_wire_width, 0,
			    KV_OP_INVALID);
	tcase_add_loop_test(tcase, test_kv_partition_state, 0, 4);
	tcase_add_loop_test(tcase, test_kv_accounting_reservation, 0, 8);
	tcase_add_loop_test(tcase, test_kv_accounting_job, 0, 4);
	tcase_add_loop_test(tcase, test_kv_accounting_messages, 0, 16);
	tcase_add_test(tcase, test_kv_malformed);
	suite_add_tcase(suite, tcase);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_VERBOSE);
	failures = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failures;
}
