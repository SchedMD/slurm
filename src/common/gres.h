/*****************************************************************************\
 *  gres.h - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _GRES_H
#define _GRES_H

#include "slurm/slurm.h"
#include "src/common/bitstring.h"
#include "src/common/pack.h"

#define GRES_MAGIC 0x438a34d4


enum {
	GRES_VAL_TYPE_FOUND  = 0,
	GRES_VAL_TYPE_CONFIG = 1,
	GRES_VAL_TYPE_AVAIL  = 2,
	GRES_VAL_TYPE_ALLOC  = 3
};

/* Gres state information gathered by slurmd daemon */
typedef struct gres_slurmd_conf {
	/* Count of gres available in this configuration record */
	uint32_t count;

	/* Specific CPUs associated with this configuration record */
	uint16_t cpu_cnt;
	char *cpus;

	/* Device file associated with this configuration record */
	char *file;
	uint8_t has_file;	/* non-zero if file is set, flag for RPC */

	/* Name of this gres */
	char *name;

	/* Type of this gres (e.g. model name) */
	char *type;

	/* Gres ID number */
	uint32_t plugin_id;
} gres_slurmd_conf_t;

/* Current gres state information managed by slurmctld daemon */
typedef struct gres_node_state {
	/* Actual hardware found */
	uint32_t gres_cnt_found;

	/* Configured resources via Gres parameter */
	uint32_t gres_cnt_config;

	/* Non-consumable: Do not track resources allocated to jobs */
	bool no_consume;

	/* Total resources available for allocation to jobs.
	 * gres_cnt_found or gres_cnt_config, depending upon FastSchedule */
	uint32_t gres_cnt_avail;

	/* List of GRES in current use. Set NULL if needs to be rebuilt. */
	char *gres_used;

	/* Resources currently allocated to jobs */
	uint32_t  gres_cnt_alloc;
	bitstr_t *gres_bit_alloc;	/* If gres.conf contains File field */

	/* Topology specific information (if gres.conf contains CPUs option) */
	uint16_t topo_cnt;		/* Size of topo_ arrays */
	bitstr_t **topo_cpus_bitmap;
	bitstr_t **topo_gres_bitmap;
	uint32_t *topo_gres_cnt_alloc;
	uint32_t *topo_gres_cnt_avail;
	char **topo_model;		/* Type of this gres (e.g. model name) */

	/* Gres type specific information (if gres.conf contains type option) */
	uint16_t type_cnt;		/* Size of type_ arrays */
	uint32_t *type_cnt_alloc;
	uint32_t *type_cnt_avail;
	char **type_model;		/* Type of this gres (e.g. model name) */
} gres_node_state_t;

/* Gres job state as used by slurmctld daemon */
typedef struct gres_job_state {
	char *type_model;		/* Type of this gres (e.g. model name) */

	/* Count of resources needed per node */
	uint32_t gres_cnt_alloc;

	/* Resources currently allocated to job on each node */
	uint32_t node_cnt;		/* 0 if no_consume */
	bitstr_t **gres_bit_alloc;

	/* Resources currently allocated to job steps on each node.
	 * This will be a subset of resources allocated to the job.
	 * gres_bit_step_alloc is a subset of gres_bit_alloc */
	bitstr_t **gres_bit_step_alloc;
	uint32_t  *gres_cnt_step_alloc;
} gres_job_state_t;

/* Gres job step state as used by slurmctld daemon */
typedef struct gres_step_state {
	char *type_model;		/* Type of this gres (e.g. model name) */

	/* Count of resources needed per node */
	uint32_t gres_cnt_alloc;

	/* Resources currently allocated to the job step on each node
	 *
	 * NOTE: node_cnt and the size of node_in_use and gres_bit_alloc are
	 * identical to that of the job for simplicity. Bits in node_in_use
	 * are set for those node of the job that are used by this step and 
	 * gres_bit_alloc are also set if the job's gres_bit_alloc is set */
	uint32_t node_cnt;
	bitstr_t *node_in_use;
	bitstr_t **gres_bit_alloc;
} gres_step_state_t;

/*
 * Initialize the gres plugin.
 *
 * Returns a SLURM errno.
 */
extern int gres_plugin_init(void);

/*
 * Terminate the gres plugin. Free memory.
 *
 * Returns a SLURM errno.
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
extern int gres_plugin_reconfig(bool *did_change);

/*
 * Provide a plugin-specific help message for salloc, sbatch and srun
 * IN/OUT msg - buffer provided by caller and filled in by plugin
 * IN msg_size - size of msg buffer in bytes
 */
extern int gres_plugin_help_msg(char *msg, int msg_size);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMD DAEMON                         *
 **************************************************************************
 */
/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs on configured on this node
 * IN array_len - count of elements in dev_path and gres_name
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt, char *node_name);

/*
 * Pack this node's gres configuration into a buffer
 * IN/OUT buffer - message buffer to pack
 */
extern int gres_plugin_node_config_pack(Buf buffer);

/*
 * Return information about the configured gres devices on the node
 * OUT dev_path - the devices paths as written on gres.conf file
 * OUT gres_name - the names of the devices (ex. gpu, nic,..)
 * IN array_len - count of elements in dev_path and gres_name
 * IN node_name - Name of this compute node
 * OUT int - number of lines of gres.conf file
 */
extern int gres_plugin_node_config_devices_path(char **dev_path,
						char **gres_name,
						int array_len,
						char *node_name);

