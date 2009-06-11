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

#ifndef _NODE_SELECT_INFO_H
#define _NODE_SELECT_INFO_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "src/common/pack.h"

typedef struct {
	char *bg_block_id;
	char *blrtsimage;       /* BlrtsImage for this block */
	int *bp_inx;            /* list index pairs into node_table for *nodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	int conn_type;
	char *ionodes;
	int *ionode_inx;        /* list index pairs for ionodes in the
				 * node listed for *ionodes:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	int job_running;
	char *linuximage;       /* LinuxImage for this block */
	char *mloaderimage;     /* mloaderImage for this block */
	char *nodes;
	int node_cnt;
	int node_use;
	char *owner_name;
	char *ramdiskimage;     /* RamDiskImage for this block */
	int state;
} bg_info_record_t;

typedef struct {
	bg_info_record_t *bg_info_array;
	time_t    last_update;
	uint32_t  record_count;
} node_select_info_msg_t;

/*
 * slurm_print_node_select_info_msg - output information about all Bluegene 
 *	blocks based upon message as loaded using slurm_load_node_select
 * IN out - file to write to
 * IN info_ptr - node_select information message pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_node_select_info_msg(
	FILE *out, node_select_info_msg_t *info_ptr, int one_liner);

/*
 * slurm_print_node_select_info - output information about a specific Bluegene 
 *	block based upon message as loaded using slurm_load_node_select
 * IN out - file to write to
 * IN bg_info_ptr - an individual block information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_node_select_info(
	FILE *out, bg_info_record_t *bg_info_ptr, int one_liner);

/*
 * slurm_sprint_node_select_info - output information about a specific Bluegene 
 *	block based upon message as loaded using slurm_load_node_select
 * IN bg_info_ptr - an individual partition information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_node_select_info(
	bg_info_record_t * bg_info_ptr, int one_liner);

/*
 * slurm_load_node_select - issue RPC to get slurm all node select plugin 
 *      information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN node_select_info_msg_pptr - place to store a node select configuration 
 *      pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_select_info_msg
 */
extern int slurm_load_node_select(
	time_t update_time,
	node_select_info_msg_t **node_select_info_msg_pptr);
/*
 * node_select_info_msg_free - free buffer returned by 
 *	slurm_load_node_select
 * IN node_select_info_msg_pptr - data is freed and pointer is set to NULL
 * RET 0 or a slurm error code
 */
extern int slurm_free_node_select(
	node_select_info_msg_t **node_select_info_msg_pptr);

/* Unpack node select info from a buffer */
extern int slurm_unpack_node_select(
	node_select_info_msg_t **node_select_info_msg_pptr, Buf buffer);

#endif

