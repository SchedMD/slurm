/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test that hostlist_find() and the hostset lookups report the right host.
 *****************************************************************************/

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* these are not in slurm.h */
int slurm_hostlist_delete_host(hostlist_t *, const char *);
hostset_t *slurm_hostset_create(const char *);
void slurm_hostset_destroy(hostset_t *);
int slurm_hostset_find(hostset_t *, const char *);
int slurm_hostset_within(hostset_t *, const char *);

static void _free_host(char *host)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Host names are allocated with xmalloc() since 26.11 (!4768) */
	xfree(host);
#else
	free(host);
#endif
}

/*
 * The index a literal walk of the expanded list gives, which is what
 * hostlist_find() is supposed to return.  -1 when absent.
 */
static int _truth_index(const char *list, const char *host)
{
	hostlist_t *hl = slurm_hostlist_create(list);
	char *p;
	int i = 0, ret = -1;

	ck_assert(hl != NULL);

	while ((p = slurm_hostlist_shift(hl))) {
		if (!xstrcmp(p, host)) {
			_free_host(p);
			ret = i;
			break;
		}
		_free_host(p);
		i++;
	}
	slurm_hostlist_destroy(hl);

	return ret;
}

static void _assert_find(const char *list, const char *host, int expected)
{
	hostlist_t *hl = slurm_hostlist_create(list);
	hostset_t *hs = slurm_hostset_create(list);
	int found, set_found, within;

	ck_assert(hl != NULL);
	ck_assert(hs != NULL);

	/* the expectation has to agree with a literal walk of the list */
	ck_assert_int_eq(_truth_index(list, host), expected);

	found = slurm_hostlist_find(hl, host);
	set_found = slurm_hostset_find(hs, host);
	within = slurm_hostset_within(hs, host);

	ck_assert_msg(found == expected, "hostlist_find(%s, %s) = %d, want %d",
		      list, host, found, expected);
	if (!strchr(list, ',')) {
		/* a single list is already sorted, so the index must agree */
		ck_assert_msg(set_found == expected,
			      "hostset_find(%s, %s) = %d, want %d", list, host,
			      set_found, expected);
	} else {
		/* hostset is sorted and deduped, so only the sign holds */
		ck_assert_msg((set_found < 0) == (expected < 0),
			      "hostset_find(%s, %s) = %d, want %s", list, host,
			      set_found, (expected < 0) ? "absent" : "present");
	}
	ck_assert_msg(!within == (expected < 0),
		      "hostset_within(%s, %s) = %d, want %d", list, host,
		      within, (expected >= 0));

	slurm_hostlist_destroy(hl);
	slurm_hostset_destroy(hs);
}

START_TEST(find_plain_member)
{
	_assert_find("node[0000-0099]", "node0000", 0);
	_assert_find("node[0000-0099]", "node0042", 42);
	_assert_find("node[0000-0099]", "node0099", 99);
	_assert_find("host[1-3],host5", "host5", 3);
	_assert_find("node1,node[0000-0009],nodegpu1", "node1", 0);
	_assert_find("node1,node[0000-0009],nodegpu1", "node0005", 6);
}

END_TEST

START_TEST(find_leading_zero_member)
{
	/* a range prefix may carry the leading zeros of its own suffix */
	_assert_find("nid0000[2-7]", "nid00002", 0);
	_assert_find("nid0000[2-7]", "nid00007", 5);
	_assert_find("node00[00-99]", "node0042", 42);
	_assert_find("nb1[0-5]", "nb13", 3);
	_assert_find("node0000[0-9]", "node00005", 5);
}

END_TEST

START_TEST(find_plain_miss)
{
	/* out of range, wrong width, or simply not present */
	_assert_find("node[0000-0099]", "node0100", -1);
	_assert_find("node[0000-0099]", "node42", -1);
	_assert_find("node[0001-0099]", "node0000", -1);
	_assert_find("nid0000[2-7]", "nid000002", -1);
	_assert_find("nid0000[2-7]", "nid2", -1);
}

END_TEST

