/*****************************************************************************\
 *  job_submit_lua.c - Set defaults in job submit request specifications.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

#define MIN_ACCTG_FREQUENCY 30

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
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char plugin_name[]       	= "Job submit lua plugin";
const char plugin_type[]       	= "job_submit/lua";
const uint32_t plugin_version   = 100;
const uint32_t min_plug_version = 100;

static const char lua_script_path[] = DEFAULT_SCRIPT_DIR "/job_submit.lua";
static lua_State *L = NULL;
static struct job_descriptor *job_ptr = NULL;

/*
 *  Mutex for protecting multi-threaded access to this plugin.
 *   (Only 1 thread at a time should be in here)
 */
#ifdef WITH_PTHREADS
static pthread_mutex_t lua_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@lists.llnl.gov  Thanks!
\*****************************************************************************/

/*
 *  Lua interface to SLURM log facility:
 */
static int _log_lua_msg (lua_State *L)
{
	const char *prefix  = "job_submit.lua";
	int        level    = 0;
	const char *msg;

	/*
	 *  Optional numeric prefix indicating the log level
	 *  of the message.
	 */

	/*
	 *  Pop message off the lua stack
	 */
	msg = lua_tostring(L, -1);
	lua_pop (L, 1);
	/*
	 *  Pop level off stack:
	 */
	level = (int)lua_tonumber (L, -1);
	lua_pop (L, 1);

	/*
	 *  Call appropriate slurm log function based on log-level argument
	 */
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

static int _log_lua_error (lua_State *L)
{
	const char *prefix  = "job_submit.lua";
	const char *msg     = lua_tostring (L, -1);
	error ("%s: %s", prefix, msg);
	return (0);
}

static const struct luaL_Reg slurm_functions [] = {
	{ "log",   _log_lua_msg   },
	{ "error", _log_lua_error },
	{ NULL,    NULL        }
};

static void _register_lua_slurm_output_functions (void)
{
	/*
	 *  Register slurm output functions in a global "slurm" table
	 */
	lua_newtable (L);
	luaL_register (L, NULL, slurm_functions);

	/*
	 *  Create more user-friendly lua versions of SLURM log functions.
	 */
	luaL_loadstring (L, "slurm.error (string.format(unpack({...})))");
	lua_setfield (L, -2, "log_error");
	luaL_loadstring (L, "slurm.log (0, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_info");
	luaL_loadstring (L, "slurm.log (1, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_verbose");
	luaL_loadstring (L, "slurm.log (2, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_debug");
	luaL_loadstring (L, "slurm.log (3, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_debug2");
	luaL_loadstring (L, "slurm.log (4, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_debug3");
	luaL_loadstring (L, "slurm.log (5, string.format(unpack({...})))");
	lua_setfield (L, -2, "log_debug4");

	/*
	 * slurm.SUCCESS, slurm.FAILURE and slurm.ERROR
	 */
	lua_pushnumber (L, SLURM_FAILURE);
	lua_setfield (L, -2, "FAILURE");
	lua_pushnumber (L, SLURM_ERROR);
	lua_setfield (L, -2, "ERROR");
	lua_pushnumber (L, SLURM_SUCCESS);
	lua_setfield (L, -2, "SUCCESS");

	lua_setglobal (L, "slurm");
}

static int _get_job_field (lua_State *L)
{
//	const struct job_descriptor *job_ptr = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 1);

	if (job_ptr == NULL) {
		error("_get_job_field: job_ptr is NULL");
		lua_pushnil (L);
	} else if (!strcmp(name, "account")) {
		lua_pushstring (L, job_ptr->account);
	} else if (!strcmp(name, "partition")) {
		lua_pushstring (L, job_ptr->partition);
	} else {
		lua_pushnil (L);
	}

	return 1;
}

static int _set_job_field (lua_State *L)
{
//	const struct job_descriptor *job_ptr = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 1);
	const char *value_str;

	if (job_ptr == NULL) {
		error("_set_job_field: job_ptr is NULL");
		lua_pushnil (L);
	} else if (!strcmp(name, "account")) {
		value_str = luaL_checkstring(L, 1);
		xfree(job_ptr->account);
		if (strlen(value_str))
			job_ptr->account = xstrdup(value_str);
	} else if (!strcmp(name, "partition")) {
		value_str = luaL_checkstring(L, 1);
		xfree(job_ptr->partition);
		if (strlen(value_str))
			job_ptr->partition = xstrdup(value_str);
	} else {
		error("_set_job_field: unrecognized field: %s", name);
	}

	return 0;
}

static void _register_lua_slurm_job_functions (void)
{
	lua_pushcfunction(L, _get_job_field);
	lua_setglobal(L, "_get_job_field");
	lua_pushcfunction(L, _set_job_field);
	lua_setglobal(L, "_set_job_field");
}

/*
 *  check that global symbol [name] in lua script is a function
 */
static int _check_lua_script_function(const char *name)
{
	int rc = 0;
	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1))
		rc = -1;
	lua_pop(L, -1);
	return (rc);
}

