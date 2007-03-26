/*****************************************************************************\
 *  options.c - option functions for sacct
 *
 *  $Id: options.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-226842.
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
#include "src/common/slurm_database.h"
#include "sacct.h"
#include <time.h>

void _destroy_parts(void *object);
void _destroy_steps(void *object);
void _dump_header(acct_header_t header);
void _help_fields_msg(void);
void _help_msg(void);
void _usage(void);
void _init_params();

int selected_status[STATUS_COUNT];
List selected_parts = NULL;
List selected_steps = NULL;

void _destroy_parts(void *object)
{
	char *part = (char *)object;
	xfree(part);
}

void _destroy_steps(void *object)
{
	selected_step_t *step = (selected_step_t *)object;
	if(step) {
		xfree(step->job);
		xfree(step->step);
		xfree(step);
	}
}

void _show_rec(char *f[])
{
	int 	i;
	fprintf(stderr, "rec>");
	for (i=0; f[i]; i++)
		fprintf(stderr, " %s", f[i]);
	fprintf(stderr, "\n");
	return;
}

/* _dump_header() -- dump the common fields of a record
 *
 * In:	Index into the jobs table
 * Out: Nothing.
 */
void _dump_header(acct_header_t header)
{
	struct tm ts;
	gmtime_r(&header.timestamp, &ts);
	printf("%u %s %04d%02d%02d%02d%02d%02d %d %s %s ",
	       header.jobnum,
	       header.partition,
	       1900+(ts.tm_year),
	          1+(ts.tm_mon),
		  ts.tm_mday,
	          ts.tm_hour,
		  ts.tm_min,
		  ts.tm_sec,
	       (int)header.job_submit,
	       header.blockid,	/* block id */
	       "-");	/* reserved 1 */
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
	       "    Equivalent to \"--fields=jobstep,status,error\". This option\n"
	       "    has no effect if --dump is specified.\n"
	       "-d, --dump\n"
	       "    Dump the raw data records\n"
	       "--duplicates\n"
	       "    If SLURM job ids are reset, but the job accounting log file\n"
	       "    isn't reset at the same time (with -e, for example), some\n"
	       "    job numbers will probably appear more than once in the\n"
	       "    accounting log file to refer to different jobs; such jobs\n"
	       "    can be distinguished by the \"job_submit\" time stamp in the\n"
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
	       "    we use \"--fields=jobstep,jobname,partition,ncpus,status,error\".\n"
	       "-f<file>, --file=<file>\n"
	       "    Read data from the specified file, rather than SLURM's current\n"
	       "    accounting log file.\n"
	       "-l, --long\n"
	       "    Equivalent to specifying\n"
	       "    \"--fields=jobstep,usercpu,systemcpu,minflt,majflt,nprocs,\n"
	       "    ncpus,elapsed,status,exitcode\"\n"
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
	       "    Select jobs based on their current status: running (r),\n"
	       "    completed (cd), failed (f), timeout (to), and node_fail (nf).\n"
	       "-S, --stat\n"
	       "    Get real time status of a jobstep supplied by the -j\n"
	       "    option\n" 
	       "-t, --total\n"
	       "    Only show cumulative statistics for each job, not the\n"
	       "    intermediate steps\n"
	       "-u <uid>, --uid <uid>\n"
	       "    Select only jobs submitted by the user with uid <uid>.  Only\n"
	       "    root users are allowed to specify a uid other than their own.\n"
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
	params.opt_dump = 0;		/* --dump */
	params.opt_dup = -1;		/* --duplicates; +1 = explicitly set */
	params.opt_fdump = 0;		/* --formattted_dump */
	params.opt_stat = 0;		/* --stat */
	params.opt_gid = -1;		/* --gid (-1=wildcard, 0=root) */
	params.opt_header = 1;		/* can only be cleared */
	params.opt_help = 0;		/* --help */
	params.opt_long = 0;		/* --long */
	params.opt_lowmem = 0;		/* --low_memory */
	params.opt_purge = 0;		/* --purge */
	params.opt_total = 0;		/* --total */
	params.opt_uid = -1;		/* --uid (-1=wildcard, 0=root) */
	params.opt_verbose = 0;		/* --verbose */
	params.opt_expire_timespec = NULL; /* --expire= */
	params.opt_field_list = NULL;	/* --fields= */
	params.opt_filein = NULL;	/* --file */
	params.opt_job_list = NULL;	/* --jobs */
	params.opt_partition_list = NULL;/* --partitions */
	params.opt_state_list = NULL;	/* --states */
}

