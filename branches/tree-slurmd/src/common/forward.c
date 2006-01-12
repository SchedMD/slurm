/*****************************************************************************\
 *  submit.c - submit a job with supplied contraints
 *  $Id: submit.c 6636 2005-11-17 19:50:10Z jette $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "src/common/forward.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#define BUF_SIZE 4096
#define MAX_RETRIES 3

void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = (forward_msg_t *)arg;
	unsigned int tmplen, msglen;
	Buf buffer = init_buf(0);
	int retry = 0;
	int i=0;
	int rc = SLURM_SUCCESS;
	List ret_list = NULL;
	ret_types_t *type = NULL;
	ret_types_t *returned_type = NULL;
	slurm_msg_t msg;
	slurm_fd fd;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;
	char name[MAX_NAME_LEN];

	if ((fd = slurm_open_msg_conn(&fwd_msg->addr)) < 0) {
		error("forward_thread: %m");
		ret_list = list_create(destroy_ret_types);
		if(fwd_msg->header.forward.cnt == 0)
			goto nothing_sent;
		type = xmalloc(sizeof(ret_types_t));
		list_push(ret_list, type);
		type->type = REQUEST_PING;
		type->msg_rc = SLURM_ERROR;
		ret_data_info->data = NULL;
		type->err = errno;
		for(i=0; i<fwd_msg->header.forward.cnt; i++) {
			strncpy(name,
				&fwd_msg->header.
				forward.name[i * MAX_NAME_LEN],
				MAX_NAME_LEN);
			ret_data_info = xmalloc(sizeof(ret_types_t));
			list_push(type->ret_data_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			ret_data_info->nodeid = 
				fwd_msg->header.forward.node_id[i];
		}
		goto nothing_sent;
	}
	pack_header(&fwd_msg->header, buffer);
	
	tmplen = get_buf_offset(buffer);

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
		error("slurm_msg_sendto: %m");
		ret_list = list_create(destroy_ret_types);
		if(fwd_msg->header.forward.cnt == 0)
			goto nothing_sent;
		type = xmalloc(sizeof(ret_types_t));
		list_push(ret_list, type);
		type->type = REQUEST_PING;
		type->msg_rc = SLURM_ERROR;
		ret_data_info->data = NULL;
		type->err = errno;
		for(i=0; i<fwd_msg->header.forward.cnt; i++) {
			strncpy(name,
				&fwd_msg->header.
				forward.name[i * MAX_NAME_LEN],
				MAX_NAME_LEN);
			ret_data_info = xmalloc(sizeof(ret_types_t));
			list_push(type->ret_data_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			ret_data_info->nodeid =
				fwd_msg->header.forward.node_id[i];
		}
		goto nothing_sent;
	}

	if ((fwd_msg->header.msg_type == REQUEST_SHUTDOWN) ||
	    (fwd_msg->header.msg_type == REQUEST_RECONFIGURE)) {
		pthread_mutex_lock(fwd_msg->forward_mutex);
		type = xmalloc(sizeof(ret_types_t));
		list_push(fwd_msg->ret_list, type);
		type->ret_data_list = list_create(destroy_data_info);
		ret_data_info = xmalloc(sizeof(ret_types_t));
		list_push(type->ret_data_list, ret_data_info);
		ret_data_info->node_name = xstrdup(fwd_msg->node_name);
		ret_data_info->nodeid = fwd_msg->header.srun_node_id;
		for(i=0; i<fwd_msg->header.forward.cnt; i++) {
			strncpy(name,
				&fwd_msg->header.
				forward.name[i * MAX_NAME_LEN],
				MAX_NAME_LEN);
			ret_data_info = xmalloc(sizeof(ret_types_t));
			list_push(type->ret_data_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			ret_data_info->nodeid = 
				fwd_msg->header.forward.node_id[i];
		}
		pthread_mutex_unlock(fwd_msg->forward_mutex);
		pthread_cond_signal(fwd_msg->notify);
	
		goto cleanup;
	}
	ret_list = slurm_receive_msg(fd, &msg, fwd_msg->timeout);

	if ((fd >= 0) && slurm_close_accepted_conn(fd) < 0)
		error ("close(%d): %m", fd);
	g_slurm_auth_destroy(msg.cred);

nothing_sent:
	type = xmalloc(sizeof(ret_types_t));
	list_push(ret_list, type);

	type->err = errno;
	type->ret_data_list = list_create(destroy_data_info);
	ret_data_info = xmalloc(sizeof(ret_types_t));
	list_push(type->ret_data_list, ret_data_info);
	ret_data_info->node_name = xstrdup(fwd_msg->node_name);
	ret_data_info->nodeid = fwd_msg->header.srun_node_id;
						
	if(errno != SLURM_SUCCESS) {
		type->type = REQUEST_PING;
		type->msg_rc = SLURM_ERROR;
		ret_data_info->data = NULL;
	} else {
		type->type = msg.msg_type;
		type->msg_rc = ((return_code_msg_t *)msg.data)->return_code;
		ret_data_info->data = msg.data;
	}
	
	while((returned_type = list_pop(ret_list)) != NULL) {
		pthread_mutex_lock(fwd_msg->forward_mutex);
		itr = list_iterator_create(fwd_msg->ret_list);	
		while((type = (ret_types_t *) list_next(itr)) != NULL) {
			if(type->msg_rc == returned_type->msg_rc) {
				while(ret_data_info = 
				      list_pop(returned_type->ret_data_list)) {
					list_push(type->ret_data_list, 
						  ret_data_info);
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
			while(ret_data_info = 
			      list_pop(returned_type->ret_data_list)) {
				list_push(type->ret_data_list, ret_data_info);
			}
		}		
		destroy_ret_types(returned_type);
		pthread_mutex_unlock(fwd_msg->forward_mutex);
	}
	pthread_cond_signal(fwd_msg->notify);
	list_destroy(ret_list);
cleanup:
	xfree(fwd_msg->buf);
	destroy_forward(&fwd_msg->header.forward);
	free_buf(buffer);	
}

extern int forward_msg(forward_struct_t *forward_struct, 
		       header_t *header)
{
	int i;
	int retries = 0;
	forward_msg_t *forward_msg;
	int thr_count = 0;
	int *span = set_span(header->forward.cnt);
	Buf buffer = forward_struct->buffer;

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
		
		forward_msg->header.orig_addr = header->orig_addr;
		forward_msg->header.version = header->version;
		forward_msg->header.flags = header->flags;
		forward_msg->header.msg_type = header->msg_type;
		forward_msg->header.body_length = header->body_length;
		forward_msg->header.srun_node_id = header->forward.node_id[i];
		forward_msg->header.ret_list = NULL;
		forward_msg->header.ret_cnt = 0;
	
		forward_msg->addr = header->forward.addr[i];
		strncpy(forward_msg->node_name,
			&header->forward.name[i * MAX_NAME_LEN],
			MAX_NAME_LEN);
	        
		set_forward_addrs(&forward_msg->header.forward,
				  span[thr_count],
				  &i,
				  header->forward.cnt,
				  header->forward.addr,
				  header->forward.name,
				  header->forward.node_id);
				  
		forward_msg->buf_len = remaining_buf(buffer);
		forward_msg->buf = 
			xmalloc(sizeof(char)*forward_msg->buf_len);
		memcpy(forward_msg->buf, 
		       &buffer->head[buffer->processed], 
		       forward_msg->buf_len);
			
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
 * set_forward_addrs - add to the message possible forwards to go to
 * IN: forward     - forward_t *   - message to add forwards to
 * IN: thr_count   - int           - number of messages already done
 * IN: pos         - int *         - posistion in the forward_addr and names
 *                                   will change to update to set the 
 *				     correct start after forwarding 
 *      			     information has been added.
 * IN: forward_addr- sockaddr_in * - list of address structures to forward to
 * IN: forward_names - char *      - list of names in MAX_NAME_LEN increments
 * RET: SLURM_SUCCESS - int
 */
