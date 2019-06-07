#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/list.h"
#include "src/common/slurm_persist_conn.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"


START_TEST(invalid_protocol)
{
	int rc;
	slurmdb_cluster_rec_t *rec = NULL;

	rc = slurmdb_unpack_cluster_rec((void **)&rec, 0, NULL);
	ck_assert_int_eq(rc, SLURM_ERROR);
}
END_TEST

START_TEST(pack_back2_null_rec)
{
	int rc;
	slurmdb_cluster_rec_t pack_rec;
	slurmdb_cluster_rec_t *unpack_rec;
	Buf buf = init_buf(1024);

	slurmdb_init_cluster_rec(&pack_rec, false);
	pack_rec.fed.state        = 0;
	pack_rec.dimensions       = 1;
	pack_rec.plugin_id_select = NO_VAL;

	slurmdb_pack_cluster_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(pack_rec.accounting_list == unpack_rec->accounting_list);
	ck_assert(pack_rec.control_host    == unpack_rec->control_host);
	ck_assert(pack_rec.fed.name        == unpack_rec->fed.name);
	ck_assert(pack_rec.name            == unpack_rec->name);
	ck_assert(pack_rec.nodes           == unpack_rec->nodes);
	ck_assert(pack_rec.fed.recv        == unpack_rec->fed.recv);
	ck_assert(pack_rec.fed.send        == unpack_rec->fed.send);
	/* 17.11 */
	ck_assert(pack_rec.fed.feature_list == unpack_rec->fed.feature_list);

	/* root_assoc gets unpacked into a empty structure */
	ck_assert(unpack_rec->root_assoc != NULL);

	ck_assert_uint_eq(pack_rec.classification,   unpack_rec->classification);
	ck_assert_uint_eq(pack_rec.dimensions,       unpack_rec->dimensions);
	ck_assert_uint_eq(pack_rec.fed.id,           unpack_rec->fed.id);
	ck_assert_uint_eq(pack_rec.fed.state,        unpack_rec->fed.state);
	ck_assert_uint_eq(pack_rec.flags,            unpack_rec->flags);
	ck_assert_uint_eq(pack_rec.plugin_id_select, unpack_rec->plugin_id_select);
	ck_assert_uint_eq(pack_rec.rpc_version,      unpack_rec->rpc_version);

	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

START_TEST(pack_back2_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_cluster_rec_t *unpack_rec;
	slurmdb_cluster_rec_t pack_rec = {0};

	slurmdb_cluster_accounting_rec_t acct_rec = {0};
	pack_rec.accounting_list  = list_create(NULL);;
	list_append(pack_rec.accounting_list, &acct_rec);

	pack_rec.classification   = 2;
	pack_rec.control_host     = xstrdup("control_host");
	pack_rec.dimensions       = 3;
	pack_rec.fed.name         = xstrdup("fed_name");
	pack_rec.fed.id           = 4;
	pack_rec.fed.state        = 5;
	pack_rec.flags            = 7;
	pack_rec.name             = xstrdup("name");
	pack_rec.nodes            = xstrdup("nodes");
	pack_rec.plugin_id_select = 8;
	pack_rec.fed.feature_list = list_create(slurm_destroy_char);
	slurm_addto_mode_char_list(pack_rec.fed.feature_list, "a,b,c", 0);
	ck_assert_int_eq(list_count(pack_rec.fed.feature_list), 3);

	/* will be tested separately. */
	pack_rec.root_assoc       = NULL;
	pack_rec.rpc_version      = 9;

	slurm_persist_conn_t p_recv = {0};
	p_recv.fd = 11;
	pack_rec.fed.recv         = &p_recv;

	slurm_persist_conn_t p_send = {0};
	p_send.fd = 10;
	pack_rec.fed.send         = &p_send;


	slurmdb_pack_cluster_rec(&pack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert_uint_eq(list_count(pack_rec.accounting_list), list_count(unpack_rec->accounting_list));
	ck_assert_uint_eq(pack_rec.classification, unpack_rec->classification);
	ck_assert_uint_eq(pack_rec.dimensions, unpack_rec->dimensions);
	ck_assert_uint_eq(pack_rec.fed.id, unpack_rec->fed.id);
	ck_assert_uint_eq(pack_rec.fed.state, unpack_rec->fed.state);
	ck_assert_uint_eq(pack_rec.flags, unpack_rec->flags);
	ck_assert_uint_eq(pack_rec.plugin_id_select, unpack_rec->plugin_id_select);
	ck_assert_uint_eq(pack_rec.rpc_version, unpack_rec->rpc_version);

	ck_assert(unpack_rec->root_assoc != NULL);

	ck_assert(unpack_rec->fed.recv != NULL);
	ck_assert(unpack_rec->fed.send != NULL);
	ck_assert_int_eq(((slurm_persist_conn_t *)unpack_rec->fed.recv)->fd, -1);
	ck_assert_int_eq(((slurm_persist_conn_t *)unpack_rec->fed.send)->fd, -1);

	ck_assert_str_eq(pack_rec.control_host, unpack_rec->control_host);
	ck_assert_str_eq(pack_rec.fed.name, unpack_rec->fed.name);
	ck_assert_str_eq(pack_rec.name, unpack_rec->name);
	ck_assert_str_eq(pack_rec.nodes, unpack_rec->nodes);

	char *feature;
	ck_assert_int_eq(list_count(pack_rec.fed.feature_list), list_count(unpack_rec->fed.feature_list));
	ListIterator itr = list_iterator_create(pack_rec.fed.feature_list);
	while ((feature = list_next(itr))) {
		if (!list_find_first(unpack_rec->fed.feature_list, slurm_find_char_in_list, feature))
			ck_abort_msg("Didn't find feature %s in unpacked list",
				     feature);
	}

	FREE_NULL_LIST(pack_rec.accounting_list);
	xfree(pack_rec.control_host);
	xfree(pack_rec.fed.name);
	xfree(pack_rec.name);
	xfree(pack_rec.nodes);
	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

START_TEST(pack_back2_rec_null_ptrs)
{
	Buf buf = init_buf(1024);
	slurmdb_cluster_rec_t pack_rec = {0};
	pack_rec.accounting_list  = NULL;
	pack_rec.classification   = 2;
	pack_rec.control_host     = NULL;
	pack_rec.dimensions       = 3;
	pack_rec.fed.name         = NULL;
	pack_rec.fed.id           = 4;
	pack_rec.fed.state        = 5;
	pack_rec.flags            = 7;
	pack_rec.name             = NULL;
	pack_rec.nodes            = NULL;
	pack_rec.plugin_id_select = 8;
	pack_rec.root_assoc       = NULL;
	pack_rec.rpc_version      = 9;
	pack_rec.fed.recv         = NULL;
	pack_rec.fed.send         = NULL;

	slurmdb_pack_cluster_rec(&pack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_cluster_rec_t *unpack_rec;
	int rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_MIN_PROTOCOL_VERSION, buf);

	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(pack_rec.accounting_list  == unpack_rec->accounting_list);
	ck_assert(pack_rec.classification   == unpack_rec->classification);
	ck_assert(pack_rec.control_host     == unpack_rec->control_host);
	ck_assert(pack_rec.dimensions       == unpack_rec->dimensions);
	ck_assert(pack_rec.fed.name         == unpack_rec->fed.name);
	ck_assert(pack_rec.fed.id           == unpack_rec->fed.id);
	ck_assert(pack_rec.fed.state        == unpack_rec->fed.state);
	ck_assert(pack_rec.flags            == unpack_rec->flags);
	ck_assert(pack_rec.name             == unpack_rec->name);
	ck_assert(pack_rec.nodes            == unpack_rec->nodes);
	ck_assert(pack_rec.plugin_id_select == unpack_rec->plugin_id_select);

	/* root_assoc gets unpacked into a empty structure */
	ck_assert(unpack_rec->root_assoc    != NULL);
	ck_assert(pack_rec.rpc_version      == unpack_rec->rpc_version);
	ck_assert(pack_rec.fed.recv         == unpack_rec->fed.recv);
	ck_assert(pack_rec.fed.send         == unpack_rec->fed.send);
	ck_assert(pack_rec.fed.feature_list == unpack_rec->fed.feature_list);

	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

START_TEST(pack_back1_null_rec)
{
	int rc;
	slurmdb_cluster_rec_t pack_rec;
	slurmdb_cluster_rec_t *unpack_rec;
	Buf buf = init_buf(1024);

	slurmdb_init_cluster_rec(&pack_rec, false);
	pack_rec.fed.state        = 0;
	pack_rec.dimensions       = 1;
	pack_rec.plugin_id_select = NO_VAL;
	slurmdb_pack_cluster_rec(NULL, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(pack_rec.accounting_list == unpack_rec->accounting_list);
	ck_assert(pack_rec.control_host    == unpack_rec->control_host);
	ck_assert(pack_rec.fed.name        == unpack_rec->fed.name);
	ck_assert(pack_rec.name            == unpack_rec->name);
	ck_assert(pack_rec.nodes           == unpack_rec->nodes);
	ck_assert(pack_rec.fed.recv        == unpack_rec->fed.recv);
	ck_assert(pack_rec.fed.send        == unpack_rec->fed.send);
	ck_assert(pack_rec.fed.feature_list == unpack_rec->fed.feature_list);

	/* root_assoc gets unpacked into a empty structure */
	ck_assert(unpack_rec->root_assoc != NULL);

	ck_assert_uint_eq(pack_rec.classification,   unpack_rec->classification);
	ck_assert_uint_eq(pack_rec.dimensions,       unpack_rec->dimensions);
	ck_assert_uint_eq(pack_rec.fed.id,           unpack_rec->fed.id);
	ck_assert_uint_eq(pack_rec.fed.state,        unpack_rec->fed.state);
	ck_assert_uint_eq(pack_rec.flags,            unpack_rec->flags);
	ck_assert_uint_eq(pack_rec.plugin_id_select, unpack_rec->plugin_id_select);
	ck_assert_uint_eq(pack_rec.rpc_version,      unpack_rec->rpc_version);

	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

START_TEST(pack_back1_rec)
{
	int rc;
	Buf buf = init_buf(1024);
	slurmdb_cluster_rec_t *unpack_rec;
	slurmdb_cluster_rec_t pack_rec = {0};

	slurmdb_cluster_accounting_rec_t acct_rec = {0};
	pack_rec.accounting_list  = list_create(NULL);;
	list_append(pack_rec.accounting_list, &acct_rec);

	pack_rec.classification   = 2;
	pack_rec.control_host     = xstrdup("control_host");
	pack_rec.dimensions       = 3;
	pack_rec.fed.name         = xstrdup("fed_name");
	pack_rec.fed.id           = 4;
	pack_rec.fed.state        = 5;
	pack_rec.flags            = 7;
	pack_rec.name             = xstrdup("name");
	pack_rec.nodes            = xstrdup("nodes");
	pack_rec.plugin_id_select = 8;
	pack_rec.fed.feature_list = list_create(slurm_destroy_char);
	slurm_addto_mode_char_list(pack_rec.fed.feature_list, "a,b,c", 0);
	ck_assert_int_eq(list_count(pack_rec.fed.feature_list), 3);

	/* will be tested separately. */
	pack_rec.root_assoc       = NULL;
	pack_rec.rpc_version      = 9;

	slurm_persist_conn_t p_recv = {0};
	p_recv.fd = 11;
	pack_rec.fed.recv         = &p_recv;

	slurm_persist_conn_t p_send = {0};
	p_send.fd = 10;
	pack_rec.fed.send         = &p_send;


	slurmdb_pack_cluster_rec(&pack_rec, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);
	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert_uint_eq(list_count(pack_rec.accounting_list), list_count(unpack_rec->accounting_list));
	ck_assert_uint_eq(pack_rec.classification, unpack_rec->classification);
	ck_assert_uint_eq(pack_rec.dimensions, unpack_rec->dimensions);
	ck_assert_uint_eq(pack_rec.fed.id, unpack_rec->fed.id);
	ck_assert_uint_eq(pack_rec.fed.state, unpack_rec->fed.state);
	ck_assert_uint_eq(pack_rec.flags, unpack_rec->flags);
	ck_assert_uint_eq(pack_rec.plugin_id_select, unpack_rec->plugin_id_select);
	ck_assert_uint_eq(pack_rec.rpc_version, unpack_rec->rpc_version);

	ck_assert(unpack_rec->root_assoc != NULL);

	ck_assert(unpack_rec->fed.recv != NULL);
	ck_assert(unpack_rec->fed.send != NULL);
	ck_assert_int_eq(((slurm_persist_conn_t *)unpack_rec->fed.recv)->fd, -1);
	ck_assert_int_eq(((slurm_persist_conn_t *)unpack_rec->fed.send)->fd, -1);

	ck_assert_str_eq(pack_rec.control_host, unpack_rec->control_host);
	ck_assert_str_eq(pack_rec.fed.name, unpack_rec->fed.name);
	ck_assert_str_eq(pack_rec.name, unpack_rec->name);
	ck_assert_str_eq(pack_rec.nodes, unpack_rec->nodes);

	char *feature;
	ck_assert_int_eq(list_count(pack_rec.fed.feature_list), list_count(unpack_rec->fed.feature_list));
	ListIterator itr = list_iterator_create(pack_rec.fed.feature_list);
	while ((feature = list_next(itr))) {
		if (!list_find_first(unpack_rec->fed.feature_list, slurm_find_char_in_list, feature))
			ck_abort_msg("Didn't find feature %s in unpacked list",
				     feature);
	}

	FREE_NULL_LIST(pack_rec.accounting_list);
	xfree(pack_rec.control_host);
	xfree(pack_rec.fed.name);
	xfree(pack_rec.name);
	xfree(pack_rec.nodes);
	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

START_TEST(pack_back1_rec_null_ptrs)
{
	Buf buf = init_buf(1024);
	slurmdb_cluster_rec_t pack_rec = {0};
	pack_rec.accounting_list  = NULL;
	pack_rec.classification   = 2;
	pack_rec.control_host     = NULL;
	pack_rec.dimensions       = 3;
	pack_rec.fed.name         = NULL;
	pack_rec.fed.id           = 4;
	pack_rec.fed.state        = 5;
	pack_rec.flags            = 7;
	pack_rec.name             = NULL;
	pack_rec.nodes            = NULL;
	pack_rec.plugin_id_select = 8;
	pack_rec.root_assoc       = NULL;
	pack_rec.rpc_version      = 9;
	pack_rec.fed.recv         = NULL;
	pack_rec.fed.send         = NULL;

	slurmdb_pack_cluster_rec(&pack_rec, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_cluster_rec_t *unpack_rec;
	int rc = slurmdb_unpack_cluster_rec((void **)&unpack_rec, SLURM_ONE_BACK_PROTOCOL_VERSION, buf);

	ck_assert_int_eq(rc, SLURM_SUCCESS);
	ck_assert(pack_rec.accounting_list  == unpack_rec->accounting_list);
	ck_assert(pack_rec.classification   == unpack_rec->classification);
	ck_assert(pack_rec.control_host     == unpack_rec->control_host);
	ck_assert(pack_rec.dimensions       == unpack_rec->dimensions);
	ck_assert(pack_rec.fed.name         == unpack_rec->fed.name);
	ck_assert(pack_rec.fed.id           == unpack_rec->fed.id);
	ck_assert(pack_rec.fed.state        == unpack_rec->fed.state);
	ck_assert(pack_rec.flags            == unpack_rec->flags);
	ck_assert(pack_rec.name             == unpack_rec->name);
	ck_assert(pack_rec.nodes            == unpack_rec->nodes);
	ck_assert(pack_rec.plugin_id_select == unpack_rec->plugin_id_select);

	/* root_assoc gets unpacked into a empty structure */
	ck_assert(unpack_rec->root_assoc    != NULL);
	ck_assert(pack_rec.rpc_version      == unpack_rec->rpc_version);
	ck_assert(pack_rec.fed.recv         == unpack_rec->fed.recv);
	ck_assert(pack_rec.fed.send         == unpack_rec->fed.send);
	ck_assert(pack_rec.fed.feature_list == unpack_rec->fed.feature_list);

	free_buf(buf);
	slurmdb_destroy_cluster_rec(unpack_rec);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* suite(void)
{
	Suite* s = suite_create("Pack slurmdb_cluster_rec_t");
	TCase* tc_core = tcase_create("Pack slurmdb_cluster_rec_t");
	tcase_add_test(tc_core, invalid_protocol);

	tcase_add_test(tc_core, pack_back1_null_rec);
	tcase_add_test(tc_core, pack_back1_rec);
	tcase_add_test(tc_core, pack_back1_rec_null_ptrs);

	tcase_add_test(tc_core, pack_back2_null_rec);
	tcase_add_test(tc_core, pack_back2_rec);
	tcase_add_test(tc_core, pack_back2_rec_null_ptrs);

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
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
