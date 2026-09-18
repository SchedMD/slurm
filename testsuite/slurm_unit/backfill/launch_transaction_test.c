#include <stdio.h>
#include <stdlib.h>

#include "src/plugins/sched/backfill/backfill.c"

#define REQUIRE(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

bitstr_t *job_launch_node_bitmap;
bitstr_t *het_job_launch_node_bitmap;
bitstr_t *up_node_bitmap;
time_t last_node_update;

static uint32_t next_job_id = 1000;
static uint32_t preempt_calls;
static time_t test_now = 10000;
static part_record_t test_partition;

time_t __wrap_time(time_t *result)
{
	if (result)
		*result = test_now;
	return test_now;
}

job_record_t *find_job_record(uint32_t job_id)
{
	list_itr_t *iter = list_iterator_create(job_list);
	job_record_t *job_ptr;

	while ((job_ptr = list_next(iter))) {
		if (job_ptr->job_id == job_id) {
			list_iterator_destroy(iter);
			return job_ptr;
		}
	}
	list_iterator_destroy(iter);
	return NULL;
}

job_record_t *__wrap_job_array_split(job_record_t *job_ptr, bool list_add)
{
	job_record_t *pending = xmalloc(sizeof(*pending));

	REQUIRE(list_add);
	REQUIRE(job_ptr->array_recs->task_cnt > 1);
	memcpy(pending, job_ptr, sizeof(*pending));
	pending->details = xmalloc(sizeof(*pending->details));
	memcpy(pending->details, job_ptr->details, sizeof(*pending->details));
	pending->details->preempt_start_time = 0;
	pending->bf_launch_array_slot = false;
	pending->state_desc = xstrdup(job_ptr->state_desc);
	pending->system_comment = xstrdup(job_ptr->system_comment);
	pending->sched_nodes = NULL;
	bit_clear(pending->array_recs->task_id_bitmap, job_ptr->array_task_id);
	pending->array_recs->task_cnt--;
	pending->array_task_id = NO_VAL;
	job_ptr->array_recs = NULL;
	job_ptr->bf_launch_array_slots = 0;
	job_ptr->job_id = next_job_id++;
	list_append(job_list, pending);
	return pending;
}

void __wrap_schedule_job_save(void)
{
}

char *__wrap_bitmap2node_name(bitstr_t *bitmap)
{
	return xstrdup_printf("node-%ld", (long) bit_ffs(bitmap));
}

part_record_t *__wrap_find_part_record(char *name)
{
	return !strcmp(name, test_partition.name) ? &test_partition : NULL;
}

bool __wrap_job_overlap_and_running(bitstr_t *bitmap, list_t *licenses,
				    job_record_t *job_ptr)
{
	return IS_JOB_RUNNING(job_ptr) && job_ptr->node_bitmap &&
		bit_overlap_any(bitmap, job_ptr->node_bitmap);
}

uint16_t __wrap_slurm_job_preempt_mode(job_record_t *job_ptr)
{
	return PREEMPT_MODE_REQUEUE;
}

int __wrap_slurm_job_preempt(job_record_t *victim, job_record_t *owner,
			   uint16_t mode, bool ignore_time)
{
	job_launch_t *launch = _job_launch_find(owner->job_id);
	bitstr_t *available = bit_copy(test_partition.node_bitmap);
	job_record_t competitor = { .job_id = 9999 };

	REQUIRE(launch);
	REQUIRE(mode == PREEMPT_MODE_REQUEUE);
	REQUIRE(ignore_time);
	bf_launch_txn_filter_nodes(&competitor, available);
	REQUIRE(!bit_overlap_any(available, launch->planned_node_bitmap));
	FREE_NULL_BITMAP(available);
	preempt_calls++;
	victim->preempt_time = test_now;
	victim->end_time = test_now + 300;
	victim->bit_flags |= GRACE_PREEMPT;
	return SLURM_SUCCESS;
}

static job_record_t *new_job(uint32_t job_id, uint32_t state)
{
	job_record_t *job_ptr = xmalloc(sizeof(*job_ptr));

	job_ptr->magic = JOB_MAGIC;
	job_ptr->job_id = job_id;
	job_ptr->array_task_id = NO_VAL;
	job_ptr->job_state = state;
	job_ptr->priority = 100;
	job_ptr->part_ptr = &test_partition;
	job_ptr->details = xmalloc(sizeof(*job_ptr->details));
	list_append(job_list, job_ptr);
	return job_ptr;
}

