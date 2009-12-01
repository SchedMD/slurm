/*****************************************************************************\
 *  suspend.c - job step suspend and resume functions.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm.h>
#include "src/common/slurm_protocol_api.h"

static int _suspend_op (uint16_t op, uint32_t job_id);
/*
 * _suspend_op - perform a suspend/resume operation for some job.
 * IN op      - operation to perform
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
static int _suspend_op (uint16_t op, uint32_t job_id)
{
	int rc;
	suspend_msg_t sus_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	sus_req.op       = op;
	sus_req.job_id   = job_id;
	req_msg.msg_type = REQUEST_SUSPEND;
	req_msg.data     = &sus_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_ERROR;

	slurm_seterrno(rc);
	return rc;
}

/*
 * slurm_suspend - suspend execution of a job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_suspend (uint32_t job_id)
{
	return _suspend_op (SUSPEND_JOB, job_id);
}

/*
 * slurm_resume - resume execution of a previously suspended job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_resume (uint32_t job_id)
{
	return _suspend_op (RESUME_JOB, job_id);
}

/*
 * slurm_requeue - re-queue a batch job, if already running
 *	then terminate it first
 * RET 0 or a slurm error code
 */
extern int slurm_requeue (uint32_t job_id)
{
	int rc;
	job_id_msg_t requeue_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	requeue_req.job_id	= job_id;
	req_msg.msg_type	= REQUEST_JOB_REQUEUE;
	req_msg.data		= &requeue_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_ERROR;

	slurm_seterrno(rc);
	return rc;
}

