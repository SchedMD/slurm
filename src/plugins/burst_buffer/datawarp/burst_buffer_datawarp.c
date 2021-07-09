/*****************************************************************************\
 *  burst_buffer_datawarp.c - Plugin for managing a Cray DataWarp burst_buffer
 *****************************************************************************
 *  Copyright (C) 2014-2018 SchedMD LLC.
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

#include "config.h"

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
#include "src/common/bitstring.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/run_command.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

#define _DEBUG 0	/* Detailed debugging information */
#define TIME_SLOP 60	/* Time allowed to synchronize operations between
			 * threads */
#define MAX_RETRY_CNT 2	/* Hold job if "pre_run" operation fails more than
			 * 2 times */

/* Script line types */
#define LINE_OTHER 0
#define LINE_BB    1
#define LINE_DW    2

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
 * the plugin (e.g., "burst_buffer" for Slurm burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "burst_buffer datawarp plugin";
const char plugin_type[]        = "burst_buffer/datawarp";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins */
static bb_state_t	bb_state;
static uint32_t		last_persistent_id = 1;

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern uint16_t accounting_enforce __attribute__((weak_import));
extern void *acct_db_conn  __attribute__((weak_import));
#else
uint16_t accounting_enforce = 0;
void *acct_db_conn = NULL;
#endif


/* Description of each Cray DW configuration entry
 */
typedef struct bb_configs {
	uint32_t id;
	uint32_t instance;
} bb_configs_t;

/* Description of each Cray DW instance entry, including persistent buffers
 */
typedef struct bb_instances {
	uint32_t id;
	uint64_t bytes;
	uint32_t session;
} bb_instances_t;

/* Description of each Cray DW pool entry
 */
typedef struct bb_pools {
	char *id;
	char *units;
	uint64_t granularity;
	uint64_t quantity;
	uint64_t free;
} bb_pools_t;

/* Description of each Cray DW pool entry
 */
typedef struct bb_sessions {
	uint32_t created;
	uint32_t id;
	char    *token;
	bool     used;
	uint32_t user_id;
} bb_sessions_t;

typedef struct {
	char   **args;
	uint32_t job_id;
	uint32_t timeout;
	uint32_t user_id;
} pre_run_args_t;

typedef struct {
	char   **args1;
	char   **args2;
	uint64_t bb_size;
	uint32_t job_id;
	char    *pool;
	uint32_t user_id;
} stage_args_t;

typedef struct create_buf_data {
	char *access;		/* Access mode */
	bool hurry;		/* Set to destroy in a hurry (no stage-out) */
	uint32_t job_id;	/* Job ID to use */
	char *job_script;	/* Path to job script */
	char *name;		/* Name of the persistent burst buffer */
	char *pool;		/* Name of pool in which to create the buffer */
	uint64_t size;		/* Size in bytes */
	char *type;		/* Access type */
	uint32_t user_id;
} create_buf_data_t;

#define BB_UNITS_BYTES 1
struct bb_total_size {
	int units;
	uint64_t capacity;
};

static int	_alloc_job_bb(job_record_t *job_ptr, bb_job_t *bb_job,
			      bool job_ready);
static void	_apply_limits(void);
static void *	_bb_agent(void *args);
static void	_bb_free_configs(bb_configs_t *ents, int num_ent);
static void	_bb_free_instances(bb_instances_t *ents, int num_ent);
static void	_bb_free_pools(bb_pools_t *ents, int num_ent);
static void	_bb_free_sessions(bb_sessions_t *ents, int num_ent);
static bb_configs_t *_bb_get_configs(int *num_ent, bb_state_t *state_ptr,
				     uint32_t timeout);
static bb_instances_t *_bb_get_instances(int *num_ent, bb_state_t *state_ptr,
					 uint32_t timeout);
static bb_pools_t *_bb_get_pools(int *num_ent, bb_state_t *state_ptr,
				 uint32_t timeout);
static bb_sessions_t *_bb_get_sessions(int *num_ent, bb_state_t *state_ptr,
				       uint32_t timeout);
static int	_create_bufs(job_record_t *job_ptr, bb_job_t *bb_job,
			     bool job_ready);
static void *	_create_persistent(void *x);
static void *	_destroy_persistent(void *x);
static void	_free_create_args(create_buf_data_t *create_args);
static bb_job_t *_get_bb_job(job_record_t *job_ptr);
static bool	_have_dw_cmd_opts(bb_job_t *bb_job);
static void	_job_queue_del(void *x);
static bb_configs_t *_json_parse_configs_array(json_object *jobj, char *key,
					       int *num);
static bb_instances_t *_json_parse_instances_array(json_object *jobj, char *key,
						   int *num);
static struct bb_pools *_json_parse_pools_array(json_object *jobj, char *key,
						int *num);
static struct bb_sessions *_json_parse_sessions_array(json_object *jobj,
						      char *key, int *num);
static void	_json_parse_configs_object(json_object *jobj,
					   bb_configs_t *ent);
static void	_json_parse_instances_object(json_object *jobj,
					     bb_instances_t *ent);
static void	_json_parse_pools_object(json_object *jobj, bb_pools_t *ent);
static void	_json_parse_sessions_object(json_object *jobj,
					    bb_sessions_t *ent);
static struct bb_total_size *_json_parse_real_size(json_object *j);
static void	_log_script_argv(char **script_argv, char *resp_msg);
static void	_load_state(bool init_config);
static int	_parse_bb_opts(job_desc_msg_t *job_desc, uint64_t *bb_size,
			       uid_t submit_uid);
static void	_parse_config_links(json_object *instance, bb_configs_t *ent);
static void	_parse_instance_capacity(json_object *instance,
					 bb_instances_t *ent);
static void	_parse_instance_links(json_object *instance,
				      bb_instances_t *ent);
static void	_pick_alloc_account(bb_alloc_t *bb_alloc);
static void	_purge_bb_files(uint32_t job_id, job_record_t *job_ptr);
static void	_purge_vestigial_bufs(void);
static void	_python2json(char *buf);
static void	_recover_bb_state(void);
static int	_queue_stage_in(job_record_t *job_ptr, bb_job_t *bb_job);
static int	_queue_stage_out(job_record_t *job_ptr, bb_job_t *bb_job);
static void	_queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry);
static void	_reset_buf_state(uint32_t user_id, uint32_t job_id, char *name,
				 int new_state, uint64_t buf_size);
static void	_save_bb_state(void);
static void	_set_assoc_mgr_ptrs(bb_alloc_t *bb_alloc);
static void *	_start_pre_run(void *x);
static void *	_start_stage_in(void *x);
static void *	_start_stage_out(void *x);
static void *	_start_teardown(void *x);
static void	_test_config(void);
static bool	_test_persistent_use_ready(bb_job_t *bb_job,
					   job_record_t *job_ptr);
static void	_timeout_bb_rec(void);
static int	_xlate_batch(job_desc_msg_t *job_desc);
static int	_xlate_interactive(job_desc_msg_t *job_desc);

/* Convert a Python string to real JSON format. Specifically replace single
 * quotes with double quotes and strip leading "u" before the single quotes.
 * See: https://github.com/stedolan/jq/issues/312 */
static void _python2json(char *buf)
{
	bool quoted = false;
	int i, o;

	if (!buf)
		return;
	for (i = 0, o = 0; ; i++) {
		if (buf[i] == '\'') {
			buf[o++] = '\"';
			quoted = !quoted;
		} else if ((buf[i] == 'u') && (buf[i+1] == '\'') && !quoted) {
			/* Skip over unicode flag */
		} else {
			buf[o++] = buf[i];
			if (buf[i] == '\0')
				break;
		}
	}
}

/* Log a command's arguments. */
static void _log_script_argv(char **script_argv, char *resp_msg)
{
	char *cmd_line = NULL;
	int i;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF))
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

static void _job_queue_del(void *x)
{
	bb_job_queue_rec_t *job_rec = (bb_job_queue_rec_t *) x;
	if (job_rec) {
		xfree(job_rec);
	}
}

/* Purge files we have created for the job.
 * bb_state.bb_mutex is locked on function entry.
 * job_ptr may be NULL if not found */
static void _purge_bb_files(uint32_t job_id, job_record_t *job_ptr)

{
	char *hash_dir = NULL, *job_dir = NULL;
	char *script_file = NULL, *path_file = NULL, *client_nids_file = NULL;
	int hash_inx;

	hash_inx = job_id % 10;
	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_id);
	(void) mkdir(job_dir, 0700);

	xstrfmtcat(client_nids_file, "%s/client_nids", job_dir);
	(void) unlink(client_nids_file);
	xfree(client_nids_file);

	xstrfmtcat(path_file, "%s/pathfile", job_dir);
	(void) unlink(path_file);
	xfree(path_file);

	if (!job_ptr || (job_ptr->batch_flag == 0)) {
		xstrfmtcat(script_file, "%s/script", job_dir);
		(void) unlink(script_file);
		xfree(script_file);
	}

	(void) unlink(job_dir);
	xfree(job_dir);
	xfree(hash_dir);
}

/* Validate that our configuration is valid for this plugin type */
static void _test_config(void)
{
	if (!bb_state.bb_config.get_sys_state) {
		debug("GetSysState is NULL");
		bb_state.bb_config.get_sys_state =
			xstrdup("/opt/cray/dw_wlm/default/bin/dw_wlm_cli");
	}
	if (!bb_state.bb_config.get_sys_status) {
		debug("GetSysStatus is NULL");
		bb_state.bb_config.get_sys_status =
			xstrdup("/opt/cray/dws/default/bin/dwstat");
	}
}

/* Allocate resources to a job and begin setup/stage-in */
static int _alloc_job_bb(job_record_t *job_ptr, bb_job_t *bb_job,
			 bool job_ready)
{
	int rc = SLURM_SUCCESS;

	log_flag(BURST_BUF, "start job allocate %pJ",
		 job_ptr);

	if (bb_job->buf_cnt &&
	    (_create_bufs(job_ptr, bb_job, job_ready) > 0))
		return EAGAIN;

	if (bb_job->state < BB_STATE_STAGING_IN) {
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_STAGING_IN);
		rc = _queue_stage_in(job_ptr, bb_job);
		if (rc != SLURM_SUCCESS) {
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
			_queue_teardown(job_ptr->job_id, job_ptr->user_id,
					true);
		}
	}

	return rc;
}

/* Perform periodic background activities */
static void *_bb_agent(void *args)
{
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	while (!bb_state.term_flag) {
		bb_sleep(&bb_state, AGENT_INTERVAL);
		if (!bb_state.term_flag) {
			_load_state(false);	/* Has own locking */
			lock_slurmctld(job_write_lock);
			slurm_mutex_lock(&bb_state.bb_mutex);
			_timeout_bb_rec();
			slurm_mutex_unlock(&bb_state.bb_mutex);
			unlock_slurmctld(job_write_lock);
		}
		_save_bb_state();	/* Has own locks excluding file write */
	}

	return NULL;
}

/* Given a request size and a pool name (or NULL name for default pool),
 * return the required buffer size (rounded up by granularity) */
static uint64_t _set_granularity(uint64_t orig_size, char *bb_pool)
{
	burst_buffer_pool_t *pool_ptr;
	uint64_t new_size;
	int i;

	if (!bb_pool || !xstrcmp(bb_pool, bb_state.bb_config.default_pool)) {
		new_size = bb_granularity(orig_size,
					  bb_state.bb_config.granularity);
		return new_size;
	}

	for (i = 0, pool_ptr = bb_state.bb_config.pool_ptr;
	     i < bb_state.bb_config.pool_cnt; i++, pool_ptr++) {
		if (!xstrcmp(bb_pool, pool_ptr->name)) {
			new_size = bb_granularity(orig_size,
						  pool_ptr->granularity);
			return new_size;
		}
	}
	debug("Could not find pool %s", bb_pool);
	return orig_size;
}

/* Return the burst buffer size specification of a job
 * RET size data structure or NULL of none found
 * NOTE: delete return value using _del_bb_size() */
static bb_job_t *_get_bb_job(job_record_t *job_ptr)
{
	char *bb_specs, *bb_hurry, *bb_name, *bb_type, *bb_access, *bb_pool;
	char *end_ptr = NULL, *save_ptr = NULL, *sub_tok, *tok;
	bool have_bb = false;
	uint64_t tmp_cnt;
	int inx;
	bb_job_t *bb_job;
	uint16_t new_bb_state;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return NULL;

	if ((bb_job = bb_job_find(&bb_state, job_ptr->job_id)))
		return bb_job;	/* Cached data */

	bb_job = bb_job_alloc(&bb_state, job_ptr->job_id);
	bb_job->account = xstrdup(job_ptr->account);
	if (job_ptr->part_ptr)
		bb_job->partition = xstrdup(job_ptr->part_ptr->name);
	if (job_ptr->qos_ptr)
		bb_job->qos = xstrdup(job_ptr->qos_ptr->name);
	new_bb_state = job_ptr->burst_buffer_state ?
		bb_state_num(job_ptr->burst_buffer_state) : BB_STATE_PENDING;
	bb_set_job_bb_state(job_ptr, bb_job, new_bb_state);
	bb_job->user_id = job_ptr->user_id;
	bb_specs = xstrdup(job_ptr->burst_buffer);
	tok = strtok_r(bb_specs, "\n", &save_ptr);
	while (tok) {
		uint32_t bb_flag = 0;
		if (tok[0] != '#') {
			tok = strtok_r(NULL, "\n", &save_ptr);
			continue;
		}
		if ((tok[1] == 'B') && (tok[2] == 'B'))
			bb_flag = BB_FLAG_BB_OP;
		else if ((tok[1] == 'D') && (tok[2] == 'W'))
			bb_flag = BB_FLAG_DW_OP;

		/*
		 * Effective Slurm v18.08 and CLE6.0UP06 the create_persistent
		 * and destroy_persistent functions are directly supported by
		 * dw_wlm_cli. Support "#BB" format for backward compatibility.
		 */
		if (bb_flag != 0) {
			tok += 3;
			while (isspace(tok[0]))
				tok++;
		}

		/*
		 * Is % symbol replacement required? Only done on "#DW" / "#BB"
		 * lines.
		 */
		if (bb_flag && strchr(tok, (int) '%'))
			bb_job->need_symbol_replacement = true;

		if (bb_flag == BB_FLAG_BB_OP) {
			if (!xstrncmp(tok, "create_persistent", 17)) {
				have_bb = true;
				bb_access = NULL;
				bb_name = NULL;
				bb_pool = NULL;
				bb_type = NULL;
				if ((sub_tok = strstr(tok, "access_mode="))) {
					bb_access = xstrdup(sub_tok + 12);
					sub_tok = strchr(bb_access, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				} else if ((sub_tok = strstr(tok, "access="))) {
					bb_access = xstrdup(sub_tok + 7);
					sub_tok = strchr(bb_access, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				}
				if ((sub_tok = strstr(tok, "capacity="))) {
					tmp_cnt = bb_get_size_num(sub_tok+9, 1);
				} else {
					tmp_cnt = 0;
				}
				if ((sub_tok = strstr(tok, "name="))) {
					bb_name = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_name, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				}
				if ((sub_tok = strstr(tok, "pool="))) {
					bb_pool = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_pool, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				} else {
					bb_pool = xstrdup(
						bb_state.bb_config.default_pool);
				}
				if ((sub_tok = strstr(tok, "type="))) {
					bb_type = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_type, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				}
				inx = bb_job->buf_cnt++;
				bb_job->buf_ptr = xrealloc(bb_job->buf_ptr,
							   sizeof(bb_buf_t) *
							   bb_job->buf_cnt);
				bb_job->buf_ptr[inx].access = bb_access;
				bb_job->buf_ptr[inx].create = true;
				bb_job->buf_ptr[inx].flags = bb_flag;
				//bb_job->buf_ptr[inx].hurry = false;
				bb_job->buf_ptr[inx].name = bb_name;
				bb_job->buf_ptr[inx].pool = bb_pool;
				tmp_cnt = _set_granularity(tmp_cnt, bb_pool);
				bb_job->buf_ptr[inx].size = tmp_cnt;
				bb_job->buf_ptr[inx].state = BB_STATE_PENDING;
				bb_job->buf_ptr[inx].type = bb_type;
				//bb_job->buf_ptr[inx].use = false;
				bb_job->persist_add += tmp_cnt;
			} else if (!xstrncmp(tok, "destroy_persistent", 18)) {
				have_bb = true;
				bb_name = NULL;
				if ((sub_tok = strstr(tok, "name="))) {
					bb_name = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_name, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				}
				/* if ((sub_tok = strstr(tok, "type="))) { */
				/* 	bb_type = xstrdup(sub_tok + 5); */
				/* 	sub_tok = strchr(bb_type, ' '); */
				/* 	if (sub_tok) */
				/* 		sub_tok[0] = '\0'; */
				/* } */
				bb_hurry = strstr(tok, "hurry");
				inx = bb_job->buf_cnt++;
				bb_job->buf_ptr = xrealloc(bb_job->buf_ptr,
							   sizeof(bb_buf_t) *
							   bb_job->buf_cnt);
				//bb_job->buf_ptr[inx].access = NULL;
				//bb_job->buf_ptr[inx].create = false;
				bb_job->buf_ptr[inx].destroy = true;
				bb_job->buf_ptr[inx].flags = bb_flag;
				bb_job->buf_ptr[inx].hurry = (bb_hurry != NULL);
				bb_job->buf_ptr[inx].name = bb_name;
				//bb_job->buf_ptr[inx].pool = NULL;
				//bb_job->buf_ptr[inx].size = 0;
				bb_job->buf_ptr[inx].state = BB_STATE_PENDING;
				//bb_job->buf_ptr[inx].type = NULL;
				//bb_job->buf_ptr[inx].use = false;
			} else {
				/* Ignore other (future) options */
			}
		}
		if (bb_flag == BB_FLAG_DW_OP) {
			if (!xstrncmp(tok, "jobdw", 5)) {
				have_bb = true;
				if ((sub_tok = strstr(tok, "capacity="))) {
					tmp_cnt = bb_get_size_num(sub_tok+9, 1);
				} else {
					tmp_cnt = 0;
				}
				if ((sub_tok = strstr(tok, "pool="))) {
					xfree(bb_job->job_pool);
					bb_job->job_pool = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_job->job_pool, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				} else {
					bb_job->job_pool = xstrdup(
						bb_state.bb_config.default_pool);
				}
				tmp_cnt = _set_granularity(tmp_cnt,
							   bb_job->job_pool);
				bb_job->req_size += tmp_cnt;
				bb_job->total_size += tmp_cnt;
				bb_job->use_job_buf = true;
			} else if (!xstrncmp(tok, "persistentdw", 12)) {
				/* Persistent buffer use */
				have_bb = true;
				bb_name = NULL;
				if ((sub_tok = strstr(tok, "name="))) {
					bb_name = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_name, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				}
				inx = bb_job->buf_cnt++;
				bb_job->buf_ptr = xrealloc(bb_job->buf_ptr,
							   sizeof(bb_buf_t) *
							   bb_job->buf_cnt);
				//bb_job->buf_ptr[inx].access = NULL;
				//bb_job->buf_ptr[inx].create = false;
				//bb_job->buf_ptr[inx].destroy = false;
				//bb_job->buf_ptr[inx].hurry = false;
				bb_job->buf_ptr[inx].name = bb_name;
				//bb_job->buf_ptr[inx].size = 0;
				bb_job->buf_ptr[inx].state = BB_STATE_PENDING;
				//bb_job->buf_ptr[inx].type = NULL;
				bb_job->buf_ptr[inx].use = true;
			} else if (!xstrncmp(tok, "swap", 4)) {
				have_bb = true;
				tok += 4;
				while (isspace(tok[0]))
					tok++;
				bb_job->swap_size = strtol(tok, &end_ptr, 10);
				if (job_ptr->details &&
				    job_ptr->details->max_nodes) {
					bb_job->swap_nodes =
						job_ptr->details->max_nodes;
				} else if (job_ptr->details) {
					bb_job->swap_nodes =
						job_ptr->details->min_nodes;
				} else {
					bb_job->swap_nodes = 1;
				}
				tmp_cnt = (uint64_t) bb_job->swap_size *
					  bb_job->swap_nodes;
				if ((sub_tok = strstr(tok, "pool="))) {
					xfree(bb_job->job_pool);
					bb_job->job_pool = xstrdup(sub_tok + 5);
					sub_tok = strchr(bb_job->job_pool, ' ');
					if (sub_tok)
						sub_tok[0] = '\0';
				} else if (!bb_job->job_pool) {
					bb_job->job_pool = xstrdup(
						bb_state.bb_config.default_pool);
				}
				tmp_cnt = _set_granularity(tmp_cnt,
							   bb_job->job_pool);
				bb_job->req_size += tmp_cnt;
				bb_job->total_size += tmp_cnt;
				bb_job->use_job_buf = true;
			} else {
				/* Ignore stage-in, stage-out, etc. */
			}
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(bb_specs);

	if (!have_bb) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		xstrfmtcat(job_ptr->state_desc,
			   "%s: Invalid burst buffer spec (%s)",
			   plugin_type, job_ptr->burst_buffer);
		job_ptr->priority = 0;
		info("Invalid burst buffer spec for %pJ (%s)",
		     job_ptr, job_ptr->burst_buffer);
		bb_job_del(&bb_state, job_ptr->job_id);
		return NULL;
	}
	if (!bb_job->job_pool)
		bb_job->job_pool = xstrdup(bb_state.bb_config.default_pool);
	if (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF)
		bb_job_log(&bb_state, bb_job);
	return bb_job;
}

/* At slurmctld start up time, for every currently active burst buffer,
 * update that user's limit. Also log every recovered buffer */
static void _apply_limits(void)
{
	bool emulate_cray = false;
	bb_alloc_t *bb_alloc;
	int i;

	if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY)
		emulate_cray = true;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			info("Recovered buffer Name:%s User:%u Pool:%s Size:%"PRIu64,
			     bb_alloc->name, bb_alloc->user_id,
			     bb_alloc->pool, bb_alloc->size);
			_set_assoc_mgr_ptrs(bb_alloc);
			bb_limit_add(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state, emulate_cray);
			bb_alloc = bb_alloc->next;
		}
	}
}

