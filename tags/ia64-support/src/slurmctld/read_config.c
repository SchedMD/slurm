/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
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
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/slurmctld/locks.h"
#include "src/common/macros.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#define BUF_SIZE 1024

static int  _build_bitmaps(void);
static int  _init_all_slurm_conf(void);
static int  _parse_node_spec(char *in_line);
static int  _parse_part_spec(char *in_line);
static void _restore_node_state(struct node_record *old_node_table_ptr, 
				int old_node_record_count);
static void _set_config_defaults(slurm_ctl_conf_t * ctl_conf_ptr);
static int  _sync_nodes_to_comp_job(void);
static int  _sync_nodes_to_jobs(void);
static int  _sync_nodes_to_active_job(struct job_record *job_ptr);
#ifdef 	HAVE_LIBELAN3
static void _validate_node_proc_count(void);
#endif

static char highest_node_name[MAX_NAME_LEN] = "";
int node_record_count = 0;


/*
 * _build_bitmaps - build node bitmaps to define which nodes are in which 
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * RET 0 if no error, errno otherwise
 * Note: Operates on common variables, no arguments
 * global: idle_node_bitmap - bitmap record of idle nodes
 *	up_node_bitmap - bitmap records of up nodes
 *	node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	part_list - pointer to global partition list
 */
