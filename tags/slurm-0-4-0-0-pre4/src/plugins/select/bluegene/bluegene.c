/*****************************************************************************\
 *  bluegene.c - blue gene node allocation module. 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <time.h>
#include <slurm/slurm.h>

#include "src/slurmctld/proc_req.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/common/xstring.h"
#include "bluegene.h"
#include "partition_sys.h"

#define BUFSIZE 4096
#define BITSIZE 128
#define SLEEP_TIME 60 /* BLUEGENE_PTHREAD checks every 60 secs */
#define _DEBUG 0

char* bgl_conf = BLUEGENE_CONFIG_FILE;

/* Global variables */
List bgl_list = NULL;			/* list of bgl_record entries */
List bgl_conf_list = NULL;		/* list of bgl_conf_record entries */

#define SWAP(a,b,t)	\
_STMT_START {	\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END

 /** some local functions */
static int  _copy_slurm_partition_list(List slurm_part_list);
static int _find_best_partition_match(struct job_record* job_ptr, 
				bitstr_t* slurm_part_bitmap,
				int min_nodes, int max_nodes,
				int spec, bgl_record_t** found_bgl_record);
static int _parse_request(char* request_string, partition_t** request);
static int _wire_bgl_partitions(void);
static int _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b);
static int _bgl_record_cmpf_dec(bgl_record_t* rec_a, bgl_record_t* rec_b);
static void _destroy_bgl_record(void* object);
static void _destroy_bgl_conf_record(void* object);
static void _process_config();
static int _parse_bgl_spec(char *in_line);
static bgl_conf_record_t* _find_config_by_nodes(char* nodes);
static int _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes);
static void _update_bgl_node_bitmap(void);
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str);

/* Rotate a geometry array through six permutations */
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);

#ifdef USE_BGL_FILES
static char *_convert_bp_state(rm_BP_state_t state);
static void _set_bp_node_state(rm_BP_state_t state, rm_element_t *element);
#endif

/**
 * create_static_partitions - create the static partitions that will be used
 * for scheduling.  
 * IN - (global, from slurmctld): the system and desired partition configurations 
 * OUT - (global, to slurmctld): Table of partitionIDs to geometries
 * RET - success of fitting all configurations
 */
extern int create_static_partitions(List part_list)
{
	/** purge the old list.  Later on, it may be more efficient just to amend the list */
	if (bgl_list) {
		bgl_record_t *record;
		while ((record = list_pop(bgl_list)))
			_destroy_bgl_record(record);
	} else
		bgl_list = list_create(_destroy_bgl_record);

	/** copy the slurm conf partition info, this will fill in bgl_list */
	if (_copy_slurm_partition_list(part_list))
		return SLURM_ERROR;

	_process_config();
	/* after reading in the configuration, we have a list of partition 
	 * requests (List <int*>) that we can use to partition up the system
	 */
	_wire_bgl_partitions();

	return SLURM_SUCCESS;
}

/**
 * IN - requests: list of bgl_record(s)
 */
