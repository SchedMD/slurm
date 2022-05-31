/*****************************************************************************\
 *  node_conf.h - definitions for reading the node part of slurm configuration
 *  file and work with the corresponding structures
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2016 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifndef _HAVE_NODE_CONF_H
#define _HAVE_NODE_CONF_H

#include "config.h"

#include <inttypes.h>
#include <time.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xhash.h"

#define CONFIG_MAGIC	0xc065eded
#define NODE_MAGIC	0x0de575ed

typedef struct {
	uint16_t boards;	/* count of boards configured */
	uint16_t core_spec_cnt;	/* number of specialized cores */
	uint16_t cores;		/* number of cores per socket */
	uint32_t cpu_bind;	/* default CPU binding type */
	char *cpu_spec_list;	/* arbitrary list of specialized cpus */
	uint16_t cpus;		/* count of processors running on the node */
	char *feature;		/* arbitrary list of node's features */
	char *gres;		/* arbitrary list of node's generic resources */
	uint32_t magic;		/* magic cookie to test data integrity */
	uint64_t mem_spec_limit; /* MB real memory for memory specialization */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this configuration */
	char *nodes;		/* name of nodes with this configuration */
	uint64_t real_memory;	/* MB real memory on the node */
	uint16_t threads;	/* number of threads per core */
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	uint16_t tot_sockets;	/* number of sockets per node */
	double  *tres_weights;	/* array of TRES weights */
	char    *tres_weights_str; /* per TRES billing weight string */
	uint32_t weight;	/* arbitrary priority of node for
				 * scheduling work on */
} config_record_t;
extern List config_list;	/* list of config_record entries */

extern List front_end_list;	/* list of slurm_conf_frontend_t entries */

