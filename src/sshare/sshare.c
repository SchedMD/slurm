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

#include "src/sshare/sshare.h"
#include "src/common/proc_args.h"
#include <grp.h>


#define BUFFER_SIZE 4096
#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101

static int      _get_info(shares_request_msg_t *shares_req,
			  shares_response_msg_t **shares_resp);
static int      _addto_name_char_list(List char_list, char *names, bool gid);
static int 	_single_cluster(shares_request_msg_t *req_msg);
static int 	_multi_cluster(shares_request_msg_t *req_msg);
static char *   _convert_to_name(uint32_t id, bool is_gid);
static void     _print_version( void );
static void	_usage(void);
static void     _help_format_msg(void);

int exit_code;		/* sshare's exit code, =1 on any error at any time */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int verbosity;		/* count of -v options */
uint32_t my_uid = 0;
List clusters = NULL;
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
		{NULL,       0, 0, 0}
	};

	exit_code         = 0;
	long_flag	  = 0;
	quiet_flag        = 0;
	verbosity         = 0;
	memset(&req_msg, 0, sizeof(shares_request_msg_t));
	slurm_conf_init(NULL);
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
				req_msg.acct_list =
					list_create(slurm_destroy_char);
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
			if (!(clusters =
			     slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
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
				req_msg.user_list =
					list_create(slurm_destroy_char);
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
		ListIterator itr = list_iterator_create(req_msg.user_list);
		while ((temp = list_next(itr)))
			fprintf(stderr, "\t: %s\n", temp);
		list_iterator_destroy(itr);
	} else if (!req_msg.user_list || !list_count(req_msg.user_list)) {
		struct passwd *pwd;
		if ((pwd = getpwuid(getuid()))) {
			if (!req_msg.user_list) {
				req_msg.user_list =
					list_create(slurm_destroy_char);
			}
			temp = xstrdup(pwd->pw_name);
			list_append(req_msg.user_list, temp);
			if (verbosity) {
				fprintf(stderr, "Users requested:\n");
				fprintf(stderr, "\t: %s\n", temp);
			}
		}
	}

	if (req_msg.acct_list && list_count(req_msg.acct_list)) {
		if (verbosity) {
			fprintf(stderr, "Accounts requested:\n");
			ListIterator itr = list_iterator_create(req_msg.acct_list);
			while ((temp = list_next(itr)))
				fprintf(stderr, "\t: %s\n", temp);
			list_iterator_destroy(itr);
		}
	} else {
		if (req_msg.acct_list
		   && list_count(req_msg.acct_list)) {
			FREE_NULL_LIST(req_msg.acct_list);
		}
		if (verbosity)
			fprintf(stderr, "Accounts requested:\n\t: all\n");

	}

	if (clusters)
		exit_code = _multi_cluster(&req_msg);
	else
		exit_code = _single_cluster(&req_msg);

	exit(exit_code);
}


static int _single_cluster(shares_request_msg_t *req_msg)
{
	int rc = SLURM_SUCCESS;
	shares_response_msg_t *resp_msg = NULL;

	rc = _get_info(req_msg, &resp_msg);
	if (rc) {
		slurm_perror("Couldn't get shares from controller");
		return rc;
	}

	process(resp_msg, options);
	slurm_free_shares_response_msg(resp_msg);

	return rc;
}

static int _multi_cluster(shares_request_msg_t *req_msg)
{
	ListIterator itr;
	bool first = true;
	int rc = 0, rc2;

	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		if (first)
			first = false;
		else
			printf("\n");
		printf("CLUSTER: %s\n", working_cluster_rec->name);
		rc2 = _single_cluster(req_msg);
		rc  = MAX(rc, rc2);
	}
	list_iterator_destroy(itr);

	return rc;
}

static int _get_info(shares_request_msg_t *shares_req,
		     shares_response_msg_t **shares_resp)
{
	int rc;
        slurm_msg_t req_msg;
        slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

        req_msg.msg_type = REQUEST_SHARE_INFO;
        req_msg.data     = shares_req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SHARE_INFO:
		*shares_resp = (shares_response_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*shares_resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/* returns number of objects added to list */
static int _addto_name_char_list(List char_list, char *names, bool gid)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					//info("got %s %d", name, i-start);
					if (isdigit((int) *name)) {
						uint32_t id = strtoul(name,
								      NULL, 10);
						xfree(name);
						name = _convert_to_name(
							id, gid);
					}

					while ((tmp_char = list_next(itr))) {
						if (!xstrcasecmp(tmp_char,
								 name))
							break;
					}

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if (!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));

			if (isdigit((int) *name)) {
				uint32_t id = strtoul(name, NULL, 10);
				xfree(name);
				name = _convert_to_name(id, gid);
			}

			while ((tmp_char = list_next(itr))) {
				if (!xstrcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		}
	}
	list_iterator_destroy(itr);
	return count;
}

static char *_convert_to_name(uint32_t id, bool is_gid)
{
	char *name = NULL;

	if (is_gid) {
		struct group *grp;
		if (!(grp = getgrgid((gid_t) id))) {
			fprintf(stderr, "Invalid group id: %u\n", id);
			exit(1);
		}
		name = xstrdup(grp->gr_name);
	} else {
		struct passwd *pwd;
		if (!(pwd = getpwuid((uid_t) id))) {
			fprintf(stderr, "Invalid user id: %u\n", id);
			exit(1);
		}
		name = xstrdup(pwd->pw_name);
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