/*
 * Provide information about the allocate gres devices for a particular job
 * IN gres_list - jobs allocated gres devices
 * IN gres_count - count of gres.conf records for each gres name
 * OUT gres_bit_alloc - the exact devices which are allocated
 */
extern void gres_plugin_job_state_file(List gres_list, int *gres_bit_alloc,
				       int *gres_count);

/*
 * Provide information about the allocate gres devices for a particular step
 * IN gres_list - jobs allocated gres devices
 * IN gres_count - count of gres.conf records for each gres name
 * OUT gres_bit_alloc - the exact devices which are allocated
 */
extern void gres_plugin_step_state_file(List gres_list, int *gres_bit_alloc,
					int *gres_count);

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

/*
 * Unpack this node's configuration from a buffer (built/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_node_config_unpack(Buf buffer, char *node_name);

/*
 * Validate a node's configuration and put a gres record onto a list
 *	(expected to be an element of slurmctld's node record).
 * Called immediately after gres_plugin_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Gres information from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config,
					    char **new_config,
					    List *gres_list,
					    uint16_t fast_schedule,
					    char **reason_down);

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 */
extern int gres_plugin_node_reconfig(char *node_name,
				     char *orig_config,
				     char **new_config,
				     List *gres_list,
				     uint16_t fast_schedule);

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
				  int *gres_count_ids, int *gres_count_vals,
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
				 int *gres_count_ids, int *gres_count_vals);

/*
 * Given a job's requested gres configuration, validate it and build a gres list
 * IN req_config - job request's gres input string
 * OUT gres_list - List of Gres records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_state_validate(char *req_config, List *gres_list);

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only gres_cnt_alloc, node_cnt and gres_bit_alloc are copied
 *	 Job step details are NOT copied.
 */
List gres_plugin_job_state_dup(List gres_list);

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_plugin_job_state_extract(List gres_list, int node_index);

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
 * Clear the cpu_bitmap for CPUs which are not usable by this job (i.e. for
 *	CPUs which are already bound to other jobs or lack GRES)
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN/OUT cpu_bitmap - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * IN node_name      - name of the node (for logging)
 */
extern void gres_plugin_job_core_filter(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *cpu_bitmap,
					int cpu_start_bit, int cpu_end_bit,
					char *node_name);

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres, bitstr_t *cpu_bitmap,
				     int cpu_start_bit, int cpu_end_bit,
				     uint32_t job_id, char *node_name);

/*
 * Allocate resource to a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_offset - zero-origin index to the node of interest
 * IN cpu_cnt     - number of CPUs allocated to this job on this node
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list, 
				 int node_cnt, int node_offset,
				 uint32_t cpu_cnt, uint32_t job_id,
				 char *node_name, bitstr_t *core_bitmap);

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
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_dealloc(List job_gres_list, List node_gres_list, 
				   int node_offset, uint32_t job_id,
				   char *node_name);

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
  */
extern void gres_plugin_job_set_env(char ***job_env_ptr, List job_gres_list);


/*
 * Extract from the job record's gres_list the count of allocated resources of
 * 	the named gres type.
 * IN job_gres_list  - job record's gres_list.
 * IN gres_name_type - the name of the gres type to retrieve the associated
 *	value from.
 * RET The value associated with the gres type or NO_VAL if not found.
 */
extern uint32_t gres_plugin_get_job_value_by_type(List job_gres_list,
						  char *gres_name_type);

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_plugin_job_state_validate()
 * IN job_id    - job's ID
 */
extern void gres_plugin_job_state_log(List gres_list, uint32_t job_id);

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN req_config - step request's gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list - List of Gres records for this job
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_step_state_validate(char *req_config,
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

/*
 * Set environment variables as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_plugin_step_allocate()
  */
extern void gres_plugin_step_set_env(char ***job_env_ptr, List step_gres_list);

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_plugin_step_allocate()
 * IN job_id - job's ID
 */
extern void gres_plugin_step_state_log(List gres_list, uint32_t job_id,
				       uint32_t step_id);

/*
 * Determine how many CPUs of a job's allocation can be allocated to a job
 *	on a specific node
 * IN job_gres_list - a running job's gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * RET Count of available CPUs on this node, NO_VAL if no limit
 */
extern uint32_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool ignore_alloc,
				      uint32_t job_id, uint32_t step_id);

/*
 * Allocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_offset - job's zero-origin index to the node of interest
 * IN cpu_cnt - number of CPUs allocated to this job on this node
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, int cpu_cnt,
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
 * Map a given GRES type ID back to a GRES type name.
 * gres_id IN - GRES type ID to search for.
 * gres_name IN - Pre-allocated string in which to store the GRES type name.
 * gres_name_len - Size of gres_name in bytes
 * RET - error code (currently not used--always return SLURM_SUCCESS)
 */
extern int gres_gresid_to_gresname(uint32_t gres_id, char* gres_name,
				   int gres_name_len);

/*
 * Determine how many GRES of a given type are allocated to a job
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of a GRES type
 * RET count of this GRES allocated to this job
 */
extern uint32_t gres_get_value_by_type(List job_gres_list, char* gres_name);

enum gres_job_data_type {
	GRES_JOB_DATA_COUNT,	/* data-> uint32_t  */
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
	GRES_STEP_DATA_COUNT,	/* data-> uint32_t  */
	GRES_STEP_DATA_BITMAP,	/* data-> bitstr_t* */
};

/*
 * get data from a step's GRES data structure
 * IN job_gres_list  - step's GRES data structure
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

#endif /* !_GRES_H */
