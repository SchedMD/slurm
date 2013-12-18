/*****************************************************************************\
 *  bridge_status.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
static bool rt_waiting = false;

static void *_before_rt_poll(void *no_data);

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
static pthread_t before_rt_thread;
static pthread_t real_time_thread;
static pthread_t poll_thread;
static pthread_t action_poll_thread;
static bgsched::realtime::Client *rt_client_ptr = NULL;
pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t get_hardware_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* ba_system_mutex and block_state_mutex must be locked before this.
 * If the state == Hardware::SoftwareFailure job lock must be locked
 * as well. */
static void _handle_soft_error_midplane(ba_mp_t *ba_mp,
					EnumWrapper<Hardware::State> state,
					List *delete_list, bool print_debug)
{
	ListIterator itr, itr2;
	bg_record_t *bg_record;

	if ((state != Hardware::Available)
	    && (state != Hardware::SoftwareFailure)) {
		error("_handle_soft_error_midplane: The state %s isn't "
		      "handled here",
		      bridge_hardware_state_string(state.toValue()));
		return;
	}

	assert(ba_mp);

	if (!ba_mp->cnode_err_bitmap)
		ba_mp->cnode_err_bitmap = bit_alloc(bg_conf->mp_cnode_cnt);

	if (state == Hardware::SoftwareFailure)
		bit_nset(ba_mp->cnode_err_bitmap, 0,
			 bit_size(ba_mp->cnode_err_bitmap)-1);
	else {
		if (bit_ffs(ba_mp->cnode_err_bitmap) == -1)
			return;

		bit_nclear(ba_mp->cnode_err_bitmap, 0,
			   bit_size(ba_mp->cnode_err_bitmap)-1);
	}

	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = (bg_record_t *)list_next(itr))) {
		float err_ratio;
		ba_mp_t *found_ba_mp;

		if (!bit_test(bg_record->mp_bitmap, ba_mp->index))
			continue;

		itr2 = list_iterator_create(bg_record->ba_mp_list);
		while ((found_ba_mp = (ba_mp_t *)list_next(itr2))) {
			int cnt_diff;

			if (!found_ba_mp->used
			    || (found_ba_mp->index != ba_mp->index))
				continue;

			if (!found_ba_mp->cnode_err_bitmap)
				found_ba_mp->cnode_err_bitmap =
					bit_alloc(bg_conf->mp_cnode_cnt);

			if (state == Hardware::SoftwareFailure) {
				/* Check to make sure we haven't already got
				   some of these or not through the cnode catch.
				*/
				if ((cnt_diff = bit_clear_count(
					     found_ba_mp->cnode_err_bitmap))) {
					bit_nset(found_ba_mp->cnode_err_bitmap,
						 0,
						 bit_size(found_ba_mp->
							  cnode_err_bitmap)-1);
					if (bg_record->cnode_cnt
					    < bg_conf->mp_cnode_cnt)
						bg_record->cnode_err_cnt =
							bg_record->cnode_cnt;
					else
						bg_record->cnode_err_cnt +=
							cnt_diff;
					bg_status_remove_jobs_from_failed_block(
						bg_record, ba_mp->index,
						1, delete_list, &kill_job_list);
				}
			} else {
				/* Check to make sure we haven't already got
				   some of these or not through the cnode catch.
				*/
				if ((cnt_diff = bit_set_count(
					     found_ba_mp->cnode_err_bitmap))) {
					bit_nclear(found_ba_mp->
						   cnode_err_bitmap, 0,
						   bit_size(
							   found_ba_mp->
							   cnode_err_bitmap)-1);
					if (bg_record->cnode_cnt
					    < bg_conf->mp_cnode_cnt)
						bg_record->cnode_err_cnt = 0;
					else
						bg_record->cnode_err_cnt -=
							cnt_diff;
				}
			}
			break;
		}
		list_iterator_destroy(itr2);

		if ((int32_t)bg_record->cnode_err_cnt >
		    (int32_t)bg_record->cnode_cnt) {
			error("_handle_soft_error_midplane: "
			      "got more cnodes in error than are "
			      "possible %d > %d",
			      bg_record->cnode_err_cnt, bg_record->cnode_cnt);
			bg_record->cnode_err_cnt = bg_record->cnode_cnt;
		} else if ((int32_t)bg_record->cnode_err_cnt < 0) {
			error("_handle_soft_error_midplane: "
			      "cnode err underflow %d < 0",
			      bg_record->cnode_err_cnt);
			bg_record->cnode_err_cnt = 0;
		}

		err_ratio = (float)bg_record->cnode_err_cnt
			/ (float)bg_record->cnode_cnt;
		bg_record->err_ratio = err_ratio * 100;

		/* handle really small ratios (Shouldn't be needed
		 * here but here just to be safe) */
		if (!bg_record->err_ratio && bg_record->cnode_err_cnt)
			bg_record->err_ratio = 1;

		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
			debug("_handle_soft_error_midplane: "
			      "count in error for %s is %u with ratio at %u",
			      bg_record->bg_block_id,
			      bg_record->cnode_err_cnt,
			      bg_record->err_ratio);
		last_bg_update = time(NULL);
	}
	list_iterator_destroy(itr);
}

