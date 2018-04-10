/*****************************************************************************\
 *  layout.h - layout data structures and main functions
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr>
 *  at Bull for slurm-2.6.
 *  Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-14.11.
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

#ifndef __LAYOUT_DGR6BCQ2_INC__
#define __LAYOUT_DGR6BCQ2_INC__

#include "src/common/xtree.h"

/*
 * NOTE FOR ADDING RELATIONNAL STRUCTURES:
 *
 * When adding a relationnal structure you must :
 * - add definition to layout_st
 * - add the LAYOUT_STRUCT_RELTYPE constant
 * and then add logics in 3 functions:
 * - layout_init
 * - layout_free
 * - layout_node_delete
 *
 */
#define LAYOUT_STRUCT_TREE 1

typedef struct layout_st {
	char* name;        /* the name of the layout */
	uint32_t priority; /* the priority of the layout among the others,
			    * might be useful for selecting resources
			    * refining the results through a list of layouts */
	int struct_type;   /* type of relational structure (@see entity.h) */
	char* type;        /* the type of the layout, exp: racking, power...*/
	union {	           /* relational structure used by the layout */
		xtree_t* tree;
	};
} layout_t;

/*
 * layout_init - initialize a particular layout struct 
 * IN layout - the layout struct to initialize
 * IN name - the layout struct to initialize
 * IN type - the layout struct to initialize
 * IN priority - the layout priority value among the other layouts
 * IN struct_type - the type of relational structure to use to connect
 *      the entities managed by this layout (tree structure is the only
 *      relational structure supported for now)
 */
void layout_init(layout_t* layout, const char* name, const char* type,
		 uint32_t priority, int struct_type);

/*
 * layout_free - destroy a particular layout struct 
 * IN layout - the layout struct to free
 */
void layout_free(layout_t* layout);

/*
 * layout_get_name - return the name of a layout
 * IN layout - the layout struct to use
 *
 * Return value is the name of the layout
 */
const char* layout_get_name(const layout_t* layout);

/*
 * layout_get_type - return the type of a layout
 * IN layout - the layout struct to use
 *
 * Return value is the type of the layout
 */
const char* layout_get_type(const layout_t* layout);

/*
 * layout_get_priority - return the numeric priority of a layout
 * IN layout - the layout struct to use
 *
 * Return value is the priority of the layout
 */
uint32_t layout_get_priority(const layout_t* layout);


/*
 * layout_node_delete - remove a particular node from the relational
 *      structure of the layout
 * IN layout - the layout struct to use
 * IN node - a (void*) pointer to the relational struct node to remove
 */
void layout_node_delete(layout_t* layout, void* node);


/*
 * layout_get_tree - get the tree relational structure associated to a layout
 * IN layout - the layout struct to use
 *
 * Return value is a pointer to the xtree_t struct or NULL if not available
 */
xtree_t* layout_get_tree(layout_t* layout);

/*
 * layout_hashable_identify - defines a hashable identifying function to
 *      use with xhash.
 *
 * Note: it currently just returns the name of the layout
 */
const char* layout_hashable_identify(void* item);

/* layout_hashable_identify_by_type - defines a per-type hashable identifying
 *      function to use with xhash.
 *
 * Note: it currently just returns the type of the layout
 */
const char* layout_hashable_identify_by_type(void* item);

#endif /* end of include guard: __LAYOUT_DGR6BCQ2_INC__ */
