/*****************************************************************************\
 *  step_dependency.c - dependencies between the steps of one job
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

#include "config.h"

#include "src/stepmgr/step_dependency.h"

#include "src/common/dependency.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Bound on the expression bytes accepted, checked before anything is parsed or
 * looked up.  MAX_STEP_DEPEND_CNT edges of a plausible length, rounded up.
 */
#define MAX_STEP_DEPEND_LEN 1024

/* Bound per-expression edge storage and lazy evaluation work. */
#define MAX_STEP_DEPEND_CNT 64

/*
 * True if this step id may take part in a dependency, as either end.  Batch,
 * extern, interactive, external launcher and the pending/wildcard sentinels are
 * all above the ordinary range, and het steps are out of scope for now.
 * IN step_id - the id to test
 * RET true if the id names an ordinary non-het step
 */
extern bool step_depend_ordinary_step_id(slurm_step_id_t *step_id)
{
	if (step_id->step_id > SLURM_MAX_NORMAL_STEP_ID)
		return false;
	if (step_id->step_het_comp != NO_VAL)
		return false;
	return true;
}

/*
 * Parse one "<job_id>.<step_id>" target.
 * IN tok - the text to parse
 * OUT step_id - the parsed identity, other members left as the caller set them
 * RET one char past the target, or NULL for invalid syntax
 */
static char *_parse_target(char *tok, slurm_step_id_t *step_id)
{
	slurm_selected_step_t selected_step = SLURM_SELECTED_STEP_INITIALIZER;
	char *target_end = NULL;
	char *target_string = NULL;

	target_end = tok + strcspn(tok, ":,?");
	target_string = xstrndup(tok, target_end - tok);
	if (unfmt_job_id_string(target_string, &selected_step, 0) ||
	    (strspn(target_string, "0123456789.") != strlen(target_string))) {
		target_end = NULL;
	} else {
		step_id->job_id = selected_step.step_id.job_id;
		step_id->step_id = selected_step.step_id.step_id;
	}
	xfree(target_string);

	return target_end;
}

/*
 * Validate a dependency target and find its current step record.
 * IN job_ptr - the job the dependent step runs in
 * IN dependent - the dependent step's own id
 * IN target - the parsed target id
 * IN depend_type - the dependency type
 * OUT target_step_ptr - the target's step record, NULL if absent
 * RET SLURM_SUCCESS or ESLURM_DEPENDENCY
 */
static int _validate_target(job_record_t *job_ptr, slurm_step_id_t *dependent,
			    slurm_step_id_t *target, uint16_t depend_type,
			    step_record_t **target_step_ptr)
{
	*target_step_ptr = NULL;

	/*
	 * Only steps of this same job.  An array task may name its own
	 * concrete job id; the master array id or another task is a different
	 * job, whose steps this stepmgr does not run.
	 */
	if (target->job_id != job_ptr->job_id) {
		error("%s: step dependency target %ps is not part of %pJ",
		      __func__, target, job_ptr);
		return ESLURM_DEPENDENCY;
	}

	if (!step_depend_ordinary_step_id(target)) {
		error("%s: step dependency target %ps is not an ordinary step",
		      __func__, target);
		return ESLURM_DEPENDENCY;
	}

	if (verify_step_id(target, dependent) &&
	    (dependent->step_id != NO_VAL)) {
		error("%s: %ps cannot depend on itself", __func__, dependent);
		return ESLURM_DEPENDENCY;
	}

	*target_step_ptr = find_step_record(job_ptr, target);
	/* Lookup treats an unspecified het component as a wildcard. */
	if (*target_step_ptr &&
	    !step_depend_ordinary_step_id(&(*target_step_ptr)->step_id)) {
		error("%s: step dependency target %ps is not an ordinary step",
		      __func__, &(*target_step_ptr)->step_id);
		return ESLURM_DEPENDENCY;
	}
	/* Like job dependencies, outcome-based types need a target record. */
	if (!*target_step_ptr && ((depend_type == SLURM_DEPEND_AFTER_OK) ||
				  (depend_type == SLURM_DEPEND_AFTER_NOT_OK))) {
		error("%s: step dependency target %ps is not a known step of %pJ",
		      __func__, target, job_ptr);
		return ESLURM_DEPENDENCY;
	}

	return SLURM_SUCCESS;
}

