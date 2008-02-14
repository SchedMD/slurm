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

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"

/* Local functions */
static int   _get_jobs(Buf buffer);
static int   _init_conn(Buf buffer);
static int   _job_complete(Buf buffer);
static int   _job_start(Buf buffer);
static int   _job_submit(Buf buffer);
static int   _job_suspend(Buf buffer);
static int   _step_complete(Buf buffer);
static int   _step_start(Buf bufferg);

/* Process an incoming RPC
 * RET SLURM_SUCCESS or error code */
extern int proc_req(char *msg, uint32_t msg_size, bool first)
{
	int rc = SLURM_SUCCESS;
	uint16_t msg_type;
	Buf buffer;

	buffer = create_buf(msg, msg_size);  /* puts msg into buffer struct */
	safe_unpack16(&msg_type, buffer);

	if (first && (msg_type != DBD_INIT)) {
		error("Initial RPC not DBD_INIT type (%d)", msg_type);
		rc = EINVAL;
	} else {
		switch (msg_type) {
		case DBD_INIT:
			rc = _init_conn(buffer);
			break;
		case DBD_GET_JOBS:
			rc = _get_jobs(buffer);
			break;
		case DBD_JOB_COMPLETE:
			rc = _job_complete(buffer);
			break;
		case DBD_JOB_START:
			rc = _job_start(buffer);
			break;
		case DBD_JOB_SUBMIT:
			rc = _job_submit(buffer);
			break;
		case DBD_JOB_SUSPEND:
			rc = _job_suspend(buffer);
			break;
		case DBD_STEP_COMPLETE:
			rc = _step_complete(buffer);
			break;
		case DBD_STEP_START:
			rc = _step_start(buffer);
			break;
		default:
			error("invalid RPC msg_type=%d", msg_type);
			rc = EINVAL;
			break;
		}
	}

	xfer_buf_data(buffer);	/* delete buffer struct without xfree of msg */
	return rc;

unpack_error:
	free_buf(buffer);
	return SLURM_ERROR;
}

static int _get_jobs(Buf buffer)
{
	dbd_get_jobs_msg_t *get_jobs_msg;

	if (slurm_dbd_unpack_get_jobs_msg(&get_jobs_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_JOBS message");
		return SLURM_ERROR;
	}

	info("DBD_GET_JOBS: job filter %u", get_jobs_msg->job_id);
	slurm_dbd_free_get_jobs_msg(get_jobs_msg);
	return SLURM_SUCCESS;
}

static int _init_conn(Buf buffer)
{
	dbd_init_msg_t *init_msg;

	if (slurm_dbd_unpack_init_msg(&init_msg, buffer) != SLURM_SUCCESS) {
		error("Failed to unpack DBD_INIT message");
		return SLURM_ERROR;
	}
	if (init_msg->version != SLURM_DBD_VERSION) {
		error("Incompatable RPC version (%d != %d)",
			init_msg->version, SLURM_DBD_VERSION);
		return SLURM_ERROR;
	}

	info("DBD_INIT: %u", init_msg->version);
	slurm_dbd_free_init_msg(init_msg);
	return SLURM_SUCCESS;
}

static int  _job_complete(Buf buffer)
{
	dbd_job_comp_msg_t *job_comp_msg;

	if (slurm_dbd_unpack_job_complete_msg(&job_comp_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_COMPLETE message");
		return SLURM_ERROR;
	}

	info("DBD_JOB_COMPLETE: %u", job_comp_msg->job_id);
	slurm_dbd_free_job_complete_msg(job_comp_msg);
	return SLURM_SUCCESS;
}

static int  _job_start(Buf buffer)
{
	dbd_job_start_msg_t *job_start_msg;

	if (slurm_dbd_unpack_job_start_msg(&job_start_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_START message");
		return SLURM_ERROR;
	}

	info("DBD_JOB_START: %u", job_start_msg->job_id);
	slurm_dbd_free_job_start_msg(job_start_msg);
	return SLURM_SUCCESS;
}

static int  _job_submit(Buf buffer)
{
	dbd_job_submit_msg_t *job_submit_msg;

	if (slurm_dbd_unpack_job_submit_msg(&job_submit_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_SUBMIT message");
		return SLURM_ERROR;
	}

	info("DBD_JOB_SUBMIT: %u", job_submit_msg->job_id);
	slurm_dbd_free_job_submit_msg(job_submit_msg);
	return SLURM_SUCCESS;
}

static int  _job_suspend(Buf buffer)
{
	dbd_job_suspend_msg_t *job_suspend_msg;

	if (slurm_dbd_unpack_job_suspend_msg(&job_suspend_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_SUSPEND message");
		return SLURM_ERROR;
	}

	info("DBD_JOB_SUSPEND: %u", job_suspend_msg->job_id);
	slurm_dbd_free_job_suspend_msg(job_suspend_msg);
	return SLURM_SUCCESS;
}

static int  _step_complete(Buf buffer)
{
	dbd_step_comp_msg_t *step_comp_msg;

	if (slurm_dbd_unpack_step_complete_msg(&step_comp_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_COMPLETE message");
		return SLURM_ERROR;
	}

	info("DBD_STEP_COMPLETE: %u.%u", 
	     step_comp_msg->job_id, step_comp_msg->step_id);
	slurm_dbd_free_step_complete_msg(step_comp_msg);
	return SLURM_SUCCESS;
}

static int  _step_start(Buf buffer)
{
	dbd_step_start_msg_t *step_start_msg;

	if (slurm_dbd_unpack_step_start_msg(&step_start_msg, buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_START message");
		return SLURM_ERROR;
	}

	info("DBD_STEP_START: %u.%u", 
	     step_start_msg->job_id, step_start_msg->step_id);
	slurm_dbd_free_step_start_msg(step_start_msg);
	return SLURM_SUCCESS;
}
