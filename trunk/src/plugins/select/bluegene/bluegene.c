/*****************************************************************************\
 *  bluegene.c - bgl node allocation plugin. 
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>
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

#include <stdlib.h>
#include "src/slurmctld/proc_req.h"
#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/common/xstring.h"
#include "bluegene.h"
#include "partition_sys.h"
#include "src/common/hostlist.h"

#define RANGE_MAX 8192
#define BUFSIZE 4096
#define BITSIZE 128
#define SLEEP_TIME 3 /* BLUEGENE_PTHREAD checks every 3 secs */

char* bgl_conf = BLUEGENE_CONFIG_FILE;

 /** some internally used functions */

/** */
int _find_best_partition_match(struct job_record* job_ptr, bitstr_t* slurm_part_bitmap,
			       int min_nodes, int max_nodes,
			       int spec, bgl_record_t** found_bgl_record);
/** */
int _parse_request(char* request_string, partition_t** request);
/** */
int _get_request_dimensions(int* bl, int* tr, uint16_t** dim);
/** */
int _extract_range(char* request, char** result);
/** */
int _wire_bgl_partitions();
/** */
int _bgl_record_cmpf_inc(bgl_record_t* A, bgl_record_t* B);
/** */
int _bgl_record_cmpf_dec(bgl_record_t* A, bgl_record_t* B);
/** 
 * to be used by list object to destroy the array elements
 */
void _destroy_bgl_record(void* object);
/** */
void _destroy_bgl_conf_record(void* object);

/** */
void _print_bitmap(bitstr_t* bitmap);

/** */
void _process_config();
/** */
int _parse_bgl_spec(char *in_line);
/** */
int _copy_slurm_partition_list();
/** */
int _find_part_type(char* nodes, rm_partition_t** return_part_type);
/** */
int _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes);
/** */
int _compute_part_size(char* nodes);
/** */
void _update_bgl_node_bitmap();
/** */
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str);

/**
 * create_static_partitions - create the static partitions that will be used
 * for scheduling.  
 * IN - (global, from slurmctld): the system and desired partition configurations 
 * OUT - (global, to slurmctld): Table of partitionIDs to geometries
 * RET - success of fitting all configurations
 */
int create_static_partitions()
{
	/** purge the old list.  Later on, it may be more efficient just to amend the list */
	if (bgl_list){
		list_destroy(bgl_list);
	} 
	bgl_list = list_create(_destroy_bgl_record);

	/** copy the slurm conf partition info, this will fill in bgl_list */
	if (_copy_slurm_partition_list()){
		return SLURM_ERROR;
	}

	_process_config();
	/* after reading in the configuration, we have a list of partition requests (List <int*>)
	 * that we can use to partition up the system
	 */
	_wire_bgl_partitions();

	
	return SLURM_SUCCESS;
}

/**
 * IN - requests: list of bgl_record(s)
 */
