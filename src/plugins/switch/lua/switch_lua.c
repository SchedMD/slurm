/***************************************************************************
* Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
***************************************************************************/

#include "slurm/slurm.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/pack_data.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xstring.h"
#include "src/interfaces/switch.h"
#include "src/lua/slurm_lua.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "switch lua plugin";
const char plugin_type[] = "switch/lua";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static char *lua_script_path;
static time_t lua_script_last_loaded = (time_t) 0;
static lua_State *L = NULL;
static bool block_job_complete = false;
static bool setup_special_steps = false;

static const char *req_fxns[] = { NULL };

static const char log_ctx_key[] = "switch/lua log context";

/* Prefix log lines from the script with lua_function and job_id. */
static void _set_log_ctx(lua_State *L, const char *lua_function,
			 uint32_t job_id)
{
	char *ctx = NULL;

	if (job_id)
		ctx = xstrdup_printf("%s JobId=%u", lua_function, job_id);
	else
		ctx = xstrdup(lua_function);

	lua_pushstring(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, log_ctx_key);
	xfree(ctx);
}

static void _clear_log_ctx(lua_State *L)
{
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, log_ctx_key);
}

static char *_log_prefix(lua_State *L)
{
	char *prefix = NULL;
	const char *ctx;

	lua_getfield(L, LUA_REGISTRYINDEX, log_ctx_key);
	if ((ctx = lua_tostring(L, -1)))
		prefix = xstrdup_printf("%s: %s", plugin_type, ctx);
	else
		prefix = xstrdup(plugin_type);
	lua_pop(L, 1);

	return prefix;
}

/* slurm.log([level,] msg), with the levels of the generic slurm.log */
static int _log_lua_msg(lua_State *L)
{
	int level = 0;
	const char *msg;
	char *prefix;

	if (lua_gettop(L) > 1) {
		level = (int) lua_tonumber(L, 1);
		msg = lua_tostring(L, 2);
	} else {
		msg = lua_tostring(L, 1);
	}

	prefix = _log_prefix(L);

	if (level > 4)
		debug4("%s: %s", prefix, msg);
	else if (level == 4)
		debug3("%s: %s", prefix, msg);
	else if (level == 3)
		debug2("%s: %s", prefix, msg);
	else if (level == 2)
		debug("%s: %s", prefix, msg);
	else if (level == 1)
		verbose("%s: %s", prefix, msg);
	else if (level == 0)
		info("%s: %s", prefix, msg);

	xfree(prefix);
	return 0;
}

/* slurm.error(msg) */
static int _log_lua_error(lua_State *L)
{
	const char *msg = lua_tostring(L, 1);
	char *prefix = _log_prefix(L);

	error("%s: %s", prefix, msg);

	xfree(prefix);
	return 0;
}

/* slurm.user_msg() has no user to reach here, so it logs instead. */
static int _log_lua_user_msg(lua_State *L)
{
	const char *msg = lua_tostring(L, 1);
	char *prefix = _log_prefix(L);

	info("%s: user message: %s", prefix, msg);

	xfree(prefix);
	return 0;
}

static const struct luaL_Reg slurm_functions[] = {
	{ "log", _log_lua_msg },
	{ "error", _log_lua_error },
	{ "user_msg", _log_lua_user_msg },
	{ NULL, NULL },
};

static void _loadscript_extra(lua_State *L)
{
	slurm_lua_table_register(L, NULL, slurm_functions);
	/* Must be done after the slurm_functions are registered */
	lua_setglobal(L, "slurm");

	/* print() as the Lua manual describes it, but into the log */
	luaL_loadstring(L, "local t = {} "
			   "for i = 1, select('#', ...) do "
			   "t[i] = tostring((select(i, ...))) "
			   "end "
			   "slurm.log(0, table.concat(t, '\\t'))");
	lua_setglobal(L, "print");
}

/* Required for switch plugins: */
const uint32_t plugin_id = SWITCH_PLUGIN_LUA;

/* Call a no-argument Lua function; undefined or failing counts as false. */
static bool _call_bool_function(const char *lua_function)
{
	bool ret = false;
	int rc = SLURM_SUCCESS;
	char *err = NULL;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded,
				       _loadscript_extra, NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto out;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		goto out;
	}

	_set_log_ctx(L, lua_function, 0);
	rc = slurm_lua_pcall(L, 0, 1, &err, __func__);
	_clear_log_ctx(L);
	if (rc) {
		error("%s: %s: %s failed: %s", plugin_type, lua_script_path,
		      lua_function, err);
		xfree(err);
		goto out;
	}

	ret = lua_toboolean(L, -1);
	lua_pop(L, 1);