static int _build_bitmaps(void)
{
	int i, j, error_code;
	char *this_node_name;
	ListIterator config_record_iterator;
	ListIterator part_record_iterator;
	struct config_record *config_record_point;
	struct part_record *part_record_point;
	struct node_record *node_record_point;
	bitstr_t *all_part_node_bitmap;
	hostlist_t host_list;

	error_code = 0;
	last_node_update = time(NULL);
	last_part_update = time(NULL);

	/* initialize the idle and up bitmaps */
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	idle_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	up_node_bitmap   = (bitstr_t *) bit_alloc(node_record_count);
	if ((idle_node_bitmap == NULL) ||
	    (up_node_bitmap   == NULL)) 
		fatal ("memory allocation failure");
	/* initialize the configuration bitmaps */
	config_record_iterator = list_iterator_create(config_list);
	if (config_record_iterator == NULL)
		fatal ("memory allocation failure");

	while ((config_record_point = (struct config_record *)
				      list_next(config_record_iterator))) {
		FREE_NULL_BITMAP(config_record_point->node_bitmap);
		config_record_point->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
		if (config_record_point->node_bitmap == NULL)
			fatal ("memory allocation failure");
	}
	list_iterator_destroy(config_record_iterator);

	/* scan all nodes and identify which are up, idle and 
	 * their configuration */
	for (i = 0; i < node_record_count; i++) {
		uint16_t base_state, no_resp_flag;

		if (node_record_table_ptr[i].name[0] == '\0')
			continue;	/* defunct */
		base_state   = node_record_table_ptr[i].node_state & 
			       (~NODE_STATE_NO_RESPOND);
		no_resp_flag = node_record_table_ptr[i].node_state & 
			       NODE_STATE_NO_RESPOND;
		if (base_state == NODE_STATE_IDLE)
			bit_set(idle_node_bitmap, i);
		if ((base_state != NODE_STATE_DOWN)    &&
		    (base_state != NODE_STATE_UNKNOWN) &&
		    (base_state != NODE_STATE_DRAINED) &&
		    (no_resp_flag == 0))
			bit_set(up_node_bitmap, i);
		if (node_record_table_ptr[i].config_ptr)
			bit_set(node_record_table_ptr[i].config_ptr->
				node_bitmap, i);
	}

	/* scan partition table and identify nodes in each */
	all_part_node_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	if (all_part_node_bitmap == NULL)
		fatal ("memory allocation failure");
	part_record_iterator = list_iterator_create(part_list);
	if (part_record_iterator == NULL)
		fatal ("memory allocation failure");

	while ((part_record_point =
		(struct part_record *) list_next(part_record_iterator))) {
		FREE_NULL_BITMAP(part_record_point->node_bitmap);
		part_record_point->node_bitmap =
		    (bitstr_t *) bit_alloc(node_record_count);
		if (part_record_point->node_bitmap == NULL)
			fatal ("memory allocation failure");

		/* check for each node in the partition */
		if ((part_record_point->nodes == NULL) ||
		    (part_record_point->nodes[0] == '\0'))
			continue;

		if ((host_list =
		     hostlist_create(part_record_point->nodes)) == NULL) {
			error("hostlist_create error for %s, %m",
			      part_record_point->nodes);
			continue;
		}

		while ((this_node_name = hostlist_shift(host_list))) {
			node_record_point =
			    find_node_record(this_node_name);
			if (node_record_point == NULL) {
				error
				    ("_build_bitmaps: invalid node name specified %s",
				     this_node_name);
				free(this_node_name);
				continue;
			}
			j = node_record_point - node_record_table_ptr;
			if (bit_test(all_part_node_bitmap, j) == 1) {
				error
				    ("_build_bitmaps: node %s defined in more than one partition",
				     this_node_name);
				error
				    ("_build_bitmaps: only the first specification is honored");
			} else {
				bit_set(part_record_point->node_bitmap, j);
				bit_set(all_part_node_bitmap, j);
				part_record_point->total_nodes++;
				part_record_point->total_cpus +=
				    node_record_point->cpus;
				node_record_point->partition_ptr =
				    part_record_point;
			}
			free(this_node_name);
		}
		hostlist_destroy(host_list);
	}
	list_iterator_destroy(part_record_iterator);
	bit_free(all_part_node_bitmap);
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


/* 
 * _parse_node_spec - parse the node specification (per the configuration 
 *	file format), build table and set values
 * IN/OUT in_line - line from the configuration file, parsed keywords 
 *	and values replaced by blanks
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 * global: default_config_record - default configuration values for
 *	                           group of nodes
 *	default_node_record - default node configuration values
 */
static int _parse_node_spec(char *in_line)
{
	char *node_addr, *node_name, *state, *feature;
	char *this_node_addr, *this_node_name;
	int error_code, first, i;
	int state_val, cpus_val, real_memory_val, tmp_disk_val, weight_val;
	struct node_record *node_record_point;
	struct config_record *config_point = NULL;
	hostlist_t addr_list = NULL, host_list = NULL;

	node_addr = node_name = state = feature = (char *) NULL;
	cpus_val = real_memory_val = state_val = NO_VAL;
	tmp_disk_val = weight_val = NO_VAL;
	if ((error_code = load_string(&node_name, "NodeName=", in_line)))
		return error_code;
	if (node_name == NULL)
		return 0;	/* no node info */

	error_code = slurm_parser(in_line,
				  "Feature=", 's', &feature,
				  "NodeAddr=", 's', &node_addr,
				  "Procs=", 'd', &cpus_val,
				  "RealMemory=", 'd', &real_memory_val,
				  "State=", 's', &state,
				  "TmpDisk=", 'd', &tmp_disk_val,
				  "Weight=", 'd', &weight_val, "END");

	if (error_code)
		goto cleanup;

	if (state != NULL) {
		state_val = NO_VAL;
		for (i = 0; i <= NODE_STATE_END; i++) {
			if (strcasecmp(node_state_string(i), "END") == 0)
				break;
			if (strcasecmp(node_state_string(i), state) == 0) {
				state_val = i;
				break;
			}
		}
		if ((state_val == NO_VAL) ||
		    (state_val == NODE_STATE_COMPLETING)) {
			error
			    ("_parse_node_spec: invalid initial state %s for node %s",
			     state, node_name);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(state);
	}

	if (node_addr &&
	    ((addr_list = hostlist_create(node_addr)) == NULL)) {
		error("hostlist_create error for %s: %m", node_addr);
		error_code = errno;
		goto cleanup;
	}

	if ((host_list = hostlist_create(node_name)) == NULL) {
		error("hostlist_create error for %s: %m", node_name);
		error_code = errno;
		goto cleanup;
	}

	first = 1;
	while ((this_node_name = hostlist_shift(host_list))) {
		if (strcmp(this_node_name, "localhost") == 0) {
			free(this_node_name);
			this_node_name = malloc(128);
			if (this_node_name == NULL)
				fatal ("memory allocation failure");
			getnodename(this_node_name, 128);
		}
		if (strcasecmp(this_node_name, "DEFAULT") == 0) {
			xfree(node_name);
			if (cpus_val != NO_VAL)
				default_config_record.cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				default_config_record.real_memory =
				 		   real_memory_val;
			if (tmp_disk_val != NO_VAL)
				default_config_record.tmp_disk =
						    tmp_disk_val;
			if (weight_val != NO_VAL)
				default_config_record.weight = weight_val;
			if (state_val != NO_VAL)
				default_node_record.node_state = state_val;
			if (feature) {
				xfree(default_config_record.feature);
				default_config_record.feature = feature;
				feature = NULL;
			}
			free(this_node_name);
			break;
		}

		if (first == 1) {
			first = 0;
			config_point = create_config_record();
			if (config_point->nodes)
				free(config_point->nodes);
			config_point->nodes = node_name;
			if (cpus_val != NO_VAL)
				config_point->cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				config_point->real_memory =
						    real_memory_val;
			if (tmp_disk_val != NO_VAL)
				config_point->tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				config_point->weight = weight_val;
			if (feature) {
				xfree(config_point->feature);
				config_point->feature = feature;
				feature = NULL;
			}
		}

		if (strcmp(this_node_name, highest_node_name) <= 0)
			node_record_point =
				    find_node_record(this_node_name);
		else {
			strncpy(highest_node_name, this_node_name,
				MAX_NAME_LEN);
			node_record_point = NULL;
		}

		if (node_record_point == NULL) {
			node_record_point =
			    create_node_record(config_point,
					       this_node_name);
			if ((state_val != NO_VAL) &&
			    (state_val != NODE_STATE_UNKNOWN))
				node_record_point->node_state = state_val;
			node_record_point->last_response = (time_t) 0;
			if (node_addr)
				this_node_addr = hostlist_shift(addr_list);
			else
				this_node_addr = NULL;
			if (this_node_addr) {
				strncpy(node_record_point->comm_name,
					this_node_addr, MAX_NAME_LEN);
				free(this_node_addr);
			} else {
				strncpy(node_record_point->comm_name,
					node_record_point->name,
					MAX_NAME_LEN);
			}
		} else {
			error
			    ("_parse_node_spec: reconfiguration for node %s ignored",
			     this_node_name);
		}
		free(this_node_name);
	}

	/* free allocated storage */
	xfree(node_addr);
	if (addr_list)
		hostlist_destroy(addr_list);
	hostlist_destroy(host_list);
	return error_code;

      cleanup:
	xfree(node_addr);
	xfree(node_name);
	xfree(feature);
	xfree(state);
	return error_code;
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
	char *allow_groups, *nodes, *partition_name;
	char *default_str, *root_str, *shared_str, *state_str;
	int max_time_val, max_nodes_val, min_nodes_val;
	int root_val, default_val;
	int state_val, shared_val;
	int error_code;
	struct part_record *part_record_point;

	partition_name = (char *) NULL;
	default_str = shared_str = state_str = (char *) NULL;
	max_time_val = max_nodes_val = root_val = default_val = state_val =
	    shared_val = NO_VAL;

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
				  "RootOnly=", 's', &root_str,
				  "MaxTime=", 'd', &max_time_val,
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
			error
			    ("update_part: ignored partition %s update, bad state %s",
			     partition_name, default_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(default_str);
	}

	if (root_str) {
		if (strcasecmp(root_str, "YES") == 0)
			root_val = 1;
		else if (strcasecmp(root_str, "NO") == 0)
			root_val = 0;
		else {
			error
			    ("update_part: ignored partition %s update, bad key %s",
			     partition_name, root_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(root_str);
	}

	if (shared_str) {
		if (strcasecmp(shared_str, "YES") == 0)
			shared_val = SHARED_YES;
		else if (strcasecmp(shared_str, "NO") == 0)
			shared_val = SHARED_NO;
		else if (strcasecmp(shared_str, "FORCE") == 0)
			shared_val = SHARED_FORCE;
		else {
			error
			    ("update_part: ignored partition %s update, bad shared %s",
			     partition_name, shared_str);
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
			error
			    ("update_part: ignored partition %s update, bad state %s",
			     partition_name, state_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree(state_str);
	}

	if (strcasecmp(partition_name, "DEFAULT") == 0) {
		xfree(partition_name);
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
			default_part.allow_groups = allow_groups;
			allow_groups = NULL;
		}
		if (nodes) {
			xfree(default_part.nodes);
			default_part.nodes = nodes;
			nodes = NULL;
		}
		return 0;
	}

	part_record_point =
	    list_find_first(part_list, &list_find_part, partition_name);
	if (part_record_point == NULL) {
		part_record_point = create_part_record();
		strcpy(part_record_point->name, partition_name);
	} else {
		info("_parse_node_spec: duplicate entry for partition %s",
		     partition_name);
	}
	if (default_val == 1) {
		if (strlen(default_part_name) > 0)
			info(
			   "_parse_part_spec: changing default partition from %s to %s", 
			   default_part_name, partition_name);
		strcpy(default_part_name, partition_name);
		default_part_loc = part_record_point;
	}
	if (max_time_val != NO_VAL)
		part_record_point->max_time  = max_time_val;
	if (max_nodes_val != NO_VAL)
		part_record_point->max_nodes = max_nodes_val;
	if (min_nodes_val != NO_VAL)
		part_record_point->min_nodes = min_nodes_val;
	if (root_val != NO_VAL)
		part_record_point->root_only = root_val;
	if (state_val != NO_VAL)
		part_record_point->state_up  = state_val;
	if (shared_val != NO_VAL)
		part_record_point->shared    = shared_val;
	if (allow_groups) {
		xfree(part_record_point->allow_groups);
		part_record_point->allow_groups = allow_groups;
		allow_groups = NULL;
	}
	if (nodes) {
		xfree(part_record_point->nodes);
		if (strcmp(nodes, "localhost") == 0) {
			xfree(nodes);
			nodes = xmalloc(128);
			if (nodes == NULL)
				fatal ("memory allocation failure");
			getnodename(nodes, 128);
		}
		part_record_point->nodes = nodes;
		nodes = NULL;
	}
	xfree(partition_name);
	return 0;

      cleanup:
	xfree(allow_groups);
	xfree(default_str);
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
 * IN recover - set to use state saved from last slurmctld shutdown
 * RET 0 if no error, otherwise an error code
 * Note: Operates on common variables only
 */
int read_slurm_conf(int recover)
{
	clock_t start_time;
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUF_SIZE];	/* input line */
	int i, j, error_code;
	int old_node_record_count;
	struct node_record *old_node_table_ptr;
	char node_name[MAX_NAME_LEN];

	/* initialization */
	start_time = clock();
	old_node_record_count = node_record_count;
	old_node_table_ptr = 
		node_record_table_ptr;  /* save node states for reconfig RPC */
	node_record_table_ptr = NULL;
	if ((error_code = _init_all_slurm_conf())) {
		node_record_table_ptr = old_node_table_ptr;
		return error_code;
	}

	slurm_spec_file = fopen(slurmctld_conf.slurm_conf, "r");
	if (slurm_spec_file == NULL) {
		error("read_slurm_conf error opening file %s, %m",
		      slurmctld_conf.slurm_conf);
		exit(1);
	}

	info("read_slurm_conf: loading configuration from %s",
	     slurmctld_conf.slurm_conf);

	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen(in_line) >= (BUF_SIZE - 1)) {
			error
			    ("read_slurm_conf line %d, of input file %s too long",
			     line_num, slurmctld_conf.slurm_conf);
			xfree(old_node_table_ptr);
			fclose(slurm_spec_file);
			return E2BIG;
			break;
		}

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		/* escape sequence "\#" translated to "#" */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}
				continue;
			}
			in_line[i] = (char) NULL;
			break;
		}

		/* parse what is left, non-comments */

		/* overall configuration parameters */
		if ((error_code =
		     parse_config_spec(in_line, &slurmctld_conf))) {
			fclose(slurm_spec_file);
			xfree(old_node_table_ptr);
			return error_code;
		}

		/* node configuration parameters */
		if ((error_code = _parse_node_spec(in_line))) {
			fclose(slurm_spec_file);
			xfree(old_node_table_ptr);
			return error_code;
		}

		/* partition configuration parameters */
		if ((error_code = _parse_part_spec(in_line))) {
			fclose(slurm_spec_file);
			xfree(old_node_table_ptr);
			return error_code;
		}

		/* report any leftover strings on input line */
		report_leftover(in_line, line_num);
	}
	fclose(slurm_spec_file);

	validate_config(&slurmctld_conf);
	_set_config_defaults(&slurmctld_conf);
	update_logging();

	if (default_part_loc == NULL) {
		error("read_slurm_conf: default partition not set.");
		xfree(old_node_table_ptr);
		return EINVAL;
	}

	if (node_record_count < 1) {
		error("read_slurm_conf: no nodes configured.");
		xfree(old_node_table_ptr);
		return EINVAL;
	}

	rehash();
	set_slurmd_addr();

	if ((error_code = getnodename(node_name, MAX_NAME_LEN)))
		fatal("getnodename error %s", slurm_strerror(error_code));
	if (slurmctld_conf.control_machine &&
	    (strcmp(node_name, slurmctld_conf.control_machine) == 0))
		(void) shutdown_backup_controller();

	if (recover) {
		(void) load_all_node_state();
		(void) load_all_part_state();
		(void) load_all_job_state();
	} else {
		reset_first_job_id();
		if (old_node_table_ptr) {
			info("restoring original state of nodes");
			_restore_node_state(old_node_table_ptr, 
					    old_node_record_count);
		}
		reset_job_bitmaps();
	}
	(void) _sync_nodes_to_jobs();
	(void) sync_job_files();
	xfree(old_node_table_ptr);

	if ((error_code = _build_bitmaps()))
		return error_code;
#ifdef 	HAVE_LIBELAN3
	_validate_node_proc_count();
#endif
	(void) _sync_nodes_to_comp_job();

	load_part_uid_allow_list(1);

	/* sort config_list by weight for scheduling */
	list_sort(config_list, &list_compare_config);

	slurmctld_conf.last_update = time(NULL);
	info("read_slurm_conf: finished loading configuration, time=%ld",
	     (long) (clock() - start_time));

	return SLURM_SUCCESS;
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
		node_ptr->node_state	= old_node_table_ptr[i].node_state;
		node_ptr->last_response	= old_node_table_ptr[i].last_response;
		node_ptr->cpus		= old_node_table_ptr[i].cpus;
		node_ptr->real_memory	= old_node_table_ptr[i].real_memory;
		node_ptr->tmp_disk	= old_node_table_ptr[i].tmp_disk;
	}
}


