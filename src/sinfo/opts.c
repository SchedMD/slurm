/****************************************************************************\
 *  opts.c - sinfo command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurmdb.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/interfaces/serializer.h"

#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP      0x100
#define OPT_LONG_USAGE     0x101
#define OPT_LONG_HIDE      0x102
#define OPT_LONG_LOCAL     0x103
#define OPT_LONG_NOCONVERT 0x104
#define OPT_LONG_FEDR      0x105
#define OPT_LONG_JSON      0x106
#define OPT_LONG_YAML      0x107
#define OPT_LONG_AUTOCOMP  0x108
#define OPT_LONG_HELPFORMAT 0x109
#define OPT_LONG_HELPFORMAT2 0x110
#define OPT_LONG_HELPSTATE 0x111

/* FUNCTIONS */
static list_t *_build_state_list(char *str);
static list_t *_build_all_states_list(void);
static list_t *_build_part_list(char *parts);
static char *_get_prefix(char *token);
static void  _help( void );
static void _help_format(void);
static void _help_format2(void);
static int   _parse_format(char *);
static int   _parse_long_format(char *);
static bool  _node_state_equal(int state_id, const char *state_str);
static int   _node_state_id(char *str);
static const char * _node_state_list(void);
static void  _parse_token(char *token, char *field, int *field_size,
			  bool *right_justify, char **suffix);
static void  _parse_long_token(char *token, char *sep, int *field_size,
			       bool *right_justify, char **suffix);
static void  _print_options(void);
static void  _usage(void);
static void _print_node_states(void);

