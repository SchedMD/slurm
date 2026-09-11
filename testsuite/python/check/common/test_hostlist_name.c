/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test how a host name is split, folded into a range and read back out.
 *****************************************************************************/

#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* these are not in slurm.h */
void slurm_hostlist_sort(hostlist_t *hl);
int slurm_hostlist_push_host_dims(hostlist_t *hl, const char *host, int dims);
char *slurm_hostlist_shift_dims(hostlist_t *hl, int dims);

/*
 * hostlist_push_host() folds a name into the last range when it continues
 * it, so these cases pin where that fold does and does not happen.
 */
static void check_push(char **hosts, int cnt, char *expected, int expected_cnt)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char *got;

	ck_assert(hl != NULL);

	for (int i = 0; i < cnt; i++)
		ck_assert_int_eq(slurm_hostlist_push_host(hl, hosts[i]), 1);

	got = slurm_hostlist_ranged_string_xmalloc(hl);
	ck_assert_str_eq(got, expected);
	ck_assert_int_eq(slurm_hostlist_count(hl), expected_cnt);

	xfree(got);
	slurm_hostlist_destroy(hl);
}

START_TEST(push_sequential_folds)
{
	char *hosts[] = { "node1", "node2", "node3", "node4" };

	check_push(hosts, 4, "node[1-4]", 4);
}

END_TEST

START_TEST(push_single_host)
{
	char *hosts[] = { "node7" };

	check_push(hosts, 1, "node7", 1);
}

END_TEST

START_TEST(push_gap_does_not_fold)
{
	char *hosts[] = { "node1", "node3", "node5" };

	check_push(hosts, 3, "node[1,3,5]", 3);
}

END_TEST

START_TEST(push_repeated_name_kept)
{
	char *hosts[] = { "node1", "node1" };

	/* a hostlist keeps duplicates, unlike a hostset */
	check_push(hosts, 2, "node[1,1]", 2);
}

END_TEST

START_TEST(push_descending_does_not_fold)
{
	char *hosts[] = { "node3", "node2", "node1" };

	check_push(hosts, 3, "node[3,2,1]", 3);
}

END_TEST

START_TEST(push_zero_padded_folds)
{
	char *hosts[] = { "node01", "node02", "node03" };

	check_push(hosts, 3, "node[01-03]", 3);
}

END_TEST

START_TEST(push_padding_across_boundary)
{
	char *hosts[] = { "node08", "node09", "node10", "node11" };

	check_push(hosts, 4, "node[08-11]", 4);
}

END_TEST

START_TEST(push_mixed_width_not_equivalent)
{
	/* "node1" and "node02" pad differently, so they cannot share a range */
	char *hosts[] = { "node1", "node02" };

	check_push(hosts, 2, "node[1,02]", 2);
}

END_TEST

START_TEST(push_unpadded_digit_crossing)
{
	/*
	 * Widths 1 and 2. Width 1 applied to both leaves either number without
	 * padding, so the widths are equivalent and the range folds.
	 */
	char *hosts[] = { "node9", "node10" };

	check_push(hosts, 2, "node[9-10]", 2);
}

END_TEST

START_TEST(push_numeric_only_names)
{
	/* the name is all digits, so the prefix is empty */
	char *hosts[] = { "1", "2" };

	check_push(hosts, 2, "[1-2]", 2);
}

END_TEST

START_TEST(push_zero_value_folds)
{
	char *hosts[] = { "node0", "node1" };

	check_push(hosts, 2, "node[0-1]", 2);
}

END_TEST

START_TEST(push_wide_zero_padding)
{
	char *hosts[] = { "node000", "node001" };

	check_push(hosts, 2, "node[000-001]", 2);
}

END_TEST

/*
 * Push "<prefix of plen 'n'>5" and "...6" and check that they fold. Prefix
 * lengths either side of HOST_NAME_MAX are worth covering because that is
 * where a name stops fitting the buffer a caller would size for one.
 */
static void check_prefix_len_folds(int plen)
{
	char *prefix = xmalloc(plen + 1);
	char *hosts[2], *expected;

	memset(prefix, 'n', plen);

	hosts[0] = xstrdup_printf("%s5", prefix);
	hosts[1] = xstrdup_printf("%s6", prefix);
	expected = xstrdup_printf("%s[5-6]", prefix);

	check_push(hosts, 2, expected, 2);

	xfree(hosts[0]);
	xfree(hosts[1]);
	xfree(expected);
	xfree(prefix);
}

