/*****************************************************************************\
 *  node_features_knl_cray.c - Plugin for managing a Cray KNL state information
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <ctype.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include "slurm/slurm.h"

#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
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

/* Maximum poll wait time for child processes, in milliseconds */
#define MAX_POLL_WAIT 500

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
const char plugin_name[]        = "node_features knl_cray plugin";
const char plugin_type[]        = "node_features/knl_cray";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static char *capmc_path = NULL;
static int   capmc_timeout = 0;		/* capmc command timeout in msec */
static bool  debug_flag = false;

typedef struct mcdram_cap {
	uint32_t nid;
	char *mcdram_cfg;
} mcdram_cap_t;

typedef struct mcdram_cfg {
	uint32_t nid;
	char *mcdram_cfg;
} mcdram_cfg_t;

static void _free_script_argv(char **script_argv);
static mcdram_cap_t *_json_parse_mcdram_cap_array(json_object *jobj, char *key,
						  int *num);
static void _json_parse_mcdram_cap_object(json_object *jobj,
					  mcdram_cap_t *ent);
static void _log_script_argv(char **script_argv, char *resp_msg);
static void _mcdram_cap_free(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt);
static void _mcdram_cap_log(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt);
static char *_run_script(char **script_argv, int *status);
static int  _tot_wait (struct timeval *start_time);

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

static void _json_parse_mcdram_cap_object(json_object *jobj,
					  mcdram_cap_t *ent)
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
			if (strcmp(iter.key, "nid") == 0) {
				ent->nid = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (strcmp(iter.key, "mcdram_cfg") == 0) {
				ent->mcdram_cfg = xstrdup(p);
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

	for (i = 0; i < mcdram_cap_cnt; i++) {
		xfree(mcdram_cap[i].mcdram_cfg);
	}
	xfree(mcdram_cap);
}

static void _mcdram_cap_log(mcdram_cap_t *mcdram_cap, int mcdram_cap_cnt)
{
	int i;

	for (i = 0; i < mcdram_cap_cnt; i++) {
		info("MCDRAM_CAP[%d]: nid:%u mcdram_cfg:%s",
		     i, mcdram_cap[i].nid, mcdram_cap[i].mcdram_cfg);
	}
}

/* Run a script and return its stdout plus exit status */
static char *_run_script(char **script_argv, int *status)
{
	int cc, i, new_wait, resp_size = 0, resp_offset = 0;
	pid_t cpid;
	char *resp = NULL;
	int pfd[2] = { -1, -1 };

	if (access(capmc_path, R_OK | X_OK) < 0) {
		error("%s: %s can not be executed: %m", __func__, capmc_path);
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
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execv(capmc_path, script_argv);
		error("%s: execv(%s): %m", __func__, capmc_path);
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
				error("%s: read(%s): %m", __func__, capmc_path);
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

/* Load configuration */
extern int init(void)
{
//FIXME
capmc_path = xstrdup("/home/jette/Desktop/SLURM/install.linux/sbin/capmc");
capmc_timeout = 1000;	/* MSEC */
	if (slurm_get_debug_flags() & DEBUG_FLAG_NODE_FEATURES)
		debug_flag = true;

	return SLURM_SUCCESS;
}

/* Release allocated memory */
extern int fini(void)
{
	xfree(capmc_path);
	capmc_timeout = 0;
	debug_flag = false;
	return SLURM_SUCCESS;
}

/* Reload configuration */
extern int node_features_p_reconfig(void)
{
	(void)fini();
	(void)init();	/* SAFE?? ADD LOCKS? */
	return SLURM_SUCCESS;
}

extern int node_features_p_get_node(char *node_list)
{
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
	char *resp_msg;
	char **script_argv;
	mcdram_cap_t *mcdram_cap;
	int mcdram_cap_cnt = 0;

	script_argv = xmalloc(sizeof(char *) * 10);	/* NULL terminated */
	script_argv[0] = xstrdup("capmc");
	script_argv[1] = xstrdup("get_mcdram_capabilities");

	START_TIMER;
	resp_msg = _run_script(script_argv, &status);
	END_TIMER;
	if (debug_flag) {
		info("%s: get_mcdram_capabilities ran for %s",
		     __func__, TIME_STR);
	}
	_log_script_argv(script_argv, resp_msg);
	_free_script_argv(script_argv);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: get_mcdram_capabilities status:%u response:%s",
		      __func__, status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s: get_mcdram_capabilities returned no information",
		     __func__);
		_free_script_argv(script_argv);
		return SLURM_ERROR;
	}

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, resp_msg);
		xfree(resp_msg);
		return SLURM_ERROR;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (strcmp(iter.key, "nids"))
			continue;
		mcdram_cap = _json_parse_mcdram_cap_array(j, iter.key,
							  &mcdram_cap_cnt);
		break;
	}
	json_object_put(j);	/* Frees json memory */
	if (debug_flag)
		_mcdram_cap_log(mcdram_cap, mcdram_cap_cnt);

	_mcdram_cap_free(mcdram_cap, mcdram_cap_cnt);

	return SLURM_SUCCESS;
}