/* Write current burst buffer state to a file so that we can preserve account,
 * partition, and QOS information of persistent burst buffers as there is no
 * place to store that information within the DataWarp data structures */
static void _save_bb_state(void)
{
	static time_t last_save_time = 0;
	static int high_buffer_size = 16 * 1024;
	time_t save_time = time(NULL);
	bb_alloc_t *bb_alloc;
	uint32_t rec_count = 0;
	buf_t *buffer;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	int i, count_offset, offset;
	uint16_t protocol_version = SLURM_PROTOCOL_VERSION;

	if ((bb_state.last_update_time <= last_save_time) &&
	    !bb_state.term_flag)
		return;

	/* Build buffer with name/account/partition/qos information for all
	 * named burst buffers so we can preserve limits across restarts */
	buffer = init_buf(high_buffer_size);
	pack16(protocol_version, buffer);
	count_offset = get_buf_offset(buffer);
	pack32(rec_count, buffer);
	if (bb_state.bb_ahash) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		for (i = 0; i < BB_HASH_SIZE; i++) {
			bb_alloc = bb_state.bb_ahash[i];
			while (bb_alloc) {
				if (bb_alloc->name) {
					packstr(bb_alloc->account,	buffer);
					pack_time(bb_alloc->create_time,buffer);
					pack32(bb_alloc->id,		buffer);
					packstr(bb_alloc->name,		buffer);
					packstr(bb_alloc->partition,	buffer);
					packstr(bb_alloc->pool,		buffer);
					packstr(bb_alloc->qos,		buffer);
					pack32(bb_alloc->user_id,	buffer);
					if (bb_state.bb_config.flags &
					    BB_FLAG_EMULATE_CRAY)
						pack64(bb_alloc->size,	buffer);
					rec_count++;
				}
				bb_alloc = bb_alloc->next;
			}
		}
		save_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		offset = get_buf_offset(buffer);
		set_buf_offset(buffer, count_offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, offset);
	}

	xstrfmtcat(old_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_cray_state.old");
	xstrfmtcat(reg_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_cray_state");
	xstrfmtcat(new_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_cray_state.new");

	bb_write_state_file(old_file, reg_file, new_file, "burst_buffer_cray",
			    buffer, high_buffer_size, save_time,
			    &last_save_time);

	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	free_buf(buffer);
}

/* Recover saved burst buffer state and use it to preserve account, partition,
 * and QOS information for persistent burst buffers. */
static void _recover_bb_state(void)
{
	char *state_file = NULL, *data = NULL;
	int data_allocated, data_read = 0;
	uint16_t protocol_version = NO_VAL16;
	uint32_t data_size = 0, rec_count = 0, name_len = 0;
	uint32_t id = 0, user_id = 0;
	uint64_t size = 0;
	int i, state_fd;
	char *account = NULL, *name = NULL;
	char *partition = NULL, *pool = NULL, *qos = NULL;
	char *end_ptr = NULL;
	time_t create_time = 0;
	bb_alloc_t *bb_alloc;
	buf_t *buffer;

	state_fd = bb_open_state_file("burst_buffer_cray_state", &state_file);
	if (state_fd < 0) {
		info("No burst buffer state file (%s) to recover",
		     state_file);
		xfree(state_file);
		return;
	}
	data_allocated = BUF_SIZE;
	data = xmalloc(data_allocated);
	while (1) {
		data_read = read(state_fd, &data[data_size], BUF_SIZE);
		if (data_read < 0) {
			if  (errno == EINTR)
				continue;
			else {
				error("Read error on %s: %m", state_file);
				break;
			}
		} else if (data_read == 0)     /* eof */
			break;
		data_size      += data_read;
		data_allocated += data_read;
		xrealloc(data, data_allocated);
	}
	close(state_fd);
	xfree(state_file);

	buffer = create_buf(data, data_size);
	safe_unpack16(&protocol_version, buffer);
	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover burst_buffer/datawarp state, data version incompatible, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("**********************************************************************");
		error("Can not recover burst_buffer/datawarp state, data version incompatible");
		error("**********************************************************************");
		return;
	}

	safe_unpack32(&rec_count, buffer);
	for (i = 0; i < rec_count; i++) {
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&account,   &name_len, buffer);
			safe_unpack_time(&create_time, buffer);
			safe_unpack32(&id, buffer);
			safe_unpackstr_xmalloc(&name,      &name_len, buffer);
			safe_unpackstr_xmalloc(&partition, &name_len, buffer);
			safe_unpackstr_xmalloc(&pool,      &name_len, buffer);
			safe_unpackstr_xmalloc(&qos,       &name_len, buffer);
			safe_unpack32(&user_id, buffer);
			if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY)
				safe_unpack64(&size, buffer);
		}

		slurm_mutex_lock(&bb_state.bb_mutex);
		if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY) {
			bb_alloc = bb_alloc_name_rec(&bb_state, name, user_id);
			bb_alloc->id = id;
			last_persistent_id = MAX(last_persistent_id, id);
			if (name && (name[0] >='0') && (name[0] <='9')) {
				bb_alloc->job_id = strtol(name, &end_ptr, 10);
				bb_alloc->array_job_id = bb_alloc->job_id;
				bb_alloc->array_task_id = NO_VAL;
			}
			bb_alloc->seen_time = time(NULL);
			bb_alloc->size = size;
		} else {
			bb_alloc = bb_find_name_rec(name, user_id, &bb_state);
		}
		if (bb_alloc) {
			log_flag(BURST_BUF, "Recovered burst buffer %s from user %u",
				 bb_alloc->name, bb_alloc->user_id);
			xfree(bb_alloc->account);
			bb_alloc->account = account;
			account = NULL;
			bb_alloc->create_time = create_time;
			xfree(bb_alloc->partition);
			bb_alloc->partition = partition;
			partition = NULL;
			xfree(bb_alloc->pool);
			bb_alloc->pool = pool;
			pool = NULL;
			xfree(bb_alloc->qos);
			bb_alloc->qos = qos;
			qos = NULL;
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
		xfree(account);
		xfree(name);
		xfree(partition);
		xfree(pool);
		xfree(qos);
	}

	info("Recovered state of %d burst buffers", rec_count);
	free_buf(buffer);
	return;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete burst buffer data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete burst buffer data checkpoint file");
	xfree(account);
	xfree(name);
	xfree(partition);
	xfree(qos);
	free_buf(buffer);
	return;
}

/* We just found an unexpected session, set default account, QOS, & partition.
 * Copy the information from any currently existing session for the same user.
 * If none found, use his default account and QOS.
 * NOTE: assoc_mgr_locks need to be locked with
 * assoc_mgr_lock_t assoc_locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
 *				    NO_LOCK, NO_LOCK, NO_LOCK };
 * before calling this.
 */
static void _pick_alloc_account(bb_alloc_t *bb_alloc)
{
	slurmdb_assoc_rec_t assoc_rec;
	slurmdb_qos_rec_t   qos_rec;
	bb_alloc_t *bb_ptr = NULL;

	bb_ptr = bb_state.bb_ahash[bb_alloc->user_id % BB_HASH_SIZE];
	while (bb_ptr) {
		if ((bb_ptr          != bb_alloc) &&
		    (bb_ptr->user_id == bb_alloc->user_id)) {
			xfree(bb_alloc->account);
			bb_alloc->account   = xstrdup(bb_ptr->account);
			bb_alloc->assoc_ptr = bb_ptr->assoc_ptr;
			xfree(bb_alloc->partition);
			bb_alloc->partition = xstrdup(bb_ptr->partition);
			xfree(bb_alloc->qos);
			bb_alloc->qos       = xstrdup(bb_ptr->qos);
			bb_alloc->qos_ptr = bb_ptr->qos_ptr;
			xfree(bb_alloc->assocs);
			bb_alloc->assocs    = xstrdup(bb_ptr->assocs);
			return;
		}
		bb_ptr = bb_ptr->next;
	}

	/* Set default for this user */
	bb_alloc->partition = xstrdup(default_part_name);
	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
	assoc_rec.partition = default_part_name;
	assoc_rec.uid = bb_alloc->user_id;

	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &bb_alloc->assoc_ptr,
				    true) == SLURM_SUCCESS) {
		xfree(bb_alloc->account);
		bb_alloc->account   = xstrdup(assoc_rec.acct);
		xfree(bb_alloc->assocs);
		if (bb_alloc->assoc_ptr)
			bb_alloc->assocs =
				xstrdup_printf(",%u,", bb_alloc->assoc_ptr->id);

		assoc_mgr_get_default_qos_info(bb_alloc->assoc_ptr, &qos_rec);
		if (assoc_mgr_fill_in_qos(acct_db_conn, &qos_rec,
					  accounting_enforce,
					  &bb_alloc->qos_ptr,
					  true) == SLURM_SUCCESS) {
			xfree(bb_alloc->qos);
			if (bb_alloc->qos_ptr)
				bb_alloc->qos =
					xstrdup(bb_alloc->qos_ptr->name);
		}
	}
}

/* For a given user/partition/account, set it's assoc_ptr */
static void _set_assoc_mgr_ptrs(bb_alloc_t *bb_alloc)
{
	/* read locks on assoc */
	assoc_mgr_lock_t assoc_locks =
		{ .assoc = READ_LOCK, .qos = READ_LOCK, .user = READ_LOCK };
	slurmdb_assoc_rec_t assoc_rec;
	slurmdb_qos_rec_t qos_rec;

	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	assoc_rec.acct      = bb_alloc->account;
	assoc_rec.partition = bb_alloc->partition;
	assoc_rec.uid       = bb_alloc->user_id;
	assoc_mgr_lock(&assoc_locks);
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &bb_alloc->assoc_ptr,
				    true) == SLURM_SUCCESS) {
		xfree(bb_alloc->assocs);
		if (bb_alloc->assoc_ptr) {
			bb_alloc->assocs =
				xstrdup_printf(",%u,", bb_alloc->assoc_ptr->id);
		}
	}

	memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
	qos_rec.name = bb_alloc->qos;
	if (assoc_mgr_fill_in_qos(acct_db_conn, &qos_rec, accounting_enforce,
				  &bb_alloc->qos_ptr, true) != SLURM_SUCCESS)
		verbose("Invalid QOS name: %s",
			bb_alloc->qos);

	assoc_mgr_unlock(&assoc_locks);
}

/*
 * Determine the current actual burst buffer state.
 */
static void _load_state(bool init_config)
{
	static bool first_run = true;
	burst_buffer_pool_t *pool_ptr;
	bb_configs_t *configs;
	bb_instances_t *instances;
	bb_pools_t *pools;
	bb_sessions_t *sessions;
	bb_alloc_t *bb_alloc;
	job_record_t *job_ptr;
	int num_configs = 0, num_instances = 0, num_pools = 0, num_sessions = 0;
	int i, j, pools_inx;
	char *end_ptr = NULL;
	time_t now = time(NULL);
	uint32_t timeout;
	assoc_mgr_lock_t assoc_locks = { .assoc = READ_LOCK,
					 .qos = READ_LOCK,
					 .user = READ_LOCK };
	bool found_pool;
	bitstr_t *pools_bitmap;

	slurm_mutex_lock(&bb_state.bb_mutex);
	timeout = bb_state.bb_config.other_timeout * 1000;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/*
	 * Load the pools information
	 */
	pools = _bb_get_pools(&num_pools, &bb_state, timeout);
	if (pools == NULL) {
		error("failed to find DataWarp entries, what now?");
		return;
	}

	pools_bitmap = bit_alloc(bb_state.bb_config.pool_cnt + num_pools);
	slurm_mutex_lock(&bb_state.bb_mutex);
	if (!bb_state.bb_config.default_pool && (num_pools > 0)) {
		info("Setting DefaultPool to %s",
		     pools[0].id);
		bb_state.bb_config.default_pool = xstrdup(pools[0].id);
	}

	for (i = 0; i < num_pools; i++) {
		/* ID: "bytes" */
		if (xstrcmp(pools[i].id,
			    bb_state.bb_config.default_pool) == 0) {
			bb_state.bb_config.granularity = pools[i].granularity;
			bb_state.total_space = pools[i].quantity *
					       pools[i].granularity;
			bb_state.unfree_space = pools[i].quantity -
						pools[i].free;
			bb_state.unfree_space *= pools[i].granularity;
			continue;
		}

		found_pool = false;
		pool_ptr = bb_state.bb_config.pool_ptr;
		for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
			if (!xstrcmp(pool_ptr->name, pools[i].id)) {
				found_pool = true;
				break;
			}
		}
		if (!found_pool) {
			if (!first_run) {
				info("Newly reported pool %s",
				     pools[i].id);
			}
			bb_state.bb_config.pool_ptr
				= xrealloc(bb_state.bb_config.pool_ptr,
					   sizeof(burst_buffer_pool_t) *
					   (bb_state.bb_config.pool_cnt + 1));
			pool_ptr = bb_state.bb_config.pool_ptr +
				   bb_state.bb_config.pool_cnt;
			pool_ptr->name = xstrdup(pools[i].id);
			bb_state.bb_config.pool_cnt++;
		}

		pools_inx = pool_ptr - bb_state.bb_config.pool_ptr;
		bit_set(pools_bitmap, pools_inx);
		pool_ptr->total_space = pools[i].quantity *
					pools[i].granularity;
		pool_ptr->granularity = pools[i].granularity;
		pool_ptr->unfree_space = pools[i].quantity - pools[i].free;
		pool_ptr->unfree_space *= pools[i].granularity;
	}

	pool_ptr = bb_state.bb_config.pool_ptr;
	for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
		if (bit_test(pools_bitmap, j) || (pool_ptr->total_space == 0))
			continue;
		error("Pool %s no longer reported by system, setting size to zero",
		       pool_ptr->name);
		pool_ptr->total_space  = 0;
		pool_ptr->used_space   = 0;
		pool_ptr->unfree_space = 0;
	}
	first_run = false;
	slurm_mutex_unlock(&bb_state.bb_mutex);
	FREE_NULL_BITMAP(pools_bitmap);
	_bb_free_pools(pools, num_pools);

	/*
	 * Load the instances information
	 */
	instances = _bb_get_instances(&num_instances, &bb_state, timeout);
	if (instances == NULL) {
		log_flag(BURST_BUF, "No DataWarp instances found");
		num_instances = 0;	/* Redundant, but fixes CLANG bug */
	}
	sessions = _bb_get_sessions(&num_sessions, &bb_state, timeout);
	assoc_mgr_lock(&assoc_locks);
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_state.last_load_time = time(NULL);
	for (i = 0; i < num_sessions; i++) {
		if (!init_config) {
			bb_alloc = bb_find_name_rec(sessions[i].token,
						    sessions[i].user_id,
						    &bb_state);
			if (bb_alloc) {
				bb_alloc->seen_time = bb_state.last_load_time;
				continue;
			}
			if (difftime(now, sessions[i].created) <
			    bb_state.bb_config.other_timeout) {
				/* Newly created in other thread. Give that
				 * thread a chance to add the entry */
				continue;
			}
			error("Unexpected burst buffer found: %s",
			      sessions[i].token);
		}

		bb_alloc = bb_alloc_name_rec(&bb_state, sessions[i].token,
					     sessions[i].user_id);
		bb_alloc->create_time = sessions[i].created;
		bb_alloc->id = sessions[i].id;
		if ((sessions[i].token != NULL)   &&
		    (sessions[i].token[0] >= '0') &&
		    (sessions[i].token[0] <= '9')) {
			bb_alloc->job_id =
				strtol(sessions[i].token, &end_ptr, 10);
			job_ptr = find_job_record(bb_alloc->job_id);
			if (job_ptr) {
				bb_alloc->array_job_id = job_ptr->array_job_id;
				bb_alloc->array_task_id =job_ptr->array_task_id;
			} else {
				bb_alloc->array_task_id = NO_VAL;
			}
		}
		for (j = 0; j < num_instances; j++) {
			if (sessions[i].id != instances[j].session)
				continue;
			bb_alloc->size += instances[j].bytes;
		}
		bb_alloc->seen_time = bb_state.last_load_time;

		if (!init_config) {	/* Newly found buffer */
			_pick_alloc_account(bb_alloc);
			bb_limit_add(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state, false);
		}
		if (bb_alloc->job_id == 0)
			bb_post_persist_create(NULL, bb_alloc, &bb_state);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	assoc_mgr_unlock(&assoc_locks);
	_bb_free_sessions(sessions, num_sessions);
	_bb_free_instances(instances, num_instances);

	if (!init_config)
		return;

	/*
	 * Load the configurations information
	 * NOTE: This information is currently unused
	 */
	configs = _bb_get_configs(&num_configs, &bb_state, timeout);
	if (configs == NULL) {
		info("No DataWarp configurations found");
		num_configs = 0;
	}
	_bb_free_configs(configs, num_configs);

	_recover_bb_state();
	_apply_limits();
	bb_state.last_update_time = time(NULL);

	return;
}