/* Set configuration parameters to default values if not initialized 
 * by the configuration file 
 */
static void _set_config_defaults(slurm_ctl_conf_t * ctl_conf_ptr)
{
	if (ctl_conf_ptr->backup_controller == NULL)
		info(
		   "read_slurm_conf: backup_controller value not specified.");

	if (ctl_conf_ptr->fast_schedule == (uint16_t) NO_VAL)
		ctl_conf_ptr->fast_schedule = 1;

	if (ctl_conf_ptr->first_job_id == (uint32_t) NO_VAL)
		ctl_conf_ptr->first_job_id = 1;

	if (ctl_conf_ptr->hash_base == (uint16_t) NO_VAL)
		ctl_conf_ptr->hash_base = 10;

	if (ctl_conf_ptr->heartbeat_interval == (uint16_t) NO_VAL)
		ctl_conf_ptr->heartbeat_interval = 60;

	if (ctl_conf_ptr->inactive_limit == (uint16_t) NO_VAL)
		ctl_conf_ptr->inactive_limit = 0;	/* unlimited */

	if (ctl_conf_ptr->kill_wait == (uint16_t) NO_VAL)
		ctl_conf_ptr->kill_wait = 30;

	if (ctl_conf_ptr->ret2service == (uint16_t) NO_VAL)
		ctl_conf_ptr->ret2service = 0;

	if (ctl_conf_ptr->slurmctld_timeout == (uint16_t) NO_VAL)
		ctl_conf_ptr->slurmctld_timeout = 300;

	if (ctl_conf_ptr->slurmd_timeout == (uint16_t) NO_VAL)
		ctl_conf_ptr->slurmd_timeout = 300;

	if (ctl_conf_ptr->state_save_location == NULL)
		ctl_conf_ptr->state_save_location =
		    xstrdup(DEFAULT_TMP_FS);

	if (ctl_conf_ptr->tmp_fs == NULL)
		ctl_conf_ptr->tmp_fs = xstrdup(DEFAULT_TMP_FS);
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
	ListIterator job_record_iterator;
	int update_cnt = 0;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) 
			  list_next(job_record_iterator))) {
		if (job_ptr->node_bitmap == NULL)
			continue;

		if ((job_ptr->job_state == JOB_RUNNING) ||
		    (job_ptr->job_state &  JOB_COMPLETING))
			update_cnt += _sync_nodes_to_active_job(job_ptr);
	}
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
	ListIterator job_record_iterator;
	int update_cnt = 0;

	job_record_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) 
			  list_next(job_record_iterator))) {
		if ((job_ptr->node_bitmap) &&
		    (job_ptr->job_state & JOB_COMPLETING)) {
			update_cnt++;
			info("Killing job_id %u", job_ptr->job_id);
			deallocate_nodes(job_ptr, false);
		}
	}
	if (update_cnt)
		info("_sync_nodes_to_jobs updated state of %d nodes",
		     update_cnt);
	return update_cnt;
}

