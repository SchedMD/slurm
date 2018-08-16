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
	slurmdb_cluster_rec_t *rec = NULL;
	Buf buf = init_buf(1024);

	rc = slurmdb_unpack_user_rec((void **)&rec, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);

	free_buf(buf);
}
END_TEST

START_TEST(pack_1702_null_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_user_rec_t pack_rec = {0};

	slurmdb_pack_user_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_user_rec_t *unpack_rec;
	rc = slurmdb_unpack_user_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                     == SLURM_SUCCESS);
	ck_assert(pack_rec.admin_level   == unpack_rec->admin_level);
	ck_assert(pack_rec.assoc_list    == unpack_rec->assoc_list);
	ck_assert(pack_rec.coord_accts   == unpack_rec->coord_accts);
	ck_assert(pack_rec.wckey_list    == unpack_rec->wckey_list);
	ck_assert(pack_rec.uid           == unpack_rec->uid);
	ck_assert(pack_rec.default_acct  == unpack_rec->default_acct);
	ck_assert(pack_rec.default_wckey == unpack_rec->default_wckey);
	ck_assert(pack_rec.name          == unpack_rec->name);
	ck_assert(pack_rec.old_name      == unpack_rec->old_name);

	free_buf(buf);
	slurmdb_destroy_user_rec(unpack_rec);
}
END_TEST

START_TEST(pack_1702_rec)
{
	int rc;
	slurmdb_user_rec_t pack_rec;
	pack_rec.admin_level   = 1;

	slurmdb_assoc_rec_t assoc_rec = {0};
	pack_rec.assoc_list    = list_create(NULL);
	list_append(pack_rec.assoc_list, &assoc_rec);

	slurmdb_coord_rec_t coord_rec = {0};
	pack_rec.coord_accts   = list_create(NULL);
	list_append(pack_rec.coord_accts, &coord_rec);

	slurmdb_wckey_rec_t wckey_rec = {0};
	pack_rec.wckey_list    = list_create(NULL);
	list_append(pack_rec.wckey_list, &wckey_rec);

	pack_rec.default_acct  = xstrdup("default_acct");
	pack_rec.default_wckey = xstrdup("default_wckey");
	pack_rec.name          = xstrdup("name");
	pack_rec.old_name      = xstrdup("old_name");
	pack_rec.uid           = 12345;

	Buf buf = init_buf(1024);
	slurmdb_pack_user_rec(&pack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_user_rec_t *unpack_rec;
	rc = slurmdb_unpack_user_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert_uint_eq(pack_rec.admin_level,            unpack_rec->admin_level);
	ck_assert_int_eq(list_count(pack_rec.assoc_list),  list_count(unpack_rec->assoc_list));
	ck_assert_int_eq(list_count(pack_rec.coord_accts), list_count(unpack_rec->coord_accts));
	ck_assert_int_eq(list_count(pack_rec.wckey_list),  list_count(unpack_rec->wckey_list));
	ck_assert_uint_eq(pack_rec.uid,                    unpack_rec->uid);
	ck_assert_str_eq(pack_rec.default_acct,            unpack_rec->default_acct);
	ck_assert_str_eq(pack_rec.default_wckey,           unpack_rec->default_wckey);
	ck_assert_str_eq(pack_rec.name,                    unpack_rec->name);
	ck_assert_str_eq(pack_rec.old_name,                unpack_rec->old_name);

	free_buf(buf);
	xfree(pack_rec.default_acct);
	xfree(pack_rec.default_wckey);
	xfree(pack_rec.name);
	xfree(pack_rec.old_name);
	slurmdb_destroy_user_rec(unpack_rec);
}
END_TEST

START_TEST(pack_1702_rec_null_ptrs)
{
	slurmdb_user_rec_t pack_rec = {0};
	pack_rec.admin_level   = 1;
	pack_rec.uid           = 12345;

	Buf buf = init_buf(1024);
	slurmdb_pack_user_rec(&pack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_user_rec_t *unpack_rec;
	int rc = slurmdb_unpack_user_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert_uint_eq(pack_rec.admin_level, unpack_rec->admin_level);
	ck_assert(pack_rec.assoc_list    == unpack_rec->assoc_list);
	ck_assert(pack_rec.coord_accts   == unpack_rec->coord_accts);
	ck_assert(pack_rec.wckey_list    == unpack_rec->wckey_list);
	ck_assert(pack_rec.default_acct  == unpack_rec->default_acct);
	ck_assert(pack_rec.default_wckey == unpack_rec->default_wckey);
	ck_assert(pack_rec.name          == unpack_rec->name);
	ck_assert(pack_rec.old_name      == unpack_rec->old_name);
	ck_assert_uint_eq(pack_rec.uid,  unpack_rec->uid);

	free_buf(buf);
	slurmdb_destroy_user_rec(unpack_rec);

}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_user_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_user_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_rec);
	tcase_add_test(tc_core, pack_1702_null_rec);
	tcase_add_test(tc_core, pack_1702_rec_null_ptrs);
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
