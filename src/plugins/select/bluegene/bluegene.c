/*****************************************************************************\
 *  bluegene.c - blue gene node configuration processing module. 
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov> et. al.
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
#include <stdio.h>

#define BUFSIZE 4096
#define BITSIZE 128
#define MMCS_POLL_TIME 120	/* poll MMCS for down switches and nodes 
				 * every 120 secs */
#define BGL_POLL_TIME 0	        /* poll bgl partitions every 3 secs */

#define _DEBUG 0

char* bgl_conf = NULL;

/* Global variables */
rm_BGL_t *bgl;
List bgl_list = NULL;			/* list of bgl_record entries */
List bgl_curr_part_list = NULL;  	/* current bgl partitions */
List bgl_found_part_list = NULL;  	/* found bgl partitions */
char *bluegene_blrts = NULL, *bluegene_linux = NULL, *bluegene_mloader = NULL;
char *bluegene_ramdisk = NULL, *bridge_api_file = NULL;
char *change_numpsets = NULL;
int numpsets;
bool agent_fini = false;
int bridge_api_verb = 0;
time_t last_bgl_update;
pthread_mutex_t part_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_part_to_free = 0;
int num_part_freed = 0;
int partitions_are_created = 0;

#ifdef HAVE_BGL_FILES
static pthread_mutex_t freed_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static int _update_bgl_record_state();
#endif

/* some local functions */
#ifdef HAVE_BGL
static int  _addto_node_list(bgl_record_t *bgl_record, int *start, int *end);
#endif
static void _set_bgl_lists();
static int  _validate_config_nodes(void);
static int  _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b);
static int  _parse_bgl_spec(char *in_line);
static void _process_nodes(bgl_record_t *bgl_record);
static int  _reopen_bridge_log(void);
static void _strip_13_10(char *line);

/* Initialize all plugin variables */
extern int init_bgl(void)
{
#ifdef HAVE_BGL_FILES
	int rc;
	rm_size3D_t bp_size;
	
	info("Attempting to contact MMCS");
	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		fatal("init_bgl: rm_set_serial(): %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}
	
	if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
		fatal("init_bgl: rm_get_BGL(): %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}

	if ((rc = rm_get_data(bgl, RM_Msize, &bp_size)) != STATUS_OK) {
		fatal("init_bgl: rm_get_data(): %s", bgl_err_str(rc));
		return SLURM_ERROR;
	}
	verbose("BlueGene configured with %d x %d x %d base partitions",
		bp_size.X, bp_size.Y, bp_size.Z);
	DIM_SIZE[X]=bp_size.X;
	DIM_SIZE[Y]=bp_size.Y;
	DIM_SIZE[Z]=bp_size.Z;
#endif
	pa_init(NULL);

	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
extern void fini_bgl(void)
{
	int rc;

	_set_bgl_lists();
	
	if (bgl_list) {
		list_destroy(bgl_list);
		bgl_list = NULL;
	}
	
	if (bgl_curr_part_list) {
		list_destroy(bgl_curr_part_list);
		bgl_curr_part_list = NULL;
	}
	
	if (bgl_found_part_list) {
		list_destroy(bgl_found_part_list);
		bgl_found_part_list = NULL;
	}

	xfree(bluegene_blrts);
	xfree(bluegene_linux);
	xfree(bluegene_mloader);
	xfree(bluegene_ramdisk);
	xfree(bridge_api_file);

#ifdef HAVE_BGL_FILES
	if(bgl)
		if ((rc = rm_free_BGL(bgl)) != STATUS_OK)
			error("rm_free_BGL(): %s", bgl_err_str(rc));
#endif	
	pa_fini();
}

extern void print_bgl_record(bgl_record_t* bgl_record)
{
	if (!bgl_record) {
		error("print_bgl_record, record given is null");
		return;
	}
#if _DEBUG
	info(" bgl_record: ");
	if (bgl_record->bgl_part_id)
		info("\tbgl_part_id: %s", bgl_record->bgl_part_id);
	info("\tnodes: %s", bgl_record->nodes);
	info("\tsize: %d", bgl_record->bp_count);
	info("\tgeo: %dx%dx%d", bgl_record->geo[X], bgl_record->geo[Y], 
	     bgl_record->geo[Z]);
	info("\tlifecycle: %s", convert_lifecycle(bgl_record->part_lifecycle));
	info("\tconn_type: %s", convert_conn_type(bgl_record->conn_type));
	info("\tnode_use: %s", convert_node_use(bgl_record->node_use));
	if (bgl_record->hostlist) {
		char buffer[BUFSIZE];
		hostlist_ranged_string(bgl_record->hostlist, BUFSIZE, buffer);
		info("\thostlist %s", buffer);
	}
	if (bgl_record->bitmap) {
		char bitstring[BITSIZE];
		bit_fmt(bitstring, BITSIZE, bgl_record->bitmap);
		info("\tbitmap: %s", bitstring);
	}
#else
	info("bgl_part_id=%s nodes=%s", bgl_record->bgl_part_id, 
	     bgl_record->nodes);
#endif
}

extern void destroy_bgl_record(void* object)
{
	bgl_record_t* bgl_record = (bgl_record_t*) object;

	if (bgl_record) {
		xfree(bgl_record->nodes);
		xfree(bgl_record->user_name);
		xfree(bgl_record->target_name);
		if(bgl_record->bgl_part_list)
			list_destroy(bgl_record->bgl_part_list);
		if(bgl_record->hostlist)
			hostlist_destroy(bgl_record->hostlist);
		if(bgl_record->bitmap)
			bit_free(bgl_record->bitmap);
		xfree(bgl_record->bgl_part_id);
		
		xfree(bgl_record);
	}
}


extern bgl_record_t *find_bgl_record(char *bgl_part_id)
{
	ListIterator itr;
	bgl_record_t *bgl_record = NULL;
		
	if(!bgl_part_id)
		return NULL;
			
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = 
			(bgl_record_t *) list_next(itr)) != NULL) {
			if(bgl_record->bgl_part_id)
				if (!strcmp(bgl_record->bgl_part_id, 
					    bgl_part_id))
					break;
		}
		list_iterator_destroy(itr);
		if(bgl_record)
			return bgl_record;
		else
			return NULL;
	} else {
		error("find_bgl_record: no bgl_list");
		return NULL;
	}
	
}
/* All changes to the bgl_list target_name must 
   be done before this function is called. 
*/
extern int update_partition_user(bgl_record_t *bgl_record) 
{
#ifdef HAVE_BGL_FILES
	int rc=0;
	struct passwd *pw_ent = NULL;
	
	if(!bgl_record->target_name) {
		error("Must set target_name to run update_partition_user.");
		return -1;
	}

	if((rc = remove_all_users(bgl_record->bgl_part_id, 
				  bgl_record->target_name))
	   == REMOVE_USER_ERR) {
		error("Something happened removing "
		      "users from partition %s", 
		      bgl_record->bgl_part_id);
		return -1;
	} else if (rc == REMOVE_USER_NONE) {
		if (strcmp(bgl_record->target_name, 
			   slurmctld_conf.slurm_user_name)) {
			info("Adding user %s to Partition %s",
			     bgl_record->target_name, 
			     bgl_record->bgl_part_id);
		
			if ((rc = rm_add_part_user(bgl_record->bgl_part_id, 
						   bgl_record->target_name)) 
			    != STATUS_OK) {
				error("rm_add_part_user(%s,%s): %s", 
				      bgl_record->bgl_part_id, 
				      bgl_record->target_name,
				      bgl_err_str(rc));
				return -1;
			} 
		}
	}
	
	if(strcmp(bgl_record->target_name, bgl_record->user_name)) {
		xfree(bgl_record->user_name);
		bgl_record->user_name = xstrdup(bgl_record->target_name);
		if((pw_ent = getpwnam(bgl_record->user_name)) == NULL) {
			error("getpwnam(%s): %m", bgl_record->user_name);
			return -1;
		} else {
			bgl_record->user_uid = pw_ent->pw_uid; 
		}		
		return 1;
	}
	
#endif
	return 0;
}

