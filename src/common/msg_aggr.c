/*****************************************************************************\
 *  msg_aggr.c - Message Aggregator for sending messages to the
 *               slurmctld, if a reply is expected this also will wait
 *               and get that reply when received.
 *****************************************************************************
 *  Copyright (C) 2015 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Martin Perry <martin.perry@bull.com>
 *             Danny Auble <da@schedmd.com>
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

#include <pthread.h>

#include "slurm/slurm.h"

#include "src/common/macros.h"
#include "src/common/msg_aggr.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/slurmd.h"

typedef struct {
	pthread_mutex_t	aggr_mutex;
	pthread_cond_t	cond;
	uint32_t        debug_flags;
	bool		max_msgs;
	uint64_t        max_msg_cnt;
	List            msg_aggr_list;
	List            msg_list;
	pthread_mutex_t	mutex;
	slurm_addr_t    node_addr;
	bool            running;
	pthread_t       thread_id;
	uint64_t        window;
} msg_collection_type_t;

typedef struct {
	uint16_t msg_index;
	void (*resp_callback) (slurm_msg_t *msg);
	pthread_cond_t wait_cond;
} msg_aggr_t;


/*
 * Message collection data & controls
 */
static msg_collection_type_t msg_collection;


static void _msg_aggr_free(void *x)
{
	msg_aggr_t *object = (msg_aggr_t *)x;
	if (object) {
		slurm_cond_destroy(&object->wait_cond);
		xfree(object);
	}
}

static msg_aggr_t *_handle_msg_aggr_ret(uint32_t msg_index, bool locked)
{
	msg_aggr_t *msg_aggr;
	ListIterator itr;

	if (!locked)
		slurm_mutex_lock(&msg_collection.aggr_mutex);

	itr = list_iterator_create(msg_collection.msg_aggr_list);

	while ((msg_aggr = list_next(itr))) {
		/* just remove them all */
		if (!msg_index) {
			/* make sure we don't wait any longer */
			slurm_cond_signal(&msg_aggr->wait_cond);
			list_remove(itr);
		} else if (msg_aggr->msg_index == msg_index) {
			list_remove(itr);
			break;
		}

	}
	list_iterator_destroy(itr);

	if (!locked)
		slurm_mutex_unlock(&msg_collection.aggr_mutex);

	return msg_aggr;
}

static int _send_to_backup_collector(slurm_msg_t *msg, int rc)
{
	slurm_addr_t *next_dest = NULL;

	if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE) {
		info("%s: primary %s, getting backup", __func__,
		     rc ? "can't be reached" : "is null");
	}

	if ((next_dest = route_g_next_collector_backup())) {
		if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE) {
			char addrbuf[100];
			slurm_print_slurm_addr(next_dest, addrbuf, 32);
			info("%s: *next_dest is %s", __func__, addrbuf);
		}
		memcpy(&msg->address, next_dest, sizeof(slurm_addr_t));
		rc = slurm_send_only_node_msg(msg);
	}

	if (!next_dest ||  (rc != SLURM_SUCCESS)) {
		if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE)
			info("%s: backup %s, sending msg to controller",
			     __func__, rc ? "can't be reached" : "is null");
		rc = slurm_send_only_controller_msg(msg, working_cluster_rec);
	}

	return rc;
}

/*
 *  Send a msg to the next msg aggregation collector node. If primary
 *  collector is unavailable or returns error, try backup collector.
 *  If backup collector is unavailable or returns error, send msg
 *  directly to controller.
 */
static int _send_to_next_collector(slurm_msg_t *msg)
{
	slurm_addr_t *next_dest = NULL;
	bool i_am_collector;
	int rc = SLURM_SUCCESS;

	if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE)
		info("msg aggr: send_to_next_collector: getting primary next "
		     "collector");
	if ((next_dest = route_g_next_collector(&i_am_collector))) {
		if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE) {
			char addrbuf[100];
			slurm_print_slurm_addr(next_dest, addrbuf, 32);
			info("msg aggr: send_to_next_collector: *next_dest is "
			     "%s", addrbuf);
		}
		memcpy(&msg->address, next_dest, sizeof(slurm_addr_t));
		rc = slurm_send_only_node_msg(msg);
	}

	if (!next_dest || (rc != SLURM_SUCCESS))
		rc = _send_to_backup_collector(msg, rc);

	return rc;
}

/*
 * _msg_aggregation_sender()
 *
 *  Start and terminate message collection windows.
 *  Send collected msgs to next collector node or final destination
 *  at window expiration.
 */
