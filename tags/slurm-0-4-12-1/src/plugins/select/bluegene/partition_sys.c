/*****************************************************************************\
 *  partition_sys.c - component used for wiring up the partitions
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "bluegene.h"


/** these are used in the dynamic partitioning algorithm */

/* global system = list of free partitions */
List bgl_sys_free = NULL;
/* global system = list of allocated partitions */
List bgl_sys_allocated = NULL;

/* static void _init_sys(partition_t*); */

   /** 
    * _get_bp: get the BP at location loc
    *
    * IN - bgl: pointer to preinitialized bgl pointer
    * IN - bp: pointer to preinitailized rm_element_t that will 
    *      hold the BP that we resolve to.
    * IN - loc: location of the desired BP 
    * OUT - bp: will point to BP at location loc
    * OUT - rc: error code (0 = success)
    */
#ifdef HAVE_BGL_FILES
static void _pre_allocate(bgl_record_t *bgl_record);
static int _post_allocate(bgl_record_t *bgl_record);
static int _part_list_find(void *object, void *key);
static int _post_bgl_init_read(void *object, void *arg);

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
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff[i]);
		}
		debug(" ]");
	}
	list_iterator_destroy(itr);
}
#endif

/** 
 * initialize the BGL partition in the resource manager 
 */
static void _pre_allocate(bgl_record_t *bgl_record)
{
	rm_set_data(bgl_record->bgl_part, RM_PartitionBlrtsImg,   
		bluegene_blrts);
	rm_set_data(bgl_record->bgl_part, RM_PartitionLinuxImg,   
		bluegene_linux);
	rm_set_data(bgl_record->bgl_part, RM_PartitionMloaderImg, 
		bluegene_mloader);
	rm_set_data(bgl_record->bgl_part, RM_PartitionRamdiskImg, 
		bluegene_ramdisk);
	rm_set_data(bgl_record->bgl_part, RM_PartitionConnection, 
		&bgl_record->conn_type);
	rm_set_data(bgl_record->bgl_part, RM_PartitionMode, 
		&bgl_record->node_use);
	rm_set_data(bgl_record->bgl_part, RM_PartitionPsetsPerBP, &numpsets); 
	rm_set_data(bgl_record->bgl_part, RM_PartitionUserName, USER_NAME);
}

/** 
 * add the partition record to the DB and boot it up!
 */
static int _post_allocate(bgl_record_t *bgl_record)
{
	int rc;
	pm_partition_id_t part_id;
	//char command[255];
	/* Add partition record to the DB */
	debug("adding partition\n");
	
	rc = rm_add_partition(bgl_record->bgl_part);
	if (rc != STATUS_OK) {
		error("Error adding partition");
		return(-1);
	}
	debug("done adding\n");
	
	/* Get back the new partition id */
	rm_get_data(bgl_record->bgl_part, RM_PartitionID, &part_id);
	bgl_record->bgl_part_id = xstrdup(part_id);
	/* if (change_numpsets) { */
/* 		memset(command,0,255); */
/* 		sprintf(command,"%s %s", change_numpsets, part_id); */
/* 		info("%s",command); */
/* 		system(command); */
/* 	} */
	/* We are done with the partition */
	rm_free_partition(bgl_record->bgl_part);

	/* Initiate boot of the partition */
	/* debug("Booting Partition %s", bgl_record->bgl_part_id); */
/* 	rc = pm_create_partition(bgl_record->bgl_part_id); */
/* 	if (rc != STATUS_OK) { */
/* 		error("Error booting_partition partition"); */
/* 		return(-1); */
/* 	} */

/* 	/\* Wait for Partition to be booted *\/ */
/* 	rc = rm_get_partition(bgl_record->bgl_part_id, &bgl_record->bgl_part); */
/* 	if (rc != STATUS_OK) { */
/* 		error("Error in GetPartition"); */
/* 		return(-1); */
/* 	} */
/* 	rm_free_partition(bgl_record->bgl_part); */
	
	fflush(stdout);

	return 0;
}


