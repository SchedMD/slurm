/*****************************************************************************\
 *  dynamic_block.c - functions for creating blocks in a dynamic environment.
 *
 *  $Id: dynamic_block.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "bg_dynamic_block.h"

static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes);

static int _breakup_blocks(List block_list, List new_blocks,
			   select_ba_request_t *request, List my_block_list,
			   int cnodes, bool only_free, bool only_small);

/*
 * create_dynamic_block - create new block(s) to be used for a new
 * job allocation.
 * RET - a list of created block(s) or NULL on failure errno is set.
 */
extern List create_dynamic_block(List block_list,
				 select_ba_request_t *request,
				 List my_block_list,
				 bool track_down_nodes)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr, itr2;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	List results = NULL;
	List new_blocks = NULL;
	bitstr_t *my_bitmap = NULL;
	select_ba_request_t blockreq;
	int cnodes = request->procs / bg_conf->cpu_ratio;
	int orig_cnodes;
	uint16_t start_geo[SYSTEM_DIMENSIONS];

	if (cnodes < bg_conf->smallest_block)
		cnodes = bg_conf->smallest_block;
	orig_cnodes = cnodes;

	if (bg_conf->sub_blocks && (cnodes < bg_conf->mp_cnode_cnt)) {
		cnodes = bg_conf->mp_cnode_cnt;
		request->conn_type[0] = SELECT_TORUS;
	} else if (cnodes < bg_conf->smallest_block) {
		error("Can't create this size %d "
		      "on this system the smallest block is %u",
		      cnodes, bg_conf->smallest_block);
		goto finished;
	}
	memset(&blockreq, 0, sizeof(select_ba_request_t));
	memcpy(start_geo, request->geometry, sizeof(start_geo));

	/* We need to lock this just incase a blocks_overlap is called
	   which will in turn reset and set the system as it sees fit.
	*/
	slurm_mutex_lock(&block_state_mutex);
	if (my_block_list) {
		reset_ba_system(track_down_nodes);
		itr = list_iterator_create(my_block_list);
		while ((bg_record = list_next(itr))) {
			if (bg_record->magic != BLOCK_MAGIC) {
				/* This should never happen since we
				   only call this on copies of blocks
				   and we check on this during the
				   copy.
				*/
				error("create_dynamic_block: "
				      "got a block with bad magic?");
				continue;
			}
			if (bg_record->free_cnt) {
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK) {
					int dim;
					char start_geo[SYSTEM_DIMENSIONS+1];
					char geo[SYSTEM_DIMENSIONS+1];
					for (dim=0; dim<SYSTEM_DIMENSIONS;
					     dim++) {
						start_geo[dim] = alpha_num[
							bg_record->start[dim]];
						geo[dim] = alpha_num[
							bg_record->geo[dim]];
					}
					start_geo[dim] = '\0';
					geo[dim] = '\0';
					info("not adding %s(%s) %s %s %s %u "
					     "(free_cnt)",
					     bg_record->bg_block_id,
					     bg_record->mp_str,
					     bg_block_state_string(
						     bg_record->state),
					     start_geo,
					     geo,
					     bg_record->cnode_cnt);
				}
				continue;
			}

			if (!my_bitmap)
				my_bitmap = bit_alloc(
					bit_size(bg_record->mp_bitmap));

			if (!bit_super_set(bg_record->mp_bitmap, my_bitmap)) {
				bit_or(my_bitmap, bg_record->mp_bitmap);

				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK) {
					int dim;
					char start_geo[SYSTEM_DIMENSIONS+1];
					char geo[SYSTEM_DIMENSIONS+1];
					for (dim=0; dim<SYSTEM_DIMENSIONS;
					     dim++) {
						start_geo[dim] = alpha_num[
							bg_record->start[dim]];
						geo[dim] = alpha_num[
							bg_record->geo[dim]];
					}
					start_geo[dim] = '\0';
					geo[dim] = '\0';
					info("adding %s(%s) %s %s %s %u",
					     bg_record->bg_block_id,
					     bg_record->mp_str,
					     bg_block_state_string(
						     bg_record->state),
					     start_geo, geo,
					     bg_record->cnode_cnt);
				}
				if (check_and_set_mp_list(
					    bg_record->ba_mp_list)
				    == SLURM_ERROR) {
					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_BG_PICK)
						info("something happened in "
						     "the load of %s",
						     bg_record->bg_block_id);
					list_iterator_destroy(itr);
					FREE_NULL_BITMAP(my_bitmap);
					rc = SLURM_ERROR;
					goto finished;
				}
			} else {
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK) {
					int dim;
					char start_geo[SYSTEM_DIMENSIONS+1];
					char geo[SYSTEM_DIMENSIONS+1];
					for (dim=0; dim<SYSTEM_DIMENSIONS;
					     dim++) {
						start_geo[dim] = alpha_num[
							bg_record->start[dim]];
						geo[dim] = alpha_num[
							bg_record->geo[dim]];
					}
					start_geo[dim] = '\0';
					geo[dim] = '\0';
					info("not adding %s(%s) %s %s %s %u ",
					     bg_record->bg_block_id,
					     bg_record->mp_str,
					     bg_block_state_string(
						     bg_record->state),
					     start_geo,
					     geo,
					     bg_record->cnode_cnt);
				}
				/* just so we don't look at it later */
				bg_record->free_cnt = -1;
			}
		}
		list_iterator_destroy(itr);
		FREE_NULL_BITMAP(my_bitmap);
	} else {
		reset_ba_system(false);
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("No list was given");
	}

	if (request->avail_mp_bitmap)
		ba_set_removable_mps(request->avail_mp_bitmap, 1);

