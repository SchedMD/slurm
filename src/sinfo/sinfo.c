/*****************************************************************************\
 *  sinfo.c - Report overall state the system
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

#include <inttypes.h>

#include "slurm/slurmdb.h"

#include "src/common/macros.h"
#include "src/common/sercli.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/xhash.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/select.h"

#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

/********************
 * Global Variables *
 ********************/
typedef struct build_part_info {
	node_info_msg_t *node_msg;
	bool *part_has_nodes;
	int part_num;
	partition_info_t *part_ptr;
	xhash_t *sinfo_hash;
	list_t *sinfo_list;
} build_part_info_t;

/* Data structures for pthreads used to gather node/partition information from
 * multiple clusters in parallel */
typedef struct load_info_struct {
	slurmdb_cluster_rec_t *cluster;
	list_t *node_info_msg_list;
	list_t *part_info_msg_list;
	list_t *resp_msg_list;
} load_info_struct_t;

struct sinfo_parameters params;

static int sinfo_cnt;	/* thread count */
static pthread_mutex_t sinfo_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sinfo_cnt_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t sinfo_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*************
 * Functions *
 *************/
static void _free_sinfo_format(void *object);
static void _free_params(void);
void *      _build_part_info(void *args);
static int _build_sinfo_data(list_t *sinfo_list,
			     partition_info_msg_t *partition_msg,
			     node_info_msg_t *node_msg);
static sinfo_data_t *_create_sinfo(partition_info_t* part_ptr,
				   uint16_t part_inx, node_info_t *node_ptr);
static char *_build_part_match_key(partition_info_t *part_ptr);
static int  _find_part_list(void *x, void *key);
static bool _filter_out(node_info_t *node_ptr);
static int _get_info(bool clear_old, slurmdb_federation_rec_t *fed,
		     char *cluster_name, data_parser_t *parser);
static int _insert_node_ptr(list_t *sinfo_list, xhash_t *sinfo_hash,
			    bool *part_has_nodes, int part_num,
			    const char *part_key, partition_info_t *part_ptr,
			    node_info_t *node_ptr);
static int _load_resv(reserve_info_msg_t **reserv_pptr, bool clear_old);
static int _multi_cluster(list_t *clusters, data_parser_t *parser);
static void _node_list_delete(void *data);
static void _part_list_delete(void *data);
static list_t *_query_fed_servers(slurmdb_federation_rec_t *fed,
				  list_t *node_info_msg_list,
				  list_t *part_info_msg_list);
static list_t *_query_server(bool clear_old);
static int _reservation_report(reserve_info_msg_t *resv_ptr);
static void _sinfo_list_delete(void *data);
static void _sort_hostlist(list_t *sinfo_list);
static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr);

int main(int argc, char **argv)
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc = 0;
	data_parser_t *parser = NULL;

	slurm_init(NULL);
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);
	memset(&params, 0, sizeof(params));
	params.format_list = list_create(_free_sinfo_format);
	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

	if (params.mimetype) {
		rc = data_parser_cli_load(&parser, NULL, argc, argv,
					  params.mimetype, params.data_parser);
		if (rc || !parser)
			goto cleanup;
	}

	while (1) {
		if (!params.no_header && !params.mimetype &&
		    (params.iterate || params.verbose || params.long_output))
			print_date();

		if (!params.clusters) {
			if (_get_info(false, params.fed, NULL, parser))
				rc = 1;
		} else if (_multi_cluster(params.clusters, parser))
			rc = 1;
		if (params.iterate) {
			printf("\n");
			sleep(params.iterate);
		} else
			break;
	}

cleanup:
	if (params.mimetype)
		data_parser_cli_free_ctxt(&parser);

	_free_params();

	/* Collapse any load/RPC errno to 1 to match sinfo's historic exit. */
	exit(rc ? 1 : 0);
}

static void _free_sinfo_format(void *object)
{
	sinfo_format_t *x = (sinfo_format_t *)object;

	if (!x)
		return;
	xfree(x->suffix);
	xfree(x);
}

static void _free_params(void)
{
	FREE_NULL_LIST(params.clusters);
	xfree(params.format);
	xfree(params.nodes);
	xfree(params.partition);
	xfree(params.sort);
	xfree(params.states);
	FREE_NULL_LIST(params.part_list);
	FREE_NULL_LIST(params.format_list);
	FREE_NULL_LIST(params.state_list);
	slurmdb_destroy_federation_rec(params.fed);
}

static int _list_find_func(void *x, void *key)
{
	sinfo_format_t *sinfo_format = (sinfo_format_t *) x;
	if (sinfo_format->function == key)
		return 1;
	return 0;
}

static void prepend_cluster_name(void)
{
	if (list_find_first(params.format_list, _list_find_func,
			    _print_cluster_name))
		return;
	format_prepend_cluster_name(params.format_list, 8, false, NULL);
}

static int _multi_cluster(list_t *clusters, data_parser_t *parser)
{
	list_itr_t *itr;
	bool first = true;
	int rc = 0, rc2;

	if ((list_count(clusters) > 1) && params.no_header &&
	    params.def_format)
		prepend_cluster_name();
	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		if (!params.no_header) {
			if (first)
				first = false;
			else
				printf("\n");
			printf("CLUSTER: %s\n", working_cluster_rec->name);
		}
		rc2 = _get_info(true, NULL, working_cluster_rec->name, parser);
		if (rc2)
			rc = 1;
	}
	list_iterator_destroy(itr);

	return rc;
}

