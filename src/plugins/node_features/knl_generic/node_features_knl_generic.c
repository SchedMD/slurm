/*****************************************************************************\
 *  node_features_knl_generic.c - Plugin for managing Intel KNL state
 *  information on a generic Linux cluster
 *****************************************************************************
 *  Copyright (C) 2016-2017 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *             Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <fcntl.h>
#ifdef HAVE_NUMA
#undef NUMA_VERSION1_COMPATIBILITY
#include <numa.h>
#endif
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmd/slurmd/req.h"

/* Maximum poll wait time for child processes, in milliseconds */
#define MAX_POLL_WAIT   500
#define DEFAULT_SYSCFG_TIMEOUT 1000

/* Intel Knights Landing Configuration Modes */
#define KNL_NUMA_CNT	5
#define KNL_MCDRAM_CNT	5
#define KNL_NUMA_FLAG	0x00ff
#define KNL_ALL2ALL	0x0001
#define KNL_SNC2	0x0002
#define KNL_SNC4	0x0004
#define KNL_HEMI	0x0008
#define KNL_QUAD	0x0010
#define KNL_MCDRAM_FLAG	0xff00
#define KNL_CACHE	0x0100
#define KNL_EQUAL	0x0200
#define KNL_HYBRID	0x0400
#define KNL_FLAT	0x0800
#define KNL_AUTO	0x1000

#ifndef MODPROBE_PATH
#define MODPROBE_PATH	"/sbin/modprobe"
#endif
#define ZONE_SORT_PATH	"/sys/kernel/zone_sort_free_pages/nodeid"
//#define ZONE_SORT_PATH	"/tmp/nodeid"	/* For testing */

#ifndef DEFAULT_MCDRAM_SIZE
#define DEFAULT_MCDRAM_SIZE ((uint64_t) 16 * 1024 * 1024 * 1024)
#endif

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurmctld_config_t slurmctld_config __attribute__((weak_import));
#else
slurmctld_config_t slurmctld_config;
#endif

typedef enum {
	KNL_SYSTEM_TYPE_NOT_SET,
	KNL_SYSTEM_TYPE_INTEL,
	KNL_SYSTEM_TYPE_DELL,
} knl_system_type_t;

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "node_features" for SLURM node_features) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a node_features plugin if the plugin_type string has a prefix of
 * "node_features/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "node_features knl_generic plugin";
const char plugin_type[]        = "node_features/knl_generic";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* Configuration Parameters */
static uint16_t allow_mcdram = KNL_MCDRAM_FLAG;
static uint16_t allow_numa = KNL_NUMA_FLAG;
static uid_t *allowed_uid = NULL;
static int allowed_uid_cnt = 0;
static uint32_t boot_time = (5 * 60);	/* 5 minute estimated boot time */
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool debug_flag = false;
static uint16_t default_mcdram = KNL_CACHE;
static uint16_t default_numa = KNL_ALL2ALL;
static char *mc_path = NULL;
static uint32_t syscfg_timeout = 0;
static bool reconfig = false;
static time_t shutdown_time = 0;
static int syscfg_found = -1;
static char *syscfg_path = NULL;
static knl_system_type_t knl_system_type = KNL_SYSTEM_TYPE_INTEL;
static uint32_t ume_check_interval = 0;
static pthread_mutex_t ume_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ume_thread = 0;
static uint32_t force_load = 0;
static int hw_is_knl = -1;

/* Percentage of MCDRAM used for cache by type, updated from syscfg */
static int mcdram_pct[KNL_MCDRAM_CNT];
static uint64_t *mcdram_per_node = NULL;

static s_p_options_t knl_conf_file_options[] = {
	{"AllowMCDRAM", S_P_STRING},
	{"AllowNUMA", S_P_STRING},
	{"AllowUserBoot", S_P_STRING},
	{"BootTime", S_P_UINT32},
	{"DefaultMCDRAM", S_P_STRING},
	{"DefaultNUMA", S_P_STRING},
	{"Force", S_P_UINT32},
	{"LogFile", S_P_STRING},
	{"McPath", S_P_STRING},
	{"SyscfgPath", S_P_STRING},
	{"SyscfgTimeout", S_P_UINT32},
	{"SystemType", S_P_STRING},
	{"UmeCheckInterval", S_P_UINT32},
	{NULL}
};

