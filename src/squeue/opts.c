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

#include "src/common/data.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/common/ref.h"
#include "src/common/uid.h"
#include "src/interfaces/serializer.h"

#include "src/squeue/squeue.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP         0x100
#define OPT_LONG_USAGE        0x101
#define OPT_LONG_HIDE         0x102
#define OPT_LONG_START        0x103
#define OPT_LONG_NOCONVERT    0x104
#define OPT_LONG_LOCAL        0x106
#define OPT_LONG_SIBLING      0x107
#define OPT_LONG_FEDR         0x108
#define OPT_LONG_ME           0x109
#define OPT_LONG_JSON         0x110
#define OPT_LONG_YAML         0x111
#define OPT_LONG_AUTOCOMP     0x112
#define OPT_LONG_NOTME        0x113
#define OPT_LONG_HELPSTATE    0x114
#define OPT_LONG_HELPFORMAT   0x115
#define OPT_LONG_HELPFORMAT2  0x116

/* FUNCTIONS */
static list_t *_build_job_list(char *str);
static list_t *_build_str_list(char *str);
static list_t *_build_state_list(char *str);
static list_t *_build_step_list(char *str);
static list_t *_build_user_list(char *str);
static char *_get_prefix(char *token);
static void  _help( void );
static void _help_format(bool step);
static void _help_format2(bool step);
static int   _parse_state( char* str, uint32_t* states );
static void  _parse_token( char *token, char *field, int *field_size,
			   bool *right_justify, char **suffix);
static void _parse_long_token( char *token, char *sep, int *field_size,
			       bool *right_justify, char **suffix);
static void _print_job_states(void);
static void  _print_options( void );
static void  _usage( void );
static void _filter_nodes(void);
static list_t *_load_clusters_nodes(void);
static void _node_info_list_del(void *data);
static char *_map_node_name(list_t *clusters_node_info, char *name);

decl_static_data(help_txt);
decl_static_data(usage_txt);

/*
 * parse_command_line
 */
