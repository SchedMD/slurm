/*****************************************************************************\
 *  job_resources.h - functions to manage data structure identifying specific
 *	CPUs allocated to a job, step or partition
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _JOB_RESOURCES_H
#define _JOB_RESOURCES_H

#include <inttypes.h>

#include "src/common/bitstring.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"

/* struct job_resources defines exactly which resources are allocated
 *	to a job, step, partition, etc.
 *
 * core_bitmap		- Bitmap of allocated cores for all nodes and sockets.
 *			  The bitmap reflects allocated resources only on the
 *			  allocated nodes, not the full system resources.
 * core_bitmap_used	- Bitmap of cores allocated to job steps (see above)
 * cores_per_socket	- Count of cores per socket on this node, build by
 *			  build_job_resources() and ensures consistent
 *			  interpretation of core_bitmap
 * cpus			- Count of desired/allocated CPUs per node for job/step
 * cpus_used		- For a job, count of CPUs per node used by job steps
 * cpu_array_cnt	- Count of elements in cpu_array_* below
 * cpu_array_value	- Count of allocated CPUs per node for job
 * cpu_array_reps	- Number of consecutive nodes on which cpu_array_value
 *			  is duplicated. See NOTES below.
 * memory_allocated	- MB per node reserved for the job or step
 * memory_used		- MB per node of memory consumed by job steps
 * nhosts		- Number of nodes in the allocation.  On a
 *                        bluegene machine this represents the number
 *                        of midplanes used.  This should always be
 *                        the number of bits set in node_bitmap.
 * node_bitmap		- Bitmap of nodes allocated to the job. Unlike the
 *			  node_bitmap in slurmctld's job record, the bits
 *			  here do NOT get cleared as the job completes on a
 *			  node
 * node_req		- NODE_CR_RESERVED|NODE_CR_ONE_ROW|NODE_CR_AVAILABLE
 * nodes		- Names of nodes in original job allocation
 * ncpus		- Number of processors in the allocation
 * sock_core_rep_count	- How many consecutive nodes that sockets_per_node
 *			  and cores_per_socket apply to, build by
 *			  build_job_resources() and ensures consistent
 *			  interpretation of core_bitmap
 * sockets_per_node	- Count of sockets on this node, build by
 *			  build_job_resources() and ensures consistent
 *			  interpretation of core_bitmap
 * whole_node		- Job allocated full node (used only by select/cons_res)
 *
 * NOTES:
 * cpu_array_* contains the same information as "cpus", but in a more compact
 * format. For example if cpus = {4, 4, 2, 2, 2, 2, 2, 2} then cpu_array_cnt=2
 * cpu_array_value = {4, 2} and cpu_array_reps = {2, 6}. We do not need to
 * save/restore these values, but generate them by calling
 * build_job_resources_cpu_array()
 *
 * Sample layout of core_bitmap:
 *   |               Node_0              |               Node_1              |
 *   |      Sock_0     |      Sock_1     |      Sock_0     |      Sock_1     |
 *   | Core_0 | Core_1 | Core_0 | Core_1 | Core_0 | Core_1 | Core_0 | Core_1 |
 *   | Bit_0  | Bit_1  | Bit_2  | Bit_3  | Bit_4  | Bit_5  | Bit_6  | Bit_7  |
 *
 * If a job changes size (reliquishes nodes), the node_bitmap will remain
 * unchanged, but cpus, cpus_used, cpus_array_*, and memory_used will be 
 * updated (e.g. cpus and mem_used on that node cleared).
 */
struct job_resources {
	bitstr_t *core_bitmap;
	bitstr_t *core_bitmap_used;
	uint32_t  cpu_array_cnt;
	uint16_t *cpu_array_value;
	uint32_t *cpu_array_reps;
	uint16_t *cpus;
	uint16_t *cpus_used;
	uint16_t *cores_per_socket;
	uint64_t *memory_allocated;
	uint64_t *memory_used;
	uint32_t  nhosts;
	bitstr_t *node_bitmap;
	uint32_t  node_req;
	char	 *nodes;
	uint32_t  ncpus;
	uint32_t *sock_core_rep_count;
	uint16_t *sockets_per_node;
	uint8_t   whole_node;
};

