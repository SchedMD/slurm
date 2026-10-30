/*****************************************************************************\
 *  Copyright (C) 2026 Google LLC.
\*****************************************************************************/

/*
 * Unit test of the gres/tpu sysfs discovery through gres_g_node_config_load():
 * how the PCI functions of a mock /sys/bus/pci/devices tree are grouped into
 * one GRES record per TPU chip, which ones are ignored, and the order of the
 * chips in the final list of gres_slurmd_conf_t records.
 *
 * Every case builds its own mock sysfs tree (pointed to by
 * SLURM_TPU_SYSFS_PATH), slurm.conf, tpu.yaml and gres.conf. Unless a case
 * gives the node GRES of slurm.conf, the node has none, so every TPU record
 * comes from the detection. Note that without node GRES the gres.conf records
 * are dropped before the plugins run, as slurm.conf sets the count of each
 * GRES of the node.
 *
 * The mock tree has 4 chips, in PCI slots 08 to 05, each with 1 or 2 PCI
 * functions (cores). IOMMU groups start at 8, so they span single and double
 * digits (a lexicographic sort differs from a numeric one), and they are given
 * in reverse slot order (sorting by address cannot pass for sorting by group).
 * It also has 3 functions that must be skipped:
 *   - 0000:00:09.0: matching vendor/device with no IOMMU group
 *   - 0000:00:0a.0: vendor 0x1ae0 with an unconfigured PCI device ID (0x00ff)
 *   - 0000:00:0b.0: matching PCI device ID with a non-matching vendor (0x9999)
 *
 * The final list also keeps the record of the other GRES (fake) of GresTypes.
 *
 * Originally test_144_40 (python).
 */

#include <check.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"

#include "scheck.h"

#define NODE_NAME "testnode"
#define CPU_CNT 96
#define CPULIST "0-95"
#define LOAD_LOG "load.log"
#define TPU_VENDOR "0x1ae0"
#define V6E_ID "0x006f"
#define TPU7X_ID "0x0076"
#define MAX_CHIPS 4
#define MAX_EXTRA 4

#define SLURM_CONF_CONTENT \
	"ControlMachine=test_machine\n" \
	"ClusterName=test_cluster\n" \
	"SlurmUser=root\n" \
	"GresTypes=tpu,fake\n"

#define TPU_YAML_CONTENT \
	"tpu_types:\n" \
	"  - type: tpu7x\n" \
	"    pci_device_id: \"" TPU7X_ID "\"\n" \
	"    shape_per_node: 2x2x1\n" \
	"    shape_per_segment: 4x4x4\n" \
	"    nodes_per_pod: 2304\n" \
	"  - type: v6e\n" \
	"    pci_device_id: \"" V6E_ID "\"\n" \
	"    shape_per_node: 2x2\n" \
	"    shape_per_segment: 16x16\n" \
	"    nodes_per_pod: 64\n"

#define NO_IOMMU_REGEX "has no IOMMU group"
#define SHARED_REGEX "shares an IOMMU group"

#define V6E_FILES \
	{ "/dev/vfio/8", "/dev/vfio/9", "/dev/vfio/10", "/dev/vfio/11" }
#define TPU7X_FILES \
	{ "/dev/vfio/8,/dev/vfio/9", "/dev/vfio/10,/dev/vfio/11", \
	  "/dev/vfio/12,/dev/vfio/13", "/dev/vfio/14,/dev/vfio/15" }

/* One more PCI function of the device_id of the case */
typedef struct {
	char *addr;
	int group;
} extra_func_t;

typedef struct {
	char *id;
	char *device_id; /* PCI device ID of the chips, NULL for no devices */
	int cores; /* PCI functions per chip */
	bool long_cpulist; /* local_cpulist longer than 255 bytes */
	extra_func_t extra[MAX_EXTRA];
	char *node_gres; /* GRES of the node in slurm.conf */
	char *gres_conf;
	/* Expected TPU records */
	char *type;
	char *files[MAX_CHIPS + 1]; /* NULL terminated, in order */
	bool mult; /* chips with more than one core */
	uint64_t fake_cnt; /* Count of the fake GRES record */
	/* If set, log_cnt log lines must match this regex */
	char *log_regex;
	int log_cnt;
} tpu_case_t;

