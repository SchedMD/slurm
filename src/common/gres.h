/*****************************************************************************\
 *  gres.h - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef _GRES_H
#define _GRES_H

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "src/common/bitstring.h"
#include "src/common/job_resources.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"

#define GRES_MAGIC 0x438a34d4
#define GRES_MAX_LINK 1023

enum {
	GRES_VAL_TYPE_FOUND  = 0,
	GRES_VAL_TYPE_CONFIG = 1,
	GRES_VAL_TYPE_AVAIL  = 2,
	GRES_VAL_TYPE_ALLOC  = 3
};

typedef struct {
	int alloc;
	int dev_num;
	char *major;
	char *path;
} gres_device_t;

#define GRES_CONF_OLD_FILE	0x01	/* File= is configured. No independent
					 * information about Type= option.*/
#define GRES_CONF_HAS_FILE	0x02	/* File= is configured */
#define GRES_CONF_HAS_TYPE	0x04	/* Type= is configured */

#define GRES_NO_CONSUME		0x0001	/* Requesting no consume of resources */

/* GRES AutoDetect options */
#define GRES_AUTODETECT_NONE    0x00000000
#define GRES_AUTODETECT_NVML    0x00000001

/* Gres state information gathered by slurmd daemon */
typedef struct gres_slurmd_conf {
	uint8_t config_flags;	/* See GRES_CONF_* values above */

	/* Count of gres available in this configuration record */
	uint64_t count;

	/* Specific CPUs associated with this configuration record */
	uint32_t cpu_cnt;
	char *cpus;
	bitstr_t *cpus_bitmap;	/* Using LOCAL mapping */

	/* Device file associated with this configuration record */
	char *file;

	/* Comma-separated list of communication link IDs (numbers) */
	char *links;

	/* Name of this gres */
	char *name;

	/* Type of this GRES (e.g. model name) */
	char *type_name;

	/* GRES ID number */
	uint32_t plugin_id;
} gres_slurmd_conf_t;


/* Extra data and functions to be passed in to the node_config_load() */
typedef struct node_config_load {
	/* How many CPUs there are configured on the node */
	uint32_t cpu_cnt;
	/* A pointer to the mac_to_abs function */
	int (*xcpuinfo_mac_to_abs) (char *mac, char **abs);
} node_config_load_t;

/* Current GRES state information managed by slurmctld daemon */
typedef struct gres_node_state {
	/* Actual hardware found */
	uint64_t gres_cnt_found;

	/* Configured resources via "Gres" parameter */
	uint64_t gres_cnt_config;

	/* Non-consumable: Do not track resources allocated to jobs */
	bool no_consume;

	/* True if set by node_feature plugin, ignore info from compute node */
	bool node_feature;

	/*
	 * Total resources available for allocation to jobs.
	 * gres_cnt_found or gres_cnt_config, depending upon FastSchedule
	 */
	uint64_t gres_cnt_avail;

	/* List of GRES in current use. Set NULL if needs to be rebuilt. */
	char *gres_used;

	/* Resources currently allocated to jobs */
	uint64_t  gres_cnt_alloc;
	bitstr_t *gres_bit_alloc;	/* If gres.conf contains File field */

	/*
	 * Topology specific information. In the case of gres/mps, there is one
	 * topo record per file (GPU) and the size of the GRES bitmaps (i.e.
	 * gres_bit_alloc and topo_gres_bitmap[#]) is equal to the number of
	 * GPUs on the node while the count is a site-configurable value.
	 */
	uint16_t topo_cnt;		/* Size of topo_ arrays */
	int link_len;			/* Size of link_cnt */
	int **links_cnt;		/* Count of links between GRES */
	bitstr_t **topo_core_bitmap;
	bitstr_t **topo_gres_bitmap;
	uint64_t *topo_gres_cnt_alloc;
	uint64_t *topo_gres_cnt_avail;
	uint32_t *topo_type_id;		/* GRES type (e.g. model ID) */
	char **topo_type_name;		/* GRES type (e.g. model name) */

	/*
	 * GRES type specific information (if gres.conf contains type option)
	 *
	 * NOTE: If a job requests GRES without a type specification, these
	 * type_cnt_alloc will not be incremented. Only the gres_cnt_alloc
	 * will be incremented.
	 */
	uint16_t type_cnt;		/* Size of type_ arrays */
	uint64_t *type_cnt_alloc;
	uint64_t *type_cnt_avail;
	uint32_t *type_id;		/* GRES type (e.g. model ID) */
	char **type_name;		/* GRES type (e.g. model name) */
} gres_node_state_t;

