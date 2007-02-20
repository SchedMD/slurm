/*****************************************************************************\
 *  security_2_2.c - test that job's user id is validated.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et.al.
 *  UCRL-CODE-217948.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

/* This functions are not defined in slurm/slurm.h for external use.
 * They are used internal security testing only. */
extern char *slurm_get_auth_type(void);
extern int slurm_set_auth_type(char *auth_type);

typedef void * Buf;

/* Attempt to run a job without a credential */
int 
main (int argc, char *argv[])
{
	int error_code;
	job_desc_msg_t job_mesg;
	resource_allocation_and_run_response_msg_t* run_resp_msg ;

	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. user_id	= getuid();
	job_mesg. min_nodes	= 1;

	printf("Changing command's authtype from %s to ",
		slurm_get_auth_type());
	slurm_set_auth_type("auth/dummy"); 
	printf("%s\n",slurm_get_auth_type());

	error_code = slurm_allocate_resources_and_run ( &job_mesg , 
							&run_resp_msg ); 
	if (error_code == SLURM_SUCCESS) {
		fprintf (stderr, "ERROR: The allocate succeeded\n");
		exit(1);
	} else if (slurm_get_errno() != SLURM_PROTOCOL_AUTHENTICATION_ERROR) {
		fprintf (stderr, "ERROR: The allocation failed for some "
			 "reason other then authentication\n");
		fprintf (stderr, "Error message was: %s\n",
			 slurm_strerror(slurm_get_errno()));
	} else {
		printf ("SUCCESS!\n");
		printf ("The allocate request was rejected as expected.\n");
		printf ("Check SlurmctldLog for an error message.\n");
		printf ("Error returned from API: %s\n", 
			slurm_strerror(slurm_get_errno()));
		exit(0);
	}
}