static int _queue_stage_in(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *hash_dir = NULL, *job_dir = NULL, *job_pool;
	char *client_nodes_file_nid = NULL;
	char **setup_argv, **data_in_argv;
	stage_args_t *stage_args;
	int hash_inx = job_ptr->job_id % 10;
	int rc = SLURM_SUCCESS;
	bb_alloc_t *bb_alloc = NULL;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
	if (job_ptr->sched_nodes) {
		xstrfmtcat(client_nodes_file_nid, "%s/client_nids", job_dir);
		if (bb_write_nid_file(client_nodes_file_nid,
				      job_ptr->sched_nodes, job_ptr))
			xfree(client_nodes_file_nid);
	}
	setup_argv = xcalloc(20, sizeof(char *));	/* NULL terminated */
	setup_argv[0] = xstrdup("dw_wlm_cli");
	setup_argv[1] = xstrdup("--function");
	setup_argv[2] = xstrdup("setup");
	setup_argv[3] = xstrdup("--token");
	xstrfmtcat(setup_argv[4], "%u", job_ptr->job_id);
	setup_argv[5] = xstrdup("--caller");
	setup_argv[6] = xstrdup("SLURM");
	setup_argv[7] = xstrdup("--user");
	xstrfmtcat(setup_argv[8], "%u", job_ptr->user_id);
	setup_argv[9] = xstrdup("--groupid");
	xstrfmtcat(setup_argv[10], "%u", job_ptr->group_id);
	setup_argv[11] = xstrdup("--capacity");
	if (bb_job->job_pool)
		job_pool = bb_job->job_pool;
	else
		job_pool = bb_state.bb_config.default_pool;
	xstrfmtcat(setup_argv[12], "%s:%s",
		   job_pool, bb_get_size_str(bb_job->total_size));
	setup_argv[13] = xstrdup("--job");
	setup_argv[14] = bb_handle_job_script(job_ptr, bb_job);
	if (client_nodes_file_nid) {
#if defined(HAVE_NATIVE_CRAY)
		setup_argv[15] = xstrdup("--nidlistfile");
#else
		setup_argv[15] = xstrdup("--nodehostnamefile");
#endif
		setup_argv[16] = xstrdup(client_nodes_file_nid);
	}
	/*
	 * Create bb allocation for the job now. Check if it has already been
	 * created (perhaps it was created but then slurmctld restarted).
	 * bb_alloc is the structure that is state saved.
	 * If we wait until the _start_stage_in thread to create bb_alloc,
	 * we introduce a race condition where the thread could be killed
	 * (if slurmctld is shut down) before the thread creates
	 * bb_alloc. That race would mean the burst buffer isn't state saved.
	 */
	if (!(bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr))) {
		bb_alloc = bb_alloc_job(&bb_state, job_ptr, bb_job);
		bb_alloc->create_time = time(NULL);
	}
	bb_limit_add(job_ptr->user_id, bb_job->total_size, job_pool, &bb_state,
		     true);

	data_in_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	data_in_argv[0] = xstrdup("dw_wlm_cli");
	data_in_argv[1] = xstrdup("--function");
	data_in_argv[2] = xstrdup("data_in");
	data_in_argv[3] = xstrdup("--token");
	xstrfmtcat(data_in_argv[4], "%u", job_ptr->job_id);
	data_in_argv[5] = xstrdup("--job");
	data_in_argv[6] = bb_handle_job_script(job_ptr, bb_job);

	stage_args = xmalloc(sizeof(stage_args_t));
	stage_args->bb_size = bb_job->total_size;
	stage_args->job_id  = job_ptr->job_id;
	stage_args->pool    = xstrdup(job_pool);
	stage_args->user_id = job_ptr->user_id;
	stage_args->args1   = setup_argv;
	stage_args->args2   = data_in_argv;

	slurm_thread_create_detached(NULL, _start_stage_in, stage_args);

	xfree(hash_dir);
	xfree(job_dir);
	xfree(client_nodes_file_nid);
	return rc;
}

static void *_start_stage_in(void *x)
{
	stage_args_t *stage_args = (stage_args_t *) x;
	char **setup_argv, **size_argv, **data_in_argv;
	char *resp_msg = NULL, *resp_msg2 = NULL, *op = NULL;
	uint64_t real_size = 0;
	int rc = SLURM_SUCCESS, status = 0, timeout;
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job;
	bool get_real_size = false;
	DEF_TIMERS;
	pthread_t tid = pthread_self();
	track_script_rec_add(stage_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.status = &status,
		.tid = tid,
	};

	setup_argv   = stage_args->args1;
	data_in_argv = stage_args->args2;

	timeout = bb_state.bb_config.other_timeout * 1000;
	op = "setup";
	START_TIMER;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = setup_argv;
	run_command_args.script_type = "setup";
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	info("setup for job JobId=%u ran for %s",
	     stage_args->job_id, TIME_STR);

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("setup for JobId=%u terminated by slurmctld",
		     stage_args->job_id);
		xfree_array(setup_argv);
		xfree_array(data_in_argv);
		xfree(resp_msg);
		xfree(stage_args->pool);
		xfree(stage_args);
		track_script_remove(pthread_self());
		return NULL;
	}
	track_script_reset_cpid(pthread_self(), 0);

	_log_script_argv(setup_argv, resp_msg);
	slurm_mutex_lock(&bb_state.bb_mutex);
	/*
	 * The buffer's actual size may be larger than requested by the user.
	 * Remove limit here and restore limit based upon actual size below
	 * (assuming buffer allocation succeeded, or just leave it out).
	 */
	bb_limit_rem(stage_args->user_id, stage_args->bb_size, stage_args->pool,
		     &bb_state);

	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		/*
		 * Unlock bb_mutex before locking job_write_lock to avoid
		 * deadlock, since job_write_lock is always locked first.
		 */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		trigger_burst_buffer();
		error("setup for JobId=%u status:%u response:%s",
		      stage_args->job_id, status,
		      resp_msg);
		rc = SLURM_ERROR;
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(stage_args->job_id);
		if (job_ptr)
			bb_update_system_comment(job_ptr, "setup", resp_msg, 0);
		unlock_slurmctld(job_write_lock);
	} else {
		bb_job = bb_job_find(&bb_state, stage_args->job_id);
		if (!bb_job) {
			error("unable to find bb_job record for JobId=%u",
			      stage_args->job_id);
			rc = SLURM_ERROR;
		} else if (bb_job->total_size) {
			/* Restore limit based upon actual size. */
			bb_limit_add(stage_args->user_id, bb_job->total_size,
				     stage_args->pool, &bb_state, true);
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}

	if (rc == SLURM_SUCCESS) {
		timeout = bb_state.bb_config.stage_in_timeout * 1000;
		xfree(resp_msg);

		op = "dws_data_in";
		START_TIMER;
		/* Overwrite changed parameters */
		run_command_args.max_wait = timeout;
		run_command_args.script_argv = data_in_argv;
		run_command_args.script_type = "dws_data_in";
		resp_msg = run_command(&run_command_args);
		END_TIMER;
		info("dws_data_in for JobId=%u ran for %s",
		     stage_args->job_id, TIME_STR);
		if (track_script_killed(pthread_self(), status, true)) {
			/* I was killed by slurmtrack, bail out right now */
			info("dws_data_in for JobId=%u terminated by slurmctld",
			     stage_args->job_id);
			xfree_array(setup_argv);
			xfree_array(data_in_argv);
			xfree(resp_msg);
			xfree(stage_args->pool);
			xfree(stage_args);
			/*
			 * Don't need to free track_script_rec here,
			 * it is handled elsewhere since it still being tracked.
			 */
			return NULL;
		}
		track_script_reset_cpid(pthread_self(), 0);

		_log_script_argv(data_in_argv, resp_msg);
		if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
		    !strstr(resp_msg, "No matching session")) {
			trigger_burst_buffer();
			error("dws_data_in for JobId=%u status:%u response:%s",
			      stage_args->job_id, status,
			      resp_msg);
			rc = SLURM_ERROR;
			lock_slurmctld(job_write_lock);
			job_ptr = find_job_record(stage_args->job_id);
			if (job_ptr)
				bb_update_system_comment(job_ptr, "data_in",
							 resp_msg, 0);
			unlock_slurmctld(job_write_lock);
		}
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_job = bb_job_find(&bb_state, stage_args->job_id);
	if (bb_job && bb_job->req_size)
		get_real_size = true;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* Round up job buffer size based upon DW "equalize_fragments"
	 * configuration parameter */
	if (get_real_size) {
		size_argv = xcalloc(10, sizeof(char *)); /* NULL terminated */
		size_argv[0] = xstrdup("dw_wlm_cli");
		size_argv[1] = xstrdup("--function");
		size_argv[2] = xstrdup("real_size");
		size_argv[3] = xstrdup("--token");
		xstrfmtcat(size_argv[4], "%u", stage_args->job_id);
		START_TIMER;
		run_command_args.max_wait = timeout;
		run_command_args.script_argv = size_argv;
		run_command_args.script_type = "real_size";
		resp_msg2 = run_command(&run_command_args);
		END_TIMER;
		if ((DELTA_TIMER > 200000) ||	/* 0.2 secs */
		    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF))
			info("real_size ran for %s",
			     TIME_STR);

		if (track_script_killed(pthread_self(), status, true)) {
			/* I was killed by slurmtrack, bail out right now */
			info("real_size for JobId=%u terminated by slurmctld",
			     stage_args->job_id);
			xfree_array(setup_argv);
			xfree_array(data_in_argv);
			xfree(resp_msg);
			xfree(resp_msg2);
			xfree_array(size_argv);
			xfree(stage_args->pool);
			xfree(stage_args);
			/*
			 * Don't need to free track_script_rec here,
			 * it is handled elsewhere since it still being tracked.
			 */
			return NULL;
		}
		track_script_reset_cpid(pthread_self(), 0);

		/* Use resp_msg2 to preserve resp_msg for error message below */
		_log_script_argv(size_argv, resp_msg2);

		if (WIFEXITED(status) && (WEXITSTATUS(status) != 0) &&
		    resp_msg2 &&
		    (strncmp(resp_msg2, "invalid function", 16) == 0)) {
			debug("Old dw_wlm_cli does not support real_size function");
		} else if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
			error("real_size for JobId=%u status:%u response:%s",
			      stage_args->job_id,
			      status, resp_msg2);
		} else if (resp_msg2 && resp_msg2[0]) {
			json_object *j;
			struct bb_total_size *ent;
			j = json_tokener_parse(resp_msg2);
			if (j == NULL) {
				error("json parser failed on \"%s\"",
				      resp_msg2);
			} else {
				ent = _json_parse_real_size(j);
				json_object_put(j);	/* Frees json memory */
				if (ent && (ent->units == BB_UNITS_BYTES))
					real_size = ent->capacity;
				xfree(ent);
			}
		}
		xfree(resp_msg2);
		xfree_array(size_argv);
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_args->job_id);
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_args->job_id);
	} else if (rc == SLURM_SUCCESS) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = bb_job_find(&bb_state, stage_args->job_id);
		if (bb_job)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		if (bb_job && bb_job->total_size) {
			if (real_size > bb_job->req_size) {
				info("%pJ total_size increased from %"PRIu64" to %"PRIu64,
				     job_ptr,
				     bb_job->req_size, real_size);
				bb_job->total_size = real_size;
			}
			bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
			if (bb_alloc) {
				bb_alloc->state = BB_STATE_STAGED_IN;
				bb_alloc->state_time = time(NULL);
				log_flag(BURST_BUF, "Setup/stage-in complete for %pJ",
					 job_ptr);
				queue_job_scheduler();
				bb_state.last_update_time = time(NULL);
			} else {
				error("unable to find bb_alloc record for %pJ",
				      job_ptr);
			}
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
	} else {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		xstrfmtcat(job_ptr->state_desc, "%s: %s: %s",
			   plugin_type, op, resp_msg);
		job_ptr->priority = 0;	/* Hold job */
		bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
		if (bb_alloc) {
			bb_alloc->state_time = time(NULL);
			bb_state.last_update_time = time(NULL);
			if (bb_state.bb_config.flags &
			    BB_FLAG_TEARDOWN_FAILURE) {
				bb_alloc->state = BB_STATE_TEARDOWN;
				_queue_teardown(job_ptr->job_id,
						job_ptr->user_id, true);
			} else {
				bb_alloc->state = BB_STATE_ALLOCATED;
			}
		} else {
			_queue_teardown(job_ptr->job_id, job_ptr->user_id,true);
		}
	}
	unlock_slurmctld(job_write_lock);

	xfree(resp_msg);
	xfree_array(setup_argv);
	xfree_array(data_in_argv);
	xfree(stage_args->pool);
	xfree(stage_args);

	track_script_remove(tid);

	return NULL;
}

static int _queue_stage_out(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *hash_dir = NULL, *job_dir = NULL;
	char **post_run_argv, **data_out_argv;
	stage_args_t *stage_args;
	int hash_inx = bb_job->job_id % 10, rc = SLURM_SUCCESS;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, bb_job->job_id);

	data_out_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	data_out_argv[0] = xstrdup("dw_wlm_cli");
	data_out_argv[1] = xstrdup("--function");
	data_out_argv[2] = xstrdup("data_out");
	data_out_argv[3] = xstrdup("--token");
	xstrfmtcat(data_out_argv[4], "%u", bb_job->job_id);
	data_out_argv[5] = xstrdup("--job");
	data_out_argv[6] = bb_handle_job_script(job_ptr, bb_job);

	post_run_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	post_run_argv[0] = xstrdup("dw_wlm_cli");
	post_run_argv[1] = xstrdup("--function");
	post_run_argv[2] = xstrdup("post_run");
	post_run_argv[3] = xstrdup("--token");
	xstrfmtcat(post_run_argv[4], "%u", bb_job->job_id);
	post_run_argv[5] = xstrdup("--job");
	post_run_argv[6] = bb_handle_job_script(job_ptr, bb_job);

	stage_args = xmalloc(sizeof(stage_args_t));
	stage_args->args1   = data_out_argv;
	stage_args->args2   = post_run_argv;
	stage_args->job_id  = bb_job->job_id;
	stage_args->user_id = bb_job->user_id;

	slurm_thread_create_detached(NULL, _start_stage_out, stage_args);

	xfree(hash_dir);
	xfree(job_dir);
	return rc;
}

