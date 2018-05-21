/*****************************************************************************\
 *  xtree.h - functions used for tree data structure manament
 *****************************************************************************
 *  Copyright (C) 2012 CEA/DAM/DIF
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

#ifndef __XTREE_O7S7VY_INC__
#define __XTREE_O7S7VY_INC__

#include <stdint.h>

/**
 * The root node's parent must always be NULL (or browsing algorithm which
 * stops at root when going up, will either crash or go to an infinite loop).
 */
typedef struct xtree_node_st {
        void*                 data;     /* data of the node          */
        struct xtree_node_st* parent;   /* parent node, level up     */
        struct xtree_node_st* start;    /* first node, level below   */
        struct xtree_node_st* end;      /* last node, level below    */
        struct xtree_node_st* next;     /* next node, same level     */
        struct xtree_node_st* previous; /* previous node, same level */
} xtree_node_t;

/* function prototype to deallocate data stored in tree nodes */
typedef void (*xtree_free_data_function_t)(xtree_node_t* node);

typedef struct xtree_st {
        xtree_node_t*              root;  /* root node of the tree    */
        xtree_free_data_function_t free;  /* frees nodes data or null */
        uint32_t                   count; /* always: number of nodes  */
        uint32_t                   depth; /* cached depth             */
        uint32_t                   state; /* see XTREE_STATE_*        */
} xtree_t;

/* free a complete tree whatever the `tree` entry point, it can be a leaf for
 * example.
 *
 * The tree itself is freed but the `xtree_t` structure is not freed (since it
 * can be stored on the stack).
 *
 * During freeing operation the tree is in a invalid state.
 *
 * @param tree is the tree entry point.
 */
void xtree_free(xtree_t* tree);

/**
 * initialize a xtree_t structure with an empty tree
 *
 * @param freefunc is the function which will be used to free data associated
 *                 with tree's nodes or NULL, freefunc must only desallocate
 *                 node->data not the node itself, node is passed as a
 *                 parameter if the user wants to do some processing according
 *                 to the being-to-be-freed address of the node.
 * @param tree is the structure to initialize
 *
 */
void xtree_init(xtree_t* tree, xtree_free_data_function_t freefunc);

/**
 * Sets the functions which will be used to free data member for each node or
 * NULL to disable freeing. This function should not be used if nodes have
 * already been added to the tree.
 * @param freefunc is the freeing function.
 */
void xtree_set_freefunc(xtree_t* tree, xtree_free_data_function_t freefunc);

#define XTREE_STATE_DEPTHCACHED 1

#define XTREE_PREPEND        1 /** append to child list  */
#define XTREE_APPEND         2 /** prepend to child list */
#define XTREE_REFRESH_DEPTH  4 /** default: don't refresh at insertion */

/** convenient function to get the parent of a node */
xtree_node_t* xtree_get_parent(xtree_t* tree, xtree_node_t* node);

#define xtree_node_get_data(node) ((node) ? (node)->data : NULL)

#define xtree_get_root(tree) ((tree) ? (tree)->root : NULL)

/** convenient function to get node count
 * @returns the count of node inside the tree, constant time, or UINT32_MAX
 * if tree is NULL.
 */
uint32_t xtree_get_count(xtree_t* tree);

/** Add a child to a node of a tree.
 * @param tree the tree to manage, `parent` belongs to it.
 * @param parent is the node where to add a child to.
 * @param data is the data member associated with the new child.
 * @param flags is a combination of the following :
 * XTREE_APPEND: add the new node after `node` (mutually exclusive with
 * PREPEND);
 * XTREE_PREPEND: add the new node before `node` (mutually exclusive with
 * APPEND);
 * XTREE_REFRESH_DEPTH: refresh the cached depth of the tree.
 * @returns the new child node added or NULL if parent is NULL but tree has
 * root node. This function assumes a flag is given or abort o/w.
 */
xtree_node_t* xtree_add_child(xtree_t* tree,
			      xtree_node_t* parent,
			      void* data,
			      uint8_t flags);

/** Add a sibling to a node.
 * @param tree is the tree to manage, NULL tree is illegal.
 * @param node is the node next to which the new node should be added. When
 * node is null, the function has the same behaviour as xtree_add_child.
 * @param data is the data associated with the new node being added.
 * @param flags is a combination of the following :
 * XTREE_APPEND: add the new node after `node` (mutually exclusive with
 * PREPEND);
 * XTREE_PREPEND: add the new node before `node` (mutually exclusive with
 * APPEND);
 * XTREE_REFRESH_DEPTH: refresh the cached depth of the tree.
 * @returns the new child node added or NULL for illegal parameter.
 */
xtree_node_t* xtree_add_sibling(xtree_t* tree,
				xtree_node_t* node,
				void* data,
				uint8_t flags);

/** Calculate a tree depth by calling xtree_walk to browse the tree.
 * This function browse the complete tree to determine the greatest depth of
 * the tree.
 * @param tree the tree to calculate depth from.
 * @returns the depth of a given tree, a return value of 0 means the tree has
 *          no nodes, even not a root node, it is an empty tree.
 */
uint32_t xtree_depth_const(const xtree_t* tree);

/** Calculate depth starting from node (call xtree_walk).
 * @param tree the tree to calculate depth from.
 * @param node the starting point for calculating depth.
 */
uint32_t xtree_depth_const_node(const xtree_t* tree, const xtree_node_t* node);

/** Calculate a tree depth, caching its result inside the tree (call
 * xtree_walk).
 * @see xtree_depth_const
 */
uint32_t xtree_depth(xtree_t* tree);