/* Gres job state as used by slurmctld daemon */
typedef struct gres_job_state {
	char *gres_name;		/* GRES name (e.g. "gpu") */
	uint32_t type_id;		/* GRES type (e.g. model ID) */
	char *type_name;		/* GRES type (e.g. model name) */
	uint16_t flags;			/* GRES_NO_CONSUME, etc. */

	/* Count of required GRES resources plus associated CPUs and memory */
	uint16_t cpus_per_gres;
	uint64_t gres_per_job;
	uint64_t gres_per_node;
	uint64_t gres_per_socket;
	uint64_t gres_per_task;
	uint64_t mem_per_gres;

	/*
	 * Default GRES configuration parameters. These values are subject to
	 * change depending upon which partition the job is currently being
	 * considered for scheduling in.
	 */
	uint16_t def_cpus_per_gres;
	uint64_t def_mem_per_gres;

	/*
	 * Selected resource details. One entry per node on the cluster.
	 * Used by select/cons_tres to identify which resources would be
	 * allocated on a node IF that node is included in the job allocation.
	 * Once specific nodes are selected for the job allocation, select
	 * portions of these arrays are copied to gres_bit_alloc and
	 * gres_cnt_node_alloc. The fields can then be cleared.
	 */
	uint32_t total_node_cnt;	/* cluster total node count */
	bitstr_t **gres_bit_select;	/* Per node GRES selected,
					 * Used with GRES files */
	uint64_t *gres_cnt_node_select;	/* Per node GRES selected,
					 * Used without GRES files */

	/* Allocated resources details */
	uint64_t total_gres;		/* Count of allocated GRES to job */
	uint32_t node_cnt;		/* 0 if no_consume */
	bitstr_t **gres_bit_alloc;	/* Per node GRES allocated,
					 * Used with GRES files */
	uint64_t *gres_cnt_node_alloc;	/* Per node GRES allocated,
					 * Used with and without GRES files */

	/*
	 * Resources currently allocated to job steps on each node.
	 * This will be a subset of resources allocated to the job.
	 * gres_bit_step_alloc is a subset of gres_bit_alloc
	 */
	bitstr_t **gres_bit_step_alloc;
	uint64_t  *gres_cnt_step_alloc;
} gres_job_state_t;

/* Used to set Prolog and Epilog env var. Currently designed for gres/mps. */
typedef struct gres_epilog_info {
	uint32_t plugin_id;	/* GRES ID number */
	uint32_t node_cnt;	/* Count of all hosts allocated to job */
	char *node_list;	/* List of all hosts allocated to job */
	bitstr_t **gres_bit_alloc; /* Per-node bitmap of allocated resources */
	uint64_t *gres_cnt_node_alloc;	/* Per node GRES allocated,
					 * Used with and without GRES files */
} gres_epilog_info_t;

/* Gres job step state as used by slurmctld daemon */
typedef struct gres_step_state {
	uint32_t type_id;		/* GRES type (e.g. model ID) */
	char *type_name;		/* GRES type (e.g. model name) */
	uint16_t flags;			/* GRES_NO_CONSUME, etc. */

	/* Count of required GRES resources plus associated CPUs and memory */
	uint16_t cpus_per_gres;
	uint64_t gres_per_step;
	uint64_t gres_per_node;
	uint64_t gres_per_socket;
	uint64_t gres_per_task;
	uint64_t mem_per_gres;

	/*
	 * Allocated resources details
	 *
	 * NOTE: node_cnt and the size of node_in_use and gres_bit_alloc are
	 * identical to that of the job for simplicity. Bits in node_in_use
	 * are set for those node of the job that are used by this step and
	 * gres_bit_alloc are also set if the job's gres_bit_alloc is set
	 */
	uint64_t total_gres;		/* allocated GRES for this step */
	uint64_t gross_gres;		/* used during the scheduling phase,
					 * GRES that could be available for this
					 * step if no other steps active */
	uint64_t *gres_cnt_node_alloc;	/* Per node GRES allocated,
					 * Used without GRES files */
	uint32_t node_cnt;
	bitstr_t *node_in_use;
	bitstr_t **gres_bit_alloc;	/* Used with GRES files */
} gres_step_state_t;

/* Per-socket GRES availability information for scheduling purposes */
typedef struct sock_gres {	/* GRES availability by socket */
	bitstr_t *bits_any_sock;/* Per-socket GRES bitmap of this name & type */
	bitstr_t **bits_by_sock;/* Per-socket GRES bitmap of this name & type */
	uint64_t cnt_any_sock;	/* GRES count unconstrained by cores */
	uint64_t *cnt_by_sock;	/* Per-socket GRES count of this name & type */
	char *gres_name;	/* GRES name */
	gres_job_state_t *job_specs;	/* Pointer to job info, for limits */
	uint64_t max_node_gres;	/* Maximum GRES permitted on this node */
	gres_node_state_t *node_specs;	/* Pointer to node info, for state */
	uint32_t plugin_id;	/* Plugin ID (for quick search) */
	int sock_cnt;		/* Socket count, size of bits_by_sock and
				 * cnt_by_sock arrays */
	uint64_t total_cnt;	/* Total GRES count of this name & type */
	uint32_t type_id;	/* GRES type (e.g. model ID) */
	char *type_name;	/* GRES type (e.g. model name) */
} sock_gres_t;

/* Similar to multi_core_data_t in slurm_protocol_defs.h */
typedef struct gres_mc_data {
	uint16_t boards_per_node;   /* boards per node required by job */
	uint16_t sockets_per_board; /* sockets per board required by job */
	uint16_t sockets_per_node;  /* sockets per node required by job */
	uint16_t cores_per_socket;  /* cores per cpu required by job */
	uint16_t threads_per_core;  /* threads per core required by job */

	uint16_t cpus_per_task;     /* Count of CPUs per task */
	uint32_t ntasks_per_job;    /* number of tasks to invoke for job or NO_VAL */
	uint16_t ntasks_per_node;   /* number of tasks to invoke on each node */
	uint16_t ntasks_per_board;  /* number of tasks to invoke on each board */
	uint16_t ntasks_per_socket; /* number of tasks to invoke on each socket */
	uint16_t ntasks_per_core;   /* number of tasks to invoke on each core */
	uint8_t overcommit;         /* processors being over subscribed */
	uint16_t plane_size;        /* plane size for SLURM_DIST_PLANE */
	uint32_t task_dist;         /* task distribution directives */
	uint8_t whole_node;         /* allocate entire node */
} gres_mc_data_t;