START_TEST(find_absent_shared_prefix)
{
	/* letters where the range has digits are not a numeric suffix */
	_assert_find("node[0000-0099]", "nodegpu1", -1);
	_assert_find("node[0000-0099]", "nodeabc5", -1);
	_assert_find("node[00-99]", "nodeA1", -1);
	_assert_find("compute[00-99]", "computeA1", -1);
	_assert_find("abc[0000-0009]", "abcdef1", -1);
	_assert_find("abc[000-999]", "abcd12", -1);
}

END_TEST

START_TEST(find_absent_swallowed_suffix)
{
	/* the prefix must not swallow the whole suffix and leave none */
	_assert_find("node0000[0-9]", "node0000", -1);
	_assert_find("nb1[0-5]", "nb1", -1);
}

END_TEST

START_TEST(delete_absent_is_noop)
{
	hostlist_t *hl = slurm_hostlist_create("node[0000-0099]");
	hostlist_t *hl2 = slurm_hostlist_create("node0000[0-9]");

	ck_assert(hl != NULL);
	ck_assert(hl2 != NULL);

	/* an absent host that shares the prefix must not delete a member */
	ck_assert_int_eq(slurm_hostlist_delete_host(hl, "nodegpu1"), 0);
	ck_assert_int_eq(slurm_hostlist_count(hl), 100);
	ck_assert_int_eq(slurm_hostlist_find(hl, "node0000"), 0);

	ck_assert_int_eq(slurm_hostlist_delete_host(hl2, "node0000"), 0);
	ck_assert_int_eq(slurm_hostlist_count(hl2), 10);
	ck_assert_int_eq(slurm_hostlist_find(hl2, "node00000"), 0);

	slurm_hostlist_destroy(hl);
	slurm_hostlist_destroy(hl2);
}

END_TEST

START_TEST(within_multiple_hosts)
{
	hostset_t *hs = slurm_hostset_create("node[0000-0099]");
	hostset_t *hs2 = slurm_hostset_create("node1,node[0000-0009],nodegpu1");

	ck_assert(hs != NULL);
	ck_assert(hs2 != NULL);

	/* every host has to be in the set, and an absent one must say no */
	ck_assert_int_eq(slurm_hostset_within(hs, "node0001,nodegpu1"), 0);
	ck_assert_int_eq(slurm_hostset_within(hs, "node[0001-0003]"), 1);
	ck_assert_int_eq(slurm_hostset_within(hs2, "nodegpu1,node1"), 1);

	slurm_hostset_destroy(hs);
	slurm_hostset_destroy(hs2);
}

END_TEST

START_TEST(find_member_among_decoys)
{
	/* a genuine member keeps its own index, not a decoy's */
	_assert_find("node1,node[0000-0009],nodegpu1", "nodegpu1", 11);
	_assert_find("node1,node[0000-0009],nodegpu1", "nodegpu2", -1);
}

END_TEST

START_TEST(find_resplit_no_leak)
{
	/* a range that cannot match must not spoil a later one that can */
	_assert_find("nid0000[8-9],nid[00002-00007]", "nid00002", 2);
}

END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *make_find_suite(void)
{
	Suite *s = suite_create("hostlist_find");
	TCase *tc_member = tcase_create("member");
	TCase *tc_absent = tcase_create("absent");

	tcase_add_test(tc_member, find_plain_member);
	tcase_add_test(tc_member, find_leading_zero_member);
	tcase_add_test(tc_member, find_plain_miss);
	tcase_add_test(tc_member, find_member_among_decoys);
	tcase_add_test(tc_member, find_resplit_no_leak);
	suite_add_tcase(s, tc_member);

	tcase_add_test(tc_absent, find_absent_shared_prefix);
	tcase_add_test(tc_absent, find_absent_swallowed_suffix);
	tcase_add_test(tc_absent, delete_absent_is_noop);
	tcase_add_test(tc_absent, within_multiple_hosts);
	suite_add_tcase(s, tc_absent);

	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

extern int main(int argc, char **argv)
{
	int number_failed;
	SRunner *sr = srunner_create(make_find_suite());

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return number_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
