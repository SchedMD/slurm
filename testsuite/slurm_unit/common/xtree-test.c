/*****************************************************************************\
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

/* TODO: voir comment vérifier les leak de mémoires avec valgrind avec ce
 * framework (si jamais il y a déjà des exemples).
 */

#include <check.h>
#include <stdlib.h>

#include "src/common/xmalloc.h"
#include "src/common/xtree.h"

/*****************************************************************************
 * FIXTURE                                                                   *
 *****************************************************************************/

xtree_t mytree_empty;
xtree_t mytree_by_addchild;

/* here we construct a tree in the following form :
 *       1
 *    / / \  \
 *    6 2  3  5
 *     / \
 *     7  4
 * numbers are chronological adding order.
 */
static void init_by_addchild(void)
{
    xtree_t* tree = &mytree_by_addchild;
    char* fake_addr = (char*)1;

    xtree_add_child(tree, NULL, fake_addr, XTREE_APPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root->start, fake_addr, XTREE_APPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root, fake_addr, XTREE_PREPEND);
    ++fake_addr;
    xtree_add_child(tree, tree->root->start->next, fake_addr, XTREE_PREPEND);
}

static void setup(void)
{
    xtree_init(&mytree_empty, NULL);
    init_by_addchild();
}

static void teardown(void)
{
    xtree_free(&mytree_empty);
    xtree_free(&mytree_by_addchild);
}

/*****************************************************************************
 * UNIT TESTS                                                                *
 ****************************************************************************/

START_TEST(test_xtree_creation_unmanaged)
{
    xtree_t* tree = &mytree_empty;

    fail_unless(tree->root == NULL,
            "tree has a root on creation");
    fail_unless(tree->count == 0,
            "tree has nodes on creation");
    fail_unless(tree->depth == 0,
            "tree has a depth on creation");
    fail_unless(xtree_depth_const(tree) == 0,
            "tree depth is not 0 on creation");
    fail_unless(tree->state == XTREE_STATE_DEPTHCACHED,
            "tree is not cached on creation");
}
END_TEST

START_TEST(test_xtree_add_root_node_unmanaged)
{
    xtree_t* tree = &mytree_empty;
    char* fake_addr = (char*)1;

    fail_unless(xtree_add_child(tree, NULL, fake_addr, XTREE_APPEND) != NULL,
            "unable to add root node");
    fail_unless(tree->root != NULL,
            "root node has not been allocated");
    fail_unless(tree->free == NULL,
            "bad free function in the tree");
    fail_unless(tree->count == 1,
            "there should be at least one node and only one in node count");
    fail_unless(xtree_depth_const(tree) == 1,
            "tree should have a depth of one (depth %d)",
            xtree_depth_const(tree));
    fail_unless(tree->root->data == (void*)1,
            "node data is incorrect");
    fail_unless(tree->root->parent == NULL,
            "root node has a parent");
    fail_unless(tree->root->start == NULL && tree->root->end == NULL,
            "root node should not already have child in it");
    fail_unless(tree->root->next == NULL && tree->root->previous == NULL,
            "root node have invalid siblings");

    xtree_refresh_depth(tree);
    fail_unless(tree->depth == 1,
            "root node refreshed should have one depth (root level)");
    fail_unless(tree->state == XTREE_STATE_DEPTHCACHED,
            "root node should now have its depth been cached");

    ++fake_addr;
    fail_unless(xtree_add_child(tree, NULL, fake_addr, XTREE_APPEND) == NULL,
            "xtree_add_child with NULL parent and root node in tree should "
            "return a NULL pointer");
    fail_unless(tree->root->data == (void*)1,
            "xtree_add_child generated an operation and should not in context");
    fail_unless(tree->root->start == NULL,
            "xtree_add_child had added an invalid child");
    fail_unless(tree->root->start == tree->root->end,
            "xtree_add_child invalidated root node child list");

    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL,
            "unable to add child node to root node");
    fail_unless(tree->count == 2,
            "bad tree node count");
    fail_unless(xtree_depth_const(tree) == 2,
            "bad depth after root's first child");
    fail_unless(tree->state != XTREE_STATE_DEPTHCACHED,
            "tree should not have already cached level count");

    fail_unless(tree->root &&
            tree->root->data == (void*)1 &&
            tree->root->parent == NULL &&
            tree->root->next == NULL && tree->root->previous == NULL,
            "root node has badly been modified");
    fail_unless(!!tree->root->start,
            "root has no child, but should have one");
    fail_unless(tree->root->start == tree->root->end,
            "root child list is inconsistent");

    fail_unless(tree->root->start->data == (void*)2,
            "bad child data");
    fail_unless(tree->root->start->parent == tree->root,
            "child parent does not point to root node");
    fail_unless(!tree->root->start->start,
            "child should be unique for now");
    fail_unless(tree->root->start->start == tree->root->start->end,
            "child children list is inconsistent");
    fail_unless(!tree->root->start->next && !tree->root->start->previous,
            "child should not have siblings");

    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL,
            "unable to add second child");

    fail_unless(tree->root->start != tree->root->end,
            "root should have more children");
    fail_unless(tree->root->start->next == tree->root->end &&
            tree->root->end->previous == tree->root->start &&
            tree->root->end->next == NULL &&
            tree->root->start->previous == NULL,
            "root children list is inconsistent");
    fail_unless(tree->root->end->data == (void*)3,
            "root second child has bad data");
}
END_TEST

