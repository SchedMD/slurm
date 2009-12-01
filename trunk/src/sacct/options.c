/*****************************************************************************\
 *  options.c - option functions for sacct
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/parse_time.h"
#include "sacct.h"
#include <time.h>

void _help_fields_msg(void);
void _help_msg(void);
void _usage(void);
void _init_params();

int selected_state[STATE_COUNT];
List selected_parts = NULL;
List selected_steps = NULL;
void *acct_db_conn = NULL;

List print_fields_list = NULL;
ListIterator print_fields_itr = NULL;
int field_count = 0;
List qos_list = NULL;

void _help_fields_msg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf(" ");
		else if(i)
			printf("\n");
		printf("%-13s", fields[i].name);
	}
	printf("\n");
	return;
}

static char *_convert_to_id(char *name, bool gid)
{
	if(gid) {
		struct group *grp;
		if (!(grp=getgrnam(name))) {
			fprintf(stderr, "Invalid group id: %s\n", name);
			exit(1);
		}
		xfree(name);
		name = xstrdup_printf("%d", grp->gr_gid);
	} else {
		struct passwd *pwd;
		if (!(pwd=getpwnam(name))) {
			fprintf(stderr, "Invalid user id: %s\n", name);
			exit(1);
		}
		xfree(name);
		name = xstrdup_printf("%d", pwd->pw_uid);
	}
	return name;
}

/* returns number of objects added to list */
static int _addto_id_char_list(List char_list, char *names, bool gid)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
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
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					//info("got %s %d", name, i-start);
					if (!isdigit((int) *name)) {
						name = _convert_to_id(
							name, gid);
					}
					
					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}

					if(!tmp_char) {
						list_append(char_list, name);
						count++;
					} else 
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if(!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			
			if (!isdigit((int) *name)) {
				name = _convert_to_id(name, gid);
			}
			
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char) {
				list_append(char_list, name);
				count++;
			} else 
				xfree(name);
		}
	}	
	list_iterator_destroy(itr);
	return count;
} 

/* returns number of objects added to list */
static int _addto_state_char_list(List char_list, char *names)
{
	int i=0, start=0, c;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
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
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					c = decode_state_char(name);
					if (c == -1)
						fatal("unrecognized job "
						      "state value");
					xfree(name);
					name = xstrdup_printf("%d", c);
					
					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}

					if(!tmp_char) {
						list_append(char_list, name);
						count++;
					} else 
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if(!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			c = decode_state_char(name);
			if (c == -1)
				fatal("unrecognized job state value");
			xfree(name);
			name = xstrdup_printf("%d", c);
			
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char) {
				list_append(char_list, name);
				count++;
			} else 
				xfree(name);
		}
	}	
	list_iterator_destroy(itr);
	return count;
} 

/* returns number of objects added to list */
static int _addto_step_list(List step_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *dot = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	jobacct_selected_step_t *curr_step = NULL;
	
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!step_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(step_list);
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
						list_append(step_list,
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
				list_append(step_list, selected_step);
				count++;
			} else 
				destroy_jobacct_selected_step(
					selected_step);
		}
	}	
	list_iterator_destroy(itr);
	return count;
} 

