/*****************************************************************************\
 *  proctrack_lua.c
 *****************************************************************************
 *  Copyright (C) 2009 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"


const char plugin_name[]            = "LUA proctrack module";
const char plugin_type[]            = "proctrack/lua";
const uint32_t plugin_version       = 91;

static const char lua_script_path[] = DEFAULT_SCRIPT_DIR "/proctrack.lua";
static lua_State *L = NULL;

/*
 *  Mutex for protecting multi-threaded access to this plugin.
 *   (Only 1 thread at a time should be in here)
 */
#ifdef WITH_PTHREADS
static pthread_mutex_t lua_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 *  Lua interface to SLURM log facility:
 */
static int l_log_msg (lua_State *L)
{
	const char *prefix  = "proctrack.lua";
	int        level    = 0;
	const char *msg;

	/*
	 *  Optional numeric prefix indicating the log level
	 *   of the message.
	 */

	/*
	 *  Pop message off the lua stack
	 */
	msg = lua_tostring (L, -1);
	lua_pop (L, 1);
	/*
	 *  Pop level off stack:
	 */
	level = (int) lua_tonumber (L, -1);
	lua_pop (L, 1);

	/*
	 *  Call appropriate slurm log function based on log-level argument
	 */
	if (level > 3)
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

static int l_log_error (lua_State *L)
{
	const char *prefix  = "proctrack.lua";
	const char *msg     = lua_tostring (L, -1);
	error ("%s: %s", prefix, msg);
	return (0);
}

static const struct luaL_Reg slurm_functions [] = {
	{ "log",   l_log_msg   },
	{ "error", l_log_error },
	{ NULL,    NULL        }
};

static int lua_register_slurm_output_functions (void)
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
	return 0;
}

/*
 *  check that global symbol [name] in lua script is a function
 */
static int check_lua_script_function (const char *name)
{
	int rc = 0;
	lua_getglobal (L, name);
	if (!lua_isfunction (L, -1))
		rc = -1;
	lua_pop (L, -1);
	return (rc);
}

/*
 *   Verify all required fuctions are defined in the proctrack/lua script
 */
static int check_lua_script_functions (void)
{
	int rc = 0;
	int i;
	const char *fns[] = {
		"proctrack_g_create",
		"proctrack_g_add",
		"proctrack_g_signal",
		"proctrack_g_destroy",
		"proctrack_g_find",
		"proctrack_g_has_pid",
		"proctrack_g_wait",
		"proctrack_g_get_pids",
		NULL
	};

	i = 0;
	do {
		if (check_lua_script_function (fns[i]) < 0) {
			error ("proctrack/lua: %s: missing required function %s",
			       lua_script_path, fns[i]);
			rc = -1;
		}
	} while (fns[++i]);

	return (rc);
}

/*
 *  NOTE: The init callback should never be called multiple times,
 *   let alone called from multiple threads. Therefore, locking
 *   is unecessary here.
 */