static s_p_hashtbl_t *_config_make_tbl(char *filename);
static int  _knl_mcdram_bits_cnt(uint16_t mcdram_num);
static uint16_t _knl_mcdram_parse(char *mcdram_str, char *sep);
static char *_knl_mcdram_str(uint16_t mcdram_num);
static uint16_t _knl_mcdram_token(char *token);
static int _knl_numa_bits_cnt(uint16_t numa_num);
static uint16_t _knl_numa_parse(char *numa_str, char *sep);
static char *_knl_numa_str(uint16_t numa_num);
static uint16_t _knl_numa_token(char *token);
static void _log_script_argv(char **script_argv, char *resp_msg);
static char *_run_script(char *cmd_path, char **script_argv, int *status);
static int  _tot_wait (struct timeval *start_time);

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
	if (mcdram_num & KNL_HYBRID) {
		xstrfmtcat(mcdram_str, "%shybrid", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_FLAT) {
		xstrfmtcat(mcdram_str, "%sflat", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_EQUAL) {
		xstrfmtcat(mcdram_str, "%sequal", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_AUTO) {
		xstrfmtcat(mcdram_str, "%sauto", sep);
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
	else if (!xstrcasecmp(token, "hybrid"))
		mcdram_num = KNL_HYBRID;
	else if (!xstrcasecmp(token, "flat") ||
		 !xstrcasecmp(token, "memory"))
		mcdram_num = KNL_FLAT;
	else if (!xstrcasecmp(token, "equal"))
		mcdram_num = KNL_EQUAL;
	else if (!xstrcasecmp(token, "auto"))
		mcdram_num = KNL_AUTO;

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
 * Translate KNL System enum to equivalent string value
 */
static char *_knl_system_type_str(knl_system_type_t system_type)
{
	switch (system_type) {
	case KNL_SYSTEM_TYPE_INTEL:
		return "Intel";
	case KNL_SYSTEM_TYPE_DELL:
		return "Dell";
	case KNL_SYSTEM_TYPE_NOT_SET:
	default:
		return "Unknown";
	}
}

/*
 * Given a KNL System token, return its equivalent enum value
 * token IN - String to scan
 * RET System enum value
 */
static knl_system_type_t _knl_system_type_token(char *token)
{
	knl_system_type_t system_type;

	if (!xstrcasecmp("intel", token))
		system_type = KNL_SYSTEM_TYPE_INTEL;
	else if (!xstrcasecmp("dell", token))
		system_type = KNL_SYSTEM_TYPE_DELL;
	else
		system_type = KNL_SYSTEM_TYPE_NOT_SET;

	return system_type;
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
		resp = xstrdup("Slurm node_features/knl_generic configuration error");
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
			new_wait = syscfg_timeout - _tot_wait(&tstart);
			if (new_wait <= 0) {
				error("%s: %s poll timeout @ %d msec",
				      __func__, script_argv[1], syscfg_timeout);
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
				error("%s: read(%s): %m", __func__, syscfg_path);
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
			error("knl_generic.conf: Invalid AllowUserBoot: %s", tok);
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
	char *knl_conf_file, *tmp_str = NULL, *resume_program;
	s_p_hashtbl_t *tbl;
	struct stat stat_buf;
	int rc = SLURM_SUCCESS;
	char* cpuinfo_path = "/proc/cpuinfo";
	FILE *cpu_info_file;
	char buf[1024];

	/* Set default values */
	allow_mcdram = KNL_MCDRAM_FLAG;
	allow_numa = KNL_NUMA_FLAG;
	xfree(allowed_uid);
	xfree(mc_path);
	xfree(syscfg_path);
	allowed_uid_cnt = 0;
	syscfg_timeout = DEFAULT_SYSCFG_TIMEOUT;
	debug_flag = false;
	default_mcdram = KNL_CACHE;
	default_numa = KNL_ALL2ALL;

//FIXME: Need better mechanism to get MCDRAM percentages
//	for (i = 0; i < KNL_MCDRAM_CNT; i++)
//		mcdram_pct[i] = -1;
	mcdram_pct[0] = 100;	// KNL_CACHE
	mcdram_pct[1] = 50;	// KNL_EQUAL
	mcdram_pct[2] = 50;	// KNL_HYBRID
	mcdram_pct[3] = 0;	// KNL_FLAT
	mcdram_pct[4] = 0;	// KNL_AUTO

	knl_conf_file = get_extra_conf_path("knl_generic.conf");
	if ((stat(knl_conf_file, &stat_buf) == 0) &&
	    (tbl = _config_make_tbl(knl_conf_file))) {
		if (s_p_get_string(&tmp_str, "AllowMCDRAM", tbl)) {
			allow_mcdram = _knl_mcdram_parse(tmp_str, ",");
			if (_knl_mcdram_bits_cnt(allow_mcdram) < 1) {
				fatal("knl_generic.conf: Invalid AllowMCDRAM=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AllowNUMA", tbl)) {
			allow_numa = _knl_numa_parse(tmp_str, ",");
			if (_knl_numa_bits_cnt(allow_numa) < 1) {
				fatal("knl_generic.conf: Invalid AllowNUMA=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AllowUserBoot", tbl)) {
			_make_uid_array(tmp_str);
			xfree(tmp_str);
		}
		(void) s_p_get_uint32(&boot_time, "BootTime", tbl);
		if (s_p_get_string(&tmp_str, "DefaultMCDRAM", tbl)) {
			default_mcdram = _knl_mcdram_parse(tmp_str, ",");
			if (_knl_mcdram_bits_cnt(default_mcdram) != 1) {
				fatal("knl_generic.conf: Invalid DefaultMCDRAM=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultNUMA", tbl)) {
			default_numa = _knl_numa_parse(tmp_str, ",");
			if (_knl_numa_bits_cnt(default_numa) != 1) {
				fatal("knl_generic.conf: Invalid DefaultNUMA=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		(void) s_p_get_uint32(&force_load, "Force", tbl);
		(void) s_p_get_string(&mc_path, "McPath", tbl);
		(void) s_p_get_string(&syscfg_path, "SyscfgPath", tbl);
		if (s_p_get_string(&tmp_str, "SystemType", tbl)) {
			if ((knl_system_type = _knl_system_type_token(tmp_str))
			    == KNL_SYSTEM_TYPE_NOT_SET)
				fatal("knl_generic.conf: Invalid SystemType=%s.",
				      tmp_str);
			xfree(tmp_str);
		}
		(void) s_p_get_uint32(&syscfg_timeout, "SyscfgTimeout", tbl);
		(void) s_p_get_uint32(&ume_check_interval, "UmeCheckInterval",
				      tbl);

		s_p_hashtbl_destroy(tbl);
	} else if (errno != ENOENT) {
		error("Error opening/reading knl_generic.conf: %m");
		rc = SLURM_ERROR;
	}
	xfree(knl_conf_file);
	if (!mc_path)
		mc_path = xstrdup("/sys/devices/system/edac/mc");
	if (!syscfg_path)
		syscfg_path = xstrdup("/usr/bin/syscfg");
	if (access(syscfg_path, X_OK) == 0)
		syscfg_found = 1;
	else
		syscfg_found = 0;

	hw_is_knl = 0;
	cpu_info_file = fopen(cpuinfo_path, "r");
	if (cpu_info_file == NULL) {
		error("Error opening/reading %s: %m", cpuinfo_path);
	} else {
		while (fgets(buf, sizeof(buf), cpu_info_file)) {
			if (strstr(buf, "Xeon Phi")) {
				hw_is_knl = 1;
				break;
			}
		}
		fclose(cpu_info_file);
	}

	if ((resume_program = slurm_get_resume_program())) {
		error("Use of ResumeProgram with %s not currently supported",
		      plugin_name);
		xfree(resume_program);
		rc = SLURM_ERROR;
	}

	if (slurm_get_debug_flags() & DEBUG_FLAG_NODE_FEATURES)
		debug_flag = true;

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
		info("DefaultMCDRAM=%s DefaultNUMA=%s",
		     default_mcdram_str, default_numa_str);
		info("Force=%u", force_load);
		info("McPath=%s", mc_path);
		info("SyscfgPath=%s", syscfg_path);
		info("SyscfgTimeout=%u msec", syscfg_timeout);
		info("SystemType=%s", _knl_system_type_str(knl_system_type));
		info("UmeCheckInterval=%u", ume_check_interval);
		xfree(allow_mcdram_str);
		xfree(allow_numa_str);
		xfree(allow_user_str);
		xfree(default_mcdram_str);
		xfree(default_numa_str);
	}
	gres_plugin_add("hbm");

	if ((rc == SLURM_SUCCESS) &&
	    ume_check_interval && run_in_daemon("slurmd")) {
		slurm_mutex_lock(&ume_mutex);
		slurm_thread_create(&ume_thread, _ume_agent, NULL);
		slurm_mutex_unlock(&ume_mutex);
	}

	return rc;
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
	xfree(allowed_uid);
	allowed_uid_cnt = 0;
	debug_flag = false;
	xfree(mcdram_per_node);
	xfree(mc_path);
	xfree(syscfg_path);

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

/* Update active and available features on specified nodes,
 * sets features on all nodes if node_list is NULL */
extern int node_features_p_get_node(char *node_list)
{
	slurm_mutex_lock(&config_mutex);
	if (reconfig) {
		(void) init();
		reconfig = false;
	}
	slurm_mutex_unlock(&config_mutex);
	return SLURM_SUCCESS;
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
	char *avail_states = NULL, *cur_state = NULL;
	char *resp_msg, *argv[10], *avail_sep = "", *cur_sep = "", *tok;
	int status = 0;
	int len = 0;

	if (!syscfg_path || !avail_modes || !current_mode)
		return;
	if ((syscfg_found == 0) || (!hw_is_knl && !force_load)) {
		/* This node on cluster lacks syscfg; should not be KNL */
		static bool log_event = true;
		if (log_event) {
			info("%s: syscfg program not found or node isn't KNL, can not get KNL modes",
			     __func__);
			log_event = false;
		}
		*avail_modes = NULL;
		*current_mode = NULL;
		return;
	}

	switch (knl_system_type) {
	case KNL_SYSTEM_TYPE_INTEL:
		argv[0] = "syscfg";
		argv[1] = "/d";
		argv[2] = "BIOSSETTINGS";
		argv[3] = "Cluster Mode";
		argv[4] = NULL;
		break;
	case KNL_SYSTEM_TYPE_DELL:
		argv[0] = "syscfg";
		argv[1] = "--SystemMemoryModel";
		argv[2] = NULL;
		break;
	default:
		/* This should never happen */
		error("%s: Unknown SystemType. %d", __func__, knl_system_type);
		*avail_modes = NULL;
		*current_mode = NULL;
		return;
	}
	resp_msg = _run_script(syscfg_path, argv, &status);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: syscfg (get cluster mode) status:%u response:%s",
		      __func__, status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s: syscfg returned no information", __func__);
	} else {
		tok = NULL;
		_log_script_argv(argv, resp_msg);
		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			tok = strstr(resp_msg, "Current Value : ");
			len = 16;
			break;
		case KNL_SYSTEM_TYPE_DELL:
			tok = strstr(resp_msg, "SystemMemoryModel=");
			len = 18;
			break;
		default:
			/* already handled above, should never get here */
			break;
		}
		if (tok) {
			tok += len;
			if (!xstrncasecmp(tok, "All2All", 3)) {
				cur_state = xstrdup("a2a");
				cur_sep = ",";
			} else if (!xstrncasecmp(tok, "Hemisphere", 3)) {
				cur_state = xstrdup("hemi");
				cur_sep = ",";
			} else if (!xstrncasecmp(tok, "Quadrant", 3)) {
				cur_state = xstrdup("quad");
				cur_sep = ",";
			} else if (!xstrncasecmp(tok, "SNC-2", 5)) {
				cur_state = xstrdup("snc2");
				cur_sep = ",";
			} else if (!xstrncasecmp(tok, "SNC-4", 5)) {
				cur_state = xstrdup("snc4");
				cur_sep = ",";
			}
		}

		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_DELL:
			argv[0] = "syscfg";
			argv[1] = "-h";
			argv[2] = "--SystemMemoryModel";
			argv[3] = NULL;

			xfree(resp_msg);
			resp_msg = _run_script(syscfg_path, argv, &status);
			if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
				error("%s: syscfg (get cluster mode) status:%u response:%s",
				      __func__, status, resp_msg);
			}
			if (resp_msg == NULL)
				info("%s: syscfg -h --SystemMemoryModel returned no information", __func__);
			break;
		default:
			break;
		}

		if (xstrcasestr(resp_msg, "All2All")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "a2a");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Hemisphere")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "hemi");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Quadrant")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "quad");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "SNC-2")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "snc2");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "SNC-4")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "snc4");
			avail_sep = ",";
		}
		xfree(resp_msg);
	}

	switch (knl_system_type) {
	case KNL_SYSTEM_TYPE_INTEL:
		argv[0] = "syscfg";
		argv[1] = "/d";
		argv[2] = "BIOSSETTINGS";
		argv[3] = "Memory Mode";
		argv[4] = NULL;
		break;
	case KNL_SYSTEM_TYPE_DELL:
		argv[0] = "syscfg";
		argv[1] = "--ProcEmbMemMode";
		argv[2] = NULL;
		break;
	default:
		/* already handled above, should never get here */
		break;
	}
	resp_msg = _run_script(syscfg_path, argv, &status);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: syscfg (get memory mode) status:%u response:%s",
		      __func__, status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s: syscfg returned no information", __func__);
	} else {
		tok = NULL;
		_log_script_argv(argv, resp_msg);
		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			tok = strstr(resp_msg, "Current Value : ");
			len = 16;
			break;
		case KNL_SYSTEM_TYPE_DELL:
			tok = strstr(resp_msg, "ProcEmbMemMode=");
			len = 15;
			break;
		default:
			/* already handled above, should never get here */
			break;
		}
		if (tok) {
			tok += len;
			if (!xstrncasecmp(tok, "Cache", 3)) {
				xstrfmtcat(cur_state, "%s%s", cur_sep, "cache");
			} else if (!xstrncasecmp(tok, "Flat", 3) ||
				   !xstrncasecmp(tok, "Memory", 3)) {
				xstrfmtcat(cur_state, "%s%s", cur_sep, "flat");
			} else if (!xstrncasecmp(tok, "Hybrid", 3)) {
				xstrfmtcat(cur_state, "%s%s", cur_sep, "hybrid");
			} else if (!xstrncasecmp(tok, "Equal", 3)) {
				xstrfmtcat(cur_state, "%s%s", cur_sep, "equal");
			} else if (!xstrncasecmp(tok, "Auto", 3)) {
				xstrfmtcat(cur_state, "%s%s", cur_sep, "auto");
			}
		}

		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_DELL:
			argv[0] = "syscfg";
			argv[1] = "-h";
			argv[2] = "--ProcEmbMemMode";
			argv[3] = NULL;

			xfree(resp_msg);
			resp_msg = _run_script(syscfg_path, argv, &status);
			if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
				error("%s: syscfg (get memory mode) status help:%u response:%s",
				      __func__, status, resp_msg);
			}
			if (resp_msg == NULL)
				info("%s: syscfg -h returned no information", __func__);
			break;
		default:
			break;
		}

		if (xstrcasestr(resp_msg, "Cache")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "cache");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Flat") ||
		    xstrcasestr(resp_msg, "Memory")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "flat");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Hybrid")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "hybrid");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Equal")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "equal");
			avail_sep = ",";
		}
		if (xstrcasestr(resp_msg, "Auto")) {
			xstrfmtcat(avail_states, "%s%s", avail_sep, "auto");
			/* avail_sep = ",";	CLANG error: Dead assignment */
		}
		xfree(resp_msg);
	}

	if (*avail_modes) {	/* Append for multiple node_features plugins */
		if (*avail_modes[0])
			avail_sep = ",";
		else
			avail_sep = "";
		xstrfmtcat(*avail_modes, "%s%s", avail_sep, avail_states);
		xfree(avail_states);
	} else {
		*avail_modes = avail_states;
	}

	if (*current_mode) {	/* Append for multiple node_features plugins */
		if (*current_mode[0])
			cur_sep = ",";
		else
			cur_sep = "";
		xstrfmtcat(*current_mode, "%s%s", cur_sep, cur_state);
		xfree(cur_state);
	} else {
		*current_mode = cur_state;
	}
}