void _help_msg(void)
{
	printf("\
sacct [<OPTION>]                                                            \n\
    Valid <OPTION> values are:                                              \n\
     -a, --allusers:                                                        \n\
	           Display jobs for all users. By default, only the         \n\
                   current user's jobs are displayed.  If ran by user root  \n\
                   this is the default.                                     \n\
     -A, --accounts:                                                        \n\
	           Use this comma separated list of accounts to select jobs \n\
                   to display.  By default, all accounts are selected.      \n\
     -b, --brief:                                                           \n\
	           Equivalent to '--format=jobstep,state,error'. This option\n\
	           has no effect if --dump is specified.                    \n\
     -c, --completion: Use job completion instead of accounting data.       \n\
     -C, --clusters:                                                        \n\
                   Only send data about these clusters. -1 for all clusters.\n\
     -d, --dump:   Dump the raw data records                                \n\
     -D, --duplicates:                                                      \n\
	           If SLURM job ids are reset, some job numbers will        \n\
	           probably appear more than once refering to different jobs.\n\
	           Without this option only the most recent jobs will be    \n\
                   displayed.                                               \n\
     -e, --helpformat:                                                      \n\
	           Print a list of fields that can be specified with the    \n\
	           '--format' option                                        \n\
     -E, --endtime:                                                         \n\
                   Select jobs eligible before this time.  If states are    \n\
                   given with the -s option return jobs in this state before\n\
                   this period.                                             \n\
     -f, --file=file:                                                       \n\
	           Read data from the specified file, rather than SLURM's   \n\
                   current accounting log file. (Only appliciable when      \n\
                   running the filetxt plugin.)                             \n\
     -g, --gid, --group:                                                    \n\
	           Use this comma separated list of gids or group names     \n\
                   to select jobs to display.  By default, all groups are   \n\
                   selected.                                                \n\
     -h, --help:   Print this description of use.                           \n\
     -i, --nnodes=N:                                                        \n\
                   Return jobs which ran on this many nodes (N = min[-max]) \n\
     -I, --ncpus=N:                                                         \n\
                   Return jobs which ran on this many cpus (N = min[-max])  \n\
     -j, --jobs:                                                            \n\
	           Format is <job(.step)>. Display information about this   \n\
                   job or comma-separated list of jobs. The default is all  \n\
                   jobs. Adding .step will display the specfic job step of  \n\
                   that job.                                                \n\
     -l, --long:                                                            \n\
	           Equivalent to specifying                                 \n\
	           '--fields=jobid,jobname,partition,maxvsize,maxvsizenode, \n\
                             maxvsizetask,avevsize,maxrss,maxrssnode,       \n\
                             maxrsstask,averss,maxpages,maxpagesnode,       \n\
                             maxpagestask,avepages,mincpu,mincpunode,       \n\
                             mincputask,avecpu,ntasks,alloccpus,elapsed,    \n\
	                     state,exitcode'                                \n\
     -L, --allclusters:                                                     \n\
	           Display jobs ran on all clusters. By default, only jobs  \n\
                   ran on the cluster from where sacct is called are        \n\
                   displayed.                                               \n\
     -n, --noheader:                                                        \n\
	           No header will be added to the beginning of output.      \n\
                   The default is to print a header; the option has no effect\n\
                   if --dump is specified                                   \n\
     -N, --nodelist:                                                        \n\
                   Display jobs that ran on any of these nodes,             \n\
                   can be one or more using a ranged string.                \n\
     -o, --format:                                                          \n\
	           Comma separated list of fields. (use \"--helpformat\"    \n\
                   for a list of available fields).                         \n\
     -O, --formatted_dump:                                                  \n\
	           Dump accounting records in an easy-to-read format,       \n\
                   primarily for debugging.                                 \n\
     -p, --parsable: output will be '|' delimited with a '|' at the end     \n\
     -P, --parsable2: output will be '|' delimited without a '|' at the end \n\
     -r, --partition:                                                       \n\
	           Comma separated list of partitions to select jobs and    \n\
                   job steps from. The default is all partitions.           \n\
     -s, --state:                                                           \n\
	           Select jobs based on their current state or the state    \n\
                   they were in during the time period given: running (r),  \n\
	           completed (cd), failed (f), timeout (to), and            \n\
                   node_fail (nf).                                          \n\
     -S, --starttime:                                                       \n\
                   Select jobs eligible after this time.  Default is        \n\
                   midnight of current day.  If states are given with the -s\n\
                   option then return jobs in this state at this time, 'now'\n\
                   is also used as the default time.                        \n\
     -T, --truncate:                                                        \n\
                   Truncate time.  So if a job started before --starttime   \n\
                   the start time would be truncated to --starttime.        \n\
                   The same for end time and --endtime.                     \n\
     -u, --uid, --user:                                                     \n\
	           Use this comma separated list of uids or user names      \n\
                   to select jobs to display.  By default, the running      \n\
                   user's uid is used.                                      \n\
     --usage:      Display brief usage message.                             \n\
     -v, --verbose:                                                         \n\
	           Primarily for debugging purposes, report the state of    \n\
                   various variables during processing.                     \n\
     -V, --version: Print version.                                          \n\
     -W, --wckeys:                                                          \n\
                   Only send data about these wckeys.  Default is all.      \n\
     -X, --allocations:                                                     \n\
	           Only show cumulative statistics for each job, not the    \n\
	           intermediate steps.                                      \n\
	                                                                    \n\
     Note, valid start/end time formats are...                              \n\
	           HH:MM[:SS] [AM|PM]                                       \n\
	           MMDD[YY] or MM/DD[/YY] or MM.DD[.YY]                     \n\
	           MM/DD[/YY]-HH:MM[:SS]                                    \n\
	           YYYY-MM-DD[THH:MM[:SS]]                                  \n\
\n");

	return;
}

void _usage(void)
{
	printf("Usage: sacct [options]\n\tUse --help for help\n");
}