extern int step_depend_parse(job_record_t *job_ptr, slurm_step_id_t *step_id,
			     char *expression, list_t **depend_list,
			     bool *or_flag)
{
	list_t *new_list = NULL;
	char *tok = expression;
	sep_depend_t sep_type = SEP_DEPEND_ANY;
	int rc = SLURM_SUCCESS;

	xassert(depend_list);
	xassert(or_flag);

	if (!expression || !expression[0]) {
		error("%s: empty step dependency", __func__);
		return ESLURM_DEPENDENCY;
	}
	if (strlen(expression) > MAX_STEP_DEPEND_LEN) {
		error("%s: step dependency longer than %d bytes",
		      __func__, MAX_STEP_DEPEND_LEN);
		return ESLURM_DEPENDENCY;
	}

	new_list = list_create(step_depend_spec_free);

	while (rc == SLURM_SUCCESS) {
		uint16_t depend_type = SLURM_DEPEND_AFTER_ANY;
		sep_depend_t parsed_sep;

		if (!depend_parse_type(&tok, &depend_type) &&
		    ((*tok < '0') || (*tok > '9'))) {
			rc = ESLURM_DEPENDENCY;
			break;
		}

		if ((depend_type != SLURM_DEPEND_AFTER) &&
		    (depend_type != SLURM_DEPEND_AFTER_ANY) &&
		    (depend_type != SLURM_DEPEND_AFTER_OK) &&
		    (depend_type != SLURM_DEPEND_AFTER_NOT_OK)) {
			rc = ESLURM_DEPENDENCY;
			break;
		}

		/* One or more ':'-separated targets of this type. */
		while (rc == SLURM_SUCCESS) {
			step_depend_spec_t *dep_ptr = NULL;
			step_record_t *target_step_ptr = NULL;
			slurm_step_id_t target = {
				.sluid = job_ptr->step_id.sluid,
				.step_het_comp = NO_VAL,
			};

			if (!(tok = _parse_target(tok, &target))) {
				rc = ESLURM_DEPENDENCY;
				break;
			}
			if ((rc = _validate_target(job_ptr, step_id, &target,
						   depend_type,
						   &target_step_ptr)))
				break;
			if (list_count(new_list) >= MAX_STEP_DEPEND_CNT) {
				error("%s: more than %d step dependencies",
				      __func__, MAX_STEP_DEPEND_CNT);
				rc = ESLURM_DEPENDENCY;
				break;
			}

			dep_ptr = step_depend_spec_create(target_step_ptr,
							  depend_type);
			list_append(new_list, dep_ptr);

			if (*tok != ':')
				break;
			tok++; /* skip over ":" */
		}
		if (rc)
			break;

		/*
		 * AND and OR do not mix: the whole expression is one or the
		 * other, as it is for job dependencies, and passing back the
		 * separator seen so far is what enforces that.
		 */
		if (!depend_parse_sep(tok, sep_type, &tok, &parsed_sep, &rc))
			break;
		sep_type = parsed_sep;
	}

	if (!rc && tok[0]) /* trailing junk */
		rc = ESLURM_DEPENDENCY;

	if (rc) {
		FREE_NULL_LIST(new_list);
		error("%s: invalid step dependency \"%s\"",
		      __func__, expression);
		return ESLURM_DEPENDENCY;
	}

	*depend_list = new_list;
	*or_flag = (sep_type == SEP_DEPEND_OR);

	return SLURM_SUCCESS;
}
