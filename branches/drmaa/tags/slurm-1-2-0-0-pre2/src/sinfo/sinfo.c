/*****************************************************************************\
 *  sinfo.c - Report overall state the system
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/api/node_select_info.h"
#include "src/common/xstring.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/sinfo/sinfo.h"
#include "src/sinfo/print.h"

#ifdef HAVE_BG_FILES
# include "src/plugins/select/bluegene/wrap_rm_api.h"
#endif

/********************
 * Global Variables *
 ********************/
struct sinfo_parameters params;
 
/************
 * Funtions *
 ************/
static int  _bg_report(void);
static int  _build_sinfo_data(List sinfo_list, 
		partition_info_msg_t *partition_msg,
		node_info_msg_t *node_msg);
static void _create_sinfo(List sinfo_list, partition_info_t* part_ptr, 
			  uint16_t part_inx, node_info_t *node_ptr);
static bool _filter_out(node_info_t *node_ptr);
static void _sinfo_list_delete(void *data);
static node_info_t *_find_node(char *node_name, node_info_msg_t *node_msg); 
static bool _match_node_data(sinfo_data_t *sinfo_ptr, 
                             node_info_t *node_ptr);
static bool _match_part_data(sinfo_data_t *sinfo_ptr, 
                             partition_info_t* part_ptr);
static int  _query_server(partition_info_msg_t ** part_pptr,
		node_info_msg_t ** node_pptr);
static void _sort_hostlist(List sinfo_list);
static int  _strcmp(char *data1, char *data2);
static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	partition_info_msg_t *partition_msg = NULL;
	node_info_msg_t *node_msg = NULL;
	List sinfo_list = NULL;
	int rc = 0;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);

	while (1) {
		if ((!params.no_header)
		&&  (params.iterate || params.verbose || params.long_output))
			print_date();

		if (params.bg_flag)
			(void) _bg_report();
		else if (_query_server(&partition_msg, &node_msg) != 0)
			rc = 1;
		else {
			sinfo_list = list_create(_sinfo_list_delete);
			_build_sinfo_data(sinfo_list, partition_msg, node_msg);
	 		sort_sinfo_list(sinfo_list);
			print_sinfo_list(sinfo_list);
		}
		if (params.iterate) {
			if (sinfo_list) {
				list_destroy(sinfo_list);
				sinfo_list = NULL;
			}
			printf("\n");
			sleep(params.iterate);
		} else
			break;
	}

	exit(rc);
}

static char *_conn_type_str(int conn_type)
{
	switch (conn_type) {
		case (SELECT_MESH):
			return "MESH";
		case (SELECT_TORUS):
			return "TORUS";
	}
	return "?";
}

static char *_node_use_str(int node_use)
{
	switch (node_use) {
		case (SELECT_COPROCESSOR_MODE):
			return "COPROCESSOR";
		case (SELECT_VIRTUAL_NODE_MODE):
			return "VIRTUAL";
	}
	return "?";
}

