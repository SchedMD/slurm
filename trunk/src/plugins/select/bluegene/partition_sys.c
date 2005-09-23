/*****************************************************************************\
 *  partition_sys.c - component used for wiring up the partitions
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
static int _post_bgl_init_read(void *object, void *arg);

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
 * initialize the BGL partition in the resource manager 
 */
static void _pre_allocate(bgl_record_t *bgl_record)
{
	int rc;

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionBlrtsImg,   
			bluegene_blrts)) != STATUS_OK)
		error("rm_set_data(RM_PartitionBlrtsImg)", bgl_err_str(rc));

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionLinuxImg,   
			bluegene_linux)) != STATUS_OK) 
		error("rm_set_data(RM_PartitionLinuxImg)", bgl_err_str(rc));

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionMloaderImg, 
			bluegene_mloader)) != STATUS_OK)
		error("rm_set_data(RM_PartitionMloaderImg)", bgl_err_str(rc));

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionRamdiskImg, 
			bluegene_ramdisk)) != STATUS_OK)
		error("rm_set_data(RM_PartitionRamdiskImg)", bgl_err_str(rc));

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionConnection, 
			&bgl_record->conn_type)) != STATUS_OK)
		error("rm_set_data(RM_PartitionConnection)", bgl_err_str(rc));

	/* if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionMode,  */
/* 			&bgl_record->node_use)) != STATUS_OK) */
/* 		error("rm_set_data(RM_PartitionMode)", bgl_err_str(rc)); */

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionPsetsPerBP, 
			&numpsets)) != STATUS_OK)
		error("rm_set_data(RM_PartitionPsetsPerBP)", bgl_err_str(rc));

	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionUserName, 
			slurmctld_conf.slurm_user_name)) != STATUS_OK)
		error("rm_set_data(RM_PartitionUserName)", bgl_err_str(rc));
/* 	info("setting it here"); */
/* 	bgl_record->bgl_part_id = "RMP101"; */
/* 	if ((rc = rm_set_data(bgl_record->bgl_part, RM_PartitionID,  */
/* 			&bgl_record->bgl_part_id)) != STATUS_OK) */
/* 		error("rm_set_data(RM_PartitionID)", bgl_err_str(rc)); */
}

/** 
 * add the partition record to the DB
 */
static int _post_allocate(bgl_record_t *bgl_record)
{
	int rc, i;
	pm_partition_id_t part_id;
	struct passwd *pw_ent = NULL;
	/* Add partition record to the DB */
	debug("adding partition\n");
	
	for(i=0;i<MAX_ADD_RETRY; i++) {
		if ((rc = rm_add_partition(bgl_record->bgl_part)) != STATUS_OK) {
			error("rm_add_partition(): %s", bgl_err_str(rc));
			rc = SLURM_ERROR;
		} else {
			rc = SLURM_SUCCESS;
			break;
		}
		sleep(3);
	}
	if(rc == SLURM_ERROR) {
		if ((rc = rm_free_partition(bgl_record->bgl_part)) 
		    != STATUS_OK)
			error("rm_free_partition(): %s", bgl_err_str(rc));
		fatal("couldn't add last partition.");
	}
	debug("done adding\n");
	
	/* Get back the new partition id */
	if ((rc = rm_get_data(bgl_record->bgl_part, RM_PartitionID, &part_id))
			 != STATUS_OK) {
		error("rm_get_data(RM_PartitionID): %s", bgl_err_str(rc));
		bgl_record->bgl_part_id = xstrdup("UNKNOWN");
	} else {
		if(!part_id) {
			error("No Partition ID was returned from database");
			return SLURM_ERROR;
		}
		bgl_record->bgl_part_id = xstrdup(part_id);
		
		free(part_id);
		
		xfree(bgl_record->target_name);
		bgl_record->target_name = 
			xstrdup(slurmctld_conf.slurm_user_name);

		xfree(bgl_record->user_name);
		bgl_record->user_name = 
			xstrdup(slurmctld_conf.slurm_user_name);

		if((pw_ent = getpwnam(bgl_record->user_name)) == NULL) {
			error("getpwnam(%s): %m", bgl_record->user_name);
		} else {
			bgl_record->user_uid = pw_ent->pw_uid;
		} 
		last_bgl_update = time(NULL);
		
	}
	/* We are done with the partition */
	if ((rc = rm_free_partition(bgl_record->bgl_part)) != STATUS_OK)
		error("rm_free_partition(): %s", bgl_err_str(rc));	
	return rc;
}


