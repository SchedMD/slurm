/*****************************************************************************\
 *  bluegene.c - blue gene node configuration processing module. 
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
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <slurm/slurm.h>

#include "src/slurmctld/proc_req.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "bgl_job_place.h"
#include "bluegene.h"
#include "partition_sys.h"
#include "state_test.h"

#define BUFSIZE 4096
#define BITSIZE 128
#define DEFAULT_BLUEGENE_SERIAL "BGL"
#define NODE_POLL_TIME 60	/* poll CMCS node state every 60 secs */
#define SWITCH_POLL_TIME 90	/* poll CMCS switch state every 90 secs */

#define _DEBUG 0

char* bgl_conf = BLUEGENE_CONFIG_FILE;
List bgl_conf_list = NULL;              /* list of bgl_conf_record entries */

/* Global variables */
rm_BGL_t *bgl;
List bgl_list = NULL;			/* list of bgl_record entries */
char *bluegene_blrts = NULL, *bluegene_linux = NULL, *bluegene_mloader = NULL;
char *bluegene_ramdisk = NULL, *bluegene_serial = NULL;
bool agent_fini = false;

/* some local functions */
static int  _bgl_record_cmpf_inc(bgl_record_t* rec_a, bgl_record_t* rec_b);
static int  _bgl_record_cmpf_dec(bgl_record_t* rec_a, bgl_record_t* rec_b);
static int  _copy_slurm_partition_list(List slurm_part_list);
static void _destroy_bgl_conf_record(void* object);
static void _destroy_bgl_record(void* object);
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
				char *tv_str, int len_tv_str);
static bgl_conf_record_t* 
            _find_config_by_nodes(char* nodes);
static int  _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes);
static int  _parse_bgl_spec(char *in_line);
static int  _parse_request(char* request_string, partition_t** request);
static void _process_config(void);
static int  _sync_partitions(void);
static int  _validate_config_nodes(void);
static int  _wire_bgl_partitions(void);

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

	if (bgl_list) {
		bgl_record_t *record;
		while ((record = list_pop(bgl_list)))
			_destroy_bgl_record(record);
	} else
		bgl_list = list_create(_destroy_bgl_record);

	/* copy the slurm.conf partition info from slurmctld into bgl_list */
	if ((rc = _copy_slurm_partition_list(part_list)))
		return rc;

	/* syncronize slurm.conf and bluegene.conf data */
	_process_config();

	/* 
	 * After reading in the configuration, we have a list of partition 
	 * requests configurations that we can use to partition up the system. 
	 * We also have the current BGL state information. Sync the two up,
	 * rewire and create partitions as needed.
	 */
	if ((rc = _sync_partitions()))
		return rc;

	return rc;
}

/* Synchronize the actual bluegene partitions to that configured in SLURM */ 
static int _sync_partitions(void)
{
	int rc = SLURM_SUCCESS;

	/* Check if partitions configured in SLURM are already configured on
	 * the system */
	if ((rc = _validate_config_nodes())) {
		/* If not, delete all existing partitions and jobs then
		 * configure from scratch */
		rc = _wire_bgl_partitions();
	}

	return rc;
}

/*
 * Match slurm configuration information with current BGL partition 
 * configuration. Return SLURM_SUCCESS if they match, else an error 
 * code. Writes bgl_partition_id into bgl_list records.
 */
static int  _validate_config_nodes(void)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BGL_FILES
	bgl_record_t* conf_record;	/* records from configuration files */
	bgl_record_t* init_record;	/* records from actual BGL config */
	ListIterator itr_conf, itr_init;
	char nodes[1024];

	/* read current bgl partition info into bgl_init_part_list */
	if ((rc = read_bgl_partitions()))
		return rc;

	itr_conf = list_iterator_create(bgl_list);
	while ((conf_record = (bgl_record_t*) list_next(itr_conf))) {
		/* translate hostlist to ranged string for consistent format */
		(void) hostlist_ranged_string(conf_record->hostlist, 
			sizeof(nodes), nodes);
        	/* search here */
		itr_init = list_iterator_create(bgl_init_part_list);
		while ((init_record = (bgl_record_t*) list_next(itr_init))) {
			if (strcmp(nodes, init_record->nodes))
				continue;	/* wrong nodes */
			if ((conf_record->conn_type != init_record->conn_type)
			||  (conf_record->node_use  != init_record->node_use))
				break;		/* must reconfig this part */
			conf_record->bgl_part_id = xstrdup(init_record->
					bgl_part_id);
			break;
		}
		if (!conf_record->bgl_part_id) {
			info("BGL PartitionID:NONE Nodes %s", nodes);
			rc = EINVAL;
		} else {
			info("BGL PartitionID:%s Nodes:%s Conn:%s Mode:%s",
				conf_record->bgl_part_id, nodes,  
				convert_conn_type(conf_record->conn_type),
				convert_node_use(conf_record->node_use));
		}
		list_iterator_destroy(itr_init);
	}
	list_iterator_destroy(itr_conf);