static void *_start_stage_out(void *x)
{
	stage_args_t *stage_args = (stage_args_t *)x;
	char **post_run_argv, **data_out_argv, *resp_msg = NULL, *op = NULL;
	int rc = SLURM_SUCCESS, status = 0, timeout;
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job = NULL;
	DEF_TIMERS
	pthread_t tid = pthread_self();
	track_script_rec_add(stage_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.status = &status,
		.tid = tid,
	};

	data_out_argv = stage_args->args1;
	post_run_argv = stage_args->args2;

	timeout = bb_state.bb_config.other_timeout * 1000;
	op = "dws_post_run";
	START_TIMER;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = post_run_argv;
	run_command_args.script_type = "dws_post_run";
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	if ((DELTA_TIMER > 500000) ||	/* 0.5 secs */
	    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF)) {
		info("dws_post_run for JobId=%u ran for %s",
		     stage_args->job_id, TIME_STR);
	}

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("dws_post_run for JobId=%u terminated by slurmctld",
		     stage_args->job_id);
		xfree_array(post_run_argv);
		xfree_array(data_out_argv);
		xfree(resp_msg);
		xfree(stage_args->pool);
		xfree(stage_args);
		track_script_remove(pthread_self());
		return NULL;
	}
	track_script_reset_cpid(pthread_self(), 0);

	_log_script_argv(post_run_argv, resp_msg);
	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_args->job_id);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		trigger_burst_buffer();
		error("dws_post_run for JobId=%u status:%u response:%s",
		      stage_args->job_id, status,
		      resp_msg);
		rc = SLURM_ERROR;
		if (job_ptr) {
			job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s: post_run: %s",
				   plugin_type, resp_msg);
			bb_update_system_comment(job_ptr, "post_run",
						 resp_msg, 1);
		}
	}
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_args->job_id);
	} else {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = _get_bb_job(job_ptr);
		if (bb_job)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGING_OUT);
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}
	unlock_slurmctld(job_write_lock);

	if (rc == SLURM_SUCCESS) {
		timeout = bb_state.bb_config.stage_out_timeout * 1000;
		op = "dws_data_out";
		START_TIMER;
		xfree(resp_msg);
		run_command_args.max_wait = timeout;
		run_command_args.script_argv = data_out_argv;
		run_command_args.script_type = "dws_data_out";
		resp_msg = run_command(&run_command_args);
		END_TIMER;
		if ((DELTA_TIMER > 1000000) ||	/* 10 secs */
		    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF)) {
			info("dws_data_out for JobId=%u ran for %s",
			     stage_args->job_id,
			     TIME_STR);
		}

		if (track_script_killed(pthread_self(), status, true)) {
			/* I was killed by slurmtrack, bail out right now */
			info("dws_data_out for JobId=%u terminated by slurmctld",
			     stage_args->job_id);
			xfree_array(post_run_argv);
			xfree_array(data_out_argv);
			xfree(resp_msg);
			xfree(stage_args->pool);
			xfree(stage_args);
			track_script_remove(pthread_self());
			return NULL;
		}
		track_script_reset_cpid(pthread_self(), 0);

		_log_script_argv(data_out_argv, resp_msg);
		if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
		    !strstr(resp_msg, "No matching session")) {
			trigger_burst_buffer();
			error("dws_data_out for JobId=%u status:%u response:%s",
			      stage_args->job_id,
			      status, resp_msg);
			rc = SLURM_ERROR;
			lock_slurmctld(job_write_lock);
			job_ptr = find_job_record(stage_args->job_id);
			if (job_ptr) {
				job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
				xfree(job_ptr->state_desc);
				xstrfmtcat(job_ptr->state_desc,
					   "%s: stage-out: %s",
					   plugin_type, resp_msg);
				bb_update_system_comment(job_ptr, "data_out",
							 resp_msg, 1);
			}
			unlock_slurmctld(job_write_lock);
		}
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_args->job_id);
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_args->job_id);
	} else {
		if (rc != SLURM_SUCCESS) {
			job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s: %s: %s",
				   plugin_type, op, resp_msg);
		} else {
			job_ptr->job_state &= (~JOB_STAGE_OUT);
			xfree(job_ptr->state_desc);
			last_job_update = time(NULL);
		}
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = _get_bb_job(job_ptr);
		if ((rc == SLURM_SUCCESS) && bb_job)
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
		bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
		if (bb_alloc) {
			if (rc == SLURM_SUCCESS) {
				log_flag(BURST_BUF, "Stage-out/post-run complete for %pJ",
					 job_ptr);
				/* bb_alloc->state = BB_STATE_STAGED_OUT; */
				bb_alloc->state = BB_STATE_TEARDOWN;
				bb_alloc->state_time = time(NULL);
			} else {
				if (bb_state.bb_config.flags &
				    BB_FLAG_TEARDOWN_FAILURE) {
					bb_alloc->state = BB_STATE_TEARDOWN;
					_queue_teardown(stage_args->job_id,
							stage_args->user_id,
							false);
				} else
					bb_alloc->state = BB_STATE_STAGED_IN;
				log_flag(BURST_BUF, "Stage-out failed for %pJ",
					 job_ptr);
			}
			bb_state.last_update_time = time(NULL);
		} else if (bb_job && bb_job->total_size) {
			error("unable to find bb record for %pJ",
			      job_ptr);
		}
		if (rc == SLURM_SUCCESS) {
			_queue_teardown(stage_args->job_id, stage_args->user_id,
					false);
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}
	unlock_slurmctld(job_write_lock);

	xfree(resp_msg);
	xfree_array(post_run_argv);
	xfree_array(data_out_argv);
	xfree(stage_args);

	track_script_remove(pthread_self());

	return NULL;
}

static void _queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry)
{
	struct stat buf;
	char *hash_dir = NULL, *job_script = NULL;
	char **teardown_argv;
	stage_args_t *teardown_args;
	int fd, hash_inx = job_id % 10;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	xstrfmtcat(job_script, "%s/job.%u/script", hash_dir, job_id);
	if (stat(job_script, &buf) == -1) {
		xfree(job_script);
		xstrfmtcat(job_script, "%s/burst_buffer_script",
			   slurm_conf.state_save_location);
		if (stat(job_script, &buf) == -1) {
			fd = creat(job_script, 0755);
			if (fd >= 0) {
				int len;
				char *dummy_script = "#!/bin/bash\nexit 0\n";
				len = strlen(dummy_script) + 1;
				if (write(fd, dummy_script, len) != len) {
					verbose("write(%s): %m",
						job_script);
				}
				close(fd);
			}
		}
	}

	teardown_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	teardown_argv[0] = xstrdup("dw_wlm_cli");
	teardown_argv[1] = xstrdup("--function");
	teardown_argv[2] = xstrdup("teardown");
	teardown_argv[3] = xstrdup("--token");
	xstrfmtcat(teardown_argv[4], "%u", job_id);
	teardown_argv[5] = xstrdup("--job");
	teardown_argv[6] = xstrdup(job_script);
	if (hurry)
		teardown_argv[7] = xstrdup("--hurry");

	teardown_args = xmalloc(sizeof(stage_args_t));
	teardown_args->job_id  = job_id;
	teardown_args->user_id = user_id;
	teardown_args->args1   = teardown_argv;

	slurm_thread_create_detached(NULL, _start_teardown, teardown_args);

	xfree(hash_dir);
	xfree(job_script);
}

static void *_start_teardown(void *x)
{
	static uint32_t previous_job_id = 0;
	stage_args_t *teardown_args = (stage_args_t *)x;
	char **teardown_argv, *resp_msg = NULL;
	int status = 0, timeout;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job = NULL;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;
	bool hurry;
	pthread_t tid = pthread_self();
	track_script_rec_add(teardown_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.status = &status,
		.tid = tid,
	};

	teardown_argv = teardown_args->args1;

	if (previous_job_id == teardown_args->job_id)
		sleep(5);
	previous_job_id = teardown_args->job_id;

	START_TIMER;
	timeout = bb_state.bb_config.other_timeout * 1000;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = teardown_argv;
	run_command_args.script_type = "teardown";
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	info("teardown for JobId=%u ran for %s",
	     teardown_args->job_id, TIME_STR);

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("teardown for JobId=%u terminated by slurmctld",
		     teardown_args->job_id);
		xfree(resp_msg);
		xfree_array(teardown_argv);
		xfree(teardown_args);
		track_script_remove(pthread_self());
		return NULL;
	}
	/* track_script_reset_cpid(pthread_self(), 0); */

	_log_script_argv(teardown_argv, resp_msg);

	/*
	 * "Teardown" is run at every termination of every job that _might_
	 * have a burst buffer, so an error of "token not found" should be
	 * fairly common and not indicative of a problem.
	 */
	if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
	    (!resp_msg ||
	     (!strstr(resp_msg, "No matching session") &&
	      !strstr(resp_msg, "token not found")))) {
		lock_slurmctld(job_write_lock);
		slurm_mutex_lock(&bb_state.bb_mutex);
		job_ptr = find_job_record(teardown_args->job_id);
		if (job_ptr &&
		    (bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr))) {
			bb_alloc->state = BB_STATE_TEARDOWN_FAIL;
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);

		trigger_burst_buffer();
		error("teardown for JobId=%u status:%u response:%s",
		      teardown_args->job_id, status,
		      resp_msg);


		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(teardown_args->job_id);
		if (job_ptr) {
			job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s: teardown: %s",
				   plugin_type, resp_msg);
			bb_update_system_comment(job_ptr, "teardown",
						 resp_msg, 0);
		}
		unlock_slurmctld(job_write_lock);


		if (!xstrcmp(teardown_argv[7], "--hurry"))
			hurry = true;
		else
			hurry = false;
		_queue_teardown(teardown_args->job_id, teardown_args->user_id,
				hurry);
	} else {
		lock_slurmctld(job_write_lock);
		slurm_mutex_lock(&bb_state.bb_mutex);
		job_ptr = find_job_record(teardown_args->job_id);
		_purge_bb_files(teardown_args->job_id, job_ptr);
		if (job_ptr) {
			if ((bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr))){
				bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
					     bb_alloc->pool, &bb_state);
				(void) bb_free_alloc_rec(&bb_state, bb_alloc);
			}
			if ((bb_job = _get_bb_job(job_ptr))) {
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_COMPLETE);
				bb_job_del(&bb_state, bb_job->job_id);
			}
			job_ptr->job_state &= (~JOB_STAGE_OUT);
			if (!IS_JOB_PENDING(job_ptr) &&	/* No email if requeue */
			    (job_ptr->mail_type & MAIL_JOB_STAGE_OUT)) {
				/*
				 * NOTE: If a job uses multiple burst buffer
				 * plugins, the message will be sent after the
				 * teardown completes in the first plugin
				 */
				mail_job_info(job_ptr, MAIL_JOB_STAGE_OUT);
				job_ptr->mail_type &= (~MAIL_JOB_STAGE_OUT);
			}
		} else {
			/*
			 * This will happen when slurmctld restarts and needs
			 * to clear vestigial buffers
			 */
			char buf_name[32];
			snprintf(buf_name, sizeof(buf_name), "%u",
				 teardown_args->job_id);
			bb_alloc = bb_find_name_rec(buf_name,
						    teardown_args->user_id,
						    &bb_state);
			if (bb_alloc) {
				bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
					     bb_alloc->pool, &bb_state);
				(void) bb_free_alloc_rec(&bb_state, bb_alloc);
			}

		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	}

	xfree(resp_msg);
	xfree_array(teardown_argv);
	xfree(teardown_args);

	track_script_remove(pthread_self());

	return NULL;
}

/* Handle timeout of burst buffer events:
 * 1. Purge per-job burst buffer records when the stage-out has completed and
 *    the job has been purged from Slurm
 * 2. Test for StageInTimeout events
 * 3. Test for StageOutTimeout events
 */
static void _timeout_bb_rec(void)
{
	bb_alloc_t **bb_pptr, *bb_alloc = NULL;
	job_record_t *job_ptr;
	int i;

	if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY)
		return;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_pptr = &bb_state.bb_ahash[i];
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			if (((bb_alloc->seen_time + TIME_SLOP) <
			     bb_state.last_load_time) &&
			    (bb_alloc->state == BB_STATE_TEARDOWN)) {
				/*
				 * Teardown likely complete, but bb_alloc state
				 * not yet updated; skip the record
				 */
			} else if ((bb_alloc->seen_time + TIME_SLOP) <
				   bb_state.last_load_time) {
				assoc_mgr_lock_t assoc_locks =
					{ .assoc = READ_LOCK,
					  .qos = READ_LOCK };
				/*
				 * assoc_mgr needs locking to call
				 * bb_post_persist_delete
				 */
				if (bb_alloc->job_id == 0) {
					info("Persistent burst buffer %s purged",
					     bb_alloc->name);
				} else
					log_flag(BURST_BUF, "burst buffer for JobId=%u purged",
						 bb_alloc->job_id);
				bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
					     bb_alloc->pool, &bb_state);

				assoc_mgr_lock(&assoc_locks);
				bb_post_persist_delete(bb_alloc, &bb_state);
				assoc_mgr_unlock(&assoc_locks);

				*bb_pptr = bb_alloc->next;
				bb_free_alloc_buf(bb_alloc);
				break;
			} else if (bb_alloc->state == BB_STATE_COMPLETE) {
				job_ptr = find_job_record(bb_alloc->job_id);
				if (!job_ptr || IS_JOB_PENDING(job_ptr)) {
					/* Job purged or BB preempted */
					*bb_pptr = bb_alloc->next;
					bb_free_alloc_buf(bb_alloc);
					break;
				}
			}
			bb_pptr = &bb_alloc->next;
			bb_alloc = bb_alloc->next;
		}
	}
}