extern int configure_partition(bgl_record_t *bgl_record)
{
	/* new partition to be added */
	rm_new_partition(&bgl_record->bgl_part); 
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

	int bp_cnt, i;
	rm_element_t *bp_ptr = NULL;
	rm_bp_id_t bpid;
	rm_partition_t *part_ptr = NULL;
	char node_name_tmp[7], *user_name = NULL;
	bgl_record_t *bgl_record = NULL;
	struct passwd *pw_ent = NULL;
	
	int *coord;
	int part_number, part_count;
	char *part_name = NULL;
	rm_partition_list_t *part_list = NULL;
	rm_partition_state_flag_t state = PARTITION_ALL_FLAG;
	

	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		error("rm_set_serial(): %s\n", bgl_err_str(rc));
		return SLURM_ERROR;
	}			
	set_bp_map();
	if ((rc = rm_get_partitions_info(state, &part_list))
			!= STATUS_OK) {
		error("rm_get_partitions_info(): %s", bgl_err_str(rc));
		return SLURM_ERROR;
		
	}
	
	if ((rc = rm_get_data(part_list, RM_PartListSize, &part_count))
			!= STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", bgl_err_str(rc));
		part_count = 0;
	}
	
	for(part_number=0; part_number<part_count; part_number++) {
		
		if (part_number) {
			if ((rc = rm_get_data(part_list, RM_PartListNextPart,
					&part_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
					bgl_err_str(rc));
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list, RM_PartListFirstPart, 
					&part_ptr)) != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart): %s",
					bgl_err_str(rc));
				break;
			}
		}

		if ((rc = rm_get_data(part_ptr, RM_PartitionID, &part_name))
				!= STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s", 
				bgl_err_str(rc));
			continue;
		}

		if(!part_name) {
			error("No Partition ID was returned from database");
			continue;
		}

		if(strncmp("RMP", part_name, 3)) {
			free(part_name);
			continue;
		}
		if(bgl_recover) 
			if ((rc = rm_get_partition(part_name, &part_ptr))
			    != STATUS_OK) {
				error("Partition %s doesn't exist.",
				      part_name);
				rc = SLURM_ERROR;
				free(part_name);
				break;
			}
		/* New BGL partition record */		
		
		bgl_record = xmalloc(sizeof(bgl_record_t));
		list_push(bgl_curr_part_list, bgl_record);
									
		bgl_record->bgl_part_id = xstrdup(part_name);
		
		free(part_name);

		bgl_record->state = -1;
		if ((rc = rm_get_data(part_ptr, RM_PartitionBPNum, &bp_cnt)) 
		    != STATUS_OK) {
			error("rm_get_data(RM_BPNum): %s", bgl_err_str(rc));
			bp_cnt = 0;
		}
		
		if(bp_cnt==0)
			goto clean_up;

		bgl_record->bgl_part_list = list_create(NULL);
		bgl_record->hostlist = hostlist_create(NULL);
		
		for (i=0; i<bp_cnt; i++) {
			if(i) {
				if ((rc = rm_get_data(part_ptr, 
						      RM_PartitionNextBP, 
						      &bp_ptr))
				    != STATUS_OK) {
					error("rm_get_data(RM_NextBP): %s",
					      bgl_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			} else {
				if ((rc = rm_get_data(part_ptr, 
						      RM_PartitionFirstBP, 
						      &bp_ptr))
				    != STATUS_OK) {
					error("rm_get_data(RM_FirstBP): %s", 
					      bgl_err_str(rc));
					rc = SLURM_ERROR;
					if (bgl_recover)
						rm_free_partition(part_ptr);
					return rc;
				}	
			}
			if ((rc = rm_get_data(bp_ptr, RM_BPID, &bpid))
			    != STATUS_OK) {
				error("rm_get_data(RM_BPLoc): %s",
				      bgl_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}

			if(!bpid) {
				error("No BP ID was returned from database");
				continue;
			}

			coord = find_bp_loc(bpid);

			free(bpid);

			if(!coord)
				fatal("No contact with db2. Shutting down.");
				
			snprintf(node_name_tmp, sizeof(node_name_tmp), 
				"bgl%d%d%d", 
				coord[X], coord[Y], coord[Z]);
		
			hostlist_push(bgl_record->hostlist, node_name_tmp);
		}	
		
		// need to get the 000x000 range for nodes
		// also need to get coords
				
		if ((rc = rm_get_data(part_ptr, RM_PartitionConnection,
					 &bgl_record->conn_type))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionConnection): %s",
			      bgl_err_str(rc));
		}
		if ((rc = rm_get_data(part_ptr, RM_PartitionMode,
					 &bgl_record->node_use))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionMode): %s",
			      bgl_err_str(rc));
		}
			
		if ((rc = rm_get_data(part_ptr, RM_PartitionState,
					 &bgl_record->state)) != STATUS_OK) {
			error("rm_get_data(RM_PartitionState): %s",
			      bgl_err_str(rc));
		} else if(bgl_record->state == RM_PARTITION_CONFIGURING)
			bgl_record->boot_state = 1;
		else
			bgl_record->boot_state = 0;
			
		debug("Partition %s is in state %d",
		      bgl_record->bgl_part_id, 
		      bgl_record->state);
		
		if ((rc = rm_get_data(part_ptr, RM_PartitionUsersNum,
					 &bp_cnt)) != STATUS_OK) {
			error("rm_get_data(RM_PartitionUsersNum): %s",
			      bgl_err_str(rc));
		} else {
			if(bp_cnt==0) {
				bgl_record->user_name = 
					xstrdup(slurmctld_conf.
						slurm_user_name);
				bgl_record->target_name = 
					xstrdup(slurmctld_conf.
						slurm_user_name);
			} else {
				user_name = NULL;
				if ((rc = rm_get_data(part_ptr, RM_PartitionFirstUser, 
						&user_name)) != STATUS_OK) {
					error("rm_get_data(RM_PartitionFirstUser): %s",
						bgl_err_str(rc));
				}
				if(!user_name) {
					error("No user name was "
					      "returned from database");
					goto clean_up;
				}
				bgl_record->user_name = xstrdup(user_name);
			
				if(!bgl_record->boot_state)
					bgl_record->target_name = 
						xstrdup(slurmctld_conf.
							slurm_user_name);
				else
					bgl_record->target_name = 
						xstrdup(user_name);
				
				free(user_name);
					
			}
			if((pw_ent = getpwnam(bgl_record->user_name)) 
			   == NULL) {
				error("getpwnam(%s): %m", 
				      bgl_record->user_name);
			} else {
				bgl_record->user_uid = pw_ent->pw_uid;
			} 
		}

		if ((rc = rm_get_data(part_ptr, RM_PartitionBPNum,
				&bgl_record->bp_count)) != STATUS_OK) {
			error("rm_get_data(RM_PartitionBPNum): %s",
			      bgl_err_str(rc));
		} 
		debug("has %d BPs",
		      bgl_record->bp_count);
				
		if ((rc = rm_get_data(part_ptr, RM_PartitionSwitchNum,
				&bgl_record->switch_count)) != STATUS_OK) {
			error("rm_get_data(RM_PartitionSwitchNum): %s",
			      bgl_err_str(rc));
		} 
				
		bgl_record->part_lifecycle = STATIC;
						
clean_up:	if (bgl_recover
		&&  ((rc = rm_free_partition(part_ptr)) != STATUS_OK)) {
			error("rm_free_partition(): %s", bgl_err_str(rc));
		}
	}
	rm_free_partition_list(part_list);

	/* perform post-processing for each bluegene partition */
	if(bgl_recover)
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
	//print_bgl_record(bgl_record);

	return SLURM_SUCCESS;
}

#endif

