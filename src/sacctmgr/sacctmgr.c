/*****************************************************************************\
 *  sacctmgr.c - administration tool for slurm's accounting.
 *	         provides interface to read, write, update, and configure
 *               accounting.
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/data.h"
#include "src/common/ref.h"
#include "src/common/xsignal.h"
#include "src/common/proc_args.h"
#include "src/common/strlcpy.h"

#include "src/interfaces/serializer.h"

#define OPT_LONG_AUTOCOMP 0x100
#define OPT_LONG_JSON 0x101
#define OPT_LONG_YAML 0x102

char *command_name;
int exit_code;		/* sacctmgr's exit code, =1 on any error at any time */
int exit_flag;		/* program to terminate if =1 */
int one_liner;		/* one record per line if =1 */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int readonly_flag;      /* make it so you can only run list commands */
int verbosity;		/* count of -v options */
int rollback_flag;       /* immediate execute=1, else = 0 */
int with_assoc_flag = 0;
void *db_conn = NULL;
uint32_t my_uid = 0;
List g_qos_list = NULL;
List g_res_list = NULL;
List g_tres_list = NULL;
const char *mime_type = NULL; /* mimetype if we are using data_parser */

/* by default, normalize all usernames to lower case */
bool user_case_norm = true;
bool tree_display = 0;
bool have_db_conn = false;

static void	_add_it(int argc, char **argv);
static void	_archive_it(int argc, char **argv);
static void	_clear_it(int argc, char **argv);
static void	_show_it(int argc, char **argv);
static void	_modify_it(int argc, char **argv);
static void	_delete_it(int argc, char **argv);
static int	_get_command(int *argc, char **argv);
static void     _print_version(void);
static int	_process_command(int argc, char **argv);
static void	_usage(void);

decl_static_data(usage_txt);

int main(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS, opt_char;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;
	int local_exit_code = 0;
	int option_index;
	uint16_t persist_conn_flags = 0;

	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"help",     0, 0, 'h'},
		{"usage",    0, 0, 'h'},
		{"immediate",0, 0, 'i'},
		{"noheader",0, 0, 'n'},
		{"oneliner", 0, 0, 'o'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"quiet",    0, 0, 'Q'},
		{"readonly", 0, 0, 'r'},
		{"associations", 0, 0, 's'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{"json", 0, 0, OPT_LONG_JSON},
		{"yaml", 0, 0, OPT_LONG_YAML},
		{NULL,       0, 0, 0}
	};

	command_name      = argv[0];
	rollback_flag     = 1;
	exit_code         = 0;
	exit_flag         = 0;
	quiet_flag        = 0;
	readonly_flag     = 0;
	verbosity         = 0;
	slurm_init(NULL);
	log_init("sacctmgr", opts, SYSLOG_FACILITY_DAEMON, NULL);

	while((opt_char = getopt_long(argc, argv, "hionpPQrsvV",
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
		case (int)'Q':
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
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_JSON :
			mime_type = MIME_TYPE_JSON;
			if (data_init())
				fatal("data_init() failed");
			if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
				fatal("JSON plugin load failure");
			break;
		case OPT_LONG_YAML :
			mime_type = MIME_TYPE_YAML;
			if (data_init())
				fatal("data_init() failed");
			if (serializer_g_init(MIME_TYPE_YAML_PLUGIN, NULL))
				fatal("YAML plugin load failure");
			break;
		default:
			exit_code = 1;
			fprintf(stderr, "getopt error, returned %c\n",
				opt_char);
			exit(exit_code);
		}
	}

	if (verbosity) {
		opts.stderr_level += verbosity;
		opts.prefix_level = 1;
		log_alter(opts, 0, NULL);
	}

	/* Check to see if we are running a supported accounting plugin */
	if (!slurm_with_slurmdbd()) {
		fprintf(stderr,
		        "You are not running a supported accounting_storage plugin\n"
		        "Only 'accounting_storage/slurmdbd' is supported.\n");
		exit(1);
	}

	errno = 0;
	db_conn = slurmdb_connection_get(&persist_conn_flags);

	if (!errno)
		have_db_conn = true;

	my_uid = getuid();

	if (persist_conn_flags & PERSIST_FLAG_P_USER_CASE)
		user_case_norm = false;


	/* We are only running a single command and exiting */
	if (optind < argc)
		error_code = _process_command(argc - optind, argv + optind);
	else {
		/* We are running interactively multiple commands */
		int input_field_count = 0;
		char **input_fields = xcalloc(MAX_INPUT_FIELDS, sizeof(char *));
		while (error_code == SLURM_SUCCESS) {
			error_code = _get_command(
				&input_field_count, input_fields);
			if (error_code || exit_flag)
				break;

			error_code = _process_command(
				input_field_count, input_fields);
			/* This is here so if someone made a mistake we allow
			 * them to fix it and let the process happen since there
			 * are checks for global exit_code we need to reset it.
			 */
			if (exit_code) {
				local_exit_code = exit_code;
				exit_code = 0;
			}
			if (exit_flag)
				break;
		}
		xfree(input_fields);
	}

	/* readline library writes \n when echoes the input string, it does
	 * not when it sees the EOF, so in that case we have to print it to
	 * align the terminal prompt.
	 */
	if (exit_flag == 2)
		putchar('\n');
	if (local_exit_code)
		exit_code = local_exit_code;
	slurmdb_connection_close(&db_conn);
	slurm_acct_storage_fini();
	FREE_NULL_LIST(g_qos_list);
	FREE_NULL_LIST(g_res_list);
	FREE_NULL_LIST(g_tres_list);

	exit(exit_code);
}

