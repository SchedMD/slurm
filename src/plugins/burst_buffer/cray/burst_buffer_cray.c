/*****************************************************************************\
 *  burst_buffer_cray.c - Plugin for managing a Cray burst_buffer
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
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

#if HAVE_JSON
#  include <json-c/json.h>
#endif

//FIXME: Test in "configure" to find the header is required
#if HAVE_DW_WLM_LIB_H
#  include <dw_wlm_lib.h>
#endif

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
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
 * the plugin (e.g., "burst_buffer" for SLURM burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "burst_buffer cray plugin";
const char plugin_type[]        = "burst_buffer/cray";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins */
static bb_state_t 	bb_state;
static char *		state_save_loc = NULL;

/* Description of each Cray bb entry
 */
typedef struct bb_entry {
	char *id;
	char *units;
	uint64_t granularity;
	uint64_t quantity;
	uint64_t free;
	uint64_t gb_granularity;
	uint64_t gb_quantity;
	uint64_t gb_free;
} bb_entry_t;

typedef struct {
	uint32_t job_id;
	uint32_t timeout;
	char **args1;
	char **args2;
}
stage_args_t;

typedef struct {		/* Used for scheduling */
	char *   name;		/* BB GRES name, e.g. "nodes" */
	uint64_t add_cnt;	/* Additional GRES required */
	uint64_t avail_cnt;	/* Additional GRES available */
} needed_gres_t;

static int	_alloc_job_bb(struct job_record *job_ptr, bb_job_t *bb_spec);
static void *	_bb_agent(void *args);
static bb_entry_t *
		_bb_entry_get(int *num_ent, bb_state_t *state_ptr);
static void	_bb_free_entry(struct bb_entry *ents, int num_ent);
static int	_build_bb_script(struct job_record *job_ptr, char *script_file);
static void	_del_bb_spec(bb_job_t *bb_spec);
static bb_job_t *
		_get_bb_spec(struct job_record *job_ptr);
static void	_job_queue_del(void *x);
static struct bb_entry *
		_json_parse_array(json_object *jobj, char *key, int *num);
static void	_json_parse_object(json_object *jobj, struct bb_entry *ent);
static void	_log_bb_spec(bb_job_t *bb_spec);
static void	_load_state(void);
static int	_parse_bb_opts(struct job_descriptor *job_desc);
static int	_parse_interactive(struct job_descriptor *job_desc);
static void	_purge_bb_files(struct job_record *job_ptr);
static int	_queue_stage_in(struct job_record *job_ptr);
static int	_queue_stage_out(struct job_record *job_ptr);
static void	_queue_teardown(uint32_t job_id, bool hurry);
static char *	_set_cmd_path(char *cmd);
static void *	_start_stage_in(void *x);
static void *	_start_stage_out(void *x);
static void *	_start_teardown(void *x);
static void	_test_config(void);
static int	_test_size_limit(struct job_record *job_ptr, bb_job_t *bb_spec);
static void	_timeout_bb_rec(void);
static int	_write_file(char *file_name, char *buf);
static int	_write_nid_file(char *file_name, char *node_list,
				uint32_t job_id);

static void _job_queue_del(void *x)
{
	job_queue_rec_t *job_rec = (job_queue_rec_t *) x;
	if (job_rec) {
		_del_bb_spec(job_rec->bb_spec);
		xfree(job_rec);
	}
}

/* Purge files we have created for the job.
 * bb_state.bb_mutex is locked on function entry. */
