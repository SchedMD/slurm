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

	slurmdb_accounting_rec_t *accounting_rec = xmalloc(sizeof(slurmdb_accounting_rec_t));
	Buf buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_accounting_rec_t *acr;

	slurmdb_pack_accounting_rec((void **)&accounting_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_accounting_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);

	free_buf(buf);
	slurmdb_destroy_accounting_rec(accounting_rec);
}
END_TEST


START_TEST(pack_1702_null_accounting_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_accounting_rec_t pack_ar = {0};

	slurmdb_pack_accounting_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_accounting_rec_t *unpack_ar;
	rc = slurmdb_unpack_accounting_rec((void **)&unpack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                     == SLURM_SUCCESS);
	ck_assert(pack_ar.alloc_secs     == unpack_ar->alloc_secs);
	ck_assert(pack_ar.id             == unpack_ar->id);
	ck_assert(pack_ar.period_start   == unpack_ar->period_start);
	ck_assert(pack_ar.tres_rec.count == unpack_ar->tres_rec.count);

	free_buf(buf);
	slurmdb_destroy_accounting_rec(unpack_ar);
}
END_TEST

START_TEST(pack_1702_accounting_rec)
{
	int rc;

	slurmdb_accounting_rec_t *pack_ar = xmalloc(sizeof(slurmdb_accounting_rec_t));
	pack_ar->alloc_secs     = 12;
	pack_ar->id            	= 222;
	pack_ar->period_start   = 0;
	pack_ar->tres_rec.count = 53;

	Buf buf = init_buf(1024);
	slurmdb_pack_accounting_rec(pack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_accounting_rec_t *unpack_ar;
	rc = slurmdb_unpack_accounting_rec((void **)&unpack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                      == SLURM_SUCCESS);
	ck_assert(pack_ar->alloc_secs     == unpack_ar->alloc_secs);
	ck_assert(pack_ar->id             == unpack_ar->id);
	ck_assert(pack_ar->period_start   == unpack_ar->period_start);
	ck_assert(pack_ar->tres_rec.count == unpack_ar->tres_rec.count);

	free_buf(buf);
	slurmdb_destroy_accounting_rec(pack_ar);
	slurmdb_destroy_accounting_rec(unpack_ar);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_accounting_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_accounting_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_accounting_rec);
	tcase_add_test(tc_core, pack_1702_null_accounting_rec);
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
