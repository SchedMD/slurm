/****************************************************************************\
 *  * slurm_protocol_defs.h - definitions used for RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
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

#include <popt.h>
#include "squeue.h"

#define OPT_JOBS      	0x01
#define OPT_JOBS_NONE 	0x02
#define OPT_STEPS     	0x03
#define OPT_STEPS_NONE	0x04
#define OPT_PARTITIONS	0x05
#define OPT_NODES     	0x06
#define OPT_STATES     	0x07
#define OPT_FORMAT    	0x08
#define OPT_VERBOSE   	0x09


extern struct squeue_parameters params;
char *states = NULL;
char *partitions = NULL;

/* FUNCTIONS */
int parse_state( char* str, enum job_states* states );
int parse_format( char* );

/*
 * parse_command_line
 */
int
parse_command_line( int argc, char* argv[] )
{
	/* { long-option, short-option, argument type, variable address, option tag, docstr, argstr } */

	poptContext context;
	char next_opt, curr_opt;
	int rc = 0;

	/* Declare the Options */
	static const struct poptOption options[] = 
	{
		{"jobs", 'j', POPT_ARG_NONE, &params.job_flag, OPT_JOBS_NONE, "job to view", "job_id"},
		{"job-steps", 's', POPT_ARG_NONE, &params.step_flag, OPT_STEPS_NONE, "job_step to list.  No job_step implies all job_steps" , "job_step_id"},
		{"states", 't', POPT_ARG_STRING, &states, OPT_STATES, "comma seperated list of states to view", "states"},
		{"partitions", 'p', POPT_ARG_STRING, &partitions, OPT_PARTITIONS,"partitions", "partitions"},
		{"format", 'o', POPT_ARG_STRING, &params.format, OPT_FORMAT, "format string", "format string"},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose, OPT_VERBOSE, "verbosity level", "level"},
		POPT_AUTOHELP 
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end the list */
	};

	/* Initial the popt contexts */
	context = poptGetContext(NULL, argc, (const char**)argv, options, 0);
	next_opt = poptGetNextOpt(context); 

	while ( next_opt > -1  )
	{
		const char* opt_arg = NULL;
		curr_opt = next_opt;
		next_opt = poptGetNextOpt(context);

		switch ( curr_opt )
		{
			case OPT_JOBS_NONE:
				/* Eventually I will want to parse these and form a list */
				if ( (opt_arg = poptGetArg( context )) != NULL )
					params.jobs = (char*)opt_arg;
				break;	
			case OPT_STEPS_NONE:
				/* Eventually I will want to parse these and form a list */
				if ( (opt_arg = poptGetArg( context )) != NULL )
					params.steps = (char*)opt_arg;
				break;	
			case OPT_STATES:
				if ( parse_state( states, &params.state ) != SLURM_SUCCESS )
				{
					fprintf(stderr, "Invalid node state\n");
					exit( 0 );
				}
				break;	
			case OPT_PARTITIONS:
				params.partitions = partitions;	
				break;	
			case OPT_FORMAT:
				parse_format( params.format );
				break;
			case OPT_VERBOSE:
				params.verbose = true;
				break;	
			default:
				break;	
		}
		if ( (opt_arg = poptGetArg( context )) != NULL )
		{
			fprintf(stderr, "%s: %s \"%s\"\n", argv[0], poptStrerror(POPT_ERROR_BADOPT), opt_arg);
			exit (1);
		}
		if ( curr_opt < 0 )
		{
			fprintf(stderr, "%s: \"%s\" %s\n", argv[0], poptBadOption(context, POPT_BADOPTION_NOALIAS), poptStrerror(next_opt));

			exit (1);
		}
	}
	if ( next_opt < -1 )
	{
		fprintf(stderr, "%s: \"%s\" %s\n", argv[0], poptBadOption(context, POPT_BADOPTION_NOALIAS), poptStrerror(next_opt));
		exit (1);
	}


	return rc;
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

int 
parse_format( char* format )
{

	return SLURM_SUCCESS;
}

void 
print_options()
{
	printf( "-----------------------------\n" );
	printf( "job_flag %d\n", params.job_flag );
	printf( "step_flag %d\n", params.step_flag );
	printf( "jobs %s\n", params.jobs );	
	printf( "steps %s\n", params.steps );	
	printf( "state %s\n", job_state_string( params.state ) );
	printf( "verbose %d\n", params.verbose );
	printf( "format %s\n", params.format );
	printf( "partitions %s\n", params.partitions ) ;
	printf( "-----------------------------\n\n\n" );
} ;