/*
 *   Verify all required functions are defined in the job_submit/lua script
 */
static int _check_lua_script_functions()
{
	int rc = 0;
	int i;
	const char *fns[] = {
		"slurm_job_submit",
		"slurm_job_modify",
		NULL
	};

	i = 0;
	do {
		if(_check_lua_script_function(fns[i]) < 0) {
			error("job_submit/lua: %s: "
			      "missing required function %s",
			      lua_script_path, fns[i]);
			rc = -1;
		}
	} while(fns[++i]);

	return (rc);
}

static int _fill_job_desc_from_lua(struct job_descriptor *job_desc)
{
	lua_getfield(L, -1, "acctg_freq");
	job_desc->acctg_freq = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "begin_time");
	job_desc->begin_time = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "comment");
	xfree(job_desc->comment);
	job_desc->comment = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "contiguous");
	job_desc->contiguous = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "cores_per_socket");
	job_desc->cores_per_socket = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "dependency");
	xfree(job_desc->dependency);
	job_desc->dependency = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "end_time");
	job_desc->end_time = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "exc_nodes");
	xfree(job_desc->exc_nodes);
	job_desc->exc_nodes = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "features");
	xfree(job_desc->features);
	job_desc->features = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "gres");
	xfree(job_desc->gres);
	job_desc->gres = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "licenses");
	xfree(job_desc->licenses);
	job_desc->licenses = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "max_cpus");
	job_desc->max_cpus = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "max_nodes");
	job_desc->max_nodes = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "min_cpus");
	job_desc->min_cpus = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "min_nodes");
	job_desc->min_nodes = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "name");
	xfree(job_desc->name);
	job_desc->name = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "nice");
	job_desc->nice = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "ntasks_per_node");
	job_desc->ntasks_per_node = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "num_tasks");
	job_desc->num_tasks = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "pn_min_cpus");
	job_desc->pn_min_cpus = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "pn_min_memory");
	job_desc->pn_min_memory = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "pn_min_tmp_disk");
	job_desc->pn_min_tmp_disk = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "priority");
	job_desc->priority = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "qos");
	xfree(job_desc->qos);
	job_desc->qos = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "requeue");
	job_desc->requeue = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "req_nodes");
	xfree(job_desc->req_nodes);
	job_desc->req_nodes = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "reservation");
	xfree(job_desc->reservation);
	job_desc->reservation = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, -1, "shared");
	job_desc->shared = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "sockets_per_node");
	job_desc->sockets_per_node = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "threads_per_core");
	job_desc->threads_per_core = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "time_limit");
	job_desc->time_limit = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "time_min");
	job_desc->time_min = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, -1, "wckey");
	xfree(job_desc->wckey);
	job_desc->wckey = xstrdup(lua_tostring(L, -1));
	lua_pop(L, 1);

	return SLURM_SUCCESS;
}

