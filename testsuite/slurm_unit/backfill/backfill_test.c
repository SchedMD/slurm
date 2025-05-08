#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backfill.h"
#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/job_features.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/serializer.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include <check.h>

#ifndef HAVE_FRONT_END

diag_stats_t slurmctld_diag_stats;

void *acct_db_conn = NULL;
uint32_t cluster_cpus = NO_VAL;
list_t *job_list = NULL;
list_t *resume_job_list = NULL;
time_t last_job_update = (time_t) 0;
time_t last_part_update = (time_t) 0;
time_t last_node_update = (time_t) 0;
time_t last_resv_update = (time_t) 0;
int slurmctld_tres_cnt = 4;
uint16_t accounting_enforce = 0;
int active_node_record_count;
slurm_conf_t slurm_conf;
node_record_t **node_record_table_ptr;
list_t *part_list;
list_t *resv_list = NULL;
int node_record_count;
slurmctld_config_t slurmctld_config;
uint32_t max_powered_nodes = NO_VAL;
bool preempt_send_user_signal = false;
int sched_interval = 60;
int batch_sched_delay = 3;
bool disable_remote_singleton = false;
int max_depend_depth = 10;
bool cloud_dns = false;
uint32_t validate_resv_cnt = 0;

bitstr_t *asap_node_bitmap = NULL; /* bitmap of rebooting asap nodes */
bitstr_t *avail_node_bitmap = NULL; /* bitmap of available nodes */
bitstr_t *bf_ignore_node_bitmap = NULL; /* bitmap of nodes to ignore during a
					 * backfill cycle */
bitstr_t *booting_node_bitmap = NULL; /* bitmap of booting nodes */
bitstr_t *cg_node_bitmap = NULL; /* bitmap of completing nodes */
bitstr_t *cloud_node_bitmap = NULL; /* bitmap of cloud nodes */
bitstr_t *external_node_bitmap = NULL; /* bitmap of external nodes */
bitstr_t *future_node_bitmap = NULL; /* bitmap of FUTURE nodes */
bitstr_t *idle_node_bitmap = NULL; /* bitmap of idle nodes */
bitstr_t *power_down_node_bitmap = NULL; /* bitmap of powered down nodes */
bitstr_t *rs_node_bitmap = NULL; /* bitmap of resuming nodes */
bitstr_t *share_node_bitmap = NULL; /* bitmap of sharable nodes */
bitstr_t *up_node_bitmap = NULL; /* bitmap of non-down nodes */
bitstr_t *power_up_node_bitmap = NULL; /* bitmap of power_up requested nodes */

pthread_mutex_t check_bf_running_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	char *testcases;
	char *configdir;
} backfilltest_opts_t;

static backfilltest_opts_t params;

static void _free_options(void);
static void _help_msg(void);
static int _set_options(const int argc, char **argv);

static void _init_opts(void)
{
	memset(&params, 0, sizeof(backfilltest_opts_t));
}

static int _set_options(int argc, char **argv)
{
	int c;

	_init_opts();

	while ((c = getopt(argc, argv, "c:t:U")) != EOF) {
		switch (c) {
		case 'c':
			params.configdir = xstrdup(optarg);
			break;
		case 't':
			params.testcases = xstrdup(optarg);
			break;
		case 'U':
			_help_msg();
			exit(1);
			break;
		default:
			exit(1);
		}
	}

	return 0;
}

/* _check_params()
 */
static int _check_params(void)
{
	char *conf_path;
	char *src_dir = getenv("srcdir");

	if (params.configdir) {
		conf_path = xstrdup_printf("%s/slurm.conf", params.configdir);
	} else if (src_dir) {
		conf_path = xstrdup_printf("%s/slurm.conf", src_dir);
	} else {
		conf_path = xstrdup("slurm.conf");
	}

	setenv("SLURM_CONF", conf_path, 1);
	xfree(conf_path);

	return 0;
}

static void _help_msg(void)
{
	info("\
Usage backfill-test [<OPTION>]\n"
"\n"
"Valid <OPTION> values are:\n"
" -c     Path to a directory with slurm config files.\n"
" -t     Path to a file containing test cases.\n"
" -U     Display brief usage message\n"
"backfill-test can run in two modes:pre-set libcheck tests or\n"
"as a backfill emulator when the '-t' option is used.\n");
}