// clang-format off
static const tpu_case_t cases[] = {
	/* Single-core chips (v6e) have 1 VFIO file per record */
	{
		.id = "v6e_single_core_discovery",
		.device_id = V6E_ID,
		.cores = 1,
		.gres_conf = "AutoDetect=tpu\n",
		.type = "v6e",
		.files = V6E_FILES,
		.log_regex = NO_IOMMU_REGEX,
		.log_cnt = 1,
	},
	/*
	 * Dual-core chips (tpu7x) group their 2 PCI functions in one record,
	 * sorted numerically by their lowest IOMMU group
	 */
	{
		.id = "tpu7x_dual_core_grouping_and_numeric_iommu_order",
		.device_id = TPU7X_ID,
		.cores = 2,
		.gres_conf = "AutoDetect=tpu\n",
		.type = "tpu7x",
		.files = TPU7X_FILES,
		.mult = true,
	},
	/* A local_cpulist longer than 255 bytes is read in full */
	{
		.id = "long_local_cpulist",
		.device_id = V6E_ID,
		.cores = 1,
		.long_cpulist = true,
		.gres_conf = "AutoDetect=tpu\n",
		.type = "v6e",
		.files = V6E_FILES,
	},
	/* A node with no TPU PCI devices has no TPU records */
	{
		.id = "empty_sysfs_discovers_nothing",
		.gres_conf = "AutoDetect=tpu\n",
	},
	/* AutoDetect=off in gres.conf suppresses the sysfs scan */
	{
		.id = "autodetect_off_suppresses_sysfs_scan",
		.device_id = TPU7X_ID,
		.cores = 2,
		.gres_conf = "AutoDetect=off\n",
		.log_regex = "AutoDetect=off, not scanning for TPUs",
		.log_cnt = 1,
	},
	/* Detection runs with AutoDetect unset */
	{
		.id = "autodetect_runs_unless_off_unset",
		.device_id = V6E_ID,
		.cores = 1,
		.gres_conf = "",
		.type = "v6e",
		.files = V6E_FILES,
	},
	/* Detection runs with a node's AutoDetect=tpu over a global off */
	{
		.id = "autodetect_runs_unless_off_node_overrides_global_off",
		.device_id = V6E_ID,
		.cores = 1,
		.gres_conf = "AutoDetect=off\n"
			     "NodeName=" NODE_NAME " AutoDetect=tpu\n",
		.type = "v6e",
		.files = V6E_FILES,
	},
	/*
	 * Detected chips replace the Name=tpu lines of gres.conf, and the
	 * lines of other GRES are kept. The node GRES keep the gres.conf lines
	 * until the plugin runs.
	 */
	{
		.id = "detected_chips_replace_configured_tpu_lines",
		.device_id = TPU7X_ID,
		.cores = 2,
		.node_gres = "tpu:tpu7x:2,fake:1",
		.gres_conf = "Name=tpu Type=tpu7x Count=2\n"
			     "Name=fake Count=1\n",
		.type = "tpu7x",
		.files = TPU7X_FILES,
		.mult = true,
		.fake_cnt = 1,
	},
	/* Single-core chips that share an IOMMU group are ignored */
	{
		.id = "chips_sharing_an_iommu_group_ignored_single_core",
		.device_id = V6E_ID,
		.cores = 1,
		.extra = {
			{ "0000:00:0c.0", 20 },
			{ "0000:00:0d.0", 20 },
		},
		.gres_conf = "AutoDetect=tpu\n",
		.type = "v6e",
		.files = V6E_FILES,
		.log_regex = SHARED_REGEX,
		.log_cnt = 2,
	},
	/* Dual-core chips that share an IOMMU group are ignored */
	{
		.id = "chips_sharing_an_iommu_group_ignored_dual_core",
		.device_id = TPU7X_ID,
		.cores = 2,
		.extra = {
			{ "0000:00:0c.0", 20 },
			{ "0000:00:0c.1", 21 },
			{ "0000:00:0d.0", 21 },
			{ "0000:00:0d.1", 22 },
		},
		.gres_conf = "AutoDetect=tpu\n",
		.type = "tpu7x",
		.files = TPU7X_FILES,
		.mult = true,
		.log_regex = SHARED_REGEX,
		.log_cnt = 2,
	},
};
// clang-format on

