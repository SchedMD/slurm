/*****************************************************************************\
 * squeue - Report jobs in the system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <src/api/slurm.h>
#include <src/common/hostlist.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/xmalloc.h>

static char *command_name;
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */

void print_job (char * job_id_str);
void usage ();

int 
main (int argc, char *argv[]) 
{
	int i;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	command_name = argv[0];
	quiet_flag = 0;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (argc > 1) {
		for ( i = 1; i < argc; i++ ) {
			print_job (argv[i]);
		}
	}
	else
		print_job (NULL);

	exit (0);
}


/*
 * print_job - print the specified job's information
 * input: job_id - job's id or NULL to print information about all jobs
 */
void 
print_job (char * job_id_str) 
{
	int i, print_cnt = 0;
	uint32_t job_id = 0;
	static job_info_msg_t * job_buffer_ptr = NULL;
	job_table_t *job_ptr = NULL;

	if (job_buffer_ptr == NULL) {
		if ( (slurm_load_jobs ((time_t) NULL, &job_buffer_ptr) ) ) {
			if (quiet_flag != 1)
				slurm_perror ("slurm_load_jobs error:");
			return;
		}
	}
	
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) job_buffer_ptr->last_update);

	if (job_id_str)
		job_id = (uint32_t) strtol (job_id_str, (char **)NULL, 10);

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_id_str && job_id != job_ptr[i].job_id) 
			continue;
		print_cnt++;
		slurm_print_job_table (stdout, & job_ptr[i] ) ;
		if (job_id_str)
			break;
	}

	if ((print_cnt == 0) && (quiet_flag != 1)) {
		if (job_buffer_ptr->record_count)
			printf ("Job %u not found\n", job_id);
		else
			printf ("No jobs in the system\n");
	}
}


/* usage - show the valid squeue commands */
void
usage () {
	printf ("sqeueue [OPTIONS ...]\n");
}
