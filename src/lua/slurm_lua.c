/*****************************************************************************\
 *  slurm_lua.c - Lua integration common functions
 *****************************************************************************
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

#include <dlfcn.h>
#include <stdio.h>
#include "config.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/lua/slurm_lua.h"

#include "src/interfaces/serializer.h"

/* Max table depth for data_t conversions */
#define MAX_DEPTH 16
#define DUMP_DATA_FOREACH_ARGS_MAGIC 0x02141444

typedef struct {
	int magic; /* DUMP_DATA_FOREACH_ARGS_MAGIC */
	lua_State *L;
	int rc;
	int table_index;
	int field_index;
} dump_data_foreach_args_t;

static void *lua_handle = NULL;

#ifdef HAVE_LUA

#if LUA_VERSION_NUM >= 502
#define LUA_ERROR_BACKTRACE
#endif

/*
 * These are defined here so when we link with something other than the
 * slurmctld we will have these symbols defined. They will get overwritten when
 * linking with the slurmctld.
 */
#if defined (__APPLE__)
extern uint16_t accounting_enforce __attribute__((weak_import));
extern void *acct_db_conn  __attribute__((weak_import));
#else
uint16_t accounting_enforce = 0;
void *acct_db_conn = NULL;
#endif

#define LUA_ERROR_HANDLER_FUNC "slurm_backtrace_on_error"

#define T(status_code, string, err) { status_code, #status_code, string, err }

static const struct {
	lua_status_code_t status_code;
	const char *status_code_string;
	const char *string;
	slurm_err_t err;
} lua_status_codes[] = {
	/*
	 * Status codes macros from lua.h and messages derived from:
	 *	https://www.lua.org/manual/5.3/manual.html
	 */
	T(LUA_OK, "SUCCESS", SLURM_SUCCESS),
	T(LUA_YIELD, "Thread yielded", ESLURM_LUA_FUNC_FAILED),
	T(LUA_ERRRUN, "Runtime error", ESLURM_LUA_FUNC_FAILED_RUNTIME_ERROR),
	T(LUA_ERRSYNTAX, "Syntax error during precompilation",
	  ESLURM_LUA_INVALID_SYNTAX),
	T(LUA_ERRMEM, "Memory allocation error", ESLURM_LUA_FUNC_FAILED_ENOMEM),
#ifdef LUA_ERRGCMM
	T(LUA_ERRGCMM, "Error while running a __gc metamethod",
	  ESLURM_LUA_FUNC_FAILED_GARBAGE_COLLECTOR),
#endif
	T(LUA_ERRERR, "Error while running the message handler",
	  ESLURM_LUA_FUNC_FAILED_RUNTIME_ERROR),
};
#undef T

static int _lua_to_data(lua_State *L, data_t *dst, const int index,
			const int depth, const char *parent,
			const bool parent_is_table);
static int _from_data(lua_State *L, const data_t *src);

extern const char *slurm_lua_status_code_string(lua_status_code_t sc)
{
	for (int i = 0; i < ARRAY_SIZE(lua_status_codes); i++)
		if (lua_status_codes[i].status_code == sc)
			return lua_status_codes[i].string;

	/*
	 * Should never happen but only Lua controls returns these values so it
	 * is out of Slurm's control.
	 */
	return "Unknown Lua status code";
}

extern const char *slurm_lua_status_code_stringify(lua_status_code_t sc)
{
	for (int i = 0; i < ARRAY_SIZE(lua_status_codes); i++)
		if (lua_status_codes[i].status_code == sc)
			return lua_status_codes[i].status_code_string;

	return "INVALID";
}

extern slurm_err_t slurm_lua_status_error(lua_status_code_t sc)
{
	for (int i = 0; i < ARRAY_SIZE(lua_status_codes); i++)
		if (lua_status_codes[i].status_code == sc)
			return lua_status_codes[i].err;

	return ESLURM_LUA_FUNC_FAILED;
}

#ifdef LUA_ERROR_BACKTRACE

/*
 * Error callback handler function.
 * Partially based on msghandler() from lua.c (from the lua binary).
 */
static int _on_error_callback(lua_State *L)
{
	const char *msg = NULL;

	/*
	 * Only log the backtrace when running at least under DEBUG_FLAG_SCRIPT
	 * as this is not a free operation and may end up logging excessively.
	 */
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT))
		return 1;

	/* Lua should have already have already placed error string on stack */
	if (!(msg = lua_tostring(L, -1))) {
		/*
		 * Request Lua convert error to string if lua_tostring() failed
		 * for any reason or gracefully fail while getting the type of
		 * what was unexpectedly pushed on the stack
		 */
		if (luaL_callmeta(L, -1, "__tostring") &&
		    lua_type(L, -1) == LUA_TSTRING)
			msg = lua_tostring(L, -1);
		else
			msg = lua_pushfstring(L, "Unknown error of type %s",
					      luaL_typename(L, -1));
	}

	/* msg should always have a string at this point */
	xassert(msg && msg[0]);

	log_flag(SCRIPT, "%s: Lua@0x%"PRIxPTR" failed: %s",
		 __func__, (uintptr_t) L, msg);

	/* Request Lua generate backtrace */
	luaL_traceback(L, L, NULL, 1);

	if ((msg = lua_tostring(L, -1))) {
		char *save_ptr = NULL, *token = NULL, *str = NULL;
		int count = 0, line = 0;

		xassert(msg && msg[0]);

		/* count number of lines */
		for (const char *ptr = msg; ptr && *ptr; count++)
			if ((ptr = strstr(ptr, "\n")))
				ptr++;

		/*
		 * Split up the backtrace by each newline to keep the logs
		 * readable
		 */
		str = xstrdup(msg);
		token = strtok_r(str, "\n", &save_ptr);
		while (token) {
			token = strtok_r(NULL, "\n", &save_ptr);
			line++;

			log_flag(SCRIPT, "%s: Lua@0x%"PRIxPTR" backtrace[%04d/%04d]: %s",
				 __func__, (uintptr_t) L, line, count, token);
		}
		xfree(str);
	}

	/*
	 * Pop backtrace off stack to preserve returning original error message
	 * for existing logging
	 */
	lua_pop(L, -1);

	/* returning original error message */
	return 1;
}

/*
 * Push error callback before pcall args
 * IN L - Lua state
 * RET index for function (aka msgh)
 */
static int _push_error_callback(lua_State *L)
{
	const int index = 1;

	/*
	 * Always place error handler function at the very bottom of the stack
	 * to avoid it getting moved around by any of the arg or return stack
	 * shifting
	 *
	 * The error handler doesn't work when registered via slurm_functions[]
	 * due to the requirement to be placed directly on the stack.
	 */

	lua_getglobal(L, LUA_ERROR_HANDLER_FUNC);

	/* There must be something on the stack here */
	xassert(lua_gettop(L) > 0);

	lua_insert(L, index);

	return index;
}