START_TEST(push_max_length_name_folds)
{
	/*
	 * HOST_NAME_MAX does not count the terminator, so a name holds
	 * HOST_NAME_MAX characters. This whole name is that long, digit
	 * included.
	 */
	check_prefix_len_folds(HOST_NAME_MAX - 1);
}

END_TEST

START_TEST(push_prefix_at_name_limit)
{
	/* a prefix exactly as long as a host name may be */
	check_prefix_len_folds(HOST_NAME_MAX);
}

END_TEST

START_TEST(push_prefix_past_name_limit)
{
	/* one character past that length still folds */
	check_prefix_len_folds(HOST_NAME_MAX + 1);
}

END_TEST

START_TEST(push_padded_cluster_names)
{
	/* the nidNNNNN style, zero padded to a fixed width */
	char *hosts[] = { "nid00001", "nid00002", "nid00003" };

	check_push(hosts, 3, "nid[00001-00003]", 3);
}

END_TEST

START_TEST(push_separator_in_prefix)
{
	/* a dash before the number is part of the prefix */
	char *hosts[] = { "cn-0041", "cn-0042" };

	check_push(hosts, 2, "cn-[0041-0042]", 2);
}

END_TEST

START_TEST(push_digits_inside_prefix)
{
	/*
	 * The prefix scan only walks back over trailing digits, so the "1"
	 * of "rack1" stays in the prefix.
	 */
	char *hosts[] = { "rack1-node1", "rack1-node2" };

	check_push(hosts, 2, "rack1-node[1-2]", 2);
}

END_TEST

START_TEST(push_largest_number_still_folds)
{
	/*
	 * The two only fold into one range if both suffixes were read as
	 * numbers, so this shows the largest number a host can have still
	 * parses. Build the names from the limit, which is not the same on
	 * every host.
	 */
	char *hosts[2], *expected;

	hosts[0] = xstrdup_printf("node%lu", ULONG_MAX - 2);
	hosts[1] = xstrdup_printf("node%lu", ULONG_MAX - 1);
	expected = xstrdup_printf("node[%lu-%lu]", ULONG_MAX - 2, ULONG_MAX - 1);

	check_push(hosts, 2, expected, 2);

	xfree(hosts[0]);
	xfree(hosts[1]);
	xfree(expected);
}

END_TEST

START_TEST(push_ulong_max_is_rejected)
{
	/* ULONG_MAX fits, but a range can not end on it */
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char *host = xstrdup_printf("node%lu", ULONG_MAX);

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_push_host(hl, host), 0);
	ck_assert_int_eq(slurm_hostlist_count(hl), 0);

	xfree(host);
	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(push_oversized_number_is_rejected)
{
	/* one past what fits, so there is no number we can hold */
	hostlist_t *hl = slurm_hostlist_create(NULL);

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_push_host(hl,
						  "node18446744073709551616"),
			 0);
	ck_assert_int_eq(slurm_hostlist_count(hl), 0);

	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(push_oversized_number_after_digits_is_kept)
{
	/*
	 * Only a suffix that is all digits can overflow. Anything after them
	 * makes the whole name the prefix, so this one is still a host.
	 */
	char *hosts[] = { "node18446744073709551616abc" };

	check_push(hosts, 1, "node18446744073709551616abc", 1);
}

END_TEST

START_TEST(push_dotted_name_not_folded_on_domain)
{
	/* a trailing domain leaves no numeric suffix to fold on */
	char *hosts[] = { "node1.example.com", "node2.example.com" };

	check_push(hosts, 2, "node1.example.com,node2.example.com", 2);
}

END_TEST