/* ba_system_mutex && block_state_mutex must be unlocked before this */
static void _handle_bad_midplane(char *bg_down_node,
				 EnumWrapper<Hardware::State> state,
				 bool print_debug)
{
	assert(bg_down_node);

	if (!node_already_down(bg_down_node)) {
		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
			error("Midplane %s, state went to '%s', "
			      "marking midplane down.",
			      bg_down_node,
			      bridge_hardware_state_string(state.toValue()));
		slurm_drain_nodes(
			bg_down_node,
			(char *)"select_bluegene: MMCS midplane not UP",
			slurm_get_slurm_user_id());
	}
}

static void _handle_bad_switch(int dim, const char *mp_coords,
			       EnumWrapper<Hardware::State> state,
			       bool block_state_locked, bool print_debug)
{
	char bg_down_node[128];

	assert(mp_coords);

	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, mp_coords);

	if (!node_already_down(bg_down_node)) {
		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
			error("Switch at dim '%d' on Midplane %s, "
			      "state went to '%s', marking midplane down.",
			      dim, bg_down_node,
			      bridge_hardware_state_string(state.toValue()));
		/* unlock mutex here since slurm_drain_nodes could produce
		   deadlock */
		slurm_mutex_unlock(&ba_system_mutex);
		if (block_state_locked)
			slurm_mutex_unlock(&block_state_mutex);
		slurm_drain_nodes(bg_down_node,
				  (char *)"select_bluegene: MMCS switch not UP",
				  slurm_get_slurm_user_id());
		if (block_state_locked)
			slurm_mutex_lock(&block_state_mutex);
		slurm_mutex_lock(&ba_system_mutex);
	}
}

/* job_read_lock && ba_system_mutex && block_state_mutex must be
 * unlocked before this */
static void _handle_bad_nodeboard(const char *nb_name, char* bg_down_node,
				  EnumWrapper<Hardware::State> state,
				  char *reason, bool print_debug)
{
	int io_start;
	int rc;

	assert(nb_name);
	assert(bg_down_node);

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

	rc = down_nodecard(bg_down_node, io_start, 0, reason);

	if (print_debug
	    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME)) {
		if (rc == SLURM_SUCCESS)
			debug("nodeboard %s on %s is in an error state '%s'",
			      nb_name, bg_down_node,
			      bridge_hardware_state_string(state.toValue()));
		else
			debug2("nodeboard %s on %s is in an error state '%s', "
			       "but error was returned when trying to make "
			       "it so",
			       nb_name, bg_down_node,
			       bridge_hardware_state_string(state.toValue()));
	}
	return;
}