typedef enum {
	GRES_STATE_TYPE_NODE = 0,
	GRES_STATE_TYPE_JOB,
	GRES_STATE_TYPE_STEP
} gres_state_type_enum_t;

/*
 * Initialize the GRES plugins.
 *
 * Returns a Slurm errno.
 */
extern int gres_plugin_init(void);

/*
 * Terminate the GRES plugins. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int gres_plugin_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Perform reconfig, re-read any configuration files
 * OUT did_change - set if gres configuration changed
 */
extern int gres_plugin_reconfig(void);

/*
 * Return a plugin-specific help message for salloc, sbatch and srun
 * Result must be xfree()'d
 */
extern char *gres_plugin_help_msg(void);

/*
 * Convert a GRES name or model into a number for faster comparison operations
 * IN name - GRES name or model
 * RET - An int representing a custom hash of the name
 */
extern uint32_t gres_plugin_build_id(char *name);

/*
 * Takes a GRES config line (typically from slurm.conf) and remove any
 * records for GRES which are not defined in GresTypes.
 * RET string of valid GRES, Release memory using xfree()
 */
extern char *gres_plugin_name_filter(char *orig_gres, char *nodes);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMD DAEMON                         *
 **************************************************************************
 */
/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs configured on this node
 * IN node_name - Name of this node
 * IN gres_list - Node's GRES information as loaded from slurm.conf by slurmd
 * IN xcpuinfo_abs_to_mac - Pointer to xcpuinfo_abs_to_mac() funct, if available
 * IN xcpuinfo_mac_to_abs - Pointer to xcpuinfo_mac_to_abs() funct, if available
 * NOTE: Called from slurmd and slurmstepd
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt, char *node_name,
					List gres_list,
					void *xcpuinfo_abs_to_mac,
					void *xcpuinfo_mac_to_abs);

/*
 * Pack this node's gres configuration into a buffer
 * IN/OUT buffer - message buffer to pack
 */
extern int gres_plugin_node_config_pack(Buf buffer);

/*
 * Set GRES devices as allocated or not for a particular job
 * IN gres_list - allocated gres devices
 * IN is_job - if is job function expects gres_job_state_t's else
 *             gres_step_state_t's
 * RET - List of gres_device_t containing all devices from all GRES with alloc
 *       set correctly if the device is allocated to the job/step.
 */
extern List gres_plugin_get_allocated_devices(List gres_list, bool is_job);

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_plugin_send_stepd(int fd);

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_plugin_recv_stepd(int fd);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMCTLD DAEMON                      *
 **************************************************************************
 */
/*
 * Build a node's gres record based only upon the slurm.conf contents
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 */
extern int gres_plugin_init_node_config(char *node_name, char *orig_config,
					List *gres_list);

/* Add a GRES record. This is used by the node_features plugin after the
 * slurm.conf file is read and the initial GRES records are built by
 * gres_plugin_init(). */
extern void gres_plugin_add(char *gres_name);

/*
 * Unpack this node's configuration from a buffer (built/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_node_config_unpack(Buf buffer, char *node_name);

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_plugin_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN threads_per_core - Count of CPUs (threads) per core on this node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN sock_cnt - Count of sockets on this node
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config,
					    char **new_config,
					    List *gres_list,
					    int threads_per_core,
					    int cores_per_sock, int sock_cnt,
					    uint16_t fast_schedule,
					    char **reason_down);

/*
 * Add a GRES from node_feature plugin
 * IN node_name - name of the node for which the gres information applies
 * IN gres_name - name of the GRES being added or updated from the plugin
 * IN gres_size - count of this GRES on this node
 * IN/OUT new_config - Updated GRES info from slurm.conf
 * IN/OUT gres_list - List of GRES records for this node to track usage
 */
extern void gres_plugin_node_feature(char *node_name,
				     char *gres_name, uint64_t gres_size,
				     char **new_config, List *gres_list);

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN new_gres - Updated GRES information supplied from slurm.conf or scontrol
 * IN/OUT gres_str - Node's current GRES string, updated as needed
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * IN cores_per_sock - Number of cores per socket on this node
 * IN sock_per_node - Total count of sockets on this node (on any board)
 */
extern int gres_plugin_node_reconfig(char *node_name,
				     char *new_gres,
				     char **gres_str,
				     List *gres_list,
				     uint16_t fast_schedule,
				     int cores_per_sock,
				     int sock_per_node);

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN/OUT buffer - location to write state to
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_node_state_pack(List gres_list, Buf buffer,
				       char *node_name);
/*
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_node_state_unpack(List *gres_list, Buf buffer,
					 char *node_name,
					 uint16_t protocol_version);

/*
 * Duplicate a node gres status (used for will-run logic)
 * IN gres_list - node gres state information
 * RET a copy of gres_list or NULL on failure
 */
extern List gres_plugin_node_state_dup(List gres_list);

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_plugin_node_state_dealloc_all(List gres_list);

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_plugin_node_state_log(List gres_list, char *node_name);

