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
#define NODE_POLL_TIME 120	/* poll MMCS node state every 120 secs */
#define SWITCH_POLL_TIME 180	/* poll MMCS switch state every 180 secs */

#define _DEBUG 0

char* bgl_conf = BLUEGENE_CONFIG_FILE;

/* Global variables */
rm_BGL_t *bgl;
List bgl_list = NULL;			/* list of bgl_record entries */
List bgl_curr_part_list = NULL;  	/* current bgl partitions */
List bgl_found_part_list = NULL;  	/* found bgl partitions */
char *bluegene_blrts = NULL, *bluegene_linux = NULL, *bluegene_mloader = NULL;
char *bluegene_ramdisk = NULL;
bool agent_fini = false;

/* some local functions */
static int  _addto_node_list(bgl_record_t *bgl_record, int *start, int *end);
static void _set_bgl_lists();
static int  _validate_config_nodes(void);
static int  _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b);
/* static int  _bgl_record_cmpf_dec(bgl_record_t* rec_a, bgl_record_t* rec_b); */
//static int  _copy_slurm_partition_list(List slurm_part_list);
//static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2, char *tv_str, int len_tv_str);
static int  _parse_bgl_spec(char *in_line);
//static int  _parse_request(char* request_string, partition_t** request);
static void _process_nodes(bgl_record_t *bgl_record);
//static void _process_config(void);
//static int  _sync_partitions(void);
//static bgl_record_t* _find_config_by_nodes(char* nodes);
//static int  _listfindf_part_record(bgl_record_t* bgl_record, char *nodes);
//static int _char2num(char in);

/* Initialize all plugin variables */
int init_bgl(void)
{
#ifdef HAVE_BGL_FILES
	int rc;
	rm_size3D_t bp_size;
#endif


#ifdef HAVE_BGL_FILES
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
	pa_init(NULL);
#endif

	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
void fini_bgl(void)
{
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
	if (bgl)
		slurm_rm_free_BGL(bgl);
#endif
	pa_fini();
}

void print_bgl_record(bgl_record_t* bgl_record)
{
	if (!bgl_record) {
		error("print_bgl_record, record given is null");
		return;
	}

//#if DEBUG
	info(" bgl_record: ");
	if (bgl_record->bgl_part_id)
		info("\tbgl_part_id: %s", bgl_record->bgl_part_id);
	info("\tnodes: %s", bgl_record->nodes);
	info("\tsize: %d", bgl_record->bp_count);
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
//#endif
	//exit(0);
}

void destroy_bgl_record(void* object)
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

char* convert_lifecycle(lifecycle_type_t lifecycle)
{
	if (lifecycle == DYNAMIC)
		return "DYNAMIC";
	else 
		return "STATIC";
}

char* convert_conn_type(rm_connection_type_t conn_type)
{
	switch (conn_type) {
		case (SELECT_MESH): 
			return "RM_MESH"; 
		case (SELECT_TORUS): 
			return "RM_TORUS"; 
		case (SELECT_NAV):
			return "RM_NAV";
		default:
			break;
	}
	return "";
}

char* convert_node_use(rm_partition_mode_t pt)
{
	switch (pt) {
		case (SELECT_COPROCESSOR_MODE): 
			return "RM_COPROCESSOR"; 
		case (SELECT_VIRTUAL_NODE_MODE): 
			return "RM_VIRTUAL"; 
		default:
			break;
	}
	return "";
}


/** 
 * Comparator used for sorting partitions largest to smallest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
/* static int _bgl_record_cmpf_dec(bgl_record_t* rec_a, bgl_record_t* rec_b) */
/* { */
/* 	if (rec_a->size > rec_b->size) */
/* 		return -1; */
/* 	else if (rec_a->size < rec_b->size) */
/* 		return 1; */
/* 	else  */
/* 		return 0; */
/* } */

/** 
 * sort the partitions by increasing size
 */
void sort_bgl_record_inc_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) _bgl_record_cmpf_inc);
}

/** 
 * sort the partitions by decreasing size
 */
/* void sort_bgl_record_dec_size(List records){ */
/* 	if (records == NULL) */
/* 		return; */
/* 	list_sort(records, (ListCmpF) _bgl_record_cmpf_dec); */
/* } */

/*
 * bluegene_agent - detached thread periodically updates status of
 * bluegene nodes. 
 * 
 * NOTE: I don't grab any locks here because slurm_drain_nodes grabs
 * the necessary locks.
 */
