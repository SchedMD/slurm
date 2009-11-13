/*****************************************************************************\
 *  job_resources.c - functions to manage data structure identifying specific
 *	CPUs allocated to a job, step or partition
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/job_resources.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
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
 * rc = build_job_resources(job_resrcs_ptr, node_record_table_ptr,
 *			     slurmctld_conf.fast_schedule);
 */
extern int build_job_resources(job_resources_t *job_resrcs,
			       void *node_rec_table, uint16_t fast_schedule)
{
	int i, bitmap_len;
	int core_cnt = 0, sock_inx = -1;
	uint32_t cores, socks;
	struct node_record *node_ptr, *node_record_table;

	if (job_resrcs->node_bitmap == NULL) {
		error("build_job_resources: node_bitmap is NULL");
		return SLURM_ERROR;
	}

	node_record_table = (struct node_record *) node_rec_table;
	xfree(job_resrcs->sockets_per_node);
	xfree(job_resrcs->cores_per_socket);
	xfree(job_resrcs->sock_core_rep_count);
	job_resrcs->sockets_per_node = xmalloc(sizeof(uint16_t) *
					       job_resrcs->nhosts);
	job_resrcs->cores_per_socket = xmalloc(sizeof(uint16_t) *
					       job_resrcs->nhosts);
	job_resrcs->sock_core_rep_count = xmalloc(sizeof(uint32_t) *
						  job_resrcs->nhosts);

	bitmap_len = bit_size(job_resrcs->node_bitmap);
	for (i=0; i<bitmap_len; i++) {
		if (!bit_test(job_resrcs->node_bitmap, i))
			continue;
		node_ptr = node_record_table + i;
		if (fast_schedule) {
			socks = node_ptr->config_ptr->sockets;
			cores = node_ptr->config_ptr->cores;
		} else {
			socks = node_ptr->sockets;
			cores = node_ptr->cores;
		}
		if ((sock_inx < 0) ||
		    (socks != job_resrcs->sockets_per_node[sock_inx]) ||
		    (cores != job_resrcs->cores_per_socket[sock_inx])) {
			sock_inx++;
			job_resrcs->sockets_per_node[sock_inx] = socks;
			job_resrcs->cores_per_socket[sock_inx] = cores;
		}
		job_resrcs->sock_core_rep_count[sock_inx]++;
		core_cnt += (cores * socks);
	}
#ifndef HAVE_BG
	job_resrcs->core_bitmap      = bit_alloc(core_cnt);
	job_resrcs->core_bitmap_used = bit_alloc(core_cnt);
	if ((job_resrcs->core_bitmap == NULL) ||
	    (job_resrcs->core_bitmap_used == NULL))
		fatal("bit_alloc malloc failure");
#endif
	return SLURM_SUCCESS;
}

/* Rebuild cpu_array_cnt, cpu_array_value, and cpu_array_reps based upon the
 * values of nhosts and cpus in an existing data structure
 * Return total CPU count or -1 on error */
