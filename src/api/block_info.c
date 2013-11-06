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
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "slurm/slurm.h"

#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_print_block_info_msg - output information about all Bluegene
 *	blocks based upon message as loaded using slurm_load_block
 * IN out - file to write to
 * IN info_ptr - block information message pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_block_info_msg(
	FILE *out, block_info_msg_t *info_ptr, int one_liner)
{
	int i ;
	block_info_t * block_ptr = info_ptr->block_array;
	char time_str[32];

	slurm_make_time_str ((time_t *)&info_ptr->last_update, time_str,
		sizeof(time_str));
	fprintf( out, "Bluegene Block data as of %s, record count %d\n",
		time_str, info_ptr->record_count);

	for (i = 0; i < info_ptr->record_count; i++)
		slurm_print_block_info(out, & block_ptr[i], one_liner);
}

/*
 * slurm_print_block_info - output information about a specific Bluegene
 *	block based upon message as loaded using slurm_load_block
 * IN out - file to write to
 * IN block_ptr - an individual block information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_block_info(
	FILE *out, block_info_t *block_ptr, int one_liner)
{
	char *print_this = slurm_sprint_block_info(
		block_ptr, one_liner);
	fprintf(out, "%s", print_this);
	xfree(print_this);
}


/*
 * slurm_sprint_block_info - output information about a specific Bluegene
 *	block based upon message as loaded using slurm_load_block
 * IN block_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_block_info(
	block_info_t * block_ptr, int one_liner)
{
	int j;
	char tmp1[16], tmp2[16], *tmp_char = NULL;
	char *out = NULL;
	char *line_end = "\n   ";
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (one_liner)
		line_end = " ";

	/****** Line 1 ******/
	convert_num_unit((float)block_ptr->cnode_cnt, tmp1, sizeof(tmp1),
			 UNIT_NONE);
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		convert_num_unit((float)block_ptr->cnode_err_cnt, tmp2,
				 sizeof(tmp2), UNIT_NONE);
		tmp_char = xstrdup_printf("%s/%s", tmp1, tmp2);
	} else
		tmp_char = tmp1;

	out = xstrdup_printf("BlockName=%s TotalNodes=%s State=%s%s",
			     block_ptr->bg_block_id, tmp_char,
			     bg_block_state_string(block_ptr->state),
			     line_end);
	if (cluster_flags & CLUSTER_FLAG_BGQ)
		xfree(tmp_char);
	/****** Line 2 ******/
	j = 0;
	if (block_ptr->job_list)
		j = list_count(block_ptr->job_list);

	if (!j)
		xstrcat(out, "JobRunning=NONE ");
	else if (j == 1) {
		block_job_info_t *block_job = list_peek(block_ptr->job_list);
		xstrfmtcat(out, "JobRunning=%u ", block_job->job_id);
	} else
		xstrcat(out, "JobRunning=Multiple ");

	tmp_char = conn_type_string_full(block_ptr->conn_type);
	xstrfmtcat(out, "ConnType=%s", tmp_char);
	xfree(tmp_char);
	if (cluster_flags & CLUSTER_FLAG_BGL)
		xstrfmtcat(out, " NodeUse=%s",
			   node_use_string(block_ptr->node_use));

	xstrcat(out, line_end);

	/****** Line 3 ******/
	if (block_ptr->ionode_str)
		xstrfmtcat(out, "MidPlanes=%s[%s] MPIndices=",
			   block_ptr->mp_str, block_ptr->ionode_str);
	else
		xstrfmtcat(out, "MidPlanes=%s MPIndices=",
			   block_ptr->mp_str);
	for (j = 0;
	     (block_ptr->mp_inx && (block_ptr->mp_inx[j] != -1));
	     j+=2) {
		if (j > 0)
			xstrcat(out, ",");
		xstrfmtcat(out, "%d-%d", block_ptr->mp_inx[j],
			   block_ptr->mp_inx[j+1]);
	}
	xstrcat(out, line_end);

	/****** Line 4 ******/
	xstrfmtcat(out, "MloaderImage=%s%s",
		   block_ptr->mloaderimage, line_end);

	if (cluster_flags & CLUSTER_FLAG_BGL) {
		/****** Line 5 ******/
		xstrfmtcat(out, "BlrtsImage=%s%s", block_ptr->blrtsimage,
			   line_end);
		/****** Line 6 ******/
		xstrfmtcat(out, "LinuxImage=%s%s", block_ptr->linuximage,
			   line_end);
		/****** Line 7 ******/
		xstrfmtcat(out, "RamdiskImage=%s", block_ptr->ramdiskimage);
	} else if (cluster_flags & CLUSTER_FLAG_BGP) {
		/****** Line 5 ******/
		xstrfmtcat(out, "CnloadImage=%s%s", block_ptr->linuximage,
			   line_end);
		/****** Line 6 ******/
		xstrfmtcat(out, "IoloadImage=%s", block_ptr->ramdiskimage);
	}

	if (block_ptr->reason)
		xstrfmtcat(out, "Reason=%s%s",
			   block_ptr->reason, line_end);

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}

/*
 * slurm_load_block_info - issue RPC to get slurm all node select plugin
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN block_info_msg_pptr - place to store a node select configuration
 *	pointer
 * IN show_flags - controls output form or filtering, see SHOW_FLAGS in slurm.h
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_block_info_msg
 */
extern int slurm_load_block_info (time_t update_time,
				  block_info_msg_t **block_info_msg_pptr,
				  uint16_t show_flags)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
	block_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

        req.last_update  = update_time;
	req.show_flags   = show_flags;
        req_msg.msg_type = REQUEST_BLOCK_INFO;
        req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_BLOCK_INFO:
		*block_info_msg_pptr = (block_info_msg_t *)
			resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*block_info_msg_pptr = NULL;
		break;
	default:
		*block_info_msg_pptr = NULL;
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

        return SLURM_SUCCESS;
}

extern int slurm_get_select_jobinfo(dynamic_plugin_data_t *jobinfo,
				    enum select_jobdata_type data_type,
				    void *data)
{
	return select_g_select_jobinfo_get(jobinfo, data_type, data);
}

extern int slurm_get_select_nodeinfo(dynamic_plugin_data_t *nodeinfo,
				     enum select_nodedata_type data_type,
				     enum node_states state, void *data)
{
	return select_g_select_nodeinfo_get(nodeinfo, data_type, state, data);
}