decl_static_data(help_txt);
decl_static_data(usage_txt);

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	char *env_val = NULL;
	int opt_char;
	int option_index;
	hostlist_t *host_list;
	bool long_form = false;
	bool opt_a_set = false, opt_p_set = false;
	bool env_a_set = false, env_p_set = false;
	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"all",       no_argument,       0, 'a'},
		{"dead",      no_argument,       0, 'd'},
		{"exact",     no_argument,       0, 'e'},
		{"federation",no_argument,       0, OPT_LONG_FEDR},
		{"future",    no_argument,       0, 'F'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"helpformat",no_argument,       0, OPT_LONG_HELPFORMAT},
		{"helpFormat",no_argument,       0, OPT_LONG_HELPFORMAT2},
		{"helpstate", no_argument,       0, OPT_LONG_HELPSTATE},
		{"hide",      no_argument,       0, OPT_LONG_HIDE},
		{"iterate",   required_argument, 0, 'i'},
		{"local",     no_argument,       0, OPT_LONG_LOCAL},
		{"long",      no_argument,       0, 'l'},
		{"cluster",   required_argument, 0, 'M'},
		{"clusters",  required_argument, 0, 'M'},
		{"nodes",     required_argument, 0, 'n'},
		{"noconvert", no_argument,       0, OPT_LONG_NOCONVERT},
		{"noheader",  no_argument,       0, 'h'},
		{"Node",      no_argument,       0, 'N'},
		{"format",    required_argument, 0, 'o'},
		{"Format",    required_argument, 0, 'O'},
		{"partition", required_argument, 0, 'p'},
		{"responding",no_argument,       0, 'r'},
		{"list-reasons", no_argument,    0, 'R'},
		{"summarize", no_argument,       0, 's'},
		{"sort",      required_argument, 0, 'S'},
		{"states",    required_argument, 0, 't'},
		{"reservation",no_argument,      0, 'T'},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
                {"json", optional_argument, 0, OPT_LONG_JSON},
                {"yaml", optional_argument, 0, OPT_LONG_YAML},
		{NULL,        0,                 0, 0}
	};

	params.convert_flags = CONVERT_NUM_UNIT_EXACT;

	if (xstrstr(slurm_conf.fed_params, "fed_display"))
		params.federation_flag = true;

	if (getenv("SINFO_ALL")) {
		env_a_set = true;
		params.all_flag = true;
	}
	if (getenv("SINFO_FEDERATION"))
		params.federation_flag = true;
	if (getenv("SINFO_FUTURE"))
		params.future_flag = true;
	if (getenv("SINFO_LOCAL"))
		params.local = true;
	if ( ( env_val = getenv("SINFO_PARTITION") ) ) {
		env_p_set = true;
		params.partition = xstrdup(env_val);
		params.part_list = _build_part_list(env_val);
		params.all_flag = true;
	}
	if (env_a_set && env_p_set) {
		error("Conflicting options, SINFO_ALL and SINFO_PARTITION, specified. "
		      "Please choose one or the other.");
		exit(1);
	}
	if ( ( env_val = getenv("SINFO_SORT") ) )
		params.sort = xstrdup(env_val);
	if ( ( env_val = getenv("SLURM_CLUSTERS") ) ) {
		xfree(params.cluster_names);
		params.cluster_names = xstrdup(env_val);
		params.local = true;
	}

	while ((opt_char = getopt_long(argc, argv,
				       "adeFhi:lM:n:No:O:p:rRsS:t:TvV",
				       long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr,
				"Try \"sinfo --help\" for more information\n");
			exit(1);
			break;
		case (int)'a':
			opt_a_set = true;
			xfree(params.partition);
			FREE_NULL_LIST(params.part_list);
			params.all_flag = true;
			break;
		case (int)'d':
			params.dead_nodes = true;
			break;
		case (int)'e':
			params.exact_match = true;
			break;
		case (int)'F':
			params.future_flag = true;
			break;
		case (int)'h':
			params.no_header = true;
			break;
		case (int) 'i':
			params.iterate= atoi(optarg);
			if (params.iterate <= 0) {
				error ("Error: invalid entry for "
				       "--iterate=%s", optarg);
				exit(1);
			}
			break;
		case (int) 'l':
			params.long_output = true;
			break;
		case (int) 'M':
			xfree(params.cluster_names);
			params.cluster_names = xstrdup(optarg);
			params.local = true;
			break;
		case OPT_LONG_NOCONVERT:
			params.convert_flags |= CONVERT_NUM_UNIT_NO;
			break;
		case (int) 'n':
			xfree(params.nodes);
			params.nodes = xstrdup(optarg);
			/*
			 * confirm valid nodelist entry
			 */
			host_list = hostlist_create(params.nodes);
			if (!host_list) {
				error("'%s' invalid entry for --nodes",
				      optarg);
				exit(1);
			}
			if (hostlist_count(host_list) == 1) {
				params.node_name_single = true;
				xfree(params.nodes);
				params.nodes =
				    hostlist_deranged_string_xmalloc(host_list);
			} else
				params.node_name_single = false;
			hostlist_destroy(host_list);
			break;
		case (int) 'N':
			params.node_flag = true;
			break;
		case (int) 'o':
			xfree(params.format);
			params.format = xstrdup(optarg);
			break;
		case (int) 'O':
			long_form = true;
			xfree(params.format);
			params.format = xstrdup(optarg);
			break;
		case (int) 'p':
			opt_p_set = true;
			xfree(params.partition);
			FREE_NULL_LIST(params.part_list);
			params.partition = xstrdup(optarg);
			params.part_list = _build_part_list(optarg);
			params.all_flag = true;
			break;
		case (int) 'r':
			params.responding_nodes = true;
			break;
		case (int) 'R':
			params.list_reasons = true;
			break;
		case (int) 's':
			params.summarize = true;
			break;
		case (int) 'S':
			xfree(params.sort);
			params.sort = xstrdup(optarg);
			break;
		case (int) 't':
			xfree(params.states);
			params.states = xstrdup(optarg);
			if (!(params.state_list = _build_state_list(optarg))) {
				error ("valid states: %s", _node_state_list ());
				exit (1);
			}
			break;
		case (int) 'T':
			params.reservation_flag = true;
			break;
		case (int) 'v':
			params.verbose++;
			break;
		case (int) 'V':
			print_slurm_version ();
			exit(0);
		case (int) OPT_LONG_FEDR:
			params.federation_flag = true;
			break;
		case (int) OPT_LONG_HELP:
			_help();
			exit(0);
		case (int) OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_HIDE:
			params.all_flag = false;
			break;
		case OPT_LONG_LOCAL:
			params.local = true;
			break;
		case OPT_LONG_JSON:
			params.mimetype = MIME_TYPE_JSON;
			params.data_parser = optarg;
			params.match_flags |= MATCH_FLAG_GRES_USED;
			if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
				fatal("JSON plugin load failure");
			break;
		case OPT_LONG_YAML:
			params.mimetype = MIME_TYPE_YAML;
			params.data_parser = optarg;
			params.match_flags |= MATCH_FLAG_GRES_USED;
			if (serializer_g_init(MIME_TYPE_YAML_PLUGIN, NULL))
				fatal("YAML plugin load failure");
			break;
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
			break;
		case OPT_LONG_HELPFORMAT:
			_help_format();
			exit(0);
			break;
		case OPT_LONG_HELPFORMAT2:
			_help_format2();
			exit(0);
		case OPT_LONG_HELPSTATE:
			_print_node_states();
			exit(0);
			break;
		}
	}

	if (params.long_output && params.format)
		fatal("Options -o(--format) and -l(--long) are mutually exclusive. Please remove one and retry.");

	if (opt_a_set && opt_p_set) {
		error("Conflicting options, -a and -p, specified. "
		      "Please choose one or the other.");
		exit(1);
	}

	FREE_NULL_LIST(params.clusters);
	if (params.cluster_names) {
		if (slurm_get_cluster_info(&(params.clusters),
					   params.cluster_names,
					   (params.federation_flag ?
					    SHOW_FEDERATION : SHOW_LOCAL))) {

			print_db_notok(params.cluster_names, 0);
			fatal("Could not get cluster information");
		}
		working_cluster_rec = list_peek(params.clusters);
		params.local = true;
	}

	params.cluster_flags = slurmdb_setup_cluster_flags();

	if (params.federation_flag && !params.clusters && !params.local) {
		void *ptr = NULL;
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			params.local = true;
			slurm_destroy_federation_rec(ptr);
		} else {
			params.fed = (slurmdb_federation_rec_t *) ptr;
		}
	}

	if ( params.format == NULL ) {
		params.def_format = true;
		if ( params.summarize ) {
			long_form = true;
			params.part_field_flag = true;	/* compute size later */
			params.format = xstrdup("partition:9 ,available:.5 ,time:.10 ,nodeaiot:.16 ,nodelist:0");
		} else if ( params.node_flag ) {
			long_form = true;
			params.node_field_flag = true;	/* compute size later */
			params.part_field_flag = true;	/* compute size later */
			params.format = params.long_output ?
				xstrdup("nodelist:0 ,nodes:.6 ,partition:.9 ,statelong:.11 ,cpus:4 ,socketcorethread:.8 ,memory:.6 ,disk:.8 ,weight:.6 ,features:.8 ,reason:20") :
				xstrdup("nodelist:0 ,nodes:.6 ,partition:.9 ,statecompact:6");

		} else if (params.list_reasons) {
			long_form = true;
			params.format = params.long_output ?
				xstrdup("reason:20 ,userlong:12 ,timestamp:19 ,statecompact:6 ,nodelist:0") :
				xstrdup("reason:20 ,user:9 ,timestamp:19 ,nodelist:0");

		} else if ((env_val = getenv ("SINFO_FORMAT"))) {
			params.format = xstrdup(env_val);

		} else if (params.fed) {
			long_form = true;
			params.part_field_flag = true;	/* compute size later */
			params.format = params.long_output ?
				xstrdup("partition:9 ,cluster:8 ,available:.5 ,time:.10 ,size:.10 ,root:.4 ,oversubscribe:.8 ,groups:.10 ,nodes:.6 ,statelong:.11 ,reservation:.11 ,nodelist:0") :
				xstrdup("partition:9 ,cluster:8 ,available:.5 ,time:.10 ,nodes:.6 ,statecompact:.6 ,nodelist:0");
		} else {
			long_form = true;
			params.part_field_flag = true;	/* compute size later */
			params.format = params.long_output ?
				xstrdup("partition:9 ,available:.5 ,time:.10 ,size:.10 ,root:.4 ,oversubscribe:.8 ,groups:.10 ,nodes:.6 ,statelong:.11 ,reservation:.11 ,nodelist:0") :
				xstrdup("partition:9 ,available:.5 ,time:.10 ,nodes:.6 ,statecompact:.6 ,nodelist:0");
		}
	}

	if (long_form)
		_parse_long_format(params.format);
	else
		_parse_format(params.format);

	if (params.list_reasons && (params.state_list == NULL)) {
		params.states = xstrdup("down,fail,drain");
		if (!(params.state_list = _build_state_list (params.states)))
			fatal ("Unable to build state list for -R!");
	}

	if (params.dead_nodes || params.nodes || params.partition ||
			params.responding_nodes ||params.state_list)
		params.filtering = true;

	if (params.verbose)
		_print_options();
}