static int _wire_bgl_partitions(void)
{
	bgl_record_t* cur_record;
	partition_t* cur_partition;
	ListIterator itr;

	itr = list_iterator_create(bgl_list);
	while ((cur_record = (bgl_record_t*) list_next(itr))) {
		cur_partition = (partition_t*) cur_record->alloc_part;
		if (configure_switches(cur_partition))
			error("error on cur_record %s", cur_record->nodes);
	}	
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

/** 
 * process the slurm configuration to interpret BGL specific semantics: 
 * if MaxNodes == MinNodes == size (Nodes), = static partition, otherwise
 * 
 * creates a List of allocation requests made up of partition_t's (see partition_sys.h)
 * 
 * 
 */
static void _process_config()
{
	ListIterator itr;
	bgl_record_t *bgl_part;
	partition_t* request_result;

	itr = list_iterator_create(bgl_list);
	while ((bgl_part = (bgl_record_t*) list_next(itr))) {
		/** 
		 * _parse_request() will fill up the partition_t's
		 * bl_coord, tr_coord, dimensions, and size
		 */
		request_result = NULL;
		if (_parse_request(bgl_part->nodes, &request_result)
		|| (request_result == NULL))
			error("_process_config: error parsing request %s", 
				bgl_part->nodes);
		else {
			/** 
			 * bgl_part->conn_type should have been extracted in
			 * copy_slurm_partition_list
			 */
			request_result->bgl_record_ptr = bgl_part;
			request_result->node_use = bgl_part->node_use;
			request_result->conn_type = bgl_part->conn_type;
			bgl_part->alloc_part = request_result;
		}
	}
	list_iterator_destroy(itr);
}

/* copy the current partition info that was read in from slurm.conf so
 * that we can maintain our own separate table of bgl_part_id to
 * slurm_part_id.
 */
static int _copy_slurm_partition_list(List slurm_part_list)
{
	struct part_record* slurm_part;
	bgl_record_t* bgl_record;
	ListIterator itr;
	char* cur_nodes, *delimiter=",", *nodes_tmp, *next_ptr, *orig_ptr;
	int rc = SLURM_SUCCESS;

	itr = list_iterator_create(slurm_part_list);
	/** 
	 * try to find the corresponding bgl_conf_record for the
	 * nodes specified in the slurm_part_list, but if not
	 * found, _find_conn_type will default to RM_MESH
	 */
	while ((slurm_part = (struct part_record *) list_next(itr))) {
		/* no need to create record for slurm partition without nodes*/
		if ((slurm_part->nodes == NULL)
		|| (slurm_part->nodes[0] == '\0'))
			continue;
		nodes_tmp = xstrdup(slurm_part->nodes);
		orig_ptr = nodes_tmp;

		cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr);
#if _DEBUG
		debug("_copy_slurm_partition_list parse:%s, token[0]:%s", 
			slurm_part->nodes, cur_nodes);
#endif
		/** 
		 * for each of the slurm partitions, there may be
		 * several bgl partitions, so we need to find how to
		 * wire each of those bgl partitions.
		 */
		while (cur_nodes != NULL) {
			bgl_conf_record_t *config_ptr;
			config_ptr = _find_config_by_nodes(cur_nodes);
			if (config_ptr == NULL) {
				error("Nodes missing from bluegene.conf: %s", cur_nodes);
				rc = SLURM_ERROR;
				goto cleanup;
			}

			bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
			bgl_record->nodes = xstrdup(cur_nodes);
			bgl_record->slurm_part_id = xstrdup(slurm_part->name);

			bgl_record->node_use = config_ptr->node_use;
			bgl_record->conn_type = config_ptr->conn_type;
			bgl_record->hostlist = hostlist_create(cur_nodes);
			bgl_record->size = hostlist_count(bgl_record->hostlist);
			if (node_name2bitmap(cur_nodes, false, &(bgl_record->bitmap))){
				error("_copy_slurm_partition_list unable to convert nodes "
					"%s to bitmap", cur_nodes);
				_destroy_bgl_record(bgl_record);
				rc = SLURM_ERROR;
				goto cleanup;
			}

#if 0	/* Future use */
			if ((slurm_part->min_nodes != slurm_part->max_nodes)
			|| (bgl_record->size != slurm_part->max_nodes))
				bgl_record->part_lifecycle = DYNAMIC;
			else
#endif
				bgl_record->part_lifecycle = STATIC;

			print_bgl_record(bgl_record);
			list_push(bgl_list, bgl_record);

			nodes_tmp = next_ptr;
			cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr);
		} /* end while(cur_nodes) */

	cleanup:
		xfree(orig_ptr);
			
	} /* end while(slurm_part) */
	list_iterator_destroy(itr);

	return rc;
}

