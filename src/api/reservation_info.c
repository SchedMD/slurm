/*****************************************************************************\
 *  reservation_info.c - get/print the reservation state information of slurm
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/state_control.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_print_reservation_info_msg - output information about all Slurm
 *	reservations based upon message as loaded using slurm_load_reservation
 * IN out - file to write to
 * IN resv_info_ptr - reservation information message pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_reservation_info_msg ( FILE* out,
		reserve_info_msg_t * resv_info_ptr, int one_liner )
{
	int i ;
	reserve_info_t * resv_ptr = resv_info_ptr->reservation_array ;
	char time_str[32];

	slurm_make_time_str( (time_t *)&resv_info_ptr->last_update, time_str,
			     sizeof(time_str));
	fprintf( out, "Reservation data as of %s, record count %d\n",
		time_str, resv_info_ptr->record_count);

	for (i = 0; i < resv_info_ptr->record_count; i++) {
		slurm_print_reservation_info ( out, & resv_ptr[i], one_liner );
	}

}

/*
 * slurm_print_reservation_info - output information about a specific Slurm
 *	reservation based upon message as loaded using slurm_load_reservation
 * IN out - file to write to
 * IN resv_ptr - an individual reservation information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_reservation_info ( FILE* out, reserve_info_t * resv_ptr,
				    int one_liner )
{
	char *print_this = slurm_sprint_reservation_info(resv_ptr, one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}


/*
 * slurm_sprint_reservation_info - output information about a specific Slurm
 *	reservation based upon message as loaded using slurm_load_reservations
 * IN resv_ptr - an individual reservation information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_reservation_info ( reserve_info_t * resv_ptr,
				      int one_liner )
{
	char tmp1[32], tmp2[32], tmp3[32], *flag_str = NULL;
	char *state="INACTIVE";
	char *out = NULL, *watts_str = NULL;
	uint32_t duration;
	time_t now = time(NULL);
	char *line_end = (one_liner) ? " " : "\n   ";
	int i;

	/****** Line ******/
	slurm_make_time_str(&resv_ptr->start_time, tmp1, sizeof(tmp1));
	slurm_make_time_str(&resv_ptr->end_time,   tmp2, sizeof(tmp2));
	if (resv_ptr->end_time >= resv_ptr->start_time) {
		duration = difftime(resv_ptr->end_time, resv_ptr->start_time);
		secs2time_str(duration, tmp3, sizeof(tmp3));
	} else {
		snprintf(tmp3, sizeof(tmp3), "N/A");
	}
	xstrfmtcat(out,
		 "ReservationName=%s StartTime=%s EndTime=%s Duration=%s",
		 resv_ptr->name, tmp1, tmp2, tmp3);
	xstrcat(out, line_end);

	/****** Line ******/
	flag_str = reservation_flags_string(resv_ptr->flags);

	xstrfmtcat(out, "Nodes=%s NodeCnt=%u CoreCnt=%u Features=%s "
		   "PartitionName=%s Flags=%s",
		   resv_ptr->node_list,
		   (resv_ptr->node_cnt == NO_VAL) ? 0 : resv_ptr->node_cnt,
		   resv_ptr->core_cnt, resv_ptr->features, resv_ptr->partition,
		   flag_str);
	xfree(flag_str);
	xstrcat(out, line_end);

	/****** Line (optional) ******/
	for (i = 0; i < resv_ptr->core_spec_cnt; i++) {
		xstrfmtcat(out,
			 "  NodeName=%s CoreIDs=%s",
			 resv_ptr->core_spec[i].node_name,
			 resv_ptr->core_spec[i].core_id);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	xstrfmtcat(out,
		   "TRES=%s", resv_ptr->tres_str);
	xstrcat(out, line_end);

	/****** Line ******/
	watts_str = state_control_watts_to_str(resv_ptr->resv_watts);
	if ((resv_ptr->start_time <= now) && (resv_ptr->end_time >= now))
		state = "ACTIVE";
	xstrfmtcat(out,
		   "Users=%s Accounts=%s Licenses=%s State=%s BurstBuffer=%s "
		   "Watts=%s", resv_ptr->users, resv_ptr->accounts,
		   resv_ptr->licenses, state, resv_ptr->burst_buffer, watts_str);
	xfree(watts_str);

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}



/*
 * slurm_load_reservations - issue RPC to get all slurm reservation
 *	configuration information if changed since update_time
 * IN update_time - time of current configuration data
 * IN reserve_info_msg_pptr - place to store a reservation configuration
 *	pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_reservation_info_msg
 */
extern int slurm_load_reservations (time_t update_time,
				    reserve_info_msg_t **resp)
{
        int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;
        resv_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

        req.last_update  = update_time;
        req_msg.msg_type = REQUEST_RESERVATION_INFO;
        req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_RESERVATION_INFO:
		*resp = (reserve_info_msg_t *) resp_msg.data;
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
