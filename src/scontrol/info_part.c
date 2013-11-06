/*****************************************************************************\
 *  info_part.c - partition information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
scontrol_load_partitions (partition_info_msg_t **part_buffer_pptr)
{
	int error_code;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	partition_info_msg_t *part_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (old_part_info_ptr) {
		if (last_show_flags != show_flags)
			old_part_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_partitions (
			old_part_info_ptr->last_update,
			&part_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg (old_part_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			part_info_ptr = old_part_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_part no change in data\n");
		}
	}
	else
		error_code = slurm_load_partitions((time_t) NULL,
						   &part_info_ptr, show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_part_info_ptr = part_info_ptr;
		last_show_flags = show_flags;
		*part_buffer_pptr = part_info_ptr;
	}

	return error_code;
}

/*
 * scontrol_print_part - print the specified partition's information
 * IN partition_name - NULL to print information about all partition
 */
extern void
scontrol_print_part (char *partition_name)
{
	int error_code, i, print_cnt = 0;
	partition_info_msg_t *part_info_ptr = NULL;
	partition_info_t *part_ptr = NULL;

	error_code = scontrol_load_partitions(&part_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_partitions error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&part_info_ptr->last_update,
			       time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, part_info_ptr->record_count);
	}

	part_ptr = part_info_ptr->partition_array;
	for (i = 0; i < part_info_ptr->record_count; i++) {
		if (partition_name &&
		    strcmp (partition_name, part_ptr[i].name) != 0)
			continue;
		print_cnt++;
		slurm_print_partition_info (stdout, & part_ptr[i],
		                            one_liner ) ;
		if (partition_name)
			break;
	}

	if (print_cnt == 0) {
		if (partition_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Partition %s not found\n",
				        partition_name);
		} else if (quiet_flag != 1)
			printf ("No partitions in the system\n");
	}
}