static char *base_dir = NULL;

/* RET a local_cpulist of all the CPUs that is longer than 255 bytes */
static char *_long_cpulist(void)
{
	char *cpulist = NULL;

	for (int i = 0; i < CPU_CNT; i++)
		xstrfmtcat(cpulist, "%s%d", (i ? "," : ""), i);
	ck_assert_msg(strlen(cpulist) > 255,
		      "The cpulist should be long enough to truncate");

	return cpulist;
}

static void _mkdir(const char *path)
{
	ck_assert_msg(!mkdir(path, 0700) || (errno == EEXIST),
		      "mkdir(%s) failed: %s", path, strerror(errno));
}

/*
 * Add the PCI function addr to the mock sysfs tree, in the IOMMU group group
 * (none if negative).
 */
static void _make_func(const char *sysfs_dir, const char *addr,
		       const char *vendor, const char *device_id, int group,
		       const char *cpulist)
{
	char *dev_dir = NULL, *content = NULL;

	xstrfmtcat(dev_dir, "%s/%s", sysfs_dir, addr);
	_mkdir(dev_dir);

	xstrfmtcat(content, "%s\n", vendor);
	scheck_write_file(dev_dir, "vendor", content);
	xfree(content);
	xstrfmtcat(content, "%s\n", device_id);
	scheck_write_file(dev_dir, "device", content);
	xfree(content);
	xstrfmtcat(content, "%s\n", cpulist);
	scheck_write_file(dev_dir, "local_cpulist", content);
	xfree(content);

	if (group >= 0) {
		char *group_dir = NULL, *link = NULL, *target = NULL;

		xstrfmtcat(group_dir, "%s/.groups/%d", sysfs_dir, group);
		_mkdir(group_dir);
		xstrfmtcat(link, "%s/iommu_group", dev_dir);
		xstrfmtcat(target, "../.groups/%d", group);
		ck_assert_msg(!symlink(target, link), "symlink(%s) failed: %s",
			      link, strerror(errno));
		xfree(group_dir);
		xfree(link);
		xfree(target);
	}

	xfree(dev_dir);
}

/*
 * Build the mock sysfs tree of a case in sysfs_dir. See the description of the
 * tree at the top.
 */
static void _build_sysfs(const tpu_case_t *tc, const char *sysfs_dir,
			 const char *cpulist)
{
	const char *slots[MAX_CHIPS] = { "08", "07", "06", "05" };
	char *groups_dir = NULL;
	int group = 8;

	_mkdir(sysfs_dir);
	xstrfmtcat(groups_dir, "%s/.groups", sysfs_dir);
	_mkdir(groups_dir);
	xfree(groups_dir);

	if (!tc->device_id)
		return;

	for (int i = 0; i < MAX_CHIPS; i++) {
		for (int core = 0; core < tc->cores; core++) {
			char *addr =
				xstrdup_printf("0000:00:%s.%d", slots[i], core);

			_make_func(sysfs_dir, addr, TPU_VENDOR, tc->device_id,
				   group++, cpulist);
			xfree(addr);
		}
	}

	_make_func(sysfs_dir, "0000:00:09.0", TPU_VENDOR, tc->device_id, -1,
		   cpulist);
	_make_func(sysfs_dir, "0000:00:0a.0", TPU_VENDOR, "0x00ff", 99,
		   cpulist);
	_make_func(sysfs_dir, "0000:00:0b.0", "0x9999", tc->device_id, 98,
		   cpulist);

	for (int i = 0; (i < MAX_EXTRA) && tc->extra[i].addr; i++)
		_make_func(sysfs_dir, tc->extra[i].addr, TPU_VENDOR,
			   tc->device_id, tc->extra[i].group, cpulist);
}

