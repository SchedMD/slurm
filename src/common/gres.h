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

typedef enum {
	GRES_INTERNAL_FLAG_NONE = 0,
	GRES_INTERNAL_FLAG_VERBOSE = 1 << 0,
	/*
	 * If a job/step/task has sharing GRES (GPU), don't let shared GRES
	 * (MPS, Shard) clear that env.
	 */
	GRES_INTERNAL_FLAG_PROTECT_ENV = 2 << 0,
} gres_internal_flags_t;

typedef enum {
	DEV_TYPE_NONE,
	DEV_TYPE_BLOCK,
	DEV_TYPE_CHAR,
} gres_device_type_t;

typedef struct {
	uint32_t major;
	uint32_t minor;
	gres_device_type_t type;
} gres_device_id_t;

typedef struct {
	int index; /* GRES bitmap index */
	int alloc;
	gres_device_id_t dev_desc;
	int dev_num; /* Number at the end of the device filename */
	char *path;
	char *unique_id; /* Used for GPU binding with MIGs */
} gres_device_t;

#define GRES_CONF_HAS_MULT   SLURM_BIT(0) /* MultipleFiles is configured */
#define GRES_CONF_HAS_FILE   SLURM_BIT(1) /* File/MultipleFiles is configured */
#define GRES_CONF_HAS_TYPE   SLURM_BIT(2) /* Type= is configured */
#define GRES_CONF_COUNT_ONLY SLURM_BIT(3) /* GRES lacks plugin to load */
#define GRES_CONF_LOADED     SLURM_BIT(4) /* used to avoid loading a plugin
					   * multiple times */
#define GRES_CONF_ENV_NVML   SLURM_BIT(5) /* Set CUDA_VISIBLE_DEVICES */
#define GRES_CONF_ENV_RSMI   SLURM_BIT(6) /* Set ROCR_VISIBLE_DEVICES */
#define GRES_CONF_ENV_OPENCL SLURM_BIT(7) /* Set GPU_DEVICE_ORDINAL */
#define GRES_CONF_ENV_DEF    SLURM_BIT(8) /* Env flags were set to defaults */

#define GRES_CONF_SHARED     SLURM_BIT(9) /* Treat this as a shared GRES */
#define GRES_CONF_ONE_SHARING SLURM_BIT(10) /* Only allow use of a shared GRES
					     * on one of the sharing GRES */

#define GRES_CONF_ENV_ONEAPI SLURM_BIT(11) /* Set ZE_AFFINITY_MASK */

#define GRES_CONF_ENV_SET    0x000008E0   /* Easy check if any of
					   * GRES_CONF_ENV_* are set. */

#define GRES_NO_CONSUME		0x0001	/* Requesting no consume of resources */

/* GRES AutoDetect options */
#define GRES_AUTODETECT_UNSET     0x00000000 /* Not set */
#define GRES_AUTODETECT_GPU_NVML  0x00000001
#define GRES_AUTODETECT_GPU_RSMI  0x00000002
#define GRES_AUTODETECT_GPU_OFF   0x00000004 /* Do NOT use global */
#define GRES_AUTODETECT_GPU_ONEAPI 0x00000008

#define GRES_AUTODETECT_GPU_FLAGS 0x000000ff /* reserve first 8 bits for gpu
					      * flags */

typedef enum {
	GRES_STATE_SRC_STATE_PTR,
	GRES_STATE_SRC_CONTEXT_PTR,
	GRES_STATE_SRC_KEY_PTR,
} gres_state_src_t;

typedef struct gres_search_key {
	uint32_t config_flags;	/* See GRES_CONF_* values above */
	int node_offset;
	uint32_t plugin_id;
	uint32_t type_id;
} gres_key_t;