try_small_again:
	if (request->size==1 && cnodes < bg_conf->mp_cnode_cnt) {
		switch(cnodes) {
#ifdef HAVE_BGL
		case 32:
			blockreq.small32 = 4;
			blockreq.small128 = 3;
			break;
		case 128:
			blockreq.small128 = 4;
			break;
#else
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			blockreq.small256 = 1;
			break;
		case 256:
			blockreq.small256 = 2;
			break;
#endif
		default:
			debug("This size %d is unknown on this system", cnodes);
			goto finished;
			break;
		}

		/* Sort the list so the small blocks are in the order
		 * of ionodes. */
		list_sort(block_list, (ListCmpF)bg_record_cmpf_inc);
		request->conn_type[0] = SELECT_SMALL;
		new_blocks = list_create(destroy_bg_record);
		/* check only blocks that are free and small */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    cnodes, true, true)
		    == SLURM_SUCCESS)
			goto finished;

		/* check only blocks that are free and any size */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    cnodes, true, false)
		    == SLURM_SUCCESS)
			goto finished;

		/* check usable blocks that are small with any state */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    cnodes, false, true)
		    == SLURM_SUCCESS)
			goto finished;

		/* check all usable blocks */
		/* This check will result in unused, booted blocks to
		   be freed before looking at free space, so we will
		   just skip it.  If you want this kind of behavior
		   enable it.
		*/
		/* if (_breakup_blocks(block_list, new_blocks, */
		/* 		    request, my_block_list, */
		/* 		    cnodes, false, false) */
		/*     == SLURM_SUCCESS) */
		/* 	goto finished; */

		/* Re-sort the list back to the original order. */
		list_sort(block_list, (ListCmpF)bg_record_sort_aval_inc);
		list_destroy(new_blocks);
		new_blocks = NULL;
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("small block not able to be placed inside others");
	}

	//debug("going to create %d", request->size);
	if (!new_ba_request(request)) {
		if (request->geometry[0] != (uint16_t)NO_VAL) {
			char *geo = give_geo(request->geometry,
					     SYSTEM_DIMENSIONS, 1);
			error("Problems with request for size %d geo %s",
			      request->size, geo);
			xfree(geo);
		} else {
			error("Problems with request for size %d.  "
			      "No geo given.",
			      request->size);
		}
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto finished;
	}

	/* try on free midplanes */
	if (results)
		list_flush(results);
	else {
#ifdef HAVE_BGQ
		results = list_create(destroy_ba_mp);
#else
		results = list_create(NULL);
#endif
	}

	rc = allocate_block(request, results);
	/* This could be changed in allocate_block so set it back up */
	memcpy(request->geometry, start_geo, sizeof(start_geo));

	if (rc) {
		rc = SLURM_SUCCESS;
		goto setup_records;
	}

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("allocate failure for %d midplanes with free midplanes",
		     request->size);
	rc = SLURM_ERROR;

	if (!list_count(my_block_list) || !my_block_list)
		goto finished;

	/*Try to put block starting in the smallest of the exisiting blocks*/
	itr = list_iterator_create(my_block_list);
	itr2 = list_iterator_create(my_block_list);
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		bool is_small = 0;
		/* never check a block with a job running */
		if (bg_record->free_cnt
		    || ((bg_record->job_running != NO_JOB_RUNNING)
			|| (bg_record->job_list
			    && list_count(bg_record->job_list))))
			continue;

		/* Here we are only looking for the first
		   block on the midplane.  So either the count
		   is greater or equal than
		   bg_conf->mp_cnode_cnt or the first bit is
		   set in the ionode_bitmap.
		*/
		if (bg_record->cnode_cnt < bg_conf->mp_cnode_cnt) {
			bool found = 0;
			if (bit_ffs(bg_record->ionode_bitmap) != 0)
				continue;
			/* Check to see if we have other blocks in
			   this midplane that have jobs running.
			*/
			while ((found_record = list_next(itr2))) {
				/* Don't check free_cnt here since
				   if this block shares the same
				   midplane it will automatically be
				   -1. So just look for running jobs.
				*/
				if (((found_record->job_running
				      != NO_JOB_RUNNING)
				     || (found_record->job_list
					 && list_count(found_record->job_list)))
				    && bit_overlap(bg_record->mp_bitmap,
						   found_record->mp_bitmap)) {
					found = 1;
					break;
				}
			}
			list_iterator_reset(itr2);
			if (found)
				continue;
			is_small = 1;
		}

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("removing %s(%s) for request %d",
			     bg_record->bg_block_id,
			     bg_record->mp_str, request->size);

		remove_block(bg_record->ba_mp_list, is_small);
		if (results)
			list_flush(results);
		else {
#ifdef HAVE_BGQ
			results = list_create(destroy_ba_mp);
#else
			results = list_create(NULL);
#endif
		}

		rc = allocate_block(request, results);
		/* This could be changed in allocate_block so set it back up */
		memcpy(request->geometry, start_geo, sizeof(start_geo));
		if (rc) {
			rc = SLURM_SUCCESS;
			break;
		}

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("allocate failure for size %d midplanes",
			     request->size);
		rc = SLURM_ERROR;
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