extern int remove_all_users(char *bgl_part_id, char *user_name) 
{
	int returnc = REMOVE_USER_NONE;
#ifdef HAVE_BGL_FILES
	char *user;
	rm_partition_t *part_ptr = NULL;
	int rc, i, user_count;

	if ((rc = rm_get_partition(bgl_part_id,  &part_ptr)) != STATUS_OK) {
		error("rm_get_partition(%s): %s", 
		      bgl_part_id, 
		      bgl_err_str(rc));
		return REMOVE_USER_ERR;
	}	
	
	if((rc = rm_get_data(part_ptr, RM_PartitionUsersNum, &user_count)) 
	   != STATUS_OK) {
		error("rm_get_data(RM_PartitionUsersNum): %s", 
		      bgl_err_str(rc));
		returnc = REMOVE_USER_ERR;
		user_count = 0;
	} else
		debug2("got %d users for %s",user_count, bgl_part_id);
	for(i=0; i<user_count; i++) {
		if(i) {
			if ((rc = rm_get_data(part_ptr, 
					      RM_PartitionNextUser, 
					      &user)) 
			    != STATUS_OK) {
				error("rm_get_partition(%s): %s", 
				      bgl_part_id, 
				      bgl_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_ptr, 
					      RM_PartitionFirstUser, 
					      &user)) 
			    != STATUS_OK) {
				error("rm_get_data(%s): %s", 
				      bgl_part_id, 
				      bgl_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		}
		if(!user) {
			error("No user was returned from database");
			continue;
		}
		if(!strcmp(user, slurmctld_conf.slurm_user_name)) {
			free(user);
			continue;
		}

		if(user_name) {
			if(!strcmp(user, user_name)) {
				returnc = REMOVE_USER_FOUND;
				free(user);
				continue;
			}
		}
		
		info("Removing user %s from Partition %s", 
		      user, 
		      bgl_part_id);
		if ((rc = rm_remove_part_user(bgl_part_id, user)) 
		    != STATUS_OK) {
			debug("user %s isn't on partition %s",
			      user, 
			      bgl_part_id);
		}
		free(user);
	}
	if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) {
		error("rm_free_partition(): %s", bgl_err_str(rc));
	}
#endif
	return returnc;
}

extern void set_part_user(bgl_record_t *bgl_record) 
{
	debug("resetting the boot state flag and "
	      "counter for partition %s.",
	      bgl_record->bgl_part_id);
	bgl_record->boot_state = 0;
	bgl_record->boot_count = 0;
	if(update_partition_user(bgl_record) == 1) 
		last_bgl_update = time(NULL);
	
	xfree(bgl_record->target_name);
	bgl_record->target_name = 
		xstrdup(slurmctld_conf.slurm_user_name);
}

extern char* convert_lifecycle(lifecycle_type_t lifecycle)
{
	if (lifecycle == DYNAMIC)
		return "DYNAMIC";
	else 
		return "STATIC";
}

extern char* convert_conn_type(rm_connection_type_t conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH): 
		return "MESH"; 
	case (SELECT_TORUS): 
		return "TORUS"; 
	case (SELECT_NAV):
		return "NAV";
	default:
		break;
	}
	return "";
}

extern char* convert_node_use(rm_partition_mode_t pt)
{
	switch (pt) {
	case (SELECT_COPROCESSOR_MODE): 
		return "COPROCESSOR"; 
	case (SELECT_VIRTUAL_NODE_MODE): 
		return "VIRTUAL"; 
	default:
		break;
	}
	return "";
}

/** 
 * sort the partitions by increasing size
 */
extern void sort_bgl_record_inc_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) _bgl_record_cmpf_inc);
}

/*
 * bluegene_agent - detached thread periodically updates status of
 * bluegene nodes. 
 * 
 * NOTE: I don't grab any locks here because slurm_drain_nodes grabs
 * the necessary locks.
 */
