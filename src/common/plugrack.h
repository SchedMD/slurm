/*****************************************************************************\
 * plugrack.h - an intelligent container for plugins
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
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

#ifndef __PLUGRACK_H__
#define __PLUGRACK_H__

#include <sys/types.h>

#include "src/common/plugin.h"
#include "src/common/list.h"

/* Opaque type for plugin rack. */
typedef struct _plugrack * plugrack_t;

/*
 * Returns a new plugin rack object on success and NULL on failure.
 */
plugrack_t plugrack_create( void );

/*
 * Destroy a plugin rack.  All the associated plugins are unloaded and
 * all associated memory is deallocated.
 *
 * Returns a SLURM errno.
 */
int plugrack_destroy( plugrack_t rack );

/*
 * Set the major type of the plugins for this rack.  This affects
 * subsequent calls to add plugins from files.
 *
 * Pass NULL to disable typing in plugins handled by this rack.
 * This is the default.
 *
 * Returns a SLURM errno.
 */
int plugrack_set_major_type( plugrack_t rack, const char *type );

/*
 * Paranoia settings.  OR these together, if desired.
 *
 * _DIR_OWN - verify that the directory containing the plugin is owned
 * by a certain user.
 * _DIR_WRITABLE - verify that the directory containing the plugin is
 * not writable by anyone except its owner.
 * _FILE_OWN - verify that the plugin is owned by a certain user.
 * _FILE_WRITABLE - verify that the plugin is not writable by anyone
 * except its onwer.
 */
#define PLUGRACK_PARANOIA_NONE			0x00
#define PLUGRACK_PARANOIA_DIR_OWN		0x01
#define PLUGRACK_PARANOIA_DIR_WRITABLE		0x02
#define PLUGRACK_PARANOIA_FILE_OWN		0x04
#define PLUGRACK_PARANOIA_FILE_WRITABLE		0x08

/*
 * Indicate the manner in which the rack should be paranoid about
 * accepting plugins.
 *
 * paranoia_flags is an ORed combination of the flags listed above.
 * They do not combine across separate calls; the flags must be fully
 * specified at each call.
 *
 * The paranoia setting affects only subsequent attempts to place
 * plugins in the rack.
 *
 * If the flag parameter specifies ownership checking, "uid" gives the
 * numerical user ID of the authorized owner of the plugin and the
 * directory where it resides.  If no ownership checking is requested,
 * this parameter is ignored.
 *
 * Returns a SLURM errno.
 */
int plugrack_set_paranoia( plugrack_t rack,
			   const uint32_t paranoia_flags,
			   const uid_t uid );

/*
 * Add plugins to a rack by scanning the given directory.  If a
 * type has been set for this rack, only those plugins whose major type
 * matches the rack's type will be loaded.  If a rack's paranoia factors
 * have been set, they are applied to files considered candidates for
 * plugins.  Plugins that fail the paranoid examination are not loaded.
 *
 * Returns a SLURM errno.
 */
int plugrack_read_dir( plugrack_t rack,
		       const char *dir );

/*
 * Add plugins to the rack by reading the given cache.  Note that plugins
 * may not actually load, but the rack will be made aware of them.
 *
 * NOT CURRENTLY IMPLEMENTED.
 */
int plugrack_read_cache( plugrack_t rack,
			 const char *cache );

/*
 * Remove from memory all plugins that are not currently in use by the
 * program.
 *
 * Returns a SLURM errno.
 */
int plugrack_purge_idle( plugrack_t rack );

/*
 * Load into memory all plugins which are currently unloaded.
 *
 * Returns a SLURM errno.
 */
int plugrack_load_all( plugrack_t rack );

/*
 * Write the current contents of the plugin rack to a file
 * in cache format, suitable to be read later using plugrack_read_cache().
 *
 * Returns a SLURM errno.
 */
int plugrack_write_cache( plugrack_t rack, const char *cache );

/*
 * Find a plugin in the rack which matches the given minor type,
 * load it if necessary, and return a handle to it.
 *
 * Returns PLUGIN_INVALID_HANDLE if a suitable plugin cannot be
 * found or loaded.
 */
plugin_handle_t plugrack_use_by_type( plugrack_t rack,
				      const char *type );

/*
 * Indicate that a plugin is no longer needed.  Whether the plugin
 * is actually unloaded depends on the rack's disposal policy.
 *
 * Returns a SLURM errno.
 */
int plugrack_finished_with_plugin( plugrack_t rack, plugin_handle_t plug );

/*
 * print all plugins in rack
 *
 * Returns a SLURM errno.
 */
int plugrack_print_all_plugin( plugrack_t rack);


#endif /*__PLUGRACK_H__*/
