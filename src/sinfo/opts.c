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
#include <src/sinfo/sinfo.h>

#define OPT_SUMMARIZE 	0x01
#define OPT_NODE_STATE 	0x02
#define OPT_PARTITION	0x03
#define OPT_NODE     	0x04
#define OPT_FORMAT    	0x05
#define OPT_VERBOSE   	0x06

extern struct sinfo_parameters params;

/*
 * parse_command_line
 */
int
parse_command_line( int argc, char* argv[] )
{
	/* { long-option, short-option, argument type, variable address, option tag, docstr, argstr } */

	poptContext context;
	char next_opt;
	int rc = 0;

	/* Declare the Options */
	static const struct poptOption options[] = 
	{
		{"state", 't', POPT_ARG_STRING, &params.state, OPT_NODE_STATE, "specify the what state of nodes to view", "state"},
		{"partition", 'p', POPT_ARG_STRING, &params.partition, OPT_PARTITION,"specify a partition", "partition"},
		{"node", 'n', POPT_ARG_STRING, &params.node, OPT_NODE,"specify a specific node", "node"},
		{"format", 'o', POPT_ARG_STRING, &params.format, OPT_FORMAT, "format string", "format string"},
		{"summarize", 's', POPT_ARG_NONE, &params.summarize, OPT_VERBOSE, "summarize partitition information, do not show individual nodes", NULL},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose, OPT_VERBOSE, "verbosity level", "level"},
		POPT_AUTOHELP 
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end the list */
	};

	/* Initial the popt contexts */
	context = poptGetContext(NULL, argc, (const char**)argv, options, 0);

	while ( ( next_opt = poptGetNextOpt(context) ) > -1  )
	{
		switch ( next_opt )
		{
			case OPT_SUMMARIZE:
				break;	
				
			case OPT_NODE_STATE:
				break;	
				
			case OPT_PARTITION:
				break;	
				
			case OPT_NODE:
				break;	
				
			case OPT_FORMAT:
				break;	
				
			case OPT_VERBOSE:
				break;	

			default:
				break;	
		}
	}
	
	return rc;
}

/*
 * parse_state - parse state information
 * input - char* comma seperated list of states
 */
int
parse_state( char* str, enum node_states* states )
{	
	/* FIXME - this will eventually return an array of enums
	 */

	if ( strcasecmp( str, node_state_string( NODE_STATE_DOWN )) == 0 )
		*states = NODE_STATE_DOWN;
	else if ( strcasecmp( str, node_state_string( NODE_STATE_UNKNOWN )) == 0 )
		*states = NODE_STATE_UNKNOWN;
	else if ( strcasecmp( str, node_state_string( NODE_STATE_IDLE )) == 0 )
		*states = NODE_STATE_IDLE;
	else if ( strcasecmp( str, node_state_string( NODE_STATE_ALLOCATED )) == 0 )
		*states = NODE_STATE_ALLOCATED;
	else if ( strcasecmp( str, node_state_string( NODE_STATE_DRAINED )) == 0 )
		*states = NODE_STATE_DRAINED;
	else if ( strcasecmp( str, node_state_string( NODE_STATE_DRAINING )) == 0 )
		*states = NODE_STATE_DRAINING;
	else return SLURM_ERROR;

	return SLURM_SUCCESS;
}

void 
print_options()
{
	printf( "-----------------------------\n" );
	printf( "partition = %s\n", params.partition ) ;
	printf( "state = %s\n",  params.state );
	printf( "summarize = %s\n", params.summarize ? "true" : "false" );
	printf( "node = %s\n", params.node );
	printf( "verbose = %d\n", params.verbose );
	printf( "format = %s\n", params.format );
	printf( "-----------------------------\n\n\n" );
} ;


