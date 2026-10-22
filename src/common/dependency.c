/*****************************************************************************\
 *  dependency.c - dependency decisions shared by jobs and steps
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

#include "src/common/dependency.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Parse a dependency type followed by a colon.
 * IN/OUT tok - clause text, advanced past the type and colon on success
 * OUT depend_type - parsed SLURM_DEPEND_* value, unchanged on failure
 * RET true if a dependency type was found
 */
extern bool depend_parse_type(char **tok, uint16_t *depend_type)
{
	char *separator = NULL;

	if (!(separator = xstrchr(*tok, ':')))
		return false;

	if (!xstrncasecmp(*tok, "afternotok:", 11))
		*depend_type = SLURM_DEPEND_AFTER_NOT_OK;
	else if (!xstrncasecmp(*tok, "aftercorr:", 10))
		*depend_type = SLURM_DEPEND_AFTER_CORRESPOND;
	else if (!xstrncasecmp(*tok, "afterany:", 9))
		*depend_type = SLURM_DEPEND_AFTER_ANY;
	else if (!xstrncasecmp(*tok, "afterok:", 8))
		*depend_type = SLURM_DEPEND_AFTER_OK;
	else if (!xstrncasecmp(*tok, "afterburstbuffer:", 11))
		*depend_type = SLURM_DEPEND_BURST_BUFFER;
	else if (!xstrncasecmp(*tok, "after:", 6))
		*depend_type = SLURM_DEPEND_AFTER;
	else if (!xstrncasecmp(*tok, "expand:", 7))
		*depend_type = SLURM_DEPEND_EXPAND;
	else
		return false;

	*tok = separator + 1;
	return true;
}

extern bool depend_parse_sep(char *tok, sep_depend_t expected_sep,
			     char **sep_ptr, sep_depend_t *sep_type, int *rc)
{
	sep_depend_t parsed_sep = SEP_DEPEND_ANY;
	*sep_ptr = tok;
	if (tok) {
		if (tok[0] == ',') {
			parsed_sep = SEP_DEPEND_AND;
		} else if (tok[0] == '?') {
			parsed_sep = SEP_DEPEND_OR;
		} else {
			return false;
		}
	}

	if ((expected_sep != SEP_DEPEND_ANY) && (expected_sep != parsed_sep)) {
		*rc = ESLURM_DEPENDENCY;
		return false;
	}
	*sep_ptr = tok + 1;
	*sep_type = parsed_sep;
	return true;
}

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
				  bool after_delay_elapsed, bool special_exit)
{
	switch (depend_type) {
	case SLURM_DEPEND_AFTER:
		if (is_pending || !after_delay_elapsed)
			return DEPEND_NOT_FULFILLED;
		return DEPEND_FULFILLED;
	case SLURM_DEPEND_AFTER_ANY:
		return is_completed ? DEPEND_FULFILLED : DEPEND_NOT_FULFILLED;
	case SLURM_DEPEND_AFTER_OK:
		if (!is_completed)
			return DEPEND_NOT_FULFILLED;
		return is_complete ? DEPEND_FULFILLED : DEPEND_FAILED;
	case SLURM_DEPEND_AFTER_NOT_OK:
		if (special_exit)
			return DEPEND_FULFILLED;
		if (!is_completed)
			return DEPEND_NOT_FULFILLED;
		return is_complete ? DEPEND_FAILED : DEPEND_FULFILLED;
	default:
		return NO_VAL;
	}
}

/*
 * Normalize a terminal result for dependency success/failure decisions.
 * IN step_ptr - ending step
 * RET JOB_COMPLETE for success, JOB_FAILED otherwise
 */
static uint32_t _step_depend_terminal_state(step_record_t *step_ptr)
{
	if ((step_terminal_state(step_ptr) == JOB_COMPLETE) &&
	    !step_ptr->exit_code)
		return JOB_COMPLETE;
	return JOB_FAILED;
}

extern step_depend_spec_t *step_depend_spec_create(step_record_t *target_step,
						   uint16_t depend_type)
{
	step_depend_spec_t *dependency = NULL;
	step_depend_outcome_t *outcome = NULL;

	xassert((depend_type == SLURM_DEPEND_AFTER) ||
		(depend_type == SLURM_DEPEND_AFTER_ANY) ||
		(depend_type == SLURM_DEPEND_AFTER_OK) ||
		(depend_type == SLURM_DEPEND_AFTER_NOT_OK));

	dependency = xmalloc(sizeof(*dependency));
	dependency->depend_type = depend_type;
	if (!target_step) {
		xassert((depend_type == SLURM_DEPEND_AFTER) ||
			(depend_type == SLURM_DEPEND_AFTER_ANY));
		return dependency;
	}

	if (!target_step->step_depend_outcome) {
		outcome = xmalloc(sizeof(*outcome));
		outcome->reference_count = 1;
		/* Failure marking does not mean tasks have finished. */
		if (IS_STEP_COMPLETING(target_step))
			outcome->state =
				_step_depend_terminal_state(target_step);
		else if (IS_STEP_PENDING(target_step))
			outcome->state = JOB_PENDING;
		else
			outcome->state = JOB_RUNNING;
		target_step->step_depend_outcome = outcome;
	}

	outcome = target_step->step_depend_outcome;
	outcome->reference_count++;
	if (depend_type == SLURM_DEPEND_AFTER)
		outcome->after_edge_count++;
	dependency->outcome = outcome;
	return dependency;
}

extern void step_depend_outcome_release(step_depend_outcome_t **outcome)
{
	if (!*outcome)
		return;

	xassert((*outcome)->reference_count);
	if (!--(*outcome)->reference_count)
		xfree(*outcome);
	*outcome = NULL;
}

extern void step_depend_spec_free(void *x)
{
	step_depend_spec_t *dependency = x;

	if (!dependency)
		return;
	if (dependency->outcome &&
	    (dependency->depend_type == SLURM_DEPEND_AFTER)) {
		xassert(dependency->outcome->after_edge_count);
		dependency->outcome->after_edge_count--;
	}
	step_depend_outcome_release(&dependency->outcome);
	xfree(dependency);
}

extern bool step_depend_outcome_update(step_record_t *step_ptr, bool started)
{
	step_depend_outcome_t *outcome = step_ptr->step_depend_outcome;
	uint32_t state = JOB_PENDING;

	if (!outcome || (outcome->state == JOB_COMPLETE) ||
	    (outcome->state == JOB_FAILED))
		return false;

	state = started ? JOB_RUNNING : _step_depend_terminal_state(step_ptr);
	if (state == outcome->state)
		return false;

	outcome->state = state;
	return started ? (outcome->after_edge_count > 0) :
			 (outcome->reference_count > 1);
}