int _wire_bgl_partitions()
{
	bgl_record_t* cur_record;
	partition_t* cur_partition;
	ListIterator itr;
	debug("bluegene::wire_bgl_partitions");

	itr = list_iterator_create(bgl_list);
	while ((cur_record = (bgl_record_t*) list_next(itr))) {
		cur_partition = (partition_t*) cur_record->alloc_part;
		if (configure_switches(cur_partition)){
			error("error on cur_record %s", cur_record->nodes);
		}
	}	
	list_iterator_destroy(itr);

	debug("wire_bgl_partitions done");
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
void _process_config()
{
	ListIterator itr;
	bgl_record_t *bgl_part;
	partition_t* request_result;

	itr = list_iterator_create(bgl_list);
	while ((bgl_part = (bgl_record_t*) list_next(itr))) {
		/** 
		 * parse request will fill up the partition_t's
		 * bl_coord, tr_coord, dimensions, and size
		 */
		request_result = NULL;
		if (_parse_request(bgl_part->nodes, &request_result) || request_result == NULL){
			error("_process_config: error parsing request %s", bgl_part->nodes);
		} else {
			/** 
			 * bgl_part->part_type should have been extracted in
			 * copy_slurm_partition_list
			 */
			request_result->bgl_record_ptr = bgl_part;
			request_result->part_type = (rm_partition_t*) bgl_part->part_type;
			bgl_part->alloc_part = request_result;
		}
	}
	list_iterator_destroy(itr);
}

/* copy the current partition info that was read in from slurm.conf so
 * that we can maintain our own separate table of bgl_part_id to
 * slurm_part_id.
 */
int _copy_slurm_partition_list()
{
	struct part_record* slurm_part;
	bgl_record_t* bgl_record;
	ListIterator itr;
	char* cur_nodes, *delimiter=",", *nodes_tmp, *next_ptr, *orig_ptr;
	int err, rc;

	if (!slurm_part_list){
		error("_copy_slurm_partition_list: slurm_part_list is not initialized yet");
		return SLURM_ERROR;
	}
	itr = list_iterator_create(slurm_part_list);
	/** 
	 * try to find the corresponding bgl_conf_record for the
	 * nodes specified in the slurm_part_list, but if not
	 * found, _find_part_type will default to RM_MESH
	 */
	rc = SLURM_SUCCESS;
	while ((slurm_part = (struct part_record *) list_next(itr))) {
		nodes_tmp = xstrdup(slurm_part->nodes);
		orig_ptr = nodes_tmp;

		cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr);
		/** debugging info */
		/*
		{
			debug("current slurm nodes to parse <%s>", slurm_part->nodes);
			 debug("slurm_part->node_bitmap");
			 _print_bitmap(slurm_part->node_bitmap);
		}
		*/
		// debug("received token");
		// debug("received token <%s>", cur_nodes);
		/** 
		 * for each of the slurm partitions, there may be
		 * several bgl partitions, so we need to find how to
		 * wire each of those bgl partitions.
		 */
		err = 0;
		while(cur_nodes != NULL){
			bgl_record = (bgl_record_t*) xmalloc(sizeof(bgl_record_t));
			if (!bgl_record){
				error("_copy_slurm_partition_list: not enough memory for bgl_record"
				      "for node %s", cur_nodes);
				err = 1;
				goto cleanup_while;
			}

			bgl_record->nodes = xstrdup(cur_nodes);
			bgl_record->slurm_part_id = slurm_part->name;
			bgl_record->part_type = (rm_partition_t*) xmalloc(sizeof(rm_partition_t));
			if (!bgl_record->part_type){
				error("_copy_slurm_partition_list: not enough memory for bgl_record->part_type");
				err = 1;
				goto cleanup_while;
			}

			if (_find_part_type(cur_nodes, &bgl_record->part_type)){
				error("_copy_slurm_partition_list: not enough memory for bgl_record->part_type");
				err = 1;
				goto cleanup_while;
			}

			bgl_record->hostlist = (hostlist_t *) xmalloc(sizeof(hostlist_t));
			*(bgl_record->hostlist) = hostlist_create(cur_nodes);
			bgl_record->size = hostlist_count(*(bgl_record->hostlist));
			if (node_name2bitmap(cur_nodes, false, &(bgl_record->bitmap))){
				error("unable to convert nodes %s to bitmap", cur_nodes);
			}
			
			if (slurm_part->min_nodes == slurm_part->max_nodes && 
			    bgl_record->size == slurm_part->max_nodes)
				bgl_record->part_lifecycle = STATIC;
			else 
				bgl_record->part_lifecycle = DYNAMIC;
			// print_bgl_record(bgl_record);
			list_push(bgl_list, bgl_record);
			
		cleanup_while: 
			nodes_tmp = next_ptr;
			cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr);
			if (err) {
				rc = SLURM_ERROR;
				goto cleanup;
			}

		} /* end while(cur_nodes) */

	cleanup:
		if (orig_ptr){
			xfree(orig_ptr);
			orig_ptr = NULL;
		}
			
	} /* end while(slurm_part) */
	list_iterator_destroy(itr);

	return rc;
}