/*
 *  Create lua 'job' table and leave it on the (lua) stack.
 */
static int _create_lua_job_desc_table(struct job_descriptor *job_desc)
{
	lua_newtable(L);

	lua_pushnumber(L, job_desc->acctg_freq);
	lua_setfield(L, -2, "acctg_freq");
	lua_pushnumber(L, job_desc->begin_time);
	lua_setfield(L, -2, "begin_time");
	lua_pushstring(L, job_desc->comment);
	lua_setfield(L, -2, "comment");
	lua_pushnumber(L, job_desc->contiguous);
	lua_setfield(L, -2, "contiguous");
	lua_pushnumber(L, job_desc->cores_per_socket);
	lua_setfield(L, -2, "cores_per_socket");
	lua_pushstring(L, job_desc->dependency);
	lua_setfield(L, -2, "dependency");
	lua_pushnumber(L, job_desc->end_time);
	lua_setfield(L, -2, "end_time");
	lua_pushstring(L, job_desc->exc_nodes);
	lua_setfield(L, -2, "exc_nodes");
	lua_pushstring(L, job_desc->features);
	lua_setfield(L, -2, "features");
	lua_pushstring(L, job_desc->gres);
	lua_setfield(L, -2, "gres");
	lua_pushstring(L, job_desc->licenses);
	lua_setfield(L, -2, "licenses");
	lua_pushnumber(L, job_desc->max_cpus);
	lua_setfield(L, -2, "max_cpus");
	lua_pushnumber(L, job_desc->max_nodes);
	lua_setfield(L, -2, "max_nodes");
	lua_pushnumber(L, job_desc->min_cpus);
	lua_setfield(L, -2, "min_cpus");
	lua_pushnumber(L, job_desc->min_nodes);
	lua_setfield(L, -2, "min_nodes");
	lua_pushstring(L, job_desc->name);
	lua_setfield(L, -2, "name");
	lua_pushnumber(L, job_desc->nice);
	lua_setfield(L, -2, "nice");
	lua_pushnumber(L, job_desc->ntasks_per_node);
	lua_setfield(L, -2, "ntasks_per_node");
	lua_pushnumber(L, job_desc->num_tasks);
	lua_setfield(L, -2, "num_tasks");
	lua_pushstring(L, job_desc->partition);
	lua_setfield(L, -2, "partition");
	lua_pushnumber(L, job_desc->pn_min_cpus);
	lua_setfield(L, -2, "pn_min_cpus");
	lua_pushnumber(L, job_desc->pn_min_memory);
	lua_setfield(L, -2, "pn_min_memory");
	lua_pushnumber(L, job_desc->pn_min_tmp_disk);
	lua_setfield(L, -2, "pn_min_tmp_disk");
	lua_pushnumber(L, job_desc->priority);
	lua_setfield(L, -2, "priority");
	lua_pushstring(L, job_desc->qos);
	lua_setfield(L, -2, "qos");
	lua_pushnumber(L, job_desc->requeue);
	lua_setfield(L, -2, "requeue");
	lua_pushstring(L, job_desc->req_nodes);
	lua_setfield(L, -2, "req_nodes");
	lua_pushstring(L, job_desc->reservation);
	lua_setfield(L, -2, "reservation");
	lua_pushnumber(L, job_desc->shared);
	lua_setfield(L, -2, "shared");
	lua_pushnumber(L, job_desc->sockets_per_node);
	lua_setfield(L, -2, "sockets_per_node");
	lua_pushnumber(L, job_desc->threads_per_core);
	lua_setfield(L, -2, "threads_per_core");
	lua_pushnumber(L, job_desc->time_limit);
	lua_setfield(L, -2, "time_limit");
	lua_pushnumber(L, job_desc->time_min);
	lua_setfield(L, -2, "time_min");
	lua_pushstring(L, job_desc->wckey);
	lua_setfield(L, -2, "wckey");

	return (0);
}

