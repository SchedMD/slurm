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

	slurmdb_clus_res_rec_t *clus_res_rec = xmalloc(sizeof(slurmdb_clus_res_rec_t));
	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_clus_res_rec_t *acr;

	slurmdb_pack_clus_res_rec((void **)&clus_res_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_clus_res_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);

	free_buf(buf);
	slurmdb_destroy_clus_res_rec(clus_res_rec);
}
END_TEST



START_TEST(pack_1702_null_clus_res_rec)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_clus_res_rec_t pack_crr = {0};

	slurmdb_pack_clus_res_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_clus_res_rec_t *unpack_crr;
	rc = slurmdb_unpack_clus_res_rec((void **)&unpack_crr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc               == SLURM_SUCCESS);
	ck_assert(pack_crr.cluster == unpack_crr->cluster);

	/* when given a NULL pointer, the pack function sets allowed to
	 * NO_VAL, not 0. */
	ck_assert(NO_VAL == unpack_crr->allowed);

	free_buf(buf);
	slurmdb_destroy_clus_res_rec(unpack_crr);
}
END_TEST

START_TEST(pack_1702_clus_res_rec)
{
	int rc;

	slurmdb_clus_res_rec_t *pack_crr = xmalloc(sizeof(slurmdb_clus_res_rec_t));
	pack_crr->allowed = 12;
	pack_crr->cluster         = xstrdup("Diogenes");

	buf_t *buf = init_buf(1024);
	slurmdb_pack_clus_res_rec(pack_crr, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_clus_res_rec_t *unpack_crr;
	rc = slurmdb_unpack_clus_res_rec((void **)&unpack_crr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                        == SLURM_SUCCESS);
	ck_assert(pack_crr->allowed == unpack_crr->allowed);
	ck_assert_str_eq(pack_crr->cluster, unpack_crr->cluster);

	free_buf(buf);
	slurmdb_destroy_clus_res_rec(pack_crr);
	slurmdb_destroy_clus_res_rec(unpack_crr);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("Pack slurmdb_clus_res_rec_t");
	TCase *tc_core = tcase_create("Pack slurmdb_clus_res_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_clus_res_rec);
	tcase_add_test(tc_core, pack_1702_null_clus_res_rec);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(suite());

	//srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