static char *_part_state_str(int state)
{
	static char tmp[16];

#ifdef HAVE_BG_FILES
	switch (state) {
		case RM_PARTITION_BUSY:
			return "BUSY";
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

/*
 * _bg_report - download and print current bgblock state information
 */
static int _bg_report(void)
{
	static node_select_info_msg_t *old_bg_ptr = NULL, *new_bg_ptr;
	int error_code, i;

	if (old_bg_ptr) {
		error_code = slurm_load_node_select(old_bg_ptr->last_update, 
				&new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&new_bg_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = old_bg_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
				&new_bg_ptr);
	}
	if (error_code) {
		slurm_perror("slurm_load_node_select");
		return error_code;
	}

	if (!params.no_header)
		printf("BG_BLOCK         NODES        OWNER    STATE    CONNECTION USE\n");
/*                      1234567890123456 123456789012 12345678 12345678 1234567890 12345+ */
/*                      RMP_22Apr1544018 bg[123x456]  name     READY    TORUS      COPROCESSOR */

	for (i=0; i<new_bg_ptr->record_count; i++) {
		printf("%-16.16s %-12.12s %-8.8s %-8.8s %-10.10s %s\n",
			new_bg_ptr->bg_info_array[i].bg_block_id,
			new_bg_ptr->bg_info_array[i].nodes,
			new_bg_ptr->bg_info_array[i].owner_name,
			_part_state_str(new_bg_ptr->bg_info_array[i].state),
			_conn_type_str(new_bg_ptr->bg_info_array[i].conn_type),
			_node_use_str(new_bg_ptr->bg_info_array[i].node_use));
	}

	return SLURM_SUCCESS;
}

/*
 * _query_server - download the current server state
 * part_pptr IN/OUT - partition information message
 * node_pptr IN/OUT - node information message 
 * RET zero or error code
 */
static int
_query_server(partition_info_msg_t ** part_pptr,
	      node_info_msg_t ** node_pptr)
{
	static partition_info_msg_t *old_part_ptr = NULL, *new_part_ptr;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;
	uint16_t show_flags = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
		
	if (old_part_ptr) {
		error_code =
		    slurm_load_partitions(old_part_ptr->last_update,
					  &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(old_part_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = old_part_ptr;
		}
	} else
		error_code =
		    slurm_load_partitions((time_t) NULL, &new_part_ptr,
					  show_flags);
	if (error_code) {
		slurm_perror("slurm_load_part");
		return error_code;
	}

	old_part_ptr = new_part_ptr;
	*part_pptr = new_part_ptr;
	
	if (old_node_ptr) {
		error_code =
		    slurm_load_node(old_node_ptr->last_update,
				    &new_node_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_node_ptr = old_node_ptr;
		}
	} else
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
				show_flags);
	if (error_code) {
		slurm_perror("slurm_load_node");
		return error_code;
	}
	old_node_ptr = new_node_ptr;
	*node_pptr = new_node_ptr;

	return SLURM_SUCCESS;
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
	node_info_t *node_ptr;
	partition_info_t* part_ptr;
	ListIterator i;
	int j;
	hostlist_t hl;
	sinfo_data_t *sinfo_ptr;
	char *node_name = NULL;

	/* by default every partition is shown, even if no nodes */
	if ((!params.node_flag) && params.match_flags.partition_flag) {
		part_ptr = partition_msg->partition_array;
		for (j=0; j<partition_msg->record_count; j++, part_ptr++) {
			if ((!params.partition) || 
			    (_strcmp(params.partition, part_ptr->name) == 0))
				_create_sinfo(sinfo_list, part_ptr, 
					      (uint16_t) j, NULL);
		}
	}

	/* make sinfo_list entries for every node in every partition */
	for (j=0; j<partition_msg->record_count; j++, part_ptr++) {
		part_ptr = &(partition_msg->partition_array[j]);
		
		if (params.filtering && params.partition
		&&  _strcmp(part_ptr->name, params.partition))
			continue;
		hl = hostlist_create(part_ptr->nodes);
		while (1) {
			if (node_name)
				free(node_name);
			node_name = hostlist_shift(hl);
			if (!node_name)
				break;
			node_ptr = _find_node(node_name, node_msg);
			if (!node_ptr)
				continue;
			if (params.filtering && _filter_out(node_ptr))
				continue;
			i = list_iterator_create(sinfo_list);
			while ((sinfo_ptr = list_next(i))) {
				if (!_match_part_data(sinfo_ptr, part_ptr))
					continue;
				if (sinfo_ptr->nodes_tot
				&& (!_match_node_data(sinfo_ptr, node_ptr)))
					continue;
				_update_sinfo(sinfo_ptr, node_ptr);
				break;
			}
			/* if no match, create new sinfo_data entry */
			if (sinfo_ptr == NULL) {
				_create_sinfo(sinfo_list, part_ptr, 
					      (uint16_t) j, node_ptr);
			}
			list_iterator_destroy(i);
		}
		hostlist_destroy(hl);
	}
		
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

	if ( (params.dead_nodes) &&
	     (!(node_ptr->node_state & NODE_STATE_NO_RESPOND)) )
		return true;

	if ( (params.responding_nodes) &&
	     (node_ptr->node_state & NODE_STATE_NO_RESPOND) )
		return true;

	if (params.state_list) {
		int *node_state;
		bool match = false;
		uint16_t base_state = node_ptr->node_state & 
			NODE_STATE_BASE;
		ListIterator iterator;

		iterator = list_iterator_create(params.state_list);
		while ((node_state = list_next(iterator))) {
			if (*node_state & NODE_STATE_FLAGS) {
				if (*node_state & node_ptr->node_state) {
					match = true;
					break;
				}
			} else {
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

static bool _match_node_data(sinfo_data_t *sinfo_ptr, 
                             node_info_t *node_ptr)
{
	if (sinfo_ptr->nodes &&
	    params.match_flags.features_flag &&
	    (_strcmp(node_ptr->features, sinfo_ptr->features)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.reason_flag &&
	    (_strcmp(node_ptr->reason, sinfo_ptr->reason)))
		return false;

	if (params.match_flags.state_flag &&
	    (node_ptr->node_state != sinfo_ptr->node_state))
		return false;

	/* If no need to exactly match sizes, just return here 
	 * otherwise check cpus, disk, memory and weigth individually */
	if (!params.exact_match)
		return true;
	if (params.match_flags.cpus_flag &&
	    (node_ptr->cpus        != sinfo_ptr->min_cpus))
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

	return true;
}

static bool _match_part_data(sinfo_data_t *sinfo_ptr, 
                             partition_info_t* part_ptr)
{
	if (part_ptr == sinfo_ptr->part_info) /* identical partition */
		return true;
	if ((part_ptr == NULL) || (sinfo_ptr->part_info == NULL))
		return false;

	if (params.match_flags.avail_flag &&
	    (part_ptr->state_up != sinfo_ptr->part_info->state_up))
		return false;
			
	if (params.match_flags.groups_flag &&
	    (_strcmp(part_ptr->allow_groups, 
	             sinfo_ptr->part_info->allow_groups)))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->min_nodes != sinfo_ptr->part_info->min_nodes))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->max_nodes != sinfo_ptr->part_info->max_nodes))
		return false;

	if (params.match_flags.max_time_flag &&
	    (part_ptr->max_time != sinfo_ptr->part_info->max_time))
		return false;

	if (params.match_flags.partition_flag &&
	    (_strcmp(part_ptr->name, sinfo_ptr->part_info->name)))
		return false;

	if (params.match_flags.root_flag &&
	    (part_ptr->root_only != sinfo_ptr->part_info->root_only))
		return false;

	if (params.match_flags.share_flag &&
	    (part_ptr->shared != sinfo_ptr->part_info->shared))
		return false;

	return true;
}

static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr)
{
	uint16_t base_state;
	int node_scaling = 1;

	if(sinfo_ptr->part_info->node_scaling)
		node_scaling = sinfo_ptr->part_info->node_scaling;
	
	if (sinfo_ptr->nodes_tot == 0) {	/* first node added */
		sinfo_ptr->node_state = node_ptr->node_state;
		sinfo_ptr->features   = node_ptr->features;
		sinfo_ptr->reason     = node_ptr->reason;
		sinfo_ptr->min_cpus   = node_ptr->cpus;
		sinfo_ptr->max_cpus   = node_ptr->cpus;
		sinfo_ptr->min_disk   = node_ptr->tmp_disk;
		sinfo_ptr->max_disk   = node_ptr->tmp_disk;
		sinfo_ptr->min_mem    = node_ptr->real_memory;
		sinfo_ptr->max_mem    = node_ptr->real_memory;
		sinfo_ptr->min_weight = node_ptr->weight;
		sinfo_ptr->max_weight = node_ptr->weight;
	} else if (hostlist_find(sinfo_ptr->nodes, node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		return;
	} else {
		if (sinfo_ptr->min_cpus > node_ptr->cpus)
			sinfo_ptr->min_cpus = node_ptr->cpus;
		if (sinfo_ptr->max_cpus < node_ptr->cpus)
			sinfo_ptr->max_cpus = node_ptr->cpus;

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
	}

	base_state = node_ptr->node_state & NODE_STATE_BASE;
	if (node_ptr->node_state & NODE_STATE_DRAIN)
		sinfo_ptr->nodes_other += node_scaling;
	else if ((base_state == NODE_STATE_ALLOCATED)
	||       (node_ptr->node_state & NODE_STATE_COMPLETING))
		sinfo_ptr->nodes_alloc += node_scaling;
	else if (base_state == NODE_STATE_IDLE)
		sinfo_ptr->nodes_idle += node_scaling;
	else 
		sinfo_ptr->nodes_other += node_scaling;
	sinfo_ptr->nodes_tot += node_scaling;
	hostlist_push(sinfo_ptr->nodes, node_ptr->name);
}

/* 
 * _create_sinfo - create an sinfo record for the given node and partition
 * sinfo_list IN/OUT - table of accumulated sinfo records
 * part_ptr IN       - pointer to partition record to add
 * part_inx IN       - index of partition record (0-origin)
 * node_ptr IN       - pointer to node record to add
 */
static void _create_sinfo(List sinfo_list, partition_info_t* part_ptr, 
			  uint16_t part_inx, node_info_t *node_ptr)
{
	sinfo_data_t *sinfo_ptr;
	int node_scaling = 1;
	/* create an entry */
	sinfo_ptr = xmalloc(sizeof(sinfo_data_t));

	sinfo_ptr->part_info = part_ptr;
	if(sinfo_ptr->part_info->node_scaling) {
		node_scaling = sinfo_ptr->part_info->node_scaling;
	}
	if (node_ptr) {
		uint16_t base_state = node_ptr->node_state & 
			NODE_STATE_BASE;
		sinfo_ptr->node_state = node_ptr->node_state;
		if ((base_state == NODE_STATE_ALLOCATED)
		||  (node_ptr->node_state & NODE_STATE_COMPLETING))
			sinfo_ptr->nodes_alloc += node_scaling;
		else if (base_state == NODE_STATE_IDLE)
			sinfo_ptr->nodes_idle += node_scaling;
		else 
			sinfo_ptr->nodes_other += node_scaling;
		sinfo_ptr->nodes_tot += node_scaling;
		sinfo_ptr->min_cpus = node_ptr->cpus;
		sinfo_ptr->max_cpus = node_ptr->cpus;

		sinfo_ptr->min_disk = node_ptr->tmp_disk;
		sinfo_ptr->max_disk = node_ptr->tmp_disk;

		sinfo_ptr->min_mem = node_ptr->real_memory;
		sinfo_ptr->max_mem = node_ptr->real_memory;

		sinfo_ptr->min_weight = node_ptr->weight;
		sinfo_ptr->max_weight = node_ptr->weight;

		sinfo_ptr->features = node_ptr->features;
		sinfo_ptr->reason   = node_ptr->reason;

		sinfo_ptr->nodes = hostlist_create(node_ptr->name);
		sinfo_ptr->part_inx = part_inx;
	} else {
		sinfo_ptr->nodes = hostlist_create("");
		sinfo_ptr->part_inx = part_inx;
	}

	list_append(sinfo_list, sinfo_ptr);
}

/* 
 * _find_node - find a node by name
 * node_name IN     - name of node to locate
 * node_msg IN      - node information message from API
 */
static node_info_t *_find_node(char *node_name, node_info_msg_t *node_msg)
{
	int i;
	if (node_name == NULL)
		return NULL;

	for (i=0; i<node_msg->record_count; i++) {
		if (_strcmp(node_name, node_msg->node_array[i].name))
			continue;
		return &(node_msg->node_array[i]);
	}

	/* not found */
	return NULL;
}

static void _sinfo_list_delete(void *data)
{
	sinfo_data_t *sinfo_ptr = data;

	hostlist_destroy(sinfo_ptr->nodes);
	xfree(sinfo_ptr);
}

/* like strcmp, but works with NULL pointers */
static int _strcmp(char *data1, char *data2) 
{
	static char null_str[] = "(null)";

	if (data1 == NULL)
		data1 = null_str;
	if (data2 == NULL)
		data2 = null_str;
	return strcmp(data1, data2);
}

