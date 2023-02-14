/*****************************************************************************\
 *  job_resources.c - functions to manage data structure identifying specific
 *	CPUs allocated to a job, step or partition
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/hostlist.h"
#include "src/common/job_resources.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/slurmctld.h"


/* Create an empty job_resources data structure */
extern job_resources_t *create_job_resources(void)
{
	job_resources_t *job_resrcs;

	job_resrcs = xmalloc(sizeof(struct job_resources));
	return job_resrcs;
}

/* Set the socket and core counts associated with a set of selected
 * nodes of a job_resources data structure based upon slurmctld state.
 * (sets cores_per_socket, sockets_per_node, and sock_core_rep_count based
 * upon the value of node_bitmap, also creates core_bitmap based upon
 * the total number of cores in the allocation). Call this ONLY from
 * slurmctld. Example of use:
 *
 * job_resources_t *job_resrcs_ptr = create_job_resources();
 * node_name2bitmap("dummy[2,5,12,16]", true, &(job_res_ptr->node_bitmap));
 * rc = build_job_resources(job_resrcs_ptr);
 */
extern int build_job_resources(job_resources_t *job_resrcs)
{
	int core_cnt = 0, sock_inx = -1;
	node_record_t *node_ptr;

	if (job_resrcs->node_bitmap == NULL) {
		error("build_job_resources: node_bitmap is NULL");
		return SLURM_ERROR;
	}

	xfree(job_resrcs->sockets_per_node);
	xfree(job_resrcs->cores_per_socket);
	xfree(job_resrcs->sock_core_rep_count);
	job_resrcs->sockets_per_node = xcalloc(job_resrcs->nhosts,
					       sizeof(uint16_t));
	job_resrcs->cores_per_socket = xcalloc(job_resrcs->nhosts,
					       sizeof(uint16_t));
	job_resrcs->sock_core_rep_count = xcalloc(job_resrcs->nhosts,
						  sizeof(uint32_t));

	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs->node_bitmap, &i)); i++) {
		if ((sock_inx < 0) ||
		    (node_ptr->tot_sockets !=
		     job_resrcs->sockets_per_node[sock_inx]) ||
		    (node_ptr->cores !=
		     job_resrcs->cores_per_socket[sock_inx])) {
			sock_inx++;
			job_resrcs->sockets_per_node[sock_inx] =
				node_ptr->tot_sockets;
			job_resrcs->cores_per_socket[sock_inx] =
				node_ptr->cores;
		}
		job_resrcs->sock_core_rep_count[sock_inx]++;
		core_cnt += node_ptr->tot_cores;
	}
	if (core_cnt) {
		/*
		 * A zero size job (for burst buffer create/destroy only)
		 * will have no bitmaps.
		 */
		job_resrcs->core_bitmap = bit_alloc(core_cnt);
		job_resrcs->core_bitmap_used = bit_alloc(core_cnt);
	}
	return SLURM_SUCCESS;
}

/* Rebuild cpu_array_cnt, cpu_array_value, and cpu_array_reps based upon the
 * values of nhosts and cpus in an existing data structure
 * Return total CPU count or -1 on error */
extern int build_job_resources_cpu_array(job_resources_t *job_resrcs_ptr)
{
	int cpu_count = 0;
	uint32_t last_cpu_cnt = NO_VAL;
	int node_cpu_count;

	if (job_resrcs_ptr->nhosts == 0)
		return cpu_count;	/* no work to do */
	if (job_resrcs_ptr->cpus == NULL) {
		error("build_job_resources_cpu_array: cpus==NULL");
		return -1;
	}

	/* clear vestigial data and create new arrays of max size */
	job_resrcs_ptr->cpu_array_cnt = 0;
	xfree(job_resrcs_ptr->cpu_array_reps);
	job_resrcs_ptr->cpu_array_reps = xcalloc(job_resrcs_ptr->nhosts,
						 sizeof(uint32_t));
	xfree(job_resrcs_ptr->cpu_array_value);
	job_resrcs_ptr->cpu_array_value = xcalloc(job_resrcs_ptr->nhosts,
						  sizeof(uint16_t));

	for (int i = 0, j = 0;
	     next_node_bitmap(job_resrcs_ptr->node_bitmap, &i); i++) {
		/*
		 * This needs to be the threads per core count to handle
		 * allocations correctly.
		 */
		node_cpu_count = job_resources_get_node_cpu_cnt(
			job_resrcs_ptr, j, i);

		if (node_cpu_count != last_cpu_cnt) {
			last_cpu_cnt = node_cpu_count;
			job_resrcs_ptr->cpu_array_value[
				job_resrcs_ptr->cpu_array_cnt]
				= last_cpu_cnt;
			job_resrcs_ptr->cpu_array_reps[
				job_resrcs_ptr->cpu_array_cnt] = 1;
			job_resrcs_ptr->cpu_array_cnt++;
		} else {
			job_resrcs_ptr->cpu_array_reps[
				job_resrcs_ptr->cpu_array_cnt-1]++;
		}
		/* This needs to be the full amount for accounting */
		cpu_count += job_resrcs_ptr->cpus[j];
		j++;
	}
	return cpu_count;
}

/* Reset the node_bitmap in a job_resources data structure
 * This is needed after a restart/reconfiguration since nodes can
 * be added or removed from the system resulting in changing in
 * the bitmap size or bit positions */
extern int reset_node_bitmap(void *void_job_ptr)
{
	job_record_t *job_ptr = (job_record_t *) void_job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int i;

	if (!job_resrcs_ptr)
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(job_resrcs_ptr->node_bitmap);

	if (job_resrcs_ptr->nodes &&
	    (node_name2bitmap(job_resrcs_ptr->nodes, false,
			      &job_resrcs_ptr->node_bitmap))) {
		error("Invalid nodes (%s) for %pJ",
		      job_resrcs_ptr->nodes, job_ptr);
		return SLURM_ERROR;
	} else if (job_resrcs_ptr->nodes == NULL) {
		job_resrcs_ptr->node_bitmap = bit_alloc(node_record_count);
	}

	i = bit_set_count(job_resrcs_ptr->node_bitmap);
	if (job_resrcs_ptr->nhosts != i) {
		error("Invalid change in resource allocation node count for %pJ, %u to %d",
		      job_ptr, job_resrcs_ptr->nhosts, i);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

extern int valid_job_resources(job_resources_t *job_resrcs)
{
	int sock_inx = 0, sock_cnt = 0;
	int total_job_cores, total_node_cores;
	node_record_t *node_ptr;

	if (job_resrcs->node_bitmap == NULL) {
		error("valid_job_resources: node_bitmap is NULL");
		return SLURM_ERROR;
	}
	if ((job_resrcs->sockets_per_node == NULL) ||
	    (job_resrcs->cores_per_socket == NULL) ||
	    (job_resrcs->sock_core_rep_count == NULL)) {
		error("valid_job_resources: socket/core array is NULL");
		return SLURM_ERROR;
	}

	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs->node_bitmap, &i)); i++) {
		if (sock_cnt >= job_resrcs->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_cnt = 0;
		}
		/* KNL nodes can should maintain a constant total core count,
		 * but the socket/NUMA count can change on reboot */
		total_job_cores = job_resrcs->sockets_per_node[sock_inx] *
				  job_resrcs->cores_per_socket[sock_inx];
		total_node_cores = node_ptr->tot_cores;
		if (total_job_cores != total_node_cores) {
			error("valid_job_resources: %s sockets:%u,%u, cores %u,%u",
			      node_ptr->name,
			      node_ptr->tot_sockets,
			      job_resrcs->sockets_per_node[sock_inx],
			      node_ptr->cores,
			      job_resrcs->cores_per_socket[sock_inx]);
			return SLURM_ERROR;
		}
		sock_cnt++;
	}
	return SLURM_SUCCESS;
}