static char *
_next_tok (char *sep, char **str)
{
	char *tok;

	/* push str past any leading separators */
	while ((**str != '\0') && (strchr(sep, **str) != NULL))
		(*str)++;

	if (**str == '\0')
		return NULL;

	/* assign token ptr */
	tok = *str;

	/* push str past token and leave pointing to first separator */
	while ((**str != '\0') && (strchr(sep, **str) == NULL))
		(*str)++;

	/* nullify consecutive separators and push str beyond them */
	while ((**str != '\0') && (strchr(sep, **str) != NULL))
		*(*str)++ = '\0';

	return (tok);
}

/*
 * _build_state_list - build a list of node states
 * IN str - comma separated list of job states
 * RET List of enum job_states values
 */
static list_t *_build_state_list(char *state_str)
{
	list_t *state_ids;
	char *orig, *str, *state;

	if (state_str == NULL)
		return NULL;
	if (xstrcasecmp (state_str, "all") == 0 )
		return _build_all_states_list ();

	orig = str = xstrdup (state_str);
	state_ids = list_create (NULL);

	if (xstrstr(state_str, "&"))
	    params.state_list_and = true;

	state = strtok_r(state_str, ",&", &str);
	while (state) {
		int *id = xmalloc (sizeof (*id));
		if ((*id = _node_state_id (state)) < 0) {
			error ("Bad state string: \"%s\"", state);
			return (NULL);
		}
		list_append (state_ids, id);
		state = strtok_r(NULL, ",&", &str);
	}

	xfree (orig);
	return (state_ids);
}

