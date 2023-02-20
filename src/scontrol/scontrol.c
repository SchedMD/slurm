/*****************************************************************************\
 *  scontrol.c - administration tool for slurm.
 *	provides interface to read, write, update, and configurations.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "scontrol.h"
#include "src/common/data.h"
#include "src/common/proc_args.h"
#include "src/common/ref.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/serializer.h"

#define OPT_LONG_HIDE    0x102
#define OPT_LONG_LOCAL   0x103
#define OPT_LONG_SIBLING 0x104
#define OPT_LONG_FEDR    0x105
#define OPT_LONG_AUTOCOMP 0x106
#define OPT_LONG_JSON 0x107
#define OPT_LONG_YAML 0x108

/* Global externs from scontrol.h */
char *command_name;
List clusters = NULL;
int all_flag = 0;	/* display even hidden partitions */
int detail_flag = 0;	/* display additional details */
int future_flag = 0;	/* display future nodes */
int exit_code = 0;	/* scontrol's exit code, =1 on any error at any time */
int exit_flag = 0;	/* program to terminate if =1 */
int federation_flag = 0;/* show federated jobs */
int local_flag = 0;     /* show only local jobs -- not remote remote sib jobs */
int one_liner = 0;	/* one record per line if =1 */
int quiet_flag = 0;	/* quiet=1, verbose=-1, normal=0 */
int sibling_flag = 0;   /* show sibling jobs (if any fed job). */
int verbosity = 0;	/* count of "-v" options */
uint32_t cluster_flags; /* what type of cluster are we talking to */
uint32_t euid = SLURM_AUTH_NOBODY; /* proxy request as user */
const char *mime_type = NULL; /* mimetype if we are using data_parser */

front_end_info_msg_t *old_front_end_info_ptr = NULL;
job_info_msg_t *old_job_info_ptr = NULL;
node_info_msg_t *old_node_info_ptr = NULL;
partition_info_msg_t *old_part_info_ptr = NULL;
reserve_info_msg_t *old_res_info_ptr = NULL;
slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr = NULL;

static void	_create_it(int argc, char **argv);
static void	_delete_it(int argc, char **argv);
static void     _show_it(int argc, char **argv);
static void	_fetch_token(int argc, char **argv);
static int _get_command(int *argc, char **argv);
static void _print_config(char *config_param, int argc, char **argv);
static void     _print_daemons(void);
static void     _print_aliases(char* node_hostname);
static void _print_ping(int argc, char **argv);
static void	_print_slurmd(char *hostlist);
static void     _print_version(void);
static int	_process_command(int argc, char **argv);
static void	_update_it(int argc, char **argv);
static int	_update_slurmctld_debug(char *val);
static void	_usage(void);
static void	_write_config(char *file_name);

decl_static_data(usage_txt);

int main(int argc, char **argv)
{
	int error_code = SLURM_SUCCESS, opt_char;
	char *env_val;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	int option_index;
	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"all",      0, 0, 'a'},
		{"cluster",  1, 0, 'M'},
		{"clusters", 1, 0, 'M'},
		{"details",  0, 0, 'd'},
		{"federation",0, 0, OPT_LONG_FEDR},
		{"future",   0, 0, 'F'},
		{"help",     0, 0, 'h'},
		{"hide",     0, 0, OPT_LONG_HIDE},
		{"json", 0, 0, OPT_LONG_JSON},
		{"local",    0, 0, OPT_LONG_LOCAL},
		{"oneliner", 0, 0, 'o'},
		{"quiet",    0, 0, 'Q'},
		{"sibling",  0, 0, OPT_LONG_SIBLING},
		{"uid",	     1, 0, 'u'},
		{"usage",    0, 0, 'h'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{"yaml", 0, 0, OPT_LONG_YAML},
		{NULL,       0, 0, 0}
	};

	command_name = argv[0];
	slurm_init(NULL);
	log_init("scontrol", opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (xstrstr(slurm_conf.fed_params, "fed_display"))
		federation_flag = true;

	if (getenv ("SCONTROL_ALL"))
		all_flag = 1;
	if ((env_val = getenv("SLURM_CLUSTERS"))) {
		if (!(clusters = slurmdb_get_info_cluster(env_val))) {
			print_db_notok(env_val, 1);
			exit(1);
		}
		working_cluster_rec = list_peek(clusters);
		local_flag = 1;
	}
	if (getenv("SCONTROL_FEDERATION"))
		federation_flag = 1;
	if (getenv("SCONTROL_FUTURE"))
		future_flag = 1;
	if (getenv("SCONTROL_LOCAL"))
		local_flag = 1;
	if (getenv("SCONTROL_SIB") || getenv("SCONTROL_SIBLING"))
		sibling_flag = 1;

	while (1) {
		if ((optind < argc) &&
		    !xstrncasecmp(argv[optind], "setdebugflags", 8))
			break;	/* avoid parsing "-<flagname>" as option */
		if ((opt_char = getopt_long(argc, argv, "adhM:FoQu:vV",
					    long_options, &option_index)) == -1)
			break;
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"scontrol --help\" for "
				"more information\n");
			exit(1);
			break;
		case (int)'a':
			all_flag = 1;
			break;
		case (int)'d':
			detail_flag = 1;
			break;
		case (int)'F':
			future_flag = 1;
			break;
		case (int)'h':
			_usage ();
			exit(exit_code);
			break;
		case OPT_LONG_FEDR:
			federation_flag = 1;
			break;
		case OPT_LONG_HIDE:
			all_flag = 0;
			detail_flag = 0;
			break;
		case OPT_LONG_LOCAL:
			local_flag = 1;
			break;
		case (int)'M':
			if (clusters) {
				FREE_NULL_LIST(clusters);
				working_cluster_rec = NULL;
			}
			if (!(clusters = slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(clusters);
			local_flag = 1;
			break;
		case (int)'o':
			one_liner = 1;
			break;
		case (int)'Q':
			quiet_flag = 1;
			break;
		case OPT_LONG_SIBLING:
			sibling_flag = 1;
			break;
		case (int)'u':
			if (uid_from_string(optarg, &euid) < 0) {
				error("--uid=\"%s\" invalid", optarg);
				exit(exit_code);
			}
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

	if (clusters && (list_count(clusters) > 1))
		fatal("Only one cluster can be used at a time with scontrol");
	cluster_flags = slurmdb_setup_cluster_flags();

	if (verbosity) {
		opts.stderr_level += verbosity;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

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
			if (error_code || exit_flag) {	/* EOF */
				putchar('\n');
				break;
			}

			error_code = _process_command(
				input_field_count, input_fields);
			if (exit_flag)
				break;
		}
		xfree(input_fields);
	}

#ifdef MEMORY_LEAK_DEBUG
	FREE_NULL_LIST(clusters);
	slurm_conf_destroy();
	serializer_g_fini();
	data_fini();
#endif /* MEMORY_LEAK_DEBUG */

	exit(exit_code);
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
	in_line = readline ("scontrol: ");
#else
	in_line = _getline("scontrol: ");
#endif
	if (in_line == NULL) {
		exit_flag = true;
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
				 command_name, MAX_INPUT_FIELDS - 1);
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

/*
 * _write_config - write the configuration parameters and values to a file.
 */
static void _write_config(char *file_name)
{
	int error_code;
	node_info_msg_t *node_info_ptr = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	/* slurm config loading code copied from _print_config() */

	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf (
				old_slurm_ctl_conf_ptr->last_update,
				&slurm_ctl_conf_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		} else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1) {
				printf ("slurm_load_ctl_conf no change "
					"in data\n");
			}
		}
	} else {
		error_code = slurm_load_ctl_conf ((time_t) NULL,
						  &slurm_ctl_conf_ptr);
	}

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_ctl_conf error");
	} else
		old_slurm_ctl_conf_ptr = slurm_ctl_conf_ptr;


	if (error_code == SLURM_SUCCESS) {
		int save_all_flag = all_flag;
		all_flag = 1;

		if (file_name)
			setenv("SLURM_CONF_OUT", file_name, 1);

		/* now gather node info */
		error_code = scontrol_load_nodes(&node_info_ptr, SHOW_ALL);

		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_load_node error");
			all_flag = save_all_flag;
			return;
		}

		/* now gather partition info */
		error_code = scontrol_load_partitions(&part_info_ptr);
		all_flag = save_all_flag;
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_load_partitions error");
			return;
		}

		/* send the info off to be written */
		slurm_write_ctl_conf (slurm_ctl_conf_ptr,
				      node_info_ptr,
				      part_info_ptr);
	}
}

