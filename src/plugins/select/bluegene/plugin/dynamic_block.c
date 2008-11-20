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
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "dynamic_block.h"

#ifdef HAVE_BGL
static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int procs);
#else
static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes);
#endif
static int _breakup_blocks(List block_list, List new_blocks,
			   ba_request_t *request, List my_block_list);

/*
 * create_dynamic_block - create new block(s) to be used for a new
 * job allocation.
 * RET - a list of created block(s) or NULL on failure errno is set.
 */
extern List create_dynamic_block(List block_list, 
				 ba_request_t *request, List my_block_list)
{
	int rc = SLURM_SUCCESS;
	
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	List results = NULL;
	List new_blocks = NULL;
	bitstr_t *my_bitmap = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	int i;
	blockreq_t blockreq;
	int cnodes = request->procs / bluegene_proc_ratio;

	if(cnodes < bluegene_smallest_block) {
		error("Can't create this size %d "
		      "on this system numpsets is %d",
		      request->procs,
		      bluegene_numpsets);
		goto finished;
	}
	memset(&blockreq, 0, sizeof(blockreq_t));

	slurm_mutex_lock(&block_state_mutex);
	if(my_block_list) {
		reset_ba_system(true);
		itr = list_iterator_create(my_block_list);
		while ((bg_record = list_next(itr))) {
			if(!my_bitmap) {
				my_bitmap = 
					bit_alloc(bit_size(bg_record->bitmap));
			}
				
			if(!bit_super_set(bg_record->bitmap, my_bitmap)) {
				bit_or(my_bitmap, bg_record->bitmap);
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					geo[i] = bg_record->geo[i];
				debug2("adding %s %c%c%c %c%c%c",
				       bg_record->nodes,
				       alpha_num[bg_record->start[X]],
				       alpha_num[bg_record->start[Y]],
				       alpha_num[bg_record->start[Z]],
				       alpha_num[geo[X]],
				       alpha_num[geo[Y]],
				       alpha_num[geo[Z]]);

				if(check_and_set_node_list(
					   bg_record->bg_block_list)
				   == SLURM_ERROR) {
					debug2("something happened in "
					       "the load of %s",
					       bg_record->bg_block_id);
					list_iterator_destroy(itr);
					FREE_NULL_BITMAP(my_bitmap);
					rc = SLURM_ERROR;
					goto finished;
				}
			}
		}
		list_iterator_destroy(itr);
		FREE_NULL_BITMAP(my_bitmap);
	} else {
		reset_ba_system(false);
		debug("No list was given");
	}

	if(request->avail_node_bitmap) {
 		char *nodes = NULL;
		bitstr_t *bitmap = bit_alloc(node_record_count);
		
		/* we want the bps that aren't in this partition to
		 * mark them as used
		 */
		bit_or(bitmap, request->avail_node_bitmap);
		bit_not(bitmap);
		nodes = bitmap2node_name(bitmap);
		
		//info("not using %s", nodes);
		removable_set_bps(nodes);

		xfree(nodes);
		FREE_NULL_BITMAP(bitmap);
	}
#ifdef HAVE_BGL
	if(request->size==1 && cnodes < bluegene_bp_node_cnt) {
		request->conn_type = SELECT_SMALL;
		if(request->procs == (procs_per_node/16)) {
			if(!bluegene_nodecard_ionode_cnt) {
				error("can't create this size %d "
				      "on this system numpsets is %d",
				      request->procs,
				      bluegene_numpsets);
				goto finished;
			}

			blockreq.small32=4;
			blockreq.small128=3;
		} else {
			if(!bluegene_quarter_ionode_cnt) {
				error("can't create this size %d "
				      "on this system numpsets is %d",
				      request->procs,
				      bluegene_numpsets);
				goto finished;
			}
			blockreq.small128=4;
		}
		new_blocks = list_create(destroy_bg_record);
		if(_breakup_blocks(block_list, new_blocks, 
				   request, my_block_list)
		   != SLURM_SUCCESS) {
			list_destroy(new_blocks);
			new_blocks = NULL;
			debug2("small block not able to be placed");
			//rc = SLURM_ERROR;
		} else 
			goto finished;
	}
#else
	if(request->size==1 && cnodes < bluegene_bp_node_cnt) {
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
			error("This size %d is unknown on this system", cnodes);
			goto finished;
			break;
		}
	
		request->conn_type = SELECT_SMALL;
		new_blocks = list_create(destroy_bg_record);
		if(_breakup_blocks(block_list, new_blocks, 
				   request, my_block_list)
		   != SLURM_SUCCESS) {
			list_destroy(new_blocks);
			new_blocks = NULL;
			debug2("small block not able to be placed");
			//rc = SLURM_ERROR;
		} else 
			goto finished;
	}
