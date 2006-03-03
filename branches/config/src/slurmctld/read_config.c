/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_rlimits_info.h"

#define BUFFER_SIZE	1024

static int  _build_bitmaps(void);
static int  _init_all_slurm_conf(void);
static int  _parse_node_spec(char *in_line);
static int  _parse_part_spec(char *in_line);
static void _purge_old_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count);
static void _restore_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count);
static int  _preserve_plugins(slurm_ctl_conf_t * ctl_conf_ptr, 
				char *old_auth_type, char *old_checkpoint_type,
				char *old_sched_type, char *old_select_type,
				char *old_switch_type);
static int  _sync_nodes_to_comp_job(void);
static int  _sync_nodes_to_jobs(void);
static int  _sync_nodes_to_active_job(struct job_record *job_ptr);
#ifdef 	HAVE_ELAN
static void _validate_node_proc_count(void);
#endif

static char highest_node_name[MAX_NAME_LEN] = "";
int node_record_count = 0;

/* FIXME - declarations for temporarily moved functions */
#define MULTIPLE_VALUE_MSG "Multiple values for %s, latest one used"


/*
 * _build_bitmaps - build node bitmaps to define which nodes are in which 
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * RET 0 if no error, errno otherwise
 * Note: Operates on common variables, no arguments
 * global: idle_node_bitmap, avail_node_bitmap, share_node_bitmap 
 *	node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	part_list - pointer to global partition list
 */
static int _build_bitmaps(void)
{
	int i, j, error_code = SLURM_SUCCESS;
	char *this_node_name;
	ListIterator config_iterator;
	ListIterator part_iterator;
	struct config_record *config_ptr;
	struct part_record   *part_ptr;
	struct node_record   *node_ptr;
	struct job_record    *job_ptr;
	ListIterator job_iterator;
	hostlist_t host_list;

	last_node_update = time(NULL);
	last_part_update = time(NULL);

	/* initialize the idle and up bitmaps */
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	idle_node_bitmap  = (bitstr_t *) bit_alloc(node_record_count);
	avail_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	share_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	if ((idle_node_bitmap     == NULL) ||
	    (avail_node_bitmap    == NULL) ||
	    (share_node_bitmap    == NULL)) 
		fatal ("bit_alloc malloc failure");

	/* initialize the configuration bitmaps */
	config_iterator = list_iterator_create(config_list);
	if (config_iterator == NULL)
		fatal ("memory allocation failure");

	while ((config_ptr = (struct config_record *)
				      list_next(config_iterator))) {
		FREE_NULL_BITMAP(config_ptr->node_bitmap);
		config_ptr->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
		if (config_ptr->node_bitmap == NULL)
			fatal ("bit_alloc malloc failure");
	}
	list_iterator_destroy(config_iterator);

	/* Set all bits, all nodes initially available for sharing */
	bit_nset(share_node_bitmap, 0, (node_record_count-1));

	/* identify all nodes non-sharable due to non-sharing jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bitstr_t *tmp_bits;
		if ((job_ptr->job_state   != JOB_RUNNING) ||
		    (job_ptr->node_bitmap == NULL)        ||
		    (job_ptr->details     == NULL)        ||
		    (job_ptr->details->shared != 0))
			continue;
		tmp_bits = bit_copy(job_ptr->node_bitmap);
		if (tmp_bits == NULL)
			fatal ("bit_copy malloc failure");
		bit_not(tmp_bits);
		bit_and(share_node_bitmap, tmp_bits);
		bit_free(tmp_bits);
	}
	list_iterator_destroy(job_iterator);

	/* scan all nodes and identify which are up, idle and 
	 * their configuration, resync DRAINED vs. DRAINING state */
	for (i = 0; i < node_record_count; i++) {
		uint16_t base_state, no_resp_flag, job_cnt;

		if (node_record_table_ptr[i].name[0] == '\0')
			continue;	/* defunct */
		base_state = node_record_table_ptr[i].node_state & 
				NODE_STATE_BASE; 
		no_resp_flag = node_record_table_ptr[i].node_state & 
				NODE_STATE_NO_RESPOND;
		job_cnt = node_record_table_ptr[i].run_job_cnt +
		          node_record_table_ptr[i].comp_job_cnt;

		if (((base_state == NODE_STATE_IDLE) && (job_cnt == 0))
		||  (base_state == NODE_STATE_DOWN))
			bit_set(idle_node_bitmap, i);
		if ((base_state == NODE_STATE_IDLE)
		||  (base_state == NODE_STATE_ALLOCATED)
		&&  (no_resp_flag == 0))
			bit_set(avail_node_bitmap, i);
		if (node_record_table_ptr[i].config_ptr)
			bit_set(node_record_table_ptr[i].config_ptr->
				node_bitmap, i);
	}

	/* scan partition table and identify nodes in each */
	part_iterator = list_iterator_create(part_list);
	if (part_iterator == NULL)
		fatal ("memory allocation failure");

	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		FREE_NULL_BITMAP(part_ptr->node_bitmap);
		part_ptr->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
		if (part_ptr->node_bitmap == NULL)
			fatal ("bit_alloc malloc failure");

		/* check for each node in the partition */
		if ((part_ptr->nodes == NULL) || (part_ptr->nodes[0] == '\0'))
			continue;

		if ((host_list = hostlist_create(part_ptr->nodes)) == NULL) {
			fatal("hostlist_create error for %s, %m",
			      part_ptr->nodes);
			continue;
		}

		while ((this_node_name = hostlist_shift(host_list))) {
			node_ptr = find_node_record(this_node_name);
			if (node_ptr == NULL) {
				fatal("_build_bitmaps: node %s is referenced "
					"but not defined in slurm.conf "
					"(no NodeName specification)", 
					this_node_name);
				free(this_node_name);
				continue;
			}
			j = node_ptr - node_record_table_ptr;
			bit_set(part_ptr->node_bitmap, j);
			part_ptr->total_nodes++;
			if (slurmctld_conf.fast_schedule)
				part_ptr->total_cpus += 
					node_ptr->config_ptr->cpus;
			else
				part_ptr->total_cpus += node_ptr->cpus;
			node_ptr->part_cnt++;
			xrealloc(node_ptr->part_pptr, (node_ptr->part_cnt *
				sizeof(struct part_record *)));
			node_ptr->part_pptr[node_ptr->part_cnt-1] = part_ptr;
			free(this_node_name);
		}
		hostlist_destroy(host_list);
	}
	list_iterator_destroy(part_iterator);
	return error_code;
}


