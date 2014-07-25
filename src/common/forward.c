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
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "slurm/slurm.h"

#include "src/common/forward.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_route.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_interface.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#define MAX_RETRIES 3

typedef struct {
	pthread_cond_t *notify;
	int            *p_thr_count;
	slurm_msg_t *orig_msg;
	List ret_list;
	int timeout;
	hostlist_t tree_hl;
	pthread_mutex_t *tree_mutex;
} fwd_tree_t;

void _destroy_tree_fwd(fwd_tree_t *fwd_tree)
{
	if (fwd_tree) {
		if (fwd_tree->tree_hl)
			hostlist_destroy(fwd_tree->tree_hl);

		/*
		 * Lock and decrease thread counter, start_msg_tree is waiting
		 * for a null thread count to exit its main loop
		 */
		slurm_mutex_lock(fwd_tree->tree_mutex);
		(*(fwd_tree->p_thr_count))--;
		pthread_cond_signal(fwd_tree->notify);
		slurm_mutex_unlock(fwd_tree->tree_mutex);

		xfree(fwd_tree);
	}
}

void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = (forward_msg_t *)arg;
	Buf buffer = init_buf(BUF_SIZE);	/* probably enough for header */
	List ret_list = NULL;
	slurm_fd_t fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	char *name = NULL;
	hostlist_t hl = hostlist_create(fwd_msg->header.forward.nodelist);
	slurm_addr_t addr;
	char *buf = NULL;
	int steps = 0;
	int start_timeout = fwd_msg->timeout;

	/* repeat until we are sure the message was sent */
	while ((name = hostlist_shift(hl))) {
		if (slurm_conf_get_addr(name, &addr) == SLURM_ERROR) {
			error("forward_thread: can't find address for host "
			      "%s, check slurm.conf", name);
			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name,
					       SLURM_UNKNOWN_FORWARD_ADDR);
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
			mark_as_failed_forward(
				&fwd_msg->ret_list, name,
				SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			free(name);
			if (hostlist_count(hl) > 0) {
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				continue;
			}
			goto cleanup;
		}
		buf = hostlist_ranged_string_xmalloc(hl);

		xfree(fwd_msg->header.forward.nodelist);
		fwd_msg->header.forward.nodelist = buf;
		fwd_msg->header.forward.cnt = hostlist_count(hl);
#if 0
		info("sending %d forwards (%s) to %s",
		     fwd_msg->header.forward.cnt,
		     fwd_msg->header.forward.nodelist, name);
#endif
		if (fwd_msg->header.forward.nodelist[0]) {
			debug3("forward: send to %s along with %s",
			       name, fwd_msg->header.forward.nodelist);
		} else
			debug3("forward: send to %s ", name);

		pack_header(&fwd_msg->header, buffer);

		/* add forward data to buffer */
		if (remaining_buf(buffer) < fwd_msg->buf_len) {
			int new_size = buffer->processed + fwd_msg->buf_len;
			new_size += 1024; /* padded for paranoia */
			xrealloc_nz(buffer->head, new_size);
			buffer->size = new_size;
		}
		if (fwd_msg->buf_len) {
			memcpy(&buffer->head[buffer->processed],
			       fwd_msg->buf, fwd_msg->buf_len);
			buffer->processed += fwd_msg->buf_len;
		}

		/*
		 * forward message
		 */
		if (_slurm_msg_sendto(fd,
				     get_buf_data(buffer),
				     get_buf_offset(buffer),
				     SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) < 0) {
			error("forward_thread: slurm_msg_sendto: %m");

			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name,
					       errno);
			free(name);
			if (hostlist_count(hl) > 0) {
				free_buf(buffer);
				buffer = init_buf(fwd_msg->buf_len);
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				slurm_close_accepted_conn(fd);
				fd = -1;
				continue;
			}
			goto cleanup;
		}

		if ((fwd_msg->header.msg_type == REQUEST_SHUTDOWN) ||
		    (fwd_msg->header.msg_type == REQUEST_RECONFIGURE) ||
		    (fwd_msg->header.msg_type == REQUEST_REBOOT_NODES)) {
			slurm_mutex_lock(fwd_msg->forward_mutex);
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			list_push(fwd_msg->ret_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			free(name);
			while ((name = hostlist_shift(hl))) {
				ret_data_info =
					xmalloc(sizeof(ret_data_info_t));
				list_push(fwd_msg->ret_list, ret_data_info);
				ret_data_info->node_name = xstrdup(name);
				free(name);
			}
			goto cleanup;
		}

		if (fwd_msg->header.forward.cnt > 0) {
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

		if (!ret_list || (fwd_msg->header.forward.cnt != 0
				 && list_count(ret_list) <= 1)) {
			slurm_mutex_lock(fwd_msg->forward_mutex);
			mark_as_failed_forward(&fwd_msg->ret_list, name,
					       errno);
			free(name);
			if (ret_list)
				list_destroy(ret_list);
			if (hostlist_count(hl) > 0) {
				free_buf(buffer);
				buffer = init_buf(fwd_msg->buf_len);
				slurm_mutex_unlock(fwd_msg->forward_mutex);
				slurm_close_accepted_conn(fd);
				fd = -1;
				continue;
			}
			goto cleanup;
		} else if ((fwd_msg->header.forward.cnt+1)
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
			while ((tmp = hostlist_next(host_itr))) {
				int node_found = 0;
				itr = list_iterator_create(ret_list);
				while ((ret_data_info = list_next(itr))) {
					if (!ret_data_info->node_name) {
						first_node_found = 1;
						ret_data_info->node_name =
							xstrdup(name);
					}
					if (!strcmp(tmp,
						   ret_data_info->node_name)) {
						node_found = 1;
						break;
					}
				}
				list_iterator_destroy(itr);
				if (!node_found) {
					mark_as_failed_forward(
						&fwd_msg->ret_list,
						tmp,
						SLURM_COMMUNICATIONS_CONNECTION_ERROR);
				}
				free(tmp);
			}
			hostlist_iterator_destroy(host_itr);
			if (!first_node_found) {
				mark_as_failed_forward(&fwd_msg->ret_list,
						       name,
						       SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			}
		}
		break;
	}
	slurm_mutex_lock(fwd_msg->forward_mutex);
	if (ret_list) {
		while ((ret_data_info = list_pop(ret_list)) != NULL) {
			if (!ret_data_info->node_name) {
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

void *_fwd_tree_thread(void *arg)
{
	fwd_tree_t *fwd_tree = (fwd_tree_t *)arg;
	List ret_list = NULL;
	char *name = NULL;
	char *buf = NULL;
	slurm_msg_t send_msg;

	slurm_msg_t_init(&send_msg);
	send_msg.msg_type = fwd_tree->orig_msg->msg_type;
	send_msg.data = fwd_tree->orig_msg->data;
	send_msg.protocol_version = fwd_tree->orig_msg->protocol_version;

	/* repeat until we are sure the message was sent */
	while ((name = hostlist_shift(fwd_tree->tree_hl))) {
		if (slurm_conf_get_addr(name, &send_msg.address)
		    == SLURM_ERROR) {
			error("fwd_tree_thread: can't find address for host "
			      "%s, check slurm.conf", name);
			slurm_mutex_lock(fwd_tree->tree_mutex);
			mark_as_failed_forward(&fwd_tree->ret_list, name,
					       SLURM_UNKNOWN_FORWARD_ADDR);
 			pthread_cond_signal(fwd_tree->notify);
			slurm_mutex_unlock(fwd_tree->tree_mutex);
			free(name);

			continue;
		}

		send_msg.forward.timeout = fwd_tree->timeout;
		if ((send_msg.forward.cnt = hostlist_count(fwd_tree->tree_hl))){
			buf = hostlist_ranged_string_xmalloc(
					fwd_tree->tree_hl);
			send_msg.forward.nodelist = buf;
		} else
			send_msg.forward.nodelist = NULL;

		if (send_msg.forward.nodelist && send_msg.forward.nodelist[0]) {
			debug3("Tree sending to %s along with %s",
			       name, send_msg.forward.nodelist);
		} else
			debug3("Tree sending to %s", name);

		ret_list = slurm_send_addr_recv_msgs(&send_msg, name,
						     fwd_tree->timeout);

		xfree(send_msg.forward.nodelist);

		if (ret_list) {
			int ret_cnt = list_count(ret_list);
			/* This is most common if a slurmd is running
			   an older version of Slurm than the
			   originator of the message.
			*/
			if ((ret_cnt <= send_msg.forward.cnt) &&
			    (errno != SLURM_COMMUNICATIONS_CONNECTION_ERROR)) {
				error("fwd_tree_thread: %s failed to forward "
				      "the message, expecting %d ret got only "
				      "%d",
				      name, send_msg.forward.cnt + 1, ret_cnt);
				if (ret_cnt > 1) { /* not likely */
					ret_data_info_t *ret_data_info = NULL;
					ListIterator itr =
						list_iterator_create(ret_list);
					while ((ret_data_info =
						list_next(itr))) {
						if (strcmp(ret_data_info->
							   node_name, name))
							hostlist_delete_host(
								fwd_tree->
								tree_hl,
								ret_data_info->
								node_name);
					}
					list_iterator_destroy(itr);
				}
			}

			slurm_mutex_lock(fwd_tree->tree_mutex);
			list_transfer(fwd_tree->ret_list, ret_list);
			pthread_cond_signal(fwd_tree->notify);
			slurm_mutex_unlock(fwd_tree->tree_mutex);
			list_destroy(ret_list);
			/* try next node */
			if (ret_cnt <= send_msg.forward.cnt) {
				free(name);
				continue;
			}
		} else {
			/* This should never happen (when this was
			 * written slurm_send_addr_recv_msgs always
			 * returned a list */
			error("fwd_tree_thread: no return list given from "
			      "slurm_send_addr_recv_msgs spawned for %s",
			      name);
			slurm_mutex_lock(fwd_tree->tree_mutex);
			mark_as_failed_forward(
				&fwd_tree->ret_list, name,
				SLURM_COMMUNICATIONS_CONNECTION_ERROR);
 			pthread_cond_signal(fwd_tree->notify);
			slurm_mutex_unlock(fwd_tree->tree_mutex);
			free(name);

			continue;
		}

		free(name);

		/* check for error and try again */
		if (errno == SLURM_COMMUNICATIONS_CONNECTION_ERROR)
 			continue;

		break;
	}

	_destroy_tree_fwd(fwd_tree);

	return NULL;
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
	if (from && from->init == FORWARD_INIT) {
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
 *                                             children processes
 * IN: header         - header_t             - header from message that came in
 *                                             needing to be forwarded.
 * RET: SLURM_SUCCESS - int
 */
extern int forward_msg(forward_struct_t *forward_struct,
		       header_t *header)
{
	int j = 0;
	int retries = 0;
	forward_msg_t *forward_msg = NULL;
	int thr_count = 0;
	hostlist_t hl = NULL;
	hostlist_t* sp_hl;
	int hl_count = 0;

	if (!forward_struct->ret_list) {
		error("didn't get a ret_list from forward_struct");
		return SLURM_ERROR;
	}
	hl = hostlist_create(header->forward.nodelist);
	hostlist_uniq(hl);

	if ( route_g_split_hostlist(hl, &sp_hl, &hl_count) ) {
		error("unable to split forward hostlist");
		hostlist_destroy(hl);
		return SLURM_ERROR;
	}
	for (j = 0; j < hl_count; j++) {

		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		char *buf = NULL;

		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
		    (&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");

		forward_msg = &forward_struct->forward_msg[thr_count];
		forward_msg->ret_list = forward_struct->ret_list;

		forward_msg->timeout = forward_struct->timeout;

		if (forward_msg->timeout <= 0) {
			/* convert secs to msec */
			forward_msg->timeout  = slurm_get_msg_timeout() * 1000;
		}

		forward_msg->notify = &forward_struct->notify;
		forward_msg->forward_mutex = &forward_struct->forward_mutex;
		forward_msg->buf_len = forward_struct->buf_len;
		forward_msg->buf = forward_struct->buf;

		memcpy(&forward_msg->header.orig_addr,
		       &header->orig_addr,
		       sizeof(slurm_addr_t));

		forward_msg->header.version = header->version;
		forward_msg->header.flags = header->flags;
		forward_msg->header.msg_type = header->msg_type;
		forward_msg->header.body_length = header->body_length;
		forward_msg->header.ret_list = NULL;
		forward_msg->header.ret_cnt = 0;

		buf = hostlist_ranged_string_xmalloc(sp_hl[j]);
		hostlist_destroy(sp_hl[j]);
		forward_init(&forward_msg->header.forward, NULL);
		forward_msg->header.forward.nodelist = buf;
		while (pthread_create(&thread_agent, &attr_agent,
				     _forward_thread,
				     (void *)forward_msg)) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			usleep(100000);	/* sleep and try again */
		}
		slurm_attr_destroy(&attr_agent);
		thr_count++;
	}
	xfree(sp_hl);
	hostlist_destroy(hl);
	return SLURM_SUCCESS;
}

/*
 * start_msg_tree  - logic to begin the forward tree and
 *                   accumulate the return codes from processes getting the
 *                   the forwarded message
 *
 * IN: hl          - hostlist_t   - list of every node to send message to
 * IN: msg         - slurm_msg_t  - message to send.
 * IN: timeout     - int          - how long to wait in milliseconds.
 * RET List 	   - List containing the responses of the children
 *		     (if any) we forwarded the message to. List
 *		     containing type (ret_data_info_t).
 */
extern List start_msg_tree(hostlist_t hl, slurm_msg_t *msg, int timeout)
{
	fwd_tree_t *fwd_tree = NULL;
	pthread_mutex_t tree_mutex;
	pthread_cond_t notify;
	int j = 0, count = 0;
	List ret_list = NULL;
	int thr_count = 0;
	int host_count = 0;
	hostlist_t* sp_hl;
	int hl_count = 0;

	xassert(hl);
	xassert(msg);

	hostlist_uniq(hl);
	host_count = hostlist_count(hl);

	if ( route_g_split_hostlist(hl, &sp_hl, &hl_count) ) {
		error("unable to split forward hostlist");
		return NULL;
	}
	slurm_mutex_init(&tree_mutex);
	pthread_cond_init(&notify, NULL);

	ret_list = list_create(destroy_data_info);

	for (j = 0; j < hl_count; j++) {
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		int retries = 0;

		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
		    (&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");

		fwd_tree = xmalloc(sizeof(fwd_tree_t));
		fwd_tree->orig_msg = msg;
		fwd_tree->ret_list = ret_list;
		fwd_tree->timeout = timeout;
		fwd_tree->notify = &notify;
		fwd_tree->p_thr_count = &thr_count;
		fwd_tree->tree_mutex = &tree_mutex;

		if (fwd_tree->timeout <= 0) {
			/* convert secs to msec */
			fwd_tree->timeout  = slurm_get_msg_timeout() * 1000;
		}

		fwd_tree->tree_hl = sp_hl[j];
		sp_hl[j] = NULL;

		/*
		 * Lock and increase thread counter, we need that to protect
		 * the start_msg_tree waiting loop that was originally designed
		 * around a "while ((count < host_count))" loop. In case where a
		 * fwd thread was not able to get all the return codes from
		 * children, the waiting loop was deadlocked.
		 */
		slurm_mutex_lock(&tree_mutex);
		thr_count++;
		slurm_mutex_unlock(&tree_mutex);

		while (pthread_create(&thread_agent, &attr_agent,
				      _fwd_tree_thread, (void *)fwd_tree)) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			usleep(100000);	/* sleep and try again */
		}
		slurm_attr_destroy(&attr_agent);

	}
	xfree(sp_hl);

	slurm_mutex_lock(&tree_mutex);

	count = list_count(ret_list);
	debug2("Tree head got back %d looking for %d", count, host_count);
	while (thr_count > 0) {
		pthread_cond_wait(&notify, &tree_mutex);
		count = list_count(ret_list);
		debug2("Tree head got back %d", count);
	}
	xassert(count >= host_count);	/* Tree head did not get all responses,
					 * but no more active fwd threads!*/
	slurm_mutex_unlock(&tree_mutex);

	slurm_mutex_destroy(&tree_mutex);
	pthread_cond_destroy(&notify);

	return ret_list;
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
	if (!*ret_list)
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
	if (msg->forward_struct) {
		debug2("looking for %d", msg->forward_struct->fwd_cnt);
		slurm_mutex_lock(&msg->forward_struct->forward_mutex);
		count = 0;
		if (msg->ret_list != NULL)
			count = list_count(msg->ret_list);

		debug2("Got back %d", count);
		while ((count < msg->forward_struct->fwd_cnt)) {
			pthread_cond_wait(&msg->forward_struct->notify,
					  &msg->forward_struct->forward_mutex);

			if (msg->ret_list != NULL) {
				count = list_count(msg->ret_list);
			}
			debug2("Got back %d", count);

		}
		debug2("Got them all");
		slurm_mutex_unlock(&msg->forward_struct->forward_mutex);
		destroy_forward_struct(msg->forward_struct);
		msg->forward_struct = NULL;
	}
	return;
}

void destroy_data_info(void *object)
{
	ret_data_info_t *ret_data_info = (ret_data_info_t *)object;
	if (ret_data_info) {
		slurm_free_msg_data(ret_data_info->type,
				    ret_data_info->data);
		xfree(ret_data_info->node_name);
		xfree(ret_data_info);
	}
}

void destroy_forward(forward_t *forward)
{
	if (forward->init == FORWARD_INIT) {
		xfree(forward->nodelist);
		forward->init = 0;
	} else {
		error("destroy_forward: no init");
	}
}

void destroy_forward_struct(forward_struct_t *forward_struct)
{
	if (forward_struct) {
		xfree(forward_struct->buf);
		xfree(forward_struct->forward_msg);
		slurm_mutex_destroy(&forward_struct->forward_mutex);
		pthread_cond_destroy(&forward_struct->notify);
		xfree(forward_struct);
	}
}