/* ba_system_mutex && block_state_mutex must be locked before this */
static void _handle_node_change(ba_mp_t *ba_mp, const std::string& cnode_loc,
				EnumWrapper<Hardware::State> state,
				List *delete_list, bool print_debug)
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
		char bg_down_node[128];

		snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
			 bg_conf->slurm_node_prefix, ba_mp->coord_str);
		snprintf(nc_name, sizeof(nc_name), "N%d", nc_loc);
		snprintf(reason, sizeof(reason),
			 "_handle_node_change: On midplane %s nodeboard %s "
			 "had cnode %u%u%u%u%u(%s) go into an error state.",
			 bg_down_node,
			 nc_name,
			 cnode_coords[0],
			 cnode_coords[1],
			 cnode_coords[2],
			 cnode_coords[3],
			 cnode_coords[4],
			 cnode_loc.c_str());
		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
			error("%s", reason);
		/* unlock mutex here since _handle_bad_nodeboard could produce
		   deadlock */
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);
		unlock_slurmctld(job_read_lock);
		_handle_bad_nodeboard(nc_name, bg_down_node,
				      state, reason, print_debug);
		lock_slurmctld(job_read_lock);
		slurm_mutex_lock(&block_state_mutex);
		slurm_mutex_lock(&ba_system_mutex);
	}

	if (!changed)
		return;
	last_bg_update = time(NULL);
	if (print_debug
	    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
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
		/* if a block has a free_cnt we still need to apply this */
		if (!bit_test(bg_record->mp_bitmap, ba_mp->index))
			continue;
		itr2 = list_iterator_create(bg_record->ba_mp_list);
		while ((found_ba_mp = (ba_mp_t *)list_next(itr2))) {
			float err_ratio;

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
			} else if (set) {
				bit_clear(found_ba_mp->cnode_err_bitmap, inx);
				if (bg_record->cnode_err_cnt)
					bg_record->cnode_err_cnt--;
			}

			err_ratio = (float)bg_record->cnode_err_cnt
				/ (float)bg_record->cnode_cnt;
                        bg_record->err_ratio = err_ratio * 100;

			/* handle really small ratios */
			if (!bg_record->err_ratio && bg_record->cnode_err_cnt)
				bg_record->err_ratio = 1;

			if (print_debug
			    && !(bg_conf->slurm_debug_flags
				 & DEBUG_FLAG_NO_REALTIME))
				debug("count in error for %s is %u "
				      "with ratio at %u",
				      bg_record->bg_block_id,
				      bg_record->cnode_err_cnt,
				      bg_record->err_ratio);

			/* If the state is available no reason to go
			 * kill jobs so just break out here instead.
			 *
			 * Also if we already issued a free on this
			 * block there could of been a new job added
			 * that is waiting for the block to be freed
			 * so don't go around and fail it before it starts.
			 */
			if (state == Hardware::Available || bg_record->free_cnt)
				break;

			/* If the state is Hardware::Error send NULL
			   since we do not want to free the block that
			   we just put into an Error state above that
			   might not be running a job anymore.
			*/
			bg_status_remove_jobs_from_failed_block(
				bg_record, inx, 0,
				(state != Hardware::Error) ? delete_list : NULL,
				&kill_job_list);

			break;
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);
}

static void _handle_cable_change(int dim, ba_mp_t *ba_mp,
				 EnumWrapper<Hardware::State> state,
				 List *delete_list, bool print_debug)
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
		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
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

		if (print_debug
		    && !(bg_conf->slurm_debug_flags & DEBUG_FLAG_NO_REALTIME))
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
			if (bg_record->destroy)
				continue;
			if (bg_record->mp_count == 1)
				continue;
			if (!bit_test(bg_record->mp_bitmap, ba_mp->index))
				continue;
			if (!bit_test(bg_record->mp_bitmap, next_ba_mp->index))
				continue;
			if (!*delete_list)
				*delete_list = list_create(NULL);

			debug("_handle_cable_change: going to "
			      "remove block %s, bad underlying cable.",
			      bg_record->bg_block_id);
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
			pthread_attr_t thread_attr;
			slurm_attr_init(&thread_attr);
			if (pthread_create(&before_rt_thread, &thread_attr,
					   _before_rt_poll, NULL))
				fatal("pthread_create error %m");
			slurm_attr_destroy(&thread_attr);

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
		if (rt_waiting || slurmctld_config.shutdown_time)
			break;
	}
	slurm_mutex_unlock(&block_state_mutex);
	unlock_slurmctld(job_read_lock);

	bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);

	if (updated == 1)
		last_bg_update = time(NULL);
}