static void * _msg_aggregation_sender(void *arg)
{
	struct timeval now;
	struct timespec timeout;
	slurm_msg_t msg;
	composite_msg_t cmp;

	slurm_mutex_lock(&msg_collection.mutex);
	msg_collection.running = 1;
	slurm_cond_signal(&msg_collection.cond);

	while (msg_collection.running) {
		/* Wait for a new msg to be collected */
		slurm_cond_wait(&msg_collection.cond, &msg_collection.mutex);

		if (!msg_collection.running &&
		    !list_count(msg_collection.msg_list))
			break;

		/* A msg has been collected; start new window */
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + (msg_collection.window / 1000);
		timeout.tv_nsec = (now.tv_usec * 1000) +
			(1000000 * (msg_collection.window % 1000));
		timeout.tv_sec += timeout.tv_nsec / 1000000000;
		timeout.tv_nsec %= 1000000000;

		slurm_cond_timedwait(&msg_collection.cond,
				     &msg_collection.mutex, &timeout);

		if (!msg_collection.running &&
		    !list_count(msg_collection.msg_list))
			break;

		msg_collection.max_msgs = true;

		/* Msg collection window has expired and message collection
		 * is suspended; now build and send composite msg */
		memset(&msg, 0, sizeof(slurm_msg_t));
		memset(&cmp, 0, sizeof(composite_msg_t));

		memcpy(&cmp.sender, &msg_collection.node_addr,
		       sizeof(slurm_addr_t));
		cmp.msg_list = msg_collection.msg_list;

		msg_collection.msg_list =
			list_create(slurm_free_comp_msg_list);
		msg_collection.max_msgs = false;

		slurm_msg_t_init(&msg);
		msg.msg_type = MESSAGE_COMPOSITE;
		msg.protocol_version = SLURM_PROTOCOL_VERSION;
		msg.data = &cmp;

		if (_send_to_next_collector(&msg) != SLURM_SUCCESS) {
			error("_msg_aggregation_engine: Unable to send "
			      "composite msg: %m");
		}
		FREE_NULL_LIST(cmp.msg_list);

		/* Resume message collection */
		slurm_cond_broadcast(&msg_collection.cond);
	}

	slurm_mutex_unlock(&msg_collection.mutex);
	return NULL;
}

extern void msg_aggr_sender_init(char *host, uint16_t port, uint64_t window,
				 uint64_t max_msg_cnt)
{
	if (msg_collection.running || (max_msg_cnt <= 1))
		return;


	memset(&msg_collection, 0, sizeof(msg_collection_type_t));

	slurm_mutex_init(&msg_collection.aggr_mutex);
	slurm_mutex_init(&msg_collection.mutex);

	slurm_mutex_lock(&msg_collection.mutex);
	slurm_mutex_lock(&msg_collection.aggr_mutex);
	slurm_cond_init(&msg_collection.cond, NULL);
	slurm_set_addr(&msg_collection.node_addr, port, host);
	msg_collection.window = window;
	msg_collection.max_msg_cnt = max_msg_cnt;
	msg_collection.msg_aggr_list = list_create(_msg_aggr_free);
	msg_collection.msg_list = list_create(slurm_free_comp_msg_list);
	msg_collection.max_msgs = false;
	msg_collection.debug_flags = slurm_get_debug_flags();
	slurm_mutex_unlock(&msg_collection.aggr_mutex);

	slurm_thread_create(&msg_collection.thread_id,
			    &_msg_aggregation_sender, NULL);

	/* wait for thread to start */
	slurm_cond_wait(&msg_collection.cond, &msg_collection.mutex);

	slurm_mutex_unlock(&msg_collection.mutex);
}

extern void msg_aggr_sender_reconfig(uint64_t window, uint64_t max_msg_cnt)
{
	if (msg_collection.running) {
		slurm_mutex_lock(&msg_collection.mutex);
		msg_collection.window = window;
		msg_collection.max_msg_cnt = max_msg_cnt;
		msg_collection.debug_flags = slurm_get_debug_flags();
		slurm_mutex_unlock(&msg_collection.mutex);
	} else if (max_msg_cnt > 1) {
		error("can't start the msg_aggr on a reconfig, "
		      "a restart is needed");
	}
}

extern void msg_aggr_sender_fini(void)
{
	if (!msg_collection.running)
		return;
	msg_collection.running = 0;
	slurm_mutex_lock(&msg_collection.mutex);

	slurm_cond_signal(&msg_collection.cond);
	slurm_mutex_unlock(&msg_collection.mutex);

	pthread_join(msg_collection.thread_id, NULL);
	msg_collection.thread_id = (pthread_t) 0;

	slurm_cond_destroy(&msg_collection.cond);
	/* signal and clear the waiting list */
	slurm_mutex_lock(&msg_collection.aggr_mutex);
	_handle_msg_aggr_ret(0, 1);
	FREE_NULL_LIST(msg_collection.msg_aggr_list);
	slurm_mutex_unlock(&msg_collection.aggr_mutex);
	FREE_NULL_LIST(msg_collection.msg_list);
	slurm_mutex_destroy(&msg_collection.mutex);
}