#endif	
	if(request->conn_type == SELECT_NAV)
		request->conn_type = SELECT_TORUS;
	
	if(!new_ba_request(request)) {
		error("Problems with request for size %d geo %dx%dx%d", 
		      request->size,
		      request->geometry[X], 
		      request->geometry[Y], 
		      request->geometry[Z]);
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto finished;
	} 
	
	if(!list_count(block_list) || !my_block_list) {
		bg_record = NULL;
		goto no_list;
	}

	/*Try to put block starting in the smallest of the exisiting blocks*/
	if(!request->start_req) {
		itr = list_iterator_create(block_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			request->rotate_count = 0;
			request->elongate_count = 1;
		
			/* Here we are only looking for the first
			   block on the midplane.  So either the count
			   is greater or equal than
			   bluegene_bp_node_cnt or the first bit is
			   set in the ionode_bitmap.
			*/
			if(bg_record->job_running == NO_JOB_RUNNING 
#ifdef HAVE_BGL
			   && (bg_record->quarter == (uint16_t) NO_VAL
			       || (bg_record->quarter == 0 
				   && (bg_record->nodecard == (uint16_t) NO_VAL
				       || bg_record->nodecard == 0)))
#else
			   && ((bg_record->node_cnt >= bluegene_bp_node_cnt)
			       || (bit_ffs(bg_record->ionode_bitmap) == 0))
#endif
				) {
				
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
					request->start[i] = 
						bg_record->start[i];
				debug2("allocating %s %c%c%c %d",
				       bg_record->nodes,
				       alpha_num[request->start[X]],
				       alpha_num[request->start[Y]],
				       alpha_num[request->start[Z]],
				       request->size);
				request->start_req = 1;
				rc = SLURM_SUCCESS;
				if(results)
					list_delete_all(
						results,
						&empty_null_destroy_list, "");
				else
					results = list_create(NULL);
				if (!allocate_block(request, results)){
					debug2("1 allocate failure for size %d "
					       "base partitions", 
					       request->size);
					rc = SLURM_ERROR;
				} else 
					break;
			}
		}
		list_iterator_destroy(itr);
		
		request->start_req = 0;
		for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
			request->start[i] = (uint16_t) NO_VAL;
	}

no_list:
	if(!bg_record) {		
		rc = SLURM_SUCCESS;
		if(results)
			list_delete_all(results, 
					&empty_null_destroy_list, "");
		else
			results = list_create(NULL);
		if (!allocate_block(request, results)) {
			debug2("allocate failure for size %d base partitions", 
			       request->size);
			rc = SLURM_ERROR;
		}
	}

	if(rc != SLURM_SUCCESS) 
		goto finished;
	
	/*set up bg_record(s) here */
	new_blocks = list_create(destroy_bg_record);

	blockreq.block = request->save_name;
#ifdef HAVE_BGL
	blockreq.blrtsimage = request->blrtsimage;
#endif
	blockreq.linuximage = request->linuximage;
	blockreq.mloaderimage = request->mloaderimage;
	blockreq.ramdiskimage = request->ramdiskimage;
	blockreq.conn_type = request->conn_type;

	add_bg_record(new_blocks, results, &blockreq);

