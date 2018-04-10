/*****************************************************************************\
 *  src/common/slurm_mpi.h - Generic mpi selector for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
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

#ifndef _SRUN_MPI_H
#define _SRUN_MPI_H

#include <stdbool.h>

#include "slurm/slurm.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_mpi_context *slurm_mpi_context_t;
typedef void mpi_plugin_client_state_t;

typedef struct {
	uint32_t jobid;
	uint32_t pack_jobid;	/* pack job leader or NO_VAL */
	uint32_t stepid;
	slurm_step_layout_t *step_layout;
} mpi_plugin_client_info_t;

typedef struct {
	uint32_t jobid;  /* Current Slurm job id                      */
	uint32_t stepid; /* Current step id (or NO_VAL)               */
	uint32_t nnodes; /* number of nodes in current job step       */
	uint32_t nodeid; /* relative position of this node in job     */
	uint32_t ntasks; /* total number of tasks in current job      */
	uint32_t ltasks; /* number of tasks on *this* (local) node    */

	uint32_t gtaskid;/* global task rank within the job step      */
	int      ltaskid;/* task rank within the local node           */

	slurm_addr_t *self;
	slurm_addr_t *client;
} mpi_plugin_task_info_t;

/**********************************************************************
 * Hooks called by the slurmd and/or slurmstepd.
 **********************************************************************/

/*
 * Just load the requested plugin.  No explicit calls into the plugin
 * once loaded (just the implicit call to the plugin's init() function).
 *
 * The MPI module type is passed through an environment variable
 * SLURM_MPI_TYPE from the client.  There is no more official protocol.
 * This function will remove SLURM_MPI_TYPE from the environment variable
 * array "env", if it exists.
 */
int mpi_hook_slurmstepd_init (char ***env);

/*
 * Load the plugin (if not already loaded) and call the plugin
 * p_mpi_hook_slurmstepd_task() function.
 *
 * This function is called from within each process that will exec() a
 * task.  The process will be running as the user of the job step at that
 * point.
 *
 * If the plugin wants to set environment variables for the task,
 * it will add the necessary variables the env array pointed
 * to be "env".  If "env" is NULL, a new array will be allocated
 * automaticallly.
 *
 * The returned "env" array may be manipulated (and freed) by using
 * the src/common/env.c:env_array_* functions.
 */
int mpi_hook_slurmstepd_task (const mpi_plugin_task_info_t *job, char ***env);


int mpi_hook_slurmstepd_prefork (const stepd_step_rec_t *job, char ***env);

/**********************************************************************
 * Hooks called by client applications.
 * For instance: srun, slaunch, slurm_step_launch().
 **********************************************************************/

/*
 * Just load the requested plugin.  No explicit calls into the plugin
 * once loaded (just the implicit call to the plugin's init() function).
 *
 * If "mpi_type" is NULL, the system-default mpi plugin
 * is initialized.
 */
int mpi_hook_client_init (char *mpi_type);

/*
 * Call the plugin p_mpi_hook_client_prelaunch() function.
 *
 * If the plugin requires that environment variables be set in the
 * environment of every task, it will add the necessary variables
 * the env array pointed to be "env".  If "env" is NULL, a new
 * array will be allocated automaticallly.
 *
 * The returned "env" array may be manipulated (and freed) by using
 * the src/common/env.c:env_array_* functions.
 *
 * Returns NULL on error.  On success returns an opaque pointer
 * to MPI state for this job step.  Free the state by calling
 * mpi_hook_client_fini().
 */
mpi_plugin_client_state_t *
mpi_hook_client_prelaunch(const mpi_plugin_client_info_t *job, char ***env);

/* Call the plugin p_mpi_hook_client_fini() function. */
int mpi_hook_client_fini (mpi_plugin_client_state_t *state);

/**********************************************************************
 * FIXME - Nobody calls the following function.  Perhaps someone should.
 **********************************************************************/
int mpi_fini (void);

#endif /* !_SRUN_MPI_H */
