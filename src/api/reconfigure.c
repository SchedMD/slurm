/*****************************************************************************\
 *  reconfigure.c - request that slurmctld shutdown or re-read the
 *	            configuration files
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/forward.h"
#include "src/common/xmalloc.h"

static int _send_message_controller(int dest, slurm_msg_t *req);

/*
 * slurm_reconfigure - issue RPC to have Slurm controller (slurmctld)
 *	reload its configuration file
 * RET 0 or a slurm error code
 */
int
slurm_reconfigure (void)
{
	int rc;
	slurm_msg_t req;

	slurm_msg_t_init(&req);

	req.msg_type = REQUEST_RECONFIGURE;

	if (slurm_send_recv_controller_rc_msg(&req, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_ping - issue RPC to have Slurm controller (slurmctld)
 * IN dest - controller to contact (0=primary, 1=backup, 2=backup2, etc.)
 * RET 0 or a slurm error code
 */
extern int slurm_ping(int dest)
{
	int rc ;
	slurm_msg_t request_msg ;

	slurm_msg_t_init(&request_msg);
	request_msg.msg_type = REQUEST_PING ;
	rc = _send_message_controller(dest, &request_msg);

	return rc;
}

/*
 * slurm_shutdown - issue RPC to have Slurm controller (slurmctld)
 *	cease operations, both the primary and all backup controllers
 *	are shutdown.
 * IN options - 0: all slurm daemons are shutdown
 *              1: slurmctld generates a core file
 *              2: only the slurmctld is shutdown (no core file)
 * RET 0 or a slurm error code
 */
extern int slurm_shutdown(uint16_t options)
{
	slurm_msg_t req_msg;
	shutdown_msg_t shutdown_msg;
	int i;

	slurm_msg_t_init(&req_msg);
	shutdown_msg.options = options;
	req_msg.msg_type     = REQUEST_SHUTDOWN;
	req_msg.data         = &shutdown_msg;

	/*
	 * Explicity send the message to both primary and backup controllers
	 */
	if (!working_cluster_rec) {
		uint32_t control_cnt = slurm_get_control_cnt();
		for (i = 1; i < control_cnt; i++)
			(void) _send_message_controller(i, &req_msg);
	}
	return _send_message_controller(0, &req_msg);
}

/*
 * slurm_takeover - issue RPC to have a Slurm backup controller take over the
 *                  primary controller. REQUEST_CONTROL is sent by the backup
 *                  to the primary controller to take control
 * backup_inx IN - Index of BackupController to assume controller (typically 1)
 * RET 0 or a slurm error code
 */
extern int slurm_takeover(int backup_inx)
{
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type     = REQUEST_TAKEOVER;
	if (backup_inx < 1)
		return SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR;
	return _send_message_controller(backup_inx, &req_msg);
}

static int _send_message_controller(int dest, slurm_msg_t *req)
{
	int rc = SLURM_PROTOCOL_SUCCESS;
	int fd = -1;
	slurm_msg_t resp_msg;

	/*
	 * always communicate with a single node (primary or some backup per
	 * value of "dest")
	 */
	if ((fd = slurm_open_controller_conn_spec(dest,
						  working_cluster_rec)) < 0) {
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
	}

	if (slurm_send_node_msg(fd, req) < 0) {
		slurm_shutdown_msg_conn(fd);
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_SEND_ERROR);
	}
	slurm_msg_t_init(&resp_msg);

	if ((rc = slurm_receive_msg(fd, &resp_msg, 0)) != 0) {
		slurm_free_msg_members(&resp_msg);
		slurm_shutdown_msg_conn(fd);
		return SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR;
	}

	if (slurm_shutdown_msg_conn(fd) != SLURM_SUCCESS)
		rc = SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR;
	else if (resp_msg.msg_type != RESPONSE_SLURM_RC)
		rc = SLURM_UNEXPECTED_MSG_ERROR;
	else
		rc = slurm_get_return_code(resp_msg.msg_type,
					   resp_msg.data);

	slurm_free_msg_members(&resp_msg);

	if (rc)
		slurm_seterrno_ret(rc);
        return rc;
}

/*
 * slurm_set_debugflags - issue RPC to set slurm controller debug flags
 * IN debug_flags_plus  - debug flags to be added
 * IN debug_flags_minus - debug flags to be removed
 * IN debug_flags_set   - new debug flags value
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int
slurm_set_debugflags (uint64_t debug_flags_plus, uint64_t debug_flags_minus)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	set_debug_flags_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.debug_flags_minus = debug_flags_minus;
	req.debug_flags_plus  = debug_flags_plus;
	req_msg.msg_type = REQUEST_SET_DEBUG_FLAGS;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
        return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_set_debug_level - issue RPC to set slurm controller debug level
 * IN debug_level - requested debug level
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int
slurm_set_debug_level (uint32_t debug_level)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	set_debug_level_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.debug_level  = debug_level;
	req_msg.msg_type = REQUEST_SET_DEBUG_LEVEL;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
        return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_set_schedlog_level - issue RPC to set slurm scheduler log level
 * IN schedlog_level - requested scheduler log level
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int
slurm_set_schedlog_level (uint32_t schedlog_level)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	set_debug_level_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.debug_level  = schedlog_level;
	req_msg.msg_type = REQUEST_SET_SCHEDLOG_LEVEL;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
        return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_set_fs_dampeningfactor - issue RPC to set fs dampening factor
 * IN factor  - requested fs dampening factor
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_set_fs_dampeningfactor (uint16_t factor)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	set_fs_dampening_factor_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.dampening_factor  = factor;
	req_msg.msg_type = REQUEST_SET_FS_DAMPENING_FACTOR;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
        return SLURM_PROTOCOL_SUCCESS;
}