/*
 * _build_all_states_list - build a list containing all possible node states
 * RET List of enum job_states values
 */
static list_t *_build_all_states_list(void)
{
	list_t *my_list;
	int i;
	uint16_t *state_id;

	my_list = list_create( NULL );
	for (i = 0; i < NODE_STATE_END; i++) {
		state_id = xmalloc( sizeof( uint16_t ) );
		*state_id = (uint16_t) i;
		list_append( my_list, state_id );
	}

	state_id = xmalloc( sizeof( uint16_t ) );
	*state_id = NODE_STATE_DRAIN;
	list_append( my_list, state_id );

	state_id = xmalloc( sizeof( uint16_t ) );
	*state_id = NODE_STATE_COMPLETING;
	list_append( my_list, state_id );

	return my_list;
}

/*
 * _build_part_list - build a list of partition names
 * IN parts - comma separated list of partitions
 * RET List of partition names
 */
static list_t *_build_part_list(char *parts)
{
	list_t *part_list;
	char *orig, *str, *part;

	orig = str = xstrdup(parts);
	part_list = list_create(NULL);

	while ((part = _next_tok (",", &str))) {
		char *tmp_part = xstrdup(part);
		list_append (part_list, tmp_part);
	}

	xfree(orig);
	return (part_list);
}

static const char *
_node_state_list (void)
{
	int i;
	static char *all_states = NULL;

	if (all_states)
		return (all_states);

	all_states = xstrdup (node_state_string(0));
	for (i = 1; i < NODE_STATE_END; i++) {
		xstrcat (all_states, ",");
		xstrcat (all_states, node_state_string(i));
	}

	xstrcat(all_states,
		",DRAIN,DRAINED,DRAINING,NO_RESPOND,RESERVED,PLANNED,BLOCKED");
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_CLOUD));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_COMPLETING));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_POWERING_DOWN));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_POWERED_DOWN));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_POWER_DOWN));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_POWERING_UP));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_FAIL));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_MAINT));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_REBOOT_REQUESTED));
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_REBOOT_ISSUED));

	for (i = 0; i < strlen (all_states); i++)
		all_states[i] = tolower (all_states[i]);

	return (all_states);
}