/* Perform basic burst_buffer option validation */
static int _parse_bb_opts(job_desc_msg_t *job_desc, uint64_t *bb_size,
			  uid_t submit_uid)
{
	char *bb_script, *save_ptr = NULL;
	char *bb_name = NULL, *bb_pool, *capacity;
	char *end_ptr = NULL, *sub_tok, *tok;
	uint64_t tmp_cnt, swap_cnt = 0;
	int rc = SLURM_SUCCESS;
	bool enable_persist = false, have_bb = false, have_stage_out = false;

	xassert(bb_size);
	*bb_size = 0;

	if (validate_operator(submit_uid) ||
	    (bb_state.bb_config.flags & BB_FLAG_ENABLE_PERSISTENT))
		enable_persist = true;

	if (job_desc->script)
		rc = _xlate_batch(job_desc);
	else
		rc = _xlate_interactive(job_desc);
	if ((rc != SLURM_SUCCESS) || (!job_desc->burst_buffer))
		return rc;

	bb_script = xstrdup(job_desc->burst_buffer);
	tok = strtok_r(bb_script, "\n", &save_ptr);
	while (tok) {
		uint32_t bb_flag = 0;
		tmp_cnt = 0;
		if (tok[0] != '#')
			break;	/* Quit at first non-comment */

		if ((tok[1] == 'B') && (tok[2] == 'B'))
			bb_flag = BB_FLAG_BB_OP;
		else if ((tok[1] == 'D') && (tok[2] == 'W'))
			bb_flag = BB_FLAG_DW_OP;

		/*
		 * Effective Slurm v18.08 and CLE6.0UP06 the create_persistent
		 * and destroy_persistent functions are directly supported by
		 * dw_wlm_cli. Support "#BB" format for backward compatibility.
		 */
		if (bb_flag == BB_FLAG_BB_OP) {
			tok += 3;
			while (isspace(tok[0]))
				tok++;
			if (!xstrncmp(tok, "create_persistent", 17) &&
			    !enable_persist) {
				info("User %d disabled from creating persistent burst buffer",
				     submit_uid);
				rc = ESLURM_BURST_BUFFER_PERMISSION;
				break;
			} else if (!xstrncmp(tok, "create_persistent", 17)) {
				have_bb = true;
				bb_name = NULL;
				bb_pool = NULL;
				if ((sub_tok = strstr(tok, "capacity="))) {
					tmp_cnt = bb_get_size_num(sub_tok+9, 1);
				}
				if (tmp_cnt == 0)
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				if ((sub_tok = strstr(tok, "name="))) {
					bb_name = xstrdup(sub_tok + 5);
					if ((sub_tok = strchr(bb_name, ' ')))
						sub_tok[0] = '\0';
				} else {
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				}
				if (!bb_name ||
				    ((bb_name[0] >= '0') &&
				     (bb_name[0] <= '9')))
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				xfree(bb_name);
				if ((sub_tok = strstr(tok, "pool="))) {
					bb_pool = xstrdup(sub_tok + 5);
					if ((sub_tok = strchr(bb_pool, ' ')))
						sub_tok[0] = '\0';
				}
				if (!bb_valid_pool_test(&bb_state, bb_pool))
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				*bb_size += _set_granularity(tmp_cnt, bb_pool);
				xfree(bb_pool);
				if (rc != SLURM_SUCCESS)
					break;
			} else if (!xstrncmp(tok, "destroy_persistent", 18) &&
				   !enable_persist) {
				info("User %d disabled from destroying persistent burst buffer",
				     submit_uid);
				rc = ESLURM_BURST_BUFFER_PERMISSION;
				break;
			} else if (!xstrncmp(tok, "destroy_persistent", 18)) {
				have_bb = true;
				if (!(sub_tok = strstr(tok, "name="))) {
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
					break;
				}
			} else {
				/* Ignore other (future) options */
			}
		}
		if (bb_flag == BB_FLAG_DW_OP) {
			tok += 3;
			while (isspace(tok[0]))
				tok++;
			if (!xstrncmp(tok, "jobdw", 5) &&
			    (capacity = strstr(tok, "capacity="))) {
				bb_pool = NULL;
				have_bb = true;
				tmp_cnt = bb_get_size_num(capacity + 9, 1);
				if (tmp_cnt == 0) {
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
					break;
				}
				if ((sub_tok = strstr(tok, "pool="))) {
					bb_pool = xstrdup(sub_tok + 5);
					if ((sub_tok = strchr(bb_pool, ' ')))
						sub_tok[0] = '\0';
				}
				if (!bb_valid_pool_test(&bb_state, bb_pool))
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				*bb_size += _set_granularity(tmp_cnt, bb_pool);
				xfree(bb_pool);
			} else if (!xstrncmp(tok, "persistentdw", 12)) {
				have_bb = true;
			} else if (!xstrncmp(tok, "swap", 4)) {
				bb_pool = NULL;
				have_bb = true;
				tok += 4;
				while (isspace(tok[0]) && (tok[0] != '\0'))
					tok++;
				swap_cnt += strtol(tok, &end_ptr, 10);
				if ((job_desc->max_nodes == 0) ||
				    (job_desc->max_nodes == NO_VAL)) {
					info("user %u submitted job with swap space specification, but no max node count specification",
					     job_desc->user_id);
					if (job_desc->min_nodes == NO_VAL)
						job_desc->min_nodes = 1;
					job_desc->max_nodes =
						job_desc->min_nodes;
				}
				tmp_cnt = swap_cnt * job_desc->max_nodes;
				if ((sub_tok = strstr(tok, "pool="))) {
					bb_pool = xstrdup(sub_tok + 5);
					if ((sub_tok = strchr(bb_pool, ' ')))
						sub_tok[0] = '\0';
				}
				if (!bb_valid_pool_test(&bb_state, bb_pool))
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
				*bb_size += _set_granularity(tmp_cnt, bb_pool);
				xfree(bb_pool);
			} else if (!xstrncmp(tok, "stage_out", 9)) {
				have_stage_out = true;
			} else if (!xstrncmp(tok, "create_persistent", 17) ||
				   !xstrncmp(tok, "destroy_persistent", 18)) {
				/*
				 * Disable support until Slurm v18.08 to prevent
				 * user directed persistent burst buffer changes
				 * outside of Slurm control.
				 */
				rc = ESLURM_BURST_BUFFER_PERMISSION;
				break;

			}
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(bb_script);

	if (!have_bb)
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;

	if (!have_stage_out) {
		/* prevent sending stage out email */
		job_desc->mail_type &= (~MAIL_JOB_STAGE_OUT);
	}

	return rc;
}

/* Copy a batch job's burst_buffer options into a separate buffer.
 * merge continued lines into a single line */
static int _xlate_batch(job_desc_msg_t *job_desc)
{
	char *script, *save_ptr = NULL, *tok;
	int line_type, prev_type = LINE_OTHER;
	bool is_cont = false, has_space = false;
	int len, rc = SLURM_SUCCESS;

	/*
	 * Any command line --bb options get added to the script
	 */
	if (job_desc->burst_buffer) {
		rc = _xlate_interactive(job_desc);
		if (rc != SLURM_SUCCESS)
			return rc;
		run_command_add_to_script(&job_desc->script,
					  job_desc->burst_buffer);
		xfree(job_desc->burst_buffer);
	}

	script = xstrdup(job_desc->script);
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break;	/* Quit at first non-comment */

		if ((tok[1] == 'B') && (tok[2] == 'B'))
			line_type = LINE_BB;
		else if ((tok[1] == 'D') && (tok[2] == 'W'))
			line_type = LINE_DW;
		else
			line_type = LINE_OTHER;

		if (line_type == LINE_OTHER) {
			is_cont = false;
		} else {
			if (is_cont) {
				if (line_type != prev_type) {
					/*
					 * Mixing "#DW" with "#BB" on same
					 * (continued) line, error
					 */
					rc =ESLURM_INVALID_BURST_BUFFER_REQUEST;
					break;
				}
				tok += 3; 	/* Skip "#DW" or "#BB" */
				while (has_space && isspace(tok[0]))
					tok++;	/* Skip duplicate spaces */
			} else if (job_desc->burst_buffer) {
				xstrcat(job_desc->burst_buffer, "\n");
			}
			prev_type = line_type;

			len = strlen(tok);
			if (tok[len - 1] == '\\') {
				has_space = isspace(tok[len - 2]);
				tok[strlen(tok) - 1] = '\0';
				is_cont = true;
			} else {
				is_cont = false;
			}
			xstrcat(job_desc->burst_buffer, tok);
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(script);
	if (rc != SLURM_SUCCESS)
		xfree(job_desc->burst_buffer);
	return rc;
}

/* Parse simple interactive burst_buffer options into an format identical to
 * burst_buffer options in a batch script file */
static int _xlate_interactive(job_desc_msg_t *job_desc)
{
	char *access = NULL, *bb_copy = NULL, *capacity = NULL, *pool = NULL;
	char *swap = NULL, *type = NULL;
	char *end_ptr = NULL, *sep, *tok;
	uint64_t buf_size = 0, swap_cnt = 0;
	int i, rc = SLURM_SUCCESS, tok_len;

	if (!job_desc->burst_buffer || (job_desc->burst_buffer[0] == '#'))
		return rc;

	if (strstr(job_desc->burst_buffer, "create_persistent") ||
	    strstr(job_desc->burst_buffer, "destroy_persistent")) {
		/* Create or destroy of persistent burst buffers NOT supported
		 * via --bb option. Use --bbf or a batch script instead. */
		return ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}

	bb_copy = xstrdup(job_desc->burst_buffer);
	if ((tok = strstr(bb_copy, "access="))) {
		access = xstrdup(tok + 7);
		sep = strchr(access, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(access, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(access) + 7;
		memset(tok, ' ', tok_len);
	}
	if ((access == NULL) &&		/* Not set above with "access=" */
	    (tok = strstr(bb_copy, "access_mode="))) {
		access = xstrdup(tok + 12);
		sep = strchr(access, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(access, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(access) + 12;
		memset(tok, ' ', tok_len);
	}

	if ((tok = strstr(bb_copy, "capacity="))) {
		buf_size = bb_get_size_num(tok + 9, 1);
		if (buf_size == 0) {
			rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			goto fini;
		}
		capacity = xstrdup(tok + 9);
		sep = strchr(capacity, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(capacity, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(capacity) + 9;
		memset(tok, ' ', tok_len);
	}


	if ((tok = strstr(bb_copy, "pool="))) {
		pool = xstrdup(tok + 5);
		sep = strchr(pool, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(pool, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(pool) + 5;
		memset(tok, ' ', tok_len);
	}

	if ((tok = strstr(bb_copy, "swap="))) {
		swap_cnt = strtol(tok + 5, &end_ptr, 10);
		if (swap_cnt == 0) {
			rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			goto fini;
		}
		swap = xstrdup(tok + 5);
		sep = strchr(swap, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(swap, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(swap) + 5;
		memset(tok, ' ', tok_len);
	}

	if ((tok = strstr(bb_copy, "type="))) {
		type = xstrdup(tok + 5);
		sep = strchr(type, ',');
		if (sep)
			sep[0] = '\0';
		sep = strchr(type, ' ');
		if (sep)
			sep[0] = '\0';
		tok_len = strlen(type) + 5;
		memset(tok, ' ', tok_len);
	}

	if (rc == SLURM_SUCCESS) {
		/* Look for vestigial content. Treating this as an error would
		 * prevent backward compatibility. Just log it for now. */
		for (i = 0; bb_copy[i]; i++) {
			if (isspace(bb_copy[i]))
				continue;
			verbose("Unrecognized --bb content: %s",
				bb_copy + i);
//			rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
//			goto fini;
		}
	}

	if (rc == SLURM_SUCCESS)
		xfree(job_desc->burst_buffer);
	if ((rc == SLURM_SUCCESS) && (swap_cnt || buf_size)) {
		if (swap_cnt) {
			xstrfmtcat(job_desc->burst_buffer,
				   "#DW swap %"PRIu64"GiB", swap_cnt);
			if (pool) {
				xstrfmtcat(job_desc->burst_buffer,
					   " pool=%s", pool);
			}
		}
		if (buf_size) {
			if (job_desc->burst_buffer)
				xstrfmtcat(job_desc->burst_buffer, "\n");
			xstrfmtcat(job_desc->burst_buffer,
				   "#DW jobdw capacity=%s",
				   bb_get_size_str(buf_size));
			if (access) {
				xstrfmtcat(job_desc->burst_buffer,
					   " access_mode=%s", access);
			}
			if (pool) {
				xstrfmtcat(job_desc->burst_buffer,
					   " pool=%s", pool);
			}
			if (type) {
				xstrfmtcat(job_desc->burst_buffer,
					   " type=%s", type);
			}
		}
	}

fini:	xfree(access);
	xfree(bb_copy);
	xfree(capacity);
	xfree(pool);
	xfree(swap);
	xfree(type);
	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Read and validate configuration file here. Spawn thread to
 * periodically read Datawarp state.
 */
extern int init(void)
{
	slurm_mutex_init(&bb_state.bb_mutex);
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, (char *)plugin_type); /* Removes "const" */
	_test_config();
	log_flag(BURST_BUF, "");
	bb_alloc_cache(&bb_state);
	run_command_init();
	slurm_thread_create(&bb_state.bb_thread, _bb_agent, NULL);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory and shutdown
 * threads.
 */
extern int fini(void)
{
	int pc, last_pc = 0;

	run_command_shutdown();
	while ((pc = run_command_count()) > 0) {
		if ((last_pc != 0) && (last_pc != pc)) {
			info("waiting for %d running processes",
			     pc);
		}
		last_pc = pc;
		usleep(100000);
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "");

	slurm_mutex_lock(&bb_state.term_mutex);
	bb_state.term_flag = true;
	slurm_cond_signal(&bb_state.term_cond);
	slurm_mutex_unlock(&bb_state.term_mutex);

	if (bb_state.bb_thread) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		pthread_join(bb_state.bb_thread, NULL);
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_state.bb_thread = 0;
	}
	bb_clear_config(&bb_state.bb_config, true);
	bb_clear_cache(&bb_state);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

static void _pre_queue_stage_out(job_record_t *job_ptr, bb_job_t *bb_job)
{
	bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_POST_RUN);
	job_ptr->job_state |= JOB_STAGE_OUT;
	xfree(job_ptr->state_desc);
	xstrfmtcat(job_ptr->state_desc, "%s: Stage-out in progress",
		   plugin_type);
	_queue_stage_out(job_ptr, bb_job);
}

/*
 * This function should only be called from _purge_vestigial_bufs().
 * We need to reset the burst buffer state and restart any threads that may
 * have been running before slurmctld was shutdown, depending on the state
 * that the burst buffer is in.
 */
static void _recover_job_bb(job_record_t *job_ptr, bb_alloc_t *bb_alloc,
			    time_t defer_time)
{
	bb_job_t *bb_job;
	uint16_t job_bb_state = bb_state_num(job_ptr->burst_buffer_state);

	/*
	 * Call _get_bb_job() to create a cache of the job's burst buffer info,
	 * including the state. Lots of functions will call this so do it now to
	 * create the cache, and we may need to change the burst buffer state.
	 * The job burst buffer state is set in job_ptr and in bb_job.
	 * bb_alloc is used for persistent burst buffers, so bb_alloc->state
	 * isn't used for job burst buffers.
	 */
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		/* This shouldn't happen. */
		error("%s: %pJ does not have a burst buffer specification, tearing down vestigial burst buffer.",
		      __func__, job_ptr);
		_queue_teardown(bb_alloc->job_id, bb_alloc->user_id, false);
		return;
	}

	switch(job_bb_state) {
		/*
		 * First 4 states are specific to persistent burst buffers.
		 * We shouldn't get here since _purge_vestigial_bufs() handles
		 * persistent burst buffers.
		 */
		case BB_STATE_ALLOCATING:
		case BB_STATE_ALLOCATED:
		case BB_STATE_DELETING:
		case BB_STATE_DELETED:
			error("%s: Unexpected burst buffer state %s for %pJ",
			      __func__, job_ptr->burst_buffer_state, job_ptr);
			break;
		/* Pending states for jobs: */
		case BB_STATE_STAGING_IN:
		case BB_STATE_STAGED_IN:
		case BB_STATE_ALLOC_REVOKE:
			/*
			 * We do not know the state of file staging,
			 * so teardown the buffer and defer the job
			 * for at least 60 seconds (for the teardown).
			 * Also set the burst buffer state back to PENDING.
			 */
			log_flag(BURST_BUF, "Purging buffer for pending %pJ",
				 job_ptr);
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
			_queue_teardown(bb_alloc->job_id,
					bb_alloc->user_id, true);
			if (job_ptr->details &&
			    (job_ptr->details->begin_time < defer_time)){
				job_ptr->details->begin_time =
					defer_time;
			}
			break;
		/* Running states for jobs: */
		case BB_STATE_PRE_RUN:
			/*
			 * slurmctld will call bb_g_job_begin() which will
			 * handle burst buffers in this state.
			 */
			break;
		case BB_STATE_RUNNING:
		case BB_STATE_SUSPEND:
			/* Nothing to do here. */
			break;
		/* Post running states for jobs: */
		case BB_STATE_POST_RUN:
		case BB_STATE_STAGING_OUT:
		case BB_STATE_STAGED_OUT:
			log_flag(BURST_BUF, "Restarting burst buffer stage out for %pJ",
				 job_ptr);
			/*
			 * _pre_queue_stage_out() sets the burst buffer state
			 * correctly and restarts the needed thread.
			 */
			_pre_queue_stage_out(job_ptr, bb_job);
			break;
		case BB_STATE_TEARDOWN:
		case BB_STATE_TEARDOWN_FAIL:
			log_flag(BURST_BUF, "Restarting burst buffer teardown for %pJ",
				 job_ptr);
			_queue_teardown(bb_alloc->job_id,
					bb_alloc->user_id, false);
			break;
		case BB_STATE_COMPLETE:
			/*
			 * We shouldn't get here since the bb_alloc record is
			 * removed when the job's bb state is set to
			 * BB_STATE_COMPLETE during teardown.
			 */
			log_flag(BURST_BUF, "Clearing burst buffer for completed job %pJ",
				 job_ptr);
			/*
			 * Subtract the space this burst buffer was allocated
			 * since _load_state() calls _apply_limits()
			 * which calls bb_limit_add() for all burst buffers.
			 */
			bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state);
			(void) bb_free_alloc_rec(&bb_state, bb_alloc);
			break;
		default:
			error("%s: Invalid job burst buffer state %s for %pJ",
			      __func__, job_ptr->burst_buffer_state, job_ptr);
			break;
	}
}

/* Identify and purge any vestigial buffers (i.e. we have a job buffer, but
 * the matching job is either gone or completed OR we have a job buffer and a
 * pending job, but don't know the status of stage-in) */
static void _purge_vestigial_bufs(void)
{
	bb_alloc_t *bb_alloc = NULL;
	time_t defer_time = time(NULL) + 60;
	int i;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			job_record_t *job_ptr = NULL;
			if (bb_alloc->job_id)
				job_ptr = find_job_record(bb_alloc->job_id);
			if (bb_alloc->job_id == 0) {
				/* Persistent buffer, do not purge */
			} else if (!job_ptr) {
				info("Purging vestigial buffer for JobId=%u",
				     bb_alloc->job_id);
				_queue_teardown(bb_alloc->job_id,
						bb_alloc->user_id, false);
			} else {
				_recover_job_bb(job_ptr, bb_alloc, defer_time);
			}
			bb_alloc = bb_alloc->next;
		}
	}
}

/*
 * Return the total burst buffer size in MB
 */
extern uint64_t bb_p_get_system_size(void)
{
	uint64_t size = 0;

	slurm_mutex_lock(&bb_state.bb_mutex);
	size = bb_state.total_space / (1024 * 1024);	/* bytes to MB */
	slurm_mutex_unlock(&bb_state.bb_mutex);
	return size;
}

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a Slurm errno.
 */
extern int bb_p_load_state(bool init_config)
{
	if (!init_config)
		return SLURM_SUCCESS;

	/* In practice the Cray APIs are too slow to run inline on each
	 * scheduling cycle. Do so on a periodic basis from _bb_agent(). */
	log_flag(BURST_BUF, "");
	_load_state(init_config);	/* Has own locking */
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_set_tres_pos(&bb_state);
	_purge_vestigial_bufs();
	slurm_mutex_unlock(&bb_state.bb_mutex);

	_save_bb_state();	/* Has own locks excluding file write */

	return SLURM_SUCCESS;
}

/*
 * Return string containing current burst buffer status
 * argc IN - count of status command arguments
 * argv IN - status command arguments
 * RET status string, release memory using xfree()
 */
extern char *bb_p_get_status(uint32_t argc, char **argv)
{
	char *status_resp, **script_argv;
	int i, status = 0;
	run_command_args_t run_command_args = {
		.max_wait = 2000,
		.script_path = bb_state.bb_config.get_sys_status,
		.script_type = "dwstat",
		.status = &status,
	};

	script_argv = xcalloc((argc + 2), sizeof(char *));
	script_argv[0] = "dwstat";
	for (i = 0; i < argc; i++)
		script_argv[i + 1] = argv[i];
	run_command_args.script_argv = script_argv;
	status_resp = run_command(&run_command_args);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		xfree(status_resp);
		status_resp = xstrdup("Error running dwstat\n");
	}
	xfree(script_argv);

	return status_resp;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_reconfig(void)
{
	char *old_default_pool;
	int i;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "");
	old_default_pool = bb_state.bb_config.default_pool;
	bb_state.bb_config.default_pool = NULL;
	bb_load_config(&bb_state, (char *)plugin_type); /* Remove "const" */
	if (!bb_state.bb_config.default_pool)
		bb_state.bb_config.default_pool = old_default_pool;
	else
		xfree(old_default_pool);
	_test_config();
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* reconfig is the place we make sure the pointers are correct */
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc_t *bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			_set_assoc_mgr_ptrs(bb_alloc);
			bb_alloc = bb_alloc->next;
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a Slurm errno.
 */
extern int bb_p_state_pack(uid_t uid, buf_t *buffer, uint16_t protocol_version)
{
	uint32_t rec_count = 0;

	slurm_mutex_lock(&bb_state.bb_mutex);
	packstr(bb_state.name, buffer);
	bb_pack_state(&bb_state, buffer, protocol_version);

	if (((bb_state.bb_config.flags & BB_FLAG_PRIVATE_DATA) == 0) ||
	    validate_operator(uid))
		uid = 0;	/* User can see all data */
	rec_count = bb_pack_bufs(uid, &bb_state, buffer, protocol_version);
	(void) bb_pack_usage(uid, &bb_state, buffer, protocol_version);
	log_flag(BURST_BUF, "record_count:%u",
		 rec_count);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed after setting default account + qos, but prior to
 * establishing job ID or creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate(job_desc_msg_t *job_desc, uid_t submit_uid,
			     char **err_msg)
{
	uint64_t bb_size = 0;
	int i, rc;

	xassert(job_desc);
	xassert(job_desc->tres_req_cnt);
	xassert(err_msg);

	rc = _parse_bb_opts(job_desc, &bb_size, submit_uid);
	if (rc != SLURM_SUCCESS)
		return rc;

	if ((job_desc->burst_buffer == NULL) ||
	    (job_desc->burst_buffer[0] == '\0'))
		return rc;

	log_flag(BURST_BUF, "job_user_id:%u, submit_uid:%u",
		 job_desc->user_id, submit_uid);
	log_flag(BURST_BUF, "burst_buffer:%s",
		 job_desc->burst_buffer);

	if (job_desc->user_id == 0) {
		info("User root can not allocate burst buffers");
		*err_msg = xstrdup("User root can not allocate burst buffers");
		return ESLURM_BURST_BUFFER_PERMISSION;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.allow_users) {
		bool found_user = false;
		for (i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i]) {
				found_user = true;
				break;
			}
		}
		if (!found_user) {
			*err_msg = xstrdup("User not found in AllowUsers");
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.bb_config.deny_users) {
		bool found_user = false;
		for (i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i]) {
				found_user = true;
				break;
			}
		}
		if (found_user) {
			*err_msg = xstrdup("User found in DenyUsers");
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.tres_pos > 0) {
		job_desc->tres_req_cnt[bb_state.tres_pos] =
			bb_size / (1024 * 1024);
	}

fini:	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/* Add key=value pairs from "resp_msg" to the job's environment */
static void _update_job_env(job_record_t *job_ptr, char *file_path)
{
	struct stat stat_buf;
	char *data_buf = NULL, *start, *sep;
	int path_fd, i, inx = 0, env_cnt = 0;
	ssize_t read_size;

	/* Read the DataWarp generated environment variable file */
	path_fd = open(file_path, O_CLOEXEC);
	if (path_fd == -1) {
		error("open error on file %s: %m",
		      file_path);
		return;
	}
	if (fstat(path_fd, &stat_buf) == -1) {
		error("stat error on file %s: %m",
		      file_path);
		stat_buf.st_size = 2048;
	} else if (stat_buf.st_size == 0)
		goto fini;
	data_buf = xmalloc_nz(stat_buf.st_size + 1);
	while (inx < stat_buf.st_size) {
		read_size = read(path_fd, data_buf + inx, stat_buf.st_size);
		if (read_size < 0)
			data_buf[inx] = '\0';
		else
			data_buf[inx + read_size] = '\0';
		if (read_size > 0) {
			inx += read_size;
		} else if (read_size == 0) {	/* EOF */
			break;
		} else if (read_size < 0) {	/* error */
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("read error on file %s: %m",
			      file_path);
			break;
		}
	}
	log_flag(BURST_BUF, "%s", data_buf);

	/* Get count of environment variables in the file */
	env_cnt = 0;
	if (data_buf) {
		for (i = 0; data_buf[i]; i++) {
			if (data_buf[i] == '=')
				env_cnt++;
		}
	}

	/* Add to supplemental environment variables (in job record) */
	if (env_cnt) {
		xrecalloc(job_ptr->details->env_sup,
			  MAX(job_ptr->details->env_cnt + env_cnt, 1 + env_cnt),
			  sizeof(char *));
		start = data_buf;
		for (i = 0; (i < env_cnt) && start[0]; i++) {
			sep = strchr(start, '\n');
			if (sep)
				sep[0] = '\0';
			job_ptr->details->env_sup[job_ptr->details->env_cnt++] =
				xstrdup(start);
			if (sep)
				start = sep + 1;
			else
				break;
		}
	}

fini:	xfree(data_buf);
	close(path_fd);
}

/* Return true if #DW options (excludes #BB options) */
static bool _have_dw_cmd_opts(bb_job_t *bb_job)
{
	int i;
	bb_buf_t *bb_buf;

	xassert(bb_job);
	if (bb_job->total_size)
		return true;

	for (i = 0, bb_buf = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, bb_buf++) {
		if (bb_buf->use)
			return true;
	}

	return false;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * NOTE: We run several DW APIs at job submit time so that we can notify the
 * user immediately if there is some error, although that can be a relatively
 * slow operation. We have a timeout of 3 seconds on the DW APIs here and log
 * any times over 0.2 seconds.
 *
 * NOTE: We do this work inline so the user can be notified immediately if
 * there is some problem with their script.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate2(job_record_t *job_ptr, char **err_msg)
{
	char *hash_dir = NULL, *job_dir = NULL, *script_file = NULL;
	char *task_script_file = NULL;
	char *resp_msg = NULL, **script_argv;
	char *dw_cli_path;
	int fd = -1, hash_inx, rc = SLURM_SUCCESS, status = 0;
	bb_job_t *bb_job;
	uint32_t timeout;
	bool using_master_script = false;
	DEF_TIMERS;
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.script_type = "job_process",
		.status = &status,
	};

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0')) {
		if (job_ptr->details->min_nodes == 0)
			rc = ESLURM_INVALID_NODE_COUNT;
		return rc;
	}

	/* Initialization */
	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.last_load_time == 0) {
		/* Assume request is valid for now, can't test it anyway */
		info("Burst buffer down, skip tests for %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return rc;
	}
	bb_job = _get_bb_job(job_ptr);
	if (bb_job == NULL) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		if (job_ptr->details->min_nodes == 0)
			rc = ESLURM_INVALID_NODE_COUNT;
		return rc;
	}
	if ((job_ptr->details->min_nodes == 0) && bb_job->use_job_buf) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}

	if (!_have_dw_cmd_opts(bb_job)) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return rc;
	}

	log_flag(BURST_BUF, "%pJ", job_ptr);

	timeout = bb_state.bb_config.validate_timeout * 1000;
	dw_cli_path = xstrdup(bb_state.bb_config.get_sys_state);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* Standard file location for job arrays */
	if ((job_ptr->array_task_id != NO_VAL) &&
	    (job_ptr->array_job_id != job_ptr->job_id)) {
		hash_inx = job_ptr->array_job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir,
			   job_ptr->array_job_id);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(script_file, "%s/script", job_dir);
		fd = open(script_file, 0);
		if (fd >= 0) {	/* found the script */
			close(fd);
			using_master_script = true;
		} else {
			xfree(hash_dir);
		}
	} else {
		hash_inx = job_ptr->job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(script_file, "%s/script", job_dir);
		if (job_ptr->batch_flag == 0)
			rc = bb_build_bb_script(job_ptr, script_file);
	}

	/* Run "job_process" function, validates user script */
	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("job_process");
	script_argv[3] = xstrdup("--job");
	xstrfmtcat(script_argv[4], "%s", script_file);
	START_TIMER;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	if ((DELTA_TIMER > 200000) ||	/* 0.2 secs */
	    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF))
		info("job_process ran for %s",
		     TIME_STR);
	_log_script_argv(script_argv, resp_msg);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		error("job_process for %pJ status:%u response:%s",
		      job_ptr, status, resp_msg);
		if (err_msg) {
			xfree(*err_msg);
			xstrfmtcat(*err_msg, "%s: %s", plugin_type, resp_msg);
		}
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}
	xfree(resp_msg);
	xfree_array(script_argv);

	/* Clean-up */
	xfree(hash_dir);
	xfree(job_dir);
	xfree(dw_cli_path);
	if (rc != SLURM_SUCCESS) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job_del(&bb_state, job_ptr->job_id);
		slurm_mutex_unlock(&bb_state.bb_mutex);
	} else if (using_master_script) {
		/* Job array's need to have script file in the "standard"
		 * location for the remaining logic, make hard link */
		hash_inx = job_ptr->job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
		xfree(hash_dir);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(task_script_file, "%s/script", job_dir);
		xfree(job_dir);
		if ((link(script_file, task_script_file) != 0) &&
		    (errno != EEXIST)) {
			error("link(%s,%s): %m",
			      script_file,
			      task_script_file);
		}
	}
	xfree(task_script_file);
	xfree(script_file);

	return rc;
}

static struct bb_total_size *_json_parse_real_size(json_object *j)
{
	enum json_type type;
	struct json_object_iter iter;
	struct bb_total_size *bb_tot_sz;
	const char *p;

	bb_tot_sz = xmalloc(sizeof(struct bb_total_size));
	json_object_object_foreachC(j, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
			case json_type_string:
				if (!xstrcmp(iter.key, "units")) {
					p = json_object_get_string(iter.val);
					if (!xstrcmp(p, "bytes")) {
						bb_tot_sz->units =
							BB_UNITS_BYTES;
					}
				}
				break;
			case json_type_int:
				if (!xstrcmp(iter.key, "capacity")) {
					bb_tot_sz->capacity =
						json_object_get_int64(iter.val);
				}
				break;
			default:
				break;
		}
	}

	return bb_tot_sz;
}

