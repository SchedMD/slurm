/*****************************************************************************\
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
#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/xhash.h"
#include "src/common/xmalloc.h"

/* FIXME: how to check memory leaks with valgrind ? (to check if xhash_free
 * does free all structures correctly). */

/*****************************************************************************
 * DEFINITIONS
 *****************************************************************************/

typedef struct hashable_st {
	char id[255];
	uint32_t idn;
} hashable_t;

void hashable_identify(void* voiditem, const char** key, uint32_t* key_len)
{
	hashable_t* item = (hashable_t*)voiditem;
	*key = item->id;
	*key_len = strlen(item->id);
}

/*****************************************************************************
 * FIXTURE                                                                   *
 *****************************************************************************/

xhash_t* g_ht = NULL;
hashable_t g_hashables[200];
uint32_t g_hashableslen = sizeof(g_hashables)/sizeof(g_hashables[0]);

static void setup(void)
{
	int i;
	g_ht = xhash_init(hashable_identify, NULL);
	if (!g_ht) return; /* fatal error, will be detected by test cases */
	for (i = 0; i < g_hashableslen; ++i) {
		snprintf(g_hashables[i].id, sizeof(g_hashables[i].id), "%d", i);
		g_hashables[i].idn = i;
		/* it is an error if xhash_add returns null but it will be
		 * detected by test cases */
		if (!xhash_add(g_ht, g_hashables + i)) return;
	}
}

static void teardown(void)
{
	xhash_free(g_ht);
}

/*****************************************************************************
 * UNIT TESTS                                                                *
 ****************************************************************************/

START_TEST(test_init_free)
{
	xhash_t* ht = NULL;

	mark_point();

	/* invalid case */
	ht = xhash_init(NULL, NULL);
	fail_unless(ht == NULL, "allocated table without identifying function");

	/* alloc and free */
	ht = xhash_init(hashable_identify, NULL);
	fail_unless(ht != NULL, "hash table was not allocated");
	xhash_free(ht);
}
END_TEST

START_TEST(test_add)
{
	xhash_t* ht = NULL;
	hashable_t a[4] = {{"0", 0}, {"1", 1}, {"2", 2}, {"3", 3}};
	int i, len = sizeof(a)/sizeof(a[0]);
	char buffer[255];
	ht = xhash_init(hashable_identify, NULL);
	fail_unless(xhash_add(NULL, a) == NULL, "invalid cases not null");
	fail_unless(xhash_add(ht, NULL) == NULL, "invalid cases not null");
	fail_unless(xhash_add(ht, a)   != NULL, "xhash_add failed");
	fail_unless(xhash_add(ht, a+1) != NULL, "xhash_add failed");
	fail_unless(xhash_add(ht, a+2) != NULL, "xhash_add failed");
	fail_unless(xhash_add(ht, a+3) != NULL, "xhash_add failed");
	for (i = 0; i < len; ++i) {
		snprintf(buffer, sizeof(buffer), "%d", i);
		fail_unless(xhash_get_str(ht, buffer) == (a + i),
				"bad hashable item returned");
	}
	xhash_free(ht);
}
END_TEST

START_TEST(test_find)
{
	xhash_t* ht = g_ht;
	char buffer[255];
	int i;

	/* test bad match */
	fail_unless(xhash_get_str(ht, "bad") == NULL  , "invalid case not null");
	fail_unless(xhash_get_str(ht, "-1") == NULL   , "invalid case not null");
	fail_unless(xhash_get_str(ht, "10000") == NULL, "invalid case not null");

	/* test all good indexes */
	for (i = 0; i < g_hashableslen; ++i) {
		snprintf(buffer, sizeof(buffer), "%d", i);
		fail_unless(xhash_get_str(ht, buffer) == (g_hashables + i),
				"bad hashable item returned");
	}
}
END_TEST

/* returns the number of item deleted from the hash table */
static int test_delete_helper()
{
	xhash_t* ht = g_ht;
	int ret = 0;
	int i;
	char buffer[255];
	for (i = 0; i < g_hashableslen; ++i) {
		snprintf(buffer, sizeof(buffer), "%d", i);
		if (xhash_get_str(ht, buffer) != (g_hashables + i)) {
			++ret;
		}
	}
	return ret;
}

START_TEST(test_delete)
{
	xhash_t* ht = g_ht;
	int result;
	char buffer[255];

	/* invalid cases */
	xhash_delete_str(NULL, "1");
	fail_unless(xhash_get_str(ht, "1") != NULL, "invalid case null");
	/* Deleting non-existent item should do nothing. */
	xhash_delete(ht, NULL, 0);
	fail_unless(xhash_count(ht) == g_hashableslen,
			"invalid delete has been done");
	result = test_delete_helper();
	fail_unless(result == 0,
			"no item should have been deleted, but %d were deleted",
			result);

	/* test correct deletion */
	xhash_delete_str(ht, "10");
	fail_unless(xhash_get_str(ht, "10") == NULL, "item not deleted");
	fail_unless(xhash_count(ht) == (g_hashableslen-1), "bad count");
	/* left edge */
	xhash_delete_str(ht, "0");
	fail_unless(xhash_get_str(ht, "0") == NULL, "item not deleted");
	fail_unless(xhash_count(ht) == (g_hashableslen-2), "bad count");
	/* right edge */
	snprintf(buffer, sizeof(buffer), "%u", (g_hashableslen-2));
	xhash_delete_str(ht, buffer);
	fail_unless(xhash_get_str(ht, "0") == NULL, "item not deleted");
	fail_unless(xhash_count(ht) == (g_hashableslen-3), "bad count");

	result = test_delete_helper();
	fail_unless(result == 3, "bad number of items were deleted: %d",
			result);
}
END_TEST

START_TEST(test_count)
{
	xhash_t* ht = g_ht;
	hashable_t a[4] = {{"0", 0}, {"1", 1}, {"2", 2}, {"3", 3}};
	fail_unless(xhash_count(ht) == g_hashableslen,
		"invalid count (fixture table)");
	ht = xhash_init(hashable_identify, NULL);
	xhash_add(ht, a);
	xhash_add(ht, a+1);
	xhash_add(ht, a+2);
	xhash_add(ht, a+3);
	fail_unless(xhash_count(ht) == 4, "invalid count (fresh table)");
	xhash_free(ht);
}
END_TEST

static void test_walk_helper_callback(void* item, void* arg)
{
	hashable_t* hashable = (hashable_t*)item;
	hashable->idn = UINT32_MAX;
}

START_TEST(test_walk)
{
	xhash_t* ht = g_ht;
	int i;
	xhash_walk(ht, test_walk_helper_callback, NULL);
	for (i = 0; i < g_hashableslen; ++i) {
		fail_unless(g_hashables[i].idn == UINT32_MAX,
				"hashable item was not walked over");
	}
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite* xhash_suite(void)
{
	Suite* s = suite_create("xhash");
	TCase* tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_init_free);
	tcase_add_test(tc_core, test_add);
	tcase_add_test(tc_core, test_find);
	tcase_add_test(tc_core, test_delete);
	tcase_add_test(tc_core, test_count);
	tcase_add_test(tc_core, test_walk);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
    int number_failed;
    SRunner* sr = srunner_create(xhash_suite());

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
