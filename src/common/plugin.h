/*****************************************************************************\
 * plugin.h - plugin abstraction and operations.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windlay <jwindley@lnxi.com>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef __GENERIC_PLUGIN_H__
#define __GENERIC_PLUGIN_H__

#include <inttypes.h>
#include <sys/types.h>
#include <dirent.h>

#include "src/common/list.h"
#include "slurm/slurm_errno.h"

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

typedef struct {
	plugin_handle_t	cur_plugin;
	void *plugin_list;
	char *type;
} plugin_context_t;


#define PLUGIN_INVALID_HANDLE ((void*)0)

typedef enum {
	EPLUGIN_SUCCESS = 0,     /* Success                             */
	EPLUGIN_NOTFOUND,        /* Plugin file does not exist          */
	EPLUGIN_ACCESS_ERROR,    /* Access denied                       */
	EPLUGIN_DLOPEN_FAILED,   /* Dlopen not successful               */
	EPLUGIN_INIT_FAILED,     /* Plugin's init() callback failed     */
	EPLUGIN_MISSING_NAME,    /* plugin_name/type/version missing    */
	EPLUGIN_BAD_VERSION,     /* incompatible plugin version         */
} plugin_err_t;

const char *plugin_strerror(plugin_err_t err);

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
 * Returns a plugin_err_t.
 */
extern plugin_err_t plugin_peek(const char *fq_path,
				char *plugin_type,
				const size_t type_len,
				uint32_t *plugin_version);


/*
 * Simplest way to get a plugin -- load it from a file.
 *
 * pph     - Pointer to a plugin handle
 * fq_path - the fully-qualified pathname (i.e., from root) to
 * the plugin to load.
 *
 * Returns EPLUGIN_SUCCESS on success, and an plugin_err_t error
 * code on failure.
 *
 * The plugin's initialization code will be executed prior
 * to this function's return.
 */
plugin_err_t plugin_load_from_file(plugin_handle_t *pph, const char *fq_path);

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


/*
 * Create a priority context
 * plugin_type - IN - name of plugin major type (select)
 * uler_type - IN - name of plugin minor type (linear)
 * ptrs[] - IN/OUT - an array of pointers into which the addresses of the
 *        respective symbols should be placed.  ptrs[i] will receive
 *        the address of names[i].
 * names[] - IN - an argv-like array of symbol names to resolve.
 * names_size - IN - size of names[] (sizeof(names))
 *
 * Returns plugin_context_t on success of NULL if failed.  On success
 * ptrs[] is filled in with the symbols from names[].
 *
 * Free memory with plugin_context_destroy
 */
extern plugin_context_t *plugin_context_create(
	const char *plugin_type, const char *uler_type,
	void *ptrs[], const char *names[], size_t names_size);

/*
 * Destroy a context created from plugin_context_create.
 */
extern int plugin_context_destroy(plugin_context_t *c);

/*
 * Return a list of plugin names that match the given type.
 *
 * IN plugin_type - Type of plugin to search for in the plugin_dir.
 * RET list of plugin names, NULL if none found.
 */
extern List plugin_get_plugins_of_type(char *plugin_type);

#endif /*__GENERIC_PLUGIN_H__*/