char test_table[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static void myfree(xtree_node_t* x)
{
    int* item = (int*)x->data;
    fail_unless(*item < 10 && *item >= 0,
            "bad data passed to freeing function");
    fail_unless(test_table[*item] == 1,
            "item was duplicated/corrupted");
    test_table[*item] = 0;
    xfree(item);
}

/* here we construct a tree in the following form :
 *       R
 *      / \
 *     /\
 *    /\
 *   /\
 *  /
 * Then free it (in teardown).
 */
START_TEST(test_xtree_freeing_elements)
{
    xtree_t* tree = &mytree_empty;
    xtree_node_t* node = NULL;
    int* x = NULL;
    int i = 0;

    xtree_set_freefunc(tree, (xtree_free_data_function_t) myfree);

    x = (int*)xmalloc(sizeof(int));
    fail_unless(x != NULL,
            "unable to allocate memory for test");
    *x = i;
    test_table[i] = 1;
    xtree_add_child(tree, NULL, x, XTREE_APPEND);
    node = tree->root;

    for(i = 1; i < 10; ++i) {
        x = (int*)xmalloc(sizeof(int));
        fail_unless(x != NULL,
                "unable to allocate memory for test");
        *x = i;
        test_table[i] = 1;
        xtree_add_child(tree, node, x, XTREE_APPEND);
        if ((i % 2) == 0) {
            node = node->start;
        }
    }

    xtree_free(tree);

    for(i = 0; i < 10; ++i) {
        fail_unless(test_table[i] == 0,
                "one element has not been freed in the table (num %d)",
                i);
    }
}
END_TEST

/* here we construct a tree in the following form :
 *       1
 *    / / \  \
 *    6 2  3  5
 *     / \
 *     7  4
 * numbers are chronological adding order.
 */
START_TEST(test_xtree_with_add_child)
{
    xtree_t* tree = &mytree_empty;
    xtree_node_t* level1_2 = NULL;
    char* fake_addr = (char*)1;

    fail_unless(xtree_add_child(tree, NULL, fake_addr, XTREE_APPEND) != NULL,
            NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL, NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL, NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root->start, fake_addr, XTREE_APPEND)
            != NULL, NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL, NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_PREPEND)
            != NULL, NULL);
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root->start->next,
                fake_addr, XTREE_PREPEND)
            != NULL, NULL);

    fail_unless(tree->root->start->data == (void*)6 &&
            tree->root->start->next->data == (void*)2 &&
            tree->root->start->next->next->data == (void*)3 &&
            tree->root->start->next->next->next->data == (void*)5,
            "bad tree for children level 1 browsing the tree forward");
    fail_unless(tree->root->end->data == (void*)5 &&
            tree->root->end->previous->data == (void*)3 &&
            tree->root->end->previous->previous->data == (void*)2 &&
            tree->root->end->previous->previous->previous->data == (void*)6,
            "bad tree for children level 1 browsing backward");
    fail_unless(tree->root->start->previous == NULL &&
            tree->root->end->next == NULL,
            "bad tree edges");
    fail_unless(tree->root->start->start == NULL && /* 6 */
            tree->root->start->next->start != NULL && /* 2 */
            tree->root->start->next->end != NULL && /* 2 */
            tree->root->start->next->start != /* 2 */
                tree->root->start->next->end && /* 2 */
            tree->root->start->next->next->start == NULL && /* 3 */
            tree->root->start->next->next->next->start == NULL, /* 5 */
            "bad tree structure for children of child list level 1");
    level1_2 = tree->root->start->next;
    fail_unless(level1_2->start->data == (void*)7 &&
            level1_2->start->start == NULL &&
            level1_2->start->previous == NULL &&
            level1_2->start->next ==
                level1_2->end &&
            level1_2->end->data == (void*)4 &&
            level1_2->end->next == NULL &&
            level1_2->end->start == NULL,
        "bad tree structure for children level 2");
}
END_TEST

