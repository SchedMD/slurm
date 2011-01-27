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
#include "bridge_linker.h"
#include "../block_allocator/block_allocator.h"
#include "../bluegene.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/slurmctld/locks.h"
}

#if defined HAVE_BG_FILES && defined HAVE_BGQ

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

#endif

static bool bridge_status_inited = false;

#if defined HAVE_BG_FILES && defined HAVE_BGQ

#define RETRY_BOOT_COUNT 3

/*
 * Handle compute block status changes as a result of a block allocate.
 */
typedef class event_handler: public bgsched::realtime::ClientEventListener {
public:
	/*
	 *  Handle a block state changed real-time event.
	 */
	void handleBlockStateChangedRealtimeEvent(
		const BlockStateChangedEventInfo& eventInfo);
} event_handler_t;

typedef struct {
	int jobid;
} kill_job_struct_t;

static List kill_job_list = NULL;
static pthread_t real_time_thread;
static pthread_t poll_thread;
static bgsched::realtime::Client *rt_client_ptr = NULL;
pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _destroy_kill_struct(void *object)
{
	kill_job_struct_t *freeit = (kill_job_struct_t *)object;

	if (freeit) {
		xfree(freeit);
	}
}

static int _block_is_deallocating(bg_record_t *bg_record)
{
	int jobid = bg_record->job_running;
	char *user_name = NULL;

	if (bg_record->modifying)
		return SLURM_SUCCESS;

	user_name = xstrdup(bg_conf->slurm_user_name);
	if (bridge_block_remove_all_users(bg_record, NULL) == REMOVE_USER_ERR) {
		error("Something happened removing users from block %s",
		      bg_record->bg_block_id);
	}

	if (bg_record->target_name && bg_record->user_name) {
		if (!strcmp(bg_record->target_name, user_name)) {
			if (strcmp(bg_record->target_name, bg_record->user_name)
			    || (jobid > NO_JOB_RUNNING)) {
				kill_job_struct_t *freeit =
					(kill_job_struct_t *)
					xmalloc(sizeof(freeit));
				freeit->jobid = jobid;
				list_push(kill_job_list, freeit);

				error("Block %s was in a ready state "
				      "for user %s but is being freed. "
				      "Job %d was lost.",
				      bg_record->bg_block_id,
				      bg_record->user_name,
				      jobid);
			} else {
				debug("Block %s was in a ready state "
				      "but is being freed. No job running.",
				      bg_record->bg_block_id);
			}
		} else {
			error("State went to free on a boot for block %s.",
			      bg_record->bg_block_id);
		}
	} else if (bg_record->user_name) {
		error("Target Name was not set "
		      "not set for block %s.",
		      bg_record->bg_block_id);
		bg_record->target_name = xstrdup(bg_record->user_name);
	} else {
		error("Target Name and User Name are "
		      "not set for block %s.",
		      bg_record->bg_block_id);
		bg_record->user_name = xstrdup(user_name);
		bg_record->target_name = xstrdup(bg_record->user_name);
	}

	if (remove_from_bg_list(bg_lists->job_running, bg_record)
	    == SLURM_SUCCESS)
		num_unused_cpus += bg_record->cpu_cnt;
	remove_from_bg_list(bg_lists->booted, bg_record);

	xfree(user_name);

	return SLURM_SUCCESS;
}

