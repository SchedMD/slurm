/*****************************************************************************\
 *  bluegene.c - blue gene node configuration processing module. 
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

#define BUFSIZE 4096
#define BITSIZE 128

/*
 * The BGL bridge APIs are *not* thread safe. This means we can not 
 * presently test for down nodes and switches in a separate pthread. 
 * We could do so from within bgl_job_run.c:_part_agent(), but these 
 * APIs are so slow (10-15 seconds for rm_get_BGL) that we do not 
 * want to slow down job launch or termination by that much. When
 * the APIs are thread safe, revert to the code marked by
 * "#ifdef BGL_THREAD_SAFE".        - Jette 2/17/2005
 */
#define MMCS_POLL_TIME 120	/* poll MMCS for down switches and nodes 
				 * every 120 secs */

#define _DEBUG 0

char* bgl_conf = BLUEGENE_CONFIG_FILE;

/* Global variables */
rm_BGL_t *bgl;
List bgl_list = NULL;			/* list of bgl_record entries */
List bgl_curr_part_list = NULL;  	/* current bgl partitions */
List bgl_found_part_list = NULL;  	/* found bgl partitions */
char *bluegene_blrts = NULL, *bluegene_linux = NULL, *bluegene_mloader = NULL;
char *bluegene_ramdisk = NULL;
char *change_numpsets = NULL;
int numpsets;
bool agent_fini = false;

/* some local functions */
#ifdef HAVE_BGL
static int  _addto_node_list(bgl_record_t *bgl_record, int *start, int *end);
#endif
static void _set_bgl_lists();
static int  _validate_config_nodes(void);
static int  _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b);
static int  _parse_bgl_spec(char *in_line);
static void _process_nodes(bgl_record_t *bgl_record);

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
	/* pm_partition_id_t part_id; */
/* 	bgl_record_t *record; */
/* 	ListIterator itr; */
	
/* 	itr = list_iterator_create(bgl_list); */
/* 	while ((record = (bgl_record_t*) list_next(itr))) { */
/* 		part_id=record->bgl_part_id; */
/* 		debug("removing the jobs on partition %s\n", */
/* 		      (char *)part_id); */
/* 		term_jobs_on_part(part_id); */
		
/* 		debug("destroying %s\n",(char *)part_id); */
/* 		bgl_free_partition(part_id); */
		
/* 		rm_remove_partition(part_id); */
/* 		debug("done\n"); */
/* 	} */
	
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

