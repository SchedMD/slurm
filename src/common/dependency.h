/*****************************************************************************\
 *  dependency.h - dependency decisions shared by jobs and steps
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

#ifndef _DEPENDENCY_H
#define _DEPENDENCY_H

#include "src/common/job_record.h"

/* Which separator joins the clauses of a dependency expression */
typedef enum {
	SEP_DEPEND_OR,
	SEP_DEPEND_AND,
	SEP_DEPEND_ANY,
} sep_depend_t;

/* Accumulates what a set of dependency edge states means, see depend_reduce */
typedef struct {
	bool and_failed;
	bool has_unfulfilled;
	bool or_satisfied;
} depend_reduce_t;

/*
 * Parse a dependency type followed by a colon.
 * IN/OUT tok - clause text, advanced past the type and colon on success
 * OUT depend_type - parsed SLURM_DEPEND_* value, unchanged on failure
 * RET true if a dependency type was found
 */
extern bool depend_parse_type(char **tok, uint16_t *depend_type);

/*
 * depend_parse_sep - parse the separator between two dependency clauses.
 *	All dependencies in one expression are ORed or ANDed; they cannot be
 *	mixed, which is enforced by passing back the separator seen so far as
 *	expected_sep.
 * IN tok - string to parse
 * IN expected_sep - which separator(s) may appear, SEP_DEPEND_ANY for the first
 * OUT sep_ptr - one char past the separator if there was one, else tok
 * OUT sep_type - the separator that was parsed
 * OUT rc - set to ESLURM_DEPENDENCY if the wrong separator appeared
 * RET true when a separator was successfully parsed
 */
extern bool depend_parse_sep(char *tok, sep_depend_t expected_sep,
			     char **sep_ptr, sep_depend_t *sep_type, int *rc);

/*
 * Test the state decision shared by job and step dependencies.
 * IN depend_type - SLURM_DEPEND_* dependency type
 * IN is_complete - true if the target succeeded
 * IN is_completed - true if the target finished
 * IN is_pending - true if the target has not started or finished
 * IN after_delay_elapsed - true if no after delay remains or it is bypassed
 * IN special_exit - true if the target has special-exit status
 * RET DEPEND_* state, or NO_VAL for a type handled by the controller
 */
extern uint32_t depend_test_state(uint16_t depend_type, bool is_complete,
				  bool is_completed, bool is_pending,
				  bool after_delay_elapsed, bool special_exit);

/*
 * depend_reduce - fold one edge's state into a running verdict.
 *	All dependencies are ORed or ANDed, never a mix.
 *	OR'd: one fulfilled edge passes; one+ unfulfilled still depends; all
 *	failed can never be satisfied.
 *	AND'd: one failed can never be satisfied; one+ unfulfilled still
 *	depends; all fulfilled passes.
 * IN depend_state - the DEPEND_* state of one edge
 * IN or_flag - true if the edges are ORed rather than ANDed
 * IN/OUT reduce - the verdict being accumulated
 */
extern void depend_reduce(uint32_t depend_state, bool or_flag,
			  depend_reduce_t *reduce);

/*
 * Get the scheduling verdict from accumulated dependency states.
 * IN reduce - accumulated edge states
 * IN or_flag - true if the edges are ORed rather than ANDed
 * RET DEPEND_FULFILLED, DEPEND_NOT_FULFILLED, or DEPEND_FAILED; an empty
 *     reduction is fulfilled for AND and failed for OR
 */
extern uint32_t depend_reduce_result(const depend_reduce_t *reduce,
				     bool or_flag);

/* Shared by a logical step's record and dependency edges under step lock. */
struct step_depend_outcome {
	uint32_t after_edge_count; /* retained after edges, excluding owner */
	uint32_t reference_count; /* record owner plus dependency edges */
	uint32_t state; /* JOB_PENDING, RUNNING, COMPLETE, or FAILED */
};

/* A NULL outcome permanently fulfills a missing after/afterany target. */
typedef struct {
	uint16_t depend_type; /* SLURM_DEPEND_* type */
	step_depend_outcome_t *outcome;
} step_depend_spec_t;

/*
 * step_depend_reduce_list - read shared outcomes to reduce dependency edges
 *	to a scheduling verdict.  AND is ready when every edge is fulfilled and
 *	impossible once any edge has failed; OR is ready once any edge is
 *	fulfilled and impossible only when all of them have failed.
 * IN depend_list - list of step_depend_spec_t, may be NULL
 * IN or_flag - whether the edges are ORed
 * RET WAIT_NO_REASON if ready, WAIT_DEPENDENCY if waiting, or WAIT_DEP_INVALID
 *     if the dependencies cannot be satisfied
 */
extern enum job_state_reason step_depend_reduce_list(list_t *depend_list,
						     bool or_flag);

/*
 * Create an edge and acquire its target outcome under the step lock.
 * IN/OUT target_step - live target, NULL for missing after/afterany targets
 * IN depend_type - supported SLURM_DEPEND_* type
 * RET owned dependency edge
 */
extern step_depend_spec_t *step_depend_spec_create(step_record_t *target_step,
						   uint16_t depend_type);

/*
 * Free an edge and release its outcome reference under the step lock.
 * IN x - dependency edge, or NULL
 */
extern void step_depend_spec_free(void *x);

/*
 * Release an outcome reference and clear its slot under the step lock.
 * IN/OUT outcome - reference to release, cleared on return
 */
extern void step_depend_outcome_release(step_depend_outcome_t **outcome);

/*
 * Publish one committed lifecycle event under the step lock without waking.
 * IN/OUT step_ptr - logical step owning the outcome to update
 * IN started - true for allocation commit, false for terminal outcome
 * RET true if pending steps should reconsider their dependencies
 */
extern bool step_depend_outcome_update(step_record_t *step_ptr, bool started);

#endif