extern void parse_command_line(int argc, char **argv)
{
	char *env_val = NULL;
	bool override_format_env = false;
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"accounts",   required_argument, 0, 'A'},
		{"all",        no_argument,       0, 'a'},
		{"array",      no_argument,       0, 'r'},
		{"Format",     required_argument, 0, 'O'},
		{"format",     required_argument, 0, 'o'},
		{"federation", no_argument,       0, OPT_LONG_FEDR},
		{"help",       no_argument,       0, OPT_LONG_HELP},
		{"helpstate",  no_argument,       0, OPT_LONG_HELPSTATE},
		{"helpformat", no_argument,       0, OPT_LONG_HELPFORMAT},
		{"helpFormat", no_argument,       0, OPT_LONG_HELPFORMAT2},
		{"hide",       no_argument,       0, OPT_LONG_HIDE},
		{"iterate",    required_argument, 0, 'i'},
		{"jobs",       optional_argument, 0, 'j'},
		{"local",      no_argument,       0, OPT_LONG_LOCAL},
		{"long",       no_argument,       0, 'l'},
		{"licenses",   required_argument, 0, 'L'},
		{"cluster",    required_argument, 0, 'M'},
		{"clusters",   required_argument, 0, 'M'},
		{"me",         no_argument,       0, OPT_LONG_ME},
		{"name",       required_argument, 0, 'n'},
                {"noconvert",  no_argument,       0, OPT_LONG_NOCONVERT},
		{"node",       required_argument, 0, 'w'},
		{"nodes",      required_argument, 0, 'w'},
		{"nodelist",   required_argument, 0, 'w'},
		{"noheader",   no_argument,       0, 'h'},
		{"notme",      no_argument,       0, OPT_LONG_NOTME},
		{"partitions", required_argument, 0, 'p'},
		{"priority",   no_argument,       0, 'P'},
		{"qos",        required_argument, 0, 'q'},
		{"reservation",required_argument, 0, 'R'},
		{"sib",        no_argument,       0, OPT_LONG_SIBLING},
		{"sibling",    no_argument,       0, OPT_LONG_SIBLING},
		{"sort",       required_argument, 0, 'S'},
		{"start",      no_argument,       0, OPT_LONG_START},
		{"steps",      optional_argument, 0, 's'},
		{"states",     required_argument, 0, 't'},
		{"usage",      no_argument,       0, OPT_LONG_USAGE},
		{"user",       required_argument, 0, 'u'},
		{"users",      required_argument, 0, 'u'},
		{"verbose",    no_argument,       0, 'v'},
		{"version",    no_argument,       0, 'V'},
		{"json", optional_argument, 0, OPT_LONG_JSON},
		{"yaml", optional_argument, 0, OPT_LONG_YAML},
		{NULL,         0,                 0, 0}
	};

	params.convert_flags = CONVERT_NUM_UNIT_EXACT;

	if (xstrstr(slurm_conf.fed_params, "fed_display"))
		params.federation_flag = true;

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
		params.local_flag = true;
	}
	if (getenv("SQUEUE_FEDERATION"))
		params.federation_flag = true;
	if (getenv("SQUEUE_LOCAL"))
		params.local_flag = true;
	if (getenv("SQUEUE_PRIORITY"))
		params.priority_flag = true;
	if (getenv("SQUEUE_SIB") || getenv("SQUEUE_SIBLING"))
		params.sibling_flag = true;
	while ((opt_char = getopt_long(argc, argv,
				       "A:ahi:j::lL:n:M:O:o:p:Pq:R:rs::S:t:u:U:vVw:",
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
		case (int) 'L':
			xfree(params.licenses);
			params.licenses = xstrdup(optarg);
			params.licenses_list = _build_str_list(params.licenses);
			break;
		case (int) 'M':
			if (params.clusters)
				FREE_NULL_LIST(params.clusters);
			if (!(params.clusters =
			      slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(params.clusters);
			params.local_flag = true;
			break;
		case (int) 'n':
			xfree(params.names);
			params.names = xstrdup(optarg);
			params.name_list = _build_str_list( params.names );
			break;
		case (int) 'O':
			xfree(params.format_long);
			if (params.format == NULL) {
				params.format_long = xstrdup(optarg);
			} else {
				error ("-O (--Format) is incompatible with -o "
				       "(--format)");
				exit(1);
			}
			override_format_env = true;
			break;

		case (int) 'o':
			xfree(params.format);
			if (params.format_long == NULL) {
				params.format = xstrdup(optarg);
			} else {
				error ("-o (--format) is incompatible with -O "
				       "(--Format)");
				exit(1);
			}
			override_format_env = true;
			break;
		case (int) 'p':
			xfree(params.partitions);
			params.partitions = xstrdup(optarg);
			params.part_list =
				_build_str_list( params.partitions );
			params.all_flag = true;
			break;
		case (int) 'P':
			params.priority_flag = true;
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
			setenv("SLURM_BITSTR_LEN", "0", 1);
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
		case OPT_LONG_FEDR:
			params.federation_flag = true;
			break;
		case OPT_LONG_HIDE:
			params.all_flag = false;
			break;
		case OPT_LONG_LOCAL:
			params.local_flag = true;
			break;
		case OPT_LONG_ME:
			xfree(params.users);
			xstrfmtcat(params.users, "%u", geteuid());
			params.user_list = _build_user_list(params.users);
			break;
		case OPT_LONG_SIBLING:
			params.sibling_flag = true;
			break;
		case OPT_LONG_START:
			params.start_flag = true;
			override_format_env = true;
			break;
		case OPT_LONG_NOCONVERT:
			params.convert_flags |= CONVERT_NUM_UNIT_NO;
			break;
		case OPT_LONG_NOTME:
			params.notme_flag = true;
			break;
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_JSON:
			params.mimetype = MIME_TYPE_JSON;
			params.data_parser = optarg;
			params.detail_flag = true;
			if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
				fatal("JSON plugin load failure");
			break;
		case OPT_LONG_YAML:
			params.mimetype = MIME_TYPE_YAML;
			params.data_parser = optarg;
			params.detail_flag = true;
			if (serializer_g_init(MIME_TYPE_YAML_PLUGIN, NULL))
				fatal("YAML plugin load failure");
			break;
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_HELPSTATE:
			_print_job_states();
			exit(0);
			break;
		case OPT_LONG_HELPFORMAT:
			_help_format(params.step_flag);
			exit(0);
			break;
		case OPT_LONG_HELPFORMAT2:
			_help_format2(params.step_flag);
			exit(0);
			break;
		}
	}

	if (params.long_list && params.format)
		fatal("Options -o(--format) and -l(--long) are mutually exclusive. Please remove one and retry.");

	if (!override_format_env) {
		if ((env_val = getenv("SQUEUE_FORMAT")))
			params.format = xstrdup(env_val);
		else if ((env_val = getenv("SQUEUE_FORMAT2")))
			params.format_long = xstrdup(env_val);
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

	if ( params.nodes )
		_filter_nodes();

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
	     ( env_val = getenv("SQUEUE_LICENSES") ) ) {
		params.licenses = xstrdup(env_val);
		params.licenses_list = _build_str_list( params.licenses );
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
			params.format = xstrdup("%.18i %.9P %.8j %.8u %.2t %.19S %.6D %20Y %R");
		if (params.sort == NULL)
			params.sort = xstrdup("S");
		if (params.states == NULL) {
			params.states = xstrdup("PD");
			params.state_list = _build_state_list( params.states );
		}
	}

	if (params.job_list && (list_count(params.job_list) == 1)) {
		squeue_job_step_t *job_step_ptr = list_peek(params.job_list);
		params.job_id = job_step_ptr->step_id.job_id;
	}
	if (params.user_list && (list_count(params.user_list) == 1)) {
		list_itr_t *iterator;
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

static const char *_job_state_list(void)
{
	int i;
	static char *state_names = NULL;

	if (state_names)
		return state_names;

	state_names = xstrdup(job_state_string(0));
	for (i = 1; i < JOB_END; i++) {
		xstrcat(state_names, ",");
		xstrcat(state_names, job_state_string(i));
	}
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_COMPLETING));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_CONFIGURING));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_RESIZING));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_RESV_DEL_HOLD));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_REQUEUE));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_REQUEUE_FED));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_REQUEUE_HOLD));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_REVOKED));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_SIGNALING));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_SPECIAL_EXIT));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_STAGE_OUT));
	xstrcat(state_names, ",");
	xstrcat(state_names, job_state_string(JOB_STOPPED));

	for (i = 0; i < strlen(state_names); i++)
		state_names[i] = tolower(state_names[i]);

	return state_names;
}

