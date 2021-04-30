/*****************************************************************************\
 *  burst_buffer_lua.c - Plugin for managing burst buffers with lua
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#include <ctype.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/xstring.h"
#include "src/lua/slurm_lua.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

/* Script directive */
#define DIRECTIVE_STR "BB_LUA"

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
const char plugin_name[]        = "burst_buffer lua plugin";
const char plugin_type[]        = "burst_buffer/lua";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*
 * Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins.
 */
static bb_state_t bb_state;

static int directive_len = strlen(DIRECTIVE_STR);

static const char lua_script_path[] = DEFAULT_SCRIPT_DIR "/burst_buffer.lua";
static time_t lua_script_last_loaded = (time_t) 0;
static lua_State *L = NULL;
static const char *req_fxns[] = {
	"slurm_bb_job_process",
	"slurm_bb_job_teardown",
	NULL
};

/* Description of each pool entry */
typedef struct bb_pools {
	char *name;
	uint64_t granularity;
	uint64_t quantity;
	uint64_t free;
} bb_pools_t;

typedef struct {
	uint32_t job_id;
	uint32_t user_id;
	char **args1;
	char **args2;
	uint64_t bb_size;
	char *pool;
} stage_args_t;

static void _loadscript_extra(lua_State *st)
{
        /* local setup */
	/*
	 * We may add functions later (like job_submit/lua and cli_filter/lua),
	 * but for now we don't have any.
	 */
	//slurm_lua_table_register(st, NULL, slurm_functions);

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(st, "slurm");
}

/*
 * Call a function in burst_buffer.lua.
 */
