/*****************************************************************************\
 *  forward.c - forward RPCs through hierarchical slurmd communications
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "src/common/forward.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#define MAX_RETRIES 3
int _destroy_data_info_data(uint32_t type, ret_data_info_t *ret_data_info);


void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = (forward_msg_t *)arg;
	Buf buffer = init_buf(0);
	int i=0;
	List ret_list = NULL;
	ret_types_t *type = NULL;
	ret_types_t *returned_type = NULL;
	slurm_msg_t msg;
	slurm_fd fd;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;
	char name[MAX_SLURM_NAME];

	msg.forward.cnt = 0;
start_again:
	/* info("sending to %s with %d forwards",  */
/* 	     fwd_msg->node_name, fwd_msg->header.forward.cnt); */
	if ((fd = slurm_open_msg_conn(&fwd_msg->addr)) < 0) {
		error("forward_thread to %s: %m", fwd_msg->node_name);
		slurm_mutex_lock(fwd_msg->forward_mutex);
		if(forward_msg_to_next(fwd_msg, errno)) {
			slurm_mutex_unlock(fwd_msg->forward_mutex);
			free_buf(buffer);	
			buffer = init_buf(0);
			goto start_again;
		}
		goto cleanup;
		/* ret_list = list_create(destroy_ret_types); */
/* 		no_resp_forwards(&fwd_msg->header.forward, &ret_list, errno); */
/* 		goto nothing_sent; */
		
	}
	pack_header(&fwd_msg->header, buffer);
	
	/* add forward data to buffer */
	if (remaining_buf(buffer) < fwd_msg->buf_len) {
		buffer->size += (fwd_msg->buf_len + BUF_SIZE);
		xrealloc(buffer->head, buffer->size);
	}
	if (fwd_msg->buf_len) {
		memcpy(&buffer->head[buffer->processed], 
		       fwd_msg->buf, fwd_msg->buf_len);
		buffer->processed += fwd_msg->buf_len;
	}
	
	/*
	 * forward message
	 */
	if(_slurm_msg_sendto(fd, 
			     get_buf_data(buffer), 
			     get_buf_offset(buffer),
			     SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) < 0) {
		error("forward_thread: slurm_msg_sendto: %m");
		slurm_mutex_lock(fwd_msg->forward_mutex);
		if(forward_msg_to_next(fwd_msg, errno)) {
			slurm_mutex_unlock(fwd_msg->forward_mutex);
			free_buf(buffer);	
			buffer = init_buf(0);
			goto start_again;
		}
		goto cleanup;
	/* 	ret_list = list_create(destroy_ret_types); */
/* 		no_resp_forwards(&fwd_msg->header.forward, &ret_list, errno); */
/* 		goto nothing_sent; */
	}

	if ((fwd_msg->header.msg_type == REQUEST_SHUTDOWN) ||
	    (fwd_msg->header.msg_type == REQUEST_RECONFIGURE)) {
		slurm_mutex_lock(fwd_msg->forward_mutex);
		type = xmalloc(sizeof(ret_types_t));
		list_push(fwd_msg->ret_list, type);
		type->ret_data_list = list_create(destroy_data_info);
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		list_push(type->ret_data_list, ret_data_info);
		ret_data_info->node_name = xstrdup(fwd_msg->node_name);
		ret_data_info->nodeid = fwd_msg->header.srun_node_id;
		for(i=0; i<fwd_msg->header.forward.cnt; i++) {
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			list_push(type->ret_data_list, ret_data_info);
			strncpy(name,
				&fwd_msg->header.forward.
				name[i * MAX_SLURM_NAME],
				MAX_SLURM_NAME);
			ret_data_info->node_name = xstrdup(name);
			ret_data_info->nodeid = 
				fwd_msg->header.forward.node_id[i];
		}
		goto cleanup;
	}
	
	ret_list = slurm_receive_msg(fd, &msg, fwd_msg->timeout);

	if(!ret_list || (fwd_msg->header.forward.cnt != 0 
			 && list_count(ret_list) == 0)) {
		slurm_mutex_lock(fwd_msg->forward_mutex);
		if(forward_msg_to_next(fwd_msg, errno)) {
			slurm_mutex_unlock(fwd_msg->forward_mutex);
			free_buf(buffer);	
			buffer = init_buf(0);
			goto start_again;
		}
		goto cleanup;
		//no_resp_forwards(&fwd_msg->header.forward, &ret_list, errno);
	}
