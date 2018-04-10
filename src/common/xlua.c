/*****************************************************************************\
 *  xlua.h - Lua integration common functions
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
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

#include "src/common/xlua.h"

#ifdef HAVE_LUA
# include <lua.h>
#else
# define LUA_VERSION_NUM 0
#endif
/*
 *  Common function to dlopen() the appropriate Lua libraries, and
 *   ensure the lua version matches what we compiled against.
 */
int xlua_dlopen(void)
{
	/*
	 *  Need to dlopen() liblua.so with RTLD_GLOBAL in order to
	 *   ensure symbols from liblua are available to libs opened
	 *   by any lua scripts.
	 */
	if (!LUA_VERSION_NUM) {
		fatal("Slurm wasn't configured against any LUA lib but you are trying to use it like it was.  Please check config.log and reconfigure against liblua.  Make sure you have lua devel installed.");
	} else if (!dlopen("liblua.so",       RTLD_NOW | RTLD_GLOBAL) &&
#if LUA_VERSION_NUM == 503
		   !dlopen("liblua-5.3.so",   RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.3.so",    RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.3.so.0",  RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua.so.5.3",   RTLD_NOW | RTLD_GLOBAL)
#elif LUA_VERSION_NUM == 502
		   !dlopen("liblua-5.2.so",   RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.2.so",    RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.2.so.0",  RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua.so.5.2",   RTLD_NOW | RTLD_GLOBAL)
#else
		   !dlopen("liblua-5.1.so",   RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.1.so",    RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua5.1.so.0",  RTLD_NOW | RTLD_GLOBAL) &&
		   !dlopen("liblua.so.5.1",   RTLD_NOW | RTLD_GLOBAL)
#endif
		) {
		return error("Failed to open liblua.so: %s", dlerror());
	}
	return SLURM_SUCCESS;
}