#ifdef HAVE_BGL_FILES
	if(bgl)
		rm_free_BGL(bgl);
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
		if(bgl_record->nodes) {
			xfree(bgl_record->nodes);
			xfree(bgl_record->owner_name);
			if (bgl_record->bgl_part_list)
				list_destroy(bgl_record->bgl_part_list);
			if (bgl_record->hostlist)
				hostlist_destroy(bgl_record->hostlist);
			if (bgl_record->bitmap)
				bit_free(bgl_record->bitmap);
			xfree(bgl_record->bgl_part_id);
		
			xfree(bgl_record);
		}
	}
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

	last_mmcs_test = time(NULL) + MMCS_POLL_TIME;
	while (!agent_fini) {
#ifdef BGL_THREAD_SAFE
		time_t now = time(NULL);

		if (difftime(now, last_mmcs_test) >= MMCS_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			last_mmcs_test = now;
			test_mmcs_failures();	/* can run for a while */
		}
#endif
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

	ListIterator itr, itr_found;
	bgl_record_t *bgl_record, *found_record;
	
	reset_pa_system();
	
	itr = list_iterator_create(bgl_list);
	while ((bgl_record = (bgl_record_t *) list_next(itr)) != NULL) {
			
		if(bgl_record->bp_count>0 && bgl_record->node_use==SELECT_COPROCESSOR_MODE)
			set_bgl_part(bgl_record->bgl_part_list, 
				     bgl_record->bp_count, 
				     bgl_record->conn_type);
			
	}
	list_iterator_destroy(itr);
	

	itr = list_iterator_create(bgl_list);
	while ((bgl_record = (bgl_record_t *) list_next(itr)) != NULL) {
		itr_found = list_iterator_create(bgl_found_part_list);
		while ((found_record = (bgl_record_t*) list_next(itr_found)) != NULL) {
			if (!strcmp(bgl_record->nodes, found_record->nodes)) {
				break;	/* don't reboot this one */
			}
		}
		list_iterator_destroy(itr_found);
		if(found_record == NULL) {
#ifdef HAVE_BGL_FILES
			//bgl_record->node_use = SELECT_VIRTUAL_NODE_MODE;
			configure_partition(bgl_record);
			print_bgl_record(bgl_record);
			/* Here we are adding some partitions manually because of the way
			   We want to run the system.  This will need to be changed for 
			   the real system because this is not going to work in the real
			   deal.
			*/
			bgl_record = (bgl_record_t *) list_next(itr);
			if(bgl_record == NULL)
				break;
			configure_partition(bgl_record);
			print_bgl_record(bgl_record);
			
#endif
		}
	}
	list_iterator_destroy(itr);

	/* Here we are adding some partitions manually because of the way
	   We want to run the system.  This will need to be changed for 
	   the real system because this is not going to work in the real
	   deal.
	*/
	
#ifdef HAVE_BGL_FILES
	reset_pa_system();

	bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
	
	bgl_record->nodes = xmalloc(sizeof(char)*13);
	memset(bgl_record->nodes, 0, 13);
	if(DIM_SIZE[X]==1 && DIM_SIZE[Y]==1 && DIM_SIZE[Z]==1)
		sprintf(bgl_record->nodes, "bgl000");
       	else
		sprintf(bgl_record->nodes, "bgl[000x%d%d%d]", DIM_SIZE[X]-1,  DIM_SIZE[Y]-1, DIM_SIZE[Z]-1);
       	itr = list_iterator_create(bgl_list);
	while ((found_record = (bgl_record_t *) list_next(itr)) != NULL) {
		if (!strcmp(bgl_record->nodes, found_record->nodes)) {
			goto no_total;	/* don't reboot this one */
		}
	}
	list_iterator_destroy(itr);
	bgl_record->bgl_part_list = list_create(NULL);			
	bgl_record->hostlist = hostlist_create(NULL);
	_process_nodes(bgl_record);
	list_push(bgl_list, bgl_record);
	
	bgl_record->conn_type = SELECT_TORUS;
	
	set_bgl_part(bgl_record->bgl_part_list, 
		     bgl_record->bp_count, 
		     bgl_record->conn_type);
	bgl_record->node_use = SELECT_COPROCESSOR_MODE;
	configure_partition(bgl_record);
	print_bgl_record(bgl_record);

	found_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
	list_push(bgl_list, found_record);
			
	found_record->bgl_part_list = bgl_record->bgl_part_list;			
	found_record->hostlist = bgl_record->hostlist;
	found_record->nodes = xstrdup(bgl_record->nodes);
	
	found_record->bp_count = bgl_record->bp_count;
	found_record->switch_count = bgl_record->switch_count;
	found_record->geo[X] = bgl_record->geo[X];
	found_record->geo[Y] = bgl_record->geo[Y];
	found_record->geo[Z] = bgl_record->geo[Z];
			
	found_record->conn_type = bgl_record->conn_type;
	found_record->bitmap = bgl_record->bitmap;
	found_record->node_use = SELECT_VIRTUAL_NODE_MODE;
	configure_partition(found_record);
	print_bgl_record(found_record);

no_total:
	
	rc = SLURM_SUCCESS;
/* 	itr = list_iterator_create(bgl_list); */
	/* printf("\n\n"); */
/* 	while ((found_record = (bgl_record_t *) list_next(itr)) != NULL) { */
	       
/* 		print_bgl_record(found_record); */
/* 	} */
/* 	list_iterator_destroy(itr); */
/* 	exit(0); */
        /*********************************************************/
#endif

	return rc;
}

extern int bgl_free_partition(pm_partition_id_t part_id)
{
#ifdef HAVE_BGL_FILES
	rm_partition_state_t state;
	rm_partition_t *my_part;
	int rc;

        if ((rc = rm_get_partition(part_id, &my_part))
	    != STATUS_OK) {
		error("couldn't get the partition in bgl_free_partition");
	} else {
		rm_get_data(my_part, RM_PartitionState, &state);
		if(state != RM_PARTITION_FREE)
			pm_destroy_partition(part_id);
			
		rm_get_data(my_part, RM_PartitionState, &state);
		while ((state != RM_PARTITION_FREE) 
		       && (state != RM_PARTITION_ERROR)){
			debug(".");
			rc=rm_free_partition(my_part);
			if(rc!=STATUS_OK){
				error("Error freeing partition\n");
				return(-1);
			}
			sleep(3);
			rc=rm_get_partition(part_id,&my_part);
			if(rc!=STATUS_OK) {
				error("Error in GetPartition\n");
				return(-1);
			}
			rm_get_data(my_part, RM_PartitionState,
				    &state);
		}
		//Free memory allocated to mypart
		rc=rm_free_partition(my_part);
		if(rc!=STATUS_OK){
			error("Error freeing partition\n");
			return(-1);
		}
		
	}
#endif
	return SLURM_SUCCESS;
}

