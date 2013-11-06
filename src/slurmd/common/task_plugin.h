/*****************************************************************************\
 *  task_plugin.h - Define plugin functions for task pre_launch and post_term.
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _SLURMD_TASK_PLUGIN_H_
#define _SLURMD_TASK_PLUGIN_H_

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Initialize the task plugin.
 *
 * RET - slurm error code
 */
extern int slurmd_task_init( void );

/*
 * Terminate the task plugin, free memory.
 *
 * RET - slurm error code
 */
extern int slurmd_task_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Slurmd has received a batch job launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_batch_request(uint32_t job_id,
				       batch_job_launch_msg_t *req);

/*
 * Slurmd has received a launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_launch_request(uint32_t job_id,
				 launch_tasks_request_msg_t *req,
				 uint32_t node_id );

/*
 * Slurmd is reserving resources for the task.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_reserve_resources(uint32_t job_id,
				    launch_tasks_request_msg_t *req,
				    uint32_t node_id );

/*
 * Slurmd is suspending a job.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_suspend_job(uint32_t job_id);

/*
 * Slurmd is resuming a previously suspended job.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_resume_job(uint32_t job_id);

/*
 * Slurmd is releasing resources for the task.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_release_resources(uint32_t job_id);

/*
 * Note that a task launch is about to occur.
 * Run before setting UID to the user.
 *
 * RET - slurm error code
 */
extern int task_g_pre_setuid(stepd_step_rec_t *job);

/*
 * Note in privileged mode that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch_priv(stepd_step_rec_t *job);

/*
 * Note that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch(stepd_step_rec_t *job);

/*
 * Note that a task has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_term(stepd_step_rec_t *job,
			    stepd_step_task_info_t *task);

/*
 * Note that a step has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_step(stepd_step_rec_t *job);

#endif /* _SLURMD_TASK_PLUGIN_H_ */
