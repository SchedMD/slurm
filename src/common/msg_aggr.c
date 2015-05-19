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

#include "slurm/slurm.h"

#include "src/common/msg_aggr.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_route.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_interface.h"

#include "src/slurmd/slurmd/slurmd.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

typedef struct {
	pthread_mutex_t	mutex;
	pthread_cond_t	cond;
	bool		max_msgs;
	uint64_t        max_msg_cnt;
	composite_msg_t	msgs;
	slurm_addr_t    node_addr;
	uint64_t        window;
} msg_collection_type_t;

/*
 * Message collection data & controls
 */
static msg_collection_type_t msg_collection;

static int _send_to_backup_collector(slurm_msg_t *msg, int rc,
				     uint32_t debug_flags)
{
	slurm_addr_t *next_dest = NULL;

	if (debug_flags & DEBUG_FLAG_ROUTE) {
		info("_send_to_backup_collector: primary %s, "
		     "getting backup",
		     rc ? "can't be reached" : "is null");
	}

	if ((next_dest = route_g_next_collector_backup())) {
		if (debug_flags & DEBUG_FLAG_ROUTE) {
			char addrbuf[100];
			slurm_print_slurm_addr(next_dest, addrbuf, 32);
			info("_send_to_backup_collector: *next_dest is "
			     "%s", addrbuf);
		}
		memcpy(&msg->address, next_dest, sizeof(slurm_addr_t));
		rc = slurm_send_only_node_msg(msg);
	}

	if (!next_dest ||  (rc != SLURM_SUCCESS)) {
		if (debug_flags & DEBUG_FLAG_ROUTE)
			info("_send_to_backup_collector: backup %s, "
			     "sending msg to controller",
			     rc ? "can't be reached" : "is null");
		rc = slurm_send_only_controller_msg(msg);
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
	uint32_t debug_flags = slurm_get_debug_flags();

	if (debug_flags & DEBUG_FLAG_ROUTE)
		info("msg aggr: send_to_next_collector: getting primary next "
		     "collector");
	if ((next_dest = route_g_next_collector(&i_am_collector))) {
		if (debug_flags & DEBUG_FLAG_ROUTE) {
			char addrbuf[100];
			slurm_print_slurm_addr(next_dest, addrbuf, 32);
			info("msg aggr: send_to_next_collector: *next_dest is "
			     "%s", addrbuf);
		}
		memcpy(&msg->address, next_dest, sizeof(slurm_addr_t));
		rc = slurm_send_only_node_msg(msg);
	}

	if (!next_dest || (rc != SLURM_SUCCESS))
		rc = _send_to_backup_collector(msg, rc, debug_flags);

	return rc;
}
static pthread_t msg_aggr_pthread = (pthread_t) 0;
static bool msg_aggr_running = 0;

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

	msg_aggr_running = 1;

	msg_aggr_pthread = pthread_self();
	slurm_mutex_init(&msg_collection.mutex);
	pthread_cond_init(&msg_collection.cond, NULL);
	slurm_mutex_lock(&msg_collection.mutex);
	msg_collection.msgs.msg_list = list_create(slurm_free_comp_msg_list);
	msg_collection.msgs.base_msgs = 0;
	msg_collection.msgs.comp_msgs = 0;
	msg_collection.max_msgs = false;

	while (msg_aggr_running) {
		/* Wait for a new msg to be collected */
		pthread_cond_wait(&msg_collection.cond, &msg_collection.mutex);

		/* A msg has been collected; start new window */
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + (msg_collection.window / 1000);
		timeout.tv_nsec = (now.tv_usec * 1000) +
			(1000000 * (msg_collection.window % 1000));
		timeout.tv_sec += timeout.tv_nsec / 1000000000;
		timeout.tv_nsec %= 1000000000;

		pthread_cond_timedwait(&msg_collection.cond,
				       &msg_collection.mutex, &timeout);

		msg_collection.max_msgs = true;

		/* Msg collection window has expired and message collection
		 * is suspended; now build and send composite msg */
		memset(&msg, 0, sizeof(slurm_msg_t));
		memset(&cmp, 0, sizeof(composite_msg_t));

		memcpy(&cmp.sender, &msg_collection.node_addr,
		       sizeof(slurm_addr_t));
		cmp.base_msgs = msg_collection.msgs.base_msgs;
		cmp.comp_msgs = msg_collection.msgs.comp_msgs;
		cmp.msg_list = msg_collection.msgs.msg_list;

		msg_collection.msgs.msg_list =
			list_create(slurm_free_comp_msg_list);
		msg_collection.msgs.base_msgs = 0;
		msg_collection.msgs.comp_msgs = 0;
		msg_collection.max_msgs = false;

		slurm_msg_t_init(&msg);
		msg.msg_type = MESSAGE_COMPOSITE;
		msg.protocol_version = SLURM_PROTOCOL_VERSION;
		msg.data = &cmp;

		if (slurm_send_to_next_collector(&msg) != SLURM_SUCCESS) {
			error("_msg_aggregation_engine: Unable to send "
			      "composite msg: %m");
		}
		list_destroy(cmp.msg_list);

		/* Resume message collection */
		pthread_cond_broadcast(&msg_collection.cond);
	}

	return NULL;
}