START_TEST(push_shift_round_trip)
{
	/* A folded range must give the names back in the order pushed */
	char *hosts[] = { "node1", "node2", "node3" };
	hostlist_t *hl = slurm_hostlist_create(NULL);

	ck_assert(hl != NULL);
	for (int i = 0; i < 3; i++)
		ck_assert_int_eq(slurm_hostlist_push_host(hl, hosts[i]), 1);

	for (int i = 0; i < 3; i++) {
		char *host = slurm_hostlist_shift(hl);

		ck_assert(host != NULL);
		ck_assert_str_eq(host, hosts[i]);
		free(host);
	}
	ck_assert_int_eq(slurm_hostlist_count(hl), 0);

	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(push_different_prefix_does_not_fold)
{
	char *hosts[] = { "node1", "other2" };

	check_push(hosts, 2, "node1,other2", 2);
}

END_TEST

START_TEST(push_no_numeric_suffix)
{
	char *hosts[] = { "alpha", "beta" };

	check_push(hosts, 2, "alpha,beta", 2);
}

END_TEST

START_TEST(push_suffix_then_none)
{
	char *hosts[] = { "node1", "node" };

	check_push(hosts, 2, "node1,node", 2);
}

END_TEST

START_TEST(push_long_run_folds)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char name[32], *got;

	ck_assert(hl != NULL);

	for (int i = 1; i <= 1000; i++) {
		snprintf(name, sizeof(name), "node%d", i);
		ck_assert_msg(slurm_hostlist_push_host(hl, name) == 1,
			      "push of %s failed", name);
	}

	got = slurm_hostlist_ranged_string_xmalloc(hl);
	ck_assert_str_eq(got, "node[1-1000]");
	ck_assert_int_eq(slurm_hostlist_count(hl), 1000);

	xfree(got);
	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(push_sort_then_fold)
{
	char *hosts[] = { "node3", "node1", "node2" };
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char *got;

	ck_assert(hl != NULL);

	for (int i = 0; i < 3; i++)
		ck_assert_int_eq(slurm_hostlist_push_host(hl, hosts[i]), 1);

	slurm_hostlist_sort(hl);

	got = slurm_hostlist_ranged_string_xmalloc(hl);
	ck_assert_str_eq(got, "node[1-3]");
	ck_assert_int_eq(slurm_hostlist_count(hl), 3);

	xfree(got);
	slurm_hostlist_destroy(hl);
}

END_TEST

START_TEST(push_null_host_is_rejected)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_push_host(hl, NULL), 0);
	ck_assert_int_eq(slurm_hostlist_count(hl), 0);

	slurm_hostlist_destroy(hl);
}

END_TEST

/*
 * Push each name, then look every one of them up again. The push and the
 * lookup read a host name by separate routes, so this pins that the two agree
 * on where the prefix ends and what the suffix is worth.
 */
static void check_push_then_find(char **hosts, int cnt)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);

	ck_assert(hl != NULL);

	for (int i = 0; i < cnt; i++)
		ck_assert_int_eq(slurm_hostlist_push_host(hl, hosts[i]), 1);

	for (int i = 0; i < cnt; i++)
		ck_assert_msg(slurm_hostlist_find(hl, hosts[i]) == i,
			      "%s was pushed at %d but not found there",
			      hosts[i], i);

	slurm_hostlist_destroy(hl);
}

START_TEST(find_folded_range)
{
	char *hosts[] = { "node1", "node2", "node3" };

	check_push_then_find(hosts, 3);
}

END_TEST

START_TEST(find_no_suffix)
{
	char *hosts[] = { "alpha", "node1.example.com" };

	check_push_then_find(hosts, 2);
}

END_TEST

START_TEST(find_long_prefix)
{
	/* a prefix longer than a host name may be */
	char *prefix = xmalloc(HOST_NAME_MAX + 2);
	char *hosts[2];

	memset(prefix, 'n', HOST_NAME_MAX + 1);
	hosts[0] = xstrdup_printf("%s1", prefix);
	hosts[1] = xstrdup_printf("%s2", prefix);

	check_push_then_find(hosts, 2);

	xfree(hosts[0]);
	xfree(hosts[1]);
	xfree(prefix);
}

END_TEST

START_TEST(find_oversized_number_is_not_found)
{
	/*
	 * A number too large to hold is not found, even in a list that holds
	 * the largest number a host can have.
	 */
	hostlist_t *hl = slurm_hostlist_create("node18446744073709551614");

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_find(hl, "node18446744073709551614"),
			 0);
	ck_assert_int_eq(slurm_hostlist_find(hl, "node18446744073709551616"),
			 -1);

	slurm_hostlist_destroy(hl);
}

END_TEST

/*
 * Build a hostlist from one string and compare what it gives back. The
 * tokenizer has to find a bracketed group without reading past the token it
 * is on, so these cover brackets in several positions.
 */
static void check_create(char *spec, char *expected, int expected_cnt)
{
	hostlist_t *hl = slurm_hostlist_create(spec);
	char *got;

	ck_assert(hl != NULL);

	got = slurm_hostlist_ranged_string_xmalloc(hl);
	ck_assert_str_eq(got, expected);
	ck_assert_int_eq(slurm_hostlist_count(hl), expected_cnt);

	xfree(got);
	slurm_hostlist_destroy(hl);
}