extern void *bluegene_agent(void *args)
{
	static time_t last_mmcs_test;
	static time_t last_bgl_test;
	int rc;

	last_mmcs_test = time(NULL) + MMCS_POLL_TIME;
	last_bgl_test = time(NULL) + BGL_POLL_TIME;
	while (!agent_fini) {
		time_t now = time(NULL);

		if (difftime(now, last_bgl_test) >= BGL_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			if(last_bgl_update) {
				last_bgl_test = now;
				if((rc = update_partition_list()) == 1)
					last_bgl_update = now;
				else if(rc == -1)
					error("Error "
					      "with update_partition_list");
			}
		}

		if (difftime(now, last_mmcs_test) >= MMCS_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			last_mmcs_test = now;
			test_mmcs_failures();	/* can run for a while */
		}	
				
		sleep(1);
	}
	return NULL;
}

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
extern char *bgl_err_str(status_t inx)
{
#ifdef HAVE_BGL_FILES
	switch (inx) {
	case STATUS_OK:
		return "Status OK";
	case PARTITION_NOT_FOUND:
		return "Partition not found";
	case JOB_NOT_FOUND:
		return "Job not found";
	case BP_NOT_FOUND:
		return "Base partition not found";
	case SWITCH_NOT_FOUND:
		return "Switch not found";
	case JOB_ALREADY_DEFINED:
		return "Job already defined";
	case CONNECTION_ERROR:
		return "Connection error";
	case INTERNAL_ERROR:
		return "Internal error";
	case INVALID_INPUT:
		return "Invalid input";
	case INCOMPATIBLE_STATE:
		return "Incompatible state";
	case INCONSISTENT_DATA:
		return "Inconsistent data";
	}
#endif

	return "?";
}

/*
 * create_static_partitions - create the static partitions that will be used
 *   for scheduling.  
 * IN/OUT part_list - (global, from slurmctld): SLURM's partition 
 *   configurations. Fill in bgl_part_id 
 * RET - success of fitting all configurations
 */
extern int create_static_partitions(List part_list)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	struct passwd *pw_ent = NULL;
	bgl_record_t *bgl_record = NULL, *found_record = NULL;
	char *name = NULL;
#ifndef HAVE_BGL_FILES
	static int block_inx = 0;
#else
	ListIterator itr_found;
	init_wires();