finished:
	reset_all_removed_bps();
	
	xfree(request->save_name);
	
	if(request->elongate_geos) {
		list_destroy(request->elongate_geos);
		request->elongate_geos = NULL;
	}
	if(results)
		list_destroy(results);
	errno = rc;
	slurm_mutex_unlock(&block_state_mutex);

	return new_blocks;
}

#ifdef HAVE_BGL
extern bg_record_t *create_small_record(bg_record_t *bg_record, 
					uint16_t quarter, uint16_t nodecard)
{
	bg_record_t *found_record = NULL;
	int small_size = 4;
	ba_node_t *new_ba_node = NULL;
	ba_node_t *ba_node = NULL;
	found_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
				
	found_record->job_running = NO_JOB_RUNNING;
	found_record->user_name = xstrdup(bg_record->user_name);
	found_record->user_uid = bg_record->user_uid;
	found_record->bg_block_list = list_create(destroy_ba_node);
	ba_node = list_peek(bg_record->bg_block_list);
	if(!ba_node) {
		hostlist_t hl = hostlist_create(bg_record->nodes);
		char *host = hostlist_shift(hl);
		hostlist_destroy(hl);
		found_record->nodes = xstrdup(host);
		free(host);
		error("you gave me a list with no ba_nodes using %s", 
		      found_record->nodes);
	} else {
		int i=0,j=0;
		new_ba_node = ba_copy_node(ba_node);
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
			for(j=0;j<NUM_PORTS_PER_NODE;j++) {
				ba_node->axis_switch[i].int_wire[j].used = 0;	
				if(i!=X) {
					if(j==3 || j==4) 
						ba_node->axis_switch[i].
							int_wire[j].
							used = 1;	
				}
				ba_node->axis_switch[i].int_wire[j].
					port_tar = j;
			}
		}
		list_append(found_record->bg_block_list, new_ba_node);
		found_record->bp_count = 1;
		found_record->nodes = xstrdup_printf(
			"%s%c%c%c", 
			bg_slurm_node_prefix, 
			alpha_num[ba_node->coord[X]],
			alpha_num[ba_node->coord[Y]],
			alpha_num[ba_node->coord[Z]]);
	}

	found_record->blrtsimage = xstrdup(bg_record->blrtsimage);
	found_record->linuximage = xstrdup(bg_record->linuximage);
	found_record->mloaderimage = xstrdup(bg_record->mloaderimage);
	found_record->ramdiskimage = xstrdup(bg_record->ramdiskimage);

	process_nodes(found_record, false);
				
	found_record->conn_type = SELECT_SMALL;
				
	found_record->node_use = SELECT_COPROCESSOR_MODE;

	if(nodecard != (uint16_t) NO_VAL)
		small_size = bluegene_bp_nodecard_cnt;
	found_record->cpus_per_bp = procs_per_node/small_size;
	found_record->node_cnt = bluegene_bp_node_cnt/small_size;
	found_record->quarter = quarter; 
	found_record->nodecard = nodecard;
	
	if(set_ionodes(found_record) == SLURM_ERROR) 
		error("couldn't create ionode_bitmap for %d.%d",
		      found_record->quarter, found_record->nodecard);
	return found_record;
}

