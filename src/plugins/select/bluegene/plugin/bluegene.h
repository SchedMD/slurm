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

#include "bg_record_functions.h"

typedef enum bg_layout_type {
	LAYOUT_STATIC,  /* no overlaps, except for full system block
			   blocks never change */
	LAYOUT_OVERLAP, /* overlaps permitted, must be defined in 
			   bluegene.conf file */
	LAYOUT_DYNAMIC	/* slurm will make all blocks */
} bg_layout_t;


/* Global variables */

extern my_bluegene_t *bg;
#ifdef HAVE_BGL
extern char *default_blrtsimage;
#endif
extern char *default_linuximage;
extern char *default_mloaderimage;
extern char *default_ramdiskimage;
extern char *bridge_api_file;
extern char *bg_slurm_user_name;
extern char *bg_slurm_node_prefix;
extern bg_layout_t bluegene_layout_mode;
extern uint16_t bluegene_numpsets;
extern uint16_t bluegene_bp_node_cnt;
extern uint16_t bluegene_nodecard_node_cnt;
extern uint16_t bluegene_nodecard_ionode_cnt;
extern uint16_t bluegene_quarter_node_cnt;
extern uint16_t bluegene_quarter_ionode_cnt;
extern ba_system_t *ba_system_ptr;
extern time_t last_bg_update;

extern List bg_curr_block_list; 	/* Initial bg block state */
extern List bg_list;			/* List of configured BG blocks */
extern List bg_job_block_list;  	/* jobs running in these blocks */
extern List bg_booted_block_list;  	/* blocks that are booted */
extern List bg_freeing_list;  	        /* blocks that being freed */
#ifdef HAVE_BGL
extern List bg_blrtsimage_list;
#endif
extern List bg_linuximage_list;
extern List bg_mloaderimage_list;
extern List bg_ramdiskimage_list;

extern bool agent_fini;
extern pthread_mutex_t block_state_mutex;
extern pthread_mutex_t request_list_mutex;
extern int num_block_to_free;
extern int num_block_freed;
extern int blocks_are_created;
extern int procs_per_node;
extern int num_unused_cpus;

#define MAX_PTHREAD_RETRIES  1
#define BLOCK_ERROR_STATE    -3
#define NO_JOB_RUNNING       -1
#define MAX_AGENT_COUNT      30
#define BUFSIZE 4096
#define BITSIZE 128

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

extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b);


/* remove all users from a block but what is in user_name */
/* Note return codes */
#define REMOVE_USER_ERR  -1
#define REMOVE_USER_NONE  0
#define REMOVE_USER_FOUND 2
extern int remove_all_users(char *bg_block_id, char *user_name);
extern int set_block_user(bg_record_t *bg_record);

/* Return strings representing blue gene data types */
extern char *convert_conn_type(rm_connection_type_t conn_type);
#ifdef HAVE_BGL
extern char *convert_node_use(rm_partition_mode_t pt);
#endif
/* sort a list of bg_records by size (node count) */
extern void sort_bg_record_inc_size(List records);

/* bluegene_agent - detached thread periodically tests status of bluegene 
 * nodes and switches */
extern void *bluegene_agent(void *args);

extern int bg_free_block(bg_record_t *bg_record);

#ifndef HAVE_BGL
extern int bg_reboot_block(bg_record_t *bg_record);
#endif

extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record);
extern bg_record_t *find_and_remove_org_from_bg_list(List my_list, 
						     bg_record_t *bg_record);
extern bg_record_t *find_org_in_bg_list(List my_list, bg_record_t *bg_record);
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
 
