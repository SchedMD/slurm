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

#include "bluegene.h"
#include "src/common/uid.h"

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
	int send_psets=bluegene_numpsets;

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
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionCnloadImg,
				  bg_record->linuximage)) != STATUS_OK) 
		error("bridge_set_data(RM_PartitionLinuxCnloadImg)",
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionIoloadImg, 
				  bg_record->ramdiskimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionIoloadImg)", 
		      bg_err_str(rc));

#endif
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionMloaderImg, 
				  bg_record->mloaderimage)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionMloaderImg)", 
		      bg_err_str(rc));

	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionConnection, 
				  &bg_record->conn_type)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionConnection)", 
		      bg_err_str(rc));
	
	/* rc = bluegene_bp_node_cnt/bg_record->node_cnt; */
/* 	if(rc > 1) */
/* 		send_psets = bluegene_numpsets/rc; */
	
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionPsetsPerBP, 
				  &send_psets)) != STATUS_OK)
		error("bridge_set_data(RM_PartitionPsetsPerBP)", 
		      bg_err_str(rc));
	slurm_conf_lock();
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionUserName, 
				  slurmctld_conf.slurm_user_name)) 
	    != STATUS_OK)
		error("bridge_set_data(RM_PartitionUserName)", bg_err_str(rc));
	slurm_conf_unlock();
/* 	info("setting it here"); */
/* 	bg_record->bg_block_id = "RMP101"; */
/* 	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionID,  */
/* 			&bg_record->bg_block_id)) != STATUS_OK) */
/* 		error("bridge_set_data(RM_PartitionID)", bg_err_str(rc)); */
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

		slurm_conf_lock();
		bg_record->target_name = 
			xstrdup(slurmctld_conf.slurm_user_name);

		xfree(bg_record->user_name);
		bg_record->user_name = 
			xstrdup(slurmctld_conf.slurm_user_name);
		slurm_conf_unlock();

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
			info("first new block inx will now be %d", block_inx);
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
static int _find_nodecard(bg_record_t *bg_record, 
			  rm_partition_t *block_ptr)
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
		if(strcmp(my_card_name,card_name)) {
			free(card_name);
			continue;
		}
		free(card_name);
		bg_record->nodecard = (i%4);
		break;
	}
cleanup:
	free(my_card_name);
	return SLURM_SUCCESS;
}
#endif