#endif
	slurm_mutex_lock(&part_state_mutex);
	reset_pa_system();
		
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = (bgl_record_t *) list_next(itr)) 
		       != NULL) {
			
			if(bgl_record->bp_count>0 
			   && !bgl_record->full_partition) {
				debug("adding %s %d%d%d",
				      bgl_record->nodes,
				      bgl_record->start[X],
				      bgl_record->start[Y],
				      bgl_record->start[Z]);
				name = set_bgl_part(NULL,
						    bgl_record->start, 
						    bgl_record->geo, 
						    bgl_record->conn_type);
				if(!name) {
					error("I was unable to make the "
					      "requested partition.");
					slurm_mutex_unlock(&part_state_mutex);
					return SLURM_ERROR;
				}
				xfree(name);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 1");
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	}

#ifdef HAVE_BGL_FILES
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = (bgl_record_t *) list_next(itr)) 
		       != NULL) {
			if(bgl_found_part_list) {
				itr_found = list_iterator_create(
					bgl_found_part_list);
				while ((found_record = (bgl_record_t*) 
					list_next(itr_found)) != NULL) {
					if (!strcmp(bgl_record->nodes, 
						    found_record->nodes)) {
						/* don't reboot this one */
						break;	
					}
				}
				list_iterator_destroy(itr_found);
			} else {
				error("create_static_partitions: "
				      "no bgl_found_part_list 1");
			}
			if(found_record == NULL) {
				if((rc = configure_partition(bgl_record)) 
				   == SLURM_ERROR) {
					list_iterator_destroy(itr);
					slurm_mutex_unlock(&part_state_mutex);
					return rc;
				}
				print_bgl_record(bgl_record);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 2");
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	}
#endif
	
	/* Here we are adding a partition that in for the entire machine 
	   just in case it isn't in the bluegene.conf file.
	*/
	
	reset_pa_system();

	bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
	
	bgl_record->nodes = xmalloc(sizeof(char)*13);
	if(DIM_SIZE[X]==1 && DIM_SIZE[Y]==1 && DIM_SIZE[Z]==1)
		sprintf(bgl_record->nodes, "bgl000");
       	else
		sprintf(bgl_record->nodes, "bgl[000x%d%d%d]", 
			DIM_SIZE[X]-1,  
			DIM_SIZE[Y]-1, 
			DIM_SIZE[Z]-1);
	bgl_record->geo[X] = DIM_SIZE[X]-1;
	bgl_record->geo[Y] = DIM_SIZE[Y]-1;
	bgl_record->geo[Z] = DIM_SIZE[Z]-1;
	
       	if(bgl_found_part_list) {
		itr = list_iterator_create(bgl_found_part_list);
		while ((found_record = (bgl_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(bgl_record->nodes, found_record->nodes)) {
				destroy_bgl_record(bgl_record);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_found_part_list 2");
	}
	
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((found_record = (bgl_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(bgl_record->nodes, found_record->nodes)) {
				destroy_bgl_record(bgl_record);
				list_iterator_destroy(itr);
				/* don't create total already defined */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 3");
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	}
	bgl_record->bgl_part_list = list_create(NULL);			
	bgl_record->hostlist = hostlist_create(NULL);
	/* bgl_record->boot_state = 0;		Implicit */
	_process_nodes(bgl_record);
	list_append(bgl_list, bgl_record);
	
	bgl_record->conn_type = SELECT_TORUS;
	bgl_record->user_name = xstrdup(slurmctld_conf.slurm_user_name);
	bgl_record->target_name = xstrdup(slurmctld_conf.slurm_user_name);
	if((pw_ent = getpwnam(bgl_record->user_name)) == NULL) {
		error("getpwnam(%s): %m", bgl_record->user_name);
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	} else {
		bgl_record->user_uid = pw_ent->pw_uid;
	}
	
	name = set_bgl_part(NULL,
			    bgl_record->start, 
			    bgl_record->geo, 
			    bgl_record->conn_type);

	if(!name) {
		error("I was unable to make the "
		      "requested partition.");
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	}
	xfree(name);
	bgl_record->node_use = SELECT_COPROCESSOR_MODE;
#ifdef HAVE_BGL_FILES
	if((rc = configure_partition(bgl_record)) == SLURM_ERROR) {
		slurm_mutex_unlock(&part_state_mutex);
		return rc;
	}
	print_bgl_record(bgl_record);

#else
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = (bgl_record_t*) list_next(itr))) {
			if (bgl_record->bgl_part_id)
				continue;
			bgl_record->bgl_part_id = xmalloc(8);
			snprintf(bgl_record->bgl_part_id, 8, "RMP%d", 
				 block_inx++);
			info("BGL PartitionID:%s Nodes:%s Conn:%s Mode:%s",
			     bgl_record->bgl_part_id, bgl_record->nodes,
			     convert_conn_type(bgl_record->conn_type),
			     convert_node_use(bgl_record->node_use));
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 4");
		slurm_mutex_unlock(&part_state_mutex);
		return SLURM_ERROR;
	}
#endif	/* HAVE_BGL_FILES */
	
no_total:
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		while ((bgl_record = (bgl_record_t*) list_next(itr)) != NULL) {
			if ((bgl_record->geo[X] == DIM_SIZE[X])
			    && (bgl_record->geo[Y] == DIM_SIZE[Y])
			    && (bgl_record->geo[Z] == DIM_SIZE[Z])) {
				debug("full partiton = %s.", 
				      bgl_record->bgl_part_id);
				bgl_record->full_partition = 1;
				
				break;
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 5");
	}
	last_bgl_update = time(NULL);
	slurm_mutex_unlock(&part_state_mutex);
#ifdef _PRINT_PARTS_AND_EXIT
	if(bgl_list) {
		itr = list_iterator_create(bgl_list);
		debug("\n\n");
		while ((found_record = (bgl_record_t *) list_next(itr)) 
		       != NULL) {
			print_bgl_record(found_record);
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_partitions: no bgl_list 5");
	}
 	exit(0);
#endif	/* _PRINT_PARTS_AND_EXIT */
	rc = SLURM_SUCCESS;
	//exit(0);
	return rc;
}

extern int bgl_free_partition(bgl_record_t *bgl_record)
{
#ifdef HAVE_BGL_FILES
	int rc;
	if(!bgl_record) {
		error("bgl_free_partition: there was no bgl_record");
		return SLURM_ERROR;
	}
	while (1) {
		if (bgl_record->state != -1
		    && bgl_record->state != RM_PARTITION_FREE 
		    && bgl_record->state != RM_PARTITION_DEALLOCATING) {
			debug("pm_destroy %s",bgl_record->bgl_part_id);
			if ((rc = pm_destroy_partition(
				     bgl_record->bgl_part_id)) 
			    != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					debug("partition %s is not found");
					break;
				}
				error("pm_destroy_partition(%s): %s "
				      "State = %d",
				      bgl_record->bgl_part_id, 
				      bgl_err_str(rc), bgl_record->state);
			}
		}
		
		if ((bgl_record->state == RM_PARTITION_FREE)
		    ||  (bgl_record->state == RM_PARTITION_ERROR))
			break;
		sleep(3);
	}
#endif
	return SLURM_SUCCESS;
}

/* Free multiple partitions in parallel */
extern void *mult_free_part(void *args)
{
#ifdef HAVE_BGL_FILES
	bgl_record_t *bgl_record = (bgl_record_t*) args;

	debug("freeing the partition %s.", bgl_record->bgl_part_id);
	bgl_free_partition(bgl_record);	
	debug("done\n");
	slurm_mutex_lock(&freed_cnt_mutex);
	num_part_freed++;
	slurm_mutex_unlock(&freed_cnt_mutex);
#endif	
	return NULL;
}

/* destroy multiple partitions in parallel */
extern void *mult_destroy_part(void *args)
{
#ifdef HAVE_BGL_FILES
	bgl_record_t *bgl_record = (bgl_record_t*) args;
	int rc;

	debug("removing the jobs on partition %s\n",
	      bgl_record->bgl_part_id);
	term_jobs_on_part(bgl_record->bgl_part_id);
	
	debug("destroying %s\n",
	      (char *)bgl_record->bgl_part_id);
	bgl_free_partition(bgl_record);
	
	rc = rm_remove_partition(
		bgl_record->bgl_part_id);
	if (rc != STATUS_OK) {
		error("rm_remove_partition(%s): %s",
		      bgl_record->bgl_part_id,
		      bgl_err_str(rc));
	} else
		debug("done\n");
	slurm_mutex_lock(&freed_cnt_mutex);
	num_part_freed++;
	slurm_mutex_unlock(&freed_cnt_mutex);

#endif	
	return NULL;
}

#ifdef HAVE_BGL
static int _addto_node_list(bgl_record_t *bgl_record, int *start, int *end)
{
	int node_count=0;
	int x,y,z;
	char node_name_tmp[7];
	debug3("%d%d%dx%d%d%d",
	     start[X],
	     start[Y],
	     start[Z],
	     end[X],
	     end[Y],
	     end[Z]);
	debug3("%d%d%d",
	     DIM_SIZE[X],
	     DIM_SIZE[Y],
	     DIM_SIZE[Z]);
	     
	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);
	
	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				sprintf(node_name_tmp, "bgl%d%d%d", 
					x, y, z);		
				list_append(bgl_record->bgl_part_list, 
					    &pa_system_ptr->grid[x][y][z]);
				node_count++;
			}
		}
	}
	return node_count;
}
#endif