/*
 * Build a string indicating a node's drained GRES
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_drain(List gres_list);

/*
 * Build a string indicating a node's used GRES
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_used(List gres_list);

/*
 * Give the total system count of a given GRES
 * Returns NO_VAL64 if name not found
 */
extern uint64_t gres_get_system_cnt(char *name);

/*
 * Get the count of a node's GRES
 * IN gres_list - List of Gres records for this node to track usage
 * IN name - name of gres
 */
extern uint64_t gres_plugin_node_config_cnt(List gres_list, char *name);

/*
 * Fill in an array of GRES type ids contained within the given node gres_list
 *		and an array of corresponding counts of those GRES types.
 * IN gres_list - a List of GRES types found on a node.
 * IN arr_len - Length of the arrays (the number of elements in the gres_list).
 * IN gres_count_ids, gres_count_vals - the GRES type ID's and values found
 *	 	in the gres_list.
 * IN val_type - Type of value desired, see GRES_VAL_TYPE_*
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_node_count(List gres_list, int arr_len,
				  uint32_t *gres_count_ids,
				  uint64_t *gres_count_vals,
				  int val_type);

/*
 * Fill in an array of GRES type ids contained within the given job gres_list
 *		and an array of corresponding counts of those GRES types.
 * IN gres_list - a List of GRES types allocated to a job.
 * IN arr_len - Length of the arrays (the number of elements in the gres_list).
 * IN gres_count_ids, gres_count_vals - the GRES type ID's and values found
 *	 	in the gres_list.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_count(List gres_list, int arr_len,
				 uint32_t *gres_count_ids,
				 uint64_t *gres_count_vals);

/*
 * Build a string identifying total GRES counts of each type
 * IN gres_list - a List of GRES types allocated to a job.
 * RET string containing comma-separated list of gres type:model:count
 *     must release memory using xfree()
 */
extern char *gres_plugin_job_alloc_count(List gres_list);

/*
 * Pack a job's allocated gres information for use by prolog/epilog
 * IN gres_list - generated by gres_plugin_job_config_validate()
 * IN/OUT buffer - location to write state to
 */
extern int gres_plugin_job_alloc_pack(List gres_list, Buf buffer,
				      uint16_t protocol_version);

/*
 * Unpack a job's allocated gres information for use by prolog/epilog
 * OUT gres_list - restored state stored by gres_plugin_job_alloc_pack()
 * IN/OUT buffer - location to read state from
 */
extern int gres_plugin_job_alloc_unpack(List *gres_list, Buf buffer,
					uint16_t protocol_version);

/*
 * Build List of information needed to set job's Prolog or Epilog environment
 * variables
 *
 * IN job_gres_list - job's GRES allocation info
 * IN hostlist - list of nodes associated with the job
 * RET information about the job's GRES allocation needed by Prolog or Epilog
 */
extern List gres_plugin_epilog_build_env(List job_gres_list, char *node_list);

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * IN/OUT epilog_env_ptr - environment variable array
 * IN epilog_gres_list - generated by TBD
 * IN node_inx - zero origin node index
 */
extern void gres_plugin_epilog_set_env(char ***epilog_env_ptr,
				       List epilog_gres_list, int node_inx);

/*
 * Given a job's requested GRES configuration, validate it and build a GRES list
 * Note: This function can be used for a new request with gres_list==NULL or
 *	 used to update an existing job, in which case gres_list is a copy
 *	 of the job's original value (so we can clear fields as needed)
 * IN *tres* - job requested gres input string
 * IN/OUT num_tasks - requested task count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT min_nodes - requested minimum node count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT max_nodes - requested maximum node count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT ntasks_per_node - requested tasks_per_node count, may be reset to
 *		      provide consistent gres_per_node/task values
 * IN/OUT ntasks_per_socket - requested ntasks_per_socket count, may be reset to
 *		      provide consistent gres_per_node/task values
 * IN/OUT sockets_per_node - requested sockets_per_node count, may be reset to
 *		      provide consistent gres_per_socket/node values
 * IN/OUT cpus_per_task - requested ntasks_per_socket count, may be reset to
 *		      provide consistent gres_per_task/cpus_per_gres values
 * OUT gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_state_validate(char *cpus_per_tres,
					  char *tres_freq,
					  char *tres_per_job,
					  char *tres_per_node,
					  char *tres_per_socket,
					  char *tres_per_task,
					  char *mem_per_tres,
					  uint32_t *num_tasks,
					  uint32_t *min_nodes,
					  uint32_t *max_nodes,
					  uint16_t *ntasks_per_node,
					  uint16_t *ntasks_per_socket,
					  uint16_t *sockets_per_node,
					  uint16_t *cpus_per_task,
					  List *gres_list);

/*
 * Determine if a job's specified GRES can be supported. This is designed to
 * prevent the running of a job using the GRES options only supported by the
 * select/cons_tres plugin when switching (on slurmctld restart) from the
 * cons_tres plugin to any other select plugin.
 *
 * IN gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_revalidate(List gres_list);

/*
 * Determine if a job's specified GRES are currently valid. This is designed to
 * manage jobs allocated GRES which are either no longer supported or a GRES
 * configured with the "File" option in gres.conf where the count has changed,
 * in which case we don't know how to map the job's old GRES bitmap onto the
 * current GRES bitmaps.
 *
 * IN job_id - ID of job being validated (used for logging)
 * IN job_gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_revalidate2(uint32_t job_id, List job_gres_list,
				       bitstr_t *node_bitmap);

/*
 * Clear GRES allocation info for all job GRES at start of scheduling cycle
 * Return TRUE if any gres_per_job constraints to satisfy
 */
