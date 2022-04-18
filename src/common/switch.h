/*****************************************************************************\
 *  src/common/switch.h - Generic switch (switch_g) info for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _SWITCH_H
#define _SWITCH_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/* opaque data structures - no peeking! */
#ifndef __switch_jobinfo_t_defined
#  define __switch_jobinfo_t_defined
   typedef struct switch_jobinfo   switch_jobinfo_t;
#endif
typedef struct slurm_switch_context slurm_switch_context_t;

/*******************************************\
 * GLOBAL SWITCH STATE MANAGEMENT FUNCIONS *
\*******************************************/

/* initialize the switch plugin */
extern int  switch_init(bool only_default);

extern int switch_g_reconfig(void);

/* terminate the switch plugin and free all memory */
extern int switch_fini (void);

/* save any global switch state to a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory into which switch state is saved
 * RET         - slurm error code
 */
extern int  switch_g_save   (char *dir_name);

/* restore any global switch state from a file within the specified directory
 * the actual file name used in plugin specific
 * IN dir_name - directory from hich switch state is restored
 * IN recover  - "true" to restore switch state, "false" to start with
 *               a clean slate.
 * RET         - slurm error code
 */
extern int  switch_g_restore(char *dir_name, bool recover);

/* clear all current switch window allocation information
 * RET         - slurm error code
 */
extern int  switch_g_clear(void);

/******************************************************\
 * JOB-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

/* allocate storage for a switch job credential
 * OUT jobinfo - storage for a switch job credential
 * IN job_id   - job id of the job this is for.
 * IN step_id  - step id of the job this is for.
 * RET         - slurm error code
 * NOTE: storage must be freed using g_switch_g_free_jobinfo
 */
extern int  switch_g_alloc_jobinfo (dynamic_plugin_data_t **jobinfo,
				    uint32_t job_id, uint32_t step_id);

/* fill a job's switch credential
 * OUT jobinfo  - storage for a switch job credential
 * IN  step_layout - the layout of the step with at least the nodes,
 *                   tasks_per_node and tids set
 * IN  step_ptr    - step_record_t for this step
 * NOTE: step_ptr will be NULL for "srun --no-allocate" calls
 * NOTE: storage must be freed using g_switch_g_free_jobinfo
 */
extern int switch_g_build_jobinfo(dynamic_plugin_data_t *jobinfo,
				  slurm_step_layout_t *step_layout,
				  step_record_t *step_ptr);

/* duplicate a job's switch credential
 * IN  source  - storage for a switch job credential
 * OUT dest    - pointer to NULL at beginning, will point to storage for
 *               duplicated switch job credential
 * NOTE: storage must be freed using g_switch_g_free_jobinfo
 */
extern int  switch_g_duplicate_jobinfo(dynamic_plugin_data_t *source,
				       dynamic_plugin_data_t **dest);

/* free storage previously allocated for a switch job credential
 * IN jobinfo  - the switch job credential to be freed
 */
extern void switch_g_free_jobinfo  (dynamic_plugin_data_t *jobinfo);

/* pack a switch job credential into a buffer in machine independent form
 * IN jobinfo  - the switch job credential to be saved
 * OUT buffer  - buffer with switch credential appended
 * IN protocol_version - version of Slurm we are talking to.
 * RET         - slurm error code
 */
extern int switch_g_pack_jobinfo(dynamic_plugin_data_t *jobinfo, buf_t *buffer,
				 uint16_t protocol_version);

/* unpack a switch job credential from a buffer
 * OUT jobinfo - the switch job credential read
 * IN  buffer  - buffer with switch credential read from current pointer loc
 * IN  protocol_version - version of Slurm we are talking to.
 * RET         - slurm error code
 * NOTE: returned value must be freed using g_switch_g_free_jobinfo
 */
extern int switch_g_unpack_jobinfo(dynamic_plugin_data_t **jobinfo,
				   buf_t *buffer,
				   uint16_t protocol_version);

/* get some field from a switch job credential
 * IN jobinfo - the switch job credential
 * IN data_type - the type of data to get from the credential
 * OUT data - the desired data from the credential
 * RET         - slurm error code
 */
extern int  switch_g_get_jobinfo(dynamic_plugin_data_t *jobinfo,
	int data_type, void *data);

/*
 * Note that the job step associated with the specified nodelist
 * has completed execution.
 */
extern int switch_g_job_step_complete(dynamic_plugin_data_t *jobinfo,
	char *nodelist);

/*
 * End of job - free any slurmctld job-specific switch data
 */
extern void switch_g_job_complete(uint32_t job_id);

/*
 * Restore the switch allocation information "jobinfo" for an already
 * allocated job step, most likely to restore the switch information
 * after a call to switch_g_clear().
 */
extern int switch_g_job_step_allocated(dynamic_plugin_data_t *jobinfo,
	char *nodelist);

/********************************************************************\
 * JOB LAUNCH AND MANAGEMENT FUNCTIONS RELATED TO SWITCH CREDENTIAL *
\********************************************************************/