static job_record_t *new_array(uint32_t job_id, uint32_t task_count,
			       uint32_t limit)
{
	job_record_t *job_ptr = new_job(job_id, JOB_PENDING);

	job_ptr->array_job_id = job_id;
	job_ptr->array_recs = xmalloc(sizeof(*job_ptr->array_recs));
	job_ptr->array_recs->task_cnt = task_count;
	job_ptr->array_recs->max_run_tasks = limit;
	job_ptr->array_recs->task_id_bitmap = bit_alloc(task_count);
	bit_set_all(job_ptr->array_recs->task_id_bitmap);
	return job_ptr;
}

static job_launch_t *open_plan(job_record_t *owner, int node_index,
			       uint32_t victim_id)
{
	job_launch_t *launch;
	list_t *victims = list_create(xfree_ptr);
	uint32_t *victim = xmalloc(sizeof(*victim));
	bitstr_t *nodes = bit_alloc(64);

	*victim = victim_id;
	list_append(victims, victim);
	bit_set(nodes, node_index);
	launch = _job_launch_begin(owner, &test_partition, NULL, NULL, false,
				   nodes, victims);
	FREE_NULL_BITMAP(nodes);
	FREE_NULL_LIST(victims);
	return launch;
}

static void test_parallel_array_plans(void)
{
	job_record_t *owner_a = new_array(10, 8, 3);
	job_record_t *owner_b, *master;
	job_record_t *victim_a = new_job(100, JOB_RUNNING);
	job_record_t *victim_b = new_job(101, JOB_RUNNING);
	job_launch_t *plan_a, *plan_b;
	uint32_t calls_before = preempt_calls;
	char *reason = NULL;

	victim_a->node_bitmap = bit_alloc(64);
	bit_set(victim_a->node_bitmap, 0);
	victim_b->node_bitmap = bit_alloc(64);
	bit_set(victim_b->node_bitmap, 1);
	bf_max_job_array_launch = 2;
	plan_a = open_plan(owner_a, 0, victim_a->job_id);
	REQUIRE(plan_a && owner_a->job_id != 10);
	REQUIRE(_job_launch_preempt_planned_jobs(owner_a, plan_a));
	owner_b = find_job_record(10);
	REQUIRE(!owner_b->bf_launch_transaction);
	REQUIRE(!owner_b->bf_launch_array_slot);
	REQUIRE(owner_b->details->preempt_start_time == 0);
	plan_b = open_plan(owner_b, 1, victim_b->job_id);
	REQUIRE(plan_b && owner_b->job_id != owner_a->job_id);
	REQUIRE(_job_launch_preempt_planned_jobs(owner_b, plan_b));
	REQUIRE(victim_a->preempt_time == victim_b->preempt_time);
	REQUIRE(victim_a->end_time == test_now + 300);
	REQUIRE(preempt_calls == calls_before + 2);
	master = find_job_record(10);
	REQUIRE(master->bf_launch_array_slots == 2);
	REQUIRE(!open_plan(master, 2, victim_a->job_id));
	REQUIRE(!strcmp(master->state_desc, "ArrayPreemptionLimit"));
	REQUIRE(master->array_recs->task_cnt == 6);
	REQUIRE(_job_launch_validate(plan_a, &reason));
	REQUIRE(!reason);
	master->array_recs->pend_run_tasks = 1000;
	REQUIRE(_job_runnable_now(owner_a));
	REQUIRE(_job_launch_validate(plan_a, &reason));
	REQUIRE(job_array_start_test(owner_a));
	REQUIRE(owner_a->sched_nodes && owner_b->sched_nodes);
	REQUIRE(strcmp(owner_a->sched_nodes, owner_b->sched_nodes));
	test_now += 299;
	REQUIRE(_job_launch_preempt_planned_jobs(owner_a, plan_a));
	REQUIRE(preempt_calls == calls_before + 2);
	_job_launch_update_status(owner_a, plan_a, test_now, false);
	REQUIRE(strstr(owner_a->system_comment, "00:00:01 remaining of 00:05:00"));
	test_now += 2;
	REQUIRE(_job_launch_preempt_planned_jobs(owner_a, plan_a));
	REQUIRE(preempt_calls == calls_before + 2);
	victim_a->job_state = JOB_COMPLETING;
	REQUIRE(_job_launch_preempt_planned_jobs(owner_a, plan_a));
	victim_a->job_state = JOB_COMPLETE;
	REQUIRE(!_job_launch_preempt_planned_jobs(owner_a, plan_a));
	job_array_start(owner_a);
	owner_a->job_state = JOB_RUNNING;
	_job_launch_clear(plan_a, "job started");
	REQUIRE(master->bf_launch_array_slots == 1);
	REQUIRE(master->array_recs->tot_run_tasks == 1);
	REQUIRE(!owner_a->bf_launch_array_slot);
	REQUIRE(!owner_a->bf_launch_transaction);
	REQUIRE(_job_launch_find(owner_b->job_id) == plan_b);
	owner_b->job_state = JOB_CANCELLED;
	_job_launch_clear_expired();
	REQUIRE(master->bf_launch_array_slots == 0);
	REQUIRE(!_job_launch_find(owner_b->job_id));
	REQUIRE(victim_b->preempt_time != 0);
	job_array_launch_slot_release(owner_b);
	REQUIRE(master->bf_launch_array_slots == 0);
	puts("PASS: parallel plans, grace, cleanup, cap, start, cancellation");
}

