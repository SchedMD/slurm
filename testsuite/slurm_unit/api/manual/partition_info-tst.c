/*****************************************************************************\
 *  partition_info-tst.c - exercise the Slurm partition_info API
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et.al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code ;
	partition_info_msg_t * part_info_ptr = NULL;

	error_code = slurm_load_partitions (last_update_time, &part_info_ptr, 1);
	if (error_code) {
		slurm_perror ("slurm_load_partitions");
		return (error_code);
	}

	note("Updated at %ld, record count %d\n",
		(time_t) part_info_ptr->last_update, 
		part_info_ptr->record_count);

	slurm_print_partition_info_msg (stdout, part_info_ptr, 0);
	slurm_free_partition_info_msg (part_info_ptr);
	return (0);
}