out:
	return ret;
}

extern int init(void)
{
	int rc = SLURM_SUCCESS;

	if (!running_in_slurmctld() && !running_in_slurmstepd())
		return SLURM_SUCCESS;

	lua_script_path = get_extra_conf_path("switch.lua");

	if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
		return rc;

	rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra,
				  NULL);

	if (rc != SLURM_SUCCESS)
		return rc;

	block_job_complete =
		_call_bool_function("slurm_switch_block_job_complete");
	setup_special_steps =
		_call_bool_function("slurm_switch_setup_special_steps");

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	if (L) {
		lua_close(L);
		L = NULL;
	}
	slurm_lua_fini();
	xfree(lua_script_path);
}

static int _lua_step_rec_field(lua_State *L, const stepd_step_rec_t *step,
			       const char *name)
{
	if (!step) {
		error("%s: step is NULL", __func__);
		lua_pushnil(L);
	} else if (!xstrcmp(name, "job_id")) {
		lua_pushinteger(L, step->step_id.job_id);
	} else if (!xstrcmp(name, "partition")) {
		if (step->msg && step->msg->cred && step->msg->cred->arg)
			lua_pushstring(L, step->msg->cred->arg->job_partition);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "sluid")) {
		char sluid_str[SLUID_STR_BYTES];

		print_sluid(step->step_id.sluid, sluid_str, sizeof(sluid_str));
		lua_pushstring(L, sluid_str);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

static int _lua_step_rec_field_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	stepd_step_rec_t *step;

	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_step_rec_ptr");
	step = lua_touserdata(L, -1);

	return _lua_step_rec_field(L, step, name);
}

static void _lua_push_step_rec(stepd_step_rec_t *step, lua_State *L)
{
	/* A table whose fields _lua_step_rec_field_index() reads from step */
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, _lua_step_rec_field_index);
	lua_setfield(L, -2, "__index");
	lua_pushlightuserdata(L, step);
	lua_setfield(L, -2, "_step_rec_ptr");
	lua_setmetatable(L, -2);
}