static int _run_lua_script(const char *lua_func, int (*callback) (void *x),
			   void *callback_args)
{
	int rc, num_args;

	rc = slurm_lua_loadscript(&L, "burst_buffer/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		return rc;

	/*
	 * All lua script functions should have been verified during
	 * initialization:
	 */
	lua_getglobal(L, lua_func);
	if (lua_isnil(L, -1)) {
		error("%s: Couldn't find function %s",
		      __func__, lua_func);
		lua_close(L);
		return SLURM_ERROR;
	}

	/*
	 * TODO: Push arguments to the stack. Maybe use a callback that
	 * returns the number of arguments.
	 */
	if (callback) {
		num_args = callback(callback_args);
		info("XXX%sXXX: callback returned %d", __func__, num_args);
	}

	slurm_lua_stack_dump("burst_buffer/lua", "before lua_pcall", L);
	if (lua_pcall(L, 0, 1, 0) != 0) {
		error("%s: %s",
		      lua_script_path, lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s: non-numeric return code, returning success",
			     lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump("burst_buffer/lua", "after lua_pcall", L);

	lua_close(L);
	return rc;
}

/*
 * Handle timeout of burst buffer events:
 * 1. Purge per-job burst buffer records when the stage-out has completed and
 *    the job has been purged from Slurm
 * 2. Test for StageInTimeout events
 * 3. Test for StageOutTimeout events
 */
static void _timeout_bb_rec(void)
{
	/* Not implemented yet. */
}

/*
 * Write current burst buffer state to a file.
 */
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

	buffer = init_buf(high_buffer_size);
	pack16(protocol_version, buffer);
	count_offset = get_buf_offset(buffer);
	pack32(rec_count, buffer);
	/* Each allocated burst buffer is in bb_state.bb_ahash */
	if (bb_state.bb_ahash) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		for (i = 0; i < BB_HASH_SIZE; i++) {
			bb_alloc = bb_state.bb_ahash[i];
			while (bb_alloc) {
				packstr(bb_alloc->account, buffer);
				pack_time(bb_alloc->create_time, buffer);
				pack32(bb_alloc->id, buffer);
				packstr(bb_alloc->name, buffer);
				packstr(bb_alloc->partition, buffer);
				packstr(bb_alloc->pool, buffer);
				packstr(bb_alloc->qos, buffer);
				pack32(bb_alloc->user_id, buffer);
				pack64(bb_alloc->size,	buffer);
				rec_count++;
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
	           "burst_buffer_lua_state.old");
	xstrfmtcat(reg_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_lua_state");
	xstrfmtcat(new_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_lua_state.new");

	bb_write_state_file(old_file, reg_file, new_file, "burst_buffer_lua",
			    buffer, high_buffer_size, save_time,
			    &last_save_time);

	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	free_buf(buffer);
}

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

	state_fd = bb_open_state_file("burst_buffer_lua_state", &state_file);
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
		if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&account,   &name_len, buffer);
			safe_unpack_time(&create_time, buffer);
			safe_unpack32(&id, buffer);
			safe_unpackstr_xmalloc(&name,      &name_len, buffer);
			safe_unpackstr_xmalloc(&partition, &name_len, buffer);
			safe_unpackstr_xmalloc(&pool,      &name_len, buffer);
			safe_unpackstr_xmalloc(&qos,       &name_len, buffer);
			safe_unpack32(&user_id, buffer);
			safe_unpack64(&size, buffer);
		}

		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_alloc = bb_alloc_name_rec(&bb_state, name, user_id);
		bb_alloc->id = id;
		if (name && (name[0] >='0') && (name[0] <='9')) {
			bb_alloc->job_id = strtol(name, &end_ptr, 10);
			bb_alloc->array_job_id = bb_alloc->job_id;
			bb_alloc->array_task_id = NO_VAL;
		}
		bb_alloc->seen_time = time(NULL);
		bb_alloc->size = size;
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

static void _apply_limits(void)
{
	bb_alloc_t *bb_alloc;

	for (int i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			info("Recovered buffer Name:%s User:%u Pool:%s Size:%"PRIu64,
			     bb_alloc->name, bb_alloc->user_id,
			     bb_alloc->pool, bb_alloc->size);
			_set_assoc_mgr_ptrs(bb_alloc);
			bb_limit_add(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state, true);
			bb_alloc = bb_alloc->next;
		}
	}
}

static void _bb_free_pools(bb_pools_t *pools, int num_ent)
{
	for (int i = 0; i < num_ent; i++)
		xfree(pools[i].name);

	xfree(pools);
}

static int _push_pools_args(void *args)
{
	int test_arg = *((int *)(args));
	info("%s: hello! arg=%d", __func__, test_arg);
	return 42;
}

static bb_pools_t *_bb_get_pools(int *num_pools, bb_state_t *state_ptr,
				 uint32_t timeout)
{
	int rc;
	int callback_arg;

	/* Call lua function. */
	callback_arg = 13;
	rc = _run_lua_script("slurm_bb_pools", _push_pools_args, &callback_arg);
	log_flag(BURST_BUF, "slurm_bb_pools return code=%d", rc);
	*num_pools = 0;
	return NULL;
}

static void _load_state(bool init_config)
{
	bb_pools_t *pools;
	int num_pools = 0;
	uint32_t timeout;

	if (!init_config)
		return;

	slurm_mutex_lock(&bb_state.bb_mutex);
	timeout = bb_state.bb_config.other_timeout * 1000;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* Load the pools information. */
	pools = _bb_get_pools(&num_pools, &bb_state, timeout);
	_bb_free_pools(pools, num_pools);


	/* Load allocated burst buffers from state files. */
	bb_state.last_load_time = time(NULL);
	_recover_bb_state();
	_apply_limits();
	bb_state.last_update_time = time(NULL);

	return;
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

/*
 * Copy a batch job's burst_buffer options into a separate buffer.
 * Merge continued lines into a single line.
 */
static int _xlate_batch(job_desc_msg_t *job_desc)
{
	char *script, *save_ptr = NULL, *tok;
	bool is_cont = false, has_space = false;
	int len, rc = SLURM_SUCCESS;

	/*
	 * Any command line --bb options get added to the script
	 */
	if (job_desc->burst_buffer) {
		bb_add_bb_to_script(&job_desc->script, job_desc->burst_buffer);
		xfree(job_desc->burst_buffer);
	}

	script = xstrdup(job_desc->script);
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break; /* Quit at first non-comment */

		if (xstrncmp(tok + 1, DIRECTIVE_STR, directive_len)) {
			/* Skip lines without a burst buffer directive. */
			is_cont = false;
		} else {
			if (is_cont) {
				/*
				 * Continuation of the previous line. Add to
				 * the previous line without the newline and
				 * without repeating the directive.
				 */
				tok += directive_len + 1; /* Add 1 for '#' */
				while (has_space && isspace(tok[0]))
					tok++; /* Skip extra spaces */
			} else if (job_desc->burst_buffer) {
				xstrcat(job_desc->burst_buffer, "\n");
			}

			len = strlen(tok);
			if (tok[len - 1] == '\\') {
				/* Next line is a continuation of this line. */
				has_space = isspace(tok[len - 2]);
				tok[len - 1] = '\0';
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

/*
 * Given a request size and a pool name (or NULL name for default pool),
 * return the required buffer size (rounded up by granularity)
 */
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
			if (!pool_ptr->granularity) {
				/*
				 * This should never happen if we initialize
				 * the pools correctly, so if this error happens
				 * it means we initialized the pool wrong.
				 * This avoids a divide by 0 error.
				 */
				error("%s: Invalid granularity of 0 for pool %s. Setting granularity=1.",
				      __func__, pool_ptr->name);
				pool_ptr->granularity = 1;
			}
			new_size = bb_granularity(orig_size,
						  pool_ptr->granularity);
			return new_size;
		}
	}
	debug("Could not find pool %s", bb_pool);
	return orig_size;
}

/* Perform basic burst_buffer option validation */
static int _parse_bb_opts(job_desc_msg_t *job_desc, uint64_t *bb_size,
			  uid_t submit_uid)
{
	char *bb_script, *save_ptr = NULL;
	char *bb_pool, *capacity;
	char *end_ptr = NULL, *sub_tok, *tok;
	uint64_t tmp_cnt, swap_cnt = 0;
	int rc = SLURM_SUCCESS;
	bool have_bb = false, have_stage_out = false;

	xassert(bb_size);
	*bb_size = 0;

	/*
	 * Combine command line options with script, and copy the script to
	 * job_desc->burst_buffer.
	 */
	if (job_desc->script)
		rc = _xlate_batch(job_desc);
	if ((rc != SLURM_SUCCESS) || (!job_desc->burst_buffer))
		return rc;

	/* Now validate some burst buffer options and get the size. */
	bb_script = xstrdup(job_desc->burst_buffer);
	tok = strtok_r(bb_script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break; /* Quit at first non-comment */
		tok++; /* Skip '#' */

		if (xstrncmp(tok, DIRECTIVE_STR, directive_len)) {
			/* Skip lines without a burst buffer directive. */
			tok = strtok_r(NULL, "\n", &save_ptr);
			continue;
		}
		tok += directive_len; /* Skip the directive string. */
		while (isspace(tok[0]))
			tok++;
		if (!xstrncmp(tok, "jobdw", 5) &&
		    (capacity = strstr(tok, "capacity="))) {
			char *num_ptr = capacity + 9;

			bb_pool = NULL;
			have_bb = true;
			tmp_cnt = bb_get_size_num(num_ptr, 1);
			if (tmp_cnt == 0) {
				rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
				break;
			}
			if ((sub_tok = strstr(tok, "pool="))) {
				bb_pool = xstrdup(sub_tok + 5);
				if ((sub_tok = strchr(bb_pool, ' ')))
					sub_tok[0] = '\0';
			}
			if (!bb_valid_pool_test(&bb_state, bb_pool))
				rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			*bb_size += _set_granularity(tmp_cnt, bb_pool);
			xfree(bb_pool);
		} else if (!xstrncmp(tok, "swap", 4)) {
			bb_pool = NULL;
			have_bb = true;
			tok += 4;
			while (isspace(tok[0] && (tok[0] != '\0')))
				tok++;
			swap_cnt += strtol(tok, &end_ptr, 10);
			if ((job_desc->max_nodes == 0) ||
			    (job_desc->max_nodes == NO_VAL)) {
				info("User %u submitted job with swap space specification, but no max node count specification. Setting max nodes to min nodes.",
				     submit_uid);
				if (job_desc->min_nodes == NO_VAL)
					job_desc->min_nodes = 1;
				job_desc->max_nodes = job_desc->min_nodes;
			}
			tmp_cnt = swap_cnt + job_desc->max_nodes;
			if ((sub_tok = strstr(tok, "pool="))) {
				bb_pool = xstrdup(sub_tok + 5);
				if ((sub_tok = strchr(bb_pool, ' ')))
					sub_tok[0] = '\0';
			}
			if (!bb_valid_pool_test(&bb_state, bb_pool))
				rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			*bb_size += _set_granularity(tmp_cnt, bb_pool);
			xfree(bb_pool);
		} else if (!xstrncmp(tok, "stage_out", 9)) {
			have_stage_out = true;
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

static bb_job_t *_get_bb_job(job_record_t *job_ptr)
{
	char *bb_specs;
	char *end_ptr = NULL, *save_ptr = NULL, *sub_tok, *tok;
	bool have_bb = false;
	uint64_t tmp_cnt;
	uint16_t new_bb_state;
	bb_job_t *bb_job;

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
		/* Skip lines that don't have a burst buffer directive. */
		if ((tok[0] != '#') ||
		    xstrncmp(tok + 1, DIRECTIVE_STR, directive_len)) {
			tok = strtok_r(NULL, "\n", &save_ptr);
			continue;
		}

		tok += directive_len + 1; /* Add 1 for the '#' character. */
		while (isspace(tok[0]))
			tok++;

		if (!xstrncmp(tok, "jobdw", 5)) {
			have_bb = true;
			if ((sub_tok = strstr(tok, "capacity="))) {
				tmp_cnt = bb_get_size_num(sub_tok + 9, 1);
			} else {
				tmp_cnt = 0;
			}
			if ((sub_tok = strstr(tok, "pool"))) {
				xfree(bb_job->job_pool);
				bb_job->job_pool = xstrdup(sub_tok + 5);
				sub_tok = strchr(bb_job->job_pool, ' ');
				if (sub_tok)
					sub_tok[0] = '\0';
			} else {
				bb_job->job_pool = xstrdup(
					bb_state.bb_config.default_pool);
			}
			tmp_cnt = _set_granularity(tmp_cnt, bb_job->job_pool);
			bb_job->req_size += tmp_cnt;
			bb_job->total_size += tmp_cnt;
			bb_job->use_job_buf = true;
		} else if (!xstrncmp(tok, "swap", 4)) {
			have_bb = true;
			tok += 4;
			while (isspace(tok[0]))
				tok++;
			bb_job->swap_size = strtol(tok, &end_ptr, 10);
			if (job_ptr->details && job_ptr->details->max_nodes) {
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
				sub_tok = strchr(bb_job->job_pool, ' ' );
				if (sub_tok)
					sub_tok[0] = '\0';
			} else if (!bb_job->job_pool) {
				bb_job->job_pool = xstrdup(
					bb_state.bb_config.default_pool);
			}
			tmp_cnt = _set_granularity(tmp_cnt, bb_job->job_pool);
			bb_job->req_size += tmp_cnt;
			bb_job->total_size += tmp_cnt;
			bb_job->use_job_buf = true;
		} else {
			/* Ignore stage-in, stage-out, etc. */
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

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	slurm_mutex_init(&bb_state.bb_mutex);
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, (char *)plugin_type); /* Removes "const" */
	log_flag(BURST_BUF, "");
	bb_alloc_cache(&bb_state);
	slurm_thread_create(&bb_state.bb_thread, _bb_agent, NULL);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern int fini(void)
{
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

static void _purge_vestigial_bufs(void)
{
	/* Not yet implemented */
}

/*
 * Return the total burst buffer size in MB
 */
extern uint64_t bb_p_get_system_size(void)
{
	uint64_t size = 0;
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

	log_flag(BURST_BUF, "");
	_load_state(init_config); /* Has own locking */
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_set_tres_pos(&bb_state);
	_purge_vestigial_bufs();
	slurm_mutex_unlock(&bb_state.bb_mutex);

	_save_bb_state(); /* Has own locks excluding file write */

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
	return NULL;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_reconfig(void)
{
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
	log_flag(BURST_BUF, "record_count:%u", rec_count);
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
extern int bb_p_job_validate(job_desc_msg_t *job_desc, uid_t submit_uid)
{
	uint64_t bb_size = 0;
	int rc;

	xassert(job_desc);
	xassert(job_desc->tres_req_cnt);

	rc = _parse_bb_opts(job_desc, &bb_size, submit_uid);
	if (rc != SLURM_SUCCESS)
		return rc;

	if ((job_desc->burst_buffer == NULL) ||
	    (job_desc->burst_buffer[0] == '\0'))
		return rc;

	log_flag(BURST_BUF, "job_user_id:%u, submit_uid:%d",
		 job_desc->user_id, submit_uid);
	log_flag(BURST_BUF, "burst_buffer:\n%s",
		 job_desc->burst_buffer);

	if (job_desc->user_id == 0) {
		info("User root can not allocate burst buffers");
		return ESLURM_BURST_BUFFER_PERMISSION;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.allow_users) {
		bool found_user = false;
		for (int i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i]) {
				found_user = true;
				break;
			}
		}
		if (!found_user) {
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.bb_config.deny_users) {
		bool found_user = false;
		for (int i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i]) {
				found_user = true;
				break;
			}
		}
		if (found_user) {
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.tres_pos > 0) {
		job_desc->tres_req_cnt[bb_state.tres_pos] =
			bb_size / (1024 * 1024);
	}

fini:
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate2(job_record_t *job_ptr, char **err_msg)
{
	int rc = SLURM_SUCCESS;
	bb_job_t *bb_job;

	/* Initialization */
	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.last_load_time == 0) {
		/* Assume request is valid for now, can't test it anyway */
		info("Burst buffer down, skip tests for %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
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

	log_flag(BURST_BUF, "%pJ", job_ptr);

	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* Run "job_process" function, validates user script */
	rc = _run_lua_script("slurm_bb_job_process", NULL, NULL);
	log_flag(BURST_BUF, "Return code=%d", rc);
	return rc;
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
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(job_record_t *job_ptr)
{
	time_t est_start = time(NULL);
	return est_start;
}

/*
 * If the job (x) should be allocated a burst buffer, add it to the
 * job_candidates list (arg).
 */
static int _identify_bb_candidate(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	List job_candidates = (List) arg;
	bb_job_t *bb_job;
	bb_job_queue_rec_t *job_rec;

	if (!IS_JOB_PENDING(job_ptr) || (job_ptr->start_time == 0) ||
	    (job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE)))
		return SLURM_SUCCESS; /* Can't operate on job array struct */

	bb_job = _get_bb_job(job_ptr);
	if (bb_job == NULL)
		return SLURM_SUCCESS;
	if (bb_job->state == BB_STATE_COMPLETE) {
		/* Job requeued or slurmctld restarted during stage-in */
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_PENDING);
	} else if (bb_job->state >= BB_STATE_POST_RUN) {
		/* Requeued job still staging out */
		return SLURM_SUCCESS;
	}
	job_rec = xmalloc(sizeof(bb_job_queue_rec_t));
	job_rec->job_ptr = job_ptr;
	job_rec->bb_job = bb_job;
	list_push(job_candidates, job_rec);
	return SLURM_SUCCESS;
}

static void _purge_bb_files(uint32_t job_id, job_record_t *job_ptr)
{
	/* TODO: Not yet implemented */
}

static void *_start_teardown(void *x)
{
	int rc;
	stage_args_t *teardown_args = (stage_args_t *)x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job = NULL;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	/* Run lua "teardown" function */
	START_TIMER;
	rc = _run_lua_script("slurm_bb_job_teardown", NULL, NULL);
	END_TIMER;
	info("Teardown for JobId=%u ran for %s",
	     teardown_args->job_id, TIME_STR);

	if (rc != SLURM_SUCCESS) {
		/*
		 * TODO: Do more things, also call _queue_teardown() to
		 * requeue teardown
		 */
		error("Teardown function failed.");
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
			if ((bb_job = _get_bb_job(job_ptr)))
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_COMPLETE);
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

	xfree(teardown_args);

	return NULL;
}

static void _queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry)
{
	stage_args_t *teardown_args;
	pthread_t tid;

	/*
	 * TODO: Setup arguments for the burst buffer "teardown" function and
	 * pass arguments to _start_teardown.
	 */
	teardown_args = xmalloc(sizeof *teardown_args);
	teardown_args->job_id = job_id;
	teardown_args->user_id = user_id;

	slurm_thread_create(&tid, _start_teardown, teardown_args);
}

static int _queue_stage_in(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *job_pool;
	bb_alloc_t *bb_alloc = NULL;

	/* TODO: Setup arguments for the burst buffer "setup" function. */
	if (bb_job->job_pool)
		job_pool = bb_job->job_pool;
	else
		job_pool = bb_state.bb_config.default_pool;

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

	/* TODO: Setup arguments for the burst buffer "data_in" function. */
	/* TODO: Create a thread to do stage in: "_start_stage_in()" */
	return SLURM_SUCCESS;
}

static int _alloc_job_bb(job_record_t *job_ptr, bb_job_t *bb_job,
			 bool job_ready)
{
	int rc = SLURM_SUCCESS;

	log_flag(BURST_BUF, "start job allocate %pJ", job_ptr);

	if (bb_job->state < BB_STATE_STAGING_IN) {
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_STAGING_IN);
		rc = _queue_stage_in(job_ptr, bb_job);
		/*
		 * TODO: _queue_stage_in() always returns success right now.
		 * If it always returns success in the future I could change
		 * it to return void and remove this if statement.
		 */
		if (rc != SLURM_SUCCESS) {
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
			_queue_teardown(job_ptr->job_id, job_ptr->user_id,
					true);
		}
	}

	return rc;
}

static int _try_alloc_job_bb(void *x, void *arg)
{
	bb_job_queue_rec_t *job_rec = (bb_job_queue_rec_t *) x;
	job_record_t *job_ptr = job_rec->job_ptr;
	bb_job_t *bb_job = job_rec->bb_job;
	int rc;

	if (bb_job->state >= BB_STATE_STAGING_IN)
		return SLURM_SUCCESS; /* Job was already allocated a buffer */

	rc = bb_test_size_limit(job_ptr, bb_job, &bb_state, _queue_teardown);
	if (rc == 0) {
		/*
		 * Job could start now. Allocate burst buffer and continue to
		 * the next job.
		 */
		(void) _alloc_job_bb(job_ptr, bb_job, true);
		rc = SLURM_SUCCESS;
	} else if (rc == 1) /* Exceeds configured limits, try next job */
		rc = SLURM_SUCCESS;
	else /* No space currently available, break out of loop */
		rc = SLURM_ERROR;

	return rc;
}

/*
 * Attempt to allocate resources and begin file staging for pending jobs.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	List job_candidates;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "Mutex locked");

	if (bb_state.last_load_time == 0) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
	}

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(xfree_ptr);
	list_for_each(job_queue, _identify_bb_candidate, job_candidates);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	/* Try to allocate burst buffers for these jobs. */
	bb_set_use_time(&bb_state);
	list_for_each(job_candidates, _try_alloc_job_bb, NULL);

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
	return SLURM_SUCCESS;
}

/* Revoke allocation, but do not release resources.
 * Executed after bb_p_job_begin() if there was an allocation failure.
 * Does not release previously allocated resources.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_revoke_alloc(job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_start_stage_out(job_record_t *job_ptr)
{
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
	return 1;
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
	return 1;
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

/*
 * Translate a burst buffer string to it's equivalent TRES string
 * Caller must xfree the return value
 */
extern char *bb_p_xlate_bb_2_tres_str(char *burst_buffer)
{
	return NULL;
}
