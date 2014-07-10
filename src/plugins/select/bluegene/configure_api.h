/*****************************************************************************\
 *  configure_api.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef _BG_CONFIGURE_API_H_
#define _BG_CONFIGURE_API_H_

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/parse_spec.h"

#include "ba_common.h"

extern int bg_configure_init(void);
extern int bg_configure_fini(void);

/*
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 *
 * IN: node_info_msg_t * can be null,
 *     should be from slurm_load_node().
 * IN: load_bridge: whiether or not to get bridge information
 *
 * return: void.
 */
extern void bg_configure_ba_init(
	node_info_msg_t *node_info_ptr, bool load_bridge);

/*
 * destroy all the internal (global) data structs.
 */
extern void bg_configure_ba_fini(void);

/* Setup the wires on the system and the structures needed to create
 * blocks.  This should be called before trying to create blocks.
 */
extern void bg_configure_ba_setup_wires(void);

/*
 * Resets the virtual system to a virgin state.  If track_down_mps is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern void bg_configure_reset_ba_system(bool track_down_mps);

extern void bg_configure_destroy_ba_mp(void *ptr);

/* Convert PASS_FOUND_* into equivalent string
 * Caller MUST xfree() the returned value */
extern char *bg_configure_ba_passthroughs_string(uint16_t passthrough);

/*
 * set the mp in the internal configuration as in, or not in use,
 * along with the current state of the mp.
 *
 * IN ba_mp: ba_mp_t to update state
 * IN state: new state of ba_mp_t
 */
extern void bg_configure_ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state);

/*
 * Used to set all midplanes in a special used state except the ones
 * we are able to use in a new allocation.
 *
 * IN: bitmap of midplanes we do or do not want
 * IN: except - If true set all midplanes not set in the bitmap else
 *              set all midplanes that are set in the bitmap.
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Note: Need to call ba_reset_all_removed_mps before starting another
 * allocation attempt after
 */
extern int bg_configure_ba_set_removable_mps(bitstr_t *bitmap, bool except);

/*
 * Resets the virtual system to the pervious state before calling
 * ba_set_removable_mps.
 */
extern int bg_configure_ba_reset_all_removed_mps(void);

/*
 * create a block request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a block of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1).
 *
 * IN/OUT - ba_request: structure to allocate and fill in.
 *
 * ALL below IN's need to be set within the ba_request before the call
 * if you want them to be used.
 * ALL below OUT's are set and returned within the ba_request.
 * IN - avail_mp_bitmap: bitmap of usable midplanes.
 * IN - blrtsimage: BlrtsImage for this block if not default
 * IN - conn_type: connection type of request (TORUS or MESH or SMALL)
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN/OUT - geometry: requested/returned geometry of block
 * IN - linuximage: LinuxImage for this block if not default
 * IN - mloaderimage: MLoaderImage for this block if not default
 * OUT - passthroughs: if there were passthroughs used in the
 *       generation of the block.
 * IN - procs: Number of real processors requested
 * IN - RamDiskimage: RamDiskImage for this block if not default
 * IN - rotate: if true, allows rotation of block during fit
 * OUT - save_name: hostlist of midplanes used in block
 * IN/OUT - size: requested/returned count of midplanes in block
 * IN - start: geo location of where to start the allocation
 * IN - start_req: if set use the start variable to start at
 * return success of allocation/validation of params
 */
extern int bg_configure_new_ba_request(select_ba_request_t* ba_request);

/*
 * Try to allocate a block.
 *
 * IN - ba_request: allocation request
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_block will create the
 * list, but the caller must destroy it.
 *
 * return: success or error of request
 */
extern int bg_configure_allocate_block(
	select_ba_request_t* ba_request, List results);

/*
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int bg_configure_remove_block(List mps, bool is_small);

/* translate a string of at least AXYZ into a ba_mp_t ptr */
extern ba_mp_t *bg_configure_str2ba_mp(const char *coords);
/*
 * find a base blocks bg location (rack/midplane)
 */
extern ba_mp_t *bg_configure_loc2ba_mp(const char* mp_id);

extern ba_mp_t *bg_configure_coord2ba_mp(const uint16_t *coord);
extern char *bg_configure_give_geo(uint16_t *int_geo, int dims, bool with_sep);

extern s_p_hashtbl_t *bg_configure_config_make_tbl(char *filename);

extern void ba_configure_set_ba_debug_flags(uint64_t debug_flags);

#endif
