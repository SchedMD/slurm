/*****************************************************************************\
 *  node_features_knl_cray.c - Plugin for managing Cray KNL state information
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <fcntl.h>
#ifdef HAVE_NUMA
#undef NUMA_VERSION1_COMPATIBILITY
#include <numa.h>
#endif
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#endif

#include "slurm/slurm.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/fd.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmd/slurmd/req.h"

/* Maximum poll wait time for child processes, in milliseconds */
#define MAX_POLL_WAIT 500

/* Default and minimum timeout parameters for the capmc command */
#define DEFAULT_CAPMC_RETRIES 4
#define DEFAULT_CAPMC_TIMEOUT 60000	/* 60 seconds */
#define MIN_CAPMC_TIMEOUT 1000		/* 1 second */

/* Intel Knights Landing Configuration Modes */
#define KNL_NUMA_CNT	5
#define KNL_MCDRAM_CNT	4
#define KNL_NUMA_FLAG	0x00ff
#define KNL_ALL2ALL	0x0001
#define KNL_SNC2	0x0002
#define KNL_SNC4	0x0004
#define KNL_HEMI	0x0008
#define KNL_QUAD	0x0010
#define KNL_MCDRAM_FLAG	0xff00
#define KNL_CACHE	0x0100
#define KNL_EQUAL	0x0200
#define KNL_SPLIT	0x0400
#define KNL_FLAT	0x0800

#ifndef MODPROBE_PATH
#define MODPROBE_PATH	"/sbin/modprobe"
#endif
#define ZONE_SORT_PATH	"/sys/kernel/zone_sort_free_pages/nodeid"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurmctld_config_t slurmctld_config __attribute__((weak_import));
bitstr_t *avail_node_bitmap __attribute__((weak_import));
#else
slurmctld_config_t slurmctld_config;
bitstr_t *avail_node_bitmap;
#endif

/*
 * These variables are required by the burst buffer plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "node_features" for Slurm node_features) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will only
 * load a node_features plugin if the plugin_type string has a prefix of
 * "node_features/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "node_features knl_cray plugin";
const char plugin_type[]        = "node_features/knl_cray";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
List active_feature_list __attribute__((weak_import));
#else
List active_feature_list;
#endif

/* Configuration Parameters */
static uint16_t allow_mcdram = KNL_MCDRAM_FLAG;
static uint16_t allow_numa = KNL_NUMA_FLAG;
static uid_t *allowed_uid = NULL;
static int allowed_uid_cnt = 0;
static uint32_t boot_time = (45 * 60);	/* 45 minute estimated boot time */
static char *capmc_path = NULL;
static uint32_t capmc_poll_freq = 45;	/* capmc state polling frequency */
static uint32_t capmc_retries = DEFAULT_CAPMC_RETRIES;
static uint32_t capmc_timeout = 0;	/* capmc command timeout in msec */
static char *cnselect_path = NULL;
static uint32_t cpu_bind[KNL_NUMA_CNT];	/* Derived from numa_cpu_bind */
static bool debug_flag = false;
static uint16_t default_mcdram = KNL_CACHE;
static uint16_t default_numa = KNL_ALL2ALL;
static char *mc_path = NULL;
static uint32_t node_reboot_weight = (INFINITE - 1);
static char *numa_cpu_bind = NULL;
static char *syscfg_path = NULL;
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool reconfig = false;
static uint32_t ume_check_interval = 0;
static pthread_mutex_t ume_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ume_thread = 0;
static uint32_t validate_mode = 0;

static bitstr_t *knl_node_bitmap = NULL;	/* KNL nodes found by capmc */
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *node_list_queue = NULL;
static time_t node_time_queue = (time_t) 0;
static time_t shutdown_time = (time_t) 0;
static pthread_t queue_thread = 0;

/* Percentage of MCDRAM used for cache by type, updated from capmc */
static int mcdram_pct[KNL_MCDRAM_CNT];
static int mcdram_set = 0;
static uint64_t *mcdram_per_node = NULL;

/* NOTE: New knl_cray.conf parameters added below must also be added to the
 * contribs/cray/capmc_suspend.c and contribs/cray/capmc_resume.c files */
static s_p_options_t knl_conf_file_options[] = {
	{"AllowMCDRAM", S_P_STRING},
	{"AllowNUMA", S_P_STRING},
	{"AllowUserBoot", S_P_STRING},
	{"BootTime", S_P_UINT32},
	{"CapmcPath", S_P_STRING},
	{"CapmcPollFreq", S_P_UINT32},
	{"CapmcRetries", S_P_UINT32},
	{"CapmcTimeout", S_P_UINT32},
	{"CnselectPath", S_P_STRING},
	{"DefaultMCDRAM", S_P_STRING},
	{"DefaultNUMA", S_P_STRING},
	{"LogFile", S_P_STRING},
	{"McPath", S_P_STRING},
	{"NumaCpuBind", S_P_STRING},
	{"SyscfgPath", S_P_STRING},
	{"NodeRebootWeight", S_P_UINT32},
	{"UmeCheckInterval", S_P_UINT32},
	{"ValidateMode", S_P_UINT32},
	{NULL}
};

typedef struct mcdram_cap {
	uint32_t nid;
	char *mcdram_cfg;
} mcdram_cap_t;

typedef struct mcdram_cfg {
	uint64_t dram_size;
	uint32_t nid;
	char *mcdram_cfg;
	uint64_t mcdram_size;
	uint16_t mcdram_pct;
} mcdram_cfg_t;

typedef struct mcdram_cfg2 {
	int cache_pct;
	char *mcdram_cfg;
	char *nid_str;
	bitstr_t *node_bitmap;
} mcdram_cfg2_t;

typedef struct numa_cap {
	uint32_t nid;
	char *numa_cfg;
} numa_cap_t;

typedef struct numa_cfg {
	uint32_t nid;
	char *numa_cfg;
} numa_cfg_t;

typedef struct numa_cfg2 {
	char *nid_str;
	bitstr_t *node_bitmap;
	char *numa_cfg;
} numa_cfg2_t;

static void _check_node_disabled(void);
static void _check_node_status(void);
static s_p_hashtbl_t *_config_make_tbl(char *filename);
static void _free_script_argv(char **script_argv);
static mcdram_cap_t *_json_parse_mcdram_cap_array(json_object *jobj, char *key,
						  int *num);
static mcdram_cfg_t *_json_parse_mcdram_cfg_array(json_object *jobj, char *key,
						  int *num);
static void _json_parse_mcdram_cap_object(json_object *jobj, mcdram_cap_t *ent);
static void _json_parse_mcdram_cfg_object(json_object *jobj, mcdram_cfg_t *ent);
static numa_cap_t *_json_parse_numa_cap_array(json_object *jobj, char *key,
					      int *num);
static void _json_parse_numa_cap_object(json_object *jobj, numa_cap_t *ent);
static numa_cfg_t *_json_parse_numa_cfg_array(json_object *jobj, char *key,
					      int *num);
static void _json_parse_numa_cfg_object(json_object *jobj, numa_cfg_t *ent);
static int  _knl_mcdram_bits_cnt(uint16_t mcdram_num);
static uint16_t _knl_mcdram_parse(char *mcdram_str, char *sep);
static char *_knl_mcdram_str(uint16_t mcdram_num);
static uint16_t _knl_mcdram_token(char *token);
static int _knl_numa_bits_cnt(uint16_t numa_num);
static uint16_t _knl_numa_parse(char *numa_str, char *sep);
static char *_knl_numa_str(uint16_t numa_num);
static int _knl_numa_inx(char *token);
static uint16_t _knl_numa_token(char *token);
static mcdram_cfg2_t *_load_current_mcdram(int *num);
static numa_cfg2_t *_load_current_numa(int *num);
static char *_load_mcdram_type(int cache_pct);
static char *_load_numa_type(char *type);
static void _log_script_argv(char **script_argv, char *resp_msg);
static void _mcdram_cap_free(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt);
static void _mcdram_cap_log(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt);
static void _mcdram_cfg_free(mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt);
static void _mcdram_cfg2_free(mcdram_cfg2_t *mcdram_cfg2, int mcdram_cfg2_cnt);
static void _mcdram_cfg_log(mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt);
static void _merge_strings(char **node_features, char *node_cfg,
			   uint16_t allow_types);
static void _numa_cap_free(numa_cap_t *numa_cap, int numa_cap_cnt);
static void _numa_cap_log(numa_cap_t *numa_cap, int numa_cap_cnt);
static void _numa_cfg_free(numa_cfg_t *numa_cfg, int numa_cfg_cnt);
static void _numa_cfg2_free(numa_cfg2_t *numa_cfg, int numa_cfg2_cnt);
static void _numa_cfg_log(numa_cfg_t *numa_cfg, int numa_cfg_cnt);
static void _numa_cfg2_log(numa_cfg2_t *numa_cfg, int numa_cfg2_cnt);
static uint64_t _parse_size(char *size_str);
extern void *_queue_agent(void *args);
static int  _queue_node_update(char *node_list);
static char *_run_script(char *cmd_path, char **script_argv, int *status);
static void _strip_knl_opts(char **features);
static int  _tot_wait (struct timeval *start_time);
static void *_ume_agent(void *args);
static void _update_all_node_features(
				mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt,
				mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt,
				numa_cap_t *numa_cap, int numa_cap_cnt,
				numa_cfg_t *numa_cfg, int numa_cfg_cnt);
static void _update_cpu_bind(void);
static void _update_mcdram_pct(char *tok, int mcdram_num);
static void _update_node_features(struct node_record *node_ptr,
				  mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt,
				  mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt,
				  numa_cap_t *numa_cap, int numa_cap_cnt,
				  numa_cfg_t *numa_cfg, int numa_cfg_cnt);
static int _update_node_state(char *node_list, bool set_locks);
static void _validate_node_features(struct node_record *node_ptr);

/* Function used both internally and externally */
extern int node_features_p_node_update(char *active_features,
				       bitstr_t *node_bitmap);

static s_p_hashtbl_t *_config_make_tbl(char *filename)
{
	s_p_hashtbl_t *tbl = NULL;

	xassert(filename);

	if (!(tbl = s_p_hashtbl_create(knl_conf_file_options))) {
		error("knl.conf: %s: s_p_hashtbl_create error: %m", __func__);
		return tbl;
	}

	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		error("knl.conf: %s: s_p_parse_file error: %m", __func__);
		s_p_hashtbl_destroy(tbl);
		tbl = NULL;
	}

	return tbl;
}

/*
 * Return the count of MCDRAM bits set
 */
static int _knl_mcdram_bits_cnt(uint16_t mcdram_num)
{
	int cnt = 0, i;
	uint16_t tmp = 1;

	for (i = 0; i < 16; i++) {
		if ((mcdram_num & KNL_MCDRAM_FLAG) & tmp)
			cnt++;
		tmp = tmp << 1;
	}
	return cnt;
}

/*
 * Translate KNL MCDRAM string to equivalent numeric value
 * mcdram_str IN - String to scan
 * sep IN - token separator to search for
 * RET MCDRAM numeric value
 */
static uint16_t _knl_mcdram_parse(char *mcdram_str, char *sep)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t mcdram_num = 0;

	if (!mcdram_str)
		return mcdram_num;

	tmp = xstrdup(mcdram_str);
	tok = strtok_r(tmp, sep, &save_ptr);
	while (tok) {
		mcdram_num |= _knl_mcdram_token(tok);
		tok = strtok_r(NULL, sep, &save_ptr);
	}
	xfree(tmp);

	return mcdram_num;
}

/*
 * Translate KNL MCDRAM number to equivalent string value
 * Caller must free return value
 */
