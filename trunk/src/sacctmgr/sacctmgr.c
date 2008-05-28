/*****************************************************************************\
 *  sacctmgr.c - administration tool for slurm's accounting. 
 *	         provides interface to read, write, update, and configure
 *               accounting.
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/sacctmgr/print.h"
#include "src/common/xsignal.h"

#define OPT_LONG_HIDE   0x102
#define BUFFER_SIZE 4096

typedef struct {
	uint16_t admin;
	char *def_acct;
	char *desc;
	uint32_t fairshare;
	uint32_t max_cpu_secs_per_job; 
	uint32_t max_jobs;
	uint32_t max_nodes_per_job; 
	uint32_t max_wall_duration_per_job;
	char *name;
	char *org;
	char *part;
	uint16_t qos;
} sacctmgr_file_opts_t;


char *command_name;
int all_flag;		/* display even hidden partitions */
int exit_code;		/* sacctmgr's exit code, =1 on any error at any time */
int exit_flag;		/* program to terminate if =1 */
int input_words;	/* number of words of input permitted */
int one_liner;		/* one record per line if =1 */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int rollback_flag;       /* immediate execute=1, else = 0 */
int with_assoc_flag = 0;
void *db_conn = NULL;
uint32_t my_uid = 0;

static void	_show_it (int argc, char *argv[]);
static void	_add_it (int argc, char *argv[]);
static void	_modify_it (int argc, char *argv[]);
static void	_delete_it (int argc, char *argv[]);
static void	_load_file (int argc, char *argv[]);
static int	_get_command (int *argc, char *argv[]);
static void     _print_version( void );
static int	_process_command (int argc, char *argv[]);
static void	_usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS, i, opt_char, input_field_count;
	char **input_fields;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	int option_index;
	static struct option long_options[] = {
		{"all",      0, 0, 'a'},
		{"help",     0, 0, 'h'},
		{"hide",     0, 0, OPT_LONG_HIDE},
		{"immediate",0, 0, 'i'},
		{"oneliner", 0, 0, 'o'},
		{"no_header", 0, 0, 'n'},
		{"parsable", 0, 0, 'p'},
		{"quiet",    0, 0, 'q'},
		{"usage",    0, 0, 'h'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{NULL,       0, 0, 0}
	};

	command_name      = argv[0];
	all_flag          = 0;
	rollback_flag     = 1;
	exit_code         = 0;
	exit_flag         = 0;
	input_field_count = 0;
	quiet_flag        = 0;
	log_init("sacctmgr", opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (getenv ("SACCTMGR_ALL"))
		all_flag= 1;

	while((opt_char = getopt_long(argc, argv, "ahionpqsvV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"sacctmgr --help\" "
				"for more information\n");
			exit(1);
			break;
		case (int)'a':
			all_flag = 1;
			break;
		case (int)'h':
			_usage ();
			exit(exit_code);
			break;
		case OPT_LONG_HIDE:
			all_flag = 0;
			break;
		case (int)'i':
			rollback_flag = 0;
			break;
		case (int)'o':
			one_liner = 1;
			break;
		case (int)'n':
			have_header = 0;
			break;
		case (int)'p':
			parsable_print = 1;
			break;
		case (int)'q':
			quiet_flag = 1;
			break;
		case (int)'s':
			with_assoc_flag = 1;
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

	db_conn = acct_storage_g_get_connection(false, rollback_flag);
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
	printf("\n");
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
	in_line = readline ("sacctmgr: ");
#else
	in_line = getline("sacctmgr: ");
#endif
	if (in_line == NULL)
		return 0;
	else if (strcmp (in_line, "!!") == 0) {
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
 * RET 0 or errno (only for errors fatal to sacctmgr)
 */
static int
_process_command (int argc, char *argv[]) 
{
	if (argc < 1) {
		exit_code = 1;
		if (quiet_flag == -1)
			fprintf(stderr, "no input");
	} else if (strncasecmp (argv[0], "all", 3) == 0) {
		all_flag = 1;
	} else if (strncasecmp (argv[0], "associations", 3) == 0) {
		with_assoc_flag = 1;
	} else if (strncasecmp (argv[0], "help", 2) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_usage ();
	} else if (strncasecmp (argv[0], "hide", 2) == 0) {
		all_flag = 0;
	} else if (strncasecmp (argv[0], "load", 2) == 0) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else
			_load_file((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "oneliner", 1) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		one_liner = 1;
	} else if (strncasecmp (argv[0], "quiet", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		quiet_flag = 1;
	} else if ((strncasecmp (argv[0], "exit", 4) == 0) ||
		   (strncasecmp (argv[0], "quit", 4) == 0)) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		exit_flag = 1;
	} else if ((strncasecmp (argv[0], "add", 3) == 0) ||
		   (strncasecmp (argv[0], "create", 3) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else
			_add_it((argc - 1), &argv[1]);
	} else if ((strncasecmp (argv[0], "show", 3) == 0) ||
		   (strncasecmp (argv[0], "list", 3) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else 
			_show_it((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "modify", 1) == 0) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		} else 		
			_modify_it((argc - 1), &argv[1]);
	} else if ((strncasecmp (argv[0], "delete", 3) == 0) ||
		   (strncasecmp (argv[0], "remove", 3) == 0)) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		} else 
			_delete_it((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "verbose", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		quiet_flag = -1;
	} else if (strncasecmp (argv[0], "rollup", 2) == 0) {
		time_t my_time = 0;
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}

		if(argc > 1)
			my_time = parse_time(argv[1]);
		if(acct_storage_g_roll_usage(db_conn, my_time)
		   == SLURM_SUCCESS) {
			if(commit_check("Would you like to commit rollup?")) {
				acct_storage_g_commit(db_conn, 1);
			} else {
				printf(" Rollup Discarded\n");
				acct_storage_g_commit(db_conn, 0);
			}
		}
	} else if (strncasecmp (argv[0], "version", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		_print_version();
	} else {
		exit_code = 1;
		fprintf (stderr, "invalid keyword: %s\n", argv[0]);
	}
		
	return 0;
}

/* 
 * _add_it - add the entity per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _add_it (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	/* First identify the entity to add */
	if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_add_user((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_add_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_add_cluster((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in add command\n");
		fprintf(stderr, "Input line must include, ");
		fprintf(stderr, "\"User\", \"Account\", ");
		fprintf(stderr, "or \"Cluster\"\n");
	}
	
	if (error_code) {
		exit_code = 1;
	}
}

/* 
 * _show_it - list the slurm configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _show_it (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;
		
	/* First identify the entity to list */
	if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_list_user((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Account", 2) == 0) {
		error_code = sacctmgr_list_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Association", 2) == 0) {
		error_code = sacctmgr_list_association((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_list_cluster((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in list command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", \"Association\", ");
		fprintf(stderr, "or \"Cluster\"\n");
	} 
	
	if (error_code) {
		exit_code = 1;
	}
}


/* 
 * _modify_it - modify the slurm configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _modify_it (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	/* First identify the entity to modify */
	if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_modify_user((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_modify_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_modify_cluster((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in modify command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", ");
		fprintf(stderr, "or \"Cluster\"\n");
	}

	if (error_code) {
		exit_code = 1;
	}
}

/* 
 * _delete_it - delete the slurm configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _delete_it (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;

	/* First identify the entity to delete */
	if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_delete_user((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_delete_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_delete_cluster((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in delete command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", ");
		fprintf(stderr, "or \"Cluster\"\n");
	}
	
	if (error_code) {
		exit_code = 1;
	}
}

static int _strip_continuation(char *buf, int len)
{
	char *ptr;
	int bs = 0;

	for (ptr = buf+len-1; ptr >= buf; ptr--) {
		if (*ptr == '\\')
			bs++;
		else if (isspace(*ptr) && bs == 0)
			continue;
		else
			break;
	}
	/* Check for an odd number of contiguous backslashes at
	   the end of the line */
	if (bs % 2 == 1) {
		ptr = ptr + bs;
		*ptr = '\0';
		return (ptr - buf);
	} else {
		return len; /* no continuation */
	}
}

/* Strip comments from a line by terminating the string
 * where the comment begins.
 * Everything after a non-escaped "#" is a comment.
 */
static void _strip_comments(char *line)
{
	int i;
	int len = strlen(line);
	int bs_count = 0;

	for (i = 0; i < len; i++) {
		/* if # character is preceded by an even number of
		 * escape characters '\' */
		if (line[i] == '#' && (bs_count%2) == 0) {
			line[i] = '\0';
 			break;
		} else if (line[i] == '\\') {
			bs_count++;
		} else {
			bs_count = 0;
		}
	}
}

/*
 * Strips any escape characters, "\".  If you WANT a back-slash,
 * it must be escaped, "\\".
 */
static void _strip_escapes(char *line)
{
	int i, j;
	int len = strlen(line);

	for (i = 0, j = 0; i < len+1; i++, j++) {
		if (line[i] == '\\')
			i++;
		line[j] = line[i];
	}
}

/*
 * Reads the next line from the "file" into buffer "buf".
 *
 * Concatonates together lines that are continued on
 * the next line by a trailing "\".  Strips out comments,
 * replaces escaped "\#" with "#", and replaces "\\" with "\".
 */
static int _get_next_line(char *buf, int buf_size, FILE *file)
{
	char *ptr = buf;
	int leftover = buf_size;
	int read_size, new_size;
	int lines = 0;

	while (fgets(ptr, leftover, file)) {
		lines++;
		_strip_comments(ptr);
		read_size = strlen(ptr);
		new_size = _strip_continuation(ptr, read_size);
		if (new_size < read_size) {
			ptr += new_size;
			leftover -= new_size;
		} else { /* no continuation */
			break;
		}
	}
	/* _strip_cr_nl(buf); */ /* not necessary */
	_strip_escapes(buf);
	
	return lines;
}

static void _destroy_sacctmgr_file_opts(void *object)
{
	sacctmgr_file_opts_t *file_opts = (sacctmgr_file_opts_t *)object;

	if(file_opts) {
		xfree(file_opts->def_acct);
		xfree(file_opts->desc);
		xfree(file_opts->name);
		xfree(file_opts->org);
		xfree(file_opts->part);
		xfree(file_opts);		
	}
}

static sacctmgr_file_opts_t *_parse_options(char *options)
{
	int start=0, i=0, end=0, mins, quote = 0;
 	char *sub = NULL;
	sacctmgr_file_opts_t *file_opts = xmalloc(sizeof(sacctmgr_file_opts_t));
	char *option = NULL;

	file_opts->fairshare = NO_VAL;
	file_opts->max_cpu_secs_per_job = NO_VAL;
	file_opts->max_jobs = NO_VAL;
	file_opts->max_nodes_per_job = NO_VAL;
	file_opts->max_wall_duration_per_job = NO_VAL;

	while(options[i]) {
		quote = 0;
		start=i;
		
		while(options[i] && options[i] != ':' && options[i] != '\n') {
			if(options[i] == '"') {
				if(quote)
					quote = 0;
				else
					quote = 1;
			}
			i++;
		}
		if(quote) {
			while(options[i] && options[i] != '"') 
				i++;
			if(!options[i])
				fatal("There is a problem with option "
				      "%s with quotes.", option);
			i++;
		}
		sub = xstrndup(options+start, i-start);
		end = parse_option_end(sub);
		
		option = strip_quotes(sub+end, NULL);
		
		if(!end) {
			if(file_opts->name) {
				printf(" Bad format on %s: "
				       "End your option with "
				       "an '=' sign\n", sub);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
			file_opts->name = xstrdup(option);
		} else if (strncasecmp (sub, "AdminLevel", 2) == 0) {
			file_opts->admin = str_2_acct_admin_level(option);
		} else if (strncasecmp (sub, "DefaultAccount", 3) == 0) {
			file_opts->def_acct = xstrdup(option);
		} else if (strncasecmp (sub, "Description", 3) == 0) {
			file_opts->desc = xstrdup(option);
		} else if (strncasecmp (sub, "FairShare", 1) == 0) {
			if (get_uint(option, &file_opts->fairshare, 
			    "FairShare") != SLURM_SUCCESS) {
				printf(" Bad FairShare value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (strncasecmp (sub, "MaxCPUSec", 4) == 0
			   || strncasecmp (sub, "MaxProcSec", 4) == 0) {
			if (get_uint(option, &file_opts->max_cpu_secs_per_job,
			    "MaxCPUSec") != SLURM_SUCCESS) {
				printf(" Bad MaxCPUSec value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (strncasecmp (sub, "MaxJobs", 4) == 0) {
			if (get_uint(option, &file_opts->max_jobs,
			    "MaxJobs") != SLURM_SUCCESS) {
				printf(" Bad MaxJobs value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (strncasecmp (sub, "MaxNodes", 4) == 0) {
			if (get_uint(option, &file_opts->max_nodes_per_job,
			    "MaxNodes") != SLURM_SUCCESS) {
				printf(" Bad MaxNodes value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (strncasecmp (sub, "MaxWall", 4) == 0) {
			mins = time_str2mins(option);
			if (mins >= 0) {
				file_opts->max_wall_duration_per_job 
					= (uint32_t) mins;
			} else if (strcmp(option, "-1") == 0) {
				file_opts->max_wall_duration_per_job = -1;
			} else {
				printf(" Bad MaxWall time format: %s\n", 
					option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (strncasecmp (sub, "Organization", 1) == 0) {
			file_opts->org = xstrdup(option);
		} else if (strncasecmp (sub, "QosLevel", 1) == 0
			   || strncasecmp (sub, "Expedite", 1) == 0) {
			file_opts->qos = str_2_acct_qos(option);
		} else {
			printf(" Unknown option: %s\n", sub);
		}

		xfree(sub);
		xfree(option);

		if(options[i] == ':')
			i++;
		else
			break;
	}
	
	xfree(sub);
	xfree(option);

	if(!file_opts->name) {
		printf(" error: No name given\n");
		_destroy_sacctmgr_file_opts(file_opts);
	}
	return file_opts;
}

static void _load_file (int argc, char *argv[])
{
	DEF_TIMERS;
	char line[BUFFER_SIZE];
	FILE *fd = NULL;
	char *parent = NULL;
	char *cluster_name = NULL;
	char object[25];
	int start = 0, len = 0, i = 0;
	int lc=0, num_lines=0;
	int rc = SLURM_SUCCESS;

	sacctmgr_file_opts_t *file_opts = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_account_rec_t *acct = NULL;
	acct_cluster_rec_t *cluster = NULL;
	acct_user_rec_t *user = NULL;

	List curr_assoc_list = NULL;
	List curr_acct_list = acct_storage_g_get_accounts(db_conn, NULL);
	List curr_cluster_list = acct_storage_g_get_clusters(db_conn, NULL);
	List curr_user_list = acct_storage_g_get_users(db_conn, NULL);

	/* This will be freed in their local counter parts */
	List acct_list = list_create(NULL);
	List acct_assoc_list = list_create(NULL);
	List user_list = list_create(NULL);
	List user_assoc_list = list_create(NULL);

	ListIterator itr;

	List print_fields_list;

	print_field_t name_field;
	print_field_t acct_field;
	print_field_t parent_field;
	print_field_t fs_field;
	print_field_t mc_field;
	print_field_t mj_field;
	print_field_t mn_field;
	print_field_t mw_field;

	print_field_t desc_field;
	print_field_t org_field;
	print_field_t qos_field;

	print_field_t admin_field;
	print_field_t dacct_field;
	
	int set = 0;
	
	fd = fopen(argv[0], "r");
	if (fd == NULL) {
		printf(" error: Unable to read \"%s\": %m\n", argv[0]);
		return;
	}

	while((num_lines = _get_next_line(line, BUFFER_SIZE, fd)) > 0) {
		lc += num_lines;
		/* skip empty lines */
		if (line[0] == '\0') {
			continue;
		}
		len = strlen(line);

		memset(object, 0, sizeof(object));

		/* first find the object */
		start=0;
		for(i=0; i<len; i++) {
			if(line[i] == '-') {
				start = i;
				if(line[i-1] == ' ') 
					i--;
				if(i<sizeof(object))
					strncpy(object, line, i);
				break;
			} 
		}
		if(!object[0]) {
			printf(" error: Misformatted line(%d): %s\n", lc, line);
			rc = SLURM_ERROR;
			break;
		} 
		while(line[start] != ' ' && start<len)
			start++;
		if(start>=len) {
			printf(" error: Nothing after object "
			       "name '%s'. line(%d)\n",
			       object, lc);
			rc = SLURM_ERROR;
			break;
			
		}
		start++;
		
		if(!strcasecmp("Machine", object) 
		   || !strcasecmp("Cluster", object)) {
			acct_association_cond_t assoc_cond;

			if(cluster_name) {
				printf(" You can only add one cluster "
				       "at a time.\n");
				rc = SLURM_ERROR;
				break;
			}
			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				printf(" error: Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}
			cluster_name = xstrdup(file_opts->name);
			if(!sacctmgr_find_cluster_from_list(
				   curr_cluster_list, cluster_name)) {
				List cluster_list =
					list_create(destroy_acct_cluster_rec);
				cluster = xmalloc(sizeof(acct_cluster_rec_t));
				list_append(cluster_list, cluster);
				cluster->name = xstrdup(cluster_name);
				cluster->default_fairshare =
					file_opts->fairshare;		
				cluster->default_max_cpu_secs_per_job = 
					file_opts->max_cpu_secs_per_job;
				cluster->default_max_jobs = file_opts->max_jobs;
				cluster->default_max_nodes_per_job = 
					file_opts->max_nodes_per_job;
				cluster->default_max_wall_duration_per_job = 
					file_opts->max_wall_duration_per_job;
				notice_thread_init();
				rc = acct_storage_g_add_clusters(
					db_conn, my_uid, cluster_list);
				notice_thread_fini();
				list_destroy(cluster_list);

				if(rc != SLURM_SUCCESS) {
					printf(" Problem adding machine\n");
					rc = SLURM_ERROR;
					break;
				}
			}
			info("For cluster %s", cluster_name);
			_destroy_sacctmgr_file_opts(file_opts);
			
			memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
			assoc_cond.cluster_list = list_create(NULL);
			list_append(assoc_cond.cluster_list, cluster_name);
			curr_assoc_list = acct_storage_g_get_associations(
				db_conn, &assoc_cond);
			list_destroy(assoc_cond.cluster_list);

			if(!curr_assoc_list) {
				printf(" Problem getting associations "
				       "for this cluster\n");
				rc = SLURM_ERROR;
				break;
			}
			//info("got %d assocs", list_count(curr_assoc_list));
			continue;
		} else if(!cluster_name) {
			printf(" error: You need to specify a cluster name "
			       "first with 'Cluster - name' in your file\n");
			break;
		}

		if(!strcasecmp("Parent", object)) {
			if(parent) 
				xfree(parent);
			
			i = start;
			while(line[i] != '\n' && i<len)
				i++;
			
			if(i >= len) {
				printf(" error: No parent name "
				       "given line(%d)\n",
				       lc);
				rc = SLURM_ERROR;
				break;
			}
			parent = xstrndup(line+start, i-start);
			//info("got parent %s", parent);
			if(!sacctmgr_find_account_base_assoc_from_list(
				   curr_assoc_list, parent, cluster_name)
			   && !sacctmgr_find_account_base_assoc_from_list(
				   acct_assoc_list, parent, cluster_name)) {
				printf(" error: line(%d) You need to add "
				       "this parent (%s) as a child before "
				       "you can add childern to it.\n",
				       lc, parent);
				break;
			}
			continue;
		} else if(!parent) {
			parent = xstrdup("root");
			printf(" No parent given creating off root, "
			       "If incorrect specify 'Parent - name' "
			       "before any childern in your file\n");
		} 
	
		if(!strcasecmp("Project", object)
		   || !strcasecmp("Account", object)) {
			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				printf(" error: Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}
			
			//info("got a project %s of %s", file_opts->name, parent);
			if(!sacctmgr_find_account_from_list(
				   curr_acct_list, file_opts->name)) {
				acct = xmalloc(sizeof(acct_account_rec_t));
				acct->assoc_list = NULL;	
				acct->name = xstrdup(file_opts->name);
				if(file_opts->desc) 
					acct->description =
						xstrdup(file_opts->desc);
				else
					acct->description = 
						xstrdup(file_opts->name);
				if(file_opts->org)
					acct->organization =
						xstrdup(file_opts->org);
				else if(strcmp(parent, "root"))
					acct->organization = xstrdup(parent);
				else
					acct->organization =
						xstrdup(file_opts->name);
				/* info("adding acct %s (%s) (%s)", */
/* 				     acct->name, acct->description, */
/* 				     acct->organization); */
				acct->qos = file_opts->qos;
				list_append(acct_list, acct);
				list_append(curr_acct_list, acct);

				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(file_opts->name);
				assoc->cluster = xstrdup(cluster_name);
				assoc->parent_acct = xstrdup(parent);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_per_job =
					file_opts->max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					file_opts->max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job = 
					file_opts->max_cpu_secs_per_job;
				list_append(acct_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(!sacctmgr_find_account_base_assoc_from_list(
					  curr_assoc_list, file_opts->name,
					  cluster_name) &&
				  !sacctmgr_find_account_base_assoc_from_list(
					  acct_assoc_list, file_opts->name,
					  cluster_name)) {
				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(file_opts->name);
				assoc->cluster = xstrdup(cluster_name);
				assoc->parent_acct = xstrdup(parent);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_per_job =
					file_opts->max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					file_opts->max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job = 
					file_opts->max_cpu_secs_per_job;
				list_append(acct_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			}
			_destroy_sacctmgr_file_opts(file_opts);
			continue;
		} else if(!strcasecmp("User", object)) {
			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				printf(" error: Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}
			if(!sacctmgr_find_user_from_list(
				   curr_user_list, file_opts->name)
			   && !sacctmgr_find_user_from_list(
				   user_list, file_opts->name)) {
				user = xmalloc(sizeof(acct_user_rec_t));
				user->assoc_list = NULL;
				user->name = xstrdup(file_opts->name);
				if(file_opts->def_acct)
					user->default_acct = 
						xstrdup(file_opts->def_acct);
				else
					user->default_acct = xstrdup(parent);
					
				user->qos = file_opts->qos;
				user->admin_level = file_opts->admin;
				
				list_append(user_list, user);
				/* don't add anything to the
				   curr_user_list */

				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(parent);
				assoc->cluster = xstrdup(cluster_name);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_per_job =
					file_opts->max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					file_opts->max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job = 
					file_opts->max_cpu_secs_per_job;
				assoc->partition = xstrdup(file_opts->part);
				assoc->user = xstrdup(file_opts->name);
				
				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(!sacctmgr_find_association_from_list(
					  curr_assoc_list,
					  file_opts->name, parent,
					  cluster_name, file_opts->part)
				&& !sacctmgr_find_association_from_list(
					  user_assoc_list,
					  file_opts->name, parent,
					  cluster_name, file_opts->part)) {
				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(parent);
				assoc->cluster = xstrdup(cluster_name);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_per_job =
					file_opts->max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					file_opts->max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job = 
					file_opts->max_cpu_secs_per_job;
				assoc->partition = xstrdup(file_opts->part);
				assoc->user = xstrdup(file_opts->name);
				
				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			}
			//info("got a user %s", file_opts->name);
			_destroy_sacctmgr_file_opts(file_opts);
			continue;
		} else {
			printf(" error: Misformatted line(%d): %s\n", lc, line);
			rc = SLURM_ERROR;
			break;
		}
	}
	fclose(fd);
	xfree(cluster_name);
	xfree(parent);

	admin_field.name = "Admin";
	admin_field.len = 9;
	admin_field.print_routine = print_str;

	name_field.name = "Name";
	name_field.len = 10;
	name_field.print_routine = print_str;
		
	parent_field.name = "Parent";
	parent_field.len = 10;
	parent_field.print_routine = print_str;
	
	acct_field.name = "Account";
	acct_field.len = 10;
	acct_field.print_routine = print_str;
	
	dacct_field.name = "Def Acct";
	dacct_field.len = 10;
	dacct_field.print_routine = print_str;
	
	desc_field.name = "Descr";
	desc_field.len = 10;
	desc_field.print_routine = print_str;
	
	org_field.name = "Org";
	org_field.len = 10;
	org_field.print_routine = print_str;
	
	qos_field.name = "QOS";
	qos_field.len = 9;
	qos_field.print_routine = print_str;
	
	fs_field.name = "FairShare";
	fs_field.len = 10;
	fs_field.print_routine = print_uint;

	mc_field.name = "MaxCPUSecs";
	mc_field.len = 10;
	mc_field.print_routine = print_uint;

	mj_field.name = "MaxJobs";
	mj_field.len = 7;
	mj_field.print_routine = print_uint;

	mn_field.name = "MaxNodes";
	mn_field.len = 8;
	mn_field.print_routine = print_uint;

	mw_field.name = "MaxWall";
	mw_field.len = 7;
	mw_field.print_routine = print_time;
		
	START_TIMER;
	if(rc == SLURM_SUCCESS && list_count(acct_list)) {
		printf("Accounts\n");

		print_fields_list = list_create(NULL);
		list_append(print_fields_list, &name_field);
		list_append(print_fields_list, &desc_field);
		list_append(print_fields_list, &org_field);
		list_append(print_fields_list, &qos_field);

		print_header(print_fields_list);

		itr = list_iterator_create(acct_list);
		while((acct = list_next(itr))) {
			print_str(SLURM_PRINT_VALUE, &name_field, 
				  acct->name);
			print_str(SLURM_PRINT_VALUE, &desc_field, 
				  acct->description);
			print_str(SLURM_PRINT_VALUE, &org_field, 
				  acct->organization);
			print_str(SLURM_PRINT_VALUE, &qos_field, 
				  acct_qos_str(acct->qos));
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_destroy(print_fields_list);
		rc = acct_storage_g_add_accounts(db_conn, my_uid, acct_list);
		printf("---------------------------------------------------\n");
		set = 1;
	}
	
	if(rc == SLURM_SUCCESS && list_count(acct_assoc_list)) {
		printf("Account Associations\n");

		print_fields_list = list_create(NULL);
		list_append(print_fields_list, &name_field);
		list_append(print_fields_list, &parent_field);
		list_append(print_fields_list, &fs_field);
		list_append(print_fields_list, &mc_field);
		list_append(print_fields_list, &mj_field);
		list_append(print_fields_list, &mn_field);
		list_append(print_fields_list, &mw_field);

		print_header(print_fields_list);
		
		itr = list_iterator_create(acct_assoc_list);
		while((assoc = list_next(itr))) {
			print_str(SLURM_PRINT_VALUE, &name_field, assoc->acct);
			print_str(SLURM_PRINT_VALUE, &parent_field, 
				  assoc->parent_acct);
			print_uint(SLURM_PRINT_VALUE, &fs_field, 
				   assoc->fairshare);
			print_uint(SLURM_PRINT_VALUE, &mc_field, 
				   assoc->max_cpu_secs_per_job);
			print_uint(SLURM_PRINT_VALUE, &mj_field, 
				   assoc->max_jobs);
			print_uint(SLURM_PRINT_VALUE, &mn_field, 
				   assoc->max_nodes_per_job);
			print_time(SLURM_PRINT_VALUE, &mw_field,
				   assoc->max_wall_duration_per_job);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_destroy(print_fields_list);
		
		rc = acct_storage_g_add_associations(
			db_conn, my_uid, acct_assoc_list);
		printf("---------------------------------------------------\n");
		set = 1;
	}
	if(rc == SLURM_SUCCESS && list_count(user_list)) {
		printf("Users\n");

		print_fields_list = list_create(NULL);
		list_append(print_fields_list, &name_field);
		list_append(print_fields_list, &dacct_field);
		list_append(print_fields_list, &qos_field);
		list_append(print_fields_list, &admin_field);

		print_header(print_fields_list);

		itr = list_iterator_create(user_list);
		while((acct = list_next(itr))) {
			print_str(SLURM_PRINT_VALUE, &name_field, user->name);
			print_str(SLURM_PRINT_VALUE, &dacct_field, 
				  user->default_acct);
			print_str(SLURM_PRINT_VALUE, &qos_field, 
				  acct_qos_str(user->qos));
			print_str(SLURM_PRINT_VALUE, &admin_field,
				  acct_admin_level_str(user->admin_level));
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_destroy(print_fields_list);
		
		rc = acct_storage_g_add_users(db_conn, my_uid, user_list);
		printf("---------------------------------------------------\n");
		set = 1;
	}

	if(rc == SLURM_SUCCESS && list_count(user_assoc_list)) {
		printf("User Associations\n");

		print_fields_list = list_create(NULL);
		list_append(print_fields_list, &name_field);
		list_append(print_fields_list, &acct_field);
		list_append(print_fields_list, &fs_field);
		list_append(print_fields_list, &mc_field);
		list_append(print_fields_list, &mj_field);
		list_append(print_fields_list, &mn_field);
		list_append(print_fields_list, &mw_field);

		print_header(print_fields_list);
		
		itr = list_iterator_create(user_assoc_list);
		while((assoc = list_next(itr))) {
			print_str(SLURM_PRINT_VALUE, &name_field, assoc->user);
			print_str(SLURM_PRINT_VALUE, &acct_field, assoc->acct);
			print_uint(SLURM_PRINT_VALUE, &fs_field, 
				   assoc->fairshare);
			print_uint(SLURM_PRINT_VALUE, &mc_field, 
				   assoc->max_cpu_secs_per_job);
			print_uint(SLURM_PRINT_VALUE, &mj_field, 
				   assoc->max_jobs);
			print_uint(SLURM_PRINT_VALUE, &mn_field, 
				   assoc->max_nodes_per_job);
			print_uint(SLURM_PRINT_VALUE, &mw_field,
				   assoc->max_wall_duration_per_job);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_destroy(print_fields_list);
	
		rc = acct_storage_g_add_associations(
			db_conn, my_uid, user_assoc_list);
		printf("---------------------------------------------------\n");
		set = 1;
	}
	END_TIMER2("add cluster");
		
	info("Done adding cluster in %s", TIME_STR);
		
	if(rc == SLURM_SUCCESS) {
		if(set) {
			if(commit_check("Would you like to commit changes?")) {
				acct_storage_g_commit(db_conn, 1);
			} else {
				printf(" Changes Discarded\n");
				acct_storage_g_commit(db_conn, 0);
			}
		} else {
			printf(" Nothing new added.\n");
		}
	} else {
		printf(" error: Problem with requests.\n");
	}

	list_destroy(acct_list);
	list_destroy(acct_assoc_list);
	list_destroy(user_list);
	list_destroy(user_assoc_list);
	if(curr_acct_list)
		list_destroy(curr_acct_list);
	if(curr_assoc_list)
		list_destroy(curr_assoc_list);
	if(curr_cluster_list)
		list_destroy(curr_cluster_list);
	if(curr_user_list)
		list_destroy(curr_user_list);
}

/* _usage - show the valid sacctmgr commands */
void _usage () {
	printf ("\
sacctmgr [<OPTION>] [<COMMAND>]                                            \n\
    Valid <OPTION> values are:                                             \n\
     -a or --all: equivalent to \"all\" command                            \n\
     -h or --help: equivalent to \"help\" command                          \n\
     --hide: equivalent to \"hide\" command                                \n\
     -i or --immediate: commit changes immediately                         \n\
     -o or --oneliner: equivalent to \"oneliner\" command                  \n\
     -q or --quiet: equivalent to \"quiet\" command                        \n\
     -s or --associations: equivalent to \"associations\" command          \n\
     -v or --verbose: equivalent to \"verbose\" command                    \n\
     -V or --version: equivalent to \"version\" command                    \n\
                                                                           \n\
  <keyword> may be omitted from the execute line and sacctmgr will execute \n\
  in interactive mode. It will process commands as entered until explicitly\n\
  terminated.                                                              \n\
                                                                           \n\
    Valid <COMMAND> values are:                                            \n\
     all                      display information about all entities,      \n\
                              including hidden/deleted ones.               \n\
     add <ENTITY> <SPECS>     add entity                                   \n\
     associations             when using show/list will list the           \n\
                              associations associated with the entity.     \n\
     delete <ENTITY> <SPECS>  delete the specified entity(s)               \n\
     exit                     terminate sacctmgr                           \n\
     help                     print this description of use.               \n\
     hide                     do not display information about             \n\
                              hidden/deleted entities.                     \n\
     list <ENTITY> [<SPECS>]  display info of identified entity, default   \n\
                              is display all.                              \n\
     modify <ENTITY> <SPECS>  modify entity                                \n\
     no_header                no header will be added to the beginning of  \n\
                              output.                                      \n\
     oneliner                 report output one record per line.           \n\
     quiet                    print no messages other than error messages. \n\
     quit                     terminate this command.                      \n\
     parsable                 output will be | delimited                   \n\
     show                     same as list                                 \n\
     verbose                  enable detailed logging.                     \n\
     version                  display tool version number.                 \n\
     !!                       Repeat the last command entered.             \n\
                                                                           \n\
  <ENTITY> may be \"cluster\", \"account\", or \"user\".                   \n\
                                                                           \n\
  <SPECS> are different for each command entity pair.                      \n\
       list account       - Clusters=, Descriptions=, Format=, Names=,     \n\
                            Organizations=, Parents=, and WithAssocs       \n\
       add account        - Clusters=, Description=, Fairshare=,           \n\
                            MaxCPUSecs=, MaxJobs=, MaxNodes=, MaxWall=,    \n\
                            Names=, Organization=, Parent=, and QosLevel   \n\
       modify account     - (set options) Description=, Fairshare=,        \n\
                            MaxCPUSecs=, MaxJobs=, MaxNodes=, MaxWall=,    \n\
                            Organization=, Parent=, and QosLevel=          \n\
                            (where options) Clusters=, Descriptions=,      \n\
                            Names=, Organizations=, Parent=, and QosLevel= \n\
       delete account     - Clusters=, Descriptions=, Names=,              \n\
                            Organizations=, and Parents=                   \n\
                                                                           \n\
       list associations  - Accounts=, Clusters=, Format=, ID=,            \n\
                            Partitions=, Parent=, Users=                   \n\
                                                                           \n\
       list cluster       - Names= Format=                                 \n\
       add cluster        - Fairshare=, MaxCPUSecs=,                       \n\
                            MaxJobs=, MaxNodes=, MaxWall=, and Names=      \n\
       modify cluster     - (set options) Fairshare=, MaxCPUSecs=,         \n\
                            MaxJobs=, MaxNodes=, and MaxWall=              \n\
                            (where options) Names=                         \n\
       delete cluster     - Names=                                         \n\
                                                                           \n\
       list user          - AdminLevel=, DefaultAccounts=, Format=, Names=,\n\
                            QosLevel=, and WithAssocs                      \n\
       add user           - Accounts=, AdminLevel=, Clusters=,             \n\
                            DefaultAccount=, Fairshare=, MaxCPUSecs=,      \n\
                            MaxJobs=, MaxNodes=, MaxWall=, Names=,         \n\
                            Partitions=, and QosLevel=                     \n\
       modify user        - (set options) AdminLevel=, DefaultAccount=,    \n\
                            Fairshare=, MaxCPUSecs=, MaxJobs=,             \n\
                            MaxNodes=, MaxWall=, and QosLevel=             \n\
                            (where options) Accounts=, AdminLevel=,        \n\
                            Clusters=, DefaultAccounts=, Names=,           \n\
                            Partitions=, and QosLevel=                     \n\
       delete user        - Accounts=, AdminLevel=, Clusters=,             \n\
                            DefaultAccounts=, and Names=                   \n\
                                                                           \n\
                                                                           \n\
  All commands entitys, and options are case-insensitive.               \n\n");
	
}

