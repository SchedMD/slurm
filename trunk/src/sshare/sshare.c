/*****************************************************************************\
 *  sshare.c -   tool for listing the shares of association in
 *               relationship to the cluster running on. 
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "src/sshare/sshare.h"
#include <grp.h>


#define BUFFER_SIZE 4096

int exit_code;		/* sshare's exit code, =1 on any error at any time */
int quiet_flag;		/* quiet=1, verbose=-1, normal=0 */
int verbosity;		/* count of -v options */
sshare_time_format_t time_format = SSHARE_TIME_MINS;
char *time_format_string = "Minutes";
uint32_t my_uid = 0;

static int      _set_time_format(char *format);
static int      _get_info(shares_request_msg_t *shares_req, 
			  shares_response_msg_t **shares_resp);
static int      _addto_name_char_list(List char_list, char *names, bool gid);
static char *   _convert_to_name(int id, bool gid);
static void     _print_version( void );
static void	_usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS, opt_char;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	shares_request_msg_t req_msg;
	shares_response_msg_t *resp_msg = NULL;
	char *temp = NULL;
	int option_index;
	bool all_users = 0;

	static struct option long_options[] = {
		{"all", 0,0, 'a'},
		{"accounts", 1, 0, 'A'},
		{"help",     0, 0, 'h'},
		{"no_header", 0, 0, 'n'},
		{"parsable", 0, 0, 'p'},
		{"parsable2", 0, 0, 'P'},
		{"quiet",    0, 0, 'q'},
		{"uid", 1, 0, 'u'},
		{"usage",    0, 0, 'h'},
		{"user", 1, 0, 'u'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{NULL,       0, 0, 0}
	};

	/* Check to see if we are running a supported accounting plugin */
	temp = slurm_get_priority_type();
	if(strcasecmp(temp, "priority/multifactor")) {
		fprintf (stderr, "You are not running a supported "
			 "priority plugin\n(%s).\n"
			 "Only 'priority/multifactor' is supported.\n",
			temp);
		xfree(temp);
		exit(1);
	}
	xfree(temp);

	exit_code         = 0;
	quiet_flag        = 0;
	verbosity         = 0;
	memset(&req_msg, 0, sizeof(shares_request_msg_t));
	log_init("sshare", opts, SYSLOG_FACILITY_DAEMON, NULL);

	while((opt_char = getopt_long(argc, argv, "aA:hnpPqu:t:vV",
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
			if(!req_msg.acct_list) 
				req_msg.acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(req_msg.acct_list, optarg);
			break;
		case 'h':
			_usage ();
			exit(exit_code);
			break;
		case 'n':
			print_fields_have_header = 0;
			break;
		case 'p':
			print_fields_parsable_print = 
			PRINT_FIELDS_PARSABLE_ENDING;
			break;
		case 'P':
			print_fields_parsable_print =
			PRINT_FIELDS_PARSABLE_NO_ENDING;
			break;
		case 'q':
			quiet_flag = 1;
			break;
		case 'u':
			if(!strcmp(optarg, "-1")) {
				all_users = 1;
				break;
			}
			all_users = 0;
			if(!req_msg.user_list)
				req_msg.user_list = 
					list_create(slurm_destroy_char);
			_addto_name_char_list(req_msg.user_list, optarg, 0);
			break;
		case 't':
			_set_time_format(optarg);
			break;
		case 'v':
			quiet_flag = -1;
			verbosity++;
			break;
		case 'V':
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

	if (verbosity) {
		opts.stderr_level += verbosity;
		opts.prefix_level = 1;
		log_alter(opts, 0, NULL);
	}

	if(all_users) {
		if(req_msg.user_list 
		   && list_count(req_msg.user_list)) {
			list_destroy(req_msg.user_list);
			req_msg.user_list = NULL;
		}
		if(verbosity)
			fprintf(stderr, "Users requested:\n\t: all\n");
	} else if (verbosity && req_msg.user_list 
	    && list_count(req_msg.user_list)) {
		fprintf(stderr, "Users requested:\n");
		ListIterator itr = list_iterator_create(req_msg.user_list);
		while((temp = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", temp);
		list_iterator_destroy(itr);
	} else if(!req_msg.user_list || !list_count(req_msg.user_list)) {
		struct passwd *pwd = getpwuid(getuid());
		if(!req_msg.user_list)
			req_msg.user_list = list_create(slurm_destroy_char);
		temp = xstrdup(pwd->pw_name);
		list_append(req_msg.user_list, temp);
		if(verbosity) {
			fprintf(stderr, "Users requested:\n");
			fprintf(stderr, "\t: %s\n", temp);
		}
	}

	if(req_msg.acct_list && list_count(req_msg.acct_list)) {
		fprintf(stderr, "Accounts requested:\n");
		ListIterator itr = list_iterator_create(req_msg.acct_list);
		while((temp = list_next(itr))) 
			fprintf(stderr, "\t: %s\n", temp);
		list_iterator_destroy(itr);
	} else {
		if(req_msg.acct_list 
		   && list_count(req_msg.acct_list)) {
			list_destroy(req_msg.acct_list);
			req_msg.acct_list = NULL;
		}
		if(verbosity)
			fprintf(stderr, "Accounts requested:\n\t: all\n");

	}

	error_code = _get_info(&req_msg, &resp_msg);

	if(req_msg.acct_list)
		list_destroy(req_msg.acct_list);
	if(req_msg.user_list)
		list_destroy(req_msg.user_list);

	if (error_code) {
		slurm_perror("Couldn't get shares from controller");
		exit(error_code);
	}

	/* do stuff with it */
	process(resp_msg);

	slurm_free_shares_response_msg(resp_msg);

	exit(exit_code);
}

static int _set_time_format(char *format)
{
	if (strncasecmp (format, "Seconds", 1) == 0) {
		time_format = SSHARE_TIME_SECS;
		time_format_string = "Seconds";
	} else if (strncasecmp (format, "Minutes", 1) == 0) {
		time_format = SSHARE_TIME_MINS;
		time_format_string = "Minutes";
	} else if (strncasecmp (format, "Hours", 1) == 0) {
		time_format = SSHARE_TIME_HOURS;
		time_format_string = "Hours";
	} else {
		fprintf (stderr, "unknown time format %s", format);	
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
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
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
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

	return SLURM_PROTOCOL_SUCCESS;  	
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
					if (isdigit((int) *name)) {
						int id = atoi(name);
						xfree(name);
						name = _convert_to_name(
							id, gid);
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
			
			if (isdigit((int) *name)) {
				int id = atoi(name);
				xfree(name);
				name = _convert_to_name(id, gid);
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

static char *_convert_to_name(int id, bool gid)
{
	char *name = NULL;

	if(gid) {
		struct group *grp;
		if (!(grp=getgrgid(id))) {
			fprintf(stderr, "Invalid group id: %s\n", name);
			exit(1);
		}
		name = xstrdup(grp->gr_name);
	} else {
		struct passwd *pwd;
		if (!(pwd=getpwuid(id))) {
			fprintf(stderr, "Invalid user id: %s\n", name);
			exit(1);
		}
		name = xstrdup(pwd->pw_name);
	}
	return name;
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

/* _usage - show the valid sshare commands */
void _usage () {
	printf ("\
sshare [<OPTION>] [<COMMAND>]                                              \n\
    Valid <OPTION> values are:                                             \n\
     -a or --all: equivalent to \"all\" command                            \n\
     -A or --accounts: equivalent to \"accounts\" command                  \n\
     -h or --help or --usage: equivalent to \"help\" command               \n\
     -n or --no_header: no header will be added to the beginning of output \n\
     -p or --parsable: output will be '|' delimited with a '|' at the end  \n\
     -P or --parsable2: output will be '|' delimited without a '|' at the end\n\
     -q or --quiet: equivalent to \"quiet\" command                        \n\
     -u or --uid or user: equivalent to \"user\" command                   \n\
     -t or --time: equivalent to \"time\" command                          \n\
     -v or --verbose: equivalent to \"verbose\" command                    \n\
     -V or --version: equivalent to \"version\" command                    \n\
                                                                           \n\
                                                                           \n\
                                                                           \n\
    Valid <COMMAND> values are:                                            \n\
     --all                      list all                                   \n\
     --accounts <Account>       list accounts (comma separated list)       \n\
     --help                     print this description of use.             \n\
     --no_header                output without a header                    \n\
     --parsable                 output will be | delimited with an ending '|'\n\
     --parsable2                output will be | delimited without an ending '|'\n\
     --quiet                    print no messages other than error messages. \n\
     --uid                      list uid (comma separated list)            \n\
     --usage                    show help                                  \n\
     --user <User>              list user (comma separated list)           \n\
     --verbose                  wordy and windy output.                    \n\
     --version                  display tool version number.               \n\
                                                                           \n\
                                                                           \n\
                                                                           \n\
                                                                           \n\
  All commands entitys, and options are case-insensitive.               \n\n");
	
}

