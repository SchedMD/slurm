/*****************************************************************************\
 *  partition_info.c - get/print the partition state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
	char time_str[32];

	slurm_make_time_str ((time_t *)&part_info_ptr->last_update, time_str,
		sizeof(time_str));
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
	char *print_this = slurm_sprint_partition_info(part_ptr, one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}


/*
 * slurm_sprint_partition_info - output information about a specific Slurm
 *	partition based upon message as loaded using slurm_load_partitions
 * IN part_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_partition_info ( partition_info_t * part_ptr,
				    int one_liner )
{
	char tmp[16];
	char *out = NULL;
	char *allow_deny, *value;
	uint16_t force, preempt_mode, val;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *line_end = (one_liner) ? " " : "\n   ";

	/****** Line 1 ******/

	xstrfmtcat(out, "PartitionName=%s", part_ptr->name);
	xstrcat(out, line_end);

	/****** Line 2 ******/

	if ((part_ptr->allow_groups == NULL) ||
	    (part_ptr->allow_groups[0] == '\0'))
		xstrcat(out, "AllowGroups=ALL");
	else {
		xstrfmtcat(out, "AllowGroups=%s", part_ptr->allow_groups);
	}

	if (part_ptr->allow_accounts || !part_ptr->deny_accounts) {
		allow_deny = "Allow";
		if ((part_ptr->allow_accounts == NULL) ||
		    (part_ptr->allow_accounts[0] == '\0'))
			value = "ALL";
		else
			value = part_ptr->allow_accounts;
	} else {
		allow_deny = "Deny";
		value = part_ptr->deny_accounts;
	}
	xstrfmtcat(out, " %sAccounts=%s", allow_deny, value);

	if (part_ptr->allow_qos || !part_ptr->deny_qos) {
		allow_deny = "Allow";
		if ((part_ptr->allow_qos == NULL) ||
		    (part_ptr->allow_qos[0] == '\0'))
			value = "ALL";
		else
			value = part_ptr->allow_qos;
	} else {
		allow_deny = "Deny";
		value = part_ptr->deny_qos;
	}
	xstrfmtcat(out, " %sQos=%s", allow_deny, value);
	xstrcat(out, line_end);

	/****** Line 3 ******/
	if (part_ptr->allow_alloc_nodes == NULL)
		xstrcat(out, "AllocNodes=ALL");
	else
		xstrfmtcat(out, "AllocNodes=%s", part_ptr->allow_alloc_nodes);

	if (part_ptr->alternate != NULL) {
		xstrfmtcat(out, " Alternate=%s", part_ptr->alternate);
	}

	if (part_ptr->flags & PART_FLAG_DEFAULT)
		xstrcat(out, " Default=YES");
	else
		xstrcat(out, " Default=NO");

	if (part_ptr->qos_char)
		xstrfmtcat(out, " QoS=%s", part_ptr->qos_char);
	else
		xstrcat(out, " QoS=N/A");

	xstrcat(out, line_end);

	/****** Line 4 added here for BG partitions only
	 ****** to maintain alphabetized output ******/

	if (cluster_flags & CLUSTER_FLAG_BG) {
		xstrfmtcat(out, "Midplanes=%s", part_ptr->nodes);
		xstrcat(out, line_end);
	}

	/****** Line 5 ******/

	if (part_ptr->default_time == INFINITE)
		xstrcat(out, "DefaultTime=UNLIMITED");
	else if (part_ptr->default_time == NO_VAL)
		xstrcat(out, "DefaultTime=NONE");
	else {
		char time_line[32];
		secs2time_str(part_ptr->default_time * 60, time_line,
			sizeof(time_line));
		xstrfmtcat(out, "DefaultTime=%s", time_line);
	}

	if (part_ptr->flags & PART_FLAG_NO_ROOT)
		xstrcat(out, " DisableRootJobs=YES");
	else
		xstrcat(out, " DisableRootJobs=NO");

	if (part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)
		xstrcat(out, " ExclusiveUser=YES");
	else
		xstrcat(out, " ExclusiveUser=NO");

	xstrfmtcat(out, " GraceTime=%u", part_ptr->grace_time);

	if (part_ptr->flags & PART_FLAG_HIDDEN)
		xstrcat(out, " Hidden=YES");
	else
		xstrcat(out, " Hidden=NO");

	xstrcat(out, line_end);

	/****** Line 6 ******/

	if (part_ptr->max_nodes == INFINITE)
		xstrcat(out, "MaxNodes=UNLIMITED");
	else {
		if (cluster_flags & CLUSTER_FLAG_BG) {
			convert_num_unit((float)part_ptr->max_nodes, tmp,
					 sizeof(tmp), UNIT_NONE, NO_VAL,
					 CONVERT_NUM_UNIT_EXACT);
			xstrfmtcat(out, "MaxNodes=%s", tmp);
		} else
			xstrfmtcat(out, "MaxNodes=%u", part_ptr->max_nodes);

	}

	if (part_ptr->max_time == INFINITE)
		xstrcat(out, " MaxTime=UNLIMITED");
	else {
		char time_line[32];
		secs2time_str(part_ptr->max_time * 60, time_line,
			      sizeof(time_line));
		xstrfmtcat(out, " MaxTime=%s", time_line);
	}

	if (cluster_flags & CLUSTER_FLAG_BG) {
		convert_num_unit((float)part_ptr->min_nodes, tmp, sizeof(tmp),
				 UNIT_NONE, NO_VAL, CONVERT_NUM_UNIT_EXACT);
		xstrfmtcat(out, " MinNodes=%s", tmp);
	} else
		xstrfmtcat(out, " MinNodes=%u", part_ptr->min_nodes);

	if (part_ptr->flags & PART_FLAG_LLN)
		xstrcat(out, " LLN=YES");
	else
		xstrcat(out, " LLN=NO");

	if (part_ptr->max_cpus_per_node == INFINITE)
		xstrcat(out, " MaxCPUsPerNode=UNLIMITED");
	else {
		xstrfmtcat(out, " MaxCPUsPerNode=%u",
			   part_ptr->max_cpus_per_node);
	}

	xstrcat(out, line_end);

	/****** Line added here for non BG nodes
	 to keep with alphabetized output******/

	if (!(cluster_flags & CLUSTER_FLAG_BG)) {
		xstrfmtcat(out, "Nodes=%s", part_ptr->nodes);
		xstrcat(out, line_end);
	}

	/****** Line 7 ******/

	xstrfmtcat(out, "PriorityJobFactor=%u", part_ptr->priority_job_factor);
	xstrfmtcat(out, " PriorityTier=%u", part_ptr->priority_tier);

	if (part_ptr->flags & PART_FLAG_ROOT_ONLY)
		xstrcat(out, " RootOnly=YES");
	else
		xstrcat(out, " RootOnly=NO");

	if (part_ptr->flags & PART_FLAG_REQ_RESV)
		xstrcat(out, " ReqResv=YES");
	else
		xstrcat(out, " ReqResv=NO");

	force = part_ptr->max_share & SHARED_FORCE;
	val = part_ptr->max_share & (~SHARED_FORCE);
	if (val == 0)
		xstrcat(out, " OverSubscribe=EXCLUSIVE");
	else if (force)
		xstrfmtcat(out, " OverSubscribe=FORCE:%u", val);
	else if (val == 1)
		xstrcat(out, " OverSubscribe=NO");
	else
		xstrfmtcat(out, " OverSubscribe=YES:%u", val);

	xstrcat(out, line_end);

	/****** Line ******/
	if (part_ptr->over_time_limit == NO_VAL16)
		xstrfmtcat(out, "OverTimeLimit=NONE");
	else if (part_ptr->over_time_limit == (uint16_t) INFINITE)
		xstrfmtcat(out, "OverTimeLimit=UNLIMITED");
	else
		xstrfmtcat(out, "OverTimeLimit=%u", part_ptr->over_time_limit);

	preempt_mode = part_ptr->preempt_mode;
	if (preempt_mode == NO_VAL16)
		preempt_mode = slurm_get_preempt_mode(); /* use cluster param */
	xstrfmtcat(out, " PreemptMode=%s", preempt_mode_string(preempt_mode));

	xstrcat(out, line_end);

	/****** Line ******/
	if (part_ptr->state_up == PARTITION_UP)
		xstrcat(out, "State=UP");
	else if (part_ptr->state_up == PARTITION_DOWN)
		xstrcat(out, "State=DOWN");
	else if (part_ptr->state_up == PARTITION_INACTIVE)
		xstrcat(out, "State=INACTIVE");
	else if (part_ptr->state_up == PARTITION_DRAIN)
		xstrcat(out, "State=DRAIN");
	else
		xstrcat(out, "State=UNKNOWN");

	if (cluster_flags & CLUSTER_FLAG_BG) {
		convert_num_unit((float)part_ptr->total_cpus, tmp, sizeof(tmp),
				 UNIT_NONE, NO_VAL, CONVERT_NUM_UNIT_EXACT);
		xstrfmtcat(out, " TotalCPUs=%s", tmp);
	} else
		xstrfmtcat(out, " TotalCPUs=%u", part_ptr->total_cpus);


	if (cluster_flags & CLUSTER_FLAG_BG) {
		convert_num_unit((float)part_ptr->total_nodes, tmp, sizeof(tmp),
				 UNIT_NONE, NO_VAL, CONVERT_NUM_UNIT_EXACT);
		xstrfmtcat(out, " TotalNodes=%s", tmp);
	} else
		xstrfmtcat(out, " TotalNodes=%u", part_ptr->total_nodes);

	xstrfmtcat(out, " SelectTypeParameters=%s",
		   select_type_param_string(part_ptr->cr_type));

	xstrcat(out, line_end);

	/****** Line 9 ******/
	if (part_ptr->def_mem_per_cpu & MEM_PER_CPU) {
		if (part_ptr->def_mem_per_cpu == MEM_PER_CPU) {
			xstrcat(out, "DefMemPerCPU=UNLIMITED");
		} else {
			xstrfmtcat(out, "DefMemPerCPU=%"PRIu64"",
				   part_ptr->def_mem_per_cpu & (~MEM_PER_CPU));
		}
	} else if (part_ptr->def_mem_per_cpu == 0) {
		xstrcat(out, "DefMemPerNode=UNLIMITED");
	} else {
		xstrfmtcat(out, "DefMemPerNode=%"PRIu64"", part_ptr->def_mem_per_cpu);
	}

	if (part_ptr->max_mem_per_cpu & MEM_PER_CPU) {
		if (part_ptr->max_mem_per_cpu == MEM_PER_CPU) {
			xstrcat(out, " MaxMemPerCPU=UNLIMITED");
		} else {
			xstrfmtcat(out, " MaxMemPerCPU=%"PRIu64"",
				   part_ptr->max_mem_per_cpu & (~MEM_PER_CPU));
		}
	} else if (part_ptr->max_mem_per_cpu == 0) {
		xstrcat(out, " MaxMemPerNode=UNLIMITED");
	} else {
		xstrfmtcat(out, " MaxMemPerNode=%"PRIu64"", part_ptr->max_mem_per_cpu);
	}

	/****** Line 10 ******/
	if (part_ptr->billing_weights_str) {
		xstrcat(out, line_end);

		xstrfmtcat(out, "TRESBillingWeights=%s",
			   part_ptr->billing_weights_str);
	}

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}


/*
 * slurm_load_partitions - issue RPC to get slurm all partition configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN partition_info_msg_pptr - place to store a partition configuration
 *	pointer
 * IN show_flags - partition filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_partition_info_msg
 */
extern int slurm_load_partitions (time_t update_time,
				  partition_info_msg_t **resp,
				  uint16_t show_flags)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
        part_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_PARTITION_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

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