/*
 * _parse_state - convert job state name string to numeric value
 * IN str - state name
 * OUT states - enum job_states value corresponding to str
 * RET 0 or error code
 */
static int
_parse_state( char* str, uint32_t* states )
{
	if (job_state_num(str) != NO_VAL)
		return SLURM_SUCCESS;

	error("Invalid job state specified: %s", str);
	error("Valid job states include: %s\n", _job_state_list());
	return SLURM_ERROR;
}

static void _print_job_states(void)
{
	char *states = xstrdup(_job_state_list());

	for (uint32_t i = 0; states[i]; i++){
		if (states[i] == ',')
			states[i] = '\n';
	}

	if (states)
		printf("%s\n", states);

	xfree(states);
}

static fmt_data_job_t fmt_data_job[] = {
	{"Account", 'a', _print_job_account, 0},
	{"AccrueTime", 0, _print_job_accrue_time, 0},
	{"admin_comment", 0, _print_job_admin_comment, 0},
	{"AllocNodes", 0, _print_job_alloc_nodes, 0},
	{"AllocSID", 0, _print_job_alloc_sid, 0},
	{"ArrayJobId", 'F', _print_job_array_job_id, 0},
	{"ArrayTaskId", 'K', _print_job_array_task_id, 0},
	{"AssocId", 0, _print_job_assoc_id, 0},
	{"BatchFlag", 0, _print_job_batch_flag, 0},
	{"BatchHost", 'B', _print_job_batch_host, 0},
	{"BoardsPerNode", 0, _print_job_boards_per_node, 0},
	{"BurstBuffer", 0, _print_job_burst_buffer, 0},
	{"BurstBufferState", 0, _print_job_burst_buffer_state, 0},
	{"Cluster", 0, _print_job_cluster_name, 0},
	{"ClusterFeature", 0, _print_job_cluster_features, 0},
	{"Command", 'o', _print_job_command, 0},
	{"Comment", 'k', _print_job_comment, 0},
	{"Container", 0, _print_job_container, 0},
	{"ContainerId", 0, _print_job_container_id, 0},
	{"Contiguous", 'O', _print_job_contiguous, 0},
	{"Cores", 'I', _print_cores, 0},
	{"CoreSpec", 'X', _print_job_core_spec, 0},
	{"CPUsPerTask", 0, _print_job_cpus_per_task, 0},
	{"cpus-per-task", 0, _print_job_cpus_per_task, 0},
	{"cpus-per-tres", 0, _print_job_cpus_per_tres, 0},
	{"Deadline", 0, _print_job_deadline, 0},
	{"DelayBoot", 0, _print_job_delay_boot, 0},
	{"Dependency", 'E', _print_job_dependency, 0},
	{"DerivedEC", 0, _print_job_derived_ec, 0},
	{"EligibleTime", 0, _print_job_eligible_time, 0},
	{"EndTime", 'e', _print_job_time_end, 0},
	{"ExcNodes", 'x', _print_job_exc_nodes, 0},
	{"exit_code", 0, _print_job_exit_code, 0},
	{"Feature", 'f', _print_job_features, 0},
	{"Gres", 'b', _print_job_tres_per_node, FMT_FLAG_HIDDEN}, /* vestigial*/
	{"GroupId", 'G', _print_job_group_id, 0},
	{"GroupName", 'g', _print_job_group_name, 0},
	{"HetJobId", 0, _print_job_het_job_id, 0},
	{"HetJobIdSet", 0, _print_job_het_job_id_set, 0},
	{"HetJobOffset", 0, _print_job_het_job_offset, 0},
	{"JobArrayId", 'i', _print_job_job_id, 0},
	{"JobId", 'A', _print_job_job_id2, 0},
	{"LastSchedEval", 0, _print_job_last_sched_eval, 0},
	{"Licenses", 'W', _print_job_licenses, 0},
	{"MaxCPUs", 0, _print_job_max_cpus, 0},
	{"MaxNodes", 0, _print_job_max_nodes, 0},
	{"mem-per-tres", 0, _print_job_mem_per_tres, 0},
	{"MCSLabel", 0, _print_job_mcs_label, 0},
	{"MinCPUs", 'c', _print_pn_min_cpus, 0},
	{"MinMemory", 'm', _print_pn_min_memory, 0},
	{"MinTime", 0, _print_job_min_time, 0},
	{"MinTmpDisk", 'd', _print_pn_min_tmp_disk, 0},
	{"Name", 'j', _print_job_name, 0},
	{"Network", 0, _print_job_network, 0},
	{"Nice", 'y', _print_job_nice, 0},
	{"NodeList", 'N', _print_job_nodes, 0},
	{"NTPerCore", 0, _print_job_ntasks_per_core, 0},
	{"NTPerNode", 0, _print_job_ntasks_per_node, 0},
	{"NTPerSocket", 0, _print_job_ntasks_per_socket, 0},
	{"NTPerBoard", 0, _print_job_ntasks_per_board, 0},
	{"NumCPUs", 'C', _print_job_num_cpus, 0},
	{"NumNodes", 'D', _print_job_num_nodes, 0},
	{"NumTasks", 0, _print_job_num_tasks, 0},
	{"Origin", 0, _print_job_fed_origin, 0},
	{"OriginRaw", 0, _print_job_fed_origin_raw, 0},
	{"OverSubscribe", 'h', _print_job_over_subscribe, 0},
	{"PackJobId", 0, _print_job_het_job_id, FMT_FLAG_HIDDEN},
	{"PackJobIdSet", 0, _print_job_het_job_id_set, FMT_FLAG_HIDDEN},
	{"PackJobOffset", 0, _print_job_het_job_offset, FMT_FLAG_HIDDEN},
	{"Partition", 'P', _print_job_partition, 0},
	{"PendingTime", 0, _print_job_time_pending, 0},
	{"PreemptTime", 0, _print_job_preempt_time, 0},
	{"Prefer", 0, _print_job_prefer, 0},
	{"Priority", 'p', _print_job_priority, 0},
	{"PriorityLong", 'Q', _print_job_priority_long, 0},
	{"Profile", 0, _print_job_profile, 0},
	{"QOS", 'q', _print_job_qos, 0},
	{"Reason", 'r', _print_job_reason, 0},
	{"ReasonList", 'R', _print_job_reason_list, 0},
	{"Reboot", 0, _print_job_reboot, 0},
	{"ReqNodes", 'n', _print_job_req_nodes, 0},
	{"ReqSwitch", 0, _print_job_req_switch, 0},
	{"Requeue", 0, _print_job_requeue, 0},
	{"Reservation", 'v', _print_job_reservation, 0},
	{"ResizeTime", 0, _print_job_resize_time, 0},
	{"RestartCnt", 0, _print_job_restart_cnt, 0},
	{"SchedNodes", 'Y', _print_job_schednodes, 0},
	{"SCT", 'z', _print_job_num_sct, 0},
	{"SiblingsActive", 0, _print_job_fed_siblings_active, 0},
	{"SiblingsActiveRaw", 0, _print_job_fed_siblings_active_raw, 0},
	{"SiblingsViable", 0, _print_job_fed_siblings_viable, 0},
	{"SiblingsViableRaw", 0, _print_job_fed_siblings_viable_raw, 0},
	{"Shared", 'h', _print_job_over_subscribe, FMT_FLAG_HIDDEN},
	{"Sockets", 'H', _print_sockets, 0},
	{"SPerBoard", 0, _print_job_sockets_per_board, 0},
	{"StartTime", 'S', _print_job_time_start, 0},
	{"State", 'T', _print_job_job_state, 0},
	{"StateCompact", 't', _print_job_job_state_compact, 0},
	{"StdErr", 0, _print_job_std_err, 0},
	{"StdIn", 0, _print_job_std_in, 0},
	{"StdOut", 0, _print_job_std_out, 0},
	{"SubmitTime", 'V', _print_job_time_submit, 0},
	{"system_comment", 0, _print_job_system_comment, 0},
	{"Threads", 'J', _print_threads, 0},
	{"TimeLeft", 'L', _print_job_time_left, 0},
	{"TimeLimit", 'l', _print_job_time_limit, 0},
	{"TimeUsed", 'M', _print_job_time_used, 0},
	{"Tres", 0, _print_job_tres_alloc, FMT_FLAG_HIDDEN},
	{"tres-alloc", 0, _print_job_tres_alloc, 0},
	{"tres-bind", 0, _print_job_tres_bind, 0},
	{"tres-freq", 0, _print_job_tres_freq, 0},
	{"tres-per-job", 0, _print_job_tres_per_job, 0},
	{"tres-per-node", 0, _print_job_tres_per_node,
	 FMT_FLAG_HIDDEN}, /* vestigial */
	{"tres-per-socket", 0, _print_job_tres_per_socket, 0},
	{"tres-per-task", 0, _print_job_tres_per_task, 0},
	{"UserId", 'U', _print_job_user_id, 0},
	{"UserName", 'u', _print_job_user_name, 0},
	{"Wait4Switch", 0, _print_job_wait4switch, 0},
	{"WCKey", 'w', _print_job_wckey, 0},
	{"WorkDir", 'Z', _print_job_work_dir, 0},
	{NULL, 0, NULL, 0},
};