#else
extern bg_record_t *create_small_record(bg_record_t *bg_record, 
					bitstr_t *ionodes, int size)
{
	bg_record_t *found_record = NULL;
	ba_node_t *new_ba_node = NULL;
	ba_node_t *ba_node = NULL;
#ifdef HAVE_BGL
	int small_size = 4;
#else
	char bitstring[BITSIZE];
#endif
	found_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
				
	found_record->job_running = NO_JOB_RUNNING;
	found_record->user_name = xstrdup(bg_record->user_name);
	found_record->user_uid = bg_record->user_uid;
	found_record->bg_block_list = list_create(destroy_ba_node);
	ba_node = list_peek(bg_record->bg_block_list);
	if(!ba_node) {
		hostlist_t hl = hostlist_create(bg_record->nodes);
		char *host = hostlist_shift(hl);
		hostlist_destroy(hl);
		found_record->nodes = xstrdup(host);
		free(host);
		error("you gave me a list with no ba_nodes using %s", 
		      found_record->nodes);
	} else {
		int i=0,j=0;
		new_ba_node = ba_copy_node(ba_node);
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++){
			for(j=0;j<NUM_PORTS_PER_NODE;j++) {
				ba_node->axis_switch[i].int_wire[j].used = 0;	
				if(i!=X) {
					if(j==3 || j==4) 
						ba_node->axis_switch[i].
							int_wire[j].
							used = 1;	
				}
				ba_node->axis_switch[i].int_wire[j].
					port_tar = j;
			}
		}
		list_append(found_record->bg_block_list, new_ba_node);
		found_record->bp_count = 1;
		found_record->nodes = xstrdup_printf(
			"%s%c%c%c", 
			bg_slurm_node_prefix, 
			alpha_num[ba_node->coord[X]],
			alpha_num[ba_node->coord[Y]],
			alpha_num[ba_node->coord[Z]]);
	}
#ifdef HAVE_BGL
	found_record->blrtsimage = xstrdup(bg_record->blrtsimage);
#endif
	found_record->linuximage = xstrdup(bg_record->linuximage);
	found_record->mloaderimage = xstrdup(bg_record->mloaderimage);
	found_record->ramdiskimage = xstrdup(bg_record->ramdiskimage);

	process_nodes(found_record, false);
				
	found_record->conn_type = SELECT_SMALL;
				
#ifdef HAVE_BGL
	found_record->node_use = SELECT_COPROCESSOR_MODE;
#endif
	xassert(bluegene_proc_ratio);
	found_record->cpus_per_bp = bluegene_proc_ratio * size;
	found_record->node_cnt = size;

	found_record->ionode_bitmap = bit_copy(ionodes);
	bit_fmt(bitstring, BITSIZE, found_record->ionode_bitmap);
	found_record->ionodes = xstrdup(bitstring);
	return found_record;
}
#endif

/*********************** Local Functions *************************/

#ifdef HAVE_BGL
static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int procs) 
{
	bg_record_t *found_record = NULL;
	bool full_bp = false; 
	int small_count = 0;
	int small_size = 0;
	uint16_t num_nodecard = 0, num_quarter = 0;
	int i;
	int node_cnt = 0;
	uint16_t quarter = 0;
	uint16_t nodecard = 0;

	if(bg_record->quarter == (uint16_t) NO_VAL)
		full_bp = true;
	
	if(procs == (procs_per_node/bluegene_bp_nodecard_cnt) 
	   && bluegene_nodecard_ionode_cnt) {
		num_nodecard=4;
		if(full_bp)
			num_quarter=3;
	} else if(full_bp) {
		num_quarter = 4;
	} else {
		error("you asked for something that was already this size");
		return SLURM_ERROR;
	}
	debug2("asking for %d 32s from a %d block",
	       num_nodecard, bg_record->node_cnt);
	small_count = num_nodecard+num_quarter; 

	/* break base partition up into 16 parts */
	small_size = bluegene_bp_node_cnt/bluegene_nodecard_node_cnt;
	node_cnt = 0;
	if(!full_bp)
		quarter = bg_record->quarter;
	else
		quarter = 0;
	nodecard = 0;
	for(i=0; i<small_count; i++) {
		if(i == num_nodecard) {
			/* break base partition up into 4 parts */
			small_size = 4;
		}
		
		if(small_size == 4)
			nodecard = (uint16_t)NO_VAL;
		else
			nodecard = i%4; 
		found_record = create_small_record(bg_record,
						   quarter,
						   nodecard);
		list_append(new_blocks, found_record);
				
		node_cnt += bluegene_bp_node_cnt/small_size;
		if(node_cnt == 128) {
			node_cnt = 0;
			quarter++;
		}
	}
		
	return SLURM_SUCCESS;
}

