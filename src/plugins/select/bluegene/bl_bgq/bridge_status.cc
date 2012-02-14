/*****************************************************************************\
 *  bridge_status.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
#include <bgsched/NodeBoard.h>
#include <bgsched/Hardware.h>
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

static bool initial_poll = true;
static bool rt_running = false;

/*
 * Handle compute block status changes as a result of a block allocate.
 */
typedef class event_handler: public bgsched::realtime::ClientEventListener {
public:
	/*
	 * Handle a real-time started event.
	 */
	virtual void handleRealtimeStartedRealtimeEvent(
		const RealtimeStartedEventInfo& event);

	/*
	 * Handle a real-time ended event.
	 */
	virtual void handleRealtimeEndedRealtimeEvent(
		const RealtimeEndedEventInfo& event);

	/*
	 *  Handle a block state changed real-time event.
	 */
	virtual void handleBlockStateChangedRealtimeEvent(
		const BlockStateChangedEventInfo& event);

	/*
	 *  Handle a midplane state changed real-time event.
	 */
	virtual void handleMidplaneStateChangedRealtimeEvent(
		const MidplaneStateChangedEventInfo& event);

	/*
	 * Handle a switch state changed real-time event.
	 */
	virtual void handleSwitchStateChangedRealtimeEvent(
		const SwitchStateChangedEventInfo& event);

	/*
	 * Handle a node board state changed real-time event.
	 */
	virtual void handleNodeBoardStateChangedRealtimeEvent(
		const NodeBoardStateChangedEventInfo& event);

	/*
	 * Handle a cnode state changed real-time event.
	 */
	virtual void handleNodeStateChangedRealtimeEvent(
		const NodeStateChangedEventInfo& event);

	/*
	 * Handle a cable state changed real-time event.
	 */
	virtual void handleTorusCableStateChangedRealtimeEvent(
		const TorusCableStateChangedEventInfo& event);

} event_handler_t;

static List kill_job_list = NULL;
static pthread_t real_time_thread;
static pthread_t poll_thread;
static bgsched::realtime::Client *rt_client_ptr = NULL;
pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;

/* rt_mutex must be locked before calling this. */
static void _bridge_status_disconnect()
{
	try {
		rt_client_ptr->disconnect();
	} catch (bgsched::realtime::InternalErrorException& err) {
		bridge_handle_realtime_internal_errors(
			"realtime::disconnect", err.getError().toValue());
	} catch (...) {
		error("Unknown error from realtime::disconnect");
	}
}

static void _handle_bad_midplane(const char *mp_coords,
				 EnumWrapper<Hardware::State> state)
{
	char bg_down_node[128];

	assert(mp_coords);

	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, mp_coords);

	if (!node_already_down(bg_down_node)) {
		error("Midplane %s, state went to '%s', marking midplane down.",
		      bg_down_node,
		      bridge_hardware_state_string(state.toValue()));
		slurm_drain_nodes(
			bg_down_node,
			(char *)"select_bluegene: MMCS midplane not UP",
			slurm_get_slurm_user_id());
	}
}

static void _handle_bad_switch(int dim, const char *mp_coords,
			       EnumWrapper<Hardware::State> state)
{
	char bg_down_node[128];

	assert(mp_coords);

	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, mp_coords);

	if (!node_already_down(bg_down_node)) {
		error("Switch at dim '%d' on Midplane %s, state went to '%s', "
		      "marking midplane down.",
		      dim, bg_down_node,
		      bridge_hardware_state_string(state.toValue()));
		slurm_drain_nodes(bg_down_node,
				  (char *)"select_bluegene: MMCS switch not UP",
				  slurm_get_slurm_user_id());
	}
}