extern void msg_aggr_sender_init(char *host, uint16_t port, uint64_t window,
				 uint64_t max_msg_cnt)
{
	int            rc;
	pthread_attr_t attr;
	pthread_t      id;
	int            retries = 0;

	if (msg_aggr_running)
		return;

	slurm_set_addr(&msg_collection.node_addr, port, host);
	msg_collection.window = window;
	msg_collection.max_msg_cnt = max_msg_cnt;

	slurm_attr_init(&attr);
	rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		errno = rc;
		fatal("msg aggregation: unable to set detachstate on attr: %m");
		slurm_attr_destroy(&attr);
		return;
	}

	while (pthread_create(&id, &attr, &_msg_aggregation_sender, NULL)) {
		error("msg aggregation: pthread_create: %m");
		if (++retries > 3)
			fatal("msg aggregation: pthread_create: %m");
		usleep(10);	/* sleep and again */
	}

	return;
}

extern void msg_aggr_sender_reconfig(uint64_t window, uint64_t max_msg_cnt)
{
	if (msg_aggr_running) {
		msg_collection.window = window;
		msg_collection.max_msg_cnt = max_msg_cnt;
	}
}

extern void msg_aggr_sender_fini(void)
{
	msg_aggr_running = 0;

	if (msg_aggr_pthread && (pthread_self() != msg_aggr_pthread))
		pthread_kill(msg_aggr_pthread, SIGTERM);
}

extern void msg_aggr_add_msg(slurm_msg_t *msg)
{
	slurm_msg_t *msg_ptr;

	slurm_mutex_lock(&msg_collection.mutex);
	if (msg_collection.max_msgs == true) {
		pthread_cond_wait(&msg_collection.cond, &msg_collection.mutex);
	}
	msg_ptr = msg;
	/* Add msg to message collection */
	list_append(msg_collection.msgs.msg_list, msg_ptr);
	if (msg_ptr->msg_type == MESSAGE_COMPOSITE) {
		msg_collection.msgs.comp_msgs++;
	} else {
		msg_collection.msgs.base_msgs++;
	}
	if (list_count(msg_collection.msgs.msg_list) == 1) {
		/* First msg in collection; initiate new window */
		pthread_cond_signal(&msg_collection.cond);
	}
	if (list_count(msg_collection.msgs.msg_list) >=
	    msg_collection.max_msg_cnt) {
		/* Max msgs reached; terminate window */
		msg_collection.max_msgs = true;
		pthread_cond_signal(&msg_collection.cond);
	}
	slurm_mutex_unlock(&msg_collection.mutex);
}