static fmt_data_step_t fmt_data_step[] = {
	{"ArrayJobId", 0, _print_step_array_job_id, 0},
	{"ArrayTaskId", 0, _print_step_array_task_id, 0},
	{"Cluster", 0, _print_step_cluster_name, 0},
	{"Container", 0, _print_step_container, 0},
	{"ContainerId", 0, _print_step_container_id, 0},
	{"CPUFreq", 0, _print_step_cpu_freq, 0},
	{"cpus-per-tres", 0, _print_step_cpus_per_tres, 0},
	{"Gres", 0, _print_step_tres_per_node, FMT_FLAG_HIDDEN}, /* vestigial */
	{"JobId", 0, _print_step_job_id, 0},
	{"mem-per-tres", 0, _print_step_mem_per_tres, 0},
	{"Network", 0, _print_step_network, 0},
	{"Nodes", 0, _print_step_nodes, 0},
	{"NumCPUs", 0, _print_step_num_cpus, 0},
	{"NumTasks", 0, _print_step_num_tasks, 0},
	{"Partition", 0, _print_step_partition, 0},
	{"ResvPorts", 0, _print_step_resv_ports, 0},
	{"StartTime", 0, _print_step_time_start, 0},
	{"StepId", 0, _print_step_id, 0},
	{"StepName", 0, _print_step_name, 0},
	{"StepState", 0, _print_step_state, 0},
	{"TimeLimit", 0, _print_step_time_limit, 0},
	{"TimeUsed", 0, _print_step_time_used, 0},
	{"tres-bind", 0, _print_step_tres_bind, 0},
	{"tres-freq", 0, _print_step_tres_freq, 0},
	{"tres-per-job", 0, _print_step_tres_per_step, 0},
	{"tres-per-node", 0, _print_step_tres_per_node,
	 FMT_FLAG_HIDDEN}, /* vestigial */
	{"tres-per-socket", 0, _print_step_tres_per_socket, 0},
	{"tres-per-step", 0, _print_step_tres_per_step, 0},
	{"tres-per-task", 0, _print_step_tres_per_task, 0},
	{"UserId", 0, _print_step_user_id, 0},
	{"UserName", 0, _print_step_user_name, 0},
	{NULL, 'A', _print_step_num_tasks, 0},
	{NULL, 'b', _print_step_tres_per_node, FMT_FLAG_HIDDEN}, /* vestigial */
	{NULL, 'i', _print_step_id, 0},
	{NULL, 'j', _print_step_name, 0},
	{NULL, 'l', _print_step_time_limit, 0},
	{NULL, 'M', _print_step_time_used, 0},
	{NULL, 'N', _print_step_nodes, 0},
	{NULL, 'P', _print_step_partition, 0},
	{NULL, 'S', _print_step_time_start, 0},
	{NULL, 'u', _print_step_user_name, 0},
	{NULL, 'U', _print_step_user_id, 0},
	{NULL, 0, NULL, 0},
};

