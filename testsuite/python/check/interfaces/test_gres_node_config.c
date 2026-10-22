/*****************************************************************************\
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

/*
 * Unit test of gres_g_node_config_load(): how a node's GRES from slurm.conf,
 * its gres.conf records and the GPUs detected on the system (faked through
 * fake_gpus.conf) are merged, normalized and sorted into the final list of
 * gres_slurmd_conf_t records.
 *
 * Every case runs with a fixed cpu_cnt and without the xcpuinfo abstract to
 * machine CPU conversion, so the results don't depend on the hardware of the
 * host running the test.
 *
 * In the configuration strings of the cases, DEV_PREFIX is replaced by the
 * path prefix of the fake device files (e.g. DEV_PREFIX1 -> <dir>/nvidia1).
 * The fake_gpus.conf format is:
 *	type|cpu_count|cores|links|file[|unique_id[|flags]]
 *
 * Besides the final list, cases can check how many log lines match a regex,
 * e.g. for the errors and warnings telling the admin that a record is ignored.
 * Cases using AutoDetect=nvml are only built with NVML support.
 *
 * Originally test39.18 (expect) and then test_144_17 (python).
 */

#include <check.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"

#define NODE_NAME "tux0"
#define CPU_CNT 4
#define DEV_PREFIX "DEV_PREFIX"
#define DEV_CNT 9
#define LOAD_LOG "load.log"

/* Exit codes of the child process of _check_fail() */
#define FATAL_EXIT_CODE 1 /* The one used by fatal() */
#define LOG_EXIT_CODE 2
#define ERROR_EXIT_CODE 3

#define SLURM_CONF_CONTENT \
	"SlurmctldHost=test_machine\n" \
	"ClusterName=test_cluster\n" \
	"GresTypes=gpu,mps,nic,mic,tmpdisk\n"

/* Env flags of a GPU without an explicit vendor env flag */
#define ENV_ALL "ENV_NVML,ENV_RSMI,ENV_ONEAPI,ENV_OPENCL,ENV_DEFAULT"

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
#define A58_FLAGS "CountOnly,GENERATED"
#define B4_MISMATCH "Cores, Links or UUID"
#else
#define A58_FLAGS "CountOnly"
#define B4_MISMATCH "Cores or Links"
#endif

/* ChunkSizes and ChunkOrder are added in 26.11 */
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
#define HAVE_GRES_CHUNKS
#endif

/*
 * The cpu_count of the NVML affinity cases (a6, a10, a12) is the size in bits
 * of the unsigned long arrays that NVML fills.
 */
_Static_assert(sizeof(unsigned long) == 8, "cases assume 64-bit longs");

/* An expected record of the final gres.conf list. NULL means unset. */
typedef struct {
	char *name;
	char *type;
	uint64_t count;
	uint32_t cpu_cnt;
	char *cpus;
	char *links;
	char *file;
	char *unique_id;
	char *flags;
	char *chunk_sizes;
	char *chunk_order;
} exp_rec_t;

typedef struct {
	char *id;
	char *node_gres; /* The node's Gres= from slurm.conf */
	char *gres_conf;
	char *fake_gpus_conf;
	exp_rec_t *expected; /* Terminated by a record with no name */
	bool fail; /* Loading must fail, either with fatal() or an error */
	char *log_regex; /* If set, log_cnt log lines must match this regex */
	int log_cnt;
} gres_case_t;

