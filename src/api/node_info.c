/*****************************************************************************\
 *  node_info.c - get/print the node state information of slurm
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
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

#include "src/common/slurm_protocol_api.h"

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_info_msg_ptr - node information message pointer
 * IN one_liner - print as a single line if true
 */
void 
slurm_print_node_info_msg ( FILE * out, node_info_msg_t * node_info_msg_ptr, 
			    int one_liner )
{
	int i;
	node_info_t * node_ptr = node_info_msg_ptr -> node_array ;
	char time_str[16];

	make_time_str ((time_t *)&node_info_msg_ptr->last_update, time_str);
	fprintf( out, "Node data as of %s, record count %d\n",
		time_str, node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) {
		slurm_print_node_table ( out, & node_ptr[i], one_liner ) ;
	}
}


/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_node_table ( FILE * out, node_info_t * node_ptr, int one_liner )
{
	/****** Line 1 ******/
	fprintf ( out, "NodeName=%s State=%s CPUs=%u ", 
		node_ptr->name, node_state_string(node_ptr->node_state), 
		node_ptr->cpus);
	fprintf ( out, "RealMemory=%u TmpDisk=%u", 
		node_ptr->real_memory, node_ptr->tmp_disk);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 2 ******/
	fprintf ( out, "Weight=%u Partition=%s Features=%s", 
		node_ptr->weight, node_ptr->partition, node_ptr->features);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");


	/****** Line 3 ******/
	fprintf ( out, "Reason=%s", node_ptr->reason);
	if (one_liner)
		fprintf ( out, "\n");
	else
		fprintf ( out, "\n\n");
}


/*
 * slurm_load_node - issue RPC to get slurm all node configuration information 
 *	if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
int
slurm_load_node (time_t update_time, node_info_msg_t **resp)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
        last_update_msg_t req;

        req.last_update  = update_time;
        req_msg.msg_type = REQUEST_NODE_INFO;
        req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_INFO:
		*resp = (node_info_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

        return SLURM_PROTOCOL_SUCCESS;
}