#ifdef HAVE_BG_GET_ACTION
static void _do_block_action_poll(void)
{
	bg_record_t *bg_record;
	ListIterator itr;
	BlockFilter filter;
	BlockFilter::Statuses statuses;
	Block::Ptrs vec;
	List kill_list = NULL;

	if (!bg_lists->main)
		return;

	/* IBM says only asking for initialized blocks is much more
	   efficient than asking for each block individually.
	*/
	statuses.insert(Block::Initialized);
	filter.setStatuses(&statuses);
	vec = bridge_get_blocks(filter);
	if (vec.empty())
		return;

	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	BOOST_FOREACH(const Block::Ptr& block_ptr, vec) {
		while ((bg_record = (bg_record_t *) list_next(itr))) {
			if ((bg_record->magic != BLOCK_MAGIC)
			    || !bg_record->bg_block_id
			    || (bg_record->state != BG_BLOCK_INITED)
			    || strcmp(bg_record->bg_block_id,
				      block_ptr->getName().c_str()))
				continue;

			bg_record->action = bridge_translate_action(
				block_ptr->getAction().toValue());

			if (!bg_record->reason
			    && (bg_record->action == BG_BLOCK_ACTION_FREE)
			    && (bg_record->state == BG_BLOCK_INITED)) {
				/* Set the reason to something so
				   admins know why things aren't working.
				*/
				bg_record->reason = xstrdup(
					"Block can't be used, it has an "
					"action item of 'D' on it.");
				bg_record_hw_failure(bg_record, &kill_list);
				last_bg_update = time(NULL);
			} else if (bg_record->reason
				   && (bg_record->action
				       != BG_BLOCK_ACTION_FREE)
				   && !(bg_record->state
					& BG_BLOCK_ERROR_FLAG)) {
				xfree(bg_record->reason);
				last_bg_update = time(NULL);
			}

			break;
		}
		if (slurmctld_config.shutdown_time)
			break;
		list_iterator_reset(itr);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);
	/* kill any jobs that need be killed */
	bg_record_post_hw_failure(&kill_list, 0);
}

static void *_block_action_poll(void *no_data)
{
	while (bridge_status_inited) {
		//debug("polling for actions");
		if (blocks_are_created)
			_do_block_action_poll();
		sleep(1);
	}

	return NULL;
}
#endif

/* Even though ba_mp should be coming from the main list
 * ba_system_mutex && block_state_mutex must be unlocked before
 * this.  Anywhere in this function where ba_mp is used should be
 * locked.
 */
static void _handle_midplane_update(ComputeHardware::ConstPtr bgq,
				    ba_mp_t *ba_mp, List *delete_list)
{
	Midplane::ConstPtr mp_ptr = bridge_get_midplane(bgq, ba_mp);
	int i;
	Dimension dim;
	char bg_down_node[128];

	if (!mp_ptr) {
		info("no midplane in the system at %s", ba_mp->coord_str);
		return;
	}

	/* Handle this here so we don't have to lock if we don't have too. */
	slurm_mutex_lock(&ba_system_mutex);
	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, ba_mp->coord_str);
	slurm_mutex_unlock(&ba_system_mutex);

	if (mp_ptr->getState() == Hardware::SoftwareFailure) {
		lock_slurmctld(job_read_lock);
		slurm_mutex_lock(&block_state_mutex);
		slurm_mutex_lock(&ba_system_mutex);
		_handle_soft_error_midplane(
			ba_mp, mp_ptr->getState(), delete_list, 0);
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);
		unlock_slurmctld(job_read_lock);
	} else if (mp_ptr->getState() != Hardware::Available) {
		_handle_bad_midplane(bg_down_node, mp_ptr->getState(), 0);
		/* no reason to continue */
		return;
	} else {
		slurm_mutex_lock(&block_state_mutex);
		slurm_mutex_lock(&ba_system_mutex);
		if (ba_mp->cnode_err_bitmap
		    && bit_ffs(ba_mp->cnode_err_bitmap) != -1)
			_handle_soft_error_midplane(
				ba_mp, mp_ptr->getState(), delete_list, 0);
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);

		Node::ConstPtrs vec = bridge_get_midplane_nodes(
			mp_ptr->getLocation());
		if (!vec.empty()) {
			/* This, by far, is the most time consuming
			   process in the polling (especially if there
			   are changes).  So lock/unlock on each one
			   so if there are other people waiting for
			   the locks they don't have to wait for all
			   this to finish.
			*/
			BOOST_FOREACH(const Node::ConstPtr& cnode_ptr, vec) {
				lock_slurmctld(job_read_lock);
				slurm_mutex_lock(&block_state_mutex);
				slurm_mutex_lock(&ba_system_mutex);
				_handle_node_change(ba_mp,
						    cnode_ptr->getLocation(),
						    cnode_ptr->getState(),
						    delete_list, 0);
				slurm_mutex_unlock(&ba_system_mutex);
				slurm_mutex_unlock(&block_state_mutex);
				unlock_slurmctld(job_read_lock);
				if (rt_waiting
				    || slurmctld_config.shutdown_time)
					return;
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
		    && (nb_ptr->getState() != Hardware::Available)) {
			_handle_bad_nodeboard(
				nb_ptr->getLocation().substr(7,3).c_str(),
				bg_down_node, nb_ptr->getState(), NULL, 0);
			if (rt_waiting || slurmctld_config.shutdown_time)
				return;
		}
	}

	for (dim=Dimension::A; dim<=Dimension::D; dim++) {
		Switch::ConstPtr switch_ptr = bridge_get_switch(mp_ptr, dim);
		if (switch_ptr) {
			if (switch_ptr->getState() != Hardware::Available) {
				_handle_bad_switch(dim,
						   bg_down_node,
						   switch_ptr->getState(),
						   1, 0);
				if (rt_waiting
				    || slurmctld_config.shutdown_time)
					return;
			} else {
				Cable::ConstPtr my_cable =
					switch_ptr->getCable();
				/* Dimensions of length 1 do not have a
				   cable. (duh).
				*/
				if (my_cable) {
					/* block_state_mutex may be
					 * needed in _handle_cable_change,
					 * so lock it first to avoid
					 * dead lock */
					slurm_mutex_lock(&block_state_mutex);
					slurm_mutex_lock(&ba_system_mutex);
					_handle_cable_change(
						dim, ba_mp,
						my_cable->getState(),
						delete_list, 0);
					slurm_mutex_unlock(&ba_system_mutex);
					slurm_mutex_unlock(&block_state_mutex);
					if (rt_waiting
					    || slurmctld_config.shutdown_time)
						return;
				}
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
			if (rt_waiting || slurmctld_config.shutdown_time)
				return;
		}
		return;
	}
	/* We are ignoring locks here to deal with speed.
	   _handle_midplane_update should handle the locks for us when
	   needed.  Since the ba_mp list doesn't get destroyed until
	   the very end this should be safe.
	*/
	if ((ba_mp = coord2ba_mp(coords)))
		_handle_midplane_update(bgqsys, ba_mp, &delete_list);

	bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);

	if (delete_list) {
		bool delete_it = 0;
		if (initial_poll && bg_conf->sub_mp_sys)
			delete_it = 1;

		free_block_list(NO_VAL, delete_list, 1, 0);
		list_destroy(delete_list);
	}
}

