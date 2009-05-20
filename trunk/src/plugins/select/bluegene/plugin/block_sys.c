/*****************************************************************************\
 *  block_sys.c - component used for wiring up the blocks
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
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

#include "bluegene.h"
#include "src/common/uid.h"
#include <fcntl.h>

/** these are used in the dynamic partitioning algorithm */

/* global system = list of free blocks */
List bg_sys_free = NULL;
/* global system = list of allocated blocks */
List bg_sys_allocated = NULL;

/** 
 * _get_bp: get the BP at location loc
 *
 * IN - bg: pointer to preinitialized bg pointer
 * IN - bp: pointer to preinitailized rm_element_t that will 
 *      hold the BP that we resolve to.
 * IN - loc: location of the desired BP 
 * OUT - bp: will point to BP at location loc
 * OUT - rc: error code (0 = success)
 */

static void _pre_allocate(bg_record_t *bg_record);
static int _post_allocate(bg_record_t *bg_record);

#define MAX_ADD_RETRY 2

#if 0
/* Vestigial
 * print out a list
 */
static void _print_list(List list)
{
	int* stuff = NULL, i = 0;
	ListIterator itr;

	if (list == NULL)
		return;

	debug("trying to get the list iterator");
	itr = list_iterator_create(list);
	debug("done");

	debug("printing list");
	while ((stuff = (int*) list_next(itr))) {
		debug("stuff %d", stuff);
		if (stuff == NULL){
			break; 
		}

		debug("[ %d", stuff[0]);
		for (i=1; i<PA_SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff[i]);
		}
		debug(" ]");
	}
	list_iterator_destroy(itr);
}
#endif

/** 
 * initialize the BG block in the resource manager 
 */
static void _pre_allocate(bg_record_t *bg_record)
{
#ifdef HAVE_BG_FILES
	int rc;
	int send_psets=bg_conf->numpsets;

#ifdef HAVE_BGL
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionBlrtsImg,   
				  bg_record->blrtsimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionBlrtsImg)", bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionLinuxImg,   
				  bg_record->linuximage)) != STATUS_OK) 
		error("bridge_set_data(RM_PartitionLinuxImg)", bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionRamdiskImg, 
				  bg_record->ramdiskimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionRamdiskImg)", 
		      bg_err_str(rc));
#else
	struct tm my_tm;
	struct timeval my_tv;

	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionCnloadImg,
				  bg_record->linuximage)) != STATUS_OK) 
		error("bridge_set_data(RM_PartitionLinuxCnloadImg)",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionIoloadImg, 
				  bg_record->ramdiskimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionIoloadImg)", 
		      bg_err_str(rc));

	gettimeofday(&my_tv, NULL);
	localtime_r(&my_tv.tv_sec, &my_tm);
	bg_record->bg_block_id = xstrdup_printf(
		"RMP%2.2d%2.2s%2.2d%2.2d%2.2d%3.3d",
		my_tm.tm_mday, mon_abbr(my_tm.tm_mon), 
		my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, my_tv.tv_usec/1000);
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionID,
				  bg_record->bg_block_id)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionID)", bg_err_str(rc));
#endif
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionMloaderImg, 
				  bg_record->mloaderimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionMloaderImg)", 
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionConnection, 
				  &bg_record->conn_type)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionConnection)", 
		      bg_err_str(rc));
	
	/* rc = bg_conf->bp_node_cnt/bg_record->node_cnt; */
/* 	if(rc > 1) */
/* 		send_psets = bg_conf->numpsets/rc; */
	
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionPsetsPerBP, 
				  &send_psets)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionPsetsPerBP)", 
		      bg_err_str(rc));
	
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionUserName, 
				  bg_conf->slurm_user_name)) 
	    != STATUS_OK)
		error("bridge_set_data(RM_PartitionUserName)", bg_err_str(rc));
	
#endif
}

/** 
 * add the block record to the DB
 */