extern int read_bgl_conf(void)
{
	DEF_TIMERS;
	FILE *bgl_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code;
	bgl_conf_record_t *conf_rec;

	/* initialization */
	START_TIMER;
	/* bgl_conf defined in bgl_node_alloc.h */
	bgl_spec_file = fopen(bgl_conf, "r");
	if (bgl_spec_file == NULL)
		fatal("read_bgl_conf error opening file %s, %m",
		      bgl_conf);
	/* empty the old list before reading new data */
	while ((conf_rec = list_pop(bgl_conf_list)))
		_destroy_bgl_conf_record(conf_rec);

	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUFSIZE, bgl_spec_file) != NULL) {
		line_num++;
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("read_bgl_config line %d, of input file %s "
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

	END_TIMER;
	debug("read_bgl_conf: finished loading configuration %s", TIME_STR);
	
	return error_code;
}

/*
 * phung: edited to piggy back on this function to also allow configuration
 * option of the partition (ie, you can specify the config to be a 2x2x2
 * partition.
 *
 * _parse_part_spec - parse the partition specification, build table and 
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
	char *nodes = NULL, *node_use = NULL, *conn_type = NULL;
	bgl_conf_record_t* new_record;

	error_code = slurm_parser(in_line,
				"Nodes=", 's', &nodes,
				"Type=", 's', &conn_type,
				"Use=", 's', &node_use,
				"END");

	if (error_code)
		goto cleanup;
	if (!nodes && !node_use && !conn_type)
		goto cleanup;	/* only comment */
	if (!nodes && (node_use || conn_type)) {
		error("bluegene.conf lacks Nodes value, but has Type or Use value");
		error_code = SLURM_ERROR;
		goto cleanup;
	}

	new_record = (bgl_conf_record_t*) xmalloc(sizeof(bgl_conf_record_t));
	new_record->nodes = nodes;
	nodes = NULL;	/* pointer moved, nothing left to xfree */

	if (!conn_type)
		new_record->conn_type = RM_MESH;
	else if (strcasecmp(conn_type, "TORUS") == 0)
		new_record->conn_type = RM_TORUS;
	else if (strcasecmp(conn_type, "MESH") == 0)
		new_record->conn_type = RM_MESH;
	else {
		error("_parse_bgl_spec: partition type %s invalid for nodes "
			"%s, defaulting to type: MESH", 
			conn_type, new_record->nodes);
		new_record->conn_type = RM_MESH;
	}

	if (!node_use)
		new_record->node_use = RM_PARTITION_COPROCESSOR_MODE;
	else if (strcasecmp(node_use, "COPROCESSOR") == 0)
		new_record->node_use = RM_PARTITION_COPROCESSOR_MODE;
	else if (strcasecmp(node_use, "VIRTUAL") == 0)
		new_record->node_use = RM_PARTITION_VIRTUAL_NODE_MODE;
	else {
		error("_parse_bgl_spec: node use %s invalid for nodes %s, "
			"defaulting to type: COPROCESSOR", 
			node_use, new_record->nodes);
		new_record->node_use = RM_PARTITION_COPROCESSOR_MODE;
	}
	list_push(bgl_conf_list, new_record);

#if _DEBUG
	debug("_parse_bgl_spec: added nodes=%s type=%s use=%s", 
		new_record->nodes, 
		convert_conn_type(new_record->conn_type), 
		convert_node_use(new_record->node_use));
#endif

  cleanup:
	xfree(conn_type);
	xfree(node_use);
	xfree(nodes);
	return error_code;
}

static void _destroy_bgl_record(void* object)
{
	bgl_record_t* this_record = (bgl_record_t*) object;

	if (this_record) {
		xfree(this_record->nodes);
		xfree(this_record->slurm_part_id);
		if (this_record->hostlist)
			hostlist_destroy(this_record->hostlist);
		if (this_record->bitmap)
			bit_free(this_record->bitmap);
		xfree(this_record->alloc_part);
		xfree(this_record->bgl_part_id);
		xfree(this_record);
	}
}

static void _destroy_bgl_conf_record(void* object)
{
	bgl_conf_record_t* this_record = (bgl_conf_record_t*) object;
	if (this_record) {
		xfree(this_record->nodes);
		xfree(this_record);
	}
}

/** 
 * search through the list of nodes,types to find the partition 
 * containing the given nodes
 */
static bgl_conf_record_t* _find_config_by_nodes(char* nodes)
{
	return (bgl_conf_record_t*) list_find_first(bgl_conf_list,
				(ListFindF) _listfindf_conf_part_record, 
				nodes);
}

/** nodes example: 000x111 */
static int _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes)
{
	return (!strcasecmp(record->nodes, nodes));
}

/** 
 * parses the request_string
 */
static int _char2num(char in)
{
	int i = in - '0';
	if ((i < 0) || (i > 9))
		return -1;
	return i;
}