static void *_poll(void *no_data)
{
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
		if (!rt_waiting && blocks_are_created)
			_do_block_poll();
		/* only do every 30 seconds */
		if (!rt_waiting && ((curr_time - 30) >= last_ran)) {
			uint16_t coords[SYSTEM_DIMENSIONS];
			_do_hardware_poll(0, coords,
					  bridge_get_compute_hardware());
			last_ran = time(NULL);
		}

		slurm_mutex_unlock(&rt_mutex);
		sleep(1);
	}

	return NULL;
}

static void *_before_rt_poll(void *no_data)
{
	uint16_t coords[SYSTEM_DIMENSIONS];
	/* To make sure we don't have any missing state */
	if ((!rt_waiting || initial_poll) && blocks_are_created)
		_do_block_poll();
	/* Since the RealTime server could YoYo this could be called
	   many, many times.  bridge_get_compute_hardware is a heavy
	   function so to avoid it being called too many times we will
	   serialize things here.
	*/
	slurm_mutex_lock(&get_hardware_mutex);
	if (!rt_waiting || initial_poll)
		_do_hardware_poll(0, coords, bridge_get_compute_hardware());
	slurm_mutex_unlock(&get_hardware_mutex);
	/* If this was the first time through set to false so we
	   handle things differently on every other call.
	*/
	if (initial_poll)
		initial_poll = false;
	return NULL;
}

void event_handler::handleRealtimeStartedRealtimeEvent(
	const RealtimeStartedEventInfo& event)
{
	if (!rt_running && !rt_waiting) {
		pthread_attr_t thread_attr;
		/* If we are in the middle of polling, break out since
		   we are just going to do it again right after.
		*/
		rt_waiting = 1;
		slurm_mutex_lock(&rt_mutex);
		rt_waiting = 0;
		rt_running = 1;
		info("RealTime server started back up!");
		/* Since we need to exit this function for the
		   realtime server to start giving us info spawn a
		   thread that will do it for us in the background.
		*/
		slurm_attr_init(&thread_attr);
		if (pthread_create(&before_rt_thread, &thread_attr,
				   _before_rt_poll, NULL))
			fatal("pthread_create error %m");
		slurm_attr_destroy(&thread_attr);
	} else if (rt_waiting)
		info("Realtime server appears to have gone and come back "
		      "while we were trying to bring it back");
}