extern bool gres_plugin_job_sched_init(List job_gres_list);

/*
 * Return TRUE if all gres_per_job specifications are satisfied
 */
extern bool gres_plugin_job_sched_test(List job_gres_list, uint32_t job_id);

/*
 * Return TRUE if all gres_per_job specifications will be satisfied with
 *	the addtitional resources provided by a single node
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN job_id - The job being tested
 */
extern bool gres_plugin_job_sched_test2(List job_gres_list, List sock_gres_list,
					uint32_t job_id);

/*
 * Update a job's total_gres counter as we add a node to potential allocation
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN avail_cpus - CPUs currently available on this node
 */
extern void gres_plugin_job_sched_add(List job_gres_list, List sock_gres_list,
				      uint16_t avail_cpus);

/*
 * Create/update List GRES that can be made available on the specified node
 * IN/OUT consec_gres - List of sock_gres_t that can be made available on
 *			a set of nodes
 * IN job_gres_list - List of job's GRES requirements (gres_job_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_plugin_job_sched_consec(List *consec_gres, List job_gres_list,
					 List sock_gres_list);

/*
 * Determine if the additional sock_gres_list resources will result in
 * satisfying the job's gres_per_job constraints
 * IN job_gres_list - job's GRES requirements
 * IN sock_gres_list - available GRES in a set of nodes, data structure built
 *		       by gres_plugin_job_sched_consec()
 */
extern bool gres_plugin_job_sched_sufficient(List job_gres_list,
					     List sock_gres_list);

/*
 * Given a List of sock_gres_t entries, return a string identifying the
 * count of each GRES available on this set of nodes
 * IN sock_gres_list - count of GRES available in this group of nodes
 * IN job_gres_list - job GRES specification, used only to get GRES name/type
 * RET xfree the returned string
 */
extern char *gres_plugin_job_sched_str(List sock_gres_list, List job_gres_list);

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only gres_cnt_alloc, node_cnt and gres_bit_alloc are copied
 *	 Job step details are NOT copied.
 */
extern List gres_plugin_job_state_dup(List gres_list);

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
extern List gres_plugin_job_state_extract(List gres_list, int node_index);

/*
 * Pack a job's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_job_config_validate()
 * IN/OUT buffer - location to write state to
 * IN job_id - job's ID
 * IN details - if set then pack job step allocation details (only needed to
 *	 	save/restore job state, not needed in job credential for
 *		slurmd task binding)
 *
 * NOTE: A job's allocation to steps is not recorded here, but recovered with
 *	 the job step state information upon slurmctld restart.
 */
extern int gres_plugin_job_state_pack(List gres_list, Buf buffer,
				      uint32_t job_id, bool details,
				      uint16_t protocol_version);

/*
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_plugin_job_state_unpack(List *gres_list, Buf buffer,
					uint32_t job_id,
					uint16_t protocol_version);

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES)
 * IN job_gres_list   - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list  - node's gres_list built by
 *                      gres_plugin_node_config_validate()
 * IN use_total_gres  - if set then consider all GRES resources as available,
 *		        and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores (NULL if no restriction)
 * IN core_start_bit  - index into core_bitmap for this node's first cores
 * IN core_end_bit    - index into core_bitmap for this node's last cores
 */
extern void gres_plugin_job_core_filter(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *core_bitmap,
					int core_start_bit, int core_end_bit,
					char *node_name);

/*
 * Determine how many cores on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN core_bitmap    - Identification of available cores (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first core
 * IN core_end_bit   - index into core_bitmap for this node's last core
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres, bitstr_t *core_bitmap,
				     int core_start_bit, int core_end_bit,
				     uint32_t job_id, char *node_name);

/*
 * Determine how many cores on each socket of a node can be used by this job
 * IN job_gres_list   - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list  - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres  - if set then consider all gres resources as available,
 *		        and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores on this node
 * IN sockets         - Count of sockets on the node
 * IN cores_per_sock  - Count of cores per socket on this node
 * IN job_id          - job's ID (for logging)
 * IN node_name       - name of the node (for logging)
 * IN enforce_binding - if true then only use GRES with direct access to cores
 * IN s_p_n           - Expected sockets_per_node (NO_VAL if not limited)
 * OUT req_sock_map   - bitmap of specific requires sockets
 * IN user_id         - job's user ID
 * IN node_inx        - index of node to be evaluated
 * RET: List of sock_gres_t entries identifying what resources are available on
 *	each core. Returns NULL if none available. Call FREE_NULL_LIST() to
 *	release memory.
 */