/*
 * _print_config - print the specified configuration parameter and value
 * IN config_param - NULL to print all parameters and values
 */
static void _print_config(char *config_param, int argc, char **argv)
{
	int error_code;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf (
				old_slurm_ctl_conf_ptr->last_update,
				&slurm_ctl_conf_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1) {
				printf ("slurm_load_ctl_conf no change "
					"in data\n");
			}
		}
	}
	else
		error_code = slurm_load_ctl_conf ((time_t) NULL,
						  &slurm_ctl_conf_ptr);

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_ctl_conf error");
	}
	else
		old_slurm_ctl_conf_ptr = slurm_ctl_conf_ptr;


	if (error_code == SLURM_SUCCESS) {
		slurm_print_ctl_conf (stdout, slurm_ctl_conf_ptr) ;
		fprintf(stdout, "\n");
	}
	if (slurm_ctl_conf_ptr)
		_print_ping(argc, argv);
}

/* Print slurmd status on localhost.
 * Parse hostlist in the future */
static void _print_slurmd(char *hostlist)
{
	slurmd_status_t *slurmd_status;

	if (slurm_load_slurmd_status(&slurmd_status)) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_slurmd_status");
	} else {
		slurm_print_slurmd_status(stdout, slurmd_status);
		slurm_free_slurmd_status(slurmd_status);
	}
}

/* Print state of controllers only */
static void _print_ping(int argc, char **argv)
{
	static const char *state[2] = { "DOWN", "UP" };
	char mode[64];
	bool down_msg = false;
	controller_ping_t *pings = ping_all_controllers();

	if (mime_type) {
		exit_code = DATA_DUMP_CLI(CONTROLLER_PING_ARRAY, pings, "pings",
					  argc, argv, NULL, mime_type);
		xfree(pings);
		return;
	}

	exit_code = 1;
	for (controller_ping_t *ping = pings; ping && ping->hostname; ping++) {
		if (ping->pinged)
			exit_code = SLURM_SUCCESS;
		else
			down_msg = true;

		if (ping->offset == 0)
			snprintf(mode, sizeof(mode), "primary");
		else if ((ping->offset == 1) && (slurm_conf.control_cnt == 2))
			snprintf(mode, sizeof(mode), "backup");
		else
			snprintf(mode, sizeof(mode), "backup%d", ping->offset);
		fprintf(stdout, "Slurmctld(%s) at %s is %s\n",
			mode, ping->hostname, state[ping->pinged]);
	}
	xfree(pings);

	if (down_msg && (getuid() == 0)) {
		fprintf(stdout, "*****************************************\n");
		fprintf(stdout, "** RESTORE SLURMCTLD DAEMON TO SERVICE **\n");
		fprintf(stdout, "*****************************************\n");
	}
}

/*
 * _print_daemons - report what daemons should be running on this node
 */
