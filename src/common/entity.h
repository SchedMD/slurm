/*****************************************************************************\
 *  entity.h - layouts entities data structures and main functions
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

#ifndef __ENTITY_GDBZJYZL_INC__
#define __ENTITY_GDBZJYZL_INC__

#include "src/common/list.h"
#include "src/common/xhash.h"
#include "src/common/xtree.h"
#include "src/common/layout.h"

/*****************************************************************************\
 *                                 STRUCTURES                                *
\*****************************************************************************/

/* definition of the entity itself, main structure of this file. */
typedef struct entity_st {
	char* name;     /* unique name of this entity */
	char* type;     /* basic type of entity */
	xhash_t* data;  /* data table, stores data items */
	List nodes;     /* list of nodes where this entity
			   appears */
	void *ptr;      /* private data for arbitraty ptr */
} entity_t;

/* definition of the key-value structure used internaly by entities */
typedef struct entity_data_st {
	const char* key; /* memory not owned, see layouts_keydef */
	void* value;
} entity_data_t;

/* definition of the entity node structure used internaly by entities
 * to represent the layout nodes that are linked to them */
typedef struct entity_node_st {
	layout_t* layout; /* layout containing a relationnal structure holding
			   * a reference to the entity */
	entity_t* entity; /* pointer to the associated entity */
	void* node;       /* pointer to the relational node referencing
			     this entity node */
} entity_node_t;

/*****************************************************************************\
 *                                 FUNCTIONS                                 *
\*****************************************************************************/

/*
 * entity_init - initialize an entity
 *
 * IN entity - the entity struct to initialize
 * IN name - the name of the entity
 * IN type - the type of the entity
 */
void entity_init(entity_t* entity, const char* name, const char* type);

/*
 * entity_free - free entity internals
 *
 * IN entity - the entity struct to fee internals from
 */
void entity_free(entity_t* entity);

/*
 * entity_get_name - get the name of an entity
 *
 * IN entity - the entity struct to use
 *
 * Return value is the name of the entity
 */
const char* entity_get_name(const entity_t* entity);

/*
 * entity_get_type - get the type of an entity
 *
 * IN entity - the entity struct to use
 *
 * Return value is the type of the entity
 */
const char* entity_get_type(const entity_t* entity);

/*
 * entity_get_data - copy the content of the data associated to a particular key
 *       of an entity into a buffer up to the requested size
 *
 * IN entity - the entity struct to use
 * IN key - the targeted key
 * IN value - ponter to the mem area to fill
 * IN size - size of the mem area to copy
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if no element found
 */
int entity_get_data(const entity_t* entity, const char* key,
		    void* value, size_t size);

/*
 * entity_get_data_ref - get the address of the pointer to the data associated
 *       with a particular key of an entity
 *
 * IN entity - the entity struct to use
 * IN key - the targeted key
 *
 * Return value is the address of the (void*) pointer to the data associated to
 *       the key or NULL in case of error
 */
void* entity_get_data_ref(const entity_t* entity, const char* key);

/*
 * entity_set_data - copy the content of the input buffer up to the requested
 *       size into the the buffer associated to a particular key of an entity
 *       (note that the entity key value's buffer is allocated internally if
 *       necessary)
 *
 * IN entity - the entity struct to use
 * IN key - the targeted key
 * IN value - ponter to the mem area to fill with
 * IN size - size of the mem area to copy
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if no element found
 */
int entity_set_data(const entity_t* entity, const char* key,
		    void* value, size_t size);

/*
 * entity_set_data_ref - associate a particular key of an entity with the
 *       input buffer, 
 *       with a particular key of an entity
 *
 * IN entity - the entity struct to use
 * IN key - the key the data must be associated to
 * IN value - the data to associate with the key (potentially overriding
 *       previous value)
 * IN _free - a function to apply on the former value in case it exists
 *       before overriding
 *
 * Return SLURM_SUCCESS or SLURM_ERROR in case of error
 */
int entity_set_data_ref(const entity_t* entity, const char* key, void* value,
			void (*_free)(void*));

/*
 * entity_delete_data - delete the data associated with a particular key
 *       of an entity
 *
 * IN entity - the entity struct to use
 * IN key - the key the data must be deleted from
 */
void entity_delete_data(entity_t* entity, const char* key);

/*
 * entity_clear_data - removes all the entity key/value pairs
 *
 * IN entity - the entity struct to use
 *
 * Notes: does not free value, user is responsible for it, if the data_freefunc
 *       is null.
 */
void entity_clear_data(entity_t* entity);

/*
 * entity_add_node - add a per layout entity node to the list of nodes referring
 *       to this entity
 *
 * IN entity - the entity struct to use
 * IN layout - the layout to create an entity node referring to this entity
 *
 * Notes: - the returned entity_node does not point to anything at that point.
 *          it will be added to a relational structure and will then have to 
 *          be associated to the underlying relational node afterwards.
 *        - the entity node will not own the memory of the relationnal node.
 */
entity_node_t* entity_add_node(entity_t* entity, layout_t* layout);

/*
 * entity_get_node - get the entity node referring to a particular layout in
 *       the list of entity nodes associated to an entity.
 *
 * IN entity - the entity struct to use
 * IN layout - the layout having an entity node referring to this entity
 *
 * Return value is the entity node of the layout or NULL if not found
 */
entity_node_t* entity_get_node(entity_t* entity, layout_t* layout);

/*
 * entity_delete_node - remove the entity node referring to a particular layout
 *       from the list of entity nodes associated to an entity
 *
 * IN entity - the entity struct to use
 * IN layout - the layout having an entity node referring to this entity
 *
 * Return SLURM_SUCCESS or SLURM_ERROR
 */
int entity_delete_node(entity_t* entity, layout_t* layout);

/*
 * entity_clear_nodes - remove all the entity node associated to an entity
 *
 * IN entity - the entity struct to use
 *
 * Return SLURM_SUCCESS or SLURM_ERROR
 */
int entity_clear_nodes(entity_t* entity);

/*
 * entity_nodes_walk - iterate over the nodes referring to this entity
 *       applying a particular callback with a particular arg. It can be
 *       used to search, compare, and do other general operation on each nodes
 *       associated with an entity.
 *
 * IN entity - the entity struct to use
 * IN callback - the callback function to use. The first arg will receive the
 *       layout of the node being processed, the second will be the node itself
 *       and the third one will be the arg passed to the function.
 * IN arg - the arg to pass to the callback function for every node.
 */
void entity_nodes_walk(entity_t* entity,
		       void (*callback)(layout_t*, entity_node_t*, void*),
		       void* arg);

/*
 * entity_hashable_identify - defines a hashable identifying function to
 *      use with xhash.
 *
 * Note: it currently just returns the name of the entity
 */
const char* entity_hashable_identify(void* item);

#endif /* end of include guard: __ENTITY_GDBZJYZL_INC__ */
