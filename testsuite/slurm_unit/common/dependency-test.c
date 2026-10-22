/*****************************************************************************\
 *  dependency-test.c - dependency reduction tests
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
\*****************************************************************************/

#include "config.h"

#include <stdlib.h>

#include "src/common/dependency.h"
#include "src/common/job_record.h"
#include "src/common/log.h"
#include "src/common/macros.h"

static const uint16_t dependency_types[] = {
	SLURM_DEPEND_AFTER,
	SLURM_DEPEND_AFTER_ANY,
	SLURM_DEPEND_AFTER_OK,
	SLURM_DEPEND_AFTER_NOT_OK,
};

/* Check after-family rules across lifecycle states and both exceptions. */
static void _test_state_rules(void)
{
	uint32_t target_states[] = {
		JOB_PENDING,
		JOB_RUNNING,
		JOB_COMPLETE,
		JOB_FAILED,
	};
	uint32_t results[][4] = {
		{ DEPEND_NOT_FULFILLED, DEPEND_FULFILLED, DEPEND_FULFILLED,
		  DEPEND_FULFILLED },
		{ DEPEND_NOT_FULFILLED, DEPEND_NOT_FULFILLED, DEPEND_FULFILLED,
		  DEPEND_FULFILLED },
		{ DEPEND_NOT_FULFILLED, DEPEND_NOT_FULFILLED, DEPEND_FULFILLED,
		  DEPEND_FAILED },
		{ DEPEND_NOT_FULFILLED, DEPEND_NOT_FULFILLED, DEPEND_FAILED,
		  DEPEND_FULFILLED },
	};

	for (size_t target = 0; target < ARRAY_SIZE(target_states); target++) {
		for (size_t type = 0; type < ARRAY_SIZE(dependency_types);
		     type++) {
			for (size_t delay = 0; delay < 2; delay++) {
				for (size_t special = 0; special < 2;
				     special++) {
					uint32_t expected =
						results[type][target];
					uint32_t actual = NO_VAL;

					if ((dependency_types[type] ==
					     SLURM_DEPEND_AFTER) &&
					    !delay)
						expected = DEPEND_NOT_FULFILLED;
					if ((dependency_types[type] ==
					     SLURM_DEPEND_AFTER_NOT_OK) &&
					    special)
						expected = DEPEND_FULFILLED;
					actual = depend_test_state(
						dependency_types[type],
						target_states[target] ==
							JOB_COMPLETE,
						target_states[target] >=
							JOB_COMPLETE,
						target_states[target] ==
							JOB_PENDING,
						delay, special);
					if (actual != expected)
						fatal("Incorrect verdict for type %u, state %u, delay %zu, special exit %zu",
						      dependency_types[type],
						      target_states[target],
						      delay, special);
				}
			}
		}
	}
	if (depend_test_state(SLURM_DEPEND_SINGLETON, false, false, false, true,
			      false) != NO_VAL)
		fatal("Controller-only dependency type was handled");
}

/*
 * Check a single edge repeatedly without changing its outcome or references.
 * IN dependency - edge to examine
 * IN reason - expected scheduling reason
 */
static void _check_edge(step_depend_spec_t *dependency,
			enum job_state_reason reason)
{
	step_depend_outcome_t *outcome = dependency->outcome;
	step_depend_outcome_t saved = { 0 };
	list_t *depend_list = list_create(NULL);
	uint16_t depend_type = dependency->depend_type;

	if (outcome)
		saved = *outcome;
	list_append(depend_list, dependency);
	for (size_t i = 0; i < 3; i++) {
		if ((step_depend_reduce_list(depend_list, false) != reason) ||
		    (step_depend_reduce_list(depend_list, true) != reason))
			fatal("Incorrect dependency reason for type %u (state %u)",
			      dependency->depend_type,
			      outcome ? outcome->state : NO_VAL);
	}
	if ((dependency->outcome != outcome) ||
	    (dependency->depend_type != depend_type))
		fatal("Reduction modified dependency edge");
	if (outcome && ((outcome->state != saved.state) ||
			(outcome->reference_count != saved.reference_count) ||
			(outcome->after_edge_count != saved.after_edge_count)))
		fatal("Reduction modified shared outcome");
	list_destroy(depend_list);
}

/*
 * Check AND/OR reduction for a pair of states after releasing target owners.
 * IN first_state - first DEPEND_* state
 * IN second_state - second DEPEND_* state
 * IN and_result - expected AND verdict
 * IN or_result - expected OR verdict
 */
