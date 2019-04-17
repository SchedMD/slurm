/*****************************************************************************\
 *  sinfo.c - Report overall state the system
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

#include "slurm/slurmdb.h"
#include "src/common/xstring.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/sinfo/sinfo.h"
#include "src/sinfo/print.h"

/********************
 * Global Variables *
 ********************/
typedef struct build_part_info {
	node_info_msg_t *node_msg;
	uint16_t part_num;
	partition_info_t *part_ptr;
	List sinfo_list;
} build_part_info_t;

/* Data structures for pthreads used to gather node/partition information from
 * multiple clusters in parallel */
typedef struct load_info_struct {
	slurmdb_cluster_rec_t *cluster;
	List node_info_msg_list;
	List part_info_msg_list;
	List resp_msg_list;
} load_info_struct_t;

struct sinfo_parameters params;

static int sinfo_cnt;	/* thread count */
static pthread_mutex_t sinfo_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sinfo_cnt_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t sinfo_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*************
 * Functions *
 *************/
void *      _build_part_info(void *args);
static int  _build_sinfo_data(List sinfo_list,
			      partition_info_msg_t *partition_msg,
			      node_info_msg_t *node_msg);
static sinfo_data_t *_create_sinfo(partition_info_t* part_ptr,
				   uint16_t part_inx, node_info_t *node_ptr);
static int  _find_part_list(void *x, void *key);
static bool _filter_out(node_info_t *node_ptr);
static int  _get_info(bool clear_old, slurmdb_federation_rec_t *fed);
static int  _insert_node_ptr(List sinfo_list, uint16_t part_num,
			     partition_info_t *part_ptr,
			     node_info_t *node_ptr);
static int  _load_resv(reserve_info_msg_t ** reserv_pptr, bool clear_old);
static bool _match_node_data(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr);
static bool _match_part_data(sinfo_data_t *sinfo_ptr,
			     partition_info_t* part_ptr);
static int  _multi_cluster(List clusters);
static void _node_list_delete(void *data);
static void _part_list_delete(void *data);
static List _query_fed_servers(slurmdb_federation_rec_t *fed,
			       List node_info_msg_list,
			       List part_info_msg_list);
static List _query_server(bool clear_old);
static int  _reservation_report(reserve_info_msg_t *resv_ptr);
static bool _serial_part_data(void);
static void _sinfo_list_delete(void *data);
static void _sort_hostlist(List sinfo_list);
static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr);

int main(int argc, char **argv)
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc = 0;

	slurm_conf_init(NULL);
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);
	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

	while (1) {
		if ((!params.no_header) &&
		    (params.iterate || params.verbose || params.long_output))
			print_date();

		if (!params.clusters) {
			if (_get_info(false, params.fed))
				rc = 1;
		} else if (_multi_cluster(params.clusters) != 0)
			rc = 1;
		if (params.iterate) {
			printf("\n");
			sleep(params.iterate);
		} else
			break;
	}

	exit(rc);
}

static int _multi_cluster(List clusters)
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
		rc2 = _get_info(true, NULL);
		rc = MAX(rc, rc2);
	}
	list_iterator_destroy(itr);

	return rc;
}

/* clear_old IN - if set then don't preserve old info (it might be from
 *		  another cluster)
 * fed IN - information about other clusters in this federation
 */
