/*****************************************************************************\
 *  forward.c - forward RPCs through hierarchical slurmd communications
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov>.
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
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_interface.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#define MAX_RETRIES 3

void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = (forward_msg_t *)arg;
	Buf buffer = init_buf(0);
	int i=0;
	List ret_list = NULL;
	slurm_fd fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	char *name = NULL;
	hostlist_t hl = hostlist_create(fwd_msg->header.forward.nodelist);
	slurm_addr addr;
	char buf[8196];
	int steps = 0;
	int start_timeout = fwd_msg->timeout;
		
	/* repeat until we are sure the message was sent */ 
	while((name = hostlist_shift(hl))) {
		if(slurm_conf_get_addr(name, &addr) == SLURM_ERROR) {
			error("forward_thread: can't find address for host "
			      "%s, check slurm.conf", name);
			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name,
					SLURM_COMMUNICATIONS_CONNECTION_ERROR);
 			free(name);
			if (hostlist_count(hl) > 0) {
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				continue;
			}
			goto cleanup;			
		}
		if ((fd = slurm_open_msg_conn(&addr)) < 0) {
			error("forward_thread to %s: %m", name);

			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name,
					SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			free(name);
			if (hostlist_count(hl) > 0) {
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				continue;
			}
			goto cleanup;			
		}
		hostlist_ranged_string(hl, sizeof(buf), buf);

		xfree(fwd_msg->header.forward.nodelist);
		fwd_msg->header.forward.nodelist = xstrdup(buf);
		fwd_msg->header.forward.cnt = hostlist_count(hl);
		/* info("sending %d forwards (%s) to %s", */
/* 		     fwd_msg->header.forward.cnt, */
/* 		     fwd_msg->header.forward.nodelist, name); */
		if (fwd_msg->header.forward.nodelist[0]) {
			debug3("forward: send to %s along with %s",
			       name, fwd_msg->header.forward.nodelist);
		} else
			debug3("forward: send to %s ", name);
		
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
			mark_as_failed_forward(&fwd_msg->ret_list, name, 
					       errno);
			free(name);
			if(hostlist_count(hl) > 0) {
				free_buf(buffer);	
				buffer = init_buf(0);
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				slurm_close_accepted_conn(fd);
				fd = -1;
				continue;
			}
			goto cleanup;			
		}

		if ((fwd_msg->header.msg_type == REQUEST_SHUTDOWN) ||
		    (fwd_msg->header.msg_type == REQUEST_RECONFIGURE)) {
			slurm_mutex_lock(fwd_msg->forward_mutex);
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			list_push(fwd_msg->ret_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			free(name);
			i=0;
			while((name = hostlist_shift(hl))) {
				ret_data_info = 
					xmalloc(sizeof(ret_data_info_t));
				list_push(fwd_msg->ret_list, ret_data_info);
				ret_data_info->node_name = xstrdup(name);
				free(name);
			}
			goto cleanup;
		}

		if(fwd_msg->header.forward.cnt > 0) {
			static int message_timeout = -1;
			if (message_timeout < 0)
				message_timeout = 
					slurm_get_msg_timeout() * 1000;
			steps = (fwd_msg->header.forward.cnt+1) /
				slurm_get_tree_width();
			fwd_msg->timeout = (message_timeout*steps);
/* 			info("got %d * %d = %d", message_timeout, steps, fwd_msg->timeout); */
			steps++;
			fwd_msg->timeout += (start_timeout*steps);
/* 			info("now  + %d*%d = %d", start_timeout, steps, fwd_msg->timeout); */
		}	
		
		ret_list = slurm_receive_msgs(fd, steps, fwd_msg->timeout);
		/* info("sent %d forwards got %d back", */
/* 		     fwd_msg->header.forward.cnt, list_count(ret_list)); */
		     
		if(!ret_list || (fwd_msg->header.forward.cnt != 0 
				 && list_count(ret_list) <= 1)) {
			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name, 
					       errno);
			free(name);
			if(ret_list)
				list_destroy(ret_list);
			if (hostlist_count(hl) > 0) {
				free_buf(buffer);	
				buffer = init_buf(0);
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				slurm_close_accepted_conn(fd);
				fd = -1;
				continue;
			}
			goto cleanup;			
		} else if((fwd_msg->header.forward.cnt+1)
			  != list_count(ret_list)) {
			/* this should never be called since the above
			   should catch the failed forwards and pipe
			   them back down, but this is here so we
			   never have to worry about a locked
			   mutex */
			ListIterator itr = NULL;
			char *tmp = NULL;
			int first_node_found = 0;
			hostlist_iterator_t host_itr
				= hostlist_iterator_create(hl);
			error("We shouldn't be here.  We forwarded to %d "
			      "but only got %d back",
			      (fwd_msg->header.forward.cnt+1),
			      list_count(ret_list));
			while((tmp = hostlist_next(host_itr))) {
				int node_found = 0;
				itr = list_iterator_create(ret_list);
				while((ret_data_info = list_next(itr))) {
					if(!ret_data_info->node_name) {
						first_node_found = 1;
						ret_data_info->node_name =
							xstrdup(name);
					}
					if(!strcmp(tmp,
						   ret_data_info->node_name)) {
						node_found = 1;
						break;
					}
				}
				list_iterator_destroy(itr);
				if(!node_found) {
					mark_as_failed_forward(
						&fwd_msg->ret_list,
						tmp, 
						SLURM_COMMUNICATIONS_CONNECTION_ERROR);	
				}
				free(tmp);
			}
			hostlist_iterator_destroy(host_itr);
			if(!first_node_found) {
				mark_as_failed_forward(&fwd_msg->ret_list,
						       name, 
						       SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			}
		}
		break;
	}

	slurm_mutex_lock(fwd_msg->forward_mutex);
	if(ret_list) {
		while((ret_data_info = list_pop(ret_list)) != NULL) {
			if(!ret_data_info->node_name) {
				ret_data_info->node_name = xstrdup(name);
			}
			list_push(fwd_msg->ret_list, ret_data_info);
			debug3("got response from %s",
			       ret_data_info->node_name);
		}
		list_destroy(ret_list);
	}
	free(name);
