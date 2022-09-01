/*****************************************************************************\
 *  task_plugin.h - Define plugin functions for task pre_launch and post_term.
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SLURMD_TASK_PLUGIN_H_
#define _SLURMD_TASK_PLUGIN_H_

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/cpuset.h>
typedef cpuset_t cpu_set_t;
#endif

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/* The size to represent a cpu_set_t as a hex string (including null) */
#define CPU_SET_HEX_STR_SIZE (1 + (CPU_SETSIZE / 4))

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
extern int task_g_slurmd_batch_request(batch_job_launch_msg_t *req);

/*
 * Slurmd has received a launch request.
 *
 * RET - slurm error code
 */
extern int task_g_slurmd_launch_request(launch_tasks_request_msg_t *req,
					uint32_t node_id, char **err_msg);

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
 * Note that a task launch is about to occur.
 * Run before setting UID to the user.
 *
 * RET - slurm error code
 */
extern int task_g_pre_setuid(stepd_step_rec_t *step);

/*
 * Note in privileged mode that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch_priv(stepd_step_rec_t *step, uint32_t node_tid);

/*
 * Note that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int task_g_pre_launch(stepd_step_rec_t *step);

/*
 * Note that a task has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_term(stepd_step_rec_t *step,
			    stepd_step_task_info_t *task);

/*
 * Note that a step has terminated.
 *
 * RET - slurm error code
 */
extern int task_g_post_step(stepd_step_rec_t *step);

/*
 * Keep track of a pid.
 *
 * RET - slurm error code
 */
extern int task_g_add_pid(pid_t pid);


extern void task_slurm_chkaffinity(cpu_set_t *mask, stepd_step_rec_t *step,
				   int statval, uint32_t taskid);

/*
 * Convert a CPU bitmask to a hex string.
 *
 * IN mask - A CPU bitmask pointer.
 * IN/OUT str - A char pointer used to return a string of size
 *		CPU_SET_HEX_STR_SIZE.
 * RET - Returns a pointer to a string slice in str that starts at the first
 *	 non-zero hex char or last zero hex char if all bits are not set.
 */
extern char *task_cpuset_to_str(const cpu_set_t *mask, char *str);

/*
 * Convert a hex string to a CPU bitmask.
 *
 * IN/OUT mask - An empty CPU bitmask pointer that will be set according to CPUs
 *		 specified by the hex values in str.
 * IN str - A null-terminated hex string that specifies CPUs to set.
 * RET - Returns -1 if str could not be interpreted into valid hex or if str is
 *	 too large, else returns 0 on success.
 */
extern int task_str_to_cpuset(cpu_set_t *mask, const char* str);

#endif /* _SLURMD_TASK_PLUGIN_H_ */
