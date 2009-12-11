/*****************************************************************************\
 *  partition_info.c - get/print the partition state information of slurm
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
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
	char tmp1[16], tmp2[16];
	char tmp_line[MAXHOSTRANGELEN];
	char *out = NULL;
	uint16_t force, val;

	/****** Line 1 ******/

	snprintf(tmp_line, sizeof(tmp_line),
		 "PartitionName=%s",
		 part_ptr->name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/

	if (part_ptr->allow_alloc_nodes == NULL)
		snprintf(tmp_line, sizeof(tmp_line), "AllocNodes=%s","ALL");
	else
		snprintf(tmp_line, sizeof(tmp_line), "AllocNodes=%s",
			 part_ptr->allow_alloc_nodes);
	xstrcat(out, tmp_line);
	if ((part_ptr->allow_groups == NULL) ||
	    (part_ptr->allow_groups[0] == '\0'))
		sprintf(tmp_line, " AllowGroups=ALL");
	else {
		snprintf(tmp_line, sizeof(tmp_line),
			" AllowGroups=%s", part_ptr->allow_groups);
	}
	xstrcat(out, tmp_line);
	if (part_ptr->default_part)
		sprintf(tmp_line, " Default=YES");
	else
		sprintf(tmp_line, " Default=NO");
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line added here for BG partitions
	 to keep with alphabetized output******/

#ifdef HAVE_BG
	snprintf(tmp_line, sizeof(tmp_line), "BasePartitions=%s",
		part_ptr->nodes);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");
#else
#endif

	/****** Line 3 ******/

	if (part_ptr->default_time == INFINITE)
		sprintf(tmp_line, "DefaultTime=UNLIMITED");
	else if (part_ptr->default_time == NO_VAL)
		sprintf(tmp_line, "DefaultTime=NONE");
	else {
		char time_line[32];
		secs2time_str(part_ptr->default_time * 60, time_line,
			sizeof(time_line));
		sprintf(tmp_line, " DefaultTime=%s", time_line);
	}
	xstrcat(out, tmp_line);
	if (part_ptr->disable_root_jobs)
		sprintf(tmp_line, " DisableRootJobs=YES");
	else
		sprintf(tmp_line, " DisableRootJobs=NO");
	xstrcat(out, tmp_line);

	if (part_ptr->hidden)
		sprintf(tmp_line, " Hidden=YES");
	else
		sprintf(tmp_line, " Hidden=NO");
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/

	if (part_ptr->max_nodes == INFINITE)
		sprintf(tmp_line, "MaxNodes=UNLIMITED");
	else {
#ifdef HAVE_BG
		convert_num_unit((float)part_ptr->max_nodes, tmp1, sizeof(tmp1),
				 UNIT_NONE);
#else
		snprintf(tmp1, sizeof(tmp1),"%u", part_ptr->max_nodes);
#endif
		sprintf(tmp_line, "MaxNodes=%s", tmp1);
	}
	xstrcat(out, tmp_line);
	if (part_ptr->max_time == INFINITE)
		sprintf(tmp_line, " MaxTime=UNLIMITED");
	else {
		char time_line[32];
		secs2time_str(part_ptr->max_time * 60, time_line,
			      sizeof(time_line));
		sprintf(tmp_line, " MaxTime=%s", time_line);
	}
	xstrcat(out, tmp_line);
#ifdef HAVE_BG
	convert_num_unit((float)part_ptr->min_nodes, tmp1, sizeof(tmp1),
			 UNIT_NONE);
#else
	snprintf(tmp1, sizeof(tmp1), "%u", part_ptr->min_nodes);
#endif
	sprintf(tmp_line, " MinNodes=%s", tmp1);
	xstrcat(out, tmp_line);

	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line added here for non BG nodes
	 to keep with alphabetized output******/

#ifdef HAVE_BG
	/***Proceed to non BG option***/
#else
	snprintf(tmp_line, sizeof(tmp_line), "Nodes=%s", part_ptr->nodes);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");
#endif

	/****** Line 6 ******/

	sprintf(tmp_line, "Priority=%u", part_ptr->priority);
	xstrcat(out, tmp_line);
	if (part_ptr->root_only)
		sprintf(tmp_line, " RootOnly=YES");
	else
		sprintf(tmp_line, " RootOnly=NO");
	xstrcat(out, tmp_line);

	force = part_ptr->max_share & SHARED_FORCE;
	val = part_ptr->max_share & (~SHARED_FORCE);
	if (val == 0)
		xstrcat(out, " Shared=EXCLUSIVE");
	else if (force) {
		sprintf(tmp_line, " Shared=FORCE:%u", val);
		xstrcat(out, tmp_line);
	} else if (val == 1)
		xstrcat(out, " Shared=NO");
	else {
		sprintf(tmp_line, " Shared=YES:%u", val);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 7 ******/

	if (part_ptr->state_up)
		sprintf(tmp_line, "State=UP");
	else
		sprintf(tmp_line, "State=DOWN");
	xstrcat(out, tmp_line);

#ifdef HAVE_BG
	convert_num_unit((float)part_ptr->total_cpus, tmp1, sizeof(tmp1),
			 UNIT_NONE);
#else
	snprintf(tmp1, sizeof(tmp1), "%u", part_ptr->total_cpus);
#endif
	sprintf(tmp_line, " TotalCPUs=%s", tmp1);
	xstrcat(out, tmp_line);
#ifdef HAVE_BG
	convert_num_unit((float)part_ptr->total_nodes, tmp2, sizeof(tmp2),
			 UNIT_NONE);
#else
	snprintf(tmp2, sizeof(tmp2), "%u", part_ptr->total_nodes);
#endif
		sprintf(tmp_line, " TotalNodes=%s", tmp2);
	xstrcat(out, tmp_line);
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
		partition_info_msg_t **resp, uint16_t show_flags)
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
