/*
 * update_node_topo-test.c - tests for the Topology= handling of
 * update_node(): one value per node when the value has ';' separators, the
 * same value for every node otherwise.
 *
 * node_mgr.c is linked in directly; the rest of slurmctld is not, so the few
 * slurmctld symbols update_node() reaches on this path are defined here.
 */
#include <check.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/node_features.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/slurmctld.h"

uint32_t max_powered_nodes = NO_VAL;
void *acct_db_conn = NULL;

extern hostlist_t *nodespec_to_hostlist(const char *nodes, bool uniq,
					char **nodesets)
{
	hostlist_t *hl = hostlist_create(nodes);

	if (uniq)
		hostlist_uniq(hl);
	return hl;
}

/* slurmctld builds these at startup; node_features_build_list_eq() needs them */
static int _build_config_bitmap(void *x, void *arg)
{
	config_record_t *config_ptr = x;

	return node_name2bitmap(config_ptr->nodes, false,
				&config_ptr->node_bitmap, NULL);
}

static int _update_with_features(char *nodes, char *topology,
				 char *features_act)
{
	update_node_msg_t msg;
	int rc;

	slurm_init_update_node_msg(&msg);
	msg.node_names = xstrdup(nodes);
	msg.topology_str = xstrdup(topology);
	msg.features_act = xstrdup(features_act);
	rc = update_node(&msg, 0);
	xfree(msg.node_names);
	xfree(msg.topology_str);
	xfree(msg.features_act);

	return rc;
}

static int _update(char *nodes, char *topology)
{
	return _update_with_features(nodes, topology, NULL);
}

static void _check_topo(char *node, char *expected)
{
	node_record_t *node_ptr = find_node_record(node);

	ck_assert_ptr_nonnull(node_ptr);
	if (expected)
		ck_assert_str_eq(node_ptr->topology_str, expected);
	else
		ck_assert_msg(!node_ptr->topology_str,
			      "%s: expected no topology, got %s", node,
			      node_ptr->topology_str);
}

START_TEST(test_one_topology_per_node)
{
	ck_assert_int_eq(_update("node01,node02", "topo-a:b1;topo-b:b1"),
			 SLURM_SUCCESS);
	_check_topo("node01", "topo-a:b1");
	_check_topo("node02", "topo-b:b1");
}

END_TEST

START_TEST(test_node_list_order_is_kept)
{
	ck_assert_int_eq(_update("node02,node01", "topo-a:b1;topo-b:b1"),
			 SLURM_SUCCESS);
	_check_topo("node02", "topo-a:b1");
	_check_topo("node01", "topo-b:b1");
}

END_TEST

START_TEST(test_several_topologies_on_one_node)
{
	ck_assert_int_eq(_update("node01,node02",
				 "topo-a:b1,topo-b:b1;topo-b:b1"),
			 SLURM_SUCCESS);
	_check_topo("node01", "topo-a:b1,topo-b:b1");
	_check_topo("node02", "topo-b:b1");
}

END_TEST

START_TEST(test_no_separator_applies_to_every_node)
{
	ck_assert_int_eq(_update("node[01-03]", "topo-b:b1"), SLURM_SUCCESS);
	_check_topo("node01", "topo-b:b1");
	_check_topo("node02", "topo-b:b1");
	_check_topo("node03", "topo-b:b1");
}

END_TEST

START_TEST(test_count_mismatch_changes_nothing)
{
	ck_assert_int_eq(_update("node01,node02", "topo-a:b1"), SLURM_SUCCESS);

	ck_assert_int_eq(_update("node01,node02", "topo-b:b1"
						  ";topo-b:b1;topo-b:b1"),
			 ESLURM_INVALID_NODE_NAME);
	ck_assert_int_eq(_update("node01,node02,node03", "topo-b:b1;topo-b:b1"),
			 ESLURM_INVALID_NODE_NAME);
	_check_topo("node01", "topo-a:b1");
	_check_topo("node02", "topo-a:b1");
}

END_TEST

START_TEST(test_empty_value_removes_that_node)
{
	ck_assert_int_eq(_update("node01,node02", "topo-a:b1"), SLURM_SUCCESS);

	ck_assert_int_eq(_update("node01,node02", ";topo-b:b1"), SLURM_SUCCESS);
	_check_topo("node01", NULL);
	_check_topo("node02", "topo-b:b1");
}

END_TEST

START_TEST(test_invalid_value_still_updates_other_nodes)
{
	ck_assert_int_eq(_update("node[01-03]", "topo-a:b1"), SLURM_SUCCESS);

	/* The request fails, but only the node with the invalid value is kept */
	ck_assert_int_eq(_update("node01,node02,node03",
				 "topo-b:b1;topo-b:nope;topo-b:b1"),
			 ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE);
	_check_topo("node01", "topo-b:b1");
	_check_topo("node02", "topo-a:b1");
	_check_topo("node03", "topo-b:b1");
}

END_TEST

START_TEST(test_invalid_value_fails_with_other_fields)
{
	ck_assert_int_eq(_update("node01,node02", "topo-a:b1"), SLURM_SUCCESS);

	/* node02's features update must not hide node01's rejected value */
	ck_assert_int_eq(_update_with_features("node01,node02",
					       "topo-b:nope;topo-b:b1",
					       "block"),
			 ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE);
	_check_topo("node01", "topo-a:b1");
	_check_topo("node02", "topo-b:b1");
}

END_TEST

int main(void)
{
	SRunner *sr;
	Suite *s;
	TCase *tc_core;
	int number_failed;
	char *src_dir;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_ERROR;
	log_init("update_node_topo-test", log_opts, 0, NULL);

	if ((src_dir = getenv("srcdir"))) {
		char *slurm_conf_path = NULL;

		xstrfmtcat(slurm_conf_path, "%s/%s", src_dir, "slurm.conf");
		setenv("SLURM_CONF", slurm_conf_path, 1);
		xfree(slurm_conf_path);
	}

	slurm_conf_init(NULL);
	init_node_conf();
	build_all_nodeline_info(false, 0);
	if (topology_g_init() || topology_g_build_config())
		fatal("failed to initialize topology");
	if (acct_storage_g_init())
		fatal("failed to initialize accounting storage");
	list_for_each(config_list, _build_config_bitmap, NULL);
	node_features_build_list_eq();
	asap_node_bitmap = bit_alloc(node_record_count);
	rs_node_bitmap = bit_alloc(node_record_count);

	tc_core = tcase_create("update_node_topo");
	tcase_add_test(tc_core, test_one_topology_per_node);
	tcase_add_test(tc_core, test_node_list_order_is_kept);
	tcase_add_test(tc_core, test_several_topologies_on_one_node);
	tcase_add_test(tc_core, test_no_separator_applies_to_every_node);
	tcase_add_test(tc_core, test_count_mismatch_changes_nothing);
	tcase_add_test(tc_core, test_empty_value_removes_that_node);
	tcase_add_test(tc_core, test_invalid_value_still_updates_other_nodes);
	tcase_add_test(tc_core, test_invalid_value_fails_with_other_fields);
	tcase_set_timeout(tc_core, 10);

	s = suite_create("update_node_topo");
	suite_add_tcase(s, tc_core);
	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