int read_bgl_conf()
{
	DEF_TIMERS;
	FILE *bgl_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code;

	/* initialization */
	START_TIMER;
	/* bgl_conf defined in bgl_node_alloc.h */
	bgl_spec_file = fopen(bgl_conf, "r");
	if (bgl_spec_file == NULL)
		fatal("read_bgl_conf error opening file %s, %m",
		      bgl_conf);

	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUFSIZE, bgl_spec_file) != NULL) {
		line_num++;
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("_read_bgl_config line %d, of input file %s "
			      "too long", 
			      line_num, bgl_conf);
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
		if ((error_code = _parse_bgl_spec(in_line))) {
			error("_parse_bgl_spec error, skipping this line");

		}
		
		/* report any leftover strings on input line */
		report_leftover(in_line, line_num);
	}
	fclose(bgl_spec_file);

	END_TIMER;
	debug("select_bluegene _read_bgl_conf: finished loading configuration %s",
	      TIME_STR);
	
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
int _parse_bgl_spec(char *in_line)
{
	int error_code = SLURM_SUCCESS;
	char *nodes = NULL, *part_type = NULL;
	bgl_conf_record_t* new_record;

	error_code = slurm_parser(in_line,
				  "Nodes=", 's', &nodes,
				  "Type=", 's', &part_type,
				  "END");

	/** error if you're not specifying nodes or partition type on
	    this line */
	if (error_code || !nodes || !part_type){
		xfree(nodes);
		xfree(part_type);
		nodes = NULL;
		part_type = NULL;
		return error_code;
	}

	// debug("parsed nodes %s", nodes);
	// debug("partition type %s", part_type);

	new_record = (bgl_conf_record_t*) xmalloc(sizeof(bgl_conf_record_t));
	if (!new_record){
		error("_parse_bgl_spec: not enough memory for new_record");
		return SLURM_ERROR;
	}

	new_record->nodes = xstrdup(nodes);
	if (!(new_record->nodes)){
		error("_parse_bgl_spec: not enough memory for new_record nodes string");
		return SLURM_ERROR;		
	}
	new_record->part_type = xmalloc(sizeof(rm_partition_t));
	if (!(new_record->part_type)){
		error("_parse_bgl_spec: not enough memory for new_record part type");
		return SLURM_ERROR;
	}

	if (strcasecmp(part_type, "TORUS") == 0){
		// error("warning, TORUS specified, but I can't handle those yet!  Defaulting to mesh");
		/** FIXME */
		*(new_record->part_type) = RM_TORUS;
		// new_record->part_type = RM_MESH;
	} else if (strcasecmp(part_type, "MESH") == 0) {
		*(new_record->part_type) = RM_MESH;
	} else {
		error("_parse_bgl_spec: partition type %s invalid for nodes %s",
		      part_type, nodes);
		error("defaulting to type: MESH");
		/* error("defaulting to type: PREFER_TORUS"); */
		*(new_record->part_type) = RM_MESH;
	}
	list_push(bgl_conf_list, new_record);
	
	return SLURM_SUCCESS;
}

void _destroy_bgl_record(void* object)
{
	bgl_record_t* this_record = (bgl_record_t*) object;
	if (this_record){
		if (this_record->slurm_part_id){
			xfree(this_record->slurm_part_id);
			this_record->slurm_part_id = NULL;
		}
		if (this_record->nodes){
			xfree(this_record->nodes);
			this_record->nodes = NULL;
		}
		if (this_record->part_type){
			xfree(this_record->part_type);
			this_record->part_type = NULL;
		}
		if (this_record->hostlist){
			hostlist_destroy(*(this_record->hostlist));
			*(this_record->hostlist) = NULL;
		}
		if (this_record->bitmap){
			bit_free(this_record->bitmap);
			this_record->bitmap = NULL;
		}

#ifdef _RM_API_H__
		if (this_record->bgl_part_id){
			xfree(this_record->bgl_part_id);
			this_record->bgl_part_id = NULL;
		}
#endif

	}
	xfree(this_record);
	this_record = NULL;
}

void _destroy_bgl_conf_record(void* object)
{
	bgl_conf_record_t* this_record = (bgl_conf_record_t*) object;
	if (this_record){
		if (this_record->nodes){
			xfree(this_record->nodes);
			this_record->nodes = NULL;
		}
		if (this_record->part_type){
			xfree(this_record->part_type);
			this_record->part_type = NULL;
		}
	}
	xfree(this_record);
	this_record = NULL;
}

/** 
 * search through the list of nodes,types to find the partition type
 * for the given nodes
 */
