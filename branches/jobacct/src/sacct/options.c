/*****************************************************************************\
 *  options.c - option functions for sacct
 *
 *  $Id: options.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-217948.
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

#include "sacct.h"

int expire_time_match = 0;	/* How much of the timestamp is significant
				   for --expire */
int NjobstepsSelected = 0;
char *partitionsSelected[MAX_PARTITIONS];
int NpartitionsSelected = 0;
char *statesSelected[MAX_STATES];
int NstatesSelected = 0;


typedef struct	expired_table {  /* table of expired jobs */
	long	job;
	char	submitted[TIMESTAMP_LENGTH];
} 	expired_table_t;

void _show_rec(char *f[]);
int _cmp_jrec(const void *a1, const void *a2);
void _dump_header(long j);
FILE *_open_log_file(void);
void _help_fields_msg(void);
void _help_msg(void);
void _usage(void);
void _init_params();
char *_prefix_filename(char *path, char *prefix);



void _show_rec(char *f[])
{
	int 	i;
	fprintf(stderr, "rec>");
	for (i=0; f[i]; i++)
		fprintf(stderr, " %s", f[i]);
	fprintf(stderr, "\n");
	return;
}

int _cmp_jrec(const void *a1, const void *a2) {
	expired_table_t *j1 = (expired_table_t *) a1;
	expired_table_t *j2 = (expired_table_t *) a2;

	if (j1->job <  j2->job)
		return -1;
	else if (j1->job == j2->job)
		return strncmp(j1->submitted, j2->submitted, TIMESTAMP_LENGTH);
	return 1;
}

/* _dump_header() -- dump the common fields of a record
 *
 * In:	Index into the jobs table
 * Out: Nothing.
 */
void _dump_header(long j)
{
	printf("%ld %s %s %ld %s %s ",
	       jobs[j].job,
	       jobs[j].partition,
	       jobs[j].submitted,
	       jobs[j].starttime,
	       "-",	/* reserved 2 */
	       "-"	/* reserved 1 */
		);
}
/* _open_log_file() -- find the current or specified log file, and open it
 *
 * IN:		Nothing
 * RETURNS:	Nothing
 *
 * Side effects:
 * 	- Sets opt_filein to the current system accounting log unless
 * 	  the user specified another file.
 */

FILE *_open_log_file(void)
{
	FILE *fd = fopen(params.opt_filein, "r");
	if (fd == NULL) {
		perror(params.opt_filein);
		exit(1);
	}
	return fd;
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
	       "    * The default input file is the file named in the \"jobacct_loc\"\n"
	       "      parameter in " SLURM_CONFIG_FILE ".\n"
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
	       "    can be distinguished by the \"submitted\" time stamp in the\n"
	       "    data records.\n"
	       "      When data for specific jobs or jobsteps are requested with\n"
	       "    the --jobs or --jobsteps options, we assume that the user\n"
	       "    wants to see only the most recent job with that number. This\n"
	       "    behavior can be overridden by specifying --duplicates, in\n"
	       "    which case all records that match the selection criteria\n"
	       "    will be returned.\n"
	       "      When neither --jobs or --jobsteps is specified, we report\n"
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
	       "    ncpus,elapsed,status,error\"\n"
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
	       "-j <job_list>, --jobs=<job_list>\n"
	       "    Display information about this job or comma-separated\n"
	       "    list of jobs. The default is all jobs.\n"
	       "-J <job.step>, --jobstep=<job.step>\n"
	       "    Show data only for the specified step of the specified job.\n"
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
	       "    variables during processing.\n");
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
	params.opt_jobstep_list = NULL;	/* --jobstep */
	params.opt_partition_list = NULL;/* --partitions */
	params.opt_state_list = NULL;	/* --states */
}

/* prefix_filename() -- insert a filename prefix into a path
 *
 * IN:	path = fully-qualified path+file name
 *      prefix = the prefix to insert into the file name
 * RETURNS: pointer to the updated path+file name
 */

char *_prefix_filename(char *path, char *prefix) {
	char	*out;
	int     i,
		plen;

	plen = strlen(path);
	out = xmalloc(plen+strlen(prefix)+1);
	for (i=plen-1; i>=0; i--)
		if (path[i]=='/') {
			break;
		}
	i++;
	*out = 0;
	strncpy(out, path, i);
	out[i] = 0;
	strcat(out, prefix);
	strcat(out, path+i);
	return(out);
}

