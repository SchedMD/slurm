/*****************************************************************************\
 *  bridge_status.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

extern "C" {
#include "../ba_bgq/block_allocator.h"
#include "../bg_core.h"
#include "../bg_status.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/slurmctld/locks.h"
}

#if defined HAVE_BG_FILES

#include <bgsched/bgsched.h>
#include <bgsched/Block.h>
#include <bgsched/core/core.h>
#include <boost/foreach.hpp>
#include <bgsched/realtime/Client.h>
#include <bgsched/realtime/ClientConfiguration.h>
#include <bgsched/realtime/ClientEventListener.h>
#include <bgsched/realtime/Filter.h>
#include "bridge_status.h"

#include <iostream>

using namespace std;
using namespace bgsched;
using namespace bgsched::core;
using namespace bgsched::realtime;
#endif

static bool bridge_status_inited = false;

#if defined HAVE_BG_FILES

/*
 * Handle compute block status changes as a result of a block allocate.
 */
typedef class event_handler: public bgsched::realtime::ClientEventListener {
public:
	/*
	 *  Handle a block state changed real-time event.
	 */
	void handleBlockStateChangedRealtimeEvent(
		const BlockStateChangedEventInfo& info);
	// /*
	//  *  Handle a midplane state changed real-time event.
	//  */
	// virtual void handleMidplaneStateChangedRealtimeEvent(
	// 	const MidplaneStateChangedEventInfo& info);

	// /*
	//  * Handle a switch state changed real-time event.
	//  */
	// virtual void handleSwitchStateChangedRealtimeEvent(
	// 	const SwitchStateChangedEventInfo& info);

	// /*
	//  * Handle a node board state changed real-time event.
	//  */
	// virtual void handleNodeBoardStateChangedRealtimeEvent(
	// 	const NodeBoardStateChangedEventInfo& info);

	// /*
	//  * Handle a cable state changed real-time event.
	//  */
	// virtual void handleCableStateChangedRealtimeEvent(
	// 	const CableStateChangedEventInfo& info);

} event_handler_t;

static List kill_job_list = NULL;
static pthread_t real_time_thread;
static pthread_t poll_thread;
static bgsched::realtime::Client *rt_client_ptr = NULL;
pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;

static bg_block_status_t _translate_status(bgsched::Block::Status state_in)
{
	switch (state_in) {
	case Block::Allocated:
		return BG_BLOCK_ALLOCATED;
		break;
	case Block::Booting:
		return BG_BLOCK_BOOTING;
		break;
	case Block::Free:
		return BG_BLOCK_FREE;
		break;
	case Block::Initialized:
		return BG_BLOCK_INITED;
		break;
	case Block::Terminating:
		return BG_BLOCK_TERM;
		break;
	default:
		return BG_BLOCK_ERROR;
		break;
	}
	error("unknown block state %d", state_in);
	return BG_BLOCK_NAV;
}

void event_handler::handleBlockStateChangedRealtimeEvent(
        const BlockStateChangedEventInfo& event)
{
	bg_record_t *bg_record = NULL;
	const char *bg_block_id = event.getBlockName().c_str();

	info("Received block status changed real-time event. Block=%s state=%d",
	     bg_block_id, event.getStatus());

	if (!bg_lists->main)
		return;

	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main, bg_block_id);
	if (!bg_record) {
		slurm_mutex_unlock(&block_state_mutex);
		info("bg_record %s isn't in the main list", bg_block_id);
		return;
	}

	bg_status_update_block_state(bg_record,
				     _translate_status(event.getStatus()),
				     kill_job_list);

	slurm_mutex_unlock(&block_state_mutex);

	bg_status_process_kill_job_list(kill_job_list);

	last_bg_update = time(NULL);
}

static int _real_time_connect(void)
{
	int rc = SLURM_ERROR;
	int count = 0;
	int sleep_value = 5;

	while (bridge_status_inited && (rc != SLURM_SUCCESS)) {
		try {
			rt_client_ptr->connect();
			rc = SLURM_SUCCESS;
		} catch (...) {
			rc = SLURM_ERROR;
			error("couldn't connect to the real_time server, "
			      "trying for %d seconds.", count * sleep_value);
			sleep(sleep_value);
			count++;
		}
	}

	return rc;
}

