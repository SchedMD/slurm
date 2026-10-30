/*****************************************************************************\
 *  step_dependency.h - dependencies between the steps of one job
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

#ifndef _STEP_DEPENDENCY_H
#define _STEP_DEPENDENCY_H

#include "src/common/job_record.h"

/*
 * step_depend_ordinary_step_id - true if this step id may take part in a
 *	dependency, as either end.  Batch, extern, interactive, external
 *	launcher and the pending/wildcard sentinels are all above the ordinary
 *	range, and het steps are out of scope for now.
 * IN step_id - the id to test
 * RET true if the id names an ordinary non-het step
 */
extern bool step_depend_ordinary_step_id(slurm_step_id_t *step_id);

/*
 * step_depend_parse - parse a --step-dependency expression into edges.
 *
 *	The grammar mirrors the job dependency one, restricted to the step
 *	types, and AND/OR is global across the whole expression as it is for
 *	jobs: "afterok:1.0:1.1?afterany:1.2" means any one of the three edges
 *	releases the step, not (.0 and .1) or .2.
 *
 *		expression := clause ("," clause)* | clause ("?" clause)*
 *		clause     := [type ":"] target (":" target)*
 *		type       := "after"|"afterany"|"afterok"|"afternotok"
 *		target     := job_id "." step_id
 *
 *	An omitted type defaults to afterany for that clause.
 *	Every target must name an ordinary step of this same job, whether or
 *	not an earlier OR edge is satisfied.  As for job dependencies, a
 *	missing target satisfies after and afterany, but is an error for
 *	afterok and afternotok because its outcome is unavailable.
 *
 * IN job_ptr - the job the dependent step runs in
 * IN step_id - the dependent step's own id, to reject self-dependencies
 * IN expression - the submitted --step-dependency string
 * OUT depend_list - the parsed edges, caller must free. Untouched on error
 * OUT or_flag - true if the edges are ORed, false if ANDed
 * RET SLURM_SUCCESS, ESLURM_DEPENDENCY, or ESLURM_STEP_DEP_FAILED if ANDed
 *     edges need the same target to both succeed and fail
 */
extern int step_depend_parse(job_record_t *job_ptr, slurm_step_id_t *step_id,
			     char *expression, list_t **depend_list,
			     bool *or_flag);

#endif
