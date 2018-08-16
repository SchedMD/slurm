#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/list.h"
#include "src/common/pack.h"

START_TEST(invalid_protocol)
{
	int rc;
	uint32_t x;

	slurmdb_assoc_usage_t *assoc_usage = xmalloc(sizeof(slurmdb_assoc_usage_t));
	Buf buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_assoc_usage_t *au;

	slurmdb_pack_assoc_usage((void *)assoc_usage, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_assoc_usage((void **)&au, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_assoc_usage(assoc_usage);
}
END_TEST

START_TEST(pack_1702_assoc_usage)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_assoc_usage_t *pack_au = xmalloc(sizeof(slurmdb_assoc_usage_t));

	pack_au->children_list          = NULL;
	pack_au->grp_used_tres          = NULL;
	pack_au->grp_used_tres_run_secs = NULL;
	pack_au->grp_used_wall          = 77;
	pack_au->fs_factor              = 0;
	pack_au->level_shares           = 0;
	pack_au->parent_assoc_ptr       = NULL;
	pack_au->fs_assoc_ptr           = NULL;
	pack_au->shares_norm            = 0;
	pack_au->tres_cnt               = 0;
	pack_au->usage_efctv            = 123123;
	pack_au->usage_norm             = 4857;
	pack_au->usage_raw              = 4747;
	pack_au->usage_tres_raw         = NULL;
	pack_au->used_jobs              = 234;
	pack_au->used_submit_jobs       = 433;
	pack_au->level_fs               = 3333;
	pack_au->valid_qos              = NULL;

	slurmdb_pack_assoc_usage(pack_au, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_assoc_usage_t *unpack_au;
	rc = slurmdb_unpack_assoc_usage((void **)&unpack_au, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                              == SLURM_SUCCESS);
	ck_assert(pack_au->children_list          == unpack_au->children_list);
	ck_assert(pack_au->grp_used_tres          == unpack_au->grp_used_tres);
	ck_assert(pack_au->grp_used_tres_run_secs == unpack_au->grp_used_tres_run_secs);
	ck_assert(pack_au->grp_used_wall          == unpack_au->grp_used_wall);
	ck_assert(pack_au->fs_factor              == unpack_au->fs_factor);
	ck_assert(pack_au->level_shares           == unpack_au->level_shares);
	ck_assert(pack_au->parent_assoc_ptr       == unpack_au->parent_assoc_ptr);
	ck_assert(pack_au->fs_assoc_ptr           == unpack_au->fs_assoc_ptr);
	ck_assert(pack_au->shares_norm            == unpack_au->shares_norm);
	ck_assert(pack_au->tres_cnt               == unpack_au->tres_cnt);
	ck_assert(pack_au->usage_efctv            == unpack_au->usage_efctv);
	ck_assert(pack_au->usage_norm             == unpack_au->usage_norm);
	ck_assert(pack_au->usage_raw              == unpack_au->usage_raw);
	ck_assert(pack_au->usage_tres_raw         == unpack_au->usage_tres_raw);
	ck_assert(pack_au->used_jobs              == unpack_au->used_jobs);
	ck_assert(pack_au->used_submit_jobs       == unpack_au->used_submit_jobs);
	ck_assert(pack_au->level_fs               == unpack_au->level_fs);
	ck_assert(pack_au->valid_qos              == unpack_au->valid_qos);

	free_buf(buf);
	slurmdb_destroy_assoc_usage(pack_au);
	slurmdb_destroy_assoc_usage(unpack_au);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_assoc_usage_t");
	TCase* tc_core = tcase_create("Pack slurmdb_assoc_usage_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_assoc_usage);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner* sr = srunner_create(suite());

	//srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