static int _get_info(bool clear_old, slurmdb_federation_rec_t *fed)
{
	List node_info_msg_list = NULL, part_info_msg_list = NULL;
	reserve_info_msg_t *reserv_msg = NULL;
	List sinfo_list = NULL;
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

	sort_sinfo_list(sinfo_list);
	print_sinfo_list(sinfo_list);

	FREE_NULL_LIST(node_info_msg_list);
	FREE_NULL_LIST(part_info_msg_list);
	FREE_NULL_LIST(sinfo_list);
	return SLURM_SUCCESS;
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
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
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
static List _query_server(bool clear_old)
{
	static partition_info_msg_t *old_part_ptr = NULL, *new_part_ptr;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;
	uint16_t show_flags = 0;
	int cc;
	node_info_t *node_ptr;
	List sinfo_list = NULL;

	if (params.all_flag)
		show_flags |= SHOW_ALL;

	if (old_part_ptr) {
		if (clear_old)
			old_part_ptr->last_update = 0;
		error_code = slurm_load_partitions(old_part_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(old_part_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
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
	if (params.match_flags.gres_used_flag)
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
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
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

	/* Set the node state as NODE_STATE_MIXED. */
	for (cc = 0; cc < new_node_ptr->record_count; cc++) {
		node_ptr = &(new_node_ptr->node_array[cc]);
		if (IS_NODE_DRAIN(node_ptr)) {
			/* don't worry about mixed since the
			 * whole node is being drained. */
		} else {
			uint16_t alloc_cpus = 0, idle_cpus;

			select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
						     SELECT_NODEDATA_SUBCNT,
						     NODE_STATE_ALLOCATED,
						     &alloc_cpus);
			idle_cpus = node_ptr->cpus - alloc_cpus;

			if (idle_cpus && (idle_cpus != node_ptr->cpus)) {
				node_ptr->node_state &= NODE_STATE_FLAGS;
				node_ptr->node_state |= NODE_STATE_MIXED;
			}
		}
	}

	sinfo_list = list_create(_sinfo_list_delete);
	_build_sinfo_data(sinfo_list, new_part_ptr, new_node_ptr);

	return sinfo_list;
}

static void *_load_job_prio_thread(void *args)
{
	load_info_struct_t *load_args = (load_info_struct_t *) args;
	uint16_t show_flags = 0;
	char *node_name = NULL;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	int error_code;
	partition_info_msg_t *new_part_ptr;
	node_info_msg_t *new_node_ptr;
	int cc;
	node_info_t *node_ptr;
	List sinfo_list = NULL;

	if (params.node_name_single)
		node_name = params.nodes;
	if (params.all_flag)
		show_flags |= SHOW_ALL;

	error_code = slurm_load_partitions2((time_t) NULL, &new_part_ptr,
					    show_flags, cluster);
	if (error_code) {
		slurm_perror("slurm_load_partitions");
		xfree(args);
		return (void *) NULL;
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
		return (void *) NULL;
	}
	list_append(load_args->node_info_msg_list, new_node_ptr);

	/* Set the node state as NODE_STATE_MIXED. */
	for (cc = 0; cc < new_node_ptr->record_count; cc++) {
		node_ptr = &(new_node_ptr->node_array[cc]);
		if (IS_NODE_DRAIN(node_ptr)) {
			/* don't worry about mixed since the
			 * whole node is being drained. */
		} else {
			uint16_t alloc_cpus = 0, idle_cpus;

			select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
						     SELECT_NODEDATA_SUBCNT,
						     NODE_STATE_ALLOCATED,
						     &alloc_cpus);
			idle_cpus = node_ptr->cpus - alloc_cpus;

			if (idle_cpus && (idle_cpus != node_ptr->cpus)) {
				node_ptr->node_state &= NODE_STATE_FLAGS;
				node_ptr->node_state |= NODE_STATE_MIXED;
			}
		}
	}

	sinfo_list = list_create(_sinfo_list_delete);
	_build_sinfo_data(sinfo_list, new_part_ptr, new_node_ptr);
	if (sinfo_list) {
		sinfo_data_t *sinfo_ptr;
		ListIterator iter;
		iter = list_iterator_create(sinfo_list);
		while ((sinfo_ptr = (sinfo_data_t *) list_next(iter)))
			sinfo_ptr->cluster_name = cluster->name;
		list_iterator_destroy(iter);
		list_transfer(load_args->resp_msg_list, sinfo_list);
		list_destroy(sinfo_list);
	}

	xfree(args);
	return (void *) NULL;
}

/*
 * _query_fed_servers - download the current server state in parallel for
 *		all clusters in a federation
 * fed IN - identification of clusters in federation
 * RET List of node/partition records
 */
static List _query_fed_servers(slurmdb_federation_rec_t *fed,
			       List node_info_msg_list,
			       List part_info_msg_list)
{
	List resp_msg_list;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	ListIterator iter;
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
		slurm_thread_create(&load_thread[pthread_count],
				    _load_job_prio_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		pthread_join(load_thread[i], NULL);
	xfree(load_thread);

	return resp_msg_list;
}

/* Build information about a partition using one pthread per partition */
void *_build_part_info(void *args)
{
	build_part_info_t *build_struct_ptr;
	List sinfo_list;
	partition_info_t *part_ptr;
	node_info_msg_t *node_msg;
	node_info_t *node_ptr = NULL;
	uint16_t part_num;
	int j = 0;

	if (_serial_part_data())
		slurm_mutex_lock(&sinfo_list_mutex);
	build_struct_ptr = (build_part_info_t *) args;
	sinfo_list = build_struct_ptr->sinfo_list;
	part_num = build_struct_ptr->part_num;
	part_ptr = build_struct_ptr->part_ptr;
	node_msg = build_struct_ptr->node_msg;

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

			_insert_node_ptr(sinfo_list, part_num,
					 part_ptr, node_ptr);
		}
		j += 2;
	}

	xfree(args);
	if (_serial_part_data())
		slurm_mutex_unlock(&sinfo_list_mutex);
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
static int _build_sinfo_data(List sinfo_list,
			     partition_info_msg_t *partition_msg,
			     node_info_msg_t *node_msg)
{
	build_part_info_t *build_struct_ptr;
	node_info_t *node_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	int j;

	/* by default every partition is shown, even if no nodes */
	if ((!params.node_flag) && params.match_flags.partition_flag) {
		part_ptr = partition_msg->partition_array;
		for (j = 0; j < partition_msg->record_count; j++, part_ptr++) {
			if ((!params.part_list) ||
			    (list_find_first(params.part_list,
					     _find_part_list,
					     part_ptr->name))) {
				list_append(sinfo_list, _create_sinfo(
						    part_ptr, (uint16_t) j,
						    NULL));
			}
		}
	}

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
			hostlist_t hl;

			node_ptr = &(node_msg->node_array[0]);
			if ((node_ptr->name == NULL) ||
			    (part_ptr->nodes == NULL))
				continue;
			hl = hostlist_create(part_ptr->nodes);
			pos = hostlist_find(hl, node_msg->node_array[0].name);
			hostlist_destroy(hl);
			if (pos < 0)
				continue;
			_insert_node_ptr(sinfo_list, (uint16_t) j,
					 part_ptr, node_ptr);
			continue;
		}

		/* Process each partition using a separate thread */
		build_struct_ptr = xmalloc(sizeof(build_part_info_t));
		build_struct_ptr->node_msg   = node_msg;
		build_struct_ptr->part_num   = (uint16_t) j;
		build_struct_ptr->part_ptr   = part_ptr;
		build_struct_ptr->sinfo_list = sinfo_list;

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

	_sort_hostlist(sinfo_list);
	return SLURM_SUCCESS;
}

/*
 * _filter_out - Determine if the specified node should be filtered out or
 *	reported.
 * node_ptr IN - node to consider filtering out
 * RET - true if node should not be reported, false otherwise
 */
static bool _filter_out(node_info_t *node_ptr)
{
	static hostlist_t host_list = NULL;

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
		int *node_state;
		bool match = false;
		uint32_t base_state;
		ListIterator iterator;
		uint16_t cpus = 0;
		node_info_t tmp_node, *tmp_node_ptr = &tmp_node;

		iterator = list_iterator_create(params.state_list);
		while ((node_state = list_next(iterator))) {
			tmp_node_ptr->node_state = *node_state;
			if (*node_state == NODE_STATE_DRAIN) {
				/* We search for anything that has the
				 * drain flag set */
				if (IS_NODE_DRAIN(node_ptr)) {
					match = true;
					break;
				}
			} else if (IS_NODE_DRAINING(tmp_node_ptr)) {
				/* We search for anything that gets mapped to
				 * DRAINING in node_state_string */
				if (IS_NODE_DRAINING(node_ptr)) {
					match = true;
					break;
				}
			} else if (IS_NODE_DRAINED(tmp_node_ptr)) {
				/* We search for anything that gets mapped to
				 * DRAINED in node_state_string */
				if (IS_NODE_DRAINED(node_ptr)) {
					match = true;
					break;
				}
			} else if (*node_state & NODE_STATE_FLAGS) {
				if (*node_state & node_ptr->node_state) {
					match = true;
					break;
				}
			} else if (*node_state == NODE_STATE_ALLOCATED) {
				slurm_get_select_nodeinfo(
					node_ptr->select_nodeinfo,
					SELECT_NODEDATA_SUBCNT,
					NODE_STATE_ALLOCATED,
					&cpus);
				if (cpus) {
					match = true;
					break;
				}
			} else if (*node_state == NODE_STATE_IDLE) {
				base_state = node_ptr->node_state &
					(~NODE_STATE_NO_RESPOND);
				if (base_state == NODE_STATE_IDLE) {
					match = true;
					break;
				}
			} else {
				base_state =
					node_ptr->node_state & NODE_STATE_BASE;
				if (base_state == *node_state) {
					match = true;
					break;
				}
			}
		}
		list_iterator_destroy(iterator);
		if (!match)
			return true;
	}

	return false;
}

