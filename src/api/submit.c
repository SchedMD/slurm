/*****************************************************************************\
 *  submit.c - submit a job with supplied contraints
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include "slurm/slurm.h"

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_submit_batch_job - issue RPC to submit a job for later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_desc_msg - description of batch job request
 * OUT resp - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_submit_batch_job(job_desc_msg_t *req,
				  submit_response_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	req_msg.msg_type = REQUEST_SUBMIT_BATCH_JOB;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);
	if (rc == SLURM_ERROR)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		*resp = (submit_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_submit_batch_pack_job - issue RPC to submit a heterogeneous job for
 *				 later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_req_list - List of resource allocation requests, type job_desc_msg_t
 * OUT resp - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_submit_batch_pack_job(List job_req_list,
				       submit_response_msg_t **resp)
{
	int rc;
	job_desc_msg_t *req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	ListIterator iter;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/*
	 * set session id for this request
	 */
	iter = list_iterator_create(job_req_list);
	while ((req = (job_desc_msg_t *) list_next(iter))) {
		if (req->alloc_sid == NO_VAL)
			req->alloc_sid = getsid(0);
	}
	list_iterator_destroy(iter);

	req_msg.msg_type = REQUEST_SUBMIT_BATCH_JOB_PACK;
	req_msg.data     = job_req_list;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);
	if (rc == SLURM_ERROR)
		return SLURM_ERROR;
	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		*resp = (submit_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_SUCCESS;
}
