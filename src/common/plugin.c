/*****************************************************************************\
 * plugin.h - plugin architecture implementation.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by AUTHOR <AUTHOR@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <stdio.h>
#include <dlfcn.h>	/* don't know if there's an autoconf for this. */

#include "src/common/plugin.h"


plugin_handle_t
plugin_load_from_file( const char *fq_path )
{
	plugin_handle_t plug;

	/* Try to open the shared object. */
	plug = dlopen( fq_path, RTLD_NOW );
	if ( plug == NULL ) {
		return PLUGIN_INVALID_HANDLE;
	}

	/* Now see if our required symbols are defined. */
	if ( ( dlsym( plug, PLUGIN_NAME ) == NULL ) ||
	     ( dlsym( plug, PLUGIN_TYPE ) == NULL ) ||
	     ( dlsym( plug, PLUGIN_VERSION ) == NULL ) ) {
		return PLUGIN_INVALID_HANDLE;
	}

	return plug;
}


/*
 * Must test plugin validity before doing dlopen() and dlsym()
 * operations because some implementations of these functions
 * crash if the library handle is not valid.
 */

void
plugin_unload( plugin_handle_t plug )
{
	if ( plug != PLUGIN_INVALID_HANDLE ) (void) dlclose( plug );
}


void *
plugin_get_sym( plugin_handle_t plug, const char *name )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return dlsym( plug, name );
	else
		return NULL;
}

const char *
plugin_get_name( plugin_handle_t plug )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return (const char *) dlsym( plug, PLUGIN_NAME );
	else
		return NULL;
}

const char *
plugin_get_type( plugin_handle_t plug )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return (const char *) dlsym( plug, PLUGIN_TYPE );
	else
		return NULL;
}

uint32_t
plugin_get_version( plugin_handle_t plug )
{
	uint32_t *ptr;

	if ( plug == PLUGIN_INVALID_HANDLE ) return 0;	
	ptr = (uint32_t *) dlsym( plug, PLUGIN_VERSION );
	return ptr ? *ptr : 0;
}

int
plugin_get_syms( plugin_handle_t plug,
				 int n_syms,
				 const char *names[],
				 void *ptrs[] )
{
	int i, count;

	count = 0;
	for ( i = 0; i < n_syms; ++i ) {
		ptrs[ i ] = dlsym( plug, names[ i ] );
		if ( ptrs[ i ] ) ++count;
	}

	return count;
}