void _init_params()
{
	memset(&params, 0, sizeof(sacct_parameters_t));
	params.job_cond = xmalloc(sizeof(acct_job_cond_t));
	params.job_cond->without_usage_truncation = 1;
}

int decode_state_char(char *state)
{
	if (!strncasecmp(state, "p", 1))
		return JOB_PENDING; 	/* we should never see this */
	else if (!strncasecmp(state, "r", 1))
		return JOB_RUNNING;
	else if (!strncasecmp(state, "su", 1))
		return JOB_SUSPENDED;
	else if (!strncasecmp(state, "cd", 2))
		return JOB_COMPLETE;
	else if (!strncasecmp(state, "ca", 2))
		return JOB_CANCELLED;
	else if (!strncasecmp(state, "f", 1))
		return JOB_FAILED;
	else if (!strncasecmp(state, "to", 1))
		return JOB_TIMEOUT;
	else if (!strncasecmp(state, "nf", 1))
		return JOB_NODE_FAIL;
	else
		return -1; // unknown
} 

int get_data(void)
{
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;

	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	acct_job_cond_t *job_cond = params.job_cond;
	
	if(params.opt_completion) {
		jobs = g_slurm_jobcomp_get_jobs(job_cond);
		return SLURM_SUCCESS;
	} else {
				
		jobs = jobacct_storage_g_get_jobs_cond(acct_db_conn, getuid(),
						       job_cond);
	}

	if (params.opt_fdump) 
		return SLURM_SUCCESS;

	if(!jobs)
		return SLURM_ERROR;

	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		if(job->user) {
			struct	passwd *pw = NULL;		 
			if ((pw=getpwnam(job->user)))
				job->uid = pw->pw_uid;
		}
		
		if(!job->steps || !list_count(job->steps)) 
			continue;
		
		itr_step = list_iterator_create(job->steps);
		while((step = list_next(itr_step)) != NULL) {
			/* now aggregate the aggregatable */
			job->alloc_cpus = MAX(job->alloc_cpus, step->ncpus);

			if(step->state < JOB_COMPLETE)
				continue;
			job->tot_cpu_sec += step->tot_cpu_sec;
			job->tot_cpu_usec += step->tot_cpu_usec;
			job->user_cpu_sec +=
				step->user_cpu_sec;
			job->user_cpu_usec +=
				step->user_cpu_usec;
			job->sys_cpu_sec +=
				step->sys_cpu_sec;
			job->sys_cpu_usec +=
				step->sys_cpu_usec;
			/* get the max for all the sacct_t struct */
			aggregate_sacct(&job->sacct, &step->sacct);
		}
		list_iterator_destroy(itr_step);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
} 