static char *_knl_mcdram_str(uint16_t mcdram_num)
{
	char *mcdram_str = NULL, *sep = "";

	if (mcdram_num & KNL_CACHE) {
		xstrfmtcat(mcdram_str, "%scache", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_SPLIT) {
		xstrfmtcat(mcdram_str, "%ssplit", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_FLAT) {
		xstrfmtcat(mcdram_str, "%sflat", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_EQUAL) {
		xstrfmtcat(mcdram_str, "%sequal", sep);
//		sep = ",";	/* Remove to avoid CLANG error */
	}

	return mcdram_str;
}

/*
 * Given a KNL MCDRAM token, return its equivalent numeric value
 * token IN - String to scan
 * RET MCDRAM numeric value
 */
static uint16_t _knl_mcdram_token(char *token)
{
	uint16_t mcdram_num = 0;

	if (!xstrcasecmp(token, "cache"))
		mcdram_num = KNL_CACHE;
	else if (!xstrcasecmp(token, "split"))
		mcdram_num = KNL_SPLIT;
	else if (!xstrcasecmp(token, "flat"))
		mcdram_num = KNL_FLAT;
	else if (!xstrcasecmp(token, "equal"))
		mcdram_num = KNL_EQUAL;

	return mcdram_num;
}

/*
 * Return the count of NUMA bits set
 */
static int _knl_numa_bits_cnt(uint16_t numa_num)
{
	int cnt = 0, i;
	uint16_t tmp = 1;

	for (i = 0; i < 16; i++) {
		if ((numa_num & KNL_NUMA_FLAG) & tmp)
			cnt++;
		tmp = tmp << 1;
	}
	return cnt;
}

/*
 * Translate KNL NUMA string to equivalent numeric value
 * numa_str IN - String to scan
 * sep IN - token separator to search for
 * RET NUMA numeric value
 */
static uint16_t _knl_numa_parse(char *numa_str, char *sep)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t numa_num = 0;

	if (!numa_str)
		return numa_num;

	tmp = xstrdup(numa_str);
	tok = strtok_r(tmp, sep, &save_ptr);
	while (tok) {
		numa_num |= _knl_numa_token(tok);
		tok = strtok_r(NULL, sep, &save_ptr);
	}
	xfree(tmp);

	return numa_num;
}

/*
 * Translate KNL NUMA number to equivalent string value
 * Caller must free return value
 */
static char *_knl_numa_str(uint16_t numa_num)
{
	char *numa_str = NULL, *sep = "";

	if (numa_num & KNL_ALL2ALL) {
		xstrfmtcat(numa_str, "%sa2a", sep);
		sep = ",";
	}
	if (numa_num & KNL_SNC2) {
		xstrfmtcat(numa_str, "%ssnc2", sep);
		sep = ",";
	}
	if (numa_num & KNL_SNC4) {
		xstrfmtcat(numa_str, "%ssnc4", sep);
		sep = ",";
	}
	if (numa_num & KNL_HEMI) {
		xstrfmtcat(numa_str, "%shemi", sep);
		sep = ",";
	}
	if (numa_num & KNL_QUAD) {
		xstrfmtcat(numa_str, "%squad", sep);
//		sep = ",";	/* Remove to avoid CLANG error */
	}

	return numa_str;

}

/*
 * Given a KNL NUMA token, return its equivalent numeric value
 * token IN - String to scan
 * RET NUMA numeric value
 */
static uint16_t _knl_numa_token(char *token)
{
	uint16_t numa_num = 0;

	if (!xstrcasecmp(token, "a2a"))
		numa_num |= KNL_ALL2ALL;
	else if (!xstrcasecmp(token, "snc2"))
		numa_num |= KNL_SNC2;
	else if (!xstrcasecmp(token, "snc4"))
		numa_num |= KNL_SNC4;
	else if (!xstrcasecmp(token, "hemi"))
		numa_num |= KNL_HEMI;
	else if (!xstrcasecmp(token, "quad"))
		numa_num |= KNL_QUAD;

	return numa_num;
}

/*
 * Given a KNL NUMA token, return its cpu_bind offset
 * token IN - String to scan
 * RET NUMA offset or -1 if not found
 */
static int _knl_numa_inx(char *token)
{
	uint16_t numa_num;
	int i;

	numa_num = _knl_numa_token(token);
	for (i = 0; i < KNL_NUMA_CNT; i++) {
		if ((0x01 << i) == numa_num)
			return i;
	}
	return -1;
}

/* Remove all KNL feature names from the "features" string */
static void _strip_knl_opts(char **features)
{
	char *save_ptr = NULL, *tok;
	char *tmp_str, *result_str = NULL, *sep = "";

	if (*features == NULL)
		return;

	tmp_str = xstrdup(*features);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		if (!_knl_mcdram_token(tok) && !_knl_numa_token(tok)) {
			xstrfmtcat(result_str, "%s%s", sep, tok);
			sep = ",";
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
	xfree(*features);
	*features = result_str;
}

/*
 * Return time in msec since "start time"
 */
static int _tot_wait (struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/* Free an array of xmalloced records. The array must be NULL terminated. */
static void _free_script_argv(char **script_argv)
{
	int i;

	for (i = 0; script_argv[i]; i++)
		xfree(script_argv[i]);
	xfree(script_argv);
}

/*
 * Update cpu_bind array from current numa_cpu_bind configuration parameter
 */
static void _update_cpu_bind(void)
{
	char *save_ptr = NULL, *sep, *tok, *tmp;
	int rc = SLURM_SUCCESS;
	int i, numa_inx, numa_def;
	uint32_t cpu_bind_val = 0;

	for (i = 0; i < KNL_NUMA_CNT; i++)
		cpu_bind[0] = 0;

	if (!numa_cpu_bind)
		return;

	tmp = xstrdup(numa_cpu_bind);
	tok = strtok_r(tmp, ";", &save_ptr);
	while (tok) {
		sep = strchr(tok, '=');
		if (!sep) {
			rc = SLURM_ERROR;
			break;
		}
		sep[0] = '\0';
		numa_def = _knl_numa_token(tok);
		if (numa_def == 0) {
			rc = SLURM_ERROR;
			break;
		}
		if (xlate_cpu_bind_str(sep + 1, &cpu_bind_val) !=
		    SLURM_SUCCESS) {
			rc = SLURM_ERROR;
			break;
		}
		numa_inx = -1;
		for (i = 0; i < KNL_NUMA_CNT; i++) {
			if ((0x1 << i) == numa_def) {
				numa_inx = i;
				break;
			}
		}
		if (numa_inx > -1)
			cpu_bind[numa_inx] = cpu_bind_val;
		tok = strtok_r(NULL, ";", &save_ptr);
	}
	xfree(tmp);

	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid NumaCpuBind (%s), ignored",
		      plugin_type, numa_cpu_bind);
	}

	if (debug_flag) {
		for (i = 0; i < KNL_NUMA_CNT; i++) {
			char cpu_bind_str[128], *numa_str;
			if (cpu_bind[i] == 0)
				continue;
			numa_str = _knl_numa_str(0x1 << i);
			slurm_sprint_cpu_bind_type(cpu_bind_str, cpu_bind[i]);
			info("CpuBind[%s] = %s", numa_str, cpu_bind_str);
			xfree(numa_str);
		}
	}
}

/*
 * Update our mcdram_pct array with new data.
 * tok IN - percentage of MCDRAM to be used as cache (string form)
 * mcdram_num - MCDRAM value (bit from KNL_FLAT, etc.)
 */
static void _update_mcdram_pct(char *tok, int mcdram_num)
{
	int inx;

	if (mcdram_set == KNL_MCDRAM_CNT)
		return;

	for (inx = 0; inx < KNL_MCDRAM_CNT; inx++) {
		if ((KNL_CACHE << inx) == mcdram_num)
			break;
	}
	if ((inx >= KNL_MCDRAM_CNT) || (mcdram_pct[inx] != -1))
		return;
	mcdram_pct[inx] = strtol(tok, NULL, 10);
	mcdram_set++;
}

static void _json_parse_mcdram_cap_object(json_object *jobj, mcdram_cap_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;
	char *tmp_str, *tok, *save_ptr = NULL, *sep = "";
	int last_mcdram_num = -1;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "nid") == 0) {
				ent->nid = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "mcdram_cfg") == 0) {
				tmp_str = xstrdup(p);
				tok = strtok_r(tmp_str, ",", &save_ptr);
				while (tok) {
					if ((tok[0] >= '0') && (tok[0] <= '9')){
						_update_mcdram_pct(tok,
							last_mcdram_num);
						last_mcdram_num = -1;
					} else {
						last_mcdram_num =
							_knl_mcdram_token(tok);
						xstrfmtcat(ent->mcdram_cfg,
							   "%s%s", sep, tok);
						sep = ",";
					}
					tok = strtok_r(NULL, ",", &save_ptr);
				}
				xfree(tmp_str);
			}
			break;
		default:
			break;
		}
	}
}

static uint64_t _parse_size(char *size_str)
{
	uint64_t size_num = 0;
	char *end_ptr = NULL;

	size_num = (uint64_t) strtol(size_str, &end_ptr, 10);
	if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K'))
		size_num *= 1024;
	else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M'))
		size_num *= (1024 * 1024);
	else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G'))
		size_num *= (1024 * 1024 * 1024);
	else if (end_ptr[0] != '\0')
		info("Invalid MCDRAM size: %s", size_str);

	return size_num;
}

static void _json_parse_mcdram_cfg_object(json_object *jobj, mcdram_cfg_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;

	/* Initialize object */
	ent->dram_size   = NO_VAL;
	ent->mcdram_pct  = NO_VAL16;
	ent->mcdram_size = NO_VAL;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "nid") == 0) {
				ent->nid = x;
			} else if (xstrcmp(iter.key, "mcdram_pct") == 0) {
				ent->mcdram_pct = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "dram_size") == 0) {
				ent->dram_size = _parse_size((char *) p);
			} else if (xstrcmp(iter.key, "mcdram_cfg") == 0) {
				ent->mcdram_cfg = xstrdup(p);
			} else if (xstrcmp(iter.key, "mcdram_pct") == 0) {
				ent->mcdram_pct = _parse_size((char *) p);
			} else if (xstrcmp(iter.key, "mcdram_size") == 0) {
				ent->mcdram_size = _parse_size((char *) p);
			}
			break;
		default:
			break;
		}
	}
}

static void _json_parse_numa_cap_object(json_object *jobj, numa_cap_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "nid") == 0) {
				ent->nid = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "numa_cfg") == 0) {
				ent->numa_cfg = xstrdup(p);
			}
			break;
		default:
			break;
		}
	}
}

static void _json_parse_numa_cfg_object(json_object *jobj, numa_cfg_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "nid") == 0) {
				ent->nid = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "numa_cfg") == 0) {
				ent->numa_cfg = xstrdup(p);
			}
			break;
		default:
			break;
		}
	}
}

static mcdram_cap_t *_json_parse_mcdram_cap_array(json_object *jobj, char *key,
						  int *num)
{
	json_object *jarray;
	json_object *jvalue;
	mcdram_cap_t *ents;
	int i;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(mcdram_cap_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_mcdram_cap_object(jvalue, &ents[i]);
	}

	return ents;
}

/* Return NID string for all nodes with specified MCDRAM mode (HBM percentage).
 * NOTE: Information not returned for nodes which are not up
 * NOTE: xfree() the return value. */
static char *_load_mcdram_type(int cache_pct)
{
	char **script_argv, *resp_msg;
	int i, status = 0;
	DEF_TIMERS;

	if (cache_pct < 0)	/* Unsupported configuration on this system */
		return NULL;
	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("cnselect");
	script_argv[1] = xstrdup("-e");
	xstrfmtcat(script_argv[2], "hbmcachepct.eq.%d", cache_pct);
	START_TIMER;
	resp_msg = _run_script(cnselect_path, script_argv, &status);
	END_TIMER;
	if (debug_flag) {
		info("%s: %s %s %s ran for %s", __func__,
		     script_argv[0], script_argv[1], script_argv[2], TIME_STR);
	}
	if (resp_msg == NULL) {
		debug("%s: %s %s %s returned no information",
		      __func__, script_argv[0], script_argv[1], script_argv[2]);
	} else {
		i = strlen(resp_msg);
		if (resp_msg[i-1] == '\n')
			resp_msg[i-1] = '\0';
	}
	_log_script_argv(script_argv, resp_msg);
	_free_script_argv(script_argv);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: %s %s %s status:%u response:%s", __func__,
		      script_argv[0], script_argv[1], script_argv[2],
		      status, resp_msg);
	}
	return resp_msg;
}

/* Return table of MCDRAM modes and NID string identifying nodes with that mode.
 * Use _mcdram_cfg2_free() to release returned data structure */