static void _purge_bb_files(struct job_record *job_ptr)
{
	char *hash_dir = NULL, *job_dir = NULL;
	char *script_file = NULL, *setup_env_file = NULL;
	char *data_in_env_file = NULL, *pre_run_env_file = NULL;
	char *post_run_env_file = NULL, *data_out_env_file = NULL;
	char *teardown_env_file = NULL, *client_nids_file = NULL;
	int hash_inx;

	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(hash_dir, "%s/hash.%d", state_save_loc, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
	(void) mkdir(job_dir, 0700);
	xstrfmtcat(setup_env_file, "%s/setup_env", job_dir);
	xstrfmtcat(data_in_env_file, "%s/data_in_env", job_dir);
	xstrfmtcat(pre_run_env_file, "%s/pre_run_env", job_dir);
	xstrfmtcat(post_run_env_file, "%s/post_run_env", job_dir);
	xstrfmtcat(data_out_env_file, "%s/data_out_env", job_dir);
	xstrfmtcat(teardown_env_file, "%s/teardown_env", job_dir);
	xstrfmtcat(client_nids_file, "%s/client_nids", job_dir);

	(void) unlink(setup_env_file);
	(void) unlink(data_in_env_file);
	(void) unlink(pre_run_env_file);
	(void) unlink(post_run_env_file);
	(void) unlink(data_out_env_file);
	(void) unlink(teardown_env_file);
	(void) unlink(client_nids_file);
	if (job_ptr->batch_flag == 0) {
		xstrfmtcat(script_file, "%s/script", job_dir);
		(void) unlink(script_file);
		(void) unlink(job_dir);
	}

	xfree(hash_dir);
	xfree(job_dir);
	xfree(script_file);
	xfree(setup_env_file);
	xfree(data_in_env_file);
	xfree(pre_run_env_file);
	xfree(post_run_env_file);
	xfree(data_out_env_file);
	xfree(teardown_env_file);
	xfree(client_nids_file);
}

/* Validate that our configuration is valid for this plugin type */
static void _test_config(void)
{
	if (!bb_state.bb_config.get_sys_state)
		fatal("%s: GetSysState is NULL", __func__);
}

/* Return the path to the specified command using the configured "get_sys_state"
 * path as a base.
 * xfree the return value */
static char *_set_cmd_path(char *cmd)
{
	char *tmp, *sep;

	tmp = xstrdup(bb_state.bb_config.get_sys_state);
	sep = strrchr(tmp, '/');
	if (sep)
		sep[1] = '\0';
	else
		tmp[0] = '\0';
	xstrcat(tmp, cmd);

	return tmp;
}

static int _alloc_job_bb(struct job_record *job_ptr, bb_job_t *bb_spec)
{
	bb_alloc_t *bb_ptr;
	char jobid_buf[32];
	int rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: start stage-in %s", __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}
	bb_ptr = bb_alloc_job(&bb_state, job_ptr, bb_spec);
	bb_ptr->state = BB_STATE_STAGING_IN;
	bb_ptr->state_time = time(NULL);
	rc = _queue_stage_in(job_ptr);
	if (rc != SLURM_SUCCESS) {
		bb_ptr->state = BB_STATE_TEARDOWN;
		bb_ptr->state_time = time(NULL);
		_queue_teardown(job_ptr->job_id, true);
	}

	return rc;
}

/* Perform periodic background activities */
static void *_bb_agent(void *args)
{
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	while (!bb_state.term_flag) {
		bb_sleep(&bb_state, AGENT_INTERVAL);
		if (bb_state.term_flag)
			break;
		lock_slurmctld(job_write_lock);
		pthread_mutex_lock(&bb_state.bb_mutex);
		_load_state();
		_timeout_bb_rec();
		pthread_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	}
	return NULL;
}

static void _del_bb_spec(bb_job_t *bb_spec)
{
	int i;

	if (bb_spec) {
		for (i = 0; i < bb_spec->gres_cnt; i++)
			xfree(bb_spec->gres_ptr[i].name);
		xfree(bb_spec->gres_ptr);
		xfree(bb_spec);
	}
}

static void _log_bb_spec(bb_job_t *bb_spec)
{
	char *out_buf = NULL;
	int i;

	if (bb_spec) {
		xstrfmtcat(out_buf, "%s: ", plugin_type);
		for (i = 0; i < bb_spec->gres_cnt; i++) {
			xstrfmtcat(out_buf, "Gres[%d]:%s:%"PRIu64" ",
				   i, bb_spec->gres_ptr[i].name,
				   bb_spec->gres_ptr[i].count);
		}
		xstrfmtcat(out_buf, "Swap:%ux%u ", bb_spec->swap_size,
			   bb_spec->swap_nodes);
		xstrfmtcat(out_buf, "TotalSize:%"PRIu64"", bb_spec->total_size);
		verbose("%s", out_buf);
		xfree(out_buf);
	}
}

/* Return the burst buffer size specification of a job
 * RET size data structure or NULL of none found
 * NOTE: delete return value using _del_bb_size() */
static bb_job_t *_get_bb_spec(struct job_record *job_ptr)
{
	char *end_ptr = NULL, *sep, *tok, *tmp;
	bool have_bb = false;
	int inx;
	bb_job_t *bb_spec;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return NULL;

	bb_spec = xmalloc(sizeof(bb_job_t));
	tok = strstr(job_ptr->burst_buffer, "SLURM_SIZE=");
	if (tok) {	/* Format: "SLURM_SIZE=%u" */
		tok += 11;
		bb_spec->total_size = bb_get_size_num(tok,
					bb_state.bb_config.granularity);
		if (bb_spec->total_size)
			have_bb = true;
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_SWAP=");
	if (tok) {	/* Format: "SLURM_SWAP=%uGB(%uNodes)" */
		tok += 11;
		bb_spec->swap_size = strtol(tok, &end_ptr, 10);
		if (bb_spec->swap_size)
			have_bb = true;
		if ((end_ptr[0] == 'G') && (end_ptr[1] == 'B') &&
		    (end_ptr[2] == '(')) {
			bb_spec->swap_nodes = strtol(end_ptr + 3, NULL, 10);
		} else {
			bb_spec->swap_nodes = 1;
		}
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_GRES=");
	if (tok) {	/* Format: "SLURM_GRES=nodes:%u" */
		tmp = xstrdup(tok + 11);
		tok = strtok_r(tmp, ",", &end_ptr);
		while (tok) {
			have_bb = true;
			inx = bb_spec->gres_cnt;
			bb_spec->gres_cnt++;
			bb_spec->gres_ptr = xrealloc(bb_spec->gres_ptr,
						     sizeof(bb_gres_t) *
						     bb_spec->gres_cnt);
			sep = strchr(tok, ':');
			if (sep) {
				sep[0] = '\0';
				bb_spec->gres_ptr[inx].count =
						strtol(sep + 1, NULL, 10);
			} else {
				bb_spec->gres_ptr[inx].count = 1;
			}
			bb_spec->gres_ptr[inx].name = xstrdup(tok);
			tok = strtok_r(NULL, ",", &end_ptr);
		}
		xfree(tmp);
	}
	if (!have_bb)
		xfree(bb_spec);

	if (bb_state.bb_config.debug_flag)
		_log_bb_spec(bb_spec);
	return bb_spec;
}

/* Determine if a job contains a burst buffer specification.
 * Fast variant of _get_bb_spec() function above, tests for any non-zero value.
 * RETURN true if job contains a burst buffer specification. */
static bool _test_bb_spec(struct job_record *job_ptr)
{
	char *end_ptr = NULL, *sep, *tok, *tmp;
	bool have_bb = false;
	int val;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return false;

	tok = strstr(job_ptr->burst_buffer, "SLURM_SIZE=");
	if (tok) {	/* Format: "SLURM_SIZE=%u" */
		tok += 11;
		val = strtol(tok, &end_ptr, 10);
		if (val)
			return true;
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_SWAP=");
	if (tok) {	/* Format: "SLURM_SWAP=%uGB(%uNodes)" */
		tok += 11;
		val = strtol(tok, &end_ptr, 10);
		if (val)
			return true;
	}

	tok = strstr(job_ptr->burst_buffer, "SLURM_GRES=");
	if (tok) {	/* Format: "SLURM_GRES=nodes:%u" */
		tmp = xstrdup(tok + 11);
		tok = strtok_r(tmp, ",", &end_ptr);
		while (tok && !have_bb) {
			sep = strchr(tok, ':');
			if (sep) {
				sep[0] = '\0';
				val = strtol(sep + 1, NULL, 10);
				if (val)
					have_bb = true;
			} else {
				have_bb = true;
			}
			tok = strtok_r(NULL, ",", &end_ptr);
		}
		xfree(tmp);
	}

	return have_bb;
}

/*
 * Determine the current actual burst buffer state.
 * Run the program "get_sys_state" and parse stdout for details.
 * job_id IN - specific job to get information about, or 0 for all jobs
 */
static void _load_state(void)
{
	burst_buffer_gres_t *gres_ptr;
	bb_entry_t *ents;
	int num_ents = 0;
	int i;
static bool first_load = true;
//FIXME: Need logic to handle resource allocation/free in progress
if (!first_load) return;
first_load = false;

	bb_state.last_load_time = time(NULL);
	ents = _bb_entry_get(&num_ents, &bb_state);
	if (ents == NULL) {
		error("%s: failed to be burst buffer entries, what now?",
		      __func__);
		return;
	}

	for (i = 0; i < num_ents; i++) {
		/* ID: "bytes" */
		if (strcmp(ents[i].id, "bytes") == 0) {
			bb_state.bb_config.granularity
				= ents[i].gb_granularity;
			bb_state.total_space
				= ents[i].gb_quantity;
			bb_state.used_space
				= ents[i].gb_quantity - ents[i].gb_free;
			xassert(bb_state.used_space >= 0);

			/* Everything else is a burst buffer
			 * generic resource (gres)
			 */
			bb_state.bb_config.gres_cnt = 0;
			continue;
		}

		bb_state.bb_config.gres_ptr
			= xrealloc(bb_state.bb_config.gres_ptr,
				   sizeof(burst_buffer_gres_t) *
				   (bb_state.bb_config.gres_cnt + 1));
		gres_ptr = bb_state.bb_config.gres_ptr +
			   bb_state.bb_config.gres_cnt;
		bb_state.bb_config.gres_cnt++;
		gres_ptr->avail_cnt = ents[i].quantity;
		gres_ptr->granularity = ents[i].gb_granularity;
		gres_ptr->name = xstrdup(ents[i].id);
		gres_ptr->used_cnt = ents[i].gb_quantity - ents[i].gb_free;
	}
	_bb_free_entry(ents, num_ents);
}

/* Write an string representing the NIDs of a job's nodes to an arbitrary
 * file location
 * RET 0 or Slurm error code
 */
static int _write_nid_file(char *file_name, char *node_list, uint32_t job_id)
{
	char *tmp, *sep, *tok, *save_ptr = NULL, *buf = NULL;
	int i, rc;

	tmp = xstrdup(node_list);
	sep = strrchr(tmp, ']');
	if (sep)
		sep[0] = '\0';
	sep = strchr(tmp, '[');
	if (sep) {
		sep++;
	} else {
		sep = tmp;
		for (i = 0; !isdigit(sep[0]) && sep[0]; i++)
			sep++;
	}
	tok = strtok_r(sep, ",", &save_ptr);
	while (tok) {
		xstrfmtcat(buf, "%s\n", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	if (buf) {
		rc = _write_file(file_name, buf);
		xfree(buf);
	} else {
		error("%s: job %u has node list without numeric component (%s)",
		      __func__, job_id, node_list);
		rc = EINVAL;
	}
	return rc;
}

/* Write an arbitrary string to an arbitrary file name */
static int _write_file(char *file_name, char *buf)
{
	int amount, fd, nwrite, pos;

	(void) unlink(file_name);
	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return errno;
	}

	if (!buf) {
		error("%s: buf is NULL", __func__);
		return SLURM_ERROR;
	}

	nwrite = strlen(buf) + 1;
	pos = 0;
	while (nwrite > 0) {
		amount = write(fd, &buf[pos], nwrite);
		if ((amount < 0) && (errno != EINTR)) {
			error("Error writing file %s, %m", file_name);
			close(fd);
			return ESLURM_WRITING_TO_FILE;
		}
		nwrite -= amount;
		pos    += amount;
	}

	(void) close(fd);
	return SLURM_SUCCESS;
}

static int _queue_stage_in(struct job_record *job_ptr)
{
	char *capacity = NULL, *hash_dir = NULL, *job_dir = NULL;
	char *client_nodes_file_nid = NULL;
	char *tok, **setup_argv, **data_in_argv;
	stage_args_t *stage_args;
	int hash_inx = job_ptr->job_id % 10;
	uint32_t tmp32;
	uint64_t tmp64;
	pthread_attr_t stage_attr;
	pthread_t stage_tid = 0;
	int rc = SLURM_SUCCESS;
	char jobid_buf[32];

	if (job_ptr->burst_buffer) {
		tok = strstr(job_ptr->burst_buffer, "SLURM_SIZE=");
		if (tok) {
			tmp64 = strtoll(tok + 11, NULL, 10);
			xstrfmtcat(capacity, "bytes:%"PRIu64"GB", tmp64);
		} else {
			tok = strstr(job_ptr->burst_buffer, "SLURM_GRES=");
			if (tok) {
				tok = strstr(tok, "nodes:");
				if (tok) {
					tmp32 = strtoll(tok + 6, NULL, 10);
					xstrfmtcat(capacity, "nodes:%u", tmp32);
				}
			}
		}
	}
	if (!capacity) {
		error("%s: %s has invalid burst buffer spec(%s)", __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
		     job_ptr->burst_buffer);
		return SLURM_ERROR;
	}

	xstrfmtcat(hash_dir, "%s/hash.%d", state_save_loc, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
	if (job_ptr->sched_nodes) {
		xstrfmtcat(client_nodes_file_nid, "%s/client_nids", job_dir);
		if (_write_nid_file(client_nodes_file_nid,
				    job_ptr->sched_nodes, job_ptr->job_id))
			xfree(client_nodes_file_nid);
	}
	setup_argv = xmalloc(sizeof(char *) * 10);
	setup_argv[0] = xstrdup("dws_setup");
	xstrfmtcat(setup_argv[1], "--token=%u", job_ptr->job_id);
	xstrfmtcat(setup_argv[2], "--caller=%s", "SLURM");
	xstrfmtcat(setup_argv[3], "--owner=%d", job_ptr->user_id);
	xstrfmtcat(setup_argv[4], "--capacity=%s", capacity);
	xstrfmtcat(setup_argv[5], "--job-environment-file=%s/setup_env",
		   job_dir);
	if (client_nodes_file_nid) {
		xstrfmtcat(setup_argv[6], "--client-nodes-file-nids=%s",
			   client_nodes_file_nid);
	}

	data_in_argv = xmalloc(sizeof(char *) * 10);
	data_in_argv[0] = xstrdup("dws_data_in");
	xstrfmtcat(data_in_argv[1], "--job-environment-file=%s/data_in_env",
		   job_dir);

	stage_args = xmalloc(sizeof(stage_args_t));
	stage_args->job_id  = job_ptr->job_id;
	stage_args->timeout = bb_state.bb_config.stage_in_timeout;
	stage_args->args1   = setup_argv;
	stage_args->args2   = data_in_argv;

	slurm_attr_init(&stage_attr);
	if (pthread_attr_setdetachstate(&stage_attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	while (pthread_create(&stage_tid, &stage_attr, _start_stage_in,
			      stage_args)) {
		if (errno != EAGAIN) {
			error("%s: pthread_create: %m", __func__);
			_start_stage_in(stage_args);	/* Do in-line */
			break;
		}
		usleep(100000);
	}
	slurm_attr_destroy(&stage_attr);

	xfree(capacity);
	xfree(hash_dir);
	xfree(job_dir);
	xfree(client_nodes_file_nid);
	return rc;
}

static void *_start_stage_in(void *x)
{
	stage_args_t *stage_args;
	char *dws_setup_path, *dws_data_in_path;
	char **setup_argv, **data_in_argv, *resp_msg = NULL;
	int i, rc = SLURM_SUCCESS, status = 0, timeout;
	slurmctld_lock_t job_write_lock =
		    { NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	struct job_record *job_ptr;
	bb_alloc_t *bb_ptr;
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *error_buf = NULL, output_buf = NULL;
	int status = 0;
#endif

	stage_args = (stage_args_t *) x;
	setup_argv   = stage_args->args1;
	data_in_argv = stage_args->args2;

#if HAVE_DW_WLM_LIB_H
	START_TIMER;
	status = dw_wlm_lib_setup(&output_buf, &error_buf,
		const char *token,
		const char *caller,
		int  owner,
		const char *capacity,
		const char *client_nodes_filename,
		const char *client_nodes_filename_nids,
		const char *job_env_filename);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_setup for job %u: %s",
		      __func__, teardown_args->job_id, error_buf);
		free(error_buf);
	} else if (bb_state.bb_config.debug_flag) {
		info("%s: dw_wlm_lib_setup for job %u ran for %s: %s",
		     __func__, teardown_args->job_id, TIME_STR, output_buf);
	}
#else
	if (stage_args->timeout)
		timeout = stage_args->timeout * 1000;
	else
		timeout = 5000;
	dws_setup_path = _set_cmd_path("dws_setup");
	START_TIMER;
	resp_msg = bb_run_script("dws_setup", dws_setup_path,
				 setup_argv, timeout, &status);
	END_TIMER;
	if (DELTA_TIMER > 500000) {	/* 0.5 secs */
		info("%s: dws_setup for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	} else if (bb_state.bb_config.debug_flag) {
		debug("%s: dws_setup for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	}
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: dws_setup for job %u status:%u response:%s",
		      __func__, stage_args->job_id, status, resp_msg);
		rc = SLURM_ERROR;
	}
	xfree(resp_msg);
#endif

	
#if HAVE_DW_WLM_LIB_H
	START_TIMER;
	status = dw_wlm_lib_data_in(&output_buf, &error_buf,
		const char *job_env_filename);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_data_in for job %u: %s",
		      __func__, teardown_args->job_id, error_buf);
		free(error_buf);
	} else if (bb_state.bb_config.debug_flag) {
		info("%s: dw_wlm_lib_data_in for job %u ran for %s: %s",
		     __func__, teardown_args->job_id, TIME_STR, output_buf);
	}
#else
	dws_data_in_path = _set_cmd_path("dws_data_in");
	if (stage_args->timeout)
		timeout = stage_args->timeout * 1000;
	else
		timeout = 24 * 60 * 60 * 1000;	/* One day */
	START_TIMER;
	resp_msg = bb_run_script("dws_data_in", dws_data_in_path,
				 data_in_argv, timeout, &status);
	END_TIMER;
	if (DELTA_TIMER > 5000000) {	/* 5 secs */
		info("%s: dws_data_in for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	} else if (bb_state.bb_config.debug_flag) {
		debug("%s: dws_data_in for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	}
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: dws_data_in for job %u status:%u response:%s",
		      __func__, stage_args->job_id, status, resp_msg);
		rc = SLURM_ERROR;
	}
	xfree(resp_msg);
#endif

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_args->job_id);
	if (!job_ptr) {
		error("%s: unable to find job record for job %u",
		      __func__, stage_args->job_id);
	} else if (rc == SLURM_SUCCESS) {
		pthread_mutex_lock(&bb_state.bb_mutex);
		bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
		if (bb_ptr) {
			bb_ptr->state = BB_STATE_STAGED_IN;
			bb_ptr->state_time = time(NULL);
			if (bb_state.bb_config.debug_flag) {
				info("%s: Stage-in complete for job %u",
				     __func__, stage_args->job_id);
			}
			queue_job_scheduler();
		} else {
			error("%s: unable to find bb record for job %u",
			      __func__, stage_args->job_id);
		}
		pthread_mutex_unlock(&bb_state.bb_mutex);
	} else {
		xfree(job_ptr->state_desc);
		job_ptr->state_desc = xstrdup("burst buffer stage_in error");
		job_ptr->priority = 0;	/* Hold job */
		bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
		if (bb_ptr) {
			bb_ptr->state = BB_STATE_TEARDOWN;
			bb_ptr->state_time = time(NULL);
		}
		_queue_teardown(job_ptr->job_id, true);
	}
	unlock_slurmctld(job_write_lock);

	for (i = 0; setup_argv[i]; i++)
		xfree(setup_argv[i]);
	xfree(setup_argv);
	for (i = 0; data_in_argv[i]; i++)
		xfree(data_in_argv[i]);
	xfree(data_in_argv);
	xfree(stage_args);
	xfree(dws_setup_path);
	xfree(dws_data_in_path);
	return NULL;
}

static int _queue_stage_out(struct job_record *job_ptr)
{
	char *hash_dir = NULL, *job_dir = NULL;
	char **post_run_argv, **data_out_argv;
	stage_args_t *stage_args;
	int hash_inx = job_ptr->job_id % 10, rc = SLURM_SUCCESS;
	pthread_attr_t stage_attr;
	pthread_t stage_tid = 0;

	xstrfmtcat(hash_dir, "%s/hash.%d", state_save_loc, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
	post_run_argv = xmalloc(sizeof(char *) * 10);
	post_run_argv[0] = xstrdup("dws_post_run");
	xstrfmtcat(post_run_argv[1], "--job-environment-file=%s/post_run_env",
		   job_dir);
	data_out_argv = xmalloc(sizeof(char *) * 10);
	data_out_argv[0] = xstrdup("dws_data_out");
	xstrfmtcat(data_out_argv[1], "--job-environment-file=%s/data_out_env",
		   job_dir);

	stage_args = xmalloc(sizeof(stage_args_t));
	stage_args->job_id  = job_ptr->job_id;
	stage_args->timeout = bb_state.bb_config.stage_out_timeout;
	stage_args->args1   = post_run_argv;
	stage_args->args2   = data_out_argv;

	slurm_attr_init(&stage_attr);
	if (pthread_attr_setdetachstate(&stage_attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	while (pthread_create(&stage_tid, &stage_attr, _start_stage_out,
			      stage_args)) {
		if (errno != EAGAIN) {
			error("%s: pthread_create: %m", __func__);
			_start_stage_out(stage_args);	/* Do in-line */
			break;
		}
		usleep(100000);
	}
	slurm_attr_destroy(&stage_attr);

	xfree(hash_dir);
	xfree(job_dir);
	return rc;
}

static void *_start_stage_out(void *x)
{
	stage_args_t *stage_args;
	char *dws_post_run_path, *dws_data_out_path;
	char **post_run_argv, **data_out_argv, *resp_msg = NULL;
	int i, rc = SLURM_SUCCESS, status = 0, timeout;
	slurmctld_lock_t job_write_lock =
		    { NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	struct job_record *job_ptr;
	bb_alloc_t *bb_ptr = NULL;
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *error_buf = NULL, *output_buf = NULL, *job_env_filename;
	int status;
#endif

	stage_args = (stage_args_t *) x;
	post_run_argv = stage_args->args1;
	data_out_argv = stage_args->args2;

#if HAVE_DW_WLM_LIB_H
	job_env_filename = post_run_argv[1];
	START_TIMER;
	status = dw_wlm_lib_post_run(&output_buf, &error_buf, job_env_filename);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_post_run:%s", __func__, err_buf);
		free(err_buf);
		return SLURM_ERROR;
	}
	if (bb_state.bb_config.debug_flag) {
		debug("%s: dw_wlm_lib_post_run for %s ran for %s",  __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
		      TIME_STR);
	}
#else
	dws_post_run_path = _set_cmd_path("dws_post_run");
	if (stage_args->timeout)
		timeout = stage_args->timeout * 1000;
	else
		timeout = 5000;
	START_TIMER;
	resp_msg = bb_run_script("dws_post_run", dws_post_run_path,
				 post_run_argv, timeout, &status);
	END_TIMER;
	if (DELTA_TIMER > 500000) {	/* 0.5 secs */
		info("%s: dws_post_run for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	} else if (bb_state.bb_config.debug_flag) {
		debug("%s: dws_post_run for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	}
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: dws_post_run for job %u status:%u response:%s",
		      __func__, stage_args->job_id, status, resp_msg);
		rc = SLURM_ERROR;
	}
	xfree(resp_msg);
#endif

#if HAVE_DW_WLM_LIB_H
	START_TIMER;
	status = dw_wlm_lib_data_out(&output_buf, &error_buf, job_env_filename);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_data_out:%s", __func__, err_buf);
		free(err_buf);
		return SLURM_ERROR;
	}
	if (bb_state.bb_config.debug_flag) {
		debug("%s: dw_wlm_lib_data_out for %s ran for %s",  __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
		      TIME_STR);
	}
#else
	dws_data_out_path = _set_cmd_path("dws_data_out");
	if (stage_args->timeout)
		timeout = stage_args->timeout * 1000;
	else
		timeout = 24 * 60 * 60 * 1000;	/* One day */
	START_TIMER;
	resp_msg = bb_run_script("dws_data_out", dws_data_out_path,
				 data_out_argv, timeout, &status);
	END_TIMER;
	if (DELTA_TIMER > 5000000) {	/* 5 secs */
		info("%s: dws_data_out for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	} else if (bb_state.bb_config.debug_flag) {
		debug("%s: dws_data_out for job %u ran for %s",
		     __func__, stage_args->job_id, TIME_STR);
	}
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: dws_data_out for job %u status:%u response:%s",
		      __func__, stage_args->job_id, status, resp_msg);
		rc = SLURM_ERROR;
	}
	xfree(resp_msg);
#endif

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_args->job_id);
	if (!job_ptr) {
		error("%s: unable to find job record for job %u",
		      __func__, stage_args->job_id);
	} else {
		pthread_mutex_lock(&bb_state.bb_mutex);
		bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
		if (bb_ptr) {
			if (rc == SLURM_SUCCESS) {
				if (bb_state.bb_config.debug_flag) {
					info("%s: Stage-out complete for job %u",
					     __func__, stage_args->job_id);
				}
				/* bb_ptr->state = BB_STATE_STAGED_OUT; */
				bb_ptr->state = BB_STATE_TEARDOWN;
				bb_ptr->state_time = time(NULL);
				_queue_teardown(stage_args->job_id, true);
			} else if (bb_state.bb_config.debug_flag) {
				info("%s: Stage-out failed for job %u",
				     __func__, stage_args->job_id);
			}
		} else {
			error("%s: unable to find bb record for job %u",
			      __func__, stage_args->job_id);
		}
		pthread_mutex_unlock(&bb_state.bb_mutex);
	}
	unlock_slurmctld(job_write_lock);

	for (i = 0; post_run_argv[i]; i++)
		xfree(post_run_argv[i]);
	xfree(post_run_argv);
	for (i = 0; data_out_argv[i]; i++)
		xfree(data_out_argv[i]);
	xfree(data_out_argv);
	xfree(stage_args);
	xfree(dws_post_run_path);
	xfree(dws_data_out_path);
	return NULL;
}

static void _queue_teardown(uint32_t job_id, bool hurry)
{
	char **teardown_argv;
	stage_args_t *teardown_args;
	int hash_inx = job_id % 10;
	pthread_attr_t teardown_attr;
	pthread_t teardown_tid = 0;

	teardown_argv = xmalloc(sizeof(char *) * 10);
	teardown_argv[0] = xstrdup("dws_teardown");
	xstrfmtcat(teardown_argv[1],
		   "--job-environment-file=%s/hash.%d/job.%u/teardown_env",
		   state_save_loc, hash_inx, job_id);
	if (hurry)
		teardown_argv[2] = xstrdup("1");

	teardown_args = xmalloc(sizeof(stage_args_t));
	teardown_args->job_id  = job_id;
	teardown_args->timeout = 0;
	teardown_args->args1   = teardown_argv;

	slurm_attr_init(&teardown_attr);
	if (pthread_attr_setdetachstate(&teardown_attr,PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	while (pthread_create(&teardown_tid, &teardown_attr, _start_teardown,
			      teardown_args)) {
		if (errno != EAGAIN) {
			error("%s: pthread_create: %m", __func__);
			_start_teardown(teardown_args);	/* Do in-line */
			break;
		}
		usleep(100000);
	}
	slurm_attr_destroy(&teardown_attr);
}

static void *_start_teardown(void *x)
{
	stage_args_t *teardown_args;
	char *dws_teardown_path, **teardown_argv, *resp_msg = NULL;
	int i, status = 0, timeout;
	struct job_record *job_ptr;
	bb_alloc_t *bb_ptr = NULL;
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *error_buf = NULL, output_buf = NULL;
	int hurry = 0, status = 0;
#endif

	teardown_args = (stage_args_t *) x;
	teardown_argv = teardown_args->args1;

	START_TIMER;
	dws_teardown_path = _set_cmd_path("dws_teardown");
	if (teardown_args->timeout)
		timeout = teardown_args->timeout * 1000;
	else
		timeout = 5000;
#if HAVE_DW_WLM_LIB_H
	if (teardown_args->argv[2] && (teardown_args->argv[2][0] == '1'))
		hurry = 1;
	status = dw_wlm_lib_teardown(&output_buf, &error_buf, teardown_args->argv[1],
				     hurry);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_teardown for job %u: %s",
		      __func__, teardown_args->job_id, error_buf);
		free(error_buf);
	} else if (bb_state.bb_config.debug_flag) {
		info("%s: dw_wlm_lib_teardown for job %u ran for %s: %s",
		     __func__, teardown_args->job_id, TIME_STR, output_buf);
	}
#else
	resp_msg = bb_run_script("dws_teardown", dws_teardown_path,
				teardown_argv, timeout, &status);
	END_TIMER;
	if ((DELTA_TIMER > 500000) ||	/* 0.5 secs */
	    (bb_state.bb_config.debug_flag)) {
		info("%s: dws_teardown for job %u ran for %s",
		     __func__, teardown_args->job_id, TIME_STR);
	}
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("%s: dws_teardown for job %u status:%u response:%s",
		      __func__, teardown_args->job_id, status, resp_msg);
	}
	xfree(resp_msg);
#endif

	pthread_mutex_lock(&bb_state.bb_mutex);
	if ((job_ptr = find_job_record(teardown_args->job_id))) {
		_purge_bb_files(job_ptr);
		bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	}
	if (bb_ptr) {
		bb_ptr->cancelled = true;
		bb_ptr->end_time = 0;
		bb_ptr->state = BB_STATE_COMPLETE;
		bb_ptr->state_time = time(NULL);
		bb_remove_user_load(bb_ptr, &bb_state);
	} else {
		error("%s: unable to find bb record for job %u",
		      __func__, teardown_args->job_id);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	for (i = 0; teardown_argv[i]; i++)
		xfree(teardown_argv[i]);
	xfree(teardown_argv);
	xfree(teardown_argv);
	xfree(teardown_args);
	xfree(dws_teardown_path);
	return NULL;
}

static void _free_needed_gres_struct(needed_gres_t *needed_gres_ptr,
				     int gres_cnt)
{
	int i;
	if (needed_gres_ptr == NULL)
		return;

	for (i = 0; i < gres_cnt; i++)
		xfree(needed_gres_ptr->name);
	xfree(needed_gres_ptr);
}

static uint64_t _get_bb_resv(char *gres_name, burst_buffer_info_msg_t *resv_bb)
{
	burst_buffer_info_t *bb_array;
	burst_buffer_gres_t *gres_ptr;
	uint64_t resv_gres = 0;
	int i, j;

	if (!resv_bb)
		return resv_gres;

	for (i = 0, bb_array = resv_bb->burst_buffer_array;
	     i < resv_bb->record_count; i++, bb_array++) {
		if (bb_array->name && xstrcmp(bb_array->name, bb_state.name))
			continue;
		for (j = 0, gres_ptr = bb_array->gres_ptr;
		     j < bb_array->gres_cnt; j++, gres_ptr++) {
			if (!xstrcmp(gres_name, gres_ptr->name))
				resv_gres += gres_ptr->used_cnt;
		}
	}

	return resv_gres;
}

/* Test if a job can be allocated a burst buffer.
 * This may preempt currently active stage-in for higher priority jobs.
 *
 * RET 0: Job can be started now
 *     1: Job exceeds configured limits, continue testing with next job
 *     2: Job needs more resources than currently available can not start,
 *        skip all remaining jobs
 */
static int _test_size_limit(struct job_record *job_ptr, bb_job_t *bb_spec)
{
	burst_buffer_info_msg_t *resv_bb;
	needed_gres_t *needed_gres_ptr = NULL;
	struct preempt_bb_recs *preempt_ptr = NULL;
	List preempt_list;
	ListIterator preempt_iter;
	bb_user_t *user_ptr;
	int64_t tmp_g, tmp_u, tmp_j, tmp_r;
	int64_t lim_u, add_space, resv_space = 0;
	int64_t tmp_f;	/* Could go negative due to reservations */
	int64_t add_total_space_needed = 0, add_user_space_needed = 0;
	int64_t add_total_space_avail  = 0, add_user_space_avail  = 0;
	int64_t add_total_gres_needed = 0, add_total_gres_avail = 0;
	time_t now = time(NULL);
	bb_alloc_t *bb_ptr = NULL;
	int d, i, j, k;
	char jobid_buf[32];

	xassert(bb_spec);
	add_space = bb_spec->total_size;

	/* Determine if burst buffer can be allocated now for the job.
	 * If not, determine how much space must be free. */
	if (((bb_state.bb_config.job_size_limit  != NO_VAL64) &&
	     (add_space > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL64) &&
	     (add_space > bb_state.bb_config.user_size_limit))) {
		debug("%s: %s requested space above limit", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		return 1;
	}

	resv_bb = job_test_bb_resv(job_ptr, now);
	if (resv_bb) {
		burst_buffer_info_t *resv_bb_ptr;
		for (i = 0, resv_bb_ptr = resv_bb->burst_buffer_array;
		     i < resv_bb->record_count; i++, resv_bb_ptr++) {
			if (resv_bb_ptr->name &&
			    strcmp(resv_bb_ptr->name, bb_state.name))
				continue;
			resv_bb_ptr->used_space =
				bb_granularity(resv_bb_ptr->used_space,
					       bb_state.bb_config.granularity);
			resv_space += resv_bb_ptr->used_space;
		}
	}

	if (bb_state.bb_config.user_size_limit != NO_VAL64) {
		user_ptr = bb_find_user_rec(job_ptr->user_id,bb_state.bb_uhash);
		tmp_u = user_ptr->size;
		tmp_j = add_space;
		lim_u = bb_state.bb_config.user_size_limit;
		if (tmp_u + tmp_j > lim_u)
			add_user_space_needed = tmp_u + tmp_j - lim_u;
	}
	add_total_space_needed = bb_state.used_space + add_space + resv_space -
				 bb_state.total_space;
	needed_gres_ptr = xmalloc(sizeof(needed_gres_t) * bb_spec->gres_cnt);
	for (i = 0; i < bb_spec->gres_cnt; i++) {
		needed_gres_ptr[i].name = xstrdup(bb_spec->gres_ptr[i].name);
		for (j = 0; j < bb_state.bb_config.gres_cnt; j++) {
			if (strcmp(bb_spec->gres_ptr[i].name,
				   bb_state.bb_config.gres_ptr[j].name))
				continue;
			tmp_g = bb_granularity(bb_spec->gres_ptr[i].count,
					       bb_state.bb_config.gres_ptr[j].
					       granularity);
			bb_spec->gres_ptr[i].count = tmp_g;
			if (tmp_g > bb_state.bb_config.gres_ptr[j].avail_cnt) {
				debug("%s: %s requests more %s GRES than"
				      "configured", __func__,
				      jobid2fmt(job_ptr, jobid_buf,
						sizeof(jobid_buf)),
				      bb_spec->gres_ptr[i].name);
				_free_needed_gres_struct(needed_gres_ptr,
							 bb_spec->gres_cnt);
				if (resv_bb)
					slurm_free_burst_buffer_info_msg(resv_bb);
				return 1;
			}
			tmp_r = _get_bb_resv(bb_spec->gres_ptr[i].name,resv_bb);
			tmp_f = bb_state.bb_config.gres_ptr[j].avail_cnt -
				bb_state.bb_config.gres_ptr[j].used_cnt - tmp_r;
			if (tmp_g > tmp_f)
				needed_gres_ptr[i].add_cnt = tmp_g - tmp_f;
			add_total_gres_needed += needed_gres_ptr[i].add_cnt;
			break;
		}
		if (j >= bb_state.bb_config.gres_cnt) {
			debug("%s: %s requests %s GRES which are undefined",
			      __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      bb_spec->gres_ptr[i].name);
			_free_needed_gres_struct(needed_gres_ptr,
						 bb_spec->gres_cnt);
			if (resv_bb)
				slurm_free_burst_buffer_info_msg(resv_bb);
			return 1;
		}
	}

	if (resv_bb)
		slurm_free_burst_buffer_info_msg(resv_bb);

	if ((add_total_space_needed <= 0) &&
	    (add_user_space_needed  <= 0) && (add_total_gres_needed <= 0)) {
		_free_needed_gres_struct(needed_gres_ptr, bb_spec->gres_cnt);
		return 0;
	}

	/* Identify candidate burst buffers to revoke for higher priority job */
	preempt_list = list_create(bb_job_queue_del);
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
			if (bb_ptr->job_id &&
			    (bb_ptr->use_time > now) &&
			    (bb_ptr->use_time > job_ptr->start_time)) {
				preempt_ptr = xmalloc(sizeof(
						struct preempt_bb_recs));
				preempt_ptr->bb_ptr = bb_ptr;
				preempt_ptr->job_id = bb_ptr->job_id;
				preempt_ptr->size = bb_ptr->size;
				preempt_ptr->use_time = bb_ptr->use_time;
				preempt_ptr->user_id = bb_ptr->user_id;
				list_push(preempt_list, preempt_ptr);
				add_total_space_avail += bb_ptr->size;
				if (bb_ptr->user_id == job_ptr->user_id);
					add_user_space_avail += bb_ptr->size;
				if (add_total_gres_needed<add_total_gres_avail)
					j = bb_ptr->gres_cnt;
				else
					j = 0;
				for ( ; j < bb_ptr->gres_cnt; j++) {
					d = needed_gres_ptr[j].add_cnt -
					    needed_gres_ptr[j].avail_cnt;
					if (d <= 0)
						continue;
					for (k = 0; k < bb_spec->gres_cnt; k++){
						if (strcmp(needed_gres_ptr[j].name,
							   bb_spec->gres_ptr[k].name))
							continue;
						if (bb_spec->gres_ptr[k].count <
						    d) {
							d = bb_spec->
							    gres_ptr[k].count;
						}
						add_total_gres_avail += d;
						needed_gres_ptr[j].avail_cnt+=d;
					}
				}
			}
			bb_ptr = bb_ptr->next;
		}
	}

	if ((add_total_space_avail >= add_total_space_needed) &&
	    (add_user_space_avail  >= add_user_space_needed)  &&
	    (add_total_gres_avail  >= add_total_gres_needed)) {
		list_sort(preempt_list, bb_preempt_queue_sort);
		preempt_iter = list_iterator_create(preempt_list);
		while ((preempt_ptr = list_next(preempt_iter)) &&
		       (add_total_space_needed || add_user_space_needed ||
			add_total_gres_needed)) {
			bool do_preempt = false;
			if (add_user_space_needed &&
			    (preempt_ptr->user_id == job_ptr->user_id)) {
				do_preempt = true;
				add_user_space_needed  -= preempt_ptr->size;
				add_total_space_needed -= preempt_ptr->size;
			}
			if ((add_total_space_needed > add_user_space_needed) &&
			    (preempt_ptr->user_id != job_ptr->user_id)) {
				do_preempt = true;
				add_total_space_needed -= preempt_ptr->size;
			}
			if (add_total_gres_needed) {
				for (j = 0; j < bb_spec->gres_cnt; j++) {
					d = needed_gres_ptr[j].add_cnt;
					if (d <= 0)
						continue;
					for (k = 0;
					     k < preempt_ptr->bb_ptr->gres_cnt;
					     k++) {
						if (strcmp(needed_gres_ptr[j].name,
							   preempt_ptr->bb_ptr->
							   gres_ptr[k].name))
							continue;
						if (preempt_ptr->bb_ptr->
						    gres_ptr[k].used_cnt < d) {
							d = preempt_ptr->bb_ptr->
							    gres_ptr[k].used_cnt;
						}
						add_total_gres_needed -= d;
						needed_gres_ptr[j].add_cnt -= d;
						do_preempt = true;
					}
				}
			}
			if (do_preempt) {
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				preempt_ptr->bb_ptr->state = BB_STATE_TEARDOWN;
				preempt_ptr->bb_ptr->state_time = time(NULL);
				_queue_teardown(preempt_ptr->job_id, true);
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for %s", plugin_type,
					     __func__, preempt_ptr->job_id,
					     jobid2fmt(job_ptr, jobid_buf,
						       sizeof(jobid_buf)));
				}
			}
		}
		list_iterator_destroy(preempt_iter);
	}
	list_destroy(preempt_list);
	_free_needed_gres_struct(needed_gres_ptr, bb_spec->gres_cnt);

	return 2;
}

/* Handle timeout of burst buffer events:
 * 1. Purge per-job burst buffer records when the stage-out has completed and
 *    the job has been purged from Slurm
 * 2. Test for StageInTimeout events
 * 3. Test for StageOutTimeout events
 */
static void _timeout_bb_rec(void)
{
	bb_alloc_t **bb_pptr, *bb_ptr = NULL;
	struct job_record *job_ptr;
	int i;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_pptr = &bb_state.bb_hash[i];
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
//FIXME: Need to add BBS load state logic to track persistent BB limits
bb_ptr->seen_time = bb_state.last_load_time;
			if (bb_ptr->seen_time < bb_state.last_load_time) {
				if (bb_ptr->job_id == 0) {
					info("%s: Persistent burst buffer %s "
					     "purged",
					     __func__, bb_ptr->name);
				} else if (bb_state.bb_config.debug_flag) {
					info("%s: burst buffer for job %u "
					     "purged",
					     __func__, bb_ptr->job_id);
				}
				bb_remove_user_load(bb_ptr, &bb_state);
				*bb_pptr = bb_ptr->next;
				xfree(bb_ptr);
				break;
			}
			if (bb_ptr->state == BB_STATE_COMPLETE) {
				job_ptr = find_job_record(bb_ptr->job_id);
				if (!job_ptr || IS_JOB_PENDING(job_ptr)) {
					/* Job purged or BB preempted */
					*bb_pptr = bb_ptr->next;
					bb_free_rec(bb_ptr);
					break;
				}
			}
			bb_pptr = &bb_ptr->next;
			bb_ptr = bb_ptr->next;
		}
	}
}

/* Translate a batch script or interactive burst_buffer options into in
 * appropriate burst_buffer argument */
static int _parse_bb_opts(struct job_descriptor *job_desc)
{
	char *capacity, *end_ptr = NULL, *script, *save_ptr = NULL, *tok;
	int64_t raw_cnt;
	uint64_t gb_cnt = 0;
	uint32_t node_cnt = 0, swap_cnt = 0;
	int rc = SLURM_SUCCESS;

	if (!job_desc->script)
		return _parse_interactive(job_desc);

	script = xstrdup(job_desc->script);
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if ((tok[0] != '#') || (tok[1] != 'B') || (tok[2] != 'B')) {
			;
		} else {
			tok += 3;
			while (isspace(tok[0]))
				tok++;
			if (!strncmp(tok, "jobbb", 5) &&
			    (capacity = strstr(tok, "capacity="))) {
				raw_cnt = strtoll(capacity + 9, &end_ptr, 10);
				if (raw_cnt <= 0) {
					rc = ESLURM_INVALID_BURST_BUFFER_CHANGE;
					break;
				}
				if ((end_ptr[0] == 'n') ||
				    (end_ptr[0] == 'N')) {
					node_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'm') ||
					   (end_ptr[0] == 'M')) {
					raw_cnt = (raw_cnt + 1023) / 1024;
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'g') ||
					   (end_ptr[0] == 'G')) {
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 't') ||
					   (end_ptr[0] == 'T')) {
					raw_cnt *= 1024;
					gb_cnt += raw_cnt;
				} else if ((end_ptr[0] == 'p') ||
					   (end_ptr[0] == 'P')) {
					raw_cnt *= (1024 * 1024);
					gb_cnt += raw_cnt;
				}
			} else if (!strncmp(tok, "swap", 4)) {
				tok += 4;
				if (isspace(tok[0]))
					tok++;
				swap_cnt = strtol(tok, &end_ptr, 10);
			}
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(script);

	if ((rc == SLURM_SUCCESS) && (gb_cnt || node_cnt || swap_cnt)) {
		xfree(job_desc->burst_buffer);
		if (swap_cnt) {
			uint32_t job_nodes;
			if ((job_desc->max_nodes == 0) ||
			    (job_desc->max_nodes == NO_VAL)) {
				job_nodes = 1;
				info("%s: user %u submitted job with swap "
				     "space specification, but no node count "
				     "specification",
				     __func__, job_desc->user_id);

			} else {
				job_nodes = job_desc->max_nodes;
			}
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_SWAP=%uGB(%uNodes)",
				   swap_cnt, job_nodes);
			gb_cnt += swap_cnt * job_nodes;
		}
		if (gb_cnt) {
			if (job_desc->burst_buffer)
				xstrcat(job_desc->burst_buffer, " ");
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_SIZE=%"PRIu64"", gb_cnt);
		}
		if (node_cnt) {
			if (job_desc->burst_buffer)
				xstrcat(job_desc->burst_buffer, " ");
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_GRES=nodes:%u", node_cnt);
		}
	}

	return rc;
}

