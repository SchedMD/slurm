/*****************************************************************************\
 *  slurm_protocol_util.c - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/slurmdbd/read_config.h"

uint16_t _get_slurm_version(uint32_t rpc_version)
{
	uint16_t version;

	if (rpc_version >= 11)
		version = SLURM_PROTOCOL_VERSION;
	else if (rpc_version >= 10)
		version = SLURM_2_4_PROTOCOL_VERSION;
	else if (rpc_version >= 9)
		version = SLURM_2_3_PROTOCOL_VERSION;
	else if (rpc_version >= 8)
		version = SLURM_2_2_PROTOCOL_VERSION;
	else if (rpc_version >= 6)
		version = SLURM_2_1_PROTOCOL_VERSION;
	else if (rpc_version >= 5)
		version = SLURM_2_0_PROTOCOL_VERSION;
	else
		version = SLURM_1_3_PROTOCOL_VERSION;

	return version;
}

/*
 * check_header_version checks to see that the specified header was sent
 * from a node running the same version of the protocol as the current node
 * IN header - the message header received
 * RET - SLURM error code
 */
int check_header_version(header_t * header)
{
	uint16_t check_version = SLURM_PROTOCOL_VERSION;

	if (working_cluster_rec)
		check_version = _get_slurm_version(
			working_cluster_rec->rpc_version);

	if (slurmdbd_conf) {
		if ((header->version != SLURM_PROTOCOL_VERSION)     &&
		    (header->version != SLURM_2_4_PROTOCOL_VERSION) &&
		    (header->version != SLURM_2_3_PROTOCOL_VERSION) &&
		    (header->version != SLURM_2_2_PROTOCOL_VERSION) &&
		    (header->version != SLURM_2_1_PROTOCOL_VERSION))
			slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
	} else if (header->version != check_version) {
		/* Starting with 2.2 we will handle previous versions
		 * of SLURM for some calls */
		switch(header->msg_type) {
		case REQUEST_BLOCK_INFO:
		case REQUEST_BUILD_INFO:
		case REQUEST_CANCEL_JOB_STEP:
		case REQUEST_CHECKPOINT:
		case REQUEST_CHECKPOINT_COMP:
		case REQUEST_CHECKPOINT_TASK_COMP:
		case REQUEST_COMPLETE_BATCH_SCRIPT:	/* From slurmstepd */
		case REQUEST_COMPLETE_JOB_ALLOCATION:
		case REQUEST_CREATE_PARTITION:
		case REQUEST_CREATE_RESERVATION:
		case REQUEST_DELETE_PARTITION:
		case REQUEST_DELETE_RESERVATION:
		case REQUEST_FRONT_END_INFO:
		case REQUEST_JOB_ALLOCATION_INFO:
		case REQUEST_JOB_ALLOCATION_INFO_LITE:
		case REQUEST_JOB_END_TIME:
		case REQUEST_JOB_INFO:
		case REQUEST_JOB_INFO_SINGLE:
		case REQUEST_JOB_NOTIFY:
		case REQUEST_JOB_READY:
		case REQUEST_JOB_REQUEUE:
		case REQUEST_JOB_STEP_INFO:
		case REQUEST_JOB_WILL_RUN:
		case REQUEST_NODE_INFO:
		case REQUEST_PARTITION_INFO:
		case REQUEST_PING:
		case REQUEST_PRIORITY_FACTORS:
		case REQUEST_REBOOT_NODES:
		case REQUEST_RECONFIGURE:
		case REQUEST_RESERVATION_INFO:
		case REQUEST_SET_DEBUG_FLAGS:
		case REQUEST_SET_DEBUG_LEVEL:
		case REQUEST_SET_SCHEDLOG_LEVEL:
		case REQUEST_SHARE_INFO:
		case REQUEST_SHUTDOWN:
		case REQUEST_SHUTDOWN_IMMEDIATE:
		case REQUEST_SPANK_ENVIRONMENT:
		case REQUEST_STEP_COMPLETE:		/* From slurmstepd */
		case REQUEST_STEP_LAYOUT:
		case REQUEST_SUBMIT_BATCH_JOB:
		case REQUEST_SUSPEND:
		case REQUEST_TERMINATE_JOB:
		case REQUEST_TERMINATE_TASKS:
		case REQUEST_TOPO_INFO:
		case REQUEST_TRIGGER_CLEAR:
		case REQUEST_TRIGGER_GET:
		case REQUEST_TRIGGER_PULL:
		case REQUEST_TRIGGER_SET:
		case REQUEST_UPDATE_BLOCK:
		case REQUEST_UPDATE_FRONT_END:
		case REQUEST_UPDATE_JOB:
		case REQUEST_UPDATE_JOB_STEP:
		case REQUEST_UPDATE_NODE:
		case REQUEST_UPDATE_PARTITION:
		case REQUEST_UPDATE_RESERVATION:
			if ((header->version == SLURM_2_4_PROTOCOL_VERSION) ||
			    (header->version == SLURM_2_3_PROTOCOL_VERSION) ||
			    (header->version == SLURM_2_2_PROTOCOL_VERSION) ||
			    (header->version == SLURM_2_1_PROTOCOL_VERSION))
				break;
		default:
			debug("unsupported RPC %d", header->msg_type);
			slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
			break;
		}
	}
	return SLURM_PROTOCOL_SUCCESS;
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
	memset(header, 0, sizeof(header));
	/* Since the slurmdbd could talk to a host of different
	   versions of slurm this needs to be kept current when the
	   protocol version changes. */
	if (msg->protocol_version != (uint16_t)NO_VAL)
		header->version = msg->protocol_version;
	else if (working_cluster_rec)
		header->version = _get_slurm_version(
			working_cluster_rec->rpc_version);
	else if ((msg->msg_type == ACCOUNTING_UPDATE_MSG) ||
	         (msg->msg_type == ACCOUNTING_FIRST_REG)) {
		uint32_t rpc_version =
			((accounting_update_msg_t *)msg->data)->rpc_version;
		header->version = _get_slurm_version(rpc_version);
	} else
		header->version = SLURM_PROTOCOL_VERSION;

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


/* log the supplied slurm task launch message as debug3() level */
void slurm_print_launch_task_msg(launch_tasks_request_msg_t *msg, char *name)
{
	int i;
	int node_id = nodelist_find(msg->complete_nodelist, name);

	debug3("job_id: %u", msg->job_id);
	debug3("job_step_id: %u", msg->job_step_id);
	debug3("uid: %u", msg->uid);
	debug3("gid: %u", msg->gid);
	debug3("tasks_to_launch: %u", *(msg->tasks_to_launch));
	debug3("envc: %u", msg->envc);
	for (i = 0; i < msg->envc; i++) {
		debug3("env[%d]: %s", i, msg->env[i]);
	}
	debug3("cwd: %s", msg->cwd);
	debug3("argc: %u", msg->argc);
	for (i = 0; i < msg->argc; i++) {
		debug3("argv[%d]: %s", i, msg->argv[i]);
	}
	debug3("msg -> resp_port  = %u", *(msg->resp_port));
	debug3("msg -> io_port    = %u", *(msg->io_port));
	debug3("msg -> task_flags = %x", msg->task_flags);

	for (i = 0; i < msg->tasks_to_launch[node_id]; i++) {
		debug3("global_task_id[%d]: %u ", i,
		       msg->global_task_ids[node_id][i]);
	}
}