/* Test if a job's feature specification is valid */
extern int node_features_p_job_valid(char *job_features)
{
	uint16_t job_mcdram, job_numa;
	int mcdram_cnt, numa_cnt;

	if ((job_features == NULL) || (job_features[0] == '\0'))
		return SLURM_SUCCESS;

	if (strchr(job_features, '[') ||	/* Unsupported operator */
	    strchr(job_features, ']') ||
	    strchr(job_features, '|') ||
	    strchr(job_features, '*'))
		return ESLURM_INVALID_KNL;

	job_mcdram = _knl_mcdram_parse(job_features, "&,");
	mcdram_cnt = _knl_mcdram_bits_cnt(job_mcdram);
	if (mcdram_cnt > 1)			/* Multiple MCDRAM options */
		return ESLURM_INVALID_KNL;

	job_numa = _knl_numa_parse(job_features, "&,");
	numa_cnt = _knl_numa_bits_cnt(job_numa);
	if (numa_cnt > 1)			/* Multiple NUMA options */
		return ESLURM_INVALID_KNL;

	return SLURM_SUCCESS;
}

/* Translate a job's feature request to the node features needed at boot time */
extern char *node_features_p_job_xlate(char *job_features)
{
	char *node_features = NULL;
	char *tmp, *save_ptr = NULL, *sep = "", *tok;
	bool has_numa = false, has_mcdram = false;

	if ((job_features == NULL) || (job_features[0] ==  '\0'))
		return node_features;

	tmp = xstrdup(job_features);
	tok = strtok_r(tmp, "&", &save_ptr);
	while (tok) {
		bool knl_opt = false;
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
		tok = strtok_r(NULL, "&", &save_ptr);
	}
	xfree(tmp);

	/*
	 * No MCDRAM or NUMA features specified. This might be a non-KNL node.
	 * In that case, do not set the default any MCDRAM or NUMA features.
	 */
	if (!has_mcdram && !has_numa)
		return xstrdup(job_features);

	/* Add default options */
	if (!has_mcdram) {
		tmp = _knl_mcdram_str(default_mcdram);
		xstrfmtcat(node_features, "%s%s", sep, tmp);
		sep = ",";
		xfree(tmp);
	}
	if (!has_numa) {
		tmp = _knl_numa_str(default_numa);
		xstrfmtcat(node_features, "%s%s", sep, tmp);
		// sep = ",";		Removed to avoid CLANG error
		xfree(tmp);
	}

	return node_features;
}

