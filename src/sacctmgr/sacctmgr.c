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
#include "src/common/xsignal.h"

#define BUFFER_SIZE 4096

char *command_name;
int exit_code;		/* sacctmgr's exit code, =1 on any error at any time */
int exit_flag;		/* program to terminate if =1 */
int input_words;	/* number of words of input permitted */
int one_liner;		/* one record per line if =1 */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int readonly_flag;      /* make it so you can only run list commands */
int verbosity;		/* count of -v options */
int rollback_flag;       /* immediate execute=1, else = 0 */
int with_assoc_flag = 0;
void *db_conn = NULL;
uint32_t my_uid = 0;

static void	_show_it (int argc, char *argv[]);
static void	_add_it (int argc, char *argv[]);
static void	_modify_it (int argc, char *argv[]);
static void	_delete_it (int argc, char *argv[]);
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
	int local_exit_code = 0;
	char *temp = NULL;
	int option_index;
	static struct option long_options[] = {
		{"help",     0, 0, 'h'},
		{"immediate",0, 0, 'i'},
		{"oneliner", 0, 0, 'o'},
		{"no_header", 0, 0, 'n'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"quiet",    0, 0, 'q'},
		{"readonly", 0, 0, 'r'},
		{"associations", 0, 0, 's'},
		{"usage",    0, 0, 'h'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{NULL,       0, 0, 0}
	};

	command_name      = argv[0];
	rollback_flag     = 1;
	exit_code         = 0;
	exit_flag         = 0;
	input_field_count = 0;
	quiet_flag        = 0;
	readonly_flag     = 0;
	verbosity         = 0;
	log_init("sacctmgr", opts, SYSLOG_FACILITY_DAEMON, NULL);

	while((opt_char = getopt_long(argc, argv, "hionpPqrsvV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"sacctmgr --help\" "
				"for more information\n");
			exit(1);
			break;
		case (int)'h':
			_usage ();
			exit(exit_code);
			break;
		case (int)'i':
			rollback_flag = 0;
			break;
		case (int)'o':
			one_liner = 1;
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
		case (int)'r':
			readonly_flag = 1;
			break;
		case (int)'s':
			with_assoc_flag = 1;
			break;
		case (int)'v':
			quiet_flag = -1;
			verbosity++;
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

	if (verbosity) {
		opts.stderr_level += verbosity;
		opts.prefix_level = 1;
		log_alter(opts, 0, NULL);
	}

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
	/* always do a rollback.  If you don't then if there is an
	 * error you can not rollback ;)
	 */
	db_conn = acct_storage_g_get_connection(false, 0, 1);
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
		/* This is here so if someone made a mistake we allow
		 * them to fix it and let the process happen since there
		 * are checks for global exit_code we need to reset it.
		 */
		if(exit_code) {
			local_exit_code = exit_code;
			exit_code = 0;
		}
	}
	if(local_exit_code) 
		exit_code = local_exit_code;
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
	} else if (strncasecmp (argv[0], "associations", 3) == 0) {
		with_assoc_flag = 1;
	} else if (strncasecmp (argv[0], "dump", 3) == 0) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else
			sacctmgr_dump_cluster((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "help", 2) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_usage ();
	} else if (strncasecmp (argv[0], "load", 2) == 0) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		} else
			load_sacctmgr_cfg_file((argc - 1), &argv[1]);
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
		   (strncasecmp (argv[0], "\\q", 2) == 0) ||
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
	} else if (strncasecmp (argv[0], "readonly", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		readonly_flag = 1;
	} else if (strncasecmp (argv[0], "rollup", 2) == 0) {
		time_t my_time = 0;
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}

		if(argc > 1)
			my_time = parse_time(argv[1], 1);
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

	if(readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;		
	}

	/* reset the connection to get the most recent stuff */
	acct_storage_g_commit(db_conn, 0);

	/* First identify the entity to add */
	if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_add_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 2) == 0) {
		error_code = sacctmgr_add_cluster((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Coordinator", 2) == 0) {
		error_code = sacctmgr_add_coord((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "QOS", 1) == 0) {
		error_code = sacctmgr_add_qos((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_add_user((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in add command\n");
		fprintf(stderr, "Input line must include, ");
		fprintf(stderr, "\"User\", \"Account\", \"Coordinator\", ");
		fprintf(stderr, "\"Cluster\", or \"QOS\"\n");
	}
	
	if (error_code == SLURM_ERROR) {
		exit_code = 1;
	}
}

/* 
 * _show_it - list the slurm configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * undocumented association options wopi and wopl
 * without parent info and without parent limits
 */
static void _show_it (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS;
		
	/* reset the connection to get the most recent stuff */
	acct_storage_g_commit(db_conn, 0);

	/* First identify the entity to list */
	if (strncasecmp (argv[0], "Account", 2) == 0) {
		error_code = sacctmgr_list_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Association", 2) == 0) {
		error_code = sacctmgr_list_association((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_list_cluster((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "QOS", 1) == 0) {
		error_code = sacctmgr_list_qos((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Transactions", 1) == 0) {
		error_code = sacctmgr_list_txn((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_list_user((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in list command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", \"Association\", ");
		fprintf(stderr, "\"Cluster\", or \"QOS\"\n");
	} 
	
	if (error_code == SLURM_ERROR) {
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


	if(readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;		
	}

	/* reset the connection to get the most recent stuff */
	acct_storage_g_commit(db_conn, 0);

	/* First identify the entity to modify */
	if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_modify_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 1) == 0) {
		error_code = sacctmgr_modify_cluster((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_modify_user((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in modify command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", ");
		fprintf(stderr, "or \"Cluster\"\n");
	}

	if (error_code == SLURM_ERROR) {
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

	if(readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;		
	}

	/* reset the connection to get the most recent stuff */
	acct_storage_g_commit(db_conn, 0);

	/* First identify the entity to delete */
	if (strncasecmp (argv[0], "Account", 1) == 0) {
		error_code = sacctmgr_delete_account((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Cluster", 2) == 0) {
		error_code = sacctmgr_delete_cluster((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "Coordinator", 2) == 0) {
		error_code = sacctmgr_delete_coord((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "QOS", 2) == 0) {
		error_code = sacctmgr_delete_qos((argc - 1), &argv[1]);
	} else if (strncasecmp (argv[0], "User", 1) == 0) {
		error_code = sacctmgr_delete_user((argc - 1), &argv[1]);
	} else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in delete command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"User\", \"Account\", \"Coordinator\", ");
		fprintf(stderr, "\"Cluster\", or \"QOS\"\n");
	}
	
	if (error_code == SLURM_ERROR) {
		exit_code = 1;
	}
}

/* _usage - show the valid sacctmgr commands */
void _usage () {
	printf ("\
sacctmgr [<OPTION>] [<COMMAND>]                                            \n\
    Valid <OPTION> values are:                                             \n\
     -h or --help: equivalent to \"help\" command                          \n\
     -i or --immediate: commit changes immediately                         \n\
     -n or --no_header: no header will be added to the beginning of output \n\
     -o or --oneliner: equivalent to \"oneliner\" command                  \n\
     -p or --parsable: output will be '|' delimited with a '|' at the end  \n\
     -P or --parsable2: output will be '|' delimited without a '|' at the end\n\
     -q or --quiet: equivalent to \"quiet\" command                        \n\
     -r or --readonly: equivalent to \"readonly\" command                  \n\
     -s or --associations: equivalent to \"associations\" command          \n\
     -v or --verbose: equivalent to \"verbose\" command                    \n\
     -V or --version: equivalent to \"version\" command                    \n\
                                                                           \n\
  <keyword> may be omitted from the execute line and sacctmgr will execute \n\
  in interactive mode. It will process commands as entered until explicitly\n\
  terminated.                                                              \n\
                                                                           \n\
    Valid <COMMAND> values are:                                            \n\
     add <ENTITY> <SPECS>     add entity                                   \n\
     associations             when using show/list will list the           \n\
                              associations associated with the entity.     \n\
     delete <ENTITY> <SPECS>  delete the specified entity(s)               \n\
     dump <CLUSTER> <FILE>    dump database information of the             \n\
                              specified cluster to the flat file.          \n\
     exit                     terminate sacctmgr                           \n\
     help                     print this description of use.               \n\
     list <ENTITY> [<SPECS>]  display info of identified entity, default   \n\
                              is display all.                              \n\
     load <FILE>              read in the file to update the database      \n\
                              with the file contents.                      \n\
     modify <ENTITY> <SPECS>  modify entity                                \n\
     oneliner                 report output one record per line.           \n\
     parsable                 output will be | delimited with an ending '|'\n\
     parsable2                output will be | delimited without an ending '|'\n\
     readonly                 makes it so no modification can happen.      \n\
     quiet                    print no messages other than error messages. \n\
     quit                     terminate this command.                      \n\
     show                     same as list                                 \n\
     verbose                  enable detailed logging.                     \n\
     version                  display tool version number.                 \n\
     !!                       Repeat the last command entered.             \n\
                                                                           \n\
  <ENTITY> may be \"account\", \"association\", \"cluster\",               \n\
                  \"coordinator\", \"qos\", \"transaction\", or \"user\".  \n\
                                                                           \n\
  <SPECS> are different for each command entity pair.                      \n\
       list account       - Clusters=, Descriptions=, Format=, Names=,     \n\
                            Organizations=, Parents=, WithCoor=,           \n\
                            WithSubAccounts, and WithAssocs                \n\
       add account        - Clusters=, Description=, Fairshare=,           \n\
                            GrpCPUMins=, GrpCPUs=, GrpJobs=, GrpNodes=,    \n\
                            GrpSubmitJob=, GrpWall=, MaxCPUMins=, MaxJobs=,\n\
                            MaxNodes=, MaxWall=, Names=, Organization=,    \n\
                            Parent=, and QosLevel                          \n\
       modify account     - (set options) Description=, Fairshare=,        \n\
                            GrpCPUMins=, GrpCPUs=, GrpJobs=, GrpNodes=,    \n\
                            GrpSubmitJob=, GrpWall=, MaxCPUMins=, MaxJobs=,\n\
                            MaxNodes=, MaxWall=, Names=, Organization=,    \n\
                            Parent=, and QosLevel=                         \n\
                            (where options) Clusters=, Descriptions=,      \n\
                            Names=, Organizations=, Parent=, and QosLevel= \n\
       delete account     - Clusters=, Descriptions=, Names=,              \n\
                            Organizations=, and Parents=                   \n\
                                                                           \n\
       list associations  - Accounts=, Clusters=, Format=, ID=,            \n\
                            Partitions=, Parent=, Tree, Users=,            \n\
                            WithSubAccounts, WithDeleted, WOPInfo,         \n\
                            and WOPLimits                                  \n\
                                                                           \n\
       list cluster       - Names= Format=                                 \n\
       add cluster        - Fairshare=, GrpCPUMins=, GrpCPUs=, GrpJobs=,  \n\
                            GrpNodes=, GrpSubmitJob=, GrpWall=, MaxCPUMins=\n\
                            MaxJobs=, MaxNodes=, MaxWall=, and Name=       \n\
       modify cluster     - (set options) Fairshare=, GrpCPUMins=,         \n\
                            GrpCPUs=, GrpJobs=, GrpNodes=, GrpSubmitJob=,  \n\
                            GrpWall=, MaxCPUMins=, MaxJobs=, MaxNodes=,    \n\
                            and MaxWall=                                   \n\
                            (where options) Names=                         \n\
       delete cluster     - Names=                                         \n\
                                                                           \n\
       add coordinator    - Accounts=, and Names=                          \n\
       delete coordinator - Accounts=, and Names=                          \n\
                                                                           \n\
       list qos           - Descriptions=, Ids=, Names=, and WithDeleted   \n\
       add qos            - Description=, and Names=                       \n\
       delete qos         - Descriptions=, Ids=, and Names=                \n\
                                                                           \n\
       list transactions  - Actor=, EndTime,                               \n\
                            Format=, ID=, and Start=                       \n\
                                                                           \n\
       list user          - AdminLevel=, DefaultAccounts=, Format=, Names=,\n\
                            QosLevel=, WithCoor=, and WithAssocs           \n\
       add user           - Accounts=, AdminLevel=, Clusters=,             \n\
                            DefaultAccount=, Fairshare=, MaxCPUMins=       \n\
                            MaxCPUs=, MaxJobs=, MaxNodes=, MaxWall=,       \n\
                            Names=, Partitions=, and QosLevel=             \n\
       modify user        - (set options) AdminLevel=, DefaultAccount=,    \n\
                            Fairshare=, MaxCPUMins=, MaxCPUs= MaxJobs=,    \n\
                            MaxNodes=, MaxWall=, and QosLevel=             \n\
                            (where options) Accounts=, AdminLevel=,        \n\
                            Clusters=, DefaultAccounts=, Names=,           \n\
                            Partitions=, and QosLevel=                     \n\
       delete user        - Accounts=, AdminLevel=, Clusters=,             \n\
                            DefaultAccounts=, and Names=                   \n\
                                                                           \n\
  Format options are different for listing each entity pair.               \n\
                                                                           \n\
       Account            - Account, CoordinatorList, Description,         \n\
                            Organization                                   \n\
                                                                           \n\
       Association        - Account, Cluster, Fairshare, GrpCPUMins,       \n\
                            GrpCPUs, GrpJobs, GrpNodes, GrpSubmitJob,      \n\
                            GrpWall, ID, LFT, MaxCPUSecs, MaxJobs,         \n\
                            MaxNodes, MaxWall, QOS, ParentID,              \n\
                            ParentName, Partition, RGT, User               \n\
                                                                           \n\
       Cluster            - Cluster, ControlHost, ControlPort, Fairshare   \n\
                            MaxCPUSecs, MaxJobs, MaxNodes, MaxWall         \n\
                                                                           \n\
       QOS                - Description, ID, Name                          \n\
                                                                           \n\
       Transactions       - Action, Actor, ID, Info, TimeStamp, Where      \n\
                                                                           \n\
       User               - AdminLevel, CoordinatorList, DefaultAccount,   \n\
                            User                                           \n\
                                                                           \n\
                                                                           \n\
  All commands entitys, and options are case-insensitive.               \n\n");
	
}