static mcdram_cfg2_t *_load_current_mcdram(int *num)
{
	mcdram_cfg2_t *mcdram_cfg;
	int i;

	mcdram_cfg = xmalloc(sizeof(mcdram_cfg2_t) * 4);

	for (i = 0; i < 4; i++) {
		mcdram_cfg[i].cache_pct = mcdram_pct[i];
		mcdram_cfg[i].mcdram_cfg = _knl_mcdram_str(KNL_CACHE << i);
		mcdram_cfg[i].nid_str = _load_mcdram_type(mcdram_cfg[i].cache_pct);
		if (mcdram_cfg[i].nid_str && mcdram_cfg[i].nid_str[0]) {
			mcdram_cfg[i].node_bitmap = bit_alloc(100000);
			(void) bit_unfmt(mcdram_cfg[i].node_bitmap,
					 mcdram_cfg[i].nid_str);
		}
	}
	*num = 4;
	return mcdram_cfg;
}

/* Return NID string for all nodes with specified NUMA mode.
 * NOTE: Information not returned for nodes which are not up
 * NOTE: xfree() the return value. */
static char *_load_numa_type(char *type)
{
	char **script_argv, *resp_msg;
	int i, status = 0;
	DEF_TIMERS;

	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("cnselect");
	script_argv[1] = xstrdup("-e");
	xstrfmtcat(script_argv[2], "numa_cfg.eq.%s", type);
	START_TIMER;
	resp_msg = _run_script(cnselect_path, script_argv, &status);
	END_TIMER;
	if (debug_flag) {
		info("%s: %s %s %s ran for %s", __func__,
		     script_argv[0], script_argv[1], script_argv[2], TIME_STR);
	}
	if (resp_msg == NULL) {
		debug("%s: %s %s %s returned no information",
		      __func__, script_argv[0], script_argv[1], script_argv[2]);
	} else {
		i = strlen(resp_msg);
		if (resp_msg[i-1] == '\n')
			resp_msg[i-1] = '\0';
	}
	_log_script_argv(script_argv, resp_msg);
	_free_script_argv(script_argv);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: %s %s %s status:%u response:%s", __func__,
		      script_argv[0], script_argv[1], script_argv[2],
		      status, resp_msg);
	}
	return resp_msg;
}

/* Return table of NUMA modes and NID string identifying nodes with that mode.
 * Use _numa_cfg2_free() to release returned data structure */
static numa_cfg2_t *_load_current_numa(int *num)
{
	numa_cfg2_t *numa_cfg2;
	int i;

	numa_cfg2 = xmalloc(sizeof(numa_cfg2_t) * 5);
	numa_cfg2[0].numa_cfg = xstrdup("a2a");
	numa_cfg2[1].numa_cfg = xstrdup("snc2");
	numa_cfg2[2].numa_cfg = xstrdup("snc4");
	numa_cfg2[3].numa_cfg = xstrdup("hemi");
	numa_cfg2[4].numa_cfg = xstrdup("quad");

	for (i = 0; i < 5; i++) {
		numa_cfg2[i].nid_str = _load_numa_type(numa_cfg2[i].numa_cfg);
		if (numa_cfg2[i].nid_str && numa_cfg2[i].nid_str[0]) {
			numa_cfg2[i].node_bitmap = bit_alloc(100000);
			(void) bit_unfmt(numa_cfg2[i].node_bitmap,
					 numa_cfg2[i].nid_str);
		}
	}
	*num = 5;
	return numa_cfg2;
}

static mcdram_cfg_t *_json_parse_mcdram_cfg_array(json_object *jobj, char *key,
						  int *num)
{
	json_object *jarray;
	json_object *jvalue;
	mcdram_cfg_t *ents;
	int i;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(mcdram_cfg_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_mcdram_cfg_object(jvalue, &ents[i]);
	}

	return ents;
}

static numa_cap_t *_json_parse_numa_cap_array(json_object *jobj, char *key,
					      int *num)
{
	json_object *jarray;
	json_object *jvalue;
	numa_cap_t *ents;
	int i;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(numa_cap_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_numa_cap_object(jvalue, &ents[i]);
	}

	return ents;
}

static numa_cfg_t *_json_parse_numa_cfg_array(json_object *jobj, char *key,
					      int *num)
{
	json_object *jarray;
	json_object *jvalue;
	numa_cfg_t *ents;
	int i;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(numa_cfg_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_numa_cfg_object(jvalue, &ents[i]);
	}

	return ents;
}

/* Log a command's arguments. */
static void _log_script_argv(char **script_argv, char *resp_msg)
{
	char *cmd_line = NULL;
	int i;

	if (!debug_flag)
		return;

	for (i = 0; script_argv[i]; i++) {
		if (i)
			xstrcat(cmd_line, " ");
		xstrcat(cmd_line, script_argv[i]);
	}
	info("%s", cmd_line);
	if (resp_msg && resp_msg[0])
		info("%s", resp_msg);
	xfree(cmd_line);
}

static void _mcdram_cap_free(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt)
{
	int i;

	if (!mcdram_cap)
		return;
	for (i = 0; i < mcdram_cap_cnt; i++) {
		xfree(mcdram_cap[i].mcdram_cfg);
	}
	xfree(mcdram_cap);
}

static void _mcdram_cap_log(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt)
{
	int i;

	if (!mcdram_cap)
		return;
	for (i = 0; i < mcdram_cap_cnt; i++) {
		info("MCDRAM_CAP[%d]: nid:%u mcdram_cfg:%s",
		     i, mcdram_cap[i].nid, mcdram_cap[i].mcdram_cfg);
	}
}

static void _mcdram_cfg_free(mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt)
{
	int i;

	if (!mcdram_cfg)
		return;
	for (i = 0; i < mcdram_cfg_cnt; i++) {
		xfree(mcdram_cfg[i].mcdram_cfg);
	}
	xfree(mcdram_cfg);
}

static void _mcdram_cfg2_free(mcdram_cfg2_t *mcdram_cfg2, int mcdram_cfg2_cnt)
{
	int i;

	if (!mcdram_cfg2)
		return;
	for (i = 0; i < mcdram_cfg2_cnt; i++) {
		xfree(mcdram_cfg2[i].mcdram_cfg);
		FREE_NULL_BITMAP(mcdram_cfg2[i].node_bitmap);
		xfree(mcdram_cfg2[i].nid_str);
	}
	xfree(mcdram_cfg2);
}

static void _mcdram_cfg_log(mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt)
{
	int i;

	if (!mcdram_cfg)
		return;
	for (i = 0; i < mcdram_cfg_cnt; i++) {
		info("MCDRAM_CFG[%d]: nid:%u dram_size:%"PRIu64" mcdram_cfg:%s mcdram_pct:%u mcdram_size:%"PRIu64,
		     i, mcdram_cfg[i].nid, mcdram_cfg[i].dram_size,
		     mcdram_cfg[i].mcdram_cfg, mcdram_cfg[i].mcdram_pct,
		     mcdram_cfg[i].mcdram_size);
	}
}

static void _mcdram_cfg2_log(mcdram_cfg2_t *mcdram_cfg2, int mcdram_cfg2_cnt)
{
	int i;

	if (!mcdram_cfg2)
		return;
	for (i = 0; i < mcdram_cfg2_cnt; i++) {
		info("MCDRAM_CFG[%d]: nid_str:%s mcdram_cfg:%s cache_pct:%d",
		     i, mcdram_cfg2[i].nid_str, mcdram_cfg2[i].mcdram_cfg,
		     mcdram_cfg2[i].cache_pct);
	}
}

static void _numa_cap_free(numa_cap_t *numa_cap, int numa_cap_cnt)
{
	int i;

	if (!numa_cap)
		return;
	for (i = 0; i < numa_cap_cnt; i++) {
		xfree(numa_cap[i].numa_cfg);
	}
	xfree(numa_cap);
}

static void _numa_cap_log(numa_cap_t *numa_cap, int numa_cap_cnt)
{
	int i;

	if (!numa_cap)
		return;
	for (i = 0; i < numa_cap_cnt; i++) {
		info("NUMA_CAP[%d]: nid:%u numa_cfg:%s",
		     i, numa_cap[i].nid, numa_cap[i].numa_cfg);
	}
}

static void _numa_cfg_free(numa_cfg_t *numa_cfg, int numa_cfg_cnt)
{
	int i;

	if (!numa_cfg)
		return;
	for (i = 0; i < numa_cfg_cnt; i++) {
		xfree(numa_cfg[i].numa_cfg);
	}
	xfree(numa_cfg);
}

static void _numa_cfg2_free(numa_cfg2_t *numa_cfg2, int numa_cfg2_cnt)
{
	int i;

	if (!numa_cfg2)
		return;
	for (i = 0; i < numa_cfg2_cnt; i++) {
		xfree(numa_cfg2[i].nid_str);
		xfree(numa_cfg2[i].numa_cfg);
		FREE_NULL_BITMAP(numa_cfg2[i].node_bitmap);
	}
	xfree(numa_cfg2);
}

static void _numa_cfg_log(numa_cfg_t *numa_cfg, int numa_cfg_cnt)
{
	int i;

	if (!numa_cfg)
		return;
	for (i = 0; i < numa_cfg_cnt; i++) {
		info("NUMA_CFG[%d]: nid:%u numa_cfg:%s",
		     i, numa_cfg[i].nid, numa_cfg[i].numa_cfg);
	}
}

static void _numa_cfg2_log(numa_cfg2_t *numa_cfg2, int numa_cfg2_cnt)
{
	int i;

	if (!numa_cfg2)
		return;
	for (i = 0; i < numa_cfg2_cnt; i++) {
		info("NUMA_CFG[%d]: nid_str:%s numa_cfg:%s",
		     i, numa_cfg2[i].nid_str, numa_cfg2[i].numa_cfg);
	}
}

/* Run a script and return its stdout plus exit status */
static char *_run_script(char *cmd_path, char **script_argv, int *status)
{
	int cc, i, new_wait, resp_size = 0, resp_offset = 0;
	pid_t cpid;
	char *resp = NULL;
	int pfd[2] = { -1, -1 };

	if (access(cmd_path, R_OK | X_OK) < 0) {
		error("%s: %s can not be executed: %m", __func__, cmd_path);
		*status = 127;
		resp = xstrdup("Slurm node_features/knl_cray configuration error");
		return resp;
	}
	if (pipe(pfd) != 0) {
		error("%s: pipe(): %m", __func__);
		*status = 127;
		resp = xstrdup("System error");
		return resp;
	}

	if ((cpid = fork()) == 0) {
		cc = sysconf(_SC_OPEN_MAX);
		dup2(pfd[1], STDERR_FILENO);
		dup2(pfd[1], STDOUT_FILENO);
		for (i = 0; i < cc; i++) {
			if ((i != STDERR_FILENO) && (i != STDOUT_FILENO))
				close(i);
		}
		setpgid(0, 0);
		execv(cmd_path, script_argv);
		error("%s: execv(%s): %m", __func__, cmd_path);
		exit(127);
	} else if (cpid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		error("%s: fork(): %m", __func__);
	} else {
		struct pollfd fds;
		struct timeval tstart;
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(pfd[1]);
		gettimeofday(&tstart, NULL);
		while (1) {
			if (slurmctld_config.shutdown_time) {
				error("%s: killing %s operation on shutdown",
				      __func__, script_argv[1]);
				break;
			}
			fds.fd = pfd[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			new_wait = capmc_timeout - _tot_wait(&tstart);
			if (new_wait <= 0) {
				error("%s: %s poll timeout @ %d msec",
				      __func__, script_argv[1], capmc_timeout);
				break;
			}
			new_wait = MIN(new_wait, MAX_POLL_WAIT);
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				continue;
			} else if (i < 0) {
				error("%s: %s poll:%m", __func__,
				      script_argv[1]);
				break;
			}
			if ((fds.revents & POLLIN) == 0)
				break;
			i = read(pfd[0], resp + resp_offset,
				 resp_size - resp_offset);
			if (i == 0) {
				break;
			} else if (i < 0) {
				if (errno == EAGAIN)
					continue;
				error("%s: read(%s): %m", __func__, cmd_path);
				break;
			} else {
				resp_offset += i;
				if (resp_offset + 1024 >= resp_size) {
					resp_size *= 2;
					resp = xrealloc(resp, resp_size);
				}
			}
		}
		killpg(cpid, SIGTERM);
		usleep(10000);
		killpg(cpid, SIGKILL);
		waitpid(cpid, status, 0);
		close(pfd[0]);
	}
	return resp;
}

