/*****************************************************************************\
 *  job_state_reason.h
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

#ifndef _JOB_STATE_REASON_H
#define _JOB_STATE_REASON_H

#include "slurm/slurm.h"

#define JSR_QOS_GRP SLURM_BIT(0) /* job is held because of a QOS GRP limit */
#define JSR_QOS_ASSOC SLURM_BIT(1) /* job is held because of a QOS or ASSOC
				    * limit */
#define JSR_MISC SLURM_BIT(2) /* job is held because of a miscellaneous limit */
#define JSR_PART SLURM_BIT(3) /* job is held because of a partition limit */

/* Given a job's reason for waiting, return a descriptive string */
extern const char *job_state_reason_string(enum job_state_reason inx);

/* Given a job's reason string for waiting, return enum job_state_reason */
extern enum job_state_reason job_state_reason_num(char *reason);

/*
 * Determine if the job_state_reason is using any flags given or not.
 */
extern bool job_state_reason_check(enum job_state_reason inx, uint32_t flags);

#endif
