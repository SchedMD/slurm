/*****************************************************************************\
 *  options.c - option functions for sstat
 *
 *  $Id: options.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "src/common/read_config.h"
#include "src/common/proc_args.h"
#include "sstat.h"
#include <time.h>

void _help_fields_msg(void);
void _help_msg(void);
void _usage(void);
void _init_params();

void _help_fields_msg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf("  ");
		else if(i)
			printf("\n");
		printf("%-12s", fields[i].name);
	}
	printf("\n");
	return;
}

void _help_msg(void)
{
	printf("\
sstat [<OPTION>] -j <job(.stepid)>                                          \n\
    Valid <OPTION> values are:                                              \n\
      -a, --allsteps:                                                       \n\
                   Print all steps for the given job(s) when no step is     \n\
                   specified.                                               \n\
      -e, --helpformat:                                                     \n\
	           Print a list of fields that can be specified with the    \n\
	           '--format' option                                        \n\
     -h, --help:   Print this description of use.                           \n\
     -j, --jobs:                                                            \n\
	           Format is <job(.step)>. Stat this job step               \n\
                   or comma-separated list of job steps. This option is     \n\
                   required.  The step portion will default to step 0 if not\n\
                   specified, unless the --allsteps flag is set where not   \n\
                   specifying a step will result in all running steps to be  \n\
                   displayed.                                               \n\
     -n, --noheader:                                                        \n\
	           No header will be added to the beginning of output.      \n\
                   The default is to print a header.                        \n\
     -o, --format:                                                          \n\
	           Comma separated list of fields. (use \"--helpformat\"    \n\
                   for a list of available fields).                         \n\
     -p, --parsable: output will be '|' delimited with a '|' at the end     \n\
     -P, --parsable2: output will be '|' delimited without a '|' at the end \n\
     --usage:      Display brief usage message.                             \n\
     -v, --verbose:                                                         \n\
	           Primarily for debugging purposes, report the state of    \n\
                   various variables during processing.                     \n\
     -V, --version: Print version.                                          \n\
\n");

	return;
}

void _usage(void)
{
	printf("Usage: sstat [options] -j <job(.stepid)>\n"
	       "\tUse --help for help\n");
}


void _do_help(void)
{
	switch (params.opt_help) {
	case 1:
		_help_msg();
		break;
	case 2:
		_help_fields_msg();
		break;
	case 3:
		_usage();
		break;
	default:
		fprintf(stderr, "sacct bug: params.opt_help=%d\n",
			params.opt_help);
	}
}

void _init_params()
{
	memset(&params, 0, sizeof(sstat_parameters_t));
}

/* returns number of objects added to list */
static int _addto_job_list(List job_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *dot = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	jobacct_selected_step_t *curr_step = NULL;

	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!job_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(job_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if(quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if(names[i] == ',') {
				if((i-start) > 0) {
					char *dot = NULL;
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					selected_step = xmalloc(
						sizeof(jobacct_selected_step_t));
					dot = strstr(name, ".");
					if (dot == NULL) {
						debug2("No jobstep requested");
						selected_step->stepid = NO_VAL;
					} else {
						*dot++ = 0;
						selected_step->stepid =
							atoi(dot);
					}
					selected_step->jobid = atoi(name);
					xfree(name);

					while((curr_step = list_next(itr))) {
						if((curr_step->jobid
						    == selected_step->jobid)
						   && (curr_step->stepid
						       == selected_step->
						       stepid))
							break;
					}

					if(!curr_step) {
						list_append(job_list,
							    selected_step);
						count++;
					} else
						destroy_jobacct_selected_step(
							selected_step);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));

			selected_step =
				xmalloc(sizeof(jobacct_selected_step_t));
			dot = strstr(name, ".");
			if (dot == NULL) {
				debug2("No jobstep requested");
				selected_step->stepid = NO_VAL;
			} else {
				*dot++ = 0;
				selected_step->stepid = atoi(dot);
			}
			selected_step->jobid = atoi(name);
			xfree(name);

			while((curr_step = list_next(itr))) {
				if((curr_step->jobid == selected_step->jobid)
				   && (curr_step->stepid
				       == selected_step->stepid))
					break;
			}

			if(!curr_step) {
				list_append(job_list, selected_step);
				count++;
			} else
				destroy_jobacct_selected_step(
					selected_step);
		}
	}
	list_iterator_destroy(itr);
	return count;
}