// clang-format off
static const gres_case_t cases[] = {
	{
		/* Detected GPUs are sorted by File and inherit their Cores */
		.id = "a2",
		.node_gres = "gpu:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "2-3", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "2-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
	{
		/* Detected GPUs without File are dropped from the final list */
		.id = "a4",
		.node_gres = "gpu:8",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla|4|(null)|(null)|(null)\n"
			"tesla|4|(null)|(null)|DEV_PREFIX1\n"
			"tesla|4|(null)|-1,0|(null)\n"
			"tesla|4|(null)|-1,0|DEV_PREFIX3\n"
			"tesla|4|0-1|(null)|(null)\n"
			"tesla|4|0-1|(null)|DEV_PREFIX2\n"
			"tesla|4|0-1|-1,0|(null)\n"
			"tesla|4|0-1|-1,0|DEV_PREFIX4\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, NULL, "-1,0", "DEV_PREFIX3", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "0-1", "-1,0", "DEV_PREFIX4", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
#ifdef HAVE_NVML
	{
		.id = "a6",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf =
			"tesla|1280|~XXXXXXXXXXXXXXXXXXXX|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 1280, "0-1279", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a8",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|64|~X|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 64, "0-63", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a10",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|192|~XXX|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 192, "0-191", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a12",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|32768|~max|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 32768, "0-32767", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a14",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|0|~zero|(null)|DEV_PREFIX1\n",
		.fail = true,
		.log_regex =
			"fatal: _set_cpu_set_bitstr: cpu_set_bitstr is empty",
		.log_cnt = 1,
	},
	{
		.id = "a16",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|16|~one|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 16, "0", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a18",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|4|~three|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a20",
		.node_gres = "gpu:1",
		.gres_conf = "AutoDetect=nvml\n",
		.fake_gpus_conf = "tesla|16|~half|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 16, "8-15", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
#endif
	{
		.id = "a22",
		.node_gres = "gpu:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "2-3", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "2-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE" },
			{ 0 },
		},
	},
	{
		.id = "a24",
		.node_gres = "gpu:v100:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "v100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "v100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "v100", 1, 4, "2-3", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "v100", 1, 4, "2-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a26",
		.node_gres = "gpu:TESLA_V100:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "TESLA_V100", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "TESLA_V100", 1, 4, "0-1", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "TESLA_V100", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "TESLA_V100", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a28",
		.node_gres = "gpu:pcie-16gb:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "pcie-16gb", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "pcie-16gb", 1, 4, "0-1", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "pcie-16gb", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "pcie-16gb", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a30",
		.node_gres = "gpu:tesla_v100-pcie-16gb:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla_v100-pcie-16gb|4|2-3|(null)|DEV_PREFIX4\n"
			"tesla_v100-pcie-16gb|4|0-1|(null)|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla_v100-pcie-16gb", 1, 4, "0-1", NULL,
			  "DEV_PREFIX1", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla_v100-pcie-16gb", 1, 4, "0-1", NULL,
			  "DEV_PREFIX2", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla_v100-pcie-16gb", 1, 4, "2-3", NULL,
			  "DEV_PREFIX3", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla_v100-pcie-16gb", 1, 4, "2-3", NULL,
			  "DEV_PREFIX4", NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a32",
		.node_gres = "gpu:k20m:1,gpu:k20m1:1,gpu:v100:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla_k20m1|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla_k20m|4|0-1|(null)|DEV_PREFIX2\n"
			"tesla_v100-sxm2-32gb|4|0-1|(null)|DEV_PREFIX3\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "k20m1", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "v100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a34",
		.node_gres = "gpu:p100:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX2\n"
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX6\n"
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX3\n"
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX4\n"
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX1\n"
			"nvidia-p100|4|0-1|(null)|DEV_PREFIX5\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "p100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a36",
		.node_gres = "gpu:aaaa:1,gpu:a:1,gpu:aa:1,gpu:aaa:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"aa|4|0-1|(null)|DEV_PREFIX2\n"
			"aaa|4|0-1|(null)|DEV_PREFIX1\n"
			"a|4|0-1|(null)|DEV_PREFIX3\n"
			"aaaa|4|0-1|(null)|DEV_PREFIX4\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "aa", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "aaaa", 1, 4, "0-1", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a38",
		.node_gres = "gpu:k20:2,gpu:k20m:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"k20|4|0-1|(null)|DEV_PREFIX2\n"
			"k20m|4|0-1|(null)|DEV_PREFIX1\n"
			"k20|4|0-1|(null)|DEV_PREFIX3\n"
			"k20m|4|0-1|(null)|DEV_PREFIX4\n"
			"k20|4|0-1|(null)|DEV_PREFIX5\n"
			"k20m|4|0-1|(null)|DEV_PREFIX7\n"
			"k20|4|0-1|(null)|DEV_PREFIX6\n"
			"k20m|4|0-1|(null)|DEV_PREFIX8\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX7", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX8", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a39",
		.node_gres = "gpu:k20:2,gpu:k20m:2",
		.gres_conf = "",
		.fake_gpus_conf =
			"k20m|4|0-1|(null)|DEV_PREFIX1\n"
			"k20m|4|0-1|(null)|DEV_PREFIX2\n"
			"k20m|4|0-1|(null)|DEV_PREFIX3\n"
			"k20|4|0-1|(null)|DEV_PREFIX4\n"
			"k20|4|0-1|(null)|DEV_PREFIX5\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20m", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "k20", 1, 4, "0-1", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a40",
		.node_gres = "gpu:aaa:3,gpu:bbb:2,gpu:ccc:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"test-bbb-test|4|0-1|(null)|DEV_PREFIX3\n"
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX2\n"
			"test-ccc-test|4|0-1|(null)|DEV_PREFIX5\n"
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX1\n"
			"test-bbb-test|4|0-1|(null)|DEV_PREFIX4\n"
			"test-ccc-test|4|0-1|(null)|DEV_PREFIX6\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "bbb", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "bbb", 1, 4, "0-1", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "ccc", 1, 4, "0-1", NULL, "DEV_PREFIX5", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a42",
		.node_gres = "gpu:aaa:1,gpu:aaa:1,gpu:aaa:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX4\n"
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX3\n"
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX2\n"
			"test-aaa-test|4|0-1|(null)|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "aaa", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a52",
		.node_gres = "gpu:tesla:3,gpu:special:1",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "a53",
		.node_gres = "gpu:tesla:3,gpu:special:1",
		.gres_conf = "Name=gpu Type=special File=DEV_PREFIX5\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "special", 1, 4, NULL, NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "a54",
		.node_gres = "gpu:tesla:3,gpu:special:1",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0\n"
			"Name=gpu Type=special File=DEV_PREFIX5\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "special", 1, 4, NULL, NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "a55",
		.node_gres = "gpu:tesla:3,gpu:special:1",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[0-2] Cores=0-1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=0\n"
			"Name=gpu Type=special File=DEV_PREFIX5\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "special", 1, 4, NULL, NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
		.log_regex =
			"warning: A line in gres.conf for GRES [^[:space:]]+ has [0-9]+ more configured than expected in slurm.conf\\. Ignoring extra GRES\\.",
		.log_cnt = 1,
	},
	{
		.id = "a56",
		.node_gres = "gpu:tesla:4,gpu:special:1",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[0-3] Cores=0-1\n"
			"Name=gpu Type=special File=DEV_PREFIX5\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "special", 1, 4, NULL, NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "a57",
		.node_gres = "",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[0-3] Cores=0-1\n"
			"Name=gpu Type=special File=DEV_PREFIX5\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.log_regex =
			"warning: A line in gres.conf for GRES [^[:space:]]+ has [0-9]+ more configured than expected in slurm.conf\\. Ignoring extra GRES\\.",
		.log_cnt = 2,
	},
	{
		.id = "a58",
		.node_gres = "gpu:tesla:3,gpu:special:1,tmpdisk:100",
		.gres_conf = "",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "tmpdisk", NULL, 100, 4, NULL, NULL, NULL, NULL,
			  A58_FLAGS },
			{ 0 },
		},
	},
	{
		.id = "a59",
		.node_gres = "gpu:tesla:2,gpu:gtx:1",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[0-4]\n"
			"Name=gpu Type=tesla File=DEV_PREFIX5\n"
			"Name=gpu Type=special File=DEV_PREFIX6\n"
			"Name=gpu Type=gtx File=DEV_PREFIX7\n"
			"Name=nic Count=100\n"
			"Name=tmpdisk Count=10G\n",
		.fake_gpus_conf =
			"tesla|4|0-1|-1|DEV_PREFIX0\n"
			"tesla|4|0-1|-1|DEV_PREFIX1\n"
			"tesla|4|0-1|-1|DEV_PREFIX2\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", "-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "gtx", 1, 4, NULL, NULL, "DEV_PREFIX7", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
		.log_regex =
			"warning: A line in gres.conf for GRES [^[:space:]]+ has [0-9]+ more configured than expected in slurm.conf\\. Ignoring extra GRES\\.",
		.log_cnt = 5,
	},
	{
		.id = "a60",
		.node_gres = "gpu:tesla:4",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla|4|0-1|(null)|DEV_PREFIX1|MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/7/0\n"
			"tesla|4|0-1|(null)|DEV_PREFIX2|MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/10/0\n"
			"tesla|4|2-3|(null)|DEV_PREFIX3|MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/8/0\n"
			"tesla|4|2-3|(null)|DEV_PREFIX4|MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/9/0\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  "MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/7/0",
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX2",
			  "MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/10/0",
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  "MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/8/0",
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  "MIG-GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee/9/0",
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "b2",
		.node_gres = "gpu:tesla:1,gpu:1080:1,gpu:gtx560:1,mps:300",
		.gres_conf = "",
		.fake_gpus_conf =
			"tesla|4|0-1|(null)|DEV_PREFIX1\n"
			"1080ti|4|0-1|(null)|DEV_PREFIX2\n"
			"gtx560|4|0-1|(null)|DEV_PREFIX0\n",
		.expected = (exp_rec_t[]) {
			{ "mps", "gtx560", 100, 4, "0-1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "gtx560", 1, 4, "0-1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "mps", "tesla", 100, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "mps", "1080", 100, 4, "0-1", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "1080", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		/*
		 * gres.conf records whose Cores (DEV_PREFIX0) or Links
		 * (DEV_PREFIX[3-4]) don't match the detected GPUs are ignored
		 * with an error, and so are those detected GPUs.
		 * gres.conf.5 Links: a line with Links can only contain a single
		 * File.
		 */
		.id = "b4",
		.node_gres = "gpu:ti:3,gpu:gtx:3",
		.gres_conf =
			"Name=gpu Type=ti File=DEV_PREFIX0 COREs=0\n"
			"Name=gpu Type=ti File=DEV_PREFIX[1-2] COREs=0-1\n"
			"Name=gpu Type=gtx File=DEV_PREFIX3 COREs=0-1 Links=-1,0,0,0,0,0\n"
			"Name=gpu Type=gtx File=DEV_PREFIX4 COREs=0-1 Links=-1,0,0,0,0,0\n"
			"Name=gpu Type=gtx File=DEV_PREFIX5 COREs=0-1 Links=-1,0,0,0,0,0\n",
		.fake_gpus_conf =
			"1080ti|4|0-1|(null)|DEV_PREFIX0\n"
			"1080ti|4|0-1|(null)|DEV_PREFIX1\n"
			"1080ti|4|0-1|(null)|DEV_PREFIX2\n"
			"gtx560|4|0-1|0,0,0,-1,0,0|DEV_PREFIX3\n"
			"gtx560|4|0-1|0,0,0,0,-1,0|DEV_PREFIX4\n"
			"gtx560|4|0-1|-1,0,0,0,0,0|DEV_PREFIX5\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "gtx", 1, 4, "0-1", "-1,0,0,0,0,0",
			  "DEV_PREFIX5", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "ti", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "ti", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
		.log_regex =
			"error: This GPU specified in .*conf has mismatching " B4_MISMATCH,
		.log_cnt = 3,
	},
	{
		.id = "b5",
		.node_gres = "gpu:a100:8",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 COREs=0-1  Links=0,0,0,-1,0,0,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 COREs=0-1  Links=0,0,-1,0,0,0,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX2 COREs=0-1  Links=0,-1,0,0,0,0,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX3 COREs=0-1  Links=-1,0,0,0,0,0,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX4 COREs=2-3 Links=0,0,0,0,0,0,0,-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX5 COREs=2-3 Links=0,0,0,0,0,0,-1,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX6 COREs=2-3 Links=0,0,0,0,0,-1,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX7 COREs=2-3 Links=0,0,0,0,-1,0,0,0\n",
		.fake_gpus_conf =
			"a100|4|0-1|-1,0,0,0,0,0,0,0|DEV_PREFIX3\n"
			"a100|4|0-1|0,-1,0,0,0,0,0,0|DEV_PREFIX2\n"
			"a100|4|0-1|0,0,-1,0,0,0,0,0|DEV_PREFIX1\n"
			"a100|4|0-1|0,0,0,-1,0,0,0,0|DEV_PREFIX0\n"
			"a100|4|2-3|0,0,0,0,-1,0,0,0|DEV_PREFIX7\n"
			"a100|4|2-3|0,0,0,0,0,-1,0,0|DEV_PREFIX6\n"
			"a100|4|2-3|0,0,0,0,0,0,-1,0|DEV_PREFIX5\n"
			"a100|4|2-3|0,0,0,0,0,0,0,-1|DEV_PREFIX4\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", "-1,0,0,0,0,0,0,0",
			  "DEV_PREFIX3", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,-1,0,0,0,0,0,0",
			  "DEV_PREFIX2", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,0,-1,0,0,0,0,0",
			  "DEV_PREFIX1", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,0,0,-1,0,0,0,0",
			  "DEV_PREFIX0", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", "0,0,0,0,-1,0,0,0",
			  "DEV_PREFIX7", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", "0,0,0,0,0,-1,0,0",
			  "DEV_PREFIX6", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", "0,0,0,0,0,0,-1,0",
			  "DEV_PREFIX5", NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", "0,0,0,0,0,0,0,-1",
			  "DEV_PREFIX4", NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "b6",
		.node_gres = "gpu:1080:3",
		.gres_conf =
			"Name=gpu Type=1080 File=DEV_PREFIX[0-2] COREs=0-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "1080", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "1080", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "1080", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "b7",
		.node_gres = "gpu:a100:8",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 COREs=0-1  Links=0,0,0,-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 COREs=0-1  Links=0,0,-1,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX2 COREs=0-1  Links=0,-1,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX3 COREs=0-1  Links=-1,0,0,0\n"
			"Name=gpu Type=a100 File=DEV_PREFIX4 COREs=2-3\n"
			"Name=gpu Type=a100 File=DEV_PREFIX5 COREs=2-3\n"
			"Name=gpu Type=a100 File=DEV_PREFIX6 COREs=2-3\n"
			"Name=gpu Type=a100 File=DEV_PREFIX7 COREs=2-3\n",
		.fake_gpus_conf =
			"a100|4|0-1|-1,0,0,0|DEV_PREFIX3\n"
			"a100|4|0-1|0,-1,0,0|DEV_PREFIX2\n"
			"a100|4|0-1|0,0,-1,0|DEV_PREFIX1\n"
			"a100|4|0-1|0,0,0,-1|DEV_PREFIX0\n"
			"a100|4|2-3|(null)|DEV_PREFIX7\n"
			"a100|4|2-3|(null)|DEV_PREFIX6\n"
			"a100|4|2-3|(null)|DEV_PREFIX5\n"
			"a100|4|2-3|(null)|DEV_PREFIX4\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", "-1,0,0,0", "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,-1,0,0", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,0,-1,0", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", "0,0,0,-1", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", NULL, "DEV_PREFIX5", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", NULL, "DEV_PREFIX6", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "2-3", NULL, "DEV_PREFIX7", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		/* gres.conf.5 Links: a line with Links can only contain a single File */
		.id = "b8",
		.node_gres = "gpu:5",
		.gres_conf =
			"Name=gpu File=DEV_PREFIX[0-1] Cores=0,1\n"
			"Name=gpu File=DEV_PREFIX2 Cores=0,1 Links=-1\n"
			"Name=gpu File=DEV_PREFIX3 Cores=0,1 Links=-1\n"
			"Name=gpu File=DEV_PREFIX4 Cores=0\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, "0,1", "-1", "DEV_PREFIX2", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0,1", "-1", "DEV_PREFIX3", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0,1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0,1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "b10",
		.node_gres = "gpu:tesla:1,gpu:1",
		.gres_conf = "",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"GRES in slurm.conf have a type while others do not",
		.log_cnt = 1,
	},
	{
		.id = "b12",
		.node_gres = "gpu:tesla:2,gpu:v100:2",
		.gres_conf =
			"Name=gpu            File=DEV_PREFIX[0-1]\n"
			"Name=gpu Type=v100  File=DEV_PREFIX[2-3]\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: gres.conf for [^[:space:]]+, some records have \"Type=\" specification while others do not",
		.log_cnt = 1,
	},
	{
		.id = "b14",
		.node_gres = "gpu:tesla:2,gpu:v100:2",
		.gres_conf =
			"Name=gpu Type=tesla  File=DEV_PREFIX[0-1]\n"
			"Name=gpu Type=v100\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: gres.conf for [^[:space:]]+, some records have \"File\" specification while others do not",
		.log_cnt = 1,
	},
	{
		.id = "c2",
		.node_gres = "gpu:tesla:1,gpu:gtx560:1,mps:200",
		.gres_conf =
			"Name=gpu Type=gtx560 File=DEV_PREFIX0 COREs=0,1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 COREs=2,3\n"
			"Name=mps Count=100 File=DEV_PREFIX1\n"
			"Name=mps Count=100 File=DEV_PREFIX0\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "mps", "gtx560", 100, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "gtx560", 1, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "mps", "tesla", 100, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c4",
		.node_gres = "gpu:tesla:1,gpu:gtx560:1,mps:210",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 COREs=2,3\n"
			"Name=gpu Type=gtx560 File=DEV_PREFIX0 COREs=0,1\n"
			"Name=mps Count=210\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "mps", "gtx560", 105, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL ",SHARED,ONE_SHARING" },
			{ "gpu", "gtx560", 1, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "mps", "tesla", 105, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL ",SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c6",
		.node_gres = "gpu:tesla:3",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[3-4] Cores=2-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-1\n",
		.fake_gpus_conf =
			"tesla|4|0-1|(null)|DEV_PREFIX1\n"
			"tesla|4|0-1|(null)|DEV_PREFIX2\n"
			"tesla|4|2-3|(null)|DEV_PREFIX3\n"
			"tesla|4|2-3|(null)|DEV_PREFIX4\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "c8",
		.node_gres = "gpu:tesla:6",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=2-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2 Cores=2-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=2-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=2-3\n",
		.fake_gpus_conf =
			"tesla|4|0-1|(null)|DEV_PREFIX5\n"
			"tesla|4|0-1|(null)|DEV_PREFIX6\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX6",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "c10",
		.node_gres = "gpu:tesla:8",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX0 Cores=0-3 Links=-1,2,2,0,0,0,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-3 Links=2,-1,2,0,0,0,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2 Cores=0-3 Links=2,2,-1,0,0,0,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=0-3 Links=0,0,0,-1,0,1,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=0-3 Links=0,0,0,0,-1,1,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX5 Cores=0-3 Links=0,0,0,1,1,-1,0,0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX6 Cores=0-3 Links=0,0,0,0,0,0,-1,2\n"
			"Name=gpu Type=tesla File=DEV_PREFIX7 Cores=0-3 Links=0,0,0,0,0,0,2,-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-3", "-1,2,2,0,0,0,0,0",
			  "DEV_PREFIX0", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "2,-1,2,0,0,0,0,0",
			  "DEV_PREFIX1", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "2,2,-1,0,0,0,0,0",
			  "DEV_PREFIX2", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,0,0,-1,0,1,0,0",
			  "DEV_PREFIX3", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,0,0,0,-1,1,0,0",
			  "DEV_PREFIX4", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,0,0,1,1,-1,0,0",
			  "DEV_PREFIX5", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,0,0,0,0,0,-1,2",
			  "DEV_PREFIX6", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,0,0,0,0,0,2,-1",
			  "DEV_PREFIX7", NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c12",
		.node_gres = "",
		.gres_conf = "",
		.fake_gpus_conf = "",
	},
	{
		.id = "c14",
		.node_gres = "gpu:tesla:4",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[1-4] Cores=2-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c16",
		.node_gres = "gpu:tesla:1,tmpdisk:disky:1,nic:nikki:1,mic:mickey:1",
		.gres_conf =
			"Name=gpu     Type=tesla File=DEV_PREFIX1 Cores=0-3\n"
			"Name=tmpdisk Type=disky File=DEV_PREFIX2 Cores=0-3\n"
			"Name=nic     Type=nikki File=DEV_PREFIX3 Cores=0-3\n"
			"Name=mic     Type=mickey File=DEV_PREFIX4 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "nic", "nikki", 1, 4, "0-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "mic", "mickey", 1, 4, "0-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "tmpdisk", "disky", 1, 4, "0-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE" },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c17",
		.node_gres = "gpu:1,tmpdisk:1,nic:1,mic:1",
		.gres_conf =
			"Name=gpu     File=DEV_PREFIX1 Cores=0-3\n"
			"Name=tmpdisk File=DEV_PREFIX2 Cores=0-3\n"
			"Name=nic     File=DEV_PREFIX3 Cores=0-3\n"
			"Name=mic     File=DEV_PREFIX4 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "nic", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE" },
			{ "mic", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE" },
			{ "tmpdisk", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE" },
			{ "gpu", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c18",
		.node_gres = "gpu:tesla:2",
		.gres_conf =
			"NodeName=tux0 Name=gpu     Type=tesla File=DEV_PREFIX1 Cores=0-3\n"
			"NodeName=tux0              Type=tesla File=DEV_PREFIX2 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
		.log_regex = "error: Invalid GRES data, no type name",
		.log_cnt = 1,
	},
	{
		.id = "c20",
		.node_gres = "",
		.gres_conf = "Type=tesla File=DEV_PREFIX1 Cores=0-3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"error: _parse_next_key: Parsing error at unrecognized key",
		.log_cnt = 1,
	},
	{
		.id = "c22",
		.node_gres = "tmpdisk:10g",
		.gres_conf = "Name=tmpdisk count=10g\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "tmpdisk", NULL, 10737418240, 4, NULL, NULL, NULL,
			  NULL, "" },
			{ 0 },
		},
	},
	{
		.id = "c23",
		.node_gres = "tmpdisk:tempy:10g",
		.gres_conf = "Name=tmpdisk Type=tempy Count=10g Cores=0-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "tmpdisk", "tempy", 10737418240, 4, "0-1", NULL, NULL,
			  NULL, "HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "c24",
		.node_gres = "gpu:a:2,gpu:b:2",
		.gres_conf =
			"Name=gpu Type=a File=DEV_PREFIX1 Cores=0-3\n"
			"Name=gpu Type=a File=DEV_PREFIX2 Cores=0-3\n"
			"Name=gpu Type=b File=DEV_PREFIX5 Cores=0-3\n"
			"Name=gpu Type=b File=DEV_PREFIX6 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a", 1, 4, "0-3", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "a", 1, 4, "0-3", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "b", 1, 4, "0-3", NULL, "DEV_PREFIX5", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "b", 1, 4, "0-3", NULL, "DEV_PREFIX6", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c28",
		.node_gres = "gpu:tesla_a:4,gpu:tesla_b:4",
		.gres_conf =
			"Name=gpu Type=tesla_a File=DEV_PREFIX[1-2] Cores=0-3\n"
			"Name=gpu Type=tesla_b File=DEV_PREFIX[3-4] Cores=0-3\n"
			"Name=gpu Type=tesla_a File=DEV_PREFIX5       Cores=0-3\n"
			"Name=gpu Type=tesla_b File=DEV_PREFIX6       Cores=0-3\n"
			"Name=gpu Type=tesla_a File=DEV_PREFIX7       Cores=0-3\n"
			"Name=gpu Type=tesla_b File=DEV_PREFIX8       Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla_a", 1, 4, "0-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_a", 1, 4, "0-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_b", 1, 4, "0-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_b", 1, 4, "0-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_a", 1, 4, "0-3", NULL, "DEV_PREFIX5",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_b", 1, 4, "0-3", NULL, "DEV_PREFIX6",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_a", 1, 4, "0-3", NULL, "DEV_PREFIX7",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla_b", 1, 4, "0-3", NULL, "DEV_PREFIX8",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c30",
		.node_gres = "gpu:tesla:6",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX3       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[3-4] Cores=0-3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "error: gpu duplicate device file name",
		.log_cnt = 1,
	},
	{
		.id = "c32",
		.node_gres = "gpu:tesla:10",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[1-2] Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[1-3] Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[1-4] Cores=0-3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "error: gpu duplicate device file name",
		.log_cnt = 1,
	},
	{
		.id = "c34",
		.node_gres = "gpu:tesla:10",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX4       Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[3-4] Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[2-4] Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX[1-4] Cores=0-3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "error: gpu duplicate device file name",
		.log_cnt = 1,
	},
	{
		.id = "c36",
		.node_gres = "gpu:tesla:4",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-2\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "error: gpu duplicate device file name",
		.log_cnt = 1,
	},
	{
		.id = "c38",
		.node_gres = "gpu:tesla:4",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2 Cores=0-1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=0-2\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-1", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-2", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c40",
		.node_gres = "gpu:tesla:4",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2 Cores=0-3 Links=\"\"\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=0-3 Links=null\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=0-3 Links=0\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
		.log_regex = "error: gres.conf: Ignoring invalid Link",
		.log_cnt = 3,
	},
	{
		.id = "c42",
		.node_gres = "gpu:tesla:4",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 Cores=0-3 Links=0-1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2 Cores=0-3 Links=0,-1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=0-3 Links=0-2\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=0-3 Links=0,-1,2\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0-3", "0,-1", "DEV_PREFIX2",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", "0,-1,2", "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0-3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c46",
		.node_gres = "gpu:4",
		.gres_conf =
			"Name=gpu File=DEV_PREFIX1 Cores=0-3\n"
			"Name=gpu File=DEV_PREFIX2 Cores=0-3\n"
			"Name=gpu File=DEV_PREFIX3 Cores=0-3\n"
			"Name=gpu File=DEV_PREFIX4 Cores=0-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, "0-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c48",
		.node_gres = "gpu:tesla:5",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX2\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 Cores=\"\"\n"
			"Name=gpu Type=tesla File=DEV_PREFIX4 Cores=null\n"
			"Name=gpu Type=tesla File=DEV_PREFIX5 Cores=0\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, NULL, NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, NULL, NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "null", NULL, "DEV_PREFIX4",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0", NULL, "DEV_PREFIX5", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c50",
		.node_gres = "gpu:gtx560:1,gpu:tesla:1,mps:200",
		.gres_conf =
			"Name=gpu Type=gtx560 File=DEV_PREFIX0 COREs=0,1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX1 COREs=2,3\n"
			"Name=mps Count=100 File=DEV_PREFIX0\n"
			"Name=mps Count=100 File=DEV_PREFIX1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "mps", "gtx560", 100, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "gtx560", 1, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "mps", "tesla", 100, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c52",
		.node_gres = "gpu:tesla:3",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX[0-1] COREs=0,1\n"
			"# NOTE: nvidia2 device is out of service\n"
			"# Name=gpu Type=tesla File=DEV_PREFIX[2-3] COREs=2,3\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 COREs=2,3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "tesla", 1, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "0,1", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c54",
		.node_gres = "gpu:4",
		.gres_conf =
			"## Explicitly specify devices on nodes tux0-tux15\n"
			"# NodeName=tux[0-15] Name=gpu File=DEV_PREFIX[0-3]\n"
			"# NOTE: tux3 DEV_PREFIX1 device is out of service\n"
			"NodeName=tux[0-2] Name=gpu File=DEV_PREFIX[0-3]\n"
			"NodeName=tux3 Name=gpu File=DEV_PREFIX[0,2-3]\n"
			"NodeName=tux[4-15] Name=gpu File=DEV_PREFIX[0-3]\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE," ENV_ALL },
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "c56",
		.node_gres = "gpu:gtx560:1,gpu:tesla:2,mps:900",
		.gres_conf =
			"Name=gpu Type=tesla File=DEV_PREFIX1 COREs=2,3\n"
			"Name=gpu Type=gtx560 File=DEV_PREFIX0 COREs=0,1\n"
			"Name=gpu Type=tesla File=DEV_PREFIX3 COREs=2,3\n"
			"Name=mps Count=200 File=DEV_PREFIX1\n"
			"Name=mps Count=300 File=DEV_PREFIX3\n"
			"Name=mps Count=400 File=DEV_PREFIX8\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "mps", "gtx560", 0, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL ",SHARED,ONE_SHARING" },
			{ "gpu", "gtx560", 1, 4, "0,1", NULL, "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "mps", "tesla", 200, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "mps", "tesla", 300, 4, "2,3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE,SHARED,ONE_SHARING" },
			{ "gpu", "tesla", 1, 4, "2,3", NULL, "DEV_PREFIX3",
			  NULL, "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "d1",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX2 Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX3 Cores=0-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		.id = "d2",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=no_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 Cores=0-1 Flags=no_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX2 Cores=0-1 Flags=no_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX3 Cores=0-1 Flags=no_gpu_env\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "d3",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"NodeName=tux0      Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=amd_gpu_env\n"
			"NodeName=tux0      Name=gpu Type=a100 File=DEV_PREFIX1 Cores=0-1 Flags=amd_gpu_env\n"
			"NodeName=tux1 Name=gpu Type=a100 File=DEV_PREFIX2 Cores=0-1 Flags=nvidia_gpu_env\n"
			"NodeName=tux1 Name=gpu Type=a100 File=DEV_PREFIX3 Cores=0-1 Flags=nvidia_gpu_env\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ 0 },
		},
	},
	{
		.id = "d4",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0       Cores=0-1 Flags=no_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX[1-3] Cores=0-1 Flags=no_gpu_env\n",
		.fake_gpus_conf =
			"a100|4|0-1|(null)|DEV_PREFIX0|(null)|nvidia_gpu_env\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE" },
			{ 0 },
		},
	},
	{
		.id = "d5",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0       Cores=0-1 Flags=nvidia_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX[1-3] Cores=0-1 Flags=nvidia_gpu_env\n",
		.fake_gpus_conf =
			"a100|4|0-1|(null)|DEV_PREFIX0|(null)|nvidia_gpu_env\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ 0 },
		},
	},
	{
		.id = "d6",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX[1-3] Cores=0-1 Flags=nvidia_gpu_env\n",
		.fake_gpus_conf =
			"a100|4|0-1|(null)|DEV_PREFIX0|(null)|nvidia_gpu_env\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML" },
			{ 0 },
		},
	},
	{
		.id = "d8",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=amd_gpu_env,nvidia_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX2 Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX3 Cores=0-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML,ENV_RSMI" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML,ENV_RSMI" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML,ENV_RSMI" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_NVML,ENV_RSMI" },
			{ 0 },
		},
	},
	{
		.id = "d9",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX[0-1] Cores=0-1 Flags=no_gpu_env\n"
			"Name=gpu Type=a100 File=DEV_PREFIX[2-3] Cores=0-1 Flags=nvidia_gpu_env\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: Invalid GRES record name=[^[:space:]]+ type=[^[:space:]]+: Flags \\([^[:space:]]+\\) does not match env flags for previous GRES of same node and name",
		.log_cnt = 1,
	},
	{
		.id = "d10",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0       Cores=0-1\n"
			"Name=gpu Type=a100 File=DEV_PREFIX[1-3] Cores=0-1 Flags=nvidia_gpu_env\n",
		.fake_gpus_conf =
			"a100|4|0-1|(null)|DEV_PREFIX0|(null)|nvidia_gpu_env\n",
		.fail = true,
		.log_regex =
			"fatal: Invalid GRES record name=[^[:space:]]+ type=[^[:space:]]+: Flags \\([^[:space:]]+\\) does not match env flags for previous GRES of same node and name",
		.log_cnt = 1,
	},
	{
		.id = "d11",
		.node_gres = "gpu:a100:1,gpu:b100:1,gpu:c100:1,gpu:d100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=amd_gpu_env\n"
			"Name=gpu Type=b100 File=DEV_PREFIX1 Cores=0-1\n"
			"Name=gpu Type=c100 File=DEV_PREFIX2 Cores=0-1\n"
			"Name=gpu Type=d100 File=DEV_PREFIX3 Cores=0-1\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ "gpu", "b100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ "gpu", "c100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ "gpu", "d100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_RSMI" },
			{ 0 },
		},
	},
	{
		/* gres.conf.5 Flags: intel_gpu_env sets ZE_AFFINITY_MASK */
		.id = "e1",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=intel_gpu_env\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_ONEAPI" },
			{ 0 },
		},
	},
	{
		/* gres.conf.5 Flags: opencl_env sets GPU_DEVICE_ORDINAL */
		.id = "e2",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=opencl_env\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE,ENV_OPENCL" },
			{ 0 },
		},
	},
	{
		/*
		 * gres.conf.5 Flags: no_gpu_env is mutually exclusive to all
		 * other environment-related flags.
		 */
		.id = "e3",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=no_gpu_env,nvidia_gpu_env\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: .*contains \"no_gpu_env\", which must be mutually exclusive",
		.log_cnt = 1,
	},
	{
		/*
		 * gres.conf.5 File: if File is specified then Count must be
		 * either set to the number of file names specified or not set.
		 */
		.id = "e4",
		.node_gres = "gpu:3",
		.gres_conf = "Name=gpu File=DEV_PREFIX[0-1] Count=3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "fatal: .*count does not match File value",
		.log_cnt = 1,
	},
	{
		/*
		 * gres.conf.5 MultipleFiles: several device files managed as a
		 * single generic resource.
		 */
		.id = "e5",
		.node_gres = "gpu:1",
		.gres_conf = "Name=gpu MultipleFiles=DEV_PREFIX[0-1]\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", NULL, 1, 4, NULL, NULL, "DEV_PREFIX[0-1]", NULL,
			  "HAS_FILE," ENV_ALL },
			{ 0 },
		},
	},
	{
		/* gres.conf.5: File and MultipleFiles are mutually exclusive */
		.id = "e6",
		.node_gres = "gpu:2",
		.gres_conf =
			"Name=gpu File=DEV_PREFIX0 MultipleFiles=DEV_PREFIX[1-2]\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: File and MultipleFiles options are mutually exclusive",
		.log_cnt = 1,
	},
	{
		/* Same as e3, with amd_gpu_env */
		.id = "e7",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=no_gpu_env,amd_gpu_env\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: .*contains \"no_gpu_env\", which must be mutually exclusive",
		.log_cnt = 1,
	},
	{
		/* Same as e3, with intel_gpu_env */
		.id = "e8",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=no_gpu_env,intel_gpu_env\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: .*contains \"no_gpu_env\", which must be mutually exclusive",
		.log_cnt = 1,
	},
	{
		/* Same as e3, with opencl_env */
		.id = "e9",
		.node_gres = "gpu:a100:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Cores=0-1 Flags=no_gpu_env,opencl_env\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: .*contains \"no_gpu_env\", which must be mutually exclusive",
		.log_cnt = 1,
	},
	{
		/*
		 * gres.conf.5 Count and slurm.conf.5 Gres: a suffix of K, M, G,
		 * T or P multiplies by 1024, 1048576, 1073741824, etc.
		 * CountOnly is configured as in the gres.conf.5 example.
		 */
		.id = "e10",
		.node_gres = "nic:1K,mic:1M,tmpdisk:1T",
		.gres_conf =
			"Name=nic Count=1K Flags=CountOnly\n"
			"Name=mic Count=1M Flags=CountOnly\n"
			"Name=tmpdisk Count=1T Flags=CountOnly\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "nic", NULL, 1024ULL, 4, NULL, NULL, NULL, NULL,
			  "CountOnly" },
			{ "mic", NULL, 1048576ULL, 4, NULL, NULL, NULL, NULL,
			  "CountOnly" },
			{ "tmpdisk", NULL, 1099511627776ULL, 4, NULL, NULL, NULL,
			  NULL, "CountOnly" },
			{ 0 },
		},
	},
	{
		/* Same as e10 for the P suffix */
		.id = "e11",
		.node_gres = "tmpdisk:1P",
		.gres_conf = "Name=tmpdisk Count=1P Flags=CountOnly\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "tmpdisk", NULL, 1125899906842624ULL, 4, NULL, NULL,
			  NULL, NULL, "CountOnly" },
			{ 0 },
		},
	},