/* Gres state information gathered by slurmd daemon */
typedef struct gres_slurmd_conf {
	uint32_t config_flags;	/* See GRES_CONF_* values above */

	/* Count of gres available in this configuration record */
	uint64_t count;

	/* The # of CPUs on the node */
	uint32_t cpu_cnt;
	/* abstract/logical mapping range of cores */
	char *cpus;
	/* machine/local/physical CPU mapping */
	bitstr_t *cpus_bitmap;

	/* Device file associated with this configuration record */
	char *file;

	/* Comma-separated list of communication link IDs (numbers) */
	char *links;

	/* Name of this gres */
	char *name;

	/* Type of this GRES (e.g. model name) */
	char *type_name;

	/* Used for GPU binding with MIGs */
	char *unique_id;

	/* GRES ID number */
	uint32_t plugin_id;
} gres_slurmd_conf_t;


/* Extra data and functions to be passed in to the node_config_load() */
typedef struct {
	/* How many CPUs there are configured on the node */
	uint32_t cpu_cnt;
	/* True if called in the slurmd */
	bool in_slurmd;
	/* A pointer to the mac_to_abs function */
	int (*xcpuinfo_mac_to_abs) (char *mac, char **abs);
} node_config_load_t;

/* Current GRES state information managed by slurmctld daemon */
typedef struct gres_node_state {
	struct gres_node_state *alt_gres_ns;
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
	 * gres_cnt_found or gres_cnt_config, depending upon config_overrides
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
	uint16_t ntasks_per_gres;

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
	 * These are also not used after allocation and should not be used when
	 * restoring state after a slurmctld restart.
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

typedef enum {
	GRES_STATE_TYPE_UNSET = 0,
	GRES_STATE_TYPE_NODE,
	GRES_STATE_TYPE_JOB,
	GRES_STATE_TYPE_STEP
} gres_state_type_enum_t;

/* Generic gres data structure for adding to a list. Depending upon the
 * context, gres_data points to gres_node_state_t, gres_job_state_t or
 * gres_step_state_t */
typedef struct gres_state {
	uint32_t config_flags;	/* See GRES_CONF_* values above */
	uint32_t plugin_id;
	void *gres_data;
	char *gres_name;		/* GRES name (e.g. "gpu") */
	gres_state_type_enum_t state_type;
} gres_state_t;

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
	uint16_t ntasks_per_gres;

	/*
	 * Allocated resources details
	 *
	 * NOTE: node_cnt and the size of node_in_use and gres_bit_alloc are
	 * identical to that of the job for simplicity. Bits in node_in_use
	 * are set for those node of the job that are used by this step and
	 * gres_bit_alloc are also set if the job's gres_bit_alloc is set
	 * gres_cnt_node_alloc is an array the same size as the number of nodes
	 * in the job because node_cnt is the same as the job.
	 */
	uint64_t total_gres;		/* allocated GRES for this step */
	uint64_t gross_gres;		/* used during the scheduling phase,
					 * GRES that could be available for this
					 * step if no other steps active */
	uint64_t *gres_cnt_node_alloc;	/* Per node GRES allocated,
					 * Used with and without GRES files */
	uint32_t node_cnt;
	bitstr_t *node_in_use;
	bitstr_t **gres_bit_alloc;	/* Used with GRES files */
} gres_step_state_t;

/* Per-socket GRES availability information for scheduling purposes */
typedef struct sock_gres {	/* GRES availability by socket */
	bitstr_t *bits_any_sock;/* GRES bitmap unconstrained by cores */
	bitstr_t **bits_by_sock;/* Per-socket GRES bitmap of this name & type */
	uint64_t cnt_any_sock;	/* GRES count unconstrained by cores */
	uint64_t *cnt_by_sock;	/* Per-socket GRES count of this name & type */
	gres_state_t *gres_state_job; /* Pointer to job info, for limits */
	gres_state_t *gres_state_node; /* Pointer to node info, for state */
	uint64_t max_node_gres;	/* Maximum GRES permitted on this node */
	int sock_cnt;		/* Socket count, size of bits_by_sock and
				 * cnt_by_sock arrays */
	uint64_t total_cnt;	/* Total GRES count of this name & type */
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
	uint16_t ntasks_per_tres; /* number of tasks that can access each gpu */
	uint8_t overcommit;         /* processors being over subscribed */
	uint16_t plane_size;        /* plane size for SLURM_DIST_PLANE */
	uint32_t task_dist;         /* task distribution directives */
	uint8_t whole_node;         /* allocate entire node */
} gres_mc_data_t;

