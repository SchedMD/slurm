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

#include <popt.h>
#include <src/sinfo/sinfo.h>

#define OPT_SUMMARIZE 	0x01
#define OPT_NODE_STATE 	0x02
#define OPT_PARTITION	0x03
#define OPT_NODE     	0x04
#define OPT_FORMAT    	0x05
#define OPT_VERBOSE   	0x06
#define OPT_ITERATE   	0x07

int parse_state( char* str, enum node_states* states );

extern struct sinfo_parameters params;
char *temp_state;

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
		{"iterate", 'i', POPT_ARG_INT, &params.iterate, OPT_ITERATE, "specify an interation period", "seconds"},
		{"state", 't', POPT_ARG_STRING, &temp_state, OPT_NODE_STATE, "specify the what state of nodes to view", "NODE_STATE"},
		{"partition", 'p', POPT_ARG_NONE, &params.partition_flag, OPT_PARTITION,"show partition information and optionally specify a specific partition", "PARTITION"},
		{"node", 'n', POPT_ARG_NONE, &params.node_flag, OPT_NODE, "specify a specific node", "NODE"},
		{"long", 'l', POPT_ARG_NONE, &params.long_output, OPT_FORMAT, "long output - displays more information", NULL},
		{"summarize", 's', POPT_ARG_NONE, &params.summarize, OPT_VERBOSE, "summarize partitition information, do not show individual nodes", NULL},
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
			case OPT_PARTITION:
				if ( (opt_arg = poptGetArg( context )) != NULL )
				{
					params.partition = (char *) opt_arg;
				}
				break;	
				
			case OPT_NODE:
				if ( (opt_arg = poptGetArg( context )) != NULL )
				{
					params.node = (char *) opt_arg;
				}
				break;
			case OPT_NODE_STATE:
				{
					params.state_flag = true;
					if ( parse_state( temp_state, &params.state ) == SLURM_ERROR )
					{
						fprintf(stderr, "%s: %s is an invalid node state\n", argv[0], temp_state);
						exit (1);
					}
				}
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

	if ( params.verbose )
		print_options();
	return rc;
}

/*
 * parse_state - parse state information
 * input - char* comma seperated list of states
 */
int
parse_state( char* str, enum node_states* states )
{	
	int i;

	for (i = 0; i <= NODE_STATE_END; i++) {
		if (strcasecmp (node_state_string(i), "END") == 0)
			return SLURM_ERROR;

		if (strcasecmp (node_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}	
	}	
	return SLURM_ERROR;
}

void 
print_options()
{
	printf( "-----------------------------\n" );
	printf( "partition(%s) = %s\n", (params.partition_flag ? "true" : "false"), params.partition );
	printf( "node(%s) = %s\n", (params.node_flag ? "true" : "false"), params.node );
	printf( "state(%s) = %s\n",  (params.state_flag ? "true" : "false"), 
				node_state_string( params.state ) );
	printf( "summarize = %s\n", params.summarize ? "true" : "false" );
	printf( "verbose = %d\n", params.verbose );
	printf( "long output = %s\n",  params.long_output ? "true" : "false" );
	printf( "-----------------------------\n\n" );
} ;