/*
 * Register error handler callback into Lua globals
 * IN L - Lua stack
 */
static void _register_error_callback(lua_State *L)
{
	lua_register(L, LUA_ERROR_HANDLER_FUNC, _on_error_callback);
}

#else

#define _push_error_callback(L) 0
#define _register_error_callback(L) ((void) 0)

#endif

static int _setup_stringarray(lua_State *L, int limit, char **data)
{
	/*
	 * if limit/data empty this will create an empty array intentionally to
	 * allow the client code to iterate over it
	 */
	lua_newtable(L);
	for (int i = 0; i < limit && data && data[i]; i++) {
		/* by convention lua indexes array tables from 1 */
		lua_pushnumber(L, i + 1);
		lua_pushstring(L, data[i]);
		lua_settable(L, -3);
	}
	return 1;
}

extern int slurm_lua_pcall(lua_State *L, int nargs, int nresults,
			   char **err_ptr, const char *caller)
{
	lua_status_code_t sc;
	int rc;
	int msgh = 0;

	if (slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT) {
		/* Resolve out the error handler if it was already pushed */
		msgh = _push_error_callback(L);
	}

	sc = lua_pcall(L, nargs, nresults, msgh);
	rc = slurm_lua_status_error(sc);

	if (rc) {
		/*
		 * When a lua_pcall() fails, Lua "pushes a single value on the
		 * stack (the error object)" per lua_pcall() description in the
		 * reference manual:
		 *	https://www.lua.org/manual/5.3/manual.html
		 * When msgh == 0, this is the same as the return value of
		 * lua_pcall().
		 *
		 * This function will lua_pop() that value to remove it from
		 * the stack.
		 */
		if (!(*err_ptr = xstrdup(lua_tostring(L, -1))))
			*err_ptr = xstrdup(slurm_strerror(rc));

		lua_pop(L, 1);

		error("%s: lua_pcall(0x%"PRIxPTR", %d, %d, %d)=%s(%s)=%s",
		      caller, (uintptr_t) L, nargs, nresults, msgh,
		      slurm_lua_status_code_stringify(sc),
		      slurm_lua_status_code_string(sc), *err_ptr);
	} else {
		log_flag(SCRIPT, "%s: lua_pcall(0x%"PRIxPTR", %d, %d, %d)=%s(%s)=%s",
		      caller, (uintptr_t) L, nargs, nresults, msgh,
		      slurm_lua_status_code_stringify(sc),
		      slurm_lua_status_code_string(sc), slurm_strerror(rc));
	}

	/* Pop error handler off stack if pushed */
	if (msgh)
		lua_remove(L, msgh);

	return rc;
}

extern bool slurm_lua_is_function_defined(lua_State *L, const char *name)
{
	bool rc = false;

	lua_getglobal(L, name);
	rc = lua_isfunction(L, -1);
	lua_pop(L, -1);

	return rc;
}

/*
 *   Verify all required functions are defined in the script
 */
static int _check_lua_script_functions(lua_State *L, const char *plugin,
				       const char *script_path,
				       const char **req_fxns)
{
	int rc = 0;
	const char **ptr = NULL;
	for (ptr = req_fxns; ptr && *ptr; ptr++) {
		if (!slurm_lua_is_function_defined(L, *ptr)) {
			error("%s: %s: missing required function %s",
			      plugin, script_path, *ptr);
			rc = -1;
		}
	}

	return (rc);
}

/*
 *  Lua interface to Slurm log facility:
 */
static int _log_lua_msg (lua_State *L)
{
	const char *prefix  = "lua";
	int        level    = 0;
	const char *msg;

	/*
	 *  Optional numeric prefix indicating the log level
	 *  of the message.
	 */

	/* Pop message off the lua stack */
	msg = lua_tostring(L, -1);
	lua_pop(L, 1);

	/* Pop level off stack: */
	level = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);

	/* Call appropriate slurm log function based on log-level argument */
	if (level > 4)
		debug4 ("%s: %s", prefix, msg);
	else if (level == 4)
		debug3 ("%s: %s", prefix, msg);
	else if (level == 3)
		debug2 ("%s: %s", prefix, msg);
	else if (level == 2)
		debug ("%s: %s", prefix, msg);
	else if (level == 1)
		verbose ("%s: %s", prefix, msg);
	else if (level == 0)
		info ("%s: %s", prefix, msg);
	return (0);
}

static int _log_lua_error(lua_State *L)
{
	const char *prefix  = "lua";
	const char *msg     = lua_tostring(L, -1);
	error ("%s: %s", prefix, msg);
	return (0);
}

static int _time_str2mins(lua_State *L) {
	const char *time = lua_tostring(L, -1);
	int minutes = time_str2mins(time);
	lua_pushnumber(L, minutes);
	return 1;
}

static int _get_qos_priority(lua_State *L)
{
	const char *qos_name = lua_tostring(L, -1);
	slurmdb_qos_rec_t qos = { 0 };

	qos.name = xstrdup(qos_name);
	if (assoc_mgr_fill_in_qos(acct_db_conn, &qos, accounting_enforce, NULL,
				  false)) {
		error("Invalid QOS name: %s", qos.name);
		xfree(qos.name);
		return 0;
	}
	xfree(qos.name);

	lua_pushnumber(L, qos.priority);
	return 1;
}

static int _parse(lua_State *L, const char *mime_type)
{
	int rc = EINVAL;
	const char *str = NULL;
	size_t str_len = 0;
	data_t *data = NULL;

	if (!(str = lua_tolstring(L, -1, &str_len))) {
		rc = ESLURM_LUA_INVALID_CONVERSION_TYPE;
		goto failed;
	}

	if ((rc = serialize_g_string_to_data(&data, str, str_len, mime_type)))
		goto failed;

	log_flag_hex(SCRIPT, str, str_len,
		     "%s: Lua@0x%" PRIxPTR "[+%d]: parsed %s",
		     __func__, (uintptr_t) L, lua_gettop(L), mime_type);

	/* Pop string arg off stack after converting it */
	lua_pop(L, -1);

	if ((rc = slurm_lua_from_data(L, data)))
		goto failed;

	FREE_NULL_DATA(data);
	return 1;
failed:
	error("%s: Lua@0x%" PRIxPTR "[+%d]: parsing string as %s failed: %s",
	      __func__, (uintptr_t) L, lua_gettop(L), mime_type,
	      slurm_strerror(rc));

	if (str_len > 0)
		log_flag_hex(SCRIPT, str, str_len, "%s: parsing %s failed",
			     __func__, mime_type);

	FREE_NULL_DATA(data);

	(void) lua_pushfstring(L, "Conversion from %s failed: %s", mime_type,
			       slurm_strerror(rc));
	lua_error(L);
	fatal_abort("lua_error() should never return");
}