static void
_print_daemons (void)
{
	slurm_ctl_conf_info_msg_t *conf;
	char node_name_short[HOST_NAME_MAX];
	char node_name_long[HOST_NAME_MAX];
	char *c, *n, *token, *save_ptr = NULL;
	int actld = 0, ctld = 0, d = 0, i;
	char *daemon_list = NULL;

	conf = slurm_conf_lock();

	gethostname_short(node_name_short, HOST_NAME_MAX);
	gethostname(node_name_long, HOST_NAME_MAX);
	for (i = 0; i < conf->control_cnt; i++) {
		if (!conf->control_machine[i])
			break;
		actld = 1;
		c = xstrdup(conf->control_machine[i]);
		token = strtok_r(c, ",", &save_ptr);
		while (token) {
			if (!xstrcmp(token, node_name_short) ||
			    !xstrcmp(token, node_name_long)  ||
			    !xstrcasecmp(token, "localhost")) {
				ctld = 1;
				break;
			}
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(c);
		if (ctld)
			break;
	}
	slurm_conf_unlock();

	if ((n = slurm_conf_get_nodename(node_name_short))) {
		d = 1;
		xfree(n);
	} else if ((n = slurm_conf_get_aliased_nodename())) {
		d = 1;
		xfree(n);
	} else if ((n = slurm_conf_get_nodename("localhost"))) {
		d = 1;
		xfree(n);
	}

	if (actld && ctld)
		xstrcat(daemon_list, "slurmctld ");
	if (actld && d)
		xstrcat(daemon_list, "slurmd");
	fprintf (stdout, "%s\n", daemon_list) ;
	xfree(daemon_list);
}

/*
 * _print_aliases - report which aliases should be running on this node
 */
static void
_print_aliases (char* node_hostname)
{
	char me[HOST_NAME_MAX], *n = NULL, *a = NULL;
	char *s;

	if (!node_hostname) {
		gethostname_short(me, HOST_NAME_MAX);
		s = me;
	} else
		s = node_hostname;

	if (!(n = slurm_conf_get_aliases(s)) && (s == me)) {

		if (!(a = slurm_conf_get_aliased_nodename()))
			a = slurm_conf_get_nodename("localhost");

		if (a) {
			n = slurm_conf_get_aliases(a);
			xfree(a);
		}
	}

	if (n) {
		fprintf(stdout, "%s\n", n);
		xfree(n);
	}

}

void _process_reboot_command(const char *tag, int argc, char **argv)
{
	int error_code = SLURM_SUCCESS;
	bool asap = false;
	char *reason = NULL;
	uint32_t next_state = NO_VAL;
	int argc_offset = 1;

	if (argc > 1) {
		int i = 1;
		for (; i <= 3 && i < argc; i++) {
			if (!strcasecmp(argv[i], "ASAP")) {
				asap = true;
				argc_offset++;
			} else if (!xstrncasecmp(argv[i], "Reason=",
						 strlen("Reason="))) {
				char *tmp_ptr = strchr(argv[i], '=');
				if (!tmp_ptr || !*(tmp_ptr + 1)) {
					exit_code = 1;
					if (!quiet_flag)
						fprintf(stderr, "missing reason\n");
					xfree(reason);
					return;
				}

				xfree(reason);
				reason = xstrdup(tmp_ptr+1);
				argc_offset++;
			} else if (!xstrncasecmp(argv[i], "nextstate=",
						 strlen("nextstate="))) {
				int state_str_len;
				char* state_str;
				char *tmp_ptr = strchr(argv[i], '=');
				if (!tmp_ptr || !*(tmp_ptr + 1)) {
					exit_code = 1;
					if (!quiet_flag)
						fprintf(stderr, "missing state\n");
					xfree(reason);
					return;
				}

				state_str = xstrdup(tmp_ptr+1);
				state_str_len = strlen(state_str);
				argc_offset++;

				if (!xstrncasecmp(state_str, "DOWN",
						  MAX(state_str_len, 1)))
					next_state = NODE_STATE_DOWN;
				else if (!xstrncasecmp(state_str, "RESUME",
						       MAX(state_str_len, 1)))
					next_state = NODE_RESUME;
				else {
					exit_code = 1;
					if (!quiet_flag) {
						fprintf(stderr, "Invalid state: %s\n",
							state_str);
						fprintf(stderr, "Valid states: DOWN, RESUME\n");
					}
					xfree(reason);
					xfree(state_str);
					return;
				}
				xfree(state_str);
			}
		}
	}
	if ((argc - argc_offset) > 1) {
		exit_code = 1;
		fprintf (stderr,
			 "too many arguments for keyword:%s\n",
			 tag);
	} else if ((argc - argc_offset) < 1) {
		exit_code = 1;
		fprintf(stderr, "Missing node list. Specify ALL|<NodeList>\n");
	} else {
		error_code = scontrol_reboot_nodes(argv[argc_offset], asap,
						   next_state, reason);
	}

	xfree(reason);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("scontrol_reboot_nodes error");
	}
}

static void _setdebug(int argc, char **argv)
{
	int level = -1, index = 0;
	char *endptr;
	char *levels[] = {
		"quiet", "fatal", "error", "info", "verbose", "debug",
		"debug2", "debug3", "debug4", "debug5", NULL
	};

	if (argc > 3) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr, "too many arguments for keyword:%s\n",
				argv[0]);
		return;
	} else if (argc < 2) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr, "too few arguments for keyword:%s\n",
				argv[0]);
		return;
	}

	while (levels[index]) {
		if (!xstrcasecmp(argv[1], levels[index])) {
			level = index;
			break;
		}
		index ++;
	}

	if (level == -1) {
		/* effective levels: 0 - 9 */
		level = (int) strtoul(argv[1], &endptr, 10);
		if (*endptr != '\0' || level > 9) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, "invalid debug level: %s\n",
					argv[1]);
			return;
		}
	}

	if (argc == 2) {
		if (slurm_set_debug_level(level)) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror("slurm_set_debug_level error");
		}
	} else if (argc == 3) {
		/*
		 * scontrol setdebug <level> nodes=<list of nodes>
		 */
		char *nodes;

		if (xstrncasecmp(argv[2], "Nodes=", 6)) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, "Invalid option: %s\n",
					argv[1]);
			return;
		}

		if ((nodes = strchr(argv[2], '=')))
			nodes++;

		if (slurm_set_slurmd_debug_level(nodes, level)) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, "Failed to change debug level on one or more nodes.\n");
		}
	}
}

static void _setdebugflags(int argc, char **argv)
{
	char *tag = argv[0];
	int i, mode = 0;
	uint64_t debug_flags_plus = 0;
	uint64_t debug_flags_minus = 0, flags;

	if (argc < 2) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr, "too few arguments for keyword:%s\n",
				tag);
		return;
	}

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '+')
			mode = 1;
		else if (argv[i][0] == '-')
			mode = -1;
		else {
			mode = 0;
			break;
		}

		if (debug_str2flags(&argv[i][1], &flags) != SLURM_SUCCESS)
			break;
		if (mode == 1)
			debug_flags_plus |= flags;
		else
			debug_flags_minus |= flags;
	}

	if ((i == (argc - 1)) && (!xstrncasecmp(argv[i], "node=", 5) ||
				  !xstrncasecmp(argv[i], "nodes=", 6))) {
		char *nodes = strchr(argv[i], '=') + 1;

		if (slurm_set_slurmd_debug_flags(nodes, debug_flags_plus,
						 debug_flags_minus)) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"Failed to set DebugFlags on one or more nodes.\n");
		}
	} else if (i < argc) {
		exit_code = 1;
		if (quiet_flag != 1) {
			fprintf(stderr, "invalid debug flag: %s\n", argv[i]);
		}
		if ((quiet_flag != 1) && (mode == 0)) {
			fprintf(stderr, "Usage: setdebugflags {+|-}NAME [{+|-}NAME] [nodes=<NODES>]\n");
		}
	} else {
		if (slurm_set_debugflags(debug_flags_plus, debug_flags_minus)) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror("slurm_set_debug_flags error");
		}
	}
}

