/*****************************************************************************\
 *  slurm_mpi.h - Generic MPI selector for Slurm
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

#ifndef _SLURM_MPI_H
#define _SLURM_MPI_H

#include <stdbool.h>

#include "slurm/slurm.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

enum mpi_plugin_type {
	MPI_PLUGIN_NONE = 101,
	MPI_PLUGIN_PMI2 = 102,
	MPI_PLUGIN_CRAY_SHASTA = 103,
	MPI_PLUGIN_PMIX2 = 104,
	MPI_PLUGIN_PMIX3 = 105,
	MPI_PLUGIN_PMIX4 = 106,
	MPI_PLUGIN_PMIX5 = 107,
};

typedef struct slurm_mpi_context *slurm_mpi_context_t;
typedef void mpi_plugin_client_state_t;

typedef struct {
	uint32_t het_job_id; /* Hetjob leader id or NO_VAL */
	uint32_t het_job_task_offset; /* Hetjob task offset or NO_VAL */
	slurm_step_id_t step_id; /* Current step id (or NO_VAL) */
	slurm_step_layout_t *step_layout;
} mpi_step_info_t;

typedef struct {
	slurm_addr_t *client;
	uint32_t gtaskid;/* global task rank within the job step */
	int ltaskid; /* task rank within the local node */
	uint32_t ltasks; /* number of tasks on *this* (local) node */
	uint32_t nnodes; /* number of nodes in current job step */
	uint32_t nodeid; /* relative position of this node in job */
	uint32_t ntasks; /* total number of tasks in current job */
	slurm_addr_t *self;
	slurm_step_id_t step_id; /* Current step id (or NO_VAL) */
} mpi_task_info_t;

/**********************************************************************
 * Hooks called by the slurmd and/or slurmstepd.
 **********************************************************************/

/*
 * This function will remove SLURM_MPI_TYPE from the environment variable
 * array "*env", if it exists and its value is "none".
 */
extern int mpi_process_env(char ***env);

extern int mpi_g_slurmstepd_prefork(const stepd_step_rec_t *step, char ***env);

/*
 * Load the plugin (if not already loaded) and call the plugin
 * mpi_p_slurmstepd_task() function.
 *
 * This function is called from within each process that will exec() a
 * task.  The process will be running as the user of the job step at that
 * point.
 *
 * If the plugin wants to set environment variables for the task,
 * it will add the necessary variables the env array pointed
 * to be "env".  If "*env" is NULL, a new array will be allocated
 * automatically.
 *
 * The returned "*env" array may be manipulated (and freed) by using
 * the src/common/env.c:env_array_* functions.
 */
extern int mpi_g_slurmstepd_task(const mpi_task_info_t *mpi_task,
				 char ***env);

/**********************************************************************
 * Hooks called by client applications.
 * For instance: srun, slaunch, slurm_step_launch().
 **********************************************************************/

/*
 * Just load the requested plugin.  No explicit calls into the plugin
 * once loaded (just the implicit call to the plugin's init() function).
 *
 * If "*mpi_type" is NULL, the system-default mpi plugin
 * is initialized.
 */
extern int mpi_g_client_init(char **mpi_type);

/*
 * Call the plugin mpi_p_client_prelaunch() function.
 *
 * If the plugin requires that environment variables be set in the
 * environment of every task, it will add the necessary variables
 * the env array pointed to be "env".  If "*env" is NULL, a new
 * array will be allocated automatically.
 *
 * The returned "*env" array may be manipulated (and freed) by using
 * the src/common/env.c:env_array_* functions.
 *
 * Returns NULL on error.  On success returns an opaque pointer
 * to MPI state for this job step.  Free the state by calling
 * mpi_g_client_fini().
 */
extern mpi_plugin_client_state_t *mpi_g_client_prelaunch(
	const mpi_step_info_t *mpi_step, char ***env);

/* Call the plugin mpi_p_client_fini() function. */
extern int mpi_g_client_fini(mpi_plugin_client_state_t *state);

/* Initialize all available plugins, read and set their config from mpi.conf. */
extern int mpi_g_daemon_init(void);

/* Fini and init in sequence */
extern int mpi_g_daemon_reconfig(void);

/* Deliver a printable list to the client with config from all loaded plugins */
extern List mpi_g_conf_get_printable(void);

/* Functions to send config from Slurmd to Slurstepd, peer to peer */
extern int mpi_conf_send_stepd(int fd, uint32_t plugin_id);
extern int mpi_conf_recv_stepd(int fd);

/* given a mpi_type return the plugin_id see mpi_plugin_type above */
extern int mpi_id_from_plugin_type(char *mpi_type);

/* Tear down things in the MPI plugin */
extern int mpi_fini(void);

#endif /* !_SLURM_MPI_H */
