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

#include "src/sinfo/sinfo.h"

#define OPT_NODE_STATE 	0x02
#define OPT_PARTITION	0x03
#define OPT_NODE     	0x04
#define OPT_FORMAT    	0x05
#define OPT_VERBOSE   	0x06
#define OPT_ITERATE   	0x07
#define OPT_EXACT   	0x08

static int _parse_state(char *str, enum node_states *states);

extern struct sinfo_parameters params;
char *temp_state;

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

	/* Declare the Options */
	static const struct poptOption options[] = {
		{"exact", 'e', POPT_ARG_NONE, &params.exact_match, OPT_EXACT,
		 "group nodes only on exact match of configuration",NULL},
		{"iterate", 'i', POPT_ARG_INT, &params.iterate,
		 OPT_ITERATE, "specify an interation period", "seconds"},
		{"state", 't', POPT_ARG_STRING, &temp_state,
		 OPT_NODE_STATE, "specify the what state of nodes to view",
		 "node_state"},
		{"partition", 'p', POPT_ARG_STRING, &params.partition,
		 OPT_PARTITION, "report on specific partition", "PARTITION"},
		{"nodes", 'n', POPT_ARG_STRING, &params.nodes, OPT_NODE,
		 "report on specific node(s)", "NODES"},
		{"Node", 'N', POPT_ARG_NONE, &params.node_flag, OPT_FORMAT,
		 "Node-centric format", NULL},
		{"long", 'l', POPT_ARG_NONE, &params.long_output,
		 OPT_FORMAT, "long output - displays more information",
		 NULL},
		{"summarize", 's', POPT_ARG_NONE, &params.summarize,
		 OPT_FORMAT,"report state summary only", NULL},
		{"verbose", 'v', POPT_ARG_NONE, &params.verbose,
		 OPT_VERBOSE, "verbosity level", "level"},
		POPT_AUTOHELP 
		{NULL, '\0', 0, NULL, 0, NULL, NULL} /* end */
	};

	/* Initial the popt contexts */
	context = poptGetContext("sinfo", argc, (const char **) argv,
				 options, POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(context, "[-elNsv]");

	while ((curr_opt = poptGetNextOpt(context)) > 0) {
		if (curr_opt == OPT_NODE_STATE) {
			params.state_flag = true;
			if (_parse_state(temp_state, &params.state)
			    == SLURM_ERROR) {
				fprintf(stderr,
					"%s: %s is invalid node state\n",
					argv[0], temp_state);
				exit(1);
			}
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

	if (params.verbose)
		print_options();
	return rc;
}

/*
 * _parse_state - parse state information
 * IN str - name of a job states
 * OUT states - numeric equivalent sjob state
 * RET 0 or error code
 */
static int _parse_state(char *str, enum node_states *states)
{
	int i;

	for (i = 0; i <= NODE_STATE_END; i++) {
		if (strcasecmp(node_state_string(i), "END") == 0)
			break;

		if (strcasecmp(node_state_string(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}

		if (strcasecmp(node_state_string(i | NODE_STATE_NO_RESPOND), 
			       str) == 0) {
			*states = i | NODE_STATE_NO_RESPOND;
			return SLURM_SUCCESS;
		}

		if (strcasecmp(node_state_string_compact(i), str) == 0) {
			*states = i;
			return SLURM_SUCCESS;
		}

		if (strcasecmp(node_state_string_compact(i | 
			       NODE_STATE_NO_RESPOND), str) == 0) {
			*states = i | NODE_STATE_NO_RESPOND;
			return SLURM_SUCCESS;
		}
	}
	return SLURM_ERROR;
}

/* print the parameters specified */
void print_options( void )
{
	char *node_state;
	
	if (params.state_flag)
		node_state = node_state_string(params.state);
	else
		node_state = "N/A";

	printf("-----------------------------\n");
	printf("exact       = %d\n", params.exact_match);
	printf("long format = %s\n", params.long_output ? "true" : "false");
	printf("nodes       = %s\n", params.nodes ? params.nodes : "N/A");
	printf("Node format = %s\n", params.node_flag   ? "true" : "false");
	printf("partition   = %s\n", params.partition ? 
				     params.partition: "N/A");
	printf("state       = %s\n", node_state);
	printf("summarize   = %s\n", params.summarize   ? "true" : "false");
	printf("verbose     = %d\n", params.verbose);
	printf("-----------------------------\n\n");
}
