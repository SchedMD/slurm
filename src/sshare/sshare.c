/*****************************************************************************\
 *  sshare.c -   tool for listing the shares of association in
 *               relationship to the cluster running on.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include <grp.h>

#include "src/common/data.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/priority.h"
#include "src/interfaces/serializer.h"
#include "src/sshare/sshare.h"

#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_AUTOCOMP 0x102
#define OPT_LONG_JSON 0x103
#define OPT_LONG_YAML 0x104

static int _addto_name_char_list(list_t *char_list, char *names, bool gid);
static int _single_cluster(int argc, char **argv,
			   shares_request_msg_t *req_msg);
static int _multi_cluster(int argc, char **argv, shares_request_msg_t *req_msg);
static char *   _convert_to_name(uint32_t id, bool is_gid);
static void     _print_version( void );
static void	_usage(void);
static void     _help_format_msg(void);

int exit_code;		/* sshare's exit code, =1 on any error at any time */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int verbosity;		/* count of -v options */
uint32_t my_uid = 0;
list_t *clusters = NULL;
uint16_t options = 0;

int main (int argc, char **argv)
{
	int opt_char;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	shares_request_msg_t req_msg;
	char *temp = NULL;
	int option_index;
	bool all_users = 0;

	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"accounts", 1, 0, 'A'},
		{"all",      0, 0, 'a'},
                {"helpformat",0,0, 'e'},
		{"long",     0, 0, 'l'},
		{"partition",0, 0, 'm'},
		{"cluster",  1, 0, 'M'},
		{"clusters", 1, 0, 'M'},
		{"noheader", 0, 0, 'n'},
		{"format",   1, 0, 'o'},
		{"parsable", 0, 0, 'p'},
		{"parsable2",0, 0, 'P'},
		{"users",    1, 0, 'u'},
		{"Users",    0, 0, 'U'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{"help",     0, 0, OPT_LONG_HELP},
		{"usage",    0, 0, OPT_LONG_USAGE},
		{"json",    optional_argument, 0, OPT_LONG_JSON},
		{"yaml",    optional_argument, 0, OPT_LONG_YAML},
		{NULL,       0, 0, 0}
	};

	exit_code         = 0;
	long_flag	  = 0;
	quiet_flag        = 0;
	verbosity         = 0;
	memset(&req_msg, 0, sizeof(shares_request_msg_t));
	slurm_init(NULL);
	if (priority_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize priority plugin");
	log_init("sshare", opts, SYSLOG_FACILITY_DAEMON, NULL);

	while ((opt_char = getopt_long(argc, argv, "aA:ehlM:no:pPqUu:t:vVm",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"sshare --help\" "
				"for more information\n");
			exit(1);
			break;
		case 'a':
			all_users = 1;
			break;
		case 'A':
			if (!req_msg.acct_list)
				req_msg.acct_list = list_create(xfree_ptr);
			slurm_addto_char_list(req_msg.acct_list, optarg);
			break;
		case 'e':
			_help_format_msg();
			exit(0);
			break;
		case 'h':
			print_fields_have_header = 0;
			break;
		case 'l':
			long_flag = 1;
			break;
		case 'M':
			FREE_NULL_LIST(clusters);
			if (slurm_get_cluster_info(&(clusters), optarg, 0)) {
				print_db_notok(optarg, 0);
				fatal("Could not get cluster information");
			}
			working_cluster_rec = list_peek(clusters);
			break;
		case 'm':
			options |= PRINT_PARTITIONS;
			break;
		case 'n':
			print_fields_have_header = 0;
			break;
		case 'o':
			xstrfmtcat(opt_field_list, "%s,", optarg);
			break;
		case 'p':
			print_fields_parsable_print =
			PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case 'P':
			print_fields_parsable_print =
			PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case 'u':
			if (!xstrcmp(optarg, "-1")) {
				all_users = 1;
				break;
			}
			all_users = 0;
			if (!req_msg.user_list)
				req_msg.user_list = list_create(xfree_ptr);
			_addto_name_char_list(req_msg.user_list, optarg, 0);
			break;
		case 'U':
			options |= PRINT_USERS_ONLY;
			break;
		case 'v':
			quiet_flag = -1;
			verbosity++;
			break;
		case 'V':
			_print_version();
			exit(exit_code);
			break;
		case OPT_LONG_HELP:
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_JSON:
			mimetype = MIME_TYPE_JSON;
			data_parser = optarg;
			if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
				fatal("JSON plugin load failure");
			break;
		case OPT_LONG_YAML:
			mimetype = MIME_TYPE_YAML;
			data_parser = optarg;
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

	if (all_users) {
		if (req_msg.user_list
		   && list_count(req_msg.user_list)) {
			FREE_NULL_LIST(req_msg.user_list);
		}
		if (verbosity)
			fprintf(stderr, "Users requested:\n\t: all\n");
	} else if (verbosity && req_msg.user_list
	    && list_count(req_msg.user_list)) {
		fprintf(stderr, "Users requested:\n");
		list_itr_t *itr = list_iterator_create(req_msg.user_list);
		while ((temp = list_next(itr)))
			fprintf(stderr, "\t: %s\n", temp);
		list_iterator_destroy(itr);
	} else if (!req_msg.user_list || !list_count(req_msg.user_list)) {
		char *user = uid_to_string_or_null(getuid());
		if (user) {
			if (!req_msg.user_list) {
				req_msg.user_list = list_create(xfree_ptr);
			}
			list_append(req_msg.user_list, user);
			if (verbosity) {
				fprintf(stderr, "Users requested:\n");
				fprintf(stderr, "\t: %s\n", user);
			}
		}
	}

	if (verbosity && req_msg.acct_list && list_count(req_msg.acct_list)) {
		list_itr_t *itr = list_iterator_create(req_msg.acct_list);
		fprintf(stderr, "Accounts requested:\n");
		while ((temp = list_next(itr)))
			fprintf(stderr, "\t: %s\n", temp);
		list_iterator_destroy(itr);
	} else if (verbosity) {
		fprintf(stderr, "Accounts requested:\n\t: all\n");
	}

	if (clusters)
		exit_code = _multi_cluster(argc, argv, &req_msg);
	else
		exit_code = _single_cluster(argc, argv, &req_msg);

	FREE_NULL_LIST(req_msg.acct_list);
	FREE_NULL_LIST(req_msg.user_list);
	exit(exit_code);
}


static int _single_cluster(int argc, char **argv, shares_request_msg_t *req_msg)
{
	int rc = SLURM_SUCCESS;
	shares_response_msg_t *resp_msg = NULL;

	rc = slurm_associations_get_shares(req_msg, &resp_msg);
	if (rc) {
		slurm_perror("Couldn't get shares from controller");
		return rc;
	}

	if (mimetype) {
		DATA_DUMP_CLI_SINGLE(OPENAPI_SHARES_RESP, resp_msg, argc, argv,
				     NULL, mimetype, data_parser, rc);
	} else {
		process(resp_msg, options);
	}

	slurm_free_shares_response_msg(resp_msg);

	return rc;
}

static int _multi_cluster(int argc, char **argv, shares_request_msg_t *req_msg)
{
	list_itr_t *itr;
	bool first = true;
	int rc = 0, rc2;

	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		if (first)
			first = false;
		else
			printf("\n");
		printf("CLUSTER: %s\n", working_cluster_rec->name);
		rc2 = _single_cluster(argc, argv, req_msg);
		if (rc2)
			rc = 1;
	}
	list_iterator_destroy(itr);

	return rc;
}

/* returns number of objects added to list */
static int _addto_name_char_list_internal(list_t *char_list, char *name,
					  void *x)
{
	char *tmp_name = NULL;
	bool gid = *(bool *)x;

	if (isdigit(*name)) {
		uint32_t id = strtoul(name, NULL, 10);
		tmp_name = _convert_to_name(id, gid);
	} else
		tmp_name = xstrdup(name);

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

/* returns number of objects added to list */
static int _addto_name_char_list(list_t *char_list, char *names, bool gid)
{
	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(char_list, names, &gid,
				     _addto_name_char_list_internal);
}

static char *_convert_to_name(uint32_t id, bool is_gid)
{
	char *name = NULL;

	if (is_gid) {
		if (!(name = gid_to_string_or_null(id))) {
			fprintf(stderr, "Invalid group id: %u\n", id);
			exit(1);
		}
	} else {
		if (!(name = uid_to_string_or_null(id))) {
			fprintf(stderr, "Invalid user id: %u\n", id);
			exit(1);
		}
	}
	return name;
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

/* _usage - show the valid sshare options */
void _usage(void){
	printf ("\
Usage:  sshare [OPTION]                                                    \n\
  Valid OPTIONs are:                                                       \n\
    -a or --all            list all users                                  \n\
    -A or --accounts=      display specific accounts (comma separated list)\n\
    -e or --helpformat     Print a list of fields that can be specified    \n\
                           with the '--format' option                      \n\
    --json[=data_parser]   Produce JSON output                             \n\
    -l or --long           include normalized usage in output              \n\
    -m or --partition      print the partition part of the association     \n\
    -M or --cluster=names  clusters to issue commands to.                  \n\
                           NOTE: SlurmDBD must be up.                      \n\
    -n or --noheader       omit header from output                         \n\
    -o or --format=        Comma separated list of fields (use             \n\
                           \"--helpformat\" for a list of available fields).\n\
    -p or --parsable       '|' delimited output with a trailing '|'        \n\
    -P or --parsable2      '|' delimited output without a trailing '|'     \n\
    -u or --users=         display specific users (comma separated list)   \n\
    -U or --Users          display only user information                   \n\
    -v or --verbose        display more information                        \n\
    -V or --version        display tool version number                     \n\
    --yaml[=data_parser]   Produce YAML output                             \n\
          --help           display this usage description                  \n\
          --usage          display this usage description                  \n\
                                                                           \n\n");
}

static void _help_format_msg(void)
{
	int i;

	for (i = 0; fields[i].name; i++) {
		if (i & 3)
			printf(" ");
		else if (i)
			printf("\n");
		printf("%-17s", fields[i].name);
	}
	printf("\n");
	return;
}