int get_data(void)
{
	char line[BUFFER_SIZE];
	char	*f[MAX_RECORD_FIELDS+2],    /* End list with null entry and,
					       possibly, more data than we
					       expected */
		*fptr;
	int	bufsize=MINBUFSIZE,
		i;
	FILE *fd = NULL;
	int lc = 0;

	fd = _open_log_file();
	
	while (fgets(line, BUFFER_SIZE, fd)) {
		lc++;
		fptr = line;	/* break the record into NULL-
				   terminated strings */
		for (i = 0; i < MAX_RECORD_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL) {
				fptr = strstr(f[i], "\n");
				if (fptr)
					*fptr = 0;
				break; 
			}
			else
				*fptr++ = 0;
		}
		f[++i] = 0;
		if (i < F_NUMFIELDS) {
			if (params.opt_verbose > 1)
				fprintf(stderr,
					"Invalid record (too short) "
					"at input line %ld\n", lc);
			if (params.opt_verbose > 2)
				_show_rec(f);
			inputError++;
			continue;
		} else if (i > MAX_RECORD_FIELDS) {
			if (params.opt_verbose > 1)
				fprintf(stderr,
					"Extra data at input line %ld\n", lc);
			if (params.opt_verbose > 2)
				_show_rec(f);
			inputError++;
		}
		if (strcmp(f[F_RECVERSION], "1")) {
			if (params.opt_verbose > 1)
				fprintf(stderr,
					"Invalid record version at input"
					" line %ld\n",
					lc);
			if (params.opt_verbose > 2)
				_show_rec(f);
			inputError++;
			continue;
		}
		if (NjobstepsSelected) {
			for (i = 0; i < NjobstepsSelected; i++) {
				if (strcmp(jobstepsSelected[i].job, f[F_JOB]))
					continue;
				/* job matches; does the step> */
				if (jobstepsSelected[i].step == NULL)
					goto foundjob;
				/* is it anything but JOB_STEP? */
				if (strcmp(f[F_RECTYPE], "JOB_STEP"))
					goto foundjob;
				if (strcmp(f[F_JOBSTEP],
					   jobstepsSelected[i].step) == 0)
					goto foundjob;
			}
			continue;	/* no match */
		}
	foundjob:
		if (NpartitionsSelected) {
			for (i = 0; i < NpartitionsSelected; i++)
				if (strcasecmp(f[F_PARTITION],
					       partitionsSelected[i]) == 0)
					goto foundp;
			continue;	/* no match */
		}
	foundp: 
		if (params.opt_fdump) {
			doFdump(f, lc);
			continue;
		}
		/* Build suitable tables with all the data */
		if (strcmp(f[F_RECTYPE],"JOB_START")==0) {
			processJobStart(f);
		} else if (strcmp(f[F_RECTYPE],"JOB_STEP")==0) {
			processJobStep(f);
		} else if (strcmp(f[F_RECTYPE],"JOB_TERMINATED")==0) {
			processJobTerminated(f);
		} else {
			if (params.opt_verbose > 1)
				fprintf(stderr,
					"Invalid record at line %ld of"
					" input file\n",
					lc);
			if (params.opt_verbose > 2)
				_show_rec(f);
			inputError++;
		}
	}
	if (params.opt_verbose)
		fprintf(stderr,
			"Info: %ld job%s and %ld jobstep%s passed initial"
			" filters, %ld bad record%s\n",
			Njobs, (Njobs==1? "" : "s"),
			Njobsteps, (Njobsteps==1? "" : "s"),
			inputError, (inputError==1? "" : "s"));

	if (ferror(fd)) {
		perror(params.opt_filein);
		exit(1);
	} 
	fclose(fd);

	return;
} 

