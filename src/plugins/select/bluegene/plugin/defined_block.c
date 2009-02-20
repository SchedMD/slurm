/*****************************************************************************\
 *  defined_block.c - functions for creating blocks in a static environment.
 *
 *  $Id: defined_block.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "defined_block.h"

/*
 * create_defined_blocks - create the static blocks that will be used
 * for scheduling, all partitions must be able to be created and booted
 * at once.  
 * IN - int overlapped, 1 if partitions are to be overlapped, 0 if they are
 * static.
 * RET - success of fitting all configurations
 */
extern int create_defined_blocks(bg_layout_t overlapped,
				 List bg_found_block_list)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	bg_record_t *bg_record = NULL;
	ListIterator itr_found;
	int i;
	bg_record_t *found_record = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	char temp[256];
	List results = NULL;
	
#ifdef HAVE_BG_FILES
	init_wires();
#endif
	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(false);
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while((bg_record = list_next(itr))) {
			if(bg_found_block_list) {
				itr_found = list_iterator_create(
					bg_found_block_list);
				while ((found_record = (bg_record_t*) 
					list_next(itr_found)) != NULL) {
/* 					info("%s.%d.%d ?= %s.%d.%d\n", */
/* 					     bg_record->nodes, */
/* 					     bg_record->quarter, */
/* 					     bg_record->nodecard, */
/* 					     found_record->nodes, */
/* 					     found_record->quarter, */
/* 					     found_record->nodecard); */
					
					if ((bit_equal(bg_record->bitmap, 
						       found_record->bitmap))
#ifdef HAVE_BGL
					    && (bg_record->quarter ==
						found_record->quarter)
					    && (bg_record->nodecard ==
						found_record->nodecard)
#else
					    && (bit_equal(bg_record->
							  ionode_bitmap, 
							  found_record->
							  ionode_bitmap))
#endif
						) {
						/* don't reboot this one */
						break;	
					}
				}
				list_iterator_destroy(itr_found);
			} else {
				error("create_defined_blocks: "
				      "no bg_found_block_list 1");
			}
			if(bg_record->bp_count > 0 
			   && !bg_record->full_block
			   && bg_record->cpu_cnt >= procs_per_node) {
				char *name = NULL;

				if(overlapped == LAYOUT_OVERLAP) 
					reset_ba_system(false);
									
				/* we want the bps that aren't
				 * in this record to mark them as used
				 */
				if(set_all_bps_except(bg_record->nodes)
				   != SLURM_SUCCESS)
					fatal("something happened in "
					      "the load of %s"
					      "Did you use smap to "
					      "make the "
					      "bluegene.conf file?",
					      bg_record->bg_block_id);

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
				if(bg_record->bg_block_list
				   && list_count(bg_record->bg_block_list)) {
					if(check_and_set_node_list(
						   bg_record->bg_block_list)
					   == SLURM_ERROR) {
						debug2("something happened in "
						       "the load of %s"
						       "Did you use smap to "
						       "make the "
						       "bluegene.conf file?",
						       bg_record->bg_block_id);
						list_iterator_destroy(itr);
						reset_all_removed_bps();
						slurm_mutex_unlock(
							&block_state_mutex);
						return SLURM_ERROR;
					}
				} else {
					results = list_create(NULL);
					name = set_bg_block(
						results,
						bg_record->start, 
						geo, 
						bg_record->conn_type);
					reset_all_removed_bps();
					if(!name) {
						error("I was unable to "
						      "make the "
						      "requested block.");
						list_destroy(results);
						list_iterator_destroy(itr);
						slurm_mutex_unlock(
							&block_state_mutex);
						return SLURM_ERROR;
					}
					
					snprintf(temp, sizeof(temp), "%s%s",
						 bg_slurm_node_prefix,
						 name);
					
					xfree(name);
					if(strcmp(temp, bg_record->nodes)) {
						fatal("given list of %s "
						      "but allocated %s, "
						      "your order might be "
						      "wrong in bluegene.conf",
						      bg_record->nodes,
						      temp);
					}
					if(bg_record->bg_block_list)
						list_destroy(bg_record->
							     bg_block_list);
					bg_record->bg_block_list =
						list_create(destroy_ba_node);
					copy_node_path(
						results, 
						&bg_record->bg_block_list);
					list_destroy(results);
				}
			}
			if(found_record == NULL) {
				if(bg_record->full_block) {
					/* if this is defined we need
					   to remove it since we are
					   going to try to create it
					   later on overlap systems
					   this doesn't matter, but
					   since we don't clear the
					   table on static mode we
					   can't do it here or it just
					   won't work since other
					   wires will be or are
					   already set
					*/ 
					list_remove(itr);
					continue;
				}
				if((rc = configure_block(bg_record)) 
				   == SLURM_ERROR) {
					list_iterator_destroy(itr);
					slurm_mutex_unlock(&block_state_mutex);
					return rc;
				}
				print_bg_record(bg_record);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_defined_blocks: no bg_list 2");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	slurm_mutex_unlock(&block_state_mutex);
	create_full_system_block(bg_found_block_list);

	slurm_mutex_lock(&block_state_mutex);
	sort_bg_record_inc_size(bg_list);
	slurm_mutex_unlock(&block_state_mutex);
	
#ifdef _PRINT_BLOCKS_AND_EXIT
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		debug("\n\n");
		while ((found_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			print_bg_record(found_record);
		}
		list_iterator_destroy(itr);
	} else {
		error("create_defined_blocks: no bg_list 5");
	}
 	exit(0);
#endif	/* _PRINT_BLOCKS_AND_EXIT */
	rc = SLURM_SUCCESS;
	//exit(0);
	return rc;
}

extern int create_full_system_block(List bg_found_block_list)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	char *name = NULL;
	List records = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	int i;
	blockreq_t blockreq;
	List results = NULL;
	
	/* Here we are adding a block that in for the entire machine 
	   just in case it isn't in the bluegene.conf file.
	*/
	slurm_mutex_lock(&block_state_mutex);
	
//#ifdef HAVE_BG_FILES
	geo[X] = DIM_SIZE[X] - 1;
	geo[Y] = DIM_SIZE[Y] - 1;
	geo[Z] = DIM_SIZE[Z] - 1;
/* #else */
/* 	geo[X] = max_dim[X]; */
/* 	geo[Y] = max_dim[Y]; */
/* 	geo[Z] = max_dim[Z]; */
/* #endif */
	
	i = (10+strlen(bg_slurm_node_prefix));
	name = xmalloc(i);
	if((geo[X] == 0) && (geo[Y] == 0) && (geo[Z] == 0))
		snprintf(name, i, "%s000",
			 bg_slurm_node_prefix);
	else
		snprintf(name, i, "%s[000x%c%c%c]",
			 bg_slurm_node_prefix,
			 alpha_num[geo[X]], alpha_num[geo[Y]],
			 alpha_num[geo[Z]]);
	
			
	if(bg_found_block_list) {
		itr = list_iterator_create(bg_found_block_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if (!strcmp(name, bg_record->nodes)) {
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_full_system_block: no bg_found_block_list 2");
	}
	
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(name, bg_record->nodes)) {
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		xfree(name);
		error("create_overlapped_blocks: no bg_list 3");
		rc = SLURM_ERROR;
		goto no_total;
	}

	records = list_create(destroy_bg_record);

	memset(&blockreq, 0, sizeof(blockreq_t));
	blockreq.block = name;
	blockreq.conn_type = SELECT_TORUS;

	add_bg_record(records, NULL, &blockreq);
	xfree(name);
	
	bg_record = (bg_record_t *) list_pop(records);
	if(!bg_record) {
		error("Nothing was returned from full system create");
		rc = SLURM_ERROR;
		goto no_total;
	}
	reset_ba_system(false);
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
	results = list_create(NULL);
	name = set_bg_block(results,
			    bg_record->start, 
			    geo, 
			    bg_record->conn_type);
	if(!name) {
		error("I was unable to make the "
		      "requested block.");
		list_destroy(results);
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	xfree(name);
	if(bg_record->bg_block_list)
		list_destroy(bg_record->bg_block_list);
	bg_record->bg_block_list = list_create(destroy_ba_node);
	copy_node_path(results, &bg_record->bg_block_list);
	list_destroy(results);
				
	if((rc = configure_block(bg_record)) == SLURM_ERROR) {
		error("create_full_system_block: "
		      "unable to configure block in api");
		destroy_bg_record(bg_record);
		goto no_total;
	}

	print_bg_record(bg_record);
	list_append(bg_list, bg_record);

no_total:
	if(records)
		list_destroy(records);
	slurm_mutex_unlock(&block_state_mutex);
	return rc;
}
