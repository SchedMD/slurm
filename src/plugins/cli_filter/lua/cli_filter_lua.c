/*****************************************************************************\
 *  cli_filter_lua.c - lua CLI option processing specifications.
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/interfaces/cli_filter.h"
#include "src/common/data.h"
#include "src/common/slurm_opt.h"
#include "src/common/spank.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"
#include "src/lua/slurm_lua.h"
#include "src/plugins/cli_filter/common/cli_filter_common.h"

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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "cli filter defaults plugin";
const char plugin_type[]       	= "cli_filter/lua";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;
static char *lua_script_path;
static lua_State *L = NULL;
static char **stored_data = NULL;
static size_t stored_n = 0;
static size_t stored_sz = 0;
static time_t lua_script_last_loaded = 0;

static const char *req_fxns[] = {
	"slurm_cli_setup_defaults",
	"slurm_cli_pre_submit",
	"slurm_cli_post_submit",
	NULL
};

static int _lua_cli_json(lua_State *st);
static int _lua_cli_json_env(lua_State *st);
static int _store_data(lua_State *st);
static int _retrieve_data(lua_State *st);
static void _loadscript_extra(lua_State *st);

static const struct luaL_Reg slurm_functions[] = {
	{ "json_cli_options",_lua_cli_json },
	{ "json_env",	_lua_cli_json_env },
	{ "cli_store",	_store_data },
	{ "cli_retrieve", _retrieve_data },
	{ NULL,		NULL        }
};

/*
 *  NOTE: The init callback should never be called multiple times,
 *   let alone called from multiple threads. Therefore, locking
 *   is unnecessary here.
 */
int init(void)
{
        int rc = SLURM_SUCCESS;

        if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
                return rc;

	if ((rc = data_init())) {
		error("%s: unable to init data structures: %s", __func__,
		      slurm_strerror(rc));
		return rc;
	}

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("%s: unable to load JSON serializer: %s", __func__,
		      slurm_strerror(rc));
		return rc;
	}

	stored_data = xmalloc(sizeof(char *) * 24);
	stored_sz = 24;
	lua_script_path = get_extra_conf_path("cli_filter.lua");

	return slurm_lua_loadscript(&L, "cli_filter/lua",
				    lua_script_path, req_fxns,
				    &lua_script_last_loaded, _loadscript_extra);
}

int fini(void)
{
	for (int i = 0; i < stored_n; i++)
		xfree(stored_data[i]);
	xfree(stored_data);
	xfree(lua_script_path);

        lua_close(L);

	slurm_lua_fini();

        return SLURM_SUCCESS;
}

static int _setup_stringarray(lua_State *st, int limit, char **data) {

	/*
	 * if limit/data empty this will create an empty array intentionally to
         * allow the client code to iterate over it
	 */
	lua_newtable(st);
	for (int i = 0; i < limit && data && data[i]; i++) {
		/* lua indexes tables from 1 */
		lua_pushnumber(st, i + 1);
		lua_pushstring(st, data[i]);
		lua_settable(st, -3);
	}
	return 1;
}

static int _setup_option_field_argv(lua_State *st, slurm_opt_t *opt)
{
	char **argv = NULL;
	int  argc = 0;

	argv = opt->argv;
	argc = opt->argc;

	return _setup_stringarray(st, argc, argv);
}

static int _setup_option_field_spank(lua_State *st)
{
	char **plugins = NULL;
	size_t n_plugins = 0;

	n_plugins = spank_get_plugin_names(&plugins);

	lua_newtable(st); /* table to push */
	for (size_t i = 0; i < n_plugins; i++) {
		char **opts = NULL;
		size_t n_opts = 0;
		n_opts = spank_get_plugin_option_names(plugins[i], &opts);

		lua_newtable(st);
		for (size_t j = 0; j < n_opts; j++) {
			char *value = spank_option_get(opts[j]);
			if (value)
				if (strlen(value) == 0)
					lua_pushstring(st, "set");
				else
					lua_pushstring(st, value);
			else
				lua_pushnil(st);
			lua_setfield(st, -2, opts[j]);
			xfree(opts[j]);
		}
		lua_setfield(st, -2, plugins[i]);

		xfree(opts);
		xfree(plugins[i]);
	}
	xfree(plugins);
	return 1;
}

static int _get_option_field_index(lua_State *st)
{
	const char *name;
	slurm_opt_t *options = NULL;
	char *value = NULL;

	name = luaL_checkstring(st, -1);
	lua_getmetatable(st, -2);
	lua_getfield(st, -1, "_opt");
	options = (slurm_opt_t *) lua_touserdata(st, -1);

	/* getmetatable and getfield pushed two items onto the stack above
	 * get rid of them here, and leave the name string at the top of
	 * the stack */
	lua_settop(st, -3);

	if (!strcmp(name, "argv"))
		return _setup_option_field_argv(st, options);
	else if (!strcmp(name, "spank"))
		return _setup_option_field_spank(st);
	else if (!strcmp(name, "spank_job_env"))
		return _setup_stringarray(st, options->spank_job_env_size,
					  options->spank_job_env);
	else if (!strcmp(name, "type")) {
		if (options->salloc_opt)
			lua_pushstring(st, "salloc");
		else if (options->sbatch_opt)
			lua_pushstring(st, "sbatch");
		else if (options->scron_opt)
			lua_pushstring(st, "scrontab");
		else if (options->srun_opt)
			lua_pushstring(st, "srun");
		else
			lua_pushstring(st, "other");
		return 1;
	}

	value = slurm_option_get(options, name);
	if (!value)
		lua_pushnil(st);
	else
		lua_pushstring(st, value);
	xfree(value);
	return 1;
}

