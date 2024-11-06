/*****************************************************************************\
 *  jobcomp_lua.c - Job completion plugin for user-defined lua script
 *****************************************************************************
 *  Copyright (C) 2019 The Regents of the University of California.
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
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

#include <inttypes.h>
#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/interfaces/jobcomp.h"
#include "src/lua/slurm_lua.h"
#include "src/slurmctld/slurmctld.h"

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job completion logging LUA plugin";
const char plugin_type[]       	= "jobcomp/lua";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;
static char *lua_script_path;
static lua_State *L = NULL;
static time_t lua_script_last_loaded = (time_t) 0;
static int _job_rec_field_index(lua_State *L);
static void _push_job_rec(job_record_t *job_ptr);
static int _set_job_rec_field_index(lua_State *L);

static const char *req_fxns[] = {
	"slurm_jobcomp_log_record",
	NULL
};

/*
 *  Mutex for protecting multi-threaded access to this plugin.
 *   (Only 1 thread at a time should be in here)
 */
static pthread_mutex_t lua_lock = PTHREAD_MUTEX_INITIALIZER;

static void _push_job_rec(job_record_t *job_ptr)
{
	lua_newtable(L);

	lua_newtable(L);
	lua_pushcfunction(L, _job_rec_field_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, _set_job_rec_field_index);
	lua_setfield(L, -2, "__newindex");
	/* Store the job_ptr in the metatable, so the index
	 * function knows which struct it's getting data for.
	 */
	lua_pushlightuserdata(L, job_ptr);
	lua_setfield(L, -2, "_job_rec_ptr");
	lua_setmetatable(L, -2);
}

/* Get fields in an existing slurmctld job_record */
static int _job_rec_field_index(lua_State *st)
{
	const char *name = luaL_checkstring(st, 2);
	job_record_t *job_ptr;

	lua_getmetatable(st, -2);
	lua_getfield(st, -1, "_job_rec_ptr");
	job_ptr = lua_touserdata(st, -1);

	return slurm_lua_job_record_field(st, job_ptr, name);
}

/* Set fields in the job request structure on job submit or modify */
static int _set_job_rec_field_index(lua_State *st)
{
	const char *name, *value_str;
	job_record_t *job_ptr;

	name = luaL_checkstring(st, 2);
	lua_getmetatable(st, -3);
	lua_getfield(st, -1, "_job_rec_ptr");
	job_ptr = lua_touserdata(st, -1);
	if (job_ptr == NULL) {
		error("%s: job_ptr is NULL", __func__);
	} else if (!xstrcmp(name, "admin_comment")) {
		value_str = luaL_checkstring(st, 3);
		xfree(job_ptr->admin_comment);
		if (strlen(value_str))
			job_ptr->admin_comment = xstrdup(value_str);
	} else {
		error("%s: unrecognized field: %s", __func__, name);
	}
	return SLURM_SUCCESS;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int rc = SLURM_SUCCESS;

	if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
		return rc;

	lua_script_path = get_extra_conf_path("jobcomp.lua");
	slurm_mutex_lock(&lua_lock);
	rc = slurm_lua_loadscript(&L, "job_comp/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, NULL);
	slurm_mutex_unlock(&lua_lock);

	return rc;
}

extern int fini(void)
{
	if (L) {
		lua_close(L);
		L = NULL;
		lua_script_last_loaded = 0;
	}
	xfree(lua_script_path);

	slurm_lua_fini();

	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm job completion
 * logging API.
 */

extern int jobcomp_p_set_location(void)
{
	return SLURM_SUCCESS;
}

extern int jobcomp_p_log_record(job_record_t *job_ptr)
{
	int rc;
	slurm_mutex_lock(&lua_lock);

	rc = slurm_lua_loadscript(&L, "jobcomp/lua", lua_script_path,
				  req_fxns, &lua_script_last_loaded, NULL);

	if (rc != SLURM_SUCCESS)
		goto out;

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_jobcomp_log_record");
	if (lua_isnil(L, -1))
		goto out;

	_push_job_rec(job_ptr);
	slurm_lua_stack_dump("jobcomp/lua", "log_record, before lua_pcall", L);
	if (lua_pcall (L, 1, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring (L, -1));
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
	slurm_lua_stack_dump("jobcomp/lua", "log_record, after lua_pcall", L);

out:	slurm_mutex_unlock(&lua_lock);
	return rc;
}

extern List jobcomp_p_get_jobs(void *job_cond)
{
	return NULL;
}