static int _breakup_blocks(List block_list, List new_blocks,
			   ba_request_t *request, List my_block_list)
{
	int rc = SLURM_ERROR;
	bg_record_t *bg_record = NULL;
	ListIterator itr;
	int proc_cnt=0;
	int total_proc_cnt=0;
	uint16_t last_quarter = (uint16_t) NO_VAL;
	char tmp_char[256];
	
	debug2("proc count = %d size = %d",
	       request->procs, request->size);
	
	itr = list_iterator_create(block_list);			
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(bg_record->job_running != NO_JOB_RUNNING)
			continue;
		if(bg_record->state != RM_PARTITION_FREE)
			continue;
		if (request->avail_node_bitmap &&
		    !bit_super_set(bg_record->bitmap,
				   request->avail_node_bitmap)) {
			debug2("bg block %s has nodes not usable by this job",
			       bg_record->bg_block_id);
			continue;
		}

		if(request->start_req) {
			if ((request->start[X] != bg_record->start[X])
			    || (request->start[Y] != bg_record->start[Y])
			    || (request->start[Z] != bg_record->start[Z])) {
				debug4("small got %c%c%c looking for %c%c%c",
				       alpha_num[bg_record->start[X]],
				       alpha_num[bg_record->start[Y]],
				       alpha_num[bg_record->start[Z]],
				       alpha_num[request->start[X]],
				       alpha_num[request->start[Y]],
				       alpha_num[request->start[Z]]);
				continue;
			}
			debug3("small found %c%c%c looking for %c%c%c",
			       alpha_num[bg_record->start[X]],
			       alpha_num[bg_record->start[Y]],
			       alpha_num[bg_record->start[Z]],
			       alpha_num[request->start[X]],
			       alpha_num[request->start[Y]],
			       alpha_num[request->start[Z]]);
		}
		proc_cnt = bg_record->bp_count * 
			bg_record->cpus_per_bp;
		if(proc_cnt == request->procs) {
			debug2("found it here %s, %s",
			       bg_record->bg_block_id,
			       bg_record->nodes);
			request->save_name = xstrdup_printf(
				"%c%c%c",
				alpha_num[bg_record->start[X]],
				alpha_num[bg_record->start[Y]],
				alpha_num[bg_record->start[Z]]);
			rc = SLURM_SUCCESS;
			goto finished;
		}
		if(bg_record->node_cnt > bluegene_bp_node_cnt)
			continue;
		if(proc_cnt < request->procs) {
			if(last_quarter != bg_record->quarter){
				last_quarter = bg_record->quarter;
				total_proc_cnt = proc_cnt;
			} else {
				total_proc_cnt += proc_cnt;
			}
			debug2("1 got %d on quarter %d",
			       total_proc_cnt, last_quarter);
			if(total_proc_cnt == request->procs) {
				request->save_name = xstrdup_printf(
					"%c%c%c",
					alpha_num[bg_record->start[X]],
					alpha_num[bg_record->start[Y]],
					alpha_num[bg_record->start[Z]]);
				if(!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;	
				}
						
				bg_record = create_small_record(
					bg_record,
					last_quarter,
					(uint16_t) NO_VAL);
				list_append(new_blocks, bg_record);
							
				rc = SLURM_SUCCESS;
				goto finished;	
			}
			continue;
		}
		break;
	}
	if(bg_record) {
		debug2("got one on the first pass");
		goto found_one;
	}
	list_iterator_reset(itr);
	last_quarter = (uint16_t) NO_VAL;
	while ((bg_record = (bg_record_t *) list_next(itr)) 
	       != NULL) {
		if(bg_record->job_running != NO_JOB_RUNNING)
			continue;
		if (request->avail_node_bitmap &&
		    !bit_super_set(bg_record->bitmap,
				   request->avail_node_bitmap)) {
			debug2("bg block %s has nodes not usable by this job",
			       bg_record->bg_block_id);
			continue;
		}

		if(request->start_req) {
			if ((request->start[X] != bg_record->start[X])
			    || (request->start[Y] != bg_record->start[Y])
			    || (request->start[Z] != bg_record->start[Z])) {
				debug4("small 2 got %c%c%c looking for %c%c%c",
				       alpha_num[bg_record->start[X]],
				       alpha_num[bg_record->start[Y]],
				       alpha_num[bg_record->start[Z]],
				       alpha_num[request->start[X]],
				       alpha_num[request->start[Y]],
				       alpha_num[request->start[Z]]);
				continue;
			}
			debug3("small 2 found %c%c%c looking for %c%c%c",
			       alpha_num[bg_record->start[X]],
			       alpha_num[bg_record->start[Y]],
			       alpha_num[bg_record->start[Z]],
			       alpha_num[request->start[X]],
			       alpha_num[request->start[Y]],
			       alpha_num[request->start[Z]]);
		}
				
		proc_cnt = bg_record->bp_count * bg_record->cpus_per_bp;
		if(proc_cnt == request->procs) {
			debug2("found it here %s, %s",
			       bg_record->bg_block_id,
			       bg_record->nodes);
			request->save_name = xstrdup_printf(
				"%c%c%c",
				alpha_num[bg_record->start[X]],
				alpha_num[bg_record->start[Y]],
				alpha_num[bg_record->start[Z]]);
			rc = SLURM_SUCCESS;
			goto finished;
		} 

		if(bg_record->node_cnt > bluegene_bp_node_cnt)
			continue;
		if(proc_cnt < request->procs) {
			if(last_quarter != bg_record->quarter){
				last_quarter = bg_record->quarter;
				total_proc_cnt = proc_cnt;
			} else {
				total_proc_cnt += proc_cnt;
			}
			debug2("got %d on quarter %d",
			       total_proc_cnt, last_quarter);
			if(total_proc_cnt == request->procs) {
				request->save_name = xstrdup_printf(
					"%c%c%c",
					alpha_num[bg_record->start[X]],
					alpha_num[bg_record->start[Y]],
					alpha_num[bg_record->start[Z]]);
				if(!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;	
				}
				bg_record = create_small_record(
					bg_record,
					last_quarter,
					(uint16_t) NO_VAL);
				list_append(new_blocks, bg_record);
								
				rc = SLURM_SUCCESS;
				goto finished;	
			}
			continue;
		}				
		break;
	}
