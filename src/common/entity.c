/*****************************************************************************\
 *  entity.c - layouts entities data structures and main functions
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

#include "string.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/entity.h"
#include "src/common/layout.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xtree.h"


/*****************************************************************************\
 *                                 HELPERS                                   *
\*****************************************************************************/

static void _entity_data_identify(void* item, const char** key,
				  uint32_t* key_len)
{
	entity_data_t* data_item = (entity_data_t*)item;
	*key = data_item->key;
	*key_len = strlen(data_item->key);
}

static void _entity_data_destroy(void* x)
{
	entity_data_t* entity_data = (entity_data_t*)x;
	if (entity_data) {
		xfree(entity_data->value);
		xfree(entity_data);
	}
}

static void _entity_node_destroy(void* x)
{
	entity_node_t* entity_node = (entity_node_t*)x;
	if (entity_node) {
		/* layout and node should be freed elsewhere */
		xfree(entity_node);
	}
}

static int _entity_add_data(const entity_t* entity, const char* key,
			    void* value, size_t size,
			    void (*_free)(void*), bool byreference)
{
	entity_data_t* result;
	entity_data_t* new_data_item;

	if (!key || !*key || !value)
		return SLURM_ERROR;

	result = (entity_data_t*)xhash_get_str(entity->data, key);
	if (result != NULL) {
		/* update existing value by ref or by override */
		if (byreference) {
			if (_free)
				_free(result->value);
			result->value = value;
		} else {
			memcpy(result->value, value, size);
		}
		return SLURM_SUCCESS;
	}

	/* add a new KV if not already existing, by ref or allocating
	 * a new buffer and dumping the provided input */
	new_data_item = (entity_data_t*)xmalloc(sizeof(entity_data_t));
	new_data_item->key = key;
	if (byreference) {
		new_data_item->value = value;
	} else {
		new_data_item->value = (void*) xmalloc(size);
		memcpy(new_data_item->value, value, size);
	}
	result = xhash_add(entity->data, new_data_item);
	if (result == NULL) {
		xfree(new_data_item);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*****************************************************************************\
 *                                 FUNCTIONS                                 *
\*****************************************************************************/

void entity_init(entity_t* entity, const char* name, const char* type)
{
	entity->name = xstrdup(name);
	entity->type = xstrdup(type);
	entity->data = xhash_init(_entity_data_identify, _entity_data_destroy);
	entity->nodes = list_create(_entity_node_destroy);
	entity->ptr = NULL;
}

void entity_free(entity_t* entity)
{
	if (entity) {
		xfree(entity->name);
		xfree(entity->type);
		xhash_free(entity->data);
		FREE_NULL_LIST(entity->nodes);
	}
}

const char* entity_get_name(const entity_t* entity)
{
	return entity->name;
}

const char* entity_get_type(const entity_t* entity)
{
	return entity->type;
}

int entity_get_data(const entity_t* entity, const char* key,
		    void* value, size_t size)
{
	void* data = NULL;
	data = entity_get_data_ref(entity, key);
	if (data != NULL) {
		memcpy(value, data, size);
		return SLURM_SUCCESS;
	}
	return SLURM_ERROR;
}

void* entity_get_data_ref(const entity_t* entity, const char* key)
{
	entity_data_t* data = (entity_data_t*)xhash_get_str(entity->data, key);
	if (data) {
		return data->value;
	}
	return NULL;
}

int entity_set_data(const entity_t* entity, const char* key,
		    void* value, size_t size)
{
	return _entity_add_data(entity, key, value, size, NULL, false);
}

int entity_set_data_ref(const entity_t* entity, const char* key, void* value,
			void (*_free)(void*))
{
	return _entity_add_data(entity, key, value, 0, _free, true);
}

void entity_delete_data(entity_t* entity, const char* key)
{
	xhash_delete_str(entity->data, key);
}

void entity_clear_data(entity_t* entity)
{
	xhash_clear(entity->data);
}

entity_node_t* entity_add_node(entity_t* entity, layout_t* layout)
{

	entity_node_t* entity_node = (entity_node_t*)xmalloc(
		sizeof(entity_node_t));
	entity_node->layout = layout;
	entity_node->entity = entity;
	entity_node->node = NULL;
	entity_node = list_append(entity->nodes, entity_node);
	return entity_node;
}

typedef struct _entity_get_node_walk_st {
	layout_t* layout;
	entity_node_t* node;
} _entity_get_node_walk_t;

static void _entity_get_node_walkfunc(layout_t* layout,
				      entity_node_t* node, void* arg)
{
	_entity_get_node_walk_t* real_arg =
		(_entity_get_node_walk_t*) arg;
	/* Note that if multiple nodes of the same layout are added
	 * to a single entity, the last one will be returned.
	 * An entity MUST NOT be added more than once /!\ */
	if (layout == real_arg->layout) {
		real_arg->node = node;
	}
}

entity_node_t* entity_get_node(entity_t* entity, layout_t* layout)
{
	_entity_get_node_walk_t arg;
	arg.layout = layout;
	arg.node = NULL;
	entity_nodes_walk(entity, _entity_get_node_walkfunc, (void*) &arg);
	return arg.node;
}

static int _entity_node_find(void* x, void* key)
{
	entity_node_t* entity_node = (entity_node_t*)x;
	return entity_node->node == key;
}

int entity_delete_node(entity_t* entity, layout_t* layout)
{
	int rc = SLURM_ERROR;
	entity_node_t* node;
	ListIterator i;
	node = entity_get_node(entity, layout);
	if (node == NULL)
		return rc;
	i = list_iterator_create(entity->nodes);
	if (list_find(i, _entity_node_find, node)) {
		list_delete_item(i);
		rc = SLURM_SUCCESS;
	}
	list_iterator_destroy(i);
	return rc;
}

int entity_clear_nodes(entity_t* entity)
{
	list_flush(entity->nodes);
	return SLURM_SUCCESS;
}

typedef struct _entity_nodes_walkstruct_st {
	void (*callback)(layout_t* layout, entity_node_t* node, void* arg);
	void* arg;
} _entity_nodes_walkstruct_t;

static int _entity_nodes_walkfunc(void* x, void* arg)
{
	entity_node_t* entity_node = (entity_node_t*)x;
	_entity_nodes_walkstruct_t* real_arg =
		(_entity_nodes_walkstruct_t*)arg;
	real_arg->callback(entity_node->layout,
			   entity_node,
			   real_arg->arg);
	return 0;
}

void entity_nodes_walk(entity_t* entity,
		       void (*callback)(layout_t* layout,
					entity_node_t* node,
					void* arg),
		       void* arg)
{
	_entity_nodes_walkstruct_t real_arg;
	real_arg.callback = callback;
	real_arg.arg = arg;
	list_for_each(entity->nodes, _entity_nodes_walkfunc, &real_arg);
}

void entity_hashable_identify(void* item, const char** key, uint32_t* key_len)
{
	entity_t* entity = (entity_t*)item;
	*key = entity->name;
	*key_len = strlen(entity->name);
}