START_TEST(create_plain_list_has_no_brackets)
{
	check_create("n1,n2,n3", "n[1-3]", 3);
}

END_TEST

START_TEST(create_plain_token_before_bracket)
{
	/* the bracket belongs to the second token, not the first */
	check_create("a1,b[1-3]", "a1,b[1-3]", 4);
}

END_TEST

START_TEST(create_separator_inside_bracket)
{
	/* the comma in the brackets does not end the token */
	check_create("a[1,3-4],b1", "a[1,3-4],b1", 4);
}

END_TEST

START_TEST(create_two_bracket_groups_in_one_token)
{
	/*
	 * The scan has to go back for the second group in the same token. Only
	 * the last group stays a range, the one before it is written out.
	 */
	check_create("a[1-2]b[3-4]", "a1b[3-4],a2b[3-4]", 4);
}

END_TEST

START_TEST(create_unclosed_bracket_is_rejected)
{
	/* an open bracket with nothing to close it is not a host list */
	ck_assert(slurm_hostlist_create("a[1-2,b3") == NULL);
}

END_TEST

START_TEST(create_oversized_number_in_bracket_is_rejected)
{
	/* a number too large to hold is not a range */
	ck_assert(slurm_hostlist_create("node[18446744073709551616]") == NULL);
}

END_TEST

START_TEST(create_oversized_range_end_is_rejected)
{
	/*
	 * The low end of the range fits and the high end does not, so this is
	 * the second number of the range. Keep the span between the two short,
	 * or the range is turned down for its size before the number is read.
	 */
	ck_assert(slurm_hostlist_create(
			  "node[18446744073709551615-18446744073709551616]") ==
		  NULL);
}

END_TEST

START_TEST(create_range_to_ulong_max_is_rejected)
{
	ck_assert(slurm_hostlist_create(
			  "node[18446744073709551614-18446744073709551615]") ==
		  NULL);
}

END_TEST

START_TEST(create_oversized_number_is_rejected)
{
	/* the plain name is rejected the same as the bracketed one */
	ck_assert(slurm_hostlist_create("node18446744073709551616") == NULL);
}

END_TEST

START_TEST(create_oversized_number_in_list_is_rejected)
{
	/* one bad name is enough to reject the whole list */
	ck_assert(slurm_hostlist_create("a1,node18446744073709551616,b2") ==
		  NULL);
}

END_TEST

START_TEST(push_empty_host_is_kept)
{
	/* an empty name has no prefix and no suffix, but it is still a host */
	char *hosts[] = { "" };

	check_push(hosts, 1, "", 1);
}

END_TEST

START_TEST(push_sort_uses_natural_order)
{
	/*
	 * Prefixes sort naturally, so "rack2-" comes before "rack10-" even
	 * though it is the larger of the two byte for byte.
	 */
	char *hosts[] = { "rack10-node1", "rack2-node1" };
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char *got;

	ck_assert(hl != NULL);

	for (int i = 0; i < 2; i++)
		ck_assert_int_eq(slurm_hostlist_push_host(hl, hosts[i]), 1);

	slurm_hostlist_sort(hl);

	got = slurm_hostlist_ranged_string_xmalloc(hl);
	ck_assert_str_eq(got, "rack2-node1,rack10-node1");
	ck_assert_int_eq(slurm_hostlist_count(hl), 2);

	xfree(got);
	slurm_hostlist_destroy(hl);
}

END_TEST

/*
 * Push one name with an explicit dimension count, then take it back at the
 * same count. Reading and writing in one basis keeps the name comparable to
 * what went in, whatever base the suffix used.
 */
static void check_push_dims(char *host, int dims)
{
	hostlist_t *hl = slurm_hostlist_create(NULL);
	char *got;

	ck_assert(hl != NULL);
	ck_assert_int_eq(slurm_hostlist_push_host_dims(hl, host, dims), 1);
	ck_assert_int_eq(slurm_hostlist_count(hl), 1);

	got = slurm_hostlist_shift_dims(hl, dims);
	ck_assert(got != NULL);
	ck_assert_str_eq(got, host);

	free(got);
	slurm_hostlist_destroy(hl);
}

START_TEST(push_dims_base36_suffix)
{
	/* the suffix is as wide as dims, so its letters read in base 36 */
	check_push_dims("nodeABC", 3);
}

END_TEST

START_TEST(push_dims_narrow_suffix_is_base10)
{
	/* a suffix narrower than dims falls back to base 10 */
	check_push_dims("node12", 3);
}

