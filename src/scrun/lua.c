/*****************************************************************************\
 *  lua.c - scrun lua handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "src/lua/slurm_lua.h"

#include "src/common/env.h"
#include "src/common/oci_config.h"
#include "src/common/run_command.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/scrun/scrun.h"

static char *lua_script_path = NULL;
static time_t lua_script_last_loaded = (time_t) 0;
static lua_State *L = NULL;
static const char *req_fxns[] = {
	"slurm_scrun_stage_in",
	"slurm_scrun_stage_out",
	NULL
};

static int _lua_override_bundle(lua_State *L)
{
	const char *path = lua_tostring(L, -1);

	write_lock_state();
	debug("%s: override bundle path: %s -> %s",
	      __func__, state.bundle, path);

	xfree(state.bundle);
	state.bundle = xstrdup(path);
	lua_pushstring(L, state.orig_bundle);
	unlock_state();

	return 1;
}

static int _lua_override_rootfs(lua_State *L)
{
	const char *path = lua_tostring(L, -1);

	write_lock_state();
	debug("%s: override rootfs path: %s -> %s",
	      __func__, state.root_path, path);

	xfree(state.root_path);
	state.root_path = xstrdup(path);
	lua_pushstring(L, state.orig_root_path);
	unlock_state();

	return 1;
}

static void _exec_add(data_t *data, const char *arg)
{
	data_set_string(data_list_append(data), arg);
}

static int _lua_allocator_command(lua_State *L)
{
	int status;
	run_command_args_t run = {
		.orphan_on_shutdown = false,
		.status = &status,
		.max_wait = -1,
		.script_type = "Lua-Command",
	};
	data_t *args;
	char *out;
	char **argvl;
	const char *cmd = lua_tostring(L, -1);

	debug("%s: request to run allocator command: %s", __func__, cmd);

	args = data_set_list(data_new());

	_exec_add(args, "/bin/sh");
	_exec_add(args, "-c");
	_exec_add(args, cmd);

	argvl = create_argv(args);
	FREE_NULL_DATA(args);

	run.script_path = argvl[0];
	run.script_argv = argvl;
	read_lock_state();
	run.env = env_array_copy((const char **) state.job_env);
	unlock_state();
	out = run_command(&run);

	if (get_log_level() >= LOG_LEVEL_DEBUG2)
		print_multi_line_string(out, -1, LOG_LEVEL_DEBUG2);

	/* same free pattern as environ */
	env_array_free(argvl);
	env_array_free(run.env);

	lua_pushnumber(L, WEXITSTATUS(status));
	lua_pushstring(L, out);

	xfree(out);
	return 2;
}

static int _lua_remote_command(lua_State *L)
{
	int status;
	run_command_args_t run = {
		.orphan_on_shutdown = false,
		.status = &status,
		.max_wait = -1,
		.script_type = "Lua-Command",
	};
	data_t *args;
	char *jobid, *out;
	char **argvl;
	const char *cmd = lua_tostring(L, -1);

	debug("%s: request to run remote command: %s", __func__, cmd);

	args = data_set_list(data_new());

	read_lock_state();
	run.env = env_array_copy((const char **) state.job_env);
	jobid = xstrdup_printf("%u", state.jobid);
	unlock_state();

	_exec_add(args, "/bin/sh");
	_exec_add(args, "-c");
	_exec_add(args, "exec \"$0\" \"$@\"");

	if (oci_conf->srun_path)
		_exec_add(args, oci_conf->srun_path);
	else /* let sh find srun from PATH */
		_exec_add(args, "srun");

	for (int i = 0; oci_conf->srun_args && oci_conf->srun_args[i]; i++)
		_exec_add(args, oci_conf->srun_args[i]);

	_exec_add(args, "--jobid");
	_exec_add(args, jobid);
	xfree(jobid);
	_exec_add(args, "--no-kill");
	_exec_add(args, "--job-name");
	_exec_add(args, "lua-command");
	_exec_add(args, "--");
	_exec_add(args, "/bin/sh");
	_exec_add(args, "-c");
	_exec_add(args, cmd);

	argvl = create_argv(args);
	FREE_NULL_DATA(args);

	run.script_path = argvl[0];
	run.script_argv = argvl;
	out = run_command(&run);

	if (get_log_level() >= LOG_LEVEL_DEBUG2)
		print_multi_line_string(out, -1, LOG_LEVEL_DEBUG2);

	/* same free pattern as environ */
	env_array_free(argvl);
	env_array_free(run.env);

	lua_pushnumber(L, WEXITSTATUS(status));
	lua_pushstring(L, out);

	xfree(out);
	return 2;
}