/*
 * Fill in the tres_cnt (in MB) based off the job record
 * NOTE: Based upon job-specific burst buffers, excludes persistent buffers
 * IN job_ptr - job record
 * IN/OUT tres_cnt - fill in this already allocated array with tres_cnts
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void bb_p_job_set_tres_cnt(job_record_t *job_ptr, uint64_t *tres_cnt,
				  bool locked)
{
	bb_job_t *bb_job;

	if (!tres_cnt) {
		error("No tres_cnt given when looking at %pJ",
		      job_ptr);
	}

	if (bb_state.tres_pos < 0) {
		/* BB not defined in AccountingStorageTRES */
		return;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if ((bb_job = _get_bb_job(job_ptr))) {
		tres_cnt[bb_state.tres_pos] =
			bb_job->total_size / (1024 * 1024);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(job_record_t *job_ptr)
{
	time_t est_start = time(NULL);
	bb_job_t *bb_job;
	int rc;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return est_start;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE))) {
		est_start += 300;	/* 5 minutes, guess... */
		return est_start;	/* Can't operate on job array struct */
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.last_load_time == 0) {
		est_start += 3600;	/* 1 hour, guess... */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return est_start;	/* Can't operate on job array struct */
	}

	if ((bb_job = _get_bb_job(job_ptr)) == NULL) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return est_start;
	}

	log_flag(BURST_BUF, "%pJ", job_ptr);

	if ((bb_job->persist_add == 0) && (bb_job->swap_size == 0) &&
	    (bb_job->total_size == 0)) {
		/* Only deleting or using persistent buffers */
		if (!_test_persistent_use_ready(bb_job, job_ptr))
			est_start += 60 * 60;	/* one hour, guess... */
	} else if (bb_job->state == BB_STATE_PENDING) {
		rc = bb_test_size_limit(job_ptr, bb_job, &bb_state,
					_queue_teardown);
		if (rc == 0) {		/* Could start now */
			;
		} else if (rc == 1) {	/* Exceeds configured limits */
			est_start += 365 * 24 * 60 * 60;
		} else {		/* No space currently available */
			est_start = MAX(est_start, bb_state.next_end_time);
		}
	} else {	/* Allocation or staging in progress */
		est_start++;
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return est_start;
}

/*
 * Attempt to allocate resources and begin file staging for pending jobs.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	bb_job_queue_rec_t *job_rec;
	List job_candidates;
	ListIterator job_iter;
	job_record_t *job_ptr;
	bb_job_t *bb_job;
	int rc;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "Mutex locked");

	if (bb_state.last_load_time == 0) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
	}

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(_job_queue_del);
	job_iter = list_iterator_create(job_queue);
	while ((job_ptr = list_next(job_iter))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    (job_ptr->start_time == 0) ||
		    (job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
			continue;
		if (job_ptr->array_recs &&
		    ((job_ptr->array_task_id == NO_VAL) ||
		     (job_ptr->array_task_id == INFINITE)))
			continue;	/* Can't operate on job array struct */
		bb_job = _get_bb_job(job_ptr);
		if (bb_job == NULL)
			continue;
		if (bb_job->state == BB_STATE_COMPLETE)
			bb_set_job_bb_state(job_ptr, bb_job, /* job requeued */
					    BB_STATE_PENDING);
		else if (bb_job->state >= BB_STATE_POST_RUN)
			continue;	/* Requeued job still staging out */
		job_rec = xmalloc(sizeof(bb_job_queue_rec_t));
		job_rec->job_ptr = job_ptr;
		job_rec->bb_job = bb_job;
		list_push(job_candidates, job_rec);
	}
	list_iterator_destroy(job_iter);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	bb_set_use_time(&bb_state);
	job_iter = list_iterator_create(job_candidates);
	while ((job_rec = list_next(job_iter))) {
		job_ptr = job_rec->job_ptr;
		bb_job = job_rec->bb_job;
		if (bb_job->state >= BB_STATE_STAGING_IN)
			continue;	/* Job was already allocated a buffer */

		rc = bb_test_size_limit(job_ptr, bb_job, &bb_state,
					_queue_teardown);
		if (rc == 0)		/* Could start now */
			(void) _alloc_job_bb(job_ptr, bb_job, true);
		else if (rc == 1)	/* Exceeds configured limits */
			continue;
		else			/* No space currently available */
			break;
	}
	list_iterator_destroy(job_iter);
	slurm_mutex_unlock(&bb_state.bb_mutex);
	FREE_NULL_LIST(job_candidates);

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
extern int bb_p_job_test_stage_in(job_record_t *job_ptr, bool test_only)
{
	bb_job_t *bb_job = NULL;
	int rc = 1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE)))
		return -1;	/* Can't operate on job array structure */

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ test_only:%d",
		 job_ptr, (int) test_only);
	if (bb_state.last_load_time != 0)
		bb_job = _get_bb_job(job_ptr);
	if (bb_job && (bb_job->state == BB_STATE_COMPLETE))
		bb_set_job_bb_state(job_ptr, bb_job,
				    BB_STATE_PENDING); /* job requeued */
	if (bb_job == NULL) {
		rc = -1;
	} else if (bb_job->state < BB_STATE_STAGING_IN) {
		/* Job buffer not allocated, create now if space available */
		rc = -1;
		if ((test_only == false) &&
		    (bb_test_size_limit(job_ptr, bb_job, &bb_state,
					_queue_teardown) == 0) &&
		    (_alloc_job_bb(job_ptr, bb_job, false) == SLURM_SUCCESS)) {
			rc = 0;	/* Setup/stage-in in progress */
		}
	} else if (bb_job->state == BB_STATE_STAGING_IN) {
		rc = 0;
	} else if (bb_job->state == BB_STATE_STAGED_IN) {
		rc = 1;
	} else {
		rc = -1;	/* Requeued job still staging in */
	}

	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run successfully AND
 * the compute nodes selected for the job.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_begin(job_record_t *job_ptr)
{
	char *client_nodes_file_nid = NULL;
	pre_run_args_t *pre_run_args;
	char **pre_run_argv = NULL, **script_argv = NULL;
	char *job_dir = NULL, *path_file, *resp_msg;
	int arg_inx, hash_inx, rc = SLURM_SUCCESS, status = 0;
	bb_job_t *bb_job;
	uint32_t timeout;
	bool do_pre_run;
	DEF_TIMERS;
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.script_type = "paths",
		.status = &status,
	};

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	if (((!job_ptr->job_resrcs || !job_ptr->job_resrcs->nodes)) &&
	    (job_ptr->details->min_nodes != 0)) {
		error("%pJ lacks node allocation",
		      job_ptr);
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ",
		 job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not start %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		error("no job record buffer for %pJ",
		      job_ptr);
		xfree(job_ptr->state_desc);
		job_ptr->state_desc =
			xstrdup("Could not find burst buffer record");
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}
	do_pre_run = _have_dw_cmd_opts(bb_job);

	/* Confirm that persistent burst buffers work has been completed */
	if ((_create_bufs(job_ptr, bb_job, true) > 0)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_desc =
			xstrdup("Error managing persistent burst buffers");
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}

	hash_inx = job_ptr->job_id % 10;
	xstrfmtcat(job_dir, "%s/hash.%d/job.%u",
		   slurm_conf.state_save_location, hash_inx, job_ptr->job_id);
	xstrfmtcat(client_nodes_file_nid, "%s/client_nids", job_dir);
	if (do_pre_run)
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_PRE_RUN);
	else
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_RUNNING);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	if (job_ptr->job_resrcs && job_ptr->job_resrcs->nodes &&
	    bb_write_nid_file(client_nodes_file_nid, job_ptr->job_resrcs->nodes,
			      job_ptr)) {
		xfree(client_nodes_file_nid);
	}

	/* Run "paths" function, get DataWarp environment variables */
	if (do_pre_run) {
		/* Setup "paths" operation */
		timeout = bb_state.bb_config.validate_timeout * 1000;
		script_argv = xcalloc(10, sizeof(char *)); /* NULL terminate */
		script_argv[0] = xstrdup("dw_wlm_cli");
		script_argv[1] = xstrdup("--function");
		script_argv[2] = xstrdup("paths");
		script_argv[3] = xstrdup("--job");
		xstrfmtcat(script_argv[4], "%s/script", job_dir);
		script_argv[5] = xstrdup("--token");
		xstrfmtcat(script_argv[6], "%u", job_ptr->job_id);
		script_argv[7] = xstrdup("--pathfile");
		xstrfmtcat(script_argv[8], "%s/path", job_dir);
		path_file = script_argv[8];
		START_TIMER;
		run_command_args.max_wait = timeout;
		run_command_args.script_argv = script_argv;
		resp_msg = run_command(&run_command_args);
		END_TIMER;
		if ((DELTA_TIMER > 200000) ||	/* 0.2 secs */
		    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF))
			info("paths ran for %s",
			     TIME_STR);
		_log_script_argv(script_argv, resp_msg);
#if 1
		//FIXME: Cray API returning "job_file_valid True" but exit 1 in some cases
		if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
		    (!resp_msg ||
		     strncmp(resp_msg, "job_file_valid True", 19))) {
#else
		if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
#endif
			error("paths for %pJ status:%u response:%s",
			      job_ptr, status, resp_msg);
			xfree(resp_msg);
			rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			xfree_array(script_argv);
			goto fini;
		} else {
			_update_job_env(job_ptr, path_file);
			xfree(resp_msg);
		}
		xfree_array(script_argv);

		/* Setup "pre_run" operation */
		pre_run_argv = xcalloc(12, sizeof(char *));
		pre_run_argv[0] = xstrdup("dw_wlm_cli");
		pre_run_argv[1] = xstrdup("--function");
		pre_run_argv[2] = xstrdup("pre_run");
		pre_run_argv[3] = xstrdup("--token");
		xstrfmtcat(pre_run_argv[4], "%u", job_ptr->job_id);
		pre_run_argv[5] = xstrdup("--job");
		xstrfmtcat(pre_run_argv[6], "%s/script", job_dir);
		arg_inx = 7;
		if (client_nodes_file_nid) {
#if defined(HAVE_NATIVE_CRAY)
			pre_run_argv[arg_inx++] = xstrdup("--nidlistfile");
#else
			pre_run_argv[arg_inx++] = xstrdup("--nodehostnamefile");
#endif
			pre_run_argv[arg_inx++] =
				xstrdup(client_nodes_file_nid);
		}
		pre_run_args = xmalloc(sizeof(pre_run_args_t));
		pre_run_args->args    = pre_run_argv;
		pre_run_args->job_id  = job_ptr->job_id;
		pre_run_args->timeout = bb_state.bb_config.other_timeout * 1000;
		pre_run_args->user_id = job_ptr->user_id;
		if (job_ptr->details) {	/* Defer launch until completion */
			job_ptr->details->prolog_running++;
			job_ptr->job_state |= JOB_CONFIGURING;
		}

		slurm_thread_create_detached(NULL, _start_pre_run,
					     pre_run_args);
	}

fini:
	xfree(client_nodes_file_nid);
	xfree(job_dir);
	return rc;
}

/* Kill job from CONFIGURING state */
static void _kill_job(job_record_t *job_ptr, bool hold_job)
{
	last_job_update = time(NULL);
	job_ptr->end_time = last_job_update;
	if (hold_job)
		job_ptr->priority = 0;
	build_cg_bitmap(job_ptr);
	job_ptr->exit_code = 1;
	job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
	xfree(job_ptr->state_desc);
	job_ptr->state_desc = xstrdup("Burst buffer pre_run error");

	job_ptr->job_state  = JOB_REQUEUE;
	job_completion_logger(job_ptr, true);
	job_ptr->job_state = JOB_PENDING | JOB_COMPLETING;

	deallocate_nodes(job_ptr, false, false, false);
}