static void _set_bgl_lists()
{
	bgl_record_t *bgl_record = NULL;
	
	slurm_mutex_lock(&part_state_mutex);
	if (bgl_found_part_list) {
		while ((bgl_record = list_pop(bgl_found_part_list)) != NULL) {
		}
	} else
		bgl_found_part_list = list_create(NULL);
	
	if (bgl_curr_part_list){
		while ((bgl_record = list_pop(bgl_curr_part_list)) != NULL){
			destroy_bgl_record(bgl_record);
		}
	} else
		bgl_curr_part_list = list_create(destroy_bgl_record);
	
/* empty the old list before reading new data */
	if (bgl_list) {
		while ((bgl_record = list_pop(bgl_list)) != NULL) {
			destroy_bgl_record(bgl_record);		
		}
	} else
		bgl_list = list_create(destroy_bgl_record);
	slurm_mutex_unlock(&part_state_mutex);
		
}

/*
 * Match slurm configuration information with current BGL partition 
 * configuration. Return SLURM_SUCCESS if they match, else an error 
 * code. Writes bgl_partition_id into bgl_list records.
 */

static int _validate_config_nodes(void)
{
	int rc = SLURM_ERROR;
#ifdef HAVE_BGL_FILES
	bgl_record_t* record = NULL;	
	bgl_record_t* init_record = NULL;
	ListIterator itr_conf;
	ListIterator itr_curr;
	rm_partition_mode_t node_use;
	
	/* read current bgl partition info into bgl_curr_part_list */
	if (read_bgl_partitions() == SLURM_ERROR)
		return SLURM_ERROR;
	
	if(!bgl_recover) 
		return SLURM_ERROR;
	
	if(bgl_list) {
		itr_conf = list_iterator_create(bgl_list);
		while ((record = (bgl_record_t*) list_next(itr_conf))) {
			/* translate hostlist to ranged 
			   string for consistent format
			   search here 
			*/
			node_use = SELECT_COPROCESSOR_MODE; 
		
			if(bgl_curr_part_list) {
				itr_curr = list_iterator_create(
					bgl_curr_part_list);	
				while ((init_record = (bgl_record_t*) 
					list_next(itr_curr)) 
				       != NULL) {
					if (strcasecmp(record->nodes, 
						       init_record->nodes))
						continue; /* wrong nodes */
					if (record->conn_type 
					    != init_record->conn_type)
						continue; /* wrong conn_type */
					record->bgl_part_id = xstrdup(
						init_record->bgl_part_id);
					record->state = init_record->state;
					record->node_use = 
						init_record->node_use;
					record->user_uid = 
						init_record->user_uid;
					record->user_name = xstrdup(
						init_record->user_name);
					record->target_name = xstrdup(
						init_record->target_name);
					record->boot_state = 
						init_record->boot_state;
					break;
				}
				list_iterator_destroy(itr_curr);
			} else {
				error("_validate_config_nodes: "
				      "no bgl_curr_part_list");
			}
			if (!record->bgl_part_id) {
				info("Partition found in bluegene.conf to be "
				     "created: Nodes:%s", 
				     record->nodes);
				rc = SLURM_ERROR;
			} else {
				list_append(bgl_found_part_list, record);
				info("Found existing BGL PartitionID:%s "
				     "Nodes:%s Conn:%s Mode:%s",
				     record->bgl_part_id, 
				     record->nodes,
				     convert_conn_type(record->conn_type),
				     convert_node_use(record->node_use));
			}
		}		
		list_iterator_destroy(itr_conf);
		if(bgl_curr_part_list) {
			itr_curr = list_iterator_create(
				bgl_curr_part_list);
			while ((init_record = (bgl_record_t*) 
				list_next(itr_curr)) 
			       != NULL) {
				_process_nodes(init_record);
				debug3("%s %d %d%d%d %d%d%d",
				       init_record->bgl_part_id, 
				       init_record->bp_count, 
				       init_record->geo[X],
				       init_record->geo[Y],
				       init_record->geo[Z],
				       DIM_SIZE[X],
				       DIM_SIZE[Y],
				       DIM_SIZE[Z]);
				if ((init_record->geo[X] == DIM_SIZE[X])
				    && (init_record->geo[Y] == DIM_SIZE[Y])
				    && (init_record->geo[Z] == DIM_SIZE[Z])) {
					record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
					list_append(bgl_list, record);
	
					record->full_partition = 1;
					record->bgl_part_id = xstrdup(
						init_record->bgl_part_id);
					record->nodes = xstrdup(
						init_record->nodes);
					record->state = init_record->state;
					record->node_use =
						init_record->node_use;
					record->user_uid =
						init_record->user_uid;
					record->user_name = xstrdup(
						init_record->user_name);
					record->target_name = xstrdup(
						init_record->target_name);
					record->conn_type = 
						init_record->conn_type;
					record->node_use = 
						init_record->node_use;
					record->bp_count = 
						init_record->bp_count;
					record->boot_state = 
						init_record->boot_state;
					record->switch_count = 
						init_record->switch_count;
					if((record->bitmap = 
					    bit_copy(init_record->bitmap)) 
					   == NULL) {
						error("Unable to copy "
						      "bitmap for", 
						      init_record->nodes);
					}
					list_append(bgl_found_part_list, 
						    record);
					info("Found existing BGL "
					     "PartitionID:%s "
					     "Nodes:%s Conn:%s Mode:%s",
					     record->bgl_part_id, 
					     record->nodes,
					     convert_conn_type(
						     record->conn_type),
					     convert_node_use(
						     record->node_use));
					break;
				}
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_validate_config_nodes: "
			      "no bgl_curr_part_list 2");
		}

		if(list_count(bgl_list) == list_count(bgl_curr_part_list))
			rc = SLURM_SUCCESS;
	} else {
		error("_validate_config_nodes: no bgl_list");
		rc = SLURM_ERROR;
	}
#endif

	return rc;
}