static int _post_allocate(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES	
	int i;
	pm_partition_id_t block_id;
	uid_t my_uid;

	/* Add partition record to the DB */
	debug2("adding block\n");

	for(i=0;i<MAX_ADD_RETRY; i++) {
		if ((rc = bridge_add_block(bg_record->bg_block)) 
		    != STATUS_OK) {
			error("bridge_add_block(): %s", bg_err_str(rc));
			rc = SLURM_ERROR;
		} else {
			rc = SLURM_SUCCESS;
			break;
		}
		sleep(3);
	}
	if(rc == SLURM_ERROR) {
		info("going to free it");
		if ((rc = bridge_free_block(bg_record->bg_block)) 
		    != STATUS_OK)
			error("bridge_free_block(): %s", bg_err_str(rc));
		fatal("couldn't add last block.");
	}
	debug2("done adding\n");
	
	/* Get back the new block id */
	if ((rc = bridge_get_data(bg_record->bg_block, RM_PartitionID, 
				  &block_id))
	    != STATUS_OK) {
		error("bridge_get_data(RM_PartitionID): %s", bg_err_str(rc));
		bg_record->bg_block_id = xstrdup("UNKNOWN");
	} else {
		if(!block_id) {
			error("No Block ID was returned from database");
			return SLURM_ERROR;
		}
		bg_record->bg_block_id = xstrdup(block_id);
		
		free(block_id);
		
		xfree(bg_record->target_name);

		
		bg_record->target_name = 
			xstrdup(bg_conf->slurm_user_name);

		xfree(bg_record->user_name);
		bg_record->user_name = 
			xstrdup(bg_conf->slurm_user_name);
		

		my_uid = uid_from_string(bg_record->user_name);
		if (my_uid == (uid_t) -1) {
			error("uid_from_string(%s): %m", bg_record->user_name);
		} else {
			bg_record->user_uid = my_uid;
		} 
	}
	/* We are done with the block */
	if ((rc = bridge_free_block(bg_record->bg_block)) != STATUS_OK)
		error("bridge_free_block(): %s", bg_err_str(rc));	
#else
	/* We are just looking for a real number here no need for a
	   base conversion
	*/
	static int block_inx = 0;
	int i=0, temp = 0;
	if(bg_record->bg_block_id) {
		while(bg_record->bg_block_id[i]
		      && (bg_record->bg_block_id[i] > '9' 
			  || bg_record->bg_block_id[i] < '0')) 		
			i++;
		if(bg_record->bg_block_id[i]) {
			temp = atoi(bg_record->bg_block_id+i)+1;
			if(temp > block_inx)
				block_inx = temp;
			debug4("first new block inx will now be %d", block_inx);
		}
	} else {
		bg_record->bg_block_id = xmalloc(8);
		snprintf(bg_record->bg_block_id, 8,
			 "RMP%d", block_inx++);
	}
#endif	

	return rc;
}