static int _parse_request(char* request_string, partition_t** request_result)
{
	int loc = 0, i,j, rc = SLURM_ERROR;

	if (!request_string) {
		error("_parse_request request_string is NULL");
		return SLURM_ERROR;
	}

	debug("incoming request %s", request_string);
	*request_result = (partition_t*) xmalloc(sizeof(partition_t));

	for (i=0; ; i++) {
		if (request_string[i] == '\0')
			break;
		if (loc == 0) {
			if (request_string[i] == '[')
				loc++;
		} else if (loc == 1) {
			for (j=0; j<SYSTEM_DIMENSIONS; j++)
				(*request_result)->bl_coord[j] = _char2num(request_string[i+j]);
			i += (SYSTEM_DIMENSIONS - 1);
			loc++;
		} else if (loc == 2) {
			if (request_string[i] != 'x')
				break;
			loc++;
		} else if (loc == 3) {
			for (j=0; j<SYSTEM_DIMENSIONS; j++)
				(*request_result)->tr_coord[j] = _char2num(request_string[i+j]);
			i += (SYSTEM_DIMENSIONS - 1);
			loc++;
		} else if (loc == 4) {
			if (request_string[i] != ']')
				break;
			loc++;
			break;
		}
	}
	if (loc != 5) {
		error("_parse_request: Mal-formed node list: %s", 
			request_string);
error("DIM=%d, loc=%d i=%d", SYSTEM_DIMENSIONS, loc, i);
		goto cleanup;
	}

	(*request_result)->size = 1;
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (((*request_result)->bl_coord[i] < 0)
		||  ((*request_result)->tr_coord[i] < 0)) {
			error("_parse_request: Bad node list values: %s", 
				request_string);
			goto cleanup;
		}
		/* count self */
		(*request_result)->dimensions[i] =
			(*request_result)->tr_coord[i] -
			(*request_result)->bl_coord[i] + 1;
		(*request_result)->size *=
			(*request_result)->dimensions[i];
	}
	rc = SLURM_SUCCESS;
	
 cleanup: 
	if (rc == SLURM_ERROR)
		xfree(*request_result);
	return rc;
}

/* Initialize all plugin variables */
extern int init_bgl(void)
{
#ifdef HAVE_BGL_FILES
	int rc;

	/* FIXME: this needs to be read in from conf file. */
	rc = rm_set_serial("BGL");
	if (rc != STATUS_OK){
		error("init_bgl: rm_set_serial failed, errno=%d", rc);
		return SLURM_ERROR;
	}
#endif

#ifdef USE_BGL_FILES
/* This requires the existence of "./db.properties" file */
	rc = rm_get_BGL(&bgl);
	if (rc != STATUS_OK){
		error("init_bgl: rm_get_BGL failed, errno=%d", rc);
		return SLURM_ERROR;
	}
#endif
	/** global variable */
	bgl_conf_list = (List) list_create(_destroy_bgl_conf_record);

	/* for testing purposes */
	init_bgl_partition_num();

	info("BlueGene plugin loaded successfully");

	return SLURM_SUCCESS;
}

/* Purge all plugin variables */
extern void fini_bgl(void)
{
	if (bgl_list) {
		list_destroy(bgl_list);
		bgl_list = NULL;
	}
	if (bgl_conf_list) {
		list_destroy(bgl_conf_list);
		bgl_conf_list = NULL;
	}
}

extern void print_bgl_record(bgl_record_t* record)
{
	if (!record) {
		error("print_bgl_record, record given is null");
		return;
	}

#if _DEBUG
	info(" bgl_record: ");
	info("\tslurm_part_id: %s", record->slurm_part_id);
	if (record->bgl_part_id)
		info("\tbgl_part_id: %d", *(record->bgl_part_id));
	info("\tnodes: %s", record->nodes);
	info("\tsize: %d", record->size);
	info("\tlifecycle: %s", convert_lifecycle(record->part_lifecycle));
	info("\tconn_type: %s", convert_conn_type(record->conn_type));
	info("\tnode_use: %s", convert_node_use(record->node_use));

	if (record->hostlist){
		char buffer[BUFSIZE];
		hostlist_ranged_string(record->hostlist, BUFSIZE, buffer);
		info("\thostlist %s", buffer);
	}

	if (record->alloc_part){
		info("\talloc_part:");
		print_partition(record->alloc_part);
	} else {
		info("\talloc_part: NULL");
	}

	if (record->bitmap){
		char bitstring[BITSIZE];
		bit_fmt(bitstring, BITSIZE, record->bitmap);
		info("\tbitmap: %s", bitstring);
	}
#endif
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
		case (RM_MESH): 
			return "RM_MESH"; 
		case (RM_TORUS): 
			return "RM_TORUS"; 
		case (RM_NAV):
			return "RM_NAV";
		default:
			break;
	}
	return "";
}