/*
 * Create the configuration and the mock sysfs tree of a case in its own
 * directory, and point SLURM_CONF and SLURM_TPU_SYSFS_PATH to them.
 * RET the case directory
 */
static char *_setup_case(const tpu_case_t *tc, const char *cpulist)
{
	char *case_dir = NULL, *path = NULL;

	xstrfmtcat(case_dir, "%s/%s", base_dir, tc->id);
	_mkdir(case_dir);

	scheck_write_file(case_dir, "slurm.conf", SLURM_CONF_CONTENT);
	scheck_write_file(case_dir, "tpu.yaml", TPU_YAML_CONTENT);
	scheck_write_file(case_dir, "gres.conf", tc->gres_conf);

	xstrfmtcat(path, "%s/sysfs", case_dir);
	_build_sysfs(tc, path, cpulist);
	setenv("SLURM_TPU_SYSFS_PATH", path, 1);
	xfree(path);

	xstrfmtcat(path, "%s/slurm.conf", case_dir);
	setenv("SLURM_CONF", path, 1);
	xfree(path);

	return case_dir;
}

/* sysfs reports machine CPU numbering, keep it as the abstract one */
static int _mac_to_abs(char *mac, char **abs)
{
	*abs = xstrdup(mac);
	return SLURM_SUCCESS;
}

static char *_recs2str(list_t *conf_list)
{
	list_itr_t *itr = NULL;
	gres_slurmd_conf_t *rec = NULL;
	char *str = NULL;

	if (!conf_list)
		return xstrdup("(none)");

	itr = list_iterator_create(conf_list);
	while ((rec = list_next(itr)))
		xstrfmtcat(str,
			   "\n  Name=%s Type=%s Count=%" PRIu64
			   " Cores=%s File=%s Flags=%s",
			   rec->name, rec->type_name, rec->count, rec->cpus,
			   rec->file, gres_flags2str(rec->config_flags));
	list_iterator_destroy(itr);

	return str;
}

static void _check_str(const char *id, int idx, const char *field,
		       const char *exp, const char *act, const char *recs)
{
	ck_assert_msg(
		!xstrcmp(exp, act),
		"%s: TPU record %d %s should be %s, but got %s. Records:%s", id,
		idx, field, exp ? exp : "(null)", act ? act : "(null)", recs);
}

/*
 * Check the TPU records (name tpu with a non-zero count, as a GRES of
 * GresTypes without any record gets one with a zero count) and the record of
 * the fake GRES.
 */