#ifdef HAVE_BG_FILES
#ifdef HAVE_BGL
static int _find_nodecard(rm_partition_t *block_ptr, int *nc_id)
{
	char *my_card_name = NULL;
	char *card_name = NULL;
	rm_bp_id_t bp_id = NULL;
	int num = 0;
	int i=0;
	int rc;
	rm_nodecard_list_t *ncard_list = NULL;
	rm_nodecard_t *ncard = NULL;
	rm_BP_t *curr_bp = NULL;
	
	xassert(block_ptr);
	xassert(nc_id);

	if((rc = bridge_get_data(block_ptr,
				 RM_PartitionFirstNodeCard,
				 &ncard))
	   != STATUS_OK) {
		error("bridge_get_data(RM_FirstCard): %s",
		      bg_err_str(rc));
	}
	if((rc = bridge_get_data(ncard,
				 RM_NodeCardID,
				 &my_card_name))
	   != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardID): %s",
		      bg_err_str(rc));
	}
	
	if((rc = bridge_get_data(block_ptr,
				 RM_PartitionFirstBP,
				 &curr_bp))
	   != STATUS_OK) {
		error("bridge_get_data(RM_PartitionFirstBP): %s",
		      bg_err_str(rc));
	}
	if ((rc = bridge_get_data(curr_bp, RM_BPID, &bp_id))
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPID): %d", rc);
		return SLURM_ERROR;
	}
	
	if ((rc = bridge_get_nodecards(bp_id, &ncard_list))
	    != STATUS_OK) {
		error("bridge_get_nodecards(%s): %d",
		      bp_id, rc);
		free(bp_id);
		return SLURM_ERROR;
	}
	free(bp_id);
	if((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	   != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardListSize): %s", 
		      bg_err_str(rc));
		return SLURM_ERROR;
	}
	
	for(i=0; i<num; i++) {
		if (i) {
			if ((rc = 
			     bridge_get_data(ncard_list, 
					     RM_NodeCardListNext, 
					     &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      rc);
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list, 
						  RM_NodeCardListFirst, 
						  &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst: %s",
				      rc);
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
		if ((rc = bridge_get_data(ncard, 
					  RM_NodeCardID, 
					  &card_name)) != STATUS_OK) {
			error("bridge_get_data(RM_NodeCardID: %s",
			      rc);
			rc = SLURM_ERROR;
			goto cleanup;
		}
		if(strcmp(my_card_name, card_name)) {
			free(card_name);
			continue;
		}
		free(card_name);
		(*nc_id) = i;
		break;
	}
cleanup:
	free(my_card_name);
	return SLURM_SUCCESS;
}
#endif
#endif

extern int configure_block(bg_record_t *bg_record)
{
	/* new block to be added */
#ifdef HAVE_BG_FILES
	bridge_new_block(&bg_record->bg_block); 
#endif
	_pre_allocate(bg_record);

	if(bg_record->cpu_cnt < bg_conf->procs_per_bp)
		configure_small_block(bg_record);
	else
		configure_block_switches(bg_record);
	
	_post_allocate(bg_record); 
	return 1;
}

#ifdef HAVE_BG_FILES
/*
 * Download from MMCS the initial BG block information
 */
int read_bg_blocks(List curr_block_list)
{
	int rc = SLURM_SUCCESS;

	int bp_cnt, i, nc_cnt, io_cnt;
	rm_element_t *bp_ptr = NULL;
	rm_bp_id_t bpid;
	rm_partition_t *block_ptr = NULL;
	char node_name_tmp[255], *user_name = NULL;
	bg_record_t *bg_record = NULL;
	uid_t my_uid;
	
	int *coord = NULL;
	int block_number, block_count;
	char *tmp_char = NULL;

	rm_partition_list_t *block_list = NULL;
	rm_partition_state_flag_t state = PARTITION_ALL_FLAG;
	rm_nodecard_t *ncard = NULL;
	int nc_id, io_start;

	bool small = false;
	hostlist_t hostlist;		/* expanded form of hosts */

	set_bp_map();
	if ((rc = bridge_get_blocks_info(state, &block_list))
	    != STATUS_OK) {
		error("2 rm_get_blocks_info(): %s", bg_err_str(rc));
		return SLURM_ERROR;
		
	}
	
	if ((rc = bridge_get_data(block_list, RM_PartListSize, &block_count))
	    != STATUS_OK) {
		error("bridge_get_data(RM_PartListSize): %s", bg_err_str(rc));
		block_count = 0;
	}
	info("querying the system for existing blocks");
	for(block_number=0; block_number<block_count; block_number++) {
		
		if (block_number) {
			if ((rc = bridge_get_data(block_list, 
						  RM_PartListNextPart,
						  &block_ptr)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartListNextPart): %s",
				      bg_err_str(rc));
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_list, 
						  RM_PartListFirstPart,
						  &block_ptr)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartListFirstPart): %s",
				      bg_err_str(rc));
				break;
			}
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionID, 
					  &tmp_char))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionID): %s", 
			      bg_err_str(rc));
			continue;
		}

		if(!tmp_char) {
			error("No Block ID was returned from database");
			continue;
		}

		if(strncmp("RMP", tmp_char, 3)) {
			free(tmp_char);
			continue;
		}

		if(bg_recover) {
			if ((rc = bridge_get_block(tmp_char, &block_ptr))
			    != STATUS_OK) {
				error("Block %s doesn't exist.",
				      tmp_char);
				rc = SLURM_ERROR;
				free(tmp_char);
				break;
			}
		}
		/* New BG Block record */		
		
		bg_record = xmalloc(sizeof(bg_record_t));
		list_push(curr_block_list, bg_record);
		
		bg_record->bg_block_id = xstrdup(tmp_char);
		free(tmp_char);

		bg_record->state = NO_VAL;
