#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/list.h"
#include "src/common/pack.h"

#ifndef NDEBUG
START_TEST(pack_null_usage)
{
	int rc;
	uint32_t x;

	slurmdb_assoc_rec_t *assoc_rec = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc_rec->usage = NULL;

	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_assoc_rec_t *acr;

	/* Should assert */
	slurmdb_pack_assoc_rec_with_usage((void *)assoc_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_assoc_rec_with_usage((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_assoc_rec(assoc_rec);
}
END_TEST
#endif

START_TEST(invalid_protocol)
{
	int rc;
	uint32_t x;

	slurmdb_assoc_rec_t *assoc_rec = xmalloc(sizeof(slurmdb_assoc_rec_t));
	assoc_rec->usage = xmalloc(sizeof(slurmdb_assoc_usage_t));

	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_assoc_rec_t *acr;

	slurmdb_pack_assoc_rec_with_usage((void *)assoc_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_assoc_rec_with_usage((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_assoc_rec(assoc_rec);
}
END_TEST


START_TEST(pack_1702_assoc_rec)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_assoc_rec_t *pack_arwu   = xmalloc(sizeof(slurmdb_assoc_rec_t));
	slurmdb_accounting_rec_t *art  = xmalloc(sizeof(slurmdb_accounting_rec_t));

	art->id   = 66;
	char *qos = xstrdup("Eusebius");

	pack_arwu->accounting_list	   = list_create(slurmdb_destroy_accounting_rec);
	pack_arwu->acct           	   = xstrdup("Socrates");
	pack_arwu->assoc_next     	   = xmalloc(sizeof(slurmdb_assoc_rec_t));
	pack_arwu->assoc_next->rgt         = 11;
	pack_arwu->assoc_next->grp_jobs    = 22;
	pack_arwu->assoc_next_id  	   = xmalloc(sizeof(slurmdb_assoc_rec_t));
	pack_arwu->assoc_next_id->grp_jobs = 33;
	pack_arwu->cluster        	   = xstrdup("Parmenides");
	pack_arwu->def_qos_id   	   = 1;
	pack_arwu->grp_jobs                = 2;
	pack_arwu->grp_submit_jobs         = 3;
	pack_arwu->grp_tres                = xstrdup("Parmenides");
	pack_arwu->grp_tres_ctld           = NULL;
	pack_arwu->grp_tres_mins           = xstrdup("Parmenides");
	pack_arwu->grp_tres_run_mins       = xstrdup("Parmenides");
	pack_arwu->grp_tres_run_mins_ctld  = NULL;
	pack_arwu->grp_wall                = 6;
	pack_arwu->id                      = 7;
	pack_arwu->is_def                  = 8;
	pack_arwu->lft                     = 9;
	pack_arwu->max_jobs                = 1;
	pack_arwu->max_submit_jobs         = 2;
	pack_arwu->max_tres_mins_pj        = xstrdup("Parmenides");
	pack_arwu->max_tres_mins_ctld      = NULL;
	pack_arwu->max_tres_run_mins       = xstrdup("Parmenides");
	pack_arwu->max_tres_run_mins_ctld  = NULL;
	pack_arwu->max_tres_pj             = xstrdup("Parmenides");
	pack_arwu->max_tres_ctld           = NULL;
	pack_arwu->max_tres_pn             = xstrdup("Parmenides");
	pack_arwu->max_tres_pn_ctld        = NULL;
	pack_arwu->max_wall_pj             = 7;
	pack_arwu->parent_acct             = xstrdup("Parmenides");
	pack_arwu->parent_id               = 8;
	pack_arwu->partition               = xstrdup("Parmenides");
	pack_arwu->qos_list                = list_create(xfree_ptr);
	pack_arwu->rgt                     = 9;
	pack_arwu->shares_raw              = 1;
	pack_arwu->uid                     = 2;
	pack_arwu->usage                   = xmalloc(sizeof(slurmdb_assoc_usage_t));
	pack_arwu->user                    = xstrdup("Parmenides");


	pack_arwu->usage->children_list          = NULL;
	pack_arwu->usage->grp_used_tres          = NULL;
	pack_arwu->usage->grp_used_tres_run_secs = NULL;
	pack_arwu->usage->grp_used_wall          = 77;
	pack_arwu->usage->fs_factor              = 0;
	pack_arwu->usage->level_shares           = 0;
	pack_arwu->usage->parent_assoc_ptr       = NULL;
	pack_arwu->usage->fs_assoc_ptr           = NULL;
	pack_arwu->usage->shares_norm            = 0;
	pack_arwu->usage->tres_cnt               = 0;
	pack_arwu->usage->usage_efctv            = 123123;
	pack_arwu->usage->usage_norm             = 4857;
	pack_arwu->usage->usage_raw              = 4747;
	pack_arwu->usage->usage_tres_raw         = NULL;
	pack_arwu->usage->used_jobs              = 234;
	pack_arwu->usage->used_submit_jobs       = 433;
	pack_arwu->usage->level_fs               = 3333;
	pack_arwu->usage->valid_qos              = NULL;


	list_append(pack_arwu->accounting_list, (void *)art);
	list_append(pack_arwu->qos_list       , (void *)qos);


	slurmdb_pack_assoc_rec_with_usage(pack_arwu, SLURM_MIN_PROTOCOL_VERSION, buf);
	set_buf_offset(buf, 0);
	slurmdb_assoc_rec_t *unpack_arwu;
	rc = slurmdb_unpack_assoc_rec_with_usage((void **)&unpack_arwu, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    	       == SLURM_SUCCESS);
	ck_assert_str_eq(pack_arwu->acct,                 unpack_arwu->acct);
	ck_assert(NULL                   	       == unpack_arwu->assoc_next);
	ck_assert(NULL                 	               == unpack_arwu->assoc_next_id);
	ck_assert_str_eq(pack_arwu->cluster,              unpack_arwu->cluster);
	ck_assert(pack_arwu->def_qos_id                == unpack_arwu->def_qos_id);
	ck_assert(pack_arwu->grp_jobs                  == unpack_arwu->grp_jobs);
	ck_assert(pack_arwu->grp_submit_jobs           == unpack_arwu->grp_submit_jobs);
	ck_assert_str_eq(pack_arwu->grp_tres,             unpack_arwu->grp_tres);
	ck_assert(pack_arwu->grp_tres_ctld  	       == unpack_arwu->grp_tres_ctld);
	ck_assert_str_eq(pack_arwu->grp_tres_run_mins,    unpack_arwu->grp_tres_run_mins);
	ck_assert(pack_arwu->grp_wall                  == unpack_arwu->grp_wall);
	ck_assert(pack_arwu->id                        == unpack_arwu->id);
	ck_assert(pack_arwu->is_def                    == unpack_arwu->is_def);
	ck_assert(pack_arwu->lft                       == unpack_arwu->lft);
	ck_assert(pack_arwu->max_jobs                  == unpack_arwu->max_jobs);
	ck_assert(pack_arwu->max_submit_jobs           == unpack_arwu->max_submit_jobs);
	ck_assert_str_eq(pack_arwu->max_tres_mins_pj,     unpack_arwu->max_tres_mins_pj);
	ck_assert(pack_arwu->max_tres_mins_ctld        == unpack_arwu->max_tres_mins_ctld);
	ck_assert_str_eq(pack_arwu->max_tres_run_mins,    unpack_arwu->max_tres_run_mins);
	ck_assert(pack_arwu->max_tres_run_mins_ctld    == unpack_arwu->max_tres_run_mins_ctld);
	ck_assert_str_eq(pack_arwu->max_tres_pj,          unpack_arwu->max_tres_pj);
	ck_assert(pack_arwu->max_tres_ctld             == unpack_arwu->max_tres_ctld);
	ck_assert_str_eq(pack_arwu->max_tres_pn,          unpack_arwu->max_tres_pn);
	ck_assert(pack_arwu->max_tres_pn_ctld          == unpack_arwu->max_tres_pn_ctld);
	ck_assert(pack_arwu->max_wall_pj               == unpack_arwu->max_wall_pj);
	ck_assert_str_eq(pack_arwu->parent_acct,          unpack_arwu->parent_acct);
	ck_assert(pack_arwu->parent_id                 == unpack_arwu->parent_id);
	ck_assert_str_eq(pack_arwu->partition,            unpack_arwu->partition);
	ck_assert(pack_arwu->rgt                       == unpack_arwu->rgt);
	ck_assert(pack_arwu->shares_raw                == unpack_arwu->shares_raw);
	ck_assert(pack_arwu->uid                       == unpack_arwu->uid);
	ck_assert_str_eq(pack_arwu->user,                 unpack_arwu->user);

	slurmdb_accounting_rec_t *b = (slurmdb_accounting_rec_t *)list_peek(pack_arwu->accounting_list);
	slurmdb_accounting_rec_t *a = (slurmdb_accounting_rec_t *)list_peek(pack_arwu->accounting_list);

	char *before = (char *)list_peek(pack_arwu->qos_list);
	char *after  = (char *)list_peek(unpack_arwu->qos_list);

	ck_assert(b->id == a->id);

	ck_assert_str_eq(before, after);

	ck_assert(pack_arwu->usage->children_list          == unpack_arwu->usage->children_list);
	ck_assert(pack_arwu->usage->grp_used_tres          == unpack_arwu->usage->grp_used_tres);
	ck_assert(pack_arwu->usage->grp_used_tres_run_secs == unpack_arwu->usage->grp_used_tres_run_secs);
	ck_assert(pack_arwu->usage->grp_used_wall          == unpack_arwu->usage->grp_used_wall);
	ck_assert(pack_arwu->usage->fs_factor              == unpack_arwu->usage->fs_factor);
	ck_assert(pack_arwu->usage->level_shares           == unpack_arwu->usage->level_shares);
	ck_assert(pack_arwu->usage->parent_assoc_ptr       == unpack_arwu->usage->parent_assoc_ptr);
	ck_assert(pack_arwu->usage->fs_assoc_ptr           == unpack_arwu->usage->fs_assoc_ptr);
	ck_assert(pack_arwu->usage->shares_norm            == unpack_arwu->usage->shares_norm);
	ck_assert(pack_arwu->usage->tres_cnt               == unpack_arwu->usage->tres_cnt);
	ck_assert(pack_arwu->usage->usage_efctv            == unpack_arwu->usage->usage_efctv);
	ck_assert(pack_arwu->usage->usage_norm             == unpack_arwu->usage->usage_norm);
	ck_assert(pack_arwu->usage->usage_raw              == unpack_arwu->usage->usage_raw);
	ck_assert(pack_arwu->usage->usage_tres_raw         == unpack_arwu->usage->usage_tres_raw);
	ck_assert(pack_arwu->usage->used_jobs              == unpack_arwu->usage->used_jobs);
	ck_assert(pack_arwu->usage->used_submit_jobs       == unpack_arwu->usage->used_submit_jobs);
	ck_assert(pack_arwu->usage->level_fs               == unpack_arwu->usage->level_fs);
	ck_assert(pack_arwu->usage->valid_qos              == unpack_arwu->usage->valid_qos);



	free_buf(buf);
	xfree(pack_arwu->assoc_next);
	xfree(pack_arwu->assoc_next_id);
	xfree(pack_arwu->usage);
	slurmdb_destroy_assoc_rec(pack_arwu);
	slurmdb_destroy_assoc_rec(unpack_arwu);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("Pack slurmdb_assoc_rec_t");
	TCase *tc_core = tcase_create("Pack slurmdb_assoc_rec_t");
	tcase_add_test(tc_core, invalid_protocol);

#ifndef NDEBUG
       if (srunner_fork_status(sr) != CK_NOFORK)
               tcase_add_test_raise_signal(tc_core, pack_null_usage, SIGABRT);
#endif

	tcase_add_test(tc_core, pack_1702_assoc_rec);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
    int number_failed;
    SRunner *sr = srunner_create(NULL);
    //srunner_set_fork_status(sr, CK_NOFORK);
    srunner_add_suite(sr, suite(sr));

    srunner_run_all(sr, CK_VERBOSE);
    //srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
