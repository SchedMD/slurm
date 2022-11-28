/*****************************************************************************\
 *  dist_tasks.h - Assign task count for each resource.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
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

#ifndef _CONS_TRES_DIST_TASKS_H
#define _CONS_TRES_DIST_TASKS_H

#include "select_cons_tres.h"

/*
 * dist_tasks_compute_c_b - compute the number of tasks on each
 * of the node for the cyclic and block distribution. We need to do
 * this in the case of consumable resources so that we have an exact
 * count for the needed hardware resources which will be used later to
 * update the different used resources per node structures.
 *
 * The most common case is when we have more resources than needed. In
 * that case we just "take" what we need and "release" the remaining
 * resources for other jobs. In the case where we oversubscribe the
 * processing units (PUs) we keep the initial set of resources.
 *
 * IN/OUT job_ptr - pointer to job being scheduled. The per-node
 *                  job_res->cpus array is recomputed here.
 * IN gres_task_limit - array of task limits based upon job's GRES specification
 *			offset based upon bits set in
 *			job_ptr->job_resrcs->node_bitmap
 * IN gres_min_cpus - array of minimum required CPUs based upon job's GRES
 * 		      specification, offset based upon bits set in
 * 		      job_ptr->job_resrcs->node_bitmap
 */
extern int dist_tasks_compute_c_b(job_record_t *job_ptr,
				  uint32_t *gres_task_limit,
				  uint32_t *gres_min_cpus);

#endif /* !_CONS_TRES_DIST_TASKS_H */