extern job_resources_t *copy_job_resources(job_resources_t *job_resrcs_ptr)
{
	int i, sock_inx = 0;
	job_resources_t *new_layout = xmalloc(sizeof(struct job_resources));

	xassert(job_resrcs_ptr);
	new_layout->nhosts = job_resrcs_ptr->nhosts;
	new_layout->ncpus = job_resrcs_ptr->ncpus;
	new_layout->node_req = job_resrcs_ptr->node_req;
	new_layout->whole_node = job_resrcs_ptr->whole_node;
	if (job_resrcs_ptr->core_bitmap) {
		new_layout->core_bitmap = bit_copy(job_resrcs_ptr->
						   core_bitmap);
	}
	if (job_resrcs_ptr->core_bitmap_used) {
		new_layout->core_bitmap_used = bit_copy(job_resrcs_ptr->
							core_bitmap_used);
	}
	if (job_resrcs_ptr->node_bitmap) {
		new_layout->node_bitmap = bit_copy(job_resrcs_ptr->
						   node_bitmap);
	}

	new_layout->cpu_array_cnt = job_resrcs_ptr->cpu_array_cnt;
	if (job_resrcs_ptr->cpu_array_reps &&
	    job_resrcs_ptr->cpu_array_cnt) {
		new_layout->cpu_array_reps =
			xcalloc(job_resrcs_ptr->cpu_array_cnt,
				sizeof(uint32_t));
		memcpy(new_layout->cpu_array_reps,
		       job_resrcs_ptr->cpu_array_reps,
		       (sizeof(uint32_t) * job_resrcs_ptr->cpu_array_cnt));
	}
	if (job_resrcs_ptr->cpu_array_value &&
	    job_resrcs_ptr->cpu_array_cnt) {
		new_layout->cpu_array_value =
			xcalloc(job_resrcs_ptr->cpu_array_cnt,
				sizeof(uint16_t));
		memcpy(new_layout->cpu_array_value,
		       job_resrcs_ptr->cpu_array_value,
		       (sizeof(uint16_t) * job_resrcs_ptr->cpu_array_cnt));
	}

	if (job_resrcs_ptr->cpus) {
		new_layout->cpus = xcalloc(job_resrcs_ptr->nhosts,
					   sizeof(uint16_t));
		memcpy(new_layout->cpus, job_resrcs_ptr->cpus,
		       (sizeof(uint16_t) * job_resrcs_ptr->nhosts));
	}
	if (job_resrcs_ptr->cpus_used) {
		new_layout->cpus_used = xcalloc(job_resrcs_ptr->nhosts,
						sizeof(uint16_t));
		memcpy(new_layout->cpus_used, job_resrcs_ptr->cpus_used,
		       (sizeof(uint16_t) * job_resrcs_ptr->nhosts));
	}

	if (job_resrcs_ptr->memory_allocated) {
		new_layout->memory_allocated = xcalloc(new_layout->nhosts,
						       sizeof(uint64_t));
		memcpy(new_layout->memory_allocated,
		       job_resrcs_ptr->memory_allocated,
		       (sizeof(uint64_t) * job_resrcs_ptr->nhosts));
	}
	if (job_resrcs_ptr->memory_used) {
		new_layout->memory_used = xcalloc(new_layout->nhosts,
						  sizeof(uint64_t));
		memcpy(new_layout->memory_used,
		       job_resrcs_ptr->memory_used,
		       (sizeof(uint64_t) * job_resrcs_ptr->nhosts));
	}

	/* Copy sockets_per_node, cores_per_socket and core_sock_rep_count */
	new_layout->sockets_per_node = xcalloc(new_layout->nhosts,
					       sizeof(uint16_t));
	new_layout->cores_per_socket = xcalloc(new_layout->nhosts,
					       sizeof(uint16_t));
	new_layout->sock_core_rep_count = xcalloc(new_layout->nhosts,
						  sizeof(uint32_t));
	for (i=0; i<new_layout->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] ==  0) {
			error("copy_job_resources: sock_core_rep_count=0");
			break;
		}
		sock_inx += job_resrcs_ptr->sock_core_rep_count[i];
		if (sock_inx >= job_resrcs_ptr->nhosts) {
			i++;
			break;
		}
	}
	memcpy(new_layout->sockets_per_node,
	       job_resrcs_ptr->sockets_per_node, (sizeof(uint16_t) * i));
	memcpy(new_layout->cores_per_socket,
	       job_resrcs_ptr->cores_per_socket, (sizeof(uint16_t) * i));
	memcpy(new_layout->sock_core_rep_count,
	       job_resrcs_ptr->sock_core_rep_count,
	       (sizeof(uint32_t) * i));

	return new_layout;
}

extern void free_job_resources(job_resources_t **job_resrcs_pptr)
{
	job_resources_t *job_resrcs_ptr = *job_resrcs_pptr;

	if (job_resrcs_ptr) {
		FREE_NULL_BITMAP(job_resrcs_ptr->core_bitmap);
		FREE_NULL_BITMAP(job_resrcs_ptr->core_bitmap_used);
		xfree(job_resrcs_ptr->cores_per_socket);
		xfree(job_resrcs_ptr->cpu_array_reps);
		xfree(job_resrcs_ptr->cpu_array_value);
		xfree(job_resrcs_ptr->cpus);
		xfree(job_resrcs_ptr->cpus_used);
		xfree(job_resrcs_ptr->memory_allocated);
		xfree(job_resrcs_ptr->memory_used);
		FREE_NULL_BITMAP(job_resrcs_ptr->node_bitmap);
		xfree(job_resrcs_ptr->nodes);
		xfree(job_resrcs_ptr->sock_core_rep_count);
		xfree(job_resrcs_ptr->sockets_per_node);
		xfree(job_resrcs_ptr->tasks_per_node);
		xfree(job_resrcs_ptr);
		*job_resrcs_pptr = NULL;
	}
}

/*
 * Log the contents of a job_resources data structure using info()
 *
 * Function argument is void * to avoid a circular dependency between
 * job_resources.h and slurmctld.h. Cast inside the function here to
 * resolve that problem for now.
 */