#ifdef HAVE_GRES_CHUNKS
	{
		/*
		 * gres.conf.5 ChunkSizes and ChunkOrder are kept on every
		 * record of their Type, and a Type that sets none has none
		 */
		.id = "f1",
		.node_gres = "gpu:a100:4,gpu:l4:1",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX[0-3] Cores=0-1 ChunkSizes=2,4 ChunkOrder=0,2,1,3\n"
			"Name=gpu Type=l4 File=DEV_PREFIX4 Cores=2-3\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2,4", "0,2,1,3" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2,4", "0,2,1,3" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX2", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2,4", "0,2,1,3" },
			{ "gpu", "a100", 1, 4, "0-1", NULL, "DEV_PREFIX3", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2,4", "0,2,1,3" },
			{ "gpu", "l4", 1, 4, "2-3", NULL, "DEV_PREFIX4", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL },
			{ 0 },
		},
	},
	{
		/*
		 * gres.conf.5 ChunkSizes: the chunk settings come from gres.conf
		 * and are kept when a detected GPU replaces the record
		 */
		.id = "f2",
		.node_gres = "gpu:a100:2",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX[0-1] ChunkSizes=2\n",
		.fake_gpus_conf =
			"a100|4|0-1|-1,0|DEV_PREFIX0\n"
			"a100|4|0-1|0,-1|DEV_PREFIX1\n",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, "0-1", "-1,0", "DEV_PREFIX0",
			  NULL, "HAS_FILE,HAS_TYPE", "2" },
			{ "gpu", "a100", 1, 4, "0-1", "0,-1", "DEV_PREFIX1",
			  NULL, "HAS_FILE,HAS_TYPE", "2" },
			{ 0 },
		},
	},
	{
		/* gres.conf.5 ChunkSizes: Links is ignored, with a warning */
		.id = "f3",
		.node_gres = "gpu:a100:2",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX0 Links=-1,2 ChunkSizes=2\n"
			"Name=gpu Type=a100 File=DEV_PREFIX1 Links=2,-1 ChunkSizes=2\n",
		.fake_gpus_conf = "",
		.expected = (exp_rec_t[]) {
			{ "gpu", "a100", 1, 4, NULL, NULL, "DEV_PREFIX0", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2" },
			{ "gpu", "a100", 1, 4, NULL, NULL, "DEV_PREFIX1", NULL,
			  "HAS_FILE,HAS_TYPE," ENV_ALL, "2" },
			{ 0 },
		},
		.log_regex =
			"warning: gres.conf: Ignoring Links for Name=gpu Type=a100, ChunkSizes takes precedence",
		.log_cnt = 2,
	},
	{
		/* gres.conf.5 ChunkOrder: requires ChunkSizes */
		.id = "f4",
		.node_gres = "gpu:2",
		.gres_conf = "Name=gpu File=DEV_PREFIX[0-1] ChunkOrder=1,0\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "fatal: .*ChunkOrder requires ChunkSizes",
		.log_cnt = 1,
	},
	{
		/* gres.conf.5 ChunkSizes: each size is a multiple of the last */
		.id = "f5",
		.node_gres = "gpu:6",
		.gres_conf = "Name=gpu File=DEV_PREFIX[0-5] ChunkSizes=2,3\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "fatal: .*invalid ChunkSizes 2,3",
		.log_cnt = 1,
	},
	{
		/* gres.conf.5 ChunkOrder: a permutation, so no repeats */
		.id = "f6",
		.node_gres = "gpu:2",
		.gres_conf =
			"Name=gpu File=DEV_PREFIX[0-1] ChunkSizes=2 ChunkOrder=0,0\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "fatal: .*invalid ChunkOrder 0,0",
		.log_cnt = 1,
	},
	{
		/* gres.conf.5 ChunkSizes: requires File or MultipleFiles */
		.id = "f7",
		.node_gres = "gpu:2",
		.gres_conf = "Name=gpu Count=2 ChunkSizes=2\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex =
			"fatal: .*ChunkSizes requires File or MultipleFiles",
		.log_cnt = 1,
	},
	{
		/*
		 * gres.conf.5 ChunkSizes: every line of a Type must set the same
		 * values
		 */
		.id = "f8",
		.node_gres = "gpu:a100:4",
		.gres_conf =
			"Name=gpu Type=a100 File=DEV_PREFIX[0-1] ChunkSizes=2\n"
			"Name=gpu Type=a100 File=DEV_PREFIX[2-3] ChunkSizes=2,4\n",
		.fake_gpus_conf = "",
		.fail = true,
		.log_regex = "fatal: .*ChunkSizes and ChunkOrder must match",
		.log_cnt = 1,
	},
#endif
};
// clang-format on