/*
 * Notes on job related switch_g functions:
 *
 * switch_g functions are run within slurmd in the following way:
 * (Diagram courtesy of Jim Garlick [see qsw.c] )
 *
 *  Process 1 (root)        Process 2 (root, user)  |  Process 3 (user task)
 *                                                  |
 *  switch_g_job_preinit                            |
 *  fork ------------------ switch_g_job_init       |
 *  waitpid                 setuid, chdir, etc.     |
 *                          fork N procs -----------+--- switch_g_job_attach
 *                          wait all                |    exec mpi process
 *                          switch_g_job_fini*      |
 *  switch_g_job_postfini                           |
 *                                                  |
 *
 * [ *Note: switch_g_job_fini() is run as the uid of the job owner, not root ]
 */

/*
 * Prepare node for job.
 *
 * pre is run as root in the first slurmd process, the so called job
 * manager. This function can be used to perform any initialization
 * that needs to be performed in the same process as switch_g_job_fini()
 *
 */
extern int switch_g_job_preinit(stepd_step_rec_t *job);

/*
 * initialize switch_g on node for job. This function is run from the
 * slurmstepd process (some switch_g implementations may require
 * switch_g init functions to be executed from a separate process
 * than the process executing switch_g_job_fini() [e.g. QsNet])
 *
 */
extern int switch_g_job_init(stepd_step_rec_t *job);

/*
 * Determine if a job can be suspended
 *
 * IN jobinfo - switch information for a job step
 * RET SLURM_SUCCESS or error code
 */
extern int switch_g_job_suspend_test(dynamic_plugin_data_t *jobinfo);

/*
 * Build data structure containing information needed to suspend or resume
 * a job
 *
 * IN jobinfo - switch information for a job step
 * RET data to be sent with job suspend/resume RPC
 */
extern void switch_g_job_suspend_info_get(dynamic_plugin_data_t *jobinfo,
					  void **suspend_info);
/*
 * Pack data structure containing information needed to suspend or resume
 * a job
 *
 * IN suspend_info - data to be sent with job suspend/resume RPC
 * IN/OUT buffer to hold the data
 * IN protocol_version - version of Slurm we are talking to.
 */
extern void switch_g_job_suspend_info_pack(void *suspend_info, buf_t *buffer,
					   uint16_t protocol_version);
/*
 * Unpack data structure containing information needed to suspend or resume
 * a job
 *
 * IN suspend_info - data to be sent with job suspend/resume RPC
 * IN/OUT buffer that holds the data
 * IN protocol_version - version of Slurm we are talking to.
 * RET SLURM_SUCCESS or error code
 */
extern int switch_g_job_suspend_info_unpack(void **suspend_info, buf_t *buffer,
					    uint16_t protocol_version);
/*
 * Free data structure containing information needed to suspend or resume
 * a job
 *
 * IN suspend_info - data sent with job suspend/resume RPC
 */
extern void switch_g_job_suspend_info_free(void *suspend_info);

/*
 * Suspend a job's use of switch resources. This may reset MPI timeout values
 * and/or release switch resources. See also switch_g_job_resume().
 *
 * IN max_wait - maximum number of seconds to wait for operation to complete
 * RET SLURM_SUCCESS or error code
 */
extern int switch_g_job_suspend(void *suspend_info, int max_wait);

/*
 * Resume a job's use of switch resources. See also switch_g_job_suspend().
 *
 * IN max_wait - maximum number of seconds to wait for operation to complete
 * RET SLURM_SUCCESS or error code
 */
extern int switch_g_job_resume(void *suspend_info, int max_wait);

/*
 * This function is run from the same process as switch_g_job_init()
 * after all job tasks have exited. It is *not* run as root, because
 * the process in question has already setuid to the job owner.
 *
 */
extern int switch_g_job_fini(dynamic_plugin_data_t *jobinfo);

/*
 * Finalize switch_g on node.
 *
 * This function is run from the initial slurmd process (same process
 * as switch_g_job_preinit()), and is run as root. Any cleanup routines
 * that need to be run with root privileges should be run from this
 * function.
 */
extern int switch_g_job_postfini(stepd_step_rec_t *job);

/*
 * attach process to switch_g_job
 * (Called from within the process, so it is appropriate to set
 * switch_g specific environment variables here)
 */
extern int switch_g_job_attach(dynamic_plugin_data_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t rank);

/********************************************************************\
 * JOB STEP {PRE,POST}-SUSPEND and {PRE-POST}-RESUME FUNCTIONS      *
\********************************************************************/

/*
 * Do job-step-related pre-suspend actions
 */
extern int switch_g_job_step_pre_suspend(stepd_step_rec_t *job);

/*
 * Do job-step-related post-suspend actions
 */
extern int switch_g_job_step_post_suspend(stepd_step_rec_t *job);

/*
 * Do job-step-related pre-resume actions
 */
extern int switch_g_job_step_pre_resume(stepd_step_rec_t *job);

/*
 * Do job-step-related post-resume actions
 */
extern int switch_g_job_step_post_resume(stepd_step_rec_t *job);

#endif /* _SWITCH_H */
