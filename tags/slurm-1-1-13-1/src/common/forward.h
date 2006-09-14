/*****************************************************************************\
 *  forward.h - get/print the job state information of slurm
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov>
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
#include "src/common/dist_tasks.h"

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
 * forward_msg_to_next- logic to change the address and forward structure of a
 *                      message to the next one in the queue and mark the  
 *                      current node as having an error adding it to the return
 *                      list of the fwd_msg.
 *
 * IN: fwd_msg        - forward_msg_t *      - holds information about message
 *                                             and the childern it was suppose 
 *                                             to forward to
 * IN: err            - int                  - error message from attempt
 *
 * RET: 0/1           - int                  - if 1 more to forward to 0 if 
 *                                             no one left to forward to.
 * you need to slurm_mutex_lock(fwd_msg->forward_mutex);
 * before coming in here
 */
extern int forward_msg_to_next(forward_msg_t *fwd_msg, int err);

/*
 * forward_set     - divide a mesage up into components for forwarding
 * IN: forward     - forward_t *   - struct to store forward info
 * IN: span        - int           - count of forwards to do
 * IN/OUT: pos     - int *         - position in the original messages  
 *                                   structures
 * IN: from        - forward_t *   - information from original message
 * RET: SLURM_SUCCESS - int
 *
 * NOTE: Call destroy_forward() to release allocated memory
 */
/********************************************************************
// Code taken from slurmctld/agent.c
// This function should be used to set up the forward structure in 
// a message that could be forwarded.

// Set the span with total count of hosts to send to
int *span = set_span(agent_arg_ptr->node_count, 0);

// Fill in a local forward structure with count of threads to created
// by this program, an array of names and addrs of hosts and node_id 
// (if any) to be sent to along with the timeout of the message
forward.cnt  = agent_arg_ptr->node_count;
forward.name = agent_arg_ptr->node_names;
forward.addr = agent_arg_ptr->slurm_addr;
forward.node_id = NULL;
forward.timeout = SLURM_MESSAGE_TIMEOUT_MSEC_STATIC;

thr_count = 0;
for (i = 0; i < agent_arg_ptr->node_count; i++) {
	thread_ptr[thr_count].state      = DSH_NEW;
	thread_ptr[thr_count].slurm_addr = agent_arg_ptr->slurm_addr[i];
	strncpy(thread_ptr[thr_count].node_name,
		&agent_arg_ptr->node_names[i * MAX_SLURM_NAME],
		MAX_SLURM_NAME);
// For each 'main' thread we want to add hosts for this one to forward to.
// Send the thread_ptr's forward, span at the thr_count, the address of 
// position we are in the count, and the forward we set up earlier
	forward_set(&thread_ptr[thr_count].forward,
		    span[thr_count],
		    &i,
		    &forward);

	thr_count++;		       
}

// Free the span
xfree(span);
// Set the new thread_count to the number with the forwards taken out 
// of the count since we don't keep track of those on the master sender
agent_info_ptr->thread_count = thr_count;	
********************************************************************/
extern int forward_set (forward_t *forward, 
			int span,
			int *pos,
			forward_t *from);

/*
 * forward_set_launch - add to the message possible forwards to go to during 
 *                      a job launch
 * IN: forward     - forward_t *           - struct to store forward info
 * IN: span        - int                   - count of forwards to do
 * IN: step_layout - slurm_step_layout_t * - contains information about hosts
 *                                           from original message
 * IN: slurmd_addr - slurm_addr *          - addrs of hosts to send messages to
 * IN: itr         - hostlist_iterator_t   - count into host list of hosts to 
 *                                           send messages to 
 * IN: timeout     - int32_t               - timeout if any to wait for 
 *                                           message responses
 * RET: SLURM_SUCCESS - int
 */

/********************************************************************
Code taken from srun/launch.c
This function should be used sending a launch message that could be forwarded.

//set the span with total count of hosts to send to
int *span = set_span(job->step_layout->num_hosts, 0);
	
//set up hostlist off the nodelist of the job
hostlist = hostlist_create(job->nodelist); 		
itr = hostlist_iterator_create(hostlist);
job->thr_count = 0;
for (i = 0; i < job->step_layout->num_hosts; i++) {
	slurm_msg_t                *m = &msg_array_ptr[job->thr_count];
	
	m->srun_node_id    = (uint32_t)i;			
	m->msg_type        = REQUEST_LAUNCH_TASKS;
	m->data            = &r;
	m->ret_list = NULL;
// set orig_add.sin_addr.s_addr to 0 meaning there is no one 
// forwarded this message to this node
	m->orig_addr.sin_addr.s_addr = 0;
	m->buffer = buffer;

	j=0; 
	while(host = hostlist_next(itr)) { 
		if(!strcmp(host,job->step_layout->host[i])) {
       		       free(host);
		       break; 
		}
  	       	j++; 
	       	free(host);
        }
	hostlist_iterator_reset(itr);
	memcpy(&m->address, 
	       &job->slurmd_addr[j], 
	       sizeof(slurm_addr));
	
// send the messages forward struct to be filled in with the information from
// the other variables
	forward_set_launch(&m->forward,
			   span[job->thr_count],
			   &i,
			   job->step_layout,
			   job->slurmd_addr,
			   itr,
			   opt.msg_timeout);
//increment the count of threads created		
	job->thr_count++;
}
//free the span and destroy the hostlist we created
xfree(span);
hostlist_iterator_destroy(itr);
hostlist_destroy(hostlist);
********************************************************************/
extern int forward_set_launch (forward_t *forward, 
			       int span,
			       int *pos,
			       slurm_step_layout_t *step_layout,
			       slurm_addr *slurmd_addr,
			       hostlist_iterator_t itr,
			       int32_t timeout);


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
//This function should only be used after a message is recieved.

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
extern int no_resp_forwards(forward_t *forward, List *ret_list, int err);

/* destroyers */
extern void destroy_data_info(void *object);
extern void destroy_forward(forward_t *forward);
extern void destroy_forward_struct(forward_struct_t *forward_struct);
extern void destroy_ret_types(void *object);
	
#endif