extern List gres_plugin_job_test2(List job_gres_list, List node_gres_list,
				  bool use_total_gres, bitstr_t *core_bitmap,
				  uint16_t sockets, uint16_t cores_per_sock,
				  uint32_t job_id, char *node_name,
				  bool enforce_binding, uint32_t s_p_n,
				  bitstr_t **req_sock_map, uint32_t user_id,
				  const uint32_t node_inx);

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list  - list of sock_gres_t entries built by gres_plugin_job_test2()
 * IN avail_mem       - memory available for the job
 * IN max_cpus        - maximum CPUs available on this node (limited by
 *                      specialized cores and partition CPUs-per-node)
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN core_bitmap     - Identification of available cores on this node
 * IN sockets         - Count of sockets on the node
 * IN cores_per_sock  - Count of cores per socket on this node
 * IN cpus_per_core   - Count of CPUs per core on this node
 * IN sock_per_node   - sockets requested by job per node or NO_VAL
 * IN task_per_node   - tasks requested by job per node or NO_VAL16
 * OUT avail_gpus     - Count of available GPUs on this node
 * OUT near_gpus      - Count of GPUs available on sockets with available CPUs
 * RET - 0 if job can use this node, -1 otherwise (some GRES limit prevents use)
 */
extern int gres_plugin_job_core_filter2(List sock_gres_list, uint64_t avail_mem,
					uint16_t max_cpus,
					bool enforce_binding,
					bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock,
					uint16_t cpus_per_core,
					uint32_t sock_per_node,
					uint16_t task_per_node,
					uint16_t *avail_gpus,
					uint16_t *near_gpus);

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by gres_plugin_job_test2()
 * IN sockets - Count of sockets on the node
 * IN cores_per_socket - Count of cores per socket on the node
 * IN cpus_per_core - Count of CPUs per core on the node
 * IN avail_cpus - Count of available CPUs on the node, UPDATED
 * IN min_tasks_this_node - Minimum count of tasks that can be started on this
 *                          node, UPDATED
 * IN max_tasks_this_node - Maximum count of tasks that can be started on this
 *                          node or NO_VAL, UPDATED
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN first_pass - set if first scheduling attempt for this job, use
 *		   co-located GRES and cores if possible
 * IN avail_cores - cores available on this node, UPDATED
 */
extern void gres_plugin_job_core_filter3(gres_mc_data_t *mc_ptr,
					 List sock_gres_list,
					 uint16_t sockets,
					 uint16_t cores_per_socket,
					 uint16_t cpus_per_core,
					 uint16_t *avail_cpus,
					 uint32_t *min_tasks_this_node,
					 uint32_t *max_tasks_this_node,
					 int rem_nodes,
					 bool enforce_binding,
					 bool first_pass,
					 bitstr_t *avail_core);

/*
 * Return the maximum number of tasks that can be started on a node with
 * sock_gres_list (per-socket GRES details for some node)
 */
extern uint32_t gres_plugin_get_task_limit(List sock_gres_list);

/*
 * Make final GRES selection for the job
 * sock_gres_list IN - per-socket GRES details, one record per allocated node
 * job_id IN - job ID for logging
 * job_res IN - job resource allocation
 * overcommit IN - job's ability to overcommit resources
 * tres_mc_ptr IN - job's multi-core options
 * node_table_ptr IN - slurmctld's node records
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_core_filter4(List *sock_gres_list, uint32_t job_id,
					struct job_resources *job_res,
					uint8_t overcommit,
					gres_mc_data_t *tres_mc_ptr,
					struct node_record *node_table_ptr);

/*
 * Determine if job GRES specification includes a tres-per-task specification
 * RET TRUE if any GRES requested by the job include a tres-per-task option
 */
extern bool gres_plugin_job_tres_per_task(List job_gres_list);

/*
 * Determine if the job GRES specification includes a mem-per-tres specification
 * RET largest mem-per-tres specification found
 */
extern uint64_t gres_plugin_job_mem_max(List job_gres_list);

/*
 * Set per-node memory limits based upon GRES assignments
 * RET TRUE if mem-per-tres specification used to set memory limits
 */
extern bool gres_plugin_job_mem_set(List job_gres_list,
				    job_resources_t *job_res);

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request (based upon total GRES times cpus_per_gres value)
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * task_count IN - count of tasks in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_plugin_job_min_cpus(uint32_t node_count,
				    uint32_t sockets_per_node,
				    uint32_t task_count,
				    List job_gres_list);

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request on one node
 * sockets_per_node IN - count of sockets per node in job allocation
 * tasks_per_node IN - count of tasks per node in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_plugin_job_min_cpu_node(uint32_t sockets_per_node,
					uint32_t tasks_per_node,
					List job_gres_list);

/*
 * Select and allocate GRES to a job and update node and job GRES information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN user_id     - job's user ID
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list,
				 int node_cnt, int node_index, int node_offset,
				 uint32_t job_id, char *node_name,
				 bitstr_t *core_bitmap, uint32_t user_id);

/* Clear any vestigial job gres state. This may be needed on job requeue. */
extern void gres_plugin_job_clear(List job_gres_list);

/*
 * Deallocate resource from a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		gres_plugin_node_config_validate()
 * IN node_offset - zero-origin index to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN old_job     - true if job started before last slurmctld reboot.
 *		    Immediately after slurmctld restart and before the node's
 *		    registration, the GRES type and topology. This results in
 *		    some incorrect internal bookkeeping, but does not cause
 *		    failures in terms of allocating GRES to jobs.
 * IN user_id     - job's user ID
 * IN: job_fini   - job fully terminating on this node (not just a test)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_dealloc(List job_gres_list, List node_gres_list,
				   int node_offset, uint32_t job_id,
				   char *node_name, bool old_job,
				   uint32_t user_id, bool job_fini);

/*
 * Merge one job's gres allocation into another job's gres allocation.
 * IN from_job_gres_list - List of gres records for the job being merged
 *			into another job
 * IN from_job_node_bitmap - bitmap of nodes for the job being merged into
 *			another job
 * IN/OUT to_job_gres_list - List of gres records for the job being merged
 *			into job
 * IN to_job_node_bitmap - bitmap of nodes for the job being merged into
 */