extern char* convert_node_use(rm_partition_mode_t pt)
{
	switch (pt) {
		case (RM_PARTITION_COPROCESSOR_MODE): 
			return "RM_COPROCESSOR"; 
		case (RM_PARTITION_VIRTUAL_NODE_MODE): 
			return "RM_VIRTUAL"; 
		default:
			break;
	}
	return "";
}

/** 
 * finds the best match for a given job request 
 * 
 * IN - int spec right now holds the place for some type of
 * specification as to the importance of certain job params, for
 * instance, geometry, type, size, etc.
 * 
 * OUT - part_id of matched partition, NULL otherwise
 * returns 1 for error (no match)
 * 
 */
static int _find_best_partition_match(struct job_record* job_ptr, 
		bitstr_t* slurm_part_bitmap, int min_nodes, int max_nodes,
		int spec, bgl_record_t** found_bgl_record)
{
	ListIterator itr;
	bgl_record_t* record;
	int i;
	uint16_t req_geometry[SYSTEM_DIMENSIONS];
	uint16_t conn_type, node_use, rotate, target_size = 1;

	sort_bgl_record_inc_size(bgl_list);

	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_GEOMETRY, req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_NODE_USE, &node_use);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_ROTATE, &rotate);
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		target_size *= req_geometry[i];
	if (target_size == 0)	/* no geometry specified */
		target_size = min_nodes;

	/** this is where we should have the control flow depending on
	    the spec arguement*/
	itr = list_iterator_create(bgl_list);
	*found_bgl_record = NULL;
	/* NEED TO PUT THIS LOGIC IN: 
	 * if RM_NAV, then the partition with both the TORUS and the
	 * dims should be favored over the MESH and the dims, but
	 * foremost is the correct num of dims. 
	 */
	debug("number of partitions to check: %d", list_count(bgl_list));
	while ((record = (bgl_record_t*) list_next(itr))) {
		/*
		 * check that the number of nodes is suitable
		 */
 		if ((record->size < min_nodes)
		||  (max_nodes != 0 && record->size > max_nodes)
		||  (record->size < target_size)) {
			debug("partition %s node count not suitable",
				record->slurm_part_id);
			continue;
		}

		/* Check that configured */
		if (!record->alloc_part) {
			error("warning, bgl_record %s undefined in bluegene.conf",
				record->nodes);
			continue;
		}

		/*
		 * Next we check that this partition's bitmap is within 
		 * the set of nodes which the job can use. 
		 * Nodes not available for the job could be down,
		 * drained, allocated to some other job, or in some 
		 * SLURM partition not available to this job.
		 */
		if (!bit_super_set(record->bitmap, slurm_part_bitmap)) {
			debug("bgl partition %s has nodes not usable by this "
				"job", record->nodes);
			continue;
		}

		/*
		 * Insure that any required nodes are in this BGL partition
		 */
		if (job_ptr->details->req_node_bitmap
		&& (!bit_super_set(job_ptr->details->req_node_bitmap,
				record->bitmap))) {
			info("bgl partition %s lacks required nodes",
				record->nodes);
			continue;
		}

		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		if ((conn_type != record->conn_type)
		&& (conn_type != RM_NAV)) {
			debug("bgl partition %s conn-type not usable", record->nodes);
			continue;
		} 

		/***********************************************/
		/* check the node_use specified matches */
		/***********************************************/
		if (node_use != record->node_use) {
			debug("bgl partition %s node-use not usable", record->nodes);
			continue;
		}

		/*****************************************/
		/** match up geometry as "best" possible */
		/*****************************************/
		if (req_geometry[0] == 0)
			;	/*Geometry not specified */
		else {	/* match requested geometry */
			bool match = false;
			int rot_cnt = 0;	/* attempt six rotations of dimensions */

			for (rot_cnt=0; rot_cnt<6; rot_cnt++) {
				for (i=0; i<SYSTEM_DIMENSIONS; i++) {
					if (record->alloc_part->dimensions[i] <
							req_geometry[i])
						break;
				}
				if (i == SYSTEM_DIMENSIONS) {
					match = true;
					break;
				}
				if (rotate == 0)
					break;		/* not usable */
				_rotate_geo(req_geometry, rot_cnt);
			}

			if (!match) 
				continue;	/* Not usable */
		}

		if ((*found_bgl_record == NULL)
		||  (record->size < (*found_bgl_record)->size)) {
			*found_bgl_record = record;
			if (record->size == target_size)
				break;
		}
	}	
	
	/** set the bitmap and do other allocation activities */
	if (*found_bgl_record) {
		debug("_find_best_partition_match %s <%s>", 
		      (*found_bgl_record)->slurm_part_id, (*found_bgl_record)->nodes);
		bit_and(slurm_part_bitmap, (*found_bgl_record)->bitmap);
		return SLURM_SUCCESS;
	}
	
	debug("_find_best_partition_match none found");
	return SLURM_ERROR;
}