static void _merge_strings(char **node_features, char *node_cfg,
			   uint16_t allow_types)
{
	char *tmp_str1, *tok1, *save_ptr1 = NULL;
	char *tmp_str2, *tok2, *save_ptr2 = NULL;
	bool mcdram_filter = false, numa_filter = false;

	if ((node_cfg == NULL) || (node_cfg[0] == '\0'))
		return;
	if (*node_features == NULL) {
		*node_features = xstrdup(node_cfg);
		return;
	}

	if ((allow_types &  KNL_MCDRAM_FLAG) &&
	    (allow_types != KNL_MCDRAM_FLAG))
		mcdram_filter = true;
	if ((allow_types &  KNL_NUMA_FLAG) &&
	    (allow_types != KNL_NUMA_FLAG))
		numa_filter = true;

	/* Merge strings and avoid duplicates */
	tmp_str1 = xstrdup(node_cfg);
	tok1 = strtok_r(tmp_str1, ",", &save_ptr1);
	while (tok1) {
		bool match = false;
		if (mcdram_filter &&
		    ((_knl_mcdram_token(tok1) & allow_types) == 0))
			goto next_tok;
		if (numa_filter &&
		    ((_knl_numa_token(tok1) & allow_types) == 0))
			goto next_tok;
		tmp_str2 = xstrdup(*node_features);
		tok2 = strtok_r(tmp_str2, ",", &save_ptr2);
		while (tok2) {
			if (!xstrcmp(tok1, tok2)) {
				match = true;
				break;
			}
			tok2 = strtok_r(NULL, ",", &save_ptr2);
		}
		xfree(tmp_str2);
		if (!match)
			xstrfmtcat(*node_features, ",%s", tok1);
next_tok:	tok1 = strtok_r(NULL, ",", &save_ptr1);
	}
	xfree(tmp_str1);
}

static void _make_node_down(struct node_record *node_ptr)
{
	if (!avail_node_bitmap) {
		/*
		 * In process of initial slurmctld startup,
		 * node data structures not completely built yet
		 */
		node_ptr->node_state |= NODE_STATE_DRAIN;
		node_ptr->reason = xstrdup("Invalid KNL modes");
		node_ptr->reason_time = time(NULL);
		node_ptr->reason_uid = getuid();
	} else {
		(void) drain_nodes(node_ptr->name, "Invalid KNL modes",
				   getuid());
	}
}

/*
 * Determine that the actual KNL mode matches the available and current node
 * features, otherwise DRAIN the node
 */
static void _validate_node_features(struct node_record *node_ptr)
{
	char *tmp_str, *tok, *save_ptr = NULL;
	uint16_t actual_mcdram = 0, actual_numa = 0;
	uint16_t config_mcdram = 0, config_numa = 0;
	uint16_t count_mcdram = 0,  count_numa = 0;
	uint16_t tmp_mcdram, tmp_numa;

	if (!node_ptr->features || IS_NODE_DOWN(node_ptr))
		return;

	tmp_str = xstrdup(node_ptr->features);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		if ((tmp_mcdram = _knl_mcdram_token(tok))) {
			config_mcdram |= tmp_mcdram;
			count_mcdram++;
		} else if ((tmp_numa = _knl_numa_token(tok))) {
			config_numa |= tmp_numa;
			count_numa++;
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);

	tmp_str = xstrdup(node_ptr->features_act);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		if ((tmp_mcdram = _knl_mcdram_token(tok)))
			actual_mcdram |= tmp_mcdram;
		else if ((tmp_numa = _knl_numa_token(tok)))
			actual_numa |= tmp_numa;
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);

	if ((config_mcdram != actual_mcdram) || (count_mcdram != 1) ||
	    (config_numa   != actual_numa)   || (count_numa != 1)) {
		_make_node_down(node_ptr);
		error("Invalid KNL modes on node %s", node_ptr->name);
	}
}

/*
 * Remove all KNL MCDRAM and NUMA type GRES from this node (it isn't KNL),
 * returns count of KNL features found.
 */
static int _strip_knl_features(char **node_feature)
{
	char *tmp_str1, *tok1, *save_ptr1 = NULL;
	char *tmp_str2 = NULL, *sep = "";
	int cnt = 0;

	xassert(node_feature);
	if (*node_feature == NULL)
		return cnt;
	tmp_str1 = xstrdup(*node_feature);
	tok1 = strtok_r(tmp_str1, ",", &save_ptr1);
	while (tok1) {
		if (_knl_mcdram_token(tok1) || _knl_numa_token(tok1)) {
			cnt++;
		} else {
			xstrfmtcat(tmp_str2, "%s%s", sep, tok1);
			sep = ",";
		}
		tok1 = strtok_r(NULL, ",", &save_ptr1);
	}
	if (cnt) {	/* Update the nodes features */
		xfree(*node_feature);
		*node_feature = tmp_str2;
	} else {	/* Discard new feature list */
		xfree(tmp_str2);
	}
	xfree(tmp_str1);
	return cnt;
}

/* Update features and features_act fields for ALL nodes based upon
 * its current configuration provided by capmc */
static void _update_all_node_features(
				mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt,
				mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt,
				numa_cap_t *numa_cap, int numa_cap_cnt,
				numa_cfg_t *numa_cfg, int numa_cfg_cnt)
{
	struct node_record *node_ptr;
	char node_name[32], *prefix;
	int i, node_inx, numa_inx, width = 5;
	uint64_t mcdram_size;

	if ((node_record_table_ptr == NULL) ||
	    (node_record_table_ptr->name == NULL)) {
		prefix = xstrdup("nid");
	} else {
		prefix = xstrdup(node_record_table_ptr->name);
		for (i = 0; prefix[i]; i++) {
			if ((prefix[i] >= '0') && (prefix[i] <= '9')) {
				prefix[i] = '\0';
				width = 1;
				for (i++ ; prefix[i]; i++)
					width++;
				break;
			}
		}
	}
	if (mcdram_cap) {
		if (!knl_node_bitmap)
			knl_node_bitmap = bit_alloc(node_record_count);
		for (i = 0; i < mcdram_cap_cnt; i++) {
			snprintf(node_name, sizeof(node_name),
				 "%s%.*d", prefix, width, mcdram_cap[i].nid);
			node_ptr = find_node_record(node_name);
			if (node_ptr) {
				node_inx = node_ptr - node_record_table_ptr;
				bit_set(knl_node_bitmap, node_inx);
				if (validate_mode == 0) {
					_merge_strings(&node_ptr->features,
						       mcdram_cap[i].mcdram_cfg,
						       allow_mcdram);
				}
			}
		}
	}
	if (mcdram_cfg) {
		for (i = 0; i < mcdram_cfg_cnt; i++) {
			snprintf(node_name, sizeof(node_name),
				 "%s%.*d", prefix, width, mcdram_cfg[i].nid);
			if (!(node_ptr = find_node_record(node_name)))
				continue;
			mcdram_per_node[node_ptr - node_record_table_ptr] =
				mcdram_cfg[i].mcdram_size;
			_merge_strings(&node_ptr->features_act,
				       mcdram_cfg[i].mcdram_cfg,
				       allow_mcdram);
			mcdram_size = mcdram_cfg[i].mcdram_size *
				      (100 - mcdram_cfg[i].mcdram_pct) / 100;
			if (!node_ptr->gres) {
				node_ptr->gres =
					xstrdup(node_ptr->config_ptr->gres);
			}
			gres_plugin_node_feature(node_ptr->name, "hbm",
						 mcdram_size, &node_ptr->gres,
						 &node_ptr->gres_list);
		}
	}
	if (numa_cap && (validate_mode == 0)) {
		for (i = 0; i < numa_cap_cnt; i++) {
			snprintf(node_name, sizeof(node_name),
				 "%s%.*d", prefix, width, numa_cap[i].nid);
			node_ptr = find_node_record(node_name);
			if (node_ptr) {
				_merge_strings(&node_ptr->features,
					       numa_cap[i].numa_cfg,
					       allow_numa);
			}
		}
	}
	if (numa_cfg) {
		for (i = 0; i < numa_cfg_cnt; i++) {
			snprintf(node_name, sizeof(node_name),
				 "%s%.*u", prefix, width, numa_cfg[i].nid);
			node_ptr = find_node_record(node_name);
			if (node_ptr) {
				_merge_strings(&node_ptr->features_act,
					       numa_cfg[i].numa_cfg,
					       allow_numa);
				numa_inx = _knl_numa_inx(numa_cfg[i].numa_cfg);
				if ((numa_inx >= 0) && cpu_bind[numa_inx])
					node_ptr->cpu_bind = cpu_bind[numa_inx];
			}
		}
	}

	/*
	 * Make sure that only nodes reported by "capmc get_mcdram_capabilities"
	 * contain KNL features
	 */
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (knl_node_bitmap && bit_test(knl_node_bitmap, i)) {
			if (validate_mode)
				_validate_node_features(node_ptr);
			continue;
		}
		node_inx = _strip_knl_features(&node_ptr->features) +
			   _strip_knl_features(&node_ptr->features_act);
		if (node_inx) {
			error("Removed KNL features from non-KNL node %s",
			      node_ptr->name);
		}
		if (!node_ptr->gres)
			node_ptr->gres = xstrdup(node_ptr->config_ptr->gres);
		gres_plugin_node_feature(node_ptr->name, "hbm", 0,
					 &node_ptr->gres, &node_ptr->gres_list);
	}

	xfree(prefix);
}

/*
 * Update a specific node's features and features_act fields based upon
 * its current configuration provided by capmc
 */
static void _update_node_features(struct node_record *node_ptr,
				  mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt,
				  mcdram_cfg_t *mcdram_cfg, int mcdram_cfg_cnt,
				  numa_cap_t *numa_cap, int numa_cap_cnt,
				  numa_cfg_t *numa_cfg, int numa_cfg_cnt)
{
	int i, nid, node_inx, numa_inx;
	char *end_ptr = "";
	uint64_t mcdram_size;
	bitstr_t *node_bitmap = NULL;
	bool is_knl = false;

	xassert(node_ptr);
	nid = strtol(node_ptr->name + 3, &end_ptr, 10);
	if (end_ptr[0] != '\0') {
		error("%s: Invalid node name (%s)", __func__, node_ptr->name);
		return;
	}

	_strip_knl_opts(&node_ptr->features);
	if (node_ptr->features && !node_ptr->features_act)
		node_ptr->features_act = xstrdup(node_ptr->features);
	_strip_knl_opts(&node_ptr->features_act);

	if (mcdram_cap && (validate_mode == 0)) {
		for (i = 0; i < mcdram_cap_cnt; i++) {
			if (nid == mcdram_cap[i].nid) {
				_merge_strings(&node_ptr->features,
					       mcdram_cap[i].mcdram_cfg,
					       allow_mcdram);
				is_knl = true;
				break;
			}
		}
	}

	if (mcdram_cfg) {
		for (i = 0; i < mcdram_cfg_cnt; i++) {
			if (nid != mcdram_cfg[i].nid)
				continue;
			_merge_strings(&node_ptr->features_act,
				       mcdram_cfg[i].mcdram_cfg, allow_mcdram);

			mcdram_per_node[node_ptr - node_record_table_ptr] =
				mcdram_cfg[i].mcdram_size;
			mcdram_size = mcdram_cfg[i].mcdram_size *
				      (100 - mcdram_cfg[i].mcdram_pct) / 100;
			if (!node_ptr->gres) {
				node_ptr->gres =
					xstrdup(node_ptr->config_ptr->gres);
			}
			if (!node_ptr->gres) {
				node_ptr->gres =
					xstrdup(node_ptr->config_ptr->gres);
			}
			gres_plugin_node_feature(node_ptr->name, "hbm",
						 mcdram_size, &node_ptr->gres,
						 &node_ptr->gres_list);
			break;
		}
	}
	if (numa_cap && (validate_mode == 0)) {
		for (i = 0; i < numa_cap_cnt; i++) {
			if (nid == numa_cap[i].nid) {
				_merge_strings(&node_ptr->features,
					       numa_cap[i].numa_cfg,
					       allow_numa);
				break;
			}
		}
	}
	if (numa_cfg) {
		for (i = 0; i < numa_cfg_cnt; i++) {
			if (nid == numa_cfg[i].nid) {
				_merge_strings(&node_ptr->features_act,
					       numa_cfg[i].numa_cfg,
					       allow_numa);
				numa_inx = _knl_numa_inx(numa_cfg[i].numa_cfg);
				if ((numa_inx >= 0) && cpu_bind[numa_inx])
					node_ptr->cpu_bind = cpu_bind[numa_inx];
				break;
			}
		}
	}

	/* Make sure that only nodes reported by "capmc get_mcdram_capabilities"
	 * contain KNL features */
	if (is_knl) {
		if (validate_mode)
			_validate_node_features(node_ptr);
	} else {
		node_inx = _strip_knl_features(&node_ptr->features) +
			   _strip_knl_features(&node_ptr->features_act);
		if (node_inx) {
			error("Removed KNL features from non-KNL node %s",
			      node_ptr->name);
		}
		if (!node_ptr->gres) {
			node_ptr->gres =
				xstrdup(node_ptr->config_ptr->gres);
		}
		gres_plugin_node_feature(node_ptr->name, "hbm", 0,
					 &node_ptr->gres, &node_ptr->gres_list);
	}

	/* Update bitmaps and lists used by slurmctld for scheduling */
	node_bitmap = bit_alloc(node_record_count);
	bit_set(node_bitmap, (node_ptr - node_record_table_ptr));
	update_feature_list(active_feature_list, node_ptr->features_act,
			    node_bitmap);
	(void) node_features_p_node_update(node_ptr->features_act, node_bitmap);
	FREE_NULL_BITMAP(node_bitmap);
}