/* 
 * _init_all_slurm_conf - initialize or re-initialize the slurm 
 *	configuration values.  
 * RET 0 if no error, otherwise an error code.
 * NOTE: We leave the job table intact
 * NOTE: Operates on common variables, no arguments
 */
static int _init_all_slurm_conf(void)
{
	int error_code;

	init_slurm_conf(&slurmctld_conf);

	if ((error_code = init_node_conf()))
		return error_code;

	if ((error_code = init_part_conf()))
		return error_code;

	if ((error_code = init_job_conf()))
		return error_code;

	strcpy(highest_node_name, "");
	return 0;
}

static int _state_str2int(const char *state_str)
{
	int state_val = NO_VAL;
	int i;

	for (i = 0; i <= NODE_STATE_END; i++) {
		if (strcasecmp(node_state_string(i), "END") == 0)
			break;
		if (strcasecmp(node_state_string(i), state_str) == 0) {
			state_val = i;
			break;
		}
	}
	if ((i == 0) && (strncasecmp("DRAIN", state_str, 5) == 0))
		state_val = NODE_STATE_IDLE | NODE_STATE_DRAIN;
	if (state_val == NO_VAL) {
		error("invalid state %s", state_str);
		errno = EINVAL;
	}
	return state_val;
}

/* Caller must be holding slurm_conf_lock() */
static void _set_node_prefix(const char *nodenames, slurm_ctl_conf_t *conf)
{
	int i;
	char *tmp;

	for (i = 1; nodenames[i] != '\0'; i++) {
		if((nodenames[i-1] == '[') 
		   || (nodenames[i-1] <= '9'
		       && nodenames[i-1] >= '0'))
			break;
	}
	xfree(conf->node_prefix);
	if(nodenames[i] == '\0')
		conf->node_prefix = xstrdup(nodenames);
	else {
		tmp = xmalloc(sizeof(char)*i+1);
		memset(tmp, 0, i+1);
		snprintf(tmp, i, "%s", nodenames);
		conf->node_prefix = tmp;
		tmp = NULL;
	}
	debug3("Prefix is %s %s %d", conf->node_prefix, nodenames, i);
}