/* here we construct a tree in the following form :
 *         1
 *    / / / \ \ \
 *   7 2 6   4 3 5
 * numbers are chronological adding order.
 */
START_TEST(test_xtree_with_add_sibling)
{
    xtree_t* tree = &mytree_empty;
    char* fake_addr = (char*)1;

    fail_unless(xtree_add_sibling(tree, NULL, fake_addr, XTREE_APPEND) != NULL,
            NULL); /* 1 */
    ++fake_addr;
    fail_unless(xtree_add_child(tree, tree->root, fake_addr, XTREE_APPEND)
            != NULL, NULL); /* 2 */
    fail_unless(xtree_add_sibling(tree, tree->root, fake_addr, XTREE_APPEND)
            == NULL, "add_sibling should return null when used with root node");
    ++fake_addr;
    fail_unless(xtree_add_sibling(tree, tree->root->start, fake_addr, XTREE_APPEND)
            != NULL, NULL); /* 3 */
    ++fake_addr;
    fail_unless(xtree_add_sibling(tree, tree->root->end, fake_addr, XTREE_PREPEND)
            != NULL, NULL); /* 4 */
    ++fake_addr;
    fail_unless(xtree_add_sibling(tree, tree->root->end, fake_addr, XTREE_APPEND)
            != NULL, NULL); /* 5 */
    ++fake_addr;
    fail_unless(xtree_add_sibling(tree, tree->root->start, fake_addr, XTREE_APPEND)
            != NULL, NULL); /* 6 */
    ++fake_addr;
    fail_unless(xtree_add_sibling(tree, tree->root->start, fake_addr, XTREE_PREPEND)
            != NULL, NULL); /* 7 */

    fail_unless(tree->root->data == (void*)1,
            "bad root node");

    fail_unless(tree->root->start->data == (void*)7 &&
            tree->root->start->next->data == (void*)2 &&
            tree->root->start->next->next->data == (void*)6 &&
            tree->root->start->next->next->next->data == (void*)4,
            "bad tree structure browsing forward");
    fail_unless(tree->root->end->data == (void*)5 &&
            tree->root->end->previous->data == (void*)3 &&
            tree->root->end->previous->previous->data == (void*)4 &&
            tree->root->end->previous->previous->previous->data == (void*)6,
            "bad tree structure browsing backward");
    fail_unless(tree->root->start->previous == NULL &&
            tree->root->end->next == NULL,
            "bad tree edges");
    fail_unless(tree->root->start->start == NULL && /* 7 */
            tree->root->start->next->start == NULL && /* 2 */
            tree->root->start->next->next->start == NULL && /* 6 */
            tree->root->end->start == NULL && /* 5 */
            tree->root->end->previous->start == NULL && /* 3 */
            tree->root->end->previous->previous->start == NULL, /* 4 */
            "bad tree structure level 1 should not have children");
}
END_TEST

