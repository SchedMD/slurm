/****************************************************************************\
 *  opts.c - srun command line option parsing
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

#include <popt.h>

#include <src/popt/popt.h>
#include <src/squeue/squeue.h>

#define __DEBUG 1

#define OPT_JOBS      	0x01
#define OPT_JOBS_NONE 	0x02
#define OPT_STEPS     	0x03
#define OPT_STEPS_NONE	0x04
#define OPT_PARTITIONS	0x05
#define OPT_NODES     	0x06
#define OPT_STATES     	0x07
#define OPT_FORMAT    	0x08
#define OPT_VERBOSE   	0x09
#define OPT_ITERATE   	0x0a
#define OPT_USERS   	0x0b
#define OPT_LONG   	0x0c

extern struct squeue_parameters params;

/* FUNCTIONS */
static List build_job_list( char* str );
static List build_part_list( char* str );
static List build_state_list( char* str );
static List build_step_list( char* str );
static List build_user_list( char* str );
static int  parse_state( char* str, enum job_states* states );
static int  parse_format( char* );
static void print_options( void );

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
		{"iterate", 'i', POPT_ARG_INT, &params.iterate, OPT_ITERATE,
			 "specify an interation period", "seconds"},
		{"jobs", 'j', POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL, 
			&params.jobs, OPT_JOBS_NONE, 
			"comma separated list of jobs to view, default is all", 
			"job_id"},
		{"job-steps", 's', POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL, 
			&params.steps, OPT_STEPS_NONE, 
			"comma separated list of job steps to view, default is all" , 
			"job_step"},
		{"long", 'l', POPT_ARG_NONE, &params.long_list, OPT_LONG, 
			"long report", NULL},
		{"states", 't', POPT_ARG_STRING, &params.states, OPT_STATES, 
			"comma seperated list of states to view", "states"},
		{"partitions", 'p', POPT_ARG_STRING, 
			&params.partitions, OPT_PARTITIONS,
			"comma separated list of partitions to view", "partitions"},
		{"format", 'o', POPT_ARG_STRING, &params.format, OPT_FORMAT, 
			"format specification", "format"},
		{"user", 'u', POPT_ARG_STRING, &params.users, OPT_USERS, 
			"comma separated list of users to view", "user_name"},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose, OPT_VERBOSE, 
			"verbosity level", NULL},
		POPT_AUTOHELP
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end the list */
	};

	/* Initial the popt contexts */
	context = poptGetContext("squeue", argc, (const char**)argv, 
				options, POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(context, "[-jlsv]");

	next_opt = poptGetNextOpt(context); 

	while ( next_opt > -1  )
	{
		const char* opt_arg = NULL;
		curr_opt = next_opt;
		next_opt = poptGetNextOpt(context);

		switch ( curr_opt )
		{
			case OPT_JOBS_NONE:
				params.job_flag = true;
				params.job_list = build_job_list( params.jobs );
				break;	
			case OPT_STEPS_NONE:
				params.step_flag = true;
				params.step_list = build_step_list( params.steps );
				break;	
			case OPT_STATES:
				params.state_list = build_state_list( params.states );
				break;	
			case OPT_PARTITIONS:
				params.part_list = build_part_list( params.partitions );
				break;	
			case OPT_USERS:
				params.user_list = build_user_list( params.users );
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
			fprintf(stderr, "%s: %s \"%s\"\n", argv[0], 
				poptStrerror(POPT_ERROR_BADOPT), opt_arg);
			exit (1);
		}
		if ( curr_opt < 0 )
		{
			fprintf(stderr, "%s: \"%s\" %s\n", argv[0], 
				poptBadOption(context, POPT_BADOPTION_NOALIAS), 
				poptStrerror(next_opt));
			exit (1);
		}
	}
	if ( next_opt < -1 )
	{
		const char *bad_opt;
		bad_opt = poptBadOption(context, POPT_BADOPTION_NOALIAS);
		if (strcmp (bad_opt, "-h"))
			error("bad argument %s: %s", bad_opt, poptStrerror(next_opt));
		poptPrintUsage(context, stderr, 0);

		exit (1);
	}

	if ( params.verbose )
		print_options();
	return rc;
}

/*
 * parse_state - parse state information
 * input - char* comma seperated list of states
 */