extern int configure_partition(bgl_record_t *bgl_record)
{
	
	rm_new_partition(&bgl_record->bgl_part); /* new partition to be added */
	_pre_allocate(bgl_record);
	
	configure_partition_switches(bgl_record);
	
	_post_allocate(bgl_record); 
	return 1;
}

/*
 * Download from MMCS the initial BGL partition information
 */
int read_bgl_partitions()
{
	int rc = SLURM_SUCCESS;

	int bp_cnt, i, rm_rc;
	rm_element_t *bp_ptr;
	rm_location_t bp_loc;
	pm_partition_id_t part_id;
	rm_partition_t *part_ptr;
	char node_name_tmp[7], *owner_name;
	bgl_record_t *bgl_record;
#ifndef USE_BGL_FILE
	int *coord;
	char *bp_id;
	int part_number, part_count;
	char *part_name;
	rm_partition_list_t *part_list;
	rm_partition_state_flag_t state = 7;
	
#endif

	/* This code is here to blow add partitions after we get the 
	   system to return correct location information
	*/
	return 1;
#ifndef USE_BGL_FILES
	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		error("rm_set_serial(): %d\n", rc);
		return SLURM_ERROR;
	}			
	if ((rc = rm_get_partitions_info(state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions(): %s",
		      bgl_err_str(rc));
		return SLURM_ERROR;
		
	}
	
	rm_get_data(part_list, RM_PartListSize, &part_count);
	
	rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
	
	for(part_number=0; part_number<part_count; part_number++) {
		rm_get_data(part_ptr, RM_PartitionID, &part_name);
		if(strncmp("RMP",part_name,3))
			goto next_partition;
		
		//debug("Checking if Partition %s is free",part_name);
		if ((rc = rm_get_partition(part_name, &part_ptr))
		    != STATUS_OK) {
			debug("Above error is ok. "
			      "Partition %s doesn't exist.",
			      part_name);
			rc = SLURM_SUCCESS;
			break;
			/* FIX ME: This will need to continue not break 
			   after testing is done.
			*/
			//continue;
		}
		/* New BGL partition record */
		
		
		if ((rm_rc = rm_get_data(part_ptr, RM_PartitionBPNum, &bp_cnt)) != STATUS_OK) {
			error("rm_get_data(RM_BPNum): %s", bgl_err_str(rm_rc));
			bp_cnt = 0;
		}
		if(bp_cnt==0)
			continue;
		if ((rm_rc = rm_get_data(part_ptr, RM_PartitionFirstBP, &bp_ptr))
		    != STATUS_OK) {
			error("rm_get_data(RM_FirstBP): %s",
			      bgl_err_str(rm_rc));
			rc = SLURM_ERROR;
			return rc;
		}
		bgl_record = xmalloc(sizeof(bgl_record_t));
		list_push(bgl_curr_part_list, bgl_record);
				
		bgl_record->bgl_part_list = list_create(NULL);
		bgl_record->hostlist = hostlist_create(NULL);
		bgl_record->bgl_part_id = xstrdup(part_name);
		//rm_BP_id_t *bp_id;
		for (i=0; i<bp_cnt; i++) {
			if ((rm_rc = rm_get_data(bp_ptr, RM_BPID, &part_id))
			    != STATUS_OK) {
				error("rm_get_data(RM_BPLoc): %s",
				      bgl_err_str(rm_rc));
				rc = SLURM_ERROR;
				break;
			}
			debug("bp_id is %s\n",part_id);

			coord = find_bp_loc(bp_id);
			
			sprintf(node_name_tmp, "bgl%d%d%d", 
				coord[X], coord[Y], coord[Z]);
		
			debug("adding %s to partition %s\n",node_name_tmp,part_name);

			hostlist_push(bgl_record->hostlist, node_name_tmp);
			list_append(bgl_record->bgl_part_list, 
				    &pa_system_ptr->grid[bp_loc.X][bp_loc.Y][bp_loc.Z]);
			if ((rm_rc = rm_get_data(part_ptr, RM_PartitionNextBP, &bp_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_NextBP): %s",
				      bgl_err_str(rm_rc));
				rc = SLURM_ERROR;
				break;
			}
		}	
		// need to get the 000x000 range for nodes
		// also need to get coords
				
		if ((rm_rc = rm_get_data(part_ptr,
					 RM_PartitionConnection,
					 &bgl_record->conn_type))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionConnection): %s",
			      bgl_err_str(rm_rc));
		}
		if ((rm_rc = rm_get_data(part_ptr, RM_PartitionMode,
					 &bgl_record->node_use))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionMode): %s",
			      bgl_err_str(rm_rc));
		}
			
		if ((rm_rc = rm_get_data(part_ptr, 
					 RM_PartitionUserName,
					 &owner_name)) != STATUS_OK) {
			error("rm_get_data(RM_PartitionUserName): %s",
			      bgl_err_str(rm_rc));
		} else
			bgl_record->owner_name = xstrdup(owner_name);
							
		if ((rm_rc = rm_get_data(part_ptr, 
					 RM_PartitionBPNum,
					 &bgl_record->bp_count))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionUserName): %s",
			      bgl_err_str(rm_rc));
		} 
				
		if ((rm_rc = rm_get_data(part_ptr, 
					 RM_PartitionSwitchNum,
					 &bgl_record->switch_count))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionUserName): %s",
			      bgl_err_str(rm_rc));
		} 
				
		bgl_record->part_lifecycle = STATIC;
				
	next_partition:
		/* if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) { */