/*
 * parse_format - Take the user's format specification and use it to build
 *	build the format specifications (internalize it to print.c data
 *	structures)
 * IN format - user's format specification
 * RET zero or error code
 */
extern int parse_format(char *format)
{
	int field_size;
	bool right_justify;
	char *prefix = NULL, *suffix = NULL, *token = NULL;
	char *tmp_char = NULL, *tmp_format = NULL;
	char field[1];
	bool format_all = false;
	int i;
	bool found = false;

	if (format == NULL) {
		error ("Format option lacks specification.");
		exit( 1 );
	}

	params.format_list = list_create( NULL );
	if ((prefix = _get_prefix(format))) {
		if (params.step_flag) {
			step_format_add_prefix( params.format_list, 0, 0,
						prefix);
		} else {
			job_format_add_prefix( params.format_list, 0, 0,
					       prefix);
		}
	}

	if (!xstrcasecmp(format, "%all")) {
		xstrfmtcat(tmp_format, "%c%c", '%', 'a');
		for (i = 'b'; i <= 'z'; i++)
			xstrfmtcat(tmp_format, "|%c%c", '%', (char) i);
		for (i = 'A'; i <= 'Z'; i++)
			xstrfmtcat(tmp_format, "|%c%c", '%', (char) i);
		format_all = true;
	} else {
		tmp_format = xstrdup(format);
	}

	token = strtok_r( tmp_format, "%", &tmp_char);
	if (token && (format[0] != '%'))	/* toss header */
		token = strtok_r( NULL, "%", &tmp_char );
	while (token) {
		found = false;
		_parse_token( token, field, &field_size, &right_justify,
			      &suffix);

		if (params.step_flag) {
			for (i = 0; fmt_data_step[i].name || fmt_data_step[i].c;
			     i++) {
				if (field[0] == fmt_data_step[i].c) {
					found = true;
					step_format_add_function(
						params.format_list, field_size,
						right_justify, suffix,
						fmt_data_step[i].fn);
					break;
				}
			}
			if (found)
				; /* NO-OP */
			else if (format_all)
				xfree(suffix);	/* ignore */
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
			for (i = 0; fmt_data_job[i].name || fmt_data_job[i].c;
			     i++) {
				if (field[0] == fmt_data_job[i].c) {
					found = true;
					job_format_add_function(
						params.format_list, field_size,
						right_justify, suffix,
						fmt_data_job[i].fn);
					break;
				}
			}
			if (found)
				; /* NO-OP */
			else if (format_all)
				xfree(suffix);	/* ignore */
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

extern int parse_long_format(char *format_long)
{
	int field_size;
	bool right_justify;
	char *tmp_format = NULL, *token = NULL, *str_tmp = NULL;
	char *sep = NULL;
	char* suffix = NULL;
	bool found = false;
	int i = 0;

	if (format_long == NULL) {
		error("Format long option lacks specification");
		exit( 1 );
	}

	params.format_list = list_create(NULL);
	tmp_format = xstrdup(format_long);
	token = strtok_r(tmp_format, ",",&str_tmp);
	while (token) {
		found = false;
		_parse_long_token( token, sep, &field_size, &right_justify,
				   &suffix);

		if (params.step_flag) {
			for (i = 0; fmt_data_step[i].name || fmt_data_step[i].c;
			     i++) {
				if (!xstrcasecmp(token, fmt_data_step[i].name)) {
					found = true;
					step_format_add_function(
						params.format_list, field_size,
						right_justify, suffix,
						fmt_data_step[i].fn);
					break;
				}
			}
			if (!found) {
				step_format_add_invalid( params.format_list,
							 field_size,
							 right_justify,
							 suffix );
				error ( "Invalid job step format "
					"specification: %s",
					token );
			}
		} else {
			for (i = 0; fmt_data_job[i].name || fmt_data_job[i].c;
			     i++) {
				if (!xstrcasecmp(token, fmt_data_job[i].name)) {
					found = true;
					job_format_add_function(
						params.format_list, field_size,
						right_justify, suffix,
						fmt_data_job[i].fn);
					break;
				}
			}
			if (!found) {
				job_format_add_invalid( params.format_list,
							field_size,
							right_justify,
							suffix );
				error( "Invalid job format specification: %s",
				       token );
			}
		}

		token = strtok_r(NULL, ",", &str_tmp);
	}

	xfree(tmp_format);
	return SLURM_SUCCESS;
}


/* Take a format specification and copy out it's prefix
 * IN/OUT token - input specification, everything before "%" is removed
 * RET - everything before "%" in the token
 */
static char *_get_prefix(char *token)
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
 * OUT suffix - string containing everything after the field specification
 */
static void _parse_token(char *token, char *field, int *field_size,
			 bool *right_justify, char **suffix)
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

static void _parse_long_token(char *token, char *sep, int *field_size,
			      bool *right_justify, char **suffix)
{
	char *end_ptr = NULL, *ptr;

	*suffix = NULL;
	xassert(token);
	ptr = strchr(token, ':');
	if (ptr) {
		ptr[0] = '\0';
		if (ptr[1] == '.') {
			*right_justify = true;
			ptr++;
		} else {
			*right_justify = false;
		}
		*field_size = strtol(ptr + 1, &end_ptr, 10);
		if (end_ptr[0] != '\0')
			*suffix = xstrdup(end_ptr);
	} else {
		*right_justify = false;
		*field_size = 20;
	}
}

/* print the parameters specified */
static void _print_options(void)
{
	list_itr_t *iterator;
	int i;
	char *license, *name, *part;
	uint32_t *user;
	uint32_t *state_id;
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
	printf( "federation  = %s\n", params.federation_flag ? "true":"false");
	printf( "format      = %s\n", params.format );
	printf( "iterate     = %d\n", params.iterate );
	printf( "job_flag    = %d\n", params.job_flag );
	printf( "jobs        = %s\n", params.jobs );
	printf( "licenses    = %s\n", params.licenses );
	printf( "local       = %s\n", params.local_flag ? "true" : "false");
	printf( "names       = %s\n", params.names );
	printf( "nodes       = %s\n", hostlist ) ;
	printf( "partitions  = %s\n", params.partitions ) ;
	printf( "priority    = %s\n", params.priority_flag ? "true" : "false");
	printf( "reservation = %s\n", params.reservation ) ;
	printf( "sibling      = %s\n", params.sibling_flag ? "true" : "false");
	printf( "sort        = %s\n", params.sort ) ;
	printf( "start_flag  = %d\n", params.start_flag );
	printf( "states      = %s\n", params.states ) ;
	printf( "step_flag   = %d\n", params.step_flag );
	printf( "steps       = %s\n", params.steps );
	printf( "users       = %s\n", params.users );
	printf( "verbose     = %d\n", params.verbose );

	if (params.verbose <= 1)
		goto endit;
	if (params.job_list) {
		i = 0;
		iterator = list_iterator_create( params.job_list );
		while ( (job_step_id = list_next( iterator )) ) {
			if (job_step_id->array_id == NO_VAL) {
				printf( "job_list[%d] = %u\n", i++,
					job_step_id->step_id.job_id );
			} else {
				printf( "job_list[%d] = %u_%u\n", i++,
					job_step_id->step_id.job_id,
					job_step_id->array_id );
			}
		}
		list_iterator_destroy( iterator );
	}


	if (params.name_list) {
		i = 0;
		iterator = list_iterator_create( params.name_list );
		while ( (name = list_next( iterator )) ) {
			printf( "name_list[%d] = %u\n", i++, *name);
		}
		list_iterator_destroy( iterator );
	}

	if (params.licenses_list) {
		i = 0;
		iterator = list_iterator_create( params.licenses_list );
		while ( (license = list_next( iterator )) ) {
			printf( "licenses_list[%d] = %s\n", i++, license);
		}
		list_iterator_destroy( iterator );
	}

	if (params.part_list) {
		i = 0;
		iterator = list_iterator_create( params.part_list );
		while ( (part = list_next( iterator )) ) {
			printf( "part_list[%d] = %s\n", i++, part);
		}
		list_iterator_destroy( iterator );
	}

	if (params.all_states) {
		printf( "state_list = all\n");
	} else if (params.state_list) {
		i = 0;
		iterator = list_iterator_create( params.state_list );
		while ( (state_id = list_next( iterator )) ) {
			printf( "state_list[%d] = %s\n",
				i++, job_state_string( *state_id ));
		}
		list_iterator_destroy( iterator );
	}

	if (params.step_list) {
		char tmp_char[34];
		i = 0;
		iterator = list_iterator_create( params.step_list );
		while ( (job_step_id = list_next( iterator )) ) {
			if (job_step_id->array_id == NO_VAL) {
				log_build_step_id_str(&job_step_id->step_id,
						      tmp_char,
						      sizeof(tmp_char),
						      STEP_ID_FLAG_NO_PREFIX);
				printf( "step_list[%d] = %s\n", i++,
					tmp_char);
			} else {
				log_build_step_id_str(&job_step_id->step_id,
						      tmp_char,
						      sizeof(tmp_char),
						      (STEP_ID_FLAG_NO_PREFIX |
						       STEP_ID_FLAG_NO_JOB));
				printf( "step_list[%d] = %u_%u.%s\n", i++,
					job_step_id->step_id.job_id,
					job_step_id->array_id,
					tmp_char);
			}
		}
		list_iterator_destroy( iterator );
	}

	if (params.user_list) {
		i = 0;
		iterator = list_iterator_create( params.user_list );
		while ( (user = list_next( iterator )) ) {
			printf( "user_list[%d] = %u\n", i++, *user);
		}
		list_iterator_destroy( iterator );
	}
endit:
	printf( "-----------------------------\n\n\n" );
} ;


/*
 * _build_job_list- build a list of job_ids
 * IN str - comma separated list of job_ids
 * RET List of job_ids (uint32_t)
 */
static list_t *_build_job_list(char *str)
{
	list_t *my_list;
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
			array_id = NO_VAL;
		if (job_id <= 0) {
			error( "Invalid job id: %s", job );
			exit( 1 );
		}

		job_step_id = xmalloc( sizeof( squeue_job_step_t ) );
		job_step_id->step_id.job_id   = job_id;
		job_step_id->array_id = array_id;
		list_append( my_list, job_step_id );
		job = strtok_r (NULL, ",", &tmp_char);
	}
	xfree(my_job_list);
	return my_list;
}

/*
 * _build_str_list - convert a string of comma-separated elements
 *		     into a list of strings
 * IN str - comma separated list of strings
 * RET List of strings
 */
static list_t *_build_str_list(char *str)
{
	list_t *my_list;
	char *elem, *tok = NULL, *tmp_char = NULL, *my_str = NULL;

	if (str == NULL)
		return NULL;
	my_list = list_create(NULL);
	my_str = xstrdup(str);
	tok = strtok_r(my_str, ",", &tmp_char);
	while (tok) {
		elem = xstrdup(tok);
		list_append(my_list, elem);
		tok = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_str);
	return my_list;
}

/*
 * _build_state_list - build a list of job states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static list_t *_build_state_list(char *str)
{
	list_t *my_list;
	char *state = NULL, *tmp_char = NULL, *my_state_list = NULL;
	uint32_t *state_id = NULL;

	if (str == NULL)
		return NULL;
	if (!xstrcasecmp(str, "all")) {
		params.all_states = true;
		return NULL;
	}
	params.all_states = false;

	my_list = list_create(NULL);
	my_state_list = xstrdup(str);
	state = strtok_r( my_state_list, ",", &tmp_char );
	while (state) {
		state_id = xmalloc(sizeof(uint32_t));
		if (_parse_state(state, state_id) != SLURM_SUCCESS)
			exit(1);
		list_append(my_list, state_id);
		state = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_state_list);
	return my_list;

}

/*
 * _build_step_list- build a list of job/step_ids
 * IN str - comma separated list of job_id[array_id].step_id values
 * RET List of job/step_ids (structure of uint32_t's)
 */
static list_t *_build_step_list(char *str)
{
	list_t *my_list;
	char *end_ptr = NULL, *step = NULL, *tmp_char = NULL, *tmps_char = NULL;
	char *job_name = NULL, *step_name = NULL, *my_step_list = NULL;
	int job_id, array_id, step_id;
	squeue_job_step_t *job_step_id = NULL;

	if (str == NULL)
		return NULL;

	my_list = list_create(NULL);
	my_step_list = xstrdup(str);
	step = strtok_r(my_step_list, ",", &tmp_char);
	while (step) {
		job_name = strtok_r(step, ".", &tmps_char);
		if (job_name == NULL)
			break;
		step_name = strtok_r(NULL, ".", &tmps_char);
		job_id = strtol(job_name, &end_ptr, 10);
		if (end_ptr[0] == '_')
			array_id = strtol(end_ptr + 1, &end_ptr, 10);
		else
			array_id = NO_VAL;
		if (step_name == NULL) {
			error("Invalid job_step id: %s.??", job_name);
			exit(1);
		}
		step_id = strtol( step_name, &end_ptr, 10 );
		if ((job_id <= 0) || (step_id < 0)) {
			error("Invalid job_step id: %s.%s",
			      job_name, step_name);
			exit(1);
		}
		job_step_id = xmalloc(sizeof(squeue_job_step_t));
		job_step_id->step_id.job_id   = job_id;
		job_step_id->array_id = array_id;
		job_step_id->step_id.step_id  = step_id;
		list_append(my_list, job_step_id);
		step = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_step_list);
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

	if (str == NULL)
		return NULL;

	my_list = list_create(NULL);
	my_user_list = xstrdup(str);
	user = strtok_r(my_user_list, ",", &tmp_char);
	while (user) {
		uid_t some_uid;
		if (uid_from_string(user, &some_uid) == 0) {
			uint32_t *user_id = NULL;
			user_id = xmalloc(sizeof(uint32_t));
			*user_id = (uint32_t) some_uid;
			list_append(my_list, user_id);
		} else {
			error("Invalid user: %s\n", user);
		}
		user = strtok_r(NULL, ",", &tmp_char);
	}
	xfree(my_user_list);
	return my_list;
}

static void _help(void)
{
	char *txt;
	static_ref_to_cstring(txt, help_txt);
	printf("%s", txt);
	xfree(txt);
}

static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	printf("%s", txt);
	xfree(txt);
}

static void _print_job_fmt_fields(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data_job[i].c || fmt_data_job[i].name; i++) {
		if (!fmt_data_job[i].c)
			continue;
		if (fmt_data_job[i].flags & FMT_FLAG_HIDDEN)
			continue;

		if (cnt & 8) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%%%-5c", fmt_data_job[i].c);
	}
	printf("\n");
}

