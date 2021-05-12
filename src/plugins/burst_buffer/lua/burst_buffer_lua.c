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

#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include "slurm/slurm.h"

#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/xstring.h"
#include "src/lua/slurm_lua.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"
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
static const char *req_fxns[] = {
	"slurm_bb_job_process",
	"slurm_bb_pools",
	"slurm_bb_job_teardown",
	"slurm_bb_setup",
	"slurm_bb_data_in",
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
	bool hurry;
	uint32_t job_id;
	uint32_t user_id;
	char *job_script;
} teardown_args_t;

typedef struct {
	uint64_t bb_size;
	uint32_t gid;
	uint32_t job_id;
	char *job_script;
	char *nodes_file;
	char *pool;
	uint32_t uid;
} stage_in_args_t;

typedef int (*push_lua_args_cb_t) (lua_State *L, void *args);

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

static int _handle_lua_return_code(lua_State *L, const char *lua_func)
{
	/* Return code is always the bottom of the stack. */
	if (!lua_isnumber(L, 1)) {
		error("%s: %s returned a non-numeric return code, returning error",
		      __func__, lua_func);
		return SLURM_ERROR;
	} else {
		return lua_tonumber(L, 1);
	}
}

static void _print_lua_rc_msg(int rc, const char *lua_func, char **resp_msg)
{
	if (resp_msg) {
		if (rc)
			error("%s returned:%d; response:%s",
			      lua_func, rc, *resp_msg);
		else
			log_flag(BURST_BUF, "%s returned:%d, response:%s",
				 lua_func, rc, *resp_msg);
	} else {
		if (rc)
			error("%s returned:%d", lua_func, rc);
		else
			log_flag(BURST_BUF, "%s returned:%d", lua_func, rc);
	}
}

static int _handle_lua_return(lua_State *L, const char *lua_func,
			      char **ret_str)
{
	int rc = SLURM_SUCCESS;
	int num_stack_elems = lua_gettop(L);

	if (!num_stack_elems) {
		log_flag(BURST_BUF, "%s finished and didn't return anything",
			 lua_func);
		return rc; /* No results, return success. */
	}

	/* Bottom of the stack should be the return code. */
	rc = _handle_lua_return_code(L, lua_func);

	if (num_stack_elems > 1) {
		/*
		 * Multiple results. Right now we only consider up to 2 results,
		 * and the second should be a string.
		 */
		xassert(ret_str);

		if (lua_isstring(L, 2)) {
			xfree(*ret_str);
			*ret_str = xstrdup(lua_tostring(L, 2));
		} else {
			/* Don't know how to handle non-strings here. */
			error("%s: Cannot handle non-string as second return value for lua function %s.",
			      __func__, lua_func);
			rc = SLURM_ERROR;
		}
	}

	_print_lua_rc_msg(rc, lua_func, ret_str);

	/* Pop everything from the stack. */
	lua_pop(L, num_stack_elems);

	return rc;
}

/*
 * Call a function in burst_buffer.lua.
 */
static int _run_lua_script(const char *lua_func,
			   push_lua_args_cb_t push_args_cb,
			   void *callback_args, char **ret_str)
{

	/*
	 * We don't make lua_State L or lua_script_last_loaded static.
	 * If they were static, then only 1 thread could use them at a time.
	 * This would be problematic for performance since these
	 * calls can possibly last a long time. By not making them static it
	 * means we can let these calls run in parallel, but it also means
	 * we don't preserve the previous script. Therefore, we have to
	 * reload the script every time even if the script hasn't changed.
	 * Also, if there is ever a problem loading the script then we can't
	 * fall back to the old script.
	 */
	lua_State *L = NULL;
	time_t lua_script_last_loaded = (time_t) 0;
	int rc, num_args = 0;

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
	 * The callback pushes arguments to the stack which will be used by
	 * the lua function. The callback returns the number of arguments pushed
	 * to the stack.
	 */
	if (push_args_cb)
		num_args = push_args_cb(L, callback_args);

	slurm_lua_stack_dump("burst_buffer/lua", "before lua_pcall", L);
	if (lua_pcall(L, num_args, LUA_MULTRET, 0) != 0) {
		error("%s: %s",
		      lua_script_path, lua_tostring(L, -1));
		rc = SLURM_ERROR;
		lua_pop(L, lua_gettop(L));
	} else {
		slurm_lua_stack_dump("burst_buffer/lua", "after lua_pcall, before returns have been popped", L);
		rc = _handle_lua_return(L, lua_func, ret_str);
	}
	slurm_lua_stack_dump("burst_buffer/lua", "after lua_pcall, after returns have been popped", L);

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

static void _json_parse_pools_object(json_object *jobj, bb_pools_t *pools)
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
				pools->granularity = x;
			} else if (xstrcmp(iter.key, "quantity") == 0) {
				pools->quantity = x;
			} else if (xstrcmp(iter.key, "free") == 0) {
				pools->free = x;
			}
			break;
		case json_type_string:
			p = json_object_get_string(iter.val);
			if (xstrcmp(iter.key, "id") == 0) {
				pools->name = xstrdup(p);
			}
			break;
		default:
			break;
		}
	}
}

