/*****************************************************************************\
 *  info_block.c - BlueGene block information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "scontrol.h"

/* Load current partiton table information into *part_buffer_pptr */
extern int
scontrol_load_block (block_info_msg_t **block_info_pptr)
{
	int error_code;
	block_info_msg_t *info_ptr = NULL;
	uint16_t show_flags = 0;

	if (all_flag)
		show_flags |= SHOW_ALL;
	if (old_block_info_ptr) {
		error_code = slurm_load_block_info(
			old_block_info_ptr->last_update, &info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_block_info_msg(old_block_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			info_ptr = old_block_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_block no "
					"change in data\n");
		}
	} else
		error_code = slurm_load_block_info((time_t)NULL,
						   &info_ptr, show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_block_info_ptr = info_ptr;
		*block_info_pptr = info_ptr;
	}

	return error_code;
}

/*
 * scontrol_print_block - print the specified block's information
 * IN block_name - NULL to print information about all block
 */
extern void
scontrol_print_block (char *block_name)
{
	int error_code, i, print_cnt = 0;
	block_info_msg_t *block_info_ptr = NULL;
	block_info_t *block_ptr = NULL;

	error_code = scontrol_load_block(&block_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_block error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str(
			(time_t *)&block_info_ptr->last_update,
			time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, block_info_ptr->record_count);
	}

	block_ptr = block_info_ptr->block_array;
	for (i = 0; i < block_info_ptr->record_count; i++) {
		if (block_name
		    && strcmp(block_name, block_ptr[i].bg_block_id))
			continue;
		print_cnt++;
		slurm_print_block_info(
			stdout, &block_ptr[i], one_liner);
		if (block_name)
			break;
	}

	if (print_cnt == 0) {
		if (block_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Block %s not found\n",
				        block_name);
		} else if (quiet_flag != 1)
			printf ("No blocks in the system\n");
	}
}
