/*****************************************************************************\
 *  partition_info.c - get/print the partition state information of slurm
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

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"

/*
 * slurm_print_partition_info_msg - output information about all Slurm 
 *	partitions based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_info_ptr - partitions information message pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_partition_info_msg ( FILE* out, 
		partition_info_msg_t * part_info_ptr, int one_liner )
{
	int i ;
	partition_info_t * part_ptr = part_info_ptr->partition_array ;
	char time_str[16];

	make_time_str ((time_t *)&part_info_ptr->last_update, time_str);
	fprintf( out, "Partition data as of %s, record count %d\n",
		time_str, part_info_ptr->record_count);

	for (i = 0; i < part_info_ptr->record_count; i++) {
		slurm_print_partition_info ( out, & part_ptr[i], one_liner ) ;
	}

}

/*
 * slurm_print_partition_info - output information about a specific Slurm 
 *	partition based upon message as loaded using slurm_load_partitions
 * IN out - file to write to
 * IN part_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_partition_info ( FILE* out, partition_info_t * part_ptr, 
				  int one_liner )
{
	int j ;

	/****** Line 1 ******/
	fprintf ( out, "PartitionName=%s ", part_ptr->name);
	fprintf ( out, "TotalNodes=%u ", part_ptr->total_nodes);
	fprintf ( out, "TotalCPUs=%u ", part_ptr->total_cpus);
	if (part_ptr->root_only)
		fprintf ( out, "RootOnly=YES");
	else
		fprintf ( out, "RootOnly=NO");
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 2 ******/
	if (part_ptr->default_part)
		fprintf ( out, "Default=YES ");
	else
		fprintf ( out, "Default=NO ");
	if (part_ptr->shared == SHARED_NO)
		fprintf ( out, "Shared=NO ");
	else if (part_ptr->shared == SHARED_YES)
		fprintf ( out, "Shared=YES ");
	else
		fprintf ( out, "Shared=FORCE ");
	if (part_ptr->state_up)
		fprintf ( out, "State=UP ");
	else
		fprintf ( out, "State=DOWN ");
	if (part_ptr->max_time == INFINITE)
		fprintf ( out, "MaxTime=UNLIMITED");
	else
		fprintf ( out, "MaxTime=%u", part_ptr->max_time);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 3 ******/
	fprintf ( out, "MinNodes=%u ", part_ptr->min_nodes);
	if (part_ptr->max_nodes == INFINITE)
		fprintf ( out, "MaxNodes=UNLIMITED ");
	else
		fprintf ( out, "MaxNodes=%u ", part_ptr->max_nodes);
	fprintf ( out, "AllowGroups=%s", part_ptr->allow_groups);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 4 ******/
	fprintf ( out, "Nodes=%s NodeIndecies=", part_ptr->nodes);
	for (j = 0; part_ptr->node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", part_ptr->node_inx[j]);
		else
			fprintf( out, "%d", part_ptr->node_inx[j]);
		if (part_ptr->node_inx[j] == -1)
			break;
	}
	fprintf( out, "\n\n");
}



/*
 * slurm_load_partitions - issue RPC to get slurm all partition configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN partition_info_msg_pptr - place to store a partition configuration 
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_partition_info_msg
 */
int
slurm_load_partitions (time_t update_time, partition_info_msg_t **resp)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
        last_update_msg_t req;

        req.last_update  = update_time;
        req_msg.msg_type = REQUEST_PARTITION_INFO;
        req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_PARTITION_INFO:
		*resp = (partition_info_msg_t *) resp_msg.data;
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