/* _free_options()
 */
static void _free_options(void)
{
	xfree(params.testcases);
	xfree(params.configdir);
}

/* this will leak memory, but we don't care really */
static void _list_delete_job(void *job_entry)
{
	job_record_t *job_ptr = (job_record_t *) job_entry;

	xfree(job_ptr);
}

static int _print_job(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	bitstr_t *tmp_bitmap;
	uint32_t *now = arg;

	printf("Job_Id=%u %s ", job_ptr->job_id,
	       job_state_string(job_ptr->job_state));

	if (job_ptr->sched_nodes && IS_JOB_PENDING(job_ptr)) {
		node_name2bitmap(job_ptr->sched_nodes, false, &tmp_bitmap,
				 NULL);
		bit_not(tmp_bitmap);
		printf("planned on %s start_time:+%ld fragmentation:%u\n",
		       job_ptr->sched_nodes, job_ptr->start_time - *now,
		       topology_g_get_fragmentation(tmp_bitmap));
		FREE_NULL_BITMAP(tmp_bitmap);
	} else if (IS_JOB_RUNNING(job_ptr)) {
		char *tmp_str = bitmap2node_name(job_ptr->node_bitmap);

		tmp_bitmap = bit_copy(job_ptr->node_bitmap);
		bit_not(tmp_bitmap);
		printf("on %s end_time:+%ld fragmentation:%u\n",
		       tmp_str, job_ptr->end_time - *now,
		       topology_g_get_fragmentation(tmp_bitmap));
		FREE_NULL_BITMAP(tmp_bitmap);
	} else {
		printf(" no planned\n");
	}

	fflush(stdout);

	return 0;
}

job_record_t *__add_job(uint32_t job_id, uint32_t priority, uint32_t nodes,
			uint32_t time_limit, char *licenses)
{
	static uint32_t last_job_id = 0;

	if (!job_id) {
		job_id = ++last_job_id;
	} else
		last_job_id = MAX(last_job_id, job_id);

	job_record_t *job_ptr = job_record_create();

	job_ptr->priority = priority;
	job_ptr->job_id = job_id;
	job_ptr->partition = xstrdup("test");
	job_ptr->part_ptr = find_part_record("test");
	job_ptr->state_reason = WAIT_NO_REASON;
	job_ptr->details->min_nodes = nodes;
	job_ptr->details->max_nodes = nodes;
	job_ptr->details->num_tasks = nodes;
	job_ptr->details->min_cpus = nodes;
	job_ptr->details->max_cpus = NO_VAL;
	job_ptr->details->cpus_per_task = 1;
	job_ptr->details->task_dist = SLURM_DIST_CYCLIC;
	job_ptr->details->share_res = 1;
	job_ptr->details->whole_node = 0;
	job_ptr->time_limit = time_limit;
	job_ptr->best_switch = true;
	job_ptr->limit_set.tres = xcalloc(slurmctld_tres_cnt, sizeof(uint16_t));
	job_ptr->tres_req_cnt = xcalloc(slurmctld_tres_cnt, sizeof(uint64_t));
	job_ptr->tres_req_cnt[TRES_ARRAY_NODE] = job_ptr->details->min_nodes;
	job_ptr->tres_req_cnt[TRES_ARRAY_MEM] = 1;
	job_ptr->tres_req_cnt[TRES_ARRAY_CPU] = job_ptr->details->min_cpus;

	if (licenses) {
		bool valid = true;
		job_ptr->license_list = license_validate(licenses, true, true,
							 true, NULL, &valid);
		job_ptr->licenses = xstrdup(licenses);
	}

	list_append(job_list, job_ptr);

	return job_ptr;
}

