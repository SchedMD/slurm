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
	slurmdb_account_rec_t *account_rec;
	buf_t *buf = init_buf(1024);


	rc = slurmdb_unpack_account_rec((void **)&account_rec, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	free_buf(buf);
}
END_TEST

START_TEST(pack_1702_null_account_rec)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_account_rec_t pack_ar = {0};

	slurmdb_pack_account_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_account_rec_t *unpack_ar;
	rc = slurmdb_unpack_account_rec((void **)&unpack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_ar.assoc_list    == unpack_ar->assoc_list);
	ck_assert(pack_ar.coordinators  == unpack_ar->coordinators);
	ck_assert(pack_ar.description   == unpack_ar->description);
	ck_assert(pack_ar.name          == unpack_ar->name);
	ck_assert(pack_ar.organization  == unpack_ar->organization);

	free_buf(buf);
	slurmdb_destroy_account_rec(unpack_ar);
}
END_TEST

START_TEST(pack_1702_account_rec)
{
	int rc;

	slurmdb_account_rec_t *pack_ar = xmalloc(sizeof(slurmdb_account_rec_t));
	pack_ar->description           = xstrdup("default_acct");
	pack_ar->name                  = xstrdup("default_name");
	pack_ar->organization          = xstrdup("default_organization");
	pack_ar->assoc_list            = list_create(slurmdb_destroy_assoc_rec);
	pack_ar->coordinators          = list_create(slurmdb_destroy_coord_rec);
	slurmdb_coord_rec_t * j = xmalloc(sizeof(slurmdb_coord_rec_t));
	slurmdb_assoc_rec_t * k = xmalloc(sizeof(slurmdb_assoc_rec_t));

	k->lft    = 88;
	j->name   = xstrdup("Bertrand Russell");
	j->direct = 5;

	list_append(pack_ar->coordinators, (void *)j);
	list_append(pack_ar->assoc_list,   (void *)k);

	buf_t *buf = init_buf(1024);
	slurmdb_pack_account_rec(pack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_account_rec_t *unpack_ar;
	rc = slurmdb_unpack_account_rec((void **)&unpack_ar, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                              == SLURM_SUCCESS);
	ck_assert_str_eq(pack_ar->description ,      unpack_ar->description);
	ck_assert_str_eq(pack_ar->name        ,      unpack_ar->name);
	ck_assert_str_eq(pack_ar->organization,      unpack_ar->organization);
	ck_assert(list_count(pack_ar->assoc_list) == list_count(unpack_ar->assoc_list));

	slurmdb_assoc_rec_t bar          = *(slurmdb_assoc_rec_t *)list_peek(pack_ar->assoc_list);
	slurmdb_assoc_rec_t aar          = *(slurmdb_assoc_rec_t *)list_peek(unpack_ar->assoc_list);
	slurmdb_coord_rec_t bc           = *(slurmdb_coord_rec_t *)list_peek(pack_ar->coordinators);
	slurmdb_coord_rec_t ac           = *(slurmdb_coord_rec_t *)list_peek(unpack_ar->coordinators);

	ck_assert_str_eq(bc.name,            ac.name);
	ck_assert(bc.direct              == ac.direct);
	ck_assert(bar.lft                == aar.lft);

	free_buf(buf);
	slurmdb_destroy_account_rec(pack_ar);
	slurmdb_destroy_account_rec(unpack_ar);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("Pack slurmdb_account_rec_t");
	TCase *tc_core = tcase_create("Pack slurmdb_account_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_account_rec);
	tcase_add_test(tc_core, pack_1702_null_account_rec);
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
