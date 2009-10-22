/*****************************************************************************\
 *  node_info.c - get/print the node state information of slurm
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
	char time_str[32];

	slurm_make_time_str ((time_t *)&node_info_msg_ptr->last_update, 
			     time_str, sizeof(time_str));
	fprintf( out, "Node data as of %s, record count %d\n",
		 time_str, node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) {
		slurm_print_node_table ( out, & node_ptr[i], 
					 node_info_msg_ptr->node_scaling,
					 one_liner ) ;
	}
}


/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 * IN node_scaling - number of nodes each node represents
 * IN one_liner - print as a single line if true
 */
void
slurm_print_node_table ( FILE * out, node_info_t * node_ptr, 
			 int node_scaling, int one_liner )
{
	char *print_this = slurm_sprint_node_table(node_ptr, node_scaling, 
						   one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}

/*
 * slurm_sprint_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN node_ptr - an individual node information record pointer
 * IN node_scaling - number of nodes each node represents
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *
slurm_sprint_node_table (node_info_t * node_ptr, 
			 int node_scaling, int one_liner )
{
	uint16_t my_state = node_ptr->node_state;
	char *comp_str = "", *drain_str = "", *power_str = "";
	char tmp_line[512];
	char *out = NULL;
	uint16_t err_cpus = 0, alloc_cpus = 0;
	int cpus_per_node = 1;
	int total_used = node_ptr->cpus;

	if(node_scaling) 
		cpus_per_node = node_ptr->cpus / node_scaling;

	if (my_state & NODE_STATE_COMPLETING) {
		my_state &= (~NODE_STATE_COMPLETING);
		comp_str = "+COMPLETING";
	}
	if (my_state & NODE_STATE_DRAIN) {
		my_state &= (~NODE_STATE_DRAIN);
		drain_str = "+DRAIN";
	}
	if (my_state & NODE_STATE_POWER_SAVE) {
		my_state &= (~NODE_STATE_POWER_SAVE);
		power_str = "+POWER";
	}
	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo, 
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ALLOCATED,
				  &alloc_cpus);
#ifdef HAVE_BG
	if(!alloc_cpus 
	   && (IS_NODE_ALLOCATED(node_ptr) || IS_NODE_COMPLETING(node_ptr)))
		alloc_cpus = node_ptr->cpus;
	else
		alloc_cpus *= cpus_per_node;
#endif
	total_used -= alloc_cpus;

	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo, 
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ERROR,
				  &err_cpus);
#ifdef HAVE_BG
	err_cpus *= cpus_per_node;
#endif
	total_used -= err_cpus;

	if ((alloc_cpus && err_cpus) || 
	    (total_used  && (total_used != node_ptr->cpus))) {
		my_state &= NODE_STATE_FLAGS;
		my_state |= NODE_STATE_MIXED;
	}

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "NodeName=%s",
		 node_ptr->name);
	xstrcat(out, tmp_line);
	if (node_ptr->arch ) {
		snprintf(tmp_line, sizeof(tmp_line),
			 " Arch=%s",
			 node_ptr->arch);
		xstrcat(out, tmp_line);
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 " CoresPerSocket=%u",
		 node_ptr->cores);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/

	snprintf(tmp_line, sizeof(tmp_line),
		 "CPUAlloc=%u CPUErr=%u CPUTot=%u Features=%s",
		 alloc_cpus, err_cpus, node_ptr->cpus, node_ptr->features);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");
	
	/****** Line 3 ******/
	if (node_ptr->os ) {
		snprintf(tmp_line, sizeof(tmp_line),
			 "OS=%s",
			 node_ptr->os);
		xstrcat(out, tmp_line);
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 " RealMemory=%u Reason=%s",
		 node_ptr->real_memory, node_ptr->reason);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/

	snprintf(tmp_line, sizeof(tmp_line),
		 "Sockets=%u State=%s%s%s%s ThreadsPerCore=%u "
		 "TmpDisk=%u Weight=%u",
		 node_ptr->sockets, node_state_string(my_state),
		 comp_str, drain_str, power_str, node_ptr->threads, 
		 node_ptr->tmp_disk, node_ptr->weight);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n");
	return out;
}


/*
 * slurm_load_node - issue RPC to get slurm all node configuration information 
 *	if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node (time_t update_time, 
			    node_info_msg_t **resp, uint16_t show_flags)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
        node_info_request_msg_t req;
	
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
        req.last_update  = update_time;
	req.show_flags   = show_flags;
        req_msg.msg_type = REQUEST_NODE_INFO;
        req_msg.data     = &req;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;
		
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