int decode_status_char(char *status)
{
	if (!strcasecmp(status, "p"))
		return JOB_PENDING; 	/* we should never see this */
	else if (!strcasecmp(status, "r"))
		return JOB_RUNNING;
	else if (!strcasecmp(status, "su"))
		return JOB_SUSPENDED;
	else if (!strcasecmp(status, "cd"))
		return JOB_COMPLETE;
	else if (!strcasecmp(status, "ca"))
		return JOB_CANCELLED;
	else if (!strcasecmp(status, "f"))
		return JOB_FAILED;
	else if (!strcasecmp(status, "to"))
		return JOB_TIMEOUT;
	else if (!strcasecmp(status, "nf"))
		return JOB_NODE_FAIL;
	else
		return -1; // unknown
} 

int get_data(void)
{
	job_rec_t *job = NULL;
	step_rec_t *step = NULL;

	ListIterator itr = NULL;
	ListIterator itr_step = NULL;

	database_g_jobacct_get_jobs(jobs, selected_steps,
				    selected_parts, &params);
	if (params.opt_fdump) {
		return SLURM_SUCCESS;
	}
	
	itr = list_iterator_create(jobs);
	while((job = list_next(itr)) != NULL) {
		if(!list_count(job->steps))
			continue;
	
		itr_step = list_iterator_create(job->steps);
		while((step = list_next(itr)) != NULL) {
			/* now aggregate the aggregatable */
			job->ncpus = MAX(job->ncpus, step->ncpus);
			if(step->status < JOB_COMPLETE)
				continue;
			job->tot_cpu_sec += step->tot_cpu_sec;
			job->tot_cpu_usec += step->tot_cpu_usec;
			job->rusage.ru_utime.tv_sec +=
				step->rusage.ru_utime.tv_sec;
			job->rusage.ru_utime.tv_usec +=
				step->rusage.ru_utime.tv_usec;
			job->rusage.ru_stime.tv_sec +=
				step->rusage.ru_stime.tv_sec;
			job->rusage.ru_stime.tv_usec +=
				step->rusage.ru_stime.tv_usec;
			job->rusage.ru_inblock += step->rusage.ru_inblock;
			job->rusage.ru_oublock += step->rusage.ru_oublock;
			job->rusage.ru_msgsnd += step->rusage.ru_msgsnd;
			job->rusage.ru_msgrcv += step->rusage.ru_msgrcv;
			job->rusage.ru_nsignals += step->rusage.ru_nsignals;
			job->rusage.ru_nvcsw += step->rusage.ru_nvcsw;
			job->rusage.ru_nivcsw += step->rusage.ru_nivcsw;
			
			/* and finally the maximums for any process */
			job->rusage.ru_maxrss = MAX(job->rusage.ru_maxrss,
						    step->rusage.ru_maxrss);
			job->rusage.ru_ixrss = MAX(job->rusage.ru_ixrss,
						   step->rusage.ru_ixrss);
			job->rusage.ru_idrss = MAX(job->rusage.ru_idrss,
						   step->rusage.ru_idrss);
			job->rusage.ru_isrss = MAX(job->rusage.ru_isrss,
						   step->rusage.ru_isrss);
			job->rusage.ru_minflt = MAX(job->rusage.ru_minflt,
						    step->rusage.ru_minflt);
			job->rusage.ru_majflt = MAX(job->rusage.ru_majflt,
						    step->rusage.ru_majflt);
			job->rusage.ru_nswap = MAX(job->rusage.ru_nswap,
						   step->rusage.ru_nswap);
			/* get the max for all the sacct_t struct */
			aggregate_sacct(&job->sacct, &step->sacct);
		}
	}

	return SLURM_SUCCESS;
} 