static void _handle_bad_nodeboard(const char *nb_name, const char* mp_coords,
				  EnumWrapper<Hardware::State> state,
				  char *reason, bool block_state_locked)
{
	char bg_down_node[128];
	int io_start;
	int rc;

	assert(nb_name);
	assert(mp_coords);

	/* From the first nodecard id we can figure
	   out where to start from with the alloc of ionodes.
	*/
	io_start = atoi((char*)nb_name+1);
	io_start *= bg_conf->io_ratio;

	/* On small systems with less than a midplane the
	   database may see the nodecards there but in missing
	   state.  To avoid getting a bunch of warnings here just
	   skip over the ones missing.
	*/
	if (io_start >= bg_conf->ionodes_per_mp) {
		if (state == Hardware::Missing)
			debug3("Nodeboard %s is missing",
			       nb_name);
		else
			error("We don't have the system configured "
			      "for this nodeboard %s, we only have "
			      "%d ionodes and this starts at %d",
			      nb_name, bg_conf->ionodes_per_mp, io_start);
		return;
	}

	/* if (!ionode_bitmap) */
	/* 	ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp); */
	/* info("setting %s start %d of %d", */
	/*      nb_name,  io_start, bg_conf->ionodes_per_mp); */
	/* bit_nset(ionode_bitmap, io_start, io_start+io_cnt); */

	/* we have to handle each nodecard separately to make
	   sure we don't create holes in the system */
	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, mp_coords);

	/* unlock mutex here since down_nodecard could produce
	   deadlock */
	slurm_mutex_unlock(&ba_system_mutex);
	if (block_state_locked)
		slurm_mutex_unlock(&block_state_mutex);
	rc = down_nodecard(bg_down_node, io_start, 0, reason);
	if (block_state_locked)
		slurm_mutex_lock(&block_state_mutex);
	slurm_mutex_lock(&ba_system_mutex);

	if (rc == SLURM_SUCCESS)
		debug("nodeboard %s on %s is in an error state '%s'",
		      nb_name, bg_down_node,
		      bridge_hardware_state_string(state.toValue()));
	else
		debug2("nodeboard %s on %s is in an error state '%s', "
		       "but error was returned when trying to make it so",
		       nb_name, bg_down_node,
		       bridge_hardware_state_string(state.toValue()));
	return;
}