/* 
 * _build_single_nodeline_info - rom the slurm.conf reader, build table,
 * 	and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: default_config_record - default configuration values for
 *	                           group of nodes
 *	default_node_record - default node configuration values
 */
static int _build_single_nodeline_info(slurm_conf_node_t *node_ptr,
				       struct config_record *config_ptr,
				       slurm_ctl_conf_t *conf)
{
	int error_code, i;
	int state_val;
	struct node_record *node_rec = NULL;
	hostlist_t alias_list = NULL;
	hostlist_t hostname_list = NULL;
	hostlist_t address_list = NULL;
	char *alias = NULL;
	char *hostname = NULL;
	char *address = NULL;

	if (node_ptr->state != NULL) {
		state_val = _state_str2int(node_ptr->state);
		if (state_val == NO_VAL)
			goto cleanup;
	}

	if ((alias_list = hostlist_create(node_ptr->nodenames)) == NULL) {
		error("Unable to create NodeName list from %s",
		      node_ptr->nodenames);
		error_code = errno;
		goto cleanup;
	}
	if ((hostname_list = hostlist_create(node_ptr->hostnames)) == NULL) {
		error("Unable to create NodeHostname list from %s",
		      node_ptr->hostnames);
		error_code = errno;
		goto cleanup;
	}
	if ((address_list = hostlist_create(node_ptr->addresses)) == NULL) {
		error("Unable to create NodeAddr list from %s",
		      node_ptr->addresses);
		error_code = errno;
		goto cleanup;
	}

	_set_node_prefix(node_ptr->nodenames, conf);

	/* some sanity checks */
#ifdef HAVE_FRONT_END
	if (hostlist_count(hostname_list) != 1
	    || hostlist_count(address_list) != 1) {
		error("Only one hostname and address allowed "
		      "in FRONT_END mode");
		goto cleanup;
	}
	hostname = node_ptr->hostnames;
	address = node_ptr->addresses;
#else
	if (hostlist_count(hostname_list) < hostlist_count(alias_list)) {
		error("At least as many NodeHostname are required "
		      "as NodeName");
		goto cleanup;
	}
	if (hostlist_count(address_list) < hostlist_count(alias_list)) {
		error("At least as many NodeAddr are required as NodeName");
		goto cleanup;
	}
#endif

	/* now build the individual node structures */
	while ((alias = hostlist_shift(alias_list))) {
#ifndef HAVE_FRONT_END
		hostname = hostlist_shift(hostname_list);
		address = hostlist_shift(address_list);
#endif		

		if (strcmp(alias, highest_node_name) <= 0)
			node_rec = find_node_record(alias);
		else {
			strncpy(highest_node_name, alias, MAX_NAME_LEN);
			node_rec = NULL;
		}

		if (node_rec == NULL) {
			node_rec = create_node_record(config_ptr, alias);
			if ((state_val != NO_VAL) &&
			    (state_val != NODE_STATE_UNKNOWN))
				node_rec->node_state = state_val;
			node_rec->last_response = (time_t) 0;
			strncpy(node_rec->comm_name, address, MAX_NAME_LEN);

			node_rec->port = node_ptr->port;
			node_rec->reason = xstrdup(node_ptr->reason);
		} else {
			error("reconfiguration for node %s", alias);
			if ((state_val != NO_VAL) &&
			    (state_val != NODE_STATE_UNKNOWN))
				node_rec->node_state = state_val;
			if (node_ptr->reason) {
				xfree(node_rec->reason);
				node_rec->reason = xstrdup(node_ptr->reason);
			}
		}
		free(alias);
#ifndef HAVE_FRONT_END
		free(hostname);
		free(address);
#endif
	}

	/* free allocated storage */
cleanup:
	if (alias_list)
		hostlist_destroy(alias_list);
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (address_list)
		hostlist_destroy(address_list);
	return error_code;

}

