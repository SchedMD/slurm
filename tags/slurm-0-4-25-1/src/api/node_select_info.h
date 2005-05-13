/*****************************************************************************\
 *  node_select_info.h - get/free node select plugin state information from
 *	 slurm
 *  NOTE: This header file is not currently exported
 *  NOTE: This software specifically supports only BlueGene/L for now. It 
 *	will be made more general in the future
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#ifndef _NODE_SELECT_INFO_H
#define _NODE_SELECT_INFO_H

#include <stdint.h>
#include <time.h>

typedef struct {
	char *nodes;
	char *owner_name;
	char *bgl_part_id;
	int state;
	int conn_type;
	int node_use;
} bgl_info_record_t;

typedef struct {
	time_t    last_update;
	uint32_t  record_count;
	bgl_info_record_t *bgl_info_array;
} node_select_info_msg_t;

/*
 * slurm_load_node_select - issue RPC to get slurm all node select plugin 
 *      information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_select_info_msg_pptr - place to store a node select configuration 
 *      pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_select_info_msg
 */
extern int slurm_load_node_select (time_t update_time,
                node_select_info_msg_t **node_select_info_msg_pptr);

/*
 * slurm_free_node_select_info_msg - free buffer returned by 
 *	slurm_load_node_select
 * IN node_select_info_msg_pptr - data is freed and pointer is set to NULL
 * RET 0 or a slurm error code
 */
extern int slurm_free_node_select_info_msg (node_select_info_msg_t **
		node_select_info_msg_pptr);

#endif