extern int switch_p_save(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_restore(bool recover)
{
	return SLURM_SUCCESS;
}

/* jobinfo is a data_t, packed as an exists flag followed by pack_data(). */
extern void switch_p_jobinfo_pack(void *job_info, buf_t *buffer,
				  uint16_t protocol_version)
{
	if (!job_info) {
		packbool(false, buffer);
		return;
	}
	packbool(true, buffer);
	if (pack_data(job_info, protocol_version, buffer))
		error("switch/lua: %s: failed to pack jobinfo", __func__);
}

extern int switch_p_jobinfo_unpack(void **job_info, buf_t *buffer,
				   uint16_t protocol_version)
{
	bool exists;
	data_t *data = NULL;

	xassert(job_info);

	*job_info = NULL;
	safe_unpackbool(&exists, buffer);
	if (!exists)
		return SLURM_SUCCESS;

	data = data_new();
	safe_unpack_data(data, protocol_version, buffer);

	*job_info = data;
	return SLURM_SUCCESS;
unpack_error:
	FREE_NULL_DATA(data);
	return SLURM_ERROR;
}

extern void switch_p_jobinfo_free(job_record_t *job_ptr)
{
	xassert(job_ptr);
	FREE_NULL_DATA(job_ptr->switch_jobinfo);
}

/* switch/lua stepinfo is just a copy of the jobinfo data_t */
extern int switch_p_stepinfo_build(void **step_info, void *switch_jobinfo,
				   step_record_t *step_ptr)
{
	data_t *data_step = NULL;

	if (!step_info || !switch_jobinfo)
		return SLURM_SUCCESS;

	data_step = data_new();
	if (!data_copy(data_step, switch_jobinfo)) {
		error("switch/lua: %s: failed to copy jobinfo to stepinfo",
		      __func__);
		FREE_NULL_DATA(data_step);
		return SLURM_ERROR;
	}
	*step_info = data_step;

	return SLURM_SUCCESS;
}

extern void switch_p_stepinfo_duplicate(void *source, void **dest)
{
	data_t *data_step = NULL;

	if (!source || !dest)
		return;

	data_step = data_new();
	if (!data_copy(data_step, source)) {
		error("switch/lua: %s: failed to duplicate stepinfo", __func__);
		FREE_NULL_DATA(data_step);
		return;
	}
	*dest = data_step;
}

extern void switch_p_stepinfo_free(void *step_info)
{
	FREE_NULL_DATA(step_info);
}

extern void switch_p_stepinfo_pack(void *step_info, buf_t *buffer,
				   uint16_t protocol_version)
{
	switch_p_jobinfo_pack(step_info, buffer, protocol_version);
}

extern int switch_p_stepinfo_unpack(void **step_info, buf_t *buffer,
				    uint16_t protocol_version)
{
	return switch_p_jobinfo_unpack(step_info, buffer, protocol_version);
}

/*
 * Whether slurm_switch_job_init() also runs for the batch and interactive
 * steps, as slurm_switch_setup_special_steps() answered at load.
 */
extern bool switch_p_setup_special_steps(void)
{
	return setup_special_steps;
}

/* Interpret numeric response from a lua function. */
static int _get_lua_rc(lua_State *L, const char *lua_function, int index)
{
	int rc;

	if (lua_isnumber(L, index)) {
		rc = lua_tonumber(L, index);
	} else {
		error("switch/lua: %s: non-numeric return code, assuming success",
		      lua_function);
		return SLURM_SUCCESS;
	}
	if (rc) {
		const char *msg = rc > 0 ? "Not enforced" : "Enforced";
		error("switch/lua: %s: reported failure (%d), %s",
		      lua_function, rc, msg);
	}
	if (rc > 0)
		rc = SLURM_SUCCESS;
	return rc;
}

static data_t *_get_data_stepinfo(stepd_step_rec_t *step_ptr)
{
	if (!step_ptr || !step_ptr->switch_step || !step_ptr->switch_step->data)
		return NULL;
	return (data_t *) step_ptr->switch_step->data;
}

/* Runs in slurmstepd once per node for every normal step. */
extern int switch_p_job_preinit(stepd_step_rec_t *step)
{
	int rc = SLURM_SUCCESS;
	const char *lua_function = "slurm_switch_job_preinit";
	data_t *data = NULL;
	char *err = NULL;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded,
				       _loadscript_extra, NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto end;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		rc = SLURM_SUCCESS;
		goto end;
	}

	_lua_push_step_rec(step, L);

	data = _get_data_stepinfo(step);
	if (data) {
		int top = lua_gettop(L);
		if (slurm_lua_from_data(L, data)) {
			error("switch/lua: %s: failed to push switch_stepinfo",
			      __func__);
			lua_settop(L, top);
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}

	_set_log_ctx(L, lua_function, step->step_id.job_id);
	rc = slurm_lua_pcall(L, 2, 1, &err, __func__);
	_clear_log_ctx(L);
	if (rc) {
		error("%s: %s: %s failed: %s", plugin_type, lua_script_path,
		      lua_function, err);
		xfree(err);

		/* if a script fails, don't blow up the job start */
		rc = SLURM_SUCCESS;
	} else {
		rc = _get_lua_rc(L, lua_function, -1);
		lua_pop(L, 1);
	}

end:
	return rc;
}

static int _apply_env_table(lua_State *L, char ***env)
{
	int rc = SLURM_SUCCESS;
	int table;

	if (lua_isnoneornil(L, -1)) {
		lua_pop(L, 1);
		return SLURM_SUCCESS;
	}

	if (!lua_istable(L, -1)) {
		error("switch/lua: expected a table of env vars, got: %s",
		      luaL_typename(L, -1));
		lua_pop(L, 1);
		return SLURM_ERROR;
	}

	table = lua_gettop(L);

	lua_pushnil(L);

	while (lua_next(L, table)) {
		/* stack: -1 value, -2 name */
		const char *name, *value = NULL;

		if (lua_type(L, -2) != LUA_TSTRING) {
			error("switch/lua: %s: ignoring non-string name of type %s",
			      __func__, luaL_typename(L, -2));
			lua_pop(L, 1);
			continue;
		}

		name = lua_tostring(L, -2);

		switch (lua_type(L, -1)) {
		case LUA_TSTRING:
		case LUA_TNUMBER:
			value = lua_tostring(L, -1);
			break;
		case LUA_TBOOLEAN:
			if (lua_toboolean(L, -1))
				value = "1";
			else
				unsetenvp(*env, name);
			break;
		default:
			error("switch/lua: %s: skipping %s, unsupported value or type: %s",
			      __func__, name, luaL_typename(L, -1));
			rc = SLURM_ERROR;
			break;
		}

		if (value && !env_array_overwrite(env, name, value)) {
			error("switch/lua: %s: failed to set: %s", __func__,
			      name);
			rc = SLURM_ERROR;
		}

		/* Pop the value, leave the key for the next lua_next(). */
		lua_pop(L, 1);
	}

	lua_pop(L, 1);

	return rc;
}

/*
 * Runs in slurmstepd once per node for every normal step, and for the batch
 * and interactive steps if slurm_switch_setup_special_steps() returned true.
 * The script returns a code and a table of environment variables to set.
 */
extern int switch_p_job_init(stepd_step_rec_t *step)
{
	int rc = SLURM_SUCCESS;
	const char *lua_function = "slurm_switch_job_init";
	data_t *data = NULL;
	char *err = NULL;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded,
				       _loadscript_extra, NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto end;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		rc = SLURM_SUCCESS;

		goto end;
	}

	_lua_push_step_rec(step, L);

	data = _get_data_stepinfo(step);
	if (data) {
		int top = lua_gettop(L);
		if (slurm_lua_from_data(L, data)) {
			error("switch/lua: %s: failed to push switch_stepinfo",
			      __func__);
			lua_settop(L, top);
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}

	_set_log_ctx(L, lua_function, step->step_id.job_id);
	rc = slurm_lua_pcall(L, 2, 2, &err, __func__);
	_clear_log_ctx(L);
	if (rc) {
		error("%s: %s: %s failed: %s", plugin_type, lua_script_path,
		      lua_function, err);
		xfree(err);
		/* if a script fails, don't blow up the job start */
		rc = SLURM_SUCCESS;
		goto end;
	} else {
		rc = _get_lua_rc(L, lua_function, -2);

		/* apply env changes, side effect is popping the table */
		if (_apply_env_table(L, &step->env)) {
			error("switch/lua: %s: failed to apply env updates",
			      lua_function);
		}

		lua_pop(L, 1);
	}
end:
	return rc;
}

/*
 * Teardown counterpart of switch_p_job_preinit(). A negative return drains the
 * node; a positive one or a script error is only logged.
 */
extern int switch_p_job_postfini(stepd_step_rec_t *step)
{
	int rc = SLURM_SUCCESS;
	const char *lua_function = "slurm_switch_job_postfini";
	data_t *data = NULL;
	char *err = NULL;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded,
				       _loadscript_extra, NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto end;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		rc = SLURM_SUCCESS;
		goto end;
	}

	_lua_push_step_rec(step, L);
	data = _get_data_stepinfo(step);
	if (data) {
		int top = lua_gettop(L);
		if (slurm_lua_from_data(L, data)) {
			error("switch/lua: %s: failed to push switch_stepinfo",
			      __func__);
			lua_settop(L, top);
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}

	_set_log_ctx(L, lua_function, step->step_id.job_id);
	rc = slurm_lua_pcall(L, 2, 1, &err, __func__);
	_clear_log_ctx(L);
	if (rc) {
		error("%s: %s: %s failed: %s", plugin_type, lua_script_path,
		      lua_function, err);
		xfree(err);
		/* if a script fails, don't drain all the nodes */
		rc = SLURM_SUCCESS;
	} else {
		rc = _get_lua_rc(L, lua_function, -1);
		lua_pop(L, 1);
	}

end:
	return rc;
}

extern int switch_p_job_attach(void *stepinfo, char ***env, uint32_t nodeid,
			       uint32_t procid, uint32_t nnodes,
			       uint32_t nprocs, uint32_t rank)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_complete(void *stepinfo, char *nodelist)
{
	return SLURM_SUCCESS;
}

static job_info_msg_t *_pack_job_info(job_record_t *job_ptr)
{
	buf_t *info_buf = NULL;
	list_t *job_ids = NULL;
	job_info_msg_t *job_info = NULL;
	slurm_msg_t msg = {
		.auth = SLURM_MSG_AUTH_INIT_AS_INTERNAL,
		.protocol_version = SLURM_PROTOCOL_VERSION,
	};

	job_ids = list_create(NULL);
	list_append(job_ids, &job_ptr->job_id);
	info_buf = pack_spec_jobs(&msg, job_ids, SHOW_DETAIL);

	if (info_buf) {
		slurm_msg_t *info_msg = xmalloc(sizeof *info_msg);
		slurm_msg_t_init(info_msg);
		info_msg->protocol_version = SLURM_PROTOCOL_VERSION;
		info_msg->msg_type = RESPONSE_JOB_INFO;

		set_buf_offset(info_buf, 0);
		unpack_msg(info_msg, info_buf);
		job_info = info_msg->data;
		info_msg->data = NULL;
		slurm_free_msg(info_msg);

		xassert(job_info);
	}

	return job_info;
}

/*
 * Run slurm_switch_job_start() for a newly allocated job. A table it returns
 * becomes the job's switch jobinfo; nil leaves the job unmanaged.
 */
extern int switch_p_job_start(job_record_t *job_ptr, bool test_only)
{
	job_info_msg_t *job_info_msg = NULL;
	job_info_t *job_info = NULL;
	int rc = SLURM_SUCCESS;
	const char *lua_function = "slurm_switch_job_start";

	if (test_only)
		return SLURM_SUCCESS;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded, NULL,
				       NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto end;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		rc = SLURM_SUCCESS;
		goto end;
	}

	job_info_msg = _pack_job_info(job_ptr);
	if (!job_info_msg || !job_info_msg->record_count) {
		error("switch/lua: %s: failed to pack job.", __func__);
		rc = SLURM_ERROR;
		goto end;
	}
	job_info = &job_info_msg->job_array[0];

	slurm_lua_push_job_info(job_info, L);

	if (lua_pcall(L, 1, 2, 0)) {
		error("switch/lua: %s(): %s: %s", lua_function,
		      lua_script_path, lua_tostring(L, -1));
		lua_pop(L, 1);
		rc = SLURM_ERROR;
		goto end;
	}
	if (lua_isnumber(L, -2)) {
		rc = lua_tonumber(L, -2);
	} else {
		error("switch/lua: %s(): non-numeric return code, deferring job.",
		      lua_function);
		rc = SLURM_ERROR;
	}

	if (lua_isnoneornil(L, -1)) {
		FREE_NULL_DATA(job_ptr->switch_jobinfo);
	} else {
		data_t *data = NULL;
		if (slurm_lua_to_data(L, &data)) {
			error("switch/lua: %s(): failed to parse return",
			      lua_function);
			FREE_NULL_DATA(data);
			rc = SLURM_ERROR;
		} else {
			FREE_NULL_DATA(job_ptr->switch_jobinfo);
			job_ptr->switch_jobinfo = data;
		}
	}
	lua_pop(L, 2);

end:
	if (job_info_msg)
		slurm_free_job_info_msg(job_info_msg);
	return rc;
}

