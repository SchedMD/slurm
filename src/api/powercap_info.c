/*****************************************************************************\
 *  powercap_info.c - Definitions for power capping configuration display
 *****************************************************************************
 *  Copyright (C) 2013 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_load_powercap - issue RPC to get slurm powercapping details 
 * IN powercap_info_msg_pptr - place to store a pointer to the result
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_powercap_info_msg
 */
extern int slurm_load_powercap(powercap_info_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_POWERCAP_INFO;
	req_msg.data     = NULL;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_POWERCAP_INFO:
		*resp = (powercap_info_msg_t *) resp_msg.data;
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

	return SLURM_SUCCESS;
}

/*
 * slurm_print_powercap_info_msg - output information about powercapping
 *	configuration based upon message as loaded using slurm_load_powercap
 * IN out - file to write to
 * IN powercap_info_msg_ptr - powercapping information message pointer
 * IN one_liner - print as a single line if not zero
 */
extern void slurm_print_powercap_info_msg(FILE * out, powercap_info_msg_t *ptr,
					  int one_liner)
{
	char *out_buf = NULL;

	if (ptr->power_cap == 0) {
		/****** Line 1 ******/
		xstrcat(out_buf, "Powercapping disabled by configuration."
			" See PowerParameters in `man slurm.conf'\n");
		fprintf(out, "%s", out_buf);
		xfree(out_buf);
	} else {
		/****** Line 1 ******/
		xstrfmtcat(out_buf, "MinWatts=%u CurrentWatts=%u ",
			   ptr->min_watts, ptr->cur_max_watts);
		if (ptr->power_cap == INFINITE) {
			xstrcat(out_buf, "PowerCap=INFINITE ");
		} else {
			xstrfmtcat(out_buf, "PowerCap=%u ", ptr->power_cap);
		}
		xstrfmtcat(out_buf, "PowerFloor=%u PowerChangeRate=%u",
			   ptr->power_floor, ptr->power_change);
		xstrfmtcat(out_buf, "AdjustedMaxWatts=%u MaxWatts=%u",
			   ptr->adj_max_watts, ptr->max_watts);

		xstrcat(out_buf, "\n");
		fprintf(out, "%s", out_buf);
		xfree(out_buf);
	}
}