static int _set_cluster_name(void *x, void *arg)
{
	sinfo_data_t *sinfo_data = (sinfo_data_t *) x;
	xfree(sinfo_data->cluster_name);
	sinfo_data->cluster_name = xstrdup((char *)arg);
	return 0;
}

/* clear_old IN - if set then don't preserve old info (it might be from
 *		  another cluster)
 * fed IN - information about other clusters in this federation
 */
static int _get_info(bool clear_old, slurmdb_federation_rec_t *fed,
		     char *cluster_name, data_parser_t *parser)
{
	list_t *node_info_msg_list = NULL, *part_info_msg_list = NULL;
	reserve_info_msg_t *reserv_msg = NULL;
	list_t *sinfo_list = NULL;
	int rc = SLURM_SUCCESS;

	if (params.reservation_flag) {
		if (_load_resv(&reserv_msg, clear_old))
			rc = SLURM_ERROR;
		else
			(void) _reservation_report(reserv_msg);
		return rc;
	}

	if (fed) {
		node_info_msg_list = list_create(_node_list_delete);
		part_info_msg_list = list_create(_part_list_delete);
		sinfo_list = _query_fed_servers(fed, node_info_msg_list,
						part_info_msg_list);
	} else {
		sinfo_list = _query_server(clear_old);
	}

	if (!sinfo_list)
		return SLURM_ERROR;
	if (cluster_name) {
		(void) list_for_each(sinfo_list, _set_cluster_name,
				     cluster_name);
	}

	sort_sinfo_list(sinfo_list);
	if (params.mimetype)
		rc = data_parser_dump_cli_single(DATA_PARSER_OPENAPI_SINFO_RESP,
						 sinfo_list, parser);
	else
		rc = print_sinfo_list(sinfo_list);

	FREE_NULL_LIST(node_info_msg_list);
	FREE_NULL_LIST(part_info_msg_list);
	FREE_NULL_LIST(sinfo_list);
	return rc;
}

/*
 * _reservation_report - print current reservation information
 */
static int _reservation_report(reserve_info_msg_t *resv_ptr)
{
	if (!resv_ptr) {
		slurm_perror("No resv_ptr given\n");
		return SLURM_ERROR;
	}
	if (resv_ptr->record_count)
		print_sinfo_reservation(resv_ptr);
	else
		printf ("No reservations in the system\n");
	return SLURM_SUCCESS;
}

/*
 * _load_resv - download the current server's reservation state
 * reserv_pptr IN/OUT - reservation information message
 * clear_old IN - If set, then always replace old data, needed when going
 *		  between clusters.
 * RET zero or error code
 */
static int _load_resv(reserve_info_msg_t **reserv_pptr, bool clear_old)
{
	static reserve_info_msg_t *old_resv_ptr = NULL, *new_resv_ptr;
	int error_code;

	if (old_resv_ptr) {
		if (clear_old)
			old_resv_ptr->last_update = 0;
		error_code = slurm_load_reservations(old_resv_ptr->last_update,
						     &new_resv_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_reservation_info_msg(old_resv_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_resv_ptr = old_resv_ptr;
		}
	} else {
		error_code = slurm_load_reservations((time_t) NULL,
						     &new_resv_ptr);
	}

	if (error_code) {
		slurm_perror("slurm_load_reservations");
		return error_code;
	}
	old_resv_ptr = new_resv_ptr;
	*reserv_pptr = new_resv_ptr;

	return SLURM_SUCCESS;
}

/*
 * _query_server - download the current server state
 * clear_old IN - If set, then always replace old data, needed when going
 *		  between clusters.
 * RET List of node/partition records
 */
static list_t *_query_server(bool clear_old)
{
	static partition_info_msg_t *old_part_ptr = NULL, *new_part_ptr;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;
	uint16_t show_flags = SHOW_MIXED;
	list_t *sinfo_list = NULL;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (params.future_flag)
		show_flags |= SHOW_FUTURE;

	if (old_part_ptr) {
		if (clear_old)
			old_part_ptr->last_update = 0;
		error_code = slurm_load_partitions(old_part_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(old_part_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = old_part_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, &new_part_ptr,
						   show_flags);
	}
	if (error_code) {
		slurm_perror("slurm_load_partitions");
		return sinfo_list;
	}
	old_part_ptr = new_part_ptr;

	/* GRES used is only populated on nodes with detail flag */
	if (params.match_flags & MATCH_FLAG_GRES_USED)
		show_flags |= SHOW_DETAIL;

	if (old_node_ptr) {
		if (clear_old)
			old_node_ptr->last_update = 0;
		if (params.node_name_single) {
			error_code = slurm_load_node_single(&new_node_ptr,
							    params.nodes,
							    show_flags);
		} else {
			error_code = slurm_load_node(old_node_ptr->last_update,
						     &new_node_ptr, show_flags);
		}
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_node_ptr = old_node_ptr;
		}
	} else if (params.node_name_single) {
		error_code = slurm_load_node_single(&new_node_ptr, params.nodes,
						    show_flags);
	} else {
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
					     show_flags);
	}
	if (error_code) {
		slurm_perror("slurm_load_node");
		return sinfo_list;
	}
	old_node_ptr = new_node_ptr;

	sinfo_list = list_create(_sinfo_list_delete);
	_build_sinfo_data(sinfo_list, new_part_ptr, new_node_ptr);

	return sinfo_list;
}