static void _print_step_fmt_fields(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data_step[i].c || fmt_data_step[i].name; i++) {
		if (!fmt_data_step[i].c)
			continue;
		if (fmt_data_step[i].flags & FMT_FLAG_HIDDEN)
			continue;

		if (cnt & 8) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%%%-5c", fmt_data_step[i].c);
	}
	printf("\n");
}

static void _help_format(bool step_flag)
{
	if (step_flag)
		_print_step_fmt_fields();
	else
		_print_job_fmt_fields();
}

static void _print_job_fmt_fields2(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data_job[i].c || fmt_data_job[i].name; i++) {
		if (!fmt_data_job[i].name)
			continue;
		if (fmt_data_job[i].flags & FMT_FLAG_HIDDEN)
			continue;

		if (cnt & 4) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%-20s", fmt_data_job[i].name);
	}
	printf("\n");
}

static void _print_step_fmt_fields2(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data_step[i].c || fmt_data_step[i].name; i++) {
		if (!fmt_data_step[i].name)
			continue;
		if (fmt_data_step[i].flags & FMT_FLAG_HIDDEN)
			continue;

		if (cnt & 4) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%-20s", fmt_data_step[i].name);
	}
	printf("\n");
}

static void _help_format2(bool step_flag)
{
	if (step_flag)
		_print_step_fmt_fields2();
	else
		_print_job_fmt_fields2();
}