static void _check_records(const tpu_case_t *tc, const char *cpulist)
{
	list_t *conf_list = gres_get_node_conf_list();
	list_itr_t *itr = NULL;
	gres_slurmd_conf_t *rec = NULL;
	char *recs = _recs2str(conf_list);
	int exp_cnt = 0, tpu_cnt = 0, fake_cnt = 0;

	while ((exp_cnt < MAX_CHIPS) && tc->files[exp_cnt])
		exp_cnt++;

	if (conf_list)
		itr = list_iterator_create(conf_list);
	while (itr && (rec = list_next(itr))) {
		if (!xstrcmp(rec->name, "fake")) {
			ck_assert_msg(
				rec->count == tc->fake_cnt,
				"%s: the fake GRES Count should be %" PRIu64
				", but got %" PRIu64 ". Records:%s",
				tc->id, tc->fake_cnt, rec->count, recs);
			fake_cnt++;
			continue;
		}
		if (xstrcmp(rec->name, "tpu") || !rec->count)
			continue;

		ck_assert_msg(
			tpu_cnt < exp_cnt,
			"%s: there should be %d TPU records, but got more. Records:%s",
			tc->id, exp_cnt, recs);
		_check_str(tc->id, tpu_cnt, "Type", tc->type, rec->type_name,
			   recs);
		_check_str(tc->id, tpu_cnt, "File", tc->files[tpu_cnt],
			   rec->file, recs);
		_check_str(tc->id, tpu_cnt, "Cores", cpulist, rec->cpus, recs);
		ck_assert_msg(
			rec->count == 1,
			"%s: TPU record %d Count should be 1, but got %" PRIu64
			". Records:%s",
			tc->id, tpu_cnt, rec->count, recs);
		ck_assert_msg(
			rec->config_flags & GRES_CONF_AUTODETECT,
			"%s: TPU record %d should be autodetected. Records:%s",
			tc->id, tpu_cnt, recs);
		ck_assert_msg(
			!(rec->config_flags & GRES_CONF_HAS_MULT) == !tc->mult,
			"%s: TPU record %d should %sbe a multi-core chip. Records:%s",
			tc->id, tpu_cnt, (tc->mult ? "" : "not "), recs);
		tpu_cnt++;
	}
	if (itr)
		list_iterator_destroy(itr);

	ck_assert_msg(
		tpu_cnt == exp_cnt,
		"%s: there should be %d TPU records, but got %d. Records:%s",
		tc->id, exp_cnt, tpu_cnt, recs);
	ck_assert_msg(
		fake_cnt == 1,
		"%s: there should be 1 fake GRES record, but got %d. Records:%s",
		tc->id, fake_cnt, recs);

	FREE_NULL_LIST(conf_list);
	xfree(recs);
}

START_TEST(test_tpu_discovery)
{
	const tpu_case_t *tc = &cases[_i];
	char *case_dir = NULL, *log_path = NULL, *log_txt = NULL;
	char *cpulist = tc->long_cpulist ? _long_cpulist() : xstrdup(CPULIST);
	list_t *node_gres = NULL;
	int rc = SLURM_ERROR;

	case_dir = _setup_case(tc, cpulist);
	xstrfmtcat(log_path, "%s/%s", case_dir, LOAD_LOG);

	/*
	 * Log the debug messages of the plugin too, and keep logging into
	 * stderr to show any unexpected fatal()
	 */
	ck_assert_msg(!scheck_log_to_file(log_path, LOG_LEVEL_DEBUG,
					  LOG_LEVEL_INFO),
		      "%s: unable to log into %s", tc->id, log_path);

	slurm_init(NULL);
	if (tc->node_gres)
		gres_init_node_config(tc->node_gres, &node_gres);
	rc = gres_g_node_config_load(CPU_CNT, NODE_NAME, node_gres, NULL,
				     _mac_to_abs);
	FREE_NULL_LIST(node_gres);
	log_txt = scheck_read_file(log_path);

	ck_assert_msg(
		rc == SLURM_SUCCESS,
		"%s: gres_g_node_config_load() should succeed, but got: %s. Log:\n%s",
		tc->id, slurm_strerror(rc), log_txt);
	_check_records(tc, cpulist);
	if (tc->log_regex)
		scheck_assert_log_count(tc->id, log_txt, tc->log_regex,
					tc->log_cnt);

	xfree(log_txt);
	xfree(log_path);
	xfree(case_dir);
	xfree(cpulist);
}

END_TEST

int main(void)
{
	SRunner *sr = NULL;
	Suite *s = NULL;
	TCase *tc_core = NULL;
	int number_failed = 0;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	log_opts.stderr_level = LOG_LEVEL_INFO;
	log_init("gres-tpu-test", log_opts, 0, NULL);

	/* Create the case directories in the working directory */
	if (!(base_dir = scheck_mkdtemp("gres_tpu")))
		return EXIT_FAILURE;

	tc_core = tcase_create("tpu_discovery");
	tcase_add_loop_test(tc_core, test_tpu_discovery, 0, ARRAY_SIZE(cases));
	s = suite_create("gres_tpu");
	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	xfree(base_dir);
	log_fini();

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