/*
 * Run slurm_switch_job_complete() for a job that has switch jobinfo. Any
 * failure drains the job's nodes.
 */
extern int switch_p_job_complete(job_record_t *job_ptr,
				 switch_complete_cb_t complete,
				 switch_complete_mode_t *mode)
{
	job_info_msg_t *job_info_msg = NULL;
	job_info_t *job_info = NULL;
	int rc = SLURM_SUCCESS;
	const char *lua_function = "slurm_switch_job_complete";

	if (!job_ptr->switch_jobinfo)
		return SLURM_SUCCESS;

	*mode = SWITCH_COMPLETE_MODE_SYNC;

	if ((rc = slurm_lua_loadscript(&L, "switch/lua", lua_script_path,
				       req_fxns, &lua_script_last_loaded, NULL,
				       NULL))) {
		error("switch/lua: %s: failed to load script", __func__);
		goto end;
	}

	lua_getglobal(L, lua_function);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		rc = SLURM_SUCCESS;
		goto end;
	}

	job_info_msg = _pack_job_info(job_ptr);
	if (!job_info_msg || !job_info_msg->record_count) {
		error("switch/lua: %s: failed to pack job.", __func__);
		rc = SLURM_ERROR;
		goto end;
	}
	job_info = &job_info_msg->job_array[0];

	slurm_lua_push_job_info(job_info, L);

	if (lua_pcall(L, 1, 1, 0)) {
		error("switch/lua: %s(): %s: %s", lua_function,
		      lua_script_path, lua_tostring(L, -1));
		lua_pop(L, 1);
		rc = SLURM_ERROR;
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			error("switch/lua: %s: non-numeric return code",
			     lua_function);
			rc = SLURM_ERROR;
		}
		lua_pop(L, 1);
	}

end:
	if (job_info_msg)
		slurm_free_job_info_msg(job_info_msg);
	return rc;
}

extern uint32_t switch_p_job_channel(job_record_t *job_ptr, char *node_name)
{
	return NO_VAL;
}

extern int switch_p_fs_init(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern void switch_p_stepmgr_fini(uint32_t job_id) {}

extern void switch_p_stepmgr_init(void) {}
