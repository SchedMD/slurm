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
#include <src/common/list.h>
#include <src/popt/popt.h>

#include "print.h"

static char *command_name;
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */

int parse_state( char* str, enum job_states* states );
int parse_command_line( int argc, char* argv[] );
void print_options();
void print_job (char * job_id_str);
void print_job_steps( uint32_t job_id, uint16_t step_id );
void usage ();

struct command_line_params {
	bool job_flag;
	bool step_flag;
	uint16_t step;
	uint32_t job;
	enum job_states state;
	int verbose;
	char* format;
	char* partitions;
} params;


int 
main (int argc, char *argv[]) 
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	command_name = argv[0];
	quiet_flag = 0;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line( argc, argv );
	print_options();
	
	if ( params.step_flag )
	{
		print_job_steps( 0, 0 );
	}
	else print_job( NULL );

	exit (0);
}


/*
 * parse_command_line
 */
char *jobs = NULL;
char *steps = NULL;
char *states = NULL;
char *partitions = NULL;

int
parse_command_line( int argc, char* argv[] )
{
	/* { long-option, short-option, argument type, variable address, option tag, docstr, argstr } */

	poptContext context;
	char next_opt;
	static const struct poptOption options[] = 
	{
		{"jobs", 'j', POPT_ARG_NONE, NULL, 'j', NULL, NULL},
		{"job-steps", 's', POPT_ARG_NONE, NULL, 's',NULL, NULL},
		{"states", 't', POPT_ARG_STRING, &states, 't',NULL, NULL},
		{"partitions", 'p', POPT_ARG_STRING, &partitions, 'p',NULL, NULL},
		{"format", 'o', POPT_ARG_STRING, &params.format, 'o',NULL, NULL},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose, 'v',NULL, NULL},
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end the list */
	};

	context = poptGetContext(NULL, argc, (const char**)argv, options, 0);

	while ( ( next_opt = poptGetNextOpt(context) ) >= 0 )
	{
		switch ( next_opt )
		{
			case 'j':
				params.job_flag = true;
				break;	
			case 's':
				params.step_flag = true;
				break;	
			case 't':
				if ( parse_state( states, &params.state ) != SLURM_SUCCESS )
				{
					fprintf(stderr, "Invalid node state\n");
					exit( 0 );
				}
				break;	
			case 'p':
				params.partitions = partitions;	
				break;	
			case 'o':
				break;	
			case 'v':
				params.verbose = true;
				break;	
			default:
				break;	
		}
	}

	if ( next_opt < -1 )
	{
		usage();
	}

	return 1;
}


/*
 * parse_state - parse state information
 * input - char* comma seperated list of states
 */
int
parse_state( char* str, enum job_states* states )
{	
	/* FIXME - this will eventually return an array of enums
	 */

	if ( strcasecmp( str, job_state_string( JOB_PENDING )) == 0 )
		*states = JOB_PENDING;
	else if ( strcasecmp( str, job_state_string( JOB_STAGE_IN )) == 0 )
		*states = JOB_STAGE_IN;
	else if ( strcasecmp( str, job_state_string( JOB_RUNNING )) == 0 )
		*states = JOB_RUNNING;
	else if ( strcasecmp( str, job_state_string( JOB_STAGE_OUT)) == 0 )
		*states = JOB_STAGE_OUT;
	else if ( strcasecmp( str, job_state_string( JOB_COMPLETE )) == 0 )
		*states = JOB_COMPLETE;
	else if ( strcasecmp( str, job_state_string( JOB_TIMEOUT )) == 0 )
		*states = JOB_TIMEOUT;
	else if ( strcasecmp( str, job_state_string( JOB_FAILED )) == 0 )
		*states = JOB_FAILED;
	else if ( strcasecmp( str, job_state_string( JOB_END )) == 0 )
		*states = JOB_END;
	else return SLURM_ERROR;

	return SLURM_SUCCESS;
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
	job_info_t *job_ptr = NULL;


	List format = list_create( NULL );
	job_format_add_job_id( format, 12, false );	
	job_format_add_name( format, 10, true );	
	job_format_add_user_name( format, 8, true );	
	job_format_add_job_state( format, 10, true );	
	job_format_add_time_limit( format, 6, false );	
	job_format_add_start_time( format, 15, false );	
	job_format_add_end_time( format, 15, false );	
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

	job_ptr = job_buffer_ptr->job_array ;

	print_jobs_from_list( format, NULL );
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_id_str && job_id != job_ptr[i].job_id) 
			continue;
		print_cnt++;
		print_jobs_from_list( format, & job_ptr[i] );
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


void
print_job_steps( uint32_t job_id, uint16_t step_id )
{
	int i;
	int rc = SLURM_SUCCESS;
	static job_step_info_response_msg_t * step_msg = NULL;


	List format = list_create( NULL );

	step_format_add_id( format, 12, true );
	step_format_add_user_id( format, 8, true );
	step_format_add_start_time( format, 12, true );
	step_format_add_nodes( format, 20, true );
	
	if ( ( rc = slurm_get_job_steps( job_id, step_id, &step_msg ) ) != SLURM_SUCCESS )
	{
		error( "slurm_get_job_steps failed: I really should be more descriptive here\n");
		return;
	}

	print_steps_from_list( format, NULL );
	if ( step_msg->job_step_count > 0 )
		for ( i=0; i < step_msg->job_step_count; i++ )
		{
			print_steps_from_list( format, &step_msg->job_steps[i] );
		}
	else printf("No job steps found in system" );
	

	
}

/* usage - show the valid squeue commands */
void
usage () {
	printf ("sqeueue [OPTIONS ...]\n");
}

void 
print_options()
{
	printf( "-----------------------------\n" );
	printf( "job_flag %d\n", params.job_flag );
	printf( "step_flag %d\n", params.step_flag );
	printf( "state %s\n", job_state_string( params.state ) );
	printf( "verbose %d\n", params.verbose );
	printf( "format %s\n", params.format );
	printf( "partitions %s\n", params.partitions ) ;
	printf( "-----------------------------\n\n\n" );
} ;