#ifndef HAVE_BGL
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionSize, 
					  &bp_cnt)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSize): %s", 
			      bg_err_str(rc));
			goto clean_up;
		}
				
		if(bp_cnt==0)
			goto clean_up;

		bg_record->node_cnt = bp_cnt;
		bg_record->cpu_cnt = bg_conf->proc_ratio * bg_record->node_cnt;
#endif
		bg_record->job_running = NO_JOB_RUNNING;
		
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionBPNum, 
					  &bp_cnt)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPNum): %s", 
			      bg_err_str(rc));
			goto clean_up;
		}
				
		if(bp_cnt==0)
			goto clean_up;
		bg_record->bp_count = bp_cnt;

		debug3("has %d BPs", bg_record->bp_count);
		
		if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
					  &bg_record->switch_count)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSwitchNum): %s",
			      bg_err_str(rc));
			goto clean_up;
		} 

		if ((rc = bridge_get_data(block_ptr, RM_PartitionSmall, 
					  &small)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSmall): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		
		if(small) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionOptions,
						  &tmp_char))
			    != STATUS_OK) {
				error("bridge_get_data(RM_PartitionOptions): "
				      "%s", bg_err_str(rc));
				goto clean_up;
			} else if(tmp_char) {
				switch(tmp_char[0]) {
				case 's':
					bg_record->conn_type = SELECT_HTC_S;
					break;
				case 'd':
					bg_record->conn_type = SELECT_HTC_D;
					break;
				case 'v':
					bg_record->conn_type = SELECT_HTC_V;
					break;
				case 'l':
					bg_record->conn_type = SELECT_HTC_L;
					break;
				default:
					bg_record->conn_type = SELECT_SMALL;
					break;
				}
				
				free(tmp_char);
			} else
				bg_record->conn_type = SELECT_SMALL;

			if((rc = bridge_get_data(block_ptr,
						 RM_PartitionFirstNodeCard,
						 &ncard))
			   != STATUS_OK) {
				error("bridge_get_data("
				      "RM_PartitionFirstNodeCard): %s",
				      bg_err_str(rc));
				goto clean_up;
			}
			
			if((rc = bridge_get_data(block_ptr,
						 RM_PartitionNodeCardNum,
						 &nc_cnt))
			   != STATUS_OK) {
				error("bridge_get_data("
				      "RM_PartitionNodeCardNum): %s",
				      bg_err_str(rc));
				goto clean_up;
			}
#ifdef HAVE_BGL
			/* Translate nodecard count to ionode count */
			if((io_cnt = nc_cnt * bg_conf->io_ratio))
				io_cnt--;

			nc_id = 0;
			if(nc_cnt == 1) 
				_find_nodecard(block_ptr, &nc_id);
			
			bg_record->node_cnt = 
				nc_cnt * bg_conf->nodecard_node_cnt;
			bg_record->cpu_cnt =
				bg_conf->proc_ratio * bg_record->node_cnt;

			if ((rc = bridge_get_data(ncard, 
						  RM_NodeCardQuarter, 
						  &io_start)) != STATUS_OK) {
				error("bridge_get_data(CardQuarter): %d",rc);
				goto clean_up;
			}
			io_start *= bg_conf->quarter_ionode_cnt;
			io_start += bg_conf->nodecard_ionode_cnt * (nc_id%4);
