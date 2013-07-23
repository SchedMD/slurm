/*****************************************************************************\
 *  job_container_plugin.h - job container plugin stub.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette
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

#ifndef _JOB_CONTAINER_PLUGIN_H_
#define _JOB_CONTAINER_PLUGIN_H_

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Initialize the job container plugin.
 *
 * RET - slurm error code
 */
extern int job_container_init(void);

/*
 * Terminate the job container plugin, free memory.
 *
 * RET - slurm error code
 */
extern int job_container_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/* Create a container for the specified job */
extern int container_g_create(uint32_t job_id);

/* Add a process to the specified job's container.
 * A proctrack containter will be generated containing the process
 * before container_g_add_cont() is called (see below). */
extern int container_g_add_pid(uint32_t job_id, pid_t pid, uid_t uid);

/* Add a proctrack container (PAGG) to the specified job's container
 * The PAGG will be the job's cont_id returned by proctrack/sgi_job */
extern int container_g_add_cont(uint32_t job_id, uint64_t cont_id);

/* Delete the container for the specified job */
extern int container_g_delete(uint32_t job_id);

/* Restore container information */
extern int container_g_restore(char * dir_name, bool recover);

/* Note change in configuration (e.g. "DebugFlag=JobContainer" set) */
extern void container_g_reconfig(void);

#endif /* _JOB_CONTAINER_PLUGIN_H_ */