/** 
 * Comparator used for sorting partitions smallest to largest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b)
{
	if (rec_a->size < rec_b->size)
		return -1;
	else if (rec_a->size > rec_b->size)
		return 1;
	else 
		return 0;
}

/** 
 * Comparator used for sorting partitions largest to smallest
 * 
 * returns: -1: rec_a >rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
static int _bgl_record_cmpf_dec(bgl_record_t* rec_a, bgl_record_t* rec_b)
{
	if (rec_a->size > rec_b->size)
		return -1;
	else if (rec_a->size < rec_b->size)
		return 1;
	else 
		return 0;
}

/** 
 * sort the partitions by increasing size
 */
extern void sort_bgl_record_inc_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) _bgl_record_cmpf_inc);
}

/** 
 * sort the partitions by decreasing size
 */
void sort_bgl_record_dec_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) _bgl_record_cmpf_dec);
}

/** 
 * 
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_part_bitmap,
		      int min_nodes, int max_nodes)
{
	int spec = 1; // this will be like, keep TYPE a priority, etc, blah blah.
	bgl_record_t* record;
	char buf[100];

	debug("bluegene::submit_job");

	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
		SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%d-%d", buf, min_nodes, max_nodes);
	
	if (_find_best_partition_match(job_ptr, slurm_part_bitmap, min_nodes, max_nodes, 
				       spec, &record)) {
		return SLURM_ERROR;
	} else {
		/* now we place the part_id into the env of the script to run */
		// FIXME, create a fake bgl part id string
		/* since the bgl_part_id is a number, (most likely single digit), 
		 * we'll create an LLNL_#, i.e. LLNL_4 = 6 chars + 1 for NULL
		 */
		char bgl_part_id[BITSIZE];
#ifdef USE_BGL_FILES
		snprintf(bgl_part_id, BITSIZE, "%s", *record->bgl_part_id);
#else
		snprintf(bgl_part_id, BITSIZE, "LLNL_128_16");
#endif
		debug("found fake bgl_part_id %s", bgl_part_id);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_PART_ID, bgl_part_id);
	}

	/** we should do the BGL stuff here like, init BGL job stuff... */
	return SLURM_SUCCESS;
}

/** 
 * global - bgl: 
 * 
 * hmm, so it seems here we have to parse through the entire list of
 * base partitions to update our system.  so since we have to go
 * through the list anyways, we would like to have instant O(1) access
 * to the nodelist that we need to update.
 */