static int _dump(lua_State *L, const char *mime_type)
{
	int rc = EINVAL;
	char *str = NULL;
	size_t str_len = 0;
	data_t *data = data_new();

	if ((rc = slurm_lua_to_data(L, data)))
		goto failed;

	/* Pop table arg off stack after converting it */
	lua_pop(L, -1);

	if ((rc = serialize_g_data_to_string(&str, &str_len, data, mime_type,
					     SER_FLAGS_NONE)))
		goto failed;

	log_flag_hex(SCRIPT, str, str_len,
		     "%s: Lua@0x%" PRIxPTR "[+%d]: dumped %pD->%s",
		     __func__, (uintptr_t) L, lua_gettop(L), data, mime_type);

	FREE_NULL_DATA(data);

	(void) lua_pushstring(L, str);

	xfree(str);
	return 1;
failed:
	error("%s: Lua@0x%" PRIxPTR "[+%d]: dumping %pD as %s failed: %s",
	      __func__, (uintptr_t) L, lua_gettop(L), data, mime_type,
	      slurm_strerror(rc));

	FREE_NULL_DATA(data);
	xfree(str);

	(void) lua_pushfstring(L, "Conversion to %s failed: %s", mime_type,
			       slurm_strerror(rc));
	lua_error(L);
	fatal_abort("lua_error() should never return");
}

static int _from_json(lua_State *L)
{
	static bool load_once = false;

	if (!load_once) {
		serializer_required(MIME_TYPE_JSON);
		load_once = true;
	}

	return _parse(L, MIME_TYPE_JSON);
}

static int _to_json(lua_State *L)
{
	static bool load_once = false;

	if (!load_once) {
		serializer_required(MIME_TYPE_JSON);
		load_once = true;
	}

	return _dump(L, MIME_TYPE_JSON);
}

static int _from_yaml(lua_State *L)
{
	static bool load_once = false;

	if (!load_once) {
		serializer_required(MIME_TYPE_YAML);
		load_once = true;
	}

	return _parse(L, MIME_TYPE_YAML);
}

static int _to_yaml(lua_State *L)
{
	static bool load_once = false;

	if (!load_once) {
		serializer_required(MIME_TYPE_YAML);
		load_once = true;
	}

	return _dump(L, MIME_TYPE_YAML);
}

static const struct luaL_Reg slurm_functions[] = {
	{ "log", _log_lua_msg },
	{ "error", _log_lua_error },
	{ "time_str2mins", _time_str2mins },
	{ "get_qos_priority", _get_qos_priority },
	{ "to_json", _to_json },
	{ "from_json", _from_json },
	{ "to_yaml", _to_yaml },
	{ "from_yaml", _from_yaml },
	{ NULL, NULL }
};

static void _register_slurm_output_errtab(lua_State *L)
{
	int i;

	for (i = 0; i < slurm_errtab_size; i++) {
		lua_pushnumber(L, slurm_errtab[i].xe_number);
		lua_setfield(L, -2, slurm_errtab[i].xe_name);
	}
}

static void _register_slurm_output_functions(lua_State *L)
{
	char *unpack_str;
	char tmp_string[100];

#if LUA_VERSION_NUM == 501
	unpack_str = "unpack";
#else
	unpack_str = "table.unpack";
#endif
	/*
	 *  Register slurm output functions in a global "slurm" table
	 */
	lua_newtable(L);
	slurm_lua_table_register(L, NULL, slurm_functions);

	/*
	 *  Create more user-friendly lua versions of Slurm log functions.
	 */
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.error (string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_error");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (0, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_info");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (1, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_verbose");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (2, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_debug");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (3, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_debug2");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (4, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_debug3");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.log (5, string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_debug4");
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.user_msg (string.format(%s({...})))", unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_user");

	/*
	 * Error codes: slurm.SUCCESS, slurm.FAILURE, slurm.ERROR, etc.
	 */
	lua_pushnumber(L, SLURM_ERROR);
	lua_setfield(L, -2, "ERROR");
	lua_pushnumber(L, SLURM_ERROR);
	lua_setfield(L, -2, "FAILURE");
	lua_pushnumber(L, SLURM_SUCCESS);
	lua_setfield(L, -2, "SUCCESS");
	_register_slurm_output_errtab(L);

	/*
	 * Other definitions needed to interpret data
	 * slurm.MEM_PER_CPU, slurm.NO_VAL, etc.
	 */
	lua_pushnumber(L, ALLOC_SID_ADMIN_HOLD);
	lua_setfield(L, -2, "ALLOC_SID_ADMIN_HOLD");
	lua_pushnumber(L, ALLOC_SID_USER_HOLD);
	lua_setfield(L, -2, "ALLOC_SID_USER_HOLD");
	lua_pushnumber(L, INFINITE);
	lua_setfield(L, -2, "INFINITE");
	lua_pushnumber(L, (double) INFINITE64);
	lua_setfield(L, -2, "INFINITE64");
	lua_pushnumber(L, MAIL_INVALID_DEPEND);
	lua_setfield(L, -2, "MAIL_INVALID_DEPEND");
	lua_pushnumber(L, MAIL_JOB_BEGIN);
	lua_setfield(L, -2, "MAIL_JOB_BEGIN");
	lua_pushnumber(L, MAIL_JOB_END);
	lua_setfield(L, -2, "MAIL_JOB_END");
	lua_pushnumber(L, MAIL_JOB_FAIL);
	lua_setfield(L, -2, "MAIL_JOB_FAIL");
	lua_pushnumber(L, MAIL_JOB_REQUEUE);
	lua_setfield(L, -2, "MAIL_JOB_REQUEUE");
	lua_pushnumber(L, MAIL_JOB_TIME100);
	lua_setfield(L, -2, "MAIL_JOB_TIME100");
	lua_pushnumber(L, MAIL_JOB_TIME90);
	lua_setfield(L, -2, "MAIL_JOB_TIME890");
	lua_pushnumber(L, MAIL_JOB_TIME80);
	lua_setfield(L, -2, "MAIL_JOB_TIME80");
	lua_pushnumber(L, MAIL_JOB_TIME50);
	lua_setfield(L, -2, "MAIL_JOB_TIME50");
	lua_pushnumber(L, MAIL_JOB_STAGE_OUT);
	lua_setfield(L, -2, "MAIL_JOB_STAGE_OUT");
	lua_pushnumber(L, MEM_PER_CPU);
	lua_setfield(L, -2, "MEM_PER_CPU");
	lua_pushnumber(L, NICE_OFFSET);
	lua_setfield(L, -2, "NICE_OFFSET");
	lua_pushnumber(L, JOB_SHARED_NONE);
	lua_setfield(L, -2, "JOB_SHARED_NONE");
	lua_pushnumber(L, JOB_SHARED_OK);
	lua_setfield(L, -2, "JOB_SHARED_OK");
	lua_pushnumber(L, JOB_SHARED_USER);
	lua_setfield(L, -2, "JOB_SHARED_USER");
	lua_pushnumber(L, JOB_SHARED_MCS);
	lua_setfield(L, -2, "JOB_SHARED_MCS");
	lua_pushnumber(L, (double) NO_VAL64);
	lua_setfield(L, -2, "NO_VAL64");
	lua_pushnumber(L, NO_VAL);
	lua_setfield(L, -2, "NO_VAL");
	lua_pushnumber(L, NO_VAL16);
	lua_setfield(L, -2, "NO_VAL16");
	lua_pushnumber(L, NO_VAL8);
	lua_setfield(L, -2, "NO_VAL8");
	lua_pushnumber(L, SHARED_FORCE);
	lua_setfield(L, -2, "SHARED_FORCE");

	/*
	 * job_desc bitflags
	 */
	lua_pushnumber(L, GRES_ALLOW_TASK_SHARING);
	lua_setfield(L, -2, "GRES_ALLOW_TASK_SHARING");
	lua_pushnumber(L, GRES_DISABLE_BIND);
	lua_setfield(L, -2, "GRES_DISABLE_BIND");
	lua_pushnumber(L, GRES_ENFORCE_BIND);
	lua_setfield(L, -2, "GRES_ENFORCE_BIND");
	lua_pushnumber(L, GRES_MULT_TASKS_PER_SHARING);
	lua_setfield(L, -2, "GRES_MULT_TASKS_PER_SHARING");
	lua_pushnumber(L, GRES_ONE_TASK_PER_SHARING);
	lua_setfield(L, -2, "GRES_ONE_TASK_PER_SHARING");
	lua_pushnumber(L, KILL_INV_DEP);
	lua_setfield(L, -2, "KILL_INV_DEP");
	lua_pushnumber(L, NO_KILL_INV_DEP);
	lua_setfield(L, -2, "NO_KILL_INV_DEP");
	lua_pushnumber(L, SPREAD_JOB);
	lua_setfield(L, -2, "SPREAD_JOB");
	lua_pushnumber(L, USE_MIN_NODES);
	lua_setfield(L, -2, "USE_MIN_NODES");
	lua_pushnumber(L, STEPMGR_ENABLED);
	lua_setfield(L, -2, "STEPMGR_ENABLED");
	lua_pushnumber(L, SPREAD_SEGMENTS);
	lua_setfield(L, -2, "SPREAD_SEGMENTS");
	lua_pushnumber(L, CONSOLIDATE_SEGMENTS);
	lua_setfield(L, -2, "CONSOLIDATE_SEGMENTS");
	lua_pushnumber(L, EXPEDITED_REQUEUE);
	lua_setfield(L, -2, "EXPEDITED_REQUEUE");

	lua_pushstring(L, slurm_conf.cluster_name);
	lua_setfield(L, -2, "CLUSTER_NAME");
}