void parse_command_line(int argc, char **argv)
{
	extern int optind;
	int c, i, optionIndex = 0;
	char *end, *start, *acct_type;
	struct stat stat_buf;
	static struct option long_options[] = {
		{"all", 0,0, 'a'},
		{"brief", 0, 0, 'b'},
		{"duplicates", 0, &params.opt_dup, 1},
		{"dump", 0, 0, 'd'},
		{"expire", 1, 0, 'e'},
		{"fields", 1, 0, 'F'},
		{"file", 1, 0, 'f'},
		{"formatted_dump", 0, 0, 'O'},
		{"gid", 1, 0, 'g'},
		{"group", 1, 0, 'g'},
		{"help", 0, &params.opt_help, 1},
		{"help-fields", 0, &params.opt_help, 2},
		{"jobs", 1, 0, 'j'},
		{"jobstep", 1, 0, 'J'},
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
		{0, 0, 0, 0}
	};

	_init_params();

	if ((i=getuid()))	/* default to current user unless root*/
		params.opt_uid = i;

	opterr = 1;		/* Let getopt report problems to the user */

	while (1) {		/* now cycle through the command line */
		c = getopt_long(argc, argv, "abde:F:f:g:hj:J:lOPp:s:tUu:Vv",
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
			int		i;
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
					expire_time_match = MATCH_DAY;
					acc=0;
					break;
				case 'H':
				case 'h':
					params.opt_expire += 
						acc*SECONDS_IN_HOUR;
					expire_time_match = MATCH_HOUR;
					acc=0;
					break;
				case 'M':
				case 'm':
					params.opt_expire += 
						acc*SECONDS_IN_MINUTE;
					expire_time_match = MATCH_MINUTE;
					acc=0;
					break;
				default:
					params.opt_expire = -1;
					goto bad_timespec;
				} 
			}
			params.opt_expire += acc*SECONDS_IN_MINUTE;
			if ((expire_time_match == 0) || (acc != 0))
				/* If they say "1d2", we interpret that
				 * as 24 hours+2 minutes ago. */
				expire_time_match = MATCH_MINUTE;
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
			if (strspn(optarg, "0123456789, ") < strlen(optarg)) {
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

		case 'J':
			if (strspn(optarg, ".0123456789, ") < strlen(optarg)) {
				fprintf(stderr, "Invalid jobstep list: %s\n",
					optarg);
				exit(1);
			}
			params.opt_jobstep_list =
				xrealloc(params.opt_jobstep_list,
					 (params.opt_jobstep_list==NULL? 0 :
					  strlen(params.opt_jobstep_list)) +
					 strlen(optarg) + 1);
			strcat(params.opt_jobstep_list, optarg);
			strcat(params.opt_jobstep_list, ",");
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
			int	i;
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
		if (params.opt_job_list || params.opt_jobstep_list)
			/* They probably want the most recent job N if
			 * they requested specific jobs or steps. */
			params.opt_dup = 0;

	if (params.opt_verbose) {
		fprintf(stderr, "Options selected:\n"
			"\topt_dump=%d\n"
			"\topt_dup=%d\n"
			"\topt_expire=%s (%lu seconds)\n"
			"\topt_fdump=%d\n"
			"\topt_field_list=%s\n"
			"\topt_filein=%s\n"
			"\topt_header=%d\n"
			"\topt_help=%d\n"
			"\topt_job_list=%s\n"
			"\topt_jobstep_list=%s\n"
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
			params.opt_field_list,
			params.opt_filein,
			params.opt_header,
			params.opt_help,
			params.opt_job_list,
			params.opt_jobstep_list,
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
		while ((end = strstr(start, ","))) {
			*end = 0;;
			partitionsSelected[NpartitionsSelected] = start;
			NpartitionsSelected++;
			start = end + 1;
		}
		if (params.opt_verbose) {
			int i;
			fprintf(stderr, "Partitions requested:\n");
			for (i = 0; i < NpartitionsSelected; i++)
				fprintf(stderr, "\t: %s\n",
					partitionsSelected[i]);
		}
	}

	/* specific jobsteps requested? */
	if (params.opt_jobstep_list) {
		char *dot;

		start = params.opt_jobstep_list;
		while ((end = strstr(start, ","))) {
			*end = 0;;
			while (isspace(*start))
				start++;	/* discard whitespace */
			dot = strstr(start, ".");
			if (dot == NULL) {
				fprintf(stderr, "Invalid jobstep: %s\n",
					start);
				exit(1);
			}
			*dot++ = 0;
			jobstepsSelected[NjobstepsSelected].job = start;
			jobstepsSelected[NjobstepsSelected].step = dot;
			NjobstepsSelected++;
			start = end + 1;
		}
		if (params.opt_verbose) {
			int i;
			fprintf(stderr, "Job steps requested:\n");
			for (i = 0; i < NjobstepsSelected; i++)
				fprintf(stderr, "\t: %s.%s\n",
					jobstepsSelected[i].job,
					jobstepsSelected[i].step);
		}
	}

	/* specific jobs requested? */
	if (params.opt_job_list) { 
		start = params.opt_job_list;
		while ((end = strstr(start, ","))) {
			while (isspace(*start))
				start++;	/* discard whitespace */
			*end = 0;
			jobstepsSelected[NjobstepsSelected].job = start;
			jobstepsSelected[NjobstepsSelected].step = NULL;
			NjobstepsSelected++;
			start = end + 1;
		}
		if (params.opt_verbose) {
			int i;
			fprintf(stderr, "Jobs requested:\n");
			for (i = 0; i < NjobstepsSelected; i++)
				fprintf(stderr, "\t: %s\n",
					jobstepsSelected[i].job);
		}
	}

	/* specific states (completion status) requested? */
	if (params.opt_state_list) {
		start = params.opt_state_list;
		while ((end = strstr(start, ","))) {
			*end = 0;
			statesSelected[NstatesSelected] = start;
			NstatesSelected++;
			start = end + 1;
		}
		if (params.opt_verbose) {
			int i;
			fprintf(stderr, "States requested:\n");
			for (i = 0; i < NstatesSelected; i++)
				fprintf(stderr, "\t: %s\n", statesSelected[i]);
		}
	}

	/* select the output fields */
	if (params.opt_field_list==NULL) {
		if (params.opt_dump || params.opt_expire)
			goto endopt;
		params.opt_field_list = xmalloc(sizeof(DEFAULT_FIELDS)+1);
		strcpy(params.opt_field_list,DEFAULT_FIELDS); 
		strcat(params.opt_field_list, ",");
	}
	start = params.opt_field_list;
	while ((end = strstr(start, ","))) {
		*end = 0;
		for (i = 0; fields[i].name; i++) {
			if (strcasecmp(fields[i].name, start) == 0)
				goto foundfield;
		}
		fprintf(stderr,
			"Invalid field requested: \"%s\"\n",
			start);
		exit(1);
	foundfield:
		printFields[NprintFields++] = i;
		start = end + 1;
	}
	if (params.opt_verbose) {
		fprintf(stderr, "%d field%s selected:\n",
			NprintFields,
			(NprintFields==1? "" : "s"));
		for (i = 0; i < NprintFields; i++)
			fprintf(stderr,
				"\t%s\n",
				fields[printFields[i]].name);
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

void doDump(void)
{
	long	j, js;
	int rc;
	for (j=0; j<Njobs; j++) {
		if (params.opt_dup==0)
			if (jobs[j].jobnum_superseded) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Note: Skipping older"
						" job %ld dated %s\n",
						jobs[j].job,
						jobs[j].submitted);
				continue;
			}
		if (params.opt_uid>=0)
			if (jobs[j].uid != params.opt_uid)
				continue;
		/* JOB_START */
		if (params.opt_jobstep_list == NULL) {
			if ((jobs[j].job_start_seen==0) &&
			    (jobs[j].job_step_seen) ) {
				/* If we only saw JOB_TERMINATED, the
				 * job was probably canceled. */ 
				fprintf(stderr,
					"Error: No JOB_START record for "
					"job %ld\n",
					jobs[j].job);
				if (rc<ERROR)
					rc = ERROR;
			}
			_dump_header(j);
			printf("JOB_START %d %d %d %d %s %d %d %ld %s\n", 
			       1,
			       16,
			       jobs[j].uid,
			       jobs[j].gid,
			       jobs[j].jobname,
			       jobs[j].batch,
			       jobs[j].priority,
			       jobs[j].ncpus,
			       jobs[j].nodes
				);
		}
		/* JOB_STEP */
		for (js=jobs[j].first_jobstep; js>=0; js=jobsteps[js].next) {
			if ((strcasecmp(jobsteps[js].cstatus, "R")==0) &&
			    jobs[j].job_terminated_seen) {
				strcpy(jobsteps[js].cstatus,"f");
				jobsteps[js].error=1;
			}
			_dump_header(j);
			printf("JOB_STEP %d %d %ld %s ",
			       1,
			       38,
			       jobsteps[js].jobstep,
			       jobsteps[js].stepname); 
			printf("%s %s %d %ld %ld %ld ",
			       jobsteps[js].finished,
			       jobsteps[js].cstatus,
			       jobsteps[js].error,
			       jobsteps[js].nprocs,
			       jobsteps[js].ncpus,
			       jobsteps[js].elapsed);
			printf("%ld %ld %ld %ld %ld %ld ",
			       jobsteps[js].tot_cpu_sec,
			       jobsteps[js].tot_cpu_usec,
			       jobsteps[js].tot_user_sec,
			       jobsteps[js].tot_user_usec,
			       jobsteps[js].tot_sys_sec,
			       jobsteps[js].tot_sys_usec);
			printf("%ld %ld %ld %ld %ld %ld %ld %ld %ld "
			       "%ld %ld %ld %ld %ld %ld %ld\n",
			       jobsteps[js].rss,
			       jobsteps[js].ixrss,
			       jobsteps[js].idrss,
			       jobsteps[js].isrss,
			       jobsteps[js].minflt,
			       jobsteps[js].majflt,
			       jobsteps[js].nswap,
			       jobsteps[js].inblocks,
			       jobsteps[js].oublocks,
			       jobsteps[js].msgsnd,
			       jobsteps[js].msgrcv,
			       jobsteps[js].nsignals,
			       jobsteps[js].nvcsw,
			       jobsteps[js].nivcsw,
			       jobsteps[js].vsize,
			       jobsteps[js].psize);
		}
		/* JOB_TERMINATED */
		if (params.opt_jobstep_list == NULL) {
			_dump_header(j);
			printf("JOB_TERMINATED %d %d %ld ",
			       1,
			       38,
			       jobs[j].elapsed);
			printf("%s %s %d %ld %ld %ld ",
			       jobs[j].finished,
			       jobs[j].cstatus,
			       jobs[j].error,
			       jobs[j].nprocs,
			       jobs[j].ncpus,
			       jobs[j].elapsed);
			printf("%ld %ld %ld %ld %ld %ld ",
			       jobs[j].tot_cpu_sec,
			       jobs[j].tot_cpu_usec,
			       jobs[j].tot_user_sec,
			       jobs[j].tot_user_usec,
			       jobs[j].tot_sys_sec,
			       jobs[j].tot_sys_usec);
			printf("%ld %ld %ld %ld %ld %ld ",
			       jobs[j].rss,
			       jobs[j].ixrss,
			       jobs[j].idrss,
			       jobs[j].isrss,
			       jobs[j].minflt,
			       jobs[j].majflt);
			printf("%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", 
			       jobs[j].nswap,
			       jobs[j].inblocks,
			       jobs[j].oublocks,
			       jobs[j].msgsnd,
			       jobs[j].msgrcv,
			       jobs[j].nsignals,
			       jobs[j].nvcsw,
			       jobs[j].nivcsw,
			       jobs[j].vsize,
			       jobs[j].psize);
		}
	}
}

/* doExpire() -- purge expired data from the accounting log file
 *
 * What we're doing:
 *  1. Open logfile.orig
 *  2. stat logfile.orig
 *     - confirm that it's not a sym link
 *     - capture the ownership and permissions
 *  3. scan logfile.orig for JOB_TERMINATED records with F_FINISHED dates
 *     that precede the specified expiration date. Build exp_table as
 *     a list of expired jobs.
 *  4. Open logfile.expired for append
 *  5. Create logfile.new as ".new.<logfile>" (output with line buffering)
 *  6. Re-scan logfile.orig, writing
 *     - Expired job records to logfile.expired
 *     - Other job records to logfile.new
 *  7. Rename logfile.orig as ".old.<logfile>"
 *  8. Rename logfile.new as "<logfile>"
 *  9. Execute "scontrol reconfigure" which will cause slurmctld to
 *     start writing to logfile.new
 * 10. fseek(ftell(logfile.orig)) to clear EOF
 * 11. Copy any new records from logfile.orig to logfile.new
 * 12. Close logfile.expired, logfile.new
 * 13. Unlink .old.<logfile>
 */

void doExpire(void)
{
#define EXP_STG_LENGTH 12
#define TERM_REC_FIELDS 11
	char	*buf,
		exp_stg[EXP_STG_LENGTH+1], /* YYYYMMDDhhmm */
		*expired_logfile_name,
		*f[TERM_REC_FIELDS],
		*fptr,
		*new_logfile_name,
		*old_logfile_name;
	int	bufsize=MINBUFSIZE,
		file_err=0,
		new_file,
		i;
	expired_table_t *exp_table;
	int	exp_table_allocated = 0,
		exp_table_entries = 0;
	pid_t	pid;
	struct	stat statbuf;
	mode_t	prot = 0600;
	uid_t	uid;
	gid_t	gid;
	FILE	*expired_logfile,
		*new_logfile;
	FILE *fd = NULL;
	int lc=0;
	/* Figure out our expiration date */
	{
		struct tm	*ts;	/* timestamp decoder */
		time_t		expiry;
		expiry = time(NULL)-params.opt_expire;
		ts = gmtime(&expiry);
		sprintf(exp_stg, "%04d%02d%02d%02d%02d",
			(ts->tm_year+1900), (ts->tm_mon+1), ts->tm_mday,
			ts->tm_hour, ts->tm_min);
		if (params.opt_verbose)
			fprintf(stderr, "Purging jobs completed prior to %s\n",
				exp_stg);
	}
	/* Open the current or specified logfile, or quit */
	fd = _open_log_file();
	if (stat(params.opt_filein, &statbuf)) {
		perror("stat'ing logfile");
		exit(1);
	}
	if ((statbuf.st_mode & S_IFLNK) == S_IFLNK) {
		fprintf(stderr, "%s is a symbolic link; --expire requires "
			"a hard-linked file name\n", params.opt_filein);
		exit(1);
	}
	if ( (statbuf.st_mode & S_IFREG)==0 ) {
		fprintf(stderr, "%s is not a regular file; --expire "
			"only works on accounting log files\n",
			params.opt_filein);
		exit(1);
	}
	prot = statbuf.st_mode & 0777;
	gid  = statbuf.st_gid;
	uid  = statbuf.st_uid;
	old_logfile_name = _prefix_filename(params.opt_filein, ".old.");
	if (stat(old_logfile_name, &statbuf)) {
		if (errno != ENOENT) {
			fprintf(stderr,"Error checking for %s: ",
				old_logfile_name);
			perror("");
			exit(1);
		}
	} else {
		fprintf(stderr, "Warning! %s exists -- please remove "
			"or rename it before proceeding",
			old_logfile_name);
		exit(1);
	}

	/* create our initial buffer */
	buf = xmalloc(bufsize);
	exp_table = xmalloc( sizeof(expired_table_t)
				* (exp_table_allocated=10000));
	while (fgets(buf, BUFFER_SIZE, fd)) {
		lc++;
		fptr = buf;	/* break the record into NULL-
				   terminated strings */
		for (i = 0; i < TERM_REC_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break; 
			else
				*fptr++ = 0;
		}
		if (i < TERM_REC_FIELDS)
			continue;	/* Odd, but complain some other time */
		if (strcmp(f[F_RECVERSION], "1")) {
			if (params.opt_purge)	/* catch it again later */
				continue;
			fprintf(stderr,
				"Invalid record version \"%s\" at input "
				"line %ld.\n"
				"(Use --expire --purge to force the "
				"removal of this record.)\n"
				"It is unsafe to complete this "
				"operation -- terminating.\n",
				f[F_RECVERSION], lc);
			exit(1);
		}
		if (strcmp(f[F_RECTYPE], "JOB_TERMINATED")==0) {
			if (strncmp(exp_stg, f[F_FINISHED],
				    expire_time_match)<0) 
				continue;
			if (NpartitionsSelected) {
				for (i = 0; i < NpartitionsSelected; i++)
					if (strcasecmp(f[F_PARTITION],
						       partitionsSelected[i]) 
					    == 0)
						goto pmatch;
				continue;	/* no match */
			}
		pmatch: 
			if (exp_table_allocated <= exp_table_entries)
				exp_table = xrealloc(exp_table,
						     sizeof(expired_table_t) *
						     (exp_table_allocated
						      +=10000));
			exp_table[exp_table_entries].job =
				strtol(f[F_JOB], NULL, 10);
			strncpy(exp_table[exp_table_entries].submitted,
				f[F_SUBMITTED], TIMESTAMP_LENGTH);
			if (params.opt_verbose > 2)
				fprintf(stderr, "Selected: %8ld %s\n",
					exp_table[exp_table_entries].job,
					exp_table[exp_table_entries].
					submitted);
			exp_table_entries++;
		}
	}
	if (exp_table_entries == 0) {
		printf("No job records were purged.\n");
		exit(0);
	}
	expired_logfile_name =
		xmalloc(strlen(params.opt_filein)+sizeof(".expired"));
	sprintf(expired_logfile_name, "%s.expired", params.opt_filein);
	new_file = stat(expired_logfile_name, &statbuf);
	if ((expired_logfile = fopen(expired_logfile_name, "a"))==NULL) {
		fprintf(stderr, "Error while opening %s", 
			expired_logfile_name);
		perror("");
		exit(1);
	}
	if (new_file) {  /* By default, the expired file looks like the log */
		chmod(expired_logfile_name, prot);
		chown(expired_logfile_name, uid, gid);
	}
	new_logfile_name = _prefix_filename(params.opt_filein, ".new.");
	if ((new_logfile = fopen(new_logfile_name, "w"))==NULL) {
		fprintf(stderr, "Error while opening %s",
			new_logfile_name);
		perror("");
		exit(1);
	}
	chmod(new_logfile_name, prot);     /* preserve file protection */
	chown(new_logfile_name, uid, gid); /* and ownership */
	/* Use line buffering to allow us to safely write
	 * to the log file at the same time as slurmctld. */ 
	if (setvbuf(new_logfile, NULL, _IOLBF, 0)) {
		perror("setvbuf()");
		exit(1);
	}

	qsort(exp_table, exp_table_entries, 
	      sizeof(expired_table_t), _cmp_jrec);
	if (params.opt_verbose > 2) {
		fprintf(stderr, "--- contents of exp_table ---");
		for (i=0; i<exp_table_entries; i++) {
			if ((i%5)==0)
				fprintf(stderr, "\n");
			else
				fprintf(stderr, "\t");
			fprintf(stderr, "%ld", exp_table[i].job);
		}
		fprintf(stderr, "\n---- end of exp_table ---\n");
	}
	rewind(fd);
	lc=0;
	while (fgets(buf, BUFFER_SIZE, fd)) {
		expired_table_t tmp;

		fptr = buf;
		lc++;

		for (i=0; i<F_NUMFIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break; 
			fptr++;
		}
		if (i >= F_NUMFIELDS) 	/* Enough data for these checks? */
			if (strncmp(f[F_RECVERSION], "1 ", 2)==0 ) 
				if ((strncmp(f[F_RECTYPE],
					     "JOB_START ", 10)==0)
				    || (strncmp(f[F_RECTYPE],
						"JOB_STEP ", 9)==0)
				    || (strncmp(f[F_RECTYPE],
						"JOB_TERMINATED ", 15)==0))
					goto goodrec;

		/* Ugh; invalid record */
		fprintf(stderr, "Invalid record at input line %ld", lc);
		if (params.opt_purge) {
			fprintf(stderr, "; purging it.\n");
			continue;
		}
		fprintf(stderr,
			".\n(Use --expire --purge to force the "
			"removal of this record.)\n"
			"It is unsafe to complete this "
			"operation -- terminating.\n");
		exit(1);

	goodrec:
		tmp.job = strtol(f[F_JOB], NULL, 10);
		strncpy(tmp.submitted, f[F_SUBMITTED], TIMESTAMP_LENGTH);
		tmp.submitted[TIMESTAMP_LENGTH-1] = 0;
		if (bsearch(&tmp, exp_table, exp_table_entries,
			    sizeof(expired_table_t), _cmp_jrec)) {
			if (fputs(buf, expired_logfile)<0) {
				perror("writing expired_logfile");
				exit(1);
			}
		} else {
			if (fputs(buf, new_logfile)<0) {
				perror("writing new_logfile");
				exit(1);
			}
		}
	}
	fclose(expired_logfile);
	if (rename(params.opt_filein, old_logfile_name)) {
		perror("renaming logfile to .old.");
		exit(1);
	}
	if (rename(new_logfile_name, params.opt_filein)) {
		perror("renaming new logfile");
		/* undo it? */
		if (rename(old_logfile_name, params.opt_filein)==0) 
			fprintf(stderr, "Please correct the problem "
				"and try again");
		else
			fprintf(stderr, "SEVERE ERROR: Current accounting "
				"log may have been renamed %s;\n"
				"please rename it to \"%s\" if necessary, "
			        "and try again\n",
				old_logfile_name, params.opt_filein);
		exit(1);
	}
	fflush(new_logfile);	/* Flush the buffers before forking */
	fflush(fd);
	if ((pid=fork())) {
		if (waitpid(pid, &i, 0) < 1) {
			perror("forking scontrol");
			exit(1);
		}
	} else {
		fclose(new_logfile);
		fclose(fd);
		execlp("scontrol", "scontrol", "reconfigure", NULL);
		perror("attempting to run \"scontrol reconfigure\"");
		exit(1);
	}
	if (WEXITSTATUS(i)) {
		file_err = 1;
		fprintf(stderr, "Error: Attempt to execute \"scontrol "
			"reconfigure\" failed. If SLURM is\n"
			"running, please rename the file \"%s\"\n"
			" to \"%s\" and try again.\n",
			old_logfile_name, params.opt_filein);
	}
	if (fseek(fd, 0, SEEK_CUR)) {	/* clear EOF */
		perror("looking for late-arriving records");
		exit(1);
	}
	while (fgets(buf, BUFFER_SIZE, fd)) {
		if (fputs(buf, new_logfile)<0) {
			perror("writing final records");
			exit(1);
		}
	}
	fclose(new_logfile);
	fclose(fd);
	if (file_err==0)
		unlink(old_logfile_name);
	printf("%d jobs expired.\n", exp_table_entries);
	exit(0);
}

void doFdump(char* fields[], int lc)
{
	int	i=0,
		numfields;
	struct {
		char	*start;
		char	*step;
		char	*term;
	} fnames[] = {
		{ "job",	"job",	"job"	},		/*  0 */
		{ "partition",	"partition",	"partition" },	/*  1 */
		{ "submitted",	"submitted",	"submitted" },	/*  2 */
		{ "starttime",	"starttime",	"starttime" },	/*  3 */
		{ "uid.gid",	"uid.gid",	"uid.gid" },	/*  4 */
		{ "reserved-1",	"reserved-1",	"reserved-1" },	/*  5 */
		{ "recordType", "recordType",	"recordType" },	/*  6 */
		{ "recordVers",	"recordVers",	"recordVers" },	/*  7 */
		{ "numFields",	"numFields",	"numFields" },	/*  8 */
		{ "uid",	"jobStep",	"totElapsed" },	/*  9 */
		{ "gid",	"finished",	"finished" },	/* 10 */
		{"jobName",	"cStatus",	"cStatus" },	/* 11 */ 
		{"batchFlag",	"error",	"error" },	/* 12 */
		{"priority",	"nprocs",	"nprocs" },	/* 13 */
		{"ncpus",	"ncpus",	"ncpus" },	/* 14 */
		{"nodeList",	"elapsed",	"elapsed" },	/* 15 */
		{"????",	"cpu_sec",	"cpu_sec" },	/* 16 */
		{"????",	"cpu_usec",	"cpu_usec" },	/* 17 */
		{"????",	"user_sec",	"user_sec" },	/* 18 */
		{"????",	"user_usec",	"user_usec" },	/* 19 */
		{"????",	"sys_sec",	"sys_sec" },	/* 20 */
		{"????",	"sys_usec",	"sys_usec" },	/* 21 */
		{"????",	"rss",		"rss" },	/* 22 */
		{"????",	"ixrss",	"ixrss" },	/* 23 */
		{"????",	"idrss",	"idrss" },	/* 24 */
		{"????",	"isrss",	"isrss" },	/* 25 */
		{"????",	"minflt",	"minflt" },	/* 26 */
		{"????",	"majflt",	"majflt" },	/* 27 */
		{"????",	"nswap",	"nswap" },	/* 28 */
		{"????",	"inblocks",	"inblocks" },	/* 29 */
		{"????",	"oublocks",	"oublocks" },	/* 30 */
		{"????",	"msgsnd",	"msgsnd" },	/* 31 */
		{"????",	"msgrcv",	"msgrcv" },	/* 32 */
		{"????",	"nsignals",	"nsignals" },	/* 33 */
		{"????",	"nvcsw",	"nvcsw" },	/* 34 */
		{"????",	"nivcsw",	"nivcsw" },	/* 35 */
		{"????",	"vsize",	"vsize" },	/* 36 */
		{"????",	"psize",	"psize" }	/* 37 */
	};
	numfields = atoi(fields[F_NUMFIELDS]);
	printf("\n-------Line %ld ---------------\n", lc);
	if (strcmp(fields[F_RECTYPE], "JOB_START")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].start, fields[i]);
	else if (strcmp(fields[F_RECTYPE], "JOB_STEP")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].step, fields[i]);
	else if (strcmp(fields[F_RECTYPE], "JOB_TERMINATED")==0)
		for (i=0; fields[i] && (i<numfields); i++)
			printf("%12s: %s\n", fnames[i].term, fields[i]);
	else 	/* _get_data() already told them of unknown record type */
		printf("      Field[%02d]: %s\n", i, fields[i]); 
	for ( ; fields[i]; i++)	/* Any extra data? */
		printf("extra field[%02d]: %s\n", i, fields[i]);
}