void event_handler::handleRealtimeEndedRealtimeEvent(
	const RealtimeEndedEventInfo& event)
{
	if (rt_running) {
		rt_running = 0;
		slurm_mutex_unlock(&rt_mutex);
		info("RealTime server stopped serving info");
	} else {
		info("RealTime server stopped serving info before "
		      "we gave it back control.");
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
		slurm_mutex_unlock(&block_state_mutex);
		unlock_slurmctld(job_read_lock);
		debug2("bridge_status: bg_record %s isn't in the main list",
		       bg_block_id);
		return;
	}

	bg_status_update_block_state(bg_record,
				     bridge_translate_status(event.getStatus()),
				     kill_job_list);

	slurm_mutex_unlock(&block_state_mutex);
	unlock_slurmctld(job_read_lock);

	bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);

	last_bg_update = time(NULL);
}

void event_handler::handleMidplaneStateChangedRealtimeEvent(
	const MidplaneStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	ba_mp_t *ba_mp;
	int dim;
	char bg_down_node[128];
	List delete_list = NULL;

	if (event.getPreviousState() == event.getState()) {
		debug("Switch previous state was same as current (%s - %s)",
		       bridge_hardware_state_string(event.getPreviousState()),
		       bridge_hardware_state_string(event.getState()));
		//return;
	}

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
		if (event.getPreviousState() == Hardware::SoftwareFailure) {
			slurm_mutex_lock(&block_state_mutex);
			slurm_mutex_lock(&ba_system_mutex);
			_handle_soft_error_midplane(ba_mp, event.getState(),
						    &delete_list, 1);
			slurm_mutex_unlock(&ba_system_mutex);
			slurm_mutex_unlock(&block_state_mutex);
		}
	} else if (event.getState() == Hardware::SoftwareFailure) {
		info("Midplane %s(%s), went into %s state",
		     event.getLocation().c_str(),
		     ba_mp->coord_str,
		     bridge_hardware_state_string(event.getState()));
		slurm_mutex_unlock(&ba_system_mutex);
		lock_slurmctld(job_read_lock);
		slurm_mutex_lock(&block_state_mutex);
		slurm_mutex_lock(&ba_system_mutex);
		_handle_soft_error_midplane(ba_mp, event.getState(),
					    &delete_list, 1);
		slurm_mutex_unlock(&ba_system_mutex);
		slurm_mutex_unlock(&block_state_mutex);
		unlock_slurmctld(job_read_lock);
		bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);
	} else {
		/* Else mark the midplane down */
		snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
			 bg_conf->slurm_node_prefix, ba_mp->coord_str);
		slurm_mutex_unlock(&ba_system_mutex);
		_handle_bad_midplane(bg_down_node, event.getState(), 1);
	}

	if (delete_list) {
		free_block_list(NO_VAL, delete_list, 0, 0);
		list_destroy(delete_list);
	}

	return;
}

void event_handler::handleSwitchStateChangedRealtimeEvent(
	const SwitchStateChangedEventInfo& event)
{
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *ba_mp;
	char bg_down_node[128];


	if (event.getPreviousState() == event.getState()) {
		debug("Switch previous state was same as current (%s - %s)",
		       bridge_hardware_state_string(event.getPreviousState()),
		       bridge_hardware_state_string(event.getState()));
		//return;
	}

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

	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, ba_mp->coord_str);
	slurm_mutex_unlock(&ba_system_mutex);

	/* Else mark the midplane down */
	_handle_bad_switch(dim, bg_down_node, event.getState(), 0, 1);

	return;
}

