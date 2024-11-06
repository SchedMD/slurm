/*****************************************************************************\
 *  forward.c - forward RPCs through hierarchical slurmd communications
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/forward.h"
#include "src/common/macros.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/topology.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static slurm_node_alias_addrs_t *last_alias_addrs = NULL;
static pthread_mutex_t alias_addrs_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	pthread_cond_t *notify;
	int            *p_thr_count;
	slurm_msg_t *orig_msg;
	list_t *ret_list;
	int timeout;
	int tree_depth;
	hostlist_t *tree_hl;
	pthread_mutex_t *tree_mutex;
} fwd_tree_t;

static void _start_msg_tree_internal(hostlist_t *hl, hostlist_t **sp_hl,
				     fwd_tree_t *fwd_tree_in,
				     int hl_count);
static void _forward_msg_internal(hostlist_t *hl, hostlist_t **sp_hl,
				  forward_struct_t *fwd_struct,
				  header_t *header, int timeout,
				  int hl_count);

void _destroy_tree_fwd(fwd_tree_t *fwd_tree)
{
	if (fwd_tree) {
		FREE_NULL_HOSTLIST(fwd_tree->tree_hl);

		/*
		 * Lock and decrease thread counter, start_msg_tree is waiting
		 * for a null thread count to exit its main loop
		 */
		slurm_mutex_lock(fwd_tree->tree_mutex);
		(*(fwd_tree->p_thr_count))--;
		slurm_cond_signal(fwd_tree->notify);
		slurm_mutex_unlock(fwd_tree->tree_mutex);

		xfree(fwd_tree);
	}
}

static int _forward_get_addr(forward_struct_t *fwd_struct, char *name,
			     slurm_addr_t *address)
{
	hostlist_t *hl = hostlist_create(fwd_struct->alias_addrs->node_list);
	int n = hostlist_find(hl, name);
	hostlist_destroy(hl);
	if (n < 0)
		return SLURM_ERROR;
	*address = fwd_struct->alias_addrs->node_addrs[n];

	return SLURM_SUCCESS;
}

