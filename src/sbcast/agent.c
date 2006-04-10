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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/sbcast/sbcast.h"

#define AGENT_THREAD_COUNT 10
typedef enum {
	DSH_NEW,	/* Request not yet started */
	DSH_ACTIVE,	/* Request in progress */
	DSH_DONE,	/* Request completed normally */
	DSH_NO_RESP,	/* Request timed out */
	DSH_FAILED	/* Request resulted in error */
} state_t;

typedef struct thd {
	pthread_t thread;		/* thread ID */
	pthread_attr_t attr;		/* thread attributes */
	state_t state;			/* thread state */
	time_t start_time;		/* start time */
	time_t end_time;		/* end time or delta time
					 * upon termination */
	struct sockaddr_in slurm_addr;	/* structure holding info for all
					 * forwarding info */
	char node_name[MAX_SLURM_NAME];	/* node's name */
	List ret_list;
} thd_t;

/* issue the RPC to ship the file's data */
extern void send_rpc(file_bcast_msg_t *bcast_msg, 
		resource_allocation_response_msg_t *alloc_resp)
{
	/* This code will handle message fanout to multiple slurmd */
	forward_t from, forward;
	slurm_msg_t msg;
	hostlist_t hl;
	List ret_list = NULL;
	ListIterator itr, data_itr;
	ret_types_t *ret_type = NULL;
	ret_data_info_t *ret_data_info = NULL;
	int i, rc = SLURM_SUCCESS;

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
	i = 0;
	forward_set(&forward, alloc_resp->node_cnt, &i, &from);

	msg.msg_type	= REQUEST_FILE_BCAST;
	msg.address	= alloc_resp->node_addr[0];
	msg.data	= bcast_msg;
	msg.forward	= forward;
	msg.ret_list	= NULL;
	msg.orig_addr.sin_addr.s_addr = 0;
	msg.srun_node_id = 0;

	ret_list = slurm_send_recv_rc_msg(&msg, 
			SLURM_MESSAGE_TIMEOUT_MSEC_STATIC);
	if (ret_list == NULL) {
		error("slurm_send_recv_rc_msg: %m");
		exit(1);
	}

	itr = list_iterator_create(ret_list);
	while ((ret_type = list_next(itr)) != NULL) {
		data_itr = list_iterator_create(ret_type->ret_data_list);
		while((ret_data_info = list_next(data_itr)) != NULL) {
			i = ret_type->msg_rc;
			if (i != SLURM_SUCCESS) {
				if (!strcmp(ret_data_info->node_name,
						"localhost")) {
					xfree(ret_data_info->node_name);
					ret_data_info->node_name =
						xstrdup(from.name);
				}
				error("REQUEST_FILE_BCAST(%s): %s",
					ret_data_info->node_name,
					slurm_strerror(i));
				rc = i;
			}
		}
		list_iterator_destroy(data_itr);
	}
	list_iterator_destroy(itr);
	if (ret_list)
		list_destroy(ret_list);
	if (rc)
		exit(1);
}