setup_records:
	if (rc == SLURM_SUCCESS) {
		/*set up bg_record(s) here */
		new_blocks = list_create(destroy_bg_record);

		blockreq.save_name = request->save_name;
#ifdef HAVE_BGL
		blockreq.blrtsimage = request->blrtsimage;
#endif
		blockreq.linuximage = request->linuximage;
		blockreq.mloaderimage = request->mloaderimage;
		blockreq.ramdiskimage = request->ramdiskimage;
		memcpy(blockreq.start, request->start, sizeof(blockreq.start));
		memcpy(blockreq.conn_type, request->conn_type,
		       sizeof(blockreq.conn_type));

		add_bg_record(new_blocks, &results, &blockreq, 0, 0);
	}

finished:
	if (!new_blocks && orig_cnodes != cnodes) {
		cnodes = orig_cnodes;
		goto try_small_again;
	}

	if (request->avail_mp_bitmap
	    && (bit_ffc(request->avail_mp_bitmap) == -1))
		ba_reset_all_removed_mps();
	slurm_mutex_unlock(&block_state_mutex);

	/* reset the ones we mucked with */
	itr = list_iterator_create(my_block_list);
	while ((bg_record = (bg_record_t *) list_next(itr))) {
		if (bg_record->free_cnt == -1)
			bg_record->free_cnt = 0;
	}
	list_iterator_destroy(itr);

	xfree(request->save_name);

	if (results) {
		list_destroy(results);
		results = NULL;
	}

	errno = rc;

	return new_blocks;
}

