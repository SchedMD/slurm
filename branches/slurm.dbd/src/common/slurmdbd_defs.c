/****************************************************************************\
 *  slurmdbd_defs.c - functions for use with Slurm DBD RPCs
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

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include "slurm/slurm_errno.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xmalloc.h"

/*
 * Free data structures
 */
void inline dbd_free_get_jobs_msg(dbd_get_jobs_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_job_complete_msg(dbd_job_comp_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_job_start_msg(dbd_job_start_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_job_submit_msg(dbd_job_submit_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_job_suspend_msg(dbd_job_suspend_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_step_complete_msg(dbd_step_comp_msg_t *msg)
{
	xfree(msg);
}

void inline dbd_free_step_start_msg(dbd_step_start_msg_t *msg)
{
	xfree(msg);
}

/*
 * Pack and unpack data structures
 */
void inline dbd_pack_get_jobs_msg(dbd_get_jobs_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
}

int inline dbd_unpack_get_jobs_msg(dbd_get_jobs_msg_t **msg, Buf buffer)
{
	dbd_get_jobs_msg_t *msg_ptr = xmalloc(sizeof(dbd_get_jobs_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_job_complete_msg(dbd_job_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
}

int inline dbd_unpack_job_complete_msg(dbd_job_comp_msg_t **msg, Buf buffer)
{
	dbd_job_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_job_start_msg(dbd_job_start_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
}

int inline dbd_unpack_job_start_msg(dbd_job_start_msg_t **msg, Buf buffer)
{
	dbd_job_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_start_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_job_submit_msg(dbd_job_submit_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
}

int inline dbd_unpack_job_submit_msg(dbd_job_submit_msg_t **msg, Buf buffer)
{
	dbd_job_submit_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_submit_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_job_suspend_msg(dbd_job_suspend_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
}

int inline dbd_unpack_job_suspend_msg(dbd_job_suspend_msg_t **msg, Buf buffer)
{
	dbd_job_suspend_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_suspend_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_step_complete_msg(dbd_step_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
	pack32(msg->step_id, buffer);
}

int inline dbd_unpack_step_complete_msg(dbd_step_comp_msg_t **msg, Buf buffer)
{
	dbd_step_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline dbd_pack_step_start_msg(dbd_step_start_msg_t *msg, Buf buffer)
{
	pack32(msg->job_id, buffer);
	pack32(msg->step_id, buffer);
}

int inline dbd_unpack_step_start_msg(dbd_step_start_msg_t **msg, Buf buffer)
{
	dbd_step_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_start_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}
