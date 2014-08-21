/*****************************************************************************\
 *  front_end_info.c - get/print the state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_print_front_end_info_msg - output information about all Slurm
 *	front_ends based upon message as loaded using slurm_load_front_end
 * IN out - file to write to
 * IN front_end_info_msg_ptr - front_end information message pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_front_end_info_msg (FILE * out,
				front_end_info_msg_t * front_end_info_msg_ptr,
				int one_liner)
{
	int i;
	front_end_info_t *front_end_ptr;
	char time_str[32];

	front_end_ptr = front_end_info_msg_ptr->front_end_array;
	slurm_make_time_str((time_t *)&front_end_info_msg_ptr->last_update,
			    time_str, sizeof(time_str));
	fprintf(out, "front_end data as of %s, record count %d\n",
		time_str, front_end_info_msg_ptr->record_count);

	for (i = 0; i < front_end_info_msg_ptr-> record_count; i++) {
		slurm_print_front_end_table(out, &front_end_ptr[i],
					    one_liner ) ;
	}
}


/*
 * slurm_print_front_end_table - output information about a specific Slurm
 *	front_ends based upon message as loaded using slurm_load_front_end
 * IN out - file to write to
 * IN front_end_ptr - an individual front_end information record pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_front_end_table (FILE * out, front_end_info_t * front_end_ptr,
			     int one_liner)
{
	char *print_this = slurm_sprint_front_end_table(front_end_ptr,
							one_liner);
	fprintf(out, "%s", print_this);
	xfree(print_this);
}

/*
 * slurm_sprint_front_end_table - output information about a specific Slurm
 *	front_end based upon message as loaded using slurm_load_front_end
 * IN front_end_ptr - an individual front_end information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *
slurm_sprint_front_end_table (front_end_info_t * front_end_ptr,
			      int one_liner)
{
	uint32_t my_state = front_end_ptr->node_state;
	char *drain_str = "";
	char tmp_line[512], time_str[32];
	char *out = NULL;

	if (my_state & NODE_STATE_DRAIN) {
		my_state &= (~NODE_STATE_DRAIN);
		drain_str = "+DRAIN";
	}

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line), "FrontendName=%s ",
		 front_end_ptr->name);
	xstrcat(out, tmp_line);
	snprintf(tmp_line, sizeof(tmp_line), "State=%s%s ",
		 node_state_string(my_state), drain_str);
	xstrcat(out, tmp_line);
	snprintf(tmp_line, sizeof(tmp_line), "Version=%s ",
		 front_end_ptr->version);
	xstrcat(out, tmp_line);
	if (front_end_ptr->reason_time) {
		char *user_name = uid_to_string(front_end_ptr->reason_uid);
		slurm_make_time_str((time_t *)&front_end_ptr->reason_time,
				    time_str, sizeof(time_str));
		snprintf(tmp_line, sizeof(tmp_line), "Reason=%s [%s@%s]",
			 front_end_ptr->reason, user_name, time_str);
		xstrcat(out, tmp_line);
		xfree(user_name);
	} else {
		snprintf(tmp_line, sizeof(tmp_line), "Reason=%s",
			 front_end_ptr->reason);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	slurm_make_time_str((time_t *)&front_end_ptr->boot_time,
			    time_str, sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "BootTime=%s ", time_str);
	xstrcat(out, tmp_line);
	slurm_make_time_str((time_t *)&front_end_ptr->slurmd_start_time,
			    time_str, sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "SlurmdStartTime=%s", time_str);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 (optional) ******/
	if (front_end_ptr->allow_groups || front_end_ptr->allow_users ||
	    front_end_ptr->deny_groups  || front_end_ptr->deny_users) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		if (front_end_ptr->allow_groups) {
			xstrfmtcat(out, "AllowGroups=%s ",
				   front_end_ptr->allow_groups);
		}
		if (front_end_ptr->allow_users) {
			xstrfmtcat(out, "AllowUsers=%s ",
				   front_end_ptr->allow_users);
		}
		if (front_end_ptr->deny_groups) {
			xstrfmtcat(out, "DenyGroups=%s ",
				   front_end_ptr->deny_groups);
		}
		if (front_end_ptr->deny_users) {
			xstrfmtcat(out, "DenyUsers=%s ",
				   front_end_ptr->deny_users);
		}
	}

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}


/*
 * slurm_load_front_end - issue RPC to get slurm all front_end configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN front_end_info_msg_pptr - place to store a front_end configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_front_end_info_msg
 */
int
slurm_load_front_end (time_t update_time, front_end_info_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	front_end_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req.last_update  = update_time;
	req_msg.msg_type = REQUEST_FRONT_END_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_FRONT_END_INFO:
		*resp = (front_end_info_msg_t *) resp_msg.data;
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