#if !HAVE_READLINE
/*
 * Alternative to readline if readline is not available
 */
static char *_getline(const char *prompt)
{
	char buf[4096];
	char *line;
	int len;

	printf("%s", prompt);

	/* Set "line" here to avoid a warning, discard later */
	line = fgets(buf, 4096, stdin);
	if (line == NULL)
		return NULL;
	len = strlen(buf);
	if ((len == 0) || (len >= 4096))
		return NULL;
	if (buf[len-1] == '\n')
		buf[len-1] = '\0';
	else
		len++;
	line = malloc(len);
	if (!line)
		return NULL;
	strlcpy(line, buf, len);
	return line;
}
#endif

/*
 * _get_command - get a command from the user
 * OUT argc - location to store count of arguments
 * OUT argv - location to store the argument list
 */
static int _get_command (int *argc, char **argv)
{
	char *in_line;
	static char *last_in_line = NULL;
	int i, in_line_size;
	static int last_in_line_size = 0;

	*argc = 0;

#if HAVE_READLINE
	in_line = readline ("sacctmgr: ");
#else
	in_line = _getline("sacctmgr: ");
#endif
	if (in_line == NULL) {
		exit_flag = 2;
		return 0;
	} else if (xstrncmp (in_line, "#", 1) == 0) {
		free (in_line);
		return 0;
	} else if (xstrcmp (in_line, "!!") == 0) {
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
				 command_name, MAX_INPUT_FIELDS-1);
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
	print_slurm_version();
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
static int _process_command (int argc, char **argv)
{
	int command_len = 0, rc;

	if (argc < 1) {
		exit_code = 1;
		if (quiet_flag == -1)
			fprintf(stderr, "no input");
		return 0;
	}

	command_len = strlen(argv[0]);

	if (xstrncasecmp (argv[0], "associations",
			 MAX(command_len, 3)) == 0) {
		with_assoc_flag = 1;
	} else if (xstrncasecmp(argv[0], "dump", MAX(command_len, 3)) == 0) {
		sacctmgr_dump_cluster((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "help", MAX(command_len, 2)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_usage ();
	} else if (xstrncasecmp(argv[0], "load", MAX(command_len, 2)) == 0) {
		load_sacctmgr_cfg_file((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "oneliner",
				MAX(command_len, 1)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		one_liner = 1;
	} else if (xstrncasecmp(argv[0], "quiet", MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		quiet_flag = 1;
	} else if ((xstrncasecmp(argv[0], "exit", MAX(command_len, 4)) == 0) ||
		   (xstrncasecmp(argv[0], "\\q", MAX(command_len, 2)) == 0) ||
		   (xstrncasecmp(argv[0], "quit", MAX(command_len, 4)) == 0)) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		exit_flag = 1;
	} else if ((xstrncasecmp(argv[0], "add", MAX(command_len, 3)) == 0) ||
		   (xstrncasecmp(argv[0], "create",
				 MAX(command_len, 3)) == 0)) {
		_add_it((argc - 1), &argv[1]);
	} else if ((xstrncasecmp(argv[0], "archive",
				 MAX(command_len, 3)) == 0)) {
		_archive_it((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "clear", MAX(command_len, 3)) == 0) {
		_clear_it((argc - 1), &argv[1]);
	} else if ((xstrncasecmp(argv[0], "show", MAX(command_len, 3)) == 0) ||
		   (xstrncasecmp(argv[0], "list", MAX(command_len, 3)) == 0)) {
		_show_it((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "modify", MAX(command_len, 1))
		   || !xstrncasecmp(argv[0], "update", MAX(command_len, 1))) {
		_modify_it((argc - 1), &argv[1]);
	} else if ((xstrncasecmp(argv[0], "delete",
				 MAX(command_len, 3)) == 0) ||
		   (xstrncasecmp(argv[0], "remove",
				 MAX(command_len, 3)) == 0)) {
		_delete_it((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "verbose", MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}
		quiet_flag = -1;
	} else if (xstrncasecmp(argv[0], "readonly",
				MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}
		readonly_flag = 1;
	} else if (xstrncasecmp(argv[0], "reconfigure",
				MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}

		slurmdb_reconfig(db_conn);
	} else if (xstrncasecmp(argv[0], "rollup", MAX(command_len, 2)) == 0) {
		time_t my_start = 0;
		time_t my_end = 0;
		uint16_t archive_data = 0;
		if (argc > 4) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}

		if (argc > 1)
			my_start = parse_time(argv[1], 1);
		if (argc > 2)
			my_end = parse_time(argv[2], 1);
		if (argc > 3)
			archive_data = atoi(argv[3]);
		if (slurmdb_usage_roll(db_conn, my_start,
				       my_end, archive_data, NULL)
		   == SLURM_SUCCESS) {
			if (commit_check("Would you like to commit rollup?")) {
				slurmdb_connection_commit(db_conn, 1);
			} else {
				printf(" Rollup Discarded\n");
				slurmdb_connection_commit(db_conn, 0);
			}
		}
	} else if (xstrncasecmp(argv[0], "shutdown",
				MAX(command_len, 4)) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}

		rc = slurmdb_shutdown(db_conn);
		if (rc != SLURM_SUCCESS) {
			fprintf(stderr, " Problem shutting down server: %s\n",
				slurm_strerror(rc));
			exit_code = 1;
		}
	} else if (xstrncasecmp(argv[0], "version", MAX(command_len, 4)) == 0) {
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
static void _add_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!have_db_conn) {
		exit_code = 1;
		return;
	}

	if (readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;
	}

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);
	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	/* First identify the entity to add */
	if (!xstrncasecmp(argv[0], "Account", MAX(command_len, 1))
	    || !xstrncasecmp(argv[0], "Acct", MAX(command_len, 4))) {
		error_code = sacctmgr_add_account((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Cluster", MAX(command_len, 2))) {
		error_code = sacctmgr_add_cluster((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Coordinator", MAX(command_len, 2))) {
		error_code = sacctmgr_add_coord((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Federation", MAX(command_len, 1))) {
		error_code = sacctmgr_add_federation((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "QOS", MAX(command_len, 1))) {
		error_code = sacctmgr_add_qos((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Resource", MAX(command_len, 1))) {
		error_code = sacctmgr_add_res((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "User", MAX(command_len, 1))) {
		error_code = sacctmgr_add_user((argc - 1), &argv[1]);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in add command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"Account\", \"Cluster\", \"Coordinator\", ");
		fprintf(stderr, "\"Federation\", \"QOS\", \"Resource\", ");
		fprintf(stderr, "or \"User\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
		exit_code = 1;
	}
}

/*
 * _archive_it - archive the entity per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _archive_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!have_db_conn) {
		exit_code = 1;
		return;
	}

	if (readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;
	}

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);
	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	/* First identify the entity to add */
	if (xstrncasecmp(argv[0], "dump", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_archive_dump((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "load", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_archive_load((argc - 1), &argv[1]);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in archive command\n");
		fprintf(stderr, "Input line must include, ");
		fprintf(stderr, "\"Dump\", or \"load\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
		exit_code = 1;
	}
}

/*
 * _clear_it - Clear the slurm configuration per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _clear_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!have_db_conn) {
		exit_code = 1;
		return;
	}

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);

	/* First identify the entity to list */
	if (!xstrncasecmp(argv[0], "Stats", MAX(command_len, 1))) {
		error_code = slurmdb_clear_stats(db_conn);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in list command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"Stats\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
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
static void _show_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);
	if (!have_db_conn &&
	    xstrncasecmp(argv[0], "Configuration",
			 MAX(command_len, 2))) {
		exit_code = 1;
		return;
	}


	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	/* First identify the entity to list */
	if (xstrncasecmp(argv[0], "Accounts", MAX(command_len, 2)) == 0
	    || !xstrncasecmp(argv[0], "Acct", MAX(command_len, 4))) {
		error_code = sacctmgr_list_account((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Associations",
				MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_list_assoc((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Clusters",
				MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_list_cluster((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Configuration",
				MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_list_config();
	} else if (xstrncasecmp(argv[0], "Events",
				MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_event((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Federation",
				MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_federation((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Problems",
				MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_problem((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "RunawayJobs", MAX(command_len, 2)) ||
		   !xstrncasecmp(argv[0], "OrphanJobs", MAX(command_len, 1)) ||
		   !xstrncasecmp(argv[0], "LostJobs", MAX(command_len, 1))) {
		error_code = sacctmgr_list_runaway_jobs((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "QOS", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_qos((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Resource", MAX(command_len, 4))) {
		error_code = sacctmgr_list_res((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Reservations", MAX(command_len, 4))||
		   !xstrncasecmp(argv[0], "Resv", MAX(command_len, 4))) {
		error_code = sacctmgr_list_reservation((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Stats", MAX(command_len, 1))) {
		error_code = sacctmgr_list_stats((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(argv[0], "Transactions", MAX(command_len, 1))
		   || !xstrncasecmp(argv[0], "Txn", MAX(command_len, 1))) {
		error_code = sacctmgr_list_txn((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Users", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_user((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "WCKeys", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_list_wckey((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "tres", MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_list_tres(argc - 1, &argv[1]);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in list command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"Account\", \"Association\", "
			"\"Cluster\", \"Configuration\",\n\"Event\", "
			"\"Federation\", \"Problem\", \"QOS\", \"Resource\", "
			"\"Reservation\",\n\"RunAwayJobs\", \"Stats\", "
			"\"Transaction\", \"TRES\", \"User\", or \"WCKey\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
		exit_code = 1;
	}
}


/*
 * _modify_it - modify the slurm configuration per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _modify_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!have_db_conn) {
		exit_code = 1;
		return;
	}

	if (readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;
	}

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);
	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	/* First identify the entity to modify */
	if (xstrncasecmp(argv[0], "Accounts", MAX(command_len, 1)) == 0
	    || !xstrncasecmp(argv[0], "Acct", MAX(command_len, 4))) {
		error_code = sacctmgr_modify_account((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Clusters",
				MAX(command_len, 5)) == 0) {
		error_code = sacctmgr_modify_cluster((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Federation",
			       MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_modify_federation((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Job", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_modify_job((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "QOSs", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_modify_qos((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Resource", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_modify_res((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Users", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_modify_user((argc - 1), &argv[1]);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in modify command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"Account\", \"Cluster\", \"Federation\", "
			"\"Job\", \"QOS\", \"Resource\" or \"User\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
		exit_code = 1;
	}
}

/*
 * _delete_it - delete the slurm configuration per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _delete_it(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	int command_len = 0;

	if (!have_db_conn) {
		exit_code = 1;
		return;
	}

	if (readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;
	}

	if (!argv[0])
		goto helpme;

	command_len = strlen(argv[0]);
	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	/* First identify the entity to delete */
	if (xstrncasecmp(argv[0], "Accounts", MAX(command_len, 1)) == 0
	    || !xstrncasecmp(argv[0], "Acct", MAX(command_len, 4))) {
		error_code = sacctmgr_delete_account((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Clusters",
				MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_delete_cluster((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Coordinators",
				MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_delete_coord((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Federations",
				MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_delete_federation((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "QOS", MAX(command_len, 2)) == 0) {
		error_code = sacctmgr_delete_qos((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Resource", MAX(command_len, 1)) == 0){
		error_code = sacctmgr_delete_res((argc - 1), &argv[1]);
	} else if (xstrncasecmp(argv[0], "Users", MAX(command_len, 1)) == 0) {
		error_code = sacctmgr_delete_user((argc - 1), &argv[1]);
	} else {
	helpme:
		exit_code = 1;
		fprintf(stderr, "No valid entity in delete command\n");
		fprintf(stderr, "Input line must include ");
		fprintf(stderr, "\"Account\", \"Cluster\", \"Coordinator\", ");
		fprintf(stderr, "\"Federation\", \"QOS\", \"Resource\", or ");
		fprintf(stderr, "\"User\"\n");
	}

	if (error_code != SLURM_SUCCESS) {
		exit_code = 1;
	}
}

static void _usage(void)
{
        char *txt;
        static_ref_to_cstring(txt, usage_txt);
        printf("%s\n", txt);
        xfree(txt);
}
