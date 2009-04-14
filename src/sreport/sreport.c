/*****************************************************************************\
 *  sreport.c - report generating tool for slurm accounting.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/sreport/sreport.h"
#include "src/sreport/assoc_reports.h"
#include "src/sreport/cluster_reports.h"
#include "src/sreport/job_reports.h"
#include "src/sreport/user_reports.h"
#include "src/common/xsignal.h"

#define OPT_LONG_HIDE   0x102
#define BUFFER_SIZE 4096

char *command_name;
int exit_code;		/* sreport's exit code, =1 on any error at any time */
int exit_flag;		/* program to terminate if =1 */
int input_words;	/* number of words of input permitted */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int all_clusters_flag = 0;
sreport_time_format_t time_format = SREPORT_TIME_MINS;
char *time_format_string = "Minutes";
void *db_conn = NULL;
uint32_t my_uid = 0;
sreport_sort_t sort_flag = SREPORT_SORT_TIME;

static void	_job_rep (int argc, char *argv[]);
static void	_user_rep (int argc, char *argv[]);
static void	_cluster_rep (int argc, char *argv[]);
static void	_assoc_rep (int argc, char *argv[]);
static int	_get_command (int *argc, char *argv[]);
static void     _print_version( void );
static int	_process_command (int argc, char *argv[]);
static int      _set_time_format(char *format);
static int      _set_sort(char *format);
static void	_usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS, i, opt_char, input_field_count;
	char **input_fields;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	char *temp = NULL;
	int option_index;
	static struct option long_options[] = {
		{"all_clusters", 0, 0, 'a'},
		{"help",     0, 0, 'h'},
		{"immediate",0, 0, 'i'},
		{"no_header", 0, 0, 'n'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"quiet",    0, 0, 'q'},
		{"sort",    0, 0, 's'},
		{"usage",    0, 0, 'h'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{NULL,       0, 0, 0}
	};

	command_name      = argv[0];
	exit_code         = 0;
	exit_flag         = 0;
	input_field_count = 0;
	quiet_flag        = 0;
	log_init("sreport", opts, SYSLOG_FACILITY_DAEMON, NULL);

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_accounting_storage_type();
	if(strcasecmp(temp, "accounting_storage/slurmdbd")
	   && strcasecmp(temp, "accounting_storage/mysql")) {
		fprintf (stderr, "You are not running a supported "
			 "accounting_storage plugin\n(%s).\n"
			 "Only 'accounting_storage/slurmdbd' "
			 "and 'accounting_storage/mysql' are supported.\n",
			temp);
		xfree(temp);
		exit(1);
	}
	xfree(temp);

	while((opt_char = getopt_long(argc, argv, "ahnpPqs:t:vV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"sreport --help\" "
				"for more information\n");
			exit(1);
			break;
		case (int)'h':
			_usage ();
			exit(exit_code);
			break;
		case (int)'a':
			all_clusters_flag = 1;
			break;
		case (int)'n':
			print_fields_have_header = 0;
			break;
		case (int)'p':
			print_fields_parsable_print = 
			PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case (int)'P':
			print_fields_parsable_print =
			PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case (int)'q':
			quiet_flag = 1;
			break;
		case (int)'s':
			_set_sort(optarg);
			break;
		case (int)'t':
			_set_time_format(optarg);
			break;
		case (int)'v':
			quiet_flag = -1;
			break;
		case (int)'V':
			_print_version();
			exit(exit_code);
			break;
		default:
			exit_code = 1;
			fprintf(stderr, "getopt error, returned %c\n", 
				opt_char);
			exit(exit_code);
		}
	}

	if (argc > MAX_INPUT_FIELDS)	/* bogus input, but continue anyway */
		input_words = argc;
	else
		input_words = 128;
	input_fields = (char **) xmalloc (sizeof (char *) * input_words);
	if (optind < argc) {
		for (i = optind; i < argc; i++) {
			input_fields[input_field_count++] = argv[i];
		}	
	}

	db_conn = acct_storage_g_get_connection(false, 0, false);
	my_uid = getuid();

	if (input_field_count)
		exit_flag = 1;
	else
		error_code = _get_command (&input_field_count, input_fields);
	while (error_code == SLURM_SUCCESS) {
		error_code = _process_command (input_field_count, 
					       input_fields);
		if (error_code || exit_flag)
			break;
		error_code = _get_command (&input_field_count, input_fields);
	}

	acct_storage_g_close_connection(&db_conn);
	slurm_acct_storage_fini();
	exit(exit_code);
}