static void _fetch_token(int argc, char **argv)
{
	char *username = NULL, *token;
	int lifespan = 0;

	for (int i = 1; i < argc; i++) {
		if (!xstrncasecmp("lifespan=", argv[i], 9)) {
			char *val = argv[i] + 9;
			lifespan = parse_int("lifespan", val, true);
		} else if (!xstrncasecmp("username=", argv[i], 9))
			username = argv[i] + 9;
		else {
			fprintf(stderr, "Invalid option: `%s`\n", argv[i]);
			exit_code = 1;
			return;
		}
	}

	if (!(token = slurm_fetch_token(username, lifespan))) {
		/*
		 * Error message printed slurm_fetch_token().
		 * No need for error message here.
		 */
		exit_code = 1;
		return;
	}

	printf("SLURM_JWT=%s\n", token);

	xfree(token);
}

/*
 * _process_command - process the user's command
 * IN argc - count of arguments
 * IN argv - the arguments
 * RET 0 or errno (only for errors fatal to scontrol)
 */
static int _process_command (int argc, char **argv)
{
	int error_code = 0;
	char *tag = argv[0];
	int tag_len = 0;
	int i;

	if (argc < 1) {
		exit_code = 1;
		if (quiet_flag == -1)
			fprintf(stderr, "no input");
		return 0;
	} else if (tag)
		tag_len = strlen(tag);
	else {
		if (quiet_flag == -1)
			fprintf(stderr, "input problem");
		return 0;
	}

	if (!xstrncasecmp(tag, "abort", MAX(tag_len, 5))) {
		/* require full command name */
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		error_code = slurm_shutdown (1);
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_shutdown error");
		}
	} else if (!xstrncasecmp(tag, "all", MAX(tag_len, 2))) {
		all_flag = 1;
	} else if (!xstrncasecmp(tag, "cancel_reboot", MAX(tag_len, 3))) {
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else if (argc < 2) {
			exit_code = 1;
			fprintf (stderr,
				 "missing argument for keyword:%s\n",
				 tag);
		} else
			scontrol_cancel_reboot(argv[1]);
	} else if (!xstrncasecmp(tag, "completing", MAX(tag_len, 2))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else
			scontrol_print_completing();
	} else if (!xstrncasecmp(tag, "cluster", MAX(tag_len, 2))) {
		if (clusters) {
			FREE_NULL_LIST(clusters);
			working_cluster_rec = NULL;
		}
		if (argc >= 2) {
			if (!(clusters = slurmdb_get_info_cluster(argv[1]))) {
				print_db_notok(argv[1], 0);
				exit(1);
			}
			working_cluster_rec = list_peek(clusters);
			if (list_count(clusters) > 1) {
				fatal("Only one cluster can be used at a time "
				      "with scontrol");
			}
		}
		cluster_flags = slurmdb_setup_cluster_flags();
		slurm_free_front_end_info_msg(old_front_end_info_ptr);
		old_front_end_info_ptr = NULL;
		slurm_free_job_info_msg(old_job_info_ptr);
		old_job_info_ptr = NULL;
		slurm_free_node_info_msg(old_node_info_ptr);
		old_node_info_ptr = NULL;
		slurm_free_partition_info_msg(old_part_info_ptr);
		old_part_info_ptr = NULL;
		slurm_free_reservation_info_msg(old_res_info_ptr);
		old_res_info_ptr = NULL;
		slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		old_slurm_ctl_conf_ptr = NULL;
		/* if (old_job_info_ptr) */
		/* 	old_job_info_ptr->last_update = 0; */
		/* if (old_node_info_ptr) */
		/* 	old_node_info_ptr->last_update = 0; */
		/* if (old_part_info_ptr) */
		/* 	old_part_info_ptr->last_update = 0; */
		/* if (old_res_info_ptr) */
		/* 	old_res_info_ptr->last_update = 0; */
		/* if (old_slurm_ctl_conf_ptr) */
		/* 	old_slurm_ctl_conf_ptr->last_update = 0; */
	} else if (!xstrncasecmp(tag, "create", MAX(tag_len, 2))) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 tag);
			return 0;
		}
		_create_it ((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(tag, "details", MAX(tag_len, 1))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
			return 0;
		}
		detail_flag = 1;
	} else if (!xstrncasecmp(tag, "errnumstr", MAX(tag_len, 2)) ||
		   !xstrncasecmp(tag, "errnostr", MAX(tag_len, 2))) {
		if (argc != 2) {
			exit_code = 1;
			fprintf (stderr,
				 "one arguments required for keyword:%s\n",
				 tag);
		} else {
			char *end_ptr;
			int err = strtol(argv[1], &end_ptr, 10);
			if (end_ptr[0] == '\0') {
				printf("%s\n", slurm_strerror(err));
			} else {
				exit_code = 1;
				fprintf (stderr,
					 "numeric arguments required for keyword:%s\n",
					 tag);
			}
		}
	} else if (!xstrncasecmp(tag, "exit", MAX(tag_len, 2))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		exit_flag = 1;
	} else if (!xstrncasecmp(tag, "gethost", MAX(tag_len, 7))) {
		if (argc == 3)
			scontrol_gethost(argv[1], argv[2]);
		else {
			exit_code = 1;
			fprintf(stderr,
				"two arguments required for keyword:%s\n",
				tag);
		}
	} else if (!xstrncasecmp(tag, "hash_file", MAX(tag_len, 15))) {
		if (argc > 3) {
			exit_code = 1;
			fprintf(stderr, "too many arguments for keyword:%s\n",
				tag);
		} else if (argc < 2) {
			exit_code = 1;
			fprintf(stderr, "missing argument for keyword:%s\n",
				tag);
		} else {
			int hash_len;
			buf_t *buf;
			slurm_hash_t hash = { 0 };

			if (argc > 2)
				hash.type = atoi(argv[2]);

			if (!(buf = create_mmap_buf(argv[1]))) {
				exit_code = 1;
				fprintf(stderr, "Can't open `%s`\n", argv[1]);
			} else {
				hash_len = hash_g_compute(buf->head, buf->size,
							  NULL, 0, &hash);

				FREE_NULL_BUFFER(buf);
				for (int i = 0; i < hash_len; i++)
					printf("%02x", (int)hash.hash[i]);
				printf("\n");
			}
		}
	} else if (!xstrncasecmp(tag, "hash", MAX(tag_len, 9))) {
		if (argc > 3) {
			exit_code = 1;
			fprintf(stderr, "too many arguments for keyword:%s\n",
				tag);
		} else if (argc < 2) {
			exit_code = 1;
			fprintf(stderr, "missing argument for keyword:%s\n",
				tag);
		} else {
			int hash_len;
			slurm_hash_t hash = { 0 };

			if (argc > 2)
				hash.type = atoi(argv[2]);

			hash_len = hash_g_compute(argv[1], strlen(argv[1]),
						  NULL, 0, &hash);
			for (int i = 0; i < hash_len; i++)
				printf("%02x", (int)hash.hash[i]);
			printf("\n");
		}
	} else if (!xstrncasecmp(tag, "help", MAX(tag_len, 2))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		_usage ();
	} else if (!xstrncasecmp(tag, "hide", MAX(tag_len, 2))) {
		all_flag = 0;
		detail_flag = 0;
	} else if (!xstrncasecmp(tag, "oneliner", MAX(tag_len, 1))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		one_liner = 1;
	} else if (!xstrncasecmp(tag, "pidinfo", MAX(tag_len, 3))) {
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else if (argc < 2) {
			exit_code = 1;
			fprintf (stderr,
				 "missing argument for keyword:%s\n",
				 tag);
		} else
			scontrol_pid_info ((pid_t) atol (argv[1]) );
	} else if (!xstrncasecmp(tag, "ping", MAX(tag_len, 3))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else
			_print_ping(argc, argv);
	} else if (!xstrncasecmp(tag, "\\q", 2) ||
		   !xstrncasecmp(tag, "quiet", MAX(tag_len, 4))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
				 tag);
		}
		quiet_flag = 1;
	} else if (!xstrncasecmp(tag, "quit", MAX(tag_len, 4))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		exit_flag = 1;
	} else if (!xstrncasecmp(tag, "reboot_nodes", MAX(tag_len, 3))) {
		_process_reboot_command(tag, argc, argv);
	} else if (!xstrncasecmp(tag, "reconfigure", MAX(tag_len, 3))) {
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
				 tag);
		}
		error_code = slurm_reconfigure();
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_reconfigure error");
		}
	} else if (!xstrncasecmp(tag, "requeue", MAX(tag_len, 3))) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			uint32_t i, flags = 0, start_pos = 1;
			for (i = 1; i < argc; i++) {
				if (parse_requeue_flags(argv[i], &flags))
					break;
				start_pos++;
			}
			for (i = start_pos; i < argc; i++) {
				scontrol_requeue(flags, argv[i]);
			}
		}
	} else if (!xstrncasecmp(tag, "requeuehold", 11)) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			uint32_t i, flags = 0, start_pos = 1;
			for (i = 1; i < argc; i++) {
				if (parse_requeue_flags(argv[i], &flags))
					break;
				start_pos++;
			}
			for (i = start_pos; i < argc; i++) {
				scontrol_requeue_hold(flags, argv[i]);
			}
		}

	} else if (!xstrncasecmp(tag, "hold",  4) ||
		   !xstrncasecmp(tag, "holdu", 5) ||
		   !xstrncasecmp(tag, "uhold", 5) ||
	           !xstrncasecmp(tag, "release", MAX(tag_len, 3))) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			for (i = 1; i < argc; i++) {
				error_code = scontrol_hold(argv[0], argv[i]);
				if (error_code) {
					exit_code = 1;
					if (quiet_flag != 1)
						slurm_perror("slurm_suspend error");
				}
			}
			(void) scontrol_hold(argv[0], NULL);   /* Clear cache */
		}
	} else if (!xstrncasecmp(tag, "suspend", MAX(tag_len, 2)) ||
		   !xstrncasecmp(tag, "resume", MAX(tag_len, 3))) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			for (i = 1; i < argc; i++) {
				scontrol_suspend(argv[0], argv[i]);
			}
		}
	} else if (!xstrncasecmp(tag, "top", MAX(tag_len, 3))) {
		if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else if (argc > 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
		} else {
			scontrol_top_job(argv[1]);
		}
	} else if (!xstrncasecmp(tag, "token", MAX(tag_len, 3))) {
		_fetch_token(argc, argv);
	} else if (!xstrncasecmp(tag, "wait_job", MAX(tag_len, 2))) {
		if (argc > 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
		} else if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			error_code = scontrol_job_ready(argv[1]);
			if (error_code)
				exit_code = 1;
		}
	} else if (!xstrncasecmp(tag, "setdebugflags", MAX(tag_len, 9))) {
		_setdebugflags(argc, argv);
	} else if (!xstrncasecmp(tag, "fsdampeningfactor", MAX(tag_len, 3)) ||
		 !xstrncasecmp(tag, "fairsharedampeningfactor",
			      MAX(tag_len, 3))) {
		if (argc > 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
		} else if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			uint16_t factor = 0;
			char *endptr;
			factor = (uint16_t)strtoul(argv[1], &endptr, 10);
			if (*endptr != '\0' || factor == 0) {
				if (quiet_flag != 1)
					fprintf(stderr,
						"invalid dampening factor: %s\n",
						argv[1]);
			} else {
				error_code = slurm_set_fs_dampeningfactor(
						factor);
				if (error_code) {
					exit_code = 1;
					if (quiet_flag != 1)
						slurm_perror("slurm_set_fs_dampeningfactor error");
				}
			}
		}
	} else if (!xstrncasecmp(tag, "setdebug", MAX(tag_len, 2))) {
		_setdebug(argc, argv);
	} else if (!xstrncasecmp(tag, "schedloglevel", MAX(tag_len, 3))) {
		if (argc > 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
		} else if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					tag);
		} else {
			int level = -1;
			char *endptr;
			char *levels[] = {
				"disable", "enable", NULL};
			int index = 0;
			while (levels[index]) {
				if (xstrcasecmp(argv[1], levels[index]) == 0) {
					level = index;
					break;
				}
				index ++;
			}
			if (level == -1) {
				/* effective levels: 0 - 1 */
				level = (int)strtoul (argv[1], &endptr, 10);
				if (*endptr != '\0' || level > 1) {
					level = -1;
					exit_code = 1;
					if (quiet_flag != 1)
						fprintf(stderr, "invalid schedlog "
							"level: %s\n", argv[1]);
				}
			}
			if (level != -1) {
				error_code = slurm_set_schedlog_level(
					level);
				if (error_code) {
					exit_code = 1;
					if (quiet_flag != 1)
						slurm_perror(
							"slurm_set_schedlog_level"
							" error");
				}
			}
		}
	} else if (!xstrncasecmp(tag, "show", MAX(tag_len, 3))) {
		_show_it (argc, argv);
	} else if (!xstrncasecmp(tag, "write", MAX(tag_len, 5))) {
		if (argc < 2) {
			exit_code = 1;
			fprintf(stderr,
				"too few arguments for keyword:%s\n",
				tag);
		} else if (!xstrncasecmp(argv[1], "batch_script",
					 MAX(strlen(argv[1]), 5))) {
			/* write batch_script <jobid> <optional filename> */
			if (argc > 4) {
				exit_code = 1;
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
			} else {
				scontrol_batch_script(argc-2, &argv[2]);
			}
		} else if (!xstrncasecmp(argv[1], "config",
					 MAX(strlen(argv[1]), 6))) {
			/* write config */
			if (argc > 3) {
				exit_code = 1;
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					tag);
			} else {
				_write_config(argv[2]);
			}
		} else {
			exit_code = 1;
			fprintf(stderr,
				"invalid write argument:%s\n",
				argv[1]);
		}
	} else if (!xstrncasecmp(tag, "takeover", MAX(tag_len, 8))) {
		int backup_inx = 1, control_cnt;
		slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

		slurm_ctl_conf_ptr = slurm_conf_lock();
		control_cnt = slurm_ctl_conf_ptr->control_cnt;
		slurm_conf_unlock();
		if (argc > 2) {
			exit_code = 1;
			fprintf(stderr, "%s: too many arguments\n",
				tag);
			backup_inx = -1;
		} else if (argc == 2) {
			backup_inx = atoi(argv[1]);
			if ((backup_inx < 1) || (backup_inx >= control_cnt)) {
				exit_code = 1;
				fprintf(stderr,
					"%s: invalid backup controller index (%d)\n",
					tag, backup_inx);
				backup_inx = -1;
			}
		} else if (control_cnt < 1) {
			exit_code = 1;
			fprintf(stderr, "%s: no backup controller defined\n",
				tag);
			backup_inx = -1;
		}

		if (backup_inx != -1) {
			error_code = slurm_takeover(backup_inx);
			if (error_code) {
				exit_code = 1;
				if (quiet_flag != 1)
					slurm_perror("slurm_takeover error");
			}
		}
	} else if (!xstrncasecmp(tag, "shutdown", MAX(tag_len, 8))) {
		/* require full command name */
		uint16_t options = SLURMCTLD_SHUTDOWN_ALL;
		if (argc == 2) {
			if (xstrcmp(argv[1], "slurmctld") &&
			    xstrcmp(argv[1], "controller")) {
				error_code = 1;
				exit_code = 1;
				fprintf (stderr,
					 "invalid shutdown argument:%s\n",
					 argv[1]);
			} else
				options= SLURMCTLD_SHUTDOWN_CTLD;
		} else if (argc > 2) {
			error_code = 1;
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		}
		if (error_code == 0) {
			error_code = slurm_shutdown(options);
			if (error_code) {
				exit_code = 1;
				if (quiet_flag != 1)
					slurm_perror ("slurm_shutdown error");
			}
		}
	} else if (!xstrncasecmp(tag, "update", MAX(tag_len, 1))) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 tag);
			return 0;
		}
		_update_it ((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(tag, "delete", MAX(tag_len, 1))) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 tag);
			return 0;
		}
		_delete_it ((argc - 1), &argv[1]);
	} else if (!xstrncasecmp(tag, "verbose", MAX(tag_len, 4))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 tag);
		}
		quiet_flag = -1;
	} else if (!xstrncasecmp(tag, "version", MAX(tag_len, 4))) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 tag);
		}
		_print_version();
	} else if (!xstrncasecmp(tag, "listpids", MAX(tag_len, 1))) {
		if (argc > 3) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else {
			scontrol_list_pids (argc == 1 ? NULL : argv[1],
					    argc <= 2 ? NULL : argv[2]);
		}
	} else if (!xstrncasecmp(tag, "getent", MAX(tag_len, 6))) {
		scontrol_getent(argc == 1 ? NULL : argv[1]);
	} else if (!xstrncasecmp(tag, "notify", MAX(tag_len, 1))) {
		if (argc < 3) {
			exit_code = 1;
			fprintf (stderr,
				 "too few arguments for keyword:%s\n",
				 tag);
		} else if (scontrol_job_notify(argc-1, &argv[1])) {
			exit_code = 1;
			slurm_perror("job notify failure");
		}
	} else if (!xstrncasecmp(tag, "callerid", MAX(tag_len, 3))) {
		if (argc < 5) {
			exit_code = 1;
			fprintf (stderr,
				 "too few arguments for keyword:%s\n",
				 tag);
		} else if (argc > 6) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 tag);
		} else if (scontrol_callerid(argc-1, &argv[1])) {
			exit_code = 1;
			slurm_perror("callerid failure");
		}
	} else {
		exit_code = 1;
		fprintf (stderr, "invalid keyword: %s\n", tag);
	}

	return 0;
}