static int _set_option_field(lua_State *st)
{
	slurm_opt_t *options = NULL;
	const char *name = NULL;
	const char *value = NULL;
	bool early = false;

	slurm_lua_stack_dump("cli_filter/lua", "early _set_option_field", st);
	name = luaL_checkstring(st, -2);
	value = luaL_checkstring(st, -1);
	lua_getmetatable(st, -3);
	lua_getfield(st, -1, "_opt");
	options = (slurm_opt_t *) lua_touserdata(st, -1);
	lua_getfield(st, -2, "_early");
	early = lua_toboolean(st, -1);

	/* getmetatable and getfields pushed three items onto the stack above
	 * get rid of them here, and leave the name string at the top of
	 * the stack */
	lua_settop(st, -4);

	if (slurm_option_set(options, name, value, early))
		return 1;
	return 0;
}

static void _push_options(slurm_opt_t *opt, bool early)
{
	lua_newtable(L); /* table to push */

	lua_newtable(L); /* metatable */
	lua_pushcfunction(L, _get_option_field_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, _set_option_field);
	lua_setfield(L, -2, "__newindex");
	/* Store opt in the metatable, so the index
	 * function knows which struct it's getting data for.
	 */

	lua_pushlightuserdata(L, opt);
	lua_setfield(L, -2, "_opt");
	lua_pushboolean(L, early);
	lua_setfield(L, -2, "_early");

	lua_setmetatable(L, -2);

}

static int _lua_cli_json(lua_State *st)
{
	char *json = NULL;
	slurm_opt_t *options = NULL;

	if (!lua_getmetatable(st, -1)) {
		error("json_cli_options requires one argument - options structure");
		return 0;
	}

	lua_getfield(st, -1, "_opt");
	options = (slurm_opt_t *) lua_touserdata(st, -1);
	lua_settop(st, -3);

	json = cli_filter_json_set_options(options);
	if (json)
		lua_pushstring(st, json);
	else
		lua_pushnil(st);
	xfree(json);
	return 1;
}

static int _lua_cli_json_env(lua_State *st)
{
	char *output = cli_filter_json_env();
	lua_pushstring(st, output);
	xfree(output);
	return 1;
}

static int _store_data(lua_State *st)
{
	int key = 0;
	const char *data = NULL;

	key = (int) lua_tonumber(st, -2);
	data = luaL_checkstring(st, -1);

	if (key >= stored_sz) {
		stored_data = xrealloc(stored_data,
				       (key + 24) * sizeof(char *));
		stored_sz = key + 24;
	}
	if (key > stored_n)
		stored_n = key;
	stored_data[key] = xstrdup(data);
	return 0;
}

static int _retrieve_data(lua_State *st)
{
	int key = (int) lua_tonumber(st, -1);
	if (key <= stored_n && stored_data[key])
		lua_pushstring(st, stored_data[key]);
	else
		lua_pushnil(st);
	return 1;
}

static void _loadscript_extra(lua_State *st)
{
        /* local setup */
	slurm_lua_table_register(st, NULL, slurm_functions);

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(st, "slurm");
}

extern int cli_filter_p_setup_defaults(slurm_opt_t *opt, bool early)
{
	int rc;

	rc = slurm_lua_loadscript(&L, "cli_filter/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		goto out;

	lua_getglobal(L, "slurm_cli_setup_defaults");
	if (lua_isnil(L, -1))
		goto out;
	_push_options(opt, early);
	slurm_lua_stack_dump("cli_filter/lua",
			     "setup_defaults, before lua_pcall", L);
	if (lua_pcall(L, 1, 1, 0) != 0) {
		error("%s/lua: %s: %s", __func__, lua_script_path,
		      lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code", __func__,
			     lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"cli_filter/lua", "setup_defaults, after lua_pcall", L);

out:
	return rc;
}

extern int cli_filter_p_pre_submit(slurm_opt_t *opt, int offset)
{
	int rc;

	rc = slurm_lua_loadscript(&L, "cli_filter/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		goto out;
	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_cli_pre_submit");
	if (lua_isnil(L, -1))
		goto out;

	_push_options(opt, false);
	lua_pushnumber(L, (double) offset);

	slurm_lua_stack_dump(
		"cli_filter/lua", "pre_submit, before lua_pcall", L);
	if (lua_pcall(L, 2, 1, 0) != 0) {
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
		"cli_filter/lua", "pre_submit, after lua_pcall", L);

out:
	return rc;
}

extern void cli_filter_p_post_submit(
	int offset, uint32_t jobid, uint32_t stepid)
{
	int rc;

	rc = slurm_lua_loadscript(&L, "cli_filter/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		goto out;

	lua_getglobal(L, "slurm_cli_post_submit");
	if (lua_isnil(L, -1))
		goto out;
	lua_pushnumber(L, (double) offset);
	lua_pushnumber(L, (double) jobid);
	lua_pushnumber(L, (double) stepid);
	slurm_lua_stack_dump(
		"cli_filter/lua", "post_submit, before lua_pcall", L);
	if (lua_pcall(L, 3, 1, 0) != 0) {
		error("%s/lua: %s: %s", __func__, lua_script_path,
		      lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			(void)lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code", __func__,
			     lua_script_path);
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"cli_filter/lua", "post_submit, after lua_pcall", L);

out:
	return;
}