#endif

	return rc;
}

/* Current blue gene partitions do not match the configuration, 
 * rewire everything and recreate the partitions */
static int _wire_bgl_partitions(void)
{
	int rc = SLURM_SUCCESS;
#ifdef USE_BGL_FILES
/* orignial logic from Dan Phung */
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
#else
	error("FIXME: Add logic to re-wire partitions");
	rc = EINVAL;
#endif
	return rc;
}

/*
 * process the slurm configuration to interpret BGL specific semantics: 
 * if MaxNodes == MinNodes == size (Nodes), = static partition, otherwise
 * creates a List of allocation requests made up of partition_t's (see 
 * partition_sys.h)
 */
static void _process_config(void)
{
	ListIterator itr;
	bgl_record_t *bgl_part;
	partition_t* request_result;

	itr = list_iterator_create(bgl_list);
	while ((bgl_part = (bgl_record_t*) list_next(itr))) {
		/*
		 * _parse_request() will fill up the partition_t's
		 * bl_coord, tr_coord, dimensions, and size
		 */
		request_result = NULL;
		if (_parse_request(bgl_part->nodes, &request_result)
		|| (request_result == NULL))
			error("_process_config: error parsing request %s", 
				bgl_part->nodes);
		else {
			/* 
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

/* 
 * Copy the current partition info that slurmctld read from slurm.conf
 * so that we can maintain our own separate table in bgl_list. Note that
 * read_bgl_conf() has already been executed and read bluegene.conf.
 */
static int _copy_slurm_partition_list(List slurm_part_list)
{
	struct part_record* slurm_part;
	bgl_record_t* bgl_record;
	ListIterator itr;
	char* cur_nodes, *delimiter=",", *nodes_tmp, *next_ptr, *orig_ptr;
	int rc = SLURM_SUCCESS;

	itr = list_iterator_create(slurm_part_list);
	/* 
	 * try to find the corresponding bgl_conf_record for the
	 * nodes specified in the slurm_part_list, but if not
	 * found, _find_conn_type will default to RM_MESH
	 */
	while ((slurm_part = (struct part_record *) list_next(itr))) {
		/* no need to create record for slurm partition without nodes */
		if ((slurm_part->nodes == NULL)
		||  (slurm_part->nodes[0] == '\0'))
			continue;
		nodes_tmp = xstrdup(slurm_part->nodes);
		orig_ptr = nodes_tmp;

		cur_nodes = strtok_r(nodes_tmp, delimiter, &next_ptr);
#if _DEBUG
		debug("_copy_slurm_partition_list parse:%s, token[0]:%s", 
			slurm_part->nodes, cur_nodes);
#endif
		/* 
		 * for each of the slurm partitions, there may be
		 * several bgl partitions, so we need to find how to
		 * wire each of those bluegene partitions.
		 */
		while (cur_nodes != NULL) {
			bgl_conf_record_t *config_ptr;
			config_ptr = _find_config_by_nodes(cur_nodes);
			if (config_ptr == NULL) {
				error("Nodes missing from bluegene.conf: %s", 
					cur_nodes);
				rc = SLURM_ERROR;
				goto cleanup;
			}

			bgl_record = (bgl_record_t*) xmalloc(
					sizeof(bgl_record_t));
			bgl_record->nodes = xstrdup(cur_nodes);
			bgl_record->slurm_part_id = xstrdup(slurm_part->name);

			bgl_record->node_use = config_ptr->node_use;
			bgl_record->conn_type = config_ptr->conn_type;
			bgl_record->hostlist = hostlist_create(cur_nodes);
			bgl_record->size = hostlist_count(bgl_record->hostlist);
			if (node_name2bitmap(cur_nodes, false, 
					&(bgl_record->bitmap))){
				error("_copy_slurm_partition_list unable to "
					"convert nodes %s to bitmap", 
					cur_nodes);
				_destroy_bgl_record(bgl_record);
				rc = SLURM_ERROR;
				goto cleanup;
			}

#if 0	/* Possible future use */
			if ((slurm_part->min_nodes != slurm_part->max_nodes)
			||  (bgl_record->size != slurm_part->max_nodes))
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

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * partitions are static/dynamic, torus/mesh, etc.
 */
extern int read_bgl_conf(void)
{
	DEF_TIMERS;
	FILE *bgl_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code;
	bgl_conf_record_t *conf_rec;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;

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
	START_TIMER;
	/* bgl_conf defined in bgl_node_alloc.h */
	bgl_spec_file = fopen(bgl_conf, "r");
	if (bgl_spec_file == NULL)
		fatal("read_bgl_conf error opening file %s, %m",
		      bgl_conf);

	/* empty the old list before reading new data */
	if (bgl_conf_list) {
		while ((conf_rec = list_pop(bgl_conf_list)))
			_destroy_bgl_conf_record(conf_rec);
	} else
		bgl_conf_list = (List) list_create(_destroy_bgl_conf_record);

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
		
	if (!bluegene_blrts)
		fatal("BlrtsImage not configured in bluegene.conf");
	if (!bluegene_linux)
		fatal("LinuxImage not configured in bluegene.conf");
	if (!bluegene_mloader)
		fatal("MloaderImage not configured in bluegene.conf");
	if (!bluegene_ramdisk)
		fatal("RamDiskImage not configured in bluegene.conf");
	if (!bluegene_serial)
		bluegene_serial = xstrdup(DEFAULT_BLUEGENE_SERIAL);
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
	char *nodes = NULL, *serial = NULL;
	int conn_type = 0;
	int node_use = 0;
	char *blrts_image = NULL,   *linux_image = NULL;
	char *mloader_image = NULL, *ramdisk_image = NULL;
	bgl_conf_record_t* new_record;

	error_code = slurm_parser(in_line,
				"BlrtsImage=", 's', &blrts_image,
				"LinuxImage=", 's', &linux_image,
				"MloaderImage=", 's', &mloader_image,
				"Nodes=", 's', &nodes,
				"RamDiskImage=", 's', &ramdisk_image,
				"Serial=", 's', &serial,
				"Type=", 'd', &conn_type,
				"Use=", 'd', &node_use,
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
	if (serial) {
		xfree(bluegene_serial);
		bluegene_serial = serial;
		serial = NULL;	/* nothing left to xfree */
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

	new_record = (bgl_conf_record_t*) xmalloc(sizeof(bgl_conf_record_t));
	new_record->nodes = nodes;
	nodes = NULL;	/* pointer moved, nothing left to xfree */
	
	if (!conn_type)
		new_record->conn_type = SELECT_MESH;
	else 
		new_record->conn_type = SELECT_TORUS;
	
	if (!node_use)
		new_record->node_use = SELECT_VIRTUAL_NODE_MODE;
	else 
		new_record->node_use = SELECT_COPROCESSOR_MODE;
	
	list_push(bgl_conf_list, new_record);
#if _DEBUG
	debug("_parse_bgl_spec: added nodes=%s type=%s use=%s", 
		new_record->nodes, 
		convert_conn_type(new_record->conn_type), 
		convert_node_use(new_record->node_use));
#endif

  cleanup:
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

/* 
 * search through the list of nodes, types to find the partition 
 * containing the given nodes
 */
static bgl_conf_record_t* _find_config_by_nodes(char* nodes)
{
	return (bgl_conf_record_t*) list_find_first(bgl_conf_list,
				(ListFindF) _listfindf_conf_part_record, 
				nodes);
}

/* Compare node list in bgl_conf_record against node list string */
static int _listfindf_conf_part_record(bgl_conf_record_t* record, char *nodes)
{
	return (!strcasecmp(record->nodes, nodes));
}

/* 
 * Convert a character into its numeric equivalent or -1 on error
 */
static int _char2num(char in)
{
	int i = in - '0';
	if ((i < 0) || (i > 9))
		return -1;
	return i;
}

/*
 * Translate a node list into numeric locations in the BGL node matric
 * IN request_string - node list, must be in the form "bgl[123x456]"
 * OUT request_result - allocated data structure (must be xfreed) that 
 *   notes end-points in a node block
 */
static int _parse_request(char* request_string, partition_t** request_result)
{
	int loc = 0, i,j, rc = SLURM_ERROR;

	if (!request_string) {
		error("_parse_request request_string is NULL");
		return SLURM_ERROR;
	}

	debug3("bluegene config request %s", request_string);
	*request_result = (partition_t*) xmalloc(sizeof(partition_t));

	for (i=0; ; i++) {
		if (request_string[i] == '\0')
			break;
		if (loc == 0) {
			if (request_string[i] == '[')
				loc++;
		} else if (loc == 1) {
			for (j=0; j<SYSTEM_DIMENSIONS; j++) {
				(*request_result)->bl_coord[j] = 
						_char2num(request_string[i+j]);
			}
			i += (SYSTEM_DIMENSIONS - 1);
			loc++;
		} else if (loc == 2) {
			if (request_string[i] != 'x')
				break;
			loc++;
		} else if (loc == 3) {
			for (j=0; j<SYSTEM_DIMENSIONS; j++) {
				(*request_result)->tr_coord[j] = 
						_char2num(request_string[i+j]);
			}
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
		/* error("DIM=%d, loc=%d i=%d", SYSTEM_DIMENSIONS, loc, i); */
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
	rm_size3D_t bp_size;
#endif

	read_bgl_conf();

#ifdef HAVE_BGL_FILES
	rc = rm_set_serial(bluegene_serial);
	if (rc != STATUS_OK){
		fatal("init_bgl: rm_set_serial failed, errno=%d", rc);
		return SLURM_ERROR;
	}
	
	rc = rm_get_BGL(&bgl);
	if (rc != STATUS_OK){
		fatal("init_bgl: rm_get_BGL failed, errno=%d", rc);
		return SLURM_ERROR;
	}

	rc = rm_get_data(bgl, RM_Msize, &bp_size);
	if (rc != STATUS_OK) {
		fatal("init_bgl: rm_get_data failed, errno=%d", rc);
		return SLURM_ERROR;
	}
	verbose("BlueGene configured with %d x %d x %d base partitions",
		bp_size.X, bp_size.Y, bp_size.Z);
#endif

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
	if (bgl_init_part_list) {
		list_destroy(bgl_init_part_list);
		bgl_init_part_list = NULL;
	}

	xfree(bluegene_blrts);
	xfree(bluegene_linux);
	xfree(bluegene_mloader);
	xfree(bluegene_ramdisk);
	xfree(bluegene_serial);

#ifdef USE_BGL_FILES
/* FIXME: rm_free_BGL() is consistenly generating a segfault, even 
 * immediately following a rm_get_BGL() - Jette 11/22/04 */
	if (bgl) {
		rm_free_BGL(bgl);
		bgl = NULL;
	}
#endif
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
		info("\tbgl_part_id: %s", record->bgl_part_id);
	info("\tnodes: %s", record->nodes);
	info("\tsize: %d", record->size);
	info("\tlifecycle: %s", convert_lifecycle(record->part_lifecycle));
	info("\tconn_type: %s", convert_conn_type(record->conn_type));
	info("\tnode_use: %s", convert_node_use(record->node_use));

	if (record->hostlist) {
		char buffer[BUFSIZE];
		hostlist_ranged_string(record->hostlist, BUFSIZE, buffer);
		info("\thostlist %s", buffer);
	}

	if (record->alloc_part) {
		info("\talloc_part:");
		print_partition(record->alloc_part);
	} else {
		info("\talloc_part: NULL");
	}

	if (record->bitmap) {
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

extern char* convert_node_use(rm_partition_mode_t pt)
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

/* 
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
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str)
{
	long delta_t;
	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t);
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

