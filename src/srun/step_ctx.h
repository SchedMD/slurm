/*****************************************************************************\
 *  step_ctx.h - step context declarations
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
 *  Christopher J. Morrone <morrone2@llnl.gov>
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _SRUN_STEP_CTX_H
#define _SRUN_STEP_CTX_H

#include <inttypes.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/api/step_launch.h"

#include "src/common/step_ctx.h"

/*
 * step_ctx_create_timeout - Create a job step and its context.
 * IN step_req - job step request
 * IN timeout - in milliseconds
 * IN srun_opt - srun options
 * OUT timed_out - indicate if poll timed-out
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using step_ctx_destroy()
 */
extern slurm_step_ctx_t *step_ctx_create_timeout(
	job_step_create_request_msg_t *step_req, int timeout, bool *timed_out,
	srun_opt_t *srun_opt);

/*
 * step_ctx_create_no_alloc - Create a job step and its context without
 *                            getting an allocation.
 * IN step_req - job step request
 * IN step_id     - since we are faking it give me the id to use
 * RET the step context or NULL on failure with slurm errno set
 * NOTE: Free allocated memory using step_ctx_destroy()
 */
extern slurm_step_ctx_t *step_ctx_create_no_alloc(
	job_step_create_request_msg_t *step_req, uint32_t step_id);

#endif /* _SRUN_STEP_CTX_H */
