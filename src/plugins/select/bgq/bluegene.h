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

#include "bg_structs.h"
#include "bg_record_functions.h"
#include "block_allocator/block_allocator.h"

/* Change BLOCK_STATE_VERSION value when changing the state save
 * format i.e. pack_block() */
#define BLOCK_STATE_VERSION      "VER001"

#include "bg_job_place.h"
#include "bg_job_run.h"
#include "jobinfo.h"
#include "nodeinfo.h"

/* bluegene.c */
/**********************************************/

/* Initialize all plugin variables */
extern int init_bg(void);

/* Purge all plugin variables */
extern void fini_bg(void);

extern bool blocks_overlap(bg_record_t *rec_a, bg_record_t *rec_b);

extern void bg_requeue_job(uint32_t job_id, bool wait_for_start);

/* remove all users from a block but what is in user_name */
/* Note return codes */
extern int remove_all_users(bg_record_t *bg_record, char *user_name);
extern int set_block_user(bg_record_t *bg_record);

/* sort a list of bg_records by size (node count) */
extern void sort_bg_record_inc_size(List records);

/* block_agent - detached thread periodically tests status of bluegene
 * blocks */
extern void *block_agent(void *args);

/* state_agent - thread periodically tests status of bluegene
 * nodes, nodecards, and switches */
extern void *state_agent(void *args);

extern int bg_free_block(bg_record_t *bg_record, bool wait, bool locked);

extern int remove_from_bg_list(List my_bg_list, bg_record_t *bg_record);
extern bg_record_t *find_and_remove_org_from_bg_list(List my_list,
						     bg_record_t *bg_record);
extern bg_record_t *find_org_in_bg_list(List my_list, bg_record_t *bg_record);
extern void *mult_free_block(void *args);
extern void *mult_destroy_block(void *args);
extern int free_block_list(uint32_t job_id, List track_list,
			   bool destroy, bool wait);
extern int read_bg_conf();
extern int validate_current_blocks(char *dir);
extern int node_already_down(char *node_name);

/* block_sys.c */
/*****************************************************/
extern int configure_block(bg_record_t * bg_conf_record);
extern int read_bg_blocks();
extern int load_state_file(List curr_block_list, char *dir_name);

/* bg_switch_connections.c */
/*****************************************************/
extern int configure_small_block(bg_record_t *bg_record);
extern int configure_block_switches(bg_record_t * bg_conf_record);


/* select_bluegene.c */
/*****************************************************/
extern int select_p_update_block(update_block_msg_t *block_desc_ptr);

#endif /* _BLUEGENE_H_ */

