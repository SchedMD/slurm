/*****************************************************************************\
 *  dist_tasks.h - Assign task count for each resource.
 *****************************************************************************
 *  Copyright (C) 2018-2019 SchedMD LLC
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

/*
 * To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many cores are needed on each node.
 *
 * This routine is a slightly modified "version" of the routine
 * _task_layout_block in src/common/dist_tasks.c. We do not need to
 * assign tasks to job->hostid[] and job->tids[][] at this point so
 * the core allocation is the same for cyclic and block.
 *
 * For the consumable resources support we need to determine what
 * "node/Core/thread"-tuplets will be allocated for a given job.
 * In the past we assumed that we only allocated one task per PU
 * (processing unit, the lowest allocatable logical processor,
 * core or thread depending upon configuration) and didn't allow
 * the use of overcommit. We have changed this philosophy and are now
 * allowing people to overcommit their resources and expect the system
 * administrator to enable the task/affinity plug-in which will then
 * bind all of a job's tasks to its allocated resources thereby
 * avoiding interference between co-allocated running jobs.
 *
 * In the consumable resources environment we need to determine the
 * layout schema within slurmctld.
 *
 * We have a core_bitmap of all available cores. All we're doing here
 * is removing cores that are not needed based on the task count, and
 * the choice of cores to remove is based on the distribution:
 * - "cyclic" removes cores "evenly", starting from the last socket,
 * - "block" removes cores from the "last" socket(s)
 * - "plane" removes cores "in chunks"
 *
 * IN job_ptr - job to be allocated resources
 * IN cr_type - allocation type (sockets, cores, etc.)
 * IN preempt_mode - true if testing with simulated preempted jobs
 * IN core_array - system-wide bitmap of cores originally available to
 *		the job, only used to identify specialized cores
 * IN gres_task_limit - array of task limits based upon job GRES specification,
 *		offset based upon bits set in job_ptr->job_resrcs->node_bitmap
 * IN gres_min_cpus - array of minimum required CPUs based upon job's GRES
 * 		      specification, offset based upon bits set in
 * 		      job_ptr->job_resrcs->node_bitmap
 */
extern int dist_tasks(job_record_t *job_ptr, const uint16_t cr_type,
		      bool preempt_mode, bitstr_t **core_array,
		      uint32_t *gres_task_limit,
		      uint32_t *gres_min_cpus);

/* Return true if more tasks can be allocated for this job on this node */
extern bool dist_tasks_tres_tasks_avail(uint32_t *gres_task_limit,
					job_resources_t *job_res,
					uint32_t node_offset);

/* Add CPUs back to job_ptr->job_res->cpus to satisfy gres_min_cpus */
extern void dist_tasks_gres_min_cpus(job_record_t *job_ptr,
				     uint16_t *avail_cpus,
				     uint32_t *gres_min_cpus);

#endif /* !_CONS_TRES_DIST_TASKS_H */