static void _handle_node_change(ba_mp_t *ba_mp, const std::string& cnode_loc,
				EnumWrapper<Hardware::State> state,
				List *delete_list)
{
	Coordinates ibm_cnode_coords = getNodeMidplaneCoordinates(cnode_loc);
	uint16_t cnode_coords[Dimension::NodeDims];
	int inx, set, changed = 0;
	uint16_t dim;
	bg_record_t *bg_record;
	ba_mp_t *found_ba_mp;
	ListIterator itr, itr2;
	select_nodeinfo_t *nodeinfo;
	struct node_record *node_ptr = NULL;

	/* This will be handled on the initial poll only */
	if (!initial_poll && bg_conf->sub_mp_sys
	    && (state == Hardware::Missing))
		return;

	if (!ba_mp->cnode_err_bitmap)
		ba_mp->cnode_err_bitmap = bit_alloc(bg_conf->mp_cnode_cnt);

	for (dim = 0; dim < Dimension::NodeDims; dim++)
		cnode_coords[dim] = ibm_cnode_coords[dim];

	inx = ba_node_xlate_to_1d(cnode_coords, ba_mp_geo_system);
	if (inx >= bit_size(ba_mp->cnode_err_bitmap)) {
		error("trying to set cnode %d but we only have %d",
		      inx, bit_size(ba_mp->cnode_err_bitmap));
		return;
	}

	node_ptr = &(node_record_table_ptr[ba_mp->index]);
	set = bit_test(ba_mp->cnode_err_bitmap, inx);
	if (bg_conf->sub_mp_sys && (state == Hardware::Missing)) {
		struct part_record *part_ptr;
		/* If Missing we are just going to throw any block
		   away so don't set the err bitmap. Remove the
		   hardware from the system instead. */
		if (node_ptr->cpus >= bg_conf->cpu_ratio)
			node_ptr->cpus -= bg_conf->cpu_ratio;
		if (node_ptr->sockets)
			node_ptr->sockets--;
		if (node_ptr->real_memory >= 16384)
			node_ptr->real_memory -= 16384;

		if (bg_conf->actual_cnodes_per_mp)
			bg_conf->actual_cnodes_per_mp--;
		itr = list_iterator_create(part_list);
		while ((part_ptr = (struct part_record *)list_next(itr))) {
			if (!bit_test(part_ptr->node_bitmap, ba_mp->index))
				continue;
			if (part_ptr->total_cpus >= bg_conf->cpu_ratio)
				part_ptr->total_cpus -= bg_conf->cpu_ratio;
		}
		list_iterator_destroy(itr);

		changed = 1;
	} else if (state != Hardware::Available) {
		if (!set) {
			bit_set(ba_mp->cnode_err_bitmap, inx);
			changed = 1;
		}
	} else if (set) {
		bit_clear(ba_mp->cnode_err_bitmap, inx);
		changed = 1;
	}

	/* If the state is error this could happen after a software
	   error and thus mean it wasn't changed so we need to handle
	   it no matter what.
	*/
	if (state == Hardware::Error) {
		int nc_loc = ba_translate_coord2nc(cnode_coords);
		char nc_name[10];
		char reason[255];
		snprintf(nc_name, sizeof(nc_name), "N%d", nc_loc);
		snprintf(reason, sizeof(reason),
			 "_handle_node_change: On midplane %s nodeboard %s "
			 "had cnode %u%u%u%u%u(%s) got into an error state.",
			 ba_mp->coord_str,
			 nc_name,
			 cnode_coords[0],
			 cnode_coords[1],
			 cnode_coords[2],
			 cnode_coords[3],
			 cnode_coords[4],
			 cnode_loc.c_str());
		error("%s", reason);
		_handle_bad_nodeboard(nc_name, ba_mp->coord_str,
				      state, reason, 1);
	}

	if (!changed)
		return;
	last_bg_update = time(NULL);
	info("_handle_node_change: state for %s - %s is '%s'",
	     ba_mp->coord_str, cnode_loc.c_str(),
	     bridge_hardware_state_string(state.toValue()));

	assert(node_ptr->select_nodeinfo);
	nodeinfo = (select_nodeinfo_t *)node_ptr->select_nodeinfo->data;
	assert(nodeinfo);
	xfree(nodeinfo->failed_cnodes);
	nodeinfo->failed_cnodes = ba_node_map_ranged_hostlist(
		ba_mp->cnode_err_bitmap, ba_mp_geo_system);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = (bg_record_t *)list_next(itr))) {
		if (bg_record->free_cnt)
			continue;
		if (!bit_test(bg_record->mp_bitmap, ba_mp->index))
			continue;
		itr2 = list_iterator_create(bg_record->ba_mp_list);
		while ((found_ba_mp = (ba_mp_t *)list_next(itr2))) {
			float err_ratio;
			struct job_record *job_ptr = NULL;

			if (found_ba_mp->index != ba_mp->index)
				continue;
			if (!found_ba_mp->used)
				continue;
			/* perhaps this block isn't involved in this
			   error */
			if (found_ba_mp->cnode_usable_bitmap) {
				if (bit_test(found_ba_mp->cnode_usable_bitmap,
					     inx))
					continue;
			}

			if (bg_conf->sub_mp_sys
			    && (state == Hardware::Missing)) {
				if (!*delete_list)
					*delete_list = list_create(NULL);
				debug("Removing block %s, "
				      "it has missing cnodes",
				      bg_record->bg_block_id);
				/* If we don't have any mp_counts
				 * force block removal */
				bg_record->mp_count = 0;
				list_push(*delete_list, bg_record);
				break;
			}

			if (!found_ba_mp->cnode_err_bitmap)
				found_ba_mp->cnode_err_bitmap =
					bit_alloc(bg_conf->mp_cnode_cnt);

			if (state != Hardware::Available) {
				bit_set(found_ba_mp->cnode_err_bitmap, inx);
				bg_record->cnode_err_cnt++;
			} else if (set && bg_record->cnode_err_cnt) {
				bit_clear(found_ba_mp->cnode_err_bitmap, inx);
				bg_record->cnode_err_cnt--;
			}

			err_ratio = (float)bg_record->cnode_err_cnt
				/ (float)bg_record->cnode_cnt;
                        bg_record->err_ratio = err_ratio * 100;

			/* handle really small ratios */
			if (!bg_record->err_ratio && bg_record->cnode_err_cnt)
				bg_record->err_ratio = 1;
			debug("count in error for %s is %u with ratio at %u",
			      bg_record->bg_block_id, bg_record->cnode_err_cnt,
			      bg_record->err_ratio);

			if (bg_record->job_ptr)
				job_ptr = bg_record->job_ptr;
			else if (bg_record->job_list
				 && list_count(bg_record->job_list)) {
				ListIterator job_itr = list_iterator_create(
					bg_record->job_list);
				while ((job_ptr = (struct job_record *)
					list_next(job_itr))) {
					select_jobinfo_t *jobinfo =
						(select_jobinfo_t *)
						job_ptr->select_jobinfo->data;
					/* If no units_avail we are
					   using the whole thing, else
					   check the index.
					*/
					if (!jobinfo->units_avail
					    || bit_test(jobinfo->units_avail,
							inx))
						break;
				}
				list_iterator_destroy(job_itr);
			}

			/* block_state_mutex is locked so handle this later */
			if (job_ptr && job_ptr->kill_on_node_fail) {
				kill_job_struct_t *freeit =
					(kill_job_struct_t *)
					xmalloc(sizeof(freeit));
				freeit->jobid = job_ptr->job_id;
				list_push(kill_job_list, freeit);
			}

			break;
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);
}

