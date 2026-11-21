/*****************************************************************************\
 *  preempt_qos-test.c - Test QOS alternatives in preemption queue ordering.
 *****************************************************************************
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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

#include "config.h"

/* Exercise the scheduler comparator without loading a controller plugin. */
#define preempt_g_job_preempt_check preempt_p_job_preempt_check
#include "src/slurmctld/job_scheduler.c"
#undef preempt_g_job_preempt_check

#include "src/plugins/preempt/qos/preempt_qos.c"

#undef NDEBUG
#include <assert.h>

extern bool slurm_preemption_enabled(void)
{
	return true;
}

static void _test_queue_alternatives(void)
{
	slurmdb_qos_rec_t low = { .id = 1 };
	slurmdb_qos_rec_t high = { .id = 2 };
	uint32_t priorities[] = { 400, 300 };
	priority_mult_t multi = { .priority_array = priorities };
	job_record_t mixed = {
		.qos_ptr = &low,
		.priority = 10,
		.prio_mult = &multi,
	};
	job_record_t single = { .qos_ptr = &high, .priority = 100 };
	job_queue_rec_t preferred = {
		.job_ptr = &mixed,
		.qos_ptr = &high,
		.priority = 400,
	};
	job_queue_rec_t fallback = {
		.job_ptr = &mixed,
		.qos_ptr = &low,
		.priority = 300,
	};
	job_queue_rec_t competitor = {
		.job_ptr = &single,
		.qos_ptr = &high,
		.priority = 100,
	};
	job_queue_rec_t *a = &preferred, *b = &competitor;

	high.preempt_bitstr = bit_alloc(3);
	bit_set(high.preempt_bitstr, low.id);
	slurm_conf.preempt_mode = PREEMPT_MODE_REQUEUE;

	/* The preferred entry must not inherit its job's fallback QOS. */
	assert(!preempt_p_job_preempt_check(&competitor, &preferred));
	assert(!preempt_p_job_preempt_check(&preferred, &competitor));
	assert(sort_job_queue2(&a, &b) < 0);
	assert(sort_job_queue2(&b, &a) > 0);
	assert(mixed.qos_ptr == &low && mixed.priority == 10);
	for (int selected = 0; selected < 2; selected++) {
		list_t *queue = list_create(NULL);

		mixed.qos_ptr = selected ? &high : &low;
		list_append(queue, &fallback);
		list_append(queue, &competitor);
		list_append(queue, &preferred);
		sort_job_queue(queue);
		assert(list_pop(queue) == &preferred);
		assert(list_pop(queue) == &competitor);
		assert(list_pop(queue) == &fallback);
		assert(mixed.qos_ptr == (selected ? &high : &low));
		assert(mixed.priority == 10);
		list_destroy(queue);
	}

	/* Entries for the same job still have distinct preemption relations. */
	assert(preempt_p_job_preempt_check(&preferred, &fallback));
	assert(!preempt_p_job_preempt_check(&fallback, &preferred));
	mixed.qos_ptr = &high;
	assert(preempt_p_job_preempt_check(&competitor, &fallback));
	assert(!preempt_p_job_preempt_check(&fallback, &competitor));

	preferred.qos_ptr = NULL;
	assert(!preempt_p_job_preempt_check(&competitor, &preferred));
	assert(!preempt_p_job_preempt_check(&preferred, &competitor));
	preferred.qos_ptr = &high;
	low.preempt_bitstr = bit_alloc(3);
	assert(!preempt_p_job_preempt_check(&fallback, &preferred));

	/* PRIORITY and WITHIN must use entry priorities, not stale job values. */
	for (int global = 0; global < 2; global++) {
		for (int within = 0; within < 2; within++) {
			uint16_t mode = within ? PREEMPT_MODE_WITHIN :
						 PREEMPT_MODE_PRIORITY;

			slurm_conf.preempt_mode = global ? mode : 0;
			high.preempt_mode = global ? 0 : mode;
			competitor.qos_ptr = within ? &high : &low;
			mixed.priority = 1;
			single.priority = 1000;
			preferred.priority = 200;
			competitor.priority = 100;
			assert(preempt_p_job_preempt_check(&preferred,
							   &competitor));
			preferred.priority = 100;
			assert(!preempt_p_job_preempt_check(&preferred,
							    &competitor));
			preferred.priority = 50;
			mixed.priority = 1000;
			single.priority = 1;
			assert(!preempt_p_job_preempt_check(&preferred,
							    &competitor));
		}
	}

	/* Running-job preemption continues to use the selected job QOS. */
	slurm_conf.preempt_mode = PREEMPT_MODE_REQUEUE;
	high.preempt_mode = 0;
	mixed.qos_ptr = &low;
	assert(preempt_p_preemptable(&mixed, &single));
	assert(!preempt_p_preemptable(&single, &mixed));
	mixed.qos_ptr = &high;
	assert(!preempt_p_preemptable(&mixed, &single));
	high.preempt_mode = PREEMPT_MODE_WITHIN;
	mixed.priority = 100;
	single.priority = 200;
	assert(preempt_p_preemptable(&mixed, &single));
	assert(!preempt_p_preemptable(&single, &mixed));
	mixed.qos_ptr = NULL;
	assert(!preempt_p_preemptable(&mixed, &single));
	assert(!preempt_p_preemptable(&single, &mixed));

	bit_free(low.preempt_bitstr);
	bit_free(high.preempt_bitstr);
}

int main(void)
{
	_test_queue_alternatives();
	return 0;
}