static void *_start_pre_run(void *x)
{
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	pre_run_args_t *pre_run_args = (pre_run_args_t *) x;
	char *resp_msg = NULL;
	bb_job_t *bb_job = NULL;
	int status = 0;
	job_record_t *job_ptr;
	bool run_kill_job = false;
	bool hold_job = false, nodes_ready = false;
	DEF_TIMERS;
	pthread_t tid = pthread_self();
	track_script_rec_add(pre_run_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.max_wait = pre_run_args->timeout * 1000,
		.script_argv = pre_run_args->args,
		.script_path = bb_state.bb_config.get_sys_state,
		.script_type = "dws_pre_run",
		.status = &status,
		.tid = tid,
	};

	/* Wait for node boot to complete */
	while (!nodes_ready) {
		lock_slurmctld(job_read_lock);
		job_ptr = find_job_record(pre_run_args->job_id);
		if (!job_ptr || IS_JOB_COMPLETED(job_ptr)) {
			unlock_slurmctld(job_read_lock);
			track_script_remove(pthread_self());
			return NULL;
		}
		if (test_job_nodes_ready(job_ptr))
			nodes_ready = true;
		unlock_slurmctld(job_read_lock);
		if (!nodes_ready)
			sleep(60);
	}


	START_TIMER;
	resp_msg = run_command(&run_command_args);
	END_TIMER;

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("dws_pre_run for JobId=%u terminated by slurmctld",
		     pre_run_args->job_id);
		xfree(resp_msg);
		xfree_array(pre_run_args->args);
		xfree(pre_run_args);
		track_script_remove(pthread_self());
		return NULL;
	}
	/* track_script_reset_cpid(pthread_self(), 0); */

	lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&bb_state.bb_mutex);
	job_ptr = find_job_record(pre_run_args->job_id);
	if ((DELTA_TIMER > 500000) ||	/* 0.5 secs */
	    (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF)) {
		info("dws_pre_run for %pJ ran for %s",
		     job_ptr, TIME_STR);
	}
	if (job_ptr)
		bb_job = _get_bb_job(job_ptr);
	_log_script_argv(pre_run_args->args, resp_msg);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		/* Pre-run failure */
		trigger_burst_buffer();
		error("dws_pre_run for %pJ status:%u response:%s",
		      job_ptr, status, resp_msg);
		if (job_ptr) {
			bb_update_system_comment(job_ptr, "pre_run", resp_msg,
						 0);
			if (IS_JOB_RUNNING(job_ptr))
				run_kill_job = true;
			if (bb_job) {
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_TEARDOWN);
				if (bb_job->retry_cnt++ > MAX_RETRY_CNT)
					hold_job = true;
			}
		}
		_queue_teardown(pre_run_args->job_id, pre_run_args->user_id,
				true);
	} else if (bb_job) {
		/* Pre-run success and the job's BB record exists */
		if (bb_job->state == BB_STATE_ALLOC_REVOKE)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		else
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_RUNNING);
	}
	if (job_ptr) {
		if (run_kill_job)
			job_ptr->job_state &= ~JOB_CONFIGURING;
		prolog_running_decr(job_ptr);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	if (run_kill_job) {
		/* bb_mutex must be unlocked before calling this */
		_kill_job(job_ptr, hold_job);
	}
	unlock_slurmctld(job_write_lock);

	xfree(resp_msg);
	xfree_array(pre_run_args->args);
	xfree(pre_run_args);

	track_script_remove(pthread_self());

	return NULL;
}

/* Revoke allocation, but do not release resources.
 * Executed after bb_p_job_begin() if there was an allocation failure.
 * Does not release previously allocated resources.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_revoke_alloc(job_record_t *job_ptr)
{
	bb_job_t *bb_job = NULL;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (job_ptr)
		bb_job = _get_bb_job(job_ptr);
	if (bb_job) {
		if (bb_job->state == BB_STATE_RUNNING)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		else if (bb_job->state == BB_STATE_PRE_RUN)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_ALLOC_REVOKE);
	} else {
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_start_stage_out(job_record_t *job_ptr)
{
	bb_job_t *bb_job;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not stage out %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		/* No job buffers. Assuming use of persistent buffers only */
		verbose("%pJ bb job record not found",
			job_ptr);
	} else if (bb_job->state < BB_STATE_RUNNING) {
		/* Job never started. Just teardown the buffer */
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true);
	} else if (bb_job->state < BB_STATE_POST_RUN) {
		_pre_queue_stage_out(job_ptr, bb_job);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer post_run operation is complete
 *
 * RET: 0 - post_run is underway
 *      1 - post_run complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_post_run(job_record_t *job_ptr)
{
	bb_job_t *bb_job;
	int rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not post_run %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return -1;
	}
	bb_job = bb_job_find(&bb_state, job_ptr->job_id);
	if (!bb_job) {
		/* No job buffers. Assuming use of persistent buffers only */
		verbose("%pJ bb job record not found",
			job_ptr);
		rc =  1;
	} else {
		if (bb_job->state < BB_STATE_POST_RUN) {
			rc = -1;
		} else if (bb_job->state > BB_STATE_POST_RUN) {
			rc =  1;
		} else {
			rc =  0;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_stage_out(job_record_t *job_ptr)
{
	bb_job_t *bb_job;
	int rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not stage-out %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return -1;
	}
	bb_job = bb_job_find(&bb_state, job_ptr->job_id);
	if (!bb_job) {
		/*
		 * This is expected if the burst buffer completed teardown,
		 * or if only persistent burst buffers were used.
		 */
		rc =  1;
	} else {
		if (bb_job->state == BB_STATE_PENDING) {
			/*
			 * No job BB work not started before job was killed.
			 * Alternately slurmctld daemon restarted after the
			 * job's BB work was completed.
			 */
			rc =  1;
		} else if (bb_job->state < BB_STATE_POST_RUN) {
			rc = -1;
		} else if (bb_job->state > BB_STATE_STAGING_OUT) {
			rc =  1;
			if (bb_job->state == BB_STATE_COMPLETE)
				bb_job_del(&bb_state, bb_job->job_id);
		} else {
			rc =  0;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_cancel(job_record_t *job_ptr)
{
	bb_job_t *bb_job;
	bb_alloc_t *bb_alloc;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not cancel %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}

	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		/* Nothing ever allocated, nothing to clean up */
	} else if (bb_job->state == BB_STATE_PENDING) {
		bb_set_job_bb_state(job_ptr, bb_job, /* Nothing to clean up */
				    BB_STATE_COMPLETE);
	} else {
		/* Note: Persistent burst buffer actions already completed
		 * for the job are not reversed */
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
		bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
		if (bb_alloc) {
			bb_alloc->state = BB_STATE_TEARDOWN;
			bb_alloc->state_time = time(NULL);
			bb_state.last_update_time = time(NULL);

		}
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

static void _free_create_args(create_buf_data_t *create_args)
{
	if (create_args) {
		xfree(create_args->access);
		xfree(create_args->job_script);
		xfree(create_args->name);
		xfree(create_args->pool);
		xfree(create_args->type);
		xfree(create_args);
	}
}

/*
 * Create/destroy persistent burst buffers
 * job_ptr IN - job to operate upon
 * bb_job IN - job's burst buffer data
 * job_ready IN - if true, job is ready to run now, if false then do not
 *                delete persistent buffers
 * Returns count of buffer create/destroy requests which are pending
 */
static int _create_bufs(job_record_t *job_ptr, bb_job_t *bb_job,
			bool job_ready)
{
	create_buf_data_t *create_args;
	bb_buf_t *buf_ptr;
	bb_alloc_t *bb_alloc;
	int i, hash_inx, rc = 0;

	xassert(bb_job);
	for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, buf_ptr++) {
		if ((buf_ptr->state == BB_STATE_ALLOCATING) ||
		    (buf_ptr->state == BB_STATE_DELETING)) {
			rc++;
		} else if (buf_ptr->state != BB_STATE_PENDING) {
			;	/* Nothing to do */
		} else if ((buf_ptr->flags == BB_FLAG_BB_OP) &&
			   buf_ptr->create) {	/* Create the buffer */
			bb_alloc = bb_find_name_rec(buf_ptr->name,
						    job_ptr->user_id,
						    &bb_state);
			if (bb_alloc &&
			    (bb_alloc->user_id != job_ptr->user_id)) {
				info("Attempt by %pJ user %u to create duplicate persistent burst buffer named %s and currently owned by user %u",
				      job_ptr, job_ptr->user_id,
				      buf_ptr->name, bb_alloc->user_id);
				job_ptr->priority = 0;
				job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
				xfree(job_ptr->state_desc);
				job_ptr->state_desc = xstrdup(
					"Burst buffer create_persistent error");
				buf_ptr->state = BB_STATE_COMPLETE;
				bb_update_system_comment(
						job_ptr, "create_persistent",
						"Duplicate buffer name", 0);
				rc++;
				break;
			} else if (bb_alloc) {
				/* Duplicate create likely result of requeue */
				debug("Attempt by %pJ to create duplicate persistent burst buffer named %s",
				      job_ptr, buf_ptr->name);
				buf_ptr->create = false; /* Creation complete */
				if (bb_job->persist_add >= bb_alloc->size) {
					bb_job->persist_add -= bb_alloc->size;
				} else {
					error("Persistent buffer size underflow for %pJ",
					      job_ptr);
					bb_job->persist_add = 0;
				}
				continue;
			}
			rc++;
			if (!buf_ptr->pool) {
				buf_ptr->pool =
					xstrdup(bb_state.bb_config.default_pool);
			}
			bb_limit_add(job_ptr->user_id, buf_ptr->size,
				     buf_ptr->pool, &bb_state, true);
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_ALLOCATING);
			buf_ptr->state = BB_STATE_ALLOCATING;
			create_args = xmalloc(sizeof(create_buf_data_t));
			create_args->access = xstrdup(buf_ptr->access);
			create_args->job_id = job_ptr->job_id;
			create_args->name = xstrdup(buf_ptr->name);
			create_args->pool = xstrdup(buf_ptr->pool);
			create_args->size = buf_ptr->size;
			create_args->type = xstrdup(buf_ptr->type);
			create_args->user_id = job_ptr->user_id;

			slurm_thread_create_detached(NULL, _create_persistent,
						     create_args);
		} else if ((buf_ptr->flags == BB_FLAG_BB_OP) &&
			   buf_ptr->destroy && job_ready) {
			/* Delete the buffer */
			bb_alloc = bb_find_name_rec(buf_ptr->name,
						    job_ptr->user_id,
						    &bb_state);
			if (!bb_alloc) {
				/* Ignore request if named buffer not found */
				info("No burst buffer with name '%s' found for %pJ",
				     buf_ptr->name, job_ptr);
				continue;
			}
			rc++;
			if ((bb_alloc->user_id != job_ptr->user_id) &&
			    !validate_super_user(job_ptr->user_id)) {
				info("Attempt by user %u %pJ to destroy buffer %s owned by user %u",
				     job_ptr->user_id, job_ptr,
				     buf_ptr->name, bb_alloc->user_id);
				job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
				xstrfmtcat(job_ptr->state_desc,
					   "%s: Delete buffer %s permission "
					   "denied",
					   plugin_type, buf_ptr->name);
				job_ptr->priority = 0;  /* Hold job */
				continue;
			}

			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_DELETING);
			buf_ptr->state = BB_STATE_DELETING;
			create_args = xmalloc(sizeof(create_buf_data_t));
			create_args->hurry = buf_ptr->hurry;
			create_args->job_id = job_ptr->job_id;
			hash_inx = job_ptr->job_id % 10;
			xstrfmtcat(create_args->job_script,
				   "%s/hash.%d/job.%u/script",
				   slurm_conf.state_save_location,
				   hash_inx, job_ptr->job_id);
			create_args->name = xstrdup(buf_ptr->name);
			create_args->user_id = job_ptr->user_id;

			slurm_thread_create_detached(NULL, _destroy_persistent,
						     create_args);
		} else if ((buf_ptr->flags == BB_FLAG_BB_OP) &&
			   buf_ptr->destroy) {
			rc++;
		} else if ((buf_ptr->flags != BB_FLAG_BB_OP) &&
			   buf_ptr->use) {
			/*
			 * Persistent buffer not created or destroyed, but used.
			 * Just check for existence
			 */
			bb_alloc = bb_find_name_rec(buf_ptr->name,
						    job_ptr->user_id,
						    &bb_state);
			if (bb_alloc && (bb_alloc->state == BB_STATE_ALLOCATED))
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_ALLOCATED);
			else
				rc++;
		}
	}

	return rc;
}

/* Test for the existence of persistent burst buffers to be used (but not
 * created) by this job. Return true of they are all ready */
static bool _test_persistent_use_ready(bb_job_t *bb_job,
				       job_record_t *job_ptr)
{
	int i, not_ready_cnt = 0;
	bb_alloc_t *bb_alloc;
	bb_buf_t *buf_ptr;

	xassert(bb_job);
	for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, buf_ptr++) {
		if (buf_ptr->create || buf_ptr->destroy)
			continue;
		bb_alloc = bb_find_name_rec(buf_ptr->name, job_ptr->user_id,
					    &bb_state);
		if (bb_alloc && (bb_alloc->state == BB_STATE_ALLOCATED)) {
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_ALLOCATED);
		} else {
			not_ready_cnt++;
			break;
		}
	}
	if (not_ready_cnt != 0)
		return false;
	return true;
}

/* Reset data structures based upon a change in buffer state
 * IN user_id - User effected
 * IN job_id - Job effected
 * IN name - Buffer name
 * IN new_state - New buffer state
 * IN buf_size - Size of created burst buffer only, used to decrement remaining
 *               space requirement for the job
 */
static void _reset_buf_state(uint32_t user_id, uint32_t job_id, char *name,
			     int new_state, uint64_t buf_size)
{
	bb_buf_t *buf_ptr;
	bb_job_t *bb_job;
	int i, old_state;
	bool active_buf = false;

	bb_job = bb_job_find(&bb_state, job_id);
	if (!bb_job) {
		error("Could not find job record for JobId=%u",
		      job_id);
		return;
	}

	/* Update the buffer's state in job record */
	for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, buf_ptr++) {
		if (xstrcmp(name, buf_ptr->name))
			continue;
		old_state = buf_ptr->state;
		buf_ptr->state = new_state;
		if ((old_state == BB_STATE_ALLOCATING) &&
		    (new_state == BB_STATE_PENDING)) {
			bb_limit_rem(user_id, buf_ptr->size, buf_ptr->pool,
				     &bb_state);
		}
		if ((old_state == BB_STATE_DELETING) &&
		    (new_state == BB_STATE_PENDING)) {
			bb_limit_rem(user_id, buf_ptr->size, buf_ptr->pool,
				     &bb_state);
		}
		if ((old_state == BB_STATE_ALLOCATING) &&
		    (new_state == BB_STATE_ALLOCATED)  &&
		    ((name[0] < '0') || (name[0] > '9'))) {
			buf_ptr->create = false;  /* Buffer creation complete */
			if (bb_job->persist_add >= buf_size) {
				bb_job->persist_add -= buf_size;
			} else {
				error("Persistent buffer size underflow for JobId=%u",
				      job_id);
				bb_job->persist_add = 0;
			}
		}
		break;
	}

	for (i = 0, buf_ptr = bb_job->buf_ptr; i < bb_job->buf_cnt;
	     i++, buf_ptr++) {
		old_state = buf_ptr->state;
		if ((old_state == BB_STATE_PENDING)    ||
		    (old_state == BB_STATE_ALLOCATING) ||
		    (old_state == BB_STATE_DELETING)   ||
		    (old_state == BB_STATE_TEARDOWN)   ||
		    (old_state == BB_STATE_TEARDOWN_FAIL))
			active_buf = true;
		break;
	}
	if (!active_buf) {
		job_record_t *job_ptr = find_job_record(job_id);
		if (bb_job->state == BB_STATE_ALLOCATING)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_ALLOCATED);
		else if (bb_job->state == BB_STATE_DELETING)
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_DELETED);
		queue_job_scheduler();
	}
}

/* Create a persistent burst buffer based upon user specifications. */
static void *_create_persistent(void *x)
{
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	create_buf_data_t *create_args = (create_buf_data_t *) x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc;
	char **script_argv, *resp_msg;
	int i, status = 0;
	uint32_t timeout;
	DEF_TIMERS;
	pthread_t tid = pthread_self();
	track_script_rec_add(create_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.script_type = "create_persistent",
		.status = &status,
		.tid = tid,
	};

	script_argv = xcalloc(20, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("create_persistent");
	script_argv[3] = xstrdup("-c");
	script_argv[4] = xstrdup("CLI");
	script_argv[5] = xstrdup("-t");		/* name */
	script_argv[6] = xstrdup(create_args->name);
	script_argv[7] = xstrdup("-u");		/* user iD */
	xstrfmtcat(script_argv[8], "%u", create_args->user_id);
	script_argv[9] = xstrdup("-C");		/* configuration */
	xstrfmtcat(script_argv[10], "%s:%"PRIu64"",
		   create_args->pool, create_args->size);
	slurm_mutex_lock(&bb_state.bb_mutex);
	timeout = bb_state.bb_config.other_timeout * 1000;
	slurm_mutex_unlock(&bb_state.bb_mutex);
	i = 11;
	if (create_args->access) {
		script_argv[i++] = xstrdup("-a");
		script_argv[i++] = xstrdup(create_args->access);
	}
	if (create_args->type) {
		script_argv[i++] = xstrdup("-T");
		script_argv[i++] = xstrdup(create_args->type);
	}
	/* NOTE: There is an optional group ID parameter available and
	 * currently not used by Slurm */

	START_TIMER;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	_log_script_argv(script_argv, resp_msg);
	xfree_array(script_argv);
	END_TIMER;
	info("create_persistent of %s ran for %s",
	     create_args->name, TIME_STR);

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("create_persistent for JobId=%u terminated by slurmctld",
		     create_args->job_id);
		xfree(resp_msg);
		_free_create_args(create_args);
		track_script_remove(pthread_self());
		return NULL;
	}
	/* track_script_reset_cpid(pthread_self(), 0); */

	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		trigger_burst_buffer();
		error("For JobId=%u Name=%s status:%u response:%s",
		      create_args->job_id,
		      create_args->name, status, resp_msg);
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(create_args->job_id);
		if (!job_ptr) {
			error("unable to find job record for JobId=%u",
			      create_args->job_id);
		} else {
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			job_ptr->priority = 0;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s",
				   resp_msg);
			bb_update_system_comment(job_ptr, "create_persistent",
						 resp_msg, 0);
		}
		slurm_mutex_lock(&bb_state.bb_mutex);
		_reset_buf_state(create_args->user_id, create_args->job_id,
				 create_args->name, BB_STATE_PENDING, 0);
		bb_state.last_update_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	} else if (resp_msg && strstr(resp_msg, "created")) {
		assoc_mgr_lock_t assoc_locks =
			{ .assoc = READ_LOCK, .qos = READ_LOCK };
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(create_args->job_id);
		if (!job_ptr) {
			error("unable to find job record for JobId=%u",
			      create_args->job_id);
		}
		assoc_mgr_lock(&assoc_locks);
		slurm_mutex_lock(&bb_state.bb_mutex);
		_reset_buf_state(create_args->user_id, create_args->job_id,
				 create_args->name, BB_STATE_ALLOCATED,
				 create_args->size);
		bb_alloc = bb_alloc_name_rec(&bb_state, create_args->name,
					     create_args->user_id);
		bb_alloc->size = create_args->size;
		bb_alloc->pool = xstrdup(create_args->pool);
		if (job_ptr) {
			bb_alloc->account   = xstrdup(job_ptr->account);
			if (job_ptr->assoc_ptr) {
				/* Only add the direct association id
				 * here, we don't need to keep track
				 * of the tree.
				 */
				slurmdb_assoc_rec_t *assoc = job_ptr->assoc_ptr;
				bb_alloc->assoc_ptr = assoc;
				xfree(bb_alloc->assocs);
				bb_alloc->assocs = xstrdup_printf(
					",%u,", assoc->id);
			}
			if (job_ptr->qos_ptr) {
				slurmdb_qos_rec_t *qos_ptr = job_ptr->qos_ptr;
				bb_alloc->qos_ptr = qos_ptr;
				bb_alloc->qos = xstrdup(qos_ptr->name);
			}

			if (job_ptr->part_ptr) {
				bb_alloc->partition =
					xstrdup(job_ptr->part_ptr->name);
			}
		}
		if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY) {
			bb_alloc->create_time = time(NULL);
			bb_alloc->id = ++last_persistent_id;
		} else {
			bb_sessions_t *sessions;
			int  num_sessions = 0;
			sessions = _bb_get_sessions(&num_sessions, &bb_state,
						    timeout);
			for (i = 0; i < num_sessions; i++) {
				if (xstrcmp(sessions[i].token,
					    create_args->name))
					continue;
				bb_alloc->create_time = sessions[i].created;
				bb_alloc->id = sessions[i].id;
				break;
			}
			_bb_free_sessions(sessions, num_sessions);
		}
		(void) bb_post_persist_create(job_ptr, bb_alloc, &bb_state);
		bb_state.last_update_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		assoc_mgr_unlock(&assoc_locks);
		unlock_slurmctld(job_write_lock);
	}
	xfree(resp_msg);
	_free_create_args(create_args);

	track_script_remove(pthread_self());

	return NULL;
}

