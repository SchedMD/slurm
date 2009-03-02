/*****************************************************************************\
 *  slurm_protocol_util.c - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/slurmdbd/read_config.h"

/* 
 * check_header_version checks to see that the specified header was sent 
 * from a node running the same version of the protocol as the current node 
 * IN header - the message header received
 * RET - SLURM error code
 */
int check_header_version(header_t * header)
{
	if(slurmdbd_conf) {
		if (header->version != SLURM_PROTOCOL_VERSION
		    && header->version != SLURM_1_3_PROTOCOL_VERSION)
			slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
	} else if (header->version != SLURM_PROTOCOL_VERSION)
		slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * init_header - simple function to create a header, always insuring that 
 * an accurate version string is inserted
 * OUT header - the message header to be send
 * IN msg_type - type of message to be send
 * IN flags - message flags to be send
 */
void init_header(header_t *header, slurm_msg_t *msg,
		 uint16_t flags)
{
	memset(header, 0, sizeof(header));
	/* Since the slurmdbd could talk to a host of different
	   versions of slurm this needs to be kept current when the
	   protocol version changes. */
	if(msg->msg_type == ACCOUNTING_UPDATE_MSG
	   || msg->msg_type == ACCOUNTING_FIRST_REG) {
		uint32_t rpc_version =
			((accounting_update_msg_t *)msg->data)->rpc_version;
		if(rpc_version < 5)
			header->version = SLURM_1_3_PROTOCOL_VERSION;
		else if(rpc_version >= 5)
			header->version = SLURM_PROTOCOL_VERSION;
	} else 
		header->version = SLURM_PROTOCOL_VERSION;
	
	header->flags = flags;
	header->msg_type = msg->msg_type;
	header->body_length = 0;	/* over-written later */
	header->forward = msg->forward;
	if(msg->ret_list)
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
	debug3("tasks_to_launch: %u", msg->tasks_to_launch);
	debug3("envc: %u", msg->envc);
	for (i = 0; i < msg->envc; i++) {
		debug3("env[%d]: %s", i, msg->env[i]);
	}
	debug3("cwd: %s", msg->cwd);
	debug3("argc: %u", msg->argc);
	for (i = 0; i < msg->argc; i++) {
		debug3("argv[%d]: %s", i, msg->argv[i]);
	}
	debug3("msg -> resp_port  = %u", msg->resp_port);
	debug3("msg -> io_port    = %u", msg->io_port);
	debug3("msg -> task_flags = %x", msg->task_flags);

	for (i = 0; i < msg->tasks_to_launch[node_id]; i++) {
		debug3("global_task_id[%d]: %u ", i,
		       msg->global_task_ids[node_id][i]);
	}
}
