/*****************************************************************************\
 *  node_select_info.c - get the node select plugin state information of slurm
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifdef HAVE_SYS_SYSLOG_H
#  include <sys/syslog.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/api/node_select_info.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

/*
 * slurm_load_node_select - issue RPC to get slurm all node select plugin 
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_select_info_msg_pptr - place to store a node select configuration 
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_select_info_msg
 */
extern int slurm_load_node_select (time_t update_time, 
		node_select_info_msg_t **node_select_info_msg_pptr)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
	node_info_select_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

        req.last_update  = update_time;
        req_msg.msg_type = REQUEST_NODE_SELECT_INFO;
        req_msg.data     = &req;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;
	
	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_SELECT_INFO:
		*node_select_info_msg_pptr = (node_select_info_msg_t *) 
			resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		*node_select_info_msg_pptr = NULL;
		break;
	default:
		*node_select_info_msg_pptr = NULL;
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

        return SLURM_SUCCESS;
}

extern int slurm_free_node_select(
	node_select_info_msg_t **node_select_info_msg_pptr)
{
	return node_select_info_msg_free(node_select_info_msg_pptr);
}

/* Unpack node select info from a buffer */
extern int slurm_unpack_node_select(
	node_select_info_msg_t **node_select_info_msg_pptr, Buf buffer)
{
	return node_select_info_msg_unpack(node_select_info_msg_pptr, buffer);
}

extern int slurm_get_select_jobinfo(select_jobinfo_t *jobinfo,
				    enum select_jobdata_type data_type,
				    void *data)
{
	return select_g_select_jobinfo_get(jobinfo, data_type, data);
}

extern int slurm_get_select_nodeinfo(select_nodeinfo_t *nodeinfo, 
				     enum select_nodedata_type data_type,
				     enum node_states state, void *data)
{
	return select_g_select_nodeinfo_get(nodeinfo, data_type, state, data);
}
