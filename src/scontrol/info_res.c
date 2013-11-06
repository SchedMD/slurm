/*****************************************************************************\
 *  info_res.c - reservation information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bremer <dbremer@llnl.gov>
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

/* Load current reservation table information into *res_buffer_pptr */
extern int
scontrol_load_reservations(reserve_info_msg_t **res_buffer_pptr)
{
	int error_code;
	reserve_info_msg_t *res_info_ptr = NULL;

	if (old_res_info_ptr) {
		error_code = slurm_load_reservations (
			old_res_info_ptr->last_update,
			&res_info_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_reservation_info_msg (old_res_info_ptr);

		} else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			res_info_ptr = old_res_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1) {
				printf ("slurm_load_reservations: no change "
					"in data\n");
			}
		}
	}
	else {
		error_code = slurm_load_reservations((time_t) NULL,
						     &res_info_ptr);
	}

	if (error_code == SLURM_SUCCESS) {
		old_res_info_ptr = res_info_ptr;
		*res_buffer_pptr = res_info_ptr;
	}

	return error_code;
}

/*
 * scontrol_print_res - print the specified reservation's information
 * IN reservation_name - NULL to print information about all reservations
 */
extern void
scontrol_print_res (char *reservation_name)
{
	int error_code, i, print_cnt = 0;
	reserve_info_msg_t *res_info_ptr = NULL;
	reserve_info_t *res_ptr = NULL;

	error_code = scontrol_load_reservations(&res_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_reservations error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&res_info_ptr->last_update,
			       time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, res_info_ptr->record_count);
	}

	res_ptr = res_info_ptr->reservation_array;
	for (i = 0; i < res_info_ptr->record_count; i++) {
		if (reservation_name &&
		    strcmp (reservation_name, res_ptr[i].name) != 0)
			continue;
		print_cnt++;
		slurm_print_reservation_info (stdout, & res_ptr[i],
		                              one_liner ) ;
		if (reservation_name)
			break;
	}

	if (print_cnt == 0) {
		if (reservation_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Reservation %s not found\n",
				        reservation_name);
		} else if (quiet_flag != 1)
			printf ("No reservations in the system\n");
	}
}

