static void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = arg;
	forward_struct_t *fwd_struct = fwd_msg->fwd_struct;
	forward_t *fwd_ptr = &fwd_msg->header.forward;
	buf_t *buffer = init_buf(BUF_SIZE);	/* probably enough for header */
	list_t *ret_list = NULL;
	int fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	char *name = NULL;
	hostlist_t *hl = hostlist_create(fwd_ptr->nodelist);
	slurm_addr_t addr;
	char *buf = NULL;

	/* repeat until we are sure the message was sent */
	while ((name = hostlist_shift(hl))) {
		if ((!(fwd_msg->header.flags & SLURM_PACK_ADDRS) ||
		     _forward_get_addr(fwd_struct, name, &addr)) &&
		    slurm_conf_get_addr(name, &addr, fwd_msg->header.flags)) {
			error("%s: can't find address for host %s, check slurm.conf",
			      __func__, name);
			slurm_mutex_lock(&fwd_struct->forward_mutex);
			mark_as_failed_forward(&fwd_struct->ret_list, name,
					       SLURM_UNKNOWN_FORWARD_ADDR);
 			free(name);
			if (hostlist_count(hl) > 0) {
				slurm_mutex_unlock(&fwd_struct->forward_mutex);
				continue;
			}
			goto cleanup;
		}
		if ((fd = slurm_open_msg_conn(&addr)) < 0) {
			error("%s: failed to %s (%pA): %m",
			      __func__, name, &addr);

			slurm_mutex_lock(&fwd_struct->forward_mutex);
			mark_as_failed_forward(
				&fwd_struct->ret_list, name,
				SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			free(name);
			if (hostlist_count(hl) > 0) {
				slurm_mutex_unlock(&fwd_struct->forward_mutex);
				/* Abandon tree. This way if all the
				 * nodes in the branch are down we
				 * don't have to time out for each
				 * node serially.
				 */
				_forward_msg_internal(hl, NULL, fwd_struct,
						      &fwd_msg->header, 0,
						      hostlist_count(hl));
				continue;
			}
			goto cleanup;
		}
		buf = hostlist_ranged_string_xmalloc(hl);

		xfree(fwd_ptr->nodelist);
		fwd_ptr->nodelist = buf;
		fwd_ptr->cnt = hostlist_count(hl);

		if (fwd_msg->header.flags & SLURM_PACK_ADDRS)
			fwd_ptr->alias_addrs = *(fwd_struct->alias_addrs);

#if 0
		info("sending %d forwards (%s) to %s",
		     fwd_ptr->cnt, fwd_ptr->nodelist, name);
#endif
		if (fwd_ptr->nodelist[0]) {
			debug3("forward: send to %s along with %s",
			       name, fwd_ptr->nodelist);
		} else
			debug3("forward: send to %s ", name);

		pack_header(&fwd_msg->header, buffer);

		/* add forward data to buffer */
		if (remaining_buf(buffer) < fwd_struct->buf_len) {
			int new_size = buffer->processed + fwd_struct->buf_len;
			new_size += 1024; /* padded for paranoia */
			xrealloc_nz(buffer->head, new_size);
			buffer->size = new_size;
		}
		if (fwd_struct->buf_len) {
			memcpy(&buffer->head[buffer->processed],
			       fwd_struct->buf, fwd_struct->buf_len);
			buffer->processed += fwd_struct->buf_len;
		}

		/*
		 * forward message
		 */
		if (slurm_msg_sendto(fd,
				     get_buf_data(buffer),
				     get_buf_offset(buffer)) < 0) {
			error("%s: slurm_msg_sendto: %m", __func__);

			slurm_mutex_lock(&fwd_struct->forward_mutex);
			mark_as_failed_forward(&fwd_struct->ret_list, name,
					       errno);
			free(name);
			if (hostlist_count(hl) > 0) {
				FREE_NULL_BUFFER(buffer);
				buffer = init_buf(fwd_struct->buf_len);
				slurm_mutex_unlock(&fwd_struct->forward_mutex);
				close(fd);
				fd = -1;
				/* Abandon tree. This way if all the
				 * nodes in the branch are down we
				 * don't have to time out for each
				 * node serially.
				 */
				_forward_msg_internal(hl, NULL, fwd_struct,
						      &fwd_msg->header, 0,
						      hostlist_count(hl));
				continue;
			}
			goto cleanup;
		}

		/* These messages don't have a return message, but if
		 * we got here things worked out so make note of the
		 * list of nodes as success.
		 */
		if ((fwd_msg->header.msg_type == REQUEST_SHUTDOWN) ||
		    (fwd_msg->header.msg_type == REQUEST_RECONFIGURE) ||
		    (fwd_msg->header.msg_type == REQUEST_REBOOT_NODES)) {
			slurm_mutex_lock(&fwd_struct->forward_mutex);
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			list_push(fwd_struct->ret_list, ret_data_info);
			ret_data_info->node_name = xstrdup(name);
			free(name);
			while ((name = hostlist_shift(hl))) {
				ret_data_info =
					xmalloc(sizeof(ret_data_info_t));
				list_push(fwd_struct->ret_list, ret_data_info);
				ret_data_info->node_name = xstrdup(name);
				free(name);
			}
			goto cleanup;
		}

		ret_list = slurm_receive_resp_msgs(fd, fwd_ptr->tree_depth,
						   fwd_ptr->timeout);
		/* info("sent %d forwards got %d back", */
		/*      fwd_ptr->cnt, list_count(ret_list)); */

		if (!ret_list || (fwd_ptr->cnt && list_count(ret_list) <= 1)) {
			slurm_mutex_lock(&fwd_struct->forward_mutex);
			mark_as_failed_forward(&fwd_struct->ret_list, name,
					       errno);
			free(name);
			FREE_NULL_LIST(ret_list);
			if (hostlist_count(hl) > 0) {
				FREE_NULL_BUFFER(buffer);
				buffer = init_buf(fwd_struct->buf_len);
				slurm_mutex_unlock(&fwd_struct->forward_mutex);
				close(fd);
				fd = -1;
				continue;
			}
			goto cleanup;
		} else if ((fwd_ptr->cnt + 1) != list_count(ret_list)) {
			/* this should never be called since the above
			   should catch the failed forwards and pipe
			   them back down, but this is here so we
			   never have to worry about a locked
			   mutex */
			list_itr_t *itr = NULL;
			char *tmp = NULL;
			int first_node_found = 0;
			hostlist_iterator_t *host_itr
				= hostlist_iterator_create(hl);
			error("We shouldn't be here.  We forwarded to %d but only got %d back",
			      (fwd_ptr->cnt + 1), list_count(ret_list));
			while ((tmp = hostlist_next(host_itr))) {
				int node_found = 0;
				itr = list_iterator_create(ret_list);
				while ((ret_data_info = list_next(itr))) {
					if (!ret_data_info->node_name) {
						first_node_found = 1;
						ret_data_info->node_name =
							xstrdup(name);
					}
					if (!xstrcmp(tmp,
						   ret_data_info->node_name)) {
						node_found = 1;
						break;
					}
				}
				list_iterator_destroy(itr);
				if (!node_found) {
					slurm_mutex_lock(&fwd_struct->forward_mutex);
					mark_as_failed_forward(
						&fwd_struct->ret_list,
						tmp,
						SLURM_COMMUNICATIONS_CONNECTION_ERROR);
					slurm_mutex_unlock(&fwd_struct->forward_mutex);
				}
				free(tmp);
			}
			hostlist_iterator_destroy(host_itr);
			if (!first_node_found) {
				slurm_mutex_lock(&fwd_struct->forward_mutex);
				mark_as_failed_forward(
					&fwd_struct->ret_list,
					name,
					SLURM_COMMUNICATIONS_CONNECTION_ERROR);
				slurm_mutex_unlock(&fwd_struct->forward_mutex);
			}
		}
		break;
	}
	slurm_mutex_lock(&fwd_struct->forward_mutex);
	if (ret_list) {
		while ((ret_data_info = list_pop(ret_list)) != NULL) {
			if (!ret_data_info->node_name) {
				ret_data_info->node_name = xstrdup(name);
			}
			list_push(fwd_struct->ret_list, ret_data_info);
			debug3("got response from %s",
			       ret_data_info->node_name);
		}
		FREE_NULL_LIST(ret_list);
	}
	free(name);
cleanup:
	if ((fd >= 0) && close(fd) < 0)
		error ("close(%d): %m", fd);
	hostlist_destroy(hl);
	fwd_ptr->alias_addrs.net_cred = NULL;
	fwd_ptr->alias_addrs.node_addrs = NULL;
	fwd_ptr->alias_addrs.node_list = NULL;
	destroy_forward(fwd_ptr);
	FREE_NULL_BUFFER(buffer);
	slurm_cond_signal(&fwd_struct->notify);
	slurm_mutex_unlock(&fwd_struct->forward_mutex);
	xfree(fwd_msg);

	return (NULL);
}