static char *base_dir = NULL;

static char *_subst_dev(const char *str, const char *dev_prefix)
{
	char *out = xstrdup(str);

	xstrsubstituteall(out, DEV_PREFIX, dev_prefix);
	return out;
}

static void _write_file(const char *dir, const char *name, const char *content,
			const char *dev_prefix)
{
	char *path = NULL, *data = _subst_dev(content, dev_prefix);
	FILE *fp = NULL;

	xstrfmtcat(path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	if (!fp)
		ck_abort_msg("fopen(%s) failed: %s", path, strerror(errno));
	ck_assert_msg(fputs(data, fp) >= 0, "writing %s failed", path);
	fclose(fp);
	xfree(path);
	xfree(data);
}

static char *_read_file(const char *path)
{
	char buf[4096], *txt = NULL;
	size_t len = 0;
	FILE *fp = fopen(path, "r");

	if (!fp)
		return xstrdup_printf("(cannot open %s: %s)", path,
				      strerror(errno));
	while ((len = fread(buf, 1, sizeof(buf), fp)) > 0)
		xstrncat(txt, buf, len);
	fclose(fp);
	return txt;
}

/*
 * Create the configuration of a case in its own directory, and point
 * SLURM_CONF to it.
 * RET the case directory
 */
static char *_setup_case(const gres_case_t *tc, char **dev_prefix)
{
	char *case_dir = NULL, *path = NULL;

	xstrfmtcat(case_dir, "%s/%s", base_dir, tc->id);
	ck_assert_msg(!mkdir(case_dir, 0700), "mkdir(%s) failed: %s", case_dir,
		      strerror(errno));
	xstrfmtcat(*dev_prefix, "%s/nvidia", case_dir);

	for (int i = 0; i < DEV_CNT; i++) {
		char *dev = xstrdup_printf("nvidia%d", i);

		_write_file(case_dir, dev, "", *dev_prefix);
		xfree(dev);
	}
	_write_file(case_dir, "slurm.conf", SLURM_CONF_CONTENT, *dev_prefix);
	_write_file(case_dir, "gres.conf", tc->gres_conf, *dev_prefix);
	_write_file(case_dir, "fake_gpus.conf", tc->fake_gpus_conf,
		    *dev_prefix);

	xstrfmtcat(path, "%s/slurm.conf", case_dir);
	setenv("SLURM_CONF", path, 1);
	xfree(path);

	return case_dir;
}

/*
 * Load the GRES configuration of the node like slurmd does.
 * NOTE: No ck_assert*() here, it also runs in a child process of the test.
 */
static int _load_gres(const gres_case_t *tc)
{
	list_t *gres_list = NULL;
	int rc = SLURM_ERROR;

	slurm_init(NULL);
	if (select_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize node selection plugin");

	gres_init_node_config(tc->node_gres, &gres_list);
	rc = gres_g_node_config_load(CPU_CNT, NODE_NAME, gres_list, NULL, NULL);
	FREE_NULL_LIST(gres_list);

	return rc;
}

static char *_recs2str(list_t *conf_list)
{
	list_itr_t *itr = NULL;
	gres_slurmd_conf_t *rec = NULL;
	char *str = NULL;

	if (!conf_list)
		return xstrdup("(none)");

	itr = list_iterator_create(conf_list);
	while ((rec = list_next(itr))) {
		xstrfmtcat(
			str,
			"\n  Name=%s Type=%s Count=%" PRIu64
			" CpuCnt=%u Cores=%s Links=%s File=%s UniqueId=%s Flags=%s",
			rec->name, rec->type_name, rec->count, rec->cpu_cnt,
			rec->cpus, rec->links, rec->file, rec->unique_id,
			gres_flags2str(rec->config_flags));
#ifdef HAVE_GRES_CHUNKS
		xstrfmtcat(str, " ChunkSizes=%s ChunkOrder=%s",
			   rec->chunk_sizes, rec->chunk_order);
#endif
	}
	list_iterator_destroy(itr);

	return str;
}

static void _check_str(const char *id, int idx, const char *field,
		       const char *exp, const char *act, const char *recs)
{
	ck_assert_msg(!xstrcmp(exp, act),
		      "%s: record %d %s should be %s, but got %s. Records:%s",
		      id, idx, field, exp ? exp : "(null)",
		      act ? act : "(null)", recs);
}

static void _check_records(const gres_case_t *tc, const char *dev_prefix)
{
	list_t *conf_list = gres_get_node_conf_list();
	list_itr_t *itr = NULL;
	gres_slurmd_conf_t *rec = NULL;
	char *recs = _recs2str(conf_list);
	int exp_cnt = 0, act_cnt = conf_list ? list_count(conf_list) : 0;
	int idx = 0;

	while (tc->expected && tc->expected[exp_cnt].name)
		exp_cnt++;
	ck_assert_msg(act_cnt == exp_cnt,
		      "%s: there should be %d records, but got %d. Records:%s",
		      tc->id, exp_cnt, act_cnt, recs);
	if (!conf_list) {
		xfree(recs);
		return;
	}

	itr = list_iterator_create(conf_list);
	while ((rec = list_next(itr))) {
		const exp_rec_t *exp = &tc->expected[idx];
		char *exp_file =
			exp->file ? _subst_dev(exp->file, dev_prefix) : NULL;

		_check_str(tc->id, idx, "Name", exp->name, rec->name, recs);
		_check_str(tc->id, idx, "Type", exp->type, rec->type_name,
			   recs);
		ck_assert_msg(rec->count == exp->count,
			      "%s: record %d Count should be %" PRIu64
			      ", but got %" PRIu64 ". Records:%s",
			      tc->id, idx, exp->count, rec->count, recs);
		ck_assert_msg(
			rec->cpu_cnt == exp->cpu_cnt,
			"%s: record %d CpuCnt should be %u, but got %u. Records:%s",
			tc->id, idx, exp->cpu_cnt, rec->cpu_cnt, recs);
		_check_str(tc->id, idx, "Cores", exp->cpus, rec->cpus, recs);
		_check_str(tc->id, idx, "Links", exp->links, rec->links, recs);
		_check_str(tc->id, idx, "File", exp_file, rec->file, recs);
		_check_str(tc->id, idx, "UniqueId", exp->unique_id,
			   rec->unique_id, recs);
		_check_str(tc->id, idx, "Flags", exp->flags,
			   gres_flags2str(rec->config_flags), recs);
#ifdef HAVE_GRES_CHUNKS
		_check_str(tc->id, idx, "ChunkSizes", exp->chunk_sizes,
			   rec->chunk_sizes, recs);
		_check_str(tc->id, idx, "ChunkOrder", exp->chunk_order,
			   rec->chunk_order, recs);
#endif

		xfree(exp_file);
		idx++;
	}
	list_iterator_destroy(itr);

	FREE_NULL_LIST(conf_list);
	xfree(recs);
}

static void _check_not_shared(const char *id, int idx, const char *field,
			      const char *a, const char *b)
{
	ck_assert_msg(!a || (a != b),
		      "%s: record %d %s is shared by two copies of the list",
		      id, idx, field);
}

/*
 * gres_get_node_conf_list() must return a deep copy, or freeing it frees the
 * strings of the live list too. Compare two copies, as a shared string is the
 * same pointer in both.
 */
static void _check_deep_copy(const gres_case_t *tc)
{
	list_t *list_a = gres_get_node_conf_list();
	list_t *list_b = gres_get_node_conf_list();
	list_itr_t *itr_a = NULL, *itr_b = NULL;
	gres_slurmd_conf_t *a = NULL, *b = NULL;
	int idx = 0;

	if (!list_a) {
		FREE_NULL_LIST(list_b);
		return;
	}

	itr_a = list_iterator_create(list_a);
	itr_b = list_iterator_create(list_b);
	while ((a = list_next(itr_a)) && (b = list_next(itr_b))) {
		_check_not_shared(tc->id, idx, "Name", a->name, b->name);
		_check_not_shared(tc->id, idx, "Type", a->type_name,
				  b->type_name);
		_check_not_shared(tc->id, idx, "Cores", a->cpus, b->cpus);
		_check_not_shared(tc->id, idx, "Links", a->links, b->links);
		_check_not_shared(tc->id, idx, "File", a->file, b->file);
		_check_not_shared(tc->id, idx, "UniqueId", a->unique_id,
				  b->unique_id);
#ifdef HAVE_GRES_CHUNKS
		_check_not_shared(tc->id, idx, "ChunkSizes", a->chunk_sizes,
				  b->chunk_sizes);
		_check_not_shared(tc->id, idx, "ChunkOrder", a->chunk_order,
				  b->chunk_order);
#endif
		idx++;
	}
	list_iterator_destroy(itr_a);
	list_iterator_destroy(itr_b);

	FREE_NULL_LIST(list_a);
	FREE_NULL_LIST(list_b);
}

/* RET the number of lines of log_txt matching the extended regex pattern */
static int _log_count(const char *log_txt, const char *pattern)
{
	regex_t re = { 0 };
	char *copy, *line, *save_ptr = NULL;
	int cnt = 0;

	ck_assert_msg(!regcomp(&re, pattern, (REG_EXTENDED | REG_NOSUB)),
		      "invalid regex: %s", pattern);

	copy = xstrdup(log_txt);
	line = copy ? strtok_r(copy, "\n", &save_ptr) : NULL;
	while (line) {
		if (!regexec(&re, line, 0, NULL, 0))
			cnt++;
		line = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(copy);
	regfree(&re);

	return cnt;
}

static void _check_log(const gres_case_t *tc, const char *log_txt)
{
	int cnt = 0;

	if (!tc->log_regex)
		return;

	cnt = _log_count(log_txt, tc->log_regex);
	ck_assert_msg(
		cnt == tc->log_cnt,
		"%s: there should be %d log lines matching \"%s\", but got %d. Log:\n%s",
		tc->id, tc->log_cnt, tc->log_regex, cnt, log_txt);
}

/* Log into log_path, and also into stderr up to stderr_level */
static int _log_to_file(char *log_path, log_level_t stderr_level)
{
	log_options_t opts = LOG_OPTS_INITIALIZER;

	opts.stderr_level = stderr_level;
	opts.syslog_level = LOG_LEVEL_QUIET;
	opts.logfile_level = LOG_LEVEL_INFO;

	return log_alter(opts, 0, log_path);
}

/*
 * fatal() logs and exit()s, so load the configuration in a child process
 * logging to a file, and check its exit code and the messages logged.
 * The child exits with FATAL_EXIT_CODE if fatal() is called, or with
 * ERROR_EXIT_CODE if loading returns an error.
 */
static void _check_fail(const gres_case_t *tc, char *log_path)
{
	char *log_txt = NULL;
	pid_t pid = 0;
	int status = 0;

	pid = fork();
	ck_assert_msg(pid >= 0, "fork() failed: %s", strerror(errno));
	if (!pid) {
		/* Make fatal() exit() instead of abort() */
		unsetenv("ABORT_ON_FATAL");
		if (_log_to_file(log_path, LOG_LEVEL_QUIET))
			_exit(LOG_EXIT_CODE);

		if (_load_gres(tc) != SLURM_SUCCESS)
			_exit(ERROR_EXIT_CODE);
		_exit(0);
	}

	ck_assert_msg(waitpid(pid, &status, 0) == pid, "waitpid() failed: %s",
		      strerror(errno));
	log_txt = _read_file(log_path);

	ck_assert_msg(
		WIFEXITED(status) &&
			((WEXITSTATUS(status) == FATAL_EXIT_CODE) ||
			 (WEXITSTATUS(status) == ERROR_EXIT_CODE)),
		"%s: loading the GRES configuration should fail (exit code %d for fatal() or %d for an error returned), but got wait status 0x%x. Log:\n%s",
		tc->id, FATAL_EXIT_CODE, ERROR_EXIT_CODE, status, log_txt);
	_check_log(tc, log_txt);

	xfree(log_txt);
}

/*
 * Load the configuration logging to a file, and check the final list and the
 * messages logged. Keep logging into stderr too, to show any unexpected
 * fatal() in the test output.
 */
static void _check_success(const gres_case_t *tc, char *log_path,
			   const char *dev_prefix)
{
	char *log_txt = NULL;
	int rc = SLURM_ERROR;

	ck_assert_msg(!_log_to_file(log_path, LOG_LEVEL_INFO),
		      "%s: unable to log into %s", tc->id, log_path);
	rc = _load_gres(tc);
	log_txt = _read_file(log_path);

	ck_assert_msg(
		rc == SLURM_SUCCESS,
		"%s: gres_g_node_config_load() should succeed, but got: %s. Log:\n%s",
		tc->id, slurm_strerror(rc), log_txt);
	_check_records(tc, dev_prefix);
	_check_deep_copy(tc);
	_check_log(tc, log_txt);

	xfree(log_txt);
}

START_TEST(test_node_config_load)
{
	const gres_case_t *tc = &cases[_i];
	char *case_dir, *dev_prefix = NULL, *log_path = NULL;

	case_dir = _setup_case(tc, &dev_prefix);
	xstrfmtcat(log_path, "%s/%s", case_dir, LOAD_LOG);

	if (tc->fail)
		_check_fail(tc, log_path);
	else
		_check_success(tc, log_path, dev_prefix);

	xfree(log_path);
	xfree(dev_prefix);
	xfree(case_dir);
}

END_TEST

int main(void)
{
	SRunner *sr = NULL;
	Suite *s = NULL;
	TCase *tc_core = NULL;
	int number_failed = 0;
	char *cwd = NULL;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	log_opts.stderr_level = LOG_LEVEL_INFO;
	log_init("gres-node-config-test", log_opts, 0, NULL);

	/*
	 * Create the case directories in the working directory, which is the
	 * pytest tmp_path of the test. Nothing is removed by this test, pytest
	 * takes care of it, so the files of failed cases can be inspected.
	 */
	if (!(cwd = getcwd(NULL, 0))) {
		error("getcwd() failed: %m");
		return EXIT_FAILURE;
	}
	base_dir = xstrdup_printf("%s/gres_node_config-XXXXXX", cwd);
	free(cwd);
	if (!mkdtemp(base_dir)) {
		error("mkdtemp(%s) failed: %m", base_dir);
		return EXIT_FAILURE;
	}

	tc_core = tcase_create("node_config_load");
	tcase_add_loop_test(tc_core, test_node_config_load, 0,
			    ARRAY_SIZE(cases));
	s = suite_create("gres_node_config");
	suite_add_tcase(s, tc_core);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	xfree(base_dir);
	log_fini();

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