static void _handle_cable_change(int dim, ba_mp_t *ba_mp,
				 EnumWrapper<Hardware::State> state,
				 List *delete_list)
{
	select_nodeinfo_t *nodeinfo;
	struct node_record *node_ptr = NULL;
	char reason[200];
	ba_mp_t *next_ba_mp = NULL;;

	if (state == Hardware::Available) {
		/* no change */
		if (!(ba_mp->axis_switch[dim].usage & BG_SWITCH_CABLE_ERROR))
			return;
		next_ba_mp = ba_mp->next_mp[dim];

		node_ptr = &(node_record_table_ptr[ba_mp->index]);
		assert(node_ptr->select_nodeinfo);
		nodeinfo = (select_nodeinfo_t *)node_ptr->select_nodeinfo->data;
		assert(nodeinfo);

		ba_mp->axis_switch[dim].usage &= (~BG_SWITCH_CABLE_ERROR_FULL);
		info("Cable in dim '%u' on Midplane %s, "
		     "has returned to service",
		     dim, ba_mp->coord_str);
		/* Don't resume any blocks in the error, Admins will
		   do this when they make sure it is ready.  Really
		   only matters for static blocks.  On a dynamic
		   system no block will be left around if a cable is bad.
		*/
		snprintf(reason, sizeof(reason),
			 "Cable going from %s -> %s (%d) is not available.\n",
			 ba_mp->coord_str, next_ba_mp->coord_str, dim);

		xstrsubstitute(nodeinfo->extra_info, reason, NULL);
		if (nodeinfo->extra_info && !strlen(nodeinfo->extra_info))
			xfree(nodeinfo->extra_info);

	} else if (!(ba_mp->axis_switch[dim].usage & BG_SWITCH_CABLE_ERROR)) {
		bg_record_t *bg_record = NULL;
		ListIterator itr;

		next_ba_mp = ba_mp->next_mp[dim];

		node_ptr = &(node_record_table_ptr[ba_mp->index]);
		assert(node_ptr->select_nodeinfo);
		nodeinfo = (select_nodeinfo_t *)node_ptr->select_nodeinfo->data;
		assert(nodeinfo);

		ba_mp->axis_switch[dim].usage |= BG_SWITCH_CABLE_ERROR_FULL;

		error("Cable at dim '%d' on Midplane %s, "
		      "state went to '%s', marking cable down.",
		      dim, ba_mp->coord_str,
		      bridge_hardware_state_string(state.toValue()));

		snprintf(reason, sizeof(reason),
			 "Cable going from %s -> %s (%d) is not available.\n",
			 ba_mp->coord_str, next_ba_mp->coord_str, dim);
		if (nodeinfo->extra_info) {
			if (!strstr(nodeinfo->extra_info, reason))
				xstrcat(nodeinfo->extra_info, reason);
		} else
			nodeinfo->extra_info = xstrdup(reason);

		/* Now handle potential overlapping blocks. */
		itr = list_iterator_create(bg_lists->main);
		while ((bg_record = (bg_record_t *)list_next(itr))) {
			if (bg_record->free_cnt)
				continue;
			if (bg_record->mp_count == 1)
				continue;
			if (!bit_test(bg_record->mp_bitmap, ba_mp->index))
				continue;
			if (!bit_test(bg_record->mp_bitmap, next_ba_mp->index))
				continue;
			if (!*delete_list)
				*delete_list = list_create(NULL);
			list_push(*delete_list, bg_record);
		}
		list_iterator_destroy(itr);
	}
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

	rt_filter.setNodes(true);
	rt_filter.setNodeBoards(true);
	rt_filter.setSwitches(true);
	rt_filter.setBlocks(true);

	rt_filter.setMidplanes(true);
	rt_filter.setTorusCables(true);

	block_statuses.insert(Block::Free);
	block_statuses.insert(Block::Booting);
	block_statuses.insert(Block::Initialized);
	block_statuses.insert(Block::Terminating);
	rt_filter.setBlockStatuses(&block_statuses);

	rt_client_ptr->addListener(event_hand);

	rc = _real_time_connect();

	while (bridge_status_inited) {
		bgsched::realtime::Filter::Id filter_id; // Assigned filter id

		slurm_mutex_lock(&rt_mutex);
		rt_running = 1;

		if (!bridge_status_inited) {
			rt_running = 0;
			slurm_mutex_unlock(&rt_mutex);
			break;
		}

		if (rc == SLURM_SUCCESS) {
			/* receiveMessages will set this to false if
			   all is well.  Otherwise we did fail.
			*/
			failed = true;
			try {
				rt_client_ptr->setFilter(rt_filter, &filter_id,
							 NULL);
				rt_client_ptr->requestUpdates(NULL);
				rt_client_ptr->receiveMessages(NULL, NULL,
							       &failed);
			} catch (bgsched::realtime::ClientStateException& err) {
				bridge_handle_input_errors(
					"RealTime Setup",
					err.getError().toValue(), NULL);
			} catch (bgsched::realtime::ConnectionException& err) {
				bridge_handle_input_errors(
					"RealTime Setup",
					err.getError().toValue(), NULL);
			} catch (bgsched::realtime::ProtocolException& err) {
				bridge_handle_input_errors(
					"RealTime Setup",
					err.getError().toValue(), NULL);
			} catch (...) {
				error("RealTime Setup: Unknown error thrown?");
			}
		} else
			failed = true;

		rt_running = 0;
		slurm_mutex_unlock(&rt_mutex);

		if (bridge_status_inited && failed) {
			error("Disconnected from real-time events. "
			      "Will try to reconnect.");
			rc = _real_time_connect();
			if (rc == SLURM_SUCCESS) {
				info("real-time server connected again");
				failed = false;
			}
		}
	}
	return NULL;
}