typedef struct node_record node_record_t;
struct node_record {
	char *arch;			/* computer architecture */
	char *bcast_address;		/* BcastAddr */
	uint16_t boards; 		/* count of boards configured */
	time_t boot_req_time;		/* Time of node boot request */
	time_t boot_time;		/* Time of node boot,
					 * computed from up_time */
	char *comm_name;		/* communications path name to node */
	char *comment;			/* arbitrary comment */
	uint16_t comp_job_cnt;		/* count of jobs completing on node */
	config_record_t *config_ptr;	/* configuration spec ptr */
	uint16_t core_spec_cnt;		/* number of specialized cores on node*/
	uint16_t cores;			/* number of cores per socket */
	uint32_t cpu_bind;		/* default CPU binding type */
	uint32_t cpu_load;		/* CPU load * 100 */
	time_t cpu_load_time;		/* Time when cpu_load last set */
	char *cpu_spec_list;		/* node's specialized cpus */
	uint16_t cpus;			/* count of processors on the node */
	uint16_t cpus_efctv;		/* count of effective cpus on the node.
					   i.e. cpus minus specialized cpus*/
	acct_gather_energy_t *energy;	/* power consumption data */
	ext_sensors_data_t *ext_sensors; /* external sensor data */
	char *extra;			/* arbitrary string */
	char *features;			/* node's available features, used only
					 * for state save/restore, DO NOT
					 * use for scheduling purposes */
	char *features_act;		/* node's active features, used only
					 * for state save/restore, DO NOT
					 * use for scheduling purposes */
	uint64_t free_mem;		/* Free memory in MiB */
	time_t free_mem_time;		/* Time when free_mem last set */
	char *gres;			/* node's generic resources, used only
					 * for state save/restore, DO NOT
					 * use for scheduling purposes */
	List gres_list;			/* list of gres state info managed by
					 * plugins */
	uint32_t index;			/* Index into node_record_table_ptr */
	time_t last_busy;		/* time node was last busy (no jobs) */
	time_t last_response;		/* last response from the node */
	uint32_t magic;			/* magic cookie for data integrity */
	char *mcs_label;		/* mcs_label if mcs plugin in use */
	uint64_t mem_spec_limit;	/* MB memory limit for specialization */
	char *name;			/* name of the node. NULL==defunct */
	uint32_t next_state;		/* state after reboot */
	uint16_t no_share_job_cnt;	/* count of jobs running that will
					 * not share nodes */
	char *node_hostname;		/* hostname of the node */
	node_record_t *node_next;	/* next entry with same hash index */
	uint32_t node_rank;		/* Hilbert number based on node name,
					 * or other sequence number used to
					 * order nodes by location,
					 * no need to save/restore */
	bitstr_t *node_spec_bitmap;	/* node cpu specialization bitmap */
	uint32_t node_state;		/* enum node_states, ORed with
					 * NODE_STATE_NO_RESPOND if not
					 * responding */
	bool not_responding;		/* set if fails to respond,
					 * clear after logging this */
	char *os;			/* operating system now running */
	uint32_t owner;			/* User allowed to use node or NO_VAL */
	uint16_t owner_job_cnt;		/* Count of exclusive jobs by "owner" */
	uint16_t part_cnt;		/* number of associated partitions */
	void **part_pptr;		/* array of pointers to partitions
					 * associated with this node*/
	uint16_t port;			/* TCP port number of the slurmd */
	power_mgmt_data_t *power;	/* power management data */
	time_t power_save_req_time;	/* Time of power_save request */
	uint16_t protocol_version;	/* Slurm version number */
	uint64_t real_memory;		/* MB real memory on the node */
	char *reason; 			/* why a node is DOWN or DRAINING */
	time_t reason_time;		/* Time stamp when reason was
					 * set, ignore if no reason is set. */
	uint32_t reason_uid;		/* User that set the reason, ignore if
					 * no reason is set. */
	uint16_t resume_timeout; 	/* time required in order to perform a
					 * node resume operation */
	uint16_t run_job_cnt;		/* count of jobs running on node */
	uint64_t sched_weight;		/* Node's weight for scheduling
					 * purposes. For cons_tres use */
	dynamic_plugin_data_t *select_nodeinfo; /* opaque data structure,
						 * use select_g_get_nodeinfo()
						 * to access contents */
	time_t slurmd_start_time;	/* Time of slurmd startup */
	uint16_t sus_job_cnt;		/* count of jobs suspended on node */
	uint32_t suspend_time; 		/* node idle for this long before
					 * power save mode */
	uint16_t suspend_timeout;	/* time required in order to perform a
					 * node suspend operation */
	uint64_t *tres_cnt;		/* tres this node has. NO_PACK*/
	char *tres_fmt_str;		/* tres this node has */
	char *tres_str;                 /* tres this node has */
	uint16_t threads;		/* number of threads per core */
	uint32_t tmp_disk;		/* MB total disk in TMP_FS */
	uint16_t tot_cores;		/* number of cores per node */
	uint16_t tot_sockets;		/* number of sockets per node */
	uint32_t up_time;		/* seconds since node boot */
	char *version;			/* Slurm version */
	uint16_t tpc;	                /* number of threads we are using per
					 * core */
	uint32_t weight;		/* orignal weight, used only for state
					 * save/restore, DO NOT use for
					 * scheduling purposes. */
};
extern node_record_t **node_record_table_ptr;  /* ptr to node records */
extern int node_record_count;		/* count in node_record_table_ptr */
extern xhash_t* node_hash_table;	/* hash table for node records */
extern time_t last_node_update;		/* time of last node record update */

extern uint16_t *cr_node_num_cores;
extern uint32_t *cr_node_cores_offset;

/*
 * bitmap2node_name_sortable - given a bitmap, build a list of comma
 *	separated node names. names may include regular expressions
 *	(e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * IN sort   - returned ordered list or not
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name_sortable (bitstr_t *bitmap, bool sort);

/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node
 *	names. names may include regular expressions (e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name (bitstr_t *bitmap);

/*
 * bitmap2hostlist - given a bitmap, build a hostlist
 * IN bitmap - bitmap pointer
 * RET pointer to hostlist or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
hostlist_t bitmap2hostlist (bitstr_t *bitmap);

/*
 * build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * IN set_bitmap - if true then set node_bitmap in config record (used by
 *		    slurmd), false is used by slurmctld, clients, and testsuite
 * IN tres_cnt - number of TRES configured on system (used on controller side)
 */