extern bg_record_t *create_small_record(bg_record_t *bg_record,
					bitstr_t *ionodes, int size)
{
	bg_record_t *found_record = NULL;
	ba_mp_t *new_ba_mp = NULL;
	ba_mp_t *ba_mp = NULL;

	found_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
	found_record->magic = BLOCK_MAGIC;

	/* This will be a list containing jobs running on this
	   block */
	if (bg_conf->sub_blocks)
		found_record->job_list = list_create(NULL);
	found_record->job_running = NO_JOB_RUNNING;

#ifdef HAVE_BGL
	found_record->node_use = SELECT_COPROCESSOR_MODE;
	found_record->blrtsimage = xstrdup(bg_record->blrtsimage);
#endif
#ifdef HAVE_BG_L_P
	found_record->linuximage = xstrdup(bg_record->linuximage);
	found_record->ramdiskimage = xstrdup(bg_record->ramdiskimage);
#endif
	found_record->mloaderimage = xstrdup(bg_record->mloaderimage);

	if (bg_record->conn_type[0] >= SELECT_SMALL)
		found_record->conn_type[0] = bg_record->conn_type[0];
	else
		found_record->conn_type[0] = SELECT_SMALL;

	xassert(bg_conf->cpu_ratio);
	found_record->cpu_cnt = bg_conf->cpu_ratio * size;
	found_record->cnode_cnt = size;

	found_record->ionode_bitmap = bit_copy(ionodes);
	ba_set_ionode_str(found_record);

	found_record->ba_mp_list = list_create(destroy_ba_mp);

	slurm_mutex_lock(&ba_system_mutex);
	if (bg_record->ba_mp_list)
		ba_mp = list_peek(bg_record->ba_mp_list);
	if (!ba_mp) {
		if (bg_record->mp_str) {
			int j = 0, dim;
			char *nodes = bg_record->mp_str;
			uint16_t coords[SYSTEM_DIMENSIONS];
			while (nodes[j] != '\0') {
				if ((nodes[j] >= '0' && nodes[j] <= '9')
				    || (nodes[j] >= 'A' && nodes[j] <= 'Z')) {
					break;
				}
				j++;
			}
			if (nodes[j] && ((strlen(nodes)
					  - (j + SYSTEM_DIMENSIONS)) >= 0)) {
				for (dim = 0; dim < SYSTEM_DIMENSIONS;
				     dim++, j++)
					coords[dim] = select_char2coord(
						nodes[j]);
				ba_mp = coord2ba_mp(coords);
			}
			error("you gave me a list with no ba_mps using %s",
			      ba_mp->coord_str);
		} else {
			ba_mp = coord2ba_mp(found_record->start);
			error("you gave me a record with no ba_mps "
			      "and no nodes either using %s",
			      ba_mp->coord_str);
		}
	}

	xassert(ba_mp);

	new_ba_mp = ba_copy_mp(ba_mp);
	slurm_mutex_unlock(&ba_system_mutex);
	/* We need to have this node wrapped in Q to handle
	   wires correctly when creating around the midplane.
	*/
	ba_setup_mp(new_ba_mp, false, true);

	new_ba_mp->used = BA_MP_USED_TRUE;

	/* Create these now so we can deal with error cnodes if/when
	   they happen.  Since this is the easiest place to figure it
	   out for blocks that don't use the entire block */
	if ((new_ba_mp->cnode_bitmap =
	     ba_create_ba_mp_cnode_bitmap(found_record))) {
		new_ba_mp->cnode_err_bitmap = bit_alloc(bg_conf->mp_cnode_cnt);
		new_ba_mp->cnode_usable_bitmap =
			bit_copy(new_ba_mp->cnode_bitmap);
	}

	list_append(found_record->ba_mp_list, new_ba_mp);
	found_record->mp_count = 1;
	found_record->mp_str = xstrdup_printf(
		"%s%s",
		bg_conf->slurm_node_prefix, new_ba_mp->coord_str);

	process_nodes(found_record, false);

	/* Force small blocks to always be non-full system blocks.
	 * This really only plays a part on sub-midplane systems. */
	found_record->full_block = 0;

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("made small block of %s[%s]",
		     found_record->mp_str, found_record->ionode_str);

	return found_record;
}

/*********************** Local Functions *************************/

static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes)
{
	bool full_mp = false;
	bitoff_t start = 0;
	select_ba_request_t blockreq;

	memset(&blockreq, 0, sizeof(select_ba_request_t));

	switch(bg_record->cnode_cnt) {
#ifdef HAVE_BGL
	case 32:
		error("We got a 32 we should never have this");
		goto finished;
		break;
	case 128:
		switch(cnodes) {
		case 32:
			blockreq.small32 = 4;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		break;
	default:
		switch(cnodes) {
		case 32:
			blockreq.small32 = 4;
			blockreq.small128 = 3;
			break;
		case 128:
			blockreq.small128 = 4;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		full_mp = true;
		break;
#else
	case 16:
		error("We got a 16 we should never have this");
		goto finished;
		break;
	case 32:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		break;
	case 64:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		break;
	case 128:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		break;
	case 256:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		break;
	default:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			blockreq.small256 = 1;
			break;
		case 256:
			blockreq.small256 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->cnode_cnt);
			goto finished;
			break;
		}
		full_mp = true;
		break;
#endif
	}

	if (!full_mp && bg_record->ionode_bitmap) {
		if ((start = bit_ffs(bg_record->ionode_bitmap)) == -1)
			start = 0;
	}

#ifdef HAVE_BGL
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("Asking for %u 32CNBlocks, and %u 128CNBlocks "
		     "from a %u block, starting at ionode %d.",
		     blockreq.small32, blockreq.small128,
		     bg_record->cnode_cnt, start);
#else
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("Asking for %u 16CNBlocks, %u 32CNBlocks, "
		     "%u 64CNBlocks, %u 128CNBlocks, and %u 256CNBlocks "
		     "from a %u block, starting at ionode %d.",
		     blockreq.small16, blockreq.small32,
		     blockreq.small64, blockreq.small128,
		     blockreq.small256, bg_record->cnode_cnt, start);
#endif
	handle_small_record_request(new_blocks, &blockreq, bg_record, start);

finished:
	return SLURM_SUCCESS;
}

