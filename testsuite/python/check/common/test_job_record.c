#include <check.h>

#include "src/common/job_record.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/topology.h"

static void _setup(void)
{
	slurm_init(NULL);
	ck_assert_int_eq(topology_g_init(), SLURM_SUCCESS);
}

static void _teardown(void)
{
	topology_g_fini();
	slurm_fini();
}

static TCase *_create_tcase(void)
{
	TCase *tcase = tcase_create("priority");

	tcase_add_checked_fixture(tcase, _setup, _teardown);
	return tcase;
}

static const uint16_t protocols[] = {
	SLURM_25_05_PROTOCOL_VERSION,
	SLURM_25_11_PROTOCOL_VERSION,
	SLURM_26_05_PROTOCOL_VERSION,
	SLURM_26_11_PROTOCOL_VERSION,
};

static void _check_priority(job_record_t *job, bool for_state,
			    uint16_t protocol, uint32_t expected)
{
	buf_t *buffer = init_buf(1024);
	job_record_t *decoded = job_record_create();

	job_record_pack_common(job, for_state, buffer, protocol);
	set_buf_offset(buffer, 0);
	ck_assert_int_eq(job_record_unpack_common(decoded, buffer, protocol),
			 SLURM_SUCCESS);
	ck_assert_uint_eq(decoded->priority, expected);

	job_record_delete(decoded);
	FREE_NULL_BUFFER(buffer);
}

START_TEST(test_multi_qos_priority_reporting)
{
	slurmdb_qos_rec_t qos[2] = { 0 };
	job_record_t *job = job_record_create();
	uint16_t protocol = protocols[_i];

	job->qos_list = list_create(NULL);
	list_append(job->qos_list, &qos[0]);
	list_append(job->qos_list, &qos[1]);
	job->job_state = JOB_PENDING;
	job->priority = 10;
	job->prio_mult = xmalloc(sizeof(*job->prio_mult));
	job->prio_mult->priority_array = xcalloc(2, sizeof(uint32_t));
	job->prio_mult->priority_array[0] = 10;
	job->prio_mult->priority_array[1] = 100;

	_check_priority(job, false, protocol, 100);
	_check_priority(job, true, protocol, 10);

	/* Nonempty priorities may remain after a job is held. */
	job->priority = 0;
	_check_priority(job, false, protocol, 0);
	_check_priority(job, true, protocol, 0);

	job->priority = 40;
	job->direct_set_prio = 1;
	_check_priority(job, false, protocol, 40);

	job->direct_set_prio = 0;
	job->job_state = JOB_RUNNING;
	_check_priority(job, false, protocol, 40);

	job_record_delete(job);
}

END_TEST

extern int main(int argc, char **argv)
{
	TCase *tcase = _create_tcase();
	Suite *suite = suite_create("job_record");
	SRunner *runner;
	int failures;

	tcase_add_loop_test(tcase, test_multi_qos_priority_reporting, 0,
			    ARRAY_SIZE(protocols));
	suite_add_tcase(suite, tcase);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_VERBOSE);
	failures = srunner_ntests_failed(runner);
	srunner_free(runner);

	return failures;
}
