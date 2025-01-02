#include <check.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/topology.h"

START_TEST(test_fragmentation)
{
	bitstr_t *my_bitmap = bit_alloc(node_record_count);
	ck_assert_msg(topology_g_get_fragmentation(my_bitmap) == 160, "MAX");
	bit_not(my_bitmap);
	ck_assert_msg(topology_g_get_fragmentation(my_bitmap) == 0, "MIN");
	bit_clear(my_bitmap, 0);
	ck_assert_msg(topology_g_get_fragmentation(my_bitmap) == 61, "0");
	bit_clear(my_bitmap, 1);
	bit_clear(my_bitmap, 2);
	ck_assert_msg(topology_g_get_fragmentation(my_bitmap) == 63, "0-2");
	bit_clear(my_bitmap, 31);
	ck_assert_msg(topology_g_get_fragmentation(my_bitmap) == 92, "0-2,31");

	FREE_NULL_BITMAP(my_bitmap);
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
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("topo-test", log_opts, 0, NULL);

	if ((src_dir = getenv("srcdir"))) {
		char *slurm_conf_path = NULL;
		xstrfmtcat(slurm_conf_path, "%s/%s", src_dir, "slurm.conf");
		setenv("SLURM_CONF", slurm_conf_path, 1);
		xfree(slurm_conf_path);
	}

	slurm_init(NULL);
	init_node_conf();
	build_all_nodeline_info(false, 0);
	topology_g_init();
	topology_g_build_config();

	tc_core = tcase_create("topo");
	tcase_add_test(tc_core, test_fragmentation);

	s = suite_create("topo");
	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