/*
 * Validate and assign filtered nodes to params.nodes.
 */
static void _filter_nodes(void)
{
	char *name = NULL, *nodename = NULL;
	hostset_t *nodenames = hostset_create(NULL);
	list_t *clusters_nodes = NULL;

	/* Retrieve node_info from controllers */
	if (!(clusters_nodes = _load_clusters_nodes()))
		exit(1);

	/* Map all node names specified with -w, if known to any controller. */
	while ((name = hostset_shift(params.nodes))) {
		if (!(nodename = _map_node_name(clusters_nodes, name))) {
			free(name);
			hostset_destroy(params.nodes);
			FREE_NULL_LIST(clusters_nodes);
			exit(1);
		}
		hostset_insert(nodenames, nodename);
		free(name);
		xfree(nodename);
	}
	FREE_NULL_LIST(clusters_nodes);

	/* Replace params.nodes with the new one */
	hostset_destroy(params.nodes);
	params.nodes = nodenames;
}

/*
 * ListDelF for a list of node_info_msg_t.
 */
static void _node_info_list_del(void *data)
{
	node_info_msg_t *node_info_ptr = data;

	slurm_free_node_info_msg(node_info_ptr);
}

/*
 * Retrieve node_info_msg_t for params.clusters or just local cluster.
 * RET: List of all needed node_info_msg_t or NULL if any fail
 *
 * NOTE: caller must free the returned list if not NULL.
 */