int _find_part_type(char* nodes, rm_partition_t** return_part_type)
{
	bgl_conf_record_t* record = NULL;

	record = (bgl_conf_record_t*) list_find_first(bgl_conf_list,
						      (ListFindF) _listfindf_conf_part_record, 
						      nodes);

	*return_part_type = (rm_partition_t*) xmalloc(sizeof(rm_partition_t));
	if (!(*return_part_type)) {
		error("_find_part_type: not enough memory for return_part_type");
		return SLURM_ERROR;
	}

	if (record != NULL && record->part_type != NULL){
		**return_part_type = *(record->part_type);
	} else {
		// error("warning: nodes not found in slurm.conf, defaulting to type RM_MESH");
		**return_part_type = RM_MESH;
	}
	
	return SLURM_SUCCESS;
}

/** nodes example: 000x111 */
int _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes)
{
	return (!strcasecmp(record->nodes, nodes));
}

int _compute_part_size(char* nodes) 
{
	int size;
	/* nhosts is stored as int, hopefully unsigned 32-bit */
	hostlist_t hosts = hostlist_create(nodes);
	size = (int) hostlist_count(hosts);
	hostlist_destroy(hosts);
	// debug("compute_part_size for %s = %d", nodes, size);	
	return size;
}

/** 
 * converts a request of form ABCxXYZ to two int*'s
 * of bl[ABC] and tr[XYZ].
 */
int char2intptr(char* request, int** bl, int** tr)
{
	int i, rc;
	char *request_tmp, *delimit = ",x", *next_ptr, *orig_ptr;
	char zero = '0';
	char* token;

	rc = SLURM_ERROR;
	request_tmp = xstrdup(request);
	(*bl) = (int*) xmalloc(sizeof(int) * SYSTEM_DIMENSIONS);
	(*tr) = (int*) xmalloc(sizeof(int) * SYSTEM_DIMENSIONS);

	if (!request_tmp || !bl || !tr){
		error("char2intptr: not enough memory for new structs");
		goto cleanup;
	}

	orig_ptr = request_tmp;
	token = strtok_r(request_tmp, delimit, &next_ptr);
	if (token == NULL)
		goto cleanup;

	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		(*bl)[i] = (int)(token[i]-zero);
	}
	
	request_tmp = next_ptr;
	token = strtok_r(request_tmp, delimit, &next_ptr);
	if (token == NULL)
		goto cleanup;

	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		(*tr)[i] = (int)(token[i]-zero);
	}

	rc = SLURM_SUCCESS;

 cleanup:
	if (rc == SLURM_ERROR){
		xfree(bl);
		xfree(tr);
		bl = NULL; tr = NULL;
	}

	if (orig_ptr != NULL){
		xfree(orig_ptr);
		orig_ptr = NULL;
	}

	return rc;
}

/** 
 * parses the request_string
 */
int _parse_request(char* request_string, partition_t** request_result)
{
	char* range;
	int *bl=NULL, *tr=NULL;
	uint16_t *dim=NULL;
	int i, rc;

	rc = SLURM_ERROR;
	if (!request_string)
		goto cleanup;
	
	debug("incoming request %s", request_string);
	*request_result = (partition_t*) xmalloc(sizeof(partition_t));
	if (!(*request_result)) {
		error("parse_request: not enough memory for request");
		goto cleanup;
	}
	
	/** token needs to be of the form 000x000 */
	if(_extract_range(request_string, &range)){
		goto cleanup;
	}
	
	if (char2intptr(range, &bl, &tr) || bl == NULL || tr == NULL ||
	    _get_request_dimensions(bl, tr, &dim)){
		goto cleanup;
	}

	/** place all the correct values into the request */
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		(*request_result)->bl_coord[i] = bl[i];
		(*request_result)->tr_coord[i] = tr[i];
		(*request_result)->dimensions[i] = dim[i];
	}

	(*request_result)->size = int_array_size(dim);
	rc = SLURM_SUCCESS;
	
 cleanup: 
	if (bl) {
		xfree(bl);
		bl = NULL;
	}
	if (tr) {
		xfree(tr);
		tr = NULL; 
	}
	if (dim) {
		xfree(dim);
		dim = NULL;
	}
	if (range){
		xfree(range);	
		range = NULL;
	}
	if (rc == SLURM_ERROR){
		xfree(request_result);
		request_result = NULL;
	}
	return rc;
}