static int _sync_nodes_to_active_job(struct job_record *job_ptr)
{
	int i, cnt = 0;
	uint16_t base_state, no_resp_flag;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		base_state = node_record_table_ptr[i].node_state & 
			     (~NODE_STATE_NO_RESPOND);
		if (base_state == NODE_STATE_DOWN) {
			job_ptr->job_state = JOB_NODE_FAIL | JOB_COMPLETING;
			job_ptr->end_time = time(NULL);
			delete_all_step_records(job_ptr);
		} else {
	 		node_record_table_ptr[i].run_job_cnt++; /* NOTE:
					* This counter moved to comp_job_cnt 
					* by _sync_nodes_to_comp_job() */
			no_resp_flag = node_record_table_ptr[i].node_state & 
				       NODE_STATE_NO_RESPOND;
			if ((base_state == NODE_STATE_UNKNOWN) || 
			    (base_state == NODE_STATE_IDLE)) {
				cnt++;
				node_record_table_ptr[i].node_state =
				    NODE_STATE_ALLOCATED | no_resp_flag;
			} else if (base_state == NODE_STATE_DRAINED) {
				cnt++;
				node_record_table_ptr[i].node_state =
				    NODE_STATE_DRAINING | no_resp_flag;
			}
		} 
	}
	return cnt;
}

#ifdef 	HAVE_LIBELAN3
/* Every node in a given partition must have the same processor count 
 * at present, this function insure it */
static void _validate_node_proc_count(void)
{
	ListIterator part_record_iterator;
	struct part_record *part_record_point;
	int first_bit, last_bit, i, node_size, part_size;

	part_record_iterator = list_iterator_create(part_list);
	while ((part_record_point =
		(struct part_record *) list_next(part_record_iterator))) {
		first_bit = bit_ffs(part_record_point->node_bitmap);
		last_bit = bit_fls(part_record_point->node_bitmap);
		part_size = -1;
		for (i = first_bit; i <= last_bit; i++) {
			if (bit_test(part_record_point->node_bitmap, i) == 0)
				continue;

			if (slurmctld_conf.fast_schedule)
				node_size =
				    node_record_table_ptr[i].config_ptr->
				    cpus;
			else
				node_size = node_record_table_ptr[i].cpus;

			if (part_size == -1)
				part_size = node_size;
			else if (part_size != node_size)
				fatal
				    ("Partition %s has inconsisent processor count",
				     part_record_point->name);
		}
	}
	list_iterator_destroy(part_record_iterator);
}
#endif