extern void build_all_nodeline_info(bool set_bitmap, int tres_cnt);

/*
 * build_all_frontend_info - get a array of slurm_conf_frontend_t structures
 *	from the slurm.conf reader, build table, and set values
 * is_slurmd_context: set to true if run from slurmd
 */
extern void build_all_frontend_info (bool is_slurmd_context);

/*
 * Expand a nodeline's node names, host names, addrs, ports into separate nodes.
 */
extern void expand_nodeline_info(slurm_conf_node_t *node_ptr,
				 config_record_t *config_ptr,
				 void (*_callback) (
				       char *alias, char *hostname,
				       char *address, char *bcast_addr,
				       uint16_t port, int state_val,
				       slurm_conf_node_t *node_ptr,
				       config_record_t *config_ptr));

/*
 * create_config_record - create a config_record entry and set is values to
 *	the defaults. each config record corresponds to a line in the
 *	slurm.conf file and typically describes the configuration of a
 *	large number of nodes
 * RET pointer to the config_record
 * NOTE: memory allocated will remain in existence until
 *	_delete_config_record() is called to delete all configuration records
 */
extern config_record_t *create_config_record(void);

/*
 * Create a config_record and initialize it with the given conf_node.
 *
 * IN conf_node - conf_node from slurm.conf to initialize config_record with.
 * IN tres_cnt - number of system tres to initialize tres arrays.
 * RET return config_record_t* on success, NULL otherwise.
 */
extern config_record_t *config_record_from_conf_node(
	slurm_conf_node_t *conf_node, int tres_cnt);

/*
 * Grow the node_record_table_ptr.
 */
extern void grow_node_record_table_ptr();

/*
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * NOTE: grows node_record_table_ptr if needed and appends a new node_record_t *
 *       to node_record_table_ptr and increases node_record_count.
 */
extern node_record_t *create_node_record(config_record_t *config_ptr,
					 char *node_name);

/*
 * Create a new node_record_t * at the specified index.
 *
 * IN index - index in node_record_table_ptr where to create new
 *            node_record_t *. node_record_table_ptr[index] should be null and
 *            less than node_record_count.
 * IN node_name - name of node to create
 * IN config_ptr - pointer to node's configuration information
 * RET new node_record_t * on sucess, NULL otherwise.
 * NOTE: node_record_count isn't changed.
 */
extern node_record_t *create_node_record_at(int index, char *node_name,
					    config_record_t *config_ptr);

/*
 * Add a node to node_record_table_ptr without growing the table and increasing
 * node_reocrd_count. The node in an empty slot in the node_record_table_ptr.
 *
 * IN alias - name of node.
 * IN config_ptr - config_record_t* to initialize node with.
 * RET node_record_t* on SUCESS, NULL otherwise.
 */
extern node_record_t *add_node_record(char *alias, config_record_t *config_ptr);

/*
 * Add existing record to node_record_table_ptr
 *
 * e.g. Preserving dynamic nodes after a reconfig.
 * Node must fit in currently allocated node_record_count/MaxNodeCount.
 * node_ptr->config_ptr is added to the the global config_list.
 */
extern void insert_node_record(node_record_t *node_ptr);

/*
 * Delete node from node_record_table_ptr.
 *
 * IN node_ptr - node_ptr to delete
 */
extern void delete_node_record(node_record_t *node_ptr);

/*
 * find_node_record - find a record for node with specified name
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Logs an error if the node name is NOT found
 */
extern node_record_t *find_node_record(char *name);

/*
 * find_node_record2 - find a record for node with specified name
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Does not log an error if the node name is NOT found
 */
