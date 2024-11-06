/*****************************************************************************\
 *  slurm_protocol_util.c - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/slurmdbd/read_config.h"

/*
 * check_header_version checks to see that the specified header was sent
 * from a node running the same version of the protocol as the current node
 * IN header - the message header received
 * RET - Slurm error code
 */
int check_header_version(header_t * header)
{
	uint16_t check_version = SLURM_PROTOCOL_VERSION;

	if (working_cluster_rec)
		check_version = working_cluster_rec->rpc_version;

	if (slurmdbd_conf) {
		if ((header->version != SLURM_PROTOCOL_VERSION)     &&
		    (header->version != SLURM_ONE_BACK_PROTOCOL_VERSION) &&
		    (header->version != SLURM_MIN_PROTOCOL_VERSION)) {
			debug("unsupported RPC version %hu msg type %s(%u)",
			      header->version, rpc_num2string(header->msg_type),
			      header->msg_type);
			slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
		}
	} else if (header->version != check_version) {
		switch (header->msg_type) {
		case REQUEST_LAUNCH_TASKS:
		case RESPONSE_LAUNCH_TASKS:
			if (working_cluster_rec) {
				/* Disable job step creation/launch
				 * between major releases. Other RPCs
				 * should all be supported. */
				debug("unsupported RPC type %hu",
				      header->msg_type);
				slurm_seterrno_ret(
					SLURM_PROTOCOL_VERSION_ERROR);
				break;
			}
		default:
			if ((header->version != SLURM_PROTOCOL_VERSION)     &&
			    (header->version !=
			     SLURM_ONE_BACK_PROTOCOL_VERSION) &&
			    (header->version != SLURM_MIN_PROTOCOL_VERSION)) {
				debug("Unsupported RPC version %hu "
				      "msg type %s(%u)", header->version,
				      rpc_num2string(header->msg_type),
				      header->msg_type);
				slurm_seterrno_ret(
					SLURM_PROTOCOL_VERSION_ERROR);
			}
			break;

		}
	}

	return SLURM_SUCCESS;
}

/*
 * init_header - simple function to create a header, always insuring that
 * an accurate version string is inserted
 * OUT header - the message header to be send
 * IN msg_type - type of message to be send
 * IN flags - message flags to be send
 */
void init_header(header_t *header, slurm_msg_t *msg, uint16_t flags)
{
	memset(header, 0, sizeof(header_t));
	/* Since the slurmdbd could talk to a host of different
	   versions of slurm this needs to be kept current when the
	   protocol version changes. */
	if (msg->protocol_version != NO_VAL16)
		header->version = msg->protocol_version;
	else if (working_cluster_rec)
		msg->protocol_version = header->version =
			working_cluster_rec->rpc_version;
	else if ((msg->msg_type == ACCOUNTING_UPDATE_MSG) ||
	         (msg->msg_type == ACCOUNTING_FIRST_REG)) {
		uint16_t rpc_version =
			((accounting_update_msg_t *)msg->data)->rpc_version;
		msg->protocol_version = header->version = rpc_version;
	} else
		msg->protocol_version = header->version =
			SLURM_PROTOCOL_VERSION;

	header->flags = flags;
	header->msg_type = msg->msg_type;
	header->body_length = 0;	/* over-written later */
	header->forward = msg->forward;
	if (msg->ret_list)
		header->ret_cnt = list_count(msg->ret_list);
	else
		header->ret_cnt = 0;
	header->ret_list = msg->ret_list;
	header->orig_addr = msg->orig_addr;
}

/*
 * update_header - update a message header with the message len
 * OUT header - the message header to update
 * IN msg_length - length of message to be send
 */
void update_header(header_t * header, uint32_t msg_length)
{
	header->body_length = msg_length;
}

/* Get the port number from a slurm_addr_t */
uint16_t slurm_get_port(slurm_addr_t *addr)
{
	if (addr->ss_family == AF_INET6)
		return ntohs(((struct sockaddr_in6 *) addr)->sin6_port);
	else if (addr->ss_family == AF_INET)
		return ntohs(((struct sockaddr_in *) addr)->sin_port);

	error("%s: Address family '%d' not supported",
	      __func__, addr->ss_family);
	return 0;
}

/* Set the port number in a slurm_addr_t */
void slurm_set_port(slurm_addr_t *addr, uint16_t port)
{
	if (addr->ss_family == AF_INET6)
		((struct sockaddr_in6 *) addr)->sin6_port = htons(port);
	else if (addr->ss_family == AF_INET)
		((struct sockaddr_in *) addr)->sin_port = htons(port);
	else
		error("%s: attempting to set port without address family",
		      __func__);
}

bool slurm_addr_is_unspec(slurm_addr_t *addr)
{
	return (addr->ss_family == AF_UNSPEC);
}
