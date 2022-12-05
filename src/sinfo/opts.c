/****************************************************************************\
 *  opts.c - sinfo command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD <https://www.schedmd.com>.
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

/* FUNCTIONS */
static List  _build_state_list( char* str );
static List  _build_all_states_list( void );
static List  _build_part_list( char *parts );
static char *_get_prefix(char *token);
static void  _help( void );
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

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	char *env_val = NULL;
	int opt_char;
	int option_index;
	hostlist_t host_list;
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
                {"json", no_argument, 0, OPT_LONG_JSON},
                {"yaml", no_argument, 0, OPT_LONG_YAML},
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
		if (!(params.clusters = slurmdb_get_info_cluster(env_val))) {
			print_db_notok(env_val, 1);
			exit(1);
		}
		working_cluster_rec = list_peek(params.clusters);
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
			FREE_NULL_LIST(params.clusters);
			if (!(params.clusters =
			      slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(params.clusters);
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
			data_init();
			serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL);
			break;
		case OPT_LONG_YAML:
			params.mimetype = MIME_TYPE_YAML;
			data_init();
			serializer_g_init(MIME_TYPE_YAML_PLUGIN, NULL);
			break;
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
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
static List
_build_state_list (char *state_str)
{
	List state_ids;
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
static List
_build_all_states_list( void )
{
	List my_list;
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
static List
_build_part_list(char *parts)
{
	List part_list;
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
		",DRAIN,DRAINED,DRAINING,NO_RESPOND,RESERVED,PERFCTRS");
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
	xstrcat(all_states, ",");
	xstrcat(all_states, node_state_string(NODE_STATE_PLANNED));

	for (i = 0; i < strlen (all_states); i++)
		all_states[i] = tolower (all_states[i]);

	return (all_states);
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
	if ((xstrncasecmp("PERFCTRS", str, len) == 0) ||
	    (xstrncasecmp("NPC", str, len) == 0))
		return NODE_STATE_NET;
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
		_parse_token( token, field, &field_size, &right_justify,
			      &suffix);
		if        (field[0] == 'a') {
			params.match_flags.avail_flag = true;
			format_add_avail( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'A') {
			format_add_nodes_ai( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'b') {
			params.match_flags.features_act_flag = true;
			format_add_features_act( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'B') {
			params.match_flags.max_cpus_per_node_flag = true;
			format_add_max_cpus_per_node( params.format_list,
					     field_size,
					     right_justify,
					     suffix );
		} else if (field[0] == 'c') {
			params.match_flags.cpus_flag = true;
			format_add_cpus( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'C') {
			params.match_flags.cpus_flag = true;
			format_add_cpus_aiot( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'd') {
			params.match_flags.disk_flag = true;
			format_add_disk( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'D') {
			format_add_nodes( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'e') {
			params.match_flags.free_mem_flag = true;
			format_add_free_mem( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'E') {
			params.match_flags.reason_flag = true;
			format_add_reason( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'f') {
			params.match_flags.features_flag = true;
			format_add_features( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'F') {
			format_add_nodes_aiot( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'g') {
			params.match_flags.groups_flag = true;
			format_add_groups( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'G') {
			params.match_flags.gres_flag = true;
			format_add_gres( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'h') {
			params.match_flags.oversubscribe_flag = true;
			format_add_oversubscribe( params.format_list,
						  field_size,
						  right_justify,
						  suffix );
		} else if (field[0] == 'H') {
			params.match_flags.reason_timestamp_flag = true;
			format_add_timestamp( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (field[0] == 'i') {
			params.match_flags.resv_name_flag = true;
			format_add_resv_name(params.format_list,
					     field_size,
					     right_justify,
					     suffix);
		} else if (field[0] == 'I') {
			params.match_flags.priority_job_factor_flag = true;
			format_add_priority_job_factor(params.format_list,
					field_size,
					right_justify,
					suffix);
		} else if (field[0] == 'l') {
			params.match_flags.max_time_flag = true;
			format_add_time( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'L') {
			params.match_flags.default_time_flag = true;
			format_add_default_time( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'm') {
			params.match_flags.memory_flag = true;
			format_add_memory( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'M') {
			params.match_flags.preempt_mode_flag = true;
			format_add_preempt_mode( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'n') {
			params.match_flags.hostnames_flag = true;
			format_add_node_hostnames( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'N') {
			format_add_node_list( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'o') {
			params.match_flags.node_addr_flag = true;
			format_add_node_address( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'O') {
			params.match_flags.cpu_load_flag = true;
			format_add_cpu_load( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'p') {
			params.match_flags.priority_tier_flag = true;
			format_add_priority_tier(params.format_list,
					field_size,
					right_justify,
					suffix);
		} else if (field[0] == 'P') {
			params.match_flags.partition_flag = true;
			format_add_partition( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'r') {
			params.match_flags.root_flag = true;
			format_add_root( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'R') {
			params.match_flags.partition_flag = true;
			format_add_partition_name( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 's') {
			params.match_flags.job_size_flag = true;
			format_add_size( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (field[0] == 'S') {
			format_add_alloc_nodes( params.format_list,
						field_size,
						right_justify,
						suffix );
		} else if (field[0] == 't') {
			params.match_flags.state_flag = true;
			format_add_state_compact( params.format_list,
						  field_size,
						  right_justify,
						  suffix );
		} else if (field[0] == 'T') {
			params.match_flags.state_flag = true;
			format_add_state_long( params.format_list,
					       field_size,
					       right_justify,
					       suffix );
		} else if (field[0] == 'u') {
			params.match_flags.reason_user_flag = true;
			format_add_user( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'U') {
			params.match_flags.reason_user_flag = true;
			format_add_user_long( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (field[0] == 'v') {
			params.match_flags.version_flag = true;
			format_add_version( params.format_list,
					    field_size,
					    right_justify,
					    suffix);
		} else if (field[0] == 'V') {
			format_add_cluster_name(params.format_list,
						field_size,
						right_justify,
						suffix);
		} else if (field[0] == 'w') {
			params.match_flags.weight_flag = true;
			format_add_weight( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'X') {
			params.match_flags.sockets_flag = true;
			format_add_sockets( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'Y') {
			params.match_flags.cores_flag = true;
			format_add_cores( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'Z') {
			params.match_flags.threads_flag = true;
			format_add_threads( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (field[0] == 'z') {
			params.match_flags.sct_flag = true;
			format_add_sct( params.format_list,
					field_size,
					right_justify,
					suffix );
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

	if (format_long == NULL) {
		error("Format long option lacks specification");
		exit( 1 );
	}

	tmp_format = xstrdup(format_long);
	token = strtok_r(tmp_format, ",",&str_tmp);

	while (token) {
		_parse_long_token( token, sep, &field_size, &right_justify,
				   &suffix);
		if (!xstrcasecmp(token, "all")) {
			_parse_format ("%all");
			xfree(suffix);
		} else if (!xstrcasecmp(token, "allocmem")) {
			params.match_flags.alloc_mem_flag = true;
			format_add_alloc_mem( params.format_list,
						field_size,
						right_justify,
						suffix );
		} else if (!xstrcasecmp(token, "allocnodes")) {
			format_add_alloc_nodes( params.format_list,
						field_size,
						right_justify,
						suffix );
		} else if (!xstrcasecmp(token, "available")) {
			params.match_flags.avail_flag = true;
			format_add_avail( params.format_list,
					  field_size,
					  right_justify,
					  suffix );
		} else if (!xstrcasecmp(token, "cluster")) {
			format_add_cluster_name(params.format_list,
						field_size,
						right_justify,
						suffix);
		} else if (!xstrcasecmp(token, "comment")) {
			params.match_flags.comment_flag = true;
			format_add_comment(params.format_list,
					   field_size,
					   right_justify,
					   suffix);
		} else if (!xstrcasecmp(token, "cpus")) {
			params.match_flags.cpus_flag = true;
			format_add_cpus( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "cpusload")) {
			params.match_flags.cpu_load_flag = true;
			format_add_cpu_load( params.format_list,
					     field_size,
					     right_justify,
					     suffix );
		} else if (!xstrcasecmp(token, "freemem")) {
			params.match_flags.free_mem_flag = true;
			format_add_free_mem( params.format_list,
					     field_size,
					     right_justify,
					     suffix );
		} else if (!xstrcasecmp(token, "cpusstate")) {
			params.match_flags.cpus_flag = true;
			format_add_cpus_aiot( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (!xstrcasecmp(token, "cores")) {
			params.match_flags.cores_flag = true;
			format_add_cores( params.format_list,
					  field_size,
					  right_justify,
					  suffix );
		} else if (!xstrcasecmp(token, "defaulttime")) {
			params.match_flags.default_time_flag = true;
			format_add_default_time( params.format_list,
						 field_size,
						 right_justify,
						 suffix );
		} else if (!xstrcasecmp(token, "disk")) {
			params.match_flags.disk_flag = true;
			format_add_disk( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "extra")) {
			params.match_flags.extra_flag = true;
			format_add_extra(params.format_list, field_size,
					 right_justify, suffix);
		} else if (!xstrcasecmp(token, "features")) {
			params.match_flags.features_flag = true;
			format_add_features( params.format_list,
					     field_size,
					     right_justify,
					     suffix );
		} else if (!xstrcasecmp(token, "features_act")) {
			params.match_flags.features_act_flag = true;
			format_add_features_act( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (!xstrcasecmp(token, "groups")) {
			params.match_flags.groups_flag = true;
			format_add_groups( params.format_list,
					   field_size,
					   right_justify,
					   suffix );
		} else if (!xstrcasecmp(token, "gres")) {
			params.match_flags.gres_flag = true;
			format_add_gres( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "gresused")) {
			params.match_flags.gres_used_flag = true;
			format_add_gres_used( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "maxcpuspernode")) {
			params.match_flags.max_cpus_per_node_flag = true;
			format_add_max_cpus_per_node( params.format_list,
						      field_size,
						      right_justify,
						      suffix );
		} else if (!xstrcasecmp(token, "memory")) {
			params.match_flags.memory_flag = true;
			format_add_memory( params.format_list,
					   field_size,
					   right_justify,
					   suffix );
		} else if (!xstrcasecmp(token, "nodes")) {
			format_add_nodes( params.format_list,
					  field_size,
					  right_justify,
					  suffix );
		} else if (!xstrcasecmp(token, "nodeaddr")) {
			params.match_flags.node_addr_flag = true;
			format_add_node_address( params.format_list,
						 field_size,
						 right_justify,
						 suffix );
		} else if (!xstrcasecmp(token, "nodeai")) {
			format_add_nodes_ai( params.format_list,
					     field_size,
					     right_justify,
					     suffix );
		} else if (!xstrcasecmp(token, "nodeaiot")) {
			format_add_nodes_aiot( params.format_list,
					       field_size,
					       right_justify,
					       suffix );
		} else if (!xstrcasecmp(token, "nodehost")) {
			params.match_flags.hostnames_flag = true;
			format_add_node_hostnames( params.format_list,
						   field_size,
						   right_justify,
						   suffix );
		} else if (!xstrcasecmp(token, "nodelist")) {
			format_add_node_list( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (!xstrcasecmp(token, "partition")) {
			params.match_flags.partition_flag = true;
			format_add_partition( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (!xstrcasecmp(token, "partitionname")) {
			params.match_flags.partition_flag = true;
			format_add_partition_name( params.format_list,
						   field_size,
						   right_justify,
						   suffix );
		} else if (!xstrcasecmp(token, "port")) {
			params.match_flags.port_flag = true;
			format_add_port( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "preemptmode")) {
			params.match_flags.preempt_mode_flag = true;
			format_add_preempt_mode( params.format_list,
						 field_size,
						 right_justify,
						 suffix );
		} else if (!xstrcasecmp(token, "priorityjobfactor")) {
			params.match_flags.priority_job_factor_flag = true;
			format_add_priority_job_factor(params.format_list,
						       field_size,
						       right_justify,
						       suffix );
		} else if (!xstrcasecmp(token, "prioritytier")) {
			params.match_flags.priority_tier_flag = true;
			format_add_priority_tier(params.format_list,
						 field_size,
						 right_justify,
						 suffix );
		} else if (!xstrcasecmp(token, "reason")) {
			params.match_flags.reason_flag = true;
			format_add_reason( params.format_list,
					   field_size,
					   right_justify,
					   suffix );
		} else if (!xstrcasecmp(token, "reservation")) {
			params.match_flags.resv_name_flag = true;
			format_add_resv_name(params.format_list,
					     field_size,
					     right_justify,
					     suffix);
		} else if (!xstrcasecmp(token, "root")) {
			params.match_flags.root_flag = true;
			format_add_root( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "oversubscribe") ||
			   !xstrcasecmp(token, "share")) {
			params.match_flags.oversubscribe_flag = true;
			format_add_oversubscribe( params.format_list,
						  field_size,
						  right_justify,
						  suffix );
		} else if (!xstrcasecmp(token, "size")) {
			params.match_flags.job_size_flag = true;
			format_add_size( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "statecompact")) {
			params.match_flags.state_flag = true;
			format_add_state_compact( params.format_list,
						  field_size,
						  right_justify,
						  suffix );
		} else if (!xstrcasecmp(token, "statecomplete")) {
			params.match_flags.statecomplete_flag = true;
			format_add_state_complete(params.format_list,
						  field_size, right_justify,
						  suffix );
		} else if (!xstrcasecmp(token, "statelong")) {
			params.match_flags.state_flag = true;
			format_add_state_long( params.format_list,
					       field_size,
					       right_justify,
					       suffix );
		} else if (!xstrcasecmp(token, "sockets")) {
			params.match_flags.sockets_flag = true;
			format_add_sockets( params.format_list,
					    field_size,
					    right_justify,
					    suffix );
		} else if (!xstrcasecmp(token, "socketcorethread")) {
			params.match_flags.sct_flag = true;
			format_add_sct( params.format_list,
					field_size,
					right_justify,
					suffix );
		} else if (!xstrcasecmp(token, "time")) {
			params.match_flags.max_time_flag = true;
			format_add_time( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "timestamp")) {
			params.match_flags.reason_timestamp_flag = true;
			format_add_timestamp( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (!xstrcasecmp(token, "threads")) {
			params.match_flags.threads_flag = true;
			format_add_threads( params.format_list,
					    field_size,
					    right_justify,
					    suffix );
		} else if (!xstrcasecmp(token, "user")) {
			params.match_flags.reason_user_flag = true;
			format_add_user( params.format_list,
					 field_size,
					 right_justify,
					 suffix );
		} else if (!xstrcasecmp(token, "userlong")) {
			params.match_flags.reason_user_flag = true;
			format_add_user_long( params.format_list,
					      field_size,
					      right_justify,
					      suffix );
		} else if (!xstrcasecmp(token, "version")) {
			params.match_flags.version_flag = true;
			format_add_version( params.format_list,
					    field_size,
					    right_justify,
					    suffix);
		} else if (!xstrcasecmp(token, "weight")) {
			params.match_flags.weight_flag = true;
			format_add_weight( params.format_list,
					   field_size,
					   right_justify,
					   suffix );
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
	printf("alloc_mem_flag  = %s\n", params.match_flags.alloc_mem_flag ?
			"true" : "false");
	printf("avail_flag      = %s\n", params.match_flags.avail_flag ?
			"true" : "false");
	printf("comment_flag    = %s\n", params.match_flags.comment_flag ?
			"true" : "false");
	printf("cpus_flag       = %s\n", params.match_flags.cpus_flag ?
			"true" : "false");
	printf("default_time_flag =%s\n", params.match_flags.default_time_flag ?
					"true" : "false");
	printf("disk_flag       = %s\n", params.match_flags.disk_flag ?
			"true" : "false");
	printf("extra_flag      = %s\n",
	       params.match_flags.extra_flag ? "true" : "false");
	printf("features_flag   = %s\n", params.match_flags.features_flag ?
			"true" : "false");
	printf("features_flag_act = %s\n", params.match_flags.features_act_flag?
			"true" : "false");
	printf("groups_flag     = %s\n", params.match_flags.groups_flag ?
					"true" : "false");
	printf("gres_flag       = %s\n", params.match_flags.gres_flag ?
			"true" : "false");
	printf("gres_used_flag  = %s\n", params.match_flags.gres_used_flag ?
			"true" : "false");
	printf("job_size_flag   = %s\n", params.match_flags.job_size_flag ?
					"true" : "false");
	printf("max_time_flag   = %s\n", params.match_flags.max_time_flag ?
					"true" : "false");
	printf("memory_flag     = %s\n", params.match_flags.memory_flag ?
			"true" : "false");
	printf("partition_flag  = %s\n", params.match_flags.partition_flag ?
			"true" : "false");
	printf("port_flag       = %s\n", params.match_flags.port_flag ?
			"true" : "false");
	printf("priority_job_factor_flag   = %s\n",
			params.match_flags.priority_job_factor_flag ?
			"true" : "false");
	printf("priority_tier_flag   = %s\n",
			params.match_flags.priority_tier_flag ?
			"true" : "false");
	printf("reason_flag     = %s\n", params.match_flags.reason_flag ?
			"true" : "false");
	printf("reason_timestamp_flag = %s\n",
			params.match_flags.reason_timestamp_flag ?
			"true" : "false");
	printf("reason_user_flag = %s\n",
			params.match_flags.reason_user_flag ?  "true" : "false");
	printf("reservation_flag = %s\n", params.reservation_flag ?
			"true" : "false");
	printf("resv_name_flag   = %s\n", params.match_flags.resv_name_flag ?
	       "true" : "false");
	printf("root_flag       = %s\n", params.match_flags.root_flag ?
			"true" : "false");
	printf("oversubscribe_flag      = %s\n",
			params.match_flags.oversubscribe_flag ?
			"true" : "false");
	printf("state_flag      = %s\n", params.match_flags.state_flag ?
			"true" : "false");
	printf("statecomplete_flag = %s\n",
	       params.match_flags.statecomplete_flag ? "true" : "false");
	printf("weight_flag     = %s\n", params.match_flags.weight_flag ?
			"true" : "false");
	printf("-----------------------------\n\n");
}

static void _usage( void )
{
	printf("\
Usage: sinfo [-abdelNRrsTv] [-i seconds] [-t states] [-p partition] [-n nodes]\n\
             [-S fields] [-o format] [-O Format] [--federation] [--local]\n");
}

static void _help( void )
{
	printf ("\
Usage: sinfo [OPTIONS]\n\
  -a, --all                  show all partitions (including hidden and those\n\
			     not accessible)\n\
  -d, --dead                 show only non-responding nodes\n\
  -e, --exact                group nodes only on exact match of configuration\n\
      --federation           Report federated information if a member of one\n\
  -F, --future               Report information about nodes in \"FUTURE\" state.    \n\
  -h, --noheader             no headers on output\n\
  --hide                     do not show hidden or non-accessible partitions\n\
  -i, --iterate=seconds      specify an iteration period\n\
      --json                 Produce JSON output\n\
      --local                show only local cluster in a federation.\n\
                             Overrides --federation.\n\
  -l, --long                 long output - displays more information\n\
  -M, --clusters=names       clusters to issue commands to. Implies --local.\n\
                             NOTE: SlurmDBD must be up.\n\
  -n, --nodes=NODES          report on specific node(s)\n\
  --noconvert                don't convert units from their original type\n\
			     (e.g. 2048M won't be converted to 2G).\n\
  -N, --Node                 Node-centric format\n\
  -o, --format=format        format specification\n\
  -O, --Format=format        long format specification\n\
  -p, --partition=PARTITION  report on specific partition\n\
  -r, --responding           report only responding nodes\n\
  -R, --list-reasons         list reason nodes are down or drained\n\
  -s, --summarize            report state summary only\n\
  -S, --sort=fields          comma separated list of fields to sort on\n\
  -t, --states=node_state    specify the what states of nodes to view\n\
  -T, --reservation          show only reservation information\n\
  -v, --verbose              verbosity level\n\
  -V, --version              output version information and exit\n\
      --yaml                 Produce YAML output\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