found_one:
	if(bg_record) {
		List temp_list = NULL;
		bg_record_t *found_record = NULL;

		if(bg_record->original) {
			debug3("This was a copy");
			found_record = bg_record->original;
		} else {
			debug3("looking for original");
			found_record = find_org_in_bg_list(
				bg_list, bg_record);
		}
		if(!found_record) {
			error("this record wasn't found in the list!");
			rc = SLURM_ERROR;
			goto finished;
		}
		
		format_node_name(found_record, tmp_char, sizeof(tmp_char));
			
		debug2("going to split %s, %s",
		       found_record->bg_block_id,
		       tmp_char);
		request->save_name = xstrdup_printf(
			"%c%c%c",
			alpha_num[found_record->start[X]],
			alpha_num[found_record->start[Y]],
			alpha_num[found_record->start[Z]]);
		if(!my_block_list) {
			rc = SLURM_SUCCESS;
			goto finished;	
		}
		_split_block(block_list, new_blocks,
			     found_record, request->procs);
		remove_from_bg_list(block_list, bg_record);
		destroy_bg_record(bg_record);
		remove_from_bg_list(bg_list, found_record);
		temp_list = list_create(NULL);
		list_push(temp_list, found_record);
		num_block_to_free++;
		free_block_list(temp_list);
		list_destroy(temp_list);
		rc = SLURM_SUCCESS;
		goto finished;
	}
	