#else
			/* Translate nodecard count to ionode count */
			if((io_cnt = nc_cnt * bg_conf->io_ratio))
				io_cnt--;

			if ((rc = bridge_get_data(ncard, 
						  RM_NodeCardID, 
						  &tmp_char)) != STATUS_OK) {
				error("bridge_get_data(RM_NodeCardID): %d",rc);
				goto clean_up;
			}
			
			if(!tmp_char)
				goto clean_up;
			
			/* From the first nodecard id we can figure
			   out where to start from with the alloc of ionodes.
			*/
			nc_id = atoi((char*)tmp_char+1);
			free(tmp_char);
			io_start = nc_id * bg_conf->io_ratio;
			if(bg_record->node_cnt < bg_conf->nodecard_node_cnt) {
				rm_ionode_t *ionode;

				/* figure out the ionode we are using */
				if ((rc = bridge_get_data(
					     ncard, 
					     RM_NodeCardFirstIONode, 
					     &ionode)) != STATUS_OK) {
					error("bridge_get_data("
					      "RM_NodeCardFirstIONode): %d",
					      rc);
					goto clean_up;
				}
				if ((rc = bridge_get_data(ionode,
							  RM_IONodeID, 
							  &tmp_char)) 
				    != STATUS_OK) {				
					error("bridge_get_data("
					      "RM_NodeCardIONodeNum): %s", 
					      bg_err_str(rc));
					rc = SLURM_ERROR;
					goto clean_up;
				}			
				
				if(!tmp_char)
					goto clean_up;
				/* just add the ionode num to the
				 * io_start */
				io_start += atoi((char*)tmp_char+1);
				free(tmp_char);
				/* make sure i is 0 since we are only using
				 * 1 ionode */
				io_cnt = 0;
			}
#endif

			if(set_ionodes(bg_record, io_start, io_cnt)
			   == SLURM_ERROR)
				error("couldn't create ionode_bitmap "
				      "for ionodes %d to %d",
				      io_start, io_start+io_cnt);
			debug3("%s uses ionodes %s",
			       bg_record->bg_block_id,
			       bg_record->ionodes);
		} else {
#ifdef HAVE_BGL
			bg_record->cpu_cnt = bg_conf->procs_per_bp 
				* bg_record->bp_count;
			bg_record->node_cnt =  bg_conf->bp_node_cnt
				* bg_record->bp_count;
#endif
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionConnection,
						  &bg_record->conn_type))
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionConnection): %s",
				      bg_err_str(rc));
				goto clean_up;
			}
			/* Set the bitmap blank here if it is a full
			   node we don't want anything set we also
			   don't want the bg_record->ionodes set.
			*/
			bg_record->ionode_bitmap = bit_alloc(bg_conf->numpsets);
		}		
		
		bg_record->bg_block_list =
			get_and_set_block_wiring(bg_record->bg_block_id);
		if(!bg_record->bg_block_list)
			fatal("couldn't get the wiring info for block %s",
			      bg_record->bg_block_id);
		hostlist = hostlist_create(NULL);

		for (i=0; i<bp_cnt; i++) {
			if(i) {
				if ((rc = bridge_get_data(block_ptr, 
							  RM_PartitionNextBP, 
							  &bp_ptr))
				    != STATUS_OK) {
					error("bridge_get_data(RM_NextBP): %s",
					      bg_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			} else {
				if ((rc = bridge_get_data(block_ptr, 
							  RM_PartitionFirstBP, 
							  &bp_ptr))
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_FirstBP): %s", 
					      bg_err_str(rc));
					rc = SLURM_ERROR;
					if (bg_recover)
						bridge_free_block(block_ptr);
					return rc;
				}	
			}
			if ((rc = bridge_get_data(bp_ptr, RM_BPID, &bpid))
			    != STATUS_OK) {
				error("bridge_get_data(RM_BPID): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}

			if(!bpid) {
				error("No BP ID was returned from database");
				continue;
			}
			
			coord = find_bp_loc(bpid);

			if(!coord) {
				fatal("Could not find coordinates for "
				      "BP ID %s", (char *) bpid);
			}
			free(bpid);

			
			snprintf(node_name_tmp, 
				 sizeof(node_name_tmp),
				 "%s%c%c%c", 
				 bg_conf->slurm_node_prefix,
				 alpha_num[coord[X]], alpha_num[coord[Y]],
				 alpha_num[coord[Z]]);
			
			
			hostlist_push(hostlist, node_name_tmp);
		}	
		i = 1024;
		bg_record->nodes = xmalloc(i);
		while (hostlist_ranged_string(hostlist, i,
					      bg_record->nodes) < 0) {
			i *= 2;
			xrealloc(bg_record->nodes, i);
		}
		hostlist_destroy(hostlist);
		debug3("got nodes of %s", bg_record->nodes);
		// need to get the 000x000 range for nodes
		// also need to get coords
		
#ifdef HAVE_BGL
		if ((rc = bridge_get_data(block_ptr, RM_PartitionMode,
					  &bg_record->node_use))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionMode): %s",
			      bg_err_str(rc));
		}
