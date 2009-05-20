/*****************************************************************************\
 *  complete.c - note the completion a slurm job or job step
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"

/*
 * slurm_complete_job - note the completion of a job allocation
 * IN job_id - the job's id
 * IN job_return_code - the highest exit code of any task of the job
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_complete_job ( uint32_t job_id, uint32_t job_return_code )
{
	int rc;
	slurm_msg_t req_msg;
	complete_job_allocation_msg_t req;

	slurm_msg_t_init(&req_msg);
	req.job_id      = job_id;
	req.job_rc      = job_return_code;

	req_msg.msg_type= REQUEST_COMPLETE_JOB_ALLOCATION;
	req_msg.data	= &req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
	       return SLURM_ERROR;	
	
	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_PROTOCOL_SUCCESS;
}