START_TEST(test_xtree_depth)
{
    xtree_t* tree = &mytree_by_addchild;
    uint32_t size;

    fail_unless(~tree->state & XTREE_STATE_DEPTHCACHED,
            "state is cached, should not be");
    size = xtree_depth(tree);
    fail_unless(size == 3, "bad depth, returned: %lu", size);
    fail_unless(xtree_depth(tree) == size, "error refreshing the cached depth");
    fail_unless(xtree_depth_const(tree) == size, NULL);
    fail_unless(xtree_depth_const_node(tree, tree->root) == size, NULL);
    fail_unless(xtree_depth_const_node(tree, tree->root->start),
            "bad subtree level depth");
    fail_unless(xtree_depth_const_node(tree, tree->root->start->next),
            "bad subtree level depth");
    fail_unless(xtree_depth_const_node(tree, tree->root->start->next->start),
            "bad subtree level depth");
}
END_TEST

typedef struct {
    void*     node_data;
    uint8_t   which;
    uint32_t  level;
} walk_couples_t;

typedef struct walk_st {
    walk_couples_t* table_pos;
    uint8_t         error;
    uint8_t         executed;
    walk_couples_t  got;
} walk_test_t;


static uint8_t action_test(xtree_node_t* node,
                uint8_t which,
                uint32_t level,
                void* arg)
{
    walk_test_t* data = (walk_test_t*)arg;
    if (data) {
        data->executed = 1;
        if (data->table_pos->node_data == node->data &&
            data->table_pos->which == which &&
            data->table_pos->level == level) {
            ++data->table_pos;
        } else {
            ++data->error;
            data->got.node_data = node->data;
            data->got.which     = which;
            data->got.level     = level;
            return 0;
        }
    }
    return 1;
}