extern void log_job_resources(void *void_job_ptr)
{
	job_record_t *job_ptr = (job_record_t *) void_job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int bit_inx = 0, bit_reps, i;
	int array_size, node_inx;
	int sock_inx = 0, sock_reps = 0;

	if (job_resrcs_ptr == NULL) {
		error("%s: job_resrcs_ptr is NULL", __func__);
		return;
	}

	info("====================");
	info("%pJ nhosts:%u ncpus:%u node_req:%u nodes=%s",
	     job_ptr, job_resrcs_ptr->nhosts, job_resrcs_ptr->ncpus,
	     job_resrcs_ptr->node_req, job_resrcs_ptr->nodes);

	if (job_resrcs_ptr->cpus == NULL) {
		error("%s: cpus array is NULL", __func__);
		return;
	}
	if (job_resrcs_ptr->memory_allocated == NULL) {
		error("%s: memory array is NULL", __func__);
		return;
	}
	if ((job_resrcs_ptr->cores_per_socket == NULL) ||
	    (job_resrcs_ptr->sockets_per_node == NULL) ||
	    (job_resrcs_ptr->sock_core_rep_count == NULL)) {
		error("%s: socket/core array is NULL", __func__);
		return;
	}
	if (job_resrcs_ptr->core_bitmap == NULL) {
		error("%s: core_bitmap is NULL", __func__);
		return;
	}
	if (job_resrcs_ptr->core_bitmap_used == NULL) {
		error("%s: core_bitmap_used is NULL", __func__);
		return;
	}
	array_size = bit_size(job_resrcs_ptr->core_bitmap);

	/* Can only log node_bitmap from slurmctld, so don't bother here */
	for (node_inx=0; node_inx<job_resrcs_ptr->nhosts; node_inx++) {
		uint32_t cpus_used = 0;
		uint64_t memory_allocated = 0, memory_used = 0;
		info("Node[%d]:", node_inx);

		if (sock_reps >=
		    job_resrcs_ptr->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		if (job_resrcs_ptr->cpus_used)
			cpus_used = job_resrcs_ptr->cpus_used[node_inx];
		if (job_resrcs_ptr->memory_used)
			memory_used = job_resrcs_ptr->memory_used[node_inx];
		if (job_resrcs_ptr->memory_allocated)
			memory_allocated = job_resrcs_ptr->
				memory_allocated[node_inx];

		info("  Mem(MB):%"PRIu64":%"PRIu64"  Sockets:%u"
		     "  Cores:%u  CPUs:%u:%u",
		     memory_allocated, memory_used,
		     job_resrcs_ptr->sockets_per_node[sock_inx],
		     job_resrcs_ptr->cores_per_socket[sock_inx],
		     job_resrcs_ptr->cpus[node_inx],
		     cpus_used);

		bit_reps = job_resrcs_ptr->sockets_per_node[sock_inx] *
			job_resrcs_ptr->cores_per_socket[sock_inx];
		for (i=0; i<bit_reps; i++) {
			if (bit_inx >= array_size) {
				error("%s: array size wrong", __func__);
				break;
			}
			if (bit_test(job_resrcs_ptr->core_bitmap,
				     bit_inx)) {
				char *core_used = "";
				if (bit_test(job_resrcs_ptr->
					     core_bitmap_used, bit_inx))
					core_used = " and in use";
				info("  Socket[%d] Core[%d] is allocated%s",
				     (i / job_resrcs_ptr->
				      cores_per_socket[sock_inx]),
				     (i % job_resrcs_ptr->
				      cores_per_socket[sock_inx]),
				     core_used);
			}
			bit_inx++;
		}
	}
	for (node_inx=0; node_inx<job_resrcs_ptr->cpu_array_cnt;
	     node_inx++) {
		if (node_inx == 0)
			info("--------------------");
		info("cpu_array_value[%d]:%u reps:%u", node_inx,
		     job_resrcs_ptr->cpu_array_value[node_inx],
		     job_resrcs_ptr->cpu_array_reps[node_inx]);
	}
	info("====================");
}

extern void pack_job_resources(job_resources_t *job_resrcs_ptr, buf_t *buffer,
			       uint16_t protocol_version)
{
	int i;
	uint32_t sock_recs = 0;

	if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		if (job_resrcs_ptr == NULL) {
			uint32_t empty = NO_VAL;
			pack32(empty, buffer);
			return;
		}

		pack32(job_resrcs_ptr->nhosts, buffer);
		pack32(job_resrcs_ptr->ncpus, buffer);
		pack32(job_resrcs_ptr->node_req, buffer);
		packstr(job_resrcs_ptr->nodes, buffer);
		pack8(job_resrcs_ptr->whole_node, buffer);
		pack16(job_resrcs_ptr->threads_per_core, buffer);
		pack16(job_resrcs_ptr->cr_type, buffer);

		if (job_resrcs_ptr->cpu_array_reps)
			pack32_array(job_resrcs_ptr->cpu_array_reps,
				     job_resrcs_ptr->cpu_array_cnt, buffer);
		else
			pack32_array(job_resrcs_ptr->cpu_array_reps, 0, buffer);

		if (job_resrcs_ptr->cpu_array_value)
			pack16_array(job_resrcs_ptr->cpu_array_value,
				     job_resrcs_ptr->cpu_array_cnt, buffer);
		else
			pack16_array(job_resrcs_ptr->cpu_array_value,
				     0, buffer);

		if (job_resrcs_ptr->cpus)
			pack16_array(job_resrcs_ptr->cpus,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack16_array(job_resrcs_ptr->cpus, 0, buffer);

		if (job_resrcs_ptr->cpus_used)
			pack16_array(job_resrcs_ptr->cpus_used,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack16_array(job_resrcs_ptr->cpus_used, 0, buffer);

		if (job_resrcs_ptr->memory_allocated)
			pack64_array(job_resrcs_ptr->memory_allocated,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack64_array(job_resrcs_ptr->memory_allocated,
				     0, buffer);

		if (job_resrcs_ptr->memory_used)
			pack64_array(job_resrcs_ptr->memory_used,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack64_array(job_resrcs_ptr->memory_used, 0, buffer);

		xassert(job_resrcs_ptr->cores_per_socket);
		xassert(job_resrcs_ptr->sock_core_rep_count);
		xassert(job_resrcs_ptr->sockets_per_node);

		for (i=0; i < job_resrcs_ptr->nhosts; i++) {
			sock_recs += job_resrcs_ptr->
				     sock_core_rep_count[i];
			if (sock_recs >= job_resrcs_ptr->nhosts)
				break;
		}
		i++;
		pack16_array(job_resrcs_ptr->sockets_per_node,
			     (uint32_t) i, buffer);
		pack16_array(job_resrcs_ptr->cores_per_socket,
			     (uint32_t) i, buffer);
		pack32_array(job_resrcs_ptr->sock_core_rep_count,
			     (uint32_t) i, buffer);

		xassert(job_resrcs_ptr->core_bitmap);
		xassert(job_resrcs_ptr->core_bitmap_used);
		pack_bit_str_hex(job_resrcs_ptr->core_bitmap, buffer);
		pack_bit_str_hex(job_resrcs_ptr->core_bitmap_used,
				 buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (job_resrcs_ptr == NULL) {
			uint32_t empty = NO_VAL;
			pack32(empty, buffer);
			return;
		}

		pack32(job_resrcs_ptr->nhosts, buffer);
		pack32(job_resrcs_ptr->ncpus, buffer);
		pack32(job_resrcs_ptr->node_req, buffer);
		packstr(job_resrcs_ptr->nodes, buffer);
		pack8(job_resrcs_ptr->whole_node, buffer);
		pack16(job_resrcs_ptr->threads_per_core, buffer);
		pack16(job_resrcs_ptr->cr_type, buffer);

		if (job_resrcs_ptr->cpu_array_reps)
			pack32_array(job_resrcs_ptr->cpu_array_reps,
				     job_resrcs_ptr->cpu_array_cnt, buffer);
		else
			pack32_array(job_resrcs_ptr->cpu_array_reps, 0, buffer);

		if (job_resrcs_ptr->cpu_array_value)
			pack16_array(job_resrcs_ptr->cpu_array_value,
				     job_resrcs_ptr->cpu_array_cnt, buffer);
		else
			pack16_array(job_resrcs_ptr->cpu_array_value,
				     0, buffer);

		if (job_resrcs_ptr->cpus)
			pack16_array(job_resrcs_ptr->cpus,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack16_array(job_resrcs_ptr->cpus, 0, buffer);

		if (job_resrcs_ptr->cpus_used)
			pack16_array(job_resrcs_ptr->cpus_used,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack16_array(job_resrcs_ptr->cpus_used, 0, buffer);

		if (job_resrcs_ptr->memory_allocated)
			pack64_array(job_resrcs_ptr->memory_allocated,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack64_array(job_resrcs_ptr->memory_allocated,
				     0, buffer);

		if (job_resrcs_ptr->memory_used)
			pack64_array(job_resrcs_ptr->memory_used,
				     job_resrcs_ptr->nhosts, buffer);
		else
			pack64_array(job_resrcs_ptr->memory_used, 0, buffer);

		xassert(job_resrcs_ptr->cores_per_socket);
		xassert(job_resrcs_ptr->sock_core_rep_count);
		xassert(job_resrcs_ptr->sockets_per_node);

		for (i=0; i < job_resrcs_ptr->nhosts; i++) {
			sock_recs += job_resrcs_ptr->
				     sock_core_rep_count[i];
			if (sock_recs >= job_resrcs_ptr->nhosts)
				break;
		}
		i++;
		pack16_array(job_resrcs_ptr->sockets_per_node,
			     (uint32_t) i, buffer);
		pack16_array(job_resrcs_ptr->cores_per_socket,
			     (uint32_t) i, buffer);
		pack32_array(job_resrcs_ptr->sock_core_rep_count,
			     (uint32_t) i, buffer);

		xassert(job_resrcs_ptr->core_bitmap);
		xassert(job_resrcs_ptr->core_bitmap_used);
		pack_bit_str_hex(job_resrcs_ptr->core_bitmap, buffer);
		pack_bit_str_hex(job_resrcs_ptr->core_bitmap_used,
				 buffer);
	} else {
		error("pack_job_resources: protocol_version %hu not supported",
		      protocol_version);
	}
}

extern int unpack_job_resources(job_resources_t **job_resrcs_pptr,
				buf_t *buffer, uint16_t protocol_version)
{
	char *bit_fmt = NULL;
	uint32_t empty, tmp32;
	job_resources_t *job_resrcs;

	xassert(job_resrcs_pptr);
	if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		safe_unpack32(&empty, buffer);
		if (empty == NO_VAL) {
			*job_resrcs_pptr = NULL;
			return SLURM_SUCCESS;
		}

		job_resrcs = xmalloc(sizeof(struct job_resources));
		job_resrcs->nhosts = empty;
		safe_unpack32(&job_resrcs->ncpus, buffer);
		safe_unpack32(&job_resrcs->node_req, buffer);
		safe_unpackstr_xmalloc(&job_resrcs->nodes, &tmp32, buffer);
		safe_unpack8(&job_resrcs->whole_node, buffer);
		safe_unpack16(&job_resrcs->threads_per_core, buffer);
		safe_unpack16(&job_resrcs->cr_type, buffer);

		safe_unpack32_array(&job_resrcs->cpu_array_reps,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpu_array_reps);
		job_resrcs->cpu_array_cnt = tmp32;

		safe_unpack16_array(&job_resrcs->cpu_array_value,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpu_array_value);

		if (tmp32 != job_resrcs->cpu_array_cnt)
			goto unpack_error;

		safe_unpack16_array(&job_resrcs->cpus, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpus);
		if (tmp32 != job_resrcs->nhosts)
			goto unpack_error;
		safe_unpack16_array(&job_resrcs->cpus_used, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpus_used);

		safe_unpack64_array(&job_resrcs->memory_allocated,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->memory_allocated);
		safe_unpack64_array(&job_resrcs->memory_used, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->memory_used);

		safe_unpack16_array(&job_resrcs->sockets_per_node,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->sockets_per_node);
		safe_unpack16_array(&job_resrcs->cores_per_socket,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cores_per_socket);
		safe_unpack32_array(&job_resrcs->sock_core_rep_count,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->sock_core_rep_count);

		unpack_bit_str_hex(&job_resrcs->core_bitmap, buffer);
		unpack_bit_str_hex(&job_resrcs->core_bitmap_used,
				   buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&empty, buffer);
		if (empty == NO_VAL) {
			*job_resrcs_pptr = NULL;
			return SLURM_SUCCESS;
		}

		job_resrcs = xmalloc(sizeof(struct job_resources));
		job_resrcs->nhosts = empty;
		safe_unpack32(&job_resrcs->ncpus, buffer);
		safe_unpack32(&job_resrcs->node_req, buffer);
		safe_unpackstr_xmalloc(&job_resrcs->nodes, &tmp32, buffer);
		safe_unpack8(&job_resrcs->whole_node, buffer);
		safe_unpack16(&job_resrcs->threads_per_core, buffer);
		safe_unpack16(&job_resrcs->cr_type, buffer);

		safe_unpack32_array(&job_resrcs->cpu_array_reps,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpu_array_reps);
		job_resrcs->cpu_array_cnt = tmp32;

		safe_unpack16_array(&job_resrcs->cpu_array_value,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpu_array_value);

		if (tmp32 != job_resrcs->cpu_array_cnt)
			goto unpack_error;

		safe_unpack16_array(&job_resrcs->cpus, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpus);
		if (tmp32 != job_resrcs->nhosts)
			goto unpack_error;
		safe_unpack16_array(&job_resrcs->cpus_used, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cpus_used);

		safe_unpack64_array(&job_resrcs->memory_allocated,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->memory_allocated);
		safe_unpack64_array(&job_resrcs->memory_used, &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->memory_used);

		safe_unpack16_array(&job_resrcs->sockets_per_node,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->sockets_per_node);
		safe_unpack16_array(&job_resrcs->cores_per_socket,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->cores_per_socket);
		safe_unpack32_array(&job_resrcs->sock_core_rep_count,
				    &tmp32, buffer);
		if (tmp32 == 0)
			xfree(job_resrcs->sock_core_rep_count);

		unpack_bit_str_hex(&job_resrcs->core_bitmap, buffer);
		unpack_bit_str_hex(&job_resrcs->core_bitmap_used,
				   buffer);
	} else {
		error("unpack_job_resources: protocol_version %hu not "
		      "supported", protocol_version);
		goto unpack_error;
	}

	*job_resrcs_pptr = job_resrcs;
	return SLURM_SUCCESS;

unpack_error:
	error("unpack_job_resources: unpack error");
	free_job_resources(&job_resrcs);
	xfree(bit_fmt);
	*job_resrcs_pptr = NULL;
	return SLURM_ERROR;
}

extern int get_job_resources_offset(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id, uint16_t socket_id,
				    uint16_t core_id)
{
	int i, bit_inx = 0;

	xassert(job_resrcs_ptr);

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				job_resrcs_ptr->sock_core_rep_count[i];
			node_id -= job_resrcs_ptr->sock_core_rep_count[i];
		} else if (socket_id >= job_resrcs_ptr->sockets_per_node[i]) {
			error("get_job_resrcs_bit: socket_id >= socket_cnt "
			      "(%u >= %u)", socket_id,
			      job_resrcs_ptr->sockets_per_node[i]);
			return -1;
		} else if (core_id >= job_resrcs_ptr->cores_per_socket[i]) {
			error("get_job_resrcs_bit: core_id >= core_cnt "
			      "(%u >= %u)", core_id,
			      job_resrcs_ptr->cores_per_socket[i]);
			return -1;
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				node_id;
			bit_inx += job_resrcs_ptr->cores_per_socket[i] *
				socket_id;
			bit_inx += core_id;
			break;
		}
	}
	i = bit_size(job_resrcs_ptr->core_bitmap);
	if (bit_inx >= i) {
		error("get_job_resources_bit: offset >= bitmap size "
		      "(%d >= %d)", bit_inx, i);
		return -1;
	}

	return bit_inx;
}

extern int get_job_resources_bit(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t socket_id,
				 uint16_t core_id)
{
	int bit_inx = get_job_resources_offset(job_resrcs_ptr, node_id,
					       socket_id, core_id);
	if (bit_inx < 0)
		return SLURM_ERROR;

	return bit_test(job_resrcs_ptr->core_bitmap, bit_inx);
}

extern int set_job_resources_bit(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t socket_id,
				 uint16_t core_id)
{
	int bit_inx = get_job_resources_offset(job_resrcs_ptr, node_id,
					       socket_id, core_id);
	if (bit_inx < 0)
		return SLURM_ERROR;

	bit_set(job_resrcs_ptr->core_bitmap, bit_inx);
	return SLURM_SUCCESS;
}

/* For every core bitmap and core_bitmap_used set in the "from" resources
 * structure at from_node_offset, set the corresponding bit in the "new"
 * resources structure at new_node_offset */
extern int job_resources_bits_copy(job_resources_t *new_job_resrcs_ptr,
				   uint16_t new_node_offset,
				   job_resources_t *from_job_resrcs_ptr,
				   uint16_t from_node_offset)
{
	int i, rc = SLURM_SUCCESS;
	int new_core_cnt = 0, from_core_cnt = 0;

	xassert(new_job_resrcs_ptr);
	xassert(from_job_resrcs_ptr);

	if (new_node_offset >= new_job_resrcs_ptr->nhosts) {
		error("job_resources_bits_move: new_node_offset invalid "
		      "(%u is 0 or >=%u)", new_node_offset,
		      new_job_resrcs_ptr->nhosts);
		return SLURM_ERROR;
	}
	for (i = 0; i < new_job_resrcs_ptr->nhosts; i++) {
		if (new_job_resrcs_ptr->sock_core_rep_count[i] <=
		    new_node_offset) {
			new_node_offset -= new_job_resrcs_ptr->
					   sock_core_rep_count[i];
		} else {
			new_core_cnt = new_job_resrcs_ptr->sockets_per_node[i] *
				new_job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}

	if (from_node_offset >= from_job_resrcs_ptr->nhosts) {
		error("job_resources_bits_move: from_node_offset invalid "
		      "(%u is 0 or >=%u)", from_node_offset,
		      from_job_resrcs_ptr->nhosts);
		return SLURM_ERROR;
	}
	for (i = 0; i < from_job_resrcs_ptr->nhosts; i++) {
		if (from_job_resrcs_ptr->sock_core_rep_count[i] <=
		    from_node_offset) {
			from_node_offset -= from_job_resrcs_ptr->
					    sock_core_rep_count[i];
		} else {
			from_core_cnt = from_job_resrcs_ptr->sockets_per_node[i] *
				from_job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}

	if (new_core_cnt != from_core_cnt) {
		error("job_resources_bits_move: core_cnt mis-match (%d != %d)",
		      new_core_cnt, from_core_cnt);
		rc = SLURM_ERROR;
	}

	bit_or(new_job_resrcs_ptr->core_bitmap,
	       from_job_resrcs_ptr->core_bitmap);
	bit_or(new_job_resrcs_ptr->core_bitmap_used,
	       from_job_resrcs_ptr->core_bitmap_used);

	return rc;
}

/*
 * AND two job_resources structures.
 * Every node/core set in job_resrcs1_ptr and job_resrcs2_ptr is set in the
 * resulting job_resrcs1_ptr data structure
 * RET SLURM_SUCCESS or an error code
 */
extern int job_resources_and(job_resources_t *job_resrcs1_ptr,
			     job_resources_t *job_resrcs2_ptr)
{
	int i, i_first, i_last, j;
	int node_cnt;
	int sock_core_cnt1 = 0, sock_core_cnt2 = 0;
	int so_co_off1 = 0, so_co_off2 = 0;
	int core_cnt, core_cnt1, core_cnt2;;
	int core_off1 = 0, core_off2 = 0;
	int rc = SLURM_SUCCESS;

	xassert(job_resrcs1_ptr);
	xassert(job_resrcs2_ptr);
	xassert(job_resrcs1_ptr->core_bitmap);
	xassert(job_resrcs2_ptr->core_bitmap);
	xassert(job_resrcs1_ptr->node_bitmap);
	xassert(job_resrcs2_ptr->node_bitmap);

	/* Allocate space for merged arrays */
	node_cnt = bit_size(job_resrcs1_ptr->node_bitmap);
	i = bit_size(job_resrcs2_ptr->node_bitmap);
	if (node_cnt != i) {
		error("%s: node_bitmap sizes differ (%d != %d)", __func__,
		      node_cnt, i);
		rc = SLURM_ERROR;
		node_cnt = MIN(node_cnt, i);
	}

	/* Set the values in data structure used for merging */
	i_first = bit_ffs(job_resrcs1_ptr->node_bitmap);
	i = bit_ffs(job_resrcs2_ptr->node_bitmap);
	if ((i != -1) && (i < i_first))
		i_first = i;
	i_last = bit_fls(job_resrcs1_ptr->node_bitmap);
	i = bit_fls(job_resrcs2_ptr->node_bitmap);
	if ((i != -1) && (i > i_last))
		i_last = i;
	if (i_last >= node_cnt)
		i_last = node_cnt - 1;
	if (i_last == -1)	/* node_bitmap empty in both inputs */
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		bool match1 = false, match2 = false;
		if (bit_test(job_resrcs1_ptr->node_bitmap, i))
			match1 = true;
		if (bit_test(job_resrcs2_ptr->node_bitmap, i))
			match2 = true;
		if (!match1 && !match2)	/* Unused node */
			continue;
		if (match1 && match2) {	/* Merge (AND) core_bitmaps */
			if (++sock_core_cnt1 >
			    job_resrcs1_ptr->sock_core_rep_count[so_co_off1]) {
				sock_core_cnt1 = 0;
				so_co_off1++;
			}
			if (++sock_core_cnt2 >
			   job_resrcs2_ptr->sock_core_rep_count[so_co_off2]) {
				sock_core_cnt2 = 0;
				so_co_off2++;
			}

			core_cnt1 =
				job_resrcs1_ptr->cores_per_socket[so_co_off1] *
				job_resrcs1_ptr->sockets_per_node[so_co_off1];
			core_cnt2 =
				job_resrcs2_ptr->cores_per_socket[so_co_off2] *
				job_resrcs2_ptr->sockets_per_node[so_co_off2];
			if (core_cnt1 != core_cnt2) {
				error("%s: Inconsistent socket/core count for node_inx %d (%d != %d)",
				      __func__, i, core_cnt1, core_cnt2);
				rc = SLURM_ERROR;
			}
			core_cnt = MIN(core_cnt1, core_cnt2);
			for (j = 0; j < core_cnt; j++) {
				if (bit_test(job_resrcs1_ptr->core_bitmap,
					     core_off1 + j) &&
				    !bit_test(job_resrcs2_ptr->core_bitmap,
					      core_off2 + j)) {
					bit_clear(job_resrcs1_ptr->core_bitmap,
						  core_off1 + j);
				}
			}
			core_off1 += core_cnt1;
			core_off2 += core_cnt2;
		} else if (match1) {
			if (++sock_core_cnt1 >
			    job_resrcs1_ptr->sock_core_rep_count[so_co_off1]) {
				sock_core_cnt1 = 0;
				so_co_off1++;
			}
			core_cnt1 =
				job_resrcs1_ptr->cores_per_socket[so_co_off1] *
				job_resrcs1_ptr->sockets_per_node[so_co_off1];
			for (j = 0; j < core_cnt1; j++) {
				bit_clear(job_resrcs1_ptr->core_bitmap,
					  core_off1 + j);
			}
			core_off1 += core_cnt1;
		} else { /* match2 only */
			if (++sock_core_cnt2 >
			    job_resrcs2_ptr->sock_core_rep_count[so_co_off2]) {
				sock_core_cnt2 = 0;
				so_co_off2++;
			}
			core_cnt2 =
				job_resrcs2_ptr->cores_per_socket[so_co_off2] *
				job_resrcs2_ptr->sockets_per_node[so_co_off2];
			core_off2 += core_cnt2;
		}
	}

	return rc;
}

/*
 * OR two job_resources structures.
 * Every node/core set in job_resrcs1_ptr or job_resrcs2_ptr is set in the
 * resulting job_resrcs1_ptr data structure.
 * NOTE: Only these job_resources_t fields in job_resrcs1_ptr are changed:
 *	core_bitmap, node_bitmap
 *	cores_per_socket, sockets_per_node, sock_core_rep_count, nhosts
 * RET SLURM_SUCCESS or an error code, best effort operation happens on error
 */
extern int job_resources_or(job_resources_t *job_resrcs1_ptr,
			    job_resources_t *job_resrcs2_ptr)
{
	job_resources_t *job_resrcs_new;
	int i, i_first, i_last, j;
	int node_cnt, node_inx = -1;
	int sock_core_cnt1 = 0, sock_core_cnt2 = 0;
	int so_co_off1 = 0, so_co_off2 = 0;
	int core_cnt, core_cnt1, core_cnt2;
	int core_off = 0, core_off1 = 0, core_off2 = 0;
	int rc = SLURM_SUCCESS;

	xassert(job_resrcs1_ptr);
	xassert(job_resrcs2_ptr);
	xassert(job_resrcs1_ptr->core_bitmap);
	xassert(job_resrcs2_ptr->core_bitmap);
	xassert(job_resrcs1_ptr->node_bitmap);
	xassert(job_resrcs2_ptr->node_bitmap);

	/* Allocate space for merged arrays */
	job_resrcs_new = xmalloc(sizeof(job_resources_t));
	node_cnt = bit_size(job_resrcs1_ptr->node_bitmap);
	i = bit_size(job_resrcs2_ptr->node_bitmap);
	if (node_cnt != i) {
		error("%s: node_bitmap sizes differ (%d != %d)", __func__,
		      node_cnt, i);
		rc = SLURM_ERROR;
		node_cnt = MIN(node_cnt, i);
	}
	job_resrcs_new->node_bitmap = bit_alloc(node_cnt);
	i = bit_set_count(job_resrcs1_ptr->node_bitmap) +
	    bit_set_count(job_resrcs2_ptr->node_bitmap);
	job_resrcs_new->cores_per_socket = xcalloc(i, sizeof(uint32_t));
	job_resrcs_new->sockets_per_node = xcalloc(i, sizeof(uint32_t));
	job_resrcs_new->sock_core_rep_count = xcalloc(i, sizeof(uint32_t));
	i = bit_size(job_resrcs1_ptr->core_bitmap) +
	    bit_size(job_resrcs2_ptr->core_bitmap);
	job_resrcs_new->core_bitmap = bit_alloc(i);	/* May be over-sized */

	/* Set the values in data structure used for merging */
	i_first = bit_ffs(job_resrcs1_ptr->node_bitmap);
	i = bit_ffs(job_resrcs2_ptr->node_bitmap);
	if ((i != -1) && (i < i_first))
		i_first = i;
	i_last = bit_fls(job_resrcs1_ptr->node_bitmap);
	i = bit_fls(job_resrcs2_ptr->node_bitmap);
	if ((i != -1) && (i > i_last))
		i_last = i;
	if (i_last >= node_cnt)
		i_last = node_cnt - 1;
	if (i_last == -1)	/* node_bitmap empty in both inputs */
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		bool match1 = false, match2 = false;
		if (bit_test(job_resrcs1_ptr->node_bitmap, i))
			match1 = true;
		if (bit_test(job_resrcs2_ptr->node_bitmap, i))
			match2 = true;
		if (!match1 && !match2)	/* Unused node */
			continue;
		bit_set(job_resrcs_new->node_bitmap, i);
		node_inx++;
		if (match1 && match2) {	/* Merge (OR) core_bitmaps */
			if (++sock_core_cnt1 >
			    job_resrcs1_ptr->sock_core_rep_count[so_co_off1]) {
				sock_core_cnt1 = 0;
				so_co_off1++;
			}
			if (++sock_core_cnt2 >
			   job_resrcs2_ptr->sock_core_rep_count[so_co_off2]) {
				sock_core_cnt2 = 0;
				so_co_off2++;
			}

			job_resrcs_new->cores_per_socket[node_inx] =
				job_resrcs1_ptr->cores_per_socket[so_co_off1];
			job_resrcs_new->sockets_per_node[node_inx] =
				job_resrcs1_ptr->sockets_per_node[so_co_off1];

			core_cnt1 =
				job_resrcs1_ptr->cores_per_socket[so_co_off1] *
				job_resrcs1_ptr->sockets_per_node[so_co_off1];
			core_cnt2 =
				job_resrcs2_ptr->cores_per_socket[so_co_off2] *
				job_resrcs2_ptr->sockets_per_node[so_co_off2];
			if (core_cnt1 != core_cnt2) {
				error("%s: Inconsistent socket/core count for node_inx %d (%d != %d)",
				      __func__, i, core_cnt1, core_cnt2);
				rc = SLURM_ERROR;
			}
			core_cnt = MIN(core_cnt1, core_cnt2);
			for (j = 0; j < core_cnt; j++) {
				if (bit_test(job_resrcs1_ptr->core_bitmap,
					     core_off1 + j) ||
				    bit_test(job_resrcs2_ptr->core_bitmap,
					     core_off2 + j)) {
					bit_set(job_resrcs_new->core_bitmap,
						core_off + j);
				}
			}
			core_off  += core_cnt;
			core_off1 += core_cnt1;
			core_off2 += core_cnt2;
		} else if (match1) {		/* Copy core bitmap */
			if (++sock_core_cnt1 >
			    job_resrcs1_ptr->sock_core_rep_count[so_co_off1]) {
				sock_core_cnt1 = 0;
				so_co_off1++;
			}
			job_resrcs_new->cores_per_socket[node_inx] =
				job_resrcs1_ptr->cores_per_socket[so_co_off1];
			job_resrcs_new->sockets_per_node[node_inx] =
				job_resrcs1_ptr->sockets_per_node[so_co_off1];
			core_cnt1 = job_resrcs_new->cores_per_socket[node_inx] *
				    job_resrcs_new->sockets_per_node[node_inx];
			for (j = 0; j < core_cnt1; j++) {
				if (bit_test(job_resrcs1_ptr->core_bitmap,
					     core_off1 + j)) {
					bit_set(job_resrcs_new->core_bitmap,
						core_off + j);
				}
			}

			core_off  += core_cnt1;
			core_off1 += core_cnt1;
		} else { /* match2 only */	/* Copy core bitmap */
			if (++sock_core_cnt2 >
			    job_resrcs2_ptr->sock_core_rep_count[so_co_off2]) {
				sock_core_cnt2 = 0;
				so_co_off2++;
			}
			job_resrcs_new->cores_per_socket[node_inx] =
				job_resrcs2_ptr->cores_per_socket[so_co_off2];
			job_resrcs_new->sockets_per_node[node_inx] =
				job_resrcs2_ptr->sockets_per_node[so_co_off2];
			core_cnt2 = job_resrcs_new->cores_per_socket[node_inx] *
				    job_resrcs_new->sockets_per_node[node_inx];
			for (j = 0; j < core_cnt2; j++) {
				if (bit_test(job_resrcs2_ptr->core_bitmap,
					     core_off2 + j)) {
					bit_set(job_resrcs_new->core_bitmap,
						core_off + j);
				}
			}

			core_off += core_cnt2;
			core_off2 += core_cnt2;
		}
		job_resrcs_new->sock_core_rep_count[node_inx] = 1;
	}

	/* Update data structure fields as needed */
	job_resrcs1_ptr->nhosts = node_inx + 1;
	FREE_NULL_BITMAP(job_resrcs1_ptr->core_bitmap);
	job_resrcs1_ptr->core_bitmap = job_resrcs_new->core_bitmap;
	FREE_NULL_BITMAP(job_resrcs1_ptr->node_bitmap);
	job_resrcs1_ptr->node_bitmap = job_resrcs_new->node_bitmap;
	xfree(job_resrcs1_ptr->cores_per_socket);
	job_resrcs1_ptr->cores_per_socket = job_resrcs_new->cores_per_socket;
	xfree(job_resrcs1_ptr->sock_core_rep_count);
	job_resrcs1_ptr->sock_core_rep_count =
		job_resrcs_new->sock_core_rep_count;
	xfree(job_resrcs1_ptr->sockets_per_node);
	job_resrcs1_ptr->sockets_per_node = job_resrcs_new->sockets_per_node;
	xfree(job_resrcs_new);

	return rc;
}

extern int get_job_resources_node(job_resources_t *job_resrcs_ptr,
				  uint32_t node_id)
{
	int i, bit_inx = 0, core_cnt = 0;

	xassert(job_resrcs_ptr);

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				job_resrcs_ptr->sock_core_rep_count[i];
			node_id -= job_resrcs_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				node_id;
			core_cnt = job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}
	if (core_cnt < 1) {
		error("get_job_resources_node: core_cnt=0");
		return 0;
	}
	i = bit_size(job_resrcs_ptr->core_bitmap);
	if ((bit_inx + core_cnt) > i) {
		error("get_job_resources_node: offset > bitmap size "
		      "(%d >= %d)", (bit_inx + core_cnt), i);
		return 0;
	}

	for (i=0; i<core_cnt; i++) {
		if (bit_test(job_resrcs_ptr->core_bitmap, bit_inx++))
			return 1;
	}
	return 0;
}

static int _change_job_resources_node(job_resources_t *job_resrcs_ptr,
				      uint32_t node_id, bool new_value)
{
	int i, bit_inx = 0, core_cnt = 0;

	xassert(job_resrcs_ptr);

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				job_resrcs_ptr->sock_core_rep_count[i];
			node_id -= job_resrcs_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				node_id;
			core_cnt = job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}
	if (core_cnt < 1) {
		error("_change_job_resources_node: core_cnt=0");
		return SLURM_ERROR;
	}

	i = bit_size(job_resrcs_ptr->core_bitmap);
	if ((bit_inx + core_cnt) > i) {
		error("_change_job_resources_node: offset > bitmap size "
		      "(%d >= %d)", (bit_inx + core_cnt), i);
		return SLURM_ERROR;
	}

	for (i=0; i<core_cnt; i++) {
		if (new_value)
			bit_set(job_resrcs_ptr->core_bitmap, bit_inx++);
		else
			bit_clear(job_resrcs_ptr->core_bitmap, bit_inx++);
	}

	return SLURM_SUCCESS;
}

extern int set_job_resources_node(job_resources_t *job_resrcs_ptr,
				  uint32_t node_id)
{
	return _change_job_resources_node(job_resrcs_ptr, node_id, true);
}

extern int clear_job_resources_node(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id)
{
	return _change_job_resources_node(job_resrcs_ptr, node_id, false);
}

/* Completely remove specified node from job resources structure */
extern int extract_job_resources_node(job_resources_t *job, uint32_t node_id)
{
	int i, n;
	int bit_inx = 0, core_cnt = 0, host_cnt, len, node_inx = node_id;

	xassert(job);

	/* Modify core/socket counter arrays to remove this node */
	host_cnt = job->nhosts;
	for (i = 0; i < job->nhosts; i++) {
		host_cnt -= job->sock_core_rep_count[i];
		if (job->sock_core_rep_count[i] <= node_inx) {
			bit_inx += job->sockets_per_node[i] *
				   job->cores_per_socket[i] *
				   job->sock_core_rep_count[i];
			node_inx -= job->sock_core_rep_count[i];
		} else {
			bit_inx += job->sockets_per_node[i] *
				   job->cores_per_socket[i] * node_inx;
			core_cnt = job->sockets_per_node[i] *
				   job->cores_per_socket[i];
			job->sock_core_rep_count[i]--;
			if (job->sock_core_rep_count[i] == 0) {
				for ( ; host_cnt > 0; i++) {
					job->cores_per_socket[i] =
						job->cores_per_socket[i+1];
					job->sock_core_rep_count[i] =
						job->sock_core_rep_count[i+1];
					job->sockets_per_node[i] =
						job->sockets_per_node[i+1];
					host_cnt -= job->sock_core_rep_count[i];
				}
			}
			break;
		}
	}
	if (core_cnt < 1) {
		error("%s: core_cnt=0", __func__);
		return SLURM_ERROR;
	}

	/* Shift core_bitmap contents and shrink it to remove this node */
	len = bit_size(job->core_bitmap);
	for (i = bit_inx; (i + core_cnt) < len; i++) {
		if (bit_test(job->core_bitmap, i + core_cnt))
			bit_set(job->core_bitmap, i);
		else
			bit_clear(job->core_bitmap, i);
		if (!job->core_bitmap_used)
			;
		else if (bit_test(job->core_bitmap_used, i + core_cnt))
			bit_set(job->core_bitmap_used, i);
		else
			bit_clear(job->core_bitmap_used, i);
	}
	bit_realloc(job->core_bitmap, len - core_cnt);
	if (job->core_bitmap_used)
		bit_realloc(job->core_bitmap_used, len - core_cnt);

	/* Shift cpus, cpus_used, memory_allocated, and memory_used arrays */
	for (i = 0, n = -1; next_node_bitmap(job->node_bitmap, &i); i++) {
		if (++n == node_id) {
			bit_clear(job->node_bitmap, i);
			break;
		}
	}
	job->nhosts--;
	for (i = n; i < job->nhosts; i++) {
		job->cpus[i] = job->cpus[i+1];
		job->cpus_used[i] = job->cpus_used[i+1];
		job->memory_allocated[i] = job->memory_allocated[i+1];
		job->memory_used[i] = job->memory_used[i+1];
	}

	xfree(job->nodes);
	job->nodes = bitmap2node_name(job->node_bitmap);
	job->ncpus = build_job_resources_cpu_array(job);

	return SLURM_SUCCESS;
}

/* Return the count of core bitmaps set for the specific node */
extern int count_job_resources_node(job_resources_t *job_resrcs_ptr,
				    uint32_t node_id)
{
	int i, bit_inx = 0, core_cnt = 0;
	int set_cnt = 0;

	xassert(job_resrcs_ptr);

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				job_resrcs_ptr->sock_core_rep_count[i];
			node_id -= job_resrcs_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				node_id;
			core_cnt = job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}
	if (core_cnt < 1) {
		error("count_job_resources_node: core_cnt=0");
		return set_cnt;
	}

	i = bit_size(job_resrcs_ptr->core_bitmap);
	if ((bit_inx + core_cnt) > i) {
		error("count_job_resources_node: offset > bitmap size "
		      "(%d >= %d)", (bit_inx + core_cnt), i);
		return set_cnt;
	}

	for (i=0; i<core_cnt; i++) {
		if (bit_test(job_resrcs_ptr->core_bitmap, bit_inx++))
			set_cnt++;
	}

	return set_cnt;
}

/* Return a copy of core_bitmap only for the specific node */
extern bitstr_t * copy_job_resources_node(job_resources_t *job_resrcs_ptr,
					  uint32_t node_id)
{
	int i, bit_inx = 0, core_cnt = 0;
	bitstr_t *core_bitmap;

	xassert(job_resrcs_ptr);

	for (i = 0; i < job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				   job_resrcs_ptr->cores_per_socket[i] *
				   job_resrcs_ptr->sock_core_rep_count[i];
			node_id -= job_resrcs_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[i] *
				   job_resrcs_ptr->cores_per_socket[i] *
				   node_id;
			core_cnt = job_resrcs_ptr->sockets_per_node[i] *
				   job_resrcs_ptr->cores_per_socket[i];
			break;
		}
	}
	if (core_cnt < 1) {
		error("copy_job_resources_node: core_cnt=0");
		return NULL;
	}

	i = bit_size(job_resrcs_ptr->core_bitmap);
	if ((bit_inx + core_cnt) > i) {
		error("copy_job_resources_node: offset > bitmap size "
		      "(%d >= %d)", (bit_inx + core_cnt), i);
		return NULL;
	}

	core_bitmap = bit_alloc(core_cnt);
	for (i = 0; i < core_cnt; i++) {
		if (bit_test(job_resrcs_ptr->core_bitmap, bit_inx++))
			bit_set(core_bitmap, i);
	}

	return core_bitmap;
}

extern int get_job_resources_cnt(job_resources_t *job_resrcs_ptr,
				 uint32_t node_id, uint16_t *socket_cnt,
				 uint16_t *cores_per_socket_cnt)
{
	int i, node_inx = -1;

	xassert(socket_cnt);
	xassert(cores_per_socket_cnt);
	xassert(job_resrcs_ptr->cores_per_socket);
	xassert(job_resrcs_ptr->sock_core_rep_count);
	xassert(job_resrcs_ptr->sockets_per_node);

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		node_inx += job_resrcs_ptr->sock_core_rep_count[i];
		if (node_id <= node_inx) {
			*cores_per_socket_cnt = job_resrcs_ptr->
				cores_per_socket[i];
			*socket_cnt = job_resrcs_ptr->sockets_per_node[i];
			return SLURM_SUCCESS;
		}
	}

	error("get_job_resources_cnt: invalid node_id: %u", node_id);
	*cores_per_socket_cnt = 0;
	*socket_cnt = 0;
	return SLURM_ERROR;
}