/*
 * _create_it - create a slurm configuration per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _create_it(int argc, char **argv)
{
	/* Scan for "res" first, anywhere in the args.  When creating
	   a reservation there is a partition= option, which we don't
	   want to mistake for a requestion to create a partition. */
	int i, error_code = SLURM_SUCCESS;
	for (i=0; i<argc; i++) {
		char *tag = argv[i];
		char *val = strchr(argv[i], '=');
		int tag_len;

		if (val) {
			tag_len = val - argv[i];
			val++;
		} else {
			tag_len = strlen(tag);
		}
		if (!xstrncasecmp(tag, "NodeName", MAX(tag_len, 3))) {
			error_code = scontrol_create_node(argc, argv);
			break;
		} else if (!xstrncasecmp(tag, "PartitionName",
					 MAX(tag_len, 3))) {
			error_code = scontrol_create_part(argc, argv);
			break;
		} else if (!xstrncasecmp(tag, "ReservationName",
					 MAX(tag_len, 3))) {
			error_code = scontrol_create_res(argc, argv);
			break;
		}
	}

	if (i >= argc) {
		exit_code = 1;
		error("Invalid creation entity: %s", argv[0]);
	} else if (error_code)
		exit_code = 1;
}




/*
 * _delete_it - delete the specified slurm entity
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _delete_it(int argc, char **argv)
{
	char *tag = NULL, *val = NULL;
	int tag_len = 0;


	if (argc == 1) {
		tag = argv[0];
		val = strchr(argv[0], '=');
		if (val) {
			tag_len = val - argv[0];
			val++;
		} else {
			error("Proper format is 'delete <ENTITY>=<ID>' or 'delete <ENTITY> <ID>'");
			exit_code = 1;
			return;
		}
	} else if (argc == 2) {
		tag = argv[0];
		tag_len = strlen(argv[0]);
		val = argv[1];
	} else {
		error("Proper format is 'delete <ENTITY>=<ID>' or 'delete <ENTITY> <ID>'");
		exit_code = 1;
		return;
	}

	/* First identify the entity type to delete */
	if (xstrncasecmp(tag, "NodeName", MAX(tag_len, 3)) == 0) {
		update_node_msg_t node_msg = {0};
		node_msg.node_names = val;
		if (slurm_delete_node(&node_msg)) {
			char errmsg[64];
			snprintf(errmsg, 64, "delete_node %s", argv[0]);
			slurm_perror(errmsg);
			exit_code = 1;
		}
	} else if (xstrncasecmp(tag, "PartitionName", MAX(tag_len, 3)) == 0) {
		delete_part_msg_t part_msg;
		memset(&part_msg, 0, sizeof(part_msg));
		part_msg.name = val;
		if (slurm_delete_partition(&part_msg)) {
			char errmsg[64];
			snprintf(errmsg, 64, "delete_partition %s", argv[0]);
			slurm_perror(errmsg);
			exit_code = 1;
		}
	} else if (xstrncasecmp(tag, "ReservationName", MAX(tag_len, 3)) == 0) {
		reservation_name_msg_t   res_msg;
		memset(&res_msg, 0, sizeof(res_msg));
		res_msg.name = val;
		if (slurm_delete_reservation(&res_msg)) {
			char errmsg[64];
			snprintf(errmsg, 64, "delete_reservation %s", argv[0]);
			slurm_perror(errmsg);
			exit_code = 1;
		}
	} else {
		exit_code = 1;
		fprintf(stderr, "Invalid deletion entity: %s\n", argv[0]);
	}
}