static void _print_node_states(void)
{
	char *states = xstrdup(_node_state_list());

	for (uint32_t i = 0; states[i]; i++){
		if (states[i] == ',')
			states[i] = '\n';
	}

	if (states)
		printf("%s\n", states);

	xfree(states);
}

static bool
_node_state_equal (int i, const char *str)
{
	int len = strlen (str);

	if ((xstrncasecmp(node_state_string_compact(i), str, len) == 0) ||
	    (xstrncasecmp(node_state_string(i),         str, len) == 0))
		return (true);
	return (false);
}

/*
 * _parse_state - convert node state name string to numeric value
 * IN str - state name
 * OUT states - node_state value corresponding to str
 * RET 0 or error code
 */
static int
_node_state_id (char *str)
{
	int i;
	int len = strlen (str);

	for (i = 0; i < NODE_STATE_END; i++) {
		if (_node_state_equal (i, str))
			return (i);
	}

	if ((xstrncasecmp("BLOCKED", str, len) == 0) ||
	    (xstrncasecmp("BLOCK", str, len) == 0))
		return NODE_STATE_BLOCKED;
	if ((xstrncasecmp("PLANNED", str, len) == 0) ||
	    (xstrncasecmp("PLND", str, len) == 0))
		return NODE_STATE_PLANNED;
	if (xstrncasecmp("DRAIN", str, len) == 0)
		return NODE_STATE_DRAIN;
	if (xstrncasecmp("DRAINED", str, len) == 0)
		return NODE_STATE_DRAIN | NODE_STATE_IDLE;
	if ((xstrncasecmp("RESV", str, len) == 0) ||
	    (xstrncasecmp("RESERVED", str, len) == 0))
		return NODE_STATE_RES;
	if ((xstrncasecmp("DRAINING", str, len) == 0) ||
	    (xstrncasecmp("DRNG", str, len) == 0))
		return NODE_STATE_DRAIN | NODE_STATE_ALLOCATED;
	if (_node_state_equal (NODE_STATE_COMPLETING, str))
		return NODE_STATE_COMPLETING;
	if (xstrncasecmp("NO_RESPOND", str, len) == 0)
		return NODE_STATE_NO_RESPOND;
	if (_node_state_equal (NODE_STATE_POWERING_DOWN, str))
		return NODE_STATE_POWERING_DOWN;
	if (_node_state_equal (NODE_STATE_POWERED_DOWN, str))
		return NODE_STATE_POWERED_DOWN;
	if (_node_state_equal (NODE_STATE_POWER_DOWN, str))
		return NODE_STATE_POWER_DOWN;
	if (_node_state_equal (NODE_STATE_POWERING_UP, str))
		return NODE_STATE_POWERING_UP;
	if (_node_state_equal (NODE_STATE_FAIL, str))
		return NODE_STATE_FAIL;
	if (_node_state_equal (NODE_STATE_MAINT, str))
		return NODE_STATE_MAINT;
	if (_node_state_equal (NODE_STATE_REBOOT_REQUESTED, str))
		return NODE_STATE_REBOOT_REQUESTED;
	if (_node_state_equal (NODE_STATE_REBOOT_ISSUED, str))
		return NODE_STATE_REBOOT_ISSUED;
	if (_node_state_equal(NODE_STATE_CLOUD, str))
		return NODE_STATE_CLOUD;

	return (-1);
}

