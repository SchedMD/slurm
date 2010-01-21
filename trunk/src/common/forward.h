/*****************************************************************************\
 *  forward.h - get/print the job state information of slurm
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov>
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

#ifndef _FORWARD_H
#define _FORWARD_H

#include <stdint.h>
#include "src/common/slurm_protocol_api.h"

/*
 * forward_init    - initilize forward structure
 * IN: forward     - forward_t *   - struct to store forward info
 * IN: from        - forward_t *   - (OPTIONAL) can be NULL, can be used to
 *                                   init the forward to this state
 * RET: VOID
 */
extern void forward_init(forward_t *forward, forward_t *from);

/*
 * forward_msg	      - logic to forward a message which has been received and
 *			accumulate the return codes from processes getting the
 *			the forwarded message
 *
 * IN: forward_struct - forward_struct_t *   - holds information about message
 *                                             that needs to be forwarded to
 *      				       childern processes
 * IN: header         - header_t             - header from message that came in
 *                                             needing to be forwarded.
 * RET: SLURM_SUCCESS - int
 */
/*********************************************************************
// Code taken from common/slurm_protocol_api.c
// Set up the forward_struct using the remainder of the buffer being received,
// right after header has been removed form the original buffer

forward_struct = xmalloc(sizeof(forward_struct_t));
forward_struct->buf_len = remaining_buf(buffer);
forward_struct->buf = xmalloc(sizeof(char) * forward_struct->buf_len);
memcpy(forward_struct->buf, &buffer->head[buffer->processed],
       forward_struct->buf_len);
forward_struct->ret_list = ret_list;

forward_struct->timeout = timeout - header.forward.timeout;

// Send the structure created off the buffer and the header from the message
if (forward_msg(forward_struct, &header) == SLURM_ERROR) {
       error("problem with forward msg");
}

*********************************************************************/
extern int forward_msg(forward_struct_t *forward_struct,
		       header_t *header);


/*
 * start_msg_tree  - logic to begin the forward tree and
 *                   accumulate the return codes from processes getting the
 *                   the forwarded message
 *
 * IN: hl          - hostlist_t   - list of every node to send message to
 * IN: msg         - slurm_msg_t  - message to send.
 * IN: timeout     - int          - how long to wait in milliseconds.
 * RET List 	   - List containing the responses of the childern
 *		     (if any) we forwarded the message to. List
 *		     containing type (ret_data_info_t).
 */
extern List start_msg_tree(hostlist_t hl, slurm_msg_t *msg, int timeout);

/*
 * mark_as_failed_forward- mark a node as failed and add it to "ret_list"
 *
 * IN: ret_list       - List *   - ret_list to put ret_data_info
 * IN: node_name      - char *   - node name that failed
 * IN: err            - int      - error message from attempt
 *
 */
extern void mark_as_failed_forward(List *ret_list, char *node_name, int err);

extern void forward_wait(slurm_msg_t *msg);

/*
 * no_resp_forward - Used to respond for nodes not able to respond since
 *                   the parent had failed in some way
 * IN: forward     - forward_t *   -
 * IN: ret_list    - List *        -
 * IN: err         - int           - type of error from parent
 * RET: SLURM_SUCCESS - int
 */
/*********************************************************************
Code taken from common/slurm_protocol_api.c
//This function should only be used after a message is received.

// a call to slurm_receive_msg will fill in a ret_list
	ret_list = slurm_receive_msg(fd, resp, timeout);
}

// if ret_list is null or list_count is 0 means there may have been an error
// this fuction will check to make sure if there were supposed to be forwards
// we handle the return code for the messages
if(!ret_list || list_count(ret_list) == 0) {
	no_resp_forwards(&req->forward, &ret_list, errno);
}
**********************************************************************/
/* extern int no_resp_forwards(forward_t *forward, List *ret_list, int err); */

/* destroyers */
extern void destroy_data_info(void *object);
extern void destroy_forward(forward_t *forward);
extern void destroy_forward_struct(forward_struct_t *forward_struct);

#endif