/*
 *  NOTE: The init callback should never be called multiple times,
 *   let alone called from multiple threads. Therefore, locking
 *   is unnecessary here.
 */
int init (void)
{
	int rc = SLURM_SUCCESS;

	/*
	 *  Need to dlopen() liblua.so with RTLD_GLOBAL in order to
	 *   ensure symbols from liblua are available to libs opened
	 *   by any lua scripts.
	 */
	if (!dlopen("liblua.so", RTLD_NOW | RTLD_GLOBAL)) {
		if (!dlopen ("liblua5.1.so", RTLD_NOW | RTLD_GLOBAL)) {
			return (error("Failed to open liblua.so: %s",
				      dlerror()));
		}
	}

	/*
	 *  Initilize lua
	 */
	L = luaL_newstate();
	luaL_openlibs(L);
	if (luaL_loadfile(L, lua_script_path)) {
		return error("lua: %s: %s", lua_script_path,
			     lua_tostring(L, -1));
	}

	/*
	 *  Register SLURM functions in lua state:
	 *  logging and job state read/write
	 */
	_register_lua_slurm_output_functions();
	_register_lua_slurm_job_functions();

	/*
	 *  Run the user script:
	 */
	if (lua_pcall(L, 0, 1, 0) != 0) {
		return error("job_submit/lua: %s: %s",
			     lua_script_path, lua_tostring (L, -1));
	}

	/*
	 *  Get any return code from the lua script
	 */
	rc = (int) lua_tonumber(L, -1);
	lua_pop (L, 1);
	if (rc != SLURM_SUCCESS)
		return rc;

	/*
	 *  Check for required lua script functions:
	 */
	return (_check_lua_script_functions());
}

int fini (void)
{
	lua_close (L);
	return SLURM_SUCCESS;
}


/* Lua script hook called for "submit job" event. */
extern int job_submit(struct job_descriptor *job_desc)
{
	int rc = SLURM_ERROR;
	slurm_mutex_lock (&lua_lock);

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_job_submit");
	if (lua_isnil(L, -1))
		goto out;

	_create_lua_job_desc_table(job_desc);
	//lua_newtable (L);
	//lua_pushlightuserdata (L, job_desc);
job_ptr = job_desc;
	if (lua_pcall (L, 1, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring (L, -1));
		goto out;
	}

	/*
	 *  Get the container id off the stack:
	 */
	if (lua_isnil (L, -1)) {
		error ("%s/lua: problem running", __func__);
		lua_pop (L, -1);
		goto out;
	}

	/*
	 *   table of what we pushed down to lua in _create_lua_job_desc_table
	 */
	if (!lua_istable(L, -1)) {
		error ("%s/lua: function should return a table",
			__func__);
		goto out;
	}

	rc = _fill_job_desc_from_lua(job_desc);

out:
	slurm_mutex_unlock (&lua_lock);
	return rc;
}

/* Lua script hook called for "modify job" event. */
extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr)
{
	int rc = SLURM_ERROR;
	slurm_mutex_lock (&lua_lock);

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_job_modify");
	if (lua_isnil(L, -1))
		goto out;

	_create_lua_job_desc_table(job_desc);
	if (lua_pcall (L, 1, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring (L, -1));
		goto out;
	}

	/*
	 *  Get the container id off the stack:
	 */
	if (lua_isnil (L, -1)) {
		error ("%s/lua: problem running", __func__);
		lua_pop (L, -1);
		goto out;
	}

	/*
	 *   table of what we pushed down to lua in _create_lua_job_desc_table
	 */
	if (!lua_istable(L, -1)) {
		error ("%s/lua: function should return a table",
			__func__);
		goto out;
	}

	rc = _fill_job_desc_from_lua(job_desc);

out:
	slurm_mutex_unlock (&lua_lock);
	return rc;
}