void parse_command_line(int argc, char **argv)
{
	extern int optind;
	int c, i, optionIndex = 0;
	char *end = NULL, *start = NULL, *acct_type = NULL;
	selected_step_t *selected_step = NULL;
	ListIterator itr = NULL;
	struct stat stat_buf;
	char *dot = NULL;

	static struct option long_options[] = {
		{"all", 0,0, 'a'},
		{"brief", 0, 0, 'b'},
		{"duplicates", 0, &params.opt_dup, 1},
		{"dump", 0, 0, 'd'},
		{"expire", 1, 0, 'e'},
		{"fields", 1, 0, 'F'},
		{"file", 1, 0, 'f'},
		{"formatted_dump", 0, 0, 'O'},
		{"stat", 0, 0, 'S'},
		{"gid", 1, 0, 'g'},
		{"group", 1, 0, 'g'},
		{"help", 0, &params.opt_help, 1},
		{"help-fields", 0, &params.opt_help, 2},
		{"jobs", 1, 0, 'j'},
		{"long", 0, 0, 'l'},
		{"big_logfile", 0, &params.opt_lowmem, 1},
		{"noduplicates", 0, &params.opt_dup, 0},
		{"noheader", 0, &params.opt_header, 0},
		{"partition", 1, 0, 'p'},
		{"purge", 0, 0, 'P'},
		{"state", 1, 0, 's'},
		{"total", 0, 0,  't'},
		{"uid", 1, 0, 'u'},
		{"usage", 0, &params.opt_help, 3},
		{"user", 1, 0, 'u'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0}};

	_init_params();

	if ((i=getuid()))	/* default to current user unless root*/
		params.opt_uid = i;

	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv, "abde:F:f:g:hj:J:lOPp:s:StUu:Vv",
				long_options, &optionIndex);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
			params.opt_uid = -1;
			break;
		case 'b':
			params.opt_field_list =
				xrealloc(params.opt_field_list,
					 (params.opt_field_list==NULL? 0 :
					  sizeof(params.opt_field_list)) +
					 sizeof(BRIEF_FIELDS)+1);
			strcat(params.opt_field_list, BRIEF_FIELDS);
			strcat(params.opt_field_list, ",");
			break;

		case 'd':
			params.opt_dump = 1;
			break;

		case 'e':
		{	/* decode the time spec */
			long	acc=0;
			params.opt_expire_timespec = strdup(optarg);
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
		params.opt_uid = -1;	/* fix default; can't purge by uid */
		break;

		case 'F':
			if(params.opt_stat)
				xfree(params.opt_field_list);
			
			params.opt_field_list =
				xrealloc(params.opt_field_list,
					 (params.opt_field_list==NULL? 0 :
					  strlen(params.opt_field_list)) +
					 strlen(optarg) + 1);
			strcat(params.opt_field_list, optarg);
			strcat(params.opt_field_list, ",");
			break;

		case 'f':
			params.opt_filein =
				xrealloc(params.opt_filein, strlen(optarg)+1);
			strcpy(params.opt_filein, optarg);
			break;

		case 'g':
			if (isdigit((int) *optarg))
				params.opt_gid = atoi(optarg);
			else {
				struct group *grp;
				if ((grp=getgrnam(optarg))==NULL) {
					fprintf(stderr,
						"Invalid group id: %s\n",
						optarg);
					exit(1);
				}
				params.opt_gid=grp->gr_gid;
			}
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
			params.opt_job_list =
				xrealloc(params.opt_job_list,
					 (params.opt_job_list==NULL? 0 :
					  strlen(params.opt_job_list)) +
					 strlen(optarg) + 1);
			strcat(params.opt_job_list, optarg);
			strcat(params.opt_job_list, ",");
			break;

		case 'l':
			params.opt_field_list =
				xrealloc(params.opt_field_list,
					 (params.opt_field_list==NULL? 0 :
					  strlen(params.opt_field_list)) +
					 sizeof(LONG_FIELDS)+1);
			strcat(params.opt_field_list, LONG_FIELDS);
			strcat(params.opt_field_list, ",");
			break;

		case 'O':
			params.opt_fdump = 1;
			break;

		case 'P':
			params.opt_purge = 1;
			break;

		case 'p':
			params.opt_partition_list =
				xrealloc(params.opt_partition_list,
					 (params.opt_partition_list==NULL? 0 :
					  strlen(params.opt_partition_list)) +
					 strlen(optarg) + 1);
			strcat(params.opt_partition_list, optarg);
			strcat(params.opt_partition_list, ",");
			break;

		case 's':
			params.opt_state_list =
				xrealloc(params.opt_state_list,
					 (params.opt_state_list==NULL? 0 :
					  strlen(params.opt_state_list)) +
					 strlen(optarg) + 1);
			strcat(params.opt_state_list, optarg);
			strcat(params.opt_state_list, ",");
			break;

		case 'S':
			if(!params.opt_field_list) {
				params.opt_field_list = 
					xmalloc(sizeof(STAT_FIELDS)+1);
				strcat(params.opt_field_list, STAT_FIELDS);
				strcat(params.opt_field_list, ",");
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
			if (isdigit((int) *optarg))
				params.opt_uid = atoi(optarg);
			else {
				struct passwd *pwd;
				if ((pwd=getpwnam(optarg))==NULL) {
					fprintf(stderr, 
						"Invalid user id: %s\n",
						optarg);
					exit(1);
				}
				params.opt_uid=pwd->pw_uid;
			}
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
		{
			char	obuf[20]; /* should be long enough */
			char	*rev="$Revision: 7267 $";
			char	*s;

			s=strstr(rev, " ")+1;
			for (i=0; s[i]!=' '; i++)
				obuf[i]=s[i];
			obuf[i] = 0;
			printf("%s: %s\n", argv[0], obuf);
			exit(0);
		}

		case ':':
		case '?':	/* getopt() has explained it */
			exit(1); 
		}
	}

	/* Now set params.opt_dup, unless they've already done so */
	if (params.opt_dup < 0)	/* not already set explicitly */
		if (params.opt_job_list)
			/* They probably want the most recent job N if
			 * they requested specific jobs or steps. */
			params.opt_dup = 0;

	if (params.opt_verbose) {
		fprintf(stderr, "Options selected:\n"
			"\topt_dump=%d\n"
			"\topt_dup=%d\n"
			"\topt_expire=%s (%lu seconds)\n"
			"\topt_fdump=%d\n"
			"\topt_stat=%d\n"
			"\topt_field_list=%s\n"
			"\topt_filein=%s\n"
			"\topt_header=%d\n"
			"\topt_help=%d\n"
			"\topt_job_list=%s\n"
			"\topt_long=%d\n"
			"\topt_lowmem=%d\n"
			"\topt_partition_list=%s\n"
			"\topt_purge=%d\n"
			"\topt_state_list=%s\n"
			"\topt_total=%d\n"
			"\topt_uid=%d\n"
			"\topt_verbose=%d\n",
			params.opt_dump,
			params.opt_dup,
			params.opt_expire_timespec, params.opt_expire,
			params.opt_fdump,
			params.opt_stat,
			params.opt_field_list,
			params.opt_filein,
			params.opt_header,
			params.opt_help,
			params.opt_job_list,
			params.opt_long,
			params.opt_lowmem,
			params.opt_partition_list,
			params.opt_purge,
			params.opt_state_list,
			params.opt_total,
			params.opt_uid,
			params.opt_verbose);
	}

	/* check if we have accounting data to view */
	if (params.opt_filein == NULL)
		params.opt_filein = slurm_get_jobacct_loc();
	acct_type = slurm_get_jobacct_type();
	if ((strcmp(acct_type, "jobacct/none") == 0)
	    &&  (stat(params.opt_filein, &stat_buf) != 0)) {
		fprintf(stderr, "SLURM accounting is disabled\n");
		exit(1);
	}
	xfree(acct_type);

	/* specific partitions requested? */
	if (params.opt_partition_list) {

		start = params.opt_partition_list;
		while ((end = strstr(start, ",")) && start) {
			*end = 0;
			while (isspace(*start))
				start++;	/* discard whitespace */
			if(!(int)*start)
				continue;
			acct_type = xstrdup(start);
			list_append(selected_parts, acct_type);
			start = end + 1;
		}
		if (params.opt_verbose) {
			fprintf(stderr, "Partitions requested:\n");
			itr = list_iterator_create(selected_parts);
			while((start = list_next(itr))) 
				fprintf(stderr, "\t: %s\n", start);
			list_iterator_destroy(itr);
		}
	}

	/* specific jobs requested? */
	if (params.opt_job_list) { 
		start = params.opt_job_list;
		while ((end = strstr(start, ",")) && start) {
			*end = 0;
			while (isspace(*start))
				start++;	/* discard whitespace */
			if(!(int)*start)
				continue;
			selected_step = xmalloc(sizeof(selected_step_t));
			list_append(selected_steps, selected_step);
			
			dot = strstr(start, ".");
			if (dot == NULL) {
				debug2("No jobstep requested");
				selected_step->step = NULL;
			} else {
				*dot++ = 0;
				selected_step->step = xstrdup(dot);
			}
			selected_step->job = xstrdup(start);
			start = end + 1;
		}
		if (params.opt_verbose) {
			fprintf(stderr, "Jobs requested:\n");
			itr = list_iterator_create(selected_steps);
			while((selected_step = list_next(itr))) {
				if(selected_step->step) 
					fprintf(stderr, "\t: %s.%s\n",
						selected_step->job,
						selected_step->step);
				else	
					fprintf(stderr, "\t: %s\n", 
						selected_step->job);
			}
			list_iterator_destroy(itr);
		}
	}

	/* specific states (completion status) requested? */
	if (params.opt_state_list) {
		start = params.opt_state_list;
		while ((end = strstr(start, ",")) && start) {
			int c;
			*end = 0;
			while (isspace(*start))
				start++;	/* discard whitespace */
			if(!(int)*start)
				continue;
			c = decode_status_char(start);
			if (c == -1)
				fatal("unrecognized job state value");
			selected_status[c] = 1;
			start = end + 1;
		}
		if (params.opt_verbose) {
			fprintf(stderr, "States requested:\n");
			for(i=0; i< STATUS_COUNT; i++) {
				if(selected_status[i]) {
					fprintf(stderr, "\t: %s\n", 
						job_state_string(i));
					break;
				}
			}
		}
	}

	/* select the output fields */
	if (params.opt_field_list==NULL) {
		if (params.opt_dump || params.opt_expire)
			goto endopt;
		params.opt_field_list = xmalloc(sizeof(DEFAULT_FIELDS)+1);
		strcpy(params.opt_field_list, DEFAULT_FIELDS); 
		strcat(params.opt_field_list, ",");
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
	job_rec_t *job = NULL;
	step_rec_t *step = NULL;
	struct tm ts;
	
	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		if (!params.opt_dup)
			if (job->jobnum_superseded) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Note: Skipping older"
						" job %u dated %d\n",
						job->header.jobnum,
						(int)job->header.job_submit);
				continue;
			}
		if (params.opt_uid>=0)
			if (job->header.uid != params.opt_uid)
				continue;
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
			if (!job->job_start_seen && job->job_step_seen) {
				/* If we only saw JOB_TERMINATED, the
				 * job was probably canceled. */ 
				fprintf(stderr,
					"Error: No JOB_START record for "
					"job %u\n",
					job->header.jobnum);
			}
			_dump_header(job->header);
			printf("JOB_START 1 16 %d %d %s %d %d %d %s %s\n", 
			       job->header.uid,
			       job->header.gid,
			       job->jobname,
			       job->track_steps,
			       job->priority,
			       job->ncpus,
			       job->nodes,
			       job->account);
		}
		/* JOB_STEP */
		itr_step = list_iterator_create(job->steps);
		while((step = list_next(itr_step))) {
			if (step->status == JOB_RUNNING &&
			    job->job_terminated_seen) {
				step->status = JOB_FAILED;
				step->exitcode=1;
			}
			_dump_header(step->header);
			if(step->end == 0)
				step->end = job->end;
				
			gmtime_r(&step->end, &ts);
			printf("JOB_STEP 1 50 %u %04d%02d%02d%02d%02d%02d ",
			       step->stepnum,
			       1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
			            ts.tm_hour, ts.tm_min, ts.tm_sec);
			printf("%s %d %d %d %d ",
			       job_state_string_compact(step->status),
			       step->exitcode,
			       step->ntasks,
			       step->ncpus,
			       step->elapsed);
			printf("%d %d %d %d %d %d ",
			       step->tot_cpu_sec,
			       step->tot_cpu_usec,
			       (int)step->rusage.ru_utime.tv_sec,
			       (int)step->rusage.ru_utime.tv_usec,
			       (int)step->rusage.ru_stime.tv_sec,
			       (int)step->rusage.ru_stime.tv_usec);
			printf("%d %d %d %d %d %d %d %d %d "
			       "%d %d %d %d %d %d %d ",
			       (int)step->rusage.ru_maxrss,
			       (int)step->rusage.ru_ixrss,
			       (int)step->rusage.ru_idrss,
			       (int)step->rusage.ru_isrss,
			       (int)step->rusage.ru_minflt,
			       (int)step->rusage.ru_majflt,
			       (int)step->rusage.ru_nswap,
			       (int)step->rusage.ru_inblock,
			       (int)step->rusage.ru_oublock,
			       (int)step->rusage.ru_msgsnd,
			       (int)step->rusage.ru_msgrcv,
			       (int)step->rusage.ru_nsignals,
			       (int)step->rusage.ru_nvcsw,
			       (int)step->rusage.ru_nivcsw,
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
			_dump_header(job->header);
			gmtime_r(&job->end, &ts);
			printf("JOB_TERMINATED 1 50 %d ",
			       job->elapsed);
			printf("%04d%02d%02d%02d%02d%02d ",
			1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
			      ts.tm_hour, ts.tm_min, ts.tm_sec); 
			printf("%s %d %d %d %d ",
			       job_state_string_compact(job->status),
			       job->exitcode,
			       job->ntasks,
			       job->ncpus,
			       job->elapsed);
			printf("%d %d %d %d %d %d ",
			       job->tot_cpu_sec,
			       job->tot_cpu_usec,
			       (int)job->rusage.ru_utime.tv_sec,
			       (int)job->rusage.ru_utime.tv_usec,
			       (int)job->rusage.ru_stime.tv_sec,
			       (int)job->rusage.ru_stime.tv_usec);
			printf("%d %d %d %d %d %d %d %d %d "
			       "%d %d %d %d %d %d %d ",
			       (int)job->rusage.ru_maxrss,
			       (int)job->rusage.ru_ixrss,
			       (int)job->rusage.ru_idrss,
			       (int)job->rusage.ru_isrss,
			       (int)job->rusage.ru_minflt,
			       (int)job->rusage.ru_majflt,
			       (int)job->rusage.ru_nswap,
			       (int)job->rusage.ru_inblock,
			       (int)job->rusage.ru_oublock,
			       (int)job->rusage.ru_msgsnd,
			       (int)job->rusage.ru_msgrcv,
			       (int)job->rusage.ru_nsignals,
			       (int)job->rusage.ru_nvcsw,
			       (int)job->rusage.ru_nivcsw,
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

/* do_expire() -- purge expired data from the accounting log file
 */

void do_expire(void)
{
	database_g_jobacct_archive(selected_parts, &params);
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
	int rc = 0;
	
	ListIterator itr = NULL;
	ListIterator itr_step = NULL;
	job_rec_t *job = NULL;
	step_rec_t *step = NULL;
	
	if (params.opt_total)
		do_jobsteps = 0;
	
	itr = list_iterator_create(jobs);
	while((job = list_next(itr))) {
		if (!params.opt_dup)
			if (job->jobnum_superseded) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Note: Skipping older"
						" job %u dated %d\n",
						job->header.jobnum,
						(int)job->header.job_submit);
				continue;
			}
		if (!job->job_start_seen && job->job_step_seen) {
			/* If we only saw JOB_TERMINATED, the job was
			 * probably canceled. */
			fprintf(stderr,
				"Error: No JOB_START record for job %u\n",
				job->header.jobnum);
			if (rc<ERROR)
				rc = ERROR;
		}
		if (params.opt_verbose > 1) {
			if (!job->job_start_seen)
				fprintf(stderr,
					"Note: No JOB_START record for "
					"job %u\n",
					job->header.jobnum);
			if (!job->job_step_seen)
				fprintf(stderr,
					"Note: No JOB_STEP record for "
					"job %u\n",
					job->header.jobnum);
			if (!job->job_terminated_seen)
				fprintf(stderr,
					"Note: No JOB_TERMINATED record for "
					"job %u\n",
					job->header.jobnum);
		}
		if (params.opt_uid >= 0 && (job->header.uid != params.opt_uid))
			continue;
		if (params.opt_gid >= 0 && (job->header.gid != params.opt_gid))
			continue;
		if(job->sacct.min_cpu == NO_VAL)
			job->sacct.min_cpu = 0;
		
		if(list_count(job->steps)) {
			job->sacct.ave_cpu /= list_count(job->steps);
			job->sacct.ave_rss /= list_count(job->steps);
			job->sacct.ave_vsize /= list_count(job->steps);
			job->sacct.ave_pages /= list_count(job->steps);
		}

		if (job->show_full) {
			if (params.opt_state_list) {
				if(!selected_status[job->status])
					continue;
			}
			print_fields(JOB, job);
		}
		
		if (do_jobsteps && (job->track_steps || !job->show_full)) {
			itr_step = list_iterator_create(job->steps);
			while((step = list_next(itr_step))) {
				if (step->status == JOB_RUNNING 
				    && job->job_terminated_seen) {
					step->status = JOB_FAILED;
				}
				if (params.opt_state_list) {
					if(!selected_status[step->status])
						continue;
				}
				if(step->end == 0)
					step->end = job->end;
				print_fields(JOBSTEP, step);
			} 
			list_iterator_destroy(itr_step);
		}
	}
	list_iterator_destroy(itr);
}

void do_stat()
{
	ListIterator itr = NULL;
	uint32_t jobid = 0;
	uint32_t stepid = 0;
	selected_step_t *selected_step = NULL;
	
	itr = list_iterator_create(selected_steps);
	while((selected_step = list_next(itr))) {
		jobid = atoi(selected_step->job);
		if(selected_step->step)
			stepid = atoi(selected_step->step);
		else
			stepid = 0;
		sacct_stat(jobid, stepid);
	}
	list_iterator_destroy(itr);
}
void sacct_init()
{
	int i=0;
	jobs = list_create(destroy_job);
	selected_parts = list_create(_destroy_parts);
	selected_steps = list_create(_destroy_steps);
	for(i=0; i<STATUS_COUNT; i++)
		selected_status[i] = 0;
}

void sacct_fini()
{
	list_destroy(jobs);
	list_destroy(selected_parts);
	list_destroy(selected_steps);
}
