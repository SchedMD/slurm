/*****************************************************************************\
 *  node_info-tst.c - exercise the SLURM node information API
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et.al.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	node_info_msg_t * node_info_msg_ptr = NULL;

	error_code = slurm_load_node (last_update_time, &node_info_msg_ptr, 1);
	if (error_code) {
		slurm_perror ("last_update_time");
		return (error_code);
	}

	printf("Nodes updated at %ld, record count %d\n",
		(long)node_info_msg_ptr ->last_update, 
		node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) 
	{
		/* to limit output we print only the first 10 entries, 
		 * last 1 entry, and every 200th entry */
		if ((i < 10) || (i % 200 == 0) || 
		    ((i + 1)  == node_info_msg_ptr-> record_count)) {
			slurm_print_node_table ( stdout, & node_info_msg_ptr ->
							 node_array[i], 0 ) ;
		}
		else if ((i==10) || (i % 200 == 1))
			printf ("skipping...\n");
	}

	slurm_free_node_info_msg ( node_info_msg_ptr ) ;
	return (0);
}