/* Parse interactive burst_buffer options into an appropriate burst_buffer
 * argument */
static int _parse_interactive(struct job_descriptor *job_desc)
{
	char *capacity, *end_ptr = NULL, *tok;
	int64_t raw_cnt;
	uint64_t gb_cnt = 0;
	uint32_t node_cnt = 0, swap_cnt = 0;
	int rc = SLURM_SUCCESS;

	if (!job_desc->burst_buffer)
		return rc;

	tok = job_desc->burst_buffer;
	while ((capacity = strstr(tok, "capacity="))) {
		raw_cnt = strtoll(capacity + 9, &end_ptr, 10);
		if (raw_cnt <= 0) {
			rc = ESLURM_INVALID_BURST_BUFFER_CHANGE;
			break;
		}
		if ((end_ptr[0] == 'n') || (end_ptr[0] == 'N')) {
			node_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			raw_cnt = (raw_cnt + 1023) / 1024;
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G')) {
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 't') || (end_ptr[0] == 'T')) {
			raw_cnt *= 1024;
			gb_cnt += raw_cnt;
		} else if ((end_ptr[0] == 'p') || (end_ptr[0] == 'P')) {
			raw_cnt *= (1024 * 1024);
			gb_cnt += raw_cnt;
		}
		tok = capacity + 9;
	}

	if ((tok = strstr(job_desc->burst_buffer, "swap=")))
		swap_cnt = strtol(tok + 5, &end_ptr, 10);

	if ((rc == SLURM_SUCCESS) && (gb_cnt || node_cnt || swap_cnt)) {
		if (swap_cnt) {
			uint32_t job_nodes;
			if ((job_desc->max_nodes == 0) ||
			    (job_desc->max_nodes == NO_VAL)) {
				job_nodes = 1;
				info("%s: user %u submitted job with swap "
				     "space specification, but no node count "
				     "specification",
				     __func__, job_desc->user_id);

			} else {
				job_nodes = job_desc->max_nodes;
			}
			xstrfmtcat(job_desc->burst_buffer,
				   " SLURM_SWAP=%uGB(%uNodes)",
				   swap_cnt, job_nodes);
			gb_cnt += swap_cnt * job_nodes;
		}
		if (gb_cnt) {
			xstrfmtcat(job_desc->burst_buffer,
				   " SLURM_SIZE=%"PRIu64"", gb_cnt);
		}
		if (node_cnt) {
			xstrfmtcat(job_desc->burst_buffer,
				   "SLURM_GRES=nodes:%u", node_cnt);
		}
	}

	return rc;
}


