/*****************************************************************************\
 *  layouts_mgr.h - layouts manager data structures and main functions
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr>
 *  at Bull for slurm-2.6.
 *  Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-14.11.
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

#ifndef __LAYOUTS_MGR_1NRINRSD__INC__
#define __LAYOUTS_MGR_1NRINRSD__INC__

#include "src/common/list.h"
#include "src/common/xhash.h"
#include "src/common/xtree.h"
#include "src/common/parse_config.h"

#include "src/common/layout.h"
#include "src/common/entity.h"

/*
 * Layouts are managed through a "layouts manager" of type layouts_mgr_t.
 *
 * The layouts_mgr_t manages the layouts and entities loaded through the list
 * of layouts specified in the Slurm configuration file (slurm.conf)
 *
 * At startup, Slurm initialize one layouts_mgr_t using slurm_layouts_init()
 * and then load the required layouts defined in the configuration using
 * slurm_layouts_load_config().
 *
 * The different layouts and entities can then be queried using either
 * slurm_layouts_get_layout() and slurm_layouts_get_entity().
 *
 * Note that each entity contains a list of nodes appearing inside the
 * associated layouts.
 */

/*
 * Potential enhancement to complete: agregate specified plugin etypes in a
 *      xhash in the mgr, avoiding same string to be duplicated again and again.
 *      (in short: apply the same logic for etypes as for entity data keys.)
 */

typedef enum layouts_keydef_types_en {
	L_T_ERROR = 0,
	L_T_STRING,
	L_T_LONG,
	L_T_UINT16,
	L_T_UINT32,
	L_T_BOOLEAN,
	L_T_FLOAT,
	L_T_DOUBLE,
	L_T_LONG_DOUBLE,
	L_T_CUSTOM,
} layouts_keydef_types_t;

typedef struct layouts_keyspec_st {
	char*			key;
	layouts_keydef_types_t	type;
	void			(*custom_destroy)(void*);
	char*			(*custom_dump)(void*);
} layouts_keyspec_t;

typedef struct layouts_plugin_spec_st {
	const s_p_options_t*		options;
	const layouts_keyspec_t*	keyspec;
	int				struct_type;
	const char**			etypes;
	bool				automerge;
} layouts_plugin_spec_t;

/*****************************************************************************\
 *                             PLUGIN FUNCTIONS                              *
\*****************************************************************************/

/*
 * slurm_layouts_init - intialize the layouts mgr, load the required plugins
 *        and initialize the internal hash tables for entities, keydefs and
 *        layouts.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if all the required layouts were not
 * loaded correctly.
 *
 * Notes: this call do not try to read and parse the layouts configuration
 * files. It only loads the layouts plugins, dlsym the layout API and conf
 * elements to prepare the reading and parsing performed in the adhoc call
 * slurm_layouts_load_config()
 *
 */
int slurm_layouts_init(void);

/*
 * slurm_layouts_fini - uninitialize the layouts mgr and free the internal
 *        hash tables.
 */
int slurm_layouts_fini(void);

/*
 * slurm_layouts_load_config - use the layouts plugins details loaded during
 *        slurm_layouts_init() and read+parse the different layouts
 *        configuration files, creating the entities and the relational
 *        structures associated the eaf of them.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if all the required layouts were not
 * loaded correctly.
 */
int slurm_layouts_load_config(void);

/*
 * slurm_layouts_get_layout - return the layout from a given type
 *
 * Return a pointer to the layout_t struct of the layout or NULL if not found
 */
layout_t* slurm_layouts_get_layout(const char* type);

/*
 * slurm_layouts_get_entity - return the entity from a given name
 *
 * Return a pointer to the entity_t struct of the entity or NULL if not found
 */
entity_t* slurm_layouts_get_entity(const char* name);

#endif /* end of include guard: __LAYOUTS_MGR_1NRINRSD__INC__ */