END_TEST

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

extern int main(int argc, char **argv)
{
	int failures;
	SRunner *sr;
	Suite *s = suite_create("hostlist_name");
	TCase *tc_fold = tcase_create("fold");
	TCase *tc_padding = tcase_create("padding");
	TCase *tc_prefix = tcase_create("prefix");
	TCase *tc_suffix = tcase_create("suffix");
	TCase *tc_dims = tcase_create("dims");
	TCase *tc_order = tcase_create("order");
	TCase *tc_find = tcase_create("find");
	TCase *tc_create = tcase_create("create");

	tcase_add_test(tc_fold, push_single_host);
	tcase_add_test(tc_fold, push_sequential_folds);
	tcase_add_test(tc_fold, push_gap_does_not_fold);
	tcase_add_test(tc_fold, push_repeated_name_kept);
	tcase_add_test(tc_fold, push_zero_value_folds);
	tcase_add_test(tc_fold, push_long_run_folds);
	tcase_add_test(tc_fold, push_null_host_is_rejected);
	tcase_add_test(tc_fold, push_empty_host_is_kept);
	suite_add_tcase(s, tc_fold);

	tcase_add_test(tc_padding, push_zero_padded_folds);
	tcase_add_test(tc_padding, push_padding_across_boundary);
	tcase_add_test(tc_padding, push_mixed_width_not_equivalent);
	tcase_add_test(tc_padding, push_unpadded_digit_crossing);
	tcase_add_test(tc_padding, push_wide_zero_padding);
	tcase_add_test(tc_padding, push_padded_cluster_names);
	suite_add_tcase(s, tc_padding);

	tcase_add_test(tc_prefix, push_numeric_only_names);
	tcase_add_test(tc_prefix, push_separator_in_prefix);
	tcase_add_test(tc_prefix, push_digits_inside_prefix);
	tcase_add_test(tc_prefix, push_different_prefix_does_not_fold);
	tcase_add_test(tc_prefix, push_max_length_name_folds);
	tcase_add_test(tc_prefix, push_prefix_at_name_limit);
	tcase_add_test(tc_prefix, push_prefix_past_name_limit);
	suite_add_tcase(s, tc_prefix);

	tcase_add_test(tc_suffix, push_no_numeric_suffix);
	tcase_add_test(tc_suffix, push_suffix_then_none);
	tcase_add_test(tc_suffix, push_dotted_name_not_folded_on_domain);
	tcase_add_test(tc_suffix, push_largest_number_still_folds);
	tcase_add_test(tc_suffix, push_ulong_max_is_rejected);
	tcase_add_test(tc_suffix, push_oversized_number_is_rejected);
	tcase_add_test(tc_suffix, push_oversized_number_after_digits_is_kept);
	suite_add_tcase(s, tc_suffix);

	tcase_add_test(tc_dims, push_dims_base36_suffix);
	tcase_add_test(tc_dims, push_dims_narrow_suffix_is_base10);
	suite_add_tcase(s, tc_dims);

	tcase_add_test(tc_order, push_descending_does_not_fold);
	tcase_add_test(tc_order, push_shift_round_trip);
	tcase_add_test(tc_order, push_sort_then_fold);
	tcase_add_test(tc_order, push_sort_uses_natural_order);
	suite_add_tcase(s, tc_order);

	tcase_add_test(tc_find, find_folded_range);
	tcase_add_test(tc_find, find_no_suffix);
	tcase_add_test(tc_find, find_long_prefix);
	tcase_add_test(tc_find, find_oversized_number_is_not_found);
	suite_add_tcase(s, tc_find);

	tcase_add_test(tc_create, create_plain_list_has_no_brackets);
	tcase_add_test(tc_create, create_plain_token_before_bracket);
	tcase_add_test(tc_create, create_separator_inside_bracket);
	tcase_add_test(tc_create, create_two_bracket_groups_in_one_token);
	tcase_add_test(tc_create, create_unclosed_bracket_is_rejected);
	tcase_add_test(tc_create,
		       create_oversized_number_in_bracket_is_rejected);
	tcase_add_test(tc_create, create_oversized_range_end_is_rejected);
	tcase_add_test(tc_create, create_range_to_ulong_max_is_rejected);
	tcase_add_test(tc_create, create_oversized_number_is_rejected);
	tcase_add_test(tc_create, create_oversized_number_in_list_is_rejected);
	suite_add_tcase(s, tc_create);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