static int _build_bb_script(struct job_record *job_ptr, char *script_file)
{
	char *in_buf, *out_buf = NULL;
	char *sep, *tok, *tmp;
	int i, rc;

	xstrcat(out_buf, "#!/bin/bash\n");

	if ((tok = strstr(job_ptr->burst_buffer, "swap="))) {
		tok += 5;
		i = strtol(tok, NULL, 10);
		xstrfmtcat(out_buf, "#BB swap %dGB\n", i);
	}

	in_buf = xstrdup(job_ptr->burst_buffer);
	tmp = in_buf;
	while ((tok = strstr(tmp, "persistentbb="))) {
		tok += 13;
		sep = NULL;
		if ((tok[0] == '\'') || (tok[0] == '\"')) {
			sep = strchr(tok + 1, tok[0]);
			if (sep) {
				tok++;
				sep[0] = '\0';
				tmp = sep + 1;
			}
		}
		if (!sep) {
			sep = tok;
			while ((sep[0] != ' ') && (sep[0] != '\0'))
				sep++;
			if (sep[0] == '\0') {
				tmp = sep;
			} else {
				sep[0] = '\0';
				tmp = sep + 1;
			}
		}
		xstrfmtcat(out_buf, "#BB persistentbb %s\n", tok);
	}
	xfree(in_buf);

	in_buf = xstrdup(job_ptr->burst_buffer);
	tmp = in_buf;
	if ((tok = strstr(tmp, "jobbb="))) {
		tok += 6;
		sep = NULL;
		if ((tok[0] == '\'') || (tok[0] == '\"')) {
			sep = strchr(tok + 1, tok[0]);
			if (sep) {
				tok++;
				sep[0] = '\0';
			}
		}
		if (!sep) {
			sep = tok;
			while ((sep[0] != ' ') && (sep[0] != '\0'))
				sep++;
			sep[0] = '\0';
		}
		xstrfmtcat(out_buf, "#BB jobbb %s\n", tok);
	}
	xfree(in_buf);

	rc = _write_file(script_file, out_buf);
	xfree(out_buf);
	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, (char *)plugin_type); /* Remove "const" */
	_test_config();
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_alloc_cache(&bb_state);
	slurm_attr_init(&attr);
	while (pthread_create(&bb_state.bb_thread, &attr, _bb_agent, NULL)) {
		if (errno != EAGAIN) {
			fatal("%s: Unable to start thread: %m", __func__);
			break;
		}
		usleep(100000);
	}
	if (!state_save_loc)
		state_save_loc = slurm_get_state_save_location();

