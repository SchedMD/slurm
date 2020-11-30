#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/list.h"
#include "src/common/pack.h"

START_TEST(invalid_protocol)
{
	int rc;
	uint32_t x;

	slurmdb_event_cond_t *cond_rec = xmalloc(sizeof(slurmdb_event_cond_t));
	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_event_cond_t *acr;

	slurmdb_pack_event_cond((void **)&cond_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_event_cond((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_event_cond(cond_rec);
}
END_TEST

static void _init_event_cond(slurmdb_event_cond_t *pack)
{
	pack->cpus_max = 1;
	pack->cpus_min = 2;
	pack->event_type = 3;
	pack->period_end = 5;
	pack->period_start = 6;

	pack->node_list = xstrdup("node1,node2");

	pack->cluster_list = list_create(xfree_ptr);
	list_append(pack->cluster_list, "cluster1");
	list_append(pack->cluster_list, "cluster2");

	pack->format_list = list_create(xfree_ptr);
	list_append(pack->format_list, "format1");
	list_append(pack->format_list, "format2");

	pack->reason_list = list_create(xfree_ptr);
	list_append(pack->reason_list, "reason1");
	list_append(pack->reason_list, "reason2");

	pack->reason_uid_list = list_create(xfree_ptr);
	list_append(pack->reason_uid_list, "uid1");
	list_append(pack->reason_uid_list, "uid2");

	pack->state_list = list_create(xfree_ptr);
	list_append(pack->state_list, "state1");
	list_append(pack->state_list, "state2");
}

static void _test_list_str_eq(List a, List b)
{
	char *str;

	if (!a && !b)
		return;

	ck_assert(a);
	ck_assert(b);

	ck_assert(list_count(a) == list_count(b));

	ListIterator itr_a = list_iterator_create(a);
	while ((str = list_next(itr_a)))
		ck_assert(list_find_first(b, slurm_find_char_in_list, str));
}

static void _test_cond_eq(uint16_t protocol_version,
			 slurmdb_event_cond_t *pack)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_pack_event_cond(pack, protocol_version, buf);
	set_buf_offset(buf, 0);

	slurmdb_event_cond_t *unpack;
	rc = slurmdb_unpack_event_cond((void **)&unpack, protocol_version, buf);
	ck_assert(rc == SLURM_SUCCESS);
	ck_assert(pack->cpus_max == unpack->cpus_max);
	ck_assert(pack->cpus_min == unpack->cpus_min);
	ck_assert(pack->event_type == unpack->event_type);
	ck_assert(pack->period_end == unpack->period_end);
	ck_assert(pack->period_start == unpack->period_start);

	ck_assert_str_eq(pack->node_list, unpack->node_list);

	_test_list_str_eq(pack->cluster_list, unpack->cluster_list);
	/**
	 * The unpack for format_list actually won't create a list if there are
	 * 0 count in the list.
	 */
	_test_list_str_eq(pack->format_list, unpack->format_list);
	_test_list_str_eq(pack->reason_list, unpack->reason_list);
	_test_list_str_eq(pack->reason_uid_list, unpack->reason_uid_list);
	_test_list_str_eq(pack->state_list, unpack->state_list);

	free_buf(buf);
	slurmdb_destroy_event_cond(unpack);
}

static void _run_test(uint16_t protocol_version)
{
	slurmdb_event_cond_t pack = {0};

	_init_event_cond(&pack);
	_test_cond_eq(protocol_version, &pack);
}

START_TEST(pack_current_event_cond)
{
	_run_test(SLURM_PROTOCOL_VERSION);
}
END_TEST

START_TEST(pack_last_event_cond)
{
	_run_test(SLURM_ONE_BACK_PROTOCOL_VERSION);
}
END_TEST

START_TEST(pack_min_event_cond)
{
	_run_test(SLURM_MIN_PROTOCOL_VERSION);
}
END_TEST

/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("Pack slurmdb_event_cond_t");
	TCase *tc_core = tcase_create("Pack slurmdb_event_cond_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_current_event_cond);
	tcase_add_test(tc_core, pack_last_event_cond);
	tcase_add_test(tc_core, pack_min_event_cond);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(suite());

	//srunner_set_fork_status(sr, CK_NOFORK);

	srunner_run_all(sr, CK_VERBOSE);
	//srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