extern int build_job_resources_cpu_array(job_resources_t *job_resrcs_ptr)
{
	int cpu_count = 0, i;
	uint32_t last_cpu_cnt = 0;

	if (job_resrcs_ptr->nhosts == 0)
		return cpu_count;	/* no work to do */
	if (job_resrcs_ptr->cpus == NULL) {
		error("build_job_resources_cpu_array: cpus==NULL");
		return -1;
	}

	/* clear vestigial data and create new arrays of max size */
	job_resrcs_ptr->cpu_array_cnt = 0;
	xfree(job_resrcs_ptr->cpu_array_reps);
	job_resrcs_ptr->cpu_array_reps =
		xmalloc(job_resrcs_ptr->nhosts * sizeof(uint32_t));
	xfree(job_resrcs_ptr->cpu_array_value);
	job_resrcs_ptr->cpu_array_value =
		xmalloc(job_resrcs_ptr->nhosts * sizeof(uint16_t));

	for (i=0; i<job_resrcs_ptr->nhosts; i++) {
		if (job_resrcs_ptr->cpus[i] != last_cpu_cnt) {
			last_cpu_cnt = job_resrcs_ptr->cpus[i];
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
		cpu_count += last_cpu_cnt;
	}
	return cpu_count;
}

/* Rebuild cpus array based upon the values of nhosts, cpu_array_value and
 * cpu_array_reps in an existing data structure
 * Return total CPU count or -1 on error */
extern int build_job_resources_cpus_array(job_resources_t *job_resrcs_ptr)
{
	int cpu_count = 0, cpu_inx, i, j;

	if (job_resrcs_ptr->nhosts == 0)
		return cpu_count;	/* no work to do */
	if (job_resrcs_ptr->cpu_array_cnt == 0) {
		error("build_job_resources_cpus_array: cpu_array_cnt==0");
		return -1;
	}
	if (job_resrcs_ptr->cpu_array_value == NULL) {
		error("build_job_resources_cpus_array: cpu_array_value==NULL");
		return -1;
	}
	if (job_resrcs_ptr->cpu_array_reps == NULL) {
		error("build_job_resources_cpus_array: cpu_array_reps==NULL");
		return -1;
	}

	/* clear vestigial data and create new arrays of max size */
	xfree(job_resrcs_ptr->cpus);
	job_resrcs_ptr->cpus =
		xmalloc(job_resrcs_ptr->nhosts * sizeof(uint16_t));

	cpu_inx = 0;
	for (i=0; i<job_resrcs_ptr->cpu_array_cnt; i++) {
		for (j=0; j<job_resrcs_ptr->cpu_array_reps[i]; j++) {
			if (cpu_inx >= job_resrcs_ptr->nhosts) {
				error("build_job_resources_cpus_array: "
				      "cpu_array is too long");
				return -1;
			}
			cpu_count += job_resrcs_ptr->cpus[i];
			job_resrcs_ptr->cpus[cpu_inx++] =
				job_resrcs_ptr->cpus[i];
		}
	}
	if (cpu_inx < job_resrcs_ptr->nhosts) {
		error("build_job_resources_cpus_array: "
		      "cpu_array is incomplete");
		return -1;
	}
	return cpu_count;
}

/* Reset the node_bitmap in a job_resources data structure
 * This is needed after a restart/reconfiguration since nodes can
 * be added or removed from the system resulting in changing in
 * the bitmap size or bit positions */
extern void reset_node_bitmap(job_resources_t *job_resrcs_ptr,
			      bitstr_t *new_node_bitmap)
{
	if (job_resrcs_ptr) {
		if (job_resrcs_ptr->node_bitmap)
			bit_free(job_resrcs_ptr->node_bitmap);
		if (new_node_bitmap) {
			job_resrcs_ptr->node_bitmap =
				bit_copy(new_node_bitmap);
		}
	}
}

extern int valid_job_resources(job_resources_t *job_resrcs,
			       void *node_rec_table,
			       uint16_t fast_schedule)
{
	int i, bitmap_len;
	int sock_inx = 0, sock_cnt = 0;
	uint32_t cores, socks;
	struct node_record *node_ptr, *node_record_table;

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

	node_record_table = (struct node_record *) node_rec_table;
	bitmap_len = bit_size(job_resrcs->node_bitmap);
	for (i=0; i<bitmap_len; i++) {
		if (!bit_test(job_resrcs->node_bitmap, i))
			continue;
		node_ptr = node_record_table + i;
		if (fast_schedule) {
			socks = node_ptr->config_ptr->sockets;
			cores = node_ptr->config_ptr->cores;
		} else {
			socks = node_ptr->sockets;
			cores = node_ptr->cores;
		}
		if (sock_cnt >= job_resrcs->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_cnt = 0;
		}
		if ((socks != job_resrcs->sockets_per_node[sock_inx]) ||
		    (cores != job_resrcs->cores_per_socket[sock_inx])) {
			error("valid_job_resources: "
			      "%s sockets:%u,%u, cores %u,%u",
			      node_ptr->name,
			      socks,
			      job_resrcs->sockets_per_node[sock_inx],
			      cores,
			      job_resrcs->cores_per_socket[sock_inx]);
			return SLURM_ERROR;
		}
		sock_cnt++;
	}
	return SLURM_SUCCESS;
}

extern job_resources_t *copy_job_resources(
	job_resources_t *job_resrcs_ptr)
{
	int i, sock_inx = 0;
	job_resources_t *new_layout = xmalloc(sizeof(struct job_resources));

	xassert(job_resrcs_ptr);
	new_layout->nhosts = job_resrcs_ptr->nhosts;
	new_layout->nprocs = job_resrcs_ptr->nprocs;
	new_layout->node_req = job_resrcs_ptr->node_req;
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
			xmalloc(sizeof(uint32_t) *
				job_resrcs_ptr->cpu_array_cnt);
		memcpy(new_layout->cpu_array_reps,
		       job_resrcs_ptr->cpu_array_reps,
		       (sizeof(uint32_t) * job_resrcs_ptr->cpu_array_cnt));
	}
	if (job_resrcs_ptr->cpu_array_value &&
	    job_resrcs_ptr->cpu_array_cnt) {
		new_layout->cpu_array_value =
			xmalloc(sizeof(uint16_t) *
				job_resrcs_ptr->cpu_array_cnt);
		memcpy(new_layout->cpu_array_value,
		       job_resrcs_ptr->cpu_array_value,
		       (sizeof(uint16_t) * job_resrcs_ptr->cpu_array_cnt));
	}

	if (job_resrcs_ptr->cpus) {
		new_layout->cpus = xmalloc(sizeof(uint16_t) *
					   job_resrcs_ptr->nhosts);
		memcpy(new_layout->cpus, job_resrcs_ptr->cpus,
		       (sizeof(uint16_t) * job_resrcs_ptr->nhosts));
	}
	if (job_resrcs_ptr->cpus_used) {
		new_layout->cpus_used = xmalloc(sizeof(uint16_t) *
						job_resrcs_ptr->nhosts);
		memcpy(new_layout->cpus_used, job_resrcs_ptr->cpus_used,
		       (sizeof(uint16_t) * job_resrcs_ptr->nhosts));
	}

	if (job_resrcs_ptr->memory_allocated) {
		new_layout->memory_allocated = xmalloc(sizeof(uint32_t) *
						       new_layout->nhosts);
		memcpy(new_layout->memory_allocated,
		       job_resrcs_ptr->memory_allocated,
		       (sizeof(uint32_t) * job_resrcs_ptr->nhosts));
	}
	if (job_resrcs_ptr->memory_used) {
		new_layout->memory_used = xmalloc(sizeof(uint32_t) *
						  new_layout->nhosts);
		memcpy(new_layout->memory_used,
		       job_resrcs_ptr->memory_used,
		       (sizeof(uint32_t) * job_resrcs_ptr->nhosts));
	}

	/* Copy sockets_per_node, cores_per_socket and core_sock_rep_count */
	new_layout->sockets_per_node = xmalloc(sizeof(uint16_t) *
					       new_layout->nhosts);
	new_layout->cores_per_socket = xmalloc(sizeof(uint16_t) *
					       new_layout->nhosts);
	new_layout->sock_core_rep_count = xmalloc(sizeof(uint32_t) *
						  new_layout->nhosts);
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
		if (job_resrcs_ptr->core_bitmap)
			bit_free(job_resrcs_ptr->core_bitmap);
		if (job_resrcs_ptr->core_bitmap_used)
			bit_free(job_resrcs_ptr->core_bitmap_used);
		xfree(job_resrcs_ptr->cores_per_socket);
		xfree(job_resrcs_ptr->cpu_array_reps);
		xfree(job_resrcs_ptr->cpu_array_value);
		xfree(job_resrcs_ptr->cpus);
		xfree(job_resrcs_ptr->cpus_used);
		xfree(job_resrcs_ptr->memory_allocated);
		xfree(job_resrcs_ptr->memory_used);
		if (job_resrcs_ptr->node_bitmap)
			bit_free(job_resrcs_ptr->node_bitmap);
		if (job_resrcs_ptr->node_hl)
			hostlist_destroy(job_resrcs_ptr->node_hl);
		xfree(job_resrcs_ptr->sock_core_rep_count);
		xfree(job_resrcs_ptr->sockets_per_node);
		xfree(job_resrcs_ptr);
		*job_resrcs_pptr = NULL;
	}
}

