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

/* Get fields in an existing slurmctld job record
 * NOTE: This is an incomplete list of job record fields.
 * Add more as needed and send patches to slurm-dev@llnl.gov */
static int _get_job_rec_field (lua_State *L)
{
	const struct job_record *job_ptr = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);

	if (job_ptr == NULL) {
		error("_get_job_field: job_ptr is NULL");
		lua_pushnil (L);
	} else if (!strcmp(name, "account")) {
		lua_pushstring (L, job_ptr->account);
	} else if (!strcmp(name, "comment")) {
		lua_pushstring (L, job_ptr->comment);
	} else if (!strcmp(name, "gres")) {
		lua_pushstring (L, job_ptr->gres);
	} else if (!strcmp(name, "job_id")) {
		lua_pushnumber (L, job_ptr->job_id);
	} else if (!strcmp(name, "job_state")) {
		lua_pushnumber (L, job_ptr->job_state);
	} else if (!strcmp(name, "licenses")) {
		lua_pushstring (L, job_ptr->licenses);
	} else if (!strcmp(name, "max_cpus")) {
		if (job_ptr->details)
			lua_pushnumber (L, job_ptr->details->max_cpus);
		else
			lua_pushnumber (L, 0);
	} else if (!strcmp(name, "max_nodes")) {
		if (job_ptr->details)
			lua_pushnumber (L, job_ptr->details->max_nodes);
		else
			lua_pushnumber (L, 0);
	} else if (!strcmp(name, "min_cpus")) {
		if (job_ptr->details)
			lua_pushnumber (L, job_ptr->details->min_cpus);
		else
			lua_pushnumber (L, 0);
	} else if (!strcmp(name, "min_nodes")) {
		if (job_ptr->details)
			lua_pushnumber (L, job_ptr->details->min_nodes);
		else
			lua_pushnumber (L, 0);
	} else if (!strcmp(name, "partition")) {
		lua_pushstring (L, job_ptr->partition);
	} else if (!strcmp(name, "time_limit")) {
		lua_pushnumber (L, job_ptr->time_limit);
	} else if (!strcmp(name, "time_min")) {
		lua_pushnumber (L, job_ptr->time_min);
	} else if (!strcmp(name, "wckey")) {
		lua_pushstring (L, job_ptr->wckey);
	} else {
		lua_pushnil (L);
	}

	return 1;
}

/* Get fields in the job request record on job submit or modify */
static int _get_job_req_field (lua_State *L)
{
	const struct job_descriptor *job_desc = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);

	if (job_desc == NULL) {
		error("_get_job_req_field: job_desc is NULL");
		lua_pushnil (L);
	} else if (!strcmp(name, "account")) {
		lua_pushstring (L, job_desc->account);
	} else if (!strcmp(name, "acctg_freq")) {
		lua_pushnumber (L, job_desc->acctg_freq);
	} else if (!strcmp(name, "begin_time")) {
		lua_pushnumber (L, job_desc->begin_time);
	} else if (!strcmp(name, "comment")) {
		lua_pushstring (L, job_desc->comment);
	} else if (!strcmp(name, "contiguous")) {
		lua_pushnumber (L, job_desc->contiguous);
	} else if (!strcmp(name, "cores_per_socket")) {
		lua_pushnumber (L, job_desc->cores_per_socket);
	} else if (!strcmp(name, "dependency")) {
		lua_pushstring (L, job_desc->dependency);
	} else if (!strcmp(name, "end_time")) {
		lua_pushnumber (L, job_desc->end_time);
	} else if (!strcmp(name, "exc_nodes")) {
		lua_pushstring (L, job_desc->exc_nodes);
	} else if (!strcmp(name, "features")) {
		lua_pushstring (L, job_desc->features);
	} else if (!strcmp(name, "gres")) {
		lua_pushstring (L, job_desc->gres);
	} else if (!strcmp(name, "group_id")) {
		lua_pushnumber (L, job_desc->group_id);
	} else if (!strcmp(name, "licenses")) {
		lua_pushstring (L, job_desc->licenses);
	} else if (!strcmp(name, "max_cpus")) {
		lua_pushnumber (L, job_desc->max_cpus);
	} else if (!strcmp(name, "max_nodes")) {
		lua_pushnumber (L, job_desc->max_nodes);
	} else if (!strcmp(name, "min_cpus")) {
		lua_pushnumber (L, job_desc->min_cpus);
	} else if (!strcmp(name, "min_nodes")) {
		lua_pushnumber (L, job_desc->min_nodes);
	} else if (!strcmp(name, "name")) {
		lua_pushstring (L, job_desc->name);
	} else if (!strcmp(name, "nice")) {
		lua_pushnumber (L, job_desc->nice);
	} else if (!strcmp(name, "ntasks_per_node")) {
		lua_pushnumber (L, job_desc->ntasks_per_node);
	} else if (!strcmp(name, "num_tasks")) {
		lua_pushnumber (L, job_desc->num_tasks);
	} else if (!strcmp(name, "partition")) {
		lua_pushstring (L, job_desc->partition);
	} else if (!strcmp(name, "pn_min_cpus")) {
		lua_pushnumber (L, job_desc->pn_min_cpus);
	} else if (!strcmp(name, "pn_min_memory")) {
		lua_pushnumber (L, job_desc->pn_min_memory);
	} else if (!strcmp(name, "pn_min_tmp_disk")) {
		lua_pushnumber (L, job_desc->pn_min_tmp_disk);
	} else if (!strcmp(name, "priority")) {
		lua_pushnumber (L, job_desc->priority);
	} else if (!strcmp(name, "qos")) {
		lua_pushstring (L, job_desc->qos);
	} else if (!strcmp(name, "req_nodes")) {
		lua_pushstring (L, job_desc->req_nodes);
	} else if (!strcmp(name, "requeue")) {
		lua_pushnumber (L, job_desc->requeue);
	} else if (!strcmp(name, "reservation")) {
		lua_pushstring (L, job_desc->reservation);
	} else if (!strcmp(name, "shared")) {
		lua_pushnumber (L, job_desc->shared);
	} else if (!strcmp(name, "sockets_per_node")) {
		lua_pushnumber (L, job_desc->sockets_per_node);
	} else if (!strcmp(name, "threads_per_core")) {
		lua_pushnumber (L, job_desc->threads_per_core);
	} else if (!strcmp(name, "time_limit")) {
		lua_pushnumber (L, job_desc->time_limit);
	} else if (!strcmp(name, "time_min")) {
		lua_pushnumber (L, job_desc->time_min);
	} else if (!strcmp(name, "user_id")) {
		lua_pushnumber (L, job_desc->user_id);
	} else if (!strcmp(name, "wckey")) {
		lua_pushstring (L, job_desc->wckey);
	} else {
		lua_pushnil (L);
	}

	return 1;
}

