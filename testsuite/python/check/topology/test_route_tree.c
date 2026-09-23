/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test topology/tree message fan-out (TopologyParam=RouteTree) through
 * topology_g_split_hostlist().
 *****************************************************************************/
#include <check.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/topology.h"

/*
 * Each test case has its own directory holding a slurm.conf, a topology.conf
 * and a testcases file. The testcases file is made of blank line separated
 * blocks: the first line of a block is the hostlist to split, the remaining
 * lines are the expected sublists, in order. Lines starting with '#' are
 * comments.
 */
#define TESTCASES_DIR "test_route_tree_testcases"

typedef struct {
	char *nodes; /* hostlist to split */
	char **expected; /* expected sublists */
	int expected_cnt;
} split_case_t;

static split_case_t *split_cases = NULL;
static int split_case_cnt = 0;

static char *_testcase_path(const char *testcase, const char *file)
{
	const char *src_dir = getenv("srcdir");

	return xstrdup_printf("%s/%s/%s/%s", src_dir ? src_dir : ".",
			      TESTCASES_DIR, testcase, file);
}

static void _init_topology(const char *testcase)
{
	char *conf_path = _testcase_path(testcase, "slurm.conf");

	setenv("SLURM_CONF", conf_path, 1);
	xfree(conf_path);

	slurm_init(NULL);
	init_node_conf();
	build_all_nodeline_info(true, 0);

	ck_assert_msg(topology_g_init() == SLURM_SUCCESS,
		      "Unable to initialize the topology plugin");
	ck_assert_msg(topology_g_build_config() == SLURM_SUCCESS,
		      "Unable to build the topology config");
}

static void _read_split_cases(const char *testcase)
{
	char *path = _testcase_path(testcase, "testcases");
	FILE *fp = fopen(path, "r");
	char *line = NULL;
	size_t len = 0;
	ssize_t n;
	int idx = -1; /* index of the case being parsed, -1 if none */

	ck_assert_msg(fp != NULL, "Unable to open %s: %s", path,
		      strerror(errno));

	while ((n = getline(&line, &len, fp)) != -1) {
		if (line[0] == '#')
			continue;

		while ((n > 0) &&
		       ((line[n - 1] == '\n') || (line[n - 1] == '\r')))
			line[--n] = '\0';

		if (!n) {
			/* A blank line ends the current case */
			idx = -1;
			continue;
		}

		if (idx < 0) {
			idx = split_case_cnt++;
			xrecalloc(split_cases, split_case_cnt,
				  sizeof(*split_cases));
			split_cases[idx].nodes = xstrdup(line);
			continue;
		}

		xrecalloc(split_cases[idx].expected,
			  split_cases[idx].expected_cnt + 1, sizeof(char *));
		split_cases[idx].expected[split_cases[idx].expected_cnt++] =
			xstrdup(line);
	}

	free(line);
	fclose(fp);

	ck_assert_msg(split_case_cnt > 0, "No test cases found in %s", path);
	xfree(path);
}

static void _free_split_cases(void)
{
	for (int i = 0; i < split_case_cnt; i++) {
		for (int j = 0; j < split_cases[i].expected_cnt; j++)
			xfree(split_cases[i].expected[j]);
		xfree(split_cases[i].expected);
		xfree(split_cases[i].nodes);
	}
	xfree(split_cases);
	split_case_cnt = 0;
}

static void _run_split_case(split_case_t *split_case)
{
	hostlist_t *hl = hostlist_create(split_case->nodes);
	hostlist_t **sp_hl = NULL;
	char **returned;
	int sp_cnt = 0;
	bool failed = false;

	ck_assert_msg(topology_g_split_hostlist(hl, &sp_hl, &sp_cnt, 0) !=
			      SLURM_ERROR,
		      "Unable to split hostlist %s", split_case->nodes);

	returned = xcalloc(sp_cnt, sizeof(*returned));
	for (int i = 0; i < sp_cnt; i++)
		returned[i] = hostlist_ranged_string_xmalloc(sp_hl[i]);

	if (sp_cnt != split_case->expected_cnt) {
		failed = true;
	} else {
		for (int i = 0; i < sp_cnt; i++) {
			if (xstrcmp(returned[i], split_case->expected[i])) {
				failed = true;
				break;
			}
		}
	}

	if (failed) {
		printf("Splitting %s returned %d sublists, expected %d:\n",
		       split_case->nodes, sp_cnt, split_case->expected_cnt);
		for (int i = 0; i < split_case->expected_cnt; i++)
			printf("  expected[%d] = %s\n", i,
			       split_case->expected[i]);
		for (int i = 0; i < sp_cnt; i++)
			printf("  returned[%d] = %s\n", i, returned[i]);
	}

	for (int i = 0; i < sp_cnt; i++) {
		xfree(returned[i]);
		hostlist_destroy(sp_hl[i]);
	}
	xfree(returned);
	xfree(sp_hl);
	hostlist_destroy(hl);

	ck_assert_msg(!failed, "Unexpected split of %s, see stdout for details",
		      split_case->nodes);
}

static void _test_testcase(const char *testcase)
{
	_init_topology(testcase);
	_read_split_cases(testcase);

	for (int i = 0; i < split_case_cnt; i++)
		_run_split_case(&split_cases[i]);

	_free_split_cases();
}

START_TEST(test_symmetric_tree)
{
	/* 10000 nodes, 3 levels of switches, all of them balanced */
	_test_testcase("symmetric_tree");
}

END_TEST

START_TEST(test_unbalanced_tree)
{
	/* Same 10000 nodes, but the two halves have a different depth */
	_test_testcase("unbalanced_tree");
}

END_TEST

START_TEST(test_default_treewidth)
{
	/* Without TopologyParam=RouteTree the split only honors TreeWidth */
	_test_testcase("default_treewidth");
}

END_TEST

START_TEST(test_no_root_switch)
{
	/* Top-level switches with no common root are split independently */
	_test_testcase("no_root_switch");
}

END_TEST

START_TEST(test_node_not_in_topology)
{
	/* Nodes missing from topology.conf are relayed on their own */
	_test_testcase("node_not_in_topology");
}

END_TEST

START_TEST(test_disconnected_trunks)
{
	/*
	 * Ticket 25473: a leaf that is the only spanned child of a top-level
	 * switch must relay its own nodes instead of falling back to one relay
	 * per node.
	 */
	_test_testcase("disconnected_trunks");
}

END_TEST

int main(void)
{
	SRunner *sr;
	Suite *s;
	TCase *tc_core;
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG;
	log_init("route-tree-test", log_opts, 0, NULL);

	tc_core = tcase_create("route_tree");
	tcase_add_test(tc_core, test_symmetric_tree);
	tcase_add_test(tc_core, test_unbalanced_tree);
	tcase_add_test(tc_core, test_default_treewidth);
	tcase_add_test(tc_core, test_no_root_switch);
	tcase_add_test(tc_core, test_node_not_in_topology);
	tcase_add_test(tc_core, test_disconnected_trunks);

	tcase_set_timeout(tc_core, 60);
	s = suite_create("route_tree");
	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
