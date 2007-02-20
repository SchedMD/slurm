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
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/sbcast/sbcast.h"

#define MAX_RETRIES     10
#define MAX_THREADS      8	/* These can be huge messages, so
				 * only run MAX_THREADS at one time */
typedef struct thd {
	pthread_t thread;	/* thread ID */
	slurm_msg_t *msg;	/* message to send */
	int rc;			/* highest return codes from RPC */
	char node_name[MAX_SLURM_NAME];
} thd_t;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;

static void *_agent_thread(void *args);

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
			if (!ret_data_info->node_name) {
				ret_data_info->node_name = 
					xstrdup(thread_ptr->node_name);
				ret_data_info->addr = msg->address;
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

/* Issue the RPC to transfer the file's data */
extern void send_rpc(file_bcast_msg_t *bcast_msg,
		resource_allocation_response_msg_t *alloc_resp)
{
	/* Preserve some data structures across calls for better performance */
	static forward_t from, forward[MAX_THREADS];
	static int threads_used = 0;
	static slurm_msg_t msg[MAX_THREADS];

	int i, fanout, rc = SLURM_SUCCESS;
	int retries = 0;
	thd_t thread_info[MAX_THREADS];
	pthread_attr_t attr;

	if (threads_used == 0) {
		hostlist_t hl;
		int *span;

		if (params.fanout)
			fanout = MIN(MAX_THREADS, params.fanout);
		else
			fanout = MAX_THREADS;
		span = set_span(alloc_resp->node_cnt, fanout);
		from.cnt  = alloc_resp->node_cnt;
		from.name = xmalloc(MAX_SLURM_NAME * alloc_resp->node_cnt);
		hl = hostlist_create(alloc_resp->node_list);
		for (i=0; i<alloc_resp->node_cnt; i++) {
			char *host = hostlist_shift(hl);
			strncpy(&from.name[MAX_SLURM_NAME*i], host, 
				MAX_SLURM_NAME);
			free(host);
		}
		hostlist_destroy(hl);
		from.addr	= alloc_resp->node_addr;
		from.node_id	= NULL;
		from.timeout	= SLURM_MESSAGE_TIMEOUT_MSEC_STATIC;

		for (i=0; i<alloc_resp->node_cnt; i++) {
			int j = i;
			strncpy(thread_info[threads_used].node_name, 
				&from.name[MAX_SLURM_NAME*i], MAX_SLURM_NAME); 
			       
			forward_set(&forward[threads_used], span[threads_used],
				&i, &from);
			msg[threads_used].msg_type    = REQUEST_FILE_BCAST;
			msg[threads_used].address     = alloc_resp->node_addr[j];
			msg[threads_used].data        = bcast_msg;
			msg[threads_used].forward     = forward[threads_used];
			msg[threads_used].ret_list    = NULL;
			msg[threads_used].orig_addr.sin_addr.s_addr = 0;
			msg[threads_used].srun_node_id = 0;
			
			
			threads_used++;
		}
		xfree(span);
		debug("using %d threads", threads_used);
	}

	slurm_attr_init(&attr);
	if (pthread_attr_setstacksize(&attr, 3 * 1024*1024))
		error("pthread_attr_setstacksize: %m");
	if (pthread_attr_setdetachstate (&attr,
			PTHREAD_CREATE_JOINABLE))
		error("pthread_attr_setdetachstate error %m");

	for (i=0; i<threads_used; i++) {
		slurm_mutex_lock(&agent_cnt_mutex);
		agent_cnt++;
		slurm_mutex_unlock(&agent_cnt_mutex);

		thread_info[i].msg = &msg[i];
		while (pthread_create(&thread_info[i].thread,
				&attr, _agent_thread,
				(void *) &thread_info[i])) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and retry */
		}
	}

	/* wait until pthreads complete */
	slurm_mutex_lock(&agent_cnt_mutex);
	while (agent_cnt)
		pthread_cond_wait(&agent_cnt_cond, &agent_cnt_mutex);
	slurm_mutex_unlock(&agent_cnt_mutex);
	pthread_attr_destroy(&attr);

	for (i=0; i<threads_used; i++)
		 rc = MAX(rc, thread_info[i].rc);

	if (rc)
		exit(1);
}