int _get_request_dimensions(int* bl, int* tr, uint16_t** dim)
{
	int i;
	/*
	  debug("get request dimensions dim: bl[%d %d %d] tr[%d %d %d]", 
	  bl[0], bl[1], bl[2], tr[0], tr[1], tr[2]);
	*/
	if (bl == NULL || tr == NULL){
		return SLURM_ERROR;
	}
		
	*dim = (uint16_t*) xmalloc(sizeof(uint16_t) * SYSTEM_DIMENSIONS);
	if (!(*dim)) {
		error("get_request_dimensions: not enough memory for dim");
		return SLURM_ERROR;
	}
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		(*dim)[i] = tr[i] - bl[i] + 1; /* plus one because we're
						  counting current
						  number, so 0 to 1 = 2
					       */
		if ((*dim)[i] <= 0){
			error("_get_request_dimensions: tr dimension less than bl dimension.");
			goto cleanup;
		}
	}
	// debug("dim: [%d %d %d]", (*dim)[0], (*dim)[1], (*dim)[2]);
	return SLURM_SUCCESS;

 cleanup: 
	xfree(*dim);
	*dim = NULL;
	return SLURM_ERROR;
}

int init_bgl()
{
#ifdef _RM_API_H__
	int rc;
	rc = rm_get_BGL(&bgl);
	if (rc != STATUS_OK){
		error("init_bgl: rm_get_BGL failed");
		return SLURM_ERROR;
	}
#endif
	/** global variable */
	bgl_conf_list = (List) list_create(_destroy_bgl_conf_record);
	init_BGL_PARTITION_NUM();

	return SLURM_SUCCESS;
}

int _extract_range(char* request, char** result)
{
	int RANGE_SIZE = 7; /* expecting something of the size: 000x000 = 7 chars */
	int i, request_length;
	int start = 0, end = 0;

	if (!request)
		return SLURM_ERROR;
	*result = (char*) xmalloc(sizeof(RANGE_SIZE) + 1); /* +1 for NULL term */
	if (!(*result)) {
		error("_extract_range: not enough memory for *result");
		return SLURM_ERROR;
	}

	request_length = strlen(request);
	for(i=0; i<request_length; i++){
		if (request[i] == ']'){
			xstrcatchar(*result, '\0');
			end = 1;
			break;
		}

		if (start){
			xstrcatchar(*result, request[i]);
		}
		
		if (request[i] == '[')
			start = 1;
	}

	if (!start || !end)
		goto cleanup;

	return SLURM_SUCCESS;

 cleanup:
	if (*result){
		xfree(*result);
		*result = NULL;
	}
	error("_extract_range: could not extract range from node list");
	return SLURM_ERROR;
}

void print_bgl_record(bgl_record_t* record)
{
	if (!record){
		error("print_bgl_record, record given is null");
	}

	debug(" bgl_record:");
	debug(" \tslurm_part_id: %s", record->slurm_part_id);
	if (record->bgl_part_id)
		debug(" \tbgl_part_id: %d", *(record->bgl_part_id));
	debug(" \tnodes: %s", record->nodes);
	// debug(" size: %d", record->size);
	debug(" \tlifecycle: %s", convert_lifecycle(record->part_lifecycle));
	debug(" \tpart_type: %s", convert_part_type(record->part_type));

	if (record->hostlist){
		char buffer[RANGE_MAX];
		hostlist_ranged_string(*(record->hostlist), RANGE_MAX, buffer);
		debug(" \thostlist %s", buffer);
	}

	if (record->alloc_part){
		debug(" \talloc_part:");
		print_partition(record->alloc_part);
	} else {
		debug(" \talloc_part: NULL");
	}

	if (record->bitmap){
		char* bitstring = (char*) xmalloc(sizeof(char)*BITSIZE);
		bit_fmt(bitstring, BITSIZE, record->bitmap);
		debug("\tbitmap: %s", bitstring);
		xfree(bitstring);
	}
}

