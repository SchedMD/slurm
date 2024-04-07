/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
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

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/lua/slurm_lua.h"

static const char *lua_script_path = LUA_TEST_SCRIPT;

static void _loadscript_extra(lua_State *st)
{
	static const struct luaL_Reg slurm_functions[] = {
		{ NULL, NULL },
	};

	xassert(st);

	slurm_lua_table_register(st, NULL, slurm_functions);

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(st, "slurm");
}

extern buf_t *get_job_script(const job_record_t *job_ptr)
{
	/* required to avoid linker errors with slurm_lua.so */
	fatal_abort("this should not get called");
}

static lua_State *_load_lua_script(const char **req_fxns)
{
	time_t lua_script_last_loaded = (time_t) 0;
	lua_State *L = NULL;
	char *error_msg = NULL;

	ck_assert(!slurm_lua_loadscript(&L, "lua-test", lua_script_path,
					req_fxns, &lua_script_last_loaded,
					_loadscript_extra, &error_msg));
	ck_assert(!error_msg);
	return L;
}

static void _unload_lua_script(lua_State *L)
{
	debug3("%s: Unloading Lua script %s", __func__, lua_script_path);
	lua_close(L);
}

START_TEST(test_load_script)
{
	static const char *fxns[] = {
		"return_true",
		"return_false",
		"return_args",
		NULL
	};
	lua_State *L = _load_lua_script(fxns);

	slurm_lua_stack_dump("lua", "before lua_pcall", L);

	lua_getglobal(L, "return_true");
	ck_assert_int_eq(lua_type(L, -1), LUA_TFUNCTION);
	ck_assert_int_eq(lua_pcall(L, 0, 1, 0), 0);
	ck_assert_int_eq(lua_isboolean(L, -1), 1);
	ck_assert(lua_toboolean(L, -1));
	lua_pop(L, 1);

	lua_getglobal(L, "return_false");
	ck_assert_int_eq(lua_type(L, -1), LUA_TFUNCTION);
	ck_assert_int_eq(lua_pcall(L, 0, 1, 0), 0);
	ck_assert_int_eq(lua_isboolean(L, -1), 1);
	ck_assert(!lua_toboolean(L, -1));
	lua_pop(L, 1);

	lua_getglobal(L, "return_args");
	ck_assert_int_eq(lua_type(L, -1), LUA_TFUNCTION);
	lua_pushinteger(L, 12345);
	ck_assert_int_eq(lua_pcall(L, 1, 1, 0), 0);
	ck_assert_int_eq(lua_type(L, -1), LUA_TNUMBER);
	ck_assert_int_eq(lua_tonumber(L, -1), 12345);
	lua_pop(L, 1);

	slurm_lua_stack_dump("lua", "after lua_pcall", L);

	_unload_lua_script(L);
}
END_TEST

extern Suite *suite_lua(void)
{
	Suite *s = suite_create("lua");
	TCase *tc_core = tcase_create("lua");

	tcase_add_test(tc_core, test_load_script);

	suite_add_tcase(s, tc_core);
	return s;
}

extern int main(void)
{
	enum print_output po = CK_ENV;
	enum fork_status fs = CK_FORK_GETENV;
	SRunner *sr = NULL;
	int number_failed = 0;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("lua-test", log_opts, 0, NULL);

	if (log_opts.stderr_level >= LOG_LEVEL_DEBUG) {
		/* automatically be gdb friendly when debug logging */
		po = CK_VERBOSE;
		fs = CK_NOFORK;
	}

	sr = srunner_create(suite_lua());
	srunner_set_fork_status(sr, fs);
	srunner_run_all(sr, po);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	slurm_lua_fini();
	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