#ifdef HAVE_BGL
static int _addto_node_list(bgl_record_t *bgl_record, int *start, int *end)
{
	int node_count=0;
	int x,y,z;
	char node_name_tmp[7];
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
				hostlist_push(bgl_record->hostlist, 
					node_name_tmp);
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
	bgl_record_t *bgl_record;
	
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
	bgl_record_t* record;	/* records from configuration files */
	bgl_record_t* init_record;	/* records from actual BGL config */
	ListIterator itr_conf, itr_curr;
	rm_partition_mode_t node_use;
	
	/* read current bgl partition info into bgl_curr_part_list */
	if (read_bgl_partitions() == SLURM_ERROR)
		return SLURM_ERROR;

	itr_conf = list_iterator_create(bgl_list);
	while ((record = (bgl_record_t*) list_next(itr_conf))) {
		/* translate hostlist to ranged string for consistent format */
        	/* search here */
		node_use = SELECT_COPROCESSOR_MODE; 

		itr_curr = list_iterator_create(bgl_curr_part_list);
		while ((init_record = (bgl_record_t*) list_next(itr_curr)) 
		       != NULL) {
			if (strcasecmp(record->nodes, init_record->nodes)) {
				continue;	/* wrong nodes */
			}
			if (record->conn_type != init_record->conn_type)
				continue;		/* must reconfig this part */
			if(record->node_use != init_record->node_use)
				continue;
			record->bgl_part_id = xstrdup(init_record->bgl_part_id);
			break;
		}
		list_iterator_destroy(itr_curr);
		if (!record->bgl_part_id) {
			info("BGL PartitionID:NONE Nodes:%s", record->nodes);
			rc = SLURM_SUCCESS;
		} else {
			list_push(bgl_found_part_list, record);
			info("BGL PartitionID:%s Nodes:%s Conn:%s Mode:%s",
			     init_record->bgl_part_id, record->nodes,
			     convert_conn_type(record->conn_type),
			     convert_node_use(record->node_use));
		}
	}
	
	list_iterator_destroy(itr_conf);
	if(list_count(bgl_list) != list_count(bgl_curr_part_list))
		rc = SLURM_SUCCESS;
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
	int rc;
	ListIterator itr_curr, itr_found;
	bgl_record_t *found_record, *init_record;
        pm_partition_id_t part_id;
	rm_partition_t *my_part;
	int part_number, lowest_part=300;
	char part_name[7];
			
	/******************************************************************/
	itr_curr = list_iterator_create(bgl_curr_part_list);
	while ((init_record = (bgl_record_t*) list_next(itr_curr))) {
		part_id=init_record->bgl_part_id;
		part_number = atoi(init_record->bgl_part_id+3);
		if(part_number<lowest_part)
			lowest_part = part_number;
	}
	list_iterator_destroy(itr_curr);
//	if(lowest_part != 101) {
	/* 	rm_get_partitions(RM_PARTITION_FREE, &part_list); */
/* 		rm_get_data(part_list, RM_PartListSize, &size); */
/* 		printf("This is the size %d\n",size); */
/* 		for(i=0;i<size;i++) { */
/* 			if(!i) */
/* 				rm_get_data(part_list, RM_PartListFirstPart, &my_part); */
/* 			else */
/* 				rm_get_data(part_list, RM_PartListNextPart, &my_part); */
/* 			rm_get_data(my_part, RM_PartListNextPart, &part_id); */
/* 			printf("this is the name %s\n",part_id); */
/* 			if(!strncasecmp("RMP",part_id,3)) { */
/* 				init_record = xmalloc(sizeof(bgl_record_t)); */
/* 				list_push(bgl_curr_part_list, init_record); */
/* 				init_record->bgl_part_id = xstrdup(part_id); */
/* 			} */
/* 			xfree(part_id); */
/* 			rm_free_partition(my_part);			 */
/* 		} */
/* 		exit(0); */

	/* Here is where we clear all the partitions that exist. This will need to 
	   be taken out when we get better code from IBM.
	*/
	for(part_number=101; part_number<lowest_part; part_number++) {
		memset(part_name,0,7);
		sprintf(part_name, "RMP%d", part_number);
		//debug("Checking if Partition %s is free",part_name);
		if ((rc = rm_get_partition(part_name, &my_part))
		    != STATUS_OK) {
			debug("Above error is ok. "
			      "Partition %s doesn't exist.",
			      part_name);
			continue;
		}
		debug("removing the jobs on partition %s\n",
		      (char *)part_name);
		term_jobs_on_part(part_name);
		
		debug("destroying %s\n",(char *)part_name);
		rc = bgl_free_partition(part_name);
		
		rm_remove_partition(part_name);
		debug("done\n");
		
		//sleep(3);
		//debug("Removed Freed Partition %s",part_name);
	}
	
	/*************************************************/
//	}
	
	itr_curr = list_iterator_create(bgl_curr_part_list);
	while ((init_record = (bgl_record_t*) list_next(itr_curr))) {
		part_id=init_record->bgl_part_id;
		itr_found = list_iterator_create(bgl_found_part_list);
		while ((found_record = (bgl_record_t*) list_next(itr_found)) 
				!= NULL) {
			if (!strcmp(init_record->bgl_part_id, 
				found_record->bgl_part_id)) {
				break;	/* don't reboot this one */
			}
		}
		list_iterator_destroy(itr_found);
		if(found_record == NULL) {
			
			debug("removing the jobs on partition %s\n",
			      (char *)part_id);
			term_jobs_on_part(part_id);
			
			debug("destroying %s\n",(char *)part_id);
			rc = bgl_free_partition(part_id);
			
			rm_remove_partition(part_id);
			debug("done\n");			
		}
	}
	//exit(0);
	list_iterator_destroy(itr_curr);
#endif	
	return 1;
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
		fatal("bluegene.conf file not defined");
	if (stat(bgl_conf, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bgl_conf);
	if (last_config_update
	&&  (last_config_update == config_stat.st_mtime)) {
		debug("bluegene.conf unchanged");
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
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("_read_bgl_config line %d, of input file %s "
			      "too long", line_num, bgl_conf);
			fclose(bgl_spec_file);
			return E2BIG;
			break;
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
		
	if (!bluegene_blrts)
		fatal("BlrtsImage not configured in bluegene.conf");
	if (!bluegene_linux)
		fatal("LinuxImage not configured in bluegene.conf");
	if (!bluegene_mloader)
		fatal("MloaderImage not configured in bluegene.conf");
	if (!bluegene_ramdisk)
		fatal("RamDiskImage not configured in bluegene.conf");
	if (!numpsets)
		info("Warning: Numpsets not configured in bluegene.conf");
	
	/* Check to see if the configs we have are correct */
	if (!_validate_config_nodes()) { 
		_delete_old_partitions();
		/* FIXME: Wait for MMCS to actually complete the 
		 * partition deletions */
		sleep(3);
	}
	
	/* looking for partitions only I created */
	if (create_static_partitions(NULL)) {
		/* error in creating the static partitions, so
		 * partitions referenced by submitted jobs won't
		 * correspond to actual slurm partitions/bgl
		 * partitions.
		 */
		fatal("Error, could not create the static partitions");
		return error_code;
	}
	return error_code;
}

static void _strip_13_10(char *word)
{
	int len = strlen(word);
	int i;

	for(i=0;i<len;i++) {
		if(word[i]==13 || word[i]==10) {
			word[i] = '\0';
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
	char *pset_num=NULL;
	bgl_record_t *bgl_record, *found_record;
	
	error_code = slurm_parser(in_line,
				  "BlrtsImage=", 's', &blrts_image,
				  "LinuxImage=", 's', &linux_image,
				  "MloaderImage=", 's', &mloader_image,
				  "Numpsets=", 's', &pset_num,
				  "Nodes=", 's', &nodes,
				  "RamDiskImage=", 's', &ramdisk_image,
				  "Type=", 's', &conn_type,
				  "Use=", 's', &node_use,
				  "END");

	if (error_code)
		goto cleanup;

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
	if (pset_num) {
		_strip_13_10(pset_num);
		numpsets = atoi(pset_num);
		xfree(pset_num);
	}

	/* Process node information */
	if (!nodes && !conn_type)
		goto cleanup;	/* no data */
	if (!nodes && conn_type) {
		error("bluegene.conf lacks Nodes value, but has "
			"Type or Use value");
		error_code = SLURM_ERROR;
		goto cleanup;
	}

	bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
	list_push(bgl_list, bgl_record);
	
	bgl_record->bgl_part_list = list_create(NULL);			
	bgl_record->hostlist = hostlist_create(NULL);
	
	_strip_13_10(nodes);
	bgl_record->nodes = xstrdup(nodes);
	xfree(nodes);	/* pointer moved, nothing left to xfree */
	
	_process_nodes(bgl_record);
	
	if (conn_type)
		_strip_13_10(conn_type);
	if (!conn_type || !strcasecmp(conn_type,"TORUS"))
		bgl_record->conn_type = SELECT_TORUS;
	else
		bgl_record->conn_type = SELECT_MESH;
	
	if (conn_type)
		xfree(conn_type);	/* pointer moved, nothing left to xfree */

	if (node_use) {
		/* First we check to see if we only want one type of mode */
		if(!strcasecmp(conn_type,"COPROCESSOR"))
			bgl_record->node_use = SELECT_COPROCESSOR_MODE;
		else
			bgl_record->node_use = SELECT_VIRTUAL_NODE_MODE;
		bgl_record->partner = NULL;
	} else {
		/* If not then we will make both. */

		/* this is here to make a co_proc and virtual partition just like each other */

		bgl_record->node_use = SELECT_VIRTUAL_NODE_MODE;
			
		found_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
		list_push(bgl_list, found_record);
	
		bgl_record->partner = found_record;
		found_record->partner = bgl_record;
		
		found_record->bgl_part_list = bgl_record->bgl_part_list;			
		found_record->hostlist = bgl_record->hostlist;
		found_record->nodes = xstrdup(bgl_record->nodes);
	
		found_record->bp_count = bgl_record->bp_count;
		found_record->switch_count = bgl_record->switch_count;
		found_record->geo[X] = bgl_record->geo[X];
		found_record->geo[Y] = bgl_record->geo[Y];
		found_record->geo[Z] = bgl_record->geo[Z];
	
		found_record->conn_type = bgl_record->conn_type;
		found_record->bitmap = bgl_record->bitmap;
		found_record->node_use = SELECT_COPROCESSOR_MODE;
	
	}
#if _DEBUG
	debug("_parse_bgl_spec: added nodes=%s type=%s use=%s", 
		bgl_record->nodes, 
		convert_conn_type(bgl_record->conn_type), 
		convert_node_use(bgl_record->node_use));
#endif

  cleanup:
	return error_code;
}

static void _process_nodes(bgl_record_t *bgl_record)
{
#ifdef HAVE_BGL
	int j=0, number;
	int start[PA_SYSTEM_DIMENSIONS];
	int end[PA_SYSTEM_DIMENSIONS];
	char buffer[BUFSIZE];
	int funky=0;

	bgl_record->bp_count = 0;

	while (bgl_record->nodes[j] != '\0') {
		if ((bgl_record->nodes[j]   == '[')
		    && (bgl_record->nodes[j+8] == ']')
		    && ((bgl_record->nodes[j+4] == 'x')
			|| (bgl_record->nodes[j+4] == '-'))) {
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
			j += 5;
			bgl_record->bp_count += _addto_node_list(bgl_record, 
						       start, 
						       end);
			if(bgl_record->nodes[j] != ',')
				break;
		} else if((bgl_record->nodes[j] < 58 && bgl_record->nodes[j] > 47) 
			  && bgl_record->nodes[j-1] != '[') {
					
			number = atoi(bgl_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			bgl_record->bp_count += _addto_node_list(bgl_record, 
						       start, 
						       start);
			if(bgl_record->nodes[j] != ',')
				break;	
		}
		j++;
	}
	hostlist_ranged_string(bgl_record->hostlist, BUFSIZE, buffer);
	if(strcmp(buffer,bgl_record->nodes)) {
		xfree(bgl_record->nodes);
		bgl_record->nodes = xstrdup(buffer);
	}
	j=0;
	while (bgl_record->nodes[j] != '\0') {
		if ((bgl_record->nodes[j]   == '[')
		    && (bgl_record->nodes[j+8] == ']')
		    && ((bgl_record->nodes[j+4] == 'x')
			|| (bgl_record->nodes[j+4] == '-'))) {
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
			j += 5;			
			if(bgl_record->nodes[j] != ',') 
				break;
			funky=1;
		} else if((bgl_record->nodes[j] < 58 && bgl_record->nodes[j] > 47) 
			  && bgl_record->nodes[j-1] != '[') {
			number = atoi(bgl_record->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			if(bgl_record->nodes[j] != ',')
				break;	
			funky=1;
		}
		
		j++;
	}

	if(!funky) {
		bgl_record->geo[X] = (end[X] - start[X])+1;
		bgl_record->geo[Y] = (end[Y] - start[Y])+1;
		bgl_record->geo[Z] = (end[Z] - start[Z])+1;
	}
		
	if (node_name2bitmap(bgl_record->nodes, 
			     false, 
			     &bgl_record->bitmap)) {
		error("Unable to convert nodes %s to bitmap", 
		      bgl_record->nodes);
	}
#endif
	return;
}

