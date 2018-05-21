#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/list.h"

START_TEST(invalid_protocol)
{
	int rc;
	slurmdb_used_limits_t *used_limits;
	Buf buf = init_buf(1024);

	rc = slurmdb_unpack_used_limits((void **)&used_limits, 0, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	free_buf(buf);
}
END_TEST

START_TEST(pack_1702_null_used_limits)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_used_limits_t pack_ul = {0};

	slurmdb_pack_used_limits(NULL, 0, SLURM_17_02_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_used_limits_t *unpack_ul;
	rc = slurmdb_unpack_used_limits((void **)&unpack_ul, 0, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_ul.acct          == unpack_ul->acct);
	ck_assert(pack_ul.jobs          == unpack_ul->jobs);
	ck_assert(pack_ul.submit_jobs   == unpack_ul->submit_jobs);
	ck_assert(pack_ul.tres          == unpack_ul->tres);
	ck_assert(pack_ul.tres_run_mins == unpack_ul->tres_run_mins);
	ck_assert(pack_ul.uid           == unpack_ul->uid);

	free_buf(buf);
	slurmdb_destroy_used_limits(unpack_ul);
}
END_TEST

START_TEST(pack_1702_used_limits)
{
	int rc;
	int i = 0;
	int tres_cnt = 4;

	slurmdb_used_limits_t *pack_ul = xmalloc(sizeof(slurmdb_used_limits_t));
	pack_ul->acct          = xstrdup("default_acct");
	pack_ul->jobs          = 12345;
	pack_ul->submit_jobs   = 11234;
	pack_ul->tres          = xmalloc(tres_cnt * sizeof(uint64_t));
	pack_ul->tres_run_mins = xmalloc(tres_cnt * sizeof(uint64_t));
	pack_ul->uid           = 11123;

	for(int i = 0; i < tres_cnt; i++){
		pack_ul->tres[i] = 5*1;
		pack_ul->tres_run_mins[i] = 10*i;
	}

	Buf buf = init_buf(1024);
	slurmdb_pack_used_limits(pack_ul, tres_cnt, SLURM_17_02_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_used_limits_t *unpack_ul;
	rc = slurmdb_unpack_used_limits((void **)&unpack_ul, tres_cnt, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc == SLURM_SUCCESS);
	ck_assert_str_eq(pack_ul->acct, unpack_ul->acct);
	ck_assert(pack_ul->jobs        == unpack_ul->jobs);
	ck_assert(pack_ul->submit_jobs == unpack_ul->submit_jobs);
	for(i=0; i<tres_cnt; i++){
		ck_assert(pack_ul->tres[i]          == unpack_ul->tres[i]);
		ck_assert(pack_ul->tres_run_mins[i] == unpack_ul->tres_run_mins[i]);
	}
	ck_assert(pack_ul->uid == unpack_ul->uid);

	free_buf(buf);
	slurmdb_destroy_used_limits(pack_ul);
	slurmdb_destroy_used_limits(unpack_ul);
}
END_TEST

START_TEST(pack_1702_used_limits_null_ptrs)
{
	int rc;
	int tres_cnt = 0;

	slurmdb_used_limits_t *pack_ul = xmalloc(sizeof(slurmdb_used_limits_t));

	pack_ul->jobs        = 12345;
	pack_ul->submit_jobs = 11234;
	pack_ul->uid         = 11123;

	Buf buf = init_buf(1024);
	slurmdb_pack_used_limits(pack_ul, tres_cnt, SLURM_17_02_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_used_limits_t *unpack_ul;
	rc = slurmdb_unpack_used_limits((void **)&unpack_ul, tres_cnt, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc == SLURM_SUCCESS);
	ck_assert(pack_ul->acct          == unpack_ul->acct);
	ck_assert(pack_ul->jobs          == unpack_ul->jobs);
	ck_assert(pack_ul->submit_jobs   == unpack_ul->submit_jobs);
	ck_assert(pack_ul->tres          == unpack_ul->tres);
	ck_assert(pack_ul->tres_run_mins == unpack_ul->tres_run_mins);
	ck_assert(pack_ul->uid           == unpack_ul->uid);

	free_buf(buf);
	slurmdb_destroy_used_limits(pack_ul);
	slurmdb_destroy_used_limits(unpack_ul);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_used_limits_t");
	TCase* tc_core = tcase_create("Pack slurmdb_used_limits_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_used_limits);
	tcase_add_test(tc_core, pack_1702_null_used_limits);
	tcase_add_test(tc_core, pack_1702_used_limits_null_ptrs);
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

	srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
