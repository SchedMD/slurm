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

#include "src/common/dynamic_plugin_data.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*******************************************\
 * GLOBAL SWITCH STATE MANAGEMENT FUNCTIONS *
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
 * JOB-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCTIONS *
\******************************************************/

extern void switch_g_jobinfo_pack(void *switch_jobinfo, buf_t *buffer,
				  uint16_t protocol_version);

extern int switch_g_jobinfo_unpack(void **switch_jobinfo, buf_t *buffer,
				   uint16_t protocol_version);

/* Free switch_jobinfo struct when switch_g_job_complete can't be used */
extern void switch_g_jobinfo_free(job_record_t *job_ptr);

/******************************************************\
 * STEP-SPECIFIC SWITCH CREDENTIAL MANAGEMENT FUNCTIONS *
\******************************************************/

/*
 * create a step's switch credential
 * OUT stepinfo - storage for a step switch credential
 * IN  jobinfo - build stepinfo from this switch jobinfo
 * IN  step_ptr    - step_record_t for this step
 * NOTE: step_ptr will be NULL for "srun --no-allocate" calls
 * NOTE: storage must be freed using switch_g_stepinfo_free
 */
extern int switch_g_stepinfo_build(dynamic_plugin_data_t **stepinfo,
				   void *switch_jobinfo,
				   step_record_t *step_ptr);

/*
 * duplicate a step's switch credential
 * IN  source  - storage for a switch job credential
 * OUT dest    - pointer to NULL at beginning, will point to storage for
 *               duplicated switch job credential
 * NOTE: storage must be freed using switch_g_stepinfo_free
 */
extern void switch_g_stepinfo_duplicate(dynamic_plugin_data_t *source,
					dynamic_plugin_data_t **dest);

/*
 * free storage previously allocated for a switch step credential
 * IN jobinfo  - the switch job credential to be freed
 */
extern void switch_g_stepinfo_free(dynamic_plugin_data_t *jobinfo);

/*
 * IN jobinfo  - the switch job credential to be saved
 * OUT buffer  - buffer with switch credential appended
 * IN protocol_version - version of Slurm we are talking to.
 */
extern void switch_g_stepinfo_pack(dynamic_plugin_data_t *jobinfo,
				   buf_t *buffer, uint16_t protocol_version);

/*
 * OUT jobinfo - the switch job credential read
 * IN  buffer  - buffer with switch credential read from current pointer loc
 * IN  protocol_version - version of Slurm we are talking to.
 * RET         - slurm error code
 * NOTE: returned value must be freed using switch_g_stepinfo_free
 *	 Actual stepinfo will only be unpacked in the stepd as this is the only
 *	 location that requires it.
 */
extern int switch_g_stepinfo_unpack(dynamic_plugin_data_t **jobinfo,
				    buf_t *buffer, uint16_t protocol_version);

/*
 * Return true if switch plugin should be setup on special steps
 * (batch/interactive/extern steps), otherwise return false.
 */
extern bool switch_g_setup_special_steps(void);

/*
 * Note that the job step associated with the specified nodelist
 * has completed execution.
 */
extern int switch_g_job_step_complete(dynamic_plugin_data_t *jobinfo,
	char *nodelist);

typedef enum {
	SWITCH_START_KEPT = 0, /* job record took ownership of jobinfo */
	SWITCH_START_DONE, /* report handled or dropped, plugin frees jobinfo */
	SWITCH_START_UNDO, /* no job waits for this setup any more, plugin
			    * tears it down and frees jobinfo */
} switch_start_ret_t;

/*
 * Report the result of an asynchronous switch_g_job_start(). Call only after
 * it has returned, from a thread holding no slurmctld locks:
 * switch_g_job_start() runs with the job write lock held and this takes it.
 *
 * IN sluid - job being started (its record may be gone)
 * IN rc - SLURM_SUCCESS, or an error to have the job requeued or cancelled
 * IN jobinfo - switch state to store on the job record, or NULL
 * IN teardown - with an error rc, true if the setup may have left state that
 *		 switch_g_job_complete() must tear down
 *
 * RET - who frees jobinfo, and whether the plugin must tear the setup down
 */