cleanup:
	if ((fd >= 0) && slurm_close_accepted_conn(fd) < 0)
		error ("close(%d): %m", fd);
	hostlist_destroy(hl);
	destroy_forward(&fwd_msg->header.forward);
	free_buf(buffer);	
	pthread_cond_signal(fwd_msg->notify);
	slurm_mutex_unlock(fwd_msg->forward_mutex);

	return (NULL);
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
		forward->nodelist = from->nodelist;
		forward->init = from->init;
	} else {
		forward->cnt = 0;
		forward->timeout = 0;
		forward->nodelist = NULL;
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
	int i = 0, j = 0;
	int retries = 0;
	forward_msg_t *forward_msg = NULL;
	int thr_count = 0;
	int *span = set_span(header->forward.cnt, 0);
	hostlist_t hl = NULL;
	hostlist_t forward_hl = NULL;
	char *name = NULL;

	if(!forward_struct->ret_list) {
		error("didn't get a ret_list from forward_struct");
		xfree(span);
		return SLURM_ERROR;
	}
	hl = hostlist_create(header->forward.nodelist);	
	slurm_mutex_init(&forward_struct->forward_mutex);
	pthread_cond_init(&forward_struct->notify, NULL);
	
	forward_struct->forward_msg = 
		xmalloc(sizeof(forward_msg_t) * header->forward.cnt);
	i = 0;
	
	while((name = hostlist_shift(hl))) {
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		char buf[8192];
		
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
		    (&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		forward_msg = &forward_struct->forward_msg[thr_count];
		forward_msg->ret_list = forward_struct->ret_list;
		
		forward_msg->timeout = forward_struct->timeout;
		
		if(forward_msg->timeout <= 0) {
			/* convert secs to msec */
			forward_msg->timeout  = slurm_get_msg_timeout() * 1000; 
		}

		forward_msg->notify = &forward_struct->notify;
		forward_msg->forward_mutex = &forward_struct->forward_mutex;
		forward_msg->buf_len = forward_struct->buf_len;
		forward_msg->buf = forward_struct->buf;
		
		memcpy(&forward_msg->header.orig_addr, 
		       &header->orig_addr, 
		       sizeof(slurm_addr));
		
		forward_msg->header.version = header->version;
		forward_msg->header.flags = header->flags;
		forward_msg->header.msg_type = header->msg_type;
		forward_msg->header.body_length = header->body_length;
		forward_msg->header.ret_list = NULL;
		forward_msg->header.ret_cnt = 0;
		
		forward_hl = hostlist_create(name);
		i++;
		free(name);
		for(j = 0; j < span[thr_count]; j++) {
			name = hostlist_shift(hl);
			if(!name)
				break;
			hostlist_push(forward_hl, name);
			free(name);
			i++;
		}
		hostlist_uniq(forward_hl);
		hostlist_ranged_string(forward_hl, sizeof(buf), buf);
		hostlist_destroy(forward_hl);
		forward_msg->header.forward.nodelist = xstrdup(buf);
		while(pthread_create(&thread_agent, &attr_agent,
				   _forward_thread, 
				   (void *)forward_msg)) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and try again */
		}
		slurm_attr_destroy(&attr_agent);
		thr_count++; 
	}
	hostlist_destroy(hl);
	xfree(span);
	return SLURM_SUCCESS;
}


