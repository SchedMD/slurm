/*****************************************************************************\
 *  capmc_resume.c - Power up identified nodes with (optional) features.
 *  Once complete, modify the node's active features as needed.
 *
 *  Usage: "capmc_resume <hostlist> [features]"
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
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

#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAX_THREADS 256

/* Maximum poll wait time for child processes, in milliseconds */
#define MAX_POLL_WAIT 500

#define DEFAULT_CAPMC_TIMEOUT 10000	/* 10 seconds */
#define MIN_CAPMC_TIMEOUT 1000		/* 1 second */

/* Static variables */
static char *capmc_path = NULL;
static uint32_t capmc_timeout = DEFAULT_CAPMC_TIMEOUT;
static char *log_file = NULL;
static bitstr_t *node_bitmap = NULL;
static char *prog_name = NULL;
static char *mcdram_mode = NULL, *numa_mode = NULL;
static char *syscfg_path = NULL;

static pthread_mutex_t thread_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  thread_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int thread_cnt = 0;

static s_p_options_t knl_conf_file_options[] = {
	{"CapmcPath", S_P_STRING},
	{"CapmcTimeout", S_P_UINT32},
	{"DefaultNUMA", S_P_STRING},
	{"DefaultMCDRAM", S_P_STRING},
	{"LogFile", S_P_STRING},
	{"SyscfgPath", S_P_STRING},
	{NULL}
};

/* Static functions */
static s_p_hashtbl_t *_config_make_tbl(char *filename);
static uint32_t *_json_parse_nids(json_object *jobj, char *key, int *num);
static void *_node_update(void *args);
static void _read_config(void);
static char *_run_script(char **script_argv, int *status);
static int _tot_wait(struct timeval *start_time);

static s_p_hashtbl_t *_config_make_tbl(char *filename)
{
	s_p_hashtbl_t *tbl = NULL;

	xassert(filename);

	if (!(tbl = s_p_hashtbl_create(knl_conf_file_options))) {
		error("%s: s_p_hashtbl_create error: %s", log_file,
		      slurm_strerror(slurm_get_errno()));
		return tbl;
	}

	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		error("%s: s_p_parse_file error: %s", log_file,
		      slurm_strerror(slurm_get_errno()));
		s_p_hashtbl_destroy(tbl);
		tbl = NULL;
	}

	return tbl;
}

static void _read_config(void)
{
	char *knl_conf_file;
	s_p_hashtbl_t *tbl;

	capmc_timeout = DEFAULT_CAPMC_TIMEOUT;
	knl_conf_file = get_extra_conf_path("knl_cray.conf");
	if ((tbl = _config_make_tbl(knl_conf_file))) {
		(void) s_p_get_string(&capmc_path, "CapmcPath", tbl);
		(void) s_p_get_uint32(&capmc_timeout, "CapmcTimeout", tbl);
		(void) s_p_get_string(&log_file, "LogFile", tbl);
		(void) s_p_get_string(&syscfg_path, "SyscfgPath", tbl);
	}
	xfree(knl_conf_file);
	s_p_hashtbl_destroy(tbl);
	if (!capmc_path)
		capmc_path = xstrdup("/opt/cray/capmc/default/bin/capmc");
	capmc_timeout = MAX(capmc_timeout, MIN_CAPMC_TIMEOUT);
	if (!log_file)
		log_file = slurm_get_job_slurmctld_logfile();
}

/*
 * Return time in msec since "start time"
 */