static void test_array_budget(void)
{
	job_record_t *master = new_array(20, 1, 2);
	job_record_t *first = new_job(21, JOB_PENDING);
	job_record_t *second = new_job(22, JOB_PENDING);
	job_record_t *outsider = new_job(23, JOB_PENDING);

	first->array_job_id = second->array_job_id = outsider->array_job_id = 20;
	first->array_task_id = 1;
	second->array_task_id = 2;
	outsider->array_task_id = 3;
	REQUIRE(job_array_launch_slot_acquire(first, 20));
	REQUIRE(job_array_launch_slot_acquire(second, 20));
	REQUIRE(job_array_launch_slot_acquire(second, 20));
	REQUIRE(master->bf_launch_array_slots == 2);
	REQUIRE(job_array_start_test(first));
	REQUIRE(!job_array_start_test(outsider));
	REQUIRE(!job_array_launch_slot_acquire(outsider, 20));
	master->array_recs->max_run_tasks = 1;
	REQUIRE(job_array_start_test(first));
	job_array_start(first);
	REQUIRE(master->bf_launch_array_slots == 1);
	REQUIRE(!job_array_start_test(second));
	master->array_recs->tot_run_tasks--;
	REQUIRE(job_array_start_test(second));
	job_array_start(second);
	REQUIRE(master->bf_launch_array_slots == 0);
	master->array_recs->tot_run_tasks--;
	REQUIRE(job_array_start_test(first));
	REQUIRE(!first->bf_launch_array_slot);
	REQUIRE(job_array_launch_slot_acquire(master, 20));
	master->array_recs->pend_run_tasks = 1000;
	REQUIRE(_job_runnable_now(master));
	REQUIRE(!job_array_start_test(outsider));
	job_array_launch_slot_release(master);
	REQUIRE(master->bf_launch_array_slots == 0);
	REQUIRE(!job_array_launch_slot_acquire(master, 0));
	REQUIRE(job_array_start_test(outsider));
	puts("PASS: percent limit, owner exemption, limit reduction, last task, zero cap");
}

