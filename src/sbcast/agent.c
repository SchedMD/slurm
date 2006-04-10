/*****************************************************************************\
 *  agent.c - File transfer agent (handles message traffic)
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/sbcast/sbcast.h"

#define MAX_RETRIES     10
#define MAX_THREADS      4	/* These are huge messages, so only
				 * run MAX_THREADS at one time */

typedef enum {
	DSH_NEW,	/* Request not yet started */
	DSH_ACTIVE,	/* Request in progress */
	DSH_DONE,	/* Request completed normally */
	DSH_NO_RESP,	/* Request timed out */
	DSH_FAILED	/* Request resulted in error */
} state_t;

typedef struct thd {
	pthread_t thread;	/* thread ID */
	pthread_attr_t attr;	/* thread attributes */
	slurm_msg_t *msg;	/* message to send */
	int rc;			/* highest return codes from RPC */
} thd_t;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;

static void *_agent_thread(void *args);

/* issue the RPC to ship the file's data */
extern void send_rpc(file_bcast_msg_t *bcast_msg, 
		resource_allocation_response_msg_t *alloc_resp)
{
	/* This code will handle message fanout to multiple slurmd */
	forward_t from, *forward;
	hostlist_t hl;
	int i, rc = 0, retries = 0;
	int thr_count = 0, *span = set_span(alloc_resp->node_cnt);
	thd_t *thread_info;

	thread_info	= xmalloc(slurm_get_tree_width() * sizeof(thd_t));
	forward         = xmalloc(slurm_get_tree_width() * sizeof(forward_t));
	from.cnt	= alloc_resp->node_cnt;
	from.name	= xmalloc(MAX_SLURM_NAME * alloc_resp->node_cnt);
	hl = hostlist_create(alloc_resp->node_list);
	for (i=0; i<alloc_resp->node_cnt; i++) {
		char *host = hostlist_shift(hl);
		strncpy(&from.name[MAX_SLURM_NAME*i], host, MAX_SLURM_NAME);
		free(host);
	}
	hostlist_destroy(hl);
	from.addr	= alloc_resp->node_addr;
	from.node_id	= NULL;
	from.timeout	= SLURM_MESSAGE_TIMEOUT_MSEC_STATIC;
	for (i=0; i<alloc_resp->node_cnt; i++) {
		forward_set(&forward[i], span[thr_count], &i, &from);
		thread_info[i].msg = xmalloc(sizeof(slurm_msg_t));
		thread_info[i].msg->msg_type	= REQUEST_FILE_BCAST;
		thread_info[i].msg->data	= bcast_msg;
		thread_info[i].msg->forward	= forward[i];
		thread_info[i].msg->ret_list	= NULL;
		thread_info[i].msg->orig_addr.sin_addr.s_addr = 0;
		thread_info[i].msg->srun_node_id= 0;
		thread_info[i].rc		= -1;
		thread_info[i].msg->address	= alloc_resp->node_addr[i];
		thr_count++;
	}
	xfree(span);
	debug("spawning %d threads", thr_count);

	for (i=0; i<thr_count; i++) {
		slurm_mutex_lock(&agent_cnt_mutex);
		while (agent_cnt >= MAX_THREADS)
			pthread_cond_wait(&agent_cnt_cond, &agent_cnt_mutex);
		agent_cnt++;
		slurm_mutex_unlock(&agent_cnt_mutex);

		slurm_attr_init(&thread_info[i].attr);
		if (pthread_attr_setdetachstate (&thread_info[i].attr,
				PTHREAD_CREATE_JOINABLE))
			error("pthread_attr_setdetachstate error %m");
		while (pthread_create(&thread_info[i].thread, 
				&thread_info[i].attr, 
				_agent_thread,
				(void *) &thread_info[i])) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1); 	/* sleep and again */
		}
	}

	/* wait until pthreads complete */
	slurm_mutex_lock(&agent_cnt_mutex);
	while (agent_cnt)
		pthread_cond_wait(&agent_cnt_cond, &agent_cnt_mutex);
	slurm_mutex_unlock(&agent_cnt_mutex);

	for (i=0; i<thr_count; i++) {
		rc = MAX(rc, thread_info[i].rc);
		xfree(thread_info[i].msg);
		destroy_forward(&forward[i]);
	}
	xfree(from.name);
	xfree(forward);
	xfree(thread_info);
	if (rc)
		exit(1);
}

static void *_agent_thread(void *args)
{
	List ret_list = NULL;
	ret_types_t *ret_type = NULL;
	thd_t *thread_ptr = (thd_t *) args;
	slurm_msg_t *msg = thread_ptr->msg;
	ListIterator itr, data_itr;
	ret_data_info_t *ret_data_info = NULL;
	int rc = 0;

	ret_list = slurm_send_recv_rc_msg(msg, 
			SLURM_MESSAGE_TIMEOUT_MSEC_STATIC);
	if (ret_list == NULL) {
		error("slurm_send_recv_rc_msg: %m");
		exit(1);
	}

	itr = list_iterator_create(ret_list);
	while ((ret_type = list_next(itr)) != NULL) {
		data_itr = list_iterator_create(ret_type->ret_data_list);
		while ((ret_data_info = list_next(data_itr)) != NULL) {
			if (ret_type->msg_rc == SLURM_SUCCESS)
				continue;
			if (!strcmp(ret_data_info->node_name,
					"localhost")) {
				xfree(ret_data_info->node_name);
				ret_data_info->node_name = 
					xmalloc(MAX_SLURM_NAME);
				getnodename(ret_data_info->node_name, 
					MAX_SLURM_NAME);
			}
			error("REQUEST_FILE_BCAST(%s): %s",
				ret_data_info->node_name,
				slurm_strerror(ret_type->msg_rc));
			rc = MAX(rc, ret_type->msg_rc);
		}
		list_iterator_destroy(data_itr);
	}

	thread_ptr->rc = rc;
	list_iterator_destroy(itr);
	if (ret_list)
		list_destroy(ret_list);
	slurm_mutex_lock(&agent_cnt_mutex);
	agent_cnt--;
	slurm_mutex_unlock(&agent_cnt_mutex);
	pthread_cond_broadcast(&agent_cnt_cond);
	return NULL;
}