//FIXME: Set up BBS for running jobs
//FIXME: Call dws_teardown for stray BBS
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern int fini(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	pthread_mutex_lock(&bb_state.term_mutex);
	bb_state.term_flag = true;
	pthread_cond_signal(&bb_state.term_cond);
	pthread_mutex_unlock(&bb_state.term_mutex);

	if (bb_state.bb_thread) {
		pthread_join(bb_state.bb_thread, NULL);
		bb_state.bb_thread = 0;
	}
	bb_clear_config(&bb_state.bb_config, true);
	bb_clear_cache(&bb_state);
	xfree(state_save_loc);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a SLURM errno.
 */
extern int bb_p_load_state(bool init_config)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		debug("%s: %s", plugin_type,  __func__);
	_load_state();
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_reconfig(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_load_config(&bb_state, (char *)plugin_type); /* Remove "const" */
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a SLURM errno.
 */
extern int bb_p_state_pack(uid_t uid, Buf buffer, uint16_t protocol_version)
{
	uint32_t rec_count = 0;
	int eof, offset;

	pthread_mutex_lock(&bb_state.bb_mutex);
	packstr(bb_state.name, buffer);
	offset = get_buf_offset(buffer);
	pack32(rec_count,        buffer);
	bb_pack_state(&bb_state, buffer, protocol_version);
	if (bb_state.bb_config.private_data == 0)
		uid = 0;	/* User can see all data */
	rec_count = bb_pack_bufs(uid, bb_state.bb_hash,buffer,protocol_version);
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}
	if (bb_state.bb_config.debug_flag) {
		debug("%s: %s: record_count:%u",
		      plugin_type,  __func__, rec_count);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed prior to establishing job ID or creating script file.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid)
{
	bool have_gres = false, have_swap = false;
	int64_t bb_size = 0;
	char *key;
	int i, rc;

	if ((rc = _parse_bb_opts(job_desc)) != SLURM_SUCCESS)
		return rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_user_id:%u, submit_uid:%d",
		     plugin_type, __func__, job_desc->user_id, submit_uid);
		info("%s: burst_buffer:%s", __func__, job_desc->burst_buffer);
		info("%s: script:%s", __func__, job_desc->script);
	}

	if (job_desc->burst_buffer) {
		key = strstr(job_desc->burst_buffer, "SLURM_SIZE=");
		if (key) {
			bb_size = bb_get_size_num(key + 11,
						bb_state.bb_config.granularity);
		}
		if (strstr(job_desc->burst_buffer, "SLURM_GRES="))
			have_gres = true;
		if (strstr(job_desc->burst_buffer, "SLURM_SWAP="))
			have_swap = true;
	}
	if ((bb_size == 0) && (have_gres == false) && (have_swap == false))
		return SLURM_SUCCESS;
	if (bb_size < 0)
		return ESLURM_BURST_BUFFER_LIMIT;

	pthread_mutex_lock(&bb_state.bb_mutex);
	if (((bb_state.bb_config.job_size_limit  != NO_VAL64) &&
	     (bb_size > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL64) &&
	     (bb_size > bb_state.bb_config.user_size_limit))) {
		pthread_mutex_unlock(&bb_state.bb_mutex);
		return ESLURM_BURST_BUFFER_LIMIT;
	}

	if (bb_state.bb_config.allow_users) {
		for (i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i])
				break;
		}
		if (bb_state.bb_config.allow_users[i] == 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_state.bb_config.deny_users) {
		for (i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i])
				break;
		}
		if (bb_state.bb_config.deny_users[i] != 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_size > bb_state.total_space) {
		info("Job from user %u requested burst buffer size of "
		     "%"PRIu64", but total space is only %"PRIu64"",
		     job_desc->user_id, bb_size, bb_state.total_space);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	job_desc->shared = 0;	/* Compute nodes can not be shared */

	return SLURM_SUCCESS;
}

