/*****************************************************************************\
 *  topo_info.c - get/print the switch topology state information of slurm
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
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

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_load_topo - issue RPC to get slurm all switch topology configuration
 *	information
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_topo_info_msg
 */
extern int slurm_load_topo(topo_info_response_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_TOPO_INFO;
	req_msg.data     = NULL;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_TOPO_INFO:
		*resp = (topo_info_response_msg_t *) resp_msg.data;
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

/*
 * slurm_print_topo_info_msg - output information about all switch topology
 *	configuration information based upon message as loaded using
 *	slurm_load_topo
 * IN out - file to write to
 * IN topo_info_msg_ptr - switch topology information message pointer
 * IN one_liner - print as a single line if not zero
 */
extern void slurm_print_topo_info_msg(
	FILE * out,
	topo_info_response_msg_t *topo_info_msg_ptr,
	int one_liner)
{
	int i;
	topo_info_t *topo_ptr = topo_info_msg_ptr->topo_array;

	if (topo_info_msg_ptr->record_count == 0) {
		error("No topology information available");
		return;
	}

	for (i = 0; i < topo_info_msg_ptr->record_count; i++)
		slurm_print_topo_record(out, &topo_ptr[i], one_liner);
}



/*
 * slurm_print_topo_record - output information about a specific Slurm topology
 *	record based upon message as loaded using slurm_load_topo
 * IN out - file to write to
 * IN topo_ptr - an individual switch information record pointer
 * IN one_liner - print as a single line if not zero
 * RET out - char * containing formatted output (must be freed after call)
 *	   NULL is returned on failure.
 */
extern void slurm_print_topo_record(FILE * out, topo_info_t *topo_ptr,
				    int one_liner)
{
	char tmp_line[512];
	char *out_buf = NULL;

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		"SwitchName=%s Level=%u LinkSpeed=%u ",
		topo_ptr->name, topo_ptr->level, topo_ptr->link_speed);
	xstrcat(out_buf, tmp_line);

	if (topo_ptr->nodes && topo_ptr->nodes[0]) {
		snprintf(tmp_line, sizeof(tmp_line),
			 "Nodes=%s ", topo_ptr->nodes);
		xstrcat(out_buf, tmp_line);
	}
	if (topo_ptr->switches && topo_ptr->switches[0]) {
		snprintf(tmp_line, sizeof(tmp_line),
			 "Switches=%s ", topo_ptr->switches);
		xstrcat(out_buf, tmp_line);
	}

	xstrcat(out_buf, "\n");
	fprintf(out, "%s", out_buf);
	xfree(out_buf);
}