/* 
 * Comparator used for sorting partitions smallest to largest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b)
{
	if (rec_a->bp_count < rec_b->bp_count)
		return -1;
	else if (rec_a->bp_count > rec_b->bp_count)
		return 1;
	else
		return 0;
}

static int _delete_old_partitions(void)
{
#ifdef HAVE_BGL_FILES
	ListIterator itr_curr, itr_found;
	bgl_record_t *found_record = NULL, *init_record = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	List bgl_destroy_list = list_create(NULL);

	num_part_to_free = 0;
	num_part_freed = 0;
				
	if(!bgl_recover) {
		if(bgl_curr_part_list) {
			itr_curr = list_iterator_create(bgl_curr_part_list);
			while ((init_record = (bgl_record_t*) 
				list_next(itr_curr))) {
				slurm_attr_init(&attr_agent);
				if (pthread_attr_setdetachstate(
					    &attr_agent, 
					    PTHREAD_CREATE_JOINABLE))
					error("pthread_attr_setdetach"
						      "state error %m");

				list_push(bgl_destroy_list, init_record);
				retries = 0;
				while (pthread_create(&thread_agent, 
						      &attr_agent, 
						      mult_destroy_part, 
						      (void *)
						      init_record)) {
					error("pthread_create "
					      "error %m");
					if (++retries 
					    > MAX_PTHREAD_RETRIES)
						fatal("Can't create "
						      "pthread");
					/* sleep and retry */
					usleep(1000);	
				}
				num_part_to_free++;
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_partitions: "
			      "no bgl_curr_part_list 1");
			return SLURM_ERROR;
		}
	} else {
		if(bgl_curr_part_list) {
			itr_curr = list_iterator_create(bgl_curr_part_list);
			while ((init_record = (bgl_record_t*) 
				list_next(itr_curr))) {
				if(bgl_found_part_list) {
					itr_found = list_iterator_create(
						bgl_found_part_list);
					while ((found_record = (bgl_record_t*) 
						list_next(itr_found)) 
					       != NULL) {
						if (!strcmp(init_record->
							    bgl_part_id, 
							    found_record->
							    bgl_part_id)) {
							/* don't delete 
							   this one 
							*/
							break;	
						}
					}
					list_iterator_destroy(itr_found);
				} else {
					error("_delete_old_partitions: "
					      "no bgl_found_part_list");
					return SLURM_ERROR;
				}
				if(found_record == NULL) {
					slurm_attr_init(&attr_agent);
					if (pthread_attr_setdetachstate(
						    &attr_agent, 
						    PTHREAD_CREATE_JOINABLE))
						error("pthread_attr_setdetach"
						      "state error %m");
				
					list_push(bgl_destroy_list, 
						  init_record);
					retries = 0;
					while (pthread_create(
						       &thread_agent, 
						       &attr_agent, 
						       mult_destroy_part, 
						       (void *)init_record)) {
						error("pthread_create "
						      "error %m");
						if (++retries 
						    > MAX_PTHREAD_RETRIES)
							fatal("Can't create "
							      "pthread");
						/* sleep and retry */
						usleep(1000);	
					}
					num_part_to_free++;
				}
			}		
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_partitions: "
			      "no bgl_curr_part_list 2");
			return SLURM_ERROR;
		}
	}
	retries=30;
	while(num_part_to_free != num_part_freed) {
		_update_bgl_record_state(bgl_destroy_list);
		if(retries==30) {
			info("Waiting for old partitions to be "
			     "freed.  Have %d of %d",
			     num_part_freed, 
			     num_part_to_free);
			retries=0;
		}
		retries++;
		sleep(1);
	}
	list_destroy(bgl_destroy_list);
	
#endif	
	return SLURM_SUCCESS;
}

static char *_get_bgl_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc;
	int i;

	if (!val)
		return xstrdup(BLUEGENE_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("bluegene.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "bluegene.conf");
	return rc;
}

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * partitions are static/dynamic, torus/mesh, etc.
 */
extern int read_bgl_conf(void)
{
	FILE *bgl_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code = SLURM_SUCCESS;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;

	debug("Reading the bluegene.conf file");

	/* check if config file has changed */
	if (!bgl_conf)
		bgl_conf = _get_bgl_conf();
	if (stat(bgl_conf, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bgl_conf);
	if (last_config_update) {
		if(last_config_update == config_stat.st_mtime)
			debug("bluegene.conf unchanged");
		else
			debug("bluegene.conf changed, doing nothing");
		_reopen_bridge_log();
		last_config_update = config_stat.st_mtime; 
		return SLURM_SUCCESS;
	}
	last_config_update = config_stat.st_mtime; 

	/* initialization */
	/* bgl_conf defined in bgl_node_alloc.h */
	bgl_spec_file = fopen(bgl_conf, "r");
	if (bgl_spec_file == NULL)
		fatal("_read_bgl_conf error opening file %s, %m",
		      bgl_conf);
	
	_set_bgl_lists();	
	
	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUFSIZE, bgl_spec_file) != NULL) {
		line_num++;
		_strip_13_10(in_line);
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("_read_bgl_config line %d, of input file %s "
			      "too long", line_num, bgl_conf);
			fclose(bgl_spec_file);
			xfree(bgl_conf);
			return E2BIG;
		}

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		/* escape sequence "\#" translated to "#" */
		for (i = 0; i < BUFSIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < BUFSIZE; j++) {
					in_line[j - 1] = in_line[j];
				}
				continue;
			}
			in_line[i] = (char) NULL;
			break;
		}
		
		/* parse what is left, non-comments */
		/* partition configuration parameters */
		error_code = _parse_bgl_spec(in_line);
		
		/* report any leftover strings on input line */
		report_leftover(in_line, line_num);
	}
	fclose(bgl_spec_file);
	xfree(bgl_conf);
		
	if (!bluegene_blrts)
		fatal("BlrtsImage not configured in bluegene.conf");
	if (!bluegene_linux)
		fatal("LinuxImage not configured in bluegene.conf");
	if (!bluegene_mloader)
		fatal("MloaderImage not configured in bluegene.conf");
	if (!bluegene_ramdisk)
		fatal("RamDiskImage not configured in bluegene.conf");
	if (!bridge_api_file)
		info("BridgeAPILogFile not configured in bluegene.conf");
	else
		_reopen_bridge_log();	
	if (!numpsets)
		info("Warning: Numpsets not configured in bluegene.conf");