static int _tot_wait(struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/* Run a script and return its stdout plus exit status */
static char *_run_script(char **script_argv, int *status)
{
	int cc, i, new_wait, resp_size = 0, resp_offset = 0;
	pid_t cpid;
	char *resp = NULL;
	int pfd[2] = { -1, -1 };

	if (access(capmc_path, R_OK | X_OK) < 0) {
		error("%s: Can not execute: %s", log_file, capmc_path);
		*status = 127;
		resp = xstrdup("Slurm node_features/knl_cray configuration error");
		return resp;
	}
	if (pipe(pfd) != 0) {
		error("%s: pipe(): %s", log_file,
		      slurm_strerror(slurm_get_errno()));
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
		error("%s: execv(): %s", log_file,
		      slurm_strerror(slurm_get_errno()));
		exit(127);
	} else if (cpid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		error("%s: fork(): %s", log_file,
		      slurm_strerror(slurm_get_errno()));
	} else {
		struct pollfd fds;
		struct timeval tstart;
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(pfd[1]);
		gettimeofday(&tstart, NULL);
		while (1) {
			fds.fd = pfd[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			new_wait = capmc_timeout - _tot_wait(&tstart);
			if (new_wait <= 0) {
				error("%s: poll() timeout @ %d msec", log_file,
				      capmc_timeout);
				break;
			}
			new_wait = MIN(new_wait, MAX_POLL_WAIT);
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				continue;
			} else if (i < 0) {
				error("%s: poll(): %s", log_file,
				      slurm_strerror(slurm_get_errno()));
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
				error("%s: read(): %s", log_file,
				      slurm_strerror(slurm_get_errno()));
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

static bool _check_node_state(int nid, char *nid_str, char *state)
{
	bool node_state_ok = false;
	char *argv[10], *resp_msg;
	int i, nid_cnt, status = 0;
	uint32_t *nid_array;
	json_object *j;

	argv[0] = "capmc";
	argv[1] = "node_status";
	argv[2] = "-n";
	argv[3] = nid_str;
	argv[4] = NULL;
	resp_msg = _run_script(argv, &status);
	if (status != 0) {
		error("%s: capmc(%s,%s,%s): %d %s", log_file,
			argv[1], argv[2], argv[3], status, resp_msg);
	}
	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("%s: json parser failed on %s", log_file, resp_msg);
		xfree(resp_msg);
		return node_state_ok;
	}
	xfree(resp_msg);

	nid_cnt = 0;
	nid_array = _json_parse_nids(j, "off", &nid_cnt);
	json_object_put(j);	/* Frees json memory */
	for (i = 0; i < nid_cnt; i++) {
		if (nid_array[i] == nid) {
			node_state_ok = true;
			break;
		}
	}
	xfree(nid_array);

	return node_state_ok;
}

static void *_node_update(void *args)
{
	char *node_name = (char *) args;
	char *argv[10], nid_str[32], *resp_msg;
	int i, nid = -1, status = 0;
	bool node_state_ok;

	for (i = 0; node_name[i]; i++) {
		if ((node_name[i] >= '0') && (node_name[i] <= '9')) {
			nid = strtol(node_name + i, NULL, 10);
			break;
		}
	}
	if (nid < 0) {
		error("%s: No valid NID: %s", log_file, node_name);
		goto fini;
	}
	snprintf(nid_str, sizeof(nid_str), "%d", nid);

	if (mcdram_mode) {
		/* Update MCDRAM mode.
		* Example: "capmc set_mcdram_cfg –n 43 –m cache" */
		argv[0] = "capmc";
		argv[1] = "set_mcdram_cfg";
		argv[2] = "-m";
		argv[3] = mcdram_mode;
		argv[4] = "-n";
		argv[5] = nid_str;
		argv[6] = NULL;
		resp_msg = _run_script(argv, &status);
		if (status != 0) {
			error("%s: capmc(%s,%s,%s,%s,%s): %d %s", log_file,
			      argv[1], argv[2], argv[3], argv[4], argv[5],
			      status, resp_msg);
		}
		xfree(resp_msg);
	}

	if (numa_mode) {
		/* Update NUMA mode.
		 * Example: "capmc set_numa_cfg –m a2a –n 43" */
		argv[0] = "capmc";
		argv[1] = "set_numa_cfg";
		argv[2] = "-m";
		argv[3] = numa_mode;
		argv[4] = "-n";
		argv[5] = nid_str;
		argv[6] = NULL;
		resp_msg = _run_script(argv, &status);
		if (status != 0) {
			error("%s: capmc(%s,%s,%s,%s,%s): %d %s", log_file,
			      argv[1], argv[2], argv[3], argv[4], argv[5],
			      status, resp_msg);
		}
		xfree(resp_msg);
	}

	/* Test if already in "off" state */
	node_state_ok = _check_node_state(nid, nid_str, "off");

	/* Request node power down.
	 * Example: "capmc node_off –n 43" */
	if (!node_state_ok) {
		argv[0] = "capmc";
		argv[1] = "node_off";
		argv[2] = "-n";
		argv[3] = nid_str;
		argv[4] = NULL;
		resp_msg = _run_script(argv, &status);
		if (status != 0) {
			error("%s: capmc(%s,%s,%s): %d %s", log_file,
			      argv[1], argv[2], argv[3], status, resp_msg);
		}
		xfree(resp_msg);
	}

	/* Wait for node in "off" state */
	while (!node_state_ok) {
		sleep(2);
		node_state_ok = _check_node_state(nid, nid_str, "off");
	}

	/* Request node power up.
	 * Example: "capmc node_on –n 43" */
	argv[0] = "capmc";
	argv[1] = "node_on";
	argv[2] = "-n";
	argv[3] = nid_str;
	argv[4] = NULL;
	resp_msg = _run_script(argv, &status);
	if (status != 0) {
		error("%s: capmc(%s,%s,%s): %d %s", log_file,
			argv[1], argv[2], argv[3], status, resp_msg);
	}
	xfree(resp_msg);

fini:	slurm_mutex_lock(&thread_cnt_mutex);
	thread_cnt--;
	pthread_cond_signal(&thread_cnt_cond);
	slurm_mutex_unlock(&thread_cnt_mutex);
	return NULL;
}

static uint32_t *_json_parse_nids(json_object *jobj, char *key, int *num)
{
	json_object *j_array = NULL;
	json_object *j_value = NULL;
	enum json_type j_type;
	uint32_t *ents;
	int i, cnt;

	*num = 0;
        json_object_object_get_ex(jobj, key, &j_array);
	if (!j_array) {
		error("%s: Unable to parse nid specification", log_file);
		return NULL;
	}

	cnt = json_object_array_length(j_array);
	ents = xmalloc(sizeof(uint32_t) * cnt);
	for (i = 0; i < cnt; i++) {
		j_value = json_object_array_get_idx(j_array, i);
		j_type = json_object_get_type(j_value);
		if (j_type != json_type_int) {
			error("%s: Unable to parse nid specification",
			      log_file);
			break;
		} else {
			ents[i] = (uint32_t) json_object_get_int64(j_value);
			*num = i + 1;
		}
	}
	return ents;
}

/* Wait for all identified computed nodes to enter "on" state */
static void _wait_all_nodes_on(void)
{
	char *argv[10], *resp_msg;
	int i, nid_cnt = 0, status = 0;
	json_object *j;
	uint32_t *nid_array;
	time_t start_time = time(NULL);

	while ((difftime(time(NULL), start_time) < (30 * 60)) &&
	       (bit_set_count(node_bitmap) > 0)) {
		sleep(20);
		argv[0] = "capmc";
		argv[1] = "node_status";
		argv[2] = NULL;
		resp_msg = _run_script(argv, &status);
		if (status != 0) {
			error("%s: capmc(%s,%s,%s): %d %s", log_file,
				argv[1], argv[2], argv[3], status, resp_msg);
			break;
		}
		j = json_tokener_parse(resp_msg);
		if (j == NULL) {
			error("%s: json parser failed on %s",
			      log_file, resp_msg);
			xfree(resp_msg);
			break;
		}
		xfree(resp_msg);
		nid_cnt = 0;
		nid_array = _json_parse_nids(j, "on", &nid_cnt);
		json_object_put(j);	/* Frees json memory */
		for (i = 0; i < nid_cnt; i++) {
			bit_clear(node_bitmap, nid_array[i]);
		}
		xfree(nid_array);
	}
}

int main(int argc, char *argv[])
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	char *features, *save_ptr = NULL, *tok;
	update_node_msg_t node_msg;
	int rc =  SLURM_SUCCESS;
	hostlist_t hl = NULL;
	char *node_name;
	pthread_attr_t attr_work;
	pthread_t thread_work = 0;

	prog_name = argv[0];
	_read_config();
	log_opts.stderr_level = LOG_LEVEL_QUIET;
	log_opts.syslog_level = LOG_LEVEL_QUIET;
	if (slurm_get_debug_flags() && DEBUG_FLAG_NODE_FEATURES)
		log_opts.logfile_level += 3;
	(void) log_init(argv[0], log_opts, LOG_DAEMON, log_file);

	/* Parse the MCDRAM and NUMA boot options */
	if (argc == 3) {
		features = xstrdup(argv[2]);
		tok = strtok_r(features, ",", &save_ptr);
		while (tok) {
			printf("%s\n", tok);
			if (!strcasecmp(tok, "a2a")  ||
			    !strcasecmp(tok, "hemi") ||
			    !strcasecmp(tok, "quad") ||
			    !strcasecmp(tok, "snc2") ||
			    !strcasecmp(tok, "snc4")) {
				xfree(numa_mode);
				numa_mode = xstrdup(tok);
			} else if (!strcasecmp(tok, "cache")  ||
				   !strcasecmp(tok, "equal") ||
				   !strcasecmp(tok, "flat")) {
				xfree(mcdram_mode);
				mcdram_mode = xstrdup(tok);
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(features);
	}

	/* Spawn threads to change MCDRAM and NUMA states and start node
	 * reboot process */
	if ((hl = hostlist_create(argv[1])) == NULL) {
		error("%s: Invalid hostlist (%s)", prog_name, argv[1]);
		exit(2);
	}
	node_bitmap = bit_alloc(100000);
	while ((node_name = hostlist_pop(hl))) {
		slurm_mutex_lock(&thread_cnt_mutex);
		while (1) {
			if (thread_cnt <= MAX_THREADS) {
				thread_cnt++;
				break;
			} else {	/* wait for state change and retry */
				pthread_cond_wait(&thread_cnt_cond,
						  &thread_cnt_mutex);
			}
		}
		slurm_mutex_unlock(&thread_cnt_mutex);

		slurm_attr_init(&attr_work);
		(void) pthread_attr_setdetachstate
			(&attr_work, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thread_work, &attr_work, _node_update,
				   (void *) node_name)) {
			_node_update((void *) node_name);
		}
		slurm_attr_destroy(&attr_work);
	}

	/* Wait for work threads to complete */
	slurm_mutex_lock(&thread_cnt_mutex);
	while (1) {
		if (thread_cnt == 0)
			break;
		else	/* wait for state change and retry */
			pthread_cond_wait(&thread_cnt_cond, &thread_cnt_mutex);
	}
	slurm_mutex_unlock(&thread_cnt_mutex);
	hostlist_destroy(hl);
	xfree(mcdram_mode);
	xfree(numa_mode);

	/* Wait for all nodes to change state to "on" */
	_wait_all_nodes_on();

	if ((argc == 3) && !syscfg_path) {
		slurm_init_update_node_msg(&node_msg);
		node_msg.node_names = argv[1];
		node_msg.features_act = argv[2];
		rc = slurm_update_node(&node_msg);
	}

	if (rc == SLURM_SUCCESS) {
		exit(0);
	} else {
		error("%s: slurm_update_node(\'%s\', \'%s\'): %s\n",
		      prog_name, argv[1], argv[2],
		      slurm_strerror(slurm_get_errno()));
		exit(1);
	}
}
