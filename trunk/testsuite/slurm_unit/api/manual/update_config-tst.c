/*****************************************************************************\
 *  update_config-tst.c - exercise the SLURM update configuration API
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et.al.
 *  LLNL-CODE-402394.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

#ifndef true
#define false 0
#define true  1
#endif

#define NAME_LEN 128
static int _getnodename (char *name, size_t len);

/* main is used here for module testing purposes only */
/* DO NOT RUN AGAINST PRODUCTION NODES, IT CAN MESS UP STATE */
int
main (int argc, char *argv[]) {
	int error_code;
	update_part_msg_t	part_update1 ;
	update_part_msg_t	part_update2 ;
	update_node_msg_t	node_update1 ;
	update_node_msg_t	node_update2 ;
	char node_name[NAME_LEN];

	slurm_init_part_desc_msg ( &part_update1 );
	slurm_init_part_desc_msg ( &part_update2 );
	part_update1 . name = "batch" ;
	part_update2 . name = "batch" ;
	part_update1 . state_up = false ;
	part_update2 . state_up = true ; 

	_getnodename(node_name, NAME_LEN);
	node_update1 . node_names = node_name ;
	node_update2 . node_names = node_name ;
	node_update1 . node_state = NODE_STATE_DRAIN ;
	node_update2 . node_state = NODE_RESUME ; 

	error_code = slurm_update_partition ( &part_update1);
	if (error_code)
		slurm_perror ("slurm_update_partition #1");

	error_code = slurm_update_partition ( &part_update2);
	if (error_code)
		slurm_perror ("slurm_update_partition #2");

	error_code = slurm_update_node ( &node_update1);
	if (error_code)
		slurm_perror ("slurm_update_node #1");

	error_code = slurm_update_node ( &node_update2);
	if (error_code)
		slurm_perror ("slurm_update_node #2");

	return (errno);
}


/* getnodename - equivalent to gethostname, but return only the first 
 * component of the fully qualified name 
 * (e.g. "linux123.foo.bar" becomes "linux123") 
 */
static int _getnodename (char *name, size_t len)
{
	int error_code, name_len;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return error_code;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr == NULL)
		dot_ptr = path_name + strlen(path_name);
	else
		dot_ptr[0] = '\0';

	name_len = (dot_ptr - path_name);
	if (name_len > len)
		return ENAMETOOLONG;

	strcpy (name, path_name);
	return 0;
}