extern int set_forward_addrs (forward_t *forward, 
			      int span,
			      int *pos,
			      int total,
			      struct sockaddr_in *forward_addr,
			      char *forward_names,
			      int *forward_ids)
{
        int j = 1;
	char name[MAX_NAME_LEN];
	
	strncpy(name,
		&forward_names[(*pos) * MAX_NAME_LEN],
		MAX_NAME_LEN);
	info("forwarding to %s",name);
	
	if(span > 0) {
		forward->addr = xmalloc(sizeof(slurm_addr) * span);
		forward->name = xmalloc(sizeof(char) * (MAX_NAME_LEN * span));
		forward->node_id = xmalloc(sizeof(int) * span);
					
		while(j<span && ((*pos+j) < total)) {
			forward->addr[j-1] = forward_addr[*pos+j];
			strncpy(&forward->name[(j-1) * MAX_NAME_LEN], 
				&forward_names[(*pos+j) * MAX_NAME_LEN], 
				MAX_NAME_LEN);

			if(forward_ids)
				forward->node_id[j-1] = forward_ids[*pos+j];
			else
				forward->node_id[j-1] = 0;

			strncpy(name,
				&forward_names[(*pos+j) * MAX_NAME_LEN],
				MAX_NAME_LEN);
			info("along with %s",name);		
			j++;
		}
		j--;
		forward->cnt = j;
		*pos += j;
	} else {
		forward->cnt = 0;
		forward->addr = NULL;
		forward->name = NULL;
		forward->node_id = NULL;
	}
	
	return SLURM_SUCCESS;
}