void doHelp(void)
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

/* doList() -- List the assembled data
 *
 * In:	Nothing explicit.
 * Out:	void.
 *
 * At this point, we have already selected the desired data,
 * so we just need to print it for the user.
 */
void doList(void)
{
	int	f, pf;
	long	j, js;
	int	doJobs=1,
		doJobsteps=1,
		i;
	int rc = 0;
	if (params.opt_total)
		doJobsteps = 0;
	else if (params.opt_jobstep_list)
		doJobs = 0;
	for (j=0; j<Njobs; j++) {
		if (params.opt_dup==0)
			if (jobs[j].jobnum_superseded) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Note: Skipping older"
						" job %ld dated %s\n",
						jobs[j].job,
						jobs[j].submitted);
				continue;
			}
		if ((jobs[j].job_start_seen==0) && jobs[j].job_step_seen) {
			/* If we only saw JOB_TERMINATED, the job was
			 * probably canceled. */
			fprintf(stderr,
				"Error: No JOB_START record for " "job %ld\n",
				jobs[j].job);
			if (rc<ERROR)
				rc = ERROR;
		}
		if (params.opt_verbose > 1) {
			if (jobs[j].job_start_seen==0)
				fprintf(stderr,
					"Note: No JOB_START record for "
					"job %ld\n",
					jobs[j].job);
			if (jobs[j].job_step_seen==0)
				fprintf(stderr,
					"Note: No JOB_STEP record for "
					"job %ld\n",
					jobs[j].job);
			if (jobs[j].job_terminated_seen==0)
				fprintf(stderr,
					"Note: No JOB_TERMINATED record for "
					"job %ld\n",
					jobs[j].job);
		}
		if (params.opt_uid >= 0 && (jobs[j].uid != params.opt_uid))
			continue;
		if (params.opt_gid >= 0 && (jobs[j].gid != params.opt_gid))
			continue;
		if (doJobs) {
			if (params.opt_state_list) {
				for (i=0; i<NstatesSelected; i++) {
					if (strcasecmp(jobs[j].cstatus,
						       statesSelected[i])==0)
						goto jstate;
				}
				continue;
			}
		jstate:
			for (f=0; f<NprintFields; f++) {
				pf = printFields[f];
				if (f)
					printf(" ");
				(fields[pf].print_routine)(JOB, j);
			}
			printf("\n");
		}
		if (doJobsteps) {
			for (js=jobs[j].first_jobstep;
			     js>=0;
			     js=jobsteps[js].next) {
				if ((strcasecmp(jobsteps[js].cstatus,"R")==0) 
				    && jobs[j].job_terminated_seen) {
					strcpy(jobsteps[js].cstatus,"f");
					jobsteps[js].error=1;
				}
				if (params.opt_state_list) {
					for (i=0; i<NstatesSelected; i++) {
						if (strcasecmp(
							    jobsteps[js].
							    cstatus,
							    statesSelected[i])
						    == 0)
							goto js_state;
					}
					continue;
				}
			js_state:
				for(f=0; f<NprintFields; f++) {
					pf = printFields[f];
					if (f)
						printf(" ");
					(fields[pf].print_routine)(
						JOBSTEP, js);
				}
				printf("\n");
			} 
		}
	}
}
