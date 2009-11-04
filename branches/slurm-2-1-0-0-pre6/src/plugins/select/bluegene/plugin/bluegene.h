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
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

typedef struct {
#ifdef HAVE_BGL
	List blrts_list;
#endif
	uint16_t bp_node_cnt;
	uint16_t bp_nodecard_cnt;
	char *bridge_api_file;
	uint16_t bridge_api_verb;
#ifdef HAVE_BGL
	char *default_blrtsimage;
#endif
	char *default_linuximage;
	char *default_mloaderimage;
	char *default_ramdiskimage;
	uint16_t deny_pass;
	double io_ratio;
	bg_layout_t layout_mode;
	List linux_list;
	List mloader_list;
	double nc_ratio;
	uint16_t nodecard_node_cnt;
	uint16_t nodecard_ionode_cnt;
	uint16_t numpsets;
	uint16_t cpu_ratio;
	uint32_t cpus_per_bp;
	uint16_t quarter_node_cnt;
	uint16_t quarter_ionode_cnt;
	List ramdisk_list;
	char *slurm_user_name;
	char *slurm_node_prefix;
	uint32_t smallest_block;
} bg_config_t;

typedef struct {
	List booted;         /* blocks that are booted */
	List job_running;    /* jobs running in these blocks */
	List freeing;        /* blocks that being freed */
	List main;	    /* List of configured BG blocks */
	List valid_small32;
	List valid_small64;
	List valid_small128;
	List valid_small256;
} bg_lists_t;

/* Global variables */
extern bg_config_t *bg_conf;
extern bg_lists_t *bg_lists;
extern ba_system_t *ba_system_ptr;
extern time_t last_bg_update;
extern bool agent_fini;
extern pthread_mutex_t block_state_mutex;
extern pthread_mutex_t request_list_mutex;
extern int num_block_to_free;
extern int num_block_freed;
extern int blocks_are_created;
extern int num_unused_cpus;

#define MAX_PTHREAD_RETRIES  1
#define BLOCK_ERROR_STATE    -3
#define ADMIN_ERROR_STATE    -4
#define NO_JOB_RUNNING       -1
#define MAX_AGENT_COUNT      30
#define BUFSIZE 4096
#define BITSIZE 128
/* Change BLOCK_STATE_VERSION value when changing the state save
 * format i.e. pack_block() */
#define BLOCK_STATE_VERSION      "VER002"

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

/* sort a list of bg_records by size (node count) */
extern void sort_bg_record_inc_size(List records);

/* block_agent - detached thread periodically tests status of bluegene 
 * blocks */
extern void *block_agent(void *args);

/* state_agent - thread periodically tests status of bluegene 
 * nodes, nodecards, and switches */
extern void *state_agent(void *args);

extern int bg_free_block(bg_record_t *bg_record);

extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record);
extern bg_record_t *find_and_remove_org_from_bg_list(List my_list, 
						     bg_record_t *bg_record);
extern bg_record_t *find_org_in_bg_list(List my_list, bg_record_t *bg_record);
extern void *mult_free_block(void *args);
extern void *mult_destroy_block(void *args);
extern int free_block_list(List delete_list);
extern int read_bg_conf();
extern int validate_current_blocks(char *dir);

/* block_sys.c */
/*****************************************************/
extern int configure_block(bg_record_t * bg_conf_record);
extern int read_bg_blocks();
extern int load_state_file(List curr_block_list, char *dir_name);

/* bg_switch_connections.c */
/*****************************************************/
extern int configure_small_block(bg_record_t *bg_record);
extern int configure_block_switches(bg_record_t * bg_conf_record);

#endif /* _BLUEGENE_H_ */
 