extern int set_forward_launch (forward_t *forward, 
			       int span,
			       int *pos,
			       srun_job_t *job,
			       hostlist_iterator_t itr)
{
	
	int j=1, i;
	char *host = NULL;
	int total = job->step_layout->num_hosts;
	
	/* char name[MAX_NAME_LEN]; */
/* 	strncpy(name, */
/* 		job->step_layout->host[*pos], */
/* 		MAX_NAME_LEN); */
/* 	info("forwarding to %s",name); */
	
	if(span > 0) {
		forward->addr = xmalloc(sizeof(slurm_addr) * span);
		forward->name = xmalloc(sizeof(char) * (MAX_NAME_LEN * span));
		forward->node_id = xmalloc(sizeof(int) * span);
		
		while(j<span && ((*pos+j) < total)) {
			i=0; 
			while(host = hostlist_next(itr)) { 
				if(!strcmp(host,
					   job->step_layout->host[*pos+j])) {
					free(host);
					break; 
				}
				i++; 
				free(host);
			}
			hostlist_iterator_reset(itr);
			forward->addr[j-1] = job->slurmd_addr[i];
			strncpy(&forward->name[(j-1) * MAX_NAME_LEN], 
				job->step_layout->host[*pos+j], 
				MAX_NAME_LEN);
			forward->node_id[j-1] = (*pos+j);
			/* strncpy(name, */
/* 				job->step_layout->host[*pos+j], */
/* 				MAX_NAME_LEN); */
/* 			info("along with %s",name);	 */	
			j++;
		}
			
		j--;
		forward->cnt = j;
		*pos += j;
	} else {
		forward->cnt = 0;
		forward->addr = NULL;
		forward->name = NULL;
		forward->node_id = NULL;
	}

	return SLURM_SUCCESS;
}
extern int *set_span(int total)
{
	int *span = xmalloc(sizeof(int)*forward_span_count);
	int left = total;
	int i = 0;
	
	memset(span,0,forward_span_count);
	if(total <= forward_span_count) {
		return span;
	} 
	
	while(left>0) {
		for(i=0; i<forward_span_count; i++) {
			if((forward_span_count-i)>=left) {
				if(span[i] == 0) {
					left = 0;
					break;
				} else {
					span[i] += left;
					left = 0;
					break;
				}
			} else if(left<=forward_span_count) {
				span[i]+=left;
				left = 0;
				break;
			}
			span[i] += forward_span_count;
			left -= forward_span_count;
		}
	}
	return span;
}

void destroy_data_info(void *object)
{
	ret_data_info_t *ret_data_info = (ret_data_info_t *)object;
	if(ret_data_info) {
		xfree(ret_data_info->node_name);
		/*FIXME: needs to probably be something for all 
		  types or messages */
		xfree(ret_data_info->data);
		xfree(ret_data_info);
	}
}

void destroy_forward_struct(void *object)
{
	forward_struct_t *forward_struct = (forward_struct_t *)object;
	destroy_forward_msg(forward_struct->forward_msg);
}

void destroy_forward(void *object) 
{
	forward_t *forward = (forward_t *)object;
	xfree(forward->addr);
	xfree(forward->name);
	xfree(forward->node_id);
}

void destroy_forward_msg(void *object)
{
	forward_msg_t *forward_msg = (forward_msg_t *)object;
	xfree(forward_msg->buf);
	destroy_forward(&forward_msg->header.forward);	
}
/* 
 * Free just the list from forwarded messages
 */
void destroy_ret_types(void *object)
{
	ret_types_t *ret_type = (ret_types_t *)object;
	if(ret_type) {
		if(ret_type->ret_data_list) {
			list_destroy(ret_type->ret_data_list);
			ret_type->ret_data_list = NULL;
		}
		xfree(ret_type);
	}
}