int
parse_state( char* str, enum job_states* states )
{	
	int i;

	for (i = 0; ; i++) {
		if (strcasecmp (job_state_string(i), "END") == 0)
			break;

		if (strcasecmp (job_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
		if (strcasecmp (job_state_string_compact(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
	}	
	return SLURM_ERROR;
}

int 
parse_format( char* format )
{
	/* perform check of format string */
	return SLURM_SUCCESS;
}

void 
print_options()
{
#if	__DEBUG
	ListIterator iterator;
	int i;
	char *part, *user;
	enum job_states *state_id;
	squeue_job_step_t *job_step_id;
	uint32_t *job_id;
#endif

	printf( "-----------------------------\n" );
	printf( "iterate %d\n", params.iterate );
	printf( "job_flag %d\n", params.job_flag );
	printf( "step_flag %d\n", params.step_flag );
	printf( "jobs %s\n", params.jobs );	
	printf( "partitions %s\n", params.partitions ) ;
	printf( "states %s\n", params.states ) ;
	printf( "steps %s\n", params.steps );	
	printf( "users %s\n", params.users );	
	printf( "verbose %d\n", params.verbose );
	printf( "format %s\n", params.format );
#if	__DEBUG
	if (params.job_list) {
		i = 0;
		iterator = list_iterator_create( params.job_list );
		while ( (job_id = list_next( iterator )) ) {
			printf( "job_list[%d] = %u\n", i++, *job_id);
		}
		list_iterator_destroy( iterator );
	}

	if (params.part_list) {
		i = 0;
		iterator = list_iterator_create( params.part_list );
		while ( (part = list_next( iterator )) ) {
			printf( "part_list[%d] = %s\n", i++, part);
		}
		list_iterator_destroy( iterator );
	}

	if (params.state_list) {
		i = 0;
		iterator = list_iterator_create( params.state_list );
		while ( (state_id = list_next( iterator )) ) {
			printf( "state_list[%d] = %s\n", 
				i++, job_state_string( *state_id ));
		}
		list_iterator_destroy( iterator );
	}

	if (params.step_list) {
		i = 0;
		iterator = list_iterator_create( params.step_list );
		while ( (job_step_id = list_next( iterator )) ) {
			printf( "step_list[%d] = %u.%u\n", i++, 
				job_step_id->job_id, job_step_id->step_id );
		}
		list_iterator_destroy( iterator );
	}

	if (params.user_list) {
		i = 0;
		iterator = list_iterator_create( params.user_list );
		while ( (user = list_next( iterator )) ) {
			printf( "user_list[%d] = %s\n", i++, user);
		}
		list_iterator_destroy( iterator );
	}

#endif
	printf( "-----------------------------\n\n\n" );
} ;


static List 
build_job_list( char* str )
{
	List my_list;
	char *job, *tmp_char, *my_job_list;
	int i;
	uint32_t *job_id;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	i = strlen( str );
	my_job_list = malloc( i+1 );
	strcpy( my_job_list, str );
	job = strtok_r( my_job_list, ",", &tmp_char );
	while (job) 
	{
		i = strtol( job, (char **) NULL, 10 );
		if (i <= 0) {
			fprintf( stderr, "Invalid job id: %s\n", job );
			exit( 1 );
		}
		job_id = malloc( sizeof( uint32_t ) );
		*job_id = (uint32_t) i;
		list_append( my_list, job_id );
		job = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

static List 
build_part_list( char* str )
{
	List my_list;
	char *part, *tmp_char, *my_part_list;
	int i;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	i = strlen( str );
	my_part_list = malloc( i+1 );
	strcpy( my_part_list, str );
	part = strtok_r( my_part_list, ",", &tmp_char );
	while (part) 
	{
		list_append( my_list, part );
		part = strtok_r( NULL, ",", &tmp_char );
	}
	return my_list;
}

static List 
build_state_list( char* str )
{
	List my_list;
	char *state, *tmp_char, *my_state_list;
	int i;
	enum job_states *state_id;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	i = strlen( str );
	my_state_list = malloc( i+1 );
	strcpy( my_state_list, str );
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state) 
	{
		state_id = malloc( sizeof( enum job_states ) );
		if ( parse_state( state, state_id ) != SLURM_SUCCESS )
		{
			fprintf( stderr, "Invalid node state: %s\n", state );
			exit( 1 );
		}
		list_append( my_list, state_id );
		state = strtok_r( NULL, ",", &tmp_char );
	}
	return my_list;

}

static List 
build_step_list( char* str )
{
	List my_list;
	char *step, *tmp_char, *tmps_char, *my_step_list;
	char *job_name, *step_name;
	int i, j;
	squeue_job_step_t *job_step_id;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	i = strlen( str );
	my_step_list = malloc( i+1 );
	strcpy( my_step_list, str );
	step = strtok_r( my_step_list, ",", &tmp_char );
	while (step) 
	{
		job_name = strtok_r( step, ".", &tmps_char );
		step_name = strtok_r( NULL, ".", &tmps_char );
		i = strtol( job_name, (char **) NULL, 10 );
		if (step_name == NULL) {
			fprintf( stderr, "Invalid job_step id: %s.??\n", job_name );
			exit( 1 );
		}
		j = strtol( step_name, (char **) NULL, 10 );
		if ((i <= 0) || (j < 0)) {
			fprintf( stderr, "Invalid job_step id: %s.%s\n", 
				job_name, step_name );
			exit( 1 );
		}
		job_step_id = malloc( sizeof( squeue_job_step_t ) );
		job_step_id->job_id  = (uint32_t) i;
		job_step_id->step_id = (uint32_t) j;
		list_append( my_list, job_step_id );
		step = strtok_r( NULL, ",", &tmp_char);
	}
	return my_list;
}

static List 
build_user_list( char* str )
{
	List my_list;
	char *user, *tmp_char, *my_user_list;
	int i;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	i = strlen( str );
	my_user_list = malloc( i+1 );
	strcpy( my_user_list, str );
	user = strtok_r( my_user_list, ",", &tmp_char );
	while (user) 
	{
		list_append( my_list, user );
		user = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

