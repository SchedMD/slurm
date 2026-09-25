/*****************************************************************************\
 *  test_pack_job_desc.c - unit test for packing job_desc_msg_t
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/hash.h"

/*
 * Pack a job descriptor as REQUEST_SUBMIT_BATCH_JOB and unpack it again
 * IN job_desc - job descriptor to pack
 * IN protocol_version - protocol version to pack and unpack with
 * RET unpacked job descriptor (caller must free)
 */
static job_desc_msg_t *_round_trip(job_desc_msg_t *job_desc,
				   uint16_t protocol_version)
{
	buf_t *buffer = init_buf(0);
	slurm_msg_t msg = {
		.msg_type = REQUEST_SUBMIT_BATCH_JOB,
		.protocol_version = protocol_version,
		.data = job_desc,
	};
	uint32_t len = 0;

	ck_assert_int_eq(pack_msg(&msg, buffer), SLURM_SUCCESS);
	len = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);

	msg.data = NULL;
	ck_assert_int_eq(unpack_msg(&msg, buffer), SLURM_SUCCESS);
	ck_assert_ptr_nonnull(msg.data);
	/* every byte packed has to be read back */
	ck_assert_int_eq(get_buf_offset(buffer), len);

	FREE_NULL_BUFFER(buffer);
	return msg.data;
}

/*
 * Create a job descriptor requesting a runtime with runtime specific data
 * RET job descriptor (caller must free)
 */
static job_desc_msg_t *_new_job_desc(void)
{
	job_desc_msg_t *job_desc = xmalloc(sizeof(*job_desc));
	data_t *nested = NULL, *list = NULL;

	slurm_init_job_desc_msg(job_desc);
	job_desc->container = xstrdup("/tmp/bundle");
	job_desc->runtime = xstrdup("runtime/test");
	job_desc->runtime_data = data_set_dict(data_new());

	data_set_string(data_key_set(job_desc->runtime_data, "string"),
			"value");
	data_set_int(data_key_set(job_desc->runtime_data, "int"), 1234);
	nested = data_set_dict(data_key_set(job_desc->runtime_data, "dict"));
	data_set_bool(data_key_set(nested, "bool"), true);
	list = data_set_list(data_key_set(nested, "list"));
	data_set_string(data_list_append(list), "item");

	return job_desc;
}

START_TEST(test_runtime_data)
{
	job_desc_msg_t *job_desc = _new_job_desc();
	job_desc_msg_t *out = _round_trip(job_desc, SLURM_PROTOCOL_VERSION);

	ck_assert_ptr_nonnull(out->runtime_data);
	ck_assert_msg(data_check_match(job_desc->runtime_data,
				       out->runtime_data, false),
		      "runtime_data changed while packing");

	/* the fields next to runtime_data are not disturbed */
	ck_assert_str_eq(out->container, job_desc->container);
	ck_assert_str_eq(out->runtime, job_desc->runtime);

	slurm_free_job_desc_msg(job_desc);
	slurm_free_job_desc_msg(out);
}

END_TEST

START_TEST(test_runtime_data_null)
{
	job_desc_msg_t *job_desc = _new_job_desc();
	job_desc_msg_t *out = NULL;

	FREE_NULL_DATA(job_desc->runtime_data);
	out = _round_trip(job_desc, SLURM_PROTOCOL_VERSION);

	ck_assert_ptr_null(out->runtime_data);
	ck_assert_str_eq(out->runtime, job_desc->runtime);

	slurm_free_job_desc_msg(job_desc);
	slurm_free_job_desc_msg(out);
}

END_TEST

START_TEST(test_runtime_data_26_05)
{
	job_desc_msg_t *job_desc = _new_job_desc();
	job_desc_msg_t *out =
		_round_trip(job_desc, SLURM_26_05_PROTOCOL_VERSION);

	/* runtime_data is new in 26.11 and is not sent to older peers */
	ck_assert_ptr_null(out->runtime_data);
	ck_assert_str_eq(out->container, job_desc->container);
	ck_assert_str_eq(out->runtime, job_desc->runtime);

	slurm_free_job_desc_msg(job_desc);
	slurm_free_job_desc_msg(out);
}

END_TEST

/*
 * Pack a message and report what pack_msg() returned
 * IN msg_type - message type to pack
 * IN data - message data
 * IN protocol_version - protocol version to pack with
 * RET return code of pack_msg()
 */
static int _pack_rc(uint16_t msg_type, void *data, uint16_t protocol_version)
{
	buf_t *buffer = init_buf(0);
	slurm_msg_t msg = {
		.msg_type = msg_type,
		.protocol_version = protocol_version,
		.data = data,
	};
	int rc = pack_msg(&msg, buffer);

	FREE_NULL_BUFFER(buffer);
	return rc;
}

START_TEST(test_runtime_data_unpackable)
{
	job_desc_msg_t *job_desc = _new_job_desc();
	list_t *het_job = list_create(NULL);
	crontab_update_request_msg_t crontab = {
		.jobs = het_job,
	};
	data_t *at = data_set_list(job_desc->runtime_data);

	/* far deeper than pack_data() will ever pack */
	for (int i = 0; i < 128; i++)
		at = data_set_list(data_list_append(at));

	/* the sender fails instead of sending a message the peer rejects */
	ck_assert_int_ne(_pack_rc(REQUEST_SUBMIT_BATCH_JOB, job_desc,
				  SLURM_PROTOCOL_VERSION),
			 SLURM_SUCCESS);

	list_append(het_job, job_desc);
	ck_assert_int_ne(_pack_rc(REQUEST_SUBMIT_BATCH_HET_JOB, het_job,
				  SLURM_PROTOCOL_VERSION),
			 SLURM_SUCCESS);
	ck_assert_int_ne(_pack_rc(REQUEST_UPDATE_CRONTAB, &crontab,
				  SLURM_PROTOCOL_VERSION),
			 SLURM_SUCCESS);

	/* runtime_data is never sent to older peers so it cannot fail */
	ck_assert_int_eq(_pack_rc(REQUEST_SUBMIT_BATCH_JOB, job_desc,
				  SLURM_26_05_PROTOCOL_VERSION),
			 SLURM_SUCCESS);

	FREE_NULL_LIST(het_job);
	slurm_free_job_desc_msg(job_desc);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures = EINVAL;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	TCase *tcase = tcase_create("pack_job_desc");
	Suite *suite = suite_create("pack_job_desc");
	SRunner *sr = NULL;

	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("test_pack_job_desc", log_opts, 0, NULL);

	/* unpacking a job descriptor hashes its script */
	if (slurm_conf_init(NULL) || hash_g_init())
		fatal("unable to load slurm.conf and hash plugins");

	tcase_add_test(tcase, test_runtime_data);
	tcase_add_test(tcase, test_runtime_data_null);
	tcase_add_test(tcase, test_runtime_data_26_05);
	tcase_add_test(tcase, test_runtime_data_unpackable);
	suite_add_tcase(suite, tcase);

	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	hash_g_fini();
	slurm_conf_destroy();
	log_fini();

	return failures;
}
