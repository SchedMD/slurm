/****************************************************************************\
 *  opts.c - squeue command line option parsing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2013 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/common/uid.h"

#include "src/squeue/squeue.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_HIDE  0x102
#define OPT_LONG_START 0x103

/* FUNCTIONS */
static List  _build_job_list( char* str );
static List  _build_str_list( char* str );
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static List  _build_step_list( char* str );
static List  _build_user_list( char* str );
static char *_get_prefix(char *token);
static void  _help( void );
static int   _parse_state( char* str, uint16_t* states );
static void  _parse_token( char *token, char *field, int *field_size,
			   bool *right_justify, char **suffix);
static void  _print_options( void );
static void  _usage( void );

/*
 * parse_command_line
 */
extern void
parse_command_line( int argc, char* argv[] )
{
	char *env_val = NULL;
	bool override_format_env = false;
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"accounts",   required_argument, 0, 'A'},
		{"all",        no_argument,       0, 'a'},
		{"array",      no_argument,       0, 'r'},
		{"format",     required_argument, 0, 'o'},
		{"help",       no_argument,       0, OPT_LONG_HELP},
		{"hide",       no_argument,       0, OPT_LONG_HIDE},
		{"iterate",    required_argument, 0, 'i'},
		{"jobs",       optional_argument, 0, 'j'},
		{"long",       no_argument,       0, 'l'},
		{"cluster",    required_argument, 0, 'M'},
		{"clusters",   required_argument, 0, 'M'},
		{"name",       required_argument, 0, 'n'},
		{"node",       required_argument, 0, 'w'},
		{"nodes",      required_argument, 0, 'w'},
		{"nodelist",   required_argument, 0, 'w'},
		{"noheader",   no_argument,       0, 'h'},
		{"partitions", required_argument, 0, 'p'},
		{"qos",        required_argument, 0, 'q'},
		{"reservation",required_argument, 0, 'R'},
		{"sort",       required_argument, 0, 'S'},
		{"start",      no_argument,       0, OPT_LONG_START},
		{"steps",      optional_argument, 0, 's'},
		{"states",     required_argument, 0, 't'},
		{"usage",      no_argument,       0, OPT_LONG_USAGE},
		{"user",       required_argument, 0, 'u'},
		{"users",      required_argument, 0, 'u'},
		{"verbose",    no_argument,       0, 'v'},
		{"version",    no_argument,       0, 'V'},
		{NULL,         0,                 0, 0}
	};

	if (getenv("SQUEUE_ALL"))
		params.all_flag = true;
	if (getenv("SQUEUE_ARRAY"))
		params.array_flag = true;
	if ( ( env_val = getenv("SQUEUE_SORT") ) )
		params.sort = xstrdup(env_val);
	if ( ( env_val = getenv("SLURM_CLUSTERS") ) ) {
		if (!(params.clusters = slurmdb_get_info_cluster(env_val))) {
			print_db_notok(env_val, 1);
			exit(1);
		}
		working_cluster_rec = list_peek(params.clusters);
	}

	while ((opt_char = getopt_long(argc, argv,
				       "A:ahi:j::ln:M:o:p:q:R:rs::S:t:u:U:vVw:",
				       long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"squeue --help\" "
				"for more information\n");
			exit(1);
		case (int) 'A':
		case (int) 'U':	/* backwards compatibility */
			xfree(params.accounts);
			params.accounts = xstrdup(optarg);
			params.account_list =
				_build_str_list( params.accounts );
		break;
		case (int)'a':
			params.all_flag = true;
			break;
		case (int)'h':
			params.no_header = true;
			break;
		case (int) 'i':
			params.iterate= atoi(optarg);
			if (params.iterate <= 0) {
				error ("--iterate=%s\n", optarg);
				exit(1);
			}
			break;
		case (int) 'j':
			if (optarg) {
				params.jobs = xstrdup(optarg);
				params.job_list =
					_build_job_list(params.jobs);
			}
			params.job_flag = true;
			break;
		case (int) 'l':
			params.long_list = true;
			override_format_env = true;
			break;
		case (int) 'M':
			if (params.clusters)
				list_destroy(params.clusters);
			if (!(params.clusters =
			    slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(params.clusters);
			break;
		case (int) 'n':
			xfree(params.names);
			params.names = xstrdup(optarg);
			params.name_list = _build_str_list( params.names );
			break;
		case (int) 'o':
			xfree(params.format);
			params.format = xstrdup(optarg);
			override_format_env = true;

			break;
		case (int) 'p':
			xfree(params.partitions);
			params.partitions = xstrdup(optarg);
			params.part_list =
				_build_str_list( params.partitions );
			params.all_flag = true;
			break;
		case (int) 'q':
			xfree(params.qoss);
			params.qoss = xstrdup(optarg);
			params.qos_list =
				_build_str_list( params.qoss );
			break;
		case (int) 'R':
			xfree(params.reservation);
			params.reservation = xstrdup(optarg);
			break;
		case (int)'r':
			params.array_flag = true;
			break;
		case (int) 's':
			if (optarg) {
				params.steps = xstrdup(optarg);
				params.step_list =
					_build_step_list(params.steps);
			}
			params.step_flag = true;
			override_format_env = true;
			break;
		case (int) 'S':
			xfree(params.sort);
			params.sort = xstrdup(optarg);
			break;
		case (int) 't':
			xfree(params.states);
			params.states = xstrdup(optarg);
			params.state_list =
				_build_state_list( params.states );
			break;
		case (int) 'u':
			xfree(params.users);
			params.users = xstrdup(optarg);
			params.user_list =
				_build_user_list( params.users );
			break;
		case (int) 'v':
			params.verbose++;
			break;
		case (int) 'V':
			print_slurm_version();
			exit(0);
		case (int) 'w':
			if (params.nodes)
				hostset_destroy(params.nodes);

			params.nodes = hostset_create(optarg);
			if (params.nodes == NULL) {
				error("'%s' invalid entry for --nodelist",
				      optarg);
				exit(1);
			}
			break;
		case OPT_LONG_HELP:
			_help();
			exit(0);
		case OPT_LONG_HIDE:
			params.all_flag = false;
			break;
		case OPT_LONG_START:
			params.start_flag = true;
			break;
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		}
	}

	if ( override_format_env == false ) {
		if ( ( env_val = getenv("SQUEUE_FORMAT") ) )
			params.format = xstrdup(env_val);
	}

	params.cluster_flags = slurmdb_setup_cluster_flags();
	if (optind < argc) {
		if (params.job_flag) {
			params.jobs = xstrdup(argv[optind++]);
			params.job_list = _build_job_list(params.jobs);
		} else if (params.step_flag) {
			params.steps = xstrdup(argv[optind++]);
			params.step_list = _build_step_list(params.steps);
		}
		if (optind < argc) {
			error("Unrecognized option: %s",argv[optind]);
			_usage();
			exit(1);
		}
	}

	if ( params.job_flag && params.step_flag) {
		if (params.job_list) {
			verbose("Printing job steps with job filter");
			params.job_flag = false;
		} else {
			error("Incompatible options --jobs and --steps");
			exit(1);
		}
	}

	if ( params.nodes ) {
		char *name1 = NULL;
		char *name2 = NULL;
		hostset_t nodenames = hostset_create(NULL);

		while ( hostset_count(params.nodes) > 0 ) {
			name1 = hostset_pop(params.nodes);

			/* localhost = use current host name */
			if ( strcasecmp("localhost", name1) == 0 ) {
				name2 = xmalloc(128);
				gethostname_short(name2, 128);
			} else {
				/* translate NodeHostName to NodeName */
				name2 = slurm_conf_get_nodename(name1);

				/* use NodeName if translation failed */
				if ( name2 == NULL )
					name2 = xstrdup(name1);
			}
			hostset_insert(nodenames, name2);
			free(name1);
			xfree(name2);
		}

		/* Replace params.nodename with the new one */
		hostset_destroy(params.nodes);
		params.nodes = nodenames;
	}

	if ( ( params.accounts == NULL ) &&
	     ( env_val = getenv("SQUEUE_ACCOUNT") ) ) {
		params.accounts = xstrdup(env_val);
		params.account_list = _build_str_list( params.accounts );
	}

	if ( ( params.names == NULL ) &&
	     ( env_val = getenv("SQUEUE_NAMES") ) ) {
		params.names = xstrdup(env_val);
		params.name_list = _build_str_list( params.names );
	}

	if ( ( params.partitions == NULL ) &&
	     ( env_val = getenv("SQUEUE_PARTITION") ) ) {
		params.partitions = xstrdup(env_val);
		params.part_list = _build_str_list( params.partitions );
		params.all_flag = true;
	}

	if ( ( params.qoss == NULL ) &&
	     ( env_val = getenv("SQUEUE_QOS") ) ) {
		params.qoss = xstrdup(env_val);
		params.qos_list = _build_str_list( params.qoss );
	}

	if ( ( params.states == NULL ) &&
	     ( env_val = getenv("SQUEUE_STATES") ) ) {
		params.states = xstrdup(env_val);
		params.state_list = _build_state_list( params.states );
	}

	if ( ( params.users == NULL ) &&
	     ( env_val = getenv("SQUEUE_USERS") ) ) {
		params.users = xstrdup(env_val);
		params.user_list = _build_user_list( params.users );
	}

	if ( params.start_flag && !params.step_flag ) {
		/* Set more defaults */
		if (params.format == NULL)
			params.format = xstrdup("%.7i %.9P %.8j %.8u %.2t %.19S %.6D %R");
		if (params.sort == NULL)
			params.sort = xstrdup("S");
		if (params.states == NULL) {
			params.states = xstrdup("PD");
			params.state_list = _build_state_list( params.states );
		}
	}

	if (params.job_list && (list_count(params.job_list) == 1)) {
		ListIterator iterator;
		uint32_t *job_id_ptr;
		iterator = list_iterator_create(params.job_list);
		job_id_ptr = list_next(iterator);
		params.job_id = *job_id_ptr;
		list_iterator_destroy(iterator);
	}
	if (params.user_list && (list_count(params.user_list) == 1)) {
		ListIterator iterator;
		uint32_t *uid_ptr;
		iterator = list_iterator_create(params.user_list);
		while ((uid_ptr = list_next(iterator))) {
			params.user_id = *uid_ptr;
			break;
		}
		list_iterator_destroy(iterator);
	}

	if ( params.verbose )
		_print_options();
}

/*
 * _parse_state - convert job state name string to numeric value
 * IN str - state name
 * OUT states - enum job_states value corresponding to str
 * RET 0 or error code
 */
static int
_parse_state( char* str, uint16_t* states )
{
	int i;
	char *state_names;

	if ((i = job_state_num(str)) >= 0) {
		*states = (uint16_t) i;
		return SLURM_SUCCESS;
	}

	error ("Invalid job state specified: %s", str);
	state_names = xstrdup(job_state_string(0));
	for (i=1; i<JOB_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, job_state_string(i));
	}
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_COMPLETING));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_CONFIGURING));
	error ("Valid job states include: %s\n", state_names);
	xfree (state_names);
	return SLURM_ERROR;
}

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
	bool format_all = false;
	int i;

	if (format == NULL) {
		error ("Format option lacks specification.");
		exit( 1 );
	}

	params.format_list = list_create( NULL );
	if ((prefix = _get_prefix(format))) {
		if (params.step_flag)
			step_format_add_prefix( params.format_list, 0, 0,
						prefix);
		else
			job_format_add_prefix( params.format_list, 0, 0,
					       prefix);
	}

	if (!strcasecmp(format, "%all")) {
		xstrfmtcat(tmp_format, "%c%c", '%', 'a');
		for (i = 'b'; i <= 'z'; i++)
			xstrfmtcat(tmp_format, "|%c%c", '%', (char) i);
		for (i = 'A'; i <= 'Z'; i++)
			xstrfmtcat(tmp_format, "|%c%c ", '%', (char) i);
		format_all = true;
	} else {
		tmp_format = xstrdup(format);
	}

	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		_parse_token( token, field, &field_size, &right_justify,
			      &suffix);
		if (params.step_flag) {
			if      (field[0] == 'A')
				step_format_add_num_tasks( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'b')
				step_format_add_gres( params.format_list,
						      field_size,
						      right_justify, suffix );
			else if (field[0] == 'i')
				step_format_add_id( params.format_list,
						    field_size,
						    right_justify, suffix );
			else if (field[0] == 'j')
				step_format_add_name( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'l')
				step_format_add_time_limit( params.format_list,
							    field_size,
							    right_justify,
							    suffix );
			else if (field[0] == 'M')
				step_format_add_time_used( params.format_list,
							    field_size,
							    right_justify,
							    suffix );
			else if (field[0] == 'N')
				step_format_add_nodes( params.format_list,
						       field_size,
						       right_justify, suffix );
			else if (field[0] == 'P')
				step_format_add_partition( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'S')
				step_format_add_time_start( params.format_list,
							    field_size,
							    right_justify,
							    suffix );
			else if (field[0] == 'U')
				step_format_add_user_id( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'u')
				step_format_add_user_name( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (format_all)
				;	/* ignore */
			else {
				prefix = xstrdup("%");
				xstrcat(prefix, token);
				xfree(suffix);
				suffix = prefix;

				step_format_add_invalid( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
				error ( "Invalid job step format "
					"specification: %c",
					field[0] );
			}
		} else {
			if (field[0] == 'a')
				job_format_add_account( params.format_list,
							field_size,
							right_justify,
							suffix  );
			else if (field[0] == 'A')
				job_format_add_job_id2(params.format_list,
						       field_size,
						       right_justify,
						       suffix);
			else if (field[0] == 'b')
				job_format_add_gres( params.format_list,
						     field_size, right_justify,
						     suffix );
			else if (field[0] == 'B')
				job_format_add_batch_host( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'c')
				job_format_add_min_cpus( params.format_list,
							 field_size,
							 right_justify,
							 suffix  );
			else if (field[0] == 'C')
				job_format_add_num_cpus( params.format_list,
							 field_size,
							 right_justify,
							 suffix  );
			else if (field[0] == 'd')
				job_format_add_min_tmp_disk(
							  params.format_list,
							  field_size,
							  right_justify,
							  suffix  );
			else if (field[0] == 'D')
				job_format_add_num_nodes( params.format_list,
							  field_size,
							  right_justify,
							  suffix  );
			else if (field[0] == 'e')
				job_format_add_time_end( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'E')
				job_format_add_dependency( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'f')
				job_format_add_features( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'F')
				job_format_add_array_job_id(
							params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'g')
				job_format_add_group_name( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'G')
				job_format_add_group_id( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'h')
				job_format_add_shared( params.format_list,
						       field_size,
						       right_justify,
						       suffix );
			else if (field[0] == 'H')
				job_format_add_sockets( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'i')
				job_format_add_job_id( params.format_list,
						       field_size,
						       right_justify,
						       suffix );
			else if (field[0] == 'I')
				job_format_add_cores( params.format_list,
						      field_size,
						      right_justify, suffix );
			else if (field[0] == 'j')
				job_format_add_name( params.format_list,
						     field_size,
						     right_justify, suffix );
			else if (field[0] == 'J')
				job_format_add_threads( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'k')
				job_format_add_comment( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'K')
				job_format_add_array_task_id(
							params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'l')
				job_format_add_time_limit( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'L')
				job_format_add_time_left( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'm')
				job_format_add_min_memory( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'M')
				job_format_add_time_used( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'n')
				job_format_add_req_nodes( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'N')
				job_format_add_nodes( params.format_list,
						      field_size,
						      right_justify, suffix );
			else if (field[0] == 'o')
				job_format_add_command( params.format_list,
							field_size,
							right_justify, suffix);
			else if (field[0] == 'O')
				job_format_add_contiguous( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'p')
				job_format_add_priority( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'P')
				job_format_add_partition( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'q')
				job_format_add_qos( params.format_list,
						    field_size,
						    right_justify,
						    suffix );
			else if (field[0] == 'Q')
				 job_format_add_priority_long(
							params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'r')
				job_format_add_reason( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'R')
				job_format_add_reason_list( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 's')
				job_format_add_select_jobinfo(
							 params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (field[0] == 'S')
				job_format_add_time_start( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 't')
				job_format_add_job_state_compact(
							params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'T')
				job_format_add_job_state( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'U')
				job_format_add_user_id( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'u')
				job_format_add_user_name( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'v')
				job_format_add_reservation( params.format_list,
							field_size,
							right_justify,
							suffix );
			else if (field[0] == 'w')
				job_format_add_wckey( params.format_list,
						      field_size,
						      right_justify,
						      suffix );
			else if (field[0] == 'W')
				job_format_add_licenses( params.format_list,
						     field_size,
						     right_justify, suffix );
			else if (field[0] == 'x')
				job_format_add_exc_nodes( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'X')
				job_format_add_core_spec( params.format_list,
							  field_size,
							  right_justify,
							  suffix );
			else if (field[0] == 'z')
				job_format_add_num_sct( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
			else if (field[0] == 'Z')
				job_format_add_work_dir( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
			else if (format_all)
				;	/* ignore */
			else {
				prefix = xstrdup("%");
				xstrcat(prefix, token);
				xfree(suffix);
				suffix = prefix;

				job_format_add_invalid( params.format_list,
							   field_size,
							   right_justify,
							   suffix );
				error( "Invalid job format specification: %c",
				       field[0] );
			}
		}
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

	assert (token != NULL);

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
	ListIterator iterator;
	int i;
	char *part, *name;
	uint32_t *user;
	enum job_states *state_id;
	squeue_job_step_t *job_step_id;
	char hostlist[8192];

	if (params.nodes) {
		hostset_ranged_string(params.nodes, sizeof(hostlist)-1,
				      hostlist);
	} else
		hostlist[0] = '\0';

	printf( "-----------------------------\n" );
	printf( "all         = %s\n", params.all_flag ? "true" : "false");
	printf( "array       = %s\n", params.array_flag ? "true" : "false");
	printf( "format      = %s\n", params.format );
	printf( "iterate     = %d\n", params.iterate );
	printf( "job_flag    = %d\n", params.job_flag );
	printf( "jobs        = %s\n", params.jobs );
	printf( "names       = %s\n", params.names );
	printf( "nodes       = %s\n", hostlist ) ;
	printf( "partitions  = %s\n", params.partitions ) ;
	printf( "reservation = %s\n", params.reservation ) ;
	printf( "sort        = %s\n", params.sort ) ;
	printf( "start_flag  = %d\n", params.start_flag );
	printf( "states      = %s\n", params.states ) ;
	printf( "step_flag   = %d\n", params.step_flag );
	printf( "steps       = %s\n", params.steps );
	printf( "users       = %s\n", params.users );
	printf( "verbose     = %d\n", params.verbose );

	if ((params.verbose > 1) && params.job_list) {
		i = 0;
		iterator = list_iterator_create( params.job_list );
		while ( (job_step_id = list_next( iterator )) ) {
			if (job_step_id->array_id == (uint16_t) NO_VAL) {
				printf( "job_list[%d] = %u\n", i++,
					job_step_id->job_id );
			} else {
				printf( "job_list[%d] = %u_%u\n", i++,
					job_step_id->job_id,
					job_step_id->array_id );
			}
		}
		list_iterator_destroy( iterator );
	}


	if ((params.verbose > 1) && params.name_list) {
		i = 0;
		iterator = list_iterator_create( params.name_list );
		while ( (name = list_next( iterator )) ) {
			printf( "name_list[%d] = %u\n", i++, *name);
		}
		list_iterator_destroy( iterator );
	}

	if ((params.verbose > 1) && params.part_list) {
		i = 0;
		iterator = list_iterator_create( params.part_list );
		while ( (part = list_next( iterator )) ) {
			printf( "part_list[%d] = %s\n", i++, part);
		}
		list_iterator_destroy( iterator );
	}

	if ((params.verbose > 1) && params.state_list) {
		i = 0;
		iterator = list_iterator_create( params.state_list );
		while ( (state_id = list_next( iterator )) ) {
			printf( "state_list[%d] = %s\n",
				i++, job_state_string( *state_id ));
		}
		list_iterator_destroy( iterator );
	}

	if ((params.verbose > 1) && params.step_list) {
		i = 0;
		iterator = list_iterator_create( params.step_list );
		while ( (job_step_id = list_next( iterator )) ) {
			if (job_step_id->array_id == (uint16_t) NO_VAL) {
				printf( "step_list[%d] = %u.%u\n", i++,
					job_step_id->job_id,
					job_step_id->step_id );
			} else {
				printf( "step_list[%d] = %u_%u.%u\n", i++,
					job_step_id->job_id,
					job_step_id->array_id,
					job_step_id->step_id );
			}
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
} ;


/*
 * _build_job_list- build a list of job_ids
 * IN str - comma separated list of job_ids
 * RET List of job_ids (uint32_t)
 */
static List
_build_job_list( char* str )
{
	List my_list;
	char *end_ptr = NULL, *job = NULL, *tmp_char = NULL;
	char *my_job_list = NULL;
	int job_id, array_id;
	squeue_job_step_t *job_step_id;

	if ( str == NULL )
		return NULL;
	my_list = list_create( NULL );
	my_job_list = xstrdup( str );
	job = strtok_r( my_job_list, ",", &tmp_char );
	while (job) {
		job_id = strtol( job, &end_ptr, 10 );
		if (end_ptr[0] == '_')
			array_id = strtol( end_ptr + 1, &end_ptr, 10 );
		else
			array_id = (uint16_t) NO_VAL;
		if (job_id <= 0) {
			error( "Invalid job id: %s", job );
			exit( 1 );
		}

		job_step_id = xmalloc( sizeof( squeue_job_step_t ) );
		job_step_id->job_id   = (uint32_t) job_id;
		job_step_id->array_id = (uint16_t) array_id;
		list_append( my_list, job_step_id );
		job = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

/*
 * _build_str_list - convert a string of comma-separated elements
 *		     into a list of strings
 * IN str - comma separated list of strings
 * RET List of strings
 */
static List
_build_str_list( char* str )
{
	List my_list;
	char *part = NULL, *tmp_char = NULL, *my_part_list = NULL;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_part_list = xstrdup( str );
	part = strtok_r( my_part_list, ",", &tmp_char );
	while (part) {
		list_append( my_list, part );
		part = strtok_r( NULL, ",", &tmp_char );
	}
	/* NOTE: Do NOT xfree my_part_list or the elements just added to the
	 * list will also be freed. */
	return my_list;
}

/*
 * _build_state_list - build a list of job states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static List
_build_state_list( char* str )
{
	List my_list;
	char *state = NULL, *tmp_char = NULL, *my_state_list = NULL;
	uint16_t *state_id = NULL;

	if ( str == NULL)
		return NULL;
	if ( strcasecmp( str, "all" ) == 0 )
		return _build_all_states_list ();

	my_list = list_create( NULL );
	my_state_list = xstrdup( str );
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state)
	{
		state_id = xmalloc( sizeof( uint16_t ) );
		if ( _parse_state( state, state_id ) != SLURM_SUCCESS )
			exit( 1 );
		list_append( my_list, state_id );
		state = strtok_r( NULL, ",", &tmp_char );
	}
	return my_list;

}

/*
 * _build_all_states_list - build a list containing all possible job states
 * RET List of uint16_t values
 */
static List
_build_all_states_list( void )
{
	List my_list;
	int i;
	uint16_t * state_id;

	my_list = list_create( NULL );
	for (i = 0; i<JOB_END; i++) {
		state_id = xmalloc( sizeof(uint16_t) );
		*state_id = (uint16_t) i;
		list_append( my_list, state_id );
	}
	state_id = xmalloc( sizeof(uint16_t) );
	*state_id = (uint16_t) JOB_COMPLETING;
	list_append( my_list, state_id );
	state_id = xmalloc( sizeof(uint16_t) );
	*state_id = (uint16_t) JOB_CONFIGURING;
	list_append( my_list, state_id );
	return my_list;

}

/*
 * _build_step_list- build a list of job/step_ids
 * IN str - comma separated list of job_id[array_id].step_id values
 * RET List of job/step_ids (structure of uint32_t's)
 */
static List
_build_step_list( char* str )
{
	List my_list;
	char *end_ptr = NULL, *step = NULL, *tmp_char = NULL, *tmps_char = NULL;
	char *job_name = NULL, *step_name = NULL, *my_step_list = NULL;
	int job_id, array_id, step_id;
	squeue_job_step_t *job_step_id = NULL;

	if ( str == NULL)
		return NULL;
	my_list = list_create( NULL );
	my_step_list = xstrdup( str );
	step = strtok_r( my_step_list, ",", &tmp_char );
	while (step) {
		job_name = strtok_r( step, ".", &tmps_char );
		step_name = strtok_r( NULL, ".", &tmps_char );
		job_id = strtol( job_name, &end_ptr, 10 );
		if (end_ptr[0] == '_')
			array_id = strtol( end_ptr + 1, &end_ptr, 10 );
		else
			array_id = (uint16_t) NO_VAL;
		if (step_name == NULL) {
			error ( "Invalid job_step id: %s.??",
				 job_name );
			exit( 1 );
		}
		step_id = strtol( step_name, &end_ptr, 10 );
		if ((job_id <= 0) || (step_id < 0)) {
			error( "Invalid job_step id: %s.%s",
				job_name, step_name );
			exit( 1 );
		}
		job_step_id = xmalloc( sizeof( squeue_job_step_t ) );
		job_step_id->job_id   = (uint32_t) job_id;
		job_step_id->array_id = (uint16_t) array_id;
		job_step_id->step_id  = (uint32_t) step_id;
		list_append( my_list, job_step_id );
		step = strtok_r( NULL, ",", &tmp_char);
	}
	return my_list;
}

/*
 * _build_user_list- build a list of UIDs
 * IN str - comma separated list of user names
 * RET List of UIDs (uint32_t)
 */
static List
_build_user_list( char* str )
{
	List my_list;
	char *user = NULL;
	char *tmp_char = NULL, *my_user_list = NULL;

	if ( str == NULL )
		return NULL;
	my_list = list_create( NULL );
	my_user_list = xstrdup( str );
	user = strtok_r( my_user_list, ",", &tmp_char );
	while (user) {
		uid_t some_uid;
		if ( uid_from_string( user, &some_uid ) == 0 ) {
			uint32_t *user_id = NULL;
			user_id = xmalloc( sizeof( uint32_t ));
			*user_id = (uint32_t) some_uid;
			list_append( my_list, user_id );
		} else {
			error( "Invalid user: %s\n", user);
		}
		user = strtok_r (NULL, ",", &tmp_char);
	}
	return my_list;
}

static void _usage(void)
{
	printf("\
Usage: squeue [-i seconds] [-n name] [-o format] [-p partitions]\n\
              [-R reservation] [-S fields] [--start] [-t states]\n\
              [-u user_name] [--usage] [-w nodes] [-ahjlsv]\n");
}

static void _help(void)
{
	printf("\
Usage: squeue [OPTIONS]\n\
  -A, --account=account(s)        comma separated list of accounts\n\
				  to view, default is all accounts\n\
  -a, --all                       display jobs in hidden partitions\n\
  -h, --noheader                  no headers on output\n\
      --hide                      do not display jobs in hidden partitions\n\
  -i, --iterate=seconds           specify an interation period\n\
  -j, --job=job(s)                comma separated list of jobs IDs\n\
				  to view, default is all\n\
  -l, --long                      long report\n\
  -M, --clusters=cluster_name     cluster to issue commands to.  Default is\n\
                                  current cluster.  cluster with no name will\n\
                                  reset to default.\n\
  -n, --name=job_name(s)          comma separated list of job names to view\n\
  -o, --format=format             format specification\n\
  -p, --partition=partition(s)    comma separated list of partitions\n\
				  to view, default is all partitions\n\
  -q, --qos=qos(s)                comma separated list of qos's\n\
				  to view, default is all qos's\n\
  -R, --reservation=name          reservation to view, default is all\n\
  -s, --step=step(s)              comma separated list of job steps\n\
				  to view, default is all\n\
  -S, --sort=fields               comma separated list of fields to sort on\n\
      --start                     print expected start times of pending jobs\n\
  -t, --states=states             comma separated list of states to view,\n\
				  default is pending and running,\n\
				  '--states=all' reports all states\n\
  -u, --user=user_name(s)         comma separated list of users to view\n\
      --name=job_name(s)          comma separated list of job names to view\n\
  -v, --verbose                   verbosity level\n\
  -V, --version                   output version information and exit\n\
  -w, --nodelist=hostlist         list of nodes to view, default is \n\
				  all nodes\n\
\nHelp options:\n\
  --help                          show this help message\n\
  --usage                         display a brief summary of squeue options\n");
}