static void _loadscript_extra(lua_State *st)
{
	static const struct luaL_Reg slurm_functions[] = {
		{ "set_bundle_path", _lua_override_bundle },
		{ "set_root_path", _lua_override_rootfs },
		{ "remote_command", _lua_remote_command },
		{ "allocator_command", _lua_allocator_command },
		{ NULL, NULL }
	};

	xassert(st);

	slurm_lua_table_register(st, NULL, slurm_functions);
	luaL_loadstring(st,
			"slurm.user_msg (string.format(table.unpack({...})))");
	lua_setfield(st, -2, "log_user");

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(st, "slurm");
}

extern void init_lua(void)
{
	int rc = SLURM_SUCCESS;

	if ((rc = slurm_lua_init()))
		fatal("%s: unable to load lua: %s",
		      __func__, slurm_strerror(rc));

	lua_script_path = get_extra_conf_path("scrun.lua");

	if ((rc = slurm_lua_loadscript(&L, "scrun", lua_script_path, req_fxns,
				       &lua_script_last_loaded,
				       _loadscript_extra)))
		fatal("%s: unable to load lua script %s: %s",
		      __func__, lua_script_path, slurm_strerror(rc));
}

extern void destroy_lua(void)
{
	if (L) {
		debug3("%s: Unloading Lua script %s",
		       __func__, lua_script_path);
		lua_close(L);
		L = NULL;
		lua_script_last_loaded = 0;
	}

	xfree(lua_script_path);

	slurm_lua_fini();
}

extern int stage_in(void)
{
	int rc = SLURM_ERROR;

	debug5("scrun container lua stage_in()");

	check_state();

	write_lock_state();
	xassert(!state.staged_in);
	state.staged_in = true;
	unlock_state();

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_scrun_stage_in");
	if (lua_isnil(L, -1)) {
		debug("%s: slurm_scrun_stage_in() missing", __func__);
		return SLURM_SUCCESS;
	}

	read_lock_state();
	lua_pushstring(L, state.id);
	lua_pushstring(L, state.bundle);
	lua_pushstring(L, state.spool_dir);
	lua_pushstring(L, state.config_file);
	lua_pushnumber(L, state.jobid);
	lua_pushnumber(L, state.user_id);
	lua_pushnumber(L, state.group_id);
	lua_createtable(L, 0, envcount(state.job_env));
	for (int i = 0; state.job_env[i]; i++) {
		lua_pushnumber(L, i);
		lua_pushstring(L, state.job_env[i]);
		lua_settable(L, -3);
	}
	unlock_state();

	slurm_lua_stack_dump(
		"scrun/lua", "slurm_scrun_stage_in, before lua_pcall", L);
	if (lua_pcall(L, 8, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code",
			     __func__, lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"scrun/lua", "slurm_scrun_stage_in, after lua_pcall", L);

	return rc;
}

extern int stage_out(void)
{
	int rc = SLURM_ERROR;

	debug5("scrun container lua stage_out()");

#ifndef NDEBUG
	check_state();
	read_lock_state();
	xassert(state.staged_in);
	unlock_state();
#endif

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_scrun_stage_out");
	if (lua_isnil(L, -1)) {
		debug("%s: slurm_scrun_stage_out() missing", __func__);
		return SLURM_SUCCESS;
	}

	read_lock_state();
	lua_pushstring(L, state.id);
	lua_pushstring(L, state.bundle);
	lua_pushstring(L, state.orig_bundle);
	lua_pushstring(L, state.root_path);
	lua_pushstring(L, state.orig_root_path);
	lua_pushstring(L, state.spool_dir);
	lua_pushstring(L, state.config_file);
	lua_pushnumber(L, state.jobid);
	lua_pushnumber(L, state.user_id);
	lua_pushnumber(L, state.group_id);
	unlock_state();

	slurm_lua_stack_dump(
		"scrun/lua", "slurm_scrun_stage_out, before lua_pcall", L);
	if (lua_pcall(L, 10, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code",
			     __func__, lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"scrun/lua", "slurm_scrun_stage_out, after lua_pcall", L);

	return rc;
}

extern buf_t *get_job_script(const job_record_t *job_ptr)
{
	/* required to avoid linker errors with slurm_lua.so */
	fatal_abort("this should not get called");
}