static char *_find_key_val(char *key, char *resp_msg)
{
	char *sep = NULL, *tok, *val = NULL;
	int i;

	if ((key == NULL) || (resp_msg == NULL))
		return NULL;

	if ((tok = strstr(resp_msg, "Possible Values")))
		tok += 15;
	else
		tok = resp_msg;
	if ((tok = strstr(tok, key)))
		sep = strchr(tok, ':');
	if (sep) {
		sep++;
		while ((sep[0] != '\0')&& !isdigit(sep[0]))
			sep++;
		if (isdigit(sep[0])) {
			val = xstrdup(sep);
			for (i = 1 ; val[i]; i++) {
				if (!isdigit(val[i])) {
					val[i] = '\0';
					break;
				}
			}
		}
	}

	return val;
}

/* Set's the node's active features based upon job constraints.
 * NOTE: Executed by the slurmd daemon.
 * IN active_features - New active features
 * RET error code */
extern int node_features_p_node_set(char *active_features)
{
	char *resp_msg, *argv[10], tmp[100];
	char *key;
	int error_code = SLURM_SUCCESS, status = 0;
	char *mcdram_mode = NULL, *numa_mode = NULL;

	if ((active_features == NULL) || (active_features[0] == '\0'))
		return SLURM_SUCCESS;

	if (!syscfg_path) {
		error("%s: SyscfgPath not configured", __func__);
		return SLURM_ERROR;
	}
	if ((syscfg_found == 0) || (!hw_is_knl && !force_load)) {
		/* This node on cluster lacks syscfg; should not be KNL */
		static bool log_event = true;
		if (log_event) {
			error("%s: syscfg program not found or node isn't KNL; can not set KNL modes",
			      __func__);
			log_event = false;
		}
		return SLURM_ERROR;
	}

	/* Identify available Cluster/NUMA modes */
	switch (knl_system_type) {
	case KNL_SYSTEM_TYPE_INTEL:
		argv[0] = "syscfg";
		argv[1] = "/d";
		argv[2] = "BIOSSETTINGS";
		argv[3] = "Cluster Mode";
		argv[4] = NULL;
		break;
	case KNL_SYSTEM_TYPE_DELL:
		argv[0] = "syscfg";
		argv[1] = "--SystemMemoryModel";
		argv[2] = NULL;
		break;
	default:
		/* This should never happen */
		error("%s: Unknown SystemType. %d", __func__, knl_system_type);
		return SLURM_ERROR;
	}
	resp_msg = _run_script(syscfg_path, argv, &status);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: syscfg (get cluster mode) status:%u response:%s",
		      __func__, status, resp_msg);
		error_code = SLURM_ERROR;
	}
	if (resp_msg == NULL) {
		info("%s: syscfg returned no information", __func__);
	} else {
		_log_script_argv(argv, resp_msg);
		if (strstr(active_features, "a2a"))
			key = "All2All";
		else if (strstr(active_features, "hemi"))
			key = "Hemisphere";
		else if (strstr(active_features, "quad"))
			key = "Quadrant";
		else if (strstr(active_features, "snc2"))
			key = "SNC-2";
		else if (strstr(active_features, "snc4"))
			key = "SNC-4";
		else
			key = NULL;
		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			numa_mode = _find_key_val(key, resp_msg);
			break;
		case KNL_SYSTEM_TYPE_DELL:
			numa_mode = xstrdup(key);
		default:
			break;
		}
		xfree(resp_msg);
	}

	/* Reset current Cluster/NUMA mode */
	if (numa_mode) {
		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			argv[0] = "syscfg";
			argv[1] = "/bcs";
			argv[2] = "";
			argv[3] = "BIOSSETTINGS";
			argv[4] = "Cluster Mode";
			argv[5] = numa_mode;
			argv[6] = NULL;
			break;
		case KNL_SYSTEM_TYPE_DELL:
			snprintf(tmp, sizeof(tmp),
				 "--SystemMemoryModel=%s", numa_mode);
			argv[0] = "syscfg";
			argv[1] = tmp;
			argv[2] = NULL;
			break;
		default:
			/* already handled above, should never get here */
			break;
		}
		resp_msg = _run_script(syscfg_path, argv, &status);
		if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
			error("%s: syscfg (set cluster mode) status:%u response:%s",
			      __func__, status, resp_msg);
			error_code = SLURM_ERROR;
		}  else {
			_log_script_argv(argv, resp_msg);
		}
		xfree(resp_msg);
		xfree(numa_mode);
	}

	/* Identify available Memory/MCDRAM modes */
	switch (knl_system_type) {
	case KNL_SYSTEM_TYPE_INTEL:
		argv[0] = "syscfg";
		argv[1] = "/d";
		argv[2] = "BIOSSETTINGS";
		argv[3] = "Memory Mode";
		argv[4] = NULL;
		break;
	case KNL_SYSTEM_TYPE_DELL:
		argv[0] = "syscfg";
		argv[1] = "--ProcEmbMemMode";
		argv[2] = NULL;
		break;
	default:
		/* already handled above, should never get here */
		break;
	}
	resp_msg = _run_script(syscfg_path, argv, &status);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: syscfg (get memory mode) status:%u response:%s",
		      __func__, status, resp_msg);
		error_code = SLURM_ERROR;
	}
	if (resp_msg == NULL) {
		info("%s: syscfg returned no information", __func__);
	} else {
		_log_script_argv(argv, resp_msg);
		if (strstr(active_features, "cache"))
			key = "Cache";
		else if (strstr(active_features, "flat"))
			switch (knl_system_type) {
			case KNL_SYSTEM_TYPE_INTEL:
				key = "Flat";
				break;
			case KNL_SYSTEM_TYPE_DELL:
				key = "Memory";
				break;
			default:
				key = NULL;
				break;
			}
		else if (strstr(active_features, "hybrid"))
			key = "Hybrid";
		else if (strstr(active_features, "equal"))
			key = "Equal";
		else if (strstr(active_features, "auto"))
			key = "Auto";
		else
			key = NULL;

		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			mcdram_mode = _find_key_val(key, resp_msg);
			break;
		case KNL_SYSTEM_TYPE_DELL:
			mcdram_mode = xstrdup(key);
		default:
			break;
		}
		xfree(resp_msg);
	}

	/* Reset current Memory/MCDRAM mode */
	if (mcdram_mode) {
		switch (knl_system_type) {
		case KNL_SYSTEM_TYPE_INTEL:
			argv[0] = "syscfg";
			argv[1] = "/bcs";
			argv[2] = "";
			argv[3] = "BIOSSETTINGS";
			argv[4] = "Memory Mode";
			argv[5] = mcdram_mode;
			argv[6] = NULL;
			break;
		case KNL_SYSTEM_TYPE_DELL:
			snprintf(tmp, sizeof(tmp),
				 "--ProcEmbMemMode=%s", mcdram_mode);
			argv[0] = "syscfg";
			argv[1] = tmp;
			argv[2] = NULL;
			break;
		default:
			/* already handled above, should never get here */
			break;
		}

		resp_msg = _run_script(syscfg_path, argv, &status);
		if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
			error("%s: syscfg (set memory mode) status:%u response:%s",
			      __func__, status, resp_msg);
			error_code = SLURM_ERROR;
		} else {
			_log_script_argv(argv, resp_msg);
		}
		xfree(resp_msg);
		xfree(mcdram_mode);
	}

	/* Clear features, do not pass as argument to reboot program
	 * (assuming we are calling /sbin/reboot). */
	active_features[0] = '\0';

	return error_code;
}