/* 		} */
		rm_get_data(part_list, RM_PartListNextPart, &part_ptr);

		/* if ((rm_rc = rm_free_partition(part_ptr)) */
/* 		    != STATUS_OK) { */
/* 			error("rm_free_partition(): %s", */
/* 			      bgl_err_str(rm_rc)); */
/* 		} */

		//sleep(3);
		//debug("Removed Freed Partition %s",part_name);
	}
	rm_free_partition_list(part_list);
		
//#endif
#else
	if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
		fatal("init_bgl: rm_get_BGL(): %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}

	if ((rm_rc = rm_get_data(bgl, RM_BPNum, &bp_cnt)) != STATUS_OK) {
		error("rm_get_data(RM_BPNum): %s", bgl_err_str(rm_rc));
		rc = SLURM_ERROR;
		bp_cnt = 0;
	}
	
        if ((rm_rc = rm_get_data(bgl, RM_FirstBP, &bp_ptr))
            != STATUS_OK) {
                error("rm_get_data(RM_FirstBP): %s",
                      bgl_err_str(rm_rc));
                rc = SLURM_ERROR;
                return rc;
        }

        for (i=0; i<bp_cnt; i++) {

		if ((rm_rc = rm_get_data(bp_ptr, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			error("rm_get_data(RM_BPLoc): %s",
			      bgl_err_str(rm_rc));
			rc = SLURM_ERROR;
			break;
		}

		sprintf(node_name_tmp, "bgl%d%d%d", 
			bp_loc.X, bp_loc.Y, bp_loc.Z);
		
		if ((rm_rc = rm_get_data(bp_ptr, RM_BPPartID, &part_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_BPPartID: %s",
			      bgl_err_str(rm_rc));
			rc = SLURM_ERROR;
			break;
		}

		if (!part_id || (part_id[0] == '\0')) {
                        error("no part_id exiting");
			rc = SLURM_ERROR;
			break; 
		}
		//info("Node:%s in BglBlock:%s", node_name_tmp, part_id);
		if(strncmp("RMP",part_id,3)) 
			goto noadd;
		bgl_record = list_find_first(bgl_curr_part_list,
					       _part_list_find, part_id);
		if (!bgl_record) {
			/* New BGL partition record */
			if ((rm_rc = rm_get_partition(part_id, &part_ptr))
			    != STATUS_OK) {
				error("rm_get_partition(%s): %s",
				      part_id, bgl_err_str(rm_rc));
				rc = SLURM_ERROR;
				continue;
			}
			bgl_record = xmalloc(sizeof(bgl_record_t));
			list_push(bgl_curr_part_list, bgl_record);
				
			bgl_record->bgl_part_list = list_create(NULL);
			list_append(bgl_record->bgl_part_list, &pa_system_ptr->grid[bp_loc.X][bp_loc.Y][bp_loc.Z]);
			bgl_record->hostlist = hostlist_create(node_name_tmp);
			bgl_record->bgl_part_id = xstrdup(part_id);
				
			// need to get the 000x000 range for nodes
			// also need to get coords
				
			if ((rm_rc = rm_get_data(part_ptr,
						 RM_PartitionConnection,
						 &bgl_record->conn_type))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionConnection): %s",
				      bgl_err_str(rm_rc));
			}
			if ((rm_rc = rm_get_data(part_ptr, RM_PartitionMode,
						 &bgl_record->node_use))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionMode): %s",
				      bgl_err_str(rm_rc));
			}
			
			if ((rm_rc = rm_get_data(part_ptr, 
					RM_PartitionUserName,
					&owner_name)) != STATUS_OK) {
				error("rm_get_data(RM_PartitionUserName): %s",
					bgl_err_str(rm_rc));
			} else
				bgl_record->owner_name = xstrdup(owner_name);
							
			if ((rm_rc = rm_get_data(part_ptr, 
						 RM_PartitionBPNum,
						 &bgl_record->bp_count))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionUserName): %s",
				      bgl_err_str(rm_rc));
			} 
				
			if ((rm_rc = rm_get_data(part_ptr, 
						 RM_PartitionSwitchNum,
						 &bgl_record->switch_count))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionUserName): %s",
				      bgl_err_str(rm_rc));
			} 
				
			bgl_record->part_lifecycle = STATIC;
				

			if ((rm_rc = rm_free_partition(part_ptr))
			    != STATUS_OK) {
				error("rm_free_partition(): %s",
				      bgl_err_str(rm_rc));
			}
			

		} else {
			hostlist_push(bgl_record->hostlist, node_name_tmp);
			list_append(bgl_record->bgl_part_list, 
				    &pa_system_ptr->grid[bp_loc.X][bp_loc.Y][bp_loc.Z]);			
		}
	noadd:
                if ((rm_rc = rm_get_data(bgl, RM_NextBP, &bp_ptr))
		    != STATUS_OK) {
			error("rm_get_data(RM_NextBP): %s",
			      bgl_err_str(rm_rc));
			rc = SLURM_ERROR;
			break;
		}
	}
