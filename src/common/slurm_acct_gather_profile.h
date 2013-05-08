/*****************************************************************************\
 *  slurm_acct_gather_profile.h - implementation-independent job profile
 *  accounting plugin definitions
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifndef __SLURM_ACCT_GATHER_PROFILE_H__
#define __SLURM_ACCT_GATHER_PROFILE_H__

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_acct_gather.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Load the plugin
 */
extern int acct_gather_profile_init(void);

/*
 * Unload the plugin
 */
extern int acct_gather_profile_fini(void);
/*
 * Define plugin local conf for acct_gather.conf
 *
 * Parameters
 * 	full_options -- pointer that will receive list of plugin local
 *			definitions
 *	full_options_cnt -- count of plugin local definitions
 */
extern void acct_gather_profile_g_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt);
/*
 * set plugin local conf from acct_gather.conf into its structure
 *
 * Parameters
 * 	tbl - hash table of acct_gather.conf key-values.
 */
extern void acct_gather_profile_g_conf_set(s_p_hashtbl_t *tbl);

/*
 * get acct_gather.conf parameters
 *
 * returns - pointer to static slurm_acct_gather_conf_t
 */
extern void* acct_gather_profile_g_conf_get(void);

/*
 * Called from slurmctld, when it starts.
 * Provide an opportunity to make necessary directories and other global
 * initialization.
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_controller_start();

/*
 * Called once per step on each node from slurmstepd, before launching tasks.
 * Provides an opportunity to create files and other node-step level
 * initialization.
 *
 * Parameters
 *	job -- structure defining a slurm job
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_node_step_start(slurmd_job_t* job);

/*
 * Called once per step on each node from slurmstepd, after all tasks end.
 * Provides an opportunity to close files, etc.
 *
 * Parameters
 *	job -- structure defining a slurm job
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_node_step_end(slurmd_job_t* job);

/*
 * Called once per task from slurmstepd, BEFORE node step start is called.
 * Provides an opportunity to gather beginning values from node counters
 * (bytes_read ...)
 * At this point in the life cycle, the value of the --profile option isn't
 * known and and files are not open so calls to the 'add_*_data'
 * functions cannot be made.
 *
 * Parameters
 *	job -- structure defining a slurm job
 *	taskid -- slurm taskid
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_task_start(slurmd_job_t* job,
		uint32_t taskid);

/*
 * Called once per task from slurmstepd.
 * Provides an opportunity to put final data for a task.
 *
 * Parameters
 *	job     -- structure defining a slurm job
 *	taskpid -- linux process id of task
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_task_end(slurmd_job_t* job, pid_t taskpid);

/*
 * Called from the job_acct_gather poll_data routine.
 * Provides an opportunity to put data from the job step info structure.
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_job_sample();

/*
 * Put data at the Node Totals level. Typically called when the step ends.
 *
 * Parameters
 *	job   -- structure defining a slurm job
 *	group -- identifies the data stream (source of data).
 *	type  -- identifies the type of data.
 *	data  -- data structure to be put to the file.
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_add_node_data(slurmd_job_t* job, char* group,
		char* type, void* data);

/*
 * Put data at the Node Samples level. Typically called from something called
 * at either job_acct_gather interval or acct_gather_energy interval.
 * All samples in the same group will eventually be consolidated in one
 * dataset
 *
 * Parameters
 *	group -- identifies the data stream (source of data).
 *	type  -- identifies the type of data.
 *	data  -- data structure to be put to the file.
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_add_sample_data(char* group, char* type,
		void* data);

/*
 * Put data at the Task Totals level. Typically called at task end.
 *
 * Parameters
 *	job    -- structure defining a slurm job
 *	taskid -- slurm taskid
 *	group  -- identifies the data stream (source of data).
 *	type   -- identifies the type of data.
 *	data   -- data structure to be put to the file.
 *
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 */
extern int acct_gather_profile_g_add_task_data(slurmd_job_t* job,
		uint32_t taskid, char* group, char* type, void* data);

/*
 * get the slurm taskid from a pid.
 *
 * Parameters
 * 	pid    - a linux process id
 * 	gtid   - (out) pointer to variable to receive slurm taskid
 *
 * Returns
 *      corresponding slurm taskid (or -1)
 * Returns -- SLURM_SUCCESS or SLURM_ERROR
 *
 */
extern int get_taskid_from_pid(pid_t pid, uint32_t *gtid);

#endif /*__SLURM_ACCT_GATHER_PROFILE_H__*/