static int _fwd_tree_get_addr(fwd_tree_t *fwd_tree, char *name,
			      slurm_addr_t *address)
{
	if ((fwd_tree->orig_msg->flags & SLURM_PACK_ADDRS) &&
	    fwd_tree->orig_msg->forward.alias_addrs.node_addrs) {
		hostlist_t *hl =
			hostlist_create(fwd_tree->orig_msg->forward.alias_addrs.node_list);
		int n = hostlist_find(hl, name);
		hostlist_destroy(hl);
		if (n < 0)
			return SLURM_ERROR;
		*address =
			fwd_tree->orig_msg->forward.alias_addrs.node_addrs[n];
	} else if (slurm_conf_get_addr(name, address,
				       fwd_tree->orig_msg->flags) ==
		   SLURM_ERROR) {
		error("%s: can't find address for host %s, check slurm.conf",
		      __func__, name);
		slurm_mutex_lock(fwd_tree->tree_mutex);
		mark_as_failed_forward(&fwd_tree->ret_list, name,
				       SLURM_UNKNOWN_FORWARD_ADDR);
		slurm_cond_signal(fwd_tree->notify);
		slurm_mutex_unlock(fwd_tree->tree_mutex);

		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void *_fwd_tree_thread(void *arg)
{
	fwd_tree_t *fwd_tree = arg;
	list_t *ret_list = NULL;
	char *name = NULL;
	char *buf = NULL;
	slurm_msg_t send_msg;

	slurm_msg_t_init(&send_msg);
	send_msg.msg_type = fwd_tree->orig_msg->msg_type;
	send_msg.flags = fwd_tree->orig_msg->flags;
	send_msg.data = fwd_tree->orig_msg->data;
	send_msg.protocol_version = fwd_tree->orig_msg->protocol_version;
	if (fwd_tree->orig_msg->restrict_uid_set)
		slurm_msg_set_r_uid(&send_msg,
				    fwd_tree->orig_msg->restrict_uid);

	/* repeat until we are sure the message was sent */
	while ((name = hostlist_shift(fwd_tree->tree_hl))) {
		if (_fwd_tree_get_addr(fwd_tree, name, &send_msg.address)) {
			free(name);

			continue;
		}

		/*
		 * Tell additional message forwarding to use the same
		 * tree_width; without this, additional message forwarding
		 * defaults to slurm_conf.tree_width (see _forward_thread).
		 */
		send_msg.forward.tree_width =
			fwd_tree->orig_msg->forward.tree_width;
		send_msg.forward.tree_depth = fwd_tree->tree_depth;
		send_msg.forward.timeout = fwd_tree->timeout;
		if ((send_msg.forward.cnt = hostlist_count(fwd_tree->tree_hl))){
			buf = hostlist_ranged_string_xmalloc(
					fwd_tree->tree_hl);
			send_msg.forward.nodelist = buf;
			if (send_msg.flags & SLURM_PACK_ADDRS) {
				send_msg.forward.alias_addrs =
					fwd_tree->orig_msg->forward.alias_addrs;
			}
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
				error("%s: %s failed to forward the message, expecting %d ret got only %d",
				      __func__, name, send_msg.forward.cnt + 1,
				      ret_cnt);
				if (ret_cnt > 1) { /* not likely */
					ret_data_info_t *ret_data_info = NULL;
					list_itr_t *itr =
						list_iterator_create(ret_list);
					while ((ret_data_info =
						list_next(itr))) {
						if (xstrcmp(ret_data_info->
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
			slurm_cond_signal(fwd_tree->notify);
			slurm_mutex_unlock(fwd_tree->tree_mutex);
			FREE_NULL_LIST(ret_list);
			/* try next node */
			if (ret_cnt <= send_msg.forward.cnt) {
				free(name);
				/* Abandon tree. This way if all the
				 * nodes in the branch are down we
				 * don't have to time out for each
				 * node serially.
				 */
				_start_msg_tree_internal(
					fwd_tree->tree_hl, NULL,
					fwd_tree,
					hostlist_count(fwd_tree->tree_hl));
				continue;
			}
		} else {
			/* This should never happen (when this was
			 * written slurm_send_addr_recv_msgs always
			 * returned a list */
			error("%s: no return list given from slurm_send_addr_recv_msgs spawned for %s",
			      __func__, name);
			slurm_mutex_lock(fwd_tree->tree_mutex);
			mark_as_failed_forward(
				&fwd_tree->ret_list, name,
				SLURM_COMMUNICATIONS_CONNECTION_ERROR);
 			slurm_cond_signal(fwd_tree->notify);
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

static void _start_msg_tree_internal(hostlist_t *hl, hostlist_t **sp_hl,
				     fwd_tree_t *fwd_tree_in,
				     int hl_count)
{
	int j;
	fwd_tree_t *fwd_tree;

	xassert((hl || sp_hl) && !(hl && sp_hl));
	xassert(fwd_tree_in);
	xassert(fwd_tree_in->p_thr_count);
	xassert(fwd_tree_in->tree_mutex);
	xassert(fwd_tree_in->notify);
	xassert(fwd_tree_in->ret_list);

	if (hl)
		xassert(hl_count == hostlist_count(hl));

	if (fwd_tree_in->timeout <= 0)
		/* convert secs to msec */
		fwd_tree_in->timeout = slurm_conf.msg_timeout * 1000;

	for (j = 0; j < hl_count; j++) {
		fwd_tree = xmalloc(sizeof(fwd_tree_t));
		memcpy(fwd_tree, fwd_tree_in, sizeof(fwd_tree_t));

		if (sp_hl) {
			fwd_tree->tree_hl = sp_hl[j];
			sp_hl[j] = NULL;
		} else if (hl) {
			char *name = hostlist_shift(hl);
			fwd_tree->tree_hl = hostlist_create(name);
			free(name);
		}

		/*
		 * Lock and increase thread counter, we need that to protect
		 * the start_msg_tree waiting loop that was originally designed
		 * around a "while ((count < host_count))" loop. In case where a
		 * fwd thread was not able to get all the return codes from
		 * children, the waiting loop was deadlocked.
		 */
		slurm_mutex_lock(fwd_tree->tree_mutex);
		(*fwd_tree->p_thr_count)++;
		slurm_mutex_unlock(fwd_tree->tree_mutex);

		slurm_thread_create_detached(_fwd_tree_thread, fwd_tree);
	}
}

static void _forward_msg_internal(hostlist_t *hl, hostlist_t **sp_hl,
				  forward_struct_t *fwd_struct,
				  header_t *header, int timeout,
				  int hl_count)
{
	int j;
	forward_msg_t *fwd_msg = NULL;
	char *buf = NULL, *tmp_char = NULL;

	if (timeout <= 0)
		/* convert secs to msec */
		timeout = slurm_conf.msg_timeout * 1000;

	for (j = 0; j < hl_count; j++) {
		fwd_msg = xmalloc(sizeof(forward_msg_t));

		fwd_msg->fwd_struct = fwd_struct;

		fwd_msg->timeout = timeout;

		memcpy(&fwd_msg->header.orig_addr,
		       &header->orig_addr,
		       sizeof(slurm_addr_t));

		fwd_msg->header.version = header->version;
		fwd_msg->header.flags = header->flags;
		fwd_msg->header.msg_type = header->msg_type;
		fwd_msg->header.body_length = header->body_length;
		fwd_msg->header.ret_list = NULL;
		fwd_msg->header.ret_cnt = 0;

		if (sp_hl) {
			buf = hostlist_ranged_string_xmalloc(sp_hl[j]);
			hostlist_destroy(sp_hl[j]);
		} else {
			tmp_char = hostlist_shift(hl);
			buf = xstrdup(tmp_char);
			free(tmp_char);
		}

		forward_init(&fwd_msg->header.forward);
		fwd_msg->header.forward.nodelist = buf;
		fwd_msg->header.forward.tree_width = header->forward.tree_width;
		fwd_msg->header.forward.tree_depth = header->forward.tree_depth;
		fwd_msg->header.forward.timeout = header->forward.timeout;
		slurm_thread_create_detached(_forward_thread, fwd_msg);
	}
}

/*
 * forward_init    - initialize forward structure
 * IN: forward     - forward_t *   - struct to store forward info
 * RET: VOID
 */
extern void forward_init(forward_t *forward)
{
	*forward = (forward_t) FORWARD_INITIALIZER;
}

/*
 * forward_msg        - logic to forward a message which has been received and
 *                      accumulate the return codes from processes getting the
 *                      forwarded message
 *
 * IN: forward_struct - forward_struct_t *   - holds information about message
 *                                             that needs to be forwarded to
 *                                             children processes
 * IN: header         - header_t             - header from message that came in
 *                                             needing to be forwarded.
 * RET: SLURM_SUCCESS - int
 */
extern int forward_msg(forward_struct_t *forward_struct, header_t *header)
{
	hostlist_t *hl = NULL;
	hostlist_t **sp_hl;
	int hl_count = 0, depth;

	if (!forward_struct->ret_list) {
		error("didn't get a ret_list from forward_struct");
		return SLURM_ERROR;
	}
	hl = hostlist_create(header->forward.nodelist);
	if (header->flags & SLURM_PACK_ADDRS) {
		forward_struct->alias_addrs = extract_net_cred(
			header->forward.alias_addrs.net_cred, header->version);
		if (!forward_struct->alias_addrs) {
			error("unable to extract net_cred");
			hostlist_destroy(hl);
			return SLURM_ERROR;
		}
		forward_struct->alias_addrs->net_cred =
			header->forward.alias_addrs.net_cred;
		header->forward.alias_addrs.net_cred = NULL;
	}

	hostlist_uniq(hl);

	if ((depth = topology_g_split_hostlist(hl, &sp_hl, &hl_count,
					       header->forward.tree_width)) ==
	    SLURM_ERROR) {
		error("unable to split forward hostlist");
		hostlist_destroy(hl);
		return SLURM_ERROR;
	}

	/* Calculate the new timeout based on the original timeout */
	if (header->forward.tree_depth)
		header->forward.timeout = (header->forward.timeout * depth) /
					  header->forward.tree_depth;
	else
		header->forward.timeout *= 2 * depth;
	header->forward.tree_depth = depth;
	forward_struct->timeout = header->forward.timeout;
	_forward_msg_internal(NULL, sp_hl, forward_struct, header,
			      forward_struct->timeout, hl_count);

	xfree(sp_hl);
	hostlist_destroy(hl);
	return SLURM_SUCCESS;
}

static void _get_alias_addrs(hostlist_t *hl, slurm_msg_t *msg, int *cnt)
{
	hostlist_iterator_t *hi;
	char *node_name;
	int addr_index = 0;
	forward_t *forward = &(msg->forward);

	if (!(msg->flags & SLURM_PACK_ADDRS))
		return;
	slurm_free_node_alias_addrs_members(&forward->alias_addrs);

	forward->alias_addrs.node_addrs = xcalloc(*cnt, sizeof(slurm_addr_t));

	hi = hostlist_iterator_create(hl);
	while ((node_name = hostlist_next(hi))) {
		slurm_addr_t *addr =
			&forward->alias_addrs.node_addrs[addr_index];
		if (!slurm_conf_get_addr(node_name, addr, msg->flags)) {
			++addr_index;
		} else {
			hostlist_remove(hi);
			forward->cnt--;
			(*cnt)--;
		}
		free(node_name);
	}
	hostlist_iterator_destroy(hi);

	forward->alias_addrs.node_list = hostlist_ranged_string_xmalloc(hl);
	forward->alias_addrs.node_cnt = *cnt;

	forward->alias_addrs.net_cred =
		create_net_cred(&forward->alias_addrs, msg->protocol_version);
}

/*
 * Get dynamic addrs if forwarding to a unknown/unresolvable nodes.
 */
static void _get_dynamic_addrs(hostlist_t *hl, slurm_msg_t *msg)
{
	char *name;
	hostlist_iterator_t *itr;
	bool try_last = false;
	hostlist_t *hl_last = NULL;

	xassert(hl);
	xassert(msg);

	if (running_in_daemon())
		return;

	if (msg->flags & SLURM_PACK_ADDRS)
		return;

	itr = hostlist_iterator_create(hl);
	slurm_mutex_lock(&alias_addrs_mutex);
	if (last_alias_addrs &&
	    (last_alias_addrs->expiration - time(NULL)) > 10) {
		try_last = true;
		hl_last = hostlist_create(last_alias_addrs->node_list);
	}

	while ((name = hostlist_next(itr))) {
		slurm_node_alias_addrs_t *alias_addrs = NULL;
		char *nodelist;
		bool dynamic;

		if (!slurm_conf_check_addr(name, &dynamic) && !dynamic) {
			free(name);
			continue;
		}

		if (try_last && (hostlist_find(hl_last, name) >= 0)) {
			msg->flags |= SLURM_PACK_ADDRS;
			free(name);
			continue;
		}
		try_last = false;
		nodelist = hostlist_ranged_string_xmalloc(hl);
		if (!slurm_get_node_alias_addrs(nodelist, &alias_addrs)) {
			msg->flags |= SLURM_PACK_ADDRS;
		}
		slurm_free_node_alias_addrs(last_alias_addrs);
		last_alias_addrs = alias_addrs;
		free(name);
		xfree(nodelist);
		break;
	}
	hostlist_iterator_destroy(itr);
	hostlist_destroy(hl_last);

	if (last_alias_addrs && (msg->flags & SLURM_PACK_ADDRS)) {
		slurm_copy_node_alias_addrs_members(&(msg->forward.alias_addrs),
						    last_alias_addrs);
	}
	slurm_mutex_unlock(&alias_addrs_mutex);
}

/*
 * start_msg_tree  - logic to begin the forward tree and
 *                   accumulate the return codes from processes getting the
 *                   forwarded message
 *
 * IN: hl          - hostlist_t   - list of every node to send message to
 * IN: msg         - slurm_msg_t  - message to send.
 * IN: timeout     - int          - how long to wait in milliseconds.
 * RET list_t *    - list containing the responses of the children
 *		     (if any) we forwarded the message to. list
 *		     containing type (ret_data_info_t).
 */
extern list_t *start_msg_tree(hostlist_t *hl, slurm_msg_t *msg, int timeout)
{
	fwd_tree_t fwd_tree;
	pthread_mutex_t tree_mutex;
	pthread_cond_t notify;
	int count = 0;
	list_t *ret_list = NULL;
	int thr_count = 0;
	int host_count = 0;
	hostlist_t **sp_hl;
	int hl_count = 0, depth;

	xassert(hl);
	xassert(msg);

	if (timeout <= 0) {
		/* convert secs to msec */
		timeout = slurm_conf.msg_timeout * MSEC_IN_SEC;
	}

	hostlist_uniq(hl);
	host_count = hostlist_count(hl);

	_get_alias_addrs(hl, msg, &host_count);
	_get_dynamic_addrs(hl, msg);

	if ((depth = topology_g_split_hostlist(hl, &sp_hl, &hl_count,
					       msg->forward.tree_width)) ==
	    SLURM_ERROR) {
		error("unable to split forward hostlist");
		return NULL;
	}
	slurm_mutex_init(&tree_mutex);
	slurm_cond_init(&notify, NULL);

	ret_list = list_create(destroy_data_info);

	memset(&fwd_tree, 0, sizeof(fwd_tree));
	fwd_tree.orig_msg = msg;
	fwd_tree.ret_list = ret_list;
	fwd_tree.tree_depth = depth;
	fwd_tree.timeout = 2 * depth * timeout;
	fwd_tree.notify = &notify;
	fwd_tree.p_thr_count = &thr_count;
	fwd_tree.tree_mutex = &tree_mutex;

	_start_msg_tree_internal(NULL, sp_hl, &fwd_tree, hl_count);

	xfree(sp_hl);

	slurm_mutex_lock(&tree_mutex);

	count = list_count(ret_list);
	debug2("Tree head got back %d looking for %d", count, host_count);
	while (thr_count > 0) {
		slurm_cond_wait(&notify, &tree_mutex);
		count = list_count(ret_list);
		debug2("Tree head got back %d", count);
	}
	xassert(count >= host_count);	/* Tree head did not get all responses,
					 * but no more active fwd threads!*/
	slurm_mutex_unlock(&tree_mutex);

	slurm_mutex_destroy(&tree_mutex);
	slurm_cond_destroy(&notify);

	return ret_list;
}

/*
 * mark_as_failed_forward- mark a node as failed and add it to "ret_list"
 *
 * IN: ret_list       - list_t ** - ret_list to put ret_data_info
 * IN: node_name      - char *   - node name that failed
 * IN: err            - int      - error message from attempt
 *
 */
extern void mark_as_failed_forward(list_t **ret_list, char *node_name, int err)
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
			slurm_cond_wait(&msg->forward_struct->notify,
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

extern void fwd_set_alias_addrs(slurm_node_alias_addrs_t *alias_addrs)
{
	if (!alias_addrs)
		return;

	slurm_mutex_lock(&alias_addrs_mutex);

	if (!last_alias_addrs)
		last_alias_addrs = xmalloc(sizeof(*last_alias_addrs));
	slurm_copy_node_alias_addrs_members(last_alias_addrs, alias_addrs);

	slurm_mutex_unlock(&alias_addrs_mutex);
}

extern void destroy_data_info(void *object)
{
	ret_data_info_t *ret_data_info = object;
	if (ret_data_info) {
		slurm_free_msg_data(ret_data_info->type,
				    ret_data_info->data);
		xfree(ret_data_info->node_name);
		xfree(ret_data_info);
	}
}

extern void destroy_forward(forward_t *forward)
{
	if (forward->init == FORWARD_INIT) {
		slurm_free_node_alias_addrs_members(&forward->alias_addrs);
		xfree(forward->nodelist);
		forward->init = 0;
	} else {
		error("%s: no init", __func__);
	}
}

extern void destroy_forward_struct(forward_struct_t *forward_struct)
{
	if (forward_struct) {
		xfree(forward_struct->buf);
		slurm_mutex_destroy(&forward_struct->forward_mutex);
		slurm_cond_destroy(&forward_struct->notify);
		slurm_free_node_alias_addrs(forward_struct->alias_addrs);
		xfree(forward_struct);
	}
}
