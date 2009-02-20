/*****************************************************************************\
 * plugin.h - plugin abstraction and operations.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windlay <jwindley@lnxi.com>.
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

#ifndef __GENERIC_PLUGIN_H__
#define __GENERIC_PLUGIN_H__

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif
#else /* ! HAVE_CONFIG_H_ */
#  include <inttypes.h>
#endif /* HAVE_CONFIG_H */

#include <slurm/slurm_errno.h>

/*
 * These symbols are required to be defined in any plugin managed by
 * this code.  Use these macros instead of string literals in order to
 * avoid typographical errors.
 *
 * Their meanings are described in the sample plugin.
 */
#define PLUGIN_NAME		"plugin_name"
#define PLUGIN_TYPE		"plugin_type"
#define PLUGIN_VERSION		"plugin_version"

/*
 * Opaque type for plugin handle.  Most plugin operations will want
 * of these.
 *
 * Currently there is no user-space memory associated with the plugin
 * handle other than the pointer with which it is implemented.  While
 * allowing a handle to pass out of scope without explicit destruction
 * will not leak user memory, it may leave the plugin loaded in memory.
 */
typedef void *plugin_handle_t;

#define PLUGIN_INVALID_HANDLE ((void*)0)

/*
 * "Peek" into a plugin to discover its type and version.  This does
 * not run the plugin's init() or fini() functions (as defined in this
 * API) but the _init() and _fini() functions (defined by the underlying
 * OS) are run.
 *
 * fq_path - fully-qualified pathname to the plugin.
 * plugin_type - a buffer in which to store the plugin type.  May be
 *	NULL to indicate that the caller is not interested in the
 *	plugin type.
 * type_len - the number of bytes available in plugin_type.  The type
 *	will be zero-terminated if space permits.
 * plugin_version - pointer to place to store the plugin version.  May
 *	be NULL to indicate that the caller is not interested in the
 *	plugin version.
 *
 * Returns a SLURM errno.
 */
int plugin_peek( const char *fq_path,
		 char *plugin_type,
		 const size_t type_len,
		 uint32_t *plugin_version );


/*
 * Simplest way to get a plugin -- load it from a file.
 *
 * fq_path - the fully-qualified pathname (i.e., from root) to
 * the plugin to load.
 *
 * Returns a handle if successful, or NULL if not.
 *
 * The plugin's initialization code will be executed prior
 * to this function's return.
 */
plugin_handle_t plugin_load_from_file( const char *fq_path );

/*
 * load plugin and link hooks.
 *
 * type_name - plugin type as entered into slurm.conf.
 *
 * n_syms - the number of symbols in names[].
 * names[] - an argv-like array of symbol names to resolve.
 * ptrs[] - an array of pointers into which the addresses of the respective
 * 	symbols should be placed.  ptrs[i] will receive the address of
 *	names[i].
 *
 * Returns a handle if successful, or NULL if not.
 *
 * The plugin's initialization code will be executed prior
 * to this function's return.
 */
plugin_handle_t plugin_load_and_link(const char *type_name, int n_syms,
				     const char *names[], void *ptrs[]);

/*
 * Unload a plugin from memory.
 */
void plugin_unload( plugin_handle_t plug );

/*
 * Get the address of a named symbol in the plugin.
 *
 * Returns the address of the symbol or NULL if not found.
 */
void *plugin_get_sym( plugin_handle_t plug, const char *name );

/*
 * Access functions to get the name, type, and version of a plugin
 * from the plugin itself.
 */
const char *plugin_get_name( plugin_handle_t plug );
const char *plugin_get_type( plugin_handle_t plug );
uint32_t plugin_get_version( plugin_handle_t plug );

/*
 * Get the addresses of several symbols from the plugin at once.
 *
 * n_syms - the number of symbols in names[].
 * names[] - an argv-like array of symbol names to resolve.
 * ptrs[] - an array of pointers into which the addresses of the respective
 * 	symbols should be placed.  ptrs[i] will receive the address of
 *	names[i].
 *
 * Returns the number of symbols successfully resolved.  Pointers whose
 * associated symbol name was not found will be set to NULL.
 */
int plugin_get_syms( plugin_handle_t plug,
		     int n_syms,
		     const char *names[],
		     void *ptrs[] );

#endif /*__GENERIC_PLUGIN_H__*/