static void test_failure_isolation(void)
{
	job_record_t *owner = new_array(30, 5, 5);
	job_record_t *master, *sibling;
	job_launch_t *plan, *sibling_plan;

	bf_max_job_array_launch = 2;
	plan = open_plan(owner, 3, 200);
	REQUIRE(plan);
	master = find_job_record(30);
	_job_launch_enter_cleanup(plan, "test node failure");
	REQUIRE(master->bf_launch_array_slots == 1);
	_job_launch_clear_expired();
	REQUIRE(master->bf_launch_array_slots == 0);
	REQUIRE(owner->bf_launch_retry_after > test_now);
	REQUIRE(!master->bf_launch_retry_after);
	REQUIRE(!master->bf_launch_replan_count);
	sibling = master;
	sibling_plan = open_plan(sibling, 4, 200);
	REQUIRE(sibling_plan);
	REQUIRE(sibling->bf_launch_replan_count == 0);
	master = find_job_record(30);
	REQUIRE(!open_plan(master, 4, 200));
	REQUIRE(master->bf_launch_array_slots == 1);
	test_now = owner->bf_launch_retry_after;
	plan = open_plan(owner, 3, 200);
	REQUIRE(plan);
	_job_launch_enter_cleanup(plan, "second failure");
	_job_launch_clear_expired();
	REQUIRE(owner->bf_launch_replan_blocked);
	REQUIRE(!master->bf_launch_replan_blocked);
	REQUIRE(master->bf_launch_array_slots == 1);
	_job_launch_clear(sibling_plan, "test teardown");
	REQUIRE(master->bf_launch_array_slots == 0);
	puts("PASS: disjoint ownership, cleanup slot, bounded replan isolation, teardown");
}

static void test_non_array_unchanged(void)
{
	job_record_t *ordinary = new_job(40, JOB_PENDING);
	job_record_t *hetjob = new_job(41, JOB_PENDING);
	job_launch_t *plan;

	bf_max_job_array_launch = 0;
	plan = open_plan(ordinary, 5, 200);
	REQUIRE(plan);
	REQUIRE(!ordinary->bf_launch_array_slot);
	_job_launch_clear(plan, "test done");
	hetjob->het_job_id = 41;
	REQUIRE(!open_plan(hetjob, 5, 200));
	REQUIRE(!hetjob->bf_launch_array_slot);
	puts("PASS: ordinary jobs bypass array cap; hetjobs keep their group path");
}

static void test_shared_victim(void)
{
	job_record_t *first = new_array(50, 4, 2);
	job_record_t *second, *master;
	job_record_t *victim = new_job(500, JOB_RUNNING);
	job_launch_t *first_plan, *second_plan;
	uint32_t calls_before = preempt_calls;
	time_t deadline;

	victim->node_bitmap = bit_alloc(64);
	bit_set(victim->node_bitmap, 6);
	bit_set(victim->node_bitmap, 7);
	bf_max_job_array_launch = 2;
	first_plan = open_plan(first, 6, victim->job_id);
	REQUIRE(first_plan);
	REQUIRE(_job_launch_preempt_planned_jobs(first, first_plan));
	deadline = victim->end_time;
	second = find_job_record(50);
	second_plan = open_plan(second, 7, victim->job_id);
	REQUIRE(second_plan);
	test_now += 10;
	REQUIRE(_job_launch_preempt_planned_jobs(second, second_plan));
	REQUIRE(preempt_calls == calls_before + 1);
	REQUIRE(victim->end_time == deadline);
	_job_launch_clear(first_plan, "cancel first owner");
	master = find_job_record(50);
	REQUIRE(master->bf_launch_array_slots == 1);
	REQUIRE(_job_launch_find(second->job_id) == second_plan);
	REQUIRE(_job_launch_preempt_planned_jobs(second, second_plan));
	REQUIRE(preempt_calls == calls_before + 1);
	FREE_NULL_LIST(job_launch_list);
	REQUIRE(master->bf_launch_array_slots == 0);
	REQUIRE(!second->bf_launch_transaction);
	puts("PASS: shared victim signaled once, independent cancellation, shutdown");
}

int main(void)
{
	job_list = list_create(NULL);
	job_launch_list = list_create(_job_launch_del);
	job_launch_node_bitmap = bit_alloc(64);
	up_node_bitmap = bit_alloc(64);
	bit_set_all(up_node_bitmap);
	test_partition.name = "test";
	test_partition.node_bitmap = bit_copy(up_node_bitmap);
	node_record_count = 64;
	node_record_table_ptr = xcalloc(64, sizeof(*node_record_table_ptr));
	for (int node_index = 0; node_index < 64; node_index++) {
		node_record_t *node = xmalloc(sizeof(*node));

		node->index = node_index;
		node->node_state = NODE_STATE_IDLE;
		node->name = xstrdup_printf("node-%d", node_index);
		node_record_table_ptr[node_index] = node;
	}
	test_parallel_array_plans();
	test_array_budget();
	test_failure_isolation();
	test_non_array_unchanged();
	test_shared_victim();
	return 0;
}