extern void gres_plugin_job_merge(List from_job_gres_list,
				  bitstr_t *from_job_node_bitmap,
				  List to_job_gres_list,
				  bitstr_t *to_job_node_bitmap);

/*
 * Set environment variables as required for a batch job
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_plugin_job_alloc()
 * IN node_inx - zero origin node index
 */
extern void gres_plugin_job_set_env(char ***job_env_ptr, List job_gres_list,
				    int node_inx);

/*
 * Set job default parameters in a given element of a list
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of gres, apply defaults to all elements (e.g. updates to
 *		  gres_name="gpu" would apply to "gpu:tesla", "gpu:volta", etc.)
 * IN cpu_per_gpu - value to set as default
 * IN mem_per_gpu - value to set as default
 */
extern void gres_plugin_job_set_defs(List job_gres_list, char *gres_name,
				     uint64_t cpu_per_gpu,
				     uint64_t mem_per_gpu);

/*
 * Extract from the job record's gres_list the count of allocated resources of
 * 	the named gres type.
 * IN job_gres_list  - job record's gres_list.
 * IN gres_name_type - the name of the gres type to retrieve the associated
 *	value from.
 * RET The value associated with the gres type or NO_VAL if not found.
 */
extern uint64_t gres_plugin_get_job_value_by_type(List job_gres_list,
						  char *gres_name_type);

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_plugin_job_state_validate()
 * IN job_id    - job's ID
 */
extern void gres_plugin_job_state_log(List gres_list, uint32_t job_id);

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN *tres* - step's request's gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list - List of Gres records for this job
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_step_state_validate(char *cpus_per_tres,
					   char *tres_per_step,
					   char *tres_per_node,
					   char *tres_per_socket,
					   char *tres_per_task,
					   char *mem_per_tres,
					   List *step_gres_list,
					   List job_gres_list, uint32_t job_id,
					   uint32_t step_id);

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_dup(List gres_list);

/*
 * Create a copy of a step's gres state for a particular node index
 * IN gres_list - List of Gres records for this step to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_extract(List gres_list, int node_index);

/*
 * A job allocation size has changed. Update the job step gres information
 * bitmaps and other data structures.
 * IN gres_list - List of Gres records for this step to track usage
 * IN orig_job_node_bitmap - bitmap of nodes in the original job allocation
 * IN new_job_node_bitmap - bitmap of nodes in the new job allocation
 */
void gres_plugin_step_state_rebase(List gres_list,
				   bitstr_t *orig_job_node_bitmap,
				   bitstr_t *new_job_node_bitmap);

/*
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_step_allocate()
 * IN/OUT buffer - location to write state to
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_pack(List gres_list, Buf buffer,
				       uint32_t job_id, uint32_t step_id,
				       uint16_t protocol_version);

/*
 * Unpack a step's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_unpack(List *gres_list, Buf buffer,
					 uint32_t job_id, uint32_t step_id,
					 uint16_t protocol_version);

/* Return the count of GRES of a specific name on this machine
 * IN step_gres_list - generated by gres_plugin_step_allocate()
 * IN gres_name - name of the GRES to match
 * RET count of GRES of this specific name available to the job or NO_VAL64
 */
extern uint64_t gres_plugin_step_count(List step_gres_list, char *gres_name);

/*
 * Configure the GRES hardware allocated to the current step while privileged
 *
 * IN step_gres_list - Step's GRES specification
 * IN node_id        - relative position of this node in step
 * IN settings       - string containing configuration settings for the hardware
 */
extern void gres_plugin_step_hardware_init(List step_gres_list,
					   uint32_t node_id, char *settings);

/*
 * Optionally undo GRES hardware configuration while privileged
 */
extern void gres_plugin_step_hardware_fini(void);

/*
 * Set environment as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_plugin_step_alloc()
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN tres_bind - TRES binding directives (new format, a string)
 * IN tres_freq - TRES power management directives
 * IN local_proc_id - task rank, local to this compute node only
 */
extern void gres_plugin_step_set_env(char ***job_env_ptr, List step_gres_list,
				     uint16_t accel_bind_type, char *tres_bind,
				     char *tres_freq, int local_proc_id);

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_plugin_step_allocate()
 * IN job_id - job's ID
 */
extern void gres_plugin_step_state_log(List gres_list, uint32_t job_id,
				       uint32_t step_id);

/*
 * Determine how many cores of a job's allocation can be allocated to a step
 *	on a specific node
 * IN job_gres_list - a running job's gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN first_step_node - true if this is node zero of the step (do initialization)
 * IN cpus_per_task - number of CPUs required per task
 * IN max_rem_nodes - maximum nodes remaining for step (including this one)
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * RET Count of available cores on this node (sort of):
 *     NO_VAL64 if no limit or 0 if node is not usable
 */
extern uint64_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool first_step_node,
				      uint16_t cpus_per_task, int max_rem_nodes,
				      bool ignore_alloc,
				      uint32_t job_id, uint32_t step_id);