/*
 * Initialize the GRES plugins.
 *
 * Returns a Slurm errno.
 */
extern int gres_init(void);

/*
 * Terminate the GRES plugins. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int gres_fini(void);

/*
**************************************************************************
*                          P L U G I N   C A L L S                       *
**************************************************************************
*/

/*
 * Perform reconfig, re-read any configuration files
 * OUT did_change - set if gres configuration changed
 */
extern int gres_reconfig(void);

/*
 * Return a plugin-specific help message for salloc, sbatch and srun
 * Result must be xfree()'d
 */
extern char *gres_help_msg(void);

/*
 * Convert a GRES name or model into a number for faster comparison operations
 * IN name - GRES name or model
 * RET - An int representing a custom hash of the name
 */
extern uint32_t gres_build_id(char *name);

/*
 * Takes a GRES config line (typically from slurm.conf) and remove any
 * records for GRES which are not defined in GresTypes.
 * RET string of valid GRES, Release memory using xfree()
 */
extern char *gres_name_filter(char *orig_gres, char *nodes);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMD DAEMON                         *
 **************************************************************************
 */
/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs configured for node node_name.
 * IN node_name - Name of the node to load the GRES config for.
 * IN gres_list - Node's GRES information as loaded from slurm.conf by slurmd
 * IN xcpuinfo_abs_to_mac - Pointer to xcpuinfo_abs_to_mac() funct. If
 *	specified, Slurm will convert gres_slurmd_conf->cpus_bitmap (a bitmap
 *	derived from gres.conf's "Cores" range string) into machine format
 *	(normal slrumd/stepd operation). If not, it will remain unconverted (for
 *	testing purposes or when unused).
 * IN xcpuinfo_mac_to_abs - Pointer to xcpuinfo_mac_to_abs() funct. Used to
 *	convert CPU affinities from machine format (as collected from NVML and
 *	others) into abstract format, for sanity checking purposes.
 * NOTE: Called from slurmd (and from slurmctld for each cloud node)
 */
extern int gres_g_node_config_load(uint32_t cpu_cnt, char *node_name,
				   List gres_list,
				   void *xcpuinfo_abs_to_mac,
				   void *xcpuinfo_mac_to_abs);

/*
 * Set GRES devices as allocated or not for a particular job
 * IN gres_list - allocated gres devices
 * IN is_job - if is job function expects gres_job_state_t's else
 *             gres_step_state_t's
 * RET - List of gres_device_t containing all devices from all GRES with alloc
 *       set correctly if the device is allocated to the job/step.
 */
extern List gres_g_get_devices(List gres_list, bool is_job,
			       uint16_t accel_bind_type, char *tres_bind_str,
			       int local_proc_id, pid_t pid);

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_g_send_stepd(int fd, slurm_msg_t *msg);

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_g_recv_stepd(int fd, slurm_msg_t *msg);

/*
 * Pack this node's gres configuration into a buffer
 * IN/OUT buffer - message buffer to pack
 */
extern int gres_node_config_pack(buf_t *buffer);

/*
**************************************************************************
*                 PLUGIN CALLS FOR SLURMCTLD DAEMON                      *
**************************************************************************
*/
/*
 * Build a node's gres record based only upon the slurm.conf contents
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 */
extern int gres_init_node_config(char *orig_config, List *gres_list);

/*
 * Return how many gres Names are on the system.
 */
extern int gres_get_gres_cnt(void);

/* Add a GRES record. This is used by the node_features plugin after the
 * slurm.conf file is read and the initial GRES records are built by
 * gres_init(). */
extern void gres_add(char *gres_name);

/*
 * Unpack this node's configuration from a buffer (built/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_node_config_unpack(buf_t *buffer, char *node_name);

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from merged slurm.conf/gres.conf
 * IN/OUT new_config - Updated gres info from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN threads_per_core - Count of CPUs (threads) per core on this node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN sock_cnt - Count of sockets on this node
 * IN config_overrides - true: Don't validate hardware, use slurm.conf
 *                             configuration
 *		         false: Validate hardware config, but use slurm.conf
 *                              config
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_node_config_validate(char *node_name,
				     char *orig_config,
				     char **new_config,
				     List *gres_list,
				     int threads_per_core,
				     int cores_per_sock, int sock_cnt,
				     bool config_overrides,
				     char **reason_down);

/*
 * Add a GRES from node_feature plugin
 * IN node_name - name of the node for which the gres information applies
 * IN gres_name - name of the GRES being added or updated from the plugin
 * IN gres_size - count of this GRES on this node
 * IN/OUT new_config - Updated GRES info from slurm.conf
 * IN/OUT gres_list - List of GRES records for this node to track usage
 */
extern void gres_node_feature(char *node_name,
			      char *gres_name, uint64_t gres_size,
			      char **new_config, List *gres_list);

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN new_gres - Updated GRES information supplied from slurm.conf or scontrol
 * IN/OUT gres_str - Node's current GRES string, updated as needed
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN config_overrides - true: Don't validate hardware, use slurm.conf
 *                             configuration
 *		         false: Validate hardware config, but use slurm.conf
 *                              config
 * IN cores_per_sock - Number of cores per socket on this node
 * IN sock_per_node - Total count of sockets on this node (on any board)
 */
extern int gres_node_reconfig(char *node_name,
			      char *new_gres,
			      char **gres_str,
			      List *gres_list,
			      bool config_overrides,
			      int cores_per_sock,
			      int sock_per_node);

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_node_config_validate()
 * IN/OUT buffer - location to write state to
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_node_state_pack(List gres_list, buf_t *buffer,
				char *node_name);
/*
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_node_state_unpack(List *gres_list, buf_t *buffer,
				  char *node_name,
				  uint16_t protocol_version);

/*
 * Duplicate a node gres status (used for will-run logic)
 * IN gres_list - node gres state information
 * RET a copy of gres_list or NULL on failure
 */
extern List gres_node_state_list_dup(List gres_list);

/* Copy gres_job_state_t record for ALL nodes */
extern void *gres_job_state_dup(gres_job_state_t *gres_js);

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_node_state_dealloc_all(List gres_list);

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_node_state_log(List gres_list, char *node_name);

/* Return true if any node_state_t has gres_cnt_alloc greater than 0 */
extern bool gres_node_state_list_has_alloc_gres(List gres_list);

/*
 * Build a string indicating a node's drained GRES
 * IN gres_list - generated by gres_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_drain(List gres_list);

/*
 * Build a string indicating a node's used GRES
 * IN gres_list - generated by gres_node_config_validate()
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
extern uint64_t gres_node_config_cnt(List gres_list, char *name);

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
extern int gres_node_count(List gres_list, int arr_len,
			   uint32_t *gres_count_ids,
			   uint64_t *gres_count_vals,
			   int val_type);

/*
 * Pack a job's allocated gres information for use by prolog/epilog
 * IN gres_list - generated by gres_job_config_validate()
 * IN/OUT buffer - location to write state to
 */
extern int gres_job_alloc_pack(List gres_list, buf_t *buffer,
			       uint16_t protocol_version);

/*
 * Unpack a job's allocated gres information for use by prolog/epilog
 * OUT gres_list - restored state stored by gres_job_alloc_pack()
 * IN/OUT buffer - location to read state from
 */
extern int gres_job_alloc_unpack(List *gres_list, buf_t *buffer,
				 uint16_t protocol_version);

/*
 * Build List of information needed to set job's Prolog or Epilog environment
 * variables
 *
 * IN job_gres_list - job's GRES allocation info
 * IN hostlist - list of nodes associated with the job
 * RET information about the job's GRES allocation needed by Prolog or Epilog
 */
extern List gres_g_epilog_build_env(List job_gres_list, char *node_list);

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * IN/OUT epilog_env_ptr - environment variable array
 * IN epilog_gres_list - generated by TBD
 * IN node_inx - zero origin node index
 */
extern void gres_g_epilog_set_env(char ***epilog_env_ptr,
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
 * IN/OUT ntasks_per_tres - requested ntasks_per_tres count
 * OUT gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_job_state_validate(char *cpus_per_tres,
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
				   uint16_t *ntasks_per_tres,
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
extern int gres_job_revalidate(List gres_list);

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
extern int gres_job_revalidate2(uint32_t job_id, List job_gres_list,
				bitstr_t *node_bitmap);

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only gres_cnt_alloc, node_cnt and gres_bit_alloc are copied
 *	 Job step details are NOT copied.
 */
extern List gres_job_state_list_dup(List gres_list);

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
extern List gres_job_state_extract(List gres_list, int node_index);

/*
 * Pack a job's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_job_config_validate()
 * IN/OUT buffer - location to write state to
 * IN job_id - job's ID
 * IN details - if set then pack job step allocation details (only needed to
 *	 	save/restore job state, not needed in job credential for
 *		slurmd task binding)
 *
 * NOTE: A job's allocation to steps is not recorded here, but recovered with
 *	 the job step state information upon slurmctld restart.
 */
extern int gres_job_state_pack(List gres_list, buf_t *buffer,
			       uint32_t job_id, bool details,
			       uint16_t protocol_version);

/*
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_job_state_unpack(List *gres_list, buf_t *buffer,
				 uint32_t job_id,
				 uint16_t protocol_version);

/*
 * Determine how many cores on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN core_bitmap    - Identification of available cores (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first core
 * IN core_end_bit   - index into core_bitmap for this node's last core
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN disable binding- --gres-flags=disable-binding
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_job_test(List job_gres_list, List node_gres_list,
			      bool use_total_gres, bitstr_t *core_bitmap,
			      int core_start_bit, int core_end_bit,
			      uint32_t job_id, char *node_name,
			      bool disable_binding);

/*
 * Set environment variables as required for a batch job
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_job_alloc()
 * IN node_inx - zero origin node index
 */
extern void gres_g_job_set_env(char ***job_env_ptr, List job_gres_list,
			       int node_inx);

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_job_state_validate()
 * IN job_id    - job's ID
 */
extern void gres_job_state_log(List gres_list, uint32_t job_id);

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN *tres* - step's request's gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list_req - List of requested gres for this job
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_step_state_validate(char *cpus_per_tres,
				    char *tres_per_step,
				    char *tres_per_node,
				    char *tres_per_socket,
				    char *tres_per_task,
				    char *mem_per_tres,
				    uint16_t ntasks_per_tres,
				    uint32_t step_min_nodes,
				    List *step_gres_list,
				    List job_gres_list_req, uint32_t job_id,
				    uint32_t step_id,
				    uint32_t *num_tasks,
				    uint32_t *cpu_count, char **err_msg);

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_step_state_list_dup(List gres_list);

/*
 * Create a copy of a step's gres state for a particular node index
 * IN gres_list - List of Gres records for this step to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_step_state_extract(List gres_list, int node_index);

/*
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_ctld_step_alloc()
 * IN/OUT buffer - location to write state to
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_pack(List gres_list, buf_t *buffer,
				slurm_step_id_t *step_id,
				uint16_t protocol_version);

/*
 * Unpack a step's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_unpack(List *gres_list, buf_t *buffer,
				  slurm_step_id_t *step_id,
				  uint16_t protocol_version);

/* Return the count of GRES of a specific name on this machine
 * IN step_gres_list - generated by gres_ctld_step_alloc()
 * IN gres_name - name of the GRES to match
 * RET count of GRES of this specific name available to the job or NO_VAL64
 */
extern uint64_t gres_step_count(List step_gres_list, char *gres_name);

/*
 * Configure the GRES hardware allocated to the current step while privileged
 *
 * IN step_gres_list - Step's GRES specification
 * IN node_id        - relative position of this node in step
 * IN settings       - string containing configuration settings for the hardware
 */
extern void gres_g_step_hardware_init(List step_gres_list,
				      uint32_t node_id, char *settings);

/*
 * Optionally undo GRES hardware configuration while privileged
 */
extern void gres_g_step_hardware_fini(void);

/*
 * Set environment as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_step_alloc()
 */
extern void gres_g_step_set_env(char ***job_env_ptr, List step_gres_list);

/*
 * Change the task's inherited environment (from the step, and set by
 * gres_g_step_set_env()). Use this to implement GPU task binding.
 *
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_ctld_step_alloc()
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN tres_bind - TRES binding directives (new format, a string)
 * IN local_proc_id - task rank, local to this compute node only
 */
extern void gres_g_task_set_env(char ***job_env_ptr, List step_gres_list,
				uint16_t accel_bind_type, char *tres_bind,
				int local_proc_id);

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_ctld_step_alloc()
 * IN job_id - job's ID
 * IN step_id - step's ID
 */
extern void gres_step_state_log(List gres_list, uint32_t job_id,
				uint32_t step_id);

/*
 * Determine how many cores of a job's allocation can be allocated to a step
 *	on a specific node
 * IN job_gres_list - a running job's allocated gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN first_step_node - true if this is node zero of the step (do initialization)
 * IN cpus_per_task - number of CPUs required per task
 * IN max_rem_nodes - maximum nodes remaining for step (including this one)
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * IN test_mem - true if we should test if mem_per_gres would exceed a limit.
 * IN job_resrcs_ptr - pointer to this job's job_resources_t; used to know
 *                     how much of the job's memory is available.
 * OUT err_code - If an error occurred, set this to tell the caller why the
 *                error happend.
 * RET Count of available cores on this node (sort of):
 *     NO_VAL64 if no limit or 0 if node is not usable
 */
extern uint64_t gres_step_test(List step_gres_list, List job_gres_list,
			       int node_offset, bool first_step_node,
			       uint16_t cpus_per_task, int max_rem_nodes,
			       bool ignore_alloc,
			       uint32_t job_id, uint32_t step_id,
			       bool test_mem, job_resources_t *job_resrcs_ptr,
			       int *err_code);

/*
 * Build a string containing the GRES details for a given node and socket
 * sock_gres_list IN - List of sock_gres_t entries
 * sock_inx IN - zero-origin socket for which information is to be returned
 * RET string, must call xfree() to release memory
 */
extern char *gres_sock_str(List sock_gres_list, int sock_inx);

/*
 * Determine total count GRES of a given type are allocated to a job across
 * all nodes
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
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

extern uint32_t gres_get_autodetect_flags(void);

/* Convert the major/minor info to a string */
extern char *gres_device_id2str(gres_device_id_t *gres_dev);

/* Free memory for gres_device_t record */
extern void destroy_gres_device(void *gres_device_ptr);

/* Destroy a gres_slurmd_conf_t record, free it's memory */
extern void destroy_gres_slurmd_conf(void *x);

/*
 * Convert GRES config_flags to a string. The pointer returned references local
 * storage in this function, which is not re-entrant.
 */
extern char *gres_flags2str(uint32_t config_flags);

/*
 * Parse a gres.conf Flags string
 */
extern uint32_t gres_flags_parse(char *input, bool *no_gpu_env,
				 bool *sharing_mentioned);

/*
 * Creates a gres_slurmd_conf_t record to add to a list of gres_slurmd_conf_t
 * records
 */
extern void add_gres_to_list(List gres_list, char *name, uint64_t device_cnt,
			     int cpu_cnt, char *cpu_aff_abs_range,
			     bitstr_t *cpu_aff_mac_bitstr, char *device_file,
			     char *type, char *links, char *unique_id,
			     uint32_t flags);

extern int gres_find_id(void *x, void *key);

extern int gres_find_flags(void *x, void *key);

/* Find job record with matching name and type */
extern int gres_find_job_by_key_exact_type(void *x, void *key);

/* Find job record with matching name and type */
extern int gres_find_job_by_key(void *x, void *key);

/* Find job record with matching name and type */
extern int gres_find_job_by_key_with_cnt(void *x, void *key);

/* Find step record with matching name and type */
extern int gres_find_step_by_key(void *x, void *key);

/*
 * Find a sock_gres_t record in a list by matching the plugin_id and type_id
 *	from a gres_state_t job record
 * IN x - a sock_gres_t record to test
 * IN key - the gres_state_t record (from a job) we want to match
 * RET 1 on match, otherwise 0
 */
extern int gres_find_sock_by_job_state(void *x, void *key);

/*
 * Test if GRES env variables should be set to global device ID or a device
 * ID that always starts at zero (based upon what the application can see).
 * RET true if TaskPlugin in slurm.conf contains `cgroup` (task/cgroup) AND
 * ConstrainDevices=yes in cgroup.conf.
 */
extern bool gres_use_local_device_index(void);

/* create a gres_state_t */
extern gres_state_t *gres_create_state(void *src_ptr,
				       gres_state_src_t state_src,
				       gres_state_type_enum_t state_type,
				       void *gres_data);

extern void gres_job_list_delete(void *list_element);

extern void gres_step_list_delete(void *list_element);

extern void gres_sock_delete(void *x);

/*
 * Given a tres_cnt array of size slurmctld_tres_cnt reset all GRES counters to
 * 0.
 * IN/OUT tres_cnt - gres spots zeroed out
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void gres_clear_tres_cnt(uint64_t *tres_cnt, bool locked);

/*
 * Return TRUE if this plugin ID consumes GRES count > 1 for a single device
 * file (e.g. MPS)
 */
extern bool gres_id_shared(uint32_t config_flags);

/*
 * Return TRUE if this plugin ID shares resources with another GRES that
 * consumes subsets of its resources (e.g. GPU)
 */
extern bool gres_id_sharing(uint32_t plugin_id);

extern void gres_add_type(char *type, gres_node_state_t *gres_ns,
			  uint64_t tmp_gres_cnt);

extern void gres_validate_node_cores(gres_node_state_t *gres_ns,
				     int cores_ctld, char *node_name);

/*
 * Prepend "gres:" to each gres for proper parsing with TRES functions
 * caller must xfree result.
 */
extern char *gres_prepend_tres_type(const char *gres_str);

/*
 * Create and return a comma-separated zeroed-out links string with a -1 in the
 * given GPU position indicated by index. Caller must xfree() the returned
 * string.
 *
 * Used to record the enumeration order (PCI bus ID order) of GPUs for sorting,
 * even when the GPU does not support nvlinks. E.g. for three total GPUs, their
 * links strings would look like this:
 *
 * GPU at index 0: -1,0,0
 * GPU at index 1: 0,-1,0
 * GPU at index 2: 0,0,-1
 */
extern char *gres_links_create_empty(unsigned int index,
				     unsigned int device_count);

/*
 * Check that we have a comma-delimited list of numbers, and return the index of
 * the GPU (-1) in the links string.
 *
 * Returns a non-zero-based index of the GPU in the links string, if found.
 * If not found, returns a negative value.
 * Return values:
 * 0+: GPU index
 * -1: links string is NULL.
 * -2: links string is not NULL, but is invalid. Possible invalid reasons:
 *     * error parsing the comma-delimited links string
 *     * links string is an empty string
 *     * the 'self' GPU identifier isn't found (i.e. no -1)
 *     * there is more than one 'self' GPU identifier found
 */
extern int gres_links_validate(char *links);

/* Determine if the gres should only allow allocation on the busy one */
extern bool gres_use_busy_dev(gres_state_t *gres_state_node,
			      bool use_total_gres);

/*
 * Dummy reading of gres.conf without loading data.
 * Meant to be used by slurmctld to discover Include files and append them
 * to conf_includes_list for configless files push.
 */
extern void gres_parse_config_dummy(void);

#endif /* !_GRES_H */