static bb_pools_t * _json_parse_pools_array(json_object *jobj, char *key,
					    int *num)
{
	json_object *jarray;
	int i;
	json_object *jvalue;
	bb_pools_t *pools;

	jarray = jobj;
	json_object_object_get_ex(jobj, key, &jarray);

	*num = json_object_array_length(jarray);
	pools = xcalloc(*num, sizeof(bb_pools_t));

	for (i = 0; i < *num; i++) {
		jvalue = json_object_array_get_idx(jarray, i);
		_json_parse_pools_object(jvalue, &pools[i]);
	}

	return pools;
}

static bb_pools_t *_bb_get_pools(int *num_pools, bb_state_t *state_ptr,
				 uint32_t timeout)
{
	int rc;
	char *resp_msg = NULL;
	const char *lua_func_name = "slurm_bb_pools";
	bb_pools_t *pools = NULL;
	json_object *j;
	json_object_iter iter;

	*num_pools = 0;

	/* Call lua function. */
	rc = _run_lua_script(lua_func_name, NULL, NULL, &resp_msg);
	if (rc) {
		trigger_burst_buffer();
		return NULL;
	}
	if (!resp_msg) {
		error("%s returned no pools", lua_func_name);
		return NULL;
	}

	j = json_tokener_parse(resp_msg);
	if (j == NULL) {
		error("json parser failed on \"%s\"",
		      resp_msg);
		xfree(resp_msg);
		return NULL;
	}
	xfree(resp_msg);

	json_object_object_foreachC(j, iter) {
		if (pools) {
			error("Multiple pool objects");
			break;
		}
		pools = _json_parse_pools_array(j, iter.key, num_pools);
	}
	json_object_put(j);	/* Frees json memory */

	return pools;
}