static fmt_data_t fmt_data[] = {
	{"AllocMem", 0, _print_alloc_mem, MATCH_FLAG_ALLOC_MEM},
	{"AllocNodes", 'S', _print_alloc_nodes, 0, 0},
	{"Available", 'a', _print_avail, MATCH_FLAG_AVAIL, 0},
	{"Cluster", 'V', _print_cluster_name, 0, 0},
	{"Comment", 0, _print_comment, MATCH_FLAG_COMMENT, 0},
	{"Cores", 'Y', _print_cores, MATCH_FLAG_CORES, 0},
	{"CPUs", 'c', _print_cpus, MATCH_FLAG_CPUS, 0},
	{"CPUsLoad", 'O', _print_cpu_load, MATCH_FLAG_CPU_LOAD, 0},
	{"CPUsState", 'C', _print_cpus_aiot, MATCH_FLAG_CPUS, 0},
	{"DefaultTime", 'L', _print_default_time, MATCH_FLAG_DEFAULT_TIME, 0},
	{"Disk", 'd', _print_disk, MATCH_FLAG_DISK, 0},
	{"Extra", 0, _print_extra, MATCH_FLAG_EXTRA, 0},
	{"Features", 'f', _print_features, MATCH_FLAG_FEATURES, 0},
	{"features_act", 'b', _print_features_act, MATCH_FLAG_FEATURES_ACT, 0},
	{"FreeMem", 'e', _print_free_mem, MATCH_FLAG_FREE_MEM, 0},
	{"Gres", 'G', _print_gres, MATCH_FLAG_GRES, 0},
	{"GresUsed", 'G', _print_gres_used, MATCH_FLAG_GRES_USED, 0},
	{"Groups", 'g', _print_groups, MATCH_FLAG_GROUPS, 0},
	{"MaxCPUsPerNode", 'B', _print_max_cpus_per_node,
	 MATCH_FLAG_MAX_CPUS_PER_NODE, 0},
	{"Memory", 'm', _print_memory, MATCH_FLAG_MEMORY, 0},
	{"NodeAddr", 'o', _print_node_address, MATCH_FLAG_NODE_ADDR, 0},
	{"NodeAI", 'A', _print_nodes_ai, 0, 0},
	{"NodeAIOT", 'F', _print_nodes_aiot, 0, 0},
	{"NodeHost", 'n', _print_node_hostnames, MATCH_FLAG_HOSTNAMES, 0},
	{"NodeList", 'N', _print_node_list, 0, 0},
	{"Nodes", 'D', _print_nodes_t, 0, 0},
	{"OverSubscribe", 'h', _print_oversubscribe, 0,
	 MATCH_FLAG_OVERSUBSCRIBE},
	{"Partition", 'P', _print_partition, MATCH_FLAG_PARTITION, 0},
	{"PartitionName", 'R', _print_partition_name, MATCH_FLAG_PARTITION, 0},
	{"Port", 0, _print_port, MATCH_FLAG_PORT, 0},
	{"PreemptMode", 'M', _print_preempt_mode, MATCH_FLAG_PREEMPT_MODE, 0},
	{"PriorityJobFactor", 'I', _print_priority_job_factor,
	 MATCH_FLAG_PRIORITY_JOB_FACTOR, 0},
	{"PriorityTier", 'p', _print_priority_tier, MATCH_FLAG_PRIORITY_TIER, 0},
	{"Reason", 'E', _print_reason, MATCH_FLAG_REASON, 0},
	{"Reservation", 'i', _print_resv_name, MATCH_FLAG_RESV_NAME, 0},
	{"Root", 'r', _print_root, MATCH_FLAG_ROOT, 0},
	{"Share", 'h', _print_oversubscribe, MATCH_FLAG_OVERSUBSCRIBE,
	 FMT_FLAG_HIDDEN},
	{"Size", 's', _print_size, MATCH_FLAG_JOB_SIZE, 0},
	{"Sockets", 'X', _print_sockets, MATCH_FLAG_SOCKETS, 0},
	{"SocketCoreThread", 'z', _print_sct, MATCH_FLAG_SCT, 0},
	{"StateCompact", 't', _print_state_compact, MATCH_FLAG_STATE, 0},
	{"StateComplete", 0, _print_state_complete, MATCH_FLAG_STATE_COMPLETE, 0},
	{"StateLong", 'T', _print_state_long, MATCH_FLAG_STATE, 0},
	{"Threads", 'Z', _print_threads, MATCH_FLAG_THREADS, 0},
	{"Time", 'l', _print_time, MATCH_FLAG_MAX_TIME, 0},
	{"TimeStamp", 'H', _print_timestamp, MATCH_FLAG_REASON_TIMESTAMP, 0},
	{"User", 'u', _print_user, MATCH_FLAG_REASON_USER, 0},
	{"UserLong", 'U', _print_user_long, MATCH_FLAG_REASON_USER, 0},
	{"Version", 'v', _print_version, MATCH_FLAG_VERSION, 0},
	{"Weight", 'w', _print_weight, MATCH_FLAG_WEIGHT, 0},
	{NULL, 0, NULL, 0, 0},
};