char* convert_lifecycle(lifecycle_type_t lifecycle)
{
	if (lifecycle == DYNAMIC)
		return "DYNAMIC";
	else 
		return "STATIC";
}

char* convert_part_type(rm_partition_t* pt)
{
	switch(*pt) {
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
int _find_best_partition_match(struct job_record* job_ptr, bitstr_t* slurm_part_bitmap,
			       int min_nodes, int max_nodes,
			       int spec, bgl_record_t** found_bgl_record)
{
	/** FIXME, need to get all the partition_t's in a list, or a common data structure
	 * that holds all that info I need!!!
	 */
	ListIterator itr;
	bgl_record_t* record;
	int i, num_dim_best, cur_dim_match;
	uint16_t* geometry = NULL;
	uint16_t req_geometry[SYSTEM_DIMENSIONS];
	uint16_t conn_type, node_use, rotate;
	sort_bgl_record_inc_size(bgl_list);

	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_GEOMETRY, req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_NODE_USE, &node_use);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
		SELECT_DATA_ROTATE, &rotate);

	/** this is where we should have the control flow depending on
	    the spec arguement*/
	num_dim_best = 0;
	itr = list_iterator_create(bgl_list);
	*found_bgl_record = NULL;
	/* NEED TO PUT THIS LOGIC IN: 
	 * if RM_NAV, then the partition with both the TORUS and the
	 * dims should be favored over the MESH and the dims, but
	 * foremost is the correct num of dims. 
	 */
	debug("number of partitions to check: %d", list_count(bgl_list));
	while ((record = (bgl_record_t*) list_next(itr))) {
		debug("- - - - - - - - - - - - -");
		debug("check partition <%s>", record->slurm_part_id);
		debug("- - - - - - - - - - - - -");
		if (!record){
			error("FIXME: well, bad bad bad..."); 
			continue;
		}
		/** 
		 * first we check against the bitmap to see 
		 * if this partition can be used for this job.
		 * 		 
		 * the slurm partition bitmap is a superset of the bgl part bitmap
		 * 
		 * - if we AND the incoming slurm bitmap with the bgl
		 * bitmap, and the bgl bitmap is different that should
		 * mean that some nodes in the slurm bitmap have been
		 * "drained" or set otherwise unusable.
		 */
		// debug("- - - - - - - - - - - - -");
		// debug("check partition bitmap");
		// _print_bitmap(record->bitmap);
		if (!bit_super_set(record->bitmap, slurm_part_bitmap)){
			debug("bgl partition %s unusable", record->nodes);
			continue;
		}
		// debug("- - - - - - - - - - - - -");

		/*******************************************/
		/** check that the number of nodes match   */
		/*******************************************/
		// debug("nodes num match: max %d min %d record_num_nodes %d",
		// max_nodes, min_nodes, record->size);
 		if (record->size < min_nodes || (max_nodes != 0 && record->size > max_nodes)){
			error("debug request num nodes doesn't fit"); 
			continue;
		}
		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		// debug("part type match %s ? %s", convert_part_type(&job_ptr->type), 
		// convert_part_type(record->part_type));
		if (!record->part_type){
			error("find_best_partition_match record->part_type is NULL"); 
			continue;
		}
		debug("conn_type %d", conn_type);
		if (conn_type != *(record->part_type) &&
		    conn_type != RM_NAV){
			continue;
		} 
		/*****************************************/
		/** match up geometry as "best" possible */
		/*****************************************/
		if (req_geometry[0] == 0){
			debug("find_best_partitionmatch: we don't care about geometry");
			*found_bgl_record = record;
			break;
		}
		if (rotate)
			rotate_part(req_geometry, &geometry); 
		
		cur_dim_match = 0;
		for (i=0; i<SYSTEM_DIMENSIONS; i++){
			if (!record->alloc_part) {
				error("warning, bgl_record %s has not found a home...",
				      record->nodes);
				continue;
			}
			
			/**
			 * we should distinguish between an exact match and a
			 * fuzzy match (being greater than
			 */
			if (record->alloc_part->dimensions[i] >= req_geometry[i]){
				cur_dim_match++;
			}
		}
		
		if (cur_dim_match > num_dim_best){
			*found_bgl_record = record;
			num_dim_best = cur_dim_match;
			if (num_dim_best == SYSTEM_DIMENSIONS)
					break;
		}
	}	
	
	/** set the bitmap and do other allocation activities */
	if (*found_bgl_record){
		debug("phung: SUCCESS! found partition %s <%s>", 
		      (*found_bgl_record)->slurm_part_id, (*found_bgl_record)->nodes);
		bit_and(slurm_part_bitmap, (*found_bgl_record)->bitmap);
		debug("- - - - - - - - - - - - -");
		return SLURM_SUCCESS;
	}
	
	debug("phung: FAILURE! no bgl record found");
	return SLURM_ERROR;
}