static int _load_pools(uint32_t timeout)
{
	static bool first_run = true;
	bool have_new_pools = false;
	int num_pools = 0, i, j, pools_inx;
	burst_buffer_pool_t *pool_ptr;
	bb_pools_t *pools;
	bitstr_t *pools_bitmap;

	/* Load the pools information from burst_buffer.lua. */
	pools = _bb_get_pools(&num_pools, &bb_state, timeout);
	if (!pools) {
		error("Didn't find any pools from burst_buffer.lua. Cannot use burst buffers until pools are defined");
		return SLURM_ERROR;
	}

	pools_bitmap = bit_alloc(bb_state.bb_config.pool_cnt + num_pools);
	slurm_mutex_lock(&bb_state.bb_mutex);

	/* TODO: Set default pool. */

	/* Put found pools into bb_state.bb_config.pool_ptr. */
	for (i = 0; i < num_pools; i++) {
		bool found_pool = false;
		pool_ptr = bb_state.bb_config.pool_ptr;
		for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
			if (!xstrcmp(pool_ptr->name, pools[i].name)) {
				found_pool = true;
				break;
			}
		}
		if (!found_pool) {
			have_new_pools = true;
			/* This is a new pool. Add it to bb_state. */
			if (!first_run)
				info("Newly reported pool %s", pools[i].name);
			bb_state.bb_config.pool_ptr =
				xrealloc(bb_state.bb_config.pool_ptr,
					 sizeof(burst_buffer_pool_t) *
					 (bb_state.bb_config.pool_cnt + 1));
			pool_ptr = bb_state.bb_config.pool_ptr +
				bb_state.bb_config.pool_cnt;
			pool_ptr->name = xstrdup(pools[i].name);
			bb_state.bb_config.pool_cnt++;
		}

		pools_inx = pool_ptr - bb_state.bb_config.pool_ptr;
		bit_set(pools_bitmap, pools_inx);

		if (!pools[i].granularity) {
			info("Granularity cannot be zero. Setting granularity to 1 for pool %s",
			     pool_ptr->name);
			pools[i].granularity = 1;
		}
		/*
		 * TODO: Put some sanity checks on values here
		 * (check for overflow or underflow)?
		 */
		pool_ptr->total_space = pools[i].quantity *
			pools[i].granularity;
		pool_ptr->granularity = pools[i].granularity;
		pool_ptr->unfree_space = pools[i].quantity - pools[i].free;
		pool_ptr->unfree_space *= pools[i].granularity;
	}

	pool_ptr = bb_state.bb_config.pool_ptr;
	for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
		if (bit_test(pools_bitmap, j) || (pool_ptr->total_space == 0)) {
			if (have_new_pools)
				log_flag(BURST_BUF, "Pool name=%s, granularity=%"PRIu64", total_space=%"PRIu64", used_space=%"PRIu64", unfree_space=%"PRIu64,
					 pool_ptr->name, pool_ptr->granularity,
					 pool_ptr->total_space,
					 pool_ptr->used_space,
					 pool_ptr->unfree_space);
			continue;
		}
		error("Pool %s is no longer reported by the system, setting size to zero",
		      pool_ptr->name);
		pool_ptr->total_space = 0;
		pool_ptr->used_space = 0;
		pool_ptr->unfree_space = 0;
	}
	first_run = false;
	slurm_mutex_unlock(&bb_state.bb_mutex);
	FREE_NULL_BITMAP(pools_bitmap);
	_bb_free_pools(pools, num_pools);

	return SLURM_SUCCESS;
}

static void _load_state(bool init_config)
{
	uint32_t timeout;

	slurm_mutex_lock(&bb_state.bb_mutex);
	timeout = bb_state.bb_config.other_timeout * 1000;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	if (_load_pools(timeout) != SLURM_SUCCESS)
		return;

	bb_state.last_load_time = time(NULL);

	if (!init_config)
		return;

	/* Load allocated burst buffers from state files. */
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
	int rc;

        if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
                return rc;

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

	slurm_lua_fini();

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
 * For interactive jobs, build a script containing the relevant burst buffer
 * commands, as needed by the Lua API.
 */
static int _build_bb_script(job_record_t *job_ptr, char *script_file)
{
	char *out_buf = NULL;
	int rc, fd;

	xassert(job_ptr);
	xassert(job_ptr->burst_buffer);

	/* Open file */
	(void) unlink(script_file);
	fd = creat(script_file, 0600);
	if (fd < 0) {
		rc = errno;
		error("Error creating file %s, %m", script_file);
		return rc;
	}

	/* Write burst buffer specification to the file. */
	xstrcat(out_buf, "#!/bin/bash\n");
	xstrcat(out_buf, job_ptr->burst_buffer);
	safe_write(fd, out_buf, strlen(out_buf));

	xfree(out_buf);
	(void) close(fd);

	return SLURM_SUCCESS;

rwfail:
	error("Failed to write %s to %s", out_buf, script_file);
	xfree(out_buf);
	(void) close(fd);

	return SLURM_ERROR;
}

/* Push the path to the job script. Return 1 (number of arguments pushed). */
static int _push_job_process_args(lua_State *L, void *args)
{
	char *script_file = (char *) args;

	xassert(script_file);

	lua_pushstring(L, script_file);

	return 1;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate2(job_record_t *job_ptr, char **err_msg)
{
	const char *lua_func_name = "slurm_bb_job_process";
	int rc = SLURM_SUCCESS, fd = -1, hash_inx;
	char *hash_dir = NULL, *job_dir = NULL, *script_file = NULL;
	char *task_script_file = NULL, *resp_msg = NULL;
	bool using_master_script = false;
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
		if (job_ptr->batch_flag == 0) {
			rc = _build_bb_script(job_ptr, script_file);
		}
	}


	/* Run "job_process" function, validates user script */
	rc = _run_lua_script(lua_func_name, _push_job_process_args,
			     script_file, &resp_msg);
	if (rc) {
		if (err_msg && resp_msg) {
			xfree(*err_msg);
			xstrfmtcat(*err_msg, "%s: %s",
				   plugin_type, resp_msg);
		}
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}
	xfree(resp_msg);

	/* Clean up */
	xfree(hash_dir);
	xfree(job_dir);
	if (rc != SLURM_SUCCESS) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job_del(&bb_state, job_ptr->job_id);
		slurm_mutex_unlock(&bb_state.bb_mutex);
	} else if (using_master_script) {
		/*
		 * Job arrays need to have script file in the "standard"
		 * location for the remaining logic. Make hard link.
		 */
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
			error("%s: link(%s,%s): %m",
			      __func__, script_file, task_script_file);
		}
	}
	xfree(task_script_file);
	xfree(script_file);

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