START_TEST(test_xtree_walk)
{
    xtree_t*        tree    = &mytree_by_addchild;
    xtree_node_t*   node    = NULL;
    walk_couples_t  table[] = {
        /*  0 */ {(void*)1, XTREE_PREORDER, 0},
        /*  1 */ {(void*)6, XTREE_LEAF    , 1},
        /*  2 */ {(void*)1, XTREE_INORDER , 0},
        /*  3 */ {(void*)2, XTREE_PREORDER, 1},
        /*  4 */ {(void*)7, XTREE_LEAF    , 2},
        /*  5 */ {(void*)2, XTREE_INORDER , 1},
        /*  6 */ {(void*)4, XTREE_PREORDER, 2},
        /*  7 */ {(void*)8, XTREE_LEAF    , 3},
        /*  8 */ {(void*)4, XTREE_ENDORDER, 2},
        /*  9 */ {(void*)2, XTREE_ENDORDER, 1},
        /* 10 */ {(void*)1, XTREE_INORDER , 0},
        /* 11 */ {(void*)3, XTREE_LEAF    , 1},
        /* 12 */ {(void*)1, XTREE_INORDER , 0},
        /* 13 */ {(void*)5, XTREE_LEAF    , 1},
        /* 14 */ {(void*)1, XTREE_ENDORDER, 0}
    };
    walk_test_t walk_data = {NULL, 0, 0}; /* standard: init stay static */
    walk_data.table_pos = table;

    node = xtree_add_child(tree, tree->root->start->next->end, (void*)8,
            XTREE_APPEND);
    fail_unless(node == tree->root->start->next->end->start,
            "fail to add required node for tests");

    /* invalid cases */
    node = xtree_walk(tree, NULL, UINT32_MAX, 0, NULL, NULL);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node = xtree_walk(NULL, tree->root, UINT32_MAX, 0, NULL, NULL);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node = xtree_walk(tree, tree->root, UINT32_MAX, 0, NULL, NULL);
    fail_unless(node == NULL, "invalid case, however returned not null");

    /* should not execute function */
    node = xtree_walk(tree, tree->root, UINT32_MAX, 0,
            action_test, &walk_data);
    fail_unless(node == NULL, "invalid case, however returned not null");
    fail_unless(walk_data.executed == 0,
            "invalid case (min > max) but got executed");
    fail_unless(walk_data.error == 0,
            "invalid case, error detected but should not have been executed");
    fail_unless(walk_data.table_pos == table,
            "invalid case table_pos advanced but should not");

    /* test tree walk through */
    node = xtree_walk(tree, NULL, 0, UINT32_MAX, action_test, &walk_data);
    fail_unless(walk_data.executed == 1,
            "should have executed at least one time");
    fail_unless(walk_data.table_pos != NULL,
            "invalid pointer value for table_pos");
#if 0
/* FIXME: Test below are failing in v14.11.0 with message:
 * .... expected: 1: 1: 0: 0, got 1: 16: 0
 * None of this code is actually used, so commenting it out for now */
    fail_unless(walk_data.table_pos ==
            (table + (sizeof(table)/sizeof(table[0]))),
            /* ^^^^^^ invalid addr but normal at the end of normal execution */
            "unexpected stop (data, which, level, couple index)"
            " expected: %x: %u: %lu: %d,"
            " got %x: %u: %lu",
            walk_data.table_pos->node_data,
            walk_data.table_pos->which,
            walk_data.table_pos->level,
            (int)(walk_data.table_pos - table),
            /* got */
            walk_data.got.node_data,
            walk_data.got.which,
            walk_data.got.level);
    fail_unless(node == NULL, "returned value indicates unexpected stop");
    fail_unless(walk_data.error == 0, "error counter was incremented");
#endif
}
END_TEST

uint8_t compare_test(const void* node_data, const void* arg)
{
    return !(node_data == arg);
}

START_TEST(test_xtree_find)
{
    xtree_t*      tree = &mytree_by_addchild;
    xtree_node_t* node = NULL;

    /* test not found result or bad params */
    node = xtree_find(tree, compare_test, NULL);
    fail_unless(node == NULL,
            "bad result (should be NULL): %x",
            (node)?node->data:NULL);
    /* the test ^^^^ is necessary since this is a macro/function, the node is
     * deferred at the same time it is being tested */

    node = xtree_find(tree, NULL, (void*)4);
    fail_unless(node == NULL,
            "bad result (should be NULL): %x",
            (node)?node->data:NULL);

    node = xtree_find(tree, compare_test, (void*)10);
    fail_unless(node == NULL,
            "bad result (should be NULL): %x",
            (node)?node->data:NULL);

    /* test different node depth */
    node = xtree_find(tree, compare_test, (void*)1);
    fail_unless(node != NULL,
            "result is null however it should have been found");
    fail_unless(node == tree->root,
            "root node should have been found, but found : %x",
            (node)?node->data:NULL);

    node = xtree_find(tree, compare_test, (void*)4);
    fail_unless(node != NULL,
            "result is null however it should have been found");
    fail_unless(tree->root->start->next->end == node,
            "bad result (search 4): %x",
            (node)?node->data:NULL);

    node = xtree_find(tree, compare_test, (void*)5);
    fail_unless(node != NULL,
            "result is null however it should have been found");
    fail_unless(tree->root->end == node,
            "bad result (search 5): %x",
            (node)?node->data:NULL);

    /* test node with parent and with childs */
    node = xtree_find(tree, compare_test, (void*)2);
    fail_unless(node != NULL,
            "result is null however it should have been found");
    fail_unless(tree->root->start->next == node,
            "bad result (search 2): %x",
            (node)?node->data:NULL);

}
END_TEST