void parse_command_line(int argc, char **argv)
{
	extern int optind;
	int c, i, optionIndex = 0;
	char *end = NULL, *start = NULL, *acct_type = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	struct stat stat_buf;
	char *dot = NULL;
	bool brief_output = FALSE, long_output = FALSE;
	bool all_users = 0;
	bool all_clusters = 0;
	acct_job_cond_t *job_cond = params.job_cond;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	int verbosity;		/* count of -v options */
	bool set;

	static struct option long_options[] = {
		{"allusers", 0,0, 'a'},
		{"allclusters", 0,0, 'L'},
		{"accounts", 1, 0, 'A'},
		{"allocations", 0, &params.opt_allocs,  1},
		{"brief", 0, 0, 'b'},
		{"completion", 0, &params.opt_completion, 'c'},
		{"clusters", 1, 0, 'C'},
		{"dump", 0, 0, 'd'},
		{"duplicates", 0, &params.opt_dup, 1},
		{"helpformat", 0, 0, 'e'},
		{"help-fields", 0, 0, 'e'},
		{"endtime", 1, 0, 'E'},
		{"file", 1, 0, 'f'},
		{"gid", 1, 0, 'g'},
		{"group", 1, 0, 'g'},
		{"help", 0, 0, 'h'},
		{"helpformat", 0, &params.opt_help, 2},
		{"nnodes", 1, 0, 'i'},
		{"ncpus", 1, 0, 'I'},
		{"jobs", 1, 0, 'j'},
		{"long", 0, 0, 'l'},
		{"nodelist", 1, 0, 'N'},
		{"noheader", 0, 0, 'n'},
		{"fields", 1, 0, 'o'},
		{"format", 1, 0, 'o'},
		{"formatted_dump", 0, 0, 'O'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"partition", 1, 0, 'r'},
		{"state", 1, 0, 's'},
		{"starttime", 1, 0, 'S'},
		{"truncate", 0, 0, 'T'},
		{"uid", 1, 0, 'u'},
		{"usage", 0, &params.opt_help, 3},
		{"user", 1, 0, 'u'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{"wckeys", 1, 0, 'W'},
		{0, 0, 0, 0}};

	params.opt_uid = getuid();
	params.opt_gid = getgid();

	verbosity         = 0;
	log_init("sacct", opts, SYSLOG_FACILITY_DAEMON, NULL);
	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv,
				"aA:bcC:dDeE:f:g:hi:I:j:lLnN:o:OpPr:s:S:Ttu:vVW:X",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			all_users = 1;
			break;
		case 'A':
			if(!job_cond->acct_list) 
				job_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->acct_list, optarg);
			break;
		case 'b':
			brief_output = true;
			break;
		case 'c':
			params.opt_completion = 1;
			break;
		case 'C':
			if(!strcasecmp(optarg, "-1")) {
				all_clusters = 1;
				break;
			}
			all_clusters=0;
			if(!job_cond->cluster_list) 
				job_cond->cluster_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->cluster_list, optarg);
			break;
		case 'd':
			params.opt_dump = 1;
			break;	
		case 'D':
			params.opt_dup = 1;
			break;
		case 'e':
			params.opt_help = 2;
			break;
		case 'E':
			job_cond->usage_end = parse_time(optarg, 1);
			break;
		case 'f':
			xfree(params.opt_filein);
			params.opt_filein = xstrdup(optarg);
			break;
		case 'g':
			if(!job_cond->groupid_list)
				job_cond->groupid_list = 
					list_create(slurm_destroy_char);
			_addto_id_char_list(job_cond->groupid_list, optarg, 1);
			break;
		case 'h':
			params.opt_help = 1;
			break;
		case 'i':
			set = get_resource_arg_range(
				optarg, 
				"requested node range",
				(int *)&job_cond->nodes_min,
				(int *)&job_cond->nodes_max,
				true);
			
			if (set == false) {
				error("invalid node range -i '%s'", 
				      optarg);
				exit(1);
			}			
			break;
		case 'I':
			set = get_resource_arg_range(
				optarg, 
				"requested cpu range",
				(int *)&job_cond->cpus_min,
				(int *)&job_cond->cpus_max,
				true);
			
			if (set == false) {
				error("invalid cpu range -i '%s'", 
				      optarg);
				exit(1);
			}
			break;
		case 'j':
			if ((strspn(optarg, "0123456789, ") < strlen(optarg))
			    && (strspn(optarg, ".0123456789, ") 
				< strlen(optarg))) {
				fprintf(stderr, "Invalid jobs list: %s\n",
					optarg);
				exit(1);
			}
			
			if(!job_cond->step_list)
				job_cond->step_list = list_create(
					destroy_jobacct_selected_step);
			_addto_step_list(job_cond->step_list, optarg);
			break;
		case 'L':
			all_clusters = 1;
			break;
		case 'l':
			long_output = true;
			break;
		case 'o':
			xstrfmtcat(params.opt_field_list, "%s,", optarg);
			break;
		case 'O':
			params.opt_fdump = 1;
			break;
		case 'n':
			print_fields_have_header = 0;
			break;
		case 'N':
			if(job_cond->used_nodes) {
				error("Aleady asked for nodes '%s'",
				      job_cond->used_nodes);
				break;
			}
			job_cond->used_nodes = xstrdup(optarg);
			break;
		case 'p':
			print_fields_parsable_print = 
				PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case 'P':
			print_fields_parsable_print = 
				PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case 'r':
			if(!job_cond->partition_list)
				job_cond->partition_list =
					list_create(slurm_destroy_char);

			slurm_addto_char_list(job_cond->partition_list,
					      optarg);
			break;
		case 's':
			if(!job_cond->state_list)
				job_cond->state_list =
					list_create(slurm_destroy_char);

			_addto_state_char_list(job_cond->state_list, optarg);
			break;
		case 'S':
			job_cond->usage_start = parse_time(optarg, 1);
			break;
		case 'T':
			job_cond->without_usage_truncation = 0;
			break;
		case 'U':
			params.opt_help = 3;
			break;
		case 'u':
			if(!strcmp(optarg, "-1")) {
				all_users = 1;
				break;
			}
			all_users = 0;
			if(!job_cond->userid_list)
				job_cond->userid_list = 
					list_create(slurm_destroy_char);
			_addto_id_char_list(job_cond->userid_list, optarg, 0);
			break;
		case 'v':
			/* Handle -vvv thusly...
			 */
			verbosity++;
			break;
		case 'W':
			if(!job_cond->wckey_list) 
				job_cond->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->wckey_list, optarg);
			break;
		case 'V':
			printf("%s %s\n", PACKAGE, SLURM_VERSION);
			exit(0);
		case 't':
		case 'X':
			params.opt_allocs = 1;
			break;
		case ':':
		case '?':	/* getopt() has explained it */
			exit(1); 
		}
	}

	if (verbosity) {
		opts.stderr_level += verbosity;
		opts.prefix_level = 1;
		log_alter(opts, 0, NULL);
	}


	/* Now set params.opt_dup, unless they've already done so */
	if (params.opt_dup < 0)	/* not already set explicitly */
		params.opt_dup = 0;
	
	if (params.opt_fdump) 
		params.opt_dup |= FDUMP_FLAG;

	job_cond->duplicates = params.opt_dup;
	job_cond->without_steps = params.opt_allocs;

	if(!job_cond->usage_start) {
		if(job_cond->state_list)
			job_cond->usage_start = time(NULL);
		else {
			struct tm start_tm;
			
			if(!localtime_r(&job_cond->usage_start, &start_tm)) {
				error("Couldn't get localtime from %d", 
				      job_cond->usage_start);
				return;
			}
			start_tm.tm_sec = 0;
			start_tm.tm_min = 0;
			start_tm.tm_hour = 0;
			start_tm.tm_isdst = -1;
			job_cond->usage_start = mktime(&start_tm);
		}
	}
	
	if(verbosity > 0) {
		char *start_char =NULL, *end_char = NULL;
		
		start_char = xstrdup(ctime(&job_cond->usage_start));
		/* remove the new line */
		start_char[strlen(start_char)-1] = '\0';
		if(job_cond->usage_end) {
			end_char = xstrdup(ctime(&job_cond->usage_end));
			/* remove the new line */
			end_char[strlen(end_char)-1] = '\0';
		} else
			end_char = xstrdup("Now");
		info("Jobs eligible from %s - %s\n", start_char, end_char);
		xfree(start_char);
		xfree(end_char);
	}

	debug("Options selected:\n"
	      "\topt_completion=%d\n"
	      "\topt_dump=%d\n"
	      "\topt_dup=%d\n"
	      "\topt_fdump=%d\n"
	      "\topt_field_list=%s\n"
	      "\topt_help=%d\n"
	      "\topt_allocs=%d\n",
	      params.opt_completion,
	      params.opt_dump,
	      params.opt_dup,
	      params.opt_fdump,
	      params.opt_field_list,
	      params.opt_help,
	      params.opt_allocs);

	if(params.opt_completion) {
		g_slurm_jobcomp_init(params.opt_filein);

		acct_type = slurm_get_jobcomp_type();
		if ((strcmp(acct_type, "jobcomp/none") == 0)
		    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
			fprintf(stderr, "SLURM job completion is disabled\n");
			exit(1);
		}
		xfree(acct_type);
	} else {
		slurm_acct_storage_init(params.opt_filein);
		
		acct_type = slurm_get_accounting_storage_type();
		if ((strcmp(acct_type, "accounting_storage/none") == 0)
		    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
			fprintf(stderr,
				"SLURM accounting storage is disabled\n");
			exit(1);
		}
		xfree(acct_type);
		acct_db_conn = acct_storage_g_get_connection(false, 0, false);
		if(errno != SLURM_SUCCESS) {
			error("Problem talking to the database: %m");
			exit(1);
		}
	}

	/* specific clusters requested? */
	if(all_clusters) {
		if(job_cond->cluster_list 
		   && list_count(job_cond->cluster_list)) {
			list_destroy(job_cond->cluster_list);
			job_cond->cluster_list = NULL;
		}
		debug2("Clusters requested:\tall\n");
	} else if (job_cond->cluster_list 
		   && list_count(job_cond->cluster_list)) {
		debug2( "Clusters requested:\n");
		itr = list_iterator_create(job_cond->cluster_list);
		while((start = list_next(itr))) 
			debug2("\t: %s\n", start);
		list_iterator_destroy(itr);
	} else if(!job_cond->cluster_list 
		  || !list_count(job_cond->cluster_list)) {
		if(!job_cond->cluster_list)
			job_cond->cluster_list =
				list_create(slurm_destroy_char);
		if((start = slurm_get_cluster_name())) {
			list_append(job_cond->cluster_list, start);
			debug2("Clusters requested:\t%s", start);
		}
	}

	/* if any jobs or nodes are specified set to look for all users if none
	   are set */
	if(!job_cond->userid_list || !list_count(job_cond->userid_list)) 
		if((job_cond->step_list && list_count(job_cond->step_list)) 
		   || job_cond->used_nodes)
			all_users=1;      

	if(all_users) {
		if(job_cond->userid_list && list_count(job_cond->userid_list)) {
			list_destroy(job_cond->userid_list);
			job_cond->userid_list = NULL;
		}
		debug2("Userids requested:\tall\n");
	} else if (job_cond->userid_list && list_count(job_cond->userid_list)) {
		debug2("Userids requested:");
		itr = list_iterator_create(job_cond->userid_list);
		while((start = list_next(itr))) 
			debug2("\t: %s", start);
		list_iterator_destroy(itr);
	} else if(!job_cond->userid_list 
		  || !list_count(job_cond->userid_list)) {
		if(!job_cond->userid_list)
			job_cond->userid_list = list_create(slurm_destroy_char);
		start = xstrdup_printf("%u", params.opt_uid);
		list_append(job_cond->userid_list, start);
		debug2("Userid requested\t: %s", start);
	}

	if (job_cond->groupid_list && list_count(job_cond->groupid_list)) {
		debug2("Groupids requested:\n");
		itr = list_iterator_create(job_cond->groupid_list);
		while((start = list_next(itr))) 
			debug2("\t: %s\n", start);
		list_iterator_destroy(itr);
	} 

	/* specific partitions requested? */
	if (job_cond->partition_list && list_count(job_cond->partition_list)) {
		debug2("Partitions requested:");
		itr = list_iterator_create(job_cond->partition_list);
		while((start = list_next(itr))) 
			debug2("\t: %s\n", start);
		list_iterator_destroy(itr);
	}

	/* specific jobs requested? */
	if (job_cond->step_list && list_count(job_cond->step_list)) { 
		debug2("Jobs requested:");
		itr = list_iterator_create(job_cond->step_list);
		while((selected_step = list_next(itr))) {
			if(selected_step->stepid != NO_VAL) 
				debug2("\t: %d.%d",
					selected_step->jobid,
					selected_step->stepid);
			else	
				debug2("\t: %d", 
					selected_step->jobid);
		}
		list_iterator_destroy(itr);
	}

	/* specific states (completion state) requested? */
	if (job_cond->state_list && list_count(job_cond->state_list)) {
		debug2("States requested:");
		itr = list_iterator_create(job_cond->state_list);
		while((start = list_next(itr))) {
			debug2("\t: %s", 
				job_state_string(atoi(start)));
		}
		list_iterator_destroy(itr);
	}

	if (job_cond->wckey_list && list_count(job_cond->wckey_list)) {
		debug2("Wckeys requested:");
		itr = list_iterator_create(job_cond->wckey_list);
		while((start = list_next(itr))) 
			debug2("\t: %s\n", start);
		list_iterator_destroy(itr);
	} 

	/* select the output fields */
	if(brief_output) {
		if(params.opt_completion)
			dot = BRIEF_COMP_FIELDS;
		else
			dot = BRIEF_FIELDS;
		
		xstrfmtcat(params.opt_field_list, "%s,", dot);
	} 

	if(long_output) {
		if(params.opt_completion)
			dot = LONG_COMP_FIELDS;
		else
			dot = LONG_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
	} 
	
	if (params.opt_field_list==NULL) {
		if (params.opt_dump)
			goto endopt;
		if(params.opt_completion)
			dot = DEFAULT_COMP_FIELDS;
		else
			dot = DEFAULT_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
	}

	start = params.opt_field_list;
	while ((end = strstr(start, ","))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;

		*end = 0;
		while (isspace(*start))
			start++;	/* discard whitespace */
		if(!(int)*start)
			continue;

		if((tmp_char = strstr(start, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		} 
		
		command_len = strlen(start);

		for (i = 0; fields[i].name; i++) {
			if (!strncasecmp(fields[i].name, start, command_len))
				goto foundfield;
		}
		error("Invalid field requested: \"%s\"", start);
		exit(1);
	foundfield:
		if(newlen)
			fields[i].len = newlen;
		list_append(print_fields_list, &fields[i]);
		start = end + 1;
	}
	field_count = list_count(print_fields_list);
endopt:
	if (optind < argc) {
		debug2("Error: Unknown arguments:");
		for (i=optind; i<argc; i++)
			debug2(" %s", argv[i]);
		debug2("\n");
		exit(1);
	}
	return;
}

/* Note: do_dump() strives to present data in an upward-compatible
 * manner so that apps written to use data from `sacct -d` in slurm
 * v1.0 will continue to work in v1.1 and later.
 *
 * To help ensure this compatibility,
 * a. The meaning of an existing field never changes
 * b. New fields are appended to the end of a record
 *
 * The "numfields" field of the record can be used as a sub-version
 * number, as it will never decrease for the life of the current
 * record version number (currently 1). For example, if your app needs
 * to use field 28, a record with numfields<28 is too old a version
 * for you, while numfields>=28 will provide what you are expecting.
 */ 
void do_dump(void)
{
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	struct tm ts;
	
	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		if(job->sacct.min_cpu == (float)NO_VAL)
			job->sacct.min_cpu = 0;
		
		if(list_count(job->steps)) {
			job->sacct.ave_cpu /= list_count(job->steps);
			job->sacct.ave_rss /= list_count(job->steps);
			job->sacct.ave_vsize /= list_count(job->steps);
			job->sacct.ave_pages /= list_count(job->steps);
		}

		/* JOB_START */
		if (job->show_full) {
			gmtime_r(&job->start, &ts);
			printf("%u %s %04d%02d%02d%02d%02d%02d %d %s %s ",
			       job->jobid,
			       job->partition,
			       1900+(ts.tm_year),
			       1+(ts.tm_mon),
			       ts.tm_mday,
			       ts.tm_hour,
			       ts.tm_min,
			       ts.tm_sec,
			       (int)job->submit,
			       job->blockid,	/* block id */
			       "-");	/* reserved 1 */

			printf("JOB_START 1 16 %d %d %s %d %d %d %s %s\n", 
			       job->uid,
			       job->gid,
			       job->jobname,
			       job->track_steps,
			       job->priority,
			       job->alloc_cpus,
			       job->nodes,
			       job->account);
		}
		/* JOB_STEP */
		itr_step = list_iterator_create(job->steps);
		while((step = list_next(itr_step))) {
			gmtime_r(&step->start, &ts);
			printf("%u %s %04d%02d%02d%02d%02d%02d %d %s %s ",
			       job->jobid,
			       job->partition,
			       1900+(ts.tm_year),
			       1+(ts.tm_mon),
			       ts.tm_mday,
			       ts.tm_hour,
			       ts.tm_min,
			       ts.tm_sec,
			       (int)job->submit,
			       job->blockid,	/* block id */
			       "-");	/* reserved 1 */
			if(step->end == 0)
				step->end = job->end;
				
			gmtime_r(&step->end, &ts);
			printf("JOB_STEP 1 50 %u %04d%02d%02d%02d%02d%02d ",
			       step->stepid,
			       1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
			            ts.tm_hour, ts.tm_min, ts.tm_sec);
			printf("%s %d %d %d %d ",
			       job_state_string_compact(step->state),
			       step->exitcode,
			       step->ncpus,
			       step->ncpus,
			       step->elapsed);
			printf("%d %d %d %d %d %d %d %d",
			       step->tot_cpu_sec,
			       step->tot_cpu_usec,
			       (int)step->user_cpu_sec,
			       (int)step->user_cpu_usec,
			       (int)step->sys_cpu_sec,
			       (int)step->sys_cpu_usec,
			       step->sacct.max_vsize/1024, 
			       step->sacct.max_rss/1024);
			/* Data added in Slurm v1.1 */
			printf("%u %u %.2f %u %u %.2f %d %u %u %.2f "
			       "%.2f %u %u %.2f %s %s %s\n",
			       step->sacct.max_vsize_id.nodeid,
			       step->sacct.max_vsize_id.taskid,
			       step->sacct.ave_vsize/1024,
			       step->sacct.max_rss_id.nodeid,
			       step->sacct.max_rss_id.taskid,
			       step->sacct.ave_rss/1024,
			       step->sacct.max_pages,
			       step->sacct.max_pages_id.nodeid,
			       step->sacct.max_pages_id.taskid,
			       step->sacct.ave_pages,
			       step->sacct.min_cpu,
			       step->sacct.min_cpu_id.nodeid,
			       step->sacct.min_cpu_id.taskid,
			       step->sacct.ave_cpu,
			       step->stepname,
			       step->nodes,
			       job->account);
		}
		list_iterator_destroy(itr_step);
		/* JOB_TERMINATED */
		if (job->show_full) {
			gmtime_r(&job->start, &ts);
			printf("%u %s %04d%02d%02d%02d%02d%02d %d %s %s ",
			       job->jobid,
			       job->partition,
			       1900+(ts.tm_year),
			       1+(ts.tm_mon),
			       ts.tm_mday,
			       ts.tm_hour,
			       ts.tm_min,
			       ts.tm_sec,
			       (int)job->submit,
			       job->blockid,	/* block id */
			       "-");	/* reserved 1 */
			gmtime_r(&job->end, &ts);
			printf("JOB_TERMINATED 1 50 %d ",
			       job->elapsed);
			printf("%04d%02d%02d%02d%02d%02d ",
			1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
			      ts.tm_hour, ts.tm_min, ts.tm_sec); 
			printf("%s %d %d %d %d ",
			       job_state_string_compact(job->state),
			       job->exitcode,
			       job->alloc_cpus,
			       job->alloc_cpus,
			       job->elapsed);
			printf("%d %d %d %d %d %d %d %d",
			       job->tot_cpu_sec,
			       job->tot_cpu_usec,
			       (int)job->user_cpu_sec,
			       (int)job->user_cpu_usec,
			       (int)job->sys_cpu_sec,
			       (int)job->sys_cpu_usec,
			       job->sacct.max_vsize/1024, 
			       job->sacct.max_rss/1024);
			/* Data added in Slurm v1.1 */
			printf("%u %u %.2f %u %u %.2f %d %u %u %.2f "
			       "%.2f %u %u %.2f %s %s %s %d\n",
			       job->sacct.max_vsize_id.nodeid,
			       job->sacct.max_vsize_id.taskid,
			       job->sacct.ave_vsize/1024,
			       job->sacct.max_rss_id.nodeid,
			       job->sacct.max_rss_id.taskid,
			       job->sacct.ave_rss/1024,
			       job->sacct.max_pages,
			       job->sacct.max_pages_id.nodeid,
			       job->sacct.max_pages_id.taskid,
			       job->sacct.ave_pages,
			       job->sacct.min_cpu,
			       job->sacct.min_cpu_id.nodeid,
			       job->sacct.min_cpu_id.taskid,
			       job->sacct.ave_cpu,
			       "-",
			       job->nodes,
			       job->account,
			       job->requid);			
		}
	}
	list_iterator_destroy(itr);		
}