void *bluegene_agent(void *args)
{
	static time_t last_node_test, last_switch_test, now;

	last_node_test = last_switch_test = time(NULL);
	while (!agent_fini) {
		sleep(1);
		now = time(NULL);

		if (difftime(now, last_node_test) >= NODE_POLL_TIME) {
			if (agent_fini)		/* don't bother */
				return NULL;	/* quit now */
			last_node_test = now;
			test_down_nodes();	/* can run for a while */
		}

		if (difftime(now, last_switch_test) >= SWITCH_POLL_TIME) {
			if (agent_fini)         /* don't bother */
				return NULL;	/* quit now */
			last_switch_test = now;
			test_down_switches();	/* can run for a while */
		}
	}
	return NULL;
}

/*
 * _diff_tv_str - build a string showing the time difference between two times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 */
/* static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2, */
/* 		char *tv_str, int len_tv_str) */
/* { */
/* 	long delta_t; */
/* 	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000; */
/* 	delta_t +=  tv2->tv_usec - tv1->tv_usec; */
/* 	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t); */
/* } */

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
char *bgl_err_str(status_t inx)
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
int create_static_partitions(List part_list)
{
	int rc = SLURM_SUCCESS;

#ifdef HAVE_BGL_FILES

	ListIterator itr, itr_found;
	bgl_record_t *bgl_record, *found_record;
	
	reset_pa_system();
	
	itr = list_iterator_create(bgl_list);
	while ((bgl_record = (bgl_record_t *) list_next(itr)) != NULL) {
			
		if(bgl_record->bp_count>0)
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
			configure_partition(bgl_record);
			print_bgl_record(bgl_record);
		}
	}
	list_iterator_destroy(itr);
	rc = SLURM_SUCCESS;
	
#else
	if (bgl_list) {
		bgl_record_t *bgl_record;
		while ((bgl_record = list_pop(bgl_list)))
			destroy_bgl_record(bgl_record);
	} else
		bgl_list = list_create(destroy_bgl_record);

	/* copy the slurm.conf partition info from slurmctld into bgl_list */
/* 	if ((rc = _copy_slurm_partition_list(part_list))) */
/* 		return rc; */

	/* syncronize slurm.conf and bluegene.conf data */
//	_process_config();
#endif
	return rc;
}

/* Synchronize the actual bluegene partitions to that configured in SLURM */ 
/* static int _sync_partitions(void) */
/* { */
/* 	int rc = SLURM_SUCCESS; */

/* 	/\* Check if partitions configured in SLURM are already configured on */
/* 	 * the system *\/ */
/* 	if ((rc = _validate_config_nodes())) { */
/* 		/\* If not, delete all existing partitions and jobs then */
/* 		 * configure from scratch *\/ */
/* 		rc = _wire_bgl_partitions(); */
/* 	} */

/* 	return rc; */
/* } */

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
				hostlist_push(bgl_record->hostlist, node_name_tmp);
				list_append(bgl_record->bgl_part_list, 
					    &pa_system_ptr->grid[x][y][z]);
				node_count++;
			}
		}
	}
	return node_count;
}

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
	int rc = 1;
#ifdef HAVE_BGL_FILES
	bgl_record_t* record;	/* records from configuration files */
	bgl_record_t* init_record;	/* records from actual BGL config */
	ListIterator itr_conf, itr_curr;


	/* read current bgl partition info into bgl_curr_part_list */
	if (read_bgl_partitions() == SLURM_ERROR)
		return -1;

	itr_conf = list_iterator_create(bgl_list);
	while ((record = (bgl_record_t*) list_next(itr_conf))) {
		/* translate hostlist to ranged string for consistent format */
        	/* search here */
		itr_curr = list_iterator_create(bgl_curr_part_list);
		while ((init_record = (bgl_record_t*) list_next(itr_curr)) != NULL) {
/* 			info("%s:%s",record->nodes, init_record->nodes); */
/* 			info("%d:%d", record->conn_type, init_record->conn_type); */
/* 			info("%d:%d", record->node_use, init_record->node_use); */
			if (strcasecmp(record->nodes, init_record->nodes)) {
				continue;	/* wrong nodes */
			}
			//info("found %s:%s",record->nodes, init_record->nodes);
			if ((record->conn_type != init_record->conn_type)
			    ||  (record->node_use  != init_record->node_use))
				break;		/* must reconfig this part */
			record->bgl_part_id = xstrdup(init_record->bgl_part_id);
			break;
		}
		list_iterator_destroy(itr_curr);
		if (!record->bgl_part_id) {
			info("BGL PartitionID:NONE Nodes:%s", record->nodes);
			rc = 0;
		} else {
			list_push(bgl_found_part_list, record);
			info("BGL PartitionID:%s Nodes:%s Conn:%s Mode:%s",
				record->bgl_part_id, record->nodes,
				convert_conn_type(record->conn_type),
				convert_node_use(record->node_use));
		}
		
	}
	list_iterator_destroy(itr_conf);
	if(list_count(bgl_list) != list_count(bgl_curr_part_list))
		rc = 0;
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