static int _update_block_state(bg_record_t *bg_record,
			       bgsched::Block::Status state_in)
{
	bgq_block_status_t state;
	bool skipped_dealloc = false;
	kill_job_struct_t *freeit = NULL;

	switch (state_in) {
	case Block::Allocated:
		state = BG_BLOCK_ALLOCATED;
		break;
	case Block::Booting:
		state = BG_BLOCK_BOOTING;
		break;
	case Block::Free:
		state = BG_BLOCK_FREE;
		break;
	case Block::Initialized:
		state = BG_BLOCK_INITED;
		break;
	case Block::Terminating:
		state = BG_BLOCK_TERM;
		break;
	default:
		state = BG_BLOCK_ERROR;
		break;
	}

	if (bg_record->job_running == BLOCK_ERROR_STATE
	    || bg_record->state == state)
		return 0;

	debug("state of Block %s was %d and now is %d",
	      bg_record->bg_block_id,
	      bg_record->state,
	      state);

	/*
	  check to make sure block went
	  through freeing correctly
	*/
	if ((bg_record->state != BG_BLOCK_TERM
	     && bg_record->state != BG_BLOCK_ERROR)
	    && state == BG_BLOCK_FREE)
		skipped_dealloc = 1;
	else if ((bg_record->state == BG_BLOCK_INITED
		  || bg_record->state == BG_BLOCK_ALLOCATED)
		 && (state == BG_BLOCK_BOOTING)) {
		/* This means the user did a reboot through
		   mpirun but we missed the state
		   change */
		debug("Block %s skipped rebooting, "
		      "but it really is.  "
		      "Setting target_name back to %s",
		      bg_record->bg_block_id,
		      bg_record->user_name);
		xfree(bg_record->target_name);
		bg_record->target_name = xstrdup(bg_record->user_name);
	} else if ((bg_record->state == BG_BLOCK_TERM)
		   && (state == BG_BLOCK_BOOTING))
		/* This is a funky state IBM says
		   isn't a bug, but all their
		   documentation says this doesn't
		   happen, but IBM says oh yeah, you
		   weren't really suppose to notice
		   that. So we will just skip this
		   state and act like this didn't happen. */
		goto nochange_state;

	bg_record->state = state;

	if (bg_record->state == BG_BLOCK_TERM || skipped_dealloc)
		_block_is_deallocating(bg_record);
	else if (bg_record->state == BG_BLOCK_BOOTING) {
		debug("Setting bootflag for %s",
		      bg_record->bg_block_id);
		bg_record->boot_state = 1;
	} else if (bg_record->state == BG_BLOCK_FREE) {
		if (remove_from_bg_list(bg_lists->job_running, bg_record)
		    == SLURM_SUCCESS)
			num_unused_cpus += bg_record->cpu_cnt;
		remove_from_bg_list(bg_lists->booted,
				    bg_record);
	} else if (bg_record->state == BG_BLOCK_ERROR) {
		if (bg_record->boot_state == 1)
			error("Block %s in an error state while booting.",
			      bg_record->bg_block_id);
		else
			error("Block %s in an error state.",
			      bg_record->bg_block_id);
		remove_from_bg_list(bg_lists->booted, bg_record);
		trigger_block_error();
	} else if (bg_record->state == BG_BLOCK_INITED) {
		if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
			list_push(bg_lists->booted, bg_record);
	} else if (bg_record->state == BG_BLOCK_ALLOCATED) {
		if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
			list_push(bg_lists->booted, bg_record);
		if (remove_from_bg_list(bg_lists->job_running, bg_record)
		    == SLURM_SUCCESS)
			num_unused_cpus -= bg_record->cpu_cnt;
	}

nochange_state:

	/* check the boot state */
	debug3("boot state for block %s is %d",
	       bg_record->bg_block_id,
	       bg_record->boot_state);
	if (bg_record->boot_state == 1) {
		switch(bg_record->state) {
		case BG_BLOCK_BOOTING:
			debug3("checking to make sure user %s "
			       "is the user.",
			       bg_record->target_name);

			if (update_block_user(bg_record, 0) == 1)
				last_bg_update = time(NULL);
			if (bg_record->job_ptr) {
				bg_record->job_ptr->job_state |=
					JOB_CONFIGURING;
				last_job_update = time(NULL);
			}
			break;
		case BG_BLOCK_ERROR:
			/* If we get an error on boot that
			 * means it is a transparent L3 error
			 * and should be trying to fix
			 * itself.  If this is the case we
			 * just hang out waiting for the state
			 * to go to free where we will try to
			 * boot again below.
			 */
			break;
		case BG_BLOCK_FREE:
			if (bg_record->boot_count < RETRY_BOOT_COUNT) {
				bridge_block_boot(bg_record);

				if (bg_record->magic == BLOCK_MAGIC) {
					debug("boot count for block %s is %d",
					      bg_record->bg_block_id,
					      bg_record->boot_count);
					bg_record->boot_count++;
				}
			} else {
				char *reason = (char *)
					"status_check: Boot fails ";

				error("Couldn't boot Block %s for user %s",
				      bg_record->bg_block_id,
				      bg_record->target_name);

				slurm_mutex_unlock(&block_state_mutex);
				requeue_and_error(bg_record, reason);
				slurm_mutex_lock(&block_state_mutex);

				bg_record->boot_state = 0;
				bg_record->boot_count = 0;
				if (remove_from_bg_list(
					    bg_lists->job_running, bg_record)
				    == SLURM_SUCCESS) {
					num_unused_cpus += bg_record->cpu_cnt;
				}
				remove_from_bg_list(
					bg_lists->booted, bg_record);
			}
			break;
		case BG_BLOCK_INITED:
		case BG_BLOCK_ALLOCATED:
			debug("block %s is ready.",
			      bg_record->bg_block_id);
			if (bg_record->job_ptr) {
				bg_record->job_ptr->job_state &=
					(~JOB_CONFIGURING);
				last_job_update = time(NULL);
			}
			/* boot flags are reset here */
			if (set_block_user(bg_record) == SLURM_ERROR) {
				freeit = (kill_job_struct_t *)
					xmalloc(sizeof(kill_job_struct_t));
				freeit->jobid = bg_record->job_running;
				list_push(kill_job_list, freeit);
			}
			break;
		case BG_BLOCK_TERM:
			debug2("Block %s is in a deallocating state "
			       "during a boot.  Doing nothing until "
			       "free state.",
			       bg_record->bg_block_id);
			break;
		default:
			debug("Hey the state of block "
			      "%s is %d(%s) doing nothing.",
			      bg_record->bg_block_id,
			      bg_record->state,
			      bg_block_state_string(bg_record->state));
			break;
		}
	}

	return 1;
}