void load_test()
{
	char buffer[256];
	FILE *f = fopen(params.testcases, "r");

	if (f == NULL) {
		return;
	}

	while (fgets(buffer, 256, f)) {
		char *p;
		uint32_t job_id, priority, nodes, time_limit;
		if ((buffer[0] == '#') || (buffer[0] == '\n'))
			continue;
		job_id = strtoul(buffer, &p, 10);
		priority = strtoul(p, &p, 10);
		nodes = strtoul(p, &p, 10);
		time_limit = strtoul(p, &p, 10);

		__add_job(job_id, priority, nodes, time_limit, p);
	}
	fclose(f);
}

/*
 * Test simple backfill situation
 *
 * 1st job uses 1 node
 * 2nd job requests all nodes and can't run.
 * 3rd job requests remaining 31 nodes and gets backfilled.
 */
START_TEST(test_backfill_1)
{
	job_record_t *job_ptr;
	uint32_t now = time(NULL);

	/* job_id, priority, nodes, time_limit, licenses */
	__add_job(0, 10, 1, 10, NULL);
	__add_job(0, 5, 32, 10, NULL);
	__add_job(0, 1, 31, 5, NULL);

	__attempt_backfill();

	list_for_each(job_list, _print_job, &now);

	job_ptr = find_job_record(1);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 1 RUNNING");

	job_ptr = find_job_record(2);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 2 !RUNNING");

	job_ptr = find_job_record(3);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 3 RUNNING");
}
END_TEST

/*
 * Test for starving jobs described in scenario in Ticket 20847
 *
 * Bigger, lower priority jobs were jumping ahead of smaller, higher priority
 * jobs.
 *
 * Higher priority jobs had no start time.
 */
START_TEST(test_backfill_2)
{
	uint32_t now = time(NULL);

	/* job_id, priority, nodes, time_limit, licenses */
	__add_job(0, 10, 6, 10, NULL);
	__add_job(0, 9, 27, 15, NULL);
	__add_job(0, 8, 28, 14, NULL);
	__add_job(0, 7, 29, 13, NULL);
	__add_job(0, 6, 30, 12, NULL);
	__add_job(0, 5, 5, 10, NULL);
	__add_job(0, 5, 5, 10, NULL);
	/* This job would jump ahead of the priority 6 job */
	__add_job(0, 1, 30, 11, NULL);

	__attempt_backfill();
	list_for_each(job_list, _print_job, &now);

	for (int i = 1; i < 9; i++) {
		for (int j = 1; j < 9; j++) {
			job_record_t *job1_ptr = find_job_record(i);
			job_record_t *job2_ptr = find_job_record(j);
			if (!job1_ptr || !job2_ptr)
				continue;
			if ((job1_ptr->priority > job2_ptr->priority) &&
			    (job1_ptr->details->min_nodes <=
			     job2_ptr->details->min_nodes) &&
			    (job2_ptr->start_time) &&
			    (!job1_ptr->start_time ||
			     (job1_ptr->start_time > job2_ptr->start_time)))
				ck_abort_msg("Wrong backfill order");
		}
	}
}
END_TEST

/*
 * Test BF_MAX_JOB_TEST (default=500)
 *
 * Submit 1000 jobs.
 * Check if 500 jobs are backfilled.
 * 501th job shouldn't have a start time.
 */
START_TEST(test_backfill_3)
{
	job_record_t *job1_ptr;
	job_record_t *job2_ptr;

	for (int i = 0; i < 1000; i++) {
		/* job_id, priority, nodes, time_limit, licenses */
		__add_job(0, 10, 6, 10, NULL);
	}

	__attempt_backfill();

	job1_ptr = find_job_record(500);
	job2_ptr = find_job_record(501);
	ck_assert_msg((job1_ptr->start_time && !job2_ptr->start_time),
		      "Completed testing 500 (bf_max_job_test) jobs");

	/*
	 * uint32_t now = time(NULL);
	 * list_for_each(job_list, _print_job, &now);
	 */
}
END_TEST

/*
 * Test basic simplest backfiling of licences
 */