/*
 * Purge files we have created for the job.
 * bb_state.bb_mutex is locked on function entry.
 * job_ptr may be NULL if not found
 */
static void _purge_bb_files(uint32_t job_id, job_record_t *job_ptr)
{
	char *hash_dir = NULL, *job_dir = NULL;
	char *script_file = NULL;
	int hash_inx;

	hash_inx = job_id % 10;
	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_id);
	(void) mkdir(job_dir, 0700);

	if (!job_ptr || (job_ptr->batch_flag == 0)) {
		xstrfmtcat(script_file, "%s/script", job_dir);
		(void) unlink(script_file);
		xfree(script_file);
	}

	(void) unlink(job_dir);
	xfree(job_dir);
	xfree(hash_dir);
}

/*
 * TODO: This was copied from burst_buffer_datawarp.c. Should I put it
 * in burst_buffer_common.c?
 */
static void _update_system_comment(job_record_t *job_ptr, char *operation,
				   char *resp_msg, bool update_database)
{
	char *sep = NULL;

	if (job_ptr->system_comment &&
	    (strlen(job_ptr->system_comment) >= 1024)) {
		/* Avoid filling comment with repeated BB failures */
		return;
	}

	if (job_ptr->system_comment)
		xstrftimecat(sep, "\n%x %X");
	else
		xstrftimecat(sep, "%x %X");
	xstrfmtcat(job_ptr->system_comment, "%s %s: %s: %s",
		   sep, plugin_type, operation, resp_msg);
	xfree(sep);

	if (update_database) {
		slurmdb_job_cond_t job_cond;
		slurmdb_job_rec_t job_rec;
		slurm_selected_step_t selected_step;
		List ret_list;

		memset(&job_cond, 0, sizeof(slurmdb_job_cond_t));
		memset(&job_rec, 0, sizeof(slurmdb_job_rec_t));
		memset(&selected_step, 0, sizeof(slurm_selected_step_t));

		selected_step.array_task_id = NO_VAL;
		selected_step.step_id.job_id = job_ptr->job_id;
		selected_step.het_job_offset = NO_VAL;
		selected_step.step_id.step_id = NO_VAL;
		selected_step.step_id.step_het_comp = NO_VAL;
		job_cond.step_list = list_create(NULL);
		list_append(job_cond.step_list, &selected_step);

		job_cond.flags = JOBCOND_FLAG_NO_WAIT |
			JOBCOND_FLAG_NO_DEFAULT_USAGE;

		job_cond.cluster_list = list_create(NULL);
		list_append(job_cond.cluster_list, slurm_conf.cluster_name);

		job_cond.usage_start = job_ptr->details->submit_time;

		job_rec.system_comment = job_ptr->system_comment;

		ret_list = acct_storage_g_modify_job(acct_db_conn,
		                                     slurm_conf.slurm_user_id,
		                                     &job_cond, &job_rec);

		FREE_NULL_LIST(job_cond.cluster_list);
		FREE_NULL_LIST(job_cond.step_list);
		FREE_NULL_LIST(ret_list);
	}
}