static void _make_uid_array(char *uid_str)
{
	char *save_ptr = NULL, *tmp_str, *tok;
	int i, uid_cnt = 0;

	if (!uid_str)
		return;

	/* Count the number of users */
	for (i = 0; uid_str[i]; i++) {
		if (uid_str[i] == ',')
			uid_cnt++;
	}
	uid_cnt++;

	allowed_uid = xmalloc(sizeof(uid_t) * uid_cnt);
	allowed_uid_cnt = 0;
	tmp_str = xstrdup(uid_str);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		if (uid_from_string(tok, &allowed_uid[allowed_uid_cnt++]) < 0)
			error("knl_cray.conf: Invalid AllowUserBoot: %s", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
}

static char *_make_uid_str(uid_t *uid_array, int uid_cnt)
{
	char *sep = "", *tmp_str = NULL, *uid_str = NULL;
	int i;

	if (allowed_uid_cnt == 0) {
		uid_str = xstrdup("ALL");
		return uid_str;
	}

	for (i = 0; i < uid_cnt; i++) {
		tmp_str = uid_to_string(uid_array[i]);
		xstrfmtcat(uid_str, "%s%s(%d)", sep, tmp_str, uid_array[i]);
		xfree(tmp_str);
		sep = ",";
	}

	return uid_str;
}

/* Watch for Uncorrectable Memory Errors. Notify jobs if any detected */
static void *_ume_agent(void *args)
{
	struct timespec req;
	int i, mc_num, csrow_num, ue_count, last_ue_count = -1;
	int *fd = NULL, fd_cnt = 0, fd_size = 0, ume_path_size;
	char buf[8], *ume_path;
	ssize_t rd_size;

	/* Identify and open array of UME file descriptors */
	ume_path_size = strlen(mc_path) + 32;
	ume_path = xmalloc(ume_path_size);
	for (mc_num = 0; ; mc_num++) {
		for (csrow_num = 0; ; csrow_num++) {
			if (fd_cnt == fd_size) {
				fd_size += 64;
				fd = xrealloc(fd, sizeof(int) * fd_size);
			}
			snprintf(ume_path, ume_path_size,
				 "%s/mc%d/csrow%d/ue_count",
				 mc_path, mc_num, csrow_num);
			if ((fd[fd_cnt] = open(ume_path, 0)) >= 0)
				fd_cnt++;
			else
				break;
		}
		if (csrow_num == 0)
			break;
	}
	xfree(ume_path);

	while (!shutdown_time) {
		/* Get current UME count */
		ue_count = 0;
		for (i = 0; i < fd_cnt; i++) {
			(void) lseek(fd[i], 0, SEEK_SET);
			rd_size = read(fd[i], buf, 7);
			if (rd_size <= 0)
				continue;
			buf[rd_size] = '\0';
			ue_count += atoi(buf);
		}

		if (shutdown_time)
			break;
		/* If UME count changed, notify all steps */
		if ((last_ue_count < ue_count) && (last_ue_count != -1)) {
			i = ume_notify();
			error("UME error detected. Notified %d job steps", i);
		}
		last_ue_count = ue_count;

		if (shutdown_time)
			break;
		/* Sleep before retry */
		req.tv_sec  =  ume_check_interval / 1000000;
		req.tv_nsec = (ume_check_interval % 1000000) * 1000;
		(void) nanosleep(&req, NULL);
	}

	for (i = 0; i < fd_cnt; i++)
		(void) close(fd[i]);
	xfree(fd);

	return NULL;
}

/* Load configuration */
extern int init(void)
{
	char *allow_mcdram_str, *allow_numa_str, *allow_user_str;
	char *default_mcdram_str, *default_numa_str;
	char *knl_conf_file, *tmp_str = NULL;
	s_p_hashtbl_t *tbl;
	struct stat stat_buf;
	int i;

	/* Set default values */
	allow_mcdram = KNL_MCDRAM_FLAG;
	allow_numa = KNL_NUMA_FLAG;
	xfree(allowed_uid);
	allowed_uid_cnt = 0;
	xfree(capmc_path);
	capmc_poll_freq = 45;
	capmc_timeout = DEFAULT_CAPMC_TIMEOUT;
	for (i = 0; i < KNL_NUMA_CNT; i++)
		cpu_bind[i] = 0;
	xfree(cnselect_path);
	debug_flag = false;
	default_mcdram = KNL_CACHE;
	default_numa = KNL_ALL2ALL;
	xfree(mc_path);
	for (i = 0; i < KNL_MCDRAM_CNT; i++)
		mcdram_pct[i] = -1;
	mcdram_set = 0;
	xfree(numa_cpu_bind);
	xfree(syscfg_path);

	if (slurm_get_debug_flags() & DEBUG_FLAG_NODE_FEATURES)
		debug_flag = true;

	knl_conf_file = get_extra_conf_path("knl_cray.conf");
	if ((stat(knl_conf_file, &stat_buf) == 0) &&
	    (tbl = _config_make_tbl(knl_conf_file))) {
		if (s_p_get_string(&tmp_str, "AllowMCDRAM", tbl)) {
			allow_mcdram = _knl_mcdram_parse(tmp_str, ",");
			if (_knl_mcdram_bits_cnt(allow_mcdram) < 1) {
				fatal("knl_cray.conf: Invalid AllowMCDRAM=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AllowNUMA", tbl)) {
			allow_numa = _knl_numa_parse(tmp_str, ",");
			if (_knl_numa_bits_cnt(allow_numa) < 1) {
				fatal("knl_cray.conf: Invalid AllowNUMA=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AllowUserBoot", tbl)) {
			_make_uid_array(tmp_str);
			xfree(tmp_str);
		}
		(void) s_p_get_uint32(&boot_time, "BootTime", tbl);
		(void) s_p_get_string(&capmc_path, "CapmcPath", tbl);
		(void) s_p_get_uint32(&capmc_poll_freq, "CapmcPollFreq", tbl);
		(void) s_p_get_uint32(&capmc_retries, "CapmcRetries", tbl);
		(void) s_p_get_uint32(&capmc_timeout, "CapmcTimeout", tbl);
		(void) s_p_get_string(&cnselect_path, "CnselectPath", tbl);
		if (s_p_get_string(&tmp_str, "DefaultMCDRAM", tbl)) {
			default_mcdram = _knl_mcdram_parse(tmp_str, ",");
			if (_knl_mcdram_bits_cnt(default_mcdram) != 1) {
				fatal("knl_cray.conf: Invalid DefaultMCDRAM=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultNUMA", tbl)) {
			default_numa = _knl_numa_parse(tmp_str, ",");
			if (_knl_numa_bits_cnt(default_numa) != 1) {
				fatal("knl_cray.conf: Invalid DefaultNUMA=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		(void) s_p_get_string(&mc_path, "McPath", tbl);
		(void) s_p_get_uint32(&node_reboot_weight, "NodeRebootWeight",
				      tbl);
		if (s_p_get_string(&numa_cpu_bind, "NumaCpuBind", tbl))
			_update_cpu_bind();
		(void) s_p_get_string(&syscfg_path, "SyscfgPath", tbl);
		(void) s_p_get_uint32(&ume_check_interval, "UmeCheckInterval",
				      tbl);
		(void) s_p_get_uint32(&validate_mode, "ValidateMode", tbl);
		s_p_hashtbl_destroy(tbl);
	} else {
		error("something wrong with opening/reading knl_cray.conf");
	}
	xfree(knl_conf_file);
	if (!capmc_path)
		capmc_path = xstrdup("/opt/cray/capmc/default/bin/capmc");
	capmc_timeout = MAX(capmc_timeout, MIN_CAPMC_TIMEOUT);
	if (!cnselect_path)
		cnselect_path = xstrdup("/opt/cray/sdb/default/bin/cnselect");
	if (!mc_path)
		mc_path = xstrdup("/sys/devices/system/edac/mc");
	if (!syscfg_path)
		verbose("SyscfgPath is not configured");

	if (slurm_get_debug_flags() & DEBUG_FLAG_NODE_FEATURES) {
		allow_mcdram_str = _knl_mcdram_str(allow_mcdram);
		allow_numa_str = _knl_numa_str(allow_numa);
		allow_user_str = _make_uid_str(allowed_uid, allowed_uid_cnt);
		default_mcdram_str = _knl_mcdram_str(default_mcdram);
		default_numa_str = _knl_numa_str(default_numa);
		info("AllowMCDRAM=%s AllowNUMA=%s",
		     allow_mcdram_str, allow_numa_str);
		info("AllowUserBoot=%s", allow_user_str);
		info("BootTIme=%u", boot_time);
		info("CapmcPath=%s", capmc_path);
		info("CapmcPollFreq=%u sec", capmc_poll_freq);
		info("CapmcRetries=%u", capmc_retries);
		info("CapmcTimeout=%u msec", capmc_timeout);
		info("CnselectPath=%s", cnselect_path);
		info("DefaultMCDRAM=%s DefaultNUMA=%s",
		     default_mcdram_str, default_numa_str);
		info("McPath=%s", mc_path);
		info("NodeRebootWeight=%u", node_reboot_weight);
		info("NumaCpuBind=%s", numa_cpu_bind);
		info("SyscfgPath=%s", syscfg_path);
		info("UmeCheckInterval=%u", ume_check_interval);
		info("ValidateMode=%u", validate_mode);
		xfree(allow_mcdram_str);
		xfree(allow_numa_str);
		xfree(allow_user_str);
		xfree(default_mcdram_str);
		xfree(default_numa_str);
	}
	gres_plugin_add("hbm");

	if (ume_check_interval && run_in_daemon("slurmd")) {
		slurm_mutex_lock(&ume_mutex);
		slurm_thread_create(&ume_thread, _ume_agent, NULL);
		slurm_mutex_unlock(&ume_mutex);
	}

	slurm_mutex_lock(&queue_mutex);
	if (queue_thread == 0) {
		/* since we do a join on this later we don't make it detached */
		slurm_thread_create(&queue_thread, _queue_agent, NULL);
	}
	slurm_mutex_unlock(&queue_mutex);

	return SLURM_SUCCESS;
}

/* Release allocated memory */
extern int fini(void)
{
	shutdown_time = time(NULL);
	slurm_mutex_lock(&ume_mutex);
	if (ume_thread) {
		pthread_join(ume_thread, NULL);
		ume_thread = 0;
	}
	slurm_mutex_unlock(&ume_mutex);
	pthread_join(queue_thread, NULL);
	slurm_mutex_lock(&queue_mutex);
	xfree(node_list_queue);	/* just drop requessts */
	shutdown_time = (time_t) 0;
	queue_thread = 0;
	slurm_mutex_unlock(&queue_mutex);

	xfree(allowed_uid);
	allowed_uid_cnt = 0;
	xfree(capmc_path);
	xfree(cnselect_path);
	capmc_timeout = 0;
	debug_flag = false;
	xfree(mc_path);
	xfree(mcdram_per_node);
	xfree(numa_cpu_bind);
	xfree(syscfg_path);
	FREE_NULL_BITMAP(knl_node_bitmap);

	return SLURM_SUCCESS;
}

/* Reload configuration */
extern int node_features_p_reconfig(void)
{
	slurm_mutex_lock(&config_mutex);
	reconfig = true;
	slurm_mutex_unlock(&config_mutex);
	return SLURM_SUCCESS;
}

/* Put any nodes NOT found by "capmc node_status" into DRAIN state */
static void _check_node_status(void)
{
	json_object *j_obj;
	json_object_iter iter;
	json_object *j_array = NULL;
	json_object *j_value;
	char *resp_msg, **script_argv;
	int i, nid, num_ent, retry, status = 0;
	struct node_record *node_ptr;
	bitstr_t *capmc_node_bitmap = NULL;
	DEF_TIMERS;

	script_argv = xmalloc(sizeof(char *) * 4); /* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("node_status");
	for (retry = 0; ; retry++) {
		START_TIMER;
		resp_msg = _run_script(capmc_path, script_argv, &status);
		END_TIMER;
		if (debug_flag)
			info("%s: node_status ran for %s", __func__, TIME_STR);
		_log_script_argv(script_argv, resp_msg);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
			break;	/* Success */
		error("%s: node_status status:%u response:%s",
		      __func__, status, resp_msg);
		if (resp_msg == NULL) {
			info("%s: node_status returned no information",
			     __func__);
			_free_script_argv(script_argv);
			return;
		}
		if (strstr(resp_msg, "Could not lookup") &&
		    (retry <= capmc_retries)) {
			/* State Manager is down. Sleep and retry */
			sleep(1);
			xfree(resp_msg);
		} else {
			xfree(resp_msg);
			_free_script_argv(script_argv);
			return;
		}
	}
	_free_script_argv(script_argv);

	j_obj = json_tokener_parse(resp_msg);
	if (j_obj == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		return;
	}
	xfree(resp_msg);

	capmc_node_bitmap = bit_alloc(100000);
	json_object_object_foreachC(j_obj, iter) {
		/* NOTE: The error number "e" and message "err_msg"
		 * fields are currently ignored. */
		if (!xstrcmp(iter.key, "e") ||
		    !xstrcmp(iter.key, "err_msg"))
			continue;
		if (json_object_get_type(iter.val) != json_type_array)
			continue;
		json_object_object_get_ex(j_obj, iter.key, &j_array);
		if (!j_array) {
			error("%s: Unable to parse nid specification",
			      __func__);
			FREE_NULL_BITMAP(capmc_node_bitmap);
			return;
		}
		num_ent = json_object_array_length(j_array);
		for (i = 0; i < num_ent; i++) {
			j_value = json_object_array_get_idx(j_array, i);
			if (json_object_get_type(j_value) !=
			    json_type_int) {
				error("%s: Unable to parse nid specification",
				      __func__);
			} else {
				nid = json_object_get_int64(j_value);
				if ((nid >= 0) && (nid < 100000))
					bit_set(capmc_node_bitmap, nid);
			}
		}
	}
	json_object_put(j_obj);	/* Frees json memory */

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		nid = atoi(node_ptr->name + 3);	/* Skip "nid" */
		if ((nid < 0) || (nid >= 100000) ||
		    bit_test(capmc_node_bitmap, nid))
			continue;
		info("Node %s not found by \'capmc node_status\', draining it",
		     node_ptr->name);
		if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr))
			continue;
		node_ptr->node_state |= NODE_STATE_DRAIN;
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup("Node not found by capmc");
		node_ptr->reason_time = time(NULL);
		node_ptr->reason_uid = slurm_get_slurm_user_id();
		if (avail_node_bitmap)
			bit_clear(avail_node_bitmap, i);
	}
	FREE_NULL_BITMAP(capmc_node_bitmap);
}

/* Put any disabled nodes into DRAIN state */
static void _check_node_disabled(void)
{
/* FIXME: To be added
 *
 * STEP 0 (for testing), disable/enable nodes:
 * > xtcli disable ${TARGET_NODE}
 * > xtcli enable ${TARGET_NODE}
 *
 * STEP 1: Identify disabled compute nodes
 * > xtshow --compute --disabled
 * L1s ...
 * L0s ...
 * Nodes ...
 * c0-0c0s7n0:    -|  disabled  [noflags|]
 * SeaStars ...
 * Links ...
 * c1-0c2s1s1l1:    -|  disabled  [noflags|]
 *
 * STEP 2: Map cname to nid name
 * > rtr -Im ${TARGET_BLADE}
 *
 * STEP 3: Drain the disabled compute nodes
 * See logic in _check_node_status() above.
 */
}

/* Periodically update node information for specified nodes. We can't do this
 * work in real-time since capmc takes multiple seconds to execute. */
extern void *_queue_agent(void *args)
{
	char *node_list;

	while (shutdown_time == 0) {
		sleep(1);
		if (shutdown_time)
			break;

		if (node_list_queue &&
		    (difftime(time(NULL), node_time_queue) >= 30)) {
			slurm_mutex_lock(&queue_mutex);
			node_list = node_list_queue;
			node_list_queue = NULL;
			node_time_queue = (time_t) 0;
			slurm_mutex_unlock(&queue_mutex);
			(void) _update_node_state(node_list, true);
			xfree(node_list);
		}
	}

	return NULL;
}

/* Queue request to update node information */
static int _queue_node_update(char *node_list)
{
	slurm_mutex_lock(&queue_mutex);
	if (node_time_queue == 0)
		node_time_queue = time(NULL);
	if (node_list_queue)
		xstrcat(node_list_queue, ",");
	xstrcat(node_list_queue, node_list);
	slurm_mutex_unlock(&queue_mutex);

	return SLURM_SUCCESS;
}

/* Update active and available features on specified nodes.
 * If node_list is NULL then update ALL nodes now.
 * If node_list is not NULL, then queue a request to update select nodes later.
 */
extern int node_features_p_get_node(char *node_list)
{
	if (node_list &&		/* Selected node to be update */
	    mcdram_per_node &&		/* and needed global info is */
	    (mcdram_pct[0] != -1))	/* already available */
		return _queue_node_update(node_list);

	return _update_node_state(node_list, false);
}

static int _update_node_state(char *node_list, bool set_locks)
{
	json_object *j;
	json_object_iter iter;
	int i, k, rc = SLURM_SUCCESS, retry, status = 0;
	DEF_TIMERS;
	char *resp_msg, **script_argv;
	mcdram_cap_t *mcdram_cap = NULL;
	mcdram_cfg_t *mcdram_cfg = NULL;
	mcdram_cfg2_t *mcdram_cfg2 = NULL;
	numa_cap_t *numa_cap = NULL;
	numa_cfg_t *numa_cfg = NULL;
	numa_cfg2_t *numa_cfg2 = NULL;
	int mcdram_cap_cnt = 0, mcdram_cfg_cnt = 0, mcdram_cfg2_cnt = 0;
	int numa_cap_cnt = 0, numa_cfg_cnt = 0, numa_cfg2_cnt = 0;
	struct node_record *node_ptr;
	hostlist_t host_list;
	char *node_name;

	slurm_mutex_lock(&config_mutex);
	if (reconfig) {
		(void) init();
		reconfig = false;
	}
	slurm_mutex_unlock(&config_mutex);

	_check_node_status();	/* Drain nodes not found by capmc */
	_check_node_disabled();	/* Drain disabled nodes */

	if (!mcdram_per_node)
		mcdram_per_node = xmalloc(sizeof(uint64_t) * node_record_count);

	/*
	 * Load available MCDRAM capabilities
	 */
	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("get_mcdram_capabilities");
	for (retry = 0; ; retry++) {
		START_TIMER;
		resp_msg = _run_script(capmc_path, script_argv, &status);
		END_TIMER;
		if (debug_flag) {
			info("%s: get_mcdram_capabilities ran for %s",
			     __func__, TIME_STR);
		}
		_log_script_argv(script_argv, resp_msg);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
			break;	/* Success */
		error("%s: get_mcdram_capabilities status:%u response:%s",
		      __func__, status, resp_msg);
		if (resp_msg == NULL) {
			info("%s: get_mcdram_capabilities returned no information",
			     __func__);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (strstr(resp_msg, "Could not lookup") &&
		    (retry <= capmc_retries)) {
			/* State Manager is down. Sleep and retry */
			sleep(1);
			xfree(resp_msg);
		} else {
			xfree(resp_msg);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
	}
	_free_script_argv(script_argv);

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		rc = SLURM_ERROR;
		goto fini;
	}
	xfree(resp_msg);
	json_object_object_foreachC(j, iter) {
		if (xstrcmp(iter.key, "nids"))
			continue;
		mcdram_cap = _json_parse_mcdram_cap_array(j, iter.key,
							  &mcdram_cap_cnt);
		break;
	}
	json_object_put(j);	/* Frees json memory */

	/*
	 * Load current MCDRAM configuration
	 */
	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("get_mcdram_cfg");
	for (retry = 0; ; retry++) {
		START_TIMER;
		resp_msg = _run_script(capmc_path, script_argv, &status);
		END_TIMER;
		if (debug_flag) {
			info("%s: get_mcdram_cfg ran for %s",
			     __func__, TIME_STR);
		}
		_log_script_argv(script_argv, resp_msg);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
			break;	/* Success */
		error("%s: get_mcdram_cfg status:%u response:%s",
		      __func__, status, resp_msg);
		if (resp_msg == NULL) {
			info("%s: get_mcdram_cfg returned no information",
			     __func__);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (strstr(resp_msg, "Could not lookup") &&
		    (retry <= capmc_retries)) {
			/* State Manager is down. Sleep and retry */
			sleep(1);
			xfree(resp_msg);
		} else {
			xfree(resp_msg);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
	}
	_free_script_argv(script_argv);

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		rc = SLURM_ERROR;
		goto fini;
	}
	xfree(resp_msg);
	json_object_object_foreachC(j, iter) {
		if (xstrcmp(iter.key, "nids"))
			continue;
		mcdram_cfg = _json_parse_mcdram_cfg_array(j, iter.key,
							  &mcdram_cfg_cnt);
		break;
	}
	json_object_put(j);	/* Frees json memory */

	mcdram_cfg2 = _load_current_mcdram(&mcdram_cfg2_cnt);

	/*
	 * Load available NUMA capabilities
	 */
	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("get_numa_capabilities");
	for (retry = 0; ; retry++) {
		START_TIMER;
		resp_msg = _run_script(capmc_path, script_argv, &status);
		END_TIMER;
		if (debug_flag) {
			info("%s: get_numa_capabilities ran for %s",
			     __func__, TIME_STR);
		}
		_log_script_argv(script_argv, resp_msg);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
			break;	/* Success */
		error("%s: get_numa_capabilities status:%u response:%s",
		      __func__, status, resp_msg);
		if (resp_msg == NULL) {
			info("%s: get_numa_capabilities returned no information",
			     __func__);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (strstr(resp_msg, "Could not lookup") &&
		    (retry <= capmc_retries)) {
			/* State Manager is down. Sleep and retry */
			sleep(1);
			xfree(resp_msg);
		} else {
			xfree(resp_msg);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
	}
	_free_script_argv(script_argv);

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		rc = SLURM_ERROR;
		goto fini;
	}
	xfree(resp_msg);
	json_object_object_foreachC(j, iter) {
		if (xstrcmp(iter.key, "nids"))
			continue;
		numa_cap = _json_parse_numa_cap_array(j, iter.key,
						      &numa_cap_cnt);
		break;
	}
	json_object_put(j);	/* Frees json memory */

	/*
	 * Load current NUMA configuration
	 */
	script_argv = xmalloc(sizeof(char *) * 4);	/* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("get_numa_cfg");
	for (retry = 0; ; retry++) {
		START_TIMER;
		resp_msg = _run_script(capmc_path, script_argv, &status);
		END_TIMER;
		if (debug_flag)
			info("%s: get_numa_cfg ran for %s", __func__, TIME_STR);
		_log_script_argv(script_argv, resp_msg);
		if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
			break;	/* Success */
		error("%s: get_numa_cfg status:%u response:%s",
		      __func__, status, resp_msg);
		if (resp_msg == NULL) {
			info("%s: get_numa_cfg returned no information",
			     __func__);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (strstr(resp_msg, "Could not lookup") &&
		    (retry <= capmc_retries)) {
			/* State Manager is down. Sleep and retry */
			sleep(1);
			xfree(resp_msg);
		} else {
			xfree(resp_msg);
			_free_script_argv(script_argv);
			rc = SLURM_ERROR;
			goto fini;
		}
	}
	_free_script_argv(script_argv);

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		rc = SLURM_ERROR;
		goto fini;
	}
	xfree(resp_msg);
	json_object_object_foreachC(j, iter) {
		if (xstrcmp(iter.key, "nids"))
			continue;
		numa_cfg = _json_parse_numa_cfg_array(j, iter.key,
						      &numa_cfg_cnt);
		break;
	}
	json_object_put(j);	/* Frees json memory */

	numa_cfg2 = _load_current_numa(&numa_cfg2_cnt);

	if (debug_flag) {
		_mcdram_cap_log(mcdram_cap, mcdram_cap_cnt);
		_mcdram_cfg_log(mcdram_cfg, mcdram_cfg_cnt);
		_mcdram_cfg2_log(mcdram_cfg2, mcdram_cfg2_cnt);
		_numa_cap_log(numa_cap, numa_cap_cnt);
		_numa_cfg_log(numa_cfg, numa_cfg_cnt);
		_numa_cfg2_log(numa_cfg2, numa_cfg2_cnt);
	}
	for (i = 0; i < mcdram_cfg_cnt; i++) {
		for (k = 0; k < mcdram_cfg2_cnt; k++) {
			if (!mcdram_cfg2[k].node_bitmap ||
			    !bit_test(mcdram_cfg2[k].node_bitmap,
				      mcdram_cfg[i].nid))
				continue;
			if (mcdram_cfg[i].mcdram_pct !=
			    mcdram_cfg2[k].cache_pct) {
				if (mcdram_cfg[i].mcdram_pct == NO_VAL16) {
					info("%s: No mcdram_pct from capmc for nid %u",
					     __func__, mcdram_cfg[i].nid);
				} else {
					info("%s: HBM mismatch between capmc "
					     "and cnselect for nid %u (%u != %d)",
					     __func__, mcdram_cfg[i].nid,
					     mcdram_cfg[i].mcdram_pct,
					     mcdram_cfg2[k].cache_pct);
				}
				mcdram_cfg[i].mcdram_pct =
					mcdram_cfg2[k].cache_pct;
				xfree(mcdram_cfg[i].mcdram_cfg);
				mcdram_cfg[i].mcdram_cfg =
					xstrdup(mcdram_cfg2[k].mcdram_cfg);
			}
			break;
		}
	}
	for (i = 0; i < numa_cfg_cnt; i++) {
		for (k = 0; k < numa_cfg2_cnt; k++) {
			if (!numa_cfg2[k].node_bitmap ||
			    !bit_test(numa_cfg2[k].node_bitmap,
				      numa_cfg[i].nid))
				continue;
			if (xstrcmp(numa_cfg[i].numa_cfg,
				    numa_cfg2[k].numa_cfg)) {
				if (!numa_cfg[i].numa_cfg) {
					info("%s: No numa_cfg from capmc for nid %u",
					     __func__, numa_cfg[i].nid);
				} else {
					info("%s: NUMA mismatch between capmc "
					     "and cnselect for nid %u (%s != %s)",
					     __func__, numa_cfg[i].nid,
					     numa_cfg[i].numa_cfg,
					     numa_cfg2[k].numa_cfg);
				}
				xfree(numa_cfg[i].numa_cfg);
				numa_cfg[i].numa_cfg =
					xstrdup(numa_cfg2[k].numa_cfg);
			}
			break;
		}
	}

	START_TIMER;
	if (node_list) {
		/* Write nodes */
		slurmctld_lock_t write_nodes_lock = {
			NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK};

		if ((host_list = hostlist_create(node_list)) == NULL) {
			error ("hostlist_create error on %s: %m", node_list);
			goto fini;
		}
		hostlist_uniq(host_list);

		if (set_locks)
			lock_slurmctld(write_nodes_lock);
		while ((node_name = hostlist_shift(host_list))) {
			node_ptr = find_node_record(node_name);
			if (node_ptr) {
				_update_node_features(node_ptr,
						      mcdram_cap,mcdram_cap_cnt,
						      mcdram_cfg,mcdram_cfg_cnt,
						      numa_cap, numa_cap_cnt,
						      numa_cfg, numa_cfg_cnt);
			}
			free(node_name);
		}
		if (set_locks)
			unlock_slurmctld(write_nodes_lock);
		hostlist_destroy(host_list);
	} else {
		time_t now = time(NULL);
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (node_ptr->last_response > now) {
				/*
				 * Reboot likely in progress.
				 * Preserve active KNL features and merge
				 * with configured non-KNL features
				 */
				_merge_strings(&node_ptr->features_act,
					       node_ptr->features, 0);
				continue;
			}
			if (validate_mode == 0) {
				_strip_knl_opts(&node_ptr->features);
				xfree(node_ptr->features_act);
				if (node_ptr->features) {
					node_ptr->features_act =
						xstrdup(node_ptr->features);
				}
			} else {
				if (node_ptr->features) {
					node_ptr->features_act =
						xstrdup(node_ptr->features);
				}
			}
		}
		_update_all_node_features(mcdram_cap, mcdram_cap_cnt,
					  mcdram_cfg, mcdram_cfg_cnt,
					  numa_cap, numa_cap_cnt,
					  numa_cfg, numa_cfg_cnt);
	}
	END_TIMER;
	if (debug_flag)
		info("%s: update_node_features ran for %s", __func__, TIME_STR);

	last_node_update = time(NULL);

fini:	_mcdram_cap_free(mcdram_cap, mcdram_cap_cnt);
	_mcdram_cfg_free(mcdram_cfg, mcdram_cfg_cnt);
	_mcdram_cfg2_free(mcdram_cfg2, mcdram_cfg2_cnt);
	_numa_cap_free(numa_cap, numa_cap_cnt);
	_numa_cfg_free(numa_cfg, numa_cfg_cnt);
	_numa_cfg2_free(numa_cfg2, numa_cfg2_cnt);

	return rc;
}

/* Get this node's current and available MCDRAM and NUMA settings from BIOS.
 * avail_modes IN/OUT - append available modes, must be xfreed
 * current_mode IN/OUT - append current modes, must be xfreed
 *
 * NOTE: Not applicable on Cray systems; can be used on other systems.
 *
 * NOTES about syscfg (from Intel):
 * To display the BIOS Parameters:
 * >> syscfg /d biossettings <"BIOS variable Name">
 *
 * To Set the BIOS Parameters:
 * >> syscfg /bcs <AdminPw> <"BIOS variable Name"> <Value>
 * Note: If AdminPw is not set use ""
 */
extern void node_features_p_node_state(char **avail_modes, char **current_mode)
{
	return;		/*  Not applicable on Cray systems */
}

/* Test if a job's feature specification is valid */
extern int node_features_p_job_valid(char *job_features)
{
	uint16_t job_mcdram, job_numa;
	int mcdram_cnt, numa_cnt;
	int last_mcdram_cnt = 0, last_numa_cnt = 0;
	int rc = SLURM_SUCCESS;
	char last_sep = '\0', *tmp, *tok, *save_ptr = NULL;

	if ((job_features == NULL) || (job_features[0] == '\0'))
		return SLURM_SUCCESS;

	tmp = xstrdup(job_features);
	tok = strtok_r(tmp, "[]()|", &save_ptr);
	while (tok) {
		last_sep = tok[strlen(tok) - 1];
		job_mcdram = _knl_mcdram_parse(tok, "&,*");
		mcdram_cnt = _knl_mcdram_bits_cnt(job_mcdram) + last_mcdram_cnt;
		if (mcdram_cnt > 1) {	/* Multiple ANDed MCDRAM options */
			rc = ESLURM_INVALID_KNL;
			break;
		}

		job_numa = _knl_numa_parse(tok, "&,*");
		numa_cnt = _knl_numa_bits_cnt(job_numa) + last_numa_cnt;
		if (numa_cnt > 1) {	/* Multiple ANDed NUMA options */
			rc = ESLURM_INVALID_KNL;
			break;
		}
		tok = strtok_r(NULL, "[]()|", &save_ptr);
		if (tok &&
		    ((last_sep == '&') ||	/* e.g. "equal&(flat|cache)" */
		     (tok[0] == '&'))) {	/* e.g. "(flat|cache)&equal" */
			last_mcdram_cnt += mcdram_cnt;
			last_numa_cnt += numa_cnt;
		} else {
			last_mcdram_cnt = 0;
			last_numa_cnt = 0;
		}
	}
	xfree(tmp);

	return rc;
}

/*
 * Translate a job's feature request to the node features needed at boot time.
 *	If multiple MCDRAM or NUMA values are ORed, pick the first ones.
 * IN job_features - job's --constraint specification
 * RET features required on node reboot. Must xfree to release memory
 */
extern char *node_features_p_job_xlate(char *job_features)
{
	char *node_features = NULL;
	char *tmp, *save_ptr = NULL, *mult, *sep = "", *tok;
	bool has_numa = false, has_mcdram = false;

	if ((job_features == NULL) || (job_features[0] ==  '\0'))
		return node_features;

	tmp = xstrdup(job_features);
	tok = strtok_r(tmp, "[]()|&", &save_ptr);
	while (tok) {
		bool knl_opt = false;
		if ((mult = strchr(tok, '*')))
			mult[0] = '\0';
		if (_knl_mcdram_token(tok)) {
			if (!has_mcdram) {
				has_mcdram = true;
				knl_opt = true;
			}
		}
		if (_knl_numa_token(tok)) {
			if (!has_numa) {
				has_numa = true;
				knl_opt = true;
			}
		}
		if (knl_opt) {
			xstrfmtcat(node_features, "%s%s", sep, tok);
			sep = ",";
		}
		tok = strtok_r(NULL, "[]()|&", &save_ptr);
	}
	xfree(tmp);

	return node_features;
}

/* Return bitmap of KNL nodes, NULL if none identified */
extern bitstr_t *node_features_p_get_node_bitmap(void)
{
	if (knl_node_bitmap)
		return bit_copy(knl_node_bitmap);
	return NULL;
}

/* Return count of overlaping bits in active_bitmap and knl_node_bitmap */
extern int node_features_p_overlap(bitstr_t *active_bitmap)
{
	int cnt = 0;

	if (!knl_node_bitmap || !active_bitmap ||
	    !(cnt = bit_overlap(active_bitmap, knl_node_bitmap)))
		return 0;

	return cnt;
}

/* Return true if the plugin requires PowerSave mode for booting nodes */
extern bool node_features_p_node_power(void)
{
	return true;
}

/* Set's the node's active features based upon job constraints.
 * NOTE: Executed by the slurmd daemon.
 * NOTE: Not applicable for knl_cray plugin, reconfiguration done by slurmctld
 * IN active_features - New active features
 * RET error code */
extern int node_features_p_node_set(char *active_features)
{
	return SLURM_SUCCESS;
}

/*
 * Note the active features associated with a set of nodes have been updated.
 * Specifically update the node's "hbm" GRES and "CpuBind" values as needed.
 * IN active_features - New active features
 * IN node_bitmap - bitmap of nodes changed
 * RET error code
 */
extern int node_features_p_node_update(char *active_features,
				       bitstr_t *node_bitmap)
{
	int i, i_first, i_last;
	int rc = SLURM_SUCCESS, numa_inx = -1;
	int mcdram_inx = 0;
	uint64_t mcdram_size;
	struct node_record *node_ptr;
	char *save_ptr = NULL, *tmp, *tok;

	if (mcdram_per_node == NULL)
		error("%s: mcdram_per_node == NULL", __func__);

	if (active_features) {
		tmp = xstrdup(active_features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if (numa_inx == -1)
				numa_inx = _knl_numa_inx(tok);
			mcdram_inx |= _knl_mcdram_token(tok);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	}

	if (mcdram_inx >= 0) {
		for (i = 0; i < KNL_MCDRAM_CNT; i++) {
			if ((KNL_CACHE << i) == mcdram_inx)
				break;
		}
		if ((i >= KNL_MCDRAM_CNT) || (mcdram_pct[i] == -1))
			mcdram_inx = -1;
		else
			mcdram_inx = i;
	} else {
		mcdram_inx = -1;
	}

	xassert(node_bitmap);
	i_first = bit_ffs(node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		if (i >= node_record_count) {
			error("%s: Invalid node index (%d >= %d)",
			      __func__, i, node_record_count);
			rc = SLURM_ERROR;
			break;
		}
		node_ptr = node_record_table_ptr + i;
		if ((numa_inx >= 0) && cpu_bind[numa_inx])
			node_ptr->cpu_bind = cpu_bind[numa_inx];
		if (mcdram_per_node && (mcdram_inx >= 0)) {
			mcdram_size = mcdram_per_node[i] *
				      (100 - mcdram_pct[mcdram_inx]) / 100;
			gres_plugin_node_feature(node_ptr->name, "hbm",
						 mcdram_size, &node_ptr->gres,
						 &node_ptr->gres_list);
		}
	}

	return rc;
}

/*
 * Return TRUE if the specified node update request is valid with respect
 * to features changes (i.e. don't permit a non-KNL node to set KNL features).
 *
 * arg IN - Pointer to struct node_record record
 * update_node_msg IN - Pointer to update request
 */
extern bool node_features_p_node_update_valid(void *arg,
					update_node_msg_t *update_node_msg)
{
	struct node_record *node_ptr = (struct node_record *) arg;
	char *tmp, *save_ptr = NULL, *tok;
	bool is_knl = false, invalid_feature = false;

	/* No feature changes */
	if (!update_node_msg->features && !update_node_msg->features_act)
		return true;

	/* Determine if this is KNL node based upon current features */
	if (node_ptr->features && node_ptr->features[0]) {
		tmp = xstrdup(node_ptr->features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if (_knl_mcdram_token(tok) || _knl_numa_token(tok)) {
				is_knl = true;
				break;
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	}
	if (is_knl)
		return true;

	/* Validate that AvailableFeatures update request has no KNL modes */
	if (update_node_msg->features) {
		tmp = xstrdup(update_node_msg->features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if (_knl_mcdram_token(tok) || _knl_numa_token(tok)) {
				invalid_feature = true;
				break;
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
		if (invalid_feature) {
			info("Invalid AvailableFeatures update request (%s) for non-KNL node %s",
			     update_node_msg->features, node_ptr->name);
			return false;
		}
	}

	/* Validate that ActiveFeatures update request has no KNL modes */
	if (update_node_msg->features_act) {
		tmp = xstrdup(update_node_msg->features_act);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if (_knl_mcdram_token(tok) || _knl_numa_token(tok)) {
				invalid_feature = true;
				break;
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
		if (invalid_feature) {
			info("Invalid ActiveFeatures update request (%s) for non-KNL node %s",
			     update_node_msg->features_act, node_ptr->name);
			return false;
		}
	}

	/*
	 * For non-KNL node, active and available features must match
	 */
	if (!update_node_msg->features) {
		update_node_msg->features =
			xstrdup(update_node_msg->features_act);
	} else if (!update_node_msg->features_act) {
		update_node_msg->features_act =
			xstrdup(update_node_msg->features);
	} else if (xstrcmp(update_node_msg->features,
			   update_node_msg->features_act)) {
		info("Invalid ActiveFeatures != AvailableFeatures (%s != %s) for non-KNL node %s",
		     update_node_msg->features, update_node_msg->features_act,
		     node_ptr->name);
		return false;
	}

	return true;
}

/* Return TRUE if this (one) feature name is under this plugin's control */
extern bool node_features_p_changeable_feature(char *feature)
{
	if ((validate_mode == 0) &&
	    (_knl_mcdram_token(feature) || _knl_numa_token(feature)))
		return true;
	return false;
}

/*
 * Translate a node's feature specification by replacing any features associated
 *	with this plugin in the original value with the new values, preserving
 *	any features that are not associated with this plugin
 * IN new_features - newly active features
 * IN orig_features - original active features
 * IN avail_features - original available features
 * IN node_inx - index of node in node table
 * RET node's new merged features, must be xfreed
 */
extern char *node_features_p_node_xlate(char *new_features, char *orig_features,
					char *avail_features, int node_inx)
{
	char *node_features = NULL;
	char *tmp, *save_ptr = NULL, *sep = "", *tok;
	uint16_t new_mcdram = 0, new_numa = 0;
	uint16_t tmp_mcdram, tmp_numa;
	bool is_knl = false;

	if (avail_features) {
		tmp = xstrdup(avail_features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if (_knl_mcdram_token(tok) || _knl_numa_token(tok)) {
				is_knl = true;
			} else {
				xstrfmtcat(node_features, "%s%s", sep, tok);
				sep = ",";
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
		if (!is_knl) {
			xfree(node_features);
			sep = "";
		}
	}

	if (new_features) {
		/* Copy non-KNL features */
		if (!is_knl && new_features) {
			tmp = xstrdup(new_features);
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if ((_knl_mcdram_token(tok) == 0) &&
				    (_knl_numa_token(tok)   == 0)) {
					xstrfmtcat(node_features, "%s%s", sep,
						   tok);
					sep = ",";
				}
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		}

		/* Copy new KNL features in MCDRAM/NUMA order */
		tmp = xstrdup(new_features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if ((tmp_mcdram = _knl_mcdram_token(tok)))
				new_mcdram |= tmp_mcdram;
			else if ((tmp_numa = _knl_numa_token(tok)))
				new_numa |= tmp_numa;
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);

		if (is_knl && ((new_mcdram == 0) || (new_numa == 0))) {
			/*
			 * New active features lacks current MCDRAM or NUMA,
			 * copy values from original
			 */
			tmp = xstrdup(orig_features);
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if ((new_mcdram == 0) &&
				    (tmp_mcdram = _knl_mcdram_token(tok)))
					new_mcdram |= tmp_mcdram;
				else if ((new_numa == 0) &&
					 (tmp_numa = _knl_numa_token(tok)))
					new_numa |= tmp_numa;
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		}
		if (new_mcdram) {
			tmp = _knl_mcdram_str(new_mcdram);
			xstrfmtcat(node_features, "%s%s", sep, tmp);
			xfree(tmp);
			sep = ",";
		}
		if (new_numa) {
			tmp = _knl_numa_str(new_numa);
			xstrfmtcat(node_features, "%s%s", sep, tmp);
			xfree(tmp);
		}
	}

	return node_features;
}

/* Translate a node's new feature specification into a "standard" ordering
 * RET node's new merged features, must be xfreed */
extern char *node_features_p_node_xlate2(char *new_features)
{
	char *node_features = NULL;
	char *tmp, *save_ptr = NULL, *sep = "", *tok;
	uint16_t new_mcdram = 0, new_numa = 0;
	uint16_t tmp_mcdram, tmp_numa;

	if (new_features) {
		tmp = xstrdup(new_features);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if ((tmp_mcdram = _knl_mcdram_token(tok))) {
				new_mcdram |= tmp_mcdram;
			} else if ((tmp_numa = _knl_numa_token(tok))) {
				new_numa |= tmp_numa;
			} else {
				xstrfmtcat(node_features, "%s%s", sep, tok);
				sep = ",";
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
		if (new_mcdram) {
			tmp = _knl_mcdram_str(new_mcdram);
			xstrfmtcat(node_features, "%s%s", sep, tmp);
			xfree(tmp);
			sep = ",";
		}
		if (new_numa) {
			tmp = _knl_numa_str(new_numa);
			xstrfmtcat(node_features, "%s%s", sep, tmp);
			xfree(tmp);
		}
	}

	return node_features;
}

/* Perform set up for step launch
 * mem_sort IN - Trigger sort of memory pages (KNL zonesort)
 * numa_bitmap IN - NUMA nodes allocated to this job */
extern void node_features_p_step_config(bool mem_sort, bitstr_t *numa_bitmap)
{
#ifdef HAVE_NUMA
	if (mem_sort && (numa_available() != -1)) {
		struct stat sb;
		int buf_len, fd, i, len, rc;
		char buf[12];

		if (stat(ZONE_SORT_PATH, &sb) == -1) {
			rc = system(MODPROBE_PATH " zonesort_module");
			if (rc != -1)
				rc = WEXITSTATUS(rc);
			if (rc) {
				verbose("%s: zonesort execution failure. Return code: %d",
					__func__, rc);
			}
		}
		if ((fd = open(ZONE_SORT_PATH, O_WRONLY | O_SYNC)) == -1) {
			error("%s: Could not open file %s: %m",
			      __func__, ZONE_SORT_PATH);
		} else {
			len = numa_max_node() + 1;
			for (i = 0; i < len; i++) {
				if (numa_bitmap && !bit_test(numa_bitmap, i))
					continue;
				snprintf(buf, sizeof(buf), "%d", i);
				buf_len = strlen(buf) + 1;
				if (write(fd, buf, buf_len) != buf_len) {
					error("%s: Could not write file %s: %m",
					      __func__, ZONE_SORT_PATH);
				}
			}
			(void) close(fd);
		}
	}
#endif
}

/* Determine if the specified user can modify the currently available node
 * features */
extern bool node_features_p_user_update(uid_t uid)
{
	int i;

	if (allowed_uid_cnt == 0)   /* Default is ALL users allowed to update */
		return true;

	for (i = 0; i < allowed_uid_cnt; i++) {
		if (allowed_uid[i] == uid)
			return true;
	}

	return false;
}

/* Return estimated reboot time, in seconds */
extern uint32_t node_features_p_boot_time(void)
{
	return boot_time;
}

/* Get node features plugin configuration */
extern void node_features_p_get_config(config_plugin_params_t *p)
{
	config_key_pair_t *key_pair;
	List data;

	xassert(p);
	xstrcat(p->name, plugin_type);
	data = p->key_pairs;

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowMCDRAM");
	key_pair->value = _knl_mcdram_str(allow_mcdram);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowNUMA");
	key_pair->value = _knl_numa_str(allow_numa);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowUserBoot");
	key_pair->value = _make_uid_str(allowed_uid, allowed_uid_cnt);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BootTime");
	key_pair->value = xstrdup_printf("%u", boot_time);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CapmcPath");
	key_pair->value = xstrdup(capmc_path);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CapmcPollFreq");
	key_pair->value = xstrdup_printf("%u", capmc_poll_freq);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CapmcRetries");
	key_pair->value = xstrdup_printf("%u", capmc_retries);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CapmcTimeout");
	key_pair->value = xstrdup_printf("%u", capmc_timeout);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CnselectPath");
	key_pair->value = xstrdup(cnselect_path);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DefaultMCDRAM");
	key_pair->value = _knl_mcdram_str(default_mcdram);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DefaultNUMA");
	key_pair->value = _knl_numa_str(default_numa);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("McPath");
	key_pair->value = xstrdup(mc_path);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("NodeRebootWeight");
	key_pair->value = xstrdup_printf("%u", node_reboot_weight);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SyscfgPath");
	key_pair->value = xstrdup(syscfg_path);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UmeCheckInterval");
	key_pair->value = xstrdup_printf("%u", ume_check_interval);
	list_append(data, key_pair);

	list_sort(data, (ListCmpF) sort_key_pairs);

	return;
}

/*
 * Return node "weight" field if reboot required to change mode
 */
extern uint32_t node_features_p_reboot_weight(void)
{
	return node_reboot_weight;
}
