/****************************************************************************\
 *  opts.c - sinfo command line option processing functions
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_POPT_H
#  include <popt.h>
#else
#  include "src/popt/popt.h"
#endif


#include "src/common/xstring.h"
#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

#define OPT_NODE_STATE 	0x02
#define OPT_PARTITION	0x03
#define OPT_NODE     	0x04
#define OPT_NODES     	0x05
#define OPT_FORMAT    	0x06
#define OPT_VERBOSE   	0x07
#define OPT_ITERATE   	0x08
#define OPT_EXACT   	0x09
#define OPT_LONG    	0x0a
#define OPT_SHORT  	0x0b
#define OPT_NO_HEAD   	0x0c
#define OPT_VERSION     0x0d
#define OPT_SORT    	0x0e

/* FUNCTIONS */
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static char *_get_prefix(char *token);
static int   _parse_format( char* );
static int   _parse_state(char *str, uint16_t *states);
static void  _parse_token( char *token, char *field, int *field_size, 
                           bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _print_version( void );

/*
 * parse_command_line
 */
int parse_command_line(int argc, char *argv[])
{
	/* { long-option, short-option, argument type, variable address, 
	   option tag, docstr, argstr } */

	poptContext context;
	int curr_opt;
	int rc = 0;
	char *env_val = NULL;

	/* Declare the Options */
	static const struct poptOption options[] = {
		{"exact", 'e', POPT_ARG_NONE, &params.exact_match, OPT_EXACT,
			"group nodes only on exact match of configuration",
			NULL},
		{"iterate", 'i', POPT_ARG_INT, &params.iterate,
			OPT_ITERATE, "specify an interation period", 
			"seconds"},
		{"states", 't', POPT_ARG_STRING, &params.states,
			OPT_NODE_STATE, 
			"specify the what states of nodes to view",
			"node_state"},
		{"partition", 'p', POPT_ARG_STRING, &params.partition,
			OPT_PARTITION, "report on specific partition", 
			"PARTITION"},
		{"nodes", 'n', POPT_ARG_STRING, &params.nodes, OPT_NODES,
			"report on specific node(s)", "NODES"},
		{"Node", 'N', POPT_ARG_NONE, &params.node_flag, OPT_NODE,
			"Node-centric format", NULL},
		{"long", 'l', POPT_ARG_NONE, &params.long_output,
			OPT_LONG, "long output - displays more information",
			NULL},
		{"sort", 'S', POPT_ARG_STRING, &params.sort, OPT_SORT,
			"comma seperated list of fields to sort on", "fields"},
		{"summarize", 's', POPT_ARG_NONE, &params.summarize,
			OPT_SHORT,"report state summary only", NULL},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose,
			OPT_VERBOSE, "verbosity level", "level"},
		{"noheader", 'h', POPT_ARG_NONE, &params.no_header, 
			OPT_NO_HEAD, "no headers on output", NULL},
		{"format", 'o', POPT_ARG_STRING, &params.format, OPT_FORMAT, 
			"format specification", "format"},
		{"version", 'V', POPT_ARG_NONE, 0, OPT_VERSION,
			"output version information and exit", NULL},
		POPT_AUTOHELP 
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end */
	};

	/* Initial the popt contexts */
	context = poptGetContext("sinfo", argc, (const char **) argv,
				 options, POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(context, "[-elNsv]");

	while ((curr_opt = poptGetNextOpt(context)) > 0) {
		switch ( curr_opt )
		{
			case OPT_NODE_STATE:
				params.state_list = _build_state_list( params.states );
				break;
			case OPT_VERSION:
				_print_version();
				exit(0);
				break;	
			default:
				break;	
		}
	}
	if (curr_opt < -1) {
		const char *bad_opt;
		bad_opt = poptBadOption(context, POPT_BADOPTION_NOALIAS);
		fprintf(stderr, "bad argument %s: %s\n", bad_opt,
			poptStrerror(curr_opt));
		fprintf(stderr, "Try \"%s --help\" for more information\n",
			argv[0]);
		exit(1);
	}

	if ( ( params.format == NULL ) && 
	     ( env_val = getenv("SINFO_FORMAT") ) )
		params.format = xstrdup(env_val);

	if ( ( params.partition == NULL ) && 
	     ( env_val = getenv("SINFO_PARTITION") ) )
		params.partition = xstrdup(env_val);

	if ( ( params.partition == NULL ) && 
	     ( env_val = getenv("SINFO_SORT") ) )
		params.sort = xstrdup(env_val);

	if ( params.format == NULL ) {
		if ( params.summarize ) 
			params.format = "%9P %.5a %.9l %.15F  %N";
		else if ( params.node_flag ) {
			params.node_field_flag = true;	/* compute size later */
			if ( params.long_output ) {
				params.format = "%N %.5D %.9P %.11T %.4c "
					        "%.6m %.8d %.6w %.8f %20R";
			} else {
				params.format = "%N %.5D %.9P %6t";
			}
		} else {
			if ( params.long_output )
				params.format = "%9P %.5a %.9l %.8s %.4r %.5h "
					        "%.10g %.5D %.11T %N";
			else
				params.format = "%9P %.5a %.9l %.5D %.6t %N";
		}
	}
	_parse_format( params.format );

	if (params.nodes || params.partition || params.state_list)
		params.filtering = true;

	if (params.verbose)
		_print_options();
	return rc;
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
	uint16_t *state_id;

	if ( str == NULL)
		return NULL;
	if ( strcasecmp( str, "all" ) == 0 )
		return _build_all_states_list ();

	my_list = list_create( NULL );
	my_state_list = xstrdup( str );
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state) 
	{
		state_id = xmalloc( sizeof( uint16_t ) );
		if ( _parse_state( state, state_id ) != SLURM_SUCCESS )
			exit( 1 );
		list_append( my_list, state_id );
		state = strtok_r( NULL, ",", &tmp_char );
	}
	xfree( my_state_list );
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
	uint16_t *state_id;

	my_list = list_create( NULL );
	for (i = 0; i<NODE_STATE_END; i++) {
		state_id = xmalloc( sizeof( uint16_t ) );
		*state_id = (uint16_t) i;
		list_append( my_list, state_id );
	}
	return my_list;
}