static void _purge_job_file(char *job_dir, char *file_name)
{
	char *tmp = NULL;
	xstrfmtcat(tmp, "%s/%s", job_dir, file_name);
	(void) unlink(tmp);
	xfree(tmp);
}

static void _purge_job_files(char *job_dir)
{
	_purge_job_file(job_dir, "setup_env");
	_purge_job_file(job_dir, "data_in_env");
	_purge_job_file(job_dir, "pre_run_env");
	_purge_job_file(job_dir, "post_run_env");
	_purge_job_file(job_dir, "data_out_env");
	_purge_job_file(job_dir, "teardown_env");
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_validate2(struct job_record *job_ptr, char **err_msg,
			      bool is_job_array)
{
	char *hash_dir = NULL, *job_dir = NULL, *script_file = NULL;
	char *resp_msg = NULL, **script_argv, *dws_job_process_path;
	int i, hash_inx, rc = SLURM_SUCCESS, status = 0;
	char jobid_buf[32];
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *output_buf = NULL, *error_buf = NULL;
	char *job = NULL, *setup_env_filename = NULL;
	char *data_in_env_filename = NULL, *pre_run_env_filename = NULL;
	char *post_run_env_filename = NULL, *data_out_env_filename = NULL;
	char *teardown_env_filename = NULL;
	int status = 0;
#else
#endif

	if (!_test_bb_spec(job_ptr))
		return rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	pthread_mutex_lock(&bb_state.bb_mutex);
	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(hash_dir, "%s/hash.%d", state_save_loc, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
	(void) mkdir(job_dir, 0700);
	xstrfmtcat(script_file, "%s/script", job_dir);
	if (job_ptr->batch_flag == 0)
		rc = _build_bb_script(job_ptr, script_file);
	dws_job_process_path = _set_cmd_path("dws_job_process");
	script_argv = xmalloc(sizeof(char *) * 10);
	script_argv[0] = xstrdup("dws_job_process");
	xstrfmtcat(script_argv[1], "--job=%s", script_file);
	xstrfmtcat(script_argv[2], "--setup-env-file=%s/setup_env", job_dir);
	xstrfmtcat(script_argv[3], "--data-in-env-file=%s/data_in_env",job_dir);
	xstrfmtcat(script_argv[4], "--pre-run-env-file=%s/pre_run_env",job_dir);
	xstrfmtcat(script_argv[5], "--post-run-env-file=%s/post_run_env",
		   job_dir);
	xstrfmtcat(script_argv[6], "--data-out-env-file=%s/data_out_env",
		   job_dir);
	xstrfmtcat(script_argv[7], "--teardown-env-file=%s/teardown_env",
		  job_dir);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	START_TIMER;
#if HAVE_DW_WLM_LIB_H
job = script_argv[1];
setup_env_filename = script_argv[2];
data_in_env_filename = script_argv[3];
pre_run_env_filename = script_argv[4];
post_run_env_filename = script_argv[5];
data_out_env_filename = script_argv[6];
teardown_env_filename = script_argv[7];
	status = dw_wlm_lib_job_process(&output_buf, &error_buf, job,
					setup_env_filename,
					data_in_env_filename,
					pre_run_env_filename,
					post_run_env_filename,
					data_out_env_filename,
					teardown_env_filename);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_job_process:%s", __func__, error_buf);
		free(error_buf);
		return SLURM_ERROR;
	}
	if (bb_state.bb_config.debug_flag) {
		debug("%s: dw_wlm_lib_job_process for %s ran for %s",
		      __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
		      TIME_STR);
	}
	if (status) {
		xfree(*err_msg);
		*err_msg = xstrdup(error_buf);
		free(error_buf);
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}
#else
	resp_msg = bb_run_script("dws_job_process", dws_job_process_path,
				 script_argv, 2000, &status);
	END_TIMER;
	if (DELTA_TIMER > 200000)	/* 0.2 secs */
		info("%s: dws_job_process ran for %s", __func__, TIME_STR);
	else if (bb_state.bb_config.debug_flag)
		debug("%s: dws_job_process ran for %s", __func__, TIME_STR);

	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		if (err_msg) {
			xfree(*err_msg);
			*err_msg = resp_msg;
		}
		resp_msg = NULL;
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	} else {
		xfree(resp_msg);
	}
#endif
	if (is_job_array)
		_purge_job_files(job_dir);

	for (i = 0; script_argv[i]; i++)
		xfree(script_argv[i]);
	xfree(script_argv);
	xfree(hash_dir);
	xfree(job_dir);
	xfree(script_file);
	xfree(dws_job_process_path);

	return rc;
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	time_t est_start = time(NULL);
	bb_job_t *bb_spec;
	char jobid_buf[32];
	int rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s",
		     plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
		return est_start;
	if ((bb_spec = _get_bb_spec(job_ptr)) == NULL)
		return est_start;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		rc = _test_size_limit(job_ptr, bb_spec);
		if (rc == 0) {		/* Could start now */
			;
		} else if (rc == 1) {	/* Exceeds configured limits */
			est_start += 365 * 24 * 60 * 60;
		} else {		/* No space currently available */
			est_start = MAX(est_start, bb_state.next_end_time);
		}
	} else if (bb_ptr->state < BB_STATE_STAGED_IN) {
		est_start++;
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);
	_del_bb_spec(bb_spec);

	return est_start;
}