#endif			
		if ((rc = bridge_get_data(block_ptr, RM_PartitionState,
					  &bg_record->state)) != STATUS_OK) {
			error("bridge_get_data(RM_PartitionState): %s",
			      bg_err_str(rc));
			goto clean_up;
		} else if(bg_record->state == RM_PARTITION_CONFIGURING)
			bg_record->boot_state = 1;
		else
			bg_record->boot_state = 0;
			
		debug3("Block %s is in state %d",
		       bg_record->bg_block_id, 
		       bg_record->state);
		
		process_nodes(bg_record, false);

		/* We can stop processing information now since we
		   don't need to rest of the information to decide if
		   this is the correct block. */
		if(bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum,
					  &bp_cnt)) != STATUS_OK) {
			error("bridge_get_data(RM_PartitionUsersNum): %s",
			      bg_err_str(rc));
			goto clean_up;
		} else {
			if(bp_cnt==0) {
				
				bg_record->user_name = 
					xstrdup(bg_conf->slurm_user_name);
				bg_record->target_name = 
					xstrdup(bg_conf->slurm_user_name);
				
			} else {
				user_name = NULL;
				if ((rc = bridge_get_data(
					     block_ptr, 
					     RM_PartitionFirstUser, 
					     &user_name)) 
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_PartitionFirstUser): %s",
					      bg_err_str(rc));
					goto clean_up;
				}
				if(!user_name) {
					error("No user name was "
					      "returned from database");
					goto clean_up;
				}
				bg_record->user_name = xstrdup(user_name);
			
				if(!bg_record->boot_state) {
					
					bg_record->target_name = 
						xstrdup(bg_conf->slurm_user_name);
					
				} else
					bg_record->target_name = 
						xstrdup(user_name);
				
				free(user_name);
					
			}
			my_uid = uid_from_string(bg_record->user_name);
			if (my_uid == (uid_t) -1) {
				error("uid_from_string(%s): %m", 
				      bg_record->user_name);
			} else {
				bg_record->user_uid = my_uid;
			} 
		}
		
#ifdef HAVE_BGL
		/* get the images of the block */
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionBlrtsImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionBlrtsImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No BlrtsImg was returned from database");
			goto clean_up;
		}
		bg_record->blrtsimage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionLinuxImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionLinuxImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No LinuxImg was returned from database");
			goto clean_up;
		}
		bg_record->linuximage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionRamdiskImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionRamdiskImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No RamdiskImg was returned from database");
			goto clean_up;
		}
		bg_record->ramdiskimage = xstrdup(user_name);

#else
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionCnloadImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionCnloadImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No CnloadImg was returned from database");
			goto clean_up;
		}
		bg_record->linuximage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionIoloadImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionIoloadImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No IoloadImg was returned from database");
			goto clean_up;
		}
		bg_record->ramdiskimage = xstrdup(user_name);

#endif
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionMloaderImg, 
					  &user_name)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionMloaderImg): %s",
			      bg_err_str(rc));
			goto clean_up;
		}
		if(!user_name) {
			error("No MloaderImg was returned from database");
			goto clean_up;
		}
		bg_record->mloaderimage = xstrdup(user_name);

					
	clean_up:	
		if (bg_recover 
		    && ((rc = bridge_free_block(block_ptr)) != STATUS_OK)) {
			error("bridge_free_block(): %s", bg_err_str(rc));
		}
	}
	bridge_free_block_list(block_list);
	
	return rc;
}

#endif