START_TEST(test_backfill_lic_1)
{
	uint32_t now = time(NULL);
	job_record_t *job_ptr;

	/* job_id, priority, nodes, time_limit	*/
	__add_job(1, 10, 1, 10, "lic1");
	__add_job(2, 9, 1, 10, "lic1");
	__add_job(3, 8, 1, 10, "lic1");
	__add_job(4, 7, 1, 10, NULL);

	__attempt_backfill();
	list_for_each(job_list, _print_job, &now);

	job_ptr = find_job_record(1);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 1 RUNNING");

	job_ptr = find_job_record(2);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 2 !RUNNING");

	job_ptr = find_job_record(3);
	ck_assert_msg(!IS_JOB_RUNNING(job_ptr), "Job 3 !RUNNING");

	job_ptr = find_job_record(4);
	ck_assert_msg(IS_JOB_RUNNING(job_ptr), "Job 4 RUNNING");
}

END_TEST

/*
 * Test for wrong start_time scenario in Issue 50271
 */
START_TEST(test_backfill_lic_2)
{
	uint32_t now = time(NULL);
	job_record_t *job1_ptr, *job2_ptr;
	part_record_t *part_ptr = find_part_record("test");

	part_ptr->max_share = 1;

	for (int i = 0; i < 12; i++) {
		/* job_id, priority, nodes, time_limit	*/
		__add_job(0, 10, 1, 10, "lic2");
	}
	__attempt_backfill();
	list_for_each(job_list, _print_job, &now);

	job1_ptr = find_job_record(7);
	job2_ptr = find_job_record(12);

	if (job1_ptr->start_time != job2_ptr->start_time)
		ck_abort_msg("Wrong start_time");
}

END_TEST

#endif

int main(int argc, char *argv[])
{
	int number_failed = 0;
#ifndef HAVE_FRONT_END
	part_record_t *part_ptr = part_record_create();

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_INFO;
	log_init("backfill-test", log_opts, 0, NULL);

	_set_options(argc, argv);

	_check_params();
	slurm_init(NULL);
	select_g_init();
	init_node_conf();
	build_all_nodeline_info(true, 0);
	serializer_g_init(NULL, NULL);
	switch_g_init(true);
	topology_g_init();
	topology_g_build_config();

	avail_node_bitmap = bit_alloc(node_record_count);
	bit_not(avail_node_bitmap);

	part_list = list_create(NULL);
	part_ptr->name = xstrdup("test");
	part_ptr->node_bitmap = bit_copy(avail_node_bitmap);
	part_ptr->max_share = 0;
	list_append(part_list, part_ptr);

	select_g_node_init();
	node_features_g_init();
	jobcomp_g_init();

	asap_node_bitmap = bit_alloc(node_record_count);
	rs_node_bitmap = bit_alloc(node_record_count);
	cg_node_bitmap = bit_alloc(node_record_count);
	external_node_bitmap = bit_alloc(node_record_count);
	power_down_node_bitmap = bit_alloc(node_record_count);
	booting_node_bitmap = bit_alloc(node_record_count);

	bf_ignore_node_bitmap = bit_alloc(node_record_count);

	up_node_bitmap = bit_copy(avail_node_bitmap);
	share_node_bitmap = bit_copy(avail_node_bitmap);
	idle_node_bitmap = bit_copy(avail_node_bitmap);
	power_up_node_bitmap = bit_copy(avail_node_bitmap);

	job_list = list_create(_list_delete_job);

	resv_list = list_create(NULL);

	license_init(slurm_conf.licenses);

	select_g_reconfigure();

	if (!params.testcases) {
		Suite *s = suite_create("backfill");
		SRunner *sr = srunner_create(s);
		TCase *tc = tcase_create("backfill");

		tcase_set_timeout(tc, 10);

		tcase_add_test(tc, test_backfill_1);
		tcase_add_test(tc, test_backfill_2);
		tcase_add_test(tc, test_backfill_3);

		tcase_add_test(tc, test_backfill_lic_1);
		tcase_add_test(tc, test_backfill_lic_2);

		suite_add_tcase(s, tc);

		srunner_run_all(sr, CK_ENV);
		number_failed += srunner_ntests_failed(sr);
		srunner_free(sr);
	} else {
		uint32_t now;
		load_test();
		now = time(NULL);
		__attempt_backfill();
		list_for_each(job_list, _print_job, &now);
	}

	_free_options();
#endif
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