static void _sort_hostlist(List sinfo_list)
{
	ListIterator i;
	sinfo_data_t *sinfo_ptr;

	i = list_iterator_create(sinfo_list);
	while ((sinfo_ptr = list_next(i)))
		hostlist_sort(sinfo_ptr->nodes);
	list_iterator_destroy(i);
}

/* Return false if this node's data needs to be added to sinfo's table of
 * data to print. Return true if it is duplicate/redundant data. */
static bool _match_node_data(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr)
{
	uint64_t tmp = 0;

	if (params.node_flag)
		return false;

	if (params.match_flags.hostnames_flag &&
	    (hostlist_find(sinfo_ptr->hostnames,
			   node_ptr->node_hostname) == -1))
		return false;

	if (params.match_flags.node_addr_flag &&
	    (hostlist_find(sinfo_ptr->node_addr, node_ptr->node_addr) == -1))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.features_flag &&
	    (xstrcmp(node_ptr->features, sinfo_ptr->features)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.features_act_flag &&
	    (xstrcmp(node_ptr->features_act, sinfo_ptr->features_act)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.gres_flag &&
	    (xstrcmp(node_ptr->gres, sinfo_ptr->gres)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.gres_used_flag &&
	    (xstrcmp(node_ptr->gres_used, sinfo_ptr->gres_used)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.reason_flag &&
	    (xstrcmp(node_ptr->reason, sinfo_ptr->reason)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.reason_timestamp_flag &&
	    (node_ptr->reason_time != sinfo_ptr->reason_time))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.reason_user_flag &&
	    node_ptr->reason_uid != sinfo_ptr->reason_uid) {
		return false;
	}

	if (params.match_flags.state_flag) {
		char *state1, *state2;
		state1 = node_state_string(node_ptr->node_state);
		state2 = node_state_string(sinfo_ptr->node_state);
		if (xstrcmp(state1, state2))
			return false;
	}

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_MEM_ALLOC,
				     NODE_STATE_ALLOCATED,
				     &tmp);
	if (params.match_flags.alloc_mem_flag &&
	    (tmp != sinfo_ptr->alloc_memory))
		return false;

	/* If no need to exactly match sizes, just return here
	 * otherwise check cpus, disk, memory and weigth individually */
	if (!params.exact_match)
		return true;

	if (params.match_flags.cpus_flag &&
	    (node_ptr->cpus        != sinfo_ptr->min_cpus))
		return false;

	if (params.match_flags.sockets_flag &&
	    (node_ptr->sockets     != sinfo_ptr->min_sockets))
		return false;
	if (params.match_flags.cores_flag &&
	    (node_ptr->cores       != sinfo_ptr->min_cores))
		return false;
	if (params.match_flags.threads_flag &&
	    (node_ptr->threads     != sinfo_ptr->min_threads))
		return false;
	if (params.match_flags.sct_flag &&
	    ((node_ptr->sockets     != sinfo_ptr->min_sockets) ||
	     (node_ptr->cores       != sinfo_ptr->min_cores) ||
	     (node_ptr->threads     != sinfo_ptr->min_threads)))
		return false;
	if (params.match_flags.disk_flag &&
	    (node_ptr->tmp_disk    != sinfo_ptr->min_disk))
		return false;
	if (params.match_flags.memory_flag &&
	    (node_ptr->real_memory != sinfo_ptr->min_mem))
		return false;
	if (params.match_flags.weight_flag &&
	    (node_ptr->weight      != sinfo_ptr->min_weight))
		return false;
	if (params.match_flags.cpu_load_flag &&
	    (node_ptr->cpu_load        != sinfo_ptr->min_cpu_load))
		return false;
	if (params.match_flags.free_mem_flag &&
	    (node_ptr->free_mem        != sinfo_ptr->min_free_mem))
		return false;
	if (params.match_flags.port_flag &&
	    (node_ptr->port != sinfo_ptr->port))
		return false;
	if (params.match_flags.version_flag &&
	    (node_ptr->version     != sinfo_ptr->version))
		return false;

	return true;
}

/* Return true if the processing of partition data must be serialized. In that
 * case, multiple partitions can write into the same sinfo data structure
 * entries. The logic here is similar to that in _match_part_data() below. */
static bool _serial_part_data(void)
{
	if (params.list_reasons)	/* Don't care about partition */
		return true;
	if (params.match_flags.partition_flag)	/* Match partition name */
		return false;
	return true;
}

static bool _match_part_data(sinfo_data_t *sinfo_ptr,
			     partition_info_t* part_ptr)
{
	if (params.list_reasons)	/* Don't care about partition */
		return true;
	if (part_ptr == sinfo_ptr->part_info) /* identical partition */
		return true;
	if ((part_ptr == NULL) || (sinfo_ptr->part_info == NULL))
		return false;

	if (params.match_flags.partition_flag
	    && (xstrcmp(part_ptr->name, sinfo_ptr->part_info->name)))
		return false;

	if (params.match_flags.avail_flag &&
	    (part_ptr->state_up != sinfo_ptr->part_info->state_up))
		return false;

	if (params.match_flags.groups_flag &&
	    (xstrcmp(part_ptr->allow_groups,
		     sinfo_ptr->part_info->allow_groups)))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->min_nodes != sinfo_ptr->part_info->min_nodes))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->max_nodes != sinfo_ptr->part_info->max_nodes))
		return false;

	if (params.match_flags.default_time_flag &&
	    (part_ptr->default_time != sinfo_ptr->part_info->default_time))
		return false;

	if (params.match_flags.max_time_flag &&
	    (part_ptr->max_time != sinfo_ptr->part_info->max_time))
		return false;

	if (params.match_flags.root_flag &&
	    ((part_ptr->flags & PART_FLAG_ROOT_ONLY) !=
	     (sinfo_ptr->part_info->flags & PART_FLAG_ROOT_ONLY)))
		return false;

	if (params.match_flags.oversubscribe_flag &&
	    (part_ptr->max_share != sinfo_ptr->part_info->max_share))
		return false;

	if (params.match_flags.preempt_mode_flag &&
	    (part_ptr->preempt_mode != sinfo_ptr->part_info->preempt_mode))
		return false;

	if (params.match_flags.priority_tier_flag &&
	    (part_ptr->priority_tier != sinfo_ptr->part_info->priority_tier))
		return false;

	if (params.match_flags.priority_job_factor_flag &&
	    (part_ptr->priority_job_factor !=
	     sinfo_ptr->part_info->priority_job_factor))
		return false;

	if (params.match_flags.max_cpus_per_node_flag &&
	    (part_ptr->max_cpus_per_node !=
	     sinfo_ptr->part_info->max_cpus_per_node))
		return false;

	return true;
}