extern int configure_block(bg_record_t *bg_record)
{
	/* new block to be added */
#ifdef HAVE_BG_FILES
	bridge_new_block(&bg_record->bg_block); 
#endif
	_pre_allocate(bg_record);

	if(bg_record->cpus_per_bp < procs_per_node)
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
int read_bg_blocks()
{
	int rc = SLURM_SUCCESS;

	int bp_cnt, i;
	rm_element_t *bp_ptr = NULL;
	rm_bp_id_t bpid;
	rm_partition_t *block_ptr = NULL;
	char node_name_tmp[255], *user_name = NULL;
	bg_record_t *bg_record = NULL;
	uid_t my_uid;
	
	int *coord = NULL;
	int block_number, block_count;
	char *block_name = NULL;
	rm_partition_list_t *block_list = NULL;
	rm_partition_state_flag_t state = PARTITION_ALL_FLAG;
	rm_nodecard_t *ncard = NULL;
	rm_quarter_t quarter;
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
					  &block_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionID): %s", 
			      bg_err_str(rc));
			continue;
		}

		if(!block_name) {
			error("No Block ID was returned from database");
			continue;
		}

		if(strncmp("RMP", block_name, 3)) {
			free(block_name);
			continue;
		}
		if(bg_recover) {
			if ((rc = bridge_get_block(block_name, &block_ptr))
			    != STATUS_OK) {
				error("Block %s doesn't exist.",
				      block_name);
				rc = SLURM_ERROR;
				free(block_name);
				break;
			}
		}
		/* New BG Block record */		
		
		bg_record = xmalloc(sizeof(bg_record_t));
		list_push(bg_curr_block_list, bg_record);
		
		bg_record->bg_block_id = xstrdup(block_name);
		free(block_name);

		bg_record->state = NO_VAL;
		bg_record->quarter = (uint16_t) NO_VAL;
		bg_record->nodecard = (uint16_t) NO_VAL;
		bg_record->job_running = NO_JOB_RUNNING;
		
		if ((rc = bridge_get_data(block_ptr, 
					  RM_PartitionBPNum, 
					  &bp_cnt)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPNum): %s", 
			      bg_err_str(rc));
			bp_cnt = 0;
		}
				
		if(bp_cnt==0)
			goto clean_up;
		bg_record->bp_count = bp_cnt;
		debug3("has %d BPs",
		       bg_record->bp_count);
		
		if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
					  &bg_record->switch_count)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSwitchNum): %s",
			      bg_err_str(rc));
		} 

		if ((rc = bridge_get_data(block_ptr, RM_PartitionSmall, 
					  &small)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
			bp_cnt = 0;
		}
		if(small) {
			if((rc = bridge_get_data(block_ptr,
						 RM_PartitionFirstNodeCard,
						 &ncard))
			   != STATUS_OK) {
				error("bridge_get_data(RM_FirstCard): %s",
				      bg_err_str(rc));
				bp_cnt = 0;
			}
			if ((rc = bridge_get_data(ncard, 
						  RM_NodeCardQuarter, 
						  &quarter)) != STATUS_OK) {
				error("bridge_get_data(CardQuarter): %d",rc);
				bp_cnt = 0;
			}
			bg_record->quarter = quarter;
			if((rc = bridge_get_data(block_ptr,
						 RM_PartitionNodeCardNum,
						 &i))
			   != STATUS_OK) {
				error("bridge_get_data(RM_FirstCard): %s",
				      bg_err_str(rc));
				bp_cnt = 0;
			}
			if(i == 1) {
				_find_nodecard(bg_record, block_ptr);
				i = 16;
			} 
			
			bg_record->cpus_per_bp = procs_per_node/i;
			bg_record->node_cnt = bluegene_bp_node_cnt/i;
				
			debug3("%s is in quarter %d nodecard %d",
			       bg_record->bg_block_id,
			       bg_record->quarter,
			       bg_record->nodecard);
			bg_record->conn_type = SELECT_SMALL;
			
		} else {
			bg_record->cpus_per_bp = procs_per_node;
			bg_record->node_cnt =  bluegene_bp_node_cnt
				* bg_record->bp_count;

			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionConnection,
						  &bg_record->conn_type))
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionConnection): %s",
				      bg_err_str(rc));
			}
			
		}
		
		if(set_ionodes(bg_record) == SLURM_ERROR) 
			error("couldn't create ionode_bitmap "
			      "for %d.%d",
			      bg_record->quarter, bg_record->nodecard);
		
		bg_record->bg_block_list =
			get_and_set_block_wiring(bg_record->bg_block_id);
		if(!bg_record->bg_block_list)
			fatal("couldn't get the wiring info for block %s",
			      bg_record->bg_block_id);
		hostlist = hostlist_create(NULL);

		/* this needs to be changed for small blocks,
		   we just don't know what they are suppose to look 
		   like just yet. 
		*/

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

			slurm_conf_lock();
			snprintf(node_name_tmp, 
				 sizeof(node_name_tmp),
				 "%s%c%c%c", 
				 slurmctld_conf.node_prefix,
				 alpha_num[coord[X]], alpha_num[coord[Y]],
				 alpha_num[coord[Z]]);
			slurm_conf_unlock();
			
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
		} else if(bg_record->state == RM_PARTITION_CONFIGURING)
			bg_record->boot_state = 1;
		else
			bg_record->boot_state = 0;
			
		debug3("Block %s is in state %d",
		       bg_record->bg_block_id, 
		       bg_record->state);
		
		process_nodes(bg_record, false);
	
		if(bluegene_layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_list, tmp_record);
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum,
					  &bp_cnt)) != STATUS_OK) {
			error("bridge_get_data(RM_PartitionUsersNum): %s",
			      bg_err_str(rc));
		} else {
			if(bp_cnt==0) {
				slurm_conf_lock();
				bg_record->user_name = 
					xstrdup(slurmctld_conf.
						slurm_user_name);
				bg_record->target_name = 
					xstrdup(slurmctld_conf.
						slurm_user_name);
				slurm_conf_unlock();
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
				}
				if(!user_name) {
					error("No user name was "
					      "returned from database");
					goto clean_up;
				}
				bg_record->user_name = xstrdup(user_name);
			
				if(!bg_record->boot_state) {
					slurm_conf_lock();
					bg_record->target_name = 
						xstrdup(slurmctld_conf.
							slurm_user_name);
					slurm_conf_unlock();
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
		}
		if(!user_name) {
			error("No MloaderImg was returned from database");
			goto clean_up;
		}
		bg_record->mloaderimage = xstrdup(user_name);

					
	clean_up:	
		if (bg_recover
		    &&  ((rc = bridge_free_block(block_ptr)) 
			 != STATUS_OK)) {
			error("bridge_free_block(): %s", bg_err_str(rc));
		}
	}
	bridge_free_block_list(block_list);
	
	return rc;
}

#endif