static int _push_teardown_args(lua_State *L, void *args)
{
	teardown_args_t *teardown_args = (teardown_args_t *) args;

	lua_pushinteger(L, teardown_args->job_id);
	lua_pushinteger(L, teardown_args->user_id);
	lua_pushstring(L, teardown_args->job_script);
	lua_pushboolean(L, teardown_args->hurry);

	/* Pushed 4 arguments. */
	return 4;
}

static void *_start_teardown(void *x)
{
	int rc;
	char *resp_msg = NULL;
	teardown_args_t *teardown_args = (teardown_args_t *)x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job = NULL;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	DEF_TIMERS;

	/* Run lua "teardown" function */
	START_TIMER;
	rc = _run_lua_script("slurm_bb_job_teardown", _push_teardown_args,
			     teardown_args, &resp_msg);
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

	xfree(resp_msg);
	xfree(teardown_args->job_script);
	xfree(teardown_args);

	return NULL;
}

static void _queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry)
{
	char *hash_dir = NULL, *job_script = NULL;
	int hash_inx = job_id % 10;
	struct stat buf;
	teardown_args_t *teardown_args;
	pthread_t tid;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	xstrfmtcat(job_script, "%s/job.%u/script", hash_dir, job_id);
	if (stat(job_script, &buf) == -1) {
		int fd = creat(job_script, 0755);
		if (fd >= 0) {
			int len;
			char *dummy_script = "#!/bin/bash\nexit 0\n";
			len = strlen(dummy_script) + 1;
			if (write(fd, dummy_script, len) != len) {
				verbose("%s: write(%s): %m",
					__func__, job_script);
			}
			close(fd);
		}
	}

	teardown_args = xmalloc(sizeof *teardown_args);
	teardown_args->job_id = job_id;
	teardown_args->user_id = user_id;
	teardown_args->job_script = job_script;
	teardown_args->hurry = hurry;

	slurm_thread_create(&tid, _start_teardown, teardown_args);

	xfree(hash_dir);
}


/* Push arguments to the lua stack. Return the number of arguments pushed. */
static int _push_data_in_args(lua_State *L, void *args)
{
	stage_in_args_t *data_in_args = (stage_in_args_t *) args;

	lua_pushinteger(L, data_in_args->job_id);
	lua_pushstring(L, data_in_args->job_script);

	return 2;
}

/* Push arguments to the lua stack. Return the number of arguments pushed. */
static int _push_setup_args(lua_State *L, void *args)
{
	stage_in_args_t *setup_args = (stage_in_args_t *) args;

	lua_pushinteger(L, setup_args->job_id);
	lua_pushinteger(L, setup_args->uid);
	lua_pushinteger(L, setup_args->gid);
	lua_pushstring(L, setup_args->pool);
	lua_pushinteger(L, setup_args->bb_size);
	lua_pushstring(L, setup_args->job_script);

	return 6;
}

