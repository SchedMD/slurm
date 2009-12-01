/*****************************************************************************\
 *  proctrack_lua.c
 *****************************************************************************
 *  Copyright (C) 2009 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
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

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "src/common/log.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"


const char plugin_name[]            = "LUA proctrack module";
const char plugin_type[]            = "proctrack/lua";
const uint32_t plugin_version       = 90;

static const char lua_script_path[] = DEFAULT_SCRIPT_DIR "/proctrack.lua";
static lua_State *L = NULL;

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

static int lua_register_slurm_output_functions ()
{
	/*
	 *  Register slurm output functions in a global "slurm" table
	 */
	lua_newtable (L);
	luaL_register (L, NULL, slurm_functions);
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
static int check_lua_script_functions ()
{
	int rc = 0;
	int i;
	const char *fns[] = {
		"slurm_container_create",
		"slurm_container_add",
		"slurm_container_signal",
		"slurm_container_destroy",
		"slurm_container_find",
		"slurm_container_has_pid",
		"slurm_container_wait",
		"slurm_container_get_pids",
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

int init (void)
{
	int rc = SLURM_SUCCESS;

	/*
	 *  Need to dlopen() liblua.so with RTLD_GLOBAL in order to
	 *   ensure symbols from liblua are available to libs opened
	 *   by any lua scripts.
	 */
	if (!dlopen ("liblua.so", RTLD_NOW | RTLD_GLOBAL)) {
		return (error ("Failed to open liblua.so: %s", dlerror()));
	}

	/*
	 *  Initilize lua
	 */
	L = luaL_newstate ();
	luaL_openlibs (L);
	if (luaL_loadfile (L, lua_script_path))
		return error ("lua: %s: %s", lua_script_path, lua_tostring (L, -1));

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
static int lua_job_table_create (slurmd_job_t *job)
{
	lua_newtable (L);

	lua_pushnumber (L, job->jobid);
	lua_setfield (L, -2, "jobid");
	lua_pushnumber (L, job->stepid);
	lua_setfield (L, -2, "stepid");
	lua_pushnumber (L, job->nodeid);
	lua_setfield (L, -2, "nodeid");
	lua_pushnumber (L, job->ntasks);
	lua_setfield (L, -2, "ntasks");
	lua_pushnumber (L, job->nprocs);
	lua_setfield (L, -2, "nprocs");
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

	lua_pushstring (L, job->alloc_cores ? job->alloc_cores : "");
	lua_setfield (L, -2, "CPUs");
	lua_pushstring (L, job->cwd ? job->cwd : "");
	lua_setfield (L, -2, "cwd");

	return (0);
}

int slurm_container_create (slurmd_job_t *job)
{
	double id;
	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal (L, "slurm_container_create");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_job_table_create (job);
	if (lua_pcall (L, 1, 1, 0) != 0)
		return error ("proctrack/lua: %s: slurm_container_create: %s",
			      lua_script_path, lua_tostring (L, -1));

	/*
	 *  Get the container id off the stack:
	 */
	if (lua_isnil (L, -1)) {
		lua_pop (L, -1);
		return (-1);
	}
	id = lua_tonumber (L, -1);
	job->cont_id = id;
	info ("job->cont_id = %u (%.0f) \n", job->cont_id, id);
	lua_pop (L, -1);
	return (0);
}

int slurm_container_add (slurmd_job_t *job, pid_t pid)
{
	int rc;

	lua_getglobal (L, "slurm_container_add");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_job_table_create (job);
	lua_pushnumber (L, job->cont_id);
	lua_pushnumber (L, pid);

	if (lua_pcall (L, 3, 1, 0) != 0)
		return error ("running lua function 'slurm_container_add': %s",
			      lua_tostring (L, -1));

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
	return (rc);
}

int slurm_container_signal (uint32_t id, int sig)
{
	int rc;
	lua_getglobal (L, "slurm_container_signal");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, id);
	lua_pushnumber (L, sig);

	if (lua_pcall (L, 2, 1, 0) != 0)
		return error ("running lua function 'slurm_container_signal': %s",
			      lua_tostring (L, -1));

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
	return (rc);
}

int slurm_container_destroy (uint32_t id)
{
	int rc;
	lua_getglobal (L, "slurm_container_destroy");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, id);

	if (lua_pcall (L, 1, 1, 0) != 0)
		return error ("running lua function 'slurm_container_destroy': %s",
			      lua_tostring (L, -1));

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
	return (rc);
}

uint32_t slurm_container_find (pid_t pid)
{
	uint32_t id;
	lua_getglobal (L, "slurm_container_find");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, pid);

	if (lua_pcall (L, 1, 1, 0) != 0)
		return error ("running lua function 'slurm_container_find': %s",
			      lua_tostring (L, -1));

	id = (uint32_t) lua_tonumber (L, -1);
	lua_pop (L, -1);
	return (id);
}

bool slurm_container_has_pid (uint32_t id, pid_t pid)
{
	int rc;
	lua_getglobal (L, "slurm_container_has_pid");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, id);
	lua_pushnumber (L, pid);

	if (lua_pcall (L, 2, 1, 0) != 0)
		return error ("running lua function 'slurm_container_has_pid': %s",
			      lua_tostring (L, -1));

	rc = lua_toboolean (L, -1);
	lua_pop (L, -1);
	return (rc == 1);
}

int slurm_container_wait (uint32_t id)
{
	int rc;
	lua_getglobal (L, "slurm_container_wait");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, id);

	if (lua_pcall (L, 1, 1, 0) != 0)
		return error ("running lua function 'slurm_container_wait': %s",
			      lua_tostring (L, -1));

	rc = lua_tonumber (L, -1);
	lua_pop (L, -1);
	return (rc);
}

int slurm_container_get_pids (uint32_t cont_id, pid_t **pids, int *npids)
{
	int i = 0;
	int t = 0;
	pid_t *p;

	lua_getglobal (L, "slurm_container_get_pids");
	if (lua_isnil (L, -1))
		return SLURM_FAILURE;

	lua_pushnumber (L, cont_id);

	if (lua_pcall (L, 1, 1, 0) != 0)
		return (error ("%s: %s: %s",
			       "proctrack/lua",
			       __func__,
			       lua_tostring (L, -1)));

	/*
	 *   list of PIDs should be returned in a table from the lua
	 *    script. If a table wasn't returned then generate an error:
	 */
	if (!lua_istable(L, -1))
		return (error ("%s: %s: function should return a table",
			       "proctrack/lua",
			       __func__));

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
	return SLURM_SUCCESS;
}

