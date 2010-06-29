/*****************************************************************************\
 *  agent.c - File transfer agent (handles message traffic)
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
	slurm_msg_t msg;	/* message to send */
	int rc;			/* highest return codes from RPC */
	char *nodelist;
} thd_t;

static pthread_mutex_t agent_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cnt_cond  = PTHREAD_COND_INITIALIZER;
static int agent_cnt = 0;

static void *_agent_thread(void *args);

static void *_agent_thread(void *args)
{
	List ret_list = NULL;
	thd_t *thread_ptr = (thd_t *) args;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	int rc = 0, msg_rc;

	ret_list = slurm_send_recv_msgs(thread_ptr->nodelist,
					&thread_ptr->msg,
					params.timeout, false);
	if (ret_list == NULL) {
		error("slurm_send_recv_msgs: %m");
		exit(1);
	}

	itr = list_iterator_create(ret_list);
	while ((ret_data_info = list_next(itr))) {
		msg_rc = slurm_get_return_code(ret_data_info->type,
					       ret_data_info->data);
		if (msg_rc == SLURM_SUCCESS)
			continue;

		error("REQUEST_FILE_BCAST(%s): %s",
		      ret_data_info->node_name,
		      slurm_strerror(msg_rc));
		rc = MAX(rc, msg_rc);
	}

	thread_ptr->rc = rc;
	list_iterator_destroy(itr);
	if (ret_list)
		list_destroy(ret_list);
	slurm_mutex_lock(&agent_cnt_mutex);
	agent_cnt--;
	pthread_cond_broadcast(&agent_cnt_cond);
	slurm_mutex_unlock(&agent_cnt_mutex);
	return NULL;
}

/* Issue the RPC to transfer the file's data */
extern void send_rpc(file_bcast_msg_t *bcast_msg,
		     job_sbcast_cred_msg_t *sbcast_cred)
{
	/* Preserve some data structures across calls for better performance */
	static int threads_used = 0;
	static thd_t thread_info[MAX_THREADS];

	int i, fanout, rc = SLURM_SUCCESS;
	int retries = 0;
	pthread_attr_t attr;

	if (threads_used == 0) {
		hostlist_t hl;
		hostlist_t new_hl;
		int *span = NULL;
		char buf[8192];
		char *name = NULL;

		if (params.fanout)
			fanout = MIN(MAX_THREADS, params.fanout);
		else
			fanout = MAX_THREADS;

		span = set_span(sbcast_cred->node_cnt, fanout);

		hl = hostlist_create(sbcast_cred->node_list);

		i = 0;
		while (i < sbcast_cred->node_cnt) {
			int j = 0;
			name = hostlist_shift(hl);
			if(!name) {
				debug3("no more nodes to send to");
				break;
			}
			new_hl = hostlist_create(name);
			free(name);
			i++;
			for(j = 0; j < span[threads_used]; j++) {
				name = hostlist_shift(hl);
				if(!name)
					break;
				hostlist_push(new_hl, name);
				free(name);
				i++;
			}
			hostlist_ranged_string(new_hl, sizeof(buf), buf);
			hostlist_destroy(new_hl);
			thread_info[threads_used].nodelist = xstrdup(buf);
			slurm_msg_t_init(&thread_info[threads_used].msg);
			thread_info[threads_used].msg.msg_type =
				REQUEST_FILE_BCAST;
			threads_used++;
		}
		xfree(span);
		hostlist_destroy(hl);
		debug("using %d threads", threads_used);
	}

	slurm_attr_init(&attr);
	if (pthread_attr_setstacksize(&attr, 3 * 1024*1024))
		error("pthread_attr_setstacksize: %m");
	if (pthread_attr_setdetachstate (&attr,
			PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	for (i=0; i<threads_used; i++) {
		thread_info[i].msg.data = bcast_msg;
		slurm_mutex_lock(&agent_cnt_mutex);
		agent_cnt++;
		slurm_mutex_unlock(&agent_cnt_mutex);

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
