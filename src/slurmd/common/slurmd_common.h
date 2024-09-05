/*****************************************************************************\
 *  slurmd_common.c - functions for determining job status
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _JOB_STATUS_H
#define _JOB_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/*
 *  Send epilog complete message to currently active controller.
 *   Returns SLURM_SUCCESS if message sent successfully,
 *           SLURM_ERROR if epilog complete message fails to be sent.
 */
extern int epilog_complete(uint32_t jobid, char *node_list, int rc);

/* Connect to the slurmd and determine if the requested job has running steps */
extern bool is_job_running(uint32_t job_id, bool ignore_extern);

/*
 *  Wait for up to max_time seconds.
 *  If max_time == 0, send SIGKILL to tasks repeatedly
 *
 *  Returns true if all job processes are gone
 */
extern bool pause_for_job_completion(uint32_t job_id, int max_time,
				     bool ignore_extern);

/*
 * terminate_all_steps - signals the container of all steps of a job
 * jobid IN - id of job to signal
 * batch IN - if true signal batch script, otherwise skip it
 * RET count of signaled job steps (plus batch script, if applicable)
 */
extern int terminate_all_steps(uint32_t jobid, bool batch, bool extern_step);


extern int run_prolog(job_env_t *job_env, slurm_cred_t *cred);

extern int run_epilog(job_env_t *job_env, slurm_cred_t *cred);

#endif /* _JOB_STATUS_H */