/* Set fields in the job request structure on job submit or modify */
static int _set_job_req_field (lua_State *L)
{
	struct job_descriptor *job_desc = lua_touserdata(L, 1);
	const char *name, *value_str;

	name = luaL_checkstring(L, 2);
	if (job_desc == NULL) {
		error("_set_job_req_field: job_desc is NULL");
	} else if (!strcmp(name, "account")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->account);
		if (strlen(value_str))
			job_desc->account = xstrdup(value_str);
	} else if (!strcmp(name, "acctg_freq")) {
		job_desc->acctg_freq = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "begin_time")) {
		job_desc->begin_time = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "comment")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->comment);
		if (strlen(value_str))
			job_desc->comment = xstrdup(value_str);
	} else if (!strcmp(name, "contiguous")) {
		job_desc->contiguous = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "cores_per_socket")) {
		job_desc->cores_per_socket = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "dependency")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->dependency);
		if (strlen(value_str))
			job_desc->dependency = xstrdup(value_str);
	} else if (!strcmp(name, "end_time")) {
		job_desc->end_time = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "exc_nodes")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->exc_nodes);
		if (strlen(value_str))
			job_desc->exc_nodes = xstrdup(value_str);
	} else if (!strcmp(name, "features")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->features);
		if (strlen(value_str))
			job_desc->features = xstrdup(value_str);
	} else if (!strcmp(name, "gres")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->gres);
		if (strlen(value_str))
			job_desc->gres = xstrdup(value_str);
	} else if (!strcmp(name, "licenses")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->licenses);
		if (strlen(value_str))
			job_desc->licenses = xstrdup(value_str);
	} else if (!strcmp(name, "max_cpus")) {
		job_desc->max_cpus = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "max_nodes")) {
		job_desc->max_nodes = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "min_cpus")) {
		job_desc->min_cpus = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "min_nodes")) {
		job_desc->min_nodes = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "name")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->name);
		if (strlen(value_str))
			job_desc->name = xstrdup(value_str);
	} else if (!strcmp(name, "nice")) {
		job_desc->nice = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "ntasks_per_node")) {
		job_desc->ntasks_per_node = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "num_tasks")) {
		job_desc->num_tasks = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "partition")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->partition);
		if (strlen(value_str))
			job_desc->partition = xstrdup(value_str);
	} else if (!strcmp(name, "pn_min_cpus")) {
		job_desc->pn_min_cpus = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "pn_min_memory")) {
		job_desc->pn_min_memory = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "pn_min_tmp_disk")) {
		job_desc->pn_min_tmp_disk = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "priority")) {
		job_desc->priority = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "qos")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->qos);
		if (strlen(value_str))
			job_desc->qos = xstrdup(value_str);
	} else if (!strcmp(name, "req_nodes")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->req_nodes);
		if (strlen(value_str))
			job_desc->req_nodes = xstrdup(value_str);
	} else if (!strcmp(name, "requeue")) {
		job_desc->requeue = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "reservation")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->reservation);
		if (strlen(value_str))
			job_desc->reservation = xstrdup(value_str);
	} else if (!strcmp(name, "shared")) {
		job_desc->shared = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "sockets_per_node")) {
		job_desc->sockets_per_node = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "threads_per_core")) {
		job_desc->threads_per_core = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "time_limit")) {
		job_desc->time_limit = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "time_min")) {
		job_desc->time_min = luaL_checknumber(L, 3);
	} else if (!strcmp(name, "wckey")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->wckey);
		if (strlen(value_str))
			job_desc->wckey = xstrdup(value_str);
	} else {
		error("_set_job_field: unrecognized field: %s", name);
	}

	return 0;
}