static void _do_block_poll(void)
{
	bg_record_t *bg_record;
	ListIterator itr;
	int updated = 0;

	if (!bg_lists->main)
		return;

	/* Always lock the slurmctld before locking the
	 * block_state_mutex to avoid deadlock. */
	lock_slurmctld(job_read_lock);
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = (bg_record_t *) list_next(itr))) {
		BlockFilter filter;
		Block::Ptrs vec;

		if ((bg_record->magic != BLOCK_MAGIC)
		    || !bg_record->bg_block_id)
			continue;

		filter.setName(string(bg_record->bg_block_id));

		vec = bridge_get_blocks(filter);
		if (vec.empty()) {
			debug("block %s not found, removing "
			      "from slurm", bg_record->bg_block_id);
			list_delete_item(itr);
			continue;
		}
		const Block::Ptr &block_ptr = *(vec.begin());

		if (bg_status_update_block_state(
			    bg_record,
			    bridge_translate_status(
				    block_ptr->getStatus().toValue()),
			    kill_job_list))
			updated = 1;
	}
	slurm_mutex_unlock(&block_state_mutex);
	unlock_slurmctld(job_read_lock);

	bg_status_process_kill_job_list(kill_job_list);

	if (updated == 1)
		last_bg_update = time(NULL);

}

static void _handle_midplane_update(ComputeHardware::ConstPtr bgq,
				    ba_mp_t *ba_mp, List *delete_list)
{
	Midplane::ConstPtr mp_ptr = bridge_get_midplane(bgq, ba_mp);
	int i;
	Dimension dim;

	if (!mp_ptr) {
		info("no midplane in the system at %s", ba_mp->coord_str);
		return;
	}

	if (mp_ptr->getState() != Hardware::Available) {
		_handle_bad_midplane(ba_mp->coord_str, mp_ptr->getState());
		/* no reason to continue */
		return;
	} else {
		Node::ConstPtrs vec = bridge_get_midplane_nodes(
			mp_ptr->getLocation());
		if (!vec.empty()) {
			BOOST_FOREACH(const Node::ConstPtr& cnode_ptr, vec) {
				_handle_node_change(ba_mp,
						    cnode_ptr->getLocation(),
						    cnode_ptr->getState(),
						    delete_list);
			}
		}
	}

	for (i=0; i<16; i++) {
		NodeBoard::ConstPtr nb_ptr = bridge_get_nodeboard(mp_ptr, i);
		/* When a cnode is in error state a nodeboard is also
		   set in an error state.  Since we want to track on
		   the cnode level and not the nodeboard level we can
		   use the isMetaState option that will tell me of
		   this state.  If it isn't set then the nodeboard
		   itself is in an error state so procede.
		*/
		if (nb_ptr && !nb_ptr->isMetaState()
		    && (nb_ptr->getState() != Hardware::Available))
			_handle_bad_nodeboard(
				nb_ptr->getLocation().substr(7,3).c_str(),
				ba_mp->coord_str, nb_ptr->getState(), NULL, 1);
	}

	for (dim=Dimension::A; dim<=Dimension::D; dim++) {
		Switch::ConstPtr switch_ptr = bridge_get_switch(mp_ptr, dim);
		if (switch_ptr) {
			if (switch_ptr->getState() != Hardware::Available)
				_handle_bad_switch(dim,
						   ba_mp->coord_str,
						   switch_ptr->getState());
			else {
				Cable::ConstPtr my_cable =
					switch_ptr->getCable();
				/* Dimensions of length 1 do not have a
				   cable. (duh).
				*/
				if (my_cable)
					_handle_cable_change(
						dim, ba_mp,
						my_cable->getState(),
						delete_list);
			}
		}
	}
}

