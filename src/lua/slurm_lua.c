/*****************************************************************************\
 *  slurm_lua.c - Lua integration common functions
 *****************************************************************************
 *  Copyright (C) 2015-2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/xstring.h"
#include "src/lua/slurm_lua.h"

static void *lua_handle = NULL;

#ifdef HAVE_LUA

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

/*
 *  check that global symbol [name] in lua script is a function
 */
static int _check_lua_script_function(lua_State *L, const char *name)
{
	int rc = 0;
	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1))
		rc = -1;
	lua_pop(L, -1);
	return (rc);
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
		if (_check_lua_script_function(L, *ptr) < 0) {
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

static const struct luaL_Reg slurm_functions [] = {
	{ "log", _log_lua_msg },
	{ "error", _log_lua_error },
	{ "time_str2mins", _time_str2mins},
	{ NULL, NULL }
};

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

	/*
	 * Error codes: slurm.SUCCESS, slurm.FAILURE, slurm.ERROR, etc.
	 */
	lua_pushnumber(L, SLURM_ERROR);
	lua_setfield(L, -2, "ERROR");
	lua_pushnumber(L, SLURM_ERROR);
	lua_setfield(L, -2, "FAILURE");
	lua_pushnumber(L, SLURM_SUCCESS);
	lua_setfield(L, -2, "SUCCESS");
	lua_pushnumber(L, ESLURM_ACCESS_DENIED);
	lua_setfield(L, -2, "ESLURM_ACCESS_DENIED");
	lua_pushnumber(L, ESLURM_ACCOUNTING_POLICY);
	lua_setfield(L, -2, "ESLURM_ACCOUNTING_POLICY");
	lua_pushnumber(L, ESLURM_INVALID_ACCOUNT);
	lua_setfield(L, -2, "ESLURM_INVALID_ACCOUNT");
	lua_pushnumber(L, ESLURM_INVALID_LICENSES);
	lua_setfield(L, -2, "ESLURM_INVALID_LICENSES");
	lua_pushnumber(L, ESLURM_INVALID_NODE_COUNT);
	lua_setfield(L, -2, "ESLURM_INVALID_NODE_COUNT");
	lua_pushnumber(L, ESLURM_INVALID_TIME_LIMIT);
	lua_setfield(L, -2, "ESLURM_INVALID_TIME_LIMIT");
	lua_pushnumber(L, ESLURM_JOB_MISSING_SIZE_SPECIFICATION);
	lua_setfield(L, -2, "ESLURM_JOB_MISSING_SIZE_SPECIFICATION");
	lua_pushnumber(L, ESLURM_MISSING_TIME_LIMIT);
	lua_setfield(L, -2, "ESLURM_MISSING_TIME_LIMIT");

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
	lua_pushnumber(L, GRES_DISABLE_BIND);
	lua_setfield(L, -2, "GRES_DISABLE_BIND");
	lua_pushnumber(L, GRES_ENFORCE_BIND);
	lua_setfield(L, -2, "GRES_ENFORCE_BIND");
	lua_pushnumber(L, KILL_INV_DEP);
	lua_setfield(L, -2, "KILL_INV_DEP");
	lua_pushnumber(L, NO_KILL_INV_DEP);
	lua_setfield(L, -2, "NO_KILL_INV_DEP");
	lua_pushnumber(L, SPREAD_JOB);
	lua_setfield(L, -2, "SPREAD_JOB");
	lua_pushnumber(L, USE_MIN_NODES);
	lua_setfield(L, -2, "USE_MIN_NODES");

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
	} else if (!xstrcmp(name, "derived_ec")) {
		lua_pushnumber(L, job_ptr->derived_ec);
	} else if (!xstrcmp(name, "direct_set_prio")) {
		lua_pushnumber(L, job_ptr->direct_set_prio);
	} else if (!xstrcmp(name, "end_time")) {
		lua_pushnumber(L, job_ptr->end_time);
	} else if (!xstrcmp(name, "exit_code")) {
		lua_pushnumber(L, job_ptr->exit_code);
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
		free_buf(bscript);
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

extern int slurm_lua_isnumber(lua_State *L, int index)
{
	/* lua_isnumber didn't exist before lua5.3 */
#if LUA_VERSION_NUM == 503
	return lua_isnumber(L, index);
#else
	return lua_isnumber(L, index);
#endif
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
				void (*local_options)(lua_State *L))
{
	lua_State *new = NULL;
	lua_State *curr = *L;
	struct stat st;
	int rc = 0;

	if (stat(script_path, &st) != 0) {
		if (curr) {
			(void) error("%s: Unable to stat %s, using old script: %s",
			             plugin, script_path, strerror(errno));
			return SLURM_SUCCESS;
		}
		(void) error("%s: Unable to stat %s: %s",
		             plugin, script_path, strerror(errno));
		return SLURM_ERROR;
	}

	if (st.st_mtime <= *load_time) {
		debug3("%s: %s: skipping loading Lua script: %s", plugin,
		       __func__, script_path);
		return SLURM_SUCCESS;
	}
	debug3("%s: %s: loading Lua script: %s", __func__, plugin, script_path);

	/* Initialize lua */
	if (!(new = luaL_newstate())) {
		error("%s: %s: luaL_newstate() failed to allocate.",
		      plugin, __func__);
		return SLURM_SUCCESS;
	}

	luaL_openlibs(new);
	if (luaL_loadfile(new, script_path)) {
		if (curr) {
			error("%s: %s: %s, using previous script",
			      plugin, script_path,
			      lua_tostring(new, -1));
			lua_close(new);
			return SLURM_SUCCESS;
		}
		error("%s: %s: %s", plugin, script_path,
		      lua_tostring(new, -1));
		lua_pop(new, 1);
		lua_close(new);
		return SLURM_ERROR;
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


	/*
	 *  Run the user script:
	 */
	if (lua_pcall(new, 0, 1, 0)) {
		if (curr) {
			error("%s: %s: %s, using previous script",
			      plugin, script_path,
			      lua_tostring(new, -1));
			lua_close(new);
			return SLURM_SUCCESS;
		}
		error("%s: %s: %s", plugin, script_path,
		      lua_tostring(new, -1));
		lua_pop(new, 1);
		lua_close(new);
		return SLURM_ERROR;
	}

	/*
	 *  Get any return code from the lua script
	 */
	rc = (int) lua_tonumber(new, -1);
	if (rc != SLURM_SUCCESS) {
		if (curr) {
			(void) error("%s: %s: returned %d on load, using previous script",
			             plugin, script_path, rc);
			lua_close(new);
			return SLURM_SUCCESS;
		}
		(void) error("%s: %s: returned %d on load", plugin,
			     script_path, rc);
		lua_pop(new, 1);
		lua_close(new);
		return SLURM_ERROR;
	}

	/*
	 *  Check for required lua script functions:
	 */
	rc = _check_lua_script_functions(new, plugin, script_path, req_fxns);
	if (rc != SLURM_SUCCESS) {
		if (curr) {
			(void) error("%s: %s: required function(s) not present, using previous script",
			             plugin, script_path);
			lua_close(new);
			return SLURM_SUCCESS;
		}
		lua_close(new);
		return SLURM_ERROR;
	}

	*load_time = st.st_mtime;
	if (curr)
		lua_close(curr);
	*L = new;
	return SLURM_SUCCESS;
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