extern int load_state_file(List curr_block_list, char *dir_name)
{
	int state_fd, i, j=0;
	char *state_file = NULL;
	Buf buffer = NULL;
	char *data = NULL;
	int data_size = 0;
	node_select_info_msg_t *node_select_ptr = NULL;
	bg_record_t *bg_record = NULL;
	bg_info_record_t *bg_info_record = NULL;
	bitstr_t *node_bitmap = NULL, *ionode_bitmap = NULL;
	int geo[BA_SYSTEM_DIMENSIONS];
	char temp[256];
	List results = NULL;
	int data_allocated, data_read = 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	int blocks = 0;
	uid_t my_uid;
	int ionodes = 0;
	char *name = NULL;
	struct part_record *part_ptr = NULL;
	char *non_usable_nodes = NULL;
	bitstr_t *bitmap = NULL;
	ListIterator itr = NULL;

	if(!dir_name) {
		debug2("Starting bluegene with clean slate");
		return SLURM_SUCCESS;
	}

	xassert(curr_block_list);

	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/block_state");
	state_fd = open(state_file, O_RDONLY);
	if(state_fd < 0) {
		error("No block state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m", 
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	/*
	 * Check the data version so that when the format changes, we 
	 * we don't try to unpack data using the wrong format routines
	 */
	if(size_buf(buffer)
	   >= sizeof(uint32_t) + strlen(BLOCK_STATE_VERSION)) {
	        char *ptr = get_buf_data(buffer);
		if (!memcmp(&ptr[sizeof(uint32_t)], BLOCK_STATE_VERSION, 3)) {
		        safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
		        debug3("Version string in block_state header is %s",
			       ver_str);
		}
	}
	if (ver_str && (strcmp(ver_str, BLOCK_STATE_VERSION) != 0)) {
		error("Can not recover block state, "
		      "data version incompatable");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	if(select_g_unpack_node_info(&node_select_ptr, buffer) == SLURM_ERROR) {
		error("select_p_state_restore: problem unpacking node_info");
		goto unpack_error;
	}

#ifdef HAVE_BG_FILES
	for (i=0; i<node_select_ptr->record_count; i++) {
		bg_info_record = &(node_select_ptr->bg_info_array[i]);
		
		/* we only care about the states we need here
		 * everthing else should have been set up already */
		if(bg_info_record->state == RM_PARTITION_ERROR) {
			if((bg_record = find_bg_record_in_list(
				    curr_block_list,
				    bg_info_record->bg_block_id)))
				/* put_block_in_error_state should be
				   called after the bg_lists->main has been
				   made.  We can't call it here since
				   this record isn't the record kept
				   around in bg_lists->main.
				*/
				bg_record->state = bg_info_record->state;
		}
	}

	select_g_free_node_info(&node_select_ptr);
	free_buf(buffer);
	return SLURM_SUCCESS;
#endif

	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(false);

	/* Locks are already in place to protect part_list here */
	bitmap = bit_alloc(node_record_count);
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		/* we only want to use bps that are in partitions
		 */
		bit_or(bitmap, part_ptr->node_bitmap);
	}
	list_iterator_destroy(itr);

	bit_not(bitmap);
	non_usable_nodes = bitmap2node_name(bitmap);
	FREE_NULL_BITMAP(bitmap);
	removable_set_bps(non_usable_nodes);

	node_bitmap = bit_alloc(node_record_count);	
	ionode_bitmap = bit_alloc(bg_conf->numpsets);	
	for (i=0; i<node_select_ptr->record_count; i++) {
		bg_info_record = &(node_select_ptr->bg_info_array[i]);
		
		bit_nclear(node_bitmap, 0, bit_size(node_bitmap) - 1);
		bit_nclear(ionode_bitmap, 0, bit_size(ionode_bitmap) - 1);
		
		j = 0;
		while(bg_info_record->bp_inx[j] >= 0) {
			if (bg_info_record->bp_inx[j+1]
			    >= node_record_count) {
				fatal("Job state recovered incompatable with "
					"bluegene.conf. bp=%u state=%d",
					node_record_count,
					bg_info_record->bp_inx[j+1]);
			}
			bit_nset(node_bitmap,
				 bg_info_record->bp_inx[j],
				 bg_info_record->bp_inx[j+1]);
			j += 2;
		}		

		j = 0;
		while(bg_info_record->ionode_inx[j] >= 0) {
			if (bg_info_record->ionode_inx[j+1]
			    >= bg_conf->numpsets) {
				fatal("Job state recovered incompatable with "
					"bluegene.conf. ionodes=%u state=%d",
					bg_conf->numpsets,
					bg_info_record->ionode_inx[j+1]);
			}
			bit_nset(ionode_bitmap,
				 bg_info_record->ionode_inx[j],
				 bg_info_record->ionode_inx[j+1]);
			j += 2;
		}		
					
		bg_record = xmalloc(sizeof(bg_record_t));
		bg_record->bg_block_id =
			xstrdup(bg_info_record->bg_block_id);
		bg_record->nodes =
			xstrdup(bg_info_record->nodes);
		bg_record->ionodes =
			xstrdup(bg_info_record->ionodes);
		bg_record->ionode_bitmap = bit_copy(ionode_bitmap);
		/* put_block_in_error_state should be
		   called after the bg_lists->main has been
		   made.  We can't call it here since
		   this record isn't the record kept
		   around in bg_lists->main.
		*/
		bg_record->state = bg_info_record->state;
		bg_record->job_running = NO_JOB_RUNNING;

		bg_record->bp_count = bit_size(node_bitmap);
		bg_record->node_cnt = bg_info_record->node_cnt;
		if(bg_conf->bp_node_cnt > bg_record->node_cnt) {
			ionodes = bg_conf->bp_node_cnt 
				/ bg_record->node_cnt;
			bg_record->cpu_cnt = bg_conf->procs_per_bp / ionodes;
		} else {
			bg_record->cpu_cnt = bg_conf->procs_per_bp
				* bg_record->bp_count;
		}
#ifdef HAVE_BGL
		bg_record->node_use = bg_info_record->node_use;
#endif
		bg_record->conn_type = bg_info_record->conn_type;
		bg_record->boot_state = 0;

		process_nodes(bg_record, true);

		bg_record->target_name = xstrdup(bg_conf->slurm_user_name);
		bg_record->user_name = xstrdup(bg_conf->slurm_user_name);
			
		my_uid = uid_from_string(bg_record->user_name);
		if (my_uid == (uid_t) -1) {
			error("uid_from_strin(%s): %m", 
			      bg_record->user_name);
		} else {
			bg_record->user_uid = my_uid;
		} 
				
#ifdef HAVE_BGL
		bg_record->blrtsimage =
			xstrdup(bg_info_record->blrtsimage);
#endif
		bg_record->linuximage = 
			xstrdup(bg_info_record->linuximage);
		bg_record->mloaderimage =
			xstrdup(bg_info_record->mloaderimage);
		bg_record->ramdiskimage =
			xstrdup(bg_info_record->ramdiskimage);

		for(j=0; j<BA_SYSTEM_DIMENSIONS; j++) 
			geo[j] = bg_record->geo[j];
				
		if((bg_conf->layout_mode == LAYOUT_OVERLAP)
		   || bg_record->full_block) {
			reset_ba_system(false);
			removable_set_bps(non_usable_nodes);
		}

		results = list_create(NULL);
		name = set_bg_block(results,
				    bg_record->start, 
				    geo, 
				    bg_record->conn_type);
		if(!name) {
			error("I was unable to "
			      "make the "
			      "requested block.");
			list_destroy(results);
			destroy_bg_record(bg_record);
			continue;
		}

			
		snprintf(temp, sizeof(temp), "%s%s",
			 bg_conf->slurm_node_prefix,
			 name);
		
		xfree(name);
		if(strcmp(temp, bg_record->nodes)) {
			fatal("bad wiring in preserved state "
			      "(found %s, but allocated %s) "
			      "YOU MUST COLDSTART",
			      bg_record->nodes, temp);
		}
		if(bg_record->bg_block_list)
			list_destroy(bg_record->bg_block_list);
		bg_record->bg_block_list =
			list_create(destroy_ba_node);
		copy_node_path(results, &bg_record->bg_block_list);
		list_destroy(results);			
			
		configure_block(bg_record);
		blocks++;
		list_push(curr_block_list, bg_record);		
		if(bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}
	}

	xfree(non_usable_nodes);
	FREE_NULL_BITMAP(ionode_bitmap);
	FREE_NULL_BITMAP(node_bitmap);

	sort_bg_record_inc_size(curr_block_list);
	slurm_mutex_unlock(&block_state_mutex);
		
	info("Recovered %d blocks", blocks);
	select_g_free_node_info(&node_select_ptr);
	free_buf(buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete block data checkpoint file");
	free_buf(buffer);
	return SLURM_FAILURE;
}
