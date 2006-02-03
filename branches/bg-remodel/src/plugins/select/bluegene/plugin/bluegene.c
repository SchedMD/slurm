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
#define BITSZE 128
#define MMCS_POLL_TIME 120	/* poll MMCS for down switches and nodes 
				 * every 120 secs */
#define BG_POLL_TIME 0	        /* poll bg blocks every 3 secs */

#define _DEBUG 0

char* bg_conf = NULL;

/* Global variables */
rm_BGL_t *bg;
List bg_list = NULL;			/* total list of bg_record entries */
List bg_curr_block_list = NULL;  	/* current bg blocks in bluegene.conf*/
List bg_found_block_list = NULL;  	/* found bg blocks already on system */
List bg_job_block_list = NULL;  	/* jobs running in these blocks */
List bg_booted_block_list = NULL;  	/* blocks that are booted */
char *bluegene_blrts = NULL, *bluegene_linux = NULL, *bluegene_mloader = NULL;
char *bluegene_ramdisk = NULL, *bridge_api_file = NULL; 
char *bluegene_layout_mode = NULL;
int bluegene_numpsets = 0;
int bluegene_mp_node_cnt = 0;
int bluegene_nc_node_cnt = 0;
bool agent_fini = false;
int bridge_api_verb = 0;
time_t last_bg_update;
pthread_mutex_t block_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int num_block_to_free = 0;
int num_block_freed = 0;
int blocks_are_created = 0;
bg_record_t *full_system_block = NULL;

#ifdef HAVE_BG_FILES
  static pthread_mutex_t freed_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
  static int _update_bg_record_state(List bg_destroy_list);
#else
# if BA_SYSTEM_DIMENSIONS==3
    int max_dim[BA_SYSTEM_DIMENSIONS] = { 0, 0, 0 };
# else
    int max_dim[BA_SYSTEM_DIMENSIONS] = { 0 };
# endif
#endif

/* some local functions */
#ifdef HAVE_BG
static int  _addto_node_list(bg_record_t *bg_record, int *start, int *end);
#endif
#ifdef HAVE_BG_FILES
#endif
static void _set_bg_lists();
static int  _validate_config_nodes(void);
static int  _bg_record_cmpf_inc(bg_record_t *rec_a, bg_record_t *rec_b);
static int _delete_old_blocks(void);
static char *_get_bg_conf(void);
static void _strip_13_10(char *line);
static int  _parse_bg_spec(char *in_line);
static void _process_nodes(bg_record_t *bg_record);
static int  _reopen_bridge_log(void);

