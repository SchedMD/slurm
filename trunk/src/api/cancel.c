/*****************************************************************************\
 *  cancel.c - cancel a slurm job or job step
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"

/*
 * slurm_kill_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * IN batch_flag - 1 to signal batch shell only, otherwise 0
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_kill_job ( uint32_t job_id, uint16_t signal, uint16_t batch_flag )
{
	int rc;
	slurm_msg_t msg;
	job_step_kill_msg_t req;

	slurm_msg_t_init(&msg);
	/* 
	 * Request message:
	 */
	req.job_id      = job_id;
	req.job_step_id = NO_VAL;
	req.signal      = signal;
	req.batch_flag  = (uint16_t) batch_flag;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
	msg.data        = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

/*
 * Kill a job step with job id "job_id" and step id "step_id", optionally
 *	sending the processes in the job step a signal "signal"
 * IN job_id     - the job's id
 * IN step_id    - the job step's id
 * IN signal     - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_kill_job_step (uint32_t job_id, uint32_t step_id, uint16_t signal)
{
	int rc;
	slurm_msg_t msg;
	job_step_kill_msg_t req;

	slurm_msg_t_init(&msg);
	/* 
	 * Request message:
	 */
	req.job_id      = job_id;
	req.job_step_id = step_id;
	req.signal      = signal;
	req.batch_flag	= false;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
        msg.data        = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}