extern void msg_aggr_add_msg(slurm_msg_t *msg, bool wait,
			     void (*resp_callback) (slurm_msg_t *msg))
{
	int count;
	static uint16_t msg_index = 1;
	static uint32_t wait_count = 0;

	if (!msg_collection.running)
		return;

	slurm_mutex_lock(&msg_collection.mutex);
	if (msg_collection.max_msgs == true) {
		slurm_cond_wait(&msg_collection.cond, &msg_collection.mutex);
	}

	msg->msg_index = msg_index++;

	/* Add msg to message collection */
	list_append(msg_collection.msg_list, msg);

	count = list_count(msg_collection.msg_list);


	/* First msg in collection; initiate new window */
	if (count == 1)
		slurm_cond_signal(&msg_collection.cond);

	/* Max msgs reached; terminate window */
	if (count >= msg_collection.max_msg_cnt) {
		msg_collection.max_msgs = true;
		slurm_cond_signal(&msg_collection.cond);
	}
	slurm_mutex_unlock(&msg_collection.mutex);

	if (wait) {
		msg_aggr_t *msg_aggr = xmalloc(sizeof(msg_aggr_t));
		uint16_t        msg_timeout;
		struct timeval  now;
		struct timespec timeout;

		msg_aggr->msg_index = msg->msg_index;
		msg_aggr->resp_callback = resp_callback;
		slurm_cond_init(&msg_aggr->wait_cond, NULL);

		slurm_mutex_lock(&msg_collection.aggr_mutex);
		list_append(msg_collection.msg_aggr_list, msg_aggr);

		msg_timeout = slurm_get_msg_timeout();
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + msg_timeout;
		timeout.tv_nsec = now.tv_usec * 1000;

		wait_count++;

		if (pthread_cond_timedwait(&msg_aggr->wait_cond,
					   &msg_collection.aggr_mutex,
					   &timeout) == ETIMEDOUT)
			_handle_msg_aggr_ret(msg_aggr->msg_index, 1);
		wait_count--;
		slurm_mutex_unlock(&msg_collection.aggr_mutex);

		if (!msg_collection.running && !wait_count)
			slurm_mutex_destroy(&msg_collection.aggr_mutex);
		_msg_aggr_free(msg_aggr);
	}
}

extern void msg_aggr_add_comp(Buf buffer, void *auth_cred, header_t *header)
{
	slurm_msg_t *msg;

	if (!msg_collection.running)
		return;

	msg = xmalloc_nz(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);

	msg->protocol_version = header->version;
	msg->msg_type = header->msg_type;
	msg->flags = header->flags;

	msg->auth_cred = auth_cred;

	msg->data = buffer;
	msg->data_size = remaining_buf(buffer);

	msg_aggr_add_msg(msg, 0, NULL);
}

extern void msg_aggr_resp(slurm_msg_t *msg)
{
	slurm_msg_t *next_msg;
	composite_msg_t *comp_msg;
	msg_aggr_t *msg_aggr;
	ListIterator itr;

	comp_msg = (composite_msg_t *)msg->data;
	itr = list_iterator_create(comp_msg->msg_list);
	if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE)
		info("msg_aggr_resp: processing composite msg_list...");
	while ((next_msg = list_next(itr))) {
		switch (next_msg->msg_type) {
		case RESPONSE_NODE_REGISTRATION:
		case REQUEST_BATCH_JOB_LAUNCH:
		case RESPONSE_SLURM_RC:
			/* signal sending thread that slurmctld received this
			 * msg */
			if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE)
				info("msg_aggr_resp: response found for "
				     "index %u signaling sending thread",
				     next_msg->msg_index);
			slurm_mutex_lock(&msg_collection.aggr_mutex);
			if (!(msg_aggr = _handle_msg_aggr_ret(
				      next_msg->msg_index, 1))) {
				debug2("msg_aggr_resp: error: unable to "
				       "locate aggr message struct for job %u",
					next_msg->msg_index);
				slurm_mutex_unlock(&msg_collection.aggr_mutex);
				continue;
			}
			if (msg_aggr->resp_callback &&
			    (next_msg->msg_type != RESPONSE_SLURM_RC))
				(*(msg_aggr->resp_callback))(next_msg);
			slurm_cond_signal(&msg_aggr->wait_cond);
			slurm_mutex_unlock(&msg_collection.aggr_mutex);
			break;
		case RESPONSE_MESSAGE_COMPOSITE:
			comp_msg = (composite_msg_t *)next_msg->data;
			/* set up the address here for the next node */
			memcpy(&next_msg->address, &comp_msg->sender,
			       sizeof(slurm_addr_t));

			if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE) {
				char addrbuf[100];
				slurm_print_slurm_addr(&next_msg->address,
						       addrbuf, 32);
				info("msg_aggr_resp: composite response msg "
				     "found for %s", addrbuf);
			}

			slurm_send_only_node_msg(next_msg);

			break;
		default:
			error("_rpc_composite_resp: invalid msg type in "
			      "composite msg_list");
			break;
		}
	}
	list_iterator_destroy(itr);
	if (msg_collection.debug_flags & DEBUG_FLAG_ROUTE)
		info("msg aggr: _rpc_composite_resp: finished processing "
		     "composite msg_list...");
}