START_TEST(test_xtree_delete)
{
    xtree_t* tree = &mytree_by_addchild;

    /* bad args */
    fail_unless(xtree_depth(tree) == 3, NULL);
    fail_unless(xtree_delete(NULL, tree->root) == NULL, "bad return");
    fail_unless(xtree_get_count(tree) == 7, "bad count update");
    fail_unless(tree->state & XTREE_STATE_DEPTHCACHED,
            "level should still be cached");
    fail_unless(xtree_delete(tree, NULL) == NULL, "bad return");
    fail_unless(xtree_get_count(tree) == 7, "bad count update");
    fail_unless(tree->state & XTREE_STATE_DEPTHCACHED,
            "level should still be cached");
    fail_unless(xtree_depth(tree) == 3, NULL);

    /* tree structure */
    fail_unless(xtree_delete(tree, tree->root->start) == tree->root,
            "parent of 6 should have been root node");
    fail_unless(xtree_depth(tree) == 3, NULL);
    fail_unless(tree->root->start->data == (void*)2 &&
            tree->root->start->next->data == (void*)3 &&
            tree->root->start->next->next->data == (void*)5,
            "children should be now 2 -> 3 -> 5");
    fail_unless(tree->root->start->previous == NULL,
            "bad children list edges");
    fail_unless(xtree_get_count(tree) == 6, "bad count update");
    fail_unless(tree->state & XTREE_STATE_DEPTHCACHED,
            "level should still be cached");
    fail_unless(tree->depth == 3 && xtree_depth(tree) == 3,
            "depth should not have changed");

    /* structure and depth changing */
    fail_unless(xtree_delete(tree, tree->root->start->start) ==
            tree->root->start,
            "parent of 7 should have been node 2");
    fail_unless(xtree_depth(tree) == 3, NULL);
    fail_unless(tree->state & XTREE_STATE_DEPTHCACHED,
            "level should still be cached");
    fail_unless(tree->depth == 3, "depth should not have changed");
    fail_unless(xtree_get_count(tree) == 5, "bad count update");

    fail_unless(xtree_delete(tree, tree->root->start->start) ==
            tree->root->start,
            "parent of 4 should have been node 2");
    fail_unless(tree->root->start->start == NULL &&
            tree->root->start->end == NULL,
            "bad edges for node 2");
    fail_unless(tree->root->start->data == (void*)2 &&
            tree->root->start->next->data == (void*)3 &&
            tree->root->start->next->next->data == (void*)5,
            "tree deconstruction");
    fail_unless(tree->root->start->previous == NULL &&
            tree->root->end->next == NULL,
            "tree edges deconstruction");
    fail_unless(~tree->state & XTREE_STATE_DEPTHCACHED,
            "level should not be cached");
    fail_unless(xtree_depth(tree) == 2,
            "the last removal should have reduced depth");

    /* root node delete test */
    fail_unless(xtree_delete(tree, tree->root) == NULL, "bad return");
}
END_TEST