/*
 * node_res_record.node_state assists with the unique state of each node.
 * When a job is allocated, these flags provide protection for nodes in a
 * Shared=NO or Shared=EXCLUSIVE partition from other jobs.
 *
 * NOTES:
 * - If node is in use by Shared=NO part, some CPUs/memory may be available
 * - Caution with NODE_CR_AVAILABLE: a Sharing partition could be full.
 *
 * - these values are staggered so that they can be incremented as multiple
 *   jobs are allocated to each node. This is needed to be able to support
 *   preemption, which can override these protections.
 */
enum node_cr_state {
	NODE_CR_AVAILABLE = 0,    /* The node may be IDLE or IN USE (shared) */
	NODE_CR_ONE_ROW = 1,      /* node is in use by Shared=NO part */
	NODE_CR_RESERVED = 64000  /* node is in use by Shared=EXCLUSIVE part */
};

/* Create an empty job_resources data structure, just a call to xmalloc() */
extern job_resources_t *create_job_resources(void);

/* Set the socket and core counts associated with a set of selected
 * nodes of a job_resources data structure based upon slurmctld state.
 * (sets cores_per_socket, sockets_per_node, and sock_core_rep_count based
 * upon the value of node_bitmap, also creates core_bitmap based upon
 * the total number of cores in the allocation). Call this ONLY from
 * slurmctld. Example of use:
 *
 * job_resources_t *job_resrcs_ptr = create_job_resources();
 * node_name2bitmap("dummy[2,5,12,16]", true, &(job_res_ptr->node_bitmap));
 * rc = build_job_resources(job_resrcs_ptr, node_record_table_ptr,
 *			     slurmctld_conf.fast_schedule);
 */
extern int build_job_resources(job_resources_t *job_resrcs_ptr,
			       void *node_rec_table, uint16_t fast_schedule);

/* Rebuild cpu_array_cnt, cpu_array_value, and cpu_array_reps based upon the
 * values of cpus in an existing data structure
 * Return total CPU count or -1 on error */
extern int build_job_resources_cpu_array(job_resources_t *job_resrcs_ptr);

/* Rebuild cpus array based upon the values of nhosts, cpu_array_value and
 * cpu_array_reps in an existing data structure
 * Return total CPU count or -1 on error */
extern int build_job_resources_cpus_array(job_resources_t *job_resrcs_ptr);

/* Validate a job_resources data structure originally built using
 * build_job_resources() is still valid based upon slurmctld state.
 * NOTE: Reset the node_bitmap field before calling this function.
 * If the sockets_per_node or cores_per_socket for any node in the allocation
 * changes, then return SLURM_ERROR. Otherwise return SLURM_SUCCESS. Any
 * change in a node's socket or core count require that any job running on
 * that node be killed. Example of use:
 *
 * rc = valid_job_resources(job_resrcs_ptr, node_record_table_ptr,
 *			     slurmctld_conf.fast_schedule);
 */
extern int valid_job_resources(job_resources_t *job_resrcs_ptr,
			       void *node_rec_table, uint16_t fast_schedule);

/* Make a copy of a job_resources data structure,
 * free using free_job_resources() */
extern job_resources_t *copy_job_resources(job_resources_t *job_resrcs_ptr);

/* Free job_resources data structure created using copy_job_resources() or
 *	unpack_job_resources() */
extern void free_job_resources(job_resources_t **job_resrcs_pptr);

/* Log the contents of a job_resources data structure using info() */
extern void log_job_resources(void *job_ptr);

/* Un/pack full job_resources data structure */
extern void pack_job_resources(job_resources_t *job_resrcs_ptr, Buf buffer,
			       uint16_t protocol_version);
extern int unpack_job_resources(job_resources_t **job_resrcs_pptr,
				Buf buffer, uint16_t protocol_version);

/* Reset the node_bitmap in a job_resources data structure
 * This is needed after a restart/reconfiguration since nodes can
 * be added or removed from the system resulting in changing in
 * the bitmap size or bit positions */
extern int reset_node_bitmap(void *job_ptr);

/* For a given node_id, socket_id and core_id, get it's offset within
 * the core bitmap */
extern int get_job_resources_offset(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id, uint16_t socket_id,
				    uint16_t core_id);

/* Get/set bit value at specified location.
 *	node_id, socket_id and core_id are all zero origin */