static void *_real_time(void *no_data)
{
	event_handler_t event_hand;
	int rc = SLURM_SUCCESS;
	bool failed = false;
	Filter::BlockStatuses block_statuses;
  	Filter rt_filter(Filter::createNone());

	rt_filter.setBlocks(true);
        block_statuses.insert(Block::Free);
	block_statuses.insert(Block::Booting);
        block_statuses.insert(Block::Initialized);
	block_statuses.insert(Block::Terminating);
        rt_filter.setBlockStatuses(&block_statuses);

 	// rt_filter.get().setMidplanes(true);
 	// rt_filter.get().setNodeBoards(true);
 	// rt_filter.get().setSwitches(true);
 	// rt_filter.get().setCables(true);

	rt_client_ptr->addListener(event_hand);

	rc = _real_time_connect();

	while (bridge_status_inited) {
		bgsched::realtime::Filter::Id filter_id; // Assigned filter id

		slurm_mutex_lock(&rt_mutex);
		if (bridge_status_inited) {
			slurm_mutex_unlock(&rt_mutex);
			break;
		}

		if (rc == SLURM_SUCCESS) {
			rt_client_ptr->setFilter(rt_filter, &filter_id, NULL);

			rt_client_ptr->requestUpdates(NULL);
			rt_client_ptr->receiveMessages(NULL, NULL, &failed);
		} else
			failed = true;

		slurm_mutex_unlock(&rt_mutex);

		if (bridge_status_inited && failed) {
			error("Disconnected from real-time events. "
			     "Will try tCopy of SP2o reconnect.");
			rc = _real_time_connect();
			if (rc == SLURM_SUCCESS) {
				info("real-time server connected again");
				failed = false;
			}
		}
	}
	return NULL;
}

static void *_poll(void *no_data)
{
	event_handler_t event_hand;

	while (bridge_status_inited) {
		//debug("polling waiting until realtime dies");
		slurm_mutex_lock(&rt_mutex);
		if (!bridge_status_inited) {
			slurm_mutex_unlock(&rt_mutex);
			break;
		}
		//debug("polling taking over, realtime is dead");
		bridge_status_do_poll();
		slurm_mutex_unlock(&rt_mutex);
		sleep(1);
	}
	return NULL;
}
#endif

extern int bridge_status_init(void)
{
	if (bridge_status_inited)
		return SLURM_ERROR;

	bridge_status_inited = true;

#if defined HAVE_BG_FILES
	pthread_attr_t thread_attr;

	if (!kill_job_list)
		kill_job_list = bg_status_create_kill_job_list();

	rt_client_ptr = new(bgsched::realtime::Client);

	slurm_attr_init(&thread_attr);
	if (pthread_create(&real_time_thread, &thread_attr, _real_time, NULL))
		fatal("pthread_create error %m");
	slurm_attr_init(&thread_attr);
	if (pthread_create(&poll_thread, &thread_attr, _poll, NULL))
		fatal("pthread_create error %m");
	slurm_attr_destroy(&thread_attr);
#endif
	return SLURM_SUCCESS;
}

extern int bridge_status_fini(void)
{
	if (!bridge_status_inited)
		return SLURM_ERROR;

	bridge_status_inited = false;
#if defined HAVE_BG_FILES
	/* make the rt connection end. */
	rt_client_ptr->disconnect();

	if (kill_job_list) {
		list_destroy(kill_job_list);
		kill_job_list = NULL;
	}

	if (real_time_thread) {
		pthread_join(real_time_thread, NULL);
		real_time_thread = 0;
	}

	if (poll_thread) {
		pthread_join(poll_thread, NULL);
		poll_thread = 0;
	}
	pthread_mutex_destroy(&rt_mutex);
	delete(rt_client_ptr);
#endif
	return SLURM_SUCCESS;
}


extern void bridge_status_do_poll(void)
{
#if defined HAVE_BG_FILES
	bg_record_t *bg_record;
	ListIterator itr;
	int updated = 0;

	if (!bg_lists->main)
		return;

	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = (bg_record_t *) list_next(itr))) {
		BlockFilter filter;
		Block::Ptrs vec;

		if ((bg_record->magic != BLOCK_MAGIC)
		    || !bg_record->bg_block_id)
			continue;

		filter.setName(string(bg_record->bg_block_id));

		vec = getBlocks(filter, BlockSort::AnyOrder);
		if (vec.empty()) {
			debug("block %s not found, removing "
			      "from slurm", bg_record->bg_block_id);
			list_remove(itr);
			destroy_bg_record(bg_record);
			continue;
		}
		const Block::Ptr &block_ptr = *(vec.begin());

		updated = bg_status_update_block_state(
			bg_record,
			_translate_status(block_ptr->getStatus().toValue()),
			kill_job_list);
	}
	slurm_mutex_unlock(&block_state_mutex);

	bg_status_process_kill_job_list(kill_job_list);

	if (updated == 1)
		last_bg_update = time(NULL);

#endif
}

/*
 * This could potentially lock the node lock in the slurmctld with
 * slurm_drain_node, so if slurmctld_locked is called we will call the
 * drainning function without locking the lock again.
 */
extern int bridge_block_check_mp_states(char *bg_block_id,
					bool slurmctld_locked)
{
	return SLURM_SUCCESS;
}