#endif
	/* perform post-processing for each bluegene partition */
	list_for_each(bgl_curr_part_list, _post_bgl_init_read, NULL);
	return rc;
}

static int _post_bgl_init_read(void *object, void *arg)
{
	bgl_record_t *bgl_record = (bgl_record_t *) object;
	int i = 1024;

	bgl_record->nodes = xmalloc(i);
	while (hostlist_ranged_string(bgl_record->hostlist, i,
			bgl_record->nodes) < 0) {
		i *= 2;
		xrealloc(bgl_record->nodes, i);
	}

	if (node_name2bitmap(bgl_record->nodes, 
			     false, 
			     &bgl_record->bitmap)) {
		error("Unable to convert nodes %s to bitmap", 
		      bgl_record->nodes);
	}
	print_bgl_record(bgl_record);

	return SLURM_SUCCESS;
}

static int  _part_list_find(void *object, void *key)
{
	bgl_record_t *part_ptr = (bgl_record_t *) object;
	pm_partition_id_t part_id = (pm_partition_id_t) key;

	if (!part_ptr->bgl_part_id) {
		error("_part_list_find: bgl_part_id == NULL");
		return -1;
	}
	if (!part_id) {
		error("_part_list_find: part_id == NULL");
		return -1;
	}

	if (strcmp(part_ptr->bgl_part_id, part_id) == 0)
		return 1;
	return 0;
}
#endif