/** 
 * Comparator used for sorting partitions smallest to largest
 * 
 * returns: -1: A greater than B 0: A equal to B 1: A less than B
 * 
 */
int _bgl_record_cmpf_inc(bgl_record_t* A, bgl_record_t* B)
{
	if (A->size < B->size)
		return -1;
	else if (A->size > B->size)
		return 1;
	else 
		return 0;
}

/** 
 * Comparator used for sorting partitions largest to smallest
 * 
 * returns: -1: A greater than B 0: A equal to B 1: A less than B
 * 
 */
int _bgl_record_cmpf_dec(bgl_record_t* A, bgl_record_t* B)
{
	if (A->size > B->size)
		return -1;
	else if (A->size < B->size)
		return 1;
	else 
		return 0;
}

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
void sort_bgl_record_dec_size(List records){
	if (records == NULL)
		return;
	list_sort(records, (ListCmpF) _bgl_record_cmpf_dec);
}

/** 
 * 
 */
int submit_job(struct job_record *job_ptr, bitstr_t *slurm_part_bitmap,
		      int min_nodes, int max_nodes)
{
	int spec = 1; // this will be like, keep TYPE a priority, etc, blah blah.
	bgl_record_t* record;
	char buf[100];

	debug("bluegene::submit_job");
	/*
	itr = list_iterator_create(bgl_list);
	while ((record = (bgl_record_t*) list_next(itr))) {
		print_bgl_record(record);
	}
	*/
	debug("******** job request ********");
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
		SELECT_PRINT_MIXED);
	debug("%s", buf);
	debug("min_nodes:\t%d", min_nodes);
	debug("max_nodes:\t%d", max_nodes);
	_print_bitmap(slurm_part_bitmap);
	
	if (_find_best_partition_match(job_ptr, slurm_part_bitmap, min_nodes, max_nodes, 
				       spec, &record)){
		return SLURM_ERROR;
	} else {
		/* now we place the part_id into the env of the script to run */
		// FIXME, create a fake bgl part id string
		/* since the bgl_part_id is a number, (most likely single digit), 
		 * we'll create an LLNL_#, i.e. LLNL_4 = 6 chars + 1 for NULL
		 */
		char *bgl_part_id = NULL;
		xstrfmtcat(bgl_part_id, "LLNL_%i", *(record->bgl_part_id));
		debug("found fake bgl_part_id %s", bgl_part_id);
		select_g_set_jobinfo(job_ptr->select_jobinfo,
			SELECT_DATA_PART_ID, bgl_part_id);
		xfree(bgl_part_id);
	}

	/** we should do the BGL stuff here like, init BGL job stuff... */
	debug("return slurm partition bitmap");
	_print_bitmap(slurm_part_bitmap);
	return SLURM_SUCCESS;
}

/** 
 * for my debugging purposes, I occasionally print out the bitmap
 */
void _print_bitmap(bitstr_t* bitmap)
{
	char* bitstring = (char*) xmalloc(sizeof(char)*BITSIZE);
	bit_fmt(bitstring, BITSIZE, bitmap);
	debug("bitmap:\t%s", bitstring);
	xfree(bitstring);
	bitstring = NULL;
}

/** 
 * global - bgl: 
 * 
 * hmm, so it seems here we have to parse through the entire list of
 * base partitions to update our system.  so since we have to go
 * through the list anyways, we would like to have instant O(1) access
 * to the nodelist that we need to update.
 */