/*
 * Allocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_offset - job's zero-origin index to the node of interest
 * IN first_step_node - true if this is node zero of the step (do initialization)
 * IN tasks_on_node - number of tasks to be launched on this node
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, bool first_step_node,
				  uint16_t tasks_on_node, uint32_t rem_nodes,
				  uint32_t job_id, uint32_t step_id);

/*
 * Deallocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_dealloc(List step_gres_list, List job_gres_list,
				    uint32_t job_id, uint32_t step_id);

/*
 * Build a string containing the GRES details for a given node and socket
 * sock_gres_list IN - List of sock_gres_t entries
 * sock_inx IN - zero-origin socket for which information is to be returned
 * RET string, must call xfree() to release memory
 */
extern char *gres_plugin_sock_str(List sock_gres_list, int sock_inx);

/*
 * Map a given GRES type ID back to a GRES type name.
 * gres_id IN - GRES type ID to search for.
 * gres_name IN - Pre-allocated string in which to store the GRES type name.
 * gres_name_len - Size of gres_name in bytes
 * RET - error code (currently not used--always return SLURM_SUCCESS)
 */
extern int gres_gresid_to_gresname(uint32_t gres_id, char* gres_name,
				   int gres_name_len);

/*
 * Determine total count GRES of a given type are allocated to a job across
 * all nodes
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of a GRES type
 * RET count of this GRES allocated to this job
 */
extern uint64_t gres_get_value_by_type(List job_gres_list, char *gres_name);

enum gres_job_data_type {
	GRES_JOB_DATA_COUNT,	/* data-> uint64_t  */
	GRES_JOB_DATA_BITMAP,	/* data-> bitstr_t* */
};

/*
 * get data from a job's GRES data structure
 * IN job_gres_list  - job's GRES data structure
 * IN gres_name - name of a GRES type
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired
 * IN data_type - type of data to get from the job's data
 * OUT data - pointer to the data from job's GRES data structure
 *            DO NOT FREE: This is a pointer into the job's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_get_job_info(List job_gres_list, char *gres_name,
			     uint32_t node_inx,
			     enum gres_job_data_type data_type, void *data);

/* Given a job's GRES data structure, return the indecies for selected elements
 * IN job_gres_list  - job's GRES data structure
 * OUT gres_detail_cnt - Number of elements (nodes) in gres_detail_str
 * OUT gres_detail_str - Description of GRES on each node
 */
extern void gres_build_job_details(List job_gres_list,
				   uint32_t *gres_detail_cnt,
				   char ***gres_detail_str);

enum gres_step_data_type {
	GRES_STEP_DATA_COUNT,	/* data-> uint64_t  */
	GRES_STEP_DATA_BITMAP,	/* data-> bitstr_t* */
};

/*
 * get data from a step's GRES data structure
 * IN step_gres_list  - step's GRES data structure
 * IN gres_name - name of a GRES type
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired. Note this can differ from the step's
 *	node allocation index.
 * IN data_type - type of data to get from the step's data
 * OUT data - pointer to the data from step's GRES data structure
 *            DO NOT FREE: This is a pointer into the step's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_get_step_info(List step_gres_list, char *gres_name,
			      uint32_t node_inx,
			      enum gres_step_data_type data_type, void *data);

extern gres_job_state_t *gres_get_job_state(List gres_list, char *name);
extern gres_step_state_t *gres_get_step_state(List gres_list, char *name);

extern uint32_t gres_get_autodetect_types(void);

/*
 * Translate a gres_list into a tres_str
 * IN gres_list - filled in with gres_job_state_t or gres_step_state_t's
 * IN is_job - if is job function expects gres_job_state_t's else
 *             gres_step_state_t's
 * IN locked - if the assoc_mgr tres read locked is locked or not
 * RET char * in a simple TRES format
 */
extern char *gres_2_tres_str(List gres_list, bool is_job, bool locked);

/* Fill in the job allocated tres_cnt based off the gres_list and node_cnt
 * IN gres_list - filled in with gres_job_state_t's
 * IN node_cnt - number of nodes in the job
 * OUT tres_cnt - gres spots filled in with total number of TRES
 *                requested for job that are requested in gres_list
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void gres_set_job_tres_cnt(List gres_list,
				  uint32_t node_cnt,
				  uint64_t *tres_cnt,
				  bool locked);

/* Fill in the node allocated tres_cnt based off the gres_list
 * IN gres_list - filled in with gres_node_state_t's gres_alloc_cnt
 * OUT tres_cnt - gres spots filled in with total number of TRES
 *                allocated on node
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void gres_set_node_tres_cnt(List gres_list, uint64_t *tres_cnt,
				   bool locked);

/* return the major info from a given path of a device */
extern char *gres_device_major(char *dev_path);

/* Free memory for gres_device_t record */
extern void destroy_gres_device(void *gres_device_ptr);

/* Destroy a gres_slurmd_conf_t record, free it's memory */
extern void destroy_gres_slurmd_conf(void *x);

/*
 * Convert GRES config_flags to a string. The pointer returned references local
 * storage in this function, which is not re-entrant.
 */
extern char *gres_flags2str(uint8_t config_flags);

/*
 * Creates a gres_slurmd_conf_t record to add to a list of gres_slurmd_conf_t
 * records
 */
extern void add_gres_to_list(List gres_list, char *name, uint64_t device_cnt,
			     int cpu_cnt, char *cpu_aff_abs_range,
			     char *device_file, char *type, char *links);

#endif /* !_GRES_H */
