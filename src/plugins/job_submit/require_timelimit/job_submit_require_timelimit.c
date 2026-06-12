/*****************************************************************************\
 *  job_submit_require_timelimit.c - Force job requests to include time limit
 *****************************************************************************
 *  Copyright (C) 2013 Rensselaer Polytechnic Institute
 *  Written by Daniel M. Weeks.
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/slurmctld/slurmctld.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Require time limit jobsubmit plugin";
const char plugin_type[] = "job_submit/require_timelimit";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	// NOTE: no job id actually exists yet (=NO_VAL)

	if (job_desc->time_limit == NO_VAL) {
		info("Missing time limit for job by uid:%u", submit_uid);
		return ESLURM_MISSING_TIME_LIMIT;
	} else if (job_desc->time_limit == INFINITE) {
		info("Bad time limit for job by uid:%u", submit_uid);
		return ESLURM_INVALID_TIME_LIMIT;
	}

	return SLURM_SUCCESS;
}

int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
	       uint32_t submit_uid, char **err_msg)
{
	if (job_desc->time_limit == INFINITE) {
		info("Bad replacement time limit for %pI", &job_desc->step_id);
		return ESLURM_INVALID_TIME_LIMIT;
	}

	return SLURM_SUCCESS;
}