#if !HAVE_READLINE
/*
 * Alternative to readline if readline is not available
 */
static char *
getline(const char *prompt)
{
	char buf[4096];
	char *line;
	int len;
	printf("%s", prompt);

	fgets(buf, 4096, stdin);
	len = strlen(buf);
	if ((len > 0) && (buf[len-1] == '\n'))
		buf[len-1] = '\0';
	else
		len++;
	line = malloc (len * sizeof(char));
	return strncpy(line, buf, len);
}
#endif

/* 
 * _job_rep - Reports having to do with jobs 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _job_rep (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;
	int command_len = strlen(argv[0]);

	/* For backwards compatibility we just look at the 1st char
	 * by default since Sizes was the original name */
	if (!strncasecmp (argv[0], "SizesByAccount", MAX(command_len, 1))) {
		error_code = job_sizes_grouped_by_top_acct(
			(argc - 1), &argv[1]);
	} else if (!strncasecmp (argv[0], 
				 "SizesByWcKey", MAX(command_len, 8))) {
		error_code = job_sizes_grouped_by_wckey(
			(argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "Not valid report %s\n", argv[0]);
		fprintf(stderr, "Valid job reports are, ");
		fprintf(stderr, "\"SizesByAccount, and SizesByWckey\"\n");
	}
	
	if (error_code) {
		exit_code = 1;
	}
}

/* 
 * _user_rep - Reports having to do with jobs 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _user_rep (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	if (strncasecmp (argv[0], "Top", 1) == 0) {
		error_code = user_top((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "Not valid report %s\n", argv[0]);
		fprintf(stderr, "Valid user reports are, ");
		fprintf(stderr, "\"Top\"\n");
	}	
	
	if (error_code) {
		exit_code = 1;
	}
}

/* 
 * _cluster_rep - Reports having to do with jobs 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _cluster_rep (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	if (strncasecmp (argv[0], "AccountUtilizationByUser", 1) == 0) {
		error_code = cluster_account_by_user((argc - 1), &argv[1]);
	} else if ((strncasecmp (argv[0], "UserUtilizationByAccount", 18) == 0)
		   || (strncasecmp (argv[0], "UA", 2) == 0)) {
		error_code = cluster_user_by_account((argc - 1), &argv[1]);
	} else if ((strncasecmp (argv[0], "UserUtilizationByWckey", 18) == 0)
		   || (strncasecmp (argv[0], "UW", 2) == 0)) {
		error_code = cluster_user_by_wckey((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Utilization", 2) == 0) {
		error_code = cluster_utilization((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "WCKeyUtilizationByUser", 1) == 0) {
		error_code = cluster_wckey_by_user((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "Not valid report %s\n", argv[0]);
		fprintf(stderr, "Valid cluster reports are, ");
		fprintf(stderr, "\"AccountUtilizationByUser\", "
			"\"UserUtilizationByAccount\", "
			"\"UserUtilizationByWckey\", \"Utilization\", "
			"and \"WCKeyUtilizationByUser\"\n");
	}
	
	if (error_code) {
		exit_code = 1;
	}
}

/* 
 * _assoc_rep - Reports having to do with jobs 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _assoc_rep (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	if (error_code) {
		exit_code = 1;
	}
}

/*
 * _get_command - get a command from the user
 * OUT argc - location to store count of arguments
 * OUT argv - location to store the argument list
 */