/* 
 * _build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: default_config_record - default configuration values for
 *	                           group of nodes
 *	default_node_record - default node configuration values
 */
static int _build_all_nodeline_info(slurm_ctl_conf_t *conf)
{
	slurm_conf_node_t *node, **ptr_array;
	struct config_record *config_ptr = NULL;
	int count;
	int i;

	count = slurm_conf_nodename_array(&ptr_array);
	if (count == 0)
		fatal("No NodeName information available!");

	for (i = 0; i < count; i++) {
		node = ptr_array[i];

		config_ptr = create_config_record();
		config_ptr->nodes = xstrdup(node->nodenames);
		config_ptr->cpus = node->cpus;
		config_ptr->real_memory = node->real_memory;
		config_ptr->tmp_disk = node->tmp_disk;
		config_ptr->weight = node->weight;
		if (node->feature) {
			xfree(config_ptr->feature);
			config_ptr->feature = xstrdup(node->feature);
		}

		_build_single_nodeline_info(node, config_ptr, conf);
	}
}


/*
 * _parse_part_spec - parse the partition specification, build table and 
 *	set values
 * IN/OUT in_line - line from the configuration file, parsed keywords 
 *	and values replaced by blanks
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
static int _parse_part_spec(char *in_line)
{
	char *allow_groups = NULL, *nodes = NULL, *partition_name = NULL;
	char *max_time_str = NULL, *default_str = NULL, *root_str = NULL;
	char *shared_str = NULL, *state_str = NULL, *hidden_str = NULL;
	int max_time_val = NO_VAL, max_nodes_val = NO_VAL;
	int min_nodes_val = NO_VAL, root_val = NO_VAL, default_val = NO_VAL;
	int hidden_val = NO_VAL, state_val = NO_VAL, shared_val = NO_VAL;
	int error_code;
	struct part_record *part_ptr;
	static int default_part_val = NO_VAL;

	if ((error_code =
	     load_string(&partition_name, "PartitionName=", in_line)))
		return error_code;
	if (partition_name == NULL)
		return 0;	/* no partition info */

	if (strlen(partition_name) >= MAX_NAME_LEN) {
		error("_parse_part_spec: partition name %s too long",
		      partition_name);
		xfree(partition_name);
		return EINVAL;
	}

	allow_groups = default_str = root_str = nodes = NULL;
	shared_str = state_str = NULL;
	error_code = slurm_parser(in_line,
				  "AllowGroups=", 's', &allow_groups,
				  "Default=", 's', &default_str,
				  "Hidden=", 's', &hidden_str,
				  "RootOnly=", 's', &root_str,
				  "MaxTime=", 's', &max_time_str,
				  "MaxNodes=", 'd', &max_nodes_val,
				  "MinNodes=", 'd', &min_nodes_val,
				  "Nodes=", 's', &nodes,
				  "Shared=", 's', &shared_str,
				  "State=", 's', &state_str, "END");

	if (error_code)
		goto cleanup;

	if (default_str) {
		if (strcasecmp(default_str, "YES") == 0)
			default_val = 1;
		else if (strcasecmp(default_str, "NO") == 0)
			default_val = 0;
		else {
			error("_parse_part_spec: ignored partition %s update, "
				"bad state %s", partition_name, default_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(default_str);
	} else
		default_val = default_part_val;

	if (hidden_str) {
		if (strcasecmp(hidden_str, "YES") == 0)
			hidden_val = 1;
		else if (strcasecmp(hidden_str, "NO") == 0)
			hidden_val = 0;
		else {
			error("_parse_part_spec: ignored partition %s update, "
				"bad key %s", partition_name, hidden_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(hidden_str);
	}

	if (root_str) {
		if (strcasecmp(root_str, "YES") == 0)
			root_val = 1;
		else if (strcasecmp(root_str, "NO") == 0)
			root_val = 0;
		else {
			error("_parse_part_spec ignored partition %s update, "
				"bad key %s", partition_name, root_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(root_str);
	}

	if (max_time_str) {
		if (strcasecmp(max_time_str, "INFINITE") == 0)
			max_time_val = INFINITE;
		else {
			char *end_ptr;
			max_time_val = strtol(max_time_str, &end_ptr, 10);
			if ((max_time_str[0] != '\0') && 
			    (end_ptr[0] != '\0')) {
				error_code = EINVAL;
				goto cleanup;
			}
		}
		xfree(max_time_str);
	}

	if (shared_str) {
		if (strcasecmp(shared_str, "YES") == 0)
			shared_val = SHARED_YES;
		else if (strcasecmp(shared_str, "NO") == 0)
			shared_val = SHARED_NO;
		else if (strcasecmp(shared_str, "FORCE") == 0)
			shared_val = SHARED_FORCE;
		else {
			error("_parse_part_spec ignored partition %s update, "
				"bad shared %s", partition_name, shared_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(shared_str);
	}

	if (state_str) {
		if (strcasecmp(state_str, "UP") == 0)
			state_val = 1;
		else if (strcasecmp(state_str, "DOWN") == 0)
			state_val = 0;
		else {
			error("_parse_part_spec ignored partition %s update, "
				"bad state %s", partition_name, state_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(state_str);
	}

	if (strcasecmp(partition_name, "DEFAULT") == 0) {
		xfree(partition_name);
		if (default_val != NO_VAL)
			default_part_val = default_val;
		if (hidden_val != NO_VAL)
			default_part.hidden  = hidden_val;
		if (max_time_val != NO_VAL)
			default_part.max_time  = max_time_val;
		if (max_nodes_val != NO_VAL)
			default_part.max_nodes = max_nodes_val;
		if (min_nodes_val != NO_VAL)
			default_part.min_nodes = min_nodes_val;
		if (root_val != NO_VAL)
			default_part.root_only = root_val;
		if (state_val != NO_VAL)
			default_part.state_up  = state_val;
		if (shared_val != NO_VAL)
			default_part.shared    = shared_val;
		if (allow_groups) {
			xfree(default_part.allow_groups);
			if (strcasecmp(allow_groups, "ALL")) {
				default_part.allow_groups = allow_groups;
				allow_groups = NULL;
			}
		}
		if (nodes) {
			xfree(default_part.nodes);
			default_part.nodes = nodes;
			nodes = NULL;
		}
		return 0;
	}

	part_ptr = list_find_first(part_list, &list_find_part, partition_name);
	if (part_ptr == NULL) {
		part_ptr = create_part_record();
		strcpy(part_ptr->name, partition_name);
	} else {
		verbose("_parse_node_spec: duplicate entry for partition %s",
		     partition_name);
	}
	if (default_val == 1) {
		if ((strlen(default_part_name) > 0)
		&&  strcmp(default_part_name,partition_name))
			info("_parse_part_spec: changing default partition "
				"from %s to %s", 
				default_part_name, partition_name);
		strcpy(default_part_name, partition_name);
		default_part_loc = part_ptr;
	}
	if (hidden_val != NO_VAL)
		part_ptr->hidden  = hidden_val;
	if (max_time_val != NO_VAL)
		part_ptr->max_time  = max_time_val;
	if (max_nodes_val != NO_VAL)
		part_ptr->max_nodes = max_nodes_val;
	if (min_nodes_val != NO_VAL)
		part_ptr->min_nodes = min_nodes_val;
	if (root_val != NO_VAL)
		part_ptr->root_only = root_val;
	if (state_val != NO_VAL)
		part_ptr->state_up  = state_val;
	if (shared_val != NO_VAL)
		part_ptr->shared    = shared_val;
	if (allow_groups) {
		xfree(part_ptr->allow_groups);
		part_ptr->allow_groups = allow_groups;
		allow_groups = NULL;
	}
	if (nodes) {
		if (strcasecmp(nodes, "localhost") == 0) {
			xfree(nodes);
			nodes = xmalloc(MAX_NAME_LEN);
			getnodename(nodes, MAX_NAME_LEN);
		}
		if (part_ptr->nodes) {
			xstrcat(part_ptr->nodes, ",");
			xstrcat(part_ptr->nodes, nodes);
			xfree(nodes);
		} else {
			part_ptr->nodes = nodes;
			nodes = NULL;
		}
	}
	xfree(partition_name);
	return 0;

      cleanup:
	xfree(allow_groups);
	xfree(default_str);
	xfree(hidden_str);
	xfree(max_time_str);
	xfree(root_str);
	xfree(nodes);
	xfree(partition_name);
	xfree(shared_str);
	xfree(state_str);
	return error_code;
}


/*
 * read_slurm_conf - load the slurm configuration from the configured file. 
 * read_slurm_conf can be called more than once if so desired.
 * IN recover - replace job, node and/or partition data with last saved 
 *              state information depending upon value
 *              0 = use no saved state information
 *              1 = recover saved job state, 
 *                  node DOWN/DRAIN state and reason information
 *              2 = recover all state saved from last slurmctld shutdown
 * RET 0 if no error, otherwise an error code
 * Note: Operates on common variables only
 */
int read_slurm_conf(int recover)
{
	DEF_TIMERS;
	int i, j, error_code;
	int old_node_record_count;
	struct node_record *old_node_table_ptr;
	char *old_auth_type       = xstrdup(slurmctld_conf.authtype);
	char *old_checkpoint_type = xstrdup(slurmctld_conf.checkpoint_type);
	char *old_sched_type      = xstrdup(slurmctld_conf.schedtype);
	char *old_select_type     = xstrdup(slurmctld_conf.select_type);
	char *old_switch_type     = xstrdup(slurmctld_conf.switch_type);
	slurm_ctl_conf_t *conf;

	/* initialization */
	START_TIMER;
	old_node_record_count = node_record_count;
	old_node_table_ptr = 
		node_record_table_ptr;  /* save node states for reconfig RPC */
	node_record_table_ptr = NULL;
	if ((error_code = _init_all_slurm_conf())) {
		node_record_table_ptr = old_node_table_ptr;
		return error_code;
	}

	slurm_conf_init(NULL);
	conf = slurm_conf_lock();
	_build_all_nodeline_info(conf);
	/* _build_all_partition_info(conf); */
	slurm_conf_unlock();

	update_logging();
	g_slurmctld_jobacct_init(slurmctld_conf.job_acct_loc,
			slurmctld_conf.job_acct_parameters);
	g_slurm_jobcomp_init(slurmctld_conf.job_comp_loc);
	slurm_sched_init();
	switch_init();

	if (default_part_loc == NULL)
		error("read_slurm_conf: default partition not set.");

	if (node_record_count < 1) {
		error("read_slurm_conf: no nodes configured.");
		xfree(old_node_table_ptr);
		return EINVAL;
	}

	rehash_node();
	rehash_jobs();
	set_slurmd_addr();

	if (recover > 1) {	/* Load node, part and job info */
		(void) load_all_node_state(false);
		(void) load_all_part_state();
		(void) load_all_job_state();
	} else if (recover == 1) {	/* Load job info only */
		(void) load_all_node_state(true);
		(void) load_all_job_state();
	} else {	/* Load no info, preserve all state */
		if (old_node_table_ptr) {
			debug("restoring original state of nodes");
			_restore_node_state(old_node_table_ptr, 
					    old_node_record_count);
		}
		reset_first_job_id();
	}
	reset_job_bitmaps();
	(void) _sync_nodes_to_jobs();
	(void) sync_job_files();
	_purge_old_node_state(old_node_table_ptr, old_node_record_count);

	if ((error_code = _build_bitmaps()))
		return error_code;
#ifdef 	HAVE_ELAN
	_validate_node_proc_count();
#endif
	if ((select_g_node_init(node_record_table_ptr, node_record_count)
			!= SLURM_SUCCESS) 
	|| (select_g_block_init(part_list) != SLURM_SUCCESS) 
	|| (select_g_job_init(job_list) != SLURM_SUCCESS)) {
		error("failed to initialize node selection plugin state");
		abort();
	}
	(void) _sync_nodes_to_comp_job(); /* must follow select_g_node_init() */
	load_part_uid_allow_list(1);

	/* sort config_list by weight for scheduling */
	list_sort(config_list, &list_compare_config);

	/* Update plugins as possible */
	error_code = _preserve_plugins(&slurmctld_conf,
			old_auth_type, old_checkpoint_type,
			old_sched_type, old_select_type,
			old_switch_type);

	slurmctld_conf.last_update = time(NULL);
	END_TIMER;
	debug("read_slurm_conf: finished loading configuration %s",
	     TIME_STR);

	return error_code;
}


/* Restore node state and size information from saved records */
static void _restore_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count)
{
	struct node_record *node_ptr;
	int i;

	for (i = 0; i < old_node_record_count; i++) {
		node_ptr  = find_node_record(old_node_table_ptr[i].name);
		if (node_ptr == NULL)
			continue;
		node_ptr->node_state    = old_node_table_ptr[i].node_state;
		node_ptr->last_response = old_node_table_ptr[i].last_response;
		node_ptr->cpus          = old_node_table_ptr[i].cpus;
		node_ptr->real_memory   = old_node_table_ptr[i].real_memory;
		node_ptr->tmp_disk      = old_node_table_ptr[i].tmp_disk;
		if(node_ptr->reason == NULL) {
			/* Recover only if not explicitly set in slurm.conf */
			node_ptr->reason	= old_node_table_ptr[i].reason;
			old_node_table_ptr[i].reason = NULL;
		}
	}
}

/* Purge old node state information */
static void _purge_old_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count)
{
	int i;

	for (i = 0; i < old_node_record_count; i++) {
		xfree(old_node_table_ptr[i].part_pptr);
		xfree(old_node_table_ptr[i].reason);
	}
	xfree(old_node_table_ptr);
}

/*
 * _preserve_plugins - preserve original plugin values over reconfiguration 
 *	as required. daemons and/or commands must be restarted for some 
 *	plugin value changes to take effect.
 * RET zero or error code
 */
static int  _preserve_plugins(slurm_ctl_conf_t * ctl_conf_ptr, 
		char *old_auth_type, char *old_checkpoint_type,
		char *old_sched_type, char *old_select_type, 
		char *old_switch_type)
{
	int rc = SLURM_SUCCESS;

	if (old_auth_type) {
		if (strcmp(old_auth_type, ctl_conf_ptr->authtype)) {
			xfree(ctl_conf_ptr->authtype);
			ctl_conf_ptr->authtype = old_auth_type;
			rc =  ESLURM_INVALID_AUTHTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_auth_type);
	}

	if (old_checkpoint_type) {
		if (strcmp(old_checkpoint_type, 
				ctl_conf_ptr->checkpoint_type)) {
			xfree(ctl_conf_ptr->checkpoint_type);
			ctl_conf_ptr->checkpoint_type = old_checkpoint_type;
			rc =  ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE;
		} else  /* free duplicate value */
			xfree(old_checkpoint_type);
	}

	if (old_sched_type) {
		if (strcmp(old_sched_type, ctl_conf_ptr->schedtype)) {
			xfree(ctl_conf_ptr->schedtype);
			ctl_conf_ptr->schedtype = old_sched_type;
			rc =  ESLURM_INVALID_SCHEDTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_sched_type);
	}


	if (old_select_type) {
		if (strcmp(old_select_type, ctl_conf_ptr->select_type)) {
			xfree(ctl_conf_ptr->select_type);
			ctl_conf_ptr->select_type = old_select_type;
			rc =  ESLURM_INVALID_SELECTTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_select_type);
	}

	if (old_switch_type) {
		if (strcmp(old_switch_type, ctl_conf_ptr->switch_type)) {
			xfree(ctl_conf_ptr->switch_type);
			ctl_conf_ptr->switch_type = old_switch_type;
			rc = ESLURM_INVALID_SWITCHTYPE_CHANGE;
		} else	/* free duplicate value */
			xfree(old_switch_type);
	}

	if (ctl_conf_ptr->backup_controller == NULL)
		info("read_slurm_conf: backup_controller not specified.");

	return rc;
}


/*
 * _sync_nodes_to_jobs - sync node state to job states on slurmctld restart.
 *	This routine marks nodes allocated to a job as busy no matter what 
 *	the node's last saved state 
 * RET count of nodes having state changed
 * Note: Operates on common variables, no arguments
 */
static int _sync_nodes_to_jobs(void)
{
	struct job_record *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->node_bitmap == NULL)
			continue;

		if ((job_ptr->job_state == JOB_RUNNING) ||
		    (job_ptr->job_state &  JOB_COMPLETING))
			update_cnt += _sync_nodes_to_active_job(job_ptr);
	}
	list_iterator_destroy(job_iterator);

	if (update_cnt)
		info("_sync_nodes_to_jobs updated state of %d nodes",
		     update_cnt);
	return update_cnt;
}

/* For jobs which are in state COMPLETING, deallocate the nodes and 
 * issue the RPC to kill the job */
static int _sync_nodes_to_comp_job(void)
{
	struct job_record *job_ptr;
	ListIterator job_iterator;
	int update_cnt = 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->node_bitmap) &&
		    (job_ptr->job_state & JOB_COMPLETING)) {
			update_cnt++;
			info("Killing job_id %u", job_ptr->job_id);
			deallocate_nodes(job_ptr, false, false);
		}
	}
	if (update_cnt)
		info("_sync_nodes_to_comp_job completing %d jobs",
			update_cnt);
	return update_cnt;
}

/* Synchronize states of nodes and active jobs (RUNNING or COMPLETING state)
 * RET count of jobs with state changes */
static int _sync_nodes_to_active_job(struct job_record *job_ptr)
{
	int i, cnt = 0;
	uint16_t base_state, node_flags;
	struct node_record *node_ptr = node_record_table_ptr;

	job_ptr->node_cnt = 0;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		job_ptr->node_cnt++;

		base_state = node_ptr->node_state & NODE_STATE_BASE;
		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
 
		node_ptr->run_job_cnt++; /* NOTE:
				* This counter moved to comp_job_cnt 
				* by _sync_nodes_to_comp_job() */
		if (((job_ptr->job_state == JOB_RUNNING) ||
		     (job_ptr->job_state &  JOB_COMPLETING)) &&
		    (job_ptr->details) && (job_ptr->details->shared == 0))
			node_ptr->no_share_job_cnt++;

		if (base_state == NODE_STATE_DOWN) {
			time_t now = time(NULL);
			job_ptr->job_state = JOB_NODE_FAIL | JOB_COMPLETING;
			job_ptr->end_time = MIN(job_ptr->end_time, now);
			delete_all_step_records(job_ptr);
			job_completion_logger(job_ptr);
			cnt++;
		} else if ((base_state == NODE_STATE_UNKNOWN) || 
			   (base_state == NODE_STATE_IDLE)) {
			cnt++;
			node_ptr->node_state =
				NODE_STATE_ALLOCATED | node_flags;
		} 
	}
	return cnt;
}

#ifdef 	HAVE_ELAN
/* Every node in a given partition must have the same processor count 
 * at present, this function insure it */
static void _validate_node_proc_count(void)
{
	ListIterator part_iterator;
	struct part_record *part_ptr;
	struct node_record *node_ptr;
	int first_bit, last_bit, i, node_size, part_size;

	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		first_bit = bit_ffs(part_ptr->node_bitmap);
		last_bit = bit_fls(part_ptr->node_bitmap);
		part_size = -1;
		for (i = first_bit; i <= last_bit; i++) {
			if (bit_test(part_ptr->node_bitmap, i) == 0)
				continue;
			node_ptr = node_record_table_ptr + i;

			if (slurmctld_conf.fast_schedule)
				node_size = node_ptr->config_ptr->cpus;
			else if (node_ptr->cpus < node_ptr->config_ptr->cpus)
				continue;	/* node too small, will be DOWN */
			else if ((node_ptr->node_state & NODE_STATE_BASE) 
					== NODE_STATE_DOWN)
				continue;
			else
				node_size = node_ptr->cpus;

			if (part_size == -1)
				part_size = node_size;
			else if (part_size != node_size)
				fatal("Partition %s has inconsistent "
					"processor count", part_ptr->name);
		}
	}
	list_iterator_destroy(part_iterator);
}
#endif

/* Normalize supplied debug level to be in range per log.h definitions */
static void _normalize_debug_level(uint16_t *level)
{
	if (*level > LOG_LEVEL_DEBUG3) {
		error("Normalizing debug level from %u to %d", 
		      *level, LOG_LEVEL_DEBUG3);
		*level = LOG_LEVEL_DEBUG3;
	}
	/* level is uint16, always > LOG_LEVEL_QUIET(0), can't underflow */
}