//#if 0	
	/* Check to see if the configs we have are correct */
	if (_validate_config_nodes() == SLURM_ERROR) { 
		_delete_old_partitions();
	}
//#endif
	/* looking for partitions only I created */
	if (create_static_partitions(NULL) == SLURM_ERROR) {
		/* error in creating the static partitions, so
		 * partitions referenced by submitted jobs won't
		 * correspond to actual slurm partitions/bgl
		 * partitions.
		 */
		fatal("Error, could not create the static partitions");
		return SLURM_ERROR;
	}
	debug("Partitions have finished being created.");
	partitions_are_created = 1;
	
	return error_code;
}

/* Explicitly strip out  new-line and carriage-return */
static void _strip_13_10(char *line)
{
	int len = strlen(line);
	int i;

	for(i=0;i<len;i++) {
		if(line[i]==13 || line[i]==10) {
			line[i] = '\0';
			return;
		}
	}
}

/*
 *
 * _parse_bgl_spec - parse the partition specification, build table and 
 *	set values
 * IN/OUT in_line - line from the configuration file, parsed keywords 
 *	and values replaced by blanks
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
static int _parse_bgl_spec(char *in_line)
{
	int error_code = SLURM_SUCCESS;
	char *nodes = NULL, *conn_type = NULL, *node_use = NULL;
	char *blrts_image = NULL,   *linux_image = NULL;
	char *mloader_image = NULL, *ramdisk_image = NULL;
	char *api_file = NULL;
	int pset_num=-1, api_verb=-1;
	bgl_record_t *bgl_record = NULL;
	struct passwd *pw_ent = NULL;
	
	//info("in_line = %s",in_line);
	error_code = slurm_parser(in_line,
				  "BlrtsImage=", 's', &blrts_image,
				  "LinuxImage=", 's', &linux_image,
				  "MloaderImage=", 's', &mloader_image,
				  "Numpsets=", 'd', &pset_num,
				  "BridgeAPIVerbose=", 'd', &api_verb,
				  "BridgeAPILogFile=", 's', &api_file,
				  "Nodes=", 's', &nodes,
				  "RamDiskImage=", 's', &ramdisk_image,
				  "Type=", 's', &conn_type,
				  "Use=", 's', &node_use,
				  "END");

	if (error_code)
		return SLURM_ERROR;

	/* Process system-wide info */
	if (blrts_image) {
		xfree(bluegene_blrts);
		bluegene_blrts = blrts_image;
		blrts_image = NULL;	/* nothing left to xfree */
	}
	if (linux_image) {
		xfree(bluegene_linux);
		bluegene_linux = linux_image;
		linux_image = NULL;	/* nothing left to xfree */
	}
	if (mloader_image) {
		xfree(bluegene_mloader);
		bluegene_mloader = mloader_image;
		mloader_image = NULL;	/* nothing left to xfree */
	}
	if (ramdisk_image) {
		xfree(bluegene_ramdisk);
		bluegene_ramdisk = ramdisk_image;
		ramdisk_image = NULL;	/* nothing left to xfree */
	}
	if (api_file) {
		xfree(bridge_api_file);
		bridge_api_file = api_file;
		api_file = NULL;	/* nothing left to xfree */
	}
	
	if (pset_num > 0) {
		numpsets = pset_num;
	}
	if (api_verb >= 0) {
		bridge_api_verb = api_verb;
	}

	/* Process node information */
	if (!nodes)
		return SLURM_SUCCESS;	/* not partition line. */
	bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
	list_append(bgl_list, bgl_record);
	
	bgl_record->user_name = xstrdup(slurmctld_conf.slurm_user_name);
	if((pw_ent = getpwnam(bgl_record->user_name)) == NULL) {
		error("getpwnam(%s): %m", bgl_record->user_name);
	} else {
		bgl_record->user_uid = pw_ent->pw_uid;
	}
	bgl_record->bgl_part_list = list_create(NULL);			
	bgl_record->hostlist = hostlist_create(NULL);
	/* bgl_record->boot_state = 0; 	Implicit */
	/* bgl_record->state = 0;	Implicit */
		
	bgl_record->nodes = xstrdup(nodes);
	xfree(nodes);	/* pointer moved, nothing left to xfree */
	
	_process_nodes(bgl_record);
	if (!conn_type || !strcasecmp(conn_type,"TORUS"))
		bgl_record->conn_type = SELECT_TORUS;
	else
		bgl_record->conn_type = SELECT_MESH;
	
	xfree(conn_type);

	bgl_record->node_use = SELECT_COPROCESSOR_MODE;

#if _DEBUG
	debug("_parse_bgl_spec: added nodes=%s type=%s use=%s", 
	      bgl_record->nodes, 
	      convert_conn_type(bgl_record->conn_type), 
	      convert_node_use(bgl_record->node_use));
#endif
	return SLURM_SUCCESS;
}