static void *_start_stage_in(void *x)
{
	int rc;
	bool get_real_size = false;
	char *resp_msg = NULL, *op = NULL;
	stage_in_args_t *stage_in_args = (stage_in_args_t *) x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job;
	slurmctld_lock_t job_write_lock = { .job = WRITE_LOCK };

	DEF_TIMERS;

	op = "setup";
	START_TIMER;
	rc = _run_lua_script("slurm_bb_setup", _push_setup_args, stage_in_args,
			     &resp_msg);
	END_TIMER;
	info("slurm_bb_setup for job JobId=%u ran for %s",
	     stage_in_args->job_id, TIME_STR);

	slurm_mutex_lock(&bb_state.bb_mutex);
	/*
	 * The buffer's actual size may be larger than requested by the user.
	 * Remove limit here and restore limit based upon actual size below
	 * (assuming buffer allocation succeeded, or just leave it out).
	 */
	bb_limit_rem(stage_in_args->uid, stage_in_args->bb_size,
		     stage_in_args->pool, &bb_state);
	if (rc != SLURM_SUCCESS) {
		/*
		 * Unlock bb_mutex before locking job_write_lock to avoid
		 * deadlock, since job_write_lock is always locked first.
		 */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		trigger_burst_buffer();
		error("setup for JobId=%u failed.", stage_in_args->job_id);
		rc = SLURM_ERROR;
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(stage_in_args->job_id);
		if (job_ptr)
			_update_system_comment(job_ptr, "setup", resp_msg, 0);
		unlock_slurmctld(job_write_lock);
	} else {
		bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
		if (!bb_job) {
			error("unable to find bb_job record for JobId=%u",
			      stage_in_args->job_id);
			rc = SLURM_ERROR;
		} else if (bb_job->total_size) {
			/* Restore limit based upon actual size. */
			bb_limit_add(stage_in_args->uid, bb_job->total_size,
				     stage_in_args->pool, &bb_state, true);
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}

	if (rc == SLURM_SUCCESS) {
		xfree(resp_msg);
		op = "data_in";
		START_TIMER;
		rc =_run_lua_script("slurm_bb_data_in", _push_data_in_args,
				    stage_in_args, &resp_msg);
		END_TIMER;
		info("slurm_bb_data_in for JobId=%u ran for %s",
		     stage_in_args->job_id, TIME_STR);
		if (rc != SLURM_SUCCESS) {
			trigger_burst_buffer();
			error("slurm_bb_data_in for JobId=%u failed.",
			      stage_in_args->job_id);
			rc = SLURM_ERROR;
			lock_slurmctld(job_write_lock);
			job_ptr = find_job_record(stage_in_args->job_id);
			if (job_ptr)
				_update_system_comment(job_ptr, "data_in",
						       resp_msg, 0);
			unlock_slurmctld(job_write_lock);
		}
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
	if (bb_job && bb_job->req_size)
		get_real_size = true;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	/*
	 * TODO:
	 * Round up job buffer size based upon "equalize_fragments"
	 * configuration parameter.
	 */
	if (get_real_size) {
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_in_args->job_id);
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_in_args->job_id);
	} else if (rc == SLURM_SUCCESS) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
		if (bb_job)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		if (bb_job && bb_job->total_size) {
			/*
			 * TODO: adjust total size to real size if real size
			 * returns something different.
			 */
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
		job_ptr->priority = 0; /* Hold job */
		bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
		if (bb_alloc) {
			bb_alloc->state_time = time(NULL);
			bb_state.last_update_time = time(NULL);
			if (bb_state.bb_config.flags &
			    BB_FLAG_TEARDOWN_FAILURE) {
				bb_alloc->state = BB_STATE_TEARDOWN;
				bb_job = bb_job_find(&bb_state,
						     stage_in_args->job_id);
				if (bb_job)
					bb_set_job_bb_state(job_ptr, bb_job,
							    BB_STATE_TEARDOWN);
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

	xfree(stage_in_args->job_script);
	xfree(stage_in_args->nodes_file);
	xfree(stage_in_args->pool);
	xfree(stage_in_args);

	return NULL;
}

static int _queue_stage_in(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *hash_dir = NULL, *job_dir = NULL, *job_pool;
	char *client_nodes_file_nid = NULL;
	int hash_inx = job_ptr->job_id % 10;
	stage_in_args_t *stage_in_args;
	bb_alloc_t *bb_alloc = NULL;
	pthread_t tid;

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

	stage_in_args = xmalloc(sizeof *stage_in_args);
	stage_in_args->job_id = job_ptr->job_id;
	stage_in_args->uid = job_ptr->user_id;
	stage_in_args->gid = job_ptr->group_id;
	if (bb_job->job_pool)
		job_pool = bb_job->job_pool;
	else
		job_pool = bb_state.bb_config.default_pool;
	stage_in_args->pool = xstrdup(job_pool);
	stage_in_args->bb_size = bb_job->total_size;
	stage_in_args->job_script = bb_handle_job_script(job_ptr, bb_job);
	if (client_nodes_file_nid) {
		stage_in_args->nodes_file = client_nodes_file_nid;
		client_nodes_file_nid = NULL;
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

	slurm_thread_create(&tid, _start_stage_in, stage_in_args);

	xfree(hash_dir);
	xfree(job_dir);

	return SLURM_SUCCESS;
}

static int _alloc_job_bb(job_record_t *job_ptr, bb_job_t *bb_job,
			 bool job_ready)
{
	int rc = SLURM_SUCCESS;

	log_flag(BURST_BUF, "start job allocate %pJ", job_ptr);

	if (bb_job->state < BB_STATE_STAGING_IN) {
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_STAGING_IN);
		_queue_stage_in(job_ptr, bb_job);
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