/* 
 * Copy the current partition info that slurmctld read from slurm.conf
 * so that we can maintain our own separate table in bgl_list. Note that
 * read_bgl_conf() has already been executed and read bluegene.conf.
 */
/* static int _copy_slurm_partition_list(List slurm_part_list) */
/* { */
/* 	struct part_record* slurm_part; */
/* 	bgl_record_t* bgl_record; */
/* 	ListIterator itr; */
/* 	char* cur_nodes, *delimiter=",", *nodes_tmp, *next_ptr, *orig_ptr; */
/* 	int rc = SLURM_SUCCESS; */

/* 	itr = list_iterator_create(slurm_part_list); */
/* 	/\* */
/* 	 * try to find the corresponding bgl_record for the */
/* 	 * nodes specified in the slurm_part_list, but if not */
/* 	 * found, _find_conn_type will default to RM_MESH */
/* 	 *\/ */
/* 	while ((slurm_part = (struct part_record *) list_next(itr))) { */
/* 		/\* no need to create record for slurm partition without nodes *\/ */
/* 		if ((slurm_part->nodes == NULL) */
/* 		||  (slurm_part->nodes[0] == '\0')) */
/* 			continue; */
/* 		nodes_tmp = xstrdup(slurm_part->nodes); */
/* 		orig_ptr = nodes_tmp; */

/* 		cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr); */
/* #if _DEBUG */
/* 		debug("_copy_slurm_partition_list parse:%s, token[0]:%s", */
/* 			slurm_part->nodes, cur_nodes); */
/* #endif */
/* 		/\* */
/* 		 * for each of the slurm partitions, there may be */
/* 		 * several bgl partitions, so we need to find how to */
/* 		 * wire each of those bluegene partitions. */
/* 		 *\/ */
/* 		while (cur_nodes != NULL) { */
/* 			bgl_record_t *config_ptr; */
/* 			config_ptr = _find_config_by_nodes(cur_nodes); */
/* 			if (config_ptr == NULL) { */
/* 				error("Nodes missing from bluegene.conf: %s", */
/* 					cur_nodes); */
/* 				rc = SLURM_ERROR; */
/* 				goto cleanup; */
/* 			} */

/* 			bgl_record = (bgl_record_t*) xmalloc( */
/* 					sizeof(bgl_record_t)); */
/* 			bgl_record->nodes = xstrdup(cur_nodes); */
			
/* 			bgl_record->node_use = config_ptr->node_use; */
/* 			bgl_record->conn_type = config_ptr->conn_type; */
			

/* #if 0	/\* Possible future use *\/ */
/* 			if ((slurm_part->min_nodes != slurm_part->max_nodes) */
/* 			||  (bgl_record->bp_count != slurm_part->max_nodes)) */
/* 				bgl_record->part_lifecycle = DYNAMIC; */
/* 			else */
/* #endif */
/* 				bgl_record->part_lifecycle = STATIC; */

/* 			print_bgl_record(bgl_record); */
/* 			list_push(bgl_list, bgl_record); */

/* 			nodes_tmp = next_ptr; */
/* 			cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr); */
/* 		} /\* end while(cur_nodes) *\/ */

/* 	cleanup: */
/* 		xfree(orig_ptr); */
			
/* 	} /\* end while(slurm_part) *\/ */
/* 	list_iterator_destroy(itr); */
/* 	return rc; */
/* } */