static void _do_hardware_poll(int level, uint16_t *coords,
			      ComputeHardware::ConstPtr bgqsys)
{
	ba_mp_t *ba_mp;
	List delete_list = NULL;

	if (!bgqsys) {
		error("_do_hardware_poll: No ComputeHardware ptr");
		return;
	}

	if (!ba_main_grid || (level > SYSTEM_DIMENSIONS))
		return;

	if (level < SYSTEM_DIMENSIONS) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outter dims here */
			_do_hardware_poll(level+1, coords, bgqsys);
		}
		return;
	}
	/* block_state_mutex may be needed in some of these functions,
	 * so lock it first to avoid dead lock */
	slurm_mutex_lock(&block_state_mutex);
	slurm_mutex_lock(&ba_system_mutex);
	if ((ba_mp = coord2ba_mp(coords)))
		_handle_midplane_update(bgqsys, ba_mp, &delete_list);
	slurm_mutex_unlock(&ba_system_mutex);
	slurm_mutex_unlock(&block_state_mutex);

	bg_status_process_kill_job_list(kill_job_list);

	if (delete_list) {
		bool delete_it = 0;
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
			delete_it = 1;
		free_block_list(NO_VAL, delete_list, delete_it, 0);
		list_destroy(delete_list);
	}
}

static void *_poll(void *no_data)
{
	event_handler_t event_hand;
	static time_t last_ran = 0;
	time_t curr_time;

	while (bridge_status_inited) {
		//debug("polling waiting until realtime dies");
		slurm_mutex_lock(&rt_mutex);
		if (!bridge_status_inited) {
			slurm_mutex_unlock(&rt_mutex);
			break;
		}
		//debug("polling taking over, realtime is dead");
		curr_time = time(NULL);
		if (blocks_are_created)
			_do_block_poll();
		/* only do every 30 seconds */
		if ((curr_time - 30) >= last_ran) {
			uint16_t coords[SYSTEM_DIMENSIONS];
			_do_hardware_poll(0, coords,
					  bridge_get_compute_hardware());
			last_ran = time(NULL);
		}

		slurm_mutex_unlock(&rt_mutex);
		/* This means we are doing outside of the thread so
		   break */
		if (initial_poll)
			break;
		sleep(1);
	}

	return NULL;
}

void event_handler::handleRealtimeStartedRealtimeEvent(
	const RealtimeStartedEventInfo& event)
{
	if (!rt_running) {
		uint16_t coords[SYSTEM_DIMENSIONS];
		slurm_mutex_lock(&rt_mutex);
		info("RealTime server started backup!");
		rt_running = 1;
		/* To make sure we don't have any missing state */
		if (blocks_are_created)
			_do_block_poll();
		/* only do every 30 seconds */
		_do_hardware_poll(0, coords, bridge_get_compute_hardware());
	}
}

void event_handler::handleRealtimeEndedRealtimeEvent(
	const RealtimeEndedEventInfo& event)
{
	if (rt_running) {
		rt_running = 0;
		slurm_mutex_unlock(&rt_mutex);
		info("RealTime server stopped serving info");
	}
}

void event_handler::handleBlockStateChangedRealtimeEvent(
        const BlockStateChangedEventInfo& event)
{
	bg_record_t *bg_record = NULL;
	const char *bg_block_id = event.getBlockName().c_str();

	if (!bg_lists->main)
		return;

	/* Always lock the slurmctld before locking the
	 * block_state_mutex to avoid deadlock. */
	lock_slurmctld(job_read_lock);
	slurm_mutex_lock(&block_state_mutex);
	bg_record = find_bg_record_in_list(bg_lists->main, bg_block_id);
	if (!bg_record) {
		unlock_slurmctld(job_read_lock);
		slurm_mutex_unlock(&block_state_mutex);
		info("bridge_status: bg_record %s isn't in the main list",
		     bg_block_id);
		return;
	}

	bg_status_update_block_state(bg_record,
				     bridge_translate_status(event.getStatus()),
				     kill_job_list);

	slurm_mutex_unlock(&block_state_mutex);
	unlock_slurmctld(job_read_lock);

	bg_status_process_kill_job_list(kill_job_list);

	last_bg_update = time(NULL);
}

