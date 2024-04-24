/****************************************************************************\
 *  opts.c - sprio command line option parsing
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Don Lipari <lipari1@llnl.gov>
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

#define _GNU_SOURCE

#include <getopt.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/sprio/sprio.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP      0x100
#define OPT_LONG_USAGE     0x101
#define OPT_LONG_LOCAL     0x102
#define OPT_LONG_SIBLING   0x103
#define OPT_LONG_FEDR      0x104
#define OPT_LONG_AUTOCOMP  0x105
#define OPT_LONG_HELPFORMAT 0x106

/* FUNCTIONS */
static list_t *_build_job_list(char *str);
static list_t *_build_part_list(char *str);
static list_t *_build_user_list(char *str);
static char *_get_prefix(char *token);
static void  _help( void );
static void _help_format(void);
static void  _parse_token( char *token, char *field, int *field_size,
                           bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _usage( void );

decl_static_data(help_txt);
decl_static_data(usage_txt);

static void _opt_env(void)
{
	char *env_val;

	if (xstrstr(slurm_conf.fed_params, "fed_display"))
		params.federation = true;

	if ((env_val = getenv("SLURM_CLUSTERS"))) {
		xfree(params.cluster_names);
		params.cluster_names = xstrdup(env_val);
		params.local = true;
	}
	if (getenv("SPRIO_FEDERATION"))
		params.federation = true;
	if (getenv("SPRIO_LOCAL"))
		params.local = true;
	if (getenv("SPRIO_SIBLING"))
		params.sibling = true;
}

/*
 * parse_command_line
 */
extern void
parse_command_line( int argc, char* *argv )
{
	int opt_char;
	int option_index;
	bool override_format_env = false;

	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"noheader",   no_argument,       0, 'h'},
		{"jobs",       optional_argument, 0, 'j'},
		{"long",       no_argument,       0, 'l'},
		{"cluster",    required_argument, 0, 'M'},
		{"clusters",   required_argument, 0, 'M'},
		{"norm",       no_argument,       0, 'n'},
		{"format",     required_argument, 0, 'o'},
		{"sort",       required_argument, 0, 'S'},
		{"partition",  required_argument, 0, 'p'},
		{"user",       required_argument, 0, 'u'},
		{"users",      required_argument, 0, 'u'},
		{"verbose",    no_argument,       0, 'v'},
		{"version",    no_argument,       0, 'V'},
		{"weights",    no_argument,       0, 'w'},
		{"federation", no_argument,       0, OPT_LONG_FEDR},
		{"help",       no_argument,       0, OPT_LONG_HELP},
		{"helpformat", no_argument, 0, OPT_LONG_HELPFORMAT},
		{"local",      no_argument,       0, OPT_LONG_LOCAL},
		{"sib",        no_argument,       0, OPT_LONG_SIBLING},
		{"sibling",    no_argument,       0, OPT_LONG_SIBLING},
		{"usage",      no_argument,       0, OPT_LONG_USAGE},
		{NULL,         0,                 0, 0}
	};

	/* get defaults from environment */
	_opt_env();

	while ((opt_char = getopt_long(argc, argv, "hj::lM:no:S:p:u:vVw",
				       long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"sprio --help\" "
				"for more information\n");
			exit(1);
		case (int)'h':
			params.no_header = true;
			break;
		case (int) 'j':
			if (optarg) {
				params.jobs = xstrdup(optarg);
				params.job_list = _build_job_list(params.jobs);
			}
			params.job_flag = true;
			break;
		case (int) 'l':
			params.long_list = true;
			override_format_env = true;
			break;
		case (int) 'M':
			xfree(params.cluster_names);
			params.cluster_names = xstrdup(optarg);
			params.local = true;
			break;
		case (int) 'n':
			params.normalized = true;
			break;
		case (int) 'o':
			xfree(params.format);
			params.format = xstrdup(optarg);
			override_format_env = true;
			break;
		case (int) 'S':
			xfree(params.sort);
			params.sort = xstrdup(optarg);
			break;
		case (int) 'p':
			xfree(params.parts);
			params.parts = xstrdup(optarg);
			params.part_list = _build_part_list(params.parts);
			break;
		case (int) 'u':
			xfree(params.users);
			params.users = xstrdup(optarg);
			params.user_list = _build_user_list(params.users);
			break;
		case (int) 'v':
			params.verbose++;
			break;
		case (int) 'V':
			print_slurm_version ();
			exit(0);
		case (int) 'w':
			params.weights = true;
			break;
		case OPT_LONG_FEDR:
			params.federation = true;
			break;
		case OPT_LONG_HELP:
			_help();
			exit(0);
		case OPT_LONG_LOCAL:
			params.local = true;
			break;
		case OPT_LONG_SIBLING:
			params.sibling = true;
			break;
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_HELPFORMAT:
			_help_format();
			exit(0);
			break;
		}
	}

	if (params.long_list && params.format)
		fatal("Options -o(--format) and -l(--long) are mutually exclusive. Please remove one and retry.");

	/* This needs to be evaluated here instead of _opt_env*/
	if (!override_format_env) {
		char *env_val;
		if ((env_val = getenv("SPRIO_FORMAT")))
			params.format = xstrdup(env_val);
	}

	if (optind < argc) {
		if (params.job_flag) {
			params.jobs = xstrdup(argv[optind++]);
			params.job_list = _build_job_list(params.jobs);
		}
		if (optind < argc) {
			error("Unrecognized option: %s",argv[optind]);
			_usage();
			exit(1);
		}
	}

	if (params.verbose)
		_print_options();

	FREE_NULL_LIST(params.clusters);
	if (params.cluster_names) {
		if (slurm_get_cluster_info(&(params.clusters),
					   params.cluster_names,
					   (params.federation ?
					    SHOW_FEDERATION : SHOW_LOCAL))) {

			print_db_notok(params.cluster_names, 0);
			fatal("Could not get cluster information");
		}
		working_cluster_rec = list_peek(params.clusters);
		params.local = true;
	}

	if (params.clusters) {
		if (list_count(params.clusters) > 1) {
			fatal("Only one cluster can be used at a time with "
			      "sprio");
		}
		working_cluster_rec = list_peek(params.clusters);
	}
}