/* Get CPU count for a specific node_id (zero origin), return -1 on error */
extern int get_job_resources_cpus(job_resources_t *job_resrcs_ptr,
				  uint32_t node_id)
{
	xassert(job_resrcs_ptr->cpus);
	if (node_id >= job_resrcs_ptr->nhosts)
		return -1;
	return (int) job_resrcs_ptr->cpus[node_id];
}

/*
 * Test if job can fit into the given full-length core_bitmap
 * IN job_resrcs_ptr - resources allocated to a job
 * IN full_bitmap - bitmap of available CPUs
 * IN bits_per_node - bits per node in the full_bitmap
 * RET 1 on success, 0 otherwise
 */
extern int job_fits_into_cores(job_resources_t *job_resrcs_ptr,
			       bitstr_t *full_bitmap,
			       const uint16_t *bits_per_node)
{
	int full_node_inx = 0, full_bit_inx  = 0, job_bit_inx  = 0, i;

	if (!full_bitmap)
		return 1;

	for (full_node_inx = 0;
	     next_node_bitmap(job_resrcs_ptr->node_bitmap, &full_node_inx);
	     full_node_inx++) {
		full_bit_inx = cr_node_cores_offset[full_node_inx];
		for (i = 0; i < bits_per_node[full_node_inx]; i++) {
			if (!bit_test(full_bitmap, full_bit_inx + i))
				continue;
			if ((job_resrcs_ptr->whole_node == 1) ||
				bit_test(job_resrcs_ptr->core_bitmap,
					job_bit_inx + i)) {
				return 0;
			}
		}
		job_bit_inx += bits_per_node[full_node_inx];
	}
	return 1;
}