finished:
	list_iterator_destroy(itr);
		
	return rc;
}
#else

static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes) 
{
	bool full_bp = false; 
	bitoff_t start = 0;
	blockreq_t blockreq;
	
	memset(&blockreq, 0, sizeof(blockreq_t));

	switch(bg_record->node_cnt) {
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
			      cnodes, bg_record->node_cnt);
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
			      cnodes, bg_record->node_cnt);
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
			      cnodes, bg_record->node_cnt);
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
			      cnodes, bg_record->node_cnt);
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
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		full_bp = true;
		break;
	}

	if(!full_bp && bg_record->ionode_bitmap)
		start = bit_ffs(bg_record->ionode_bitmap);

	debug2("Asking for %u 16CNBlocks, %u 32CNBlocks, "
	       "%u 64CNBlocks, %u 128CNBlocks, and %u 256CNBlocks"
	       "from a %u block, starting at ionode %d.", 
	       blockreq.small16, blockreq.small32, 
	       blockreq.small64, blockreq.small128, 
	       blockreq.small256, bg_record->node_cnt, start);
	handle_small_record_request(new_blocks, &blockreq, bg_record, start);

finished:
	return SLURM_SUCCESS;
}

static int _breakup_blocks(List block_list, List new_blocks,
			   ba_request_t *request, List my_block_list)
{
	int rc = SLURM_ERROR;
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL, bit_itr = NULL;
	int search_cnt = 0;
	int total_cnode_cnt=0;
	uint16_t last_quarter = (uint16_t) NO_VAL;
	char tmp_char[256];
	bitstr_t *ionodes = bit_alloc(bluegene_numpsets);
	int cnodes = request->procs / bluegene_proc_ratio;
	
	debug2("proc count = %d cnodes = %d size = %d",
	       request->procs, cnodes, request->size);
	
	switch(cnodes) {
	case 16:
		/* a 16 can go anywhere */
		break;
	case 32:
		bit_itr = list_iterator_create(bg_valid_small32);
		break;
	case 64:
		bit_itr = list_iterator_create(bg_valid_small64);
		break;
	case 128:
		bit_itr = list_iterator_create(bg_valid_small128);
		break;
	case 256:
		bit_itr = list_iterator_create(bg_valid_small256);
		break;
	default:
		error("We shouldn't be here with this size %d", cnodes);
		goto finished;
		break;				
	}

	itr = list_iterator_create(block_list);	
	/* First try with free blocks a midplane or less.  Then try with the
	 * smallest blocks.
	 */
again:		
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		if(bg_record->job_running != NO_JOB_RUNNING)
			continue;
		/* on the third time through look for just a block
		 * that isn't used */
		
		/* check for free blocks on the first and second time */
		if((search_cnt < 2)
		   && (bg_record->state != RM_PARTITION_FREE))
				continue;
		/* check small blocks first */
		if((search_cnt == 0)
		   && (bg_record->node_cnt > bluegene_bp_node_cnt))
				continue;
		
		if (request->avail_node_bitmap &&
		    !bit_super_set(bg_record->bitmap,
				   request->avail_node_bitmap)) {
			debug2("bg block %s has nodes not usable by this job",
			       bg_record->bg_block_id);
			continue;
		}

		if(request->start_req) {
			if ((request->start[X] != bg_record->start[X])
			    || (request->start[Y] != bg_record->start[Y])
			    || (request->start[Z] != bg_record->start[Z])) {
				debug4("small got %c%c%c looking for %c%c%c",
				       alpha_num[bg_record->start[X]],
				       alpha_num[bg_record->start[Y]],
				       alpha_num[bg_record->start[Z]],
				       alpha_num[request->start[X]],
				       alpha_num[request->start[Y]],
				       alpha_num[request->start[Z]]);
				continue;
			}
			debug3("small found %c%c%c looking for %c%c%c",
			       alpha_num[bg_record->start[X]],
			       alpha_num[bg_record->start[Y]],
			       alpha_num[bg_record->start[Z]],
			       alpha_num[request->start[X]],
			       alpha_num[request->start[Y]],
			       alpha_num[request->start[Z]]);
		}

		if(bg_record->node_cnt == cnodes) {
			debug2("found it here %s, %s",
			       bg_record->bg_block_id,
			       bg_record->nodes);
			request->save_name = xstrdup_printf(
				"%c%c%c",
				alpha_num[bg_record->start[X]],
				alpha_num[bg_record->start[Y]],
				alpha_num[bg_record->start[Z]]);
			rc = SLURM_SUCCESS;
			goto finished;
		}
		/* lets see if we can combine some small ones */
		if(bg_record->node_cnt < cnodes) {
			//char bitstring[BITSIZE];
			bitstr_t *bitstr = NULL;
			bit_or(ionodes, bg_record->ionode_bitmap);

			/* check and see if the bits set are a valid
			   combo */
			if(bit_itr) {
				while((bitstr = list_next(bit_itr))) {
					if(bit_super_set(ionodes, bitstr)) 
						break;
				}
				list_iterator_reset(bit_itr);
			}
			if(!bitstr) {
				bit_nclear(ionodes, 0, (bluegene_numpsets-1));
				bit_or(ionodes, bg_record->ionode_bitmap);
				total_cnode_cnt = bg_record->node_cnt;
			} else
				total_cnode_cnt += bg_record->node_cnt;

			//bit_fmt(bitstring, BITSIZE, ionodes);
			debug2("1 adding %d got %d set",
			       bg_record->node_cnt, total_cnode_cnt);
			if(total_cnode_cnt == cnodes) {
				request->save_name = xstrdup_printf(
					"%c%c%c",
					alpha_num[bg_record->start[X]],
					alpha_num[bg_record->start[Y]],
					alpha_num[bg_record->start[Z]]);
				if(!my_block_list) {
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
	
	if(!bg_record && (search_cnt < 2)) {
		search_cnt++;
		list_iterator_reset(itr);
		bit_nclear(ionodes, 0, (bluegene_numpsets-1));
		total_cnode_cnt = 0;		
		last_quarter = (uint16_t) NO_VAL;
		goto again;
	}

	if(bg_record) {
		List temp_list = NULL;
		bg_record_t *found_record = NULL;

		if(bg_record->original) {
			debug3("This was a copy");
			found_record = bg_record->original;
		} else {
			debug3("looking for original");
			found_record = find_org_in_bg_list(
				bg_list, bg_record);
		}
		if(!found_record) {
			error("this record wasn't found in the list!");
			rc = SLURM_ERROR;
			goto finished;
		}
		
		format_node_name(found_record, tmp_char, sizeof(tmp_char));
			
		debug2("going to split %s, %s",
		       found_record->bg_block_id,
		       tmp_char);
		request->save_name = xstrdup_printf(
			"%c%c%c",
			alpha_num[found_record->start[X]],
			alpha_num[found_record->start[Y]],
			alpha_num[found_record->start[Z]]);
		if(!my_block_list) {
			rc = SLURM_SUCCESS;
			goto finished;	
		}
		_split_block(block_list, new_blocks, found_record, cnodes);
		remove_from_bg_list(block_list, bg_record);
		destroy_bg_record(bg_record);
		remove_from_bg_list(bg_list, found_record);
		temp_list = list_create(NULL);
		list_push(temp_list, found_record);
		num_block_to_free++;
		free_block_list(temp_list);
		list_destroy(temp_list);
		rc = SLURM_SUCCESS;
		goto finished;
	}
	
finished:
	if(bit_itr)
		list_iterator_destroy(bit_itr);

	FREE_NULL_BITMAP(ionodes);	
	if(itr)
		list_iterator_destroy(itr);
		
	return rc;
}

#endif