/* Initialize all plugin variables */
extern int init_bg(void)
{
#ifdef HAVE_BG_FILES
	int rc;
	rm_size3D_t bp_size;
	
	info("Attempting to contact MMCS");
	if ((rc = rm_set_serial(BG_SERIAL)) != STATUS_OK) {
		fatal("init_bg: rm_set_serial(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}
	
	if ((rc = rm_get_BGL(&bg)) != STATUS_OK) {
		fatal("init_bg: rm_get_BGL(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}

	if ((rc = rm_get_data(bg, RM_Msize, &bp_size)) != STATUS_OK) {
		fatal("init_bg: rm_get_data(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}
	verbose("BlueGene configured with %d x %d x %d base blocks",
		bp_size.X, bp_size.Y, bp_size.Z);
	DIM_SIZE[X]=bp_size.X;
	DIM_SIZE[Y]=bp_size.Y;
	DIM_SIZE[Z]=bp_size.Z;
#endif
	ba_init(NULL);

	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
extern void fini_bg(void)
{
	int rc;

	_set_bg_lists();
	
	if (bg_list) {
		list_destroy(bg_list);
		bg_list = NULL;
	}	
	if (bg_curr_block_list) {
		list_destroy(bg_curr_block_list);
		bg_curr_block_list = NULL;
	}	
	if (bg_found_block_list) {
		list_destroy(bg_found_block_list);
		bg_found_block_list = NULL;
	}
	if (bg_job_block_list) {
		list_destroy(bg_job_block_list);
		bg_job_block_list = NULL;
	}
	if (bg_booted_block_list) {
		list_destroy(bg_booted_block_list);
		bg_booted_block_list = NULL;
	}
	xfree(bluegene_blrts);
	xfree(bluegene_linux);
	xfree(bluegene_mloader);
	xfree(bluegene_ramdisk);
	xfree(bridge_api_file);
	xfree(bluegene_layout_mode);

#ifdef HAVE_BG_FILES
	if(bg)
		if ((rc = rm_free_BGL(bg)) != STATUS_OK)
			error("rm_free_BGL(): %s", bg_err_str(rc));
#endif	
	ba_fini();
}

extern void print_bg_record(bg_record_t* bg_record)
{
	char tmp_char[256];

	if (!bg_record) {
		error("print_bg_record, record given is null");
		return;
	}
#if _DEBUG
	info(" bg_record: ");
	if (bg_record->bg_block_id)
		info("\tbg_block_id: %s", bg_record->bg_block_id);
	info("\tnodes: %s", bg_record->nodes);
	info("\tsize: %d Midplanes %d Nodes %d cpus", 
	     bg_record->bp_count,
	     bg_record->node_cnt,
	     bg_record->cpus_per_bp * bg_record->bp_count);
	info("\tgeo: %dx%dx%d", bg_record->geo[X], bg_record->geo[Y], 
	     bg_record->geo[Z]);
	info("\tlifecycle: %s", convert_lifecycle(bg_record->block_lifecycle));
	info("\tconn_type: %s", convert_conn_type(bg_record->conn_type));
	info("\tnode_use: %s", convert_node_use(bg_record->node_use));
	if (bg_record->hostlist) {
		char buffer[BUFSIZE];
		hostlist_ranged_string(bg_record->hostlist, BUFSIZE, buffer);
		info("\thostlist %s", buffer);
	}
	if (bg_record->bitmap) {
		char bitstring[BITSIZE];
		bit_fmt(bitstring, BITSIZE, bg_record->bitmap);
		info("\tbitmap: %s", bitstring);
	}
#else
	format_node_name(bg_record, tmp_char);
	info("bg_block_id=%s nodes=%s", bg_record->bg_block_id, 
	     tmp_char);
#endif
}

extern void destroy_bg_record(void *object)
{
	bg_record_t* bg_record = (bg_record_t*) object;

	if (bg_record) {
		xfree(bg_record->bg_block_id);
		xfree(bg_record->nodes);
		xfree(bg_record->user_name);
		xfree(bg_record->target_name);
		if(bg_record->bg_block_list)
			list_destroy(bg_record->bg_block_list);
		if(bg_record->hostlist)
			hostlist_destroy(bg_record->hostlist);
		if(bg_record->bitmap)
			bit_free(bg_record->bitmap);
		
		xfree(bg_record);
	}
}

extern void copy_bg_record(bg_record_t *fir_record, bg_record_t *sec_record)
{
	xfree(sec_record->bg_block_id);
	sec_record->bg_block_id = xstrdup(fir_record->bg_block_id);
	xfree(sec_record->nodes);
	sec_record->nodes = xstrdup(fir_record->nodes);
	xfree(sec_record->user_name);
	sec_record->user_name = xstrdup(fir_record->user_name);
	xfree(sec_record->target_name);
	sec_record->target_name = xstrdup(fir_record->target_name);
	sec_record->full_block = fir_record->full_block;
	sec_record->user_uid = fir_record->user_uid;
	sec_record->block_lifecycle = fir_record->block_lifecycle;
	sec_record->state = fir_record->state;
	sec_record->conn_type = fir_record->conn_type;
	sec_record->node_use = fir_record->node_use;
	sec_record->bp_count = fir_record->bp_count;
	sec_record->switch_count = fir_record->switch_count;
	sec_record->boot_state = fir_record->boot_state;
	sec_record->boot_count = fir_record->boot_count;
	if(sec_record->bitmap)
		bit_free(sec_record->bitmap);
	if((sec_record->bitmap = bit_copy(fir_record->bitmap)) == NULL) {
		error("Unable to copy bitmap for", fir_record->nodes);
	}
	sec_record->job_running = fir_record->job_running;
	sec_record->cpus_per_bp = fir_record->cpus_per_bp;
	sec_record->node_cnt = fir_record->node_cnt;
	sec_record->quarter = fir_record->quarter;
	sec_record->segment = fir_record->segment;
}

extern bg_record_t *find_bg_record(char *bg_block_id)
{
	ListIterator itr;
	bg_record_t *bg_record = NULL;
		
	if(!bg_block_id)
		return NULL;
			
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = 
			(bg_record_t *) list_next(itr)) != NULL) {
			if(bg_record->bg_block_id)
				if (!strcmp(bg_record->bg_block_id, 
					    bg_block_id))
					break;
		}
		list_iterator_destroy(itr);
		if(bg_record)
			return bg_record;
		else
			return NULL;
	} else {
		error("find_bg_record: no bg_list");
		return NULL;
	}
	
}
/* All changes to the bg_list target_name must 
   be done before this function is called. 
*/
extern int update_block_user(bg_record_t *bg_record) 
{
#ifdef HAVE_BG_FILES
	int rc=0;
	struct passwd *pw_ent = NULL;
	
	if(!bg_record->target_name) {
		error("Must set target_name to run update_block_user.");
		return -1;
	}

	if((rc = remove_all_users(bg_record->bg_block_id, 
				  bg_record->target_name))
	   == REMOVE_USER_ERR) {
		error("Something happened removing "
		      "users from block %s", 
		      bg_record->bg_block_id);
		return -1;
	} else if (rc == REMOVE_USER_NONE) {
		if (strcmp(bg_record->target_name, 
			   slurmctld_conf.slurm_user_name)) {
			info("Adding user %s to Block %s",
			     bg_record->target_name, 
			     bg_record->bg_block_id);
		
			if ((rc = rm_add_part_user(bg_record->bg_block_id, 
						   bg_record->target_name)) 
			    != STATUS_OK) {
				error("rm_add_part_user(%s,%s): %s", 
				      bg_record->bg_block_id, 
				      bg_record->target_name,
				      bg_err_str(rc));
				return -1;
			} 
		}
	}
	
	if(strcmp(bg_record->target_name, bg_record->user_name)) {
		xfree(bg_record->user_name);
		bg_record->user_name = xstrdup(bg_record->target_name);
		if((pw_ent = getpwnam(bg_record->user_name)) == NULL) {
			error("getpwnam(%s): %m", bg_record->user_name);
			return -1;
		} else {
			bg_record->user_uid = pw_ent->pw_uid; 
		}		
		return 1;
	}
	
#endif
	return 0;
}

extern int format_node_name(bg_record_t *bg_record, char tmp_char[])
{
	if(bg_record->quarter != -1) {
		if(bg_record->segment != -1) {
			sprintf(tmp_char,"%s.%d.%d\0",
				bg_record->nodes,
				bg_record->quarter,
				bg_record->segment);
		} else {
			sprintf(tmp_char,"%s.%d\0",
				bg_record->nodes,
				bg_record->quarter);
		}
	} else {
		sprintf(tmp_char,"%s\0",bg_record->nodes);
	}
	return SLURM_SUCCESS;
}

extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b)
{
	if (!bit_super_set(rec_a->bitmap,
			   rec_b->bitmap)
	    && !bit_super_set(rec_b->bitmap,
			      rec_a->bitmap)) {
		return false;
	}
	
	if(rec_a->quarter != -1) {
		if(rec_b->quarter == -1)
			return true;
		else if(rec_a->quarter != rec_b->quarter)
			return false;
		if(rec_a->segment != -1) {
			if(rec_b->segment == -1)
				return true;
			else if(rec_a->segment 
				!= rec_b->segment)
				return false;
		}				
	}
	return true;
}

extern int remove_all_users(char *bg_block_id, char *user_name) 
{
	int returnc = REMOVE_USER_NONE;
#ifdef HAVE_BG_FILES
	char *user;
	rm_partition_t *block_ptr = NULL;
	int rc, i, user_count;

	if ((rc = rm_get_partition(bg_block_id,  &block_ptr)) != STATUS_OK) {
		error("rm_get_partition(%s): %s", 
		      bg_block_id, 
		      bg_err_str(rc));
		return REMOVE_USER_ERR;
	}	
	
	if((rc = rm_get_data(block_ptr, RM_PartitionUsersNum, &user_count)) 
	   != STATUS_OK) {
		error("rm_get_data(RM_PartitionUsersNum): %s", 
		      bg_err_str(rc));
		returnc = REMOVE_USER_ERR;
		user_count = 0;
	} else
		debug2("got %d users for %s",user_count, bg_block_id);
	for(i=0; i<user_count; i++) {
		if(i) {
			if ((rc = rm_get_data(block_ptr, 
					      RM_PartitionNextUser, 
					      &user)) 
			    != STATUS_OK) {
				error("rm_get_partition(%s): %s", 
				      bg_block_id, 
				      bg_err_str(rc));
				returnc = REMOVE_USER_ERR;
				break;
			}
		} else {
			if ((rc = rm_get_data(block_ptr, 
					      RM_PartitionFirstUser, 
					      &user)) 
			    != STATUS_OK) {
				error("rm_get_data(%s): %s", 
				      bg_block_id, 
				      bg_err_str(rc));
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
		
		info("Removing user %s from Block %s", 
		      user, 
		      bg_block_id);
		if ((rc = rm_remove_part_user(bg_block_id, user)) 
		    != STATUS_OK) {
			debug("user %s isn't on block %s",
			      user, 
			      bg_block_id);
		}
		free(user);
	}
	if ((rc = rm_free_partition(block_ptr)) != STATUS_OK) {
		error("rm_free_partition(): %s", bg_err_str(rc));
	}
#endif
	return returnc;
}

extern void set_block_user(bg_record_t *bg_record) 
{
	int rc = 0;
	debug("resetting the boot state flag and "
	      "counter for block %s.",
	      bg_record->bg_block_id);
	bg_record->boot_state = 0;
	bg_record->boot_count = 0;
	if((rc = update_block_user(bg_record)) == 1) {
		last_bg_update = time(NULL);
	} else if (rc == -1) {
		error("Unable to add user name to block %s. "
		      "Cancelling job.",
		      bg_record->bg_block_id);
		(void) slurm_fail_job(bg_record->job_running);
	}	
	xfree(bg_record->target_name);
	bg_record->target_name = 
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
	case (SELECT_SMALL): 
		return "SMALL"; 
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
extern void sort_bg_record_inc_size(List records){
	if (records == NULL)
		return;
	slurm_mutex_lock(&block_state_mutex);
	list_sort(records, (ListCmpF) _bg_record_cmpf_inc);
	slurm_mutex_unlock(&block_state_mutex);
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
	static time_t last_bg_test;
	int rc;

	last_mmcs_test = time(NULL) + MMCS_POLL_TIME;
	last_bg_test = time(NULL) + BG_POLL_TIME;
	while (!agent_fini) {
		time_t now = time(NULL);

		if (difftime(now, last_bg_test) >= BG_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			if(last_bg_update) {
				last_bg_test = now;
				if((rc = update_block_list()) == 1)
					last_bg_update = now;
				else if(rc == -1)
					error("Error "
					      "with update_block_list");
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
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern char *bg_err_str(status_t inx)
{
#ifdef HAVE_BG_FILES
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
 * create_static_blocks - create the static blocks that will be used
 * for scheduling, all partitions must be able to be created and booted
 * at once.  
 * IN - int overlayed, 1 if partitions are to be overlayed, 0 if they are
 * static.
 * RET - success of fitting all configurations
 */
extern int create_static_blocks(int overlayed)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	struct passwd *pw_ent = NULL;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	char *name = NULL;
#ifndef HAVE_BG_FILES
	static int block_inx = 0;
#else
	ListIterator itr_found;
	init_wires();
#endif
	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system();
		
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if(bg_record->bp_count>0 
			   && !bg_record->full_block
			   && bg_record->cpus_per_bp == procs_per_node) {
				if(overlayed)
					reset_ba_system();
				debug("adding %s starting at %d%d%d",
				      bg_record->nodes,
				      bg_record->start[X],
				      bg_record->start[Y],
				      bg_record->start[Z]);
				name = set_bg_block(NULL,
						    bg_record->start, 
						    bg_record->geo, 
						    bg_record->conn_type);
				if(!name) {
					error("I was unable to make the "
					      "requested block.");
					slurm_mutex_unlock(&block_state_mutex);
					return SLURM_ERROR;
				}
				xfree(name);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_blocks: no bg_list 1");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}

#ifdef HAVE_BG_FILES
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if(bg_found_block_list) {
				itr_found = list_iterator_create(
					bg_found_block_list);
				while ((found_record = (bg_record_t*) 
					list_next(itr_found)) != NULL) {
					/*printf("%s %d %s %d\n",*/
/* 					       bg_record->nodes, */
/* 					       bg_record->quarter, */
/* 					       found_record->nodes, */
/* 					       found_record->quarter); */
					
					if ((!strcmp(bg_record->nodes, 
						     found_record->nodes))
					    && (bg_record->quarter ==
						found_record->quarter)
					    && (bg_record->segment ==
						found_record->segment)) {
						/* don't reboot this one */
						break;	
					}
				}
				list_iterator_destroy(itr_found);
			} else {
				error("create_static_blocks: "
				      "no bg_found_block_list 1");
			}
			if(found_record == NULL) {
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
		error("create_static_blocks: no bg_list 2");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
#endif
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);
	create_full_system_block();

	sort_bg_record_inc_size(bg_list);

	
#ifdef HAVE_BG_FILES
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t*) list_next(itr)) != NULL) {
			if ((bg_record->geo[X] == DIM_SIZE[X])
			    &&  (bg_record->geo[Y] == DIM_SIZE[Y])
			    &&  (bg_record->geo[Z] == DIM_SIZE[Z])) {
				debug("full system block = %s.", 
				      bg_record->bg_block_id);
				bg_record->full_block = 1;
				full_system_block = bg_record;
				break;
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_overlayed_blocks: no bg_list 5");
	}
#else
	char tmp_char[256];
	if(bg_list) {
		slurm_mutex_lock(&block_state_mutex);
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t*) list_next(itr))) {
			if (bg_record->bg_block_id)
				continue;
			bg_record->bg_block_id = xmalloc(sizeof(char)*8);
			bg_record->job_running = -1;
			snprintf(bg_record->bg_block_id, 8, "RMP%d", 
				 block_inx++);
			format_node_name(bg_record, tmp_char);
			info("BG BlockID:%s Nodes:%s Conn:%s Mode:%s",
			     bg_record->bg_block_id, tmp_char,
			     convert_conn_type(bg_record->conn_type),
			     convert_node_use(bg_record->node_use));
			
			if ((bg_record->geo[X] == max_dim[X]+1)
			    &&  (bg_record->geo[Y] == max_dim[Y]+1)
			    &&  (bg_record->geo[Z] == max_dim[Z]+1)) {
				debug("full system block = %s.", 
				      bg_record->bg_block_id);
				bg_record->full_block = 1;
				full_system_block = bg_record;
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_blocks: no bg_list 4");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	
	slurm_mutex_unlock(&block_state_mutex);
	
#endif	/* not have HAVE_BG_FILES */
	

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
		error("create_static_blocks: no bg_list 5");
	}
 	exit(0);
#endif	/* _PRINT_BLOCKS_AND_EXIT */
	rc = SLURM_SUCCESS;
	//exit(0);
	return rc;
}



/*
 * create_dynamic_block - create a new block to be used for a new
 * job allocation.  This will be added to the booted and job bg_lists.
 * RET - success of fitting configuration in the running system.
 */
extern int create_dynamic_block()
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	struct passwd *pw_ent = NULL;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	char *name = NULL;
#ifndef HAVE_BG_FILES
	static int block_inx = 0;
#else
	ListIterator itr_found;
	init_wires();
#endif
	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system();
		
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			if(bg_record->bp_count>0 
			   && !bg_record->full_block
			   && bg_record->cpus_per_bp == procs_per_node) {
				debug("adding %s %d%d%d",
				      bg_record->nodes,
				      bg_record->start[X],
				      bg_record->start[Y],
				      bg_record->start[Z]);
				name = set_bg_block(NULL,
						    bg_record->start, 
						    bg_record->geo, 
						    bg_record->conn_type);
				if(!name) {
					error("I was unable to make the "
					      "requested block.");
					slurm_mutex_unlock(&block_state_mutex);
					return SLURM_ERROR;
				}
				xfree(name);
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_static_blocks: no bg_list 1");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}

#ifdef HAVE_BG_FILES
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if(bg_found_block_list) {
				itr_found = list_iterator_create(
					bg_found_block_list);
				while ((found_record = (bg_record_t*) 
					list_next(itr_found)) != NULL) {
					/*printf("%s %d %s %d\n",*/
/* 					       bg_record->nodes, */
/* 					       bg_record->quarter, */
/* 					       found_record->nodes, */
/* 					       found_record->quarter); */
					
					if ((!strcmp(bg_record->nodes, 
						     found_record->nodes))
					    && (bg_record->quarter ==
						found_record->quarter)
					    && (bg_record->segment ==
						found_record->segment)) {
						/* don't reboot this one */
						break;	
					}
				}
				list_iterator_destroy(itr_found);
			} else {
				error("create_static_blocks: "
				      "no bg_found_block_list 1");
			}
			if(found_record == NULL) {
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
		error("create_static_blocks: no bg_list 2");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
#endif
	
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);
	sort_bg_record_inc_size(bg_list);
	
	rc = SLURM_SUCCESS;
	//exit(0);
	return rc;
}

extern int create_full_system_block()
{
	int rc = SLURM_SUCCESS;
	
	ListIterator itr;
	struct passwd *pw_ent = NULL;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	char *name = NULL;
#ifndef HAVE_BG_FILES
	static int block_inx = 0;
#else
	ListIterator itr_found;
	init_wires();
#endif
		
	/* Here we are adding a block that in for the entire machine 
	   just in case it isn't in the bluegene.conf file.
	*/
	slurm_mutex_lock(&block_state_mutex);
	
	reset_ba_system();

	bg_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
	

#ifdef HAVE_BG_FILES
	bg_record->geo[X] = DIM_SIZE[X] - 1;
	bg_record->geo[Y] = DIM_SIZE[Y] - 1;
	bg_record->geo[Z] = DIM_SIZE[Z] - 1;
#else
	bg_record->geo[X] = max_dim[X];
	bg_record->geo[Y] = max_dim[Y];
	bg_record->geo[Z] = max_dim[Z];
#endif
	name = xmalloc(sizeof(char)*(10+strlen(slurmctld_conf.node_prefix)));
	if((bg_record->geo[X] == 0) && (bg_record->geo[Y] == 0)
	   && (bg_record->geo[Z] == 0))
		sprintf(name, "%s000\0", slurmctld_conf.node_prefix);
	else
		sprintf(name, "%s[000x%d%d%d]\0",
			slurmctld_conf.node_prefix,
			bg_record->geo[X], bg_record->geo[Y], 
			bg_record->geo[Z]);
	bg_record->nodes = xstrdup(name);
	xfree(name);
	bg_record->quarter = -1;
	bg_record->segment = -1;
	bg_record->full_block = 1;
	if(bg_found_block_list) {
		itr = list_iterator_create(bg_found_block_list);
		while ((found_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(bg_record->nodes, found_record->nodes)) {
				destroy_bg_record(bg_record);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_overlayed_blocks: no bg_found_block_list 2");
	}
	
	if(bg_list) {
		itr = list_iterator_create(bg_list);
		while ((found_record = (bg_record_t *) list_next(itr)) 
		       != NULL) {
			if (!strcmp(bg_record->nodes, found_record->nodes)) {
				destroy_bg_record(bg_record);
				list_iterator_destroy(itr);
				/* don't create total already defined */
				goto no_total;	
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_overlayed_blocks: no bg_list 3");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	full_system_block = bg_record;
	bg_record->bg_block_list = list_create(NULL);			
	bg_record->hostlist = hostlist_create(NULL);
	/* bg_record->boot_state = 0;		Implicit */
	_process_nodes(bg_record);
	list_append(bg_list, bg_record);
	
	bg_record->conn_type = SELECT_TORUS;
	bg_record->user_name = xstrdup(slurmctld_conf.slurm_user_name);
	bg_record->target_name = xstrdup(slurmctld_conf.slurm_user_name);
	if((pw_ent = getpwnam(bg_record->user_name)) == NULL) {
		error("getpwnam(%s): %m", bg_record->user_name);
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	} else {
		bg_record->user_uid = pw_ent->pw_uid;
	}
	
	name = set_bg_block(NULL,
			    bg_record->start, 
			    bg_record->geo, 
			    bg_record->conn_type);

	if(!name) {
		error("I was unable to make the "
		      "requested block.");
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	xfree(name);
	bg_record->node_use = SELECT_COPROCESSOR_MODE;
	bg_record->cpus_per_bp = procs_per_node;
	bg_record->node_cnt = bluegene_mp_node_cnt * bg_record->bp_count;
#ifdef HAVE_BG_FILES
	if((rc = configure_block(bg_record)) == SLURM_ERROR) {
		slurm_mutex_unlock(&block_state_mutex);
		return rc;
	}
	print_bg_record(bg_record);

#endif	/* HAVE_BG_FILES */
	
no_total:
	slurm_mutex_unlock(&block_state_mutex);
	return rc;
}

extern int bg_free_block(bg_record_t *bg_record)
{
#ifdef HAVE_BG_FILES
	int rc;
	if(!bg_record) {
		error("bg_free_block: there was no bg_record");
		return SLURM_ERROR;
	}
	while (1) {
		if (bg_record->state != -1
		    && bg_record->state != RM_PARTITION_FREE 
		    && bg_record->state != RM_PARTITION_DEALLOCATING) {
			debug("pm_destroy %s",bg_record->bg_block_id);
			if ((rc = pm_destroy_partition(
				     bg_record->bg_block_id)) 
			    != STATUS_OK) {
				if(rc == PARTITION_NOT_FOUND) {
					debug("block %s is not found");
					break;
				}
				error("pm_destroy_partition(%s): %s "
				      "State = %d",
				      bg_record->bg_block_id, 
				      bg_err_str(rc), bg_record->state);
			}
		}
		
		if ((bg_record->state == RM_PARTITION_FREE)
		    ||  (bg_record->state == RM_PARTITION_ERROR))
			break;
		sleep(3);
	}
#endif
	return SLURM_SUCCESS;
}

/* Free multiple blocks in parallel */
extern void *mult_free_block(void *args)
{
#ifdef HAVE_BG_FILES
	bg_record_t *bg_record = (bg_record_t*) args;

	debug("freeing the block %s.", bg_record->bg_block_id);
	bg_free_block(bg_record);	
	debug("done\n");
	slurm_mutex_lock(&freed_cnt_mutex);
	num_block_freed++;
	slurm_mutex_unlock(&freed_cnt_mutex);
#endif	
	return NULL;
}

/* destroy multiple blocks in parallel */
extern void *mult_destroy_block(void *args)
{
#ifdef HAVE_BG_FILES
	bg_record_t *bg_record = (bg_record_t*) args;
	int rc;

	debug("removing the jobs on block %s\n",
	      bg_record->bg_block_id);
	term_jobs_on_block(bg_record->bg_block_id);
	
	debug("destroying %s\n",
	      (char *)bg_record->bg_block_id);
	bg_free_block(bg_record);
	
	rc = rm_remove_partition(
		bg_record->bg_block_id);
	if (rc != STATUS_OK) {
		error("rm_remove_partition(%s): %s",
		      bg_record->bg_block_id,
		      bg_err_str(rc));
	} else
		debug("done\n");
	slurm_mutex_lock(&freed_cnt_mutex);
	num_block_freed++;
	slurm_mutex_unlock(&freed_cnt_mutex);

#endif	
	return NULL;
}

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * blocks are static/dynamic, torus/mesh, etc.
 */
extern int read_bg_conf(void)
{
	FILE *bg_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code = SLURM_SUCCESS;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;

	debug("Reading the bluegene.conf file");

	/* check if config file has changed */
	if (!bg_conf)
		bg_conf = _get_bg_conf();
	if (stat(bg_conf, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bg_conf);
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
	/* bg_conf defined in bg_node_alloc.h */
	bg_spec_file = fopen(bg_conf, "r");
	if (bg_spec_file == NULL)
		fatal("_read_bg_conf error opening file %s, %m",
		      bg_conf);
	
	_set_bg_lists();	
	
	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUFSIZE, bg_spec_file) != NULL) {
		line_num++;
		_strip_13_10(in_line);
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("_read_bg_config line %d, of input file %s "
			      "too long", line_num, bg_conf);
			fclose(bg_spec_file);
			xfree(bg_conf);
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
		/* block configuration parameters */
		error_code = _parse_bg_spec(in_line);
		
		/* report any leftover strings on input line */
		report_leftover(in_line, line_num);
	}
	fclose(bg_spec_file);
	xfree(bg_conf);
		
	if (!bluegene_blrts)
		fatal("BlrtsImage not configured in bluegene.conf");
	if (!bluegene_linux)
		fatal("LinuxImage not configured in bluegene.conf");
	if (!bluegene_mloader)
		fatal("MloaderImage not configured in bluegene.conf");
	if (!bluegene_ramdisk)
		fatal("RamDiskImage not configured in bluegene.conf");
	if (!bluegene_layout_mode) {
		info("Warning: LayoutMode was not specified in bluegene.conf "
		     "defaulting to STATIC partitioning");
		bluegene_layout_mode = xstrdup("STATIC");
	}
	if (!bridge_api_file)
		info("BridgeAPILogFile not configured in bluegene.conf");
	else
		_reopen_bridge_log();	
	if (!bluegene_numpsets)
		info("Warning: Numpsets not configured in bluegene.conf");
//#if 0	
	/* Check to see if the configs we have are correct */
	if (_validate_config_nodes() == SLURM_ERROR) { 
		_delete_old_blocks();
	}
//#endif
	/* looking for blocks only I created */
	if(!strcasecmp(bluegene_layout_mode,"STATIC")) {
		if (create_static_blocks(0) == SLURM_ERROR) {
			/* error in creating the static blocks, so
			 * blocks referenced by submitted jobs won't
			 * correspond to actual slurm blocks.
			 */
			fatal("Error, could not create the static blocks");
			return SLURM_ERROR;
		}
	} else if (!strcasecmp(bluegene_layout_mode,"OVERLAP")) {
		if (create_static_blocks(1) == SLURM_ERROR) {
			/* error in creating the static blocks, so
			 * blocks referenced by submitted jobs won't
			 * correspond to actual slurm blocks.
			 */
			fatal("Error, could not create the static blocks");
			return SLURM_ERROR;
		}
	} else if (!strcasecmp(bluegene_layout_mode,"DYNAMIC")) {
		init_wires();
		info("No blocks created until jobs are submitted");
	} else {
		fatal("I don't understand this LayoutMode = %s", 
		      bluegene_layout_mode);
	}
	debug("Blocks have finished being created.");
	blocks_are_created = 1;
	
	return error_code;
}

#ifdef HAVE_BG_FILES
static int _update_bg_record_state(List bg_destroy_list)
{
	rm_partition_state_flag_t block_state = PARTITION_ALL_FLAG;
	char *name = NULL;
	rm_partition_list_t *block_list = NULL;
	int j, rc, func_rc = SLURM_SUCCESS, num_blocks = 0;
	rm_partition_state_t state;
	rm_partition_t *block_ptr = NULL;
	ListIterator itr;
	bg_record_t* bg_record = NULL;	

	if(!bg_destroy_list) {
		return SLURM_SUCCESS;
	}
	
	if ((rc = rm_get_partitions_info(block_state, &block_list))
	    != STATUS_OK) {
		error("rm_get_partitions_info(): %s", bg_err_str(rc));
		return SLURM_ERROR; 
	}

	if ((rc = rm_get_data(block_list, RM_PartListSize, &num_blocks))
	    != STATUS_OK) {
		error("rm_get_data(RM_PartListSize): %s", bg_err_str(rc));
		func_rc = SLURM_ERROR;
		num_blocks = 0;
	}
			
	for (j=0; j<num_blocks; j++) {
		if (j) {
			if ((rc = rm_get_data(block_list, 
					      RM_PartListNextPart, 
					      &block_ptr)) 
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListNextPart): %s",
				      bg_err_str(rc));
				func_rc = SLURM_ERROR;
				break;
			}
		} else {
			if ((rc = rm_get_data(block_list, 
					      RM_PartListFirstPart, 
					      &block_ptr)) 
			    != STATUS_OK) {
				error("rm_get_data(RM_PartListFirstPart: %s",
				      bg_err_str(rc));
				func_rc = SLURM_ERROR;
				break;
			}
		}
		if ((rc = rm_get_data(block_ptr, 
				      RM_PartitionID, 
				      &name))
		    != STATUS_OK) {
			error("rm_get_data(RM_PartitionID): %s", 
			      bg_err_str(rc));
			func_rc = SLURM_ERROR;
			break;
		}
		if (!name) {
			error("RM_Partition is NULL");
			continue;
		}
		
		itr = list_iterator_create(bg_destroy_list);
		while ((bg_record = (bg_record_t*) list_next(itr))) {	
			if(!bg_record->bg_block_id) 
				continue;
			if(strcmp(bg_record->bg_block_id, name)) {
				continue;		
			}
		       
			slurm_mutex_lock(&block_state_mutex);
			if ((rc = rm_get_data(block_ptr, 
					      RM_PartitionState, 
					      &state))
			    != STATUS_OK) {
				error("rm_get_data(RM_PartitionState): %s",
				      bg_err_str(rc));
			} else if(bg_record->state != state) {
				debug("state of Block %s was %d "
				      "and now is %d",
				      name, bg_record->state, state);
				bg_record->state = state;
			}
			slurm_mutex_unlock(&block_state_mutex);
			break;
		}
		list_iterator_destroy(itr);
		free(name);
	}
	
	if ((rc = rm_free_partition_list(block_list)) != STATUS_OK) {
		error("rm_free_partition_list(): %s", bg_err_str(rc));
	}
	return func_rc;
}
#endif /* HAVE_BG_FILES */

#ifdef HAVE_BG
static int _addto_node_list(bg_record_t *bg_record, int *start, int *end)
{
	int node_count=0;
	int x,y,z;
	char node_name_tmp[255];
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
				sprintf(node_name_tmp, "%s%d%d%d\0", 
					slurmctld_conf.node_prefix,
					x, y, z);		
				list_append(bg_record->bg_block_list, 
					    &ba_system_ptr->grid[x][y][z]);
				node_count++;
			}
		}
	}
	return node_count;
}
#endif //HAVE_BG

static void _set_bg_lists()
{
	bg_record_t *bg_record = NULL;
	
	slurm_mutex_lock(&block_state_mutex);
	if (bg_found_block_list) 
		list_destroy(bg_found_block_list);
	bg_found_block_list = list_create(NULL);
	
	if (bg_curr_block_list)
		list_destroy(bg_curr_block_list);
	
	bg_curr_block_list = list_create(destroy_bg_record);
	
/* empty the old list before reading new data */
	if (bg_list) 
		list_destroy(bg_list);
	bg_list = list_create(destroy_bg_record);
	slurm_mutex_unlock(&block_state_mutex);
		
}

/*
 * Match slurm configuration information with current BG block 
 * configuration. Return SLURM_SUCCESS if they match, else an error 
 * code. Writes bg_block_id into bg_list records.
 */

static int _validate_config_nodes(void)
{
	int rc = SLURM_ERROR;
#ifdef HAVE_BG_FILES
	bg_record_t* bg_record = NULL;	
	bg_record_t* init_bg_record = NULL;
	ListIterator itr_conf;
	ListIterator itr_curr;
	rm_partition_mode_t node_use;
	char tmp_char[256];
	/* read current bg block info into bg_curr_block_list */
	if (read_bg_blocks() == SLURM_ERROR)
		return SLURM_ERROR;
	
	if(!bg_recover) 
		return SLURM_ERROR;
	
	if(bg_list) {
		itr_conf = list_iterator_create(bg_list);
		while ((bg_record = (bg_record_t*) list_next(itr_conf))) {
			/* translate hostlist to ranged 
			   string for consistent format
			   search here 
			*/
			node_use = SELECT_COPROCESSOR_MODE; 
		
			if(bg_curr_block_list) {
				itr_curr = list_iterator_create(
					bg_curr_block_list);	
				while ((init_bg_record = (bg_record_t*) 
					list_next(itr_curr)) 
				       != NULL) {
					if (strcasecmp(bg_record->nodes, 
						       init_bg_record->nodes))
						continue; /* wrong nodes */
					if (bg_record->conn_type 
					    != init_bg_record->conn_type)
						continue; /* wrong conn_type */
					if(bg_record->quarter !=
					    init_bg_record->quarter)
						continue; /* wrong quart */
					if(bg_record->segment !=
					    init_bg_record->segment)
						continue; /* wrong segment */
					copy_bg_record(init_bg_record, 
						       bg_record);
					break;
				}
				list_iterator_destroy(itr_curr);
			} else {
				error("_validate_config_nodes: "
				      "no bg_curr_block_list");
			}
			if (!bg_record->bg_block_id) {
				_format_node_name(bg_record, tmp_char);
				
				info("Block found in bluegene.conf to be "
				     "created: Nodes:%s", 
				     tmp_char);
				rc = SLURM_ERROR;
			} else {
				list_append(bg_found_block_list, bg_record);
				_format_node_name(bg_record, tmp_char);
				
				info("Found existing BG BlockID:%s "
				     "Nodes:%s Conn:%s",
				     bg_record->bg_block_id, 
				     tmp_char,
				     convert_conn_type(bg_record->conn_type));
			}
		}		
		list_iterator_destroy(itr_conf);
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(
				bg_curr_block_list);
			while ((init_bg_record = (bg_record_t*) 
				list_next(itr_curr)) 
			       != NULL) {
				_process_nodes(init_bg_record);
				debug3("%s %d %d%d%d %d%d%d",
				       init_bg_record->bg_block_id, 
				       init_bg_record->bp_count, 
				       init_bg_record->geo[X],
				       init_bg_record->geo[Y],
				       init_bg_record->geo[Z],
				       DIM_SIZE[X],
				       DIM_SIZE[Y],
				       DIM_SIZE[Z]);
				if ((init_bg_record->geo[X] == DIM_SIZE[X])
				    && (init_bg_record->geo[Y] == DIM_SIZE[Y])
				    && (init_bg_record->geo[Z] == DIM_SIZE[Z]))
				{
					bg_record = (bg_record_t*) 
						xmalloc(sizeof(bg_record_t));
					list_append(bg_list, bg_record);
					list_append(bg_found_block_list, 
						    bg_record);
					copy_bg_record(init_bg_record, 
						       bg_record);
					bg_record->full_block = 1;
					full_system_block = bg_record;
					debug("full system %s",
					      bg_record->bg_block_id);
					_format_node_name(bg_record, tmp_char);
					info("Found existing BG "
					     "BlockID:%s "
					     "Nodes:%s Conn:%s",
					     bg_record->bg_block_id, 
					     tmp_char,
					     convert_conn_type(
						     bg_record->conn_type));
					break;
				}
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_validate_config_nodes: "
			      "no bg_curr_block_list 2");
		}

		if(list_count(bg_list) == list_count(bg_curr_block_list))
			rc = SLURM_SUCCESS;
	} else {
		error("_validate_config_nodes: no bg_list");
		rc = SLURM_ERROR;
	}
#endif

	return rc;
}

/* 
 * Comparator used for sorting blocks smallest to largest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bg_record_cmpf_inc(bg_record_t* rec_a, bg_record_t* rec_b)
{
	int size_a = rec_a->node_cnt;
	int size_b = rec_b->node_cnt;
	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;
	else
		return 0;
}

static int _delete_old_blocks(void)
{
#ifdef HAVE_BG_FILES
	ListIterator itr_curr, itr_found;
	bg_record_t *found_record = NULL, *init_record = NULL;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int retries;
	List bg_destroy_list = list_create(NULL);

	num_block_to_free = 0;
	num_block_freed = 0;
				
	if(!bg_recover) {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = (bg_record_t*) 
				list_next(itr_curr))) {
				slurm_attr_init(&attr_agent);
				if (pthread_attr_setdetachstate(
					    &attr_agent, 
					    PTHREAD_CREATE_JOINABLE))
					error("pthread_attr_setdetach"
						      "state error %m");

				list_push(bg_destroy_list, init_record);
				retries = 0;
				while (pthread_create(&thread_agent, 
						      &attr_agent, 
						      mult_destroy_block, 
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
				num_block_to_free++;
			}
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 1");
			return SLURM_ERROR;
		}
	} else {
		if(bg_curr_block_list) {
			itr_curr = list_iterator_create(bg_curr_block_list);
			while ((init_record = (bg_record_t*) 
				list_next(itr_curr))) {
				if(bg_found_block_list) {
					itr_found = list_iterator_create(
						bg_found_block_list);
					while ((found_record = (bg_record_t*) 
						list_next(itr_found)) 
					       != NULL) {
						if (!strcmp(init_record->
							    bg_block_id, 
							    found_record->
							    bg_block_id)) {
							/* don't delete 
							   this one 
							*/
							break;	
						}
					}
					list_iterator_destroy(itr_found);
				} else {
					error("_delete_old_blocks: "
					      "no bg_found_block_list");
					return SLURM_ERROR;
				}
				if(found_record == NULL) {
					slurm_attr_init(&attr_agent);
					if (pthread_attr_setdetachstate(
						    &attr_agent, 
						    PTHREAD_CREATE_JOINABLE))
						error("pthread_attr_setdetach"
						      "state error %m");
				
					list_push(bg_destroy_list, 
						  init_record);
					retries = 0;
					while (pthread_create(
						       &thread_agent, 
						       &attr_agent, 
						       mult_destroy_block, 
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
					num_block_to_free++;
				}
			}		
			list_iterator_destroy(itr_curr);
		} else {
			error("_delete_old_blocks: "
			      "no bg_curr_block_list 2");
			return SLURM_ERROR;
		}
	}
	retries=30;
	while(num_block_to_free != num_block_freed) {
		_update_bg_record_state(bg_destroy_list);
		if(retries==30) {
			info("Waiting for old blocks to be "
			     "freed.  Have %d of %d",
			     num_block_freed, 
			     num_block_to_free);
			retries=0;
		}
		retries++;
		sleep(1);
	}
	list_destroy(bg_destroy_list);
	
#endif	
	return SLURM_SUCCESS;
}

static char *_get_bg_conf(void)
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
 * _parse_bg_spec - parse the block specification, build table and 
 *	set values
 * IN/OUT in_line - line from the configuration file, parsed keywords 
 *	and values replaced by blanks
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: block_list - global block list pointer
 *	default_block - default parameters for a block
 */
static int _parse_bg_spec(char *in_line)
{
	int error_code = SLURM_SUCCESS;
	char *nodes = NULL, *conn_type = NULL;
	char *blrts_image = NULL,   *linux_image = NULL;
	char *mloader_image = NULL, *ramdisk_image = NULL;
	char *api_file = NULL, *layout = NULL;
	int pset_num=-1, api_verb=-1, num32=0, num128=0;
	bg_record_t *bg_record = NULL;
	bg_record_t *small_bg_record = NULL;
	ba_node_t *ba_node = NULL;
	struct passwd *pw_ent = NULL;
	ListIterator itr;
	int i=0;
	int small_size = 0;
	int small_count = 0;
	int quarter = 0;
	int node_cnt = 0;
	int mp_node_cnt = 0;
	int nc_node_cnt = 0;

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
				  "Num32=", 'd', &num32,
				  "Num128=", 'd', &num128,
				  "MidplaneNodeCnt=", 'd', &mp_node_cnt,
				  "NodeCardNodeCnt=", 'd', &nc_node_cnt,
				  "LayoutMode=", 's', &layout,
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
	if (layout) {
		xfree(bluegene_layout_mode);
		bluegene_layout_mode = layout;
		layout = NULL;
	}

	if (pset_num > 0) {
		bluegene_numpsets = pset_num;
	}
	if (api_verb >= 0) {
		bridge_api_verb = api_verb;
	}
	if (mp_node_cnt > 0) {
		bluegene_mp_node_cnt = mp_node_cnt;
	}
	if (nc_node_cnt > 0) {
		bluegene_nc_node_cnt = nc_node_cnt;
	}

	/* Process node information */
	if (!nodes)
		return SLURM_SUCCESS;	/* not block line. */
	
	if (!bluegene_mp_node_cnt)
		fatal("MidplaneNodeCnt not configured in bluegene.conf "
		      "make sure it is set before any Nodes= line");

	if (!bluegene_nc_node_cnt)
		fatal("NodeCardNodeCnt not configured in bluegene.conf "
		      "make sure it is set before any Nodes= line");

	bg_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
	
	bg_record->user_name = 
		xstrdup(slurmctld_conf.slurm_user_name);
	if((pw_ent = getpwnam(bg_record->user_name)) == NULL) {
		error("getpwnam(%s): %m", bg_record->user_name);
	} else {
		bg_record->user_uid = pw_ent->pw_uid;
	}
	bg_record->bg_block_list = list_create(NULL);		
	bg_record->hostlist = hostlist_create(NULL);
	/* bg_record->boot_state = 0; 	Implicit */
	/* bg_record->state = 0;	Implicit */
	api_verb = strlen(nodes);
	i=0;
	while((nodes[i] != '[' && (nodes[i] > 57 || nodes[i] < 48)) 
	      && (i<api_verb)) {		
		i++;
	}
	if(i<api_verb) {
		api_verb -= i;
		bg_record->nodes = xmalloc(sizeof(char)*
					   (api_verb
					    +strlen(slurmctld_conf.node_prefix)
					    +1));
		
		sprintf(bg_record->nodes, "%s%s\0", 
			slurmctld_conf.node_prefix, nodes+i);
	} else 
		fatal("Nodes=%s is in a weird format", nodes); 
	xfree(nodes); 
	
	_process_nodes(bg_record);
	if (!conn_type || !strcasecmp(conn_type,"TORUS"))
		bg_record->conn_type = SELECT_TORUS;
	else if(!strcasecmp(conn_type,"MESH"))
		bg_record->conn_type = SELECT_MESH;
	else
		bg_record->conn_type = SELECT_SMALL;
	xfree(conn_type);
	
	bg_record->node_use = SELECT_COPROCESSOR_MODE;
	bg_record->cpus_per_bp = procs_per_node;
	bg_record->node_cnt = bluegene_mp_node_cnt * bg_record->bp_count;
	bg_record->quarter = -1;
	bg_record->segment = -1;
	
	if(bg_record->conn_type != SELECT_SMALL)
		list_append(bg_list, bg_record);
	else {
		if(num32==0 && num128==0) {
			info("No specs given for this small block, "
			     "I am spliting this block into 4 quarters");
			num128=4;
		}
		if(((num32*32) + (num128*128)) != bluegene_mp_node_cnt)
			fatal("There is an error in your bluegene.conf file.\n"
			      "I am unable to request %d nodes in one "
			      "midplane with %d nodes.", 
			      ((num32*32) + (num128*128)), 
			      bluegene_mp_node_cnt);
		small_count = num32+num128; 
		
		/* Automatically create 4-way split if 
		 * conn_type == SELECT_SMALL in bluegene.conf
		 * Here we go through each node listed and do the same thing
		 * for each node.
		 */
		itr = list_iterator_create(bg_record->bg_block_list);
		while ((ba_node = list_next(itr)) != NULL) {
			/* break midplane up into 16 parts */
			small_size = 16;
			node_cnt = 0;
			quarter = 0;
			for(i=0; i<small_count; i++) {
				if(i == num32) {
					/* break midplane up into 4 parts */
					small_size = 4;
				}
				small_bg_record = (bg_record_t*) 
					xmalloc(sizeof(bg_record_t));
				list_append(bg_list, small_bg_record);
				
				small_bg_record->user_name = 
					xstrdup(bg_record->user_name);
				small_bg_record->user_uid = 
					bg_record->user_uid;
				small_bg_record->bg_block_list = 
					list_create(NULL);
				small_bg_record->hostlist = 
					hostlist_create(NULL);
				small_bg_record->nodes = 
					xstrdup(bg_record->nodes);

				_process_nodes(small_bg_record);
				
				small_bg_record->conn_type = 
					SELECT_SMALL;
				
				small_bg_record->node_use = 
					SELECT_COPROCESSOR_MODE;
				
				small_bg_record->cpus_per_bp = 
					procs_per_node/small_size;
				small_bg_record->node_cnt = 
					bluegene_mp_node_cnt/small_size;
				small_bg_record->quarter = quarter; 
				
				node_cnt += small_bg_record->node_cnt;
				if(node_cnt == 128) {
					node_cnt = 0;
					quarter++;
				}
				
				if(small_bg_record->node_cnt == 128)
					small_bg_record->segment = -1;
				else
					small_bg_record->segment = i%4; 
				
			}
		}
		list_iterator_destroy(itr);
		destroy_bg_record(bg_record);
	} 

	return SLURM_SUCCESS;
}

static void _process_nodes(bg_record_t *bg_record)
{
#ifdef HAVE_BG
	int j=0, number;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];
	ListIterator itr;
	ba_node_t* ba_node = NULL;
	
	bg_record->bp_count = 0;
				
	while (bg_record->nodes[j] != '\0') {
		if ((bg_record->nodes[j] == '['
		     || bg_record->nodes[j] == ',')
		    && (bg_record->nodes[j+8] == ']' 
			|| bg_record->nodes[j+8] == ',')
		    && (bg_record->nodes[j+4] == 'x'
			|| bg_record->nodes[j+4] == '-')) {
			j++;
			number = atoi(bg_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(bg_record->nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 3;
			if(!bg_record->bp_count) {
				bg_record->start[X] = start[X];
				bg_record->start[Y] = start[Y];
				bg_record->start[Z] = start[Z];
				debug2("start is %d%d%d",
				      bg_record->start[X],
				      bg_record->start[Y],
				      bg_record->start[Z]);
			}
			bg_record->bp_count += _addto_node_list(bg_record, 
								 start, 
								 end);
			if(bg_record->nodes[j] != ',')
				break;
			j--;
		} else if((bg_record->nodes[j] < 58 
			   && bg_record->nodes[j] > 47)) {
					
			number = atoi(bg_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			if(!bg_record->bp_count) {
				bg_record->start[X] = start[X];
				bg_record->start[Y] = start[Y];
				bg_record->start[Z] = start[Z];
				debug2("start is %d%d%d",
				      bg_record->start[X],
				      bg_record->start[Y],
				      bg_record->start[Z]);
			}
			bg_record->bp_count += _addto_node_list(bg_record, 
								 start, 
								 start);
			if(bg_record->nodes[j] != ',')
				break;
		}
		j++;
	}
	j=0;
	bg_record->geo[X] = 0;
	bg_record->geo[Y] = 0;
	bg_record->geo[Z] = 0;
	end[X] = -1;
	end[Y] = -1;
	end[Z] = -1;
	
	itr = list_iterator_create(bg_record->bg_block_list);
	while ((ba_node = list_next(itr)) != NULL) {
		if(ba_node->coord[X]>end[X]) {
			bg_record->geo[X]++;
			end[X] = ba_node->coord[X];
		}
		if(ba_node->coord[Y]>end[Y]) {
			bg_record->geo[Y]++;
			end[Y] = ba_node->coord[Y];
		}
		if(ba_node->coord[Z]>end[Z]) {
			bg_record->geo[Z]++;
			end[Z] = ba_node->coord[Z];
		}
	}
	list_iterator_destroy(itr);
	debug3("geo = %d%d%d\n",
	       bg_record->geo[X],
	       bg_record->geo[Y],
	       bg_record->geo[Z]);
	
#ifndef HAVE_BG_FILES
	max_dim[X] = MAX(max_dim[X], end[X]);
	max_dim[Y] = MAX(max_dim[Y], end[Y]);
	max_dim[Z] = MAX(max_dim[Z], end[Z]);
#endif
   
	if (node_name2bitmap(bg_record->nodes, 
			     false, 
			     &bg_record->bitmap)) {
		fatal("Unable to convert nodes %s to bitmap", 
		      bg_record->nodes);
	}
#endif
	bg_record->node_cnt = bluegene_mp_node_cnt * bg_record->bp_count;
	
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

#ifdef HAVE_BG_FILES
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