static void *_load_job_prio_thread(void *args)
{
	load_info_struct_t *load_args = (load_info_struct_t *) args;
	uint16_t show_flags = SHOW_MIXED;
	char *node_name = NULL;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	int error_code;
	partition_info_msg_t *new_part_ptr;
	node_info_msg_t *new_node_ptr;
	list_t *sinfo_list = NULL;

	if (params.node_name_single)
		node_name = params.nodes;
	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (params.future_flag)
		show_flags |= SHOW_FUTURE;

	error_code = slurm_load_partitions2((time_t) NULL, &new_part_ptr,
					    show_flags, cluster);
	if (error_code) {
		slurm_perror("slurm_load_partitions");
		xfree(args);
		return NULL;
	}
	list_append(load_args->part_info_msg_list, new_part_ptr);

	if (node_name) {
		error_code = slurm_load_node_single2(&new_node_ptr, node_name,
						     show_flags, cluster);
	} else {
		error_code = slurm_load_node2((time_t) NULL, &new_node_ptr,
					      show_flags, cluster);
	}
	if (error_code) {
		slurm_perror("slurm_load_node");
		xfree(args);
		return NULL;
	}
	list_append(load_args->node_info_msg_list, new_node_ptr);

	sinfo_list = list_create(_sinfo_list_delete);
	_build_sinfo_data(sinfo_list, new_part_ptr, new_node_ptr);
	if (sinfo_list) {
		sinfo_data_t *sinfo_ptr;
		list_itr_t *iter;
		iter = list_iterator_create(sinfo_list);
		while ((sinfo_ptr = (sinfo_data_t *) list_next(iter)))
			sinfo_ptr->cluster_name = cluster->name;
		list_iterator_destroy(iter);
		list_transfer(load_args->resp_msg_list, sinfo_list);
		FREE_NULL_LIST(sinfo_list);
	}

	xfree(args);
	return NULL;
}

/*
 * _query_fed_servers - download the current server state in parallel for
 *		all clusters in a federation
 * fed IN - identification of clusters in federation
 * RET List of node/partition records
 */