/*
 * mark_as_failed_forward- mark a node as failed and add it to "ret_list"
 *
 * IN: ret_list       - List *   - ret_list to put ret_data_info
 * IN: node_name      - char *   - node name that failed
 * IN: err            - int      - error message from attempt
 *
 */
extern void mark_as_failed_forward(List *ret_list, char *node_name, int err) 
{
	ret_data_info_t *ret_data_info = NULL;
	
	debug3("problems with %s", node_name);
	if(!*ret_list) 
		*ret_list = list_create(destroy_data_info);
	
	ret_data_info = xmalloc(sizeof(ret_data_info_t));
	ret_data_info->node_name = xstrdup(node_name);
	ret_data_info->type = RESPONSE_FORWARD_FAILED;
	ret_data_info->err = err;
	list_push(*ret_list, ret_data_info);
			
	return;
}

extern void forward_wait(slurm_msg_t * msg)
{
	int count = 0;
	
	/* wait for all the other messages on the tree under us */
	if(msg->forward_struct) {
		debug2("looking for %d", msg->forward_struct->fwd_cnt);
		slurm_mutex_lock(&msg->forward_struct->forward_mutex);
		count = 0;
		if (msg->ret_list != NULL) {
			count += list_count(msg->ret_list);
		}
		debug2("Got back %d", count);
		while((count < msg->forward_struct->fwd_cnt)) {
			pthread_cond_wait(&msg->forward_struct->notify, 
					  &msg->forward_struct->forward_mutex);
			count = 0;
			if (msg->ret_list != NULL) {
				count += list_count(msg->ret_list);
			}
			debug2("Got back %d", count);
				
		}
		debug2("Got them all");
		slurm_mutex_unlock(&msg->forward_struct->forward_mutex);
		destroy_forward_struct(msg->forward_struct);
	}
	return;
}

void destroy_data_info(void *object)
{
	ret_data_info_t *ret_data_info = (ret_data_info_t *)object;
	if(ret_data_info) {
		slurm_free_msg_data(ret_data_info->type, 
				    ret_data_info->data);		
		xfree(ret_data_info->node_name);
		xfree(ret_data_info);
	}
}

void destroy_forward(forward_t *forward) 
{
	if(forward->init == FORWARD_INIT) {
		xfree(forward->nodelist);
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