/*
 * _parse_state - convert node state name string to numeric value
 * IN str - state name
 * OUT states - node_state value corresponding to str
 * RET 0 or error code
 */
static int
_parse_state( char* str, uint16_t* states )
{	
	int i;
	char *state_names;

	for (i = 0; i<NODE_STATE_END; i++) {
		if (strcasecmp (node_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
		if (strcasecmp (node_state_string_compact(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
	}

	fprintf (stderr, "Invalid node state specified: %s\n", str);
	state_names = xstrdup(node_state_string(0));
	for (i=1; i<NODE_STATE_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, node_state_string(i));
	}
	fprintf (stderr, "Valid node states include: %s\n", state_names);
	xfree (state_names);
	return SLURM_ERROR;
}

/* Take the user's format specification and use it to build build the 
 *	format specifications (internalize it to print.c data structures) */
static int 
_parse_format( char* format )
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
	if ((prefix = _get_prefix(format)))
		format_add_prefix( params.format_list, 0, 0, prefix); 

	tmp_format = xstrdup( format );
	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		_parse_token( token, field, &field_size, &right_justify, 
			      &suffix);
		if        (field[0] == 'a') {
			params.match_flags.avail_flag = true;
			format_add_avail( params.format_list,
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'A') {
			format_add_nodes_ai( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'c') {
			format_add_cpus( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'd') {
			format_add_disk( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'D') {
			format_add_nodes( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'f') {
			params.match_flags.features_flag = true;
			format_add_features( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'F') {
			format_add_nodes_aiot( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'g') {
			params.match_flags.groups_flag = true;
			format_add_groups( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'h') {
			params.match_flags.share_flag = true;
			format_add_share( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'l') {
			params.match_flags.max_time_flag = true;
			format_add_time( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'm') {
			format_add_memory( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'N') {
			format_add_node_list( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'P') {
			params.match_flags.partition_flag = true;
			format_add_partition( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'r') {
			params.match_flags.root_flag = true;
			format_add_root( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'R') {
			params.match_flags.reason_flag = true;
			format_add_reason( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 's') {
			params.match_flags.job_size_flag = true;
			format_add_size( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 't') {
			params.match_flags.state_flag = true;
			format_add_state_compact( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'T') {
			params.match_flags.state_flag = true;
			format_add_state_long( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'w') {
			format_add_weight( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else
			fprintf(stderr, "Invalid node format specification: %c\n", 
			        field[0] );
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
void _print_options( void )
{
	printf("-----------------------------\n");
	printf("exact       = %d\n", params.exact_match);
	printf("filtering   = %s\n", params.filtering ? "true" : "false");
	printf("format      = %s\n", params.format);
	printf("iterate     = %d\n", params.iterate );
	printf("long        = %s\n", params.long_output ? "true" : "false");
	printf("no_header   = %s\n", params.no_header   ? "true" : "false");
	printf("node_field  = %s\n", params.node_field_flag ? 
					"true" : "false");
	printf("node_format = %s\n", params.node_flag   ? "true" : "false");
	printf("nodes       = %s\n", params.nodes ? params.nodes : "n/a");
	printf("partition   = %s\n", params.partition ? 
					params.partition: "n/a");
	printf("states      = %s\n", params.states);
	printf("sort        = %s\n", params.sort);
	printf("summarize   = %s\n", params.summarize   ? "true" : "false");
	printf("verbose     = %d\n", params.verbose);
	printf("-----------------------------\n");
	printf("avail_flag      = %s\n", params.match_flags.avail_flag ?
			"true" : "false");
	printf("features_flag   = %s\n", params.match_flags.features_flag ?
			"true" : "false");
	printf("groups_flag     = %s\n", params.match_flags.groups_flag ?
					"true" : "false");
	printf("job_size_flag   = %s\n", params.match_flags.job_size_flag ?
					"true" : "false");
	printf("max_time_flag   = %s\n", params.match_flags.max_time_flag ?
					"true" : "false");
	printf("partition_flag  = %s\n", params.match_flags.partition_flag ?
			"true" : "false");
	printf("reason_flag     = %s\n", params.match_flags.reason_flag ?
			"true" : "false");
	printf("root_flag       = %s\n", params.match_flags.root_flag ?
			"true" : "false");
	printf("share_flag      = %s\n", params.match_flags.share_flag ?
			"true" : "false");
	printf("state_flag      = %s\n", params.match_flags.state_flag ?
			"true" : "false");
	printf("-----------------------------\n\n");
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

