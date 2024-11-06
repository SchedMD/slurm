/*****************************************************************************\
 *  switch.h - Generic switch (switch_g) info for slurm
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

#ifndef _INTERFACES_SWITCH_H
#define _INTERFACES_SWITCH_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*******************************************\
 * GLOBAL SWITCH STATE MANAGEMENT FUNCIONS *
\*******************************************/

/* initialize the switch plugin */
extern int switch_g_init(bool only_default);

/* terminate the switch plugin and free all memory */
extern int switch_g_fini(void);

/* save any global switch state to a file within the specified directory
 * the actual file name used in plugin specific
 * RET         - slurm error code
 */
extern int switch_g_save(void);

/* restore any global switch state from a file within the specified directory
 * the actual file name used in plugin specific
 * IN recover  - "true" to restore switch state, "false" to start with
 *               a clean slate.
 * RET         - slurm error code
 */
extern int switch_g_restore(bool recover);

/******************************************************\
 * JOB-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

extern void switch_g_pack_jobinfo(void *switch_jobinfo, buf_t *buffer,
				  uint16_t protocol_version);

extern int switch_g_unpack_jobinfo(void **switch_jobinfo, buf_t *buffer,
				   uint16_t protocol_version);

/******************************************************\
 * STEP-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

/*
 * create a step's switch credential
 * OUT jobinfo  - storage for a switch job credential
 * IN  step_layout - the layout of the step with at least the nodes,
 *                   tasks_per_node and tids set
 * IN  step_ptr    - step_record_t for this step
 * NOTE: step_ptr will be NULL for "srun --no-allocate" calls
 * NOTE: storage must be freed using switch_g_free_stepinfo
 */
extern int switch_g_build_stepinfo(dynamic_plugin_data_t **jobinfo,
				   slurm_step_layout_t *step_layout,
				   step_record_t *step_ptr);

/*
 * duplicate a step's switch credential
 * IN  source  - storage for a switch job credential
 * OUT dest    - pointer to NULL at beginning, will point to storage for
 *               duplicated switch job credential
 * NOTE: storage must be freed using switch_g_free_stepinfo
 */
extern void switch_g_duplicate_stepinfo(dynamic_plugin_data_t *source,
					dynamic_plugin_data_t **dest);

/*
 * free storage previously allocated for a switch step credential
 * IN jobinfo  - the switch job credential to be freed
 */
extern void switch_g_free_stepinfo(dynamic_plugin_data_t *jobinfo);

/*
 * IN jobinfo  - the switch job credential to be saved
 * OUT buffer  - buffer with switch credential appended
 * IN protocol_version - version of Slurm we are talking to.
 */
extern void switch_g_pack_stepinfo(dynamic_plugin_data_t *jobinfo,
				   buf_t *buffer, uint16_t protocol_version);

/*
 * OUT jobinfo - the switch job credential read
 * IN  buffer  - buffer with switch credential read from current pointer loc
 * IN  protocol_version - version of Slurm we are talking to.
 * RET         - slurm error code
 * NOTE: returned value must be freed using switch_g_free_stepinfo
 *	 Actual stepinfo will only be unpacked in the stepd as this is the only
 *	 location that requires it.
 */
extern int switch_g_unpack_stepinfo(dynamic_plugin_data_t **jobinfo,
				    buf_t *buffer, uint16_t protocol_version);

/*
 * Note that the job step associated with the specified nodelist
 * has completed execution.
 */
extern int switch_g_job_step_complete(dynamic_plugin_data_t *jobinfo,
	char *nodelist);

/*
 * Runs before the job prolog.
 */
extern void switch_g_job_start(job_record_t *job_ptr);

/*
 * End of job - free any slurmctld job-specific switch data
 */
extern void switch_g_job_complete(job_record_t *job_ptr);

/********************************************************************\
 * JOB LAUNCH AND MANAGEMENT FUNCTIONS RELATED TO SWITCH CREDENTIAL *
\********************************************************************/

/*
 * Prepare node for job.
 *
 * pre is run as root in the first slurmstepd process, the so called job
 * manager. This function can be used to perform any initialization
 * that needs to be performed in the same process as switch_g_job_fini()
 *
 */
extern int switch_g_job_preinit(stepd_step_rec_t *step);

/*
 * initialize switch_g on node for job. This function is run from the
 * slurmstepd process (some switch_g implementations may require
 * switch_g init functions to be executed from a separate process
 * than the process executing switch_g_job_fini() [e.g. QsNet])
 *
 */
extern int switch_g_job_init(stepd_step_rec_t *step);

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
 * This function is run from the initial slurmstepd process (same process
 * as switch_g_job_preinit()), and is run as root. Any cleanup routines
 * that need to be run with root privileges should be run from this
 * function.
 */
extern int switch_g_job_postfini(stepd_step_rec_t *step);

/*
 * attach process to switch_g_job
 * (Called from within the process, so it is appropriate to set
 * switch_g specific environment variables here)
 */
extern int switch_g_job_attach(dynamic_plugin_data_t *jobinfo, char ***env,
			       uint32_t nodeid, uint32_t procid,
			       uint32_t nnodes, uint32_t nprocs, uint32_t rank);

extern int switch_g_fs_init(stepd_step_rec_t *step);

extern void switch_g_extern_stepinfo(void **stepinfo, job_record_t *job_ptr);

extern void switch_g_extern_step_fini(uint32_t job_id);

#endif