static fmt_data_t fmt_data[] = {
	{NULL, 'a', _print_age_priority_normalized},
	{NULL, 'A', _print_age_priority_weighted},
	{NULL, 'b', _print_assoc_priority_normalized},
	{NULL, 'B', _print_assoc_priority_weighted},
	{NULL, 'c', _print_cluster_name},
	{NULL, 'f', _print_fs_priority_normalized},
	{NULL, 'F', _print_fs_priority_weighted},
	{NULL, 'i', _print_job_job_id},
	{NULL, 'j', _print_js_priority_normalized},
	{NULL, 'J', _print_js_priority_weighted},
	{NULL, 'n', _print_qos_name},
	{NULL, 'N', _print_job_nice},
	{NULL, 'o', _print_account},
	{NULL, 'p', _print_part_priority_normalized},
	{NULL, 'P', _print_part_priority_weighted},
	{NULL, 'r', _print_partition},
	{NULL, 'S', _print_site_priority},
	{NULL, 'q', _print_qos_priority_normalized},
	{NULL, 'Q', _print_qos_priority_weighted},
	{NULL, 'u', _print_job_user_name},
	{NULL, 'y', _print_job_priority_normalized},
	{NULL, 'Y', _print_job_priority_weighted},
	{NULL, 't', _print_tres_normalized},
	{NULL, 'T', _print_tres_weighted},
	{NULL, 0, NULL},
};

/*
 * parse_format - Take the user's format specification and use it to build
 *	build the format specifications (internalize it to print.c data
 *	structures)
 * IN format - user's format specification
 * RET zero or error code
 */
extern int parse_format( char* format )
{
	int field_size;
	bool right_justify;
	char *prefix = NULL, *suffix = NULL, *token = NULL;
	char *tmp_char = NULL, *tmp_format = NULL;
	char field[1];
	int i;
	bool found = false;

	if (format == NULL) {
		error ("Format option lacks specification.");
		exit( 1 );
	}

	params.format_list = list_create( NULL );
	if ((prefix = _get_prefix(format))) {
		job_format_add_prefix( params.format_list, 0, 0, prefix);
	}

	tmp_format = xstrdup(format);

	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		found = false;
		_parse_token( token, field, &field_size, &right_justify,
			      &suffix);
		for (i = 0; fmt_data[i].name || fmt_data[i].c; i++) {
			if (field[0] == fmt_data[i].c) {
				found = true;
				job_format_add_function(
					params.format_list, field_size,
					right_justify, suffix, fmt_data[i].fn);
				break;
			}
		}
		if (found) {
			; /* NO OP */
		} else
			error( "Invalid job format specification: %c",
			       field[0] );

		token = strtok_r( NULL, "%", &tmp_char);
	}

	xfree( tmp_format );
	return SLURM_SUCCESS;
}

/* Take a format specification and copy out it's prefix
 * IN/OUT token - input specification, everything before "%" is removed
 * RET - everything before "%" in the token
 */
static char *
_get_prefix( char *token )
{
	char *pos, *prefix;

	if (token == NULL)
		return NULL;

	pos = strchr(token, (int) '%');
	if (pos == NULL)	/* everything is prefix */
		return xstrdup(token);
	if (pos == token)	/* no prefix */
		return NULL;

	pos[0] = '\0';		/* some prefix */
	prefix = xstrdup(token);
	pos[0] = '%';
	memmove(token, pos, (strlen(pos)+1));
	return prefix;
}

/* Take a format specification and break it into its components
 * IN token - input specification without leading "%", eg. ".5u"
 * OUT field - the letter code for the data type
 * OUT field_size - byte count
 * OUT right_justify - true of field to be right justified
 * OUT suffix - string containing everthing after the field specification
 */
