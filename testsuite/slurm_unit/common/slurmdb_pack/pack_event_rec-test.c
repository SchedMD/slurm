#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurmdb_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/list.h"
#include "src/common/pack.h"

START_TEST(invalid_protocol)
{
	int rc;
	uint32_t x;

	slurmdb_event_rec_t *event_rec = xmalloc(sizeof(slurmdb_event_rec_t));
	buf_t *buf = init_buf(1024);

	pack32(22, buf);
	set_buf_offset(buf, 0);

	slurmdb_event_rec_t *acr;

	slurmdb_pack_event_rec((void **)&event_rec, 0, buf);
	unpack32(&x, buf);
	rc = slurmdb_unpack_event_rec((void **)&acr, 0, buf);
	ck_assert_int_eq(rc, SLURM_ERROR);
	ck_assert(x == 22);
	free_buf(buf);
	slurmdb_destroy_event_rec(event_rec);
}
END_TEST

//	char *cluster;          /* Name of associated cluster */
//	char *cluster_nodes;    /* node list in cluster during time
//				 * period (only set in a cluster event) */
//	uint16_t event_type;    /* type of event (slurmdb_event_type_t) */
//	char *node_name;        /* Name of node (only set in a node event) */
//	time_t period_end;      /* End of period */
//	time_t period_start;    /* Start of period */
//	char *reason;           /* reason node is in state during time
//				   period (only set in a node event) */
//	uint32_t reason_uid;    /* uid of that who set the reason */
//	uint16_t state;         /* State of node during time
//				   period (only set in a node event) */
//	char *tres_str;         /* TRES touched by this event */

START_TEST(pack_min_proto_null_event_rec)
{
	int rc;
	buf_t *buf = init_buf(1024);
	slurmdb_event_rec_t pack_er = {0};

	slurmdb_pack_event_rec(NULL, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_event_rec_t *unpack_er;
	rc = slurmdb_unpack_event_rec((void **)&unpack_er, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                    == SLURM_SUCCESS);
	ck_assert(pack_er.cluster       == unpack_er->cluster);
	ck_assert(pack_er.cluster_nodes == unpack_er->cluster_nodes);
	ck_assert(pack_er.event_type    == unpack_er->event_type);
	ck_assert(pack_er.node_name     == unpack_er->node_name);
	ck_assert(pack_er.period_end    == unpack_er->period_end);
	ck_assert(pack_er.period_start  == unpack_er->period_start);
	ck_assert(pack_er.reason        == unpack_er->reason);
	ck_assert(NO_VAL                == unpack_er->reason_uid);
	ck_assert(NO_VAL                == unpack_er->state);
	ck_assert(pack_er.tres_str      == unpack_er->tres_str);

	free_buf(buf);
	slurmdb_destroy_event_rec(unpack_er);
}
END_TEST

START_TEST(pack_min_proto_event_rec)
{
	int rc;

	slurmdb_event_rec_t *pack_er = xmalloc(sizeof(slurmdb_event_rec_t));
	pack_er->cluster             = xstrdup("Joseph Butler");
	pack_er->cluster_nodes       = xstrdup("David Hume");
	pack_er->event_type          = 3;
	pack_er->node_name           = xstrdup("Baruch Spinoza");
	pack_er->period_end          = 0;
	pack_er->period_start        = 10;
	pack_er->reason              = xstrdup("Gottfried Leibniz");
	pack_er->reason_uid          = 66;
	pack_er->state               = 33;
	pack_er->tres_str            = xstrdup("Karl Marx");

	buf_t *buf = init_buf(1024);
	slurmdb_pack_event_rec(pack_er, SLURM_MIN_PROTOCOL_VERSION, buf);

	set_buf_offset(buf, 0);

	slurmdb_event_rec_t *unpack_er;
	rc = slurmdb_unpack_event_rec((void **)&unpack_er, SLURM_MIN_PROTOCOL_VERSION, buf);
	ck_assert(rc                          == SLURM_SUCCESS);
	ck_assert_str_eq(pack_er->cluster,       unpack_er->cluster);
	ck_assert_str_eq(pack_er->cluster_nodes, unpack_er->cluster_nodes);
	ck_assert(pack_er->event_type         == unpack_er->event_type);
	ck_assert_str_eq(pack_er->node_name,     unpack_er->node_name);
	ck_assert(pack_er->period_end         == unpack_er->period_end);
	ck_assert(pack_er->period_start       == unpack_er->period_start);
	ck_assert_str_eq(pack_er->reason,        unpack_er->reason);
	ck_assert(pack_er->reason_uid         == unpack_er->reason_uid);
	ck_assert(pack_er->state              == unpack_er->state);
	ck_assert_str_eq(pack_er->tres_str,      unpack_er->tres_str);

	free_buf(buf);
	slurmdb_destroy_event_rec(pack_er);
	slurmdb_destroy_event_rec(unpack_er);
}
END_TEST


/*****************************************************************************
 * TEST SUITE                                                                *
 ****************************************************************************/

Suite *suite(void)
{
	Suite *s = suite_create("Pack slurmdb_event_rec_t");
	TCase *tc_core = tcase_create("Pack slurmdb_event_rec_t");
	tcase_add_test(tc_core, invalid_protocol);
	tcase_add_test(tc_core, pack_min_proto_event_rec);
	tcase_add_test(tc_core, pack_min_proto_null_event_rec);
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