void event_handler::handleBlockStateChangedRealtimeEvent(
        const BlockStateChangedEventInfo& event)
{
	bg_record_t *bg_record = NULL;
	const char *bg_block_id = event.getBlockName().c_str();
	kill_job_struct_t *freeit = NULL;

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

	_update_block_state(bg_record, event.getStatus());

	slurm_mutex_unlock(&block_state_mutex);

	/* kill all the jobs from unexpectedly freed blocks */
	while ((freeit = (kill_job_struct_t *)list_pop(kill_job_list))) {
		debug2("Trying to requeue job %u", freeit->jobid);
		bg_requeue_job(freeit->jobid, 0);
		_destroy_kill_struct(freeit);
	}
	last_bg_update = time(NULL);
}

static int _real_time_connect(void)
{
	int rc = SLURM_ERROR;
	int count = 0;

	while (bridge_status_inited && (rc != SLURM_SUCCESS)) {
		try {
			info("going to connect");
			rt_client_ptr->connect();
			rc = SLURM_SUCCESS;
		} catch (...) {
			rc = SLURM_ERROR;
			error("couldn't connect to the real_time server, "
			      "trying for %d seconds.", count * 5);
			sleep(5);
		}
	}

	return rc;
}

static void *_real_time(void *no_data)
{
	event_handler_t event_hand;

	bool failed = false;
	bgsched::realtime::Filter rt_filter(
		bgsched::realtime::Filter::createNone());

	rt_filter.setBlocks(true);
 	//rt_filter.setBlockDeleted(true);
	// filter.get().setMidplanes(true);
 	// filter.get().setNodeBoards(true);
 	// filter.get().setSwitches(true);
 	// filter.get().setCables(true);

	info("adding listener");
	rt_client_ptr->addListener(event_hand);
  	info("Connecting real-time client..." );
	_real_time_connect();

	while (bridge_status_inited && !failed) {
		bgsched::realtime::Filter::Id filter_id; // Assigned filter id
		info("setting the filter");
		slurm_mutex_lock(&rt_mutex);
		rt_client_ptr->setFilter(rt_filter, &filter_id, NULL);
		info("Requesting updates on the real-time client...");

		rt_client_ptr->requestUpdates(NULL);

		info("Receiving messages on the real-time client...");

		rt_client_ptr->receiveMessages(NULL, NULL, &failed);
		slurm_mutex_unlock(&rt_mutex);

		if (bridge_status_inited && failed) {
			info("Disconnected from real-time events. "
			     "Will try to reconnect.");
			_real_time_connect();
			failed = false;
		}
	}

	return NULL;
}

static void *_poll(void *no_data)
{
	event_handler_t event_hand;

	while (bridge_status_inited) {
		slurm_mutex_lock(&rt_mutex);
		if (!bridge_status_inited)
			break;
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

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	pthread_attr_t thread_attr;

	if (!kill_job_list)
		kill_job_list = list_create(_destroy_kill_struct);

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
#if defined HAVE_BG_FILES && defined HAVE_BGQ

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
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	bg_record_t *bg_record;
	ListIterator itr;
	int updated = 0;
	kill_job_struct_t *freeit = NULL;

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

		if (_update_block_state(bg_record,
					block_ptr->getStatus().toValue()))
			updated = 1;
	}

	/* kill all the jobs from unexpectedly freed blocks */
	while ((freeit = (kill_job_struct_t *)list_pop(kill_job_list))) {
		debug2("Trying to requeue job %u", freeit->jobid);
		bg_requeue_job(freeit->jobid, 0);
		_destroy_kill_struct(freeit);
	}
	if (updated == 1)
		last_bg_update = time(NULL);

#endif
}
