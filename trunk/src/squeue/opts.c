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
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "src/common/xstring.h"
#include "src/popt/popt.h"
#include "src/squeue/squeue.h"

#define __DEBUG 0

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
#define OPT_LONG   	    0x0c
#define OPT_SORT   	    0x0d
#define OPT_NO_HEAD   	0x0e
#define OPT_VERSION     0x0f

/* FUNCTIONS */
static List  _build_job_list( char* str );
static List  _build_part_list( char* str );
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static List  _build_step_list( char* str );
static List  _build_user_list( char* str );
static char *_get_prefix(char *token);
static int   _max_procs_per_node(void);
static int   _parse_state( char* str, enum job_states* states );
static void  _parse_token( char *token, char *field, int *field_size, 
                           bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _print_version( void );

/*
 * parse_command_line
 */
int
parse_command_line( int argc, char* argv[] )
{
	poptContext context;
	char next_opt, curr_opt;
	char *env_val = NULL;
	int rc = 0;

	/* Declare the Options */
	static const struct poptOption options[] = 
	{
		/* { long-option, short-option, argument type, 
		 *    variable address, option tag, docstr, argstr } */

		{"iterate", 'i', POPT_ARG_INT, &params.iterate, OPT_ITERATE,
			"specify an interation period", "seconds"},
		{"noheader", 'h', POPT_ARG_NONE, NULL, OPT_NO_HEAD, 
			"no headers on output", NULL},
		{"jobs", 'j', POPT_ARG_NONE, NULL, OPT_JOBS_NONE, 
			"comma separated list of jobs to view, default is all", 
			"job_id"},
		{"steps", 's', POPT_ARG_NONE, NULL, OPT_STEPS_NONE, 
			"comma separated list of job steps to view, "
			"default is all",
			"job_step"},
		{"long", 'l', POPT_ARG_NONE, NULL, OPT_LONG, 
			"long report", NULL},
		{"sort", 'S', POPT_ARG_STRING, &params.sort, OPT_SORT,
			"comma seperated list of fields to sort on", "fields"},
		{"states", 't', POPT_ARG_STRING, &params.states, OPT_STATES,
			"comma seperated list of states to view, "
			"default is pending and running, "
			"\"all\" reports all states", "states"},
		{"partitions", 'p', POPT_ARG_STRING, 
			&params.partitions, OPT_PARTITIONS,
			"comma separated list of partitions to view, "
			"default is all partitions", 
			"partitions"},
		{"format", 'o', POPT_ARG_STRING, &params.format, OPT_FORMAT, 
			"format specification", "format"},
		{"user", 'u', POPT_ARG_STRING, &params.users, OPT_USERS,
			"comma separated list of users to view", "user_name"},
		{"verbose", 'v', POPT_ARG_NONE, NULL, OPT_VERBOSE,
			"verbosity level", NULL},
		{"version", 'V', POPT_ARG_NONE, NULL, OPT_VERSION,
			"output version information and exit", NULL},
		POPT_AUTOHELP
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end the list */
	};

	/* Initial the popt contexts */
	context = poptGetContext("squeue", argc, (const char**)argv, 
				options, POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(context, "[-hjlsv]");

	next_opt = poptGetNextOpt(context); 

	while ( next_opt > -1  )
	{
		const char* opt_arg = NULL;
		curr_opt = next_opt;
		next_opt = poptGetNextOpt(context);

		switch ( curr_opt )
		{
			case OPT_NO_HEAD:
				params.no_header = true;
				break;
			case OPT_JOBS_NONE:
				if ( (opt_arg = poptGetArg(context)) != NULL )
					params.jobs = (char*)opt_arg;
				params.job_list = _build_job_list(params.jobs);
				params.job_flag = true;
				break;	
			case OPT_STEPS_NONE:
				if ( (opt_arg = poptGetArg( context )) != NULL )
					params.steps = (char*)opt_arg;
				params.step_list = _build_step_list( params.steps );
				params.step_flag = true;
				break;	
			case OPT_LONG:
				params.long_list = true;
				break;
			case OPT_STATES:
				params.state_list = _build_state_list( params.states );
				break;	
			case OPT_PARTITIONS:
				params.part_list = 
					_build_part_list( params.partitions );
				break;	
			case OPT_USERS:
				params.user_list = _build_user_list( params.users );
				break;
			case OPT_VERBOSE:
				params.verbose++;
				break;	
			case OPT_VERSION:
				_print_version();
				exit(0);
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
		bad_opt = poptBadOption( context, POPT_BADOPTION_NOALIAS );
		fprintf( stderr, "bad argument %s: %s\n", bad_opt, 
				poptStrerror(next_opt) );
		fprintf( stderr, "Try \"%s --help\" for more information\n", 
		         argv[0] );
		exit( 1 );
	}

	if ( ( params.format == NULL ) && 
	     ( env_val = getenv("SQUEUE_FORMAT") ) ) 
		params.format = xstrdup(env_val);

	if ( ( params.partitions == NULL ) && 
	     ( env_val = getenv("SQUEUE_PARTITION") ) ) {
		params.partitions = xstrdup(env_val);
		params.part_list = _build_part_list( params.partitions );
	}

	if ( ( params.sort == NULL ) && 
	     ( env_val = getenv("SQUEUE_SORT") ) )
		params.sort = xstrdup(env_val);

	if ( ( params.states == NULL ) && 
	     ( env_val = getenv("SQUEUE_STATES") ) ) {
		params.states = xstrdup(env_val);
		params.state_list = _build_state_list( params.states );
	}

	if ( ( params.users == NULL ) && 
	     ( env_val = getenv("SQUEUE_USERS") ) ) {
		params.users = xstrdup(env_val);
		params.user_list = _build_user_list( params.users );
	}

	params.max_procs = _max_procs_per_node();

	if ( params.verbose )
		_print_options();

	return rc;
}

/* Return the maximum number of processors for any node in the cluster */
static int   _max_procs_per_node(void)
{
	int error_code, max_procs = 1;
	node_info_msg_t *node_info_ptr = NULL;

	error_code = slurm_load_node ((time_t) NULL, &node_info_ptr);
	if (error_code == SLURM_SUCCESS) {
		int i;
		node_info_t *node_ptr = node_info_ptr->node_array;
		for (i=0; i<node_info_ptr->record_count; i++) {
			max_procs = MAX(max_procs, node_ptr[i].cpus);
		}
		slurm_free_node_info_msg (node_info_ptr);
	}

	return max_procs;
}

/*
 * _parse_state - convert job state name string to numeric value
 * IN str - state name
 * OUT states - enum job_states value corresponding to str
 * RET 0 or error code
 */
static int
_parse_state( char* str, enum job_states* states )
{	
	int i;
	char *state_names;

	for (i = 0; i<JOB_END; i++) {
		if (strcasecmp (job_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
		if (strcasecmp (job_state_string_compact(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
	}
	if ((strcasecmp(job_state_string(JOB_COMPLETING), str) == 0) ||
	    (strcasecmp(job_state_string_compact(JOB_COMPLETING),str) == 0)) {
		*states = JOB_COMPLETING;
		return SLURM_SUCCESS;
	}	

	fprintf (stderr, "Invalid job state specified: %s\n", str);
	state_names = xstrdup(job_state_string(0));
	for (i=1; i<JOB_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, job_state_string(i));
	}
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_COMPLETING));
	fprintf (stderr, "Valid job states include: %s\n", state_names);
	xfree (state_names);
	return SLURM_ERROR;
}

/* 
 * parse_format - Take the user's format specification and use it to build 
 *	build the format specifications (internalize it to print.c data 
 *	structures) 
 * IN format - user's format specification
 * RET zero or error code
 */
int parse_format( char* format )
{
	int field_size;
	bool right_justify;
	char *prefix, *suffix, *token, *tmp_char, *tmp_format;
	char field[1];

	if (format == NULL) {
		fprintf( stderr, "Format option lacks specification\n" );
		exit( 1 );
	}

	params.format_list = list_create( NULL );
	if ((prefix = _get_prefix(format))) {
		if (params.step_flag)
			step_format_add_prefix( params.format_list, 0, 0, 
						prefix); 
		else
			job_format_add_prefix( params.format_list, 0, 0, 
					       prefix);
	}

	field_size = strlen( format );
	tmp_format = xmalloc( field_size + 1 );
	strcpy( tmp_format, format );

	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		_parse_token( token, field, &field_size, &right_justify, 
			      &suffix);
		if (params.step_flag) {
			if      (field[0] == 'i')
				step_format_add_id( params.format_list, 
				                    field_size, 
						    right_justify, suffix );
			else if (field[0] == 'M')
				step_format_add_time_used( params.format_list, 
				                            field_size, 
				                            right_justify, 
				                            suffix );
			else if (field[0] == 'N')
				step_format_add_nodes( params.format_list, 
				                       field_size, 
				                       right_justify, suffix );
			else if (field[0] == 'P')
				step_format_add_partition( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else if (field[0] == 'S')
				step_format_add_time_start( params.format_list, 
				                            field_size, 
				                            right_justify, 
				                            suffix );
			else if (field[0] == 'U')
				step_format_add_user_id( params.format_list, 
				                         field_size, 
				                         right_justify, 
				                         suffix );
			else if (field[0] == 'u')
				step_format_add_user_name( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else
				fprintf( 
				   stderr, 
				   "Invalid job step format specification: %c\n",
				   field[0] );
		} else {
			if      (field[0] == 'b')
				job_format_add_time_start( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix  );
			else if (field[0] == 'c')
				job_format_add_min_procs( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix  );
			else if (field[0] == 'C')
				job_format_add_num_procs( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix  );
			else if (field[0] == 'd')
				job_format_add_min_tmp_disk( 
				                          params.format_list,
				                          field_size, 
				                          right_justify, 
				                          suffix  );
			else if (field[0] == 'D')
				job_format_add_num_nodes( params.format_list,
				                          field_size, 
				                          right_justify, 
				                          suffix  );
			else if (field[0] == 'e')
				job_format_add_time_end( params.format_list, 
				                         field_size, 
				                         right_justify, 
				                         suffix );
			else if (field[0] == 'f')
				job_format_add_features( params.format_list, 
				                         field_size, 
				                         right_justify, 
				                         suffix );
			else if (field[0] == 'h')
				job_format_add_shared( params.format_list, 
				                       field_size, 
				                       right_justify, 
				                       suffix );
			else if (field[0] == 'i')
				job_format_add_job_id( params.format_list, 
				                       field_size, 
				                       right_justify, 
				                       suffix );
			else if (field[0] == 'j')
				job_format_add_name( params.format_list, 
				                     field_size, 
				                     right_justify, suffix );
			else if (field[0] == 'l')
				job_format_add_time_limit( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else if (field[0] == 'm')
				job_format_add_min_memory( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else if (field[0] == 'M')
				job_format_add_time_used( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else if (field[0] == 'n')
				job_format_add_req_nodes( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else if (field[0] == 'N')
				job_format_add_nodes( params.format_list, 
				                      field_size, 
				                      right_justify, suffix );
			else if (field[0] == 'o')
				job_format_add_num_nodes( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else if (field[0] == 'O')
				job_format_add_contiguous( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else if (field[0] == 'p')
				job_format_add_priority( params.format_list, 
				                         field_size, 
				                         right_justify, 
				                         suffix );
			else if (field[0] == 'P')
				job_format_add_partition( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else if (field[0] == 'S')
				job_format_add_time_start( params.format_list, 
				                           field_size, 
				                           right_justify, 
				                           suffix );
			else if (field[0] == 't')
				job_format_add_job_state_compact( 
							params.format_list, 
							field_size, 
							right_justify, 
				                        suffix );
			else if (field[0] == 'T')
				job_format_add_job_state( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else if (field[0] == 'U')
				job_format_add_user_id( params.format_list, 
				                        field_size, 
				                        right_justify, 
				                        suffix );
			else if (field[0] == 'u')
				job_format_add_user_name( params.format_list, 
				                          field_size, 
				                          right_justify, 
				                          suffix );
			else 
				fprintf( 
				   stderr, 
				   "Invalid job format specification: %c\n", 
				   field[0] );
		}
		token = strtok_r( NULL, "%", &tmp_char);
	}

	xfree( tmp_format );
	return SLURM_SUCCESS;
}

/* Take a format specification and copy out it's prefix
 * IN/OUT token - input specification, everything before "%" is removed
 * RET - everything before "%" in the token
 */
static char *
_get_prefix( char *token )
{
	char *pos, *prefix;

	if (token == NULL) 
		return NULL;

	pos = strchr(token, (int) '%');
	if (pos == NULL)	/* everything is prefix */
		return xstrdup(token);
	if (pos == token)	/* no prefix */
		return NULL;

	pos[0] = '\0';		/* some prefix */
	prefix = xstrdup(token);
	pos[0] = '%';
	memmove(token, pos, (strlen(pos)+1));
	return prefix;
}

/* Take a format specification and break it into its components
 * IN token - input specification without leading "%", eg. ".5u"
 * OUT field - the letter code for the data type
 * OUT field_size - byte count
 * OUT right_justify - true of field to be right justified
 * OUT suffix - tring containing everthing after the field specification
 */
static void
_parse_token( char *token, char *field, int *field_size, bool *right_justify, 
	      char **suffix)
{
	int i = 0;

	assert (token != NULL);

	if (token[i] == '.') {
		*right_justify = true;
		i++;
	} else
		*right_justify = false;

	*field_size = 0;
	while ((token[i] >= '0') && (token[i] <= '9'))
		*field_size = (*field_size * 10) + (token[i++] - '0');

	field[0] = token[i++];

	*suffix = xstrdup(&token[i]);
}

/* print the parameters specified */
static void
_print_options()
{
#if	__DEBUG
	ListIterator iterator;
	int i;
	char *part;
	uint32_t *user;
	enum job_states *state_id;
	squeue_job_step_t *job_step_id;
	uint32_t *job_id;
#endif

	printf( "-----------------------------\n" );
	printf( "format     = %s\n", params.format );
	printf( "iterate    = %d\n", params.iterate );
	printf( "job_flag   = %d\n", params.job_flag );
	printf( "jobs       = %s\n", params.jobs );
	printf( "max_procs  = %d\n", params.max_procs ) ;
	printf( "partitions = %s\n", params.partitions ) ;
	printf( "sort       = %s\n", params.sort ) ;
	printf( "states     = %s\n", params.states ) ;
	printf( "step_flag  = %d\n", params.step_flag );
	printf( "steps      = %s\n", params.steps );	
	printf( "users      = %s\n", params.users );	
	printf( "verbose    = %d\n", params.verbose );

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
			printf( "user_list[%d] = %u\n", i++, *user);
		}
		list_iterator_destroy( iterator );
	}

#endif
	printf( "-----------------------------\n\n\n" );
} ;


/*
 * _build_job_list- build a list of job_ids
 * IN str - comma separated list of job_ids
 * RET List of job_ids (uint32_t)
 */
static List 
_build_job_list( char* str )
{
	List my_list;
	char *job, *tmp_char, *my_job_list;
	int i;
	uint32_t *job_id;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_job_list = xstrdup( str );
	job = strtok_r( my_job_list, ",", &tmp_char );
	while (job) 
	{
		i = strtol( job, (char **) NULL, 10 );
		if (i <= 0) {
			fprintf( stderr, "Invalid job id: %s\n", job );
			exit( 1 );
		}
		job_id = xmalloc( sizeof( uint32_t ) );
		*job_id = (uint32_t) i;
		list_append( my_list, job_id );
		job = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

/*
 * _build_part_list- build a list of partition names
 * IN str - comma separated list of partition names
 * RET List of partition names
 */
static List 
_build_part_list( char* str )
{
	List my_list;
	char *part, *tmp_char, *my_part_list;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_part_list = xstrdup( str );
	part = strtok_r( my_part_list, ",", &tmp_char );
	while (part) 
	{
		list_append( my_list, part );
		part = strtok_r( NULL, ",", &tmp_char );
	}
	return my_list;
}

/*
 * _build_state_list - build a list of job states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static List 
_build_state_list( char* str )
{
	List my_list;
	char *state, *tmp_char, *my_state_list;
	enum job_states *state_id;

	if ( str == NULL)
		return NULL;
	if ( strcasecmp( str, "all" ) == 0 )
		return _build_all_states_list ();

	my_list = list_create( NULL );
	my_state_list = xstrdup( str );
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state) 
	{
		state_id = xmalloc( sizeof( enum job_states ) );
		if ( _parse_state( state, state_id ) != SLURM_SUCCESS )
		{
			exit( 1 );
		}
		list_append( my_list, state_id );
		state = strtok_r( NULL, ",", &tmp_char );
	}
	return my_list;

}

/*
 * _build_all_states_list - build a list containing all possible job states
 * RET List of enum job_states values
 */
static List 
_build_all_states_list( void )
{
	List my_list;
	int i;
	enum job_states * state_id;

	my_list = list_create( NULL );
	for (i = 0; i<JOB_END; i++) {
		state_id = xmalloc( sizeof( enum job_states ) );
		*state_id = ( enum job_states ) i;
		list_append( my_list, state_id );
	}
	state_id = xmalloc( sizeof( enum job_states ) );
	*state_id = ( enum job_states ) JOB_COMPLETING;
	list_append( my_list, state_id );
	return my_list;

}

/*
 * _build_step_list- build a list of job/step_ids
 * IN str - comma separated list of job_id.step_ids
 * RET List of job/step_ids (structure of uint32_t's)
 */
static List 
_build_step_list( char* str )
{
	List my_list;
	char *step, *tmp_char, *tmps_char, *my_step_list;
	char *job_name, *step_name;
	int i, j;
	squeue_job_step_t *job_step_id;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_step_list = xstrdup( str );
	step = strtok_r( my_step_list, ",", &tmp_char );
	while (step) 
	{
		job_name = strtok_r( step, ".", &tmps_char );
		step_name = strtok_r( NULL, ".", &tmps_char );
		i = strtol( job_name, (char **) NULL, 10 );
		if (step_name == NULL) {
			fprintf( stderr, "Invalid job_step id: %s.??\n", 
			         job_name );
			exit( 1 );
		}
		j = strtol( step_name, (char **) NULL, 10 );
		if ((i <= 0) || (j < 0)) {
			fprintf( stderr, "Invalid job_step id: %s.%s\n", 
				job_name, step_name );
			exit( 1 );
		}
		job_step_id = xmalloc( sizeof( squeue_job_step_t ) );
		job_step_id->job_id  = (uint32_t) i;
		job_step_id->step_id = (uint32_t) j;
		list_append( my_list, job_step_id );
		step = strtok_r( NULL, ",", &tmp_char);
	}
	return my_list;
}

/*
 * _build_user_list- build a list of UIDs
 * IN str - comma separated list of user names
 * RET List of UIDs (uint32_t)
 */
static List 
_build_user_list( char* str )
{
	List my_list;
	char *user, *tmp_char, *my_user_list;
	uint32_t *uid;
	struct passwd *passwd_ptr;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_user_list = xstrdup( str );
	user = strtok_r( my_user_list, ",", &tmp_char );
	while (user) 
	{
		passwd_ptr = getpwnam( user );
		if (passwd_ptr == NULL)
			fprintf( stderr, "Invalid user: %s\n", user);
		else {
			uid = xmalloc( sizeof( uint32_t ));
			*uid = passwd_ptr->pw_uid;
			list_append( my_list, uid );
		}
		user = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