/** Calculate tree depth with xtree_depth and cache it.
 * @param tree is the tree to refresh.
 */
void xtree_refresh_depth(xtree_t* tree);

/** Convenient function which go upward to the root node to calculate node
 * depth passed in argument.
 */
uint32_t xtree_node_depth(const xtree_node_t* node);

/** see function prototype for xtree_walk for description */
#define XTREE_PREORDER 1
#define XTREE_INORDER  2
#define XTREE_ENDORDER 4
#define XTREE_LEAF     8
#define XTREE_GROWING  16

#define XTREE_LEVEL_MAX UINT32_MAX

/** function prototype for walking through tree.
 *
 * @param node is the current node being parsed.
 * @param which informs which visit is being done on the node,
 *        XTREE_PREORDER, XTREE_INORDER, XTREE_ENDORDER indicates this node
 *        is being visited before visiting the children, between visit of
 *        each children (if more than one), and after visiting the children.
 *        XTREE_LEAF indicates the node being visited is a leaf and receive
 *        consequently only one visit.
 *
 *        XTREE_GROWING is called before any other calls, allowing a node to
 *        add childs.
 *
 * @param level is the current level, 0 being the root node.
 * @param arg is the data assigned to xtree_walk then calling it.
 * @returns 0 to indicate that xtree_walk do not need to continue to go
 *          through the tree, nonzero value continue the browsing.
 */
typedef uint8_t (*xtree_walk_function_t)(xtree_node_t* node,
					 uint8_t which,
					 uint32_t level,
					 void* arg);

/** Browse the tree depth-first, left-to-right. It mimics the C twalk
 * function.
 *
 * You should not modify tree structure during traversal, since it can cause
 * browsing errors or crash.
 *
 * @param tree is the tree you want to browse.
 * @param node is the starting point or NULL (same as tree->root) to begin
 *             the traversal.
 * @param min_level is the minimum level required to execute the action
 *                  function, minimum being root node (=0).
 * @param max_level is the maximum level to browse, the traversal's goes up
 *                  again reaching this point, maximum being UINT32_MAX for
 *                  all the tree's depth.
 * @param action is the user function to execute for each node. See the
 *               typedef documentation.
 * @param arg is the user data to pass unmodified to the user function.
 * @returns the lastest node for which action was aborted or NULL.
 */
xtree_node_t* xtree_walk(xtree_t* tree,
			 xtree_node_t* node,
			 uint32_t min_level,
			 uint32_t max_level,
			 xtree_walk_function_t action,
			 void* arg);

/** @see xtree_find */
typedef uint8_t (*xtree_find_compare_t)(const void* node_data,
					const void* arg);

/** Convenient function which calls xtree_walk to find a node according to
 * a compare function.
 *
 * @param tree is the tree to search through.
 * @param compare is a function returning 0 when the element correspond to
 *                search criterias, this function takes node_data for each
 *                node as first argument and arg as its second argument.
 * @param arg is a function argument which can be the key or whatever data
 *            the user function needs to find the searched element.
 * @returns the found node or NULL.
 */
xtree_node_t* xtree_find(xtree_t* tree,
			 xtree_find_compare_t compare,
			 const void* arg);

/** Deletes a node from the tree. You can use xtree_find or xtree_walk to
 * find the wanted node. This function frees the node data thanks to the
 * setted freefunc function of the tree. And recursively frees node childs
 * too.
 *
 * @param tree is the tree to manage.
 * @param node is the node to remove.
 * @returns the parent node of the deleted node or NULL if bad argument/tree
 *          or the node was the tree's root node.
 */
xtree_node_t* xtree_delete(xtree_t* tree, xtree_node_t* node);

/** Gets recursive parents list from a node or NULL for bad tree/parameters
 * or root node.
 * User is responsible for `xfree`'ing the returned list.
 * Parents lists starts from node's parent to root.
 *
 * @param tree the managed tree.
 * @param node the node to start finding parents (not included itself in the
 *             list).
 * @param size will be modified according to the number of parents in the
 *             returned list if the return value is not null.
 * @returns the `xmalloc`ed parents array or NULL. Although size contains the
 *          array number of elements, the array is null terminated.
 */
xtree_node_t** xtree_get_parents(xtree_t* tree,
				 xtree_node_t* node,
				 uint32_t* size);

/** Get common ancestor of all given nodes.
 * Example: 1 -> 2 -> 7, common ancestor of 2 and 7 is 1.
 *
 * @param tree is the managed tree.
 * @param nodes is a node table which should have a common ancestor, an
 *              optionnal null node ends the list, else list stops at the
 *              (size - 1)th element.
 * @param size is the number of elements the node table has, can be greather
 *             than the number of actual elements if list is null terminated
 *             (such as the UINT32_MAX value).
 * @returns the common ancestor of all nodes or NULL if no such ancestors
 *          exists (if root node is listed, returns null since root node has
 *          no ancestor).
 */
xtree_node_t* xtree_common(xtree_t* tree,
			   const xtree_node_t* const* nodes,
			   uint32_t size);

/** Get recursive list of leaves starting from node.
 * User is responsible for `xfree`'ing the returned list.
 *
 * @param tree the managed tree.
 * @param node the node to start from.
 * @param size will be modified according to the number of leaves in the
 *             retured list if the return value is not null.
 * @returns the `xmalloc`ed leaves array starting from node or NULL if bogus
 *          tree or bad parameters. Although size contains the
 *          array number of elements, the array is null terminated.
 */
xtree_node_t** xtree_get_leaves(xtree_t* tree,
				xtree_node_t* node,
				uint32_t* size);

#endif