static list_t *_query_fed_servers(slurmdb_federation_rec_t *fed,
				  list_t *node_info_msg_list,
				  list_t *part_info_msg_list)
{
	list_t *resp_msg_list;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	list_itr_t *iter;
	slurmdb_cluster_rec_t *cluster;
	load_info_struct_t *load_args;
	int i;

	/* Spawn one pthread per cluster to collect job information */
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	resp_msg_list = list_create(_sinfo_list_delete);
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */
		load_args = xmalloc(sizeof(load_info_struct_t));
		load_args->cluster = cluster;
		load_args->node_info_msg_list = node_info_msg_list;
		load_args->part_info_msg_list = part_info_msg_list;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(NULL, &load_thread[pthread_count],
				    _load_job_prio_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		slurm_thread_join(load_thread[i]);
	xfree(load_thread);

	return resp_msg_list;
}

static void _sinfo_hash_idfunc(void *item, const void **key, uint32_t *key_len)
{
	sinfo_data_t *sinfo_ptr = item;
	*key = sinfo_ptr->match_key;
	*key_len = sinfo_ptr->match_key_len;
}

/*
 * Append a length-prefixed "tag:len:value|" segment to *key so that
 * free-form strings (which may themselves contain ':' or '|') cannot
 * be crafted to make two distinct field combinations collide.
 */
static void _key_add_str(char **key, char **pos, const char *tag,
			 const char *val)
{
	/*
	 * Keep NULL apart from "". sinfo prints them differently, as "(null)"
	 * and as nothing, and the xstrcmp() comparison this key replaces never
	 * treated them as equal. A length is always digits, so "-" cannot
	 * collide with one.
	 */
	if (!val)
		xstrfmtcatat(*key, pos, "%s:-|", tag);
	else
		xstrfmtcatat(*key, pos, "%s:%zu:%s|", tag, strlen(val), val);
}

/*
 * Build the partition half of the match key.  Every node reached through a
 * given partition shares this prefix, so build it once per partition and let
 * _build_match_key() append the node half to a copy.
 */
static char *_build_part_match_key(partition_info_t *part_ptr)
{
	char *key = NULL;
	char *pos = NULL;

	/*
	 * Guarantee a non-NULL, non-empty key even when list_reasons is
	 * set and no match flags are active.  xhash_get_str() returns NULL
	 * immediately for a NULL key, so a NULL key would make every lookup
	 * miss and emit one row per node instead of grouping them.
	 */
	xstrfmtcatat(key, &pos, "k|");

	/* Partition data: not considered at all if list_reasons */
	if (!params.list_reasons) {
		/* Partition-level match fields */
		if (params.match_flags & MATCH_FLAG_PARTITION) {
			xstrfmtcatat(key, &pos, "%" PRIxPTR "|",
				     (uintptr_t) part_ptr);
			_key_add_str(&key, &pos, "pn", part_ptr->name);
		}
		if (params.match_flags & MATCH_FLAG_AVAIL)
			xstrfmtcatat(key, &pos, "av:%u|", part_ptr->state_up);
		if (params.match_flags & MATCH_FLAG_GROUPS)
			_key_add_str(&key, &pos, "pg", part_ptr->allow_groups);
		if (params.match_flags & MATCH_FLAG_JOB_SIZE)
			xstrfmtcatat(key, &pos, "js:%u:%u|",
				     part_ptr->min_nodes, part_ptr->max_nodes);
		if (params.match_flags & MATCH_FLAG_DEFAULT_TIME)
			xstrfmtcatat(key, &pos, "dt:%u|",
				     part_ptr->default_time);
		if (params.match_flags & MATCH_FLAG_MAX_TIME)
			xstrfmtcatat(key, &pos, "mt:%u|", part_ptr->max_time);
		if (params.match_flags & MATCH_FLAG_ROOT)
			xstrfmtcatat(key, &pos, "ro:%d|",
				     !!(part_ptr->flags & PART_FLAG_ROOT_ONLY));
		if (params.match_flags & MATCH_FLAG_OVERSUBSCRIBE)
			xstrfmtcatat(key, &pos, "os:%u|", part_ptr->max_share);
		if (params.match_flags & MATCH_FLAG_PREEMPT_MODE)
			xstrfmtcatat(key, &pos, "pm:%u|",
				     part_ptr->preempt_mode);
		if (params.match_flags & MATCH_FLAG_PRIORITY_TIER)
			xstrfmtcatat(key, &pos, "pt:%u|",
				     part_ptr->priority_tier);
		if (params.match_flags & MATCH_FLAG_PRIORITY_JOB_FACTOR)
			xstrfmtcatat(key, &pos, "pjf:%u|",
				     part_ptr->priority_job_factor);
		if (params.match_flags & MATCH_FLAG_MAX_CPUS_PER_NODE)
			xstrfmtcatat(key, &pos, "mc:%u|",
				     part_ptr->max_cpus_per_node);
	}

	return key;
}

/*
 * Build a composite string key encoding all active match criteria for a
 * (part_ptr, node_ptr) pair.  Two nodes that would be grouped together by
 * partition and node data produce identical keys.
 *
 * part_key IN - prefix from _build_part_match_key() for this node's partition
 */
static char *_build_match_key(const char *part_key, node_info_t *node_ptr,
			      uint32_t *key_len)
{
	char *key = xstrdup(part_key);
	char *pos = NULL;

	/* Node-level match fields (always checked) */
	if (params.match_flags & MATCH_FLAG_HOSTNAMES)
		_key_add_str(&key, &pos, "hn", node_ptr->node_hostname);
	if (params.match_flags & MATCH_FLAG_NODE_ADDR)
		_key_add_str(&key, &pos, "na", node_ptr->node_addr);
	if (params.match_flags & MATCH_FLAG_EXTRA)
		_key_add_str(&key, &pos, "ex", node_ptr->extra);
	if (params.match_flags & MATCH_FLAG_FEATURES)
		_key_add_str(&key, &pos, "ft", node_ptr->features);
	if (params.match_flags & MATCH_FLAG_FEATURES_ACT)
		_key_add_str(&key, &pos, "fa", node_ptr->features_act);
	if (params.match_flags & MATCH_FLAG_GRES)
		_key_add_str(&key, &pos, "gr", node_ptr->gres);
	if (params.match_flags & MATCH_FLAG_GRES_USED)
		_key_add_str(&key, &pos, "gu", node_ptr->gres_used);
	if (params.match_flags & MATCH_FLAG_COMMENT)
		_key_add_str(&key, &pos, "co", node_ptr->comment);
	if (params.match_flags & MATCH_FLAG_REASON)
		_key_add_str(&key, &pos, "re", node_ptr->reason);
	if (params.match_flags & MATCH_FLAG_REASON_TIMESTAMP)
		xstrfmtcatat(key, &pos, "rt:%" PRId64 "|",
			     (int64_t) node_ptr->reason_time);
	if (params.match_flags & MATCH_FLAG_REASON_USER)
		xstrfmtcatat(key, &pos, "ru:%" PRIu32 "|",
			     node_ptr->reason_uid);
	if (params.match_flags & MATCH_FLAG_RESV_NAME)
		_key_add_str(&key, &pos, "rn", node_ptr->resv_name);
	if (params.match_flags & MATCH_FLAG_STATE)
		_key_add_str(&key, &pos, "st",
			     node_state_string(node_ptr->node_state));
	if (params.match_flags & MATCH_FLAG_STATE_COMPLETE) {
		char *state = node_state_string_complete(node_ptr->node_state);
		_key_add_str(&key, &pos, "sc", state);
		xfree(state);
	}
	if (params.match_flags & MATCH_FLAG_ALLOC_MEM)
		xstrfmtcatat(key, &pos, "am:%" PRIu64 "|",
			     node_ptr->alloc_memory);

	/* Node-level match fields (exact match only) */
	if (params.exact_match) {
		if (params.match_flags & MATCH_FLAG_CPUS)
			xstrfmtcatat(key, &pos, "cp:%" PRIu16 "|",
				     node_ptr->cpus);
		if (params.match_flags & MATCH_FLAG_SOCKETS)
			xstrfmtcatat(key, &pos, "so:%" PRIu16 "|",
				     node_ptr->sockets);
		if (params.match_flags & MATCH_FLAG_CORES)
			xstrfmtcatat(key, &pos, "cr:%" PRIu16 "|",
				     node_ptr->cores);
		if (params.match_flags & MATCH_FLAG_THREADS)
			xstrfmtcatat(key, &pos, "th:%" PRIu16 "|",
				     node_ptr->threads);
		if (params.match_flags & MATCH_FLAG_SCT)
			xstrfmtcatat(key, &pos,
				     "sct:%" PRIu16 ":%" PRIu16 ":%" PRIu16 "|",
				     node_ptr->sockets, node_ptr->cores,
				     node_ptr->threads);
		if (params.match_flags & MATCH_FLAG_DISK)
			xstrfmtcatat(key, &pos, "dk:%" PRIu32 "|",
				     node_ptr->tmp_disk);
		if (params.match_flags & MATCH_FLAG_MEMORY)
			xstrfmtcatat(key, &pos, "me:%" PRIu64 "|",
				     node_ptr->real_memory);
		if (params.match_flags & MATCH_FLAG_WEIGHT)
			xstrfmtcatat(key, &pos, "wt:%" PRIu32 "|",
				     node_ptr->weight);
		if (params.match_flags & MATCH_FLAG_CPU_LOAD)
			xstrfmtcatat(key, &pos, "cl:%" PRIu32 "|",
				     node_ptr->cpu_load);
		if (params.match_flags & MATCH_FLAG_FREE_MEM)
			xstrfmtcatat(key, &pos, "fm:%" PRIu64 "|",
				     node_ptr->free_mem);
		if (params.match_flags & MATCH_FLAG_PORT)
			xstrfmtcatat(key, &pos, "po:%" PRIu16 "|",
				     node_ptr->port);
		if (params.match_flags & MATCH_FLAG_VERSION)
			_key_add_str(&key, &pos, "ve", node_ptr->version);
	}

	/*
	 * pos trails the last append, so the length is already known; the
	 * hash idfunc would otherwise walk the key again on every lookup.
	 */
	*key_len = pos ? (uint32_t) (pos - key) : (uint32_t) strlen(key);
	return key;
}

/* Build information about a partition using one pthread per partition */
void *_build_part_info(void *args)
{
	build_part_info_t *build_struct_ptr;
	list_t *sinfo_list;
	xhash_t *sinfo_hash;
	bool *part_has_nodes;
	partition_info_t *part_ptr;
	node_info_msg_t *node_msg;
	node_info_t *node_ptr = NULL;
	char *part_key;
	int part_num;
	int j = 0;

	build_struct_ptr = (build_part_info_t *) args;
	sinfo_list = build_struct_ptr->sinfo_list;
	sinfo_hash = build_struct_ptr->sinfo_hash;
	part_has_nodes = build_struct_ptr->part_has_nodes;
	part_num = build_struct_ptr->part_num;
	part_ptr = build_struct_ptr->part_ptr;
	node_msg = build_struct_ptr->node_msg;

	/*
	 * Every node this thread handles is reached through the same
	 * partition, so the partition half of the key is identical for all of
	 * them.  Build it once here rather than reformatting a dozen fields
	 * per node in the innermost loop.
	 */
	part_key = _build_part_match_key(part_ptr);

	while (part_ptr->node_inx[j] >= 0) {
		int i = 0;
		for (i = part_ptr->node_inx[j];
		     i <= part_ptr->node_inx[j+1]; i++) {
			if (i >= node_msg->record_count) {
				/* If info for single node name is loaded */
				break;
			}
			node_ptr = &(node_msg->node_array[i]);
			if (node_ptr->name == NULL)
				continue;
			_insert_node_ptr(sinfo_list, sinfo_hash, part_has_nodes,
					 part_num, part_key, part_ptr,
					 node_ptr);
		}
		j += 2;
	}

	xfree(part_key);
	xfree(args);
	slurm_mutex_lock(&sinfo_cnt_mutex);
	if (sinfo_cnt > 0) {
		sinfo_cnt--;
	} else {
		error("sinfo_cnt underflow");
		sinfo_cnt = 0;
	}
	slurm_cond_broadcast(&sinfo_cnt_cond);
	slurm_mutex_unlock(&sinfo_cnt_mutex);
	return NULL;
}

/*
 * _build_sinfo_data - make a sinfo_data entry for each unique node
 *	configuration and add it to the sinfo_list for later printing.
 * sinfo_list IN/OUT - list of unique sinfo_data records to report
 * partition_msg IN - partition info message
 * node_msg IN - node info message
 * RET zero or error code
 */
static int _build_sinfo_data(list_t *sinfo_list,
			     partition_info_msg_t *partition_msg,
			     node_info_msg_t *node_msg)
{
	build_part_info_t *build_struct_ptr;
	node_info_t *node_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	xhash_t *sinfo_hash;
	bool *part_has_nodes;
	char *part_key;
	int j;

	sinfo_hash = xhash_init(_sinfo_hash_idfunc, NULL);
	part_has_nodes = xcalloc(partition_msg->record_count, sizeof(bool));

	if (params.filtering) {
		for (j = 0; j < node_msg->record_count; j++) {
			node_ptr = &(node_msg->node_array[j]);
			if (node_ptr->name && _filter_out(node_ptr))
				xfree(node_ptr->name);
		}
	}

	/* make sinfo_list entries for every node in every partition */
	for (j = 0, part_ptr = partition_msg->partition_array;
	     j < partition_msg->record_count; j++, part_ptr++) {
		if (params.filtering && params.part_list &&
		    !list_find_first(params.part_list,
				     _find_part_list,
				     part_ptr->name))
			continue;

		if (node_msg->record_count == 1) { /* node_name_single */
			int pos = -1;
			hostlist_t *hl;

			node_ptr = &(node_msg->node_array[0]);
			if ((node_ptr->name == NULL) ||
			    (part_ptr->nodes == NULL))
				continue;
			hl = hostlist_create(part_ptr->nodes);
			pos = hostlist_find(hl, node_msg->node_array[0].name);
			hostlist_destroy(hl);
			if (pos < 0)
				continue;
			part_key = _build_part_match_key(part_ptr);
			_insert_node_ptr(sinfo_list, sinfo_hash, part_has_nodes,
					 j, part_key, part_ptr, node_ptr);
			xfree(part_key);
			continue;
		}

		/* Process each partition using a separate thread */
		build_struct_ptr = xmalloc(sizeof(build_part_info_t));
		build_struct_ptr->node_msg   = node_msg;
		build_struct_ptr->part_num = j;
		build_struct_ptr->part_ptr   = part_ptr;
		build_struct_ptr->sinfo_list = sinfo_list;
		build_struct_ptr->sinfo_hash = sinfo_hash;
		build_struct_ptr->part_has_nodes = part_has_nodes;

		slurm_mutex_lock(&sinfo_cnt_mutex);
		sinfo_cnt++;
		slurm_mutex_unlock(&sinfo_cnt_mutex);

		slurm_thread_create_detached(NULL, _build_part_info,
					     build_struct_ptr);
	}

	slurm_mutex_lock(&sinfo_cnt_mutex);
	while (sinfo_cnt) {
		slurm_cond_wait(&sinfo_cnt_cond, &sinfo_cnt_mutex);
	}
	slurm_mutex_unlock(&sinfo_cnt_mutex);

	xhash_free(sinfo_hash);

	/*
	 * Show every partition, even if it ended up with zero nodes -
	 * either because it has none, or because all of its nodes were
	 * filtered out (part_has_nodes is set only when a node was
	 * actually inserted, unlike part_ptr->node_inx which reflects the
	 * unfiltered RPC data).
	 */
	if (!params.node_flag && (params.match_flags & MATCH_FLAG_PARTITION)) {
		for (j = 0, part_ptr = partition_msg->partition_array;
		     j < partition_msg->record_count; j++, part_ptr++) {
			if (params.filtering && params.part_list &&
			    !list_find_first(params.part_list, _find_part_list,
					     part_ptr->name))
				continue;
			if (!part_has_nodes[j])
				list_append(sinfo_list,
					    _create_sinfo(part_ptr,
							  (uint16_t) j, NULL));
		}
	}

	xfree(part_has_nodes);
	_sort_hostlist(sinfo_list);
	return SLURM_SUCCESS;
}

static bool _filter_node_state(uint32_t node_state, node_info_t *node_ptr)
{
	bool match = false;
	uint32_t base_state;
	node_info_t tmp_node, *tmp_node_ptr = &tmp_node;

	if (node_state == SINFO_STATE_ALL)
		return true;

	tmp_node_ptr->node_state = node_state;

	if (node_state == NODE_STATE_DRAIN) {
		/*
		 * We search for anything that has the
		 * drain flag set
		 */
		if (IS_NODE_DRAIN(node_ptr)) {
			match = true;
		}
	} else if (IS_NODE_DRAINING(tmp_node_ptr)) {
		/*
		 * We search for anything that gets mapped to
		 * DRAINING in node_state_string
		 */
		if (IS_NODE_DRAINING(node_ptr)) {
			match = true;
		}
	} else if (IS_NODE_DRAINED(tmp_node_ptr)) {
		/*
		 * We search for anything that gets mapped to
		 * DRAINED in node_state_string
		 */
		if (IS_NODE_DRAINED(node_ptr)) {
			match = true;
		}
	} else if (node_state & NODE_STATE_FLAGS) {
		if (node_state & node_ptr->node_state) {
			match = true;
		}
	} else if (node_state == NODE_STATE_ALLOCATED) {
		if (node_ptr->alloc_cpus) {
			match = true;
		}
	} else {
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == node_state) {
			match = true;
		}
	}

	return match;
}

