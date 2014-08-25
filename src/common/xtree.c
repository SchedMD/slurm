/*****************************************************************************\
 *  xtree.c - functions used for tree data structure manament
 *****************************************************************************
 *  Copyright (C) 2012 CEA/DAM/DIF
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

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xtree.h"

/* free the node childs */
static void xtree_free_childs(xtree_t* tree, xtree_node_t* node)
{
	xtree_node_t* current_node = node;
	xtree_node_t* free_later = NULL;

	/* if (!tree || !tree->root || !node) return; comm: not a user func */
	if (current_node && current_node->start) {
		/* tree has childs, depth may have changed */
		tree->state &= ~XTREE_STATE_DEPTHCACHED;
	}

	while (current_node) {
		if (current_node->start) {
			current_node = current_node->start;
			continue;
		}
		if (current_node == node) {
			current_node->start = current_node->end = NULL;
			return;
		}
		free_later = current_node;
		if (current_node->parent) {
			current_node->parent->start = current_node->next;
		}
		current_node = current_node->parent;
		if (tree->free)
			tree->free(free_later);
		xfree(free_later);
		--tree->count;
	}
}

/* tries to free the leftest leaf each time, and remove it from the tree,
 * then go above, since node above finish to be a leaf itself.
 */
void xtree_free(xtree_t* tree)
{
	if (!tree || !tree->root)
		return;
	xtree_free_childs(tree, tree->root);

	if (tree->free)
		tree->free(tree->root);
	xfree(tree->root);

	xtree_init(tree, tree->free);
}

void xtree_init(xtree_t* tree, xtree_free_data_function_t freefunc)
{
	tree->root  = NULL;
	tree->free  = freefunc;
	tree->count = 0;
	tree->depth = 0;
	tree->state = XTREE_STATE_DEPTHCACHED;
}

void xtree_set_freefunc(xtree_t* tree, xtree_free_data_function_t freefunc)
{
	tree->free = freefunc;
}

xtree_node_t* xtree_get_parent(xtree_t* tree, xtree_node_t* node)
{
	if (!node || !tree || !tree->root)
		return NULL;
	return node->parent;
}

uint32_t xtree_get_count(xtree_t* tree)
{
	if (!tree)
		return UINT32_MAX;
	return tree->count;
}

xtree_node_t* xtree_add_child(xtree_t* tree,
			xtree_node_t* parent,
			void* data,
			uint8_t flags)
{
	xtree_node_t* newnode = NULL;

	if (!tree || (!parent && tree->root) || (parent && !tree->root)) {
		return NULL;
	}

	xassert(flags & (XTREE_APPEND | XTREE_PREPEND));

	newnode = (xtree_node_t*)xmalloc(sizeof(xtree_node_t));
	newnode->data     = data;
	newnode->parent   = parent;
	newnode->start    = NULL;
	newnode->end      = NULL;
	newnode->next     = NULL;
	newnode->previous = NULL;

	if (!parent) {
		newnode->next     = NULL;
		newnode->previous = NULL;
		tree->root	= newnode;
		tree->count       = 1;
		tree->depth       = 1;
		tree->state       = XTREE_STATE_DEPTHCACHED;
		return newnode;
	}

	if (flags & XTREE_APPEND) {
		newnode->previous = parent->end;
		newnode->next     = NULL;
		if (parent->end) {
			parent->end->next = newnode;
		} else {
			parent->start = newnode;
		}
		parent->end = newnode;
	} else {
		newnode->next     = parent->start;
		newnode->previous = NULL;
		if (parent->start) {
			parent->start->previous = newnode;
		} else {
			parent->end = newnode;
		}
		parent->start = newnode;
	}

	++tree->count;
	tree->state &= ~XTREE_STATE_DEPTHCACHED;
	if (flags & XTREE_REFRESH_DEPTH)
		xtree_refresh_depth(tree);

	return newnode;
}

xtree_node_t* xtree_add_sibling(xtree_t* tree,
				xtree_node_t* node,
				void* data,
				uint8_t flags)
{
	xtree_node_t* newnode = NULL;

	xassert(flags & (XTREE_APPEND | XTREE_PREPEND));

	if (!tree)
		return NULL;

	/* no node, same behaviour as add_child */
	if (!node) return xtree_add_child(tree, node, data, flags);

	/* root node has only childs */
	/* FIXME: better to call add_child instead here, or can be too
	 * confusing ?
	 */
	if (!node->parent)
		return NULL;

	newnode = (xtree_node_t*)xmalloc(sizeof(xtree_node_t));
	newnode->data     = data;
	newnode->parent   = node->parent;
	newnode->start    = NULL;
	newnode->end      = NULL;
	newnode->next     = NULL;
	newnode->previous = NULL;

	if (flags & XTREE_APPEND) {
		newnode->previous = node;
		newnode->next = node->next;
		node->next = newnode;
		if (newnode->next) {
			newnode->next->previous = newnode;
		} else {
			node->parent->end = newnode;
		}
	} else {
		newnode->next = node;
		newnode->previous = node->previous;
		node->previous = newnode;
		if (newnode->previous) {
			newnode->previous->next = newnode;
		} else {
			node->parent->start = newnode;
		}
	}

	++tree->count;
	tree->state &= ~XTREE_STATE_DEPTHCACHED;

	if (flags & XTREE_REFRESH_DEPTH)
		xtree_refresh_depth(tree);

	return newnode;
}