//nothing_sent:
	type = xmalloc(sizeof(ret_types_t));
	type->err = errno;
	list_push(ret_list, type);

	type->ret_data_list = list_create(destroy_data_info);
	ret_data_info = xmalloc(sizeof(ret_data_info_t));
	list_push(type->ret_data_list, ret_data_info);
	ret_data_info->node_name = xstrdup(fwd_msg->node_name);
	ret_data_info->nodeid = fwd_msg->header.srun_node_id;
						
	if(type->err != SLURM_SUCCESS) {
		type->type = REQUEST_PING;
		type->msg_rc = SLURM_ERROR;
		ret_data_info->data = NULL;
	} else {
		type->type = msg.msg_type;
		type->msg_rc = ((return_code_msg_t *)msg.data)->return_code;
		ret_data_info->data = msg.data;
		g_slurm_auth_destroy(msg.auth_cred);
	}
	debug3("got reply for %s rc %d", 
	       ret_data_info->node_name, type->msg_rc);
	slurm_mutex_lock(fwd_msg->forward_mutex);
	while((returned_type = list_pop(ret_list)) != NULL) {
		itr = list_iterator_create(fwd_msg->ret_list);	
		while((type = (ret_types_t *) list_next(itr)) != NULL) {
			if(type->msg_rc == returned_type->msg_rc){
				while((ret_data_info = 
				       list_pop(returned_type->
						ret_data_list))) {
					list_push(type->ret_data_list, 
						  ret_data_info);
					/* info("got %s", */
/* 					     ret_data_info->node_name); */
				}
				break;
			}
		}
		list_iterator_destroy(itr);
		
		if(!type) {
			type = xmalloc(sizeof(ret_types_t));
			list_push(fwd_msg->ret_list, type);
			type->type = returned_type->type;
			type->msg_rc = returned_type->msg_rc;
			type->err = returned_type->err;
			type->ret_data_list = list_create(destroy_data_info);
			while((ret_data_info = 
			      list_pop(returned_type->ret_data_list))) {
				list_push(type->ret_data_list, ret_data_info);
				//info("got %s",ret_data_info->node_name);
			}
		}		
		destroy_ret_types(returned_type);
	}
	list_destroy(ret_list);
cleanup:
	if ((fd >= 0) && slurm_close_accepted_conn(fd) < 0)
		error ("close(%d): %m", fd);
	destroy_forward(&fwd_msg->header.forward);
	free_buf(buffer);	
	pthread_cond_signal(fwd_msg->notify);
	slurm_mutex_unlock(fwd_msg->forward_mutex);

	return (NULL);
}