/*
 * _filter_out - Determine if the specified node should be filtered out or
 *	reported.
 * node_ptr IN - node to consider filtering out
 * RET - true if node should not be reported, false otherwise
 */
static bool _filter_out(node_info_t *node_ptr)
{
	static hostlist_t *host_list = NULL;

	if (params.nodes) {
		if (host_list == NULL)
			host_list = hostlist_create(params.nodes);
		if (hostlist_find (host_list, node_ptr->name) == -1)
			return true;
	}

	if (params.dead_nodes && !IS_NODE_NO_RESPOND(node_ptr))
		return true;

	if (params.responding_nodes && IS_NODE_NO_RESPOND(node_ptr))
		return true;

	if (params.state_list) {
		sinfo_state_t *node_state;
		bool match = false;
		list_itr_t *iterator;

		iterator = list_iterator_create(params.state_list);
		while ((node_state = list_next(iterator))) {
			match = _filter_node_state(node_state->state, node_ptr);
			if (node_state->op == SINFO_STATE_OP_NOT)
				match = !match;
			if (!params.state_list_and && match)
				break;
			if (params.state_list_and && !match)
				break;
		}
		list_iterator_destroy(iterator);
		if (!match)
			return true;
	}

	return false;
}

static void _sort_hostlist(list_t *sinfo_list)
{
	list_itr_t *i;
	sinfo_data_t *sinfo_ptr;

	i = list_iterator_create(sinfo_list);
	while ((sinfo_ptr = list_next(i)))
		hostlist_sort(sinfo_ptr->nodes);
	list_iterator_destroy(i);
}