/* NOTE: 0 = no node since no depth so implies no root */
uint32_t xtree_depth_const(const xtree_t* tree)
{
	if (tree->state & XTREE_STATE_DEPTHCACHED)
		return tree->depth;

	return xtree_depth_const_node(tree, tree->root);
}

static uint8_t xtree_depth_helper(xtree_node_t* node,
		uint8_t which,
		uint32_t level,
		void* arg)
{
	uint32_t* max_level = (uint32_t*)arg;

	if (level >= *max_level) {
		*max_level = level;
	}

	return 1;
}

uint32_t xtree_depth_const_node(const xtree_t* tree, const xtree_node_t* node)
{
	uint32_t max_level = 0;

	if (!tree->root)
		return 0;
	xtree_walk((xtree_t*)tree,
		   NULL,
		   0,
		   UINT32_MAX,
		   xtree_depth_helper,
		   &max_level);
	return max_level + 1;
}

uint32_t xtree_depth(xtree_t* tree)
{
	xtree_refresh_depth(tree);
	return tree->depth;
}

void xtree_refresh_depth(xtree_t* tree)
{
	if (tree->state & XTREE_STATE_DEPTHCACHED)
		return;
	tree->depth  = xtree_depth_const_node(tree, tree->root);
	tree->state |= XTREE_STATE_DEPTHCACHED;
}

uint32_t xtree_node_depth(const xtree_node_t* node)
{
	uint32_t depth = 0;
	while (node) {
		++depth;
		node = node->parent;
	}
	return depth;
}

/* always tries to browse the tree in this order : most left child, if no
 * child, go to next sibling, then if no sibling, go up until a sibling is
 * found.
 */
xtree_node_t* xtree_walk(xtree_t* tree,
			 xtree_node_t* node,
			 uint32_t min_level,
			 uint32_t max_level,
			 xtree_walk_function_t action,
			 void* arg)
{
	xtree_node_t* current_node = NULL;
	uint32_t level = 0;

	if (!tree || !action)
		return NULL;
	if (!node)
		node = tree->root;

	current_node = node;
	while (current_node) {

		if (level >= min_level && !action(current_node,
						  XTREE_GROWING,
						  level,
						  arg)) {
			return current_node;
		}

		/* go down and continue counting */
		if (current_node->start) {
			if (level >= min_level && !action(current_node,
							  XTREE_PREORDER,
							  level,
							  arg)) {
				return current_node;
			}
			if (level < max_level) {
				current_node = current_node->start;
				++level;
				continue;
			}
		} else if (level >= min_level &&
			   !action(current_node, XTREE_LEAF, level, arg)) {
			return current_node;
		}

		/* while no next member go up */
		while (!current_node->next) {
			current_node = current_node->parent;
			--level;
			if (!current_node) {
				return NULL;
			} else if (current_node == node) {
				if (level >= min_level &&
				    !action(current_node,
					    XTREE_ENDORDER,
					    level,
					    arg)) {
					return current_node;
				}
				return NULL;
			} else if (level >= min_level && !action(current_node,
							  XTREE_ENDORDER,
							  level,
							  arg)) {
				return current_node;
			}
		}

		/* go to next sibling */
		if (current_node->next) {
			if ((level >= min_level) &&
			    !action(current_node->parent,
				    XTREE_INORDER,
				    level - 1,
				    arg)) {
				return current_node;
			}
			current_node = current_node->next;
		}
	}
	return NULL;
}

struct xtree_find_st {
	xtree_find_compare_t compare;
	const void* arg;
};

static uint8_t xtree_find_helper(xtree_node_t* node,
				 uint8_t which,
				 uint32_t level,
				 void* arg)
{
	struct xtree_find_st* st = (struct xtree_find_st*)arg;
	return st->compare(node->data, st->arg);
}

xtree_node_t* xtree_find(xtree_t* tree,
		xtree_find_compare_t compare,
		const void* arg)
{

	struct xtree_find_st st;
	if (!tree || !compare)
		return NULL;
	st.compare = compare;
	st.arg = arg;
	return xtree_walk(tree, NULL, 0, UINT32_MAX, xtree_find_helper, &st);
}