void do_dump_completion(void)
{
	ListIterator itr = NULL;
	jobcomp_job_rec_t *job = NULL;
		
	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		printf("JOB %u %s %s %s %s(%u) %u(%s) %u %s %s %s %s",
		       job->jobid, job->partition, job->start_time,
		       job->end_time, job->uid_name, job->uid, job->gid,
		       job->gid_name, job->node_cnt, job->nodelist, 
		       job->jobname, job->state,
		       job->timelimit);
		if(job->blockid)
			printf(" %s %s %s %s %u %s %s",
			       job->blockid, job->connection, job->reboot,
			       job->rotate, job->max_procs, job->geo,
			       job->bg_start_point);
		printf("\n");
	}
	list_iterator_destroy(itr);
}

void do_help(void)
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
		debug2("sacct bug: params.opt_help=%d\n", 
			params.opt_help);
	}
}

/* do_list() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
void do_list(void)
{
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	
	if(!jobs)
		return;

	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		if(job->sacct.min_cpu == NO_VAL)
			job->sacct.min_cpu = 0;

		if(list_count(job->steps)) {
			job->sacct.ave_cpu /= list_count(job->steps);
			job->sacct.ave_rss /= list_count(job->steps);
			job->sacct.ave_vsize /= list_count(job->steps);
			job->sacct.ave_pages /= list_count(job->steps);
		}

		if (job->show_full) 
			print_fields(JOB, job);
		
		if (!params.opt_allocs
		    && (job->track_steps || !job->show_full)) {
			itr_step = list_iterator_create(job->steps);
			while((step = list_next(itr_step))) {
				if(step->end == 0)
					step->end = job->end;
				print_fields(JOBSTEP, step);
			} 
			list_iterator_destroy(itr_step);
		}
	}
	list_iterator_destroy(itr);
}

/* do_list_completion() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
void do_list_completion(void)
{
	ListIterator itr = NULL;
	jobcomp_job_rec_t *job = NULL;

	if(!jobs)
		return;

	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		print_fields(JOBCOMP, job);
	}
	list_iterator_destroy(itr);
}

void sacct_init()
{
	_init_params();
	print_fields_list = list_create(NULL);
	print_fields_itr = list_iterator_create(print_fields_list);
}

void sacct_fini()
{
	if(print_fields_itr)
		list_iterator_destroy(print_fields_itr);
	if(print_fields_list)
		list_destroy(print_fields_list);
	if(jobs)
		list_destroy(jobs);
	if(qos_list)
		list_destroy(qos_list);

	if(params.opt_completion)
		g_slurm_jobcomp_fini();
	else {
		acct_storage_g_close_connection(&acct_db_conn);
		slurm_acct_storage_fini();
	}
	xfree(params.opt_field_list);
	xfree(params.opt_filein);
	destroy_acct_job_cond(params.job_cond);
}