/*
 * Add job to full-length core_bitmap
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 * IN bits_per_node - bits per node in the full_bitmap
 * RET 1 on success, 0 otherwise
 */
extern void add_job_to_cores(job_resources_t *job_resrcs_ptr,
			     bitstr_t **full_core_bitmap,
			     const uint16_t *bits_per_node)
{
	int full_node_inx = 0;
	int job_bit_inx  = 0, full_bit_inx  = 0, i;

	if (!job_resrcs_ptr->core_bitmap)
		return;

	/* add the job to the row_bitmap */
	if (*full_core_bitmap == NULL) {
		uint32_t size = 0;
		for (i = 0; next_node(&i); i++)
			size += bits_per_node[i];
		*full_core_bitmap = bit_alloc(size);
	}

	for (full_node_inx = 0;
	     next_node_bitmap(job_resrcs_ptr->node_bitmap, &full_node_inx);
	     full_node_inx++) {
		full_bit_inx = cr_node_cores_offset[full_node_inx];
		for (i = 0; i < bits_per_node[full_node_inx]; i++) {
			if ((job_resrcs_ptr->whole_node != 1) &&
			    !bit_test(job_resrcs_ptr->core_bitmap,
				      job_bit_inx + i))
				continue;
			bit_set(*full_core_bitmap, full_bit_inx + i);
		}
		job_bit_inx += bits_per_node[full_node_inx];
	}
}