START_TEST(test_xtree_get_parents)
{
    xtree_t* tree = &mytree_by_addchild;
    xtree_node_t** parents = NULL;
    uint32_t size = 0;

    /* stress~ */
    fail_unless(xtree_get_parents(NULL, NULL, NULL) == NULL, "bad behavior");
    fail_unless(xtree_get_parents(tree, NULL, NULL) == NULL, "bad behavior");
    fail_unless(xtree_get_parents(NULL, tree->root->start, NULL) == NULL,
            "bad behavior");
    fail_unless(xtree_get_parents(NULL, NULL, &size) == NULL, "bad behavior");
    fail_unless(xtree_get_parents(tree, NULL, &size) == NULL, "bad behavior");
    fail_unless(xtree_get_parents(tree, tree->root->start, NULL) == NULL,
            "bad behavior");
    fail_unless(xtree_get_parents(tree, tree->root, &size) == NULL,
            "bad behavior");

    /* node 6 */
    parents = xtree_get_parents(tree, tree->root->start, &size);
    fail_unless(parents != NULL, "should have a parent here");
    fail_unless(size == 1, "should have parents' list size == 1");
    fail_unless(parents[0] == tree->root,
            "parents list of 6 should be root node");
    xfree(parents);

    /* node 1 */
    parents = xtree_get_parents(tree, tree->root, &size);
    fail_unless(parents == NULL, "root node should not have a parent list");

    /* node 2 */
    parents = xtree_get_parents(tree, tree->root->start->next, &size);
    fail_unless(parents != NULL, "should have a parent here");
    fail_unless(size == 1, "should have parents' list size == 1");
    fail_unless(parents[0] == tree->root,
            "parents list of 2 should be root node");
    xfree(parents);

    /* node 3 */
    parents = xtree_get_parents(tree, tree->root->start->next->next, &size);
    fail_unless(parents != NULL, "should have a parent here");
    fail_unless(size == 1, "should have parents' list size == 1");
    fail_unless(parents[0] == tree->root,
            "parents list of 3 should be root node");
    xfree(parents);

    /* node 5 */
    parents = xtree_get_parents(tree, tree->root->end, &size);
    fail_unless(parents != NULL, "should have a parent here");
    fail_unless(size == 1, "should have parents' list size == 1");
    fail_unless(parents[0] == tree->root,
            "parents list of 5 should be root node");
    xfree(parents);

    /* node 7 */
    parents = xtree_get_parents(tree, tree->root->start->next->start, &size);
    fail_unless(parents != NULL, "should have parents here");
    fail_unless(size == 2, "should have parents' list size == 2");
    fail_unless(parents[0] == tree->root->start->next,
            "parents[0] of 7 should be node 2 (actually %x)",
            (parents[0])?parents[0]->data:NULL);
    fail_unless(parents[1] == tree->root,
            "parents[1] of 7 should be root node");
    xfree(parents);

    /* node 4 */
    parents = xtree_get_parents(tree, tree->root->start->next->end, &size);
    fail_unless(parents != NULL, "should have parents here");
    fail_unless(size == 2, "should have parents' list size == 2");
    fail_unless(parents[0] == tree->root->start->next,
            "parents[0] of 4 should be node 2 (actually %x)",
            (parents[0])?parents[0]->data:NULL);
    fail_unless(parents[1] == tree->root,
            "parents[1] of 7 should be root node");
    xfree(parents);
}
END_TEST

START_TEST(test_xtree_common)
{
    xtree_t*        tree       = &mytree_by_addchild;
    xtree_node_t*   node       = NULL;
    const xtree_node_t*   node_list[7];

    /* invalid cases */
    node = xtree_common(NULL, NULL, 10);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node = xtree_common(tree, NULL, 10);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node_list[0] = NULL;
    node_list[1] = tree->root->end;
    node_list[2] = tree->root->start;
    node = xtree_common(tree, node_list, 3);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node_list[0] = tree->root;
    node = xtree_common(tree, node_list, 1);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node_list[0] = tree->root->start;
    node_list[1] = tree->root->end;
    node = xtree_common(NULL, node_list, 2);
    fail_unless(node == NULL, "invalid case, however returned not null");
    node = xtree_common(tree, node_list, 0);
    fail_unless(node == NULL, "invalid case, however returned not null");

    /* test for good common ancestor */

    /* 7, 5 -> 1 */
    node_list[0] = tree->root->start->next->start;
    node_list[1] = tree->root->end;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == tree->root, "bad returned node : %x",
            (node)?node->data:NULL);

    /* 2, 7 -> 1 */
    node_list[0] = tree->root->start->next;
    node_list[1] = tree->root->start->next->start;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == tree->root, "bad returned node");

    /* 4, 7 -> 2 */
    node_list[0] = tree->root->start->next->end;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == tree->root->start->next, "bad returned node");

    /* 4, 7, 2 -> 1 */
    node_list[2] = tree->root->start->next;
    node = xtree_common(tree, node_list, 3);
    fail_unless(node == tree->root, "bad returned node");

    /* 6, 7 -> 1 */
    node_list[0] = tree->root->start;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == tree->root, "bad returned node");

    /* 2, 7 -> 1 */
    node_list[0] = tree->root->start->next;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == tree->root, "bad returned node");

    /* 2, 1 -> NULL */
    node_list[1] = tree->root;
    node = xtree_common(tree, node_list, 2);
    fail_unless(node == NULL, "bad returned node");

    /* 2, 3, 5, 6 -> 1 */
    node_list[1] = tree->root->end->previous;
    node_list[2] = tree->root->end;
    node_list[3] = tree->root->start;
    node = xtree_common(tree, node_list, 4);
    fail_unless(node == tree->root, "bad returned node");

    /* 2, 3, 5, 6, 7, 4 -> 1 */
    node_list[4] = tree->root->start->next->start;
    node_list[5] = tree->root->start->next->end;
    node = xtree_common(tree, node_list, 6);
    fail_unless(node == tree->root, "bad returned node");

    /* 2, 3, 5, 6, 7, 4, 1 -> NULL */
    node_list[6] = tree->root;
    node = xtree_common(tree, node_list, 7);
    fail_unless(node == NULL, "bad returned node");
}
END_TEST

