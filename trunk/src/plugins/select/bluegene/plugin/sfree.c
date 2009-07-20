/*****************************************************************************\
 *  sfree.c - free specified block or all blocks.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "sfree.h"

/* Globals */

int all_blocks = 0;
int remove_blocks = 0;
List block_list = NULL;
bool wait_full = false;

/************
 * Functions *
 ************/
static int _get_new_info_node_select(node_select_info_msg_t **node_select_ptr)
{
	int error_code = SLURM_NO_CHANGE_IN_DATA;
#ifdef HAVE_BG
	static node_select_info_msg_t *bg_info_ptr = NULL;
	static node_select_info_msg_t *new_bg_ptr = NULL;

	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						    &new_bg_ptr);
		if (error_code == SLURM_SUCCESS) {
			node_select_info_msg_free(&bg_info_ptr);
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
						    &new_bg_ptr);
	}

	bg_info_ptr = new_bg_ptr;

	if(*node_select_ptr != bg_info_ptr) 
		error_code = SLURM_SUCCESS;
	
	*node_select_ptr = new_bg_ptr;
#endif
	return error_code;
}

static int _check_status()
{
	ListIterator itr = list_iterator_create(block_list);	
	int i=0;
	node_select_info_msg_t *node_select_ptr = NULL;
	char *block_name = NULL;
	
	while(list_count(block_list)) {
		info("waiting for %d bgblocks to free...",
		     list_count(block_list));
		if(_get_new_info_node_select(&node_select_ptr) 
		   == SLURM_SUCCESS) {
			while((block_name = list_next(itr))) {
				for (i=0; i<node_select_ptr->record_count;
				     i++) {
					if(!strcmp(block_name, 
						   node_select_ptr->
						   bg_info_array[i].
						   bg_block_id)) {
						if(node_select_ptr->
						   bg_info_array[i].
						   state == RM_PARTITION_FREE)
							list_delete_item(itr);
						break;
					}
				}
				/* Here if we didn't find the record
				   it is gone so we just will delete it. */
				if(i >= node_select_ptr->record_count)
					list_delete_item(itr);
			}
			list_iterator_reset(itr);
		}
		sleep(1);
	}
	list_iterator_destroy(itr);
	return SLURM_SUCCESS;
}

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	update_block_msg_t msg;
	ListIterator itr = NULL;
	char *block_name = NULL;
	int rc = SLURM_SUCCESS;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);

	memset(&msg, 0, sizeof(update_block_msg_t));
	if(!all_blocks && (!block_list || !list_count(block_list))) {
		error("you need at least one block to remove.");
		exit(1);
	}

	if(all_blocks) {
		int i=0;
		node_select_info_msg_t *node_select_ptr = NULL;
		_get_new_info_node_select(&node_select_ptr);
		if(block_list)
			list_flush(block_list);
		else
			block_list = list_create(slurm_destroy_char);

		for (i=0; i<node_select_ptr->record_count; i++) {
			list_append(block_list,
				    xstrdup(node_select_ptr->
					    bg_info_array[i].bg_block_id));
		}
	} 

	itr = list_iterator_create(block_list);
	while((block_name = list_next(itr))) {
		msg.state = RM_PARTITION_FREE;
		msg.bg_block_id = block_name;
		rc = slurm_update_block(&msg);
		if(rc != SLURM_SUCCESS)
			error("Error trying to free block %s: %s",
			      block_name, slurm_strerror(rc));
	}
	list_iterator_destroy(itr);
	if(wait_full)
		_check_status();
	list_destroy(block_list);
	info("done");
	return 0;
}