static void _update_bgl_node_bitmap(void)
{
#ifdef USE_BGL_FILES
	int bp_num,i;
	rm_BP_t *my_bp;
	rm_BP_state_t bp_state;
	rm_location_t bp_loc;
	rm_size3D_t bp_size;
	char down_node_list[BUFSIZE] = "";
	char bgl_down_node[128];
	char *bp_id;

	if (!bgl) {
		error("error, BGL is not initialized");
		return;
	}

	debug("---------rm_get_BGL------------");
	// rm_get_data(bgl,RM_BPsize,&bp_size);
	// rm_get_data(bgl,RM_Msize,&m_size);

	debug("BP Size = (%d x %d x %d)",bp_size.X,bp_size.Y,bp_size.Z);

	rm_get_data(bgl,RM_BPNum,&bp_num);
	debug("- - - - - BPS (%d) - - - - - -",bp_num);

	for (i=0;i<bp_num;i++) {
		if (i==0)
			rm_get_data(bgl,RM_FirstBP,&my_bp);
		else
			rm_get_data(bgl,RM_NextBP,&my_bp);
		
		// is this blocking call?
		rm_get_data(my_bp, RM_BPState, &bp_state);
		rm_get_data(my_bp, RM_BPLoc,   &bp_loc);
		rm_get_data(my_bp, RM_PartitionID, &bp_id);
		/* from here we either update the node or bitmap
		   entry */
		/** translate the location to the "node name" */
		snprintf(bgl_down_node, sizeof(bgl_down_node), "bgl%d%d%d", 
			bp_loc.X, bp_loc.Y, bp_loc.Z);
		debug("update bgl node bitmap: %s loc(%s) is in state %s", 
		      bp_id, bgl_down_node, _convert_bp_state(RM_BPState));
		// convert_partition_state(BPPartState);
		// BPID,_convert_bp_state(BPState),bp_loc.X,bp_loc.Y,
		// bp_loc.Z,BPPartID

		if (RM_BPState == RM_BP_DOWN) {
			/* now we have to convert the BGL BP to a node
			 * that slurm knows about = comma separated
			 * node list
			 */
			if ((strlen(down_node_list) + strlen(bgl_down_node) 
			     +2) < BUFSIZE) {
				if (down_node_list[0] != '\0')
					strcat(down_node_list,",");
				strcat(down_node_list, bgl_down_node);
			} else
				error("down_node_list overflow");
		}
	}

	if (!down_node_list) {
		char reason[128];
		time_t now = time(NULL);
		struct tm * time_ptr = localtime(&now);
		strftime(reason, sizeof(reason), 
			"bluegene_select: RM_BP_DOWN [SLURM @%b %d %H:%M]", 
			time_ptr);
		slurm_drain_nodes(down_node_list, reason);
	}
#endif
}


#ifdef USE_BGL_FILES
/* Convert base partition state value to a string */
static char *_convert_bp_state(rm_BP_state_t state)
{
	switch(state){ 
	case RM_BP_UP:
		return "RM_BP_UP";
		break;
	case RM_BP_DOWN:
		return "RM_BP_DOWN";
		break;
	case RM_BP_NAV:
		return "RM_BP_NAV";
	default:
		return "BP_STATE_UNIDENTIFIED!";
	}
}

/* Set a base partition's state */
static void _set_bp_node_state(rm_BP_state_t state, rm_element_t* element)
{
	/* rm_set_data(element, RM_PartitionState, state) */
	switch(state) { 
	case RM_BP_UP:
		debug("RM_BP_UP");
		break;
	case RM_BP_DOWN:
		debug("RM_BP_DOWN");
		break;
	case RM_BP_NAV:
		debug("RM_BP_NAV");
		break;
	default:
		debug("BGL state update returned UNKNOWN state");
		break;
	}
}
#endif

/*
 * bluegene_agent - detached thread periodically updates status of
 * bluegene nodes. 
 * 
 * NOTE: I don't grab any locks here because slurm_drain_nodes grabs
 * the necessary locks.
 */
extern void *
bluegene_agent(void *args)
{
	struct timeval tv1, tv2;
	char tv_str[20];

	while (1) {
		gettimeofday(&tv1, NULL);
		_update_bgl_node_bitmap();
		gettimeofday(&tv2, NULL);
		_diff_tv_str(&tv1, &tv2, tv_str, 20);
#if _DEBUG
		debug("Bluegene status update: completed, %s", tv_str);
#endif
		sleep(SLEEP_TIME);      /* don't run continuously */
	}
}

/*
 * _diff_tv_str - build a string showing the time difference between two times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 */
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str)
{
	long delta_t;
	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t);
}

/* Rotate a 3-D geometry array through its six permutations */
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
		case 0:		/* ABC -> ACB */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 1:		/* ACB -> CAB */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
		case 2:		/* CAB -> CBA */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 3:		/* CBA -> BCA */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
		case 4:		/* BCA -> BAC */
			SWAP(req_geometry[1], req_geometry[2], tmp);
			break;
		case 5:		/* BAC -> ABC */
			SWAP(req_geometry[0], req_geometry[1], tmp);
			break;
	}
}