void event_handler::handleMidplaneStateChangedRealtimeEvent(
	const MidplaneStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	ba_mp_t *ba_mp;
	int dim;

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	slurm_mutex_lock(&ba_system_mutex);
	ba_mp = coord2ba_mp(coords);

	if (!ba_mp) {
		error("Midplane %s, state went from '%s' to '%s', "
		      "but is not in our system",
		      event.getLocation().c_str(),
		      bridge_hardware_state_string(event.getPreviousState()),
		      bridge_hardware_state_string(event.getState()));
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	if (event.getState() == Hardware::Available) {
		/* Don't do anything, wait for admin to fix things,
		 * just note things are better. */

		info("Midplane %s(%s), has returned to service",
		     event.getLocation().c_str(),
		     ba_mp->coord_str);
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	/* Else mark the midplane down */
	_handle_bad_midplane(ba_mp->coord_str, event.getState());
	slurm_mutex_unlock(&ba_system_mutex);
	return;

}

void event_handler::handleSwitchStateChangedRealtimeEvent(
	const SwitchStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *ba_mp;


	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	dim = event.getDimension();
	slurm_mutex_lock(&ba_system_mutex);
	ba_mp = coord2ba_mp(coords);

	if (!ba_mp) {
		error("Switch in dim '%d' on Midplane %s, state "
		      "went from '%s' to '%s', but is not in our system",
		      dim, event.getMidplaneLocation().c_str(),
		      bridge_hardware_state_string(event.getPreviousState()),
		      bridge_hardware_state_string(event.getState()));
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	if (event.getState() == Hardware::Available) {
		/* Don't do anything, wait for admin to fix things,
		 * just note things are better. */

		info("Switch in dim '%u' on Midplane %s(%s), "
		     "has returned to service",
		     dim, event.getMidplaneLocation().c_str(),
		     ba_mp->coord_str);
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	/* Else mark the midplane down */
	_handle_bad_switch(dim, ba_mp->coord_str, event.getState());
	slurm_mutex_unlock(&ba_system_mutex);

	return;
}

void event_handler::handleNodeBoardStateChangedRealtimeEvent(
	const NodeBoardStateChangedEventInfo& event)
{
	/* When dealing with non-pointers these variables don't work
	   out correctly, so copy them.
	*/
	const char *mp_name = xstrdup(event.getLocation().substr(0,6).c_str());
	const char *nb_name = xstrdup(event.getLocation().substr(7,3).c_str());
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *ba_mp;

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	slurm_mutex_lock(&ba_system_mutex);
	ba_mp = coord2ba_mp(coords);

	if (!ba_mp) {
		error("Nodeboard '%s' on Midplane %s (%s), state went from "
		      "'%s' to '%s', but is not in our system",
		      nb_name, mp_name, event.getLocation().c_str(),
		      bridge_hardware_state_string(event.getPreviousState()),
		      bridge_hardware_state_string(event.getState()));
		xfree(nb_name);
		xfree(mp_name);
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	if (event.getState() == Hardware::Available) {
		/* Don't do anything, wait for admin to fix things,
		 * just note things are better. */

		info("Nodeboard '%s' on Midplane %s(%s), "
		     "has returned to service",
		     nb_name, mp_name,
		     ba_mp->coord_str);
		xfree(nb_name);
		xfree(mp_name);
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}

	_handle_bad_nodeboard(nb_name, ba_mp->coord_str,
			      event.getState(), NULL, 0);
	xfree(nb_name);
	xfree(mp_name);
	slurm_mutex_unlock(&ba_system_mutex);

	return;
}

void event_handler::handleNodeStateChangedRealtimeEvent(
	const NodeStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *ba_mp;
	List delete_list = NULL;

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	/* block_state_mutex may be needed in _handle_node_change,
	 * so lock it first to avoid dead lock */
	slurm_mutex_lock(&block_state_mutex);
	slurm_mutex_lock(&ba_system_mutex);
	ba_mp = coord2ba_mp(coords);

	if (!ba_mp) {
		error("Node '%s' on Midplane %s, state went from '%s' to '%s',"
		      "but is not in our system",
		      event.getLocation().c_str(),
		      event.getLocation().substr(0,6).c_str(),
		      bridge_hardware_state_string(event.getPreviousState()),
		      bridge_hardware_state_string(event.getState()));
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);
		return;
	}

	info("Node '%s' on Midplane %s, state went from '%s' to '%s'",
	     event.getLocation().c_str(), ba_mp->coord_str,
	     bridge_hardware_state_string(event.getPreviousState()),
	     bridge_hardware_state_string(event.getState()));

	_handle_node_change(ba_mp, event.getLocation(), event.getState(),
			    &delete_list);
	slurm_mutex_unlock(&ba_system_mutex);
	slurm_mutex_unlock(&block_state_mutex);

	bg_status_process_kill_job_list(kill_job_list);

	if (delete_list) {
		/* The only reason blocks are added to this list is if
		   there are missing cnodes on the block so remove
		   them from the mix.
		*/
		free_block_list(NO_VAL, delete_list, 1, 0);
		list_destroy(delete_list);
	}

	return;
}

void event_handler::handleTorusCableStateChangedRealtimeEvent(
	const TorusCableStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getFromMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *from_ba_mp;
	List delete_list = NULL;

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	dim = event.getDimension();

	/* block_state_mutex may be needed in _handle_cable_change,
	 * so lock it first to avoid dead lock */
	slurm_mutex_lock(&block_state_mutex);
	slurm_mutex_lock(&ba_system_mutex);
	from_ba_mp = coord2ba_mp(coords);
	if (!from_ba_mp) {
		error("Cable in dim '%d' on Midplane %s, state "
		      "went from '%s' to '%s', but is not in our system",
		      dim, event.getFromMidplaneLocation().c_str(),
		      bridge_hardware_state_string(event.getPreviousState()),
		      bridge_hardware_state_string(event.getState()));
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);
		return;
	}

	/* Else mark the midplane down */
	_handle_cable_change(dim, from_ba_mp, event.getState(), &delete_list);
	slurm_mutex_unlock(&ba_system_mutex);
	slurm_mutex_unlock(&block_state_mutex);

	if (delete_list) {
		bool delete_it = 0;
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
			delete_it = 1;
		free_block_list(NO_VAL, delete_list, delete_it, 0);
		list_destroy(delete_list);
	}
	return;
}

#endif

extern int bridge_status_init(void)
{
	if (bridge_status_inited)
		return SLURM_SUCCESS;

	bridge_status_inited = true;

#if defined HAVE_BG_FILES
	pthread_attr_t thread_attr;

	if (!kill_job_list)
		kill_job_list = bg_status_create_kill_job_list();

	/* get initial state */
	_poll(NULL);
	initial_poll = false;

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
	slurm_mutex_lock(&rt_mutex);

	/* make the rt connection end. */
	_bridge_status_disconnect();

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
	slurm_mutex_unlock(&rt_mutex);
	pthread_mutex_destroy(&rt_mutex);
	delete(rt_client_ptr);
#endif
	return SLURM_SUCCESS;
}

/* This needs to have block_state_mutex locked before hand. */
extern int bridge_status_update_block_list_state(List block_list)
{
	int updated = 0;
#if defined HAVE_BG_FILES
	uint16_t real_state, state;
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;

	itr = list_iterator_create(block_list);
	while ((bg_record = (bg_record_t *) list_next(itr))) {
		BlockFilter filter;
		Block::Ptrs vec;
		if (bg_record->magic != BLOCK_MAGIC) {
			/* block is gone */
			list_remove(itr);
			continue;
		} else if (!bg_record->bg_block_id)
			continue;

		filter.setName(string(bg_record->bg_block_id));

		vec = bridge_get_blocks(filter);
		if (vec.empty()) {
			debug("bridge_status_update_block_list_state: "
			      "block %s not found, removing from slurm",
			      bg_record->bg_block_id);
			/* block is gone? */
			list_remove(itr);
			continue;
		}
		const Block::Ptr &block_ptr = *(vec.begin());

		real_state = bg_record->state & (~BG_BLOCK_ERROR_FLAG);
		state = bridge_translate_status(
			block_ptr->getStatus().toValue());

		if (real_state != state) {
			if (bg_record->state & BG_BLOCK_ERROR_FLAG)
				state |= BG_BLOCK_ERROR_FLAG;

			debug("freeing state of Block %s was %s and now is %s",
			      bg_record->bg_block_id,
			      bg_block_state_string(bg_record->state),
			      bg_block_state_string(state));

			bg_record->state = state;
			updated = 1;
		}
	}
	list_iterator_destroy(itr);
#endif
	return updated;
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
