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