/*
 * _show_it - print a description of the specified slurm entity
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _show_it(int argc, char **argv)
{
	char *tag = NULL, *val = NULL;
	int tag_len = 0;
	bool allow_opt = false;

	if (argc < 2) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr,
				"too few arguments for keyword:%s\n", argv[0]);
		return;
	}

	if (!xstrncasecmp(argv[1], "assoc_mgr", MAX(tag_len, 2)) ||
	    !xstrncasecmp(argv[1], "bbstat",    MAX(tag_len, 2)) ||
	    !xstrncasecmp(argv[1], "dwstat",    MAX(tag_len, 2)))
		allow_opt = true;

	if ((argc > 3) && !allow_opt) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr,
				"too many arguments for keyword:%s\n",
				argv[0]);
		return;
	}

	tag = argv[1];
	tag_len = strlen(tag);
	val = strchr(argv[1], '=');
	if (val) {
		tag_len = val - argv[1];
		val++;
	} else if (argc == 3) {
		val = argv[2];
	} else {
		val = NULL;
	}

	if (xstrncasecmp(tag, "aliases", MAX(tag_len, 1)) == 0) {
		if (val)
			_print_aliases (val);
		else
			_print_aliases (NULL);
	} else if (!xstrncasecmp(tag, "bbstat", MAX(tag_len, 2)) ||
		   !xstrncasecmp(tag, "dwstat", MAX(tag_len, 2))) {
		scontrol_print_bbstat(argc - 2, argv + 2);
	} else if (xstrncasecmp(tag, "burstbuffer", MAX(tag_len, 2)) == 0) {
		scontrol_print_burst_buffer ();
	} else if (!xstrncasecmp(tag, "assoc_mgr", MAX(tag_len, 2)) ||
		   !xstrncasecmp(tag, "cache", MAX(tag_len, 2))) {
		scontrol_print_assoc_mgr_info(argc - 2, argv + 2);
	} else if (xstrncasecmp(tag, "config", MAX(tag_len, 1)) == 0) {
		_print_config(val, argc, argv);
	} else if (xstrncasecmp(tag, "daemons", MAX(tag_len, 1)) == 0) {
		if (val) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					argv[0]);
		} else
			_print_daemons();
	} else if (xstrncasecmp(tag, "Federations",  MAX(tag_len, 1)) == 0) {
		scontrol_print_federation();
	} else if (xstrncasecmp(tag, "FrontendName",  MAX(tag_len, 1)) == 0) {
		scontrol_print_front_end_list(val);
	} else if (xstrncasecmp(tag, "hostnames", MAX(tag_len, 5)) == 0) {
		if (val)
			scontrol_print_hosts(val);
		else
			scontrol_print_hosts(getenv("SLURM_NODELIST"));
	} else if (xstrncasecmp(tag, "hostlist", MAX(tag_len, 5)) == 0) {
		if (!val) {
			exit_code = 1;
			fprintf(stderr, "invalid encode argument\n");
			_usage();
		} else if (scontrol_encode_hostlist(val, 0))
			exit_code = 1;
	} else if (xstrncasecmp(tag, "hostlistsorted", MAX(tag_len, 9)) == 0) {
		if (!val) {
			exit_code = 1;
			fprintf(stderr, "invalid encode argument\n");
			_usage();
		} else if (scontrol_encode_hostlist(val, 1))
			exit_code = 1;
	} else if (xstrncasecmp(tag, "jobs", MAX(tag_len, 1)) == 0 ||
		   xstrncasecmp(tag, "jobid", MAX(tag_len, 1)) == 0 ) {
		scontrol_print_job(val, argc, argv);
	} else if (xstrncasecmp(tag, "licenses", MAX(tag_len, 2)) == 0) {
		scontrol_print_licenses(val, argc, argv);
	} else if (xstrncasecmp(tag, "nodes", MAX(tag_len, 1)) == 0) {
		scontrol_print_node_list(val, argc, argv);
	} else if (xstrncasecmp(tag, "partitions", MAX(tag_len, 2)) == 0 ||
		   xstrncasecmp(tag, "partitionname", MAX(tag_len, 2)) == 0) {
		scontrol_print_part(val, argc, argv);
	} else if (xstrncasecmp(tag, "reservations", MAX(tag_len, 1)) == 0 ||
		   xstrncasecmp(tag, "reservationname", MAX(tag_len, 1)) == 0) {
		scontrol_print_res(val, argc, argv);
	} else if (xstrncasecmp(tag, "slurmd", MAX(tag_len, 2)) == 0) {
		_print_slurmd (val);
	} else if (xstrncasecmp(tag, "steps", MAX(tag_len, 2)) == 0) {
		scontrol_print_step(val, argc, argv);
	} else if (xstrncasecmp(tag, "topology", MAX(tag_len, 1)) == 0) {
		scontrol_print_topo (val);
	} else {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf (stderr,
				 "invalid entity:%s for keyword:%s \n",
				 tag, argv[0]);
	}

}



/*
 * _update_it - update the slurm configuration per the supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void _update_it(int argc, char **argv)
{
	char *val = NULL;
	int i, error_code = SLURM_SUCCESS;
	int node_tag = 0, part_tag = 0, job_tag = 0;
	int res_tag = 0;
	int debug_tag = 0, step_tag = 0, front_end_tag = 0;
	int suspend_exc_nodes_tag = 0, suspend_exc_parts_tag = 0,
	    suspend_exc_states_tag = 0;
	int jerror_code = SLURM_SUCCESS;
	update_mode_t mode = UPDATE_SET;

	/* First identify the entity to update */
	for (i=0; i<argc; i++) {
		char *tag = argv[i];
		int tag_len = 0;
		val = strchr(argv[i], '=');
		if (!val){
			tag = argv[i];
			tag_len = strlen(tag);
			++i;
		} else {
			tag_len = val - argv[i];
			if (*(val - 1) == '+'){
				mode = UPDATE_ADD;
				tag_len--;
			} else if (*(val - 1) == '-') {
				mode = UPDATE_REMOVE;
				tag_len--;
			}
			val++;
		}
		if (!xstrncasecmp(tag, "NodeName", MAX(tag_len, 3))) {
			node_tag = 1;
		} else if (!xstrncasecmp(tag, "PartitionName",
					MAX(tag_len, 3))) {
			part_tag = 1;
		} else if (!xstrncasecmp(tag, "JobId", MAX(tag_len, 3)) ||
			   !xstrncasecmp(tag, "JobNAME", MAX(tag_len, 3))) {
			job_tag = 1;
		} else if (!xstrncasecmp(tag, "StepId", MAX(tag_len, 4))) {
			step_tag = 1;
		} else if (!xstrncasecmp(tag, "FrontendName",
					 MAX(tag_len, 2))) {
			front_end_tag = 1;
		} else if (!xstrncasecmp(tag, "ReservationName",
					 MAX(tag_len, 3))) {
			res_tag = 1;
		} else if (!xstrncasecmp(tag, "SlurmctldDebug",
					 MAX(tag_len, 2))) {
			debug_tag = 1;
		} else if (!xstrncasecmp(tag, "SuspendExcNodes",
					 MAX(tag_len, 11))) {
			suspend_exc_nodes_tag = 1;
		} else if (!xstrncasecmp(tag, "SuspendExcParts",
					 MAX(tag_len, 11))) {
			suspend_exc_parts_tag = 1;
		} else if (!xstrncasecmp(tag, "SuspendExcStates",
					 MAX(tag_len, 11))) {
			suspend_exc_states_tag = 1;
		}
	}
	/* The order of tests matters here.  An update job request can include
	 * partition and reservation tags, possibly before the jobid tag, but
	 * none of the other updates have a jobid tag, so check jobtag first.
	 * Likewise, check restag next, because reservations can have a
	 * partition tag.  The order of the rest doesn't matter because there
	 * aren't any other duplicate tags.  */

	if (job_tag)
		jerror_code = scontrol_update_job (argc, argv);
	else if (step_tag)
		error_code = scontrol_update_step (argc, argv);
	else if (res_tag)
		error_code = scontrol_update_res (argc, argv);
	else if (node_tag)
		error_code = scontrol_update_node (argc, argv);
	else if (front_end_tag)
		error_code = scontrol_update_front_end (argc, argv);
	else if (part_tag)
		error_code = scontrol_update_part (argc, argv);
	else if (debug_tag)
		error_code = _update_slurmctld_debug(val);
	else if (suspend_exc_nodes_tag)
		error_code = slurm_update_suspend_exc_nodes(val, mode);
	else if (suspend_exc_parts_tag)
		error_code = slurm_update_suspend_exc_parts(val, mode);
	else if (suspend_exc_states_tag)
		error_code = slurm_update_suspend_exc_states(val, mode);
	else {
		exit_code = 1;
		fprintf(stderr, "No valid entity in update command\n");
		fprintf(stderr, "Input line must include \"NodeName\", ");
		fprintf(stderr, "\"PartitionName\", \"Reservation\", "
			"\"JobId\", or \"SlurmctldDebug\"\n");
	}

	if (error_code) {
		exit_code = 1;
		slurm_perror ("slurm_update error");
	}
	/* The slurm error message is already
	 * printed for each array task in
	 * scontrol_update_job()
	 */
	if (jerror_code)
		exit_code = 1;
}

/*
 * _update_slurmctld_debug - update the slurmctld debug level
 * IN  val - new value
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
static int _update_slurmctld_debug(char *val)
{
	char *endptr = NULL;
	int error_code = SLURM_SUCCESS;
	uint32_t level;

	if (val)
		level = (uint32_t)strtoul(val, &endptr, 10);

	if ((val == NULL) || (*endptr != '\0') || (level > 9)) {
		error_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr, "invalid debug level: %s\n",
				val);
	} else {
		error_code = slurm_set_debug_level(level);
	}

	return error_code;
}

/* _usage - show the valid scontrol commands */
void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	printf("%s\n", txt);
	xfree(txt);
}
