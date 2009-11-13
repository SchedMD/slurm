/*****************************************************************************\
 *  submit-tst.c - exercise the SLURM submit job API
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
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	int error_code, i, count;
	job_desc_msg_t job_mesg;
	submit_response_msg_t *resp_msg;
	char *env[2];
	
	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. contiguous = 1; 
	job_mesg. name = ("job01");
	job_mesg. job_min_procs = 1;
	job_mesg. job_min_memory = 100;
	job_mesg. job_min_tmp_disk = 200;
	job_mesg. priority = 100;
	job_mesg. shared = 0;
	job_mesg. time_limit = 100;
	job_mesg. min_nodes = 1;
	job_mesg. user_id = getuid();
	job_mesg. script = "#!/bin/csh\n/bin/hostname\n";
	job_mesg. err = "/tmp/slurm.stderr";
	job_mesg. in = "/tmp/slurm.stdin";
	job_mesg. out = "/tmp/slurm.stdout";
	job_mesg. work_dir = "/tmp\0";
	job_mesg. env_size = 2;
	env[0] = "SLURM_ENV_0=looking_good";
	env[1] = "SLURM_ENV_1=still_good";
	job_mesg. environment = env;

	error_code = slurm_submit_batch_job( &job_mesg, &resp_msg );
	if (error_code) {
		slurm_perror ("slurm_submit_batch_job");
		return (error_code);
	}
	else {
		printf ("job %u submitted\n", resp_msg->job_id);
		slurm_free_submit_response_response_msg ( resp_msg );
	}

	if (argc > 1) 
		count = atoi (argv[1]);
	else
		count = 1;

	for (i=1; i<count; i++) {
		slurm_init_job_desc_msg( &job_mesg );
		job_mesg. contiguous = 1; 
		job_mesg. name = ("job02+");
		job_mesg. job_min_procs = 1;
		job_mesg. job_min_memory = 100 + i;
		job_mesg. job_min_tmp_disk = 200 + i;
		job_mesg. priority = 100 + i;
		job_mesg. script = "/bin/hostname\n";
		job_mesg. shared = 0;
		job_mesg. time_limit = 100 + i;
		job_mesg. min_nodes = i;
		job_mesg. user_id = getuid();
		error_code = slurm_submit_batch_job( &job_mesg, &resp_msg );
		if (error_code) {
			slurm_perror ("slurm_submit_batch_job");
			break;
		}
		else {
			printf ("job %u submitted\n", resp_msg->job_id);
			slurm_free_submit_response_response_msg ( resp_msg );
		}
	}
	exit (error_code);

}
