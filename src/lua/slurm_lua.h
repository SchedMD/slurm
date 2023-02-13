/*****************************************************************************\
 *  slurm_lua.h - Lua integration common functions
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

#ifndef _SLURM_LUA_H
#define _SLURM_LUA_H

#ifdef HAVE_LUA

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "src/slurmctld/slurmctld.h"
#include "slurm/slurm_errno.h"

/* Generic stack dump function for debugging purposes */
extern void slurm_lua_stack_dump(const char *plugin,
				 char *header, lua_State *L);

/*
 * This function loads a new lua state object.
 *
 * The new lua state object will be initialized and assigned to *L depending
 * on its mtime vs *load_time and whether the new script is succesfully loaded.
 * If it cannot load *L won't be touched and SLURM_ERROR or SUCCESS will be
 * returned depending on if *L was NULL or the old script can still be in use.
 *
 * Parameters:
 * L (in/out)   - current lua state object, should be NULL on first call
 * plugin (in) - string identifying the calling plugin, e.g. "job_submit/lua"
 * script_path (in) - path to script file
 * req_fxns (in) - NULL terminated array of functions that must exist in the
 *                 script
 * load_time (in/out) - mtime of script from the curr lua state object
 *
 * Returns:
 * SLURM_SUCCESS - if a correct Lua object is set.
 * SLURM_ERROR - an error occurred, and there's no lua state loaded.
 */
extern int slurm_lua_loadscript(lua_State **L, const char *plugin,
				const char *script_path,
				const char **req_fxns,
				time_t *load_time,
				void (*local_options)(lua_State *L));

extern void slurm_lua_table_register(lua_State *L, const char *libname,
				     const luaL_Reg *l);

/*
 * Get fields in an existing slurmctld job record.
 *
 * This is an incomplete list of job record fields. Add more as needed and
 * send patches to slurm-dev@schedmd.com.
 */
extern int slurm_lua_job_record_field(lua_State *L, const job_record_t *job_ptr,
				      const char *name);

extern int slurm_lua_isinteger(lua_State *L, int index);

#else
# define LUA_VERSION_NUM 0
#endif

/*
 *  Init function to dlopen() the appropriate Lua libraries, and
 *  ensure the lua version matches what we compiled against along with other
 *  init things.
 */
extern int slurm_lua_init(void);

/*
 * Close down the lib, free memory and such.
 */
extern void slurm_lua_fini(void);

#endif