static void _process_nodes(bgl_record_t *bgl_record)
{
#ifdef HAVE_BGL
	int j=0, number;
	int start[PA_SYSTEM_DIMENSIONS];
	int end[PA_SYSTEM_DIMENSIONS];
	ListIterator itr;
	pa_node_t* pa_node = NULL;
	
	bgl_record->bp_count = 0;
				
	while (bgl_record->nodes[j] != '\0') {
		if ((bgl_record->nodes[j] == '['
		     || bgl_record->nodes[j] == ',')
		    && (bgl_record->nodes[j+8] == ']' 
			|| bgl_record->nodes[j+8] == ',')
		    && (bgl_record->nodes[j+4] == 'x'
			|| bgl_record->nodes[j+4] == '-')) {
			j++;
			number = atoi(bgl_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(bgl_record->nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 3;
			if(!bgl_record->bp_count) {
				bgl_record->start[X] = start[X];
				bgl_record->start[Y] = start[Y];
				bgl_record->start[Z] = start[Z];
				debug2("start is %d%d%d",
				      bgl_record->start[X],
				      bgl_record->start[Y],
				      bgl_record->start[Z]);
			}
			bgl_record->bp_count += _addto_node_list(bgl_record, 
								 start, 
								 end);
			if(bgl_record->nodes[j] != ',')
				break;
			j--;
		} else if((bgl_record->nodes[j] < 58 
			   && bgl_record->nodes[j] > 47)) {
					
			number = atoi(bgl_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			if(!bgl_record->bp_count) {
				bgl_record->start[X] = start[X];
				bgl_record->start[Y] = start[Y];
				bgl_record->start[Z] = start[Z];
				debug2("start is %d%d%d",
				      bgl_record->start[X],
				      bgl_record->start[Y],
				      bgl_record->start[Z]);
			}
			bgl_record->bp_count += _addto_node_list(bgl_record, 
								 start, 
								 start);
			if(bgl_record->nodes[j] != ',')
				break;
		}
		j++;
	}
	j=0;
	bgl_record->geo[X] = 0;
	bgl_record->geo[Y] = 0;
	bgl_record->geo[Z] = 0;
	end[X] = -1;
	end[Y] = -1;
	end[Z] = -1;
	
	itr = list_iterator_create(bgl_record->bgl_part_list);
	while ((pa_node = list_next(itr)) != NULL) {
		if(pa_node->coord[X]>end[X]) {
			bgl_record->geo[X]++;
			end[X] = pa_node->coord[X];
		}
		if(pa_node->coord[Y]>end[Y]) {
			bgl_record->geo[Y]++;
			end[Y] = pa_node->coord[Y];
		}
		if(pa_node->coord[Z]>end[Z]) {
			bgl_record->geo[Z]++;
			end[Z] = pa_node->coord[Z];
		}
	}
	list_iterator_destroy(itr);
	debug3("geo = %d%d%d\n",
	       bgl_record->geo[X],
	       bgl_record->geo[Y],
	       bgl_record->geo[Z]);
		     
	if (node_name2bitmap(bgl_record->nodes, 
			     false, 
			     &bgl_record->bitmap)) {
		error("Unable to convert nodes %s to bitmap", 
		      bgl_record->nodes);
	}
#endif
	return;
}

static int _reopen_bridge_log(void)
{
	static FILE *fp = NULL;

	if (bridge_api_file == NULL)
		return SLURM_SUCCESS;

	if(fp)
		fclose(fp);
	fp = fopen(bridge_api_file,"a");
	if (fp == NULL) { 
		error("can't open file for bridgeapi.log at %s: %m", 
		      bridge_api_file);
		return SLURM_ERROR;
	}

#ifdef HAVE_BGL_FILES
	setSayMessageParams(fp, bridge_api_verb);
#else
	if (fprintf(fp, "bridgeapi.log to write here at level %d\n", 
			bridge_api_verb) < 20) {
		error("can't write to bridgeapi.log: %m");
		return SLURM_ERROR;
	}
#endif
		
	return SLURM_SUCCESS;
}

#ifdef HAVE_BGL_FILES
static int _update_bgl_record_state(List bgl_destroy_list)
{
	rm_partition_state_flag_t part_state = PARTITION_ALL_FLAG;
	char *name = NULL;
	rm_partition_list_t *part_list = NULL;
	int j, rc, func_rc = SLURM_SUCCESS, num_parts = 0;
	rm_partition_state_t state;
	rm_partition_t *part_ptr = NULL;
	ListIterator itr;
	bgl_record_t* bgl_record = NULL;	

	if(!bgl_destroy_list) {
		return SLURM_SUCCESS;
	}
	
	if ((rc = rm_get_partitions_info(part_state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions_info(): %s", bgl_err_str(rc));
		return SLURM_ERROR; 
	}

	if ((rc = rm_get_data(part_list, RM_PartListSize, &num_parts))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", bgl_err_str(rc));
		func_rc = SLURM_ERROR;
		num_parts = 0;
	}
			
	for (j=0; j<num_parts; j++) {
		if (j) {
			if ((rc = rm_get_data(part_list, 
					      RM_PartListNextPart, 
					      &part_ptr)) 
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
				      bgl_err_str(rc));
				func_rc = SLURM_ERROR;
				break;
			}
		} else {
			if ((rc = rm_get_data(part_list, 
					      RM_PartListFirstPart, 
					      &part_ptr)) 
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s",
				      bgl_err_str(rc));
				func_rc = SLURM_ERROR;
				break;
			}
		}
		if ((rc = rm_get_data(part_ptr, 
				      RM_PartitionID, 
				      &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s", 
			      bgl_err_str(rc));
			func_rc = SLURM_ERROR;
			break;
		}
		if (!name) {
			error("RM_Partition is NULL");
			continue;
		}
		
		itr = list_iterator_create(bgl_destroy_list);
		while ((bgl_record = (bgl_record_t*) list_next(itr))) {	
			if(!bgl_record->bgl_part_id) 
				continue;
			if(strcmp(bgl_record->bgl_part_id, name)) {
				continue;		
			}
		       
			slurm_mutex_lock(&part_state_mutex);
			if ((rc = rm_get_data(part_ptr, 
					      RM_PartitionState, 
					      &state))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionState): %s",
				      bgl_err_str(rc));
			} else if(bgl_record->state != state) {
				debug("state of Partition %s was %d "
				      "and now is %d",
				      name, bgl_record->state, state);
				bgl_record->state = state;
			}
			slurm_mutex_unlock(&part_state_mutex);
			break;
		}
		list_iterator_destroy(itr);
		free(name);
	}
	
	if ((rc = rm_free_partition_list(part_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s", bgl_err_str(rc));
	}
	return func_rc;
}
#endif /* HAVE_BGL_FILES */