static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr)
{
	uint32_t base_state;
	uint16_t used_cpus = 0;
	int total_cpus = 0;

	base_state = node_ptr->node_state & NODE_STATE_BASE;

	if (sinfo_ptr->nodes_total == 0) {	/* first node added */
		sinfo_ptr->node_state = node_ptr->node_state;
		sinfo_ptr->features   = node_ptr->features;
		sinfo_ptr->features_act = node_ptr->features_act;
		sinfo_ptr->gres       = node_ptr->gres;
		sinfo_ptr->gres_used  = node_ptr->gres_used;
		sinfo_ptr->comment    = node_ptr->comment;
		sinfo_ptr->extra      = node_ptr->extra;
		sinfo_ptr->reason     = node_ptr->reason;
		sinfo_ptr->reason_time= node_ptr->reason_time;
		sinfo_ptr->reason_uid = node_ptr->reason_uid;
		sinfo_ptr->resv_name  = node_ptr->resv_name;
		sinfo_ptr->min_cpus    = node_ptr->cpus;
		sinfo_ptr->max_cpus    = node_ptr->cpus;
		sinfo_ptr->min_sockets = node_ptr->sockets;
		sinfo_ptr->max_sockets = node_ptr->sockets;
		sinfo_ptr->min_cores   = node_ptr->cores;
		sinfo_ptr->max_cores   = node_ptr->cores;
		sinfo_ptr->min_threads = node_ptr->threads;
		sinfo_ptr->max_threads = node_ptr->threads;
		sinfo_ptr->min_disk   = node_ptr->tmp_disk;
		sinfo_ptr->max_disk   = node_ptr->tmp_disk;
		sinfo_ptr->min_mem    = node_ptr->real_memory;
		sinfo_ptr->max_mem    = node_ptr->real_memory;
		sinfo_ptr->port       = node_ptr->port;
		sinfo_ptr->min_weight = node_ptr->weight;
		sinfo_ptr->max_weight = node_ptr->weight;
		sinfo_ptr->min_cpu_load = node_ptr->cpu_load;
		sinfo_ptr->max_cpu_load = node_ptr->cpu_load;
		sinfo_ptr->min_free_mem = node_ptr->free_mem;
		sinfo_ptr->max_free_mem = node_ptr->free_mem;
		sinfo_ptr->max_cpus_per_node = sinfo_ptr->part_info->
					       max_cpus_per_node;
		sinfo_ptr->version    = node_ptr->version;
		sinfo_ptr->cpu_spec_list = node_ptr->cpu_spec_list;
		sinfo_ptr->mem_spec_limit = node_ptr->mem_spec_limit;
	} else if (hostlist_find(sinfo_ptr->nodes, node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		return;
	} else {
		if (sinfo_ptr->min_cpus > node_ptr->cpus)
			sinfo_ptr->min_cpus = node_ptr->cpus;
		if (sinfo_ptr->max_cpus < node_ptr->cpus)
			sinfo_ptr->max_cpus = node_ptr->cpus;

		if (sinfo_ptr->min_sockets > node_ptr->sockets)
			sinfo_ptr->min_sockets = node_ptr->sockets;
		if (sinfo_ptr->max_sockets < node_ptr->sockets)
			sinfo_ptr->max_sockets = node_ptr->sockets;

		if (sinfo_ptr->min_cores > node_ptr->cores)
			sinfo_ptr->min_cores = node_ptr->cores;
		if (sinfo_ptr->max_cores < node_ptr->cores)
			sinfo_ptr->max_cores = node_ptr->cores;

		if (sinfo_ptr->min_threads > node_ptr->threads)
			sinfo_ptr->min_threads = node_ptr->threads;
		if (sinfo_ptr->max_threads < node_ptr->threads)
			sinfo_ptr->max_threads = node_ptr->threads;

		if (sinfo_ptr->min_disk > node_ptr->tmp_disk)
			sinfo_ptr->min_disk = node_ptr->tmp_disk;
		if (sinfo_ptr->max_disk < node_ptr->tmp_disk)
			sinfo_ptr->max_disk = node_ptr->tmp_disk;

		if (sinfo_ptr->min_mem > node_ptr->real_memory)
			sinfo_ptr->min_mem = node_ptr->real_memory;
		if (sinfo_ptr->max_mem < node_ptr->real_memory)
			sinfo_ptr->max_mem = node_ptr->real_memory;

		if (sinfo_ptr->min_weight> node_ptr->weight)
			sinfo_ptr->min_weight = node_ptr->weight;
		if (sinfo_ptr->max_weight < node_ptr->weight)
			sinfo_ptr->max_weight = node_ptr->weight;

		if (sinfo_ptr->min_cpu_load > node_ptr->cpu_load)
			sinfo_ptr->min_cpu_load = node_ptr->cpu_load;
		if (sinfo_ptr->max_cpu_load < node_ptr->cpu_load)
			sinfo_ptr->max_cpu_load = node_ptr->cpu_load;

		if (sinfo_ptr->min_free_mem > node_ptr->free_mem)
			sinfo_ptr->min_free_mem = node_ptr->free_mem;
		if (sinfo_ptr->max_free_mem < node_ptr->free_mem)
			sinfo_ptr->max_free_mem = node_ptr->free_mem;
	}

	if (hostlist_find(sinfo_ptr->nodes, node_ptr->name) == -1)
		hostlist_push_host(sinfo_ptr->nodes, node_ptr->name);
	if ((params.match_flags & MATCH_FLAG_NODE_ADDR) &&
	    (hostlist_find(sinfo_ptr->node_addr, node_ptr->node_addr) == -1))
		hostlist_push_host(sinfo_ptr->node_addr, node_ptr->node_addr);
	if ((params.match_flags & MATCH_FLAG_HOSTNAMES) &&
	    (hostlist_find(sinfo_ptr->hostnames, node_ptr->node_hostname) == -1))
		hostlist_push_host(sinfo_ptr->hostnames, node_ptr->node_hostname);

	total_cpus = node_ptr->cpus;
	used_cpus = node_ptr->alloc_cpus;

	if ((base_state == NODE_STATE_ALLOCATED) ||
	    (base_state == NODE_STATE_MIXED) ||
	    IS_NODE_COMPLETING(node_ptr))
		sinfo_ptr->nodes_alloc++;
	else if (IS_NODE_DRAIN(node_ptr)
		 || (base_state == NODE_STATE_DOWN))
		sinfo_ptr->nodes_other++;
	else
		sinfo_ptr->nodes_idle++;

	sinfo_ptr->nodes_total++;

	sinfo_ptr->cpus_alloc += used_cpus;
	sinfo_ptr->cpus_total += total_cpus;
	total_cpus -= used_cpus;
	sinfo_ptr->alloc_memory = node_ptr->alloc_memory;

	if (IS_NODE_DRAIN(node_ptr) || (base_state == NODE_STATE_DOWN)) {
		sinfo_ptr->cpus_other += total_cpus;
	} else
		sinfo_ptr->cpus_idle += total_cpus;
}

static int _insert_node_ptr(list_t *sinfo_list, xhash_t *sinfo_hash,
			    bool *part_has_nodes, int part_num,
			    const char *part_key, partition_info_t *part_ptr,
			    node_info_t *node_ptr)
{
	sinfo_data_t *sinfo_ptr = NULL;
	char *key;
	uint32_t key_len = 0;

	/*
	 * -N asks for one line per node-partition pair, so nothing is ever
	 * grouped and a lookup could only ever miss. Skip the hash entirely.
	 */
	if (params.node_flag) {
		slurm_mutex_lock(&sinfo_list_mutex);
		part_has_nodes[part_num] = true;
		list_append(sinfo_list,
			    _create_sinfo(part_ptr, (uint16_t) part_num,
					  node_ptr));
		slurm_mutex_unlock(&sinfo_list_mutex);
		return SLURM_SUCCESS;
	}

	key = _build_match_key(part_key, node_ptr, &key_len);

	slurm_mutex_lock(&sinfo_list_mutex);

	part_has_nodes[part_num] = true;

	sinfo_ptr = xhash_get(sinfo_hash, key, key_len);
	if (sinfo_ptr) {
		_update_sinfo(sinfo_ptr, node_ptr);
		slurm_mutex_unlock(&sinfo_list_mutex);
		xfree(key);
		return SLURM_SUCCESS;
	}

	sinfo_ptr = _create_sinfo(part_ptr, (uint16_t) part_num, node_ptr);
	sinfo_ptr->match_key = key;
	sinfo_ptr->match_key_len = key_len;
	list_append(sinfo_list, sinfo_ptr);
	xhash_add(sinfo_hash, sinfo_ptr);

	slurm_mutex_unlock(&sinfo_list_mutex);
	return SLURM_SUCCESS;
}

/*
 * _create_sinfo - create an sinfo record for the given node and partition
 * sinfo_list IN/OUT - table of accumulated sinfo records
 * part_ptr IN       - pointer to partition record to add
 * part_inx IN       - index of partition record (0-origin)
 * node_ptr IN       - pointer to node record to add
 */
static sinfo_data_t *_create_sinfo(partition_info_t* part_ptr,
				   uint16_t part_inx, node_info_t *node_ptr)
{
	sinfo_data_t *sinfo_ptr;
	/* create an entry */
	sinfo_ptr = xmalloc(sizeof(sinfo_data_t));

	sinfo_ptr->part_info = part_ptr;
	sinfo_ptr->part_inx = part_inx;
	sinfo_ptr->nodes     = hostlist_create(NULL);
	sinfo_ptr->node_addr = hostlist_create(NULL);
	sinfo_ptr->hostnames = hostlist_create(NULL);

	if (node_ptr)
		_update_sinfo(sinfo_ptr, node_ptr);

	return sinfo_ptr;
}

static void _node_list_delete(void *data)
{
	node_info_msg_t *old_node_ptr = data;

	slurm_free_node_info_msg(old_node_ptr);
}

static void _part_list_delete(void *data)
{
	partition_info_msg_t *old_part_ptr = data;

	slurm_free_partition_info_msg(old_part_ptr);
}

static void _sinfo_list_delete(void *data)
{
	sinfo_data_t *sinfo_ptr = data;

	xfree(sinfo_ptr->match_key);
	hostlist_destroy(sinfo_ptr->nodes);
	hostlist_destroy(sinfo_ptr->node_addr);
	hostlist_destroy(sinfo_ptr->hostnames);
	xfree(sinfo_ptr);
}

/* Find the given partition name in the list */
static int _find_part_list(void *x, void *key)
{
	if (!xstrcmp((char *)x, (char *)key))
		return 1;
	return 0;
}