static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr)
{
	uint32_t base_state;
	uint64_t alloc_mem = 0;
	uint16_t used_cpus = 0;
	int total_cpus = 0;

	base_state = node_ptr->node_state & NODE_STATE_BASE;

	if (sinfo_ptr->nodes_total == 0) {	/* first node added */
		sinfo_ptr->node_state = node_ptr->node_state;
		sinfo_ptr->features   = node_ptr->features;
		sinfo_ptr->features_act = node_ptr->features_act;
		sinfo_ptr->gres       = node_ptr->gres;
		sinfo_ptr->gres_used  = node_ptr->gres_used;
		sinfo_ptr->reason     = node_ptr->reason;
		sinfo_ptr->reason_time= node_ptr->reason_time;
		sinfo_ptr->reason_uid = node_ptr->reason_uid;
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
	if (params.match_flags.node_addr_flag &&
	    (hostlist_find(sinfo_ptr->node_addr, node_ptr->node_addr) == -1))
		hostlist_push_host(sinfo_ptr->node_addr, node_ptr->node_addr);
	if (params.match_flags.hostnames_flag &&
	    (hostlist_find(sinfo_ptr->hostnames, node_ptr->node_hostname) == -1))
		hostlist_push_host(sinfo_ptr->hostnames, node_ptr->node_hostname);

	total_cpus = node_ptr->cpus;

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ALLOCATED,
				     &used_cpus);
	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_MEM_ALLOC,
				     NODE_STATE_ALLOCATED,
				     &alloc_mem);

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
	sinfo_ptr->alloc_memory = alloc_mem;

	if (IS_NODE_DRAIN(node_ptr) || (base_state == NODE_STATE_DOWN)) {
		sinfo_ptr->cpus_other += total_cpus;
	} else
		sinfo_ptr->cpus_idle += total_cpus;
}

static int _insert_node_ptr(List sinfo_list, uint16_t part_num,
			    partition_info_t *part_ptr,
			    node_info_t *node_ptr)
{
	int rc = SLURM_SUCCESS;
	sinfo_data_t *sinfo_ptr = NULL;
	ListIterator itr = NULL;

	itr = list_iterator_create(sinfo_list);
	while ((sinfo_ptr = list_next(itr))) {
		if (!_match_part_data(sinfo_ptr, part_ptr))
			continue;
		if (sinfo_ptr->nodes_total &&
		    (!_match_node_data(sinfo_ptr, node_ptr)))
			continue;
		_update_sinfo(sinfo_ptr, node_ptr);
		break;
	}
	list_iterator_destroy(itr);

	/* if no match, create new sinfo_data entry */
	if (!sinfo_ptr) {
		list_append(sinfo_list,
			    _create_sinfo(part_ptr, part_num, node_ptr));
	}

	return rc;
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