extern int get_job_resources_bit(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t socket_id,
				 uint16_t core_id);
extern int set_job_resources_bit(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t socket_id,
				 uint16_t core_id);
/* For every core bitmap set in the "from" resources structure at
 * from_node_offset, set the corresponding bit in the "new" resources structure
 * at new_node_offset */
extern int job_resources_bits_copy(job_resources_t *new_job_resrcs_ptr,
				   uint16_t new_node_offset,
				   job_resources_t *from_job_resrcs_ptr,
				   uint16_t from_node_offset);

/*
 * AND two job_resources structures.
 * Every node/core set in job_resrcs1_ptr and job_resrcs2_ptr is set in the
 * resulting job_resrcs1_ptr data structure
 * RET SLURM_SUCCESS or an error code
 */
extern int job_resources_and(job_resources_t *job_resrcs1_ptr,
			     job_resources_t *job_resrcs2_ptr);

/*
 * OR two job_resources structures.
 * Every node/core set in job_resrcs1_ptr or job_resrcs2_ptr is set in the
 * resulting job_resrcs1_ptr data structure
 * RET SLURM_SUCCESS or an error code
 */
extern int job_resources_or(job_resources_t *job_resrcs1_ptr,
			    job_resources_t *job_resrcs2_ptr);

/* Get/clear/set bit value at specified location for whole node allocations
 *	get is for any socket/core on the specified node
 *	set is for all sockets/cores on the specified node
 *	fully compatible with set/get_job_resources_bit()
 *	node_id is all zero origin */
extern int get_job_resources_node(job_resources_t *job_resrcs_ptr,
				  uint32_t node_id);
extern int clear_job_resources_node(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id);
extern int set_job_resources_node(job_resources_t *job_resrcs_ptr,
				  uint32_t node_id);

/* Completely remove specified node from job resources structure */
extern int extract_job_resources_node(job_resources_t *job_resrcs_ptr,
				      uint32_t node_id);

/* Return the count of core bitmaps set for the specific node */
extern int count_job_resources_node(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id);

/* Return a copy of core_bitmap only for the specific node */
extern bitstr_t * copy_job_resources_node(job_resources_t *job_resrcs_ptr,
					  uint32_t node_id);

/* Get socket and core count for a specific node_id (zero origin) */
extern int get_job_resources_cnt(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t *socket_cnt,
				 uint16_t *cores_per_socket_cnt);

/*
 * Test if job can fit into the given full-length core_bitmap
 * IN job_resrcs_ptr - resources allocated to a job
 * IN full_bitmap - bitmap of available CPUs
 * IN bits_per_node - bits per node in the full_bitmap
 * RET 1 on success, 0 otherwise
 */
extern int job_fits_into_cores(job_resources_t *job_resrcs_ptr,
			       bitstr_t *full_bitmap,
			       const uint16_t *bits_per_node);

/*
 * Add job to full-length core_bitmap
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 * IN bits_per_node - bits per node in the full_bitmap
 * RET 1 on success, 0 otherwise
 */
extern void add_job_to_cores(job_resources_t *job_resrcs_ptr,
			     bitstr_t **full_core_bitmap,
			     const uint16_t *bits_per_node);

/*
 * Remove job from full-length core_bitmap
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 * IN bits_per_node - bits per node in the full_bitmap
 * RET 1 on success, 0 otherwise
 */
extern void remove_job_from_cores(job_resources_t *job_resrcs_ptr,
				  bitstr_t **full_core_bitmap,
				  const uint16_t *bits_per_node);

/* Given a job pointer and a global node index, return the index of that
 * node in the job_resrcs_ptr->cpus. Return -1 if invalid */
extern int job_resources_node_inx_to_cpu_inx(job_resources_t *job_resrcs_ptr, 
					     int node_inx);
/*
 * adapt the power_cpufreq layout and set the CurrentCoreWatts value of the cores
 * based on the selection of the resources and the choice of CPU Frequency 
 * CurrentCoreWatts are set to IdleWatts when one or more jobs occupy other 
 * resources of the node and set to 0 when the node is liberated
 */
extern int adapt_layouts(job_resources_t *job_resrcs_ptr, uint32_t cpu_freq_max,
                         uint32_t node_id, char* node_name, bool new_value);

#endif /* !_JOB_RESOURCES_H */
