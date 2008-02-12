/*****************************************************************************\
 *  proc_req.c - functions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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

#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"

/* Local functions */
static void  _get_jobs(slurm_msg_t *msg);
static void  _job_complete(slurm_msg_t *msg);
static void  _job_start(slurm_msg_t *msg);
static void  _job_submit(slurm_msg_t *msg);
static void  _job_suspend(slurm_msg_t *msg);
static void  _step_complete(slurm_msg_t *msg);
static void  _step_start(slurm_msg_t *msg);

/* Process an incoming RPC
 * RET SLURM_SUCCESS or error code */
extern int proc_req(slurm_msg_t *msg)
{
	switch (msg->msg_type) {
	case DBD_GET_JOBS:
		_get_jobs(msg);
		slurm_dbd_free_get_jobs_msg(msg->data);
		break;
	case DBD_JOB_COMPLETE:
		_job_complete(msg);
		slurm_dbd_free_job_complete_msg(msg->data);
		break;
	case DBD_JOB_START:
		_job_start(msg);
		slurm_dbd_free_job_start_msg(msg->data);
		break;
	case DBD_JOB_SUBMIT:
		_job_submit(msg);
		slurm_dbd_free_job_submit_msg(msg->data);
		break;
	case DBD_JOB_SUSPEND:
		_job_suspend(msg);
		slurm_dbd_free_job_suspend_msg(msg->data);
		break;
	case DBD_STEP_COMPLETE:
		_step_complete(msg);
		slurm_dbd_free_step_complete_msg(msg->data);
		break;
	case DBD_STEP_START:
		_step_start(msg);
		slurm_dbd_free_step_start_msg(msg->data);
		break;
	default:
		error("invalid RPC msg_type=%d", msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}
	return SLURM_SUCCESS;
}

static void  _get_jobs(slurm_msg_t *msg)
{
	dbd_get_jobs_msg_t *get_jobs_msg = (dbd_get_jobs_msg_t *) msg->data;

	info("DBD_GET_JOBS: job filter %u", get_jobs_msg->job_id);
}

static void  _job_complete(slurm_msg_t *msg)
{
	dbd_job_comp_msg_t *job_comp_msg = (dbd_job_comp_msg_t *) msg->data;

	info("DBD_JOB_COMPLETE: %u", job_comp_msg->job_id);
}

static void  _job_start(slurm_msg_t *msg)
{
	dbd_job_start_msg_t *job_start_msg = (dbd_job_start_msg_t *) msg->data;

	info("DBD_JOB_START: %u", job_start_msg->job_id);
}

static void  _job_submit(slurm_msg_t *msg)
{
	dbd_job_submit_msg_t *job_submit_msg = 
				(dbd_job_submit_msg_t *) msg->data;

	info("DBD_JOB_SUBMIT: %u", job_submit_msg->job_id);
}

static void  _job_suspend(slurm_msg_t *msg)
{
	dbd_job_suspend_msg_t *job_suspend_msg = 
				(dbd_job_suspend_msg_t *) msg->data;

	info("DBD_JOB_SUSPEND: %u", job_suspend_msg->job_id);
}

static void  _step_complete(slurm_msg_t *msg)
{
	dbd_step_comp_msg_t *step_comp_msg = (dbd_step_comp_msg_t *) msg->data;

	info("DBD_STEP_COMPLETE: %u.%u", 
	     step_comp_msg->job_id, step_comp_msg->step_id);
}

static void  _step_start(slurm_msg_t *msg)
{
	dbd_step_start_msg_t *step_start_msg = (dbd_step_start_msg_t *) msg->data;

	info("DBD_STEP_START: %u.%u", 
	     step_start_msg->job_id, step_start_msg->step_id);
}
