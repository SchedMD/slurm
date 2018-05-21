#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/list.h"
#include "src/common/pack.h"

START_TEST(invalid_protocol)
{
	int rc;
	uint32_t x;

	slurmdb_assoc_rec_t *assoc_rec = xmalloc(sizeof(slurmdb_assoc_rec_t));
	Buf buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_assoc_rec_t *acr;

	slurmdb_pack_assoc_rec((void **)&assoc_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_assoc_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);

	free_buf(buf);
	slurmdb_destroy_assoc_rec(assoc_rec);
}
END_TEST


START_TEST(pack_1702_null_assoc_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_assoc_rec_t pack_ar = {0};

	slurmdb_pack_assoc_rec(NULL, SLURM_17_02_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_assoc_rec_t *unpack_ar;
	rc = slurmdb_unpack_assoc_rec((void **)&unpack_ar, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc                     	 == SLURM_SUCCESS);
	ck_assert(pack_ar.accounting_list	 == unpack_ar->accounting_list);
	ck_assert(pack_ar.acct           	 == unpack_ar->acct);
	ck_assert(pack_ar.assoc_next     	 == unpack_ar->assoc_next);
	ck_assert(pack_ar.assoc_next_id  	 == unpack_ar->assoc_next_id);
	ck_assert(pack_ar.cluster        	 == unpack_ar->cluster);
	ck_assert(NO_VAL                 	 == unpack_ar->def_qos_id);
	ck_assert(NO_VAL                 	 == unpack_ar->grp_jobs);
	ck_assert(NO_VAL                	 == unpack_ar->grp_submit_jobs);
	ck_assert(pack_ar.grp_tres       	 == unpack_ar->grp_tres);
	ck_assert(pack_ar.grp_tres_ctld  	 == unpack_ar->grp_tres_ctld);
	ck_assert(pack_ar.grp_tres_run_mins      == unpack_ar->grp_tres_run_mins);
	ck_assert(NO_VAL                         == unpack_ar->grp_wall);
	ck_assert(pack_ar.id                     == unpack_ar->id);
	ck_assert(pack_ar.is_def                 == unpack_ar->is_def);
	ck_assert(pack_ar.lft                    == unpack_ar->lft);
	ck_assert(NO_VAL                         == unpack_ar->max_jobs);
	ck_assert(NO_VAL                         == unpack_ar->max_submit_jobs);
	ck_assert(pack_ar.max_tres_mins_pj       == unpack_ar->max_tres_mins_pj);
	ck_assert(pack_ar.max_tres_mins_ctld     == unpack_ar->max_tres_mins_ctld);
	ck_assert(pack_ar.max_tres_run_mins      == unpack_ar->max_tres_run_mins);
	ck_assert(pack_ar.max_tres_run_mins_ctld == unpack_ar->max_tres_run_mins_ctld);
	ck_assert(pack_ar.max_tres_pj            == unpack_ar->max_tres_pj);
	ck_assert(pack_ar.max_tres_ctld          == unpack_ar->max_tres_ctld);
	ck_assert(pack_ar.max_tres_pn            == unpack_ar->max_tres_pn);
	ck_assert(pack_ar.max_tres_pn_ctld       == unpack_ar->max_tres_pn_ctld);
	ck_assert(NO_VAL                         == unpack_ar->max_wall_pj);
	ck_assert(pack_ar.parent_acct            == unpack_ar->parent_acct);
	ck_assert(pack_ar.parent_id              == unpack_ar->parent_id);
	ck_assert(pack_ar.partition              == unpack_ar->partition);
	ck_assert(pack_ar.qos_list               == unpack_ar->qos_list);
	ck_assert(pack_ar.rgt                    == unpack_ar->rgt);
	ck_assert(NO_VAL                         == unpack_ar->shares_raw);
	ck_assert(pack_ar.uid                    == unpack_ar->uid);
	ck_assert(pack_ar.usage                  == unpack_ar->usage);
	ck_assert(pack_ar.user                   == unpack_ar->user);

	free_buf(buf);
	slurmdb_destroy_assoc_rec(unpack_ar);
}
END_TEST

START_TEST(pack_1702_assoc_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_assoc_rec_t *pack_ar   = xmalloc(sizeof(slurmdb_assoc_rec_t));
	slurmdb_accounting_rec_t *art  = xmalloc(sizeof(slurmdb_accounting_rec_t));

	art->id   = 66;
	char *qos = xstrdup("Eusebius");

	pack_ar->accounting_list	 = list_create(slurmdb_destroy_accounting_rec);
	pack_ar->acct           	 = xstrdup("Socrates");
	pack_ar->assoc_next     	 = xmalloc(sizeof(slurmdb_assoc_rec_t));
	pack_ar->assoc_next->rgt         = 11;
	pack_ar->assoc_next->grp_jobs    = 22;
	pack_ar->assoc_next_id  	 = xmalloc(sizeof(slurmdb_assoc_rec_t));
	pack_ar->assoc_next_id->grp_jobs = 33;
	pack_ar->cluster        	 = xstrdup("Parmenides");
	pack_ar->def_qos_id   	         = 1;
	pack_ar->grp_jobs                = 2;
	pack_ar->grp_submit_jobs         = 3;
	pack_ar->grp_tres                = xstrdup("Parmenides");
	pack_ar->grp_tres_ctld           = NULL;
	pack_ar->grp_tres_mins           = xstrdup("Parmenides");
	pack_ar->grp_tres_run_mins       = xstrdup("Parmenides");
	pack_ar->grp_tres_run_mins_ctld  = NULL;
	pack_ar->grp_wall                = 6;
	pack_ar->id                      = 7;
	pack_ar->is_def                  = 8;
	pack_ar->lft                     = 9;
	pack_ar->max_jobs                = 1;
	pack_ar->max_submit_jobs         = 2;
	pack_ar->max_tres_mins_pj        = xstrdup("Parmenides");
	pack_ar->max_tres_mins_ctld      = NULL;
	pack_ar->max_tres_run_mins       = xstrdup("Parmenides");
	pack_ar->max_tres_run_mins_ctld  = NULL;
	pack_ar->max_tres_pj             = xstrdup("Parmenides");
	pack_ar->max_tres_ctld           = NULL;
	pack_ar->max_tres_pn             = xstrdup("Parmenides");
	pack_ar->max_tres_pn_ctld        = NULL;
	pack_ar->max_wall_pj             = 7;
	pack_ar->parent_acct             = xstrdup("Parmenides");
	pack_ar->parent_id               = 8;
	pack_ar->partition               = xstrdup("Parmenides");
	pack_ar->qos_list                = list_create(slurm_destroy_char);
	pack_ar->rgt                     = 9;
	pack_ar->shares_raw              = 1;
	pack_ar->uid                     = 2;
	pack_ar->usage                   = xmalloc(sizeof(slurmdb_assoc_usage_t));
	pack_ar->user                    = xstrdup("Parmenides");

	list_append(pack_ar->accounting_list, (void *)art);
	list_append(pack_ar->qos_list       , (void *)qos);


	slurmdb_pack_assoc_rec(pack_ar, SLURM_17_02_PROTOCOL_VERSION, buf);
	set_buf_offset(buf, 0);
	slurmdb_assoc_rec_t *unpack_ar;
	rc = slurmdb_unpack_assoc_rec((void **)&unpack_ar, SLURM_17_02_PROTOCOL_VERSION, buf);
	ck_assert(rc                    	     == SLURM_SUCCESS);
	ck_assert_str_eq(pack_ar->acct,                 unpack_ar->acct);
	ck_assert(NULL                   	     == unpack_ar->assoc_next);
	ck_assert(NULL                 	             == unpack_ar->assoc_next_id);
	ck_assert_str_eq(pack_ar->cluster,              unpack_ar->cluster);
	ck_assert(pack_ar->def_qos_id         	     == unpack_ar->def_qos_id);
	ck_assert(pack_ar->grp_jobs                  == unpack_ar->grp_jobs);
	ck_assert(pack_ar->grp_submit_jobs           == unpack_ar->grp_submit_jobs);
	ck_assert_str_eq(pack_ar->grp_tres,             unpack_ar->grp_tres);
	ck_assert(pack_ar->grp_tres_ctld  	     == unpack_ar->grp_tres_ctld);
	ck_assert_str_eq(pack_ar->grp_tres_run_mins,    unpack_ar->grp_tres_run_mins);
	ck_assert(pack_ar->grp_wall                  == unpack_ar->grp_wall);
	ck_assert(pack_ar->id                        == unpack_ar->id);
	ck_assert(pack_ar->is_def                    == unpack_ar->is_def);
	ck_assert(pack_ar->lft                       == unpack_ar->lft);
	ck_assert(pack_ar->max_jobs                  == unpack_ar->max_jobs);
	ck_assert(pack_ar->max_submit_jobs           == unpack_ar->max_submit_jobs);
	ck_assert_str_eq(pack_ar->max_tres_mins_pj,     unpack_ar->max_tres_mins_pj);
	ck_assert(pack_ar->max_tres_mins_ctld        == unpack_ar->max_tres_mins_ctld);
	ck_assert_str_eq(pack_ar->max_tres_run_mins,    unpack_ar->max_tres_run_mins);
	ck_assert(pack_ar->max_tres_run_mins_ctld    == unpack_ar->max_tres_run_mins_ctld);
	ck_assert_str_eq(pack_ar->max_tres_pj,          unpack_ar->max_tres_pj);
	ck_assert(pack_ar->max_tres_ctld             == unpack_ar->max_tres_ctld);
	ck_assert_str_eq(pack_ar->max_tres_pn,          unpack_ar->max_tres_pn);
	ck_assert(pack_ar->max_tres_pn_ctld          == unpack_ar->max_tres_pn_ctld);
	ck_assert(pack_ar->max_wall_pj               == unpack_ar->max_wall_pj);
	ck_assert_str_eq(pack_ar->parent_acct,          unpack_ar->parent_acct);
	ck_assert(pack_ar->parent_id                 == unpack_ar->parent_id);
	ck_assert_str_eq(pack_ar->partition,            unpack_ar->partition);
	ck_assert(pack_ar->rgt                       == unpack_ar->rgt);
	ck_assert(pack_ar->shares_raw          	     == unpack_ar->shares_raw);
	ck_assert(pack_ar->uid                       == unpack_ar->uid);
	ck_assert(NULL                               == unpack_ar->usage);
	ck_assert_str_eq(pack_ar->user,                 unpack_ar->user);

	slurmdb_accounting_rec_t *b = (slurmdb_accounting_rec_t *)list_peek(pack_ar->accounting_list);
	slurmdb_accounting_rec_t *a = (slurmdb_accounting_rec_t *)list_peek(pack_ar->accounting_list);

	char *before = (char *)list_peek(pack_ar->qos_list);
	char *after  = (char *)list_peek(unpack_ar->qos_list);

	ck_assert(b->id == a->id);

	ck_assert_str_eq(before, after);

	free_buf(buf);
	xfree(pack_ar->assoc_next);
	xfree(pack_ar->assoc_next_id);
	xfree(pack_ar->usage);
	slurmdb_destroy_assoc_rec(pack_ar);
	slurmdb_destroy_assoc_rec(unpack_ar);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_assoc_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_assoc_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_1702_assoc_rec);
	tcase_add_test(tc_core, pack_1702_null_assoc_rec);
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