/* Take the user's format specification and use it to build build the
 *	format specifications (internalize it to print.c data structures) */
static int
_parse_format( char* format )
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
		fprintf( stderr, "Format option lacks specification\n" );
		exit( 1 );
	}

	if ((prefix = _get_prefix(format)))
		format_add_prefix( params.format_list, 0, 0, prefix);

	if (!xstrcasecmp(format, "%all")) {
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
		found = false;
		_parse_token( token, field, &field_size, &right_justify,
			      &suffix);

		for (i = 0; fmt_data[i].name || fmt_data[i].c; i++) {
			if (field[0] == fmt_data[i].c) {
				found = true;
				params.match_flags |= fmt_data[i].match_flags;
				format_add_function(params.format_list,
						    field_size, right_justify,
						    suffix, fmt_data[i].fn);
				break;
			}
		}
		if (found) {
			; /* NO OP */
		} else if (format_all) {
			xfree(suffix);	/* ignore */
		} else {
			prefix = xstrdup("%");
			xstrcat(prefix, token);
			xfree(suffix);
			suffix = prefix;
			format_add_invalid( params.format_list,
					    field_size,
					    right_justify,
					    suffix );
			fprintf(stderr, "Invalid node format specification: %c\n",
				field[0] );
		}
		token = strtok_r( NULL, "%", &tmp_char);
	}

	xfree( tmp_format );
	return SLURM_SUCCESS;
}