xtree_node_t* xtree_delete(xtree_t* tree, xtree_node_t* node)
{
	xtree_node_t* parent = NULL;

	if (!tree || !tree->root || !node)
		return NULL;
	if (node == tree->root) {
		xtree_free(tree);
		return NULL;
	}

	parent = node->parent;
	if (parent->start == node && parent->end == node) {
		parent->start = parent->end = NULL;
		tree->state &= ~XTREE_STATE_DEPTHCACHED;
	} else if (parent->start == node) {
		parent->start = node->next;
		node->next->previous = NULL;
	} else if (parent->end == node) {
		parent->end = node->previous;
		node->previous->next = NULL;
	} else {
		node->previous->next = node->next;
		node->next->previous = node->previous;
	}

	xtree_free_childs(tree, node);
	if (tree->free)
		tree->free(node);
	xfree(node);
	--tree->count;

	return parent;
}

#define XTREE_GET_PARENTS_FIRST_SIZE 64

xtree_node_t** xtree_get_parents(xtree_t* tree,
		xtree_node_t* node,
		uint32_t* size)
{
	xtree_node_t*  current_node  = NULL;
	xtree_node_t** parents_list  = NULL;
	uint32_t       parents_size  = 0;
	uint32_t       parents_count = 0;
	if (!tree || !tree->root || !node || !size)
		return NULL;

	parents_size = XTREE_GET_PARENTS_FIRST_SIZE;
	parents_list = (xtree_node_t**)xmalloc(
			sizeof(xtree_node_t*)*parents_size);

	current_node = node->parent;
	while (current_node) {
		if (parents_count >= parents_size) {
			parents_size = parents_count*2;
			parents_list = (xtree_node_t**)xrealloc(parents_list,
					sizeof(xtree_node_t*)*parents_size);
		}
		parents_list[parents_count] = current_node;
		++parents_count;
		current_node = current_node->parent;
	}

	if (parents_count != 0) {
		parents_list = (xtree_node_t**)xrealloc(parents_list,
				sizeof(xtree_node_t*)*(parents_count+1));
		/* safety mesure, can be used as strlen if users assumes it */
		parents_list[parents_count] = NULL;
	}
	else {
		xfree(parents_list);
		parents_list = NULL;
	}
	*size = parents_count;
	return parents_list;
}

xtree_node_t* xtree_common(xtree_t* tree,
		const xtree_node_t* const* nodes,
		uint32_t size)
{
	xtree_node_t*  common_ancestor = NULL;
	xtree_node_t*  current_node    = NULL;
	uint32_t       i;
	uint8_t	found_common_ancestor;

	if (!tree || !tree->root || !nodes || !nodes[0] || !size ||
			!nodes[0]->parent)
		return NULL;

	common_ancestor = nodes[0]->parent;
	for (i = 1; i < size && common_ancestor; ++i) {
		found_common_ancestor = 0;
		while (common_ancestor && !found_common_ancestor) {
			if (!nodes[i]) return common_ancestor;
			current_node = nodes[i]->parent;
			while (current_node &&
			       current_node != common_ancestor) {
				current_node = current_node->parent;
			}
			if (current_node != common_ancestor) {
				common_ancestor = common_ancestor->parent;
			} else {
				found_common_ancestor = 1;
			}
		}
	}

	return common_ancestor;
}

#define XTREE_GET_LEAVES_FIRST_SIZE 64
struct xtree_get_leaves_st {
	xtree_node_t** list;
	uint32_t list_count;
	uint32_t size;
};

static uint8_t xtree_get_leaves_helper(xtree_node_t* node,
				       uint8_t which,
				       uint32_t level,
				       void* arg)
{
	struct xtree_get_leaves_st* st = (struct xtree_get_leaves_st*)arg;
	if (which == XTREE_LEAF) {
		if (st->list_count >= st->size) {
			st->size = st->list_count * 2;
			st->list = (xtree_node_t**)xrealloc(st->list,
					sizeof(xtree_node_t*)*st->size);
		}
		st->list[st->list_count] = node;
		++st->list_count;
	}
	return 1;
}

xtree_node_t** xtree_get_leaves(xtree_t* tree,
		xtree_node_t* node,
		uint32_t* size)
{
	struct xtree_get_leaves_st st;
	if (!tree || !size || !node) {
		/* testing node nulliness to return NULL since xtree_walk will
		 * be run for root node if node == NULL and return an
		 * unattended non null value. */
		return NULL;
	}
	if (!node->start) {
		/* if the node is a leave itself there is no leaves descending
		 * it, but tree walk will return the leave itself, so
		 * returning null before. */
		return NULL;
	}
	st.list_count = 0;
	st.size = XTREE_GET_LEAVES_FIRST_SIZE;
	st.list = (xtree_node_t**)xmalloc(sizeof(xtree_node_t*)*st.size);
	xtree_walk(tree, node, 0, UINT32_MAX, xtree_get_leaves_helper, &st);
	if (st.list_count != 0) {
		st.list = (xtree_node_t**)xrealloc(st.list,
				sizeof(xtree_node_t*)*(st.list_count+1));
		/* safety mesure, can be used as strlen if users assumes it */
		st.list[st.list_count] = NULL;
	}
	else {
		xfree(st.list);
		st.list = NULL;
	}
	*size = st.list_count;
	return st.list;
}