typedef switch_start_ret_t (*switch_start_cb_t)(sluid_t sluid, int rc,
						void *jobinfo, bool teardown);

/*
 * Runs before the job prolog.
 *
 * Runs again for a job whose setup was interrupted by a slurmctld restart,
 * possibly while the old slurmctld's call for the same SLUID is still running
 * (e.g. after a backup slurmctld takes over). It must be safe to repeat; the
 * old call's report is dropped.
 *
 * IN job_ptr - Job to start
 * IN test_only - Test if job can run right now
 * IN start - callback for an asynchronous start, may be NULL if test_only
 * OUT async - set true if the plugin will invoke start exactly once from
 *	       another thread; the job stays JOB_CONFIGURING until then. May be
 *	       NULL if test_only.
 *
 * RET - SLURM_SUCCESS or SLURM_ERROR and job_ptr->state_reason set if test_only
 *	 is true and job cannot run right now but may be able to later.
 */
extern int switch_g_job_start(job_record_t *job_ptr, bool test_only,
			      switch_start_cb_t start, bool *async);

/*
 * Report the result of an asynchronous switch_g_job_complete(). Call only
 * after it has returned, from a thread holding no slurmctld locks.
 *
 * IN sluid - job being completed (its record may be gone)
 * IN rc - SLURM_SUCCESS, EAGAIN to retry if nodes are held, or an error to
 *	   have the job's nodes drained
 */
typedef void (*switch_complete_cb_t)(sluid_t sluid, int rc);

typedef enum {
	SWITCH_COMPLETE_MODE_SYNC = 0, /* done on return */
	SWITCH_COMPLETE_MODE_ASYNC, /* reports via callback, nodes not held */
	SWITCH_COMPLETE_MODE_ASYNC_BLOCK_NODES, /* as ASYNC, nodes held */
} switch_complete_mode_t;

/*
 * End of job - tear down whatever switch_g_job_start() set up, including a
 * failed or abandoned setup that may have left state behind. Called during
 * node deallocation, once an outstanding setup reports or times out, and again
 * at startup for a teardown that was holding nodes when slurmctld stopped, so
 * it must be safe to repeat.
 *
 * switch_jobinfo belongs to the job record. A plugin that frees it here must
 * set it to NULL.
 *
 * EAGAIN, returned here or through the callback with
 * SWITCH_COMPLETE_MODE_ASYNC_BLOCK_NODES, keeps the nodes held and has the
 * call repeated up to SlurmctldParameters=switch_retries times before the
 * allocation is drained. A plugin returning EAGAIN must have no callback
 * outstanding and must tolerate a repeat after a partial attempt.
 *
 * IN complete - callback for the asynchronous modes, invoked exactly once
 * OUT mode - how the plugin tears down, defaults to SWITCH_COMPLETE_MODE_SYNC
 *
 * RET - SLURM_SUCCESS, EAGAIN to be called again, or an error to have the
 *       allocation drained
 */
extern int switch_g_job_complete(job_record_t *job_ptr,
				 switch_complete_cb_t complete,
				 switch_complete_mode_t *mode);

/********************************************************************\
 * JOB LAUNCH AND MANAGEMENT FUNCTIONS RELATED TO SWITCH CREDENTIAL *
\********************************************************************/

/*
 * Prepare node for job.
 *
 * pre is run as root in the first slurmstepd process, the so called job
 * manager. This function can be used to perform any initialization
 * that needs to be performed in the same process as switch_g_job_postfini()
 *
 */
extern int switch_g_job_preinit(stepd_step_rec_t *step);

/*
 * initialize switch_g on node for job. This function is run from the
 * slurmstepd process.
 *
 */
extern int switch_g_job_init(stepd_step_rec_t *step);

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

extern uint32_t switch_g_job_channel(job_record_t *job_ptr, char *node_name);

extern int switch_g_fs_init(stepd_step_rec_t *step);

extern void switch_g_stepmgr_fini(uint32_t job_id);

/*
 * Initialize slurmstepd stepmgr
 * Execute when the slurmstepd learns it is the stepmgr
 */
extern void switch_g_stepmgr_init(void);

#endif