/*
 * Validate a job submit request with respect to burst buffer options.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	job_queue_rec_t *job_rec;
	List job_candidates;
	ListIterator job_iter;
	struct job_record *job_ptr;
	bb_job_t *bb_spec;
	int rc;

	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(_job_queue_del);
	job_iter = list_iterator_create(job_queue);
	while ((job_ptr = list_next(job_iter))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    (job_ptr->start_time == 0) ||
		    (job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
			continue;
		if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
			continue;
		bb_spec = _get_bb_spec(job_ptr);
		if (bb_spec == NULL)
			continue;
		job_rec = xmalloc(sizeof(job_queue_rec_t));
		job_rec->job_ptr = job_ptr;
		job_rec->bb_spec = bb_spec;
		list_push(job_candidates, job_rec);
	}
	list_iterator_destroy(job_iter);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_set_use_time(&bb_state);
	job_iter = list_iterator_create(job_candidates);
	while ((job_rec = list_next(job_iter))) {
		job_ptr = job_rec->job_ptr;
		bb_spec = job_rec->bb_spec;

		if (bb_find_job_rec(job_ptr, bb_state.bb_hash))
			continue;	/* Job was already allocated a buffer */

		rc = _test_size_limit(job_ptr, bb_spec);
		if (rc == 0)
			(void) _alloc_job_bb(job_ptr, bb_spec);
		else if (rc == 1)
			continue;
		else /* (rc == 2) */
			break;
	}
	list_iterator_destroy(job_iter);
	pthread_mutex_unlock(&bb_state.bb_mutex);
	list_destroy(job_candidates);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to allocate burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_p_job_test_stage_in(struct job_record *job_ptr, bool test_only)
{
	bb_alloc_t *bb_ptr;
	bb_job_t *bb_spec;
	int rc = 1;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s test_only:%d",
		     plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
		     (int) test_only);
	}
	if ((bb_spec = _get_bb_spec(job_ptr)) == NULL)
		return rc;
	if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
		return -1;
	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		info("%s: %s bb_rec not found",
		      __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		rc = -1;
		if ((test_only == false) &&
		    (_test_size_limit(job_ptr, bb_spec) == 0) &&
		    (_alloc_job_bb(job_ptr, bb_spec) == SLURM_SUCCESS)) {
			rc = 0;
		}
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_IN) {
			rc = 0;
		} else if (bb_ptr->state == BB_STATE_STAGED_IN) {
			rc = 1;
		} else {
			error("%s: %s bb_state:%u", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);
	_del_bb_spec(bb_spec);

	return rc;
}

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run sucessfully AND
 * the compute nodes selected for the job.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_begin(struct job_record *job_ptr)
{
	char *pre_run_env_file = NULL, *client_nodes_file_nid = NULL;
	char *dws_pre_run_path = NULL, **pre_run_argv = NULL;
	char *job_dir = NULL;
	int i, hash_inx, rc = SLURM_SUCCESS, status = 0;
	bb_alloc_t *bb_ptr;
	char jobid_buf[32];
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *error_buf = NULL;
	char *output_buf = NULL;
	char *job_env_filename = NULL;
	int status = 0;
#else
	char *resp_msg = NULL;
#endif

	if (!_test_bb_spec(job_ptr))
		return rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s",
		     plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!job_ptr->job_resrcs || !job_ptr->job_resrcs->nodes) {
		error("%s: %s lacks node allocation", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		return SLURM_ERROR;
	}
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		error("%s: %s lacks burst buffer allocation", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		return SLURM_ERROR;
	}

	pthread_mutex_lock(&bb_state.bb_mutex);
	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(job_dir, "%s/hash.%d/job.%u", state_save_loc, hash_inx,
		   job_ptr->job_id);
	xstrfmtcat(client_nodes_file_nid, "%s/client_nids", job_dir);
	bb_ptr->state = BB_STATE_RUNNING;
	bb_ptr->state_time = time(NULL);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	rc = _write_nid_file(client_nodes_file_nid, job_ptr->job_resrcs->nodes,
			     job_ptr->job_id);
	if (rc == 0) {
#if HAVE_DW_WLM_LIB_H
		xstrfmtcat(job_env_filename,
			   "--job-environment-file=%s/pre_run_env", job_dir);
		START_TIMER;
		status = dw_wlm_lib_pre_run(&output_buf, &error_buf, NULL,
					    client_nodes_file_nid,
					    job_env_filename);
		END_TIMER;
		xfree(job_env_filename);
		if (status != DW_WLM_SUCCESS) {
			error("%s: dw_wlm_lib_pre_run:%s", __func__, error_buf);
			free(error_buf);
			return SLURM_ERROR;
		}
		if (bb_state.bb_config.debug_flag) {
			debug("%s: dw_wlm_lib_pre_run for %s ran for %s",
			      __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      TIME_STR);
		}
#else
//FIXME: Remove bb_run_script logic once the API is functional
		pre_run_argv = xmalloc(sizeof(char *) * 10);
		pre_run_argv[0] = xstrdup("dws_pre_run");
		xstrfmtcat(pre_run_argv[1],
			   "--job-environment-file=%s/pre_run_env", job_dir);
		dws_pre_run_path = _set_cmd_path("dws_pre_run");
		START_TIMER;
		resp_msg = bb_run_script("dws_pre_run", dws_pre_run_path,
					 pre_run_argv, 2000, &status);
		END_TIMER;
		if (DELTA_TIMER > 500000) {	/* 0.5 secs */
			info("%s: dws_pre_run for %s ran for %s",
			     __func__,
			     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			     TIME_STR);
		} else if (bb_state.bb_config.debug_flag) {
			debug("%s: dws_pre_run for %s ran for %s",
			      __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      TIME_STR);
		}
		if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
			time_t now = time(NULL);
			error("%s: dws_pre_run for %s status:%u response:%s",
			      __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      status, resp_msg);
			xfree(job_ptr->state_desc);
			job_ptr->state_desc = xstrdup(
					"Burst buffer pre_run error");
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			last_job_update = now;
			bb_ptr->state = BB_STATE_TEARDOWN;
			bb_ptr->state_time = now;
			_queue_teardown(job_ptr->job_id, true);
			rc = SLURM_ERROR;
		}
		xfree(resp_msg);
		for (i = 0; pre_run_argv[i]; i++)
			xfree(pre_run_argv[i]);
		xfree(pre_run_argv);
#endif
	}

	xfree(job_dir);
	xfree(pre_run_env_file);
	xfree(client_nodes_file_nid);
	xfree(dws_pre_run_path);
	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_start_stage_out(struct job_record *job_ptr)
{
//FIXME: How to handle various job terminate states (e.g. requeue, failure), user script controlled?
//FIXME: Test for memory leaks
	bb_alloc_t *bb_ptr;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!_test_bb_spec(job_ptr))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: %s bb_rec not found", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	} else {
		bb_ptr->state = BB_STATE_STAGING_OUT;
		bb_ptr->state_time = time(NULL);
		_queue_stage_out(job_ptr);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_stage_out(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	int rc = -1;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!_test_bb_spec(job_ptr))
		return 1;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: %s bb_rec not found", __func__,
		      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		rc =  1;
	} else {
		if (bb_ptr->state == BB_STATE_STAGING_OUT) {
			rc =  0;
		} else if (bb_ptr->state == BB_STATE_COMPLETE) {
			rc =  1;
		} else {
			error("%s: %s bb_state:%u", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)),
			      bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_cancel(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	char jobid_buf[32];

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: %s", plugin_type, __func__,
		     jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
	}

	if (!_test_bb_spec(job_ptr))
		return SLURM_SUCCESS;

//FIXME: Check all lock use throughout
	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (bb_ptr) {
		bb_ptr->state = BB_STATE_TEARDOWN;
		bb_ptr->state_time = time(NULL);
	}
	_queue_teardown(job_ptr->job_id, true);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/* bb_entry_get()
 *
 * This little parser handles the json stream
 * coming from the cray comamnd describing the
 * pools of burst buffers. The json stream is like
 * this { "pools": [ {}, .... {} ] } key pools
 * and an array of objects describing each pool.
 * The objects have only string and int types (for now).
 */
static
bb_entry_t *_bb_entry_get(int *num_ent, bb_state_t *state_ptr)
{
	bb_entry_t *ents = NULL;
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
#if HAVE_DW_WLM_LIB_H
	char *error_buf = NULL;
	char *output_buf = NULL;

	START_TIMER;
	status = dw_wlm_lib_pools(&output_buf, &error_buf);
	END_TIMER;
	if (status != DW_WLM_SUCCESS) {
		error("%s: dw_wlm_lib_pools:%s", __func__, error_buf);
		free(error_buf);
		return ents;
	}

	j = json_tokener_parse(output_buf);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, output_buf);
		free(output_buf);
		return ents;
	}
	free(output_buf);