/* Destroy a persistent burst buffer */
static void *_destroy_persistent(void *x)
{
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	create_buf_data_t *destroy_args = (create_buf_data_t *) x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc;
	char **script_argv, *resp_msg;
	int status = 0;
	uint32_t timeout;
	DEF_TIMERS;
	pthread_t tid = pthread_self();
	track_script_rec_add(destroy_args->job_id, 0, pthread_self());
	run_command_args_t run_command_args = {
		.script_path = bb_state.bb_config.get_sys_state,
		.script_type = "destroy_persistent",
		.status = &status,
		.tid = tid,
	};

	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_alloc = bb_find_name_rec(destroy_args->name, destroy_args->user_id,
				    &bb_state);
	if (!bb_alloc) {
		info("No burst buffer with name '%s' found for JobId=%u",
		     destroy_args->name, destroy_args->job_id);
	}
	timeout = bb_state.bb_config.other_timeout * 1000;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("teardown");
	script_argv[3] = xstrdup("--token");	/* name */
	script_argv[4] = xstrdup(destroy_args->name);
	script_argv[5] = xstrdup("--job");	/* script */
	script_argv[6] = xstrdup(destroy_args->job_script);
	if (destroy_args->hurry)
		script_argv[7] = xstrdup("--hurry");

	START_TIMER;
	run_command_args.max_wait = timeout;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	_log_script_argv(script_argv, resp_msg);
	xfree_array(script_argv);
	END_TIMER;
	info("destroy_persistent of %s ran for %s",
	     destroy_args->name, TIME_STR);

	if (track_script_killed(pthread_self(), status, true)) {
		/* I was killed by slurmtrack, bail out right now */
		info("destroy_persistent for JobId=%u terminated by slurmctld",
		     destroy_args->job_id);
		xfree(resp_msg);
		_free_create_args(destroy_args);

		track_script_remove(pthread_self());
		return NULL;
	}
	/* track_script_reset_cpid(pthread_self(), 0); */

	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		trigger_burst_buffer();
		error("destroy_persistent for JobId=%u Name=%s status:%u response:%s",
		      destroy_args->job_id,
		      destroy_args->name, status, resp_msg);
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(destroy_args->job_id);
		if (!job_ptr) {
			error("unable to find job record for JobId=%u",
			      destroy_args->job_id);
		} else {
			bb_update_system_comment(job_ptr, "teardown",
						 resp_msg, 0);
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s",
				   resp_msg);
		}
		slurm_mutex_lock(&bb_state.bb_mutex);
		_reset_buf_state(destroy_args->user_id, destroy_args->job_id,
				 destroy_args->name, BB_STATE_PENDING, 0);
		bb_state.last_update_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	} else {
		assoc_mgr_lock_t assoc_locks =
			{ .assoc = READ_LOCK, .qos = READ_LOCK };
		/*
		 * job_write_lock needed for _reset_buf_state() since it will
		 * call bb_set_job_bb_state() to modify
		 * job_ptr->burst_buffer_state
		 */
		lock_slurmctld(job_write_lock);
		/* assoc_mgr needs locking to call bb_post_persist_delete */
		if (bb_alloc)
			assoc_mgr_lock(&assoc_locks);
		slurm_mutex_lock(&bb_state.bb_mutex);
		_reset_buf_state(destroy_args->user_id, destroy_args->job_id,
				 destroy_args->name, BB_STATE_DELETED, 0);

		/* Modify internal buffer record for purging */
		if (bb_alloc) {
			bb_alloc->state = BB_STATE_COMPLETE;
			bb_alloc->job_id = destroy_args->job_id;
			bb_alloc->state_time = time(NULL);
			bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state);

			(void) bb_post_persist_delete(bb_alloc, &bb_state);

			(void) bb_free_alloc_rec(&bb_state, bb_alloc);
		}
		bb_state.last_update_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		if (bb_alloc)
			assoc_mgr_unlock(&assoc_locks);
		unlock_slurmctld(job_write_lock);
	}
	xfree(resp_msg);
	_free_create_args(destroy_args);

	track_script_remove(pthread_self());

	return NULL;
}

/* _bb_get_configs()
 *
 * Handle the JSON stream with configuration info (instance use details).
 */
static bb_configs_t *
_bb_get_configs(int *num_ent, bb_state_t *state_ptr, uint32_t timeout)
{
	bb_configs_t *ents = NULL;
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
	char *resp_msg;
	char **script_argv;
	run_command_args_t run_command_args = {
		.max_wait = timeout,
		.script_path = state_ptr->bb_config.get_sys_state,
		.script_type = "show_configurations",
		.status = &status,
	};

	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("show_configurations");

	START_TIMER;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	log_flag(BURST_BUF, "show_configurations ran for %s",
		 TIME_STR);
	_log_script_argv(script_argv, resp_msg);
	xfree_array(script_argv);
#if 0
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
#else
//FIXME: Cray bug: API returning error if no configurations, use above code when fixed
	if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
	    (!resp_msg || (resp_msg[0] != '{'))) {
#endif
		trigger_burst_buffer();
		error("show_configurations status:%u response:%s",
		      status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s returned no configurations",
		     state_ptr->bb_config.get_sys_state);
		return ents;
	}


	_python2json(resp_msg);
	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("json parser failed on \"%s\"",
		      resp_msg);
		xfree(resp_msg);
		return ents;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (ents) {
			error("Multiple configuration objects");
			break;
		}
		ents = _json_parse_configs_array(j, iter.key, num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	return ents;
}

/* _bb_get_instances()
 *
 * Handle the JSON stream with instance info (resource reservations).
 */
static bb_instances_t *
_bb_get_instances(int *num_ent, bb_state_t *state_ptr, uint32_t timeout)
{
	bb_instances_t *ents = NULL;
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
	char *resp_msg;
	char **script_argv;
	run_command_args_t run_command_args = {
		.max_wait = timeout,
		.script_path = state_ptr->bb_config.get_sys_state,
		.script_type = "show_instances",
		.status = &status,
	};

	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("show_instances");

	START_TIMER;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	log_flag(BURST_BUF, "show_instances ran for %s",
		 TIME_STR);
	_log_script_argv(script_argv, resp_msg);
	xfree_array(script_argv);
#if 0
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
#else
//FIXME: Cray bug: API returning error if no instances, use above code when fixed
	if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
	    (!resp_msg || (resp_msg[0] != '{'))) {
#endif
		trigger_burst_buffer();
		error("show_instances status:%u response:%s",
		      status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s returned no instances",
		     state_ptr->bb_config.get_sys_state);
		return ents;
	}

	_python2json(resp_msg);
	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("json parser failed on \"%s\"",
		      resp_msg);
		xfree(resp_msg);
		return ents;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (ents) {
			error("Multiple instance objects");
			break;
		}
		ents = _json_parse_instances_array(j, iter.key, num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	return ents;
}

/* _bb_get_pools()
 *
 * Handle the JSON stream with resource pool info (available resource type).
 */
static bb_pools_t *
_bb_get_pools(int *num_ent, bb_state_t *state_ptr, uint32_t timeout)
{
	bb_pools_t *ents = NULL;
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
	char *resp_msg;
	char **script_argv;
	run_command_args_t run_command_args = {
		.max_wait = timeout,
		.script_path = state_ptr->bb_config.get_sys_state,
		.script_type = "pools",
		.status = &status,
	};

	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("pools");

	START_TIMER;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	if (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF) {
		/* Only log pools data if different to limit volume of logs */
		static uint32_t last_csum = 0;
		uint32_t i, resp_csum = 0;
		debug("pools ran for %s",
		      TIME_STR);
		for (i = 0; resp_msg[i]; i++)
			resp_csum += ((i * resp_msg[i]) % 1000000);
		if (last_csum != resp_csum)
			_log_script_argv(script_argv, resp_msg);
		last_csum = resp_csum;
	}
	xfree_array(script_argv);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		trigger_burst_buffer();
		error("pools status:%u response:%s",
		      status, resp_msg);
	}
	if (resp_msg == NULL) {
		error("%s returned no pools",
		      state_ptr->bb_config.get_sys_state);
		return ents;
	}

	_python2json(resp_msg);
	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("json parser failed on \"%s\"",
		      resp_msg);
		xfree(resp_msg);
		return ents;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (ents) {
			error("Multiple pool objects");
			break;
		}
		ents = _json_parse_pools_array(j, iter.key, num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	return ents;
}

static bb_sessions_t *
_bb_get_sessions(int *num_ent, bb_state_t *state_ptr, uint32_t timeout)
{
	bb_sessions_t *ents = NULL;
	json_object *j;
	json_object_iter iter;
	int status = 0;
	DEF_TIMERS;
	char *resp_msg;
	char **script_argv;
	run_command_args_t run_command_args = {
		.max_wait = timeout,
		.script_path = state_ptr->bb_config.get_sys_state,
		.script_type = "show_sessions",
		.status = &status,
	};

	script_argv = xcalloc(10, sizeof(char *));	/* NULL terminated */
	script_argv[0] = xstrdup("dw_wlm_cli");
	script_argv[1] = xstrdup("--function");
	script_argv[2] = xstrdup("show_sessions");

	START_TIMER;
	run_command_args.script_argv = script_argv;
	resp_msg = run_command(&run_command_args);
	END_TIMER;
	log_flag(BURST_BUF, "show_sessions ran for %s",
		 TIME_STR);
	_log_script_argv(script_argv, resp_msg);
	xfree_array(script_argv);
#if 0
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
#else
//FIXME: Cray bug: API returning error if no sessions, use above code when fixed
	if ((!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) &&
	    (!resp_msg || (resp_msg[0] != '{'))) {
#endif
		trigger_burst_buffer();
		error("show_sessions status:%u response:%s",
		      status, resp_msg);
	}
	if (resp_msg == NULL) {
		info("%s returned no sessions",
		     state_ptr->bb_config.get_sys_state);
		xfree_array(script_argv);
		return ents;
	}

	_python2json(resp_msg);
	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("json parser failed on \"%s\"",
		      resp_msg);
		xfree(resp_msg);
		return ents;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (ents) {
			error("Multiple session objects");
			break;
		}
		ents = _json_parse_sessions_array(j, iter.key, num_ent);
	}
	json_object_put(j);	/* Frees json memory */

	return ents;
}

/* _bb_free_configs()
 */
static void
_bb_free_configs(bb_configs_t *ents, int num_ent)
{
	xfree(ents);
}

/* _bb_free_instances()
 */
static void
_bb_free_instances(bb_instances_t *ents, int num_ent)
{
	xfree(ents);
}

/* _bb_free_pools()
 */
static void
_bb_free_pools(bb_pools_t *ents, int num_ent)
{
	int i;

	for (i = 0; i < num_ent; i++) {
		xfree(ents[i].id);
		xfree(ents[i].units);
	}

	xfree(ents);
}

/* _bb_free_sessions()
 */
static void
_bb_free_sessions(bb_sessions_t *ents, int num_ent)
{
	int i;

	for (i = 0; i < num_ent; i++) {
		xfree(ents[i].token);
	}

	xfree(ents);
}

/* _json_parse_configs_array()
 */
static bb_configs_t *
_json_parse_configs_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	bb_configs_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xcalloc(*num, sizeof(bb_configs_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_configs_object(jvalue, &ents[i]);
	}

	return ents;
}

/* _json_parse_instances_array()
 */
static bb_instances_t *
_json_parse_instances_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	bb_instances_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xcalloc(*num, sizeof(bb_instances_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_instances_object(jvalue, &ents[i]);
	}

	return ents;
}

/* _json_parse_pools_array()
 */
static bb_pools_t *
_json_parse_pools_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	bb_pools_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xcalloc(*num, sizeof(bb_pools_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_pools_object(jvalue, &ents[i]);
	}

	return ents;
}

/* _json_parse_sessions_array()
 */
static bb_sessions_t *
_json_parse_sessions_array(json_object *jobj, char *key, int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	bb_sessions_t *ents;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	ents = xcalloc(*num, sizeof(bb_sessions_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_sessions_object(jvalue, &ents[i]);
	}

	return ents;
}

/* Parse "links" object in the "configuration" object */
static void
_parse_config_links(json_object *instance, bb_configs_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int x;

	json_object_object_foreachC(instance, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (!xstrcmp(iter.key, "instance"))
				ent->instance = x;
			break;
		default:
			break;
		}
	}
}

/* _json_parse_configs_object()
 */
static void
_json_parse_configs_object(json_object *jobj, bb_configs_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_object:
			if (xstrcmp(iter.key, "links") == 0)
				_parse_config_links(iter.val, ent);
			break;
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "id") == 0) {
				ent->id = x;
			}
			break;
		default:
			break;
		}
	}
}

/* Parse "capacity" object in the "instance" object */
static void
_parse_instance_capacity(json_object *instance, bb_instances_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;

	json_object_object_foreachC(instance, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (!xstrcmp(iter.key, "bytes"))
				ent->bytes = x;
			break;
		default:
			break;
		}
	}
}

/* Parse "links" object in the "instance" object */
static void
_parse_instance_links(json_object *instance, bb_instances_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;

	json_object_object_foreachC(instance, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (!xstrcmp(iter.key, "session"))
				ent->session = x;
			break;
		default:
			break;
		}
	}
}

/* _json_parse_instances_object()
 */
static void
_json_parse_instances_object(json_object *jobj, bb_instances_t *ent)
{
	enum json_type type;
	struct json_object_iter iter;
	int64_t x;

	json_object_object_foreachC(jobj, iter) {
		type = json_object_get_type(iter.val);
		switch (type) {
		case json_type_object:
			if (xstrcmp(iter.key, "capacity") == 0)
				_parse_instance_capacity(iter.val, ent);
			else if (xstrcmp(iter.key, "links") == 0)
				_parse_instance_links(iter.val, ent);
			break;
		case json_type_int:
			x = json_object_get_int64(iter.val);
			if (xstrcmp(iter.key, "id") == 0) {
				ent->id = x;
			}
			break;
		default:
			break;
		}
	}
}

/* _json_parse_pools_object()
 */
static void
_json_parse_pools_object(json_object *jobj, bb_pools_t *ent)
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
			if (xstrcmp(iter.key, "granularity") == 0) {
				ent->granularity = x;
			} else if (xstrcmp(iter.key, "quantity") == 0) {
				ent->quantity = x;
			} else if (xstrcmp(iter.key, "free") == 0) {
				ent->free = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "id") == 0) {
				ent->id = xstrdup(p);
			} else if (xstrcmp(iter.key, "units") == 0) {
				ent->units = xstrdup(p);
			}
			break;
		default:
			break;
		}
	}
}

/* _json_parse_session_object()
 */
static void
_json_parse_sessions_object(json_object *jobj, bb_sessions_t *ent)
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
			if (xstrcmp(iter.key, "created") == 0) {
				ent->created = x;
			} else if (xstrcmp(iter.key, "id") == 0) {
				ent->id = x;
			} else if (xstrcmp(iter.key, "owner") == 0) {
				ent->user_id = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "token") == 0) {
				ent->token = xstrdup(p);
			}
		default:
			break;
		}
	}
}

/*
 * Run a script in the burst buffer plugin
 *
 * func IN - script function to run
 * jobid IN - job id for which we are running the script (0 if not for a job)
 * argc IN - number of arguments to pass to script
 * argv IN - argument list to pass to script
 * resp_msg OUT - string returned by script
 *
 * Returns the status of the script.
 */
extern int bb_p_run_script(char *func, uint32_t job_id, uint32_t argc,
			   char **argv, char **resp_msg)
{
	return 0;
}

/*
 * Translate a burst buffer string to it's equivalent TRES string
 * (e.g. "cray:2G,generic:4M" -> "1004=2048,1005=4")
 * Caller must xfree the return value
 */
extern char *bb_p_xlate_bb_2_tres_str(char *burst_buffer)
{
	char *save_ptr = NULL, *sep, *tmp, *tok;
	char *result = NULL;
	uint64_t size, total = 0;

	if (!burst_buffer || (bb_state.tres_id < 1))
		return result;

	tmp = xstrdup(burst_buffer);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		sep = strchr(tok, ':');
		if (sep) {
			if (!xstrncmp(tok, "cray:", 5))
				tok += 5;
			else
				tok = NULL;
		}

		if (tok) {
			uint64_t mb_xlate = 1024 * 1024;
			size = bb_get_size_num(tok,
					       bb_state.bb_config.granularity);
			total += (size + mb_xlate - 1) / mb_xlate;
		}

		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	if (total)
		xstrfmtcat(result, "%d=%"PRIu64, bb_state.tres_id, total);

	return result;
}