extern node_record_t *find_node_record2(char *name);

/*
 * find_node_record_no_alias - find a record for node with specified name
 * without looking at the node's alias (NodeHostName).
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Does not log an error if the node name is NOT found
 */
extern node_record_t *find_node_record_no_alias(char *name);

/*
 * hostlist2bitmap - given a hostlist, build a bitmap representation
 * IN hl          - hostlist
 * IN best_effort - if set don't return an error on invalid node name entries
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 */
extern int hostlist2bitmap (hostlist_t hl, bool best_effort, bitstr_t **bitmap);

/*
 * init_node_conf - initialize the node configuration tables and values.
 *	this should be called before creating any node or configuration
 *	entries.
 */
extern void init_node_conf(void);

/* node_fini2 - free memory associated with node records (except bitmaps) */
extern void node_fini2 (void);

/*
 * given a node name return inx in node_record_table_ptr
 */
extern int node_name_get_inx(char *node_name);

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation
 * IN node_names  - list of nodes
 * IN best_effort - if set don't return an error on invalid node name entries
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 * NOTE: the caller must bit_free() memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bool best_effort,
			     bitstr_t **bitmap);

/* Purge the contents of a node record */
extern void purge_node_rec(node_record_t *node_ptr);

/*
 * rehash_node - build a hash table of the node_record entries.
 * NOTE: manages memory for node_hash_table
 */
extern void rehash_node (void);

/* Convert a node state string to it's equivalent enum value */
extern int state_str2int(const char *state_str, char *node_name);

/* (re)set cr_node_num_cores arrays */
extern void cr_init_global_core_data(node_record_t **node_ptr, int node_cnt);

extern void cr_fini_global_core_data(void);

/*return the coremap index to the first core of the given node */
extern uint32_t cr_get_coremap_offset(uint32_t node_index);

/* Return a bitmap the size of the machine in cores. On a Bluegene
 * system it will return a bitmap in cnodes. */
extern bitstr_t *cr_create_cluster_core_bitmap(int core_mult);

/*
 * Determine maximum number of CPUs on this node usable by a job
 * ntasks_per_core IN - tasks-per-core to be launched by this job
 * cpus_per_task IN - number of required  CPUs per task for this job
 * total_cores IN - total number of cores on this node
 * total_cpus IN - total number of CPUs on this node
 * RET count of usable CPUs on this node usable by this job
 */
extern int adjust_cpus_nppcu(uint16_t ntasks_per_core, int cpus_per_task,
			     int total_cores, int total_cpus);

/*
 * find_hostname - Given a position and a string of hosts, return the hostname
 *                 from that position.
 * IN pos - position in hosts you want returned.
 * IN hosts - string representing a hostlist of hosts.
 * RET - hostname or NULL on error.
 * NOTE: caller must xfree result.
 */
extern char *find_hostname(uint32_t pos, char *hosts);

/*
 * Return the next non-null node_record_t * in the node_record_table_ptr.
 *
 * IN/OUT index - index to start iterating node_record_table_ptr from.
 *                Should be used in the following form so that i will increment
 *                to the next slot and i == node_ptr->index.
 *                e.g.
 *                for (int i = 0; (node_ptr = next_node(&i); i++)
 * RET - next non-null node_record_t * or NULL if finished iterating.
 */
extern node_record_t *next_node(int *index);

/*
 * Return bitmap with all active nodes set.
 *
 * node_record_table_ptr may have NULL slots in it, so return a bitmap with only
 * non-null node bits set.
 *
 * NOTE: caller must free returned bitmap.
 */
extern bitstr_t *node_conf_get_active_bitmap(void);

/*
 * Set bitmap with all active node bits.
 *
 * node_record_table_ptr may have NULL slots in it, so only set non-null node
 * bits.
 */
extern void node_conf_set_all_active_bits(bitstr_t *b);

#endif /* !_HAVE_NODE_CONF_H */