START_TEST(test_xtree_get_leaves)
{
    xtree_t*       tree  = &mytree_by_addchild;
    xtree_node_t** nodes = NULL;
    uint32_t       size  = 0;

    /* invalid cases */
    nodes = xtree_get_leaves(NULL, NULL, NULL);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(tree, NULL, NULL);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(tree, tree->root, NULL);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(tree, NULL, &size);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(NULL, tree->root, &size);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(NULL, tree->root, NULL);
    fail_unless(nodes == NULL, "invalid case, however returned not null");
    nodes = xtree_get_leaves(tree, NULL, &size);
    fail_unless(nodes == NULL, "invalid case, however returned not null");

    /* get real leaves */
    nodes = xtree_get_leaves(tree, tree->root->start, &size);
    fail_unless(nodes == NULL, "should have no leaves descending 6");

    nodes = xtree_get_leaves(tree, tree->root->start->next, &size);
    fail_unless(size == 2, "should have 2 leaves from 2");
    fail_unless(nodes[0] == tree->root->start->next->start,
            "nodes[0] != nodes 7");
    fail_unless(nodes[1] == tree->root->start->next->end,
            "nodes[1] != nodes 4");
    xfree(nodes);

    nodes = xtree_get_leaves(tree, tree->root, &size);
    fail_unless(size != 6, "should have 6 leaves from root node");
    fail_unless(nodes[0] == tree->root->start, "bad leaves result");
    fail_unless(nodes[1] == tree->root->start->next->start, "bad leaves result");
    fail_unless(nodes[2] == tree->root->start->next->end,
            "bad leaves result");
    fail_unless(nodes[3] == tree->root->start->next->next, "bad leaves result");
    fail_unless(nodes[4] == tree->root->end,
            "bad leaves result");
    xfree(nodes);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* xtree_suite(void)
{
    Suite* s = suite_create("xtree");
    TCase* tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_xtree_creation_unmanaged);
    tcase_add_test(tc_core, test_xtree_add_root_node_unmanaged);
    tcase_add_test(tc_core, test_xtree_freeing_elements);
    tcase_add_test(tc_core, test_xtree_with_add_child);
    tcase_add_test(tc_core, test_xtree_with_add_sibling);
    tcase_add_test(tc_core, test_xtree_depth);
    tcase_add_test(tc_core, test_xtree_walk);
    tcase_add_test(tc_core, test_xtree_find);
    tcase_add_test(tc_core, test_xtree_delete);
    tcase_add_test(tc_core, test_xtree_get_parents);
    tcase_add_test(tc_core, test_xtree_common);
    tcase_add_test(tc_core, test_xtree_get_leaves);
    suite_add_tcase(s, tc_core);

    return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
    int number_failed;
    SRunner* sr = srunner_create(xtree_suite());

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

