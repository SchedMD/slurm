/****************************************************************************\
 *  slurm_pmi.c - PMI support functions internal to SLURM
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/api/slurm_pmi.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"

/* Transmit PMI Keyval space data */
int slurm_send_kvs_comm_set(struct kvs_comm_set *kvs_set_ptr)
{
	slurm_msg_t msg;
	int rc;

	if (kvs_set_ptr == NULL)
		return EINVAL;

	msg.msg_type = PMI_KVS_PUT_REQ;
	msg.data = (void *) kvs_set_ptr;

	/* Send the RPC to the local slurmd_step manager */
/* FIXME, sending to slurmctld right now */
/* The RPC has been verified to function properly */
#if 0
	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;

	if (rc)
		slurm_seterrno_ret(rc);
#endif
	return SLURM_SUCCESS;
}

/* Wait for barrier and get full PMI Keyval space data */
int  slurm_get_kvs_comm_set(struct kvs_comm_set **kvs_set_ptr)
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	if (kvs_set_ptr == NULL)
		return EINVAL;
	req_msg.msg_type = PMI_KVS_GET_REQ;
	req_msg.data = NULL;

	/* Send the RPC to the local slurmd_step manager */
/* FIXME, sending to slurmctld right now */
#if 0
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
	if (resp_msg.msg_type == PMI_KVS_GET_RESP)
		*kvs_set_ptr = (struct kvs_comm_set *) resp_msg.data;
	else
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
#else
	*kvs_set_ptr = NULL;
#endif
	return SLURM_SUCCESS;
}

static void _free_kvs_comm(struct kvs_comm *kvs_comm_ptr)
{
	int i;

	for (i=0; i<kvs_comm_ptr->kvs_cnt; i++) {
		xfree(kvs_comm_ptr->kvs_keys[i]);
		xfree(kvs_comm_ptr->kvs_values[i]);
	}
	xfree(kvs_comm_ptr->kvs_name);
	xfree(kvs_comm_ptr->kvs_keys);
	xfree(kvs_comm_ptr->kvs_values);
	xfree(kvs_comm_ptr);
}

/* Free kvs_comm_set returned by slurm_get_kvs_comm_set() */
void slurm_free_kvs_comm_set(struct kvs_comm_set *kvs_set_ptr)
{
	int i;

	for (i=0; i<kvs_set_ptr->kvs_comm_recs; i++)
		_free_kvs_comm(kvs_set_ptr->kvs_comm_ptr[i]);
	xfree(kvs_set_ptr->kvs_comm_ptr);
	xfree(kvs_set_ptr);
}