/* Log the contents of a job_resources data structure using info() */
extern void log_job_resources(uint32_t job_id,
			      job_resources_t *job_resrcs_ptr)
{
	int bit_inx = 0, bit_reps, i;
	int array_size, node_inx;
	int sock_inx = 0, sock_reps = 0;

	if (job_resrcs_ptr == NULL) {
		error("log_job_resources: job_resrcs_ptr is NULL");
		return;
	}

	info("====================");
	info("job_id:%u nhosts:%u nprocs:%u node_req:%u",
	     job_id, job_resrcs_ptr->nhosts, job_resrcs_ptr->nprocs,
	     job_resrcs_ptr->node_req);

	if (job_resrcs_ptr->cpus == NULL) {
		error("log_job_resources: cpus array is NULL");
		return;
	}
	if (job_resrcs_ptr->memory_allocated == NULL) {
		error("log_job_resources: memory array is NULL");
		return;
	}
	if ((job_resrcs_ptr->cores_per_socket == NULL) ||
	    (job_resrcs_ptr->sockets_per_node == NULL) ||
	    (job_resrcs_ptr->sock_core_rep_count == NULL)) {
		error("log_job_resources: socket/core array is NULL");
		return;
	}
	if (job_resrcs_ptr->core_bitmap == NULL) {
		error("log_job_resources: core_bitmap is NULL");
		return;
	}
	if (job_resrcs_ptr->core_bitmap_used == NULL) {
		error("log_job_resources: core_bitmap_used is NULL");
		return;
	}
	array_size = bit_size(job_resrcs_ptr->core_bitmap);

	/* Can only log node_bitmap from slurmctld, so don't bother here */
	for (node_inx=0; node_inx<job_resrcs_ptr->nhosts; node_inx++) {
		uint32_t cpus_used = 0, memory_allocated = 0, memory_used = 0;
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

		info("  Mem(MB):%u:%u  Sockets:%u  Cores:%u  CPUs:%u:%u",
		     memory_allocated, memory_used,
		     job_resrcs_ptr->sockets_per_node[sock_inx],
		     job_resrcs_ptr->cores_per_socket[sock_inx],
		     job_resrcs_ptr->cpus[node_inx],
		     cpus_used);

		bit_reps = job_resrcs_ptr->sockets_per_node[sock_inx] *
			job_resrcs_ptr->cores_per_socket[sock_inx];
		for (i=0; i<bit_reps; i++) {
			if (bit_inx >= array_size) {
				error("log_job_resources: array size wrong");
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

extern void pack_job_resources(job_resources_t *job_resrcs_ptr, Buf buffer)
{
	if (job_resrcs_ptr == NULL) {
		uint32_t empty = NO_VAL;
		pack32(empty, buffer);
		return;
	}

	xassert(job_resrcs_ptr->nhosts);

	pack32(job_resrcs_ptr->nhosts, buffer);
	pack32(job_resrcs_ptr->nprocs, buffer);
	pack8(job_resrcs_ptr->node_req, buffer);

	if (job_resrcs_ptr->cpu_array_reps)
		pack32_array(job_resrcs_ptr->cpu_array_reps,
			     job_resrcs_ptr->cpu_array_cnt, buffer);
	else
		pack32_array(job_resrcs_ptr->cpu_array_reps, 0, buffer);

	if (job_resrcs_ptr->cpu_array_value)
		pack16_array(job_resrcs_ptr->cpu_array_value,
			     job_resrcs_ptr->cpu_array_cnt, buffer);
	else
		pack16_array(job_resrcs_ptr->cpu_array_value, 0, buffer);

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
		pack32_array(job_resrcs_ptr->memory_allocated,
			     job_resrcs_ptr->nhosts, buffer);
	else
		pack32_array(job_resrcs_ptr->memory_allocated, 0, buffer);

	if (job_resrcs_ptr->memory_used)
		pack32_array(job_resrcs_ptr->memory_used,
			     job_resrcs_ptr->nhosts, buffer);
	else
		pack32_array(job_resrcs_ptr->memory_used, 0, buffer);

#ifndef HAVE_BG
	{
		int i;
		uint32_t core_cnt = 0, sock_recs = 0;
		xassert(job_resrcs_ptr->cores_per_socket);
		xassert(job_resrcs_ptr->sock_core_rep_count);
		xassert(job_resrcs_ptr->sockets_per_node);

		for (i=0; i<job_resrcs_ptr->nhosts; i++) {
			core_cnt += job_resrcs_ptr->sockets_per_node[i] *
				job_resrcs_ptr->cores_per_socket[i] *
				job_resrcs_ptr->sock_core_rep_count[i];
			sock_recs += job_resrcs_ptr->sock_core_rep_count[i];
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
		pack_bit_str(job_resrcs_ptr->core_bitmap, buffer);
		pack_bit_str(job_resrcs_ptr->core_bitmap_used, buffer);
		/* Do not pack the node_bitmap, but rebuild it in
		 * reset_node_bitmap() based upon job_ptr->nodes and
		 * the current node table */
	}
#endif
}

extern int unpack_job_resources(job_resources_t **job_resrcs_pptr, 
				char *nodelist, Buf buffer)
{
	char *bit_fmt = NULL;
	uint32_t empty, tmp32;
	job_resources_t *job_resrcs;

	xassert(job_resrcs_pptr);
	safe_unpack32(&empty, buffer);
	if (empty == NO_VAL) {
		*job_resrcs_pptr = NULL;
		return SLURM_SUCCESS;
	}

	job_resrcs = xmalloc(sizeof(struct job_resources));
	job_resrcs->nhosts = empty;
	safe_unpack32(&job_resrcs->nprocs, buffer);
	safe_unpack8(&job_resrcs->node_req, buffer);

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

	safe_unpack32_array(&job_resrcs->memory_allocated,
			    &tmp32, buffer);
	if (tmp32 == 0)
		xfree(job_resrcs->memory_allocated);
	safe_unpack32_array(&job_resrcs->memory_used, &tmp32, buffer);
	if (tmp32 == 0)
		xfree(job_resrcs->memory_used);

#ifndef HAVE_BG
	safe_unpack16_array(&job_resrcs->sockets_per_node, &tmp32, buffer);
	if (tmp32 == 0)
		xfree(job_resrcs->sockets_per_node);
	safe_unpack16_array(&job_resrcs->cores_per_socket, &tmp32, buffer);
	if (tmp32 == 0)
		xfree(job_resrcs->cores_per_socket);
	safe_unpack32_array(&job_resrcs->sock_core_rep_count,
			    &tmp32, buffer);
	if (tmp32 == 0)
		xfree(job_resrcs->sock_core_rep_count);

	unpack_bit_str(&job_resrcs->core_bitmap, buffer);
	unpack_bit_str(&job_resrcs->core_bitmap_used, buffer);
	/* node_bitmap is not packed, but rebuilt in reset_node_bitmap()
	 * based upon job_ptr->nodes and the current node table */
#endif
	if(nodelist)
		job_resrcs->node_hl = hostlist_create(nodelist);


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
		} else if (socket_id >= job_resrcs_ptr->
			   sockets_per_node[i]) {
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

extern int set_job_resources_node(job_resources_t *job_resrcs_ptr,
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
		error("set_job_resources_node: core_cnt=0");
		return SLURM_ERROR;
	}

	i = bit_size(job_resrcs_ptr->core_bitmap);
	if ((bit_inx + core_cnt) > i) {
		error("set_job_resources_node: offset > bitmap size "
		      "(%d >= %d)", (bit_inx + core_cnt), i);
		return SLURM_ERROR;
	}

	for (i=0; i<core_cnt; i++)
		bit_set(job_resrcs_ptr->core_bitmap, bit_inx++);

	return SLURM_SUCCESS;
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

/* Return 1 if the given job can fit into the given full-length core_bitmap,
 * else return 0.
 */
extern int job_fits_into_cores(job_resources_t *job_resrcs_ptr,
			       bitstr_t *full_bitmap,
			       const uint16_t *bits_per_node,
			       const uint32_t *bit_rep_count)
{
	uint32_t i, n, count = 1, last_bit = 0;
	uint32_t c = 0, j = 0, k = 0;

	if (!full_bitmap)
		return 1;

	for (i = 0, n = 0; i < job_resrcs_ptr->nhosts; n++) {
		last_bit += bits_per_node[k];
		if (++count > bit_rep_count[k]) {
			k++;
			count = 1;
		}
		if (bit_test(job_resrcs_ptr->node_bitmap, n) == 0) {
			c = last_bit;
			continue;
		}
		for (; c < last_bit; c++, j++) {
			if (bit_test(full_bitmap, c) &&
			    bit_test(job_resrcs_ptr->core_bitmap, j))
				return 0;
		}
		i++;
	}
	return 1;
}

/* add the given job to the given full_core_bitmap */
extern void add_job_to_cores(job_resources_t *job_resrcs_ptr,
			     bitstr_t **full_core_bitmap,
			     const uint16_t *cores_per_node,
			     const uint32_t *core_rep_count)
{
	uint32_t i, n, count = 1, last_bit = 0;
	uint32_t c = 0, j = 0, k = 0;

	if (!job_resrcs_ptr->core_bitmap)
		return;

	/* add the job to the row_bitmap */
	if (*full_core_bitmap == NULL) {
		uint32_t size = 0;
		for (i = 0; core_rep_count[i]; i++) {
			size += cores_per_node[i] * core_rep_count[i];
		}
		*full_core_bitmap = bit_alloc(size);
		if (!*full_core_bitmap)
			fatal("add_job_to_cores: bitmap memory error");
	}

	for (i = 0, n = 0; i < job_resrcs_ptr->nhosts; n++) {
		last_bit += cores_per_node[k];
		if (++count > core_rep_count[k]) {
			k++;
			count = 1;
		}
		if (bit_test(job_resrcs_ptr->node_bitmap, n) == 0) {
			c = last_bit;
			continue;
		}
		for (; c < last_bit; c++, j++) {
			if (bit_test(job_resrcs_ptr->core_bitmap, j))
				bit_set(*full_core_bitmap, c);
		}
		i++;
	}
}
