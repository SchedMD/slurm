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

	slurmdb_coord_rec_t *coord_rec = xmalloc(sizeof(slurmdb_coord_rec_t));
	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_coord_rec_t *acr;

	slurmdb_pack_coord_rec((void **)&coord_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_coord_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_coord_rec(coord_rec);
}
END_TEST



START_TEST(pack_1702_null_coord_rec)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_coord_rec_t pack_cr = {0};

	slurmdb_pack_coord_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_coord_rec_t *unpack_cr;
	rc = slurmdb_unpack_coord_rec((void **)&unpack_cr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_cr.name          == unpack_cr->name);
	ck_assert(pack_cr.direct        == unpack_cr->direct);

	free_buf(buf);
	slurmdb_destroy_coord_rec(unpack_cr);
}
END_TEST

START_TEST(pack_1702_coord_rec)
{
	int rc;

	slurmdb_coord_rec_t *pack_cr = xmalloc(sizeof(slurmdb_coord_rec_t));
	pack_cr->direct                = 12;
	pack_cr->name                  = xstrdup("Gottlob Frege");

	buf_t *buf = init_buf(1024);
	slurmdb_pack_coord_rec(pack_cr, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_coord_rec_t *unpack_cr;
	rc = slurmdb_unpack_coord_rec((void **)&unpack_cr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                        == SLURM_SUCCESS);
	ck_assert_str_eq(pack_cr->name,        unpack_cr->name);
	ck_assert(pack_cr->direct           == unpack_cr->direct);

	free_buf(buf);
	slurmdb_destroy_coord_rec(pack_cr);
	slurmdb_destroy_coord_rec(unpack_cr);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("Pack slurmdb_coord_rec_t");
	TCase *tc_core = tcase_create("Pack slurmdb_coord_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_coord_rec);
	tcase_add_test(tc_core, pack_1702_null_coord_rec);
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
