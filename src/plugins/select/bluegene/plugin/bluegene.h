/*****************************************************************************\
 *  bluegene.h - header for blue gene configuration processing module. 
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

#ifndef _BLUEGENE_H_
#define _BLUEGENE_H_

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/slurmctld/slurmctld.h"
#include "../block_allocator/block_allocator.h"

typedef int lifecycle_type_t;

enum block_lifecycle {DYNAMIC, STATIC};

typedef enum bg_layout_type {
	LAYOUT_STATIC,  /* no overlaps, except for full system block
			   blocks never change */
	LAYOUT_OVERLAP, /* overlaps permitted, must be defined in 
			   bluegene.conf file */
	LAYOUT_DYNAMIC	/* slurm will make all blocks */
}bg_layout_t;

typedef struct bg_record {
	pm_partition_id_t bg_block_id;	/* ID returned from MMCS	*/
	char *nodes;			/* String of nodes in block */
	char *user_name;		/* user using the block */
	char *target_name;		/* when a block is freed this 
					   is the name of the user we 
					   want on the block */
	int full_block;                 /* wether or not block is the full
					   block */
	uid_t user_uid;   		/* Owner of block uid	*/
	lifecycle_type_t block_lifecycle;/* either STATIC or DYNAMIC	*/
	rm_partition_state_t state;   	/* the allocated block   */
	int start[BA_SYSTEM_DIMENSIONS];/* start node */
	uint16_t geo[BA_SYSTEM_DIMENSIONS];  /* geometry */
	rm_connection_type_t conn_type;	/* Mesh or Torus or NAV */
	rm_partition_mode_t node_use;	/* either COPROCESSOR or VIRTUAL */
	rm_partition_t *bg_block;       /* structure to hold info from db2 */
	List bg_block_list;             /* node list of blocks in block */
	hostlist_t hostlist;		/* expanded form of hosts */
	int bp_count;                   /* size */
	int switch_count;               /* number of switches used. */
	int boot_state;                 /* check to see if boot failed. 
					   -1 = fail, 
					   0 = not booting, 
					   1 = booting */
	int boot_count;                 /* number of attemts boot attempts */
	bitstr_t *bitmap;               /* bitmap to check the name 
					   of block */
	int job_running;                /* job id if there is a job running 
					   on the block */
	int cpus_per_bp;                /* count of cpus per base part */
	uint32_t node_cnt;              /* count of nodes per block */
	uint16_t quarter;               /* used for small blocks 
					   determine quarter of BP */
	uint16_t nodecard;             /* used for small blocks 
					  determine nodecard of quarter */
} bg_record_t;

typedef struct {
	int source;
	int target;
} bg_conn_t;

typedef struct {
	int dim;
	List conn_list;
} bg_switch_t;

typedef struct {
	int *coord;
	int used;
	List switch_list;
} bg_bp_t;


/* Global variables */
extern rm_BGL_t *bg;
extern char *bluegene_blrts;
extern char *bluegene_linux;
extern char *bluegene_mloader;
extern char *bluegene_ramdisk;
extern char *bridge_api_file;
extern bg_layout_t bluegene_layout_mode;
extern uint16_t bluegene_numpsets;
extern uint16_t bluegene_bp_node_cnt;
extern uint16_t bluegene_nodecard_node_cnt;
extern uint16_t bluegene_quarter_node_cnt;
extern ba_system_t *ba_system_ptr;
extern time_t last_bg_update;

extern List bg_curr_block_list; 	/* Initial bg block state */
extern List bg_list;			/* List of configured BG blocks */
extern List bg_job_block_list;  	/* jobs running in these blocks */
extern List bg_booted_block_list;  	/* blocks that are booted */
extern List bg_freeing_list;  	        /* blocks that being freed */
extern List bg_request_list;  	        /* list of request that can't 
					   be made just yet */

extern bool agent_fini;
extern pthread_mutex_t block_state_mutex;
extern pthread_mutex_t request_list_mutex;
extern int num_block_to_free;
extern int num_block_freed;
extern int blocks_are_created;
extern int procs_per_node;
extern int num_unused_cpus;

#define MAX_PTHREAD_RETRIES  1
#define MAX_AGENT_COUNT      30

#include "bg_block_info.h"
#include "bg_job_place.h"
#include "bg_job_run.h"
#include "state_test.h"

/* bluegene.c */
/**********************************************/

/* Initialize all plugin variables */
extern int init_bg(void);

/* Purge all plugin variables */
extern void fini_bg(void);

/* Log a bg_record's contents */
extern void print_bg_record(bg_record_t *record);
extern void destroy_bg_record(void *object);
extern int block_exist_in_list(List my_list, bg_record_t *bg_record);
extern void process_nodes(bg_record_t *bg_record);
extern void copy_bg_record(bg_record_t *fir_record, bg_record_t *sec_record);

/* return bg_record from a bg_list */
extern bg_record_t *find_bg_record_in_list(List my_list, char *bg_block_id);

/* change username of a block bg_record_t target_name needs to be 
   updated before call of function. 
*/
extern int update_block_user(bg_record_t *bg_block_id, int set); 
extern void drain_as_needed(bg_record_t *bg_record, char *reason);
extern int format_node_name(bg_record_t *bg_record, char tmp_char[]);
extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b);


/* remove all users from a block but what is in user_name */
/* Note return codes */
#define REMOVE_USER_ERR  -1
#define REMOVE_USER_NONE  0
#define REMOVE_USER_FOUND 2
extern int remove_all_users(char *bg_block_id, char *user_name);
extern void set_block_user(bg_record_t *bg_record);

/* Return strings representing blue gene data types */
extern char *convert_lifecycle(lifecycle_type_t lifecycle);
extern char *convert_conn_type(rm_connection_type_t conn_type);
extern char *convert_node_use(rm_partition_mode_t pt);

/* sort a list of bg_records by size (node count) */
extern void sort_bg_record_inc_size(List records);

/* bluegene_agent - detached thread periodically tests status of bluegene 
 * nodes and switches */
extern void *bluegene_agent(void *args);

/*
 * create_*_block(s) - functions for creating blocks that will be used
 *   for scheduling.
 * RET - success of fitting all configurations
 */
extern int create_defined_blocks(bg_layout_t overlapped);
extern int create_dynamic_block(ba_request_t *request, List my_block_list);
extern int create_full_system_block(int *block_inx);

extern int bg_free_block(bg_record_t *bg_record);
extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record);
extern void *mult_free_block(void *args);
extern void *mult_destroy_block(void *args);
extern int free_block_list(List delete_list);
extern int read_bg_conf(void);

/* block_sys.c */
/*****************************************************/
extern int configure_block(bg_record_t * bg_conf_record);
extern int read_bg_blocks();

/* bg_switch_connections.c */
/*****************************************************/
extern int configure_small_block(bg_record_t *bg_record);
extern int configure_block_switches(bg_record_t * bg_conf_record);

#endif /* _BLUEGENE_H_ */
 
