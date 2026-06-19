#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
	topology_g_init();
	topology_g_build_config();

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

START_TEST(test_yaml_topo_config)
{
	char *slurm_unit_conf_dir;
	char *slurm_unit_topo_conf = NULL;
	int fd, rc;
	const char slurm_unit_topo_content[] = "- topology: topo1\n"
					       "  cluster_default: False\n"
					       "  tree:\n"
					       "    switches:\n"
					       "    - switch: switch_name\n"
					       "      nodes: node[01-04]\n"
					       "- topology: topo2\n"
					       "  cluster_default: False\n"
					       "  block:\n"
					       "    block_sizes:\n"
					       "      - 4\n"
					       "      - 16\n"
					       "    blocks:\n"
					       "    - block: b1\n"
					       "      nodes: node[01-04]\n"
					       "    - block: b2\n"
					       "      nodes: node[05-08]\n"
					       "    - block: b3\n"
					       "      nodes: node[09-12]\n"
					       "    - block: b4\n"
					       "      nodes: node[13-16]\n"
					       "- topology: topo3\n"
					       "  cluster_default: True\n"
					       "  flat: True\n";
	const size_t csize = sizeof(slurm_unit_topo_content);
	int idx = -1;

	slurm_unit_conf_dir = xstrdup("/tmp/slurm_unit-XXXXXX");
	if (!mkdtemp(slurm_unit_conf_dir))
		ck_abort();

	xstrfmtcat(slurm_unit_topo_conf, "/%s/topology.yaml",
		   slurm_unit_conf_dir);

	fd = open(slurm_unit_topo_conf, O_RDWR | O_CLOEXEC | O_CREAT, 0644);
	ck_assert_msg(fd > 0, "Open topology.yaml");

	rc = write(fd, slurm_unit_topo_content, csize);
	ck_assert_msg(csize <= rc, "Write to topology.yaml");

	setenv("SLURM_CONF", slurm_unit_topo_conf, 1);

	topology_g_init();
	topology_g_build_config();

	topology_g_get(TOPO_DATA_TCTX_IDX, "topo3", &idx);
	ck_assert_msg(idx == 0, "topo3 is default");

	close(fd);
	unlink(slurm_unit_topo_conf);
	rmdir(slurm_unit_conf_dir);
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

	tc_core = tcase_create("topo");
	tcase_add_test(tc_core, test_fragmentation);
	tcase_add_test(tc_core, test_yaml_topo_config);

	tcase_set_timeout(tc_core, 10);
	s = suite_create("topo");
	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
