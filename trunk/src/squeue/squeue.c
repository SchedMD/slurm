/*****************************************************************************\
 *  squeue.c - Report jobs in the slurm system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
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

#include <src/squeue/squeue.h>

static char *command_name;
struct squeue_parameters params;
int quiet_flag=0;
void print_job (char * job_id_str);
void print_job_steps( uint32_t job_id, uint16_t step_id );
void usage ();

int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	command_name = argv[0];
	quiet_flag = 0;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line( argc, argv );
	
	while (1) 
	{
		if ( params.step_flag )
			print_job_steps( 0, 0 );
		else 
			print_job( NULL );

		if ( params.iterate ) {
			printf( "\n");
			sleep( params.iterate );
		}
		else
			break;
	}

	exit (0);
}


/*
 * print_job - print the specified job's information
 * input: job_id - job's id or NULL to print information about all jobs
 */
void 
print_job (char * job_id_str) 
{
	uint32_t job_id = 0;
	static job_info_msg_t * job_buffer_ptr = NULL;

	List format = list_create( NULL );
	job_format_add_job_id( format, 12, false );	
	job_format_add_name( format, 10, true );	
	job_format_add_user_name( format, 8, false );	
	job_format_add_job_state_compact( format, 3, false );	
	job_format_add_time_limit( format, 10, false );	
	job_format_add_start_time( format, 12, false );	
	job_format_add_end_time( format, 12, false);	
	job_format_add_priority( format, 6, false );	
	job_format_add_nodes( format, 16, false );	

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

	print_jobs_array( job_buffer_ptr->job_array, job_buffer_ptr->record_count , format) ;

}


void
print_job_steps( uint32_t job_id, uint16_t step_id )
{
	int rc = SLURM_SUCCESS;
	static job_step_info_response_msg_t * step_msg = NULL;


	List format = list_create( NULL );

	step_format_add_id( format, 18, true );
	step_format_add_user_id( format, 8, true );
	step_format_add_start_time( format, 12, true );
	step_format_add_nodes( format, 20, true );
	
	if ( ( rc = slurm_get_job_steps( (time_t) NULL, job_id, step_id, &step_msg ) ) != SLURM_SUCCESS )
	{
		slurm_perror( "slurm_get_job_steps failed");
		return;
	}

	print_steps_array( step_msg->job_steps ,step_msg->job_step_count , format );	
}

