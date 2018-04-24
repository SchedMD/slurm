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
	uint32_t x;

	slurmdb_cluster_accounting_rec_t *cluster_accounting_rec = xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));
	Buf buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_cluster_accounting_rec_t *acar;

	slurmdb_pack_cluster_accounting_rec((void *)cluster_accounting_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_cluster_accounting_rec((void **)&acar, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_cluster_accounting_rec(cluster_accounting_rec);
}
END_TEST


START_TEST(pack_1702_null_cluster_accounting_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_cluster_accounting_rec_t pack_car = {0};

	slurmdb_pack_cluster_accounting_rec(NULL, SLURM_17_02_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_cluster_accounting_rec_t *unpack_car;
	rc = slurmdb_unpack_cluster_accounting_rec((void **)&unpack_car, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	slurmdb_tres_rec_t *btr = &pack_car.tres_rec;
	slurmdb_tres_rec_t *atr = &unpack_car->tres_rec;

	ck_assert(pack_car.alloc_secs   == unpack_car->alloc_secs);
	ck_assert(pack_car.down_secs    == unpack_car->down_secs);
	ck_assert(pack_car.idle_secs    == unpack_car->idle_secs);
	ck_assert(pack_car.over_secs    == unpack_car->over_secs);
	ck_assert(pack_car.pdown_secs   == unpack_car->pdown_secs);
	ck_assert(pack_car.period_start == unpack_car->period_start);
	ck_assert(pack_car.resv_secs    == unpack_car->resv_secs);

	ck_assert(btr->alloc_secs       == atr->alloc_secs);
	ck_assert(btr->rec_count        == atr->rec_count);
	ck_assert(btr->count            == atr->count);
	ck_assert(btr->id               == atr->id);
	ck_assert(btr->name             == atr->name);
	ck_assert(btr->type             == atr->type);

	free_buf(buf);
	slurmdb_destroy_cluster_accounting_rec(unpack_car);
}
END_TEST



/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_cluster_accounting_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_cluster_accounting_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_null_cluster_accounting_rec);
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
