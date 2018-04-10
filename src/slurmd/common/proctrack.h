/*****************************************************************************\
 *  proctrack.h - Define process tracking plugin functions.
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
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

#ifndef __PROC_TRACK_H__
#define __PROC_TRACK_H__

#include <stdbool.h>

#include "slurm/slurm.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Initialize the process tracking plugin.
 *
 * Returns a Slurm errno.
 */
extern int slurm_proctrack_init(void);

/*
 * Terminate the process tracking plugin, free memory.
 *
 * Returns a Slurm errno.
 */
extern int slurm_proctrack_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Create a container
 * job IN - stepd_step_rec_t structure
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in proctrack_g_add()
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_create(stepd_step_rec_t *job);

/*
 * Add a process to the specified container
 * job IN - stepd_step_rec_t structure
 * pid IN      - process ID to be added to the container
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in proctrack_g_create()
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_add(stepd_step_rec_t *job, pid_t pid);

/*
 * Signal all processes within a container
 * cont_id IN - container ID as returned by proctrack_g_create()
 * signal IN  - signal to send, if zero then perform error checking
 *              but do not send signal
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_signal(uint64_t cont_id, int signal);


/*
 * Destroy or otherwise invalidate a container,
 *	any processes within the container are not affected
 * cont_id IN - container ID as returned by proctrack_g_create()
 *
 * Returns a Slurm errno.
 */
extern int proctrack_g_destroy(uint64_t cont_id);

/*
 * Get container ID for given process ID
 *
 * Returns zero if no container found for the given pid.
 */
extern uint64_t proctrack_g_find(pid_t pid);

/*
 * Return "true" if the container "cont_id" contains the process with
 * ID "pid".
 */
extern bool proctrack_g_has_pid(uint64_t cont_id, pid_t pid);

/*
 * Wait for all processes within a container to exit.
 *
 * When proctrack_g_wait returns SLURM_SUCCESS, the container is considered
 * destroyed.  There is no need to call proctrack_g_destroy after
 * a successful call to proctrack_g_wait, and in fact it will trigger
 * undefined behavior.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR.
 */
extern int proctrack_g_wait(uint64_t cont_id);

/*
 * Get all process IDs within a container.
 *
 * IN cont_id - Container ID.
 * OUT pids - a pointer to an xmalloc'ed array of process ids, of
 *	length "npids".  If not NULL, caller must free array with xfree().
 * OUT npids - number of process IDs in the returned "pids" array.
 *
 * Return SLURM_SUCCESS if container exists (npids may be zero, and
 *   pids NULL), return SLURM_ERROR if container does not exist, or
 *   plugin does not implement the call.
 */
extern int proctrack_g_get_pids(uint64_t cont_id, pid_t **pids, int *npids);

/* Collect accounting information for all processes within a container */

#endif /*__PROC_TRACK_H__*/