static void _test_reduce(uint32_t first_state, uint32_t second_state,
			 uint32_t and_result, uint32_t or_result)
{
	depend_reduce_t and_reduce = { 0 };
	depend_reduce_t or_reduce = { 0 };
	uint32_t target_states[] = {
		[DEPEND_NOT_FULFILLED] = JOB_PENDING,
		[DEPEND_FULFILLED] = JOB_COMPLETE | JOB_COMPLETING,
		[DEPEND_FAILED] = JOB_FAILED | JOB_COMPLETING,
	};
	step_record_t first_target = {
		.state = target_states[first_state],
	};
	step_record_t second_target = {
		.state = target_states[second_state],
	};
	list_t *depend_list = list_create(step_depend_spec_free);

	enum job_state_reason reasons[] = {
		[DEPEND_NOT_FULFILLED] = WAIT_DEPENDENCY,
		[DEPEND_FULFILLED] = WAIT_NO_REASON,
		[DEPEND_FAILED] = WAIT_DEP_INVALID,
	};

	depend_reduce(first_state, false, &and_reduce);
	depend_reduce(second_state, false, &and_reduce);
	depend_reduce(first_state, true, &or_reduce);
	depend_reduce(second_state, true, &or_reduce);
	if (depend_reduce_result(&and_reduce, false) != and_result)
		fatal("Incorrect AND verdict for states %u and %u",
		      first_state, second_state);
	if (depend_reduce_result(&or_reduce, true) != or_result)
		fatal("Incorrect OR verdict for states %u and %u",
		      first_state, second_state);

	list_append(depend_list,
		    step_depend_spec_create(&first_target,
					    SLURM_DEPEND_AFTER_OK));
	list_append(depend_list,
		    step_depend_spec_create(&second_target,
					    SLURM_DEPEND_AFTER_OK));
	step_depend_outcome_release(&first_target.step_depend_outcome);
	step_depend_outcome_release(&second_target.step_depend_outcome);
	if (step_depend_reduce_list(depend_list, false) != reasons[and_result])
		fatal("Incorrect AND reason for states %u and %u",
		      first_state, second_state);
	if (step_depend_reduce_list(depend_list, true) != reasons[or_result])
		fatal("Incorrect OR reason for states %u and %u",
		      first_state, second_state);
	list_destroy(depend_list);
}

/*
 * Check first-reference initialization for all four dependency types.
 * IN state - target's current step state
 * IN exit_code - target's exit status
 * IN reasons - expected reasons in dependency_types order
 */
static void _test_initial_state(uint32_t state, uint32_t exit_code,
				const enum job_state_reason reasons[4])
{
	step_record_t target = { .state = state, .exit_code = exit_code };

	for (size_t i = 0; i < ARRAY_SIZE(dependency_types); i++) {
		step_depend_spec_t *dependency = NULL;

		dependency =
			step_depend_spec_create(&target, dependency_types[i]);
		_check_edge(dependency, reasons[i]);
		step_depend_spec_free(dependency);
	}
	if ((target.step_depend_outcome->reference_count != 1) ||
	    target.step_depend_outcome->after_edge_count)
		fatal("Initial-state edge references leaked");
	step_depend_outcome_release(&target.step_depend_outcome);
}

/*
 * Check logical-step handoff, owner deletion, and immutable terminal results.
 * IN succeeded - terminal event represents successful execution
 */