/*
 * Given a job pointer and a global node index, return the index of that
 * node in the job_resrcs_ptr->cpus. Return -1 if invalid
 */
extern int job_resources_node_inx_to_cpu_inx(job_resources_t *job_resrcs_ptr,
					     int node_inx)
{
	int node_offset;

	/* Test for error cases */
	if (!job_resrcs_ptr || !job_resrcs_ptr->node_bitmap) {
		error("%s: no job_resrcs or node_bitmap", __func__);
		return -1;
	}
	if (!bit_test(job_resrcs_ptr->node_bitmap, node_inx)) {
		/*
		 * This could happen if a job shrinks and epilog completes on
		 * node no longer in this job's allocation
		 */
		char node_str[128];
		bit_fmt(node_str, sizeof(node_str),job_resrcs_ptr->node_bitmap);
		error("%s: Invalid node_inx:%d node_bitmap:%s", __func__,
		      node_inx, node_str);
		return -1;
	}
	if (job_resrcs_ptr->cpu_array_cnt == 0) {
		error("%s: Invalid cpu_array_cnt", __func__);
		return -1;
	}

	/* Only one record, no need to search */
	if (job_resrcs_ptr->nhosts == 1)
		return 0;

	node_offset = bit_set_count_range(job_resrcs_ptr->node_bitmap, 0,
					  node_inx);

	if (node_offset >= job_resrcs_ptr->nhosts) {
		error("%s: Found %d of %d nodes", __func__,
		      job_resrcs_ptr->nhosts, node_offset);
		return -1;
	}

	return node_offset;
}

extern uint16_t job_resources_get_node_cpu_cnt(job_resources_t *job_resrcs_ptr,
					       int job_node_inx,
					       int sys_node_inx)
{
	uint16_t cpu_count = job_resrcs_ptr->cpus[job_node_inx];

	if (((job_resrcs_ptr->cr_type & CR_CORE) ||
	     (job_resrcs_ptr->cr_type & CR_SOCKET)) &&
	    (job_resrcs_ptr->threads_per_core <
	     node_record_table_ptr[sys_node_inx]->tpc)) {
		cpu_count /= node_record_table_ptr[sys_node_inx]->tpc;
		cpu_count *= job_resrcs_ptr->threads_per_core;
	}

	return cpu_count;
}
