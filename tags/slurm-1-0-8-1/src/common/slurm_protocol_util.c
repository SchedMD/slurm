/*****************************************************************************\
 *  slurm_protocol_util.c - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"

/* 
 * check_header_version checks to see that the specified header was sent 
 * from a node running the same version of the protocol as the current node 
 * IN header - the message header received
 * RET - SLURM error code
 */
int check_header_version(header_t * header)
{
	if (header->version != SLURM_PROTOCOL_VERSION) {
		debug("Invalid Protocol Version %d", header->version);
		slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
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
void init_header(header_t * header, slurm_msg_type_t msg_type,
		 uint16_t flags)
{
	header->version = SLURM_PROTOCOL_VERSION;
	header->flags = flags;
	header->msg_type = msg_type;
	header->body_length = 0;	/* over-written later */
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
void slurm_print_launch_task_msg(launch_tasks_request_msg_t * msg)
{
	int i;

	debug3("job_id: %i", msg->job_id);
	debug3("job_step_id: %i", msg->job_step_id);
	debug3("uid: %i", msg->uid);
	debug3("gid: %i", msg->gid);
	debug3("tasks_to_launch: %i", msg->tasks_to_launch);
	debug3("envc: %i", msg->envc);
	for (i = 0; i < msg->envc; i++) {
		debug3("env[%i]: %s", i, msg->env[i]);
	}
	debug3("cwd: %s", msg->cwd);
	debug3("argc: %i", msg->argc);
	for (i = 0; i < msg->argc; i++) {
		debug3("argv[%i]: %s", i, msg->argv[i]);
	}
	debug3("msg -> resp_port  = %d", msg->resp_port);
	debug3("msg -> io_port    = %d", msg->io_port);
	debug3("msg -> task_flags = %x", msg->task_flags);

	for (i = 0; i < msg->tasks_to_launch; i++) {
		debug3("global_task_id[%i]: %i ", i,
		       msg->global_task_ids[i]);
	}
}