static void _test_outcome_lifetime(bool succeeded)
{
	step_record_t target = { .state = JOB_PENDING, .exit_code = NO_VAL };
	step_record_t replacement = {
		.state = JOB_RUNNING,
		.exit_code = NO_VAL,
	};
	step_depend_spec_t *dependencies[4] = { NULL };
	step_depend_outcome_t *outcome = NULL;

	for (size_t i = 0; i < ARRAY_SIZE(dependency_types); i++) {
		dependencies[i] =
			step_depend_spec_create(&target, dependency_types[i]);
		_check_edge(dependencies[i], WAIT_DEPENDENCY);
	}
	outcome = target.step_depend_outcome;
	if ((outcome->reference_count != 5) || (outcome->after_edge_count != 1))
		fatal("Incorrect shared outcome reference counts");

	replacement.step_depend_outcome = target.step_depend_outcome;
	target.step_depend_outcome = NULL;
	if (!step_depend_outcome_update(&replacement, true))
		fatal("Committed start did not request after reconsideration");
	if (step_depend_outcome_update(&replacement, true))
		fatal("Repeated start requested another wake");
	_check_edge(dependencies[0], WAIT_NO_REASON);
	for (size_t i = 1; i < ARRAY_SIZE(dependency_types); i++)
		_check_edge(dependencies[i], WAIT_DEPENDENCY);

	replacement.state =
		succeeded ? (JOB_RUNNING | JOB_COMPLETING) : JOB_CANCELLED;
	replacement.exit_code = succeeded ? 0 : NO_VAL;
	if (!step_depend_outcome_update(&replacement, false))
		fatal("Completion did not request reconsideration");
	replacement.state = succeeded ? JOB_CANCELLED : JOB_COMPLETE;
	replacement.exit_code = succeeded ? NO_VAL : 0;
	if (step_depend_outcome_update(&replacement, false) ||
	    step_depend_outcome_update(&replacement, true))
		fatal("Terminal outcome changed after completion");
	step_depend_outcome_release(&replacement.step_depend_outcome);
	if ((outcome->reference_count != 4) || replacement.step_depend_outcome)
		fatal("Owner release did not leave edge references intact");
	for (size_t i = 0; i < ARRAY_SIZE(dependency_types); i++) {
		const enum job_state_reason terminal_reasons[][4] = {
			{ WAIT_NO_REASON, WAIT_NO_REASON, WAIT_DEP_INVALID,
			  WAIT_NO_REASON },
			{ WAIT_NO_REASON, WAIT_NO_REASON, WAIT_NO_REASON,
			  WAIT_DEP_INVALID },
		};

		_check_edge(dependencies[i], terminal_reasons[succeeded][i]);
		step_depend_spec_free(dependencies[i]);
	}
}

/* Check rejected-edge cleanup and start-interest removal on cancellation. */
static void _test_edge_cleanup(void)
{
	step_record_t target = { .state = JOB_PENDING, .exit_code = NO_VAL };
	list_t *depend_list = list_create(step_depend_spec_free);
	step_depend_spec_t *dependency = NULL;

	list_append(depend_list,
		    step_depend_spec_create(&target, SLURM_DEPEND_AFTER));
	list_append(depend_list,
		    step_depend_spec_create(&target, SLURM_DEPEND_AFTER));
	list_destroy(depend_list);
	if ((target.step_depend_outcome->reference_count != 1) ||
	    target.step_depend_outcome->after_edge_count)
		fatal("Rejected edges retained references or after interest");

	dependency = step_depend_spec_create(&target, SLURM_DEPEND_AFTER_OK);
	target.state = JOB_RUNNING;
	if (step_depend_outcome_update(&target, true))
		fatal("Start woke a terminal-only dependency");
	_check_edge(dependency, WAIT_DEPENDENCY);
	step_depend_spec_free(dependency);
	target.state = JOB_FAILED;
	if (step_depend_outcome_update(&target, false))
		fatal("Completion woke a target with no dependent edges");
	step_depend_outcome_release(&target.step_depend_outcome);
}

/* Check never-launched failure, including after release without a start. */
static void _test_unlaunched_failure(void)
{
	step_record_t target = { .state = JOB_PENDING, .exit_code = NO_VAL };
	step_depend_spec_t *dependencies[4] = { NULL };

	for (size_t i = 0; i < ARRAY_SIZE(dependency_types); i++)
		dependencies[i] =
			step_depend_spec_create(&target, dependency_types[i]);
	target.state = JOB_CANCELLED;
	if (!step_depend_outcome_update(&target, false))
		fatal("Unlaunched failure did not request reconsideration");
	target.state = JOB_COMPLETE;
	target.exit_code = 0;
	if (step_depend_outcome_update(&target, false))
		fatal("Failure outcome revived as success");
	step_depend_outcome_release(&target.step_depend_outcome);
	for (size_t i = 0; i < ARRAY_SIZE(dependency_types); i++) {
		const enum job_state_reason failed_reasons[] = {
			WAIT_NO_REASON,
			WAIT_NO_REASON,
			WAIT_DEP_INVALID,
			WAIT_NO_REASON,
		};

		_check_edge(dependencies[i], failed_reasons[i]);
		step_depend_spec_free(dependencies[i]);
	}
}

/*
 * Check failure marking does not release dependencies before completion.
 * IN state - failure marker applied while the target still runs
 */
static void _test_inflight_failure(uint32_t state)
{
	step_record_t target = { .state = state, .exit_code = NO_VAL };
	step_depend_spec_t *dependency = NULL;

	dependency =
		step_depend_spec_create(&target, SLURM_DEPEND_AFTER_NOT_OK);
	_check_edge(dependency, WAIT_DEPENDENCY);
	if (!step_depend_outcome_update(&target, false))
		fatal("Terminal failure did not request reconsideration");
	_check_edge(dependency, WAIT_NO_REASON);
	step_depend_spec_free(dependency);
	step_depend_outcome_release(&target.step_depend_outcome);
}

