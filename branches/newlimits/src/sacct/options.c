/*****************************************************************************\
 *  options.c - option functions for sacct
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

void _show_rec(char *f[])
{
	int 	i;
	fprintf(stderr, "rec>");
	for (i=0; f[i]; i++)
		fprintf(stderr, " %s", f[i]);
	fprintf(stderr, "\n");
	return;
}

void _help_fields_msg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf("  ");
		else
			printf("\n");
		printf("%-10s", fields[i].name);
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

void _help_msg(void)
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	printf("\n"
	       "By default, sacct displays accounting data for all jobs and job\n"
	       "steps that are present in the log.\n"
	       "\n"
	       "Notes:\n"
	       "\n"
	       "    * If --dump is specified,\n"
	       "          * The field selection options (--brief, --fields, ...)\n"
	       "	    have no effect\n"
	       "	  * Elapsed time fields are presented as 2 fields, integral\n"
	       "	    seconds and integral microseconds\n"
	       "    * If --dump is not specified, elapsed time fields are presented\n"
	       "      as [[days-]hours:]minutes:seconds.hundredths\n"
	       "    * The default input file is the file named in the \"jobacct_logfile\"\n"
	       "      parameter in %s.\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "-a, --all\n"
	       "    Display job accounting data for all users. By default, only\n"
	       "    data for the current user is displayed for users other than\n"
	       "    root.\n"
	       "-b, --brief\n"
	       "    Equivalent to \"--fields=jobstep,state,error\". This option\n"
	       "    has no effect if --dump is specified.\n"
	       "-c, --completion\n"
	       "    Use job completion instead of accounting data.\n"
	       "-C, --cluster\n"
	       "    Only send data about this cluster -1 for all clusters.\n"
	       "-d, --dump\n"
	       "    Dump the raw data records\n"
	       "--duplicates\n"
	       "    If SLURM job ids are reset, but the job accounting log file\n"
	       "    isn't reset at the same time (with -e, for example), some\n"
	       "    job numbers will probably appear more than once in the\n"
	       "    accounting log file to refer to different jobs; such jobs\n"
	       "    can be distinguished by the \"submit\" time stamp in the\n"
	       "    data records.\n"
	       "      When data for specific jobs are requested with\n"
	       "    the --jobs option, we assume that the user\n"
	       "    wants to see only the most recent job with that number. This\n"
	       "    behavior can be overridden by specifying --duplicates, in\n"
	       "    which case all records that match the selection criteria\n"
	       "    will be returned.\n"
	       "      When --jobs is not specified, we report\n"
	       "    data for all jobs that match the selection criteria, even if\n"
	       "    some of the job numbers are reused. Specify that you only\n"
	       "    want the most recent job for each selected job number with\n"
	       "    the --noduplicates option.\n"
	       "-e <timespec>, --expire=<timespec>\n"
	       "    Remove jobs from SLURM's current accounting log file (or the\n"
	       "    file specified with --file) that completed more than <timespec>\n"
	       "    ago.  If <timespec> is an integer, it is interpreted as\n" 
	       "    minutes. If <timespec> is an integer followed by \"h\", it is\n"
	       "    interpreted as a number of hours. If <timespec> is an integer\n"
	       "    followed by \"d\", it is interpreted as number of days. For\n"
	       "    example, \"--expire=14d\" means that you wish to purge the job\n"
	       "    accounting log of all jobs that completed more than 14 days ago.\n" 
	       "-F <field-list>, --fields=<field-list>\n"
	       "    Display the specified data (use \"--help-fields\" for a\n"
	       "    list of available fields). If no field option is specified,\n"
	       "    we use \"--fields=jobstep,jobname,partition,alloc_cpus,state,error\".\n"
	       "-f<file>, --file=<file>\n"
	       "    Read data from the specified file, rather than SLURM's current\n"
	       "    accounting log file.\n"
	       "-l, --long\n"
	       "    Equivalent to specifying\n"
	       "    \"--fields=jobstep,usercpu,systemcpu,minflt,majflt,nprocs,\n"
	       "    alloc_cpus,elapsed,state,exitcode\"\n"
	       "-O, --formatted_dump\n"
	       "    Dump accounting records in an easy-to-read format, primarily\n"
	       "    for debugging.\n"
	       "-g <gid>, --gid <gid>\n"
	       "    Select only jobs submitted from the <gid> group.\n"
	       "-h, --help\n"
	       "    Print a general help message.\n"
	       "--help-fields\n"
	       "    Print a list of fields that can be specified with the\n"
	       "    \"--fields\" option\n"
	       "-j <job(.step)>, --jobs=<job(.step)>\n"
	       "    Display information about this job or comma-separated\n"
	       "    list of jobs. The default is all jobs. Adding .step will\n"
	       "    display the specfic job step of that job.\n"
	       "--noduplicates\n"
	       "    See the discussion under --duplicates.\n"
	       "--noheader\n"
	       "    Print (or don't print) a header. The default is to print a\n"
	       "    header; the option has no effect if --dump is specified\n"
	       "-p <part_list>, --partition=<part_list>\n"
	       "    Display or purge information about jobs and job steps in the\n"
	       "    <part_list> partition(s). The default is all partitions.\n"
	       "-P --purge\n"
	       "    Used in conjunction with --expire to remove invalid data\n"
	       "    from the job accounting log.\n"
	       "-s <state-list>, --state=<state-list>\n"
	       "    Select jobs based on their current state: running (r),\n"
	       "    completed (cd), failed (f), timeout (to), and node_fail (nf).\n"
	       "-S, --stat\n"
	       "    Get real time state of a jobstep supplied by the -j\n"
	       "    option\n" 
	       "-t, --total\n"
	       "    Only show cumulative statistics for each job, not the\n"
	       "    intermediate steps\n"
	       "-u <uid>, --uid <uid>\n"
	       "    Select only jobs submitted by the user with uid <uid>.  Only\n"
	       "    root users are allowed to specify a uid other than their own -1 for all users.\n"
	       "--usage\n"
	       "    Pointer to this message.\n"
	       "-v, --verbose\n"
	       "    Primarily for debugging purposes, report the state of various\n"
	       "    variables during processing.\n", conf->slurm_conf);

	slurm_conf_unlock();

	return;
}

void _usage(void)
{
	printf("\nUsage: sacct [options]\n\tUse --help for help\n");
}

void _init_params()
{
	memset(&params, 0, sizeof(sacct_parameters_t));
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

	if(params.opt_completion) {
		jobs = g_slurm_jobcomp_get_jobs(params.opt_job_list,
						params.opt_partition_list,
						&params);
		return SLURM_SUCCESS;
	} else {
		acct_job_cond_t *job_cond = xmalloc(sizeof(acct_job_cond_t));

		job_cond->acct_list = params.opt_acct_list;
		job_cond->cluster_list = params.opt_cluster_list;
		job_cond->duplicates = params.opt_dup;
		job_cond->groupid_list = params.opt_gid_list;
		job_cond->partition_list = params.opt_partition_list;
		job_cond->step_list = params.opt_job_list;
		job_cond->state_list = params.opt_state_list;
		job_cond->usage_start = params.opt_begin;
		job_cond->usage_end = params.opt_end;
		job_cond->userid_list = params.opt_uid_list;
				
		jobs = jobacct_storage_g_get_jobs_cond(acct_db_conn, getuid(),
						       job_cond);
		destroy_acct_job_cond(job_cond);
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
		
		if(!list_count(job->steps)) 
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
	static struct option long_options[] = {
		{"all", 0,0, 'a'},
		{"accounts", 1, 0, 'A'},
		{"begin", 1, 0, 'B'},
		{"brief", 0, 0, 'b'},
		{"cluster", 1, 0, 'C'},
		{"completion", 0, &params.opt_completion, 'c'},
		{"duplicates", 0, &params.opt_dup, 1},
		{"dump", 0, 0, 'd'},
		{"end", 1, 0, 'E'},
		{"expire", 1, 0, 'e'},
		{"fields", 1, 0, 'F'},
		{"file", 1, 0, 'f'},
		{"formatted_dump", 0, 0, 'O'},
		{"gid", 1, 0, 'g'},
		{"group", 1, 0, 'g'},
		{"help", 0, &params.opt_help, 1},
		{"help-fields", 0, &params.opt_help, 2},
		{"jobs", 1, 0, 'j'},
		{"long", 0, 0, 'l'},
		{"big_logfile", 0, &params.opt_lowmem, 1},
		{"noduplicates", 0, &params.opt_dup, 0},
		{"noheader", 0, &params.opt_noheader, 1},
		{"partition", 1, 0, 'p'},
		{"purge", 0, 0, 'P'},
		{"state", 1, 0, 's'},
		{"stat", 0, 0, 'S'},
		{"total", 0, 0,  't'},
		{"uid", 1, 0, 'u'},
		{"usage", 0, &params.opt_help, 3},
		{"user", 1, 0, 'u'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0}};

	_init_params();

	params.opt_uid = getuid();
	params.opt_gid = getgid();

	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv, "aA:bB:cC:de:E:F:f:g:hj:lOPp:s:StUu:Vv",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			all_users = 1;
			break;
		case 'A':
			if(!params.opt_acct_list) 
				params.opt_acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(params.opt_acct_list, optarg);
			break;
		case 'b':
			brief_output = true;
			break;
		case 'B':
			params.opt_begin = parse_time(optarg, 1);
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
			if(!params.opt_cluster_list) 
				params.opt_cluster_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(params.opt_cluster_list, optarg);
			break;
		case 'd':
			params.opt_dump = 1;
			break;

		case 'e':
		{	/* decode the time spec */
			long	acc=0;
			params.opt_expire_timespec = xstrdup(optarg);
			for (i=0; params.opt_expire_timespec[i]; i++) {
				char	c = params.opt_expire_timespec[i];
				if (isdigit(c)) {
					acc = (acc*10)+(c-'0');
					continue;
				}
				switch (c) {
				case 'D':
				case 'd':
					params.opt_expire += 
						acc*SECONDS_IN_DAY;
					acc=0;
					break;
				case 'H':
				case 'h':
					params.opt_expire += 
						acc*SECONDS_IN_HOUR;
						acc=0;
					break;
				case 'M':
				case 'm':
					params.opt_expire += 
						acc*SECONDS_IN_MINUTE;
					acc=0;
					break;
				default:
					params.opt_expire = -1;
					goto bad_timespec;
				} 
			}
			params.opt_expire += acc*SECONDS_IN_MINUTE;
		bad_timespec:
			if (params.opt_expire <= 0) {
				fprintf(stderr,
					"Invalid timspec for "
					"--expire: \"%s\"\n",
					params.opt_expire_timespec);
				exit(1);
			}
		}
		break;
		
		case 'E':
			params.opt_end = parse_time(optarg, 1);
			break;
		case 'F':
			if(params.opt_stat)
				xfree(params.opt_field_list);
			
			xstrfmtcat(params.opt_field_list, "%s,", optarg);
			break;

		case 'f':
			xfree(params.opt_filein);
			params.opt_filein = xstrdup(optarg);
			break;

		case 'g':
			if(!params.opt_gid_list)
				params.opt_gid_list = 
					list_create(slurm_destroy_char);
			_addto_id_char_list(params.opt_gid_list, optarg, 1);
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

		case 'l':
			long_output = true;
			break;

		case 'O':
			params.opt_fdump = 1;
			break;

		case 'P':
			params.opt_purge = 1;
			break;

		case 'p':
			if(!params.opt_partition_list)
				params.opt_partition_list =
					list_create(slurm_destroy_char);

			slurm_addto_char_list(params.opt_partition_list,
					      optarg);
			break;
		case 's':
			if(!params.opt_state_list)
				params.opt_state_list =
					list_create(slurm_destroy_char);

			_addto_state_char_list(params.opt_state_list, optarg);
			break;
		case 'S':
			if(!params.opt_field_list) {
				xstrfmtcat(params.opt_field_list, "%s,",
					   STAT_FIELDS);
			}
			params.opt_stat = 1;
			break;

		case 't':
			params.opt_total = 1;
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
			if(!params.opt_uid_list)
				params.opt_uid_list = 
					list_create(slurm_destroy_char);
			_addto_id_char_list(params.opt_uid_list, optarg, 0);
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
			printf("%s %s\n", PACKAGE, SLURM_VERSION);
			exit(0);

		case ':':
		case '?':	/* getopt() has explained it */
			exit(1); 
		}
	}

	/* Now set params.opt_dup, unless they've already done so */
	if (params.opt_dup < 0)	/* not already set explicitly */
		params.opt_dup = 0;
	
	if (params.opt_fdump) 
		params.opt_dup |= FDUMP_FLAG;

	if (params.opt_verbose) {
		fprintf(stderr, "Options selected:\n"
			"\topt_completion=%d\n"
			"\topt_dump=%d\n"
			"\topt_dup=%d\n"
			"\topt_expire=%s (%lu seconds)\n"
			"\topt_fdump=%d\n"
			"\topt_stat=%d\n"
			"\topt_field_list=%s\n"
			"\topt_filein=%s\n"
			"\topt_noheader=%d\n"
			"\topt_help=%d\n"
			"\topt_long=%d\n"
			"\topt_lowmem=%d\n"
			"\topt_purge=%d\n"
			"\topt_total=%d\n"
			"\topt_verbose=%d\n",
			params.opt_completion,
			params.opt_dump,
			params.opt_dup,
			params.opt_expire_timespec, params.opt_expire,
			params.opt_fdump,
			params.opt_stat,
			params.opt_field_list,
			params.opt_filein,
			params.opt_noheader,
			params.opt_help,
			params.opt_long,
			params.opt_lowmem,
			params.opt_purge,
			params.opt_total,
			params.opt_verbose);
	}

	/* check if we have accounting data to view */
	if (params.opt_filein == NULL) {
		if(params.opt_completion) 
			params.opt_filein = slurm_get_jobcomp_loc();
		else
			params.opt_filein = slurm_get_accounting_storage_loc();
	}

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
		acct_db_conn = acct_storage_g_get_connection(false, false);
		
		acct_type = slurm_get_accounting_storage_type();
		if ((strcmp(acct_type, "accounting_storage/none") == 0)
		    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
			fprintf(stderr,
				"SLURM accounting storage is disabled\n");
			exit(1);
		}
		xfree(acct_type);
	}

	/* specific clusters requested? */
	if(all_clusters) {
		if(params.opt_cluster_list 
		   && list_count(params.opt_cluster_list)) {
			list_destroy(params.opt_cluster_list);
			params.opt_cluster_list = NULL;
		}
		if(params.opt_verbose)
			fprintf(stderr, "Clusters requested:\n\t: all\n");
	} else if (params.opt_verbose && params.opt_cluster_list 
	    && list_count(params.opt_cluster_list)) {
		fprintf(stderr, "Clusters requested:\n");
		itr = list_iterator_create(params.opt_cluster_list);
		while((start = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", start);
		list_iterator_destroy(itr);
	} else if(!params.opt_cluster_list 
		  || !list_count(params.opt_cluster_list)) {
		if(!params.opt_cluster_list)
			params.opt_cluster_list =
				list_create(slurm_destroy_char);
		if((start = slurm_get_cluster_name()))
			list_append(params.opt_cluster_list, start);
		if(params.opt_verbose) {
			fprintf(stderr, "Clusters requested:\n");
			fprintf(stderr, "\t: %s\n", start);
		}
	}

	if(all_users) {
		if(params.opt_uid_list 
		   && list_count(params.opt_uid_list)) {
			list_destroy(params.opt_uid_list);
			params.opt_uid_list = NULL;
		}
		if(params.opt_verbose)
			fprintf(stderr, "Userids requested:\n\t: all\n");
	} else if (params.opt_verbose && params.opt_uid_list 
	    && list_count(params.opt_uid_list)) {
		fprintf(stderr, "Userids requested:\n");
		itr = list_iterator_create(params.opt_uid_list);
		while((start = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", start);
		list_iterator_destroy(itr);
	} else if(!params.opt_uid_list 
		      || !list_count(params.opt_uid_list)) {
		if(!params.opt_uid_list)
			params.opt_uid_list =
				list_create(slurm_destroy_char);
		start = xstrdup_printf("%u", params.opt_uid);
		list_append(params.opt_uid_list, start);
		if(params.opt_verbose) {
			fprintf(stderr, "Userids requested:\n");
			fprintf(stderr, "\t: %s\n", start);
		}
	}

	if (params.opt_verbose && params.opt_gid_list 
	    && list_count(params.opt_gid_list)) {
		fprintf(stderr, "Groupids requested:\n");
		itr = list_iterator_create(params.opt_gid_list);
		while((start = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", start);
		list_iterator_destroy(itr);
	} 

	/* specific partitions requested? */
	if (params.opt_verbose && params.opt_partition_list 
	    && list_count(params.opt_partition_list)) {
		fprintf(stderr, "Partitions requested:\n");
		itr = list_iterator_create(params.opt_partition_list);
		while((start = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", start);
		list_iterator_destroy(itr);
	}

	/* specific jobs requested? */
	if (params.opt_verbose && params.opt_job_list
	    && list_count(params.opt_job_list)) { 
		fprintf(stderr, "Jobs requested:\n");
		itr = list_iterator_create(params.opt_job_list);
		while((selected_step = list_next(itr))) {
			if(selected_step->stepid != NO_VAL) 
				fprintf(stderr, "\t: %d.%d\n",
					selected_step->jobid,
					selected_step->stepid);
			else	
				fprintf(stderr, "\t: %d\n", 
					selected_step->jobid);
		}
		list_iterator_destroy(itr);
	}

	/* specific states (completion state) requested? */
	if (params.opt_verbose && params.opt_state_list
	    && list_count(params.opt_state_list)) {
		fprintf(stderr, "States requested:\n");
		itr = list_iterator_create(params.opt_state_list);
		while((start = list_next(itr))) {
			fprintf(stderr, "\t: %s\n", 
				job_state_string(atoi(start)));
		}
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
		if (params.opt_dump || params.opt_expire)
			goto endopt;
		if(params.opt_completion)
			dot = DEFAULT_COMP_FIELDS;
		else
			dot = DEFAULT_FIELDS;

		xstrfmtcat(params.opt_field_list, "%s,", dot);
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
		fprintf(stderr,
			"Invalid field requested: \"%s\"\n",
			start);
		exit(1);
	foundfield:
		printfields[nprintfields++] = i;
		start = end + 1;
	}
	if (params.opt_verbose) {
		fprintf(stderr, "%d field%s selected:\n",
			nprintfields,
			(nprintfields==1? "" : "s"));
		for (i = 0; i < nprintfields; i++)
			fprintf(stderr,
				"\t%s\n",
				fields[printfields[i]].name);
	} 
endopt:
	if (optind < argc) {
		fprintf(stderr, "Error: Unknown arguments:");
		for (i=optind; i<argc; i++)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n");
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
#ifdef HAVE_BG
		if(job->blockid)
			printf(" %s %s %s %s %u %s %s",
			       job->blockid, job->connection, job->reboot,
			       job->rotate, job->max_procs, job->geo,
			       job->bg_start_point);
#endif
		printf("\n");
	}
	list_iterator_destroy(itr);
}

/* do_expire() -- purge expired data from the accounting log file
 */

void do_expire()
{
	if(params.opt_completion) 
		g_slurm_jobcomp_archive(params.opt_partition_list, &params);
	else
		jobacct_storage_g_archive(acct_db_conn,
					  params.opt_partition_list, &params);
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
		fprintf(stderr, "sacct bug: params.opt_help=%d\n", 
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
	int do_jobsteps = 1;
	
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	jobacct_job_rec_t *job = NULL;
	jobacct_step_rec_t *step = NULL;
	
	if (params.opt_total)
		do_jobsteps = 0;
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

		if (job->show_full) {
			print_fields(JOB, job);
		}
		
		if (do_jobsteps && (job->track_steps || !job->show_full)) {
			itr_step = list_iterator_create(job->steps);
			while((step = list_next(itr_step))) {
				if(step->end == 0)
					step->end = job->end;
				step->account = job->account;
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
	
	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		print_fields(JOBCOMP, job);
	}
	list_iterator_destroy(itr);
}

void do_stat()
{
	ListIterator itr = NULL;
	uint32_t stepid = 0;
	jobacct_selected_step_t *selected_step = NULL;

	if(!params.opt_job_list || !list_count(params.opt_job_list)) {
		fprintf(stderr, "No job list given to stat.\n");
		return;
	}

	itr = list_iterator_create(params.opt_job_list);
	while((selected_step = list_next(itr))) {
		if(selected_step->stepid != NO_VAL)
			stepid = selected_step->stepid;
		else
			stepid = 0;
		sacct_stat(selected_step->jobid, stepid);
	}
	list_iterator_destroy(itr);
}

void sacct_init()
{
}

void sacct_fini()
{
	if(jobs)
		list_destroy(jobs);
	if(params.opt_completion)
		g_slurm_jobcomp_fini();
	else {
		acct_storage_g_close_connection(&acct_db_conn);
		slurm_acct_storage_fini();
	}
}