static int 
_get_command (int *argc, char **argv) 
{
	char *in_line;
	static char *last_in_line = NULL;
	int i, in_line_size;
	static int last_in_line_size = 0;

	*argc = 0;

#if HAVE_READLINE
	in_line = readline ("sreport: ");
#else
	in_line = getline("sreport: ");
#endif
	if (in_line == NULL)
		return 0;
	else if (strncmp (in_line, "#", 1) == 0) {
		free (in_line);
		return 0;
	} else if (strcmp (in_line, "!!") == 0) {
		free (in_line);
		in_line = last_in_line;
		in_line_size = last_in_line_size;
	} else {
		if (last_in_line)
			free (last_in_line);
		last_in_line = in_line;
		last_in_line_size = in_line_size = strlen (in_line);
	}

#if HAVE_READLINE
	add_history(in_line);
#endif

	/* break in_line into tokens */
	for (i = 0; i < in_line_size; i++) {
		bool double_quote = false, single_quote = false;
		if (in_line[i] == '\0')
			break;
		if (isspace ((int) in_line[i]))
			continue;
		if (((*argc) + 1) > MAX_INPUT_FIELDS) {	/* bogus input line */
			exit_code = 1;
			fprintf (stderr, 
				 "%s: can not process over %d words\n",
				 command_name, input_words);
			return E2BIG;
		}		
		argv[(*argc)++] = &in_line[i];
		for (i++; i < in_line_size; i++) {
			if (in_line[i] == '\042') {
				double_quote = !double_quote;
				continue;
			}
			if (in_line[i] == '\047') {
				single_quote = !single_quote;
				continue;
			}
			if (in_line[i] == '\0')
				break;
			if (double_quote || single_quote)
				continue;
			if (isspace ((int) in_line[i])) {
				in_line[i] = '\0';
				break;
			}
		}		
	}
	return 0;		
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
	if (quiet_flag == -1) {
		long version = slurm_api_version();
		printf("slurm_api_version: %ld, %ld.%ld.%ld\n", version,
			SLURM_VERSION_MAJOR(version), 
			SLURM_VERSION_MINOR(version),
			SLURM_VERSION_MICRO(version));
	}
}

/*
 * _process_command - process the user's command
 * IN argc - count of arguments
 * IN argv - the arguments
 * RET 0 or errno (only for errors fatal to sreport)
 */