static int _delete_old_partitions()
{
#ifdef HAVE_BGL_FILES
	int rc;
	ListIterator itr_curr, itr_found;
	bgl_record_t *found_record, *init_record;
	rm_partition_state_t state;
        pm_partition_id_t part_id;
	rm_partition_t *my_part;

	/******************************************************************/
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP101"; */

/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP102"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP103"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP104"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP105"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP106"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP107"; */
/* 		init_record = xmalloc(sizeof(bgl_record_t)); */
/* 		list_push(bgl_curr_part_list, init_record); */
/* 		init_record->bgl_part_id = "RMP108"; */
			
	itr_curr = list_iterator_create(bgl_curr_part_list);
	while ((init_record = (bgl_record_t*) list_next(itr_curr))) {
		part_id=init_record->bgl_part_id;
		itr_found = list_iterator_create(bgl_found_part_list);
		while ((found_record = (bgl_record_t*) list_next(itr_found)) != NULL) {
			if (!strcmp(init_record->bgl_part_id, found_record->bgl_part_id)) {
				break;	/* don't reboot this one */
			}
		}
		list_iterator_destroy(itr_found);
		if(found_record == NULL) {
			if ((rc = rm_get_partition(part_id, &my_part))
			    != STATUS_OK) {
			} else {
				
				debug("removing the jobs on partition %s\n",(char *)part_id);
				term_jobs_on_part(part_id);

				debug("destroying %s\n",(char *)part_id);
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
					rm_get_data(my_part, RM_PartitionState, &state);
					//Free memory allocated to mypart
				}
				rm_remove_partition(part_id);
				sleep(3);
				debug("done\n");
			}	
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
int read_bgl_conf(void)
{
	FILE *bgl_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code;
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

	/* Check to see if the configs we have are correct */
	if (!_validate_config_nodes()) { 
		_delete_old_partitions();
	}
	/*looking for partitions only I created */
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
	bgl_record_t* bgl_record;
	
	error_code = slurm_parser(in_line,
				"BlrtsImage=", 's', &blrts_image,
				"LinuxImage=", 's', &linux_image,
				"MloaderImage=", 's', &mloader_image,
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

	/* Process node information */
	if (!nodes && !node_use && !conn_type)
		goto cleanup;	/* no data */
	if (!nodes && (node_use || conn_type)) {
		error("bluegene.conf lacks Nodes value, but has "
			"Type or Use value");
		error_code = SLURM_ERROR;
		goto cleanup;
	}

	bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));

	bgl_record->bgl_part_list = list_create(NULL);			
	bgl_record->hostlist = hostlist_create(NULL);
	bgl_record->nodes = xstrdup(nodes);
	xfree(nodes);	/* pointer moved, nothing left to xfree */
	_process_nodes(bgl_record);

	if (!conn_type || !strcasecmp(conn_type,"TORUS"))
		bgl_record->conn_type = SELECT_TORUS;
	else
		bgl_record->conn_type = SELECT_MESH;
	//printf("node_use = %s\n",node_use);
	if (!node_use || !strcasecmp(node_use,"COPROCESSOR"))
		bgl_record->node_use = SELECT_COPROCESSOR_MODE;
	else
		bgl_record->node_use = SELECT_VIRTUAL_NODE_MODE;
	/* printf("this is the mode "); */
/* 	printf("%d\n",bgl_record->node_use); */
	list_push(bgl_list, bgl_record);

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
	int j=0, number;
	int start[PA_SYSTEM_DIMENSIONS];
	int end[PA_SYSTEM_DIMENSIONS];
	char buffer[BUFSIZE];
	
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

	if (node_name2bitmap(bgl_record->nodes, 
			     false, 
			     &bgl_record->bitmap)) {
		error("Unable to convert nodes %s to bitmap", 
		      bgl_record->nodes);
	}
	return;
}

/*
 * process the slurm configuration to interpret BGL specific semantics: 
 * if MaxNodes == MinNodes == size (Nodes), = static partition, otherwise
 * creates a List of allocation requests made up of partition_t's (see 
 * partition_sys.h)
 */
/* static void _process_config(void) */
/* { */
/* 	ListIterator itr; */
/* 	bgl_record_t *bgl_part; */
/* 	partition_t* request_result; */

/* 	itr = list_iterator_create(bgl_list); */
/* 	while ((bgl_part = (bgl_record_t*) list_next(itr))) { */
/* 		/\* */
/* 		 * _parse_request() will fill up the partition_t's */
/* 		 * bl_coord, tr_coord, dimensions, and size */
/* 		 *\/ */
/* 		request_result = NULL; */
/* 		if (_parse_request(bgl_part->nodes, &request_result) */
/* 		|| (request_result == NULL)) */
/* 			error("_process_config: error parsing request %s", */
/* 				bgl_part->nodes); */
/* 		else { */
/* 			/\* */
/* 			 * bgl_part->conn_type should have been extracted in */
/* 			 * copy_slurm_partition_list */
/* 			 *\/ */
/* 			request_result->bgl_record_ptr = bgl_part; */
/* 			request_result->node_use = bgl_part->node_use; */
/* 			request_result->conn_type = bgl_part->conn_type; */
/* 			bgl_part->alloc_part = request_result; */
/* 		} */
/* 	} */
/* 	list_iterator_destroy(itr); */
/* } */

/* 
 * search through the list of nodes, types to find the partition 
 * containing the given nodes
 */
/* static bgl_record_t* _find_config_by_nodes(char* nodes) */
/* { */
/* 	return (bgl_record_t*) list_find_first(bgl_list, */
/* 				(ListFindF) _listfindf_part_record, */
/* 				nodes); */
/* } */

/* /\* Compare node list in bgl_record against node list string *\/ */
/* static int _listfindf_part_record(bgl_record_t* bgl_record, char *nodes) */
/* { */
/* 	return (!strcasecmp(bgl_record->nodes, nodes)); */
/* } */

/* 
 * Convert a character into its numeric equivalent or -1 on error
 */
/* static int _char2num(char in) */
/* { */
/* 	int i = in - '0'; */
/* 	if ((i < 0) || (i > 9)) */
/* 		return -1; */
/* 	return i; */
/* } */

/*
 * Translate a node list into numeric locations in the BGL node matric
 * IN request_string - node list, must be in the form "bgl[123x456]"
 * OUT request_result - allocated data structure (must be xfreed) that 
 *   notes end-points in a node block
 */
/* static int _parse_request(char* request_string, partition_t** request_result) */
/* { */
/* 	int loc = 0, i,j, rc = SLURM_ERROR; */

/* 	if (!request_string) { */
/* 		error("_parse_request request_string is NULL"); */
/* 		return SLURM_ERROR; */
/* 	} */

/* 	debug3("bluegene config request %s", request_string); */
/* 	*request_result = (partition_t*) xmalloc(sizeof(partition_t)); */

/* 	for (i=0; ; i++) { */
/* 		if (request_string[i] == '\0') */
/* 			break; */
/* 		if (loc == 0) { */
/* 			if (request_string[i] == '[') */
/* 				loc++; */
/* 		} else if (loc == 1) { */
/* 			for (j=0; j<SYSTEM_DIMENSIONS; j++) { */
/* 				(*request_result)->bl_coord[j] = */
/* 						_char2num(request_string[i+j]); */
/* 			} */
/* 			i += (SYSTEM_DIMENSIONS - 1); */
/* 			loc++; */
/* 		} else if (loc == 2) { */
/* 			if (request_string[i] != 'x') */
/* 				break; */
/* 			loc++; */
/* 		} else if (loc == 3) { */
/* 			for (j=0; j<SYSTEM_DIMENSIONS; j++) { */
/* 				(*request_result)->tr_coord[j] = */
/* 						_char2num(request_string[i+j]); */
/* 			} */
/* 			i += (SYSTEM_DIMENSIONS - 1); */
/* 			loc++; */
/* 		} else if (loc == 4) { */
/* 			if (request_string[i] != ']') */
/* 				break; */
/* 			loc++; */
/* 			break; */
/* 		} */
/* 	} */
/* 	if (loc != 5) { */
/* 		error("_parse_request: Mal-formed node list: %s", */
/* 			request_string); */
/* 		/\* error("DIM=%d, loc=%d i=%d", SYSTEM_DIMENSIONS, loc, i); *\/ */
/* 		goto cleanup; */
/* 	} */

/* 	(*request_result)->size = 1; */
/* 	for (i=0; i<SYSTEM_DIMENSIONS; i++) { */
/* 		if (((*request_result)->bl_coord[i] < 0) */
/* 		||  ((*request_result)->tr_coord[i] < 0)) { */
/* 			error("_parse_request: Bad node list values: %s", */
/* 				request_string); */
/* 			goto cleanup; */
/* 		} */
/* 		/\* count self *\/ */
/* 		(*request_result)->dimensions[i] = */
/* 			(*request_result)->tr_coord[i] - */
/* 			(*request_result)->bl_coord[i] + 1; */
/* 		(*request_result)->size *= */
/* 			(*request_result)->dimensions[i]; */
/* 	} */
/* 	rc = SLURM_SUCCESS; */
	
/*  cleanup: */
/* 	if (rc == SLURM_ERROR) */
/* 		xfree(*request_result); */
/* 	return rc; */
/* } */