int init (void)
{
	int rc = SLURM_SUCCESS;

	/*
	 *  Need to dlopen() liblua.so with RTLD_GLOBAL in order to
	 *   ensure symbols from liblua are available to libs opened
	 *   by any lua scripts.
	 */
	if (!dlopen("liblua.so",      RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua-5.2.so",   RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua5.2.so",    RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua5.2.so.0",  RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua-5.1.so",   RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua5.1.so",    RTLD_NOW | RTLD_GLOBAL) &&
	    !dlopen("liblua5.1.so.0",  RTLD_NOW | RTLD_GLOBAL)) {
		return (error("Failed to open liblua.so: %s", dlerror()));
	}

	/*
	 *  Initilize lua
	 */
	L = luaL_newstate ();
	luaL_openlibs (L);
	if (luaL_loadfile (L, lua_script_path))
		return error ("lua: %s: %s", lua_script_path,
			      lua_tostring (L, -1));

	/*
	 *  Register slurm.log and slurm.error functions in lua state:
	 */
	lua_register_slurm_output_functions ();

	/*
	 *  Run the user script:
	 */
	if (lua_pcall (L, 0, 1, 0) != 0)
		return error ("proctrack/lua: %s: %s",
			      lua_script_path, lua_tostring (L, -1));

	/*
	 *  Get any return code from the lua script
	 */
	rc = (int) lua_tonumber (L, -1);
	lua_pop (L, 1);
	if (rc != SLURM_SUCCESS)
		return rc;

	/*
	 *  Check for required lua script functions:
	 */
	return (check_lua_script_functions ());
}

int fini (void)
{
	lua_close (L);
	return SLURM_SUCCESS;
}


/*
 *  Create lua 'job' table and leave it on the (lua) stack.
 */
static int lua_job_table_create (stepd_step_rec_t *job)
{
	lua_newtable (L);

	lua_pushnumber (L, job->jobid);
	lua_setfield (L, -2, "jobid");
	lua_pushnumber (L, job->stepid);
	lua_setfield (L, -2, "stepid");
	lua_pushnumber (L, job->nodeid);
	lua_setfield (L, -2, "nodeid");
	lua_pushnumber (L, job->node_tasks);
	lua_setfield (L, -2, "node_tasks");
	lua_pushnumber (L, job->ntasks);
	lua_setfield (L, -2, "ntasks");
	lua_pushnumber (L, job->cpus_per_task);
	lua_setfield (L, -2, "cpus_per_task");
	lua_pushnumber (L, job->nnodes);
	lua_setfield (L, -2, "nnodes");
	lua_pushnumber (L, job->uid);
	lua_setfield (L, -2, "uid");
	lua_pushnumber (L, job->gid);
	lua_setfield (L, -2, "gid");
	lua_pushnumber (L, job->pgid);
	lua_setfield (L, -2, "pgid");
	lua_pushnumber (L, job->jmgr_pid);
	lua_setfield (L, -2, "jmgr_pid");
	lua_pushnumber (L, job->job_mem);
	lua_setfield (L, -2, "mem");

	lua_pushstring (L, job->job_alloc_cores ? job->job_alloc_cores : "");
	lua_setfield (L, -2, "JobCPUs");
	lua_pushstring (L, job->step_alloc_cores ? job->step_alloc_cores : "");
	lua_setfield (L, -2, "StepCPUs");
	lua_pushstring (L, job->cwd ? job->cwd : "");
	lua_setfield (L, -2, "cwd");

	return (0);
}

int proctrack_p_create (stepd_step_rec_t *job)
{
	int rc = SLURM_ERROR;
	double id;

	slurm_mutex_lock (&lua_lock);

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal (L, "proctrack_g_create");
	if (lua_isnil (L, -1))
		goto out;

	lua_job_table_create (job);
	if (lua_pcall (L, 1, 1, 0) != 0) {
		error ("proctrack/lua: %s: proctrack_p_create: %s",
		       lua_script_path, lua_tostring (L, -1));
		goto out;
	}

	/*
	 *  Get the container id off the stack:
	 */
	if (lua_isnil (L, -1)) {
		error ("proctrack/lua: "
		       "proctrack_p_create did not return id");
		lua_pop (L, -1);
		goto out;
	}

	id = lua_tonumber (L, -1);
	job->cont_id = (uint64_t) id;
	info ("job->cont_id = %"PRIu64" (%.0f)", job->cont_id, id);
	lua_pop (L, -1);

	rc = SLURM_SUCCESS;
out:
	slurm_mutex_unlock (&lua_lock);
	return rc;
}

int proctrack_p_add (stepd_step_rec_t *job, pid_t pid)
{
	int rc = SLURM_ERROR;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_add");
	if (lua_isnil (L, -1))
		goto out;

	lua_job_table_create (job);
	lua_pushnumber (L, job->cont_id);
	lua_pushnumber (L, pid);

	if (lua_pcall (L, 3, 1, 0) != 0) {
		error ("running lua function "
		       "'proctrack_p_add': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
out:
	slurm_mutex_unlock (&lua_lock);
	return (rc);
}

int proctrack_p_signal (uint64_t id, int sig)
{
	int rc = SLURM_ERROR;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_signal");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, id);
	lua_pushnumber (L, sig);

	if (lua_pcall (L, 2, 1, 0) != 0) {
		error ("running lua function "
		       "'proctrack_p_signal': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
out:
	slurm_mutex_unlock (&lua_lock);
	return (rc);
}

int proctrack_p_destroy (uint64_t id)
{
	int rc = SLURM_ERROR;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_destroy");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, id);

	if (lua_pcall (L, 1, 1, 0) != 0) {
		error ("running lua function "
		       "'proctrack_p_destroy': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);

out:
	slurm_mutex_unlock (&lua_lock);
	return (rc);
}

uint64_t proctrack_p_find (pid_t pid)
{
	uint64_t id = (uint64_t) SLURM_ERROR;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_find");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, pid);

	if (lua_pcall (L, 1, 1, 0) != 0) {
		error ("running lua function 'proctrack_p_find': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	id = (uint64_t) lua_tonumber (L, -1);
	lua_pop (L, -1);

out:
	slurm_mutex_lock (&lua_lock);
	return (id);
}

bool proctrack_p_has_pid (uint64_t id, pid_t pid)
{
	int rc = 0;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_has_pid");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, id);
	lua_pushnumber (L, pid);

	if (lua_pcall (L, 2, 1, 0) != 0) {
		error ("running lua function "
		       "'proctrack_p_has_pid': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	rc = lua_toboolean (L, -1);
	lua_pop (L, -1);

out:
	slurm_mutex_unlock (&lua_lock);
	return (rc == 1);
}

int proctrack_p_wait (uint64_t id)
{
	int rc = SLURM_ERROR;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_wait");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, id);

	if (lua_pcall (L, 1, 1, 0) != 0) {
		error ("running lua function 'proctrack_p_wait': %s",
		       lua_tostring (L, -1));
		goto out;
	}

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
out:
	slurm_mutex_unlock (&lua_lock);
	return (rc);
}

int proctrack_p_get_pids (uint64_t cont_id, pid_t **pids, int *npids)
{
	int rc = SLURM_ERROR;
	int i = 0;
	int t = 0;
	pid_t *p;

	*npids = 0;

	slurm_mutex_lock (&lua_lock);

	lua_getglobal (L, "proctrack_g_get_pids");
	if (lua_isnil (L, -1))
		goto out;

	lua_pushnumber (L, cont_id);

	if (lua_pcall (L, 1, 1, 0) != 0) {
		error ("%s: %s: %s",
		       "proctrack/lua",
		       __func__,
		       lua_tostring (L, -1));
		goto out;
	}

	/*
	 *   list of PIDs should be returned in a table from the lua
	 *    script. If a table wasn't returned then generate an error:
	 */
	if (!lua_istable(L, -1)) {
		error ("%s: %s: function should return a table",
		       "proctrack/lua",
		       __func__);
		goto out;
	}

	/*
	 *  Save absolute position of table in lua stack
	 */
	t = lua_gettop (L);

	/*
	 *  Get table size and create array for slurm
	 */
	*npids = lua_objlen (L, t);
	p = (pid_t *) xmalloc (*npids * sizeof (pid_t));

	/*
	 *  Traverse table/array at position t on the stack:
	 */
	lua_pushnil (L);
	while (lua_next (L, t)) {
		p [i++] = lua_tonumber (L, -1);
		/*
		 *  pop value off stack, leave key for lua_next()
		 */
		lua_pop (L, 1);
	}
	lua_pop (L, 1);

	*pids = p;

	rc = SLURM_SUCCESS;
out:
	slurm_mutex_unlock (&lua_lock);
	return rc;
}