int decode_state_char(char *state)
{
	if (!strcasecmp(state, "p"))
		return JOB_PENDING; 	/* we should never see this */
	else if (!strcasecmp(state, "r"))
		return JOB_RUNNING;
	else if (!strcasecmp(state, "su"))
		return JOB_SUSPENDED;
	else if (!strcasecmp(state, "cd"))
		return JOB_COMPLETE;
	else if (!strcasecmp(state, "ca"))
		return JOB_CANCELLED;
	else if (!strcasecmp(state, "f"))
		return JOB_FAILED;
	else if (!strcasecmp(state, "to"))
		return JOB_TIMEOUT;
	else if (!strcasecmp(state, "nf"))
		return JOB_NODE_FAIL;
	else
		return -1; // unknown
}

void parse_command_line(int argc, char **argv)
{
	extern int optind;
	int c, i, optionIndex = 0;
	char *end = NULL, *start = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	static struct option long_options[] = {
		{"allsteps", 0, 0, 'a'},
		{"helpformat", 0, 0, 'e'},
		{"help", 0, 0, 'h'},
		{"jobs", 1, 0, 'j'},
		{"noheader", 0, 0, 'n'},
		{"fields", 1, 0, 'o'},
		{"format", 1, 0, 'o'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"usage", 0, &params.opt_help, 3},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0}};

	log_init(xbasename(argv[0]), logopt, 0, NULL);

	_init_params();

	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv, "aehj:no:pPvV",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			params.opt_all_steps = 1;
			break;
		case 'e':
			params.opt_help = 2;
			break;
		case 'h':
			params.opt_help = 1;
			break;
		case 'j':
			if ((strspn(optarg, "0123456789, ") < strlen(optarg))
			    && (strspn(optarg, ".0123456789, ")
				< strlen(optarg))) {
				fprintf(stderr, "Invalid jobs list: %s\n",
					optarg);
				exit(1);
			}
			if(!params.opt_job_list)
				params.opt_job_list = list_create(
					destroy_jobacct_selected_step);
			_addto_job_list(params.opt_job_list, optarg);
			break;
		case 'n':
			print_fields_have_header = 0;
			break;
		case 'o':
			xstrfmtcat(params.opt_field_list, "%s,", optarg);
			break;
		case 'p':
			print_fields_parsable_print =
				PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case 'P':
			print_fields_parsable_print =
				PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case 'v':
			/* Handle -vvv thusly...
			 * 0 - report only normal messages and errors
			 * 1 - report options selected and major operations
			 * 2 - report data anomalies probably not errors
			 * 3 - blather on and on
			 */
			params.opt_verbose++;
			break;

		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case ':':
		case '?':	/* getopt() has explained it */
			exit(1);
		}
	}

	if(params.opt_help) {
		_do_help();
		exit(0);
	}

	if (optind < argc) {
		optarg = argv[optind];
		if ((strspn(optarg, "0123456789, ") < strlen(optarg))
		    && (strspn(optarg, ".0123456789, ")
			< strlen(optarg))) {
			fprintf(stderr, "Invalid jobs list: %s\n",
				optarg);
			exit(1);
		}
		if(!params.opt_job_list)
			params.opt_job_list = list_create(
				destroy_jobacct_selected_step);
		_addto_job_list(params.opt_job_list, optarg);
	}

	if(!params.opt_field_list)
		xstrfmtcat(params.opt_field_list, "%s,", STAT_FIELDS);

	if (params.opt_verbose) {
		logopt.stderr_level += params.opt_verbose;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	/* specific jobs requested? */
	if (params.opt_verbose && params.opt_job_list
	    && list_count(params.opt_job_list)) {
		debug("Jobs requested:\n");
		itr = list_iterator_create(params.opt_job_list);
		while((selected_step = list_next(itr))) {
			if(selected_step->stepid != NO_VAL)
				debug("\t: %d.%d\n",
					selected_step->jobid,
					selected_step->stepid);
			else
				debug("\t: %d\n",
					selected_step->jobid);
		}
		list_iterator_destroy(itr);
	}

	start = params.opt_field_list;
	while ((end = strstr(start, ","))) {
		*end = 0;
		while (isspace(*start))
			start++;	/* discard whitespace */
		if(!(int)*start)
			continue;
		for (i = 0; fields[i].name; i++) {
			if (!strcasecmp(fields[i].name, start))
				goto foundfield;
		}
		error("Invalid field requested: \"%s\"", start);
		exit(1);
	foundfield:
		list_append(print_fields_list, &fields[i]);
		start = end + 1;
	}
	field_count = list_count(print_fields_list);

	if (optind < argc) {
		debug2("Error: Unknown arguments:");
		for (i=optind; i<argc; i++)
			debug2(" %s", argv[i]);
		debug2("\n");
		exit(1);
	}

	return;
}