/* Return true if the plugin requires PowerSave mode for booting nodes */
extern bool node_features_p_node_power(void)
{
	return false;
}

/*
 * Note the active features associated with a set of nodes have been updated.
 * Specifically update the node's "hbm" GRES value as needed.
 * IN active_features - New active features
 * IN node_bitmap - bitmap of nodes changed
 * RET error code
 */
extern int node_features_p_node_update(char *active_features,
				       bitstr_t *node_bitmap)
{
	int i, i_first, i_last;
	int rc = SLURM_SUCCESS;
	uint16_t mcdram_inx;
	uint64_t mcdram_size;
	struct node_record *node_ptr;

	if (mcdram_per_node == NULL) {
//FIXME: Additional logic is needed to determine the available MCDRAM space
//FIXME: Additional logic will also be required to handle heterogeneous sizes
		mcdram_per_node = xmalloc(sizeof(uint64_t) * node_record_count);
		for (i = 0; i < node_record_count; i++)
			mcdram_per_node[i] = DEFAULT_MCDRAM_SIZE;
	}
	mcdram_inx = _knl_mcdram_parse(active_features, ",");
	if (mcdram_inx == 0)
		return rc;
	for (i = 0; i < KNL_MCDRAM_CNT; i++) {
		if ((KNL_CACHE << i) == mcdram_inx)
			break;
	}
	if ((i >= KNL_MCDRAM_CNT) || (mcdram_pct[i] == -1))
		return rc;
	mcdram_inx = i;

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
		mcdram_size = mcdram_per_node[i] *
			      (100 - mcdram_pct[mcdram_inx]) / 100;
		node_ptr = node_record_table_ptr + i;
		gres_plugin_node_feature(node_ptr->name, "hbm",
					 mcdram_size, &node_ptr->gres,
					 &node_ptr->gres_list);
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
extern bool node_features_p_changible_feature(char *feature)
{
	if (_knl_mcdram_token(feature) || _knl_numa_token(feature))
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
 * RET node's new merged features, must be xfreed
 */
extern char *node_features_p_node_xlate(char *new_features, char *orig_features,
					char *avail_features)
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

	if (new_features && *new_features) {
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
		int buf_len, fd, i, len;
		char buf[16];

		if (stat(ZONE_SORT_PATH, &sb) == -1)
			if (system(MODPROBE_PATH " zonesort_module")) {
				/*
				 * NOOP - compiling with optimizations throws
				 * out a (void) cast and warns about ignoring
				 * the return value
				 */
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
				// info("SORT NUMA %s", buf);
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
	static int reboot_allowed = -1;
	int i;

	if (reboot_allowed == -1) {
		char *reboot_program = slurm_get_reboot_program();
		if (reboot_program && reboot_program[0])
			reboot_allowed = 1;
		else
			reboot_allowed = 0;
		xfree(reboot_program);
	}

	if (reboot_allowed != 1) {
		info("Change in KNL mode not supported. No RebootProgram configured");
		return false;
	}

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