static int _parse_long_format (char* format_long)
{
	int field_size;
	bool right_justify, format_all = false;
	char *tmp_format = NULL, *token = NULL, *str_tmp = NULL;
	char *sep = NULL;
	char* suffix = NULL;
	int i;
	bool found = false;

	if (format_long == NULL) {
		error("Format long option lacks specification");
		exit( 1 );
	}

	tmp_format = xstrdup(format_long);
	token = strtok_r(tmp_format, ",",&str_tmp);

	while (token) {
		found = false;
		_parse_long_token( token, sep, &field_size, &right_justify,
				   &suffix);
		if (!xstrcasecmp(token, "all")) {
			_parse_format ("%all");
			xfree(suffix);
		}
		for (i = 0; fmt_data[i].name || fmt_data[i].c; i++) {
			if (!xstrcasecmp(token, fmt_data[i].name)) {
				found = true;
				params.match_flags |= fmt_data[i].match_flags;
				format_add_function(params.format_list,
						    field_size, right_justify,
						    suffix, fmt_data[i].fn);
				break;
			}
		}
		if (found) {
			; /* NO OP */
		} else if (format_all) {
			/* ignore */
			xfree(suffix);
		} else {
			format_add_invalid( params.format_list,
					    field_size,
					    right_justify,
					    suffix );
			error( "Invalid job format specification: %s",
			       token );
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

static void
_parse_long_token( char *token, char *sep, int *field_size, bool *right_justify,
		   char **suffix)
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
void _print_options( void )
{
	printf("-----------------------------\n");
	printf("dead        = %s\n", params.dead_nodes ? "true" : "false");
	printf("exact       = %d\n", params.exact_match);
	printf("filtering   = %s\n", params.filtering ? "true" : "false");
	printf("format      = %s\n", params.format);
	printf("iterate     = %d\n", params.iterate );
	printf("long        = %s\n", params.long_output ? "true" : "false");
	printf("no_header   = %s\n", params.no_header   ? "true" : "false");
	printf("node_field  = %s\n", params.node_field_flag ?
					"true" : "false");
	printf("node_format = %s\n", params.node_flag   ? "true" : "false");
	printf("nodes       = %s\n", params.nodes ? params.nodes : "n/a");
	printf("part_field  = %s\n", params.part_field_flag ?
					"true" : "false");
	printf("partition   = %s\n", params.partition ?
					params.partition: "n/a");
	printf("responding  = %s\n", params.responding_nodes ?
					"true" : "false");
	printf("states      = %s\n", params.states);
	printf("sort        = %s\n", params.sort);
	printf("summarize   = %s\n", params.summarize   ? "true" : "false");
	printf("verbose     = %d\n", params.verbose);
	printf("-----------------------------\n");
	printf("all_flag        = %s\n", params.all_flag ? "true" : "false");
	printf("alloc_mem_flag  = %s\n",
	       (params.match_flags & MATCH_FLAG_ALLOC_MEM) ? "true" : "false");
	printf("avail_flag      = %s\n",
	       (params.match_flags & MATCH_FLAG_AVAIL) ? "true" : "false");
	printf("comment_flag    = %s\n",
	       (params.match_flags & MATCH_FLAG_COMMENT) ? "true" : "false");
	printf("cpus_flag       = %s\n",
	       (params.match_flags & MATCH_FLAG_CPUS) ? "true" : "false");
	printf("default_time_flag =%s\n",
	       (params.match_flags & MATCH_FLAG_DEFAULT_TIME)
	       ? "true" : "false");
	printf("disk_flag       = %s\n",
	       (params.match_flags & MATCH_FLAG_DISK) ? "true" : "false");
	printf("extra_flag      = %s\n",
	       (params.match_flags & MATCH_FLAG_EXTRA) ? "true" : "false");
	printf("features_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_FEATURES) ? "true" : "false");
	printf("features_flag_act = %s\n",
	       (params.match_flags & MATCH_FLAG_FEATURES_ACT) ?
	       "true" : "false");
	printf("groups_flag     = %s\n",
	       (params.match_flags & MATCH_FLAG_GROUPS) ? "true" : "false");
	printf("gres_flag       = %s\n",
	       (params.match_flags & MATCH_FLAG_GRES) ? "true" : "false");
	printf("gres_used_flag  = %s\n",
	       (params.match_flags & MATCH_FLAG_GRES_USED) ? "true" : "false");
	printf("job_size_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_JOB_SIZE) ? "true" : "false");
	printf("max_time_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_MAX_TIME) ? "true" : "false");
	printf("memory_flag     = %s\n",
	       (params.match_flags & MATCH_FLAG_MEMORY) ? "true" : "false");
	printf("partition_flag  = %s\n",
	       (params.match_flags & MATCH_FLAG_PARTITION) ? "true" : "false");
	printf("port_flag       = %s\n",
	       (params.match_flags & MATCH_FLAG_PORT) ? "true" : "false");
	printf("priority_job_factor_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_PRIORITY_JOB_FACTOR) ?
	       "true" : "false");
	printf("priority_tier_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_PRIORITY_TIER) ?
	       "true" : "false");
	printf("reason_flag     = %s\n",
	       (params.match_flags & MATCH_FLAG_REASON) ? "true" : "false");
	printf("reason_timestamp_flag = %s\n",
	       (params.match_flags & MATCH_FLAG_REASON_TIMESTAMP) ?
	       "true" : "false");
	printf("reason_user_flag = %s\n",
	       (params.match_flags & MATCH_FLAG_REASON_USER) ?
	       "true" : "false");
	printf("reservation_flag = %s\n", params.reservation_flag ?
			"true" : "false");
	printf("resv_name_flag   = %s\n",
	       (params.match_flags & MATCH_FLAG_RESV_NAME) ? "true" : "false");
	printf("root_flag       = %s\n",
	       (params.match_flags & MATCH_FLAG_ROOT) ? "true" : "false");
	printf("oversubscribe_flag      = %s\n",
	       (params.match_flags & MATCH_FLAG_OVERSUBSCRIBE) ?
	       "true" : "false");
	printf("state_flag      = %s\n",
	       (params.match_flags & MATCH_FLAG_STATE) ? "true" : "false");
	printf("statecomplete_flag = %s\n",
	       (params.match_flags & MATCH_FLAG_STATE_COMPLETE) ?
	       "true" : "false");
	printf("weight_flag     = %s\n",
	       (params.match_flags & MATCH_FLAG_WEIGHT) ? "true" : "false");
	printf("-----------------------------\n\n");
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
		if (fmt_data[i].flags & FMT_FLAG_HIDDEN)
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

static void _help_format2(void)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; fmt_data[i].c || fmt_data[i].name; i++) {
		if (!fmt_data[i].name)
			continue;
		if (fmt_data[i].flags & FMT_FLAG_HIDDEN)
			continue;

		if (cnt & 4) {
			cnt = 0;
			printf("\n");
		}

		cnt++;
		printf("%-20s", fmt_data[i].name);
	}
	printf("\n");
}