/* Get fields in an existing slurmctld partition record
 * NOTE: This is an incomplete list of partition record fields.
 * Add more as needed and send patches to slurm-dev@llnl.gov */
static int _get_part_rec_field (lua_State *L)
{
	const struct part_record *part_ptr = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);

	if (part_ptr == NULL) {
		error("_get_part_field: part_ptr is NULL");
		lua_pushnil (L);
	} else if (!strcmp(name, "flag_default")) {
		int is_default = 0;
		if (part_ptr->flags & PART_FLAG_DEFAULT)
			is_default = 1;
		lua_pushnumber (L, is_default);
	} else if (!strcmp(name, "flags")) {
		lua_pushnumber (L, part_ptr->flags);
	} else if (!strcmp(name, "max_nodes")) {
		lua_pushnumber (L, part_ptr->max_nodes);
	} else if (!strcmp(name, "max_nodes_orig")) {
		lua_pushnumber (L, part_ptr->max_nodes_orig);
	} else if (!strcmp(name, "max_time")) {
		lua_pushnumber (L, part_ptr->max_time);
	} else if (!strcmp(name, "min_nodes")) {
		lua_pushnumber (L, part_ptr->min_nodes);
	} else if (!strcmp(name, "min_nodes_orig")) {
		lua_pushnumber (L, part_ptr->min_nodes_orig);
	} else if (!strcmp(name, "name")) {
		lua_pushstring (L, part_ptr->name);
	} else if (!strcmp(name, "nodes")) {
		lua_pushstring (L, part_ptr->nodes);
	} else if (!strcmp(name, "priority")) {
		lua_pushnumber (L, part_ptr->priority);
	} else if (!strcmp(name, "state_up")) {
		lua_pushnumber (L, part_ptr->state_up);
	} else {
		lua_pushnil (L);
	}

	return 1;
}
#if 0
/* Filter before packing list of partitions */
	char *allow_groups;	/* comma delimited list of groups */
	uid_t *allow_uids;	/* zero terminated list of allowed users */
#endif

static void _register_lua_slurm_struct_functions (void)
{
	lua_pushcfunction(L, _get_job_rec_field);
	lua_setglobal(L, "_get_job_rec_field");
	lua_pushcfunction(L, _get_job_req_field);
	lua_setglobal(L, "_get_job_req_field");
	lua_pushcfunction(L, _set_job_req_field);
	lua_setglobal(L, "_set_job_req_field");
	lua_pushcfunction(L, _get_part_rec_field);
	lua_setglobal(L, "_get_part_rec_field");
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
		if (_check_lua_script_function(fns[i]) < 0) {
			error("job_submit/lua: %s: "
			      "missing required function %s",
			      lua_script_path, fns[i]);
			rc = -1;
		}
	} while (fns[++i]);

	return (rc);
}

static bool _user_can_use_part(uint32_t user_id, uint32_t submit_uid,
			       struct part_record *part_ptr)
{
	int i;

	if (user_id == 0) {
		if (part_ptr->flags & PART_FLAG_NO_ROOT)
			return false;
		return true;
	}

	if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && (submit_uid != 0))
		return false;

	if (part_ptr->allow_uids == NULL)
		return true;	/* No user ID filters */

	for (i=0; part_ptr->allow_uids[i]; i++) {
		if (user_id == part_ptr->allow_uids[i])
			return true;
	}
	return false;
}

static void _push_partition_list(uint32_t user_id, uint32_t submit_uid)
{
	int i = 1;
	ListIterator part_iterator;
	struct part_record *part_ptr;

	lua_newtable(L);
	part_iterator = list_iterator_create(part_list);
	if (!part_iterator)
		fatal("list_iterator_create malloc");
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		if (!_user_can_use_part(user_id, submit_uid, part_ptr))
			continue;
		lua_pushlightuserdata (L, part_ptr);
		lua_rawseti(L, -2, i++);
	}
	list_iterator_destroy(part_iterator);
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
	 *  logging and slurm structure read/write functions
	 */
	_register_lua_slurm_output_functions();
	_register_lua_slurm_struct_functions();

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
extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid)
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

	lua_pushlightuserdata (L, job_desc);
	_push_partition_list(job_desc->user_id, submit_uid);
	if (lua_pcall (L, 2, 0, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring (L, -1));
	} else
		rc = SLURM_SUCCESS;

out:	slurm_mutex_unlock (&lua_lock);
	return rc;
}

/* Lua script hook called for "modify job" event. */
extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
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

	lua_pushlightuserdata (L, job_desc);
	lua_pushlightuserdata (L, job_ptr);
	_push_partition_list(job_ptr->user_id, submit_uid);
	if (lua_pcall (L, 3, 0, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring (L, -1));
	} else
		rc = SLURM_SUCCESS;

out:	slurm_mutex_unlock (&lua_lock);
	return rc;
}