#else
//FIXME: Remove bb_run_script logic once the API is functional
	char *string;
	char **script_argv;
	int i;

	script_argv = xmalloc(sizeof(char *) * 3);
	xstrfmtcat(script_argv[0], "%s", "dws_pools");
	xstrfmtcat(script_argv[1], "%s", "pools");

	START_TIMER;
	string = bb_run_script("dws_pools",
			       state_ptr->bb_config.get_sys_state,
			       script_argv, 3000, &status);
	END_TIMER;
	if (bb_state.bb_config.debug_flag)
		debug("%s: dws_pools ran for %s", __func__, TIME_STR);
	if (string == NULL) {
		error("%s: %s did not return any pool",
		      __func__, state_ptr->bb_config.get_sys_state);
		for (i = 0; script_argv[i]; i++)
			xfree(script_argv[i]);
		xfree(script_argv);
		return ents;
	}
	for (i = 0; script_argv[i]; i++)
		xfree(script_argv[i]);
	xfree(script_argv);

	j = json_tokener_parse(string);
	if (j == NULL) {
		error("%s: json parser failed on %s", __func__, string);
		xfree(string);
		return ents;
	}
	xfree(string);
#endif

	json_object_object_foreachC(j, iter) {
		ents = _json_parse_array(j, iter.key, num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	return ents;
}

/* bb_free_entry()
 */
static
void _bb_free_entry(struct bb_entry *ents, int num_ent)
{
	int i;

	for (i = 0; i < num_ent; i++) {
		xfree(ents[i].id);
		xfree(ents[i].units);
	}

	xfree(ents);
}

/* json_parse_array()
 */
static struct bb_entry *
_json_parse_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	struct bb_entry *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xmalloc(*num * sizeof(struct bb_entry));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_object(jvalue, &ents[i]);
		/* Convert to GB
		 */
		if (strcmp(ents[i].units, "bytes") == 0) {
			ents[i].gb_granularity
				= ents[i].granularity/(1024*1024*1024);
			ents[i].gb_quantity
				= ents[i].quantity * ents[i].gb_granularity;
			ents[i].gb_free
				= ents[i].free * ents[i].gb_granularity;
		} else {
			/* So the caller can use all the entries
			 * in a loop.
			 */
			ents[i].gb_granularity = ents[i].granularity;
			ents[i].gb_quantity = ents[i].quantity;
			ents[i].gb_free = ents[i].free;
		}
	}

	return ents;
}

/* json_parse_object()
 */
static void
_json_parse_object(json_object *jobj, struct bb_entry *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;
	const char *p;

	json_object_object_foreachC(jobj, iter) {

		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_boolean:
			case json_type_double:
			case json_type_null:
			case json_type_object:
			case json_type_array:
				break;
			case json_type_int:
				x = json_object_get_int64(iter.val);
				if (strcmp(iter.key, "granularity") == 0) {
					ent->granularity = x;
				} else if (strcmp(iter.key, "quantity") == 0) {
					ent->quantity = x;
				} else if (strcmp(iter.key, "free") == 0) {
					ent->free = x;
				}
				break;
			case json_type_string:
				p = json_object_get_string(iter.val);
				if (strcmp(iter.key, "id") == 0) {
					ent->id = xstrdup(p);
				} else if (strcmp(iter.key, "units") == 0) {
					ent->units = xstrdup(p);
				}
				break;
		}
	}
}