int main(void)
{
	depend_reduce_t reduce = { 0 };
	list_t *depend_list = NULL;
	uint32_t states[] = {
		DEPEND_NOT_FULFILLED,
		DEPEND_FULFILLED,
		DEPEND_FAILED,
	};
	uint32_t and_results[][3] = {
		{ DEPEND_NOT_FULFILLED, DEPEND_NOT_FULFILLED, DEPEND_FAILED },
		{ DEPEND_NOT_FULFILLED, DEPEND_FULFILLED, DEPEND_FAILED },
		{ DEPEND_FAILED, DEPEND_FAILED, DEPEND_FAILED },
	};
	uint32_t or_results[][3] = {
		{ DEPEND_NOT_FULFILLED, DEPEND_FULFILLED,
		  DEPEND_NOT_FULFILLED },
		{ DEPEND_FULFILLED, DEPEND_FULFILLED, DEPEND_FULFILLED },
		{ DEPEND_NOT_FULFILLED, DEPEND_FULFILLED, DEPEND_FAILED },
	};

	const enum job_state_reason pending_reasons[] = {
		WAIT_DEPENDENCY,
		WAIT_DEPENDENCY,
		WAIT_DEPENDENCY,
		WAIT_DEPENDENCY,
	};

	const enum job_state_reason running_reasons[] = {
		WAIT_NO_REASON,
		WAIT_DEPENDENCY,
		WAIT_DEPENDENCY,
		WAIT_DEPENDENCY,
	};

	const enum job_state_reason complete_reasons[] = {
		WAIT_NO_REASON,
		WAIT_NO_REASON,
		WAIT_NO_REASON,
		WAIT_DEP_INVALID,
	};

	const enum job_state_reason failed_reasons[] = {
		WAIT_NO_REASON,
		WAIT_NO_REASON,
		WAIT_DEP_INVALID,
		WAIT_NO_REASON,
	};

	_test_state_rules();
	if ((depend_reduce_result(&reduce, false) != DEPEND_FULFILLED) ||
	    (depend_reduce_result(&reduce, true) != DEPEND_FAILED))
		fatal("Incorrect empty reduction verdict");
	if ((step_depend_reduce_list(NULL, false) != WAIT_NO_REASON) ||
	    (step_depend_reduce_list(NULL, true) != WAIT_NO_REASON))
		fatal("NULL step dependency list should be ready");
	depend_list = list_create(NULL);
	if ((step_depend_reduce_list(depend_list, false) != WAIT_NO_REASON) ||
	    (step_depend_reduce_list(depend_list, true) != WAIT_NO_REASON))
		fatal("Empty step dependency list should be ready");
	list_destroy(depend_list);

	for (size_t first = 0; first < ARRAY_SIZE(states); first++) {
		for (size_t second = 0; second < ARRAY_SIZE(states); second++) {
			_test_reduce(states[first], states[second],
				     and_results[first][second],
				     or_results[first][second]);
		}
	}
	for (size_t i = 0; i < 2; i++) {
		step_depend_spec_t *missing = NULL;

		missing = step_depend_spec_create(NULL, dependency_types[i]);
		_check_edge(missing, WAIT_NO_REASON);
		step_depend_spec_free(missing);
	}
	_test_initial_state(JOB_PENDING, NO_VAL, pending_reasons);
	_test_initial_state(JOB_RUNNING, NO_VAL, running_reasons);
	_test_initial_state(JOB_RUNNING | JOB_COMPLETING, 0, complete_reasons);
	_test_initial_state(JOB_COMPLETE | JOB_COMPLETING, 1, failed_reasons);
	_test_initial_state(JOB_CANCELLED | JOB_COMPLETING, NO_VAL,
			    failed_reasons);
	_test_initial_state(JOB_RUNNING | JOB_COMPLETING, NO_VAL,
			    failed_reasons);
	_test_inflight_failure(JOB_TIMEOUT);
	_test_inflight_failure(JOB_NODE_FAIL);
	_test_initial_state(JOB_TIMEOUT, NO_VAL, running_reasons);
	_test_initial_state(JOB_NODE_FAIL, NO_VAL, running_reasons);
	_test_initial_state(JOB_CANCELLED, NO_VAL, running_reasons);
	_test_outcome_lifetime(true);
	_test_outcome_lifetime(false);
	_test_edge_cleanup();
	_test_unlaunched_failure();

	return EXIT_SUCCESS;
}
