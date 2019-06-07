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

	slurmdb_federation_rec_t *federation_rec = xmalloc(sizeof(slurmdb_federation_rec_t));
	Buf buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *acr;

	slurmdb_pack_federation_rec((void **)&federation_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_federation_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);

	free_buf(buf);
	slurmdb_destroy_federation_rec(federation_rec);
}
END_TEST

START_TEST(pack_back2_null_federation_rec)
{
	int rc;
	Buf buf = init_buf(1024);

	slurmdb_pack_federation_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr = NULL;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc        == SLURM_SUCCESS);
	ck_assert(unpack_fr == NULL);

	free_buf(buf);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST

START_TEST(pack_back2_federation_rec)
{
	int rc;

	slurmdb_federation_rec_t *pack_fr = xmalloc(sizeof(slurmdb_federation_rec_t));
	pack_fr->flags        = 7;
	pack_fr->name         = xstrdup("Saint Augustine");
	pack_fr->cluster_list = list_create(slurmdb_destroy_cluster_rec);

	slurmdb_cluster_rec_t *x = xmalloc(sizeof(slurmdb_cluster_rec_t));
	x->name = xstrdup("Thomas Aquinas");

	list_append(pack_fr->cluster_list, x);

	Buf buf = init_buf(1024);
	slurmdb_pack_federation_rec(pack_fr, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_fr->flags        == unpack_fr->flags);
	ck_assert_str_eq(pack_fr->name, unpack_fr->name);

	slurmdb_cluster_rec_t *before = (slurmdb_cluster_rec_t *)list_peek(pack_fr->cluster_list);
 	slurmdb_cluster_rec_t *after  = (slurmdb_cluster_rec_t *)list_peek(unpack_fr->cluster_list);
	ck_assert_str_eq(before->name, after->name);

	free_buf(buf);
	slurmdb_destroy_federation_rec(pack_fr);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST


START_TEST(pack_back2_federation_rec_empty_list)
{
	int rc;

	slurmdb_federation_rec_t *pack_fr = xmalloc(sizeof(slurmdb_federation_rec_t));
	pack_fr->flags        = 7;
	pack_fr->name         = xstrdup("Saint Augustine");
	pack_fr->cluster_list = NULL;

	Buf buf = init_buf(1024);
	slurmdb_pack_federation_rec(pack_fr, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert_str_eq(pack_fr->name,    unpack_fr->name);
	ck_assert(pack_fr->flags        == unpack_fr->flags);
	ck_assert(pack_fr->cluster_list == unpack_fr->cluster_list);

	free_buf(buf);
	slurmdb_destroy_federation_rec(pack_fr);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST


START_TEST(pack_back1_null_federation_rec)
{
	int rc;
	Buf buf = init_buf(1024);

	slurmdb_pack_federation_rec(NULL, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr = NULL;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);
	ck_assert(rc        == SLURM_SUCCESS);
	ck_assert(unpack_fr == NULL);

	free_buf(buf);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST

START_TEST(pack_back1_federation_rec)
{
	int rc;

	slurmdb_federation_rec_t *pack_fr = xmalloc(sizeof(slurmdb_federation_rec_t));
	pack_fr->flags        = 7;
	pack_fr->name         = xstrdup("Saint Augustine");
	pack_fr->cluster_list = list_create(slurmdb_destroy_cluster_rec);

	slurmdb_cluster_rec_t *x = xmalloc(sizeof(slurmdb_cluster_rec_t));
	x->name = xstrdup("Thomas Aquinas");

	list_append(pack_fr->cluster_list, x);

	Buf buf = init_buf(1024);
	slurmdb_pack_federation_rec(pack_fr, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_fr->flags        == unpack_fr->flags);
	ck_assert_str_eq(pack_fr->name, unpack_fr->name);

	slurmdb_cluster_rec_t *before = (slurmdb_cluster_rec_t *)list_peek(pack_fr->cluster_list);
 	slurmdb_cluster_rec_t *after  = (slurmdb_cluster_rec_t *)list_peek(unpack_fr->cluster_list);
	ck_assert_str_eq(before->name, after->name);

	free_buf(buf);
	slurmdb_destroy_federation_rec(pack_fr);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST


START_TEST(pack_back1_federation_rec_empty_list)
{
	int rc;

	slurmdb_federation_rec_t *pack_fr = xmalloc(sizeof(slurmdb_federation_rec_t));
	pack_fr->flags        = 7;
	pack_fr->name         = xstrdup("Saint Augustine");
	pack_fr->cluster_list = NULL;

	Buf buf = init_buf(1024);
	slurmdb_pack_federation_rec(pack_fr, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_federation_rec_t *unpack_fr;
	rc = slurmdb_unpack_federation_rec((void **)&unpack_fr, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert_str_eq(pack_fr->name,    unpack_fr->name);
	ck_assert(pack_fr->flags        == unpack_fr->flags);
	ck_assert(pack_fr->cluster_list == unpack_fr->cluster_list);

	free_buf(buf);
	slurmdb_destroy_federation_rec(pack_fr);
	slurmdb_destroy_federation_rec(unpack_fr);
}
END_TEST



/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_federation_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_federation_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_back2_federation_rec);
	tcase_add_test(tc_core, pack_back2_null_federation_rec);
	tcase_add_test(tc_core, pack_back2_federation_rec_empty_list);

	tcase_add_test(tc_core, pack_back1_federation_rec);
	tcase_add_test(tc_core, pack_back1_null_federation_rec);
	tcase_add_test(tc_core, pack_back1_federation_rec_empty_list);

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