void event_handler::handleNodeBoardStateChangedRealtimeEvent(
	const NodeBoardStateChangedEventInfo& event)
{
	const char *mp_name;
	const char *nb_name;
	Coordinates ibm_coords = event.getMidplaneCoordinates();
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	ba_mp_t *ba_mp;
	char bg_down_node[128];

	if (event.getPreviousState() == event.getState()) {
		debug("Nodeboard previous state was same as current (%s - %s)",
		       bridge_hardware_state_string(event.getPreviousState()),
		       bridge_hardware_state_string(event.getState()));
		//return;
	}

	/* When dealing with non-pointers these variables don't work
	   out correctly, so copy them.
	*/
	mp_name = xstrdup(event.getLocation().substr(0,6).c_str());
	nb_name = xstrdup(event.getLocation().substr(7,3).c_str());

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

	snprintf(bg_down_node, sizeof(bg_down_node), "%s%s",
		 bg_conf->slurm_node_prefix, ba_mp->coord_str);
	slurm_mutex_unlock(&ba_system_mutex);

	_handle_bad_nodeboard(nb_name, bg_down_node, event.getState(), NULL, 1);
	xfree(nb_name);
	xfree(mp_name);

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

	if (event.getPreviousState() == event.getState()) {
		debug("Node previous state was same as current (%s - %s)",
		       bridge_hardware_state_string(event.getPreviousState()),
		       bridge_hardware_state_string(event.getState()));
		//return;
	}

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		coords[dim] = ibm_coords[dim];

	/* job_read_lock and block_state_mutex may be needed in
	 * _handle_node_change, so lock it first to avoid dead lock */
	lock_slurmctld(job_read_lock);
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
		unlock_slurmctld(job_read_lock);
		return;
	}

	info("Node '%s' on Midplane %s, state went from '%s' to '%s'",
	     event.getLocation().c_str(), ba_mp->coord_str,
	     bridge_hardware_state_string(event.getPreviousState()),
	     bridge_hardware_state_string(event.getState()));

	_handle_node_change(ba_mp, event.getLocation(), event.getState(),
			    &delete_list, 1);
	slurm_mutex_unlock(&ba_system_mutex);
	slurm_mutex_unlock(&block_state_mutex);
	unlock_slurmctld(job_read_lock);

	bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);

	if (delete_list) {
		free_block_list(NO_VAL, delete_list, 0, 0);
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

	if (event.getPreviousState() == event.getState()) {
		debug("Cable previous state was same as current (%s - %s)",
		       bridge_hardware_state_string(event.getPreviousState()),
		       bridge_hardware_state_string(event.getState()));
		//return;
	}

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

	/* Else mark the cable down */
	_handle_cable_change(dim, from_ba_mp, event.getState(),
			     &delete_list, 1);
	slurm_mutex_unlock(&ba_system_mutex);
	slurm_mutex_unlock(&block_state_mutex);

	if (delete_list) {
		free_block_list(NO_VAL, delete_list, 0, 0);
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

	rt_client_ptr = new(bgsched::realtime::Client);

	slurm_attr_init(&thread_attr);
	if (pthread_create(&real_time_thread, &thread_attr, _real_time, NULL))
		fatal("pthread_create error %m");
	slurm_attr_init(&thread_attr);
	if (pthread_create(&poll_thread, &thread_attr, _poll, NULL))
		fatal("pthread_create error %m");
	slurm_attr_init(&thread_attr);
#ifdef HAVE_BG_GET_ACTION
	if (pthread_create(&action_poll_thread, &thread_attr,
			   _block_action_poll, NULL))
		fatal("pthread_create error %m");
	slurm_attr_destroy(&thread_attr);
#endif
#endif
	return SLURM_SUCCESS;
}

extern int bridge_status_fini(void)
{
	if (!bridge_status_inited)
		return SLURM_ERROR;

	bridge_status_inited = false;

#if defined HAVE_BG_FILES
	rt_waiting = 1;
	/* make the rt connection end. */
	_bridge_status_disconnect();

	if (before_rt_thread) {
		pthread_join(before_rt_thread, NULL);
		before_rt_thread = 0;
	}

	if (real_time_thread) {
		pthread_join(real_time_thread, NULL);
		real_time_thread = 0;
	}

	if (poll_thread) {
		pthread_join(poll_thread, NULL);
		poll_thread = 0;
	}

	if (action_poll_thread) {
		pthread_join(action_poll_thread, NULL);
		action_poll_thread = 0;
	}

	if (kill_job_list) {
		list_destroy(kill_job_list);
		kill_job_list = NULL;
	}

	pthread_mutex_destroy(&rt_mutex);
	pthread_mutex_destroy(&get_hardware_mutex);

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
		if (!bridge_status_inited)
			break;
		else if (bg_record->magic != BLOCK_MAGIC) {
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