static void
_parse_token( char *token, char *field, int *field_size, bool *right_justify,
	      char **suffix)
{
	int i = 0;

	xassert(token);

	if (token[i] == '.') {
		*right_justify = true;
		i++;
	} else
		*right_justify = false;

	*field_size = 0;
	while ((token[i] >= '0') && (token[i] <= '9'))
		*field_size = (*field_size * 10) + (token[i++] - '0');

	field[0] = token[i++];

	*suffix = xstrdup(&token[i]);
}

/* print the parameters specified */
static void
_print_options(void)
{
	list_itr_t *iterator;
	int i;
	uint32_t *job_id;
	uint32_t *user;

	printf( "-----------------------------\n" );
	printf( "format     = %s\n", params.format );
	printf( "job_flag   = %d\n", params.job_flag );
	printf( "jobs       = %s\n", params.jobs );
	printf( "partition  = %s\n", params.parts );
	printf( "users      = %s\n", params.users );
	printf( "verbose    = %d\n", params.verbose );

	if ((params.verbose > 1) && params.job_list) {
		i = 0;
		iterator = list_iterator_create( params.job_list );
		while ( (job_id = list_next( iterator )) ) {
			printf( "job_list[%d] = %u\n", i++, *job_id);
		}
		list_iterator_destroy( iterator );
	}

	if ((params.verbose > 1) && params.user_list) {
		i = 0;
		iterator = list_iterator_create( params.user_list );
		while ( (user = list_next( iterator )) ) {
			printf( "user_list[%d] = %u\n", i++, *user);
		}
		list_iterator_destroy( iterator );
	}

	printf( "-----------------------------\n\n\n" );
}


/*
 * _build_job_list- build a list of job_ids
 * IN str - comma separated list of job_ids
 * RET List of job_ids (uint32_t)
 */
static list_t *_build_job_list(char *str)
{
	list_t *my_list;
	char *job = NULL, *tmp_char = NULL, *my_job_list = NULL;
	uint32_t *job_id = NULL;

	if ( str == NULL)
		return NULL;

	my_list = list_create(xfree_ptr);
	my_job_list = xstrdup(str);
	job = strtok_r(my_job_list, ",", &tmp_char);
	while (job) {
		slurm_selected_step_t sel_step;
		if (unfmt_job_id_string(str, &sel_step, NO_VAL)) {
			error("Invalid job id: %s", job);
			exit(1);
		} else if (sel_step.het_job_offset != NO_VAL)
			sel_step.step_id.job_id += sel_step.het_job_offset;
		job_id = xmalloc(sizeof(uint32_t));
		*job_id = sel_step.step_id.job_id;
		list_append(my_list, job_id);
		job = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_job_list);
	return my_list;
}

/*
 * _build_part_list- build a list of partitions
 * IN str - comma separated list of partition names
 * RET List of partitions (char)
 */
static list_t *_build_part_list(char *str)
{
	list_t *my_list;
	char *part = NULL;
	char *tmp_char = NULL, *tok = NULL, *my_part_list = NULL;

	if (str == NULL)
		return NULL;

	my_list = list_create(xfree_ptr);
	my_part_list = xstrdup(str);
	tok = strtok_r(my_part_list, ",", &tmp_char);
	while (tok) {
		part = xstrdup(tok);
		list_append(my_list, part);
		tok = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_part_list);
	return my_list;
}

/*
 * _build_user_list- build a list of UIDs
 * IN str - comma separated list of user names
 * RET List of UIDs (uint32_t)
 */
static list_t *_build_user_list(char *str)
{
	list_t *my_list;
	char *user = NULL;
	char *tmp_char = NULL, *my_user_list = NULL;
	uid_t uid = (uid_t) 0;

	if (str == NULL)
		return NULL;

	my_list = list_create(NULL);
	my_user_list = xstrdup(str);
	user = strtok_r(my_user_list, ",", &tmp_char);
	while (user) {
		if (uid_from_string(user, &uid) < 0) {
			error("Invalid user: %s\n", user);
		} else {
			uint32_t *u_tmp = xmalloc(sizeof( uint32_t ));
			*u_tmp = (uint32_t) uid;
			list_append(my_list, u_tmp);
		}
		user = strtok_r (NULL, ",", &tmp_char);
	}
	xfree(my_user_list);
	return my_list;
}

static void _usage( void )
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	printf("%s", txt);
	xfree(txt);
}

static void _help( void )
{
	char *txt;
	static_ref_to_cstring(txt, help_txt);
	printf("%s", txt);
	xfree(txt);
}

static void _help_format(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data[i].c || fmt_data[i].name; i++) {
		if (!fmt_data[i].c)
			continue;

		if (cnt & 8) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%%%-5c", fmt_data[i].c);
	}
	printf("\n");
}