extern void slurm_lua_table_register(lua_State *L, const char *libname,
				     const luaL_Reg *l)
{
#if LUA_VERSION_NUM == 501
	luaL_register(L, libname, l);
#else
	luaL_setfuncs(L, l, 0);
	if (libname)
		lua_setglobal(L, libname);
#endif
}

/*
 * Get fields in an existing slurmctld job record.
 *
 * This is an incomplete list of job record fields. Add more as needed and
 * send patches to slurm-dev@schedmd.com.
 */
extern int slurm_lua_job_record_field(lua_State *L, const job_record_t *job_ptr,
				      const char *name)
{
	int i;

	if (!job_ptr) {
		error("_job_rec_field: job_ptr is NULL");
		lua_pushnil(L);
	} else if (!xstrcmp(name, "account")) {
		lua_pushstring(L, job_ptr->account);
	} else if (!xstrcmp(name, "admin_comment")) {
		lua_pushstring(L, job_ptr->admin_comment);
	} else if (!xstrcmp(name, "alloc_node")) {
               lua_pushstring(L, job_ptr->alloc_node);
	} else if (!xstrcmp(name, "argv")) {
		if (job_ptr->details)
			_setup_stringarray(L, job_ptr->details->argc,
					   job_ptr->details->argv);
		else
			lua_newtable(L);
	} else if (!xstrcmp(name, "array_job_id")) {
		lua_pushnumber(L, job_ptr->array_job_id);
	} else if (!xstrcmp(name, "array_task_cnt")) {
		if (job_ptr->array_recs)
			lua_pushnumber(L, job_ptr->array_recs->task_cnt);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "array_task_id")) {
		lua_pushnumber(L, job_ptr->array_task_id);
	} else if (!xstrcmp(name, "batch_features")) {
		lua_pushstring(L, job_ptr->batch_features);
	} else if (!xstrcmp(name, "batch_host")) {
		lua_pushstring(L, job_ptr->batch_host);
	} else if (!xstrcmp(name, "best_switch")) {
		lua_pushnumber(L, job_ptr->best_switch);
	} else if (!xstrcmp(name, "burst_buffer")) {
		lua_pushstring(L, job_ptr->burst_buffer);
	} else if (!xstrcmp(name, "comment")) {
		lua_pushstring(L, job_ptr->comment);
	} else if (!xstrcmp(name, "container")) {
		lua_pushstring(L, job_ptr->container);
	} else if (!xstrcmp(name, "cpus_per_tres")) {
		lua_pushstring(L, job_ptr->cpus_per_tres);
	} else if (!xstrcmp(name, "delay_boot")) {
		lua_pushnumber(L, job_ptr->delay_boot);
	} else if (!xstrcmp(name, "curr_dependency")) {
		/*
		 * Name it "curr_dependency" rather than "dependency" because
		 * the job's dependency value can change as individual
		 * dependencies change. This prevents the use of "dependency"
		 * when someone is expecting the original dependency value.
		 */
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->dependency);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "orig_dependency")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->orig_dependency);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "derived_ec")) {
		lua_pushnumber(L, job_ptr->derived_ec);
	} else if (!xstrcmp(name, "direct_set_prio")) {
		lua_pushnumber(L, job_ptr->direct_set_prio);
	} else if (!xstrcmp(name, "end_time")) {
		lua_pushnumber(L, job_ptr->end_time);
	} else if (!xstrcmp(name, "exit_code")) {
		lua_pushnumber(L, job_ptr->exit_code);
	} else if (!xstrcmp(name, "extra")) {
		lua_pushstring(L, job_ptr->extra);
	} else if (!xstrcmp(name, "features")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->features);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "gres")) {
		/* "gres" replaced by "tres_per_node" in v18.08 */
		lua_pushstring(L, job_ptr->tres_per_node);
	} else if (!xstrcmp(name, "gres_req")) {
		lua_pushstring(L, job_ptr->tres_fmt_req_str);
	} else if (!xstrcmp(name, "gres_used")) {
		lua_pushstring(L, job_ptr->gres_used);
	} else if (!xstrcmp(name, "group_id")) {
		lua_pushnumber(L, job_ptr->group_id);
	} else if (!xstrcmp(name, "job_id")) {
		lua_pushnumber(L, job_ptr->job_id);
	} else if (!xstrcmp(name, "job_state")) {
		lua_pushnumber(L, job_ptr->job_state);
	} else if (!xstrcmp(name, "licenses")) {
		lua_pushstring(L, job_ptr->licenses);
	} else if (!xstrcmp(name, "max_cpus")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->max_cpus);
		else
			lua_pushnumber(L, 0);
	} else if (!xstrcmp(name, "max_nodes")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->max_nodes);
		else
			lua_pushnumber(L, 0);
	} else if (!xstrcmp(name, "mcs_label")) {
		lua_pushstring(L, job_ptr->mcs_label);
	} else if (!xstrcmp(name, "mem_per_tres")) {
		lua_pushstring(L, job_ptr->mem_per_tres);
	} else if (!xstrcmp(name, "min_cpus")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->min_cpus);
		else
			lua_pushnumber(L, 0);
	} else if (!xstrcmp(name, "min_mem_per_node")) {
		if (job_ptr->details &&
		    (job_ptr->details->pn_min_memory != NO_VAL64) &&
		    !(job_ptr->details->pn_min_memory & MEM_PER_CPU))
			lua_pushnumber(L, job_ptr->details->pn_min_memory);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "min_mem_per_cpu")) {
		if (job_ptr->details &&
		    (job_ptr->details->pn_min_memory != NO_VAL64) &&
		    (job_ptr->details->pn_min_memory & MEM_PER_CPU))
			lua_pushnumber(L, job_ptr->details->pn_min_memory &
				       ~MEM_PER_CPU);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "min_nodes")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->min_nodes);
		else
			lua_pushnumber(L, 0);
	} else if (!xstrcmp(name, "name")) {
		lua_pushstring(L, job_ptr->name);
	} else if (!xstrcmp(name, "nice")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->nice);
		else
			lua_pushnumber(L, NO_VAL16);
	} else if (!xstrcmp(name, "nodes")) {
		lua_pushstring(L, job_ptr->nodes);
	} else if (!xstrcmp(name, "origin_cluster")) {
		lua_pushstring(L, job_ptr->origin_cluster);
		/* Continue support for old hetjob terminology. */
	} else if (!xstrcmp(name, "pack_job_id") ||
		   !xstrcmp(name, "het_job_id")) {
		lua_pushnumber(L, job_ptr->het_job_id);
	} else if (!xstrcmp(name, "pack_job_id_set") ||
		   !xstrcmp(name, "het_job_id_set")) {
		lua_pushstring(L, job_ptr->het_job_id_set);
	} else if (!xstrcmp(name, "pack_job_offset") ||
		   !xstrcmp(name, "het_job_offset")) {
		lua_pushnumber(L, job_ptr->het_job_offset);
	} else if (!xstrcmp(name, "partition")) {
		lua_pushstring(L, job_ptr->partition);
	} else if (!xstrcmp(name, "pn_min_cpus")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->pn_min_cpus);
		else
			lua_pushnumber(L, NO_VAL);
	} else if (!xstrcmp(name, "pn_min_memory")) {
		/*
		 * FIXME: Remove this in the future, lua can't handle 64bit
		 * numbers!!!.  Use min_mem_per_node|cpu instead.
		 */
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->pn_min_memory);
		else
			lua_pushnumber(L, (double) NO_VAL64);
	} else if (!xstrcmp(name, "priority")) {
		lua_pushnumber(L, job_ptr->priority);
	} else if (!xstrcmp(name, "qos")) {
		if (job_ptr->qos_ptr) {
			lua_pushstring(L, job_ptr->qos_ptr->name);
		} else {
			lua_pushnil(L);
		}
	} else if (!xstrcmp(name, "reboot")) {
		lua_pushnumber(L, job_ptr->reboot);
	} else if (!xstrcmp(name, "req_switch")) {
		lua_pushnumber(L, job_ptr->req_switch);
	} else if (!xstrcmp(name, "resizing")) {
		int resizing = IS_JOB_RESIZING(job_ptr) ? 1 : 0;
		lua_pushnumber(L, resizing);
	} else if (!xstrcmp(name, "restart_cnt")) {
		lua_pushnumber(L, job_ptr->restart_cnt);
	} else if (!xstrcmp(name, "resv_name")) {
		lua_pushstring(L, job_ptr->resv_name);
	} else if (!xstrcmp(name, "script")) {
		buf_t *bscript = get_job_script(job_ptr);
		if (bscript) {
			char *script = bscript->head;
			if (script && script[0] != '\0')
				lua_pushstring(L, script);
			else
				lua_pushnil(L);
		} else
			lua_pushnil(L);
		FREE_NULL_BUFFER(bscript);
	} else if (!xstrcmp(name, "segment_size")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->segment_size);
		else
			lua_pushnumber(L, 0);
	} else if (!xstrcmp(name, "selinux_context")) {
		lua_pushstring(L, job_ptr->selinux_context);
 	} else if (!xstrcmp(name, "site_factor")) {
		if (job_ptr->site_factor == NO_VAL)
			lua_pushnumber(L, job_ptr->site_factor);
		else
			lua_pushnumber(L,
				       (((int64_t)job_ptr->site_factor)
					- NICE_OFFSET));
	} else if (!xstrcmp(name, "spank_job_env")) {
		if ((job_ptr->spank_job_env_size == 0) ||
		    (job_ptr->spank_job_env == NULL)) {
			lua_pushnil(L);
		} else {
			lua_newtable(L);
			for (i = 0; i < job_ptr->spank_job_env_size; i++) {
				if (job_ptr->spank_job_env[i] != NULL) {
					lua_pushnumber(L, i);
					lua_pushstring(
						L, job_ptr->spank_job_env[i]);
					lua_settable(L, -3);
				}
			}
		}
	} else if (!xstrcmp(name, "spank_job_env_size")) {
		lua_pushnumber(L, job_ptr->spank_job_env_size);
	} else if (!xstrcmp(name, "start_time")) {
		lua_pushnumber(L, job_ptr->start_time);
	} else if (!xstrcmp(name, "std_err")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->std_err);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "std_in")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->std_in);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "std_out")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->std_out);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "submit_time")) {
		if (job_ptr->details)
			lua_pushnumber(L, job_ptr->details->submit_time);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "time_limit")) {
		lua_pushnumber(L, job_ptr->time_limit);
	} else if (!xstrcmp(name, "time_min")) {
		lua_pushnumber(L, job_ptr->time_min);
	} else if (!xstrcmp(name, "total_cpus")) {
		lua_pushnumber(L, job_ptr->total_cpus);
	} else if (!xstrcmp(name, "total_nodes")) {
		lua_pushnumber(L, job_ptr->total_nodes);
	} else if (!xstrcmp(name, "tres_alloc_str")) {
		lua_pushstring(L, job_ptr->tres_alloc_str);
	} else if (!xstrcmp(name, "tres_bind")) {
		lua_pushstring(L, job_ptr->tres_bind);
	} else if (!xstrcmp(name, "tres_fmt_alloc_str")) {
		lua_pushstring(L, job_ptr->tres_fmt_alloc_str);
	} else if (!xstrcmp(name, "tres_fmt_req_str")) {
		lua_pushstring(L, job_ptr->tres_fmt_req_str);
	} else if (!xstrcmp(name, "tres_freq")) {
		lua_pushstring(L, job_ptr->tres_freq);
	} else if (!xstrcmp(name, "tres_per_job")) {
		lua_pushstring(L, job_ptr->tres_per_job);
	} else if (!xstrcmp(name, "tres_per_node")) {
		lua_pushstring(L, job_ptr->tres_per_node);
	} else if (!xstrcmp(name, "tres_per_socket")) {
		lua_pushstring(L, job_ptr->tres_per_socket);
	} else if (!xstrcmp(name, "tres_per_task")) {
		lua_pushstring(L, job_ptr->tres_per_task);
	} else if (!xstrcmp(name, "tres_req_str")) {
		lua_pushstring(L, job_ptr->tres_req_str);
	} else if (!xstrcmp(name, "user_id")) {
		lua_pushnumber(L, job_ptr->user_id);
	} else if (!xstrcmp(name, "user_name")) {
		lua_pushstring(L, job_ptr->user_name);
	} else if (!xstrcmp(name, "wait4switch")) {
		lua_pushnumber(L, job_ptr->wait4switch);
	} else if (!xstrcmp(name, "wait4switch_start")) {
		lua_pushnumber(L, job_ptr->wait4switch_start);
	} else if (!xstrcmp(name, "wckey")) {
		lua_pushstring(L, job_ptr->wckey);
	} else if (!xstrcmp(name, "work_dir")) {
		if (job_ptr->details)
			lua_pushstring(L, job_ptr->details->work_dir);
		else
			lua_pushnil(L);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

/* Generic stack dump function for debugging purposes */
extern void slurm_lua_stack_dump(const char *plugin, char *header, lua_State *L)
{
#if _DEBUG
	int i;
	int top = lua_gettop(L);

	info("%s: dumping %s stack, %d elements", plugin, header, top);
	for (i = 1; i <= top; i++) {  /* repeat for each level */
		int type = lua_type(L, i);
		switch (type) {
		case LUA_TSTRING:
			info("string[%d]:%s", i, lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:
			info("boolean[%d]:%s", i,
			     lua_toboolean(L, i) ? "true" : "false");
			break;
		case LUA_TNUMBER:
			info("number[%d]:%d", i,
			     (int) lua_tonumber(L, i));
			break;
		default:
			info("other[%d]:%s", i, lua_typename(L, type));
			break;
		}
	}
#endif
}

extern int slurm_lua_loadscript(lua_State **L, const char *plugin,
				const char *script_path,
				const char **req_fxns,
				time_t *load_time,
				void (*local_options)(lua_State *L),
				char **err_msg)
{
	lua_State *new = NULL;
	lua_State *curr = *L;
	struct stat st;
	int rc = 0;
	char *err_str = NULL, *ret_err_str = NULL;

	if (stat(script_path, &st) != 0) {
		err_str = xstrdup_printf("Unable to stat %s: %s",
					 script_path, strerror(errno));
		goto fini_error;
	}

	if (st.st_mtime <= *load_time) {
		debug3("%s: %s: skipping loading Lua script: %s", plugin,
		       __func__, script_path);
		return SLURM_SUCCESS;
	}
	debug3("%s: %s: loading Lua script: %s", __func__, plugin, script_path);

	/* Initialize lua */
	if (!(new = luaL_newstate())) {
		err_str = xstrdup_printf("luaL_newstate() failed to allocate");
		goto fini_error;
	}

	luaL_openlibs(new);
	if (luaL_loadfile(new, script_path)) {
		err_str = xstrdup_printf("%s: %s",
					 script_path, lua_tostring(new, -1));
		lua_close(new);
		goto fini_error;
	}

	/*
	 *  Register Slurm functions in lua state:
	 *  logging and slurm structure read/write functions
	 */
	_register_slurm_output_functions(new);
	if (*(local_options))
		(*(local_options))(new);
	else
		lua_setglobal(new, "slurm"); /* done in local_options */

	/* Register error handler globally */
	_register_error_callback(new);

	/*
	 *  Run the user script:
	 */
	if ((rc = slurm_lua_pcall(new, 0, 1, &ret_err_str, __func__))) {
		err_str = xstrdup_printf("%s: %s", script_path, ret_err_str);
		xfree(ret_err_str);
		lua_close(new);
		goto fini_error;
	}

	/*
	 *  Get any return code from the lua script
	 */
	rc = (int) lua_tonumber(new, -1);
	if (rc != SLURM_SUCCESS) {
		err_str = xstrdup_printf("%s: returned %d on load",
					 script_path, rc);
		lua_close(new);
		goto fini_error;
	}

	/*
	 *  Check for required lua script functions:
	 */
	rc = _check_lua_script_functions(new, plugin, script_path, req_fxns);
	if (rc != SLURM_SUCCESS) {
		err_str = xstrdup_printf("%s: required function(s) not present",
					 script_path);
		goto fini_error;
	}

	*load_time = st.st_mtime;
	if (curr)
		lua_close(curr);
	*L = new;
	return SLURM_SUCCESS;

fini_error:
	if (curr) {
		xstrfmtcat(err_str, ", using previous script");
		rc = SLURM_SUCCESS;
	} else {
		rc = SLURM_ERROR;
	}
	error("%s: %s", plugin, err_str);
	if (err_msg) {
		xfree(*err_msg);
		*err_msg = err_str;
		err_str = NULL;
	} else {
		xfree(err_str);
	}
	return rc;
}
#endif

/*
 *  Init function to dlopen() the appropriate Lua libraries, and
 *  ensure the lua version matches what we compiled against along with other
 *  init things.
 */
extern int slurm_lua_init(void)
{
	slurm_lua_fini();

	char *const lua_libs[] = {
		"liblua.so",
#if LUA_VERSION_NUM == 504
		"liblua-5.4.so",
		"liblua5.4.so",
		"liblua5.4.so.0",
		"liblua.so.5.4",
#elif LUA_VERSION_NUM == 503
		"liblua-5.3.so",
		"liblua5.3.so",
		"liblua5.3.so.0",
		"liblua.so.5.3",
#elif LUA_VERSION_NUM == 502
		"liblua-5.2.so",
		"liblua5.2.so",
		"liblua5.2.so.0",
		"liblua.so.5.2",
#else
		"liblua-5.1.so",
		"liblua5.1.so",
		"liblua5.1.so.0",
		"liblua.so.5.1",
#endif
		NULL
	};
	int i = 0;

	/*
	 *  Need to dlopen() liblua.so with RTLD_GLOBAL in order to
	 *   ensure symbols from liblua are available to libs opened
	 *   by any lua scripts.
	 */
	if (!LUA_VERSION_NUM) {
		fatal("Slurm wasn't configured against any LUA lib but you are trying to use it like it was.  Please check config.log and reconfigure against liblua.  Make sure you have lua devel installed.");
	}

	while (lua_libs[i] &&
	       !(lua_handle = dlopen(lua_libs[i], RTLD_NOW | RTLD_GLOBAL)))
		i++;

	if (!lua_handle) {
		error("Failed to open liblua.so: %s", dlerror());
		return SLURM_ERROR;
	}

	/* Load any serializer plugins for JSON/YAML conversions */
	serializer_g_init();

	return SLURM_SUCCESS;
}

/*
 * Close down the lib, free memory and such.
 */
extern void slurm_lua_fini(void)
{
	if (lua_handle)
		dlclose(lua_handle);
}

static data_for_each_cmd_t _dump_data_foreach_list(const data_t *data,
						   void *arg)
{
	dump_data_foreach_args_t *args = arg;
	lua_State *L = args->L;

	xassert(args->magic == DUMP_DATA_FOREACH_ARGS_MAGIC);

	if ((args->rc = slurm_lua_from_data(L, data)))
		return DATA_FOR_EACH_FAIL;

	lua_rawseti(L, args->table_index, args->field_index);
	args->field_index++;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _dump_data_foreach_dict(const char *key,
						   const data_t *data,
						   void *arg)
{
	dump_data_foreach_args_t *args = arg;
	lua_State *L = args->L;

	xassert(args->magic == DUMP_DATA_FOREACH_ARGS_MAGIC);

	if ((args->rc = slurm_lua_from_data(L, data)))
		return DATA_FOR_EACH_FAIL;

	lua_setfield(L, args->table_index, key);

	return DATA_FOR_EACH_CONT;
}

static int _from_data_list(lua_State *L, const data_t *src)
{
	dump_data_foreach_args_t args = {
		.magic = DUMP_DATA_FOREACH_ARGS_MAGIC,
		.L = L,
		.rc = SLURM_SUCCESS,
		.field_index = 1,
	};

	lua_createtable(L, data_get_list_length(src), 0);

	args.table_index = lua_gettop(L);

	(void) data_list_for_each_const(src, _dump_data_foreach_list, &args);

	return args.rc;
}

static int _from_data_dict(lua_State *L, const data_t *src)
{
	dump_data_foreach_args_t args = {
		.magic = DUMP_DATA_FOREACH_ARGS_MAGIC,
		.L = L,
		.rc = SLURM_SUCCESS,
	};

	lua_createtable(L, data_get_dict_length(src), 0);

	args.table_index = lua_gettop(L);

	(void) data_dict_for_each_const(src, _dump_data_foreach_dict, &args);

	return args.rc;
}

static int _from_data(lua_State *L, const data_t *src)
{
	switch (data_get_type(src)) {
	case DATA_TYPE_LIST:
		return _from_data_list(L, src);
	case DATA_TYPE_DICT:
		return _from_data_dict(L, src);
	case DATA_TYPE_NULL:
		lua_pushnil(L);
		return SLURM_SUCCESS;
	case DATA_TYPE_INT_64:
		lua_pushinteger(L, data_get_int(src));
		return SLURM_SUCCESS;
	case DATA_TYPE_FLOAT:
		lua_pushnumber(L, data_get_float(src));
		return SLURM_SUCCESS;
	case DATA_TYPE_STRING:
		lua_pushstring(L, data_get_string(src));
		return SLURM_SUCCESS;
	case DATA_TYPE_BOOL:
		lua_pushboolean(L, data_get_bool(src));
		return SLURM_SUCCESS;
	case DATA_TYPE_NONE:
		;/* fall through */
	case DATA_TYPE_MAX:
		;/* fall through */
	};

	fatal_abort("should never happen");
}

extern int slurm_lua_from_data(lua_State *L, const data_t *src)
{
	if (!L)
		return ESLURM_LUA_INVALID_STATE;

	if (!src)
		return ESLURM_DATA_PTR_NULL;

	return _from_data(L, src);
}

/* Log details on function at index */
static void _log_function(lua_State *L, const int index, const char *label)
{
	lua_Debug ar = { 0 };

	lua_pushvalue(L, index);

	if (lua_getinfo(L, ">nSl", &ar))
		log_flag(SCRIPT, "%s: type=%s name=%s%s%s%s source=%s:%d-%d executing=%d",
			 label, ar.what, ((ar.name && ar.name[0]) ?
					  ar.name : "<ANONYMOUS>"),
			 ((ar.namewhat && ar.namewhat[0]) ? "(" : ""),
			 ((ar.namewhat && ar.namewhat[0]) ?
			  ar.namewhat : ""),
			 ((ar.namewhat && ar.namewhat[0]) ? ")" : ""),
			 ar.short_src, ar.linedefined,
			 ar.lastlinedefined, ar.currentline);

	lua_pop(L, 1);
}

/* Ask Lua for string and retry by converting to a string */
static int _dump_string(lua_State *L, char **ptr, const int index,
			const char *label)
{
	const char *str = NULL;
	size_t len = 0;

	if (!(str = lua_tolstring(L, index, &len)) &&
	    luaL_callmeta(L, index, "__tostring"))
		str = lua_tolstring(L, index, &len);

	/* Only log string if it was set and was a sane length */
	if (!str || (len <= 0) || (len >= MAX_VAL)) {
		log_flag(SCRIPT, "%s: invalid string", label);
		return ESLURM_LUA_INVALID_CONVERSION_TYPE;
	}

	*ptr = xstrndup(str, len);
	return SLURM_SUCCESS;
}

static int _dump_data_string(lua_State *L, data_t *dst, const int index,
			     const char *label)
{
	char *str = NULL;
	int rc = EINVAL;

	if ((rc = _dump_string(L, &str, index, label)))
		return rc;

	log_flag_hex(SCRIPT, str, (xsize(str) - 1), "%s: string", label);
	data_set_string_own(dst, str);
	return SLURM_SUCCESS;
}

static int _foreach_table_row(lua_State *L, data_t *dst, const int index,
			      const char *parent, const int depth)
{
	/*
	 * Lua stack:
	 * -1 -> value
	 * -2 -> key
	 */
	const int key_type = lua_type(L, -2);
	char *label = NULL;
	data_t *child = NULL;
	int rc = EINVAL;

	if (key_type == LUA_TNUMBER) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT)
			xstrfmtcat(label, "%s[" LUA_NUMBER_FMT "]", parent,
				   lua_tonumber(L, -2));

#if LUA_VERSION_NUM >= 503
		/* Skip conversion to string */
		if (lua_isinteger(L, -2))
			child = data_key_set_int(dst, lua_tointeger(L, -2));
#endif
	}

	if (!child) {
		char *key = NULL;

		if ((rc = _dump_string(L, &key, -2, parent)))
			return rc;
		else if (!key)
			return ESLURM_LUA_INVALID_CONVERSION_TYPE;

		if ((slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT) && !label)
			xstrfmtcat(label, "%s[%s]", parent, key);

		child = data_key_set(dst, key);
		xfree(key);
	}

	if ((rc = _lua_to_data(L, child, lua_gettop(L), (depth + 1), label,
			       true)))
		return rc;

	/* Pop value off stack to use key for next loop */
	lua_pop(L, 1);
	xfree(label);
	return SLURM_SUCCESS;
}

static int _dump_table(lua_State *L, data_t *dst, const int index,
		       const char *parent, const int depth)
{
	int rc = SLURM_SUCCESS;

	xassert(lua_istable(L, index));

	(void) data_set_dict(dst);

	/* Walk each table row */
	lua_pushnil(L);

	while (lua_next(L, index) && !rc)
		rc = _foreach_table_row(L, dst, index, parent, depth);

	return rc;
}

/* Log details on unsupported type at index */
static void _log_invalid_type(lua_State *L, const int index, const char *label,
			      const int type, const char *typename)
{
	char *str = NULL;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT))
		return;

	if (type == LUA_TFUNCTION)
		_log_function(L, index, label);
	else
		(void) _dump_string(L, &str, index, label);

	if (str)
		log_flag_hex(SCRIPT, str, xsize(str),
			     "%s: unsupported Lua type[0x%x]: %s",
			     label, type, typename);
	else
		log_flag(SCRIPT, "%s: unsupported Lua type[0x%x]: %s",
			     label, type, typename);

	xfree(str);
}