int _destroy_data_info_data(uint32_t type, ret_data_info_t *ret_data_info)
{
	switch(type) {
	case REQUEST_BUILD_INFO:
		slurm_free_last_update_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_INFO:
		slurm_free_job_info_request_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_END_TIME:
		slurm_free_old_job_alloc_msg(ret_data_info->data);
		break;
	case REQUEST_NODE_INFO:
		slurm_free_node_info_request_msg(ret_data_info->data);
		break;
	case REQUEST_PARTITION_INFO:
		slurm_free_part_info_request_msg(ret_data_info->data);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		slurm_free_epilog_complete_msg(ret_data_info->data);
		break;
	case REQUEST_CANCEL_JOB_STEP:
		slurm_free_job_step_kill_msg(ret_data_info->data);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		slurm_free_complete_job_allocation_msg(ret_data_info->data);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		slurm_free_complete_batch_script_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_STEP_CREATE:
		slurm_free_job_step_create_request_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_STEP_INFO:
		slurm_free_job_step_info_request_msg(ret_data_info->data);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_UPDATE_JOB:
		slurm_free_job_desc_msg(ret_data_info->data);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(ret_data_info->data);
		break;
	case REQUEST_OLD_JOB_RESOURCE_ALLOCATION:
		slurm_free_old_job_alloc_msg(ret_data_info->data);
		break;
	case SLURM_SUCCESS:		
	case REQUEST_PING:		
	case REQUEST_RECONFIGURE:
	case REQUEST_CONTROL:
	case REQUEST_SHUTDOWN_IMMEDIATE:
		/* No body to free */
		break;
	case REQUEST_SHUTDOWN:
		slurm_free_shutdown_msg(ret_data_info->data);
		break;
	case REQUEST_UPDATE_NODE:
		slurm_free_update_node_msg(ret_data_info->data);
		break;
	case REQUEST_UPDATE_PARTITION:
		slurm_free_update_part_msg(ret_data_info->data);
		break;
	case REQUEST_DELETE_PARTITION:		
		slurm_free_delete_part_msg(ret_data_info->data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(ret_data_info->data);
		break;
	case REQUEST_CHECKPOINT:
		slurm_free_checkpoint_msg(ret_data_info->data);
		break;
	case REQUEST_CHECKPOINT_COMP:
		slurm_free_checkpoint_comp_msg(ret_data_info->data);
		break;
	case REQUEST_SUSPEND:
		slurm_free_suspend_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_READY:
		slurm_free_job_id_msg(ret_data_info->data);
		break;
	case REQUEST_NODE_SELECT_INFO:
		slurm_free_node_select_msg(ret_data_info->data);
		break;
	case REQUEST_STEP_COMPLETE:
		slurm_free_step_complete_msg(ret_data_info->data);
		break;
	case MESSAGE_STAT_JOBACCT:
		slurm_free_stat_jobacct_msg(ret_data_info->data);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		slurm_free_job_launch_msg(ret_data_info->data);
		break;
	case REQUEST_LAUNCH_TASKS:
		slurm_free_launch_tasks_request_msg(ret_data_info->data);
		break;
	case REQUEST_SPAWN_TASK:
		slurm_free_spawn_task_request_msg(ret_data_info->data);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		slurm_free_kill_tasks_msg(ret_data_info->data);
		break;
	case REQUEST_KILL_TIMELIMIT:
		slurm_free_timelimit_msg(ret_data_info->data);
		break; 
	case REQUEST_REATTACH_TASKS:
		slurm_free_reattach_tasks_request_msg(ret_data_info->data);
		break;
	case REQUEST_SIGNAL_JOB:
		slurm_free_signal_job_msg(ret_data_info->data);
		break;
	case REQUEST_TERMINATE_JOB:
		slurm_free_kill_job_msg(ret_data_info->data);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		slurm_free_update_job_time_msg(ret_data_info->data);
		break;
	case REQUEST_JOB_ID:
		slurm_free_job_id_request_msg(ret_data_info->data);
		break;
	case REQUEST_FILE_BCAST:
		slurm_free_file_bcast_msg(ret_data_info->data);
		break;
	case RESPONSE_SLURM_RC:
		slurm_free_return_code_msg(ret_data_info->data);
		break;
	default:
		error("invalid FORWARD ret_type=%u", type);
		break; 
	}
	return SLURM_SUCCESS;
}

/*
 * forward_init    - initilize forward structure
 * IN: forward     - forward_t *   - struct to store forward info
 * IN: from        - forward_t *   - (OPTIONAL) can be NULL, can be used to
 *                                   init the forward to this state
 * RET: VOID
 */
extern void forward_init(forward_t *forward, forward_t *from)
{
	if(from && from->init == FORWARD_INIT) {
		forward->cnt = from->cnt;
		forward->timeout = from->timeout;
		forward->addr = from->addr;
		forward->name = from->name;
		forward->node_id = from->node_id;
		forward->init = from->init;
	} else {
		forward->cnt = 0;
		forward->timeout = 0;
		forward->addr = NULL;
		forward->name = NULL;
		forward->node_id = NULL;
		forward->init = FORWARD_INIT;
	}
}

/*
 * forward_msg        - logic to forward a message which has been received and
 *                      accumulate the return codes from processes getting the
 *                      the forwarded message
 *
 * IN: forward_struct - forward_struct_t *   - holds information about message
 *                                             that needs to be forwarded to
 *                                             childern processes
 * IN: header         - header_t             - header from message that came in
 *                                             needing to be forwarded.
 * RET: SLURM_SUCCESS - int
 */
extern int forward_msg(forward_struct_t *forward_struct, 
		       header_t *header)
{
	int i;
	int retries = 0;
	forward_msg_t *forward_msg;
	int thr_count = 0;
	int *span = set_span(header->forward.cnt, 0);
	
	slurm_mutex_init(&forward_struct->forward_mutex);
	pthread_cond_init(&forward_struct->notify, NULL);
	
	forward_struct->forward_msg = 
		xmalloc(sizeof(forward_msg_t) * header->forward.cnt);
	
	for(i=0; i<header->forward.cnt; i++) {
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
		    (&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		forward_msg = &forward_struct->forward_msg[i];
		forward_msg->ret_list = forward_struct->ret_list;
		forward_msg->timeout = forward_struct->timeout;
		forward_msg->notify = &forward_struct->notify;
		forward_msg->forward_mutex = &forward_struct->forward_mutex;
		forward_msg->buf_len = forward_struct->buf_len;
		forward_msg->buf = forward_struct->buf;
		
		memcpy(&forward_msg->header.orig_addr, 
		       &header->orig_addr, 
		       sizeof(slurm_addr));
		//forward_msg->header.orig_addr = header->orig_addr;
		forward_msg->header.version = header->version;
		forward_msg->header.flags = header->flags;
		forward_msg->header.msg_type = header->msg_type;
		forward_msg->header.body_length = header->body_length;
		forward_msg->header.srun_node_id = header->forward.node_id[i];
		forward_msg->header.ret_list = NULL;
		forward_msg->header.ret_cnt = 0;
	
		memcpy(&forward_msg->addr, 
		       &header->forward.addr[i], 
		       sizeof(slurm_addr));
		strncpy(forward_msg->node_name,
			&header->forward.name[i * MAX_SLURM_NAME],
			MAX_SLURM_NAME);
       
		forward_set(&forward_msg->header.forward,
			    span[thr_count],
			    &i,
			    &header->forward);
		
		while(pthread_create(&thread_agent, &attr_agent,
				   _forward_thread, 
				   (void *)forward_msg)) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and try again */
		}
		thr_count++; 
	}
	xfree(span);
	return SLURM_SUCCESS;
}

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
extern int forward_msg_to_next(forward_msg_t *fwd_msg, int err) 
{
	ret_data_info_t *ret_data_info = NULL;
	ret_types_t *type = NULL;
	int i = 0;
	int prev_cnt = fwd_msg->header.forward.cnt;
	forward_t forward;
	ListIterator itr;
	
	forward_init(&forward, NULL);
	debug3("problems with %s", fwd_msg->node_name);
	if(fwd_msg->ret_list) {
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		ret_data_info->node_name = xstrdup(fwd_msg->node_name);
		memcpy(&ret_data_info->addr, &fwd_msg->addr, 
		       sizeof(slurm_addr));
		ret_data_info->nodeid = fwd_msg->header.srun_node_id;
		itr = list_iterator_create(fwd_msg->ret_list);	
		while((type = (ret_types_t *) list_next(itr)) != NULL) {
			if(type->msg_rc == SLURM_ERROR){
				list_push(type->ret_data_list, ret_data_info);
				break;
			}
		}
		list_iterator_destroy(itr);
		
		if(!type) {
			type = xmalloc(sizeof(ret_types_t));
			list_push(fwd_msg->ret_list, type);
			type->type = REQUEST_PING;
			type->msg_rc = SLURM_ERROR;
			type->err = err;
			type->ret_data_list = list_create(destroy_data_info);
			list_push(type->ret_data_list, ret_data_info);
		}		
	} 
	if(prev_cnt == 0) {
		debug3("no more to send to");
		return 0;
	}
	
	fwd_msg->header.srun_node_id = fwd_msg->header.forward.node_id[0];
	memcpy(&fwd_msg->addr, &fwd_msg->header.forward.addr[0], 
	       sizeof(slurm_addr));
	strncpy(fwd_msg->node_name,
		&fwd_msg->header.forward.name[i * MAX_SLURM_NAME],
		MAX_SLURM_NAME);
	i = 0;
	
	forward_set(&forward,
		    prev_cnt,
		    &i,
		    &fwd_msg->header.forward);

	destroy_forward(&fwd_msg->header.forward);
	forward_init(&fwd_msg->header.forward, &forward);
	
	return 1;
}

/*
 * forward_set - divide a mesage up into components for forwarding
 * IN: forward     - forward_t *   - struct to store forward info
 * IN: span        - int           - count of forwards to do
 * IN/OUT: pos     - int *         - position in the original messages addr 
 *                                   structure
 * IN: from        - forward_t *   - information from original message
 * RET: SLURM_SUCCESS - int
 */
extern int forward_set(forward_t *forward, 
		       int span,
		       int *pos,
		       forward_t *from)
{
        int j = 1;
	int total = from->cnt;

/* 	char name[MAX_SLURM_NAME]; */
/* 	strncpy(name, */
/* 		&from->name[(*pos) * MAX_SLURM_NAME], */
/* 		MAX_SLURM_NAME); */
/* 	info("forwarding to %s",name); */
	
	if(span > 0) {
		forward->addr = xmalloc(sizeof(slurm_addr) * span);
		forward->name = xmalloc(sizeof(char) 
					* (MAX_SLURM_NAME * span));
		
		forward->node_id = xmalloc(sizeof(int32_t) * span);
		forward->timeout = from->timeout;
		forward->init = FORWARD_INIT;
		
		while(j<span && ((*pos+j) < total)) {
			memcpy(&forward->addr[j-1],
			       &from->addr[*pos+j],
			       sizeof(slurm_addr));
			strncpy(&forward->name[(j-1) * MAX_SLURM_NAME],
				&from->name[(*pos+j) * MAX_SLURM_NAME],
				MAX_SLURM_NAME);

			if(from->node_id)
				forward->node_id[j-1] = from->node_id[*pos+j];
			else
				forward->node_id[j-1] = 0;

/* 			strncpy(name, */
/* 				&from->name[(*pos+j) * MAX_SLURM_NAME], */
/* 				MAX_SLURM_NAME); */
/* 			info("along with %s",name); */
			j++;
		}
		j--;
		forward->cnt = j;
		*pos += j;
	} else {
		forward_init(forward, NULL);
		forward->timeout = from->timeout;
	}
	
	return SLURM_SUCCESS;
}

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
extern int forward_set_launch(forward_t *forward, 
			      int span,
			      int *pos,
			      slurm_step_layout_t *step_layout,
			      slurm_addr *slurmd_addr,
			      hostlist_iterator_t itr,
			      int32_t timeout)
{
	
	int j=1, i;
	char *host = NULL;
	int total = step_layout->num_hosts;
	
	/* char name[MAX_SLURM_NAME]; */
/* 	strncpy(name, */
/* 		step_layout->host[*pos], */
/* 		MAX_SLURM_NAME); */
/* 	info("forwarding to %s",name); */
	
	if(span > 0) {
		forward->addr = xmalloc(sizeof(slurm_addr) * span);
		forward->name = 
			xmalloc(sizeof(char) * (MAX_SLURM_NAME * span));
		forward->node_id = xmalloc(sizeof(int32_t) * span);
		forward->timeout = timeout;
		forward->init = FORWARD_INIT;

		while(j<span && ((*pos+j) < total)) {
			i=0; 
			while((host = hostlist_next(itr))) { 
				if(!strcmp(host,
					   step_layout->host[*pos+j])) {
					free(host);
					break; 
				}
				i++; 
				free(host);
			}
			hostlist_iterator_reset(itr);
			memcpy(&forward->addr[j-1], 
			       &slurmd_addr[i], 
			       sizeof(slurm_addr));
			//forward->addr[j-1] = slurmd_addr[i];
			strncpy(&forward->name[(j-1) * MAX_SLURM_NAME],
				step_layout->host[*pos+j], 
				MAX_SLURM_NAME);
			forward->node_id[j-1] = (*pos+j);
			/* strncpy(name, */
/* 				step_layout->host[*pos+j], */
/* 				MAX_SLURM_NAME); */
/* 			info("along with %s",name);	 */
			j++;
		}
			
		j--;
		forward->cnt = j;
		*pos += j;
	} else {
		forward_init(forward, NULL);
		forward->timeout = timeout;
	}

	return SLURM_SUCCESS;
}

extern void forward_wait(slurm_msg_t * msg)
{
	int count = 0;
	ret_types_t *ret_type = NULL;
	ListIterator itr;

	/* wait for all the other messages on the tree under us */
	if(msg->forward_struct_init == FORWARD_INIT && msg->forward_struct) {
		debug2("looking for %d", msg->forward_struct->fwd_cnt);
		slurm_mutex_lock(&msg->forward_struct->forward_mutex);
		count = 0;
		if (msg->ret_list != NULL) {
			itr = list_iterator_create(msg->ret_list);
			while((ret_type = list_next(itr)) != NULL) {
				count += list_count(ret_type->ret_data_list);
			}
                        list_iterator_destroy(itr);
		}
		debug2("Got back %d", count);
		while((count < msg->forward_struct->fwd_cnt)) {
			pthread_cond_wait(&msg->forward_struct->notify, 
					  &msg->forward_struct->forward_mutex);
			count = 0;
			if (msg->ret_list != NULL) {
				itr = list_iterator_create(msg->ret_list);
				while((ret_type = list_next(itr)) != NULL) {
					count += list_count(
						ret_type->ret_data_list);
				}
                                list_iterator_destroy(itr);
			}
			debug2("Got back %d", count);
				
		}
		debug2("Got them all");
		slurm_mutex_unlock(&msg->forward_struct->forward_mutex);
		destroy_forward_struct(msg->forward_struct);
	}
	return;
}

extern int no_resp_forwards(forward_t *forward, List *ret_list, int err)
{
	ret_types_t *type = NULL;
	ret_data_info_t *ret_data_info = NULL;
	char name[MAX_SLURM_NAME];
	int i=0;
	if(!*ret_list)
		*ret_list = list_create(destroy_ret_types);
	if(forward->cnt == 0)
		goto no_forward;
	error("something bad happened");
	type = xmalloc(sizeof(ret_types_t));
	list_push(*ret_list, type);
	type->type = REQUEST_PING;
	type->msg_rc = SLURM_ERROR;
	type->err = err;
	type->ret_data_list = list_create(destroy_data_info);
	for(i=0; i<forward->cnt; i++) {
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		list_push(type->ret_data_list, ret_data_info);
		strncpy(name, 
			&forward->name[i * MAX_SLURM_NAME], 
			MAX_SLURM_NAME);
		ret_data_info->node_name = xstrdup(name);
		memcpy(&ret_data_info->addr, &forward->addr[i], 
		       sizeof(slurm_addr));
		ret_data_info->nodeid = forward->node_id[i];
	}
no_forward:
	return SLURM_SUCCESS;
}

void destroy_data_info(void *object)
{
	ret_data_info_t *ret_data_info = (ret_data_info_t *)object;
	if(ret_data_info) {
		xfree(ret_data_info->node_name);
		xfree(ret_data_info);
	}
}

void destroy_forward(forward_t *forward) 
{
	if(forward->init == FORWARD_INIT) {
		xfree(forward->addr);
		xfree(forward->name);
		xfree(forward->node_id);
		forward->cnt = 0;
		forward->init = 0;
	}
}

void destroy_forward_struct(forward_struct_t *forward_struct)
{
	if(forward_struct) {
		xfree(forward_struct->buf);
		xfree(forward_struct->forward_msg);
		slurm_mutex_destroy(&forward_struct->forward_mutex);
		pthread_cond_destroy(&forward_struct->notify);
		xfree(forward_struct);
	}
}
/* 
 * Free just the list from forwarded messages
 */
void destroy_ret_types(void *object)
{
	ret_types_t *ret_type = (ret_types_t *)object;
	ret_data_info_t *ret_data_info = NULL;
	if(ret_type) {
		if(ret_type->ret_data_list) {
			while((ret_data_info = 
			       list_pop(ret_type->ret_data_list))) {
				_destroy_data_info_data(ret_type->type, 
							ret_data_info);
				destroy_data_info(ret_data_info);
			}			
			list_destroy(ret_type->ret_data_list);
			ret_type->ret_data_list = NULL;
		}
		xfree(ret_type);
	}
}