static int
_process_command (int argc, char *argv[]) 
{
	int command_len = 0;

	if (argc < 1) {
		exit_code = 1;
		if (quiet_flag == -1)
			fprintf(stderr, "no input");
		return 0;
	}
	
	command_len = strlen(argv[0]);

	if ((strncasecmp (argv[0], "association", MAX(command_len, 1)) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else 
			_assoc_rep((argc - 1), &argv[1]);
	} else if ((strncasecmp (argv[0], "cluster",
				 MAX(command_len, 2)) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else 
			_cluster_rep((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "help", MAX(command_len, 2)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_usage ();
	} else if ((strncasecmp (argv[0], "job", MAX(command_len, 1)) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else 
			_job_rep((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "quiet", MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		quiet_flag = 1;
	} else if ((strncasecmp (argv[0], "exit", MAX(command_len, 1)) == 0) ||
		   (strncasecmp (argv[0], "\\q", MAX(command_len, 2)) == 0) ||
		   (strncasecmp (argv[0], "quit", MAX(command_len, 4)) == 0)) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		exit_flag = 1;
	} else if (strncasecmp (argv[0], "sort", MAX(command_len, 1)) == 0) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too few arguments for keyword:%s\n",
				 argv[0]);
		} else		
			_set_sort(argv[1]);
	} else if (strncasecmp (argv[0], "time", MAX(command_len, 1)) == 0) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too few arguments for keyword:%s\n",
				 argv[0]);
		} else		
			_set_time_format(argv[1]);
	} else if (strncasecmp (argv[0], "verbose", MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		quiet_flag = -1;
	} else if (strncasecmp (argv[0], "version", MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		_print_version();
	} else if ((strncasecmp (argv[0], "user", MAX(command_len, 1)) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else 
			_user_rep((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf (stderr, "invalid keyword: %s\n", argv[0]);
	}
		
	return 0;
}

static int _set_time_format(char *format)
{
	int command_len = strlen(format);

	if (strncasecmp (format, "SecPer", MAX(command_len, 6)) == 0) {
		time_format = SREPORT_TIME_SECS_PER;
		time_format_string = "Seconds/Percentage of Total";
	} else if (strncasecmp (format, "MinPer", MAX(command_len, 6)) == 0) {
		time_format = SREPORT_TIME_MINS_PER;
		time_format_string = "Minutes/Percentage of Total";
	} else if (strncasecmp (format, "HourPer", MAX(command_len, 6)) == 0) {
		time_format = SREPORT_TIME_HOURS_PER;
		time_format_string = "Hours/Percentage of Total";
	} else if (strncasecmp (format, "Seconds", MAX(command_len, 1)) == 0) {
		time_format = SREPORT_TIME_SECS;
		time_format_string = "Seconds";
	} else if (strncasecmp (format, "Minutes", MAX(command_len, 1)) == 0) {
		time_format = SREPORT_TIME_MINS;
		time_format_string = "Minutes";
	} else if (strncasecmp (format, "Hours", MAX(command_len, 1)) == 0) {
		time_format = SREPORT_TIME_HOURS;
		time_format_string = "Hours";
	} else if (strncasecmp (format, "Percent", MAX(command_len, 1)) == 0) {
		time_format = SREPORT_TIME_PERCENT;
		time_format_string = "Percentage of Total";
	} else {
		fprintf (stderr, "unknown time format %s", format);	
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _set_sort(char *format)
{
	int command_len = strlen(format);

	if (strncasecmp (format, "Name", MAX(command_len, 1)) == 0) {
		sort_flag = SREPORT_SORT_NAME;
	} else if (strncasecmp (format, "Time", MAX(command_len, 6)) == 0) {
		sort_flag = SREPORT_SORT_TIME;
	} else {
		fprintf (stderr, "unknown timesort format %s", format);	
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


/* _usage - show the valid sreport commands */
void _usage () {
	printf ("\
sreport [<OPTION>] [<COMMAND>]                                             \n\
    Valid <OPTION> values are:                                             \n\
     -a or --all_clusters: Use all clusters instead of current             \n\
     -h or --help: equivalent to \"help\" command                          \n\
     -n or --no_header: equivalent to \"no_header\" command                \n\
     -p or --parsable: output will be '|' delimited with a '|' at the end  \n\
     -P or --parsable2: output will be '|' delimited without a '|' at the end\n\
     -q or --quiet: equivalent to \"quiet\" command                        \n\
     -t <time_format>: Second, Minute, Hour, Percent, SecPer, MinPer, HourPer\n\
     -v or --verbose: equivalent to \"verbose\" command                    \n\
     -V or --version: equivalent to \"version\" command                    \n\
                                                                           \n\
  <keyword> may be omitted from the execute line and sreport will execute  \n\
  in interactive mode. It will process commands as entered until explicitly\n\
  terminated.                                                              \n\
                                                                           \n\
    Valid <COMMAND> values are:                                            \n\
     exit                Terminate sreport                                 \n\
     help                Print this description of use.                    \n\
     parsable            Output will be | delimited with an ending '|'     \n\
     parsable2           Output will be | delimited without an ending '|'  \n\
     quiet               Print no messages other than error messages.      \n\
     quit                Terminate this command.                           \n\
     time <time_format>  Second, Minute, Hour, Percent, SecPer, MinPer, HourPer\n\
     verbose             Enable detailed logging.                          \n\
     version             Display tool version number.                      \n\
     !!                  Repeat the last command entered.                  \n\
                                                                           \n\
    Valid report types are:                                                \n\
     cluster <REPORT> <OPTIONS>                                            \n\
     job <REPORT> <OPTIONS>                                                \n\
     user <REPORT> <OPTIONS>                                               \n\
                                                                           \n\
  <REPORT> is different for each report type.                              \n\
     cluster - AccountUtilizationByUser, UserUtilizationByAccount,         \n\
               UserUtilizationByWckey, Utilization, WCKeyUtilizationByUser \n\
     job     - SizesByAccount, SizesByWckey                                \n\
     user    - TopUsage                                                    \n\
                                                                           \n\
  <OPTIONS> are different for each report type.                            \n\
                                                                           \n\
     COMMON FOR ALL TYPES                                                  \n\
             - All_Clusters     - Use all monitored clusters default is    \n\
                                  local cluster.                           \n\
             - Clusters=<OPT>   - List of clusters to include in report    \n\
                                  Default is local cluster.                \n\
             - End=<OPT>        - Period ending for report.                \n\
                                  Default is 23:59:59 of previous day.     \n\
             - Format=<OPT>     - Comma separated list of fields to display\n\
                                  in report.                               \n\
             - Start=<OPT>      - Period start for report.                 \n\
                                  Default is 00:00:00 of previous day.     \n\
                                                                           \n\
     cluster - Accounts=<OPT>   - When used with the UserUtilizationByAccount,\n\
                                  or AccountUtilizationByUser, List of accounts\n\
                                  to include in report.  Default is all.   \n\
             - Tree             - When used with the AccountUtilizationByUser\n\
                                  report will span the accounts as they    \n\
                                  in the hierarchy.                        \n\
             - Users=<OPT>      - When used with any report other than     \n\
                                  Utilization, List of users to include in \n\
                                  report.  Default is all.                 \n\
             - Wckeys=<OPT>     - When used with the UserUtilizationByWckey\n\
                                  or WCKeyUtilizationByUser, List of wckeys\n\
                                  to include in report.  Default is all.   \n\
                                                                           \n\
     job     - Accounts=<OPT>   - List of accounts to use for the report   \n\
                                  Default is all.  The SizesbyAccount      \n\
                                  report only displays 1 hierarchical level.\n\
                                  If accounts are specified the next layer \n\
                                  of accounts under those specified will be\n\
                                  displayed, not the accounts specified.   \n\
                                  In the SizesByAccount reports the default\n\
                                  for accounts is root.  This explanation  \n\
                                  does not apply when ran with the FlatView\n\
                                  option.                                  \n\
             - FlatView         - When used with the SizesbyAccount        \n\
                                  will not group accounts in a             \n\
                                  hierarchical level, but print each       \n\
                                  account where jobs ran on a separate     \n\
                                  line without any hierarchy.              \n\
             - GID=<OPT>        - List of group ids to include in report.  \n\
                                  Default is all.                          \n\
             - Grouping=<OPT>   - Comma separated list of size groupings.  \n\
                                  (i.e. 50,100,150 would group job cpu count\n\
                                   1-49, 50-99, 100-149, > 150).           \n\
             - Jobs=<OPT>       - List of jobs/steps to include in report. \n\
                                  Default is all.                          \n\
             - Partitions=<OPT> - List of partitions jobs ran on to include\n\
                                  in report.  Default is all.              \n\
             - PrintJobCount    - When used with the any Sizes report      \n\
                                  will print number of jobs ran instead of \n\
                                  time used.                               \n\
             - Users=<OPT>      - List of users jobs to include in report. \n\
                                  Default is all.                          \n\
             - Wckeys=<OPT>     - List of wckeys to use for the report.    \n\
                                  Default is all.  The SizesbyWckey        \n\
                                  report all users summed together.  If    \n\
                                  you want only certain users specify them \n\
                                  them with the Users= option.             \n\
                                                                           \n\
     user    - Accounts=<OPT>   - List of accounts to use for the report   \n\
                                  Default is all.                          \n\
             - Group            - Group all accounts together for each user.\n\
                                  Default is a separate entry for each user\n\
                                  and account reference.                   \n\
             - TopCount=<OPT>   - Used in the TopUsage report.  Change the \n\
                                  number of users displayed.  Default is 10.\n\
             - Users=<OPT>      - List of users jobs to include in report. \n\
                                  Default is all.                          \n\
                                                                           \n\
                                                                           \n\
  Below are the format options for each report.                            \n\
                                                                           \n\
       Cluster                                                             \n\
       - AccountUtilizationByUser                                          \n\
       - UserUtilizationByAccount                                          \n\
             - Accounts, Cluster, CPUCount, Login, Proper, Used            \n\
       - UserUtilizationByWckey                                            \n\
       - WCKeyUtilizationByUser                                            \n\
             - Cluster, CPUCount, Login, Proper, Used, Wckey               \n\
       - Utilization                                                       \n\
             - Allocated, Cluster, CPUCount, Down, Idle, Overcommited,     \n\
               Reported, Reserved                                          \n\
                                                                           \n\
       Job                                                                 \n\
       - Sizes                                                             \n\
             - Account, Cluster                                            \n\
                                                                           \n\
       User                                                                \n\
       - TopUsage                                                          \n\
             - Account, Cluster, Login, Proper, Used                       \n\
                                                                           \n\
                                                                           \n\
                                                                           \n\
  Note, valid start/end time formats are...                                \n\
       HH:MM[:SS] [AM|PM]                                                  \n\
       MMDD[YY] or MM/DD[/YY] or MM.DD[.YY]                                \n\
       MM/DD[/YY]-HH:MM[:SS]                                               \n\
                                                                           \n\
                                                                           \n\
  All commands and options are case-insensitive.                         \n\n");
	
}