static int _lua_to_data(lua_State *L, data_t *dst, const int index,
			const int depth, const char *parent,
			const bool parent_is_table)
{
	int rc = EINVAL;
	char *label = NULL;
	const int type = lua_type(L, index);
	const char *typename = lua_typename(L, type);

	xassert(index > 0);

	/* Add type to Label (or hex if not an unknown Lua type) */
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT))
		; /* do nothing without logging active */
	else if (parent_is_table)
		label = xstrdup(parent);
	else if (typename)
		xstrfmtcat(label, "%s->%s", parent, typename);
	else
		xstrfmtcat(label, "%s->0x%0x", parent, type);

	if (depth > MAX_DEPTH) {
		log_flag(SCRIPT, "%s: table depth %d/%d too deep",
			 label, depth, MAX_DEPTH);
		rc = ESLURM_LUA_INVALID_CONVERSION_TYPE;
		goto done;
	}

	if (luaL_getmetafield(L, index, "__metatable") != LUA_TNIL) {
		const char *metatable = NULL;

		if (!(metatable = lua_tostring(L, -1))) {
			/* Metatable name isn't string? */
			metatable = "INVALID";
		}

		log_flag(SCRIPT, "%s: rejecting __metatable==%s",
			 label, metatable);

		lua_pop(L, 1);
		rc = ESLURM_LUA_INVALID_CONVERSION_TYPE;
		goto done;
	}

	switch (type) {
	case LUA_TNONE:
		log_flag(SCRIPT, "%s: none", label);
		data_set_null(dst);
		rc = SLURM_SUCCESS;
		goto done;
	case LUA_TNIL:
		log_flag(SCRIPT, "%s: nil", label);
		data_set_null(dst);
		rc = SLURM_SUCCESS;
		goto done;
	case LUA_TNUMBER:
		log_flag(SCRIPT, "%s: number=" LUA_NUMBER_FMT,
			 label, lua_tonumber(L, index));
#if LUA_VERSION_NUM >= 503
		if (lua_isinteger(L, index))
			data_set_int(dst, lua_tointeger(L, index));
		else
#endif
			data_set_float(dst, lua_tonumber(L, index));
		rc = SLURM_SUCCESS;
		goto done;
	case LUA_TBOOLEAN:
		log_flag(SCRIPT, "%s: boolean=%s",
			 label, BOOL_STRINGIFY(lua_toboolean(L, index)));
		data_set_bool(dst, lua_toboolean(L, index));
		rc = SLURM_SUCCESS;
		goto done;
	case LUA_TSTRING:
		rc = _dump_data_string(L, dst, index, label);
		goto done;
	case LUA_TTABLE:
		rc = _dump_table(L, dst, index, label, depth);
		goto done;
	}

	_log_invalid_type(L, index, label, type, typename);
	rc = ESLURM_LUA_INVALID_CONVERSION_TYPE;

done:
	xfree(label);
	return rc;
}

static int _to_data(lua_State *L, data_t *dst, const int index)
{
	char *label = NULL;
	int rc = EINVAL, copy = -1;
	const int top = lua_gettop(L);

	/* Copy to avoid changing value due to conversion to string */
	lua_pushvalue(L, index);
	copy = lua_gettop(L);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT)
		xstrfmtcat(label, "%s: Lua@0x%" PRIxPTR "[+%d]", __func__,
			   (uintptr_t) L, copy);

	if ((rc = _lua_to_data(L, dst, copy, 0, label, false)))
		data_set_null(dst);

	/* drop any additions to the stack */
	lua_settop(L, top);

	xfree(label);
	return rc;
}

extern int slurm_lua_to_data(lua_State *L, data_t *dst)
{
	if (!L)
		return ESLURM_LUA_INVALID_STATE;

	if (!dst)
		return ESLURM_DATA_PTR_NULL;

	return _to_data(L, dst, lua_gettop(L));
}