static int _breakup_blocks(List block_list, List new_blocks,
			   select_ba_request_t *request, List my_block_list,
			   int cnodes, bool only_free, bool only_small)
{
	int rc = SLURM_ERROR;
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL, bit_itr = NULL;
	int total_cnode_cnt=0;
	char start_char[SYSTEM_DIMENSIONS+1];
	bitstr_t *ionodes = bit_alloc(bg_conf->ionodes_per_mp);
	int curr_mp_bit = -1;
	int dim;

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("cpu_count=%d cnodes=%d o_free=%d o_small=%d",
		     request->procs, cnodes, only_free, only_small);

	switch(cnodes) {
	case 16:
		/* a 16 can go anywhere */
		break;
	case 32:
		bit_itr = list_iterator_create(bg_lists->valid_small32);
		break;
	case 64:
		bit_itr = list_iterator_create(bg_lists->valid_small64);
		break;
	case 128:
		bit_itr = list_iterator_create(bg_lists->valid_small128);
		break;
	case 256:
		bit_itr = list_iterator_create(bg_lists->valid_small256);
		break;
	default:
		error("We shouldn't be here with this size %d", cnodes);
		goto finished;
		break;
	}

	/* First try with free blocks a midplane or less.  Then try with the
	 * smallest blocks.
	 */
	itr = list_iterator_create(block_list);
	while ((bg_record = list_next(itr))) {
		/* If the free_cnt is -1 that just means we just
		   didn't add it to the system, in this case it is
		   probably a small block that we really should be
		   looking at.
		*/
		if (bg_record->free_cnt > 0) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("%s being freed by other job(s), skipping",
				     bg_record->bg_block_id);
			continue;
		}
		/* never look at a block if a job is running */
		if ((bg_record->job_running != NO_JOB_RUNNING)
		    || (bg_record->job_list && list_count(bg_record->job_list)))
			continue;
		/* on the third time through look for just a block
		 * that isn't used */

		/* check for free blocks on the first and second time */
		if (only_free && (bg_record->state != BG_BLOCK_FREE))
			continue;

		/* check small blocks first */
		if (only_small
		    && (bg_record->cnode_cnt >= bg_conf->mp_cnode_cnt))
			continue;

		if (request->avail_mp_bitmap &&
		    !bit_super_set(bg_record->mp_bitmap,
				   request->avail_mp_bitmap)) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("bg block %s has nodes not usable "
				     "by this job",
				     bg_record->bg_block_id);
			continue;
		}

		if (bg_record->cnode_cnt == cnodes) {
			ba_mp_t *ba_mp = NULL;
			if (bg_record->ba_mp_list)
				ba_mp = list_peek(bg_record->ba_mp_list);
			if (!ba_mp) {
				for (dim=0; dim<SYSTEM_DIMENSIONS; dim++)
					start_char[dim] = alpha_num[
						bg_record->start[dim]];
				start_char[dim] = '\0';
				request->save_name = xstrdup(start_char);
			} else
				request->save_name = xstrdup(ba_mp->coord_str);

			rc = SLURM_SUCCESS;
			goto finished;
		}
		/* lets see if we can combine some small ones */
		if (bg_record->cnode_cnt < cnodes) {
			char bitstring[BITSIZE];
			bitstr_t *bitstr = NULL;
			int num_over = 0;
			int num_cnodes = bg_record->cnode_cnt;
			int rec_mp_bit = bit_ffs(bg_record->mp_bitmap);

			if (curr_mp_bit != rec_mp_bit) {
				/* Got a different node than
				 * previously, since the list should
				 * be in order of nodes for small blocks
				 * just clear here since the last node
				 * doesn't have any more. */
				curr_mp_bit = rec_mp_bit;
				bit_nclear(ionodes, 0,
					   (bg_conf->ionodes_per_mp-1));
				total_cnode_cnt = 0;
			}

			/* On really busy systems we can get
			   overlapping blocks here.  If that is the
			   case only add that which doesn't overlap.
			*/
			if ((num_over = bit_overlap(
				     ionodes, bg_record->ionode_bitmap))) {
				/* Since the smallest block size is
				   the number of cnodes in an io node,
				   just multiply the num_over by that to
				   get the number of cnodes to remove.
				*/
				if ((num_cnodes -=
				     num_over * bg_conf->smallest_block) <= 0)
					continue;
			}
			bit_or(ionodes, bg_record->ionode_bitmap);

			/* check and see if the bits set are a valid
			   combo */
			if (bit_itr) {
				while ((bitstr = list_next(bit_itr))) {
					if (bit_super_set(ionodes, bitstr))
						break;
				}
				list_iterator_reset(bit_itr);
			}
			if (!bitstr) {
				bit_nclear(ionodes, 0,
					   (bg_conf->ionodes_per_mp-1));
				bit_or(ionodes, bg_record->ionode_bitmap);
				total_cnode_cnt = num_cnodes =
					bg_record->cnode_cnt;
			} else
				total_cnode_cnt += num_cnodes;

			bit_fmt(bitstring, BITSIZE, ionodes);
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("combine adding %s %s %d got %d set "
				     "ionodes %s total is %s",
				     bg_record->bg_block_id, bg_record->mp_str,
				     num_cnodes, total_cnode_cnt,
				     bg_record->ionode_str, bitstring);
			if (total_cnode_cnt == cnodes) {
				ba_mp_t *ba_mp = NULL;
				if (bg_record->ba_mp_list)
					ba_mp = list_peek(
						bg_record->ba_mp_list);
				if (!ba_mp) {
					for (dim=0; dim<SYSTEM_DIMENSIONS;
					     dim++)
						start_char[dim] = alpha_num[
							bg_record->start[dim]];
					start_char[dim] = '\0';
					request->save_name =
						xstrdup(start_char);
				} else
					request->save_name =
						xstrdup(ba_mp->coord_str);

				if (!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;
				}

				bg_record = create_small_record(bg_record,
								ionodes,
								cnodes);
				list_append(new_blocks, bg_record);

				rc = SLURM_SUCCESS;
				goto finished;
			}
			continue;
		}
		/* we found a block that is bigger than requested */
		break;
	}

	if (bg_record) {
		ba_mp_t *ba_mp = NULL;
		if (bg_record->ba_mp_list)
			ba_mp = list_peek(bg_record->ba_mp_list);
		if (!ba_mp) {
			for (dim=0; dim<SYSTEM_DIMENSIONS; dim++)
				start_char[dim] = alpha_num[
					bg_record->start[dim]];
			start_char[dim] = '\0';
			request->save_name = xstrdup(start_char);
		} else
			request->save_name = xstrdup(ba_mp->coord_str);
		/* It appears we don't need this original record
		 * anymore, just work off the copy if indeed it is a copy. */

		/* bg_record_t *found_record = NULL; */

		/* if (bg_record->original) { */
		/* 	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) */
		/* 		info("1 This was a copy %s", */
		/* 		     bg_record->bg_block_id); */
		/* 	found_record = bg_record->original; */
		/* } else { */
		/* 	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) */
		/* 		info("looking for original"); */
		/* 	found_record = find_org_in_bg_list( */
		/* 		bg_lists->main, bg_record); */
		/* } */
		if ((bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		    && bg_record->original
		    && (bg_record->original->magic != BLOCK_MAGIC)) {
			info("This record %s has bad magic, it must be "
			     "getting freed.  No worries it will all be "
			     "figured out later.",
			     bg_record->bg_block_id);
		}

		/* if (!found_record || found_record->magic != BLOCK_MAGIC) { */
		/* 	error("this record wasn't found in the list!"); */
		/* 	rc = SLURM_ERROR; */
		/* 	goto finished; */
		/* } */

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) {
			char tmp_char[256];
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("going to split %s, %s",
			     bg_record->bg_block_id,
			     tmp_char);
		}

		if (!my_block_list) {
			rc = SLURM_SUCCESS;
			goto finished;
		}
		_split_block(block_list, new_blocks, bg_record, cnodes);
		rc = SLURM_SUCCESS;
		goto finished;
	}

finished:
	if (bit_itr)
		list_iterator_destroy(bit_itr);

	FREE_NULL_BITMAP(ionodes);
	if (itr)
		list_iterator_destroy(itr);

	return rc;
}