void _update_bgl_node_bitmap()
{
#ifdef _RM_API_H__
	int bp_num,wire_num,switch_num,i;
	rm_BP_t *my_bp;
	rm_switch_t *my_switch;
	rm_wire_t *my_wire;
	rm_BP_state_t bp_state;
	rm_location_t BPLoc;
	// rm_size3D_t bp_size,size_in_bp,m_size;
	// rm_size3D_t bp_size,size_in_bp,m_size;
	char* reason = NULL;
	char* down_node_list = NULL;
	char* bgl_down_node = NULL;
	char* slurm_down_node = NULL;

	bgl_down_node = xmalloc(sizeof(char) * BUFSIZE);
	slurm_down_node = xmalloc(sizeof(char) * BUFSIZE);
	/** FIXME: dunno if we have to do this, but i'm reserving mem just to be safe */
	down_node_list = xmalloc(sizeof(char) * BUFSIZE);
	down_node_list[0] = '\0';

	if (!bgl){
		error("error, BGL is not initialized");
	}

	debug("---------rm_get_BGL------------");
	// rm_get_data(bgl,RM_BPsize,&bp_size);
	// rm_get_data(bgl,RM_Msize,&m_size);

	debug("BP Size = (%d x %d x %d)",bp_size.X,bp_size.Y,bp_size.Z);

	rm_get_data(bgl,RM_BPNum,&bp_num);
	debug("- - - - - BPS (%d) - - - - - -",bp_num);

	for(i=0;i<bp_num;i++){
		if(i==0)
			rm_get_data(bgl,RM_FirstBP,&my_bp);
		else
			rm_get_data(bgl,RM_NextBP,&my_bp);
		
		// is this blocking call?
		rm_get_data(my_bp,RM_BPState,&bp_state);
		rm_get_data(my_bp,RM_BPLoc,&BPLoc);
		/* from here we either update the node or bitmap
		   entry */
		/** translate the location to the "node name" */
		xstrfmtcat(bgl_down_node, "bgl%d%d%d", BPLoc.X, BPLoc.Y, BPLoc.Z);
		debug("update bgl node bitmap: %s loc(%s) is in state %s", 
		      BPID, 
		      bgl_down_node,
		      convert_bp_state(RM_BPState));
		// convert_partition_state(BPPartState);
		// BPID,convert_bp_state(BPState),BPLoc.X,BPLoc.Y,BPLoc.Z,BPPartID

		if (RM_BPState == RM_BP_DOWN){
			/* now we have to convert the BGL BP to a node
			 * that slurm knows about = comma separated
			 * node list
			 */
			if (done_node_list[0] != '\0')
				xstrcat(down_node_list,",");
			xstrcat(down_node_list, bgl_down_node);
		}
	}
	if (!down_node_list){
		reason = xstrdup("bluegene_select: RM_BP_DOWN");
		slurm_drain_nodes(down_node_list, reason);
		xfree(reason);
	}

 cleanup:
	xfree(bgl_down_node);
	xfree(slurm_down_node);
	xfree(down_node_list);
	  
#endif
}


#ifdef _RM_API_H__
/** */
char *convert_bp_state(rm_BP_state_t state){
	switch(state){ 
	case RM_BP_UP:
		return "RM_BP_UP";
		break;
	case RM_BP_DOWN:
		return "RM_BP_DOWN";
		break;
	case RM_BP_NAV:
		return "RM_BP_NAV";
	defalt:
		return "BP_STATE_UNIDENTIFIED!";
	}
};

/** */
void set_bp_node_state(rm_BP_state_t state, node_record node){
	switch(state){ 
	case RM_BP_UP:
		debug("RM_BP_UP");
		break;
	case RM_BP_DOWN:
		debug("RM_BP_DOWN");
		break;
	case RM_BP_NAV:
		debug("BGL state update returned UNKNOWN state");
		break;
	defalt:
		debug("BGL state update returned UNKNOWN state");
		break;
	}
};
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
		sleep(SLEEP_TIME);      /* don't run continuously */

		gettimeofday(&tv1, NULL);
		_update_bgl_node_bitmap();

		gettimeofday(&tv2, NULL);
		_diff_tv_str(&tv1, &tv2, tv_str, 20);
#if DEBUG
		info("Bluegene status update: completed, %s", tv_str);
#endif
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