static list_t *_load_clusters_nodes(void)
{
	list_t *node_info_list = NULL;
	list_itr_t *iter = NULL;
	node_info_msg_t *node_info = NULL;

	node_info_list = list_create(_node_info_list_del);

	if (params.clusters)
		iter = list_iterator_create(params.clusters);

	do {
		if (slurm_load_node(0, &node_info, SHOW_ALL)) {
			slurm_perror("slurm_load_node error");
			FREE_NULL_LIST(node_info_list);
			break;
		}

		list_append(node_info_list, node_info);
	} while (params.clusters && (working_cluster_rec = list_next(iter)));

	/*
	 * Don't need to reset working_cluster_rec here. Nobody uses it in
	 * parse_command_line(), and it's already reset later in main().
	 */
	if (params.clusters)
		list_iterator_destroy(iter);

	return node_info_list;
}

/*
 * Map name into NodeName, and handle the special "localhost" case.
 * IN: pointer to an array of pointers to node_info_msg_t
 * IN: input node name
 * RET: mapped node name if valid, NULL otherwise
 *
 * NOTE: caller must xfree() the returned name.
 */
static char *_map_node_name(list_t *clusters_node_info, char *name)
{
	char *nodename = NULL;
	node_info_msg_t *node_info;
	list_itr_t *node_info_itr;

	if (!name)
		return NULL;

	/* localhost = use current host name */
	if (!xstrcasecmp("localhost", name)) {
		nodename = xmalloc(128);
		gethostname_short(nodename, 128);
	} else
		nodename = xstrdup(name);

	node_info_itr = list_iterator_create(clusters_node_info);

	while ((node_info = list_next(node_info_itr))) {
		for (int cc = 0; cc < node_info->record_count; cc++) {
			/*
			 * This can happen if the host is removed from DNS but
			 * still in slurm.conf
			 */
			if (!node_info->node_array[cc].name)
				continue;
			if (!xstrcmp(nodename,
				     node_info->node_array[cc].name) ||
			    !xstrcmp(nodename,
				     node_info->node_array[cc].node_hostname)) {
				xfree(nodename);
				list_iterator_destroy(node_info_itr);
				return xstrdup(node_info->node_array[cc].name);
			}
		}
	}

	error("Invalid node name %s", name);
	xfree(nodename);
	list_iterator_destroy(node_info_itr);
	return NULL;
}
