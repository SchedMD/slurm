/****************************************************************************\
 *  opts.c - sinfo command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
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
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP   0x100
#define OPT_LONG_USAGE  0x101
#define OPT_LONG_HIDE	0x102

/* FUNCTIONS */
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static char *_get_prefix(char *token);
static void  _help( void );
static int   _parse_format( char* );
static bool  _node_state_equal (int state_id, const char *state_str);
static int   _node_state_id (char *str);
static const char * _node_state_list (void);
static void  _parse_token( char *token, char *field, int *field_size, 
                           bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _print_version( void );
static void  _usage( void );

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	char *env_val = NULL;
	int opt_char;
	int option_index;
	hostlist_t host_list;
	static struct option long_options[] = {
		{"all",       no_argument,       0, 'a'},
		{"bg",        no_argument,       0, 'b'},
		{"dead",      no_argument,       0, 'd'},
		{"exact",     no_argument,       0, 'e'},
		{"noheader",  no_argument,       0, 'h'},
		{"iterate",   required_argument, 0, 'i'},
		{"long",      no_argument,       0, 'l'},
		{"nodes",     required_argument, 0, 'n'},
		{"Node",      no_argument,       0, 'N'},
		{"format",    required_argument, 0, 'o'},
		{"partition", required_argument, 0, 'p'},
		{"responding",no_argument,       0, 'r'},
		{"list-reasons", no_argument,    0, 'R'},
		{"summarize", no_argument,       0, 's'},
		{"sort",      required_argument, 0, 'S'},
		{"states",    required_argument, 0, 't'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{"hide",      no_argument,       0, OPT_LONG_HIDE},
		{NULL,        0,                 0, 0}
	};

	if (getenv("SINFO_ALL"))
		params.all_flag = true;
	if ( ( env_val = getenv("SINFO_PARTITION") ) )
		params.partition = xstrdup(env_val);
	if ( ( env_val = getenv("SINFO_SORT") ) )
		params.sort = xstrdup(env_val);


	while((opt_char = getopt_long(argc, argv, "abdehi:ln:No:p:rRsS:t:vV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, 
				"Try \"sinfo --help\" for more information\n");
			exit(1);
			break;
		case (int)'a':
			params.all_flag = true;
			break;
		case (int)'b':
#ifdef HAVE_BG
			params.bg_flag = true;
#else
			error("must be on a BG system to use --bg option");
			exit(1);
#endif
			break;
		case (int)'d':
			params.dead_nodes = true;
			break;
		case (int)'e':
			params.exact_match = true;
			break;
		case (int)'h':
			params.no_header = true;
			break;
		case (int) 'i':
			params.iterate= atoi(optarg);
			if (params.iterate <= 0) {
				error ("Error: invalid entry for "
				       "--iterate=%s", optarg);
				exit(1);
			}
			break;
		case (int) 'l':
			params.long_output = true;
			break;
		case (int) 'n':
			xfree(params.nodes);
			params.nodes= xstrdup(optarg);
			/*
			 * confirm valid nodelist entry
			 */
			host_list = hostlist_create(params.nodes);
			if (!host_list) {
				error("'%s' invalid entry for --nodes",
				      optarg);
				exit(1);
			}
			hostlist_destroy(host_list);
			break;
		case (int) 'N':
			params.node_flag = true;
			break;
		case (int) 'o':
			xfree(params.format);
			params.format = xstrdup(optarg);
			break;
		case (int) 'p':
			xfree(params.partition);
			params.partition = xstrdup(optarg);
			break;
		case (int) 'r':
			params.responding_nodes = true;
			break;
		case (int) 'R':
			params.list_reasons = true;
			break;
		case (int) 's':
			params.summarize = true;
			break;
		case (int) 'S':
			xfree(params.sort);
			params.sort = xstrdup(optarg);
			break;
		case (int) 't':
			xfree(params.states);
			params.states = xstrdup(optarg);
			if (!(params.state_list = _build_state_list(optarg))) {
				error ("valid states: %s", _node_state_list ());
				exit (1);
			}
			break;
		case (int) 'v':
			params.verbose++;
			break;
		case (int) 'V':
			_print_version();
			exit(0);
		case (int) OPT_LONG_HELP:
			_help();
			exit(0);
		case (int) OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_HIDE:
			params.all_flag = false;
			break;
		}
	}

	if ( params.format == NULL ) {
		if ( params.summarize ) {
#ifdef HAVE_BG
			params.format = "%9P %.5a %.10l %.32F  %N";
#else
			params.format = "%9P %.5a %.10l %.15F  %N";
#endif
		} else if ( params.node_flag ) {
			params.node_field_flag = true;	/* compute size later */
			params.format = params.long_output ?
			  "%N %.6D %.9P %.11T %.4c %.8z %.6m %.8d %.6w %.8f %20R" :
			  "%N %.6D %.9P %6t";

		} else if (params.list_reasons) {
			params.format = params.long_output ?  
			  "%50R %6t %N" : 
			  "%50R %N";

		} else if ((env_val = getenv ("SINFO_FORMAT"))) {
			params.format = xstrdup(env_val);

		} else {
			params.format = params.long_output ? 
			  "%9P %.5a %.10l %.10s %.4r %.5h %.10g %.6D %.11T %N" :
			  "%9P %.5a %.10l %.5D %.6t %N";
		}
	}
	_parse_format( params.format );

	if (params.list_reasons && (params.state_list == NULL)) {
		params.states = xstrdup ("down,drain");
		if (!(params.state_list = _build_state_list (params.states)))
			fatal ("Unable to build state list for -R!");
	}

	if (params.dead_nodes || params.nodes || params.partition || 
			params.responding_nodes ||params.state_list)
		params.filtering = true;

	if (params.verbose)
		_print_options();
}

static char *
_next_tok (char *sep, char **str)
{
	char *tok;

        /* push str past any leading separators */
        while ((**str != '\0') && (strchr(sep, **str) != '\0'))
                (*str)++;

        if (**str == '\0')
                return NULL;

        /* assign token ptr */
        tok = *str;

        /* push str past token and leave pointing to first separator */
        while ((**str != '\0') && (strchr(sep, **str) == '\0'))
                (*str)++;

        /* nullify consecutive separators and push str beyond them */
        while ((**str != '\0') && (strchr(sep, **str) != '\0'))
                *(*str)++ = '\0';

        return (tok);
}

/*
 * _build_state_list - build a list of node states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static List 
_build_state_list (char *state_str)
{
	List state_ids;
	char *orig, *str, *state;

	if (state_str == NULL)
		return NULL;
	if (strcasecmp (state_str, "all") == 0 )
		return _build_all_states_list ();

	orig = str = xstrdup (state_str);
	state_ids = list_create (NULL);

	while ((state = _next_tok (",", &str))) {
		int *id = xmalloc (sizeof (*id));
		if ((*id = _node_state_id (state)) < 0) {
			error ("Bad state string: \"%s\"", state);
			return (NULL);
		}
		list_append (state_ids, id);
	}

	xfree (orig);
	return (state_ids);
}

/*
 * _build_all_states_list - build a list containing all possible node states
 * RET List of enum job_states values
 */
static List 
_build_all_states_list( void )
{
	List my_list;
	int i;
	uint16_t *state_id;

	my_list = list_create( NULL );
	for (i = 0; i < NODE_STATE_END; i++) {
		state_id = xmalloc( sizeof( uint16_t ) );
		*state_id = (uint16_t) i;
		list_append( my_list, state_id );
	}

	state_id = xmalloc( sizeof( uint16_t ) );
	*state_id = NODE_STATE_DRAIN;
	list_append( my_list, state_id );

	state_id = xmalloc( sizeof( uint16_t ) );
	*state_id = NODE_STATE_COMPLETING;
	list_append( my_list, state_id );

	return my_list;
}

static const char *
_node_state_list (void)
{
	int i;
	static char *all_states = NULL; 

	if (all_states)
		return (all_states);

	all_states = xstrdup (node_state_string_compact (0));
	for (i = 1; i < NODE_STATE_END; i++) {
		xstrcat (all_states, ",");
		xstrcat (all_states, node_state_string_compact(i));
	}

	xstrcat (all_states, ",");
	xstrcat (all_states, 
		node_state_string_compact(NODE_STATE_DRAIN));

	xstrcat (all_states, ",");
	xstrcat (all_states, 
		node_state_string_compact(NODE_STATE_COMPLETING));

	for (i = 0; i < strlen (all_states); i++)
		all_states[i] = tolower (all_states[i]);

	return (all_states);
}


static bool
_node_state_equal (int i, const char *str)
{
	int len = strlen (str);

	if (  (strncasecmp (node_state_string_compact(i), str, len) == 0) 
	   || (strncasecmp (node_state_string(i),         str, len) == 0)) 
		return (true);
	return (false);
}

/*
 * _parse_state - convert node state name string to numeric value
 * IN str - state name
 * OUT states - node_state value corresponding to str
 * RET 0 or error code
 */
static int 
_node_state_id (char *str)
{	
	int i;
	for (i = 0; i < NODE_STATE_END; i++) {
		if (_node_state_equal (i, str))
			return (i);
	}

	if  (_node_state_equal (NODE_STATE_DRAIN, str))
		return NODE_STATE_DRAIN;

	if (_node_state_equal (NODE_STATE_COMPLETING, str))
		return NODE_STATE_COMPLETING;

	return (-1);
}

/* Take the user's format specification and use it to build build the 
 *	format specifications (internalize it to print.c data structures) */
static int 
_parse_format( char* format )
{
	int field_size;
	bool right_justify;
	char *prefix = NULL, *suffix = NULL, *token = NULL,
		*tmp_char = NULL, *tmp_format = NULL;
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
			params.match_flags.cpus_flag = true;
			format_add_cpus( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'C') {
			params.match_flags.cpus_flag = true;
			format_add_cpus_aiot( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'd') {
			params.match_flags.disk_flag = true;
			format_add_disk( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'D') {
			format_add_nodes( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		}
/*		else if (field[0] == 'E') see 'R' below */
		else if (field[0] == 'f') {
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
		} else if (field[0] == 'L') {
			params.match_flags.default_time_flag = true;
			format_add_default_time( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'm') {
			params.match_flags.memory_flag = true;
			format_add_memory( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'N') {
			format_add_node_list( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'p') {
			params.match_flags.priority_flag = true;
			format_add_priority( params.format_list, 
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
		} else if ((field[0] == 'E') || (field[0] == 'R')) {
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
			params.match_flags.weight_flag = true;
			format_add_weight( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'X') {
			params.match_flags.sockets_flag = true;
			format_add_sockets( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'Y') {
			params.match_flags.cores_flag = true;
			format_add_cores( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'Z') {
			params.match_flags.threads_flag = true;
			format_add_threads( params.format_list, 
					field_size, 
					right_justify, 
					suffix );
		} else if (field[0] == 'z') {
			params.match_flags.sct_flag = true;
			format_add_sct( params.format_list, 
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
	printf("dead        = %s\n", params.dead_nodes ? "true" : "false");
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
	printf("responding  = %s\n", params.responding_nodes ?
					"true" : "false");
	printf("states      = %s\n", params.states);
	printf("sort        = %s\n", params.sort);
	printf("summarize   = %s\n", params.summarize   ? "true" : "false");
	printf("verbose     = %d\n", params.verbose);
	printf("-----------------------------\n");
	printf("all_flag        = %s\n", params.all_flag ? "true" : "false");
	printf("avail_flag      = %s\n", params.match_flags.avail_flag ?
			"true" : "false");
	printf("bg_flag         = %s\n", params.bg_flag ? "true" : "false");
	printf("cpus_flag       = %s\n", params.match_flags.cpus_flag ?
			"true" : "false");
	printf("default_time_flag =%s\n", params.match_flags.default_time_flag ?
					"true" : "false");
	printf("disk_flag       = %s\n", params.match_flags.disk_flag ?
			"true" : "false");
	printf("features_flag   = %s\n", params.match_flags.features_flag ?
			"true" : "false");
	printf("groups_flag     = %s\n", params.match_flags.groups_flag ?
					"true" : "false");
	printf("job_size_flag   = %s\n", params.match_flags.job_size_flag ?
					"true" : "false");
	printf("max_time_flag   = %s\n", params.match_flags.max_time_flag ?
					"true" : "false");
	printf("memory_flag     = %s\n", params.match_flags.memory_flag ?
			"true" : "false");
	printf("partition_flag  = %s\n", params.match_flags.partition_flag ?
			"true" : "false");
	printf("priority_flag   = %s\n", params.match_flags.priority_flag ?
			"true" : "false");
	printf("reason_flag     = %s\n", params.match_flags.reason_flag ?
			"true" : "false");
	printf("root_flag       = %s\n", params.match_flags.root_flag ?
			"true" : "false");
	printf("share_flag      = %s\n", params.match_flags.share_flag ?
			"true" : "false");
	printf("state_flag      = %s\n", params.match_flags.state_flag ?
			"true" : "false");
	printf("weight_flag     = %s\n", params.match_flags.weight_flag ?
			"true" : "false");
	printf("-----------------------------\n\n");
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage( void )
{
	printf("\
Usage: sinfo [-abdelNRrsv] [-i seconds] [-t states] [-p partition] [-n nodes]\n\
             [-S fields] [-o format] \n");
}

static void _help( void )
{
	printf ("\
Usage: sinfo [OPTIONS]\n\
  -a, --all                  show all partitions (including hidden and those\n\
                             not accessible)\n\
  -b, --bg                   show bgblocks (on Blue Gene systems)\n\
  -d, --dead                 show only non-responding nodes\n\
  -e, --exact                group nodes only on exact match of configuration\n\
  -h, --noheader             no headers on output\n\
  -hide                      do not show hidden or non-accessible partitions\n\
  -i, --iterate=seconds      specify an interation period\n\
  -l, --long                 long output - displays more information\n\
  -n, --nodes=NODES          report on specific node(s)\n\
  -N, --Node                 Node-centric format\n\
  -o, --format=format        format specification\n\
  -p, --partition=PARTITION  report on specific partition\n\
  -r, --responding           report only responding nodes\n\
  -R, --list-reasons         list reason nodes are down or drained\n\
  -s, --summarize            report state summary only\n\
  -S, --sort=fields          comma seperated list of fields to sort on\n\
  -t, --states=node_state    specify the what states of nodes to view\n\
  -v, --verbose              verbosity level\n\
  -V, --version              output version information and exit\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
